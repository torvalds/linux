/*
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * entry.S contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 * I changed all the .align's to 4 (16 byte alignment), as that's faster
 * on a 486.
 *
 * Stack layout in 'syscall_exit':
 * 	ptrace needs to have all regs on the stack.
 *	if the order here is changed, it needs to be
 *	updated in fork.c:copy_process, signal.c:do_signal,
 *	ptrace.c and ptrace.h
 *
 *	 0(%esp) - %ebx
 *	 4(%esp) - %ecx
 *	 8(%esp) - %edx
 *       C(%esp) - %esi
 *	10(%esp) - %edi
 *	14(%esp) - %ebp
 *	18(%esp) - %eax
 *	1C(%esp) - %ds
 *	20(%esp) - %es
 *	24(%esp) - %fs
 *	28(%esp) - %gs		saved iff !CONFIG_X86_32_LAZY_GS
 *	2C(%esp) - orig_eax
 *	30(%esp) - %eip
 *	34(%esp) - %cs
 *	38(%esp) - %eflags
 *	3C(%esp) - %oldesp
 *	40(%esp) - %oldss
 *
 * "current" is in register %ebx during any slow entries.
 */

#include <linux/linkage.h>
#include <linux/err.h>
#include <asm/thread_info.h>
#include <asm/irqflags.h>
#include <asm/errno.h>
#include <asm/segment.h>
#include <asm/smp.h>
#include <asm/page_types.h>
#include <asm/percpu.h>
#include <asm/dwarf2.h>
#include <asm/processor-flags.h>
#include <asm/ftrace.h>
#include <asm/irq_vectors.h>
#include <asm/cpufeature.h>
#include <asm/alternative-asm.h>
#include <asm/asm.h>
#include <asm/smap.h>

/* Avoid __ASSEMBLER__'ifying <linux/audit.h> just for this.  */
#include <linux/elf-em.h>
#define AUDIT_ARCH_I386		(EM_386|__AUDIT_ARCH_LE)
#define __AUDIT_ARCH_LE	   0x40000000

#ifndef CONFIG_AUDITSYSCALL
#define sysenter_audit	syscall_trace_entry
#define sysexit_audit	syscall_exit_work
#endif

	.section .entry.text, "ax"

/*
 * We use macros for low-level operations which need to be overridden
 * for paravirtualization.  The following will never clobber any registers:
 *   INTERRUPT_RETURN (aka. "iret")
 *   GET_CR0_INTO_EAX (aka. "movl %cr0, %eax")
 *   ENABLE_INTERRUPTS_SYSEXIT (aka "sti; sysexit").
 *
 * For DISABLE_INTERRUPTS/ENABLE_INTERRUPTS (aka "cli"/"sti"), you must
 * specify what registers can be overwritten (CLBR_NONE, CLBR_EAX/EDX/ECX/ANY).
 * Allowing a register to be clobbered can shrink the paravirt replacement
 * enough to patch inline, increasing performance.
 */

#ifdef CONFIG_PREEMPT
#define preempt_stop(clobbers)	DISABLE_INTERRUPTS(clobbers); TRACE_IRQS_OFF
#else
#define preempt_stop(clobbers)
#define resume_kernel		restore_all
#endif

.macro TRACE_IRQS_IRET
#ifdef CONFIG_TRACE_IRQFLAGS
	testl $X86_EFLAGS_IF,PT_EFLAGS(%esp)     # interrupts off?
	jz 1f
	TRACE_IRQS_ON
1:
#endif
.endm

/*
 * User gs save/restore
 *
 * %gs is used for userland TLS and kernel only uses it for stack
 * canary which is required to be at %gs:20 by gcc.  Read the comment
 * at the top of stackprotector.h for more info.
 *
 * Local labels 98 and 99 are used.
 */
#ifdef CONFIG_X86_32_LAZY_GS

 /* unfortunately push/pop can't be no-op */
.macro PUSH_GS
	pushl_cfi $0
.endm
.macro POP_GS pop=0
	addl $(4 + \pop), %esp
	CFI_ADJUST_CFA_OFFSET -(4 + \pop)
.endm
.macro POP_GS_EX
.endm

 /* all the rest are no-op */
.macro PTGS_TO_GS
.endm
.macro PTGS_TO_GS_EX
.endm
.macro GS_TO_REG reg
.endm
.macro REG_TO_PTGS reg
.endm
.macro SET_KERNEL_GS reg
.endm

#else	/* CONFIG_X86_32_LAZY_GS */

.macro PUSH_GS
	pushl_cfi %gs
	/*CFI_REL_OFFSET gs, 0*/
.endm

.macro POP_GS pop=0
98:	popl_cfi %gs
	/*CFI_RESTORE gs*/
  .if \pop <> 0
	add $\pop, %esp
	CFI_ADJUST_CFA_OFFSET -\pop
  .endif
.endm
.macro POP_GS_EX
.pushsection .fixup, "ax"
99:	movl $0, (%esp)
	jmp 98b
.popsection
	_ASM_EXTABLE(98b,99b)
.endm

.macro PTGS_TO_GS
98:	mov PT_GS(%esp), %gs
.endm
.macro PTGS_TO_GS_EX
.pushsection .fixup, "ax"
99:	movl $0, PT_GS(%esp)
	jmp 98b
.popsection
	_ASM_EXTABLE(98b,99b)
.endm

.macro GS_TO_REG reg
	movl %gs, \reg
	/*CFI_REGISTER gs, \reg*/
.endm
.macro REG_TO_PTGS reg
	movl \reg, PT_GS(%esp)
	/*CFI_REL_OFFSET gs, PT_GS*/
.endm
.macro SET_KERNEL_GS reg
	movl $(__KERNEL_STACK_CANARY), \reg
	movl \reg, %gs
.endm

#endif	/* CONFIG_X86_32_LAZY_GS */

