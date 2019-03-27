// MT-optimized allocator -*- C++ -*-

// Copyright (C) 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/** @file ext/mt_allocator.h
 *  This file is a GNU extension to the Standard C++ Library.
 */

#ifndef _MT_ALLOCATOR_H
#define _MT_ALLOCATOR_H 1

#include <new>
#include <cstdlib>
#include <bits/functexcept.h>
#include <ext/atomicity.h>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  using std::size_t;
  using std::ptrdiff_t;

  typedef void (*__destroy_handler)(void*);

  /// @brief  Base class for pool object.
  struct __pool_base
  {
    // Using short int as type for the binmap implies we are never
    // caching blocks larger than 32768 with this allocator.
    typedef unsigned short int _Binmap_type;

    // Variables used to configure the behavior of the allocator,
    // assigned and explained in detail below.
    struct _Tune
     {
      // Compile time constants for the default _Tune values.
      enum { _S_align = 8 };
      enum { _S_max_bytes = 128 };
      enum { _S_min_bin = 8 };
      enum { _S_chunk_size = 4096 - 4 * sizeof(void*) };
      enum { _S_max_threads = 4096 };
      enum { _S_freelist_headroom = 10 };

      // Alignment needed.
      // NB: In any case must be >= sizeof(_Block_record), that
      // is 4 on 32 bit machines and 8 on 64 bit machines.
      size_t	_M_align;
      
      // Allocation requests (after round-up to power of 2) below
      // this value will be handled by the allocator. A raw new/
      // call will be used for requests larger than this value.
      // NB: Must be much smaller than _M_chunk_size and in any
      // case <= 32768.
      size_t	_M_max_bytes; 

      // Size in bytes of the smallest bin.
      // NB: Must be a power of 2 and >= _M_align (and of course
      // much smaller than _M_max_bytes).
      size_t	_M_min_bin;

      // In order to avoid fragmenting and minimize the number of
      // new() calls we always request new memory using this
      // value. Based on previous discussions on the libstdc++
      // mailing list we have choosen the value below.
      // See http://gcc.gnu.org/ml/libstdc++/2001-07/msg00077.html
      // NB: At least one order of magnitude > _M_max_bytes. 
      size_t	_M_chunk_size;

      // The maximum number of supported threads. For
      // single-threaded operation, use one. Maximum values will
      // vary depending on details of the underlying system. (For
      // instance, Linux 2.4.18 reports 4070 in
      // /proc/sys/kernel/threads-max, while Linux 2.6.6 reports
      // 65534)
      size_t 	_M_max_threads;

      // Each time a deallocation occurs in a threaded application
      // we make sure that there are no more than
      // _M_freelist_headroom % of used memory on the freelist. If
      // the number of additional records is more than
      // _M_freelist_headroom % of the freelist, we move these
      // records back to the global pool.
      size_t 	_M_freelist_headroom;
      
      // Set to true forces all allocations to use new().
      bool 	_M_force_new; 
      
      explicit
      _Tune()
      : _M_align(_S_align), _M_max_bytes(_S_max_bytes), _M_min_bin(_S_min_bin),
      _M_chunk_size(_S_chunk_size), _M_max_threads(_S_max_threads), 
      _M_freelist_headroom(_S_freelist_headroom), 
      _M_force_new(std::getenv("GLIBCXX_FORCE_NEW") ? true : false)
      { }

      explicit
      _Tune(size_t __align, size_t __maxb, size_t __minbin, size_t __chunk, 
	    size_t __maxthreads, size_t __headroom, bool __force) 
      : _M_align(__align), _M_max_bytes(__maxb), _M_min_bin(__minbin),
      _M_chunk_size(__chunk), _M_max_threads(__maxthreads),
      _M_freelist_headroom(__headroom), _M_force_new(__force)
      { }
    };
    
    struct _Block_address
    {
      void* 			_M_initial;
      _Block_address* 		_M_next;
    };
    
    const _Tune&
    _M_get_options() const
    { return _M_options; }

    void
    _M_set_options(_Tune __t)
    { 
      if (!_M_init)
	_M_options = __t;
    }

    bool
    _M_check_threshold(size_t __bytes)
    { return __bytes > _M_options._M_max_bytes || _M_options._M_force_new; }

    size_t
    _M_get_binmap(size_t __bytes)
    { return _M_binmap[__bytes]; }

    const size_t
    _M_get_align()
    { return _M_options._M_align; }

    explicit 
    __pool_base() 
    : _M_options(_Tune()), _M_binmap(NULL), _M_init(false) { }

    explicit 
    __pool_base(const _Tune& __options)
    : _M_options(__options), _M_binmap(NULL), _M_init(false) { }

  private:
    explicit 
    __pool_base(const __pool_base&);

    __pool_base&
    operator=(const __pool_base&);

  protected:
    // Configuration options.
    _Tune 	       		_M_options;
    
    _Binmap_type* 		_M_binmap;

    // Configuration of the pool object via _M_options can happen
    // after construction but before initialization. After
    // initialization is complete, this variable is set to true.
    bool 			_M_init;
  };


  /**
   *  @brief  Data describing the underlying memory pool, parameterized on
   *  threading support.
   */
  template<bool _Thread>
    class __pool;

  /// Specialization for single thread.
  template<>
    class __pool<false> : public __pool_base
    {
    public:
      union _Block_record
      {
	// Points to the block_record of the next free block.
	_Block_record* 			_M_next;
      };

      struct _Bin_record
      {
	// An "array" of pointers to the first free block.
	_Block_record**			_M_first;

	// A list of the initial addresses of all allocated blocks.
	_Block_address*		     	_M_address;
      };
      
      void
      _M_initialize_once()
      {
	if (__builtin_expect(_M_init == false, false))
	  _M_initialize();
      }

      void
      _M_destroy() throw();

      char* 
      _M_reserve_block(size_t __bytes, const size_t __thread_id);
    
      void
      _M_reclaim_block(char* __p, size_t __bytes);
    
      size_t 
      _M_get_thread_id() { return 0; }
      
      const _Bin_record&
      _M_get_bin(size_t __which)
      { return _M_bin[__which]; }
      
      void
      _M_adjust_freelist(const _Bin_record&, _Block_record*, size_t)
      { }

      explicit __pool() 
      : _M_bin(NULL), _M_bin_size(1) { }

      explicit __pool(const __pool_base::_Tune& __tune) 
      : __pool_base(__tune), _M_bin(NULL), _M_bin_size(1) { }

    private:
      // An "array" of bin_records each of which represents a specific
      // power of 2 size. Memory to this "array" is allocated in
      // _M_initialize().
      _Bin_record*		 _M_bin;
      
      // Actual value calculated in _M_initialize().
      size_t 	       	     	_M_bin_size;     

      void
      _M_initialize();
  };
 
