//===- xray-stacks.cpp: XRay Function Call Stack Accounting ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements stack-based accounting. It takes XRay traces, and
// collates statistics across these traces to show a breakdown of time spent
// at various points of the stack to provide insight into which functions
// spend the most time in terms of a call stack. We provide a few
// sorting/filtering options for zero'ing in on the useful stacks.
//
//===----------------------------------------------------------------------===//

#include <forward_list>
#include <numeric>

#include "func-id-helper.h"
#include "trie-node.h"
#include "xray-registry.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/XRay/Graph.h"
#include "llvm/XRay/InstrumentationMap.h"
#include "llvm/XRay/Trace.h"

using namespace llvm;
using namespace llvm::xray;

static cl::SubCommand Stack("stack", "Call stack accounting");
static cl::list<std::string> StackInputs(cl::Positional,
                                         cl::desc("<xray trace>"), cl::Required,
                                         cl::sub(Stack), cl::OneOrMore);

static cl::opt<bool>
    StackKeepGoing("keep-going", cl::desc("Keep going on errors encountered"),
                   cl::sub(Stack), cl::init(false));
static cl::alias StackKeepGoing2("k", cl::aliasopt(StackKeepGoing),
                                 cl::desc("Alias for -keep-going"),
                                 cl::sub(Stack));

// TODO: Does there need to be an option to deduce tail or sibling calls?

static cl::opt<std::string> StacksInstrMap(
    "instr_map",
    cl::desc("instrumentation map used to identify function ids. "
             "Currently supports elf file instrumentation maps."),
    cl::sub(Stack), cl::init(""));
static cl::alias StacksInstrMap2("m", cl::aliasopt(StacksInstrMap),
                                 cl::desc("Alias for -instr_map"),
                                 cl::sub(Stack));

static cl::opt<bool>
    SeparateThreadStacks("per-thread-stacks",
                         cl::desc("Report top stacks within each thread id"),
                         cl::sub(Stack), cl::init(false));

static cl::opt<bool>
    AggregateThreads("aggregate-threads",
                     cl::desc("Aggregate stack times across threads"),
                     cl::sub(Stack), cl::init(false));

static cl::opt<bool>
    DumpAllStacks("all-stacks",
                  cl::desc("Dump sum of timings for all stacks. "
                           "By default separates stacks per-thread."),
                  cl::sub(Stack), cl::init(false));
static cl::alias DumpAllStacksShort("all", cl::aliasopt(DumpAllStacks),
                                    cl::desc("Alias for -all-stacks"),
                                    cl::sub(Stack));

// TODO(kpw): Add other interesting formats. Perhaps chrome trace viewer format
// possibly with aggregations or just a linear trace of timings.
enum StackOutputFormat { HUMAN, FLAMETOOL };

static cl::opt<StackOutputFormat> StacksOutputFormat(
    "stack-format",
    cl::desc("The format that output stacks should be "
             "output in. Only applies with all-stacks."),
    cl::values(
        clEnumValN(HUMAN, "human",
                   "Human readable output. Only valid without -all-stacks."),
        clEnumValN(FLAMETOOL, "flame",
                   "Format consumable by Brendan Gregg's FlameGraph tool. "
                   "Only valid with -all-stacks.")),
    cl::sub(Stack), cl::init(HUMAN));

// Types of values for each stack in a CallTrie.
enum class AggregationType {
  TOTAL_TIME,      // The total time spent in a stack and its callees.
  INVOCATION_COUNT // The number of times the stack was invoked.
};

static cl::opt<AggregationType> RequestedAggregation(
    "aggregation-type",
    cl::desc("The type of aggregation to do on call stacks."),
    cl::values(
        clEnumValN(
            AggregationType::TOTAL_TIME, "time",
            "Capture the total time spent in an all invocations of a stack."),
        clEnumValN(AggregationType::INVOCATION_COUNT, "count",
                   "Capture the number of times a stack was invoked. "
                   "In flamegraph mode, this count also includes invocations "
                   "of all callees.")),
    cl::sub(Stack), cl::init(AggregationType::TOTAL_TIME));

