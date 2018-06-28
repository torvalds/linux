/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 1991,1992  Linus Torvalds
 *
 * entry_32.S contains the system-call and low-level fault and trap handling routines.
 *
 * Stack layout while running C code:
 *	ptrace needs to have all registers on the stack.
 *	If the order here is changed, it needs to be
 *	updated in fork.c:copy_process(), signal.c:do_signal(),
 *	ptrace.c and ptrace.h
 *
 *	 0(%esp) - %ebx
 *	 4(%esp) - %ecx
 *	 8(%esp) - %edx
 *	 C(%esp) - %esi
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
 */

#include <linux/linkage.h>
#include <linux/err.h>
#include <asm/thread_info.h>
#include <asm/irqflags.h>
#include <asm/errno.h>
#include <asm/segment.h>
#include <asm/smp.h>
#include <asm/percpu.h>
#include <asm/processor-flags.h>
#include <asm/irq_vectors.h>
#include <asm/cpufeatures.h>
#include <asm/alternative-asm.h>
#include <asm/asm.h>
#include <asm/smap.h>
#include <asm/frame.h>
#include <asm/nospec-branch.h>

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
# define preempt_stop(clobbers)	DISABLE_INTERRUPTS(clobbers); TRACE_IRQS_OFF
#else
# define preempt_stop(clobbers)
# define resume_kernel		restore_all
#endif

.macro TRACE_IRQS_IRET
#ifdef CONFIG_TRACE_IRQFLAGS
	testl	$X86_EFLAGS_IF, PT_EFLAGS(%esp)     # interrupts off?
	jz	1f
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
	pushl	$0
.endm
.macro POP_GS pop=0
	addl	$(4 + \pop), %esp
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
	pushl	%gs
.endm

.macro POP_GS pop=0
98:	popl	%gs
  .if \pop <> 0
	add	$\pop, %esp
  .endif
.endm
.macro POP_GS_EX
.pushsection .fixup, "ax"
99:	movl	$0, (%esp)
	jmp	98b
.popsection
	_ASM_EXTABLE(98b, 99b)
.endm

.macro PTGS_TO_GS
98:	mov	PT_GS(%esp), %gs
.endm
.macro PTGS_TO_GS_EX
.pushsection .fixup, "ax"
99:	movl	$0, PT_GS(%esp)
	jmp	98b
.popsection
	_ASM_EXTABLE(98b, 99b)
.endm

.macro GS_TO_REG reg
	movl	%gs, \reg
.endm
.macro REG_TO_PTGS reg
	movl	\reg, PT_GS(%esp)
.endm
.macro SET_KERNEL_GS reg
	movl	$(__KERNEL_STACK_CANARY), \reg
	movl	\reg, %gs
.endm

#endif /* CONFIG_X86_32_LAZY_GS */

.macro SAVE_ALL pt_regs_ax=%eax
	cld
	PUSH_GS
	pushl	%fs
	pushl	%es
	pushl	%ds
	pushl	\pt_regs_ax
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%edx
	pushl	%ecx
	pushl	%ebx
	movl	$(__USER_DS), %edx
	movl	%edx, %ds
	movl	%edx, %es
	movl	$(__KERNEL_PERCPU), %edx
	movl	%edx, %fs
	SET_KERNEL_GS %edx
.endm

/*
 * This is a sneaky trick to help the unwinder find pt_regs on the stack.  The
 * frame pointer is replaced with an encoded pointer to pt_regs.  The encoding
 * is just clearing the MSB, which makes it an invalid stack address and is also
 * a signal to the unwinder that it's a pt_regs pointer in disguise.
 *
 * NOTE: This macro must be used *after* SAVE_ALL because it corrupts the
 * original rbp.
 */
.macro ENCODE_FRAME_POINTER
#ifdef CONFIG_FRAME_POINTER
	mov %esp, %ebp
	andl $0x7fffffff, %ebp
#endif
.endm

.macro RESTORE_INT_REGS
	popl	%ebx
	popl	%ecx
	popl	%edx
	popl	%esi
	popl	%edi
	popl	%ebp
	popl	%eax
.endm

.macro RESTORE_REGS pop=0
	RESTORE_INT_REGS
1:	popl	%ds
2:	popl	%es
3:	popl	%fs
	POP_GS \pop
.pushsection .fixup, "ax"
4:	movl	$0, (%esp)
	jmp	1b
