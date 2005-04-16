@ libgcc1 routines for ARM cpu.
@ Division routines, written by Richard Earnshaw, (rearnsha@armltd.co.uk)

/* Copyright (C) 1995, 1996, 1998 Free Software Foundation, Inc.

This file is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file with other programs, and to distribute
those programs without any restriction coming from the use of this
file.  (The General Public License restrictions do apply in other
respects; for example, they cover modification of the file, and
distribution when not linked into another program.)

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.
 */
/* This code is derived from gcc 2.95.3 */
/* I Molton     29/07/01 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/hardware.h>
#include <linux/config.h>

#define RET	movs
#define RETc(x)	mov##x##s
#define RETCOND ^

dividend	.req	r0
divisor		.req	r1
result		.req	r2
overdone        .req    r2
curbit		.req	r3
ip		.req	r12
sp		.req	r13
lr		.req	r14
pc		.req	r15
	
ENTRY(__udivsi3)
	cmp	divisor, #0
	beq	Ldiv0
	mov	curbit, #1
	mov	result, #0
	cmp	dividend, divisor
	bcc	Lgot_result_udivsi3
1:
	@ Unless the divisor is very big, shift it up in multiples of
	@ four bits, since this is the amount of unwinding in the main
	@ division loop.  Continue shifting until the divisor is 
	@ larger than the dividend.
	cmp	divisor, #0x10000000
	cmpcc	divisor, dividend
	movcc	divisor, divisor, lsl #4
	movcc	curbit, curbit, lsl #4
	bcc	1b

2:
	@ For very big divisors, we must shift it a bit at a time, or
	@ we will be in danger of overflowing.
	cmp	divisor, #0x80000000
	cmpcc	divisor, dividend
	movcc	divisor, divisor, lsl #1
	movcc	curbit, curbit, lsl #1
	bcc	2b

3:
	@ Test for possible subtractions, and note which bits
	@ are done in the result.  On the final pass, this may subtract
	@ too much from the dividend, but the result will be ok, since the
	@ "bit" will have been shifted out at the bottom.
	cmp	dividend, divisor
	subcs	dividend, dividend, divisor
	orrcs	result, result, curbit
	cmp	dividend, divisor, lsr #1
	subcs	dividend, dividend, divisor, lsr #1
	orrcs	result, result, curbit, lsr #1
	cmp	dividend, divisor, lsr #2
	subcs	dividend, dividend, divisor, lsr #2
	orrcs	result, result, curbit, lsr #2
	cmp	dividend, divisor, lsr #3
	subcs	dividend, dividend, divisor, lsr #3
	orrcs	result, result, curbit, lsr #3
	cmp	dividend, #0			@ Early termination?
	movnes	curbit, curbit, lsr #4		@ No, any more bits to do?
	movne	divisor, divisor, lsr #4
	bne	3b
Lgot_result_udivsi3:
	mov	r0, result
	RET	pc, lr

Ldiv0:
	str	lr, [sp, #-4]!
	bl	__div0
	mov	r0, #0			@ about as wrong as it could be
	ldmia	sp!, {pc}RETCOND

/* __umodsi3 ----------------------- */

ENTRY(__umodsi3)
	cmp	divisor, #0
	beq	Ldiv0
	mov	curbit, #1
	cmp	dividend, divisor
	RETc(cc)	pc, lr
1:
	@ Unless the divisor is very big, shift it up in multiples of
	@ four bits, since this is the amount of unwinding in the main
	@ division loop.  Continue shifting until the divisor is 
	@ larger than the dividend.
	cmp	divisor, #0x10000000
	cmpcc	divisor, dividend
	movcc	divisor, divisor, lsl #4
	movcc	curbit, curbit, lsl #4
	bcc	1b

2:
	@ For very big divisors, we must shift it a bit at a time, or
	@ we will be in danger of overflowing.
	cmp	divisor, #0x80000000
	cmpcc	divisor, dividend
	movcc	divisor, divisor, lsl #1
	movcc	curbit, curbit, lsl #1
	bcc	2b

3:
	@ Test for possible subtractions.  On the final pass, this may 
	@ subtract too much from the dividend, so keep track of which
	@ subtractions are done, we can fix them up afterwards...
	mov	overdone, #0
	cmp	dividend, divisor
	subcs	dividend, dividend, divisor
	cmp	dividend, divisor, lsr #1
	subcs	dividend, dividend, divisor, lsr #1
	orrcs	overdone, overdone, curbit, ror #1
	cmp	dividend, divisor, lsr #2
	subcs	dividend, dividend, divisor, lsr #2
	orrcs	overdone, overdone, curbit, ror #2
	cmp	dividend, divisor, lsr #3
	subcs	dividend, dividend, divisor, lsr #3
	orrcs	overdone, overdone, curbit, ror #3
	mov	ip, curbit
	cmp	dividend, #0			@ Early termination?
	movnes	curbit, curbit, lsr #4		@ No, any more bits to do?
	movne	divisor, divisor, lsr #4
	bne	3b

	@ Any subtractions that we should not have done will be recorded in
	@ the top three bits of "overdone".  Exactly which were not needed
	@ are governed by the position of the bit, stored in ip.
	@ If we terminated early, because dividend became zero,
	@ then none of the below will match, since the bit in ip will not be
	@ in the bottom nibble.
	ands	overdone, overdone, #0xe0000000
	RETc(eq)	pc, lr				@ No fixups needed
	tst	overdone, ip, ror #3
	addne	dividend, dividend, divisor, lsr #3
	tst	overdone, ip, ror #2
	addne	dividend, dividend, divisor, lsr #2
	tst	overdone, ip, ror #1
	addne	dividend, dividend, divisor, lsr #1
	RET	pc, lr

