/*
 *  linux/arch/x86_64/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002  Andi Kleen SuSE Labs
 *  Copyright (C) 2000  Pavel Machek <pavel@suse.cz>
 */

/*
 * entry.S contains the system-call and fault low-level handling routines.
 *
 * Some of this is documented in Documentation/x86/entry_64.txt
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after an interrupt and after each system call.
 *
 * A note on terminology:
 * - iret frame: Architecture defined interrupt frame from SS to RIP
 * at the top of the kernel process stack.
 *
 * Some macro usage:
 * - CFI macros are used to generate dwarf2 unwind information for better
 * backtraces. They don't change any code.
 * - ENTRY/END Define functions in the symbol table.
 * - TRACE_IRQ_* - Trace hard interrupt state for lock debugging.
 * - idtentry - Define exception entry points.
 */

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include <asm/dwarf2.h>
#include <asm/calling.h>
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
#include <asm/context_tracking.h>
#include <asm/smap.h>
#include <asm/pgtable_types.h>
#include <linux/err.h>

/* Avoid __ASSEMBLER__'ifying <linux/audit.h> just for this.  */
#include <linux/elf-em.h>
#define AUDIT_ARCH_X86_64	(EM_X86_64|__AUDIT_ARCH_64BIT|__AUDIT_ARCH_LE)
#define __AUDIT_ARCH_64BIT 0x80000000
#define __AUDIT_ARCH_LE	   0x40000000

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
	bt   $9,EFLAGS(%rsp)	/* interrupts off? */
	jnc  1f
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
	call debug_stack_set_zero
	TRACE_IRQS_OFF
	call debug_stack_reset
.endm

.macro TRACE_IRQS_ON_DEBUG
	call debug_stack_set_zero
	TRACE_IRQS_ON
	call debug_stack_reset
.endm

.macro TRACE_IRQS_IRETQ_DEBUG
	bt   $9,EFLAGS(%rsp)	/* interrupts off? */
	jnc  1f
	TRACE_IRQS_ON_DEBUG
1:
.endm

#else
# define TRACE_IRQS_OFF_DEBUG		TRACE_IRQS_OFF
# define TRACE_IRQS_ON_DEBUG		TRACE_IRQS_ON
# define TRACE_IRQS_IRETQ_DEBUG		TRACE_IRQS_IRETQ
#endif

/*
 * empty frame
 */
	.macro EMPTY_FRAME start=1 offset=0
	.if \start
	CFI_STARTPROC simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA rsp,8+\offset
	.else
	CFI_DEF_CFA_OFFSET 8+\offset
	.endif
	.endm

/*
 * initial frame state for interrupts (and exceptions without error code)
 */
	.macro INTR_FRAME start=1 offset=0
	EMPTY_FRAME \start, 5*8+\offset
	/*CFI_REL_OFFSET ss, 4*8+\offset*/
	CFI_REL_OFFSET rsp, 3*8+\offset
	/*CFI_REL_OFFSET rflags, 2*8+\offset*/
	/*CFI_REL_OFFSET cs, 1*8+\offset*/
	CFI_REL_OFFSET rip, 0*8+\offset
	.endm

/*
 * initial frame state for exceptions with error code (and interrupts
 * with vector already pushed)
 */
	.macro XCPT_FRAME start=1 offset=0
	INTR_FRAME \start, 1*8+\offset
	.endm

/*
 * frame that enables passing a complete pt_regs to a C function.
 */
	.macro DEFAULT_FRAME start=1 offset=0
	XCPT_FRAME \start, ORIG_RAX+\offset
	CFI_REL_OFFSET rdi, RDI+\offset
	CFI_REL_OFFSET rsi, RSI+\offset
	CFI_REL_OFFSET rdx, RDX+\offset
	CFI_REL_OFFSET rcx, RCX+\offset
	CFI_REL_OFFSET rax, RAX+\offset
	CFI_REL_OFFSET r8, R8+\offset
	CFI_REL_OFFSET r9, R9+\offset
	CFI_REL_OFFSET r10, R10+\offset
	CFI_REL_OFFSET r11, R11+\offset
	CFI_REL_OFFSET rbx, RBX+\offset
	CFI_REL_OFFSET rbp, RBP+\offset
	CFI_REL_OFFSET r12, R12+\offset
	CFI_REL_OFFSET r13, R13+\offset
	CFI_REL_OFFSET r14, R14+\offset
	CFI_REL_OFFSET r15, R15+\offset
	.endm

/*
 * 64bit SYSCALL instruction entry. Up to 6 arguments in registers.
 *
 * 64bit SYSCALL saves rip to rcx, clears rflags.RF, then saves rflags to r11,
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
 * (note: r12-r15,rbp,rbx are callee-preserved in C ABI)
 *
 * Only called from user space.
 *
 * When user can change pt_regs->foo always force IRET. That is because
 * it deals with uncanonical addresses better. SYSRET has trouble
 * with them due to bugs in both AMD and Intel CPUs.
 */

ENTRY(system_call)
	CFI_STARTPROC	simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA	rsp,0
	CFI_REGISTER	rip,rcx
	/*CFI_REGISTER	rflags,r11*/

	/*
	 * Interrupts are off on entry.
	 * We do not frame this tiny irq-off block with TRACE_IRQS_OFF/ON,
	 * it is too small to ever cause noticeable irq latency.
	 */
	SWAPGS_UNSAFE_STACK
	/*
	 * A hypervisor implementation might want to use a label
	 * after the swapgs, so that it can do the swapgs
	 * for the guest and jump here on syscall.
	 */
