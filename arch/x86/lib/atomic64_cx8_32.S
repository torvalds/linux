/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * atomic64_t for 586+
 *
 * Copyright © 2010  Luca Barbieri
 */

#include <linux/linkage.h>
#include <asm/alternative.h>

.macro read64 reg
	movl %ebx, %eax
	movl %ecx, %edx
/* we need LOCK_PREFIX since otherwise cmpxchg8b always does the write */
	LOCK_PREFIX
	cmpxchg8b (\reg)
.endm

SYM_FUNC_START(atomic64_read_cx8)
	read64 %ecx
	ret
SYM_FUNC_END(atomic64_read_cx8)

SYM_FUNC_START(atomic64_set_cx8)
1:
/* we don't need LOCK_PREFIX since aligned 64-bit writes
 * are atomic on 586 and newer */
	cmpxchg8b (%esi)
	jne 1b

	ret
SYM_FUNC_END(atomic64_set_cx8)

SYM_FUNC_START(atomic64_xchg_cx8)
1:
	LOCK_PREFIX
	cmpxchg8b (%esi)
	jne 1b

	ret
SYM_FUNC_END(atomic64_xchg_cx8)

.macro addsub_return func ins insc
SYM_FUNC_START(atomic64_\func\()_return_cx8)
	pushl %ebp
	pushl %ebx
	pushl %esi
	pushl %edi

	movl %eax, %esi
	movl %edx, %edi
	movl %ecx, %ebp

	read64 %ecx
1:
	movl %eax, %ebx
	movl %edx, %ecx
	\ins\()l %esi, %ebx
	\insc\()l %edi, %ecx
	LOCK_PREFIX
	cmpxchg8b (%ebp)
	jne 1b

10:
	movl %ebx, %eax
	movl %ecx, %edx
	popl %edi
	popl %esi
	popl %ebx
	popl %ebp
	ret
SYM_FUNC_END(atomic64_\func\()_return_cx8)
.endm

addsub_return add add adc
addsub_return sub sub sbb

.macro incdec_return func ins insc
SYM_FUNC_START(atomic64_\func\()_return_cx8)
	pushl %ebx

	read64 %esi
1:
	movl %eax, %ebx
	movl %edx, %ecx
	\ins\()l $1, %ebx
	\insc\()l $0, %ecx
	LOCK_PREFIX
	cmpxchg8b (%esi)
	jne 1b

10:
	movl %ebx, %eax
	movl %ecx, %edx
	popl %ebx
	ret
SYM_FUNC_END(atomic64_\func\()_return_cx8)
.endm

incdec_return inc add adc
incdec_return dec sub sbb

SYM_FUNC_START(atomic64_dec_if_positive_cx8)
	pushl %ebx

	read64 %esi
1:
	movl %eax, %ebx
	movl %edx, %ecx
	subl $1, %ebx
	sbb $0, %ecx
	js 2f
	LOCK_PREFIX
	cmpxchg8b (%esi)
	jne 1b

2:
	movl %ebx, %eax
	movl %ecx, %edx
	popl %ebx
	ret
SYM_FUNC_END(atomic64_dec_if_positive_cx8)

SYM_FUNC_START(atomic64_add_unless_cx8)
	pushl %ebp
	pushl %ebx
/* these just push these two parameters on the stack */
	pushl %edi
	pushl %ecx

	movl %eax, %ebp
	movl %edx, %edi

	read64 %esi
1:
	cmpl %eax, 0(%esp)
	je 4f
2:
	movl %eax, %ebx
	movl %edx, %ecx
	addl %ebp, %ebx
	adcl %edi, %ecx
	LOCK_PREFIX
	cmpxchg8b (%esi)
	jne 1b

	movl $1, %eax
3:
	addl $8, %esp
	popl %ebx
	popl %ebp
	ret
4:
	cmpl %edx, 4(%esp)
	jne 2b
	xorl %eax, %eax
	jmp 3b
SYM_FUNC_END(atomic64_add_unless_cx8)

SYM_FUNC_START(atomic64_inc_not_zero_cx8)
	pushl %ebx

	read64 %esi
1:
	movl %eax, %ecx
	orl %edx, %ecx
	jz 3f
	movl %eax, %ebx
	xorl %ecx, %ecx
	addl $1, %ebx
	adcl %edx, %ecx
	LOCK_PREFIX
	cmpxchg8b (%esi)
	jne 1b

	movl $1, %eax
3:
	popl %ebx
	ret
SYM_FUNC_END(atomic64_inc_not_zero_cx8)
