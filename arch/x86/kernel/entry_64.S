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
 * - partial stack frame: partially saved registers upto R11.
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
#include <asm/page.h>
#include <asm/irqflags.h>
#include <asm/paravirt.h>

	.code64

#ifndef CONFIG_PREEMPT
#define retint_kernel retint_restore_args
#endif	

#ifdef CONFIG_PARAVIRT
ENTRY(native_irq_enable_syscall_ret)
	movq	%gs:pda_oldrsp,%rsp
	swapgs
	sysretq
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
	.macro FIXUP_TOP_OF_STACK tmp
	movq	%gs:pda_oldrsp,\tmp
	movq  	\tmp,RSP(%rsp)
	movq    $__USER_DS,SS(%rsp)
	movq    $__USER_CS,CS(%rsp)
	movq 	$-1,RCX(%rsp)
	movq	R11(%rsp),\tmp  /* get eflags */
	movq	\tmp,EFLAGS(%rsp)
	.endm

	.macro RESTORE_TOP_OF_STACK tmp,offset=0
	movq   RSP-\offset(%rsp),\tmp
	movq   \tmp,%gs:pda_oldrsp
	movq   EFLAGS-\offset(%rsp),\tmp
	movq   \tmp,R11-\offset(%rsp)
	.endm

	.macro FAKE_STACK_FRAME child_rip
	/* push in order ss, rsp, eflags, cs, rip */
	xorl %eax, %eax
	pushq %rax /* ss */
	CFI_ADJUST_CFA_OFFSET	8
	/*CFI_REL_OFFSET	ss,0*/
	pushq %rax /* rsp */
	CFI_ADJUST_CFA_OFFSET	8
	CFI_REL_OFFSET	rsp,0
	pushq $(1<<9) /* eflags - interrupts on */
	CFI_ADJUST_CFA_OFFSET	8
	/*CFI_REL_OFFSET	rflags,0*/
	pushq $__KERNEL_CS /* cs */
	CFI_ADJUST_CFA_OFFSET	8
	/*CFI_REL_OFFSET	cs,0*/
	pushq \child_rip /* rip */
	CFI_ADJUST_CFA_OFFSET	8
	CFI_REL_OFFSET	rip,0
	pushq	%rax /* orig rax */
	CFI_ADJUST_CFA_OFFSET	8
	.endm

	.macro UNFAKE_STACK_FRAME
	addq $8*6, %rsp
	CFI_ADJUST_CFA_OFFSET	-(6*8)
	.endm

	.macro	CFI_DEFAULT_STACK start=1
	.if \start
	CFI_STARTPROC	simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA	rsp,SS+8
	.else
	CFI_DEF_CFA_OFFSET SS+8
	.endif
	CFI_REL_OFFSET	r15,R15
	CFI_REL_OFFSET	r14,R14
	CFI_REL_OFFSET	r13,R13
	CFI_REL_OFFSET	r12,R12
	CFI_REL_OFFSET	rbp,RBP
	CFI_REL_OFFSET	rbx,RBX
	CFI_REL_OFFSET	r11,R11
	CFI_REL_OFFSET	r10,R10
	CFI_REL_OFFSET	r9,R9
	CFI_REL_OFFSET	r8,R8
	CFI_REL_OFFSET	rax,RAX
	CFI_REL_OFFSET	rcx,RCX
	CFI_REL_OFFSET	rdx,RDX
	CFI_REL_OFFSET	rsi,RSI
	CFI_REL_OFFSET	rdi,RDI
	CFI_REL_OFFSET	rip,RIP
	/*CFI_REL_OFFSET	cs,CS*/
	/*CFI_REL_OFFSET	rflags,EFLAGS*/
	CFI_REL_OFFSET	rsp,RSP
	/*CFI_REL_OFFSET	ss,SS*/
	.endm
/*
 * A newly forked process directly context switches into this.
 */ 	
