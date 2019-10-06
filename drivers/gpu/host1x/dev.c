// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra host1x driver
 *
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/slab.h>

#define CREATE_TRACE_POINTS
#include <trace/events/host1x.h>
#undef CREATE_TRACE_POINTS

#if IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU)
#include <asm/dma-iommu.h>
#endif

#include "bus.h"
#include "channel.h"
#include "debug.h"
#include "dev.h"
#include "intr.h"

#include "hw/host1x01.h"
#include "hw/host1x02.h"
#include "hw/host1x04.h"
#include "hw/host1x05.h"
#include "hw/host1x06.h"
#include "hw/host1x07.h"

void host1x_hypervisor_writel(struct host1x *host1x, u32 v, u32 r)
{
	writel(v, host1x->hv_regs + r);
}

u32 host1x_hypervisor_readl(struct host1x *host1x, u32 r)
{
	return readl(host1x->hv_regs + r);
}

void host1x_sync_writel(struct host1x *host1x, u32 v, u32 r)
{
	void __iomem *sync_regs = host1x->regs + host1x->info->sync_offset;

	writel(v, sync_regs + r);
}

u32 host1x_sync_readl(struct host1x *host1x, u32 r)
{
	void __iomem *sync_regs = host1x->regs + host1x->info->sync_offset;

	return readl(sync_regs + r);
}

void host1x_ch_writel(struct host1x_channel *ch, u32 v, u32 r)
{
	writel(v, ch->regs + r);
}

u32 host1x_ch_readl(struct host1x_channel *ch, u32 r)
{
	return readl(ch->regs + r);
}

static const struct host1x_info host1x01_info = {
	.nb_channels = 8,
	.nb_pts = 32,
	.nb_mlocks = 16,
	.nb_bases = 8,
	.init = host1x01_init,
	.sync_offset = 0x3000,
	.dma_mask = DMA_BIT_MASK(32),
};

static const struct host1x_info host1x02_info = {
	.nb_channels = 9,
	.nb_pts = 32,
	.nb_mlocks = 16,
	.nb_bases = 12,
	.init = host1x02_init,
	.sync_offset = 0x3000,
	.dma_mask = DMA_BIT_MASK(32),
};

static const struct host1x_info host1x04_info = {
	.nb_channels = 12,
	.nb_pts = 192,
	.nb_mlocks = 16,
	.nb_bases = 64,
	.init = host1x04_init,
	.sync_offset = 0x2100,
	.dma_mask = DMA_BIT_MASK(34),
};

static const struct host1x_info host1x05_info = {
	.nb_channels = 14,
	.nb_pts = 192,
	.nb_mlocks = 16,
	.nb_bases = 64,
	.init = host1x05_init,
	.sync_offset = 0x2100,
	.dma_mask = DMA_BIT_MASK(34),
};

static const struct host1x_sid_entry tegra186_sid_table[] = {
	{
		/* VIC */
		.base = 0x1af0,
		.offset = 0x30,
		.limit = 0x34
	},
};

static const struct host1x_info host1x06_info = {
	.nb_channels = 63,
	.nb_pts = 576,
	.nb_mlocks = 24,
	.nb_bases = 16,
	.init = host1x06_init,
	.sync_offset = 0x0,
	.dma_mask = DMA_BIT_MASK(40),
	.has_hypervisor = true,
	.num_sid_entries = ARRAY_SIZE(tegra186_sid_table),
	.sid_table = tegra186_sid_table,
};

static const struct host1x_sid_entry tegra194_sid_table[] = {
	{
		/* VIC */
		.base = 0x1af0,
		.offset = 0x30,
		.limit = 0x34
	},
};

static const struct host1x_info host1x07_info = {
	.nb_channels = 63,
	.nb_pts = 704,
	.nb_mlocks = 32,
	.nb_bases = 0,
	.init = host1x07_init,
	.sync_offset = 0x0,
	.dma_mask = DMA_BIT_MASK(40),
	.has_hypervisor = true,
	.num_sid_entries = ARRAY_SIZE(tegra194_sid_table),
	.sid_table = tegra194_sid_table,
};