GLOBAL(system_call_after_swapgs)

	movq	%rsp,PER_CPU_VAR(rsp_scratch)
	movq	PER_CPU_VAR(kernel_stack),%rsp

	/* Construct struct pt_regs on stack */
	pushq_cfi $__USER_DS			/* pt_regs->ss */
	pushq_cfi PER_CPU_VAR(rsp_scratch)	/* pt_regs->sp */
	/*
	 * Re-enable interrupts.
	 * We use 'rsp_scratch' as a scratch space, hence irq-off block above
	 * must execute atomically in the face of possible interrupt-driven
	 * task preemption. We must enable interrupts only after we're done
	 * with using rsp_scratch:
	 */
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushq_cfi	%r11			/* pt_regs->flags */
	pushq_cfi	$__USER_CS		/* pt_regs->cs */
	pushq_cfi	%rcx			/* pt_regs->ip */
	CFI_REL_OFFSET rip,0
	pushq_cfi_reg	rax			/* pt_regs->orig_ax */
	pushq_cfi_reg	rdi			/* pt_regs->di */
	pushq_cfi_reg	rsi			/* pt_regs->si */
	pushq_cfi_reg	rdx			/* pt_regs->dx */
	pushq_cfi_reg	rcx			/* pt_regs->cx */
	pushq_cfi	$-ENOSYS		/* pt_regs->ax */
	pushq_cfi_reg	r8			/* pt_regs->r8 */
	pushq_cfi_reg	r9			/* pt_regs->r9 */
	pushq_cfi_reg	r10			/* pt_regs->r10 */
	pushq_cfi_reg	r11			/* pt_regs->r11 */
	sub	$(6*8),%rsp /* pt_regs->bp,bx,r12-15 not saved */
	CFI_ADJUST_CFA_OFFSET 6*8

	testl $_TIF_WORK_SYSCALL_ENTRY, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz tracesys
system_call_fastpath:
#if __SYSCALL_MASK == ~0
	cmpq $__NR_syscall_max,%rax
#else
	andl $__SYSCALL_MASK,%eax
	cmpl $__NR_syscall_max,%eax
#endif
	ja	1f	/* return -ENOSYS (already in pt_regs->ax) */
	movq %r10,%rcx
	call *sys_call_table(,%rax,8)
	movq %rax,RAX(%rsp)
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
	testl $_TIF_ALLWORK_MASK, ASM_THREAD_INFO(TI_flags, %rsp, SIZEOF_PTREGS)
	jnz int_ret_from_sys_call_irqs_off	/* Go to the slow path */

	CFI_REMEMBER_STATE

	RESTORE_C_REGS_EXCEPT_RCX_R11
	movq	RIP(%rsp),%rcx
	CFI_REGISTER	rip,rcx
	movq	EFLAGS(%rsp),%r11
	/*CFI_REGISTER	rflags,r11*/
	movq	RSP(%rsp),%rsp
	/*
	 * 64bit SYSRET restores rip from rcx,
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

	CFI_RESTORE_STATE

	/* Do syscall entry tracing */
tracesys:
	movq %rsp, %rdi
	movl $AUDIT_ARCH_X86_64, %esi
	call syscall_trace_enter_phase1
	test %rax, %rax
	jnz tracesys_phase2		/* if needed, run the slow path */
	RESTORE_C_REGS_EXCEPT_RAX	/* else restore clobbered regs */
	movq ORIG_RAX(%rsp), %rax
	jmp system_call_fastpath	/*      and return to the fast path */

tracesys_phase2:
	SAVE_EXTRA_REGS
	movq %rsp, %rdi
	movl $AUDIT_ARCH_X86_64, %esi
	movq %rax,%rdx
	call syscall_trace_enter_phase2

	/*
	 * Reload registers from stack in case ptrace changed them.
	 * We don't reload %rax because syscall_trace_entry_phase2() returned
	 * the value it wants us to use in the table lookup.
	 */
	RESTORE_C_REGS_EXCEPT_RAX
	RESTORE_EXTRA_REGS
#if __SYSCALL_MASK == ~0
	cmpq $__NR_syscall_max,%rax
#else
	andl $__SYSCALL_MASK,%eax
	cmpl $__NR_syscall_max,%eax
#endif
	ja	1f	/* return -ENOSYS (already in pt_regs->ax) */
	movq %r10,%rcx	/* fixup for C */
	call *sys_call_table(,%rax,8)
	movq %rax,RAX(%rsp)
1:
	/* Use IRET because user could have changed pt_regs->foo */

/*
 * Syscall return path ending with IRET.
 * Has correct iret frame.
 */
GLOBAL(int_ret_from_sys_call)
	DISABLE_INTERRUPTS(CLBR_NONE)
int_ret_from_sys_call_irqs_off: /* jumps come here from the irqs-off SYSRET path */
	TRACE_IRQS_OFF
	movl $_TIF_ALLWORK_MASK,%edi
	/* edi:	mask to check */
GLOBAL(int_with_check)
	LOCKDEP_SYS_EXIT_IRQ
	GET_THREAD_INFO(%rcx)
	movl TI_flags(%rcx),%edx
	andl %edi,%edx
	jnz   int_careful
	andl	$~TS_COMPAT,TI_status(%rcx)
	jmp	syscall_return

	/* Either reschedule or signal or syscall exit tracking needed. */
	/* First do a reschedule test. */
	/* edx:	work, edi: workmask */
int_careful:
	bt $TIF_NEED_RESCHED,%edx
	jnc  int_very_careful
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushq_cfi %rdi
	SCHEDULE_USER
	popq_cfi %rdi
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp int_with_check

	/* handle signals and tracing -- both require a full pt_regs */
