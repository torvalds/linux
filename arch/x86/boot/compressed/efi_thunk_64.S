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
 * addresses into 32-bits because we're executing with an identity
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
SYM_FUNC_START(__efi64_thunk)
	push	%rbp
	push	%rbx

	leaq	1f(%rip), %rbp

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
	movl	%r8d, 0xc(%rsp)
	movl	%r9d, 0x10(%rsp)

	leaq	0x14(%rsp), %rbx
	sgdt	(%rbx)

	/*
	 * Switch to gdt with 32-bit segments. This is the firmware GDT
	 * that was installed when the kernel started executing. This
	 * pointer was saved at the EFI stub entry point in head_64.S.
	 *
	 * Pass the saved DS selector to the 32-bit code, and use far return to
	 * restore the saved CS selector.
	 */
	leaq	efi32_boot_gdt(%rip), %rax
	lgdt	(%rax)

	movzwl	efi32_boot_ds(%rip), %edx
	movzwq	efi32_boot_cs(%rip), %rax
	pushq	%rax
	leaq	efi_enter32(%rip), %rax
	pushq	%rax
	lretq

1:	addq	$32, %rsp
	movq	%rdi, %rax

	pop	%rbx
	movl	%ebx, %ss
	pop	%rbx
	movl	%ebx, %es
	pop	%rbx
	movl	%ebx, %ds
	/* Clear out 32-bit selector from FS and GS */
	xorl	%ebx, %ebx
	movl	%ebx, %fs
	movl	%ebx, %gs

	/*
	 * Convert 32-bit status code into 64-bit.
	 */
	roll	$1, %eax
	rorq	$1, %rax

	pop	%rbx
	pop	%rbp
	RET
SYM_FUNC_END(__efi64_thunk)

	.code32
/*
 * EFI service pointer must be in %edi.
 *
 * The stack should represent the 32-bit calling convention.
 */
SYM_FUNC_START_LOCAL(efi_enter32)
	/* Load firmware selector into data and stack segment registers */
	movl	%edx, %ds
	movl	%edx, %es
	movl	%edx, %fs
	movl	%edx, %gs
	movl	%edx, %ss

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

	lgdtl	(%ebx)

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

	pushl	$__KERNEL_CS
	pushl	%ebp

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

SYM_DATA_START(efi32_boot_cs)
	.word	0
SYM_DATA_END(efi32_boot_cs)

SYM_DATA_START(efi32_boot_ds)
	.word	0
SYM_DATA_END(efi32_boot_ds)
