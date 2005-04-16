/*
 *
 *      Copyright (c) 1993 Ning and David Mosberger.
 
 This is based on code originally written by Bas Laarhoven (bas@vimec.nl)
 and David L. Brown, Jr., and incorporates improvements suggested by
 Kai Harrekilde-Petersen.

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License as
 published by the Free Software Foundation; either version 2, or (at
 your option) any later version.
 
 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-ecc.c,v $
 * $Revision: 1.3 $
 * $Date: 1997/10/05 19:18:10 $
 *
 *      This file contains the Reed-Solomon error correction code 
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

#include <linux/ftape.h>

#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/ftape-ecc.h"

/* Machines that are big-endian should define macro BIG_ENDIAN.
 * Unfortunately, there doesn't appear to be a standard include file
 * that works for all OSs.
 */

#if defined(__sparc__) || defined(__hppa)
#define BIG_ENDIAN
#endif				/* __sparc__ || __hppa */

#if defined(__mips__)
#error Find a smart way to determine the Endianness of the MIPS CPU
#endif

/* Notice: to minimize the potential for confusion, we use r to
 *         denote the independent variable of the polynomials in the
 *         Galois Field GF(2^8).  We reserve x for polynomials that
 *         that have coefficients in GF(2^8).
 *         
 * The Galois Field in which coefficient arithmetic is performed are
 * the polynomials over Z_2 (i.e., 0 and 1) modulo the irreducible
 * polynomial f(r), where f(r)=r^8 + r^7 + r^2 + r + 1.  A polynomial
 * is represented as a byte with the MSB as the coefficient of r^7 and
 * the LSB as the coefficient of r^0.  For example, the binary
 * representation of f(x) is 0x187 (of course, this doesn't fit into 8
 * bits).  In this field, the polynomial r is a primitive element.
 * That is, r^i with i in 0,...,255 enumerates all elements in the
 * field.
 *
 * The generator polynomial for the QIC-80 ECC is
 *
 *      g(x) = x^3 + r^105*x^2 + r^105*x + 1
 *
 * which can be factored into:
 *
 *      g(x) = (x-r^-1)(x-r^0)(x-r^1)
 *
 * the byte representation of the coefficients are:
 *
 *      r^105 = 0xc0
 *      r^-1  = 0xc3
 *      r^0   = 0x01
 *      r^1   = 0x02
 *
 * Notice that r^-1 = r^254 as exponent arithmetic is performed
 * modulo 2^8-1 = 255.
 *
 * For more information on Galois Fields and Reed-Solomon codes, refer
 * to any good book.  I found _An Introduction to Error Correcting
 * Codes with Applications_ by S. A. Vanstone and P. C. van Oorschot
 * to be a good introduction into the former.  _CODING THEORY: The
 * Essentials_ I found very useful for its concise description of
 * Reed-Solomon encoding/decoding.
 *
 */

typedef __u8 Matrix[3][3];

/*
 * gfpow[] is defined such that gfpow[i] returns r^i if
 * i is in the range [0..255].
 */
static const __u8 gfpow[] =
{
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
	0x87, 0x89, 0x95, 0xad, 0xdd, 0x3d, 0x7a, 0xf4,
	0x6f, 0xde, 0x3b, 0x76, 0xec, 0x5f, 0xbe, 0xfb,
	0x71, 0xe2, 0x43, 0x86, 0x8b, 0x91, 0xa5, 0xcd,
	0x1d, 0x3a, 0x74, 0xe8, 0x57, 0xae, 0xdb, 0x31,
	0x62, 0xc4, 0x0f, 0x1e, 0x3c, 0x78, 0xf0, 0x67,
	0xce, 0x1b, 0x36, 0x6c, 0xd8, 0x37, 0x6e, 0xdc,
	0x3f, 0x7e, 0xfc, 0x7f, 0xfe, 0x7b, 0xf6, 0x6b,
	0xd6, 0x2b, 0x56, 0xac, 0xdf, 0x39, 0x72, 0xe4,
	0x4f, 0x9e, 0xbb, 0xf1, 0x65, 0xca, 0x13, 0x26,
	0x4c, 0x98, 0xb7, 0xe9, 0x55, 0xaa, 0xd3, 0x21,
	0x42, 0x84, 0x8f, 0x99, 0xb5, 0xed, 0x5d, 0xba,
	0xf3, 0x61, 0xc2, 0x03, 0x06, 0x0c, 0x18, 0x30,
	0x60, 0xc0, 0x07, 0x0e, 0x1c, 0x38, 0x70, 0xe0,
	0x47, 0x8e, 0x9b, 0xb1, 0xe5, 0x4d, 0x9a, 0xb3,
	0xe1, 0x45, 0x8a, 0x93, 0xa1, 0xc5, 0x0d, 0x1a,
	0x34, 0x68, 0xd0, 0x27, 0x4e, 0x9c, 0xbf, 0xf9,
	0x75, 0xea, 0x53, 0xa6, 0xcb, 0x11, 0x22, 0x44,
	0x88, 0x97, 0xa9, 0xd5, 0x2d, 0x5a, 0xb4, 0xef,
	0x59, 0xb2, 0xe3, 0x41, 0x82, 0x83, 0x81, 0x85,
	0x8d, 0x9d, 0xbd, 0xfd, 0x7d, 0xfa, 0x73, 0xe6,
	0x4b, 0x96, 0xab, 0xd1, 0x25, 0x4a, 0x94, 0xaf,
	0xd9, 0x35, 0x6a, 0xd4, 0x2f, 0x5e, 0xbc, 0xff,
	0x79, 0xf2, 0x63, 0xc6, 0x0b, 0x16, 0x2c, 0x58,
	0xb0, 0xe7, 0x49, 0x92, 0xa3, 0xc1, 0x05, 0x0a,
	0x14, 0x28, 0x50, 0xa0, 0xc7, 0x09, 0x12, 0x24,
	0x48, 0x90, 0xa7, 0xc9, 0x15, 0x2a, 0x54, 0xa8,
	0xd7, 0x29, 0x52, 0xa4, 0xcf, 0x19, 0x32, 0x64,
	0xc8, 0x17, 0x2e, 0x5c, 0xb8, 0xf7, 0x69, 0xd2,
	0x23, 0x46, 0x8c, 0x9f, 0xb9, 0xf5, 0x6d, 0xda,
	0x33, 0x66, 0xcc, 0x1f, 0x3e, 0x7c, 0xf8, 0x77,
	0xee, 0x5b, 0xb6, 0xeb, 0x51, 0xa2, 0xc3, 0x01
};