/// A helper struct to work with formatv and XRayRecords. Makes it easier to
/// use instrumentation map names or addresses in formatted output.
struct format_xray_record : public FormatAdapter<XRayRecord> {
  explicit format_xray_record(XRayRecord record,
                              const FuncIdConversionHelper &conv)
      : FormatAdapter<XRayRecord>(std::move(record)), Converter(&conv) {}
  void format(raw_ostream &Stream, StringRef Style) override {
    Stream << formatv(
        "{FuncId: \"{0}\", ThreadId: \"{1}\", RecordType: \"{2}\"}",
        Converter->SymbolOrNumber(Item.FuncId), Item.TId,
        DecodeRecordType(Item.RecordType));
  }

private:
  Twine DecodeRecordType(uint16_t recordType) {
    switch (recordType) {
    case 0:
      return Twine("Fn Entry");
    case 1:
      return Twine("Fn Exit");
    default:
      // TODO: Add Tail exit when it is added to llvm/XRay/XRayRecord.h
      return Twine("Unknown");
    }
  }

  const FuncIdConversionHelper *Converter;
};

/// The stack command will take a set of XRay traces as arguments, and collects
/// information about the stacks of instrumented functions that appear in the
/// traces. We track the following pieces of information:
///
///   - Total time: amount of time/cycles accounted for in the traces.
///   - Stack count: number of times a specific stack appears in the
///     traces. Only instrumented functions show up in stacks.
///   - Cumulative stack time: amount of time spent in a stack accumulated
///     across the invocations in the traces.
///   - Cumulative local time: amount of time spent in each instrumented
///     function showing up in a specific stack, accumulated across the traces.
///
/// Example output for the kind of data we'd like to provide looks like the
/// following:
///
///   Total time: 3.33234 s
///   Stack ID: ...
///   Stack Count: 2093
///   #     Function                  Local Time     (%)      Stack Time     (%)
///   0     main                         2.34 ms   0.07%      3.33234  s    100%
///   1     foo()                     3.30000  s  99.02%         3.33  s  99.92%
///   2     bar()                          30 ms   0.90%           30 ms   0.90%
///
/// We can also show distributions of the function call durations with
/// statistics at each level of the stack. This works by doing the following
/// algorithm:
///
///   1. When unwinding, record the duration of each unwound function associated
///   with the path up to which the unwinding stops. For example:
///
///        Step                         Duration (? means has start time)
///
///        push a <start time>           a = ?
///        push b <start time>           a = ?, a->b = ?
///        push c <start time>           a = ?, a->b = ?, a->b->c = ?
///        pop  c <end time>             a = ?, a->b = ?, emit duration(a->b->c)
///        pop  b <end time>             a = ?, emit duration(a->b)
///        push c <start time>           a = ?, a->c = ?
///        pop  c <end time>             a = ?, emit duration(a->c)
///        pop  a <end time>             emit duration(a)
///
///   2. We then account for the various stacks we've collected, and for each of
///      them will have measurements that look like the following (continuing
///      with the above simple example):
///
///        c : [<id("a->b->c"), [durations]>, <id("a->c"), [durations]>]
///        b : [<id("a->b"), [durations]>]
///        a : [<id("a"), [durations]>]
///
///      This allows us to compute, for each stack id, and each function that
///      shows up in the stack,  some important statistics like:
///
///        - median
///        - 99th percentile
///        - mean + stddev
///        - count
///
///   3. For cases where we don't have durations for some of the higher levels
///   of the stack (perhaps instrumentation wasn't activated when the stack was
///   entered), we can mark them appropriately.
///
///  Computing this data also allows us to implement lookup by call stack nodes,
///  so that we can find functions that show up in multiple stack traces and
///  show the statistical properties of that function in various contexts. We
///  can compute information similar to the following:
///
///    Function: 'c'
///    Stacks: 2 / 2
///    Stack ID: ...
///    Stack Count: ...
///    #     Function  ...
///    0     a         ...
///    1     b         ...
///    2     c         ...
///
///    Stack ID: ...
///    Stack Count: ...
///    #     Function  ...
///    0     a         ...
///    1     c         ...
///    ----------------...
///
///    Function: 'b'
///    Stacks:  1 / 2
///    Stack ID: ...
///    Stack Count: ...
///    #     Function  ...
///    0     a         ...
///    1     b         ...
///    2     c         ...
///
///
/// To do this we require a Trie data structure that will allow us to represent
/// all the call stacks of instrumented functions in an easily traversible
/// manner when we do the aggregations and lookups. For instrumented call
/// sequences like the following:
///
///   a()
///    b()
///     c()
///     d()
///    c()
///
/// We will have a representation like so:
///
///   a -> b -> c
///   |    |
///   |    +--> d
///   |
///   +--> c
///
/// We maintain a sequence of durations on the leaves and in the internal nodes
/// as we go through and process every record from the XRay trace. We also
/// maintain an index of unique functions, and provide a means of iterating
/// through all the instrumented call stacks which we know about.

