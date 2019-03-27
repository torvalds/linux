/****************************************************************

The author of this software is David M. Gay.

Copyright (C) 1998-2000 by Lucent Technologies
All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name of Lucent or any of its entities
not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.

****************************************************************/

/* $FreeBSD$ */

/* This is a variation on dtoa.c that converts arbitary binary
   floating-point formats to and from decimal notation.  It uses
   double-precision arithmetic internally, so there are still
   various #ifdefs that adapt the calculations to the native
   double-precision arithmetic (any of IEEE, VAX D_floating,
   or IBM mainframe arithmetic).

   Please send bug reports to David M. Gay (dmg at acm dot org,
   with " at " changed at "@" and " dot " changed to ".").
 */

/* On a machine with IEEE extended-precision registers, it is
 * necessary to specify double-precision (53-bit) rounding precision
 * before invoking strtod or dtoa.  If the machine uses (the equivalent
 * of) Intel 80x87 arithmetic, the call
 *	_control87(PC_53, MCW_PC);
 * does this with many compilers.  Whether this or another call is
 * appropriate depends on the compiler; for this to work, it may be
 * necessary to #include "float.h" or another system-dependent header
 * file.
 */

/* strtod for IEEE-, VAX-, and IBM-arithmetic machines.
 *
 * This strtod returns a nearest machine number to the input decimal
 * string (or sets errno to ERANGE).  With IEEE arithmetic, ties are
 * broken by the IEEE round-even rule.  Otherwise ties are broken by
 * biased rounding (add half and chop).
 *
 * Inspired loosely by William D. Clinger's paper "How to Read Floating
 * Point Numbers Accurately" [Proc. ACM SIGPLAN '90, pp. 112-126].
 *
 * Modifications:
 *
 *	1. We only require IEEE, IBM, or VAX double-precision
 *		arithmetic (not IEEE double-extended).
 *	2. We get by with floating-point arithmetic in a case that
 *		Clinger missed -- when we're computing d * 10^n
 *		for a small integer d and the integer n is not too
 *		much larger than 22 (the maximum integer k for which
 *		we can represent 10^k exactly), we may be able to
 *		compute (d*10^k) * 10^(e-k) with just one roundoff.
 *	3. Rather than a bit-at-a-time adjustment of the binary
 *		result in the hard case, we use floating-point
 *		arithmetic to determine the adjustment to within
 *		one bit; only in really hard cases do we need to
 *		compute a second residual.
 *	4. Because of 3., we don't need a large table of powers of 10
 *		for ten-to-e (just some small tables, e.g. of 10^k
 *		for 0 <= k <= 22).
 */

