// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2014 Endless Mobile
 */

#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/soc/amlogic/meson-canvas.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/io.h>

#define NUM_CANVAS 256

/* DMC Registers */
#define DMC_CAV_LUT_DATAL	0x00
	#define CANVAS_WIDTH_LBIT	29
	#define CANVAS_WIDTH_LWID	3
#define DMC_CAV_LUT_DATAH	0x04
	#define CANVAS_WIDTH_HBIT	0
	#define CANVAS_HEIGHT_BIT	9
	#define CANVAS_WRAP_BIT		22
	#define CANVAS_BLKMODE_BIT	24
	#define CANVAS_ENDIAN_BIT	26
#define DMC_CAV_LUT_ADDR	0x08
	#define CANVAS_LUT_WR_EN	BIT(9)
	#define CANVAS_LUT_RD_EN	BIT(8)

struct meson_canvas {
	struct device *dev;
	void __iomem *reg_base;
	spinlock_t lock; /* canvas device lock */
	u8 used[NUM_CANVAS];
	bool supports_endianness;
};

static void canvas_write(struct meson_canvas *canvas, u32 reg, u32 val)
{
	writel_relaxed(val, canvas->reg_base + reg);
}

static u32 canvas_read(struct meson_canvas *canvas, u32 reg)
{
	return readl_relaxed(canvas->reg_base + reg);
}

struct meson_canvas *meson_canvas_get(struct device *dev)
{
	struct device_node *canvas_node;
	struct platform_device *canvas_pdev;
	struct meson_canvas *canvas;

	canvas_node = of_parse_phandle(dev->of_node, "amlogic,canvas", 0);
	if (!canvas_node)
		return ERR_PTR(-ENODEV);

	canvas_pdev = of_find_device_by_node(canvas_node);
	if (!canvas_pdev) {
		of_node_put(canvas_node);
		return ERR_PTR(-EPROBE_DEFER);
	}

	of_node_put(canvas_node);

	/*
	 * If priv is NULL, it's probably because the canvas hasn't
	 * properly initialized. Bail out with -EINVAL because, in the
	 * current state, this driver probe cannot return -EPROBE_DEFER
	 */
	canvas = dev_get_drvdata(&canvas_pdev->dev);
	if (!canvas) {
		put_device(&canvas_pdev->dev);
		return ERR_PTR(-EINVAL);
	}

	return canvas;
}
EXPORT_SYMBOL_GPL(meson_canvas_get);

int meson_canvas_config(struct meson_canvas *canvas, u8 canvas_index,
			u32 addr, u32 stride, u32 height,
			unsigned int wrap,
			unsigned int blkmode,
			unsigned int endian)
{
	unsigned long flags;

	if (endian && !canvas->supports_endianness) {
		dev_err(canvas->dev,
			"Endianness is not supported on this SoC\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&canvas->lock, flags);
	if (!canvas->used[canvas_index]) {
		dev_err(canvas->dev,
			"Trying to setup non allocated canvas %u\n",
			canvas_index);
		spin_unlock_irqrestore(&canvas->lock, flags);
		return -EINVAL;
	}

	canvas_write(canvas, DMC_CAV_LUT_DATAL,
		     ((addr + 7) >> 3) |
		     (((stride + 7) >> 3) << CANVAS_WIDTH_LBIT));

	canvas_write(canvas, DMC_CAV_LUT_DATAH,
		     ((((stride + 7) >> 3) >> CANVAS_WIDTH_LWID) <<
						CANVAS_WIDTH_HBIT) |
		     (height << CANVAS_HEIGHT_BIT) |
		     (wrap << CANVAS_WRAP_BIT) |
		     (blkmode << CANVAS_BLKMODE_BIT) |
		     (endian << CANVAS_ENDIAN_BIT));

	canvas_write(canvas, DMC_CAV_LUT_ADDR,
		     CANVAS_LUT_WR_EN | canvas_index);

	/* Force a read-back to make sure everything is flushed. */
	canvas_read(canvas, DMC_CAV_LUT_DATAH);
	spin_unlock_irqrestore(&canvas->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(meson_canvas_config);

int meson_canvas_alloc(struct meson_canvas *canvas, u8 *canvas_index)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&canvas->lock, flags);
	for (i = 0; i < NUM_CANVAS; ++i) {
		if (!canvas->used[i]) {
			canvas->used[i] = 1;
			spin_unlock_irqrestore(&canvas->lock, flags);
			*canvas_index = i;
			return 0;
		}
	}
	spin_unlock_irqrestore(&canvas->lock, flags);

	dev_err(canvas->dev, "No more canvas available\n");
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(meson_canvas_alloc);

int meson_canvas_free(struct meson_canvas *canvas, u8 canvas_index)
{
	unsigned long flags;

	spin_lock_irqsave(&canvas->lock, flags);
	if (!canvas->used[canvas_index]) {
		dev_err(canvas->dev,
			"Trying to free unused canvas %u\n", canvas_index);
		spin_unlock_irqrestore(&canvas->lock, flags);
		return -EINVAL;
	}
	canvas->used[canvas_index] = 0;
	spin_unlock_irqrestore(&canvas->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(meson_canvas_free);

static int meson_canvas_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct meson_canvas *canvas;
	struct device *dev = &pdev->dev;

	canvas = devm_kzalloc(dev, sizeof(*canvas), GFP_KERNEL);
	if (!canvas)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	canvas->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(canvas->reg_base))
		return PTR_ERR(canvas->reg_base);

	canvas->supports_endianness = of_device_get_match_data(dev);

	canvas->dev = dev;
	spin_lock_init(&canvas->lock);
	dev_set_drvdata(dev, canvas);

	return 0;
}

static const struct of_device_id canvas_dt_match[] = {
	{ .compatible = "amlogic,meson8-canvas", .data = (void *)false, },
	{ .compatible = "amlogic,meson8b-canvas", .data = (void *)false, },
	{ .compatible = "amlogic,meson8m2-canvas", .data = (void *)false, },
	{ .compatible = "amlogic,canvas", .data = (void *)true, },
	{}
};
MODULE_DEVICE_TABLE(of, canvas_dt_match);

static struct platform_driver meson_canvas_driver = {
	.probe = meson_canvas_probe,
	.driver = {
		.name = "amlogic-canvas",
		.of_match_table = canvas_dt_match,
	},
};
module_platform_driver(meson_canvas_driver);

MODULE_DESCRIPTION("Amlogic Canvas driver");
MODULE_AUTHOR("Maxime Jourdan <mjourdan@baylibre.com>");
MODULE_LICENSE("GPL");
