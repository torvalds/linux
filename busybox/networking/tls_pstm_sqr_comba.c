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
 *	@file    pstm_sqr_comba.c
 *	@version 33ef80f (HEAD, tag: MATRIXSSL-3-7-2-OPEN, tag: MATRIXSSL-3-7-2-COMM, origin/master, origin/HEAD, master)
 *
 *	Multiprecision Squaring with Comba technique.
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
#if !defined(__GNUC__) || !defined(__i386__)
#error "PSTM_X86 option requires GCC and 32 bit mode x86 processor"
#endif
//#pragma message ("Using 32 bit x86 Assembly Optimizations")

#define COMBA_START

#define CLEAR_CARRY \
   c0 = c1 = c2 = 0;

#define COMBA_STORE(x) \
   x = c0;

#define COMBA_STORE2(x) \
   x = c1;

#define CARRY_FORWARD \
   do { c0 = c1; c1 = c2; c2 = 0; } while (0);

#define COMBA_FINI

#define SQRADD(i, j)                                      \
asm(                                            \
	 "movl  %6,%%eax     \n\t"                            \
	 "mull  %%eax        \n\t"                            \
	 "addl  %%eax,%0     \n\t"                            \
	 "adcl  %%edx,%1     \n\t"                            \
	 "adcl  $0,%2        \n\t"                            \
	 :"=rm"(c0), "=rm"(c1), "=rm"(c2): "0"(c0), "1"(c1), "2"(c2), "m"(i) :"%eax","%edx","%cc");
	//bbox: ^^^ replaced "=r" with "=rm": %ebx is not available on shared build

#define SQRADD2(i, j)                                     \
asm(                                            \
	 "movl  %6,%%eax     \n\t"                            \
	 "mull  %7           \n\t"                            \
	 "addl  %%eax,%0     \n\t"                            \
	 "adcl  %%edx,%1     \n\t"                            \
	 "adcl  $0,%2        \n\t"                            \
	 "addl  %%eax,%0     \n\t"                            \
	 "adcl  %%edx,%1     \n\t"                            \
	 "adcl  $0,%2        \n\t"                            \
	 :"=rm"(c0), "=rm"(c1), "=rm"(c2): "0"(c0), "1"(c1), "2"(c2), "m"(i), "m"(j)  :"%eax","%edx","%cc");
	//bbox: ^^^ replaced "=r" with "=rm": %ebx is not available on shared build

#define SQRADDSC(i, j)                                    \
asm(                                                     \
	 "movl  %6,%%eax     \n\t"                            \
	 "mull  %7           \n\t"                            \
	 "movl  %%eax,%0     \n\t"                            \
	 "movl  %%edx,%1     \n\t"                            \
	 "xorl  %2,%2        \n\t"                            \
	 :"=r"(sc0), "=r"(sc1), "=r"(sc2): "0"(sc0), "1"(sc1), "2"(sc2), "g"(i), "g"(j) :"%eax","%edx","%cc");

#define SQRADDAC(i, j)                                    \
asm(                                                     \
	 "movl  %6,%%eax     \n\t"                            \
	 "mull  %7           \n\t"                            \
	 "addl  %%eax,%0     \n\t"                            \
	 "adcl  %%edx,%1     \n\t"                            \
	 "adcl  $0,%2        \n\t"                            \
	 :"=r"(sc0), "=r"(sc1), "=r"(sc2): "0"(sc0), "1"(sc1), "2"(sc2), "g"(i), "g"(j) :"%eax","%edx","%cc");

#define SQRADDDB                                          \
asm(                                                     \
	 "addl %6,%0         \n\t"                            \
	 "adcl %7,%1         \n\t"                            \
	 "adcl %8,%2         \n\t"                            \
	 "addl %6,%0         \n\t"                            \
	 "adcl %7,%1         \n\t"                            \
	 "adcl %8,%2         \n\t"                            \
	 :"=r"(c0), "=r"(c1), "=r"(c2) : "0"(c0), "1"(c1), "2"(c2), "r"(sc0), "r"(sc1), "r"(sc2) : "%cc");

/******************************************************************************/
#elif defined(PSTM_X86_64)
/* x86-64 optimized */
#if !defined(__GNUC__) || !defined(__x86_64__) || !defined(PSTM_64BIT)
#error "PSTM_X86_64 option requires PSTM_64BIT, GCC and 64 bit mode x86 processor"
#endif
//#pragma message ("Using 64 bit x86_64 Assembly Optimizations")

#define COMBA_START

#define CLEAR_CARRY \
c0 = c1 = c2 = 0;

#define COMBA_STORE(x) \
x = c0;

#define COMBA_STORE2(x) \
x = c1;

#define CARRY_FORWARD \
do { c0 = c1; c1 = c2; c2 = 0; } while (0);

#define COMBA_FINI

#define SQRADD(i, j)                                     \
asm(                                                     \
	"movq  %6,%%rax     \n\t"                            \
	"mulq  %%rax        \n\t"                            \
	"addq  %%rax,%0     \n\t"                            \
	"adcq  %%rdx,%1     \n\t"                            \
	"adcq  $0,%2        \n\t"                            \
	:"=r"(c0), "=r"(c1), "=r"(c2): "0"(c0), "1"(c1), "2"(c2), "g"(i) :"%rax","%rdx","cc");

#define SQRADD2(i, j)                                    \
asm(                                                     \
	"movq  %6,%%rax     \n\t"                            \
	"mulq  %7           \n\t"                            \
	"addq  %%rax,%0     \n\t"                            \
	"adcq  %%rdx,%1     \n\t"                            \
	"adcq  $0,%2        \n\t"                            \
	"addq  %%rax,%0     \n\t"                            \
	"adcq  %%rdx,%1     \n\t"                            \
	"adcq  $0,%2        \n\t"                            \
	:"=r"(c0), "=r"(c1), "=r"(c2): "0"(c0), "1"(c1), "2"(c2), "g"(i), "g"(j)  :"%rax","%rdx","cc");

#define SQRADDSC(i, j)                                   \
asm(                                                     \
	"movq  %6,%%rax     \n\t"                            \
	"mulq  %7           \n\t"                            \
	"movq  %%rax,%0     \n\t"                            \
	"movq  %%rdx,%1     \n\t"                            \
	"xorq  %2,%2        \n\t"                            \
	:"=r"(sc0), "=r"(sc1), "=r"(sc2): "0"(sc0), "1"(sc1), "2"(sc2), "g"(i), "g"(j) :"%rax","%rdx","cc");

#define SQRADDAC(i, j)                                   \
asm(                                                     \
	"movq  %6,%%rax     \n\t"                            \
	"mulq  %7           \n\t"                            \
	"addq  %%rax,%0     \n\t"                            \
	"adcq  %%rdx,%1     \n\t"                            \
	"adcq  $0,%2        \n\t"                            \
	:"=r"(sc0), "=r"(sc1), "=r"(sc2): "0"(sc0), "1"(sc1), "2"(sc2), "g"(i), "g"(j) :"%rax","%rdx","cc");

#define SQRADDDB                                         \
asm(                                                     \
	"addq %6,%0         \n\t"                            \
	"adcq %7,%1         \n\t"                            \
	"adcq %8,%2         \n\t"                            \
	"addq %6,%0         \n\t"                            \
	"adcq %7,%1         \n\t"                            \
	"adcq %8,%2         \n\t"                            \
	:"=r"(c0), "=r"(c1), "=r"(c2) : "0"(c0), "1"(c1), "2"(c2), "r"(sc0), "r"(sc1), "r"(sc2) : "cc");

/******************************************************************************/
#elif defined(PSTM_ARM)
/* ARM code */
//#pragma message ("Using 32 bit ARM Assembly Optimizations")