/*
 * #define IEEE_8087 for IEEE-arithmetic machines where the least
 *	significant byte has the lowest address.
 * #define IEEE_MC68k for IEEE-arithmetic machines where the most
 *	significant byte has the lowest address.
 * #define Long int on machines with 32-bit ints and 64-bit longs.
 * #define Sudden_Underflow for IEEE-format machines without gradual
 *	underflow (i.e., that flush to zero on underflow).
 * #define IBM for IBM mainframe-style floating-point arithmetic.
 * #define VAX for VAX-style floating-point arithmetic (D_floating).
 * #define No_leftright to omit left-right logic in fast floating-point
 *	computation of dtoa.
 * #define Check_FLT_ROUNDS if FLT_ROUNDS can assume the values 2 or 3.
 * #define RND_PRODQUOT to use rnd_prod and rnd_quot (assembly routines
 *	that use extended-precision instructions to compute rounded
 *	products and quotients) with IBM.
 * #define ROUND_BIASED for IEEE-format with biased rounding and arithmetic
 *	that rounds toward +Infinity.
 * #define ROUND_BIASED_without_Round_Up for IEEE-format with biased
 *	rounding when the underlying floating-point arithmetic uses
 *	unbiased rounding.  This prevent using ordinary floating-point
 *	arithmetic when the result could be computed with one rounding error.
 * #define Inaccurate_Divide for IEEE-format with correctly rounded
 *	products but inaccurate quotients, e.g., for Intel i860.
 * #define NO_LONG_LONG on machines that do not have a "long long"
 *	integer type (of >= 64 bits).  On such machines, you can
 *	#define Just_16 to store 16 bits per 32-bit Long when doing
 *	high-precision integer arithmetic.  Whether this speeds things
 *	up or slows things down depends on the machine and the number
 *	being converted.  If long long is available and the name is
 *	something other than "long long", #define Llong to be the name,
 *	and if "unsigned Llong" does not work as an unsigned version of
 *	Llong, #define #ULLong to be the corresponding unsigned type.
 * #define KR_headers for old-style C function headers.
 * #define Bad_float_h if your system lacks a float.h or if it does not
 *	define some or all of DBL_DIG, DBL_MAX_10_EXP, DBL_MAX_EXP,
 *	FLT_RADIX, FLT_ROUNDS, and DBL_MAX.
 * #define MALLOC your_malloc, where your_malloc(n) acts like malloc(n)
 *	if memory is available and otherwise does something you deem
 *	appropriate.  If MALLOC is undefined, malloc will be invoked
 *	directly -- and assumed always to succeed.  Similarly, if you
 *	want something other than the system's free() to be called to
 *	recycle memory acquired from MALLOC, #define FREE to be the
 *	name of the alternate routine.  (FREE or free is only called in
 *	pathological cases, e.g., in a gdtoa call after a gdtoa return in
 *	mode 3 with thousands of digits requested.)
 * #define Omit_Private_Memory to omit logic (added Jan. 1998) for making
 *	memory allocations from a private pool of memory when possible.
 *	When used, the private pool is PRIVATE_MEM bytes long:  2304 bytes,
 *	unless #defined to be a different length.  This default length
 *	suffices to get rid of MALLOC calls except for unusual cases,
 *	such as decimal-to-binary conversion of a very long string of
 *	digits.  When converting IEEE double precision values, the
 *	longest string gdtoa can return is about 751 bytes long.  For
 *	conversions by strtod of strings of 800 digits and all gdtoa
 *	conversions of IEEE doubles in single-threaded executions with
 *	8-byte pointers, PRIVATE_MEM >= 7400 appears to suffice; with
 *	4-byte pointers, PRIVATE_MEM >= 7112 appears adequate.
 * #define NO_INFNAN_CHECK if you do not wish to have INFNAN_CHECK
 *	#defined automatically on IEEE systems.  On such systems,
 *	when INFNAN_CHECK is #defined, strtod checks
 *	for Infinity and NaN (case insensitively).
 *	When INFNAN_CHECK is #defined and No_Hex_NaN is not #defined,
 *	strtodg also accepts (case insensitively) strings of the form
 *	NaN(x), where x is a string of hexadecimal digits (optionally
 *	preceded by 0x or 0X) and spaces; if there is only one string
 *	of hexadecimal digits, it is taken for the fraction bits of the
 *	resulting NaN; if there are two or more strings of hexadecimal
 *	digits, each string is assigned to the next available sequence
 *	of 32-bit words of fractions bits (starting with the most
 *	significant), right-aligned in each sequence.
 *	Unless GDTOA_NON_PEDANTIC_NANCHECK is #defined, input "NaN(...)"
 *	is consumed even when ... has the wrong form (in which case the
 *	"(...)" is consumed but ignored).
 * #define MULTIPLE_THREADS if the system offers preemptively scheduled
 *	multiple threads.  In this case, you must provide (or suitably
 *	#define) two locks, acquired by ACQUIRE_DTOA_LOCK(n) and freed
 *	by FREE_DTOA_LOCK(n) for n = 0 or 1.  (The second lock, accessed
 *	in pow5mult, ensures lazy evaluation of only one copy of high
 *	powers of 5; omitting this lock would introduce a small
 *	probability of wasting memory, but would otherwise be harmless.)
 *	You must also invoke freedtoa(s) to free the value s returned by
 *	dtoa.  You may do so whether or not MULTIPLE_THREADS is #defined.
 * #define IMPRECISE_INEXACT if you do not care about the setting of
 *	the STRTOG_Inexact bits in the special case of doing IEEE double
 *	precision conversions (which could also be done by the strtod in
 *	dtoa.c).
 * #define NO_HEX_FP to disable recognition of C9x's hexadecimal
 *	floating-point constants.
 * #define -DNO_ERRNO to suppress setting errno (in strtod.c and
 *	strtodg.c).
 * #define NO_STRING_H to use private versions of memcpy.
 *	On some K&R systems, it may also be necessary to
 *	#define DECLARE_SIZE_T in this case.
 * #define USE_LOCALE to use the current locale's decimal_point value.
 */

