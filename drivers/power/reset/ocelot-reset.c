// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi MIPS SoC reset driver
 *
 * License: Dual MIT/GPL
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

struct ocelot_reset_context {
	void __iomem *base;
	struct regmap *cpu_ctrl;
	struct notifier_block restart_handler;
};

#define ICPU_CFG_CPU_SYSTEM_CTRL_RESET 0x20
#define CORE_RST_PROTECT BIT(2)

#define SOFT_CHIP_RST BIT(0)

static int ocelot_restart_handle(struct notifier_block *this,
				 unsigned long mode, void *cmd)
{
	struct ocelot_reset_context *ctx = container_of(this, struct
							ocelot_reset_context,
							restart_handler);

	/* Make sure the core is not protected from reset */
	regmap_update_bits(ctx->cpu_ctrl, ICPU_CFG_CPU_SYSTEM_CTRL_RESET,
			   CORE_RST_PROTECT, 0);

	writel(SOFT_CHIP_RST, ctx->base);

	pr_emerg("Unable to restart system\n");
	return NOTIFY_DONE;
}

static int ocelot_reset_probe(struct platform_device *pdev)
{
	struct ocelot_reset_context *ctx;
	struct resource *res;

	struct device *dev = &pdev->dev;
	int err;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ctx->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctx->base))
		return PTR_ERR(ctx->base);

	ctx->cpu_ctrl = syscon_regmap_lookup_by_compatible("mscc,ocelot-cpu-syscon");
	if (IS_ERR(ctx->cpu_ctrl))
		return PTR_ERR(ctx->cpu_ctrl);

	ctx->restart_handler.notifier_call = ocelot_restart_handle;
	ctx->restart_handler.priority = 192;
	err = register_restart_handler(&ctx->restart_handler);
	if (err)
		dev_err(dev, "can't register restart notifier (err=%d)\n", err);

	return err;
}

static const struct of_device_id ocelot_reset_of_match[] = {
	{ .compatible = "mscc,ocelot-chip-reset" },
	{}
};

static struct platform_driver ocelot_reset_driver = {
	.probe = ocelot_reset_probe,
	.driver = {
		.name = "ocelot-chip-reset",
		.of_match_table = ocelot_reset_of_match,
	},
};
builtin_platform_driver(ocelot_reset_driver);
