/*
 *  linux/arch/x86_64/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002  Andi Kleen SuSE Labs
 *  Copyright (C) 2000  Pavel Machek <pavel@suse.cz>
 *
 * entry.S contains the system-call and fault low-level handling routines.
 *
 * Some of this is documented in Documentation/x86/entry_64.txt
 *
 * A note on terminology:
 * - iret frame:	Architecture defined interrupt frame from SS to RIP
 *			at the top of the kernel process stack.
 *
 * Some macro usage:
 * - ENTRY/END:		Define functions in the symbol table.
 * - TRACE_IRQ_*:	Trace hardirq state for lock debugging.
 * - idtentry:		Define exception entry points.
 */
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include "calling.h"
#include <asm/asm-offsets.h>
#include <asm/msr.h>
#include <asm/unistd.h>
#include <asm/thread_info.h>
#include <asm/hw_irq.h>
#include <asm/page_types.h>
#include <asm/irqflags.h>
#include <asm/paravirt.h>
#include <asm/percpu.h>
#include <asm/asm.h>
#include <asm/smap.h>
#include <asm/pgtable_types.h>
#include <asm/kaiser.h>
#include <linux/err.h>

/* Avoid __ASSEMBLER__'ifying <linux/audit.h> just for this.  */
#include <linux/elf-em.h>
#define AUDIT_ARCH_X86_64			(EM_X86_64|__AUDIT_ARCH_64BIT|__AUDIT_ARCH_LE)
#define __AUDIT_ARCH_64BIT			0x80000000
#define __AUDIT_ARCH_LE				0x40000000

.code64
.section .entry.text, "ax"

#ifdef CONFIG_PARAVIRT
ENTRY(native_usergs_sysret64)
	swapgs
	sysretq
ENDPROC(native_usergs_sysret64)
#endif /* CONFIG_PARAVIRT */

.macro TRACE_IRQS_IRETQ
#ifdef CONFIG_TRACE_IRQFLAGS
	bt	$9, EFLAGS(%rsp)		/* interrupts off? */
	jnc	1f
	TRACE_IRQS_ON
1:
#endif
.endm

/*
 * When dynamic function tracer is enabled it will add a breakpoint
 * to all locations that it is about to modify, sync CPUs, update
 * all the code, sync CPUs, then remove the breakpoints. In this time
 * if lockdep is enabled, it might jump back into the debug handler
 * outside the updating of the IST protection. (TRACE_IRQS_ON/OFF).
 *
 * We need to change the IDT table before calling TRACE_IRQS_ON/OFF to
 * make sure the stack pointer does not get reset back to the top
 * of the debug stack, and instead just reuses the current stack.
 */
#if defined(CONFIG_DYNAMIC_FTRACE) && defined(CONFIG_TRACE_IRQFLAGS)

.macro TRACE_IRQS_OFF_DEBUG
	call	debug_stack_set_zero
	TRACE_IRQS_OFF
	call	debug_stack_reset
.endm

.macro TRACE_IRQS_ON_DEBUG
	call	debug_stack_set_zero
	TRACE_IRQS_ON
	call	debug_stack_reset
.endm

.macro TRACE_IRQS_IRETQ_DEBUG
	bt	$9, EFLAGS(%rsp)		/* interrupts off? */
	jnc	1f
	TRACE_IRQS_ON_DEBUG
1:
.endm

#else
# define TRACE_IRQS_OFF_DEBUG			TRACE_IRQS_OFF
# define TRACE_IRQS_ON_DEBUG			TRACE_IRQS_ON
# define TRACE_IRQS_IRETQ_DEBUG			TRACE_IRQS_IRETQ
#endif

/*
 * 64-bit SYSCALL instruction entry. Up to 6 arguments in registers.
 *
 * 64-bit SYSCALL saves rip to rcx, clears rflags.RF, then saves rflags to r11,
 * then loads new ss, cs, and rip from previously programmed MSRs.
 * rflags gets masked by a value from another MSR (so CLD and CLAC
 * are not needed). SYSCALL does not save anything on the stack
 * and does not change rsp.
 *
 * Registers on entry:
 * rax  system call number
 * rcx  return address
 * r11  saved rflags (note: r11 is callee-clobbered register in C ABI)
 * rdi  arg0
 * rsi  arg1
 * rdx  arg2
 * r10  arg3 (needs to be moved to rcx to conform to C ABI)
 * r8   arg4
 * r9   arg5
 * (note: r12-r15, rbp, rbx are callee-preserved in C ABI)
 *
 * Only called from user space.
 *
 * When user can change pt_regs->foo always force IRET. That is because
 * it deals with uncanonical addresses better. SYSRET has trouble
 * with them due to bugs in both AMD and Intel CPUs.
 */

ENTRY(entry_SYSCALL_64)
	/*
	 * Interrupts are off on entry.
	 * We do not frame this tiny irq-off block with TRACE_IRQS_OFF/ON,
	 * it is too small to ever cause noticeable irq latency.
	 */
	SWAPGS_UNSAFE_STACK
	SWITCH_KERNEL_CR3_NO_STACK
	/*
	 * A hypervisor implementation might want to use a label
	 * after the swapgs, so that it can do the swapgs
	 * for the guest and jump here on syscall.
	 */
GLOBAL(entry_SYSCALL_64_after_swapgs)

	movq	%rsp, PER_CPU_VAR(rsp_scratch)
	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rsp

	/* Construct struct pt_regs on stack */
	pushq	$__USER_DS			/* pt_regs->ss */
	pushq	PER_CPU_VAR(rsp_scratch)	/* pt_regs->sp */
	/*
	 * Re-enable interrupts.
	 * We use 'rsp_scratch' as a scratch space, hence irq-off block above
	 * must execute atomically in the face of possible interrupt-driven
	 * task preemption. We must enable interrupts only after we're done
	 * with using rsp_scratch:
	 */
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushq	%r11				/* pt_regs->flags */
	pushq	$__USER_CS			/* pt_regs->cs */
	pushq	%rcx				/* pt_regs->ip */
	pushq	%rax				/* pt_regs->orig_ax */
	pushq	%rdi				/* pt_regs->di */
	pushq	%rsi				/* pt_regs->si */
	pushq	%rdx				/* pt_regs->dx */
	pushq	%rcx				/* pt_regs->cx */
	pushq	$-ENOSYS			/* pt_regs->ax */
	pushq	%r8				/* pt_regs->r8 */
	pushq	%r9				/* pt_regs->r9 */
	pushq	%r10				/* pt_regs->r10 */
	pushq	%r11				/* pt_regs->r11 */
	sub	$(6*8), %rsp			/* pt_regs->bp, bx, r12-15 not saved */

	testl	$_TIF_WORK_SYSCALL_ENTRY, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz	tracesys
entry_SYSCALL_64_fastpath:
#if __SYSCALL_MASK == ~0
	cmpq	$__NR_syscall_max, %rax
#else
	andl	$__SYSCALL_MASK, %eax
	cmpl	$__NR_syscall_max, %eax
#endif
	ja	1f				/* return -ENOSYS (already in pt_regs->ax) */
	movq	%r10, %rcx
	call	*sys_call_table(, %rax, 8)
	movq	%rax, RAX(%rsp)
