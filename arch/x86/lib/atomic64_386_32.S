/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * atomic64_t for 386/486
 *
 * Copyright © 2010  Luca Barbieri
 */

#include <linux/linkage.h>
#include <asm/alternative-asm.h>

/* if you want SMP support, implement these with real spinlocks */
.macro LOCK reg
	pushfl
	cli
.endm

.macro UNLOCK reg
	popfl
.endm

#define BEGIN(op) \
.macro endp; \
ENDPROC(atomic64_##op##_386); \
.purgem endp; \
.endm; \
ENTRY(atomic64_##op##_386); \
	LOCK v;

#define ENDP endp

#define RET \
	UNLOCK v; \
	ret

#define RET_ENDP \
	RET; \
	ENDP

#define v %ecx
BEGIN(read)
	movl  (v), %eax
	movl 4(v), %edx
RET_ENDP
#undef v

#define v %esi
BEGIN(set)
	movl %ebx,  (v)
	movl %ecx, 4(v)
RET_ENDP
#undef v

#define v  %esi
BEGIN(xchg)
	movl  (v), %eax
	movl 4(v), %edx
	movl %ebx,  (v)
	movl %ecx, 4(v)
RET_ENDP
#undef v

#define v %ecx
BEGIN(add)
	addl %eax,  (v)
	adcl %edx, 4(v)
RET_ENDP
#undef v

#define v %ecx
BEGIN(add_return)
	addl  (v), %eax
	adcl 4(v), %edx
	movl %eax,  (v)
	movl %edx, 4(v)
RET_ENDP
#undef v

#define v %ecx
BEGIN(sub)
	subl %eax,  (v)
	sbbl %edx, 4(v)
RET_ENDP
#undef v

#define v %ecx
BEGIN(sub_return)
	negl %edx
	negl %eax
	sbbl $0, %edx
	addl  (v), %eax
	adcl 4(v), %edx
	movl %eax,  (v)
	movl %edx, 4(v)
RET_ENDP
#undef v

#define v %esi
BEGIN(inc)
	addl $1,  (v)
	adcl $0, 4(v)
RET_ENDP
#undef v

#define v %esi
BEGIN(inc_return)
	movl  (v), %eax
	movl 4(v), %edx
	addl $1, %eax
	adcl $0, %edx
	movl %eax,  (v)
	movl %edx, 4(v)
RET_ENDP
#undef v

#define v %esi
BEGIN(dec)
	subl $1,  (v)
	sbbl $0, 4(v)
RET_ENDP
#undef v

#define v %esi
BEGIN(dec_return)
	movl  (v), %eax
	movl 4(v), %edx
	subl $1, %eax
	sbbl $0, %edx
	movl %eax,  (v)
	movl %edx, 4(v)
RET_ENDP
#undef v

#define v %esi
BEGIN(add_unless)
	addl %eax, %ecx
	adcl %edx, %edi
	addl  (v), %eax
	adcl 4(v), %edx
	cmpl %eax, %ecx
	je 3f
1:
	movl %eax,  (v)
	movl %edx, 4(v)
	movl $1, %eax
2:
	RET
3:
	cmpl %edx, %edi
	jne 1b
	xorl %eax, %eax
	jmp 2b
ENDP
#undef v

#define v %esi
BEGIN(inc_not_zero)
	movl  (v), %eax
	movl 4(v), %edx
	testl %eax, %eax
	je 3f
1:
	addl $1, %eax
	adcl $0, %edx
	movl %eax,  (v)
	movl %edx, 4(v)
	movl $1, %eax
2:
	RET
3:
	testl %edx, %edx
	jne 1b
	jmp 2b
ENDP
#undef v

#define v %esi
BEGIN(dec_if_positive)
	movl  (v), %eax
	movl 4(v), %edx
	subl $1, %eax
	sbbl $0, %edx
	js 1f
	movl %eax,  (v)
	movl %edx, 4(v)
1:
RET_ENDP
#undef v
