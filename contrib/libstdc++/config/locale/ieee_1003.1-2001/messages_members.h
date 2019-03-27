// std::messages implementation details, IEEE 1003.1-200x version -*- C++ -*-

// Copyright (C) 2001 Free Software Foundation, Inc.
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

/** @file messages_members.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

//
// ISO C++ 14882: 22.2.7.1.2  messages virtual functions
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // Non-virtual member functions.
  template<typename _CharT>
    typename messages<_CharT>::catalog 
    messages<_CharT>::open(const basic_string<char>& __s, const locale& __loc, 
			   const char*) const
    { return this->do_open(__s, __loc); }

  // Virtual member functions.
  template<typename _CharT>
    messages<_CharT>::~messages()
    { }

  template<typename _CharT>
    typename messages<_CharT>::catalog 
    messages<_CharT>::do_open(const basic_string<char>& __s, 
			      const locale&) const
    { return reinterpret_cast<catalog>(catopen(__s.c_str(), NL_CAT_LOCALE)); }

  template<typename _CharT>
    typename messages<_CharT>::string_type  
    messages<_CharT>::do_get(catalog __c, int __setid, int __msgid, 
			     const string_type& __dfault) const
    {
      nl_catd __nlc = reinterpret_cast<nl_catd>(__c);
      char* __msg = catgets(__nlc, __setid, __msgid, 
			    _M_convert_to_char(__dfault));
      return _M_convert_from_char(__msg);
    }

  template<typename _CharT>
    void    
    messages<_CharT>::do_close(catalog __c) const 
    { catclose(reinterpret_cast<nl_catd>(__c)); }

_GLIBCXX_END_NAMESPACE
