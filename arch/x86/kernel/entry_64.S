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
 * - errorentry/paranoidentry/zeroentry - Define exception entry points.
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
#include <asm/ftrace.h>
#include <asm/percpu.h>

/* Avoid __ASSEMBLER__'ifying <linux/audit.h> just for this.  */
#include <linux/elf-em.h>
#define AUDIT_ARCH_X86_64	(EM_X86_64|__AUDIT_ARCH_64BIT|__AUDIT_ARCH_LE)
#define __AUDIT_ARCH_64BIT 0x80000000
#define __AUDIT_ARCH_LE	   0x40000000

	.code64
	.section .entry.text, "ax"

#ifdef CONFIG_FUNCTION_TRACER
#ifdef CONFIG_DYNAMIC_FTRACE
ENTRY(mcount)
	retq
END(mcount)

ENTRY(ftrace_caller)
	cmpl $0, function_trace_stop
	jne  ftrace_stub

	MCOUNT_SAVE_FRAME

	movq 0x38(%rsp), %rdi
	movq 8(%rbp), %rsi
	subq $MCOUNT_INSN_SIZE, %rdi

GLOBAL(ftrace_call)
	call ftrace_stub

	MCOUNT_RESTORE_FRAME

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
GLOBAL(ftrace_graph_call)
	jmp ftrace_stub
#endif

GLOBAL(ftrace_stub)
	retq
END(ftrace_caller)

#else /* ! CONFIG_DYNAMIC_FTRACE */
ENTRY(mcount)
	cmpl $0, function_trace_stop
	jne  ftrace_stub

	cmpq $ftrace_stub, ftrace_trace_function
	jnz trace

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	cmpq $ftrace_stub, ftrace_graph_return
	jnz ftrace_graph_caller

	cmpq $ftrace_graph_entry_stub, ftrace_graph_entry
	jnz ftrace_graph_caller
#endif

GLOBAL(ftrace_stub)
	retq

trace:
	MCOUNT_SAVE_FRAME

	movq 0x38(%rsp), %rdi
	movq 8(%rbp), %rsi
	subq $MCOUNT_INSN_SIZE, %rdi

	call   *ftrace_trace_function

	MCOUNT_RESTORE_FRAME

	jmp ftrace_stub
END(mcount)
#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	cmpl $0, function_trace_stop
	jne ftrace_stub

	MCOUNT_SAVE_FRAME

	leaq 8(%rbp), %rdi
	movq 0x38(%rsp), %rsi
	movq (%rbp), %rdx
	subq $MCOUNT_INSN_SIZE, %rsi

	call	prepare_ftrace_return

	MCOUNT_RESTORE_FRAME

	retq
END(ftrace_graph_caller)

GLOBAL(return_to_handler)
	subq  $24, %rsp

	/* Save the return values */
	movq %rax, (%rsp)
	movq %rdx, 8(%rsp)
	movq %rbp, %rdi

	call ftrace_return_to_handler

	movq %rax, %rdi
	movq 8(%rsp), %rdx
	movq (%rsp), %rax
	addq $24, %rsp
	jmp *%rdi
#endif


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
	pushq_cfi $X86_EFLAGS_IF /* eflags - interrupts on */
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
	/*CFI_REL_OFFSET orig_rax, ORIG_RAX-ORIG_RAX*/
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
	.pushsection .kprobes.text, "ax"
ENTRY(save_args)
	XCPT_FRAME
	cld
	/*
	 * start from rbp in pt_regs and jump over
	 * return address.
	 */
	movq_cfi rdi, RDI+8-RBP
	movq_cfi rsi, RSI+8-RBP
	movq_cfi rdx, RDX+8-RBP
	movq_cfi rcx, RCX+8-RBP
	movq_cfi rax, RAX+8-RBP
	movq_cfi  r8,  R8+8-RBP
	movq_cfi  r9,  R9+8-RBP
	movq_cfi r10, R10+8-RBP
	movq_cfi r11, R11+8-RBP

	leaq -RBP+8(%rsp),%rdi	/* arg1 for handler */
	movq_cfi rbp, 8		/* push %rbp */
	leaq 8(%rsp), %rbp		/* mov %rsp, %ebp */
	testl $3, CS(%rdi)
	je 1f
	SWAPGS
	/*
	 * irq_count is used to check if a CPU is already on an interrupt stack
	 * or not. While this is essentially redundant with preempt_count it is
	 * a little cheaper to use a separate counter in the PDA (short of
	 * moving irq_enter into assembly, which would be too much work)
	 */
