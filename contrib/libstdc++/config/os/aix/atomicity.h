// Low-level functions for atomic operations: AIX version  -*- C++ -*-

// Copyright (C) 2000, 2001, 2004, 2005 Free Software Foundation, Inc.
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

#include <ext/atomicity.h>

/* We cannot use the cpu/powerpc/bits/atomicity.h inline assembly
   definitions for these operations since they depend on operations
   that are not available on the original POWER architecture.  AIX
   still runs on the POWER architecture, so it would be incorrect to
   assume the existence of these instructions.

   The definition of _Atomic_word must match the type pointed to by
   atomic_p in <sys/atomic_op.h>.  */

extern "C"
{
#include <sys/atomic_op.h>
}

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  _Atomic_word
  __attribute__ ((__unused__))
  __exchange_and_add (volatile _Atomic_word* __mem, int __val)
  { return ::fetch_and_add(const_cast<atomic_p>(__mem), __val); }

  void
  __attribute__ ((__unused__))
  __atomic_add (volatile _Atomic_word* __mem, int __val)
  { (void) ::fetch_and_add(const_cast<atomic_p>(__mem), __val); }

_GLIBCXX_END_NAMESPACE