/*
 * This is a log table.  That is, gflog[r^i] returns i (modulo f(r)).
 * gflog[0] is undefined and the first element is therefore not valid.
 */
static const __u8 gflog[256] =
{
	0xff, 0x00, 0x01, 0x63, 0x02, 0xc6, 0x64, 0x6a,
	0x03, 0xcd, 0xc7, 0xbc, 0x65, 0x7e, 0x6b, 0x2a,
	0x04, 0x8d, 0xce, 0x4e, 0xc8, 0xd4, 0xbd, 0xe1,
	0x66, 0xdd, 0x7f, 0x31, 0x6c, 0x20, 0x2b, 0xf3,
	0x05, 0x57, 0x8e, 0xe8, 0xcf, 0xac, 0x4f, 0x83,
	0xc9, 0xd9, 0xd5, 0x41, 0xbe, 0x94, 0xe2, 0xb4,
	0x67, 0x27, 0xde, 0xf0, 0x80, 0xb1, 0x32, 0x35,
	0x6d, 0x45, 0x21, 0x12, 0x2c, 0x0d, 0xf4, 0x38,
	0x06, 0x9b, 0x58, 0x1a, 0x8f, 0x79, 0xe9, 0x70,
	0xd0, 0xc2, 0xad, 0xa8, 0x50, 0x75, 0x84, 0x48,
	0xca, 0xfc, 0xda, 0x8a, 0xd6, 0x54, 0x42, 0x24,
	0xbf, 0x98, 0x95, 0xf9, 0xe3, 0x5e, 0xb5, 0x15,
	0x68, 0x61, 0x28, 0xba, 0xdf, 0x4c, 0xf1, 0x2f,
	0x81, 0xe6, 0xb2, 0x3f, 0x33, 0xee, 0x36, 0x10,
	0x6e, 0x18, 0x46, 0xa6, 0x22, 0x88, 0x13, 0xf7,
	0x2d, 0xb8, 0x0e, 0x3d, 0xf5, 0xa4, 0x39, 0x3b,
	0x07, 0x9e, 0x9c, 0x9d, 0x59, 0x9f, 0x1b, 0x08,
	0x90, 0x09, 0x7a, 0x1c, 0xea, 0xa0, 0x71, 0x5a,
	0xd1, 0x1d, 0xc3, 0x7b, 0xae, 0x0a, 0xa9, 0x91,
	0x51, 0x5b, 0x76, 0x72, 0x85, 0xa1, 0x49, 0xeb,
	0xcb, 0x7c, 0xfd, 0xc4, 0xdb, 0x1e, 0x8b, 0xd2,
	0xd7, 0x92, 0x55, 0xaa, 0x43, 0x0b, 0x25, 0xaf,
	0xc0, 0x73, 0x99, 0x77, 0x96, 0x5c, 0xfa, 0x52,
	0xe4, 0xec, 0x5f, 0x4a, 0xb6, 0xa2, 0x16, 0x86,
	0x69, 0xc5, 0x62, 0xfe, 0x29, 0x7d, 0xbb, 0xcc,
	0xe0, 0xd3, 0x4d, 0x8c, 0xf2, 0x1f, 0x30, 0xdc,
	0x82, 0xab, 0xe7, 0x56, 0xb3, 0x93, 0x40, 0xd8,
	0x34, 0xb0, 0xef, 0x26, 0x37, 0x0c, 0x11, 0x44,
	0x6f, 0x78, 0x19, 0x9a, 0x47, 0x74, 0xa7, 0xc1,
	0x23, 0x53, 0x89, 0xfb, 0x14, 0x5d, 0xf8, 0x97,
	0x2e, 0x4b, 0xb9, 0x60, 0x0f, 0xed, 0x3e, 0xe5,
	0xf6, 0x87, 0xa5, 0x17, 0x3a, 0xa3, 0x3c, 0xb7
};