int_very_careful:
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	SAVE_EXTRA_REGS
	/* Check for syscall exit trace */
	testl $_TIF_WORK_SYSCALL_EXIT,%edx
	jz int_signal
	pushq_cfi %rdi
	leaq 8(%rsp),%rdi	# &ptregs -> arg1
	call syscall_trace_leave
	popq_cfi %rdi
	andl $~(_TIF_WORK_SYSCALL_EXIT|_TIF_SYSCALL_EMU),%edi
	jmp int_restore_rest

int_signal:
	testl $_TIF_DO_NOTIFY_MASK,%edx
	jz 1f
	movq %rsp,%rdi		# &ptregs -> arg1
	xorl %esi,%esi		# oldset -> arg2
	call do_notify_resume
1:	movl $_TIF_WORK_MASK,%edi
int_restore_rest:
	RESTORE_EXTRA_REGS
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp int_with_check

syscall_return:
	/* The IRETQ could re-enable interrupts: */
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_IRETQ

	/*
	 * Try to use SYSRET instead of IRET if we're returning to
	 * a completely clean 64-bit userspace context.
	 */
	movq RCX(%rsp),%rcx
	cmpq %rcx,RIP(%rsp)		/* RCX == RIP */
	jne opportunistic_sysret_failed

	/*
	 * On Intel CPUs, SYSRET with non-canonical RCX/RIP will #GP
	 * in kernel space.  This essentially lets the user take over
	 * the kernel, since userspace controls RSP.  It's not worth
	 * testing for canonicalness exactly -- this check detects any
	 * of the 17 high bits set, which is true for non-canonical
	 * or kernel addresses.  (This will pessimize vsyscall=native.
	 * Big deal.)
	 *
	 * If virtual addresses ever become wider, this will need
	 * to be updated to remain correct on both old and new CPUs.
	 */
	.ifne __VIRTUAL_MASK_SHIFT - 47
	.error "virtual address width changed -- SYSRET checks need update"
	.endif
	shr $__VIRTUAL_MASK_SHIFT, %rcx
	jnz opportunistic_sysret_failed

	cmpq $__USER_CS,CS(%rsp)	/* CS must match SYSRET */
	jne opportunistic_sysret_failed

	movq R11(%rsp),%r11
	cmpq %r11,EFLAGS(%rsp)		/* R11 == RFLAGS */
	jne opportunistic_sysret_failed

	/*
	 * SYSRET can't restore RF.  SYSRET can restore TF, but unlike IRET,
	 * restoring TF results in a trap from userspace immediately after
	 * SYSRET.  This would cause an infinite loop whenever #DB happens
	 * with register state that satisfies the opportunistic SYSRET
	 * conditions.  For example, single-stepping this user code:
	 *
	 *           movq $stuck_here,%rcx
	 *           pushfq
	 *           popq %r11
	 *   stuck_here:
	 *
	 * would never get past 'stuck_here'.
	 */
	testq $(X86_EFLAGS_RF|X86_EFLAGS_TF), %r11
	jnz opportunistic_sysret_failed

	/* nothing to check for RSP */

	cmpq $__USER_DS,SS(%rsp)	/* SS must match SYSRET */
	jne opportunistic_sysret_failed

	/*
	 * We win!  This label is here just for ease of understanding
	 * perf profiles.  Nothing jumps here.
	 */
syscall_return_via_sysret:
	CFI_REMEMBER_STATE
	/* r11 is already restored (see code above) */
	RESTORE_C_REGS_EXCEPT_R11
	movq RSP(%rsp),%rsp
	USERGS_SYSRET64
	CFI_RESTORE_STATE

opportunistic_sysret_failed:
	SWAPGS
	jmp	restore_c_regs_and_iret
	CFI_ENDPROC
END(system_call)


	.macro FORK_LIKE func
ENTRY(stub_\func)
	CFI_STARTPROC
	DEFAULT_FRAME 0, 8		/* offset 8: return address */
	SAVE_EXTRA_REGS 8
	jmp sys_\func
	CFI_ENDPROC
END(stub_\func)
	.endm

	FORK_LIKE  clone
	FORK_LIKE  fork
	FORK_LIKE  vfork

ENTRY(stub_execve)
	CFI_STARTPROC
	DEFAULT_FRAME 0, 8
	call	sys_execve
return_from_execve:
	testl	%eax, %eax
	jz	1f
	/* exec failed, can use fast SYSRET code path in this case */
	ret
1:
	/* must use IRET code path (pt_regs->cs may have changed) */
	addq	$8, %rsp
	CFI_ADJUST_CFA_OFFSET -8
	ZERO_EXTRA_REGS
	movq	%rax,RAX(%rsp)
	jmp	int_ret_from_sys_call
	CFI_ENDPROC
END(stub_execve)
/*
 * Remaining execve stubs are only 7 bytes long.
 * ENTRY() often aligns to 16 bytes, which in this case has no benefits.
 */
	.align	8
GLOBAL(stub_execveat)
	CFI_STARTPROC
	DEFAULT_FRAME 0, 8
	call	sys_execveat
	jmp	return_from_execve
	CFI_ENDPROC
END(stub_execveat)

#ifdef CONFIG_X86_X32_ABI
	.align	8
GLOBAL(stub_x32_execve)
	CFI_STARTPROC
	DEFAULT_FRAME 0, 8
	call	compat_sys_execve
	jmp	return_from_execve
	CFI_ENDPROC
END(stub_x32_execve)
	.align	8
GLOBAL(stub_x32_execveat)
	CFI_STARTPROC
	DEFAULT_FRAME 0, 8
	call	compat_sys_execveat
	jmp	return_from_execve
	CFI_ENDPROC
END(stub_x32_execveat)
#endif

#ifdef CONFIG_IA32_EMULATION
	.align	8
GLOBAL(stub32_execve)
	CFI_STARTPROC
	call	compat_sys_execve
	jmp	return_from_execve
	CFI_ENDPROC
