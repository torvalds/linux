/*
 * Copyright 2008 Vitaly Mayatskikh <vmayatsk@redhat.com>
 * Copyright 2002 Andi Kleen, SuSE Labs.
 * Subject to the GNU Public License v2.
 *
 * Functions to copy from and to user space.
 */

#include <linux/linkage.h>
#include <asm/dwarf2.h>

#define FIX_ALIGNMENT 1

#include <asm/current.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>

	.macro ALIGN_DESTINATION
#ifdef FIX_ALIGNMENT
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
	.section .fixup,"ax"
103:	addl %ecx,%edx			/* ecx is zerorest also */
	jmp copy_user_handle_tail
	.previous

	.section __ex_table,"a"
	.align 8
	.quad 100b,103b
	.quad 101b,103b
	.previous
#endif
	.endm

/*
 * copy_user_nocache - Uncached memory copy with exception handling
 * This will force destination/source out of cache for more performance.
 */
ENTRY(__copy_user_nocache)
	CFI_STARTPROC
	cmpl $8,%edx
	jb 20f		/* less then 8 bytes, go to byte copy loop */
	ALIGN_DESTINATION
	movl %edx,%ecx
	andl $63,%edx
	shrl $6,%ecx
	jz 17f
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
	jnz 1b
17:	movl %edx,%ecx
	andl $7,%edx
	shrl $3,%ecx
	jz 20f
18:	movq (%rsi),%r8
19:	movnti %r8,(%rdi)
	leaq 8(%rsi),%rsi
	leaq 8(%rdi),%rdi
	decl %ecx
	jnz 18b
20:	andl %edx,%edx
	jz 23f
	movl %edx,%ecx
21:	movb (%rsi),%al
22:	movb %al,(%rdi)
	incq %rsi
	incq %rdi
	decl %ecx
	jnz 21b
23:	xorl %eax,%eax
	sfence
	ret

	.section .fixup,"ax"
30:	shll $6,%ecx
	addl %ecx,%edx
	jmp 60f
40:	lea (%rdx,%rcx,8),%rdx
	jmp 60f
50:	movl %ecx,%edx
60:	sfence
	jmp copy_user_handle_tail
	.previous

	.section __ex_table,"a"
	.quad 1b,30b
	.quad 2b,30b
	.quad 3b,30b
	.quad 4b,30b
	.quad 5b,30b
	.quad 6b,30b
	.quad 7b,30b
	.quad 8b,30b
	.quad 9b,30b
	.quad 10b,30b
	.quad 11b,30b
	.quad 12b,30b
	.quad 13b,30b
	.quad 14b,30b
	.quad 15b,30b
	.quad 16b,30b
	.quad 18b,40b
	.quad 19b,40b
	.quad 21b,50b
	.quad 22b,50b
	.previous
	CFI_ENDPROC
ENDPROC(__copy_user_nocache)