struct StackDuration {
  llvm::SmallVector<int64_t, 4> TerminalDurations;
  llvm::SmallVector<int64_t, 4> IntermediateDurations;
};

StackDuration mergeStackDuration(const StackDuration &Left,
                                 const StackDuration &Right) {
  StackDuration Data{};
  Data.TerminalDurations.reserve(Left.TerminalDurations.size() +
                                 Right.TerminalDurations.size());
  Data.IntermediateDurations.reserve(Left.IntermediateDurations.size() +
                                     Right.IntermediateDurations.size());
  // Aggregate the durations.
  for (auto duration : Left.TerminalDurations)
    Data.TerminalDurations.push_back(duration);
  for (auto duration : Right.TerminalDurations)
    Data.TerminalDurations.push_back(duration);

  for (auto duration : Left.IntermediateDurations)
    Data.IntermediateDurations.push_back(duration);
  for (auto duration : Right.IntermediateDurations)
    Data.IntermediateDurations.push_back(duration);
  return Data;
}

using StackTrieNode = TrieNode<StackDuration>;

template <AggregationType AggType>
std::size_t GetValueForStack(const StackTrieNode *Node);

// When computing total time spent in a stack, we're adding the timings from
// its callees and the timings from when it was a leaf.
template <>
std::size_t
GetValueForStack<AggregationType::TOTAL_TIME>(const StackTrieNode *Node) {
  auto TopSum = std::accumulate(Node->ExtraData.TerminalDurations.begin(),
                                Node->ExtraData.TerminalDurations.end(), 0uLL);
  return std::accumulate(Node->ExtraData.IntermediateDurations.begin(),
                         Node->ExtraData.IntermediateDurations.end(), TopSum);
}

// Calculates how many times a function was invoked.
// TODO: Hook up option to produce stacks
template <>
std::size_t
GetValueForStack<AggregationType::INVOCATION_COUNT>(const StackTrieNode *Node) {
  return Node->ExtraData.TerminalDurations.size() +
         Node->ExtraData.IntermediateDurations.size();
}

// Make sure there are implementations for each enum value.
template <AggregationType T> struct DependentFalseType : std::false_type {};

template <AggregationType AggType>
std::size_t GetValueForStack(const StackTrieNode *Node) {
  static_assert(DependentFalseType<AggType>::value,
                "No implementation found for aggregation type provided.");
  return 0;
}

class StackTrie {
  // Avoid the magic number of 4 propagated through the code with an alias.
  // We use this SmallVector to track the root nodes in a call graph.
  using RootVector = SmallVector<StackTrieNode *, 4>;

  // We maintain pointers to the roots of the tries we see.
  DenseMap<uint32_t, RootVector> Roots;

  // We make sure all the nodes are accounted for in this list.
  std::forward_list<StackTrieNode> NodeStore;

