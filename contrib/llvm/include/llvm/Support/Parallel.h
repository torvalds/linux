//===- llvm/Support/Parallel.h - Parallel algorithms ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PARALLEL_H
#define LLVM_SUPPORT_PARALLEL_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>

#if defined(_MSC_VER) && LLVM_ENABLE_THREADS
#pragma warning(push)
#pragma warning(disable : 4530)
#include <concrt.h>
#include <ppl.h>
#pragma warning(pop)
#endif

namespace llvm {

namespace parallel {
struct sequential_execution_policy {};
struct parallel_execution_policy {};

template <typename T>
struct is_execution_policy
    : public std::integral_constant<
          bool, llvm::is_one_of<T, sequential_execution_policy,
                                parallel_execution_policy>::value> {};

constexpr sequential_execution_policy seq{};
constexpr parallel_execution_policy par{};

namespace detail {

#if LLVM_ENABLE_THREADS

class Latch {
  uint32_t Count;
  mutable std::mutex Mutex;
  mutable std::condition_variable Cond;

public:
  explicit Latch(uint32_t Count = 0) : Count(Count) {}
  ~Latch() { sync(); }

  void inc() {
    std::lock_guard<std::mutex> lock(Mutex);
    ++Count;
  }

  void dec() {
    std::lock_guard<std::mutex> lock(Mutex);
    if (--Count == 0)
      Cond.notify_all();
  }

  void sync() const {
    std::unique_lock<std::mutex> lock(Mutex);
    Cond.wait(lock, [&] { return Count == 0; });
  }
};

class TaskGroup {
  Latch L;

public:
  void spawn(std::function<void()> f);

