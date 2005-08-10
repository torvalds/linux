/*
 * relocate_kernel.S - put the kernel image in place to boot
 * Copyright (C) 2002-2004 Eric Biederman  <ebiederm@xmission.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/linkage.h>

	/*
	 * Must be relocatable PIC code callable as a C function, that once
	 * it starts can not use the previous processes stack.
	 */
	.globl relocate_new_kernel
relocate_new_kernel:
	/* read the arguments and say goodbye to the stack */
	movl  4(%esp), %ebx /* page_list */
	movl  8(%esp), %ebp /* reboot_code_buffer */
	movl  12(%esp), %edx /* start address */
	movl  16(%esp), %ecx /* cpu_has_pae */

	/* zero out flags, and disable interrupts */
	pushl $0
	popfl

	/* set a new stack at the bottom of our page... */
	lea   4096(%ebp), %esp

	/* store the parameters back on the stack */
	pushl   %edx /* store the start address */

	/* Set cr0 to a known state:
	 * 31 0 == Paging disabled
	 * 18 0 == Alignment check disabled
	 * 16 0 == Write protect disabled
	 * 3  0 == No task switch
	 * 2  0 == Don't do FP software emulation.
	 * 0  1 == Proctected mode enabled
	 */
	movl	%cr0, %eax
	andl	$~((1<<31)|(1<<18)|(1<<16)|(1<<3)|(1<<2)), %eax
	orl	$(1<<0), %eax
	movl	%eax, %cr0

	/* clear cr4 if applicable */
	testl	%ecx, %ecx
	jz	1f
	/* Set cr4 to a known state:
	 * Setting everything to zero seems safe.
	 */
	movl	%cr4, %eax
	andl	$0, %eax
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
relocate_new_kernel_end:

	.globl relocate_new_kernel_size
relocate_new_kernel_size:
	.long relocate_new_kernel_end - relocate_new_kernel
