/* SPDX-License-Identifier: GPL-2.0 */
	.file	"wm_sqrt.S"
/*---------------------------------------------------------------------------+
 |  wm_sqrt.S                                                                |
 |                                                                           |
 | Fixed point arithmetic square root evaluation.                            |
 |                                                                           |
 | Copyright (C) 1992,1993,1995,1997                                         |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail billm@suburbia.net               |
 |                                                                           |
 | Call from C as:                                                           |
 |    int wm_sqrt(FPU_REG *n, unsigned int control_word)                     |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 |  wm_sqrt(FPU_REG *n, unsigned int control_word)                           |
 |    returns the square root of n in n.                                     |
 |                                                                           |
 |  Use Newton's method to compute the square root of a number, which must   |
 |  be in the range  [1.0 .. 4.0),  to 64 bits accuracy.                     |
 |  Does not check the sign or tag of the argument.                          |
 |  Sets the exponent, but not the sign or tag of the result.                |
 |                                                                           |
 |  The guess is kept in %esi:%edi                                           |
 +---------------------------------------------------------------------------*/

#include "exception.h"
#include "fpu_emu.h"


#ifndef NON_REENTRANT_FPU
/*	Local storage on the stack: */
#define FPU_accum_3	-4(%ebp)	/* ms word */
#define FPU_accum_2	-8(%ebp)
#define FPU_accum_1	-12(%ebp)
#define FPU_accum_0	-16(%ebp)

/*
 * The de-normalised argument:
 *                  sq_2                  sq_1              sq_0
 *        b b b b b b b ... b b b   b b b .... b b b   b 0 0 0 ... 0
 *           ^ binary point here
 */
#define FPU_fsqrt_arg_2	-20(%ebp)	/* ms word */
#define FPU_fsqrt_arg_1	-24(%ebp)
#define FPU_fsqrt_arg_0	-28(%ebp)	/* ls word, at most the ms bit is set */

#else
/*	Local storage in a static area: */
.data
	.align 4,0
FPU_accum_3:
	.long	0		/* ms word */
FPU_accum_2:
	.long	0
FPU_accum_1:
	.long	0
FPU_accum_0:
	.long	0

/* The de-normalised argument:
                    sq_2                  sq_1              sq_0
          b b b b b b b ... b b b   b b b .... b b b   b 0 0 0 ... 0
             ^ binary point here
 */
FPU_fsqrt_arg_2:
	.long	0		/* ms word */
FPU_fsqrt_arg_1:
	.long	0
FPU_fsqrt_arg_0:
	.long	0		/* ls word, at most the ms bit is set */
#endif /* NON_REENTRANT_FPU */ 


.text
ENTRY(wm_sqrt)
	pushl	%ebp
	movl	%esp,%ebp
#ifndef NON_REENTRANT_FPU
	subl	$28,%esp
#endif /* NON_REENTRANT_FPU */
	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	PARAM1,%esi

	movl	SIGH(%esi),%eax
	movl	SIGL(%esi),%ecx
	xorl	%edx,%edx

/* We use a rough linear estimate for the first guess.. */

	cmpw	EXP_BIAS,EXP(%esi)
	jnz	sqrt_arg_ge_2

	shrl	$1,%eax			/* arg is in the range  [1.0 .. 2.0) */
	rcrl	$1,%ecx
	rcrl	$1,%edx

sqrt_arg_ge_2:
/* From here on, n is never accessed directly again until it is
   replaced by the answer. */

	movl	%eax,FPU_fsqrt_arg_2		/* ms word of n */
	movl	%ecx,FPU_fsqrt_arg_1
	movl	%edx,FPU_fsqrt_arg_0

/* Make a linear first estimate */
	shrl	$1,%eax
	addl	$0x40000000,%eax
	movl	$0xaaaaaaaa,%ecx
	mull	%ecx
	shll	%edx			/* max result was 7fff... */
	testl	$0x80000000,%edx	/* but min was 3fff... */
	jnz	sqrt_prelim_no_adjust

	movl	$0x80000000,%edx	/* round up */

sqrt_prelim_no_adjust:
	movl	%edx,%esi	/* Our first guess */

/* We have now computed (approx)   (2 + x) / 3, which forms the basis
   for a few iterations of Newton's method */

	movl	FPU_fsqrt_arg_2,%ecx	/* ms word */

/*
 * From our initial estimate, three iterations are enough to get us
 * to 30 bits or so. This will then allow two iterations at better
 * precision to complete the process.
 */

/* Compute  (g + n/g)/2  at each iteration (g is the guess). */
	shrl	%ecx		/* Doing this first will prevent a divide */
				/* overflow later. */

	movl	%ecx,%edx	/* msw of the arg / 2 */
	divl	%esi		/* current estimate */
	shrl	%esi		/* divide by 2 */
	addl	%eax,%esi	/* the new estimate */

	movl	%ecx,%edx
	divl	%esi
	shrl	%esi
	addl	%eax,%esi

	movl	%ecx,%edx
	divl	%esi
	shrl	%esi
	addl	%eax,%esi

