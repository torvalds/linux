// std::time_get, std::time_put implementation, GNU version -*- C++ -*-

// Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
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

/** @file time_members.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

//
// ISO C++ 14882: 22.2.5.1.2 - time_get functions
// ISO C++ 14882: 22.2.5.3.2 - time_put functions
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

_GLIBCXX_BEGIN_NAMESPACE(std)

  template<typename _CharT>
    __timepunct<_CharT>::__timepunct(size_t __refs) 
    : facet(__refs), _M_data(NULL), _M_c_locale_timepunct(NULL), 
      _M_name_timepunct(_S_get_c_name())
    { _M_initialize_timepunct(); }

  template<typename _CharT>
    __timepunct<_CharT>::__timepunct(__cache_type* __cache, size_t __refs) 
    : facet(__refs), _M_data(__cache), _M_c_locale_timepunct(NULL), 
      _M_name_timepunct(_S_get_c_name())
    { _M_initialize_timepunct(); }

  template<typename _CharT>
    __timepunct<_CharT>::__timepunct(__c_locale __cloc, const char* __s,
				     size_t __refs) 
    : facet(__refs), _M_data(NULL), _M_c_locale_timepunct(NULL), 
      _M_name_timepunct(NULL)
    { 
      const size_t __len = std::strlen(__s) + 1;
      char* __tmp = new char[__len];
      std::memcpy(__tmp, __s, __len);
      _M_name_timepunct = __tmp;

      try
	{ _M_initialize_timepunct(__cloc); }
      catch(...)
	{
	  delete [] _M_name_timepunct;
	  __throw_exception_again;
	}
    }

  template<typename _CharT>
    __timepunct<_CharT>::~__timepunct()
    { 
      if (_M_name_timepunct != _S_get_c_name())
	delete [] _M_name_timepunct;
      delete _M_data; 
      _S_destroy_c_locale(_M_c_locale_timepunct); 
    }

_GLIBCXX_END_NAMESPACE