.macro SAVE_ALL
	cld
	PUSH_GS
	pushl_cfi %fs
	/*CFI_REL_OFFSET fs, 0;*/
	pushl_cfi %es
	/*CFI_REL_OFFSET es, 0;*/
	pushl_cfi %ds
	/*CFI_REL_OFFSET ds, 0;*/
	pushl_cfi %eax
	CFI_REL_OFFSET eax, 0
	pushl_cfi %ebp
	CFI_REL_OFFSET ebp, 0
	pushl_cfi %edi
	CFI_REL_OFFSET edi, 0
	pushl_cfi %esi
	CFI_REL_OFFSET esi, 0
	pushl_cfi %edx
	CFI_REL_OFFSET edx, 0
	pushl_cfi %ecx
	CFI_REL_OFFSET ecx, 0
	pushl_cfi %ebx
	CFI_REL_OFFSET ebx, 0
	movl $(__USER_DS), %edx
	movl %edx, %ds
	movl %edx, %es
	movl $(__KERNEL_PERCPU), %edx
	movl %edx, %fs
	SET_KERNEL_GS %edx
.endm

.macro RESTORE_INT_REGS
	popl_cfi %ebx
	CFI_RESTORE ebx
	popl_cfi %ecx
	CFI_RESTORE ecx
	popl_cfi %edx
	CFI_RESTORE edx
	popl_cfi %esi
	CFI_RESTORE esi
	popl_cfi %edi
	CFI_RESTORE edi
	popl_cfi %ebp
	CFI_RESTORE ebp
	popl_cfi %eax
	CFI_RESTORE eax
.endm

.macro RESTORE_REGS pop=0
	RESTORE_INT_REGS
1:	popl_cfi %ds
	/*CFI_RESTORE ds;*/
2:	popl_cfi %es
	/*CFI_RESTORE es;*/
3:	popl_cfi %fs
	/*CFI_RESTORE fs;*/
	POP_GS \pop
.pushsection .fixup, "ax"
4:	movl $0, (%esp)
	jmp 1b
5:	movl $0, (%esp)
	jmp 2b
6:	movl $0, (%esp)
	jmp 3b
.popsection
	_ASM_EXTABLE(1b,4b)
	_ASM_EXTABLE(2b,5b)
	_ASM_EXTABLE(3b,6b)
	POP_GS_EX
.endm

.macro RING0_INT_FRAME
	CFI_STARTPROC simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA esp, 3*4
	/*CFI_OFFSET cs, -2*4;*/
	CFI_OFFSET eip, -3*4
.endm

.macro RING0_EC_FRAME
	CFI_STARTPROC simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA esp, 4*4
	/*CFI_OFFSET cs, -2*4;*/
	CFI_OFFSET eip, -3*4
.endm

.macro RING0_PTREGS_FRAME
	CFI_STARTPROC simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA esp, PT_OLDESP-PT_EBX
	/*CFI_OFFSET cs, PT_CS-PT_OLDESP;*/
	CFI_OFFSET eip, PT_EIP-PT_OLDESP
	/*CFI_OFFSET es, PT_ES-PT_OLDESP;*/
	/*CFI_OFFSET ds, PT_DS-PT_OLDESP;*/
	CFI_OFFSET eax, PT_EAX-PT_OLDESP
	CFI_OFFSET ebp, PT_EBP-PT_OLDESP
	CFI_OFFSET edi, PT_EDI-PT_OLDESP
	CFI_OFFSET esi, PT_ESI-PT_OLDESP
	CFI_OFFSET edx, PT_EDX-PT_OLDESP
	CFI_OFFSET ecx, PT_ECX-PT_OLDESP
	CFI_OFFSET ebx, PT_EBX-PT_OLDESP
.endm

ENTRY(ret_from_fork)
	CFI_STARTPROC
	pushl_cfi %eax
	call schedule_tail
	GET_THREAD_INFO(%ebp)
	popl_cfi %eax
	pushl_cfi $0x0202		# Reset kernel eflags
	popfl_cfi
	jmp syscall_exit
	CFI_ENDPROC
END(ret_from_fork)

ENTRY(ret_from_kernel_thread)
	CFI_STARTPROC
	pushl_cfi %eax
	call schedule_tail
	GET_THREAD_INFO(%ebp)
	popl_cfi %eax
	pushl_cfi $0x0202		# Reset kernel eflags
	popfl_cfi
	movl PT_EBP(%esp),%eax
	call *PT_EBX(%esp)
	movl $0,PT_EAX(%esp)
	jmp syscall_exit
	CFI_ENDPROC
ENDPROC(ret_from_kernel_thread)

/*
 * Interrupt exit functions should be protected against kprobes
 */
	.pushsection .kprobes.text, "ax"
/*
 * Return to user mode is not as complex as all this looks,
 * but we want the default path for a system call return to
 * go as quickly as possible which is why some of this is
 * less clear than it otherwise should be.
 */

	# userspace resumption stub bypassing syscall exit tracing
	ALIGN
	RING0_PTREGS_FRAME
ret_from_exception:
	preempt_stop(CLBR_ANY)
ret_from_intr:
	GET_THREAD_INFO(%ebp)
#ifdef CONFIG_VM86
	movl PT_EFLAGS(%esp), %eax	# mix EFLAGS and CS
	movb PT_CS(%esp), %al
	andl $(X86_EFLAGS_VM | SEGMENT_RPL_MASK), %eax
#else
	/*
	 * We can be coming here from child spawned by kernel_thread().
	 */
	movl PT_CS(%esp), %eax
	andl $SEGMENT_RPL_MASK, %eax
#endif
	cmpl $USER_RPL, %eax
	jb resume_kernel		# not returning to v8086 or userspace

ENTRY(resume_userspace)
	LOCKDEP_SYS_EXIT
 	DISABLE_INTERRUPTS(CLBR_ANY)	# make sure we don't miss an interrupt
					# setting need_resched or sigpending
					# between sampling and the iret
	TRACE_IRQS_OFF
	movl TI_flags(%ebp), %ecx
	andl $_TIF_WORK_MASK, %ecx	# is there any work to be done on
					# int/exception return?
	jne work_pending
	jmp restore_all
END(ret_from_exception)

#ifdef CONFIG_PREEMPT
ENTRY(resume_kernel)
	DISABLE_INTERRUPTS(CLBR_ANY)
	cmpl $0,TI_preempt_count(%ebp)	# non-zero preempt_count ?
	jnz restore_all
