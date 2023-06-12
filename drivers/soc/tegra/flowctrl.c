// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/soc/tegra/flowctrl.c
 *
 * Functions and macros to control the flowcontroller
 *
 * Copyright (c) 2010-2012, NVIDIA Corporation. All rights reserved.
 */

#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include <soc/tegra/common.h>
#include <soc/tegra/flowctrl.h>
#include <soc/tegra/fuse.h>

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
	if (WARN_ONCE(IS_ERR_OR_NULL(tegra_flowctrl_base),
		      "Tegra flowctrl not initialised!\n"))
		return;

	writel(value, tegra_flowctrl_base + offset);

	/* ensure the update has reached the flow controller */
	wmb();
	readl_relaxed(tegra_flowctrl_base + offset);
}

u32 flowctrl_read_cpu_csr(unsigned int cpuid)
{
	u8 offset = flowctrl_offset_cpu_csr[cpuid];

	if (WARN_ONCE(IS_ERR_OR_NULL(tegra_flowctrl_base),
		      "Tegra flowctrl not initialised!\n"))
		return 0;

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

		if (tegra_get_chip_id() == TEGRA30) {
			/*
			 * The wfi doesn't work well on Tegra30 because
			 * CPU hangs under some odd circumstances after
			 * power-gating (like memory running off PLLP),
			 * hence use wfe that is working perfectly fine.
			 * Note that Tegra30 TRM doc clearly stands that
			 * wfi should be used for the "Cluster Switching",
			 * while wfe for the power-gating, just like it
			 * is done on Tegra20.
			 */
			reg |= TEGRA20_FLOW_CTRL_CSR_WFE_CPU0 << cpuid;
		} else {
			/* pwr gating on wfi */
			reg |= TEGRA30_FLOW_CTRL_CSR_WFI_CPU0 << cpuid;
		}
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

static int tegra_flowctrl_probe(struct platform_device *pdev)
{
	void __iomem *base = tegra_flowctrl_base;

	tegra_flowctrl_base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(tegra_flowctrl_base))
		return PTR_ERR(tegra_flowctrl_base);

	iounmap(base);

	return 0;
}

static const struct of_device_id tegra_flowctrl_match[] = {
	{ .compatible = "nvidia,tegra210-flowctrl" },
	{ .compatible = "nvidia,tegra124-flowctrl" },
	{ .compatible = "nvidia,tegra114-flowctrl" },
	{ .compatible = "nvidia,tegra30-flowctrl" },
	{ .compatible = "nvidia,tegra20-flowctrl" },
	{ }
};

static struct platform_driver tegra_flowctrl_driver = {
	.driver = {
		.name = "tegra-flowctrl",
		.suppress_bind_attrs = true,
		.of_match_table = tegra_flowctrl_match,
	},
	.probe = tegra_flowctrl_probe,
};
builtin_platform_driver(tegra_flowctrl_driver);

static int __init tegra_flowctrl_init(void)
{
	struct resource res;
	struct device_node *np;

	if (!soc_is_tegra())
		return 0;

	np = of_find_matching_node(NULL, tegra_flowctrl_match);
	if (np) {
		if (of_address_to_resource(np, 0, &res) < 0) {
			pr_err("failed to get flowctrl register\n");
			return -ENXIO;
		}
		of_node_put(np);
	} else if (IS_ENABLED(CONFIG_ARM)) {
		/*
		 * Hardcoded fallback for 32-bit Tegra
		 * devices if device tree node is missing.
		 */
		res.start = 0x60007000;
		res.end = 0x60007fff;
		res.flags = IORESOURCE_MEM;
	} else {
		/*
		 * At this point we're running on a Tegra,
		 * that doesn't support the flow controller
		 * (eg. Tegra186), so just return.
		 */
		return 0;
	}

	tegra_flowctrl_base = ioremap(res.start, resource_size(&res));
	if (!tegra_flowctrl_base)
		return -ENXIO;

	return 0;
}
early_initcall(tegra_flowctrl_init);
