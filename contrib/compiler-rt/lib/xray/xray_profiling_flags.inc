//===-- xray_profiling_flags.inc --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// XRay profiling runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_FLAG
#error "Define XRAY_FLAG prior to including this file!"
#endif

XRAY_FLAG(uptr, per_thread_allocator_max, 16384,
          "Maximum size of any single per-thread allocator.")
XRAY_FLAG(uptr, global_allocator_max, 2 << 24,
          "Maximum size of the global allocator for profile storage.")
XRAY_FLAG(uptr, stack_allocator_max, 2 << 20,
          "Maximum size of the traversal stack allocator.")
XRAY_FLAG(int, grace_period_ms, 1,
          "Profile collection will wait this much time in milliseconds before "
          "resetting the global state. This gives a chance to threads to "
          "notice that the profiler has been finalized and clean up.")
XRAY_FLAG(bool, no_flush, false,
          "Set to true if we want the profiling implementation to not write "
          "out files.")
XRAY_FLAG(int, buffers_max, 128,
          "The number of buffers to pre-allocate used by the profiling "
          "implementation.")