1:
/*
 * Syscall return path ending with SYSRET (fast path).
 * Has incompletely filled pt_regs.
 */
	LOCKDEP_SYS_EXIT
	/*
	 * We do not frame this tiny irq-off block with TRACE_IRQS_OFF/ON,
	 * it is too small to ever cause noticeable irq latency.
	 */
	DISABLE_INTERRUPTS(CLBR_NONE)

	/*
	 * We must check ti flags with interrupts (or at least preemption)
	 * off because we must *never* return to userspace without
	 * processing exit work that is enqueued if we're preempted here.
	 * In particular, returning to userspace with any of the one-shot
	 * flags (TIF_NOTIFY_RESUME, TIF_USER_RETURN_NOTIFY, etc) set is
	 * very bad.
	 */
	testl	$_TIF_ALLWORK_MASK, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz	int_ret_from_sys_call_irqs_off	/* Go to the slow path */

	movq	RIP(%rsp), %rcx
	movq	EFLAGS(%rsp), %r11
	RESTORE_C_REGS_EXCEPT_RCX_R11
	/*
	 * This opens a window where we have a user CR3, but are
	 * running in the kernel.  This makes using the CS
	 * register useless for telling whether or not we need to
	 * switch CR3 in NMIs.  Normal interrupts are OK because
	 * they are off here.
	 */
	SWITCH_USER_CR3
	movq	RSP(%rsp), %rsp
	/*
	 * 64-bit SYSRET restores rip from rcx,
	 * rflags from r11 (but RF and VM bits are forced to 0),
	 * cs and ss are loaded from MSRs.
	 * Restoration of rflags re-enables interrupts.
	 *
	 * NB: On AMD CPUs with the X86_BUG_SYSRET_SS_ATTRS bug, the ss
	 * descriptor is not reinitialized.  This means that we should
	 * avoid SYSRET with SS == NULL, which could happen if we schedule,
	 * exit the kernel, and re-enter using an interrupt vector.  (All
	 * interrupt entries on x86_64 set SS to NULL.)  We prevent that
	 * from happening by reloading SS in __switch_to.  (Actually
	 * detecting the failure in 64-bit userspace is tricky but can be
	 * done.)
	 */
	USERGS_SYSRET64

GLOBAL(int_ret_from_sys_call_irqs_off)
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	jmp int_ret_from_sys_call

	/* Do syscall entry tracing */
tracesys:
	movq	%rsp, %rdi
	movl	$AUDIT_ARCH_X86_64, %esi
	call	syscall_trace_enter_phase1
	test	%rax, %rax
	jnz	tracesys_phase2			/* if needed, run the slow path */
	RESTORE_C_REGS_EXCEPT_RAX		/* else restore clobbered regs */
	movq	ORIG_RAX(%rsp), %rax
	jmp	entry_SYSCALL_64_fastpath	/* and return to the fast path */

tracesys_phase2:
	SAVE_EXTRA_REGS
	movq	%rsp, %rdi
	movl	$AUDIT_ARCH_X86_64, %esi
	movq	%rax, %rdx
	call	syscall_trace_enter_phase2

	/*
	 * Reload registers from stack in case ptrace changed them.
	 * We don't reload %rax because syscall_trace_entry_phase2() returned
	 * the value it wants us to use in the table lookup.
	 */
	RESTORE_C_REGS_EXCEPT_RAX
	RESTORE_EXTRA_REGS
#if __SYSCALL_MASK == ~0
	cmpq	$__NR_syscall_max, %rax
#else
	andl	$__SYSCALL_MASK, %eax
	cmpl	$__NR_syscall_max, %eax
#endif
	ja	1f				/* return -ENOSYS (already in pt_regs->ax) */
	movq	%r10, %rcx			/* fixup for C */
	call	*sys_call_table(, %rax, 8)
	movq	%rax, RAX(%rsp)
1:
	/* Use IRET because user could have changed pt_regs->foo */

/*
 * Syscall return path ending with IRET.
 * Has correct iret frame.
 */
GLOBAL(int_ret_from_sys_call)
	SAVE_EXTRA_REGS
	movq	%rsp, %rdi
	call	syscall_return_slowpath	/* returns with IRQs disabled */
	RESTORE_EXTRA_REGS
	TRACE_IRQS_IRETQ		/* we're about to change IF */

	/*
	 * Try to use SYSRET instead of IRET if we're returning to
	 * a completely clean 64-bit userspace context.
	 */
	movq	RCX(%rsp), %rcx
	movq	RIP(%rsp), %r11
	cmpq	%rcx, %r11			/* RCX == RIP */
	jne	opportunistic_sysret_failed

	/*
	 * On Intel CPUs, SYSRET with non-canonical RCX/RIP will #GP
	 * in kernel space.  This essentially lets the user take over
	 * the kernel, since userspace controls RSP.
	 *
	 * If width of "canonical tail" ever becomes variable, this will need
	 * to be updated to remain correct on both old and new CPUs.
	 */
	.ifne __VIRTUAL_MASK_SHIFT - 47
	.error "virtual address width changed -- SYSRET checks need update"
	.endif

	/* Change top 16 bits to be the sign-extension of 47th bit */
	shl	$(64 - (__VIRTUAL_MASK_SHIFT+1)), %rcx
	sar	$(64 - (__VIRTUAL_MASK_SHIFT+1)), %rcx

	/* If this changed %rcx, it was not canonical */
	cmpq	%rcx, %r11
	jne	opportunistic_sysret_failed

	cmpq	$__USER_CS, CS(%rsp)		/* CS must match SYSRET */
	jne	opportunistic_sysret_failed

	movq	R11(%rsp), %r11
	cmpq	%r11, EFLAGS(%rsp)		/* R11 == RFLAGS */
	jne	opportunistic_sysret_failed

	/*
	 * SYSRET can't restore RF.  SYSRET can restore TF, but unlike IRET,
	 * restoring TF results in a trap from userspace immediately after
	 * SYSRET.  This would cause an infinite loop whenever #DB happens
	 * with register state that satisfies the opportunistic SYSRET
	 * conditions.  For example, single-stepping this user code:
	 *
	 *           movq	$stuck_here, %rcx
	 *           pushfq
	 *           popq %r11
	 *   stuck_here:
	 *
	 * would never get past 'stuck_here'.
	 */
	testq	$(X86_EFLAGS_RF|X86_EFLAGS_TF), %r11
	jnz	opportunistic_sysret_failed

	/* nothing to check for RSP */

	cmpq	$__USER_DS, SS(%rsp)		/* SS must match SYSRET */
	jne	opportunistic_sysret_failed

	/*
	 * We win! This label is here just for ease of understanding
	 * perf profiles. Nothing jumps here.
	 */
syscall_return_via_sysret:
	/* rcx and r11 are already restored (see code above) */
	RESTORE_C_REGS_EXCEPT_RCX_R11
	/*
	 * This opens a window where we have a user CR3, but are
	 * running in the kernel.  This makes using the CS
	 * register useless for telling whether or not we need to
	 * switch CR3 in NMIs.  Normal interrupts are OK because
	 * they are off here.
	 */
	SWITCH_USER_CR3
	movq	RSP(%rsp), %rsp
	USERGS_SYSRET64

opportunistic_sysret_failed:
	/*
	 * This opens a window where we have a user CR3, but are
	 * running in the kernel.  This makes using the CS
	 * register useless for telling whether or not we need to
	 * switch CR3 in NMIs.  Normal interrupts are OK because
	 * they are off here.
	 */
	SWITCH_USER_CR3
	SWAPGS
	jmp	restore_c_regs_and_iret
END(entry_SYSCALL_64)


	.macro FORK_LIKE func
ENTRY(stub_\func)
	SAVE_EXTRA_REGS 8
	jmp	sys_\func
END(stub_\func)
	.endm

	FORK_LIKE  clone
	FORK_LIKE  fork
	FORK_LIKE  vfork

ENTRY(stub_execve)
	call	sys_execve
