/*
 * This file contains common code that is intended to be used across
 * boards so that it's not replicated.
 *
 *  Copyright (C) 2011 Xilinx
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/cpumask.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/clk/zynq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach-types.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/hardware/cache-l2x0.h>

#include "common.h"

static struct of_device_id zynq_of_bus_ids[] __initdata = {
	{ .compatible = "simple-bus", },
	{}
};

/**
 * xilinx_init_machine() - System specific initialization, intended to be
 *			   called from board specific initialization.
 */
static void __init xilinx_init_machine(void)
{
	/*
	 * 64KB way size, 8-way associativity, parity disabled
	 */
	l2x0_of_init(0x02060000, 0xF0F0FFFF);

	of_platform_bus_probe(NULL, zynq_of_bus_ids, NULL);
}

#define SCU_PERIPH_PHYS		0xF8F00000
#define SCU_PERIPH_SIZE		SZ_8K
#define SCU_PERIPH_VIRT		(VMALLOC_END - SCU_PERIPH_SIZE)

static struct map_desc scu_desc __initdata = {
	.virtual	= SCU_PERIPH_VIRT,
	.pfn		= __phys_to_pfn(SCU_PERIPH_PHYS),
	.length		= SCU_PERIPH_SIZE,
	.type		= MT_DEVICE,
};

static void __init xilinx_zynq_timer_init(void)
{
	struct device_node *np;
	void __iomem *slcr;

	np = of_find_compatible_node(NULL, NULL, "xlnx,zynq-slcr");
	slcr = of_iomap(np, 0);
	WARN_ON(!slcr);

	xilinx_zynq_clocks_init(slcr);

	xttcpss_timer_init();
}

/**
 * xilinx_map_io() - Create memory mappings needed for early I/O.
 */
static void __init xilinx_map_io(void)
{
	debug_ll_io_init();
	iotable_init(&scu_desc, 1);
}

static const char *xilinx_dt_match[] = {
	"xlnx,zynq-zc702",
	"xlnx,zynq-7000",
	NULL
};

MACHINE_START(XILINX_EP107, "Xilinx Zynq Platform")
	.map_io		= xilinx_map_io,
	.init_irq	= irqchip_init,
	.init_machine	= xilinx_init_machine,
	.init_time	= xilinx_zynq_timer_init,
	.dt_compat	= xilinx_dt_match,
MACHINE_END