  // A map of thread ids to pairs call stack trie nodes and their start times.
  DenseMap<uint32_t, SmallVector<std::pair<StackTrieNode *, uint64_t>, 8>>
      ThreadStackMap;

  StackTrieNode *createTrieNode(uint32_t ThreadId, int32_t FuncId,
                                StackTrieNode *Parent) {
    NodeStore.push_front(StackTrieNode{FuncId, Parent, {}, {{}, {}}});
    auto I = NodeStore.begin();
    auto *Node = &*I;
    if (!Parent)
      Roots[ThreadId].push_back(Node);
    return Node;
  }

  StackTrieNode *findRootNode(uint32_t ThreadId, int32_t FuncId) {
    const auto &RootsByThread = Roots[ThreadId];
    auto I = find_if(RootsByThread,
                     [&](StackTrieNode *N) { return N->FuncId == FuncId; });
    return (I == RootsByThread.end()) ? nullptr : *I;
  }

public:
  enum class AccountRecordStatus {
    OK,              // Successfully processed
    ENTRY_NOT_FOUND, // An exit record had no matching call stack entry
    UNKNOWN_RECORD_TYPE
  };

  struct AccountRecordState {
    // We keep track of whether the call stack is currently unwinding.
    bool wasLastRecordExit;

    static AccountRecordState CreateInitialState() { return {false}; }
  };

  AccountRecordStatus accountRecord(const XRayRecord &R,
                                    AccountRecordState *state) {
    auto &TS = ThreadStackMap[R.TId];
    switch (R.Type) {
    case RecordTypes::CUSTOM_EVENT:
    case RecordTypes::TYPED_EVENT:
      return AccountRecordStatus::OK;
    case RecordTypes::ENTER:
    case RecordTypes::ENTER_ARG: {
      state->wasLastRecordExit = false;
      // When we encounter a new function entry, we want to record the TSC for
      // that entry, and the function id. Before doing so we check the top of
      // the stack to see if there are callees that already represent this
      // function.
      if (TS.empty()) {
        auto *Root = findRootNode(R.TId, R.FuncId);
        TS.emplace_back(Root ? Root : createTrieNode(R.TId, R.FuncId, nullptr),
                        R.TSC);
        return AccountRecordStatus::OK;
      }

      auto &Top = TS.back();
      auto I = find_if(Top.first->Callees,
                       [&](StackTrieNode *N) { return N->FuncId == R.FuncId; });
      if (I == Top.first->Callees.end()) {
        // We didn't find the callee in the stack trie, so we're going to
        // add to the stack then set up the pointers properly.
        auto N = createTrieNode(R.TId, R.FuncId, Top.first);
        Top.first->Callees.emplace_back(N);

        // Top may be invalidated after this statement.
        TS.emplace_back(N, R.TSC);
      } else {
        // We found the callee in the stack trie, so we'll use that pointer
        // instead, add it to the stack associated with the TSC.
        TS.emplace_back(*I, R.TSC);
      }
      return AccountRecordStatus::OK;
    }
    case RecordTypes::EXIT:
    case RecordTypes::TAIL_EXIT: {
      bool wasLastRecordExit = state->wasLastRecordExit;
      state->wasLastRecordExit = true;
      // The exit case is more interesting, since we want to be able to deduce
      // missing exit records. To do that properly, we need to look up the stack
      // and see whether the exit record matches any of the entry records. If it
      // does match, we attempt to record the durations as we pop the stack to
      // where we see the parent.
      if (TS.empty()) {
        // Short circuit, and say we can't find it.

        return AccountRecordStatus::ENTRY_NOT_FOUND;
      }

      auto FunctionEntryMatch = find_if(
          reverse(TS), [&](const std::pair<StackTrieNode *, uint64_t> &E) {
            return E.first->FuncId == R.FuncId;
          });
      auto status = AccountRecordStatus::OK;
      if (FunctionEntryMatch == TS.rend()) {
        status = AccountRecordStatus::ENTRY_NOT_FOUND;
      } else {
        // Account for offset of 1 between reverse and forward iterators. We
        // want the forward iterator to include the function that is exited.
        ++FunctionEntryMatch;
      }
      auto I = FunctionEntryMatch.base();
      for (auto &E : make_range(I, TS.end() - 1))
        E.first->ExtraData.IntermediateDurations.push_back(
            std::max(E.second, R.TSC) - std::min(E.second, R.TSC));
      auto &Deepest = TS.back();
      if (wasLastRecordExit)
        Deepest.first->ExtraData.IntermediateDurations.push_back(
            std::max(Deepest.second, R.TSC) - std::min(Deepest.second, R.TSC));
      else
        Deepest.first->ExtraData.TerminalDurations.push_back(
            std::max(Deepest.second, R.TSC) - std::min(Deepest.second, R.TSC));
      TS.erase(I, TS.end());
      return status;
    }
    }
    return AccountRecordStatus::UNKNOWN_RECORD_TYPE;
  }