return_from_execve:
	testl	%eax, %eax
	jz	1f
	/* exec failed, can use fast SYSRET code path in this case */
	ret
1:
	/* must use IRET code path (pt_regs->cs may have changed) */
	addq	$8, %rsp
	ZERO_EXTRA_REGS
	movq	%rax, RAX(%rsp)
	jmp	int_ret_from_sys_call
END(stub_execve)
/*
 * Remaining execve stubs are only 7 bytes long.
 * ENTRY() often aligns to 16 bytes, which in this case has no benefits.
 */
	.align	8
GLOBAL(stub_execveat)
	call	sys_execveat
	jmp	return_from_execve
END(stub_execveat)

#if defined(CONFIG_X86_X32_ABI)
	.align	8
GLOBAL(stub_x32_execve)
	call	compat_sys_execve
	jmp	return_from_execve
END(stub_x32_execve)
	.align	8
GLOBAL(stub_x32_execveat)
	call	compat_sys_execveat
	jmp	return_from_execve
END(stub_x32_execveat)
#endif

/*
 * sigreturn is special because it needs to restore all registers on return.
 * This cannot be done with SYSRET, so use the IRET return path instead.
 */
ENTRY(stub_rt_sigreturn)
	/*
	 * SAVE_EXTRA_REGS result is not normally needed:
	 * sigreturn overwrites all pt_regs->GPREGS.
	 * But sigreturn can fail (!), and there is no easy way to detect that.
	 * To make sure RESTORE_EXTRA_REGS doesn't restore garbage on error,
	 * we SAVE_EXTRA_REGS here.
	 */
	SAVE_EXTRA_REGS 8
	call	sys_rt_sigreturn
return_from_stub:
	addq	$8, %rsp
	RESTORE_EXTRA_REGS
	movq	%rax, RAX(%rsp)
	jmp	int_ret_from_sys_call
END(stub_rt_sigreturn)

#ifdef CONFIG_X86_X32_ABI
ENTRY(stub_x32_rt_sigreturn)
	SAVE_EXTRA_REGS 8
	call	sys32_x32_rt_sigreturn
	jmp	return_from_stub
END(stub_x32_rt_sigreturn)
#endif

/*
 * A newly forked process directly context switches into this address.
 *
 * rdi: prev task we switched from
 */
ENTRY(ret_from_fork)

	LOCK ; btr $TIF_FORK, TI_flags(%r8)

	pushq	$0x0002
	popfq					/* reset kernel eflags */

	call	schedule_tail			/* rdi: 'prev' task parameter */

	RESTORE_EXTRA_REGS

	testb	$3, CS(%rsp)			/* from kernel_thread? */

	/*
	 * By the time we get here, we have no idea whether our pt_regs,
	 * ti flags, and ti status came from the 64-bit SYSCALL fast path,
	 * the slow path, or one of the 32-bit compat paths.
	 * Use IRET code path to return, since it can safely handle
	 * all of the above.
	 */
	jnz	int_ret_from_sys_call

	/*
	 * We came from kernel_thread
	 * nb: we depend on RESTORE_EXTRA_REGS above
	 */
	movq	%rbp, %rdi
	call	*%rbx
	movl	$0, RAX(%rsp)
	RESTORE_EXTRA_REGS
	jmp	int_ret_from_sys_call
END(ret_from_fork)

/*
 * Build the entry stubs with some assembler magic.
 * We pack 1 stub into every 8-byte block.
 */
	.align 8
ENTRY(irq_entries_start)
    vector=FIRST_EXTERNAL_VECTOR
    .rept (FIRST_SYSTEM_VECTOR - FIRST_EXTERNAL_VECTOR)
	pushq	$(~vector+0x80)			/* Note: always in signed byte range */
    vector=vector+1
	jmp	common_interrupt
	.align	8
    .endr
END(irq_entries_start)

/*
 * Interrupt entry/exit.
 *
 * Interrupt entry points save only callee clobbered registers in fast path.
 *
 * Entry runs with interrupts off.
 */

/* 0(%rsp): ~(interrupt number) */
	.macro interrupt func
	cld
	ALLOC_PT_GPREGS_ON_STACK
	SAVE_C_REGS
	SAVE_EXTRA_REGS

	testb	$3, CS(%rsp)
	jz	1f

	/*
	 * IRQ from user mode.  Switch to kernel gsbase and inform context
	 * tracking that we're in kernel mode.
	 */
	SWAPGS
	SWITCH_KERNEL_CR3

	/*
	 * We need to tell lockdep that IRQs are off.  We can't do this until
	 * we fix gsbase, and we should do it before enter_from_user_mode
	 * (which can take locks).  Since TRACE_IRQS_OFF idempotent,
	 * the simplest way to handle it is to just call it twice if
	 * we enter from user mode.  There's no reason to optimize this since
	 * TRACE_IRQS_OFF is a no-op if lockdep is off.
	 */
	TRACE_IRQS_OFF

#ifdef CONFIG_CONTEXT_TRACKING
	call enter_from_user_mode
#endif

1:
	/*
	 * Save previous stack pointer, optionally switch to interrupt stack.
	 * irq_count is used to check if a CPU is already on an interrupt stack
	 * or not. While this is essentially redundant with preempt_count it is
	 * a little cheaper to use a separate counter in the PDA (short of
	 * moving irq_enter into assembly, which would be too much work)
	 */
	movq	%rsp, %rdi
	incl	PER_CPU_VAR(irq_count)
	cmovzq	PER_CPU_VAR(irq_stack_ptr), %rsp
	pushq	%rdi
	/* We entered an interrupt context - irqs are off: */
	TRACE_IRQS_OFF

	call	\func	/* rdi points to pt_regs */
	.endm

	/*
	 * The interrupt stubs push (~vector+0x80) onto the stack and
	 * then jump to common_interrupt.
	 */
	.p2align CONFIG_X86_L1_CACHE_SHIFT
common_interrupt:
	ASM_CLAC
	addq	$-0x80, (%rsp)			/* Adjust vector to [-256, -1] range */
	interrupt do_IRQ
	/* 0(%rsp): old RSP */
ret_from_intr:
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	decl	PER_CPU_VAR(irq_count)

	/* Restore saved previous stack */
	popq	%rsp

	testb	$3, CS(%rsp)
	jz	retint_kernel

	/* Interrupt came from user space */
GLOBAL(retint_user)
	mov	%rsp,%rdi
	call	prepare_exit_to_usermode
	TRACE_IRQS_IRETQ
	SWITCH_USER_CR3
	SWAPGS
	jmp	restore_regs_and_iret

/* Returning to kernel space */
retint_kernel:
#ifdef CONFIG_PREEMPT
	/* Interrupts are off */
	/* Check if we need preemption */
	bt	$9, EFLAGS(%rsp)		/* were interrupts off? */
	jnc	1f
0:	cmpl	$0, PER_CPU_VAR(__preempt_count)
	jnz	1f
	call	preempt_schedule_irq
	jmp	0b
1:
#endif
	/*
	 * The iretq could re-enable interrupts:
	 */
	TRACE_IRQS_IRETQ

/*
 * At this label, code paths which return to kernel and to user,
 * which come from interrupts/exception and from syscalls, merge.
 */
GLOBAL(restore_regs_and_iret)
	RESTORE_EXTRA_REGS
restore_c_regs_and_iret:
	RESTORE_C_REGS
	REMOVE_PT_GPREGS_FROM_STACK 8
	INTERRUPT_RETURN

ENTRY(native_iret)
	/*
	 * Are we returning to a stack segment from the LDT?  Note: in
	 * 64-bit mode SS:RSP on the exception stack is always valid.
	 */
