/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Vitaly Mayatskikh <vmayatsk@redhat.com>
 * Copyright 2002 Andi Kleen, SuSE Labs.
 *
 * Functions to copy from and to user space.
 */

#include <linux/linkage.h>
#include <asm/current.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>
#include <asm/asm.h>
#include <asm/smap.h>
#include <asm/export.h>
#include <asm/trapnr.h>

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

/*
 * The uncached copy needs to align the destination for
 * movnti and friends.
 */
.macro ALIGN_DESTINATION
	/* check for bad alignment of destination */
	movl %edi,%ecx
	andl $7,%ecx
	jz 102f				/* already aligned */
	subl $8,%ecx
	negl %ecx
	subl %ecx,%edx
100:	movb (%rsi),%al
101:	movb %al,(%rdi)
	incq %rsi
	incq %rdi
	decl %ecx
	jnz 100b
102:

	_ASM_EXTABLE_CPY(100b, .Lcopy_user_handle_align)
	_ASM_EXTABLE_CPY(101b, .Lcopy_user_handle_align)
.endm


/*
 * copy_user_nocache - Uncached memory copy with exception handling
 * This will force destination out of cache for more performance.
 *
 * Note: Cached memory copy is used when destination or size is not
 * naturally aligned. That is:
 *  - Require 8-byte alignment when size is 8 bytes or larger.
 *  - Require 4-byte alignment when size is 4 bytes.
 */
SYM_FUNC_START(__copy_user_nocache)
	/* If size is less than 8 bytes, go to 4-byte copy */
	cmpl $8,%edx
	jb .L_4b_nocache_copy_entry

	/* If destination is not 8-byte aligned, "cache" copy to align it */
	ALIGN_DESTINATION

	/* Set 4x8-byte copy count and remainder */
	movl %edx,%ecx
	andl $63,%edx
	shrl $6,%ecx
	jz .L_8b_nocache_copy_entry	/* jump if count is 0 */

	/* Perform 4x8-byte nocache loop-copy */
.L_4x8b_nocache_copy_loop:
1:	movq (%rsi),%r8
2:	movq 1*8(%rsi),%r9
3:	movq 2*8(%rsi),%r10
4:	movq 3*8(%rsi),%r11
5:	movnti %r8,(%rdi)
6:	movnti %r9,1*8(%rdi)
7:	movnti %r10,2*8(%rdi)
8:	movnti %r11,3*8(%rdi)
9:	movq 4*8(%rsi),%r8
10:	movq 5*8(%rsi),%r9
11:	movq 6*8(%rsi),%r10
12:	movq 7*8(%rsi),%r11
13:	movnti %r8,4*8(%rdi)
14:	movnti %r9,5*8(%rdi)
15:	movnti %r10,6*8(%rdi)
16:	movnti %r11,7*8(%rdi)
	leaq 64(%rsi),%rsi
	leaq 64(%rdi),%rdi
	decl %ecx
	jnz .L_4x8b_nocache_copy_loop

	/* Set 8-byte copy count and remainder */
.L_8b_nocache_copy_entry:
	movl %edx,%ecx
	andl $7,%edx
	shrl $3,%ecx
	jz .L_4b_nocache_copy_entry	/* jump if count is 0 */

	/* Perform 8-byte nocache loop-copy */
.L_8b_nocache_copy_loop:
20:	movq (%rsi),%r8
21:	movnti %r8,(%rdi)
	leaq 8(%rsi),%rsi
	leaq 8(%rdi),%rdi
	decl %ecx
	jnz .L_8b_nocache_copy_loop

	/* If no byte left, we're done */
.L_4b_nocache_copy_entry:
	andl %edx,%edx
	jz .L_finish_copy

	/* If destination is not 4-byte aligned, go to byte copy: */
	movl %edi,%ecx
	andl $3,%ecx
	jnz .L_1b_cache_copy_entry

	/* Set 4-byte copy count (1 or 0) and remainder */
	movl %edx,%ecx
	andl $3,%edx
	shrl $2,%ecx
	jz .L_1b_cache_copy_entry	/* jump if count is 0 */

	/* Perform 4-byte nocache copy: */
30:	movl (%rsi),%r8d
31:	movnti %r8d,(%rdi)
	leaq 4(%rsi),%rsi
	leaq 4(%rdi),%rdi

	/* If no bytes left, we're done: */
	andl %edx,%edx
	jz .L_finish_copy

	/* Perform byte "cache" loop-copy for the remainder */
.L_1b_cache_copy_entry:
	movl %edx,%ecx
.L_1b_cache_copy_loop:
40:	movb (%rsi),%al
41:	movb %al,(%rdi)
	incq %rsi
	incq %rdi
	decl %ecx
	jnz .L_1b_cache_copy_loop

	/* Finished copying; fence the prior stores */
.L_finish_copy:
	xorl %eax,%eax
	sfence
	RET

.L_fixup_4x8b_copy:
	shll $6,%ecx
	addl %ecx,%edx
	jmp .L_fixup_handle_tail
.L_fixup_8b_copy:
	lea (%rdx,%rcx,8),%rdx
	jmp .L_fixup_handle_tail
.L_fixup_4b_copy:
	lea (%rdx,%rcx,4),%rdx
	jmp .L_fixup_handle_tail
.L_fixup_1b_copy:
	movl %ecx,%edx
.L_fixup_handle_tail:
	sfence
	jmp .Lcopy_user_handle_tail

	_ASM_EXTABLE_CPY(1b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(2b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(3b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(4b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(5b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(6b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(7b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(8b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(9b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(10b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(11b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(12b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(13b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(14b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(15b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(16b, .L_fixup_4x8b_copy)
	_ASM_EXTABLE_CPY(20b, .L_fixup_8b_copy)
	_ASM_EXTABLE_CPY(21b, .L_fixup_8b_copy)
	_ASM_EXTABLE_CPY(30b, .L_fixup_4b_copy)
	_ASM_EXTABLE_CPY(31b, .L_fixup_4b_copy)
	_ASM_EXTABLE_CPY(40b, .L_fixup_1b_copy)
	_ASM_EXTABLE_CPY(41b, .L_fixup_1b_copy)

/*
 * Try to copy last bytes and clear the rest if needed.
 * Since protection fault in copy_from/to_user is not a normal situation,
 * it is not necessary to optimize tail handling.
 * Don't try to copy the tail if machine check happened
 *
 * Input:
 * eax trap number written by ex_handler_copy()
 * rdi destination
 * rsi source
 * rdx count
 *
 * Output:
 * eax uncopied bytes or 0 if successful.
 */
.Lcopy_user_handle_tail:
	cmp $X86_TRAP_MC,%eax
	je 3f

	movl %edx,%ecx
1:	rep movsb
2:	mov %ecx,%eax
	RET

3:
	movl %edx,%eax
	RET

	_ASM_EXTABLE_CPY(1b, 2b)

.Lcopy_user_handle_align:
	addl %ecx,%edx			/* ecx is zerorest also */
	jmp .Lcopy_user_handle_tail

SYM_FUNC_END(__copy_user_nocache)
EXPORT_SYMBOL(__copy_user_nocache)
