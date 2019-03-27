/** @file kmp_stats.cpp
 * Statistics gathering and processing.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include "kmp.h"
#include "kmp_lock.h"
#include "kmp_stats.h"
#include "kmp_str.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdlib.h> // for atexit
#include <cmath>

#define STRINGIZE2(x) #x
#define STRINGIZE(x) STRINGIZE2(x)

#define expandName(name, flags, ignore) {STRINGIZE(name), flags},
statInfo timeStat::timerInfo[] = {
    KMP_FOREACH_TIMER(expandName, 0){"TIMER_LAST", 0}};
const statInfo counter::counterInfo[] = {
    KMP_FOREACH_COUNTER(expandName, 0){"COUNTER_LAST", 0}};
#undef expandName

#define expandName(ignore1, ignore2, ignore3) {0.0, 0.0, 0.0},
kmp_stats_output_module::rgb_color kmp_stats_output_module::timerColorInfo[] = {
    KMP_FOREACH_TIMER(expandName, 0){0.0, 0.0, 0.0}};
#undef expandName

const kmp_stats_output_module::rgb_color
    kmp_stats_output_module::globalColorArray[] = {
        {1.0, 0.0, 0.0}, // red
        {1.0, 0.6, 0.0}, // orange
        {1.0, 1.0, 0.0}, // yellow
        {0.0, 1.0, 0.0}, // green
        {0.0, 0.0, 1.0}, // blue
        {0.6, 0.2, 0.8}, // purple
        {1.0, 0.0, 1.0}, // magenta
        {0.0, 0.4, 0.2}, // dark green
        {1.0, 1.0, 0.6}, // light yellow
        {0.6, 0.4, 0.6}, // dirty purple
        {0.0, 1.0, 1.0}, // cyan
        {1.0, 0.4, 0.8}, // pink
        {0.5, 0.5, 0.5}, // grey
        {0.8, 0.7, 0.5}, // brown
        {0.6, 0.6, 1.0}, // light blue
        {1.0, 0.7, 0.5}, // peach
        {0.8, 0.5, 1.0}, // lavender
        {0.6, 0.0, 0.0}, // dark red
        {0.7, 0.6, 0.0}, // gold
        {0.0, 0.0, 0.0} // black
};

// Ensure that the atexit handler only runs once.
static uint32_t statsPrinted = 0;

// output interface
static kmp_stats_output_module *__kmp_stats_global_output = NULL;

double logHistogram::binMax[] = {
    1.e1l,  1.e2l,  1.e3l,  1.e4l,  1.e5l,  1.e6l,  1.e7l,  1.e8l,
    1.e9l,  1.e10l, 1.e11l, 1.e12l, 1.e13l, 1.e14l, 1.e15l, 1.e16l,
    1.e17l, 1.e18l, 1.e19l, 1.e20l, 1.e21l, 1.e22l, 1.e23l, 1.e24l,
    1.e25l, 1.e26l, 1.e27l, 1.e28l, 1.e29l, 1.e30l};

/* ************* statistic member functions ************* */

void statistic::addSample(double sample) {
  sample -= offset;
  KMP_DEBUG_ASSERT(std::isfinite(sample));

  double delta = sample - meanVal;

  sampleCount = sampleCount + 1;
  meanVal = meanVal + delta / sampleCount;
  m2 = m2 + delta * (sample - meanVal);

  minVal = std::min(minVal, sample);
  maxVal = std::max(maxVal, sample);
  if (collectingHist)
    hist.addSample(sample);
}

statistic &statistic::operator+=(const statistic &other) {
  if (other.sampleCount == 0)
    return *this;

  if (sampleCount == 0) {
    *this = other;
    return *this;
  }

  uint64_t newSampleCount = sampleCount + other.sampleCount;
  double dnsc = double(newSampleCount);
  double dsc = double(sampleCount);
  double dscBydnsc = dsc / dnsc;
  double dosc = double(other.sampleCount);
  double delta = other.meanVal - meanVal;

  // Try to order these calculations to avoid overflows. If this were Fortran,
  // then the compiler would not be able to re-order over brackets. In C++ it
  // may be legal to do that (we certainly hope it doesn't, and CC+ Programming
  // Language 2nd edition suggests it shouldn't, since it says that exploitation
  // of associativity can only be made if the operation really is associative
  // (which floating addition isn't...)).
  meanVal = meanVal * dscBydnsc + other.meanVal * (1 - dscBydnsc);
  m2 = m2 + other.m2 + dscBydnsc * dosc * delta * delta;
  minVal = std::min(minVal, other.minVal);
  maxVal = std::max(maxVal, other.maxVal);
  sampleCount = newSampleCount;
  if (collectingHist)
    hist += other.hist;

  return *this;
}

