// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AppliedMicro X-Gene SoC Reboot Driver
 *
 * Copyright (c) 2013, Applied Micro Circuits Corporation
 * Author: Feng Kan <fkan@apm.com>
 * Author: Loc Ho <lho@apm.com>
 *
 * This driver provides system reboot functionality for APM X-Gene SoC.
 * For system shutdown, this is board specify. If a board designer
 * implements GPIO shutdown, use the gpio-poweroff.c driver.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/stat.h>
#include <linux/slab.h>

struct xgene_reboot_context {
	struct device *dev;
	void __iomem *csr;
	u32 mask;
};

static int xgene_restart_handler(struct sys_off_data *data)
{
	struct xgene_reboot_context *ctx = data->cb_data;

	/* Issue the reboot */
	writel(ctx->mask, ctx->csr);

	mdelay(1000);

	dev_emerg(ctx->dev, "Unable to restart system\n");

	return NOTIFY_DONE;
}

static int xgene_reboot_probe(struct platform_device *pdev)
{
	struct xgene_reboot_context *ctx;
	struct device *dev = &pdev->dev;
	int err;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->csr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ctx->csr)) {
		dev_err(dev, "can not map resource\n");
		return PTR_ERR(ctx->csr);
	}

	if (of_property_read_u32(dev->of_node, "mask", &ctx->mask))
		ctx->mask = 0xFFFFFFFF;

	ctx->dev = dev;
	err = devm_register_sys_off_handler(dev, SYS_OFF_MODE_RESTART, 128,
					    xgene_restart_handler, ctx);
	if (err)
		dev_err(dev, "cannot register restart handler (err=%d)\n", err);

	return err;
}

static const struct of_device_id xgene_reboot_of_match[] = {
	{ .compatible = "apm,xgene-reboot" },
	{}
};

static struct platform_driver xgene_reboot_driver = {
	.probe = xgene_reboot_probe,
	.driver = {
		.name = "xgene-reboot",
		.of_match_table = xgene_reboot_of_match,
	},
};
builtin_platform_driver(xgene_reboot_driver);
