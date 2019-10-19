/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/arch/x86_64/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002  Andi Kleen SuSE Labs
 *  Copyright (C) 2000  Pavel Machek <pavel@suse.cz>
 *
 * entry.S contains the system-call and fault low-level handling routines.
 *
 * Some of this is documented in Documentation/x86/entry_64.rst
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
#include <asm/frame.h>
#include <asm/nospec-branch.h>
#include <linux/err.h>

#include "calling.h"

.code64
.section .entry.text, "ax"

#ifdef CONFIG_PARAVIRT
ENTRY(native_usergs_sysret64)
	UNWIND_HINT_EMPTY
	swapgs
	sysretq
END(native_usergs_sysret64)
#endif /* CONFIG_PARAVIRT */

.macro TRACE_IRQS_FLAGS flags:req
#ifdef CONFIG_TRACE_IRQFLAGS
	btl	$9, \flags		/* interrupts off? */
	jnc	1f
	TRACE_IRQS_ON
1:
#endif
.endm

.macro TRACE_IRQS_IRETQ
	TRACE_IRQS_FLAGS EFLAGS(%rsp)
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
	btl	$9, EFLAGS(%rsp)		/* interrupts off? */
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
	UNWIND_HINT_EMPTY
	/*
	 * Interrupts are off on entry.
	 * We do not frame this tiny irq-off block with TRACE_IRQS_OFF/ON,
	 * it is too small to ever cause noticeable irq latency.
	 */

	swapgs
	/* tss.sp2 is scratch space. */
	movq	%rsp, PER_CPU_VAR(cpu_tss_rw + TSS_sp2)
	SWITCH_TO_KERNEL_CR3 scratch_reg=%rsp
	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rsp

	/* Construct struct pt_regs on stack */
	pushq	$__USER_DS				/* pt_regs->ss */
	pushq	PER_CPU_VAR(cpu_tss_rw + TSS_sp2)	/* pt_regs->sp */
	pushq	%r11					/* pt_regs->flags */
	pushq	$__USER_CS				/* pt_regs->cs */
	pushq	%rcx					/* pt_regs->ip */
GLOBAL(entry_SYSCALL_64_after_hwframe)
	pushq	%rax					/* pt_regs->orig_ax */

	PUSH_AND_CLEAR_REGS rax=$-ENOSYS

	TRACE_IRQS_OFF

	/* IRQs are off. */
	movq	%rax, %rdi
	movq	%rsp, %rsi
	call	do_syscall_64		/* returns with IRQs disabled */

	TRACE_IRQS_IRETQ		/* we're about to change IF */

	/*
	 * Try to use SYSRET instead of IRET if we're returning to
	 * a completely clean 64-bit userspace context.  If we're not,
	 * go to the slow exit path.
	 */
	movq	RCX(%rsp), %rcx
	movq	RIP(%rsp), %r11

	cmpq	%rcx, %r11	/* SYSRET requires RCX == RIP */
	jne	swapgs_restore_regs_and_return_to_usermode

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
#ifdef CONFIG_X86_5LEVEL
	ALTERNATIVE "shl $(64 - 48), %rcx; sar $(64 - 48), %rcx", \
		"shl $(64 - 57), %rcx; sar $(64 - 57), %rcx", X86_FEATURE_LA57
#else
	shl	$(64 - (__VIRTUAL_MASK_SHIFT+1)), %rcx
	sar	$(64 - (__VIRTUAL_MASK_SHIFT+1)), %rcx
#endif

	/* If this changed %rcx, it was not canonical */
	cmpq	%rcx, %r11
	jne	swapgs_restore_regs_and_return_to_usermode

	cmpq	$__USER_CS, CS(%rsp)		/* CS must match SYSRET */
	jne	swapgs_restore_regs_and_return_to_usermode

	movq	R11(%rsp), %r11
	cmpq	%r11, EFLAGS(%rsp)		/* R11 == RFLAGS */
	jne	swapgs_restore_regs_and_return_to_usermode

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
	jnz	swapgs_restore_regs_and_return_to_usermode

	/* nothing to check for RSP */

	cmpq	$__USER_DS, SS(%rsp)		/* SS must match SYSRET */
	jne	swapgs_restore_regs_and_return_to_usermode

	/*
	 * We win! This label is here just for ease of understanding
	 * perf profiles. Nothing jumps here.
	 */
syscall_return_via_sysret:
	/* rcx and r11 are already restored (see code above) */
	UNWIND_HINT_EMPTY
	POP_REGS pop_rdi=0 skip_r11rcx=1

	/*
	 * Now all regs are restored except RSP and RDI.
	 * Save old stack pointer and switch to trampoline stack.
	 */
	movq	%rsp, %rdi
	movq	PER_CPU_VAR(cpu_tss_rw + TSS_sp0), %rsp

	pushq	RSP-RDI(%rdi)	/* RSP */
	pushq	(%rdi)		/* RDI */

	/*
	 * We are on the trampoline stack.  All regs except RDI are live.
	 * We can do future final exit work right here.
	 */
	STACKLEAK_ERASE_NOCLOBBER

	SWITCH_TO_USER_CR3_STACK scratch_reg=%rdi

	popq	%rdi
	popq	%rsp
	USERGS_SYSRET64
