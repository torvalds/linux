// Low-level functions for atomic operations: sh version  -*- C++ -*-

// Copyright (C) 1999, 2001, 2002, 2003, 2004, 2005, 2006
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

#ifndef _BITS_ATOMICITY_H
#define _BITS_ATOMICITY_H	1

#ifdef __SH4A__

typedef int _Atomic_word;

static inline _Atomic_word
__attribute__ ((__unused__))
__exchange_and_add (volatile _Atomic_word* __mem, int __val)
{
  _Atomic_word __result;

  __asm__ __volatile__
    ("0:\n"
     "\tmovli.l\t@%2,r0\n"
     "\tmov\tr0,%1\n"
     "\tadd\t%3,r0\n"
     "\tmovco.l\tr0,@%2\n"
     "\tbf\t0b"
     : "+m" (*__mem), "=r" (__result)
     : "r" (__mem), "rI08" (__val)
     : "r0");

  return __result;
}


static inline void
__attribute__ ((__unused__))
__atomic_add (volatile _Atomic_word* __mem, int __val)
{
  asm("0:\n"
      "\tmovli.l\t@%1,r0\n"
      "\tadd\t%2,r0\n"
      "\tmovco.l\tr0,@%1\n"
      "\tbf\t0b"
      : "+m" (*__mem)
      : "r" (__mem), "rI08" (__val)
      : "r0");
}

#else /* !__SH4A__ */

/* This is generic/atomicity.h */

#include <ext/atomicity.h>
#include <ext/concurrence.h>

namespace 
{
  __gnu_cxx::__mutex atomic_mutex;
} // anonymous namespace

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  _Atomic_word
  __attribute__ ((__unused__))
  __exchange_and_add(volatile _Atomic_word* __mem, int __val)
  {
    __gnu_cxx::__scoped_lock sentry(atomic_mutex);
    _Atomic_word __result;
    __result = *__mem;
    *__mem += __val;
    return __result;
  }

  void
  __attribute__ ((__unused__))
  __atomic_add(volatile _Atomic_word* __mem, int __val)
  { __exchange_and_add(__mem, __val); }

_GLIBCXX_END_NAMESPACE

#endif /* !__SH4A__ */

#endif /* atomicity.h */
