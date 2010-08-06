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
#include <linux/module.h>
#include <mach/hardware.h>
#include <asm/io.h>

static int cpu_silicon_rev = -1;

#define SI_REV 0x48

static void query_silicon_parameter(void)
{
	void __iomem *rom = ioremap(MX51_IROM_BASE_ADDR, MX51_IROM_SIZE);
	u32 rev;

	if (!rom) {
		cpu_silicon_rev = -EINVAL;
		return;
	}

	rev = readl(rom + SI_REV);
	switch (rev) {
	case 0x1:
		cpu_silicon_rev = MX51_CHIP_REV_1_0;
		break;
	case 0x2:
		cpu_silicon_rev = MX51_CHIP_REV_1_1;
		break;
	case 0x10:
		cpu_silicon_rev = MX51_CHIP_REV_2_0;
		break;
	case 0x20:
		cpu_silicon_rev = MX51_CHIP_REV_3_0;
		break;
	default:
		cpu_silicon_rev = 0;
	}

	iounmap(rom);
}

/*
 * Returns:
 *	the silicon revision of the cpu
 *	-EINVAL - not a mx51
 */
int mx51_revision(void)
{
	if (!cpu_is_mx51())
		return -EINVAL;

	if (cpu_silicon_rev == -1)
		query_silicon_parameter();

	return cpu_silicon_rev;
}
EXPORT_SYMBOL(mx51_revision);

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
