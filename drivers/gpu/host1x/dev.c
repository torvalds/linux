/*
 * Tegra host1x driver
 *
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

static const struct host1x_info host1x06_info = {
	.nb_channels = 63,
	.nb_pts = 576,
	.nb_mlocks = 24,
	.nb_bases = 16,
	.init = host1x06_init,
	.sync_offset = 0x0,
	.dma_mask = DMA_BIT_MASK(34),
	.has_hypervisor = true,
};

static const struct of_device_id host1x_of_match[] = {
	{ .compatible = "nvidia,tegra186-host1x", .data = &host1x06_info, },
	{ .compatible = "nvidia,tegra210-host1x", .data = &host1x05_info, },
	{ .compatible = "nvidia,tegra124-host1x", .data = &host1x04_info, },
	{ .compatible = "nvidia,tegra114-host1x", .data = &host1x02_info, },
	{ .compatible = "nvidia,tegra30-host1x", .data = &host1x01_info, },
	{ .compatible = "nvidia,tegra20-host1x", .data = &host1x01_info, },
	{ },
};
MODULE_DEVICE_TABLE(of, host1x_of_match);

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
		dev_err(&pdev->dev, "failed to get clock\n");
		err = PTR_ERR(host->clk);
		return err;
	}

	host->rst = devm_reset_control_get(&pdev->dev, "host1x");
	if (IS_ERR(host->rst)) {
		err = PTR_ERR(host->rst);
		dev_err(&pdev->dev, "failed to get reset: %d\n", err);
		return err;
	}

	host->group = iommu_group_get(&pdev->dev);
	if (host->group) {
		struct iommu_domain_geometry *geometry;
		unsigned long order;

		host->domain = iommu_domain_alloc(&platform_bus_type);
		if (!host->domain) {
			err = -ENOMEM;
			goto put_group;
		}

		err = iommu_attach_group(host->domain, host->group);
		if (err) {
			if (err == -ENODEV) {
				iommu_domain_free(host->domain);
				host->domain = NULL;
				iommu_group_put(host->group);
				host->group = NULL;
				goto skip_iommu;
			}

			goto fail_free_domain;
		}

		geometry = &host->domain->geometry;

		order = __ffs(host->domain->pgsize_bitmap);
		init_iova_domain(&host->iova, 1UL << order,
				 geometry->aperture_start >> order);
		host->iova_end = geometry->aperture_end;
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