1:	incl PER_CPU_VAR(irq_count)
	jne 2f
	popq_cfi %rax			/* move return address... */
	mov PER_CPU_VAR(irq_stack_ptr),%rsp
	EMPTY_FRAME 0
	pushq_cfi %rbp			/* backlink for unwinder */
	pushq_cfi %rax			/* ... to the new stack */
	/*
	 * We entered an interrupt context - irqs are off:
	 */
2:	TRACE_IRQS_OFF
	ret
	CFI_ENDPROC
END(save_args)
	.popsection

ENTRY(save_rest)
	PARTIAL_FRAME 1 REST_SKIP+8
	movq 5*8+16(%rsp), %r11	/* save return address */
	movq_cfi rbx, RBX+16
	movq_cfi rbp, RBP+16
	movq_cfi r12, R12+16
	movq_cfi r13, R13+16
	movq_cfi r14, R14+16
	movq_cfi r15, R15+16
	movq %r11, 8(%rsp)	/* return address */
	FIXUP_TOP_OF_STACK %r11, 16
	ret
	CFI_ENDPROC
END(save_rest)

/* save complete stack frame */
	.pushsection .kprobes.text, "ax"
ENTRY(save_paranoid)
	XCPT_FRAME 1 RDI+8
	cld
	movq_cfi rdi, RDI+8
	movq_cfi rsi, RSI+8
	movq_cfi rdx, RDX+8
	movq_cfi rcx, RCX+8
	movq_cfi rax, RAX+8
	movq_cfi r8, R8+8
	movq_cfi r9, R9+8
	movq_cfi r10, R10+8
	movq_cfi r11, R11+8
	movq_cfi rbx, RBX+8
	movq_cfi rbp, RBP+8
	movq_cfi r12, R12+8
	movq_cfi r13, R13+8
	movq_cfi r14, R14+8
	movq_cfi r15, R15+8
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
	.popsection

/*
 * A newly forked process directly context switches into this address.
 *
 * rdi: prev task we switched from
 */
ENTRY(ret_from_fork)
	DEFAULT_FRAME

	LOCK ; btr $TIF_FORK,TI_flags(%r8)

	pushq_cfi kernel_eflags(%rip)
	popfq_cfi				# reset kernel eflags

	call schedule_tail			# rdi: 'prev' task parameter

	GET_THREAD_INFO(%rcx)

	RESTORE_REST

	testl $3, CS-ARGOFFSET(%rsp)		# from kernel_thread?
	je   int_ret_from_sys_call

	testl $_TIF_IA32, TI_flags(%rcx)	# 32-bit compat task needs IRET
	jnz  int_ret_from_sys_call

	RESTORE_TOP_OF_STACK %rdi, -ARGOFFSET
	jmp ret_from_sys_call			# go to the SYSRET fastpath

	CFI_ENDPROC
END(ret_from_fork)

/*
 * System call entry. Up to 6 arguments in registers are supported.
 *
 * SYSCALL does not save anything on the stack and does not change the
 * stack pointer.
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
ENTRY(system_call_after_swapgs)

	movq	%rsp,PER_CPU_VAR(old_rsp)
	movq	PER_CPU_VAR(kernel_stack),%rsp
	/*
	 * No need to follow this irqs off/on section - it's straight
	 * and short:
	 */
	ENABLE_INTERRUPTS(CLBR_NONE)
	SAVE_ARGS 8,1
	movq  %rax,ORIG_RAX-ARGOFFSET(%rsp)
	movq  %rcx,RIP-ARGOFFSET(%rsp)
	CFI_REL_OFFSET rip,RIP-ARGOFFSET
	GET_THREAD_INFO(%rcx)
	testl $_TIF_WORK_SYSCALL_ENTRY,TI_flags(%rcx)
	jnz tracesys
