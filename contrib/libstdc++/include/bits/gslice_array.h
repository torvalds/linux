// The template and inlines for the -*- C++ -*- gslice_array class.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2004, 2005
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

/** @file gslice_array.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

// Written by Gabriel Dos Reis <Gabriel.Dos-Reis@DPTMaths.ENS-Cachan.Fr>

#ifndef _GSLICE_ARRAY_H
#define _GSLICE_ARRAY_H 1

#pragma GCC system_header

_GLIBCXX_BEGIN_NAMESPACE(std)

  /**
   *  @brief  Reference to multi-dimensional subset of an array.
   *
   *  A gslice_array is a reference to the actual elements of an array
   *  specified by a gslice.  The way to get a gslice_array is to call
   *  operator[](gslice) on a valarray.  The returned gslice_array then
   *  permits carrying operations out on the referenced subset of elements in
   *  the original valarray.  For example, operator+=(valarray) will add
   *  values to the subset of elements in the underlying valarray this
   *  gslice_array refers to.
   *
   *  @param  Tp  Element type.
   */
  template<typename _Tp>
    class gslice_array
    {
    public:
      typedef _Tp value_type;

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 253. valarray helper functions are almost entirely useless

      ///  Copy constructor.  Both slices refer to the same underlying array.
      gslice_array(const gslice_array&);

      ///  Assignment operator.  Assigns slice elements to corresponding
      ///  elements of @a a.
      gslice_array& operator=(const gslice_array&);

      ///  Assign slice elements to corresponding elements of @a v.
      void operator=(const valarray<_Tp>&) const;
      ///  Multiply slice elements by corresponding elements of @a v.
      void operator*=(const valarray<_Tp>&) const;
      ///  Divide slice elements by corresponding elements of @a v.
      void operator/=(const valarray<_Tp>&) const;
      ///  Modulo slice elements by corresponding elements of @a v.
      void operator%=(const valarray<_Tp>&) const;
      ///  Add corresponding elements of @a v to slice elements.
      void operator+=(const valarray<_Tp>&) const;
      ///  Subtract corresponding elements of @a v from slice elements.
      void operator-=(const valarray<_Tp>&) const;
      ///  Logical xor slice elements with corresponding elements of @a v.
      void operator^=(const valarray<_Tp>&) const;
      ///  Logical and slice elements with corresponding elements of @a v.
      void operator&=(const valarray<_Tp>&) const;
      ///  Logical or slice elements with corresponding elements of @a v.
      void operator|=(const valarray<_Tp>&) const;
      ///  Left shift slice elements by corresponding elements of @a v.
      void operator<<=(const valarray<_Tp>&) const;
      ///  Right shift slice elements by corresponding elements of @a v.
      void operator>>=(const valarray<_Tp>&) const;
      ///  Assign all slice elements to @a t.
      void operator=(const _Tp&) const;

      template<class _Dom>
        void operator=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
        void operator*=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
        void operator/=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
        void operator%=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
        void operator+=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
        void operator-=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
        void operator^=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
        void operator&=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
        void operator|=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
        void operator<<=(const _Expr<_Dom, _Tp>&) const;
      template<class _Dom>
        void operator>>=(const _Expr<_Dom, _Tp>&) const;

    private:
      _Array<_Tp>    _M_array;
      const valarray<size_t>& _M_index;

      friend class valarray<_Tp>;

      gslice_array(_Array<_Tp>, const valarray<size_t>&);

      // not implemented
      gslice_array();
    };

  template<typename _Tp>
    inline
    gslice_array<_Tp>::gslice_array(_Array<_Tp> __a,
				    const valarray<size_t>& __i)
    : _M_array(__a), _M_index(__i) {}

  template<typename _Tp>
    inline
    gslice_array<_Tp>::gslice_array(const gslice_array<_Tp>& __a)
    : _M_array(__a._M_array), _M_index(__a._M_index) {}

  template<typename _Tp>
    inline gslice_array<_Tp>&
    gslice_array<_Tp>::operator=(const gslice_array<_Tp>& __a)
    {
      std::__valarray_copy(_Array<_Tp>(__a._M_array),
			   _Array<size_t>(__a._M_index), _M_index.size(),
			   _M_array, _Array<size_t>(_M_index));
      return *this;
    }

  template<typename _Tp>
    inline void
    gslice_array<_Tp>::operator=(const _Tp& __t) const
    {
      std::__valarray_fill(_M_array, _Array<size_t>(_M_index),
			   _M_index.size(), __t);
    }

  template<typename _Tp>
    inline void
    gslice_array<_Tp>::operator=(const valarray<_Tp>& __v) const
    {
      std::__valarray_copy(_Array<_Tp>(__v), __v.size(),
			   _M_array, _Array<size_t>(_M_index));
    }

  template<typename _Tp>
    template<class _Dom>
      inline void
      gslice_array<_Tp>::operator=(const _Expr<_Dom, _Tp>& __e) const
      {
	std::__valarray_copy (__e, _M_index.size(), _M_array,
			      _Array<size_t>(_M_index));
      }

#undef _DEFINE_VALARRAY_OPERATOR
#define _DEFINE_VALARRAY_OPERATOR(_Op, _Name)				\
  template<typename _Tp>						\
    inline void								\
    gslice_array<_Tp>::operator _Op##=(const valarray<_Tp>& __v) const	\
    {									\
      _Array_augmented_##_Name(_M_array, _Array<size_t>(_M_index),	\
			       _Array<_Tp>(__v), __v.size());		\
    }									\
									\
  template<typename _Tp>                                                \
    template<class _Dom>				                \
      inline void							\
      gslice_array<_Tp>::operator _Op##= (const _Expr<_Dom, _Tp>& __e) const\
      {									\
	_Array_augmented_##_Name(_M_array, _Array<size_t>(_M_index), __e,\
				 _M_index.size());			\
      }

_DEFINE_VALARRAY_OPERATOR(*, __multiplies)
_DEFINE_VALARRAY_OPERATOR(/, __divides)
_DEFINE_VALARRAY_OPERATOR(%, __modulus)
_DEFINE_VALARRAY_OPERATOR(+, __plus)
_DEFINE_VALARRAY_OPERATOR(-, __minus)
_DEFINE_VALARRAY_OPERATOR(^, __bitwise_xor)
_DEFINE_VALARRAY_OPERATOR(&, __bitwise_and)
_DEFINE_VALARRAY_OPERATOR(|, __bitwise_or)
_DEFINE_VALARRAY_OPERATOR(<<, __shift_left)
_DEFINE_VALARRAY_OPERATOR(>>, __shift_right)

#undef _DEFINE_VALARRAY_OPERATOR

_GLIBCXX_END_NAMESPACE

#endif /* _GSLICE_ARRAY_H */
