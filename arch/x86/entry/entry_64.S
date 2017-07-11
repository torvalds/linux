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
#include <asm/export.h>
#include <linux/err.h>

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
 * This is the only entry point used for 64-bit system calls.  The
 * hardware interface is reasonably well designed and the register to
 * argument mapping Linux uses fits well with the registers that are
 * available when SYSCALL is used.
 *
 * SYSCALL instructions can be found inlined in libc implementations as
 * well as some other programs and libraries.  There are also a handful
 * of SYSCALL instructions in the vDSO used, for example, as a
 * clock_gettimeofday fallback.
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
	/*
	 * A hypervisor implementation might want to use a label
	 * after the swapgs, so that it can do the swapgs
	 * for the guest and jump here on syscall.
	 */
GLOBAL(entry_SYSCALL_64_after_swapgs)

	movq	%rsp, PER_CPU_VAR(rsp_scratch)
	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rsp

	TRACE_IRQS_OFF

	/* Construct struct pt_regs on stack */
	pushq	$__USER_DS			/* pt_regs->ss */
	pushq	PER_CPU_VAR(rsp_scratch)	/* pt_regs->sp */
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

	/*
	 * If we need to do entry work or if we guess we'll need to do
	 * exit work, go straight to the slow path.
	 */
	movq	PER_CPU_VAR(current_task), %r11
	testl	$_TIF_WORK_SYSCALL_ENTRY|_TIF_ALLWORK_MASK, TASK_TI_flags(%r11)
	jnz	entry_SYSCALL64_slow_path

entry_SYSCALL_64_fastpath:
	/*
	 * Easy case: enable interrupts and issue the syscall.  If the syscall
	 * needs pt_regs, we'll call a stub that disables interrupts again
	 * and jumps to the slow path.
	 */
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
#if __SYSCALL_MASK == ~0
	cmpq	$__NR_syscall_max, %rax
#else
	andl	$__SYSCALL_MASK, %eax
	cmpl	$__NR_syscall_max, %eax
#endif
	ja	1f				/* return -ENOSYS (already in pt_regs->ax) */
	movq	%r10, %rcx

	/*
	 * This call instruction is handled specially in stub_ptregs_64.
	 * It might end up jumping to the slow path.  If it jumps, RAX
	 * and all argument registers are clobbered.
	 */
	call	*sys_call_table(, %rax, 8)
.Lentry_SYSCALL_64_after_fastpath_call:

	movq	%rax, RAX(%rsp)
1:

	/*
	 * If we get here, then we know that pt_regs is clean for SYSRET64.
	 * If we see that no exit work is required (which we are required
	 * to check with IRQs off), then we can go straight to SYSRET64.
	 */
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF
	movq	PER_CPU_VAR(current_task), %r11
	testl	$_TIF_ALLWORK_MASK, TASK_TI_flags(%r11)
	jnz	1f

	LOCKDEP_SYS_EXIT
	TRACE_IRQS_ON		/* user mode is traced as IRQs on */
	movq	RIP(%rsp), %rcx
	movq	EFLAGS(%rsp), %r11
	RESTORE_C_REGS_EXCEPT_RCX_R11
	movq	RSP(%rsp), %rsp
	USERGS_SYSRET64

1:
	/*
	 * The fast path looked good when we started, but something changed
	 * along the way and we need to switch to the slow path.  Calling
	 * raise(3) will trigger this, for example.  IRQs are off.
	 */
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_ANY)
	SAVE_EXTRA_REGS
	movq	%rsp, %rdi
	call	syscall_return_slowpath	/* returns with IRQs disabled */
	jmp	return_from_SYSCALL_64

entry_SYSCALL64_slow_path:
	/* IRQs are off. */
	SAVE_EXTRA_REGS
	movq	%rsp, %rdi
	call	do_syscall_64		/* returns with IRQs disabled */

