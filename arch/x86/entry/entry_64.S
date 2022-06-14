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
 * - SYM_FUNC_START/END:Define functions in the symbol table.
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
#include <asm/trapnr.h>
#include <asm/nospec-branch.h>
#include <asm/fsgsbase.h>
#include <linux/err.h>

#include "calling.h"

.code64
.section .entry.text, "ax"

#ifdef CONFIG_PARAVIRT_XXL
SYM_CODE_START(native_usergs_sysret64)
	UNWIND_HINT_EMPTY
	swapgs
	sysretq
SYM_CODE_END(native_usergs_sysret64)
#endif /* CONFIG_PARAVIRT_XXL */

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

SYM_CODE_START(entry_SYSCALL_64)
	UNWIND_HINT_ENTRY

	swapgs
	/* tss.sp2 is scratch space. */
	movq	%rsp, PER_CPU_VAR(cpu_tss_rw + TSS_sp2)
	SWITCH_TO_KERNEL_CR3 scratch_reg=%rsp
	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rsp

SYM_INNER_LABEL(entry_SYSCALL_64_safe_stack, SYM_L_GLOBAL)

	/* Construct struct pt_regs on stack */
	pushq	$__USER_DS				/* pt_regs->ss */
	pushq	PER_CPU_VAR(cpu_tss_rw + TSS_sp2)	/* pt_regs->sp */
	pushq	%r11					/* pt_regs->flags */
	pushq	$__USER_CS				/* pt_regs->cs */
	pushq	%rcx					/* pt_regs->ip */
SYM_INNER_LABEL(entry_SYSCALL_64_after_hwframe, SYM_L_GLOBAL)
	pushq	%rax					/* pt_regs->orig_ax */

	PUSH_AND_CLEAR_REGS rax=$-ENOSYS

	/* IRQs are off. */
	movq	%rax, %rdi
	movq	%rsp, %rsi

	/* clobbers %rax, make sure it is after saving the syscall nr */
	IBRS_ENTER
	UNTRAIN_RET

	call	do_syscall_64		/* returns with IRQs disabled */

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
	IBRS_EXIT
	POP_REGS pop_rdi=0

	/*
	 * Now all regs are restored except RSP and RDI.
	 * Save old stack pointer and switch to trampoline stack.
	 */
	movq	%rsp, %rdi
	movq	PER_CPU_VAR(cpu_tss_rw + TSS_sp0), %rsp
	UNWIND_HINT_EMPTY

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
SYM_CODE_END(entry_SYSCALL_64)

/*
 * %rdi: prev task
 * %rsi: next task
 */
.pushsection .text, "ax"
SYM_FUNC_START(__switch_to_asm)
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

	/*
	 * When switching from a shallower to a deeper call stack
	 * the RSB may either underflow or use entries populated
	 * with userspace addresses. On CPUs where those concerns
	 * exist, overwrite the RSB with entries which capture
	 * speculative execution to prevent attack.
	 */
	FILL_RETURN_BUFFER %r12, RSB_CLEAR_LOOPS, X86_FEATURE_RSB_CTXSW

	/* restore callee-saved registers */
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp

	jmp	__switch_to
SYM_FUNC_END(__switch_to_asm)
.popsection

/*
 * A newly forked process directly context switches into this address.
 *
 * rax: prev task we switched from
 * rbx: kernel thread func (NULL for user thread)
 * r12: kernel thread arg
 */
.pushsection .text, "ax"
SYM_CODE_START(ret_from_fork)
	UNWIND_HINT_EMPTY
	movq	%rax, %rdi
	call	schedule_tail			/* rdi: 'prev' task parameter */

	testq	%rbx, %rbx			/* from kernel_thread? */
	jnz	1f				/* kernel threads are uncommon */

2:
	UNWIND_HINT_REGS
	movq	%rsp, %rdi
	call	syscall_exit_to_user_mode	/* returns with IRQs disabled */
	jmp	swapgs_restore_regs_and_return_to_usermode

