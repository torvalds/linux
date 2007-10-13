/*
 * relocate_kernel.S - put the kernel image in place to boot
 * Copyright (C) 2002-2005 Eric Biederman  <ebiederm@xmission.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/linkage.h>
#include <asm/page.h>
#include <asm/kexec.h>

/*
 * Must be relocatable PIC code callable as a C function
 */

#define PTR(x) (x << 3)
#define PAGE_ALIGNED (1 << PAGE_SHIFT)
#define PAGE_ATTR 0x63 /* _PAGE_PRESENT|_PAGE_RW|_PAGE_ACCESSED|_PAGE_DIRTY */

	.text
	.align PAGE_ALIGNED
	.code64
	.globl relocate_kernel
relocate_kernel:
	/* %rdi indirection_page
	 * %rsi page_list
	 * %rdx start address
	 */

	/* map the control page at its virtual address */

	movq	$0x0000ff8000000000, %r10        /* mask */
	mov	$(39 - 3), %cl                   /* bits to shift */
	movq	PTR(VA_CONTROL_PAGE)(%rsi), %r11 /* address to map */

	movq	%r11, %r9
	andq	%r10, %r9
	shrq	%cl, %r9

	movq	PTR(VA_PGD)(%rsi), %r8
	addq	%r8, %r9
	movq	PTR(PA_PUD_0)(%rsi), %r8
	orq	$PAGE_ATTR, %r8
	movq	%r8, (%r9)

	shrq	$9, %r10
	sub	$9, %cl

	movq	%r11, %r9
	andq	%r10, %r9
	shrq	%cl, %r9

	movq	PTR(VA_PUD_0)(%rsi), %r8
	addq	%r8, %r9
	movq	PTR(PA_PMD_0)(%rsi), %r8
	orq	$PAGE_ATTR, %r8
	movq	%r8, (%r9)

	shrq	$9, %r10
	sub	$9, %cl

	movq	%r11, %r9
	andq	%r10, %r9
	shrq	%cl, %r9

	movq	PTR(VA_PMD_0)(%rsi), %r8
	addq	%r8, %r9
	movq	PTR(PA_PTE_0)(%rsi), %r8
	orq	$PAGE_ATTR, %r8
	movq	%r8, (%r9)

	shrq	$9, %r10
	sub	$9, %cl

	movq	%r11, %r9
	andq	%r10, %r9
	shrq	%cl, %r9

	movq	PTR(VA_PTE_0)(%rsi), %r8
	addq	%r8, %r9
	movq	PTR(PA_CONTROL_PAGE)(%rsi), %r8
	orq	$PAGE_ATTR, %r8
	movq	%r8, (%r9)

	/* identity map the control page at its physical address */

	movq	$0x0000ff8000000000, %r10        /* mask */
	mov	$(39 - 3), %cl                   /* bits to shift */
	movq	PTR(PA_CONTROL_PAGE)(%rsi), %r11 /* address to map */

	movq	%r11, %r9
	andq	%r10, %r9
	shrq	%cl, %r9

	movq	PTR(VA_PGD)(%rsi), %r8
	addq	%r8, %r9
	movq	PTR(PA_PUD_1)(%rsi), %r8
	orq	$PAGE_ATTR, %r8
	movq	%r8, (%r9)

	shrq	$9, %r10
	sub	$9, %cl

	movq	%r11, %r9
	andq	%r10, %r9
	shrq	%cl, %r9

	movq	PTR(VA_PUD_1)(%rsi), %r8
	addq	%r8, %r9
	movq	PTR(PA_PMD_1)(%rsi), %r8
	orq	$PAGE_ATTR, %r8
	movq	%r8, (%r9)

	shrq	$9, %r10
	sub	$9, %cl

	movq	%r11, %r9
	andq	%r10, %r9
	shrq	%cl, %r9

	movq	PTR(VA_PMD_1)(%rsi), %r8
	addq	%r8, %r9
	movq	PTR(PA_PTE_1)(%rsi), %r8
	orq	$PAGE_ATTR, %r8
	movq	%r8, (%r9)

	shrq	$9, %r10
	sub	$9, %cl

	movq	%r11, %r9
	andq	%r10, %r9
	shrq	%cl, %r9

	movq	PTR(VA_PTE_1)(%rsi), %r8
	addq	%r8, %r9
	movq	PTR(PA_CONTROL_PAGE)(%rsi), %r8
	orq	$PAGE_ATTR, %r8
	movq	%r8, (%r9)

