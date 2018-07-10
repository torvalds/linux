/*
 * Copyright (C) 2017 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "tls.h"

/* The file is taken almost verbatim from matrixssl-3-7-2b-open/crypto/math/.
 * Changes are flagged with //bbox
 */

/**
 *	@file    pstm_montgomery_reduce.c
 *	@version 33ef80f (HEAD, tag: MATRIXSSL-3-7-2-OPEN, tag: MATRIXSSL-3-7-2-COMM, origin/master, origin/HEAD, master)
 *
 *	Multiprecision Montgomery Reduction.
 */
/*
 *	Copyright (c) 2013-2015 INSIDE Secure Corporation
 *	Copyright (c) PeerSec Networks, 2002-2011
 *	All Rights Reserved
 *
 *	The latest version of this code is available at http://www.matrixssl.org
 *
 *	This software is open source; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This General Public License does NOT permit incorporating this software
 *	into proprietary programs.  If you are unable to comply with the GPL, a
 *	commercial license for this software may be purchased from INSIDE at
 *	http://www.insidesecure.com/eng/Company/Locations
 *
 *	This program is distributed in WITHOUT ANY WARRANTY; without even the
 *	implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *	See the GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *	http://www.gnu.org/copyleft/gpl.html
 */
/******************************************************************************/

//bbox
//#include "../cryptoApi.h"
#ifndef DISABLE_PSTM

/******************************************************************************/

#if defined(PSTM_X86)
/* x86-32 optimized for 32 bit platforms. For 64 bit mode use X86_64 instead */
#if !defined(__GNUC__) || !defined(__i386__) || !defined(PSTM_32BIT)
#error "PSTM_X86 option requires GCC and 32 bit mode x86 processor"
#endif
//#pragma message ("Using 32 bit x86 Assembly Optimizations")

#define MONT_START
#define MONT_FINI
#define LOOP_END
#define LOOP_START \
   mu = c[x] * mp

#define INNERMUL                                          \
asm(                                                      \
   "movl %5,%%eax \n\t"                                   \
   "mull %4       \n\t"                                   \
   "addl %1,%%eax \n\t"                                   \
   "adcl $0,%%edx \n\t"                                   \
   "addl %%eax,%0 \n\t"                                   \
   "adcl $0,%%edx \n\t"                                   \
   "movl %%edx,%1 \n\t"                                   \
:"=g"(_c[LO]), "=r"(cy)                                   \
:"0"(_c[LO]), "1"(cy), "g"(mu), "g"(*tmpm++)              \
: "%eax", "%edx", "%cc")

#define PROPCARRY                           \
asm(                                        \
   "addl   %1,%0    \n\t"                   \
   "setb   %%al     \n\t"                   \
   "movzbl %%al,%1 \n\t"                    \
:"=g"(_c[LO]), "=r"(cy)                     \
:"0"(_c[LO]), "1"(cy)                       \
: "%eax", "%cc")

/******************************************************************************/
#elif defined(PSTM_X86_64)
/* x86-64 optimized */
#if !defined(__GNUC__) || !defined(__x86_64__) || !defined(PSTM_64BIT)
#error "PSTM_X86_64 option requires PSTM_64BIT, GCC and 64 bit mode x86 processor"
#endif
//#pragma message ("Using 64 bit x86_64 Assembly Optimizations")

#define MONT_START
#define MONT_FINI
#define LOOP_END
#define LOOP_START \
mu = c[x] * mp

#define INNERMUL                                           \
asm(                                                       \
	"movq %5,%%rax \n\t"                                   \
	"mulq %4       \n\t"                                   \
	"addq %1,%%rax \n\t"                                   \
	"adcq $0,%%rdx \n\t"                                   \
	"addq %%rax,%0 \n\t"                                   \
	"adcq $0,%%rdx \n\t"                                   \
	"movq %%rdx,%1 \n\t"                                   \
	:"=g"(_c[LO]), "=r"(cy)                                \
	:"0"(_c[LO]), "1"(cy), "r"(mu), "r"(*tmpm++)           \
	: "%rax", "%rdx", "cc")

