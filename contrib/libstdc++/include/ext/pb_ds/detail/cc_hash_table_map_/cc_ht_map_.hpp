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
 * @file cc_ht_map_.hpp
 * Contains an implementation class for cc_ht_map_.
 */

#include <utility>
#include <iterator>
#include <ext/pb_ds/detail/cond_dealtor.hpp>
#include <ext/pb_ds/tag_and_trait.hpp>
#include <ext/pb_ds/detail/hash_fn/ranged_hash_fn.hpp>
#include <ext/pb_ds/detail/types_traits.hpp>
#include <ext/pb_ds/exception.hpp>
#include <ext/pb_ds/detail/eq_fn/hash_eq_fn.hpp>
#ifdef _GLIBCXX_DEBUG
#include <ext/pb_ds/detail/map_debug_base.hpp>
#endif 
#ifdef PB_DS_HT_MAP_TRACE_
#include <iostream>
#endif 
#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {

#define PB_DS_CLASS_T_DEC \
    template<typename Key, typename Mapped, typename Hash_Fn, \
	     typename Eq_Fn, typename Allocator, bool Store_Hash, \
	     typename Comb_Hash_Fn, typename Resize_Policy>

#ifdef PB_DS_DATA_TRUE_INDICATOR
#define PB_DS_CLASS_NAME cc_ht_map_data_
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
#define PB_DS_CLASS_NAME cc_ht_map_no_data_
#endif 

#define PB_DS_CLASS_C_DEC \
    PB_DS_CLASS_NAME<Key, Mapped, Hash_Fn, Eq_Fn, Allocator,	\
		     Store_Hash, Comb_Hash_Fn, Resize_Policy>

#define PB_DS_HASH_EQ_FN_C_DEC \
    hash_eq_fn<Key, Eq_Fn, Allocator, Store_Hash>

#define PB_DS_RANGED_HASH_FN_C_DEC \
    ranged_hash_fn<Key,	Hash_Fn, Allocator, Comb_Hash_Fn, Store_Hash>

#define PB_DS_TYPES_TRAITS_C_DEC \
    types_traits<Key, Mapped, Allocator, Store_Hash>

#ifdef _GLIBCXX_DEBUG
#define PB_DS_MAP_DEBUG_BASE_C_DEC \
    map_debug_base<Key,	Eq_Fn, typename Allocator::template rebind<Key>::other::const_reference>
#endif 

#ifdef PB_DS_DATA_TRUE_INDICATOR
#define PB_DS_V2F(X) (X).first
#define PB_DS_V2S(X) (X).second
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
#define PB_DS_V2F(X) (X)
#define PB_DS_V2S(X) Mapped_Data()
#endif 

#define PB_DS_STATIC_ASSERT(UNIQUE, E) \
    typedef static_assert_dumclass<sizeof(static_assert<(bool)(E)>)> \
    UNIQUE##static_assert_type

    // <011i$i0|\|-<|-|4i|\|i|\|g |-|4$|-| 74813.
    template<typename Key,
	     typename Mapped,
	     typename Hash_Fn,
	     typename Eq_Fn,
	     typename Allocator,
	     bool Store_Hash,
	     typename Comb_Hash_Fn,
	     typename Resize_Policy >
    class PB_DS_CLASS_NAME:
#ifdef _GLIBCXX_DEBUG
      protected PB_DS_MAP_DEBUG_BASE_C_DEC,
