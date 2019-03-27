// std::numpunct implementation details, generic version -*- C++ -*-

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

//
// ISO C++ 14882: 22.2.3.1.2  numpunct virtual functions
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

#include <locale>

_GLIBCXX_BEGIN_NAMESPACE(std)

  template<> 
    void
    numpunct<char>::_M_initialize_numpunct(__c_locale)
    {
      // "C" locale
      if (!_M_data)
	_M_data = new __numpunct_cache<char>;

      _M_data->_M_grouping = "";
      _M_data->_M_grouping_size = 0;
      _M_data->_M_use_grouping = false;

      _M_data->_M_decimal_point = '.';
      _M_data->_M_thousands_sep = ',';
      
      for (size_t __i = 0; __i < __num_base::_S_oend; ++__i)
	_M_data->_M_atoms_out[__i] = __num_base::_S_atoms_out[__i];
      
      for (size_t __i = 0; __i < __num_base::_S_iend; ++__i)
	_M_data->_M_atoms_in[__i] = __num_base::_S_atoms_in[__i];

      _M_data->_M_truename = "true";
      _M_data->_M_truename_size = 4;
      _M_data->_M_falsename = "false";
      _M_data->_M_falsename_size = 5;
    }

  template<> 
    numpunct<char>::~numpunct()
    { delete _M_data; }
      
#ifdef _GLIBCXX_USE_WCHAR_T
  template<> 
    void
    numpunct<wchar_t>::_M_initialize_numpunct(__c_locale)
    {
      // "C" locale
      if (!_M_data)
	_M_data = new __numpunct_cache<wchar_t>;

      _M_data->_M_grouping = "";
      _M_data->_M_grouping_size = 0;
      _M_data->_M_use_grouping = false;
      
      _M_data->_M_decimal_point = L'.';
      _M_data->_M_thousands_sep = L',';
      
      // Use ctype::widen code without the facet...
      for (size_t __i = 0; __i < __num_base::_S_oend; ++__i)
	_M_data->_M_atoms_out[__i] =
	  static_cast<wchar_t>(__num_base::_S_atoms_out[__i]);
      
      for (size_t __i = 0; __i < __num_base::_S_iend; ++__i)
	_M_data->_M_atoms_in[__i] =
	  static_cast<wchar_t>(__num_base::_S_atoms_in[__i]);

      _M_data->_M_truename = L"true";
      _M_data->_M_truename_size = 4;
      _M_data->_M_falsename = L"false";
      _M_data->_M_falsename_size = 5;
    }

  template<> 
    numpunct<wchar_t>::~numpunct()
    { delete _M_data; }
#endif

_GLIBCXX_END_NAMESPACE

