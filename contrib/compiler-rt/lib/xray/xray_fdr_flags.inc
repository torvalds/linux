//===-- xray_fdr_flags.inc --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// XRay FDR Mode runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef XRAY_FLAG
#error "Define XRAY_FLAG prior to including this file!"
#endif

// FDR (Flight Data Recorder) Mode logging options.
XRAY_FLAG(int, func_duration_threshold_us, 5,
          "FDR logging will try to skip functions that execute for fewer "
          "microseconds than this threshold.")
XRAY_FLAG(int, grace_period_ms, 100,
          "FDR logging will wait this much time in milliseconds before "
          "actually flushing the log; this gives a chance for threads to "
          "notice that the log has been finalized and clean up.")
XRAY_FLAG(int, buffer_size, 16384,
          "Size of buffers in the circular buffer queue.")
XRAY_FLAG(int, buffer_max, 100, "Maximum number of buffers in the queue.")
XRAY_FLAG(bool, no_file_flush, false,
          "Set to true to not write log files by default.")
