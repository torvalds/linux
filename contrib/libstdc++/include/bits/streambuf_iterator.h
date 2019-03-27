// Streambuf iterators

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
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

/** @file streambuf_iterator.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _STREAMBUF_ITERATOR_H
#define _STREAMBUF_ITERATOR_H 1

#pragma GCC system_header

#include <streambuf>
#include <debug/debug.h>

_GLIBCXX_BEGIN_NAMESPACE(std)
     
  // 24.5.3 Template class istreambuf_iterator
  /// Provides input iterator semantics for streambufs.
  template<typename _CharT, typename _Traits>
    class istreambuf_iterator
    : public iterator<input_iterator_tag, _CharT, typename _Traits::off_type,
		      _CharT*, _CharT&>
    {
    public:
      // Types:
      //@{
      /// Public typedefs
      typedef _CharT					char_type;
      typedef _Traits					traits_type;
      typedef typename _Traits::int_type		int_type;
      typedef basic_streambuf<_CharT, _Traits>		streambuf_type;
      typedef basic_istream<_CharT, _Traits>		istream_type;
      //@}

      template<typename _CharT2>
	friend typename __gnu_cxx::__enable_if<__is_char<_CharT2>::__value,
		                    ostreambuf_iterator<_CharT2> >::__type
	copy(istreambuf_iterator<_CharT2>, istreambuf_iterator<_CharT2>,
	     ostreambuf_iterator<_CharT2>);

      template<typename _CharT2>
	friend typename __gnu_cxx::__enable_if<__is_char<_CharT2>::__value, 
					       _CharT2*>::__type
	__copy_aux(istreambuf_iterator<_CharT2>, istreambuf_iterator<_CharT2>,
		   _CharT2*);

      template<typename _CharT2>
	friend typename __gnu_cxx::__enable_if<__is_char<_CharT2>::__value,
			            istreambuf_iterator<_CharT2> >::__type
	find(istreambuf_iterator<_CharT2>, istreambuf_iterator<_CharT2>,
	     const _CharT2&);

    private:
      // 24.5.3 istreambuf_iterator
      // p 1
      // If the end of stream is reached (streambuf_type::sgetc()
      // returns traits_type::eof()), the iterator becomes equal to
      // the "end of stream" iterator value.
      // NB: This implementation assumes the "end of stream" value
      // is EOF, or -1.
      mutable streambuf_type*	_M_sbuf;
      mutable int_type		_M_c;

    public:
      ///  Construct end of input stream iterator.
      istreambuf_iterator() throw()
      : _M_sbuf(0), _M_c(traits_type::eof()) { }

      ///  Construct start of input stream iterator.
      istreambuf_iterator(istream_type& __s) throw()
      : _M_sbuf(__s.rdbuf()), _M_c(traits_type::eof()) { }

      ///  Construct start of streambuf iterator.
      istreambuf_iterator(streambuf_type* __s) throw()
      : _M_sbuf(__s), _M_c(traits_type::eof()) { }

      ///  Return the current character pointed to by iterator.  This returns
      ///  streambuf.sgetc().  It cannot be assigned.  NB: The result of
      ///  operator*() on an end of stream is undefined.
      char_type
      operator*() const
      {
#ifdef _GLIBCXX_DEBUG_PEDANTIC
	// Dereferencing a past-the-end istreambuf_iterator is a
	// libstdc++ extension
	__glibcxx_requires_cond(!_M_at_eof(),
				_M_message(__gnu_debug::__msg_deref_istreambuf)
				._M_iterator(*this));
#endif
	return traits_type::to_char_type(_M_get());
      }

      /// Advance the iterator.  Calls streambuf.sbumpc().
      istreambuf_iterator&
      operator++()
      {
	__glibcxx_requires_cond(!_M_at_eof(),
				_M_message(__gnu_debug::__msg_inc_istreambuf)
				._M_iterator(*this));
	if (_M_sbuf)
	  {
	    _M_sbuf->sbumpc();
	    _M_c = traits_type::eof();
	  }
	return *this;
      }

      /// Advance the iterator.  Calls streambuf.sbumpc().
      istreambuf_iterator
      operator++(int)
      {
	__glibcxx_requires_cond(!_M_at_eof(),
				_M_message(__gnu_debug::__msg_inc_istreambuf)
				._M_iterator(*this));

	istreambuf_iterator __old = *this;
	if (_M_sbuf)
	  {
	    __old._M_c = _M_sbuf->sbumpc();
	    _M_c = traits_type::eof();
	  }
	return __old;
      }

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 110 istreambuf_iterator::equal not const
      // NB: there is also number 111 (NAD, Future) pending on this function.
      /// Return true both iterators are end or both are not end.
      bool
      equal(const istreambuf_iterator& __b) const
      {
	const bool __thiseof = _M_at_eof();
	const bool __beof = __b._M_at_eof();
	return ((__thiseof && __beof) || (!__thiseof && !__beof));
      }

    private:
      int_type
      _M_get() const
      {
	const int_type __eof = traits_type::eof();
	int_type __ret = __eof;
	if (_M_sbuf)
	  {
	    if (!traits_type::eq_int_type(_M_c, __eof))
	      __ret = _M_c;
	    else if (!traits_type::eq_int_type((__ret = _M_sbuf->sgetc()),
					       __eof))
	      _M_c = __ret;
	    else
	      _M_sbuf = 0;
	  }
	return __ret;
      }

      bool
      _M_at_eof() const
      {
	const int_type __eof = traits_type::eof();
	return traits_type::eq_int_type(_M_get(), __eof);
      }
    };

  template<typename _CharT, typename _Traits>
    inline bool
    operator==(const istreambuf_iterator<_CharT, _Traits>& __a,
	       const istreambuf_iterator<_CharT, _Traits>& __b)
    { return __a.equal(__b); }

  template<typename _CharT, typename _Traits>
    inline bool
    operator!=(const istreambuf_iterator<_CharT, _Traits>& __a,
	       const istreambuf_iterator<_CharT, _Traits>& __b)
    { return !__a.equal(__b); }

  /// Provides output iterator semantics for streambufs.
  template<typename _CharT, typename _Traits>
    class ostreambuf_iterator
    : public iterator<output_iterator_tag, void, void, void, void>
    {
    public:
      // Types:
      //@{
      /// Public typedefs
      typedef _CharT                           char_type;
      typedef _Traits                          traits_type;
      typedef basic_streambuf<_CharT, _Traits> streambuf_type;
      typedef basic_ostream<_CharT, _Traits>   ostream_type;
      //@}

      template<typename _CharT2>
	friend typename __gnu_cxx::__enable_if<__is_char<_CharT2>::__value,
		                    ostreambuf_iterator<_CharT2> >::__type
	copy(istreambuf_iterator<_CharT2>, istreambuf_iterator<_CharT2>,
	     ostreambuf_iterator<_CharT2>);

    private:
      streambuf_type*	_M_sbuf;
      bool		_M_failed;

    public:
      ///  Construct output iterator from ostream.
      ostreambuf_iterator(ostream_type& __s) throw ()
      : _M_sbuf(__s.rdbuf()), _M_failed(!_M_sbuf) { }

      ///  Construct output iterator from streambuf.
      ostreambuf_iterator(streambuf_type* __s) throw ()
      : _M_sbuf(__s), _M_failed(!_M_sbuf) { }

      ///  Write character to streambuf.  Calls streambuf.sputc().
      ostreambuf_iterator&
      operator=(_CharT __c)
      {
	if (!_M_failed &&
	    _Traits::eq_int_type(_M_sbuf->sputc(__c), _Traits::eof()))
	  _M_failed = true;
	return *this;
      }

      /// Return *this.
      ostreambuf_iterator&
      operator*()
      { return *this; }

      /// Return *this.
      ostreambuf_iterator&
      operator++(int)
      { return *this; }

      /// Return *this.
      ostreambuf_iterator&
      operator++()
      { return *this; }

      /// Return true if previous operator=() failed.
      bool
      failed() const throw()
      { return _M_failed; }

      ostreambuf_iterator&
      _M_put(const _CharT* __ws, streamsize __len)
      {
	if (__builtin_expect(!_M_failed, true)
	    && __builtin_expect(this->_M_sbuf->sputn(__ws, __len) != __len,
				false))
	  _M_failed = true;
	return *this;
      }
    };

  // Overloads for streambuf iterators.
  template<typename _CharT>
    typename __gnu_cxx::__enable_if<__is_char<_CharT>::__value,
    	                 	    ostreambuf_iterator<_CharT> >::__type
    copy(istreambuf_iterator<_CharT> __first,
	 istreambuf_iterator<_CharT> __last,
	 ostreambuf_iterator<_CharT> __result)
    {
      if (__first._M_sbuf && !__last._M_sbuf && !__result._M_failed)
	{
	  bool __ineof;
	  __copy_streambufs_eof(__first._M_sbuf, __result._M_sbuf, __ineof);
	  if (!__ineof)
	    __result._M_failed = true;
	}
      return __result;
    }

  template<typename _CharT>
    typename __gnu_cxx::__enable_if<__is_char<_CharT>::__value, 
    				    ostreambuf_iterator<_CharT> >::__type
    __copy_aux(_CharT* __first, _CharT* __last,
	       ostreambuf_iterator<_CharT> __result)
    {
      const streamsize __num = __last - __first;
      if (__num > 0)
	__result._M_put(__first, __num);
      return __result;
    }

  template<typename _CharT>
    typename __gnu_cxx::__enable_if<__is_char<_CharT>::__value,
				    ostreambuf_iterator<_CharT> >::__type
    __copy_aux(const _CharT* __first, const _CharT* __last,
	       ostreambuf_iterator<_CharT> __result)
    {
      const streamsize __num = __last - __first;
      if (__num > 0)
	__result._M_put(__first, __num);
      return __result;
    }

  template<typename _CharT>
    typename __gnu_cxx::__enable_if<__is_char<_CharT>::__value, 
    				    _CharT*>::__type
    __copy_aux(istreambuf_iterator<_CharT> __first,
	       istreambuf_iterator<_CharT> __last, _CharT* __result)
    {
      typedef istreambuf_iterator<_CharT>                  __is_iterator_type;
      typedef typename __is_iterator_type::traits_type     traits_type;
      typedef typename __is_iterator_type::streambuf_type  streambuf_type;
      typedef typename traits_type::int_type               int_type;

      if (__first._M_sbuf && !__last._M_sbuf)
	{
	  streambuf_type* __sb = __first._M_sbuf;
	  int_type __c = __sb->sgetc();
	  while (!traits_type::eq_int_type(__c, traits_type::eof()))
	    {
	      const streamsize __n = __sb->egptr() - __sb->gptr();
	      if (__n > 1)
		{
		  traits_type::copy(__result, __sb->gptr(), __n);
		  __sb->gbump(__n);
		  __result += __n;
		  __c = __sb->underflow();
		}
	      else
		{
		  *__result++ = traits_type::to_char_type(__c);
		  __c = __sb->snextc();
		}
	    }
	}
      return __result;
    }

  template<typename _CharT>
    typename __gnu_cxx::__enable_if<__is_char<_CharT>::__value,
		  		    istreambuf_iterator<_CharT> >::__type
    find(istreambuf_iterator<_CharT> __first,
	 istreambuf_iterator<_CharT> __last, const _CharT& __val)
    {
      typedef istreambuf_iterator<_CharT>                  __is_iterator_type;
      typedef typename __is_iterator_type::traits_type     traits_type;
      typedef typename __is_iterator_type::streambuf_type  streambuf_type;
      typedef typename traits_type::int_type               int_type;

      if (__first._M_sbuf && !__last._M_sbuf)
	{
	  const int_type __ival = traits_type::to_int_type(__val);
	  streambuf_type* __sb = __first._M_sbuf;
	  int_type __c = __sb->sgetc();
	  while (!traits_type::eq_int_type(__c, traits_type::eof())
		 && !traits_type::eq_int_type(__c, __ival))
	    {
	      streamsize __n = __sb->egptr() - __sb->gptr();
	      if (__n > 1)
		{
		  const _CharT* __p = traits_type::find(__sb->gptr(),
							__n, __val);
		  if (__p)
		    __n = __p - __sb->gptr();
		  __sb->gbump(__n);
		  __c = __sb->sgetc();
		}
	      else
		__c = __sb->snextc();
	    }

	  if (!traits_type::eq_int_type(__c, traits_type::eof()))
	    __first._M_c = __c;
	  else
	    __first._M_sbuf = 0;
	}
      return __first;
    }

_GLIBCXX_END_NAMESPACE

#endif
