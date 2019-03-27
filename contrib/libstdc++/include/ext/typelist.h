// -*- C++ -*-

// Copyright (C) 2005, 2006 Free Software Foundation, Inc.
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

// Copyright (C) 2004 Ami Tavory and Vladimir Dreizin, IBM-HRL.

// Permission to use, copy, modify, sell, and distribute this software
// is hereby granted without fee, provided that the above copyright
// notice appears in all copies, and that both that copyright notice and
// this permission notice appear in supporting documentation. None of
// the above authors, nor IBM Haifa Research Laboratories, make any
// representation about the suitability of this software for any
// purpose. It is provided "as is" without express or implied warranty.

/**
 * @file typelist.h
 * Contains typelist_chain definitions.
 * Typelists are an idea by Andrei Alexandrescu.
 */

#ifndef _TYPELIST_H
#define _TYPELIST_H 1

#include <ext/type_traits.h>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

namespace typelist
{
  struct null_type { };

  template<typename Root>
    struct node
    {
      typedef Root 	root;
    };

  // Forward declarations of functors.
  template<typename Hd, typename Typelist>
    struct chain
    {
      typedef Hd 	head;
      typedef Typelist 	tail;
    };

  template<typename Fn, class Typelist>
    void
    apply(Fn&, Typelist);

  template<typename Typelist0, typename Typelist1>
    struct append;

  template<typename Typelist_Typelist>
    struct append_typelist;

  template<typename Typelist, typename T>
    struct contains;
 
  template<typename Typelist, template<typename T> class Pred>
    struct filter;

  template<typename Typelist, int i>
    struct at_index;

  template<typename Typelist, template<typename T> class Transform>
    struct transform;

  template<typename Typelist_Typelist>
    struct flatten;

  template<typename Typelist>
    struct from_first;

  template<typename T1>
    struct create1;

  template<typename T1, typename T2>
    struct create2;

  template<typename T1, typename T2, typename T3>
    struct create3;

  template<typename T1, typename T2, typename T3, typename T4>
    struct create4;

  template<typename T1, typename T2, typename T3, typename T4, typename T5>
    struct create5;

  template<typename T1, typename T2, typename T3, 
	   typename T4, typename T5, typename T6>
    struct create6;
} // namespace typelist

_GLIBCXX_END_NAMESPACE


_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

namespace typelist 
{
namespace detail
{
  template<typename Fn, typename Typelist_Chain>
    struct apply_;

  template<typename Fn, typename Hd, typename Tl>
    struct apply_<Fn, chain<Hd, Tl> >
    {
      void
      operator() (Fn& f)
      {
	f.operator()(Hd());
	apply_<Fn, Tl> next;
	next(f);
      }
  };

  template<typename Fn>
    struct apply_<Fn, null_type>
    {
      void
      operator()(Fn&) { }
  };

  template<typename Typelist_Chain0, typename Typelist_Chain1>
    struct append_;

  template<typename Hd, typename Tl, typename Typelist_Chain>
    struct append_<chain<Hd, Tl>, Typelist_Chain>
    {
    private:
      typedef append_<Tl, Typelist_Chain> 			append_type;

    public:
      typedef chain<Hd, typename append_type::type> 		type;
    };

  template<typename Typelist_Chain>
    struct append_<null_type, Typelist_Chain>
    {
      typedef Typelist_Chain 			      		type;
    };

  template<typename Typelist_Chain>
    struct append_<Typelist_Chain, null_type>
    {
      typedef Typelist_Chain 					type;
    };

  template<>
    struct append_<null_type, null_type>
    {
      typedef null_type 					type;
    };

  template<typename Typelist_Typelist_Chain>
    struct append_typelist_;

  template<typename Hd>
    struct append_typelist_<chain<Hd, null_type> >
    {
      typedef chain<Hd, null_type> 				type;
    };

  template<typename Hd, typename Tl>
    struct append_typelist_<chain< Hd, Tl> >
    {
    private:
      typedef typename append_typelist_<Tl>::type 		rest_type;
      
    public:
      typedef typename append<Hd, node<rest_type> >::type::root	type;
    };

  template<typename Typelist_Chain, typename T>
    struct contains_;

  template<typename T>
    struct contains_<null_type, T>
    {
      enum
	{
	  value = false
	};
    };

