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
SYM_DATA(kexec_debug_8250_mmio32, .quad 0)
SYM_DATA(kexec_debug_8250_port, .word 0)

	.balign 16
SYM_DATA_START_LOCAL(kexec_debug_gdt)
	.word   kexec_debug_gdt_end - kexec_debug_gdt - 1
	.long   0
	.word   0
	.quad   0x00cf9a000000ffff      /* __KERNEL32_CS */
	.quad   0x00af9a000000ffff      /* __KERNEL_CS */
	.quad   0x00cf92000000ffff      /* __KERNEL_DS */
SYM_DATA_END_LABEL(kexec_debug_gdt, SYM_L_LOCAL, kexec_debug_gdt_end)

	.balign 8
SYM_DATA_START(kexec_debug_idt)
	.skip 0x100, 0x00
SYM_DATA_END(kexec_debug_idt)

	.section .text..relocate_kernel,"ax";
	.code64
SYM_CODE_START_NOALIGN(relocate_kernel)
	UNWIND_HINT_END_OF_STACK
	ANNOTATE_NOENDBR
	/*
	 * %rdi indirection_page
	 * %rsi pa_control_page
	 * %rdx start address
	 * %rcx flags: RELOC_KERNEL_*
	 */

	/* Save the CPU context, used for jumping back */
	pushq %rbx
	pushq %rbp
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	pushf

	/* Invalidate GDT/IDT, zero out flags */
	pushq	$0
	pushq	$0

	lidt	(%rsp)
	lgdt	(%rsp)
	addq	$8, %rsp
	popfq

	/* Switch to the identity mapped page tables */
	movq	%cr3, %rax
	movq	kexec_pa_table_page(%rip), %r9
	movq	%r9, %cr3

	/* Leave CR4 in %r13 to enable the right paging mode later. */
	movq	%cr4, %r13

	/*
	 * Disable global pages immediately to ensure this mapping is RWX.
	 * Disable LASS before jumping to the identity mapped page.
	 */
	movq	%r13, %r12
	andq	$~(X86_CR4_PGE | X86_CR4_LASS), %r12
	movq	%r12, %cr4

	/* Save %rsp and CRs. */
	movq	%r13, saved_cr4(%rip)
	movq    %rsp, saved_rsp(%rip)
	movq	%rax, saved_cr3(%rip)
	movq	%cr0, %rax
	movq	%rax, saved_cr0(%rip)

	/* save indirection list for jumping back */
	movq	%rdi, pa_backup_pages_map(%rip)

	/* Save the flags to %r11 as swap_pages clobbers %rcx. */
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
	 * %r9 page table page
	 * %r11 flags: RELOC_KERNEL_*
	 * %r13 original CR4 when relocate_kernel() was invoked
	 */

	/* store the start address on the stack */
	pushq   %rdx

	/* Create a GDTR (16 bits limit, 64 bits addr) on stack */
	leaq	kexec_debug_gdt(%rip), %rax
	pushq	%rax
	pushw	(%rax)

	/* Load the GDT, put the stack back */
	lgdt	(%rsp)
	addq	$10, %rsp

	/* Test that we can load segments */
	movq	%ds, %rax
	movq	%rax, %ds

	/* Now an IDTR on the stack to load the IDT the kernel created */
	leaq	kexec_debug_idt(%rip), %rsi
	pushq	%rsi
	pushw	$0xff
	lidt	(%rsp)
	addq	$10, %rsp

	//int3

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
	 * If the memory cache is in incoherent state, e.g., due to
	 * memory encryption, do WBINVD to flush cache.
	 *
	 * If SME is active, there could be old encrypted cache line
	 * entries that will conflict with the now unencrypted memory
	 * used by kexec. Flush the caches before copying the kernel.
	 *
	 * Note SME sets this flag to true when the platform supports
	 * SME, so the WBINVD is performed even SME is not activated
	 * by the kernel.  But this has no harm.
	 */
	testb	$RELOC_KERNEL_CACHE_INCOHERENT, %r11b
	jz .Lnowbinvd
	wbinvd
