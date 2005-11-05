/* Machine-dependent software floating-point definitions.  PPC version.
   Copyright (C) 1997 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

   Actually, this is a PPC (32bit) version, written based on the
   i386, sparc, and sparc64 versions, by me,
   Peter Maydell (pmaydell@chiark.greenend.org.uk).
   Comments are by and large also mine, although they may be inaccurate.

   In picking out asm fragments I've gone with the lowest common
   denominator, which also happens to be the hardware I have :->
   That is, a SPARC without hardware multiply and divide.
 */

/* basic word size definitions */
#define _FP_W_TYPE_SIZE		32
#define _FP_W_TYPE		unsigned long
#define _FP_WS_TYPE		signed long
#define _FP_I_TYPE		long

#define __ll_B			((UWtype) 1 << (W_TYPE_SIZE / 2))
#define __ll_lowpart(t)		((UWtype) (t) & (__ll_B - 1))
#define __ll_highpart(t)	((UWtype) (t) >> (W_TYPE_SIZE / 2))

/* You can optionally code some things like addition in asm. For
 * example, i386 defines __FP_FRAC_ADD_2 as asm. If you don't
 * then you get a fragment of C code [if you change an #ifdef 0
 * in op-2.h] or a call to add_ssaaaa (see below).
 * Good places to look for asm fragments to use are gcc and glibc.
 * gcc's longlong.h is useful.
 */

/* We need to know how to multiply and divide. If the host word size
 * is >= 2*fracbits you can use FP_MUL_MEAT_n_imm(t,R,X,Y) which
 * codes the multiply with whatever gcc does to 'a * b'.
 * _FP_MUL_MEAT_n_wide(t,R,X,Y,f) is used when you have an asm
 * function that can multiply two 1W values and get a 2W result.
 * Otherwise you're stuck with _FP_MUL_MEAT_n_hard(t,R,X,Y) which
 * does bitshifting to avoid overflow.
 * For division there is FP_DIV_MEAT_n_imm(t,R,X,Y,f) for word size
 * >= 2*fracbits, where f is either _FP_DIV_HELP_imm or
 * _FP_DIV_HELP_ldiv (see op-1.h).
 * _FP_DIV_MEAT_udiv() is if you have asm to do 2W/1W => (1W, 1W).
 * [GCC and glibc have longlong.h which has the asm macro udiv_qrnnd
 * to do this.]
 * In general, 'n' is the number of words required to hold the type,
 * and 't' is either S, D or Q for single/double/quad.
 *           -- PMM
 */
/* Example: SPARC64:
 * #define _FP_MUL_MEAT_S(R,X,Y)	_FP_MUL_MEAT_1_imm(S,R,X,Y)
 * #define _FP_MUL_MEAT_D(R,X,Y)	_FP_MUL_MEAT_1_wide(D,R,X,Y,umul_ppmm)
 * #define _FP_MUL_MEAT_Q(R,X,Y)	_FP_MUL_MEAT_2_wide(Q,R,X,Y,umul_ppmm)
 *
 * #define _FP_DIV_MEAT_S(R,X,Y)	_FP_DIV_MEAT_1_imm(S,R,X,Y,_FP_DIV_HELP_imm)
 * #define _FP_DIV_MEAT_D(R,X,Y)	_FP_DIV_MEAT_1_udiv(D,R,X,Y)
 * #define _FP_DIV_MEAT_Q(R,X,Y)	_FP_DIV_MEAT_2_udiv_64(Q,R,X,Y)
 *
 * Example: i386:
 * #define _FP_MUL_MEAT_S(R,X,Y)   _FP_MUL_MEAT_1_wide(S,R,X,Y,_i386_mul_32_64)
 * #define _FP_MUL_MEAT_D(R,X,Y)   _FP_MUL_MEAT_2_wide(D,R,X,Y,_i386_mul_32_64)
 *
 * #define _FP_DIV_MEAT_S(R,X,Y)   _FP_DIV_MEAT_1_udiv(S,R,X,Y,_i386_div_64_32)
 * #define _FP_DIV_MEAT_D(R,X,Y)   _FP_DIV_MEAT_2_udiv_64(D,R,X,Y)
 */

#define _FP_MUL_MEAT_S(R,X,Y)   _FP_MUL_MEAT_1_wide(S,R,X,Y,umul_ppmm)
#define _FP_MUL_MEAT_D(R,X,Y)   _FP_MUL_MEAT_2_wide(D,R,X,Y,umul_ppmm)