void statistic::scale(double factor) {
  minVal = minVal * factor;
  maxVal = maxVal * factor;
  meanVal = meanVal * factor;
  m2 = m2 * factor * factor;
  return;
}

std::string statistic::format(char unit, bool total) const {
  std::string result = formatSI(sampleCount, 9, ' ');

  if (sampleCount == 0) {
    result = result + std::string(", ") + formatSI(0.0, 9, unit);
    result = result + std::string(", ") + formatSI(0.0, 9, unit);
    result = result + std::string(", ") + formatSI(0.0, 9, unit);
    if (total)
      result = result + std::string(", ") + formatSI(0.0, 9, unit);
    result = result + std::string(", ") + formatSI(0.0, 9, unit);
  } else {
    result = result + std::string(", ") + formatSI(minVal, 9, unit);
    result = result + std::string(", ") + formatSI(meanVal, 9, unit);
    result = result + std::string(", ") + formatSI(maxVal, 9, unit);
    if (total)
      result =
          result + std::string(", ") + formatSI(meanVal * sampleCount, 9, unit);
    result = result + std::string(", ") + formatSI(getSD(), 9, unit);
  }
  return result;
}

/* ************* histogram member functions ************* */

// Lowest bin that has anything in it
int logHistogram::minBin() const {
  for (int i = 0; i < numBins; i++) {
    if (bins[i].count != 0)
      return i - logOffset;
  }
  return -logOffset;
}

// Highest bin that has anything in it
int logHistogram::maxBin() const {
  for (int i = numBins - 1; i >= 0; i--) {
    if (bins[i].count != 0)
      return i - logOffset;
  }
  return -logOffset;
}

// Which bin does this sample belong in ?
uint32_t logHistogram::findBin(double sample) {
  double v = std::fabs(sample);
  // Simply loop up looking which bin to put it in.
  // According to a micro-architect this is likely to be faster than a binary
  // search, since
  // it will only have one branch mis-predict
  for (int b = 0; b < numBins; b++)
    if (binMax[b] > v)
      return b;
  fprintf(stderr,
          "Trying to add a sample that is too large into a histogram\n");
  KMP_ASSERT(0);
  return -1;
}

void logHistogram::addSample(double sample) {
  if (sample == 0.0) {
    zeroCount += 1;
#ifdef KMP_DEBUG
    _total++;
    check();
#endif
    return;
  }
  KMP_DEBUG_ASSERT(std::isfinite(sample));
  uint32_t bin = findBin(sample);
  KMP_DEBUG_ASSERT(0 <= bin && bin < numBins);

  bins[bin].count += 1;
  bins[bin].total += sample;
#ifdef KMP_DEBUG
  _total++;
  check();
#endif
}

// This may not be the format we want, but it'll do for now
std::string logHistogram::format(char unit) const {
  std::stringstream result;

  result << "Bin,                Count,     Total\n";
  if (zeroCount) {
    result << "0,              " << formatSI(zeroCount, 9, ' ') << ", ",
        formatSI(0.0, 9, unit);
    if (count(minBin()) == 0)
      return result.str();
    result << "\n";
  }
  for (int i = minBin(); i <= maxBin(); i++) {
    result << "10**" << i << "<=v<10**" << (i + 1) << ", "
           << formatSI(count(i), 9, ' ') << ", " << formatSI(total(i), 9, unit);
    if (i != maxBin())
      result << "\n";
  }

  return result.str();
}

/* ************* explicitTimer member functions ************* */

