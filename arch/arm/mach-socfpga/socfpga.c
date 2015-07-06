/*
 *  Copyright (C) 2012-2015 Altera Corporation
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
#include <linux/irqchip.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/reboot.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/cacheflush.h>

#include "core.h"

void __iomem *sys_manager_base_addr;
void __iomem *rst_manager_base_addr;
void __iomem *sdr_ctl_base_addr;
unsigned long socfpga_cpu1start_addr;

void __init socfpga_sysmgr_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "altr,sys-mgr");

	if (of_property_read_u32(np, "cpu1-start-addr",
			(u32 *) &socfpga_cpu1start_addr))
		pr_err("SMP: Need cpu1-start-addr in device tree.\n");

	/* Ensure that socfpga_cpu1start_addr is visible to other CPUs */
	smp_wmb();
	sync_cache_w(&socfpga_cpu1start_addr);

	sys_manager_base_addr = of_iomap(np, 0);

	np = of_find_compatible_node(NULL, NULL, "altr,rst-mgr");
	rst_manager_base_addr = of_iomap(np, 0);

	np = of_find_compatible_node(NULL, NULL, "altr,sdr-ctl");
	sdr_ctl_base_addr = of_iomap(np, 0);
}

static void __init socfpga_init_irq(void)
{
	irqchip_init();
	socfpga_sysmgr_init();
}

static void socfpga_cyclone5_restart(enum reboot_mode mode, const char *cmd)
{
	u32 temp;

	temp = readl(rst_manager_base_addr + SOCFPGA_RSTMGR_CTRL);

	if (mode == REBOOT_HARD)
		temp |= RSTMGR_CTRL_SWCOLDRSTREQ;
	else
		temp |= RSTMGR_CTRL_SWWARMRSTREQ;
	writel(temp, rst_manager_base_addr + SOCFPGA_RSTMGR_CTRL);
}

static const char *altera_dt_match[] = {
	"altr,socfpga",
	NULL
};

DT_MACHINE_START(SOCFPGA, "Altera SOCFPGA")
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.init_irq	= socfpga_init_irq,
	.restart	= socfpga_cyclone5_restart,
	.dt_compat	= altera_dt_match,
MACHINE_END