  template<typename Hd, typename Tl, typename T>
    struct contains_<chain<Hd, Tl>, T>
    {
      enum
	{
	  value = contains_<Tl, T>::value
	};
    };
  
  template<typename Tl, typename T>
    struct contains_<chain<T, Tl>, T>
    {
      enum
	{
	  value = true
	};
    };

  template<typename Typelist_Chain, template<typename T> class Pred>
    struct chain_filter_;

  template<template<typename T> class Pred>
    struct chain_filter_<null_type, Pred>
    {
      typedef null_type 					type;
  };

  template<typename Hd, typename Tl, template<typename T> class Pred>
    struct chain_filter_<chain<Hd, Tl>, Pred>
    {
    private:
      enum
	{
	  include_hd = Pred<Hd>::value
	};
      
      typedef typename chain_filter_<Tl, Pred>::type 		rest_type;
      typedef chain<Hd, rest_type> 				chain_type;

    public:
      typedef typename __conditional_type<include_hd, chain_type, rest_type>::__type type;
  };

  template<typename Typelist_Chain, int i>
    struct chain_at_index_;

  template<typename Hd, typename Tl>
    struct chain_at_index_<chain<Hd, Tl>, 0>
    {
      typedef Hd 						type;
    };
  
  template<typename Hd, typename Tl, int i>
    struct chain_at_index_<chain<Hd, Tl>, i>
    {
      typedef typename chain_at_index_<Tl, i - 1>::type 	type;
    };

  template<class Typelist_Chain, template<typename T> class Transform>
    struct chain_transform_;

  template<template<typename T> class Transform>
    struct chain_transform_<null_type, Transform>
    {
      typedef null_type 					type;
    };
  
  template<class Hd, class Tl, template<typename T> class Transform>
    struct chain_transform_<chain<Hd, Tl>, Transform>
    {
    private:
      typedef typename chain_transform_<Tl, Transform>::type 	rest_type;
      typedef typename Transform<Hd>::type 			transform_type;

    public:
      typedef chain<transform_type, rest_type> 			type;
    };

  template<typename Typelist_Typelist_Chain>
    struct chain_flatten_;

  template<typename Hd_Tl>
  struct chain_flatten_<chain<Hd_Tl, null_type> >
  {
    typedef typename Hd_Tl::root 				type;
  };

  template<typename Hd_Typelist, class Tl_Typelist>
  struct chain_flatten_<chain<Hd_Typelist, Tl_Typelist> >
  {
  private:
    typedef typename chain_flatten_<Tl_Typelist>::type 		rest_type;
    typedef append<Hd_Typelist, node<rest_type> >		append_type;
  public:
    typedef typename append_type::type::root 			type;
  };
} // namespace detail
} // namespace typelist

_GLIBCXX_END_NAMESPACE

#define _GLIBCXX_TYPELIST_CHAIN1(X0) __gnu_cxx::typelist::chain<X0, __gnu_cxx::typelist::null_type>
#define _GLIBCXX_TYPELIST_CHAIN2(X0, X1) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN1(X1) >
#define _GLIBCXX_TYPELIST_CHAIN3(X0, X1, X2) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN2(X1, X2) >
#define _GLIBCXX_TYPELIST_CHAIN4(X0, X1, X2, X3) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN3(X1, X2, X3) >
#define _GLIBCXX_TYPELIST_CHAIN5(X0, X1, X2, X3, X4) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN4(X1, X2, X3, X4) >
#define _GLIBCXX_TYPELIST_CHAIN6(X0, X1, X2, X3, X4, X5) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN5(X1, X2, X3, X4, X5) >
#define _GLIBCXX_TYPELIST_CHAIN7(X0, X1, X2, X3, X4, X5, X6) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN6(X1, X2, X3, X4, X5, X6) >
#define _GLIBCXX_TYPELIST_CHAIN8(X0, X1, X2, X3, X4, X5, X6, X7) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN7(X1, X2, X3, X4, X5, X6, X7) >
#define _GLIBCXX_TYPELIST_CHAIN9(X0, X1, X2, X3, X4, X5, X6, X7, X8) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN8(X1, X2, X3, X4, X5, X6, X7, X8) >
#define _GLIBCXX_TYPELIST_CHAIN10(X0, X1, X2, X3, X4, X5, X6, X7, X8, X9) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN9(X1, X2, X3, X4, X5, X6, X7, X8, X9) >
#define _GLIBCXX_TYPELIST_CHAIN11(X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN10(X1, X2, X3, X4, X5, X6, X7, X8, X9, X10) >
#define _GLIBCXX_TYPELIST_CHAIN12(X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN11(X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11) >
#define _GLIBCXX_TYPELIST_CHAIN13(X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN12(X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12) >
#define _GLIBCXX_TYPELIST_CHAIN14(X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN13(X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13) >
#define _GLIBCXX_TYPELIST_CHAIN15(X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14) __gnu_cxx::typelist::chain<X0, _GLIBCXX_TYPELIST_CHAIN14(X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14) >

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