5:	movl	$0, (%esp)
	jmp	2b
6:	movl	$0, (%esp)
	jmp	3b
.popsection
	_ASM_EXTABLE(1b, 4b)
	_ASM_EXTABLE(2b, 5b)
	_ASM_EXTABLE(3b, 6b)
	POP_GS_EX
.endm

/*
 * %eax: prev task
 * %edx: next task
 */
ENTRY(__switch_to_asm)
	/*
	 * Save callee-saved registers
	 * This must match the order in struct inactive_task_frame
	 */
	pushl	%ebp
	pushl	%ebx
	pushl	%edi
	pushl	%esi

	/* switch stack */
	movl	%esp, TASK_threadsp(%eax)
	movl	TASK_threadsp(%edx), %esp

#ifdef CONFIG_STACKPROTECTOR
	movl	TASK_stack_canary(%edx), %ebx
	movl	%ebx, PER_CPU_VAR(stack_canary)+stack_canary_offset
#endif

#ifdef CONFIG_RETPOLINE
	/*
	 * When switching from a shallower to a deeper call stack
	 * the RSB may either underflow or use entries populated
	 * with userspace addresses. On CPUs where those concerns
	 * exist, overwrite the RSB with entries which capture
	 * speculative execution to prevent attack.
	 */
	FILL_RETURN_BUFFER %ebx, RSB_CLEAR_LOOPS, X86_FEATURE_RSB_CTXSW
#endif

	/* restore callee-saved registers */
	popl	%esi
	popl	%edi
	popl	%ebx
	popl	%ebp

	jmp	__switch_to
END(__switch_to_asm)

/*
 * The unwinder expects the last frame on the stack to always be at the same
 * offset from the end of the page, which allows it to validate the stack.
 * Calling schedule_tail() directly would break that convention because its an
 * asmlinkage function so its argument has to be pushed on the stack.  This
 * wrapper creates a proper "end of stack" frame header before the call.
 */
ENTRY(schedule_tail_wrapper)
	FRAME_BEGIN

	pushl	%eax
	call	schedule_tail
	popl	%eax

	FRAME_END
	ret
ENDPROC(schedule_tail_wrapper)
/*
 * A newly forked process directly context switches into this address.
 *
 * eax: prev task we switched from
 * ebx: kernel thread func (NULL for user thread)
 * edi: kernel thread arg
 */
ENTRY(ret_from_fork)
	call	schedule_tail_wrapper

	testl	%ebx, %ebx
	jnz	1f		/* kernel threads are uncommon */

2:
	/* When we fork, we trace the syscall return in the child, too. */
	movl    %esp, %eax
	call    syscall_return_slowpath
	jmp     restore_all

	/* kernel thread */
1:	movl	%edi, %eax
	CALL_NOSPEC %ebx
	/*
	 * A kernel thread is allowed to return here after successfully
	 * calling do_execve().  Exit to userspace to complete the execve()
	 * syscall.
	 */
	movl	$0, PT_EAX(%esp)
	jmp	2b
END(ret_from_fork)

/*
 * Return to user mode is not as complex as all this looks,
 * but we want the default path for a system call return to
 * go as quickly as possible which is why some of this is
 * less clear than it otherwise should be.
 */

	# userspace resumption stub bypassing syscall exit tracing
	ALIGN
ret_from_exception:
	preempt_stop(CLBR_ANY)
ret_from_intr:
#ifdef CONFIG_VM86
	movl	PT_EFLAGS(%esp), %eax		# mix EFLAGS and CS
	movb	PT_CS(%esp), %al
	andl	$(X86_EFLAGS_VM | SEGMENT_RPL_MASK), %eax
#else
	/*
	 * We can be coming here from child spawned by kernel_thread().
	 */
	movl	PT_CS(%esp), %eax
	andl	$SEGMENT_RPL_MASK, %eax
#endif
	cmpl	$USER_RPL, %eax
	jb	resume_kernel			# not returning to v8086 or userspace

ENTRY(resume_userspace)
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF
	movl	%esp, %eax
	call	prepare_exit_to_usermode
	jmp	restore_all
END(ret_from_exception)

#ifdef CONFIG_PREEMPT
ENTRY(resume_kernel)
	DISABLE_INTERRUPTS(CLBR_ANY)
