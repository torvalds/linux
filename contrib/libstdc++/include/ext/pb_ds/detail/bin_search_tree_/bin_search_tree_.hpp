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
 * @file bin_search_tree_.hpp
 * Contains an implementation class for bin_search_tree_.
 */
/*
 * This implementation uses an idea from the SGI STL (using a "header" node
 *    which is needed for efficient iteration).
 */

#include <ext/pb_ds/exception.hpp>
#include <ext/pb_ds/detail/eq_fn/eq_by_less.hpp>
#include <ext/pb_ds/detail/types_traits.hpp>
#include <ext/pb_ds/detail/map_debug_base.hpp>
#include <ext/pb_ds/tree_policy.hpp>
#include <ext/pb_ds/detail/cond_dealtor.hpp>
#include <ext/pb_ds/detail/type_utils.hpp>
#include <ext/pb_ds/detail/tree_trace_base.hpp>
#include <utility>
#include <functional>
#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {

#define PB_DS_CLASS_T_DEC						\
    template<typename Key, typename Mapped, class Cmp_Fn,		\
	     class Node_And_It_Traits, class Allocator>

#ifdef PB_DS_DATA_TRUE_INDICATOR
#define PB_DS_CLASS_NAME			\
    bin_search_tree_data_
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
#define PB_DS_CLASS_NAME			\
    bin_search_tree_no_data_
#endif 

#define PB_DS_CLASS_C_DEC						\
    PB_DS_CLASS_NAME<							\
						Key,			\
						Mapped,			\
						Cmp_Fn,			\
						Node_And_It_Traits,	\
						Allocator>

#define PB_DS_TYPES_TRAITS_C_DEC				\
    types_traits<				\
						Key,		\
						Mapped,		\
						Allocator,	\
						false>

#ifdef _GLIBCXX_DEBUG
#define PB_DS_MAP_DEBUG_BASE_C_DEC					\
    map_debug_base<Key,	eq_by_less<Key, Cmp_Fn>, \
	      typename Allocator::template rebind<Key>::other::const_reference>
#endif 

#ifdef PB_DS_DATA_TRUE_INDICATOR
#define PB_DS_V2F(X) (X).first
#define PB_DS_V2S(X) (X).second
#define PB_DS_EP2VP(X)& ((X)->m_value)
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
#define PB_DS_V2F(X) (X)
#define PB_DS_V2S(X) Mapped_Data()
#define PB_DS_EP2VP(X)& ((X)->m_value.first)
#endif 

#ifdef PB_DS_TREE_TRACE
#define PB_DS_TREE_TRACE_BASE_C_DEC					\
    tree_trace_base<							\
									typename Node_And_It_Traits::const_node_iterator, \
									typename Node_And_It_Traits::node_iterator, \
									Cmp_Fn,	\
									true, \
									Allocator>
#endif 

    /**
     * class description = "8i|\|4ree $34rc|-| 7r33 74813.">
     **/
    template<typename Key,
	     typename Mapped,
	     class Cmp_Fn,
	     class Node_And_It_Traits,
	     class Allocator>
    class PB_DS_CLASS_NAME :
#ifdef _GLIBCXX_DEBUG
      public PB_DS_MAP_DEBUG_BASE_C_DEC,
#endif 
#ifdef PB_DS_TREE_TRACE
      public PB_DS_TREE_TRACE_BASE_C_DEC,
