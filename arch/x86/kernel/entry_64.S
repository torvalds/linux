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
 * Normal syscalls and interrupts don't save a full stack frame, this is
 * only done for syscall tracing, signals or fork/exec et.al.
 *
 * A note on terminology:
 * - top of stack: Architecture defined interrupt frame from SS to RIP
 * at the top of the kernel process stack.
 * - partial stack frame: partially saved registers up to R11.
 * - full stack frame: Like partial stack frame, but all register saved.
 *
 * Some macro usage:
 * - CFI macros are used to generate dwarf2 unwind information for better
 * backtraces. They don't change any code.
 * - SAVE_ALL/RESTORE_ALL - Save/restore all registers
 * - SAVE_ARGS/RESTORE_ARGS - Save/restore registers that C functions modify.
 * There are unfortunately lots of special cases where some registers
 * not touched. The macro is a big mess that should be cleaned up.
 * - SAVE_REST/RESTORE_REST - Handle the registers not saved by SAVE_ARGS.
 * Gives a full stack frame.
 * - ENTRY/END Define functions in the symbol table.
 * - FIXUP_TOP_OF_STACK/RESTORE_TOP_OF_STACK - Fix up the hardware stack
 * frame that is otherwise undefined after a SYSCALL
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


#ifndef CONFIG_PREEMPT
#define retint_kernel retint_restore_args
#endif

#ifdef CONFIG_PARAVIRT
ENTRY(native_usergs_sysret64)
	swapgs
	sysretq
ENDPROC(native_usergs_sysret64)
#endif /* CONFIG_PARAVIRT */


.macro TRACE_IRQS_IRETQ offset=ARGOFFSET
#ifdef CONFIG_TRACE_IRQFLAGS
	bt   $9,EFLAGS-\offset(%rsp)	/* interrupts off? */
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

.macro TRACE_IRQS_IRETQ_DEBUG offset=ARGOFFSET
	bt   $9,EFLAGS-\offset(%rsp)	/* interrupts off? */
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
 * C code is not supposed to know about undefined top of stack. Every time
 * a C function with an pt_regs argument is called from the SYSCALL based
 * fast path FIXUP_TOP_OF_STACK is needed.
 * RESTORE_TOP_OF_STACK syncs the syscall state after any possible ptregs
 * manipulation.
 */

	/* %rsp:at FRAMEEND */
	.macro FIXUP_TOP_OF_STACK tmp offset=0
	movq PER_CPU_VAR(old_rsp),\tmp
	movq \tmp,RSP+\offset(%rsp)
	movq $__USER_DS,SS+\offset(%rsp)
	movq $__USER_CS,CS+\offset(%rsp)
	movq $-1,RCX+\offset(%rsp)
	movq R11+\offset(%rsp),\tmp  /* get eflags */
	movq \tmp,EFLAGS+\offset(%rsp)
	.endm

	.macro RESTORE_TOP_OF_STACK tmp offset=0
	movq RSP+\offset(%rsp),\tmp
	movq \tmp,PER_CPU_VAR(old_rsp)
	movq EFLAGS+\offset(%rsp),\tmp
	movq \tmp,R11+\offset(%rsp)
	.endm

	.macro FAKE_STACK_FRAME child_rip
	/* push in order ss, rsp, eflags, cs, rip */
	xorl %eax, %eax
	pushq_cfi $__KERNEL_DS /* ss */
	/*CFI_REL_OFFSET	ss,0*/
	pushq_cfi %rax /* rsp */
	CFI_REL_OFFSET	rsp,0
	pushq_cfi $(X86_EFLAGS_IF|X86_EFLAGS_FIXED) /* eflags - interrupts on */
	/*CFI_REL_OFFSET	rflags,0*/
	pushq_cfi $__KERNEL_CS /* cs */
	/*CFI_REL_OFFSET	cs,0*/
	pushq_cfi \child_rip /* rip */
	CFI_REL_OFFSET	rip,0
	pushq_cfi %rax /* orig rax */
	.endm

	.macro UNFAKE_STACK_FRAME
	addq $8*6, %rsp
	CFI_ADJUST_CFA_OFFSET	-(6*8)
	.endm

/*
 * initial frame state for interrupts (and exceptions without error code)
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
	EMPTY_FRAME \start, SS+8+\offset-RIP
	/*CFI_REL_OFFSET ss, SS+\offset-RIP*/
	CFI_REL_OFFSET rsp, RSP+\offset-RIP
	/*CFI_REL_OFFSET rflags, EFLAGS+\offset-RIP*/
	/*CFI_REL_OFFSET cs, CS+\offset-RIP*/
	CFI_REL_OFFSET rip, RIP+\offset-RIP
	.endm