#ifdef CONFIG_X86_ESPFIX64
	testb	$4, (SS-RIP)(%rsp)
	jnz	native_irq_return_ldt
#endif

.global native_irq_return_iret
native_irq_return_iret:
	/*
	 * This may fault.  Non-paranoid faults on return to userspace are
	 * handled by fixup_bad_iret.  These include #SS, #GP, and #NP.
	 * Double-faults due to espfix64 are handled in do_double_fault.
	 * Other faults here are fatal.
	 */
	iretq

#ifdef CONFIG_X86_ESPFIX64
native_irq_return_ldt:
	pushq	%rax
	pushq	%rdi
	SWAPGS
	SWITCH_KERNEL_CR3
	movq	PER_CPU_VAR(espfix_waddr), %rdi
	movq	%rax, (0*8)(%rdi)		/* RAX */
	movq	(2*8)(%rsp), %rax		/* RIP */
	movq	%rax, (1*8)(%rdi)
	movq	(3*8)(%rsp), %rax		/* CS */
	movq	%rax, (2*8)(%rdi)
	movq	(4*8)(%rsp), %rax		/* RFLAGS */
	movq	%rax, (3*8)(%rdi)
	movq	(6*8)(%rsp), %rax		/* SS */
	movq	%rax, (5*8)(%rdi)
	movq	(5*8)(%rsp), %rax		/* RSP */
	movq	%rax, (4*8)(%rdi)
	andl	$0xffff0000, %eax
	popq	%rdi
	orq	PER_CPU_VAR(espfix_stack), %rax
	SWITCH_USER_CR3
	SWAPGS
	movq	%rax, %rsp
	popq	%rax
	jmp	native_irq_return_iret
#endif
END(common_interrupt)

/*
 * APIC interrupts.
 */
.macro apicinterrupt3 num sym do_sym
ENTRY(\sym)
	ASM_CLAC
	pushq	$~(\num)
.Lcommon_\sym:
	interrupt \do_sym
	jmp	ret_from_intr
END(\sym)
.endm

#ifdef CONFIG_TRACING
#define trace(sym) trace_##sym
#define smp_trace(sym) smp_trace_##sym

.macro trace_apicinterrupt num sym
apicinterrupt3 \num trace(\sym) smp_trace(\sym)
.endm
#else
.macro trace_apicinterrupt num sym do_sym
.endm
#endif

.macro apicinterrupt num sym do_sym
apicinterrupt3 \num \sym \do_sym
trace_apicinterrupt \num \sym
.endm

#ifdef CONFIG_SMP
apicinterrupt3 IRQ_MOVE_CLEANUP_VECTOR		irq_move_cleanup_interrupt	smp_irq_move_cleanup_interrupt
apicinterrupt3 REBOOT_VECTOR			reboot_interrupt		smp_reboot_interrupt
#endif

#ifdef CONFIG_X86_UV
apicinterrupt3 UV_BAU_MESSAGE			uv_bau_message_intr1		uv_bau_message_interrupt
#endif

apicinterrupt LOCAL_TIMER_VECTOR		apic_timer_interrupt		smp_apic_timer_interrupt
apicinterrupt X86_PLATFORM_IPI_VECTOR		x86_platform_ipi		smp_x86_platform_ipi

#ifdef CONFIG_HAVE_KVM
apicinterrupt3 POSTED_INTR_VECTOR		kvm_posted_intr_ipi		smp_kvm_posted_intr_ipi
apicinterrupt3 POSTED_INTR_WAKEUP_VECTOR	kvm_posted_intr_wakeup_ipi	smp_kvm_posted_intr_wakeup_ipi
#endif

#ifdef CONFIG_X86_MCE_THRESHOLD
apicinterrupt THRESHOLD_APIC_VECTOR		threshold_interrupt		smp_threshold_interrupt
#endif

#ifdef CONFIG_X86_MCE_AMD
apicinterrupt DEFERRED_ERROR_VECTOR		deferred_error_interrupt	smp_deferred_error_interrupt
#endif

#ifdef CONFIG_X86_THERMAL_VECTOR
apicinterrupt THERMAL_APIC_VECTOR		thermal_interrupt		smp_thermal_interrupt
#endif

#ifdef CONFIG_SMP
apicinterrupt CALL_FUNCTION_SINGLE_VECTOR	call_function_single_interrupt	smp_call_function_single_interrupt
apicinterrupt CALL_FUNCTION_VECTOR		call_function_interrupt		smp_call_function_interrupt
apicinterrupt RESCHEDULE_VECTOR			reschedule_interrupt		smp_reschedule_interrupt
#endif

apicinterrupt ERROR_APIC_VECTOR			error_interrupt			smp_error_interrupt
apicinterrupt SPURIOUS_APIC_VECTOR		spurious_interrupt		smp_spurious_interrupt

#ifdef CONFIG_IRQ_WORK
apicinterrupt IRQ_WORK_VECTOR			irq_work_interrupt		smp_irq_work_interrupt
#endif

/*
 * Exception entry points.
 */
#define CPU_TSS_IST(x) PER_CPU_VAR(cpu_tss) + (TSS_ist + ((x) - 1) * 8)

.macro idtentry sym do_sym has_error_code:req paranoid=0 shift_ist=-1
ENTRY(\sym)
	/* Sanity check */
	.if \shift_ist != -1 && \paranoid == 0
	.error "using shift_ist requires paranoid=1"
	.endif

	ASM_CLAC
	PARAVIRT_ADJUST_EXCEPTION_FRAME

	.ifeq \has_error_code
	pushq	$-1				/* ORIG_RAX: no syscall to restart */
	.endif

	ALLOC_PT_GPREGS_ON_STACK

	.if \paranoid
	.if \paranoid == 1
	testb	$3, CS(%rsp)			/* If coming from userspace, switch stacks */
	jnz	1f
	.endif
	call	paranoid_entry
	.else
	call	error_entry
	.endif
	/* returned flag: ebx=0: need swapgs on exit, ebx=1: don't need it */

	.if \paranoid
	.if \shift_ist != -1
	TRACE_IRQS_OFF_DEBUG			/* reload IDT in case of recursion */
	.else
	TRACE_IRQS_OFF
	.endif
	.endif

	movq	%rsp, %rdi			/* pt_regs pointer */

	.if \has_error_code
	movq	ORIG_RAX(%rsp), %rsi		/* get error code */
	movq	$-1, ORIG_RAX(%rsp)		/* no syscall to restart */
	.else
	xorl	%esi, %esi			/* no error code */
	.endif

	.if \shift_ist != -1
	subq	$EXCEPTION_STKSZ, CPU_TSS_IST(\shift_ist)
	.endif

	call	\do_sym

	.if \shift_ist != -1
	addq	$EXCEPTION_STKSZ, CPU_TSS_IST(\shift_ist)
	.endif

	/* these procedures expect "no swapgs" flag in ebx */
	.if \paranoid
	jmp	paranoid_exit
	.else
	jmp	error_exit
	.endif

	.if \paranoid == 1
	/*
	 * Paranoid entry from userspace.  Switch stacks and treat it
	 * as a normal entry.  This means that paranoid handlers
	 * run in real process context if user_mode(regs).
	 */
1:
	call	error_entry


	movq	%rsp, %rdi			/* pt_regs pointer */
	call	sync_regs
	movq	%rax, %rsp			/* switch stack */

	movq	%rsp, %rdi			/* pt_regs pointer */

	.if \has_error_code
	movq	ORIG_RAX(%rsp), %rsi		/* get error code */
	movq	$-1, ORIG_RAX(%rsp)		/* no syscall to restart */
	.else
	xorl	%esi, %esi			/* no error code */
	.endif

	call	\do_sym

	jmp	error_exit			/* %ebx: no swapgs flag */
	.endif