END(stub32_execve)
	.align	8
GLOBAL(stub32_execveat)
	CFI_STARTPROC
	call	compat_sys_execveat
	jmp	return_from_execve
	CFI_ENDPROC
END(stub32_execveat)
#endif

/*
 * sigreturn is special because it needs to restore all registers on return.
 * This cannot be done with SYSRET, so use the IRET return path instead.
 */
ENTRY(stub_rt_sigreturn)
	CFI_STARTPROC
	DEFAULT_FRAME 0, 8
	/*
	 * SAVE_EXTRA_REGS result is not normally needed:
	 * sigreturn overwrites all pt_regs->GPREGS.
	 * But sigreturn can fail (!), and there is no easy way to detect that.
	 * To make sure RESTORE_EXTRA_REGS doesn't restore garbage on error,
	 * we SAVE_EXTRA_REGS here.
	 */
	SAVE_EXTRA_REGS 8
	call sys_rt_sigreturn
return_from_stub:
	addq	$8, %rsp
	CFI_ADJUST_CFA_OFFSET -8
	RESTORE_EXTRA_REGS
	movq %rax,RAX(%rsp)
	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(stub_rt_sigreturn)

#ifdef CONFIG_X86_X32_ABI
ENTRY(stub_x32_rt_sigreturn)
	CFI_STARTPROC
	DEFAULT_FRAME 0, 8
	SAVE_EXTRA_REGS 8
	call sys32_x32_rt_sigreturn
	jmp  return_from_stub
	CFI_ENDPROC
END(stub_x32_rt_sigreturn)
#endif

/*
 * A newly forked process directly context switches into this address.
 *
 * rdi: prev task we switched from
 */
ENTRY(ret_from_fork)
	DEFAULT_FRAME

	LOCK ; btr $TIF_FORK,TI_flags(%r8)

	pushq_cfi $0x0002
	popfq_cfi				# reset kernel eflags

	call schedule_tail			# rdi: 'prev' task parameter

	RESTORE_EXTRA_REGS

	testl $3,CS(%rsp)			# from kernel_thread?

	/*
	 * By the time we get here, we have no idea whether our pt_regs,
	 * ti flags, and ti status came from the 64-bit SYSCALL fast path,
	 * the slow path, or one of the ia32entry paths.
	 * Use IRET code path to return, since it can safely handle
	 * all of the above.
	 */
	jnz	int_ret_from_sys_call

	/* We came from kernel_thread */
	/* nb: we depend on RESTORE_EXTRA_REGS above */
	movq %rbp, %rdi
	call *%rbx
	movl $0, RAX(%rsp)
	RESTORE_EXTRA_REGS
	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(ret_from_fork)

/*
 * Build the entry stubs with some assembler magic.
 * We pack 1 stub into every 8-byte block.
 */
	.align 8
ENTRY(irq_entries_start)
	INTR_FRAME
    vector=FIRST_EXTERNAL_VECTOR
    .rept (FIRST_SYSTEM_VECTOR - FIRST_EXTERNAL_VECTOR)
	pushq_cfi $(~vector+0x80)	/* Note: always in signed byte range */
    vector=vector+1
	jmp	common_interrupt
	CFI_ADJUST_CFA_OFFSET -8
	.align	8
    .endr
	CFI_ENDPROC
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
	/*
	 * Since nothing in interrupt handling code touches r12...r15 members
	 * of "struct pt_regs", and since interrupts can nest, we can save
	 * four stack slots and simultaneously provide
	 * an unwind-friendly stack layout by saving "truncated" pt_regs
	 * exactly up to rbp slot, without these members.
	 */
	ALLOC_PT_GPREGS_ON_STACK -RBP
	SAVE_C_REGS -RBP
	/* this goes to 0(%rsp) for unwinder, not for saving the value: */
	SAVE_EXTRA_REGS_RBP -RBP

	leaq -RBP(%rsp),%rdi	/* arg1 for \func (pointer to pt_regs) */

	testl $3, CS-RBP(%rsp)
	je 1f
	SWAPGS
1:
	/*
	 * Save previous stack pointer, optionally switch to interrupt stack.
	 * irq_count is used to check if a CPU is already on an interrupt stack
	 * or not. While this is essentially redundant with preempt_count it is
	 * a little cheaper to use a separate counter in the PDA (short of
	 * moving irq_enter into assembly, which would be too much work)
	 */
	movq %rsp, %rsi
	incl PER_CPU_VAR(irq_count)
	cmovzq PER_CPU_VAR(irq_stack_ptr),%rsp
	CFI_DEF_CFA_REGISTER	rsi
	pushq %rsi
	/*
	 * For debugger:
	 * "CFA (Current Frame Address) is the value on stack + offset"
	 */
	CFI_ESCAPE	0x0f /* DW_CFA_def_cfa_expression */, 6, \
			0x77 /* DW_OP_breg7 (rsp) */, 0, \
			0x06 /* DW_OP_deref */, \
			0x08 /* DW_OP_const1u */, SIZEOF_PTREGS-RBP, \
			0x22 /* DW_OP_plus */
	/* We entered an interrupt context - irqs are off: */
	TRACE_IRQS_OFF

	call \func
	.endm

	/*
	 * The interrupt stubs push (~vector+0x80) onto the stack and
	 * then jump to common_interrupt.
	 */
	.p2align CONFIG_X86_L1_CACHE_SHIFT
common_interrupt:
	XCPT_FRAME
	ASM_CLAC
	addq $-0x80,(%rsp)		/* Adjust vector to [-256,-1] range */
	interrupt do_IRQ
	/* 0(%rsp): old RSP */