END(entry_SYSCALL_64)

/*
 * %rdi: prev task
 * %rsi: next task
 */
ENTRY(__switch_to_asm)
	UNWIND_HINT_FUNC
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

#ifdef CONFIG_STACKPROTECTOR
	movq	TASK_stack_canary(%rsi), %rbx
	movq	%rbx, PER_CPU_VAR(fixed_percpu_data) + stack_canary_offset
#endif

#ifdef CONFIG_RETPOLINE
	/*
	 * When switching from a shallower to a deeper call stack
	 * the RSB may either underflow or use entries populated
	 * with userspace addresses. On CPUs where those concerns
	 * exist, overwrite the RSB with entries which capture
	 * speculative execution to prevent attack.
	 */
	FILL_RETURN_BUFFER %r12, RSB_CLEAR_LOOPS, X86_FEATURE_RSB_CTXSW
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
	UNWIND_HINT_EMPTY
	movq	%rax, %rdi
	call	schedule_tail			/* rdi: 'prev' task parameter */

	testq	%rbx, %rbx			/* from kernel_thread? */
	jnz	1f				/* kernel threads are uncommon */

2:
	UNWIND_HINT_REGS
	movq	%rsp, %rdi
	call	syscall_return_slowpath	/* returns with IRQs disabled */
	TRACE_IRQS_ON			/* user mode is traced as IRQS on */
	jmp	swapgs_restore_regs_and_return_to_usermode

1:
	/* kernel thread */
	UNWIND_HINT_EMPTY
	movq	%r12, %rdi
	CALL_NOSPEC %rbx
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
	UNWIND_HINT_IRET_REGS
	pushq	$(~vector+0x80)			/* Note: always in signed byte range */
	jmp	common_interrupt
	.align	8
	vector=vector+1
    .endr
END(irq_entries_start)

	.align 8
ENTRY(spurious_entries_start)
    vector=FIRST_SYSTEM_VECTOR
    .rept (NR_VECTORS - FIRST_SYSTEM_VECTOR)
	UNWIND_HINT_IRET_REGS
	pushq	$(~vector+0x80)			/* Note: always in signed byte range */
	jmp	common_spurious
	.align	8
	vector=vector+1
    .endr
END(spurious_entries_start)

.macro DEBUG_ENTRY_ASSERT_IRQS_OFF
#ifdef CONFIG_DEBUG_ENTRY
	pushq %rax
	SAVE_FLAGS(CLBR_RAX)
	testl $X86_EFLAGS_IF, %eax
	jz .Lokay_\@
	ud2
.Lokay_\@:
	popq %rax
#endif
.endm

/*
 * Enters the IRQ stack if we're not already using it.  NMI-safe.  Clobbers
 * flags and puts old RSP into old_rsp, and leaves all other GPRs alone.
 * Requires kernel GSBASE.
 *
 * The invariant is that, if irq_count != -1, then the IRQ stack is in use.
 */
.macro ENTER_IRQ_STACK regs=1 old_rsp save_ret=0
	DEBUG_ENTRY_ASSERT_IRQS_OFF

	.if \save_ret
	/*
	 * If save_ret is set, the original stack contains one additional
	 * entry -- the return address. Therefore, move the address one
	 * entry below %rsp to \old_rsp.
	 */
	leaq	8(%rsp), \old_rsp
	.else
	movq	%rsp, \old_rsp
	.endif

	.if \regs
	UNWIND_HINT_REGS base=\old_rsp
	.endif

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

	movq	\old_rsp, PER_CPU_VAR(irq_stack_backing_store + IRQ_STACK_SIZE - 8)
	movq	PER_CPU_VAR(hardirq_stack_ptr), %rsp

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

	.if \regs
	UNWIND_HINT_REGS indirect=1
	.endif

	.if \save_ret
	/*
	 * Push the return address to the stack. This return address can
	 * be found at the "real" original RSP, which was offset by 8 at
	 * the beginning of this macro.
	 */
	pushq	-8(\old_rsp)
	.endif
.endm

/*
 * Undoes ENTER_IRQ_STACK.
 */
.macro LEAVE_IRQ_STACK regs=1
	DEBUG_ENTRY_ASSERT_IRQS_OFF
	/* We need to be off the IRQ stack before decrementing irq_count. */
	popq	%rsp

	.if \regs
	UNWIND_HINT_REGS
	.endif

	/*
	 * As in ENTER_IRQ_STACK, irq_count == 0, we are still claiming
	 * the irq stack but we're not on it.
	 */

	decl	PER_CPU_VAR(irq_count)
