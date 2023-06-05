/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Vitaly Mayatskikh <vmayatsk@redhat.com>
 * Copyright 2002 Andi Kleen, SuSE Labs.
 *
 * Functions to copy from and to user space.
 */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/export.h>

/*
 * rep_movs_alternative - memory copy with exception handling.
 * This version is for CPUs that don't have FSRM (Fast Short Rep Movs)
 *
 * Input:
 * rdi destination
 * rsi source
 * rcx count
 *
 * Output:
 * rcx uncopied bytes or 0 if successful.
 *
 * NOTE! The calling convention is very intentionally the same as
 * for 'rep movs', so that we can rewrite the function call with
 * just a plain 'rep movs' on machines that have FSRM.  But to make
 * it simpler for us, we can clobber rsi/rdi and rax/r8-r11 freely.
 */
SYM_FUNC_START(rep_movs_alternative)
	cmpq $64,%rcx
	jae .Lunrolled

	cmp $8,%ecx
	jae .Lword

	testl %ecx,%ecx
	je .Lexit

.Lcopy_user_tail:
0:	movb (%rsi),%al
1:	movb %al,(%rdi)
	inc %rdi
	inc %rsi
	dec %rcx
	jne .Lcopy_user_tail
.Lexit:
	RET

	_ASM_EXTABLE_UA( 0b, .Lexit)
	_ASM_EXTABLE_UA( 1b, .Lexit)

	.p2align 4
.Lword:
2:	movq (%rsi),%rax
3:	movq %rax,(%rdi)
	addq $8,%rsi
	addq $8,%rdi
	sub $8,%ecx
	je .Lexit
	cmp $8,%ecx
	jae .Lword
	jmp .Lcopy_user_tail

	_ASM_EXTABLE_UA( 2b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA( 3b, .Lcopy_user_tail)

	.p2align 4
.Lunrolled:
10:	movq (%rsi),%r8
11:	movq 8(%rsi),%r9
12:	movq 16(%rsi),%r10
13:	movq 24(%rsi),%r11
14:	movq %r8,(%rdi)
15:	movq %r9,8(%rdi)
16:	movq %r10,16(%rdi)
17:	movq %r11,24(%rdi)
20:	movq 32(%rsi),%r8
21:	movq 40(%rsi),%r9
22:	movq 48(%rsi),%r10
23:	movq 56(%rsi),%r11
24:	movq %r8,32(%rdi)
25:	movq %r9,40(%rdi)
26:	movq %r10,48(%rdi)
27:	movq %r11,56(%rdi)
	addq $64,%rsi
	addq $64,%rdi
	subq $64,%rcx
	cmpq $64,%rcx
	jae .Lunrolled
	cmpl $8,%ecx
	jae .Lword
	testl %ecx,%ecx
	jne .Lcopy_user_tail
	RET

	_ASM_EXTABLE_UA(10b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(11b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(12b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(13b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(14b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(15b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(16b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(17b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(20b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(21b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(22b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(23b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(24b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(25b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(26b, .Lcopy_user_tail)
	_ASM_EXTABLE_UA(27b, .Lcopy_user_tail)
SYM_FUNC_END(rep_movs_alternative)
EXPORT_SYMBOL(rep_movs_alternative)
