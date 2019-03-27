// Low-level functions for atomic operations: PA-RISC version  -*- C++ -*-

// Copyright (C) 2002, 2004, 2005 Free Software Foundation, Inc.
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

#include <bits/c++config.h>
#include <ext/atomicity.h>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  template<int _Inst>
    struct _Atomicity_lock
    {
      static volatile int _S_atomicity_lock;
    };
  
  template<int _Inst>
  volatile int
  _Atomicity_lock<_Inst>::_S_atomicity_lock __attribute__ ((aligned (16))) = 1;

  // Because of the lack of weak support when using the hpux som
  // linker, we explicitly instantiate the atomicity lock.
  template volatile int _Atomicity_lock<0>::_S_atomicity_lock;

  int
  __attribute__ ((__unused__))
  __exchange_and_add(volatile _Atomic_word* __mem, int __val)
  {
    _Atomic_word result;
    int tmp;
    volatile int& lock = _Atomicity_lock<0>::_S_atomicity_lock;
    
    __asm__ __volatile__ ("ldcw 0(%1),%0\n\t"
			  "cmpib,<>,n 0,%0,.+20\n\t"
			  "ldw 0(%1),%0\n\t"
			  "cmpib,= 0,%0,.-4\n\t"
			  "nop\n\t"
			  "b,n .-20"
			  : "=&r" (tmp)
			  : "r" (&lock)
			  : "memory");
    
    result = *__mem;
    *__mem = result + __val;
    __asm__ __volatile__ ("stw %1,0(%0)"
			  : : "r" (&lock), "r" (tmp) : "memory");
    return result;
  }
  
  void
  __attribute__ ((__unused__))
  __atomic_add(volatile _Atomic_word* __mem, int __val)
  {
    int tmp;
    volatile int& lock = _Atomicity_lock<0>::_S_atomicity_lock;
    
    __asm__ __volatile__ ("ldcw 0(%1),%0\n\t"
			  "cmpib,<>,n 0,%0,.+20\n\t"
			  "ldw 0(%1),%0\n\t"
			  "cmpib,= 0,%0,.-4\n\t"
			  "nop\n\t"
			  "b,n .-20"
			  : "=&r" (tmp)
			  : "r" (&lock)
			  : "memory");
    
    *__mem += __val;
    __asm__ __volatile__ ("stw %1,0(%0)"
			  : : "r" (&lock), "r" (tmp) : "memory");
  }

_GLIBCXX_END_NAMESPACE
