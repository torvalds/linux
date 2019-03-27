/* Copyright (C) 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

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

/* Implemented from the specification included in the Intel C++ Compiler
   User Guide and Reference, version 9.0.  */

#ifndef _MMINTRIN_H_INCLUDED
#define _MMINTRIN_H_INCLUDED

#ifndef __MMX__
# error "MMX instruction set not enabled"
#else
/* The Intel API is flexible enough that we must allow aliasing with other
   vector types, and their scalar components.  */
typedef int __m64 __attribute__ ((__vector_size__ (8), __may_alias__));

/* Internal data types for implementing the intrinsics.  */
typedef int __v2si __attribute__ ((__vector_size__ (8)));
typedef short __v4hi __attribute__ ((__vector_size__ (8)));
typedef char __v8qi __attribute__ ((__vector_size__ (8)));

/* Empty the multimedia state.  */
static __inline void __attribute__((__always_inline__))
_mm_empty (void)
{
  __builtin_ia32_emms ();
}

static __inline void __attribute__((__always_inline__))
_m_empty (void)
{
  _mm_empty ();
}

/* Convert I to a __m64 object.  The integer is zero-extended to 64-bits.  */
static __inline __m64  __attribute__((__always_inline__))
_mm_cvtsi32_si64 (int __i)
{
  return (__m64) __builtin_ia32_vec_init_v2si (__i, 0);
}

static __inline __m64  __attribute__((__always_inline__))
_m_from_int (int __i)
{
  return _mm_cvtsi32_si64 (__i);
}

#ifdef __x86_64__
/* Convert I to a __m64 object.  */

/* Intel intrinsic.  */
static __inline __m64  __attribute__((__always_inline__))
_m_from_int64 (long long __i)
{
  return (__m64) __i;
}

static __inline __m64  __attribute__((__always_inline__))
_mm_cvtsi64_m64 (long long __i)
{
  return (__m64) __i;
}

/* Microsoft intrinsic.  */
static __inline __m64  __attribute__((__always_inline__))
_mm_cvtsi64x_si64 (long long __i)
{
  return (__m64) __i;
}

static __inline __m64  __attribute__((__always_inline__))
_mm_set_pi64x (long long __i)
{
  return (__m64) __i;
}
#endif

/* Convert the lower 32 bits of the __m64 object into an integer.  */
static __inline int __attribute__((__always_inline__))
_mm_cvtsi64_si32 (__m64 __i)
{
  return __builtin_ia32_vec_ext_v2si ((__v2si)__i, 0);
}

static __inline int __attribute__((__always_inline__))
_m_to_int (__m64 __i)
{
  return _mm_cvtsi64_si32 (__i);
}

#ifdef __x86_64__
/* Convert the __m64 object to a 64bit integer.  */

/* Intel intrinsic.  */
static __inline long long __attribute__((__always_inline__))
_m_to_int64 (__m64 __i)
{
  return (long long)__i;
}

static __inline long long __attribute__((__always_inline__))
_mm_cvtm64_si64 (__m64 __i)
{
  return (long long)__i;
}

/* Microsoft intrinsic.  */
static __inline long long __attribute__((__always_inline__))
_mm_cvtsi64_si64x (__m64 __i)
{
  return (long long)__i;
}
#endif