1:
	/* kernel thread */
	UNWIND_HINT_EMPTY
	movq	%r12, %rdi
	CALL_NOSPEC rbx
	/*
	 * A kernel thread is allowed to return here after successfully
	 * calling kernel_execve().  Exit to userspace to complete the execve()
	 * syscall.
	 */
	movq	$0, RAX(%rsp)
	jmp	2b
SYM_CODE_END(ret_from_fork)
.popsection

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

/**
 * idtentry_body - Macro to emit code calling the C function
 * @cfunc:		C function to be called
 * @has_error_code:	Hardware pushed error code on stack
 */
.macro idtentry_body cfunc has_error_code:req

	call	error_entry
	UNWIND_HINT_REGS

	movq	%rsp, %rdi			/* pt_regs pointer into 1st argument*/

	.if \has_error_code == 1
		movq	ORIG_RAX(%rsp), %rsi	/* get error code into 2nd argument*/
		movq	$-1, ORIG_RAX(%rsp)	/* no syscall to restart */
	.endif

	call	\cfunc

	jmp	error_return
.endm

/**
 * idtentry - Macro to generate entry stubs for simple IDT entries
 * @vector:		Vector number
 * @asmsym:		ASM symbol for the entry point
 * @cfunc:		C function to be called
 * @has_error_code:	Hardware pushed error code on stack
 *
 * The macro emits code to set up the kernel context for straight forward
 * and simple IDT entries. No IST stack, no paranoid entry checks.
 */
.macro idtentry vector asmsym cfunc has_error_code:req
SYM_CODE_START(\asmsym)
	UNWIND_HINT_IRET_REGS offset=\has_error_code*8
	ASM_CLAC

	.if \has_error_code == 0
		pushq	$-1			/* ORIG_RAX: no syscall to restart */
	.endif

	.if \vector == X86_TRAP_BP
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

	idtentry_body \cfunc \has_error_code

_ASM_NOKPROBE(\asmsym)
SYM_CODE_END(\asmsym)
.endm

/*
 * Interrupt entry/exit.
 *
 + The interrupt stubs push (vector) onto the stack, which is the error_code
 * position of idtentry exceptions, and jump to one of the two idtentry points
 * (common/spurious).
 *
 * common_interrupt is a hotpath, align it to a cache line
 */
.macro idtentry_irq vector cfunc
	.p2align CONFIG_X86_L1_CACHE_SHIFT
	idtentry \vector asm_\cfunc \cfunc has_error_code=1
.endm

/*
 * System vectors which invoke their handlers directly and are not
 * going through the regular common device interrupt handling code.
 */
.macro idtentry_sysvec vector cfunc
	idtentry \vector asm_\cfunc \cfunc has_error_code=0
.endm

/**
 * idtentry_mce_db - Macro to generate entry stubs for #MC and #DB
 * @vector:		Vector number
 * @asmsym:		ASM symbol for the entry point
 * @cfunc:		C function to be called
 *
 * The macro emits code to set up the kernel context for #MC and #DB
 *
 * If the entry comes from user space it uses the normal entry path
 * including the return to user space work and preemption checks on
 * exit.
 *
 * If hits in kernel mode then it needs to go through the paranoid
 * entry as the exception can hit any random state. No preemption
 * check on exit to keep the paranoid path simple.
 */
.macro idtentry_mce_db vector asmsym cfunc
SYM_CODE_START(\asmsym)
	UNWIND_HINT_IRET_REGS
	ASM_CLAC

	pushq	$-1			/* ORIG_RAX: no syscall to restart */

	/*
	 * If the entry is from userspace, switch stacks and treat it as
	 * a normal entry.
	 */
	testb	$3, CS-ORIG_RAX(%rsp)
	jnz	.Lfrom_usermode_switch_stack_\@

	/* paranoid_entry returns GS information for paranoid_exit in EBX. */
	call	paranoid_entry

	UNWIND_HINT_REGS

	movq	%rsp, %rdi		/* pt_regs pointer */

	call	\cfunc

	jmp	paranoid_exit

	/* Switch to the regular task stack and use the noist entry point */
