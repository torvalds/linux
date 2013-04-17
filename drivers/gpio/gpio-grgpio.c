/*
 * Driver for Aeroflex Gaisler GRGPIO General Purpose I/O cores.
 *
 * 2013 (c) Aeroflex Gaisler AB
 *
 * This driver supports the GRGPIO GPIO core available in the GRLIB VHDL
 * IP core library.
 *
 * Full documentation of the GRGPIO core can be found here:
 * http://www.gaisler.com/products/grlib/grip.pdf
 *
 * See "Documentation/devicetree/bindings/gpio/gpio-grgpio.txt" for
 * information on open firmware properties.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * Contributors: Andreas Larsson <andreas@gaisler.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/basic_mmio_gpio.h>

#define GRGPIO_MAX_NGPIO 32

#define GRGPIO_DATA		0x00
#define GRGPIO_OUTPUT		0x04
#define GRGPIO_DIR		0x08
#define GRGPIO_IMASK		0x0c
#define GRGPIO_IPOL		0x10
#define GRGPIO_IEDGE		0x14
#define GRGPIO_BYPASS		0x18
#define GRGPIO_IMAP_BASE	0x20

struct grgpio_priv {
	struct bgpio_chip bgc;
	void __iomem *regs;
	struct device *dev;
};

static inline struct grgpio_priv *grgpio_gc_to_priv(struct gpio_chip *gc)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);

	return container_of(bgc, struct grgpio_priv, bgc);
}

static int grgpio_probe(struct platform_device *ofdev)
{
	struct device_node *np = ofdev->dev.of_node;
	void  __iomem *regs;
	struct gpio_chip *gc;
	struct bgpio_chip *bgc;
	struct grgpio_priv *priv;
	struct resource *res;
	int err;
	u32 prop;

	priv = devm_kzalloc(&ofdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(ofdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&ofdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	bgc = &priv->bgc;
	err = bgpio_init(bgc, &ofdev->dev, 4, regs + GRGPIO_DATA,
			 regs + GRGPIO_OUTPUT, NULL, regs + GRGPIO_DIR, NULL,
			 BGPIOF_BIG_ENDIAN_BYTE_ORDER);
	if (err) {
		dev_err(&ofdev->dev, "bgpio_init() failed\n");
		return err;
	}

	priv->regs = regs;
	priv->dev = &ofdev->dev;

	gc = &bgc->gc;
	gc->of_node = np;
	gc->owner = THIS_MODULE;
	gc->label = np->full_name;
	gc->base = -1;

	err = of_property_read_u32(np, "nbits", &prop);
	if (err || prop <= 0 || prop > GRGPIO_MAX_NGPIO) {
		gc->ngpio = GRGPIO_MAX_NGPIO;
		dev_dbg(&ofdev->dev,
			"No or invalid nbits property: assume %d\n", gc->ngpio);
	} else {
		gc->ngpio = prop;
	}

	platform_set_drvdata(ofdev, priv);

	err = gpiochip_add(gc);
	if (err) {
		dev_err(&ofdev->dev, "Could not add gpiochip\n");
		return err;
	}

	dev_info(&ofdev->dev, "regs=0x%p, base=%d, ngpio=%d\n",
		 priv->regs, gc->base, gc->ngpio);

	return 0;
}

static int grgpio_remove(struct platform_device *ofdev)
{
	struct grgpio_priv *priv = platform_get_drvdata(ofdev);

	return gpiochip_remove(&priv->bgc.gc);
}

static struct of_device_id grgpio_match[] = {
	{.name = "GAISLER_GPIO"},
	{.name = "01_01a"},
	{},
};

MODULE_DEVICE_TABLE(of, grgpio_match);

static struct platform_driver grgpio_driver = {
	.driver = {
		.name = "grgpio",
		.owner = THIS_MODULE,
		.of_match_table = grgpio_match,
	},
	.probe = grgpio_probe,
	.remove = grgpio_remove,
};
module_platform_driver(grgpio_driver);

MODULE_AUTHOR("Aeroflex Gaisler AB.");
MODULE_DESCRIPTION("Driver for Aeroflex Gaisler GRGPIO");
MODULE_LICENSE("GPL");