/* Pack the four 16-bit values from M1 into the lower four 8-bit values of
   the result, and the four 16-bit values from M2 into the upper four 8-bit
   values of the result, all with signed saturation.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_packs_pi16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_packsswb ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_packsswb (__m64 __m1, __m64 __m2)
{
  return _mm_packs_pi16 (__m1, __m2);
}

/* Pack the two 32-bit values from M1 in to the lower two 16-bit values of
   the result, and the two 32-bit values from M2 into the upper two 16-bit
   values of the result, all with signed saturation.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_packs_pi32 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_packssdw ((__v2si)__m1, (__v2si)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_packssdw (__m64 __m1, __m64 __m2)
{
  return _mm_packs_pi32 (__m1, __m2);
}

/* Pack the four 16-bit values from M1 into the lower four 8-bit values of
   the result, and the four 16-bit values from M2 into the upper four 8-bit
   values of the result, all with unsigned saturation.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_packs_pu16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_packuswb ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_packuswb (__m64 __m1, __m64 __m2)
{
  return _mm_packs_pu16 (__m1, __m2);
}

/* Interleave the four 8-bit values from the high half of M1 with the four
   8-bit values from the high half of M2.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_unpackhi_pi8 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_punpckhbw ((__v8qi)__m1, (__v8qi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_punpckhbw (__m64 __m1, __m64 __m2)
{
  return _mm_unpackhi_pi8 (__m1, __m2);
}

/* Interleave the two 16-bit values from the high half of M1 with the two
   16-bit values from the high half of M2.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_unpackhi_pi16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_punpckhwd ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_punpckhwd (__m64 __m1, __m64 __m2)
{
  return _mm_unpackhi_pi16 (__m1, __m2);
}

/* Interleave the 32-bit value from the high half of M1 with the 32-bit
   value from the high half of M2.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_unpackhi_pi32 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_punpckhdq ((__v2si)__m1, (__v2si)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_punpckhdq (__m64 __m1, __m64 __m2)
{
  return _mm_unpackhi_pi32 (__m1, __m2);
}

/* Interleave the four 8-bit values from the low half of M1 with the four
   8-bit values from the low half of M2.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_unpacklo_pi8 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_punpcklbw ((__v8qi)__m1, (__v8qi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_punpcklbw (__m64 __m1, __m64 __m2)
{
  return _mm_unpacklo_pi8 (__m1, __m2);
}

/* Interleave the two 16-bit values from the low half of M1 with the two
   16-bit values from the low half of M2.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_unpacklo_pi16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_punpcklwd ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_punpcklwd (__m64 __m1, __m64 __m2)
{
  return _mm_unpacklo_pi16 (__m1, __m2);
}

/* Interleave the 32-bit value from the low half of M1 with the 32-bit
   value from the low half of M2.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_unpacklo_pi32 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_punpckldq ((__v2si)__m1, (__v2si)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_punpckldq (__m64 __m1, __m64 __m2)
{
  return _mm_unpacklo_pi32 (__m1, __m2);
}

/* Add the 8-bit values in M1 to the 8-bit values in M2.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_add_pi8 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_paddb ((__v8qi)__m1, (__v8qi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_paddb (__m64 __m1, __m64 __m2)
{
  return _mm_add_pi8 (__m1, __m2);
}

/* Add the 16-bit values in M1 to the 16-bit values in M2.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_add_pi16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_paddw ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_paddw (__m64 __m1, __m64 __m2)
{
  return _mm_add_pi16 (__m1, __m2);
}

/* Add the 32-bit values in M1 to the 32-bit values in M2.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_add_pi32 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_paddd ((__v2si)__m1, (__v2si)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_paddd (__m64 __m1, __m64 __m2)
{
  return _mm_add_pi32 (__m1, __m2);
}

/* Add the 64-bit values in M1 to the 64-bit values in M2.  */
#ifdef __SSE2__
static __inline __m64 __attribute__((__always_inline__))
_mm_add_si64 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_paddq ((long long)__m1, (long long)__m2);
}
#endif

/* Add the 8-bit values in M1 to the 8-bit values in M2 using signed
   saturated arithmetic.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_adds_pi8 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_paddsb ((__v8qi)__m1, (__v8qi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_paddsb (__m64 __m1, __m64 __m2)
{
  return _mm_adds_pi8 (__m1, __m2);
}

/* Add the 16-bit values in M1 to the 16-bit values in M2 using signed
   saturated arithmetic.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_adds_pi16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_paddsw ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_paddsw (__m64 __m1, __m64 __m2)
{
  return _mm_adds_pi16 (__m1, __m2);
}

/* Add the 8-bit values in M1 to the 8-bit values in M2 using unsigned
   saturated arithmetic.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_adds_pu8 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_paddusb ((__v8qi)__m1, (__v8qi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_paddusb (__m64 __m1, __m64 __m2)
{
  return _mm_adds_pu8 (__m1, __m2);
}

/* Add the 16-bit values in M1 to the 16-bit values in M2 using unsigned
   saturated arithmetic.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_adds_pu16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_paddusw ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_paddusw (__m64 __m1, __m64 __m2)
{
  return _mm_adds_pu16 (__m1, __m2);
}

/* Subtract the 8-bit values in M2 from the 8-bit values in M1.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_sub_pi8 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_psubb ((__v8qi)__m1, (__v8qi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psubb (__m64 __m1, __m64 __m2)
{
  return _mm_sub_pi8 (__m1, __m2);
}

/* Subtract the 16-bit values in M2 from the 16-bit values in M1.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_sub_pi16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_psubw ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psubw (__m64 __m1, __m64 __m2)
{
  return _mm_sub_pi16 (__m1, __m2);
}

/* Subtract the 32-bit values in M2 from the 32-bit values in M1.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_sub_pi32 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_psubd ((__v2si)__m1, (__v2si)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psubd (__m64 __m1, __m64 __m2)
{
  return _mm_sub_pi32 (__m1, __m2);
}

/* Add the 64-bit values in M1 to the 64-bit values in M2.  */
#ifdef __SSE2__
static __inline __m64 __attribute__((__always_inline__))
_mm_sub_si64 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_psubq ((long long)__m1, (long long)__m2);
}
#endif

