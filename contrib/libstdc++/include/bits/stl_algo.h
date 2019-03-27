// Algorithm implementation -*- C++ -*-

// Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006
// Free Software Foundation, Inc.
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

/** @file stl_algo.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _ALGO_H
#define _ALGO_H 1

#include <bits/stl_heap.h>
#include <bits/stl_tempbuf.h>     // for _Temporary_buffer
#include <debug/debug.h>

// See concept_check.h for the __glibcxx_*_requires macros.

_GLIBCXX_BEGIN_NAMESPACE(std)

  /**
   *  @brief Find the median of three values.
   *  @param  a  A value.
   *  @param  b  A value.
   *  @param  c  A value.
   *  @return One of @p a, @p b or @p c.
   *
   *  If @c {l,m,n} is some convolution of @p {a,b,c} such that @c l<=m<=n
   *  then the value returned will be @c m.
   *  This is an SGI extension.
   *  @ingroup SGIextensions
  */
  template<typename _Tp>
    inline const _Tp&
    __median(const _Tp& __a, const _Tp& __b, const _Tp& __c)
    {
      // concept requirements
      __glibcxx_function_requires(_LessThanComparableConcept<_Tp>)
      if (__a < __b)
	if (__b < __c)
	  return __b;
	else if (__a < __c)
	  return __c;
	else
	  return __a;
      else if (__a < __c)
	return __a;
      else if (__b < __c)
	return __c;
      else
	return __b;
    }

  /**
   *  @brief Find the median of three values using a predicate for comparison.
   *  @param  a     A value.
   *  @param  b     A value.
   *  @param  c     A value.
   *  @param  comp  A binary predicate.
   *  @return One of @p a, @p b or @p c.
   *
   *  If @c {l,m,n} is some convolution of @p {a,b,c} such that @p comp(l,m)
   *  and @p comp(m,n) are both true then the value returned will be @c m.
   *  This is an SGI extension.
   *  @ingroup SGIextensions
  */
  template<typename _Tp, typename _Compare>
    inline const _Tp&
    __median(const _Tp& __a, const _Tp& __b, const _Tp& __c, _Compare __comp)
    {
      // concept requirements
      __glibcxx_function_requires(_BinaryFunctionConcept<_Compare,bool,_Tp,_Tp>)
      if (__comp(__a, __b))
	if (__comp(__b, __c))
	  return __b;
	else if (__comp(__a, __c))
	  return __c;
	else
	  return __a;
      else if (__comp(__a, __c))
	return __a;
      else if (__comp(__b, __c))
	return __c;
      else
	return __b;
    }

  /**
   *  @brief Apply a function to every element of a sequence.
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  f      A unary function object.
   *  @return   @p f.
   *
   *  Applies the function object @p f to each element in the range
   *  @p [first,last).  @p f must not modify the order of the sequence.
   *  If @p f has a return value it is ignored.
  */
  template<typename _InputIterator, typename _Function>
    _Function
    for_each(_InputIterator __first, _InputIterator __last, _Function __f)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_requires_valid_range(__first, __last);
      for ( ; __first != __last; ++__first)
	__f(*__first);
      return __f;
    }

  /**
   *  @if maint
   *  This is an overload used by find() for the Input Iterator case.
   *  @endif
  */
  template<typename _InputIterator, typename _Tp>
    inline _InputIterator
    __find(_InputIterator __first, _InputIterator __last,
	   const _Tp& __val, input_iterator_tag)
    {
      while (__first != __last && !(*__first == __val))
	++__first;
      return __first;
    }

  /**
   *  @if maint
   *  This is an overload used by find_if() for the Input Iterator case.
   *  @endif
  */
  template<typename _InputIterator, typename _Predicate>
    inline _InputIterator
    __find_if(_InputIterator __first, _InputIterator __last,
	      _Predicate __pred, input_iterator_tag)
    {
      while (__first != __last && !__pred(*__first))
	++__first;
      return __first;
    }

  /**
   *  @if maint
   *  This is an overload used by find() for the RAI case.
   *  @endif
  */
  template<typename _RandomAccessIterator, typename _Tp>
    _RandomAccessIterator
    __find(_RandomAccessIterator __first, _RandomAccessIterator __last,
	   const _Tp& __val, random_access_iterator_tag)
    {
      typename iterator_traits<_RandomAccessIterator>::difference_type
	__trip_count = (__last - __first) >> 2;

      for ( ; __trip_count > 0 ; --__trip_count)
	{
	  if (*__first == __val)
	    return __first;
	  ++__first;

	  if (*__first == __val)
	    return __first;
	  ++__first;

	  if (*__first == __val)
	    return __first;
	  ++__first;

	  if (*__first == __val)
	    return __first;
	  ++__first;
	}

      switch (__last - __first)
	{
	case 3:
	  if (*__first == __val)
	    return __first;
	  ++__first;
	case 2:
	  if (*__first == __val)
	    return __first;
	  ++__first;
	case 1:
	  if (*__first == __val)
	    return __first;
	  ++__first;
	case 0:
	default:
	  return __last;
	}
    }

  /**
   *  @if maint
   *  This is an overload used by find_if() for the RAI case.
   *  @endif
  */
  template<typename _RandomAccessIterator, typename _Predicate>
    _RandomAccessIterator
    __find_if(_RandomAccessIterator __first, _RandomAccessIterator __last,
	      _Predicate __pred, random_access_iterator_tag)
    {
      typename iterator_traits<_RandomAccessIterator>::difference_type
	__trip_count = (__last - __first) >> 2;

      for ( ; __trip_count > 0 ; --__trip_count)
	{
	  if (__pred(*__first))
	    return __first;
	  ++__first;

	  if (__pred(*__first))
	    return __first;
	  ++__first;

	  if (__pred(*__first))
	    return __first;
	  ++__first;

	  if (__pred(*__first))
	    return __first;
	  ++__first;
	}

      switch (__last - __first)
	{
	case 3:
	  if (__pred(*__first))
	    return __first;
	  ++__first;
	case 2:
	  if (__pred(*__first))
	    return __first;
	  ++__first;
	case 1:
	  if (__pred(*__first))
	    return __first;
	  ++__first;
	case 0:
	default:
	  return __last;
	}
    }

  /**
   *  @if maint
   *  This is an overload of find() for streambuf iterators.
   *  @endif
  */
  template<typename _CharT>
    typename __gnu_cxx::__enable_if<__is_char<_CharT>::__value,
				    istreambuf_iterator<_CharT> >::__type
    find(istreambuf_iterator<_CharT>, istreambuf_iterator<_CharT>,
	 const _CharT&);

  /**
   *  @brief Find the first occurrence of a value in a sequence.
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  val    The value to find.
   *  @return   The first iterator @c i in the range @p [first,last)
   *  such that @c *i == @p val, or @p last if no such iterator exists.
  */
  template<typename _InputIterator, typename _Tp>
    inline _InputIterator
    find(_InputIterator __first, _InputIterator __last,
	 const _Tp& __val)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_EqualOpConcept<
		typename iterator_traits<_InputIterator>::value_type, _Tp>)
      __glibcxx_requires_valid_range(__first, __last);
      return std::__find(__first, __last, __val,
		         std::__iterator_category(__first));
    }

  /**
   *  @brief Find the first element in a sequence for which a predicate is true.
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  pred   A predicate.
   *  @return   The first iterator @c i in the range @p [first,last)
   *  such that @p pred(*i) is true, or @p last if no such iterator exists.
  */
  template<typename _InputIterator, typename _Predicate>
    inline _InputIterator
    find_if(_InputIterator __first, _InputIterator __last,
	    _Predicate __pred)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_UnaryPredicateConcept<_Predicate,
	      typename iterator_traits<_InputIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);
      return std::__find_if(__first, __last, __pred,
			    std::__iterator_category(__first));
    }

  /**
   *  @brief Find two adjacent values in a sequence that are equal.
   *  @param  first  A forward iterator.
   *  @param  last   A forward iterator.
   *  @return   The first iterator @c i such that @c i and @c i+1 are both
   *  valid iterators in @p [first,last) and such that @c *i == @c *(i+1),
   *  or @p last if no such iterator exists.
  */
  template<typename _ForwardIterator>
    _ForwardIterator
    adjacent_find(_ForwardIterator __first, _ForwardIterator __last)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_EqualityComparableConcept<
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);
      if (__first == __last)
	return __last;
      _ForwardIterator __next = __first;
      while(++__next != __last)
	{
	  if (*__first == *__next)
	    return __first;
	  __first = __next;
	}
      return __last;
    }

  /**
   *  @brief Find two adjacent values in a sequence using a predicate.
   *  @param  first         A forward iterator.
   *  @param  last          A forward iterator.
   *  @param  binary_pred   A binary predicate.
   *  @return   The first iterator @c i such that @c i and @c i+1 are both
   *  valid iterators in @p [first,last) and such that
   *  @p binary_pred(*i,*(i+1)) is true, or @p last if no such iterator
   *  exists.
  */
  template<typename _ForwardIterator, typename _BinaryPredicate>
    _ForwardIterator
    adjacent_find(_ForwardIterator __first, _ForwardIterator __last,
		  _BinaryPredicate __binary_pred)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
	    typename iterator_traits<_ForwardIterator>::value_type,
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);
      if (__first == __last)
	return __last;
      _ForwardIterator __next = __first;
      while(++__next != __last)
	{
	  if (__binary_pred(*__first, *__next))
	    return __first;
	  __first = __next;
	}
      return __last;
    }

  /**
   *  @brief Count the number of copies of a value in a sequence.
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  value  The value to be counted.
   *  @return   The number of iterators @c i in the range @p [first,last)
   *  for which @c *i == @p value
  */
  template<typename _InputIterator, typename _Tp>
    typename iterator_traits<_InputIterator>::difference_type
    count(_InputIterator __first, _InputIterator __last, const _Tp& __value)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_EqualOpConcept<
	typename iterator_traits<_InputIterator>::value_type, _Tp>)
      __glibcxx_requires_valid_range(__first, __last);
      typename iterator_traits<_InputIterator>::difference_type __n = 0;
      for ( ; __first != __last; ++__first)
	if (*__first == __value)
	  ++__n;
      return __n;
    }

  /**
   *  @brief Count the elements of a sequence for which a predicate is true.
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  pred   A predicate.
   *  @return   The number of iterators @c i in the range @p [first,last)
   *  for which @p pred(*i) is true.
  */
  template<typename _InputIterator, typename _Predicate>
    typename iterator_traits<_InputIterator>::difference_type
    count_if(_InputIterator __first, _InputIterator __last, _Predicate __pred)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_InputIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);
      typename iterator_traits<_InputIterator>::difference_type __n = 0;
      for ( ; __first != __last; ++__first)
	if (__pred(*__first))
	  ++__n;
      return __n;
    }

  /**
   *  @brief Search a sequence for a matching sub-sequence.
   *  @param  first1  A forward iterator.
   *  @param  last1   A forward iterator.
   *  @param  first2  A forward iterator.
   *  @param  last2   A forward iterator.
   *  @return   The first iterator @c i in the range
   *  @p [first1,last1-(last2-first2)) such that @c *(i+N) == @p *(first2+N)
   *  for each @c N in the range @p [0,last2-first2), or @p last1 if no
   *  such iterator exists.
   *
   *  Searches the range @p [first1,last1) for a sub-sequence that compares
   *  equal value-by-value with the sequence given by @p [first2,last2) and
   *  returns an iterator to the first element of the sub-sequence, or
   *  @p last1 if the sub-sequence is not found.
   *
   *  Because the sub-sequence must lie completely within the range
   *  @p [first1,last1) it must start at a position less than
   *  @p last1-(last2-first2) where @p last2-first2 is the length of the
   *  sub-sequence.
   *  This means that the returned iterator @c i will be in the range
   *  @p [first1,last1-(last2-first2))
  */
  template<typename _ForwardIterator1, typename _ForwardIterator2>
    _ForwardIterator1
    search(_ForwardIterator1 __first1, _ForwardIterator1 __last1,
	   _ForwardIterator2 __first2, _ForwardIterator2 __last2)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator1>)
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator2>)
      __glibcxx_function_requires(_EqualOpConcept<
	    typename iterator_traits<_ForwardIterator1>::value_type,
	    typename iterator_traits<_ForwardIterator2>::value_type>)
      __glibcxx_requires_valid_range(__first1, __last1);
      __glibcxx_requires_valid_range(__first2, __last2);
      // Test for empty ranges
      if (__first1 == __last1 || __first2 == __last2)
	return __first1;

      // Test for a pattern of length 1.
      _ForwardIterator2 __tmp(__first2);
      ++__tmp;
      if (__tmp == __last2)
	return std::find(__first1, __last1, *__first2);

      // General case.
      _ForwardIterator2 __p1, __p;
      __p1 = __first2; ++__p1;
      _ForwardIterator1 __current = __first1;

      while (__first1 != __last1)
	{
	  __first1 = std::find(__first1, __last1, *__first2);
	  if (__first1 == __last1)
	    return __last1;

	  __p = __p1;
	  __current = __first1;
	  if (++__current == __last1)
	    return __last1;

	  while (*__current == *__p)
	    {
	      if (++__p == __last2)
		return __first1;
	      if (++__current == __last1)
		return __last1;
	    }
	  ++__first1;
	}
      return __first1;
    }

  /**
   *  @brief Search a sequence for a matching sub-sequence using a predicate.
   *  @param  first1     A forward iterator.
   *  @param  last1      A forward iterator.
   *  @param  first2     A forward iterator.
   *  @param  last2      A forward iterator.
   *  @param  predicate  A binary predicate.
   *  @return   The first iterator @c i in the range
   *  @p [first1,last1-(last2-first2)) such that
   *  @p predicate(*(i+N),*(first2+N)) is true for each @c N in the range
   *  @p [0,last2-first2), or @p last1 if no such iterator exists.
   *
   *  Searches the range @p [first1,last1) for a sub-sequence that compares
   *  equal value-by-value with the sequence given by @p [first2,last2),
   *  using @p predicate to determine equality, and returns an iterator
   *  to the first element of the sub-sequence, or @p last1 if no such
   *  iterator exists.
   *
   *  @see search(_ForwardIter1, _ForwardIter1, _ForwardIter2, _ForwardIter2)
  */
  template<typename _ForwardIterator1, typename _ForwardIterator2,
	   typename _BinaryPredicate>
    _ForwardIterator1
    search(_ForwardIterator1 __first1, _ForwardIterator1 __last1,
	   _ForwardIterator2 __first2, _ForwardIterator2 __last2,
	   _BinaryPredicate  __predicate)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator1>)
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator2>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
	    typename iterator_traits<_ForwardIterator1>::value_type,
	    typename iterator_traits<_ForwardIterator2>::value_type>)
      __glibcxx_requires_valid_range(__first1, __last1);
      __glibcxx_requires_valid_range(__first2, __last2);

      // Test for empty ranges
      if (__first1 == __last1 || __first2 == __last2)
	return __first1;

      // Test for a pattern of length 1.
      _ForwardIterator2 __tmp(__first2);
      ++__tmp;
      if (__tmp == __last2)
	{
	  while (__first1 != __last1 && !__predicate(*__first1, *__first2))
	    ++__first1;
	  return __first1;
	}

      // General case.
      _ForwardIterator2 __p1, __p;
      __p1 = __first2; ++__p1;
      _ForwardIterator1 __current = __first1;

      while (__first1 != __last1)
	{
	  while (__first1 != __last1)
	    {
	      if (__predicate(*__first1, *__first2))
		break;
	      ++__first1;
	    }
	  while (__first1 != __last1 && !__predicate(*__first1, *__first2))
	    ++__first1;
	  if (__first1 == __last1)
	    return __last1;

	  __p = __p1;
	  __current = __first1;
	  if (++__current == __last1)
	    return __last1;

	  while (__predicate(*__current, *__p))
	    {
	      if (++__p == __last2)
		return __first1;
	      if (++__current == __last1)
		return __last1;
	    }
	  ++__first1;
	}
      return __first1;
    }

  /**
   *  @if maint
   *  This is an uglified
   *  search_n(_ForwardIterator, _ForwardIterator, _Integer, const _Tp&)
   *  overloaded for forward iterators.
   *  @endif
  */
  template<typename _ForwardIterator, typename _Integer, typename _Tp>
    _ForwardIterator
    __search_n(_ForwardIterator __first, _ForwardIterator __last,
	       _Integer __count, const _Tp& __val,
	       std::forward_iterator_tag)
    {
      __first = std::find(__first, __last, __val);
      while (__first != __last)
	{
	  typename iterator_traits<_ForwardIterator>::difference_type
	    __n = __count;
	  _ForwardIterator __i = __first;
	  ++__i;
	  while (__i != __last && __n != 1 && *__i == __val)
	    {
	      ++__i;
	      --__n;
	    }
	  if (__n == 1)
	    return __first;
	  if (__i == __last)
	    return __last;
	  __first = std::find(++__i, __last, __val);
	}
      return __last;
    }

  /**
   *  @if maint
   *  This is an uglified
   *  search_n(_ForwardIterator, _ForwardIterator, _Integer, const _Tp&)
   *  overloaded for random access iterators.
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Integer, typename _Tp>
    _RandomAccessIter
    __search_n(_RandomAccessIter __first, _RandomAccessIter __last,
	       _Integer __count, const _Tp& __val, 
	       std::random_access_iterator_tag)
    {
      
      typedef typename std::iterator_traits<_RandomAccessIter>::difference_type
	_DistanceType;

      _DistanceType __tailSize = __last - __first;
      const _DistanceType __pattSize = __count;

      if (__tailSize < __pattSize)
        return __last;

      const _DistanceType __skipOffset = __pattSize - 1;
      _RandomAccessIter __lookAhead = __first + __skipOffset;
      __tailSize -= __pattSize;

      while (1) // the main loop...
	{
	  // __lookAhead here is always pointing to the last element of next 
	  // possible match.
	  while (!(*__lookAhead == __val)) // the skip loop...
	    {
	      if (__tailSize < __pattSize)
		return __last;  // Failure
	      __lookAhead += __pattSize;
	      __tailSize -= __pattSize;
	    }
	  _DistanceType __remainder = __skipOffset;
	  for (_RandomAccessIter __backTrack = __lookAhead - 1; 
	       *__backTrack == __val; --__backTrack)
	    {
	      if (--__remainder == 0)
		return (__lookAhead - __skipOffset); // Success
	    }
	  if (__remainder > __tailSize)
	    return __last; // Failure
	  __lookAhead += __remainder;
	  __tailSize -= __remainder;
	}
    }

  /**
   *  @brief Search a sequence for a number of consecutive values.
   *  @param  first  A forward iterator.
   *  @param  last   A forward iterator.
   *  @param  count  The number of consecutive values.
   *  @param  val    The value to find.
   *  @return   The first iterator @c i in the range @p [first,last-count)
   *  such that @c *(i+N) == @p val for each @c N in the range @p [0,count),
   *  or @p last if no such iterator exists.
   *
   *  Searches the range @p [first,last) for @p count consecutive elements
   *  equal to @p val.
  */
  template<typename _ForwardIterator, typename _Integer, typename _Tp>
    _ForwardIterator
    search_n(_ForwardIterator __first, _ForwardIterator __last,
	     _Integer __count, const _Tp& __val)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_EqualOpConcept<
	typename iterator_traits<_ForwardIterator>::value_type, _Tp>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__count <= 0)
	return __first;
      if (__count == 1)
	return std::find(__first, __last, __val);
      return std::__search_n(__first, __last, __count, __val,
			     std::__iterator_category(__first));
    }

  /**
   *  @if maint
   *  This is an uglified
   *  search_n(_ForwardIterator, _ForwardIterator, _Integer, const _Tp&,
   *	       _BinaryPredicate)
   *  overloaded for forward iterators.
   *  @endif
  */
  template<typename _ForwardIterator, typename _Integer, typename _Tp,
           typename _BinaryPredicate>
    _ForwardIterator
    __search_n(_ForwardIterator __first, _ForwardIterator __last,
	       _Integer __count, const _Tp& __val,
	       _BinaryPredicate __binary_pred, std::forward_iterator_tag)
    {
      while (__first != __last && !__binary_pred(*__first, __val))
        ++__first;

      while (__first != __last)
	{
	  typename iterator_traits<_ForwardIterator>::difference_type
	    __n = __count;
	  _ForwardIterator __i = __first;
	  ++__i;
	  while (__i != __last && __n != 1 && __binary_pred(*__i, __val))
	    {
	      ++__i;
	      --__n;
	    }
	  if (__n == 1)
	    return __first;
	  if (__i == __last)
	    return __last;
	  __first = ++__i;
	  while (__first != __last && !__binary_pred(*__first, __val))
	    ++__first;
	}
      return __last;
    }

  /**
   *  @if maint
   *  This is an uglified
   *  search_n(_ForwardIterator, _ForwardIterator, _Integer, const _Tp&,
   *	       _BinaryPredicate)
   *  overloaded for random access iterators.
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Integer, typename _Tp,
	   typename _BinaryPredicate>
    _RandomAccessIter
    __search_n(_RandomAccessIter __first, _RandomAccessIter __last,
	       _Integer __count, const _Tp& __val,
	       _BinaryPredicate __binary_pred, std::random_access_iterator_tag)
    {
      
      typedef typename std::iterator_traits<_RandomAccessIter>::difference_type
	_DistanceType;

      _DistanceType __tailSize = __last - __first;
      const _DistanceType __pattSize = __count;

      if (__tailSize < __pattSize)
        return __last;

      const _DistanceType __skipOffset = __pattSize - 1;
      _RandomAccessIter __lookAhead = __first + __skipOffset;
      __tailSize -= __pattSize;

      while (1) // the main loop...
	{
	  // __lookAhead here is always pointing to the last element of next 
	  // possible match.
	  while (!__binary_pred(*__lookAhead, __val)) // the skip loop...
	    {
	      if (__tailSize < __pattSize)
		return __last;  // Failure
	      __lookAhead += __pattSize;
	      __tailSize -= __pattSize;
	    }
	  _DistanceType __remainder = __skipOffset;
	  for (_RandomAccessIter __backTrack = __lookAhead - 1; 
	       __binary_pred(*__backTrack, __val); --__backTrack)
	    {
	      if (--__remainder == 0)
		return (__lookAhead - __skipOffset); // Success
	    }
	  if (__remainder > __tailSize)
	    return __last; // Failure
	  __lookAhead += __remainder;
	  __tailSize -= __remainder;
	}
    }

  /**
   *  @brief Search a sequence for a number of consecutive values using a
   *         predicate.
   *  @param  first        A forward iterator.
   *  @param  last         A forward iterator.
   *  @param  count        The number of consecutive values.
   *  @param  val          The value to find.
   *  @param  binary_pred  A binary predicate.
   *  @return   The first iterator @c i in the range @p [first,last-count)
   *  such that @p binary_pred(*(i+N),val) is true for each @c N in the
   *  range @p [0,count), or @p last if no such iterator exists.
   *
   *  Searches the range @p [first,last) for @p count consecutive elements
   *  for which the predicate returns true.
  */
  template<typename _ForwardIterator, typename _Integer, typename _Tp,
           typename _BinaryPredicate>
    _ForwardIterator
    search_n(_ForwardIterator __first, _ForwardIterator __last,
	     _Integer __count, const _Tp& __val,
	     _BinaryPredicate __binary_pred)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
	    typename iterator_traits<_ForwardIterator>::value_type, _Tp>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__count <= 0)
	return __first;
      if (__count == 1)
	{
	  while (__first != __last && !__binary_pred(*__first, __val))
	    ++__first;
	  return __first;
	}
      return std::__search_n(__first, __last, __count, __val, __binary_pred,
			     std::__iterator_category(__first));
    }

  /**
   *  @brief Swap the elements of two sequences.
   *  @param  first1  A forward iterator.
   *  @param  last1   A forward iterator.
   *  @param  first2  A forward iterator.
   *  @return   An iterator equal to @p first2+(last1-first1).
   *
   *  Swaps each element in the range @p [first1,last1) with the
   *  corresponding element in the range @p [first2,(last1-first1)).
   *  The ranges must not overlap.
  */
  template<typename _ForwardIterator1, typename _ForwardIterator2>
    _ForwardIterator2
    swap_ranges(_ForwardIterator1 __first1, _ForwardIterator1 __last1,
		_ForwardIterator2 __first2)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_ForwardIteratorConcept<
				  _ForwardIterator1>)
      __glibcxx_function_requires(_Mutable_ForwardIteratorConcept<
				  _ForwardIterator2>)
      __glibcxx_function_requires(_ConvertibleConcept<
	    typename iterator_traits<_ForwardIterator1>::value_type,
	    typename iterator_traits<_ForwardIterator2>::value_type>)
      __glibcxx_function_requires(_ConvertibleConcept<
	    typename iterator_traits<_ForwardIterator2>::value_type,
	    typename iterator_traits<_ForwardIterator1>::value_type>)
      __glibcxx_requires_valid_range(__first1, __last1);

      for ( ; __first1 != __last1; ++__first1, ++__first2)
	std::iter_swap(__first1, __first2);
      return __first2;
    }

  /**
   *  @brief Perform an operation on a sequence.
   *  @param  first     An input iterator.
   *  @param  last      An input iterator.
   *  @param  result    An output iterator.
   *  @param  unary_op  A unary operator.
   *  @return   An output iterator equal to @p result+(last-first).
   *
   *  Applies the operator to each element in the input range and assigns
   *  the results to successive elements of the output sequence.
   *  Evaluates @p *(result+N)=unary_op(*(first+N)) for each @c N in the
   *  range @p [0,last-first).
   *
   *  @p unary_op must not alter its argument.
  */
  template<typename _InputIterator, typename _OutputIterator,
	   typename _UnaryOperation>
    _OutputIterator
    transform(_InputIterator __first, _InputIterator __last,
	      _OutputIterator __result, _UnaryOperation __unary_op)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
            // "the type returned by a _UnaryOperation"
            __typeof__(__unary_op(*__first))>)
      __glibcxx_requires_valid_range(__first, __last);

      for ( ; __first != __last; ++__first, ++__result)
	*__result = __unary_op(*__first);
      return __result;
    }

  /**
   *  @brief Perform an operation on corresponding elements of two sequences.
   *  @param  first1     An input iterator.
   *  @param  last1      An input iterator.
   *  @param  first2     An input iterator.
   *  @param  result     An output iterator.
   *  @param  binary_op  A binary operator.
   *  @return   An output iterator equal to @p result+(last-first).
   *
   *  Applies the operator to the corresponding elements in the two
   *  input ranges and assigns the results to successive elements of the
   *  output sequence.
   *  Evaluates @p *(result+N)=binary_op(*(first1+N),*(first2+N)) for each
   *  @c N in the range @p [0,last1-first1).
   *
   *  @p binary_op must not alter either of its arguments.
  */
  template<typename _InputIterator1, typename _InputIterator2,
	   typename _OutputIterator, typename _BinaryOperation>
    _OutputIterator
    transform(_InputIterator1 __first1, _InputIterator1 __last1,
	      _InputIterator2 __first2, _OutputIterator __result,
	      _BinaryOperation __binary_op)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
            // "the type returned by a _BinaryOperation"
            __typeof__(__binary_op(*__first1,*__first2))>)
      __glibcxx_requires_valid_range(__first1, __last1);

      for ( ; __first1 != __last1; ++__first1, ++__first2, ++__result)
	*__result = __binary_op(*__first1, *__first2);
      return __result;
    }

  /**
   *  @brief Replace each occurrence of one value in a sequence with another
   *         value.
   *  @param  first      A forward iterator.
   *  @param  last       A forward iterator.
   *  @param  old_value  The value to be replaced.
   *  @param  new_value  The replacement value.
   *  @return   replace() returns no value.
   *
   *  For each iterator @c i in the range @p [first,last) if @c *i ==
   *  @p old_value then the assignment @c *i = @p new_value is performed.
  */
  template<typename _ForwardIterator, typename _Tp>
    void
    replace(_ForwardIterator __first, _ForwardIterator __last,
	    const _Tp& __old_value, const _Tp& __new_value)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_ForwardIteratorConcept<
				  _ForwardIterator>)
      __glibcxx_function_requires(_EqualOpConcept<
	    typename iterator_traits<_ForwardIterator>::value_type, _Tp>)
      __glibcxx_function_requires(_ConvertibleConcept<_Tp,
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      for ( ; __first != __last; ++__first)
	if (*__first == __old_value)
	  *__first = __new_value;
    }

  /**
   *  @brief Replace each value in a sequence for which a predicate returns
   *         true with another value.
   *  @param  first      A forward iterator.
   *  @param  last       A forward iterator.
   *  @param  pred       A predicate.
   *  @param  new_value  The replacement value.
   *  @return   replace_if() returns no value.
   *
   *  For each iterator @c i in the range @p [first,last) if @p pred(*i)
   *  is true then the assignment @c *i = @p new_value is performed.
  */
  template<typename _ForwardIterator, typename _Predicate, typename _Tp>
    void
    replace_if(_ForwardIterator __first, _ForwardIterator __last,
	       _Predicate __pred, const _Tp& __new_value)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_ForwardIteratorConcept<
				  _ForwardIterator>)
      __glibcxx_function_requires(_ConvertibleConcept<_Tp,
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      for ( ; __first != __last; ++__first)
	if (__pred(*__first))
	  *__first = __new_value;
    }

  /**
   *  @brief Copy a sequence, replacing each element of one value with another
   *         value.
   *  @param  first      An input iterator.
   *  @param  last       An input iterator.
   *  @param  result     An output iterator.
   *  @param  old_value  The value to be replaced.
   *  @param  new_value  The replacement value.
   *  @return   The end of the output sequence, @p result+(last-first).
   *
   *  Copies each element in the input range @p [first,last) to the
   *  output range @p [result,result+(last-first)) replacing elements
   *  equal to @p old_value with @p new_value.
  */
  template<typename _InputIterator, typename _OutputIterator, typename _Tp>
    _OutputIterator
    replace_copy(_InputIterator __first, _InputIterator __last,
		 _OutputIterator __result,
		 const _Tp& __old_value, const _Tp& __new_value)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
	    typename iterator_traits<_InputIterator>::value_type>)
      __glibcxx_function_requires(_EqualOpConcept<
	    typename iterator_traits<_InputIterator>::value_type, _Tp>)
      __glibcxx_requires_valid_range(__first, __last);

      for ( ; __first != __last; ++__first, ++__result)
	if (*__first == __old_value)
	  *__result = __new_value;
	else
	  *__result = *__first;
      return __result;
    }

  /**
   *  @brief Copy a sequence, replacing each value for which a predicate
   *         returns true with another value.
   *  @param  first      An input iterator.
   *  @param  last       An input iterator.
   *  @param  result     An output iterator.
   *  @param  pred       A predicate.
   *  @param  new_value  The replacement value.
   *  @return   The end of the output sequence, @p result+(last-first).
   *
   *  Copies each element in the range @p [first,last) to the range
   *  @p [result,result+(last-first)) replacing elements for which
   *  @p pred returns true with @p new_value.
  */
  template<typename _InputIterator, typename _OutputIterator,
	   typename _Predicate, typename _Tp>
    _OutputIterator
    replace_copy_if(_InputIterator __first, _InputIterator __last,
		    _OutputIterator __result,
		    _Predicate __pred, const _Tp& __new_value)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
	    typename iterator_traits<_InputIterator>::value_type>)
      __glibcxx_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_InputIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      for ( ; __first != __last; ++__first, ++__result)
	if (__pred(*__first))
	  *__result = __new_value;
	else
	  *__result = *__first;
      return __result;
    }

  /**
   *  @brief Assign the result of a function object to each value in a
   *         sequence.
   *  @param  first  A forward iterator.
   *  @param  last   A forward iterator.
   *  @param  gen    A function object taking no arguments.
   *  @return   generate() returns no value.
   *
   *  Performs the assignment @c *i = @p gen() for each @c i in the range
   *  @p [first,last).
  */
  template<typename _ForwardIterator, typename _Generator>
    void
    generate(_ForwardIterator __first, _ForwardIterator __last,
	     _Generator __gen)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_GeneratorConcept<_Generator,
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      for ( ; __first != __last; ++__first)
	*__first = __gen();
    }

  /**
   *  @brief Assign the result of a function object to each value in a
   *         sequence.
   *  @param  first  A forward iterator.
   *  @param  n      The length of the sequence.
   *  @param  gen    A function object taking no arguments.
   *  @return   The end of the sequence, @p first+n
   *
   *  Performs the assignment @c *i = @p gen() for each @c i in the range
   *  @p [first,first+n).
  */
  template<typename _OutputIterator, typename _Size, typename _Generator>
    _OutputIterator
    generate_n(_OutputIterator __first, _Size __n, _Generator __gen)
    {
      // concept requirements
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
            // "the type returned by a _Generator"
            __typeof__(__gen())>)

      for ( ; __n > 0; --__n, ++__first)
	*__first = __gen();
      return __first;
    }

  /**
   *  @brief Copy a sequence, removing elements of a given value.
   *  @param  first   An input iterator.
   *  @param  last    An input iterator.
   *  @param  result  An output iterator.
   *  @param  value   The value to be removed.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  Copies each element in the range @p [first,last) not equal to @p value
   *  to the range beginning at @p result.
   *  remove_copy() is stable, so the relative order of elements that are
   *  copied is unchanged.
  */
  template<typename _InputIterator, typename _OutputIterator, typename _Tp>
    _OutputIterator
    remove_copy(_InputIterator __first, _InputIterator __last,
		_OutputIterator __result, const _Tp& __value)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
	    typename iterator_traits<_InputIterator>::value_type>)
      __glibcxx_function_requires(_EqualOpConcept<
	    typename iterator_traits<_InputIterator>::value_type, _Tp>)
      __glibcxx_requires_valid_range(__first, __last);

      for ( ; __first != __last; ++__first)
	if (!(*__first == __value))
	  {
	    *__result = *__first;
	    ++__result;
	  }
      return __result;
    }

  /**
   *  @brief Copy a sequence, removing elements for which a predicate is true.
   *  @param  first   An input iterator.
   *  @param  last    An input iterator.
   *  @param  result  An output iterator.
   *  @param  pred    A predicate.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  Copies each element in the range @p [first,last) for which
   *  @p pred returns true to the range beginning at @p result.
   *
   *  remove_copy_if() is stable, so the relative order of elements that are
   *  copied is unchanged.
  */
  template<typename _InputIterator, typename _OutputIterator,
	   typename _Predicate>
    _OutputIterator
    remove_copy_if(_InputIterator __first, _InputIterator __last,
		   _OutputIterator __result, _Predicate __pred)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
	    typename iterator_traits<_InputIterator>::value_type>)
      __glibcxx_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_InputIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      for ( ; __first != __last; ++__first)
	if (!__pred(*__first))
	  {
	    *__result = *__first;
	    ++__result;
	  }
      return __result;
    }

  /**
   *  @brief Remove elements from a sequence.
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  value  The value to be removed.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  All elements equal to @p value are removed from the range
   *  @p [first,last).
   *
   *  remove() is stable, so the relative order of elements that are
   *  not removed is unchanged.
   *
   *  Elements between the end of the resulting sequence and @p last
   *  are still present, but their value is unspecified.
  */
  template<typename _ForwardIterator, typename _Tp>
    _ForwardIterator
    remove(_ForwardIterator __first, _ForwardIterator __last,
	   const _Tp& __value)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_ForwardIteratorConcept<
				  _ForwardIterator>)
      __glibcxx_function_requires(_EqualOpConcept<
	    typename iterator_traits<_ForwardIterator>::value_type, _Tp>)
      __glibcxx_requires_valid_range(__first, __last);

      __first = std::find(__first, __last, __value);
      _ForwardIterator __i = __first;
      return __first == __last ? __first
			       : std::remove_copy(++__i, __last,
						  __first, __value);
    }

  /**
   *  @brief Remove elements from a sequence using a predicate.
   *  @param  first  A forward iterator.
   *  @param  last   A forward iterator.
   *  @param  pred   A predicate.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  All elements for which @p pred returns true are removed from the range
   *  @p [first,last).
   *
   *  remove_if() is stable, so the relative order of elements that are
   *  not removed is unchanged.
   *
   *  Elements between the end of the resulting sequence and @p last
   *  are still present, but their value is unspecified.
  */
  template<typename _ForwardIterator, typename _Predicate>
    _ForwardIterator
    remove_if(_ForwardIterator __first, _ForwardIterator __last,
	      _Predicate __pred)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_ForwardIteratorConcept<
				  _ForwardIterator>)
      __glibcxx_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      __first = std::find_if(__first, __last, __pred);
      _ForwardIterator __i = __first;
      return __first == __last ? __first
			       : std::remove_copy_if(++__i, __last,
						     __first, __pred);
    }

  /**
   *  @if maint
   *  This is an uglified unique_copy(_InputIterator, _InputIterator,
   *                                  _OutputIterator)
   *  overloaded for forward iterators and output iterator as result.
   *  @endif
  */
  template<typename _ForwardIterator, typename _OutputIterator>
    _OutputIterator
    __unique_copy(_ForwardIterator __first, _ForwardIterator __last,
		  _OutputIterator __result,
		  forward_iterator_tag, output_iterator_tag)
    {
      // concept requirements -- taken care of in dispatching function
      _ForwardIterator __next = __first;
      *__result = *__first;
      while (++__next != __last)
	if (!(*__first == *__next))
	  {
	    __first = __next;
	    *++__result = *__first;
	  }
      return ++__result;
    }

  /**
   *  @if maint
   *  This is an uglified unique_copy(_InputIterator, _InputIterator,
   *                                  _OutputIterator)
   *  overloaded for input iterators and output iterator as result.
   *  @endif
  */
  template<typename _InputIterator, typename _OutputIterator>
    _OutputIterator
    __unique_copy(_InputIterator __first, _InputIterator __last,
		  _OutputIterator __result,
		  input_iterator_tag, output_iterator_tag)
    {
      // concept requirements -- taken care of in dispatching function
      typename iterator_traits<_InputIterator>::value_type __value = *__first;
      *__result = __value;
      while (++__first != __last)
	if (!(__value == *__first))
	  {
	    __value = *__first;
	    *++__result = __value;
	  }
      return ++__result;
    }

  /**
   *  @if maint
   *  This is an uglified unique_copy(_InputIterator, _InputIterator,
   *                                  _OutputIterator)
   *  overloaded for input iterators and forward iterator as result.
   *  @endif
  */
  template<typename _InputIterator, typename _ForwardIterator>
    _ForwardIterator
    __unique_copy(_InputIterator __first, _InputIterator __last,
		  _ForwardIterator __result,
		  input_iterator_tag, forward_iterator_tag)
    {
      // concept requirements -- taken care of in dispatching function
      *__result = *__first;
      while (++__first != __last)
	if (!(*__result == *__first))
	  *++__result = *__first;
      return ++__result;
    }

  /**
   *  @if maint
   *  This is an uglified
   *  unique_copy(_InputIterator, _InputIterator, _OutputIterator,
   *              _BinaryPredicate)
   *  overloaded for forward iterators and output iterator as result.
   *  @endif
  */
  template<typename _ForwardIterator, typename _OutputIterator,
	   typename _BinaryPredicate>
    _OutputIterator
    __unique_copy(_ForwardIterator __first, _ForwardIterator __last,
		  _OutputIterator __result, _BinaryPredicate __binary_pred,
		  forward_iterator_tag, output_iterator_tag)
    {
      // concept requirements -- iterators already checked
      __glibcxx_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
	  typename iterator_traits<_ForwardIterator>::value_type,
	  typename iterator_traits<_ForwardIterator>::value_type>)

      _ForwardIterator __next = __first;
      *__result = *__first;
      while (++__next != __last)
	if (!__binary_pred(*__first, *__next))
	  {
	    __first = __next;
	    *++__result = *__first;
	  }
      return ++__result;
    }

  /**
   *  @if maint
   *  This is an uglified
   *  unique_copy(_InputIterator, _InputIterator, _OutputIterator,
   *              _BinaryPredicate)
   *  overloaded for input iterators and output iterator as result.
   *  @endif
  */
  template<typename _InputIterator, typename _OutputIterator,
	   typename _BinaryPredicate>
    _OutputIterator
    __unique_copy(_InputIterator __first, _InputIterator __last,
		  _OutputIterator __result, _BinaryPredicate __binary_pred,
		  input_iterator_tag, output_iterator_tag)
    {
      // concept requirements -- iterators already checked
      __glibcxx_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
	  typename iterator_traits<_InputIterator>::value_type,
	  typename iterator_traits<_InputIterator>::value_type>)

      typename iterator_traits<_InputIterator>::value_type __value = *__first;
      *__result = __value;
      while (++__first != __last)
	if (!__binary_pred(__value, *__first))
	  {
	    __value = *__first;
	    *++__result = __value;
	  }
      return ++__result;
    }

  /**
   *  @if maint
   *  This is an uglified
   *  unique_copy(_InputIterator, _InputIterator, _OutputIterator,
   *              _BinaryPredicate)
   *  overloaded for input iterators and forward iterator as result.
   *  @endif
  */
  template<typename _InputIterator, typename _ForwardIterator,
	   typename _BinaryPredicate>
    _ForwardIterator
    __unique_copy(_InputIterator __first, _InputIterator __last,
		  _ForwardIterator __result, _BinaryPredicate __binary_pred,
		  input_iterator_tag, forward_iterator_tag)
    {
      // concept requirements -- iterators already checked
      __glibcxx_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
	  typename iterator_traits<_ForwardIterator>::value_type,
	  typename iterator_traits<_InputIterator>::value_type>)

      *__result = *__first;
      while (++__first != __last)
	if (!__binary_pred(*__result, *__first))
	  *++__result = *__first;
      return ++__result;
    }

  /**
   *  @brief Copy a sequence, removing consecutive duplicate values.
   *  @param  first   An input iterator.
   *  @param  last    An input iterator.
   *  @param  result  An output iterator.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  Copies each element in the range @p [first,last) to the range
   *  beginning at @p result, except that only the first element is copied
   *  from groups of consecutive elements that compare equal.
   *  unique_copy() is stable, so the relative order of elements that are
   *  copied is unchanged.
   *
   *  @if maint
   *  _GLIBCXX_RESOLVE_LIB_DEFECTS
   *  DR 241. Does unique_copy() require CopyConstructible and Assignable?
   *  
   *  _GLIBCXX_RESOLVE_LIB_DEFECTS
   *  DR 538. 241 again: Does unique_copy() require CopyConstructible and 
   *  Assignable?
   *  @endif
  */
  template<typename _InputIterator, typename _OutputIterator>
    inline _OutputIterator
    unique_copy(_InputIterator __first, _InputIterator __last,
		_OutputIterator __result)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
	    typename iterator_traits<_InputIterator>::value_type>)
      __glibcxx_function_requires(_EqualityComparableConcept<
	    typename iterator_traits<_InputIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return __result;
      return std::__unique_copy(__first, __last, __result,
				std::__iterator_category(__first),
				std::__iterator_category(__result));
    }

  /**
   *  @brief Copy a sequence, removing consecutive values using a predicate.
   *  @param  first        An input iterator.
   *  @param  last         An input iterator.
   *  @param  result       An output iterator.
   *  @param  binary_pred  A binary predicate.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  Copies each element in the range @p [first,last) to the range
   *  beginning at @p result, except that only the first element is copied
   *  from groups of consecutive elements for which @p binary_pred returns
   *  true.
   *  unique_copy() is stable, so the relative order of elements that are
   *  copied is unchanged.
   *
   *  @if maint
   *  _GLIBCXX_RESOLVE_LIB_DEFECTS
   *  DR 241. Does unique_copy() require CopyConstructible and Assignable?
   *  @endif
  */
  template<typename _InputIterator, typename _OutputIterator,
	   typename _BinaryPredicate>
    inline _OutputIterator
    unique_copy(_InputIterator __first, _InputIterator __last,
		_OutputIterator __result,
		_BinaryPredicate __binary_pred)
    {
      // concept requirements -- predicates checked later
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
	    typename iterator_traits<_InputIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return __result;
      return std::__unique_copy(__first, __last, __result, __binary_pred,
				std::__iterator_category(__first),
				std::__iterator_category(__result));
    }

  /**
   *  @brief Remove consecutive duplicate values from a sequence.
   *  @param  first  A forward iterator.
   *  @param  last   A forward iterator.
   *  @return  An iterator designating the end of the resulting sequence.
   *
   *  Removes all but the first element from each group of consecutive
   *  values that compare equal.
   *  unique() is stable, so the relative order of elements that are
   *  not removed is unchanged.
   *  Elements between the end of the resulting sequence and @p last
   *  are still present, but their value is unspecified.
  */
  template<typename _ForwardIterator>
    _ForwardIterator
    unique(_ForwardIterator __first, _ForwardIterator __last)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_ForwardIteratorConcept<
				  _ForwardIterator>)
      __glibcxx_function_requires(_EqualityComparableConcept<
		     typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      // Skip the beginning, if already unique.
      __first = std::adjacent_find(__first, __last);
      if (__first == __last)
	return __last;

      // Do the real copy work.
      _ForwardIterator __dest = __first;
      ++__first;
      while (++__first != __last)
	if (!(*__dest == *__first))
	  *++__dest = *__first;
      return ++__dest;
    }

  /**
   *  @brief Remove consecutive values from a sequence using a predicate.
   *  @param  first        A forward iterator.
   *  @param  last         A forward iterator.
   *  @param  binary_pred  A binary predicate.
   *  @return  An iterator designating the end of the resulting sequence.
   *
   *  Removes all but the first element from each group of consecutive
   *  values for which @p binary_pred returns true.
   *  unique() is stable, so the relative order of elements that are
   *  not removed is unchanged.
   *  Elements between the end of the resulting sequence and @p last
   *  are still present, but their value is unspecified.
  */
  template<typename _ForwardIterator, typename _BinaryPredicate>
    _ForwardIterator
    unique(_ForwardIterator __first, _ForwardIterator __last,
           _BinaryPredicate __binary_pred)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_ForwardIteratorConcept<
				  _ForwardIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
		typename iterator_traits<_ForwardIterator>::value_type,
		typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      // Skip the beginning, if already unique.
      __first = std::adjacent_find(__first, __last, __binary_pred);
      if (__first == __last)
	return __last;

      // Do the real copy work.
      _ForwardIterator __dest = __first;
      ++__first;
      while (++__first != __last)
	if (!__binary_pred(*__dest, *__first))
	  *++__dest = *__first;
      return ++__dest;
    }

  /**
   *  @if maint
   *  This is an uglified reverse(_BidirectionalIterator,
   *                              _BidirectionalIterator)
   *  overloaded for bidirectional iterators.
   *  @endif
  */
  template<typename _BidirectionalIterator>
    void
    __reverse(_BidirectionalIterator __first, _BidirectionalIterator __last,
	      bidirectional_iterator_tag)
    {
      while (true)
	if (__first == __last || __first == --__last)
	  return;
	else
	  {
	    std::iter_swap(__first, __last);
	    ++__first;
	  }
    }

  /**
   *  @if maint
   *  This is an uglified reverse(_BidirectionalIterator,
   *                              _BidirectionalIterator)
   *  overloaded for random access iterators.
   *  @endif
  */
  template<typename _RandomAccessIterator>
    void
    __reverse(_RandomAccessIterator __first, _RandomAccessIterator __last,
	      random_access_iterator_tag)
    {
      if (__first == __last)
	return;
      --__last;
      while (__first < __last)
	{
	  std::iter_swap(__first, __last);
	  ++__first;
	  --__last;
	}
    }

  /**
   *  @brief Reverse a sequence.
   *  @param  first  A bidirectional iterator.
   *  @param  last   A bidirectional iterator.
   *  @return   reverse() returns no value.
   *
   *  Reverses the order of the elements in the range @p [first,last),
   *  so that the first element becomes the last etc.
   *  For every @c i such that @p 0<=i<=(last-first)/2), @p reverse()
   *  swaps @p *(first+i) and @p *(last-(i+1))
  */
  template<typename _BidirectionalIterator>
    inline void
    reverse(_BidirectionalIterator __first, _BidirectionalIterator __last)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_BidirectionalIteratorConcept<
				  _BidirectionalIterator>)
      __glibcxx_requires_valid_range(__first, __last);
      std::__reverse(__first, __last, std::__iterator_category(__first));
    }

  /**
   *  @brief Copy a sequence, reversing its elements.
   *  @param  first   A bidirectional iterator.
   *  @param  last    A bidirectional iterator.
   *  @param  result  An output iterator.
   *  @return  An iterator designating the end of the resulting sequence.
   *
   *  Copies the elements in the range @p [first,last) to the range
   *  @p [result,result+(last-first)) such that the order of the
   *  elements is reversed.
   *  For every @c i such that @p 0<=i<=(last-first), @p reverse_copy()
   *  performs the assignment @p *(result+(last-first)-i) = *(first+i).
   *  The ranges @p [first,last) and @p [result,result+(last-first))
   *  must not overlap.
  */
  template<typename _BidirectionalIterator, typename _OutputIterator>
    _OutputIterator
    reverse_copy(_BidirectionalIterator __first, _BidirectionalIterator __last,
			     _OutputIterator __result)
    {
      // concept requirements
      __glibcxx_function_requires(_BidirectionalIteratorConcept<
				  _BidirectionalIterator>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
		typename iterator_traits<_BidirectionalIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      while (__first != __last)
	{
	  --__last;
	  *__result = *__last;
	  ++__result;
	}
      return __result;
    }


  /**
   *  @if maint
   *  This is a helper function for the rotate algorithm specialized on RAIs.
   *  It returns the greatest common divisor of two integer values.
   *  @endif
  */
  template<typename _EuclideanRingElement>
    _EuclideanRingElement
    __gcd(_EuclideanRingElement __m, _EuclideanRingElement __n)
    {
      while (__n != 0)
	{
	  _EuclideanRingElement __t = __m % __n;
	  __m = __n;
	  __n = __t;
	}
      return __m;
    }

  /**
   *  @if maint
   *  This is a helper function for the rotate algorithm.
   *  @endif
  */
  template<typename _ForwardIterator>
    void
    __rotate(_ForwardIterator __first,
	     _ForwardIterator __middle,
	     _ForwardIterator __last,
	     forward_iterator_tag)
    {
      if (__first == __middle || __last  == __middle)
	return;

      _ForwardIterator __first2 = __middle;
      do
	{
	  swap(*__first, *__first2);
	  ++__first;
	  ++__first2;
	  if (__first == __middle)
	    __middle = __first2;
	}
      while (__first2 != __last);

      __first2 = __middle;

      while (__first2 != __last)
	{
	  swap(*__first, *__first2);
	  ++__first;
	  ++__first2;
	  if (__first == __middle)
	    __middle = __first2;
	  else if (__first2 == __last)
	    __first2 = __middle;
	}
    }

  /**
   *  @if maint
   *  This is a helper function for the rotate algorithm.
   *  @endif
  */
  template<typename _BidirectionalIterator>
    void
    __rotate(_BidirectionalIterator __first,
	     _BidirectionalIterator __middle,
	     _BidirectionalIterator __last,
	      bidirectional_iterator_tag)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_BidirectionalIteratorConcept<
				  _BidirectionalIterator>)

      if (__first == __middle || __last  == __middle)
	return;

      std::__reverse(__first,  __middle, bidirectional_iterator_tag());
      std::__reverse(__middle, __last,   bidirectional_iterator_tag());

      while (__first != __middle && __middle != __last)
	{
	  swap(*__first, *--__last);
	  ++__first;
	}

      if (__first == __middle)
	std::__reverse(__middle, __last,   bidirectional_iterator_tag());
      else
	std::__reverse(__first,  __middle, bidirectional_iterator_tag());
    }

  /**
   *  @if maint
   *  This is a helper function for the rotate algorithm.
   *  @endif
  */
  template<typename _RandomAccessIterator>
    void
    __rotate(_RandomAccessIterator __first,
	     _RandomAccessIterator __middle,
	     _RandomAccessIterator __last,
	     random_access_iterator_tag)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_RandomAccessIteratorConcept<
				  _RandomAccessIterator>)

      if (__first == __middle || __last  == __middle)
	return;

      typedef typename iterator_traits<_RandomAccessIterator>::difference_type
	_Distance;
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      const _Distance __n = __last   - __first;
      const _Distance __k = __middle - __first;
      const _Distance __l = __n - __k;

      if (__k == __l)
	{
	  std::swap_ranges(__first, __middle, __middle);
	  return;
	}

      const _Distance __d = __gcd(__n, __k);

      for (_Distance __i = 0; __i < __d; __i++)
	{
	  _ValueType __tmp = *__first;
	  _RandomAccessIterator __p = __first;

	  if (__k < __l)
	    {
	      for (_Distance __j = 0; __j < __l / __d; __j++)
		{
		  if (__p > __first + __l)
		    {
		      *__p = *(__p - __l);
		      __p -= __l;
		    }

		  *__p = *(__p + __k);
		  __p += __k;
		}
	    }
	  else
	    {
	      for (_Distance __j = 0; __j < __k / __d - 1; __j ++)
		{
		  if (__p < __last - __k)
		    {
		      *__p = *(__p + __k);
		      __p += __k;
		    }
		  *__p = * (__p - __l);
		  __p -= __l;
		}
	    }

	  *__p = __tmp;
	  ++__first;
	}
    }

  /**
   *  @brief Rotate the elements of a sequence.
   *  @param  first   A forward iterator.
   *  @param  middle  A forward iterator.
   *  @param  last    A forward iterator.
   *  @return  Nothing.
   *
   *  Rotates the elements of the range @p [first,last) by @p (middle-first)
   *  positions so that the element at @p middle is moved to @p first, the
   *  element at @p middle+1 is moved to @first+1 and so on for each element
   *  in the range @p [first,last).
   *
   *  This effectively swaps the ranges @p [first,middle) and
   *  @p [middle,last).
   *
   *  Performs @p *(first+(n+(last-middle))%(last-first))=*(first+n) for
   *  each @p n in the range @p [0,last-first).
  */
  template<typename _ForwardIterator>
    inline void
    rotate(_ForwardIterator __first, _ForwardIterator __middle,
	   _ForwardIterator __last)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_ForwardIteratorConcept<
				  _ForwardIterator>)
      __glibcxx_requires_valid_range(__first, __middle);
      __glibcxx_requires_valid_range(__middle, __last);

      typedef typename iterator_traits<_ForwardIterator>::iterator_category
	_IterType;
      std::__rotate(__first, __middle, __last, _IterType());
    }

  /**
   *  @brief Copy a sequence, rotating its elements.
   *  @param  first   A forward iterator.
   *  @param  middle  A forward iterator.
   *  @param  last    A forward iterator.
   *  @param  result  An output iterator.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  Copies the elements of the range @p [first,last) to the range
   *  beginning at @result, rotating the copied elements by @p (middle-first)
   *  positions so that the element at @p middle is moved to @p result, the
   *  element at @p middle+1 is moved to @result+1 and so on for each element
   *  in the range @p [first,last).
   *
   *  Performs @p *(result+(n+(last-middle))%(last-first))=*(first+n) for
   *  each @p n in the range @p [0,last-first).
  */
  template<typename _ForwardIterator, typename _OutputIterator>
    _OutputIterator
    rotate_copy(_ForwardIterator __first, _ForwardIterator __middle,
                _ForwardIterator __last, _OutputIterator __result)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
		typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __middle);
      __glibcxx_requires_valid_range(__middle, __last);

      return std::copy(__first, __middle,
                       std::copy(__middle, __last, __result));
    }

  /**
   *  @brief Randomly shuffle the elements of a sequence.
   *  @param  first   A forward iterator.
   *  @param  last    A forward iterator.
   *  @return  Nothing.
   *
   *  Reorder the elements in the range @p [first,last) using a random
   *  distribution, so that every possible ordering of the sequence is
   *  equally likely.
  */
  template<typename _RandomAccessIterator>
    inline void
    random_shuffle(_RandomAccessIterator __first, _RandomAccessIterator __last)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIterator>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first != __last)
	for (_RandomAccessIterator __i = __first + 1; __i != __last; ++__i)
	  std::iter_swap(__i, __first + (std::rand() % ((__i - __first) + 1)));
    }

  /**
   *  @brief Shuffle the elements of a sequence using a random number
   *         generator.
   *  @param  first   A forward iterator.
   *  @param  last    A forward iterator.
   *  @param  rand    The RNG functor or function.
   *  @return  Nothing.
   *
   *  Reorders the elements in the range @p [first,last) using @p rand to
   *  provide a random distribution. Calling @p rand(N) for a positive
   *  integer @p N should return a randomly chosen integer from the
   *  range [0,N).
  */
  template<typename _RandomAccessIterator, typename _RandomNumberGenerator>
    void
    random_shuffle(_RandomAccessIterator __first, _RandomAccessIterator __last,
		   _RandomNumberGenerator& __rand)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIterator>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return;
      for (_RandomAccessIterator __i = __first + 1; __i != __last; ++__i)
	std::iter_swap(__i, __first + __rand((__i - __first) + 1));
    }


  /**
   *  @if maint
   *  This is a helper function...
   *  @endif
  */
  template<typename _ForwardIterator, typename _Predicate>
    _ForwardIterator
    __partition(_ForwardIterator __first, _ForwardIterator __last,
		_Predicate __pred,
		forward_iterator_tag)
    {
      if (__first == __last)
	return __first;

      while (__pred(*__first))
	if (++__first == __last)
	  return __first;

      _ForwardIterator __next = __first;

      while (++__next != __last)
	if (__pred(*__next))
	  {
	    swap(*__first, *__next);
	    ++__first;
	  }

      return __first;
    }

  /**
   *  @if maint
   *  This is a helper function...
   *  @endif
  */
  template<typename _BidirectionalIterator, typename _Predicate>
    _BidirectionalIterator
    __partition(_BidirectionalIterator __first, _BidirectionalIterator __last,
		_Predicate __pred,
		bidirectional_iterator_tag)
    {
      while (true)
	{
	  while (true)
	    if (__first == __last)
	      return __first;
	    else if (__pred(*__first))
	      ++__first;
	    else
	      break;
	  --__last;
	  while (true)
	    if (__first == __last)
	      return __first;
	    else if (!__pred(*__last))
	      --__last;
	    else
	      break;
	  std::iter_swap(__first, __last);
	  ++__first;
	}
    }

  /**
   *  @brief Move elements for which a predicate is true to the beginning
   *         of a sequence.
   *  @param  first   A forward iterator.
   *  @param  last    A forward iterator.
   *  @param  pred    A predicate functor.
   *  @return  An iterator @p middle such that @p pred(i) is true for each
   *  iterator @p i in the range @p [first,middle) and false for each @p i
   *  in the range @p [middle,last).
   *
   *  @p pred must not modify its operand. @p partition() does not preserve
   *  the relative ordering of elements in each group, use
   *  @p stable_partition() if this is needed.
  */
  template<typename _ForwardIterator, typename _Predicate>
    inline _ForwardIterator
    partition(_ForwardIterator __first, _ForwardIterator __last,
	      _Predicate   __pred)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_ForwardIteratorConcept<
				  _ForwardIterator>)
      __glibcxx_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      return std::__partition(__first, __last, __pred,
			      std::__iterator_category(__first));
    }


  /**
   *  @if maint
   *  This is a helper function...
   *  @endif
  */
  template<typename _ForwardIterator, typename _Predicate, typename _Distance>
    _ForwardIterator
    __inplace_stable_partition(_ForwardIterator __first,
			       _ForwardIterator __last,
			       _Predicate __pred, _Distance __len)
    {
      if (__len == 1)
	return __pred(*__first) ? __last : __first;
      _ForwardIterator __middle = __first;
      std::advance(__middle, __len / 2);
      _ForwardIterator __begin = std::__inplace_stable_partition(__first,
								 __middle,
								 __pred,
								 __len / 2);
      _ForwardIterator __end = std::__inplace_stable_partition(__middle, __last,
							       __pred,
							       __len
							       - __len / 2);
      std::rotate(__begin, __middle, __end);
      std::advance(__begin, std::distance(__middle, __end));
      return __begin;
    }

  /**
   *  @if maint
   *  This is a helper function...
   *  @endif
  */
  template<typename _ForwardIterator, typename _Pointer, typename _Predicate,
	   typename _Distance>
    _ForwardIterator
    __stable_partition_adaptive(_ForwardIterator __first,
				_ForwardIterator __last,
				_Predicate __pred, _Distance __len,
				_Pointer __buffer,
				_Distance __buffer_size)
    {
      if (__len <= __buffer_size)
	{
	  _ForwardIterator __result1 = __first;
	  _Pointer __result2 = __buffer;
	  for ( ; __first != __last ; ++__first)
	    if (__pred(*__first))
	      {
		*__result1 = *__first;
		++__result1;
	      }
	    else
	      {
		*__result2 = *__first;
		++__result2;
	      }
	  std::copy(__buffer, __result2, __result1);
	  return __result1;
	}
      else
	{
	  _ForwardIterator __middle = __first;
	  std::advance(__middle, __len / 2);
	  _ForwardIterator __begin =
	    std::__stable_partition_adaptive(__first, __middle, __pred,
					     __len / 2, __buffer,
					     __buffer_size);
	  _ForwardIterator __end =
	    std::__stable_partition_adaptive(__middle, __last, __pred,
					     __len - __len / 2,
					     __buffer, __buffer_size);
	  std::rotate(__begin, __middle, __end);
	  std::advance(__begin, std::distance(__middle, __end));
	  return __begin;
	}
    }

  /**
   *  @brief Move elements for which a predicate is true to the beginning
   *         of a sequence, preserving relative ordering.
   *  @param  first   A forward iterator.
   *  @param  last    A forward iterator.
   *  @param  pred    A predicate functor.
   *  @return  An iterator @p middle such that @p pred(i) is true for each
   *  iterator @p i in the range @p [first,middle) and false for each @p i
   *  in the range @p [middle,last).
   *
   *  Performs the same function as @p partition() with the additional
   *  guarantee that the relative ordering of elements in each group is
   *  preserved, so any two elements @p x and @p y in the range
   *  @p [first,last) such that @p pred(x)==pred(y) will have the same
   *  relative ordering after calling @p stable_partition().
  */
  template<typename _ForwardIterator, typename _Predicate>
    _ForwardIterator
    stable_partition(_ForwardIterator __first, _ForwardIterator __last,
		     _Predicate __pred)
    {
      // concept requirements
      __glibcxx_function_requires(_Mutable_ForwardIteratorConcept<
				  _ForwardIterator>)
      __glibcxx_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return __first;
      else
	{
	  typedef typename iterator_traits<_ForwardIterator>::value_type
	    _ValueType;
	  typedef typename iterator_traits<_ForwardIterator>::difference_type
	    _DistanceType;

	  _Temporary_buffer<_ForwardIterator, _ValueType> __buf(__first,
								__last);
	if (__buf.size() > 0)
	  return
	    std::__stable_partition_adaptive(__first, __last, __pred,
					  _DistanceType(__buf.requested_size()),
					  __buf.begin(), __buf.size());
	else
	  return
	    std::__inplace_stable_partition(__first, __last, __pred,
					 _DistanceType(__buf.requested_size()));
	}
    }

  /**
   *  @if maint
   *  This is a helper function...
   *  @endif
  */
  template<typename _RandomAccessIterator, typename _Tp>
    _RandomAccessIterator
    __unguarded_partition(_RandomAccessIterator __first,
			  _RandomAccessIterator __last, _Tp __pivot)
    {
      while (true)
	{
	  while (*__first < __pivot)
	    ++__first;
	  --__last;
	  while (__pivot < *__last)
	    --__last;
	  if (!(__first < __last))
	    return __first;
	  std::iter_swap(__first, __last);
	  ++__first;
	}
    }

  /**
   *  @if maint
   *  This is a helper function...
   *  @endif
  */
  template<typename _RandomAccessIterator, typename _Tp, typename _Compare>
    _RandomAccessIterator
    __unguarded_partition(_RandomAccessIterator __first,
			  _RandomAccessIterator __last,
			  _Tp __pivot, _Compare __comp)
    {
      while (true)
	{
	  while (__comp(*__first, __pivot))
	    ++__first;
	  --__last;
	  while (__comp(__pivot, *__last))
	    --__last;
	  if (!(__first < __last))
	    return __first;
	  std::iter_swap(__first, __last);
	  ++__first;
	}
    }

  /**
   *  @if maint
   *  @doctodo
   *  This controls some aspect of the sort routines.
   *  @endif
  */
  enum { _S_threshold = 16 };

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIterator, typename _Tp>
    void
    __unguarded_linear_insert(_RandomAccessIterator __last, _Tp __val)
    {
      _RandomAccessIterator __next = __last;
      --__next;
      while (__val < *__next)
	{
	  *__last = *__next;
	  __last = __next;
	  --__next;
	}
      *__last = __val;
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIterator, typename _Tp, typename _Compare>
    void
    __unguarded_linear_insert(_RandomAccessIterator __last, _Tp __val,
			      _Compare __comp)
    {
      _RandomAccessIterator __next = __last;
      --__next;
      while (__comp(__val, *__next))
	{
	  *__last = *__next;
	  __last = __next;
	  --__next;
	}
      *__last = __val;
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIterator>
    void
    __insertion_sort(_RandomAccessIterator __first,
		     _RandomAccessIterator __last)
    {
      if (__first == __last)
	return;

      for (_RandomAccessIterator __i = __first + 1; __i != __last; ++__i)
	{
	  typename iterator_traits<_RandomAccessIterator>::value_type
	    __val = *__i;
	  if (__val < *__first)
	    {
	      std::copy_backward(__first, __i, __i + 1);
	      *__first = __val;
	    }
	  else
	    std::__unguarded_linear_insert(__i, __val);
	}
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIterator, typename _Compare>
    void
    __insertion_sort(_RandomAccessIterator __first,
		     _RandomAccessIterator __last, _Compare __comp)
    {
      if (__first == __last) return;

      for (_RandomAccessIterator __i = __first + 1; __i != __last; ++__i)
	{
	  typename iterator_traits<_RandomAccessIterator>::value_type
	    __val = *__i;
	  if (__comp(__val, *__first))
	    {
	      std::copy_backward(__first, __i, __i + 1);
	      *__first = __val;
	    }
	  else
	    std::__unguarded_linear_insert(__i, __val, __comp);
	}
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIterator>
    inline void
    __unguarded_insertion_sort(_RandomAccessIterator __first,
			       _RandomAccessIterator __last)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      for (_RandomAccessIterator __i = __first; __i != __last; ++__i)
	std::__unguarded_linear_insert(__i, _ValueType(*__i));
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIterator, typename _Compare>
    inline void
    __unguarded_insertion_sort(_RandomAccessIterator __first,
			       _RandomAccessIterator __last, _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      for (_RandomAccessIterator __i = __first; __i != __last; ++__i)
	std::__unguarded_linear_insert(__i, _ValueType(*__i), __comp);
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIterator>
    void
    __final_insertion_sort(_RandomAccessIterator __first,
			   _RandomAccessIterator __last)
    {
      if (__last - __first > int(_S_threshold))
	{
	  std::__insertion_sort(__first, __first + int(_S_threshold));
	  std::__unguarded_insertion_sort(__first + int(_S_threshold), __last);
	}
      else
	std::__insertion_sort(__first, __last);
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIterator, typename _Compare>
    void
    __final_insertion_sort(_RandomAccessIterator __first,
			   _RandomAccessIterator __last, _Compare __comp)
    {
      if (__last - __first > int(_S_threshold))
	{
	  std::__insertion_sort(__first, __first + int(_S_threshold), __comp);
	  std::__unguarded_insertion_sort(__first + int(_S_threshold), __last,
					  __comp);
	}
      else
	std::__insertion_sort(__first, __last, __comp);
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routines.
   *  @endif
  */
  template<typename _RandomAccessIterator>
    void
    __heap_select(_RandomAccessIterator __first,
		  _RandomAccessIterator __middle,
		  _RandomAccessIterator __last)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      std::make_heap(__first, __middle);
      for (_RandomAccessIterator __i = __middle; __i < __last; ++__i)
	if (*__i < *__first)
	  std::__pop_heap(__first, __middle, __i, _ValueType(*__i));
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routines.
   *  @endif
  */
  template<typename _RandomAccessIterator, typename _Compare>
    void
    __heap_select(_RandomAccessIterator __first,
		  _RandomAccessIterator __middle,
		  _RandomAccessIterator __last, _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      std::make_heap(__first, __middle, __comp);
      for (_RandomAccessIterator __i = __middle; __i < __last; ++__i)
	if (__comp(*__i, *__first))
	  std::__pop_heap(__first, __middle, __i, _ValueType(*__i), __comp);
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routines.
   *  @endif
  */
  template<typename _Size>
    inline _Size
    __lg(_Size __n)
    {
      _Size __k;
      for (__k = 0; __n != 1; __n >>= 1)
	++__k;
      return __k;
    }

  /**
   *  @brief Sort the smallest elements of a sequence.
   *  @param  first   An iterator.
   *  @param  middle  Another iterator.
   *  @param  last    Another iterator.
   *  @return  Nothing.
   *
   *  Sorts the smallest @p (middle-first) elements in the range
   *  @p [first,last) and moves them to the range @p [first,middle). The
   *  order of the remaining elements in the range @p [middle,last) is
   *  undefined.
   *  After the sort if @p i and @j are iterators in the range
   *  @p [first,middle) such that @i precedes @j and @k is an iterator in
   *  the range @p [middle,last) then @p *j<*i and @p *k<*i are both false.
  */
  template<typename _RandomAccessIterator>
    inline void
    partial_sort(_RandomAccessIterator __first,
		 _RandomAccessIterator __middle,
		 _RandomAccessIterator __last)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      // concept requirements
      __glibcxx_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIterator>)
      __glibcxx_function_requires(_LessThanComparableConcept<_ValueType>)
      __glibcxx_requires_valid_range(__first, __middle);
      __glibcxx_requires_valid_range(__middle, __last);

      std::__heap_select(__first, __middle, __last);
      std::sort_heap(__first, __middle);
    }

  /**
   *  @brief Sort the smallest elements of a sequence using a predicate
   *         for comparison.
   *  @param  first   An iterator.
   *  @param  middle  Another iterator.
   *  @param  last    Another iterator.
   *  @param  comp    A comparison functor.
   *  @return  Nothing.
   *
   *  Sorts the smallest @p (middle-first) elements in the range
   *  @p [first,last) and moves them to the range @p [first,middle). The
   *  order of the remaining elements in the range @p [middle,last) is
   *  undefined.
   *  After the sort if @p i and @j are iterators in the range
   *  @p [first,middle) such that @i precedes @j and @k is an iterator in
   *  the range @p [middle,last) then @p *comp(j,*i) and @p comp(*k,*i)
   *  are both false.
  */
  template<typename _RandomAccessIterator, typename _Compare>
    inline void
    partial_sort(_RandomAccessIterator __first,
		 _RandomAccessIterator __middle,
		 _RandomAccessIterator __last,
		 _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      // concept requirements
      __glibcxx_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType, _ValueType>)
      __glibcxx_requires_valid_range(__first, __middle);
      __glibcxx_requires_valid_range(__middle, __last);

      std::__heap_select(__first, __middle, __last, __comp);
      std::sort_heap(__first, __middle, __comp);
    }

  /**
   *  @brief Copy the smallest elements of a sequence.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  result_first   A random-access iterator.
   *  @param  result_last    Another random-access iterator.
   *  @return   An iterator indicating the end of the resulting sequence.
   *
   *  Copies and sorts the smallest N values from the range @p [first,last)
   *  to the range beginning at @p result_first, where the number of
   *  elements to be copied, @p N, is the smaller of @p (last-first) and
   *  @p (result_last-result_first).
   *  After the sort if @p i and @j are iterators in the range
   *  @p [result_first,result_first+N) such that @i precedes @j then
   *  @p *j<*i is false.
   *  The value returned is @p result_first+N.
  */
  template<typename _InputIterator, typename _RandomAccessIterator>
    _RandomAccessIterator
    partial_sort_copy(_InputIterator __first, _InputIterator __last,
		      _RandomAccessIterator __result_first,
		      _RandomAccessIterator __result_last)
    {
      typedef typename iterator_traits<_InputIterator>::value_type
	_InputValueType;
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_OutputValueType;
      typedef typename iterator_traits<_RandomAccessIterator>::difference_type
	_DistanceType;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_ConvertibleConcept<_InputValueType,
				  _OutputValueType>)
      __glibcxx_function_requires(_LessThanOpConcept<_InputValueType,
				                     _OutputValueType>)
      __glibcxx_function_requires(_LessThanComparableConcept<_OutputValueType>)
      __glibcxx_requires_valid_range(__first, __last);
      __glibcxx_requires_valid_range(__result_first, __result_last);

      if (__result_first == __result_last)
	return __result_last;
      _RandomAccessIterator __result_real_last = __result_first;
      while(__first != __last && __result_real_last != __result_last)
	{
	  *__result_real_last = *__first;
	  ++__result_real_last;
	  ++__first;
	}
      std::make_heap(__result_first, __result_real_last);
      while (__first != __last)
	{
	  if (*__first < *__result_first)
	    std::__adjust_heap(__result_first, _DistanceType(0),
			       _DistanceType(__result_real_last
					     - __result_first),
			       _InputValueType(*__first));
	  ++__first;
	}
      std::sort_heap(__result_first, __result_real_last);
      return __result_real_last;
    }

  /**
   *  @brief Copy the smallest elements of a sequence using a predicate for
   *         comparison.
   *  @param  first   An input iterator.
   *  @param  last    Another input iterator.
   *  @param  result_first   A random-access iterator.
   *  @param  result_last    Another random-access iterator.
   *  @param  comp    A comparison functor.
   *  @return   An iterator indicating the end of the resulting sequence.
   *
   *  Copies and sorts the smallest N values from the range @p [first,last)
   *  to the range beginning at @p result_first, where the number of
   *  elements to be copied, @p N, is the smaller of @p (last-first) and
   *  @p (result_last-result_first).
   *  After the sort if @p i and @j are iterators in the range
   *  @p [result_first,result_first+N) such that @i precedes @j then
   *  @p comp(*j,*i) is false.
   *  The value returned is @p result_first+N.
  */
  template<typename _InputIterator, typename _RandomAccessIterator, typename _Compare>
    _RandomAccessIterator
    partial_sort_copy(_InputIterator __first, _InputIterator __last,
		      _RandomAccessIterator __result_first,
		      _RandomAccessIterator __result_last,
		      _Compare __comp)
    {
      typedef typename iterator_traits<_InputIterator>::value_type
	_InputValueType;
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_OutputValueType;
      typedef typename iterator_traits<_RandomAccessIterator>::difference_type
	_DistanceType;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_Mutable_RandomAccessIteratorConcept<
				  _RandomAccessIterator>)
      __glibcxx_function_requires(_ConvertibleConcept<_InputValueType,
				  _OutputValueType>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _InputValueType, _OutputValueType>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _OutputValueType, _OutputValueType>)
      __glibcxx_requires_valid_range(__first, __last);
      __glibcxx_requires_valid_range(__result_first, __result_last);

      if (__result_first == __result_last)
	return __result_last;
      _RandomAccessIterator __result_real_last = __result_first;
      while(__first != __last && __result_real_last != __result_last)
	{
	  *__result_real_last = *__first;
	  ++__result_real_last;
	  ++__first;
	}
      std::make_heap(__result_first, __result_real_last, __comp);
      while (__first != __last)
	{
	  if (__comp(*__first, *__result_first))
	    std::__adjust_heap(__result_first, _DistanceType(0),
			       _DistanceType(__result_real_last
					     - __result_first),
			       _InputValueType(*__first),
			       __comp);
	  ++__first;
	}
      std::sort_heap(__result_first, __result_real_last, __comp);
      return __result_real_last;
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIterator, typename _Size>
    void
    __introsort_loop(_RandomAccessIterator __first,
		     _RandomAccessIterator __last,
		     _Size __depth_limit)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      while (__last - __first > int(_S_threshold))
	{
	  if (__depth_limit == 0)
	    {
	      std::partial_sort(__first, __last, __last);
	      return;
	    }
	  --__depth_limit;
	  _RandomAccessIterator __cut =
	    std::__unguarded_partition(__first, __last,
				       _ValueType(std::__median(*__first,
								*(__first
								  + (__last
								     - __first)
								  / 2),
								*(__last
								  - 1))));
	  std::__introsort_loop(__cut, __last, __depth_limit);
	  __last = __cut;
	}
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIterator, typename _Size, typename _Compare>
    void
    __introsort_loop(_RandomAccessIterator __first,
		     _RandomAccessIterator __last,
		     _Size __depth_limit, _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      while (__last - __first > int(_S_threshold))
	{
	  if (__depth_limit == 0)
	    {
	      std::partial_sort(__first, __last, __last, __comp);
	      return;
	    }
	  --__depth_limit;
	  _RandomAccessIterator __cut =
	    std::__unguarded_partition(__first, __last,
				       _ValueType(std::__median(*__first,
								*(__first
								  + (__last
								     - __first)
								  / 2),
								*(__last - 1),
								__comp)),
				       __comp);
	  std::__introsort_loop(__cut, __last, __depth_limit, __comp);
	  __last = __cut;
	}
    }

  /**
   *  @brief Sort the elements of a sequence.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @return  Nothing.
   *
   *  Sorts the elements in the range @p [first,last) in ascending order,
   *  such that @p *(i+1)<*i is false for each iterator @p i in the range
   *  @p [first,last-1).
   *
   *  The relative ordering of equivalent elements is not preserved, use
   *  @p stable_sort() if this is needed.
  */
  template<typename _RandomAccessIterator>
    inline void
    sort(_RandomAccessIterator __first, _RandomAccessIterator __last)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      // concept requirements
      __glibcxx_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIterator>)
      __glibcxx_function_requires(_LessThanComparableConcept<_ValueType>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first != __last)
	{
	  std::__introsort_loop(__first, __last,
				std::__lg(__last - __first) * 2);
	  std::__final_insertion_sort(__first, __last);
	}
    }

  /**
   *  @brief Sort the elements of a sequence using a predicate for comparison.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  comp    A comparison functor.
   *  @return  Nothing.
   *
   *  Sorts the elements in the range @p [first,last) in ascending order,
   *  such that @p comp(*(i+1),*i) is false for every iterator @p i in the
   *  range @p [first,last-1).
   *
   *  The relative ordering of equivalent elements is not preserved, use
   *  @p stable_sort() if this is needed.
  */
  template<typename _RandomAccessIterator, typename _Compare>
    inline void
    sort(_RandomAccessIterator __first, _RandomAccessIterator __last,
	 _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      // concept requirements
      __glibcxx_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare, _ValueType,
				  _ValueType>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first != __last)
	{
	  std::__introsort_loop(__first, __last,
				std::__lg(__last - __first) * 2, __comp);
	  std::__final_insertion_sort(__first, __last, __comp);
	}
    }

  /**
   *  @brief Finds the first position in which @a val could be inserted
   *         without changing the ordering.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @return  An iterator pointing to the first element "not less than" @a val,
   *           or end() if every element is less than @a val.
   *  @ingroup binarysearch
  */
  template<typename _ForwardIterator, typename _Tp>
    _ForwardIterator
    lower_bound(_ForwardIterator __first, _ForwardIterator __last,
		const _Tp& __val)
    {
      typedef typename iterator_traits<_ForwardIterator>::value_type
	_ValueType;
      typedef typename iterator_traits<_ForwardIterator>::difference_type
	_DistanceType;

      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_LessThanOpConcept<_ValueType, _Tp>)
      __glibcxx_requires_partitioned(__first, __last, __val);

      _DistanceType __len = std::distance(__first, __last);
      _DistanceType __half;
      _ForwardIterator __middle;

      while (__len > 0)
	{
	  __half = __len >> 1;
	  __middle = __first;
	  std::advance(__middle, __half);
	  if (*__middle < __val)
	    {
	      __first = __middle;
	      ++__first;
	      __len = __len - __half - 1;
	    }
	  else
	    __len = __half;
	}
      return __first;
    }

  /**
   *  @brief Finds the first position in which @a val could be inserted
   *         without changing the ordering.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @param  comp    A functor to use for comparisons.
   *  @return  An iterator pointing to the first element "not less than" @a val,
   *           or end() if every element is less than @a val.
   *  @ingroup binarysearch
   *
   *  The comparison function should have the same effects on ordering as
   *  the function used for the initial sort.
  */
  template<typename _ForwardIterator, typename _Tp, typename _Compare>
    _ForwardIterator
    lower_bound(_ForwardIterator __first, _ForwardIterator __last,
		const _Tp& __val, _Compare __comp)
    {
      typedef typename iterator_traits<_ForwardIterator>::value_type
	_ValueType;
      typedef typename iterator_traits<_ForwardIterator>::difference_type
	_DistanceType;

      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType, _Tp>)
      __glibcxx_requires_partitioned_pred(__first, __last, __val, __comp);

      _DistanceType __len = std::distance(__first, __last);
      _DistanceType __half;
      _ForwardIterator __middle;

      while (__len > 0)
	{
	  __half = __len >> 1;
	  __middle = __first;
	  std::advance(__middle, __half);
	  if (__comp(*__middle, __val))
	    {
	      __first = __middle;
	      ++__first;
	      __len = __len - __half - 1;
	    }
	  else
	    __len = __half;
	}
      return __first;
    }

  /**
   *  @brief Finds the last position in which @a val could be inserted
   *         without changing the ordering.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @return  An iterator pointing to the first element greater than @a val,
   *           or end() if no elements are greater than @a val.
   *  @ingroup binarysearch
  */
  template<typename _ForwardIterator, typename _Tp>
    _ForwardIterator
    upper_bound(_ForwardIterator __first, _ForwardIterator __last,
		const _Tp& __val)
    {
      typedef typename iterator_traits<_ForwardIterator>::value_type
	_ValueType;
      typedef typename iterator_traits<_ForwardIterator>::difference_type
	_DistanceType;

      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_LessThanOpConcept<_Tp, _ValueType>)
      __glibcxx_requires_partitioned(__first, __last, __val);

      _DistanceType __len = std::distance(__first, __last);
      _DistanceType __half;
      _ForwardIterator __middle;

      while (__len > 0)
	{
	  __half = __len >> 1;
	  __middle = __first;
	  std::advance(__middle, __half);
	  if (__val < *__middle)
	    __len = __half;
	  else
	    {
	      __first = __middle;
	      ++__first;
	      __len = __len - __half - 1;
	    }
	}
      return __first;
    }

  /**
   *  @brief Finds the last position in which @a val could be inserted
   *         without changing the ordering.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @param  comp    A functor to use for comparisons.
   *  @return  An iterator pointing to the first element greater than @a val,
   *           or end() if no elements are greater than @a val.
   *  @ingroup binarysearch
   *
   *  The comparison function should have the same effects on ordering as
   *  the function used for the initial sort.
  */
  template<typename _ForwardIterator, typename _Tp, typename _Compare>
    _ForwardIterator
    upper_bound(_ForwardIterator __first, _ForwardIterator __last,
		const _Tp& __val, _Compare __comp)
    {
      typedef typename iterator_traits<_ForwardIterator>::value_type
	_ValueType;
      typedef typename iterator_traits<_ForwardIterator>::difference_type
	_DistanceType;

      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _Tp, _ValueType>)
      __glibcxx_requires_partitioned_pred(__first, __last, __val, __comp);

      _DistanceType __len = std::distance(__first, __last);
      _DistanceType __half;
      _ForwardIterator __middle;

      while (__len > 0)
	{
	  __half = __len >> 1;
	  __middle = __first;
	  std::advance(__middle, __half);
	  if (__comp(__val, *__middle))
	    __len = __half;
	  else
	    {
	      __first = __middle;
	      ++__first;
	      __len = __len - __half - 1;
	    }
	}
      return __first;
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIterator, typename _Distance>
    void
    __merge_without_buffer(_BidirectionalIterator __first,
			   _BidirectionalIterator __middle,
			   _BidirectionalIterator __last,
			   _Distance __len1, _Distance __len2)
    {
      if (__len1 == 0 || __len2 == 0)
	return;
      if (__len1 + __len2 == 2)
	{
	  if (*__middle < *__first)
	    std::iter_swap(__first, __middle);
	  return;
	}
      _BidirectionalIterator __first_cut = __first;
      _BidirectionalIterator __second_cut = __middle;
      _Distance __len11 = 0;
      _Distance __len22 = 0;
      if (__len1 > __len2)
	{
	  __len11 = __len1 / 2;
	  std::advance(__first_cut, __len11);
	  __second_cut = std::lower_bound(__middle, __last, *__first_cut);
	  __len22 = std::distance(__middle, __second_cut);
	}
      else
	{
	  __len22 = __len2 / 2;
	  std::advance(__second_cut, __len22);
	  __first_cut = std::upper_bound(__first, __middle, *__second_cut);
	  __len11 = std::distance(__first, __first_cut);
	}
      std::rotate(__first_cut, __middle, __second_cut);
      _BidirectionalIterator __new_middle = __first_cut;
      std::advance(__new_middle, std::distance(__middle, __second_cut));
      std::__merge_without_buffer(__first, __first_cut, __new_middle,
				  __len11, __len22);
      std::__merge_without_buffer(__new_middle, __second_cut, __last,
				  __len1 - __len11, __len2 - __len22);
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIterator, typename _Distance,
	   typename _Compare>
    void
    __merge_without_buffer(_BidirectionalIterator __first,
                           _BidirectionalIterator __middle,
			   _BidirectionalIterator __last,
			   _Distance __len1, _Distance __len2,
			   _Compare __comp)
    {
      if (__len1 == 0 || __len2 == 0)
	return;
      if (__len1 + __len2 == 2)
	{
	  if (__comp(*__middle, *__first))
	    std::iter_swap(__first, __middle);
	  return;
	}
      _BidirectionalIterator __first_cut = __first;
      _BidirectionalIterator __second_cut = __middle;
      _Distance __len11 = 0;
      _Distance __len22 = 0;
      if (__len1 > __len2)
	{
	  __len11 = __len1 / 2;
	  std::advance(__first_cut, __len11);
	  __second_cut = std::lower_bound(__middle, __last, *__first_cut,
					  __comp);
	  __len22 = std::distance(__middle, __second_cut);
	}
      else
	{
	  __len22 = __len2 / 2;
	  std::advance(__second_cut, __len22);
	  __first_cut = std::upper_bound(__first, __middle, *__second_cut,
					 __comp);
	  __len11 = std::distance(__first, __first_cut);
	}
      std::rotate(__first_cut, __middle, __second_cut);
      _BidirectionalIterator __new_middle = __first_cut;
      std::advance(__new_middle, std::distance(__middle, __second_cut));
      std::__merge_without_buffer(__first, __first_cut, __new_middle,
				  __len11, __len22, __comp);
      std::__merge_without_buffer(__new_middle, __second_cut, __last,
				  __len1 - __len11, __len2 - __len22, __comp);
    }

  /**
   *  @if maint
   *  This is a helper function for the stable sorting routines.
   *  @endif
  */
  template<typename _RandomAccessIterator>
    void
    __inplace_stable_sort(_RandomAccessIterator __first,
			  _RandomAccessIterator __last)
    {
      if (__last - __first < 15)
	{
	  std::__insertion_sort(__first, __last);
	  return;
	}
      _RandomAccessIterator __middle = __first + (__last - __first) / 2;
      std::__inplace_stable_sort(__first, __middle);
      std::__inplace_stable_sort(__middle, __last);
      std::__merge_without_buffer(__first, __middle, __last,
				  __middle - __first,
				  __last - __middle);
    }

  /**
   *  @if maint
   *  This is a helper function for the stable sorting routines.
   *  @endif
  */
  template<typename _RandomAccessIterator, typename _Compare>
    void
    __inplace_stable_sort(_RandomAccessIterator __first,
			  _RandomAccessIterator __last, _Compare __comp)
    {
      if (__last - __first < 15)
	{
	  std::__insertion_sort(__first, __last, __comp);
	  return;
	}
      _RandomAccessIterator __middle = __first + (__last - __first) / 2;
      std::__inplace_stable_sort(__first, __middle, __comp);
      std::__inplace_stable_sort(__middle, __last, __comp);
      std::__merge_without_buffer(__first, __middle, __last,
				  __middle - __first,
				  __last - __middle,
				  __comp);
    }

  /**
   *  @brief Merges two sorted ranges.
   *  @param  first1  An iterator.
   *  @param  first2  Another iterator.
   *  @param  last1   Another iterator.
   *  @param  last2   Another iterator.
   *  @param  result  An iterator pointing to the end of the merged range.
   *  @return  An iterator pointing to the first element "not less than" @a val.
   *
   *  Merges the ranges [first1,last1) and [first2,last2) into the sorted range
   *  [result, result + (last1-first1) + (last2-first2)).  Both input ranges
   *  must be sorted, and the output range must not overlap with either of
   *  the input ranges.  The sort is @e stable, that is, for equivalent
   *  elements in the two ranges, elements from the first range will always
   *  come before elements from the second.
  */
  template<typename _InputIterator1, typename _InputIterator2,
	   typename _OutputIterator>
    _OutputIterator
    merge(_InputIterator1 __first1, _InputIterator1 __last1,
	  _InputIterator2 __first2, _InputIterator2 __last2,
	  _OutputIterator __result)
    {
      typedef typename iterator_traits<_InputIterator1>::value_type
	_ValueType1;
      typedef typename iterator_traits<_InputIterator2>::value_type
	_ValueType2;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType1>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType2>)
      __glibcxx_function_requires(_LessThanOpConcept<_ValueType2, _ValueType1>)	
      __glibcxx_requires_sorted(__first1, __last1);
      __glibcxx_requires_sorted(__first2, __last2);

      while (__first1 != __last1 && __first2 != __last2)
	{
	  if (*__first2 < *__first1)
	    {
	      *__result = *__first2;
	      ++__first2;
	    }
	  else
	    {
	      *__result = *__first1;
	      ++__first1;
	    }
	  ++__result;
	}
      return std::copy(__first2, __last2, std::copy(__first1, __last1,
						    __result));
    }

  /**
   *  @brief Merges two sorted ranges.
   *  @param  first1  An iterator.
   *  @param  first2  Another iterator.
   *  @param  last1   Another iterator.
   *  @param  last2   Another iterator.
   *  @param  result  An iterator pointing to the end of the merged range.
   *  @param  comp    A functor to use for comparisons.
   *  @return  An iterator pointing to the first element "not less than" @a val.
   *
   *  Merges the ranges [first1,last1) and [first2,last2) into the sorted range
   *  [result, result + (last1-first1) + (last2-first2)).  Both input ranges
   *  must be sorted, and the output range must not overlap with either of
   *  the input ranges.  The sort is @e stable, that is, for equivalent
   *  elements in the two ranges, elements from the first range will always
   *  come before elements from the second.
   *
   *  The comparison function should have the same effects on ordering as
   *  the function used for the initial sort.
  */
  template<typename _InputIterator1, typename _InputIterator2,
	   typename _OutputIterator, typename _Compare>
    _OutputIterator
    merge(_InputIterator1 __first1, _InputIterator1 __last1,
	  _InputIterator2 __first2, _InputIterator2 __last2,
	  _OutputIterator __result, _Compare __comp)
    {
      typedef typename iterator_traits<_InputIterator1>::value_type
	_ValueType1;
      typedef typename iterator_traits<_InputIterator2>::value_type
	_ValueType2;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType1>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType2>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType2, _ValueType1>)
      __glibcxx_requires_sorted_pred(__first1, __last1, __comp);
      __glibcxx_requires_sorted_pred(__first2, __last2, __comp);

      while (__first1 != __last1 && __first2 != __last2)
	{
	  if (__comp(*__first2, *__first1))
	    {
	      *__result = *__first2;
	      ++__first2;
	    }
	  else
	    {
	      *__result = *__first1;
	      ++__first1;
	    }
	  ++__result;
	}
      return std::copy(__first2, __last2, std::copy(__first1, __last1,
						    __result));
    }

  template<typename _RandomAccessIterator1, typename _RandomAccessIterator2,
	   typename _Distance>
    void
    __merge_sort_loop(_RandomAccessIterator1 __first,
		      _RandomAccessIterator1 __last,
		      _RandomAccessIterator2 __result,
		      _Distance __step_size)
    {
      const _Distance __two_step = 2 * __step_size;

      while (__last - __first >= __two_step)
	{
	  __result = std::merge(__first, __first + __step_size,
				__first + __step_size, __first + __two_step,
				__result);
	  __first += __two_step;
	}

      __step_size = std::min(_Distance(__last - __first), __step_size);
      std::merge(__first, __first + __step_size, __first + __step_size, __last,
		 __result);
    }

  template<typename _RandomAccessIterator1, typename _RandomAccessIterator2,
	   typename _Distance, typename _Compare>
    void
    __merge_sort_loop(_RandomAccessIterator1 __first,
		      _RandomAccessIterator1 __last,
		      _RandomAccessIterator2 __result, _Distance __step_size,
		      _Compare __comp)
    {
      const _Distance __two_step = 2 * __step_size;

      while (__last - __first >= __two_step)
	{
	  __result = std::merge(__first, __first + __step_size,
				__first + __step_size, __first + __two_step,
				__result,
				__comp);
	  __first += __two_step;
	}
      __step_size = std::min(_Distance(__last - __first), __step_size);

      std::merge(__first, __first + __step_size,
		 __first + __step_size, __last,
		 __result,
		 __comp);
    }

  enum { _S_chunk_size = 7 };

  template<typename _RandomAccessIterator, typename _Distance>
    void
    __chunk_insertion_sort(_RandomAccessIterator __first,
			   _RandomAccessIterator __last,
			   _Distance __chunk_size)
    {
      while (__last - __first >= __chunk_size)
	{
	  std::__insertion_sort(__first, __first + __chunk_size);
	  __first += __chunk_size;
	}
      std::__insertion_sort(__first, __last);
    }

  template<typename _RandomAccessIterator, typename _Distance, typename _Compare>
    void
    __chunk_insertion_sort(_RandomAccessIterator __first,
			   _RandomAccessIterator __last,
			   _Distance __chunk_size, _Compare __comp)
    {
      while (__last - __first >= __chunk_size)
	{
	  std::__insertion_sort(__first, __first + __chunk_size, __comp);
	  __first += __chunk_size;
	}
      std::__insertion_sort(__first, __last, __comp);
    }

  template<typename _RandomAccessIterator, typename _Pointer>
    void
    __merge_sort_with_buffer(_RandomAccessIterator __first,
			     _RandomAccessIterator __last,
                             _Pointer __buffer)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::difference_type
	_Distance;

      const _Distance __len = __last - __first;
      const _Pointer __buffer_last = __buffer + __len;

      _Distance __step_size = _S_chunk_size;
      std::__chunk_insertion_sort(__first, __last, __step_size);

      while (__step_size < __len)
	{
	  std::__merge_sort_loop(__first, __last, __buffer, __step_size);
	  __step_size *= 2;
	  std::__merge_sort_loop(__buffer, __buffer_last, __first, __step_size);
	  __step_size *= 2;
	}
    }

  template<typename _RandomAccessIterator, typename _Pointer, typename _Compare>
    void
    __merge_sort_with_buffer(_RandomAccessIterator __first,
			     _RandomAccessIterator __last,
                             _Pointer __buffer, _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::difference_type
	_Distance;

      const _Distance __len = __last - __first;
      const _Pointer __buffer_last = __buffer + __len;

      _Distance __step_size = _S_chunk_size;
      std::__chunk_insertion_sort(__first, __last, __step_size, __comp);

      while (__step_size < __len)
	{
	  std::__merge_sort_loop(__first, __last, __buffer,
				 __step_size, __comp);
	  __step_size *= 2;
	  std::__merge_sort_loop(__buffer, __buffer_last, __first,
				 __step_size, __comp);
	  __step_size *= 2;
	}
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIterator1, typename _BidirectionalIterator2,
	   typename _BidirectionalIterator3>
    _BidirectionalIterator3
    __merge_backward(_BidirectionalIterator1 __first1,
		     _BidirectionalIterator1 __last1,
		     _BidirectionalIterator2 __first2,
		     _BidirectionalIterator2 __last2,
		     _BidirectionalIterator3 __result)
    {
      if (__first1 == __last1)
	return std::copy_backward(__first2, __last2, __result);
      if (__first2 == __last2)
	return std::copy_backward(__first1, __last1, __result);
      --__last1;
      --__last2;
      while (true)
	{
	  if (*__last2 < *__last1)
	    {
	      *--__result = *__last1;
	      if (__first1 == __last1)
		return std::copy_backward(__first2, ++__last2, __result);
	      --__last1;
	    }
	  else
	    {
	      *--__result = *__last2;
	      if (__first2 == __last2)
		return std::copy_backward(__first1, ++__last1, __result);
	      --__last2;
	    }
	}
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIterator1, typename _BidirectionalIterator2,
	   typename _BidirectionalIterator3, typename _Compare>
    _BidirectionalIterator3
    __merge_backward(_BidirectionalIterator1 __first1,
		     _BidirectionalIterator1 __last1,
		     _BidirectionalIterator2 __first2,
		     _BidirectionalIterator2 __last2,
		     _BidirectionalIterator3 __result,
		     _Compare __comp)
    {
      if (__first1 == __last1)
	return std::copy_backward(__first2, __last2, __result);
      if (__first2 == __last2)
	return std::copy_backward(__first1, __last1, __result);
      --__last1;
      --__last2;
      while (true)
	{
	  if (__comp(*__last2, *__last1))
	    {
	      *--__result = *__last1;
	      if (__first1 == __last1)
		return std::copy_backward(__first2, ++__last2, __result);
	      --__last1;
	    }
	  else
	    {
	      *--__result = *__last2;
	      if (__first2 == __last2)
		return std::copy_backward(__first1, ++__last1, __result);
	      --__last2;
	    }
	}
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIterator1, typename _BidirectionalIterator2,
	   typename _Distance>
    _BidirectionalIterator1
    __rotate_adaptive(_BidirectionalIterator1 __first,
		      _BidirectionalIterator1 __middle,
		      _BidirectionalIterator1 __last,
		      _Distance __len1, _Distance __len2,
		      _BidirectionalIterator2 __buffer,
		      _Distance __buffer_size)
    {
      _BidirectionalIterator2 __buffer_end;
      if (__len1 > __len2 && __len2 <= __buffer_size)
	{
	  __buffer_end = std::copy(__middle, __last, __buffer);
	  std::copy_backward(__first, __middle, __last);
	  return std::copy(__buffer, __buffer_end, __first);
	}
      else if (__len1 <= __buffer_size)
	{
	  __buffer_end = std::copy(__first, __middle, __buffer);
	  std::copy(__middle, __last, __first);
	  return std::copy_backward(__buffer, __buffer_end, __last);
	}
      else
	{
	  std::rotate(__first, __middle, __last);
	  std::advance(__first, std::distance(__middle, __last));
	  return __first;
	}
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIterator, typename _Distance,
	   typename _Pointer>
    void
    __merge_adaptive(_BidirectionalIterator __first,
                     _BidirectionalIterator __middle,
		     _BidirectionalIterator __last,
		     _Distance __len1, _Distance __len2,
		     _Pointer __buffer, _Distance __buffer_size)
    {
      if (__len1 <= __len2 && __len1 <= __buffer_size)
	{
	  _Pointer __buffer_end = std::copy(__first, __middle, __buffer);
	  std::merge(__buffer, __buffer_end, __middle, __last, __first);
	}
      else if (__len2 <= __buffer_size)
	{
	  _Pointer __buffer_end = std::copy(__middle, __last, __buffer);
	  std::__merge_backward(__first, __middle, __buffer,
				__buffer_end, __last);
	}
      else
	{
	  _BidirectionalIterator __first_cut = __first;
	  _BidirectionalIterator __second_cut = __middle;
	  _Distance __len11 = 0;
	  _Distance __len22 = 0;
	  if (__len1 > __len2)
	    {
	      __len11 = __len1 / 2;
	      std::advance(__first_cut, __len11);
	      __second_cut = std::lower_bound(__middle, __last,
					      *__first_cut);
	      __len22 = std::distance(__middle, __second_cut);
	    }
	  else
	    {
	      __len22 = __len2 / 2;
	      std::advance(__second_cut, __len22);
	      __first_cut = std::upper_bound(__first, __middle,
					     *__second_cut);
	      __len11 = std::distance(__first, __first_cut);
	    }
	  _BidirectionalIterator __new_middle =
	    std::__rotate_adaptive(__first_cut, __middle, __second_cut,
				   __len1 - __len11, __len22, __buffer,
				   __buffer_size);
	  std::__merge_adaptive(__first, __first_cut, __new_middle, __len11,
				__len22, __buffer, __buffer_size);
	  std::__merge_adaptive(__new_middle, __second_cut, __last,
				__len1 - __len11,
				__len2 - __len22, __buffer, __buffer_size);
	}
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIterator, typename _Distance, typename _Pointer,
	   typename _Compare>
    void
    __merge_adaptive(_BidirectionalIterator __first,
                     _BidirectionalIterator __middle,
		     _BidirectionalIterator __last,
		     _Distance __len1, _Distance __len2,
		     _Pointer __buffer, _Distance __buffer_size,
		     _Compare __comp)
    {
      if (__len1 <= __len2 && __len1 <= __buffer_size)
	{
	  _Pointer __buffer_end = std::copy(__first, __middle, __buffer);
	  std::merge(__buffer, __buffer_end, __middle, __last, __first, __comp);
	}
      else if (__len2 <= __buffer_size)
	{
	  _Pointer __buffer_end = std::copy(__middle, __last, __buffer);
	  std::__merge_backward(__first, __middle, __buffer, __buffer_end,
				__last, __comp);
	}
      else
	{
	  _BidirectionalIterator __first_cut = __first;
	  _BidirectionalIterator __second_cut = __middle;
	  _Distance __len11 = 0;
	  _Distance __len22 = 0;
	  if (__len1 > __len2)
	    {
	      __len11 = __len1 / 2;
	      std::advance(__first_cut, __len11);
	      __second_cut = std::lower_bound(__middle, __last, *__first_cut,
					      __comp);
	      __len22 = std::distance(__middle, __second_cut);
	    }
	  else
	    {
	      __len22 = __len2 / 2;
	      std::advance(__second_cut, __len22);
	      __first_cut = std::upper_bound(__first, __middle, *__second_cut,
					     __comp);
	      __len11 = std::distance(__first, __first_cut);
	    }
	  _BidirectionalIterator __new_middle =
	    std::__rotate_adaptive(__first_cut, __middle, __second_cut,
				   __len1 - __len11, __len22, __buffer,
				   __buffer_size);
	  std::__merge_adaptive(__first, __first_cut, __new_middle, __len11,
				__len22, __buffer, __buffer_size, __comp);
	  std::__merge_adaptive(__new_middle, __second_cut, __last,
				__len1 - __len11,
				__len2 - __len22, __buffer,
				__buffer_size, __comp);
	}
    }

  /**
   *  @brief Merges two sorted ranges in place.
   *  @param  first   An iterator.
   *  @param  middle  Another iterator.
   *  @param  last    Another iterator.
   *  @return  Nothing.
   *
   *  Merges two sorted and consecutive ranges, [first,middle) and
   *  [middle,last), and puts the result in [first,last).  The output will
   *  be sorted.  The sort is @e stable, that is, for equivalent
   *  elements in the two ranges, elements from the first range will always
   *  come before elements from the second.
   *
   *  If enough additional memory is available, this takes (last-first)-1
   *  comparisons.  Otherwise an NlogN algorithm is used, where N is
   *  distance(first,last).
  */
  template<typename _BidirectionalIterator>
    void
    inplace_merge(_BidirectionalIterator __first,
		  _BidirectionalIterator __middle,
		  _BidirectionalIterator __last)
    {
      typedef typename iterator_traits<_BidirectionalIterator>::value_type
          _ValueType;
      typedef typename iterator_traits<_BidirectionalIterator>::difference_type
          _DistanceType;

      // concept requirements
      __glibcxx_function_requires(_Mutable_BidirectionalIteratorConcept<
	    _BidirectionalIterator>)
      __glibcxx_function_requires(_LessThanComparableConcept<_ValueType>)
      __glibcxx_requires_sorted(__first, __middle);
      __glibcxx_requires_sorted(__middle, __last);

      if (__first == __middle || __middle == __last)
	return;

      _DistanceType __len1 = std::distance(__first, __middle);
      _DistanceType __len2 = std::distance(__middle, __last);

      _Temporary_buffer<_BidirectionalIterator, _ValueType> __buf(__first,
								  __last);
      if (__buf.begin() == 0)
	std::__merge_without_buffer(__first, __middle, __last, __len1, __len2);
      else
	std::__merge_adaptive(__first, __middle, __last, __len1, __len2,
			      __buf.begin(), _DistanceType(__buf.size()));
    }

  /**
   *  @brief Merges two sorted ranges in place.
   *  @param  first   An iterator.
   *  @param  middle  Another iterator.
   *  @param  last    Another iterator.
   *  @param  comp    A functor to use for comparisons.
   *  @return  Nothing.
   *
   *  Merges two sorted and consecutive ranges, [first,middle) and
   *  [middle,last), and puts the result in [first,last).  The output will
   *  be sorted.  The sort is @e stable, that is, for equivalent
   *  elements in the two ranges, elements from the first range will always
   *  come before elements from the second.
   *
   *  If enough additional memory is available, this takes (last-first)-1
   *  comparisons.  Otherwise an NlogN algorithm is used, where N is
   *  distance(first,last).
   *
   *  The comparison function should have the same effects on ordering as
   *  the function used for the initial sort.
  */
  template<typename _BidirectionalIterator, typename _Compare>
    void
    inplace_merge(_BidirectionalIterator __first,
		  _BidirectionalIterator __middle,
		  _BidirectionalIterator __last,
		  _Compare __comp)
    {
      typedef typename iterator_traits<_BidirectionalIterator>::value_type
          _ValueType;
      typedef typename iterator_traits<_BidirectionalIterator>::difference_type
          _DistanceType;

      // concept requirements
      __glibcxx_function_requires(_Mutable_BidirectionalIteratorConcept<
	    _BidirectionalIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
	    _ValueType, _ValueType>)
      __glibcxx_requires_sorted_pred(__first, __middle, __comp);
      __glibcxx_requires_sorted_pred(__middle, __last, __comp);

      if (__first == __middle || __middle == __last)
	return;

      const _DistanceType __len1 = std::distance(__first, __middle);
      const _DistanceType __len2 = std::distance(__middle, __last);

      _Temporary_buffer<_BidirectionalIterator, _ValueType> __buf(__first,
								  __last);
      if (__buf.begin() == 0)
	std::__merge_without_buffer(__first, __middle, __last, __len1,
				    __len2, __comp);
      else
	std::__merge_adaptive(__first, __middle, __last, __len1, __len2,
			      __buf.begin(), _DistanceType(__buf.size()),
			      __comp);
    }

  template<typename _RandomAccessIterator, typename _Pointer,
	   typename _Distance>
    void
    __stable_sort_adaptive(_RandomAccessIterator __first,
			   _RandomAccessIterator __last,
                           _Pointer __buffer, _Distance __buffer_size)
    {
      const _Distance __len = (__last - __first + 1) / 2;
      const _RandomAccessIterator __middle = __first + __len;
      if (__len > __buffer_size)
	{
	  std::__stable_sort_adaptive(__first, __middle,
				      __buffer, __buffer_size);
	  std::__stable_sort_adaptive(__middle, __last,
				      __buffer, __buffer_size);
	}
      else
	{
	  std::__merge_sort_with_buffer(__first, __middle, __buffer);
	  std::__merge_sort_with_buffer(__middle, __last, __buffer);
	}
      std::__merge_adaptive(__first, __middle, __last,
			    _Distance(__middle - __first),
			    _Distance(__last - __middle),
			    __buffer, __buffer_size);
    }

  template<typename _RandomAccessIterator, typename _Pointer,
	   typename _Distance, typename _Compare>
    void
    __stable_sort_adaptive(_RandomAccessIterator __first,
			   _RandomAccessIterator __last,
                           _Pointer __buffer, _Distance __buffer_size,
                           _Compare __comp)
    {
      const _Distance __len = (__last - __first + 1) / 2;
      const _RandomAccessIterator __middle = __first + __len;
      if (__len > __buffer_size)
	{
	  std::__stable_sort_adaptive(__first, __middle, __buffer,
				      __buffer_size, __comp);
	  std::__stable_sort_adaptive(__middle, __last, __buffer,
				      __buffer_size, __comp);
	}
      else
	{
	  std::__merge_sort_with_buffer(__first, __middle, __buffer, __comp);
	  std::__merge_sort_with_buffer(__middle, __last, __buffer, __comp);
	}
      std::__merge_adaptive(__first, __middle, __last,
			    _Distance(__middle - __first),
			    _Distance(__last - __middle),
			    __buffer, __buffer_size,
			    __comp);
    }

  /**
   *  @brief Sort the elements of a sequence, preserving the relative order
   *         of equivalent elements.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @return  Nothing.
   *
   *  Sorts the elements in the range @p [first,last) in ascending order,
   *  such that @p *(i+1)<*i is false for each iterator @p i in the range
   *  @p [first,last-1).
   *
   *  The relative ordering of equivalent elements is preserved, so any two
   *  elements @p x and @p y in the range @p [first,last) such that
   *  @p x<y is false and @p y<x is false will have the same relative
   *  ordering after calling @p stable_sort().
  */
  template<typename _RandomAccessIterator>
    inline void
    stable_sort(_RandomAccessIterator __first, _RandomAccessIterator __last)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;
      typedef typename iterator_traits<_RandomAccessIterator>::difference_type
	_DistanceType;

      // concept requirements
      __glibcxx_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIterator>)
      __glibcxx_function_requires(_LessThanComparableConcept<_ValueType>)
      __glibcxx_requires_valid_range(__first, __last);

      _Temporary_buffer<_RandomAccessIterator, _ValueType> __buf(__first,
								 __last);
      if (__buf.begin() == 0)
	std::__inplace_stable_sort(__first, __last);
      else
	std::__stable_sort_adaptive(__first, __last, __buf.begin(),
				    _DistanceType(__buf.size()));
    }

  /**
   *  @brief Sort the elements of a sequence using a predicate for comparison,
   *         preserving the relative order of equivalent elements.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  comp    A comparison functor.
   *  @return  Nothing.
   *
   *  Sorts the elements in the range @p [first,last) in ascending order,
   *  such that @p comp(*(i+1),*i) is false for each iterator @p i in the
   *  range @p [first,last-1).
   *
   *  The relative ordering of equivalent elements is preserved, so any two
   *  elements @p x and @p y in the range @p [first,last) such that
   *  @p comp(x,y) is false and @p comp(y,x) is false will have the same
   *  relative ordering after calling @p stable_sort().
  */
  template<typename _RandomAccessIterator, typename _Compare>
    inline void
    stable_sort(_RandomAccessIterator __first, _RandomAccessIterator __last,
		_Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;
      typedef typename iterator_traits<_RandomAccessIterator>::difference_type
	_DistanceType;

      // concept requirements
      __glibcxx_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType,
				  _ValueType>)
      __glibcxx_requires_valid_range(__first, __last);

      _Temporary_buffer<_RandomAccessIterator, _ValueType> __buf(__first,
								 __last);
      if (__buf.begin() == 0)
	std::__inplace_stable_sort(__first, __last, __comp);
      else
	std::__stable_sort_adaptive(__first, __last, __buf.begin(),
				    _DistanceType(__buf.size()), __comp);
    }


  template<typename _RandomAccessIterator, typename _Size>
    void
    __introselect(_RandomAccessIterator __first, _RandomAccessIterator __nth,
		  _RandomAccessIterator __last, _Size __depth_limit)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      while (__last - __first > 3)
	{
	  if (__depth_limit == 0)
	    {
	      std::__heap_select(__first, __nth + 1, __last);
	      // Place the nth largest element in its final position.
	      std::iter_swap(__first, __nth);
	      return;
	    }
	  --__depth_limit;
	  _RandomAccessIterator __cut =
	    std::__unguarded_partition(__first, __last,
				       _ValueType(std::__median(*__first,
								*(__first
								  + (__last
								     - __first)
								  / 2),
								*(__last
								  - 1))));
	  if (__cut <= __nth)
	    __first = __cut;
	  else
	    __last = __cut;
	}
      std::__insertion_sort(__first, __last);
    }

  template<typename _RandomAccessIterator, typename _Size, typename _Compare>
    void
    __introselect(_RandomAccessIterator __first, _RandomAccessIterator __nth,
		  _RandomAccessIterator __last, _Size __depth_limit,
		  _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      while (__last - __first > 3)
	{
	  if (__depth_limit == 0)
	    {
	      std::__heap_select(__first, __nth + 1, __last, __comp);
	      // Place the nth largest element in its final position.
	      std::iter_swap(__first, __nth);
	      return;
	    }
	  --__depth_limit;
	  _RandomAccessIterator __cut =
	    std::__unguarded_partition(__first, __last,
				       _ValueType(std::__median(*__first,
								*(__first
								  + (__last
								     - __first)
								  / 2),
								*(__last - 1),
								__comp)),
				       __comp);
	  if (__cut <= __nth)
	    __first = __cut;
	  else
	    __last = __cut;
	}
      std::__insertion_sort(__first, __last, __comp);
    }

  /**
   *  @brief Sort a sequence just enough to find a particular position.
   *  @param  first   An iterator.
   *  @param  nth     Another iterator.
   *  @param  last    Another iterator.
   *  @return  Nothing.
   *
   *  Rearranges the elements in the range @p [first,last) so that @p *nth
   *  is the same element that would have been in that position had the
   *  whole sequence been sorted.
   *  whole sequence been sorted. The elements either side of @p *nth are
   *  not completely sorted, but for any iterator @i in the range
   *  @p [first,nth) and any iterator @j in the range @p [nth,last) it
   *  holds that @p *j<*i is false.
  */
  template<typename _RandomAccessIterator>
    inline void
    nth_element(_RandomAccessIterator __first, _RandomAccessIterator __nth,
		_RandomAccessIterator __last)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      // concept requirements
      __glibcxx_function_requires(_Mutable_RandomAccessIteratorConcept<
				  _RandomAccessIterator>)
      __glibcxx_function_requires(_LessThanComparableConcept<_ValueType>)
      __glibcxx_requires_valid_range(__first, __nth);
      __glibcxx_requires_valid_range(__nth, __last);

      if (__first == __last || __nth == __last)
	return;

      std::__introselect(__first, __nth, __last,
			 std::__lg(__last - __first) * 2);
    }

  /**
   *  @brief Sort a sequence just enough to find a particular position
   *         using a predicate for comparison.
   *  @param  first   An iterator.
   *  @param  nth     Another iterator.
   *  @param  last    Another iterator.
   *  @param  comp    A comparison functor.
   *  @return  Nothing.
   *
   *  Rearranges the elements in the range @p [first,last) so that @p *nth
   *  is the same element that would have been in that position had the
   *  whole sequence been sorted. The elements either side of @p *nth are
   *  not completely sorted, but for any iterator @i in the range
   *  @p [first,nth) and any iterator @j in the range @p [nth,last) it
   *  holds that @p comp(*j,*i) is false.
  */
  template<typename _RandomAccessIterator, typename _Compare>
    inline void
    nth_element(_RandomAccessIterator __first, _RandomAccessIterator __nth,
		_RandomAccessIterator __last, _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIterator>::value_type
	_ValueType;

      // concept requirements
      __glibcxx_function_requires(_Mutable_RandomAccessIteratorConcept<
				  _RandomAccessIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType, _ValueType>)
      __glibcxx_requires_valid_range(__first, __nth);
      __glibcxx_requires_valid_range(__nth, __last);

      if (__first == __last || __nth == __last)
	return;

      std::__introselect(__first, __nth, __last,
			 std::__lg(__last - __first) * 2, __comp);
    }

  /**
   *  @brief Finds the largest subrange in which @a val could be inserted
   *         at any place in it without changing the ordering.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @return  An pair of iterators defining the subrange.
   *  @ingroup binarysearch
   *
   *  This is equivalent to
   *  @code
   *    std::make_pair(lower_bound(first, last, val),
   *                   upper_bound(first, last, val))
   *  @endcode
   *  but does not actually call those functions.
  */
  template<typename _ForwardIterator, typename _Tp>
    pair<_ForwardIterator, _ForwardIterator>
    equal_range(_ForwardIterator __first, _ForwardIterator __last,
		const _Tp& __val)
    {
      typedef typename iterator_traits<_ForwardIterator>::value_type
	_ValueType;
      typedef typename iterator_traits<_ForwardIterator>::difference_type
	_DistanceType;

      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_LessThanOpConcept<_ValueType, _Tp>)
      __glibcxx_function_requires(_LessThanOpConcept<_Tp, _ValueType>)	
      __glibcxx_requires_partitioned(__first, __last, __val);

      _DistanceType __len = std::distance(__first, __last);
      _DistanceType __half;
      _ForwardIterator __middle, __left, __right;

      while (__len > 0)
	{
	  __half = __len >> 1;
	  __middle = __first;
	  std::advance(__middle, __half);
	  if (*__middle < __val)
	    {
	      __first = __middle;
	      ++__first;
	      __len = __len - __half - 1;
	    }
	  else if (__val < *__middle)
	    __len = __half;
	  else
	    {
	      __left = std::lower_bound(__first, __middle, __val);
	      std::advance(__first, __len);
	      __right = std::upper_bound(++__middle, __first, __val);
	      return pair<_ForwardIterator, _ForwardIterator>(__left, __right);
	    }
	}
      return pair<_ForwardIterator, _ForwardIterator>(__first, __first);
    }

  /**
   *  @brief Finds the largest subrange in which @a val could be inserted
   *         at any place in it without changing the ordering.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @param  comp    A functor to use for comparisons.
   *  @return  An pair of iterators defining the subrange.
   *  @ingroup binarysearch
   *
   *  This is equivalent to
   *  @code
   *    std::make_pair(lower_bound(first, last, val, comp),
   *                   upper_bound(first, last, val, comp))
   *  @endcode
   *  but does not actually call those functions.
  */
  template<typename _ForwardIterator, typename _Tp, typename _Compare>
    pair<_ForwardIterator, _ForwardIterator>
    equal_range(_ForwardIterator __first, _ForwardIterator __last,
		const _Tp& __val,
		_Compare __comp)
    {
      typedef typename iterator_traits<_ForwardIterator>::value_type
	_ValueType;
      typedef typename iterator_traits<_ForwardIterator>::difference_type
	_DistanceType;

      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType, _Tp>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _Tp, _ValueType>)
      __glibcxx_requires_partitioned_pred(__first, __last, __val, __comp);

      _DistanceType __len = std::distance(__first, __last);
      _DistanceType __half;
      _ForwardIterator __middle, __left, __right;

      while (__len > 0)
	{
	  __half = __len >> 1;
	  __middle = __first;
	  std::advance(__middle, __half);
	  if (__comp(*__middle, __val))
	    {
	      __first = __middle;
	      ++__first;
	      __len = __len - __half - 1;
	    }
	  else if (__comp(__val, *__middle))
	    __len = __half;
	  else
	    {
	      __left = std::lower_bound(__first, __middle, __val, __comp);
	      std::advance(__first, __len);
	      __right = std::upper_bound(++__middle, __first, __val, __comp);
	      return pair<_ForwardIterator, _ForwardIterator>(__left, __right);
	    }
	}
      return pair<_ForwardIterator, _ForwardIterator>(__first, __first);
    }

  /**
   *  @brief Determines whether an element exists in a range.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @return  True if @a val (or its equivelent) is in [@a first,@a last ].
   *  @ingroup binarysearch
   *
   *  Note that this does not actually return an iterator to @a val.  For
   *  that, use std::find or a container's specialized find member functions.
  */
  template<typename _ForwardIterator, typename _Tp>
    bool
    binary_search(_ForwardIterator __first, _ForwardIterator __last,
                  const _Tp& __val)
    {
      typedef typename iterator_traits<_ForwardIterator>::value_type
	_ValueType;

      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_LessThanOpConcept<_Tp, _ValueType>)
      __glibcxx_requires_partitioned(__first, __last, __val);

      _ForwardIterator __i = std::lower_bound(__first, __last, __val);
      return __i != __last && !(__val < *__i);
    }

  /**
   *  @brief Determines whether an element exists in a range.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @param  comp    A functor to use for comparisons.
   *  @return  True if @a val (or its equivelent) is in [@a first,@a last ].
   *  @ingroup binarysearch
   *
   *  Note that this does not actually return an iterator to @a val.  For
   *  that, use std::find or a container's specialized find member functions.
   *
   *  The comparison function should have the same effects on ordering as
   *  the function used for the initial sort.
  */
  template<typename _ForwardIterator, typename _Tp, typename _Compare>
    bool
    binary_search(_ForwardIterator __first, _ForwardIterator __last,
                  const _Tp& __val, _Compare __comp)
    {
      typedef typename iterator_traits<_ForwardIterator>::value_type
	_ValueType;

      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _Tp, _ValueType>)
      __glibcxx_requires_partitioned_pred(__first, __last, __val, __comp);

      _ForwardIterator __i = std::lower_bound(__first, __last, __val, __comp);
      return __i != __last && !__comp(__val, *__i);
    }

  // Set algorithms: includes, set_union, set_intersection, set_difference,
  // set_symmetric_difference.  All of these algorithms have the precondition
  // that their input ranges are sorted and the postcondition that their output
  // ranges are sorted.

  /**
   *  @brief Determines whether all elements of a sequence exists in a range.
   *  @param  first1  Start of search range.
   *  @param  last1   End of search range.
   *  @param  first2  Start of sequence
   *  @param  last2   End of sequence.
   *  @return  True if each element in [first2,last2) is contained in order
   *  within [first1,last1).  False otherwise.
   *  @ingroup setoperations
   *
   *  This operation expects both [first1,last1) and [first2,last2) to be
   *  sorted.  Searches for the presence of each element in [first2,last2)
   *  within [first1,last1).  The iterators over each range only move forward,
   *  so this is a linear algorithm.  If an element in [first2,last2) is not
   *  found before the search iterator reaches @a last2, false is returned.
  */
  template<typename _InputIterator1, typename _InputIterator2>
    bool
    includes(_InputIterator1 __first1, _InputIterator1 __last1,
	     _InputIterator2 __first2, _InputIterator2 __last2)
    {
      typedef typename iterator_traits<_InputIterator1>::value_type
	_ValueType1;
      typedef typename iterator_traits<_InputIterator2>::value_type
	_ValueType2;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_function_requires(_LessThanOpConcept<_ValueType1, _ValueType2>)
      __glibcxx_function_requires(_LessThanOpConcept<_ValueType2, _ValueType1>)
      __glibcxx_requires_sorted(__first1, __last1);
      __glibcxx_requires_sorted(__first2, __last2);

      while (__first1 != __last1 && __first2 != __last2)
	if (*__first2 < *__first1)
	  return false;
	else if(*__first1 < *__first2)
	  ++__first1;
	else
	  ++__first1, ++__first2;

      return __first2 == __last2;
    }

  /**
   *  @brief Determines whether all elements of a sequence exists in a range
   *  using comparison.
   *  @param  first1  Start of search range.
   *  @param  last1   End of search range.
   *  @param  first2  Start of sequence
   *  @param  last2   End of sequence.
   *  @param  comp    Comparison function to use.
   *  @return  True if each element in [first2,last2) is contained in order
   *  within [first1,last1) according to comp.  False otherwise.
   *  @ingroup setoperations
   *
   *  This operation expects both [first1,last1) and [first2,last2) to be
   *  sorted.  Searches for the presence of each element in [first2,last2)
   *  within [first1,last1), using comp to decide.  The iterators over each
   *  range only move forward, so this is a linear algorithm.  If an element
   *  in [first2,last2) is not found before the search iterator reaches @a
   *  last2, false is returned.
  */
  template<typename _InputIterator1, typename _InputIterator2,
	   typename _Compare>
    bool
    includes(_InputIterator1 __first1, _InputIterator1 __last1,
	     _InputIterator2 __first2, _InputIterator2 __last2, _Compare __comp)
    {
      typedef typename iterator_traits<_InputIterator1>::value_type
	_ValueType1;
      typedef typename iterator_traits<_InputIterator2>::value_type
	_ValueType2;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType1, _ValueType2>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType2, _ValueType1>)
      __glibcxx_requires_sorted_pred(__first1, __last1, __comp);
      __glibcxx_requires_sorted_pred(__first2, __last2, __comp);

      while (__first1 != __last1 && __first2 != __last2)
	if (__comp(*__first2, *__first1))
	  return false;
	else if(__comp(*__first1, *__first2))
	  ++__first1;
	else
	  ++__first1, ++__first2;

      return __first2 == __last2;
    }

  /**
   *  @brief Return the union of two sorted ranges.
   *  @param  first1  Start of first range.
   *  @param  last1   End of first range.
   *  @param  first2  Start of second range.
   *  @param  last2   End of second range.
   *  @return  End of the output range.
   *  @ingroup setoperations
   *
   *  This operation iterates over both ranges, copying elements present in
   *  each range in order to the output range.  Iterators increment for each
   *  range.  When the current element of one range is less than the other,
   *  that element is copied and the iterator advanced.  If an element is
   *  contained in both ranges, the element from the first range is copied and
   *  both ranges advance.  The output range may not overlap either input
   *  range.
  */
  template<typename _InputIterator1, typename _InputIterator2,
	   typename _OutputIterator>
    _OutputIterator
    set_union(_InputIterator1 __first1, _InputIterator1 __last1,
	      _InputIterator2 __first2, _InputIterator2 __last2,
	      _OutputIterator __result)
    {
      typedef typename iterator_traits<_InputIterator1>::value_type
	_ValueType1;
      typedef typename iterator_traits<_InputIterator2>::value_type
	_ValueType2;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType1>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType2>)
      __glibcxx_function_requires(_LessThanOpConcept<_ValueType1, _ValueType2>)
      __glibcxx_function_requires(_LessThanOpConcept<_ValueType2, _ValueType1>)
      __glibcxx_requires_sorted(__first1, __last1);
      __glibcxx_requires_sorted(__first2, __last2);

      while (__first1 != __last1 && __first2 != __last2)
	{
	  if (*__first1 < *__first2)
	    {
	      *__result = *__first1;
	      ++__first1;
	    }
	  else if (*__first2 < *__first1)
	    {
	      *__result = *__first2;
	      ++__first2;
	    }
	  else
	    {
	      *__result = *__first1;
	      ++__first1;
	      ++__first2;
	    }
	  ++__result;
	}
      return std::copy(__first2, __last2, std::copy(__first1, __last1,
						    __result));
    }

  /**
   *  @brief Return the union of two sorted ranges using a comparison functor.
   *  @param  first1  Start of first range.
   *  @param  last1   End of first range.
   *  @param  first2  Start of second range.
   *  @param  last2   End of second range.
   *  @param  comp    The comparison functor.
   *  @return  End of the output range.
   *  @ingroup setoperations
   *
   *  This operation iterates over both ranges, copying elements present in
   *  each range in order to the output range.  Iterators increment for each
   *  range.  When the current element of one range is less than the other
   *  according to @a comp, that element is copied and the iterator advanced.
   *  If an equivalent element according to @a comp is contained in both
   *  ranges, the element from the first range is copied and both ranges
   *  advance.  The output range may not overlap either input range.
  */
  template<typename _InputIterator1, typename _InputIterator2,
	   typename _OutputIterator, typename _Compare>
    _OutputIterator
    set_union(_InputIterator1 __first1, _InputIterator1 __last1,
	      _InputIterator2 __first2, _InputIterator2 __last2,
	      _OutputIterator __result, _Compare __comp)
    {
      typedef typename iterator_traits<_InputIterator1>::value_type
	_ValueType1;
      typedef typename iterator_traits<_InputIterator2>::value_type
	_ValueType2;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType1>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType2>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType1, _ValueType2>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType2, _ValueType1>)
      __glibcxx_requires_sorted_pred(__first1, __last1, __comp);
      __glibcxx_requires_sorted_pred(__first2, __last2, __comp);

      while (__first1 != __last1 && __first2 != __last2)
	{
	  if (__comp(*__first1, *__first2))
	    {
	      *__result = *__first1;
	      ++__first1;
	    }
	  else if (__comp(*__first2, *__first1))
	    {
	      *__result = *__first2;
	      ++__first2;
	    }
	  else
	    {
	      *__result = *__first1;
	      ++__first1;
	      ++__first2;
	    }
	  ++__result;
	}
      return std::copy(__first2, __last2, std::copy(__first1, __last1,
						    __result));
    }

  /**
   *  @brief Return the intersection of two sorted ranges.
   *  @param  first1  Start of first range.
   *  @param  last1   End of first range.
   *  @param  first2  Start of second range.
   *  @param  last2   End of second range.
   *  @return  End of the output range.
   *  @ingroup setoperations
   *
   *  This operation iterates over both ranges, copying elements present in
   *  both ranges in order to the output range.  Iterators increment for each
   *  range.  When the current element of one range is less than the other,
   *  that iterator advances.  If an element is contained in both ranges, the
   *  element from the first range is copied and both ranges advance.  The
   *  output range may not overlap either input range.
  */
  template<typename _InputIterator1, typename _InputIterator2,
	   typename _OutputIterator>
    _OutputIterator
    set_intersection(_InputIterator1 __first1, _InputIterator1 __last1,
		     _InputIterator2 __first2, _InputIterator2 __last2,
		     _OutputIterator __result)
    {
      typedef typename iterator_traits<_InputIterator1>::value_type
	_ValueType1;
      typedef typename iterator_traits<_InputIterator2>::value_type
	_ValueType2;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType1>)
      __glibcxx_function_requires(_LessThanOpConcept<_ValueType1, _ValueType2>)
      __glibcxx_function_requires(_LessThanOpConcept<_ValueType2, _ValueType1>)
      __glibcxx_requires_sorted(__first1, __last1);
      __glibcxx_requires_sorted(__first2, __last2);

      while (__first1 != __last1 && __first2 != __last2)
	if (*__first1 < *__first2)
	  ++__first1;
	else if (*__first2 < *__first1)
	  ++__first2;
	else
	  {
	    *__result = *__first1;
	    ++__first1;
	    ++__first2;
	    ++__result;
	  }
      return __result;
    }

  /**
   *  @brief Return the intersection of two sorted ranges using comparison
   *  functor.
   *  @param  first1  Start of first range.
   *  @param  last1   End of first range.
   *  @param  first2  Start of second range.
   *  @param  last2   End of second range.
   *  @param  comp    The comparison functor.
   *  @return  End of the output range.
   *  @ingroup setoperations
   *
   *  This operation iterates over both ranges, copying elements present in
   *  both ranges in order to the output range.  Iterators increment for each
   *  range.  When the current element of one range is less than the other
   *  according to @a comp, that iterator advances.  If an element is
   *  contained in both ranges according to @a comp, the element from the
   *  first range is copied and both ranges advance.  The output range may not
   *  overlap either input range.
  */
  template<typename _InputIterator1, typename _InputIterator2,
	   typename _OutputIterator, typename _Compare>
    _OutputIterator
    set_intersection(_InputIterator1 __first1, _InputIterator1 __last1,
		     _InputIterator2 __first2, _InputIterator2 __last2,
		     _OutputIterator __result, _Compare __comp)
    {
      typedef typename iterator_traits<_InputIterator1>::value_type
	_ValueType1;
      typedef typename iterator_traits<_InputIterator2>::value_type
	_ValueType2;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType1>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType1, _ValueType2>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType2, _ValueType1>)
      __glibcxx_requires_sorted_pred(__first1, __last1, __comp);
      __glibcxx_requires_sorted_pred(__first2, __last2, __comp);

      while (__first1 != __last1 && __first2 != __last2)
	if (__comp(*__first1, *__first2))
	  ++__first1;
	else if (__comp(*__first2, *__first1))
	  ++__first2;
	else
	  {
	    *__result = *__first1;
	    ++__first1;
	    ++__first2;
	    ++__result;
	  }
      return __result;
    }

  /**
   *  @brief Return the difference of two sorted ranges.
   *  @param  first1  Start of first range.
   *  @param  last1   End of first range.
   *  @param  first2  Start of second range.
   *  @param  last2   End of second range.
   *  @return  End of the output range.
   *  @ingroup setoperations
   *
   *  This operation iterates over both ranges, copying elements present in
   *  the first range but not the second in order to the output range.
   *  Iterators increment for each range.  When the current element of the
   *  first range is less than the second, that element is copied and the
   *  iterator advances.  If the current element of the second range is less,
   *  the iterator advances, but no element is copied.  If an element is
   *  contained in both ranges, no elements are copied and both ranges
   *  advance.  The output range may not overlap either input range.
  */
  template<typename _InputIterator1, typename _InputIterator2,
	   typename _OutputIterator>
    _OutputIterator
    set_difference(_InputIterator1 __first1, _InputIterator1 __last1,
		   _InputIterator2 __first2, _InputIterator2 __last2,
		   _OutputIterator __result)
    {
      typedef typename iterator_traits<_InputIterator1>::value_type
	_ValueType1;
      typedef typename iterator_traits<_InputIterator2>::value_type
	_ValueType2;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType1>)
      __glibcxx_function_requires(_LessThanOpConcept<_ValueType1, _ValueType2>)
      __glibcxx_function_requires(_LessThanOpConcept<_ValueType2, _ValueType1>)	
      __glibcxx_requires_sorted(__first1, __last1);
      __glibcxx_requires_sorted(__first2, __last2);

      while (__first1 != __last1 && __first2 != __last2)
	if (*__first1 < *__first2)
	  {
	    *__result = *__first1;
	    ++__first1;
	    ++__result;
	  }
	else if (*__first2 < *__first1)
	  ++__first2;
	else
	  {
	    ++__first1;
	    ++__first2;
	  }
      return std::copy(__first1, __last1, __result);
    }

  /**
   *  @brief  Return the difference of two sorted ranges using comparison
   *  functor.
   *  @param  first1  Start of first range.
   *  @param  last1   End of first range.
   *  @param  first2  Start of second range.
   *  @param  last2   End of second range.
   *  @param  comp    The comparison functor.
   *  @return  End of the output range.
   *  @ingroup setoperations
   *
   *  This operation iterates over both ranges, copying elements present in
   *  the first range but not the second in order to the output range.
   *  Iterators increment for each range.  When the current element of the
   *  first range is less than the second according to @a comp, that element
   *  is copied and the iterator advances.  If the current element of the
   *  second range is less, no element is copied and the iterator advances.
   *  If an element is contained in both ranges according to @a comp, no
   *  elements are copied and both ranges advance.  The output range may not
   *  overlap either input range.
  */
  template<typename _InputIterator1, typename _InputIterator2,
	   typename _OutputIterator, typename _Compare>
    _OutputIterator
    set_difference(_InputIterator1 __first1, _InputIterator1 __last1,
		   _InputIterator2 __first2, _InputIterator2 __last2,
		   _OutputIterator __result, _Compare __comp)
    {
      typedef typename iterator_traits<_InputIterator1>::value_type
	_ValueType1;
      typedef typename iterator_traits<_InputIterator2>::value_type
	_ValueType2;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType1>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType1, _ValueType2>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType2, _ValueType1>)
      __glibcxx_requires_sorted_pred(__first1, __last1, __comp);
      __glibcxx_requires_sorted_pred(__first2, __last2, __comp);

      while (__first1 != __last1 && __first2 != __last2)
	if (__comp(*__first1, *__first2))
	  {
	    *__result = *__first1;
	    ++__first1;
	    ++__result;
	  }
	else if (__comp(*__first2, *__first1))
	  ++__first2;
	else
	  {
	    ++__first1;
	    ++__first2;
	  }
      return std::copy(__first1, __last1, __result);
    }

  /**
   *  @brief  Return the symmetric difference of two sorted ranges.
   *  @param  first1  Start of first range.
   *  @param  last1   End of first range.
   *  @param  first2  Start of second range.
   *  @param  last2   End of second range.
   *  @return  End of the output range.
   *  @ingroup setoperations
   *
   *  This operation iterates over both ranges, copying elements present in
   *  one range but not the other in order to the output range.  Iterators
   *  increment for each range.  When the current element of one range is less
   *  than the other, that element is copied and the iterator advances.  If an
   *  element is contained in both ranges, no elements are copied and both
   *  ranges advance.  The output range may not overlap either input range.
  */
  template<typename _InputIterator1, typename _InputIterator2,
	   typename _OutputIterator>
    _OutputIterator
    set_symmetric_difference(_InputIterator1 __first1, _InputIterator1 __last1,
			     _InputIterator2 __first2, _InputIterator2 __last2,
			     _OutputIterator __result)
    {
      typedef typename iterator_traits<_InputIterator1>::value_type
	_ValueType1;
      typedef typename iterator_traits<_InputIterator2>::value_type
	_ValueType2;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType1>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType2>)
      __glibcxx_function_requires(_LessThanOpConcept<_ValueType1, _ValueType2>)
      __glibcxx_function_requires(_LessThanOpConcept<_ValueType2, _ValueType1>)	
      __glibcxx_requires_sorted(__first1, __last1);
      __glibcxx_requires_sorted(__first2, __last2);

      while (__first1 != __last1 && __first2 != __last2)
	if (*__first1 < *__first2)
	  {
	    *__result = *__first1;
	    ++__first1;
	    ++__result;
	  }
	else if (*__first2 < *__first1)
	  {
	    *__result = *__first2;
	    ++__first2;
	    ++__result;
	  }
	else
	  {
	    ++__first1;
	    ++__first2;
	  }
      return std::copy(__first2, __last2, std::copy(__first1,
						    __last1, __result));
    }

  /**
   *  @brief  Return the symmetric difference of two sorted ranges using
   *  comparison functor.
   *  @param  first1  Start of first range.
   *  @param  last1   End of first range.
   *  @param  first2  Start of second range.
   *  @param  last2   End of second range.
   *  @param  comp    The comparison functor.
   *  @return  End of the output range.
   *  @ingroup setoperations
   *
   *  This operation iterates over both ranges, copying elements present in
   *  one range but not the other in order to the output range.  Iterators
   *  increment for each range.  When the current element of one range is less
   *  than the other according to @a comp, that element is copied and the
   *  iterator advances.  If an element is contained in both ranges according
   *  to @a comp, no elements are copied and both ranges advance.  The output
   *  range may not overlap either input range.
  */
  template<typename _InputIterator1, typename _InputIterator2,
	   typename _OutputIterator, typename _Compare>
    _OutputIterator
    set_symmetric_difference(_InputIterator1 __first1, _InputIterator1 __last1,
			     _InputIterator2 __first2, _InputIterator2 __last2,
			     _OutputIterator __result,
			     _Compare __comp)
    {
      typedef typename iterator_traits<_InputIterator1>::value_type
	_ValueType1;
      typedef typename iterator_traits<_InputIterator2>::value_type
	_ValueType2;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType1>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				  _ValueType2>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType1, _ValueType2>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType2, _ValueType1>)
      __glibcxx_requires_sorted_pred(__first1, __last1, __comp);
      __glibcxx_requires_sorted_pred(__first2, __last2, __comp);

      while (__first1 != __last1 && __first2 != __last2)
	if (__comp(*__first1, *__first2))
	  {
	    *__result = *__first1;
	    ++__first1;
	    ++__result;
	  }
	else if (__comp(*__first2, *__first1))
	  {
	    *__result = *__first2;
	    ++__first2;
	    ++__result;
	  }
	else
	  {
	    ++__first1;
	    ++__first2;
	  }
      return std::copy(__first2, __last2, std::copy(__first1,
						    __last1, __result));
    }

  // min_element and max_element, with and without an explicitly supplied
  // comparison function.

  /**
   *  @brief  Return the maximum element in a range.
   *  @param  first  Start of range.
   *  @param  last   End of range.
   *  @return  Iterator referencing the first instance of the largest value.
  */
  template<typename _ForwardIterator>
    _ForwardIterator
    max_element(_ForwardIterator __first, _ForwardIterator __last)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return __first;
      _ForwardIterator __result = __first;
      while (++__first != __last)
	if (*__result < *__first)
	  __result = __first;
      return __result;
    }

  /**
   *  @brief  Return the maximum element in a range using comparison functor.
   *  @param  first  Start of range.
   *  @param  last   End of range.
   *  @param  comp   Comparison functor.
   *  @return  Iterator referencing the first instance of the largest value
   *  according to comp.
  */
  template<typename _ForwardIterator, typename _Compare>
    _ForwardIterator
    max_element(_ForwardIterator __first, _ForwardIterator __last,
		_Compare __comp)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_ForwardIterator>::value_type,
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last) return __first;
      _ForwardIterator __result = __first;
      while (++__first != __last)
	if (__comp(*__result, *__first)) __result = __first;
      return __result;
    }

  /**
   *  @brief  Return the minimum element in a range.
   *  @param  first  Start of range.
   *  @param  last   End of range.
   *  @return  Iterator referencing the first instance of the smallest value.
  */
  template<typename _ForwardIterator>
    _ForwardIterator
    min_element(_ForwardIterator __first, _ForwardIterator __last)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return __first;
      _ForwardIterator __result = __first;
      while (++__first != __last)
	if (*__first < *__result)
	  __result = __first;
      return __result;
    }

  /**
   *  @brief  Return the minimum element in a range using comparison functor.
   *  @param  first  Start of range.
   *  @param  last   End of range.
   *  @param  comp   Comparison functor.
   *  @return  Iterator referencing the first instance of the smallest value
   *  according to comp.
  */
  template<typename _ForwardIterator, typename _Compare>
    _ForwardIterator
    min_element(_ForwardIterator __first, _ForwardIterator __last,
		_Compare __comp)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_ForwardIterator>::value_type,
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return __first;
      _ForwardIterator __result = __first;
      while (++__first != __last)
	if (__comp(*__first, *__result))
	  __result = __first;
      return __result;
    }

  // next_permutation and prev_permutation, with and without an explicitly
  // supplied comparison function.

  /**
   *  @brief  Permute range into the next "dictionary" ordering.
   *  @param  first  Start of range.
   *  @param  last   End of range.
   *  @return  False if wrapped to first permutation, true otherwise.
   *
   *  Treats all permutations of the range as a set of "dictionary" sorted
   *  sequences.  Permutes the current sequence into the next one of this set.
   *  Returns true if there are more sequences to generate.  If the sequence
   *  is the largest of the set, the smallest is generated and false returned.
  */
  template<typename _BidirectionalIterator>
    bool
    next_permutation(_BidirectionalIterator __first,
		     _BidirectionalIterator __last)
    {
      // concept requirements
      __glibcxx_function_requires(_BidirectionalIteratorConcept<
				  _BidirectionalIterator>)
      __glibcxx_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_BidirectionalIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return false;
      _BidirectionalIterator __i = __first;
      ++__i;
      if (__i == __last)
	return false;
      __i = __last;
      --__i;

      for(;;)
	{
	  _BidirectionalIterator __ii = __i;
	  --__i;
	  if (*__i < *__ii)
	    {
	      _BidirectionalIterator __j = __last;
	      while (!(*__i < *--__j))
		{}
	      std::iter_swap(__i, __j);
	      std::reverse(__ii, __last);
	      return true;
	    }
	  if (__i == __first)
	    {
	      std::reverse(__first, __last);
	      return false;
	    }
	}
    }

  /**
   *  @brief  Permute range into the next "dictionary" ordering using
   *  comparison functor.
   *  @param  first  Start of range.
   *  @param  last   End of range.
   *  @param  comp
   *  @return  False if wrapped to first permutation, true otherwise.
   *
   *  Treats all permutations of the range [first,last) as a set of
   *  "dictionary" sorted sequences ordered by @a comp.  Permutes the current
   *  sequence into the next one of this set.  Returns true if there are more
   *  sequences to generate.  If the sequence is the largest of the set, the
   *  smallest is generated and false returned.
  */
  template<typename _BidirectionalIterator, typename _Compare>
    bool
    next_permutation(_BidirectionalIterator __first,
		     _BidirectionalIterator __last, _Compare __comp)
    {
      // concept requirements
      __glibcxx_function_requires(_BidirectionalIteratorConcept<
				  _BidirectionalIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_BidirectionalIterator>::value_type,
	    typename iterator_traits<_BidirectionalIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return false;
      _BidirectionalIterator __i = __first;
      ++__i;
      if (__i == __last)
	return false;
      __i = __last;
      --__i;

      for(;;)
	{
	  _BidirectionalIterator __ii = __i;
	  --__i;
	  if (__comp(*__i, *__ii))
	    {
	      _BidirectionalIterator __j = __last;
	      while (!__comp(*__i, *--__j))
		{}
	      std::iter_swap(__i, __j);
	      std::reverse(__ii, __last);
	      return true;
	    }
	  if (__i == __first)
	    {
	      std::reverse(__first, __last);
	      return false;
	    }
	}
    }

  /**
   *  @brief  Permute range into the previous "dictionary" ordering.
   *  @param  first  Start of range.
   *  @param  last   End of range.
   *  @return  False if wrapped to last permutation, true otherwise.
   *
   *  Treats all permutations of the range as a set of "dictionary" sorted
   *  sequences.  Permutes the current sequence into the previous one of this
   *  set.  Returns true if there are more sequences to generate.  If the
   *  sequence is the smallest of the set, the largest is generated and false
   *  returned.
  */
  template<typename _BidirectionalIterator>
    bool
    prev_permutation(_BidirectionalIterator __first,
		     _BidirectionalIterator __last)
    {
      // concept requirements
      __glibcxx_function_requires(_BidirectionalIteratorConcept<
				  _BidirectionalIterator>)
      __glibcxx_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_BidirectionalIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return false;
      _BidirectionalIterator __i = __first;
      ++__i;
      if (__i == __last)
	return false;
      __i = __last;
      --__i;

      for(;;)
	{
	  _BidirectionalIterator __ii = __i;
	  --__i;
	  if (*__ii < *__i)
	    {
	      _BidirectionalIterator __j = __last;
	      while (!(*--__j < *__i))
		{}
	      std::iter_swap(__i, __j);
	      std::reverse(__ii, __last);
	      return true;
	    }
	  if (__i == __first)
	    {
	      std::reverse(__first, __last);
	      return false;
	    }
	}
    }

  /**
   *  @brief  Permute range into the previous "dictionary" ordering using
   *  comparison functor.
   *  @param  first  Start of range.
   *  @param  last   End of range.
   *  @param  comp
   *  @return  False if wrapped to last permutation, true otherwise.
   *
   *  Treats all permutations of the range [first,last) as a set of
   *  "dictionary" sorted sequences ordered by @a comp.  Permutes the current
   *  sequence into the previous one of this set.  Returns true if there are
   *  more sequences to generate.  If the sequence is the smallest of the set,
   *  the largest is generated and false returned.
  */
  template<typename _BidirectionalIterator, typename _Compare>
    bool
    prev_permutation(_BidirectionalIterator __first,
		     _BidirectionalIterator __last, _Compare __comp)
    {
      // concept requirements
      __glibcxx_function_requires(_BidirectionalIteratorConcept<
				  _BidirectionalIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_BidirectionalIterator>::value_type,
	    typename iterator_traits<_BidirectionalIterator>::value_type>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return false;
      _BidirectionalIterator __i = __first;
      ++__i;
      if (__i == __last)
	return false;
      __i = __last;
      --__i;

      for(;;)
	{
	  _BidirectionalIterator __ii = __i;
	  --__i;
	  if (__comp(*__ii, *__i))
	    {
	      _BidirectionalIterator __j = __last;
	      while (!__comp(*--__j, *__i))
		{}
	      std::iter_swap(__i, __j);
	      std::reverse(__ii, __last);
	      return true;
	    }
	  if (__i == __first)
	    {
	      std::reverse(__first, __last);
	      return false;
	    }
	}
    }

  // find_first_of, with and without an explicitly supplied comparison function.

  /**
   *  @brief  Find element from a set in a sequence.
   *  @param  first1  Start of range to search.
   *  @param  last1   End of range to search.
   *  @param  first2  Start of match candidates.
   *  @param  last2   End of match candidates.
   *  @return   The first iterator @c i in the range
   *  @p [first1,last1) such that @c *i == @p *(i2) such that i2 is an
   *  interator in [first2,last2), or @p last1 if no such iterator exists.
   *
   *  Searches the range @p [first1,last1) for an element that is equal to
   *  some element in the range [first2,last2).  If found, returns an iterator
   *  in the range [first1,last1), otherwise returns @p last1.
  */
  template<typename _InputIterator, typename _ForwardIterator>
    _InputIterator
    find_first_of(_InputIterator __first1, _InputIterator __last1,
		  _ForwardIterator __first2, _ForwardIterator __last2)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_EqualOpConcept<
	    typename iterator_traits<_InputIterator>::value_type,
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first1, __last1);
      __glibcxx_requires_valid_range(__first2, __last2);

      for ( ; __first1 != __last1; ++__first1)
	for (_ForwardIterator __iter = __first2; __iter != __last2; ++__iter)
	  if (*__first1 == *__iter)
	    return __first1;
      return __last1;
    }

  /**
   *  @brief  Find element from a set in a sequence using a predicate.
   *  @param  first1  Start of range to search.
   *  @param  last1   End of range to search.
   *  @param  first2  Start of match candidates.
   *  @param  last2   End of match candidates.
   *  @param  comp    Predicate to use.
   *  @return   The first iterator @c i in the range
   *  @p [first1,last1) such that @c comp(*i, @p *(i2)) is true and i2 is an
   *  interator in [first2,last2), or @p last1 if no such iterator exists.
   *
   *  Searches the range @p [first1,last1) for an element that is equal to
   *  some element in the range [first2,last2).  If found, returns an iterator in
   *  the range [first1,last1), otherwise returns @p last1.
  */
  template<typename _InputIterator, typename _ForwardIterator,
	   typename _BinaryPredicate>
    _InputIterator
    find_first_of(_InputIterator __first1, _InputIterator __last1,
		  _ForwardIterator __first2, _ForwardIterator __last2,
		  _BinaryPredicate __comp)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
	    typename iterator_traits<_InputIterator>::value_type,
	    typename iterator_traits<_ForwardIterator>::value_type>)
      __glibcxx_requires_valid_range(__first1, __last1);
      __glibcxx_requires_valid_range(__first2, __last2);

      for ( ; __first1 != __last1; ++__first1)
	for (_ForwardIterator __iter = __first2; __iter != __last2; ++__iter)
	  if (__comp(*__first1, *__iter))
	    return __first1;
      return __last1;
    }


  // find_end, with and without an explicitly supplied comparison function.
  // Search [first2, last2) as a subsequence in [first1, last1), and return
  // the *last* possible match.  Note that find_end for bidirectional iterators
  // is much faster than for forward iterators.

  // find_end for forward iterators.
  template<typename _ForwardIterator1, typename _ForwardIterator2>
    _ForwardIterator1
    __find_end(_ForwardIterator1 __first1, _ForwardIterator1 __last1,
	       _ForwardIterator2 __first2, _ForwardIterator2 __last2,
	       forward_iterator_tag, forward_iterator_tag)
    {
      if (__first2 == __last2)
	return __last1;
      else
	{
	  _ForwardIterator1 __result = __last1;
	  while (1)
	    {
	      _ForwardIterator1 __new_result
		= std::search(__first1, __last1, __first2, __last2);
	      if (__new_result == __last1)
		return __result;
	      else
		{
		  __result = __new_result;
		  __first1 = __new_result;
		  ++__first1;
		}
	    }
	}
    }

  template<typename _ForwardIterator1, typename _ForwardIterator2,
	   typename _BinaryPredicate>
    _ForwardIterator1
    __find_end(_ForwardIterator1 __first1, _ForwardIterator1 __last1,
	       _ForwardIterator2 __first2, _ForwardIterator2 __last2,
	       forward_iterator_tag, forward_iterator_tag,
	       _BinaryPredicate __comp)
    {
      if (__first2 == __last2)
	return __last1;
      else
	{
	  _ForwardIterator1 __result = __last1;
	  while (1)
	    {
	      _ForwardIterator1 __new_result
		= std::search(__first1, __last1, __first2, __last2, __comp);
	      if (__new_result == __last1)
		return __result;
	      else
		{
		  __result = __new_result;
		  __first1 = __new_result;
		  ++__first1;
		}
	    }
	}
    }

  // find_end for bidirectional iterators.  Requires partial specialization.
  template<typename _BidirectionalIterator1, typename _BidirectionalIterator2>
    _BidirectionalIterator1
    __find_end(_BidirectionalIterator1 __first1,
	       _BidirectionalIterator1 __last1,
	       _BidirectionalIterator2 __first2,
	       _BidirectionalIterator2 __last2,
	       bidirectional_iterator_tag, bidirectional_iterator_tag)
    {
      // concept requirements
      __glibcxx_function_requires(_BidirectionalIteratorConcept<
				  _BidirectionalIterator1>)
      __glibcxx_function_requires(_BidirectionalIteratorConcept<
				  _BidirectionalIterator2>)

      typedef reverse_iterator<_BidirectionalIterator1> _RevIterator1;
      typedef reverse_iterator<_BidirectionalIterator2> _RevIterator2;

      _RevIterator1 __rlast1(__first1);
      _RevIterator2 __rlast2(__first2);
      _RevIterator1 __rresult = std::search(_RevIterator1(__last1), __rlast1,
					    _RevIterator2(__last2), __rlast2);

      if (__rresult == __rlast1)
	return __last1;
      else
	{
	  _BidirectionalIterator1 __result = __rresult.base();
	  std::advance(__result, -std::distance(__first2, __last2));
	  return __result;
	}
    }

  template<typename _BidirectionalIterator1, typename _BidirectionalIterator2,
	   typename _BinaryPredicate>
    _BidirectionalIterator1
    __find_end(_BidirectionalIterator1 __first1,
	       _BidirectionalIterator1 __last1,
	       _BidirectionalIterator2 __first2,
	       _BidirectionalIterator2 __last2,
	       bidirectional_iterator_tag, bidirectional_iterator_tag,
	       _BinaryPredicate __comp)
    {
      // concept requirements
      __glibcxx_function_requires(_BidirectionalIteratorConcept<
				  _BidirectionalIterator1>)
      __glibcxx_function_requires(_BidirectionalIteratorConcept<
				  _BidirectionalIterator2>)

      typedef reverse_iterator<_BidirectionalIterator1> _RevIterator1;
      typedef reverse_iterator<_BidirectionalIterator2> _RevIterator2;

      _RevIterator1 __rlast1(__first1);
      _RevIterator2 __rlast2(__first2);
      _RevIterator1 __rresult = std::search(_RevIterator1(__last1), __rlast1,
					    _RevIterator2(__last2), __rlast2,
					    __comp);

      if (__rresult == __rlast1)
	return __last1;
      else
	{
	  _BidirectionalIterator1 __result = __rresult.base();
	  std::advance(__result, -std::distance(__first2, __last2));
	  return __result;
	}
    }

  // Dispatching functions for find_end.

  /**
   *  @brief  Find last matching subsequence in a sequence.
   *  @param  first1  Start of range to search.
   *  @param  last1   End of range to search.
   *  @param  first2  Start of sequence to match.
   *  @param  last2   End of sequence to match.
   *  @return   The last iterator @c i in the range
   *  @p [first1,last1-(last2-first2)) such that @c *(i+N) == @p *(first2+N)
   *  for each @c N in the range @p [0,last2-first2), or @p last1 if no
   *  such iterator exists.
   *
   *  Searches the range @p [first1,last1) for a sub-sequence that compares
   *  equal value-by-value with the sequence given by @p [first2,last2) and
   *  returns an iterator to the first element of the sub-sequence, or
   *  @p last1 if the sub-sequence is not found.  The sub-sequence will be the
   *  last such subsequence contained in [first,last1).
   *
   *  Because the sub-sequence must lie completely within the range
   *  @p [first1,last1) it must start at a position less than
   *  @p last1-(last2-first2) where @p last2-first2 is the length of the
   *  sub-sequence.
   *  This means that the returned iterator @c i will be in the range
   *  @p [first1,last1-(last2-first2))
  */
  template<typename _ForwardIterator1, typename _ForwardIterator2>
    inline _ForwardIterator1
    find_end(_ForwardIterator1 __first1, _ForwardIterator1 __last1,
	     _ForwardIterator2 __first2, _ForwardIterator2 __last2)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator1>)
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator2>)
      __glibcxx_function_requires(_EqualOpConcept<
	    typename iterator_traits<_ForwardIterator1>::value_type,
	    typename iterator_traits<_ForwardIterator2>::value_type>)
      __glibcxx_requires_valid_range(__first1, __last1);
      __glibcxx_requires_valid_range(__first2, __last2);

      return std::__find_end(__first1, __last1, __first2, __last2,
			     std::__iterator_category(__first1),
			     std::__iterator_category(__first2));
    }

  /**
   *  @brief  Find last matching subsequence in a sequence using a predicate.
   *  @param  first1  Start of range to search.
   *  @param  last1   End of range to search.
   *  @param  first2  Start of sequence to match.
   *  @param  last2   End of sequence to match.
   *  @param  comp    The predicate to use.
   *  @return   The last iterator @c i in the range
   *  @p [first1,last1-(last2-first2)) such that @c predicate(*(i+N), @p
   *  (first2+N)) is true for each @c N in the range @p [0,last2-first2), or
   *  @p last1 if no such iterator exists.
   *
   *  Searches the range @p [first1,last1) for a sub-sequence that compares
   *  equal value-by-value with the sequence given by @p [first2,last2) using
   *  comp as a predicate and returns an iterator to the first element of the
   *  sub-sequence, or @p last1 if the sub-sequence is not found.  The
   *  sub-sequence will be the last such subsequence contained in
   *  [first,last1).
   *
   *  Because the sub-sequence must lie completely within the range
   *  @p [first1,last1) it must start at a position less than
   *  @p last1-(last2-first2) where @p last2-first2 is the length of the
   *  sub-sequence.
   *  This means that the returned iterator @c i will be in the range
   *  @p [first1,last1-(last2-first2))
  */
  template<typename _ForwardIterator1, typename _ForwardIterator2,
	   typename _BinaryPredicate>
    inline _ForwardIterator1
    find_end(_ForwardIterator1 __first1, _ForwardIterator1 __last1,
	     _ForwardIterator2 __first2, _ForwardIterator2 __last2,
	     _BinaryPredicate __comp)
    {
      // concept requirements
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator1>)
      __glibcxx_function_requires(_ForwardIteratorConcept<_ForwardIterator2>)
      __glibcxx_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
	    typename iterator_traits<_ForwardIterator1>::value_type,
	    typename iterator_traits<_ForwardIterator2>::value_type>)
      __glibcxx_requires_valid_range(__first1, __last1);
      __glibcxx_requires_valid_range(__first2, __last2);

      return std::__find_end(__first1, __last1, __first2, __last2,
			     std::__iterator_category(__first1),
			     std::__iterator_category(__first2),
			     __comp);
    }

_GLIBCXX_END_NAMESPACE

#endif /* _ALGO_H */