system_call_fastpath:
	cmpq $__NR_syscall_max,%rax
	ja badsys
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
	GET_THREAD_INFO(%rcx)
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	movl TI_flags(%rcx),%edx
	andl %edi,%edx
	jnz  sysret_careful
	CFI_REMEMBER_STATE
	/*
	 * sysretq will re-enable interrupts:
	 */
	TRACE_IRQS_ON
	movq RIP-ARGOFFSET(%rsp),%rcx
	CFI_REGISTER	rip,rcx
	RESTORE_ARGS 0,-ARG_SKIP,1
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
	call schedule
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

badsys:
	movq $-ENOSYS,RAX-ARGOFFSET(%rsp)
	jmp ret_from_sys_call

#ifdef CONFIG_AUDITSYSCALL
	/*
	 * Fast path for syscall audit without full syscall trace.
	 * We just call audit_syscall_entry() directly, and then
	 * jump back to the normal fast path.
	 */
auditsys:
	movq %r10,%r9			/* 6th arg: 4th syscall arg */
	movq %rdx,%r8			/* 5th arg: 3rd syscall arg */
	movq %rsi,%rcx			/* 4th arg: 2nd syscall arg */
	movq %rdi,%rdx			/* 3rd arg: 1st syscall arg */
	movq %rax,%rsi			/* 2nd arg: syscall number */
	movl $AUDIT_ARCH_X86_64,%edi	/* 1st arg: audit arch */
	call audit_syscall_entry
	LOAD_ARGS 0		/* reload call-clobbered registers */
	jmp system_call_fastpath

	/*
	 * Return fast path for syscall audit.  Call audit_syscall_exit()
	 * directly and then jump back to the fast path with TIF_SYSCALL_AUDIT
	 * masked off.
	 */
sysret_audit:
	movq RAX-ARGOFFSET(%rsp),%rsi	/* second arg, syscall return value */
	cmpq $0,%rsi		/* is it < 0? */
	setl %al		/* 1 if so, 0 if not */
	movzbl %al,%edi		/* zero-extend that into %edi */
	inc %edi /* first arg, 0->1(AUDITSC_SUCCESS), 1->2(AUDITSC_FAILURE) */
	call audit_syscall_exit
	movl $(_TIF_ALLWORK_MASK & ~_TIF_SYSCALL_AUDIT),%edi
	jmp sysret_check
#endif	/* CONFIG_AUDITSYSCALL */

	/* Do syscall tracing */
tracesys:
#ifdef CONFIG_AUDITSYSCALL
	testl $(_TIF_WORK_SYSCALL_ENTRY & ~_TIF_SYSCALL_AUDIT),TI_flags(%rcx)
	jz auditsys
#endif
	SAVE_REST
	movq $-ENOSYS,RAX(%rsp) /* ptrace can change this for a bad syscall */
	FIXUP_TOP_OF_STACK %rdi
	movq %rsp,%rdi
	call syscall_trace_enter
	/*
	 * Reload arg registers from stack in case ptrace changed them.
	 * We don't reload %rax because syscall_trace_enter() returned
	 * the value it wants us to use in the table lookup.
	 */
	LOAD_ARGS ARGOFFSET, 1
	RESTORE_REST
	cmpq $__NR_syscall_max,%rax
	ja   int_ret_from_sys_call	/* RAX(%rsp) set to -ENOSYS above */
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
	testl $3,CS-ARGOFFSET(%rsp)
	je retint_restore_args
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
	call schedule
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

/*
 * Certain special system calls that need to save a complete full stack frame.
 */
	.macro PTREGSCALL label,func,arg
ENTRY(\label)
	PARTIAL_FRAME 1 8		/* offset 8: return address */
	subq $REST_SKIP, %rsp
	CFI_ADJUST_CFA_OFFSET REST_SKIP
	call save_rest
	DEFAULT_FRAME 0 8		/* offset 8: return address */
	leaq 8(%rsp), \arg	/* pt_regs pointer */
	call \func
	jmp ptregscall_common
	CFI_ENDPROC
END(\label)
	.endm

	PTREGSCALL stub_clone, sys_clone, %r8
	PTREGSCALL stub_fork, sys_fork, %rdi
	PTREGSCALL stub_vfork, sys_vfork, %rdi
	PTREGSCALL stub_sigaltstack, sys_sigaltstack, %rdx
	PTREGSCALL stub_iopl, sys_iopl, %rsi

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
	movq %rsp, %rcx
	call sys_execve
	RESTORE_TOP_OF_STACK %r11
	movq %rax,RAX(%rsp)
	RESTORE_REST
	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(stub_execve)

