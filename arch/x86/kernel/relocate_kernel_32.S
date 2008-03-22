/*
 * relocate_kernel.S - put the kernel image in place to boot
 * Copyright (C) 2002-2004 Eric Biederman  <ebiederm@xmission.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/linkage.h>
#include <asm/page.h>
#include <asm/kexec.h>
#include <asm/processor-flags.h>
#include <asm/pgtable.h>

/*
 * Must be relocatable PIC code callable as a C function
 */

#define PTR(x) (x << 2)
#define PAGE_ATTR (_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)
#define PAE_PGD_ATTR (_PAGE_PRESENT)

	.text
	.align PAGE_SIZE
	.globl relocate_kernel
relocate_kernel:
	movl	8(%esp), %ebp /* list of pages */

#ifdef CONFIG_X86_PAE
	/* map the control page at its virtual address */

	movl	PTR(VA_PGD)(%ebp), %edi
	movl	PTR(VA_CONTROL_PAGE)(%ebp), %eax
	andl	$0xc0000000, %eax
	shrl	$27, %eax
	addl	%edi, %eax

	movl	PTR(PA_PMD_0)(%ebp), %edx
	orl	$PAE_PGD_ATTR, %edx
	movl	%edx, (%eax)

	movl	PTR(VA_PMD_0)(%ebp), %edi
	movl	PTR(VA_CONTROL_PAGE)(%ebp), %eax
	andl	$0x3fe00000, %eax
	shrl	$18, %eax
	addl	%edi, %eax

	movl	PTR(PA_PTE_0)(%ebp), %edx
	orl	$PAGE_ATTR, %edx
	movl	%edx, (%eax)

	movl	PTR(VA_PTE_0)(%ebp), %edi
	movl	PTR(VA_CONTROL_PAGE)(%ebp), %eax
	andl	$0x001ff000, %eax
	shrl	$9, %eax
	addl	%edi, %eax

	movl	PTR(PA_CONTROL_PAGE)(%ebp), %edx
	orl	$PAGE_ATTR, %edx
	movl	%edx, (%eax)

	/* identity map the control page at its physical address */

	movl	PTR(VA_PGD)(%ebp), %edi
	movl	PTR(PA_CONTROL_PAGE)(%ebp), %eax
	andl	$0xc0000000, %eax
	shrl	$27, %eax
	addl	%edi, %eax

	movl	PTR(PA_PMD_1)(%ebp), %edx
	orl	$PAE_PGD_ATTR, %edx
	movl	%edx, (%eax)

	movl	PTR(VA_PMD_1)(%ebp), %edi
	movl	PTR(PA_CONTROL_PAGE)(%ebp), %eax
	andl	$0x3fe00000, %eax
	shrl	$18, %eax
	addl	%edi, %eax

	movl	PTR(PA_PTE_1)(%ebp), %edx
	orl	$PAGE_ATTR, %edx
	movl	%edx, (%eax)

	movl	PTR(VA_PTE_1)(%ebp), %edi
	movl	PTR(PA_CONTROL_PAGE)(%ebp), %eax
	andl	$0x001ff000, %eax
	shrl	$9, %eax
	addl	%edi, %eax

	movl	PTR(PA_CONTROL_PAGE)(%ebp), %edx
	orl	$PAGE_ATTR, %edx
	movl	%edx, (%eax)
#else
	/* map the control page at its virtual address */

	movl	PTR(VA_PGD)(%ebp), %edi
	movl	PTR(VA_CONTROL_PAGE)(%ebp), %eax
	andl	$0xffc00000, %eax
	shrl	$20, %eax
	addl	%edi, %eax

	movl	PTR(PA_PTE_0)(%ebp), %edx
	orl	$PAGE_ATTR, %edx
	movl	%edx, (%eax)

	movl	PTR(VA_PTE_0)(%ebp), %edi
	movl	PTR(VA_CONTROL_PAGE)(%ebp), %eax
	andl	$0x003ff000, %eax
	shrl	$10, %eax
	addl	%edi, %eax

	movl	PTR(PA_CONTROL_PAGE)(%ebp), %edx
	orl	$PAGE_ATTR, %edx
	movl	%edx, (%eax)

	/* identity map the control page at its physical address */

	movl	PTR(VA_PGD)(%ebp), %edi
	movl	PTR(PA_CONTROL_PAGE)(%ebp), %eax
	andl	$0xffc00000, %eax
	shrl	$20, %eax
	addl	%edi, %eax

	movl	PTR(PA_PTE_1)(%ebp), %edx
	orl	$PAGE_ATTR, %edx
	movl	%edx, (%eax)

	movl	PTR(VA_PTE_1)(%ebp), %edi
	movl	PTR(PA_CONTROL_PAGE)(%ebp), %eax
	andl	$0x003ff000, %eax
	shrl	$10, %eax
	addl	%edi, %eax

	movl	PTR(PA_CONTROL_PAGE)(%ebp), %edx
	orl	$PAGE_ATTR, %edx
	movl	%edx, (%eax)
