// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/reboot.h>
#include <linux/pm.h>

void machine_halt(void)
{
	/* Halt the processor */
	__asm__ __volatile__("flag  1\n");
}

void machine_restart(char *__unused)
{
	/* Soft reset : jump to reset vector */
	pr_info("Put your restart handler here\n");
	machine_halt();
}

void machine_power_off(void)
{
	/* FIXME ::  power off ??? */
	machine_halt();
}

void (*pm_power_off) (void) = NULL;
EXPORT_SYMBOL(pm_power_off);