ret_from_intr:
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	decl PER_CPU_VAR(irq_count)

	/* Restore saved previous stack */
	popq %rsi
	CFI_DEF_CFA rsi,SIZEOF_PTREGS-RBP /* reg/off reset after def_cfa_expr */
	/* return code expects complete pt_regs - adjust rsp accordingly: */
	leaq -RBP(%rsi),%rsp
	CFI_DEF_CFA_REGISTER	rsp
	CFI_ADJUST_CFA_OFFSET	RBP

	testl $3,CS(%rsp)
	je retint_kernel
	/* Interrupt came from user space */

	GET_THREAD_INFO(%rcx)
	/*
	 * %rcx: thread info. Interrupts off.
	 */
retint_with_reschedule:
	movl $_TIF_WORK_MASK,%edi
retint_check:
	LOCKDEP_SYS_EXIT_IRQ
	movl TI_flags(%rcx),%edx
	andl %edi,%edx
	CFI_REMEMBER_STATE
	jnz  retint_careful

retint_swapgs:		/* return to user-space */
	/*
	 * The iretq could re-enable interrupts:
	 */
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_IRETQ

	SWAPGS
	jmp	restore_c_regs_and_iret

/* Returning to kernel space */
retint_kernel:
#ifdef CONFIG_PREEMPT
	/* Interrupts are off */
	/* Check if we need preemption */
	bt	$9,EFLAGS(%rsp)	/* interrupts were off? */
	jnc	1f
0:	cmpl	$0,PER_CPU_VAR(__preempt_count)
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
	testb $4,(SS-RIP)(%rsp)
	jnz native_irq_return_ldt
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
	pushq_cfi %rax
	pushq_cfi %rdi
	SWAPGS
	movq PER_CPU_VAR(espfix_waddr),%rdi
	movq %rax,(0*8)(%rdi)	/* RAX */
	movq (2*8)(%rsp),%rax	/* RIP */
	movq %rax,(1*8)(%rdi)
	movq (3*8)(%rsp),%rax	/* CS */
	movq %rax,(2*8)(%rdi)
	movq (4*8)(%rsp),%rax	/* RFLAGS */
	movq %rax,(3*8)(%rdi)
	movq (6*8)(%rsp),%rax	/* SS */
	movq %rax,(5*8)(%rdi)
	movq (5*8)(%rsp),%rax	/* RSP */
	movq %rax,(4*8)(%rdi)
	andl $0xffff0000,%eax
	popq_cfi %rdi
	orq PER_CPU_VAR(espfix_stack),%rax
	SWAPGS
	movq %rax,%rsp
	popq_cfi %rax
	jmp native_irq_return_iret
#endif

	/* edi: workmask, edx: work */
retint_careful:
	CFI_RESTORE_STATE
	bt    $TIF_NEED_RESCHED,%edx
	jnc   retint_signal
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushq_cfi %rdi
	SCHEDULE_USER
	popq_cfi %rdi
	GET_THREAD_INFO(%rcx)
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp retint_check

retint_signal:
	testl $_TIF_DO_NOTIFY_MASK,%edx
	jz    retint_swapgs
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	SAVE_EXTRA_REGS
	movq $-1,ORIG_RAX(%rsp)
	xorl %esi,%esi		# oldset
	movq %rsp,%rdi		# &pt_regs
	call do_notify_resume
	RESTORE_EXTRA_REGS
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	GET_THREAD_INFO(%rcx)
	jmp retint_with_reschedule

	CFI_ENDPROC
END(common_interrupt)

/*
 * APIC interrupts.
 */
.macro apicinterrupt3 num sym do_sym
ENTRY(\sym)
	INTR_FRAME
	ASM_CLAC
	pushq_cfi $~(\num)
.Lcommon_\sym:
	interrupt \do_sym
	jmp ret_from_intr
	CFI_ENDPROC
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
apicinterrupt3 IRQ_MOVE_CLEANUP_VECTOR \
	irq_move_cleanup_interrupt smp_irq_move_cleanup_interrupt
apicinterrupt3 REBOOT_VECTOR \
	reboot_interrupt smp_reboot_interrupt
#endif

#ifdef CONFIG_X86_UV
apicinterrupt3 UV_BAU_MESSAGE \
	uv_bau_message_intr1 uv_bau_message_interrupt
#endif
apicinterrupt LOCAL_TIMER_VECTOR \
	apic_timer_interrupt smp_apic_timer_interrupt
apicinterrupt X86_PLATFORM_IPI_VECTOR \
	x86_platform_ipi smp_x86_platform_ipi

#ifdef CONFIG_HAVE_KVM
apicinterrupt3 POSTED_INTR_VECTOR \
	kvm_posted_intr_ipi smp_kvm_posted_intr_ipi
#endif

#ifdef CONFIG_X86_MCE_THRESHOLD
apicinterrupt THRESHOLD_APIC_VECTOR \
	threshold_interrupt smp_threshold_interrupt
#endif

#ifdef CONFIG_X86_THERMAL_VECTOR
apicinterrupt THERMAL_APIC_VECTOR \
	thermal_interrupt smp_thermal_interrupt
#endif

#ifdef CONFIG_SMP
apicinterrupt CALL_FUNCTION_SINGLE_VECTOR \
	call_function_single_interrupt smp_call_function_single_interrupt
apicinterrupt CALL_FUNCTION_VECTOR \
	call_function_interrupt smp_call_function_interrupt
apicinterrupt RESCHEDULE_VECTOR \
	reschedule_interrupt smp_reschedule_interrupt
#endif

apicinterrupt ERROR_APIC_VECTOR \
	error_interrupt smp_error_interrupt
apicinterrupt SPURIOUS_APIC_VECTOR \
	spurious_interrupt smp_spurious_interrupt