void explicitTimer::start(tsc_tick_count tick) {
  startTime = tick;
  totalPauseTime = 0;
  if (timeStat::logEvent(timerEnumValue)) {
    __kmp_stats_thread_ptr->incrementNestValue();
  }
  return;
}

void explicitTimer::stop(tsc_tick_count tick,
                         kmp_stats_list *stats_ptr /* = nullptr */) {
  if (startTime.getValue() == 0)
    return;

  stat->addSample(((tick - startTime) - totalPauseTime).ticks());

  if (timeStat::logEvent(timerEnumValue)) {
    if (!stats_ptr)
      stats_ptr = __kmp_stats_thread_ptr;
    stats_ptr->push_event(
        startTime.getValue() - __kmp_stats_start_time.getValue(),
        tick.getValue() - __kmp_stats_start_time.getValue(),
        __kmp_stats_thread_ptr->getNestValue(), timerEnumValue);
    stats_ptr->decrementNestValue();
  }

  /* We accept the risk that we drop a sample because it really did start at
     t==0. */
  startTime = 0;
  return;
}

/* ************* partitionedTimers member functions ************* */
partitionedTimers::partitionedTimers() { timer_stack.reserve(8); }

// initialize the paritioned timers to an initial timer
void partitionedTimers::init(explicitTimer timer) {
  KMP_DEBUG_ASSERT(this->timer_stack.size() == 0);
  timer_stack.push_back(timer);
  timer_stack.back().start(tsc_tick_count::now());
}

// stop/save the current timer, and start the new timer (timer_pair)
// There is a special condition where if the current timer is equal to
// the one you are trying to push, then it only manipulates the stack,
// and it won't stop/start the currently running timer.
void partitionedTimers::push(explicitTimer timer) {
  // get the current timer
  // pause current timer
  // push new timer
  // start the new timer
  explicitTimer *current_timer, *new_timer;
  size_t stack_size;
  KMP_DEBUG_ASSERT(this->timer_stack.size() > 0);
  timer_stack.push_back(timer);
  stack_size = timer_stack.size();
  current_timer = &(timer_stack[stack_size - 2]);
  new_timer = &(timer_stack[stack_size - 1]);
  tsc_tick_count tick = tsc_tick_count::now();
  current_timer->pause(tick);
  new_timer->start(tick);
}

// stop/discard the current timer, and start the previously saved timer
void partitionedTimers::pop() {
  // get the current timer
  // stop current timer (record event/sample)
  // pop current timer
  // get the new current timer and resume
  explicitTimer *old_timer, *new_timer;
  size_t stack_size = timer_stack.size();
  KMP_DEBUG_ASSERT(stack_size > 1);
  old_timer = &(timer_stack[stack_size - 1]);
  new_timer = &(timer_stack[stack_size - 2]);
  tsc_tick_count tick = tsc_tick_count::now();
  old_timer->stop(tick);
  new_timer->resume(tick);
  timer_stack.pop_back();
}

void partitionedTimers::exchange(explicitTimer timer) {
  // get the current timer
  // stop current timer (record event/sample)
  // push new timer
  // start the new timer
  explicitTimer *current_timer, *new_timer;
  size_t stack_size;
  KMP_DEBUG_ASSERT(this->timer_stack.size() > 0);
  tsc_tick_count tick = tsc_tick_count::now();
  stack_size = timer_stack.size();
  current_timer = &(timer_stack[stack_size - 1]);
  current_timer->stop(tick);
  timer_stack.pop_back();
  timer_stack.push_back(timer);
  new_timer = &(timer_stack[stack_size - 1]);
  new_timer->start(tick);
}

// Wind up all the currently running timers.
// This pops off all the timers from the stack and clears the stack
// After this is called, init() must be run again to initialize the
// stack of timers
void partitionedTimers::windup() {
  while (timer_stack.size() > 1) {
    this->pop();
  }
  // Pop the timer from the init() call
  if (timer_stack.size() > 0) {
    timer_stack.back().stop(tsc_tick_count::now());
    timer_stack.pop_back();
  }
}

/* ************* kmp_stats_event_vector member functions ************* */

void kmp_stats_event_vector::deallocate() {
  __kmp_free(events);
  internal_size = 0;
  allocated_size = 0;
  events = NULL;
}

