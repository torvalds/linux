// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/reboot.h>

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

void machine_power_off(void)
{
	local_irq_disable();
	do_kernel_power_off();
	asm volatile ("bkpt");
}

void machine_halt(void)
{
	local_irq_disable();
	do_kernel_power_off();
	asm volatile ("bkpt");
}

void machine_restart(char *cmd)
{
	local_irq_disable();
	do_kernel_restart(cmd);
	asm volatile ("bkpt");
}
