//===- Unix/Process.cpp - Unix Process Implementation --------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides the generic Unix implementation of the Process class.
//
//===----------------------------------------------------------------------===//

#include "Unix.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/MutexGuard.h"
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SIGNAL_H
#include <signal.h>
#endif
// DragonFlyBSD, and OpenBSD have deprecated <malloc.h> for
// <stdlib.h> instead. Unix.h includes this for us already.
#if defined(HAVE_MALLOC_H) && !defined(__DragonFly__) && \
    !defined(__OpenBSD__) 
#include <malloc.h>
#endif
#if defined(HAVE_MALLCTL)
#include <malloc_np.h>
#endif
#ifdef HAVE_MALLOC_MALLOC_H
#include <malloc/malloc.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#endif
#ifdef HAVE_TERMIOS_H
#  include <termios.h>
#endif

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic UNIX code that
//===          is guaranteed to work on *all* UNIX variants.
//===----------------------------------------------------------------------===//

using namespace llvm;
using namespace sys;

static std::pair<std::chrono::microseconds, std::chrono::microseconds> getRUsageTimes() {
#if defined(HAVE_GETRUSAGE)
  struct rusage RU;
  ::getrusage(RUSAGE_SELF, &RU);
  return { toDuration(RU.ru_utime), toDuration(RU.ru_stime) };
#else
#warning Cannot get usage times on this platform
  return { std::chrono::microseconds::zero(), std::chrono::microseconds::zero() };
#endif
}

// On Cygwin, getpagesize() returns 64k(AllocationGranularity) and
// offset in mmap(3) should be aligned to the AllocationGranularity.
unsigned Process::getPageSize() {
#if defined(HAVE_GETPAGESIZE)
  static const int page_size = ::getpagesize();
#elif defined(HAVE_SYSCONF)
  static long page_size = ::sysconf(_SC_PAGE_SIZE);
#else
#error Cannot get the page size on this machine
#endif
  return static_cast<unsigned>(page_size);
}

size_t Process::GetMallocUsage() {
#if defined(HAVE_MALLINFO)
  struct mallinfo mi;
  mi = ::mallinfo();
  return mi.uordblks;
#elif defined(HAVE_MALLOC_ZONE_STATISTICS) && defined(HAVE_MALLOC_MALLOC_H)
  malloc_statistics_t Stats;
  malloc_zone_statistics(malloc_default_zone(), &Stats);
  return Stats.size_in_use;   // darwin
#elif defined(HAVE_MALLCTL)
  size_t alloc, sz;
  sz = sizeof(size_t);
  if (mallctl("stats.allocated", &alloc, &sz, NULL, 0) == 0)
    return alloc;
  return 0;
#elif defined(HAVE_SBRK)
  // Note this is only an approximation and more closely resembles
  // the value returned by mallinfo in the arena field.
  static char *StartOfMemory = reinterpret_cast<char*>(::sbrk(0));
  char *EndOfMemory = (char*)sbrk(0);
  if (EndOfMemory != ((char*)-1) && StartOfMemory != ((char*)-1))
    return EndOfMemory - StartOfMemory;
  return 0;
#else
#warning Cannot get malloc info on this platform
  return 0;
#endif
}

void Process::GetTimeUsage(TimePoint<> &elapsed, std::chrono::nanoseconds &user_time,
                           std::chrono::nanoseconds &sys_time) {
  elapsed = std::chrono::system_clock::now();
  std::tie(user_time, sys_time) = getRUsageTimes();
}

#if defined(HAVE_MACH_MACH_H) && !defined(__GNU__)
#include <mach/mach.h>
#endif

// Some LLVM programs such as bugpoint produce core files as a normal part of
// their operation. To prevent the disk from filling up, this function
// does what's necessary to prevent their generation.
void Process::PreventCoreFiles() {
#if HAVE_SETRLIMIT
  struct rlimit rlim;
  rlim.rlim_cur = rlim.rlim_max = 0;
  setrlimit(RLIMIT_CORE, &rlim);
#endif

#if defined(HAVE_MACH_MACH_H) && !defined(__GNU__)
  // Disable crash reporting on Mac OS X 10.0-10.4

  // get information about the original set of exception ports for the task
  mach_msg_type_number_t Count = 0;
  exception_mask_t OriginalMasks[EXC_TYPES_COUNT];
  exception_port_t OriginalPorts[EXC_TYPES_COUNT];
  exception_behavior_t OriginalBehaviors[EXC_TYPES_COUNT];
  thread_state_flavor_t OriginalFlavors[EXC_TYPES_COUNT];
  kern_return_t err =
    task_get_exception_ports(mach_task_self(), EXC_MASK_ALL, OriginalMasks,
                             &Count, OriginalPorts, OriginalBehaviors,
                             OriginalFlavors);
  if (err == KERN_SUCCESS) {
    // replace each with MACH_PORT_NULL.
    for (unsigned i = 0; i != Count; ++i)
      task_set_exception_ports(mach_task_self(), OriginalMasks[i],
                               MACH_PORT_NULL, OriginalBehaviors[i],
                               OriginalFlavors[i]);
  }

  // Disable crash reporting on Mac OS X 10.5
  signal(SIGABRT, _exit);
  signal(SIGILL,  _exit);
  signal(SIGFPE,  _exit);
  signal(SIGSEGV, _exit);
  signal(SIGBUS,  _exit);
#endif

  coreFilesPrevented = true;
}

