// The template and inlines for the -*- C++ -*- valarray class.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2004, 2005, 2006, 2007
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

/** @file valarray
 *  This is a Standard C++ Library header. 
 */

// Written by Gabriel Dos Reis <Gabriel.Dos-Reis@DPTMaths.ENS-Cachan.Fr>

#ifndef _GLIBCXX_VALARRAY
#define _GLIBCXX_VALARRAY 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <algorithm>
#include <debug/debug.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  template<class _Clos, typename _Tp> 
    class _Expr;

  template<typename _Tp1, typename _Tp2> 
    class _ValArray;    

  template<class _Oper, template<class, class> class _Meta, class _Dom>
    struct _UnClos;

  template<class _Oper,
        template<class, class> class _Meta1,
        template<class, class> class _Meta2,
        class _Dom1, class _Dom2> 
    class _BinClos;

  template<template<class, class> class _Meta, class _Dom> 
    class _SClos;

  template<template<class, class> class _Meta, class _Dom> 
    class _GClos;
    
  template<template<class, class> class _Meta, class _Dom> 
    class _IClos;
    
  template<template<class, class> class _Meta, class _Dom> 
    class _ValFunClos;
  
  template<template<class, class> class _Meta, class _Dom> 
    class _RefFunClos;

  template<class _Tp> class valarray;   // An array of type _Tp
  class slice;                          // BLAS-like slice out of an array
  template<class _Tp> class slice_array;
  class gslice;                         // generalized slice out of an array
  template<class _Tp> class gslice_array;
  template<class _Tp> class mask_array;     // masked array
  template<class _Tp> class indirect_array; // indirected array

_GLIBCXX_END_NAMESPACE

#include <bits/valarray_array.h>
#include <bits/valarray_before.h>
  
