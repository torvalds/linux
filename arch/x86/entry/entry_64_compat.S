/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Compatibility mode system call entry point for x86-64.
 *
 * Copyright 2000-2002 Andi Kleen, SuSE Labs.
 */
#include "calling.h"
#include <asm/asm-offsets.h>
#include <asm/current.h>
#include <asm/errno.h>
#include <asm/ia32_unistd.h>
#include <asm/thread_info.h>
#include <asm/segment.h>
#include <asm/irqflags.h>
#include <asm/asm.h>
#include <asm/smap.h>
#include <linux/linkage.h>
#include <linux/err.h>

	.section .entry.text, "ax"

/*
 * 32-bit SYSENTER entry.
 *
 * 32-bit system calls through the vDSO's __kernel_vsyscall enter here
 * on 64-bit kernels running on Intel CPUs.
 *
 * The SYSENTER instruction, in principle, should *only* occur in the
 * vDSO.  In practice, a small number of Android devices were shipped
 * with a copy of Bionic that inlined a SYSENTER instruction.  This
 * never happened in any of Google's Bionic versions -- it only happened
 * in a narrow range of Intel-provided versions.
 *
 * SYSENTER loads SS, RSP, CS, and RIP from previously programmed MSRs.
 * IF and VM in RFLAGS are cleared (IOW: interrupts are off).
 * SYSENTER does not save anything on the stack,
 * and does not save old RIP (!!!), RSP, or RFLAGS.
 *
 * Arguments:
 * eax  system call number
 * ebx  arg1
 * ecx  arg2
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * ebp  user stack
 * 0(%ebp) arg6
 */
ENTRY(entry_SYSENTER_compat)
	/* Interrupts are off on entry. */
	SWAPGS

	/* We are about to clobber %rsp anyway, clobbering here is OK */
	SWITCH_TO_KERNEL_CR3 scratch_reg=%rsp

	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rsp

	/*
	 * User tracing code (ptrace or signal handlers) might assume that
	 * the saved RAX contains a 32-bit number when we're invoking a 32-bit
	 * syscall.  Just in case the high bits are nonzero, zero-extend
	 * the syscall number.  (This could almost certainly be deleted
	 * with no ill effects.)
	 */
	movl	%eax, %eax

	/* Construct struct pt_regs on stack */
	pushq	$__USER32_DS		/* pt_regs->ss */
	pushq	%rbp			/* pt_regs->sp (stashed in bp) */

	/*
	 * Push flags.  This is nasty.  First, interrupts are currently
	 * off, but we need pt_regs->flags to have IF set.  Second, even
	 * if TF was set when SYSENTER started, it's clear by now.  We fix
	 * that later using TIF_SINGLESTEP.
	 */
	pushfq				/* pt_regs->flags (except IF = 0) */
	orl	$X86_EFLAGS_IF, (%rsp)	/* Fix saved flags */
	pushq	$__USER32_CS		/* pt_regs->cs */
	pushq	$0			/* pt_regs->ip = 0 (placeholder) */
	pushq	%rax			/* pt_regs->orig_ax */
	pushq	%rdi			/* pt_regs->di */
	pushq	%rsi			/* pt_regs->si */
	pushq	%rdx			/* pt_regs->dx */
	pushq	%rcx			/* pt_regs->cx */
	pushq	$-ENOSYS		/* pt_regs->ax */
	pushq   $0			/* pt_regs->r8  = 0 */
	pushq   $0			/* pt_regs->r9  = 0 */
	pushq   $0			/* pt_regs->r10 = 0 */
	pushq   $0			/* pt_regs->r11 = 0 */
	pushq   %rbx                    /* pt_regs->rbx */
	pushq   %rbp                    /* pt_regs->rbp (will be overwritten) */
	pushq   $0			/* pt_regs->r12 = 0 */
	pushq   $0			/* pt_regs->r13 = 0 */
	pushq   $0			/* pt_regs->r14 = 0 */
	pushq   $0			/* pt_regs->r15 = 0 */
	cld

	/*
	 * SYSENTER doesn't filter flags, so we need to clear NT and AC
	 * ourselves.  To save a few cycles, we can check whether
	 * either was set instead of doing an unconditional popfq.
	 * This needs to happen before enabling interrupts so that
	 * we don't get preempted with NT set.
	 *
	 * If TF is set, we will single-step all the way to here -- do_debug
	 * will ignore all the traps.  (Yes, this is slow, but so is
	 * single-stepping in general.  This allows us to avoid having
	 * a more complicated code to handle the case where a user program
	 * forces us to single-step through the SYSENTER entry code.)
	 *
	 * NB.: .Lsysenter_fix_flags is a label with the code under it moved
	 * out-of-line as an optimization: NT is unlikely to be set in the
	 * majority of the cases and instead of polluting the I$ unnecessarily,
	 * we're keeping that code behind a branch which will predict as
	 * not-taken and therefore its instructions won't be fetched.
	 */
	testl	$X86_EFLAGS_NT|X86_EFLAGS_AC|X86_EFLAGS_TF, EFLAGS(%rsp)
	jnz	.Lsysenter_fix_flags
