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
 * @file trie_policy.hpp
 * Contains trie-related policies.
 */

#ifndef PB_DS_TRIE_POLICY_HPP
#define PB_DS_TRIE_POLICY_HPP

#include <string>
#include <ext/pb_ds/detail/type_utils.hpp>
#include <ext/pb_ds/detail/trie_policy/trie_policy_base.hpp>

namespace pb_ds
{
  // A null node updator, indicating that no node updates are required.
  template<typename Const_Node_Iterator,
	   typename Node_Iterator,
	   typename E_Access_Traits,
	   typename Allocator>
  struct null_trie_node_update
  { };

#define PB_DS_STATIC_ASSERT(UNIQUE, E)					\
  typedef detail::static_assert_dumclass<sizeof(detail::static_assert<bool(E)>)> UNIQUE##_static_assert_type

#define PB_DS_CLASS_T_DEC						\
  template<typename String, typename String::value_type Min_E_Val, typename String::value_type Max_E_Val, bool Reverse, typename Allocator>

#define PB_DS_CLASS_C_DEC						\
  string_trie_e_access_traits<String, Min_E_Val,Max_E_Val,Reverse,Allocator>

  // Element access traits for string types.
  template<typename String = std::string,
	   typename String::value_type Min_E_Val = detail::__numeric_traits<typename String::value_type>::__min, 
	   typename String::value_type Max_E_Val = detail::__numeric_traits<typename String::value_type>::__max, 
	   bool Reverse = false,
	   typename Allocator = std::allocator<char> >
  struct string_trie_e_access_traits
  {
  public:
    typedef typename Allocator::size_type size_type;
    typedef String key_type;
    typedef typename Allocator::template rebind<key_type>::other key_rebind;
    typedef typename key_rebind::const_reference const_key_reference;

    enum
      {
	reverse = Reverse
      };

    // Element const iterator type.
    typedef typename detail::__conditional_type<Reverse, typename String::const_reverse_iterator, typename String::const_iterator>::__type const_iterator;

    // Element type.
    typedef typename std::iterator_traits<const_iterator>::value_type e_type;

    enum
      {
	min_e_val = Min_E_Val,
	max_e_val = Max_E_Val,
	max_size = max_e_val - min_e_val + 1
      };
    PB_DS_STATIC_ASSERT(min_max_size, max_size >= 2);

    // Returns a const_iterator to the first element of
    // const_key_reference agumnet.
    inline static const_iterator
    begin(const_key_reference);

    // Returns a const_iterator to the after-last element of
    // const_key_reference argument.
    inline static const_iterator
    end(const_key_reference);

    // Maps an element to a position.
    inline static size_type
    e_pos(e_type e);

  private:

    inline static const_iterator
    begin_imp(const_key_reference, detail::false_type);

    inline static const_iterator
    begin_imp(const_key_reference, detail::true_type);

    inline static const_iterator
    end_imp(const_key_reference, detail::false_type);

    inline static const_iterator
    end_imp(const_key_reference, detail::true_type);

    static detail::integral_constant<int, Reverse> s_rev_ind;
  };

#include <ext/pb_ds/detail/trie_policy/string_trie_e_access_traits_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_T_DEC \
  template<typename Const_Node_Iterator,typename Node_Iterator,class E_Access_Traits, typename Allocator>

#define PB_DS_CLASS_C_DEC \
  trie_prefix_search_node_update<Const_Node_Iterator, Node_Iterator, E_Access_Traits,Allocator>

#define PB_DS_BASE_C_DEC \
  detail::trie_policy_base<Const_Node_Iterator,Node_Iterator,E_Access_Traits, Allocator>

  // A node updator that allows tries to be searched for the range of
  // values that match a certain prefix.
  template<typename Const_Node_Iterator,
	   typename Node_Iterator,
	   typename E_Access_Traits,
	   typename Allocator>
  class trie_prefix_search_node_update : private PB_DS_BASE_C_DEC
  {
  private:
    typedef PB_DS_BASE_C_DEC base_type;

  public:
    typedef typename base_type::key_type key_type;
    typedef typename base_type::const_key_reference const_key_reference;

    // Element access traits.
    typedef E_Access_Traits e_access_traits;

    // Const element iterator.
    typedef typename e_access_traits::const_iterator const_e_iterator;

    // Allocator type.
    typedef Allocator allocator;

    // Size type.
    typedef typename allocator::size_type size_type;
    typedef detail::null_node_metadata metadata_type;
    typedef Const_Node_Iterator const_node_iterator;
    typedef Node_Iterator node_iterator;
    typedef typename const_node_iterator::value_type const_iterator;
    typedef typename node_iterator::value_type iterator;

    // Finds the const iterator range corresponding to all values
    // whose prefixes match r_key.
    std::pair<const_iterator, const_iterator>
    prefix_range(const_key_reference) const;

    // Finds the iterator range corresponding to all values whose
    // prefixes match r_key.
    std::pair<iterator, iterator>
    prefix_range(const_key_reference);

    // Finds the const iterator range corresponding to all values
    // whose prefixes match [b, e).
    std::pair<const_iterator, const_iterator>
    prefix_range(const_e_iterator, const_e_iterator) const;