#ifdef CONFIG_IRQ_WORK
apicinterrupt IRQ_WORK_VECTOR \
	irq_work_interrupt smp_irq_work_interrupt
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

	.if \has_error_code
	XCPT_FRAME
	.else
	INTR_FRAME
	.endif

	ASM_CLAC
	PARAVIRT_ADJUST_EXCEPTION_FRAME

	.ifeq \has_error_code
	pushq_cfi $-1			/* ORIG_RAX: no syscall to restart */
	.endif

	ALLOC_PT_GPREGS_ON_STACK

	.if \paranoid
	.if \paranoid == 1
	CFI_REMEMBER_STATE
	testl $3, CS(%rsp)		/* If coming from userspace, switch */
	jnz 1f				/* stacks. */
	.endif
	call paranoid_entry
	.else
	call error_entry
	.endif
	/* returned flag: ebx=0: need swapgs on exit, ebx=1: don't need it */

	DEFAULT_FRAME 0

	.if \paranoid
	.if \shift_ist != -1
	TRACE_IRQS_OFF_DEBUG		/* reload IDT in case of recursion */
	.else
	TRACE_IRQS_OFF
	.endif
	.endif

	movq %rsp,%rdi			/* pt_regs pointer */

	.if \has_error_code
	movq ORIG_RAX(%rsp),%rsi	/* get error code */
	movq $-1,ORIG_RAX(%rsp)		/* no syscall to restart */
	.else
	xorl %esi,%esi			/* no error code */
	.endif

	.if \shift_ist != -1
	subq $EXCEPTION_STKSZ, CPU_TSS_IST(\shift_ist)
	.endif

	call \do_sym

	.if \shift_ist != -1
	addq $EXCEPTION_STKSZ, CPU_TSS_IST(\shift_ist)
	.endif

	/* these procedures expect "no swapgs" flag in ebx */
	.if \paranoid
	jmp paranoid_exit
	.else
	jmp error_exit
	.endif

	.if \paranoid == 1
	CFI_RESTORE_STATE
	/*
	 * Paranoid entry from userspace.  Switch stacks and treat it
	 * as a normal entry.  This means that paranoid handlers
	 * run in real process context if user_mode(regs).
	 */
1:
	call error_entry

	DEFAULT_FRAME 0

	movq %rsp,%rdi			/* pt_regs pointer */
	call sync_regs
	movq %rax,%rsp			/* switch stack */

	movq %rsp,%rdi			/* pt_regs pointer */

	.if \has_error_code
	movq ORIG_RAX(%rsp),%rsi	/* get error code */
	movq $-1,ORIG_RAX(%rsp)		/* no syscall to restart */
	.else
	xorl %esi,%esi			/* no error code */
	.endif

	call \do_sym

	jmp error_exit			/* %ebx: no swapgs flag */
	.endif

	CFI_ENDPROC
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

idtentry divide_error do_divide_error has_error_code=0
idtentry overflow do_overflow has_error_code=0
idtentry bounds do_bounds has_error_code=0
idtentry invalid_op do_invalid_op has_error_code=0
idtentry device_not_available do_device_not_available has_error_code=0
idtentry double_fault do_double_fault has_error_code=1 paranoid=2
idtentry coprocessor_segment_overrun do_coprocessor_segment_overrun has_error_code=0
idtentry invalid_TSS do_invalid_TSS has_error_code=1
idtentry segment_not_present do_segment_not_present has_error_code=1
idtentry spurious_interrupt_bug do_spurious_interrupt_bug has_error_code=0
idtentry coprocessor_error do_coprocessor_error has_error_code=0
idtentry alignment_check do_alignment_check has_error_code=1
idtentry simd_coprocessor_error do_simd_coprocessor_error has_error_code=0


	/* Reload gs selector with exception handling */
	/* edi:  new selector */
ENTRY(native_load_gs_index)
	CFI_STARTPROC
	pushfq_cfi
	DISABLE_INTERRUPTS(CLBR_ANY & ~CLBR_RDI)
	SWAPGS
gs_change:
	movl %edi,%gs
2:	mfence		/* workaround */
	SWAPGS
	popfq_cfi
	ret
	CFI_ENDPROC
END(native_load_gs_index)

	_ASM_EXTABLE(gs_change,bad_gs)
	.section .fixup,"ax"
	/* running with kernelgs */
bad_gs:
	SWAPGS			/* switch back to user gs */
	xorl %eax,%eax
	movl %eax,%gs
	jmp  2b
	.previous

/* Call softirq on interrupt stack. Interrupts are off. */
ENTRY(do_softirq_own_stack)
	CFI_STARTPROC
	pushq_cfi %rbp
	CFI_REL_OFFSET rbp,0
	mov  %rsp,%rbp
	CFI_DEF_CFA_REGISTER rbp
	incl PER_CPU_VAR(irq_count)
	cmove PER_CPU_VAR(irq_stack_ptr),%rsp
	push  %rbp			# backlink for old unwinder
	call __do_softirq
	leaveq
	CFI_RESTORE		rbp
	CFI_DEF_CFA_REGISTER	rsp
	CFI_ADJUST_CFA_OFFSET   -8
	decl PER_CPU_VAR(irq_count)
	ret
	CFI_ENDPROC
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
ENTRY(xen_do_hypervisor_callback)   # do_hypervisor_callback(struct *pt_regs)
	CFI_STARTPROC
/*
 * Since we don't modify %rdi, evtchn_do_upall(struct *pt_regs) will
 * see the correct pointer to the pt_regs
 */
	movq %rdi, %rsp            # we don't return, adjust the stack frame
	CFI_ENDPROC
	DEFAULT_FRAME
