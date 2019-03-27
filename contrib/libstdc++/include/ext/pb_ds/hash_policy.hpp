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
 * @file hash_policy.hpp
 * Contains hash-related policies.
 */

#ifndef PB_DS_HASH_POLICY_HPP
#define PB_DS_HASH_POLICY_HPP

#include <algorithm>
#include <vector>
#include <cmath>
#include <ext/pb_ds/exception.hpp>
#include <ext/pb_ds/detail/type_utils.hpp>
#include <ext/pb_ds/detail/hash_fn/mask_based_range_hashing.hpp>
#include <ext/pb_ds/detail/hash_fn/mod_based_range_hashing.hpp>
#include <ext/pb_ds/detail/resize_policy/hash_load_check_resize_trigger_size_base.hpp>

namespace pb_ds
{
  // A null hash function, indicating that the combining hash function
  // is actually a ranged hash function.
  struct null_hash_fn
  { };

  // A null probe function, indicating that the combining probe
  // function is actually a ranged probe function.
  struct null_probe_fn
  { };

#define PB_DS_CLASS_T_DEC template<typename Size_Type>
#define PB_DS_CLASS_C_DEC linear_probe_fn<Size_Type>

  // A probe sequence policy using fixed increments.
  template<typename Size_Type = size_t>
  class linear_probe_fn
  {
  public:
    typedef Size_Type size_type;

    void
    swap(PB_DS_CLASS_C_DEC& other);

  protected:
    // Returns the i-th offset from the hash value.
    inline size_type
    operator()(size_type i) const;
  };

#include <ext/pb_ds/detail/hash_fn/linear_probe_fn_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_T_DEC template<typename Size_Type>
#define PB_DS_CLASS_C_DEC quadratic_probe_fn<Size_Type>

  // A probe sequence policy using square increments.
  template<typename Size_Type = size_t>
  class quadratic_probe_fn
  {
  public:
    typedef Size_Type size_type;

    void
    swap(PB_DS_CLASS_C_DEC& other);

  protected:
    // Returns the i-th offset from the hash value.
    inline size_type
    operator()(size_type i) const;
  };

#include <ext/pb_ds/detail/hash_fn/quadratic_probe_fn_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_T_DEC template<typename Size_Type>
#define PB_DS_CLASS_C_DEC direct_mask_range_hashing<Size_Type>

  // A mask range-hashing class (uses a bit-mask).
  template<typename Size_Type = size_t>
  class direct_mask_range_hashing 
  : public detail::mask_based_range_hashing<Size_Type>
  {
  private:
    typedef detail::mask_based_range_hashing<Size_Type> mask_based_base;

  public:
    typedef Size_Type size_type;

    void
    swap(PB_DS_CLASS_C_DEC& other);

  protected:
    void
    notify_resized(size_type size);

    // Transforms the __hash value hash into a ranged-hash value
    // (using a bit-mask).
    inline size_type
    operator()(size_type hash) const;
  };

#include <ext/pb_ds/detail/hash_fn/direct_mask_range_hashing_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_T_DEC template<typename Size_Type>
#define PB_DS_CLASS_C_DEC direct_mod_range_hashing<Size_Type>

  // A mod range-hashing class (uses the modulo function).
  template<typename Size_Type = size_t>
  class direct_mod_range_hashing 
  : public detail::mod_based_range_hashing<Size_Type>
  {
  public:
    typedef Size_Type size_type;
      
    void
    swap(PB_DS_CLASS_C_DEC& other);

  protected:
    void
    notify_resized(size_type size);
      
    // Transforms the __hash value hash into a ranged-hash value
    // (using a modulo operation).
    inline size_type
    operator()(size_type hash) const;
      
  private:
    typedef detail::mod_based_range_hashing<size_type> mod_based_base;
  };

#include <ext/pb_ds/detail/hash_fn/direct_mod_range_hashing_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_T_DEC template<bool External_Load_Access, typename Size_Type>
#define PB_DS_CLASS_C_DEC hash_load_check_resize_trigger<External_Load_Access, Size_Type>
#define PB_DS_SIZE_BASE_C_DEC detail::hash_load_check_resize_trigger_size_base<Size_Type, External_Load_Access>

  // A resize trigger policy based on a load check. It keeps the
  // load factor between some load factors load_min and load_max.
  template<bool External_Load_Access = false, typename Size_Type = size_t>
  class hash_load_check_resize_trigger : private PB_DS_SIZE_BASE_C_DEC
  {
  public:
    typedef Size_Type size_type;

    enum
      {
	external_load_access = External_Load_Access
      };

    // Default constructor, or constructor taking load_min and
    // load_max load factors between which this policy will keep the
    // actual load.
    hash_load_check_resize_trigger(float load_min = 0.125,
				   float load_max = 0.5);

