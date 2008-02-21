/*
 *  linux/arch/i386/entry.S
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
 *	28(%esp) - orig_eax
 *	2C(%esp) - %eip
 *	30(%esp) - %cs
 *	34(%esp) - %eflags
 *	38(%esp) - %oldesp
 *	3C(%esp) - %oldss
 *
 * "current" is in register %ebx during any slow entries.
 */

#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/irqflags.h>
#include <asm/errno.h>
#include <asm/segment.h>
#include <asm/smp.h>
#include <asm/page.h>
#include <asm/desc.h>
#include <asm/percpu.h>
#include <asm/dwarf2.h>
#include "irq_vectors.h"

/*
 * We use macros for low-level operations which need to be overridden
 * for paravirtualization.  The following will never clobber any registers:
 *   INTERRUPT_RETURN (aka. "iret")
 *   GET_CR0_INTO_EAX (aka. "movl %cr0, %eax")
 *   ENABLE_INTERRUPTS_SYSCALL_RET (aka "sti; sysexit").
 *
 * For DISABLE_INTERRUPTS/ENABLE_INTERRUPTS (aka "cli"/"sti"), you must
 * specify what registers can be overwritten (CLBR_NONE, CLBR_EAX/EDX/ECX/ANY).
 * Allowing a register to be clobbered can shrink the paravirt replacement
 * enough to patch inline, increasing performance.
 */

#define nr_syscalls ((syscall_table_size)/4)

CF_MASK		= 0x00000001
TF_MASK		= 0x00000100
IF_MASK		= 0x00000200
DF_MASK		= 0x00000400 
NT_MASK		= 0x00004000
VM_MASK		= 0x00020000

#ifdef CONFIG_PREEMPT
#define preempt_stop(clobbers)	DISABLE_INTERRUPTS(clobbers); TRACE_IRQS_OFF
#else
#define preempt_stop(clobbers)
#define resume_kernel		restore_nocheck
#endif

.macro TRACE_IRQS_IRET
#ifdef CONFIG_TRACE_IRQFLAGS
	testl $IF_MASK,PT_EFLAGS(%esp)     # interrupts off?
	jz 1f
	TRACE_IRQS_ON
1:
#endif
.endm

#ifdef CONFIG_VM86
#define resume_userspace_sig	check_userspace
#else
#define resume_userspace_sig	resume_userspace
#endif

#define SAVE_ALL \
	cld; \
	pushl %fs; \
	CFI_ADJUST_CFA_OFFSET 4;\
	/*CFI_REL_OFFSET fs, 0;*/\
	pushl %es; \
	CFI_ADJUST_CFA_OFFSET 4;\
	/*CFI_REL_OFFSET es, 0;*/\
	pushl %ds; \
	CFI_ADJUST_CFA_OFFSET 4;\
	/*CFI_REL_OFFSET ds, 0;*/\
	pushl %eax; \
	CFI_ADJUST_CFA_OFFSET 4;\
	CFI_REL_OFFSET eax, 0;\
	pushl %ebp; \
	CFI_ADJUST_CFA_OFFSET 4;\
	CFI_REL_OFFSET ebp, 0;\
	pushl %edi; \
	CFI_ADJUST_CFA_OFFSET 4;\
	CFI_REL_OFFSET edi, 0;\
	pushl %esi; \
	CFI_ADJUST_CFA_OFFSET 4;\
	CFI_REL_OFFSET esi, 0;\
	pushl %edx; \
	CFI_ADJUST_CFA_OFFSET 4;\
	CFI_REL_OFFSET edx, 0;\
	pushl %ecx; \
	CFI_ADJUST_CFA_OFFSET 4;\
	CFI_REL_OFFSET ecx, 0;\
	pushl %ebx; \
	CFI_ADJUST_CFA_OFFSET 4;\
	CFI_REL_OFFSET ebx, 0;\
	movl $(__USER_DS), %edx; \
	movl %edx, %ds; \
	movl %edx, %es; \
	movl $(__KERNEL_PERCPU), %edx; \
	movl %edx, %fs