/* rdi:	prev */	
ENTRY(ret_from_fork)
	CFI_DEFAULT_STACK
	push kernel_eflags(%rip)
	CFI_ADJUST_CFA_OFFSET 4
	popf				# reset kernel eflags
	CFI_ADJUST_CFA_OFFSET -4
	call schedule_tail
	GET_THREAD_INFO(%rcx)
	testl $(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT),threadinfo_flags(%rcx)
	jnz rff_trace
rff_action:	
	RESTORE_REST
	testl $3,CS-ARGOFFSET(%rsp)	# from kernel_thread?
	je   int_ret_from_sys_call
	testl $_TIF_IA32,threadinfo_flags(%rcx)
	jnz  int_ret_from_sys_call
	RESTORE_TOP_OF_STACK %rdi,ARGOFFSET
	jmp ret_from_sys_call
rff_trace:
	movq %rsp,%rdi
	call syscall_trace_leave
	GET_THREAD_INFO(%rcx)	
	jmp rff_action
	CFI_ENDPROC
END(ret_from_fork)

/*
 * System call entry. Upto 6 arguments in registers are supported.
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
	CFI_DEF_CFA	rsp,PDA_STACKOFFSET
	CFI_REGISTER	rip,rcx
	/*CFI_REGISTER	rflags,r11*/
	SWAPGS_UNSAFE_STACK
	/*
	 * A hypervisor implementation might want to use a label
	 * after the swapgs, so that it can do the swapgs
	 * for the guest and jump here on syscall.
	 */
ENTRY(system_call_after_swapgs)

	movq	%rsp,%gs:pda_oldrsp 
	movq	%gs:pda_kernelstack,%rsp
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
	testl $(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT|_TIF_SECCOMP),threadinfo_flags(%rcx)
	jnz tracesys
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
	movl threadinfo_flags(%rcx),%edx
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
	ENABLE_INTERRUPTS_SYSCALL_RET

	CFI_RESTORE_STATE
	/* Handle reschedules */
	/* edx:	work, edi: workmask */	
sysret_careful:
	bt $TIF_NEED_RESCHED,%edx
	jnc sysret_signal
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushq %rdi
	CFI_ADJUST_CFA_OFFSET 8
	call schedule
	popq  %rdi
	CFI_ADJUST_CFA_OFFSET -8
	jmp sysret_check

	/* Handle a signal */ 
sysret_signal:
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	testl $_TIF_DO_NOTIFY_MASK,%edx
	jz    1f

	/* Really a signal */
	/* edx:	work flags (arg3) */
	leaq do_notify_resume(%rip),%rax
	leaq -ARGOFFSET(%rsp),%rdi # &pt_regs -> arg1
	xorl %esi,%esi # oldset -> arg2
	call ptregscall_common
1:	movl $_TIF_NEED_RESCHED,%edi
	/* Use IRET because user could have changed frame. This
	   works because ptregscall_common has called FIXUP_TOP_OF_STACK. */
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp int_with_check
	
badsys:
	movq $-ENOSYS,RAX-ARGOFFSET(%rsp)
	jmp ret_from_sys_call

	/* Do syscall tracing */
tracesys:			 
	SAVE_REST
	movq $-ENOSYS,RAX(%rsp)
	FIXUP_TOP_OF_STACK %rdi
	movq %rsp,%rdi
	call syscall_trace_enter
	LOAD_ARGS ARGOFFSET  /* reload args from stack in case ptrace changed it */
	RESTORE_REST
	cmpq $__NR_syscall_max,%rax
	movq $-ENOSYS,%rcx
	cmova %rcx,%rax
	ja  1f
	movq %r10,%rcx	/* fixup for C */
	call *sys_call_table(,%rax,8)
1:	movq %rax,RAX-ARGOFFSET(%rsp)
	/* Use IRET because user could have changed frame */
		
/* 
 * Syscall return path ending with IRET.
 * Has correct top of stack, but partial stack frame.
 */
	.globl int_ret_from_sys_call
int_ret_from_sys_call:
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	testl $3,CS-ARGOFFSET(%rsp)
	je retint_restore_args
	movl $_TIF_ALLWORK_MASK,%edi
	/* edi:	mask to check */