.Lsysenter_flags_fixed:

	/*
	 * User mode is traced as though IRQs are on, and SYSENTER
	 * turned them off.
	 */
	TRACE_IRQS_OFF

	movq	%rsp, %rdi
	call	do_fast_syscall_32
	/* XEN PV guests always use IRET path */
	ALTERNATIVE "testl %eax, %eax; jz .Lsyscall_32_done", \
		    "jmp .Lsyscall_32_done", X86_FEATURE_XENPV
	jmp	sysret32_from_system_call

.Lsysenter_fix_flags:
	pushq	$X86_EFLAGS_FIXED
	popfq
	jmp	.Lsysenter_flags_fixed
GLOBAL(__end_entry_SYSENTER_compat)
ENDPROC(entry_SYSENTER_compat)

/*
 * 32-bit SYSCALL entry.
 *
 * 32-bit system calls through the vDSO's __kernel_vsyscall enter here
 * on 64-bit kernels running on AMD CPUs.
 *
 * The SYSCALL instruction, in principle, should *only* occur in the
 * vDSO.  In practice, it appears that this really is the case.
 * As evidence:
 *
 *  - The calling convention for SYSCALL has changed several times without
 *    anyone noticing.
 *
 *  - Prior to the in-kernel X86_BUG_SYSRET_SS_ATTRS fixup, anything
 *    user task that did SYSCALL without immediately reloading SS
 *    would randomly crash.
 *
 *  - Most programmers do not directly target AMD CPUs, and the 32-bit
 *    SYSCALL instruction does not exist on Intel CPUs.  Even on AMD
 *    CPUs, Linux disables the SYSCALL instruction on 32-bit kernels
 *    because the SYSCALL instruction in legacy/native 32-bit mode (as
 *    opposed to compat mode) is sufficiently poorly designed as to be
 *    essentially unusable.
 *
 * 32-bit SYSCALL saves RIP to RCX, clears RFLAGS.RF, then saves
 * RFLAGS to R11, then loads new SS, CS, and RIP from previously
 * programmed MSRs.  RFLAGS gets masked by a value from another MSR
 * (so CLD and CLAC are not needed).  SYSCALL does not save anything on
 * the stack and does not change RSP.
 *
 * Note: RFLAGS saving+masking-with-MSR happens only in Long mode
 * (in legacy 32-bit mode, IF, RF and VM bits are cleared and that's it).
 * Don't get confused: RFLAGS saving+masking depends on Long Mode Active bit
 * (EFER.LMA=1), NOT on bitness of userspace where SYSCALL executes
 * or target CS descriptor's L bit (SYSCALL does not read segment descriptors).
 *
 * Arguments:
 * eax  system call number
 * ecx  return address
 * ebx  arg1
 * ebp  arg2	(note: not saved in the stack frame, should not be touched)
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * esp  user stack
 * 0(%esp) arg6
 */