#define COMBA_START

#define CLEAR_CARRY \
c0 = c1 = c2 = 0;

#define COMBA_STORE(x) \
x = c0;

#define COMBA_STORE2(x) \
x = c1;

#define CARRY_FORWARD \
do { c0 = c1; c1 = c2; c2 = 0; } while (0);

#define COMBA_FINI

/* multiplies point i and j, updates carry "c1" and digit c2 */
#define SQRADD(i, j)                                             \
asm(                                                             \
"  UMULL  r0,r1,%6,%6              \n\t"                         \
"  ADDS   %0,%0,r0                 \n\t"                         \
"  ADCS   %1,%1,r1                 \n\t"                         \
"  ADC    %2,%2,#0                 \n\t"                         \
:"=r"(c0), "=r"(c1), "=r"(c2) : "0"(c0), "1"(c1), "2"(c2), "r"(i) : "r0", "r1", "%cc");

/* for squaring some of the terms are doubled... */
#define SQRADD2(i, j)                                            \
asm(                                                             \
"  UMULL  r0,r1,%6,%7              \n\t"                         \
"  ADDS   %0,%0,r0                 \n\t"                         \
"  ADCS   %1,%1,r1                 \n\t"                         \
"  ADC    %2,%2,#0                 \n\t"                         \
"  ADDS   %0,%0,r0                 \n\t"                         \
"  ADCS   %1,%1,r1                 \n\t"                         \
"  ADC    %2,%2,#0                 \n\t"                         \
:"=r"(c0), "=r"(c1), "=r"(c2) : "0"(c0), "1"(c1), "2"(c2), "r"(i), "r"(j) : "r0", "r1", "%cc");

#define SQRADDSC(i, j)                                           \
asm(                                                             \
"  UMULL  %0,%1,%6,%7              \n\t"                         \
"  SUB    %2,%2,%2                 \n\t"                         \
:"=r"(sc0), "=r"(sc1), "=r"(sc2) : "0"(sc0), "1"(sc1), "2"(sc2), "r"(i), "r"(j) : "%cc");

#define SQRADDAC(i, j)                                           \
asm(                                                             \
"  UMULL  r0,r1,%6,%7              \n\t"                         \
"  ADDS   %0,%0,r0                 \n\t"                         \
"  ADCS   %1,%1,r1                 \n\t"                         \
"  ADC    %2,%2,#0                 \n\t"                         \
:"=r"(sc0), "=r"(sc1), "=r"(sc2) : "0"(sc0), "1"(sc1), "2"(sc2), "r"(i), "r"(j) : "r0", "r1", "%cc");

#define SQRADDDB                                                 \
asm(                                                             \
"  ADDS  %0,%0,%3                     \n\t"                      \
"  ADCS  %1,%1,%4                     \n\t"                      \
"  ADC   %2,%2,%5                     \n\t"                      \
"  ADDS  %0,%0,%3                     \n\t"                      \
"  ADCS  %1,%1,%4                     \n\t"                      \
"  ADC   %2,%2,%5                     \n\t"                      \
:"=r"(c0), "=r"(c1), "=r"(c2) : "r"(sc0), "r"(sc1), "r"(sc2), "0"(c0), "1"(c1), "2"(c2) : "%cc");

/******************************************************************************/
#elif defined(PSTM_MIPS)
/* MIPS32 */
//#pragma message ("Using 32 bit MIPS Assembly Optimizations")

#define COMBA_START

#define CLEAR_CARRY \
c0 = c1 = c2 = 0;

#define COMBA_STORE(x) \
x = c0;

#define COMBA_STORE2(x) \
x = c1;

#define CARRY_FORWARD \
do { c0 = c1; c1 = c2; c2 = 0; } while (0);

#define COMBA_FINI

/* multiplies point i and j, updates carry "c1" and digit c2 */
#define SQRADD(i, j)               \
asm(                               \
	" multu  %6,%6          \n\t"  \
	" mflo   $12            \n\t"  \
	" mfhi   $13            \n\t"  \
	" addu    %0,%0,$12     \n\t"  \
	" sltu   $12,%0,$12     \n\t"  \
	" addu    %1,%1,$13     \n\t"  \
	" sltu   $13,%1,$13     \n\t"  \
	" addu    %1,%1,$12     \n\t"  \
	" sltu   $12,%1,$12     \n\t"  \
	" addu    %2,%2,$13     \n\t"  \
	" addu    %2,%2,$12     \n\t"  \
	:"=r"(c0), "=r"(c1), "=r"(c2):"0"(c0), "1"(c1), "2"(c2), "r"(i):"$12","$13");

/* for squaring some of the terms are doubled... */
#define SQRADD2(i, j)             \
asm(                              \
	" multu  %6,%7          \n\t" \
	" mflo   $12            \n\t" \
	" mfhi   $13            \n\t" \
	\
	" addu    %0,%0,$12     \n\t" \
	" sltu   $14,%0,$12     \n\t" \
	" addu    %1,%1,$13     \n\t" \
	" sltu   $15,%1,$13     \n\t" \
	" addu    %1,%1,$14     \n\t" \
	" sltu   $14,%1,$14     \n\t" \
	" addu    %2,%2,$15     \n\t" \
	" addu    %2,%2,$14     \n\t" \
	\
	" addu    %0,%0,$12     \n\t" \
	" sltu   $14,%0,$12     \n\t" \
	" addu    %1,%1,$13     \n\t" \
	" sltu   $15,%1,$13     \n\t" \
	" addu    %1,%1,$14     \n\t" \
	" sltu   $14,%1,$14     \n\t" \
	" addu    %2,%2,$15     \n\t" \
	" addu    %2,%2,$14     \n\t" \
	:"=r"(c0), "=r"(c1), "=r"(c2):"0"(c0), "1"(c1), "2"(c2), "r"(i), "r"(j):"$12", "$13", "$14", "$15");

#define SQRADDSC(i, j)             \
asm(                               \
	" multu  %6,%7          \n\t"  \
	" mflo   %0             \n\t"  \
	" mfhi   %1             \n\t"  \
	" xor    %2,%2,%2       \n\t"  \
	:"=r"(sc0), "=r"(sc1), "=r"(sc2):"0"(sc0), "1"(sc1), "2"(sc2), "r"(i),"r"(j) : "%cc");

#define SQRADDAC(i, j)            \
asm(                              \
	" multu  %6,%7          \n\t" \
	" mflo   $12            \n\t" \
	" mfhi   $13            \n\t" \
	" addu    %0,%0,$12     \n\t" \
	" sltu   $12,%0,$12     \n\t" \
	" addu    %1,%1,$13     \n\t" \
	" sltu   $13,%1,$13     \n\t" \
	" addu    %1,%1,$12     \n\t" \
	" sltu   $12,%1,$12     \n\t" \
	" addu    %2,%2,$13     \n\t" \
	" addu    %2,%2,$12     \n\t" \
	:"=r"(sc0), "=r"(sc1), "=r"(sc2):"0"(sc0), "1"(sc1), "2"(sc2), "r"(i), "r"(j):"$12", "$13", "$14");