#ifdef __GTHREADS
  /// Specialization for thread enabled, via gthreads.h.
  template<>
    class __pool<true> : public __pool_base
    {
    public:
      // Each requesting thread is assigned an id ranging from 1 to
      // _S_max_threads. Thread id 0 is used as a global memory pool.
      // In order to get constant performance on the thread assignment
      // routine, we keep a list of free ids. When a thread first
      // requests memory we remove the first record in this list and
      // stores the address in a __gthread_key. When initializing the
      // __gthread_key we specify a destructor. When this destructor
      // (i.e. the thread dies) is called, we return the thread id to
      // the front of this list.
      struct _Thread_record
      {
	// Points to next free thread id record. NULL if last record in list.
	_Thread_record*			_M_next;
	
	// Thread id ranging from 1 to _S_max_threads.
	size_t                          _M_id;
      };
      
      union _Block_record
      {
	// Points to the block_record of the next free block.
	_Block_record*			_M_next;
	
	// The thread id of the thread which has requested this block.
	size_t                          _M_thread_id;
      };
      
      struct _Bin_record
      {
	// An "array" of pointers to the first free block for each
	// thread id. Memory to this "array" is allocated in
	// _S_initialize() for _S_max_threads + global pool 0.
	_Block_record**			_M_first;
	
	// A list of the initial addresses of all allocated blocks.
	_Block_address*		     	_M_address;

	// An "array" of counters used to keep track of the amount of
	// blocks that are on the freelist/used for each thread id.
	// - Note that the second part of the allocated _M_used "array"
	//   actually hosts (atomic) counters of reclaimed blocks:  in
	//   _M_reserve_block and in _M_reclaim_block those numbers are
	//   subtracted from the first ones to obtain the actual size
	//   of the "working set" of the given thread.
	// - Memory to these "arrays" is allocated in _S_initialize()
	//   for _S_max_threads + global pool 0.
	size_t*				_M_free;
	size_t*			        _M_used;
	
	// Each bin has its own mutex which is used to ensure data
	// integrity while changing "ownership" on a block.  The mutex
	// is initialized in _S_initialize().
	__gthread_mutex_t*              _M_mutex;
      };
      
      // XXX GLIBCXX_ABI Deprecated
      void
      _M_initialize(__destroy_handler);

      void
      _M_initialize_once()
      {
	if (__builtin_expect(_M_init == false, false))
	  _M_initialize();
      }

      void
      _M_destroy() throw();

      char* 
      _M_reserve_block(size_t __bytes, const size_t __thread_id);
    
      void
      _M_reclaim_block(char* __p, size_t __bytes);
    
      const _Bin_record&
      _M_get_bin(size_t __which)
      { return _M_bin[__which]; }
      
      void
      _M_adjust_freelist(const _Bin_record& __bin, _Block_record* __block_record, 
			 size_t __thread_id)
      {
	if (__gthread_active_p())
	  {
	    __block_record->_M_thread_id = __thread_id;
	    --__bin._M_free[__thread_id];
	    ++__bin._M_used[__thread_id];
	  }
      }

      // XXX GLIBCXX_ABI Deprecated
      void 
      _M_destroy_thread_key(void*);

      size_t 
      _M_get_thread_id();

      explicit __pool() 
      : _M_bin(NULL), _M_bin_size(1), _M_thread_freelist(NULL) 
      { }

      explicit __pool(const __pool_base::_Tune& __tune) 
      : __pool_base(__tune), _M_bin(NULL), _M_bin_size(1), 
      _M_thread_freelist(NULL) 
      { }

    private:
      // An "array" of bin_records each of which represents a specific
      // power of 2 size. Memory to this "array" is allocated in
      // _M_initialize().
      _Bin_record*		_M_bin;

      // Actual value calculated in _M_initialize().
      size_t 	       	     	_M_bin_size;

      _Thread_record* 		_M_thread_freelist;
      void*			_M_thread_freelist_initial;

      void
      _M_initialize();
    };
