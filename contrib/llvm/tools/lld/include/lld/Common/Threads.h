//===- Threads.h ------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// LLD supports threads to distribute workloads to multiple cores. Using
// multicore is most effective when more than one core are idle. At the
// last step of a build, it is often the case that a linker is the only
// active process on a computer. So, we are naturally interested in using
// threads wisely to reduce latency to deliver results to users.
//
// That said, we don't want to do "too clever" things using threads.
// Complex multi-threaded algorithms are sometimes extremely hard to
// reason about and can easily mess up the entire design.
//
// Fortunately, when a linker links large programs (when the link time is
// most critical), it spends most of the time to work on massive number of
// small pieces of data of the same kind, and there are opportunities for
// large parallelism there. Here are examples:
//
//  - We have hundreds of thousands of input sections that need to be
//    copied to a result file at the last step of link. Once we fix a file
//    layout, each section can be copied to its destination and its
//    relocations can be applied independently.
//
//  - We have tens of millions of small strings when constructing a
//    mergeable string section.
//
// For the cases such as the former, we can just use parallelForEach
// instead of std::for_each (or a plain for loop). Because tasks are
// completely independent from each other, we can run them in parallel
// without any coordination between them. That's very easy to understand
// and reason about.
//
// For the cases such as the latter, we can use parallel algorithms to
// deal with massive data. We have to write code for a tailored algorithm
// for each problem, but the complexity of multi-threading is isolated in
// a single pass and doesn't affect the linker's overall design.
//
// The above approach seems to be working fairly well. As an example, when
// linking Chromium (output size 1.6 GB), using 4 cores reduces latency to
// 75% compared to single core (from 12.66 seconds to 9.55 seconds) on my
// Ivy Bridge Xeon 2.8 GHz machine. Using 40 cores reduces it to 63% (from
// 12.66 seconds to 7.95 seconds). Because of the Amdahl's law, the
// speedup is not linear, but as you add more cores, it gets faster.
//
// On a final note, if you are trying to optimize, keep the axiom "don't
// guess, measure!" in mind. Some important passes of the linker are not
// that slow. For example, resolving all symbols is not a very heavy pass,
// although it would be very hard to parallelize it. You want to first
// identify a slow pass and then optimize it.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COMMON_THREADS_H
#define LLD_COMMON_THREADS_H

#include "llvm/Support/Parallel.h"
#include <functional>

namespace lld {

extern bool ThreadsEnabled;

template <typename R, class FuncTy> void parallelForEach(R &&Range, FuncTy Fn) {
  if (ThreadsEnabled)
    for_each(llvm::parallel::par, std::begin(Range), std::end(Range), Fn);
  else
    for_each(llvm::parallel::seq, std::begin(Range), std::end(Range), Fn);
}

inline void parallelForEachN(size_t Begin, size_t End,
                             llvm::function_ref<void(size_t)> Fn) {
  if (ThreadsEnabled)
    for_each_n(llvm::parallel::par, Begin, End, Fn);
  else
    for_each_n(llvm::parallel::seq, Begin, End, Fn);
}

} // namespace lld

#endif