END(\sym)
.endm

#ifdef CONFIG_TRACING
.macro trace_idtentry sym do_sym has_error_code:req
idtentry trace(\sym) trace(\do_sym) has_error_code=\has_error_code
idtentry \sym \do_sym has_error_code=\has_error_code
.endm
#else
.macro trace_idtentry sym do_sym has_error_code:req
idtentry \sym \do_sym has_error_code=\has_error_code
.endm
#endif

idtentry divide_error			do_divide_error			has_error_code=0
idtentry overflow			do_overflow			has_error_code=0
idtentry bounds				do_bounds			has_error_code=0
idtentry invalid_op			do_invalid_op			has_error_code=0
idtentry device_not_available		do_device_not_available		has_error_code=0
idtentry double_fault			do_double_fault			has_error_code=1 paranoid=2
idtentry coprocessor_segment_overrun	do_coprocessor_segment_overrun	has_error_code=0
idtentry invalid_TSS			do_invalid_TSS			has_error_code=1
idtentry segment_not_present		do_segment_not_present		has_error_code=1
idtentry spurious_interrupt_bug		do_spurious_interrupt_bug	has_error_code=0
idtentry coprocessor_error		do_coprocessor_error		has_error_code=0
idtentry alignment_check		do_alignment_check		has_error_code=1
idtentry simd_coprocessor_error		do_simd_coprocessor_error	has_error_code=0


	/*
	 * Reload gs selector with exception handling
	 * edi:  new selector
	 */
ENTRY(native_load_gs_index)
	pushfq
	DISABLE_INTERRUPTS(CLBR_ANY & ~CLBR_RDI)
	SWAPGS
gs_change:
	movl	%edi, %gs
2:	mfence					/* workaround */
	SWAPGS
	popfq
	ret
END(native_load_gs_index)

	_ASM_EXTABLE(gs_change, bad_gs)
	.section .fixup, "ax"
	/* running with kernelgs */
bad_gs:
	SWAPGS					/* switch back to user gs */
	xorl	%eax, %eax
	movl	%eax, %gs
	jmp	2b
	.previous

/* Call softirq on interrupt stack. Interrupts are off. */
ENTRY(do_softirq_own_stack)
	pushq	%rbp
	mov	%rsp, %rbp
	incl	PER_CPU_VAR(irq_count)
	cmove	PER_CPU_VAR(irq_stack_ptr), %rsp
	push	%rbp				/* frame pointer backlink */
	call	__do_softirq
	leaveq
	decl	PER_CPU_VAR(irq_count)
	ret
END(do_softirq_own_stack)

#ifdef CONFIG_XEN
idtentry xen_hypervisor_callback xen_do_hypervisor_callback has_error_code=0

/*
 * A note on the "critical region" in our callback handler.
 * We want to avoid stacking callback handlers due to events occurring
 * during handling of the last event. To do this, we keep events disabled
 * until we've done all processing. HOWEVER, we must enable events before
 * popping the stack frame (can't be done atomically) and so it would still
 * be possible to get enough handler activations to overflow the stack.
 * Although unlikely, bugs of that kind are hard to track down, so we'd
 * like to avoid the possibility.
 * So, on entry to the handler we detect whether we interrupted an
 * existing activation in its critical region -- if so, we pop the current
 * activation and restart the handler using the previous one.
 */
ENTRY(xen_do_hypervisor_callback)		/* do_hypervisor_callback(struct *pt_regs) */

/*
 * Since we don't modify %rdi, evtchn_do_upall(struct *pt_regs) will
 * see the correct pointer to the pt_regs
 */
	movq	%rdi, %rsp			/* we don't return, adjust the stack frame */
11:	incl	PER_CPU_VAR(irq_count)
	movq	%rsp, %rbp
	cmovzq	PER_CPU_VAR(irq_stack_ptr), %rsp
	pushq	%rbp				/* frame pointer backlink */
	call	xen_evtchn_do_upcall
	popq	%rsp
	decl	PER_CPU_VAR(irq_count)
#ifndef CONFIG_PREEMPT
	call	xen_maybe_preempt_hcall
#endif
	jmp	error_exit
END(xen_do_hypervisor_callback)

/*
 * Hypervisor uses this for application faults while it executes.
 * We get here for two reasons:
 *  1. Fault while reloading DS, ES, FS or GS
 *  2. Fault while executing IRET
 * Category 1 we do not need to fix up as Xen has already reloaded all segment
 * registers that could be reloaded and zeroed the others.
 * Category 2 we fix up by killing the current process. We cannot use the
 * normal Linux return path in this case because if we use the IRET hypercall
 * to pop the stack frame we end up in an infinite loop of failsafe callbacks.
 * We distinguish between categories by comparing each saved segment register
 * with its current contents: any discrepancy means we in category 1.
 */
ENTRY(xen_failsafe_callback)
	movl	%ds, %ecx
	cmpw	%cx, 0x10(%rsp)
	jne	1f
	movl	%es, %ecx
	cmpw	%cx, 0x18(%rsp)
	jne	1f
	movl	%fs, %ecx
	cmpw	%cx, 0x20(%rsp)
	jne	1f
	movl	%gs, %ecx
	cmpw	%cx, 0x28(%rsp)
	jne	1f
	/* All segments match their saved values => Category 2 (Bad IRET). */
	movq	(%rsp), %rcx
	movq	8(%rsp), %r11
	addq	$0x30, %rsp
	pushq	$0				/* RIP */
	pushq	%r11
	pushq	%rcx
	jmp	general_protection
1:	/* Segment mismatch => Category 1 (Bad segment). Retry the IRET. */
	movq	(%rsp), %rcx
	movq	8(%rsp), %r11
	addq	$0x30, %rsp
	pushq	$-1 /* orig_ax = -1 => not a system call */
	ALLOC_PT_GPREGS_ON_STACK
	SAVE_C_REGS
	SAVE_EXTRA_REGS
	jmp	error_exit
END(xen_failsafe_callback)

apicinterrupt3 HYPERVISOR_CALLBACK_VECTOR \
	xen_hvm_callback_vector xen_evtchn_do_upcall

#endif /* CONFIG_XEN */

#if IS_ENABLED(CONFIG_HYPERV)
apicinterrupt3 HYPERVISOR_CALLBACK_VECTOR \
	hyperv_callback_vector hyperv_vector_handler
#endif /* CONFIG_HYPERV */

idtentry debug			do_debug		has_error_code=0	paranoid=1 shift_ist=DEBUG_STACK
idtentry int3			do_int3			has_error_code=0	paranoid=1 shift_ist=DEBUG_STACK
idtentry stack_segment		do_stack_segment	has_error_code=1

#ifdef CONFIG_XEN
idtentry xen_debug		do_debug		has_error_code=0
idtentry xen_int3		do_int3			has_error_code=0
idtentry xen_stack_segment	do_stack_segment	has_error_code=1
#endif

idtentry general_protection	do_general_protection	has_error_code=1
trace_idtentry page_fault	do_page_fault		has_error_code=1

#ifdef CONFIG_KVM_GUEST
idtentry async_page_fault	do_async_page_fault	has_error_code=1
#endif

#ifdef CONFIG_X86_MCE
idtentry machine_check					has_error_code=0	paranoid=1 do_sym=*machine_check_vector(%rip)
#endif

