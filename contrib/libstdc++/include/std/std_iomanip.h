// Standard stream manipulators -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2001, 2002, 2003, 2005
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

/** @file iomanip
 *  This is a Standard C++ Library header.
 */

//
// ISO C++ 14882: 27.6.3  Standard manipulators
//

#ifndef _GLIBCXX_IOMANIP
#define _GLIBCXX_IOMANIP 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <istream>
#include <functional>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // [27.6.3] standard manipulators
  // Also see DR 183.

  struct _Resetiosflags { ios_base::fmtflags _M_mask; };

  /**
   *  @brief  Manipulator for @c setf.
   *  @param  mask  A format flags mask.
   *
   *  Sent to a stream object, this manipulator resets the specified flags,
   *  via @e stream.setf(0,mask).
  */
  inline _Resetiosflags 
  resetiosflags(ios_base::fmtflags __mask)
  { 
    _Resetiosflags __x; 
    __x._M_mask = __mask; 
    return __x; 
  }

  template<typename _CharT, typename _Traits>
    inline basic_istream<_CharT,_Traits>& 
    operator>>(basic_istream<_CharT,_Traits>& __is, _Resetiosflags __f)
    { 
      __is.setf(ios_base::fmtflags(0), __f._M_mask); 
      return __is; 
    }

  template<typename _CharT, typename _Traits>
    inline basic_ostream<_CharT,_Traits>& 
    operator<<(basic_ostream<_CharT,_Traits>& __os, _Resetiosflags __f)
    { 
      __os.setf(ios_base::fmtflags(0), __f._M_mask); 
      return __os; 
    }


  struct _Setiosflags { ios_base::fmtflags _M_mask; };

  /**
   *  @brief  Manipulator for @c setf.
   *  @param  mask  A format flags mask.
   *
   *  Sent to a stream object, this manipulator sets the format flags
   *  to @a mask.
  */
  inline _Setiosflags 
  setiosflags(ios_base::fmtflags __mask)
  { 
    _Setiosflags __x; 
    __x._M_mask = __mask; 
    return __x; 
  }

  template<typename _CharT, typename _Traits>
    inline basic_istream<_CharT,_Traits>& 
    operator>>(basic_istream<_CharT,_Traits>& __is, _Setiosflags __f)
    { 
      __is.setf(__f._M_mask); 
      return __is; 
    }

  template<typename _CharT, typename _Traits>
    inline basic_ostream<_CharT,_Traits>& 
    operator<<(basic_ostream<_CharT,_Traits>& __os, _Setiosflags __f)
    { 
      __os.setf(__f._M_mask); 
      return __os; 
    }


  struct _Setbase { int _M_base; };

  /**
   *  @brief  Manipulator for @c setf.
   *  @param  base  A numeric base.
   *
   *  Sent to a stream object, this manipulator changes the
   *  @c ios_base::basefield flags to @c oct, @c dec, or @c hex when @a base
   *  is 8, 10, or 16, accordingly, and to 0 if @a base is any other value.
  */
  inline _Setbase 
  setbase(int __base)
  { 
    _Setbase __x; 
    __x._M_base = __base; 
    return __x; 
  }

  template<typename _CharT, typename _Traits>
    inline basic_istream<_CharT,_Traits>& 
    operator>>(basic_istream<_CharT,_Traits>& __is, _Setbase __f)
    {
      __is.setf(__f._M_base ==  8 ? ios_base::oct : 
	      __f._M_base == 10 ? ios_base::dec : 
	      __f._M_base == 16 ? ios_base::hex : 
	      ios_base::fmtflags(0), ios_base::basefield);
      return __is; 
    }
  
  template<typename _CharT, typename _Traits>
    inline basic_ostream<_CharT,_Traits>& 
    operator<<(basic_ostream<_CharT,_Traits>& __os, _Setbase __f)
    {
      __os.setf(__f._M_base ==  8 ? ios_base::oct : 
		__f._M_base == 10 ? ios_base::dec : 
		__f._M_base == 16 ? ios_base::hex : 
		ios_base::fmtflags(0), ios_base::basefield);
      return __os; 
    }
  

  template<typename _CharT> 
    struct _Setfill { _CharT _M_c; };

  /**
   *  @brief  Manipulator for @c fill.
   *  @param  c  The new fill character.
   *
   *  Sent to a stream object, this manipulator calls @c fill(c) for that
   *  object.
  */
  template<typename _CharT> 
    inline _Setfill<_CharT> 
    setfill(_CharT __c)
    { 
      _Setfill<_CharT> __x; 
      __x._M_c = __c; 
      return __x; 
    }

  template<typename _CharT, typename _Traits>
    inline basic_istream<_CharT,_Traits>& 
    operator>>(basic_istream<_CharT,_Traits>& __is, _Setfill<_CharT> __f)
    { 
      __is.fill(__f._M_c); 
      return __is; 
    }

  template<typename _CharT, typename _Traits>
    inline basic_ostream<_CharT,_Traits>& 
    operator<<(basic_ostream<_CharT,_Traits>& __os, _Setfill<_CharT> __f)
    { 
      __os.fill(__f._M_c); 
      return __os; 
    }


  struct _Setprecision { int _M_n; };

  /**
   *  @brief  Manipulator for @c precision.
   *  @param  n  The new precision.
   *
   *  Sent to a stream object, this manipulator calls @c precision(n) for
   *  that object.
  */
  inline _Setprecision 
  setprecision(int __n)
  { 
    _Setprecision __x; 
    __x._M_n = __n; 
    return __x; 
  }

  template<typename _CharT, typename _Traits>
    inline basic_istream<_CharT,_Traits>& 
    operator>>(basic_istream<_CharT,_Traits>& __is, _Setprecision __f)
    { 
      __is.precision(__f._M_n); 
      return __is; 
    }

  template<typename _CharT, typename _Traits>
    inline basic_ostream<_CharT,_Traits>& 
    operator<<(basic_ostream<_CharT,_Traits>& __os, _Setprecision __f)
    { 
      __os.precision(__f._M_n); 
      return __os; 
    }


  struct _Setw { int _M_n; };

  /**
   *  @brief  Manipulator for @c width.
   *  @param  n  The new width.
   *
   *  Sent to a stream object, this manipulator calls @c width(n) for
   *  that object.
  */
  inline _Setw 
  setw(int __n)
  { 
    _Setw __x; 
    __x._M_n = __n; 
    return __x; 
  }

  template<typename _CharT, typename _Traits>
    inline basic_istream<_CharT,_Traits>& 
    operator>>(basic_istream<_CharT,_Traits>& __is, _Setw __f)
    { 
      __is.width(__f._M_n); 
      return __is; 
    }

  template<typename _CharT, typename _Traits>
    inline basic_ostream<_CharT,_Traits>& 
    operator<<(basic_ostream<_CharT,_Traits>& __os, _Setw __f)
    { 
      __os.width(__f._M_n); 
      return __os; 
    }

  // Inhibit implicit instantiations for required instantiations,
  // which are defined via explicit instantiations elsewhere.  
  // NB:  This syntax is a GNU extension.
