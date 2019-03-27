//===-- Timer.cpp - Interval Timing Support -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file Interval Timing implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Timer.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <limits>

using namespace llvm;

// This ugly hack is brought to you courtesy of constructor/destructor ordering
// being unspecified by C++.  Basically the problem is that a Statistic object
// gets destroyed, which ends up calling 'GetLibSupportInfoOutputFile()'
// (below), which calls this function.  LibSupportInfoOutputFilename used to be
// a global variable, but sometimes it would get destroyed before the Statistic,
// causing havoc to ensue.  We "fix" this by creating the string the first time
// it is needed and never destroying it.
static ManagedStatic<std::string> LibSupportInfoOutputFilename;
static std::string &getLibSupportInfoOutputFilename() {
  return *LibSupportInfoOutputFilename;
}

static ManagedStatic<sys::SmartMutex<true> > TimerLock;

namespace {
  static cl::opt<bool>
  TrackSpace("track-memory", cl::desc("Enable -time-passes memory "
                                      "tracking (this may be slow)"),
             cl::Hidden);

  static cl::opt<std::string, true>
  InfoOutputFilename("info-output-file", cl::value_desc("filename"),
                     cl::desc("File to append -stats and -timer output to"),
                   cl::Hidden, cl::location(getLibSupportInfoOutputFilename()));
}

std::unique_ptr<raw_fd_ostream> llvm::CreateInfoOutputFile() {
  const std::string &OutputFilename = getLibSupportInfoOutputFilename();
  if (OutputFilename.empty())
    return llvm::make_unique<raw_fd_ostream>(2, false); // stderr.
  if (OutputFilename == "-")
    return llvm::make_unique<raw_fd_ostream>(1, false); // stdout.

  // Append mode is used because the info output file is opened and closed
  // each time -stats or -time-passes wants to print output to it. To
  // compensate for this, the test-suite Makefiles have code to delete the
  // info output file before running commands which write to it.
  std::error_code EC;
  auto Result = llvm::make_unique<raw_fd_ostream>(
      OutputFilename, EC, sys::fs::F_Append | sys::fs::F_Text);
  if (!EC)
    return Result;

  errs() << "Error opening info-output-file '"
    << OutputFilename << " for appending!\n";
  return llvm::make_unique<raw_fd_ostream>(2, false); // stderr.
}

namespace {
struct CreateDefaultTimerGroup {
  static void *call() {
    return new TimerGroup("misc", "Miscellaneous Ungrouped Timers");
  }
};
} // namespace
static ManagedStatic<TimerGroup, CreateDefaultTimerGroup> DefaultTimerGroup;
static TimerGroup *getDefaultTimerGroup() { return &*DefaultTimerGroup; }

//===----------------------------------------------------------------------===//
// Timer Implementation
//===----------------------------------------------------------------------===//

void Timer::init(StringRef Name, StringRef Description) {
  init(Name, Description, *getDefaultTimerGroup());
}

void Timer::init(StringRef Name, StringRef Description, TimerGroup &tg) {
  assert(!TG && "Timer already initialized");
  this->Name.assign(Name.begin(), Name.end());
  this->Description.assign(Description.begin(), Description.end());
  Running = Triggered = false;
  TG = &tg;
  TG->addTimer(*this);
}

Timer::~Timer() {
  if (!TG) return;  // Never initialized, or already cleared.
  TG->removeTimer(*this);
}

static inline size_t getMemUsage() {
  if (!TrackSpace) return 0;
  return sys::Process::GetMallocUsage();
}

TimeRecord TimeRecord::getCurrentTime(bool Start) {
  using Seconds = std::chrono::duration<double, std::ratio<1>>;
  TimeRecord Result;
  sys::TimePoint<> now;
  std::chrono::nanoseconds user, sys;

  if (Start) {
    Result.MemUsed = getMemUsage();
    sys::Process::GetTimeUsage(now, user, sys);
  } else {
    sys::Process::GetTimeUsage(now, user, sys);
    Result.MemUsed = getMemUsage();
  }

  Result.WallTime = Seconds(now.time_since_epoch()).count();
  Result.UserTime = Seconds(user).count();
  Result.SystemTime = Seconds(sys).count();
  return Result;
}

void Timer::startTimer() {
  assert(!Running && "Cannot start a running timer");
  Running = Triggered = true;
  StartTime = TimeRecord::getCurrentTime(true);
}

void Timer::stopTimer() {
  assert(Running && "Cannot stop a paused timer");
  Running = false;
  Time += TimeRecord::getCurrentTime(false);
  Time -= StartTime;
}

void Timer::clear() {
  Running = Triggered = false;
  Time = StartTime = TimeRecord();
}

static void printVal(double Val, double Total, raw_ostream &OS) {
  if (Total < 1e-7)   // Avoid dividing by zero.
    OS << "        -----     ";
  else
    OS << format("  %7.4f (%5.1f%%)", Val, Val*100/Total);
}