_GLIBCXX_BEGIN_NAMESPACE(std)

  /**
   *  @brief  Smart array designed to support numeric processing.
   *
   *  A valarray is an array that provides constraints intended to allow for
   *  effective optimization of numeric array processing by reducing the
   *  aliasing that can result from pointer representations.  It represents a
   *  one-dimensional array from which different multidimensional subsets can
   *  be accessed and modified.
   *  
   *  @param  Tp  Type of object in the array.
   */
  template<class _Tp> 
    class valarray
    {
      template<class _Op>
	struct _UnaryOp 
	{
	  typedef typename __fun<_Op, _Tp>::result_type __rt;
	  typedef _Expr<_UnClos<_Op, _ValArray, _Tp>, __rt> _Rt;
	};
    public:
      typedef _Tp value_type;
      
	// _lib.valarray.cons_ construct/destroy:
      ///  Construct an empty array.
      valarray();

      ///  Construct an array with @a n elements.
      explicit valarray(size_t);

      ///  Construct an array with @a n elements initialized to @a t.
      valarray(const _Tp&, size_t);

      ///  Construct an array initialized to the first @a n elements of @a t.
      valarray(const _Tp* __restrict__, size_t);

      ///  Copy constructor.
      valarray(const valarray&);

      ///  Construct an array with the same size and values in @a sa.
      valarray(const slice_array<_Tp>&);

      ///  Construct an array with the same size and values in @a ga.
      valarray(const gslice_array<_Tp>&);

      ///  Construct an array with the same size and values in @a ma.
      valarray(const mask_array<_Tp>&);

      ///  Construct an array with the same size and values in @a ia.
      valarray(const indirect_array<_Tp>&);

      template<class _Dom>
	valarray(const _Expr<_Dom, _Tp>& __e);

      ~valarray();

      // _lib.valarray.assign_ assignment:
      /**
       *  @brief  Assign elements to an array.
       *
       *  Assign elements of array to values in @a v.  Results are undefined
       *  if @a v does not have the same size as this array.
       *
       *  @param  v  Valarray to get values from.
       */
      valarray<_Tp>& operator=(const valarray<_Tp>&);

      /**
       *  @brief  Assign elements to a value.
       *
       *  Assign all elements of array to @a t.
       *
       *  @param  t  Value for elements.
       */
      valarray<_Tp>& operator=(const _Tp&);

      /**
       *  @brief  Assign elements to an array subset.
       *
       *  Assign elements of array to values in @a sa.  Results are undefined
       *  if @a sa does not have the same size as this array.
       *
       *  @param  sa  Array slice to get values from.
       */
      valarray<_Tp>& operator=(const slice_array<_Tp>&);

      /**
       *  @brief  Assign elements to an array subset.
       *
       *  Assign elements of array to values in @a ga.  Results are undefined
       *  if @a ga does not have the same size as this array.
       *
       *  @param  ga  Array slice to get values from.
       */
      valarray<_Tp>& operator=(const gslice_array<_Tp>&);

      /**
       *  @brief  Assign elements to an array subset.
       *
       *  Assign elements of array to values in @a ma.  Results are undefined
       *  if @a ma does not have the same size as this array.
       *
       *  @param  ma  Array slice to get values from.
       */
      valarray<_Tp>& operator=(const mask_array<_Tp>&);

      /**
       *  @brief  Assign elements to an array subset.
       *
       *  Assign elements of array to values in @a ia.  Results are undefined
       *  if @a ia does not have the same size as this array.
       *
       *  @param  ia  Array slice to get values from.
       */
      valarray<_Tp>& operator=(const indirect_array<_Tp>&);

      template<class _Dom> valarray<_Tp>&
	operator= (const _Expr<_Dom, _Tp>&);

      // _lib.valarray.access_ element access:
      /**
       *  Return a reference to the i'th array element.  
       *
       *  @param  i  Index of element to return.
       *  @return  Reference to the i'th element.
       */
      _Tp&                operator[](size_t);

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 389. Const overload of valarray::operator[] returns by value.
      const _Tp&          operator[](size_t) const;

      // _lib.valarray.sub_ subset operations:
      /**
       *  @brief  Return an array subset.
       *
       *  Returns a new valarray containing the elements of the array
       *  indicated by the slice argument.  The new valarray has the same size
       *  as the input slice.  @see slice.
       *
       *  @param  s  The source slice.
       *  @return  New valarray containing elements in @a s.
       */
      _Expr<_SClos<_ValArray, _Tp>, _Tp> operator[](slice) const;

      /**
       *  @brief  Return a reference to an array subset.
       *
       *  Returns a new valarray containing the elements of the array
       *  indicated by the slice argument.  The new valarray has the same size
       *  as the input slice.  @see slice.
       *
       *  @param  s  The source slice.
       *  @return  New valarray containing elements in @a s.
       */
      slice_array<_Tp>    operator[](slice);

      /**
       *  @brief  Return an array subset.
       *
       *  Returns a slice_array referencing the elements of the array
       *  indicated by the slice argument.  @see gslice.
       *
       *  @param  s  The source slice.
       *  @return  Slice_array referencing elements indicated by @a s.
       */
      _Expr<_GClos<_ValArray, _Tp>, _Tp> operator[](const gslice&) const;

      /**
       *  @brief  Return a reference to an array subset.
       *
       *  Returns a new valarray containing the elements of the array
       *  indicated by the gslice argument.  The new valarray has
       *  the same size as the input gslice.  @see gslice.
       *
       *  @param  s  The source gslice.
       *  @return  New valarray containing elements in @a s.
       */
      gslice_array<_Tp>   operator[](const gslice&);

      /**
       *  @brief  Return an array subset.
       *
       *  Returns a new valarray containing the elements of the array
       *  indicated by the argument.  The input is a valarray of bool which
       *  represents a bitmask indicating which elements should be copied into
       *  the new valarray.  Each element of the array is added to the return
       *  valarray if the corresponding element of the argument is true.
       *
       *  @param  m  The valarray bitmask.
       *  @return  New valarray containing elements indicated by @a m.
       */
      valarray<_Tp>       operator[](const valarray<bool>&) const;

      /**
       *  @brief  Return a reference to an array subset.
       *
       *  Returns a new mask_array referencing the elements of the array
       *  indicated by the argument.  The input is a valarray of bool which
       *  represents a bitmask indicating which elements are part of the
       *  subset.  Elements of the array are part of the subset if the
       *  corresponding element of the argument is true.
       *
       *  @param  m  The valarray bitmask.
       *  @return  New valarray containing elements indicated by @a m.
       */
      mask_array<_Tp>     operator[](const valarray<bool>&);

      /**
       *  @brief  Return an array subset.
       *
       *  Returns a new valarray containing the elements of the array
       *  indicated by the argument.  The elements in the argument are
       *  interpreted as the indices of elements of this valarray to copy to
       *  the return valarray.
       *
       *  @param  i  The valarray element index list.
       *  @return  New valarray containing elements in @a s.
       */
      _Expr<_IClos<_ValArray, _Tp>, _Tp>
        operator[](const valarray<size_t>&) const;

      /**
       *  @brief  Return a reference to an array subset.
       *
       *  Returns an indirect_array referencing the elements of the array
       *  indicated by the argument.  The elements in the argument are
       *  interpreted as the indices of elements of this valarray to include
       *  in the subset.  The returned indirect_array refers to these
       *  elements.
       *
       *  @param  i  The valarray element index list.
       *  @return  Indirect_array referencing elements in @a i.
       */
      indirect_array<_Tp> operator[](const valarray<size_t>&);

      // _lib.valarray.unary_ unary operators:
      ///  Return a new valarray by applying unary + to each element.
      typename _UnaryOp<__unary_plus>::_Rt  operator+() const;

      ///  Return a new valarray by applying unary - to each element.
      typename _UnaryOp<__negate>::_Rt      operator-() const;

      ///  Return a new valarray by applying unary ~ to each element.
      typename _UnaryOp<__bitwise_not>::_Rt operator~() const;

      ///  Return a new valarray by applying unary ! to each element.
      typename _UnaryOp<__logical_not>::_Rt operator!() const;

      // _lib.valarray.cassign_ computed assignment:
      ///  Multiply each element of array by @a t.
      valarray<_Tp>& operator*=(const _Tp&);

      ///  Divide each element of array by @a t.
      valarray<_Tp>& operator/=(const _Tp&);

      ///  Set each element e of array to e % @a t.
      valarray<_Tp>& operator%=(const _Tp&);

      ///  Add @a t to each element of array.
      valarray<_Tp>& operator+=(const _Tp&);

      ///  Subtract @a t to each element of array.
      valarray<_Tp>& operator-=(const _Tp&);

      ///  Set each element e of array to e ^ @a t.
      valarray<_Tp>& operator^=(const _Tp&);

      ///  Set each element e of array to e & @a t.
      valarray<_Tp>& operator&=(const _Tp&);

      ///  Set each element e of array to e | @a t.
      valarray<_Tp>& operator|=(const _Tp&);

      ///  Left shift each element e of array by @a t bits.
      valarray<_Tp>& operator<<=(const _Tp&);

      ///  Right shift each element e of array by @a t bits.
      valarray<_Tp>& operator>>=(const _Tp&);

      ///  Multiply elements of array by corresponding elements of @a v.
      valarray<_Tp>& operator*=(const valarray<_Tp>&);

      ///  Divide elements of array by corresponding elements of @a v.
      valarray<_Tp>& operator/=(const valarray<_Tp>&);

      ///  Modulo elements of array by corresponding elements of @a v.
      valarray<_Tp>& operator%=(const valarray<_Tp>&);

      ///  Add corresponding elements of @a v to elements of array.
      valarray<_Tp>& operator+=(const valarray<_Tp>&);

      ///  Subtract corresponding elements of @a v from elements of array.
      valarray<_Tp>& operator-=(const valarray<_Tp>&);

      ///  Logical xor corresponding elements of @a v with elements of array.
      valarray<_Tp>& operator^=(const valarray<_Tp>&);

      ///  Logical or corresponding elements of @a v with elements of array.
      valarray<_Tp>& operator|=(const valarray<_Tp>&);

      ///  Logical and corresponding elements of @a v with elements of array.
      valarray<_Tp>& operator&=(const valarray<_Tp>&);

      ///  Left shift elements of array by corresponding elements of @a v.
      valarray<_Tp>& operator<<=(const valarray<_Tp>&);

      ///  Right shift elements of array by corresponding elements of @a v.
      valarray<_Tp>& operator>>=(const valarray<_Tp>&);

      template<class _Dom>
	valarray<_Tp>& operator*=(const _Expr<_Dom, _Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator/=(const _Expr<_Dom, _Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator%=(const _Expr<_Dom, _Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator+=(const _Expr<_Dom, _Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator-=(const _Expr<_Dom, _Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator^=(const _Expr<_Dom, _Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator|=(const _Expr<_Dom, _Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator&=(const _Expr<_Dom, _Tp>&);
      template<class _Dom>
        valarray<_Tp>& operator<<=(const _Expr<_Dom, _Tp>&);
      template<class _Dom>
	valarray<_Tp>& operator>>=(const _Expr<_Dom, _Tp>&);

      // _lib.valarray.members_ member functions:
      ///  Return the number of elements in array.
      size_t size() const;

      /**
       *  @brief  Return the sum of all elements in the array.
       *
       *  Accumulates the sum of all elements into a Tp using +=.  The order
       *  of adding the elements is unspecified.
       */
      _Tp    sum() const;

      ///  Return the minimum element using operator<().
      _Tp    min() const;	

      ///  Return the maximum element using operator<().
      _Tp    max() const;	

      /**
       *  @brief  Return a shifted array.
       *
       *  A new valarray is constructed as a copy of this array with elements
       *  in shifted positions.  For an element with index i, the new position
       *  is i - n.  The new valarray has the same size as the current one.
       *  New elements without a value are set to 0.  Elements whose new
       *  position is outside the bounds of the array are discarded.
       *
       *  Positive arguments shift toward index 0, discarding elements [0, n).
       *  Negative arguments discard elements from the top of the array.
       *
       *  @param  n  Number of element positions to shift.
       *  @return  New valarray with elements in shifted positions.
       */
      valarray<_Tp> shift (int) const;

      /**
       *  @brief  Return a rotated array.
       *
       *  A new valarray is constructed as a copy of this array with elements
       *  in shifted positions.  For an element with index i, the new position
       *  is (i - n) % size().  The new valarray has the same size as the
       *  current one.  Elements that are shifted beyond the array bounds are
       *  shifted into the other end of the array.  No elements are lost.
       *
       *  Positive arguments shift toward index 0, wrapping around the top.
       *  Negative arguments shift towards the top, wrapping around to 0.
       *
       *  @param  n  Number of element positions to rotate.
       *  @return  New valarray with elements in shifted positions.
       */
      valarray<_Tp> cshift(int) const;

      /**
       *  @brief  Apply a function to the array.
       *
       *  Returns a new valarray with elements assigned to the result of
       *  applying func to the corresponding element of this array.  The new
       *  array has the same size as this one.
       *
       *  @param  func  Function of Tp returning Tp to apply.
       *  @return  New valarray with transformed elements.
       */
      _Expr<_ValFunClos<_ValArray, _Tp>, _Tp> apply(_Tp func(_Tp)) const;

      /**
       *  @brief  Apply a function to the array.
       *
       *  Returns a new valarray with elements assigned to the result of
       *  applying func to the corresponding element of this array.  The new
       *  array has the same size as this one.
       *
       *  @param  func  Function of const Tp& returning Tp to apply.
       *  @return  New valarray with transformed elements.
       */
      _Expr<_RefFunClos<_ValArray, _Tp>, _Tp> apply(_Tp func(const _Tp&)) const;

      /**
       *  @brief  Resize array.
       *
       *  Resize this array to @a size and set all elements to @a c.  All
       *  references and iterators are invalidated.
       *
       *  @param  size  New array size.
       *  @param  c  New value for all elements.
       */
      void resize(size_t __size, _Tp __c = _Tp());

    private:
      size_t _M_size;
      _Tp* __restrict__ _M_data;
      
      friend class _Array<_Tp>;
    };
  
  template<typename _Tp>
    inline const _Tp&
    valarray<_Tp>::operator[](size_t __i) const
    { 
      __glibcxx_requires_subscript(__i);
      return _M_data[__i];
    }

  template<typename _Tp>
    inline _Tp&
    valarray<_Tp>::operator[](size_t __i)
    { 
      __glibcxx_requires_subscript(__i);
      return _M_data[__i];
    }

_GLIBCXX_END_NAMESPACE

#include <bits/valarray_after.h>
#include <bits/slice_array.h>
#include <bits/gslice.h>
#include <bits/gslice_array.h>
#include <bits/mask_array.h>
#include <bits/indirect_array.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray() : _M_size(0), _M_data(0) {}

  template<typename _Tp>
    inline 
    valarray<_Tp>::valarray(size_t __n) 
    : _M_size(__n), _M_data(__valarray_get_storage<_Tp>(__n))
    { std::__valarray_default_construct(_M_data, _M_data + __n); }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const _Tp& __t, size_t __n)
    : _M_size(__n), _M_data(__valarray_get_storage<_Tp>(__n))
    { std::__valarray_fill_construct(_M_data, _M_data + __n, __t); }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const _Tp* __restrict__ __p, size_t __n)
    : _M_size(__n), _M_data(__valarray_get_storage<_Tp>(__n))
    { 
      _GLIBCXX_DEBUG_ASSERT(__p != 0 || __n == 0);
      std::__valarray_copy_construct(__p, __p + __n, _M_data); 
    }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const valarray<_Tp>& __v)
    : _M_size(__v._M_size), _M_data(__valarray_get_storage<_Tp>(__v._M_size))
    { std::__valarray_copy_construct(__v._M_data, __v._M_data + _M_size,
				     _M_data); }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const slice_array<_Tp>& __sa)
    : _M_size(__sa._M_sz), _M_data(__valarray_get_storage<_Tp>(__sa._M_sz))
    {
      std::__valarray_copy_construct
	(__sa._M_array, __sa._M_sz, __sa._M_stride, _Array<_Tp>(_M_data));
    }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const gslice_array<_Tp>& __ga)
    : _M_size(__ga._M_index.size()),
      _M_data(__valarray_get_storage<_Tp>(_M_size))
    {
      std::__valarray_copy_construct
	(__ga._M_array, _Array<size_t>(__ga._M_index),
	 _Array<_Tp>(_M_data), _M_size);
    }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const mask_array<_Tp>& __ma)
    : _M_size(__ma._M_sz), _M_data(__valarray_get_storage<_Tp>(__ma._M_sz))
    {
      std::__valarray_copy_construct
	(__ma._M_array, __ma._M_mask, _Array<_Tp>(_M_data), _M_size);
    }

  template<typename _Tp>
    inline
    valarray<_Tp>::valarray(const indirect_array<_Tp>& __ia)
    : _M_size(__ia._M_sz), _M_data(__valarray_get_storage<_Tp>(__ia._M_sz))
    {
      std::__valarray_copy_construct
	(__ia._M_array, __ia._M_index, _Array<_Tp>(_M_data), _M_size);
    }

  template<typename _Tp> template<class _Dom>
    inline
    valarray<_Tp>::valarray(const _Expr<_Dom, _Tp>& __e)
    : _M_size(__e.size()), _M_data(__valarray_get_storage<_Tp>(_M_size))
    { std::__valarray_copy_construct(__e, _M_size, _Array<_Tp>(_M_data)); }

  template<typename _Tp>
    inline
    valarray<_Tp>::~valarray()
    {
      std::__valarray_destroy_elements(_M_data, _M_data + _M_size);
      std::__valarray_release_memory(_M_data);
    }

  template<typename _Tp>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const valarray<_Tp>& __v)
    {
      _GLIBCXX_DEBUG_ASSERT(_M_size == __v._M_size);
      std::__valarray_copy(__v._M_data, _M_size, _M_data);
      return *this;
    }

  template<typename _Tp>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const _Tp& __t)
    {
      std::__valarray_fill(_M_data, _M_size, __t);
      return *this;
    }

  template<typename _Tp>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const slice_array<_Tp>& __sa)
    {
      _GLIBCXX_DEBUG_ASSERT(_M_size == __sa._M_sz);
      std::__valarray_copy(__sa._M_array, __sa._M_sz,
			   __sa._M_stride, _Array<_Tp>(_M_data));
      return *this;
    }

  template<typename _Tp>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const gslice_array<_Tp>& __ga)
    {
      _GLIBCXX_DEBUG_ASSERT(_M_size == __ga._M_index.size());
      std::__valarray_copy(__ga._M_array, _Array<size_t>(__ga._M_index),
			   _Array<_Tp>(_M_data), _M_size);
      return *this;
    }

  template<typename _Tp>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const mask_array<_Tp>& __ma)
    {
      _GLIBCXX_DEBUG_ASSERT(_M_size == __ma._M_sz);
      std::__valarray_copy(__ma._M_array, __ma._M_mask,
			   _Array<_Tp>(_M_data), _M_size);
      return *this;
    }

  template<typename _Tp>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const indirect_array<_Tp>& __ia)
    {
      _GLIBCXX_DEBUG_ASSERT(_M_size == __ia._M_sz);
      std::__valarray_copy(__ia._M_array, __ia._M_index,
			   _Array<_Tp>(_M_data), _M_size);
      return *this;
    }

  template<typename _Tp> template<class _Dom>
    inline valarray<_Tp>&
    valarray<_Tp>::operator=(const _Expr<_Dom, _Tp>& __e)
    {
      _GLIBCXX_DEBUG_ASSERT(_M_size == __e.size());
      std::__valarray_copy(__e, _M_size, _Array<_Tp>(_M_data));
      return *this;
    }

  template<typename _Tp>
    inline _Expr<_SClos<_ValArray,_Tp>, _Tp>
    valarray<_Tp>::operator[](slice __s) const
    {
      typedef _SClos<_ValArray,_Tp> _Closure;
      return _Expr<_Closure, _Tp>(_Closure (_Array<_Tp>(_M_data), __s));
    }

  template<typename _Tp>
    inline slice_array<_Tp>
    valarray<_Tp>::operator[](slice __s)
    { return slice_array<_Tp>(_Array<_Tp>(_M_data), __s); }

  template<typename _Tp>
    inline _Expr<_GClos<_ValArray,_Tp>, _Tp>
    valarray<_Tp>::operator[](const gslice& __gs) const
    {
      typedef _GClos<_ValArray,_Tp> _Closure;
      return _Expr<_Closure, _Tp>
	(_Closure(_Array<_Tp>(_M_data), __gs._M_index->_M_index));
    }

  template<typename _Tp>
    inline gslice_array<_Tp>
    valarray<_Tp>::operator[](const gslice& __gs)
    {
      return gslice_array<_Tp>
	(_Array<_Tp>(_M_data), __gs._M_index->_M_index);
    }

  template<typename _Tp>
    inline valarray<_Tp>
    valarray<_Tp>::operator[](const valarray<bool>& __m) const
    {
      size_t __s = 0;
      size_t __e = __m.size();
      for (size_t __i=0; __i<__e; ++__i)
	if (__m[__i]) ++__s;
      return valarray<_Tp>(mask_array<_Tp>(_Array<_Tp>(_M_data), __s,
					   _Array<bool> (__m)));
    }

  template<typename _Tp>
    inline mask_array<_Tp>
    valarray<_Tp>::operator[](const valarray<bool>& __m)
    {
      size_t __s = 0;
      size_t __e = __m.size();
      for (size_t __i=0; __i<__e; ++__i)
	if (__m[__i]) ++__s;
      return mask_array<_Tp>(_Array<_Tp>(_M_data), __s, _Array<bool>(__m));
    }

  template<typename _Tp>
    inline _Expr<_IClos<_ValArray,_Tp>, _Tp>
    valarray<_Tp>::operator[](const valarray<size_t>& __i) const
    {
      typedef _IClos<_ValArray,_Tp> _Closure;
      return _Expr<_Closure, _Tp>(_Closure(*this, __i));
    }

  template<typename _Tp>
    inline indirect_array<_Tp>
    valarray<_Tp>::operator[](const valarray<size_t>& __i)
    {
      return indirect_array<_Tp>(_Array<_Tp>(_M_data), __i.size(),
				 _Array<size_t>(__i));
    }

  template<class _Tp>
    inline size_t 
    valarray<_Tp>::size() const
    { return _M_size; }

  template<class _Tp>
    inline _Tp
    valarray<_Tp>::sum() const
    {
      _GLIBCXX_DEBUG_ASSERT(_M_size > 0);
      return std::__valarray_sum(_M_data, _M_data + _M_size);
    }

  template<class _Tp>
     inline valarray<_Tp>
     valarray<_Tp>::shift(int __n) const
     {
       valarray<_Tp> __ret;

       if (_M_size == 0)
	 return __ret;

       _Tp* __restrict__ __tmp_M_data =
	 std::__valarray_get_storage<_Tp>(_M_size);

       if (__n == 0)
	 std::__valarray_copy_construct(_M_data,
					_M_data + _M_size, __tmp_M_data);
       else if (__n > 0)      // shift left
	 {
	   if (size_t(__n) > _M_size)
	     __n = _M_size;

	   std::__valarray_copy_construct(_M_data + __n,
					  _M_data + _M_size, __tmp_M_data);
	   std::__valarray_default_construct(__tmp_M_data + _M_size - __n,
					     __tmp_M_data + _M_size);
	 }
       else                   // shift right
	 {
	   if (size_t(-__n) > _M_size)
	     __n = -_M_size;

	   std::__valarray_copy_construct(_M_data, _M_data + _M_size + __n,
					  __tmp_M_data - __n);
	   std::__valarray_default_construct(__tmp_M_data,
					     __tmp_M_data - __n);
	 }

       __ret._M_size = _M_size;
       __ret._M_data = __tmp_M_data;
       return __ret;
     }

  template<class _Tp>
     inline valarray<_Tp>
     valarray<_Tp>::cshift(int __n) const
     {
       valarray<_Tp> __ret;

       if (_M_size == 0)
	 return __ret;

       _Tp* __restrict__ __tmp_M_data =
	 std::__valarray_get_storage<_Tp>(_M_size);

       if (__n == 0)
	 std::__valarray_copy_construct(_M_data,
					_M_data + _M_size, __tmp_M_data);
       else if (__n > 0)      // cshift left
	 {
	   if (size_t(__n) > _M_size)
	     __n = __n % _M_size;

	   std::__valarray_copy_construct(_M_data, _M_data + __n,
					  __tmp_M_data + _M_size - __n);
	   std::__valarray_copy_construct(_M_data + __n, _M_data + _M_size,
					  __tmp_M_data);
	 }
       else                   // cshift right
	 {
	   if (size_t(-__n) > _M_size)
	     __n = -(size_t(-__n) % _M_size);

	   std::__valarray_copy_construct(_M_data + _M_size + __n,
					  _M_data + _M_size, __tmp_M_data);
	   std::__valarray_copy_construct(_M_data, _M_data + _M_size + __n,
					  __tmp_M_data - __n);
	 }

       __ret._M_size = _M_size;
       __ret._M_data = __tmp_M_data;
       return __ret;
     }

  template<class _Tp>
    inline void
    valarray<_Tp>::resize(size_t __n, _Tp __c)
    {
      // This complication is so to make valarray<valarray<T> > work
      // even though it is not required by the standard.  Nobody should
      // be saying valarray<valarray<T> > anyway.  See the specs.
      std::__valarray_destroy_elements(_M_data, _M_data + _M_size);
      if (_M_size != __n)
	{
	  std::__valarray_release_memory(_M_data);
	  _M_size = __n;
	  _M_data = __valarray_get_storage<_Tp>(__n);
	}
      std::__valarray_fill_construct(_M_data, _M_data + __n, __c);
    }
    
  template<typename _Tp>
    inline _Tp
    valarray<_Tp>::min() const
    {
      _GLIBCXX_DEBUG_ASSERT(_M_size > 0);
      return *std::min_element(_M_data, _M_data+_M_size);
    }

  template<typename _Tp>
    inline _Tp
    valarray<_Tp>::max() const
    {
      _GLIBCXX_DEBUG_ASSERT(_M_size > 0);
      return *std::max_element(_M_data, _M_data+_M_size);
    }
  
  template<class _Tp>
    inline _Expr<_ValFunClos<_ValArray, _Tp>, _Tp>
    valarray<_Tp>::apply(_Tp func(_Tp)) const
    {
      typedef _ValFunClos<_ValArray, _Tp> _Closure;
      return _Expr<_Closure, _Tp>(_Closure(*this, func));
    }

  template<class _Tp>
    inline _Expr<_RefFunClos<_ValArray, _Tp>, _Tp>
    valarray<_Tp>::apply(_Tp func(const _Tp &)) const
    {
      typedef _RefFunClos<_ValArray, _Tp> _Closure;
      return _Expr<_Closure, _Tp>(_Closure(*this, func));
    }

