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
#include <asm/desc_defs.h>
#include <asm/msr.h>
#include <asm/page_types.h>
#include <asm/pgtable_types.h>
#include <asm/processor-flags.h>
#include <asm/segment.h>

	.text
	.code32
#ifdef CONFIG_EFI_HANDOVER_PROTOCOL
SYM_FUNC_START(efi32_stub_entry)
	call	1f
1:	popl	%ecx

	/* Clear BSS */
	xorl	%eax, %eax
	leal	(_bss - 1b)(%ecx), %edi
	leal	(_ebss - 1b)(%ecx), %ecx
	subl	%edi, %ecx
	shrl	$2, %ecx
	cld
	rep	stosl

	add	$0x4, %esp		/* Discard return address */
	movl	8(%esp), %ebx		/* struct boot_params pointer */
	jmp	efi32_startup
SYM_FUNC_END(efi32_stub_entry)
#endif

/*
 * Called using a far call from __efi64_thunk() below, using the x86_64 SysV
 * ABI (except for R8/R9 which are inaccessible to 32-bit code - EAX/EBX are
 * used instead).  EBP+16 points to the arguments passed via the stack.
 *
 * The first argument (EDI) is a pointer to the boot service or protocol, to
 * which the remaining arguments are passed, each truncated to 32 bits.
 */
SYM_FUNC_START_LOCAL(efi_enter32)
	/*
	 * Convert x86-64 SysV ABI params to i386 ABI
	 */
	pushl	32(%ebp)	/* Up to 3 args passed via the stack */
	pushl	24(%ebp)
	pushl	16(%ebp)
	pushl	%ebx		/* R9 */
	pushl	%eax		/* R8 */
	pushl	%ecx
	pushl	%edx
	pushl	%esi

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

	call	efi32_enable_long_mode

	addl	$32, %esp
	movl	%edi, %eax
	lret
SYM_FUNC_END(efi_enter32)

	.code64
SYM_FUNC_START(__efi64_thunk)
	push	%rbp
	movl	%esp, %ebp
	push	%rbx

	/* Move args #5 and #6 into 32-bit accessible registers */
	movl	%r8d, %eax
	movl	%r9d, %ebx

	lcalll	*efi32_call(%rip)

	pop	%rbx
	pop	%rbp
	RET
SYM_FUNC_END(__efi64_thunk)

	.code32
SYM_FUNC_START_LOCAL(efi32_enable_long_mode)
	movl	%cr4, %eax
	btsl	$(X86_CR4_PAE_BIT), %eax
	movl	%eax, %cr4

	movl	$MSR_EFER, %ecx
	rdmsr
	btsl	$_EFER_LME, %eax
	wrmsr

	/* Disable interrupts - the firmware's IDT does not work in long mode */
	cli

	/* Enable paging */
	movl	%cr0, %eax
	btsl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0
	ret
SYM_FUNC_END(efi32_enable_long_mode)

/*
 * This is the common EFI stub entry point for mixed mode. It sets up the GDT
 * and page tables needed for 64-bit execution, after which it calls the
 * common 64-bit EFI entrypoint efi_stub_entry().
 *
 * Arguments:	0(%esp)	image handle
 * 		4(%esp)	EFI system table pointer
 *		%ebx	struct boot_params pointer (or NULL)
 *
 * Since this is the point of no return for ordinary execution, no registers
 * are considered live except for the function parameters. [Note that the EFI
 * stub may still exit and return to the firmware using the Exit() EFI boot
 * service.]
 */
SYM_FUNC_START_LOCAL(efi32_startup)
	movl	%esp, %ebp

	subl	$8, %esp
	sgdtl	(%esp)			/* Save GDT descriptor to the stack */
	movl	2(%esp), %esi		/* Existing GDT pointer */
	movzwl	(%esp), %ecx		/* Existing GDT limit */
	inc	%ecx			/* Existing GDT size */
	andl	$~7, %ecx		/* Ensure size is multiple of 8 */

	subl	%ecx, %esp		/* Allocate new GDT */
	andl	$~15, %esp		/* Realign the stack */
	movl	%esp, %edi		/* New GDT address */
	leal	7(%ecx), %eax		/* New GDT limit */
	pushw	%cx			/* Push 64-bit CS (for LJMP below) */
	pushl	%edi			/* Push new GDT address */
	pushw	%ax			/* Push new GDT limit */

	/* Copy GDT to the stack and add a 64-bit code segment at the end */
	movl	$GDT_ENTRY(DESC_CODE64, 0, 0xfffff) & 0xffffffff, (%edi,%ecx)
	movl	$GDT_ENTRY(DESC_CODE64, 0, 0xfffff) >> 32, 4(%edi,%ecx)
	shrl	$2, %ecx
	cld
	rep	movsl			/* Copy the firmware GDT */
	lgdtl	(%esp)			/* Switch to the new GDT */

	call	1f
1:	pop	%edi

	/* Record mixed mode entry */
	movb	$0x0, (efi_is64 - 1b)(%edi)

	/* Set up indirect far call to re-enter 32-bit mode */
	leal	(efi32_call - 1b)(%edi), %eax
	addl	%eax, (%eax)
	movw	%cs, 4(%eax)

	/* Disable paging */
	movl	%cr0, %eax
	btrl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0

	/* Set up 1:1 mapping */
	leal	(pte - 1b)(%edi), %eax
	movl	$_PAGE_PRESENT | _PAGE_RW | _PAGE_PSE, %ecx
	leal	(_PAGE_PRESENT | _PAGE_RW)(%eax), %edx
2:	movl	%ecx, (%eax)
	addl	$8, %eax
	addl	$PMD_SIZE, %ecx
	jnc	2b

	movl	$PAGE_SIZE, %ecx
	.irpc	l, 0123
	movl	%edx, \l * 8(%eax)
	addl	%ecx, %edx
	.endr
	addl	%ecx, %eax
	movl	%edx, (%eax)
	movl	%eax, %cr3

	call	efi32_enable_long_mode

	/* Set up far jump to 64-bit mode (CS is already on the stack) */
	leal	(efi_stub_entry - 1b)(%edi), %eax
	movl	%eax, 2(%esp)

	movl	0(%ebp), %edi
	movl	4(%ebp), %esi
	movl	%ebx, %edx
	ljmpl	*2(%esp)
SYM_FUNC_END(efi32_startup)

/*
 * efi_status_t efi32_pe_entry(efi_handle_t image_handle,
 *			       efi_system_table_32_t *sys_table)
 */
SYM_FUNC_START(efi32_pe_entry)
	pushl	%ebx				// save callee-save registers

	/* Check whether the CPU supports long mode */
	movl	$0x80000001, %eax		// assume extended info support
	cpuid
	btl	$29, %edx			// check long mode bit
	jnc	1f
	leal	8(%esp), %esp			// preserve stack alignment
	xor	%ebx, %ebx			// no struct boot_params pointer
	jmp	efi32_startup			// only ESP and EBX remain live
1:	movl	$0x80000003, %eax		// EFI_UNSUPPORTED
	popl	%ebx
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
SYM_DATA_START_LOCAL(efi32_call)
	.long	efi_enter32 - .
	.word	0x0
SYM_DATA_END(efi32_call)
SYM_DATA(efi_is64, .byte 1)

	.bss
	.balign PAGE_SIZE
SYM_DATA_LOCAL(pte, .fill 6 * PAGE_SIZE, 1, 0)