#define RESTORE_INT_REGS \
	popl %ebx;	\
	CFI_ADJUST_CFA_OFFSET -4;\
	CFI_RESTORE ebx;\
	popl %ecx;	\
	CFI_ADJUST_CFA_OFFSET -4;\
	CFI_RESTORE ecx;\
	popl %edx;	\
	CFI_ADJUST_CFA_OFFSET -4;\
	CFI_RESTORE edx;\
	popl %esi;	\
	CFI_ADJUST_CFA_OFFSET -4;\
	CFI_RESTORE esi;\
	popl %edi;	\
	CFI_ADJUST_CFA_OFFSET -4;\
	CFI_RESTORE edi;\
	popl %ebp;	\
	CFI_ADJUST_CFA_OFFSET -4;\
	CFI_RESTORE ebp;\
	popl %eax;	\
	CFI_ADJUST_CFA_OFFSET -4;\
	CFI_RESTORE eax

#define RESTORE_REGS	\
	RESTORE_INT_REGS; \
1:	popl %ds;	\
	CFI_ADJUST_CFA_OFFSET -4;\
	/*CFI_RESTORE ds;*/\
2:	popl %es;	\
	CFI_ADJUST_CFA_OFFSET -4;\
	/*CFI_RESTORE es;*/\
3:	popl %fs;	\
	CFI_ADJUST_CFA_OFFSET -4;\
	/*CFI_RESTORE fs;*/\
.pushsection .fixup,"ax";	\
4:	movl $0,(%esp);	\
	jmp 1b;		\
5:	movl $0,(%esp);	\
	jmp 2b;		\
6:	movl $0,(%esp);	\
	jmp 3b;		\
.section __ex_table,"a";\
	.align 4;	\
	.long 1b,4b;	\
	.long 2b,5b;	\
	.long 3b,6b;	\
.popsection

#define RING0_INT_FRAME \
	CFI_STARTPROC simple;\
	CFI_SIGNAL_FRAME;\
	CFI_DEF_CFA esp, 3*4;\
	/*CFI_OFFSET cs, -2*4;*/\
	CFI_OFFSET eip, -3*4

#define RING0_EC_FRAME \
	CFI_STARTPROC simple;\
	CFI_SIGNAL_FRAME;\
	CFI_DEF_CFA esp, 4*4;\
	/*CFI_OFFSET cs, -2*4;*/\
	CFI_OFFSET eip, -3*4

#define RING0_PTREGS_FRAME \
	CFI_STARTPROC simple;\
	CFI_SIGNAL_FRAME;\
	CFI_DEF_CFA esp, PT_OLDESP-PT_EBX;\
	/*CFI_OFFSET cs, PT_CS-PT_OLDESP;*/\
	CFI_OFFSET eip, PT_EIP-PT_OLDESP;\
	/*CFI_OFFSET es, PT_ES-PT_OLDESP;*/\
	/*CFI_OFFSET ds, PT_DS-PT_OLDESP;*/\
	CFI_OFFSET eax, PT_EAX-PT_OLDESP;\
	CFI_OFFSET ebp, PT_EBP-PT_OLDESP;\
	CFI_OFFSET edi, PT_EDI-PT_OLDESP;\
	CFI_OFFSET esi, PT_ESI-PT_OLDESP;\
	CFI_OFFSET edx, PT_EDX-PT_OLDESP;\
	CFI_OFFSET ecx, PT_ECX-PT_OLDESP;\
	CFI_OFFSET ebx, PT_EBX-PT_OLDESP

ENTRY(ret_from_fork)
	CFI_STARTPROC
	pushl %eax
	CFI_ADJUST_CFA_OFFSET 4
	call schedule_tail
	GET_THREAD_INFO(%ebp)
	popl %eax
	CFI_ADJUST_CFA_OFFSET -4
	pushl $0x0202			# Reset kernel eflags
	CFI_ADJUST_CFA_OFFSET 4
	popfl
	CFI_ADJUST_CFA_OFFSET -4
	jmp syscall_exit
	CFI_ENDPROC
