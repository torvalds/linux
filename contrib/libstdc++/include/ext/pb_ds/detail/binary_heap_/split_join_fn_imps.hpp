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
 * @file split_join_fn_imps.hpp
 * Contains an implementation class for a binary_heap.
 */

PB_DS_CLASS_T_DEC
template<typename Pred>
void
PB_DS_CLASS_C_DEC::
split(Pred pred, PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)

    typedef
    typename entry_pred<
    value_type,
    Pred,
    simple_value,
    Allocator>::type
    pred_t;

  const size_type left = partition(pred_t(pred));

  _GLIBCXX_DEBUG_ASSERT(m_size >= left);

  const size_type ersd = m_size - left;

  _GLIBCXX_DEBUG_ASSERT(m_size >= ersd);

  const size_type actual_size =
    resize_policy::get_new_size_for_arbitrary(left);

  const size_type other_actual_size =
    other.get_new_size_for_arbitrary(ersd);

  entry_pointer a_entries = NULL;
  entry_pointer a_other_entries = NULL;

  try
    {
      a_entries = s_entry_allocator.allocate(actual_size);

      a_other_entries = s_entry_allocator.allocate(other_actual_size);
    }
  catch(...)
    {
      if (a_entries != NULL)
	s_entry_allocator.deallocate(a_entries, actual_size);

      if (a_other_entries != NULL)
	s_entry_allocator.deallocate(a_other_entries, other_actual_size);

      __throw_exception_again;
    };

  for (size_type i = 0; i < other.m_size; ++i)
    erase_at(other.m_a_entries, i, s_no_throw_copies_ind);

  _GLIBCXX_DEBUG_ASSERT(actual_size >= left);
  std::copy(m_a_entries, m_a_entries + left, a_entries);
  std::copy(m_a_entries + left, m_a_entries + m_size, a_other_entries);

  s_entry_allocator.deallocate(m_a_entries, m_actual_size);
  s_entry_allocator.deallocate(other.m_a_entries, other.m_actual_size);

  m_actual_size = actual_size;
  other.m_actual_size = other_actual_size;

  m_size = left;
  other.m_size = ersd;

  m_a_entries = a_entries;
  other.m_a_entries = a_other_entries;

  std::make_heap(m_a_entries, m_a_entries + m_size, static_cast<entry_cmp& >(*this));
  std::make_heap(other.m_a_entries, other.m_a_entries + other.m_size, static_cast<entry_cmp& >(other));

  resize_policy::notify_arbitrary(m_actual_size);
  other.notify_arbitrary(other.m_actual_size);

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
    }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
join(PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)

  const size_type len = m_size + other.m_size;
  const size_type actual_size = resize_policy::get_new_size_for_arbitrary(len);

  entry_pointer a_entries = NULL;
  entry_pointer a_other_entries = NULL;

  try
    {
      a_entries = s_entry_allocator.allocate(actual_size);
      a_other_entries = s_entry_allocator.allocate(resize_policy::min_size);
    }
  catch(...)
    {
      if (a_entries != NULL)
	s_entry_allocator.deallocate(a_entries, actual_size);

      if (a_other_entries != NULL)
	s_entry_allocator.deallocate(a_other_entries, resize_policy::min_size);

      __throw_exception_again;
    }

  std::copy(m_a_entries, m_a_entries + m_size, a_entries);
  std::copy(other.m_a_entries, other.m_a_entries + other.m_size, a_entries + m_size);

  s_entry_allocator.deallocate(m_a_entries, m_actual_size);
  m_a_entries = a_entries;
  m_size = len;
  m_actual_size = actual_size;

  resize_policy::notify_arbitrary(actual_size);

  std::make_heap(m_a_entries, m_a_entries + m_size, static_cast<entry_cmp& >(*this));

  s_entry_allocator.deallocate(other.m_a_entries, other.m_actual_size);
  other.m_a_entries = a_other_entries;
  other.m_size = 0;
  other.m_actual_size = resize_policy::min_size;

  other.notify_arbitrary(resize_policy::min_size);

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
}