.endm

/*
 * Interrupt entry helper function.
 *
 * Entry runs with interrupts off. Stack layout at entry:
 * +----------------------------------------------------+
 * | regs->ss						|
 * | regs->rsp						|
 * | regs->eflags					|
 * | regs->cs						|
 * | regs->ip						|
 * +----------------------------------------------------+
 * | regs->orig_ax = ~(interrupt number)		|
 * +----------------------------------------------------+
 * | return address					|
 * +----------------------------------------------------+
 */
ENTRY(interrupt_entry)
	UNWIND_HINT_FUNC
	ASM_CLAC
	cld

	testb	$3, CS-ORIG_RAX+8(%rsp)
	jz	1f
	SWAPGS
	FENCE_SWAPGS_USER_ENTRY
	/*
	 * Switch to the thread stack. The IRET frame and orig_ax are
	 * on the stack, as well as the return address. RDI..R12 are
	 * not (yet) on the stack and space has not (yet) been
	 * allocated for them.
	 */
	pushq	%rdi

	/* Need to switch before accessing the thread stack. */
	SWITCH_TO_KERNEL_CR3 scratch_reg=%rdi
	movq	%rsp, %rdi
	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rsp

	 /*
	  * We have RDI, return address, and orig_ax on the stack on
	  * top of the IRET frame. That means offset=24
	  */
	UNWIND_HINT_IRET_REGS base=%rdi offset=24

	pushq	7*8(%rdi)		/* regs->ss */
	pushq	6*8(%rdi)		/* regs->rsp */
	pushq	5*8(%rdi)		/* regs->eflags */
	pushq	4*8(%rdi)		/* regs->cs */
	pushq	3*8(%rdi)		/* regs->ip */
	pushq	2*8(%rdi)		/* regs->orig_ax */
	pushq	8(%rdi)			/* return address */
	UNWIND_HINT_FUNC

	movq	(%rdi), %rdi
	jmp	2f
1:
	FENCE_SWAPGS_KERNEL_ENTRY
2:
	PUSH_AND_CLEAR_REGS save_ret=1
	ENCODE_FRAME_POINTER 8

	testb	$3, CS+8(%rsp)
	jz	1f

	/*
	 * IRQ from user mode.
	 *
	 * We need to tell lockdep that IRQs are off.  We can't do this until
	 * we fix gsbase, and we should do it before enter_from_user_mode
	 * (which can take locks).  Since TRACE_IRQS_OFF is idempotent,
	 * the simplest way to handle it is to just call it twice if
	 * we enter from user mode.  There's no reason to optimize this since
	 * TRACE_IRQS_OFF is a no-op if lockdep is off.
	 */
	TRACE_IRQS_OFF

	CALL_enter_from_user_mode

1:
	ENTER_IRQ_STACK old_rsp=%rdi save_ret=1
	/* We entered an interrupt context - irqs are off: */
	TRACE_IRQS_OFF

	ret
END(interrupt_entry)
_ASM_NOKPROBE(interrupt_entry)


/* Interrupt entry/exit. */

/*
 * The interrupt stubs push (~vector+0x80) onto the stack and
 * then jump to common_spurious/interrupt.
 */
common_spurious:
	addq	$-0x80, (%rsp)			/* Adjust vector to [-256, -1] range */
	call	interrupt_entry
	UNWIND_HINT_REGS indirect=1
	call	smp_spurious_interrupt		/* rdi points to pt_regs */
	jmp	ret_from_intr
END(common_spurious)
_ASM_NOKPROBE(common_spurious)

/* common_interrupt is a hotpath. Align it */
	.p2align CONFIG_X86_L1_CACHE_SHIFT
common_interrupt:
	addq	$-0x80, (%rsp)			/* Adjust vector to [-256, -1] range */
	call	interrupt_entry
	UNWIND_HINT_REGS indirect=1
	call	do_IRQ	/* rdi points to pt_regs */
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

GLOBAL(swapgs_restore_regs_and_return_to_usermode)
#ifdef CONFIG_DEBUG_ENTRY
	/* Assert that pt_regs indicates user mode. */
	testb	$3, CS(%rsp)
	jnz	1f
	ud2
