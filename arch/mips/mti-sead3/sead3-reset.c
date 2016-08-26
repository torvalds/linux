/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/io.h>
#include <linux/pm.h>

#include <asm/reboot.h>

#define SOFTRES_REG	0x1f000050
#define GORESET		0x4d

static void mips_machine_halt(void)
{
	unsigned int __iomem *softres_reg =
		ioremap(SOFTRES_REG, sizeof(unsigned int));

	__raw_writel(GORESET, softres_reg);
}

static int __init mips_reboot_setup(void)
{
	_machine_halt = mips_machine_halt;
	pm_power_off = mips_machine_halt;

	return 0;
}
arch_initcall(mips_reboot_setup);