11:	incl PER_CPU_VAR(irq_count)
	movq %rsp,%rbp
	CFI_DEF_CFA_REGISTER rbp
	cmovzq PER_CPU_VAR(irq_stack_ptr),%rsp
	pushq %rbp			# backlink for old unwinder
	call xen_evtchn_do_upcall
	popq %rsp
	CFI_DEF_CFA_REGISTER rsp
	decl PER_CPU_VAR(irq_count)
#ifndef CONFIG_PREEMPT
	call xen_maybe_preempt_hcall
#endif
	jmp  error_exit
	CFI_ENDPROC
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
	INTR_FRAME 1 (6*8)
	/*CFI_REL_OFFSET gs,GS*/
	/*CFI_REL_OFFSET fs,FS*/
	/*CFI_REL_OFFSET es,ES*/
	/*CFI_REL_OFFSET ds,DS*/
	CFI_REL_OFFSET r11,8
	CFI_REL_OFFSET rcx,0
	movw %ds,%cx
	cmpw %cx,0x10(%rsp)
	CFI_REMEMBER_STATE
	jne 1f
	movw %es,%cx
	cmpw %cx,0x18(%rsp)
	jne 1f
	movw %fs,%cx
	cmpw %cx,0x20(%rsp)
	jne 1f
	movw %gs,%cx
	cmpw %cx,0x28(%rsp)
	jne 1f
	/* All segments match their saved values => Category 2 (Bad IRET). */
	movq (%rsp),%rcx
	CFI_RESTORE rcx
	movq 8(%rsp),%r11
	CFI_RESTORE r11
	addq $0x30,%rsp
	CFI_ADJUST_CFA_OFFSET -0x30
	pushq_cfi $0	/* RIP */
	pushq_cfi %r11
	pushq_cfi %rcx
	jmp general_protection
	CFI_RESTORE_STATE
1:	/* Segment mismatch => Category 1 (Bad segment). Retry the IRET. */
	movq (%rsp),%rcx
	CFI_RESTORE rcx
	movq 8(%rsp),%r11
	CFI_RESTORE r11
	addq $0x30,%rsp
	CFI_ADJUST_CFA_OFFSET -0x30
	pushq_cfi $-1 /* orig_ax = -1 => not a system call */
	ALLOC_PT_GPREGS_ON_STACK
	SAVE_C_REGS
	SAVE_EXTRA_REGS
	jmp error_exit
	CFI_ENDPROC
END(xen_failsafe_callback)

apicinterrupt3 HYPERVISOR_CALLBACK_VECTOR \
	xen_hvm_callback_vector xen_evtchn_do_upcall

#endif /* CONFIG_XEN */

#if IS_ENABLED(CONFIG_HYPERV)
apicinterrupt3 HYPERVISOR_CALLBACK_VECTOR \
	hyperv_callback_vector hyperv_vector_handler
#endif /* CONFIG_HYPERV */

idtentry debug do_debug has_error_code=0 paranoid=1 shift_ist=DEBUG_STACK
idtentry int3 do_int3 has_error_code=0 paranoid=1 shift_ist=DEBUG_STACK
idtentry stack_segment do_stack_segment has_error_code=1
#ifdef CONFIG_XEN
idtentry xen_debug do_debug has_error_code=0
idtentry xen_int3 do_int3 has_error_code=0
idtentry xen_stack_segment do_stack_segment has_error_code=1
#endif
idtentry general_protection do_general_protection has_error_code=1
trace_idtentry page_fault do_page_fault has_error_code=1
#ifdef CONFIG_KVM_GUEST
idtentry async_page_fault do_async_page_fault has_error_code=1
#endif
#ifdef CONFIG_X86_MCE
idtentry machine_check has_error_code=0 paranoid=1 do_sym=*machine_check_vector(%rip)
#endif

/*
 * Save all registers in pt_regs, and switch gs if needed.
 * Use slow, but surefire "are we in kernel?" check.
 * Return: ebx=0: need swapgs on exit, ebx=1: otherwise
 */
ENTRY(paranoid_entry)
	XCPT_FRAME 1 15*8
	cld
	SAVE_C_REGS 8
	SAVE_EXTRA_REGS 8
	movl $1,%ebx
	movl $MSR_GS_BASE,%ecx
	rdmsr
	testl %edx,%edx
	js 1f	/* negative -> in kernel */
	SWAPGS
	xorl %ebx,%ebx
1:	ret
	CFI_ENDPROC
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
 */
/* On entry, ebx is "no swapgs" flag (1: don't need swapgs, 0: need it) */
ENTRY(paranoid_exit)
	DEFAULT_FRAME
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF_DEBUG
	testl %ebx,%ebx				/* swapgs needed? */
	jnz paranoid_exit_no_swapgs
	TRACE_IRQS_IRETQ
	SWAPGS_UNSAFE_STACK
	jmp paranoid_exit_restore
paranoid_exit_no_swapgs:
	TRACE_IRQS_IRETQ_DEBUG
paranoid_exit_restore:
	RESTORE_EXTRA_REGS
	RESTORE_C_REGS
	REMOVE_PT_GPREGS_FROM_STACK 8
	INTERRUPT_RETURN
	CFI_ENDPROC
END(paranoid_exit)

/*
 * Save all registers in pt_regs, and switch gs if needed.
 * Return: ebx=0: need swapgs on exit, ebx=1: otherwise
 */
ENTRY(error_entry)
	XCPT_FRAME 1 15*8
	cld
	SAVE_C_REGS 8
	SAVE_EXTRA_REGS 8
	xorl %ebx,%ebx
	testl $3,CS+8(%rsp)
	je error_kernelspace
error_swapgs:
	SWAPGS