#define _FP_DIV_MEAT_S(R,X,Y)   _FP_DIV_MEAT_1_udiv(S,R,X,Y)
#define _FP_DIV_MEAT_D(R,X,Y)   _FP_DIV_MEAT_2_udiv_64(D,R,X,Y)

/* These macros define what NaN looks like. They're supposed to expand to
 * a comma-separated set of 32bit unsigned ints that encode NaN.
 */
#define _FP_NANFRAC_S		_FP_QNANBIT_S
#define _FP_NANFRAC_D		_FP_QNANBIT_D, 0
#define _FP_NANFRAC_Q           _FP_QNANBIT_Q, 0, 0, 0

#define _FP_KEEPNANFRACP 1

/* This macro appears to be called when both X and Y are NaNs, and
 * has to choose one and copy it to R. i386 goes for the larger of the
 * two, sparc64 just picks Y. I don't understand this at all so I'll
 * go with sparc64 because it's shorter :->   -- PMM
 */
#define _FP_CHOOSENAN(fs, wc, R, X, Y)			\
  do {							\
    R##_s = Y##_s;					\
    _FP_FRAC_COPY_##wc(R,Y);				\
    R##_c = FP_CLS_NAN;					\
  } while (0)


extern void fp_unpack_d(long *, unsigned long *, unsigned long *,
			long *, long *, void *);
extern int  fp_pack_d(void *, long, unsigned long, unsigned long, long, long);
extern int  fp_pack_ds(void *, long, unsigned long, unsigned long, long, long);