/*
 * initial frame state for exceptions with error code (and interrupts
 * with vector already pushed)
 */
	.macro XCPT_FRAME start=1 offset=0
	INTR_FRAME \start, RIP+\offset-ORIG_RAX
	.endm

/*
 * frame that enables calling into C.
 */
	.macro PARTIAL_FRAME start=1 offset=0
	XCPT_FRAME \start, ORIG_RAX+\offset-ARGOFFSET
	CFI_REL_OFFSET rdi, RDI+\offset-ARGOFFSET
	CFI_REL_OFFSET rsi, RSI+\offset-ARGOFFSET
	CFI_REL_OFFSET rdx, RDX+\offset-ARGOFFSET
	CFI_REL_OFFSET rcx, RCX+\offset-ARGOFFSET
	CFI_REL_OFFSET rax, RAX+\offset-ARGOFFSET
	CFI_REL_OFFSET r8, R8+\offset-ARGOFFSET
	CFI_REL_OFFSET r9, R9+\offset-ARGOFFSET
	CFI_REL_OFFSET r10, R10+\offset-ARGOFFSET
	CFI_REL_OFFSET r11, R11+\offset-ARGOFFSET
	.endm

/*
 * frame that enables passing a complete pt_regs to a C function.
 */
	.macro DEFAULT_FRAME start=1 offset=0
	PARTIAL_FRAME \start, R11+\offset-R15
	CFI_REL_OFFSET rbx, RBX+\offset
	CFI_REL_OFFSET rbp, RBP+\offset
	CFI_REL_OFFSET r12, R12+\offset
	CFI_REL_OFFSET r13, R13+\offset
	CFI_REL_OFFSET r14, R14+\offset
	CFI_REL_OFFSET r15, R15+\offset
	.endm

/* save partial stack frame */
	.macro SAVE_ARGS_IRQ
	cld
	/* start from rbp in pt_regs and jump over */
	movq_cfi rdi, (RDI-RBP)
	movq_cfi rsi, (RSI-RBP)
	movq_cfi rdx, (RDX-RBP)
	movq_cfi rcx, (RCX-RBP)
	movq_cfi rax, (RAX-RBP)
	movq_cfi  r8,  (R8-RBP)
	movq_cfi  r9,  (R9-RBP)
	movq_cfi r10, (R10-RBP)
	movq_cfi r11, (R11-RBP)

	/* Save rbp so that we can unwind from get_irq_regs() */
	movq_cfi rbp, 0

	/* Save previous stack value */
	movq %rsp, %rsi

	leaq -RBP(%rsp),%rdi	/* arg1 for handler */
	testl $3, CS-RBP(%rsi)
	je 1f
	SWAPGS
	/*
	 * irq_count is used to check if a CPU is already on an interrupt stack
	 * or not. While this is essentially redundant with preempt_count it is
	 * a little cheaper to use a separate counter in the PDA (short of
	 * moving irq_enter into assembly, which would be too much work)
	 */
1:	incl PER_CPU_VAR(irq_count)
	cmovzq PER_CPU_VAR(irq_stack_ptr),%rsp
	CFI_DEF_CFA_REGISTER	rsi

	/* Store previous stack value */
	pushq %rsi
	CFI_ESCAPE	0x0f /* DW_CFA_def_cfa_expression */, 6, \
			0x77 /* DW_OP_breg7 */, 0, \
			0x06 /* DW_OP_deref */, \
			0x08 /* DW_OP_const1u */, SS+8-RBP, \
			0x22 /* DW_OP_plus */
	/* We entered an interrupt context - irqs are off: */
	TRACE_IRQS_OFF
	.endm

ENTRY(save_paranoid)
	XCPT_FRAME 1 RDI+8
	cld
	movq %rdi, RDI+8(%rsp)
	movq %rsi, RSI+8(%rsp)
	movq_cfi rdx, RDX+8
	movq_cfi rcx, RCX+8
	movq_cfi rax, RAX+8
	movq %r8, R8+8(%rsp)
	movq %r9, R9+8(%rsp)
	movq %r10, R10+8(%rsp)
	movq %r11, R11+8(%rsp)
	movq_cfi rbx, RBX+8
	movq %rbp, RBP+8(%rsp)
	movq %r12, R12+8(%rsp)
	movq %r13, R13+8(%rsp)
	movq %r14, R14+8(%rsp)
	movq %r15, R15+8(%rsp)
	movl $1,%ebx
	movl $MSR_GS_BASE,%ecx
	rdmsr
	testl %edx,%edx
	js 1f	/* negative -> in kernel */
	SWAPGS
	xorl %ebx,%ebx