  void sync() const { L.sync(); }
};

#if defined(_MSC_VER)
template <class RandomAccessIterator, class Comparator>
void parallel_sort(RandomAccessIterator Start, RandomAccessIterator End,
                   const Comparator &Comp) {
  concurrency::parallel_sort(Start, End, Comp);
}
template <class IterTy, class FuncTy>
void parallel_for_each(IterTy Begin, IterTy End, FuncTy Fn) {
  concurrency::parallel_for_each(Begin, End, Fn);
}

template <class IndexTy, class FuncTy>
void parallel_for_each_n(IndexTy Begin, IndexTy End, FuncTy Fn) {
  concurrency::parallel_for(Begin, End, Fn);
}

#else
const ptrdiff_t MinParallelSize = 1024;

/// Inclusive median.
template <class RandomAccessIterator, class Comparator>
RandomAccessIterator medianOf3(RandomAccessIterator Start,
                               RandomAccessIterator End,
                               const Comparator &Comp) {
  RandomAccessIterator Mid = Start + (std::distance(Start, End) / 2);
  return Comp(*Start, *(End - 1))
             ? (Comp(*Mid, *(End - 1)) ? (Comp(*Start, *Mid) ? Mid : Start)
                                       : End - 1)
             : (Comp(*Mid, *Start) ? (Comp(*(End - 1), *Mid) ? Mid : End - 1)
                                   : Start);
}

template <class RandomAccessIterator, class Comparator>
void parallel_quick_sort(RandomAccessIterator Start, RandomAccessIterator End,
                         const Comparator &Comp, TaskGroup &TG, size_t Depth) {
  // Do a sequential sort for small inputs.
  if (std::distance(Start, End) < detail::MinParallelSize || Depth == 0) {
    llvm::sort(Start, End, Comp);
    return;
  }

  // Partition.
  auto Pivot = medianOf3(Start, End, Comp);
  // Move Pivot to End.
  std::swap(*(End - 1), *Pivot);
  Pivot = std::partition(Start, End - 1, [&Comp, End](decltype(*Start) V) {
    return Comp(V, *(End - 1));
  });
  // Move Pivot to middle of partition.
  std::swap(*Pivot, *(End - 1));

  // Recurse.
  TG.spawn([=, &Comp, &TG] {
    parallel_quick_sort(Start, Pivot, Comp, TG, Depth - 1);
  });
  parallel_quick_sort(Pivot + 1, End, Comp, TG, Depth - 1);
}

template <class RandomAccessIterator, class Comparator>
void parallel_sort(RandomAccessIterator Start, RandomAccessIterator End,
                   const Comparator &Comp) {
  TaskGroup TG;
  parallel_quick_sort(Start, End, Comp, TG,
                      llvm::Log2_64(std::distance(Start, End)) + 1);
}

template <class IterTy, class FuncTy>
void parallel_for_each(IterTy Begin, IterTy End, FuncTy Fn) {
  // TaskGroup has a relatively high overhead, so we want to reduce
  // the number of spawn() calls. We'll create up to 1024 tasks here.
  // (Note that 1024 is an arbitrary number. This code probably needs
  // improving to take the number of available cores into account.)
  ptrdiff_t TaskSize = std::distance(Begin, End) / 1024;
  if (TaskSize == 0)
    TaskSize = 1;

  TaskGroup TG;
  while (TaskSize < std::distance(Begin, End)) {
    TG.spawn([=, &Fn] { std::for_each(Begin, Begin + TaskSize, Fn); });
    Begin += TaskSize;
  }
  std::for_each(Begin, End, Fn);
}

template <class IndexTy, class FuncTy>
void parallel_for_each_n(IndexTy Begin, IndexTy End, FuncTy Fn) {
  ptrdiff_t TaskSize = (End - Begin) / 1024;
  if (TaskSize == 0)
    TaskSize = 1;

  TaskGroup TG;
  IndexTy I = Begin;
  for (; I + TaskSize < End; I += TaskSize) {
    TG.spawn([=, &Fn] {
      for (IndexTy J = I, E = I + TaskSize; J != E; ++J)
        Fn(J);
    });
  }
  for (IndexTy J = I; J < End; ++J)
    Fn(J);
}

#endif

#endif

template <typename Iter>
using DefComparator =
    std::less<typename std::iterator_traits<Iter>::value_type>;

} // namespace detail

// sequential algorithm implementations.
template <class Policy, class RandomAccessIterator,
          class Comparator = detail::DefComparator<RandomAccessIterator>>
void sort(Policy policy, RandomAccessIterator Start, RandomAccessIterator End,
          const Comparator &Comp = Comparator()) {
  static_assert(is_execution_policy<Policy>::value,
                "Invalid execution policy!");
  llvm::sort(Start, End, Comp);
}

template <class Policy, class IterTy, class FuncTy>
void for_each(Policy policy, IterTy Begin, IterTy End, FuncTy Fn) {
  static_assert(is_execution_policy<Policy>::value,
                "Invalid execution policy!");
  std::for_each(Begin, End, Fn);
}

template <class Policy, class IndexTy, class FuncTy>
void for_each_n(Policy policy, IndexTy Begin, IndexTy End, FuncTy Fn) {
  static_assert(is_execution_policy<Policy>::value,
                "Invalid execution policy!");
  for (IndexTy I = Begin; I != End; ++I)
    Fn(I);
}

// Parallel algorithm implementations, only available when LLVM_ENABLE_THREADS
// is true.
#if LLVM_ENABLE_THREADS
template <class RandomAccessIterator,
          class Comparator = detail::DefComparator<RandomAccessIterator>>
void sort(parallel_execution_policy policy, RandomAccessIterator Start,
          RandomAccessIterator End, const Comparator &Comp = Comparator()) {
  detail::parallel_sort(Start, End, Comp);
}

template <class IterTy, class FuncTy>
void for_each(parallel_execution_policy policy, IterTy Begin, IterTy End,
              FuncTy Fn) {
  detail::parallel_for_each(Begin, End, Fn);
}

template <class IndexTy, class FuncTy>
void for_each_n(parallel_execution_policy policy, IndexTy Begin, IndexTy End,
                FuncTy Fn) {
  detail::parallel_for_each_n(Begin, End, Fn);
}
#endif

} // namespace parallel
} // namespace llvm

#endif // LLVM_SUPPORT_PARALLEL_H