#endif 
      public Cmp_Fn,
      public PB_DS_TYPES_TRAITS_C_DEC,
      public Node_And_It_Traits::node_update
    {

    protected:
      typedef
      typename Allocator::template rebind<
      typename Node_And_It_Traits::node>::other
      node_allocator;

      typedef typename node_allocator::value_type node;

      typedef typename node_allocator::pointer node_pointer;

      typedef PB_DS_TYPES_TRAITS_C_DEC traits_base;

      typedef
      typename Node_And_It_Traits::null_node_update_pointer
      null_node_update_pointer;

    private:
      typedef cond_dealtor< node, Allocator> cond_dealtor_t;

#ifdef _GLIBCXX_DEBUG
      typedef PB_DS_MAP_DEBUG_BASE_C_DEC map_debug_base;
#endif 

    public:

      typedef typename Allocator::size_type size_type;

      typedef typename Allocator::difference_type difference_type;

      typedef typename PB_DS_TYPES_TRAITS_C_DEC::key_type key_type;

      typedef typename PB_DS_TYPES_TRAITS_C_DEC::key_pointer key_pointer;

      typedef
      typename PB_DS_TYPES_TRAITS_C_DEC::const_key_pointer
      const_key_pointer;

      typedef typename PB_DS_TYPES_TRAITS_C_DEC::key_reference key_reference;

      typedef
      typename PB_DS_TYPES_TRAITS_C_DEC::const_key_reference
      const_key_reference;

#ifdef PB_DS_DATA_TRUE_INDICATOR
      typedef typename PB_DS_TYPES_TRAITS_C_DEC::mapped_type mapped_type;

      typedef
      typename PB_DS_TYPES_TRAITS_C_DEC::mapped_pointer
      mapped_pointer;

      typedef
      typename PB_DS_TYPES_TRAITS_C_DEC::const_mapped_pointer
      const_mapped_pointer;

      typedef
      typename PB_DS_TYPES_TRAITS_C_DEC::mapped_reference
      mapped_reference;

      typedef
      typename PB_DS_TYPES_TRAITS_C_DEC::const_mapped_reference
      const_mapped_reference;
#endif 

      typedef typename PB_DS_TYPES_TRAITS_C_DEC::value_type value_type;

      typedef typename PB_DS_TYPES_TRAITS_C_DEC::pointer pointer;

      typedef typename PB_DS_TYPES_TRAITS_C_DEC::const_pointer const_pointer;

      typedef typename PB_DS_TYPES_TRAITS_C_DEC::reference reference;

      typedef
      typename PB_DS_TYPES_TRAITS_C_DEC::const_reference
      const_reference;

      typedef
      typename Node_And_It_Traits::const_point_iterator
      const_point_iterator;

      typedef const_point_iterator const_iterator;

      typedef typename Node_And_It_Traits::point_iterator point_iterator;

      typedef point_iterator iterator;

      typedef
      typename Node_And_It_Traits::const_reverse_iterator
      const_reverse_iterator;

      typedef typename Node_And_It_Traits::reverse_iterator reverse_iterator;

      typedef
      typename Node_And_It_Traits::const_node_iterator
      const_node_iterator;

      typedef typename Node_And_It_Traits::node_iterator node_iterator;

      typedef Cmp_Fn cmp_fn;

      typedef Allocator allocator;

      typedef typename Node_And_It_Traits::node_update node_update;

    public:

      PB_DS_CLASS_NAME();

      PB_DS_CLASS_NAME(const Cmp_Fn& r_cmp_fn);

      PB_DS_CLASS_NAME(const Cmp_Fn& r_cmp_fn, const node_update& r_update);

      PB_DS_CLASS_NAME(const PB_DS_CLASS_C_DEC& other);

      void
      swap(PB_DS_CLASS_C_DEC& other);

      ~PB_DS_CLASS_NAME();

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
      lower_bound(const_key_reference r_key);

      inline const_point_iterator
      lower_bound(const_key_reference r_key) const;

      inline point_iterator
      upper_bound(const_key_reference r_key);

      inline const_point_iterator
      upper_bound(const_key_reference r_key) const;

      inline point_iterator
      find(const_key_reference r_key);

      inline const_point_iterator
      find(const_key_reference r_key) const;

      inline iterator
      begin();

      inline const_iterator
      begin() const;

      inline iterator
      end();

      inline const_iterator
      end() const;

      inline reverse_iterator
      rbegin();

      inline const_reverse_iterator
      rbegin() const;

      inline reverse_iterator
      rend();

      inline const_reverse_iterator
      rend() const;

      inline const_node_iterator
      node_begin() const;

      inline node_iterator
      node_begin();

      inline const_node_iterator
      node_end() const;

      inline node_iterator
      node_end();

      void
      clear();

    protected:

      void
      value_swap(PB_DS_CLASS_C_DEC& other);

      void
      initialize_min_max();

      inline iterator
      insert_imp_empty(const_reference r_value);

      inline iterator
      insert_leaf_new(const_reference r_value, node_pointer p_nd, bool left_nd);

      inline node_pointer
      get_new_node_for_leaf_insert(const_reference r_val, false_type);

      inline node_pointer
      get_new_node_for_leaf_insert(const_reference r_val, true_type);

      inline void
      actual_erase_node(node_pointer p_nd);

      inline std::pair<node_pointer, bool>
      erase(node_pointer p_nd);

      inline void
      update_min_max_for_erased_node(node_pointer p_nd);

      static void
      clear_imp(node_pointer p_nd);

      inline std::pair<
	point_iterator,
	bool>
      insert_leaf(const_reference r_value);

      inline void
      rotate_left(node_pointer p_x);

      inline void
      rotate_right(node_pointer p_y);

      inline void
      rotate_parent(node_pointer p_nd);

      inline void
      apply_update(node_pointer p_nd, null_node_update_pointer);

      template<typename Node_Update_>
      inline void
      apply_update(node_pointer p_nd, Node_Update_* p_update);

      inline void
      update_to_top(node_pointer p_nd, null_node_update_pointer);

      template<typename Node_Update_>
      inline void
      update_to_top(node_pointer p_nd, Node_Update_* p_update);

      bool
      join_prep(PB_DS_CLASS_C_DEC& other);

      void
      join_finish(PB_DS_CLASS_C_DEC& other);

      bool
      split_prep(const_key_reference r_key, PB_DS_CLASS_C_DEC& other);

      void
      split_finish(PB_DS_CLASS_C_DEC& other);

      size_type
      recursive_count(node_pointer p_nd) const;

#ifdef _GLIBCXX_DEBUG
      void
      assert_valid() const;

      void
      structure_only_assert_valid() const;

      void
      assert_node_consistent(const node_pointer p_nd) const;
#endif 

    private:
#ifdef _GLIBCXX_DEBUG
      void
      assert_iterators() const;

      void
      assert_consistent_with_debug_base() const;

      void
      assert_node_consistent_with_left(const node_pointer p_nd) const;

      void
      assert_node_consistent_with_right(const node_pointer p_nd) const;

      void
      assert_consistent_with_debug_base(const node_pointer p_nd) const;

      void
      assert_min() const;

      void
      assert_min_imp(const node_pointer p_nd) const;

      void
      assert_max() const;

      void
      assert_max_imp(const node_pointer p_nd) const;

      void
      assert_size() const;

      typedef std::pair< const_pointer, const_pointer> node_consistent_t;

      node_consistent_t
      assert_node_consistent_(const node_pointer p_nd) const;
#endif 

      void
      initialize();

      node_pointer
      recursive_copy_node(const node_pointer p_nd);

    protected:
      node_pointer m_p_head;

      size_type m_size;

      static node_allocator s_node_allocator;
    };

