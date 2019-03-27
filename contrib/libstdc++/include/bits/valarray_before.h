// The template and inlines for the -*- C++ -*- internal _Meta class.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
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

/** @file valarray_before.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

// Written by Gabriel Dos Reis <Gabriel.Dos-Reis@cmla.ens-cachan.fr>

#ifndef _VALARRAY_BEFORE_H
#define _VALARRAY_BEFORE_H 1

#pragma GCC system_header

#include <bits/slice_array.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  //
  // Implementing a loosened valarray return value is tricky.
  // First we need to meet 26.3.1/3: we should not add more than
  // two levels of template nesting. Therefore we resort to template
  // template to "flatten" loosened return value types.
  // At some point we use partial specialization to remove one level
  // template nesting due to _Expr<>
  //

  // This class is NOT defined. It doesn't need to.
  template<typename _Tp1, typename _Tp2> class _Constant;

  // Implementations of unary functions applied to valarray<>s.
  // I use hard-coded object functions here instead of a generic
  // approach like pointers to function:
  //    1) correctness: some functions take references, others values.
  //       we can't deduce the correct type afterwards.
  //    2) efficiency -- object functions can be easily inlined
  //    3) be Koenig-lookup-friendly

  struct __abs
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return abs(__t); }
  };

  struct __cos
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return cos(__t); }
  };

  struct __acos
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return acos(__t); }
  };

  struct __cosh
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return cosh(__t); }
  };

  struct __sin
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return sin(__t); }
  };

  struct __asin
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return asin(__t); }
  };

  struct __sinh
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return sinh(__t); }
  };

  struct __tan
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return tan(__t); }
  };

  struct __atan
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return atan(__t); }
  };

  struct __tanh
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return tanh(__t); }
  };

  struct __exp
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return exp(__t); }
  };

  struct __log
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return log(__t); }
  };

  struct __log10
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return log10(__t); }
  };

  struct __sqrt
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return sqrt(__t); }
  };

  // In the past, we used to tailor operator applications semantics
  // to the specialization of standard function objects (i.e. plus<>, etc.)
  // That is incorrect.  Therefore we provide our own surrogates.

  struct __unary_plus
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return +__t; }
  };

  struct __negate
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return -__t; }
  };

  struct __bitwise_not
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __t) const
      { return ~__t; }
  };

  struct __plus
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __x, const _Tp& __y) const
      { return __x + __y; }
  };

  struct __minus
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __x, const _Tp& __y) const
      { return __x - __y; }
  };

  struct __multiplies
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __x, const _Tp& __y) const
      { return __x * __y; }
  };

  struct __divides
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __x, const _Tp& __y) const
      { return __x / __y; }
  };

  struct __modulus
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __x, const _Tp& __y) const
      { return __x % __y; }
  };

  struct __bitwise_xor
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __x, const _Tp& __y) const
      { return __x ^ __y; }
  };

  struct __bitwise_and
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __x, const _Tp& __y) const
      { return __x & __y; }
  };

  struct __bitwise_or
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __x, const _Tp& __y) const
      { return __x | __y; }
  };

  struct __shift_left
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __x, const _Tp& __y) const
      { return __x << __y; }
  };

  struct __shift_right
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __x, const _Tp& __y) const
      { return __x >> __y; }
  };

  struct __logical_and
  {
    template<typename _Tp>
      bool operator()(const _Tp& __x, const _Tp& __y) const
      { return __x && __y; }
  };

  struct __logical_or
  {
    template<typename _Tp>
      bool operator()(const _Tp& __x, const _Tp& __y) const
      { return __x || __y; }
  };

  struct __logical_not
  {
    template<typename _Tp>
      bool operator()(const _Tp& __x) const { return !__x; }
  };

  struct __equal_to
  {
    template<typename _Tp>
      bool operator()(const _Tp& __x, const _Tp& __y) const
      { return __x == __y; }
  };

  struct __not_equal_to
  {
    template<typename _Tp>
      bool operator()(const _Tp& __x, const _Tp& __y) const
      { return __x != __y; }
  };

  struct __less
  {
    template<typename _Tp>
      bool operator()(const _Tp& __x, const _Tp& __y) const
      { return __x < __y; }
  };

  struct __greater
  {
    template<typename _Tp>
      bool operator()(const _Tp& __x, const _Tp& __y) const
      { return __x > __y; }
  };

  struct __less_equal
  {
    template<typename _Tp>
      bool operator()(const _Tp& __x, const _Tp& __y) const
      { return __x <= __y; }
  };

  struct __greater_equal
  {
    template<typename _Tp>
      bool operator()(const _Tp& __x, const _Tp& __y) const
      { return __x >= __y; }
  };

  // The few binary functions we miss.
  struct __atan2
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __x, const _Tp& __y) const
      { return atan2(__x, __y); }
  };

  struct __pow
  {
    template<typename _Tp>
      _Tp operator()(const _Tp& __x, const _Tp& __y) const
      { return pow(__x, __y); }
  };


  // We need these bits in order to recover the return type of
  // some functions/operators now that we're no longer using
  // function templates.
  template<typename, typename _Tp>
    struct __fun
    {
      typedef _Tp result_type;
    };

  // several specializations for relational operators.
  template<typename _Tp>
    struct __fun<__logical_not, _Tp>
    {
      typedef bool result_type;
    };

  template<typename _Tp>
    struct __fun<__logical_and, _Tp>
    {
      typedef bool result_type;
    };

  template<typename _Tp>
    struct __fun<__logical_or, _Tp>
    {
      typedef bool result_type;
    };

  template<typename _Tp>
    struct __fun<__less, _Tp>
    {
      typedef bool result_type;
    };

  template<typename _Tp>
    struct __fun<__greater, _Tp>
    {
      typedef bool result_type;
    };

  template<typename _Tp>
    struct __fun<__less_equal, _Tp>
    {
      typedef bool result_type;
    };

  template<typename _Tp>
    struct __fun<__greater_equal, _Tp>
    {
      typedef bool result_type;
    };

  template<typename _Tp>
    struct __fun<__equal_to, _Tp>
    {
      typedef bool result_type;
    };

  template<typename _Tp>
    struct __fun<__not_equal_to, _Tp>
    {
      typedef bool result_type;
    };

  //
  // Apply function taking a value/const reference closure
  //

  template<typename _Dom, typename _Arg>
    class _FunBase
    {
    public:
      typedef typename _Dom::value_type value_type;

      _FunBase(const _Dom& __e, value_type __f(_Arg))
      : _M_expr(__e), _M_func(__f) {}

      value_type operator[](size_t __i) const
      { return _M_func (_M_expr[__i]); }

      size_t size() const { return _M_expr.size ();}

    private:
      const _Dom& _M_expr;
      value_type (*_M_func)(_Arg);
    };

  template<class _Dom>
    struct _ValFunClos<_Expr,_Dom> : _FunBase<_Dom, typename _Dom::value_type>
    {
      typedef _FunBase<_Dom, typename _Dom::value_type> _Base;
      typedef typename _Base::value_type value_type;
      typedef value_type _Tp;

      _ValFunClos(const _Dom& __e, _Tp __f(_Tp)) : _Base(__e, __f) {}
    };

  template<typename _Tp>
    struct _ValFunClos<_ValArray,_Tp> : _FunBase<valarray<_Tp>, _Tp>
    {
      typedef _FunBase<valarray<_Tp>, _Tp> _Base;
      typedef _Tp value_type;

      _ValFunClos(const valarray<_Tp>& __v, _Tp __f(_Tp)) : _Base(__v, __f) {}
    };

  template<class _Dom>
    struct _RefFunClos<_Expr, _Dom>
    : _FunBase<_Dom, const typename _Dom::value_type&>
    {
      typedef _FunBase<_Dom, const typename _Dom::value_type&> _Base;
      typedef typename _Base::value_type value_type;
      typedef value_type _Tp;

      _RefFunClos(const _Dom& __e, _Tp __f(const _Tp&))
      : _Base(__e, __f) {}
    };

  template<typename _Tp>
    struct _RefFunClos<_ValArray, _Tp>
    : _FunBase<valarray<_Tp>, const _Tp&>
    {
      typedef _FunBase<valarray<_Tp>, const _Tp&> _Base;
      typedef _Tp value_type;

      _RefFunClos(const valarray<_Tp>& __v, _Tp __f(const _Tp&))
      : _Base(__v, __f) {}
    };

  //
  // Unary expression closure.
  //

  template<class _Oper, class _Arg>
    class _UnBase
    {
    public:
      typedef typename _Arg::value_type _Vt;
      typedef typename __fun<_Oper, _Vt>::result_type value_type;

      _UnBase(const _Arg& __e) : _M_expr(__e) {}

      value_type operator[](size_t __i) const
      { return _Oper()(_M_expr[__i]); }

      size_t size() const { return _M_expr.size(); }
      
    private:
      const _Arg& _M_expr;
    };

  template<class _Oper, class _Dom>
    struct _UnClos<_Oper, _Expr, _Dom>
    : _UnBase<_Oper, _Dom>
    {
      typedef _Dom _Arg;
      typedef _UnBase<_Oper, _Dom> _Base;
      typedef typename _Base::value_type value_type;

      _UnClos(const _Arg& __e) : _Base(__e) {}
    };

  template<class _Oper, typename _Tp>
    struct _UnClos<_Oper, _ValArray, _Tp>
    : _UnBase<_Oper, valarray<_Tp> >
    {
      typedef valarray<_Tp> _Arg;
      typedef _UnBase<_Oper, valarray<_Tp> > _Base;
      typedef typename _Base::value_type value_type;

      _UnClos(const _Arg& __e) : _Base(__e) {}
    };


  //
  // Binary expression closure.
  //

  template<class _Oper, class _FirstArg, class _SecondArg>
    class _BinBase
    {
    public:
      typedef typename _FirstArg::value_type _Vt;
      typedef typename __fun<_Oper, _Vt>::result_type value_type;

      _BinBase(const _FirstArg& __e1, const _SecondArg& __e2)
      : _M_expr1(__e1), _M_expr2(__e2) {}

      value_type operator[](size_t __i) const
      { return _Oper()(_M_expr1[__i], _M_expr2[__i]); }

      size_t size() const { return _M_expr1.size(); }

    private:
      const _FirstArg& _M_expr1;
      const _SecondArg& _M_expr2;
    };


  template<class _Oper, class _Clos>
    class _BinBase2
    {
    public:
      typedef typename _Clos::value_type _Vt;
      typedef typename __fun<_Oper, _Vt>::result_type value_type;

      _BinBase2(const _Clos& __e, const _Vt& __t)
      : _M_expr1(__e), _M_expr2(__t) {}

      value_type operator[](size_t __i) const
      { return _Oper()(_M_expr1[__i], _M_expr2); }

      size_t size() const { return _M_expr1.size(); }

    private:
      const _Clos& _M_expr1;
      const _Vt& _M_expr2;
    };

  template<class _Oper, class _Clos>
    class _BinBase1
    {
    public:
      typedef typename _Clos::value_type _Vt;
      typedef typename __fun<_Oper, _Vt>::result_type value_type;

      _BinBase1(const _Vt& __t, const _Clos& __e)
      : _M_expr1(__t), _M_expr2(__e) {}

      value_type operator[](size_t __i) const
      { return _Oper()(_M_expr1, _M_expr2[__i]); }

      size_t size() const { return _M_expr2.size(); }

    private:
      const _Vt& _M_expr1;
      const _Clos& _M_expr2;
    };

  template<class _Oper, class _Dom1, class _Dom2>
    struct _BinClos<_Oper, _Expr, _Expr, _Dom1, _Dom2>
    : _BinBase<_Oper, _Dom1, _Dom2>
    {
      typedef _BinBase<_Oper, _Dom1, _Dom2> _Base;
      typedef typename _Base::value_type value_type;

      _BinClos(const _Dom1& __e1, const _Dom2& __e2) : _Base(__e1, __e2) {}
    };

  template<class _Oper, typename _Tp>
    struct _BinClos<_Oper,_ValArray, _ValArray, _Tp, _Tp>
    : _BinBase<_Oper, valarray<_Tp>, valarray<_Tp> >
    {
      typedef _BinBase<_Oper, valarray<_Tp>, valarray<_Tp> > _Base;
      typedef typename _Base::value_type value_type;

      _BinClos(const valarray<_Tp>& __v, const valarray<_Tp>& __w)
      : _Base(__v, __w) {}
    };

  template<class _Oper, class _Dom>
    struct _BinClos<_Oper, _Expr, _ValArray, _Dom, typename _Dom::value_type>
    : _BinBase<_Oper, _Dom, valarray<typename _Dom::value_type> >
    {
      typedef typename _Dom::value_type _Tp;
      typedef _BinBase<_Oper,_Dom,valarray<_Tp> > _Base;
      typedef typename _Base::value_type value_type;

      _BinClos(const _Dom& __e1, const valarray<_Tp>& __e2)
      : _Base(__e1, __e2) {}
    };

  template<class _Oper, class _Dom>
    struct _BinClos<_Oper, _ValArray, _Expr, typename _Dom::value_type, _Dom>
    : _BinBase<_Oper, valarray<typename _Dom::value_type>,_Dom>
    {
      typedef typename _Dom::value_type _Tp;
      typedef _BinBase<_Oper, valarray<_Tp>, _Dom> _Base;
      typedef typename _Base::value_type value_type;

      _BinClos(const valarray<_Tp>& __e1, const _Dom& __e2)
      : _Base(__e1, __e2) {}
    };

  template<class _Oper, class _Dom>
    struct _BinClos<_Oper, _Expr, _Constant, _Dom, typename _Dom::value_type>
    : _BinBase2<_Oper, _Dom>
    {
      typedef typename _Dom::value_type _Tp;
      typedef _BinBase2<_Oper,_Dom> _Base;
      typedef typename _Base::value_type value_type;

      _BinClos(const _Dom& __e1, const _Tp& __e2) : _Base(__e1, __e2) {}
    };

  template<class _Oper, class _Dom>
    struct _BinClos<_Oper, _Constant, _Expr, typename _Dom::value_type, _Dom>
    : _BinBase1<_Oper, _Dom>
    {
      typedef typename _Dom::value_type _Tp;
      typedef _BinBase1<_Oper, _Dom> _Base;
      typedef typename _Base::value_type value_type;

      _BinClos(const _Tp& __e1, const _Dom& __e2) : _Base(__e1, __e2) {}
    };

  template<class _Oper, typename _Tp>
    struct _BinClos<_Oper, _ValArray, _Constant, _Tp, _Tp>
    : _BinBase2<_Oper, valarray<_Tp> >
    {
      typedef _BinBase2<_Oper,valarray<_Tp> > _Base;
      typedef typename _Base::value_type value_type;

      _BinClos(const valarray<_Tp>& __v, const _Tp& __t) : _Base(__v, __t) {}
    };

  template<class _Oper, typename _Tp>
    struct _BinClos<_Oper, _Constant, _ValArray, _Tp, _Tp>
    : _BinBase1<_Oper, valarray<_Tp> >
    {
      typedef _BinBase1<_Oper, valarray<_Tp> > _Base;
      typedef typename _Base::value_type value_type;

      _BinClos(const _Tp& __t, const valarray<_Tp>& __v) : _Base(__t, __v) {}
    };

    //
    // slice_array closure.
    //
  template<typename _Dom> 
    class _SBase
    {
    public:
      typedef typename _Dom::value_type value_type;
      
      _SBase (const _Dom& __e, const slice& __s)
      : _M_expr (__e), _M_slice (__s) {}
        
      value_type
      operator[] (size_t __i) const
      { return _M_expr[_M_slice.start () + __i * _M_slice.stride ()]; }
        
      size_t
      size() const
      { return _M_slice.size (); }

    private:
      const _Dom& _M_expr;
      const slice& _M_slice;
    };

  template<typename _Tp>
    class _SBase<_Array<_Tp> >
    {
    public:
      typedef _Tp value_type;
      
      _SBase (_Array<_Tp> __a, const slice& __s)
      : _M_array (__a._M_data+__s.start()), _M_size (__s.size()),
	_M_stride (__s.stride()) {}
        
      value_type
      operator[] (size_t __i) const
      { return _M_array._M_data[__i * _M_stride]; }
      
      size_t
      size() const
      { return _M_size; }

    private:
      const _Array<_Tp> _M_array;
      const size_t _M_size;
      const size_t _M_stride;
    };

  template<class _Dom>
    struct _SClos<_Expr, _Dom>
    : _SBase<_Dom>
    {
      typedef _SBase<_Dom> _Base;
      typedef typename _Base::value_type value_type;
      
      _SClos (const _Dom& __e, const slice& __s) : _Base (__e, __s) {}
    };

  template<typename _Tp>
    struct _SClos<_ValArray, _Tp>
    : _SBase<_Array<_Tp> >
    {
      typedef  _SBase<_Array<_Tp> > _Base;
      typedef _Tp value_type;
      
      _SClos (_Array<_Tp> __a, const slice& __s) : _Base (__a, __s) {}
    };

_GLIBCXX_END_NAMESPACE

#endif /* _CPP_VALARRAY_BEFORE_H */
