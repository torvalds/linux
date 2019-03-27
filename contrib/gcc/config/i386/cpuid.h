/*
 * Copyright (C) 2007 Free Software Foundation, Inc.
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 * 
 * In addition to the permissions in the GNU General Public License, the
 * Free Software Foundation gives you unlimited permission to link the
 * compiled version of this file with other programs, and to distribute
 * those programs without any restriction coming from the use of this
 * file.  (The General Public License restrictions do apply in other
 * respects; for example, they cover modification of the file, and
 * distribution when not linked into another program.)
 * 
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 * 
 *    As a special exception, if you link this library with files
 *    compiled with GCC to produce an executable, this does not cause
 *    the resulting executable to be covered by the GNU General Public License.
 *    This exception does not however invalidate any other reasons why
 *    the executable file might be covered by the GNU General Public License.
 */

/* %ecx */
#define bit_SSE3	(1 << 0)
#define bit_SSSE3	(1 << 9)
#define bit_CMPXCHG16B	(1 << 13)
#define bit_SSE4_1	(1 << 19)
#define bit_SSE4_2	(1 << 20)
#define bit_POPCNT	(1 << 23)

/* %edx */
#define bit_CMPXCHG8B	(1 << 8)
#define bit_CMOV	(1 << 15)
#define bit_MMX		(1 << 23)
#define bit_FXSAVE	(1 << 24)
#define bit_SSE		(1 << 25)
#define bit_SSE2	(1 << 26)

/* Extended Features */
/* %ecx */
#define bit_LAHF_LM	(1 << 0)
#define bit_SSE4a	(1 << 6)
#define bit_SSE5	(1 << 11)

/* %edx */
#define bit_LM		(1 << 29)
#define bit_3DNOWP	(1 << 30)
#define bit_3DNOW	(1 << 31)


#if defined(__i386__) && defined(__PIC__)
/* %ebx may be the PIC register.  */
#define __cpuid(level, a, b, c, d)			\
  __asm__ ("xchg{l}\t{%%}ebx, %1\n\t"			\
	   "cpuid\n\t"					\
	   "xchg{l}\t{%%}ebx, %1\n\t"			\
	   : "=a" (a), "=r" (b), "=c" (c), "=d" (d)	\
	   : "0" (level))
#else
#define __cpuid(level, a, b, c, d)			\
  __asm__ ("cpuid\n\t"					\
	   : "=a" (a), "=b" (b), "=c" (c), "=d" (d)	\
	   : "0" (level))
#endif

/* Return highest supported input value for cpuid instruction.  ext can
   be either 0x0 or 0x8000000 to return highest supported value for
   basic or extended cpuid information.  Function returns 0 if cpuid
   is not supported or whatever cpuid returns in eax register.  If sig
   pointer is non-null, then first four bytes of the signature
   (as found in ebx register) are returned in location pointed by sig.  */

static __inline unsigned int
__get_cpuid_max (unsigned int __ext, unsigned int *__sig)
{
  unsigned int __eax, __ebx, __ecx, __edx;

#ifndef __x86_64__
  /* See if we can use cpuid.  On AMD64 we always can.  */
  __asm__ ("pushf{l|d}\n\t"
	   "pushf{l|d}\n\t"
	   "pop{l}\t%0\n\t"
	   "mov{l}\t{%0, %1|%1, %0}\n\t"
	   "xor{l}\t{%2, %0|%0, %2}\n\t"
	   "push{l}\t%0\n\t"
	   "popf{l|d}\n\t"
	   "pushf{l|d}\n\t"
	   "pop{l}\t%0\n\t"
	   "popf{l|d}\n\t"
	   : "=&r" (__eax), "=&r" (__ebx)
	   : "i" (0x00200000));

  if (!((__eax ^ __ebx) & 0x00200000))
    return 0;
#endif

  /* Host supports cpuid.  Return highest supported cpuid input value.  */
  __cpuid (__ext, __eax, __ebx, __ecx, __edx);

  if (__sig)
    *__sig = __ebx;

  return __eax;
}

/* Return cpuid data for requested cpuid level, as found in returned
   eax, ebx, ecx and edx registers.  The function checks if cpuid is
   supported and returns 1 for valid cpuid information or 0 for
   unsupported cpuid level.  All pointers are required to be non-null.  */

static __inline int
__get_cpuid (unsigned int __level,
	     unsigned int *__eax, unsigned int *__ebx,
	     unsigned int *__ecx, unsigned int *__edx)
{
  unsigned int __ext = __level & 0x80000000;

  if (__get_cpuid_max (__ext, 0) < __level)
    return 0;

  __cpuid (__level, *__eax, *__ebx, *__ecx, *__edx);
  return 1;
}
