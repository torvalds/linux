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
 * @file ranged_hash_fn.hpp
 * Contains a unified ranged hash functor, allowing the hash tables
 * to deal with a single class for ranged hashing.
 */

#ifndef PB_DS_RANGED_HASH_FN_HPP
#define PB_DS_RANGED_HASH_FN_HPP

#include <ext/pb_ds/detail/basic_types.hpp>
#include <utility>
#include <debug/debug.h>

namespace pb_ds
{
  namespace detail
  {
    template<typename Key, typename Hash_Fn, typename Allocator, 
	     typename Comb_Hash_Fn, bool Store_Hash>
    class ranged_hash_fn;

#define PB_DS_CLASS_T_DEC \
    template<typename Key, typename Hash_Fn, typename Allocator, \
	     typename Comb_Hash_Fn>

#define PB_DS_CLASS_C_DEC \
    ranged_hash_fn<Key,	Hash_Fn, Allocator, Comb_Hash_Fn, false>

    /**
     * Specialization 1
     * The client supplies a hash function and a ranged hash function,
     * and requests that hash values not be stored.
     **/
    template<typename Key, typename Hash_Fn, typename Allocator, 
	     typename Comb_Hash_Fn>
    class ranged_hash_fn< Key, Hash_Fn, Allocator, Comb_Hash_Fn, false> 
    : public Hash_Fn, public Comb_Hash_Fn
    {
    protected:
      typedef typename Allocator::size_type size_type;
      typedef Hash_Fn hash_fn_base;
      typedef Comb_Hash_Fn comb_hash_fn_base;
      typedef typename Allocator::template rebind< Key>::other key_allocator;
      typedef typename key_allocator::const_reference const_key_reference;

      ranged_hash_fn(size_type);

      ranged_hash_fn(size_type, const Hash_Fn&);

      ranged_hash_fn(size_type, const Hash_Fn&, const Comb_Hash_Fn&);

      void
      swap(PB_DS_CLASS_C_DEC&);

      void
      notify_resized(size_type);

      inline size_type
      operator()(const_key_reference) const;
    };

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    ranged_hash_fn(size_type size)
    { Comb_Hash_Fn::notify_resized(size); }

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    ranged_hash_fn(size_type size, const Hash_Fn& r_hash_fn) 
    : Hash_Fn(r_hash_fn)
    { Comb_Hash_Fn::notify_resized(size); }

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    ranged_hash_fn(size_type size, const Hash_Fn& r_hash_fn, 
		   const Comb_Hash_Fn& r_comb_hash_fn) 
    : Hash_Fn(r_hash_fn), Comb_Hash_Fn(r_comb_hash_fn)
    { comb_hash_fn_base::notify_resized(size); }

    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    swap(PB_DS_CLASS_C_DEC& other)
    {
      comb_hash_fn_base::swap(other);
      std::swap((Hash_Fn& )(*this), (Hash_Fn& )other);
    }

    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    notify_resized(size_type size)
    { comb_hash_fn_base::notify_resized(size); }

    PB_DS_CLASS_T_DEC
    inline typename PB_DS_CLASS_C_DEC::size_type
    PB_DS_CLASS_C_DEC::
    operator()(const_key_reference r_key) const
    { return (comb_hash_fn_base::operator()(hash_fn_base::operator()(r_key)));}

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_T_DEC \
    template<typename Key, typename Hash_Fn, typename Allocator, \
	     typename Comb_Hash_Fn>

#define PB_DS_CLASS_C_DEC \
    ranged_hash_fn<Key,Hash_Fn,	Allocator, Comb_Hash_Fn, true>

    /**
     * Specialization 2
     * The client supplies a hash function and a ranged hash function,
     * and requests that hash values be stored.
     **/
    template<typename Key, typename Hash_Fn, typename Allocator,
	     typename Comb_Hash_Fn>
    class ranged_hash_fn<Key, Hash_Fn, Allocator, Comb_Hash_Fn, true> 
    : public Hash_Fn, public Comb_Hash_Fn
    {
    protected:
      typedef typename Allocator::size_type size_type;
      typedef std::pair<size_type, size_type> comp_hash;
      typedef Hash_Fn hash_fn_base;
      typedef Comb_Hash_Fn comb_hash_fn_base;
      typedef typename Allocator::template rebind<Key>::other key_allocator;
      typedef typename key_allocator::const_reference const_key_reference;

      ranged_hash_fn(size_type);

      ranged_hash_fn(size_type, const Hash_Fn&);

      ranged_hash_fn(size_type, const Hash_Fn&, const Comb_Hash_Fn&);

      void
      swap(PB_DS_CLASS_C_DEC&);

      void
      notify_resized(size_type);

      inline comp_hash
      operator()(const_key_reference) const;

      inline comp_hash
      operator()(const_key_reference, size_type) const;
    };

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    ranged_hash_fn(size_type size)
    { Comb_Hash_Fn::notify_resized(size); }

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    ranged_hash_fn(size_type size, const Hash_Fn& r_hash_fn) :
      Hash_Fn(r_hash_fn)
    { Comb_Hash_Fn::notify_resized(size); }

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    ranged_hash_fn(size_type size, const Hash_Fn& r_hash_fn, 
		   const Comb_Hash_Fn& r_comb_hash_fn) 
    : Hash_Fn(r_hash_fn), Comb_Hash_Fn(r_comb_hash_fn)
    { comb_hash_fn_base::notify_resized(size); }

    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    swap(PB_DS_CLASS_C_DEC& other)
    {
      comb_hash_fn_base::swap(other);
      std::swap((Hash_Fn& )(*this), (Hash_Fn& )other);
    }

    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    notify_resized(size_type size)
    { comb_hash_fn_base::notify_resized(size); }