#include <ext/pb_ds/detail/bin_search_tree_/constructors_destructor_fn_imps.hpp>
#include <ext/pb_ds/detail/bin_search_tree_/iterators_fn_imps.hpp>
#include <ext/pb_ds/detail/bin_search_tree_/debug_fn_imps.hpp>
#include <ext/pb_ds/detail/bin_search_tree_/insert_fn_imps.hpp>
#include <ext/pb_ds/detail/bin_search_tree_/erase_fn_imps.hpp>
#include <ext/pb_ds/detail/bin_search_tree_/find_fn_imps.hpp>
#include <ext/pb_ds/detail/bin_search_tree_/info_fn_imps.hpp>
#include <ext/pb_ds/detail/bin_search_tree_/split_join_fn_imps.hpp>
#include <ext/pb_ds/detail/bin_search_tree_/rotate_fn_imps.hpp>
#include <ext/pb_ds/detail/bin_search_tree_/policy_access_fn_imps.hpp>

#undef PB_DS_CLASS_C_DEC

#undef PB_DS_CLASS_T_DEC

#undef PB_DS_CLASS_NAME

#undef PB_DS_TYPES_TRAITS_C_DEC

#undef PB_DS_MAP_DEBUG_BASE_C_DEC

#ifdef PB_DS_TREE_TRACE
#undef PB_DS_TREE_TRACE_BASE_C_DEC
#endif 

#undef PB_DS_V2F
#undef PB_DS_EP2VP
#undef PB_DS_V2S

  } // namespace detail
} // namespace pb_ds
