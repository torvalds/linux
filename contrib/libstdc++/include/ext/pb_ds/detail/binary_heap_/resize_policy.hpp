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
 * @file resize_policy.hpp
 * Contains an implementation class for a binary_heap.
 */

#ifndef PB_DS_BINARY_HEAP_RESIZE_POLICY_HPP
#define PB_DS_BINARY_HEAP_RESIZE_POLICY_HPP

#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {

#define PB_DS_CLASS_T_DEC template<typename Size_Type>

#define PB_DS_CLASS_C_DEC resize_policy<Size_Type>

    template<typename Size_Type>
    class resize_policy
    {
    public:
      typedef Size_Type size_type;

      enum
	{
	  min_size = 16
	};

    public:
      inline
      resize_policy();

      inline void
      swap(PB_DS_CLASS_C_DEC& other);

      inline bool
      resize_needed_for_grow(size_type size) const;

      inline bool
      resize_needed_for_shrink(size_type size) const;

      inline bool
      grow_needed(size_type size) const;

      inline bool
      shrink_needed(size_type size) const;

      inline size_type
      get_new_size_for_grow() const;

      inline size_type
      get_new_size_for_shrink() const;

      size_type
      get_new_size_for_arbitrary(size_type size) const;

      inline void
      notify_grow_resize();

      inline void
      notify_shrink_resize();

      void
      notify_arbitrary(size_type actual_size);

#ifdef _GLIBCXX_DEBUG
      void
      assert_valid() const;
#endif 

#ifdef PB_DS_BINARY_HEAP_TRACE_
      void
      trace() const;
#endif 

    private:
      enum
	{
	  ratio = 8,
	  factor = 2
	};

    private:
      size_type m_next_shrink_size;
      size_type m_next_grow_size;
    };

    PB_DS_CLASS_T_DEC
    inline
    PB_DS_CLASS_C_DEC::
    resize_policy() :
      m_next_shrink_size(0),
      m_next_grow_size(min_size)
    { _GLIBCXX_DEBUG_ONLY(assert_valid();) }

    PB_DS_CLASS_T_DEC
    inline void
    PB_DS_CLASS_C_DEC::
    swap(PB_DS_CLASS_C_DEC& other)
    {
      std::swap(m_next_shrink_size, other.m_next_shrink_size);
      std::swap(m_next_grow_size, other.m_next_grow_size);
    }

    PB_DS_CLASS_T_DEC
    inline bool
    PB_DS_CLASS_C_DEC::
    resize_needed_for_grow(size_type size) const
    {
      _GLIBCXX_DEBUG_ASSERT(size <= m_next_grow_size);
      return size == m_next_grow_size;
    }

    PB_DS_CLASS_T_DEC
    inline bool
    PB_DS_CLASS_C_DEC::
    resize_needed_for_shrink(size_type size) const
    {
      _GLIBCXX_DEBUG_ASSERT(size <= m_next_grow_size);
      return size == m_next_shrink_size;
    }

    PB_DS_CLASS_T_DEC
    inline typename PB_DS_CLASS_C_DEC::size_type
    PB_DS_CLASS_C_DEC::
    get_new_size_for_grow() const
    { return m_next_grow_size*  factor; }

    PB_DS_CLASS_T_DEC
    inline typename PB_DS_CLASS_C_DEC::size_type
    PB_DS_CLASS_C_DEC::
    get_new_size_for_shrink() const
    {
      const size_type half_size = m_next_grow_size / factor;
      return std::max(static_cast<size_type>(min_size), half_size);
    }

    PB_DS_CLASS_T_DEC
    inline typename PB_DS_CLASS_C_DEC::size_type
    PB_DS_CLASS_C_DEC::
    get_new_size_for_arbitrary(size_type size) const
    {
      size_type ret = min_size;
      while (ret < size)
	ret *= factor;
      return ret;
    }

    PB_DS_CLASS_T_DEC
    inline void
    PB_DS_CLASS_C_DEC::
    notify_grow_resize()
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      _GLIBCXX_DEBUG_ASSERT(m_next_grow_size >= min_size);
      m_next_grow_size *= factor;
      m_next_shrink_size = m_next_grow_size / ratio;
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
    }

    PB_DS_CLASS_T_DEC
    inline void
    PB_DS_CLASS_C_DEC::
    notify_shrink_resize()
    {
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
      m_next_shrink_size /= factor;
      if (m_next_shrink_size == 1)
	m_next_shrink_size = 0;

      m_next_grow_size =
	std::max(m_next_grow_size / factor, static_cast<size_type>(min_size));
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
    }

    PB_DS_CLASS_T_DEC
    inline void
    PB_DS_CLASS_C_DEC::
    notify_arbitrary(size_type actual_size)
    {
      m_next_grow_size = actual_size;
      m_next_shrink_size = m_next_grow_size / ratio;
      _GLIBCXX_DEBUG_ONLY(assert_valid();)
    }

#ifdef _GLIBCXX_DEBUG
    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    assert_valid() const
    {
      _GLIBCXX_DEBUG_ASSERT(m_next_shrink_size == 0 ||
		       m_next_shrink_size*  ratio == m_next_grow_size);

      _GLIBCXX_DEBUG_ASSERT(m_next_grow_size >= min_size);
    }
#endif 

#ifdef PB_DS_BINARY_HEAP_TRACE_
    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    trace() const
    {
      std::cerr << "shrink = " << m_next_shrink_size <<
	" grow = " << m_next_grow_size << std::endl;
    }
#endif 

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

} // namespace detail
} // namespace pb_ds

#endif 
