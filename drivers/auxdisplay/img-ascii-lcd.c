// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "line-display.h"

struct img_ascii_lcd_ctx;

/**
 * struct img_ascii_lcd_config - Configuration information about an LCD model
 * @num_chars: the number of characters the LCD can display
 * @external_regmap: true if registers are in a system controller, else false
 * @update: function called to update the LCD
 */
struct img_ascii_lcd_config {
	unsigned int num_chars;
	bool external_regmap;
	void (*update)(struct linedisp *linedisp);
};

/**
 * struct img_ascii_lcd_ctx - Private data structure
 * @base: the base address of the LCD registers
 * @regmap: the regmap through which LCD registers are accessed
 * @offset: the offset within regmap to the start of the LCD registers
 * @cfg: pointer to the LCD model configuration
 * @linedisp: line display structure
 * @curr: the string currently displayed on the LCD
 */
struct img_ascii_lcd_ctx {
	union {
		void __iomem *base;
		struct regmap *regmap;
	};
	u32 offset;
	const struct img_ascii_lcd_config *cfg;
	struct linedisp linedisp;
	char curr[] __aligned(8);
};

/*
 * MIPS Boston development board
 */

static void boston_update(struct linedisp *linedisp)
{
	struct img_ascii_lcd_ctx *ctx =
		container_of(linedisp, struct img_ascii_lcd_ctx, linedisp);
	ulong val;

#if BITS_PER_LONG == 64
	val = *((u64 *)&ctx->curr[0]);
	__raw_writeq(val, ctx->base);
#elif BITS_PER_LONG == 32
	val = *((u32 *)&ctx->curr[0]);
	__raw_writel(val, ctx->base);
	val = *((u32 *)&ctx->curr[4]);
	__raw_writel(val, ctx->base + 4);
#else
# error Not 32 or 64 bit
#endif
}

static struct img_ascii_lcd_config boston_config = {
	.num_chars = 8,
	.update = boston_update,
};

/*
 * MIPS Malta development board
 */

static void malta_update(struct linedisp *linedisp)
{
	struct img_ascii_lcd_ctx *ctx =
		container_of(linedisp, struct img_ascii_lcd_ctx, linedisp);
	unsigned int i;
	int err = 0;

	for (i = 0; i < linedisp->num_chars; i++) {
		err = regmap_write(ctx->regmap,
				   ctx->offset + (i * 8), ctx->curr[i]);
		if (err)
			break;
	}

	if (unlikely(err))
		pr_err_ratelimited("Failed to update LCD display: %d\n", err);
}

static struct img_ascii_lcd_config malta_config = {
	.num_chars = 8,
	.external_regmap = true,
	.update = malta_update,
};

/*
 * MIPS SEAD3 development board
 */

enum {
	SEAD3_REG_LCD_CTRL		= 0x00,
#define SEAD3_REG_LCD_CTRL_SETDRAM	BIT(7)
	SEAD3_REG_LCD_DATA		= 0x08,
	SEAD3_REG_CPLD_STATUS		= 0x10,
#define SEAD3_REG_CPLD_STATUS_BUSY	BIT(0)
	SEAD3_REG_CPLD_DATA		= 0x18,
#define SEAD3_REG_CPLD_DATA_BUSY	BIT(7)
};

static int sead3_wait_sm_idle(struct img_ascii_lcd_ctx *ctx)
{
	unsigned int status;
	int err;

	do {
		err = regmap_read(ctx->regmap,
				  ctx->offset + SEAD3_REG_CPLD_STATUS,
				  &status);
		if (err)
			return err;
	} while (status & SEAD3_REG_CPLD_STATUS_BUSY);

	return 0;

}

static int sead3_wait_lcd_idle(struct img_ascii_lcd_ctx *ctx)
{
	unsigned int cpld_data;
	int err;

	err = sead3_wait_sm_idle(ctx);
	if (err)
		return err;

	do {
		err = regmap_read(ctx->regmap,
				  ctx->offset + SEAD3_REG_LCD_CTRL,
				  &cpld_data);
		if (err)
			return err;

		err = sead3_wait_sm_idle(ctx);
		if (err)
			return err;

		err = regmap_read(ctx->regmap,
				  ctx->offset + SEAD3_REG_CPLD_DATA,
				  &cpld_data);
		if (err)
			return err;
	} while (cpld_data & SEAD3_REG_CPLD_DATA_BUSY);

	return 0;
}