END(ret_from_fork)

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
check_userspace:
	movl PT_EFLAGS(%esp), %eax	# mix EFLAGS and CS
	movb PT_CS(%esp), %al
	andl $(VM_MASK | SEGMENT_RPL_MASK), %eax
	cmpl $USER_RPL, %eax
	jb resume_kernel		# not returning to v8086 or userspace

ENTRY(resume_userspace)
	LOCKDEP_SYS_EXIT
 	DISABLE_INTERRUPTS(CLBR_ANY)	# make sure we don't miss an interrupt
					# setting need_resched or sigpending
					# between sampling and the iret
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
	jnz restore_nocheck
need_resched:
	movl TI_flags(%ebp), %ecx	# need_resched set ?
	testb $_TIF_NEED_RESCHED, %cl
	jz restore_all
	testl $IF_MASK,PT_EFLAGS(%esp)	# interrupts off (exception path) ?
	jz restore_all
	call preempt_schedule_irq
	jmp need_resched
END(resume_kernel)
#endif
	CFI_ENDPROC

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
	 * No need to follow this irqs on/off section: the syscall
	 * disabled irqs and here we enable it straight after entry:
	 */
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushl $(__USER_DS)
	CFI_ADJUST_CFA_OFFSET 4
	/*CFI_REL_OFFSET ss, 0*/
	pushl %ebp
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET esp, 0
	pushfl
	CFI_ADJUST_CFA_OFFSET 4
	pushl $(__USER_CS)
	CFI_ADJUST_CFA_OFFSET 4
	/*CFI_REL_OFFSET cs, 0*/
	/*
	 * Push current_thread_info()->sysenter_return to the stack.
	 * A tiny bit of offset fixup is necessary - 4*4 means the 4 words
	 * pushed above; +8 corresponds to copy_thread's esp0 setting.
	 */
	pushl (TI_sysenter_return-THREAD_SIZE+8+4*4)(%esp)
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET eip, 0

/*
 * Load the potential sixth argument from user stack.
 * Careful about security.
 */
	cmpl $__PAGE_OFFSET-3,%ebp
	jae syscall_fault
1:	movl (%ebp),%ebp
.section __ex_table,"a"
	.align 4
	.long 1b,syscall_fault
.previous

	pushl %eax
	CFI_ADJUST_CFA_OFFSET 4
	SAVE_ALL
	GET_THREAD_INFO(%ebp)

	/* Note, _TIF_SECCOMP is bit number 8, and so it needs testw and not testb */
	testw $(_TIF_SYSCALL_EMU|_TIF_SYSCALL_TRACE|_TIF_SECCOMP|_TIF_SYSCALL_AUDIT),TI_flags(%ebp)
	jnz syscall_trace_entry
	cmpl $(nr_syscalls), %eax
	jae syscall_badsys
	call *sys_call_table(,%eax,4)
	movl %eax,PT_EAX(%esp)
	LOCKDEP_SYS_EXIT
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF
	movl TI_flags(%ebp), %ecx
	testw $_TIF_ALLWORK_MASK, %cx
	jne syscall_exit_work
/* if something modifies registers it must also disable sysexit */
	movl PT_EIP(%esp), %edx
	movl PT_OLDESP(%esp), %ecx
	xorl %ebp,%ebp
	TRACE_IRQS_ON
1:	mov  PT_FS(%esp), %fs
	ENABLE_INTERRUPTS_SYSCALL_RET
	CFI_ENDPROC
.pushsection .fixup,"ax"
2:	movl $0,PT_FS(%esp)
	jmp 1b
.section __ex_table,"a"
	.align 4
	.long 1b,2b
.popsection
ENDPROC(ia32_sysenter_target)

	# system call handler stub