int_with_check:
	LOCKDEP_SYS_EXIT_IRQ
	GET_THREAD_INFO(%rcx)
	movl threadinfo_flags(%rcx),%edx
	andl %edi,%edx
	jnz   int_careful
	andl    $~TS_COMPAT,threadinfo_status(%rcx)
	jmp   retint_swapgs

	/* Either reschedule or signal or syscall exit tracking needed. */
	/* First do a reschedule test. */
	/* edx:	work, edi: workmask */
int_careful:
	bt $TIF_NEED_RESCHED,%edx
	jnc  int_very_careful
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushq %rdi
	CFI_ADJUST_CFA_OFFSET 8
	call schedule
	popq %rdi
	CFI_ADJUST_CFA_OFFSET -8
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp int_with_check

	/* handle signals and tracing -- both require a full stack frame */
int_very_careful:
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	SAVE_REST
	/* Check for syscall exit trace */	
	testl $(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT|_TIF_SINGLESTEP),%edx
	jz int_signal
	pushq %rdi
	CFI_ADJUST_CFA_OFFSET 8
	leaq 8(%rsp),%rdi	# &ptregs -> arg1	
	call syscall_trace_leave
	popq %rdi
	CFI_ADJUST_CFA_OFFSET -8
	andl $~(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT|_TIF_SINGLESTEP),%edi
	jmp int_restore_rest
	
int_signal:
	testl $_TIF_DO_NOTIFY_MASK,%edx
	jz 1f
	movq %rsp,%rdi		# &ptregs -> arg1
	xorl %esi,%esi		# oldset -> arg2
	call do_notify_resume
1:	movl $_TIF_NEED_RESCHED,%edi	
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
	.globl \label
\label:
	leaq	\func(%rip),%rax
	leaq    -ARGOFFSET+8(%rsp),\arg /* 8 for return address */
	jmp	ptregscall_common
END(\label)
	.endm

	CFI_STARTPROC

	PTREGSCALL stub_clone, sys_clone, %r8
	PTREGSCALL stub_fork, sys_fork, %rdi
	PTREGSCALL stub_vfork, sys_vfork, %rdi
	PTREGSCALL stub_rt_sigsuspend, sys_rt_sigsuspend, %rdx
	PTREGSCALL stub_sigaltstack, sys_sigaltstack, %rdx
	PTREGSCALL stub_iopl, sys_iopl, %rsi

ENTRY(ptregscall_common)
	popq %r11
	CFI_ADJUST_CFA_OFFSET -8
	CFI_REGISTER rip, r11
	SAVE_REST
	movq %r11, %r15
	CFI_REGISTER rip, r15
	FIXUP_TOP_OF_STACK %r11
	call *%rax
	RESTORE_TOP_OF_STACK %r11
	movq %r15, %r11
	CFI_REGISTER rip, r11
	RESTORE_REST
	pushq %r11
	CFI_ADJUST_CFA_OFFSET 8
	CFI_REL_OFFSET rip, 0
	ret
	CFI_ENDPROC
END(ptregscall_common)
	
ENTRY(stub_execve)
	CFI_STARTPROC
	popq %r11
	CFI_ADJUST_CFA_OFFSET -8
	CFI_REGISTER rip, r11
	SAVE_REST
	FIXUP_TOP_OF_STACK %r11
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
	CFI_ADJUST_CFA_OFFSET	-8
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
 * initial frame state for interrupts and exceptions
 */
	.macro _frame ref
	CFI_STARTPROC simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA rsp,SS+8-\ref
	/*CFI_REL_OFFSET ss,SS-\ref*/
	CFI_REL_OFFSET rsp,RSP-\ref
	/*CFI_REL_OFFSET rflags,EFLAGS-\ref*/
	/*CFI_REL_OFFSET cs,CS-\ref*/
	CFI_REL_OFFSET rip,RIP-\ref
	.endm