#endif

  template<template <bool> class _PoolTp, bool _Thread>
    struct __common_pool
    {
      typedef _PoolTp<_Thread> 		pool_type;
      
      static pool_type&
      _S_get_pool()
      { 
	static pool_type _S_pool;
	return _S_pool;
      }
    };

  template<template <bool> class _PoolTp, bool _Thread>
    struct __common_pool_base;

  template<template <bool> class _PoolTp>
    struct __common_pool_base<_PoolTp, false> 
    : public __common_pool<_PoolTp, false>
    {
      using  __common_pool<_PoolTp, false>::_S_get_pool;

      static void
      _S_initialize_once()
      {
	static bool __init;
	if (__builtin_expect(__init == false, false))
	  {
	    _S_get_pool()._M_initialize_once(); 
	    __init = true;
	  }
      }
    };

#ifdef __GTHREADS
  template<template <bool> class _PoolTp>
    struct __common_pool_base<_PoolTp, true>
    : public __common_pool<_PoolTp, true>
    {
      using  __common_pool<_PoolTp, true>::_S_get_pool;
      
      static void
      _S_initialize() 
      { _S_get_pool()._M_initialize_once(); }

      static void
      _S_initialize_once()
      { 
	static bool __init;
	if (__builtin_expect(__init == false, false))
	  {
	    if (__gthread_active_p())
	      {
		// On some platforms, __gthread_once_t is an aggregate.
		static __gthread_once_t __once = __GTHREAD_ONCE_INIT;
		__gthread_once(&__once, _S_initialize);
	      }

	    // Double check initialization. May be necessary on some
	    // systems for proper construction when not compiling with
	    // thread flags.
	    _S_get_pool()._M_initialize_once(); 
	    __init = true;
	  }
      }
    };
