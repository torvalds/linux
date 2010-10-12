/*
 * HW exception handling
 *
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008 PetaLogix
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 */

/*
 * This file handles the architecture-dependent parts of hardware exceptions
 */

#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/module.h>

#include <asm/exceptions.h>
#include <asm/entry.h>		/* For KM CPU var */
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <asm/current.h>

#define MICROBLAZE_ILL_OPCODE_EXCEPTION	0x02
#define MICROBLAZE_IBUS_EXCEPTION	0x03
#define MICROBLAZE_DBUS_EXCEPTION	0x04
#define MICROBLAZE_DIV_ZERO_EXCEPTION	0x05
#define MICROBLAZE_FPU_EXCEPTION	0x06
#define MICROBLAZE_PRIVILEGED_EXCEPTION	0x07

static DEFINE_SPINLOCK(die_lock);

void die(const char *str, struct pt_regs *fp, long err)
{
	console_verbose();
	spin_lock_irq(&die_lock);
	printk(KERN_WARNING "Oops: %s, sig: %ld\n", str, err);
	show_regs(fp);
	spin_unlock_irq(&die_lock);
	/* do_exit() should take care of panic'ing from an interrupt
	 * context so we don't handle it here
	 */
	do_exit(err);
}

/* for user application debugging */
void sw_exception(struct pt_regs *regs)
{
	_exception(SIGTRAP, regs, TRAP_BRKPT, regs->r16);
}

void _exception(int signr, struct pt_regs *regs, int code, unsigned long addr)
{
	siginfo_t info;

	if (kernel_mode(regs)) {
		die("Exception in kernel mode", regs, signr);
	}
	info.si_signo = signr;
	info.si_errno = 0;
	info.si_code = code;
	info.si_addr = (void __user *) addr;
	force_sig_info(signr, &info, current);
}

asmlinkage void full_exception(struct pt_regs *regs, unsigned int type,
							int fsr, int addr)
{
#ifdef CONFIG_MMU
	int code;
	addr = regs->pc;
#endif

#if 0
	printk(KERN_WARNING "Exception %02x in %s mode, FSR=%08x PC=%08x " \
							"ESR=%08x\n",
			type, user_mode(regs) ? "user" : "kernel", fsr,
			(unsigned int) regs->pc, (unsigned int) regs->esr);
#endif

	switch (type & 0x1F) {
	case MICROBLAZE_ILL_OPCODE_EXCEPTION:
		if (user_mode(regs)) {
			pr_debug(KERN_WARNING "Illegal opcode exception " \
							"in user mode.\n");
			_exception(SIGILL, regs, ILL_ILLOPC, addr);
			return;
		}
		printk(KERN_WARNING "Illegal opcode exception " \
							"in kernel mode.\n");
		die("opcode exception", regs, SIGBUS);
		break;
	case MICROBLAZE_IBUS_EXCEPTION:
		if (user_mode(regs)) {
			pr_debug(KERN_WARNING "Instruction bus error " \
						"exception in user mode.\n");
			_exception(SIGBUS, regs, BUS_ADRERR, addr);
			return;
		}
		printk(KERN_WARNING "Instruction bus error exception " \
							"in kernel mode.\n");
		die("bus exception", regs, SIGBUS);
		break;
	case MICROBLAZE_DBUS_EXCEPTION:
		if (user_mode(regs)) {
			pr_debug(KERN_WARNING "Data bus error exception " \
							"in user mode.\n");
			_exception(SIGBUS, regs, BUS_ADRERR, addr);
			return;
		}
		printk(KERN_WARNING "Data bus error exception " \
							"in kernel mode.\n");
		die("bus exception", regs, SIGBUS);
		break;
	case MICROBLAZE_DIV_ZERO_EXCEPTION:
		if (user_mode(regs)) {
			pr_debug(KERN_WARNING "Divide by zero exception " \
							"in user mode\n");
			_exception(SIGILL, regs, FPE_INTDIV, addr);
			return;
		}
		printk(KERN_WARNING "Divide by zero exception " \
							"in kernel mode.\n");
		die("Divide by zero exception", regs, SIGBUS);
		break;
	case MICROBLAZE_FPU_EXCEPTION:
		pr_debug(KERN_WARNING "FPU exception\n");
		/* IEEE FP exception */
		/* I removed fsr variable and use code var for storing fsr */
		if (fsr & FSR_IO)
			fsr = FPE_FLTINV;
		else if (fsr & FSR_OF)
			fsr = FPE_FLTOVF;
		else if (fsr & FSR_UF)
			fsr = FPE_FLTUND;
		else if (fsr & FSR_DZ)
			fsr = FPE_FLTDIV;
		else if (fsr & FSR_DO)
			fsr = FPE_FLTRES;
		_exception(SIGFPE, regs, fsr, addr);
		break;

#ifdef CONFIG_MMU
	case MICROBLAZE_PRIVILEGED_EXCEPTION:
		pr_debug(KERN_WARNING "Privileged exception\n");
		/* "brk r0,r0" - used as debug breakpoint - old toolchain */
		if (get_user(code, (unsigned long *)regs->pc) == 0
			&& code == 0x980c0000) {
			_exception(SIGTRAP, regs, TRAP_BRKPT, addr);
		} else {
			_exception(SIGILL, regs, ILL_PRVOPC, addr);
		}
		break;
#endif
	default:
	/* FIXME what to do in unexpected exception */
		printk(KERN_WARNING "Unexpected exception %02x "
			"PC=%08x in %s mode\n", type, (unsigned int) addr,
			kernel_mode(regs) ? "kernel" : "user");
	}
	return;
}