#endif 
      public PB_DS_HASH_EQ_FN_C_DEC,
      public Resize_Policy,
      public PB_DS_RANGED_HASH_FN_C_DEC,
      public PB_DS_TYPES_TRAITS_C_DEC
    {
    private:
      typedef PB_DS_TYPES_TRAITS_C_DEC traits_base;
      typedef typename traits_base::comp_hash comp_hash;
      typedef typename traits_base::value_type value_type_;
      typedef typename traits_base::pointer pointer_;
      typedef typename traits_base::const_pointer const_pointer_;
      typedef typename traits_base::reference reference_;
      typedef typename traits_base::const_reference const_reference_;

      struct entry : public traits_base::stored_value_type
      {
	typename Allocator::template rebind<entry>::other::pointer m_p_next;
      };

      typedef cond_dealtor<entry, Allocator> cond_dealtor_t;

      typedef typename Allocator::template rebind<entry>::other entry_allocator;
      typedef typename entry_allocator::pointer entry_pointer;
      typedef typename entry_allocator::const_pointer const_entry_pointer;
      typedef typename entry_allocator::reference entry_reference;
      typedef typename entry_allocator::const_reference const_entry_reference;

      typedef typename Allocator::template rebind<entry_pointer>::other entry_pointer_allocator;
      typedef typename entry_pointer_allocator::pointer entry_pointer_array;

      typedef PB_DS_RANGED_HASH_FN_C_DEC ranged_hash_fn_base;
      typedef PB_DS_HASH_EQ_FN_C_DEC hash_eq_fn_base;
      typedef Resize_Policy resize_base;

#ifdef _GLIBCXX_DEBUG
      typedef PB_DS_MAP_DEBUG_BASE_C_DEC map_debug_base;
#endif 

#define PB_DS_GEN_POS std::pair<entry_pointer, typename Allocator::size_type>

#include <ext/pb_ds/detail/unordered_iterator/const_point_iterator.hpp>
#include <ext/pb_ds/detail/unordered_iterator/point_iterator.hpp>
#include <ext/pb_ds/detail/unordered_iterator/const_iterator.hpp>
#include <ext/pb_ds/detail/unordered_iterator/iterator.hpp>

#undef PB_DS_GEN_POS

    public:
      typedef Allocator allocator;
      typedef typename Allocator::size_type size_type;
      typedef typename Allocator::difference_type difference_type;
      typedef Hash_Fn hash_fn;
      typedef Eq_Fn eq_fn;
      typedef Comb_Hash_Fn comb_hash_fn;
      typedef Resize_Policy resize_policy;

      enum
	{
	  store_hash = Store_Hash
	};

      typedef typename traits_base::key_type key_type;
      typedef typename traits_base::key_pointer key_pointer;
      typedef typename traits_base::const_key_pointer const_key_pointer;
      typedef typename traits_base::key_reference key_reference;
      typedef typename traits_base::const_key_reference const_key_reference;
      typedef typename traits_base::mapped_type mapped_type;
      typedef typename traits_base::mapped_pointer mapped_pointer;
      typedef typename traits_base::const_mapped_pointer const_mapped_pointer;
      typedef typename traits_base::mapped_reference mapped_reference;
      typedef typename traits_base::const_mapped_reference const_mapped_reference;
      typedef typename traits_base::value_type value_type;
      typedef typename traits_base::pointer pointer;
      typedef typename traits_base::const_pointer const_pointer;
      typedef typename traits_base::reference reference;
      typedef typename traits_base::const_reference const_reference;

#ifdef PB_DS_DATA_TRUE_INDICATOR
      typedef point_iterator_ point_iterator;
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
      typedef const_point_iterator_ point_iterator;
#endif 

      typedef const_point_iterator_ const_point_iterator;

#ifdef PB_DS_DATA_TRUE_INDICATOR
      typedef iterator_ iterator;
#endif 

#ifdef PB_DS_DATA_FALSE_INDICATOR
      typedef const_iterator_ iterator;
#endif 

      typedef const_iterator_ const_iterator;

      PB_DS_CLASS_NAME();

      PB_DS_CLASS_NAME(const Hash_Fn&);

      PB_DS_CLASS_NAME(const Hash_Fn&, const Eq_Fn&);

      PB_DS_CLASS_NAME(const Hash_Fn&, const Eq_Fn&, const Comb_Hash_Fn&);

      PB_DS_CLASS_NAME(const Hash_Fn&, const Eq_Fn&, const Comb_Hash_Fn&, 
		       const Resize_Policy&);

      PB_DS_CLASS_NAME(const PB_DS_CLASS_C_DEC&);

      virtual
      ~PB_DS_CLASS_NAME();

      void
      swap(PB_DS_CLASS_C_DEC&);

      template<typename It>
      void
      copy_from_range(It, It);

      void
      initialize();

      inline size_type
      size() const;

      inline size_type
      max_size() const;

      inline bool
      empty() const;

      Hash_Fn& 
      get_hash_fn();

      const Hash_Fn& 
      get_hash_fn() const;

      Eq_Fn& 
      get_eq_fn();

      const Eq_Fn& 
      get_eq_fn() const;

      Comb_Hash_Fn& 
      get_comb_hash_fn();

      const Comb_Hash_Fn& 
      get_comb_hash_fn() const;

      Resize_Policy& 
      get_resize_policy();

      const Resize_Policy& 
      get_resize_policy() const;

      inline std::pair<point_iterator, bool>
      insert(const_reference r_val)
      { return insert_imp(r_val, traits_base::m_store_extra_indicator); }

      inline mapped_reference
      operator[](const_key_reference r_key)
      {
#ifdef PB_DS_DATA_TRUE_INDICATOR
	return (subscript_imp(r_key, traits_base::m_store_extra_indicator));
#else 
	insert(r_key);
	return traits_base::s_null_mapped;
#endif 
      }

      inline point_iterator
      find(const_key_reference);

      inline const_point_iterator
      find(const_key_reference) const;

      inline point_iterator
      find_end();

      inline const_point_iterator
      find_end() const;

      inline bool
      erase(const_key_reference);

      template<typename Pred>
      inline size_type
      erase_if(Pred);

      void
      clear();

      inline iterator
      begin();

      inline const_iterator
      begin() const;

      inline iterator
      end();

      inline const_iterator
      end() const;

#ifdef _GLIBCXX_DEBUG
      void
      assert_valid() const;
#endif 

#ifdef PB_DS_HT_MAP_TRACE_
      void
      trace() const;
#endif 

    private:
      void
      deallocate_all();

      inline bool
      do_resize_if_needed();

      inline void
      do_resize_if_needed_no_throw();

      void
      resize_imp(size_type new_size);

      void
      do_resize(size_type new_size);

      void
      resize_imp_no_exceptions(size_type, entry_pointer_array, size_type);

      inline entry_pointer
      resize_imp_no_exceptions_reassign_pointer(entry_pointer, entry_pointer_array, false_type);

      inline entry_pointer
      resize_imp_no_exceptions_reassign_pointer(entry_pointer, entry_pointer_array, true_type);

      void
      deallocate_links_in_list(entry_pointer);

      inline entry_pointer
      get_entry(const_reference, false_type);

      inline entry_pointer
      get_entry(const_reference, true_type);

      inline void
      rels_entry(entry_pointer);

#ifdef PB_DS_DATA_TRUE_INDICATOR
      inline mapped_reference
      subscript_imp(const_key_reference r_key, false_type)
      {
	_GLIBCXX_DEBUG_ONLY(assert_valid();)
        const size_type pos = ranged_hash_fn_base::operator()(r_key);
	entry_pointer p_e = m_entries[pos];
	resize_base::notify_insert_search_start();

	while (p_e != NULL 
	       && !hash_eq_fn_base::operator()(p_e->m_value.first, r_key))
	  {
	    resize_base::notify_insert_search_collision();
	    p_e = p_e->m_p_next;
	  }

	resize_base::notify_insert_search_end();
	if (p_e != NULL)
	  {
	    _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_exists(r_key);)
	    return (p_e->m_value.second);
	  }

	_GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_does_not_exist(r_key);)
	return insert_new_imp(value_type(r_key, mapped_type()), pos)->second;
      }

      inline mapped_reference
      subscript_imp(const_key_reference r_key, true_type)
      {
	_GLIBCXX_DEBUG_ONLY(assert_valid();)
	comp_hash pos_hash_pair = ranged_hash_fn_base::operator()(r_key);
	entry_pointer p_e = m_entries[pos_hash_pair.first];
	resize_base::notify_insert_search_start();
	while (p_e != NULL && 
	       !hash_eq_fn_base::operator()(p_e->m_value.first, p_e->m_hash, r_key, pos_hash_pair.second))
	  {
	    resize_base::notify_insert_search_collision();
	    p_e = p_e->m_p_next;
	  }

	resize_base::notify_insert_search_end();
	if (p_e != NULL)
	  {
	    _GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_exists(r_key);)
	    return p_e->m_value.second;
	  }

	_GLIBCXX_DEBUG_ONLY(map_debug_base::check_key_does_not_exist(r_key);)
	return insert_new_imp(value_type(r_key, mapped_type()), 
			      pos_hash_pair)->second;
      }
