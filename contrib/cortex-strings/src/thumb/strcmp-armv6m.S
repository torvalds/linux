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

/* Implementation of strcmp for ARMv6m.  This version is only used in
   ARMv6-M when we want an efficient implementation.  Otherwize if the
   code size is preferred, strcmp-armv4t.S will be used.  */

	.thumb_func
	.syntax unified
	.arch	armv6-m

	.macro DoSub n, label
	subs	r0, r0, r1
#ifdef __ARM_BIG_ENDIAN
	lsrs	r1, r4, \n
#else
	lsls	r1, r4, \n
#endif
	orrs	r1, r0
	bne	\label
	.endm

	.macro Byte_Test n, label
	lsrs	r0, r2, \n
	lsrs	r1, r3, \n
	DoSub	\n, \label
	.endm

	.text
	.p2align	0
	.global	strcmp
	.type	strcmp, %function
strcmp:
	.cfi_startproc
	mov	r2, r0
	push	{r4, r5, r6, lr}
	orrs	r2, r1
	lsls	r2, r2, #30
	bne	6f
	ldr	r5, =0x01010101
	lsls	r6, r5, #7
1:
	ldmia	r0!, {r2}
	ldmia	r1!, {r3}
	subs	r4, r2, r5
	bics	r4, r2
	ands	r4, r6
	beq	3f

#ifdef __ARM_BIG_ENDIAN
	Byte_Test #24, 4f
	Byte_Test #16, 4f
	Byte_Test #8, 4f

	b       7f
3:
	cmp     r2, r3
	beq     1b
	cmp     r2, r3
#else
	uxtb    r0, r2
	uxtb    r1, r3
	DoSub   #24, 2f

	uxth    r0, r2
	uxth    r1, r3
	DoSub   #16, 2f

	lsls    r0, r2, #8
	lsls    r1, r3, #8
	lsrs    r0, r0, #8
	lsrs    r1, r1, #8
	DoSub   #8, 2f

	lsrs    r0, r2, #24
	lsrs    r1, r3, #24
	subs    r0, r0, r1
2:
	pop     {r4, r5, r6, pc}

3:
	cmp     r2, r3
	beq     1b
	rev     r0, r2
	rev     r1, r3
	cmp     r0, r1
#endif

	bls	5f
	movs	r0, #1
4:
	pop	{r4, r5, r6, pc}
5:
	movs	r0, #0
	mvns	r0, r0
	pop	{r4, r5, r6, pc}
6:
	ldrb	r2, [r0, #0]
	ldrb	r3, [r1, #0]
	adds	r0, #1
	adds	r1, #1
	cmp	r2, #0
	beq	7f
	cmp	r2, r3
	bne	7f
	ldrb	r2, [r0, #0]
	ldrb	r3, [r1, #0]
	adds	r0, #1
	adds	r1, #1
	cmp	r2, #0
	beq	7f
	cmp	r2, r3
	beq	6b
7:
	subs	r0, r2, r3
	pop	{r4, r5, r6, pc}
	.cfi_endproc
	.size	strcmp, . - strcmp