/* This is a multiplication table for the factor 0xc0 (i.e., r^105 (mod f(r)).
 * gfmul_c0[f] returns r^105 * f(r) (modulo f(r)).
 */
static const __u8 gfmul_c0[256] =
{
	0x00, 0xc0, 0x07, 0xc7, 0x0e, 0xce, 0x09, 0xc9,
	0x1c, 0xdc, 0x1b, 0xdb, 0x12, 0xd2, 0x15, 0xd5,
	0x38, 0xf8, 0x3f, 0xff, 0x36, 0xf6, 0x31, 0xf1,
	0x24, 0xe4, 0x23, 0xe3, 0x2a, 0xea, 0x2d, 0xed,
	0x70, 0xb0, 0x77, 0xb7, 0x7e, 0xbe, 0x79, 0xb9,
	0x6c, 0xac, 0x6b, 0xab, 0x62, 0xa2, 0x65, 0xa5,
	0x48, 0x88, 0x4f, 0x8f, 0x46, 0x86, 0x41, 0x81,
	0x54, 0x94, 0x53, 0x93, 0x5a, 0x9a, 0x5d, 0x9d,
	0xe0, 0x20, 0xe7, 0x27, 0xee, 0x2e, 0xe9, 0x29,
	0xfc, 0x3c, 0xfb, 0x3b, 0xf2, 0x32, 0xf5, 0x35,
	0xd8, 0x18, 0xdf, 0x1f, 0xd6, 0x16, 0xd1, 0x11,
	0xc4, 0x04, 0xc3, 0x03, 0xca, 0x0a, 0xcd, 0x0d,
	0x90, 0x50, 0x97, 0x57, 0x9e, 0x5e, 0x99, 0x59,
	0x8c, 0x4c, 0x8b, 0x4b, 0x82, 0x42, 0x85, 0x45,
	0xa8, 0x68, 0xaf, 0x6f, 0xa6, 0x66, 0xa1, 0x61,
	0xb4, 0x74, 0xb3, 0x73, 0xba, 0x7a, 0xbd, 0x7d,
	0x47, 0x87, 0x40, 0x80, 0x49, 0x89, 0x4e, 0x8e,
	0x5b, 0x9b, 0x5c, 0x9c, 0x55, 0x95, 0x52, 0x92,
	0x7f, 0xbf, 0x78, 0xb8, 0x71, 0xb1, 0x76, 0xb6,
	0x63, 0xa3, 0x64, 0xa4, 0x6d, 0xad, 0x6a, 0xaa,
	0x37, 0xf7, 0x30, 0xf0, 0x39, 0xf9, 0x3e, 0xfe,
	0x2b, 0xeb, 0x2c, 0xec, 0x25, 0xe5, 0x22, 0xe2,
	0x0f, 0xcf, 0x08, 0xc8, 0x01, 0xc1, 0x06, 0xc6,
	0x13, 0xd3, 0x14, 0xd4, 0x1d, 0xdd, 0x1a, 0xda,
	0xa7, 0x67, 0xa0, 0x60, 0xa9, 0x69, 0xae, 0x6e,
	0xbb, 0x7b, 0xbc, 0x7c, 0xb5, 0x75, 0xb2, 0x72,
	0x9f, 0x5f, 0x98, 0x58, 0x91, 0x51, 0x96, 0x56,
	0x83, 0x43, 0x84, 0x44, 0x8d, 0x4d, 0x8a, 0x4a,
	0xd7, 0x17, 0xd0, 0x10, 0xd9, 0x19, 0xde, 0x1e,
	0xcb, 0x0b, 0xcc, 0x0c, 0xc5, 0x05, 0xc2, 0x02,
	0xef, 0x2f, 0xe8, 0x28, 0xe1, 0x21, 0xe6, 0x26,
	0xf3, 0x33, 0xf4, 0x34, 0xfd, 0x3d, 0xfa, 0x3a
};


/* Returns V modulo 255 provided V is in the range -255,-254,...,509.
 */
static inline __u8 mod255(int v)
{
	if (v > 0) {
		if (v < 255) {
			return v;
		} else {
			return v - 255;
		}
	} else {
		return v + 255;
	}
}


/* Add two numbers in the field.  Addition in this field is equivalent
 * to a bit-wise exclusive OR operation---subtraction is therefore
 * identical to addition.
 */