1:	ret
	CFI_ENDPROC
END(save_paranoid)

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

	GET_THREAD_INFO(%rcx)

	RESTORE_REST

	testl $3, CS-ARGOFFSET(%rsp)		# from kernel_thread?
	jz   1f

	testl $_TIF_IA32, TI_flags(%rcx)	# 32-bit compat task needs IRET
	jnz  int_ret_from_sys_call

	RESTORE_TOP_OF_STACK %rdi, -ARGOFFSET
	jmp ret_from_sys_call			# go to the SYSRET fastpath

1:
	subq $REST_SKIP, %rsp	# leave space for volatiles
	CFI_ADJUST_CFA_OFFSET	REST_SKIP
	movq %rbp, %rdi
	call *%rbx
	movl $0, RAX(%rsp)
	RESTORE_REST
	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(ret_from_fork)

/*
 * System call entry. Up to 6 arguments in registers are supported.
 *
 * SYSCALL does not save anything on the stack and does not change the
 * stack pointer.  However, it does mask the flags register for us, so
 * CLD and CLAC are not needed.
 */

/*
 * Register setup:
 * rax  system call number
 * rdi  arg0
 * rcx  return address for syscall/sysret, C arg3
 * rsi  arg1
 * rdx  arg2
 * r10  arg3 	(--> moved to rcx for C)
 * r8   arg4
 * r9   arg5
 * r11  eflags for syscall/sysret, temporary for C
 * r12-r15,rbp,rbx saved by C code, not touched.
 *
 * Interrupts are off on entry.
 * Only called from user space.
 *
 * XXX	if we had a free scratch register we could save the RSP into the stack frame
 *      and report it properly in ps. Unfortunately we haven't.
 *
 * When user can change the frames always force IRET. That is because
 * it deals with uncanonical addresses better. SYSRET has trouble
 * with them due to bugs in both AMD and Intel CPUs.
 */

ENTRY(system_call)
	CFI_STARTPROC	simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA	rsp,KERNEL_STACK_OFFSET
	CFI_REGISTER	rip,rcx
	/*CFI_REGISTER	rflags,r11*/
	SWAPGS_UNSAFE_STACK
	/*
	 * A hypervisor implementation might want to use a label
	 * after the swapgs, so that it can do the swapgs
	 * for the guest and jump here on syscall.
	 */
GLOBAL(system_call_after_swapgs)

	movq	%rsp,PER_CPU_VAR(old_rsp)
	movq	PER_CPU_VAR(kernel_stack),%rsp
	/*
	 * No need to follow this irqs off/on section - it's straight
	 * and short:
	 */
	ENABLE_INTERRUPTS(CLBR_NONE)
	SAVE_ARGS 8, 0, rax_enosys=1
	movq_cfi rax,(ORIG_RAX-ARGOFFSET)
	movq  %rcx,RIP-ARGOFFSET(%rsp)
	CFI_REL_OFFSET rip,RIP-ARGOFFSET
	testl $_TIF_WORK_SYSCALL_ENTRY,TI_flags+THREAD_INFO(%rsp,RIP-ARGOFFSET)
	jnz tracesys
system_call_fastpath:
#if __SYSCALL_MASK == ~0
	cmpq $__NR_syscall_max,%rax
#else
	andl $__SYSCALL_MASK,%eax
	cmpl $__NR_syscall_max,%eax
#endif
	ja ret_from_sys_call  /* and return regs->ax */
	movq %r10,%rcx
	call *sys_call_table(,%rax,8)  # XXX:	 rip relative
	movq %rax,RAX-ARGOFFSET(%rsp)
/*
 * Syscall return path ending with SYSRET (fast path)
 * Has incomplete stack frame and undefined top of stack.
 */
ret_from_sys_call:
	movl $_TIF_ALLWORK_MASK,%edi
	/* edi:	flagmask */
sysret_check:
	LOCKDEP_SYS_EXIT
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	movl TI_flags+THREAD_INFO(%rsp,RIP-ARGOFFSET),%edx
	andl %edi,%edx
	jnz  sysret_careful
	CFI_REMEMBER_STATE
	/*
	 * sysretq will re-enable interrupts:
	 */
	TRACE_IRQS_ON
	movq RIP-ARGOFFSET(%rsp),%rcx
	CFI_REGISTER	rip,rcx
	RESTORE_ARGS 1,-ARG_SKIP,0
	/*CFI_REGISTER	rflags,r11*/
	movq	PER_CPU_VAR(old_rsp), %rsp
	USERGS_SYSRET64

	CFI_RESTORE_STATE
	/* Handle reschedules */
	/* edx:	work, edi: workmask */