/*
 * Now that an estimate accurate to about 30 bits has been obtained (in %esi),
 * we improve it to 60 bits or so.
 *
 * The strategy from now on is to compute new estimates from
 *      guess := guess + (n - guess^2) / (2 * guess)
 */

/* First, find the square of the guess */
	movl	%esi,%eax
	mull	%esi
/* guess^2 now in %edx:%eax */

	movl	FPU_fsqrt_arg_1,%ecx
	subl	%ecx,%eax
	movl	FPU_fsqrt_arg_2,%ecx	/* ms word of normalized n */
	sbbl	%ecx,%edx
	jnc	sqrt_stage_2_positive

/* Subtraction gives a negative result,
   negate the result before division. */
	notl	%edx
	notl	%eax
	addl	$1,%eax
	adcl	$0,%edx

	divl	%esi
	movl	%eax,%ecx

	movl	%edx,%eax
	divl	%esi
	jmp	sqrt_stage_2_finish

sqrt_stage_2_positive:
	divl	%esi
	movl	%eax,%ecx

	movl	%edx,%eax
	divl	%esi

	notl	%ecx
	notl	%eax
	addl	$1,%eax
	adcl	$0,%ecx

sqrt_stage_2_finish:
	sarl	$1,%ecx		/* divide by 2 */
	rcrl	$1,%eax

	/* Form the new estimate in %esi:%edi */
	movl	%eax,%edi
	addl	%ecx,%esi

	jnz	sqrt_stage_2_done	/* result should be [1..2) */

#ifdef PARANOID
/* It should be possible to get here only if the arg is ffff....ffff */
	cmpl	$0xffffffff,FPU_fsqrt_arg_1
	jnz	sqrt_stage_2_error
#endif /* PARANOID */

/* The best rounded result. */
	xorl	%eax,%eax
	decl	%eax
	movl	%eax,%edi
	movl	%eax,%esi
	movl	$0x7fffffff,%eax
	jmp	sqrt_round_result

#ifdef PARANOID
sqrt_stage_2_error:
	pushl	EX_INTERNAL|0x213
	call	EXCEPTION
#endif /* PARANOID */ 

sqrt_stage_2_done:

/* Now the square root has been computed to better than 60 bits. */

/* Find the square of the guess. */
	movl	%edi,%eax		/* ls word of guess */
	mull	%edi
	movl	%edx,FPU_accum_1

	movl	%esi,%eax
	mull	%esi
	movl	%edx,FPU_accum_3
	movl	%eax,FPU_accum_2

	movl	%edi,%eax
	mull	%esi
	addl	%eax,FPU_accum_1
	adcl	%edx,FPU_accum_2
	adcl	$0,FPU_accum_3

/*	movl	%esi,%eax */
/*	mull	%edi */
	addl	%eax,FPU_accum_1
	adcl	%edx,FPU_accum_2
	adcl	$0,FPU_accum_3

/* guess^2 now in FPU_accum_3:FPU_accum_2:FPU_accum_1 */

	movl	FPU_fsqrt_arg_0,%eax		/* get normalized n */
	subl	%eax,FPU_accum_1
	movl	FPU_fsqrt_arg_1,%eax
	sbbl	%eax,FPU_accum_2
	movl	FPU_fsqrt_arg_2,%eax		/* ms word of normalized n */
	sbbl	%eax,FPU_accum_3
	jnc	sqrt_stage_3_positive

/* Subtraction gives a negative result,
   negate the result before division */
	notl	FPU_accum_1
	notl	FPU_accum_2
	notl	FPU_accum_3
	addl	$1,FPU_accum_1
	adcl	$0,FPU_accum_2

#ifdef PARANOID
	adcl	$0,FPU_accum_3	/* This must be zero */
	jz	sqrt_stage_3_no_error

sqrt_stage_3_error:
	pushl	EX_INTERNAL|0x207
	call	EXCEPTION

sqrt_stage_3_no_error:
#endif /* PARANOID */

	movl	FPU_accum_2,%edx
	movl	FPU_accum_1,%eax
	divl	%esi
	movl	%eax,%ecx

	movl	%edx,%eax
	divl	%esi

	sarl	$1,%ecx		/* divide by 2 */
	rcrl	$1,%eax

	/* prepare to round the result */

	addl	%ecx,%edi
	adcl	$0,%esi

	jmp	sqrt_stage_3_finished

