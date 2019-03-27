// class template tuple -*- C++ -*-

// Copyright (C) 2004, 2005 Free Software Foundation, Inc.
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

/** @file tr1/tuple_defs.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _GLIBCXX_REPEAT_HEADER
#  define _GLIBCXX_REPEAT_HEADER "tuple_defs.h"
#  include "repeat.h"
#  undef _GLIBCXX_REPEAT_HEADER
#endif

#ifdef _GLIBCXX_LAST_INCLUDE
// Chris Jefferson <chris@bubblescope.net>
   template<_GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS> class tuple;

   // Returns a const reference to the ith element of a tuple.
   // Any const or non-const ref elements are returned with their original type.
   template<int __i, _GLIBCXX_TEMPLATE_PARAMS>
   typename __add_ref<typename tuple_element<__i, tuple<_GLIBCXX_TEMPLATE_ARGS> >::type>::type
   get(tuple<_GLIBCXX_TEMPLATE_ARGS>& __t)
   {
     return __get_helper<__i, tuple<_GLIBCXX_TEMPLATE_ARGS> >::get_value(__t);
   }

   template<int __i, _GLIBCXX_TEMPLATE_PARAMS>
   typename __add_c_ref<typename tuple_element<__i, tuple<_GLIBCXX_TEMPLATE_ARGS> >::type>::type
   get(const tuple<_GLIBCXX_TEMPLATE_ARGS>& __t)
   {
     return __get_helper<__i, tuple<_GLIBCXX_TEMPLATE_ARGS> >::get_value(__t);
   }

 template<_GLIBCXX_TEMPLATE_PARAMS, _GLIBCXX_TEMPLATE_PARAMS_U>
 bool
 operator==(const tuple<_GLIBCXX_TEMPLATE_ARGS>& __t,
            const tuple<_GLIBCXX_TEMPLATE_ARGS_U>& __u)
 {
   typedef tuple<_GLIBCXX_TEMPLATE_ARGS> _Tp;
   typedef tuple<_GLIBCXX_TEMPLATE_ARGS_U> _Up;
   return __tuple_compare<tuple_size<_Tp>::value - tuple_size<_Tp>::value, 0,
                          tuple_size<_Tp>::value, _Tp, _Up>::__eq(__t, __u);
 }

 template<_GLIBCXX_TEMPLATE_PARAMS, _GLIBCXX_TEMPLATE_PARAMS_U>
 bool
 operator<(const tuple<_GLIBCXX_TEMPLATE_ARGS>& __t,
           const tuple<_GLIBCXX_TEMPLATE_ARGS_U>& __u)
 {
   typedef tuple<_GLIBCXX_TEMPLATE_ARGS> _Tp;
   typedef tuple<_GLIBCXX_TEMPLATE_ARGS_U> _Up;
   return __tuple_compare<tuple_size<_Tp>::value - tuple_size<_Tp>::value, 0,
                          tuple_size<_Tp>::value, _Tp, _Up>::__less(__t, __u);
 }

 template<_GLIBCXX_TEMPLATE_PARAMS, _GLIBCXX_TEMPLATE_PARAMS_U>
 bool
 operator!=(const tuple<_GLIBCXX_TEMPLATE_ARGS>& __t,
            const tuple<_GLIBCXX_TEMPLATE_ARGS_U>& __u)
 { return !(__t == __u); }

 template<_GLIBCXX_TEMPLATE_PARAMS, _GLIBCXX_TEMPLATE_PARAMS_U>
 bool
 operator>(const tuple<_GLIBCXX_TEMPLATE_ARGS>& __t,
           const tuple<_GLIBCXX_TEMPLATE_ARGS_U>& __u)
 { return __u < __t; }

 template<_GLIBCXX_TEMPLATE_PARAMS, _GLIBCXX_TEMPLATE_PARAMS_U>
 bool
 operator<=(const tuple<_GLIBCXX_TEMPLATE_ARGS>& __t,
            const tuple<_GLIBCXX_TEMPLATE_ARGS_U>& __u)
 { return !(__u < __t); }

 template<_GLIBCXX_TEMPLATE_PARAMS, _GLIBCXX_TEMPLATE_PARAMS_U>
 bool
 operator>=(const tuple<_GLIBCXX_TEMPLATE_ARGS>& __t,
            const tuple<_GLIBCXX_TEMPLATE_ARGS_U>& __u)
 { return !(__t < __u); }

 template<_GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS>
   struct __stripped_tuple_type
   {
     typedef tuple<_GLIBCXX_TEMPLATE_ARGS_STRIPPED>      __type;
   };

#endif