/*
 * Save all registers in pt_regs, and switch gs if needed.
 * Use slow, but surefire "are we in kernel?" check.
 *
 * Return: ebx=0: needs swapgs but not SWITCH_USER_CR3 in paranoid_exit
 *         ebx=1: needs neither swapgs nor SWITCH_USER_CR3 in paranoid_exit
 *         ebx=2: needs both swapgs and SWITCH_USER_CR3 in paranoid_exit
 *         ebx=3: needs SWITCH_USER_CR3 but not swapgs in paranoid_exit
 */
ENTRY(paranoid_entry)
	cld
	SAVE_C_REGS 8
	SAVE_EXTRA_REGS 8
	movl	$1, %ebx
	movl	$MSR_GS_BASE, %ecx
	rdmsr
	testl	%edx, %edx
	js	1f				/* negative -> in kernel */
	SWAPGS
	xorl	%ebx, %ebx
1:
#ifdef CONFIG_PAGE_TABLE_ISOLATION
	/*
	 * We might have come in between a swapgs and a SWITCH_KERNEL_CR3
	 * on entry, or between a SWITCH_USER_CR3 and a swapgs on exit.
	 * Do a conditional SWITCH_KERNEL_CR3: this could safely be done
	 * unconditionally, but we need to find out whether the reverse
	 * should be done on return (conveyed to paranoid_exit in %ebx).
	 */
	ALTERNATIVE "jmp 2f", "movq %cr3, %rax", X86_FEATURE_KAISER
	testl	$KAISER_SHADOW_PGD_OFFSET, %eax
	jz	2f
	orl	$2, %ebx
	andq	$(~(X86_CR3_PCID_ASID_MASK | KAISER_SHADOW_PGD_OFFSET)), %rax
	/* If PCID enabled, set X86_CR3_PCID_NOFLUSH_BIT */
	ALTERNATIVE "", "bts $63, %rax", X86_FEATURE_PCID
	movq	%rax, %cr3
2:
#endif
	ret
END(paranoid_entry)

/*
 * "Paranoid" exit path from exception stack.  This is invoked
 * only on return from non-NMI IST interrupts that came
 * from kernel space.
 *
 * We may be returning to very strange contexts (e.g. very early
 * in syscall entry), so checking for preemption here would
 * be complicated.  Fortunately, we there's no good reason
 * to try to handle preemption here.
 *
 * On entry: ebx=0: needs swapgs but not SWITCH_USER_CR3
 *           ebx=1: needs neither swapgs nor SWITCH_USER_CR3
 *           ebx=2: needs both swapgs and SWITCH_USER_CR3
 *           ebx=3: needs SWITCH_USER_CR3 but not swapgs
 */
ENTRY(paranoid_exit)
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF_DEBUG
	TRACE_IRQS_IRETQ_DEBUG
#ifdef CONFIG_PAGE_TABLE_ISOLATION
	/* No ALTERNATIVE for X86_FEATURE_KAISER: paranoid_entry sets %ebx */
	testl	$2, %ebx			/* SWITCH_USER_CR3 needed? */
	jz	paranoid_exit_no_switch
	SWITCH_USER_CR3
paranoid_exit_no_switch:
#endif
	testl	$1, %ebx			/* swapgs needed? */
	jnz	paranoid_exit_no_swapgs
	SWAPGS_UNSAFE_STACK
paranoid_exit_no_swapgs:
	RESTORE_EXTRA_REGS
	RESTORE_C_REGS
	REMOVE_PT_GPREGS_FROM_STACK 8
	INTERRUPT_RETURN
END(paranoid_exit)

/*
 * Save all registers in pt_regs, and switch gs if needed.
 * Return: EBX=0: came from user mode; EBX=1: otherwise
 */
ENTRY(error_entry)
	cld
	SAVE_C_REGS 8
	SAVE_EXTRA_REGS 8
	/*
	 * error_entry() always returns with a kernel gsbase and
	 * CR3.  We must also have a kernel CR3/gsbase before
	 * calling TRACE_IRQS_*.  Just unconditionally switch to
	 * the kernel CR3 here.
	 */
	SWITCH_KERNEL_CR3
	xorl	%ebx, %ebx
	testb	$3, CS+8(%rsp)
	jz	.Lerror_kernelspace

.Lerror_entry_from_usermode_swapgs:
	/*
	 * We entered from user mode or we're pretending to have entered
	 * from user mode due to an IRET fault.
	 */
	SWAPGS

.Lerror_entry_from_usermode_after_swapgs:
	/*
	 * We need to tell lockdep that IRQs are off.  We can't do this until
	 * we fix gsbase, and we should do it before enter_from_user_mode
	 * (which can take locks).
	 */
	TRACE_IRQS_OFF
#ifdef CONFIG_CONTEXT_TRACKING
	call enter_from_user_mode
#endif
	ret

.Lerror_entry_done:
	TRACE_IRQS_OFF
	ret

	/*
	 * There are two places in the kernel that can potentially fault with
	 * usergs. Handle them here.  B stepping K8s sometimes report a
	 * truncated RIP for IRET exceptions returning to compat mode. Check
	 * for these here too.
	 */
.Lerror_kernelspace:
	incl	%ebx
	leaq	native_irq_return_iret(%rip), %rcx
	cmpq	%rcx, RIP+8(%rsp)
	je	.Lerror_bad_iret
	movl	%ecx, %eax			/* zero extend */
	cmpq	%rax, RIP+8(%rsp)
	je	.Lbstep_iret
	cmpq	$gs_change, RIP+8(%rsp)
	jne	.Lerror_entry_done

	/*
	 * hack: gs_change can fail with user gsbase.  If this happens, fix up
	 * gsbase and proceed.  We'll fix up the exception and land in
	 * gs_change's error handler with kernel gsbase.
	 */
	jmp	.Lerror_entry_from_usermode_swapgs

.Lbstep_iret:
	/* Fix truncated RIP */
	movq	%rcx, RIP+8(%rsp)
	/* fall through */

.Lerror_bad_iret:
	/*
	 * We came from an IRET to user mode, so we have user gsbase.
	 * Switch to kernel gsbase:
	 */
	SWAPGS

	/*
	 * Pretend that the exception came from user mode: set up pt_regs
	 * as if we faulted immediately after IRET and clear EBX so that
	 * error_exit knows that we will be returning to user mode.
	 */
	mov	%rsp, %rdi
	call	fixup_bad_iret
	mov	%rax, %rsp
	decl	%ebx
	jmp	.Lerror_entry_from_usermode_after_swapgs
END(error_entry)


/*
 * On entry, EBS is a "return to kernel mode" flag:
 *   1: already in kernel mode, don't need SWAPGS
 *   0: user gsbase is loaded, we need SWAPGS and standard preparation for return to usermode
 */
ENTRY(error_exit)
	movl	%ebx, %eax
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	testl	%eax, %eax
	jnz	retint_kernel
	jmp	retint_user
END(error_exit)

