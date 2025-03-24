/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * relocate_kernel.S - put the kernel image in place to boot
 * Copyright (C) 2002-2005 Eric Biederman  <ebiederm@xmission.com>
 */

#include <linux/linkage.h>
#include <linux/stringify.h>
#include <asm/alternative.h>
#include <asm/page_types.h>
#include <asm/kexec.h>
#include <asm/processor-flags.h>
#include <asm/pgtable_types.h>
#include <asm/nospec-branch.h>
#include <asm/unwind_hints.h>
#include <asm/asm-offsets.h>

/*
 * Must be relocatable PIC code callable as a C function, in particular
 * there must be a plain RET and not jump to return thunk.
 */

#define PTR(x) (x << 3)
#define PAGE_ATTR (_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED | _PAGE_DIRTY)

/*
 * The .text..relocate_kernel and .data..relocate_kernel sections are copied
 * into the control page, and the remainder of the page is used as the stack.
 */

	.section .data..relocate_kernel,"a";
/* Minimal CPU state */
SYM_DATA_LOCAL(saved_rsp, .quad 0)
SYM_DATA_LOCAL(saved_cr0, .quad 0)
SYM_DATA_LOCAL(saved_cr3, .quad 0)
SYM_DATA_LOCAL(saved_cr4, .quad 0)
	/* other data */
SYM_DATA(kexec_va_control_page, .quad 0)
SYM_DATA(kexec_pa_table_page, .quad 0)
SYM_DATA(kexec_pa_swap_page, .quad 0)
SYM_DATA_LOCAL(pa_backup_pages_map, .quad 0)

	.section .text..relocate_kernel,"ax";
	.code64
SYM_CODE_START_NOALIGN(relocate_kernel)
	UNWIND_HINT_END_OF_STACK
	ANNOTATE_NOENDBR
	/*
	 * %rdi indirection_page
	 * %rsi pa_control_page
	 * %rdx start address
	 * %rcx preserve_context
	 * %r8  host_mem_enc_active
	 */

	/* Save the CPU context, used for jumping back */
	pushq %rbx
	pushq %rbp
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	pushf

	/* zero out flags, and disable interrupts */
	pushq $0
	popfq

	/* Switch to the identity mapped page tables */
	movq	%cr3, %rax
	movq	kexec_pa_table_page(%rip), %r9
	movq	%r9, %cr3

	/* Leave CR4 in %r13 to enable the right paging mode later. */
	movq	%cr4, %r13

	/* Disable global pages immediately to ensure this mapping is RWX */
	movq	%r13, %r12
	andq	$~(X86_CR4_PGE), %r12
	movq	%r12, %cr4

	/* Save %rsp and CRs. */
	movq	%r13, saved_cr4(%rip)
	movq    %rsp, saved_rsp(%rip)
	movq	%rax, saved_cr3(%rip)
	movq	%cr0, %rax
	movq	%rax, saved_cr0(%rip)

	/* save indirection list for jumping back */
	movq	%rdi, pa_backup_pages_map(%rip)

	/* Save the preserve_context to %r11 as swap_pages clobbers %rcx. */
	movq	%rcx, %r11

	/* setup a new stack at the end of the physical control page */
	lea	PAGE_SIZE(%rsi), %rsp

	/* jump to identity mapped page */
0:	addq	$identity_mapped - 0b, %rsi
	subq	$__relocate_kernel_start - 0b, %rsi
	ANNOTATE_RETPOLINE_SAFE
	jmp	*%rsi
SYM_CODE_END(relocate_kernel)

SYM_CODE_START_LOCAL_NOALIGN(identity_mapped)
	UNWIND_HINT_END_OF_STACK
	/*
	 * %rdi	indirection page
	 * %rdx start address
	 * %r8 host_mem_enc_active
	 * %r9 page table page
	 * %r11 preserve_context
	 * %r13 original CR4 when relocate_kernel() was invoked
	 */

	/* store the start address on the stack */
	pushq   %rdx

	/*
	 * Clear X86_CR4_CET (if it was set) such that we can clear CR0_WP
	 * below.
	 */
	movq	%cr4, %rax
	andq	$~(X86_CR4_CET), %rax
	movq	%rax, %cr4

	/*
	 * Set cr0 to a known state:
	 *  - Paging enabled
	 *  - Alignment check disabled
	 *  - Write protect disabled
	 *  - No task switch
	 *  - Don't do FP software emulation.
	 *  - Protected mode enabled
	 */
	movq	%cr0, %rax
	andq	$~(X86_CR0_AM | X86_CR0_WP | X86_CR0_TS | X86_CR0_EM), %rax
	orl	$(X86_CR0_PG | X86_CR0_PE), %eax
	movq	%rax, %cr0

	/*
	 * Set cr4 to a known state:
	 *  - physical address extension enabled
	 *  - 5-level paging, if it was enabled before
	 *  - Machine check exception on TDX guest, if it was enabled before.
	 *    Clearing MCE might not be allowed in TDX guests, depending on setup.
	 *
	 * Use R13 that contains the original CR4 value, read in relocate_kernel().
	 * PAE is always set in the original CR4.
	 */
	andl	$(X86_CR4_PAE | X86_CR4_LA57), %r13d
	ALTERNATIVE "", __stringify(orl $X86_CR4_MCE, %r13d), X86_FEATURE_TDX_GUEST
	movq	%r13, %cr4

	/* Flush the TLB (needed?) */
	movq	%r9, %cr3

	/*
	 * If SME is active, there could be old encrypted cache line
	 * entries that will conflict with the now unencrypted memory
	 * used by kexec. Flush the caches before copying the kernel.
	 */
	testq	%r8, %r8
	jz .Lsme_off
	wbinvd
