#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Options.h"

using namespace llvm;

namespace {
// This class overrides the default list implementation of printing so we
// can pretty print the list of debug counter options.  This type of
// dynamic option is pretty rare (basically this and pass lists).
class DebugCounterList : public cl::list<std::string, DebugCounter> {
private:
  using Base = cl::list<std::string, DebugCounter>;

public:
  template <class... Mods>
  explicit DebugCounterList(Mods &&... Ms) : Base(std::forward<Mods>(Ms)...) {}

private:
  void printOptionInfo(size_t GlobalWidth) const override {
    // This is a variant of from generic_parser_base::printOptionInfo.  Sadly,
    // it's not easy to make it more usable.  We could get it to print these as
    // options if we were a cl::opt and registered them, but lists don't have
    // options, nor does the parser for std::string.  The other mechanisms for
    // options are global and would pollute the global namespace with our
    // counters.  Rather than go that route, we have just overridden the
    // printing, which only a few things call anyway.
    outs() << "  -" << ArgStr;
    // All of the other options in CommandLine.cpp use ArgStr.size() + 6 for
    // width, so we do the same.
    Option::printHelpStr(HelpStr, GlobalWidth, ArgStr.size() + 6);
    const auto &CounterInstance = DebugCounter::instance();
    for (auto Name : CounterInstance) {
      const auto Info =
          CounterInstance.getCounterInfo(CounterInstance.getCounterId(Name));
      size_t NumSpaces = GlobalWidth - Info.first.size() - 8;
      outs() << "    =" << Info.first;
      outs().indent(NumSpaces) << " -   " << Info.second << '\n';
    }
  }
};
} // namespace

// Create our command line option.
static DebugCounterList DebugCounterOption(
    "debug-counter", cl::Hidden,
    cl::desc("Comma separated list of debug counter skip and count"),
    cl::CommaSeparated, cl::ZeroOrMore, cl::location(DebugCounter::instance()));

static cl::opt<bool> PrintDebugCounter(
    "print-debug-counter", cl::Hidden, cl::init(false), cl::Optional,
    cl::desc("Print out debug counter info after all counters accumulated"));

static ManagedStatic<DebugCounter> DC;

// Print information when destroyed, iff command line option is specified.
DebugCounter::~DebugCounter() {
  if (isCountingEnabled() && PrintDebugCounter)
    print(dbgs());
}

DebugCounter &DebugCounter::instance() { return *DC; }

// This is called by the command line parser when it sees a value for the
// debug-counter option defined above.
void DebugCounter::push_back(const std::string &Val) {
  if (Val.empty())
    return;
  // The strings should come in as counter=value
  auto CounterPair = StringRef(Val).split('=');
  if (CounterPair.second.empty()) {
    errs() << "DebugCounter Error: " << Val << " does not have an = in it\n";
    return;
  }
  // Now we have counter=value.
  // First, process value.
  int64_t CounterVal;
  if (CounterPair.second.getAsInteger(0, CounterVal)) {
    errs() << "DebugCounter Error: " << CounterPair.second
           << " is not a number\n";
    return;
  }
  // Now we need to see if this is the skip or the count, remove the suffix, and
  // add it to the counter values.
  if (CounterPair.first.endswith("-skip")) {
    auto CounterName = CounterPair.first.drop_back(5);
    unsigned CounterID = getCounterId(CounterName);
    if (!CounterID) {
      errs() << "DebugCounter Error: " << CounterName
             << " is not a registered counter\n";
      return;
    }
    enableAllCounters();

    CounterInfo &Counter = Counters[CounterID];
    Counter.Skip = CounterVal;
    Counter.IsSet = true;
  } else if (CounterPair.first.endswith("-count")) {
    auto CounterName = CounterPair.first.drop_back(6);
    unsigned CounterID = getCounterId(CounterName);
    if (!CounterID) {
      errs() << "DebugCounter Error: " << CounterName
             << " is not a registered counter\n";
      return;
    }
    enableAllCounters();

    CounterInfo &Counter = Counters[CounterID];
    Counter.StopAfter = CounterVal;
    Counter.IsSet = true;
  } else {
    errs() << "DebugCounter Error: " << CounterPair.first
           << " does not end with -skip or -count\n";
  }
}

void DebugCounter::print(raw_ostream &OS) const {
  SmallVector<StringRef, 16> CounterNames(RegisteredCounters.begin(),
                                          RegisteredCounters.end());
  sort(CounterNames.begin(), CounterNames.end());

  auto &Us = instance();
  OS << "Counters and values:\n";
  for (auto &CounterName : CounterNames) {
    unsigned CounterID = getCounterId(CounterName);
    OS << left_justify(RegisteredCounters[CounterID], 32) << ": {"
       << Us.Counters[CounterID].Count << "," << Us.Counters[CounterID].Skip
       << "," << Us.Counters[CounterID].StopAfter << "}\n";
  }
}

LLVM_DUMP_METHOD void DebugCounter::dump() const {
  print(dbgs());
}