1:
#endif
	POP_REGS pop_rdi=0

	/*
	 * The stack is now user RDI, orig_ax, RIP, CS, EFLAGS, RSP, SS.
	 * Save old stack pointer and switch to trampoline stack.
	 */
	movq	%rsp, %rdi
	movq	PER_CPU_VAR(cpu_tss_rw + TSS_sp0), %rsp

	/* Copy the IRET frame to the trampoline stack. */
	pushq	6*8(%rdi)	/* SS */
	pushq	5*8(%rdi)	/* RSP */
	pushq	4*8(%rdi)	/* EFLAGS */
	pushq	3*8(%rdi)	/* CS */
	pushq	2*8(%rdi)	/* RIP */

	/* Push user RDI on the trampoline stack. */
	pushq	(%rdi)

	/*
	 * We are on the trampoline stack.  All regs except RDI are live.
	 * We can do future final exit work right here.
	 */
	STACKLEAK_ERASE_NOCLOBBER

	SWITCH_TO_USER_CR3_STACK scratch_reg=%rdi

	/* Restore RDI. */
	popq	%rdi
	SWAPGS
	INTERRUPT_RETURN


/* Returning to kernel space */
retint_kernel:
#ifdef CONFIG_PREEMPTION
	/* Interrupts are off */
	/* Check if we need preemption */
	btl	$9, EFLAGS(%rsp)		/* were interrupts off? */
	jnc	1f
	cmpl	$0, PER_CPU_VAR(__preempt_count)
	jnz	1f
	call	preempt_schedule_irq
1:
#endif
	/*
	 * The iretq could re-enable interrupts:
	 */
	TRACE_IRQS_IRETQ

GLOBAL(restore_regs_and_return_to_kernel)
#ifdef CONFIG_DEBUG_ENTRY
	/* Assert that pt_regs indicates kernel mode. */
	testb	$3, CS(%rsp)
	jz	1f
	ud2
1:
#endif
	POP_REGS
	addq	$8, %rsp	/* skip regs->orig_ax */
	/*
	 * ARCH_HAS_MEMBARRIER_SYNC_CORE rely on IRET core serialization
	 * when returning from IPI handler.
	 */
	INTERRUPT_RETURN

ENTRY(native_iret)
	UNWIND_HINT_IRET_REGS
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
	SWAPGS					/* to kernel GS */
	SWITCH_TO_KERNEL_CR3 scratch_reg=%rdi	/* to kernel CR3 */

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

	/*
	 * espfix_stack[31:16] == 0.  The page tables are set up such that
	 * (espfix_stack | (X & 0xffff0000)) points to a read-only alias of
	 * espfix_waddr for any X.  That is, there are 65536 RO aliases of
	 * the same page.  Set up RSP so that RSP[31:16] contains the
	 * respective 16 bits of the /userspace/ RSP and RSP nonetheless
	 * still points to an RO alias of the ESPFIX stack.
	 */
	orq	PER_CPU_VAR(espfix_stack), %rax

	SWITCH_TO_USER_CR3_STACK scratch_reg=%rdi
	SWAPGS					/* to user GS */
	popq	%rdi				/* Restore user RDI */

	movq	%rax, %rsp
	UNWIND_HINT_IRET_REGS offset=8

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
_ASM_NOKPROBE(common_interrupt)

/*
 * APIC interrupts.
 */
.macro apicinterrupt3 num sym do_sym
ENTRY(\sym)
	UNWIND_HINT_IRET_REGS
	pushq	$~(\num)
.Lcommon_\sym:
	call	interrupt_entry
	UNWIND_HINT_REGS indirect=1
	call	\do_sym	/* rdi points to pt_regs */
	jmp	ret_from_intr
END(\sym)
_ASM_NOKPROBE(\sym)
.endm

/* Make sure APIC interrupt handlers end up in the irqentry section: */
#define PUSH_SECTION_IRQENTRY	.pushsection .irqentry.text, "ax"
#define POP_SECTION_IRQENTRY	.popsection

.macro apicinterrupt num sym do_sym
PUSH_SECTION_IRQENTRY
apicinterrupt3 \num \sym \do_sym
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
apicinterrupt3 POSTED_INTR_NESTED_VECTOR	kvm_posted_intr_nested_ipi	smp_kvm_posted_intr_nested_ipi
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
#define CPU_TSS_IST(x) PER_CPU_VAR(cpu_tss_rw) + (TSS_ist + (x) * 8)

.macro idtentry_part do_sym, has_error_code:req, read_cr2:req, paranoid:req, shift_ist=-1, ist_offset=0

	.if \paranoid
	call	paranoid_entry
	/* returned flag: ebx=0: need swapgs on exit, ebx=1: don't need it */
	.else
	call	error_entry
	.endif
	UNWIND_HINT_REGS

	.if \read_cr2
	/*
	 * Store CR2 early so subsequent faults cannot clobber it. Use R12 as
	 * intermediate storage as RDX can be clobbered in enter_from_user_mode().
	 * GET_CR2_INTO can clobber RAX.
	 */
	GET_CR2_INTO(%r12);
	.endif

	.if \shift_ist != -1
	TRACE_IRQS_OFF_DEBUG			/* reload IDT in case of recursion */
	.else
	TRACE_IRQS_OFF
	.endif

	.if \paranoid == 0
	testb	$3, CS(%rsp)
	jz	.Lfrom_kernel_no_context_tracking_\@
	CALL_enter_from_user_mode