#endif 

      inline std::pair<point_iterator, bool>
      insert_imp(const_reference, false_type);

      inline std::pair<point_iterator, bool>
      insert_imp(const_reference, true_type);

      inline pointer
      insert_new_imp(const_reference r_val, size_type pos)
      {
	if (do_resize_if_needed())
	  pos = ranged_hash_fn_base::operator()(PB_DS_V2F(r_val));

	// Following lines might throw an exception.
	entry_pointer p_e = get_entry(r_val, traits_base::m_no_throw_copies_indicator);

	// At this point no exceptions can be thrown.
	p_e->m_p_next = m_entries[pos];
	m_entries[pos] = p_e;
	resize_base::notify_inserted(++m_num_used_e);

	_GLIBCXX_DEBUG_ONLY(map_debug_base::insert_new(PB_DS_V2F(r_val));)
	_GLIBCXX_DEBUG_ONLY(assert_valid();)
	return &p_e->m_value;
      }

      inline pointer
      insert_new_imp(const_reference r_val, comp_hash& r_pos_hash_pair)
      {
	// Following lines might throw an exception.
	if (do_resize_if_needed())
	  r_pos_hash_pair = ranged_hash_fn_base::operator()(PB_DS_V2F(r_val));

	entry_pointer p_e = get_entry(r_val, traits_base::m_no_throw_copies_indicator);

	// At this point no exceptions can be thrown.
	p_e->m_hash = r_pos_hash_pair.second;
	p_e->m_p_next = m_entries[r_pos_hash_pair.first];
	m_entries[r_pos_hash_pair.first] = p_e;
	resize_base::notify_inserted(++m_num_used_e);
	_GLIBCXX_DEBUG_ONLY(map_debug_base::insert_new(PB_DS_V2F(r_val));)
	_GLIBCXX_DEBUG_ONLY(assert_valid();)
	return &p_e->m_value;
      }

      inline pointer
      find_key_pointer(const_key_reference r_key, false_type)
      {
	entry_pointer p_e = m_entries[ranged_hash_fn_base::operator()(r_key)];
	resize_base::notify_find_search_start();
	while (p_e != NULL && 
	       !hash_eq_fn_base::operator()(PB_DS_V2F(p_e->m_value), r_key))
	  {
	    resize_base::notify_find_search_collision();
	    p_e = p_e->m_p_next;
	  }

	resize_base::notify_find_search_end();

#ifdef _GLIBCXX_DEBUG
	if (p_e == NULL)
	  map_debug_base::check_key_does_not_exist(r_key);
	else
	  map_debug_base::check_key_exists(r_key);
#endif 
	return &p_e->m_value;
      }

      inline pointer
      find_key_pointer(const_key_reference r_key, true_type)
      {
	comp_hash pos_hash_pair = ranged_hash_fn_base::operator()(r_key);
	entry_pointer p_e = m_entries[pos_hash_pair.first];
	resize_base::notify_find_search_start();
	while (p_e != NULL && 
	       !hash_eq_fn_base::operator()(PB_DS_V2F(p_e->m_value),
					    p_e->m_hash,
					    r_key, pos_hash_pair.second))
	  {
	    resize_base::notify_find_search_collision();
	    p_e = p_e->m_p_next;
	  }

	resize_base::notify_find_search_end();

#ifdef _GLIBCXX_DEBUG
	if (p_e == NULL)
	  map_debug_base::check_key_does_not_exist(r_key);
	else
	  map_debug_base::check_key_exists(r_key);
#endif 
	return &p_e->m_value;
      }

      inline bool
      erase_in_pos_imp(const_key_reference, size_type);

      inline bool
      erase_in_pos_imp(const_key_reference, const comp_hash&);

      inline void
      erase_entry_pointer(entry_pointer&);