/*
 * sigreturn is special because it needs to restore all registers on return.
 * This cannot be done with SYSRET, so use the IRET return path instead.
 */
ENTRY(stub_rt_sigreturn)
	CFI_STARTPROC
	addq $8, %rsp
	PARTIAL_FRAME 0
	SAVE_REST
	movq %rsp,%rdi
	FIXUP_TOP_OF_STACK %r11
	call sys_rt_sigreturn
	movq %rax,RAX(%rsp) # fixme, this could be done at the higher layer
	RESTORE_REST
	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(stub_rt_sigreturn)

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
.rept (NR_VECTORS-FIRST_EXTERNAL_VECTOR+6)/7
	.balign 32
  .rept	7
    .if vector < NR_VECTORS
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
	call save_args
	PARTIAL_FRAME 0
	call \func
	.endm

/*
 * Interrupt entry/exit should be protected against kprobes
 */
	.pushsection .kprobes.text, "ax"
	/*
	 * The interrupt stubs push (~vector+0x80) onto the stack and
	 * then jump to common_interrupt.
	 */
	.p2align CONFIG_X86_L1_CACHE_SHIFT
common_interrupt:
	XCPT_FRAME
	addq $-0x80,(%rsp)		/* Adjust vector to [-256,-1] range */
	interrupt do_IRQ
	/* 0(%rsp): old_rsp-ARGOFFSET */
ret_from_intr:
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	decl PER_CPU_VAR(irq_count)
	leaveq

	CFI_RESTORE		rbp
	CFI_DEF_CFA_REGISTER	rsp
	CFI_ADJUST_CFA_OFFSET	-8

	/* we did not save rbx, restore only from ARGOFFSET */
	addq $8, %rsp
	CFI_ADJUST_CFA_OFFSET	-8
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
	RESTORE_ARGS 0,8,0

irq_return:
	INTERRUPT_RETURN

	.section __ex_table, "a"
	.quad irq_return, bad_iret
	.previous

#ifdef CONFIG_PARAVIRT
ENTRY(native_iret)
	iretq

	.section __ex_table,"a"
	.quad native_iret, bad_iret
	.previous
#endif

	.section .fixup,"ax"
bad_iret:
	/*
	 * The iret traps when the %cs or %ss being restored is bogus.
	 * We've lost the original trap vector and error code.
	 * #GPF is the most likely one to get for an invalid selector.
	 * So pretend we completed the iret and took the #GPF in user mode.
	 *
	 * We are now running with the kernel GS after exception recovery.
	 * But error_entry expects us to have user GS to match the user %cs,
	 * so swap back.
	 */
	pushq $0

	SWAPGS
	jmp general_protection

	.previous

	/* edi: workmask, edx: work */
retint_careful:
	CFI_RESTORE_STATE
	bt    $TIF_NEED_RESCHED,%edx
	jnc   retint_signal
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushq_cfi %rdi
	call  schedule
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
	cmpl $0,TI_preempt_count(%rcx)
	jnz  retint_restore_args
	bt  $TIF_NEED_RESCHED,TI_flags(%rcx)
	jnc  retint_restore_args
	bt   $9,EFLAGS-ARGOFFSET(%rsp)	/* interrupts off? */
	jnc  retint_restore_args
	call preempt_schedule_irq
	jmp exit_intr
#endif

	CFI_ENDPROC
END(common_interrupt)
/*
 * End of kprobes section
 */
       .popsection

/*
 * APIC interrupts.
 */
.macro apicinterrupt num sym do_sym
ENTRY(\sym)
	INTR_FRAME
	pushq_cfi $~(\num)
	interrupt \do_sym
	jmp ret_from_intr
	CFI_ENDPROC
END(\sym)
.endm

#ifdef CONFIG_SMP
apicinterrupt IRQ_MOVE_CLEANUP_VECTOR \
	irq_move_cleanup_interrupt smp_irq_move_cleanup_interrupt
apicinterrupt REBOOT_VECTOR \
	reboot_interrupt smp_reboot_interrupt
#endif

#ifdef CONFIG_X86_UV
apicinterrupt UV_BAU_MESSAGE \
	uv_bau_message_intr1 uv_bau_message_interrupt
