.text
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/pgtable_types.h>
#include <asm/page_types.h>
#include <asm/msr.h>
#include <asm/asm-offsets.h>

# Copyright 2003 Pavel Machek <pavel@suse.cz>, distribute under GPLv2

.code64
	/*
	 * Hooray, we are in Long 64-bit mode (but still running in low memory)
	 */
ENTRY(wakeup_long64)
	movq	saved_magic, %rax
	movq	$0x123456789abcdef0, %rdx
	cmpq	%rdx, %rax
	jne	bogus_64_magic

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
	jmp	*%rax
ENDPROC(wakeup_long64)

bogus_64_magic:
	jmp	bogus_64_magic

ENTRY(do_suspend_lowlevel)
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

	movq	$resume_point, saved_rip(%rip)

	movq	%rsp, saved_rsp
	movq	%rbp, saved_rbp
	movq	%rbx, saved_rbx
	movq	%rdi, saved_rdi
	movq	%rsi, saved_rsi

	addq	$8, %rsp
	movl	$3, %edi
	xorl	%eax, %eax
	call	acpi_enter_sleep_state
	/* in case something went wrong, restore the machine status and go on */
	jmp	resume_point

	.align 4
resume_point:
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

	xorl	%eax, %eax
	addq	$8, %rsp
	jmp	restore_processor_state
ENDPROC(do_suspend_lowlevel)

.data
ENTRY(saved_rbp)	.quad	0
ENTRY(saved_rsi)	.quad	0
ENTRY(saved_rdi)	.quad	0
ENTRY(saved_rbx)	.quad	0

ENTRY(saved_rip)	.quad	0
ENTRY(saved_rsp)	.quad	0

ENTRY(saved_magic)	.quad	0