#define _DEFINE_VALARRAY_UNARY_OPERATOR(_Op, _Name)                     \
  template<typename _Tp>						\
    inline typename valarray<_Tp>::template _UnaryOp<_Name>::_Rt      	\
    valarray<_Tp>::operator _Op() const					\
    {									\
      typedef _UnClos<_Name, _ValArray, _Tp> _Closure;	                \
      typedef typename __fun<_Name, _Tp>::result_type _Rt;              \
      return _Expr<_Closure, _Rt>(_Closure(*this));			\
    }

    _DEFINE_VALARRAY_UNARY_OPERATOR(+, __unary_plus)
    _DEFINE_VALARRAY_UNARY_OPERATOR(-, __negate)
    _DEFINE_VALARRAY_UNARY_OPERATOR(~, __bitwise_not)
    _DEFINE_VALARRAY_UNARY_OPERATOR (!, __logical_not)

#undef _DEFINE_VALARRAY_UNARY_OPERATOR

#define _DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(_Op, _Name)               \
  template<class _Tp>							\
    inline valarray<_Tp>&						\
    valarray<_Tp>::operator _Op##=(const _Tp &__t)			\
    {									\
      _Array_augmented_##_Name(_Array<_Tp>(_M_data), _M_size, __t);	\
      return *this;							\
    }									\
									\
  template<class _Tp>							\
    inline valarray<_Tp>&						\
    valarray<_Tp>::operator _Op##=(const valarray<_Tp> &__v)		\
    {									\
      _GLIBCXX_DEBUG_ASSERT(_M_size == __v._M_size);                    \
      _Array_augmented_##_Name(_Array<_Tp>(_M_data), _M_size, 		\
			       _Array<_Tp>(__v._M_data));		\
      return *this;							\
    }