.Lneed_resched:
	cmpl	$0, PER_CPU_VAR(__preempt_count)
	jnz	restore_all
	testl	$X86_EFLAGS_IF, PT_EFLAGS(%esp)	# interrupts off (exception path) ?
	jz	restore_all
	call	preempt_schedule_irq
	jmp	.Lneed_resched
END(resume_kernel)
#endif

GLOBAL(__begin_SYSENTER_singlestep_region)
/*
 * All code from here through __end_SYSENTER_singlestep_region is subject
 * to being single-stepped if a user program sets TF and executes SYSENTER.
 * There is absolutely nothing that we can do to prevent this from happening
 * (thanks Intel!).  To keep our handling of this situation as simple as
 * possible, we handle TF just like AC and NT, except that our #DB handler
 * will ignore all of the single-step traps generated in this range.
 */

#ifdef CONFIG_XEN
/*
 * Xen doesn't set %esp to be precisely what the normal SYSENTER
 * entry point expects, so fix it up before using the normal path.
 */
ENTRY(xen_sysenter_target)
	addl	$5*4, %esp			/* remove xen-provided frame */
	jmp	.Lsysenter_past_esp
#endif

/*
 * 32-bit SYSENTER entry.
 *
 * 32-bit system calls through the vDSO's __kernel_vsyscall enter here
 * if X86_FEATURE_SEP is available.  This is the preferred system call
 * entry on 32-bit systems.
 *
 * The SYSENTER instruction, in principle, should *only* occur in the
 * vDSO.  In practice, a small number of Android devices were shipped
 * with a copy of Bionic that inlined a SYSENTER instruction.  This
 * never happened in any of Google's Bionic versions -- it only happened
 * in a narrow range of Intel-provided versions.
 *
 * SYSENTER loads SS, ESP, CS, and EIP from previously programmed MSRs.
 * IF and VM in RFLAGS are cleared (IOW: interrupts are off).
 * SYSENTER does not save anything on the stack,
 * and does not save old EIP (!!!), ESP, or EFLAGS.
 *
 * To avoid losing track of EFLAGS.VM (and thus potentially corrupting
 * user and/or vm86 state), we explicitly disable the SYSENTER
 * instruction in vm86 mode by reprogramming the MSRs.
 *
 * Arguments:
 * eax  system call number
 * ebx  arg1
 * ecx  arg2
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * ebp  user stack
 * 0(%ebp) arg6
 */
ENTRY(entry_SYSENTER_32)
	movl	TSS_sysenter_sp0(%esp), %esp
.Lsysenter_past_esp:
	pushl	$__USER_DS		/* pt_regs->ss */
	pushl	%ebp			/* pt_regs->sp (stashed in bp) */
	pushfl				/* pt_regs->flags (except IF = 0) */
	orl	$X86_EFLAGS_IF, (%esp)	/* Fix IF */
	pushl	$__USER_CS		/* pt_regs->cs */
	pushl	$0			/* pt_regs->ip = 0 (placeholder) */
	pushl	%eax			/* pt_regs->orig_ax */
	SAVE_ALL pt_regs_ax=$-ENOSYS	/* save rest */

	/*
	 * SYSENTER doesn't filter flags, so we need to clear NT, AC
	 * and TF ourselves.  To save a few cycles, we can check whether
	 * either was set instead of doing an unconditional popfq.
	 * This needs to happen before enabling interrupts so that
	 * we don't get preempted with NT set.
	 *
	 * If TF is set, we will single-step all the way to here -- do_debug
	 * will ignore all the traps.  (Yes, this is slow, but so is
	 * single-stepping in general.  This allows us to avoid having
	 * a more complicated code to handle the case where a user program
	 * forces us to single-step through the SYSENTER entry code.)
	 *
	 * NB.: .Lsysenter_fix_flags is a label with the code under it moved
	 * out-of-line as an optimization: NT is unlikely to be set in the
	 * majority of the cases and instead of polluting the I$ unnecessarily,
	 * we're keeping that code behind a branch which will predict as
	 * not-taken and therefore its instructions won't be fetched.
	 */
	testl	$X86_EFLAGS_NT|X86_EFLAGS_AC|X86_EFLAGS_TF, PT_EFLAGS(%esp)
	jnz	.Lsysenter_fix_flags
