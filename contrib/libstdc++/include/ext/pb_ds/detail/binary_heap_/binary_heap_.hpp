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
 * @file binary_heap_.hpp
 * Contains an implementation class for a binary heap.
 */

#ifndef PB_DS_BINARY_HEAP_HPP
#define PB_DS_BINARY_HEAP_HPP

/*
 * Based on CLRS.
 */

#include <queue>
#include <algorithm>
#include <ext/pb_ds/detail/cond_dealtor.hpp>
#include <ext/pb_ds/detail/cond_dealtor.hpp>
#include <ext/pb_ds/detail/type_utils.hpp>
#include <ext/pb_ds/detail/binary_heap_/entry_cmp.hpp>
#include <ext/pb_ds/detail/binary_heap_/entry_pred.hpp>
#include <ext/pb_ds/detail/binary_heap_/resize_policy.hpp>
#include <ext/pb_ds/detail/binary_heap_/const_point_iterator.hpp>
#include <ext/pb_ds/detail/binary_heap_/const_iterator.hpp>
#ifdef PB_DS_BINARY_HEAP_TRACE_
#include <iostream>
#endif
#include <ext/pb_ds/detail/type_utils.hpp>
#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {
#define PB_DS_CLASS_T_DEC \
    template<typename Value_Type, class Cmp_Fn, class Allocator>

#define PB_DS_CLASS_C_DEC \
    binary_heap_<Value_Type, Cmp_Fn, Allocator>

#define PB_DS_ENTRY_CMP_DEC \
    entry_cmp<Value_Type, Cmp_Fn, is_simple<Value_Type>::value, Allocator>::type