/* initial frame state for interrupts (and exceptions without error code) */
#define INTR_FRAME _frame RIP
/* initial frame state for exceptions with error code (and interrupts with
   vector already pushed) */
#define XCPT_FRAME _frame ORIG_RAX

/* 
 * Interrupt entry/exit.
 *
 * Interrupt entry points save only callee clobbered registers in fast path.
 *	
 * Entry runs with interrupts off.	
 */ 

/* 0(%rsp): interrupt number */ 
	.macro interrupt func
	cld
	SAVE_ARGS
	leaq -ARGOFFSET(%rsp),%rdi	# arg1 for handler
	pushq %rbp
	CFI_ADJUST_CFA_OFFSET	8
	CFI_REL_OFFSET		rbp, 0
	movq %rsp,%rbp
	CFI_DEF_CFA_REGISTER	rbp
	testl $3,CS(%rdi)
	je 1f
	SWAPGS
	/* irqcount is used to check if a CPU is already on an interrupt
	   stack or not. While this is essentially redundant with preempt_count
	   it is a little cheaper to use a separate counter in the PDA
	   (short of moving irq_enter into assembly, which would be too
	    much work) */
1:	incl	%gs:pda_irqcount
	cmoveq %gs:pda_irqstackptr,%rsp
	push    %rbp			# backlink for old unwinder
	/*
	 * We entered an interrupt context - irqs are off:
	 */
	TRACE_IRQS_OFF
	call \func
	.endm

ENTRY(common_interrupt)
	XCPT_FRAME
	interrupt do_IRQ
	/* 0(%rsp): oldrsp-ARGOFFSET */
ret_from_intr:
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	decl %gs:pda_irqcount
	leaveq
	CFI_DEF_CFA_REGISTER	rsp
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
	movl threadinfo_flags(%rcx),%edx
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
#ifdef CONFIG_PARAVIRT
	INTERRUPT_RETURN
#endif
ENTRY(native_iret)
	iretq

	.section __ex_table,"a"
	.quad native_iret, bad_iret
	.previous
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
	pushq %rdi
	CFI_ADJUST_CFA_OFFSET	8
	call  schedule
	popq %rdi		
	CFI_ADJUST_CFA_OFFSET	-8
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
	movl $_TIF_NEED_RESCHED,%edi
	GET_THREAD_INFO(%rcx)
	jmp retint_check

#ifdef CONFIG_PREEMPT
	/* Returning to kernel space. Check if we need preemption */
	/* rcx:	 threadinfo. interrupts off. */
ENTRY(retint_kernel)
	cmpl $0,threadinfo_preempt_count(%rcx)
	jnz  retint_restore_args
	bt  $TIF_NEED_RESCHED,threadinfo_flags(%rcx)
	jnc  retint_restore_args
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
	.macro apicinterrupt num,func
	INTR_FRAME
	pushq $~(\num)
	CFI_ADJUST_CFA_OFFSET 8
	interrupt \func
	jmp ret_from_intr
	CFI_ENDPROC
	.endm

ENTRY(thermal_interrupt)
	apicinterrupt THERMAL_APIC_VECTOR,smp_thermal_interrupt
END(thermal_interrupt)

ENTRY(threshold_interrupt)
	apicinterrupt THRESHOLD_APIC_VECTOR,mce_threshold_interrupt
END(threshold_interrupt)

#ifdef CONFIG_SMP	
ENTRY(reschedule_interrupt)
	apicinterrupt RESCHEDULE_VECTOR,smp_reschedule_interrupt
END(reschedule_interrupt)

	.macro INVALIDATE_ENTRY num
ENTRY(invalidate_interrupt\num)
	apicinterrupt INVALIDATE_TLB_VECTOR_START+\num,smp_invalidate_interrupt	
END(invalidate_interrupt\num)
	.endm

	INVALIDATE_ENTRY 0
	INVALIDATE_ENTRY 1
	INVALIDATE_ENTRY 2
	INVALIDATE_ENTRY 3
	INVALIDATE_ENTRY 4
	INVALIDATE_ENTRY 5
	INVALIDATE_ENTRY 6
	INVALIDATE_ENTRY 7