sqrt_stage_3_positive:
	movl	FPU_accum_2,%edx
	movl	FPU_accum_1,%eax
	divl	%esi
	movl	%eax,%ecx

	movl	%edx,%eax
	divl	%esi

	sarl	$1,%ecx		/* divide by 2 */
	rcrl	$1,%eax

	/* prepare to round the result */

	notl	%eax		/* Negate the correction term */
	notl	%ecx
	addl	$1,%eax
	adcl	$0,%ecx		/* carry here ==> correction == 0 */
	adcl	$0xffffffff,%esi

	addl	%ecx,%edi
	adcl	$0,%esi

sqrt_stage_3_finished:

/*
 * The result in %esi:%edi:%esi should be good to about 90 bits here,
 * and the rounding information here does not have sufficient accuracy
 * in a few rare cases.
 */
	cmpl	$0xffffffe0,%eax
	ja	sqrt_near_exact_x

	cmpl	$0x00000020,%eax
	jb	sqrt_near_exact

	cmpl	$0x7fffffe0,%eax
	jb	sqrt_round_result

	cmpl	$0x80000020,%eax
	jb	sqrt_get_more_precision

sqrt_round_result:
/* Set up for rounding operations */
	movl	%eax,%edx
	movl	%esi,%eax
	movl	%edi,%ebx
	movl	PARAM1,%edi
	movw	EXP_BIAS,EXP(%edi)	/* Result is in  [1.0 .. 2.0) */
	jmp	fpu_reg_round


sqrt_near_exact_x:
/* First, the estimate must be rounded up. */
	addl	$1,%edi
	adcl	$0,%esi

sqrt_near_exact:
/*
 * This is an easy case because x^1/2 is monotonic.
 * We need just find the square of our estimate, compare it
 * with the argument, and deduce whether our estimate is
 * above, below, or exact. We use the fact that the estimate
 * is known to be accurate to about 90 bits.
 */
	movl	%edi,%eax		/* ls word of guess */
	mull	%edi
	movl	%edx,%ebx		/* 2nd ls word of square */
	movl	%eax,%ecx		/* ls word of square */

	movl	%edi,%eax
	mull	%esi
	addl	%eax,%ebx
	addl	%eax,%ebx

#ifdef PARANOID
	cmp	$0xffffffb0,%ebx
	jb	sqrt_near_exact_ok

	cmp	$0x00000050,%ebx
	ja	sqrt_near_exact_ok

	pushl	EX_INTERNAL|0x214
	call	EXCEPTION

sqrt_near_exact_ok:
#endif /* PARANOID */ 

	or	%ebx,%ebx
	js	sqrt_near_exact_small

	jnz	sqrt_near_exact_large

	or	%ebx,%edx
	jnz	sqrt_near_exact_large

/* Our estimate is exactly the right answer */
	xorl	%eax,%eax
	jmp	sqrt_round_result

sqrt_near_exact_small:
/* Our estimate is too small */
	movl	$0x000000ff,%eax
	jmp	sqrt_round_result
	
sqrt_near_exact_large:
/* Our estimate is too large, we need to decrement it */
	subl	$1,%edi
	sbbl	$0,%esi
	movl	$0xffffff00,%eax
	jmp	sqrt_round_result


sqrt_get_more_precision:
/* This case is almost the same as the above, except we start
   with an extra bit of precision in the estimate. */
	stc			/* The extra bit. */
	rcll	$1,%edi		/* Shift the estimate left one bit */
	rcll	$1,%esi

	movl	%edi,%eax		/* ls word of guess */
	mull	%edi
	movl	%edx,%ebx		/* 2nd ls word of square */
	movl	%eax,%ecx		/* ls word of square */

	movl	%edi,%eax
	mull	%esi
	addl	%eax,%ebx
	addl	%eax,%ebx

/* Put our estimate back to its original value */
	stc			/* The ms bit. */
	rcrl	$1,%esi		/* Shift the estimate left one bit */
	rcrl	$1,%edi

#ifdef PARANOID
	cmp	$0xffffff60,%ebx
	jb	sqrt_more_prec_ok

	cmp	$0x000000a0,%ebx
	ja	sqrt_more_prec_ok

	pushl	EX_INTERNAL|0x215
	call	EXCEPTION

sqrt_more_prec_ok:
#endif /* PARANOID */ 

	or	%ebx,%ebx
	js	sqrt_more_prec_small

	jnz	sqrt_more_prec_large

	or	%ebx,%ecx
	jnz	sqrt_more_prec_large

/* Our estimate is exactly the right answer */
	movl	$0x80000000,%eax
	jmp	sqrt_round_result

sqrt_more_prec_small:
/* Our estimate is too small */
	movl	$0x800000ff,%eax
	jmp	sqrt_round_result
	
sqrt_more_prec_large:
/* Our estimate is too large */
	movl	$0x7fffff00,%eax
	jmp	sqrt_round_result
ENDPROC(wm_sqrt)