Optional<std::string> Process::GetEnv(StringRef Name) {
  std::string NameStr = Name.str();
  const char *Val = ::getenv(NameStr.c_str());
  if (!Val)
    return None;
  return std::string(Val);
}

namespace {
class FDCloser {
public:
  FDCloser(int &FD) : FD(FD), KeepOpen(false) {}
  void keepOpen() { KeepOpen = true; }
  ~FDCloser() {
    if (!KeepOpen && FD >= 0)
      ::close(FD);
  }

private:
  FDCloser(const FDCloser &) = delete;
  void operator=(const FDCloser &) = delete;

  int &FD;
  bool KeepOpen;
};
}

std::error_code Process::FixupStandardFileDescriptors() {
  int NullFD = -1;
  FDCloser FDC(NullFD);
  const int StandardFDs[] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
  for (int StandardFD : StandardFDs) {
    struct stat st;
    errno = 0;
    if (RetryAfterSignal(-1, ::fstat, StandardFD, &st) < 0) {
      assert(errno && "expected errno to be set if fstat failed!");
      // fstat should return EBADF if the file descriptor is closed.
      if (errno != EBADF)
        return std::error_code(errno, std::generic_category());
    }
    // if fstat succeeds, move on to the next FD.
    if (!errno)
      continue;
    assert(errno == EBADF && "expected errno to have EBADF at this point!");

    if (NullFD < 0) {
      // Call ::open in a lambda to avoid overload resolution in
      // RetryAfterSignal when open is overloaded, such as in Bionic.
      auto Open = [&]() { return ::open("/dev/null", O_RDWR); };
      if ((NullFD = RetryAfterSignal(-1, Open)) < 0)
        return std::error_code(errno, std::generic_category());
    }

    if (NullFD == StandardFD)
      FDC.keepOpen();
    else if (dup2(NullFD, StandardFD) < 0)
      return std::error_code(errno, std::generic_category());
  }
  return std::error_code();
}

std::error_code Process::SafelyCloseFileDescriptor(int FD) {
  // Create a signal set filled with *all* signals.
  sigset_t FullSet;
  if (sigfillset(&FullSet) < 0)
    return std::error_code(errno, std::generic_category());
  // Atomically swap our current signal mask with a full mask.
  sigset_t SavedSet;
#if LLVM_ENABLE_THREADS
  if (int EC = pthread_sigmask(SIG_SETMASK, &FullSet, &SavedSet))
    return std::error_code(EC, std::generic_category());
#else
  if (sigprocmask(SIG_SETMASK, &FullSet, &SavedSet) < 0)
    return std::error_code(errno, std::generic_category());
#endif
  // Attempt to close the file descriptor.
  // We need to save the error, if one occurs, because our subsequent call to
  // pthread_sigmask might tamper with errno.
  int ErrnoFromClose = 0;
  if (::close(FD) < 0)
    ErrnoFromClose = errno;
  // Restore the signal mask back to what we saved earlier.
  int EC = 0;
#if LLVM_ENABLE_THREADS
  EC = pthread_sigmask(SIG_SETMASK, &SavedSet, nullptr);
#else
  if (sigprocmask(SIG_SETMASK, &SavedSet, nullptr) < 0)
    EC = errno;
#endif
  // The error code from close takes precedence over the one from
  // pthread_sigmask.
  if (ErrnoFromClose)
    return std::error_code(ErrnoFromClose, std::generic_category());
  return std::error_code(EC, std::generic_category());
}

bool Process::StandardInIsUserInput() {
  return FileDescriptorIsDisplayed(STDIN_FILENO);
}

bool Process::StandardOutIsDisplayed() {
  return FileDescriptorIsDisplayed(STDOUT_FILENO);
}

bool Process::StandardErrIsDisplayed() {
  return FileDescriptorIsDisplayed(STDERR_FILENO);
}

bool Process::FileDescriptorIsDisplayed(int fd) {
#if HAVE_ISATTY
  return isatty(fd);
#else
  // If we don't have isatty, just return false.
  return false;
#endif
}

static unsigned getColumns(int FileID) {
  // If COLUMNS is defined in the environment, wrap to that many columns.
  if (const char *ColumnsStr = std::getenv("COLUMNS")) {
    int Columns = std::atoi(ColumnsStr);
    if (Columns > 0)
      return Columns;
  }

  unsigned Columns = 0;

#if defined(HAVE_SYS_IOCTL_H) && defined(HAVE_TERMIOS_H)
  // Try to determine the width of the terminal.
  struct winsize ws;
  if (ioctl(FileID, TIOCGWINSZ, &ws) == 0)
    Columns = ws.ws_col;
#endif

  return Columns;
}