ENTRY(system_call)
	RING0_INT_FRAME			# can't unwind into user space anyway
	pushl %eax			# save orig_eax
	CFI_ADJUST_CFA_OFFSET 4
	SAVE_ALL
	GET_THREAD_INFO(%ebp)
					# system call tracing in operation / emulation
	/* Note, _TIF_SECCOMP is bit number 8, and so it needs testw and not testb */
	testw $(_TIF_SYSCALL_EMU|_TIF_SYSCALL_TRACE|_TIF_SECCOMP|_TIF_SYSCALL_AUDIT),TI_flags(%ebp)
	jnz syscall_trace_entry
	cmpl $(nr_syscalls), %eax
	jae syscall_badsys
syscall_call:
	call *sys_call_table(,%eax,4)
	movl %eax,PT_EAX(%esp)		# store the return value
syscall_exit:
	LOCKDEP_SYS_EXIT
	DISABLE_INTERRUPTS(CLBR_ANY)	# make sure we don't miss an interrupt
					# setting need_resched or sigpending
					# between sampling and the iret
	TRACE_IRQS_OFF
	testl $TF_MASK,PT_EFLAGS(%esp)	# If tracing set singlestep flag on exit
	jz no_singlestep
	orl $_TIF_SINGLESTEP,TI_flags(%ebp)
no_singlestep:
	movl TI_flags(%ebp), %ecx
	testw $_TIF_ALLWORK_MASK, %cx	# current->work
	jne syscall_exit_work

restore_all:
	movl PT_EFLAGS(%esp), %eax	# mix EFLAGS, SS and CS
	# Warning: PT_OLDSS(%esp) contains the wrong/random values if we
	# are returning to the kernel.
	# See comments in process.c:copy_thread() for details.
	movb PT_OLDSS(%esp), %ah
	movb PT_CS(%esp), %al
	andl $(VM_MASK | (SEGMENT_TI_MASK << 8) | SEGMENT_RPL_MASK), %eax
	cmpl $((SEGMENT_LDT << 8) | USER_RPL), %eax
	CFI_REMEMBER_STATE
	je ldt_ss			# returning to user-space with LDT SS
restore_nocheck:
	TRACE_IRQS_IRET
restore_nocheck_notrace:
	RESTORE_REGS
	addl $4, %esp			# skip orig_eax/error_code
	CFI_ADJUST_CFA_OFFSET -4
irq_return:
	INTERRUPT_RETURN
.section .fixup,"ax"
iret_exc:
	pushl $0			# no error code
	pushl $do_iret_error
	jmp error_code
.previous
.section __ex_table,"a"
	.align 4
	.long irq_return,iret_exc
.previous

	CFI_RESTORE_STATE
ldt_ss:
	larl PT_OLDSS(%esp), %eax
	jnz restore_nocheck
	testl $0x00400000, %eax		# returning to 32bit stack?
	jnz restore_nocheck		# allright, normal return

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

	/* If returning to userspace with 16bit stack,
	 * try to fix the higher word of ESP, as the CPU
	 * won't restore it.
	 * This is an "official" bug of all the x86-compatible
	 * CPUs, which we can try to work around to make
	 * dosemu and wine happy. */
	movl PT_OLDESP(%esp), %eax
	movl %esp, %edx
	call patch_espfix_desc
	pushl $__ESPFIX_SS
	CFI_ADJUST_CFA_OFFSET 4
	pushl %eax
	CFI_ADJUST_CFA_OFFSET 4
	DISABLE_INTERRUPTS(CLBR_EAX)
	TRACE_IRQS_OFF
	lss (%esp), %esp
	CFI_ADJUST_CFA_OFFSET -8
	jmp restore_nocheck
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
	testl $VM_MASK, PT_EFLAGS(%esp)
	movl %esp, %eax
	jne work_notifysig_v86		# returning to kernel-space or
					# vm86-space
	xorl %edx, %edx
	call do_notify_resume
	jmp resume_userspace_sig

	ALIGN