#define INNERMUL8				\
asm(							\
	"movq 0(%5),%%rax    \n\t"  \
	"movq 0(%2),%%r10    \n\t"  \
	"movq 0x8(%5),%%r11  \n\t"  \
	"mulq %4             \n\t"  \
	"addq %%r10,%%rax    \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq 0x8(%2),%%r10  \n\t"  \
	"addq %3,%%rax       \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq %%rax,0(%0)    \n\t"  \
	"movq %%rdx,%1       \n\t"  \
	\
	"movq %%r11,%%rax    \n\t"  \
	"movq 0x10(%5),%%r11 \n\t"  \
	"mulq %4             \n\t"  \
	"addq %%r10,%%rax    \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq 0x10(%2),%%r10 \n\t"  \
	"addq %3,%%rax       \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq %%rax,0x8(%0)  \n\t"  \
	"movq %%rdx,%1       \n\t"  \
	\
	"movq %%r11,%%rax    \n\t"  \
	"movq 0x18(%5),%%r11 \n\t"  \
	"mulq %4             \n\t"  \
	"addq %%r10,%%rax    \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq 0x18(%2),%%r10 \n\t"  \
	"addq %3,%%rax       \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq %%rax,0x10(%0) \n\t"  \
	"movq %%rdx,%1       \n\t"  \
	\
	"movq %%r11,%%rax    \n\t"  \
	"movq 0x20(%5),%%r11 \n\t"  \
	"mulq %4             \n\t"  \
	"addq %%r10,%%rax    \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq 0x20(%2),%%r10 \n\t"  \
	"addq %3,%%rax       \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq %%rax,0x18(%0) \n\t"  \
	"movq %%rdx,%1       \n\t"  \
	\
	"movq %%r11,%%rax    \n\t"  \
	"movq 0x28(%5),%%r11 \n\t"  \
	"mulq %4             \n\t"  \
	"addq %%r10,%%rax    \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq 0x28(%2),%%r10 \n\t"  \
	"addq %3,%%rax       \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq %%rax,0x20(%0) \n\t"  \
	"movq %%rdx,%1       \n\t"  \
	\
	"movq %%r11,%%rax    \n\t"  \
	"movq 0x30(%5),%%r11 \n\t"  \
	"mulq %4             \n\t"  \
	"addq %%r10,%%rax    \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq 0x30(%2),%%r10 \n\t"  \
	"addq %3,%%rax       \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq %%rax,0x28(%0) \n\t"  \
	"movq %%rdx,%1       \n\t"  \
	\
	"movq %%r11,%%rax    \n\t"  \
	"movq 0x38(%5),%%r11 \n\t"  \
	"mulq %4             \n\t"  \
	"addq %%r10,%%rax    \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq 0x38(%2),%%r10 \n\t"  \
	"addq %3,%%rax       \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq %%rax,0x30(%0) \n\t"  \
	"movq %%rdx,%1       \n\t"  \
	\
	"movq %%r11,%%rax    \n\t"  \
	"mulq %4             \n\t"  \
	"addq %%r10,%%rax    \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"addq %3,%%rax       \n\t"  \
	"adcq $0,%%rdx       \n\t"  \
	"movq %%rax,0x38(%0) \n\t"  \
	"movq %%rdx,%1       \n\t"  \
	\
	:"=r"(_c), "=r"(cy)                    \
	: "0"(_c),  "1"(cy), "g"(mu), "r"(tmpm)\
	: "%rax", "%rdx", "%r10", "%r11", "cc")

#define PROPCARRY                          \
asm(                                       \
	"addq   %1,%0    \n\t"                 \
	"setb   %%al     \n\t"                 \
	"movzbq %%al,%1 \n\t"                  \
	:"=g"(_c[LO]), "=r"(cy)                \
	:"0"(_c[LO]), "1"(cy)                  \
	: "%rax", "cc")

/******************************************************************************/
#elif defined(PSTM_ARM)

#define MONT_START
#define MONT_FINI
#define LOOP_END
#define LOOP_START \
mu = c[x] * mp

#ifdef __thumb2__
//#pragma message ("Using 32 bit ARM Thumb2 Assembly Optimizations")
#define INNERMUL                    \
asm(                                \
	" LDR    r0,%1            \n\t" \
	" ADDS   r0,r0,%0         \n\t" \
	" ITE CS                  \n\t" \
	" MOVCS  %0,#1            \n\t" \
	" MOVCC  %0,#0            \n\t" \
	" UMLAL  r0,%0,%3,%4      \n\t" \
	" STR    r0,%1            \n\t" \
	:"=r"(cy),"=m"(_c[0])\
	:"0"(cy),"r"(mu),"r"(*tmpm++),"m"(_c[0])\
	:"r0","%cc");
#define PROPCARRY                  \
asm(                               \
	" LDR   r0,%1            \n\t" \
	" ADDS  r0,r0,%0         \n\t" \
	" STR   r0,%1            \n\t" \
	" ITE CS                 \n\t" \
	" MOVCS %0,#1            \n\t" \
	" MOVCC %0,#0            \n\t" \
	:"=r"(cy),"=m"(_c[0])\
	:"0"(cy),"m"(_c[0])\
	:"r0","%cc");
#else /* Non-Thumb2 code */
//#pragma message ("Using 32 bit ARM Assembly Optimizations")
#define INNERMUL                    \
asm(                                \
	" LDR    r0,%1            \n\t" \
	" ADDS   r0,r0,%0         \n\t" \
	" MOVCS  %0,#1            \n\t" \
	" MOVCC  %0,#0            \n\t" \
	" UMLAL  r0,%0,%3,%4      \n\t" \
	" STR    r0,%1            \n\t" \
	:"=r"(cy),"=m"(_c[0])\
	:"0"(cy),"r"(mu),"r"(*tmpm++),"m"(_c[0])\
	:"r0","%cc");
#define PROPCARRY                  \
asm(                               \
	" LDR   r0,%1            \n\t" \
	" ADDS  r0,r0,%0         \n\t" \
	" STR   r0,%1            \n\t" \
	" MOVCS %0,#1            \n\t" \
	" MOVCC %0,#0            \n\t" \
	:"=r"(cy),"=m"(_c[0])\
	:"0"(cy),"m"(_c[0])\
	:"r0","%cc");
#endif /* __thumb2__ */


/******************************************************************************/
#elif defined(PSTM_MIPS)
/* MIPS32 */
//#pragma message ("Using 32 bit MIPS Assembly Optimizations")
#define MONT_START
#define MONT_FINI
#define LOOP_END
#define LOOP_START \
mu = c[x] * mp

#define INNERMUL                      \
asm(								  \
	" multu    %3,%4          \n\t"   \
	" mflo     $12            \n\t"   \
	" mfhi     $13            \n\t"   \
	" addu     $12,$12,%0     \n\t"   \
	" sltu     $10,$12,%0     \n\t"   \
	" addu     $13,$13,$10    \n\t"   \
	" lw       $10,%1         \n\t"   \
	" addu     $12,$12,$10    \n\t"   \
	" sltu     $10,$12,$10    \n\t"   \
	" addu     %0,$13,$10     \n\t"   \
	" sw       $12,%1         \n\t"   \
	:"=r"(cy),"=m"(_c[0])\
	:"r"(cy),"r"(mu),"r"(tmpm[0]),"r"(_c[0])\
	:"$10","$12","$13")\
; ++tmpm;

#define PROPCARRY                     \
asm(                                  \
	" lw       $10,%1        \n\t"    \
	" addu     $10,$10,%0    \n\t"    \
	" sw       $10,%1        \n\t"    \
	" sltu     %0,$10,%0     \n\t"    \
	:"=r"(cy),"=m"(_c[0])\
	:"r"(cy),"r"(_c[0])\
	:"$10");


/******************************************************************************/
#else

/* ISO C code */
#define MONT_START
#define MONT_FINI
#define LOOP_END
#define LOOP_START \
   mu = c[x] * mp

#define INNERMUL										\
	do { pstm_word t;									\
		t = ((pstm_word)_c[0] + (pstm_word)cy) +		\
			(((pstm_word)mu) * ((pstm_word)*tmpm++));	\
		_c[0] = (pstm_digit)t;							\
		cy = (pstm_digit)(t >> DIGIT_BIT);				\
	} while (0)

#define PROPCARRY \
   do { pstm_digit t = _c[0] += cy; cy = (t < cy); } while (0)

#endif

/******************************************************************************/

#define LO 0

/* computes x/R == x (mod N) via Montgomery Reduction */
int32 pstm_montgomery_reduce(psPool_t *pool, pstm_int *a, pstm_int *m,
		pstm_digit mp, pstm_digit *paD, uint32 paDlen)
{
	pstm_digit	*c, *_c, *tmpm, mu;
	int32		oldused, x, y;
	int		pa; //bbox: was int16

	pa = m->used;
	if (pa > a->alloc) {
		/* Sanity test for bad numbers.  This will confirm no buffer overruns */
		return PS_LIMIT_FAIL;
	}

	if (paD && paDlen >= (uint32)2*pa+1) {
		c = paD;
		memset(c, 0x0, paDlen);
	} else {
		c = xzalloc(2*pa+1);//bbox
	}
	/* copy the input */
	oldused = a->used;
	for (x = 0; x < oldused; x++) {
		c[x] = a->dp[x];
	}

	MONT_START;

	for (x = 0; x < pa; x++) {
		pstm_digit cy = 0;
		/* get Mu for this round */
		LOOP_START;
		_c   = c + x;
		tmpm = m->dp;
		y = 0;
#ifdef PSTM_X86_64
		for (; y < (pa & ~7); y += 8) {
			INNERMUL8;
			_c   += 8;
			tmpm += 8;
		}
#endif /* PSTM_X86_64 */
		for (; y < pa; y++) {
			INNERMUL;
			++_c;
		}
		LOOP_END;
		while (cy) {
			PROPCARRY;
			++_c;
		}
	}

	/* now copy out */
	_c   = c + pa;
	tmpm = a->dp;
	for (x = 0; x < pa+1; x++) {
		*tmpm++ = *_c++;
	}

	for (; x < oldused; x++)   {
		*tmpm++ = 0;
	}

	MONT_FINI;

	a->used = pa+1;
	pstm_clamp(a);

	/* reuse x as return code */
	x = PSTM_OKAY;

	/* if A >= m then A = A - m */
	if (pstm_cmp_mag (a, m) != PSTM_LT) {
		if (s_pstm_sub (a, m, a) != PSTM_OKAY) {
			x = PS_MEM_FAIL;
		}
	}
	if (paDlen < (uint32)2*pa+1) {
		psFree(c, pool);
	}
	return x;
}

#endif /* !DISABLE_PSTM */
/******************************************************************************/