static const struct of_device_id host1x_of_match[] = {
	{ .compatible = "nvidia,tegra194-host1x", .data = &host1x07_info, },
	{ .compatible = "nvidia,tegra186-host1x", .data = &host1x06_info, },
	{ .compatible = "nvidia,tegra210-host1x", .data = &host1x05_info, },
	{ .compatible = "nvidia,tegra124-host1x", .data = &host1x04_info, },
	{ .compatible = "nvidia,tegra114-host1x", .data = &host1x02_info, },
	{ .compatible = "nvidia,tegra30-host1x", .data = &host1x01_info, },
	{ .compatible = "nvidia,tegra20-host1x", .data = &host1x01_info, },
	{ },
};
MODULE_DEVICE_TABLE(of, host1x_of_match);

static void host1x_setup_sid_table(struct host1x *host)
{
	const struct host1x_info *info = host->info;
	unsigned int i;

	for (i = 0; i < info->num_sid_entries; i++) {
		const struct host1x_sid_entry *entry = &info->sid_table[i];

		host1x_hypervisor_writel(host, entry->offset, entry->base);
		host1x_hypervisor_writel(host, entry->limit, entry->base + 4);
	}
}

static int host1x_probe(struct platform_device *pdev)
{
	struct host1x *host;
	struct resource *regs, *hv_regs = NULL;
	int syncpt_irq;
	int err;

	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->info = of_device_get_match_data(&pdev->dev);

	if (host->info->has_hypervisor) {
		regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vm");
		if (!regs) {
			dev_err(&pdev->dev, "failed to get vm registers\n");
			return -ENXIO;
		}

		hv_regs = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						       "hypervisor");
		if (!hv_regs) {
			dev_err(&pdev->dev,
				"failed to get hypervisor registers\n");
			return -ENXIO;
		}
	} else {
		regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!regs) {
			dev_err(&pdev->dev, "failed to get registers\n");
			return -ENXIO;
		}
	}

	syncpt_irq = platform_get_irq(pdev, 0);
	if (syncpt_irq < 0) {
		dev_err(&pdev->dev, "failed to get IRQ: %d\n", syncpt_irq);
		return syncpt_irq;
	}

	mutex_init(&host->devices_lock);
	INIT_LIST_HEAD(&host->devices);
	INIT_LIST_HEAD(&host->list);
	host->dev = &pdev->dev;

	/* set common host1x device data */
	platform_set_drvdata(pdev, host);

	host->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(host->regs))
		return PTR_ERR(host->regs);

	if (host->info->has_hypervisor) {
		host->hv_regs = devm_ioremap_resource(&pdev->dev, hv_regs);
		if (IS_ERR(host->hv_regs))
			return PTR_ERR(host->hv_regs);
	}

	dma_set_mask_and_coherent(host->dev, host->info->dma_mask);

	if (host->info->init) {
		err = host->info->init(host);
		if (err)
			return err;
	}

	host->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(host->clk)) {
		err = PTR_ERR(host->clk);

		if (err != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get clock: %d\n", err);

		return err;
	}

	host->rst = devm_reset_control_get(&pdev->dev, "host1x");
	if (IS_ERR(host->rst)) {
		err = PTR_ERR(host->rst);
		dev_err(&pdev->dev, "failed to get reset: %d\n", err);
		return err;
	}
#if IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU)
	if (host->dev->archdata.mapping) {
		struct dma_iommu_mapping *mapping =
				to_dma_iommu_mapping(host->dev);
		arm_iommu_detach_device(host->dev);
		arm_iommu_release_mapping(mapping);
	}
#endif
	if (IS_ENABLED(CONFIG_TEGRA_HOST1X_FIREWALL))
		goto skip_iommu;

	host->group = iommu_group_get(&pdev->dev);
	if (host->group) {
		struct iommu_domain_geometry *geometry;
		u64 mask = dma_get_mask(host->dev);
		dma_addr_t start, end;
		unsigned long order;

		err = iova_cache_get();
		if (err < 0)
			goto put_group;

		host->domain = iommu_domain_alloc(&platform_bus_type);
		if (!host->domain) {
			err = -ENOMEM;
			goto put_cache;
		}

		err = iommu_attach_group(host->domain, host->group);
		if (err) {
			if (err == -ENODEV) {
				iommu_domain_free(host->domain);
				host->domain = NULL;
				iova_cache_put();
				iommu_group_put(host->group);
				host->group = NULL;
				goto skip_iommu;
			}

			goto fail_free_domain;
		}

		geometry = &host->domain->geometry;
		start = geometry->aperture_start & mask;
		end = geometry->aperture_end & mask;

		order = __ffs(host->domain->pgsize_bitmap);
		init_iova_domain(&host->iova, 1UL << order, start >> order);
		host->iova_end = end;
	}

skip_iommu:
	err = host1x_channel_list_init(&host->channel_list,
				       host->info->nb_channels);
	if (err) {
		dev_err(&pdev->dev, "failed to initialize channel list\n");
		goto fail_detach_device;
	}

	err = clk_prepare_enable(host->clk);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to enable clock\n");
		goto fail_free_channels;
	}

	err = reset_control_deassert(host->rst);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to deassert reset: %d\n", err);
		goto fail_unprepare_disable;
	}

	err = host1x_syncpt_init(host);
	if (err) {
		dev_err(&pdev->dev, "failed to initialize syncpts\n");
		goto fail_reset_assert;
	}

	err = host1x_intr_init(host, syncpt_irq);
	if (err) {
		dev_err(&pdev->dev, "failed to initialize interrupts\n");
		goto fail_deinit_syncpt;
	}

	host1x_debug_init(host);

	if (host->info->has_hypervisor)
		host1x_setup_sid_table(host);

	err = host1x_register(host);
	if (err < 0)
		goto fail_deinit_intr;

	return 0;

