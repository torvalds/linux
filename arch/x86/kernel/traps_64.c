/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'entry.S'.
 */
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/utsname.h>
#include <linux/kdebug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/unwind.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kexec.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/nmi.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/io.h>

#if defined(CONFIG_EDAC)
#include <linux/edac.h>
#endif

#include <asm/stacktrace.h>
#include <asm/processor.h>
#include <asm/debugreg.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/unwind.h>
#include <asm/desc.h>
#include <asm/i387.h>
#include <asm/pgalloc.h>
#include <asm/proto.h>
#include <asm/pda.h>
#include <asm/traps.h>

#include <mach_traps.h>

static int ignore_nmis;

static inline void conditional_sti(struct pt_regs *regs)
{
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_enable();
}

static inline void preempt_conditional_sti(struct pt_regs *regs)
{
	inc_preempt_count();
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_enable();
}

static inline void preempt_conditional_cli(struct pt_regs *regs)
{
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_disable();
	/* Make sure to not schedule here because we could be running
	   on an exception stack. */
	dec_preempt_count();
}

static void __kprobes
do_trap(int trapnr, int signr, char *str, struct pt_regs *regs,
	long error_code, siginfo_t *info)
{
	struct task_struct *tsk = current;

	if (!user_mode(regs))
		goto kernel_trap;

	/*
	 * We want error_code and trap_no set for userspace faults and
	 * kernelspace faults which result in die(), but not
	 * kernelspace faults which are fixed up.  die() gives the
	 * process no chance to handle the signal and notice the
	 * kernel fault information, so that won't result in polluting
	 * the information about previously queued, but not yet
	 * delivered, faults.  See also do_general_protection below.
	 */
	tsk->thread.error_code = error_code;
	tsk->thread.trap_no = trapnr;

	if (show_unhandled_signals && unhandled_signal(tsk, signr) &&
	    printk_ratelimit()) {
		printk(KERN_INFO
		       "%s[%d] trap %s ip:%lx sp:%lx error:%lx",
		       tsk->comm, tsk->pid, str,
		       regs->ip, regs->sp, error_code);
		print_vma_addr(" in ", regs->ip);
		printk("\n");
	}

	if (info)
		force_sig_info(signr, info, tsk);
	else
		force_sig(signr, tsk);
	return;

kernel_trap:
	if (!fixup_exception(regs)) {
		tsk->thread.error_code = error_code;
		tsk->thread.trap_no = trapnr;
		die(str, regs, error_code);
	}
	return;
}

#define DO_ERROR(trapnr, signr, str, name)				\
asmlinkage void do_##name(struct pt_regs *regs, long error_code)	\
{									\
	if (notify_die(DIE_TRAP, str, regs, error_code, trapnr, signr)	\
							== NOTIFY_STOP)	\
		return;							\
	conditional_sti(regs);						\
	do_trap(trapnr, signr, str, regs, error_code, NULL);		\
}

#define DO_ERROR_INFO(trapnr, signr, str, name, sicode, siaddr)		\
asmlinkage void do_##name(struct pt_regs *regs, long error_code)	\
{									\
	siginfo_t info;							\
	info.si_signo = signr;						\
	info.si_errno = 0;						\
	info.si_code = sicode;						\
	info.si_addr = (void __user *)siaddr;				\
	if (notify_die(DIE_TRAP, str, regs, error_code, trapnr, signr)	\
							== NOTIFY_STOP)	\
		return;							\
	conditional_sti(regs);						\
	do_trap(trapnr, signr, str, regs, error_code, &info);		\
}

DO_ERROR_INFO(0, SIGFPE, "divide error", divide_error, FPE_INTDIV, regs->ip)
DO_ERROR(4, SIGSEGV, "overflow", overflow)
DO_ERROR(5, SIGSEGV, "bounds", bounds)
DO_ERROR_INFO(6, SIGILL, "invalid opcode", invalid_op, ILL_ILLOPN, regs->ip)
DO_ERROR(9, SIGFPE, "coprocessor segment overrun", coprocessor_segment_overrun)
DO_ERROR(10, SIGSEGV, "invalid TSS", invalid_TSS)
DO_ERROR(11, SIGBUS, "segment not present", segment_not_present)
DO_ERROR_INFO(17, SIGBUS, "alignment check", alignment_check, BUS_ADRALN, 0)

