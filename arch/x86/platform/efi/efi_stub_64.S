/*
 * Function calling ABI conversion from Linux to EFI for x86_64
 *
 * Copyright (C) 2007 Intel Corp
 *	Bibo Mao <bibo.mao@intel.com>
 *	Huang Ying <ying.huang@intel.com>
 */

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/msr.h>
#include <asm/processor-flags.h>
#include <asm/page_types.h>

#define SAVE_XMM			\
	mov %rsp, %rax;			\
	subq $0x70, %rsp;		\
	and $~0xf, %rsp;		\
	mov %rax, (%rsp);		\
	mov %cr0, %rax;			\
	clts;				\
	mov %rax, 0x8(%rsp);		\
	movaps %xmm0, 0x60(%rsp);	\
	movaps %xmm1, 0x50(%rsp);	\
	movaps %xmm2, 0x40(%rsp);	\
	movaps %xmm3, 0x30(%rsp);	\
	movaps %xmm4, 0x20(%rsp);	\
	movaps %xmm5, 0x10(%rsp)

#define RESTORE_XMM			\
	movaps 0x60(%rsp), %xmm0;	\
	movaps 0x50(%rsp), %xmm1;	\
	movaps 0x40(%rsp), %xmm2;	\
	movaps 0x30(%rsp), %xmm3;	\
	movaps 0x20(%rsp), %xmm4;	\
	movaps 0x10(%rsp), %xmm5;	\
	mov 0x8(%rsp), %rsi;		\
	mov %rsi, %cr0;			\
	mov (%rsp), %rsp

	/* stolen from gcc */
	.macro FLUSH_TLB_ALL
	movq %r15, efi_scratch(%rip)
	movq %r14, efi_scratch+8(%rip)
	movq %cr4, %r15
	movq %r15, %r14
	andb $0x7f, %r14b
	movq %r14, %cr4
	movq %r15, %cr4
	movq efi_scratch+8(%rip), %r14
	movq efi_scratch(%rip), %r15
	.endm

	.macro SWITCH_PGT
	cmpb $0, efi_scratch+24(%rip)
	je 1f
	movq %r15, efi_scratch(%rip)		# r15
	# save previous CR3
	movq %cr3, %r15
	movq %r15, efi_scratch+8(%rip)		# prev_cr3
	movq efi_scratch+16(%rip), %r15		# EFI pgt
	movq %r15, %cr3
	1:
	.endm

	.macro RESTORE_PGT
	cmpb $0, efi_scratch+24(%rip)
	je 2f
	movq efi_scratch+8(%rip), %r15
	movq %r15, %cr3
	movq efi_scratch(%rip), %r15
	FLUSH_TLB_ALL
	2:
	.endm

ENTRY(efi_call)
	SAVE_XMM
	mov (%rsp), %rax
	mov 8(%rax), %rax
	subq $48, %rsp
	mov %r9, 32(%rsp)
	mov %rax, 40(%rsp)
	mov %r8, %r9
	mov %rcx, %r8
	mov %rsi, %rcx
	SWITCH_PGT
	call *%rdi
	RESTORE_PGT
	addq $48, %rsp
	RESTORE_XMM
	ret
ENDPROC(efi_call)

#ifdef CONFIG_EFI_MIXED

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

	sgdt	save_gdt(%rip)

	leaq	1f(%rip), %rbx
	movq	%rbx, func_rt_ptr(%rip)

	/* Switch to gdt with 32-bit segments */
	movl	64(%rsp), %eax
	lgdt	(%rax)

	leaq	efi_enter32(%rip), %rax
	pushq	$__KERNEL_CS
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

	movl	68(%esp), %eax
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

	movl	72(%esp), %eax
	pushl	$__KERNEL_CS
	pushl	%eax

	/* Enable paging */
	movl	%cr0, %eax
	btsl	$X86_CR0_PG_BIT, %eax
	movl	%eax, %cr0
	lret
ENDPROC(efi_enter32)

	.data
	.balign	8
	.global	efi32_boot_gdt
efi32_boot_gdt:	.word	0
		.quad	0

save_gdt:	.word	0
		.quad	0
func_rt_ptr:	.quad	0

	.global efi_gdt64
efi_gdt64:
	.word	efi_gdt64_end - efi_gdt64
	.long	0			/* Filled out by user */
	.word	0
	.quad	0x0000000000000000	/* NULL descriptor */
	.quad	0x00af9a000000ffff	/* __KERNEL_CS */
	.quad	0x00cf92000000ffff	/* __KERNEL_DS */
	.quad	0x0080890000000000	/* TS descriptor */
	.quad   0x0000000000000000	/* TS continued */
efi_gdt64_end:
#endif /* CONFIG_EFI_MIXED */

	.data
ENTRY(efi_scratch)
	.fill 3,8,0
	.byte 0
	.quad 0