/* Subtract the 8-bit values in M2 from the 8-bit values in M1 using signed
   saturating arithmetic.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_subs_pi8 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_psubsb ((__v8qi)__m1, (__v8qi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psubsb (__m64 __m1, __m64 __m2)
{
  return _mm_subs_pi8 (__m1, __m2);
}

/* Subtract the 16-bit values in M2 from the 16-bit values in M1 using
   signed saturating arithmetic.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_subs_pi16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_psubsw ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psubsw (__m64 __m1, __m64 __m2)
{
  return _mm_subs_pi16 (__m1, __m2);
}

/* Subtract the 8-bit values in M2 from the 8-bit values in M1 using
   unsigned saturating arithmetic.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_subs_pu8 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_psubusb ((__v8qi)__m1, (__v8qi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psubusb (__m64 __m1, __m64 __m2)
{
  return _mm_subs_pu8 (__m1, __m2);
}

/* Subtract the 16-bit values in M2 from the 16-bit values in M1 using
   unsigned saturating arithmetic.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_subs_pu16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_psubusw ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psubusw (__m64 __m1, __m64 __m2)
{
  return _mm_subs_pu16 (__m1, __m2);
}

/* Multiply four 16-bit values in M1 by four 16-bit values in M2 producing
   four 32-bit intermediate results, which are then summed by pairs to
   produce two 32-bit results.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_madd_pi16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_pmaddwd ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pmaddwd (__m64 __m1, __m64 __m2)
{
  return _mm_madd_pi16 (__m1, __m2);
}

/* Multiply four signed 16-bit values in M1 by four signed 16-bit values in
   M2 and produce the high 16 bits of the 32-bit results.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_mulhi_pi16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_pmulhw ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pmulhw (__m64 __m1, __m64 __m2)
{
  return _mm_mulhi_pi16 (__m1, __m2);
}

/* Multiply four 16-bit values in M1 by four 16-bit values in M2 and produce
   the low 16 bits of the results.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_mullo_pi16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_pmullw ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pmullw (__m64 __m1, __m64 __m2)
{
  return _mm_mullo_pi16 (__m1, __m2);
}

/* Shift four 16-bit values in M left by COUNT.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_sll_pi16 (__m64 __m, __m64 __count)
{
  return (__m64) __builtin_ia32_psllw ((__v4hi)__m, (long long)__count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psllw (__m64 __m, __m64 __count)
{
  return _mm_sll_pi16 (__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_mm_slli_pi16 (__m64 __m, int __count)
{
  return (__m64) __builtin_ia32_psllw ((__v4hi)__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psllwi (__m64 __m, int __count)
{
  return _mm_slli_pi16 (__m, __count);
}

/* Shift two 32-bit values in M left by COUNT.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_sll_pi32 (__m64 __m, __m64 __count)
{
  return (__m64) __builtin_ia32_pslld ((__v2si)__m, (long long)__count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pslld (__m64 __m, __m64 __count)
{
  return _mm_sll_pi32 (__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_mm_slli_pi32 (__m64 __m, int __count)
{
  return (__m64) __builtin_ia32_pslld ((__v2si)__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pslldi (__m64 __m, int __count)
{
  return _mm_slli_pi32 (__m, __count);
}

/* Shift the 64-bit value in M left by COUNT.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_sll_si64 (__m64 __m, __m64 __count)
{
  return (__m64) __builtin_ia32_psllq ((long long)__m, (long long)__count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psllq (__m64 __m, __m64 __count)
{
  return _mm_sll_si64 (__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_mm_slli_si64 (__m64 __m, int __count)
{
  return (__m64) __builtin_ia32_psllq ((long long)__m, (long long)__count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psllqi (__m64 __m, int __count)
{
  return _mm_slli_si64 (__m, __count);
}

/* Shift four 16-bit values in M right by COUNT; shift in the sign bit.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_sra_pi16 (__m64 __m, __m64 __count)
{
  return (__m64) __builtin_ia32_psraw ((__v4hi)__m, (long long)__count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psraw (__m64 __m, __m64 __count)
{
  return _mm_sra_pi16 (__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_mm_srai_pi16 (__m64 __m, int __count)
{
  return (__m64) __builtin_ia32_psraw ((__v4hi)__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psrawi (__m64 __m, int __count)
{
  return _mm_srai_pi16 (__m, __count);
}

/* Shift two 32-bit values in M right by COUNT; shift in the sign bit.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_sra_pi32 (__m64 __m, __m64 __count)
{
  return (__m64) __builtin_ia32_psrad ((__v2si)__m, (long long)__count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psrad (__m64 __m, __m64 __count)
{
  return _mm_sra_pi32 (__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_mm_srai_pi32 (__m64 __m, int __count)
{
  return (__m64) __builtin_ia32_psrad ((__v2si)__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psradi (__m64 __m, int __count)
{
  return _mm_srai_pi32 (__m, __count);
}

/* Shift four 16-bit values in M right by COUNT; shift in zeros.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_srl_pi16 (__m64 __m, __m64 __count)
{
  return (__m64) __builtin_ia32_psrlw ((__v4hi)__m, (long long)__count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psrlw (__m64 __m, __m64 __count)
{
  return _mm_srl_pi16 (__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_mm_srli_pi16 (__m64 __m, int __count)
{
  return (__m64) __builtin_ia32_psrlw ((__v4hi)__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psrlwi (__m64 __m, int __count)
{
  return _mm_srli_pi16 (__m, __count);
}

/* Shift two 32-bit values in M right by COUNT; shift in zeros.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_srl_pi32 (__m64 __m, __m64 __count)
{
  return (__m64) __builtin_ia32_psrld ((__v2si)__m, (long long)__count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psrld (__m64 __m, __m64 __count)
{
  return _mm_srl_pi32 (__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_mm_srli_pi32 (__m64 __m, int __count)
{
  return (__m64) __builtin_ia32_psrld ((__v2si)__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psrldi (__m64 __m, int __count)
{
  return _mm_srli_pi32 (__m, __count);
}

/* Shift the 64-bit value in M left by COUNT; shift in zeros.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_srl_si64 (__m64 __m, __m64 __count)
{
  return (__m64) __builtin_ia32_psrlq ((long long)__m, (long long)__count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psrlq (__m64 __m, __m64 __count)
{
  return _mm_srl_si64 (__m, __count);
}

static __inline __m64 __attribute__((__always_inline__))
_mm_srli_si64 (__m64 __m, int __count)
{
  return (__m64) __builtin_ia32_psrlq ((long long)__m, (long long)__count);
}

static __inline __m64 __attribute__((__always_inline__))
_m_psrlqi (__m64 __m, int __count)
{
  return _mm_srli_si64 (__m, __count);
}

/* Bit-wise AND the 64-bit values in M1 and M2.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_and_si64 (__m64 __m1, __m64 __m2)
{
  return __builtin_ia32_pand (__m1, __m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pand (__m64 __m1, __m64 __m2)
{
  return _mm_and_si64 (__m1, __m2);
}

/* Bit-wise complement the 64-bit value in M1 and bit-wise AND it with the
   64-bit value in M2.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_andnot_si64 (__m64 __m1, __m64 __m2)
{
  return __builtin_ia32_pandn (__m1, __m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pandn (__m64 __m1, __m64 __m2)
{
  return _mm_andnot_si64 (__m1, __m2);
}

/* Bit-wise inclusive OR the 64-bit values in M1 and M2.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_or_si64 (__m64 __m1, __m64 __m2)
{
  return __builtin_ia32_por (__m1, __m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_por (__m64 __m1, __m64 __m2)
{
  return _mm_or_si64 (__m1, __m2);
}

/* Bit-wise exclusive OR the 64-bit values in M1 and M2.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_xor_si64 (__m64 __m1, __m64 __m2)
{
  return __builtin_ia32_pxor (__m1, __m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pxor (__m64 __m1, __m64 __m2)
{
  return _mm_xor_si64 (__m1, __m2);
}

/* Compare eight 8-bit values.  The result of the comparison is 0xFF if the
   test is true and zero if false.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_cmpeq_pi8 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_pcmpeqb ((__v8qi)__m1, (__v8qi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pcmpeqb (__m64 __m1, __m64 __m2)
{
  return _mm_cmpeq_pi8 (__m1, __m2);
}

static __inline __m64 __attribute__((__always_inline__))
_mm_cmpgt_pi8 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_pcmpgtb ((__v8qi)__m1, (__v8qi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pcmpgtb (__m64 __m1, __m64 __m2)
{
  return _mm_cmpgt_pi8 (__m1, __m2);
}

/* Compare four 16-bit values.  The result of the comparison is 0xFFFF if
   the test is true and zero if false.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_cmpeq_pi16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_pcmpeqw ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pcmpeqw (__m64 __m1, __m64 __m2)
{
  return _mm_cmpeq_pi16 (__m1, __m2);
}

static __inline __m64 __attribute__((__always_inline__))
_mm_cmpgt_pi16 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_pcmpgtw ((__v4hi)__m1, (__v4hi)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pcmpgtw (__m64 __m1, __m64 __m2)
{
  return _mm_cmpgt_pi16 (__m1, __m2);
}

/* Compare two 32-bit values.  The result of the comparison is 0xFFFFFFFF if
   the test is true and zero if false.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_cmpeq_pi32 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_pcmpeqd ((__v2si)__m1, (__v2si)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pcmpeqd (__m64 __m1, __m64 __m2)
{
  return _mm_cmpeq_pi32 (__m1, __m2);
}

static __inline __m64 __attribute__((__always_inline__))
_mm_cmpgt_pi32 (__m64 __m1, __m64 __m2)
{
  return (__m64) __builtin_ia32_pcmpgtd ((__v2si)__m1, (__v2si)__m2);
}

static __inline __m64 __attribute__((__always_inline__))
_m_pcmpgtd (__m64 __m1, __m64 __m2)
{
  return _mm_cmpgt_pi32 (__m1, __m2);
}

/* Creates a 64-bit zero.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_setzero_si64 (void)
{
  return (__m64)0LL;
}

/* Creates a vector of two 32-bit values; I0 is least significant.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_set_pi32 (int __i1, int __i0)
{
  return (__m64) __builtin_ia32_vec_init_v2si (__i0, __i1);
}

/* Creates a vector of four 16-bit values; W0 is least significant.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_set_pi16 (short __w3, short __w2, short __w1, short __w0)
{
  return (__m64) __builtin_ia32_vec_init_v4hi (__w0, __w1, __w2, __w3);
}

/* Creates a vector of eight 8-bit values; B0 is least significant.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_set_pi8 (char __b7, char __b6, char __b5, char __b4,
	     char __b3, char __b2, char __b1, char __b0)
{
  return (__m64) __builtin_ia32_vec_init_v8qi (__b0, __b1, __b2, __b3,
					       __b4, __b5, __b6, __b7);
}

/* Similar, but with the arguments in reverse order.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_setr_pi32 (int __i0, int __i1)
{
  return _mm_set_pi32 (__i1, __i0);
}

static __inline __m64 __attribute__((__always_inline__))
_mm_setr_pi16 (short __w0, short __w1, short __w2, short __w3)
{
  return _mm_set_pi16 (__w3, __w2, __w1, __w0);
}

static __inline __m64 __attribute__((__always_inline__))
_mm_setr_pi8 (char __b0, char __b1, char __b2, char __b3,
	      char __b4, char __b5, char __b6, char __b7)
{
  return _mm_set_pi8 (__b7, __b6, __b5, __b4, __b3, __b2, __b1, __b0);
}

/* Creates a vector of two 32-bit values, both elements containing I.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_set1_pi32 (int __i)
{
  return _mm_set_pi32 (__i, __i);
}

/* Creates a vector of four 16-bit values, all elements containing W.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_set1_pi16 (short __w)
{
  return _mm_set_pi16 (__w, __w, __w, __w);
}

/* Creates a vector of eight 8-bit values, all elements containing B.  */
static __inline __m64 __attribute__((__always_inline__))
_mm_set1_pi8 (char __b)
{
  return _mm_set_pi8 (__b, __b, __b, __b, __b, __b, __b, __b);
}

#endif /* __MMX__ */
#endif /* _MMINTRIN_H_INCLUDED */
