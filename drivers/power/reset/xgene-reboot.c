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
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <asm/system_misc.h>

struct xgene_reboot_context {
	struct platform_device *pdev;
	void *csr;
	u32 mask;
};

static struct xgene_reboot_context *xgene_restart_ctx;

static void xgene_restart(enum reboot_mode mode, const char *cmd)
{
	struct xgene_reboot_context *ctx = xgene_restart_ctx;
	unsigned long timeout;

	/* Issue the reboot */
	if (ctx)
		writel(ctx->mask, ctx->csr);

	timeout = jiffies + HZ;
	while (time_before(jiffies, timeout))
		cpu_relax();

	dev_emerg(&ctx->pdev->dev, "Unable to restart system\n");
}

static int xgene_reboot_probe(struct platform_device *pdev)
{
	struct xgene_reboot_context *ctx;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		dev_err(&pdev->dev, "out of memory for context\n");
		return -ENODEV;
	}

	ctx->csr = of_iomap(pdev->dev.of_node, 0);
	if (!ctx->csr) {
		devm_kfree(&pdev->dev, ctx);
		dev_err(&pdev->dev, "can not map resource\n");
		return -ENODEV;
	}

	if (of_property_read_u32(pdev->dev.of_node, "mask", &ctx->mask))
		ctx->mask = 0xFFFFFFFF;

	ctx->pdev = pdev;
	arm_pm_restart = xgene_restart;
	xgene_restart_ctx = ctx;

	return 0;
}

static struct of_device_id xgene_reboot_of_match[] = {
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