.Lfrom_usermode_switch_stack_\@:
	idtentry_body noist_\cfunc, has_error_code=0

_ASM_NOKPROBE(\asmsym)
SYM_CODE_END(\asmsym)
.endm

#ifdef CONFIG_AMD_MEM_ENCRYPT
/**
 * idtentry_vc - Macro to generate entry stub for #VC
 * @vector:		Vector number
 * @asmsym:		ASM symbol for the entry point
 * @cfunc:		C function to be called
 *
 * The macro emits code to set up the kernel context for #VC. The #VC handler
 * runs on an IST stack and needs to be able to cause nested #VC exceptions.
 *
 * To make this work the #VC entry code tries its best to pretend it doesn't use
 * an IST stack by switching to the task stack if coming from user-space (which
 * includes early SYSCALL entry path) or back to the stack in the IRET frame if
 * entered from kernel-mode.
 *
 * If entered from kernel-mode the return stack is validated first, and if it is
 * not safe to use (e.g. because it points to the entry stack) the #VC handler
 * will switch to a fall-back stack (VC2) and call a special handler function.
 *
 * The macro is only used for one vector, but it is planned to be extended in
 * the future for the #HV exception.
 */
.macro idtentry_vc vector asmsym cfunc
SYM_CODE_START(\asmsym)
	UNWIND_HINT_IRET_REGS
	ASM_CLAC

	/*
	 * If the entry is from userspace, switch stacks and treat it as
	 * a normal entry.
	 */
	testb	$3, CS-ORIG_RAX(%rsp)
	jnz	.Lfrom_usermode_switch_stack_\@

	/*
	 * paranoid_entry returns SWAPGS flag for paranoid_exit in EBX.
	 * EBX == 0 -> SWAPGS, EBX == 1 -> no SWAPGS
	 */
	call	paranoid_entry

	UNWIND_HINT_REGS

	/*
	 * Switch off the IST stack to make it free for nested exceptions. The
	 * vc_switch_off_ist() function will switch back to the interrupted
	 * stack if it is safe to do so. If not it switches to the VC fall-back
	 * stack.
	 */
	movq	%rsp, %rdi		/* pt_regs pointer */
	call	vc_switch_off_ist
	movq	%rax, %rsp		/* Switch to new stack */

	ENCODE_FRAME_POINTER
	UNWIND_HINT_REGS

	/* Update pt_regs */
	movq	ORIG_RAX(%rsp), %rsi	/* get error code into 2nd argument*/
	movq	$-1, ORIG_RAX(%rsp)	/* no syscall to restart */

	movq	%rsp, %rdi		/* pt_regs pointer */

	call	kernel_\cfunc

	/*
	 * No need to switch back to the IST stack. The current stack is either
	 * identical to the stack in the IRET frame or the VC fall-back stack,
	 * so it is definitly mapped even with PTI enabled.
	 */
	jmp	paranoid_exit

	/* Switch to the regular task stack */
.Lfrom_usermode_switch_stack_\@:
	idtentry_body user_\cfunc, has_error_code=1

_ASM_NOKPROBE(\asmsym)
SYM_CODE_END(\asmsym)
.endm
#endif

/*
 * Double fault entry. Straight paranoid. No checks from which context
 * this comes because for the espfix induced #DF this would do the wrong
 * thing.
 */
.macro idtentry_df vector asmsym cfunc
SYM_CODE_START(\asmsym)
	UNWIND_HINT_IRET_REGS offset=8
	ASM_CLAC

	/* paranoid_entry returns GS information for paranoid_exit in EBX. */
	call	paranoid_entry
	UNWIND_HINT_REGS

	movq	%rsp, %rdi		/* pt_regs pointer into first argument */
	movq	ORIG_RAX(%rsp), %rsi	/* get error code into 2nd argument*/
	movq	$-1, ORIG_RAX(%rsp)	/* no syscall to restart */
	call	\cfunc

	jmp	paranoid_exit

_ASM_NOKPROBE(\asmsym)
SYM_CODE_END(\asmsym)
.endm