need_resched:
	movl TI_flags(%ebp), %ecx	# need_resched set ?
	testb $_TIF_NEED_RESCHED, %cl
	jz restore_all
	testl $X86_EFLAGS_IF,PT_EFLAGS(%esp)	# interrupts off (exception path) ?
	jz restore_all
	call preempt_schedule_irq
	jmp need_resched
END(resume_kernel)
#endif
	CFI_ENDPROC
/*
 * End of kprobes section
 */
	.popsection

/* SYSENTER_RETURN points to after the "sysenter" instruction in
   the vsyscall page.  See vsyscall-sysentry.S, which defines the symbol.  */

	# sysenter call handler stub
ENTRY(ia32_sysenter_target)
	CFI_STARTPROC simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA esp, 0
	CFI_REGISTER esp, ebp
	movl TSS_sysenter_sp0(%esp),%esp
sysenter_past_esp:
	/*
	 * Interrupts are disabled here, but we can't trace it until
	 * enough kernel state to call TRACE_IRQS_OFF can be called - but
	 * we immediately enable interrupts at that point anyway.
	 */
	pushl_cfi $__USER_DS
	/*CFI_REL_OFFSET ss, 0*/
	pushl_cfi %ebp
	CFI_REL_OFFSET esp, 0
	pushfl_cfi
	orl $X86_EFLAGS_IF, (%esp)
	pushl_cfi $__USER_CS
	/*CFI_REL_OFFSET cs, 0*/
	/*
	 * Push current_thread_info()->sysenter_return to the stack.
	 * A tiny bit of offset fixup is necessary - 4*4 means the 4 words
	 * pushed above; +8 corresponds to copy_thread's esp0 setting.
	 */
	pushl_cfi ((TI_sysenter_return)-THREAD_SIZE+8+4*4)(%esp)
	CFI_REL_OFFSET eip, 0

	pushl_cfi %eax
	SAVE_ALL
	ENABLE_INTERRUPTS(CLBR_NONE)

/*
 * Load the potential sixth argument from user stack.
 * Careful about security.
 */
	cmpl $__PAGE_OFFSET-3,%ebp
	jae syscall_fault
	ASM_STAC
1:	movl (%ebp),%ebp
	ASM_CLAC
	movl %ebp,PT_EBP(%esp)
	_ASM_EXTABLE(1b,syscall_fault)

	GET_THREAD_INFO(%ebp)

	testl $_TIF_WORK_SYSCALL_ENTRY,TI_flags(%ebp)
	jnz sysenter_audit
sysenter_do_call:
	cmpl $(NR_syscalls), %eax
	jae sysenter_badsys
	call *sys_call_table(,%eax,4)
sysenter_after_call:
	movl %eax,PT_EAX(%esp)
	LOCKDEP_SYS_EXIT
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF
	movl TI_flags(%ebp), %ecx
	testl $_TIF_ALLWORK_MASK, %ecx
	jne sysexit_audit
sysenter_exit:
/* if something modifies registers it must also disable sysexit */
	movl PT_EIP(%esp), %edx
	movl PT_OLDESP(%esp), %ecx
	xorl %ebp,%ebp
	TRACE_IRQS_ON
1:	mov  PT_FS(%esp), %fs
	PTGS_TO_GS
	ENABLE_INTERRUPTS_SYSEXIT

#ifdef CONFIG_AUDITSYSCALL
sysenter_audit:
	testl $(_TIF_WORK_SYSCALL_ENTRY & ~_TIF_SYSCALL_AUDIT),TI_flags(%ebp)
	jnz syscall_trace_entry
	addl $4,%esp
	CFI_ADJUST_CFA_OFFSET -4
	/* %esi already in 8(%esp)	   6th arg: 4th syscall arg */
	/* %edx already in 4(%esp)	   5th arg: 3rd syscall arg */
	/* %ecx already in 0(%esp)	   4th arg: 2nd syscall arg */
	movl %ebx,%ecx			/* 3rd arg: 1st syscall arg */
	movl %eax,%edx			/* 2nd arg: syscall number */
	movl $AUDIT_ARCH_I386,%eax	/* 1st arg: audit arch */
	call __audit_syscall_entry
	pushl_cfi %ebx
	movl PT_EAX(%esp),%eax		/* reload syscall number */
	jmp sysenter_do_call

sysexit_audit:
	testl $(_TIF_ALLWORK_MASK & ~_TIF_SYSCALL_AUDIT), %ecx
	jne syscall_exit_work
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_ANY)
	movl %eax,%edx		/* second arg, syscall return value */
	cmpl $-MAX_ERRNO,%eax	/* is it an error ? */
	setbe %al		/* 1 if so, 0 if not */
	movzbl %al,%eax		/* zero-extend that */
	call __audit_syscall_exit
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF
	movl TI_flags(%ebp), %ecx
	testl $(_TIF_ALLWORK_MASK & ~_TIF_SYSCALL_AUDIT), %ecx
	jne syscall_exit_work
	movl PT_EAX(%esp),%eax	/* reload syscall return value */
	jmp sysenter_exit
#endif

	CFI_ENDPROC
.pushsection .fixup,"ax"
2:	movl $0,PT_FS(%esp)
	jmp 1b
.popsection
	_ASM_EXTABLE(1b,2b)
	PTGS_TO_GS_EX
ENDPROC(ia32_sysenter_target)

/*
 * syscall stub including irq exit should be protected against kprobes
 */
	.pushsection .kprobes.text, "ax"
	# system call handler stub
ENTRY(system_call)
	RING0_INT_FRAME			# can't unwind into user space anyway
	ASM_CLAC
	pushl_cfi %eax			# save orig_eax
	SAVE_ALL
	GET_THREAD_INFO(%ebp)
					# system call tracing in operation / emulation
	testl $_TIF_WORK_SYSCALL_ENTRY,TI_flags(%ebp)
	jnz syscall_trace_entry
	cmpl $(NR_syscalls), %eax
	jae syscall_badsys
syscall_call:
	call *sys_call_table(,%eax,4)
syscall_after_call:
	movl %eax,PT_EAX(%esp)		# store the return value