#endif

  /// @brief  Policy for shared __pool objects.
  template<template <bool> class _PoolTp, bool _Thread>
    struct __common_pool_policy : public __common_pool_base<_PoolTp, _Thread>
    {
      template<typename _Tp1, template <bool> class _PoolTp1 = _PoolTp, 
	       bool _Thread1 = _Thread>
        struct _M_rebind
        { typedef __common_pool_policy<_PoolTp1, _Thread1> other; };

      using  __common_pool_base<_PoolTp, _Thread>::_S_get_pool;
      using  __common_pool_base<_PoolTp, _Thread>::_S_initialize_once;
  };
 

  template<typename _Tp, template <bool> class _PoolTp, bool _Thread>
    struct __per_type_pool
    {
      typedef _Tp 			value_type;
      typedef _PoolTp<_Thread> 		pool_type;
      
      static pool_type&
      _S_get_pool()
      { 
	// Sane defaults for the _PoolTp.
	typedef typename pool_type::_Block_record _Block_record;
	const static size_t __a = (__alignof__(_Tp) >= sizeof(_Block_record)
				   ? __alignof__(_Tp) : sizeof(_Block_record));

	typedef typename __pool_base::_Tune _Tune;
	static _Tune _S_tune(__a, sizeof(_Tp) * 64,
			     sizeof(_Tp) * 2 >= __a ? sizeof(_Tp) * 2 : __a,
			     sizeof(_Tp) * size_t(_Tune::_S_chunk_size),
			     _Tune::_S_max_threads,
			     _Tune::_S_freelist_headroom,
			     std::getenv("GLIBCXX_FORCE_NEW") ? true : false);
	static pool_type _S_pool(_S_tune);
	return _S_pool;
      }
    };

  template<typename _Tp, template <bool> class _PoolTp, bool _Thread>
    struct __per_type_pool_base;

  template<typename _Tp, template <bool> class _PoolTp>
    struct __per_type_pool_base<_Tp, _PoolTp, false> 
    : public __per_type_pool<_Tp, _PoolTp, false> 
    {
      using  __per_type_pool<_Tp, _PoolTp, false>::_S_get_pool;

      static void
      _S_initialize_once()
      {
	static bool __init;
	if (__builtin_expect(__init == false, false))
	  {
	    _S_get_pool()._M_initialize_once(); 
	    __init = true;
	  }
      }
    };

 #ifdef __GTHREADS
 template<typename _Tp, template <bool> class _PoolTp>
    struct __per_type_pool_base<_Tp, _PoolTp, true> 
    : public __per_type_pool<_Tp, _PoolTp, true> 
    {
      using  __per_type_pool<_Tp, _PoolTp, true>::_S_get_pool;

      static void
      _S_initialize() 
      { _S_get_pool()._M_initialize_once(); }

      static void
      _S_initialize_once()
      { 
	static bool __init;
	if (__builtin_expect(__init == false, false))
	  {
	    if (__gthread_active_p())
	      {
		// On some platforms, __gthread_once_t is an aggregate.
		static __gthread_once_t __once = __GTHREAD_ONCE_INIT;
		__gthread_once(&__once, _S_initialize);
	      }

	    // Double check initialization. May be necessary on some
	    // systems for proper construction when not compiling with
	    // thread flags.
	    _S_get_pool()._M_initialize_once(); 
	    __init = true;
	  }
      }
    };
#endif

  /// @brief  Policy for individual __pool objects.
  template<typename _Tp, template <bool> class _PoolTp, bool _Thread>
    struct __per_type_pool_policy 
    : public __per_type_pool_base<_Tp, _PoolTp, _Thread>
    {
      template<typename _Tp1, template <bool> class _PoolTp1 = _PoolTp, 
	       bool _Thread1 = _Thread>
        struct _M_rebind
        { typedef __per_type_pool_policy<_Tp1, _PoolTp1, _Thread1> other; };

      using  __per_type_pool_base<_Tp, _PoolTp, _Thread>::_S_get_pool;
      using  __per_type_pool_base<_Tp, _PoolTp, _Thread>::_S_initialize_once;
  };


  /// @brief  Base class for _Tp dependent member functions.
  template<typename _Tp>
    class __mt_alloc_base 
    {
    public:
      typedef size_t                    size_type;
      typedef ptrdiff_t                 difference_type;
      typedef _Tp*                      pointer;
      typedef const _Tp*                const_pointer;
      typedef _Tp&                      reference;
      typedef const _Tp&                const_reference;
      typedef _Tp                       value_type;

      pointer
      address(reference __x) const
      { return &__x; }

      const_pointer
      address(const_reference __x) const
      { return &__x; }

      size_type
      max_size() const throw() 
      { return size_t(-1) / sizeof(_Tp); }

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 402. wrong new expression in [some_] allocator::construct
      void 
      construct(pointer __p, const _Tp& __val) 
      { ::new(__p) _Tp(__val); }

      void 
      destroy(pointer __p) { __p->~_Tp(); }
    };

