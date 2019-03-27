/* Copyright (C) 2007 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, if you include this header file into source
   files compiled by GCC, this header file does not by itself cause
   the resulting executable to be covered by the GNU General Public
   License.  This exception does not however invalidate any other
   reasons why the executable file might be covered by the GNU General
   Public License.  */

/* Implemented from the specification included in the AMD Programmers
   Manual Update, version 2.x */

#ifndef _AMMINTRIN_H_INCLUDED
#define _AMMINTRIN_H_INCLUDED

#ifndef __SSE4A__
# error "SSE4A instruction set not enabled"
#else

/* We need definitions from the SSE3, SSE2 and SSE header files*/
#include <pmmintrin.h>

static __inline void __attribute__((__always_inline__))
_mm_stream_sd (double * __P, __m128d __Y)
{
  __builtin_ia32_movntsd (__P, (__v2df) __Y);
}

static __inline void __attribute__((__always_inline__))
_mm_stream_ss (float * __P, __m128 __Y)
{
  __builtin_ia32_movntss (__P, (__v4sf) __Y);
}

static __inline __m128i __attribute__((__always_inline__))
_mm_extract_si64 (__m128i __X, __m128i __Y)
{
  return (__m128i) __builtin_ia32_extrq ((__v2di) __X, (__v16qi) __Y);
}

#define _mm_extracti_si64(X, I, L) \
((__m128i) __builtin_ia32_extrqi ((__v2di)(X), I, L))

static __inline __m128i __attribute__((__always_inline__))
_mm_insert_si64 (__m128i __X,__m128i __Y)
{
  return (__m128i) __builtin_ia32_insertq ((__v2di)__X, (__v2di)__Y);
}

#define _mm_inserti_si64(X, Y, I, L) \
((__m128i) __builtin_ia32_insertqi ((__v2di)(X), (__v2di)(Y), I, L))


#endif /* __SSE4A__ */

#endif /* _AMMINTRIN_H_INCLUDED */