.Lsysenter_flags_fixed:

	/*
	 * User mode is traced as though IRQs are on, and SYSENTER
	 * turned them off.
	 */
	TRACE_IRQS_OFF

	movl	%esp, %eax
	call	do_fast_syscall_32
	/* XEN PV guests always use IRET path */
	ALTERNATIVE "testl %eax, %eax; jz .Lsyscall_32_done", \
		    "jmp .Lsyscall_32_done", X86_FEATURE_XENPV

/* Opportunistic SYSEXIT */
	TRACE_IRQS_ON			/* User mode traces as IRQs on. */
	movl	PT_EIP(%esp), %edx	/* pt_regs->ip */
	movl	PT_OLDESP(%esp), %ecx	/* pt_regs->sp */
1:	mov	PT_FS(%esp), %fs
	PTGS_TO_GS
	popl	%ebx			/* pt_regs->bx */
	addl	$2*4, %esp		/* skip pt_regs->cx and pt_regs->dx */
	popl	%esi			/* pt_regs->si */
	popl	%edi			/* pt_regs->di */
	popl	%ebp			/* pt_regs->bp */
	popl	%eax			/* pt_regs->ax */

	/*
	 * Restore all flags except IF. (We restore IF separately because
	 * STI gives a one-instruction window in which we won't be interrupted,
	 * whereas POPF does not.)
	 */
	addl	$PT_EFLAGS-PT_DS, %esp	/* point esp at pt_regs->flags */
	btr	$X86_EFLAGS_IF_BIT, (%esp)
	popfl

	/*
	 * Return back to the vDSO, which will pop ecx and edx.
	 * Don't bother with DS and ES (they already contain __USER_DS).
	 */
	sti
	sysexit

.pushsection .fixup, "ax"
2:	movl	$0, PT_FS(%esp)
	jmp	1b
.popsection
	_ASM_EXTABLE(1b, 2b)
	PTGS_TO_GS_EX

.Lsysenter_fix_flags:
	pushl	$X86_EFLAGS_FIXED
	popfl
	jmp	.Lsysenter_flags_fixed
GLOBAL(__end_SYSENTER_singlestep_region)
ENDPROC(entry_SYSENTER_32)

/*
 * 32-bit legacy system call entry.
 *
 * 32-bit x86 Linux system calls traditionally used the INT $0x80
 * instruction.  INT $0x80 lands here.
 *
 * This entry point can be used by any 32-bit perform system calls.
 * Instances of INT $0x80 can be found inline in various programs and
 * libraries.  It is also used by the vDSO's __kernel_vsyscall
 * fallback for hardware that doesn't support a faster entry method.
 * Restarted 32-bit system calls also fall back to INT $0x80
 * regardless of what instruction was originally used to do the system
 * call.  (64-bit programs can use INT $0x80 as well, but they can
 * only run on 64-bit kernels and therefore land in
 * entry_INT80_compat.)
 *
 * This is considered a slow path.  It is not used by most libc
 * implementations on modern hardware except during process startup.
 *
 * Arguments:
 * eax  system call number
 * ebx  arg1
 * ecx  arg2
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * ebp  arg6
 */
ENTRY(entry_INT80_32)
	ASM_CLAC
	pushl	%eax			/* pt_regs->orig_ax */
	SAVE_ALL pt_regs_ax=$-ENOSYS	/* save rest */

	/*
	 * User mode is traced as though IRQs are on, and the interrupt gate
	 * turned them off.
	 */
	TRACE_IRQS_OFF

	movl	%esp, %eax
	call	do_int80_syscall_32
.Lsyscall_32_done:

restore_all:
	TRACE_IRQS_IRET
.Lrestore_all_notrace:
#ifdef CONFIG_X86_ESPFIX32
	ALTERNATIVE	"jmp .Lrestore_nocheck", "", X86_BUG_ESPFIX

	movl	PT_EFLAGS(%esp), %eax		# mix EFLAGS, SS and CS
	/*
	 * Warning: PT_OLDSS(%esp) contains the wrong/random values if we
	 * are returning to the kernel.
	 * See comments in process.c:copy_thread() for details.
	 */
	movb	PT_OLDSS(%esp), %ah
	movb	PT_CS(%esp), %al
	andl	$(X86_EFLAGS_VM | (SEGMENT_TI_MASK << 8) | SEGMENT_RPL_MASK), %eax
	cmpl	$((SEGMENT_LDT << 8) | USER_RPL), %eax
	je .Lldt_ss				# returning to user-space with LDT SS
