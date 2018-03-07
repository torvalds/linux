/*
 * Copyright (C) 2012 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/reboot.h>
#include <linux/export.h>
#include <asm/sbi.h>

void (*pm_power_off)(void) = machine_power_off;
EXPORT_SYMBOL(pm_power_off);

void machine_restart(char *cmd)
{
	do_kernel_restart(cmd);
	while (1);
}

void machine_halt(void)
{
	machine_power_off();
}

void machine_power_off(void)
{
	sbi_shutdown();
	while (1);
}
