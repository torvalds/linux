/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014, 2015 Intel Corporation; author Matt Fleming
 *
 * Early support for invoking 32-bit EFI services from a 64-bit kernel.
 *
 * Because this thunking occurs before ExitBootServices() we have to
 * restore the firmware's 32-bit GDT before we make EFI serivce calls,
 * since the firmware's 32-bit IDT is still currently installed and it
 * needs to be able to service interrupts.
 *
 * On the plus side, we don't have to worry about mangling 64-bit
 * addresses into 32-bits because we're executing with an identify
 * mapped pagetable and haven't transitioned to 64-bit virtual addresses
 * yet.
 */

#include <linux/linkage.h>
#include <asm/msr.h>
#include <asm/page_types.h>
#include <asm/processor-flags.h>
#include <asm/segment.h>

	.code64
	.text
SYM_FUNC_START(efi64_thunk)
	push	%rbp
	push	%rbx

	subq	$8, %rsp
	leaq	efi_exit32(%rip), %rax
	movl	%eax, 4(%rsp)
	leaq	efi_gdt64(%rip), %rax
	movl	%eax, (%rsp)
	movl	%eax, 2(%rax)		/* Fixup the gdt base address */

	movl	%ds, %eax
	push	%rax
	movl	%es, %eax
	push	%rax
	movl	%ss, %eax
	push	%rax

	/*
	 * Convert x86-64 ABI params to i386 ABI
	 */
	subq	$32, %rsp
	movl	%esi, 0x0(%rsp)
	movl	%edx, 0x4(%rsp)
	movl	%ecx, 0x8(%rsp)
	movq	%r8, %rsi
	movl	%esi, 0xc(%rsp)
	movq	%r9, %rsi
	movl	%esi,  0x10(%rsp)

	sgdt	save_gdt(%rip)

	leaq	1f(%rip), %rbx
	movq	%rbx, func_rt_ptr(%rip)

	/*
	 * Switch to gdt with 32-bit segments. This is the firmware GDT
	 * that was installed when the kernel started executing. This
	 * pointer was saved at the EFI stub entry point in head_64.S.
	 */
	leaq	efi32_boot_gdt(%rip), %rax
	lgdt	(%rax)

	pushq	$__KERNEL_CS
	leaq	efi_enter32(%rip), %rax
	pushq	%rax
	lretq

1:	addq	$32, %rsp

	lgdt	save_gdt(%rip)

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
	addq	$8, %rsp
	pop	%rbx
	pop	%rbp
	ret
SYM_FUNC_END(efi64_thunk)

SYM_FUNC_START_LOCAL(efi_exit32)
	movq	func_rt_ptr(%rip), %rax
	push	%rax
	mov	%rdi, %rax
	ret
SYM_FUNC_END(efi_exit32)

	.code32
/*
 * EFI service pointer must be in %edi.
 *
 * The stack should represent the 32-bit calling convention.
 */
SYM_FUNC_START_LOCAL(efi_enter32)
	movl	$__KERNEL_DS, %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %ss

	/* Reload pgtables */
	movl	%cr3, %eax
	movl	%eax, %cr3

	/* Disable paging */
	movl	%cr0, %eax
	btrl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0

	/* Disable long mode via EFER */
	movl	$MSR_EFER, %ecx
	rdmsr
	btrl	$_EFER_LME, %eax
	wrmsr

	call	*%edi

	/* We must preserve return value */
	movl	%eax, %edi

	/*
	 * Some firmware will return with interrupts enabled. Be sure to
	 * disable them before we switch GDTs.
	 */
	cli

	movl	56(%esp), %eax
	movl	%eax, 2(%eax)
	lgdtl	(%eax)

	movl	%cr4, %eax
	btsl	$(X86_CR4_PAE_BIT), %eax
	movl	%eax, %cr4

	movl	%cr3, %eax
	movl	%eax, %cr3

	movl	$MSR_EFER, %ecx
	rdmsr
	btsl	$_EFER_LME, %eax
	wrmsr

	xorl	%eax, %eax
	lldt	%ax

	movl	60(%esp), %eax
	pushl	$__KERNEL_CS
	pushl	%eax

	/* Enable paging */
	movl	%cr0, %eax
	btsl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0
	lret
SYM_FUNC_END(efi_enter32)

	.data
	.balign	8
SYM_DATA_START(efi32_boot_gdt)
	.word	0
	.quad	0
SYM_DATA_END(efi32_boot_gdt)

SYM_DATA_START_LOCAL(save_gdt)
	.word	0
	.quad	0
SYM_DATA_END(save_gdt)

SYM_DATA_LOCAL(func_rt_ptr, .quad 0)

SYM_DATA_START(efi_gdt64)
	.word	efi_gdt64_end - efi_gdt64
	.long	0			/* Filled out by user */
	.word	0
	.quad	0x0000000000000000	/* NULL descriptor */
	.quad	0x00af9a000000ffff	/* __KERNEL_CS */
	.quad	0x00cf92000000ffff	/* __KERNEL_DS */
	.quad	0x0080890000000000	/* TS descriptor */
	.quad   0x0000000000000000	/* TS continued */
SYM_DATA_END_LABEL(efi_gdt64, SYM_L_LOCAL, efi_gdt64_end)
