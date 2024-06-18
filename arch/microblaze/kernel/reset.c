/*
 * Copyright (C) 2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2009 PetaLogix
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/reboot.h>

void machine_shutdown(void)
{
	pr_notice("Machine shutdown...\n");
	while (1)
		;
}

void machine_halt(void)
{
	pr_notice("Machine halt...\n");
	while (1)
		;
}

void machine_power_off(void)
{
	pr_notice("Machine power off...\n");
	while (1)
		;
}

void machine_restart(char *cmd)
{
	do_kernel_restart(cmd);
	/* Give the restart hook 1 s to take us down */
	mdelay(1000);
	pr_emerg("Reboot failed -- System halted\n");
	while (1);
}
