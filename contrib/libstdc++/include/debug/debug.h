// Debugging support implementation -*- C++ -*-

// Copyright (C) 2003, 2005, 2006
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

/** @file debug/debug.h
 *  This file is a GNU debug extension to the Standard C++ Library.
 */

#ifndef _GLIBCXX_DEBUG_MACRO_SWITCH_H
#define _GLIBCXX_DEBUG_MACRO_SWITCH_H 1

/** Macros and namespaces used by the implementation outside of debug
 *  wrappers to verify certain properties. The __glibcxx_requires_xxx
 *  macros are merely wrappers around the __glibcxx_check_xxx wrappers
 *  when we are compiling with debug mode, but disappear when we are
 *  in release mode so that there is no checking performed in, e.g.,
 *  the standard library algorithms.
*/

// Debug mode namespaces.
namespace std 
{ 
  namespace __debug { } 
}

namespace __gnu_cxx
{ 
  namespace __debug { };
}

namespace __gnu_debug
{
  using namespace std::__debug;
  using namespace __gnu_cxx::__debug;
}

#ifndef _GLIBCXX_DEBUG

# define _GLIBCXX_DEBUG_ASSERT(_Condition)
# define _GLIBCXX_DEBUG_PEDASSERT(_Condition)
# define _GLIBCXX_DEBUG_ONLY(_Statement) ;
# define __glibcxx_requires_cond(_Cond,_Msg)
# define __glibcxx_requires_valid_range(_First,_Last)
# define __glibcxx_requires_sorted(_First,_Last)
# define __glibcxx_requires_sorted_pred(_First,_Last,_Pred)
# define __glibcxx_requires_partitioned(_First,_Last,_Value)
# define __glibcxx_requires_partitioned_pred(_First,_Last,_Value,_Pred)
# define __glibcxx_requires_heap(_First,_Last)
# define __glibcxx_requires_heap_pred(_First,_Last,_Pred)
# define __glibcxx_requires_nonempty()
# define __glibcxx_requires_string(_String)
# define __glibcxx_requires_string_len(_String,_Len)
# define __glibcxx_requires_subscript(_N)

#else

# include <cstdlib>
# include <cstdio>
# include <debug/macros.h>

namespace std
{
  namespace __debug
  { 
    // Avoid the use of assert, because we're trying to keep the <cassert>
    // include out of the mix.
    inline void
    __replacement_assert(const char* __file, int __line, 
			 const char* __function, const char* __condition)
    {
      printf("%s:%d: %s: Assertion '%s' failed.\n", __file, __line,
	     __function, __condition);
      abort();
    }
  } // namespace __debug
} // namespace std

#define _GLIBCXX_DEBUG_ASSERT(_Condition)                                   \
  do 									    \
  {									    \
    if (! (_Condition))                                                     \
      std::__debug::__replacement_assert(__FILE__, __LINE__,		    \
					 __PRETTY_FUNCTION__, #_Condition); \
  } while (false)

#ifdef _GLIBCXX_DEBUG_PEDANTIC
# define _GLIBCXX_DEBUG_PEDASSERT(_Condition) _GLIBCXX_DEBUG_ASSERT(_Condition)
#else
# define _GLIBCXX_DEBUG_PEDASSERT(_Condition)
#endif
# define _GLIBCXX_DEBUG_ONLY(_Statement) _Statement

# define __glibcxx_requires_cond(_Cond,_Msg) _GLIBCXX_DEBUG_VERIFY(_Cond,_Msg)
# define __glibcxx_requires_valid_range(_First,_Last) \
     __glibcxx_check_valid_range(_First,_Last)
# define __glibcxx_requires_sorted(_First,_Last) \
     __glibcxx_check_sorted(_First,_Last)
# define __glibcxx_requires_sorted_pred(_First,_Last,_Pred) \
     __glibcxx_check_sorted_pred(_First,_Last,_Pred)
# define __glibcxx_requires_partitioned(_First,_Last,_Value)	\
     __glibcxx_check_partitioned(_First,_Last,_Value)
# define __glibcxx_requires_partitioned_pred(_First,_Last,_Value,_Pred) \
     __glibcxx_check_partitioned_pred(_First,_Last,_Value,_Pred)
# define __glibcxx_requires_heap(_First,_Last) \
     __glibcxx_check_heap(_First,_Last)
# define __glibcxx_requires_heap_pred(_First,_Last,_Pred) \
     __glibcxx_check_heap_pred(_First,_Last,_Pred)
# define __glibcxx_requires_nonempty() __glibcxx_check_nonempty()
# define __glibcxx_requires_string(_String) __glibcxx_check_string(_String)
# define __glibcxx_requires_string_len(_String,_Len)	\
     __glibcxx_check_string_len(_String,_Len)
# define __glibcxx_requires_subscript(_N) __glibcxx_check_subscript(_N)

# include <debug/functions.h>
# include <debug/formatter.h>

#endif

#endif // _GLIBCXX_DEBUG_MACRO_SWITCH_H
