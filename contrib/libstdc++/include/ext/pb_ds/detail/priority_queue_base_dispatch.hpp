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
 * @file priority_queue_base_dispatch.hpp
 * Contains an pqiative container dispatching base.
 */

#ifndef PB_DS_PRIORITY_QUEUE_BASE_DS_DISPATCHER_HPP
#define PB_DS_PRIORITY_QUEUE_BASE_DS_DISPATCHER_HPP

#include <ext/pb_ds/detail/pairing_heap_/pairing_heap_.hpp>
#include <ext/pb_ds/detail/binomial_heap_/binomial_heap_.hpp>
#include <ext/pb_ds/detail/rc_binomial_heap_/rc_binomial_heap_.hpp>
#include <ext/pb_ds/detail/binary_heap_/binary_heap_.hpp>
#include <ext/pb_ds/detail/thin_heap_/thin_heap_.hpp>

namespace pb_ds
{
    namespace detail
    {

      template<typename Value_Type, typename Cmp_Fn, typename Tag, typename Allocator>
      struct priority_queue_base_dispatch;

      template<typename Value_Type, typename Cmp_Fn, typename Allocator>
      struct priority_queue_base_dispatch<Value_Type, Cmp_Fn, pairing_heap_tag, Allocator>
      {
	typedef pairing_heap_< Value_Type, Cmp_Fn, Allocator> type;
      };

      template<typename Value_Type, typename Cmp_Fn, typename Allocator>
      struct priority_queue_base_dispatch<Value_Type, Cmp_Fn, binomial_heap_tag, Allocator>
      {
	typedef binomial_heap_< Value_Type, Cmp_Fn, Allocator> type;
      };

      template<typename Value_Type, typename Cmp_Fn, typename Allocator>
      struct priority_queue_base_dispatch<Value_Type, Cmp_Fn, rc_binomial_heap_tag, Allocator>
      {
	typedef rc_binomial_heap_< Value_Type, Cmp_Fn, Allocator> type;
      };

      template<typename Value_Type, typename Cmp_Fn, typename Allocator>
      struct priority_queue_base_dispatch<Value_Type, Cmp_Fn, binary_heap_tag, Allocator>
      {
	typedef binary_heap_< Value_Type, Cmp_Fn, Allocator> type;
      };

      template<typename Value_Type, typename Cmp_Fn, typename Allocator>
      struct priority_queue_base_dispatch<Value_Type, Cmp_Fn, thin_heap_tag, Allocator>
      {
	typedef thin_heap_< Value_Type, Cmp_Fn, Allocator> type;
      };

    } // namespace detail
} // namespace pb_ds

#endif // #ifndef PB_DS_PRIORITY_QUEUE_BASE_DS_DISPATCHER_HPP