// This function is for qsort() which requires the compare function to return
// either a negative number if event1 < event2, a positive number if event1 >
// event2 or zero if event1 == event2. This sorts by start time (lowest to
// highest).
int compare_two_events(const void *event1, const void *event2) {
  const kmp_stats_event *ev1 = RCAST(const kmp_stats_event *, event1);
  const kmp_stats_event *ev2 = RCAST(const kmp_stats_event *, event2);

  if (ev1->getStart() < ev2->getStart())
    return -1;
  else if (ev1->getStart() > ev2->getStart())
    return 1;
  else
    return 0;
}

void kmp_stats_event_vector::sort() {
  qsort(events, internal_size, sizeof(kmp_stats_event), compare_two_events);
}

/* ************* kmp_stats_list member functions ************* */

// returns a pointer to newly created stats node
kmp_stats_list *kmp_stats_list::push_back(int gtid) {
  kmp_stats_list *newnode =
      (kmp_stats_list *)__kmp_allocate(sizeof(kmp_stats_list));
  // placement new, only requires space and pointer and initializes (so
  // __kmp_allocate instead of C++ new[] is used)
  new (newnode) kmp_stats_list();
  newnode->setGtid(gtid);
  newnode->prev = this->prev;
  newnode->next = this;
  newnode->prev->next = newnode;
  newnode->next->prev = newnode;
  return newnode;
}
void kmp_stats_list::deallocate() {
  kmp_stats_list *ptr = this->next;
  kmp_stats_list *delptr = this->next;
  while (ptr != this) {
    delptr = ptr;
    ptr = ptr->next;
    // placement new means we have to explicitly call destructor.
    delptr->_event_vector.deallocate();
    delptr->~kmp_stats_list();
    __kmp_free(delptr);
  }
}
kmp_stats_list::iterator kmp_stats_list::begin() {
  kmp_stats_list::iterator it;
  it.ptr = this->next;
  return it;
}
kmp_stats_list::iterator kmp_stats_list::end() {
  kmp_stats_list::iterator it;
  it.ptr = this;
  return it;
}
int kmp_stats_list::size() {
  int retval;
  kmp_stats_list::iterator it;
  for (retval = 0, it = begin(); it != end(); it++, retval++) {
  }
  return retval;
}

/* ************* kmp_stats_list::iterator member functions ************* */

kmp_stats_list::iterator::iterator() : ptr(NULL) {}
kmp_stats_list::iterator::~iterator() {}
kmp_stats_list::iterator kmp_stats_list::iterator::operator++() {
  this->ptr = this->ptr->next;
  return *this;
}
kmp_stats_list::iterator kmp_stats_list::iterator::operator++(int dummy) {
  this->ptr = this->ptr->next;
  return *this;
}
kmp_stats_list::iterator kmp_stats_list::iterator::operator--() {
  this->ptr = this->ptr->prev;
  return *this;
}
kmp_stats_list::iterator kmp_stats_list::iterator::operator--(int dummy) {
  this->ptr = this->ptr->prev;
  return *this;
}
bool kmp_stats_list::iterator::operator!=(const kmp_stats_list::iterator &rhs) {
  return this->ptr != rhs.ptr;
}
bool kmp_stats_list::iterator::operator==(const kmp_stats_list::iterator &rhs) {
  return this->ptr == rhs.ptr;
}
kmp_stats_list *kmp_stats_list::iterator::operator*() const {
  return this->ptr;
}

/* *************  kmp_stats_output_module functions ************** */

const char *kmp_stats_output_module::eventsFileName = NULL;
const char *kmp_stats_output_module::plotFileName = NULL;
int kmp_stats_output_module::printPerThreadFlag = 0;
int kmp_stats_output_module::printPerThreadEventsFlag = 0;

static char const *lastName(char *name) {
  int l = strlen(name);
  for (int i = l - 1; i >= 0; --i) {
    if (name[i] == '.')
      name[i] = '_';
    if (name[i] == '/')
      return name + i + 1;
  }
  return name;
}

