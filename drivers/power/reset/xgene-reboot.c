/*
 * AppliedMicro X-Gene SoC Reboot Driver
 *
 * Copyright (c) 2013, Applied Micro Circuits Corporation
 * Author: Feng Kan <fkan@apm.com>
 * Author: Loc Ho <lho@apm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 * This driver provides system reboot functionality for APM X-Gene SoC.
 * For system shutdown, this is board specify. If a board designer
 * implements GPIO shutdown, use the gpio-poweroff.c driver.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/stat.h>
#include <linux/slab.h>

struct xgene_reboot_context {
	struct device *dev;
	void *csr;
	u32 mask;
	struct notifier_block restart_handler;
};

static int xgene_restart_handler(struct notifier_block *this,
				 unsigned long mode, void *cmd)
{
	struct xgene_reboot_context *ctx =
		container_of(this, struct xgene_reboot_context,
			     restart_handler);

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

	ctx->csr = of_iomap(dev->of_node, 0);
	if (!ctx->csr) {
		dev_err(dev, "can not map resource\n");
		return -ENODEV;
	}

	if (of_property_read_u32(dev->of_node, "mask", &ctx->mask))
		ctx->mask = 0xFFFFFFFF;

	ctx->dev = dev;
	ctx->restart_handler.notifier_call = xgene_restart_handler;
	ctx->restart_handler.priority = 128;
	err = register_restart_handler(&ctx->restart_handler);
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

static int __init xgene_reboot_init(void)
{
	return platform_driver_register(&xgene_reboot_driver);
}
device_initcall(xgene_reboot_init);