fail_deinit_intr:
	host1x_intr_deinit(host);
fail_deinit_syncpt:
	host1x_syncpt_deinit(host);
fail_reset_assert:
	reset_control_assert(host->rst);
fail_unprepare_disable:
	clk_disable_unprepare(host->clk);
fail_free_channels:
	host1x_channel_list_free(&host->channel_list);
fail_detach_device:
	if (host->group && host->domain) {
		put_iova_domain(&host->iova);
		iommu_detach_group(host->domain, host->group);
	}
fail_free_domain:
	if (host->domain)
		iommu_domain_free(host->domain);
put_cache:
	if (host->group)
		iova_cache_put();
put_group:
	iommu_group_put(host->group);

	return err;
}

static int host1x_remove(struct platform_device *pdev)
{
	struct host1x *host = platform_get_drvdata(pdev);

	host1x_unregister(host);
	host1x_intr_deinit(host);
	host1x_syncpt_deinit(host);
	reset_control_assert(host->rst);
	clk_disable_unprepare(host->clk);

	if (host->domain) {
		put_iova_domain(&host->iova);
		iommu_detach_group(host->domain, host->group);
		iommu_domain_free(host->domain);
		iova_cache_put();
		iommu_group_put(host->group);
	}

	return 0;
}

static struct platform_driver tegra_host1x_driver = {
	.driver = {
		.name = "tegra-host1x",
		.of_match_table = host1x_of_match,
	},
	.probe = host1x_probe,
	.remove = host1x_remove,
};

static struct platform_driver * const drivers[] = {
	&tegra_host1x_driver,
	&tegra_mipi_driver,
};

static int __init tegra_host1x_init(void)
{
	int err;

	err = bus_register(&host1x_bus_type);
	if (err < 0)
		return err;

	err = platform_register_drivers(drivers, ARRAY_SIZE(drivers));
	if (err < 0)
		bus_unregister(&host1x_bus_type);

	return err;
}
module_init(tegra_host1x_init);

static void __exit tegra_host1x_exit(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
	bus_unregister(&host1x_bus_type);
}
module_exit(tegra_host1x_exit);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_AUTHOR("Terje Bergstrom <tbergstrom@nvidia.com>");
MODULE_DESCRIPTION("Host1x driver for Tegra products");
MODULE_LICENSE("GPL");
