// -*- C++ -*-

// Copyright (C) 2005, 2006 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software
// Foundation; either version 2, or (at your option) any later
// version.

// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this library; see the file COPYING.  If not, write to
// the Free Software Foundation, 59 Temple Place - Suite 330, Boston,
// MA 02111-1307, USA.

// As a special exception, you may use this file as part of a free
// software library without restriction.  Specifically, if other files
// instantiate templates or use macros or inline functions from this
// file, or you compile this file and link it with other files to
// produce an executable, this file does not by itself cause the
// resulting executable to be covered by the GNU General Public
// License.  This exception does not however invalidate any other
// reasons why the executable file might be covered by the GNU General
// Public License.

// Copyright (C) 2004 Ami Tavory and Vladimir Dreizin, IBM-HRL.

// Permission to use, copy, modify, sell, and distribute this software
// is hereby granted without fee, provided that the above copyright
// notice appears in all copies, and that both that copyright notice
// and this permission notice appear in supporting documentation. None
// of the above authors, nor IBM Haifa Research Laboratories, make any
// representation about the suitability of this software for any
// purpose. It is provided "as is" without express or implied
// warranty.

/**
 * @file type_utils.hpp
 * Contains utilities for handnling types. All of these classes are based on
 *    "Modern C++" by Andrei Alxandrescu.
 */

#ifndef PB_DS_TYPE_UTILS_HPP
#define PB_DS_TYPE_UTILS_HPP

#include <cstddef>
#include <utility>
#include <tr1/type_traits>
#include <ext/type_traits.h>
#include <ext/numeric_traits.h>

namespace pb_ds
{
  namespace detail
  {
    using std::tr1::is_same;
    using std::tr1::is_const;
    using std::tr1::is_pointer;
    using std::tr1::is_reference;
    using std::tr1::is_fundamental;
    using std::tr1::is_member_object_pointer;
    using std::tr1::is_member_pointer;
    using std::tr1::is_base_of;
    using std::tr1::remove_const;
    using std::tr1::remove_reference;

    // Need integral_const<bool, true> <-> integral_const<int, 1>, so
    // because of this use the following typedefs instead of importing
    // std::tr1's.
    using std::tr1::integral_constant;
    typedef std::tr1::integral_constant<int, 1> true_type;
    typedef std::tr1::integral_constant<int, 0> false_type;

    using __gnu_cxx::__conditional_type;
    using __gnu_cxx::__numeric_traits;

    template<typename T>
    struct is_const_pointer
    {
      enum
	{
	  value = is_const<T>::value && is_pointer<T>::value
	};
    };

    template<typename T>
    struct is_const_reference
    {
      enum
	{
	  value = is_const<T>::value && is_reference<T>::value
	};
    };

    template<typename T>
    struct is_simple
    {
      enum
	{
	  value = is_fundamental<typename remove_const<T>::type>::value 
	  || is_pointer<typename remove_const<T>::type>::value 
	  || is_member_pointer<T>::value 
	};
    };

    template<typename T>
    class is_pair
    {
    private:
      template<typename U>
      struct is_pair_imp
      {
	enum
	  {
	    value = 0
	  };
      };

      template<typename U, typename V>
      struct is_pair_imp<std::pair<U,V> >
      {
	enum
	  {
	    value = 1
	  };
      };

    public:
      enum
	{
	  value = is_pair_imp<T>::value
	};
    };


    template<bool>
    struct static_assert;

    template<>
    struct static_assert<true>
    { };

    template<int>
    struct static_assert_dumclass
    {
      enum
	{
	  v = 1
	};
    };

    template<typename Type>
    struct type_to_type
    {
      typedef Type type;
    };
  } // namespace detail
} // namespace pb_ds

#endif 
