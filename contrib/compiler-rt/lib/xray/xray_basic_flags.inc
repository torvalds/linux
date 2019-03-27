//===-- xray_basic_flags.inc ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// XRay runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_FLAG
#error "Define XRAY_FLAG prior to including this file!"
#endif

XRAY_FLAG(int, func_duration_threshold_us, 5,
          "Basic logging will try to skip functions that execute for fewer "
          "microseconds than this threshold.")
XRAY_FLAG(int, max_stack_depth, 64,
          "Basic logging will keep track of at most this deep a call stack, "
          "any more and the recordings will be dropped.")
XRAY_FLAG(int, thread_buffer_size, 1024,
          "The number of entries to keep on a per-thread buffer.")