/* Read the name of the executable from /proc/self/cmdline */
static char const *getImageName(char *buffer, size_t buflen) {
  FILE *f = fopen("/proc/self/cmdline", "r");
  buffer[0] = char(0);
  if (!f)
    return buffer;

  // The file contains char(0) delimited words from the commandline.
  // This just returns the last filename component of the first word on the
  // line.
  size_t n = fread(buffer, 1, buflen, f);
  if (n == 0) {
    fclose(f);
    KMP_CHECK_SYSFAIL("fread", 1)
  }
  fclose(f);
  buffer[buflen - 1] = char(0);
  return lastName(buffer);
}

static void getTime(char *buffer, size_t buflen, bool underscores = false) {
  time_t timer;

  time(&timer);

  struct tm *tm_info = localtime(&timer);
  if (underscores)
    strftime(buffer, buflen, "%Y-%m-%d_%H%M%S", tm_info);
  else
    strftime(buffer, buflen, "%Y-%m-%d %H%M%S", tm_info);
}

/* Generate a stats file name, expanding prototypes */
static std::string generateFilename(char const *prototype,
                                    char const *imageName) {
  std::string res;

  for (int i = 0; prototype[i] != char(0); i++) {
    char ch = prototype[i];

    if (ch == '%') {
      i++;
      if (prototype[i] == char(0))
        break;

      switch (prototype[i]) {
      case 't': // Insert time and date
      {
        char date[26];
        getTime(date, sizeof(date), true);
        res += date;
      } break;
      case 'e': // Insert executable name
        res += imageName;
        break;
      case 'p': // Insert pid
      {
        std::stringstream ss;
        ss << getpid();
        res += ss.str();
      } break;
      default:
        res += prototype[i];
        break;
      }
    } else
      res += ch;
  }
  return res;
}

// init() is called very near the beginning of execution time in the constructor
// of __kmp_stats_global_output
void kmp_stats_output_module::init() {

  fprintf(stderr, "*** Stats enabled OpenMP* runtime ***\n");
  char *statsFileName = getenv("KMP_STATS_FILE");
  eventsFileName = getenv("KMP_STATS_EVENTS_FILE");
  plotFileName = getenv("KMP_STATS_PLOT_FILE");
  char *threadStats = getenv("KMP_STATS_THREADS");
  char *threadEvents = getenv("KMP_STATS_EVENTS");

  // set the stats output filenames based on environment variables and defaults
  if (statsFileName) {
    char imageName[1024];
    // Process any escapes (e.g., %p, %e, %t) in the name
    outputFileName = generateFilename(
        statsFileName, getImageName(&imageName[0], sizeof(imageName)));
  }
  eventsFileName = eventsFileName ? eventsFileName : "events.dat";
  plotFileName = plotFileName ? plotFileName : "events.plt";

  // set the flags based on environment variables matching: true, on, 1, .true.
  // , .t. , yes
  printPerThreadFlag = __kmp_str_match_true(threadStats);
  printPerThreadEventsFlag = __kmp_str_match_true(threadEvents);

  if (printPerThreadEventsFlag) {
    // assigns a color to each timer for printing
    setupEventColors();
  } else {
    // will clear flag so that no event will be logged
    timeStat::clearEventFlags();
  }
}

void kmp_stats_output_module::setupEventColors() {
  int i;
  int globalColorIndex = 0;
  int numGlobalColors = sizeof(globalColorArray) / sizeof(rgb_color);
  for (i = 0; i < TIMER_LAST; i++) {
    if (timeStat::logEvent((timer_e)i)) {
      timerColorInfo[i] = globalColorArray[globalColorIndex];
      globalColorIndex = (globalColorIndex + 1) % numGlobalColors;
    }
  }
}