error_sti:
	TRACE_IRQS_OFF
	ret

	/*
	 * There are two places in the kernel that can potentially fault with
	 * usergs. Handle them here.  B stepping K8s sometimes report a
	 * truncated RIP for IRET exceptions returning to compat mode. Check
	 * for these here too.
	 */
error_kernelspace:
	CFI_REL_OFFSET rcx, RCX+8
	incl %ebx
	leaq native_irq_return_iret(%rip),%rcx
	cmpq %rcx,RIP+8(%rsp)
	je error_bad_iret
	movl %ecx,%eax	/* zero extend */
	cmpq %rax,RIP+8(%rsp)
	je bstep_iret
	cmpq $gs_change,RIP+8(%rsp)
	je error_swapgs
	jmp error_sti

bstep_iret:
	/* Fix truncated RIP */
	movq %rcx,RIP+8(%rsp)
	/* fall through */

error_bad_iret:
	SWAPGS
	mov %rsp,%rdi
	call fixup_bad_iret
	mov %rax,%rsp
	decl %ebx	/* Return to usergs */
	jmp error_sti
	CFI_ENDPROC
END(error_entry)


/* On entry, ebx is "no swapgs" flag (1: don't need swapgs, 0: need it) */
ENTRY(error_exit)
	DEFAULT_FRAME
	movl %ebx,%eax
	RESTORE_EXTRA_REGS
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	GET_THREAD_INFO(%rcx)
	testl %eax,%eax
	jne retint_kernel
	LOCKDEP_SYS_EXIT_IRQ
	movl TI_flags(%rcx),%edx
	movl $_TIF_WORK_MASK,%edi
	andl %edi,%edx
	jnz retint_careful
	jmp retint_swapgs
	CFI_ENDPROC
END(error_exit)

/* Runs on exception stack */
ENTRY(nmi)
	INTR_FRAME
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

	/* Use %rdx as our temp variable throughout */
	pushq_cfi %rdx
	CFI_REL_OFFSET rdx, 0

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
	cld
	movq	%rsp, %rdx
	movq	PER_CPU_VAR(kernel_stack), %rsp
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
	call	do_nmi

	/*
	 * Return back to user mode.  We must *not* do the normal exit
	 * work, because we don't want to enable interrupts.  Fortunately,
	 * do_nmi doesn't modify pt_regs.
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
	cmpl $1, -8(%rsp)
	je nested_nmi

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

	CFI_REMEMBER_STATE

nested_nmi:
	/*
	 * Modify the "iret" frame to point to repeat_nmi, forcing another
	 * iteration of NMI handling.
	 */
	leaq -1*8(%rsp), %rdx
	movq %rdx, %rsp
	CFI_ADJUST_CFA_OFFSET 1*8
	leaq -10*8(%rsp), %rdx
	pushq_cfi $__KERNEL_DS
	pushq_cfi %rdx
	pushfq_cfi
	pushq_cfi $__KERNEL_CS
	pushq_cfi $repeat_nmi

	/* Put stack back */
	addq $(6*8), %rsp
	CFI_ADJUST_CFA_OFFSET -6*8

nested_nmi_out:
	popq_cfi %rdx
	CFI_RESTORE rdx

	/* We are returning to kernel mode, so this cannot result in a fault. */
	INTERRUPT_RETURN

	CFI_RESTORE_STATE
first_nmi:
	/* Restore rdx. */
	movq (%rsp), %rdx
	CFI_RESTORE rdx

	/* Set "NMI executing" on the stack. */
	pushq_cfi $1

	/* Leave room for the "iret" frame */
	subq $(5*8), %rsp
	CFI_ADJUST_CFA_OFFSET 5*8

	/* Copy the "original" frame to the "outermost" frame */
	.rept 5
	pushq_cfi 11*8(%rsp)
	.endr
	CFI_DEF_CFA_OFFSET 5*8

	/* Everything up to here is safe from nested NMIs */

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
	 * gsbase if needed before we call do_nmi.
	 *
	 * Set "NMI executing" in case we came back here via IRET.
	 */
	movq $1, 10*8(%rsp)

	/*
	 * Copy the "outermost" frame to the "iret" frame.  NMIs that nest
	 * here must not modify the "iret" frame while we're writing to
	 * it or it will end up containing garbage.
	 */
	addq $(10*8), %rsp
	CFI_ADJUST_CFA_OFFSET -10*8
	.rept 5
	pushq_cfi -6*8(%rsp)
	.endr
	subq $(5*8), %rsp
	CFI_DEF_CFA_OFFSET 5*8
end_repeat_nmi:

	/*
	 * Everything below this point can be preempted by a nested NMI.
	 * If this happens, then the inner NMI will change the "iret"
	 * frame to point back to repeat_nmi.
	 */
	pushq_cfi $-1		/* ORIG_RAX: no syscall to restart */
	ALLOC_PT_GPREGS_ON_STACK

	/*
	 * Use paranoid_entry to handle SWAPGS, but no need to use paranoid_exit
	 * as we should not be calling schedule in NMI context.
	 * Even with normal interrupts enabled. An NMI should not be
	 * setting NEED_RESCHED or anything that normal interrupts and
	 * exceptions might do.
	 */
	call paranoid_entry
	DEFAULT_FRAME 0

	/* paranoidentry do_nmi, 0; without TRACE_IRQS_OFF */
	movq %rsp,%rdi
	movq $-1,%rsi
	call do_nmi

	testl %ebx,%ebx				/* swapgs needed? */
	jnz nmi_restore
nmi_swapgs:
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
	CFI_ENDPROC
END(nmi)

ENTRY(ignore_sysret)
	CFI_STARTPROC
	mov $-ENOSYS,%eax
	sysret
	CFI_ENDPROC
END(ignore_sysret)