  bool isEmpty() const { return Roots.empty(); }

  void printStack(raw_ostream &OS, const StackTrieNode *Top,
                  FuncIdConversionHelper &FN) {
    // Traverse the pointers up to the parent, noting the sums, then print
    // in reverse order (callers at top, callees down bottom).
    SmallVector<const StackTrieNode *, 8> CurrentStack;
    for (auto *F = Top; F != nullptr; F = F->Parent)
      CurrentStack.push_back(F);
    int Level = 0;
    OS << formatv("{0,-5} {1,-60} {2,+12} {3,+16}\n", "lvl", "function",
                  "count", "sum");
    for (auto *F :
         reverse(make_range(CurrentStack.begin() + 1, CurrentStack.end()))) {
      auto Sum = std::accumulate(F->ExtraData.IntermediateDurations.begin(),
                                 F->ExtraData.IntermediateDurations.end(), 0LL);
      auto FuncId = FN.SymbolOrNumber(F->FuncId);
      OS << formatv("#{0,-4} {1,-60} {2,+12} {3,+16}\n", Level++,
                    FuncId.size() > 60 ? FuncId.substr(0, 57) + "..." : FuncId,
                    F->ExtraData.IntermediateDurations.size(), Sum);
    }
    auto *Leaf = *CurrentStack.begin();
    auto LeafSum =
        std::accumulate(Leaf->ExtraData.TerminalDurations.begin(),
                        Leaf->ExtraData.TerminalDurations.end(), 0LL);
    auto LeafFuncId = FN.SymbolOrNumber(Leaf->FuncId);
    OS << formatv("#{0,-4} {1,-60} {2,+12} {3,+16}\n", Level++,
                  LeafFuncId.size() > 60 ? LeafFuncId.substr(0, 57) + "..."
                                         : LeafFuncId,
                  Leaf->ExtraData.TerminalDurations.size(), LeafSum);
    OS << "\n";
  }

  /// Prints top stacks for each thread.
  void printPerThread(raw_ostream &OS, FuncIdConversionHelper &FN) {
    for (auto iter : Roots) {
      OS << "Thread " << iter.first << ":\n";
      print(OS, FN, iter.second);
      OS << "\n";
    }
  }

  /// Prints timing sums for each stack in each threads.
  template <AggregationType AggType>
  void printAllPerThread(raw_ostream &OS, FuncIdConversionHelper &FN,
                         StackOutputFormat format) {
    for (auto iter : Roots) {
      uint32_t threadId = iter.first;
      RootVector &perThreadRoots = iter.second;
      bool reportThreadId = true;
      printAll<AggType>(OS, FN, perThreadRoots, threadId, reportThreadId);
    }
  }

