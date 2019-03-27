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
 * @file tree_trace_base.hpp
 * Contains tree-related policies.
 */

#ifndef PB_DS_TREE_TRACE_BASE_HPP
#define PB_DS_TREE_TRACE_BASE_HPP

#ifdef PB_DS_TREE_TRACE

#include <ext/pb_ds/detail/basic_tree_policy/basic_tree_policy_base.hpp>
#include <ext/pb_ds/detail/basic_tree_policy/null_node_metadata.hpp>

namespace pb_ds
{

  namespace detail
  {

#ifdef PB_DS_TREE_TRACE

#define PB_DS_CLASS_T_DEC						\
    template<								\
						class Const_Node_Iterator, \
						class Node_Iterator,	\
						class Cmp_Fn,		\
						bool Node_Based,	\
						class Allocator>

#define PB_DS_CLASS_C_DEC						\
    tree_trace_base<							\
						Const_Node_Iterator,	\
						Node_Iterator,		\
						Cmp_Fn,			\
						Node_Based,		\
						Allocator>

#define PB_DS_BASE_C_DEC						\
    basic_tree_policy_base<				\
								Const_Node_Iterator, \
								Node_Iterator, \
								Allocator>

    template<typename Const_Node_Iterator,
	     class Node_Iterator,
	     class Cmp_Fn,
	     bool Node_Based,
	     class Allocator>
    class tree_trace_base : private PB_DS_BASE_C_DEC
    {
    public:
      void
      trace() const;

    private:
      typedef PB_DS_BASE_C_DEC base_type;

      typedef Const_Node_Iterator const_node_iterator;

      typedef typename Allocator::size_type size_type;

    private:
      void
      trace_node(const_node_iterator nd_it, size_type level) const;

      virtual bool
      empty() const = 0;

      virtual const_node_iterator
      node_begin() const = 0;

      virtual const_node_iterator
      node_end() const = 0;

      static void
      print_node_pointer(Const_Node_Iterator nd_it, integral_constant<int,true>);

      static void
      print_node_pointer(Const_Node_Iterator nd_it, integral_constant<int,false>);

      template<typename Metadata_>
      static void
      trace_it_metadata(Const_Node_Iterator nd_it, type_to_type<Metadata_>);

      static void
      trace_it_metadata(Const_Node_Iterator, type_to_type<null_node_metadata>);
    };

    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    trace() const
    {
      if (empty())
        return;

      trace_node(node_begin(), 0);
    }

    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    trace_node(const_node_iterator nd_it, size_type level) const
    {
      if (nd_it.get_r_child() != node_end())
        trace_node(nd_it.get_r_child(), level + 1);

      for (size_type i = 0; i < level; ++i)
        std::cerr << ' ';

      print_node_pointer(nd_it, integral_constant<int,Node_Based>());
      std::cerr << base_type::extract_key(*(*nd_it));

      typedef
        type_to_type<
	typename const_node_iterator::metadata_type>
        m_type_ind_t;

      trace_it_metadata(nd_it, m_type_ind_t());

      std::cerr << std::endl;

      if (nd_it.get_l_child() != node_end())
        trace_node(nd_it.get_l_child(), level + 1);
    }

    PB_DS_CLASS_T_DEC
    template<typename Metadata_>
    void
    PB_DS_CLASS_C_DEC::
    trace_it_metadata(Const_Node_Iterator nd_it, type_to_type<Metadata_>)
    {
      std::cerr << " (" <<
        static_cast<unsigned long>(nd_it.get_metadata()) << ") ";
    }

    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    trace_it_metadata(Const_Node_Iterator, type_to_type<null_node_metadata>)
    { }

    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    print_node_pointer(Const_Node_Iterator nd_it, integral_constant<int,true>)
    {
      std::cerr << nd_it.m_p_nd << " ";
    }

    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    print_node_pointer(Const_Node_Iterator nd_it, integral_constant<int,false>)
    {
      std::cerr <<* nd_it << " ";
    }

#undef PB_DS_CLASS_T_DEC

#undef PB_DS_CLASS_C_DEC

#undef PB_DS_BASE_C_DEC

#endif // #ifdef    PB_DS_TREE_TRACE

  } // namespace detail

} // namespace pb_ds

#endif // #ifdef PB_DS_TREE_TRACE

#endif // #ifndef PB_DS_TREE_TRACE_BASE_HPP

