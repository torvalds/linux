/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014 Intel Corporation; author Matt Fleming
 *
 * Support for invoking 32-bit EFI runtime services from a 64-bit
 * kernel.
 *
 * The below thunking functions are only used after ExitBootServices()
 * has been called. This simplifies things considerably as compared with
 * the early EFI thunking because we can leave all the kernel state
 * intact (GDT, IDT, etc) and simply invoke the the 32-bit EFI runtime
 * services from __KERNEL32_CS. This means we can continue to service
 * interrupts across an EFI mixed mode call.
 *
 * We do however, need to handle the fact that we're running in a full
 * 64-bit virtual address space. Things like the stack and instruction
 * addresses need to be accessible by the 32-bit firmware, so we rely on
 * using the identity mappings in the EFI page table to access the stack
 * and kernel text (see efi_setup_page_tables()).
 */

#include <linux/linkage.h>
#include <asm/page_types.h>
#include <asm/segment.h>

	.text
	.code64
ENTRY(efi64_thunk)
	push	%rbp
	push	%rbx

	/*
	 * Switch to 1:1 mapped 32-bit stack pointer.
	 */
	movq	%rsp, efi_saved_sp(%rip)
	movq	efi_scratch(%rip), %rsp

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

	leaq	__efi64_thunk(%rip), %rbx
	subq	%rax, %rbx
	call	*%rbx

	movq	efi_saved_sp(%rip), %rsp
	pop	%rbx
	pop	%rbp
	retq
ENDPROC(efi64_thunk)

/*
 * We run this function from the 1:1 mapping.
 *
 * This function must be invoked with a 1:1 mapped stack.
 */
ENTRY(__efi64_thunk)
	movl	%ds, %eax
	push	%rax
	movl	%es, %eax
	push	%rax
	movl	%ss, %eax
	push	%rax

	subq	$32, %rsp
	movl	%esi, 0x0(%rsp)
	movl	%edx, 0x4(%rsp)
	movl	%ecx, 0x8(%rsp)
	movq	%r8, %rsi
	movl	%esi, 0xc(%rsp)
	movq	%r9, %rsi
	movl	%esi,  0x10(%rsp)

	leaq	1f(%rip), %rbx
	movq	%rbx, func_rt_ptr(%rip)

	/* Switch to 32-bit descriptor */
	pushq	$__KERNEL32_CS
	leaq	efi_enter32(%rip), %rax
	pushq	%rax
	lretq

1:	addq	$32, %rsp

	pop	%rbx
	movl	%ebx, %ss
	pop	%rbx
	movl	%ebx, %es
	pop	%rbx
	movl	%ebx, %ds

	/*
	 * Convert 32-bit status code into 64-bit.
	 */
	test	%rax, %rax
	jz	1f
	movl	%eax, %ecx
	andl	$0x0fffffff, %ecx
	andl	$0xf0000000, %eax
	shl	$32, %rax
	or	%rcx, %rax
1:
	ret
ENDPROC(__efi64_thunk)

ENTRY(efi_exit32)
	movq	func_rt_ptr(%rip), %rax
	push	%rax
	mov	%rdi, %rax
	ret
ENDPROC(efi_exit32)

	.code32
/*
 * EFI service pointer must be in %edi.
 *
 * The stack should represent the 32-bit calling convention.
 */
ENTRY(efi_enter32)
	movl	$__KERNEL_DS, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %ss

	call	*%edi

	/* We must preserve return value */
	movl	%eax, %edi

	movl	72(%esp), %eax
	pushl	$__KERNEL_CS
	pushl	%eax

	lret
ENDPROC(efi_enter32)

	.data
	.balign	8
func_rt_ptr:		.quad 0
efi_saved_sp:		.quad 0
