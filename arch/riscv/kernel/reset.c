// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <linux/reboot.h>
#include <linux/pm.h>

static void default_power_off(void)
{
	while (1)
		wait_for_interrupt();
}

void (*pm_power_off)(void) = default_power_off;
EXPORT_SYMBOL(pm_power_off);

void machine_restart(char *cmd)
{
	do_kernel_restart(cmd);
	while (1);
}

void machine_halt(void)
{
	pm_power_off();
}

void machine_power_off(void)
{
	pm_power_off();
}