syscall_exit:
	LOCKDEP_SYS_EXIT
	DISABLE_INTERRUPTS(CLBR_ANY)	# make sure we don't miss an interrupt
					# setting need_resched or sigpending
					# between sampling and the iret
	TRACE_IRQS_OFF
	movl TI_flags(%ebp), %ecx
	testl $_TIF_ALLWORK_MASK, %ecx	# current->work
	jne syscall_exit_work

restore_all:
	TRACE_IRQS_IRET
restore_all_notrace:
#ifdef CONFIG_X86_ESPFIX32
	movl PT_EFLAGS(%esp), %eax	# mix EFLAGS, SS and CS
	# Warning: PT_OLDSS(%esp) contains the wrong/random values if we
	# are returning to the kernel.
	# See comments in process.c:copy_thread() for details.
	movb PT_OLDSS(%esp), %ah
	movb PT_CS(%esp), %al
	andl $(X86_EFLAGS_VM | (SEGMENT_TI_MASK << 8) | SEGMENT_RPL_MASK), %eax
	cmpl $((SEGMENT_LDT << 8) | USER_RPL), %eax
	CFI_REMEMBER_STATE
	je ldt_ss			# returning to user-space with LDT SS
#endif
restore_nocheck:
	RESTORE_REGS 4			# skip orig_eax/error_code
irq_return:
	INTERRUPT_RETURN
.section .fixup,"ax"
ENTRY(iret_exc)
	pushl $0			# no error code
	pushl $do_iret_error
	jmp error_code
.previous
	_ASM_EXTABLE(irq_return,iret_exc)

#ifdef CONFIG_X86_ESPFIX32
	CFI_RESTORE_STATE
ldt_ss:
#ifdef CONFIG_PARAVIRT
	/*
	 * The kernel can't run on a non-flat stack if paravirt mode
	 * is active.  Rather than try to fixup the high bits of
	 * ESP, bypass this code entirely.  This may break DOSemu
	 * and/or Wine support in a paravirt VM, although the option
	 * is still available to implement the setting of the high
	 * 16-bits in the INTERRUPT_RETURN paravirt-op.
	 */
	cmpl $0, pv_info+PARAVIRT_enabled
	jne restore_nocheck
#endif

/*
 * Setup and switch to ESPFIX stack
 *
 * We're returning to userspace with a 16 bit stack. The CPU will not
 * restore the high word of ESP for us on executing iret... This is an
 * "official" bug of all the x86-compatible CPUs, which we can work
 * around to make dosemu and wine happy. We do this by preloading the
 * high word of ESP with the high word of the userspace ESP while
 * compensating for the offset by changing to the ESPFIX segment with
 * a base address that matches for the difference.
 */
#define GDT_ESPFIX_SS PER_CPU_VAR(gdt_page) + (GDT_ENTRY_ESPFIX_SS * 8)
	mov %esp, %edx			/* load kernel esp */
	mov PT_OLDESP(%esp), %eax	/* load userspace esp */
	mov %dx, %ax			/* eax: new kernel esp */
	sub %eax, %edx			/* offset (low word is 0) */
	shr $16, %edx
	mov %dl, GDT_ESPFIX_SS + 4 /* bits 16..23 */
	mov %dh, GDT_ESPFIX_SS + 7 /* bits 24..31 */
	pushl_cfi $__ESPFIX_SS
	pushl_cfi %eax			/* new kernel esp */
	/* Disable interrupts, but do not irqtrace this section: we
	 * will soon execute iret and the tracer was already set to
	 * the irqstate after the iret */
	DISABLE_INTERRUPTS(CLBR_EAX)
	lss (%esp), %esp		/* switch to espfix segment */
	CFI_ADJUST_CFA_OFFSET -8
	jmp restore_nocheck
#endif
	CFI_ENDPROC
ENDPROC(system_call)

	# perform work that needs to be done immediately before resumption
	ALIGN
	RING0_PTREGS_FRAME		# can't unwind into user space anyway
work_pending:
	testb $_TIF_NEED_RESCHED, %cl
	jz work_notifysig
work_resched:
	call schedule
	LOCKDEP_SYS_EXIT
	DISABLE_INTERRUPTS(CLBR_ANY)	# make sure we don't miss an interrupt
					# setting need_resched or sigpending
					# between sampling and the iret
	TRACE_IRQS_OFF
	movl TI_flags(%ebp), %ecx
	andl $_TIF_WORK_MASK, %ecx	# is there any work to be done other
					# than syscall tracing?
	jz restore_all
	testb $_TIF_NEED_RESCHED, %cl
	jnz work_resched

work_notifysig:				# deal with pending signals and
					# notify-resume requests
#ifdef CONFIG_VM86
	testl $X86_EFLAGS_VM, PT_EFLAGS(%esp)
	movl %esp, %eax
	jne work_notifysig_v86		# returning to kernel-space or
					# vm86-space
1:
#else
	movl %esp, %eax
#endif
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	movb PT_CS(%esp), %bl
	andb $SEGMENT_RPL_MASK, %bl
	cmpb $USER_RPL, %bl
	jb resume_kernel
	xorl %edx, %edx
	call do_notify_resume
	jmp resume_userspace

#ifdef CONFIG_VM86
	ALIGN
work_notifysig_v86:
	pushl_cfi %ecx			# save ti_flags for do_notify_resume
	call save_v86_state		# %eax contains pt_regs pointer
	popl_cfi %ecx
	movl %eax, %esp
	jmp 1b
#endif
END(work_pending)

	# perform syscall exit tracing
	ALIGN
syscall_trace_entry:
	movl $-ENOSYS,PT_EAX(%esp)
	movl %esp, %eax
	call syscall_trace_enter
	/* What it returned is what we'll actually use.  */
	cmpl $(NR_syscalls), %eax
	jnae syscall_call
	jmp syscall_exit
END(syscall_trace_entry)

	# perform syscall exit tracing
	ALIGN
syscall_exit_work:
	testl $_TIF_WORK_SYSCALL_EXIT, %ecx
	jz work_pending
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_ANY)	# could let syscall_trace_leave() call
					# schedule() instead
	movl %esp, %eax
	call syscall_trace_leave
	jmp resume_userspace