#ifndef GDTOAIMP_H_INCLUDED
#define GDTOAIMP_H_INCLUDED

#define	Long	int

#include "gdtoa.h"
#include "gd_qnan.h"
#ifdef Honor_FLT_ROUNDS
#include <fenv.h>
#endif

#ifdef DEBUG
#include "stdio.h"
#define Bug(x) {fprintf(stderr, "%s\n", x); exit(1);}
#endif

#include "limits.h"
#include "stdlib.h"
#include "string.h"
#include "libc_private.h"

#include "namespace.h"
#include <pthread.h>
#include "un-namespace.h"
#include "xlocale_private.h"

#ifdef KR_headers
#define Char char
#else
#define Char void
#endif

#ifdef MALLOC
extern Char *MALLOC ANSI((size_t));
#else
#define MALLOC malloc
#endif

#define INFNAN_CHECK
#define USE_LOCALE
#define NO_LOCALE_CACHE
#define Honor_FLT_ROUNDS
#define Trust_FLT_ROUNDS

#undef IEEE_Arith
#undef Avoid_Underflow
#ifdef IEEE_MC68k
#define IEEE_Arith
#endif
#ifdef IEEE_8087
#define IEEE_Arith
#endif

#include "errno.h"
#ifdef Bad_float_h

#ifdef IEEE_Arith
#define DBL_DIG 15
#define DBL_MAX_10_EXP 308
#define DBL_MAX_EXP 1024
#define FLT_RADIX 2
#define DBL_MAX 1.7976931348623157e+308
#endif

#ifdef IBM
#define DBL_DIG 16
#define DBL_MAX_10_EXP 75
#define DBL_MAX_EXP 63
#define FLT_RADIX 16
#define DBL_MAX 7.2370055773322621e+75
#endif

#ifdef VAX
#define DBL_DIG 16
#define DBL_MAX_10_EXP 38
#define DBL_MAX_EXP 127
#define FLT_RADIX 2
#define DBL_MAX 1.7014118346046923e+38
#define n_bigtens 2
#endif

#ifndef LONG_MAX
#define LONG_MAX 2147483647
#endif

#else /* ifndef Bad_float_h */
#include "float.h"
#endif /* Bad_float_h */

#ifdef IEEE_Arith
#define Scale_Bit 0x10
#define n_bigtens 5
#endif

#ifdef IBM
#define n_bigtens 3
#endif

#ifdef VAX
#define n_bigtens 2
#endif

#ifndef __MATH_H__
#include "math.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(IEEE_8087) + defined(IEEE_MC68k) + defined(VAX) + defined(IBM) != 1
Exactly one of IEEE_8087, IEEE_MC68k, VAX, or IBM should be defined.
#endif

typedef union { double d; ULong L[2]; } U;

#ifdef IEEE_8087
#define word0(x) (x)->L[1]
#define word1(x) (x)->L[0]
#else
#define word0(x) (x)->L[0]
#define word1(x) (x)->L[1]
#endif
#define dval(x) (x)->d

/* The following definition of Storeinc is appropriate for MIPS processors.
 * An alternative that might be better on some machines is
 * #define Storeinc(a,b,c) (*a++ = b << 16 | c & 0xffff)
 */
