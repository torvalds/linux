/*
 * Copyright 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 *
 * This file contains the CPU initialization code.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <mach/hardware.h>
#include <asm/io.h>

static int __init post_cpu_init(void)
{
	unsigned int reg;
	void __iomem *base;

	if (!cpu_is_mx51())
		return 0;

	base = MX51_IO_ADDRESS(MX51_AIPS1_BASE_ADDR);
	__raw_writel(0x0, base + 0x40);
	__raw_writel(0x0, base + 0x44);
	__raw_writel(0x0, base + 0x48);
	__raw_writel(0x0, base + 0x4C);
	reg = __raw_readl(base + 0x50) & 0x00FFFFFF;
	__raw_writel(reg, base + 0x50);

	base = MX51_IO_ADDRESS(MX51_AIPS2_BASE_ADDR);
	__raw_writel(0x0, base + 0x40);
	__raw_writel(0x0, base + 0x44);
	__raw_writel(0x0, base + 0x48);
	__raw_writel(0x0, base + 0x4C);
	reg = __raw_readl(base + 0x50) & 0x00FFFFFF;
	__raw_writel(reg, base + 0x50);

	return 0;
}

postcore_initcall(post_cpu_init);
