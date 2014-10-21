/*
 * arch/arm/mach-tegra/flowctrl.c
 *
 * functions and macros to control the flowcontroller
 *
 * Copyright (c) 2010-2012, NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <soc/tegra/fuse.h>

#include "flowctrl.h"

static u8 flowctrl_offset_halt_cpu[] = {
	FLOW_CTRL_HALT_CPU0_EVENTS,
	FLOW_CTRL_HALT_CPU1_EVENTS,
	FLOW_CTRL_HALT_CPU1_EVENTS + 8,
	FLOW_CTRL_HALT_CPU1_EVENTS + 16,
};

static u8 flowctrl_offset_cpu_csr[] = {
	FLOW_CTRL_CPU0_CSR,
	FLOW_CTRL_CPU1_CSR,
	FLOW_CTRL_CPU1_CSR + 8,
	FLOW_CTRL_CPU1_CSR + 16,
};

static void __iomem *tegra_flowctrl_base;

static void flowctrl_update(u8 offset, u32 value)
{
	writel(value, tegra_flowctrl_base + offset);

	/* ensure the update has reached the flow controller */
	wmb();
	readl_relaxed(tegra_flowctrl_base + offset);
}

u32 flowctrl_read_cpu_csr(unsigned int cpuid)
{
	u8 offset = flowctrl_offset_cpu_csr[cpuid];

	return readl(tegra_flowctrl_base + offset);
}

void flowctrl_write_cpu_csr(unsigned int cpuid, u32 value)
{
	return flowctrl_update(flowctrl_offset_cpu_csr[cpuid], value);
}

void flowctrl_write_cpu_halt(unsigned int cpuid, u32 value)
{
	return flowctrl_update(flowctrl_offset_halt_cpu[cpuid], value);
}

void flowctrl_cpu_suspend_enter(unsigned int cpuid)
{
	unsigned int reg;
	int i;

	reg = flowctrl_read_cpu_csr(cpuid);
	switch (tegra_get_chip_id()) {
	case TEGRA20:
		/* clear wfe bitmap */
		reg &= ~TEGRA20_FLOW_CTRL_CSR_WFE_BITMAP;
		/* clear wfi bitmap */
		reg &= ~TEGRA20_FLOW_CTRL_CSR_WFI_BITMAP;
		/* pwr gating on wfe */
		reg |= TEGRA20_FLOW_CTRL_CSR_WFE_CPU0 << cpuid;
		break;
	case TEGRA30:
	case TEGRA114:
	case TEGRA124:
		/* clear wfe bitmap */
		reg &= ~TEGRA30_FLOW_CTRL_CSR_WFE_BITMAP;
		/* clear wfi bitmap */
		reg &= ~TEGRA30_FLOW_CTRL_CSR_WFI_BITMAP;
		/* pwr gating on wfi */
		reg |= TEGRA30_FLOW_CTRL_CSR_WFI_CPU0 << cpuid;
		break;
	}
	reg |= FLOW_CTRL_CSR_INTR_FLAG;			/* clear intr flag */
	reg |= FLOW_CTRL_CSR_EVENT_FLAG;		/* clear event flag */
	reg |= FLOW_CTRL_CSR_ENABLE;			/* pwr gating */
	flowctrl_write_cpu_csr(cpuid, reg);

	for (i = 0; i < num_possible_cpus(); i++) {
		if (i == cpuid)
			continue;
		reg = flowctrl_read_cpu_csr(i);
		reg |= FLOW_CTRL_CSR_EVENT_FLAG;
		reg |= FLOW_CTRL_CSR_INTR_FLAG;
		flowctrl_write_cpu_csr(i, reg);
	}
}

void flowctrl_cpu_suspend_exit(unsigned int cpuid)
{
	unsigned int reg;

	/* Disable powergating via flow controller for CPU0 */
	reg = flowctrl_read_cpu_csr(cpuid);
	switch (tegra_get_chip_id()) {
	case TEGRA20:
		/* clear wfe bitmap */
		reg &= ~TEGRA20_FLOW_CTRL_CSR_WFE_BITMAP;
		/* clear wfi bitmap */
		reg &= ~TEGRA20_FLOW_CTRL_CSR_WFI_BITMAP;
		break;
	case TEGRA30:
	case TEGRA114:
	case TEGRA124:
		/* clear wfe bitmap */
		reg &= ~TEGRA30_FLOW_CTRL_CSR_WFE_BITMAP;
		/* clear wfi bitmap */
		reg &= ~TEGRA30_FLOW_CTRL_CSR_WFI_BITMAP;
		break;
	}
	reg &= ~FLOW_CTRL_CSR_ENABLE;			/* clear enable */
	reg |= FLOW_CTRL_CSR_INTR_FLAG;			/* clear intr */
	reg |= FLOW_CTRL_CSR_EVENT_FLAG;		/* clear event */
	flowctrl_write_cpu_csr(cpuid, reg);
}

static const struct of_device_id matches[] __initconst = {
	{ .compatible = "nvidia,tegra124-flowctrl" },
	{ .compatible = "nvidia,tegra114-flowctrl" },
	{ .compatible = "nvidia,tegra30-flowctrl" },
	{ .compatible = "nvidia,tegra20-flowctrl" },
	{ }
};

void __init tegra_flowctrl_init(void)
{
	/* hardcoded fallback if device tree node is missing */
	unsigned long base = 0x60007000;
	unsigned long size = SZ_4K;
	struct device_node *np;

	np = of_find_matching_node(NULL, matches);
	if (np) {
		struct resource res;

		if (of_address_to_resource(np, 0, &res) == 0) {
			size = resource_size(&res);
			base = res.start;
		}

		of_node_put(np);
	}

	tegra_flowctrl_base = ioremap_nocache(base, size);
}
