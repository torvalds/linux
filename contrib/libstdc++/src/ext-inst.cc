// Explicit instantiation file.

// Copyright (C) 2001, 2002, 2004, 2006 Free Software Foundation, Inc.
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
// ISO C++ 14882:
//

#include <ext/rope>
#include <ext/stdio_filebuf.h>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  namespace
  {
    const int min_len = __detail::_S_max_rope_depth + 1;
  }

  template
    const unsigned long 
    rope<char, std::allocator<char> >::_S_min_len[min_len];

  template
    char
    rope<char, std::allocator<char> >::
    _S_fetch(_Rope_RopeRep<char, std::allocator<char> >*, size_type);

  template class stdio_filebuf<char>;

#ifdef _GLIBCXX_USE_WCHAR_T
  template
    const unsigned long 
    rope<wchar_t, std::allocator<wchar_t> >::_S_min_len[min_len];

  template
    wchar_t
    rope<wchar_t, std::allocator<wchar_t> >::
    _S_fetch(_Rope_RopeRep<wchar_t, std::allocator<wchar_t> >*, size_type);

  template class stdio_filebuf<wchar_t>;
#endif

_GLIBCXX_END_NAMESPACE