.Lfrom_kernel_no_context_tracking_\@:
	.endif

	movq	%rsp, %rdi			/* pt_regs pointer */

	.if \has_error_code
	movq	ORIG_RAX(%rsp), %rsi		/* get error code */
	movq	$-1, ORIG_RAX(%rsp)		/* no syscall to restart */
	.else
	xorl	%esi, %esi			/* no error code */
	.endif

	.if \shift_ist != -1
	subq	$\ist_offset, CPU_TSS_IST(\shift_ist)
	.endif

	.if \read_cr2
	movq	%r12, %rdx			/* Move CR2 into 3rd argument */
	.endif

	call	\do_sym

	.if \shift_ist != -1
	addq	$\ist_offset, CPU_TSS_IST(\shift_ist)
	.endif

	.if \paranoid
	/* this procedure expect "no swapgs" flag in ebx */
	jmp	paranoid_exit
	.else
	jmp	error_exit
	.endif

.endm

/**
 * idtentry - Generate an IDT entry stub
 * @sym:		Name of the generated entry point
 * @do_sym:		C function to be called
 * @has_error_code:	True if this IDT vector has an error code on the stack
 * @paranoid:		non-zero means that this vector may be invoked from
 *			kernel mode with user GSBASE and/or user CR3.
 *			2 is special -- see below.
 * @shift_ist:		Set to an IST index if entries from kernel mode should
 *			decrement the IST stack so that nested entries get a
 *			fresh stack.  (This is for #DB, which has a nasty habit
 *			of recursing.)
 * @create_gap:		create a 6-word stack gap when coming from kernel mode.
 * @read_cr2:		load CR2 into the 3rd argument; done before calling any C code
 *
 * idtentry generates an IDT stub that sets up a usable kernel context,
 * creates struct pt_regs, and calls @do_sym.  The stub has the following
 * special behaviors:
 *
 * On an entry from user mode, the stub switches from the trampoline or
 * IST stack to the normal thread stack.  On an exit to user mode, the
 * normal exit-to-usermode path is invoked.
 *
 * On an exit to kernel mode, if @paranoid == 0, we check for preemption,
 * whereas we omit the preemption check if @paranoid != 0.  This is purely
 * because the implementation is simpler this way.  The kernel only needs
 * to check for asynchronous kernel preemption when IRQ handlers return.
 *
 * If @paranoid == 0, then the stub will handle IRET faults by pretending
 * that the fault came from user mode.  It will handle gs_change faults by
 * pretending that the fault happened with kernel GSBASE.  Since this handling
 * is omitted for @paranoid != 0, the #GP, #SS, and #NP stubs must have
 * @paranoid == 0.  This special handling will do the wrong thing for
 * espfix-induced #DF on IRET, so #DF must not use @paranoid == 0.
 *
 * @paranoid == 2 is special: the stub will never switch stacks.  This is for
 * #DF: if the thread stack is somehow unusable, we'll still get a useful OOPS.
 */
.macro idtentry sym do_sym has_error_code:req paranoid=0 shift_ist=-1 ist_offset=0 create_gap=0 read_cr2=0
ENTRY(\sym)
	UNWIND_HINT_IRET_REGS offset=\has_error_code*8

	/* Sanity check */
	.if \shift_ist != -1 && \paranoid != 1
	.error "using shift_ist requires paranoid=1"
	.endif

	.if \create_gap && \paranoid
	.error "using create_gap requires paranoid=0"
	.endif

	ASM_CLAC

	.if \has_error_code == 0
	pushq	$-1				/* ORIG_RAX: no syscall to restart */
	.endif

	.if \paranoid == 1
	testb	$3, CS-ORIG_RAX(%rsp)		/* If coming from userspace, switch stacks */
	jnz	.Lfrom_usermode_switch_stack_\@
	.endif

	.if \create_gap == 1
	/*
	 * If coming from kernel space, create a 6-word gap to allow the
	 * int3 handler to emulate a call instruction.
	 */
	testb	$3, CS-ORIG_RAX(%rsp)
	jnz	.Lfrom_usermode_no_gap_\@
	.rept	6
	pushq	5*8(%rsp)
	.endr
	UNWIND_HINT_IRET_REGS offset=8
.Lfrom_usermode_no_gap_\@:
	.endif

	idtentry_part \do_sym, \has_error_code, \read_cr2, \paranoid, \shift_ist, \ist_offset

	.if \paranoid == 1
	/*
	 * Entry from userspace.  Switch stacks and treat it
	 * as a normal entry.  This means that paranoid handlers
	 * run in real process context if user_mode(regs).
	 */