#endif
.Lrestore_nocheck:
	RESTORE_REGS 4				# skip orig_eax/error_code
.Lirq_return:
	/*
	 * ARCH_HAS_MEMBARRIER_SYNC_CORE rely on IRET core serialization
	 * when returning from IPI handler and when returning from
	 * scheduler to user-space.
	 */
	INTERRUPT_RETURN

.section .fixup, "ax"
ENTRY(iret_exc	)
	pushl	$0				# no error code
	pushl	$do_iret_error
	jmp	common_exception
.previous
	_ASM_EXTABLE(.Lirq_return, iret_exc)

#ifdef CONFIG_X86_ESPFIX32
.Lldt_ss:
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
	mov	%esp, %edx			/* load kernel esp */
	mov	PT_OLDESP(%esp), %eax		/* load userspace esp */
	mov	%dx, %ax			/* eax: new kernel esp */
	sub	%eax, %edx			/* offset (low word is 0) */
	shr	$16, %edx
	mov	%dl, GDT_ESPFIX_SS + 4		/* bits 16..23 */
	mov	%dh, GDT_ESPFIX_SS + 7		/* bits 24..31 */
	pushl	$__ESPFIX_SS
	pushl	%eax				/* new kernel esp */
	/*
	 * Disable interrupts, but do not irqtrace this section: we
	 * will soon execute iret and the tracer was already set to
	 * the irqstate after the IRET:
	 */
	DISABLE_INTERRUPTS(CLBR_ANY)
	lss	(%esp), %esp			/* switch to espfix segment */
	jmp	.Lrestore_nocheck
#endif
ENDPROC(entry_INT80_32)

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
	mov	GDT_ESPFIX_SS + 4, %al /* bits 16..23 */
	mov	GDT_ESPFIX_SS + 7, %ah /* bits 24..31 */
	shl	$16, %eax
	addl	%esp, %eax			/* the adjusted stack pointer */
	pushl	$__KERNEL_DS
	pushl	%eax
	lss	(%esp), %esp			/* switch to the normal stack segment */
#endif
.endm
.macro UNWIND_ESPFIX_STACK
#ifdef CONFIG_X86_ESPFIX32
	movl	%ss, %eax
	/* see if on espfix stack */
	cmpw	$__ESPFIX_SS, %ax
	jne	27f
	movl	$__KERNEL_DS, %eax
	movl	%eax, %ds
	movl	%eax, %es
	/* switch to normal stack */
	FIXUP_ESPFIX_STACK
27:
#endif
.endm

/*
 * Build the entry stubs with some assembler magic.
 * We pack 1 stub into every 8-byte block.
 */
	.align 8
ENTRY(irq_entries_start)
    vector=FIRST_EXTERNAL_VECTOR
    .rept (FIRST_SYSTEM_VECTOR - FIRST_EXTERNAL_VECTOR)
	pushl	$(~vector+0x80)			/* Note: always in signed byte range */
    vector=vector+1
	jmp	common_interrupt
	.align	8
    .endr
END(irq_entries_start)

/*
 * the CPU automatically disables interrupts when executing an IRQ vector,
 * so IRQ-flags tracing has to follow that:
 */
	.p2align CONFIG_X86_L1_CACHE_SHIFT
common_interrupt:
	ASM_CLAC
	addl	$-0x80, (%esp)			/* Adjust vector into the [-256, -1] range */
	SAVE_ALL
	ENCODE_FRAME_POINTER
	TRACE_IRQS_OFF
	movl	%esp, %eax
	call	do_IRQ
	jmp	ret_from_intr
ENDPROC(common_interrupt)

#define BUILD_INTERRUPT3(name, nr, fn)	\
ENTRY(name)				\
	ASM_CLAC;			\
	pushl	$~(nr);			\
	SAVE_ALL;			\
	ENCODE_FRAME_POINTER;		\
	TRACE_IRQS_OFF			\
	movl	%esp, %eax;		\
	call	fn;			\
	jmp	ret_from_intr;		\
ENDPROC(name)

