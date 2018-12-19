/*
 * Copyright (C) 2014 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sort.h>

#include <soc/tegra/fuse.h>

#include "mc.h"

#define MC_INTSTATUS 0x000

#define MC_INTMASK 0x004

#define MC_ERR_STATUS 0x08
#define  MC_ERR_STATUS_TYPE_SHIFT 28
#define  MC_ERR_STATUS_TYPE_INVALID_SMMU_PAGE (6 << MC_ERR_STATUS_TYPE_SHIFT)
#define  MC_ERR_STATUS_TYPE_MASK (0x7 << MC_ERR_STATUS_TYPE_SHIFT)
#define  MC_ERR_STATUS_READABLE (1 << 27)
#define  MC_ERR_STATUS_WRITABLE (1 << 26)
#define  MC_ERR_STATUS_NONSECURE (1 << 25)
#define  MC_ERR_STATUS_ADR_HI_SHIFT 20
#define  MC_ERR_STATUS_ADR_HI_MASK 0x3
#define  MC_ERR_STATUS_SECURITY (1 << 17)
#define  MC_ERR_STATUS_RW (1 << 16)

#define MC_ERR_ADR 0x0c

#define MC_EMEM_ARB_CFG 0x90
#define  MC_EMEM_ARB_CFG_CYCLES_PER_UPDATE(x)	(((x) & 0x1ff) << 0)
#define  MC_EMEM_ARB_CFG_CYCLES_PER_UPDATE_MASK	0x1ff
#define MC_EMEM_ARB_MISC0 0xd8

#define MC_EMEM_ADR_CFG 0x54
#define MC_EMEM_ADR_CFG_EMEM_NUMDEV BIT(0)

static const struct of_device_id tegra_mc_of_match[] = {
#ifdef CONFIG_ARCH_TEGRA_3x_SOC
	{ .compatible = "nvidia,tegra30-mc", .data = &tegra30_mc_soc },
#endif
#ifdef CONFIG_ARCH_TEGRA_114_SOC
	{ .compatible = "nvidia,tegra114-mc", .data = &tegra114_mc_soc },
#endif
#ifdef CONFIG_ARCH_TEGRA_124_SOC
	{ .compatible = "nvidia,tegra124-mc", .data = &tegra124_mc_soc },
#endif
#ifdef CONFIG_ARCH_TEGRA_132_SOC
	{ .compatible = "nvidia,tegra132-mc", .data = &tegra132_mc_soc },
#endif
#ifdef CONFIG_ARCH_TEGRA_210_SOC
	{ .compatible = "nvidia,tegra210-mc", .data = &tegra210_mc_soc },
#endif
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_mc_of_match);

static int tegra_mc_setup_latency_allowance(struct tegra_mc *mc)
{
	unsigned long long tick;
	unsigned int i;
	u32 value;

	/* compute the number of MC clock cycles per tick */
	tick = mc->tick * clk_get_rate(mc->clk);
	do_div(tick, NSEC_PER_SEC);

	value = readl(mc->regs + MC_EMEM_ARB_CFG);
	value &= ~MC_EMEM_ARB_CFG_CYCLES_PER_UPDATE_MASK;
	value |= MC_EMEM_ARB_CFG_CYCLES_PER_UPDATE(tick);
	writel(value, mc->regs + MC_EMEM_ARB_CFG);

	/* write latency allowance defaults */
	for (i = 0; i < mc->soc->num_clients; i++) {
		const struct tegra_mc_la *la = &mc->soc->clients[i].la;
		u32 value;

		value = readl(mc->regs + la->reg);
		value &= ~(la->mask << la->shift);
		value |= (la->def & la->mask) << la->shift;
		writel(value, mc->regs + la->reg);
	}

	return 0;
}

void tegra_mc_write_emem_configuration(struct tegra_mc *mc, unsigned long rate)
{
	unsigned int i;
	struct tegra_mc_timing *timing = NULL;

	for (i = 0; i < mc->num_timings; i++) {
		if (mc->timings[i].rate == rate) {
			timing = &mc->timings[i];
			break;
		}
	}

	if (!timing) {
		dev_err(mc->dev, "no memory timing registered for rate %lu\n",
			rate);
		return;
	}

	for (i = 0; i < mc->soc->num_emem_regs; ++i)
		mc_writel(mc, timing->emem_data[i], mc->soc->emem_regs[i]);
}