/*
 * Include the defines which emit the idt entries which are shared
 * shared between 32 and 64 bit and emit the __irqentry_text_* markers
 * so the stacktrace boundary checks work.
 */
	.align 16
	.globl __irqentry_text_start
__irqentry_text_start:

#include <asm/idtentry.h>

	.align 16
	.globl __irqentry_text_end
__irqentry_text_end:

SYM_CODE_START_LOCAL(common_interrupt_return)
SYM_INNER_LABEL(swapgs_restore_regs_and_return_to_usermode, SYM_L_GLOBAL)
	IBRS_EXIT
#ifdef CONFIG_DEBUG_ENTRY
	/* Assert that pt_regs indicates user mode. */
	testb	$3, CS(%rsp)
	jnz	1f
	ud2
1:
#endif
#ifdef CONFIG_XEN_PV
	ALTERNATIVE "", "jmp xenpv_restore_regs_and_return_to_usermode", X86_FEATURE_XENPV
#endif

	POP_REGS pop_rdi=0

	/*
	 * The stack is now user RDI, orig_ax, RIP, CS, EFLAGS, RSP, SS.
	 * Save old stack pointer and switch to trampoline stack.
	 */
	movq	%rsp, %rdi
	movq	PER_CPU_VAR(cpu_tss_rw + TSS_sp0), %rsp
	UNWIND_HINT_EMPTY

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


SYM_INNER_LABEL(restore_regs_and_return_to_kernel, SYM_L_GLOBAL)
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

SYM_INNER_LABEL_ALIGN(native_iret, SYM_L_GLOBAL)
	UNWIND_HINT_IRET_REGS
	/*
	 * Are we returning to a stack segment from the LDT?  Note: in
	 * 64-bit mode SS:RSP on the exception stack is always valid.
	 */
#ifdef CONFIG_X86_ESPFIX64
	testb	$4, (SS-RIP)(%rsp)
	jnz	native_irq_return_ldt
#endif

SYM_INNER_LABEL(native_irq_return_iret, SYM_L_GLOBAL)
	/*
	 * This may fault.  Non-paranoid faults on return to userspace are
	 * handled by fixup_bad_iret.  These include #SS, #GP, and #NP.
	 * Double-faults due to espfix64 are handled in exc_double_fault.
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
	swapgs					/* to kernel GS */
	SWITCH_TO_KERNEL_CR3 scratch_reg=%rdi	/* to kernel CR3 */
	UNTRAIN_RET

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
	swapgs					/* to user GS */
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
SYM_CODE_END(common_interrupt_return)
_ASM_NOKPROBE(common_interrupt_return)

/*
 * Reload gs selector with exception handling
 * edi:  new selector
 *
 * Is in entry.text as it shouldn't be instrumented.
 */
SYM_FUNC_START(asm_load_gs_index)
	FRAME_BEGIN
	swapgs
.Lgs_change:
	movl	%edi, %gs
2:	ALTERNATIVE "", "mfence", X86_BUG_SWAPGS_FENCE
	swapgs
	FRAME_END
	RET
SYM_FUNC_END(asm_load_gs_index)
EXPORT_SYMBOL(asm_load_gs_index)

	_ASM_EXTABLE(.Lgs_change, .Lbad_gs)
	.section .fixup, "ax"
	/* running with kernelgs */
SYM_CODE_START_LOCAL_NOALIGN(.Lbad_gs)
	swapgs					/* switch back to user gs */
.macro ZAP_GS
	/* This can't be a string because the preprocessor needs to see it. */
	movl $__USER_DS, %eax
	movl %eax, %gs
.endm
	ALTERNATIVE "", "ZAP_GS", X86_BUG_NULL_SEG
	xorl	%eax, %eax
	movl	%eax, %gs
	jmp	2b
SYM_CODE_END(.Lbad_gs)
	.previous

/*
 * rdi: New stack pointer points to the top word of the stack
 * rsi: Function pointer
 * rdx: Function argument (can be NULL if none)
 */
