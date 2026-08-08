// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <linux/efi.h>
#include <linux/reboot.h>
#include <linux/pm.h>

static void default_power_off(void)
{
	while (1)
		wait_for_interrupt();
}

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);

void machine_restart(char *cmd)
{
	/*
	 * UpdateCapsule() depends on the system being reset via ResetSystem().
	 */
	if (efi_enabled(EFI_RUNTIME_SERVICES))
		efi_reboot(reboot_mode, NULL);

	do_kernel_restart(cmd);
	while (1);
}

void machine_halt(void)
{
	do_kernel_power_off();
	default_power_off();
}

void machine_power_off(void)
{
	do_kernel_power_off();
	default_power_off();
}
