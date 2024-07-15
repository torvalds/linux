/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014, 2015 Intel Corporation; author Matt Fleming
 *
 * Early support for invoking 32-bit EFI services from a 64-bit kernel.
 *
 * Because this thunking occurs before ExitBootServices() we have to
 * restore the firmware's 32-bit GDT and IDT before we make EFI service
 * calls.
 *
 * On the plus side, we don't have to worry about mangling 64-bit
 * addresses into 32-bits because we're executing with an identity
 * mapped pagetable and haven't transitioned to 64-bit virtual addresses
 * yet.
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/msr.h>
#include <asm/page_types.h>
#include <asm/processor-flags.h>
#include <asm/segment.h>
#include <asm/setup.h>

	.code64
	.text
/*
 * When booting in 64-bit mode on 32-bit EFI firmware, startup_64_mixed_mode()
 * is the first thing that runs after switching to long mode. Depending on
 * whether the EFI handover protocol or the compat entry point was used to
 * enter the kernel, it will either branch to the common 64-bit EFI stub
 * entrypoint efi_stub_entry() directly, or via the 64-bit EFI PE/COFF
 * entrypoint efi_pe_entry(). In the former case, the bootloader must provide a
 * struct bootparams pointer as the third argument, so the presence of such a
 * pointer is used to disambiguate.
 *
 *                                                             +--------------+
 *  +------------------+     +------------+            +------>| efi_pe_entry |
 *  | efi32_pe_entry   |---->|            |            |       +-----------+--+
 *  +------------------+     |            |     +------+----------------+  |
 *                           | startup_32 |---->| startup_64_mixed_mode |  |
 *  +------------------+     |            |     +------+----------------+  |
 *  | efi32_stub_entry |---->|            |            |                   |
 *  +------------------+     +------------+            |                   |
 *                                                     V                   |
 *                           +------------+     +----------------+         |
 *                           | startup_64 |<----| efi_stub_entry |<--------+
 *                           +------------+     +----------------+
 */
SYM_FUNC_START(startup_64_mixed_mode)
	lea	efi32_boot_args(%rip), %rdx
	mov	0(%rdx), %edi
	mov	4(%rdx), %esi

	/* Switch to the firmware's stack */
	movl	efi32_boot_sp(%rip), %esp
	andl	$~7, %esp

#ifdef CONFIG_EFI_HANDOVER_PROTOCOL
	mov	8(%rdx), %edx		// saved bootparams pointer
	test	%edx, %edx
	jnz	efi_stub_entry
#endif
	/*
	 * efi_pe_entry uses MS calling convention, which requires 32 bytes of
	 * shadow space on the stack even if all arguments are passed in
	 * registers. We also need an additional 8 bytes for the space that
	 * would be occupied by the return address, and this also results in
	 * the correct stack alignment for entry.
	 */
	sub	$40, %rsp
	mov	%rdi, %rcx		// MS calling convention
	mov	%rsi, %rdx
	jmp	efi_pe_entry
SYM_FUNC_END(startup_64_mixed_mode)

SYM_FUNC_START(__efi64_thunk)
	push	%rbp
	push	%rbx

	movl	%ds, %eax
	push	%rax
	movl	%es, %eax
	push	%rax
	movl	%ss, %eax
	push	%rax

	/* Copy args passed on stack */
	movq	0x30(%rsp), %rbp
	movq	0x38(%rsp), %rbx
	movq	0x40(%rsp), %rax

	/*
	 * Convert x86-64 ABI params to i386 ABI
	 */
	subq	$64, %rsp
	movl	%esi, 0x0(%rsp)
	movl	%edx, 0x4(%rsp)
	movl	%ecx, 0x8(%rsp)
	movl	%r8d, 0xc(%rsp)
	movl	%r9d, 0x10(%rsp)
	movl	%ebp, 0x14(%rsp)
	movl	%ebx, 0x18(%rsp)
	movl	%eax, 0x1c(%rsp)

	leaq	0x20(%rsp), %rbx
	sgdt	(%rbx)
	sidt	16(%rbx)

	leaq	1f(%rip), %rbp

	/*
	 * Switch to IDT and GDT with 32-bit segments. These are the firmware
	 * GDT and IDT that were installed when the kernel started executing.
	 * The pointers were saved by the efi32_entry() routine below.
	 *
	 * Pass the saved DS selector to the 32-bit code, and use far return to
	 * restore the saved CS selector.
	 */
	lidt	efi32_boot_idt(%rip)
	lgdt	efi32_boot_gdt(%rip)

	movzwl	efi32_boot_ds(%rip), %edx
	movzwq	efi32_boot_cs(%rip), %rax
	pushq	%rax
	leaq	efi_enter32(%rip), %rax
	pushq	%rax
	lretq