static void sead3_update(struct linedisp *linedisp)
{
	struct img_ascii_lcd_ctx *ctx =
		container_of(linedisp, struct img_ascii_lcd_ctx, linedisp);
	unsigned int i;
	int err = 0;

	for (i = 0; i < linedisp->num_chars; i++) {
		err = sead3_wait_lcd_idle(ctx);
		if (err)
			break;

		err = regmap_write(ctx->regmap,
				   ctx->offset + SEAD3_REG_LCD_CTRL,
				   SEAD3_REG_LCD_CTRL_SETDRAM | i);
		if (err)
			break;

		err = sead3_wait_lcd_idle(ctx);
		if (err)
			break;

		err = regmap_write(ctx->regmap,
				   ctx->offset + SEAD3_REG_LCD_DATA,
				   ctx->curr[i]);
		if (err)
			break;
	}

	if (unlikely(err))
		pr_err_ratelimited("Failed to update LCD display: %d\n", err);
}

static struct img_ascii_lcd_config sead3_config = {
	.num_chars = 16,
	.external_regmap = true,
	.update = sead3_update,
};

static const struct of_device_id img_ascii_lcd_matches[] = {
	{ .compatible = "img,boston-lcd", .data = &boston_config },
	{ .compatible = "mti,malta-lcd", .data = &malta_config },
	{ .compatible = "mti,sead3-lcd", .data = &sead3_config },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, img_ascii_lcd_matches);

/**
 * img_ascii_lcd_probe() - probe an LCD display device
 * @pdev: the LCD platform device
 *
 * Probe an LCD display device, ensuring that we have the required resources in
 * order to access the LCD & setting up private data as well as sysfs files.
 *
 * Return: 0 on success, else -ERRNO
 */
static int img_ascii_lcd_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct img_ascii_lcd_config *cfg;
	struct device *dev = &pdev->dev;
	struct img_ascii_lcd_ctx *ctx;
	int err;

	match = of_match_device(img_ascii_lcd_matches, dev);
	if (!match)
		return -ENODEV;

	cfg = match->data;
	ctx = devm_kzalloc(dev, sizeof(*ctx) + cfg->num_chars, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (cfg->external_regmap) {
		ctx->regmap = syscon_node_to_regmap(dev->parent->of_node);
		if (IS_ERR(ctx->regmap))
			return PTR_ERR(ctx->regmap);

		if (of_property_read_u32(dev->of_node, "offset", &ctx->offset))
			return -EINVAL;
	} else {
		ctx->base = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(ctx->base))
			return PTR_ERR(ctx->base);
	}

	err = linedisp_register(&ctx->linedisp, dev, cfg->num_chars, ctx->curr,
				cfg->update);
	if (err)
		return err;

	/* for backwards compatibility */
	err = compat_only_sysfs_link_entry_to_kobj(&dev->kobj,
						   &ctx->linedisp.dev.kobj,
						   "message", NULL);
	if (err)
		goto err_unregister;

	platform_set_drvdata(pdev, ctx);
	return 0;

err_unregister:
	linedisp_unregister(&ctx->linedisp);
	return err;
}

/**
 * img_ascii_lcd_remove() - remove an LCD display device
 * @pdev: the LCD platform device
 *
 * Remove an LCD display device, freeing private resources & ensuring that the
 * driver stops using the LCD display registers.
 *
 * Return: 0
 */
static int img_ascii_lcd_remove(struct platform_device *pdev)
{
	struct img_ascii_lcd_ctx *ctx = platform_get_drvdata(pdev);

	sysfs_remove_link(&pdev->dev.kobj, "message");
	linedisp_unregister(&ctx->linedisp);
	return 0;
}

static struct platform_driver img_ascii_lcd_driver = {
	.driver = {
		.name		= "img-ascii-lcd",
		.of_match_table	= img_ascii_lcd_matches,
	},
	.probe	= img_ascii_lcd_probe,
	.remove	= img_ascii_lcd_remove,
};
module_platform_driver(img_ascii_lcd_driver);

MODULE_DESCRIPTION("Imagination Technologies ASCII LCD Display");
MODULE_AUTHOR("Paul Burton <paul.burton@mips.com>");
MODULE_LICENSE("GPL");
