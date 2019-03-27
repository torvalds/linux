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
 * @file node.hpp
 * Contains an implementation struct for splay_tree_'s node.
 */

#ifndef PB_DS_SPLAY_TREE_NODE_HPP
#define PB_DS_SPLAY_TREE_NODE_HPP

namespace pb_ds
{
  namespace detail
  {
    template<typename Value_Type, class Metadata, class Allocator>
    struct splay_tree_node_
    {
    public:
      typedef Value_Type value_type;
      typedef Metadata metadata_type;

      typedef
      typename Allocator::template rebind<
      splay_tree_node_<Value_Type, Metadata, Allocator> >::other::pointer
      node_pointer;

      typedef
      typename Allocator::template rebind<metadata_type>::other::reference
      metadata_reference;

      typedef
      typename Allocator::template rebind<metadata_type>::other::const_reference
      const_metadata_reference;

#ifdef PB_DS_BIN_SEARCH_TREE_TRACE_
      void
      trace() const
      { std::cout << PB_DS_V2F(m_value) << "(" << m_metadata << ")"; }
#endif

      inline bool
      special() const
      { return m_special; }

      inline const_metadata_reference
      get_metadata() const
      { return m_metadata; }

      inline metadata_reference
      get_metadata()
      { return m_metadata; }

      value_type m_value;
      bool m_special;
      node_pointer m_p_left;
      node_pointer m_p_right;
      node_pointer m_p_parent;
      metadata_type m_metadata;
    };

    template<typename Value_Type, typename Allocator>
    struct splay_tree_node_<Value_Type, null_node_metadata, Allocator>
    {
    public:
      typedef Value_Type value_type;
      typedef null_node_metadata metadata_type;

      typedef
      typename Allocator::template rebind<
      splay_tree_node_<Value_Type, null_node_metadata, Allocator> >::other::pointer
      node_pointer;

      inline bool
      special() const
      { return m_special; }

#ifdef PB_DS_BIN_SEARCH_TREE_TRACE_
      void
      trace() const
      { std::cout << PB_DS_V2F(m_value); }
#endif 

      node_pointer m_p_left;
      node_pointer m_p_right;
      node_pointer m_p_parent;
      value_type m_value;
      bool m_special;
    };
  } // namespace detail
} // namespace pb_ds

#endif 