static inline __u8 gfadd(__u8 a, __u8 b)
{
	return a ^ b;
}


/* Add two vectors of numbers in the field.  Each byte in A and B gets
 * added individually.
 */
static inline unsigned long gfadd_long(unsigned long a, unsigned long b)
{
	return a ^ b;
}


/* Multiply two numbers in the field:
 */
static inline __u8 gfmul(__u8 a, __u8 b)
{
	if (a && b) {
		return gfpow[mod255(gflog[a] + gflog[b])];
	} else {
		return 0;
	}
}


/* Just like gfmul, except we have already looked up the log of the
 * second number.
 */
static inline __u8 gfmul_exp(__u8 a, int b)
{
	if (a) {
		return gfpow[mod255(gflog[a] + b)];
	} else {
		return 0;
	}
}


/* Just like gfmul_exp, except that A is a vector of numbers.  That
 * is, each byte in A gets multiplied by gfpow[mod255(B)].
 */
static inline unsigned long gfmul_exp_long(unsigned long a, int b)
{
	__u8 t;

	if (sizeof(long) == 4) {
		return (
		((t = (__u32)a >> 24 & 0xff) ?
		 (((__u32) gfpow[mod255(gflog[t] + b)]) << 24) : 0) |
		((t = (__u32)a >> 16 & 0xff) ?
		 (((__u32) gfpow[mod255(gflog[t] + b)]) << 16) : 0) |
		((t = (__u32)a >> 8 & 0xff) ?
		 (((__u32) gfpow[mod255(gflog[t] + b)]) << 8) : 0) |
		((t = (__u32)a >> 0 & 0xff) ?
		 (((__u32) gfpow[mod255(gflog[t] + b)]) << 0) : 0));
	} else if (sizeof(long) == 8) {
		return (
		((t = (__u64)a >> 56 & 0xff) ?
		 (((__u64) gfpow[mod255(gflog[t] + b)]) << 56) : 0) |
		((t = (__u64)a >> 48 & 0xff) ?
		 (((__u64) gfpow[mod255(gflog[t] + b)]) << 48) : 0) |
		((t = (__u64)a >> 40 & 0xff) ?
		 (((__u64) gfpow[mod255(gflog[t] + b)]) << 40) : 0) |
		((t = (__u64)a >> 32 & 0xff) ?
		 (((__u64) gfpow[mod255(gflog[t] + b)]) << 32) : 0) |
		((t = (__u64)a >> 24 & 0xff) ?
		 (((__u64) gfpow[mod255(gflog[t] + b)]) << 24) : 0) |
		((t = (__u64)a >> 16 & 0xff) ?
		 (((__u64) gfpow[mod255(gflog[t] + b)]) << 16) : 0) |
		((t = (__u64)a >> 8 & 0xff) ?
		 (((__u64) gfpow[mod255(gflog[t] + b)]) << 8) : 0) |
		((t = (__u64)a >> 0 & 0xff) ?
		 (((__u64) gfpow[mod255(gflog[t] + b)]) << 0) : 0));
	} else {
		TRACE_FUN(ft_t_any);
		TRACE_ABORT(-1, ft_t_err, "Error: size of long is %d bytes",
			    (int)sizeof(long));
	}
}


/* Divide two numbers in the field.  Returns a/b (modulo f(x)).
 */
static inline __u8 gfdiv(__u8 a, __u8 b)
{
	if (!b) {
		TRACE_FUN(ft_t_any);
		TRACE_ABORT(0xff, ft_t_bug, "Error: division by zero");
	} else if (a == 0) {
		return 0;
	} else {
		return gfpow[mod255(gflog[a] - gflog[b])];
	}
}


/* The following functions return the inverse of the matrix of the
 * linear system that needs to be solved to determine the error
 * magnitudes.  The first deals with matrices of rank 3, while the
 * second deals with matrices of rank 2.  The error indices are passed
 * in arguments L0,..,L2 (0=first sector, 31=last sector).  The error
 * indices must be sorted in ascending order, i.e., L0<L1<L2.
 *
 * The linear system that needs to be solved for the error magnitudes
 * is A * b = s, where s is the known vector of syndromes, b is the
 * vector of error magnitudes and A in the ORDER=3 case:
 *
 *    A_3 = {{1/r^L[0], 1/r^L[1], 1/r^L[2]},
 *          {        1,        1,        1},
 *          { r^L[0], r^L[1], r^L[2]}} 
 */