work_notifysig_v86:
	pushl %ecx			# save ti_flags for do_notify_resume
	CFI_ADJUST_CFA_OFFSET 4
	call save_v86_state		# %eax contains pt_regs pointer
	popl %ecx
	CFI_ADJUST_CFA_OFFSET -4
	movl %eax, %esp
#else
	movl %esp, %eax
#endif
	xorl %edx, %edx
	call do_notify_resume
	jmp resume_userspace_sig
END(work_pending)

	# perform syscall exit tracing
	ALIGN
syscall_trace_entry:
	movl $-ENOSYS,PT_EAX(%esp)
	movl %esp, %eax
	xorl %edx,%edx
	call do_syscall_trace
	cmpl $0, %eax
	jne resume_userspace		# ret != 0 -> running under PTRACE_SYSEMU,
					# so must skip actual syscall
	movl PT_ORIG_EAX(%esp), %eax
	cmpl $(nr_syscalls), %eax
	jnae syscall_call
	jmp syscall_exit
END(syscall_trace_entry)

	# perform syscall exit tracing
	ALIGN
syscall_exit_work:
	testb $(_TIF_SYSCALL_TRACE|_TIF_SYSCALL_AUDIT|_TIF_SINGLESTEP), %cl
	jz work_pending
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_ANY)	# could let do_syscall_trace() call
					# schedule() instead
	movl %esp, %eax
	movl $1, %edx
	call do_syscall_trace
	jmp resume_userspace
END(syscall_exit_work)
	CFI_ENDPROC

	RING0_INT_FRAME			# can't unwind into user space anyway
syscall_fault:
	pushl %eax			# save orig_eax
	CFI_ADJUST_CFA_OFFSET 4
	SAVE_ALL
	GET_THREAD_INFO(%ebp)
	movl $-EFAULT,PT_EAX(%esp)
	jmp resume_userspace
END(syscall_fault)

syscall_badsys:
	movl $-ENOSYS,PT_EAX(%esp)
	jmp resume_userspace
END(syscall_badsys)
	CFI_ENDPROC

#define FIXUP_ESPFIX_STACK \
	/* since we are on a wrong stack, we cant make it a C code :( */ \
	PER_CPU(gdt_page, %ebx); \
	GET_DESC_BASE(GDT_ENTRY_ESPFIX_SS, %ebx, %eax, %ax, %al, %ah); \
	addl %esp, %eax; \
	pushl $__KERNEL_DS; \
	CFI_ADJUST_CFA_OFFSET 4; \
	pushl %eax; \
	CFI_ADJUST_CFA_OFFSET 4; \
	lss (%esp), %esp; \
	CFI_ADJUST_CFA_OFFSET -8;
#define UNWIND_ESPFIX_STACK \
	movl %ss, %eax; \
	/* see if on espfix stack */ \
	cmpw $__ESPFIX_SS, %ax; \
	jne 27f; \
	movl $__KERNEL_DS, %eax; \
	movl %eax, %ds; \
	movl %eax, %es; \
	/* switch to normal stack */ \
	FIXUP_ESPFIX_STACK; \
27:;

/*
 * Build the entry stubs and pointer table with
 * some assembler magic.
 */
.section .rodata,"a"
ENTRY(interrupt)
.text

ENTRY(irq_entries_start)
	RING0_INT_FRAME
vector=0
.rept NR_IRQS
	ALIGN
 .if vector
	CFI_ADJUST_CFA_OFFSET -4
 .endif
1:	pushl $~(vector)
	CFI_ADJUST_CFA_OFFSET 4
	jmp common_interrupt
 .previous
	.long 1b
 .text
vector=vector+1
.endr
END(irq_entries_start)

.previous
END(interrupt)
.previous

/*
 * the CPU automatically disables interrupts when executing an IRQ vector,
 * so IRQ-flags tracing has to follow that:
 */
	ALIGN
common_interrupt:
	SAVE_ALL
	TRACE_IRQS_OFF
	movl %esp,%eax
	call do_IRQ
	jmp ret_from_intr