return_from_SYSCALL_64:
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
	 *
	 * Change top bits to match most significant bit (47th or 56th bit
	 * depending on paging mode) in the address.
	 */
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
	 * SYSCALL clears RF when it saves RFLAGS in R11 and SYSRET cannot
	 * restore RF properly. If the slowpath sets it for whatever reason, we
	 * need to restore it correctly.
	 *
	 * SYSRET can restore TF, but unlike IRET, restoring TF results in a
	 * trap from userspace immediately after SYSRET.  This would cause an
	 * infinite loop whenever #DB happens with register state that satisfies
	 * the opportunistic SYSRET conditions.  For example, single-stepping
	 * this user code:
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
	movq	RSP(%rsp), %rsp
	USERGS_SYSRET64

opportunistic_sysret_failed:
	SWAPGS
	jmp	restore_c_regs_and_iret
END(entry_SYSCALL_64)

ENTRY(stub_ptregs_64)
	/*
	 * Syscalls marked as needing ptregs land here.
	 * If we are on the fast path, we need to save the extra regs,
	 * which we achieve by trying again on the slow path.  If we are on
	 * the slow path, the extra regs are already saved.
	 *
	 * RAX stores a pointer to the C function implementing the syscall.
	 * IRQs are on.
	 */
	cmpq	$.Lentry_SYSCALL_64_after_fastpath_call, (%rsp)
	jne	1f

	/*
	 * Called from fast path -- disable IRQs again, pop return address
	 * and jump to slow path
	 */
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF
	popq	%rax
	jmp	entry_SYSCALL64_slow_path

1:
	jmp	*%rax				/* Called from C */
END(stub_ptregs_64)

.macro ptregs_stub func
ENTRY(ptregs_\func)
	leaq	\func(%rip), %rax
	jmp	stub_ptregs_64
END(ptregs_\func)
.endm

/* Instantiate ptregs_stub for each ptregs-using syscall */
#define __SYSCALL_64_QUAL_(sym)
#define __SYSCALL_64_QUAL_ptregs(sym) ptregs_stub sym
#define __SYSCALL_64(nr, sym, qual) __SYSCALL_64_QUAL_##qual(sym)
#include <asm/syscalls_64.h>

/*
 * %rdi: prev task
 * %rsi: next task
 */
ENTRY(__switch_to_asm)
	/*
	 * Save callee-saved registers
	 * This must match the order in inactive_task_frame
	 */
	pushq	%rbp
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	/* switch stack */
	movq	%rsp, TASK_threadsp(%rdi)
	movq	TASK_threadsp(%rsi), %rsp

#ifdef CONFIG_CC_STACKPROTECTOR
	movq	TASK_stack_canary(%rsi), %rbx
	movq	%rbx, PER_CPU_VAR(irq_stack_union)+stack_canary_offset
#endif

	/* restore callee-saved registers */
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp

	jmp	__switch_to
END(__switch_to_asm)

/*
 * A newly forked process directly context switches into this address.
 *
 * rax: prev task we switched from
 * rbx: kernel thread func (NULL for user thread)
 * r12: kernel thread arg
 */
ENTRY(ret_from_fork)
	movq	%rax, %rdi
	call	schedule_tail			/* rdi: 'prev' task parameter */

	testq	%rbx, %rbx			/* from kernel_thread? */
	jnz	1f				/* kernel threads are uncommon */

2:
	movq	%rsp, %rdi
	call	syscall_return_slowpath	/* returns with IRQs disabled */
	TRACE_IRQS_ON			/* user mode is traced as IRQS on */
	SWAPGS
	jmp	restore_regs_and_iret

1:
	/* kernel thread */
	movq	%r12, %rdi
	call	*%rbx
	/*
	 * A kernel thread is allowed to return here after successfully
	 * calling do_execve().  Exit to userspace to complete the execve()
	 * syscall.
	 */
	movq	$0, RAX(%rsp)
	jmp	2b
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