void kmp_stats_output_module::printTimerStats(FILE *statsOut,
                                              statistic const *theStats,
                                              statistic const *totalStats) {
  fprintf(statsOut,
          "Timer,                             SampleCount,    Min,      "
          "Mean,       Max,     Total,        SD\n");
  for (timer_e s = timer_e(0); s < TIMER_LAST; s = timer_e(s + 1)) {
    statistic const *stat = &theStats[s];
    char tag = timeStat::noUnits(s) ? ' ' : 'T';

    fprintf(statsOut, "%-35s, %s\n", timeStat::name(s),
            stat->format(tag, true).c_str());
  }
  // Also print the Total_ versions of times.
  for (timer_e s = timer_e(0); s < TIMER_LAST; s = timer_e(s + 1)) {
    char tag = timeStat::noUnits(s) ? ' ' : 'T';
    if (totalStats && !timeStat::noTotal(s))
      fprintf(statsOut, "Total_%-29s, %s\n", timeStat::name(s),
              totalStats[s].format(tag, true).c_str());
  }

  // Print historgram of statistics
  if (theStats[0].haveHist()) {
    fprintf(statsOut, "\nTimer distributions\n");
    for (int s = 0; s < TIMER_LAST; s++) {
      statistic const *stat = &theStats[s];

      if (stat->getCount() != 0) {
        char tag = timeStat::noUnits(timer_e(s)) ? ' ' : 'T';

        fprintf(statsOut, "%s\n", timeStat::name(timer_e(s)));
        fprintf(statsOut, "%s\n", stat->getHist()->format(tag).c_str());
      }
    }
  }
}

void kmp_stats_output_module::printCounterStats(FILE *statsOut,
                                                statistic const *theStats) {
  fprintf(statsOut, "Counter,                 ThreadCount,    Min,      Mean,  "
                    "     Max,     Total,        SD\n");
  for (int s = 0; s < COUNTER_LAST; s++) {
    statistic const *stat = &theStats[s];
    fprintf(statsOut, "%-25s, %s\n", counter::name(counter_e(s)),
            stat->format(' ', true).c_str());
  }
  // Print histogram of counters
  if (theStats[0].haveHist()) {
    fprintf(statsOut, "\nCounter distributions\n");
    for (int s = 0; s < COUNTER_LAST; s++) {
      statistic const *stat = &theStats[s];

      if (stat->getCount() != 0) {
        fprintf(statsOut, "%s\n", counter::name(counter_e(s)));
        fprintf(statsOut, "%s\n", stat->getHist()->format(' ').c_str());
      }
    }
  }
}

void kmp_stats_output_module::printCounters(FILE *statsOut,
                                            counter const *theCounters) {
  // We print all the counters even if they are zero.
  // That makes it easier to slice them into a spreadsheet if you need to.
  fprintf(statsOut, "\nCounter,                    Count\n");
  for (int c = 0; c < COUNTER_LAST; c++) {
    counter const *stat = &theCounters[c];
    fprintf(statsOut, "%-25s, %s\n", counter::name(counter_e(c)),
            formatSI(stat->getValue(), 9, ' ').c_str());
  }
}

void kmp_stats_output_module::printEvents(FILE *eventsOut,
                                          kmp_stats_event_vector *theEvents,
                                          int gtid) {
  // sort by start time before printing
  theEvents->sort();
  for (int i = 0; i < theEvents->size(); i++) {
    kmp_stats_event ev = theEvents->at(i);
    rgb_color color = getEventColor(ev.getTimerName());
    fprintf(eventsOut, "%d %lu %lu %1.1f rgb(%1.1f,%1.1f,%1.1f) %s\n", gtid,
            ev.getStart(), ev.getStop(), 1.2 - (ev.getNestLevel() * 0.2),
            color.r, color.g, color.b, timeStat::name(ev.getTimerName()));
  }
  return;
}

void kmp_stats_output_module::windupExplicitTimers() {
  // Wind up any explicit timers. We assume that it's fair at this point to just
  // walk all the explcit timers in all threads and say "it's over".
  // If the timer wasn't running, this won't record anything anyway.
  kmp_stats_list::iterator it;
  for (it = __kmp_stats_list->begin(); it != __kmp_stats_list->end(); it++) {
    kmp_stats_list *ptr = *it;
    ptr->getPartitionedTimers()->windup();
    ptr->endLife();
  }
}