    void
    swap(hash_load_check_resize_trigger& other);

    virtual
    ~hash_load_check_resize_trigger();

    // Returns a pair of the minimal and maximal loads, respectively.
    inline std::pair<float, float>
    get_loads() const;

    // Sets the loads through a pair of the minimal and maximal
    // loads, respectively.
    void
    set_loads(std::pair<float, float> load_pair);

  protected:
    inline void
    notify_insert_search_start();

    inline void
    notify_insert_search_collision();

    inline void
    notify_insert_search_end();

    inline void
    notify_find_search_start();

    inline void
    notify_find_search_collision();

    inline void
    notify_find_search_end();

    inline void
    notify_erase_search_start();

    inline void
    notify_erase_search_collision();

    inline void
    notify_erase_search_end();

    // Notifies an element was inserted. The total number of entries
    // in the table is num_entries.
    inline void
    notify_inserted(size_type num_entries);

    inline void
    notify_erased(size_type num_entries);

    // Notifies the table was cleared.
    void
    notify_cleared();

    // Notifies the table was resized as a result of this object's
    // signifying that a resize is needed.
    void
    notify_resized(size_type new_size);

    void
    notify_externally_resized(size_type new_size);

    inline bool
    is_resize_needed() const;

    inline bool
    is_grow_needed(size_type size, size_type num_entries) const;

  private:
    virtual void
    do_resize(size_type new_size);

    typedef PB_DS_SIZE_BASE_C_DEC size_base;

#ifdef _GLIBCXX_DEBUG
    void
    assert_valid() const;
#endif 

    float 	m_load_min;
    float 	m_load_max;
    size_type 	m_next_shrink_size;
    size_type 	m_next_grow_size;
    bool 	m_resize_needed;
  };

#include <ext/pb_ds/detail/resize_policy/hash_load_check_resize_trigger_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC
#undef PB_DS_SIZE_BASE_C_DEC

#define PB_DS_CLASS_T_DEC template<bool External_Load_Access, typename Size_Type>
#define PB_DS_CLASS_C_DEC cc_hash_max_collision_check_resize_trigger<External_Load_Access, Size_Type>

  // A resize trigger policy based on collision checks. It keeps the
  // simulated load factor lower than some given load factor.
  template<bool External_Load_Access = false, typename Size_Type = size_t>
  class cc_hash_max_collision_check_resize_trigger
  {
  public:
    typedef Size_Type size_type;

    enum
      {
	external_load_access = External_Load_Access
      };

    // Default constructor, or constructor taking load, a __load
    // factor which it will attempt to maintain.
    cc_hash_max_collision_check_resize_trigger(float load = 0.5);

    void
    swap(PB_DS_CLASS_C_DEC& other);

    // Returns the current load.
    inline float
    get_load() const;

    // Sets the load; does not resize the container.
    void
    set_load(float load);

  protected:
    inline void
    notify_insert_search_start();

    inline void
    notify_insert_search_collision();

    inline void
    notify_insert_search_end();

    inline void
    notify_find_search_start();

    inline void
    notify_find_search_collision();

    inline void
    notify_find_search_end();

    inline void
    notify_erase_search_start();

    inline void
    notify_erase_search_collision();

    inline void
    notify_erase_search_end();

    inline void
    notify_inserted(size_type num_entries);

    inline void
    notify_erased(size_type num_entries);

    void
    notify_cleared();

    // Notifies the table was resized as a result of this object's
    // signifying that a resize is needed.
    void
    notify_resized(size_type new_size);

    void
    notify_externally_resized(size_type new_size);

    inline bool
    is_resize_needed() const;

    inline bool
    is_grow_needed(size_type size, size_type num_entries) const;

  private:
    void
    calc_max_num_coll();

    inline void
    calc_resize_needed();

    float 	m_load;
    size_type 	m_size;
    size_type 	m_num_col;
    size_type 	m_max_col;
    bool 	m_resize_needed;
  };

#include <ext/pb_ds/detail/resize_policy/cc_hash_max_collision_check_resize_trigger_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_T_DEC template<typename Size_Type>
#define PB_DS_CLASS_C_DEC hash_exponential_size_policy<Size_Type>

  // A size policy whose sequence of sizes form an exponential
  // sequence (typically powers of 2.
  template<typename Size_Type = size_t>
  class hash_exponential_size_policy
  {
  public:
    typedef Size_Type size_type;

    // Default constructor, or onstructor taking a start_size, or
    // constructor taking a start size and grow_factor. The policy
    // will use the sequence of sizes start_size, start_size*
    // grow_factor, start_size* grow_factor^2, ...
    hash_exponential_size_policy(size_type start_size = 8,
				 size_type grow_factor = 2);

