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
#include <asm/msr.h>
#include <asm/page_types.h>
#include <asm/processor-flags.h>
#include <asm/segment.h>

	.code64
	.text
/*
 * When booting in 64-bit mode on 32-bit EFI firmware, startup_64_mixed_mode()
 * is the first thing that runs after switching to long mode. Depending on
 * whether the EFI handover protocol or the compat entry point was used to
 * enter the kernel, it will either branch to the 64-bit EFI handover
 * entrypoint at offset 0x390 in the image, or to the 64-bit EFI PE/COFF
 * entrypoint efi_pe_entry(). In the former case, the bootloader must provide a
 * struct bootparams pointer as the third argument, so the presence of such a
 * pointer is used to disambiguate.
 *
 *                                                             +--------------+
 *  +------------------+     +------------+            +------>| efi_pe_entry |
 *  | efi32_pe_entry   |---->|            |            |       +-----------+--+
 *  +------------------+     |            |     +------+----------------+  |
 *                           | startup_32 |---->| startup_64_mixed_mode |  |
 *  +------------------+     |            |     +------+----------------+  V
 *  | efi32_stub_entry |---->|            |            |     +------------------+
 *  +------------------+     +------------+            +---->| efi64_stub_entry |
 *                                                           +-------------+----+
 *                           +------------+     +----------+               |
 *                           | startup_64 |<----| efi_main |<--------------+
 *                           +------------+     +----------+
 */
SYM_FUNC_START(startup_64_mixed_mode)
	lea	efi32_boot_args(%rip), %rdx
	mov	0(%rdx), %edi
	mov	4(%rdx), %esi
	mov	8(%rdx), %edx		// saved bootparams pointer
	test	%edx, %edx
	jnz	efi64_stub_entry
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
 *		%esi	struct bootparams pointer (or NULL when not using
 *			the EFI handover protocol)
 *
 * Since this is the point of no return for ordinary execution, no registers
 * are considered live except for the function parameters. [Note that the EFI
 * stub may still exit and return to the firmware using the Exit() EFI boot
 * service.]
 */
SYM_FUNC_START(efi32_entry)
	call	1f
1:	pop	%ebx

	/* Save firmware GDTR and code/data selectors */
	sgdtl	(efi32_boot_gdt - 1b)(%ebx)
	movw	%cs, (efi32_boot_cs - 1b)(%ebx)
	movw	%ds, (efi32_boot_ds - 1b)(%ebx)

	/* Store firmware IDT descriptor */
	sidtl	(efi32_boot_idt - 1b)(%ebx)

	/* Store boot arguments */
	leal	(efi32_boot_args - 1b)(%ebx), %ebx
	movl	%ecx, 0(%ebx)
	movl	%edx, 4(%ebx)
	movl	%esi, 8(%ebx)
	movb	$0x0, 12(%ebx)          // efi_is64

	/* Disable paging */
	movl	%cr0, %eax
	btrl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0

	jmp	startup_32
SYM_FUNC_END(efi32_entry)

#define ST32_boottime		60 // offsetof(efi_system_table_32_t, boottime)
#define BS32_handle_protocol	88 // offsetof(efi_boot_services_32_t, handle_protocol)
#define LI32_image_base		32 // offsetof(efi_loaded_image_32_t, image_base)

/*
 * efi_status_t efi32_pe_entry(efi_handle_t image_handle,
 *			       efi_system_table_32_t *sys_table)
 */
SYM_FUNC_START(efi32_pe_entry)
	pushl	%ebp
	movl	%esp, %ebp
	pushl	%eax				// dummy push to allocate loaded_image

	pushl	%ebx				// save callee-save registers
	pushl	%edi

	call	verify_cpu			// check for long mode support
	testl	%eax, %eax
	movl	$0x80000003, %eax		// EFI_UNSUPPORTED
	jnz	2f

	call	1f
1:	pop	%ebx

	/* Get the loaded image protocol pointer from the image handle */
	leal	-4(%ebp), %eax
	pushl	%eax				// &loaded_image
	leal	(loaded_image_proto - 1b)(%ebx), %eax
	pushl	%eax				// pass the GUID address
	pushl	8(%ebp)				// pass the image handle

	/*
	 * Note the alignment of the stack frame.
	 *   sys_table
	 *   handle             <-- 16-byte aligned on entry by ABI
	 *   return address
	 *   frame pointer
	 *   loaded_image       <-- local variable
	 *   saved %ebx		<-- 16-byte aligned here
	 *   saved %edi
	 *   &loaded_image
	 *   &loaded_image_proto
	 *   handle             <-- 16-byte aligned for call to handle_protocol
	 */

	movl	12(%ebp), %eax			// sys_table
	movl	ST32_boottime(%eax), %eax	// sys_table->boottime
	call	*BS32_handle_protocol(%eax)	// sys_table->boottime->handle_protocol
	addl	$12, %esp			// restore argument space
	testl	%eax, %eax
	jnz	2f

	movl	8(%ebp), %ecx			// image_handle
	movl	12(%ebp), %edx			// sys_table
	movl	-4(%ebp), %esi			// loaded_image
	movl	LI32_image_base(%esi), %esi	// loaded_image->image_base
	leal	(startup_32 - 1b)(%ebx), %ebp	// runtime address of startup_32
	/*
	 * We need to set the image_offset variable here since startup_32() will
	 * use it before we get to the 64-bit efi_pe_entry() in C code.
	 */
	subl	%esi, %ebp			// calculate image_offset
	movl	%ebp, (image_offset - 1b)(%ebx)	// save image_offset
	xorl	%esi, %esi
	jmp	efi32_entry			// pass %ecx, %edx, %esi
						// no other registers remain live

2:	popl	%edi				// restore callee-save registers
	popl	%ebx
	leave
	RET
SYM_FUNC_END(efi32_pe_entry)

	.section ".rodata"
	/* EFI loaded image protocol GUID */
	.balign 4
SYM_DATA_START_LOCAL(loaded_image_proto)
	.long	0x5b1b31a1
	.word	0x9562, 0x11d2
	.byte	0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b
SYM_DATA_END(loaded_image_proto)

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
SYM_DATA_LOCAL(efi32_boot_args, .long 0, 0, 0)
SYM_DATA(efi_is64, .byte 1)