void kmp_stats_output_module::printPloticusFile() {
  int i;
  int size = __kmp_stats_list->size();
  FILE *plotOut = fopen(plotFileName, "w+");

  fprintf(plotOut, "#proc page\n"
                   "   pagesize: 15 10\n"
                   "   scale: 1.0\n\n");

  fprintf(plotOut, "#proc getdata\n"
                   "   file: %s\n\n",
          eventsFileName);

  fprintf(plotOut, "#proc areadef\n"
                   "   title: OpenMP Sampling Timeline\n"
                   "   titledetails: align=center size=16\n"
                   "   rectangle: 1 1 13 9\n"
                   "   xautorange: datafield=2,3\n"
                   "   yautorange: -1 %d\n\n",
          size);

  fprintf(plotOut, "#proc xaxis\n"
                   "   stubs: inc\n"
                   "   stubdetails: size=12\n"
                   "   label: Time (ticks)\n"
                   "   labeldetails: size=14\n\n");

  fprintf(plotOut, "#proc yaxis\n"
                   "   stubs: inc 1\n"
                   "   stubrange: 0 %d\n"
                   "   stubdetails: size=12\n"
                   "   label: Thread #\n"
                   "   labeldetails: size=14\n\n",
          size - 1);

  fprintf(plotOut, "#proc bars\n"
                   "   exactcolorfield: 5\n"
                   "   axis: x\n"
                   "   locfield: 1\n"
                   "   segmentfields: 2 3\n"
                   "   barwidthfield: 4\n\n");

  // create legend entries corresponding to the timer color
  for (i = 0; i < TIMER_LAST; i++) {
    if (timeStat::logEvent((timer_e)i)) {
      rgb_color c = getEventColor((timer_e)i);
      fprintf(plotOut, "#proc legendentry\n"
                       "   sampletype: color\n"
                       "   label: %s\n"
                       "   details: rgb(%1.1f,%1.1f,%1.1f)\n\n",
              timeStat::name((timer_e)i), c.r, c.g, c.b);
    }
  }

  fprintf(plotOut, "#proc legend\n"
                   "   format: down\n"
                   "   location: max max\n\n");
  fclose(plotOut);
  return;
}

static void outputEnvVariable(FILE *statsOut, char const *name) {
  char const *value = getenv(name);
  fprintf(statsOut, "# %s = %s\n", name, value ? value : "*unspecified*");
}

/* Print some useful information about
   * the date and time this experiment ran.
   * the machine on which it ran.
   We output all of this as stylised comments, though we may decide to parse
   some of it. */
void kmp_stats_output_module::printHeaderInfo(FILE *statsOut) {
  std::time_t now = std::time(0);
  char buffer[40];
  char hostName[80];

  std::strftime(&buffer[0], sizeof(buffer), "%c", std::localtime(&now));
  fprintf(statsOut, "# Time of run: %s\n", &buffer[0]);
  if (gethostname(&hostName[0], sizeof(hostName)) == 0)
    fprintf(statsOut, "# Hostname: %s\n", &hostName[0]);
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
  fprintf(statsOut, "# CPU:  %s\n", &__kmp_cpuinfo.name[0]);
  fprintf(statsOut, "# Family: %d, Model: %d, Stepping: %d\n",
          __kmp_cpuinfo.family, __kmp_cpuinfo.model, __kmp_cpuinfo.stepping);
  if (__kmp_cpuinfo.frequency == 0)
    fprintf(statsOut, "# Nominal frequency: Unknown\n");
  else
    fprintf(statsOut, "# Nominal frequency: %sz\n",
            formatSI(double(__kmp_cpuinfo.frequency), 9, 'H').c_str());
  outputEnvVariable(statsOut, "KMP_HW_SUBSET");
  outputEnvVariable(statsOut, "KMP_AFFINITY");
  outputEnvVariable(statsOut, "KMP_BLOCKTIME");
  outputEnvVariable(statsOut, "KMP_LIBRARY");
  fprintf(statsOut, "# Production runtime built " __DATE__ " " __TIME__ "\n");
#endif
}

