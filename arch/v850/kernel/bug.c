/*
 * arch/v850/kernel/bug.c -- Bug reporting functions
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/module.h>

#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/current.h>

/* We should use __builtin_return_address, but it doesn't work in gcc-2.90
   (which is currently our standard compiler on the v850).  */
#define ret_addr() ({ register u32 lp asm ("lp"); lp; })
#define stack_addr() ({ register u32 sp asm ("sp"); sp; })

void __bug ()
{
	printk (KERN_CRIT "kernel BUG at PC 0x%x (SP ~0x%x)!\n",
		ret_addr() - 4, /* - 4 for `jarl' */
		stack_addr());
	machine_halt ();
}

int bad_trap (int trap_num, struct pt_regs *regs)
{
	printk (KERN_CRIT
		"unimplemented trap %d called at 0x%08lx, pid %d!\n",
		trap_num, regs->pc, current->pid);
	return -ENOSYS;
}

#ifdef CONFIG_RESET_GUARD
void unexpected_reset (unsigned long ret_addr, unsigned long kmode,
		       struct task_struct *task, unsigned long sp)
{
	printk (KERN_CRIT
		"unexpected reset in %s mode, pid %d"
		" (ret_addr = 0x%lx, sp = 0x%lx)\n",
		kmode ? "kernel" : "user",
		task ? task->pid : -1,
		ret_addr, sp);

	machine_halt ();
}
#endif /* CONFIG_RESET_GUARD */



struct spec_reg_name {
	const char *name;
	int gpr;
};

struct spec_reg_name spec_reg_names[] = {
	{ "sp", GPR_SP },
	{ "gp", GPR_GP },
	{ "tp", GPR_TP },
	{ "ep", GPR_EP },
	{ "lp", GPR_LP },
	{ 0, 0 }
};

void show_regs (struct pt_regs *regs)
{
	int gpr_base, gpr_offs;

	printk ("     pc 0x%08lx    psw 0x%08lx                       kernel_mode %d\n",
		regs->pc, regs->psw, regs->kernel_mode);
	printk ("   ctpc 0x%08lx  ctpsw 0x%08lx   ctbp 0x%08lx\n",
		regs->ctpc, regs->ctpsw, regs->ctbp);

	for (gpr_base = 0; gpr_base < NUM_GPRS; gpr_base += 4) {
		for (gpr_offs = 0; gpr_offs < 4; gpr_offs++) {
			int gpr = gpr_base + gpr_offs;
			long val = regs->gpr[gpr];
			struct spec_reg_name *srn;

			for (srn = spec_reg_names; srn->name; srn++)
				if (srn->gpr == gpr)
					break;

			if (srn->name)
				printk ("%7s 0x%08lx", srn->name, val);
			else
				printk ("    r%02d 0x%08lx", gpr, val);
		}

		printk ("\n");
	}
}

/*
 * TASK is a pointer to the task whose backtrace we want to see (or NULL
 * for current task), SP is the stack pointer of the first frame that
 * should be shown in the back trace (or NULL if the entire call-chain of
 * the task should be shown).
 */
void show_stack (struct task_struct *task, unsigned long *sp)
{
	unsigned long addr, end;

	if (sp)
		addr = (unsigned long)sp;
	else if (task)
		addr = task_sp (task);
	else
		addr = stack_addr ();

	addr = addr & ~3;
	end = (addr + THREAD_SIZE - 1) & THREAD_MASK;

	while (addr < end) {
		printk ("%8lX: ", addr);
		while (addr < end) {
			printk (" %8lX", *(unsigned long *)addr);
			addr += sizeof (unsigned long);
			if (! (addr & 0xF))
				break;
		}
		printk ("\n");
	}
}

void dump_stack ()
{
	show_stack (0, 0);
}

EXPORT_SYMBOL(dump_stack);