ENTRY(call_function_interrupt)
	apicinterrupt CALL_FUNCTION_VECTOR,smp_call_function_interrupt
END(call_function_interrupt)
ENTRY(irq_move_cleanup_interrupt)
	apicinterrupt IRQ_MOVE_CLEANUP_VECTOR,smp_irq_move_cleanup_interrupt
END(irq_move_cleanup_interrupt)
#endif

ENTRY(apic_timer_interrupt)
	apicinterrupt LOCAL_TIMER_VECTOR,smp_apic_timer_interrupt
END(apic_timer_interrupt)

ENTRY(error_interrupt)
	apicinterrupt ERROR_APIC_VECTOR,smp_error_interrupt
END(error_interrupt)

ENTRY(spurious_interrupt)
	apicinterrupt SPURIOUS_APIC_VECTOR,smp_spurious_interrupt
END(spurious_interrupt)
				
/*
 * Exception entry points.
 */ 		
	.macro zeroentry sym
	INTR_FRAME
	pushq $0	/* push error code/oldrax */ 
	CFI_ADJUST_CFA_OFFSET 8
	pushq %rax	/* push real oldrax to the rdi slot */ 
	CFI_ADJUST_CFA_OFFSET 8
	CFI_REL_OFFSET rax,0
	leaq  \sym(%rip),%rax
	jmp error_entry
	CFI_ENDPROC
	.endm	

	.macro errorentry sym
	XCPT_FRAME
	pushq %rax
	CFI_ADJUST_CFA_OFFSET 8
	CFI_REL_OFFSET rax,0
	leaq  \sym(%rip),%rax
	jmp error_entry
	CFI_ENDPROC
	.endm

	/* error code is on the stack already */
	/* handle NMI like exceptions that can happen everywhere */
	.macro paranoidentry sym, ist=0, irqtrace=1
	SAVE_ALL
	cld
	movl $1,%ebx
	movl  $MSR_GS_BASE,%ecx
	rdmsr
	testl %edx,%edx
	js    1f
	SWAPGS
	xorl  %ebx,%ebx
1:
	.if \ist
	movq	%gs:pda_data_offset, %rbp
	.endif
	movq %rsp,%rdi
	movq ORIG_RAX(%rsp),%rsi
	movq $-1,ORIG_RAX(%rsp)
	.if \ist
	subq	$EXCEPTION_STKSZ, per_cpu__init_tss + TSS_ist + (\ist - 1) * 8(%rbp)
	.endif
	call \sym
	.if \ist
	addq	$EXCEPTION_STKSZ, per_cpu__init_tss + TSS_ist + (\ist - 1) * 8(%rbp)
	.endif
	DISABLE_INTERRUPTS(CLBR_NONE)
	.if \irqtrace
	TRACE_IRQS_OFF
	.endif
	.endm

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
	.macro paranoidexit trace=1
	/* ebx:	no swapgs flag */
paranoid_exit\trace:
	testl %ebx,%ebx				/* swapgs needed? */
	jnz paranoid_restore\trace
	testl $3,CS(%rsp)
	jnz   paranoid_userspace\trace
paranoid_swapgs\trace:
	.if \trace
	TRACE_IRQS_IRETQ 0
	.endif
	SWAPGS_UNSAFE_STACK
paranoid_restore\trace:
	RESTORE_ALL 8
	INTERRUPT_RETURN
paranoid_userspace\trace:
	GET_THREAD_INFO(%rcx)
	movl threadinfo_flags(%rcx),%ebx
	andl $_TIF_WORK_MASK,%ebx
	jz paranoid_swapgs\trace
	movq %rsp,%rdi			/* &pt_regs */
	call sync_regs
	movq %rax,%rsp			/* switch stack for scheduling */
	testl $_TIF_NEED_RESCHED,%ebx
	jnz paranoid_schedule\trace
	movl %ebx,%edx			/* arg3: thread flags */
	.if \trace
	TRACE_IRQS_ON
	.endif
	ENABLE_INTERRUPTS(CLBR_NONE)
	xorl %esi,%esi 			/* arg2: oldset */
	movq %rsp,%rdi 			/* arg1: &pt_regs */
	call do_notify_resume
	DISABLE_INTERRUPTS(CLBR_NONE)
	.if \trace
	TRACE_IRQS_OFF
	.endif
	jmp paranoid_userspace\trace