sysret_careful:
	bt $TIF_NEED_RESCHED,%edx
	jnc sysret_signal
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushq_cfi %rdi
	SCHEDULE_USER
	popq_cfi %rdi
	jmp sysret_check

	/* Handle a signal */
sysret_signal:
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
#ifdef CONFIG_AUDITSYSCALL
	bt $TIF_SYSCALL_AUDIT,%edx
	jc sysret_audit
#endif
	/*
	 * We have a signal, or exit tracing or single-step.
	 * These all wind up with the iret return path anyway,
	 * so just join that path right now.
	 */
	FIXUP_TOP_OF_STACK %r11, -ARGOFFSET
	jmp int_check_syscall_exit_work

#ifdef CONFIG_AUDITSYSCALL
	/*
	 * Return fast path for syscall audit.  Call __audit_syscall_exit()
	 * directly and then jump back to the fast path with TIF_SYSCALL_AUDIT
	 * masked off.
	 */
sysret_audit:
	movq RAX-ARGOFFSET(%rsp),%rsi	/* second arg, syscall return value */
	cmpq $-MAX_ERRNO,%rsi	/* is it < -MAX_ERRNO? */
	setbe %al		/* 1 if so, 0 if not */
	movzbl %al,%edi		/* zero-extend that into %edi */
	call __audit_syscall_exit
	movl $(_TIF_ALLWORK_MASK & ~_TIF_SYSCALL_AUDIT),%edi
	jmp sysret_check
#endif	/* CONFIG_AUDITSYSCALL */

	/* Do syscall tracing */
tracesys:
	leaq -REST_SKIP(%rsp), %rdi
	movq $AUDIT_ARCH_X86_64, %rsi
	call syscall_trace_enter_phase1
	test %rax, %rax
	jnz tracesys_phase2		/* if needed, run the slow path */
	LOAD_ARGS 0			/* else restore clobbered regs */
	jmp system_call_fastpath	/*      and return to the fast path */

tracesys_phase2:
	SAVE_REST
	FIXUP_TOP_OF_STACK %rdi
	movq %rsp, %rdi
	movq $AUDIT_ARCH_X86_64, %rsi
	movq %rax,%rdx
	call syscall_trace_enter_phase2

	/*
	 * Reload arg registers from stack in case ptrace changed them.
	 * We don't reload %rax because syscall_trace_entry_phase2() returned
	 * the value it wants us to use in the table lookup.
	 */
	LOAD_ARGS ARGOFFSET, 1
	RESTORE_REST
#if __SYSCALL_MASK == ~0
	cmpq $__NR_syscall_max,%rax
#else
	andl $__SYSCALL_MASK,%eax
	cmpl $__NR_syscall_max,%eax
#endif
	ja   int_ret_from_sys_call	/* RAX(%rsp) is already set */
	movq %r10,%rcx	/* fixup for C */
	call *sys_call_table(,%rax,8)
	movq %rax,RAX-ARGOFFSET(%rsp)
	/* Use IRET because user could have changed frame */

/*
 * Syscall return path ending with IRET.
 * Has correct top of stack, but partial stack frame.
 */
GLOBAL(int_ret_from_sys_call)
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	movl $_TIF_ALLWORK_MASK,%edi
	/* edi:	mask to check */
GLOBAL(int_with_check)
	LOCKDEP_SYS_EXIT_IRQ
	GET_THREAD_INFO(%rcx)
	movl TI_flags(%rcx),%edx
	andl %edi,%edx
	jnz   int_careful
	andl    $~TS_COMPAT,TI_status(%rcx)
	jmp   retint_swapgs

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

	/* handle signals and tracing -- both require a full stack frame */
int_very_careful:
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
int_check_syscall_exit_work:
	SAVE_REST
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
	RESTORE_REST
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp int_with_check
	CFI_ENDPROC
END(system_call)

	.macro FORK_LIKE func
ENTRY(stub_\func)
	CFI_STARTPROC
	popq	%r11			/* save return address */
	PARTIAL_FRAME 0
	SAVE_REST
	pushq	%r11			/* put it back on stack */
	FIXUP_TOP_OF_STACK %r11, 8
	DEFAULT_FRAME 0 8		/* offset 8: return address */
	call sys_\func
	RESTORE_TOP_OF_STACK %r11, 8
	ret $REST_SKIP		/* pop extended registers */
	CFI_ENDPROC
