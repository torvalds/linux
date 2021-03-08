/* SPDX-License-Identifier: GPL-2.0-only */
.text
#include <linux/linkage.h>
#include <linux/objtool.h>
#include <asm/segment.h>
#include <asm/pgtable_types.h>
#include <asm/page_types.h>
#include <asm/msr.h>
#include <asm/asm-offsets.h>
#include <asm/frame.h>
#include <asm/nospec-branch.h>

# Copyright 2003 Pavel Machek <pavel@suse.cz

.code64
	/*
	 * Hooray, we are in Long 64-bit mode (but still running in low memory)
	 */
SYM_FUNC_START(wakeup_long64)
	movq	saved_magic, %rax
	movq	$0x123456789abcdef0, %rdx
	cmpq	%rdx, %rax
	je	2f

	/* stop here on a saved_magic mismatch */
	movq $0xbad6d61676963, %rcx
1:
	jmp 1b
2:
	movw	$__KERNEL_DS, %ax
	movw	%ax, %ss	
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movq	saved_rsp, %rsp

	movq	saved_rbx, %rbx
	movq	saved_rdi, %rdi
	movq	saved_rsi, %rsi
	movq	saved_rbp, %rbp

	movq	saved_rip, %rax
	ANNOTATE_RETPOLINE_SAFE
	jmp	*%rax
SYM_FUNC_END(wakeup_long64)

SYM_FUNC_START(do_suspend_lowlevel)
	FRAME_BEGIN
	subq	$8, %rsp
	xorl	%eax, %eax
	call	save_processor_state

	movq	$saved_context, %rax
	movq	%rsp, pt_regs_sp(%rax)
	movq	%rbp, pt_regs_bp(%rax)
	movq	%rsi, pt_regs_si(%rax)
	movq	%rdi, pt_regs_di(%rax)
	movq	%rbx, pt_regs_bx(%rax)
	movq	%rcx, pt_regs_cx(%rax)
	movq	%rdx, pt_regs_dx(%rax)
	movq	%r8, pt_regs_r8(%rax)
	movq	%r9, pt_regs_r9(%rax)
	movq	%r10, pt_regs_r10(%rax)
	movq	%r11, pt_regs_r11(%rax)
	movq	%r12, pt_regs_r12(%rax)
	movq	%r13, pt_regs_r13(%rax)
	movq	%r14, pt_regs_r14(%rax)
	movq	%r15, pt_regs_r15(%rax)
	pushfq
	popq	pt_regs_flags(%rax)

	movq	$.Lresume_point, saved_rip(%rip)

	movq	%rsp, saved_rsp
	movq	%rbp, saved_rbp
	movq	%rbx, saved_rbx
	movq	%rdi, saved_rdi
	movq	%rsi, saved_rsi

	addq	$8, %rsp
	movl	$3, %edi
	xorl	%eax, %eax
	call	x86_acpi_enter_sleep_state
	/* in case something went wrong, restore the machine status and go on */
	jmp	.Lresume_point

	.align 4
.Lresume_point:
	/* We don't restore %rax, it must be 0 anyway */
	movq	$saved_context, %rax
	movq	saved_context_cr4(%rax), %rbx
	movq	%rbx, %cr4
	movq	saved_context_cr3(%rax), %rbx
	movq	%rbx, %cr3
	movq	saved_context_cr2(%rax), %rbx
	movq	%rbx, %cr2
	movq	saved_context_cr0(%rax), %rbx
	movq	%rbx, %cr0
	pushq	pt_regs_flags(%rax)
	popfq
	movq	pt_regs_sp(%rax), %rsp
	movq	pt_regs_bp(%rax), %rbp
	movq	pt_regs_si(%rax), %rsi
	movq	pt_regs_di(%rax), %rdi
	movq	pt_regs_bx(%rax), %rbx
	movq	pt_regs_cx(%rax), %rcx
	movq	pt_regs_dx(%rax), %rdx
	movq	pt_regs_r8(%rax), %r8
	movq	pt_regs_r9(%rax), %r9
	movq	pt_regs_r10(%rax), %r10
	movq	pt_regs_r11(%rax), %r11
	movq	pt_regs_r12(%rax), %r12
	movq	pt_regs_r13(%rax), %r13
	movq	pt_regs_r14(%rax), %r14
	movq	pt_regs_r15(%rax), %r15

#if defined(CONFIG_KASAN) && CONFIG_KASAN_STACK
	/*
	 * The suspend path may have poisoned some areas deeper in the stack,
	 * which we now need to unpoison.
	 */
	movq	%rsp, %rdi
	call	kasan_unpoison_task_stack_below
#endif

	xorl	%eax, %eax
	addq	$8, %rsp
	FRAME_END
	jmp	restore_processor_state
SYM_FUNC_END(do_suspend_lowlevel)
STACK_FRAME_NON_STANDARD do_suspend_lowlevel

.data
saved_rbp:		.quad	0
saved_rsi:		.quad	0
saved_rdi:		.quad	0
saved_rbx:		.quad	0

saved_rip:		.quad	0
saved_rsp:		.quad	0

SYM_DATA(saved_magic,	.quad	0)
