/*
 * atomic64_t for 586+
 *
 * Copyright © 2010  Luca Barbieri
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/linkage.h>
#include <asm/alternative-asm.h>
#include <asm/dwarf2.h>

.macro SAVE reg
	pushl_cfi %\reg
	CFI_REL_OFFSET \reg, 0
.endm

.macro RESTORE reg
	popl_cfi %\reg
	CFI_RESTORE \reg
.endm

.macro read64 reg
	movl %ebx, %eax
	movl %ecx, %edx
/* we need LOCK_PREFIX since otherwise cmpxchg8b always does the write */
	LOCK_PREFIX
	cmpxchg8b (\reg)
.endm

ENTRY(atomic64_read_cx8)
	CFI_STARTPROC

	read64 %ecx
	ret
	CFI_ENDPROC
ENDPROC(atomic64_read_cx8)

ENTRY(atomic64_set_cx8)
	CFI_STARTPROC

1:
/* we don't need LOCK_PREFIX since aligned 64-bit writes
 * are atomic on 586 and newer */
	cmpxchg8b (%esi)
	jne 1b

	ret
	CFI_ENDPROC
ENDPROC(atomic64_set_cx8)

ENTRY(atomic64_xchg_cx8)
	CFI_STARTPROC

1:
	LOCK_PREFIX
	cmpxchg8b (%esi)
	jne 1b

	ret
	CFI_ENDPROC
ENDPROC(atomic64_xchg_cx8)

.macro addsub_return func ins insc
ENTRY(atomic64_\func\()_return_cx8)
	CFI_STARTPROC
	SAVE ebp
	SAVE ebx
	SAVE esi
	SAVE edi

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
	RESTORE edi
	RESTORE esi
	RESTORE ebx
	RESTORE ebp
	ret
	CFI_ENDPROC
ENDPROC(atomic64_\func\()_return_cx8)
.endm

addsub_return add add adc
addsub_return sub sub sbb

.macro incdec_return func ins insc
ENTRY(atomic64_\func\()_return_cx8)
	CFI_STARTPROC
	SAVE ebx

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
	RESTORE ebx
	ret
	CFI_ENDPROC
ENDPROC(atomic64_\func\()_return_cx8)
.endm

incdec_return inc add adc
incdec_return dec sub sbb

ENTRY(atomic64_dec_if_positive_cx8)
	CFI_STARTPROC
	SAVE ebx

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
	RESTORE ebx
	ret
	CFI_ENDPROC
ENDPROC(atomic64_dec_if_positive_cx8)

ENTRY(atomic64_add_unless_cx8)
	CFI_STARTPROC
	SAVE ebp
	SAVE ebx
/* these just push these two parameters on the stack */
	SAVE edi
	SAVE ecx

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
	CFI_ADJUST_CFA_OFFSET -8
	RESTORE ebx
	RESTORE ebp
	ret
4:
	cmpl %edx, 4(%esp)
	jne 2b
	xorl %eax, %eax
	jmp 3b
	CFI_ENDPROC
ENDPROC(atomic64_add_unless_cx8)

ENTRY(atomic64_inc_not_zero_cx8)
	CFI_STARTPROC
	SAVE ebx

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
	RESTORE ebx
	ret
	CFI_ENDPROC
ENDPROC(atomic64_inc_not_zero_cx8)