ENDPROC(common_interrupt)
	CFI_ENDPROC

#define BUILD_INTERRUPT(name, nr)	\
ENTRY(name)				\
	RING0_INT_FRAME;		\
	pushl $~(nr);			\
	CFI_ADJUST_CFA_OFFSET 4;	\
	SAVE_ALL;			\
	TRACE_IRQS_OFF			\
	movl %esp,%eax;			\
	call smp_##name;		\
	jmp ret_from_intr;		\
	CFI_ENDPROC;			\
ENDPROC(name)

/* The include is where all of the SMP etc. interrupts come from */
#include "entry_arch.h"

KPROBE_ENTRY(page_fault)
	RING0_EC_FRAME
	pushl $do_page_fault
	CFI_ADJUST_CFA_OFFSET 4
	ALIGN
error_code:
	/* the function address is in %fs's slot on the stack */
	pushl %es
	CFI_ADJUST_CFA_OFFSET 4
	/*CFI_REL_OFFSET es, 0*/
	pushl %ds
	CFI_ADJUST_CFA_OFFSET 4
	/*CFI_REL_OFFSET ds, 0*/
	pushl %eax
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET eax, 0
	pushl %ebp
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ebp, 0
	pushl %edi
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET edi, 0
	pushl %esi
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET esi, 0
	pushl %edx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET edx, 0
	pushl %ecx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ecx, 0
	pushl %ebx
	CFI_ADJUST_CFA_OFFSET 4
	CFI_REL_OFFSET ebx, 0
	cld
	pushl %fs
	CFI_ADJUST_CFA_OFFSET 4
	/*CFI_REL_OFFSET fs, 0*/
	movl $(__KERNEL_PERCPU), %ecx
	movl %ecx, %fs
	UNWIND_ESPFIX_STACK
	popl %ecx
	CFI_ADJUST_CFA_OFFSET -4
	/*CFI_REGISTER es, ecx*/
	movl PT_FS(%esp), %edi		# get the function address
	movl PT_ORIG_EAX(%esp), %edx	# get the error code
	movl $-1, PT_ORIG_EAX(%esp)	# no syscall to restart
	mov  %ecx, PT_FS(%esp)
	/*CFI_REL_OFFSET fs, ES*/
	movl $(__USER_DS), %ecx
	movl %ecx, %ds
	movl %ecx, %es
	movl %esp,%eax			# pt_regs pointer
	call *%edi
	jmp ret_from_exception
	CFI_ENDPROC
KPROBE_END(page_fault)

ENTRY(coprocessor_error)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_coprocessor_error
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(coprocessor_error)

ENTRY(simd_coprocessor_error)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_simd_coprocessor_error
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(simd_coprocessor_error)

ENTRY(device_not_available)
	RING0_INT_FRAME
	pushl $-1			# mark this as an int
	CFI_ADJUST_CFA_OFFSET 4
	SAVE_ALL
	GET_CR0_INTO_EAX
	testl $0x4, %eax		# EM (math emulation bit)
	jne device_not_available_emulate
	preempt_stop(CLBR_ANY)
	call math_state_restore
	jmp ret_from_exception
device_not_available_emulate:
	pushl $0			# temporary storage for ORIG_EIP
	CFI_ADJUST_CFA_OFFSET 4
	call math_emulate
	addl $4, %esp
	CFI_ADJUST_CFA_OFFSET -4
	jmp ret_from_exception
	CFI_ENDPROC
END(device_not_available)

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
#define FIX_STACK(offset, ok, label)		\
	cmpw $__KERNEL_CS,4(%esp);		\
	jne ok;					\
label:						\
	movl TSS_sysenter_sp0+offset(%esp),%esp;	\
	CFI_DEF_CFA esp, 0;			\
	CFI_UNDEFINED eip;			\
	pushfl;					\
	CFI_ADJUST_CFA_OFFSET 4;		\
	pushl $__KERNEL_CS;			\
	CFI_ADJUST_CFA_OFFSET 4;		\
	pushl $sysenter_past_esp;		\
	CFI_ADJUST_CFA_OFFSET 4;		\
	CFI_REL_OFFSET eip, 0