END(syscall_exit_work)
	CFI_ENDPROC

	RING0_INT_FRAME			# can't unwind into user space anyway
syscall_fault:
	ASM_CLAC
	GET_THREAD_INFO(%ebp)
	movl $-EFAULT,PT_EAX(%esp)
	jmp resume_userspace
END(syscall_fault)

syscall_badsys:
	movl $-ENOSYS,%eax
	jmp syscall_after_call
END(syscall_badsys)

sysenter_badsys:
	movl $-ENOSYS,%eax
	jmp sysenter_after_call
END(syscall_badsys)
	CFI_ENDPROC
/*
 * End of kprobes section
 */
	.popsection

.macro FIXUP_ESPFIX_STACK
/*
 * Switch back for ESPFIX stack to the normal zerobased stack
 *
 * We can't call C functions using the ESPFIX stack. This code reads
 * the high word of the segment base from the GDT and swiches to the
 * normal stack and adjusts ESP with the matching offset.
 */
#ifdef CONFIG_X86_ESPFIX32
	/* fixup the stack */
	mov GDT_ESPFIX_SS + 4, %al /* bits 16..23 */
	mov GDT_ESPFIX_SS + 7, %ah /* bits 24..31 */
	shl $16, %eax
	addl %esp, %eax			/* the adjusted stack pointer */
	pushl_cfi $__KERNEL_DS
	pushl_cfi %eax
	lss (%esp), %esp		/* switch to the normal stack segment */
	CFI_ADJUST_CFA_OFFSET -8
#endif
.endm
.macro UNWIND_ESPFIX_STACK
#ifdef CONFIG_X86_ESPFIX32
	movl %ss, %eax
	/* see if on espfix stack */
	cmpw $__ESPFIX_SS, %ax
	jne 27f
	movl $__KERNEL_DS, %eax
	movl %eax, %ds
	movl %eax, %es
	/* switch to normal stack */
	FIXUP_ESPFIX_STACK
27:
#endif
.endm

/*
 * Build the entry stubs and pointer table with some assembler magic.
 * We pack 7 stubs into a single 32-byte chunk, which will fit in a
 * single cache line on all modern x86 implementations.
 */
.section .init.rodata,"a"
ENTRY(interrupt)
.section .entry.text, "ax"
	.p2align 5
	.p2align CONFIG_X86_L1_CACHE_SHIFT
ENTRY(irq_entries_start)
	RING0_INT_FRAME
vector=FIRST_EXTERNAL_VECTOR
.rept (NR_VECTORS-FIRST_EXTERNAL_VECTOR+6)/7
	.balign 32
  .rept	7
    .if vector < NR_VECTORS
      .if vector <> FIRST_EXTERNAL_VECTOR
	CFI_ADJUST_CFA_OFFSET -4
      .endif
1:	pushl_cfi $(~vector+0x80)	/* Note: always in signed byte range */
      .if ((vector-FIRST_EXTERNAL_VECTOR)%7) <> 6
	jmp 2f
      .endif
      .previous
	.long 1b
      .section .entry.text, "ax"
vector=vector+1
    .endif
  .endr
2:	jmp common_interrupt
.endr
END(irq_entries_start)

.previous
END(interrupt)
.previous

/*
 * the CPU automatically disables interrupts when executing an IRQ vector,
 * so IRQ-flags tracing has to follow that:
 */
	.p2align CONFIG_X86_L1_CACHE_SHIFT
common_interrupt:
	ASM_CLAC
	addl $-0x80,(%esp)	/* Adjust vector into the [-256,-1] range */
	SAVE_ALL
	TRACE_IRQS_OFF
	movl %esp,%eax
	call do_IRQ
	jmp ret_from_intr
ENDPROC(common_interrupt)
	CFI_ENDPROC

/*
 *  Irq entries should be protected against kprobes
 */
	.pushsection .kprobes.text, "ax"
#define BUILD_INTERRUPT3(name, nr, fn)	\
ENTRY(name)				\
	RING0_INT_FRAME;		\
	ASM_CLAC;			\
	pushl_cfi $~(nr);		\
	SAVE_ALL;			\
	TRACE_IRQS_OFF			\
	movl %esp,%eax;			\
	call fn;			\
	jmp ret_from_intr;		\
	CFI_ENDPROC;			\
ENDPROC(name)

#define BUILD_INTERRUPT(name, nr)	BUILD_INTERRUPT3(name, nr, smp_##name)

/* The include is where all of the SMP etc. interrupts come from */
#include <asm/entry_arch.h>

ENTRY(coprocessor_error)
	RING0_INT_FRAME
	ASM_CLAC
	pushl_cfi $0
	pushl_cfi $do_coprocessor_error
	jmp error_code
	CFI_ENDPROC
END(coprocessor_error)

ENTRY(simd_coprocessor_error)
	RING0_INT_FRAME
	ASM_CLAC
	pushl_cfi $0
#ifdef CONFIG_X86_INVD_BUG
	/* AMD 486 bug: invd from userspace calls exception 19 instead of #GP */
661:	pushl_cfi $do_general_protection
662:
.section .altinstructions,"a"
	altinstruction_entry 661b, 663f, X86_FEATURE_XMM, 662b-661b, 664f-663f
.previous
.section .altinstr_replacement,"ax"
663:	pushl $do_simd_coprocessor_error
664:
.previous
#else
	pushl_cfi $do_simd_coprocessor_error
#endif
	jmp error_code
	CFI_ENDPROC
END(simd_coprocessor_error)

ENTRY(device_not_available)
	RING0_INT_FRAME
	ASM_CLAC
	pushl_cfi $-1			# mark this as an int
	pushl_cfi $do_device_not_available
	jmp error_code
	CFI_ENDPROC
END(device_not_available)

#ifdef CONFIG_PARAVIRT
ENTRY(native_iret)
	iret
	_ASM_EXTABLE(native_iret, iret_exc)
END(native_iret)

ENTRY(native_irq_enable_sysexit)
	sti
	sysexit
END(native_irq_enable_sysexit)
#endif