    void
    swap(PB_DS_CLASS_C_DEC& other);

  protected:
    size_type
    get_nearest_larger_size(size_type size) const;

    size_type
    get_nearest_smaller_size(size_type size) const;

  private:
    size_type m_start_size;
    size_type m_grow_factor;
  };

#include <ext/pb_ds/detail/resize_policy/hash_exponential_size_policy_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_T_DEC
#define PB_DS_CLASS_C_DEC hash_prime_size_policy

  // A size policy whose sequence of sizes form a nearly-exponential
  // sequence of primes.
  class hash_prime_size_policy
  {
  public:
    // Size type.
    typedef size_t size_type;

    // Default constructor, or onstructor taking a start_size The
    // policy will use the sequence of sizes approximately
    // start_size, start_size* 2, start_size* 2^2, ...
    hash_prime_size_policy(size_type start_size = 8);

    inline void
    swap(PB_DS_CLASS_C_DEC& other);

  protected:
    size_type
    get_nearest_larger_size(size_type size) const;

    size_type
    get_nearest_smaller_size(size_type size) const;

  private:
    size_type m_start_size;
  };

#include <ext/pb_ds/detail/resize_policy/hash_prime_size_policy_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_T_DEC template<typename Size_Policy, typename Trigger_Policy, bool External_Size_Access, typename Size_Type>

#define PB_DS_CLASS_C_DEC hash_standard_resize_policy<Size_Policy, Trigger_Policy, External_Size_Access, Size_Type>

  // A resize policy which delegates operations to size and trigger policies.
  template<typename Size_Policy = hash_exponential_size_policy<>,
	   typename Trigger_Policy = hash_load_check_resize_trigger<>,
	   bool External_Size_Access = false,
	   typename Size_Type = size_t>
  class hash_standard_resize_policy 
  : public Size_Policy, public Trigger_Policy
  {
  public:
    typedef Size_Type 		size_type;
    typedef Trigger_Policy 	trigger_policy;
    typedef Size_Policy 	size_policy;

    enum
      {
	external_size_access = External_Size_Access
      };

    // Default constructor.
    hash_standard_resize_policy();

    // constructor taking some policies r_size_policy will be copied
    // by the Size_Policy object of this object.
    hash_standard_resize_policy(const Size_Policy& r_size_policy);

    // constructor taking some policies. r_size_policy will be
    // copied by the Size_Policy object of this
    // object. r_trigger_policy will be copied by the Trigger_Policy
    // object of this object.
    hash_standard_resize_policy(const Size_Policy& r_size_policy, 
				const Trigger_Policy& r_trigger_policy);

    virtual
    ~hash_standard_resize_policy();

    inline void
    swap(PB_DS_CLASS_C_DEC& other);

    // Access to the Size_Policy object used.
    Size_Policy& 
    get_size_policy();

    // Const access to the Size_Policy object used.
    const Size_Policy& 
    get_size_policy() const;

    // Access to the Trigger_Policy object used.
    Trigger_Policy& 
    get_trigger_policy();

    // Access to the Trigger_Policy object used.
    const Trigger_Policy& 
    get_trigger_policy() const;

    // Returns the actual size of the container.
    inline size_type
    get_actual_size() const;

    // Resizes the container to suggested_new_size, a suggested size
    // (the actual size will be determined by the Size_Policy
    // object).
    void
    resize(size_type suggested_new_size);

  protected:
    inline void
    notify_insert_search_start();

    inline void
    notify_insert_search_collision();

    inline void
    notify_insert_search_end();

    inline void
    notify_find_search_start();

    inline void
    notify_find_search_collision();

    inline void
    notify_find_search_end();

    inline void
    notify_erase_search_start();

    inline void
    notify_erase_search_collision();

    inline void
    notify_erase_search_end();

    inline void
    notify_inserted(size_type num_e);

    inline void
    notify_erased(size_type num_e);

    void
    notify_cleared();

    void
    notify_resized(size_type new_size);

    inline bool
    is_resize_needed() const;

    // Queries what the new size should be, when the container is
    // resized naturally. The current __size of the container is
    // size, and the number of used entries within the container is
    // num_used_e.
    size_type
    get_new_size(size_type size, size_type num_used_e) const;

  private:
    // Resizes to new_size.
    virtual void
    do_resize(size_type new_size);

    typedef Trigger_Policy trigger_policy_base;

    typedef Size_Policy size_policy_base;

    size_type m_size;
  };

#include <ext/pb_ds/detail/resize_policy/hash_standard_resize_policy_imp.hpp>

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

} // namespace pb_ds

#endif 