unsigned int tegra_mc_get_emem_device_count(struct tegra_mc *mc)
{
	u8 dram_count;

	dram_count = mc_readl(mc, MC_EMEM_ADR_CFG);
	dram_count &= MC_EMEM_ADR_CFG_EMEM_NUMDEV;
	dram_count++;

	return dram_count;
}

static int load_one_timing(struct tegra_mc *mc,
			   struct tegra_mc_timing *timing,
			   struct device_node *node)
{
	int err;
	u32 tmp;

	err = of_property_read_u32(node, "clock-frequency", &tmp);
	if (err) {
		dev_err(mc->dev,
			"timing %s: failed to read rate\n", node->name);
		return err;
	}

	timing->rate = tmp;
	timing->emem_data = devm_kcalloc(mc->dev, mc->soc->num_emem_regs,
					 sizeof(u32), GFP_KERNEL);
	if (!timing->emem_data)
		return -ENOMEM;

	err = of_property_read_u32_array(node, "nvidia,emem-configuration",
					 timing->emem_data,
					 mc->soc->num_emem_regs);
	if (err) {
		dev_err(mc->dev,
			"timing %s: failed to read EMEM configuration\n",
			node->name);
		return err;
	}

	return 0;
}

static int load_timings(struct tegra_mc *mc, struct device_node *node)
{
	struct device_node *child;
	struct tegra_mc_timing *timing;
	int child_count = of_get_child_count(node);
	int i = 0, err;

	mc->timings = devm_kcalloc(mc->dev, child_count, sizeof(*timing),
				   GFP_KERNEL);
	if (!mc->timings)
		return -ENOMEM;

	mc->num_timings = child_count;

	for_each_child_of_node(node, child) {
		timing = &mc->timings[i++];

		err = load_one_timing(mc, timing, child);
		if (err)
			return err;
	}

	return 0;
}

static int tegra_mc_setup_timings(struct tegra_mc *mc)
{
	struct device_node *node;
	u32 ram_code, node_ram_code;
	int err;

	ram_code = tegra_read_ram_code();

	mc->num_timings = 0;

	for_each_child_of_node(mc->dev->of_node, node) {
		err = of_property_read_u32(node, "nvidia,ram-code",
					   &node_ram_code);
		if (err || (node_ram_code != ram_code)) {
			of_node_put(node);
			continue;
		}

		err = load_timings(mc, node);
		if (err)
			return err;
		of_node_put(node);
		break;
	}

	if (mc->num_timings == 0)
		dev_warn(mc->dev,
			 "no memory timings for RAM code %u registered\n",
			 ram_code);

	return 0;
}

static const char *const status_names[32] = {
	[ 1] = "External interrupt",
	[ 6] = "EMEM address decode error",
	[ 8] = "Security violation",
	[ 9] = "EMEM arbitration error",
	[10] = "Page fault",
	[11] = "Invalid APB ASID update",
	[12] = "VPR violation",
	[13] = "Secure carveout violation",
	[16] = "MTS carveout violation",
};

static const char *const error_names[8] = {
	[2] = "EMEM decode error",
	[3] = "TrustZone violation",
	[4] = "Carveout violation",
	[6] = "SMMU translation error",
};