#define SQRADDDB                   \
asm(                               \
	" addu    %0,%0,%3       \n\t" \
	" sltu   $10,%0,%3       \n\t" \
	" addu    %1,%1,$10      \n\t" \
	" sltu   $10,%1,$10      \n\t" \
	" addu    %1,%1,%4       \n\t" \
	" sltu   $11,%1,%4       \n\t" \
	" addu    %2,%2,$10      \n\t" \
	" addu    %2,%2,$11      \n\t" \
	" addu    %2,%2,%5       \n\t" \
	\
	" addu    %0,%0,%3       \n\t" \
	" sltu   $10,%0,%3       \n\t" \
	" addu    %1,%1,$10      \n\t" \
	" sltu   $10,%1,$10      \n\t" \
	" addu    %1,%1,%4       \n\t" \
	" sltu   $11,%1,%4       \n\t" \
	" addu    %2,%2,$10      \n\t" \
	" addu    %2,%2,$11      \n\t" \
	" addu    %2,%2,%5       \n\t" \
	:"=r"(c0), "=r"(c1), "=r"(c2) : "r"(sc0), "r"(sc1), "r"(sc2), "0"(c0), "1"(c1), "2"(c2) : "$10", "$11");

#else
/******************************************************************************/
#define PSTM_ISO
/* ISO C portable code */

#define COMBA_START

#define CLEAR_CARRY \
   c0 = c1 = c2 = 0;

#define COMBA_STORE(x) \
   x = c0;

#define COMBA_STORE2(x) \
   x = c1;

#define CARRY_FORWARD \
   do { c0 = c1; c1 = c2; c2 = 0; } while (0);

#define COMBA_FINI

/* multiplies point i and j, updates carry "c1" and digit c2 */
#define SQRADD(i, j)													\
   do { pstm_word t;													\
   t = c0 + ((pstm_word)i) * ((pstm_word)j);  c0 = (pstm_digit)t;		\
   t = c1 + (t >> DIGIT_BIT);											\
   c1 = (pstm_digit)t; c2 += (pstm_digit)(t >> DIGIT_BIT);				\
   } while (0);


/* for squaring some of the terms are doubled... */
#define SQRADD2(i, j)											\
   do { pstm_word t;											\
   t  = ((pstm_word)i) * ((pstm_word)j);						\
   tt = (pstm_word)c0 + t; c0 = (pstm_digit)tt;					\
   tt = (pstm_word)c1 + (tt >> DIGIT_BIT);						\
   c1 = (pstm_digit)tt; c2 += (pstm_digit)(tt >> DIGIT_BIT);	\
   tt = (pstm_word)c0 + t; c0 = (pstm_digit)tt;					\
   tt = (pstm_word)c1 + (tt >> DIGIT_BIT);						\
   c1 = (pstm_digit)tt; c2 += (pstm_digit)(tt >> DIGIT_BIT);	\
   } while (0);

#define SQRADDSC(i, j)										\
   do { pstm_word t;										\
	  t =  ((pstm_word)i) * ((pstm_word)j);					\
	  sc0 = (pstm_digit)t; sc1 = (pstm_digit)(t >> DIGIT_BIT); sc2 = 0;	\
   } while (0);

#define SQRADDAC(i, j)														\
   do { pstm_word t;														\
   t = ((pstm_word)sc0) + ((pstm_word)i) * ((pstm_word)j);					\
   sc0 = (pstm_digit)t;														\
   t = ((pstm_word)sc1) + (t >> DIGIT_BIT); sc1 = (pstm_digit)t;			\
   sc2 += (pstm_digit)(t >> DIGIT_BIT);										\
   } while (0);

#define SQRADDDB															\
   do { pstm_word t;														\
   t = ((pstm_word)sc0) + ((pstm_word)sc0) + ((pstm_word)c0);				\
   c0 = (pstm_digit)t;														\
   t = ((pstm_word)sc1) + ((pstm_word)sc1) + c1 + (t >> DIGIT_BIT);			\
   c1 = (pstm_digit)t;														\
   c2 = c2 + sc2 + sc2 + (pstm_digit)(t >> DIGIT_BIT);						\
   } while (0);

#endif /* ISO_C */

/******************************************************************************/
/*
	Non-unrolled comba squarer
 */
//bbox: pool unused
#define pstm_sqr_comba_gen(pool, A, B, paD, paDlen) \
        pstm_sqr_comba_gen(      A, B, paD, paDlen)
static int32 pstm_sqr_comba_gen(psPool_t *pool, pstm_int *A, pstm_int *B,
			pstm_digit *paD, uint32 paDlen)
{
	int		paDfail, pa; //bbox: was int16
	int32       ix, iz;
	pstm_digit  c0, c1, c2, *dst;
#ifdef PSTM_ISO
	pstm_word   tt;
#endif

	paDfail = 0;
	/* get size of output and trim */
	pa = A->used + A->used;

	/* number of output digits to produce */
	COMBA_START;
	CLEAR_CARRY;
/*
	If b is not large enough grow it and continue
*/
	if (B->alloc < pa) {
		if (pstm_grow(B, pa) != PSTM_OKAY) {
			return PS_MEM_FAIL;
		}
	}
	if (paD != NULL) {
		if (paDlen < (sizeof(pstm_digit) * pa)) {
			paDfail = 1; /* have a paD, but it's not big enough */
			dst = xzalloc(sizeof(pstm_digit) * pa);//bbox
		} else {
			dst = paD;
			memset(dst, 0x0, paDlen);
		}
	} else {
		dst = xzalloc(sizeof(pstm_digit) * pa);//bbox
	}

	for (ix = 0; ix < pa; ix++) {
		int32      tx, ty, iy;
		pstm_digit *tmpy, *tmpx;

		/* get offsets into the two bignums */
		ty = min(A->used-1, ix);
		tx = ix - ty;

		/* setup temp aliases */
		tmpx = A->dp + tx;
		tmpy = A->dp + ty;

/*
			This is the number of times the loop will iterate,
				while (tx++ < a->used && ty-- >= 0) { ... }
*/
		iy = min(A->used-tx, ty+1);

/*
		now for squaring tx can never equal ty. We halve the distance since
		they approach at a rate of 2x and we have to round because odd cases
		need to be executed
*/
		iy = min(iy, (ty-tx+1)>>1);

		/* forward carries */
		CARRY_FORWARD;

		/* execute loop */
		for (iz = 0; iz < iy; iz++) {
			SQRADD2(*tmpx++, *tmpy--);
		}

		/* even columns have the square term in them */
		if ((ix&1) == 0) {
			SQRADD(A->dp[ix>>1], A->dp[ix>>1]);
		}

		/* store it */
		COMBA_STORE(dst[ix]);
	}

	COMBA_FINI;
/*
	setup dest
 */
	iz  = B->used;
	B->used = pa;
	{
		pstm_digit *tmpc;
		tmpc = B->dp;
		for (ix = 0; ix < pa; ix++) {
			*tmpc++ = dst[ix];
		}
		/*	clear unused digits (that existed in the old copy of c) */
		for (; ix < iz; ix++) {
			*tmpc++ = 0;
		}
	}
	pstm_clamp(B);

	if ((paD == NULL) || paDfail == 1) {
		psFree(dst, pool);
	}
	return PS_SUCCESS;
}

/******************************************************************************/
/*
	Unrolled Comba loop for 1024 bit keys
 */