#endif
apicinterrupt LOCAL_TIMER_VECTOR \
	apic_timer_interrupt smp_apic_timer_interrupt
apicinterrupt X86_PLATFORM_IPI_VECTOR \
	x86_platform_ipi smp_x86_platform_ipi

#ifdef CONFIG_SMP
.irp idx,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, \
	16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
.if NUM_INVALIDATE_TLB_VECTORS > \idx
apicinterrupt (INVALIDATE_TLB_VECTOR_START)+\idx \
	invalidate_interrupt\idx smp_invalidate_interrupt
.endif
.endr
#endif

apicinterrupt THRESHOLD_APIC_VECTOR \
	threshold_interrupt smp_threshold_interrupt
apicinterrupt THERMAL_APIC_VECTOR \
	thermal_interrupt smp_thermal_interrupt

#ifdef CONFIG_X86_MCE
apicinterrupt MCE_SELF_VECTOR \
	mce_self_interrupt smp_mce_self_interrupt
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
.macro zeroentry sym do_sym
ENTRY(\sym)
	INTR_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	pushq_cfi $-1		/* ORIG_RAX: no syscall to restart */
	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15
	call error_entry
	DEFAULT_FRAME 0
	movq %rsp,%rdi		/* pt_regs pointer */
	xorl %esi,%esi		/* no error code */
	call \do_sym
	jmp error_exit		/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

.macro paranoidzeroentry sym do_sym
ENTRY(\sym)
	INTR_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	pushq_cfi $-1		/* ORIG_RAX: no syscall to restart */
	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15
	call save_paranoid
	TRACE_IRQS_OFF
	movq %rsp,%rdi		/* pt_regs pointer */
	xorl %esi,%esi		/* no error code */
	call \do_sym
	jmp paranoid_exit	/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

#define INIT_TSS_IST(x) PER_CPU_VAR(init_tss) + (TSS_ist + ((x) - 1) * 8)
.macro paranoidzeroentry_ist sym do_sym ist
ENTRY(\sym)
	INTR_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	pushq_cfi $-1		/* ORIG_RAX: no syscall to restart */
	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15
	call save_paranoid
	TRACE_IRQS_OFF
	movq %rsp,%rdi		/* pt_regs pointer */
	xorl %esi,%esi		/* no error code */
	subq $EXCEPTION_STKSZ, INIT_TSS_IST(\ist)
	call \do_sym
	addq $EXCEPTION_STKSZ, INIT_TSS_IST(\ist)
	jmp paranoid_exit	/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

.macro errorentry sym do_sym
ENTRY(\sym)
	XCPT_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15
	call error_entry
	DEFAULT_FRAME 0
	movq %rsp,%rdi			/* pt_regs pointer */
	movq ORIG_RAX(%rsp),%rsi	/* get error code */
	movq $-1,ORIG_RAX(%rsp)		/* no syscall to restart */
	call \do_sym
	jmp error_exit			/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

	/* error code is on the stack already */
.macro paranoiderrorentry sym do_sym
ENTRY(\sym)
	XCPT_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15
	call save_paranoid
	DEFAULT_FRAME 0
	TRACE_IRQS_OFF
	movq %rsp,%rdi			/* pt_regs pointer */
	movq ORIG_RAX(%rsp),%rsi	/* get error code */
	movq $-1,ORIG_RAX(%rsp)		/* no syscall to restart */
	call \do_sym
	jmp paranoid_exit		/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

zeroentry divide_error do_divide_error
zeroentry overflow do_overflow
zeroentry bounds do_bounds
zeroentry invalid_op do_invalid_op
zeroentry device_not_available do_device_not_available
paranoiderrorentry double_fault do_double_fault
zeroentry coprocessor_segment_overrun do_coprocessor_segment_overrun
errorentry invalid_TSS do_invalid_TSS
errorentry segment_not_present do_segment_not_present
zeroentry spurious_interrupt_bug do_spurious_interrupt_bug
zeroentry coprocessor_error do_coprocessor_error
errorentry alignment_check do_alignment_check
zeroentry simd_coprocessor_error do_simd_coprocessor_error

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

	.section __ex_table,"a"
	.align 8
	.quad gs_change,bad_gs
	.previous
	.section .fixup,"ax"
	/* running with kernelgs */
