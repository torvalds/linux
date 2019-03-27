// Iostreams wrapper for stdio FILE* -*- C++ -*-

// Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
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

/** @file ext/stdio_sync_filebuf.h
 *  This file is a GNU extension to the Standard C++ Library.
 */

#ifndef _STDIO_SYNC_FILEBUF_H
#define _STDIO_SYNC_FILEBUF_H 1

#pragma GCC system_header

#include <streambuf>
#include <unistd.h>
#include <cstdio>

#ifdef _GLIBCXX_USE_WCHAR_T
#include <cwchar>
#endif

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  /// @brief  class stdio_sync_filebuf.
  template<typename _CharT, typename _Traits = std::char_traits<_CharT> >
    class stdio_sync_filebuf : public std::basic_streambuf<_CharT, _Traits>
    {
    public:
      // Types:
      typedef _CharT					char_type;
      typedef _Traits					traits_type;
      typedef typename traits_type::int_type		int_type;
      typedef typename traits_type::pos_type		pos_type;
      typedef typename traits_type::off_type		off_type;

    private:
      // Underlying stdio FILE
      std::__c_file* const _M_file;

      // Last character gotten. This is used when pbackfail is
      // called from basic_streambuf::sungetc()
      int_type _M_unget_buf;

    public:
      explicit
      stdio_sync_filebuf(std::__c_file* __f)
      : _M_file(__f), _M_unget_buf(traits_type::eof())
      { }

      /**
       *  @return  The underlying FILE*.
       *
       *  This function can be used to access the underlying "C" file pointer.
       *  Note that there is no way for the library to track what you do
       *  with the file, so be careful.
       */
      std::__c_file* const
      file() { return this->_M_file; }

    protected:
      int_type
      syncgetc();

      int_type
      syncungetc(int_type __c);

      int_type
      syncputc(int_type __c);

      virtual int_type
      underflow()
      {
	int_type __c = this->syncgetc();
	return this->syncungetc(__c);
      }

      virtual int_type
      uflow()
      {
	// Store the gotten character in case we need to unget it.
	_M_unget_buf = this->syncgetc();
	return _M_unget_buf;
      }

      virtual int_type
      pbackfail(int_type __c = traits_type::eof())
      {
	int_type __ret;
	const int_type __eof = traits_type::eof();

	// Check if the unget or putback was requested
	if (traits_type::eq_int_type(__c, __eof)) // unget
	  {
	    if (!traits_type::eq_int_type(_M_unget_buf, __eof))
	      __ret = this->syncungetc(_M_unget_buf);
	    else // buffer invalid, fail.
	      __ret = __eof;
	  }
	else // putback
	  __ret = this->syncungetc(__c);

	// The buffered character is no longer valid, discard it.
	_M_unget_buf = __eof;
	return __ret;
      }

      virtual std::streamsize
      xsgetn(char_type* __s, std::streamsize __n);

      virtual int_type
      overflow(int_type __c = traits_type::eof())
      {
	int_type __ret;
	if (traits_type::eq_int_type(__c, traits_type::eof()))
	  {
	    if (std::fflush(_M_file))
	      __ret = traits_type::eof();
	    else
	      __ret = traits_type::not_eof(__c);
	  }
	else
	  __ret = this->syncputc(__c);
	return __ret;
      }

      virtual std::streamsize
      xsputn(const char_type* __s, std::streamsize __n);

      virtual int
      sync()
      { return std::fflush(_M_file); }

      virtual std::streampos
      seekoff(std::streamoff __off, std::ios_base::seekdir __dir,
	      std::ios_base::openmode = std::ios_base::in | std::ios_base::out)
      {
	std::streampos __ret(std::streamoff(-1));
	int __whence;
	if (__dir == std::ios_base::beg)
	  __whence = SEEK_SET;
	else if (__dir == std::ios_base::cur)
	  __whence = SEEK_CUR;
	else
	  __whence = SEEK_END;
#ifdef _GLIBCXX_USE_LFS
	if (!fseeko64(_M_file, __off, __whence))
	  __ret = std::streampos(ftello64(_M_file));
#else
	if (!fseek(_M_file, __off, __whence))
	  __ret = std::streampos(std::ftell(_M_file));
#endif
	return __ret;
      }

      virtual std::streampos
      seekpos(std::streampos __pos,
	      std::ios_base::openmode __mode =
	      std::ios_base::in | std::ios_base::out)
      { return seekoff(std::streamoff(__pos), std::ios_base::beg, __mode); }
    };

  template<>
    inline stdio_sync_filebuf<char>::int_type
    stdio_sync_filebuf<char>::syncgetc()
    { return std::getc(_M_file); }

  template<>
    inline stdio_sync_filebuf<char>::int_type
    stdio_sync_filebuf<char>::syncungetc(int_type __c)
    { return std::ungetc(__c, _M_file); }

  template<>
    inline stdio_sync_filebuf<char>::int_type
    stdio_sync_filebuf<char>::syncputc(int_type __c)
    { return std::putc(__c, _M_file); }

  template<>
    inline std::streamsize
    stdio_sync_filebuf<char>::xsgetn(char* __s, std::streamsize __n)
    {
      std::streamsize __ret = std::fread(__s, 1, __n, _M_file);
      if (__ret > 0)
	_M_unget_buf = traits_type::to_int_type(__s[__ret - 1]);
      else
	_M_unget_buf = traits_type::eof();
      return __ret;
    }

  template<>
    inline std::streamsize
    stdio_sync_filebuf<char>::xsputn(const char* __s, std::streamsize __n)
    { return std::fwrite(__s, 1, __n, _M_file); }

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    inline stdio_sync_filebuf<wchar_t>::int_type
    stdio_sync_filebuf<wchar_t>::syncgetc()
    { return std::getwc(_M_file); }

  template<>
    inline stdio_sync_filebuf<wchar_t>::int_type
    stdio_sync_filebuf<wchar_t>::syncungetc(int_type __c)
    { return std::ungetwc(__c, _M_file); }

  template<>
    inline stdio_sync_filebuf<wchar_t>::int_type
    stdio_sync_filebuf<wchar_t>::syncputc(int_type __c)
    { return std::putwc(__c, _M_file); }

  template<>
    inline std::streamsize
    stdio_sync_filebuf<wchar_t>::xsgetn(wchar_t* __s, std::streamsize __n)
    {
      std::streamsize __ret = 0;
      const int_type __eof = traits_type::eof();
      while (__n--)
	{
	  int_type __c = this->syncgetc();
	  if (traits_type::eq_int_type(__c, __eof))
	    break;
	  __s[__ret] = traits_type::to_char_type(__c);
	  ++__ret;
	}

      if (__ret > 0)
	_M_unget_buf = traits_type::to_int_type(__s[__ret - 1]);
      else
	_M_unget_buf = traits_type::eof();
      return __ret;
    }

  template<>
    inline std::streamsize
    stdio_sync_filebuf<wchar_t>::xsputn(const wchar_t* __s,
					std::streamsize __n)
    {
      std::streamsize __ret = 0;
      const int_type __eof = traits_type::eof();
      while (__n--)
	{
	  if (traits_type::eq_int_type(this->syncputc(*__s++), __eof))
	    break;
	  ++__ret;
	}
      return __ret;
    }
#endif

#if _GLIBCXX_EXTERN_TEMPLATE
  extern template class stdio_sync_filebuf<char>;
#ifdef _GLIBCXX_USE_WCHAR_T
  extern template class stdio_sync_filebuf<wchar_t>;
#endif
#endif

_GLIBCXX_END_NAMESPACE

#endif
