/*
 * Copyright (C) 2014 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
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

#define MC_DECERR_EMEM_OTHERS_STATUS	0x58
#define MC_SECURITY_VIOLATION_STATUS	0x74

#define MC_EMEM_ARB_CFG 0x90
#define  MC_EMEM_ARB_CFG_CYCLES_PER_UPDATE(x)	(((x) & 0x1ff) << 0)
#define  MC_EMEM_ARB_CFG_CYCLES_PER_UPDATE_MASK	0x1ff
#define MC_EMEM_ARB_MISC0 0xd8

#define MC_EMEM_ADR_CFG 0x54
#define MC_EMEM_ADR_CFG_EMEM_NUMDEV BIT(0)

static const struct of_device_id tegra_mc_of_match[] = {
#ifdef CONFIG_ARCH_TEGRA_2x_SOC
	{ .compatible = "nvidia,tegra20-mc", .data = &tegra20_mc_soc },
#endif
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

static int terga_mc_block_dma_common(struct tegra_mc *mc,
				     const struct tegra_mc_reset *rst)
{
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&mc->lock, flags);

	value = mc_readl(mc, rst->control) | BIT(rst->bit);
	mc_writel(mc, value, rst->control);

	spin_unlock_irqrestore(&mc->lock, flags);

	return 0;
}

static bool terga_mc_dma_idling_common(struct tegra_mc *mc,
				       const struct tegra_mc_reset *rst)
{
	return (mc_readl(mc, rst->status) & BIT(rst->bit)) != 0;
}

static int terga_mc_unblock_dma_common(struct tegra_mc *mc,
				       const struct tegra_mc_reset *rst)
{
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&mc->lock, flags);

	value = mc_readl(mc, rst->control) & ~BIT(rst->bit);
	mc_writel(mc, value, rst->control);

	spin_unlock_irqrestore(&mc->lock, flags);

	return 0;
}

static int terga_mc_reset_status_common(struct tegra_mc *mc,
					const struct tegra_mc_reset *rst)
{
	return (mc_readl(mc, rst->control) & BIT(rst->bit)) != 0;
}

const struct tegra_mc_reset_ops terga_mc_reset_ops_common = {
	.block_dma = terga_mc_block_dma_common,
	.dma_idling = terga_mc_dma_idling_common,
	.unblock_dma = terga_mc_unblock_dma_common,
	.reset_status = terga_mc_reset_status_common,
};

static inline struct tegra_mc *reset_to_mc(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct tegra_mc, reset);
}

static const struct tegra_mc_reset *tegra_mc_reset_find(struct tegra_mc *mc,
							unsigned long id)
{
	unsigned int i;

	for (i = 0; i < mc->soc->num_resets; i++)
		if (mc->soc->resets[i].id == id)
			return &mc->soc->resets[i];

	return NULL;
}

static int tegra_mc_hotreset_assert(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	struct tegra_mc *mc = reset_to_mc(rcdev);
	const struct tegra_mc_reset_ops *rst_ops;
	const struct tegra_mc_reset *rst;
	int retries = 500;
	int err;

	rst = tegra_mc_reset_find(mc, id);
	if (!rst)
		return -ENODEV;

	rst_ops = mc->soc->reset_ops;
	if (!rst_ops)
		return -ENODEV;

	if (rst_ops->block_dma) {
		/* block clients DMA requests */
		err = rst_ops->block_dma(mc, rst);
		if (err) {
			dev_err(mc->dev, "Failed to block %s DMA: %d\n",
				rst->name, err);
			return err;
		}
	}

	if (rst_ops->dma_idling) {
		/* wait for completion of the outstanding DMA requests */
		while (!rst_ops->dma_idling(mc, rst)) {
			if (!retries--) {
				dev_err(mc->dev, "Failed to flush %s DMA\n",
					rst->name);
				return -EBUSY;
			}

			usleep_range(10, 100);
		}
	}

	if (rst_ops->hotreset_assert) {
		/* clear clients DMA requests sitting before arbitration */
		err = rst_ops->hotreset_assert(mc, rst);
		if (err) {
			dev_err(mc->dev, "Failed to hot reset %s: %d\n",
				rst->name, err);
			return err;
		}
	}

	return 0;
}

static int tegra_mc_hotreset_deassert(struct reset_controller_dev *rcdev,
				      unsigned long id)
{
	struct tegra_mc *mc = reset_to_mc(rcdev);
	const struct tegra_mc_reset_ops *rst_ops;
	const struct tegra_mc_reset *rst;
	int err;

	rst = tegra_mc_reset_find(mc, id);
	if (!rst)
		return -ENODEV;

	rst_ops = mc->soc->reset_ops;
	if (!rst_ops)
		return -ENODEV;

	if (rst_ops->hotreset_deassert) {
		/* take out client from hot reset */
		err = rst_ops->hotreset_deassert(mc, rst);
		if (err) {
			dev_err(mc->dev, "Failed to deassert hot reset %s: %d\n",
				rst->name, err);
			return err;
		}
	}

	if (rst_ops->unblock_dma) {
		/* allow new DMA requests to proceed to arbitration */
		err = rst_ops->unblock_dma(mc, rst);
		if (err) {
			dev_err(mc->dev, "Failed to unblock %s DMA : %d\n",
				rst->name, err);
			return err;
		}
	}

	return 0;
}