#define __FP_UNPACK_RAW_1(fs, X, val)			\
  do {							\
    union _FP_UNION_##fs *_flo =			\
    	(union _FP_UNION_##fs *)val;			\
							\
    X##_f = _flo->bits.frac;				\
    X##_e = _flo->bits.exp;				\
    X##_s = _flo->bits.sign;				\
  } while (0)

#define __FP_UNPACK_RAW_2(fs, X, val)			\
  do {							\
    union _FP_UNION_##fs *_flo =			\
    	(union _FP_UNION_##fs *)val;			\
							\
    X##_f0 = _flo->bits.frac0;				\
    X##_f1 = _flo->bits.frac1;				\
    X##_e  = _flo->bits.exp;				\
    X##_s  = _flo->bits.sign;				\
  } while (0)

#define __FP_UNPACK_S(X,val)		\
  do {					\
    __FP_UNPACK_RAW_1(S,X,val);		\
    _FP_UNPACK_CANONICAL(S,1,X);	\
  } while (0)

#define __FP_UNPACK_D(X,val)		\
	fp_unpack_d(&X##_s, &X##_f1, &X##_f0, &X##_e, &X##_c, val)

#define __FP_PACK_RAW_1(fs, val, X)			\
  do {							\
    union _FP_UNION_##fs *_flo =			\
    	(union _FP_UNION_##fs *)val;			\
							\
    _flo->bits.frac = X##_f;				\
    _flo->bits.exp  = X##_e;				\
    _flo->bits.sign = X##_s;				\
  } while (0)

#define __FP_PACK_RAW_2(fs, val, X)			\
  do {							\
    union _FP_UNION_##fs *_flo =			\
    	(union _FP_UNION_##fs *)val;			\
							\
    _flo->bits.frac0 = X##_f0;				\
    _flo->bits.frac1 = X##_f1;				\
    _flo->bits.exp   = X##_e;				\
    _flo->bits.sign  = X##_s;				\
  } while (0)

#include <linux/kernel.h>
#include <linux/sched.h>

#define __FPU_FPSCR	(current->thread.fpscr.val)

/* We only actually write to the destination register
 * if exceptions signalled (if any) will not trap.
 */
#define __FPU_ENABLED_EXC \
({						\
	(__FPU_FPSCR >> 3) & 0x1f;	\
})

#define __FPU_TRAP_P(bits) \
	((__FPU_ENABLED_EXC & (bits)) != 0)

#define __FP_PACK_S(val,X)			\
({  int __exc = _FP_PACK_CANONICAL(S,1,X);	\
    if(!__exc || !__FPU_TRAP_P(__exc))		\
        __FP_PACK_RAW_1(S,val,X);		\
    __exc;					\
})

#define __FP_PACK_D(val,X)			\
	fp_pack_d(val, X##_s, X##_f1, X##_f0, X##_e, X##_c)

#define __FP_PACK_DS(val,X)			\
	fp_pack_ds(val, X##_s, X##_f1, X##_f0, X##_e, X##_c)

/* Obtain the current rounding mode. */
#define FP_ROUNDMODE			\
({					\
	__FPU_FPSCR & 0x3;		\
})

/* the asm fragments go here: all these are taken from glibc-2.0.5's
 * stdlib/longlong.h
 */

#include <linux/types.h>
#include <asm/byteorder.h>

/* add_ssaaaa is used in op-2.h and should be equivalent to
 * #define add_ssaaaa(sh,sl,ah,al,bh,bl) (sh = ah+bh+ (( sl = al+bl) < al))
 * add_ssaaaa(high_sum, low_sum, high_addend_1, low_addend_1,
 * high_addend_2, low_addend_2) adds two UWtype integers, composed by
 * HIGH_ADDEND_1 and LOW_ADDEND_1, and HIGH_ADDEND_2 and LOW_ADDEND_2
 * respectively.  The result is placed in HIGH_SUM and LOW_SUM.  Overflow
 * (i.e. carry out) is not stored anywhere, and is lost.
 */
#define add_ssaaaa(sh, sl, ah, al, bh, bl)				\
  do {									\
    if (__builtin_constant_p (bh) && (bh) == 0)				\
      __asm__ ("{a%I4|add%I4c} %1,%3,%4\n\t{aze|addze} %0,%2"		\
	     : "=r" ((USItype)(sh)),					\
	       "=&r" ((USItype)(sl))					\
	     : "%r" ((USItype)(ah)),					\
	       "%r" ((USItype)(al)),					\
	       "rI" ((USItype)(bl)));					\
    else if (__builtin_constant_p (bh) && (bh) ==~(USItype) 0)		\
      __asm__ ("{a%I4|add%I4c} %1,%3,%4\n\t{ame|addme} %0,%2"		\
	     : "=r" ((USItype)(sh)),					\
	       "=&r" ((USItype)(sl))					\
	     : "%r" ((USItype)(ah)),					\
	       "%r" ((USItype)(al)),					\
	       "rI" ((USItype)(bl)));					\
    else								\
      __asm__ ("{a%I5|add%I5c} %1,%4,%5\n\t{ae|adde} %0,%2,%3"		\
	     : "=r" ((USItype)(sh)),					\
	       "=&r" ((USItype)(sl))					\
	     : "%r" ((USItype)(ah)),					\
	       "r" ((USItype)(bh)),					\
	       "%r" ((USItype)(al)),					\
	       "rI" ((USItype)(bl)));					\
  } while (0)

/* sub_ddmmss is used in op-2.h and udivmodti4.c and should be equivalent to
 * #define sub_ddmmss(sh, sl, ah, al, bh, bl) (sh = ah-bh - ((sl = al-bl) > al))
 * sub_ddmmss(high_difference, low_difference, high_minuend, low_minuend,
 * high_subtrahend, low_subtrahend) subtracts two two-word UWtype integers,
 * composed by HIGH_MINUEND_1 and LOW_MINUEND_1, and HIGH_SUBTRAHEND_2 and
 * LOW_SUBTRAHEND_2 respectively.  The result is placed in HIGH_DIFFERENCE
 * and LOW_DIFFERENCE.  Overflow (i.e. carry out) is not stored anywhere,
 * and is lost.
 */
#define sub_ddmmss(sh, sl, ah, al, bh, bl)				\
  do {									\
    if (__builtin_constant_p (ah) && (ah) == 0)				\
      __asm__ ("{sf%I3|subf%I3c} %1,%4,%3\n\t{sfze|subfze} %0,%2"	\
	       : "=r" ((USItype)(sh)),					\
		 "=&r" ((USItype)(sl))					\
	       : "r" ((USItype)(bh)),					\
		 "rI" ((USItype)(al)),					\
		 "r" ((USItype)(bl)));					\
    else if (__builtin_constant_p (ah) && (ah) ==~(USItype) 0)		\
      __asm__ ("{sf%I3|subf%I3c} %1,%4,%3\n\t{sfme|subfme} %0,%2"	\
	       : "=r" ((USItype)(sh)),					\
		 "=&r" ((USItype)(sl))					\
	       : "r" ((USItype)(bh)),					\
		 "rI" ((USItype)(al)),					\
		 "r" ((USItype)(bl)));					\
    else if (__builtin_constant_p (bh) && (bh) == 0)			\
      __asm__ ("{sf%I3|subf%I3c} %1,%4,%3\n\t{ame|addme} %0,%2"		\
	       : "=r" ((USItype)(sh)),					\
		 "=&r" ((USItype)(sl))					\
	       : "r" ((USItype)(ah)),					\
		 "rI" ((USItype)(al)),					\
		 "r" ((USItype)(bl)));					\
    else if (__builtin_constant_p (bh) && (bh) ==~(USItype) 0)		\
      __asm__ ("{sf%I3|subf%I3c} %1,%4,%3\n\t{aze|addze} %0,%2"		\
	       : "=r" ((USItype)(sh)),					\
		 "=&r" ((USItype)(sl))					\
	       : "r" ((USItype)(ah)),					\
		 "rI" ((USItype)(al)),					\
		 "r" ((USItype)(bl)));					\
    else								\
      __asm__ ("{sf%I4|subf%I4c} %1,%5,%4\n\t{sfe|subfe} %0,%3,%2"	\
	       : "=r" ((USItype)(sh)),					\
		 "=&r" ((USItype)(sl))					\
	       : "r" ((USItype)(ah)),					\
		 "r" ((USItype)(bh)),					\
		 "rI" ((USItype)(al)),					\
		 "r" ((USItype)(bl)));					\
  } while (0)

/* asm fragments for mul and div */

/* umul_ppmm(high_prod, low_prod, multipler, multiplicand) multiplies two
 * UWtype integers MULTIPLER and MULTIPLICAND, and generates a two UWtype
 * word product in HIGH_PROD and LOW_PROD.
 */
#define umul_ppmm(ph, pl, m0, m1)					\
  do {									\
    USItype __m0 = (m0), __m1 = (m1);					\
    __asm__ ("mulhwu %0,%1,%2"						\
	     : "=r" ((USItype)(ph))					\
	     : "%r" (__m0),						\
               "r" (__m1));						\
    (pl) = __m0 * __m1;							\
  } while (0)

/* udiv_qrnnd(quotient, remainder, high_numerator, low_numerator,
 * denominator) divides a UDWtype, composed by the UWtype integers
 * HIGH_NUMERATOR and LOW_NUMERATOR, by DENOMINATOR and places the quotient
 * in QUOTIENT and the remainder in REMAINDER.  HIGH_NUMERATOR must be less
 * than DENOMINATOR for correct operation.  If, in addition, the most
 * significant bit of DENOMINATOR must be 1, then the pre-processor symbol
 * UDIV_NEEDS_NORMALIZATION is defined to 1.
 */
#define udiv_qrnnd(q, r, n1, n0, d)					\
  do {									\
    UWtype __d1, __d0, __q1, __q0, __r1, __r0, __m;			\
    __d1 = __ll_highpart (d);						\
    __d0 = __ll_lowpart (d);						\
									\
    __r1 = (n1) % __d1;							\
    __q1 = (n1) / __d1;							\
    __m = (UWtype) __q1 * __d0;						\
    __r1 = __r1 * __ll_B | __ll_highpart (n0);				\
    if (__r1 < __m)							\
      {									\
	__q1--, __r1 += (d);						\
	if (__r1 >= (d)) /* we didn't get carry when adding to __r1 */	\
	  if (__r1 < __m)						\
	    __q1--, __r1 += (d);					\
      }									\
    __r1 -= __m;							\
									\
    __r0 = __r1 % __d1;							\
    __q0 = __r1 / __d1;							\
    __m = (UWtype) __q0 * __d0;						\
    __r0 = __r0 * __ll_B | __ll_lowpart (n0);				\
    if (__r0 < __m)							\
      {									\
	__q0--, __r0 += (d);						\
	if (__r0 >= (d))						\
	  if (__r0 < __m)						\
	    __q0--, __r0 += (d);					\
      }									\
    __r0 -= __m;							\
									\
    (q) = (UWtype) __q1 * __ll_B | __q0;				\
    (r) = __r0;								\
  } while (0)

#define UDIV_NEEDS_NORMALIZATION 1

#define abort()								\
	return 0

#ifdef __BIG_ENDIAN
#define __BYTE_ORDER __BIG_ENDIAN
#else
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif

/* Exception flags. */
#define EFLAG_INVALID		(1 << (31 - 2))
#define EFLAG_OVERFLOW		(1 << (31 - 3))
#define EFLAG_UNDERFLOW		(1 << (31 - 4))
#define EFLAG_DIVZERO		(1 << (31 - 5))
#define EFLAG_INEXACT		(1 << (31 - 6))

#define EFLAG_VXSNAN		(1 << (31 - 7))
#define EFLAG_VXISI		(1 << (31 - 8))
#define EFLAG_VXIDI		(1 << (31 - 9))
#define EFLAG_VXZDZ		(1 << (31 - 10))
#define EFLAG_VXIMZ		(1 << (31 - 11))
#define EFLAG_VXVC		(1 << (31 - 12))
#define EFLAG_VXSOFT		(1 << (31 - 21))
#define EFLAG_VXSQRT		(1 << (31 - 22))
#define EFLAG_VXCVI		(1 << (31 - 23))