/* Runs on IST stack */
asmlinkage void do_stack_segment(struct pt_regs *regs, long error_code)
{
	if (notify_die(DIE_TRAP, "stack segment", regs, error_code,
			12, SIGBUS) == NOTIFY_STOP)
		return;
	preempt_conditional_sti(regs);
	do_trap(12, SIGBUS, "stack segment", regs, error_code, NULL);
	preempt_conditional_cli(regs);
}

asmlinkage void do_double_fault(struct pt_regs *regs, long error_code)
{
	static const char str[] = "double fault";
	struct task_struct *tsk = current;

	/* Return not checked because double check cannot be ignored */
	notify_die(DIE_TRAP, str, regs, error_code, 8, SIGSEGV);

	tsk->thread.error_code = error_code;
	tsk->thread.trap_no = 8;

	/* This is always a kernel trap and never fixable (and thus must
	   never return). */
	for (;;)
		die(str, regs, error_code);
}

asmlinkage void __kprobes
do_general_protection(struct pt_regs *regs, long error_code)
{
	struct task_struct *tsk;

	conditional_sti(regs);

	tsk = current;
	if (!user_mode(regs))
		goto gp_in_kernel;

	tsk->thread.error_code = error_code;
	tsk->thread.trap_no = 13;

	if (show_unhandled_signals && unhandled_signal(tsk, SIGSEGV) &&
			printk_ratelimit()) {
		printk(KERN_INFO
			"%s[%d] general protection ip:%lx sp:%lx error:%lx",
			tsk->comm, tsk->pid,
			regs->ip, regs->sp, error_code);
		print_vma_addr(" in ", regs->ip);
		printk("\n");
	}

	force_sig(SIGSEGV, tsk);
	return;

gp_in_kernel:
	if (fixup_exception(regs))
		return;

	tsk->thread.error_code = error_code;
	tsk->thread.trap_no = 13;
	if (notify_die(DIE_GPF, "general protection fault", regs,
				error_code, 13, SIGSEGV) == NOTIFY_STOP)
		return;
	die("general protection fault", regs, error_code);
}

static notrace __kprobes void
mem_parity_error(unsigned char reason, struct pt_regs *regs)
{
	printk(KERN_EMERG "Uhhuh. NMI received for unknown reason %02x.\n",
		reason);
	printk(KERN_EMERG "You have some hardware problem, likely on the PCI bus.\n");

#if defined(CONFIG_EDAC)
	if (edac_handler_set()) {
		edac_atomic_assert_error();
		return;
	}
#endif

	if (panic_on_unrecovered_nmi)
		panic("NMI: Not continuing");

	printk(KERN_EMERG "Dazed and confused, but trying to continue\n");

	/* Clear and disable the memory parity error line. */
	reason = (reason & 0xf) | 4;
	outb(reason, 0x61);
}

static notrace __kprobes void
io_check_error(unsigned char reason, struct pt_regs *regs)
{
	printk("NMI: IOCK error (debug interrupt?)\n");
	show_registers(regs);

	/* Re-enable the IOCK line, wait for a few seconds */
	reason = (reason & 0xf) | 8;
	outb(reason, 0x61);
	mdelay(2000);
	reason &= ~8;
	outb(reason, 0x61);
}

static notrace __kprobes void
unknown_nmi_error(unsigned char reason, struct pt_regs *regs)
{
	if (notify_die(DIE_NMIUNKNOWN, "nmi", regs, reason, 2, SIGINT) ==
			NOTIFY_STOP)
		return;
	printk(KERN_EMERG "Uhhuh. NMI received for unknown reason %02x.\n",
		reason);
	printk(KERN_EMERG "Do you have a strange power saving mode enabled?\n");

	if (panic_on_unrecovered_nmi)
		panic("NMI: Not continuing");

	printk(KERN_EMERG "Dazed and confused, but trying to continue\n");
}