_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(+, __plus)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(-, __minus)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(*, __multiplies)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(/, __divides)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(%, __modulus)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(^, __bitwise_xor)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(&, __bitwise_and)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(|, __bitwise_or)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(<<, __shift_left)
_DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT(>>, __shift_right)

#undef _DEFINE_VALARRAY_AUGMENTED_ASSIGNMENT

#define _DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(_Op, _Name)          \
  template<class _Tp> template<class _Dom>				\
    inline valarray<_Tp>&						\
    valarray<_Tp>::operator _Op##=(const _Expr<_Dom, _Tp>& __e)		\
    {									\
      _Array_augmented_##_Name(_Array<_Tp>(_M_data), __e, _M_size);	\
      return *this;							\
    }

_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(+, __plus)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(-, __minus)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(*, __multiplies)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(/, __divides)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(%, __modulus)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(^, __bitwise_xor)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(&, __bitwise_and)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(|, __bitwise_or)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(<<, __shift_left)
_DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT(>>, __shift_right)

#undef _DEFINE_VALARRAY_EXPR_AUGMENTED_ASSIGNMENT
    

#define _DEFINE_BINARY_OPERATOR(_Op, _Name)				\
  template<typename _Tp>						\
    inline _Expr<_BinClos<_Name, _ValArray, _ValArray, _Tp, _Tp>,       \
                 typename __fun<_Name, _Tp>::result_type>               \
    operator _Op(const valarray<_Tp>& __v, const valarray<_Tp>& __w)	\
    {									\
      _GLIBCXX_DEBUG_ASSERT(__v.size() == __w.size());                  \
      typedef _BinClos<_Name, _ValArray, _ValArray, _Tp, _Tp> _Closure; \
      typedef typename __fun<_Name, _Tp>::result_type _Rt;              \
      return _Expr<_Closure, _Rt>(_Closure(__v, __w));                  \
    }									\
									\
  template<typename _Tp>						\
    inline _Expr<_BinClos<_Name, _ValArray,_Constant, _Tp, _Tp>,        \
                 typename __fun<_Name, _Tp>::result_type>               \
    operator _Op(const valarray<_Tp>& __v, const _Tp& __t)		\
    {									\
      typedef _BinClos<_Name, _ValArray, _Constant, _Tp, _Tp> _Closure;	\
      typedef typename __fun<_Name, _Tp>::result_type _Rt;              \
      return _Expr<_Closure, _Rt>(_Closure(__v, __t));	                \
    }									\
									\
  template<typename _Tp>						\
    inline _Expr<_BinClos<_Name, _Constant, _ValArray, _Tp, _Tp>,       \
                 typename __fun<_Name, _Tp>::result_type>               \
    operator _Op(const _Tp& __t, const valarray<_Tp>& __v)		\
    {									\
      typedef _BinClos<_Name, _Constant, _ValArray, _Tp, _Tp> _Closure; \
      typedef typename __fun<_Name, _Tp>::result_type _Rt;              \
      return _Expr<_Closure, _Rt>(_Closure(__t, __v));        	        \
    }