    PB_DS_CLASS_T_DEC
    inline typename PB_DS_CLASS_C_DEC::comp_hash
    PB_DS_CLASS_C_DEC::
    operator()(const_key_reference r_key) const
    {
      const size_type hash = hash_fn_base::operator()(r_key);
      return std::make_pair(comb_hash_fn_base::operator()(hash), hash);
    }

    PB_DS_CLASS_T_DEC
    inline typename PB_DS_CLASS_C_DEC::comp_hash
    PB_DS_CLASS_C_DEC::
    operator()
#ifdef _GLIBCXX_DEBUG
      (const_key_reference r_key, size_type hash) const
#else 
      (const_key_reference /*r_key*/, size_type hash) const
#endif
    {
      _GLIBCXX_DEBUG_ASSERT(hash == hash_fn_base::operator()(r_key));
      return std::make_pair(comb_hash_fn_base::operator()(hash), hash);
    }

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_T_DEC \
    template<typename Key, typename Allocator, typename Comb_Hash_Fn>

#define PB_DS_CLASS_C_DEC \
    ranged_hash_fn<Key,	null_hash_fn, Allocator, Comb_Hash_Fn, false>

    /**
     * Specialization 3
     * The client does not supply a hash function (by specifying
     * null_hash_fn as the Hash_Fn parameter), and requests that hash
     * values not be stored.
     **/
    template<typename Key, typename Allocator, typename Comb_Hash_Fn>
    class ranged_hash_fn<Key, null_hash_fn, Allocator, Comb_Hash_Fn, false> 
    : public null_hash_fn, public Comb_Hash_Fn
    {
    protected:
      typedef typename Allocator::size_type size_type;
      typedef Comb_Hash_Fn comb_hash_fn_base;

      ranged_hash_fn(size_type);

      ranged_hash_fn(size_type, const Comb_Hash_Fn&);

      ranged_hash_fn(size_type, const null_hash_fn&, const Comb_Hash_Fn&);

      void
      swap(PB_DS_CLASS_C_DEC&);
    };

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    ranged_hash_fn(size_type size)
    { Comb_Hash_Fn::notify_resized(size); }

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    ranged_hash_fn(size_type size, const Comb_Hash_Fn& r_comb_hash_fn) :
      Comb_Hash_Fn(r_comb_hash_fn)
    { }

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    ranged_hash_fn(size_type size, const null_hash_fn& r_null_hash_fn, 
		   const Comb_Hash_Fn& r_comb_hash_fn) 
    : Comb_Hash_Fn(r_comb_hash_fn)
    { }

    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    swap(PB_DS_CLASS_C_DEC& other)
    { comb_hash_fn_base::swap(other); }

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

#define PB_DS_CLASS_T_DEC \
    template<typename Key, typename Allocator, typename Comb_Hash_Fn>

#define PB_DS_CLASS_C_DEC \
    ranged_hash_fn<Key,	null_hash_fn, Allocator, Comb_Hash_Fn, true>

    /**
     * Specialization 4
     * The client does not supply a hash function (by specifying
     * null_hash_fn as the Hash_Fn parameter), and requests that hash
     * values be stored.
     **/
    template<typename Key, typename Allocator, typename Comb_Hash_Fn>
    class ranged_hash_fn<Key, null_hash_fn, Allocator, Comb_Hash_Fn, true> 
    : public null_hash_fn, public Comb_Hash_Fn
    {
    protected:
      typedef typename Allocator::size_type size_type;
      typedef Comb_Hash_Fn comb_hash_fn_base;

      ranged_hash_fn(size_type);

      ranged_hash_fn(size_type, const Comb_Hash_Fn&);

      ranged_hash_fn(size_type, const null_hash_fn&, const Comb_Hash_Fn&);

      void
      swap(PB_DS_CLASS_C_DEC&);
    };

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    ranged_hash_fn(size_type size)
    { Comb_Hash_Fn::notify_resized(size); }

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    ranged_hash_fn(size_type size, const Comb_Hash_Fn& r_comb_hash_fn) 
    : Comb_Hash_Fn(r_comb_hash_fn)
    { }

    PB_DS_CLASS_T_DEC
    PB_DS_CLASS_C_DEC::
    ranged_hash_fn(size_type size, const null_hash_fn& r_null_hash_fn, 
		   const Comb_Hash_Fn& r_comb_hash_fn) 
    : Comb_Hash_Fn(r_comb_hash_fn)
    { }

    PB_DS_CLASS_T_DEC
    void
    PB_DS_CLASS_C_DEC::
    swap(PB_DS_CLASS_C_DEC& other)
    { comb_hash_fn_base::swap(other); }

#undef PB_DS_CLASS_T_DEC
#undef PB_DS_CLASS_C_DEC

  } // namespace detail
} // namespace pb_ds

#endif 