/* Runs on IST stack. This code must keep interrupts off all the time.
   Nested NMIs are prevented by the CPU. */
asmlinkage notrace __kprobes void default_do_nmi(struct pt_regs *regs)
{
	unsigned char reason = 0;
	int cpu;

	cpu = smp_processor_id();

	/* Only the BSP gets external NMIs from the system. */
	if (!cpu)
		reason = get_nmi_reason();

	if (!(reason & 0xc0)) {
		if (notify_die(DIE_NMI_IPI, "nmi_ipi", regs, reason, 2, SIGINT)
								== NOTIFY_STOP)
			return;
		/*
		 * Ok, so this is none of the documented NMI sources,
		 * so it must be the NMI watchdog.
		 */
		if (nmi_watchdog_tick(regs, reason))
			return;
		if (!do_nmi_callback(regs, cpu))
			unknown_nmi_error(reason, regs);

		return;
	}
	if (notify_die(DIE_NMI, "nmi", regs, reason, 2, SIGINT) == NOTIFY_STOP)
		return;

	/* AK: following checks seem to be broken on modern chipsets. FIXME */
	if (reason & 0x80)
		mem_parity_error(reason, regs);
	if (reason & 0x40)
		io_check_error(reason, regs);
}

asmlinkage notrace __kprobes void
do_nmi(struct pt_regs *regs, long error_code)
{
	nmi_enter();

	add_pda(__nmi_count, 1);

	if (!ignore_nmis)
		default_do_nmi(regs);

	nmi_exit();
}

void stop_nmi(void)
{
	acpi_nmi_disable();
	ignore_nmis++;
}

void restart_nmi(void)
{
	ignore_nmis--;
	acpi_nmi_enable();
}

/* runs on IST stack. */
asmlinkage void __kprobes do_int3(struct pt_regs *regs, long error_code)
{
	if (notify_die(DIE_INT3, "int3", regs, error_code, 3, SIGTRAP)
			== NOTIFY_STOP)
		return;

	preempt_conditional_sti(regs);
	do_trap(3, SIGTRAP, "int3", regs, error_code, NULL);
	preempt_conditional_cli(regs);
}

/* Help handler running on IST stack to switch back to user stack
   for scheduling or signal handling. The actual stack switch is done in
   entry.S */
asmlinkage __kprobes struct pt_regs *sync_regs(struct pt_regs *eregs)
{
	struct pt_regs *regs = eregs;
	/* Did already sync */
	if (eregs == (struct pt_regs *)eregs->sp)
		;
	/* Exception from user space */
	else if (user_mode(eregs))
		regs = task_pt_regs(current);
	/* Exception from kernel and interrupts are enabled. Move to
	   kernel process stack. */
	else if (eregs->flags & X86_EFLAGS_IF)
		regs = (struct pt_regs *)(eregs->sp -= sizeof(struct pt_regs));
	if (eregs != regs)
		*regs = *eregs;
	return regs;
}

/* runs on IST stack. */
asmlinkage void __kprobes do_debug(struct pt_regs *regs,
				   unsigned long error_code)
{
	struct task_struct *tsk = current;
	unsigned long condition;
	siginfo_t info;

	get_debugreg(condition, 6);

	/*
	 * The processor cleared BTF, so don't mark that we need it set.
	 */
	clear_tsk_thread_flag(tsk, TIF_DEBUGCTLMSR);
	tsk->thread.debugctlmsr = 0;

	if (notify_die(DIE_DEBUG, "debug", regs, condition, error_code,
						SIGTRAP) == NOTIFY_STOP)
		return;

	preempt_conditional_sti(regs);

	/* Mask out spurious debug traps due to lazy DR7 setting */
	if (condition & (DR_TRAP0|DR_TRAP1|DR_TRAP2|DR_TRAP3)) {
		if (!tsk->thread.debugreg7)
			goto clear_dr7;
	}

	tsk->thread.debugreg6 = condition;

	/*
	 * Single-stepping through TF: make sure we ignore any events in
	 * kernel space (but re-enable TF when returning to user mode).
	 */
	if (condition & DR_STEP) {
		if (!user_mode(regs))
			goto clear_TF_reenable;
	}

	/* Ok, finally something we can handle */
	tsk->thread.trap_no = 1;
	tsk->thread.error_code = error_code;
	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = get_si_code(condition);
	info.si_addr = user_mode(regs) ? (void __user *)regs->ip : NULL;
	force_sig_info(SIGTRAP, &info, tsk);

clear_dr7:
	set_debugreg(0, 7);
	preempt_conditional_cli(regs);
	return;

clear_TF_reenable:
	set_tsk_thread_flag(tsk, TIF_SINGLESTEP);
	regs->flags &= ~X86_EFLAGS_TF;
	preempt_conditional_cli(regs);
	return;
}