static inline int gfinv3(__u8 l0,
			 __u8 l1, 
			 __u8 l2, 
			 Matrix Ainv)
{
	__u8 det;
	__u8 t20, t10, t21, t12, t01, t02;
	int log_det;

	/* compute some intermediate results: */
	t20 = gfpow[l2 - l0];	        /* t20 = r^l2/r^l0 */
	t10 = gfpow[l1 - l0];	        /* t10 = r^l1/r^l0 */
	t21 = gfpow[l2 - l1];	        /* t21 = r^l2/r^l1 */
	t12 = gfpow[l1 - l2 + 255];	/* t12 = r^l1/r^l2 */
	t01 = gfpow[l0 - l1 + 255];	/* t01 = r^l0/r^l1 */
	t02 = gfpow[l0 - l2 + 255];	/* t02 = r^l0/r^l2 */
	/* Calculate the determinant of matrix A_3^-1 (sometimes
	 * called the Vandermonde determinant):
	 */
	det = gfadd(t20, gfadd(t10, gfadd(t21, gfadd(t12, gfadd(t01, t02)))));
	if (!det) {
		TRACE_FUN(ft_t_any);
		TRACE_ABORT(0, ft_t_err,
			   "Inversion failed (3 CRC errors, >0 CRC failures)");
	}
	log_det = 255 - gflog[det];

	/* Now, calculate all of the coefficients:
	 */
	Ainv[0][0]= gfmul_exp(gfadd(gfpow[l1], gfpow[l2]), log_det);
	Ainv[0][1]= gfmul_exp(gfadd(t21, t12), log_det);
	Ainv[0][2]= gfmul_exp(gfadd(gfpow[255 - l1], gfpow[255 - l2]),log_det);

	Ainv[1][0]= gfmul_exp(gfadd(gfpow[l0], gfpow[l2]), log_det);
	Ainv[1][1]= gfmul_exp(gfadd(t20, t02), log_det);
	Ainv[1][2]= gfmul_exp(gfadd(gfpow[255 - l0], gfpow[255 - l2]),log_det);

	Ainv[2][0]= gfmul_exp(gfadd(gfpow[l0], gfpow[l1]), log_det);
	Ainv[2][1]= gfmul_exp(gfadd(t10, t01), log_det);
	Ainv[2][2]= gfmul_exp(gfadd(gfpow[255 - l0], gfpow[255 - l1]),log_det);

	return 1;
}


static inline int gfinv2(__u8 l0, __u8 l1, Matrix Ainv)
{
	__u8 det;
	__u8 t1, t2;
	int log_det;

	t1 = gfpow[255 - l0];
	t2 = gfpow[255 - l1];
	det = gfadd(t1, t2);
	if (!det) {
		TRACE_FUN(ft_t_any);
		TRACE_ABORT(0, ft_t_err,
			   "Inversion failed (2 CRC errors, >0 CRC failures)");
	}
	log_det = 255 - gflog[det];

	/* Now, calculate all of the coefficients:
	 */
	Ainv[0][0] = Ainv[1][0] = gfpow[log_det];

	Ainv[0][1] = gfmul_exp(t2, log_det);
	Ainv[1][1] = gfmul_exp(t1, log_det);

	return 1;
}


/* Multiply matrix A by vector S and return result in vector B.  M is
 * assumed to be of order NxN, S and B of order Nx1.
 */
static inline void gfmat_mul(int n, Matrix A, 
			     __u8 *s, __u8 *b)
{
	int i, j;
	__u8 dot_prod;

	for (i = 0; i < n; ++i) {
		dot_prod = 0;
		for (j = 0; j < n; ++j) {
			dot_prod = gfadd(dot_prod, gfmul(A[i][j], s[j]));
		}
		b[i] = dot_prod;
	}
}



/* The Reed Solomon ECC codes are computed over the N-th byte of each
 * block, where N=SECTOR_SIZE.  There are up to 29 blocks of data, and
 * 3 blocks of ECC.  The blocks are stored contiguously in memory.  A
 * segment, consequently, is assumed to have at least 4 blocks: one or
 * more data blocks plus three ECC blocks.
 *
 * Notice: In QIC-80 speak, a CRC error is a sector with an incorrect
 *         CRC.  A CRC failure is a sector with incorrect data, but
 *         a valid CRC.  In the error control literature, the former
 *         is usually called "erasure", the latter "error."
 */
/* Compute the parity bytes for C columns of data, where C is the
 * number of bytes that fit into a long integer.  We use a linear
 * feed-back register to do this.  The parity bytes P[0], P[STRIDE],
 * P[2*STRIDE] are computed such that:
 *
 *              x^k * p(x) + m(x) = 0 (modulo g(x))
 *
 * where k = NBLOCKS,
 *       p(x) = P[0] + P[STRIDE]*x + P[2*STRIDE]*x^2, and
 *       m(x) = sum_{i=0}^k m_i*x^i.
 *       m_i = DATA[i*SECTOR_SIZE]
 */