bad_gs:
	SWAPGS			/* switch back to user gs */
	xorl %eax,%eax
	movl %eax,%gs
	jmp  2b
	.previous

ENTRY(kernel_thread_helper)
	pushq $0		# fake return address
	CFI_STARTPROC
	/*
	 * Here we are in the child and the registers are set as they were
	 * at kernel_thread() invocation in the parent.
	 */
	call *%rsi
	# exit
	mov %eax, %edi
	call do_exit
	ud2			# padding for call trace
	CFI_ENDPROC
END(kernel_thread_helper)

/*
 * execve(). This function needs to use IRET, not SYSRET, to set up all state properly.
 *
 * C extern interface:
 *	 extern long execve(const char *name, char **argv, char **envp)
 *
 * asm input arguments:
 *	rdi: name, rsi: argv, rdx: envp
 *
 * We want to fallback into:
 *	extern long sys_execve(const char *name, char **argv,char **envp, struct pt_regs *regs)
 *
 * do_sys_execve asm fallback arguments:
 *	rdi: name, rsi: argv, rdx: envp, rcx: fake frame on the stack
 */
ENTRY(kernel_execve)
	CFI_STARTPROC
	FAKE_STACK_FRAME $0
	SAVE_ALL
	movq %rsp,%rcx
	call sys_execve
	movq %rax, RAX(%rsp)
	RESTORE_REST
	testq %rax,%rax
	je int_ret_from_sys_call
	RESTORE_ARGS
	UNFAKE_STACK_FRAME
	ret
	CFI_ENDPROC
END(kernel_execve)

/* Call softirq on interrupt stack. Interrupts are off. */
ENTRY(call_softirq)
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
END(call_softirq)

#ifdef CONFIG_XEN
zeroentry xen_hypervisor_callback xen_do_hypervisor_callback

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
	pushq_cfi $0
	SAVE_ALL
	jmp error_exit
	CFI_ENDPROC
END(xen_failsafe_callback)

apicinterrupt XEN_HVM_EVTCHN_CALLBACK \
	xen_hvm_callback_vector xen_evtchn_do_upcall

#endif /* CONFIG_XEN */

/*
 * Some functions should be protected against kprobes
 */
	.pushsection .kprobes.text, "ax"

paranoidzeroentry_ist debug do_debug DEBUG_STACK
paranoidzeroentry_ist int3 do_int3 DEBUG_STACK
paranoiderrorentry stack_segment do_stack_segment
#ifdef CONFIG_XEN
zeroentry xen_debug do_debug
zeroentry xen_int3 do_int3
errorentry xen_stack_segment do_stack_segment
#endif
errorentry general_protection do_general_protection
errorentry page_fault do_page_fault
#ifdef CONFIG_KVM_GUEST
errorentry async_page_fault do_async_page_fault
#endif
#ifdef CONFIG_X86_MCE
paranoidzeroentry machine_check *machine_check_vector(%rip)
#endif

	/*
	 * "Paranoid" exit path from exception stack.
	 * Paranoid because this is used by NMIs and cannot take
	 * any kernel state for granted.
	 * We don't do kernel preemption checks here, because only
	 * NMI should be common and it does not enable IRQs and
	 * cannot get reschedule ticks.
	 *
	 * "trace" is 0 for the NMI handler only, because irq-tracing
	 * is fundamentally NMI-unsafe. (we cannot change the soft and
	 * hard flags at once, atomically)
	 */

	/* ebx:	no swapgs flag */
ENTRY(paranoid_exit)
	DEFAULT_FRAME
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	testl %ebx,%ebx				/* swapgs needed? */
	jnz paranoid_restore
	testl $3,CS(%rsp)
	jnz   paranoid_userspace
paranoid_swapgs:
	TRACE_IRQS_IRETQ 0
	SWAPGS_UNSAFE_STACK
	RESTORE_ALL 8
	jmp irq_return
paranoid_restore:
	TRACE_IRQS_IRETQ 0
	RESTORE_ALL 8
	jmp irq_return