static irqreturn_t tegra_mc_irq(int irq, void *data)
{
	struct tegra_mc *mc = data;
	unsigned long status;
	unsigned int bit;

	/* mask all interrupts to avoid flooding */
	status = mc_readl(mc, MC_INTSTATUS) & mc->soc->intmask;
	if (!status)
		return IRQ_NONE;

	for_each_set_bit(bit, &status, 32) {
		const char *error = status_names[bit] ?: "unknown";
		const char *client = "unknown", *desc;
		const char *direction, *secure;
		phys_addr_t addr = 0;
		unsigned int i;
		char perm[7];
		u8 id, type;
		u32 value;

		value = mc_readl(mc, MC_ERR_STATUS);

#ifdef CONFIG_PHYS_ADDR_T_64BIT
		if (mc->soc->num_address_bits > 32) {
			addr = ((value >> MC_ERR_STATUS_ADR_HI_SHIFT) &
				MC_ERR_STATUS_ADR_HI_MASK);
			addr <<= 32;
		}
#endif

		if (value & MC_ERR_STATUS_RW)
			direction = "write";
		else
			direction = "read";

		if (value & MC_ERR_STATUS_SECURITY)
			secure = "secure ";
		else
			secure = "";

		id = value & mc->soc->client_id_mask;

		for (i = 0; i < mc->soc->num_clients; i++) {
			if (mc->soc->clients[i].id == id) {
				client = mc->soc->clients[i].name;
				break;
			}
		}

		type = (value & MC_ERR_STATUS_TYPE_MASK) >>
		       MC_ERR_STATUS_TYPE_SHIFT;
		desc = error_names[type];

		switch (value & MC_ERR_STATUS_TYPE_MASK) {
		case MC_ERR_STATUS_TYPE_INVALID_SMMU_PAGE:
			perm[0] = ' ';
			perm[1] = '[';

			if (value & MC_ERR_STATUS_READABLE)
				perm[2] = 'R';
			else
				perm[2] = '-';

			if (value & MC_ERR_STATUS_WRITABLE)
				perm[3] = 'W';
			else
				perm[3] = '-';

			if (value & MC_ERR_STATUS_NONSECURE)
				perm[4] = '-';
			else
				perm[4] = 'S';

			perm[5] = ']';
			perm[6] = '\0';
			break;

		default:
			perm[0] = '\0';
			break;
		}

		value = mc_readl(mc, MC_ERR_ADR);
		addr |= value;

		dev_err_ratelimited(mc->dev, "%s: %s%s @%pa: %s (%s%s)\n",
				    client, secure, direction, &addr, error,
				    desc, perm);
	}

	/* clear interrupts */
	mc_writel(mc, status, MC_INTSTATUS);

	return IRQ_HANDLED;
}

static int tegra_mc_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct resource *res;
	struct tegra_mc *mc;
	int err;

	match = of_match_node(tegra_mc_of_match, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	mc = devm_kzalloc(&pdev->dev, sizeof(*mc), GFP_KERNEL);
	if (!mc)
		return -ENOMEM;

	platform_set_drvdata(pdev, mc);
	mc->soc = match->data;
	mc->dev = &pdev->dev;

	/* length of MC tick in nanoseconds */
	mc->tick = 30;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mc->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mc->regs))
		return PTR_ERR(mc->regs);

	mc->clk = devm_clk_get(&pdev->dev, "mc");
	if (IS_ERR(mc->clk)) {
		dev_err(&pdev->dev, "failed to get MC clock: %ld\n",
			PTR_ERR(mc->clk));
		return PTR_ERR(mc->clk);
	}

	err = tegra_mc_setup_latency_allowance(mc);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to setup latency allowance: %d\n",
			err);
		return err;
	}

	err = tegra_mc_setup_timings(mc);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to setup timings: %d\n", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_TEGRA_IOMMU_SMMU)) {
		mc->smmu = tegra_smmu_probe(&pdev->dev, mc->soc->smmu, mc);
		if (IS_ERR(mc->smmu)) {
			dev_err(&pdev->dev, "failed to probe SMMU: %ld\n",
				PTR_ERR(mc->smmu));
			return PTR_ERR(mc->smmu);
		}
	}

	mc->irq = platform_get_irq(pdev, 0);
	if (mc->irq < 0) {
		dev_err(&pdev->dev, "interrupt not specified\n");
		return mc->irq;
	}

	err = devm_request_irq(&pdev->dev, mc->irq, tegra_mc_irq, IRQF_SHARED,
			       dev_name(&pdev->dev), mc);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to request IRQ#%u: %d\n", mc->irq,
			err);
		return err;
	}

	WARN(!mc->soc->client_id_mask, "Missing client ID mask for this SoC\n");

	mc_writel(mc, mc->soc->intmask, MC_INTMASK);

	return 0;
}

static struct platform_driver tegra_mc_driver = {
	.driver = {
		.name = "tegra-mc",
		.of_match_table = tegra_mc_of_match,
		.suppress_bind_attrs = true,
	},
	.prevent_deferred_probe = true,
	.probe = tegra_mc_probe,
};

static int tegra_mc_init(void)
{
	return platform_driver_register(&tegra_mc_driver);
}
arch_initcall(tegra_mc_init);

MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra Memory Controller driver");
MODULE_LICENSE("GPL v2");