ENTRY(overflow)
	RING0_INT_FRAME
	ASM_CLAC
	pushl_cfi $0
	pushl_cfi $do_overflow
	jmp error_code
	CFI_ENDPROC
END(overflow)

ENTRY(bounds)
	RING0_INT_FRAME
	ASM_CLAC
	pushl_cfi $0
	pushl_cfi $do_bounds
	jmp error_code
	CFI_ENDPROC
END(bounds)

ENTRY(invalid_op)
	RING0_INT_FRAME
	ASM_CLAC
	pushl_cfi $0
	pushl_cfi $do_invalid_op
	jmp error_code
	CFI_ENDPROC
END(invalid_op)

ENTRY(coprocessor_segment_overrun)
	RING0_INT_FRAME
	ASM_CLAC
	pushl_cfi $0
	pushl_cfi $do_coprocessor_segment_overrun
	jmp error_code
	CFI_ENDPROC
END(coprocessor_segment_overrun)

ENTRY(invalid_TSS)
	RING0_EC_FRAME
	ASM_CLAC
	pushl_cfi $do_invalid_TSS
	jmp error_code
	CFI_ENDPROC
END(invalid_TSS)

ENTRY(segment_not_present)
	RING0_EC_FRAME
	ASM_CLAC
	pushl_cfi $do_segment_not_present
	jmp error_code
	CFI_ENDPROC
END(segment_not_present)

ENTRY(stack_segment)
	RING0_EC_FRAME
	ASM_CLAC
	pushl_cfi $do_stack_segment
	jmp error_code
	CFI_ENDPROC
END(stack_segment)

ENTRY(alignment_check)
	RING0_EC_FRAME
	ASM_CLAC
	pushl_cfi $do_alignment_check
	jmp error_code
	CFI_ENDPROC
END(alignment_check)

ENTRY(divide_error)
	RING0_INT_FRAME
	ASM_CLAC
	pushl_cfi $0			# no error code
	pushl_cfi $do_divide_error
	jmp error_code
	CFI_ENDPROC
END(divide_error)

#ifdef CONFIG_X86_MCE
ENTRY(machine_check)
	RING0_INT_FRAME
	ASM_CLAC
	pushl_cfi $0
	pushl_cfi machine_check_vector
	jmp error_code
	CFI_ENDPROC
END(machine_check)
#endif

ENTRY(spurious_interrupt_bug)
	RING0_INT_FRAME
	ASM_CLAC
	pushl_cfi $0
	pushl_cfi $do_spurious_interrupt_bug
	jmp error_code
	CFI_ENDPROC
END(spurious_interrupt_bug)
/*
 * End of kprobes section
 */
	.popsection

#ifdef CONFIG_XEN
/* Xen doesn't set %esp to be precisely what the normal sysenter
   entrypoint expects, so fix it up before using the normal path. */
ENTRY(xen_sysenter_target)
	RING0_INT_FRAME
	addl $5*4, %esp		/* remove xen-provided frame */
	CFI_ADJUST_CFA_OFFSET -5*4
	jmp sysenter_past_esp
	CFI_ENDPROC

ENTRY(xen_hypervisor_callback)
	CFI_STARTPROC
	pushl_cfi $-1 /* orig_ax = -1 => not a system call */
	SAVE_ALL
	TRACE_IRQS_OFF

	/* Check to see if we got the event in the critical
	   region in xen_iret_direct, after we've reenabled
	   events and checked for pending events.  This simulates
	   iret instruction's behaviour where it delivers a
	   pending interrupt when enabling interrupts. */
	movl PT_EIP(%esp),%eax
	cmpl $xen_iret_start_crit,%eax
	jb   1f
	cmpl $xen_iret_end_crit,%eax
	jae  1f

	jmp  xen_iret_crit_fixup

ENTRY(xen_do_upcall)
1:	mov %esp, %eax
	call xen_evtchn_do_upcall
	jmp  ret_from_intr
	CFI_ENDPROC
ENDPROC(xen_hypervisor_callback)

# Hypervisor uses this for application faults while it executes.
# We get here for two reasons:
#  1. Fault while reloading DS, ES, FS or GS
#  2. Fault while executing IRET
# Category 1 we fix up by reattempting the load, and zeroing the segment
# register if the load fails.
# Category 2 we fix up by jumping to do_iret_error. We cannot use the
# normal Linux return path in this case because if we use the IRET hypercall
# to pop the stack frame we end up in an infinite loop of failsafe callbacks.
# We distinguish between categories by maintaining a status value in EAX.
ENTRY(xen_failsafe_callback)
	CFI_STARTPROC
	pushl_cfi %eax
	movl $1,%eax
1:	mov 4(%esp),%ds
2:	mov 8(%esp),%es
3:	mov 12(%esp),%fs
4:	mov 16(%esp),%gs
	/* EAX == 0 => Category 1 (Bad segment)
	   EAX != 0 => Category 2 (Bad IRET) */
	testl %eax,%eax
	popl_cfi %eax
	lea 16(%esp),%esp
	CFI_ADJUST_CFA_OFFSET -16
	jz 5f
	jmp iret_exc
5:	pushl_cfi $-1 /* orig_ax = -1 => not a system call */
	SAVE_ALL
	jmp ret_from_exception
	CFI_ENDPROC

.section .fixup,"ax"
6:	xorl %eax,%eax
	movl %eax,4(%esp)
	jmp 1b
7:	xorl %eax,%eax
	movl %eax,8(%esp)
	jmp 2b
8:	xorl %eax,%eax
	movl %eax,12(%esp)
	jmp 3b
9:	xorl %eax,%eax
	movl %eax,16(%esp)
	jmp 4b
.previous
	_ASM_EXTABLE(1b,6b)
	_ASM_EXTABLE(2b,7b)
	_ASM_EXTABLE(3b,8b)
	_ASM_EXTABLE(4b,9b)
ENDPROC(xen_failsafe_callback)

BUILD_INTERRUPT3(xen_hvm_callback_vector, HYPERVISOR_CALLBACK_VECTOR,
		xen_evtchn_do_upcall)

#endif	/* CONFIG_XEN */

