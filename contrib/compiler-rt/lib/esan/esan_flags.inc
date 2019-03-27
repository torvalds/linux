//===-- esan_flags.inc ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Esan runtime flags.
//
//===----------------------------------------------------------------------===//

#ifndef ESAN_FLAG
# error "Define ESAN_FLAG prior to including this file!"
#endif

// ESAN_FLAG(Type, Name, DefaultValue, Description)
// See COMMON_FLAG in sanitizer_flags.inc for more details.

//===----------------------------------------------------------------------===//
// Cross-tool options
//===----------------------------------------------------------------------===//

ESAN_FLAG(int, cache_line_size, 64,
          "The number of bytes in a cache line.  For the working-set tool, this "
          "cannot be changed without also changing the compiler "
          "instrumentation.")

//===----------------------------------------------------------------------===//
// Working set tool options
//===----------------------------------------------------------------------===//

ESAN_FLAG(bool, record_snapshots, true,
          "Working set tool: whether to sample snapshots during a run.")

// Typical profiling uses a 10ms timer.  Our snapshots take some work
// to scan memory so we reduce to 20ms.
// To disable samples, turn off record_snapshots.
ESAN_FLAG(int, sample_freq, 20,
          "Working set tool: sampling frequency in milliseconds.")

// This controls the difference in frequency between each successive series
// of snapshots.  There are 8 in total, with number 0 using sample_freq.
// Number N samples number N-1 every (1 << snapshot_step) instance of N-1.
ESAN_FLAG(int, snapshot_step, 2, "Working set tool: the log of the sampling "
          "performed for the next-higher-frequency snapshot series.")

//===----------------------------------------------------------------------===//
// Cache Fragmentation tool options
//===----------------------------------------------------------------------===//

// The difference information of a struct is reported if the struct's difference
// score is greater than the report_threshold.
ESAN_FLAG(int, report_threshold, 1<<10, "Cache-frag tool: the struct difference"
          " score threshold for reporting.")