#ifdef USE_1024_KEY_SPEED_OPTIMIZATIONS
static int32 pstm_sqr_comba16(pstm_int *A, pstm_int *B)
{
	pstm_digit *a, b[32], c0, c1, c2, sc0, sc1, sc2;
#ifdef PSTM_ISO
	pstm_word   tt;
#endif

	if (B->alloc < 32) {
		if (pstm_grow(B, 32) != PSTM_OKAY) {
			return PS_MEM_FAIL;
		}
	}
	a = A->dp;
	sc0 = sc1 = sc2 = 0;

	COMBA_START;

   /* clear carries */
   CLEAR_CARRY;

   /* output 0 */
   SQRADD(a[0],a[0]);
   COMBA_STORE(b[0]);

   /* output 1 */
   CARRY_FORWARD;
   SQRADD2(a[0], a[1]);
   COMBA_STORE(b[1]);

   /* output 2 */
   CARRY_FORWARD;
   SQRADD2(a[0], a[2]); SQRADD(a[1], a[1]);
   COMBA_STORE(b[2]);

   /* output 3 */
   CARRY_FORWARD;
   SQRADD2(a[0], a[3]); SQRADD2(a[1], a[2]);
   COMBA_STORE(b[3]);

   /* output 4 */
   CARRY_FORWARD;
   SQRADD2(a[0], a[4]); SQRADD2(a[1], a[3]); SQRADD(a[2], a[2]);
   COMBA_STORE(b[4]);

   /* output 5 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[5]); SQRADDAC(a[1], a[4]); SQRADDAC(a[2], a[3]); SQRADDDB;
   COMBA_STORE(b[5]);

   /* output 6 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[6]); SQRADDAC(a[1], a[5]); SQRADDAC(a[2], a[4]); SQRADDDB; SQRADD(a[3], a[3]);
   COMBA_STORE(b[6]);

   /* output 7 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[7]); SQRADDAC(a[1], a[6]); SQRADDAC(a[2], a[5]); SQRADDAC(a[3], a[4]); SQRADDDB;
   COMBA_STORE(b[7]);

   /* output 8 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[8]); SQRADDAC(a[1], a[7]); SQRADDAC(a[2], a[6]); SQRADDAC(a[3], a[5]); SQRADDDB; SQRADD(a[4], a[4]);
   COMBA_STORE(b[8]);

   /* output 9 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[9]); SQRADDAC(a[1], a[8]); SQRADDAC(a[2], a[7]); SQRADDAC(a[3], a[6]); SQRADDAC(a[4], a[5]); SQRADDDB;
   COMBA_STORE(b[9]);

   /* output 10 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[10]); SQRADDAC(a[1], a[9]); SQRADDAC(a[2], a[8]); SQRADDAC(a[3], a[7]); SQRADDAC(a[4], a[6]); SQRADDDB; SQRADD(a[5], a[5]);
   COMBA_STORE(b[10]);

   /* output 11 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[11]); SQRADDAC(a[1], a[10]); SQRADDAC(a[2], a[9]); SQRADDAC(a[3], a[8]); SQRADDAC(a[4], a[7]); SQRADDAC(a[5], a[6]); SQRADDDB;
   COMBA_STORE(b[11]);

   /* output 12 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[12]); SQRADDAC(a[1], a[11]); SQRADDAC(a[2], a[10]); SQRADDAC(a[3], a[9]); SQRADDAC(a[4], a[8]); SQRADDAC(a[5], a[7]); SQRADDDB; SQRADD(a[6], a[6]);
   COMBA_STORE(b[12]);

   /* output 13 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[13]); SQRADDAC(a[1], a[12]); SQRADDAC(a[2], a[11]); SQRADDAC(a[3], a[10]); SQRADDAC(a[4], a[9]); SQRADDAC(a[5], a[8]); SQRADDAC(a[6], a[7]); SQRADDDB;
   COMBA_STORE(b[13]);

   /* output 14 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[14]); SQRADDAC(a[1], a[13]); SQRADDAC(a[2], a[12]); SQRADDAC(a[3], a[11]); SQRADDAC(a[4], a[10]); SQRADDAC(a[5], a[9]); SQRADDAC(a[6], a[8]); SQRADDDB; SQRADD(a[7], a[7]);
   COMBA_STORE(b[14]);

   /* output 15 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[15]); SQRADDAC(a[1], a[14]); SQRADDAC(a[2], a[13]); SQRADDAC(a[3], a[12]); SQRADDAC(a[4], a[11]); SQRADDAC(a[5], a[10]); SQRADDAC(a[6], a[9]); SQRADDAC(a[7], a[8]); SQRADDDB;
   COMBA_STORE(b[15]);

   /* output 16 */
   CARRY_FORWARD;
   SQRADDSC(a[1], a[15]); SQRADDAC(a[2], a[14]); SQRADDAC(a[3], a[13]); SQRADDAC(a[4], a[12]); SQRADDAC(a[5], a[11]); SQRADDAC(a[6], a[10]); SQRADDAC(a[7], a[9]); SQRADDDB; SQRADD(a[8], a[8]);
   COMBA_STORE(b[16]);

   /* output 17 */
   CARRY_FORWARD;
   SQRADDSC(a[2], a[15]); SQRADDAC(a[3], a[14]); SQRADDAC(a[4], a[13]); SQRADDAC(a[5], a[12]); SQRADDAC(a[6], a[11]); SQRADDAC(a[7], a[10]); SQRADDAC(a[8], a[9]); SQRADDDB;
   COMBA_STORE(b[17]);

   /* output 18 */
   CARRY_FORWARD;
   SQRADDSC(a[3], a[15]); SQRADDAC(a[4], a[14]); SQRADDAC(a[5], a[13]); SQRADDAC(a[6], a[12]); SQRADDAC(a[7], a[11]); SQRADDAC(a[8], a[10]); SQRADDDB; SQRADD(a[9], a[9]);
   COMBA_STORE(b[18]);

   /* output 19 */
   CARRY_FORWARD;
   SQRADDSC(a[4], a[15]); SQRADDAC(a[5], a[14]); SQRADDAC(a[6], a[13]); SQRADDAC(a[7], a[12]); SQRADDAC(a[8], a[11]); SQRADDAC(a[9], a[10]); SQRADDDB;
   COMBA_STORE(b[19]);

   /* output 20 */
   CARRY_FORWARD;
   SQRADDSC(a[5], a[15]); SQRADDAC(a[6], a[14]); SQRADDAC(a[7], a[13]); SQRADDAC(a[8], a[12]); SQRADDAC(a[9], a[11]); SQRADDDB; SQRADD(a[10], a[10]);
   COMBA_STORE(b[20]);

   /* output 21 */
   CARRY_FORWARD;
   SQRADDSC(a[6], a[15]); SQRADDAC(a[7], a[14]); SQRADDAC(a[8], a[13]); SQRADDAC(a[9], a[12]); SQRADDAC(a[10], a[11]); SQRADDDB;
   COMBA_STORE(b[21]);

   /* output 22 */
   CARRY_FORWARD;
   SQRADDSC(a[7], a[15]); SQRADDAC(a[8], a[14]); SQRADDAC(a[9], a[13]); SQRADDAC(a[10], a[12]); SQRADDDB; SQRADD(a[11], a[11]);
   COMBA_STORE(b[22]);

   /* output 23 */
   CARRY_FORWARD;
   SQRADDSC(a[8], a[15]); SQRADDAC(a[9], a[14]); SQRADDAC(a[10], a[13]); SQRADDAC(a[11], a[12]); SQRADDDB;
   COMBA_STORE(b[23]);

   /* output 24 */
   CARRY_FORWARD;
   SQRADDSC(a[9], a[15]); SQRADDAC(a[10], a[14]); SQRADDAC(a[11], a[13]); SQRADDDB; SQRADD(a[12], a[12]);
   COMBA_STORE(b[24]);

   /* output 25 */
   CARRY_FORWARD;
   SQRADDSC(a[10], a[15]); SQRADDAC(a[11], a[14]); SQRADDAC(a[12], a[13]); SQRADDDB;
   COMBA_STORE(b[25]);

   /* output 26 */
   CARRY_FORWARD;
   SQRADD2(a[11], a[15]); SQRADD2(a[12], a[14]); SQRADD(a[13], a[13]);
   COMBA_STORE(b[26]);

   /* output 27 */
   CARRY_FORWARD;
   SQRADD2(a[12], a[15]); SQRADD2(a[13], a[14]);
   COMBA_STORE(b[27]);

   /* output 28 */
   CARRY_FORWARD;
   SQRADD2(a[13], a[15]); SQRADD(a[14], a[14]);
   COMBA_STORE(b[28]);

   /* output 29 */
   CARRY_FORWARD;
   SQRADD2(a[14], a[15]);
   COMBA_STORE(b[29]);

   /* output 30 */
   CARRY_FORWARD;
   SQRADD(a[15], a[15]);
   COMBA_STORE(b[30]);
   COMBA_STORE2(b[31]);
   COMBA_FINI;

   B->used = 32;
   B->sign = PSTM_ZPOS;
   memcpy(B->dp, b, 32 * sizeof(pstm_digit));
   pstm_clamp(B);
   return PSTM_OKAY;
}
#endif /* USE_1024_KEY_SPEED_OPTIMIZATIONS */


