/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/io.h>
#include <linux/pm.h>
#include <linux/reboot.h>

#include <asm/reboot.h>
#include <asm/mach-malta/malta-pm.h>

static void mips_machine_power_off(void)
{
	mips_pm_suspend(PIIX4_FUNC3IO_PMCNTRL_SUS_TYP_SOFF);

	pr_info("Failed to power down, resetting\n");
	machine_restart(NULL);
}

static int __init mips_reboot_setup(void)
{
	pm_power_off = mips_machine_power_off;

	return 0;
}
arch_initcall(mips_reboot_setup);
