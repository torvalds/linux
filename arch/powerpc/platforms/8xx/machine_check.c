// SPDX-License-Identifier: GPL-2.0-or-later
/*
 */

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/ptrace.h>

#include <asm/reg.h>

int machine_check_8xx(struct pt_regs *regs)
{
	unsigned long reason = regs->msr;

	pr_err("Machine check in kernel mode.\n");
	pr_err("Caused by (from SRR1=%lx): ", reason);
	if (reason & 0x40000000)
		pr_cont("Fetch error at address %lx\n", regs->nip);
	else
		pr_cont("Data access error at address %lx\n", regs->dar);

#ifdef CONFIG_PCI
	/* the qspan pci read routines can cause machine checks -- Cort
	 *
	 * yuck !!! that totally needs to go away ! There are better ways
	 * to deal with that than having a wart in the mcheck handler.
	 * -- BenH
	 */
	bad_page_fault(regs, SIGBUS);
	return 1;
#else
	return 0;
#endif
}