1:	addq	$64, %rsp
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

	pop	%rbx
	pop	%rbp
	RET
SYM_FUNC_END(__efi64_thunk)

	.code32
#ifdef CONFIG_EFI_HANDOVER_PROTOCOL
SYM_FUNC_START(efi32_stub_entry)
	call	1f
1:	popl	%ecx
	leal	(efi32_boot_args - 1b)(%ecx), %ebx

	/* Clear BSS */
	xorl	%eax, %eax
	leal	(_bss - 1b)(%ecx), %edi
	leal	(_ebss - 1b)(%ecx), %ecx
	subl	%edi, %ecx
	shrl	$2, %ecx
	cld
	rep	stosl

	add	$0x4, %esp		/* Discard return address */
	popl	%ecx
	popl	%edx
	popl	%esi
	movl	%esi, 8(%ebx)
	jmp	efi32_entry
SYM_FUNC_END(efi32_stub_entry)
#endif

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
	 * disable them before we switch GDTs and IDTs.
	 */
	cli

	lidtl	16(%ebx)
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

/*
 * This is the common EFI stub entry point for mixed mode.
 *
 * Arguments:	%ecx	image handle
 * 		%edx	EFI system table pointer
 *
 * Since this is the point of no return for ordinary execution, no registers
 * are considered live except for the function parameters. [Note that the EFI
 * stub may still exit and return to the firmware using the Exit() EFI boot
 * service.]
 */
SYM_FUNC_START_LOCAL(efi32_entry)
	call	1f
1:	pop	%ebx

	/* Save firmware GDTR and code/data selectors */
	sgdtl	(efi32_boot_gdt - 1b)(%ebx)
	movw	%cs, (efi32_boot_cs - 1b)(%ebx)
	movw	%ds, (efi32_boot_ds - 1b)(%ebx)

	/* Store firmware IDT descriptor */
	sidtl	(efi32_boot_idt - 1b)(%ebx)

	/* Store firmware stack pointer */
	movl	%esp, (efi32_boot_sp - 1b)(%ebx)

	/* Store boot arguments */
	leal	(efi32_boot_args - 1b)(%ebx), %ebx
	movl	%ecx, 0(%ebx)
	movl	%edx, 4(%ebx)
	movb	$0x0, 12(%ebx)          // efi_is64

	/*
	 * Allocate some memory for a temporary struct boot_params, which only
	 * needs the minimal pieces that startup_32() relies on.
	 */
	subl	$PARAM_SIZE, %esp
	movl	%esp, %esi
	movl	$PAGE_SIZE, BP_kernel_alignment(%esi)
	movl	$_end - 1b, BP_init_size(%esi)
	subl	$startup_32 - 1b, BP_init_size(%esi)

	/* Disable paging */
	movl	%cr0, %eax
	btrl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0

	jmp	startup_32
SYM_FUNC_END(efi32_entry)

/*
 * efi_status_t efi32_pe_entry(efi_handle_t image_handle,
 *			       efi_system_table_32_t *sys_table)
 */
SYM_FUNC_START(efi32_pe_entry)
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%ebx				// save callee-save registers
	pushl	%edi

	call	verify_cpu			// check for long mode support
	testl	%eax, %eax
	movl	$0x80000003, %eax		// EFI_UNSUPPORTED
	jnz	2f

	movl	8(%ebp), %ecx			// image_handle
	movl	12(%ebp), %edx			// sys_table
	jmp	efi32_entry			// pass %ecx, %edx
						// no other registers remain live

2:	popl	%edi				// restore callee-save registers
	popl	%ebx
	leave
	RET
SYM_FUNC_END(efi32_pe_entry)

#ifdef CONFIG_EFI_HANDOVER_PROTOCOL
	.org	efi32_stub_entry + 0x200
	.code64
SYM_FUNC_START_NOALIGN(efi64_stub_entry)
	jmp	efi_handover_entry
SYM_FUNC_END(efi64_stub_entry)
#endif

	.data
	.balign	8
SYM_DATA_START_LOCAL(efi32_boot_gdt)
	.word	0
	.quad	0
SYM_DATA_END(efi32_boot_gdt)

SYM_DATA_START_LOCAL(efi32_boot_idt)
	.word	0
	.quad	0
SYM_DATA_END(efi32_boot_idt)

SYM_DATA_LOCAL(efi32_boot_cs, .word 0)
SYM_DATA_LOCAL(efi32_boot_ds, .word 0)
SYM_DATA_LOCAL(efi32_boot_sp, .long 0)
SYM_DATA_LOCAL(efi32_boot_args, .long 0, 0, 0)
SYM_DATA(efi_is64, .byte 1)