#define PB_DS_RESIZE_POLICY_DEC	\
    resize_policy<typename Allocator::size_type>

    /**
     * class description = "Base class for some types of h3ap$">
     **/
    template<typename Value_Type, class Cmp_Fn, class Allocator>
    class binary_heap_ : public PB_DS_ENTRY_CMP_DEC,
			 public PB_DS_RESIZE_POLICY_DEC
    {

    private:
      enum
	{
	  simple_value = is_simple<Value_Type>::value
	};

      typedef integral_constant<int, simple_value> no_throw_copies_t;

      typedef
      typename Allocator::template rebind<
	Value_Type>::other
      value_allocator;

      typedef
      typename __conditional_type<
	simple_value,
	Value_Type,
	typename value_allocator::pointer>::__type
      entry;

      typedef
      typename Allocator::template rebind<
	entry>::other
      entry_allocator;

      typedef typename entry_allocator::pointer entry_pointer;

      typedef typename PB_DS_ENTRY_CMP_DEC entry_cmp;

      typedef PB_DS_RESIZE_POLICY_DEC resize_policy;

      typedef
      cond_dealtor<
	Value_Type,
	Allocator>
      cond_dealtor_t;

    public:

      typedef typename Allocator::size_type size_type;

      typedef typename Allocator::difference_type difference_type;

      typedef Value_Type value_type;

      typedef
      typename Allocator::template rebind<
	value_type>::other::pointer
      pointer;

      typedef
      typename Allocator::template rebind<
	value_type>::other::const_pointer
      const_pointer;

      typedef
      typename Allocator::template rebind<
	value_type>::other::reference
      reference;

      typedef
      typename Allocator::template rebind<
	value_type>::other::const_reference
      const_reference;

      typedef
      binary_heap_const_point_iterator_<
	value_type,
	entry,
	simple_value,
	Allocator>
      const_point_iterator;

      typedef const_point_iterator point_iterator;

      typedef
      binary_heap_const_iterator_<
	value_type,
	entry,
	simple_value,
	Allocator>
      const_iterator;

      typedef const_iterator iterator;

      typedef Cmp_Fn cmp_fn;

      typedef Allocator allocator;

    public:

      binary_heap_();

      binary_heap_(const Cmp_Fn& r_cmp_fn);

      binary_heap_(const PB_DS_CLASS_C_DEC& other);

      void
      swap(PB_DS_CLASS_C_DEC& other);

      ~binary_heap_();

      inline bool
      empty() const;

      inline size_type
      size() const;

      inline size_type
      max_size() const;

      Cmp_Fn& 
      get_cmp_fn();

      const Cmp_Fn& 
      get_cmp_fn() const;

      inline point_iterator
      push(const_reference r_val);

      void
      modify(point_iterator it, const_reference r_new_val);

      inline const_reference
      top() const;

      inline void
      pop();

      inline void
      erase(point_iterator it);

      template<typename Pred>
      typename PB_DS_CLASS_C_DEC::size_type
      erase_if(Pred pred);

      inline static void
      erase_at(entry_pointer a_entries, size_type size, false_type);

      inline static void
      erase_at(entry_pointer a_entries, size_type size, true_type);

      inline iterator
      begin();

      inline const_iterator
      begin() const;

      inline iterator
      end();

      inline const_iterator
      end() const;

      void
      clear();

      template<typename Pred>
      void
      split(Pred pred, PB_DS_CLASS_C_DEC& other);

      void
      join(PB_DS_CLASS_C_DEC& other);

#ifdef PB_DS_BINARY_HEAP_TRACE_
      void
      trace() const;
#endif 

    protected:

      template<typename It>
      void
      copy_from_range(It first_it, It last_it);

    private:

      void
      value_swap(PB_DS_CLASS_C_DEC& other);

      inline void
      insert_value(const_reference r_val, false_type);

      inline void
      insert_value(value_type val, true_type);

      inline void
      insert_entry(entry e);

      inline void
      resize_for_insert_if_needed();

      inline void
      swap_value_imp(entry_pointer p_e, value_type new_val, true_type);

      inline void
      swap_value_imp(entry_pointer p_e, const_reference r_new_val, false_type);

      void
      fix(entry_pointer p_e);

      inline const_reference
      top_imp(true_type) const;

      inline const_reference
      top_imp(false_type) const;

      inline static size_type
      left_child(size_type i);

      inline static size_type
      right_child(size_type i);

      inline static size_type
      parent(size_type i);

      inline void
      resize_for_erase_if_needed();

      template<typename Pred>
      size_type
      partition(Pred pred);

#ifdef _GLIBCXX_DEBUG
      void
      assert_valid() const;
#endif 

#ifdef PB_DS_BINARY_HEAP_TRACE_
      void
      trace_entry(const entry& r_e, false_type) const;

      void
      trace_entry(const entry& r_e, true_type) const;
#endif 

    private:
      static entry_allocator s_entry_allocator;

      static value_allocator s_value_allocator;

      static no_throw_copies_t s_no_throw_copies_ind;

      size_type m_size;

      size_type m_actual_size;

      entry_pointer m_a_entries;
    };

#include <ext/pb_ds/detail/binary_heap_/insert_fn_imps.hpp>
#include <ext/pb_ds/detail/binary_heap_/constructors_destructor_fn_imps.hpp>
#include <ext/pb_ds/detail/binary_heap_/iterators_fn_imps.hpp>
#include <ext/pb_ds/detail/binary_heap_/debug_fn_imps.hpp>
#include <ext/pb_ds/detail/binary_heap_/trace_fn_imps.hpp>
#include <ext/pb_ds/detail/binary_heap_/erase_fn_imps.hpp>
#include <ext/pb_ds/detail/binary_heap_/info_fn_imps.hpp>
#include <ext/pb_ds/detail/binary_heap_/find_fn_imps.hpp>
#include <ext/pb_ds/detail/binary_heap_/split_join_fn_imps.hpp>
#include <ext/pb_ds/detail/binary_heap_/policy_access_fn_imps.hpp>

#undef PB_DS_CLASS_C_DEC
#undef PB_DS_CLASS_T_DEC
#undef PB_DS_ENTRY_CMP_DEC
#undef PB_DS_RESIZE_POLICY_DEC

  } // namespace detail
} // namespace pb_ds

#endif 