void TimeRecord::print(const TimeRecord &Total, raw_ostream &OS) const {
  if (Total.getUserTime())
    printVal(getUserTime(), Total.getUserTime(), OS);
  if (Total.getSystemTime())
    printVal(getSystemTime(), Total.getSystemTime(), OS);
  if (Total.getProcessTime())
    printVal(getProcessTime(), Total.getProcessTime(), OS);
  printVal(getWallTime(), Total.getWallTime(), OS);

  OS << "  ";

  if (Total.getMemUsed())
    OS << format("%9" PRId64 "  ", (int64_t)getMemUsed());
}


//===----------------------------------------------------------------------===//
//   NamedRegionTimer Implementation
//===----------------------------------------------------------------------===//

namespace {

typedef StringMap<Timer> Name2TimerMap;

class Name2PairMap {
  StringMap<std::pair<TimerGroup*, Name2TimerMap> > Map;
public:
  ~Name2PairMap() {
    for (StringMap<std::pair<TimerGroup*, Name2TimerMap> >::iterator
         I = Map.begin(), E = Map.end(); I != E; ++I)
      delete I->second.first;
  }

  Timer &get(StringRef Name, StringRef Description, StringRef GroupName,
             StringRef GroupDescription) {
    sys::SmartScopedLock<true> L(*TimerLock);

    std::pair<TimerGroup*, Name2TimerMap> &GroupEntry = Map[GroupName];

    if (!GroupEntry.first)
      GroupEntry.first = new TimerGroup(GroupName, GroupDescription);

    Timer &T = GroupEntry.second[Name];
    if (!T.isInitialized())
      T.init(Name, Description, *GroupEntry.first);
    return T;
  }
};

}

static ManagedStatic<Name2PairMap> NamedGroupedTimers;

NamedRegionTimer::NamedRegionTimer(StringRef Name, StringRef Description,
                                   StringRef GroupName,
                                   StringRef GroupDescription, bool Enabled)
  : TimeRegion(!Enabled ? nullptr
                 : &NamedGroupedTimers->get(Name, Description, GroupName,
                                            GroupDescription)) {}

//===----------------------------------------------------------------------===//
//   TimerGroup Implementation
//===----------------------------------------------------------------------===//

/// This is the global list of TimerGroups, maintained by the TimerGroup
/// ctor/dtor and is protected by the TimerLock lock.
static TimerGroup *TimerGroupList = nullptr;

TimerGroup::TimerGroup(StringRef Name, StringRef Description)
  : Name(Name.begin(), Name.end()),
    Description(Description.begin(), Description.end()) {
  // Add the group to TimerGroupList.
  sys::SmartScopedLock<true> L(*TimerLock);
  if (TimerGroupList)
    TimerGroupList->Prev = &Next;
  Next = TimerGroupList;
  Prev = &TimerGroupList;
  TimerGroupList = this;
}

TimerGroup::TimerGroup(StringRef Name, StringRef Description,
                       const StringMap<TimeRecord> &Records)
    : TimerGroup(Name, Description) {
  TimersToPrint.reserve(Records.size());
  for (const auto &P : Records)
    TimersToPrint.emplace_back(P.getValue(), P.getKey(), P.getKey());
  assert(TimersToPrint.size() == Records.size() && "Size mismatch");
}

TimerGroup::~TimerGroup() {
  // If the timer group is destroyed before the timers it owns, accumulate and
  // print the timing data.
  while (FirstTimer)
    removeTimer(*FirstTimer);

  // Remove the group from the TimerGroupList.
  sys::SmartScopedLock<true> L(*TimerLock);
  *Prev = Next;
  if (Next)
    Next->Prev = Prev;
}


void TimerGroup::removeTimer(Timer &T) {
  sys::SmartScopedLock<true> L(*TimerLock);

  // If the timer was started, move its data to TimersToPrint.
  if (T.hasTriggered())
    TimersToPrint.emplace_back(T.Time, T.Name, T.Description);

  T.TG = nullptr;

  // Unlink the timer from our list.
  *T.Prev = T.Next;
  if (T.Next)
    T.Next->Prev = T.Prev;

  // Print the report when all timers in this group are destroyed if some of
  // them were started.
  if (FirstTimer || TimersToPrint.empty())
    return;

  std::unique_ptr<raw_ostream> OutStream = CreateInfoOutputFile();
  PrintQueuedTimers(*OutStream);
}

void TimerGroup::addTimer(Timer &T) {
  sys::SmartScopedLock<true> L(*TimerLock);

  // Add the timer to our list.
  if (FirstTimer)
    FirstTimer->Prev = &T.Next;
  T.Next = FirstTimer;
  T.Prev = &FirstTimer;
  FirstTimer = &T;
}

