// std::codecvt implementation details, generic version -*- C++ -*-

// Copyright (C) 2002, 2005 Free Software Foundation, Inc.
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
// ISO C++ 14882: 22.2.1.5 - Template class codecvt
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

#include <locale>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // Specializations.
#ifdef _GLIBCXX_USE_WCHAR_T
  codecvt_base::result
  codecvt<wchar_t, char, mbstate_t>::
  do_out(state_type& __state, const intern_type* __from, 
	 const intern_type* __from_end, const intern_type*& __from_next,
	 extern_type* __to, extern_type* __to_end,
	 extern_type*& __to_next) const
  {
    result __ret = ok;
    // The conversion must be done using a temporary destination buffer
    // since it is not possible to pass the size of the buffer to wcrtomb
    state_type __tmp_state(__state);

    // The conversion must be done by calling wcrtomb in a loop rather
    // than using wcsrtombs because wcsrtombs assumes that the input is
    // zero-terminated.

    // Either we can upper bound the total number of external characters to
    // something smaller than __to_end - __to or the conversion must be done
    // using a temporary destination buffer since it is not possible to
    // pass the size of the buffer to wcrtomb
    if (MB_CUR_MAX * (__from_end - __from) - (__to_end - __to) <= 0)
      while (__from < __from_end)
	{
	  const size_t __conv = wcrtomb(__to, *__from, &__tmp_state);
	  if (__conv == static_cast<size_t>(-1))
	    {
	      __ret = error;
	      break;
	    }
	  __state = __tmp_state;
	  __to += __conv;
	  __from++;
	}
    else
      {
	extern_type __buf[MB_LEN_MAX];
	while (__from < __from_end && __to < __to_end)
	  {
	    const size_t __conv = wcrtomb(__buf, *__from, &__tmp_state);
	    if (__conv == static_cast<size_t>(-1))
	      {
		__ret = error;
		break;
	      }
	    else if (__conv > static_cast<size_t>(__to_end - __to))
	      {
		__ret = partial;
		break;
	      }
	    
	    memcpy(__to, __buf, __conv);
	    __state = __tmp_state;
	    __to += __conv;
	    __from++;
	  }
      }

    if (__ret == ok && __from < __from_end)
      __ret = partial;

    __from_next = __from;
    __to_next = __to;
    return __ret; 
  }
  
  codecvt_base::result
  codecvt<wchar_t, char, mbstate_t>::
  do_in(state_type& __state, const extern_type* __from, 
	const extern_type* __from_end, const extern_type*& __from_next,
	intern_type* __to, intern_type* __to_end,
	intern_type*& __to_next) const
  {
    result __ret = ok;
    // This temporary state object is neccessary so __state won't be modified
    // if [__from, __from_end) is a partial multibyte character.
    state_type __tmp_state(__state);

    // Conversion must be done by calling mbrtowc in a loop rather than
    // by calling mbsrtowcs because mbsrtowcs assumes that the input
    // sequence is zero-terminated.
    while (__from < __from_end && __to < __to_end)
      {
	size_t __conv = mbrtowc(__to, __from, __from_end - __from,
				&__tmp_state);
	if (__conv == static_cast<size_t>(-1))
	  {
	    __ret = error;
	    break;
	  }
	else if (__conv == static_cast<size_t>(-2))
	  {
	    // It is unclear what to return in this case (see DR 382).
	    __ret = partial;
	    break;
	  }
	else if (__conv == 0)
	  {
	    // XXX Probably wrong for stateful encodings
	    __conv = 1;
	    *__to = L'\0';
	  }

	__state = __tmp_state;
	__to++;
	__from += __conv;
      }

    // It is not clear that __from < __from_end implies __ret != ok
    // (see DR 382).
    if (__ret == ok && __from < __from_end)
      __ret = partial;

    __from_next = __from;
    __to_next = __to;
    return __ret; 
  }

  int 
  codecvt<wchar_t, char, mbstate_t>::
  do_encoding() const throw()
  {
    // XXX This implementation assumes that the encoding is
    // stateless and is either single-byte or variable-width.
    int __ret = 0;
    if (MB_CUR_MAX == 1)
      __ret = 1;
    return __ret;
  }  

  int 
  codecvt<wchar_t, char, mbstate_t>::
  do_max_length() const throw()
  {
    // XXX Probably wrong for stateful encodings.
    int __ret = MB_CUR_MAX;
    return __ret;
  }
  
  int 
  codecvt<wchar_t, char, mbstate_t>::
  do_length(state_type& __state, const extern_type* __from,
	    const extern_type* __end, size_t __max) const
  {
    int __ret = 0;
    state_type __tmp_state(__state);

    while (__from < __end && __max)
      {
	size_t __conv = mbrtowc(NULL, __from, __end - __from, &__tmp_state);
	if (__conv == static_cast<size_t>(-1))
	  {
	    // Invalid source character
	    break;
	  }
	else if (__conv == static_cast<size_t>(-2))
	  {
	    // Remainder of input does not form a complete destination
	    // character.
	    break;
	  }
	else if (__conv == 0)
	  {
	    // XXX Probably wrong for stateful encodings
	    __conv = 1;
	  }

	__state = __tmp_state;
	__from += __conv;
	__ret += __conv;
	__max--;
      }

    return __ret; 
  }
#endif

_GLIBCXX_END_NAMESPACE