ENTRY(entry_SYSCALL_compat)
	/* Interrupts are off on entry. */
	swapgs

	/* Stash user ESP */
	movl	%esp, %r8d

	/* Use %rsp as scratch reg. User ESP is stashed in r8 */
	SWITCH_TO_KERNEL_CR3 scratch_reg=%rsp

	/* Switch to the kernel stack */
	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rsp

	/* Construct struct pt_regs on stack */
	pushq	$__USER32_DS		/* pt_regs->ss */
	pushq	%r8			/* pt_regs->sp */
	pushq	%r11			/* pt_regs->flags */
	pushq	$__USER32_CS		/* pt_regs->cs */
	pushq	%rcx			/* pt_regs->ip */
GLOBAL(entry_SYSCALL_compat_after_hwframe)
	movl	%eax, %eax		/* discard orig_ax high bits */
	pushq	%rax			/* pt_regs->orig_ax */
	pushq	%rdi			/* pt_regs->di */
	pushq	%rsi			/* pt_regs->si */
	pushq	%rdx			/* pt_regs->dx */
	pushq	%rbp			/* pt_regs->cx (stashed in bp) */
	pushq	$-ENOSYS		/* pt_regs->ax */
	pushq   $0			/* pt_regs->r8  = 0 */
	pushq   $0			/* pt_regs->r9  = 0 */
	pushq   $0			/* pt_regs->r10 = 0 */
	pushq   $0			/* pt_regs->r11 = 0 */
	pushq   %rbx                    /* pt_regs->rbx */
	pushq   %rbp                    /* pt_regs->rbp (will be overwritten) */
	pushq   $0			/* pt_regs->r12 = 0 */
	pushq   $0			/* pt_regs->r13 = 0 */
	pushq   $0			/* pt_regs->r14 = 0 */
	pushq   $0			/* pt_regs->r15 = 0 */

	/*
	 * User mode is traced as though IRQs are on, and SYSENTER
	 * turned them off.
	 */
	TRACE_IRQS_OFF

	movq	%rsp, %rdi
	call	do_fast_syscall_32
	/* XEN PV guests always use IRET path */
	ALTERNATIVE "testl %eax, %eax; jz .Lsyscall_32_done", \
		    "jmp .Lsyscall_32_done", X86_FEATURE_XENPV

	/* Opportunistic SYSRET */
sysret32_from_system_call:
	TRACE_IRQS_ON			/* User mode traces as IRQs on. */
	movq	RBX(%rsp), %rbx		/* pt_regs->rbx */
	movq	RBP(%rsp), %rbp		/* pt_regs->rbp */
	movq	EFLAGS(%rsp), %r11	/* pt_regs->flags (in r11) */
	movq	RIP(%rsp), %rcx		/* pt_regs->ip (in rcx) */
	addq	$RAX, %rsp		/* Skip r8-r15 */
	popq	%rax			/* pt_regs->rax */
	popq	%rdx			/* Skip pt_regs->cx */
	popq	%rdx			/* pt_regs->dx */
	popq	%rsi			/* pt_regs->si */
	popq	%rdi			/* pt_regs->di */

        /*
         * USERGS_SYSRET32 does:
         *  GSBASE = user's GS base
         *  EIP = ECX
         *  RFLAGS = R11
         *  CS = __USER32_CS
         *  SS = __USER_DS
         *
	 * ECX will not match pt_regs->cx, but we're returning to a vDSO
	 * trampoline that will fix up RCX, so this is okay.
	 *
	 * R12-R15 are callee-saved, so they contain whatever was in them
	 * when the system call started, which is already known to user
	 * code.  We zero R8-R10 to avoid info leaks.
         */
	movq	RSP-ORIG_RAX(%rsp), %rsp

	/*
	 * The original userspace %rsp (RSP-ORIG_RAX(%rsp)) is stored
	 * on the process stack which is not mapped to userspace and
	 * not readable after we SWITCH_TO_USER_CR3.  Delay the CR3
	 * switch until after after the last reference to the process
	 * stack.
	 *
	 * %r8/%r9 are zeroed before the sysret, thus safe to clobber.
	 */
	SWITCH_TO_USER_CR3_NOSTACK scratch_reg=%r8 scratch_reg2=%r9

	xorq	%r8, %r8
	xorq	%r9, %r9
	xorq	%r10, %r10
	swapgs
	sysretl