.Lfrom_usermode_switch_stack_\@:
	idtentry_part \do_sym, \has_error_code, \read_cr2, paranoid=0
	.endif

_ASM_NOKPROBE(\sym)
END(\sym)
.endm

idtentry divide_error			do_divide_error			has_error_code=0
idtentry overflow			do_overflow			has_error_code=0
idtentry bounds				do_bounds			has_error_code=0
idtentry invalid_op			do_invalid_op			has_error_code=0
idtentry device_not_available		do_device_not_available		has_error_code=0
idtentry double_fault			do_double_fault			has_error_code=1 paranoid=2 read_cr2=1
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
	FRAME_BEGIN
	pushfq
	DISABLE_INTERRUPTS(CLBR_ANY & ~CLBR_RDI)
	TRACE_IRQS_OFF
	SWAPGS
.Lgs_change:
	movl	%edi, %gs
2:	ALTERNATIVE "", "mfence", X86_BUG_SWAPGS_FENCE
	SWAPGS
	TRACE_IRQS_FLAGS (%rsp)
	popfq
	FRAME_END
	ret
ENDPROC(native_load_gs_index)
EXPORT_SYMBOL(native_load_gs_index)

	_ASM_EXTABLE(.Lgs_change, .Lbad_gs)
	.section .fixup, "ax"
	/* running with kernelgs */
.Lbad_gs:
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
	ENTER_IRQ_STACK regs=0 old_rsp=%r11
	call	__do_softirq
	LEAVE_IRQ_STACK regs=0
	leaveq
	ret
ENDPROC(do_softirq_own_stack)

#ifdef CONFIG_XEN_PV
idtentry hypervisor_callback xen_do_hypervisor_callback has_error_code=0

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
	UNWIND_HINT_FUNC
	movq	%rdi, %rsp			/* we don't return, adjust the stack frame */
	UNWIND_HINT_REGS

	ENTER_IRQ_STACK old_rsp=%r10
	call	xen_evtchn_do_upcall
	LEAVE_IRQ_STACK

#ifndef CONFIG_PREEMPTION
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
	UNWIND_HINT_EMPTY
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
	UNWIND_HINT_IRET_REGS offset=8
	jmp	general_protection
1:	/* Segment mismatch => Category 1 (Bad segment). Retry the IRET. */
	movq	(%rsp), %rcx
	movq	8(%rsp), %r11
	addq	$0x30, %rsp
	UNWIND_HINT_IRET_REGS
	pushq	$-1 /* orig_ax = -1 => not a system call */
	PUSH_AND_CLEAR_REGS
	ENCODE_FRAME_POINTER
	jmp	error_exit
END(xen_failsafe_callback)
#endif /* CONFIG_XEN_PV */

#ifdef CONFIG_XEN_PVHVM
apicinterrupt3 HYPERVISOR_CALLBACK_VECTOR \
	xen_hvm_callback_vector xen_evtchn_do_upcall
#endif


#if IS_ENABLED(CONFIG_HYPERV)
apicinterrupt3 HYPERVISOR_CALLBACK_VECTOR \
	hyperv_callback_vector hyperv_vector_handler

apicinterrupt3 HYPERV_REENLIGHTENMENT_VECTOR \
	hyperv_reenlightenment_vector hyperv_reenlightenment_intr

apicinterrupt3 HYPERV_STIMER0_VECTOR \
	hv_stimer0_callback_vector hv_stimer0_vector_handler
#endif /* CONFIG_HYPERV */

#if IS_ENABLED(CONFIG_ACRN_GUEST)
apicinterrupt3 HYPERVISOR_CALLBACK_VECTOR \
	acrn_hv_callback_vector acrn_hv_vector_handler
#endif

idtentry debug			do_debug		has_error_code=0	paranoid=1 shift_ist=IST_INDEX_DB ist_offset=DB_STACK_OFFSET
idtentry int3			do_int3			has_error_code=0	create_gap=1
idtentry stack_segment		do_stack_segment	has_error_code=1

#ifdef CONFIG_XEN_PV
idtentry xennmi			do_nmi			has_error_code=0
idtentry xendebug		do_debug		has_error_code=0
#endif

idtentry general_protection	do_general_protection	has_error_code=1
idtentry page_fault		do_page_fault		has_error_code=1	read_cr2=1

#ifdef CONFIG_KVM_GUEST
idtentry async_page_fault	do_async_page_fault	has_error_code=1	read_cr2=1
#endif

#ifdef CONFIG_X86_MCE
idtentry machine_check		do_mce			has_error_code=0	paranoid=1
#endif

/*
 * Save all registers in pt_regs, and switch gs if needed.
 * Use slow, but surefire "are we in kernel?" check.
 * Return: ebx=0: need swapgs on exit, ebx=1: otherwise
 */