.macro DEBUG_ENTRY_ASSERT_IRQS_OFF
#ifdef CONFIG_DEBUG_ENTRY
	pushfq
	testl $X86_EFLAGS_IF, (%rsp)
	jz .Lokay_\@
	ud2
.Lokay_\@:
	addq $8, %rsp
#endif
.endm

/*
 * Enters the IRQ stack if we're not already using it.  NMI-safe.  Clobbers
 * flags and puts old RSP into old_rsp, and leaves all other GPRs alone.
 * Requires kernel GSBASE.
 *
 * The invariant is that, if irq_count != -1, then the IRQ stack is in use.
 */
.macro ENTER_IRQ_STACK old_rsp
	DEBUG_ENTRY_ASSERT_IRQS_OFF
	movq	%rsp, \old_rsp
	incl	PER_CPU_VAR(irq_count)
	jnz	.Lirq_stack_push_old_rsp_\@

	/*
	 * Right now, if we just incremented irq_count to zero, we've
	 * claimed the IRQ stack but we haven't switched to it yet.
	 *
	 * If anything is added that can interrupt us here without using IST,
	 * it must be *extremely* careful to limit its stack usage.  This
	 * could include kprobes and a hypothetical future IST-less #DB
	 * handler.
	 *
	 * The OOPS unwinder relies on the word at the top of the IRQ
	 * stack linking back to the previous RSP for the entire time we're
	 * on the IRQ stack.  For this to work reliably, we need to write
	 * it before we actually move ourselves to the IRQ stack.
	 */

	movq	\old_rsp, PER_CPU_VAR(irq_stack_union + IRQ_STACK_SIZE - 8)
	movq	PER_CPU_VAR(irq_stack_ptr), %rsp

#ifdef CONFIG_DEBUG_ENTRY
	/*
	 * If the first movq above becomes wrong due to IRQ stack layout
	 * changes, the only way we'll notice is if we try to unwind right
	 * here.  Assert that we set up the stack right to catch this type
	 * of bug quickly.
	 */
	cmpq	-8(%rsp), \old_rsp
	je	.Lirq_stack_okay\@
	ud2
	.Lirq_stack_okay\@:
#endif

.Lirq_stack_push_old_rsp_\@:
	pushq	\old_rsp
.endm

/*
 * Undoes ENTER_IRQ_STACK.
 */
.macro LEAVE_IRQ_STACK
	DEBUG_ENTRY_ASSERT_IRQS_OFF
	/* We need to be off the IRQ stack before decrementing irq_count. */
	popq	%rsp

	/*
	 * As in ENTER_IRQ_STACK, irq_count == 0, we are still claiming
	 * the irq stack but we're not on it.
	 */

	decl	PER_CPU_VAR(irq_count)
.endm

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
	ENCODE_FRAME_POINTER

	testb	$3, CS(%rsp)
	jz	1f

	/*
	 * IRQ from user mode.  Switch to kernel gsbase and inform context
	 * tracking that we're in kernel mode.
	 */
	SWAPGS

	/*
	 * We need to tell lockdep that IRQs are off.  We can't do this until
	 * we fix gsbase, and we should do it before enter_from_user_mode
	 * (which can take locks).  Since TRACE_IRQS_OFF idempotent,
	 * the simplest way to handle it is to just call it twice if
	 * we enter from user mode.  There's no reason to optimize this since
	 * TRACE_IRQS_OFF is a no-op if lockdep is off.
	 */
	TRACE_IRQS_OFF

	CALL_enter_from_user_mode

1:
	ENTER_IRQ_STACK old_rsp=%rdi
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
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF

	LEAVE_IRQ_STACK

	testb	$3, CS(%rsp)
	jz	retint_kernel

	/* Interrupt came from user space */
