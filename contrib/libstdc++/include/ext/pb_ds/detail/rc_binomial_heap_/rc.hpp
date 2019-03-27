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
 * @file rc.hpp
 * Contains a redundant (binary counter).
 */

#ifndef PB_DS_RC_HPP
#define PB_DS_RC_HPP

namespace pb_ds
{
  namespace detail
  {

#define PB_DS_CLASS_T_DEC \
    template<typename Node, class Allocator>

#define PB_DS_CLASS_C_DEC \
    rc<Node, Allocator>

    template<typename Node, class Allocator>
    class rc
    {
    private:
      typedef Allocator allocator;

      typedef typename allocator::size_type size_type;

      typedef Node node;

      typedef
      typename allocator::template rebind<
	node>::other::pointer
      node_pointer;

      typedef
      typename allocator::template rebind<
	node_pointer>::other::pointer
      entry_pointer;

      typedef
      typename allocator::template rebind<
	node_pointer>::other::const_pointer
      const_entry_pointer;

      enum
	{
	  max_entries = sizeof(size_type) << 3
	};

    public:
      typedef node_pointer entry;

      typedef const_entry_pointer const_iterator;

    public:
      rc();

      rc(const PB_DS_CLASS_C_DEC& other);

      inline void
      swap(PB_DS_CLASS_C_DEC& other);

      inline void
      push(entry p_nd);

      inline node_pointer
      top() const;

      inline void
      pop();

      inline bool
      empty() const;

      inline size_type
      size() const;

      void
      clear();

      const const_iterator
      begin() const;

      const const_iterator
      end() const;

#ifdef _GLIBCXX_DEBUG
      void
      assert_valid() const;
#endif 

#ifdef PB_DS_RC_BINOMIAL_HEAP_TRACE_
      void
      trace() const;
#endif 

    private:
      node_pointer m_a_entries[max_entries];

      size_type m_over_top;
    };

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    rc() : m_over_top(0)
    { _GLIBCXX_DEBUG_ONLY(assert_valid();) }

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    rc(const PB_DS_CLASS_C_DEC& other) : m_over_top(0)
    { _GLIBCXX_DEBUG_ONLY(assert_valid();) }

    PB_DS_CLASS_T_DEC
    inline void
    PB_DS_CLASS_C_DEC::
    swap(PB_DS_CLASS_C_DEC& other)
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ONLY(other.assert_valid();)

      const size_type over_top = std::max(m_over_top, other.m_over_top);

      for (size_type i = 0; i < over_top; ++i)
	std::swap(m_a_entries[i], other.m_a_entries[i]);

      std::swap(m_over_top, other.m_over_top);
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
     }

    PB_DS_CLASS_T_DEC
    inline void
    PB_DS_CLASS_C_DEC::
    push(entry p_nd)
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ASSERT(m_over_top < max_entries);
      m_a_entries[m_over_top++] = p_nd;
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
    }

    PB_DS_CLASS_T_DEC
    inline void
    PB_DS_CLASS_C_DEC::
    pop()
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ASSERT(!empty());
      --m_over_top;
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
    }

    PB_DS_CLASS_T_DEC
    inline typename PB_DS_CLASS_C_DEC::node_pointer
    PB_DS_CLASS_C_DEC::
    top() const
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ASSERT(!empty());
      return *(m_a_entries + m_over_top - 1);
    }

    PB_DS_CLASS_T_DEC
    inline bool
    PB_DS_CLASS_C_DEC::
    empty() const
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      return m_over_top == 0;
    }

    PB_DS_CLASS_T_DEC
    inline typename PB_DS_CLASS_C_DEC::size_type
    PB_DS_CLASS_C_DEC::
    size() const
    { return m_over_top; }

    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    clear()
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      m_over_top = 0;
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
    }

    PB_DS_CLASS_T_DEC
    const typename PB_DS_CLASS_C_DEC::const_iterator
    PB_DS_CLASS_C_DEC::
    begin() const
    { return& m_a_entries[0]; }

    PB_DS_CLASS_T_DEC
    const typename PB_DS_CLASS_C_DEC::const_iterator
    PB_DS_CLASS_C_DEC::
    end() const
    { return& m_a_entries[m_over_top]; }

#ifdef _GLIBCXX_DEBUG
    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    assert_valid() const
    { _GLIBCXX_DEBUG_ASSERT(m_over_top < max_entries); }
#endif 

#ifdef PB_DS_RC_BINOMIAL_HEAP_TRACE_
    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    trace() const
    {
      std::cout << "rc" << std::endl;
      for (size_type i = 0; i < m_over_top; ++i)
	std::cerr << m_a_entries[i] << std::endl;
      std::cout << std::endl;
    }
#endif 

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

} // namespace detail
} // namespace pb_ds

#endif 
