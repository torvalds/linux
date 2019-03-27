/*
 * Copyright (c) 2014 ARM Ltd
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the company may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ARM LTD ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ARM LTD BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* An executable stack is *not* required for these functions.  */

.section .note.GNU-stack,"",%progbits
.previous
.eabi_attribute 25, 1

/* ANSI concatenation macros.  */

#define CONCAT1(a, b) CONCAT2(a, b)
#define CONCAT2(a, b) a ## b

/* Use the right prefix for global labels.  */

#define SYM(x) CONCAT1 (__USER_LABEL_PREFIX__, x)

#define TYPE(x) .type SYM(x),function
#define SIZE(x) .size SYM(x), . - SYM(x)
#define LSYM(x) .x

.macro cfi_start	start_label, end_label
	.pushsection	.debug_frame
LSYM(Lstart_frame):
	.4byte	LSYM(Lend_cie) - LSYM(Lstart_cie)
LSYM(Lstart_cie):
        .4byte	0xffffffff
        .byte	0x1
        .ascii	"\0"
        .uleb128 0x1
        .sleb128 -4
        .byte	0xe
        .byte	0xc
        .uleb128 0xd
        .uleb128 0x0

	.align 2
LSYM(Lend_cie):
	.4byte	LSYM(Lend_fde)-LSYM(Lstart_fde)
LSYM(Lstart_fde):
	.4byte	LSYM(Lstart_frame)
	.4byte	\start_label
	.4byte	\end_label-\start_label
	.popsection
.endm

.macro cfi_end	end_label
	.pushsection	.debug_frame
	.align	2
LSYM(Lend_fde):
	.popsection
\end_label:
.endm

.macro THUMB_LDIV0 name signed
	push	{r0, lr}
	movs	r0, #0
	bl	SYM(__aeabi_idiv0)
	pop	{r1, pc}
.endm

.macro FUNC_END name
	SIZE (__\name)
.endm

.macro DIV_FUNC_END name signed
	cfi_start	__\name, LSYM(Lend_div0)
LSYM(Ldiv0):
	THUMB_LDIV0 \name \signed
	cfi_end	LSYM(Lend_div0)
	FUNC_END \name
.endm

.macro THUMB_FUNC_START name
	.globl	SYM (\name)
	TYPE	(\name)
	.thumb_func
SYM (\name):
.endm

.macro FUNC_START name
	.text
	.globl SYM (__\name)
	TYPE (__\name)
	.align 0
	.force_thumb
	.thumb_func
	.syntax unified
SYM (__\name):
.endm

.macro	FUNC_ALIAS new old
	.globl	SYM (__\new)
	.thumb_set	SYM (__\new), SYM (__\old)
.endm

/* Register aliases.  */
work		.req	r4
dividend	.req	r0
divisor		.req	r1
overdone	.req	r2
result		.req	r2
curbit		.req	r3

/* ------------------------------------------------------------------------ */
/*		Bodies of the division and modulo routines.		    */
/* ------------------------------------------------------------------------ */
.macro BranchToDiv n, label
	lsrs	curbit, dividend, \n
	cmp	curbit, divisor
	bcc	\label
.endm

.macro DoDiv n
	lsrs	curbit, dividend, \n
	cmp	curbit, divisor
	bcc	1f
	lsls	curbit, divisor, \n
	subs	dividend, dividend, curbit

1:	adcs	result, result
.endm

.macro THUMB1_Div_Positive
	movs	result, #0
	BranchToDiv #1, LSYM(Lthumb1_div1)
	BranchToDiv #4, LSYM(Lthumb1_div4)
	BranchToDiv #8, LSYM(Lthumb1_div8)
	BranchToDiv #12, LSYM(Lthumb1_div12)
	BranchToDiv #16, LSYM(Lthumb1_div16)
LSYM(Lthumb1_div_large_positive):
	movs	result, #0xff
	lsls	divisor, divisor, #8
	rev	result, result
	lsrs	curbit, dividend, #16
	cmp	curbit, divisor
	bcc	1f
	asrs	result, #8
	lsls	divisor, divisor, #8
	beq	LSYM(Ldivbyzero_waypoint)

1:	lsrs	curbit, dividend, #12
	cmp	curbit, divisor
	bcc	LSYM(Lthumb1_div12)
	b	LSYM(Lthumb1_div16)
LSYM(Lthumb1_div_loop):
	lsrs	divisor, divisor, #8
