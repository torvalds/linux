// Helpers for ostream inserters -*- C++ -*-

// Copyright (C) 2007 Free Software Foundation, Inc.
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

/** @file ostream_insert.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _OSTREAM_INSERT_H
#define _OSTREAM_INSERT_H 1

#pragma GCC system_header

#include <iosfwd>

_GLIBCXX_BEGIN_NAMESPACE(std)

  template<typename _CharT, typename _Traits>
    inline void
    __ostream_write(basic_ostream<_CharT, _Traits>& __out,
		    const _CharT* __s, streamsize __n)
    {
      typedef basic_ostream<_CharT, _Traits>       __ostream_type;      
      typedef typename __ostream_type::ios_base    __ios_base;

      const streamsize __put = __out.rdbuf()->sputn(__s, __n);
      if (__put != __n)
	__out.setstate(__ios_base::badbit);
    }

  template<typename _CharT, typename _Traits>
    inline void
    __ostream_fill(basic_ostream<_CharT, _Traits>& __out, streamsize __n)
    {
      typedef basic_ostream<_CharT, _Traits>       __ostream_type;      
      typedef typename __ostream_type::ios_base    __ios_base;

      const _CharT __c = __out.fill();
      for (; __n > 0; --__n)
	{
	  const typename _Traits::int_type __put = __out.rdbuf()->sputc(__c);
	  if (_Traits::eq_int_type(__put, _Traits::eof()))
	    {
	      __out.setstate(__ios_base::badbit);
	      break;
	    }
	}
    }

  template<typename _CharT, typename _Traits>
    basic_ostream<_CharT, _Traits>&
    __ostream_insert(basic_ostream<_CharT, _Traits>& __out,
		     const _CharT* __s, streamsize __n)
    {
      typedef basic_ostream<_CharT, _Traits>       __ostream_type;
      typedef typename __ostream_type::ios_base    __ios_base;

      typename __ostream_type::sentry __cerb(__out);
      if (__cerb)
	{
	  try
	    {
	      const streamsize __w = __out.width();
	      if (__w > __n)
		{
		  const bool __left = ((__out.flags()
					& __ios_base::adjustfield)
				       == __ios_base::left);
		  if (!__left)
		    __ostream_fill(__out, __w - __n);
		  if (__out.good())
		    __ostream_write(__out, __s, __n);
		  if (__left && __out.good())
		    __ostream_fill(__out, __w - __n);
		}
	      else
		__ostream_write(__out, __s, __n);
	      __out.width(0);
	    }
	  catch(...)
	    { __out._M_setstate(__ios_base::badbit); }
	}
      return __out;
    }

  // Inhibit implicit instantiations for required instantiations,
  // which are defined via explicit instantiations elsewhere.
  // NB:  This syntax is a GNU extension.
#if _GLIBCXX_EXTERN_TEMPLATE
  extern template ostream& __ostream_insert(ostream&, const char*, streamsize);

#ifdef _GLIBCXX_USE_WCHAR_T
  extern template wostream& __ostream_insert(wostream&, const wchar_t*,
					     streamsize);
#endif
#endif

_GLIBCXX_END_NAMESPACE

#endif /* _OSTREAM_INSERT_H */