#ifdef __GTHREADS
#define __thread_default true
#else
#define __thread_default false
#endif

  /**
   *  @brief  This is a fixed size (power of 2) allocator which - when
   *  compiled with thread support - will maintain one freelist per
   *  size per thread plus a "global" one. Steps are taken to limit
   *  the per thread freelist sizes (by returning excess back to
   *  the "global" list).
   *
   *  Further details:
   *  http://gcc.gnu.org/onlinedocs/libstdc++/ext/mt_allocator.html
   */
  template<typename _Tp, 
	   typename _Poolp = __common_pool_policy<__pool, __thread_default> >
    class __mt_alloc : public __mt_alloc_base<_Tp>
    {
    public:
      typedef size_t                    	size_type;
      typedef ptrdiff_t                 	difference_type;
      typedef _Tp*                      	pointer;
      typedef const _Tp*                	const_pointer;
      typedef _Tp&                      	reference;
      typedef const _Tp&                	const_reference;
      typedef _Tp                       	value_type;
      typedef _Poolp      			__policy_type;
      typedef typename _Poolp::pool_type	__pool_type;

      template<typename _Tp1, typename _Poolp1 = _Poolp>
        struct rebind
        { 
	  typedef typename _Poolp1::template _M_rebind<_Tp1>::other pol_type;
	  typedef __mt_alloc<_Tp1, pol_type> other;
	};

      __mt_alloc() throw() { }

      __mt_alloc(const __mt_alloc&) throw() { }

      template<typename _Tp1, typename _Poolp1>
        __mt_alloc(const __mt_alloc<_Tp1, _Poolp1>&) throw() { }

      ~__mt_alloc() throw() { }

      pointer
      allocate(size_type __n, const void* = 0);

      void
      deallocate(pointer __p, size_type __n);

      const __pool_base::_Tune
      _M_get_options()
      { 
	// Return a copy, not a reference, for external consumption.
	return __policy_type::_S_get_pool()._M_get_options();
      }
      
      void
      _M_set_options(__pool_base::_Tune __t)
      { __policy_type::_S_get_pool()._M_set_options(__t); }
    };

  template<typename _Tp, typename _Poolp>
    typename __mt_alloc<_Tp, _Poolp>::pointer
    __mt_alloc<_Tp, _Poolp>::
    allocate(size_type __n, const void*)
    {
      if (__builtin_expect(__n > this->max_size(), false))
	std::__throw_bad_alloc();

      __policy_type::_S_initialize_once();

      // Requests larger than _M_max_bytes are handled by operator
      // new/delete directly.
      __pool_type& __pool = __policy_type::_S_get_pool();
      const size_t __bytes = __n * sizeof(_Tp);
      if (__pool._M_check_threshold(__bytes))
	{
	  void* __ret = ::operator new(__bytes);
	  return static_cast<_Tp*>(__ret);
	}
      
      // Round up to power of 2 and figure out which bin to use.
      const size_t __which = __pool._M_get_binmap(__bytes);
      const size_t __thread_id = __pool._M_get_thread_id();
      
      // Find out if we have blocks on our freelist.  If so, go ahead
      // and use them directly without having to lock anything.
      char* __c;
      typedef typename __pool_type::_Bin_record _Bin_record;
      const _Bin_record& __bin = __pool._M_get_bin(__which);
      if (__bin._M_first[__thread_id])
	{
	  // Already reserved.
	  typedef typename __pool_type::_Block_record _Block_record;
	  _Block_record* __block_record = __bin._M_first[__thread_id];
	  __bin._M_first[__thread_id] = __block_record->_M_next;
	  
	  __pool._M_adjust_freelist(__bin, __block_record, __thread_id);
	  __c = reinterpret_cast<char*>(__block_record) + __pool._M_get_align();
	}
      else
	{
	  // Null, reserve.
	  __c = __pool._M_reserve_block(__bytes, __thread_id);
	}
      return static_cast<_Tp*>(static_cast<void*>(__c));
    }
  
  template<typename _Tp, typename _Poolp>
    void
    __mt_alloc<_Tp, _Poolp>::
    deallocate(pointer __p, size_type __n)
    {
      if (__builtin_expect(__p != 0, true))
	{
	  // Requests larger than _M_max_bytes are handled by
	  // operators new/delete directly.
	  __pool_type& __pool = __policy_type::_S_get_pool();
	  const size_t __bytes = __n * sizeof(_Tp);
	  if (__pool._M_check_threshold(__bytes))
	    ::operator delete(__p);
	  else
	    __pool._M_reclaim_block(reinterpret_cast<char*>(__p), __bytes);
	}
    }
  
  template<typename _Tp, typename _Poolp>
    inline bool
    operator==(const __mt_alloc<_Tp, _Poolp>&, const __mt_alloc<_Tp, _Poolp>&)
    { return true; }
  
  template<typename _Tp, typename _Poolp>
    inline bool
    operator!=(const __mt_alloc<_Tp, _Poolp>&, const __mt_alloc<_Tp, _Poolp>&)
    { return false; }

#undef __thread_default

_GLIBCXX_END_NAMESPACE

#endif