#define BUILD_INTERRUPT(name, nr)		\
	BUILD_INTERRUPT3(name, nr, smp_##name);	\

/* The include is where all of the SMP etc. interrupts come from */
#include <asm/entry_arch.h>

ENTRY(coprocessor_error)
	ASM_CLAC
	pushl	$0
	pushl	$do_coprocessor_error
	jmp	common_exception
END(coprocessor_error)

ENTRY(simd_coprocessor_error)
	ASM_CLAC
	pushl	$0
#ifdef CONFIG_X86_INVD_BUG
	/* AMD 486 bug: invd from userspace calls exception 19 instead of #GP */
	ALTERNATIVE "pushl	$do_general_protection",	\
		    "pushl	$do_simd_coprocessor_error",	\
		    X86_FEATURE_XMM
#else
	pushl	$do_simd_coprocessor_error
#endif
	jmp	common_exception
END(simd_coprocessor_error)

ENTRY(device_not_available)
	ASM_CLAC
	pushl	$-1				# mark this as an int
	pushl	$do_device_not_available
	jmp	common_exception
END(device_not_available)

#ifdef CONFIG_PARAVIRT
ENTRY(native_iret)
	iret
	_ASM_EXTABLE(native_iret, iret_exc)
END(native_iret)
#endif

ENTRY(overflow)
	ASM_CLAC
	pushl	$0
	pushl	$do_overflow
	jmp	common_exception
END(overflow)

ENTRY(bounds)
	ASM_CLAC
	pushl	$0
	pushl	$do_bounds
	jmp	common_exception
END(bounds)

ENTRY(invalid_op)
	ASM_CLAC
	pushl	$0
	pushl	$do_invalid_op
	jmp	common_exception
END(invalid_op)

ENTRY(coprocessor_segment_overrun)
	ASM_CLAC
	pushl	$0
	pushl	$do_coprocessor_segment_overrun
	jmp	common_exception
END(coprocessor_segment_overrun)

ENTRY(invalid_TSS)
	ASM_CLAC
	pushl	$do_invalid_TSS
	jmp	common_exception
END(invalid_TSS)

ENTRY(segment_not_present)
	ASM_CLAC
	pushl	$do_segment_not_present
	jmp	common_exception
END(segment_not_present)

ENTRY(stack_segment)
	ASM_CLAC
	pushl	$do_stack_segment
	jmp	common_exception
END(stack_segment)

ENTRY(alignment_check)
	ASM_CLAC
	pushl	$do_alignment_check
	jmp	common_exception
END(alignment_check)

ENTRY(divide_error)
	ASM_CLAC
	pushl	$0				# no error code
	pushl	$do_divide_error
	jmp	common_exception
END(divide_error)

#ifdef CONFIG_X86_MCE
ENTRY(machine_check)
	ASM_CLAC
	pushl	$0
	pushl	machine_check_vector
	jmp	common_exception
END(machine_check)
#endif

ENTRY(spurious_interrupt_bug)
	ASM_CLAC
	pushl	$0
	pushl	$do_spurious_interrupt_bug
	jmp	common_exception
END(spurious_interrupt_bug)

#ifdef CONFIG_XEN
ENTRY(xen_hypervisor_callback)
	pushl	$-1				/* orig_ax = -1 => not a system call */
	SAVE_ALL
	ENCODE_FRAME_POINTER
	TRACE_IRQS_OFF

	/*
	 * Check to see if we got the event in the critical
	 * region in xen_iret_direct, after we've reenabled
	 * events and checked for pending events.  This simulates
	 * iret instruction's behaviour where it delivers a
	 * pending interrupt when enabling interrupts:
	 */
	movl	PT_EIP(%esp), %eax
	cmpl	$xen_iret_start_crit, %eax
	jb	1f
	cmpl	$xen_iret_end_crit, %eax
	jae	1f

	jmp	xen_iret_crit_fixup

ENTRY(xen_do_upcall)
1:	mov	%esp, %eax
	call	xen_evtchn_do_upcall
#ifndef CONFIG_PREEMPT
	call	xen_maybe_preempt_hcall
#endif
	jmp	ret_from_intr
ENDPROC(xen_hypervisor_callback)

/*
 * Hypervisor uses this for application faults while it executes.
 * We get here for two reasons:
 *  1. Fault while reloading DS, ES, FS or GS
 *  2. Fault while executing IRET
 * Category 1 we fix up by reattempting the load, and zeroing the segment
 * register if the load fails.
 * Category 2 we fix up by jumping to do_iret_error. We cannot use the
 * normal Linux return path in this case because if we use the IRET hypercall
 * to pop the stack frame we end up in an infinite loop of failsafe callbacks.
 * We distinguish between categories by maintaining a status value in EAX.
 */
ENTRY(xen_failsafe_callback)
	pushl	%eax
	movl	$1, %eax
1:	mov	4(%esp), %ds
2:	mov	8(%esp), %es
3:	mov	12(%esp), %fs
4:	mov	16(%esp), %gs
	/* EAX == 0 => Category 1 (Bad segment)
	   EAX != 0 => Category 2 (Bad IRET) */
	testl	%eax, %eax
	popl	%eax
	lea	16(%esp), %esp
	jz	5f
	jmp	iret_exc
5:	pushl	$-1				/* orig_ax = -1 => not a system call */
	SAVE_ALL
	ENCODE_FRAME_POINTER
	jmp	ret_from_exception

.section .fixup, "ax"
6:	xorl	%eax, %eax
	movl	%eax, 4(%esp)
	jmp	1b
7:	xorl	%eax, %eax
	movl	%eax, 8(%esp)
	jmp	2b
8:	xorl	%eax, %eax
	movl	%eax, 12(%esp)
	jmp	3b
9:	xorl	%eax, %eax
	movl	%eax, 16(%esp)
	jmp	4b
.previous
	_ASM_EXTABLE(1b, 6b)
	_ASM_EXTABLE(2b, 7b)
	_ASM_EXTABLE(3b, 8b)
	_ASM_EXTABLE(4b, 9b)
ENDPROC(xen_failsafe_callback)

BUILD_INTERRUPT3(xen_hvm_callback_vector, HYPERVISOR_CALLBACK_VECTOR,
		 xen_evtchn_do_upcall)

#endif /* CONFIG_XEN */

#if IS_ENABLED(CONFIG_HYPERV)

BUILD_INTERRUPT3(hyperv_callback_vector, HYPERVISOR_CALLBACK_VECTOR,
		 hyperv_vector_handler)

BUILD_INTERRUPT3(hyperv_reenlightenment_vector, HYPERV_REENLIGHTENMENT_VECTOR,
		 hyperv_reenlightenment_intr)

BUILD_INTERRUPT3(hv_stimer0_callback_vector, HYPERV_STIMER0_VECTOR,
		 hv_stimer0_vector_handler)

#endif /* CONFIG_HYPERV */

ENTRY(page_fault)
	ASM_CLAC
	pushl	$do_page_fault
	ALIGN
	jmp common_exception
END(page_fault)

common_exception:
	/* the function address is in %gs's slot on the stack */
	pushl	%fs
	pushl	%es
	pushl	%ds
	pushl	%eax
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%edx
	pushl	%ecx
	pushl	%ebx
	ENCODE_FRAME_POINTER
	cld
	movl	$(__KERNEL_PERCPU), %ecx
	movl	%ecx, %fs
	UNWIND_ESPFIX_STACK
	GS_TO_REG %ecx
	movl	PT_GS(%esp), %edi		# get the function address
	movl	PT_ORIG_EAX(%esp), %edx		# get the error code
	movl	$-1, PT_ORIG_EAX(%esp)		# no syscall to restart
	REG_TO_PTGS %ecx
	SET_KERNEL_GS %ecx
	movl	$(__USER_DS), %ecx
	movl	%ecx, %ds
	movl	%ecx, %es
	TRACE_IRQS_OFF
	movl	%esp, %eax			# pt_regs pointer
	CALL_NOSPEC %edi
	jmp	ret_from_exception
END(common_exception)

ENTRY(debug)
	/*
	 * #DB can happen at the first instruction of
	 * entry_SYSENTER_32 or in Xen's SYSENTER prologue.  If this
	 * happens, then we will be running on a very small stack.  We
	 * need to detect this condition and switch to the thread
	 * stack before calling any C code at all.
	 *
	 * If you edit this code, keep in mind that NMIs can happen in here.
	 */
	ASM_CLAC
	pushl	$-1				# mark this as an int
	SAVE_ALL
	ENCODE_FRAME_POINTER
	xorl	%edx, %edx			# error code 0
	movl	%esp, %eax			# pt_regs pointer

	/* Are we currently on the SYSENTER stack? */
	movl	PER_CPU_VAR(cpu_entry_area), %ecx
	addl	$CPU_ENTRY_AREA_entry_stack + SIZEOF_entry_stack, %ecx
	subl	%eax, %ecx	/* ecx = (end of entry_stack) - esp */
	cmpl	$SIZEOF_entry_stack, %ecx
	jb	.Ldebug_from_sysenter_stack

	TRACE_IRQS_OFF
	call	do_debug
	jmp	ret_from_exception

.Ldebug_from_sysenter_stack:
	/* We're on the SYSENTER stack.  Switch off. */
	movl	%esp, %ebx
	movl	PER_CPU_VAR(cpu_current_top_of_stack), %esp
	TRACE_IRQS_OFF
	call	do_debug
	movl	%ebx, %esp
	jmp	ret_from_exception
END(debug)

/*
 * NMI is doubly nasty.  It can happen on the first instruction of
 * entry_SYSENTER_32 (just like #DB), but it can also interrupt the beginning
 * of the #DB handler even if that #DB in turn hit before entry_SYSENTER_32
 * switched stacks.  We handle both conditions by simply checking whether we
 * interrupted kernel code running on the SYSENTER stack.
 */
ENTRY(nmi)
	ASM_CLAC
#ifdef CONFIG_X86_ESPFIX32
	pushl	%eax
	movl	%ss, %eax
	cmpw	$__ESPFIX_SS, %ax
	popl	%eax
	je	.Lnmi_espfix_stack
#endif

	pushl	%eax				# pt_regs->orig_ax
	SAVE_ALL
	ENCODE_FRAME_POINTER
	xorl	%edx, %edx			# zero error code
	movl	%esp, %eax			# pt_regs pointer

	/* Are we currently on the SYSENTER stack? */
	movl	PER_CPU_VAR(cpu_entry_area), %ecx
	addl	$CPU_ENTRY_AREA_entry_stack + SIZEOF_entry_stack, %ecx
	subl	%eax, %ecx	/* ecx = (end of entry_stack) - esp */
	cmpl	$SIZEOF_entry_stack, %ecx
	jb	.Lnmi_from_sysenter_stack

	/* Not on SYSENTER stack. */
	call	do_nmi
	jmp	.Lrestore_all_notrace

.Lnmi_from_sysenter_stack:
	/*
	 * We're on the SYSENTER stack.  Switch off.  No one (not even debug)
	 * is using the thread stack right now, so it's safe for us to use it.
	 */
	movl	%esp, %ebx
	movl	PER_CPU_VAR(cpu_current_top_of_stack), %esp
	call	do_nmi
	movl	%ebx, %esp
	jmp	.Lrestore_all_notrace

#ifdef CONFIG_X86_ESPFIX32
.Lnmi_espfix_stack:
	/*
	 * create the pointer to lss back
	 */
	pushl	%ss
	pushl	%esp
	addl	$4, (%esp)
	/* copy the iret frame of 12 bytes */
	.rept 3
	pushl	16(%esp)
	.endr
	pushl	%eax
	SAVE_ALL
	ENCODE_FRAME_POINTER
	FIXUP_ESPFIX_STACK			# %eax == %esp
	xorl	%edx, %edx			# zero error code
	call	do_nmi
	RESTORE_REGS
	lss	12+4(%esp), %esp		# back to espfix stack
	jmp	.Lirq_return
#endif
END(nmi)

ENTRY(int3)
	ASM_CLAC
	pushl	$-1				# mark this as an int
	SAVE_ALL
	ENCODE_FRAME_POINTER
	TRACE_IRQS_OFF
	xorl	%edx, %edx			# zero error code
	movl	%esp, %eax			# pt_regs pointer
	call	do_int3
	jmp	ret_from_exception
END(int3)

ENTRY(general_protection)
	pushl	$do_general_protection
	jmp	common_exception
END(general_protection)

#ifdef CONFIG_KVM_GUEST
ENTRY(async_page_fault)
	ASM_CLAC
	pushl	$do_async_page_fault
	jmp	common_exception
END(async_page_fault)
#endif

ENTRY(rewind_stack_do_exit)
	/* Prevent any naive code from trying to unwind to our caller. */
	xorl	%ebp, %ebp

	movl	PER_CPU_VAR(cpu_current_top_of_stack), %esi
	leal	-TOP_OF_KERNEL_STACK_PADDING-PTREGS_SIZE(%esi), %esp

	call	do_exit
1:	jmp 1b
END(rewind_stack_do_exit)