#ifdef USE_2048_KEY_SPEED_OPTIMIZATIONS
static int32 pstm_sqr_comba32(pstm_int *A, pstm_int *B)
{
   pstm_digit *a, b[64], c0, c1, c2, sc0, sc1, sc2;
#ifdef PSTM_ISO
   pstm_word tt;
#endif

	if (B->alloc < 64) {
		if (pstm_grow(B, 64) != PSTM_OKAY) {
			return PS_MEM_FAIL;
		}
	}
	sc0 = sc1 = sc2 = 0;
   a = A->dp;
   COMBA_START;

   /* clear carries */
   CLEAR_CARRY;

   /* output 0 */
   SQRADD(a[0],a[0]);
   COMBA_STORE(b[0]);

   /* output 1 */
   CARRY_FORWARD;
   SQRADD2(a[0], a[1]);
   COMBA_STORE(b[1]);

   /* output 2 */
   CARRY_FORWARD;
   SQRADD2(a[0], a[2]); SQRADD(a[1], a[1]);
   COMBA_STORE(b[2]);

   /* output 3 */
   CARRY_FORWARD;
   SQRADD2(a[0], a[3]); SQRADD2(a[1], a[2]);
   COMBA_STORE(b[3]);

   /* output 4 */
   CARRY_FORWARD;
   SQRADD2(a[0], a[4]); SQRADD2(a[1], a[3]); SQRADD(a[2], a[2]);
   COMBA_STORE(b[4]);

   /* output 5 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[5]); SQRADDAC(a[1], a[4]); SQRADDAC(a[2], a[3]); SQRADDDB;
   COMBA_STORE(b[5]);

   /* output 6 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[6]); SQRADDAC(a[1], a[5]); SQRADDAC(a[2], a[4]); SQRADDDB; SQRADD(a[3], a[3]);
   COMBA_STORE(b[6]);

   /* output 7 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[7]); SQRADDAC(a[1], a[6]); SQRADDAC(a[2], a[5]); SQRADDAC(a[3], a[4]); SQRADDDB;
   COMBA_STORE(b[7]);

   /* output 8 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[8]); SQRADDAC(a[1], a[7]); SQRADDAC(a[2], a[6]); SQRADDAC(a[3], a[5]); SQRADDDB; SQRADD(a[4], a[4]);
   COMBA_STORE(b[8]);

   /* output 9 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[9]); SQRADDAC(a[1], a[8]); SQRADDAC(a[2], a[7]); SQRADDAC(a[3], a[6]); SQRADDAC(a[4], a[5]); SQRADDDB;
   COMBA_STORE(b[9]);

   /* output 10 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[10]); SQRADDAC(a[1], a[9]); SQRADDAC(a[2], a[8]); SQRADDAC(a[3], a[7]); SQRADDAC(a[4], a[6]); SQRADDDB; SQRADD(a[5], a[5]);
   COMBA_STORE(b[10]);

   /* output 11 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[11]); SQRADDAC(a[1], a[10]); SQRADDAC(a[2], a[9]); SQRADDAC(a[3], a[8]); SQRADDAC(a[4], a[7]); SQRADDAC(a[5], a[6]); SQRADDDB;
   COMBA_STORE(b[11]);

   /* output 12 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[12]); SQRADDAC(a[1], a[11]); SQRADDAC(a[2], a[10]); SQRADDAC(a[3], a[9]); SQRADDAC(a[4], a[8]); SQRADDAC(a[5], a[7]); SQRADDDB; SQRADD(a[6], a[6]);
   COMBA_STORE(b[12]);

   /* output 13 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[13]); SQRADDAC(a[1], a[12]); SQRADDAC(a[2], a[11]); SQRADDAC(a[3], a[10]); SQRADDAC(a[4], a[9]); SQRADDAC(a[5], a[8]); SQRADDAC(a[6], a[7]); SQRADDDB;
   COMBA_STORE(b[13]);

   /* output 14 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[14]); SQRADDAC(a[1], a[13]); SQRADDAC(a[2], a[12]); SQRADDAC(a[3], a[11]); SQRADDAC(a[4], a[10]); SQRADDAC(a[5], a[9]); SQRADDAC(a[6], a[8]); SQRADDDB; SQRADD(a[7], a[7]);
   COMBA_STORE(b[14]);

   /* output 15 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[15]); SQRADDAC(a[1], a[14]); SQRADDAC(a[2], a[13]); SQRADDAC(a[3], a[12]); SQRADDAC(a[4], a[11]); SQRADDAC(a[5], a[10]); SQRADDAC(a[6], a[9]); SQRADDAC(a[7], a[8]); SQRADDDB;
   COMBA_STORE(b[15]);

   /* output 16 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[16]); SQRADDAC(a[1], a[15]); SQRADDAC(a[2], a[14]); SQRADDAC(a[3], a[13]); SQRADDAC(a[4], a[12]); SQRADDAC(a[5], a[11]); SQRADDAC(a[6], a[10]); SQRADDAC(a[7], a[9]); SQRADDDB; SQRADD(a[8], a[8]);
   COMBA_STORE(b[16]);

   /* output 17 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[17]); SQRADDAC(a[1], a[16]); SQRADDAC(a[2], a[15]); SQRADDAC(a[3], a[14]); SQRADDAC(a[4], a[13]); SQRADDAC(a[5], a[12]); SQRADDAC(a[6], a[11]); SQRADDAC(a[7], a[10]); SQRADDAC(a[8], a[9]); SQRADDDB;
   COMBA_STORE(b[17]);

   /* output 18 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[18]); SQRADDAC(a[1], a[17]); SQRADDAC(a[2], a[16]); SQRADDAC(a[3], a[15]); SQRADDAC(a[4], a[14]); SQRADDAC(a[5], a[13]); SQRADDAC(a[6], a[12]); SQRADDAC(a[7], a[11]); SQRADDAC(a[8], a[10]); SQRADDDB; SQRADD(a[9], a[9]);
   COMBA_STORE(b[18]);

   /* output 19 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[19]); SQRADDAC(a[1], a[18]); SQRADDAC(a[2], a[17]); SQRADDAC(a[3], a[16]); SQRADDAC(a[4], a[15]); SQRADDAC(a[5], a[14]); SQRADDAC(a[6], a[13]); SQRADDAC(a[7], a[12]); SQRADDAC(a[8], a[11]); SQRADDAC(a[9], a[10]); SQRADDDB;
   COMBA_STORE(b[19]);

   /* output 20 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[20]); SQRADDAC(a[1], a[19]); SQRADDAC(a[2], a[18]); SQRADDAC(a[3], a[17]); SQRADDAC(a[4], a[16]); SQRADDAC(a[5], a[15]); SQRADDAC(a[6], a[14]); SQRADDAC(a[7], a[13]); SQRADDAC(a[8], a[12]); SQRADDAC(a[9], a[11]); SQRADDDB; SQRADD(a[10], a[10]);
   COMBA_STORE(b[20]);

   /* output 21 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[21]); SQRADDAC(a[1], a[20]); SQRADDAC(a[2], a[19]); SQRADDAC(a[3], a[18]); SQRADDAC(a[4], a[17]); SQRADDAC(a[5], a[16]); SQRADDAC(a[6], a[15]); SQRADDAC(a[7], a[14]); SQRADDAC(a[8], a[13]); SQRADDAC(a[9], a[12]); SQRADDAC(a[10], a[11]); SQRADDDB;
   COMBA_STORE(b[21]);

   /* output 22 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[22]); SQRADDAC(a[1], a[21]); SQRADDAC(a[2], a[20]); SQRADDAC(a[3], a[19]); SQRADDAC(a[4], a[18]); SQRADDAC(a[5], a[17]); SQRADDAC(a[6], a[16]); SQRADDAC(a[7], a[15]); SQRADDAC(a[8], a[14]); SQRADDAC(a[9], a[13]); SQRADDAC(a[10], a[12]); SQRADDDB; SQRADD(a[11], a[11]);
   COMBA_STORE(b[22]);

   /* output 23 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[23]); SQRADDAC(a[1], a[22]); SQRADDAC(a[2], a[21]); SQRADDAC(a[3], a[20]); SQRADDAC(a[4], a[19]); SQRADDAC(a[5], a[18]); SQRADDAC(a[6], a[17]); SQRADDAC(a[7], a[16]); SQRADDAC(a[8], a[15]); SQRADDAC(a[9], a[14]); SQRADDAC(a[10], a[13]); SQRADDAC(a[11], a[12]); SQRADDDB;
   COMBA_STORE(b[23]);

   /* output 24 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[24]); SQRADDAC(a[1], a[23]); SQRADDAC(a[2], a[22]); SQRADDAC(a[3], a[21]); SQRADDAC(a[4], a[20]); SQRADDAC(a[5], a[19]); SQRADDAC(a[6], a[18]); SQRADDAC(a[7], a[17]); SQRADDAC(a[8], a[16]); SQRADDAC(a[9], a[15]); SQRADDAC(a[10], a[14]); SQRADDAC(a[11], a[13]); SQRADDDB; SQRADD(a[12], a[12]);
   COMBA_STORE(b[24]);

   /* output 25 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[25]); SQRADDAC(a[1], a[24]); SQRADDAC(a[2], a[23]); SQRADDAC(a[3], a[22]); SQRADDAC(a[4], a[21]); SQRADDAC(a[5], a[20]); SQRADDAC(a[6], a[19]); SQRADDAC(a[7], a[18]); SQRADDAC(a[8], a[17]); SQRADDAC(a[9], a[16]); SQRADDAC(a[10], a[15]); SQRADDAC(a[11], a[14]); SQRADDAC(a[12], a[13]); SQRADDDB;
   COMBA_STORE(b[25]);

   /* output 26 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[26]); SQRADDAC(a[1], a[25]); SQRADDAC(a[2], a[24]); SQRADDAC(a[3], a[23]); SQRADDAC(a[4], a[22]); SQRADDAC(a[5], a[21]); SQRADDAC(a[6], a[20]); SQRADDAC(a[7], a[19]); SQRADDAC(a[8], a[18]); SQRADDAC(a[9], a[17]); SQRADDAC(a[10], a[16]); SQRADDAC(a[11], a[15]); SQRADDAC(a[12], a[14]); SQRADDDB; SQRADD(a[13], a[13]);
   COMBA_STORE(b[26]);

   /* output 27 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[27]); SQRADDAC(a[1], a[26]); SQRADDAC(a[2], a[25]); SQRADDAC(a[3], a[24]); SQRADDAC(a[4], a[23]); SQRADDAC(a[5], a[22]); SQRADDAC(a[6], a[21]); SQRADDAC(a[7], a[20]); SQRADDAC(a[8], a[19]); SQRADDAC(a[9], a[18]); SQRADDAC(a[10], a[17]); SQRADDAC(a[11], a[16]); SQRADDAC(a[12], a[15]); SQRADDAC(a[13], a[14]); SQRADDDB;
   COMBA_STORE(b[27]);

   /* output 28 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[28]); SQRADDAC(a[1], a[27]); SQRADDAC(a[2], a[26]); SQRADDAC(a[3], a[25]); SQRADDAC(a[4], a[24]); SQRADDAC(a[5], a[23]); SQRADDAC(a[6], a[22]); SQRADDAC(a[7], a[21]); SQRADDAC(a[8], a[20]); SQRADDAC(a[9], a[19]); SQRADDAC(a[10], a[18]); SQRADDAC(a[11], a[17]); SQRADDAC(a[12], a[16]); SQRADDAC(a[13], a[15]); SQRADDDB; SQRADD(a[14], a[14]);
   COMBA_STORE(b[28]);

   /* output 29 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[29]); SQRADDAC(a[1], a[28]); SQRADDAC(a[2], a[27]); SQRADDAC(a[3], a[26]); SQRADDAC(a[4], a[25]); SQRADDAC(a[5], a[24]); SQRADDAC(a[6], a[23]); SQRADDAC(a[7], a[22]); SQRADDAC(a[8], a[21]); SQRADDAC(a[9], a[20]); SQRADDAC(a[10], a[19]); SQRADDAC(a[11], a[18]); SQRADDAC(a[12], a[17]); SQRADDAC(a[13], a[16]); SQRADDAC(a[14], a[15]); SQRADDDB;
   COMBA_STORE(b[29]);

   /* output 30 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[30]); SQRADDAC(a[1], a[29]); SQRADDAC(a[2], a[28]); SQRADDAC(a[3], a[27]); SQRADDAC(a[4], a[26]); SQRADDAC(a[5], a[25]); SQRADDAC(a[6], a[24]); SQRADDAC(a[7], a[23]); SQRADDAC(a[8], a[22]); SQRADDAC(a[9], a[21]); SQRADDAC(a[10], a[20]); SQRADDAC(a[11], a[19]); SQRADDAC(a[12], a[18]); SQRADDAC(a[13], a[17]); SQRADDAC(a[14], a[16]); SQRADDDB; SQRADD(a[15], a[15]);
   COMBA_STORE(b[30]);

   /* output 31 */
   CARRY_FORWARD;
   SQRADDSC(a[0], a[31]); SQRADDAC(a[1], a[30]); SQRADDAC(a[2], a[29]); SQRADDAC(a[3], a[28]); SQRADDAC(a[4], a[27]); SQRADDAC(a[5], a[26]); SQRADDAC(a[6], a[25]); SQRADDAC(a[7], a[24]); SQRADDAC(a[8], a[23]); SQRADDAC(a[9], a[22]); SQRADDAC(a[10], a[21]); SQRADDAC(a[11], a[20]); SQRADDAC(a[12], a[19]); SQRADDAC(a[13], a[18]); SQRADDAC(a[14], a[17]); SQRADDAC(a[15], a[16]); SQRADDDB;
   COMBA_STORE(b[31]);

   /* output 32 */
   CARRY_FORWARD;
   SQRADDSC(a[1], a[31]); SQRADDAC(a[2], a[30]); SQRADDAC(a[3], a[29]); SQRADDAC(a[4], a[28]); SQRADDAC(a[5], a[27]); SQRADDAC(a[6], a[26]); SQRADDAC(a[7], a[25]); SQRADDAC(a[8], a[24]); SQRADDAC(a[9], a[23]); SQRADDAC(a[10], a[22]); SQRADDAC(a[11], a[21]); SQRADDAC(a[12], a[20]); SQRADDAC(a[13], a[19]); SQRADDAC(a[14], a[18]); SQRADDAC(a[15], a[17]); SQRADDDB; SQRADD(a[16], a[16]);
   COMBA_STORE(b[32]);

   /* output 33 */
   CARRY_FORWARD;
   SQRADDSC(a[2], a[31]); SQRADDAC(a[3], a[30]); SQRADDAC(a[4], a[29]); SQRADDAC(a[5], a[28]); SQRADDAC(a[6], a[27]); SQRADDAC(a[7], a[26]); SQRADDAC(a[8], a[25]); SQRADDAC(a[9], a[24]); SQRADDAC(a[10], a[23]); SQRADDAC(a[11], a[22]); SQRADDAC(a[12], a[21]); SQRADDAC(a[13], a[20]); SQRADDAC(a[14], a[19]); SQRADDAC(a[15], a[18]); SQRADDAC(a[16], a[17]); SQRADDDB;
   COMBA_STORE(b[33]);

   /* output 34 */
   CARRY_FORWARD;
   SQRADDSC(a[3], a[31]); SQRADDAC(a[4], a[30]); SQRADDAC(a[5], a[29]); SQRADDAC(a[6], a[28]); SQRADDAC(a[7], a[27]); SQRADDAC(a[8], a[26]); SQRADDAC(a[9], a[25]); SQRADDAC(a[10], a[24]); SQRADDAC(a[11], a[23]); SQRADDAC(a[12], a[22]); SQRADDAC(a[13], a[21]); SQRADDAC(a[14], a[20]); SQRADDAC(a[15], a[19]); SQRADDAC(a[16], a[18]); SQRADDDB; SQRADD(a[17], a[17]);
   COMBA_STORE(b[34]);

   /* output 35 */
   CARRY_FORWARD;
   SQRADDSC(a[4], a[31]); SQRADDAC(a[5], a[30]); SQRADDAC(a[6], a[29]); SQRADDAC(a[7], a[28]); SQRADDAC(a[8], a[27]); SQRADDAC(a[9], a[26]); SQRADDAC(a[10], a[25]); SQRADDAC(a[11], a[24]); SQRADDAC(a[12], a[23]); SQRADDAC(a[13], a[22]); SQRADDAC(a[14], a[21]); SQRADDAC(a[15], a[20]); SQRADDAC(a[16], a[19]); SQRADDAC(a[17], a[18]); SQRADDDB;
   COMBA_STORE(b[35]);

   /* output 36 */
   CARRY_FORWARD;
   SQRADDSC(a[5], a[31]); SQRADDAC(a[6], a[30]); SQRADDAC(a[7], a[29]); SQRADDAC(a[8], a[28]); SQRADDAC(a[9], a[27]); SQRADDAC(a[10], a[26]); SQRADDAC(a[11], a[25]); SQRADDAC(a[12], a[24]); SQRADDAC(a[13], a[23]); SQRADDAC(a[14], a[22]); SQRADDAC(a[15], a[21]); SQRADDAC(a[16], a[20]); SQRADDAC(a[17], a[19]); SQRADDDB; SQRADD(a[18], a[18]);
   COMBA_STORE(b[36]);

   /* output 37 */
   CARRY_FORWARD;
   SQRADDSC(a[6], a[31]); SQRADDAC(a[7], a[30]); SQRADDAC(a[8], a[29]); SQRADDAC(a[9], a[28]); SQRADDAC(a[10], a[27]); SQRADDAC(a[11], a[26]); SQRADDAC(a[12], a[25]); SQRADDAC(a[13], a[24]); SQRADDAC(a[14], a[23]); SQRADDAC(a[15], a[22]); SQRADDAC(a[16], a[21]); SQRADDAC(a[17], a[20]); SQRADDAC(a[18], a[19]); SQRADDDB;
   COMBA_STORE(b[37]);

   /* output 38 */
   CARRY_FORWARD;
   SQRADDSC(a[7], a[31]); SQRADDAC(a[8], a[30]); SQRADDAC(a[9], a[29]); SQRADDAC(a[10], a[28]); SQRADDAC(a[11], a[27]); SQRADDAC(a[12], a[26]); SQRADDAC(a[13], a[25]); SQRADDAC(a[14], a[24]); SQRADDAC(a[15], a[23]); SQRADDAC(a[16], a[22]); SQRADDAC(a[17], a[21]); SQRADDAC(a[18], a[20]); SQRADDDB; SQRADD(a[19], a[19]);
   COMBA_STORE(b[38]);

   /* output 39 */
   CARRY_FORWARD;
   SQRADDSC(a[8], a[31]); SQRADDAC(a[9], a[30]); SQRADDAC(a[10], a[29]); SQRADDAC(a[11], a[28]); SQRADDAC(a[12], a[27]); SQRADDAC(a[13], a[26]); SQRADDAC(a[14], a[25]); SQRADDAC(a[15], a[24]); SQRADDAC(a[16], a[23]); SQRADDAC(a[17], a[22]); SQRADDAC(a[18], a[21]); SQRADDAC(a[19], a[20]); SQRADDDB;
   COMBA_STORE(b[39]);

   /* output 40 */
   CARRY_FORWARD;
   SQRADDSC(a[9], a[31]); SQRADDAC(a[10], a[30]); SQRADDAC(a[11], a[29]); SQRADDAC(a[12], a[28]); SQRADDAC(a[13], a[27]); SQRADDAC(a[14], a[26]); SQRADDAC(a[15], a[25]); SQRADDAC(a[16], a[24]); SQRADDAC(a[17], a[23]); SQRADDAC(a[18], a[22]); SQRADDAC(a[19], a[21]); SQRADDDB; SQRADD(a[20], a[20]);
   COMBA_STORE(b[40]);

   /* output 41 */
   CARRY_FORWARD;
   SQRADDSC(a[10], a[31]); SQRADDAC(a[11], a[30]); SQRADDAC(a[12], a[29]); SQRADDAC(a[13], a[28]); SQRADDAC(a[14], a[27]); SQRADDAC(a[15], a[26]); SQRADDAC(a[16], a[25]); SQRADDAC(a[17], a[24]); SQRADDAC(a[18], a[23]); SQRADDAC(a[19], a[22]); SQRADDAC(a[20], a[21]); SQRADDDB;
   COMBA_STORE(b[41]);

   /* output 42 */
   CARRY_FORWARD;
   SQRADDSC(a[11], a[31]); SQRADDAC(a[12], a[30]); SQRADDAC(a[13], a[29]); SQRADDAC(a[14], a[28]); SQRADDAC(a[15], a[27]); SQRADDAC(a[16], a[26]); SQRADDAC(a[17], a[25]); SQRADDAC(a[18], a[24]); SQRADDAC(a[19], a[23]); SQRADDAC(a[20], a[22]); SQRADDDB; SQRADD(a[21], a[21]);
   COMBA_STORE(b[42]);

   /* output 43 */
   CARRY_FORWARD;
   SQRADDSC(a[12], a[31]); SQRADDAC(a[13], a[30]); SQRADDAC(a[14], a[29]); SQRADDAC(a[15], a[28]); SQRADDAC(a[16], a[27]); SQRADDAC(a[17], a[26]); SQRADDAC(a[18], a[25]); SQRADDAC(a[19], a[24]); SQRADDAC(a[20], a[23]); SQRADDAC(a[21], a[22]); SQRADDDB;
   COMBA_STORE(b[43]);

   /* output 44 */
   CARRY_FORWARD;
   SQRADDSC(a[13], a[31]); SQRADDAC(a[14], a[30]); SQRADDAC(a[15], a[29]); SQRADDAC(a[16], a[28]); SQRADDAC(a[17], a[27]); SQRADDAC(a[18], a[26]); SQRADDAC(a[19], a[25]); SQRADDAC(a[20], a[24]); SQRADDAC(a[21], a[23]); SQRADDDB; SQRADD(a[22], a[22]);
   COMBA_STORE(b[44]);

   /* output 45 */
   CARRY_FORWARD;
   SQRADDSC(a[14], a[31]); SQRADDAC(a[15], a[30]); SQRADDAC(a[16], a[29]); SQRADDAC(a[17], a[28]); SQRADDAC(a[18], a[27]); SQRADDAC(a[19], a[26]); SQRADDAC(a[20], a[25]); SQRADDAC(a[21], a[24]); SQRADDAC(a[22], a[23]); SQRADDDB;
   COMBA_STORE(b[45]);

   /* output 46 */
   CARRY_FORWARD;
   SQRADDSC(a[15], a[31]); SQRADDAC(a[16], a[30]); SQRADDAC(a[17], a[29]); SQRADDAC(a[18], a[28]); SQRADDAC(a[19], a[27]); SQRADDAC(a[20], a[26]); SQRADDAC(a[21], a[25]); SQRADDAC(a[22], a[24]); SQRADDDB; SQRADD(a[23], a[23]);
   COMBA_STORE(b[46]);

   /* output 47 */
   CARRY_FORWARD;
   SQRADDSC(a[16], a[31]); SQRADDAC(a[17], a[30]); SQRADDAC(a[18], a[29]); SQRADDAC(a[19], a[28]); SQRADDAC(a[20], a[27]); SQRADDAC(a[21], a[26]); SQRADDAC(a[22], a[25]); SQRADDAC(a[23], a[24]); SQRADDDB;
   COMBA_STORE(b[47]);

   /* output 48 */
   CARRY_FORWARD;
   SQRADDSC(a[17], a[31]); SQRADDAC(a[18], a[30]); SQRADDAC(a[19], a[29]); SQRADDAC(a[20], a[28]); SQRADDAC(a[21], a[27]); SQRADDAC(a[22], a[26]); SQRADDAC(a[23], a[25]); SQRADDDB; SQRADD(a[24], a[24]);
   COMBA_STORE(b[48]);

   /* output 49 */
   CARRY_FORWARD;
   SQRADDSC(a[18], a[31]); SQRADDAC(a[19], a[30]); SQRADDAC(a[20], a[29]); SQRADDAC(a[21], a[28]); SQRADDAC(a[22], a[27]); SQRADDAC(a[23], a[26]); SQRADDAC(a[24], a[25]); SQRADDDB;
   COMBA_STORE(b[49]);

   /* output 50 */
   CARRY_FORWARD;
   SQRADDSC(a[19], a[31]); SQRADDAC(a[20], a[30]); SQRADDAC(a[21], a[29]); SQRADDAC(a[22], a[28]); SQRADDAC(a[23], a[27]); SQRADDAC(a[24], a[26]); SQRADDDB; SQRADD(a[25], a[25]);
   COMBA_STORE(b[50]);

   /* output 51 */
   CARRY_FORWARD;
   SQRADDSC(a[20], a[31]); SQRADDAC(a[21], a[30]); SQRADDAC(a[22], a[29]); SQRADDAC(a[23], a[28]); SQRADDAC(a[24], a[27]); SQRADDAC(a[25], a[26]); SQRADDDB;
   COMBA_STORE(b[51]);

   /* output 52 */
   CARRY_FORWARD;
   SQRADDSC(a[21], a[31]); SQRADDAC(a[22], a[30]); SQRADDAC(a[23], a[29]); SQRADDAC(a[24], a[28]); SQRADDAC(a[25], a[27]); SQRADDDB; SQRADD(a[26], a[26]);
   COMBA_STORE(b[52]);

   /* output 53 */
   CARRY_FORWARD;
   SQRADDSC(a[22], a[31]); SQRADDAC(a[23], a[30]); SQRADDAC(a[24], a[29]); SQRADDAC(a[25], a[28]); SQRADDAC(a[26], a[27]); SQRADDDB;
   COMBA_STORE(b[53]);

   /* output 54 */
   CARRY_FORWARD;
   SQRADDSC(a[23], a[31]); SQRADDAC(a[24], a[30]); SQRADDAC(a[25], a[29]); SQRADDAC(a[26], a[28]); SQRADDDB; SQRADD(a[27], a[27]);
   COMBA_STORE(b[54]);

   /* output 55 */
   CARRY_FORWARD;
   SQRADDSC(a[24], a[31]); SQRADDAC(a[25], a[30]); SQRADDAC(a[26], a[29]); SQRADDAC(a[27], a[28]); SQRADDDB;
   COMBA_STORE(b[55]);

   /* output 56 */
   CARRY_FORWARD;
   SQRADDSC(a[25], a[31]); SQRADDAC(a[26], a[30]); SQRADDAC(a[27], a[29]); SQRADDDB; SQRADD(a[28], a[28]);
   COMBA_STORE(b[56]);

   /* output 57 */
   CARRY_FORWARD;
   SQRADDSC(a[26], a[31]); SQRADDAC(a[27], a[30]); SQRADDAC(a[28], a[29]); SQRADDDB;
   COMBA_STORE(b[57]);

   /* output 58 */
   CARRY_FORWARD;
   SQRADD2(a[27], a[31]); SQRADD2(a[28], a[30]); SQRADD(a[29], a[29]);
   COMBA_STORE(b[58]);

   /* output 59 */
   CARRY_FORWARD;
   SQRADD2(a[28], a[31]); SQRADD2(a[29], a[30]);
   COMBA_STORE(b[59]);

   /* output 60 */
   CARRY_FORWARD;
   SQRADD2(a[29], a[31]); SQRADD(a[30], a[30]);
   COMBA_STORE(b[60]);

   /* output 61 */
   CARRY_FORWARD;
   SQRADD2(a[30], a[31]);
   COMBA_STORE(b[61]);

   /* output 62 */
   CARRY_FORWARD;
   SQRADD(a[31], a[31]);
   COMBA_STORE(b[62]);
   COMBA_STORE2(b[63]);
   COMBA_FINI;

   B->used = 64;
   B->sign = PSTM_ZPOS;
   memcpy(B->dp, b, 64 * sizeof(pstm_digit));
   pstm_clamp(B);
   return PSTM_OKAY;
}
#endif /* USE_2048_KEY_SPEED_OPTIMIZATIONS */

/******************************************************************************/
/*
 */
int32 pstm_sqr_comba(psPool_t *pool, pstm_int *A, pstm_int *B, pstm_digit *paD,
		uint32 paDlen)
{
#ifdef USE_1024_KEY_SPEED_OPTIMIZATIONS
	if (A->used == 16) {
		return pstm_sqr_comba16(A, B);
	} else {
#ifdef USE_2048_KEY_SPEED_OPTIMIZATIONS
		if (A->used == 32) {
			return pstm_sqr_comba32(A, B);
		}
#endif /* USE_2048_KEY_SPEED_OPTIMIZATIONS */
		return pstm_sqr_comba_gen(pool, A, B, paD, paDlen);
	}
#else
#ifdef USE_2048_KEY_SPEED_OPTIMIZATIONS
	if (A->used == 32) {
		return pstm_sqr_comba32(A, B);
	}
#endif /* USE_2048_KEY_SPEED_OPTIMIZATIONS */
	return pstm_sqr_comba_gen(pool, A, B, paD, paDlen);
#endif
}

#endif /* DISABLE_PSTM */
/******************************************************************************/
