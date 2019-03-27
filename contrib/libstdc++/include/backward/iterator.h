// Backward-compat support -*- C++ -*-

// Copyright (C) 2001, 2004 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/*
 *
 * Copyright (c) 1994
 * Hewlett-Packard Company
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Hewlett-Packard Company makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 *
 * Copyright (c) 1996
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

#ifndef _BACKWARD_ITERATOR_H
#define _BACKWARD_ITERATOR_H 1

#include "backward_warning.h"
#include "function.h"
#include <stddef.h>
#include "iostream.h"
#include <iterator>

#include <bits/stl_construct.h>
#include <bits/stl_raw_storage_iter.h>

#include <ext/iterator> // For 3-parameter distance extension

// Names from stl_iterator.h
using std::input_iterator_tag;
using std::output_iterator_tag;
using std::forward_iterator_tag;
using std::bidirectional_iterator_tag;
using std::random_access_iterator_tag;

#if 0
using std::iterator;
#endif

// The base classes input_iterator, output_iterator, forward_iterator,
// bidirectional_iterator, and random_access_iterator are not part of
// the C++ standard.  (They have been replaced by struct iterator.)
// They are included for backward compatibility with the HP STL.
template<typename _Tp, typename _Distance>
  struct input_iterator {
    typedef input_iterator_tag iterator_category;
    typedef _Tp                value_type;
    typedef _Distance          difference_type;
    typedef _Tp*               pointer;
    typedef _Tp&               reference;
  };

struct output_iterator {
  typedef output_iterator_tag iterator_category;
  typedef void                value_type;
  typedef void                difference_type;
  typedef void                pointer;
  typedef void                reference;
};

template<typename _Tp, typename _Distance>
  struct forward_iterator {
    typedef forward_iterator_tag iterator_category;
    typedef _Tp                  value_type;
    typedef _Distance            difference_type;
    typedef _Tp*                 pointer;
    typedef _Tp&                 reference;
  };

template<typename _Tp, typename _Distance>
  struct bidirectional_iterator {
    typedef bidirectional_iterator_tag iterator_category;
    typedef _Tp                        value_type;
    typedef _Distance                  difference_type;
    typedef _Tp*                       pointer;
    typedef _Tp&                       reference;
  };

template<typename _Tp, typename _Distance>
  struct random_access_iterator {
    typedef random_access_iterator_tag iterator_category;
    typedef _Tp                        value_type;
    typedef _Distance                  difference_type;
    typedef _Tp*                       pointer;
    typedef _Tp&                       reference;
  };

using std::iterator_traits;

template <class _Iter>
  inline typename iterator_traits<_Iter>::iterator_category
  iterator_category(const _Iter& __i)
  { return __iterator_category(__i); }

template <class _Iter>
  inline typename iterator_traits<_Iter>::difference_type*
  distance_type(const _Iter&)
  { return static_cast<typename iterator_traits<_Iter>::difference_type*>(0); }

template<class _Iter>
  inline typename iterator_traits<_Iter>::value_type*
  value_type(const _Iter& __i)
  { return static_cast<typename iterator_traits<_Iter>::value_type*>(0); }

using std::distance;
using __gnu_cxx::distance; // 3-parameter extension
using std::advance;

using std::insert_iterator;
using std::front_insert_iterator;
using std::back_insert_iterator;
using std::inserter;
using std::front_inserter;
using std::back_inserter;

using std::reverse_iterator;

using std::istream_iterator;
using std::ostream_iterator;

// Names from stl_construct.h
template<class _T1, class _T2>
  inline void
  construct(_T1* __p, const _T2& __value)
  { std::_Construct(__p, __value); }

template<class _T1>
  inline void
  construct(_T1* __p)
  { std::_Construct(__p); }

template <class _Tp>
  inline void
  destroy(_Tp* __pointer)
  { std::_Destroy(__pointer); }

template <class _ForwardIterator>
  inline void
  destroy(_ForwardIterator __first, _ForwardIterator __last)
  { std::_Destroy(__first, __last); }


// Names from stl_raw_storage_iter.h
using std::raw_storage_iterator;

#endif /* _BACKWARD_ITERATOR_H */
