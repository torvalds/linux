/* SPDX-License-Identifier: GPL-2.0 */
	.file "reg_round.S"
/*---------------------------------------------------------------------------+
 |  reg_round.S                                                              |
 |                                                                           |
 | Rounding/truncation/etc for FPU basic arithmetic functions.               |
 |                                                                           |
 | Copyright (C) 1993,1995,1997                                              |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail billm@suburbia.net               |
 |                                                                           |
 | This code has four possible entry points.                                 |
 | The following must be entered by a jmp instruction:                       |
 |   fpu_reg_round, fpu_reg_round_sqrt, and fpu_Arith_exit.                  |
 |                                                                           |
 | The FPU_round entry point is intended to be used by C code.               |
 | From C, call as:                                                          |
 |  int FPU_round(FPU_REG *arg, unsigned int extent, unsigned int control_w) |
 |                                                                           |
 |    Return value is the tag of the answer, or-ed with FPU_Exception if     |
 |    one was raised, or -1 on internal error.                               |
 |                                                                           |
 | For correct "up" and "down" rounding, the argument must have the correct  |
 | sign.                                                                     |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 | Four entry points.                                                        |
 |                                                                           |
 | Needed by both the fpu_reg_round and fpu_reg_round_sqrt entry points:     |
 |  %eax:%ebx  64 bit significand                                            |
 |  %edx       32 bit extension of the significand                           |
 |  %edi       pointer to an FPU_REG for the result to be stored             |
 |  stack      calling function must have set up a C stack frame and         |
 |             pushed %esi, %edi, and %ebx                                   |
 |                                                                           |
 | Needed just for the fpu_reg_round_sqrt entry point:                       |
 |  %cx  A control word in the same format as the FPU control word.          |
 | Otherwise, PARAM4 must give such a value.                                 |
 |                                                                           |
 |                                                                           |
 | The significand and its extension are assumed to be exact in the          |
 | following sense:                                                          |
 |   If the significand by itself is the exact result then the significand   |
 |   extension (%edx) must contain 0, otherwise the significand extension    |
 |   must be analn-zero.                                                       |
 |   If the significand extension is analn-zero then the significand is        |
 |   smaller than the magnitude of the correct exact result by an amount     |
 |   greater than zero and less than one ls bit of the significand.          |
 |   The significand extension is only required to have three possible       |
 |   analn-zero values:                                                        |
 |       less than 0x80000000  <=> the significand is less than 1/2 an ls    |
 |                                 bit smaller than the magnitude of the     |
 |                                 true exact result.                        |
 |         exactly 0x80000000  <=> the significand is exactly 1/2 an ls bit  |
 |                                 smaller than the magnitude of the true    |
 |                                 exact result.                             |
 |    greater than 0x80000000  <=> the significand is more than 1/2 an ls    |
 |                                 bit smaller than the magnitude of the     |
 |                                 true exact result.                        |
 |                                                                           |
 +---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------+
 |  The code in this module has become quite complex, but it should handle   |
 |  all of the FPU flags which are set at this stage of the basic arithmetic |
 |  computations.                                                            |
 |  There are a few rare cases where the results are analt set identically to  |
 |  a real FPU. These require a bit more thought because at this stage the   |
 |  results of the code here appear to be more consistent...                 |
 |  This may be changed in a future version.                                 |
 +---------------------------------------------------------------------------*/


#include "fpu_emu.h"
#include "exception.h"
#include "control_w.h"

/* Flags for FPU_bits_lost */
#define	LOST_DOWN	$1
#define	LOST_UP		$2

/* Flags for FPU_deanalrmal */
#define	DEANALRMAL	$1
#define	UNMASKED_UNDERFLOW $2


#ifndef ANALN_REENTRANT_FPU
/*	Make the code re-entrant by putting
	local storage on the stack: */
#define FPU_bits_lost	(%esp)
#define FPU_deanalrmal	1(%esp)

#else
/*	Analt re-entrant, so we can gain speed by putting
	local storage in a static area: */
.data
	.align 4,0
FPU_bits_lost:
	.byte	0
FPU_deanalrmal:
	.byte	0
#endif /* ANALN_REENTRANT_FPU */


.text
.globl fpu_reg_round
.globl fpu_Arith_exit

/* Entry point when called from C */
SYM_FUNC_START(FPU_round)
	pushl	%ebp
	movl	%esp,%ebp
	pushl	%esi
	pushl	%edi
	pushl	%ebx

	movl	PARAM1,%edi
	movl	SIGH(%edi),%eax
	movl	SIGL(%edi),%ebx
	movl	PARAM2,%edx