#endif

relocate_new_kernel:
	/* read the arguments and say goodbye to the stack */
	movl  4(%esp), %ebx /* page_list */
	movl  8(%esp), %ebp /* list of pages */
	movl  12(%esp), %edx /* start address */
	movl  16(%esp), %ecx /* cpu_has_pae */

	/* zero out flags, and disable interrupts */
	pushl $0
	popfl

	/* get physical address of control page now */
	/* this is impossible after page table switch */
	movl	PTR(PA_CONTROL_PAGE)(%ebp), %edi

	/* switch to new set of page tables */
	movl	PTR(PA_PGD)(%ebp), %eax
	movl	%eax, %cr3

	/* setup a new stack at the end of the physical control page */
	lea	PAGE_SIZE(%edi), %esp

	/* jump to identity mapped page */
	movl    %edi, %eax
	addl    $(identity_mapped - relocate_kernel), %eax
	pushl   %eax
	ret

identity_mapped:
	/* store the start address on the stack */
	pushl   %edx

	/* Set cr0 to a known state:
	 *  - Paging disabled
	 *  - Alignment check disabled
	 *  - Write protect disabled
	 *  - No task switch
	 *  - Don't do FP software emulation.
	 *  - Proctected mode enabled
	 */
	movl	%cr0, %eax
	andl	$~(X86_CR0_PG | X86_CR0_AM | X86_CR0_WP | X86_CR0_TS | X86_CR0_EM), %eax
	orl	$(X86_CR0_PE), %eax
	movl	%eax, %cr0

	/* clear cr4 if applicable */
	testl	%ecx, %ecx
	jz	1f
	/* Set cr4 to a known state:
	 * Setting everything to zero seems safe.
	 */
	xorl	%eax, %eax
	movl	%eax, %cr4

	jmp 1f
1:

	/* Flush the TLB (needed?) */
	xorl	%eax, %eax
	movl	%eax, %cr3

	/* Do the copies */
	movl	%ebx, %ecx
	jmp	1f

0:	/* top, read another word from the indirection page */
	movl	(%ebx), %ecx
	addl	$4, %ebx
1:
	testl	$0x1,   %ecx  /* is it a destination page */
	jz	2f
	movl	%ecx,	%edi
	andl	$0xfffff000, %edi
	jmp     0b
2:
	testl	$0x2,	%ecx  /* is it an indirection page */
	jz	2f
	movl	%ecx,	%ebx
	andl	$0xfffff000, %ebx
	jmp     0b
2:
	testl   $0x4,   %ecx /* is it the done indicator */
	jz      2f
	jmp     3f
2:
	testl   $0x8,   %ecx /* is it the source indicator */
	jz      0b	     /* Ignore it otherwise */
	movl    %ecx,   %esi /* For every source page do a copy */
	andl    $0xfffff000, %esi

	movl    $1024, %ecx
	rep ; movsl
	jmp     0b

3:

	/* To be certain of avoiding problems with self-modifying code
	 * I need to execute a serializing instruction here.
	 * So I flush the TLB, it's handy, and not processor dependent.
	 */
	xorl	%eax, %eax
	movl	%eax, %cr3

	/* set all of the registers to known values */
	/* leave %esp alone */

	xorl	%eax, %eax
	xorl	%ebx, %ebx
	xorl    %ecx, %ecx
	xorl    %edx, %edx
	xorl    %esi, %esi
	xorl    %edi, %edi
	xorl    %ebp, %ebp
	ret
