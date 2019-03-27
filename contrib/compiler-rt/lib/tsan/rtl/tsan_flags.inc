//===-- tsan_flags.inc ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// TSan runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef TSAN_FLAG
# error "Define TSAN_FLAG prior to including this file!"
#endif

// TSAN_FLAG(Type, Name, DefaultValue, Description)
// See COMMON_FLAG in sanitizer_flags.inc for more details.

TSAN_FLAG(bool, enable_annotations, true,
          "Enable dynamic annotations, otherwise they are no-ops.")
// Suppress a race report if we've already output another race report
// with the same stack.
TSAN_FLAG(bool, suppress_equal_stacks, true,
          "Suppress a race report if we've already output another race report "
          "with the same stack.")
TSAN_FLAG(bool, suppress_equal_addresses, true,
          "Suppress a race report if we've already output another race report "
          "on the same address.")

TSAN_FLAG(bool, report_bugs, true,
          "Turns off bug reporting entirely (useful for benchmarking).")
TSAN_FLAG(bool, report_thread_leaks, true, "Report thread leaks at exit?")
TSAN_FLAG(bool, report_destroy_locked, true,
          "Report destruction of a locked mutex?")
TSAN_FLAG(bool, report_mutex_bugs, true,
          "Report incorrect usages of mutexes and mutex annotations?")
TSAN_FLAG(bool, report_signal_unsafe, true,
          "Report violations of async signal-safety "
          "(e.g. malloc() call from a signal handler).")
TSAN_FLAG(bool, report_atomic_races, true,
          "Report races between atomic and plain memory accesses.")
TSAN_FLAG(
    bool, force_seq_cst_atomics, false,
    "If set, all atomics are effectively sequentially consistent (seq_cst), "
    "regardless of what user actually specified.")
TSAN_FLAG(bool, print_benign, false, "Print matched \"benign\" races at exit.")
TSAN_FLAG(bool, halt_on_error, false, "Exit after first reported error.")
TSAN_FLAG(int, atexit_sleep_ms, 1000,
          "Sleep in main thread before exiting for that many ms "
          "(useful to catch \"at exit\" races).")
TSAN_FLAG(const char *, profile_memory, "",
          "If set, periodically write memory profile to that file.")
TSAN_FLAG(int, flush_memory_ms, 0, "Flush shadow memory every X ms.")
TSAN_FLAG(int, flush_symbolizer_ms, 5000, "Flush symbolizer caches every X ms.")
TSAN_FLAG(
    int, memory_limit_mb, 0,
    "Resident memory limit in MB to aim at."
    "If the process consumes more memory, then TSan will flush shadow memory.")
TSAN_FLAG(bool, stop_on_start, false,
          "Stops on start until __tsan_resume() is called (for debugging).")
TSAN_FLAG(bool, running_on_valgrind, false,
          "Controls whether RunningOnValgrind() returns true or false.")
// There are a lot of goroutines in Go, so we use smaller history.
TSAN_FLAG(
    int, history_size, SANITIZER_GO ? 1 : 3,
    "Per-thread history size, controls how many previous memory accesses "
    "are remembered per thread.  Possible values are [0..7]. "
    "history_size=0 amounts to 32K memory accesses.  Each next value doubles "
    "the amount of memory accesses, up to history_size=7 that amounts to "
    "4M memory accesses.  The default value is 2 (128K memory accesses).")
TSAN_FLAG(int, io_sync, 1,
          "Controls level of synchronization implied by IO operations. "
          "0 - no synchronization "
          "1 - reasonable level of synchronization (write->read)"
          "2 - global synchronization of all IO operations.")
TSAN_FLAG(bool, die_after_fork, true,
          "Die after multi-threaded fork if the child creates new threads.")
TSAN_FLAG(const char *, suppressions, "", "Suppressions file name.")
TSAN_FLAG(bool, ignore_noninstrumented_modules, SANITIZER_MAC ? true : false,
          "Interceptors should only detect races when called from instrumented "
          "modules.")
TSAN_FLAG(bool, shared_ptr_interceptor, true,
          "Track atomic reference counting in libc++ shared_ptr and weak_ptr.")