END(entry_SYSCALL_compat)

/*
 * 32-bit legacy system call entry.
 *
 * 32-bit x86 Linux system calls traditionally used the INT $0x80
 * instruction.  INT $0x80 lands here.
 *
 * This entry point can be used by 32-bit and 64-bit programs to perform
 * 32-bit system calls.  Instances of INT $0x80 can be found inline in
 * various programs and libraries.  It is also used by the vDSO's
 * __kernel_vsyscall fallback for hardware that doesn't support a faster
 * entry method.  Restarted 32-bit system calls also fall back to INT
 * $0x80 regardless of what instruction was originally used to do the
 * system call.
 *
 * This is considered a slow path.  It is not used by most libc
 * implementations on modern hardware except during process startup.
 *
 * Arguments:
 * eax  system call number
 * ebx  arg1
 * ecx  arg2
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * ebp  arg6
 */
ENTRY(entry_INT80_compat)
	/*
	 * Interrupts are off on entry.
	 */
	ASM_CLAC			/* Do this early to minimize exposure */
	SWAPGS

	/*
	 * User tracing code (ptrace or signal handlers) might assume that
	 * the saved RAX contains a 32-bit number when we're invoking a 32-bit
	 * syscall.  Just in case the high bits are nonzero, zero-extend
	 * the syscall number.  (This could almost certainly be deleted
	 * with no ill effects.)
	 */
	movl	%eax, %eax

	pushq	%rax			/* pt_regs->orig_ax */

	/* switch to thread stack expects orig_ax to be pushed */
	call	switch_to_thread_stack

	pushq	%rdi			/* pt_regs->di */
	pushq	%rsi			/* pt_regs->si */
	pushq	%rdx			/* pt_regs->dx */
	pushq	%rcx			/* pt_regs->cx */
	pushq	$-ENOSYS		/* pt_regs->ax */
	pushq   $0			/* pt_regs->r8  = 0 */
	pushq   $0			/* pt_regs->r9  = 0 */
	pushq   $0			/* pt_regs->r10 = 0 */
	pushq   $0			/* pt_regs->r11 = 0 */
	pushq   %rbx                    /* pt_regs->rbx */
	pushq   %rbp                    /* pt_regs->rbp */
	pushq   %r12                    /* pt_regs->r12 */
	pushq   %r13                    /* pt_regs->r13 */
	pushq   %r14                    /* pt_regs->r14 */
	pushq   %r15                    /* pt_regs->r15 */
	cld

	/*
	 * User mode is traced as though IRQs are on, and the interrupt
	 * gate turned them off.
	 */
	TRACE_IRQS_OFF

	movq	%rsp, %rdi
	call	do_int80_syscall_32
.Lsyscall_32_done:

	/* Go back to user mode. */
	TRACE_IRQS_ON
	jmp	swapgs_restore_regs_and_return_to_usermode
END(entry_INT80_compat)

ENTRY(stub32_clone)
	/*
	 * The 32-bit clone ABI is: clone(..., int tls_val, int *child_tidptr).
	 * The 64-bit clone ABI is: clone(..., int *child_tidptr, int tls_val).
	 *
	 * The native 64-bit kernel's sys_clone() implements the latter,
	 * so we need to swap arguments here before calling it:
	 */
	xchg	%r8, %rcx
	jmp	sys_clone
ENDPROC(stub32_clone)