  /// Prints top stacks from looking at all the leaves and ignoring thread IDs.
  /// Stacks that consist of the same function IDs but were called in different
  /// thread IDs are not considered unique in this printout.
  void printIgnoringThreads(raw_ostream &OS, FuncIdConversionHelper &FN) {
    RootVector RootValues;

    // Function to pull the values out of a map iterator.
    using RootsType = decltype(Roots.begin())::value_type;
    auto MapValueFn = [](const RootsType &Value) { return Value.second; };

    for (const auto &RootNodeRange :
         make_range(map_iterator(Roots.begin(), MapValueFn),
                    map_iterator(Roots.end(), MapValueFn))) {
      for (auto *RootNode : RootNodeRange)
        RootValues.push_back(RootNode);
    }

    print(OS, FN, RootValues);
  }

  /// Creates a merged list of Tries for unique stacks that disregards their
  /// thread IDs.
  RootVector mergeAcrossThreads(std::forward_list<StackTrieNode> &NodeStore) {
    RootVector MergedByThreadRoots;
    for (auto MapIter : Roots) {
      const auto &RootNodeVector = MapIter.second;
      for (auto *Node : RootNodeVector) {
        auto MaybeFoundIter =
            find_if(MergedByThreadRoots, [Node](StackTrieNode *elem) {
              return Node->FuncId == elem->FuncId;
            });
        if (MaybeFoundIter == MergedByThreadRoots.end()) {
          MergedByThreadRoots.push_back(Node);
        } else {
          MergedByThreadRoots.push_back(mergeTrieNodes(
              **MaybeFoundIter, *Node, nullptr, NodeStore, mergeStackDuration));
          MergedByThreadRoots.erase(MaybeFoundIter);
        }
      }
    }
    return MergedByThreadRoots;
  }

  /// Print timing sums for all stacks merged by Thread ID.
  template <AggregationType AggType>
  void printAllAggregatingThreads(raw_ostream &OS, FuncIdConversionHelper &FN,
                                  StackOutputFormat format) {
    std::forward_list<StackTrieNode> AggregatedNodeStore;
    RootVector MergedByThreadRoots = mergeAcrossThreads(AggregatedNodeStore);
    bool reportThreadId = false;
    printAll<AggType>(OS, FN, MergedByThreadRoots,
                      /*threadId*/ 0, reportThreadId);
  }

  /// Merges the trie by thread id before printing top stacks.
  void printAggregatingThreads(raw_ostream &OS, FuncIdConversionHelper &FN) {
    std::forward_list<StackTrieNode> AggregatedNodeStore;
    RootVector MergedByThreadRoots = mergeAcrossThreads(AggregatedNodeStore);
    print(OS, FN, MergedByThreadRoots);
  }

  // TODO: Add a format option when more than one are supported.
  template <AggregationType AggType>
  void printAll(raw_ostream &OS, FuncIdConversionHelper &FN,
                RootVector RootValues, uint32_t ThreadId, bool ReportThread) {
    SmallVector<const StackTrieNode *, 16> S;
    for (const auto *N : RootValues) {
      S.clear();
      S.push_back(N);
      while (!S.empty()) {
        auto *Top = S.pop_back_val();
        printSingleStack<AggType>(OS, FN, ReportThread, ThreadId, Top);
        for (const auto *C : Top->Callees)
          S.push_back(C);
      }
    }
  }

  /// Prints values for stacks in a format consumable for the flamegraph.pl
  /// tool. This is a line based format that lists each level in the stack
  /// hierarchy in a semicolon delimited form followed by a space and a numeric
  /// value. If breaking down by thread, the thread ID will be added as the
  /// root level of the stack.
  template <AggregationType AggType>
  void printSingleStack(raw_ostream &OS, FuncIdConversionHelper &Converter,
                        bool ReportThread, uint32_t ThreadId,
                        const StackTrieNode *Node) {
    if (ReportThread)
      OS << "thread_" << ThreadId << ";";
    SmallVector<const StackTrieNode *, 5> lineage{};
    lineage.push_back(Node);
    while (lineage.back()->Parent != nullptr)
      lineage.push_back(lineage.back()->Parent);
    while (!lineage.empty()) {
      OS << Converter.SymbolOrNumber(lineage.back()->FuncId) << ";";
      lineage.pop_back();
    }
    OS << " " << GetValueForStack<AggType>(Node) << "\n";
  }