static int tegra_mc_hotreset_status(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	struct tegra_mc *mc = reset_to_mc(rcdev);
	const struct tegra_mc_reset_ops *rst_ops;
	const struct tegra_mc_reset *rst;

	rst = tegra_mc_reset_find(mc, id);
	if (!rst)
		return -ENODEV;

	rst_ops = mc->soc->reset_ops;
	if (!rst_ops)
		return -ENODEV;

	return rst_ops->reset_status(mc, rst);
}

static const struct reset_control_ops tegra_mc_reset_ops = {
	.assert = tegra_mc_hotreset_assert,
	.deassert = tegra_mc_hotreset_deassert,
	.status = tegra_mc_hotreset_status,
};

static int tegra_mc_reset_setup(struct tegra_mc *mc)
{
	int err;

	mc->reset.ops = &tegra_mc_reset_ops;
	mc->reset.owner = THIS_MODULE;
	mc->reset.of_node = mc->dev->of_node;
	mc->reset.of_reset_n_cells = 1;
	mc->reset.nr_resets = mc->soc->num_resets;

	err = reset_controller_register(&mc->reset);
	if (err < 0)
		return err;

	return 0;
}

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
		if (err) {
			of_node_put(child);
			return err;
		}
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
		if (err || (node_ram_code != ram_code))
			continue;

		err = load_timings(mc, node);
		of_node_put(node);
		if (err)
			return err;
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
	[ 7] = "GART page fault",
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

static __maybe_unused irqreturn_t tegra20_mc_irq(int irq, void *data)
{
	struct tegra_mc *mc = data;
	unsigned long status;
	unsigned int bit;

	/* mask all interrupts to avoid flooding */
	status = mc_readl(mc, MC_INTSTATUS) & mc->soc->intmask;
	if (!status)
		return IRQ_NONE;

	for_each_set_bit(bit, &status, 32) {
		const char *direction = "read", *secure = "";
		const char *error = status_names[bit];
		const char *client, *desc;
		phys_addr_t addr;
		u32 value, reg;
		u8 id, type;

		switch (BIT(bit)) {
		case MC_INT_DECERR_EMEM:
			reg = MC_DECERR_EMEM_OTHERS_STATUS;
			value = mc_readl(mc, reg);

			id = value & mc->soc->client_id_mask;
			desc = error_names[2];

			if (value & BIT(31))
				direction = "write";
			break;

		case MC_INT_INVALID_GART_PAGE:
			dev_err_ratelimited(mc->dev, "%s\n", error);
			continue;

		case MC_INT_SECURITY_VIOLATION:
			reg = MC_SECURITY_VIOLATION_STATUS;
			value = mc_readl(mc, reg);

			id = value & mc->soc->client_id_mask;
			type = (value & BIT(30)) ? 4 : 3;
			desc = error_names[type];
			secure = "secure ";

			if (value & BIT(31))
				direction = "write";
			break;

		default:
			continue;
		}

		client = mc->soc->clients[id].name;
		addr = mc_readl(mc, reg + sizeof(u32));

		dev_err_ratelimited(mc->dev, "%s: %s%s @%pa: %s (%s)\n",
				    client, secure, direction, &addr, error,
				    desc);
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
	void *isr;
	int err;

	match = of_match_node(tegra_mc_of_match, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	mc = devm_kzalloc(&pdev->dev, sizeof(*mc), GFP_KERNEL);
	if (!mc)
		return -ENOMEM;

	platform_set_drvdata(pdev, mc);
	spin_lock_init(&mc->lock);
	mc->soc = match->data;
	mc->dev = &pdev->dev;

	/* length of MC tick in nanoseconds */
	mc->tick = 30;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mc->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mc->regs))
		return PTR_ERR(mc->regs);

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
	if (mc->soc == &tegra20_mc_soc) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		mc->regs2 = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(mc->regs2))
			return PTR_ERR(mc->regs2);

		isr = tegra20_mc_irq;
	} else
#endif
	{
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

		isr = tegra_mc_irq;
	}

	err = tegra_mc_setup_timings(mc);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to setup timings: %d\n", err);
		return err;
	}

	err = tegra_mc_reset_setup(mc);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register reset controller: %d\n",
			err);
		return err;
	}

	mc->irq = platform_get_irq(pdev, 0);
	if (mc->irq < 0) {
		dev_err(&pdev->dev, "interrupt not specified\n");
		return mc->irq;
	}

	WARN(!mc->soc->client_id_mask, "Missing client ID mask for this SoC\n");

	mc_writel(mc, mc->soc->intmask, MC_INTMASK);

	err = devm_request_irq(&pdev->dev, mc->irq, isr, IRQF_SHARED,
			       dev_name(&pdev->dev), mc);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to request IRQ#%u: %d\n", mc->irq,
			err);
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
