//===-- Iterable.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Iterable_h_
#define liblldb_Iterable_h_

#include <utility>


namespace lldb_private {

template <typename I, typename E> E map_adapter(I &iter) {
  return iter->second;
}

template <typename I, typename E> E vector_adapter(I &iter) { return *iter; }

template <typename I, typename E> E list_adapter(I &iter) { return *iter; }

template <typename C, typename E, E (*A)(typename C::const_iterator &)>
class AdaptedConstIterator {
public:
  typedef typename C::const_iterator BackingIterator;

  // Wrapping constructor
  AdaptedConstIterator(BackingIterator backing_iterator)
      : m_iter(backing_iterator) {}

  // Default-constructible
  AdaptedConstIterator() : m_iter() {}

  // Copy-constructible
  AdaptedConstIterator(const AdaptedConstIterator &rhs) : m_iter(rhs.m_iter) {}

  // Copy-assignable
  AdaptedConstIterator &operator=(const AdaptedConstIterator &rhs) {
    m_iter = rhs.m_iter;
    return *this;
  }

  // Destructible
  ~AdaptedConstIterator() = default;

  // Comparable
  bool operator==(const AdaptedConstIterator &rhs) {
    return m_iter == rhs.m_iter;
  }

  bool operator!=(const AdaptedConstIterator &rhs) {
    return m_iter != rhs.m_iter;
  }

  // Rvalue dereferenceable
  E operator*() { return (*A)(m_iter); }

  E operator->() { return (*A)(m_iter); }

  // Offset dereferenceable
  E operator[](typename BackingIterator::difference_type offset) {
    return AdaptedConstIterator(m_iter + offset);
  }

  // Incrementable
  AdaptedConstIterator &operator++() {
    m_iter++;
    return *this;
  }

  // Decrementable
  AdaptedConstIterator &operator--() {
    m_iter--;
    return *this;
  }

  // Compound assignment
  AdaptedConstIterator &
  operator+=(typename BackingIterator::difference_type offset) {
    m_iter += offset;
    return *this;
  }

  AdaptedConstIterator &
  operator-=(typename BackingIterator::difference_type offset) {
    m_iter -= offset;
    return *this;
  }

  // Arithmetic
  AdaptedConstIterator
  operator+(typename BackingIterator::difference_type offset) {
    return AdaptedConstIterator(m_iter + offset);
  }

  AdaptedConstIterator
  operator-(typename BackingIterator::difference_type offset) {
    return AdaptedConstIterator(m_iter - offset);
  }

  // Comparable
  bool operator<(AdaptedConstIterator &rhs) { return m_iter < rhs.m_iter; }

  bool operator<=(AdaptedConstIterator &rhs) { return m_iter <= rhs.m_iter; }

  bool operator>(AdaptedConstIterator &rhs) { return m_iter > rhs.m_iter; }

  bool operator>=(AdaptedConstIterator &rhs) { return m_iter >= rhs.m_iter; }

  template <typename C1, typename E1, E1 (*A1)(typename C1::const_iterator &)>
  friend AdaptedConstIterator<C1, E1, A1>
  operator+(typename C1::const_iterator::difference_type,
            AdaptedConstIterator<C1, E1, A1> &);

  template <typename C1, typename E1, E1 (*A1)(typename C1::const_iterator &)>
  friend typename C1::const_iterator::difference_type
  operator-(AdaptedConstIterator<C1, E1, A1> &,
            AdaptedConstIterator<C1, E1, A1> &);

  template <typename C1, typename E1, E1 (*A1)(typename C1::const_iterator &)>
  friend void swap(AdaptedConstIterator<C1, E1, A1> &,
                   AdaptedConstIterator<C1, E1, A1> &);

private:
  BackingIterator m_iter;
};

template <typename C, typename E, E (*A)(typename C::const_iterator &)>
AdaptedConstIterator<C, E, A> operator+(
    typename AdaptedConstIterator<C, E, A>::BackingIterator::difference_type
        offset,
    AdaptedConstIterator<C, E, A> &rhs) {
  return rhs.operator+(offset);
}

template <typename C, typename E, E (*A)(typename C::const_iterator &)>
typename AdaptedConstIterator<C, E, A>::BackingIterator::difference_type
operator-(AdaptedConstIterator<C, E, A> &lhs,
          AdaptedConstIterator<C, E, A> &rhs) {
  return (lhs.m_iter - rhs.m_iter);
}

template <typename C, typename E, E (*A)(typename C::const_iterator &)>
void swap(AdaptedConstIterator<C, E, A> &lhs,
          AdaptedConstIterator<C, E, A> &rhs) {
  std::swap(lhs.m_iter, rhs.m_iter);
}

template <typename C, typename E, E (*A)(typename C::const_iterator &)>
class AdaptedIterable {
private:
  const C &m_container;

public:
  AdaptedIterable(const C &container) : m_container(container) {}

  AdaptedConstIterator<C, E, A> begin() {
    return AdaptedConstIterator<C, E, A>(m_container.begin());
  }

  AdaptedConstIterator<C, E, A> end() {
    return AdaptedConstIterator<C, E, A>(m_container.end());
  }
};

template <typename C, typename E, E (*A)(typename C::const_iterator &),
          typename MutexType>
class LockingAdaptedIterable : public AdaptedIterable<C, E, A> {
public:
  LockingAdaptedIterable(C &container, MutexType &mutex)
      : AdaptedIterable<C, E, A>(container), m_mutex(&mutex) {
    m_mutex->lock();
  }

  LockingAdaptedIterable(LockingAdaptedIterable &&rhs)
      : AdaptedIterable<C, E, A>(rhs), m_mutex(rhs.m_mutex) {
    rhs.m_mutex = nullptr;
  }

  ~LockingAdaptedIterable() {
    if (m_mutex)
      m_mutex->unlock();
  }

private:
  MutexType *m_mutex = nullptr;

  LockingAdaptedIterable(const LockingAdaptedIterable &) = delete;
  LockingAdaptedIterable &operator=(const LockingAdaptedIterable &) = delete;
};

} // namespace lldb_private

#endif // liblldb_Iterable_h_
