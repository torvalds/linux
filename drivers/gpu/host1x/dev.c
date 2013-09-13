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

#include <linux/module.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/io.h>

#define CREATE_TRACE_POINTS
#include <trace/events/host1x.h>

#include "dev.h"
#include "intr.h"
#include "channel.h"
#include "debug.h"
#include "hw/host1x01.h"
#include "host1x_client.h"

void host1x_set_drm_data(struct device *dev, void *data)
{
	struct host1x *host1x = dev_get_drvdata(dev);
	host1x->drm_data = data;
}

void *host1x_get_drm_data(struct device *dev)
{
	struct host1x *host1x = dev_get_drvdata(dev);
	return host1x ? host1x->drm_data : NULL;
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
	.nb_channels	= 8,
	.nb_pts		= 32,
	.nb_mlocks	= 16,
	.nb_bases	= 8,
	.init		= host1x01_init,
	.sync_offset	= 0x3000,
};

static struct of_device_id host1x_of_match[] = {
	{ .compatible = "nvidia,tegra30-host1x", .data = &host1x01_info, },
	{ .compatible = "nvidia,tegra20-host1x", .data = &host1x01_info, },
	{ },
};
MODULE_DEVICE_TABLE(of, host1x_of_match);

static int host1x_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct host1x *host;
	struct resource *regs;
	int syncpt_irq;
	int err;

	id = of_match_device(host1x_of_match, &pdev->dev);
	if (!id)
		return -EINVAL;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "failed to get registers\n");
		return -ENXIO;
	}

	syncpt_irq = platform_get_irq(pdev, 0);
	if (syncpt_irq < 0) {
		dev_err(&pdev->dev, "failed to get IRQ\n");
		return -ENXIO;
	}

	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->dev = &pdev->dev;
	host->info = id->data;

	/* set common host1x device data */
	platform_set_drvdata(pdev, host);

	host->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(host->regs))
		return PTR_ERR(host->regs);

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

	err = host1x_channel_list_init(host);
	if (err) {
		dev_err(&pdev->dev, "failed to initialize channel list\n");
		return err;
	}

	err = clk_prepare_enable(host->clk);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to enable clock\n");
		return err;
	}

	err = host1x_syncpt_init(host);
	if (err) {
		dev_err(&pdev->dev, "failed to initialize syncpts\n");
		return err;
	}

	err = host1x_intr_init(host, syncpt_irq);
	if (err) {
		dev_err(&pdev->dev, "failed to initialize interrupts\n");
		goto fail_deinit_syncpt;
	}

	host1x_debug_init(host);

	host1x_drm_alloc(pdev);

	return 0;

fail_deinit_syncpt:
	host1x_syncpt_deinit(host);
	return err;
}

static int __exit host1x_remove(struct platform_device *pdev)
{
	struct host1x *host = platform_get_drvdata(pdev);

	host1x_intr_deinit(host);
	host1x_syncpt_deinit(host);
	clk_disable_unprepare(host->clk);

	return 0;
}

static struct platform_driver tegra_host1x_driver = {
	.probe = host1x_probe,
	.remove = __exit_p(host1x_remove),
	.driver = {
		.owner = THIS_MODULE,
		.name = "tegra-host1x",
		.of_match_table = host1x_of_match,
	},
};

static int __init tegra_host1x_init(void)
{
	int err;

	err = platform_driver_register(&tegra_host1x_driver);
	if (err < 0)
		return err;

#ifdef CONFIG_DRM_TEGRA
	err = platform_driver_register(&tegra_dc_driver);
	if (err < 0)
		goto unregister_host1x;

	err = platform_driver_register(&tegra_hdmi_driver);
	if (err < 0)
		goto unregister_dc;

	err = platform_driver_register(&tegra_gr2d_driver);
	if (err < 0)
		goto unregister_hdmi;
#endif

	return 0;

#ifdef CONFIG_DRM_TEGRA
unregister_hdmi:
	platform_driver_unregister(&tegra_hdmi_driver);
unregister_dc:
	platform_driver_unregister(&tegra_dc_driver);
unregister_host1x:
	platform_driver_unregister(&tegra_host1x_driver);
	return err;
#endif
}
module_init(tegra_host1x_init);

static void __exit tegra_host1x_exit(void)
{
#ifdef CONFIG_DRM_TEGRA
	platform_driver_unregister(&tegra_gr2d_driver);
	platform_driver_unregister(&tegra_hdmi_driver);
	platform_driver_unregister(&tegra_dc_driver);
#endif
	platform_driver_unregister(&tegra_host1x_driver);
}
module_exit(tegra_host1x_exit);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_AUTHOR("Terje Bergstrom <tbergstrom@nvidia.com>");
MODULE_DESCRIPTION("Host1x driver for Tegra products");
MODULE_LICENSE("GPL");