END(stub_\func)
	.endm

	.macro FIXED_FRAME label,func
ENTRY(\label)
	CFI_STARTPROC
	PARTIAL_FRAME 0 8		/* offset 8: return address */
	FIXUP_TOP_OF_STACK %r11, 8-ARGOFFSET
	call \func
	RESTORE_TOP_OF_STACK %r11, 8-ARGOFFSET
	ret
	CFI_ENDPROC
END(\label)
	.endm

	FORK_LIKE  clone
	FORK_LIKE  fork
	FORK_LIKE  vfork
	FIXED_FRAME stub_iopl, sys_iopl

ENTRY(ptregscall_common)
	DEFAULT_FRAME 1 8	/* offset 8: return address */
	RESTORE_TOP_OF_STACK %r11, 8
	movq_cfi_restore R15+8, r15
	movq_cfi_restore R14+8, r14
	movq_cfi_restore R13+8, r13
	movq_cfi_restore R12+8, r12
	movq_cfi_restore RBP+8, rbp
	movq_cfi_restore RBX+8, rbx
	ret $REST_SKIP		/* pop extended registers */
	CFI_ENDPROC
END(ptregscall_common)

ENTRY(stub_execve)
	CFI_STARTPROC
	addq $8, %rsp
	PARTIAL_FRAME 0
	SAVE_REST
	FIXUP_TOP_OF_STACK %r11
	call sys_execve
	movq %rax,RAX(%rsp)
	RESTORE_REST
	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(stub_execve)

ENTRY(stub_execveat)
	CFI_STARTPROC
	addq $8, %rsp
	PARTIAL_FRAME 0
	SAVE_REST
	FIXUP_TOP_OF_STACK %r11
	call sys_execveat
	RESTORE_TOP_OF_STACK %r11
	movq %rax,RAX(%rsp)
	RESTORE_REST
	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(stub_execveat)

/*
 * sigreturn is special because it needs to restore all registers on return.
 * This cannot be done with SYSRET, so use the IRET return path instead.
 */
ENTRY(stub_rt_sigreturn)
	CFI_STARTPROC
	addq $8, %rsp
	PARTIAL_FRAME 0
	SAVE_REST
	FIXUP_TOP_OF_STACK %r11
	call sys_rt_sigreturn
	movq %rax,RAX(%rsp) # fixme, this could be done at the higher layer
	RESTORE_REST
	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(stub_rt_sigreturn)

#ifdef CONFIG_X86_X32_ABI
ENTRY(stub_x32_rt_sigreturn)
	CFI_STARTPROC
	addq $8, %rsp
	PARTIAL_FRAME 0
	SAVE_REST
	FIXUP_TOP_OF_STACK %r11
	call sys32_x32_rt_sigreturn
	movq %rax,RAX(%rsp) # fixme, this could be done at the higher layer
	RESTORE_REST
	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(stub_x32_rt_sigreturn)

ENTRY(stub_x32_execve)
	CFI_STARTPROC
	addq $8, %rsp
	PARTIAL_FRAME 0
	SAVE_REST
	FIXUP_TOP_OF_STACK %r11
	call compat_sys_execve
	RESTORE_TOP_OF_STACK %r11
	movq %rax,RAX(%rsp)
	RESTORE_REST
	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(stub_x32_execve)

ENTRY(stub_x32_execveat)
	CFI_STARTPROC
	addq $8, %rsp
	PARTIAL_FRAME 0
	SAVE_REST
	FIXUP_TOP_OF_STACK %r11
	call compat_sys_execveat
	RESTORE_TOP_OF_STACK %r11
	movq %rax,RAX(%rsp)
	RESTORE_REST
	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(stub_x32_execveat)

#endif

/*
 * Build the entry stubs and pointer table with some assembler magic.
 * We pack 7 stubs into a single 32-byte chunk, which will fit in a
 * single cache line on all modern x86 implementations.
 */
	.section .init.rodata,"a"
ENTRY(interrupt)
	.section .entry.text
	.p2align 5
	.p2align CONFIG_X86_L1_CACHE_SHIFT
ENTRY(irq_entries_start)
	INTR_FRAME
vector=FIRST_EXTERNAL_VECTOR
.rept (FIRST_SYSTEM_VECTOR-FIRST_EXTERNAL_VECTOR+6)/7
	.balign 32
  .rept	7
    .if vector < FIRST_SYSTEM_VECTOR
      .if vector <> FIRST_EXTERNAL_VECTOR
	CFI_ADJUST_CFA_OFFSET -8
      .endif