GLOBAL(retint_user)
	mov	%rsp,%rdi
	call	prepare_exit_to_usermode
	TRACE_IRQS_IRETQ
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
	/*
	 * We are running with user GSBASE.  All GPRs contain their user
	 * values.  We have a percpu ESPFIX stack that is eight slots
	 * long (see ESPFIX_STACK_SIZE).  espfix_waddr points to the bottom
	 * of the ESPFIX stack.
	 *
	 * We clobber RAX and RDI in this code.  We stash RDI on the
	 * normal stack and RAX on the ESPFIX stack.
	 *
	 * The ESPFIX stack layout we set up looks like this:
	 *
	 * --- top of ESPFIX stack ---
	 * SS
	 * RSP
	 * RFLAGS
	 * CS
	 * RIP  <-- RSP points here when we're done
	 * RAX  <-- espfix_waddr points here
	 * --- bottom of ESPFIX stack ---
	 */

	pushq	%rdi				/* Stash user RDI */
	SWAPGS
	movq	PER_CPU_VAR(espfix_waddr), %rdi
	movq	%rax, (0*8)(%rdi)		/* user RAX */
	movq	(1*8)(%rsp), %rax		/* user RIP */
	movq	%rax, (1*8)(%rdi)
	movq	(2*8)(%rsp), %rax		/* user CS */
	movq	%rax, (2*8)(%rdi)
	movq	(3*8)(%rsp), %rax		/* user RFLAGS */
	movq	%rax, (3*8)(%rdi)
	movq	(5*8)(%rsp), %rax		/* user SS */
	movq	%rax, (5*8)(%rdi)
	movq	(4*8)(%rsp), %rax		/* user RSP */
	movq	%rax, (4*8)(%rdi)
	/* Now RAX == RSP. */

	andl	$0xffff0000, %eax		/* RAX = (RSP & 0xffff0000) */
	popq	%rdi				/* Restore user RDI */

	/*
	 * espfix_stack[31:16] == 0.  The page tables are set up such that
	 * (espfix_stack | (X & 0xffff0000)) points to a read-only alias of
	 * espfix_waddr for any X.  That is, there are 65536 RO aliases of
	 * the same page.  Set up RSP so that RSP[31:16] contains the
	 * respective 16 bits of the /userspace/ RSP and RSP nonetheless
	 * still points to an RO alias of the ESPFIX stack.
	 */
	orq	PER_CPU_VAR(espfix_stack), %rax
	SWAPGS
	movq	%rax, %rsp

	/*
	 * At this point, we cannot write to the stack any more, but we can
	 * still read.
	 */
	popq	%rax				/* Restore user RAX */

	/*
	 * RSP now points to an ordinary IRET frame, except that the page
	 * is read-only and RSP[31:16] are preloaded with the userspace
	 * values.  We can now IRET back to userspace.
	 */
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

/* Make sure APIC interrupt handlers end up in the irqentry section: */
#if defined(CONFIG_FUNCTION_GRAPH_TRACER) || defined(CONFIG_KASAN)
# define PUSH_SECTION_IRQENTRY	.pushsection .irqentry.text, "ax"
# define POP_SECTION_IRQENTRY	.popsection
#else
# define PUSH_SECTION_IRQENTRY
# define POP_SECTION_IRQENTRY
#endif

.macro apicinterrupt num sym do_sym
PUSH_SECTION_IRQENTRY
apicinterrupt3 \num \sym \do_sym
trace_apicinterrupt \num \sym
POP_SECTION_IRQENTRY
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
.Lgs_change:
	movl	%edi, %gs
2:	ALTERNATIVE "", "mfence", X86_BUG_SWAPGS_FENCE
	SWAPGS
	popfq
	ret
END(native_load_gs_index)
EXPORT_SYMBOL(native_load_gs_index)

	_ASM_EXTABLE(.Lgs_change, bad_gs)
	.section .fixup, "ax"
	/* running with kernelgs */
bad_gs:
	SWAPGS					/* switch back to user gs */
.macro ZAP_GS
	/* This can't be a string because the preprocessor needs to see it. */
	movl $__USER_DS, %eax
	movl %eax, %gs