#if defined(IEEE_8087) + defined(VAX)
#define Storeinc(a,b,c) (((unsigned short *)a)[1] = (unsigned short)b, \
((unsigned short *)a)[0] = (unsigned short)c, a++)
#else
#define Storeinc(a,b,c) (((unsigned short *)a)[0] = (unsigned short)b, \
((unsigned short *)a)[1] = (unsigned short)c, a++)
#endif

/* #define P DBL_MANT_DIG */
/* Ten_pmax = floor(P*log(2)/log(5)) */
/* Bletch = (highest power of 2 < DBL_MAX_10_EXP) / 16 */
/* Quick_max = floor((P-1)*log(FLT_RADIX)/log(10) - 1) */
/* Int_max = floor(P*log(FLT_RADIX)/log(10) - 1) */

#ifdef IEEE_Arith
#define Exp_shift  20
#define Exp_shift1 20
#define Exp_msk1    0x100000
#define Exp_msk11   0x100000
#define Exp_mask  0x7ff00000
#define P 53
#define Bias 1023
#define Emin (-1022)
#define Exp_1  0x3ff00000
#define Exp_11 0x3ff00000
#define Ebits 11
#define Frac_mask  0xfffff
#define Frac_mask1 0xfffff
#define Ten_pmax 22
#define Bletch 0x10
#define Bndry_mask  0xfffff
#define Bndry_mask1 0xfffff
#define LSB 1
#define Sign_bit 0x80000000
#define Log2P 1
#define Tiny0 0
#define Tiny1 1
#define Quick_max 14
#define Int_max 14

#ifndef Flt_Rounds
#ifdef FLT_ROUNDS
#define Flt_Rounds FLT_ROUNDS
#else
#define Flt_Rounds 1
#endif
#endif /*Flt_Rounds*/

#else /* ifndef IEEE_Arith */
#undef  Sudden_Underflow
#define Sudden_Underflow
#ifdef IBM
#undef Flt_Rounds
#define Flt_Rounds 0
#define Exp_shift  24
#define Exp_shift1 24
#define Exp_msk1   0x1000000
#define Exp_msk11  0x1000000
#define Exp_mask  0x7f000000
#define P 14
#define Bias 65
#define Exp_1  0x41000000
#define Exp_11 0x41000000
#define Ebits 8	/* exponent has 7 bits, but 8 is the right value in b2d */
#define Frac_mask  0xffffff
#define Frac_mask1 0xffffff
#define Bletch 4
#define Ten_pmax 22
#define Bndry_mask  0xefffff
#define Bndry_mask1 0xffffff
#define LSB 1
#define Sign_bit 0x80000000
#define Log2P 4
#define Tiny0 0x100000
#define Tiny1 0
#define Quick_max 14
#define Int_max 15
#else /* VAX */
#undef Flt_Rounds
#define Flt_Rounds 1
#define Exp_shift  23
#define Exp_shift1 7
#define Exp_msk1    0x80
#define Exp_msk11   0x800000
#define Exp_mask  0x7f80
#define P 56
#define Bias 129
#define Exp_1  0x40800000
#define Exp_11 0x4080
#define Ebits 8
#define Frac_mask  0x7fffff
#define Frac_mask1 0xffff007f
#define Ten_pmax 24
#define Bletch 2
#define Bndry_mask  0xffff007f
#define Bndry_mask1 0xffff007f
#define LSB 0x10000
#define Sign_bit 0x8000
#define Log2P 1
#define Tiny0 0x80
#define Tiny1 0
#define Quick_max 15
#define Int_max 15
#endif /* IBM, VAX */
#endif /* IEEE_Arith */

#ifndef IEEE_Arith
#define ROUND_BIASED
#else
#ifdef ROUND_BIASED_without_Round_Up
#undef  ROUND_BIASED
#define ROUND_BIASED
#endif
#endif

#ifdef RND_PRODQUOT
#define rounded_product(a,b) a = rnd_prod(a, b)
#define rounded_quotient(a,b) a = rnd_quot(a, b)
#ifdef KR_headers
extern double rnd_prod(), rnd_quot();
#else
extern double rnd_prod(double, double), rnd_quot(double, double);
#endif
#else
#define rounded_product(a,b) a *= b
#define rounded_quotient(a,b) a /= b
#endif