#ifdef PB_DS_DATA_TRUE_INDICATOR
      void
      inc_it_state(pointer& r_p_value, 
		   std::pair<entry_pointer, size_type>& r_pos) const
      {
	inc_it_state((const_mapped_pointer& )r_p_value, r_pos);
      }
#endif 

      void
      inc_it_state(const_pointer& r_p_value, 
		   std::pair<entry_pointer, size_type>& r_pos) const
      {
	_GLIBCXX_DEBUG_ASSERT(r_p_value != NULL);
	r_pos.first = r_pos.first->m_p_next;
	if (r_pos.first != NULL)
	  {
	    r_p_value = &r_pos.first->m_value;
	    return;
	  }

	for (++r_pos.second; r_pos.second < m_num_e; ++r_pos.second)
	  if (m_entries[r_pos.second] != NULL)
	    {
	      r_pos.first = m_entries[r_pos.second];
	      r_p_value = &r_pos.first->m_value;
	      return;
	    }
	r_p_value = NULL;
      }

      void
      get_start_it_state(pointer& r_p_value, 
			 std::pair<entry_pointer, size_type>& r_pos) const
      {
	for (r_pos.second = 0; r_pos.second < m_num_e; ++r_pos.second)
	  if (m_entries[r_pos.second] != NULL)
	    {
	      r_pos.first = m_entries[r_pos.second];
	      r_p_value = &r_pos.first->m_value;
	      return;
	    }
	r_p_value = NULL;
      }