KPROBE_ENTRY(debug)
	RING0_INT_FRAME
	cmpl $ia32_sysenter_target,(%esp)
	jne debug_stack_correct
	FIX_STACK(12, debug_stack_correct, debug_esp_fix_insn)
debug_stack_correct:
	pushl $-1			# mark this as an int
	CFI_ADJUST_CFA_OFFSET 4
	SAVE_ALL
	xorl %edx,%edx			# error code 0
	movl %esp,%eax			# pt_regs pointer
	call do_debug
	jmp ret_from_exception
	CFI_ENDPROC
KPROBE_END(debug)

/*
 * NMI is doubly nasty. It can happen _while_ we're handling
 * a debug fault, and the debug fault hasn't yet been able to
 * clear up the stack. So we first check whether we got  an
 * NMI on the sysenter entry path, but after that we need to
 * check whether we got an NMI on the debug path where the debug
 * fault happened on the sysenter path.
 */
KPROBE_ENTRY(nmi)
	RING0_INT_FRAME
	pushl %eax
	CFI_ADJUST_CFA_OFFSET 4
	movl %ss, %eax
	cmpw $__ESPFIX_SS, %ax
	popl %eax
	CFI_ADJUST_CFA_OFFSET -4
	je nmi_espfix_stack
	cmpl $ia32_sysenter_target,(%esp)
	je nmi_stack_fixup
	pushl %eax
	CFI_ADJUST_CFA_OFFSET 4
	movl %esp,%eax
	/* Do not access memory above the end of our stack page,
	 * it might not exist.
	 */
	andl $(THREAD_SIZE-1),%eax
	cmpl $(THREAD_SIZE-20),%eax
	popl %eax
	CFI_ADJUST_CFA_OFFSET -4
	jae nmi_stack_correct
	cmpl $ia32_sysenter_target,12(%esp)
	je nmi_debug_stack_check
nmi_stack_correct:
	/* We have a RING0_INT_FRAME here */
	pushl %eax
	CFI_ADJUST_CFA_OFFSET 4
	SAVE_ALL
	xorl %edx,%edx		# zero error code
	movl %esp,%eax		# pt_regs pointer
	call do_nmi
	jmp restore_nocheck_notrace
	CFI_ENDPROC

nmi_stack_fixup:
	RING0_INT_FRAME
	FIX_STACK(12,nmi_stack_correct, 1)
	jmp nmi_stack_correct

nmi_debug_stack_check:
	/* We have a RING0_INT_FRAME here */
	cmpw $__KERNEL_CS,16(%esp)
	jne nmi_stack_correct
	cmpl $debug,(%esp)
	jb nmi_stack_correct
	cmpl $debug_esp_fix_insn,(%esp)
	ja nmi_stack_correct
	FIX_STACK(24,nmi_stack_correct, 1)
	jmp nmi_stack_correct

nmi_espfix_stack:
	/* We have a RING0_INT_FRAME here.
	 *
	 * create the pointer to lss back
	 */
	pushl %ss
	CFI_ADJUST_CFA_OFFSET 4
	pushl %esp
	CFI_ADJUST_CFA_OFFSET 4
	addw $4, (%esp)
	/* copy the iret frame of 12 bytes */
	.rept 3
	pushl 16(%esp)
	CFI_ADJUST_CFA_OFFSET 4
	.endr
	pushl %eax
	CFI_ADJUST_CFA_OFFSET 4
	SAVE_ALL
	FIXUP_ESPFIX_STACK		# %eax == %esp
	xorl %edx,%edx			# zero error code
	call do_nmi
	RESTORE_REGS
	lss 12+4(%esp), %esp		# back to espfix stack
	CFI_ADJUST_CFA_OFFSET -24
	jmp irq_return
	CFI_ENDPROC
KPROBE_END(nmi)