SYM_FUNC_START(asm_call_on_stack)
SYM_INNER_LABEL(asm_call_sysvec_on_stack, SYM_L_GLOBAL)
SYM_INNER_LABEL(asm_call_irq_on_stack, SYM_L_GLOBAL)
	/*
	 * Save the frame pointer unconditionally. This allows the ORC
	 * unwinder to handle the stack switch.
	 */
	pushq		%rbp
	mov		%rsp, %rbp

	/*
	 * The unwinder relies on the word at the top of the new stack
	 * page linking back to the previous RSP.
	 */
	mov		%rsp, (%rdi)
	mov		%rdi, %rsp
	/* Move the argument to the right place */
	mov		%rdx, %rdi

1:
	.pushsection .discard.instr_begin
	.long 1b - .
	.popsection

	CALL_NOSPEC	rsi

2:
	.pushsection .discard.instr_end
	.long 2b - .
	.popsection

	/* Restore the previous stack pointer from RBP. */
	leaveq
	RET
SYM_FUNC_END(asm_call_on_stack)

#ifdef CONFIG_XEN_PV
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
 *
 * C calling convention: exc_xen_hypervisor_callback(struct *pt_regs)
 */
SYM_CODE_START_LOCAL(exc_xen_hypervisor_callback)

/*
 * Since we don't modify %rdi, evtchn_do_upall(struct *pt_regs) will
 * see the correct pointer to the pt_regs
 */
	UNWIND_HINT_FUNC
	movq	%rdi, %rsp			/* we don't return, adjust the stack frame */
	UNWIND_HINT_REGS

	call	xen_pv_evtchn_do_upcall

	jmp	error_return
SYM_CODE_END(exc_xen_hypervisor_callback)

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
SYM_CODE_START(xen_failsafe_callback)
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
	jmp	asm_exc_general_protection
1:	/* Segment mismatch => Category 1 (Bad segment). Retry the IRET. */
	movq	(%rsp), %rcx
	movq	8(%rsp), %r11
	addq	$0x30, %rsp
	UNWIND_HINT_IRET_REGS
	pushq	$-1 /* orig_ax = -1 => not a system call */
	PUSH_AND_CLEAR_REGS
	ENCODE_FRAME_POINTER
	jmp	error_return
SYM_CODE_END(xen_failsafe_callback)
#endif /* CONFIG_XEN_PV */

/*
 * Save all registers in pt_regs. Return GSBASE related information
 * in EBX depending on the availability of the FSGSBASE instructions:
 *
 * FSGSBASE	R/EBX
 *     N        0 -> SWAPGS on exit
 *              1 -> no SWAPGS on exit
 *
 *     Y        GSBASE value at entry, must be restored in paranoid_exit
 *
 * R14 - old CR3
 * R15 - old SPEC_CTRL
 */
SYM_CODE_START_LOCAL(paranoid_entry)
	UNWIND_HINT_FUNC
	cld
	PUSH_AND_CLEAR_REGS save_ret=1
	ENCODE_FRAME_POINTER 8

	/*
	 * Always stash CR3 in %r14.  This value will be restored,
	 * verbatim, at exit.  Needed if paranoid_entry interrupted
	 * another entry that already switched to the user CR3 value
	 * but has not yet returned to userspace.
	 *
	 * This is also why CS (stashed in the "iret frame" by the
	 * hardware at entry) can not be used: this may be a return
	 * to kernel code, but with a user CR3 value.
	 *
	 * Switching CR3 does not depend on kernel GSBASE so it can
	 * be done before switching to the kernel GSBASE. This is
	 * required for FSGSBASE because the kernel GSBASE has to
	 * be retrieved from a kernel internal table.
	 */
	SAVE_AND_SWITCH_TO_KERNEL_CR3 scratch_reg=%rax save_reg=%r14

	/*
	 * Handling GSBASE depends on the availability of FSGSBASE.
	 *
	 * Without FSGSBASE the kernel enforces that negative GSBASE
	 * values indicate kernel GSBASE. With FSGSBASE no assumptions
	 * can be made about the GSBASE value when entering from user
	 * space.
	 */
	ALTERNATIVE "jmp .Lparanoid_entry_checkgs", "", X86_FEATURE_FSGSBASE

	/*
	 * Read the current GSBASE and store it in %rbx unconditionally,
	 * retrieve and set the current CPUs kernel GSBASE. The stored value
	 * has to be restored in paranoid_exit unconditionally.
	 *
	 * The unconditional write to GS base below ensures that no subsequent
	 * loads based on a mispredicted GS base can happen, therefore no LFENCE
	 * is needed here.
	 */
	SAVE_AND_SET_GSBASE scratch_reg=%rax save_reg=%rbx
	jmp .Lparanoid_gsbase_done