ENTRY(paranoid_entry)
	UNWIND_HINT_FUNC
	cld
	PUSH_AND_CLEAR_REGS save_ret=1
	ENCODE_FRAME_POINTER 8
	movl	$1, %ebx
	movl	$MSR_GS_BASE, %ecx
	rdmsr
	testl	%edx, %edx
	js	1f				/* negative -> in kernel */
	SWAPGS
	xorl	%ebx, %ebx

1:
	/*
	 * Always stash CR3 in %r14.  This value will be restored,
	 * verbatim, at exit.  Needed if paranoid_entry interrupted
	 * another entry that already switched to the user CR3 value
	 * but has not yet returned to userspace.
	 *
	 * This is also why CS (stashed in the "iret frame" by the
	 * hardware at entry) can not be used: this may be a return
	 * to kernel code, but with a user CR3 value.
	 */
	SAVE_AND_SWITCH_TO_KERNEL_CR3 scratch_reg=%rax save_reg=%r14

	/*
	 * The above SAVE_AND_SWITCH_TO_KERNEL_CR3 macro doesn't do an
	 * unconditional CR3 write, even in the PTI case.  So do an lfence
	 * to prevent GS speculation, regardless of whether PTI is enabled.
	 */
	FENCE_SWAPGS_KERNEL_ENTRY

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
 * On entry, ebx is "no swapgs" flag (1: don't need swapgs, 0: need it)
 */
ENTRY(paranoid_exit)
	UNWIND_HINT_REGS
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF_DEBUG
	testl	%ebx, %ebx			/* swapgs needed? */
	jnz	.Lparanoid_exit_no_swapgs
	TRACE_IRQS_IRETQ
	/* Always restore stashed CR3 value (see paranoid_entry) */
	RESTORE_CR3	scratch_reg=%rbx save_reg=%r14
	SWAPGS_UNSAFE_STACK
	jmp	.Lparanoid_exit_restore
.Lparanoid_exit_no_swapgs:
	TRACE_IRQS_IRETQ_DEBUG
	/* Always restore stashed CR3 value (see paranoid_entry) */
	RESTORE_CR3	scratch_reg=%rbx save_reg=%r14
.Lparanoid_exit_restore:
	jmp restore_regs_and_return_to_kernel
END(paranoid_exit)

/*
 * Save all registers in pt_regs, and switch GS if needed.
 */
ENTRY(error_entry)
	UNWIND_HINT_FUNC
	cld
	PUSH_AND_CLEAR_REGS save_ret=1
	ENCODE_FRAME_POINTER 8
	testb	$3, CS+8(%rsp)
	jz	.Lerror_kernelspace

	/*
	 * We entered from user mode or we're pretending to have entered
	 * from user mode due to an IRET fault.
	 */
	SWAPGS
	FENCE_SWAPGS_USER_ENTRY
	/* We have user CR3.  Change to kernel CR3. */
	SWITCH_TO_KERNEL_CR3 scratch_reg=%rax

.Lerror_entry_from_usermode_after_swapgs:
	/* Put us onto the real thread stack. */
	popq	%r12				/* save return addr in %12 */
	movq	%rsp, %rdi			/* arg0 = pt_regs pointer */
	call	sync_regs
	movq	%rax, %rsp			/* switch stack */
	ENCODE_FRAME_POINTER
	pushq	%r12
	ret

.Lerror_entry_done_lfence:
	FENCE_SWAPGS_KERNEL_ENTRY
.Lerror_entry_done:
	ret

	/*
	 * There are two places in the kernel that can potentially fault with
	 * usergs. Handle them here.  B stepping K8s sometimes report a
	 * truncated RIP for IRET exceptions returning to compat mode. Check
	 * for these here too.
	 */
.Lerror_kernelspace:
	leaq	native_irq_return_iret(%rip), %rcx
	cmpq	%rcx, RIP+8(%rsp)
	je	.Lerror_bad_iret
	movl	%ecx, %eax			/* zero extend */
	cmpq	%rax, RIP+8(%rsp)
	je	.Lbstep_iret
	cmpq	$.Lgs_change, RIP+8(%rsp)
	jne	.Lerror_entry_done_lfence

	/*
	 * hack: .Lgs_change can fail with user gsbase.  If this happens, fix up
	 * gsbase and proceed.  We'll fix up the exception and land in
	 * .Lgs_change's error handler with kernel gsbase.
	 */
	SWAPGS
	FENCE_SWAPGS_USER_ENTRY
	SWITCH_TO_KERNEL_CR3 scratch_reg=%rax
	jmp .Lerror_entry_done

.Lbstep_iret:
	/* Fix truncated RIP */
	movq	%rcx, RIP+8(%rsp)
	/* fall through */

.Lerror_bad_iret:
	/*
	 * We came from an IRET to user mode, so we have user
	 * gsbase and CR3.  Switch to kernel gsbase and CR3:
	 */
	SWAPGS
	FENCE_SWAPGS_USER_ENTRY
	SWITCH_TO_KERNEL_CR3 scratch_reg=%rax

	/*
	 * Pretend that the exception came from user mode: set up pt_regs
	 * as if we faulted immediately after IRET.
	 */
	mov	%rsp, %rdi
	call	fixup_bad_iret
	mov	%rax, %rsp
	jmp	.Lerror_entry_from_usermode_after_swapgs
END(error_entry)

ENTRY(error_exit)
	UNWIND_HINT_REGS
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF
	testb	$3, CS(%rsp)
	jz	retint_kernel
	jmp	retint_user
END(error_exit)

/*
 * Runs on exception stack.  Xen PV does not go through this path at all,
 * so we can use real assembly here.
 *
 * Registers:
 *	%r14: Used to save/restore the CR3 of the interrupted context
 *	      when PAGE_TABLE_ISOLATION is in use.  Do not clobber.
 */
ENTRY(nmi)
	UNWIND_HINT_IRET_REGS

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

	swapgs
	cld
	FENCE_SWAPGS_USER_ENTRY
	SWITCH_TO_KERNEL_CR3 scratch_reg=%rdx
	movq	%rsp, %rdx
	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rsp
	UNWIND_HINT_IRET_REGS base=%rdx offset=8
	pushq	5*8(%rdx)	/* pt_regs->ss */
	pushq	4*8(%rdx)	/* pt_regs->rsp */
	pushq	3*8(%rdx)	/* pt_regs->flags */
	pushq	2*8(%rdx)	/* pt_regs->cs */
	pushq	1*8(%rdx)	/* pt_regs->rip */
	UNWIND_HINT_IRET_REGS
	pushq   $-1		/* pt_regs->orig_ax */
	PUSH_AND_CLEAR_REGS rdx=(%rdx)
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
	jmp	swapgs_restore_regs_and_return_to_usermode

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
	iretq

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
	UNWIND_HINT_IRET_REGS

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
	iretq			/* continues at repeat_nmi below */
	UNWIND_HINT_IRET_REGS
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

	/*
	 * Use paranoid_entry to handle SWAPGS, but no need to use paranoid_exit
	 * as we should not be calling schedule in NMI context.
	 * Even with normal interrupts enabled. An NMI should not be
	 * setting NEED_RESCHED or anything that normal interrupts and
	 * exceptions might do.
	 */
	call	paranoid_entry
	UNWIND_HINT_REGS

	/* paranoidentry do_nmi, 0; without TRACE_IRQS_OFF */
	movq	%rsp, %rdi
	movq	$-1, %rsi
	call	do_nmi

	/* Always restore stashed CR3 value (see paranoid_entry) */
	RESTORE_CR3 scratch_reg=%r15 save_reg=%r14

	testl	%ebx, %ebx			/* swapgs needed? */
	jnz	nmi_restore
nmi_swapgs:
	SWAPGS_UNSAFE_STACK
nmi_restore:
	POP_REGS

	/*
	 * Skip orig_ax and the "outermost" frame to point RSP at the "iret"
	 * at the "iret" frame.
	 */
	addq	$6*8, %rsp

	/*
	 * Clear "NMI executing".  Set DF first so that we can easily
	 * distinguish the remaining code between here and IRET from
	 * the SYSCALL entry and exit paths.
	 *
	 * We arguably should just inspect RIP instead, but I (Andy) wrote
	 * this code when I had the misapprehension that Xen PV supported
	 * NMIs, and Xen PV would break that approach.
	 */
	std
	movq	$0, 5*8(%rsp)		/* clear "NMI executing" */

	/*
	 * iretq reads the "iret" frame and exits the NMI stack in a
	 * single instruction.  We are returning to kernel mode, so this
	 * cannot result in a fault.  Similarly, we don't need to worry
	 * about espfix64 on the way back to kernel mode.
	 */
	iretq
END(nmi)

#ifndef CONFIG_IA32_EMULATION
/*
 * This handles SYSCALL from 32-bit code.  There is no way to program
 * MSRs to fully disable 32-bit SYSCALL.
 */
ENTRY(ignore_sysret)
	UNWIND_HINT_EMPTY
	mov	$-ENOSYS, %eax
	sysret
END(ignore_sysret)
#endif

ENTRY(rewind_stack_do_exit)
	UNWIND_HINT_FUNC
	/* Prevent any naive code from trying to unwind to our caller. */
	xorl	%ebp, %ebp

	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rax
	leaq	-PTREGS_SIZE(%rax), %rsp
	UNWIND_HINT_FUNC sp_offset=PTREGS_SIZE

	call	do_exit
END(rewind_stack_do_exit)
