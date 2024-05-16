/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Vitaly Mayatskikh <vmayatsk@redhat.com>
 * Copyright 2002 Andi Kleen, SuSE Labs.
 *
 * Functions to copy from and to user space.
 */

#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>
#include <asm/asm.h>

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
 * it simpler for us, we can clobber rsi/rdi and rax freely.
 */
SYM_FUNC_START(rep_movs_alternative)
	cmpq $64,%rcx
	jae .Llarge

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

.Llarge:
0:	ALTERNATIVE "jmp .Llarge_movsq", "rep movsb", X86_FEATURE_ERMS
1:	RET

	_ASM_EXTABLE_UA( 0b, 1b)

.Llarge_movsq:
	movq %rcx,%rax
	shrq $3,%rcx
	andl $7,%eax
0:	rep movsq
	movl %eax,%ecx
	testl %ecx,%ecx
	jne .Lcopy_user_tail
	RET

1:	leaq (%rax,%rcx,8),%rcx
	jmp .Lcopy_user_tail

	_ASM_EXTABLE_UA( 0b, 1b)
SYM_FUNC_END(rep_movs_alternative)
EXPORT_SYMBOL(rep_movs_alternative)
