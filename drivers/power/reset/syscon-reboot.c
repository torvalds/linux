// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic Syscon Reboot Driver
 *
 * Copyright (c) 2013, Applied Micro Circuits Corporation
 * Author: Feng Kan <fkan@apm.com>
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

struct reboot_mode_bits {
	u32 offset;
	u32 mask;
	u32 value;
	bool valid;
};

struct reboot_data {
	struct reboot_mode_bits mode_bits[REBOOT_SOFT + 1];
	struct reboot_mode_bits catchall;
};

struct syscon_reboot_context {
	struct regmap *map;

	const struct reboot_data *rd; /* from of match data, if any */
	struct reboot_mode_bits catchall; /* from DT */

	struct notifier_block restart_handler;
};

static int syscon_restart_handle(struct notifier_block *this,
					unsigned long mode, void *cmd)
{
	struct syscon_reboot_context *ctx =
			container_of(this, struct syscon_reboot_context,
					restart_handler);
	const struct reboot_mode_bits *mode_bits;

	if (ctx->rd) {
		if (mode < ARRAY_SIZE(ctx->rd->mode_bits) &&
		    ctx->rd->mode_bits[mode].valid)
			mode_bits = &ctx->rd->mode_bits[mode];
		else
			mode_bits = &ctx->rd->catchall;
	} else {
		mode_bits = &ctx->catchall;
	}

	/* Issue the reboot */
	regmap_update_bits(ctx->map, mode_bits->offset, mode_bits->mask,
			   mode_bits->value);

	mdelay(1000);

	pr_emerg("Unable to restart system\n");
	return NOTIFY_DONE;
}

static int syscon_reboot_probe(struct platform_device *pdev)
{
	struct syscon_reboot_context *ctx;
	struct device *dev = &pdev->dev;
	int priority;
	int err;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->map = syscon_regmap_lookup_by_phandle(dev->of_node, "regmap");
	if (IS_ERR(ctx->map)) {
		ctx->map = syscon_node_to_regmap(dev->parent->of_node);
		if (IS_ERR(ctx->map))
			return PTR_ERR(ctx->map);
	}

	if (of_property_read_s32(pdev->dev.of_node, "priority", &priority))
		priority = 192;

	ctx->rd = of_device_get_match_data(dev);
	if (!ctx->rd) {
		int mask_err, value_err;

		if (of_property_read_u32(pdev->dev.of_node, "offset",
					 &ctx->catchall.offset) &&
		    of_property_read_u32(pdev->dev.of_node, "reg",
					 &ctx->catchall.offset))
			return -EINVAL;

		value_err = of_property_read_u32(pdev->dev.of_node, "value",
						 &ctx->catchall.value);
		mask_err = of_property_read_u32(pdev->dev.of_node, "mask",
						&ctx->catchall.mask);
		if (value_err && mask_err) {
			dev_err(dev, "unable to read 'value' and 'mask'");
			return -EINVAL;
		}

		if (value_err) {
			/* support old binding */
			ctx->catchall.value = ctx->catchall.mask;
			ctx->catchall.mask = 0xFFFFFFFF;
		} else if (mask_err) {
			/* support value without mask */
			ctx->catchall.mask = 0xFFFFFFFF;
		}
	}

	ctx->restart_handler.notifier_call = syscon_restart_handle;
	ctx->restart_handler.priority = priority;
	err = register_restart_handler(&ctx->restart_handler);
	if (err)
		dev_err(dev, "can't register restart notifier (err=%d)\n", err);

	return err;
}

static const struct reboot_data gs101_reboot_data = {
	.mode_bits = {
		[REBOOT_WARM] = {
			.offset = 0x3a00, /* SYSTEM_CONFIGURATION */
			.mask = 0x00000002, /* SWRESET_SYSTEM */
			.value = 0x00000002,
			.valid = true,
		},
		[REBOOT_SOFT] = {
			.offset = 0x3a00, /* SYSTEM_CONFIGURATION */
			.mask = 0x00000002, /* SWRESET_SYSTEM */
			.value = 0x00000002,
			.valid = true,
		},
	},
	.catchall = {
		.offset = 0x3e9c, /* PAD_CTRL_PWR_HOLD */
		.mask = 0x00000100,
		.value = 0x00000000,
	},
};

static const struct of_device_id syscon_reboot_of_match[] = {
	{ .compatible = "google,gs101-reboot", .data = &gs101_reboot_data  },
	{ .compatible = "syscon-reboot" },
	{}
};

static struct platform_driver syscon_reboot_driver = {
	.probe = syscon_reboot_probe,
	.driver = {
		.name = "syscon-reboot",
		.of_match_table = syscon_reboot_of_match,
	},
};
builtin_platform_driver(syscon_reboot_driver);