1:	pushq_cfi $(~vector+0x80)	/* Note: always in signed byte range */
      .if ((vector-FIRST_EXTERNAL_VECTOR)%7) <> 6
	jmp 2f
      .endif
      .previous
	.quad 1b
      .section .entry.text
vector=vector+1
    .endif
  .endr
2:	jmp common_interrupt
.endr
	CFI_ENDPROC
END(irq_entries_start)

.previous
END(interrupt)
.previous

/*
 * Interrupt entry/exit.
 *
 * Interrupt entry points save only callee clobbered registers in fast path.
 *
 * Entry runs with interrupts off.
 */

/* 0(%rsp): ~(interrupt number) */
	.macro interrupt func
	/* reserve pt_regs for scratch regs and rbp */
	subq $ORIG_RAX-RBP, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-RBP
	SAVE_ARGS_IRQ
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
	/* 0(%rsp): old_rsp-ARGOFFSET */
ret_from_intr:
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	decl PER_CPU_VAR(irq_count)

	/* Restore saved previous stack */
	popq %rsi
	CFI_DEF_CFA rsi,SS+8-RBP	/* reg/off reset after def_cfa_expr */
	leaq ARGOFFSET-RBP(%rsi), %rsp
	CFI_DEF_CFA_REGISTER	rsp
	CFI_ADJUST_CFA_OFFSET	RBP-ARGOFFSET

exit_intr:
	GET_THREAD_INFO(%rcx)
	testl $3,CS-ARGOFFSET(%rsp)
	je retint_kernel

	/* Interrupt came from user space */
	/*
	 * Has a correct top of stack, but a partial stack frame
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
	jmp restore_args

retint_restore_args:	/* return to kernel space */
	DISABLE_INTERRUPTS(CLBR_ANY)
	/*
	 * The iretq could re-enable interrupts:
	 */
	TRACE_IRQS_IRETQ
restore_args:
	RESTORE_ARGS 1,8,1

irq_return:
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
	SAVE_REST
	movq $-1,ORIG_RAX(%rsp)
	xorl %esi,%esi		# oldset
	movq %rsp,%rdi		# &pt_regs
	call do_notify_resume
	RESTORE_REST
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	GET_THREAD_INFO(%rcx)
	jmp retint_with_reschedule

#ifdef CONFIG_PREEMPT
	/* Returning to kernel space. Check if we need preemption */
	/* rcx:	 threadinfo. interrupts off. */
ENTRY(retint_kernel)
	cmpl $0,PER_CPU_VAR(__preempt_count)
	jnz  retint_restore_args
	bt   $9,EFLAGS-ARGOFFSET(%rsp)	/* interrupts off? */
	jnc  retint_restore_args
	call preempt_schedule_irq
	jmp exit_intr
#endif
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
#define INIT_TSS_IST(x) PER_CPU_VAR(init_tss) + (TSS_ist + ((x) - 1) * 8)

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

	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15

	.if \paranoid
	.if \paranoid == 1
	CFI_REMEMBER_STATE
	testl $3, CS(%rsp)		/* If coming from userspace, switch */
	jnz 1f				/* stacks. */
	.endif
	call save_paranoid
	.else
	call error_entry
	.endif

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
	subq $EXCEPTION_STKSZ, INIT_TSS_IST(\shift_ist)
	.endif

	call \do_sym

	.if \shift_ist != -1
	addq $EXCEPTION_STKSZ, INIT_TSS_IST(\shift_ist)
	.endif

	.if \paranoid
	jmp paranoid_exit		/* %ebx: no swapgs flag */
	.else
	jmp error_exit			/* %ebx: no swapgs flag */
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
	SAVE_ALL
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
	 * "Paranoid" exit path from exception stack.  This is invoked
	 * only on return from non-NMI IST interrupts that came
	 * from kernel space.
	 *
	 * We may be returning to very strange contexts (e.g. very early
	 * in syscall entry), so checking for preemption here would
	 * be complicated.  Fortunately, we there's no good reason
	 * to try to handle preemption here.
	 */

	/* ebx:	no swapgs flag */
ENTRY(paranoid_exit)
	DEFAULT_FRAME
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF_DEBUG
	testl %ebx,%ebx				/* swapgs needed? */
	jnz paranoid_restore
	TRACE_IRQS_IRETQ 0
	SWAPGS_UNSAFE_STACK
	RESTORE_ALL 8
	INTERRUPT_RETURN
