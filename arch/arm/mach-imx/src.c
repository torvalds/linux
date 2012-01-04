/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/unified.h>

#define SRC_SCR				0x000
#define SRC_GPR1			0x020
#define BP_SRC_SCR_CORE1_RST		14
#define BP_SRC_SCR_CORE1_ENABLE		22

static void __iomem *src_base;

void imx_enable_cpu(int cpu, bool enable)
{
	u32 mask, val;

	mask = 1 << (BP_SRC_SCR_CORE1_ENABLE + cpu - 1);
	val = readl_relaxed(src_base + SRC_SCR);
	val = enable ? val | mask : val & ~mask;
	writel_relaxed(val, src_base + SRC_SCR);
}

void imx_set_cpu_jump(int cpu, void *jump_addr)
{
	writel_relaxed(BSYM(virt_to_phys(jump_addr)),
		       src_base + SRC_GPR1 + cpu * 8);
}

void __init imx_src_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-src");
	src_base = of_iomap(np, 0);
	WARN_ON(!src_base);
}
