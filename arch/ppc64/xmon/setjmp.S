/*
 * Copyright (C) 1996 Paul Mackerras.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * NOTE: assert(sizeof(buf) > 184)
 */
#include <asm/processor.h>
#include <asm/ppc_asm.h>

_GLOBAL(xmon_setjmp)
	mflr    r0
	std     r0,0(r3)
	std     r1,8(r3)
	std     r2,16(r3)
	mfcr    r0
	std     r0,24(r3)
	std     r13,32(r3)
	std     r14,40(r3)
	std     r15,48(r3)
	std     r16,56(r3)
	std     r17,64(r3)
	std     r18,72(r3)
	std     r19,80(r3)
	std     r20,88(r3)
	std     r21,96(r3)
	std     r22,104(r3)
	std     r23,112(r3)
	std     r24,120(r3)
	std     r25,128(r3)
	std     r26,136(r3)
	std     r27,144(r3)
	std     r28,152(r3)
	std     r29,160(r3)
	std     r30,168(r3)
	std     r31,176(r3)
	li      r3,0
	blr

_GLOBAL(xmon_longjmp)
	cmpdi   r4,0
	bne     1f
	li      r4,1
1:	ld      r13,32(r3)
	ld      r14,40(r3)
	ld      r15,48(r3)
	ld      r16,56(r3)
	ld      r17,64(r3)
	ld      r18,72(r3)
	ld      r19,80(r3)
	ld      r20,88(r3)
	ld      r21,96(r3)
	ld      r22,104(r3)
	ld      r23,112(r3)
	ld      r24,120(r3)
	ld      r25,128(r3)
	ld      r26,136(r3)
	ld      r27,144(r3)
	ld      r28,152(r3)
	ld      r29,160(r3)
	ld      r30,168(r3)
	ld      r31,176(r3)
	ld      r0,24(r3)
	mtcrf   56,r0
	ld      r0,0(r3)
	ld      r1,8(r3)
	ld      r2,16(r3)
	mtlr    r0
	mr      r3,r4
	blr