.Lsme_off:

	call	swap_pages

	/*
	 * To be certain of avoiding problems with self-modifying code
	 * I need to execute a serializing instruction here.
	 * So I flush the TLB by reloading %cr3 here, it's handy,
	 * and not processor dependent.
	 */
	movq	%cr3, %rax
	movq	%rax, %cr3

	testq	%r11, %r11	/* preserve_context */
	jnz .Lrelocate

	/*
	 * set all of the registers to known values
	 * leave %rsp alone
	 */

	xorl	%eax, %eax
	xorl	%ebx, %ebx
	xorl    %ecx, %ecx
	xorl    %edx, %edx
	xorl    %esi, %esi
	xorl    %edi, %edi
	xorl    %ebp, %ebp
	xorl	%r8d, %r8d
	xorl	%r9d, %r9d
	xorl	%r10d, %r10d
	xorl	%r11d, %r11d
	xorl	%r12d, %r12d
	xorl	%r13d, %r13d
	xorl	%r14d, %r14d
	xorl	%r15d, %r15d

	ANNOTATE_UNRET_SAFE
	ret
	int3

.Lrelocate:
	popq	%rdx

	/* Use the swap page for the callee's stack */
	movq	kexec_pa_swap_page(%rip), %r10
	leaq	PAGE_SIZE(%r10), %rsp

	/* push the existing entry point onto the callee's stack */
	pushq	%rdx

	ANNOTATE_RETPOLINE_SAFE
	call	*%rdx

	/* get the re-entry point of the peer system */
	popq	%rbp
	movq	kexec_pa_swap_page(%rip), %r10
	movq	pa_backup_pages_map(%rip), %rdi
	movq	kexec_pa_table_page(%rip), %rax
	movq	%rax, %cr3

	/* Find start (and end) of this physical mapping of control page */
	leaq	(%rip), %r8
	ANNOTATE_NOENDBR
	andq	$PAGE_MASK, %r8
	lea	PAGE_SIZE(%r8), %rsp
	movl	$1, %r11d	/* Ensure preserve_context flag is set */
	call	swap_pages
	movq	kexec_va_control_page(%rip), %rax
0:	addq	$virtual_mapped - 0b, %rax
	subq	$__relocate_kernel_start - 0b, %rax
	pushq	%rax
	ANNOTATE_UNRET_SAFE
	ret
	int3
SYM_CODE_END(identity_mapped)

SYM_CODE_START_LOCAL_NOALIGN(virtual_mapped)
	UNWIND_HINT_END_OF_STACK
	ANNOTATE_NOENDBR // RET target, above
	movq	saved_rsp(%rip), %rsp
	movq	saved_cr4(%rip), %rax
	movq	%rax, %cr4
	movq	saved_cr3(%rip), %rax
	movq	saved_cr0(%rip), %r8
	movq	%rax, %cr3
	movq	%r8, %cr0

#ifdef CONFIG_KEXEC_JUMP
	/* Saved in save_processor_state. */
	movq    $saved_context, %rax
	lgdt    saved_context_gdt_desc(%rax)
#endif

	/* relocate_kernel() returns the re-entry point for next time */
	movq	%rbp, %rax

	popf
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbp
	popq	%rbx
	ANNOTATE_UNRET_SAFE
	ret
	int3
SYM_CODE_END(virtual_mapped)

	/* Do the copies */
SYM_CODE_START_LOCAL_NOALIGN(swap_pages)
	UNWIND_HINT_END_OF_STACK
	/*
	 * %rdi indirection page
	 * %r11 preserve_context
	 */
	movq	%rdi, %rcx	/* Put the indirection_page in %rcx */
	xorl	%edi, %edi
	xorl	%esi, %esi
	jmp	.Lstart		/* Should start with an indirection record */

.Lloop:	/* top, read another word for the indirection page */

	movq	(%rbx), %rcx
	addq	$8,	%rbx
.Lstart:
	testb	$0x1,	%cl   /* is it a destination page? */
	jz	.Lnotdest
	movq	%rcx,	%rdi
	andq	$0xfffffffffffff000, %rdi
	jmp	.Lloop
.Lnotdest:
	testb	$0x2,	%cl   /* is it an indirection page? */
	jz	.Lnotind
	movq	%rcx,   %rbx
	andq	$0xfffffffffffff000, %rbx
	jmp	.Lloop
.Lnotind:
	testb	$0x4,	%cl   /* is it the done indicator? */
	jz	.Lnotdone
	jmp	.Ldone
.Lnotdone:
	testb	$0x8,	%cl   /* is it the source indicator? */
	jz	.Lloop	      /* Ignore it otherwise */
	movq	%rcx,   %rsi  /* For ever source page do a copy */
	andq	$0xfffffffffffff000, %rsi

	movq	%rdi, %rdx    /* Save destination page to %rdx */
	movq	%rsi, %rax    /* Save source page to %rax */

	testq	%r11, %r11    /* Only actually swap for ::preserve_context */
	jz	.Lnoswap

	/* copy source page to swap page */
	movq	kexec_pa_swap_page(%rip), %rdi
	movl	$512, %ecx
	rep ; movsq

	/* copy destination page to source page */
	movq	%rax, %rdi
	movq	%rdx, %rsi
	movl	$512, %ecx
	rep ; movsq

	/* copy swap page to destination page */
	movq	%rdx, %rdi
	movq	kexec_pa_swap_page(%rip), %rsi
.Lnoswap:
	movl	$512, %ecx
	rep ; movsq

	lea	PAGE_SIZE(%rax), %rsi
	jmp	.Lloop
.Ldone:
	ANNOTATE_UNRET_SAFE
	ret
	int3
SYM_CODE_END(swap_pages)