static int kernel_math_error(struct pt_regs *regs, const char *str, int trapnr)
{
	if (fixup_exception(regs))
		return 1;

	notify_die(DIE_GPF, str, regs, 0, trapnr, SIGFPE);
	/* Illegal floating point operation in the kernel */
	current->thread.trap_no = trapnr;
	die(str, regs, 0);
	return 0;
}

/*
 * Note that we play around with the 'TS' bit in an attempt to get
 * the correct behaviour even in the presence of the asynchronous
 * IRQ13 behaviour
 */
asmlinkage void do_coprocessor_error(struct pt_regs *regs)
{
	void __user *ip = (void __user *)(regs->ip);
	struct task_struct *task;
	siginfo_t info;
	unsigned short cwd, swd;

	conditional_sti(regs);
	if (!user_mode(regs) &&
	    kernel_math_error(regs, "kernel x87 math error", 16))
		return;

	/*
	 * Save the info for the exception handler and clear the error.
	 */
	task = current;
	save_init_fpu(task);
	task->thread.trap_no = 16;
	task->thread.error_code = 0;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_code = __SI_FAULT;
	info.si_addr = ip;
	/*
	 * (~cwd & swd) will mask out exceptions that are not set to unmasked
	 * status.  0x3f is the exception bits in these regs, 0x200 is the
	 * C1 reg you need in case of a stack fault, 0x040 is the stack
	 * fault bit.  We should only be taking one exception at a time,
	 * so if this combination doesn't produce any single exception,
	 * then we have a bad program that isn't synchronizing its FPU usage
	 * and it will suffer the consequences since we won't be able to
	 * fully reproduce the context of the exception
	 */
	cwd = get_fpu_cwd(task);
	swd = get_fpu_swd(task);
	switch (swd & ~cwd & 0x3f) {
	case 0x000: /* No unmasked exception */
	default: /* Multiple exceptions */
		break;
	case 0x001: /* Invalid Op */
		/*
		 * swd & 0x240 == 0x040: Stack Underflow
		 * swd & 0x240 == 0x240: Stack Overflow
		 * User must clear the SF bit (0x40) if set
		 */
		info.si_code = FPE_FLTINV;
		break;
	case 0x002: /* Denormalize */
	case 0x010: /* Underflow */
		info.si_code = FPE_FLTUND;
		break;
	case 0x004: /* Zero Divide */
		info.si_code = FPE_FLTDIV;
		break;
	case 0x008: /* Overflow */
		info.si_code = FPE_FLTOVF;
		break;
	case 0x020: /* Precision */
		info.si_code = FPE_FLTRES;
		break;
	}
	force_sig_info(SIGFPE, &info, task);
}

asmlinkage void bad_intr(void)
{
	printk("bad interrupt");
}