  void print(raw_ostream &OS, FuncIdConversionHelper &FN,
             RootVector RootValues) {
    // Go through each of the roots, and traverse the call stack, producing the
    // aggregates as you go along. Remember these aggregates and stacks, and
    // show summary statistics about:
    //
    //   - Total number of unique stacks
    //   - Top 10 stacks by count
    //   - Top 10 stacks by aggregate duration
    SmallVector<std::pair<const StackTrieNode *, uint64_t>, 11>
        TopStacksByCount;
    SmallVector<std::pair<const StackTrieNode *, uint64_t>, 11> TopStacksBySum;
    auto greater_second =
        [](const std::pair<const StackTrieNode *, uint64_t> &A,
           const std::pair<const StackTrieNode *, uint64_t> &B) {
          return A.second > B.second;
        };
    uint64_t UniqueStacks = 0;
    for (const auto *N : RootValues) {
      SmallVector<const StackTrieNode *, 16> S;
      S.emplace_back(N);

      while (!S.empty()) {
        auto *Top = S.pop_back_val();

        // We only start printing the stack (by walking up the parent pointers)
        // when we get to a leaf function.
        if (!Top->ExtraData.TerminalDurations.empty()) {
          ++UniqueStacks;
          auto TopSum =
              std::accumulate(Top->ExtraData.TerminalDurations.begin(),
                              Top->ExtraData.TerminalDurations.end(), 0uLL);
          {
            auto E = std::make_pair(Top, TopSum);
            TopStacksBySum.insert(std::lower_bound(TopStacksBySum.begin(),
                                                   TopStacksBySum.end(), E,
                                                   greater_second),
                                  E);
            if (TopStacksBySum.size() == 11)
              TopStacksBySum.pop_back();
          }
          {
            auto E =
                std::make_pair(Top, Top->ExtraData.TerminalDurations.size());
            TopStacksByCount.insert(std::lower_bound(TopStacksByCount.begin(),
                                                     TopStacksByCount.end(), E,
                                                     greater_second),
                                    E);
            if (TopStacksByCount.size() == 11)
              TopStacksByCount.pop_back();
          }
        }
        for (const auto *C : Top->Callees)
          S.push_back(C);
      }
    }

    // Now print the statistics in the end.
    OS << "\n";
    OS << "Unique Stacks: " << UniqueStacks << "\n";
    OS << "Top 10 Stacks by leaf sum:\n\n";
    for (const auto &P : TopStacksBySum) {
      OS << "Sum: " << P.second << "\n";
      printStack(OS, P.first, FN);
    }
    OS << "\n";
    OS << "Top 10 Stacks by leaf count:\n\n";
    for (const auto &P : TopStacksByCount) {
      OS << "Count: " << P.second << "\n";
      printStack(OS, P.first, FN);
    }
    OS << "\n";
  }
};

std::string CreateErrorMessage(StackTrie::AccountRecordStatus Error,
                               const XRayRecord &Record,
                               const FuncIdConversionHelper &Converter) {
  switch (Error) {
  case StackTrie::AccountRecordStatus::ENTRY_NOT_FOUND:
    return formatv("Found record {0} with no matching function entry\n",
                   format_xray_record(Record, Converter));
  default:
    return formatv("Unknown error type for record {0}\n",
                   format_xray_record(Record, Converter));
  }
}