relocate_new_kernel:
	/* %rdi indirection_page
	 * %rsi page_list
	 * %rdx start address
	 */

	/* zero out flags, and disable interrupts */
	pushq $0
	popfq

	/* get physical address of control page now */
	/* this is impossible after page table switch */
	movq	PTR(PA_CONTROL_PAGE)(%rsi), %r8

	/* get physical address of page table now too */
	movq	PTR(PA_TABLE_PAGE)(%rsi), %rcx

	/* switch to new set of page tables */
	movq	PTR(PA_PGD)(%rsi), %r9
	movq	%r9, %cr3

	/* setup a new stack at the end of the physical control page */
	lea	4096(%r8), %rsp

	/* jump to identity mapped page */
	addq	$(identity_mapped - relocate_kernel), %r8
	pushq	%r8
	ret

identity_mapped:
	/* store the start address on the stack */
	pushq   %rdx

	/* Set cr0 to a known state:
	 * 31 1 == Paging enabled
	 * 18 0 == Alignment check disabled
	 * 16 0 == Write protect disabled
	 * 3  0 == No task switch
	 * 2  0 == Don't do FP software emulation.
	 * 0  1 == Proctected mode enabled
	 */
	movq	%cr0, %rax
	andq	$~((1<<18)|(1<<16)|(1<<3)|(1<<2)), %rax
	orl	$((1<<31)|(1<<0)), %eax
	movq	%rax, %cr0

	/* Set cr4 to a known state:
	 * 10 0 == xmm exceptions disabled
	 * 9  0 == xmm registers instructions disabled
	 * 8  0 == performance monitoring counter disabled
	 * 7  0 == page global disabled
	 * 6  0 == machine check exceptions disabled
	 * 5  1 == physical address extension enabled
	 * 4  0 == page size extensions	disabled
	 * 3  0 == Debug extensions disabled
	 * 2  0 == Time stamp disable (disabled)
	 * 1  0 == Protected mode virtual interrupts disabled
	 * 0  0 == VME disabled
	 */

	movq	$((1<<5)), %rax
	movq	%rax, %cr4

	jmp 1f
1:

	/* Switch to the identity mapped page tables,
	 * and flush the TLB.
	*/
	movq	%rcx, %cr3

	/* Do the copies */
	movq	%rdi, %rcx 	/* Put the page_list in %rcx */
	xorq	%rdi, %rdi
	xorq	%rsi, %rsi
	jmp	1f

0:	/* top, read another word for the indirection page */

	movq	(%rbx), %rcx
	addq	$8,	%rbx
1:
	testq	$0x1,	%rcx  /* is it a destination page? */
	jz	2f
	movq	%rcx,	%rdi
	andq	$0xfffffffffffff000, %rdi
	jmp	0b
2:
	testq	$0x2,	%rcx  /* is it an indirection page? */
	jz	2f
	movq	%rcx,   %rbx
	andq	$0xfffffffffffff000, %rbx
	jmp	0b
2:
	testq	$0x4,	%rcx  /* is it the done indicator? */
	jz	2f
	jmp	3f
2:
	testq	$0x8,	%rcx  /* is it the source indicator? */
	jz	0b	      /* Ignore it otherwise */
	movq	%rcx,   %rsi  /* For ever source page do a copy */
	andq	$0xfffffffffffff000, %rsi

	movq	$512,   %rcx
	rep ; movsq
	jmp	0b
3:

	/* To be certain of avoiding problems with self-modifying code
	 * I need to execute a serializing instruction here.
	 * So I flush the TLB by reloading %cr3 here, it's handy,
	 * and not processor dependent.
	 */
	movq	%cr3, %rax
	movq	%rax, %cr3

	/* set all of the registers to known values */
	/* leave %rsp alone */

	xorq	%rax, %rax
	xorq	%rbx, %rbx
	xorq    %rcx, %rcx
	xorq    %rdx, %rdx
	xorq    %rsi, %rsi
	xorq    %rdi, %rdi
	xorq    %rbp, %rbp
	xorq	%r8,  %r8
	xorq	%r9,  %r9
	xorq	%r10, %r9
	xorq	%r11, %r11
	xorq	%r12, %r12
	xorq	%r13, %r13
	xorq	%r14, %r14
	xorq	%r15, %r15

	ret