.Lparanoid_entry_checkgs:
	/* EBX = 1 -> kernel GSBASE active, no restore required */
	movl	$1, %ebx

	/*
	 * The kernel-enforced convention is a negative GSBASE indicates
	 * a kernel value. No SWAPGS needed on entry and exit.
	 */
	movl	$MSR_GS_BASE, %ecx
	rdmsr
	testl	%edx, %edx
	js	.Lparanoid_kernel_gsbase

	/* EBX = 0 -> SWAPGS required on exit */
	xorl	%ebx, %ebx
	swapgs
.Lparanoid_kernel_gsbase:
	FENCE_SWAPGS_KERNEL_ENTRY
.Lparanoid_gsbase_done:

	/*
	 * Once we have CR3 and %GS setup save and set SPEC_CTRL. Just like
	 * CR3 above, keep the old value in a callee saved register.
	 */
	IBRS_ENTER save_reg=%r15
	UNTRAIN_RET

	RET
SYM_CODE_END(paranoid_entry)

/*
 * "Paranoid" exit path from exception stack.  This is invoked
 * only on return from non-NMI IST interrupts that came
 * from kernel space.
 *
 * We may be returning to very strange contexts (e.g. very early
 * in syscall entry), so checking for preemption here would
 * be complicated.  Fortunately, there's no good reason to try
 * to handle preemption here.
 *
 * R/EBX contains the GSBASE related information depending on the
 * availability of the FSGSBASE instructions:
 *
 * FSGSBASE	R/EBX
 *     N        0 -> SWAPGS on exit
 *              1 -> no SWAPGS on exit
 *
 *     Y        User space GSBASE, must be restored unconditionally
 *
 * R14 - old CR3
 * R15 - old SPEC_CTRL
 */
SYM_CODE_START_LOCAL(paranoid_exit)
	UNWIND_HINT_REGS

	/*
	 * Must restore IBRS state before both CR3 and %GS since we need access
	 * to the per-CPU x86_spec_ctrl_shadow variable.
	 */
	IBRS_EXIT save_reg=%r15

	/*
	 * The order of operations is important. RESTORE_CR3 requires
	 * kernel GSBASE.
	 *
	 * NB to anyone to try to optimize this code: this code does
	 * not execute at all for exceptions from user mode. Those
	 * exceptions go through error_exit instead.
	 */
	RESTORE_CR3	scratch_reg=%rax save_reg=%r14

	/* Handle the three GSBASE cases */
	ALTERNATIVE "jmp .Lparanoid_exit_checkgs", "", X86_FEATURE_FSGSBASE

	/* With FSGSBASE enabled, unconditionally restore GSBASE */
	wrgsbase	%rbx
	jmp		restore_regs_and_return_to_kernel

.Lparanoid_exit_checkgs:
	/* On non-FSGSBASE systems, conditionally do SWAPGS */
	testl		%ebx, %ebx
	jnz		restore_regs_and_return_to_kernel

	/* We are returning to a context with user GSBASE */
	swapgs
	jmp		restore_regs_and_return_to_kernel
SYM_CODE_END(paranoid_exit)

/*
 * Save all registers in pt_regs, and switch GS if needed.
 */
SYM_CODE_START_LOCAL(error_entry)
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
	IBRS_ENTER
	UNTRAIN_RET