.endm
	ALTERNATIVE "", "ZAP_GS", X86_BUG_NULL_SEG
	xorl	%eax, %eax
	movl	%eax, %gs
	jmp	2b
	.previous

/* Call softirq on interrupt stack. Interrupts are off. */
ENTRY(do_softirq_own_stack)
	pushq	%rbp
	mov	%rsp, %rbp
	ENTER_IRQ_STACK old_rsp=%r11
	call	__do_softirq
	LEAVE_IRQ_STACK
	leaveq
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

	ENTER_IRQ_STACK old_rsp=%r10
	call	xen_evtchn_do_upcall
	LEAVE_IRQ_STACK

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
	ENCODE_FRAME_POINTER
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
 * Return: ebx=0: need swapgs on exit, ebx=1: otherwise
 */
ENTRY(paranoid_entry)
	cld
	SAVE_C_REGS 8
	SAVE_EXTRA_REGS 8
	ENCODE_FRAME_POINTER 8
	movl	$1, %ebx
	movl	$MSR_GS_BASE, %ecx
	rdmsr
	testl	%edx, %edx
	js	1f				/* negative -> in kernel */
	SWAPGS
	xorl	%ebx, %ebx
1:	ret
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
 * On entry, ebx is "no swapgs" flag (1: don't need swapgs, 0: need it)
 */
ENTRY(paranoid_exit)
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF_DEBUG
	testl	%ebx, %ebx			/* swapgs needed? */
	jnz	paranoid_exit_no_swapgs
	TRACE_IRQS_IRETQ
	SWAPGS_UNSAFE_STACK
	jmp	paranoid_exit_restore
paranoid_exit_no_swapgs:
	TRACE_IRQS_IRETQ_DEBUG
paranoid_exit_restore:
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
	ENCODE_FRAME_POINTER 8
	xorl	%ebx, %ebx
	testb	$3, CS+8(%rsp)
	jz	.Lerror_kernelspace

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
	CALL_enter_from_user_mode
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
	cmpq	$.Lgs_change, RIP+8(%rsp)
	jne	.Lerror_entry_done

	/*
	 * hack: .Lgs_change can fail with user gsbase.  If this happens, fix up
	 * gsbase and proceed.  We'll fix up the exception and land in
	 * .Lgs_change's error handler with kernel gsbase.
	 */
	SWAPGS
	jmp .Lerror_entry_done

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
 * On entry, EBX is a "return to kernel mode" flag:
 *   1: already in kernel mode, don't need SWAPGS
 *   0: user gsbase is loaded, we need SWAPGS and standard preparation for return to usermode
 */
ENTRY(error_exit)
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF
	testl	%ebx, %ebx
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
	ENCODE_FRAME_POINTER

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
	 * work, because we don't want to enable interrupts.
	 */
	SWAPGS
	jmp	restore_regs_and_iret

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
	 * Use paranoid_entry to handle SWAPGS, but no need to use paranoid_exit
	 * as we should not be calling schedule in NMI context.
	 * Even with normal interrupts enabled. An NMI should not be
	 * setting NEED_RESCHED or anything that normal interrupts and
	 * exceptions might do.
	 */
	call	paranoid_entry

	/* paranoidentry do_nmi, 0; without TRACE_IRQS_OFF */
	movq	%rsp, %rdi
	movq	$-1, %rsi
	call	do_nmi

	testl	%ebx, %ebx			/* swapgs needed? */
	jnz	nmi_restore
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
END(nmi)

ENTRY(ignore_sysret)
	mov	$-ENOSYS, %eax
	sysret
END(ignore_sysret)

ENTRY(rewind_stack_do_exit)
	/* Prevent any naive code from trying to unwind to our caller. */
	xorl	%ebp, %ebp

	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rax
	leaq	-TOP_OF_KERNEL_STACK_PADDING-PTREGS_SIZE(%rax), %rsp

	call	do_exit
1:	jmp 1b
END(rewind_stack_do_exit)
