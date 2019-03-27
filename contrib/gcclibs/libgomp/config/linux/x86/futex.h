/* Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@redhat.com>.

   This file is part of the GNU OpenMP Library (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
   more details.

   You should have received a copy of the GNU Lesser General Public License 
   along with libgomp; see the file COPYING.LIB.  If not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files, some
   of which are compiled with GCC, to produce an executable, this library
   does not by itself cause the resulting executable to be covered by the
   GNU General Public License.  This exception does not however invalidate
   any other reasons why the executable file might be covered by the GNU
   General Public License.  */

/* Provide target-specific access to the futex system call.  */

#define FUTEX_WAIT	0
#define FUTEX_WAKE	1

#ifdef __LP64__
# ifndef SYS_futex
#  define SYS_futex	202
# endif

static inline void
futex_wait (int *addr, int val)
{
  register long r10 __asm__("%r10") = 0;
  long res;

  __asm volatile ("syscall"
		  : "=a" (res)
		  : "0"(SYS_futex), "D" (addr), "S"(FUTEX_WAIT),
		    "d"(val), "r"(r10)
		  : "r11", "rcx", "memory");
}

static inline void
futex_wake (int *addr, int count)
{
  long res;

  __asm volatile ("syscall"
		  : "=a" (res)
		  : "0"(SYS_futex), "D" (addr), "S"(FUTEX_WAKE), "d"(count)
		  : "r11", "rcx", "memory");
}
#else
# ifndef SYS_futex
#  define SYS_futex	240
# endif

# ifdef __PIC__

static inline void
sys_futex0 (int *addr, int op, int val)
{
  long res;

  __asm volatile ("xchgl\t%%ebx, %2\n\t"
		  "int\t$0x80\n\t"
		  "xchgl\t%%ebx, %2"
		  : "=a" (res)
		  : "0"(SYS_futex), "r" (addr), "c"(op),
		    "d"(val), "S"(0)
		  : "memory");
}

# else

static inline void
sys_futex0 (int *addr, int op, int val)
{
  long res;

  __asm volatile ("int $0x80"
		  : "=a" (res)
		  : "0"(SYS_futex), "b" (addr), "c"(op),
		    "d"(val), "S"(0)
		  : "memory");
}

# endif /* __PIC__ */

static inline void
futex_wait (int *addr, int val)
{
  sys_futex0 (addr, FUTEX_WAIT, val);
}

static inline void
futex_wake (int *addr, int count)
{
  sys_futex0 (addr, FUTEX_WAKE, count);
}

#endif /* __LP64__ */