.Lerror_entry_from_usermode_after_swapgs:

	/* Put us onto the real thread stack. */
	popq	%r12				/* save return addr in %12 */
	movq	%rsp, %rdi			/* arg0 = pt_regs pointer */
	call	sync_regs
	movq	%rax, %rsp			/* switch stack */
	ENCODE_FRAME_POINTER
	pushq	%r12
	RET

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

	/*
	 * Issue an LFENCE to prevent GS speculation, regardless of whether it is a
	 * kernel or user gsbase.
	 */
.Lerror_entry_done_lfence:
	FENCE_SWAPGS_KERNEL_ENTRY
	ANNOTATE_UNRET_END
	RET

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
	IBRS_ENTER
	UNTRAIN_RET

	/*
	 * Pretend that the exception came from user mode: set up pt_regs
	 * as if we faulted immediately after IRET.
	 */
	mov	%rsp, %rdi
	call	fixup_bad_iret
	mov	%rax, %rsp
	jmp	.Lerror_entry_from_usermode_after_swapgs
SYM_CODE_END(error_entry)

SYM_CODE_START_LOCAL(error_return)
	UNWIND_HINT_REGS
	DEBUG_ENTRY_ASSERT_IRQS_OFF
	testb	$3, CS(%rsp)
	jz	restore_regs_and_return_to_kernel
	jmp	swapgs_restore_regs_and_return_to_usermode
SYM_CODE_END(error_return)

/*
 * Runs on exception stack.  Xen PV does not go through this path at all,
 * so we can use real assembly here.
 *
 * Registers:
 *	%r14: Used to save/restore the CR3 of the interrupted context
 *	      when PAGE_TABLE_ISOLATION is in use.  Do not clobber.
 */
SYM_CODE_START(asm_exc_nmi)
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

	IBRS_ENTER
	UNTRAIN_RET

	/*
	 * At this point we no longer need to worry about stack damage
	 * due to nesting -- we're on the normal thread stack and we're
	 * done with the NMI stack.
	 */

	movq	%rsp, %rdi
	movq	$-1, %rsi
	call	exc_nmi

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
	 * about to about to call exc_nmi() anyway, so we can just
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
	 * gsbase if needed before we call exc_nmi().  "NMI executing"
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

	movq	%rsp, %rdi
	movq	$-1, %rsi
	call	exc_nmi

	/* Always restore stashed SPEC_CTRL value (see paranoid_entry) */
	IBRS_EXIT save_reg=%r15

	/* Always restore stashed CR3 value (see paranoid_entry) */
	RESTORE_CR3 scratch_reg=%r15 save_reg=%r14

	/*
	 * The above invocation of paranoid_entry stored the GSBASE
	 * related information in R/EBX depending on the availability
	 * of FSGSBASE.
	 *
	 * If FSGSBASE is enabled, restore the saved GSBASE value
	 * unconditionally, otherwise take the conditional SWAPGS path.
	 */
	ALTERNATIVE "jmp nmi_no_fsgsbase", "", X86_FEATURE_FSGSBASE

	wrgsbase	%rbx
	jmp	nmi_restore

nmi_no_fsgsbase:
	/* EBX == 0 -> invoke SWAPGS */
	testl	%ebx, %ebx
	jnz	nmi_restore

nmi_swapgs:
	swapgs

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
SYM_CODE_END(asm_exc_nmi)

#ifndef CONFIG_IA32_EMULATION
/*
 * This handles SYSCALL from 32-bit code.  There is no way to program
 * MSRs to fully disable 32-bit SYSCALL.
 */
SYM_CODE_START(ignore_sysret)
	UNWIND_HINT_EMPTY
	mov	$-ENOSYS, %eax
	sysretl
SYM_CODE_END(ignore_sysret)
#endif

.pushsection .text, "ax"
SYM_CODE_START(rewind_stack_do_exit)
	UNWIND_HINT_FUNC
	/* Prevent any naive code from trying to unwind to our caller. */
	xorl	%ebp, %ebp

	movq	PER_CPU_VAR(cpu_current_top_of_stack), %rax
	leaq	-PTREGS_SIZE(%rax), %rsp
	UNWIND_HINT_REGS

	call	do_exit
SYM_CODE_END(rewind_stack_do_exit)
.popsection