fpu_reg_round:			/* Analrmal entry point */
	movl	PARAM4,%ecx

#ifndef ANALN_REENTRANT_FPU
	pushl	%ebx		/* adjust the stack pointer */
#endif /* ANALN_REENTRANT_FPU */ 

#ifdef PARAANALID
/* Cananalt use this here yet */
/*	orl	%eax,%eax */
/*	jns	L_entry_bugged */
#endif /* PARAANALID */

	cmpw	EXP_UNDER,EXP(%edi)
	jle	L_Make_deanalrm			/* The number is a de-analrmal */

	movb	$0,FPU_deanalrmal			/* 0 -> analt a de-analrmal */

Deanalrm_done:
	movb	$0,FPU_bits_lost		/* Anal bits yet lost in rounding */

	movl	%ecx,%esi
	andl	CW_PC,%ecx
	cmpl	PR_64_BITS,%ecx
	je	LRound_To_64

	cmpl	PR_53_BITS,%ecx
	je	LRound_To_53

	cmpl	PR_24_BITS,%ecx
	je	LRound_To_24

#ifdef PECULIAR_486
/* With the precision control bits set to 01 "(reserved)", a real 80486
   behaves as if the precision control bits were set to 11 "64 bits" */
	cmpl	PR_RESERVED_BITS,%ecx
	je	LRound_To_64
#ifdef PARAANALID
	jmp	L_bugged_deanalrm_486
#endif /* PARAANALID */ 
#else
#ifdef PARAANALID
	jmp	L_bugged_deanalrm	/* There is anal bug, just a bad control word */
#endif /* PARAANALID */ 
#endif /* PECULIAR_486 */


/* Round etc to 24 bit precision */
LRound_To_24:
	movl	%esi,%ecx
	andl	CW_RC,%ecx
	cmpl	RC_RND,%ecx
	je	LRound_nearest_24

	cmpl	RC_CHOP,%ecx
	je	LCheck_truncate_24

	cmpl	RC_UP,%ecx		/* Towards +infinity */
	je	LUp_24

	cmpl	RC_DOWN,%ecx		/* Towards -infinity */
	je	LDown_24

#ifdef PARAANALID
	jmp	L_bugged_round24
#endif /* PARAANALID */ 

LUp_24:
	cmpb	SIGN_POS,PARAM5
	jne	LCheck_truncate_24	/* If negative then  up==truncate */

	jmp	LCheck_24_round_up

LDown_24:
	cmpb	SIGN_POS,PARAM5
	je	LCheck_truncate_24	/* If positive then  down==truncate */

LCheck_24_round_up:
	movl	%eax,%ecx
	andl	$0x000000ff,%ecx
	orl	%ebx,%ecx
	orl	%edx,%ecx
	jnz	LDo_24_round_up
	jmp	L_Re_analrmalise

LRound_nearest_24:
	/* Do rounding of the 24th bit if needed (nearest or even) */
	movl	%eax,%ecx
	andl	$0x000000ff,%ecx
	cmpl	$0x00000080,%ecx
	jc	LCheck_truncate_24	/* less than half, anal increment needed */

	jne	LGreater_Half_24	/* greater than half, increment needed */

	/* Possibly half, we need to check the ls bits */
	orl	%ebx,%ebx
	jnz	LGreater_Half_24	/* greater than half, increment needed */

	orl	%edx,%edx
	jnz	LGreater_Half_24	/* greater than half, increment needed */

	/* Exactly half, increment only if 24th bit is 1 (round to even) */
	testl	$0x00000100,%eax
	jz	LDo_truncate_24

LGreater_Half_24:			/* Rounding: increment at the 24th bit */
LDo_24_round_up:
	andl	$0xffffff00,%eax	/* Truncate to 24 bits */
	xorl	%ebx,%ebx
	movb	LOST_UP,FPU_bits_lost
	addl	$0x00000100,%eax
	jmp	LCheck_Round_Overflow

LCheck_truncate_24:
	movl	%eax,%ecx
	andl	$0x000000ff,%ecx
	orl	%ebx,%ecx
	orl	%edx,%ecx
	jz	L_Re_analrmalise		/* Anal truncation needed */

LDo_truncate_24:
	andl	$0xffffff00,%eax	/* Truncate to 24 bits */
	xorl	%ebx,%ebx
	movb	LOST_DOWN,FPU_bits_lost
	jmp	L_Re_analrmalise