#if IS_ENABLED(CONFIG_HYPERV)

BUILD_INTERRUPT3(hyperv_callback_vector, HYPERVISOR_CALLBACK_VECTOR,
	hyperv_vector_handler)

#endif /* CONFIG_HYPERV */

#ifdef CONFIG_FUNCTION_TRACER
#ifdef CONFIG_DYNAMIC_FTRACE

ENTRY(mcount)
	ret
END(mcount)

ENTRY(ftrace_caller)
	cmpl $0, function_trace_stop
	jne  ftrace_stub

	pushl %eax
	pushl %ecx
	pushl %edx
	pushl $0	/* Pass NULL as regs pointer */
	movl 4*4(%esp), %eax
	movl 0x4(%ebp), %edx
	movl function_trace_op, %ecx
	subl $MCOUNT_INSN_SIZE, %eax

.globl ftrace_call
ftrace_call:
	call ftrace_stub

	addl $4,%esp	/* skip NULL pointer */
	popl %edx
	popl %ecx
	popl %eax
ftrace_ret:
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
.globl ftrace_graph_call
ftrace_graph_call:
	jmp ftrace_stub
#endif

.globl ftrace_stub
ftrace_stub:
	ret
END(ftrace_caller)

ENTRY(ftrace_regs_caller)
	pushf	/* push flags before compare (in cs location) */
	cmpl $0, function_trace_stop
	jne ftrace_restore_flags

	/*
	 * i386 does not save SS and ESP when coming from kernel.
	 * Instead, to get sp, &regs->sp is used (see ptrace.h).
	 * Unfortunately, that means eflags must be at the same location
	 * as the current return ip is. We move the return ip into the
	 * ip location, and move flags into the return ip location.
	 */
	pushl 4(%esp)	/* save return ip into ip slot */

	pushl $0	/* Load 0 into orig_ax */
	pushl %gs
	pushl %fs
	pushl %es
	pushl %ds
	pushl %eax
	pushl %ebp
	pushl %edi
	pushl %esi
	pushl %edx
	pushl %ecx
	pushl %ebx

	movl 13*4(%esp), %eax	/* Get the saved flags */
	movl %eax, 14*4(%esp)	/* Move saved flags into regs->flags location */
				/* clobbering return ip */
	movl $__KERNEL_CS,13*4(%esp)

	movl 12*4(%esp), %eax	/* Load ip (1st parameter) */
	subl $MCOUNT_INSN_SIZE, %eax	/* Adjust ip */
	movl 0x4(%ebp), %edx	/* Load parent ip (2nd parameter) */
	movl function_trace_op, %ecx /* Save ftrace_pos in 3rd parameter */
	pushl %esp		/* Save pt_regs as 4th parameter */

GLOBAL(ftrace_regs_call)
	call ftrace_stub

	addl $4, %esp		/* Skip pt_regs */
	movl 14*4(%esp), %eax	/* Move flags back into cs */
	movl %eax, 13*4(%esp)	/* Needed to keep addl from modifying flags */
	movl 12*4(%esp), %eax	/* Get return ip from regs->ip */
	movl %eax, 14*4(%esp)	/* Put return ip back for ret */

	popl %ebx
	popl %ecx
	popl %edx
	popl %esi
	popl %edi
	popl %ebp
	popl %eax
	popl %ds
	popl %es
	popl %fs
	popl %gs
	addl $8, %esp		/* Skip orig_ax and ip */
	popf			/* Pop flags at end (no addl to corrupt flags) */
	jmp ftrace_ret

ftrace_restore_flags:
	popf
	jmp  ftrace_stub
#else /* ! CONFIG_DYNAMIC_FTRACE */

ENTRY(mcount)
	cmpl $0, function_trace_stop
	jne  ftrace_stub

	cmpl $ftrace_stub, ftrace_trace_function
	jnz trace
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	cmpl $ftrace_stub, ftrace_graph_return
	jnz ftrace_graph_caller

	cmpl $ftrace_graph_entry_stub, ftrace_graph_entry
	jnz ftrace_graph_caller
#endif
.globl ftrace_stub
ftrace_stub:
	ret

	/* taken from glibc */
trace:
	pushl %eax
	pushl %ecx
	pushl %edx
	movl 0xc(%esp), %eax
	movl 0x4(%ebp), %edx
	subl $MCOUNT_INSN_SIZE, %eax

	call *ftrace_trace_function

	popl %edx
	popl %ecx
	popl %eax
	jmp ftrace_stub
END(mcount)
#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	pushl %eax
	pushl %ecx
	pushl %edx
	movl 0xc(%esp), %edx
	lea 0x4(%ebp), %eax
	movl (%ebp), %ecx
	subl $MCOUNT_INSN_SIZE, %edx
	call prepare_ftrace_return
	popl %edx
	popl %ecx
	popl %eax
	ret
END(ftrace_graph_caller)

.globl return_to_handler
return_to_handler:
	pushl %eax
	pushl %edx
	movl %ebp, %eax
	call ftrace_return_to_handler
	movl %eax, %ecx
	popl %edx
	popl %eax
	jmp *%ecx
#endif

/*
 * Some functions should be protected against kprobes
 */
	.pushsection .kprobes.text, "ax"

ENTRY(page_fault)
	RING0_EC_FRAME
	ASM_CLAC
	pushl_cfi $do_page_fault
	ALIGN
