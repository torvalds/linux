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
#include <asm/kaiser.h>
#include <linux/linkage.h>
#include <linux/err.h>

	.section .entry.text, "ax"

#ifdef CONFIG_PARAVIRT
ENTRY(native_usergs_sysret32)
	swapgs
	sysretl
ENDPROC(native_usergs_sysret32)
#endif

/*
 * 32-bit SYSENTER instruction entry.
 *
 * SYSENTER loads ss, rsp, cs, and rip from previously programmed MSRs.
 * IF and VM in rflags are cleared (IOW: interrupts are off).
 * SYSENTER does not save anything on the stack,
 * and does not save old rip (!!!) and rflags.
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
 *
 * This is purely a fast path. For anything complicated we use the int 0x80
 * path below. We set up a complete hardware stack frame to share code
 * with the int 0x80 path.
 */
ENTRY(entry_SYSENTER_compat)
	/* Interrupts are off on entry. */
	SWAPGS_UNSAFE_STACK
	SWITCH_KERNEL_CR3_NO_STACK
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
	ASM_CLAC			/* Clear AC after saving FLAGS */

	pushq	$__USER32_CS		/* pt_regs->cs */
	xorq    %r8,%r8
	pushq	%r8			/* pt_regs->ip = 0 (placeholder) */
	pushq	%rax			/* pt_regs->orig_ax */
	pushq	%rdi			/* pt_regs->di */
	pushq	%rsi			/* pt_regs->si */
	pushq	%rdx			/* pt_regs->dx */
	pushq	%rcx			/* pt_regs->cx */
	pushq	$-ENOSYS		/* pt_regs->ax */
	pushq   %r8                     /* pt_regs->r8  = 0 */
	pushq   %r8                     /* pt_regs->r9  = 0 */
	pushq   %r8                     /* pt_regs->r10 = 0 */
	pushq   %r8                     /* pt_regs->r11 = 0 */
	pushq   %rbx                    /* pt_regs->rbx */
	pushq   %rbp                    /* pt_regs->rbp (will be overwritten) */
	pushq   %r8                     /* pt_regs->r12 = 0 */
	pushq   %r8                     /* pt_regs->r13 = 0 */
	pushq   %r8                     /* pt_regs->r14 = 0 */
	pushq   %r8                     /* pt_regs->r15 = 0 */
	cld

	/*
	 * Sysenter doesn't filter flags, so we need to clear NT
	 * ourselves.  To save a few cycles, we can check whether
	 * NT was set instead of doing an unconditional popfq.
	 * This needs to happen before enabling interrupts so that
	 * we don't get preempted with NT set.
	 *
	 * NB.: sysenter_fix_flags is a label with the code under it moved
	 * out-of-line as an optimization: NT is unlikely to be set in the
	 * majority of the cases and instead of polluting the I$ unnecessarily,
	 * we're keeping that code behind a branch which will predict as
	 * not-taken and therefore its instructions won't be fetched.
	 */
	testl	$X86_EFLAGS_NT, EFLAGS(%rsp)
	jnz	sysenter_fix_flags
sysenter_flags_fixed:

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

sysenter_fix_flags:
	pushq	$X86_EFLAGS_FIXED
	popfq
	jmp	sysenter_flags_fixed
ENDPROC(entry_SYSENTER_compat)