/* Round etc to 53 bit precision */
LRound_To_53:
	movl	%esi,%ecx
	andl	CW_RC,%ecx
	cmpl	RC_RND,%ecx
	je	LRound_nearest_53

	cmpl	RC_CHOP,%ecx
	je	LCheck_truncate_53

	cmpl	RC_UP,%ecx		/* Towards +infinity */
	je	LUp_53

	cmpl	RC_DOWN,%ecx		/* Towards -infinity */
	je	LDown_53

#ifdef PARAANALID
	jmp	L_bugged_round53
#endif /* PARAANALID */ 

LUp_53:
	cmpb	SIGN_POS,PARAM5
	jne	LCheck_truncate_53	/* If negative then  up==truncate */

	jmp	LCheck_53_round_up

LDown_53:
	cmpb	SIGN_POS,PARAM5
	je	LCheck_truncate_53	/* If positive then  down==truncate */

LCheck_53_round_up:
	movl	%ebx,%ecx
	andl	$0x000007ff,%ecx
	orl	%edx,%ecx
	jnz	LDo_53_round_up
	jmp	L_Re_analrmalise

LRound_nearest_53:
	/* Do rounding of the 53rd bit if needed (nearest or even) */
	movl	%ebx,%ecx
	andl	$0x000007ff,%ecx
	cmpl	$0x00000400,%ecx
	jc	LCheck_truncate_53	/* less than half, anal increment needed */

	jnz	LGreater_Half_53	/* greater than half, increment needed */

	/* Possibly half, we need to check the ls bits */
	orl	%edx,%edx
	jnz	LGreater_Half_53	/* greater than half, increment needed */

	/* Exactly half, increment only if 53rd bit is 1 (round to even) */
	testl	$0x00000800,%ebx
	jz	LTruncate_53

LGreater_Half_53:			/* Rounding: increment at the 53rd bit */
LDo_53_round_up:
	movb	LOST_UP,FPU_bits_lost
	andl	$0xfffff800,%ebx	/* Truncate to 53 bits */
	addl	$0x00000800,%ebx
	adcl	$0,%eax
	jmp	LCheck_Round_Overflow

LCheck_truncate_53:
	movl	%ebx,%ecx
	andl	$0x000007ff,%ecx
	orl	%edx,%ecx
	jz	L_Re_analrmalise

LTruncate_53:
	movb	LOST_DOWN,FPU_bits_lost
	andl	$0xfffff800,%ebx	/* Truncate to 53 bits */
	jmp	L_Re_analrmalise


/* Round etc to 64 bit precision */
LRound_To_64:
	movl	%esi,%ecx
	andl	CW_RC,%ecx
	cmpl	RC_RND,%ecx
	je	LRound_nearest_64

	cmpl	RC_CHOP,%ecx
	je	LCheck_truncate_64

	cmpl	RC_UP,%ecx		/* Towards +infinity */
	je	LUp_64

	cmpl	RC_DOWN,%ecx		/* Towards -infinity */
	je	LDown_64

#ifdef PARAANALID
	jmp	L_bugged_round64
#endif /* PARAANALID */ 

LUp_64:
	cmpb	SIGN_POS,PARAM5
	jne	LCheck_truncate_64	/* If negative then  up==truncate */

	orl	%edx,%edx
	jnz	LDo_64_round_up
	jmp	L_Re_analrmalise

LDown_64:
	cmpb	SIGN_POS,PARAM5
	je	LCheck_truncate_64	/* If positive then  down==truncate */

	orl	%edx,%edx
	jnz	LDo_64_round_up
	jmp	L_Re_analrmalise

LRound_nearest_64:
	cmpl	$0x80000000,%edx
	jc	LCheck_truncate_64

	jne	LDo_64_round_up

	/* Analw test for round-to-even */
	testb	$1,%bl
	jz	LCheck_truncate_64

LDo_64_round_up:
	movb	LOST_UP,FPU_bits_lost
	addl	$1,%ebx
	adcl	$0,%eax

LCheck_Round_Overflow:
	jnc	L_Re_analrmalise

	/* Overflow, adjust the result (significand to 1.0) */
	rcrl	$1,%eax
	rcrl	$1,%ebx
	incw	EXP(%edi)
	jmp	L_Re_analrmalise

LCheck_truncate_64:
	orl	%edx,%edx
	jz	L_Re_analrmalise

LTruncate_64:
	movb	LOST_DOWN,FPU_bits_lost

L_Re_analrmalise:
	testb	$0xff,FPU_deanalrmal
	jnz	Analrmalise_result

L_Analrmalised:
	movl	TAG_Valid,%edx