/* Runs on exception stack */
ENTRY(nmi)
	/*
	 * Fix up the exception frame if we're on Xen.
	 * PARAVIRT_ADJUST_EXCEPTION_FRAME is guaranteed to push at most
	 * one value to the stack on native, so it may clobber the rdx
	 * scratch slot, but it won't clobber any of the important
	 * slots past it.
	 *
	 * Xen is a different story, because the Xen frame itself overlaps
	 * the "NMI executing" variable.
	 */
	PARAVIRT_ADJUST_EXCEPTION_FRAME

	/*
	 * We allow breakpoints in NMIs. If a breakpoint occurs, then
	 * the iretq it performs will take us out of NMI context.
	 * This means that we can have nested NMIs where the next
	 * NMI is using the top of the stack of the previous NMI. We
	 * can't let it execute because the nested NMI will corrupt the
	 * stack of the previous NMI. NMI handlers are not re-entrant
	 * anyway.
	 *
	 * To handle this case we do the following:
	 *  Check the a special location on the stack that contains
	 *  a variable that is set when NMIs are executing.
	 *  The interrupted task's stack is also checked to see if it
	 *  is an NMI stack.
	 *  If the variable is not set and the stack is not the NMI
	 *  stack then:
	 *    o Set the special variable on the stack
	 *    o Copy the interrupt frame into an "outermost" location on the
	 *      stack
	 *    o Copy the interrupt frame into an "iret" location on the stack
	 *    o Continue processing the NMI
	 *  If the variable is set or the previous stack is the NMI stack:
	 *    o Modify the "iret" location to jump to the repeat_nmi
	 *    o return back to the first NMI
	 *
	 * Now on exit of the first NMI, we first clear the stack variable
	 * The NMI stack will tell any nested NMIs at that point that it is
	 * nested. Then we pop the stack normally with iret, and if there was
	 * a nested NMI that updated the copy interrupt stack frame, a
	 * jump will be made to the repeat_nmi code that will handle the second
	 * NMI.
	 *
	 * However, espfix prevents us from directly returning to userspace
	 * with a single IRET instruction.  Similarly, IRET to user mode
	 * can fault.  We therefore handle NMIs from user space like
	 * other IST entries.
	 */

	ASM_CLAC

	/* Use %rdx as our temp variable throughout */
	pushq	%rdx

	testb	$3, CS-RIP+8(%rsp)
	jz	.Lnmi_from_kernel

	/*
	 * NMI from user mode.  We need to run on the thread stack, but we
	 * can't go through the normal entry paths: NMIs are masked, and
	 * we don't want to enable interrupts, because then we'll end
	 * up in an awkward situation in which IRQs are on but NMIs
	 * are off.
	 *
	 * We also must not push anything to the stack before switching
	 * stacks lest we corrupt the "NMI executing" variable.
	 */

	SWAPGS_UNSAFE_STACK
	/*
	 * percpu variables are mapped with user CR3, so no need
	 * to switch CR3 here.
	 */
	cld
	movq	%rsp, %rdx
	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rsp
	pushq	5*8(%rdx)	/* pt_regs->ss */
	pushq	4*8(%rdx)	/* pt_regs->rsp */
	pushq	3*8(%rdx)	/* pt_regs->flags */
	pushq	2*8(%rdx)	/* pt_regs->cs */
	pushq	1*8(%rdx)	/* pt_regs->rip */
	pushq   $-1		/* pt_regs->orig_ax */
	pushq   %rdi		/* pt_regs->di */
	pushq   %rsi		/* pt_regs->si */
	pushq   (%rdx)		/* pt_regs->dx */
	pushq   %rcx		/* pt_regs->cx */
	pushq   %rax		/* pt_regs->ax */
	pushq   %r8		/* pt_regs->r8 */
	pushq   %r9		/* pt_regs->r9 */
	pushq   %r10		/* pt_regs->r10 */
	pushq   %r11		/* pt_regs->r11 */
	pushq	%rbx		/* pt_regs->rbx */
	pushq	%rbp		/* pt_regs->rbp */
	pushq	%r12		/* pt_regs->r12 */
	pushq	%r13		/* pt_regs->r13 */
	pushq	%r14		/* pt_regs->r14 */
	pushq	%r15		/* pt_regs->r15 */

	/*
	 * At this point we no longer need to worry about stack damage
	 * due to nesting -- we're on the normal thread stack and we're
	 * done with the NMI stack.
	 */

	movq	%rsp, %rdi
	movq	$-1, %rsi
#ifdef CONFIG_PAGE_TABLE_ISOLATION
	/* Unconditionally use kernel CR3 for do_nmi() */
	/* %rax is saved above, so OK to clobber here */
	ALTERNATIVE "jmp 2f", "movq %cr3, %rax", X86_FEATURE_KAISER
	/* If PCID enabled, NOFLUSH now and NOFLUSH on return */
	ALTERNATIVE "", "bts $63, %rax", X86_FEATURE_PCID
	pushq	%rax
	/* mask off "user" bit of pgd address and 12 PCID bits: */
	andq	$(~(X86_CR3_PCID_ASID_MASK | KAISER_SHADOW_PGD_OFFSET)), %rax
	movq	%rax, %cr3
2:
#endif
	call	do_nmi

#ifdef CONFIG_PAGE_TABLE_ISOLATION
	/*
	 * Unconditionally restore CR3.  I know we return to
	 * kernel code that needs user CR3, but do we ever return
	 * to "user mode" where we need the kernel CR3?
	 */
	ALTERNATIVE "", "popq %rax; movq %rax, %cr3", X86_FEATURE_KAISER
#endif

	/*
	 * Return back to user mode.  We must *not* do the normal exit
	 * work, because we don't want to enable interrupts.  Do not
	 * switch to user CR3: we might be going back to kernel code
	 * that had a user CR3 set.
	 */
	SWAPGS
	jmp	restore_c_regs_and_iret

.Lnmi_from_kernel:
	/*
	 * Here's what our stack frame will look like:
	 * +---------------------------------------------------------+
	 * | original SS                                             |
	 * | original Return RSP                                     |
	 * | original RFLAGS                                         |
	 * | original CS                                             |
	 * | original RIP                                            |
	 * +---------------------------------------------------------+
	 * | temp storage for rdx                                    |
	 * +---------------------------------------------------------+
	 * | "NMI executing" variable                                |
	 * +---------------------------------------------------------+
	 * | iret SS          } Copied from "outermost" frame        |
	 * | iret Return RSP  } on each loop iteration; overwritten  |
	 * | iret RFLAGS      } by a nested NMI to force another     |
	 * | iret CS          } iteration if needed.                 |
	 * | iret RIP         }                                      |
	 * +---------------------------------------------------------+
	 * | outermost SS          } initialized in first_nmi;       |
	 * | outermost Return RSP  } will not be changed before      |
	 * | outermost RFLAGS      } NMI processing is done.         |
	 * | outermost CS          } Copied to "iret" frame on each  |
	 * | outermost RIP         } iteration.                      |
	 * +---------------------------------------------------------+
	 * | pt_regs                                                 |
	 * +---------------------------------------------------------+
	 *
	 * The "original" frame is used by hardware.  Before re-enabling
	 * NMIs, we need to be done with it, and we need to leave enough
	 * space for the asm code here.
	 *
	 * We return by executing IRET while RSP points to the "iret" frame.
	 * That will either return for real or it will loop back into NMI
	 * processing.
	 *
	 * The "outermost" frame is copied to the "iret" frame on each
	 * iteration of the loop, so each iteration starts with the "iret"
	 * frame pointing to the final return target.
	 */

	/*
	 * Determine whether we're a nested NMI.
	 *
	 * If we interrupted kernel code between repeat_nmi and
	 * end_repeat_nmi, then we are a nested NMI.  We must not
	 * modify the "iret" frame because it's being written by
	 * the outer NMI.  That's okay; the outer NMI handler is
	 * about to about to call do_nmi anyway, so we can just
	 * resume the outer NMI.
	 */

	movq	$repeat_nmi, %rdx
	cmpq	8(%rsp), %rdx
	ja	1f
	movq	$end_repeat_nmi, %rdx
	cmpq	8(%rsp), %rdx
	ja	nested_nmi_out