#define Big0 (Frac_mask1 | Exp_msk1*(DBL_MAX_EXP+Bias-1))
#define Big1 0xffffffff

#undef  Pack_16
#ifndef Pack_32
#define Pack_32
#endif

#ifdef NO_LONG_LONG
#undef ULLong
#ifdef Just_16
#undef Pack_32
#define Pack_16
/* When Pack_32 is not defined, we store 16 bits per 32-bit Long.
 * This makes some inner loops simpler and sometimes saves work
 * during multiplications, but it often seems to make things slightly
 * slower.  Hence the default is now to store 32 bits per Long.
 */
#endif
#else	/* long long available */
#ifndef Llong
#define Llong long long
#endif
#ifndef ULLong
#define ULLong unsigned Llong
#endif
#endif /* NO_LONG_LONG */

#ifdef Pack_32
#define ULbits 32
#define kshift 5
#define kmask 31
#define ALL_ON 0xffffffff
#else
#define ULbits 16
#define kshift 4
#define kmask 15
#define ALL_ON 0xffff
#endif

#define MULTIPLE_THREADS
extern pthread_mutex_t __gdtoa_locks[2];
#define ACQUIRE_DTOA_LOCK(n)	do {				\
	if (__isthreaded)					\
		_pthread_mutex_lock(&__gdtoa_locks[n]);		\
} while(0)
#define FREE_DTOA_LOCK(n)	do {				\
	if (__isthreaded)					\
		_pthread_mutex_unlock(&__gdtoa_locks[n]);	\
} while(0)

#define Kmax 9

 struct
Bigint {
	struct Bigint *next;
	int k, maxwds, sign, wds;
	ULong x[1];
	};

 typedef struct Bigint Bigint;

#ifdef NO_STRING_H
#ifdef DECLARE_SIZE_T
typedef unsigned int size_t;
#endif
extern void memcpy_D2A ANSI((void*, const void*, size_t));
#define Bcopy(x,y) memcpy_D2A(&x->sign,&y->sign,y->wds*sizeof(ULong) + 2*sizeof(int))
#else /* !NO_STRING_H */
#define Bcopy(x,y) memcpy(&x->sign,&y->sign,y->wds*sizeof(ULong) + 2*sizeof(int))
#endif /* NO_STRING_H */

/*
 * Paranoia: Protect exported symbols, including ones in files we don't
 * compile right now.  The standard strtof and strtod survive.
 */
#define	dtoa		__dtoa
#define	gdtoa		__gdtoa
#define	freedtoa	__freedtoa
#define	strtodg		__strtodg
#define	g_ddfmt		__g_ddfmt
#define	g_dfmt		__g_dfmt
#define	g_ffmt		__g_ffmt
#define	g_Qfmt		__g_Qfmt
#define	g_xfmt		__g_xfmt
#define	g_xLfmt		__g_xLfmt
#define	strtoId		__strtoId
#define	strtoIdd	__strtoIdd
#define	strtoIf		__strtoIf
#define	strtoIQ		__strtoIQ
#define	strtoIx		__strtoIx
#define	strtoIxL	__strtoIxL
#define	strtord_l		__strtord_l
#define	strtordd	__strtordd
#define	strtorf		__strtorf
#define	strtorQ_l		__strtorQ_l
#define	strtorx_l		__strtorx_l
#define	strtorxL	__strtorxL
#define	strtodI		__strtodI
#define	strtopd		__strtopd
#define	strtopdd	__strtopdd
#define	strtopf		__strtopf
#define	strtopQ		__strtopQ
#define	strtopx		__strtopx
#define	strtopxL	__strtopxL