    // Finds the iterator range corresponding to all values whose
    // prefixes match [b, e).
    std::pair<iterator, iterator>
    prefix_range(const_e_iterator, const_e_iterator);

  protected:
    // Called to update a node's metadata.
    inline void
    operator()(node_iterator node_it, const_node_iterator end_nd_it) const;

  private:
    // Returns the const iterator associated with the just-after last element.
    virtual const_iterator
    end() const = 0;

    // Returns the iterator associated with the just-after last element.
    virtual iterator
    end() = 0;

    // Returns the const_node_iterator associated with the trie's root node.
    virtual const_node_iterator
    node_begin() const = 0;

    // Returns the node_iterator associated with the trie's root node.
    virtual node_iterator
    node_begin() = 0;

    // Returns the const_node_iterator associated with a just-after leaf node.
    virtual const_node_iterator
    node_end() const = 0;

    // Returns the node_iterator associated with a just-after leaf node.
    virtual node_iterator
    node_end() = 0;

    // Access to the cmp_fn object.
    virtual const e_access_traits& 
    get_e_access_traits() const = 0;

    node_iterator
    next_child(node_iterator, const_e_iterator, const_e_iterator, 
	       node_iterator, const e_access_traits&);
  };

#include <ext/pb_ds/detail/trie_policy/prefix_search_node_update_imp.hpp>

#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_C_DEC \
  trie_order_statistics_node_update<Const_Node_Iterator, Node_Iterator,E_Access_Traits, Allocator>

  // Functor updating ranks of entrees.
  template<typename Const_Node_Iterator,
	   typename Node_Iterator,
	   typename E_Access_Traits,
	   typename Allocator>
  class trie_order_statistics_node_update : private PB_DS_BASE_C_DEC
  {
  private:
    typedef PB_DS_BASE_C_DEC base_type;

  public:
    typedef E_Access_Traits e_access_traits;
    typedef typename e_access_traits::const_iterator const_e_iterator;
    typedef Allocator allocator;
    typedef typename allocator::size_type size_type;
    typedef typename base_type::key_type key_type;
    typedef typename base_type::const_key_reference const_key_reference;

    typedef size_type metadata_type;
    typedef Const_Node_Iterator const_node_iterator;
    typedef Node_Iterator node_iterator;
    typedef typename const_node_iterator::value_type const_iterator;
    typedef typename node_iterator::value_type iterator;

    // Finds an entry by __order. Returns a const_iterator to the
    // entry with the __order order, or a const_iterator to the
    // container object's end if order is at least the size of the
    // container object.
    inline const_iterator
    find_by_order(size_type) const;

    // Finds an entry by __order. Returns an iterator to the entry
    // with the __order order, or an iterator to the container
    // object's end if order is at least the size of the container
    // object.
    inline iterator
    find_by_order(size_type);

    // Returns the order of a key within a sequence. For exapmle, if
    // r_key is the smallest key, this method will return 0; if r_key
    // is a key between the smallest and next key, this method will
    // return 1; if r_key is a key larger than the largest key, this
    // method will return the size of r_c.
    inline size_type
    order_of_key(const_key_reference) const;

    // Returns the order of a prefix within a sequence. For exapmle,
    // if [b, e] is the smallest prefix, this method will return 0; if
    // r_key is a key between the smallest and next key, this method
    // will return 1; if r_key is a key larger than the largest key,
    // this method will return the size of r_c.
    inline size_type
    order_of_prefix(const_e_iterator, const_e_iterator) const;

  private:
    typedef typename base_type::const_reference const_reference;
    typedef typename base_type::const_pointer const_pointer;

    typedef typename Allocator::template rebind<metadata_type>::other metadata_rebind;
    typedef typename metadata_rebind::const_reference const_metadata_reference;
    typedef typename metadata_rebind::reference metadata_reference;

    // Returns true if the container is empty.
    virtual bool
    empty() const = 0;

    // Returns the iterator associated with the trie's first element.
    virtual iterator
    begin() = 0;

    // Returns the iterator associated with the trie's
    // just-after-last element.
    virtual iterator
    end() = 0;

    // Returns the const_node_iterator associated with the trie's root node.
    virtual const_node_iterator
    node_begin() const = 0;

    // Returns the node_iterator associated with the trie's root node.
    virtual node_iterator
    node_begin() = 0;

    // Returns the const_node_iterator associated with a just-after
    // leaf node.
    virtual const_node_iterator
    node_end() const = 0;

    // Returns the node_iterator associated with a just-after leaf node.
    virtual node_iterator
    node_end() = 0;

    // Access to the cmp_fn object.
    virtual e_access_traits& 
    get_e_access_traits() = 0;

  protected:
    // Updates the rank of a node through a node_iterator node_it;
    // end_nd_it is the end node iterator.
    inline void
    operator()(node_iterator, const_node_iterator) const;

    // Destructor.
    virtual
    ~trie_order_statistics_node_update();
  };

#include <ext/pb_ds/detail/trie_policy/order_statistics_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC
#undef PB_DS_BASE_C_DEC
#undef PB_DS_STATIC_ASSERT

} // namespace pb_ds

#endif