paranoid_schedule\trace:
	.if \trace
	TRACE_IRQS_ON
	.endif
	ENABLE_INTERRUPTS(CLBR_ANY)
	call schedule
	DISABLE_INTERRUPTS(CLBR_ANY)
	.if \trace
	TRACE_IRQS_OFF
	.endif
	jmp paranoid_userspace\trace
	CFI_ENDPROC
	.endm

/*
 * Exception entry point. This expects an error code/orig_rax on the stack
 * and the exception handler in %rax.	
 */ 		  				
KPROBE_ENTRY(error_entry)
	_frame RDI
	CFI_REL_OFFSET rax,0
	/* rdi slot contains rax, oldrax contains error code */
	cld	
	subq  $14*8,%rsp
	CFI_ADJUST_CFA_OFFSET	(14*8)
	movq %rsi,13*8(%rsp)
	CFI_REL_OFFSET	rsi,RSI
	movq 14*8(%rsp),%rsi	/* load rax from rdi slot */
	CFI_REGISTER	rax,rsi
	movq %rdx,12*8(%rsp)
	CFI_REL_OFFSET	rdx,RDX
	movq %rcx,11*8(%rsp)
	CFI_REL_OFFSET	rcx,RCX
	movq %rsi,10*8(%rsp)	/* store rax */ 
	CFI_REL_OFFSET	rax,RAX
	movq %r8, 9*8(%rsp)
	CFI_REL_OFFSET	r8,R8
	movq %r9, 8*8(%rsp)
	CFI_REL_OFFSET	r9,R9
	movq %r10,7*8(%rsp)
	CFI_REL_OFFSET	r10,R10
	movq %r11,6*8(%rsp)
	CFI_REL_OFFSET	r11,R11
	movq %rbx,5*8(%rsp) 
	CFI_REL_OFFSET	rbx,RBX
	movq %rbp,4*8(%rsp) 
	CFI_REL_OFFSET	rbp,RBP
	movq %r12,3*8(%rsp) 
	CFI_REL_OFFSET	r12,R12
	movq %r13,2*8(%rsp) 
	CFI_REL_OFFSET	r13,R13
	movq %r14,1*8(%rsp) 
	CFI_REL_OFFSET	r14,R14
	movq %r15,(%rsp) 
	CFI_REL_OFFSET	r15,R15
	xorl %ebx,%ebx	
	testl $3,CS(%rsp)
	je  error_kernelspace
error_swapgs:	
	SWAPGS
error_sti:	
	movq %rdi,RDI(%rsp) 	
	CFI_REL_OFFSET	rdi,RDI
	movq %rsp,%rdi
	movq ORIG_RAX(%rsp),%rsi	/* get error code */ 
	movq $-1,ORIG_RAX(%rsp)
	call *%rax
	/* ebx:	no swapgs flag (1: don't need swapgs, 0: need it) */
error_exit:
	movl %ebx,%eax
	RESTORE_REST
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	GET_THREAD_INFO(%rcx)	
	testl %eax,%eax
	jne  retint_kernel
	LOCKDEP_SYS_EXIT_IRQ
	movl  threadinfo_flags(%rcx),%edx
	movl  $_TIF_WORK_MASK,%edi
	andl  %edi,%edx
	jnz  retint_careful
	jmp retint_swapgs
	CFI_ENDPROC