asmlinkage void do_simd_coprocessor_error(struct pt_regs *regs)
{
	void __user *ip = (void __user *)(regs->ip);
	struct task_struct *task;
	siginfo_t info;
	unsigned short mxcsr;

	conditional_sti(regs);
	if (!user_mode(regs) &&
			kernel_math_error(regs, "kernel simd math error", 19))
		return;

	/*
	 * Save the info for the exception handler and clear the error.
	 */
	task = current;
	save_init_fpu(task);
	task->thread.trap_no = 19;
	task->thread.error_code = 0;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_code = __SI_FAULT;
	info.si_addr = ip;
	/*
	 * The SIMD FPU exceptions are handled a little differently, as there
	 * is only a single status/control register.  Thus, to determine which
	 * unmasked exception was caught we must mask the exception mask bits
	 * at 0x1f80, and then use these to mask the exception bits at 0x3f.
	 */
	mxcsr = get_fpu_mxcsr(task);
	switch (~((mxcsr & 0x1f80) >> 7) & (mxcsr & 0x3f)) {
	case 0x000:
	default:
		break;
	case 0x001: /* Invalid Op */
		info.si_code = FPE_FLTINV;
		break;
	case 0x002: /* Denormalize */
	case 0x010: /* Underflow */
		info.si_code = FPE_FLTUND;
		break;
	case 0x004: /* Zero Divide */
		info.si_code = FPE_FLTDIV;
		break;
	case 0x008: /* Overflow */
		info.si_code = FPE_FLTOVF;
		break;
	case 0x020: /* Precision */
		info.si_code = FPE_FLTRES;
		break;
	}
	force_sig_info(SIGFPE, &info, task);
}

asmlinkage void do_spurious_interrupt_bug(struct pt_regs *regs)
{
}

asmlinkage void __attribute__((weak)) smp_thermal_interrupt(void)
{
}

asmlinkage void __attribute__((weak)) mce_threshold_interrupt(void)
{
}

/*
 * 'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 *
 * Careful.. There are problems with IBM-designed IRQ13 behaviour.
 * Don't touch unless you *really* know how it works.
 */
asmlinkage void math_state_restore(void)
{
	struct task_struct *me = current;

	if (!used_math()) {
		local_irq_enable();
		/*
		 * does a slab alloc which can sleep
		 */
		if (init_fpu(me)) {
			/*
			 * ran out of memory!
			 */
			do_group_exit(SIGKILL);
			return;
		}
		local_irq_disable();
	}

	clts();				/* Allow maths ops (or we recurse) */
	/*
	 * Paranoid restore. send a SIGSEGV if we fail to restore the state.
	 */
	if (unlikely(restore_fpu_checking(me))) {
		stts();
		force_sig(SIGSEGV, me);
		return;
	}
	task_thread_info(me)->status |= TS_USEDFPU;
	me->fpu_counter++;
}
EXPORT_SYMBOL_GPL(math_state_restore);

void __init trap_init(void)
{
	set_intr_gate(0, &divide_error);
	set_intr_gate_ist(1, &debug, DEBUG_STACK);
	set_intr_gate_ist(2, &nmi, NMI_STACK);
	/* int3 can be called from all */
	set_system_gate_ist(3, &int3, DEBUG_STACK);
	/* int4 can be called from all */
	set_system_gate(4, &overflow);
	set_intr_gate(5, &bounds);
	set_intr_gate(6, &invalid_op);
	set_intr_gate(7, &device_not_available);
	set_intr_gate_ist(8, &double_fault, DOUBLEFAULT_STACK);
	set_intr_gate(9, &coprocessor_segment_overrun);
	set_intr_gate(10, &invalid_TSS);
	set_intr_gate(11, &segment_not_present);
	set_intr_gate_ist(12, &stack_segment, STACKFAULT_STACK);
	set_intr_gate(13, &general_protection);
	set_intr_gate(14, &page_fault);
	set_intr_gate(15, &spurious_interrupt_bug);
	set_intr_gate(16, &coprocessor_error);
	set_intr_gate(17, &alignment_check);
#ifdef CONFIG_X86_MCE
	set_intr_gate_ist(18, &machine_check, MCE_STACK);
#endif
	set_intr_gate(19, &simd_coprocessor_error);

#ifdef CONFIG_IA32_EMULATION
	set_system_gate(IA32_SYSCALL_VECTOR, ia32_syscall);
#endif
	/*
	 * Should be a barrier for any external CPU state:
	 */
	cpu_init();
}
