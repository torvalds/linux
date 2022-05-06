// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sort.h>

#include <soc/tegra/fuse.h>

#include "mc.h"

static const struct of_device_id tegra_mc_of_match[] = {
#ifdef CONFIG_ARCH_TEGRA_2x_SOC
	{ .compatible = "nvidia,tegra20-mc-gart", .data = &tegra20_mc_soc },
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
#ifdef CONFIG_ARCH_TEGRA_186_SOC
	{ .compatible = "nvidia,tegra186-mc", .data = &tegra186_mc_soc },
#endif
#ifdef CONFIG_ARCH_TEGRA_194_SOC
	{ .compatible = "nvidia,tegra194-mc", .data = &tegra194_mc_soc },
#endif
#ifdef CONFIG_ARCH_TEGRA_234_SOC
	{ .compatible = "nvidia,tegra234-mc", .data = &tegra234_mc_soc },
#endif
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tegra_mc_of_match);

static void tegra_mc_devm_action_put_device(void *data)
{
	struct tegra_mc *mc = data;

	put_device(mc->dev);
}

/**
 * devm_tegra_memory_controller_get() - get Tegra Memory Controller handle
 * @dev: device pointer for the consumer device
 *
 * This function will search for the Memory Controller node in a device-tree
 * and retrieve the Memory Controller handle.
 *
 * Return: ERR_PTR() on error or a valid pointer to a struct tegra_mc.
 */
struct tegra_mc *devm_tegra_memory_controller_get(struct device *dev)
{
	struct platform_device *pdev;
	struct device_node *np;
	struct tegra_mc *mc;
	int err;

	np = of_parse_phandle(dev->of_node, "nvidia,memory-controller", 0);
	if (!np)
		return ERR_PTR(-ENOENT);

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return ERR_PTR(-ENODEV);