void kmp_stats_output_module::outputStats(const char *heading) {
  // Stop all the explicit timers in all threads
  // Do this before declaring the local statistics because thay have
  // constructors so will take time to create.
  windupExplicitTimers();

  statistic allStats[TIMER_LAST];
  statistic totalStats[TIMER_LAST]; /* Synthesized, cross threads versions of
                                       normal timer stats */
  statistic allCounters[COUNTER_LAST];

  FILE *statsOut =
      !outputFileName.empty() ? fopen(outputFileName.c_str(), "a+") : stderr;
  if (!statsOut)
    statsOut = stderr;

  FILE *eventsOut;
  if (eventPrintingEnabled()) {
    eventsOut = fopen(eventsFileName, "w+");
  }

  printHeaderInfo(statsOut);
  fprintf(statsOut, "%s\n", heading);
  // Accumulate across threads.
  kmp_stats_list::iterator it;
  for (it = __kmp_stats_list->begin(); it != __kmp_stats_list->end(); it++) {
    int t = (*it)->getGtid();
    // Output per thread stats if requested.
    if (printPerThreadFlag) {
      fprintf(statsOut, "Thread %d\n", t);
      printTimerStats(statsOut, (*it)->getTimers(), 0);
      printCounters(statsOut, (*it)->getCounters());
      fprintf(statsOut, "\n");
    }
    // Output per thread events if requested.
    if (eventPrintingEnabled()) {
      kmp_stats_event_vector events = (*it)->getEventVector();
      printEvents(eventsOut, &events, t);
    }

    // Accumulate timers.
    for (timer_e s = timer_e(0); s < TIMER_LAST; s = timer_e(s + 1)) {
      // See if we should ignore this timer when aggregating
      if ((timeStat::masterOnly(s) && (t != 0)) || // Timer only valid on master
          // and this thread is worker
          (timeStat::workerOnly(s) && (t == 0)) // Timer only valid on worker
          // and this thread is the master
          ) {
        continue;
      }

      statistic *threadStat = (*it)->getTimer(s);
      allStats[s] += *threadStat;

      // Add Total stats for timers that are valid in more than one thread
      if (!timeStat::noTotal(s))
        totalStats[s].addSample(threadStat->getTotal());
    }

    // Accumulate counters.
    for (counter_e c = counter_e(0); c < COUNTER_LAST; c = counter_e(c + 1)) {
      if (counter::masterOnly(c) && t != 0)
        continue;
      allCounters[c].addSample((*it)->getCounter(c)->getValue());
    }
  }

  if (eventPrintingEnabled()) {
    printPloticusFile();
    fclose(eventsOut);
  }

  fprintf(statsOut, "Aggregate for all threads\n");
  printTimerStats(statsOut, &allStats[0], &totalStats[0]);
  fprintf(statsOut, "\n");
  printCounterStats(statsOut, &allCounters[0]);

  if (statsOut != stderr)
    fclose(statsOut);
}

/* *************  exported C functions ************** */

// no name mangling for these functions, we want the c files to be able to get
// at these functions
extern "C" {

void __kmp_reset_stats() {
  kmp_stats_list::iterator it;
  for (it = __kmp_stats_list->begin(); it != __kmp_stats_list->end(); it++) {
    timeStat *timers = (*it)->getTimers();
    counter *counters = (*it)->getCounters();

    for (int t = 0; t < TIMER_LAST; t++)
      timers[t].reset();

    for (int c = 0; c < COUNTER_LAST; c++)
      counters[c].reset();

    // reset the event vector so all previous events are "erased"
    (*it)->resetEventVector();
  }
}

// This function will reset all stats and stop all threads' explicit timers if
// they haven't been stopped already.
void __kmp_output_stats(const char *heading) {
  __kmp_stats_global_output->outputStats(heading);
  __kmp_reset_stats();
}

void __kmp_accumulate_stats_at_exit(void) {
  // Only do this once.
  if (KMP_XCHG_FIXED32(&statsPrinted, 1) != 0)
    return;

  __kmp_output_stats("Statistics on exit");
}

void __kmp_stats_init(void) {
  __kmp_init_tas_lock(&__kmp_stats_lock);
  __kmp_stats_start_time = tsc_tick_count::now();
  __kmp_stats_global_output = new kmp_stats_output_module();
  __kmp_stats_list = new kmp_stats_list();
}

void __kmp_stats_fini(void) {
  __kmp_accumulate_stats_at_exit();
  __kmp_stats_list->deallocate();
  delete __kmp_stats_global_output;
  delete __kmp_stats_list;
}

} // extern "C"
