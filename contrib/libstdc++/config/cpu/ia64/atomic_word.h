// Low-level type for atomic operations -*- C++ -*-

// Copyright (C) 2004, 2005, 2006, 2007 Free Software Foundation, Inc.
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

#ifndef _GLIBCXX_ATOMIC_WORD_H
#define _GLIBCXX_ATOMIC_WORD_H	1

#include <bits/cxxabi_tweaks.h>

typedef int _Atomic_word;

namespace __gnu_cxx
{
  // Test the first byte of __g and ensure that no loads are hoisted across
  // the test.
  inline bool
  __test_and_acquire (__cxxabiv1::__guard *__g)
  {
    unsigned char __c;
    unsigned char *__p = reinterpret_cast<unsigned char *>(__g);
    // ldN.acq is a load with an implied hoist barrier.
    // would ld8+mask be faster than just doing an ld1?
    __asm __volatile ("ld1.acq %0 = %1" : "=r"(__c) : "m"(*__p) : "memory");
    return __c != 0;
  }

  // Set the first byte of __g to 1 and ensure that no stores are sunk
  // across the store.
  inline void
  __set_and_release (__cxxabiv1::__guard *__g)
  {
    unsigned char *__p = reinterpret_cast<unsigned char *>(__g);
    // stN.rel is a store with an implied sink barrier.
    // could load word, set flag, and CAS it back
    __asm __volatile ("st1.rel %0 = %1" : "=m"(*__p) : "r"(1) : "memory");
  }

  // We don't define the _BARRIER macros on ia64 because the barriers are
  // included in the test and set, above.
#define _GLIBCXX_GUARD_TEST_AND_ACQUIRE(G) __gnu_cxx::__test_and_acquire (G)
#define _GLIBCXX_GUARD_SET_AND_RELEASE(G) __gnu_cxx::__set_and_release (G)
}

#endif 
