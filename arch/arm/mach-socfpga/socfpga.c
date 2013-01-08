/*
 *  Copyright (C) 2012-2013 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/dw_apb_timer.h>
#include <linux/clk-provider.h>
#include <linux/irqchip.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/smp_twd.h>

#include "core.h"

#define SOCFPGA_NR_IRQS		512

void __iomem *socfpga_scu_base_addr = ((void __iomem *)(SOCFPGA_SCU_VIRT_BASE));
void __iomem *sys_manager_base_addr;
void __iomem *rst_manager_base_addr;

unsigned long	cpu1start_addr;

static const struct of_dev_auxdata socfpga_auxdata_lookup[] __initconst = {
	{ /* sentinel */ }
};

const static struct of_device_id irq_match[] = {
	{ .compatible = "arm,cortex-a9-gic", .data = gic_of_init, },
	{}
};

static struct map_desc scu_io_desc __initdata = {
	.virtual	= SOCFPGA_SCU_VIRT_BASE,
	.pfn		= 0, /* run-time */
	.length		= SZ_8K,
	.type		= MT_DEVICE,
};

static struct map_desc uart_io_desc __initdata = {
	.virtual	= 0xfec02000,
	.pfn		= __phys_to_pfn(0xffc02000),
	.length		= SZ_8K,
	.type		= MT_DEVICE,
};

static void __init socfpga_scu_map_io(void)
{
	unsigned long base;

	/* Get SCU base */
	asm("mrc p15, 4, %0, c15, c0, 0" : "=r" (base));

	scu_io_desc.pfn = __phys_to_pfn(base);
	iotable_init(&scu_io_desc, 1);
}

static void __init init_socfpga_vt(void)
{
	cpu1start_addr = 0xffd08010;
}

static void __init init_socfpga(void)
{
	cpu1start_addr = 0xffd080c4;
}

static void __init enable_periphs(void)
{
	/* Release all peripherals from reset.*/
	__raw_writel(0, rst_manager_base_addr + SOCFPGA_MODPERRST);

	/* Release all FPGA bridges from reset.*/
	__raw_writel(0, rst_manager_base_addr + SOCFPGA_BRGMODRST);
}

static void __init socfpga_sysmgr_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "altr,sys-mgr");
	sys_manager_base_addr = of_iomap(np, 0);

	np = of_find_compatible_node(NULL, NULL, "altr,rst-mgr");
	rst_manager_base_addr = of_iomap(np, 0);
}

static void __init socfpga_map_io(void)
{
	socfpga_scu_map_io();
	iotable_init(&uart_io_desc, 1);
	early_printk("Early printk initialized\n");
}

void __init socfpga_sysmgr_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "altr,sys-mgr");

	if (of_property_read_u32(np, "cpu1-start-addr",
			(u32 *) &cpu1start_addr))
		pr_err("SMP: Need cpu1-start-addr in device tree.\n");

	sys_manager_base_addr = of_iomap(np, 0);

	np = of_find_compatible_node(NULL, NULL, "altr,rst-mgr");
	rst_manager_base_addr = of_iomap(np, 0);

	np = of_find_compatible_node(NULL, NULL, "altr,clk-mgr");
	clk_mgr_base_addr = of_iomap(np, 0);
}

static void __init socfpga_init_irq(void)
{
	irqchip_init();
	socfpga_sysmgr_init();

	if (of_machine_is_compatible("altr,socfpga-vt"))
		init_socfpga_vt();
	else
		init_socfpga();

	socfpga_init_clocks();
	twd_local_timer_of_register();
}

static void socfpga_cyclone5_restart(char mode, const char *cmd)
{
	u32 temp;

	temp = readl(rst_manager_base_addr + SOCFPGA_RSTMGR_CTRL);

	if (mode == 'h')
		temp |= RSTMGR_CTRL_SWCOLDRSTREQ;
	else
		temp |= RSTMGR_CTRL_SWWARMRSTREQ;
	writel(temp, rst_manager_base_addr + SOCFPGA_RSTMGR_CTRL);
}

static void __init socfpga_cyclone5_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	l2x0_of_init(0, ~0UL);
#endif
	of_platform_populate(NULL, of_default_bus_match_table,
		socfpga_auxdata_lookup, NULL);

	enable_periphs();
}

static const char *altera_dt_match[] = {
	"altr,socfpga",
	NULL
};

DT_MACHINE_START(SOCFPGA, "Altera SOCFPGA")
	.smp		= smp_ops(socfpga_smp_ops),
	.map_io		= socfpga_map_io,
	.init_irq	= socfpga_init_irq,
	.init_time	= dw_apb_timer_init,
	.init_machine	= socfpga_cyclone5_init,
	.restart	= socfpga_cyclone5_restart,
	.dt_compat	= altera_dt_match,
MACHINE_END