unsigned Process::StandardOutColumns() {
  if (!StandardOutIsDisplayed())
    return 0;

  return getColumns(1);
}

unsigned Process::StandardErrColumns() {
  if (!StandardErrIsDisplayed())
    return 0;

  return getColumns(2);
}

#ifdef HAVE_TERMINFO
// We manually declare these extern functions because finding the correct
// headers from various terminfo, curses, or other sources is harder than
// writing their specs down.
extern "C" int setupterm(char *term, int filedes, int *errret);
extern "C" struct term *set_curterm(struct term *termp);
extern "C" int del_curterm(struct term *termp);
extern "C" int tigetnum(char *capname);
#endif

#ifdef HAVE_TERMINFO
static ManagedStatic<sys::Mutex> TermColorMutex;
#endif

static bool terminalHasColors(int fd) {
#ifdef HAVE_TERMINFO
  // First, acquire a global lock because these C routines are thread hostile.
  MutexGuard G(*TermColorMutex);

  int errret = 0;
  if (setupterm(nullptr, fd, &errret) != 0)
    // Regardless of why, if we can't get terminfo, we shouldn't try to print
    // colors.
    return false;

  // Test whether the terminal as set up supports color output. How to do this
  // isn't entirely obvious. We can use the curses routine 'has_colors' but it
  // would be nice to avoid a dependency on curses proper when we can make do
  // with a minimal terminfo parsing library. Also, we don't really care whether
  // the terminal supports the curses-specific color changing routines, merely
  // if it will interpret ANSI color escape codes in a reasonable way. Thus, the
  // strategy here is just to query the baseline colors capability and if it
  // supports colors at all to assume it will translate the escape codes into
  // whatever range of colors it does support. We can add more detailed tests
  // here if users report them as necessary.
  //
  // The 'tigetnum' routine returns -2 or -1 on errors, and might return 0 if
  // the terminfo says that no colors are supported.
  bool HasColors = tigetnum(const_cast<char *>("colors")) > 0;

  // Now extract the structure allocated by setupterm and free its memory
  // through a really silly dance.
  struct term *termp = set_curterm(nullptr);
  (void)del_curterm(termp); // Drop any errors here.

  // Return true if we found a color capabilities for the current terminal.
  if (HasColors)
    return true;
#else
  // When the terminfo database is not available, check if the current terminal
  // is one of terminals that are known to support ANSI color escape codes.
  if (const char *TermStr = std::getenv("TERM")) {
    return StringSwitch<bool>(TermStr)
      .Case("ansi", true)
      .Case("cygwin", true)
      .Case("linux", true)
      .StartsWith("screen", true)
      .StartsWith("xterm", true)
      .StartsWith("vt100", true)
      .StartsWith("rxvt", true)
      .EndsWith("color", true)
      .Default(false);
  }
#endif

  // Otherwise, be conservative.
  return false;
}

bool Process::FileDescriptorHasColors(int fd) {
  // A file descriptor has colors if it is displayed and the terminal has
  // colors.
  return FileDescriptorIsDisplayed(fd) && terminalHasColors(fd);
}

bool Process::StandardOutHasColors() {
  return FileDescriptorHasColors(STDOUT_FILENO);
}

bool Process::StandardErrHasColors() {
  return FileDescriptorHasColors(STDERR_FILENO);
}

void Process::UseANSIEscapeCodes(bool /*enable*/) {
  // No effect.
}

bool Process::ColorNeedsFlush() {
  // No, we use ANSI escape sequences.
  return false;
}

const char *Process::OutputColor(char code, bool bold, bool bg) {
  return colorcodes[bg?1:0][bold?1:0][code&7];
}

const char *Process::OutputBold(bool bg) {
  return "\033[1m";
}

const char *Process::OutputReverse() {
  return "\033[7m";
}

const char *Process::ResetColor() {
  return "\033[0m";
}

#if !HAVE_DECL_ARC4RANDOM
static unsigned GetRandomNumberSeed() {
  // Attempt to get the initial seed from /dev/urandom, if possible.
  int urandomFD = open("/dev/urandom", O_RDONLY);

  if (urandomFD != -1) {
    unsigned seed;
    // Don't use a buffered read to avoid reading more data
    // from /dev/urandom than we need.
    int count = read(urandomFD, (void *)&seed, sizeof(seed));

    close(urandomFD);

    // Return the seed if the read was successful.
    if (count == sizeof(seed))
      return seed;
  }

  // Otherwise, swizzle the current time and the process ID to form a reasonable
  // seed.
  const auto Now = std::chrono::high_resolution_clock::now();
  return hash_combine(Now.time_since_epoch().count(), ::getpid());
}
#endif

unsigned llvm::sys::Process::GetRandomNumber() {
#if HAVE_DECL_ARC4RANDOM
  return arc4random();
#else
  static int x = (static_cast<void>(::srand(GetRandomNumberSeed())), 0);
  (void)x;
  return ::rand();
#endif
}