LSYM(Lthumb1_div16):
	Dodiv	#15
	Dodiv	#14
	Dodiv	#13
	Dodiv	#12
LSYM(Lthumb1_div12):
	Dodiv	#11
	Dodiv	#10
	Dodiv	#9
	Dodiv	#8
	bcs	LSYM(Lthumb1_div_loop)
LSYM(Lthumb1_div8):
	Dodiv	#7
	Dodiv	#6
	Dodiv	#5
LSYM(Lthumb1_div5):
	Dodiv	#4
LSYM(Lthumb1_div4):
	Dodiv	#3
LSYM(Lthumb1_div3):
	Dodiv	#2
LSYM(Lthumb1_div2):
	Dodiv	#1
LSYM(Lthumb1_div1):
	subs	divisor, dividend, divisor
	bcs	1f
	mov	divisor, dividend

1:	adcs	result, result
	mov	dividend, result
	bx	lr

LSYM(Ldivbyzero_waypoint):
	b	LSYM(Ldiv0)
.endm

.macro THUMB1_Div_Negative
	lsrs	result, divisor, #31
	beq	1f
	rsbs	divisor, divisor, #0

1:	asrs	curbit, dividend, #32
	bcc	2f
	rsbs	dividend, dividend, #0

2:	eors	curbit, result
	movs	result, #0
	mov	ip, curbit
	BranchToDiv #4, LSYM(Lthumb1_div_negative4)
	BranchToDiv #8, LSYM(Lthumb1_div_negative8)
LSYM(Lthumb1_div_large):
	movs	result, #0xfc
	lsls	divisor, divisor, #6
	rev	result, result
	lsrs	curbit, dividend, #8
	cmp	curbit, divisor
	bcc	LSYM(Lthumb1_div_negative8)

	lsls	divisor, divisor, #6
	asrs	result, result, #6
	cmp	curbit, divisor
	bcc	LSYM(Lthumb1_div_negative8)

	lsls	divisor, divisor, #6
	asrs	result, result, #6
	cmp	curbit, divisor
	bcc	LSYM(Lthumb1_div_negative8)

	lsls	divisor, divisor, #6
	beq	LSYM(Ldivbyzero_negative)
	asrs	result, result, #6
	b	LSYM(Lthumb1_div_negative8)
LSYM(Lthumb1_div_negative_loop):
	lsrs	divisor, divisor, #6
LSYM(Lthumb1_div_negative8):
	DoDiv	#7
	DoDiv	#6
	DoDiv	#5
	DoDiv	#4
LSYM(Lthumb1_div_negative4):
	DoDiv	#3
	DoDiv	#2
	bcs	LSYM(Lthumb1_div_negative_loop)
	DoDiv	#1
	subs	divisor, dividend, divisor
	bcs	1f
	mov	divisor, dividend

1:	mov	curbit, ip
	adcs	result, result
	asrs	curbit, curbit, #1
	mov	dividend, result
	bcc	2f
	rsbs	dividend, dividend, #0
	cmp	curbit, #0

2:	bpl	3f
	rsbs	divisor, divisor, #0

3:	bx	lr

LSYM(Ldivbyzero_negative):
	mov	curbit, ip
	asrs	curbit, curbit, #1
	bcc	LSYM(Ldiv0)
	rsbs	dividend, dividend, #0
.endm

/* ------------------------------------------------------------------------ */
/*		Start of the Real Functions				    */
/* ------------------------------------------------------------------------ */

	FUNC_START aeabi_idiv0
	bx	lr
	FUNC_END aeabi_idiv0

	FUNC_START divsi3
	FUNC_ALIAS aeabi_idiv divsi3

LSYM(divsi3_skip_div0_test):
	mov	curbit, dividend
	orrs	curbit, divisor
	bmi	LSYM(Lthumb1_div_negative)

LSYM(Lthumb1_div_positive):
	THUMB1_Div_Positive

LSYM(Lthumb1_div_negative):
	THUMB1_Div_Negative

	DIV_FUNC_END divsi3 signed

	FUNC_START aeabi_idivmod

	cmp	r1, #0
	beq	LSYM(Ldiv0)
	push	{r0, r1, lr}
	bl	LSYM(divsi3_skip_div0_test)
	POP	{r1, r2, r3}
	mul	r2, r0
	sub	r1, r1, r2
	bx	r3

	FUNC_END aeabi_idivmod
/* ------------------------------------------------------------------------ */