/* Protect gdtoa-internal symbols */
#define	Balloc		__Balloc_D2A
#define	Bfree		__Bfree_D2A
#define	ULtoQ		__ULtoQ_D2A
#define	ULtof		__ULtof_D2A
#define	ULtod		__ULtod_D2A
#define	ULtodd		__ULtodd_D2A
#define	ULtox		__ULtox_D2A
#define	ULtoxL		__ULtoxL_D2A
#define	any_on		__any_on_D2A
#define	b2d		__b2d_D2A
#define	bigtens		__bigtens_D2A
#define	cmp		__cmp_D2A
#define	copybits	__copybits_D2A
#define	d2b		__d2b_D2A
#define	decrement	__decrement_D2A
#define	diff		__diff_D2A
#define	dtoa_result	__dtoa_result_D2A
#define	g__fmt		__g__fmt_D2A
#define	gethex		__gethex_D2A
#define	hexdig		__hexdig_D2A
#define	hexdig_init_D2A	__hexdig_init_D2A
#define	hexnan		__hexnan_D2A
#define	hi0bits		__hi0bits_D2A
#define	hi0bits_D2A	__hi0bits_D2A
#define	i2b		__i2b_D2A
#define	increment	__increment_D2A
#define	lo0bits		__lo0bits_D2A
#define	lshift		__lshift_D2A
#define	match		__match_D2A
#define	mult		__mult_D2A
#define	multadd		__multadd_D2A
#define	nrv_alloc	__nrv_alloc_D2A
#define	pow5mult	__pow5mult_D2A
#define	quorem		__quorem_D2A
#define	ratio		__ratio_D2A
#define	rshift		__rshift_D2A
#define	rv_alloc	__rv_alloc_D2A
#define	s2b		__s2b_D2A
#define	set_ones	__set_ones_D2A
#define	strcp		__strcp_D2A
#define	strcp_D2A      	__strcp_D2A
#define	strtoIg		__strtoIg_D2A
#define	sum		__sum_D2A
#define	tens		__tens_D2A
#define	tinytens	__tinytens_D2A
#define	tinytens	__tinytens_D2A
#define	trailz		__trailz_D2A
#define	ulp		__ulp_D2A

 extern char *dtoa_result;
 extern CONST double bigtens[], tens[], tinytens[];
 extern unsigned char hexdig[];

 extern Bigint *Balloc ANSI((int));
 extern void Bfree ANSI((Bigint*));
 extern void ULtof ANSI((ULong*, ULong*, Long, int));
 extern void ULtod ANSI((ULong*, ULong*, Long, int));
 extern void ULtodd ANSI((ULong*, ULong*, Long, int));
 extern void ULtoQ ANSI((ULong*, ULong*, Long, int));
 extern void ULtox ANSI((UShort*, ULong*, Long, int));
 extern void ULtoxL ANSI((ULong*, ULong*, Long, int));
 extern ULong any_on ANSI((Bigint*, int));
 extern double b2d ANSI((Bigint*, int*));
 extern int cmp ANSI((Bigint*, Bigint*));
 extern void copybits ANSI((ULong*, int, Bigint*));
 extern Bigint *d2b ANSI((double, int*, int*));
 extern void decrement ANSI((Bigint*));
 extern Bigint *diff ANSI((Bigint*, Bigint*));
 extern char *dtoa ANSI((double d, int mode, int ndigits,
			int *decpt, int *sign, char **rve));
 extern void freedtoa ANSI((char*));
 extern char *gdtoa ANSI((FPI *fpi, int be, ULong *bits, int *kindp,
			  int mode, int ndigits, int *decpt, char **rve));
 extern char *g__fmt ANSI((char*, char*, char*, int, ULong, size_t));
 extern int gethex ANSI((CONST char**, FPI*, Long*, Bigint**, int));
 extern void hexdig_init_D2A(Void);
 extern int hexnan ANSI((CONST char**, FPI*, ULong*));
 extern int hi0bits ANSI((ULong));
 extern Bigint *i2b ANSI((int));
 extern Bigint *increment ANSI((Bigint*));
 extern int lo0bits ANSI((ULong*));
 extern Bigint *lshift ANSI((Bigint*, int));
 extern int match ANSI((CONST char**, char*));
 extern Bigint *mult ANSI((Bigint*, Bigint*));
 extern Bigint *multadd ANSI((Bigint*, int, int));
 extern char *nrv_alloc ANSI((char*, char **, int));
 extern Bigint *pow5mult ANSI((Bigint*, int));
 extern int quorem ANSI((Bigint*, Bigint*));
 extern double ratio ANSI((Bigint*, Bigint*));
 extern void rshift ANSI((Bigint*, int));
 extern char *rv_alloc ANSI((int));
 extern Bigint *s2b ANSI((CONST char*, int, int, ULong, int));
 extern Bigint *set_ones ANSI((Bigint*, int));
 extern char *strcp ANSI((char*, const char*));
 extern int strtodg_l ANSI((CONST char*, char**, FPI*, Long*, ULong*, locale_t));

 extern int strtoId ANSI((CONST char *, char **, double *, double *));
 extern int strtoIdd ANSI((CONST char *, char **, double *, double *));
 extern int strtoIf ANSI((CONST char *, char **, float *, float *));
 extern int strtoIg ANSI((CONST char*, char**, FPI*, Long*, Bigint**, int*));
 extern int strtoIQ ANSI((CONST char *, char **, void *, void *));
 extern int strtoIx ANSI((CONST char *, char **, void *, void *));
 extern int strtoIxL ANSI((CONST char *, char **, void *, void *));
 extern double strtod ANSI((const char *s00, char **se));
 extern double strtod_l ANSI((const char *s00, char **se, locale_t));
 extern int strtopQ ANSI((CONST char *, char **, Void *));
 extern int strtopf ANSI((CONST char *, char **, float *));
 extern int strtopd ANSI((CONST char *, char **, double *));
 extern int strtopdd ANSI((CONST char *, char **, double *));
 extern int strtopx ANSI((CONST char *, char **, Void *));
 extern int strtopxL ANSI((CONST char *, char **, Void *));
 extern int strtord_l ANSI((CONST char *, char **, int, double *, locale_t));
 extern int strtordd ANSI((CONST char *, char **, int, double *));
 extern int strtorf ANSI((CONST char *, char **, int, float *));
 extern int strtorQ_l ANSI((CONST char *, char **, int, void *, locale_t));
 extern int strtorx_l ANSI((CONST char *, char **, int, void *, locale_t));
 extern int strtorxL ANSI((CONST char *, char **, int, void *));
 extern Bigint *sum ANSI((Bigint*, Bigint*));
 extern int trailz ANSI((Bigint*));
 extern double ulp ANSI((U*));