.Lnowbinvd:

	call	swap_pages

	/*
	 * To be certain of avoiding problems with self-modifying code
	 * I need to execute a serializing instruction here.
	 * So I flush the TLB by reloading %cr3 here, it's handy,
	 * and not processor dependent.
	 */
	movq	%cr3, %rax
	movq	%rax, %cr3

	testb	$RELOC_KERNEL_PRESERVE_CONTEXT, %r11b
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
	/*
	 * Ensure RELOC_KERNEL_PRESERVE_CONTEXT flag is set so that
	 * swap_pages() can swap pages correctly.  Note all other
	 * RELOC_KERNEL_* flags passed to relocate_kernel() are not
	 * restored.
	 */
	movl	$RELOC_KERNEL_PRESERVE_CONTEXT, %r11d
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
	 * %r11 flags: RELOC_KERNEL_*
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

	/* Only actually swap for ::preserve_context */
	testb	$RELOC_KERNEL_PRESERVE_CONTEXT, %r11b
	jz	.Lnoswap

	/* copy source page to swap page */
	movq	kexec_pa_swap_page(%rip), %rdi
	movl	$512, %ecx
	rep movsq

	/* copy destination page to source page */
	movq	%rax, %rdi
	movq	%rdx, %rsi
	movl	$512, %ecx
	rep movsq

	/* copy swap page to destination page */
	movq	%rdx, %rdi
	movq	kexec_pa_swap_page(%rip), %rsi
.Lnoswap:
	movl	$512, %ecx
	rep movsq

	lea	PAGE_SIZE(%rax), %rsi
	jmp	.Lloop
.Ldone:
	ANNOTATE_UNRET_SAFE
	ret
	int3
SYM_CODE_END(swap_pages)

/*
 * Generic 'print character' routine
 *  - %al: Character to be printed (may clobber %rax)
 *  - %rdx: MMIO address or port.
 */
#define XMTRDY          0x20

#define TXR             0       /*  Transmit register (WRITE) */
#define LSR             5       /*  Line Status               */

SYM_CODE_START_LOCAL_NOALIGN(pr_char_8250)
	UNWIND_HINT_FUNC
	ANNOTATE_NOENDBR
	addw	$LSR, %dx
	xchg	%al, %ah
.Lxmtrdy_loop:
	inb	%dx, %al
	testb	$XMTRDY, %al
	jnz	.Lready
	pause
	jmp .Lxmtrdy_loop

.Lready:
	subw	$LSR, %dx
	xchg	%al, %ah
	outb	%al, %dx
pr_char_null:
	ANNOTATE_NOENDBR

	ANNOTATE_UNRET_SAFE
	ret
SYM_CODE_END(pr_char_8250)

SYM_CODE_START_LOCAL_NOALIGN(pr_char_8250_mmio32)
	UNWIND_HINT_FUNC
	ANNOTATE_NOENDBR
.Lxmtrdy_loop_mmio:
	movb	(LSR*4)(%rdx), %ah
	testb	$XMTRDY, %ah
	jnz	.Lready_mmio
	pause
	jmp .Lxmtrdy_loop_mmio

.Lready_mmio:
	movb	%al, (%rdx)
	ANNOTATE_UNRET_SAFE
	ret
SYM_CODE_END(pr_char_8250_mmio32)

/*
 * Load pr_char function pointer into %rsi and load %rdx with whatever
 * that function wants to see there (typically port/MMIO address).
 */
.macro pr_setup
	leaq	pr_char_8250(%rip), %rsi
	movw	kexec_debug_8250_port(%rip), %dx
	testw	%dx, %dx
	jnz	1f

	leaq	pr_char_8250_mmio32(%rip), %rsi
	movq	kexec_debug_8250_mmio32(%rip), %rdx
	testq	%rdx, %rdx
	jnz	1f

	leaq	pr_char_null(%rip), %rsi
1:
.endm

/* Print the nybble in %bl, clobber %rax */
SYM_CODE_START_LOCAL_NOALIGN(pr_nybble)
	UNWIND_HINT_FUNC
	movb	%bl, %al
	nop
	andb	$0x0f, %al
	addb	$0x30, %al
	cmpb	$0x3a, %al
	jb	1f
	addb	$('a' - '0' - 10), %al
	ANNOTATE_RETPOLINE_SAFE
1:	jmp	*%rsi
SYM_CODE_END(pr_nybble)

SYM_CODE_START_LOCAL_NOALIGN(pr_qword)
	UNWIND_HINT_FUNC
	movq	$16, %rcx
1:	rolq	$4, %rbx
	call	pr_nybble
	loop	1b
	movb	$'\n', %al
	ANNOTATE_RETPOLINE_SAFE
	jmp	*%rsi
SYM_CODE_END(pr_qword)