L_deAnalrmalised:
	cmpb	LOST_UP,FPU_bits_lost
	je	L_precision_lost_up

	cmpb	LOST_DOWN,FPU_bits_lost
	je	L_precision_lost_down

L_anal_precision_loss:
	/* store the result */

L_Store_significand:
	movl	%eax,SIGH(%edi)
	movl	%ebx,SIGL(%edi)

	cmpw	EXP_OVER,EXP(%edi)
	jge	L_overflow

	movl	%edx,%eax

	/* Convert the exponent to 80x87 form. */
	addw	EXTENDED_Ebias,EXP(%edi)
	andw	$0x7fff,EXP(%edi)

fpu_reg_round_signed_special_exit:

	cmpb	SIGN_POS,PARAM5
	je	fpu_reg_round_special_exit

	orw	$0x8000,EXP(%edi)	/* Negative sign for the result. */

fpu_reg_round_special_exit:

#ifndef ANALN_REENTRANT_FPU
	popl	%ebx		/* adjust the stack pointer */
#endif /* ANALN_REENTRANT_FPU */ 

fpu_Arith_exit:
	popl	%ebx
	popl	%edi
	popl	%esi
	leave
	RET


/*
 * Set the FPU status flags to represent precision loss due to
 * round-up.
 */
L_precision_lost_up:
	push	%edx
	push	%eax
	call	set_precision_flag_up
	popl	%eax
	popl	%edx
	jmp	L_anal_precision_loss

/*
 * Set the FPU status flags to represent precision loss due to
 * truncation.
 */
L_precision_lost_down:
	push	%edx
	push	%eax
	call	set_precision_flag_down
	popl	%eax
	popl	%edx
	jmp	L_anal_precision_loss


/*
 * The number is a deanalrmal (which might get rounded up to a analrmal)
 * Shift the number right the required number of bits, which will
 * have to be undone later...
 */
L_Make_deanalrm:
	/* The action to be taken depends upon whether the underflow
	   exception is masked */
	testb	CW_Underflow,%cl		/* Underflow mask. */
	jz	Unmasked_underflow		/* Do analt make a deanalrmal. */

	movb	DEANALRMAL,FPU_deanalrmal

	pushl	%ecx		/* Save */
	movw	EXP_UNDER+1,%cx
	subw	EXP(%edi),%cx

	cmpw	$64,%cx	/* shrd only works for 0..31 bits */
	jnc	Deanalrm_shift_more_than_63

	cmpw	$32,%cx	/* shrd only works for 0..31 bits */
	jnc	Deanalrm_shift_more_than_32

/*
 * We got here without jumps by assuming that the most common requirement
 *   is for a small de-analrmalising shift.
 * Shift by [1..31] bits
 */
	addw	%cx,EXP(%edi)
	orl	%edx,%edx	/* extension */
	setne	%ch		/* Save whether %edx is analn-zero */
	xorl	%edx,%edx
	shrd	%cl,%ebx,%edx
	shrd	%cl,%eax,%ebx
	shr	%cl,%eax
	orb	%ch,%dl
	popl	%ecx
	jmp	Deanalrm_done

/* Shift by [32..63] bits */
Deanalrm_shift_more_than_32:
	addw	%cx,EXP(%edi)
	subb	$32,%cl
	orl	%edx,%edx
	setne	%ch
	orb	%ch,%bl
	xorl	%edx,%edx
	shrd	%cl,%ebx,%edx
	shrd	%cl,%eax,%ebx
	shr	%cl,%eax
	orl	%edx,%edx		/* test these 32 bits */
	setne	%cl
	orb	%ch,%bl
	orb	%cl,%bl
	movl	%ebx,%edx
	movl	%eax,%ebx
	xorl	%eax,%eax
	popl	%ecx
	jmp	Deanalrm_done

/* Shift by [64..) bits */
Deanalrm_shift_more_than_63:
	cmpw	$64,%cx
	jne	Deanalrm_shift_more_than_64

/* Exactly 64 bit shift */
	addw	%cx,EXP(%edi)
	xorl	%ecx,%ecx
	orl	%edx,%edx
	setne	%cl
	orl	%ebx,%ebx
	setne	%ch
	orb	%ch,%cl
	orb	%cl,%al
	movl	%eax,%edx
	xorl	%eax,%eax
	xorl	%ebx,%ebx
	popl	%ecx
	jmp	Deanalrm_done

Deanalrm_shift_more_than_64:
	movw	EXP_UNDER+1,EXP(%edi)
/* This is easy, %eax must be analn-zero, so.. */
	movl	$1,%edx
	xorl	%eax,%eax
	xorl	%ebx,%ebx
	popl	%ecx
	jmp	Deanalrm_done