paranoid_restore:
	TRACE_IRQS_IRETQ_DEBUG 0
	RESTORE_ALL 8
	INTERRUPT_RETURN
	CFI_ENDPROC
END(paranoid_exit)

/*
 * Exception entry point. This expects an error code/orig_rax on the stack.
 * returns in "no swapgs flag" in %ebx.
 */
ENTRY(error_entry)
	XCPT_FRAME
	CFI_ADJUST_CFA_OFFSET 15*8
	/* oldrax contains error code */
	cld
	movq %rdi, RDI+8(%rsp)
	movq %rsi, RSI+8(%rsp)
	movq %rdx, RDX+8(%rsp)
	movq %rcx, RCX+8(%rsp)
	movq %rax, RAX+8(%rsp)
	movq  %r8,  R8+8(%rsp)
	movq  %r9,  R9+8(%rsp)
	movq %r10, R10+8(%rsp)
	movq %r11, R11+8(%rsp)
	movq_cfi rbx, RBX+8
	movq %rbp, RBP+8(%rsp)
	movq %r12, R12+8(%rsp)
	movq %r13, R13+8(%rsp)
	movq %r14, R14+8(%rsp)
	movq %r15, R15+8(%rsp)
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


/* ebx:	no swapgs flag (1: don't need swapgs, 0: need it) */
ENTRY(error_exit)
	DEFAULT_FRAME
	movl %ebx,%eax
	RESTORE_REST
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

/*
 * Test if a given stack is an NMI stack or not.
 */
	.macro test_in_nmi reg stack nmi_ret normal_ret
	cmpq %\reg, \stack
	ja \normal_ret
	subq $EXCEPTION_STKSZ, %\reg
	cmpq %\reg, \stack
	jb \normal_ret
	jmp \nmi_ret
	.endm

	/* runs on exception stack */
ENTRY(nmi)
	INTR_FRAME
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
	 *    o Copy the interrupt frame into a "saved" location on the stack
	 *    o Copy the interrupt frame into a "copy" location on the stack
	 *    o Continue processing the NMI
	 *  If the variable is set or the previous stack is the NMI stack:
	 *    o Modify the "copy" location to jump to the repeate_nmi
	 *    o return back to the first NMI
	 *
	 * Now on exit of the first NMI, we first clear the stack variable
	 * The NMI stack will tell any nested NMIs at that point that it is
	 * nested. Then we pop the stack normally with iret, and if there was
	 * a nested NMI that updated the copy interrupt stack frame, a
	 * jump will be made to the repeat_nmi code that will handle the second
	 * NMI.
	 */

	/* Use %rdx as out temp variable throughout */
	pushq_cfi %rdx
	CFI_REL_OFFSET rdx, 0

	/*
	 * If %cs was not the kernel segment, then the NMI triggered in user
	 * space, which means it is definitely not nested.
	 */
	cmpl $__KERNEL_CS, 16(%rsp)
	jne first_nmi

	/*
	 * Check the special variable on the stack to see if NMIs are
	 * executing.
	 */
	cmpl $1, -8(%rsp)
	je nested_nmi

	/*
	 * Now test if the previous stack was an NMI stack.
	 * We need the double check. We check the NMI stack to satisfy the
	 * race when the first NMI clears the variable before returning.
	 * We check the variable because the first NMI could be in a
	 * breakpoint routine using a breakpoint stack.
	 */
	lea 6*8(%rsp), %rdx
	test_in_nmi rdx, 4*8(%rsp), nested_nmi, first_nmi
	CFI_REMEMBER_STATE

nested_nmi:
	/*
	 * Do nothing if we interrupted the fixup in repeat_nmi.
	 * It's about to repeat the NMI handler, so we are fine
	 * with ignoring this one.
	 */
	movq $repeat_nmi, %rdx
	cmpq 8(%rsp), %rdx
	ja 1f
	movq $end_repeat_nmi, %rdx
	cmpq 8(%rsp), %rdx
	ja nested_nmi_out

1:
	/* Set up the interrupted NMIs stack to jump to repeat_nmi */
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

	/* No need to check faults here */
	INTERRUPT_RETURN

	CFI_RESTORE_STATE
