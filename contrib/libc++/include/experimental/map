// -*- C++ -*-
//===----------------------------- map ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_EXPERIMENTAL_MAP
#define _LIBCPP_EXPERIMENTAL_MAP
/*
    experimental/map synopsis

// C++1z
namespace std {
namespace experimental {
inline namespace fundamentals_v1 {
namespace pmr {

  template <class Key, class T, class Compare = less<Key>>
  using map = std::map<Key, T, Compare,
                       polymorphic_allocator<pair<const Key,T>>>;

  template <class Key, class T, class Compare = less<Key>>
  using multimap = std::multimap<Key, T, Compare,
                                 polymorphic_allocator<pair<const Key,T>>>;

} // namespace pmr
} // namespace fundamentals_v1
} // namespace experimental
} // namespace std

 */

#include <experimental/__config>
#include <map>
#include <experimental/memory_resource>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_LFTS_PMR

template <class _Key, class _Value, class  _Compare = less<_Key>>
using map = _VSTD::map<_Key, _Value, _Compare,
                        polymorphic_allocator<pair<const _Key, _Value>>>;

template <class _Key, class _Value, class  _Compare = less<_Key>>
using multimap = _VSTD::multimap<_Key, _Value, _Compare,
                        polymorphic_allocator<pair<const _Key, _Value>>>;

_LIBCPP_END_NAMESPACE_LFTS_PMR

#endif /* _LIBCPP_EXPERIMENTAL_MAP */