#ifdef __cplusplus
}
#endif
/*
 * NAN_WORD0 and NAN_WORD1 are only referenced in strtod.c.  Prior to
 * 20050115, they used to be hard-wired here (to 0x7ff80000 and 0,
 * respectively), but now are determined by compiling and running
 * qnan.c to generate gd_qnan.h, which specifies d_QNAN0 and d_QNAN1.
 * Formerly gdtoaimp.h recommended supplying suitable -DNAN_WORD0=...
 * and -DNAN_WORD1=...  values if necessary.  This should still work.
 * (On HP Series 700/800 machines, -DNAN_WORD0=0x7ff40000 works.)
 */
#ifdef IEEE_Arith
#ifndef NO_INFNAN_CHECK
#undef INFNAN_CHECK
#define INFNAN_CHECK
#endif
#ifdef IEEE_MC68k
#define _0 0
#define _1 1
#ifndef NAN_WORD0
#define NAN_WORD0 d_QNAN0
#endif
#ifndef NAN_WORD1
#define NAN_WORD1 d_QNAN1
#endif
#else
#define _0 1
#define _1 0
#ifndef NAN_WORD0
#define NAN_WORD0 d_QNAN1
#endif
#ifndef NAN_WORD1
#define NAN_WORD1 d_QNAN0
#endif
#endif
#else
#undef INFNAN_CHECK
#endif

#undef SI
#ifdef Sudden_Underflow
#define SI 1
#else
#define SI 0
#endif

#endif /* GDTOAIMP_H_INCLUDED */