error_code:
	/* the function address is in %gs's slot on the stack */
	pushl_cfi %fs
	/*CFI_REL_OFFSET fs, 0*/
	pushl_cfi %es
	/*CFI_REL_OFFSET es, 0*/
	pushl_cfi %ds
	/*CFI_REL_OFFSET ds, 0*/
	pushl_cfi %eax
	CFI_REL_OFFSET eax, 0
	pushl_cfi %ebp
	CFI_REL_OFFSET ebp, 0
	pushl_cfi %edi
	CFI_REL_OFFSET edi, 0
	pushl_cfi %esi
	CFI_REL_OFFSET esi, 0
	pushl_cfi %edx
	CFI_REL_OFFSET edx, 0
	pushl_cfi %ecx
	CFI_REL_OFFSET ecx, 0
	pushl_cfi %ebx
	CFI_REL_OFFSET ebx, 0
	cld
	movl $(__KERNEL_PERCPU), %ecx
	movl %ecx, %fs
	UNWIND_ESPFIX_STACK
	GS_TO_REG %ecx
	movl PT_GS(%esp), %edi		# get the function address
	movl PT_ORIG_EAX(%esp), %edx	# get the error code
	movl $-1, PT_ORIG_EAX(%esp)	# no syscall to restart
	REG_TO_PTGS %ecx
	SET_KERNEL_GS %ecx
	movl $(__USER_DS), %ecx
	movl %ecx, %ds
	movl %ecx, %es
	TRACE_IRQS_OFF
	movl %esp,%eax			# pt_regs pointer
	call *%edi
	jmp ret_from_exception
	CFI_ENDPROC
END(page_fault)

/*
 * Debug traps and NMI can happen at the one SYSENTER instruction
 * that sets up the real kernel stack. Check here, since we can't
 * allow the wrong stack to be used.
 *
 * "TSS_sysenter_sp0+12" is because the NMI/debug handler will have
 * already pushed 3 words if it hits on the sysenter instruction:
 * eflags, cs and eip.
 *
 * We just load the right stack, and push the three (known) values
 * by hand onto the new stack - while updating the return eip past
 * the instruction that would have done it for sysenter.
 */
.macro FIX_STACK offset ok label
	cmpw $__KERNEL_CS, 4(%esp)
	jne \ok
\label:
	movl TSS_sysenter_sp0 + \offset(%esp), %esp
	CFI_DEF_CFA esp, 0
	CFI_UNDEFINED eip
	pushfl_cfi
	pushl_cfi $__KERNEL_CS
	pushl_cfi $sysenter_past_esp
	CFI_REL_OFFSET eip, 0
.endm

ENTRY(debug)
	RING0_INT_FRAME
	ASM_CLAC
	cmpl $ia32_sysenter_target,(%esp)
	jne debug_stack_correct
	FIX_STACK 12, debug_stack_correct, debug_esp_fix_insn
debug_stack_correct:
	pushl_cfi $-1			# mark this as an int
	SAVE_ALL
	TRACE_IRQS_OFF
	xorl %edx,%edx			# error code 0
	movl %esp,%eax			# pt_regs pointer
	call do_debug
	jmp ret_from_exception
	CFI_ENDPROC
END(debug)

/*
 * NMI is doubly nasty. It can happen _while_ we're handling
 * a debug fault, and the debug fault hasn't yet been able to
 * clear up the stack. So we first check whether we got  an
 * NMI on the sysenter entry path, but after that we need to
 * check whether we got an NMI on the debug path where the debug
 * fault happened on the sysenter path.
 */
ENTRY(nmi)
	RING0_INT_FRAME
	ASM_CLAC
#ifdef CONFIG_X86_ESPFIX32
	pushl_cfi %eax
	movl %ss, %eax
	cmpw $__ESPFIX_SS, %ax
	popl_cfi %eax
	je nmi_espfix_stack
#endif
	cmpl $ia32_sysenter_target,(%esp)
	je nmi_stack_fixup
	pushl_cfi %eax
	movl %esp,%eax
	/* Do not access memory above the end of our stack page,
	 * it might not exist.
	 */
	andl $(THREAD_SIZE-1),%eax
	cmpl $(THREAD_SIZE-20),%eax
	popl_cfi %eax
	jae nmi_stack_correct
	cmpl $ia32_sysenter_target,12(%esp)
	je nmi_debug_stack_check
nmi_stack_correct:
	/* We have a RING0_INT_FRAME here */
	pushl_cfi %eax
	SAVE_ALL
	xorl %edx,%edx		# zero error code
	movl %esp,%eax		# pt_regs pointer
	call do_nmi
	jmp restore_all_notrace
	CFI_ENDPROC

nmi_stack_fixup:
	RING0_INT_FRAME
	FIX_STACK 12, nmi_stack_correct, 1
	jmp nmi_stack_correct

nmi_debug_stack_check:
	/* We have a RING0_INT_FRAME here */
	cmpw $__KERNEL_CS,16(%esp)
	jne nmi_stack_correct
	cmpl $debug,(%esp)
	jb nmi_stack_correct
	cmpl $debug_esp_fix_insn,(%esp)
	ja nmi_stack_correct
	FIX_STACK 24, nmi_stack_correct, 1
	jmp nmi_stack_correct

#ifdef CONFIG_X86_ESPFIX32
nmi_espfix_stack:
	/* We have a RING0_INT_FRAME here.
	 *
	 * create the pointer to lss back
	 */
	pushl_cfi %ss
	pushl_cfi %esp
	addl $4, (%esp)
	/* copy the iret frame of 12 bytes */
	.rept 3
	pushl_cfi 16(%esp)
	.endr
	pushl_cfi %eax
	SAVE_ALL
	FIXUP_ESPFIX_STACK		# %eax == %esp
	xorl %edx,%edx			# zero error code
	call do_nmi
	RESTORE_REGS
	lss 12+4(%esp), %esp		# back to espfix stack
	CFI_ADJUST_CFA_OFFSET -24
	jmp irq_return
#endif
	CFI_ENDPROC
END(nmi)

ENTRY(int3)
	RING0_INT_FRAME
	ASM_CLAC
	pushl_cfi $-1			# mark this as an int
	SAVE_ALL
	TRACE_IRQS_OFF
	xorl %edx,%edx		# zero error code
	movl %esp,%eax		# pt_regs pointer
	call do_int3
	jmp ret_from_exception
	CFI_ENDPROC
END(int3)

ENTRY(general_protection)
	RING0_EC_FRAME
	pushl_cfi $do_general_protection
	jmp error_code
	CFI_ENDPROC
END(general_protection)

#ifdef CONFIG_KVM_GUEST
ENTRY(async_page_fault)
	RING0_EC_FRAME
	ASM_CLAC
	pushl_cfi $do_async_page_fault
	jmp error_code
	CFI_ENDPROC
END(async_page_fault)
#endif

/*
 * End of kprobes section
 */
	.popsection