	mc = platform_get_drvdata(pdev);
	if (!mc) {
		put_device(&pdev->dev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	err = devm_add_action_or_reset(dev, tegra_mc_devm_action_put_device, mc);
	if (err)
		return ERR_PTR(err);

	return mc;
}
EXPORT_SYMBOL_GPL(devm_tegra_memory_controller_get);

int tegra_mc_probe_device(struct tegra_mc *mc, struct device *dev)
{
	if (mc->soc->ops && mc->soc->ops->probe_device)
		return mc->soc->ops->probe_device(mc, dev);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra_mc_probe_device);

static int tegra_mc_block_dma_common(struct tegra_mc *mc,
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

static bool tegra_mc_dma_idling_common(struct tegra_mc *mc,
				       const struct tegra_mc_reset *rst)
{
	return (mc_readl(mc, rst->status) & BIT(rst->bit)) != 0;
}

static int tegra_mc_unblock_dma_common(struct tegra_mc *mc,
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

static int tegra_mc_reset_status_common(struct tegra_mc *mc,
					const struct tegra_mc_reset *rst)
{
	return (mc_readl(mc, rst->control) & BIT(rst->bit)) != 0;
}

const struct tegra_mc_reset_ops tegra_mc_reset_ops_common = {
	.block_dma = tegra_mc_block_dma_common,
	.dma_idling = tegra_mc_dma_idling_common,
	.unblock_dma = tegra_mc_unblock_dma_common,
	.reset_status = tegra_mc_reset_status_common,
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

	/* DMA flushing will fail if reset is already asserted */
	if (rst_ops->reset_status) {
		/* check whether reset is asserted */
		if (rst_ops->reset_status(mc, rst))
			return 0;
	}

	if (rst_ops->block_dma) {
		/* block clients DMA requests */
		err = rst_ops->block_dma(mc, rst);
		if (err) {
			dev_err(mc->dev, "failed to block %s DMA: %d\n",
				rst->name, err);
			return err;
		}
	}

	if (rst_ops->dma_idling) {
		/* wait for completion of the outstanding DMA requests */
		while (!rst_ops->dma_idling(mc, rst)) {
			if (!retries--) {
				dev_err(mc->dev, "failed to flush %s DMA\n",
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
			dev_err(mc->dev, "failed to hot reset %s: %d\n",
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
			dev_err(mc->dev, "failed to deassert hot reset %s: %d\n",
				rst->name, err);
			return err;
		}
	}

	if (rst_ops->unblock_dma) {
		/* allow new DMA requests to proceed to arbitration */
		err = rst_ops->unblock_dma(mc, rst);
		if (err) {
			dev_err(mc->dev, "failed to unblock %s DMA : %d\n",
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

int tegra_mc_write_emem_configuration(struct tegra_mc *mc, unsigned long rate)
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
		return -EINVAL;
	}

	for (i = 0; i < mc->soc->num_emem_regs; ++i)
		mc_writel(mc, timing->emem_data[i], mc->soc->emem_regs[i]);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra_mc_write_emem_configuration);

unsigned int tegra_mc_get_emem_device_count(struct tegra_mc *mc)
{
	u8 dram_count;

	dram_count = mc_readl(mc, MC_EMEM_ADR_CFG);
	dram_count &= MC_EMEM_ADR_CFG_EMEM_NUMDEV;
	dram_count++;

	return dram_count;
}
EXPORT_SYMBOL_GPL(tegra_mc_get_emem_device_count);

#if defined(CONFIG_ARCH_TEGRA_3x_SOC) || \
    defined(CONFIG_ARCH_TEGRA_114_SOC) || \
    defined(CONFIG_ARCH_TEGRA_124_SOC) || \
    defined(CONFIG_ARCH_TEGRA_132_SOC) || \
    defined(CONFIG_ARCH_TEGRA_210_SOC)
static int tegra_mc_setup_latency_allowance(struct tegra_mc *mc)
{
	unsigned long long tick;
	unsigned int i;
	u32 value;

	/* compute the number of MC clock cycles per tick */
	tick = (unsigned long long)mc->tick * clk_get_rate(mc->clk);
	do_div(tick, NSEC_PER_SEC);

	value = mc_readl(mc, MC_EMEM_ARB_CFG);
	value &= ~MC_EMEM_ARB_CFG_CYCLES_PER_UPDATE_MASK;
	value |= MC_EMEM_ARB_CFG_CYCLES_PER_UPDATE(tick);
	mc_writel(mc, value, MC_EMEM_ARB_CFG);

	/* write latency allowance defaults */
	for (i = 0; i < mc->soc->num_clients; i++) {
		const struct tegra_mc_client *client = &mc->soc->clients[i];
		u32 value;

		value = mc_readl(mc, client->regs.la.reg);
		value &= ~(client->regs.la.mask << client->regs.la.shift);
		value |= (client->regs.la.def & client->regs.la.mask) << client->regs.la.shift;
		mc_writel(mc, value, client->regs.la.reg);
	}

	/* latch new values */
	mc_writel(mc, MC_TIMING_UPDATE, MC_TIMING_CONTROL);

	return 0;
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
			"timing %pOFn: failed to read rate\n", node);
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
			"timing %pOFn: failed to read EMEM configuration\n",
			node);
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

int tegra30_mc_probe(struct tegra_mc *mc)
{
	int err;

	mc->clk = devm_clk_get_optional(mc->dev, "mc");
	if (IS_ERR(mc->clk)) {
		dev_err(mc->dev, "failed to get MC clock: %ld\n", PTR_ERR(mc->clk));
		return PTR_ERR(mc->clk);
	}

	/* ensure that debug features are disabled */
	mc_writel(mc, 0x00000000, MC_TIMING_CONTROL_DBG);

	err = tegra_mc_setup_latency_allowance(mc);
	if (err < 0) {
		dev_err(mc->dev, "failed to setup latency allowance: %d\n", err);
		return err;
	}

	err = tegra_mc_setup_timings(mc);
	if (err < 0) {
		dev_err(mc->dev, "failed to setup timings: %d\n", err);
		return err;
	}

	return 0;
}

static irqreturn_t tegra30_mc_handle_irq(int irq, void *data)
{
	struct tegra_mc *mc = data;
	unsigned long status;
	unsigned int bit;

	/* mask all interrupts to avoid flooding */
	status = mc_readl(mc, MC_INTSTATUS) & mc->soc->intmask;
	if (!status)
		return IRQ_NONE;

	for_each_set_bit(bit, &status, 32) {
		const char *error = tegra_mc_status_names[bit] ?: "unknown";
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
		desc = tegra_mc_error_names[type];

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

const struct tegra_mc_ops tegra30_mc_ops = {
	.probe = tegra30_mc_probe,
	.handle_irq = tegra30_mc_handle_irq,
};
#endif

const char *const tegra_mc_status_names[32] = {
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

const char *const tegra_mc_error_names[8] = {
	[2] = "EMEM decode error",
	[3] = "TrustZone violation",
	[4] = "Carveout violation",
	[6] = "SMMU translation error",
};

/*
 * Memory Controller (MC) has few Memory Clients that are issuing memory
 * bandwidth allocation requests to the MC interconnect provider. The MC
 * provider aggregates the requests and then sends the aggregated request
 * up to the External Memory Controller (EMC) interconnect provider which
 * re-configures hardware interface to External Memory (EMEM) in accordance
 * to the required bandwidth. Each MC interconnect node represents an
 * individual Memory Client.
 *
 * Memory interconnect topology:
 *
 *               +----+
 * +--------+    |    |
 * | TEXSRD +--->+    |
 * +--------+    |    |
 *               |    |    +-----+    +------+
 *    ...        | MC +--->+ EMC +--->+ EMEM |
 *               |    |    +-----+    +------+
 * +--------+    |    |
 * | DISP.. +--->+    |
 * +--------+    |    |
 *               +----+
 */
static int tegra_mc_interconnect_setup(struct tegra_mc *mc)
{
	struct icc_node *node;
	unsigned int i;
	int err;

	/* older device-trees don't have interconnect properties */
	if (!device_property_present(mc->dev, "#interconnect-cells") ||
	    !mc->soc->icc_ops)
		return 0;

	mc->provider.dev = mc->dev;
	mc->provider.data = &mc->provider;
	mc->provider.set = mc->soc->icc_ops->set;
	mc->provider.aggregate = mc->soc->icc_ops->aggregate;
	mc->provider.xlate_extended = mc->soc->icc_ops->xlate_extended;

	err = icc_provider_add(&mc->provider);
	if (err)
		return err;

	/* create Memory Controller node */
	node = icc_node_create(TEGRA_ICC_MC);
	if (IS_ERR(node)) {
		err = PTR_ERR(node);
		goto del_provider;
	}

	node->name = "Memory Controller";
	icc_node_add(node, &mc->provider);

	/* link Memory Controller to External Memory Controller */
	err = icc_link_create(node, TEGRA_ICC_EMC);
	if (err)
		goto remove_nodes;

	for (i = 0; i < mc->soc->num_clients; i++) {
		/* create MC client node */
		node = icc_node_create(mc->soc->clients[i].id);
		if (IS_ERR(node)) {
			err = PTR_ERR(node);
			goto remove_nodes;
		}

		node->name = mc->soc->clients[i].name;
		icc_node_add(node, &mc->provider);

		/* link Memory Client to Memory Controller */
		err = icc_link_create(node, TEGRA_ICC_MC);
		if (err)
			goto remove_nodes;
	}

	return 0;

remove_nodes:
	icc_nodes_remove(&mc->provider);
del_provider:
	icc_provider_del(&mc->provider);

	return err;
}

static int tegra_mc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct tegra_mc *mc;
	u64 mask;
	int err;

	mc = devm_kzalloc(&pdev->dev, sizeof(*mc), GFP_KERNEL);
	if (!mc)
		return -ENOMEM;

	platform_set_drvdata(pdev, mc);
	spin_lock_init(&mc->lock);
	mc->soc = of_device_get_match_data(&pdev->dev);
	mc->dev = &pdev->dev;

	mask = DMA_BIT_MASK(mc->soc->num_address_bits);

	err = dma_coerce_mask_and_coherent(&pdev->dev, mask);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set DMA mask: %d\n", err);
		return err;
	}

	/* length of MC tick in nanoseconds */
	mc->tick = 30;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mc->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mc->regs))
		return PTR_ERR(mc->regs);

	mc->debugfs.root = debugfs_create_dir("mc", NULL);

	if (mc->soc->ops && mc->soc->ops->probe) {
		err = mc->soc->ops->probe(mc);
		if (err < 0)
			return err;
	}

	if (mc->soc->ops && mc->soc->ops->handle_irq) {
		mc->irq = platform_get_irq(pdev, 0);
		if (mc->irq < 0)
			return mc->irq;

		WARN(!mc->soc->client_id_mask, "missing client ID mask for this SoC\n");

		mc_writel(mc, mc->soc->intmask, MC_INTMASK);

		err = devm_request_irq(&pdev->dev, mc->irq, mc->soc->ops->handle_irq, 0,
				       dev_name(&pdev->dev), mc);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to request IRQ#%u: %d\n", mc->irq,
				err);
			return err;
		}
	}

	if (mc->soc->reset_ops) {
		err = tegra_mc_reset_setup(mc);
		if (err < 0)
			dev_err(&pdev->dev, "failed to register reset controller: %d\n", err);
	}

	err = tegra_mc_interconnect_setup(mc);
	if (err < 0)
		dev_err(&pdev->dev, "failed to initialize interconnect: %d\n",
			err);

	if (IS_ENABLED(CONFIG_TEGRA_IOMMU_SMMU) && mc->soc->smmu) {
		mc->smmu = tegra_smmu_probe(&pdev->dev, mc->soc->smmu, mc);
		if (IS_ERR(mc->smmu)) {
			dev_err(&pdev->dev, "failed to probe SMMU: %ld\n",
				PTR_ERR(mc->smmu));
			mc->smmu = NULL;
		}
	}

	if (IS_ENABLED(CONFIG_TEGRA_IOMMU_GART) && !mc->soc->smmu) {
		mc->gart = tegra_gart_probe(&pdev->dev, mc);
		if (IS_ERR(mc->gart)) {
			dev_err(&pdev->dev, "failed to probe GART: %ld\n",
				PTR_ERR(mc->gart));
			mc->gart = NULL;
		}
	}

	return 0;
}

static int __maybe_unused tegra_mc_suspend(struct device *dev)
{
	struct tegra_mc *mc = dev_get_drvdata(dev);

	if (mc->soc->ops && mc->soc->ops->suspend)
		return mc->soc->ops->suspend(mc);

	return 0;
}

static int __maybe_unused tegra_mc_resume(struct device *dev)
{
	struct tegra_mc *mc = dev_get_drvdata(dev);

	if (mc->soc->ops && mc->soc->ops->resume)
		return mc->soc->ops->resume(mc);

	return 0;
}

static void tegra_mc_sync_state(struct device *dev)
{
	struct tegra_mc *mc = dev_get_drvdata(dev);

	/* check whether ICC provider is registered */
	if (mc->provider.dev == dev)
		icc_sync_state(dev);
}

static const struct dev_pm_ops tegra_mc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tegra_mc_suspend, tegra_mc_resume)
};

static struct platform_driver tegra_mc_driver = {
	.driver = {
		.name = "tegra-mc",
		.of_match_table = tegra_mc_of_match,
		.pm = &tegra_mc_pm_ops,
		.suppress_bind_attrs = true,
		.sync_state = tegra_mc_sync_state,
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
