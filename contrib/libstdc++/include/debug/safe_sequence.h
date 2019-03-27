// Safe sequence implementation  -*- C++ -*-

// Copyright (C) 2003, 2004, 2005, 2006
// Free Software Foundation, Inc.
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

/** @file debug/safe_sequence.h
 *  This file is a GNU debug extension to the Standard C++ Library.
 */

#ifndef _GLIBCXX_DEBUG_SAFE_SEQUENCE_H
#define _GLIBCXX_DEBUG_SAFE_SEQUENCE_H 1

#include <debug/debug.h>
#include <debug/macros.h>
#include <debug/functions.h>
#include <debug/safe_base.h>

namespace __gnu_debug
{
  template<typename _Iterator, typename _Sequence>
    class _Safe_iterator;

  /** A simple function object that returns true if the passed-in
   *  value is not equal to the stored value. It saves typing over
   *  using both bind1st and not_equal.
   */
  template<typename _Type>
    class _Not_equal_to
    {
      _Type __value;

    public:
      explicit _Not_equal_to(const _Type& __v) : __value(__v) { }

      bool
      operator()(const _Type& __x) const
      { return __value != __x; }
    };

  /** A function object that returns true when the given random access
      iterator is at least @c n steps away from the given iterator. */
  template<typename _Iterator>
    class _After_nth_from
    {
      typedef typename std::iterator_traits<_Iterator>::difference_type
      difference_type;

      _Iterator _M_base;
      difference_type _M_n;

    public:
      _After_nth_from(const difference_type& __n, const _Iterator& __base)
      : _M_base(__base), _M_n(__n) { }

      bool
      operator()(const _Iterator& __x) const
      { return __x - _M_base >= _M_n; }
    };

  /**
   * @brief Base class for constructing a "safe" sequence type that
   * tracks iterators that reference it.
   *
   * The class template %_Safe_sequence simplifies the construction of
   * "safe" sequences that track the iterators that reference the
   * sequence, so that the iterators are notified of changes in the
   * sequence that may affect their operation, e.g., if the container
   * invalidates its iterators or is destructed. This class template
   * may only be used by deriving from it and passing the name of the
   * derived class as its template parameter via the curiously
   * recurring template pattern. The derived class must have @c
   * iterator and @const_iterator types that are instantiations of
   * class template _Safe_iterator for this sequence. Iterators will
   * then be tracked automatically.
   */
  template<typename _Sequence>
    class _Safe_sequence : public _Safe_sequence_base
    {
    public:
      /** Invalidates all iterators @c x that reference this sequence,
	  are not singular, and for which @c pred(x) returns @c
	  true. The user of this routine should be careful not to make
	  copies of the iterators passed to @p pred, as the copies may
	  interfere with the invalidation. */
      template<typename _Predicate>
        void
        _M_invalidate_if(_Predicate __pred);

      /** Transfers all iterators that reference this memory location
	  to this sequence from whatever sequence they are attached
	  to. */
      template<typename _Iterator>
        void
        _M_transfer_iter(const _Safe_iterator<_Iterator, _Sequence>& __x);
    };

  template<typename _Sequence>
    template<typename _Predicate>
      void
      _Safe_sequence<_Sequence>::
      _M_invalidate_if(_Predicate __pred)
      {
        typedef typename _Sequence::iterator iterator;
        typedef typename _Sequence::const_iterator const_iterator;

	__gnu_cxx::__scoped_lock sentry(this->_M_get_mutex());
        for (_Safe_iterator_base* __iter = _M_iterators; __iter;)
	  {
	    iterator* __victim = static_cast<iterator*>(__iter);
	    __iter = __iter->_M_next;
	    if (!__victim->_M_singular())
	      {
		if (__pred(__victim->base()))
		  __victim->_M_invalidate_single();
	      }
	  }

        for (_Safe_iterator_base* __iter2 = _M_const_iterators; __iter2;)
	  {
	    const_iterator* __victim = static_cast<const_iterator*>(__iter2);
	    __iter2 = __iter2->_M_next;
	    if (!__victim->_M_singular())
	      {
		if (__pred(__victim->base()))
		  __victim->_M_invalidate_single();
	      }
	  }
      }

  template<typename _Sequence>
    template<typename _Iterator>
      void
      _Safe_sequence<_Sequence>::
      _M_transfer_iter(const _Safe_iterator<_Iterator, _Sequence>& __x)
      {
	_Safe_sequence_base* __from = __x._M_sequence;
	if (!__from)
	  return;

        typedef typename _Sequence::iterator iterator;
        typedef typename _Sequence::const_iterator const_iterator;

	__gnu_cxx::__scoped_lock sentry(this->_M_get_mutex());
        for (_Safe_iterator_base* __iter = __from->_M_iterators; __iter;)
	  {
	    iterator* __victim = static_cast<iterator*>(__iter);
	    __iter = __iter->_M_next;
	    if (!__victim->_M_singular() && __victim->base() == __x.base())
	      __victim->_M_attach_single(static_cast<_Sequence*>(this));
	  }

        for (_Safe_iterator_base* __iter2 = __from->_M_const_iterators; 
	     __iter2;)
	  {
	    const_iterator* __victim = static_cast<const_iterator*>(__iter2);
	    __iter2 = __iter2->_M_next;
	    if (!__victim->_M_singular() && __victim->base() == __x.base())
	      __victim->_M_attach_single(static_cast<_Sequence*>(this));
	  }
      }
} // namespace __gnu_debug

#endif