/*
 * 32-bit SYSCALL instruction entry.
 *
 * 32-bit SYSCALL saves rip to rcx, clears rflags.RF, then saves rflags to r11,
 * then loads new ss, cs, and rip from previously programmed MSRs.
 * rflags gets masked by a value from another MSR (so CLD and CLAC
 * are not needed). SYSCALL does not save anything on the stack
 * and does not change rsp.
 *
 * Note: rflags saving+masking-with-MSR happens only in Long mode
 * (in legacy 32-bit mode, IF, RF and VM bits are cleared and that's it).
 * Don't get confused: rflags saving+masking depends on Long Mode Active bit
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
	SWAPGS_UNSAFE_STACK
	SWITCH_KERNEL_CR3_NO_STACK

	/* Stash user ESP and switch to the kernel stack. */
	movl	%esp, %r8d
	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rsp

	/* Zero-extending 32-bit regs, do not remove */
	movl	%eax, %eax

	/* Construct struct pt_regs on stack */
	pushq	$__USER32_DS		/* pt_regs->ss */
	pushq	%r8			/* pt_regs->sp */
	pushq	%r11			/* pt_regs->flags */
	pushq	$__USER32_CS		/* pt_regs->cs */
	pushq	%rcx			/* pt_regs->ip */
	pushq	%rax			/* pt_regs->orig_ax */
	pushq	%rdi			/* pt_regs->di */
	pushq	%rsi			/* pt_regs->si */
	pushq	%rdx			/* pt_regs->dx */
	pushq	%rbp			/* pt_regs->cx (stashed in bp) */
	pushq	$-ENOSYS		/* pt_regs->ax */
	xorq    %r8,%r8
	pushq   %r8                     /* pt_regs->r8  = 0 */
	pushq   %r8                     /* pt_regs->r9  = 0 */
	pushq   %r8                     /* pt_regs->r10 = 0 */
	pushq   %r8                     /* pt_regs->r11 = 0 */
	pushq   %rbx                    /* pt_regs->rbx */
	pushq   %rbp                    /* pt_regs->rbp (will be overwritten) */
	pushq   %r8                     /* pt_regs->r12 = 0 */
	pushq   %r8                     /* pt_regs->r13 = 0 */
	pushq   %r8                     /* pt_regs->r14 = 0 */
	pushq   %r8                     /* pt_regs->r15 = 0 */

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
	SWITCH_USER_CR3
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
	xorq	%r8, %r8
	xorq	%r9, %r9
	xorq	%r10, %r10
	movq	RSP-ORIG_RAX(%rsp), %rsp
        USERGS_SYSRET32
END(entry_SYSCALL_compat)

/*
 * Emulated IA32 system calls via int 0x80.
 *
 * Arguments:
 * eax  system call number
 * ebx  arg1
 * ecx  arg2
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * ebp  arg6	(note: not saved in the stack frame, should not be touched)
 *
 * Notes:
 * Uses the same stack frame as the x86-64 version.
 * All registers except eax must be saved (but ptrace may violate that).
 * Arguments are zero extended. For system calls that want sign extension and
 * take long arguments a wrapper is needed. Most calls can just be called
 * directly.
 * Assumes it is only called from user space and entered with interrupts off.
 */

ENTRY(entry_INT80_compat)
	/*
	 * Interrupts are off on entry.
	 */
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	ASM_CLAC			/* Do this early to minimize exposure */
	SWAPGS
	SWITCH_KERNEL_CR3_NO_STACK

	/*
	 * User tracing code (ptrace or signal handlers) might assume that
	 * the saved RAX contains a 32-bit number when we're invoking a 32-bit
	 * syscall.  Just in case the high bits are nonzero, zero-extend
	 * the syscall number.  (This could almost certainly be deleted
	 * with no ill effects.)
	 */
	movl	%eax, %eax

	/* Construct struct pt_regs on stack (iret frame is already on stack) */
	pushq	%rax			/* pt_regs->orig_ax */
	pushq	%rdi			/* pt_regs->di */
	pushq	%rsi			/* pt_regs->si */
	pushq	%rdx			/* pt_regs->dx */
	pushq	%rcx			/* pt_regs->cx */
	pushq	$-ENOSYS		/* pt_regs->ax */
	xorq    %r8,%r8
	pushq   %r8                     /* pt_regs->r8  = 0 */
	pushq   %r8                     /* pt_regs->r9  = 0 */
	pushq   %r8                     /* pt_regs->r10 = 0 */
	pushq   %r8                     /* pt_regs->r11 = 0 */
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
	call	do_syscall_32_irqs_off
.Lsyscall_32_done:

	/* Go back to user mode. */
	TRACE_IRQS_ON
	SWITCH_USER_CR3
	SWAPGS
	jmp	restore_regs_and_iret
END(entry_INT80_compat)

	ALIGN
GLOBAL(stub32_clone)
	/*
	 * The 32-bit clone ABI is: clone(..., int tls_val, int *child_tidptr).
	 * The 64-bit clone ABI is: clone(..., int *child_tidptr, int tls_val).
	 *
	 * The native 64-bit kernel's sys_clone() implements the latter,
	 * so we need to swap arguments here before calling it:
	 */
	xchg	%r8, %rcx
	jmp	sys_clone
