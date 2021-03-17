/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2005-2018 Andes Technology Corporation */

#include <asm/bitfield.h>

#define _FP_W_TYPE_SIZE		32
#define _FP_W_TYPE		unsigned long
#define _FP_WS_TYPE		signed long
#define _FP_I_TYPE		long

#define __ll_B ((UWtype) 1 << (W_TYPE_SIZE / 2))
#define __ll_lowpart(t) ((UWtype) (t) & (__ll_B - 1))
#define __ll_highpart(t) ((UWtype) (t) >> (W_TYPE_SIZE / 2))

#define _FP_MUL_MEAT_S(R, X, Y)				\
	_FP_MUL_MEAT_1_wide(_FP_WFRACBITS_S, R, X, Y, umul_ppmm)
#define _FP_MUL_MEAT_D(R, X, Y)				\
	_FP_MUL_MEAT_2_wide(_FP_WFRACBITS_D, R, X, Y, umul_ppmm)
#define _FP_MUL_MEAT_Q(R, X, Y)				\
	_FP_MUL_MEAT_4_wide(_FP_WFRACBITS_Q, R, X, Y, umul_ppmm)

#define _FP_MUL_MEAT_DW_S(R, X, Y)			\
	_FP_MUL_MEAT_DW_1_wide(_FP_WFRACBITS_S, R, X, Y, umul_ppmm)
#define _FP_MUL_MEAT_DW_D(R, X, Y)			\
	_FP_MUL_MEAT_DW_2_wide(_FP_WFRACBITS_D, R, X, Y, umul_ppmm)

#define _FP_DIV_MEAT_S(R, X, Y)	_FP_DIV_MEAT_1_udiv_norm(S, R, X, Y)
#define _FP_DIV_MEAT_D(R, X, Y)	_FP_DIV_MEAT_2_udiv(D, R, X, Y)

#define _FP_NANFRAC_S		((_FP_QNANBIT_S << 1) - 1)
#define _FP_NANFRAC_D		((_FP_QNANBIT_D << 1) - 1), -1
#define _FP_NANFRAC_Q		((_FP_QNANBIT_Q << 1) - 1), -1, -1, -1
#define _FP_NANSIGN_S		0
#define _FP_NANSIGN_D		0
#define _FP_NANSIGN_Q		0

#define _FP_KEEPNANFRACP 1
#define _FP_QNANNEGATEDP 0

#define _FP_CHOOSENAN(fs, wc, R, X, Y, OP)			\
do {								\
	if ((_FP_FRAC_HIGH_RAW_##fs(X) & _FP_QNANBIT_##fs)	\
	  && !(_FP_FRAC_HIGH_RAW_##fs(Y) & _FP_QNANBIT_##fs)) { \
		R##_s = Y##_s;					\
		_FP_FRAC_COPY_##wc(R, Y);			\
	} else {						\
		R##_s = X##_s;					\
		_FP_FRAC_COPY_##wc(R, X);			\
	}							\
	R##_c = FP_CLS_NAN;					\
} while (0)

#define __FPU_FPCSR	(current->thread.fpu.fpcsr)

/* Obtain the current rounding mode. */
#define FP_ROUNDMODE                    \
({                                      \
	__FPU_FPCSR & FPCSR_mskRM;      \
})

#define FP_RND_NEAREST		0
#define FP_RND_PINF		1
#define FP_RND_MINF		2
#define FP_RND_ZERO		3

#define FP_EX_INVALID		FPCSR_mskIVO
#define FP_EX_DIVZERO		FPCSR_mskDBZ
#define FP_EX_OVERFLOW		FPCSR_mskOVF
#define FP_EX_UNDERFLOW		FPCSR_mskUDF
#define FP_EX_INEXACT		FPCSR_mskIEX

#define SF_CEQ	2
#define SF_CLT	1
#define SF_CGT	3
#define SF_CUN	4

#include <asm/byteorder.h>

#ifdef __BIG_ENDIAN__
#define __BYTE_ORDER __BIG_ENDIAN
#define __LITTLE_ENDIAN 0
#else
#define __BYTE_ORDER __LITTLE_ENDIAN
#define __BIG_ENDIAN 0
#endif

#define abort() do { } while (0)
#define umul_ppmm(w1, w0, u, v)						\
do {									\
	UWtype __x0, __x1, __x2, __x3;                                  \
	UHWtype __ul, __vl, __uh, __vh;                                 \
									\
	__ul = __ll_lowpart(u);						\
	__uh = __ll_highpart(u);					\
	__vl = __ll_lowpart(v);						\
	__vh = __ll_highpart(v);					\
									\
	__x0 = (UWtype) __ul * __vl;                                    \
	__x1 = (UWtype) __ul * __vh;                                    \
	__x2 = (UWtype) __uh * __vl;                                    \
	__x3 = (UWtype) __uh * __vh;                                    \
									\
	__x1 += __ll_highpart(__x0);					\
	__x1 += __x2;							\
	if (__x1 < __x2)						\
		__x3 += __ll_B;						\
									\
	(w1) = __x3 + __ll_highpart(__x1);				\
	(w0) = __ll_lowpart(__x1) * __ll_B + __ll_lowpart(__x0);	\
} while (0)

#define add_ssaaaa(sh, sl, ah, al, bh, bl) \
do { \
	UWtype __x; \
	__x = (al) + (bl); \
	(sh) = (ah) + (bh) + (__x < (al)); \
	(sl) = __x; \
} while (0)

#define sub_ddmmss(sh, sl, ah, al, bh, bl) \
do { \
	UWtype __x; \
	__x = (al) - (bl); \
	(sh) = (ah) - (bh) - (__x > (al)); \
	(sl) = __x; \
} while (0)

#define udiv_qrnnd(q, r, n1, n0, d)				\
do {								\
	UWtype __d1, __d0, __q1, __q0, __r1, __r0, __m;		\
	__d1 = __ll_highpart(d);				\
	__d0 = __ll_lowpart(d);					\
								\
	__r1 = (n1) % __d1;					\
	__q1 = (n1) / __d1;					\
	__m = (UWtype) __q1 * __d0;				\
	__r1 = __r1 * __ll_B | __ll_highpart(n0);		\
	if (__r1 < __m)	{					\
		__q1--, __r1 += (d);				\
		if (__r1 >= (d))				\
			if (__r1 < __m)				\
				__q1--, __r1 += (d);		\
	}							\
	__r1 -= __m;						\
	__r0 = __r1 % __d1;					\
	__q0 = __r1 / __d1;					\
	__m = (UWtype) __q0 * __d0;				\
	__r0 = __r0 * __ll_B | __ll_lowpart(n0);		\
	if (__r0 < __m)	{					\
		__q0--, __r0 += (d);				\
		if (__r0 >= (d))				\
			if (__r0 < __m)				\
				__q0--, __r0 += (d);		\
	}							\
	__r0 -= __m;						\
	(q) = (UWtype) __q1 * __ll_B | __q0;			\
	(r) = __r0;						\
} while (0)