static inline void set_parity(unsigned long *data,
			      int nblocks, 
			      unsigned long *p, 
			      int stride)
{
	unsigned long p0, p1, p2, t1, t2, *end;

	end = data + nblocks * (FT_SECTOR_SIZE / sizeof(long));
	p0 = p1 = p2 = 0;
	while (data < end) {
		/* The new parity bytes p0_i, p1_i, p2_i are computed
		 * from the old values p0_{i-1}, p1_{i-1}, p2_{i-1}
		 * recursively as:
		 *
		 *        p0_i = p1_{i-1} + r^105 * (m_{i-1} - p0_{i-1})
		 *        p1_i = p2_{i-1} + r^105 * (m_{i-1} - p0_{i-1})
		 *        p2_i =                    (m_{i-1} - p0_{i-1})
		 *
		 * With the initial condition: p0_0 = p1_0 = p2_0 = 0.
		 */
		t1 = gfadd_long(*data, p0);
		/*
		 * Multiply each byte in t1 by 0xc0:
		 */
		if (sizeof(long) == 4) {
			t2= (((__u32) gfmul_c0[(__u32)t1 >> 24 & 0xff]) << 24 |
			     ((__u32) gfmul_c0[(__u32)t1 >> 16 & 0xff]) << 16 |
			     ((__u32) gfmul_c0[(__u32)t1 >>  8 & 0xff]) <<  8 |
			     ((__u32) gfmul_c0[(__u32)t1 >>  0 & 0xff]) <<  0);
		} else if (sizeof(long) == 8) {
			t2= (((__u64) gfmul_c0[(__u64)t1 >> 56 & 0xff]) << 56 |
			     ((__u64) gfmul_c0[(__u64)t1 >> 48 & 0xff]) << 48 |
			     ((__u64) gfmul_c0[(__u64)t1 >> 40 & 0xff]) << 40 |
			     ((__u64) gfmul_c0[(__u64)t1 >> 32 & 0xff]) << 32 |
			     ((__u64) gfmul_c0[(__u64)t1 >> 24 & 0xff]) << 24 |
			     ((__u64) gfmul_c0[(__u64)t1 >> 16 & 0xff]) << 16 |
			     ((__u64) gfmul_c0[(__u64)t1 >>  8 & 0xff]) <<  8 |
			     ((__u64) gfmul_c0[(__u64)t1 >>  0 & 0xff]) <<  0);
		} else {
			TRACE_FUN(ft_t_any);
			TRACE(ft_t_err, "Error: long is of size %d",
			      (int) sizeof(long));
			TRACE_EXIT;
		}
		p0 = gfadd_long(t2, p1);
		p1 = gfadd_long(t2, p2);
		p2 = t1;
		data += FT_SECTOR_SIZE / sizeof(long);
	}
	*p = p0;
	p += stride;
	*p = p1;
	p += stride;
	*p = p2;
	return;
}


/* Compute the 3 syndrome values.  DATA should point to the first byte
 * of the column for which the syndromes are desired.  The syndromes
 * are computed over the first NBLOCKS of rows.  The three bytes will
 * be placed in S[0], S[1], and S[2].
 *
 * S[i] is the value of the "message" polynomial m(x) evaluated at the
 * i-th root of the generator polynomial g(x).
 *
 * As g(x)=(x-r^-1)(x-1)(x-r^1) we evaluate the message polynomial at
 * x=r^-1 to get S[0], at x=r^0=1 to get S[1], and at x=r to get S[2].
 * This could be done directly and efficiently via the Horner scheme.
 * However, it would require multiplication tables for the factors
 * r^-1 (0xc3) and r (0x02).  The following scheme does not require
 * any multiplication tables beyond what's needed for set_parity()
 * anyway and is slightly faster if there are no errors and slightly
 * slower if there are errors.  The latter is hopefully the infrequent
 * case.
 *
 * To understand the alternative algorithm, notice that set_parity(m,
 * k, p) computes parity bytes such that:
 *
 *      x^k * p(x) = m(x) (modulo g(x)).
 *
 * That is, to evaluate m(r^m), where r^m is a root of g(x), we can
 * simply evaluate (r^m)^k*p(r^m).  Also, notice that p is 0 if and
 * only if s is zero.  That is, if all parity bytes are 0, we know
 * there is no error in the data and consequently there is no need to
 * compute s(x) at all!  In all other cases, we compute s(x) from p(x)
 * by evaluating (r^m)^k*p(r^m) for m=-1, m=0, and m=1.  The p(x)
 * polynomial is evaluated via the Horner scheme.
 */
static int compute_syndromes(unsigned long *data, int nblocks, unsigned long *s)
{
	unsigned long p[3];

	set_parity(data, nblocks, p, 1);
	if (p[0] | p[1] | p[2]) {
		/* Some of the checked columns do not have a zero
		 * syndrome.  For simplicity, we compute the syndromes
		 * for all columns that we have computed the
		 * remainders for.
		 */
		s[0] = gfmul_exp_long(
			gfadd_long(p[0], 
				   gfmul_exp_long(
					   gfadd_long(p[1], 
						      gfmul_exp_long(p[2], -1)),
					   -1)), 
			-nblocks);
		s[1] = gfadd_long(gfadd_long(p[2], p[1]), p[0]);
		s[2] = gfmul_exp_long(
			gfadd_long(p[0], 
				   gfmul_exp_long(
					   gfadd_long(p[1],
						      gfmul_exp_long(p[2], 1)),
					   1)),
			nblocks);
		return 0;
	} else {
		return 1;
	}
}


