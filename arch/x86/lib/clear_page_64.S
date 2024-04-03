/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/asm.h>

/*
 * Most CPUs support enhanced REP MOVSB/STOSB instructions. It is
 * recommended to use this when possible and we do use them by default.
 * If enhanced REP MOVSB/STOSB is not available, try to use fast string.
 * Otherwise, use original.
 */

/*
 * Zero a page.
 * %rdi	- page
 */
SYM_FUNC_START(clear_page_rep)
	movl $4096/8,%ecx
	xorl %eax,%eax
	rep stosq
	RET
SYM_FUNC_END(clear_page_rep)
EXPORT_SYMBOL_GPL(clear_page_rep)

SYM_FUNC_START(clear_page_orig)
	xorl   %eax,%eax
	movl   $4096/64,%ecx
	.p2align 4
.Lloop:
	decl	%ecx
#define PUT(x) movq %rax,x*8(%rdi)
	movq %rax,(%rdi)
	PUT(1)
	PUT(2)
	PUT(3)
	PUT(4)
	PUT(5)
	PUT(6)
	PUT(7)
	leaq	64(%rdi),%rdi
	jnz	.Lloop
	nop
	RET
SYM_FUNC_END(clear_page_orig)
EXPORT_SYMBOL_GPL(clear_page_orig)

SYM_FUNC_START(clear_page_erms)
	movl $4096,%ecx
	xorl %eax,%eax
	rep stosb
	RET
SYM_FUNC_END(clear_page_erms)
EXPORT_SYMBOL_GPL(clear_page_erms)

/*
 * Default clear user-space.
 * Input:
 * rdi destination
 * rcx count
 * rax is zero
 *
 * Output:
 * rcx: uncleared bytes or 0 if successful.
 */
SYM_FUNC_START(rep_stos_alternative)
	cmpq $64,%rcx
	jae .Lunrolled

	cmp $8,%ecx
	jae .Lword

	testl %ecx,%ecx
	je .Lexit

.Lclear_user_tail:
0:	movb %al,(%rdi)
	inc %rdi
	dec %rcx
	jnz .Lclear_user_tail
.Lexit:
	RET

	_ASM_EXTABLE_UA( 0b, .Lexit)

.Lword:
1:	movq %rax,(%rdi)
	addq $8,%rdi
	sub $8,%ecx
	je .Lexit
	cmp $8,%ecx
	jae .Lword
	jmp .Lclear_user_tail

	.p2align 4
.Lunrolled:
10:	movq %rax,(%rdi)
11:	movq %rax,8(%rdi)
12:	movq %rax,16(%rdi)
13:	movq %rax,24(%rdi)
14:	movq %rax,32(%rdi)
15:	movq %rax,40(%rdi)
16:	movq %rax,48(%rdi)
17:	movq %rax,56(%rdi)
	addq $64,%rdi
	subq $64,%rcx
	cmpq $64,%rcx
	jae .Lunrolled
	cmpl $8,%ecx
	jae .Lword
	testl %ecx,%ecx
	jne .Lclear_user_tail
	RET

	/*
	 * If we take an exception on any of the
	 * word stores, we know that %rcx isn't zero,
	 * so we can just go to the tail clearing to
	 * get the exact count.
	 *
	 * The unrolled case might end up clearing
	 * some bytes twice. Don't care.
	 *
	 * We could use the value in %rdi to avoid
	 * a second fault on the exact count case,
	 * but do we really care? No.
	 *
	 * Finally, we could try to align %rdi at the
	 * top of the unrolling. But unaligned stores
	 * just aren't that common or expensive.
	 */
	_ASM_EXTABLE_UA( 1b, .Lclear_user_tail)
	_ASM_EXTABLE_UA(10b, .Lclear_user_tail)
	_ASM_EXTABLE_UA(11b, .Lclear_user_tail)
	_ASM_EXTABLE_UA(12b, .Lclear_user_tail)
	_ASM_EXTABLE_UA(13b, .Lclear_user_tail)
	_ASM_EXTABLE_UA(14b, .Lclear_user_tail)
	_ASM_EXTABLE_UA(15b, .Lclear_user_tail)
	_ASM_EXTABLE_UA(16b, .Lclear_user_tail)
	_ASM_EXTABLE_UA(17b, .Lclear_user_tail)
SYM_FUNC_END(rep_stos_alternative)
EXPORT_SYMBOL(rep_stos_alternative)