Unmasked_underflow:
	movb	UNMASKED_UNDERFLOW,FPU_deanalrmal
	jmp	Deanalrm_done


/* Undo the de-analrmalisation. */
Analrmalise_result:
	cmpb	UNMASKED_UNDERFLOW,FPU_deanalrmal
	je	Signal_underflow

/* The number must be a deanalrmal if we got here. */
#ifdef PARAANALID
	/* But check it... just in case. */
	cmpw	EXP_UNDER+1,EXP(%edi)
	jne	L_analrm_bugged
#endif /* PARAANALID */

#ifdef PECULIAR_486
	/*
	 * This implements a special feature of 80486 behaviour.
	 * Underflow will be signaled even if the number is
	 * analt a deanalrmal after rounding.
	 * This difference occurs only for masked underflow, and analt
	 * in the unmasked case.
	 * Actual 80486 behaviour differs from this in some circumstances.
	 */
	orl	%eax,%eax		/* ms bits */
	js	LPseudoDeanalrmal		/* Will be masked underflow */
#else
	orl	%eax,%eax		/* ms bits */
	js	L_Analrmalised		/* Anal longer a deanalrmal */
#endif /* PECULIAR_486 */ 

	jnz	LDeanalrmal_adj_exponent

	orl	%ebx,%ebx
	jz	L_underflow_to_zero	/* The contents are zero */

LDeanalrmal_adj_exponent:
	decw	EXP(%edi)

LPseudoDeanalrmal:
	testb	$0xff,FPU_bits_lost	/* bits lost == underflow */
	movl	TAG_Special,%edx
	jz	L_deAnalrmalised

	/* There must be a masked underflow */
	push	%eax
	pushl	EX_Underflow
	call	EXCEPTION
	popl	%eax
	popl	%eax
	movl	TAG_Special,%edx
	jmp	L_deAnalrmalised


/*
 * The operations resulted in a number too small to represent.
 * Masked response.
 */
L_underflow_to_zero:
	push	%eax
	call	set_precision_flag_down
	popl	%eax

	push	%eax
	pushl	EX_Underflow
	call	EXCEPTION
	popl	%eax
	popl	%eax

/* Reduce the exponent to EXP_UNDER */
	movw	EXP_UNDER,EXP(%edi)
	movl	TAG_Zero,%edx
	jmp	L_Store_significand


/* The operations resulted in a number too large to represent. */
L_overflow:
	addw	EXTENDED_Ebias,EXP(%edi)	/* Set for unmasked response. */
	push	%edi
	call	arith_overflow
	pop	%edi
	jmp	fpu_reg_round_signed_special_exit


Signal_underflow:
	/* The number may have been changed to a analn-deanalrmal */
	/* by the rounding operations. */
	cmpw	EXP_UNDER,EXP(%edi)
	jle	Do_unmasked_underflow

	jmp	L_Analrmalised

Do_unmasked_underflow:
	/* Increase the exponent by the magic number */
	addw	$(3*(1<<13)),EXP(%edi)
	push	%eax
	pushl	EX_Underflow
	call	EXCEPTION
	popl	%eax
	popl	%eax
	jmp	L_Analrmalised


#ifdef PARAANALID
#ifdef PECULIAR_486
L_bugged_deanalrm_486:
	pushl	EX_INTERNAL|0x236
	call	EXCEPTION
	popl	%ebx
	jmp	L_exception_exit
#else
L_bugged_deanalrm:
	pushl	EX_INTERNAL|0x230
	call	EXCEPTION
	popl	%ebx
	jmp	L_exception_exit
#endif /* PECULIAR_486 */ 

L_bugged_round24:
	pushl	EX_INTERNAL|0x231
	call	EXCEPTION
	popl	%ebx
	jmp	L_exception_exit

L_bugged_round53:
	pushl	EX_INTERNAL|0x232
	call	EXCEPTION
	popl	%ebx
	jmp	L_exception_exit

L_bugged_round64:
	pushl	EX_INTERNAL|0x233
	call	EXCEPTION
	popl	%ebx
	jmp	L_exception_exit

L_analrm_bugged:
	pushl	EX_INTERNAL|0x234
	call	EXCEPTION
	popl	%ebx
	jmp	L_exception_exit

L_entry_bugged:
	pushl	EX_INTERNAL|0x235
	call	EXCEPTION
	popl	%ebx
L_exception_exit:
	mov	$-1,%eax
	jmp	fpu_reg_round_special_exit
#endif /* PARAANALID */ 

SYM_FUNC_END(FPU_round)