/* Correct the block in the column pointed to by DATA.  There are NBAD
 * CRC errors and their indices are in BAD_LOC[0], up to
 * BAD_LOC[NBAD-1].  If NBAD>1, Ainv holds the inverse of the matrix
 * of the linear system that needs to be solved to determine the error
 * magnitudes.  S[0], S[1], and S[2] are the syndrome values.  If row
 * j gets corrected, then bit j will be set in CORRECTION_MAP.
 */
static inline int correct_block(__u8 *data, int nblocks,
				int nbad, int *bad_loc, Matrix Ainv,
				__u8 *s,
				SectorMap * correction_map)
{
	int ncorrected = 0;
	int i;
	__u8 t1, t2;
	__u8 c0, c1, c2;	/* check bytes */
	__u8 error_mag[3], log_error_mag;
	__u8 *dp, l, e;
	TRACE_FUN(ft_t_any);

	switch (nbad) {
	case 0:
		/* might have a CRC failure: */
		if (s[0] == 0) {
			/* more than one error */
			TRACE_ABORT(-1, ft_t_err,
				 "ECC failed (0 CRC errors, >1 CRC failures)");
		}
		t1 = gfdiv(s[1], s[0]);
		if ((bad_loc[nbad++] = gflog[t1]) >= nblocks) {
			TRACE(ft_t_err,
			      "ECC failed (0 CRC errors, >1 CRC failures)");
			TRACE_ABORT(-1, ft_t_err,
				  "attempt to correct data at %d", bad_loc[0]);
		}
		error_mag[0] = s[1];
		break;
	case 1:
		t1 = gfadd(gfmul_exp(s[1], bad_loc[0]), s[2]);
		t2 = gfadd(gfmul_exp(s[0], bad_loc[0]), s[1]);
		if (t1 == 0 && t2 == 0) {
			/* one erasure, no error: */
			Ainv[0][0] = gfpow[bad_loc[0]];
		} else if (t1 == 0 || t2 == 0) {
			/* one erasure and more than one error: */
			TRACE_ABORT(-1, ft_t_err,
				    "ECC failed (1 erasure, >1 error)");
		} else {
			/* one erasure, one error: */
			if ((bad_loc[nbad++] = gflog[gfdiv(t1, t2)]) 
			    >= nblocks) {
				TRACE(ft_t_err, "ECC failed "
				      "(1 CRC errors, >1 CRC failures)");
				TRACE_ABORT(-1, ft_t_err,
					    "attempt to correct data at %d",
					    bad_loc[1]);
			}
			if (!gfinv2(bad_loc[0], bad_loc[1], Ainv)) {
				/* inversion failed---must have more
                                 *  than one error 
				 */
				TRACE_EXIT -1;
			}
		}
		/* FALL THROUGH TO ERROR MAGNITUDE COMPUTATION:
		 */
	case 2:
	case 3:
		/* compute error magnitudes: */
		gfmat_mul(nbad, Ainv, s, error_mag);
		break;

	default:
		TRACE_ABORT(-1, ft_t_err,
			    "Internal Error: number of CRC errors > 3");
	}

	/* Perform correction by adding ERROR_MAG[i] to the byte at
	 * offset BAD_LOC[i].  Also add the value of the computed
	 * error polynomial to the syndrome values.  If the correction
	 * was successful, the resulting check bytes should be zero
	 * (i.e., the corrected data is a valid code word).
	 */
	c0 = s[0];
	c1 = s[1];
	c2 = s[2];
	for (i = 0; i < nbad; ++i) {
		e = error_mag[i];
		if (e) {
			/* correct the byte at offset L by magnitude E: */
			l = bad_loc[i];
			dp = &data[l * FT_SECTOR_SIZE];
			*dp = gfadd(*dp, e);
			*correction_map |= 1 << l;
			++ncorrected;

			log_error_mag = gflog[e];
			c0 = gfadd(c0, gfpow[mod255(log_error_mag - l)]);
			c1 = gfadd(c1, e);
			c2 = gfadd(c2, gfpow[mod255(log_error_mag + l)]);
		}
	}
	if (c0 || c1 || c2) {
		TRACE_ABORT(-1, ft_t_err,
			    "ECC self-check failed, too many errors");
	}
	TRACE_EXIT ncorrected;
}


#if defined(ECC_SANITY_CHECK) || defined(ECC_PARANOID)

/* Perform a sanity check on the computed parity bytes:
 */
static int sanity_check(unsigned long *data, int nblocks)
{
	TRACE_FUN(ft_t_any);
	unsigned long s[3];

	if (!compute_syndromes(data, nblocks, s)) {
		TRACE_ABORT(0, ft_bug,
			    "Internal Error: syndrome self-check failed");
	}
	TRACE_EXIT 1;
}

