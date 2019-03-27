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
 * @file point_iterators.hpp
 * Contains an implementation class for bin_search_tree_.
 */

#ifndef PB_DS_PAT_TRIE_FIND_ITERATORS_HPP
#define PB_DS_PAT_TRIE_FIND_ITERATORS_HPP

#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {

#define PB_DS_CONST_IT_C_DEC					\
    pat_trie_const_it_<						\
					Type_Traits,		\
					Node,			\
					Leaf,			\
					Head,			\
					Internal_Node,		\
					Is_Forward_Iterator,	\
					Allocator>

#define PB_DS_CONST_ODIR_IT_C_DEC				\
    pat_trie_const_it_<						\
					Type_Traits,		\
					Node,			\
					Leaf,			\
					Head,			\
					Internal_Node,		\
					!Is_Forward_Iterator,	\
					Allocator>

#define PB_DS_IT_C_DEC							\
    pat_trie_it_<							\
						Type_Traits,		\
						Node,			\
						Leaf,			\
						Head,			\
						Internal_Node,		\
						Is_Forward_Iterator,	\
						Allocator>

#define PB_DS_ODIR_IT_C_DEC						\
    pat_trie_it_<							\
						Type_Traits,		\
						Node,			\
						Leaf,			\
						Head,			\
						Internal_Node,		\
						!Is_Forward_Iterator,	\
						Allocator>


    // Const iterator.
    template<typename Type_Traits,
	     class Node,
	     class Leaf,
	     class Head,
	     class Internal_Node,
	     bool Is_Forward_Iterator,
	     class Allocator>
    class pat_trie_const_it_
    {

    private:
      typedef
      typename Allocator::template rebind<
      Node>::other::pointer
      node_pointer;

      typedef
      typename Allocator::template rebind<
	Leaf>::other::const_pointer
      const_leaf_pointer;

      typedef
      typename Allocator::template rebind<
	Leaf>::other::pointer
      leaf_pointer;

      typedef
      typename Allocator::template rebind<
	Head>::other::pointer
      head_pointer;

      typedef
      typename Allocator::template rebind<
	Internal_Node>::other::pointer
      internal_node_pointer;

    public:

      typedef std::bidirectional_iterator_tag iterator_category;

      typedef typename Allocator::difference_type difference_type;

      typedef typename Type_Traits::value_type value_type;

      typedef typename Type_Traits::pointer pointer;

      typedef typename Type_Traits::const_pointer const_pointer;

      typedef typename Type_Traits::reference reference;

      typedef typename Type_Traits::const_reference const_reference;

    public:

      inline
      pat_trie_const_it_(node_pointer p_nd = NULL) : m_p_nd(p_nd)
      { }

      inline
      pat_trie_const_it_(const PB_DS_CONST_ODIR_IT_C_DEC& other) 
      : m_p_nd(other.m_p_nd)
      { }

      inline
      PB_DS_CONST_IT_C_DEC& 
      operator=(const PB_DS_CONST_IT_C_DEC& other)
      {
	m_p_nd = other.m_p_nd;
	return *this;
      }

      inline
      PB_DS_CONST_IT_C_DEC& 
      operator=(const PB_DS_CONST_ODIR_IT_C_DEC& other)
      {
	m_p_nd = other.m_p_nd;
	return *this;
      }

      inline const_pointer
      operator->() const
      {
	_GLIBCXX_DEBUG_ASSERT(m_p_nd->m_type == pat_trie_leaf_node_type);
	return &static_cast<leaf_pointer>(m_p_nd)->value();
      }

      inline const_reference
      operator*() const
      {
	_GLIBCXX_DEBUG_ASSERT(m_p_nd->m_type == pat_trie_leaf_node_type);
	return static_cast<leaf_pointer>(m_p_nd)->value();
      }

      inline bool
      operator==(const PB_DS_CONST_IT_C_DEC& other) const
      { return (m_p_nd == other.m_p_nd); }

      inline bool
      operator==(const PB_DS_CONST_ODIR_IT_C_DEC& other) const
      { return (m_p_nd == other.m_p_nd); }

      inline bool
      operator!=(const PB_DS_CONST_IT_C_DEC& other) const
      { return (m_p_nd != other.m_p_nd); }

      inline bool
      operator!=(const PB_DS_CONST_ODIR_IT_C_DEC& other) const
      { return (m_p_nd != other.m_p_nd); }

      inline PB_DS_CONST_IT_C_DEC& 
      operator++()
      {
	inc(integral_constant<int,Is_Forward_Iterator>());
	return *this;
      }

      inline PB_DS_CONST_IT_C_DEC
      operator++(int)
      {
	PB_DS_CONST_IT_C_DEC ret_it(m_p_nd);
	operator++();
	return ret_it;
      }

      inline PB_DS_CONST_IT_C_DEC& 
      operator--()
      {
	dec(integral_constant<int,Is_Forward_Iterator>());
	return *this;
      }

      inline PB_DS_CONST_IT_C_DEC
      operator--(int)
      {
	PB_DS_CONST_IT_C_DEC ret_it(m_p_nd);
	operator--();
	return ret_it;
      }

    protected:
      inline void
      inc(false_type)
      { dec(true_type()); }

      void
      inc(true_type)
      {
	if (m_p_nd->m_type == pat_trie_head_node_type)
	  {
	    m_p_nd = static_cast<head_pointer>(m_p_nd)->m_p_min;
	    return;
	  }

	node_pointer p_y = m_p_nd->m_p_parent;
	while (p_y->m_type != pat_trie_head_node_type && 
	       get_larger_sibling(m_p_nd) == NULL)
	  {
	    m_p_nd = p_y;
	    p_y = p_y->m_p_parent;
	  }

	if (p_y->m_type == pat_trie_head_node_type)
	  {
	    m_p_nd = p_y;
	    return;
	  }
	m_p_nd = leftmost_descendant(get_larger_sibling(m_p_nd));
      }

      inline void
      dec(false_type)
      { inc(true_type()); }

      void
      dec(true_type)
      {
	if (m_p_nd->m_type == pat_trie_head_node_type)
	  {
	    m_p_nd = static_cast<head_pointer>(m_p_nd)->m_p_max;
	    return;
	  }

	node_pointer p_y = m_p_nd->m_p_parent;
	while (p_y->m_type != pat_trie_head_node_type && 
	       get_smaller_sibling(m_p_nd) == NULL)
	  {
	    m_p_nd = p_y;
	    p_y = p_y->m_p_parent;
	  }

	if (p_y->m_type == pat_trie_head_node_type)
	  {
	    m_p_nd = p_y;
	    return;
	  }
	m_p_nd = rightmost_descendant(get_smaller_sibling(m_p_nd));
      }

      inline static node_pointer
      get_larger_sibling(node_pointer p_nd)
      {
	internal_node_pointer p_parent =
	  static_cast<internal_node_pointer>(p_nd->m_p_parent);

	typename Internal_Node::iterator it = p_parent->begin();
	while (*it != p_nd)
	  ++it;

	typename Internal_Node::iterator next_it = it;
	++next_it;
	return ((next_it == p_parent->end())? NULL :* next_it);
      }

      inline static node_pointer
      get_smaller_sibling(node_pointer p_nd)
      {
	internal_node_pointer p_parent =
	  static_cast<internal_node_pointer>(p_nd->m_p_parent);

	typename Internal_Node::iterator it = p_parent->begin();

	if (*it == p_nd)
	  return (NULL);
	typename Internal_Node::iterator prev_it;
	do
	  {
	    prev_it = it;
	    ++it;
	    if (*it == p_nd)
	      return (*prev_it);
	  }
	while (true);

	_GLIBCXX_DEBUG_ASSERT(false);
	return (NULL);
      }

      inline static leaf_pointer
      leftmost_descendant(node_pointer p_nd)
      {
	if (p_nd->m_type == pat_trie_leaf_node_type)
	  return static_cast<leaf_pointer>(p_nd);
	return static_cast<internal_node_pointer>(p_nd)->leftmost_descendant();
      }

      inline static leaf_pointer
      rightmost_descendant(node_pointer p_nd)
      {
	if (p_nd->m_type == pat_trie_leaf_node_type)
	  return static_cast<leaf_pointer>(p_nd);
	return static_cast<internal_node_pointer>(p_nd)->rightmost_descendant();
      }

    public:
      node_pointer m_p_nd;
    };

    // Iterator.
    template<typename Type_Traits,
	     class Node,
	     class Leaf,
	     class Head,
	     class Internal_Node,
	     bool Is_Forward_Iterator,
	     class Allocator>
    class pat_trie_it_ : 
      public PB_DS_CONST_IT_C_DEC

    {
    private:
      typedef
      typename Allocator::template rebind<
      Node>::other::pointer
      node_pointer;

      typedef
      typename Allocator::template rebind<
	Leaf>::other::const_pointer
      const_leaf_pointer;

      typedef
      typename Allocator::template rebind<
	Leaf>::other::pointer
      leaf_pointer;

      typedef
      typename Allocator::template rebind<
	Head>::other::pointer
      head_pointer;

      typedef
      typename Allocator::template rebind<
	Internal_Node>::other::pointer
      internal_node_pointer;

    public:
      typedef typename Type_Traits::value_type value_type;

      typedef typename Type_Traits::const_pointer const_pointer;

      typedef typename Type_Traits::pointer pointer;

      typedef typename Type_Traits::const_reference const_reference;

      typedef typename Type_Traits::reference reference;

      inline
      pat_trie_it_(node_pointer p_nd = NULL) : PB_DS_CONST_IT_C_DEC((node_pointer)p_nd)
      { }

      inline
      pat_trie_it_(const PB_DS_ODIR_IT_C_DEC& other) : PB_DS_CONST_IT_C_DEC(other.m_p_nd)
      { }

      inline
      PB_DS_IT_C_DEC& 
      operator=(const PB_DS_IT_C_DEC& other)
      {
	base_it_type::m_p_nd = other.m_p_nd;
	return *this;
      }

      inline
      PB_DS_IT_C_DEC& 
      operator=(const PB_DS_ODIR_IT_C_DEC& other)
      {
	base_it_type::m_p_nd = other.m_p_nd;
	return *this;
      }

      inline pointer
      operator->() const
      {
	_GLIBCXX_DEBUG_ASSERT(base_it_type::m_p_nd->m_type == pat_trie_leaf_node_type);

	return &static_cast<leaf_pointer>(base_it_type::m_p_nd)->value();
      }

      inline reference
      operator*() const
      {
	_GLIBCXX_DEBUG_ASSERT(base_it_type::m_p_nd->m_type == pat_trie_leaf_node_type);
	return static_cast<leaf_pointer>(base_it_type::m_p_nd)->value();
      }

      inline PB_DS_IT_C_DEC& 
      operator++()
      {
	PB_DS_CONST_IT_C_DEC::
	  operator++();
	return *this;
      }

      inline PB_DS_IT_C_DEC
      operator++(int)
      {
	PB_DS_IT_C_DEC ret_it(base_it_type::m_p_nd);
	operator++();
	return ret_it;
      }

      inline PB_DS_IT_C_DEC& 
      operator--()
      {
	PB_DS_CONST_IT_C_DEC::operator--();
	return *this;
      }

      inline PB_DS_IT_C_DEC
      operator--(int)
      {
	PB_DS_IT_C_DEC ret_it(base_it_type::m_p_nd);
	operator--();
	return ret_it;
      }

    protected:
      typedef PB_DS_CONST_IT_C_DEC base_it_type;

      friend class PB_DS_CLASS_C_DEC;
    };

#undef PB_DS_CONST_IT_C_DEC
#undef PB_DS_CONST_ODIR_IT_C_DEC
#undef PB_DS_IT_C_DEC
#undef PB_DS_ODIR_IT_C_DEC

  } // namespace detail
} // namespace pb_ds

#endif 