_DEFINE_BINARY_OPERATOR(+, __plus)
_DEFINE_BINARY_OPERATOR(-, __minus)
_DEFINE_BINARY_OPERATOR(*, __multiplies)
_DEFINE_BINARY_OPERATOR(/, __divides)
_DEFINE_BINARY_OPERATOR(%, __modulus)
_DEFINE_BINARY_OPERATOR(^, __bitwise_xor)
_DEFINE_BINARY_OPERATOR(&, __bitwise_and)
_DEFINE_BINARY_OPERATOR(|, __bitwise_or)
_DEFINE_BINARY_OPERATOR(<<, __shift_left)
_DEFINE_BINARY_OPERATOR(>>, __shift_right)
_DEFINE_BINARY_OPERATOR(&&, __logical_and)
_DEFINE_BINARY_OPERATOR(||, __logical_or)
_DEFINE_BINARY_OPERATOR(==, __equal_to)
_DEFINE_BINARY_OPERATOR(!=, __not_equal_to)
_DEFINE_BINARY_OPERATOR(<, __less)
_DEFINE_BINARY_OPERATOR(>, __greater)
_DEFINE_BINARY_OPERATOR(<=, __less_equal)
_DEFINE_BINARY_OPERATOR(>=, __greater_equal)

#undef _DEFINE_BINARY_OPERATOR

_GLIBCXX_END_NAMESPACE

#endif /* _GLIBCXX_VALARRAY */