#endif /* defined(ECC_SANITY_CHECK) || defined(ECC_PARANOID) */

/* Compute the parity for an entire segment of data.
 */
int ftape_ecc_set_segment_parity(struct memory_segment *mseg)
{
	int i;
	__u8 *parity_bytes;

	parity_bytes = &mseg->data[(mseg->blocks - 3) * FT_SECTOR_SIZE];
	for (i = 0; i < FT_SECTOR_SIZE; i += sizeof(long)) {
		set_parity((unsigned long *) &mseg->data[i], mseg->blocks - 3,
			   (unsigned long *) &parity_bytes[i],
			   FT_SECTOR_SIZE / sizeof(long));
#ifdef ECC_PARANOID
		if (!sanity_check((unsigned long *) &mseg->data[i],
				   mseg->blocks)) {
			return -1;
		}
#endif				/* ECC_PARANOID */
	}
	return 0;
}


/* Checks and corrects (if possible) the segment MSEG.  Returns one of
 * ECC_OK, ECC_CORRECTED, and ECC_FAILED.
 */
int ftape_ecc_correct_data(struct memory_segment *mseg)
{
	int col, i, result;
	int ncorrected = 0;
	int nerasures = 0;	/* # of erasures (CRC errors) */
	int erasure_loc[3];	/* erasure locations */
	unsigned long ss[3];
	__u8 s[3];
	Matrix Ainv;
	TRACE_FUN(ft_t_flow);

	mseg->corrected = 0;

	/* find first column that has non-zero syndromes: */
	for (col = 0; col < FT_SECTOR_SIZE; col += sizeof(long)) {
		if (!compute_syndromes((unsigned long *) &mseg->data[col],
				       mseg->blocks, ss)) {
			/* something is wrong---have to fix things */
			break;
		}
	}
	if (col >= FT_SECTOR_SIZE) {
		/* all syndromes are ok, therefore nothing to correct */
		TRACE_EXIT ECC_OK;
	}
	/* count the number of CRC errors if there were any: */
	if (mseg->read_bad) {
		for (i = 0; i < mseg->blocks; i++) {
			if (BAD_CHECK(mseg->read_bad, i)) {
				if (nerasures >= 3) {
					/* this is too much for ECC */
					TRACE_ABORT(ECC_FAILED, ft_t_err,
						"ECC failed (>3 CRC errors)");
				}	/* if */
				erasure_loc[nerasures++] = i;
			}
		}
	}
	/*
	 * If there are at least 2 CRC errors, determine inverse of matrix
	 * of linear system to be solved:
	 */
	switch (nerasures) {
	case 2:
		if (!gfinv2(erasure_loc[0], erasure_loc[1], Ainv)) {
			TRACE_EXIT ECC_FAILED;
		}
		break;
	case 3:
		if (!gfinv3(erasure_loc[0], erasure_loc[1],
			    erasure_loc[2], Ainv)) {
			TRACE_EXIT ECC_FAILED;
		}
		break;
	default:
		/* this is not an error condition... */
		break;
	}

	do {
		for (i = 0; i < sizeof(long); ++i) {
			s[0] = ss[0];
			s[1] = ss[1];
			s[2] = ss[2];
			if (s[0] | s[1] | s[2]) {
#ifdef BIG_ENDIAN
				result = correct_block(
					&mseg->data[col + sizeof(long) - 1 - i],
					mseg->blocks,
					nerasures,
					erasure_loc,
					Ainv,
					s,
					&mseg->corrected);
#else
				result = correct_block(&mseg->data[col + i],
						       mseg->blocks,
						       nerasures,
						       erasure_loc,
						       Ainv,
						       s,
						       &mseg->corrected);
#endif
				if (result < 0) {
					TRACE_EXIT ECC_FAILED;
				}
				ncorrected += result;
			}
			ss[0] >>= 8;
			ss[1] >>= 8;
			ss[2] >>= 8;
		}

#ifdef ECC_SANITY_CHECK
		if (!sanity_check((unsigned long *) &mseg->data[col],
				  mseg->blocks)) {
			TRACE_EXIT ECC_FAILED;
		}
#endif				/* ECC_SANITY_CHECK */

		/* find next column with non-zero syndromes: */
		while ((col += sizeof(long)) < FT_SECTOR_SIZE) {
			if (!compute_syndromes((unsigned long *)
				    &mseg->data[col], mseg->blocks, ss)) {
				/* something is wrong---have to fix things */
				break;
			}
		}
	} while (col < FT_SECTOR_SIZE);
	if (ncorrected && nerasures == 0) {
		TRACE(ft_t_warn, "block contained error not caught by CRC");
	}
	TRACE((ncorrected > 0) ? ft_t_noise : ft_t_any, "number of corrections: %d", ncorrected);
	TRACE_EXIT ncorrected ? ECC_CORRECTED : ECC_OK;
}
