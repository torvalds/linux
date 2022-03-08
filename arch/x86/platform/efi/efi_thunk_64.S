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
#include <linux/objtool.h>
#include <asm/page_types.h>
#include <asm/segment.h>

	.text
	.code64
SYM_FUNC_START(__efi64_thunk)
STACK_FRAME_NON_STANDARD __efi64_thunk
	push	%rbp
	push	%rbx

	/*
	 * Switch to 1:1 mapped 32-bit stack pointer.
	 */
	movq	%rsp, %rax
	movq	efi_mixed_mode_stack_pa(%rip), %rsp
	push	%rax

	/*
	 * Copy args passed via the stack
	 */
	subq	$0x24, %rsp
	movq	0x18(%rax), %rbp
	movq	0x20(%rax), %rbx
	movq	0x28(%rax), %rax
	movl	%ebp, 0x18(%rsp)
	movl	%ebx, 0x1c(%rsp)
	movl	%eax, 0x20(%rsp)

	/*
	 * Calculate the physical address of the kernel text.
	 */
	movq	$__START_KERNEL_map, %rax
	subq	phys_base(%rip), %rax

	leaq	1f(%rip), %rbp
	leaq	2f(%rip), %rbx
	subq	%rax, %rbp
	subq	%rax, %rbx

	movl	%ebx, 0x0(%rsp)		/* return address */
	movl	%esi, 0x4(%rsp)
	movl	%edx, 0x8(%rsp)
	movl	%ecx, 0xc(%rsp)
	movl	%r8d, 0x10(%rsp)
	movl	%r9d, 0x14(%rsp)

	/* Switch to 32-bit descriptor */
	pushq	$__KERNEL32_CS
	pushq	%rdi			/* EFI runtime service address */
	lretq

1:	movq	0x20(%rsp), %rsp
	pop	%rbx
	pop	%rbp
	RET

	.code32
2:	pushl	$__KERNEL_CS
	pushl	%ebp
	lret
SYM_FUNC_END(__efi64_thunk)

	.bss
	.balign 8
SYM_DATA(efi_mixed_mode_stack_pa, .quad 0)