error_kernelspace:
	incl %ebx
       /* There are two places in the kernel that can potentially fault with
          usergs. Handle them here. The exception handlers after
	   iret run with kernel gs again, so don't set the user space flag.
	   B stepping K8s sometimes report an truncated RIP for IRET 
	   exceptions returning to compat mode. Check for these here too. */
	leaq native_iret(%rip),%rbp
	cmpq %rbp,RIP(%rsp) 
	je   error_swapgs
	movl %ebp,%ebp	/* zero extend */
	cmpq %rbp,RIP(%rsp) 
	je   error_swapgs
	cmpq $gs_change,RIP(%rsp)
        je   error_swapgs
	jmp  error_sti
KPROBE_END(error_entry)
	
       /* Reload gs selector with exception handling */
       /* edi:  new selector */ 
ENTRY(load_gs_index)
	CFI_STARTPROC
	pushf
	CFI_ADJUST_CFA_OFFSET 8
	DISABLE_INTERRUPTS(CLBR_ANY | ~(CLBR_RDI))
        SWAPGS
gs_change:     
        movl %edi,%gs   
2:	mfence		/* workaround */
	SWAPGS
        popf
	CFI_ADJUST_CFA_OFFSET -8
        ret
	CFI_ENDPROC
ENDPROC(load_gs_index)
       
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
	
/*
 * Create a kernel thread.
 *
 * C extern interface:
 *	extern long kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
 *
 * asm input arguments:
 *	rdi: fn, rsi: arg, rdx: flags
 */
ENTRY(kernel_thread)
	CFI_STARTPROC
	FAKE_STACK_FRAME $child_rip
	SAVE_ALL

	# rdi: flags, rsi: usp, rdx: will be &pt_regs
	movq %rdx,%rdi
	orq  kernel_thread_flags(%rip),%rdi
	movq $-1, %rsi
	movq %rsp, %rdx

	xorl %r8d,%r8d
	xorl %r9d,%r9d
	
	# clone now
	call do_fork
	movq %rax,RAX(%rsp)
	xorl %edi,%edi

	/*
	 * It isn't worth to check for reschedule here,
	 * so internally to the x86_64 port you can rely on kernel_thread()
	 * not to reschedule the child before returning, this avoids the need
	 * of hacks for example to fork off the per-CPU idle tasks.
         * [Hopefully no generic code relies on the reschedule -AK]	
	 */
	RESTORE_ALL
	UNFAKE_STACK_FRAME
	ret
	CFI_ENDPROC
ENDPROC(kernel_thread)
	
child_rip:
	pushq $0		# fake return address
	CFI_STARTPROC
	/*
	 * Here we are in the child and the registers are set as they were
	 * at kernel_thread() invocation in the parent.
	 */
	movq %rdi, %rax
	movq %rsi, %rdi
	call *%rax
	# exit
	mov %eax, %edi
	call do_exit
	CFI_ENDPROC
ENDPROC(child_rip)

/*
 * execve(). This function needs to use IRET, not SYSRET, to set up all state properly.
 *
 * C extern interface:
 *	 extern long execve(char *name, char **argv, char **envp)
 *
 * asm input arguments:
 *	rdi: name, rsi: argv, rdx: envp
 *
 * We want to fallback into:
 *	extern long sys_execve(char *name, char **argv,char **envp, struct pt_regs regs)
 *
 * do_sys_execve asm fallback arguments:
 *	rdi: name, rsi: argv, rdx: envp, fake frame on the stack
 */
ENTRY(kernel_execve)
	CFI_STARTPROC
	FAKE_STACK_FRAME $0
	SAVE_ALL	
	call sys_execve
	movq %rax, RAX(%rsp)	
	RESTORE_REST
	testq %rax,%rax
	je int_ret_from_sys_call
	RESTORE_ARGS
	UNFAKE_STACK_FRAME
	ret
	CFI_ENDPROC
ENDPROC(kernel_execve)

KPROBE_ENTRY(page_fault)
	errorentry do_page_fault
KPROBE_END(page_fault)

ENTRY(coprocessor_error)
	zeroentry do_coprocessor_error
END(coprocessor_error)