paranoid_userspace:
	GET_THREAD_INFO(%rcx)
	movl TI_flags(%rcx),%ebx
	andl $_TIF_WORK_MASK,%ebx
	jz paranoid_swapgs
	movq %rsp,%rdi			/* &pt_regs */
	call sync_regs
	movq %rax,%rsp			/* switch stack for scheduling */
	testl $_TIF_NEED_RESCHED,%ebx
	jnz paranoid_schedule
	movl %ebx,%edx			/* arg3: thread flags */
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	xorl %esi,%esi 			/* arg2: oldset */
	movq %rsp,%rdi 			/* arg1: &pt_regs */
	call do_notify_resume
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp paranoid_userspace
paranoid_schedule:
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_ANY)
	call schedule
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF
	jmp paranoid_userspace
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
	movq_cfi rdi, RDI+8
	movq_cfi rsi, RSI+8
	movq_cfi rdx, RDX+8
	movq_cfi rcx, RCX+8
	movq_cfi rax, RAX+8
	movq_cfi  r8,  R8+8
	movq_cfi  r9,  R9+8
	movq_cfi r10, R10+8
	movq_cfi r11, R11+8
	movq_cfi rbx, RBX+8
	movq_cfi rbp, RBP+8
	movq_cfi r12, R12+8
	movq_cfi r13, R13+8
	movq_cfi r14, R14+8
	movq_cfi r15, R15+8
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
 * usergs. Handle them here. The exception handlers after iret run with
 * kernel gs again, so don't set the user space flag. B stepping K8s
 * sometimes report an truncated RIP for IRET exceptions returning to
 * compat mode. Check for these here too.
 */
error_kernelspace:
	incl %ebx
	leaq irq_return(%rip),%rcx
	cmpq %rcx,RIP+8(%rsp)
	je error_swapgs
	movl %ecx,%eax	/* zero extend */
	cmpq %rax,RIP+8(%rsp)
	je bstep_iret
	cmpq $gs_change,RIP+8(%rsp)
	je error_swapgs
	jmp error_sti

bstep_iret:
	/* Fix truncated RIP */
	movq %rcx,RIP+8(%rsp)
	jmp error_swapgs
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


	/* runs on exception stack */
ENTRY(nmi)
	INTR_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	pushq_cfi $-1
	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15
	call save_paranoid
	DEFAULT_FRAME 0
	/* paranoidentry do_nmi, 0; without TRACE_IRQS_OFF */
	movq %rsp,%rdi
	movq $-1,%rsi
	call do_nmi
#ifdef CONFIG_TRACE_IRQFLAGS
	/* paranoidexit; without TRACE_IRQS_OFF */
	/* ebx:	no swapgs flag */
	DISABLE_INTERRUPTS(CLBR_NONE)
	testl %ebx,%ebx				/* swapgs needed? */
	jnz nmi_restore
	testl $3,CS(%rsp)
	jnz nmi_userspace
nmi_swapgs:
	SWAPGS_UNSAFE_STACK
nmi_restore:
	RESTORE_ALL 8
	jmp irq_return
nmi_userspace:
	GET_THREAD_INFO(%rcx)
	movl TI_flags(%rcx),%ebx
	andl $_TIF_WORK_MASK,%ebx
	jz nmi_swapgs
	movq %rsp,%rdi			/* &pt_regs */
	call sync_regs
	movq %rax,%rsp			/* switch stack for scheduling */
	testl $_TIF_NEED_RESCHED,%ebx
	jnz nmi_schedule
	movl %ebx,%edx			/* arg3: thread flags */
	ENABLE_INTERRUPTS(CLBR_NONE)
	xorl %esi,%esi 			/* arg2: oldset */
	movq %rsp,%rdi 			/* arg1: &pt_regs */
	call do_notify_resume
	DISABLE_INTERRUPTS(CLBR_NONE)
	jmp nmi_userspace
nmi_schedule:
	ENABLE_INTERRUPTS(CLBR_ANY)
	call schedule
	DISABLE_INTERRUPTS(CLBR_ANY)
	jmp nmi_userspace
	CFI_ENDPROC
#else
	jmp paranoid_exit
	CFI_ENDPROC
#endif
END(nmi)

ENTRY(ignore_sysret)
	CFI_STARTPROC
	mov $-ENOSYS,%eax
	sysret
	CFI_ENDPROC
END(ignore_sysret)

/*
 * End of kprobes section
 */
	.popsection