ENTRY(__divsi3)
	eor	ip, dividend, divisor		@ Save the sign of the result.
	mov	curbit, #1
	mov	result, #0
	cmp	divisor, #0
	rsbmi	divisor, divisor, #0		@ Loops below use unsigned.
	beq	Ldiv0
	cmp	dividend, #0
	rsbmi	dividend, dividend, #0
	cmp	dividend, divisor
	bcc	Lgot_result_divsi3

1:
	@ Unless the divisor is very big, shift it up in multiples of
	@ four bits, since this is the amount of unwinding in the main
	@ division loop.  Continue shifting until the divisor is 
	@ larger than the dividend.
	cmp	divisor, #0x10000000
	cmpcc	divisor, dividend
	movcc	divisor, divisor, lsl #4
	movcc	curbit, curbit, lsl #4
	bcc	1b

2:
	@ For very big divisors, we must shift it a bit at a time, or
	@ we will be in danger of overflowing.
	cmp	divisor, #0x80000000
	cmpcc	divisor, dividend
	movcc	divisor, divisor, lsl #1
	movcc	curbit, curbit, lsl #1
	bcc	2b

3:
	@ Test for possible subtractions, and note which bits
	@ are done in the result.  On the final pass, this may subtract
	@ too much from the dividend, but the result will be ok, since the
	@ "bit" will have been shifted out at the bottom.
	cmp	dividend, divisor
	subcs	dividend, dividend, divisor
	orrcs	result, result, curbit
	cmp	dividend, divisor, lsr #1
	subcs	dividend, dividend, divisor, lsr #1
	orrcs	result, result, curbit, lsr #1
	cmp	dividend, divisor, lsr #2
	subcs	dividend, dividend, divisor, lsr #2
	orrcs	result, result, curbit, lsr #2
	cmp	dividend, divisor, lsr #3
	subcs	dividend, dividend, divisor, lsr #3
	orrcs	result, result, curbit, lsr #3
	cmp	dividend, #0			@ Early termination?
	movnes	curbit, curbit, lsr #4		@ No, any more bits to do?
	movne	divisor, divisor, lsr #4
	bne	3b
Lgot_result_divsi3:
	mov	r0, result
	cmp	ip, #0
	rsbmi	r0, r0, #0
	RET	pc, lr

ENTRY(__modsi3)
	mov	curbit, #1
	cmp	divisor, #0
	rsbmi	divisor, divisor, #0		@ Loops below use unsigned.
	beq	Ldiv0
	@ Need to save the sign of the dividend, unfortunately, we need
	@ ip later on; this is faster than pushing lr and using that.
	str	dividend, [sp, #-4]!
	cmp	dividend, #0
	rsbmi	dividend, dividend, #0
	cmp	dividend, divisor
	bcc	Lgot_result_modsi3

1:
	@ Unless the divisor is very big, shift it up in multiples of
	@ four bits, since this is the amount of unwinding in the main
	@ division loop.  Continue shifting until the divisor is 
	@ larger than the dividend.
	cmp	divisor, #0x10000000
	cmpcc	divisor, dividend
	movcc	divisor, divisor, lsl #4
	movcc	curbit, curbit, lsl #4
	bcc	1b

2:
	@ For very big divisors, we must shift it a bit at a time, or
	@ we will be in danger of overflowing.
	cmp	divisor, #0x80000000
	cmpcc	divisor, dividend
	movcc	divisor, divisor, lsl #1
	movcc	curbit, curbit, lsl #1
	bcc	2b

3:
	@ Test for possible subtractions.  On the final pass, this may 
	@ subtract too much from the dividend, so keep track of which
	@ subtractions are done, we can fix them up afterwards...
	mov	overdone, #0
	cmp	dividend, divisor
	subcs	dividend, dividend, divisor
	cmp	dividend, divisor, lsr #1
	subcs	dividend, dividend, divisor, lsr #1
	orrcs	overdone, overdone, curbit, ror #1
	cmp	dividend, divisor, lsr #2
	subcs	dividend, dividend, divisor, lsr #2
	orrcs	overdone, overdone, curbit, ror #2
	cmp	dividend, divisor, lsr #3
	subcs	dividend, dividend, divisor, lsr #3
	orrcs	overdone, overdone, curbit, ror #3
	mov	ip, curbit
	cmp	dividend, #0			@ Early termination?
	movnes	curbit, curbit, lsr #4		@ No, any more bits to do?
	movne	divisor, divisor, lsr #4
	bne	3b

	@ Any subtractions that we should not have done will be recorded in
	@ the top three bits of "overdone".  Exactly which were not needed
	@ are governed by the position of the bit, stored in ip.
	@ If we terminated early, because dividend became zero,
	@ then none of the below will match, since the bit in ip will not be
	@ in the bottom nibble.
	ands	overdone, overdone, #0xe0000000
	beq	Lgot_result_modsi3
	tst	overdone, ip, ror #3
	addne	dividend, dividend, divisor, lsr #3
	tst	overdone, ip, ror #2
	addne	dividend, dividend, divisor, lsr #2
	tst	overdone, ip, ror #1
	addne	dividend, dividend, divisor, lsr #1
Lgot_result_modsi3:
	ldr	ip, [sp], #4
	cmp	ip, #0
	rsbmi	dividend, dividend, #0
	RET	pc, lr
