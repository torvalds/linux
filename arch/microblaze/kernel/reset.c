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
	do_kernel_power_off();
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

#ifdef CONFIG_MB_POWER_OFF_THROUGH_UNALIGNED_PC
static int unaligned_pc_sys_off(struct sys_off_data *data)
{
	__asm__(
		"bri 1\n"
	);

	return NOTIFY_DONE;
}

static int __init register_unaligned_pc_sys_off(void)
{
	struct sys_off_handler *sys_off;

	sys_off = register_sys_off_handler(SYS_OFF_MODE_POWER_OFF, SYS_OFF_PRIO_LOW,
					   unaligned_pc_sys_off, NULL);
	return PTR_ERR_OR_ZERO(sys_off);
}
device_initcall(register_unaligned_pc_sys_off);
#endif /* CONFIG_MB_POWER_OFF_THROUGH_UNALIGNED_PC */
