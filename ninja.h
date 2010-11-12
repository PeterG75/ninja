#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include <assert.h>

using namespace std;

#include "eval_env.h"

int ReadFile(const string& path, string* contents, string* err);

struct DiskInterface {
  // stat() a file, returning the mtime, or 0 if missing and -1 on other errors.
  virtual int Stat(const string& path) = 0;
  // Create a directory, returning false on failure.
  virtual bool MakeDir(const string& path) = 0;
  // Read a file to a string.  Fill in |err| on error.
  virtual string ReadFile(const string& path, string* err) = 0;

  // Create all the parent directories for path; like mkdir -p `basename path`.
  bool MakeDirs(const string& path);
};

struct RealDiskInterface : public DiskInterface {
  virtual int Stat(const string& path);
  virtual bool MakeDir(const string& path);
  virtual string ReadFile(const string& path, string* err);
};

struct Node;
struct FileStat {
  FileStat(const string& path) : path_(path), mtime_(-1), node_(NULL) {}
  void Touch(int mtime);
  // Return true if the file exists (mtime_ got a value).
  bool Stat(DiskInterface* disk_interface);

  // Return true if we needed to stat.
  bool StatIfNecessary(DiskInterface* disk_interface) {
    if (status_known())
      return false;
    Stat(disk_interface);
    return true;
  }

  bool exists() const {
    assert(status_known());
    return mtime_ != 0;
  }

  bool status_known() const {
    return mtime_ != -1;
  }

  string path_;
  // Possible values of mtime_:
  //   -1: file hasn't been examined
  //   0:  we looked, and file doesn't exist
  //   >0: actual file's mtime
  time_t mtime_;
  Node* node_;
};

struct Edge;
struct Node {
  Node(FileStat* file) : file_(file), dirty_(false), in_edge_(NULL) {}

  bool dirty() const { return dirty_; }
  void MarkDirty();
  void MarkDependentsDirty();

  FileStat* file_;
  bool dirty_;
  Edge* in_edge_;
  vector<Edge*> out_edges_;
};

struct Rule {
  Rule(const string& name) : name_(name) { }

  bool ParseCommand(const string& command, string* err) {
    return command_.Parse(command, err);
  }
  string name_;
  EvalString command_;
  EvalString depfile_;
};

class State;
struct Edge {
  Edge() : rule_(NULL), env_(NULL), implicit_deps_(0), order_only_deps_(0) {}

  void MarkDirty(Node* node);
  bool RecomputeDirty(State* state, DiskInterface* disk_interface, string* err);
  string EvaluateCommand();  // XXX move to env, take env ptr
  bool LoadDepFile(State* state, DiskInterface* disk_interface, string* err);

  void Dump();

  enum InOut { IN, OUT };

  Rule* rule_;
  vector<Node*> inputs_;
  vector<Node*> outputs_;
  EvalString::Env* env_;

  // XXX There are three types of inputs.
  // 1) explicit deps, which show up as $in on the command line;
  // 2) implicit deps, which the target depends on implicitly (e.g. C headers),
  //                   and changes in them cause the target to rebuild;
  // 3) order-only deps, which are needed before the target builds but which
  //                     don't cause the target to rebuild.
  // Currently we stuff all of these into inputs_ and keep counts of #2 and #3
  // when we need to compute subsets.  This is suboptimal; should think of a
  // better representation.  (Could make each pointer into a pair of a pointer
  // and a type of input, or if memory matters could use the low bits of the
  // pointer...)
  int implicit_deps_;
  int order_only_deps_;
  bool is_order_only(int index) {
    return index >= ((int)inputs_.size()) - order_only_deps_;
  }
};

struct StatCache {
  typedef map<string, FileStat*> Paths;
  Paths paths_;
  FileStat* GetFile(const string& path);
  void Dump();
  void Reload();
};
struct State : public EvalString::Env {
  StatCache stat_cache_;
  map<string, Rule*> rules_;
  vector<Edge*> edges_;
  map<string, string> env_;

  StatCache* stat_cache() { return &stat_cache_; }

  // EvalString::Env impl
  virtual string Evaluate(const string& var);

  void AddRule(Rule* rule);
  Rule* LookupRule(const string& rule_name);
  Edge* AddEdge(Rule* rule);
  Node* GetNode(const string& path);
  Node* LookupNode(const string& path);
  void AddInOut(Edge* edge, Edge::InOut inout, const string& path);
  void AddBinding(const string& key, const string& val);
};

struct Plan {
  bool AddTarget(Node* node, string* err);

  Edge* FindWork();
  void EdgeFinished(Edge* edge);

  bool more_to_do() const { return !want_.empty(); }

private:
  void NodeFinished(Node* node);

  set<Node*> want_;
  queue<Edge*> ready_;
};


struct Shell {
  virtual ~Shell() {}
  virtual bool RunCommand(Edge* edge);
};

struct Builder {
  Builder(State* state)
      : state_(state), disk_interface_(&default_disk_interface_) {}
  virtual ~Builder() {}

  Node* AddTarget(const string& name, string* err);
  bool Build(Shell* shell, string* err);

  State* state_;
  Plan plan_;
  RealDiskInterface default_disk_interface_;
  DiskInterface* disk_interface_;
};