.macro print_reg a, b, c, d, r
	movb	$\a, %al
	ANNOTATE_RETPOLINE_SAFE
	call	*%rsi
	movb	$\b, %al
	ANNOTATE_RETPOLINE_SAFE
	call	*%rsi
	movb	$\c, %al
	ANNOTATE_RETPOLINE_SAFE
	call	*%rsi
	movb	$\d, %al
	ANNOTATE_RETPOLINE_SAFE
	call	*%rsi
	movq	\r, %rbx
	call	pr_qword
.endm

SYM_CODE_START_NOALIGN(kexec_debug_exc_vectors)
	/* Each of these is 6 bytes. */
.macro vec_err exc
	UNWIND_HINT_ENTRY
	. = kexec_debug_exc_vectors + (\exc * KEXEC_DEBUG_EXC_HANDLER_SIZE)
	nop
	nop
	pushq	$\exc
	jmp	exc_handler
.endm

.macro vec_noerr exc
	UNWIND_HINT_ENTRY
	. = kexec_debug_exc_vectors + (\exc * KEXEC_DEBUG_EXC_HANDLER_SIZE)
	pushq	$0
	pushq	$\exc
	jmp	exc_handler
.endm

	ANNOTATE_NOENDBR
	vec_noerr  0 // #DE
	vec_noerr  1 // #DB
	vec_noerr  2 // #NMI
	vec_noerr  3 // #BP
	vec_noerr  4 // #OF
	vec_noerr  5 // #BR
	vec_noerr  6 // #UD
	vec_noerr  7 // #NM
	vec_err    8 // #DF
	vec_noerr  9
	vec_err   10 // #TS
	vec_err   11 // #NP
	vec_err   12 // #SS
	vec_err   13 // #GP
	vec_err   14 // #PF
	vec_noerr 15
SYM_CODE_END(kexec_debug_exc_vectors)

SYM_CODE_START_LOCAL_NOALIGN(exc_handler)
	/* No need for RET mitigations during kexec */
	VALIDATE_UNRET_END

	pushq	%rax
	pushq	%rbx
	pushq	%rcx
	pushq	%rdx
	pushq	%rsi

	/* Stack frame */
#define EXC_SS		0x58 /* Architectural... */
#define EXC_RSP		0x50
#define EXC_EFLAGS	0x48
#define EXC_CS		0x40
#define EXC_RIP		0x38
#define EXC_ERRORCODE	0x30 /* Either architectural or zero pushed by handler */
#define EXC_EXCEPTION	0x28 /* Pushed by handler entry point */
#define EXC_RAX		0x20 /* Pushed just above in exc_handler */
#define EXC_RBX		0x18
#define EXC_RCX		0x10
#define EXC_RDX		0x08
#define EXC_RSI		0x00

	/* Set up %rdx/%rsi for debug output */
	pr_setup

	/* rip and exception info */
	print_reg 'E', 'x', 'c', ':', EXC_EXCEPTION(%rsp)
	print_reg 'E', 'r', 'r', ':', EXC_ERRORCODE(%rsp)
	print_reg 'r', 'i', 'p', ':', EXC_RIP(%rsp)
	print_reg 'r', 's', 'p', ':', EXC_RSP(%rsp)

	/* We spilled these to the stack */
	print_reg 'r', 'a', 'x', ':', EXC_RAX(%rsp)
	print_reg 'r', 'b', 'x', ':', EXC_RBX(%rsp)
	print_reg 'r', 'c', 'x', ':', EXC_RCX(%rsp)
	print_reg 'r', 'd', 'x', ':', EXC_RDX(%rsp)
	print_reg 'r', 's', 'i', ':', EXC_RSI(%rsp)

	/* Other registers untouched */
	print_reg 'r', 'd', 'i', ':', %rdi
	print_reg 'r', '8', ' ', ':', %r8
	print_reg 'r', '9', ' ', ':', %r9
	print_reg 'r', '1', '0', ':', %r10
	print_reg 'r', '1', '1', ':', %r11
	print_reg 'r', '1', '2', ':', %r12
	print_reg 'r', '1', '3', ':', %r13
	print_reg 'r', '1', '4', ':', %r14
	print_reg 'r', '1', '5', ':', %r15
	print_reg 'c', 'r', '2', ':', %cr2

	/* Only return from INT3 */
	cmpq	$3, EXC_EXCEPTION(%rsp)
	jne	.Ldie

	popq	%rsi
	popq	%rdx
	popq	%rcx
	popq	%rbx
	popq	%rax

	addq	$16, %rsp
	iretq

.Ldie:
	hlt
	jmp	.Ldie

SYM_CODE_END(exc_handler)