1:

	/*
	 * Now check "NMI executing".  If it's set, then we're nested.
	 * This will not detect if we interrupted an outer NMI just
	 * before IRET.
	 */
	cmpl	$1, -8(%rsp)
	je	nested_nmi

	/*
	 * Now test if the previous stack was an NMI stack.  This covers
	 * the case where we interrupt an outer NMI after it clears
	 * "NMI executing" but before IRET.  We need to be careful, though:
	 * there is one case in which RSP could point to the NMI stack
	 * despite there being no NMI active: naughty userspace controls
	 * RSP at the very beginning of the SYSCALL targets.  We can
	 * pull a fast one on naughty userspace, though: we program
	 * SYSCALL to mask DF, so userspace cannot cause DF to be set
	 * if it controls the kernel's RSP.  We set DF before we clear
	 * "NMI executing".
	 */
	lea	6*8(%rsp), %rdx
	/* Compare the NMI stack (rdx) with the stack we came from (4*8(%rsp)) */
	cmpq	%rdx, 4*8(%rsp)
	/* If the stack pointer is above the NMI stack, this is a normal NMI */
	ja	first_nmi

	subq	$EXCEPTION_STKSZ, %rdx
	cmpq	%rdx, 4*8(%rsp)
	/* If it is below the NMI stack, it is a normal NMI */
	jb	first_nmi

	/* Ah, it is within the NMI stack. */

	testb	$(X86_EFLAGS_DF >> 8), (3*8 + 1)(%rsp)
	jz	first_nmi	/* RSP was user controlled. */

	/* This is a nested NMI. */

nested_nmi:
	/*
	 * Modify the "iret" frame to point to repeat_nmi, forcing another
	 * iteration of NMI handling.
	 */
	subq	$8, %rsp
	leaq	-10*8(%rsp), %rdx
	pushq	$__KERNEL_DS
	pushq	%rdx
	pushfq
	pushq	$__KERNEL_CS
	pushq	$repeat_nmi

	/* Put stack back */
	addq	$(6*8), %rsp

nested_nmi_out:
	popq	%rdx

	/* We are returning to kernel mode, so this cannot result in a fault. */
	INTERRUPT_RETURN

first_nmi:
	/* Restore rdx. */
	movq	(%rsp), %rdx

	/* Make room for "NMI executing". */
	pushq	$0

	/* Leave room for the "iret" frame */
	subq	$(5*8), %rsp

	/* Copy the "original" frame to the "outermost" frame */
	.rept 5
	pushq	11*8(%rsp)
	.endr

	/* Everything up to here is safe from nested NMIs */

#ifdef CONFIG_DEBUG_ENTRY
	/*
	 * For ease of testing, unmask NMIs right away.  Disabled by
	 * default because IRET is very expensive.
	 */
	pushq	$0		/* SS */
	pushq	%rsp		/* RSP (minus 8 because of the previous push) */
	addq	$8, (%rsp)	/* Fix up RSP */
	pushfq			/* RFLAGS */
	pushq	$__KERNEL_CS	/* CS */
	pushq	$1f		/* RIP */
	INTERRUPT_RETURN	/* continues at repeat_nmi below */
1:
#endif

repeat_nmi:
	/*
	 * If there was a nested NMI, the first NMI's iret will return
	 * here. But NMIs are still enabled and we can take another
	 * nested NMI. The nested NMI checks the interrupted RIP to see
	 * if it is between repeat_nmi and end_repeat_nmi, and if so
	 * it will just return, as we are about to repeat an NMI anyway.
	 * This makes it safe to copy to the stack frame that a nested
	 * NMI will update.
	 *
	 * RSP is pointing to "outermost RIP".  gsbase is unknown, but, if
	 * we're repeating an NMI, gsbase has the same value that it had on
	 * the first iteration.  paranoid_entry will load the kernel
	 * gsbase if needed before we call do_nmi.  "NMI executing"
	 * is zero.
	 */
	movq	$1, 10*8(%rsp)		/* Set "NMI executing". */

	/*
	 * Copy the "outermost" frame to the "iret" frame.  NMIs that nest
	 * here must not modify the "iret" frame while we're writing to
	 * it or it will end up containing garbage.
	 */
	addq	$(10*8), %rsp
	.rept 5
	pushq	-6*8(%rsp)
	.endr
	subq	$(5*8), %rsp
end_repeat_nmi:

	/*
	 * Everything below this point can be preempted by a nested NMI.
	 * If this happens, then the inner NMI will change the "iret"
	 * frame to point back to repeat_nmi.
	 */
	pushq	$-1				/* ORIG_RAX: no syscall to restart */
	ALLOC_PT_GPREGS_ON_STACK

	/*
	 * Use the same approach as paranoid_entry to handle SWAPGS, but
	 * without CR3 handling since we do that differently in NMIs.  No
	 * need to use paranoid_exit as we should not be calling schedule
	 * in NMI context.  Even with normal interrupts enabled. An NMI
	 * should not be setting NEED_RESCHED or anything that normal
	 * interrupts and exceptions might do.
	 */
	cld
	SAVE_C_REGS
	SAVE_EXTRA_REGS
	movl	$1, %ebx
	movl	$MSR_GS_BASE, %ecx
	rdmsr
	testl	%edx, %edx
	js	1f				/* negative -> in kernel */
	SWAPGS
	xorl	%ebx, %ebx
1:
	movq	%rsp, %rdi
	movq	$-1, %rsi
#ifdef CONFIG_PAGE_TABLE_ISOLATION
	/* Unconditionally use kernel CR3 for do_nmi() */
	/* %rax is saved above, so OK to clobber here */
	ALTERNATIVE "jmp 2f", "movq %cr3, %rax", X86_FEATURE_KAISER
	/* If PCID enabled, NOFLUSH now and NOFLUSH on return */
	ALTERNATIVE "", "bts $63, %rax", X86_FEATURE_PCID
	pushq	%rax
	/* mask off "user" bit of pgd address and 12 PCID bits: */
	andq	$(~(X86_CR3_PCID_ASID_MASK | KAISER_SHADOW_PGD_OFFSET)), %rax
	movq	%rax, %cr3
2:
#endif

	/* paranoidentry do_nmi, 0; without TRACE_IRQS_OFF */
	call	do_nmi

#ifdef CONFIG_PAGE_TABLE_ISOLATION
	/*
	 * Unconditionally restore CR3.  We might be returning to
	 * kernel code that needs user CR3, like just just before
	 * a sysret.
	 */
	ALTERNATIVE "", "popq %rax; movq %rax, %cr3", X86_FEATURE_KAISER
#endif

	testl	%ebx, %ebx			/* swapgs needed? */
	jnz	nmi_restore
nmi_swapgs:
	/* We fixed up CR3 above, so no need to switch it here */
	SWAPGS_UNSAFE_STACK
nmi_restore:
	RESTORE_EXTRA_REGS
	RESTORE_C_REGS

	/* Point RSP at the "iret" frame. */
	REMOVE_PT_GPREGS_FROM_STACK 6*8

	/*
	 * Clear "NMI executing".  Set DF first so that we can easily
	 * distinguish the remaining code between here and IRET from
	 * the SYSCALL entry and exit paths.  On a native kernel, we
	 * could just inspect RIP, but, on paravirt kernels,
	 * INTERRUPT_RETURN can translate into a jump into a
	 * hypercall page.
	 */
	std
	movq	$0, 5*8(%rsp)		/* clear "NMI executing" */

	/*
	 * INTERRUPT_RETURN reads the "iret" frame and exits the NMI
	 * stack in a single instruction.  We are returning to kernel
	 * mode, so this cannot result in a fault.
	 */
	INTERRUPT_RETURN
END(nmi)

ENTRY(ignore_sysret)
	mov	$-ENOSYS, %eax
	sysret
END(ignore_sysret)
