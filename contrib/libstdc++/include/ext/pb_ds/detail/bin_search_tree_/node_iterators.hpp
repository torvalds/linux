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
 * @file node_iterators.hpp
 * Contains an implementation class for bin_search_tree_.
 */

#ifndef PB_DS_BIN_SEARCH_TREE_NODE_ITERATORS_HPP
#define PB_DS_BIN_SEARCH_TREE_NODE_ITERATORS_HPP

#include <ext/pb_ds/tag_and_trait.hpp>

namespace pb_ds
{
  namespace detail
  {

#define PB_DS_TREE_CONST_NODE_ITERATOR_CLASS_C_DEC			\
    bin_search_tree_const_node_it_<					\
							Node,		\
							Const_Iterator,	\
							Iterator,	\
							Allocator>

    // Const node iterator.
    template<typename Node,
	     class Const_Iterator,
	     class Iterator,
	     class Allocator>
    class bin_search_tree_const_node_it_
    {
    private:

    private:
      typedef
      typename Allocator::template rebind<
      Node>::other::pointer
      node_pointer;

    public:

      // Category.
      typedef trivial_iterator_tag iterator_category;

      // Difference type.
      typedef trivial_iterator_difference_type difference_type;

      // __Iterator's value type.
      typedef Const_Iterator value_type;

      // __Iterator's reference type.
      typedef Const_Iterator reference;

      // __Iterator's __const reference type.
      typedef Const_Iterator const_reference;

      // Metadata type.
      typedef typename Node::metadata_type metadata_type;

      // Const metadata reference type.
      typedef
      typename Allocator::template rebind<
	metadata_type>::other::const_reference
      const_metadata_reference;

    public:

      // Default constructor.
      /*
	inline
	bin_search_tree_const_node_it_()
      */

      inline
      bin_search_tree_const_node_it_(const node_pointer p_nd = NULL) : m_p_nd(const_cast<node_pointer>(p_nd))
      { }

      // Access.
      inline const_reference
      operator*() const
      {
	return (Const_Iterator(m_p_nd));
      }

      // Metadata access.
      inline const_metadata_reference
      get_metadata() const
      {
	return (m_p_nd->get_metadata());
      }

      // Returns the __const node iterator associated with the left node.
      inline PB_DS_TREE_CONST_NODE_ITERATOR_CLASS_C_DEC
      get_l_child() const
      {
	return (PB_DS_TREE_CONST_NODE_ITERATOR_CLASS_C_DEC(m_p_nd->m_p_left));
      }

      // Returns the __const node iterator associated with the right node.
      inline PB_DS_TREE_CONST_NODE_ITERATOR_CLASS_C_DEC
      get_r_child() const
      {
	return (PB_DS_TREE_CONST_NODE_ITERATOR_CLASS_C_DEC(m_p_nd->m_p_right));
      }

      // Compares to a different iterator object.
      inline bool
      operator==(const PB_DS_TREE_CONST_NODE_ITERATOR_CLASS_C_DEC& other) const
      {
	return (m_p_nd == other.m_p_nd);
      }

      // Compares (negatively) to a different iterator object.
      inline bool
      operator!=(const PB_DS_TREE_CONST_NODE_ITERATOR_CLASS_C_DEC& other) const
      {
	return (m_p_nd != other.m_p_nd);
      }

    public:
      node_pointer m_p_nd;
    };

#define PB_DS_TREE_NODE_ITERATOR_CLASS_C_DEC			\
    bin_search_tree_node_it_<					\
						Node,		\
						Const_Iterator, \
						Iterator,	\
						Allocator>

    // Node iterator.
    template<typename Node,
	     class Const_Iterator,
	     class Iterator,
	     class Allocator>
    class bin_search_tree_node_it_ : 
      public PB_DS_TREE_CONST_NODE_ITERATOR_CLASS_C_DEC

    {

    private:
      typedef
      typename Allocator::template rebind<
      Node>::other::pointer
      node_pointer;

    public:

      // __Iterator's value type.
      typedef Iterator value_type;

      // __Iterator's reference type.
      typedef Iterator reference;

      // __Iterator's __const reference type.
      typedef Iterator const_reference;

    public:

      // Default constructor.
      /*
	inline
	bin_search_tree_node_it_();
      */

      inline
      bin_search_tree_node_it_(const node_pointer p_nd = NULL) : PB_DS_TREE_CONST_NODE_ITERATOR_CLASS_C_DEC(
													    const_cast<node_pointer>(p_nd))
      { }

      // Access.
      inline Iterator
      operator*() const
      {
	return (Iterator(PB_DS_TREE_CONST_NODE_ITERATOR_CLASS_C_DEC::m_p_nd));
      }

      // Returns the node iterator associated with the left node.
      inline PB_DS_TREE_NODE_ITERATOR_CLASS_C_DEC
      get_l_child() const
      {
	return (PB_DS_TREE_NODE_ITERATOR_CLASS_C_DEC(
						     PB_DS_TREE_CONST_NODE_ITERATOR_CLASS_C_DEC::m_p_nd->m_p_left));
      }

      // Returns the node iterator associated with the right node.
      inline PB_DS_TREE_NODE_ITERATOR_CLASS_C_DEC
      get_r_child() const
      {
	return (PB_DS_TREE_NODE_ITERATOR_CLASS_C_DEC(
						     PB_DS_TREE_CONST_NODE_ITERATOR_CLASS_C_DEC::m_p_nd->m_p_right));
      }

    };

#undef PB_DS_TREE_CONST_NODE_ITERATOR_CLASS_C_DEC

#undef PB_DS_TREE_NODE_ITERATOR_CLASS_C_DEC

  } // namespace detail
} // namespace pb_ds

#endif // #ifndef PB_DS_BIN_SEARCH_TREE_NODE_ITERATORS_HPP