namespace typelist
{
  template<typename Fn, class Typelist>
    void
    apply(Fn& fn, Typelist)
    {
      detail::apply_<Fn, typename Typelist::root> a;
      a(fn);
    }

  template<typename Typelist0, typename Typelist1>
    struct append
    {
    private:
      typedef typename Typelist0::root 				root0_type;
      typedef typename Typelist1::root 				root1_type;
      typedef detail::append_<root0_type, root1_type> 		append_type;

    public:
      typedef node<typename append_type::type> 			type;
    };

  template<typename Typelist_Typelist>
    struct append_typelist
    {
    private:
      typedef typename Typelist_Typelist::root 		      	root_type;
      typedef detail::append_typelist_<root_type> 		append_type;

    public:
      typedef node<typename append_type::type> 			type;
    };

  template<typename Typelist, typename T>
    struct contains
    {
    private:
      typedef typename Typelist::root 				root_type;

    public:
      enum
	{
	  value = detail::contains_<root_type, T>::value
	};
    };

  template<typename Typelist, template<typename T> class Pred>
    struct filter
    {
    private:
      typedef typename Typelist::root 				root_type;
      typedef detail::chain_filter_<root_type, Pred> 		filter_type;

    public:
      typedef node<typename filter_type::type> 	       		type;
    };

  template<typename Typelist, int i>
    struct at_index
    {
    private:
      typedef typename Typelist::root 				root_type;
      typedef detail::chain_at_index_<root_type, i> 		index_type;
      
    public:
      typedef typename index_type::type 			type;
    };

  template<typename Typelist, template<typename T> class Transform>
    struct transform
    {
    private:
      typedef typename Typelist::root 				root_type;
      typedef detail::chain_transform_<root_type, Transform> 	transform_type;

    public:
      typedef node<typename transform_type::type> 		type;
    };

  template<typename Typelist_Typelist>
    struct flatten
    {
    private:
      typedef typename Typelist_Typelist::root 		      	root_type;
      typedef typename detail::chain_flatten_<root_type>::type 	flatten_type;

    public:
      typedef node<flatten_type> 				type;
    };

  template<typename Typelist>
    struct from_first
    {
    private:
      typedef typename at_index<Typelist, 0>::type 		first_type;

    public:
      typedef node<chain<first_type, null_type> > 		type;
    };

  template<typename T1>
    struct create1
    {
      typedef node<_GLIBCXX_TYPELIST_CHAIN1(T1)> 		type;
    };

  template<typename T1, typename T2>
    struct create2
    {
      typedef node<_GLIBCXX_TYPELIST_CHAIN2(T1,T2)> 		type;
    };

  template<typename T1, typename T2, typename T3>
    struct create3
    {
      typedef node<_GLIBCXX_TYPELIST_CHAIN3(T1,T2,T3)>		type;
    };

  template<typename T1, typename T2, typename T3, typename T4>
    struct create4
    {
      typedef node<_GLIBCXX_TYPELIST_CHAIN4(T1,T2,T3,T4)>	type;
    };

  template<typename T1, typename T2, typename T3, 
	   typename T4, typename T5>
    struct create5
    {
      typedef node<_GLIBCXX_TYPELIST_CHAIN5(T1,T2,T3,T4,T5)>	type;
    };

  template<typename T1, typename T2, typename T3, 
	   typename T4, typename T5, typename T6>
    struct create6
    {
      typedef node<_GLIBCXX_TYPELIST_CHAIN6(T1,T2,T3,T4,T5,T6)>	type;
    };
} // namespace typelist
_GLIBCXX_END_NAMESPACE


#endif

