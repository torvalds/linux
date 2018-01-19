/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/ptrace.h>

#include <asm/reg.h>

int machine_check_4xx(struct pt_regs *regs)
{
	unsigned long reason = regs->dsisr;

	if (reason & ESR_IMCP) {
		printk("Instruction");
		mtspr(SPRN_ESR, reason & ~ESR_IMCP);
	} else
		printk("Data");
	printk(" machine check in kernel mode.\n");

	return 0;
}