#ifdef _GLIBCXX_DEBUG
      void
      assert_entry_pointer_array_valid(const entry_pointer_array) const;

      void
      assert_entry_pointer_valid(const entry_pointer, true_type) const;

      void
      assert_entry_pointer_valid(const entry_pointer, false_type) const;
#endif 

#ifdef PB_DS_HT_MAP_TRACE_
      void
      trace_list(const_entry_pointer) const;
#endif 

    private:
#ifdef PB_DS_DATA_TRUE_INDICATOR
      friend class iterator_;
#endif 

      friend class const_iterator_;

      static entry_allocator 		s_entry_allocator;
      static entry_pointer_allocator 	s_entry_pointer_allocator;
      static iterator 			s_end_it;
      static const_iterator 		s_const_end_it;
      static point_iterator 		s_find_end_it;
      static const_point_iterator 	s_const_find_end_it;

      size_type 			m_num_e;
      size_type 			m_num_used_e;
      entry_pointer_array 		m_entries;

      enum
	{
	  store_hash_ok = !Store_Hash 
	                  || !is_same<Hash_Fn, pb_ds::null_hash_fn>::value
	};

      PB_DS_STATIC_ASSERT(sth, store_hash_ok);
    };

#include <ext/pb_ds/detail/cc_hash_table_map_/constructor_destructor_fn_imps.hpp>
#include <ext/pb_ds/detail/cc_hash_table_map_/entry_list_fn_imps.hpp>
#include <ext/pb_ds/detail/cc_hash_table_map_/find_fn_imps.hpp>
#include <ext/pb_ds/detail/cc_hash_table_map_/resize_fn_imps.hpp>
#include <ext/pb_ds/detail/cc_hash_table_map_/debug_fn_imps.hpp>
#include <ext/pb_ds/detail/cc_hash_table_map_/size_fn_imps.hpp>
#include <ext/pb_ds/detail/cc_hash_table_map_/policy_access_fn_imps.hpp>
#include <ext/pb_ds/detail/cc_hash_table_map_/erase_fn_imps.hpp>
#include <ext/pb_ds/detail/cc_hash_table_map_/iterators_fn_imps.hpp>
#include <ext/pb_ds/detail/cc_hash_table_map_/insert_fn_imps.hpp>
#include <ext/pb_ds/detail/cc_hash_table_map_/trace_fn_imps.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC
#undef PB_DS_HASH_EQ_FN_C_DEC
#undef PB_DS_RANGED_HASH_FN_C_DEC
#undef PB_DS_TYPES_TRAITS_C_DEC
#undef PB_DS_MAP_DEBUG_BASE_C_DEC
#undef PB_DS_CLASS_NAME
#undef PB_DS_V2F
#undef PB_DS_V2S
#undef PB_DS_STATIC_ASSERT

  } // namespace detail
} // namespace pb_ds

