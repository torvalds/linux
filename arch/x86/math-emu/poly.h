/* SPDX-License-Identifier: GPL-2.0 */
/*---------------------------------------------------------------------------+
 |  poly.h                                                                   |
 |                                                                           |
 |  Header file for the FPU-emu poly*.c source files.                        |
 |                                                                           |
 | Copyright (C) 1994,1999                                                   |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail   billm@melbpc.org.au            |
 |                                                                           |
 | Declarations and definitions for functions operating on Xsig (12-byte     |
 | extended-significand) quantities.                                         |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#ifndef _POLY_H
#define _POLY_H

/* This 12-byte structure is used to improve the accuracy of computation
   of transcendental functions.
   Intended to be used to get results better than 8-byte computation
   allows. 9-byte would probably be sufficient.
   */
typedef struct {
	unsigned long lsw;
	unsigned long midw;
	unsigned long msw;
} Xsig;

asmlinkage void mul64(unsigned long long const *a, unsigned long long const *b,
		      unsigned long long *result);
asmlinkage void polynomial_Xsig(Xsig *, const unsigned long long *x,
				const unsigned long long terms[], const int n);

asmlinkage void mul32_Xsig(Xsig *, const unsigned long mult);
asmlinkage void mul64_Xsig(Xsig *, const unsigned long long *mult);
asmlinkage void mul_Xsig_Xsig(Xsig *dest, const Xsig *mult);

asmlinkage void shr_Xsig(Xsig *, const int n);
asmlinkage int round_Xsig(Xsig *);
asmlinkage int norm_Xsig(Xsig *);
asmlinkage void div_Xsig(Xsig *x1, const Xsig *x2, const Xsig *dest);

/* Macro to extract the most significant 32 bits from a long long */
#define LL_MSW(x)     (((unsigned long *)&x)[1])

/* Macro to initialize an Xsig struct */
#define MK_XSIG(a,b,c)     { c, b, a }

/* Macro to access the 8 ms bytes of an Xsig as a long long */
#define XSIG_LL(x)         (*(unsigned long long *)&x.midw)

/*
   Need to run gcc with optimizations on to get these to
   actually be in-line.
   */

/* Multiply two fixed-point 32 bit numbers, producing a 32 bit result.
   The answer is the ms word of the product. */
/* Some versions of gcc make it difficult to stop eax from being clobbered.
   Merely specifying that it is used doesn't work...
 */
static inline unsigned long mul_32_32(const unsigned long arg1,
				      const unsigned long arg2)
{
	int retval;
	asm volatile ("mull %2; movl %%edx,%%eax":"=a" (retval)
		      :"0"(arg1), "g"(arg2)
		      :"dx");
	return retval;
}

/* Add the 12 byte Xsig x2 to Xsig dest, with no checks for overflow. */
static inline void add_Xsig_Xsig(Xsig *dest, const Xsig *x2)
{
	asm volatile ("movl %1,%%edi; movl %2,%%esi;\n"
		      "movl (%%esi),%%eax; addl %%eax,(%%edi);\n"
		      "movl 4(%%esi),%%eax; adcl %%eax,4(%%edi);\n"
		      "movl 8(%%esi),%%eax; adcl %%eax,8(%%edi);\n":"=g"
		      (*dest):"g"(dest), "g"(x2)
		      :"ax", "si", "di");
}

/* Add the 12 byte Xsig x2 to Xsig dest, adjust exp if overflow occurs. */
/* Note: the constraints in the asm statement didn't always work properly
   with gcc 2.5.8.  Changing from using edi to using ecx got around the
   problem, but keep fingers crossed! */
static inline void add_two_Xsig(Xsig *dest, const Xsig *x2, long int *exp)
{
	asm volatile ("movl %2,%%ecx; movl %3,%%esi;\n"
		      "movl (%%esi),%%eax; addl %%eax,(%%ecx);\n"
		      "movl 4(%%esi),%%eax; adcl %%eax,4(%%ecx);\n"
		      "movl 8(%%esi),%%eax; adcl %%eax,8(%%ecx);\n"
		      "jnc 0f;\n"
		      "rcrl 8(%%ecx); rcrl 4(%%ecx); rcrl (%%ecx)\n"
		      "movl %4,%%ecx; incl (%%ecx)\n"
		      "movl $1,%%eax; jmp 1f;\n"
		      "0: xorl %%eax,%%eax;\n" "1:\n":"=g" (*exp), "=g"(*dest)
		      :"g"(dest), "g"(x2), "g"(exp)
		      :"cx", "si", "ax");
}

/* Negate (subtract from 1.0) the 12 byte Xsig */
/* This is faster in a loop on my 386 than using the "neg" instruction. */
static inline void negate_Xsig(Xsig *x)
{
	asm volatile ("movl %1,%%esi;\n"
		      "xorl %%ecx,%%ecx;\n"
		      "movl %%ecx,%%eax; subl (%%esi),%%eax; movl %%eax,(%%esi);\n"
		      "movl %%ecx,%%eax; sbbl 4(%%esi),%%eax; movl %%eax,4(%%esi);\n"
		      "movl %%ecx,%%eax; sbbl 8(%%esi),%%eax; movl %%eax,8(%%esi);\n":"=g"
		      (*x):"g"(x):"si", "ax", "cx");
}

#endif /* _POLY_H */