first_nmi:
	/*
	 * Because nested NMIs will use the pushed location that we
	 * stored in rdx, we must keep that space available.
	 * Here's what our stack frame will look like:
	 * +-------------------------+
	 * | original SS             |
	 * | original Return RSP     |
	 * | original RFLAGS         |
	 * | original CS             |
	 * | original RIP            |
	 * +-------------------------+
	 * | temp storage for rdx    |
	 * +-------------------------+
	 * | NMI executing variable  |
	 * +-------------------------+
	 * | copied SS               |
	 * | copied Return RSP       |
	 * | copied RFLAGS           |
	 * | copied CS               |
	 * | copied RIP              |
	 * +-------------------------+
	 * | Saved SS                |
	 * | Saved Return RSP        |
	 * | Saved RFLAGS            |
	 * | Saved CS                |
	 * | Saved RIP               |
	 * +-------------------------+
	 * | pt_regs                 |
	 * +-------------------------+
	 *
	 * The saved stack frame is used to fix up the copied stack frame
	 * that a nested NMI may change to make the interrupted NMI iret jump
	 * to the repeat_nmi. The original stack frame and the temp storage
	 * is also used by nested NMIs and can not be trusted on exit.
	 */
	/* Do not pop rdx, nested NMIs will corrupt that part of the stack */
	movq (%rsp), %rdx
	CFI_RESTORE rdx

	/* Set the NMI executing variable on the stack. */
	pushq_cfi $1

	/*
	 * Leave room for the "copied" frame
	 */
	subq $(5*8), %rsp
	CFI_ADJUST_CFA_OFFSET 5*8

	/* Copy the stack frame to the Saved frame */
	.rept 5
	pushq_cfi 11*8(%rsp)
	.endr
	CFI_DEF_CFA_OFFSET SS+8-RIP

	/* Everything up to here is safe from nested NMIs */

	/*
	 * If there was a nested NMI, the first NMI's iret will return
	 * here. But NMIs are still enabled and we can take another
	 * nested NMI. The nested NMI checks the interrupted RIP to see
	 * if it is between repeat_nmi and end_repeat_nmi, and if so
	 * it will just return, as we are about to repeat an NMI anyway.
	 * This makes it safe to copy to the stack frame that a nested
	 * NMI will update.
	 */
repeat_nmi:
	/*
	 * Update the stack variable to say we are still in NMI (the update
	 * is benign for the non-repeat case, where 1 was pushed just above
	 * to this very stack slot).
	 */
	movq $1, 10*8(%rsp)

	/* Make another copy, this one may be modified by nested NMIs */
	addq $(10*8), %rsp
	CFI_ADJUST_CFA_OFFSET -10*8
	.rept 5
	pushq_cfi -6*8(%rsp)
	.endr
	subq $(5*8), %rsp
	CFI_DEF_CFA_OFFSET SS+8-RIP
end_repeat_nmi:

	/*
	 * Everything below this point can be preempted by a nested
	 * NMI if the first NMI took an exception and reset our iret stack
	 * so that we repeat another NMI.
	 */
	pushq_cfi $-1		/* ORIG_RAX: no syscall to restart */
	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15
	/*
	 * Use save_paranoid to handle SWAPGS, but no need to use paranoid_exit
	 * as we should not be calling schedule in NMI context.
	 * Even with normal interrupts enabled. An NMI should not be
	 * setting NEED_RESCHED or anything that normal interrupts and
	 * exceptions might do.
	 */
	call save_paranoid
	DEFAULT_FRAME 0

	/*
	 * Save off the CR2 register. If we take a page fault in the NMI then
	 * it could corrupt the CR2 value. If the NMI preempts a page fault
	 * handler before it was able to read the CR2 register, and then the
	 * NMI itself takes a page fault, the page fault that was preempted
	 * will read the information from the NMI page fault and not the
	 * origin fault. Save it off and restore it if it changes.
	 * Use the r12 callee-saved register.
	 */
	movq %cr2, %r12

	/* paranoidentry do_nmi, 0; without TRACE_IRQS_OFF */
	movq %rsp,%rdi
	movq $-1,%rsi
	call do_nmi

	/* Did the NMI take a page fault? Restore cr2 if it did */
	movq %cr2, %rcx
	cmpq %rcx, %r12
	je 1f
	movq %r12, %cr2
1:
	
	testl %ebx,%ebx				/* swapgs needed? */
	jnz nmi_restore
nmi_swapgs:
	SWAPGS_UNSAFE_STACK
nmi_restore:
	/* Pop the extra iret frame at once */
	RESTORE_ALL 6*8

	/* Clear the NMI executing stack variable */
	movq $0, 5*8(%rsp)
	jmp irq_return
	CFI_ENDPROC
END(nmi)

ENTRY(ignore_sysret)
	CFI_STARTPROC
	mov $-ENOSYS,%eax
	sysret
	CFI_ENDPROC
END(ignore_sysret)

