/*
 * Copyright (C) 2014 Intel Corporation; author Matt Fleming
 */

#include <linux/linkage.h>
#include <asm/page_types.h>

	.text
	.code64
ENTRY(efi64_thunk)
	push	%rbp
	push	%rbx

	/*
	 * Switch to 1:1 mapped 32-bit stack pointer.
	 */
	movq	%rsp, efi_saved_sp(%rip)
	movq	efi_scratch+25(%rip), %rsp

	/*
	 * Calculate the physical address of the kernel text.
	 */
	movq	$__START_KERNEL_map, %rax
	subq	phys_base(%rip), %rax

	/*
	 * Push some physical addresses onto the stack. This is easier
	 * to do now in a code64 section while the assembler can address
	 * 64-bit values. Note that all the addresses on the stack are
	 * 32-bit.
	 */
	subq	$16, %rsp
	leaq	efi_exit32(%rip), %rbx
	subq	%rax, %rbx
	movl	%ebx, 8(%rsp)
	leaq	efi_gdt64(%rip), %rbx
	subq	%rax, %rbx
	movl	%ebx, 2(%ebx)
	movl	%ebx, 4(%rsp)
	leaq	efi_gdt32(%rip), %rbx
	subq	%rax, %rbx
	movl	%ebx, 2(%ebx)
	movl	%ebx, (%rsp)

	leaq	__efi64_thunk(%rip), %rbx
	subq	%rax, %rbx
	call	*%rbx

	movq	efi_saved_sp(%rip), %rsp
	pop	%rbx
	pop	%rbp
	retq
ENDPROC(efi64_thunk)

	.data
efi_gdt32:
	.word 	efi_gdt32_end - efi_gdt32
	.long	0			/* Filled out above */
	.word	0
	.quad	0x0000000000000000	/* NULL descriptor */
	.quad	0x00cf9a000000ffff	/* __KERNEL_CS */
	.quad	0x00cf93000000ffff	/* __KERNEL_DS */
efi_gdt32_end:

efi_saved_sp:		.quad 0