ENTRY(simd_coprocessor_error)
	zeroentry do_simd_coprocessor_error	
END(simd_coprocessor_error)

ENTRY(device_not_available)
	zeroentry math_state_restore
END(device_not_available)

	/* runs on exception stack */
KPROBE_ENTRY(debug)
 	INTR_FRAME
	pushq $0
	CFI_ADJUST_CFA_OFFSET 8		
	paranoidentry do_debug, DEBUG_STACK
	paranoidexit
KPROBE_END(debug)

	/* runs on exception stack */	
KPROBE_ENTRY(nmi)
	INTR_FRAME
	pushq $-1
	CFI_ADJUST_CFA_OFFSET 8
	paranoidentry do_nmi, 0, 0
#ifdef CONFIG_TRACE_IRQFLAGS
	paranoidexit 0
#else
	jmp paranoid_exit1
 	CFI_ENDPROC
#endif
KPROBE_END(nmi)

KPROBE_ENTRY(int3)
 	INTR_FRAME
 	pushq $0
 	CFI_ADJUST_CFA_OFFSET 8
 	paranoidentry do_int3, DEBUG_STACK
 	jmp paranoid_exit1
 	CFI_ENDPROC
KPROBE_END(int3)

ENTRY(overflow)
	zeroentry do_overflow
END(overflow)

ENTRY(bounds)
	zeroentry do_bounds
END(bounds)

ENTRY(invalid_op)
	zeroentry do_invalid_op	
END(invalid_op)

ENTRY(coprocessor_segment_overrun)
	zeroentry do_coprocessor_segment_overrun
END(coprocessor_segment_overrun)

ENTRY(reserved)
	zeroentry do_reserved
END(reserved)

	/* runs on exception stack */
ENTRY(double_fault)
	XCPT_FRAME
	paranoidentry do_double_fault
	jmp paranoid_exit1
	CFI_ENDPROC
END(double_fault)

ENTRY(invalid_TSS)
	errorentry do_invalid_TSS
END(invalid_TSS)

ENTRY(segment_not_present)
	errorentry do_segment_not_present
END(segment_not_present)

	/* runs on exception stack */
ENTRY(stack_segment)
	XCPT_FRAME
	paranoidentry do_stack_segment
	jmp paranoid_exit1
	CFI_ENDPROC
END(stack_segment)

KPROBE_ENTRY(general_protection)
	errorentry do_general_protection
KPROBE_END(general_protection)

ENTRY(alignment_check)
	errorentry do_alignment_check
END(alignment_check)

ENTRY(divide_error)
	zeroentry do_divide_error
END(divide_error)

ENTRY(spurious_interrupt_bug)
	zeroentry do_spurious_interrupt_bug
END(spurious_interrupt_bug)

#ifdef CONFIG_X86_MCE
	/* runs on exception stack */
ENTRY(machine_check)
	INTR_FRAME
	pushq $0
	CFI_ADJUST_CFA_OFFSET 8	
	paranoidentry do_machine_check
	jmp paranoid_exit1
	CFI_ENDPROC
END(machine_check)
#endif

/* Call softirq on interrupt stack. Interrupts are off. */
ENTRY(call_softirq)
	CFI_STARTPROC
	push %rbp
	CFI_ADJUST_CFA_OFFSET	8
	CFI_REL_OFFSET rbp,0
	mov  %rsp,%rbp
	CFI_DEF_CFA_REGISTER rbp
	incl %gs:pda_irqcount
	cmove %gs:pda_irqstackptr,%rsp
	push  %rbp			# backlink for old unwinder
	call __do_softirq
	leaveq
	CFI_DEF_CFA_REGISTER	rsp
	CFI_ADJUST_CFA_OFFSET   -8
	decl %gs:pda_irqcount
	ret
	CFI_ENDPROC
ENDPROC(call_softirq)

KPROBE_ENTRY(ignore_sysret)
	CFI_STARTPROC
	mov $-ENOSYS,%eax
	sysret
	CFI_ENDPROC
ENDPROC(ignore_sysret)
