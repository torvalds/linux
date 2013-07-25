/*
 * Xilinx SLCR driver
 *
 * Copyright (c) 2011-2013 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 * 02139, USA.
 */

#include <linux/export.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/clk/zynq.h>
#include "common.h"

#define SLCR_UNLOCK_MAGIC		0xDF0D
#define SLCR_UNLOCK			0x8   /* SCLR unlock register */

#define SLCR_PS_RST_CTRL_OFFSET		0x200 /* PS Software Reset Control */

#define SLCR_A9_CPU_CLKSTOP		0x10
#define SLCR_A9_CPU_RST			0x1

#define SLCR_A9_CPU_RST_CTRL		0x244 /* CPU Software Reset Control */
#define SLCR_REBOOT_STATUS		0x258 /* PS Reboot Status */

void __iomem *zynq_slcr_base;

/**
 * zynq_slcr_system_reset - Reset the entire system.
 */
void zynq_slcr_system_reset(void)
{
	u32 reboot;

	/*
	 * Unlock the SLCR then reset the system.
	 * Note that this seems to require raw i/o
	 * functions or there's a lockup?
	 */
	writel(SLCR_UNLOCK_MAGIC, zynq_slcr_base + SLCR_UNLOCK);

	/*
	 * Clear 0x0F000000 bits of reboot status register to workaround
	 * the FSBL not loading the bitstream after soft-reboot
	 * This is a temporary solution until we know more.
	 */
	reboot = readl(zynq_slcr_base + SLCR_REBOOT_STATUS);
	writel(reboot & 0xF0FFFFFF, zynq_slcr_base + SLCR_REBOOT_STATUS);
	writel(1, zynq_slcr_base + SLCR_PS_RST_CTRL_OFFSET);
}

/**
 * zynq_slcr_cpu_start - Start cpu
 * @cpu:	cpu number
 */
void zynq_slcr_cpu_start(int cpu)
{
	/* enable CPUn */
	writel(SLCR_A9_CPU_CLKSTOP << cpu,
	       zynq_slcr_base + SLCR_A9_CPU_RST_CTRL);
	/* enable CLK for CPUn */
	writel(0x0 << cpu, zynq_slcr_base + SLCR_A9_CPU_RST_CTRL);
}

/**
 * zynq_slcr_cpu_stop - Stop cpu
 * @cpu:	cpu number
 */
void zynq_slcr_cpu_stop(int cpu)
{
	/* stop CLK and reset CPUn */
	writel((SLCR_A9_CPU_CLKSTOP | SLCR_A9_CPU_RST) << cpu,
	       zynq_slcr_base + SLCR_A9_CPU_RST_CTRL);
}

/**
 * zynq_slcr_init
 * Returns 0 on success, negative errno otherwise.
 *
 * Called early during boot from platform code to remap SLCR area.
 */
int __init zynq_slcr_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "xlnx,zynq-slcr");
	if (!np) {
		pr_err("%s: no slcr node found\n", __func__);
		BUG();
	}

	zynq_slcr_base = of_iomap(np, 0);
	if (!zynq_slcr_base) {
		pr_err("%s: Unable to map I/O memory\n", __func__);
		BUG();
	}

	/* unlock the SLCR so that registers can be changed */
	writel(SLCR_UNLOCK_MAGIC, zynq_slcr_base + SLCR_UNLOCK);

	pr_info("%s mapped to %p\n", np->name, zynq_slcr_base);

	zynq_clock_init(zynq_slcr_base);

	of_node_put(np);

	return 0;
}
