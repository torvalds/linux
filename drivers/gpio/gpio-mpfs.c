// SPDX-License-Identifier: (GPL-2.0)
/*
 * Microchip PolarFire SoC (MPFS) GPIO controller driver
 *
 * Copyright (c) 2018-2024 Microchip Technology Inc. and its subsidiaries
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

#define MPFS_GPIO_CTRL(i)		(0x4 * (i))
#define MPFS_MAX_NUM_GPIO		32
#define MPFS_GPIO_EN_INT		3
#define MPFS_GPIO_EN_OUT_BUF		BIT(2)
#define MPFS_GPIO_EN_IN			BIT(1)
#define MPFS_GPIO_EN_OUT		BIT(0)
#define MPFS_GPIO_DIR_MASK		GENMASK(2, 0)

#define MPFS_GPIO_TYPE_INT_EDGE_BOTH	0x80
#define MPFS_GPIO_TYPE_INT_EDGE_NEG	0x60
#define MPFS_GPIO_TYPE_INT_EDGE_POS	0x40
#define MPFS_GPIO_TYPE_INT_LEVEL_LOW	0x20
#define MPFS_GPIO_TYPE_INT_LEVEL_HIGH	0x00
#define MPFS_GPIO_TYPE_INT_MASK		GENMASK(7, 5)
#define MPFS_IRQ_REG			0x80

#define MPFS_INP_REG			0x84
#define COREGPIO_INP_REG		0x90
#define MPFS_OUTP_REG			0x88
#define COREGPIO_OUTP_REG		0xA0

struct mpfs_gpio_reg_offsets {
	u8 inp;
	u8 outp;
};

struct mpfs_gpio_chip {
	struct regmap *regs;
	const struct mpfs_gpio_reg_offsets *offsets;
	struct gpio_chip gc;
};

static const struct regmap_config mpfs_gpio_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
};

static int mpfs_gpio_direction_input(struct gpio_chip *gc, unsigned int gpio_index)
{
	struct mpfs_gpio_chip *mpfs_gpio = gpiochip_get_data(gc);

	regmap_update_bits(mpfs_gpio->regs, MPFS_GPIO_CTRL(gpio_index),
			   MPFS_GPIO_DIR_MASK, MPFS_GPIO_EN_IN);

	return 0;
}

static int mpfs_gpio_direction_output(struct gpio_chip *gc, unsigned int gpio_index, int value)
{
	struct mpfs_gpio_chip *mpfs_gpio = gpiochip_get_data(gc);

	regmap_update_bits(mpfs_gpio->regs, MPFS_GPIO_CTRL(gpio_index),
			   MPFS_GPIO_DIR_MASK, MPFS_GPIO_EN_OUT | MPFS_GPIO_EN_OUT_BUF);
	regmap_update_bits(mpfs_gpio->regs, mpfs_gpio->offsets->outp, BIT(gpio_index),
			   value << gpio_index);

	return 0;
}

static int mpfs_gpio_get_direction(struct gpio_chip *gc,
				   unsigned int gpio_index)
{
	struct mpfs_gpio_chip *mpfs_gpio = gpiochip_get_data(gc);
	unsigned int gpio_cfg;

	regmap_read(mpfs_gpio->regs, MPFS_GPIO_CTRL(gpio_index), &gpio_cfg);
	if (gpio_cfg & MPFS_GPIO_EN_IN)
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int mpfs_gpio_get(struct gpio_chip *gc, unsigned int gpio_index)
{
	struct mpfs_gpio_chip *mpfs_gpio = gpiochip_get_data(gc);

	if (mpfs_gpio_get_direction(gc, gpio_index) == GPIO_LINE_DIRECTION_OUT)
		return regmap_test_bits(mpfs_gpio->regs, mpfs_gpio->offsets->outp, BIT(gpio_index));
	else
		return regmap_test_bits(mpfs_gpio->regs, mpfs_gpio->offsets->inp, BIT(gpio_index));
}

static int mpfs_gpio_set(struct gpio_chip *gc, unsigned int gpio_index, int value)
{
	struct mpfs_gpio_chip *mpfs_gpio = gpiochip_get_data(gc);
	int ret;

	mpfs_gpio_get(gc, gpio_index);

	ret = regmap_update_bits(mpfs_gpio->regs, mpfs_gpio->offsets->outp,
				 BIT(gpio_index), value << gpio_index);

	mpfs_gpio_get(gc, gpio_index);

	return ret;
}

static int mpfs_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpfs_gpio_chip *mpfs_gpio;
	struct clk *clk;
	void __iomem *base;
	int ngpios;

	mpfs_gpio = devm_kzalloc(dev, sizeof(*mpfs_gpio), GFP_KERNEL);
	if (!mpfs_gpio)
		return -ENOMEM;

	mpfs_gpio->offsets = device_get_match_data(&pdev->dev);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base), "failed to ioremap memory resource\n");

	mpfs_gpio->regs = devm_regmap_init_mmio(dev, base, &mpfs_gpio_regmap_config);
	if (IS_ERR(mpfs_gpio->regs))
		return dev_err_probe(dev, PTR_ERR(mpfs_gpio->regs),
				     "failed to initialise regmap\n");

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "failed to get and enable clock\n");

	ngpios = MPFS_MAX_NUM_GPIO;
	device_property_read_u32(dev, "ngpios", &ngpios);
	if (ngpios > MPFS_MAX_NUM_GPIO)
		ngpios = MPFS_MAX_NUM_GPIO;

	mpfs_gpio->gc.direction_input = mpfs_gpio_direction_input;
	mpfs_gpio->gc.direction_output = mpfs_gpio_direction_output;
	mpfs_gpio->gc.get_direction = mpfs_gpio_get_direction;
	mpfs_gpio->gc.get = mpfs_gpio_get;
	mpfs_gpio->gc.set = mpfs_gpio_set;
	mpfs_gpio->gc.base = -1;
	mpfs_gpio->gc.ngpio = ngpios;
	mpfs_gpio->gc.label = dev_name(dev);
	mpfs_gpio->gc.parent = dev;
	mpfs_gpio->gc.owner = THIS_MODULE;

	return devm_gpiochip_add_data(dev, &mpfs_gpio->gc, mpfs_gpio);
}

static const struct mpfs_gpio_reg_offsets mpfs_reg_offsets = {
	.inp = MPFS_INP_REG,
	.outp = MPFS_OUTP_REG,
};

static const struct mpfs_gpio_reg_offsets coregpio_reg_offsets = {
	.inp = COREGPIO_INP_REG,
	.outp = COREGPIO_OUTP_REG,
};

static const struct of_device_id mpfs_gpio_of_ids[] = {
	{
		.compatible = "microchip,mpfs-gpio",
		.data = &mpfs_reg_offsets,
	}, {
		.compatible = "microchip,coregpio-rtl-v3",
		.data = &coregpio_reg_offsets,
	},
	{ /* end of list */ }
};

static struct platform_driver mpfs_gpio_driver = {
	.probe = mpfs_gpio_probe,
	.driver = {
		.name = "microchip,mpfs-gpio",
		.of_match_table = mpfs_gpio_of_ids,
	},
};
builtin_platform_driver(mpfs_gpio_driver);