#if _GLIBCXX_EXTERN_TEMPLATE
  extern template ostream& operator<<(ostream&, _Setfill<char>);
  extern template ostream& operator<<(ostream&, _Setiosflags);
  extern template ostream& operator<<(ostream&, _Resetiosflags);
  extern template ostream& operator<<(ostream&, _Setbase);
  extern template ostream& operator<<(ostream&, _Setprecision);
  extern template ostream& operator<<(ostream&, _Setw);
  extern template istream& operator>>(istream&, _Setfill<char>);
  extern template istream& operator>>(istream&, _Setiosflags);
  extern template istream& operator>>(istream&, _Resetiosflags);
  extern template istream& operator>>(istream&, _Setbase);
  extern template istream& operator>>(istream&, _Setprecision);
  extern template istream& operator>>(istream&, _Setw);

#ifdef _GLIBCXX_USE_WCHAR_T
  extern template wostream& operator<<(wostream&, _Setfill<wchar_t>);
  extern template wostream& operator<<(wostream&, _Setiosflags);
  extern template wostream& operator<<(wostream&, _Resetiosflags);
  extern template wostream& operator<<(wostream&, _Setbase);
  extern template wostream& operator<<(wostream&, _Setprecision);
  extern template wostream& operator<<(wostream&, _Setw);
  extern template wistream& operator>>(wistream&, _Setfill<wchar_t>);
  extern template wistream& operator>>(wistream&, _Setiosflags);
  extern template wistream& operator>>(wistream&, _Resetiosflags);
  extern template wistream& operator>>(wistream&, _Setbase);
  extern template wistream& operator>>(wistream&, _Setprecision);
  extern template wistream& operator>>(wistream&, _Setw);
#endif
#endif

_GLIBCXX_END_NAMESPACE

#endif /* _GLIBCXX_IOMANIP */
