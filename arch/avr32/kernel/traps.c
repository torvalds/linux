/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bug.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#include <asm/addrspace.h>
#include <asm/mmu_context.h>
#include <asm/ocd.h>
#include <asm/sysreg.h>
#include <asm/traps.h>

static DEFINE_SPINLOCK(die_lock);

void NORET_TYPE die(const char *str, struct pt_regs *regs, long err)
{
	static int die_counter;

	console_verbose();
	spin_lock_irq(&die_lock);
	bust_spinlocks(1);

	printk(KERN_ALERT "Oops: %s, sig: %ld [#%d]\n" KERN_EMERG,
	       str, err, ++die_counter);
#ifdef CONFIG_PREEMPT
	printk("PREEMPT ");
#endif
#ifdef CONFIG_FRAME_POINTER
	printk("FRAME_POINTER ");
#endif
	if (current_cpu_data.features & AVR32_FEATURE_OCD) {
		unsigned long did = ocd_read(DID);
		printk("chip: 0x%03lx:0x%04lx rev %lu\n",
		       (did >> 1) & 0x7ff,
		       (did >> 12) & 0x7fff,
		       (did >> 28) & 0xf);
	} else {
		printk("cpu: arch %u r%u / core %u r%u\n",
		       current_cpu_data.arch_type,
		       current_cpu_data.arch_revision,
		       current_cpu_data.cpu_type,
		       current_cpu_data.cpu_revision);
	}

	print_modules();
	show_regs_log_lvl(regs, KERN_EMERG);
	show_stack_log_lvl(current, regs->sp, regs, KERN_EMERG);
	bust_spinlocks(0);
	add_taint(TAINT_DIE);
	spin_unlock_irq(&die_lock);

	if (in_interrupt())
		panic("Fatal exception in interrupt");

	if (panic_on_oops)
		panic("Fatal exception");

	do_exit(err);
}

void _exception(long signr, struct pt_regs *regs, int code,
		unsigned long addr)
{
	siginfo_t info;

	if (!user_mode(regs))
		die("Unhandled exception in kernel mode", regs, signr);

	memset(&info, 0, sizeof(info));
	info.si_signo = signr;
	info.si_code = code;
	info.si_addr = (void __user *)addr;
	force_sig_info(signr, &info, current);

	/*
	 * Init gets no signals that it doesn't have a handler for.
	 * That's all very well, but if it has caused a synchronous
	 * exception and we ignore the resulting signal, it will just
	 * generate the same exception over and over again and we get
	 * nowhere.  Better to kill it and let the kernel panic.
	 */
	if (is_global_init(current)) {
		__sighandler_t handler;

		spin_lock_irq(&current->sighand->siglock);
		handler = current->sighand->action[signr-1].sa.sa_handler;
		spin_unlock_irq(&current->sighand->siglock);
		if (handler == SIG_DFL) {
			/* init has generated a synchronous exception
			   and it doesn't have a handler for the signal */
			printk(KERN_CRIT "init has generated signal %ld "
			       "but has no handler for it\n", signr);
			do_exit(signr);
		}
	}
}

asmlinkage void do_nmi(unsigned long ecr, struct pt_regs *regs)
{
	int ret;

	nmi_enter();

	ret = notify_die(DIE_NMI, "NMI", regs, 0, ecr, SIGINT);
	switch (ret) {
	case NOTIFY_OK:
	case NOTIFY_STOP:
		return;
	case NOTIFY_BAD:
		die("Fatal Non-Maskable Interrupt", regs, SIGINT);
	default:
		break;
	}

	printk(KERN_ALERT "Got NMI, but nobody cared. Disabling...\n");
	nmi_disable();
}

asmlinkage void do_critical_exception(unsigned long ecr, struct pt_regs *regs)
{
	die("Critical exception", regs, SIGKILL);
}

asmlinkage void do_address_exception(unsigned long ecr, struct pt_regs *regs)
{
	_exception(SIGBUS, regs, BUS_ADRALN, regs->pc);
}

/* This way of handling undefined instructions is stolen from ARM */
static LIST_HEAD(undef_hook);
static DEFINE_SPINLOCK(undef_lock);

void register_undef_hook(struct undef_hook *hook)
{
	spin_lock_irq(&undef_lock);
	list_add(&hook->node, &undef_hook);
	spin_unlock_irq(&undef_lock);
}

void unregister_undef_hook(struct undef_hook *hook)
{
	spin_lock_irq(&undef_lock);
	list_del(&hook->node);
	spin_unlock_irq(&undef_lock);
}

static int do_cop_absent(u32 insn)
{
	int cop_nr;
	u32 cpucr;

	if ((insn & 0xfdf00000) == 0xf1900000)
		/* LDC0 */
		cop_nr = 0;
	else
		cop_nr = (insn >> 13) & 0x7;

	/* Try enabling the coprocessor */
	cpucr = sysreg_read(CPUCR);
	cpucr |= (1 << (24 + cop_nr));
	sysreg_write(CPUCR, cpucr);

	cpucr = sysreg_read(CPUCR);
	if (!(cpucr & (1 << (24 + cop_nr))))
		return -ENODEV;

	return 0;
}

int is_valid_bugaddr(unsigned long pc)
{
	unsigned short opcode;

	if (pc < PAGE_OFFSET)
		return 0;
	if (probe_kernel_address((u16 *)pc, opcode))
		return 0;

	return opcode == AVR32_BUG_OPCODE;
}

asmlinkage void do_illegal_opcode(unsigned long ecr, struct pt_regs *regs)
{
	u32 insn;
	struct undef_hook *hook;
	void __user *pc;
	long code;

	if (!user_mode(regs) && (ecr == ECR_ILLEGAL_OPCODE)) {
		enum bug_trap_type type;

		type = report_bug(regs->pc, regs);
		switch (type) {
		case BUG_TRAP_TYPE_NONE:
			break;
		case BUG_TRAP_TYPE_WARN:
			regs->pc += 2;
			return;
		case BUG_TRAP_TYPE_BUG:
			die("Kernel BUG", regs, SIGKILL);
		}
	}

	local_irq_enable();

	if (user_mode(regs)) {
		pc = (void __user *)instruction_pointer(regs);
		if (get_user(insn, (u32 __user *)pc))
			goto invalid_area;

		if (ecr == ECR_COPROC_ABSENT && !do_cop_absent(insn))
			return;

		spin_lock_irq(&undef_lock);
		list_for_each_entry(hook, &undef_hook, node) {
			if ((insn & hook->insn_mask) == hook->insn_val) {
				if (hook->fn(regs, insn) == 0) {
					spin_unlock_irq(&undef_lock);
					return;
				}
			}
		}
		spin_unlock_irq(&undef_lock);
	}

	switch (ecr) {
	case ECR_PRIVILEGE_VIOLATION:
		code = ILL_PRVOPC;
		break;
	case ECR_COPROC_ABSENT:
		code = ILL_COPROC;
		break;
	default:
		code = ILL_ILLOPC;
		break;
	}

	_exception(SIGILL, regs, code, regs->pc);
	return;

invalid_area:
	_exception(SIGSEGV, regs, SEGV_MAPERR, regs->pc);
}

asmlinkage void do_fpe(unsigned long ecr, struct pt_regs *regs)
{
	/* We have no FPU yet */
	_exception(SIGILL, regs, ILL_COPROC, regs->pc);
}


void __init trap_init(void)
{

}