#ifdef CONFIG_PARAVIRT
ENTRY(native_iret)
	iret
.section __ex_table,"a"
	.align 4
	.long native_iret, iret_exc
.previous
END(native_iret)

ENTRY(native_irq_enable_syscall_ret)
	sti
	sysexit
END(native_irq_enable_syscall_ret)
#endif

KPROBE_ENTRY(int3)
	RING0_INT_FRAME
	pushl $-1			# mark this as an int
	CFI_ADJUST_CFA_OFFSET 4
	SAVE_ALL
	xorl %edx,%edx		# zero error code
	movl %esp,%eax		# pt_regs pointer
	call do_int3
	jmp ret_from_exception
	CFI_ENDPROC
KPROBE_END(int3)

ENTRY(overflow)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_overflow
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(overflow)

ENTRY(bounds)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_bounds
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(bounds)

ENTRY(invalid_op)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_invalid_op
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(invalid_op)

ENTRY(coprocessor_segment_overrun)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_coprocessor_segment_overrun
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(coprocessor_segment_overrun)

ENTRY(invalid_TSS)
	RING0_EC_FRAME
	pushl $do_invalid_TSS
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(invalid_TSS)

ENTRY(segment_not_present)
	RING0_EC_FRAME
	pushl $do_segment_not_present
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(segment_not_present)

ENTRY(stack_segment)
	RING0_EC_FRAME
	pushl $do_stack_segment
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(stack_segment)

KPROBE_ENTRY(general_protection)
	RING0_EC_FRAME
	pushl $do_general_protection
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
KPROBE_END(general_protection)

ENTRY(alignment_check)
	RING0_EC_FRAME
	pushl $do_alignment_check
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(alignment_check)

ENTRY(divide_error)
	RING0_INT_FRAME
	pushl $0			# no error code
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_divide_error
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(divide_error)

#ifdef CONFIG_X86_MCE
ENTRY(machine_check)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl machine_check_vector
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(machine_check)
#endif

ENTRY(spurious_interrupt_bug)
	RING0_INT_FRAME
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
	pushl $do_spurious_interrupt_bug
	CFI_ADJUST_CFA_OFFSET 4
	jmp error_code
	CFI_ENDPROC
END(spurious_interrupt_bug)

ENTRY(kernel_thread_helper)
	pushl $0		# fake return address for unwinder
	CFI_STARTPROC
	movl %edx,%eax
	push %edx
	CFI_ADJUST_CFA_OFFSET 4
	call *%ebx
	push %eax
	CFI_ADJUST_CFA_OFFSET 4
	call do_exit
	CFI_ENDPROC
ENDPROC(kernel_thread_helper)

#ifdef CONFIG_XEN
ENTRY(xen_hypervisor_callback)
	CFI_STARTPROC
	pushl $0
	CFI_ADJUST_CFA_OFFSET 4
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

	call xen_iret_crit_fixup

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
	pushl %eax
	CFI_ADJUST_CFA_OFFSET 4
	movl $1,%eax
1:	mov 4(%esp),%ds
2:	mov 8(%esp),%es
3:	mov 12(%esp),%fs
4:	mov 16(%esp),%gs
	testl %eax,%eax
	popl %eax
	CFI_ADJUST_CFA_OFFSET -4
	lea 16(%esp),%esp
	CFI_ADJUST_CFA_OFFSET -16
	jz 5f
	addl $16,%esp
	jmp iret_exc		# EAX != 0 => Category 2 (Bad IRET)
5:	pushl $0		# EAX == 0 => Category 1 (Bad segment)
	CFI_ADJUST_CFA_OFFSET 4
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
.section __ex_table,"a"
	.align 4
	.long 1b,6b
	.long 2b,7b
	.long 3b,8b
	.long 4b,9b
.previous
ENDPROC(xen_failsafe_callback)

#endif	/* CONFIG_XEN */

.section .rodata,"a"
#include "syscall_table_32.S"

syscall_table_size=(.-sys_call_table)
