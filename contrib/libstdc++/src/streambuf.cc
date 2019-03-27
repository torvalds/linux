// Stream buffer classes -*- C++ -*-

// Copyright (C) 2004, 2005, 2006 Free Software Foundation, Inc.
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

//
// ISO C++ 14882: 27.5  Stream buffers
//

#include <streambuf>

_GLIBCXX_BEGIN_NAMESPACE(std)

  template<>
    streamsize
    __copy_streambufs_eof(basic_streambuf<char>* __sbin,
			  basic_streambuf<char>* __sbout, bool& __ineof)
    {
      typedef basic_streambuf<char>::traits_type traits_type;
      streamsize __ret = 0;
      __ineof = true;
      traits_type::int_type __c = __sbin->sgetc();
      while (!traits_type::eq_int_type(__c, traits_type::eof()))
	{
	  const streamsize __n = __sbin->egptr() - __sbin->gptr();
	  if (__n > 1)
	    {
	      const streamsize __wrote = __sbout->sputn(__sbin->gptr(), __n);
	      __sbin->gbump(__wrote);
	      __ret += __wrote;
	      if (__wrote < __n)
		{
		  __ineof = false;
		  break;
		}
	      __c = __sbin->underflow();
	    }
	  else
	    {
	      __c = __sbout->sputc(traits_type::to_char_type(__c));
	      if (traits_type::eq_int_type(__c, traits_type::eof()))
		{
		  __ineof = false;
		  break;
		}
	      ++__ret;
	      __c = __sbin->snextc();
	    }
	}
      return __ret;
    }

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    streamsize
    __copy_streambufs_eof(basic_streambuf<wchar_t>* __sbin,
			  basic_streambuf<wchar_t>* __sbout, bool& __ineof)
    {
      typedef basic_streambuf<wchar_t>::traits_type traits_type;
      streamsize __ret = 0;
      __ineof = true;
      traits_type::int_type __c = __sbin->sgetc();
      while (!traits_type::eq_int_type(__c, traits_type::eof()))
	{
	  const streamsize __n = __sbin->egptr() - __sbin->gptr();
	  if (__n > 1)
	    {
	      const streamsize __wrote = __sbout->sputn(__sbin->gptr(), __n);
	      __sbin->gbump(__wrote);
	      __ret += __wrote;
	      if (__wrote < __n)
		{
		  __ineof = false;
		  break;
		}
	      __c = __sbin->underflow();
	    }
	  else
	    {
	      __c = __sbout->sputc(traits_type::to_char_type(__c));
	      if (traits_type::eq_int_type(__c, traits_type::eof()))
		{
		  __ineof = false;
		  break;
		}
	      ++__ret;
	      __c = __sbin->snextc();
	    }
	}
      return __ret;
    }
#endif

_GLIBCXX_END_NAMESPACE
