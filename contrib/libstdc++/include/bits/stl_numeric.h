// Numeric functions implementation -*- C++ -*-

// Copyright (C) 2001, 2004, 2005 Free Software Foundation, Inc.
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
 * Copyright (c) 1996,1997
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

/** @file stl_numeric.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _STL_NUMERIC_H
#define _STL_NUMERIC_H 1

#include <debug/debug.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  /**
   *  @brief  Accumulate values in a range.
   *
   *  Accumulates the values in the range [first,last) using operator+().  The
   *  initial value is @a init.  The values are processed in order.
   *
   *  @param  first  Start of range.
   *  @param  last  End of range.
   *  @param  init  Starting value to add other values to.
   *  @return  The final sum.
   */
  template<typename _InputIterator, typename _Tp>
    _Tp
    accumulate(_InputIterator __first, _InputIterator __last, _Tp __init)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_requires_valid_range(__first, __last);

      for (; __first != __last; ++__first)
	__init = __init + *__first;
      return __init;
    }

  /**
   *  @brief  Accumulate values in a range with operation.
   *
   *  Accumulates the values in the range [first,last) using the function
   *  object @a binary_op.  The initial value is @a init.  The values are
   *  processed in order.
   *
   *  @param  first  Start of range.
   *  @param  last  End of range.
   *  @param  init  Starting value to add other values to.
   *  @param  binary_op  Function object to accumulate with.
   *  @return  The final sum.
   */
  template<typename _InputIterator, typename _Tp, typename _BinaryOperation>
    _Tp
    accumulate(_InputIterator __first, _InputIterator __last, _Tp __init,
	       _BinaryOperation __binary_op)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_requires_valid_range(__first, __last);

      for (; __first != __last; ++__first)
	__init = __binary_op(__init, *__first);
      return __init;
    }

  /**
   *  @brief  Compute inner product of two ranges.
   *
   *  Starting with an initial value of @a init, multiplies successive
   *  elements from the two ranges and adds each product into the accumulated
   *  value using operator+().  The values in the ranges are processed in
   *  order.
   *
   *  @param  first1  Start of range 1.
   *  @param  last1  End of range 1.
   *  @param  first2  Start of range 2.
   *  @param  init  Starting value to add other values to.
   *  @return  The final inner product.
   */
  template<typename _InputIterator1, typename _InputIterator2, typename _Tp>
    _Tp
    inner_product(_InputIterator1 __first1, _InputIterator1 __last1,
		  _InputIterator2 __first2, _Tp __init)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_requires_valid_range(__first1, __last1);

      for (; __first1 != __last1; ++__first1, ++__first2)
	__init = __init + (*__first1 * *__first2);
      return __init;
    }

  /**
   *  @brief  Compute inner product of two ranges.
   *
   *  Starting with an initial value of @a init, applies @a binary_op2 to
   *  successive elements from the two ranges and accumulates each result into
   *  the accumulated value using @a binary_op1.  The values in the ranges are
   *  processed in order.
   *
   *  @param  first1  Start of range 1.
   *  @param  last1  End of range 1.
   *  @param  first2  Start of range 2.
   *  @param  init  Starting value to add other values to.
   *  @param  binary_op1  Function object to accumulate with.
   *  @param  binary_op2  Function object to apply to pairs of input values.
   *  @return  The final inner product.
   */
  template<typename _InputIterator1, typename _InputIterator2, typename _Tp,
	    typename _BinaryOperation1, typename _BinaryOperation2>
    _Tp
    inner_product(_InputIterator1 __first1, _InputIterator1 __last1,
		  _InputIterator2 __first2, _Tp __init,
		  _BinaryOperation1 __binary_op1,
		  _BinaryOperation2 __binary_op2)
    {
      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator1>)
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator2>)
      __glibcxx_requires_valid_range(__first1, __last1);

      for (; __first1 != __last1; ++__first1, ++__first2)
	__init = __binary_op1(__init, __binary_op2(*__first1, *__first2));
      return __init;
    }

  /**
   *  @brief  Return list of partial sums
   *
   *  Accumulates the values in the range [first,last) using operator+().
   *  As each successive input value is added into the total, that partial sum
   *  is written to @a result.  Therefore, the first value in result is the
   *  first value of the input, the second value in result is the sum of the
   *  first and second input values, and so on.
   *
   *  @param  first  Start of input range.
   *  @param  last  End of input range.
   *  @param  result  Output to write sums to.
   *  @return  Iterator pointing just beyond the values written to result.
   */
  template<typename _InputIterator, typename _OutputIterator>
    _OutputIterator
    partial_sum(_InputIterator __first, _InputIterator __last,
		_OutputIterator __result)
    {
      typedef typename iterator_traits<_InputIterator>::value_type _ValueType;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				                         _ValueType>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return __result;
      _ValueType __value = *__first;
      *__result = __value;
      while (++__first != __last)
	{
	  __value = __value + *__first;
	  *++__result = __value;
	}
      return ++__result;
    }

  /**
   *  @brief  Return list of partial sums
   *
   *  Accumulates the values in the range [first,last) using operator+().
   *  As each successive input value is added into the total, that partial sum
   *  is written to @a result.  Therefore, the first value in result is the
   *  first value of the input, the second value in result is the sum of the
   *  first and second input values, and so on.
   *
   *  @param  first  Start of input range.
   *  @param  last  End of input range.
   *  @param  result  Output to write sums to.
   *  @return  Iterator pointing just beyond the values written to result.
   */
  template<typename _InputIterator, typename _OutputIterator,
	   typename _BinaryOperation>
    _OutputIterator
    partial_sum(_InputIterator __first, _InputIterator __last,
		_OutputIterator __result, _BinaryOperation __binary_op)
    {
      typedef typename iterator_traits<_InputIterator>::value_type _ValueType;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				                         _ValueType>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return __result;
      _ValueType __value = *__first;
      *__result = __value;
      while (++__first != __last)
	{
	  __value = __binary_op(__value, *__first);
	  *++__result = __value;
	}
      return ++__result;
    }

  /**
   *  @brief  Return differences between adjacent values.
   *
   *  Computes the difference between adjacent values in the range
   *  [first,last) using operator-() and writes the result to @a result.
   *
   *  @param  first  Start of input range.
   *  @param  last  End of input range.
   *  @param  result  Output to write sums to.
   *  @return  Iterator pointing just beyond the values written to result.
   */
  template<typename _InputIterator, typename _OutputIterator>
    _OutputIterator
    adjacent_difference(_InputIterator __first,
			_InputIterator __last, _OutputIterator __result)
    {
      typedef typename iterator_traits<_InputIterator>::value_type _ValueType;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				                         _ValueType>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return __result;
      _ValueType __value = *__first;
      *__result = __value;
      while (++__first != __last)
	{
	  _ValueType __tmp = *__first;
	  *++__result = __tmp - __value;
	  __value = __tmp;
	}
      return ++__result;
    }

  /**
   *  @brief  Return differences between adjacent values.
   *
   *  Computes the difference between adjacent values in the range
   *  [first,last) using the function object @a binary_op and writes the
   *  result to @a result.
   *
   *  @param  first  Start of input range.
   *  @param  last  End of input range.
   *  @param  result  Output to write sums to.
   *  @return  Iterator pointing just beyond the values written to result.
   */
  template<typename _InputIterator, typename _OutputIterator,
	   typename _BinaryOperation>
    _OutputIterator
    adjacent_difference(_InputIterator __first, _InputIterator __last,
			_OutputIterator __result, _BinaryOperation __binary_op)
    {
      typedef typename iterator_traits<_InputIterator>::value_type _ValueType;

      // concept requirements
      __glibcxx_function_requires(_InputIteratorConcept<_InputIterator>)
      __glibcxx_function_requires(_OutputIteratorConcept<_OutputIterator,
				                         _ValueType>)
      __glibcxx_requires_valid_range(__first, __last);

      if (__first == __last)
	return __result;
      _ValueType __value = *__first;
      *__result = __value;
      while (++__first != __last)
	{
	  _ValueType __tmp = *__first;
	  *++__result = __binary_op(__tmp, __value);
	  __value = __tmp;
	}
      return ++__result;
    }

_GLIBCXX_END_NAMESPACE

#endif /* _STL_NUMERIC_H */