void TimerGroup::PrintQueuedTimers(raw_ostream &OS) {
  // Sort the timers in descending order by amount of time taken.
  llvm::sort(TimersToPrint);

  TimeRecord Total;
  for (const PrintRecord &Record : TimersToPrint)
    Total += Record.Time;

  // Print out timing header.
  OS << "===" << std::string(73, '-') << "===\n";
  // Figure out how many spaces to indent TimerGroup name.
  unsigned Padding = (80-Description.length())/2;
  if (Padding > 80) Padding = 0;         // Don't allow "negative" numbers
  OS.indent(Padding) << Description << '\n';
  OS << "===" << std::string(73, '-') << "===\n";

  // If this is not an collection of ungrouped times, print the total time.
  // Ungrouped timers don't really make sense to add up.  We still print the
  // TOTAL line to make the percentages make sense.
  if (this != getDefaultTimerGroup())
    OS << format("  Total Execution Time: %5.4f seconds (%5.4f wall clock)\n",
                 Total.getProcessTime(), Total.getWallTime());
  OS << '\n';

  if (Total.getUserTime())
    OS << "   ---User Time---";
  if (Total.getSystemTime())
    OS << "   --System Time--";
  if (Total.getProcessTime())
    OS << "   --User+System--";
  OS << "   ---Wall Time---";
  if (Total.getMemUsed())
    OS << "  ---Mem---";
  OS << "  --- Name ---\n";

  // Loop through all of the timing data, printing it out.
  for (const PrintRecord &Record : make_range(TimersToPrint.rbegin(),
                                              TimersToPrint.rend())) {
    Record.Time.print(Total, OS);
    OS << Record.Description << '\n';
  }

  Total.print(Total, OS);
  OS << "Total\n\n";
  OS.flush();

  TimersToPrint.clear();
}

void TimerGroup::prepareToPrintList() {
  // See if any of our timers were started, if so add them to TimersToPrint.
  for (Timer *T = FirstTimer; T; T = T->Next) {
    if (!T->hasTriggered()) continue;
    bool WasRunning = T->isRunning();
    if (WasRunning)
      T->stopTimer();

    TimersToPrint.emplace_back(T->Time, T->Name, T->Description);

    if (WasRunning)
      T->startTimer();
  }
}

void TimerGroup::print(raw_ostream &OS) {
  sys::SmartScopedLock<true> L(*TimerLock);

  prepareToPrintList();

  // If any timers were started, print the group.
  if (!TimersToPrint.empty())
    PrintQueuedTimers(OS);
}

void TimerGroup::clear() {
  sys::SmartScopedLock<true> L(*TimerLock);
  for (Timer *T = FirstTimer; T; T = T->Next)
    T->clear();
}

void TimerGroup::printAll(raw_ostream &OS) {
  sys::SmartScopedLock<true> L(*TimerLock);

  for (TimerGroup *TG = TimerGroupList; TG; TG = TG->Next)
    TG->print(OS);
}

void TimerGroup::clearAll() {
  sys::SmartScopedLock<true> L(*TimerLock);
  for (TimerGroup *TG = TimerGroupList; TG; TG = TG->Next)
    TG->clear();
}

void TimerGroup::printJSONValue(raw_ostream &OS, const PrintRecord &R,
                                const char *suffix, double Value) {
  assert(yaml::needsQuotes(Name) == yaml::QuotingType::None &&
         "TimerGroup name should not need quotes");
  assert(yaml::needsQuotes(R.Name) == yaml::QuotingType::None &&
         "Timer name should not need quotes");
  constexpr auto max_digits10 = std::numeric_limits<double>::max_digits10;
  OS << "\t\"time." << Name << '.' << R.Name << suffix
     << "\": " << format("%.*e", max_digits10 - 1, Value);
}

const char *TimerGroup::printJSONValues(raw_ostream &OS, const char *delim) {
  sys::SmartScopedLock<true> L(*TimerLock);

  prepareToPrintList();
  for (const PrintRecord &R : TimersToPrint) {
    OS << delim;
    delim = ",\n";

    const TimeRecord &T = R.Time;
    printJSONValue(OS, R, ".wall", T.getWallTime());
    OS << delim;
    printJSONValue(OS, R, ".user", T.getUserTime());
    OS << delim;
    printJSONValue(OS, R, ".sys", T.getSystemTime());
    if (T.getMemUsed()) {
      OS << delim;
      printJSONValue(OS, R, ".mem", T.getMemUsed());
    }
  }
  TimersToPrint.clear();
  return delim;
}

const char *TimerGroup::printAllJSONValues(raw_ostream &OS, const char *delim) {
  sys::SmartScopedLock<true> L(*TimerLock);
  for (TimerGroup *TG = TimerGroupList; TG; TG = TG->Next)
    delim = TG->printJSONValues(OS, delim);
  return delim;
}

void TimerGroup::ConstructTimerLists() {
  (void)*NamedGroupedTimers;
}