static CommandRegistration Unused(&Stack, []() -> Error {
  // Load each file provided as a command-line argument. For each one of them
  // account to a single StackTrie, and just print the whole trie for now.
  StackTrie ST;
  InstrumentationMap Map;
  if (!StacksInstrMap.empty()) {
    auto InstrumentationMapOrError = loadInstrumentationMap(StacksInstrMap);
    if (!InstrumentationMapOrError)
      return joinErrors(
          make_error<StringError>(
              Twine("Cannot open instrumentation map: ") + StacksInstrMap,
              std::make_error_code(std::errc::invalid_argument)),
          InstrumentationMapOrError.takeError());
    Map = std::move(*InstrumentationMapOrError);
  }

  if (SeparateThreadStacks && AggregateThreads)
    return make_error<StringError>(
        Twine("Can't specify options for per thread reporting and reporting "
              "that aggregates threads."),
        std::make_error_code(std::errc::invalid_argument));

  if (!DumpAllStacks && StacksOutputFormat != HUMAN)
    return make_error<StringError>(
        Twine("Can't specify a non-human format without -all-stacks."),
        std::make_error_code(std::errc::invalid_argument));

  if (DumpAllStacks && StacksOutputFormat == HUMAN)
    return make_error<StringError>(
        Twine("You must specify a non-human format when reporting with "
              "-all-stacks."),
        std::make_error_code(std::errc::invalid_argument));

  symbolize::LLVMSymbolizer::Options Opts(
      symbolize::FunctionNameKind::LinkageName, true, true, false, "");
  symbolize::LLVMSymbolizer Symbolizer(Opts);
  FuncIdConversionHelper FuncIdHelper(StacksInstrMap, Symbolizer,
                                      Map.getFunctionAddresses());
  // TODO: Someday, support output to files instead of just directly to
  // standard output.
  for (const auto &Filename : StackInputs) {
    auto TraceOrErr = loadTraceFile(Filename);
    if (!TraceOrErr) {
      if (!StackKeepGoing)
        return joinErrors(
            make_error<StringError>(
                Twine("Failed loading input file '") + Filename + "'",
                std::make_error_code(std::errc::invalid_argument)),
            TraceOrErr.takeError());
      logAllUnhandledErrors(TraceOrErr.takeError(), errs());
      continue;
    }
    auto &T = *TraceOrErr;
    StackTrie::AccountRecordState AccountRecordState =
        StackTrie::AccountRecordState::CreateInitialState();
    for (const auto &Record : T) {
      auto error = ST.accountRecord(Record, &AccountRecordState);
      if (error != StackTrie::AccountRecordStatus::OK) {
        if (!StackKeepGoing)
          return make_error<StringError>(
              CreateErrorMessage(error, Record, FuncIdHelper),
              make_error_code(errc::illegal_byte_sequence));
        errs() << CreateErrorMessage(error, Record, FuncIdHelper);
      }
    }
  }
  if (ST.isEmpty()) {
    return make_error<StringError>(
        "No instrumented calls were accounted in the input file.",
        make_error_code(errc::result_out_of_range));
  }

  // Report the stacks in a long form mode for another tool to analyze.
  if (DumpAllStacks) {
    if (AggregateThreads) {
      switch (RequestedAggregation) {
      case AggregationType::TOTAL_TIME:
        ST.printAllAggregatingThreads<AggregationType::TOTAL_TIME>(
            outs(), FuncIdHelper, StacksOutputFormat);
        break;
      case AggregationType::INVOCATION_COUNT:
        ST.printAllAggregatingThreads<AggregationType::INVOCATION_COUNT>(
            outs(), FuncIdHelper, StacksOutputFormat);
        break;
      }
    } else {
      switch (RequestedAggregation) {
      case AggregationType::TOTAL_TIME:
        ST.printAllPerThread<AggregationType::TOTAL_TIME>(outs(), FuncIdHelper,
                                                          StacksOutputFormat);
        break;
      case AggregationType::INVOCATION_COUNT:
        ST.printAllPerThread<AggregationType::INVOCATION_COUNT>(
            outs(), FuncIdHelper, StacksOutputFormat);
        break;
      }
    }
    return Error::success();
  }

  // We're only outputting top stacks.
  if (AggregateThreads) {
    ST.printAggregatingThreads(outs(), FuncIdHelper);
  } else if (SeparateThreadStacks) {
    ST.printPerThread(outs(), FuncIdHelper);
  } else {
    ST.printIgnoringThreads(outs(), FuncIdHelper);
  }
  return Error::success();
});
