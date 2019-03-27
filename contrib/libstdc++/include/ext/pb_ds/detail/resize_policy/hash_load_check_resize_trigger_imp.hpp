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
 * @file hash_load_check_resize_trigger_imp.hpp
 * Contains a resize trigger implementation.
 */

#define PB_DS_STATIC_ASSERT(UNIQUE, E)  \
  typedef detail::static_assert_dumclass<sizeof(detail::static_assert<bool(E)>)> UNIQUE##static_assert_type

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
hash_load_check_resize_trigger(float load_min, float load_max) 
: m_load_min(load_min), m_load_max(load_max), m_next_shrink_size(0),
  m_next_grow_size(0), m_resize_needed(false)
{ _GLIBCXX_DEBUG_ONLY(assert_valid();) }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
notify_find_search_start()
{ _GLIBCXX_DEBUG_ONLY(assert_valid();) }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
notify_find_search_collision()
{ _GLIBCXX_DEBUG_ONLY(assert_valid();) }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
notify_find_search_end()
{ _GLIBCXX_DEBUG_ONLY(assert_valid();) }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
notify_insert_search_start()
{ _GLIBCXX_DEBUG_ONLY(assert_valid();) }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
notify_insert_search_collision()
{ _GLIBCXX_DEBUG_ONLY(assert_valid();) }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
notify_insert_search_end()
{ _GLIBCXX_DEBUG_ONLY(assert_valid();) }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
notify_erase_search_start()
{ _GLIBCXX_DEBUG_ONLY(assert_valid();) }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
notify_erase_search_collision()
{ _GLIBCXX_DEBUG_ONLY(assert_valid();) }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
notify_erase_search_end()
{ _GLIBCXX_DEBUG_ONLY(assert_valid();) }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
notify_inserted(size_type num_entries)
{
  m_resize_needed = (num_entries >= m_next_grow_size);
  size_base::set_size(num_entries);
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
notify_erased(size_type num_entries)
{
  size_base::set_size(num_entries);
  m_resize_needed = num_entries <= m_next_shrink_size;
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
inline bool
PB_DS_CLASS_C_DEC::
is_resize_needed() const
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  return m_resize_needed;
}

PB_DS_CLASS_T_DEC
inline bool
PB_DS_CLASS_C_DEC::
is_grow_needed(size_type /*size*/, size_type num_entries) const
{
  _GLIBCXX_DEBUG_ASSERT(m_resize_needed);
  return num_entries >= m_next_grow_size;
}

PB_DS_CLASS_T_DEC
PB_DS_CLASS_C_DEC::
~hash_load_check_resize_trigger() { }

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
notify_resized(size_type new_size)
{
  m_resize_needed = false;
  m_next_grow_size = size_type(m_load_max * new_size - 1);
  m_next_shrink_size = size_type(m_load_min * new_size);

#ifdef PB_DS_HT_MAP_RESIZE_TRACE_
  std::cerr << "hlcrt::notify_resized " <<
    static_cast<unsigned long>(new_size) << "    " <<
    static_cast<unsigned long>(m_load_min) << "    " <<
    static_cast<unsigned long>(m_load_max) << "    " <<
    static_cast<unsigned long>(m_next_shrink_size) << " " <<
    static_cast<unsigned long>(m_next_grow_size) << "    " << std::endl;
#endif 

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
notify_externally_resized(size_type new_size)
{
  m_resize_needed = false;
  size_type new_grow_size = size_type(m_load_max * new_size - 1);
  size_type new_shrink_size = size_type(m_load_min * new_size );
  if (new_grow_size >= m_next_grow_size)
    {
      _GLIBCXX_DEBUG_ASSERT(new_shrink_size > m_next_shrink_size);
      m_next_grow_size = new_grow_size;
      _GLIBCXX_DEBUG_ONLY(assert_valid();)

#ifdef PB_DS_HT_MAP_RESIZE_TRACE_
	std::cerr << "hlcrt::notify_externally_resized1 " <<
        static_cast<unsigned long>(new_size) << "    " <<
        static_cast<unsigned long>(m_load_min) << "    " <<
        static_cast<unsigned long>(m_load_max) << "    " <<
        static_cast<unsigned long>(m_next_shrink_size) << " " <<
        static_cast<unsigned long>(m_next_grow_size) << "    " << std::endl;
#endif 
      return;
    }

  _GLIBCXX_DEBUG_ASSERT(new_shrink_size <= m_next_shrink_size);
  m_next_shrink_size = new_shrink_size;

#ifdef PB_DS_HT_MAP_RESIZE_TRACE_
  std::cerr << "hlcrt::notify_externally_resized2 " <<
    static_cast<unsigned long>(new_size) << "    " <<
    static_cast<unsigned long>(m_load_min) << "    " <<
    static_cast<unsigned long>(m_load_max) << "    " <<
    static_cast<unsigned long>(m_next_shrink_size) << " " <<
    static_cast<unsigned long>(m_next_grow_size) << "    " << std::endl;
#endif 

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
notify_cleared()
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  size_base::set_size(0);
  m_resize_needed = (0 < m_next_shrink_size);
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
swap(PB_DS_CLASS_C_DEC& other)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
    
  size_base::swap(other);
  std::swap(m_load_min, other.m_load_min);
  std::swap(m_load_max, other.m_load_max);
  std::swap(m_resize_needed, other.m_resize_needed);
  std::swap(m_next_grow_size, other.m_next_grow_size);
  std::swap(m_next_shrink_size, other.m_next_shrink_size);

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
  _GLIBCXX_DEBUG_ONLY(other.assert_valid();)
}

PB_DS_CLASS_T_DEC
inline std::pair<float, float>
PB_DS_CLASS_C_DEC::
get_loads() const
{
  PB_DS_STATIC_ASSERT(access, external_load_access);
  return std::make_pair(m_load_min, m_load_max);
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
set_loads(std::pair<float, float> load_pair)
{
  PB_DS_STATIC_ASSERT(access, external_load_access);
  const float old_load_min = m_load_min;
  const float old_load_max = m_load_max;
  const size_type old_next_shrink_size = m_next_shrink_size;
  const size_type old_next_grow_size = m_next_grow_size;
  const bool old_resize_needed = m_resize_needed;

  try
    {
      m_load_min = load_pair.first;
      m_load_max = load_pair.second;
      do_resize(static_cast<size_type>(size_base::get_size() / ((m_load_min + m_load_max) / 2)));
    }
  catch (...)
    {
      m_load_min = old_load_min;
      m_load_max = old_load_max;
      m_next_shrink_size = old_next_shrink_size;
      m_next_grow_size = old_next_grow_size;
      m_resize_needed = old_resize_needed;
      __throw_exception_again;
    }
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
do_resize(size_type)
{ abort(); }

#ifdef _GLIBCXX_DEBUG
PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
assert_valid() const
{
  _GLIBCXX_DEBUG_ASSERT(m_load_max > m_load_min);
  _GLIBCXX_DEBUG_ASSERT(m_next_grow_size >= m_next_shrink_size);
}
#endif 

#undef PB_DS_STATIC_ASSERT

