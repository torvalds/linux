// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for EXAR XRA1403 16-bit GPIO expander
 *
 * Copyright (c) 2017, General Electric Company
 */

#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/seq_file.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>

/* XRA1403 registers */
#define XRA_GSR   0x00 /* GPIO State */
#define XRA_OCR   0x02 /* Output Control */
#define XRA_PIR   0x04 /* Input Polarity Inversion */
#define XRA_GCR   0x06 /* GPIO Configuration */
#define XRA_PUR   0x08 /* Input Internal Pull-up Resistor Enable/Disable */
#define XRA_IER   0x0A /* Input Interrupt Enable */
#define XRA_TSCR  0x0C /* Output Three-State Control */
#define XRA_ISR   0x0E /* Input Interrupt Status */
#define XRA_REIR  0x10 /* Input Rising Edge Interrupt Enable */
#define XRA_FEIR  0x12 /* Input Falling Edge Interrupt Enable */
#define XRA_IFR   0x14 /* Input Filter Enable/Disable */
#define XRA_LAST  0x15 /* Bounds */

struct xra1403 {
	struct gpio_chip  chip;
	struct regmap     *regmap;
};

static const struct regmap_config xra1403_regmap_cfg = {
		.reg_bits = 7,
		.pad_bits = 1,
		.val_bits = 8,

		.max_register = XRA_LAST,
};

static unsigned int to_reg(unsigned int reg, unsigned int offset)
{
	return reg + (offset > 7);
}

static int xra1403_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct xra1403 *xra = gpiochip_get_data(chip);

	return regmap_update_bits(xra->regmap, to_reg(XRA_GCR, offset),
			BIT(offset % 8), BIT(offset % 8));
}

static int xra1403_direction_output(struct gpio_chip *chip, unsigned int offset,
				    int value)
{
	int ret;
	struct xra1403 *xra = gpiochip_get_data(chip);

	ret = regmap_update_bits(xra->regmap, to_reg(XRA_GCR, offset),
			BIT(offset % 8), 0);
	if (ret)
		return ret;

	ret = regmap_update_bits(xra->regmap, to_reg(XRA_OCR, offset),
			BIT(offset % 8), value ? BIT(offset % 8) : 0);

	return ret;
}

static int xra1403_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	int ret;
	unsigned int val;
	struct xra1403 *xra = gpiochip_get_data(chip);

	ret = regmap_read(xra->regmap, to_reg(XRA_GCR, offset), &val);
	if (ret)
		return ret;

	if (val & BIT(offset % 8))
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int xra1403_get(struct gpio_chip *chip, unsigned int offset)
{
	int ret;
	unsigned int val;
	struct xra1403 *xra = gpiochip_get_data(chip);

	ret = regmap_read(xra->regmap, to_reg(XRA_GSR, offset), &val);
	if (ret)
		return ret;

	return !!(val & BIT(offset % 8));
}

static void xra1403_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	int ret;
	struct xra1403 *xra = gpiochip_get_data(chip);

	ret = regmap_update_bits(xra->regmap, to_reg(XRA_OCR, offset),
			BIT(offset % 8), value ? BIT(offset % 8) : 0);
	if (ret)
		dev_err(chip->parent, "Failed to set pin: %d, ret: %d\n",
				offset, ret);
}

#ifdef CONFIG_DEBUG_FS
static void xra1403_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	int reg;
	struct xra1403 *xra = gpiochip_get_data(chip);
	int value[XRA_LAST];
	int i;
	const char *label;
	unsigned int gcr;
	unsigned int gsr;

	seq_puts(s, "xra reg:");
	for (reg = 0; reg <= XRA_LAST; reg++)
		seq_printf(s, " %2.2x", reg);
	seq_puts(s, "\n  value:");
	for (reg = 0; reg < XRA_LAST; reg++) {
		regmap_read(xra->regmap, reg, &value[reg]);
		seq_printf(s, " %2.2x", value[reg]);
	}
	seq_puts(s, "\n");

	gcr = value[XRA_GCR + 1] << 8 | value[XRA_GCR];
	gsr = value[XRA_GSR + 1] << 8 | value[XRA_GSR];
	for_each_requested_gpio(chip, i, label) {
		seq_printf(s, " gpio-%-3d (%-12s) %s %s\n",
			   chip->base + i, label,
			   (gcr & BIT(i)) ? "in" : "out",
			   (gsr & BIT(i)) ? "hi" : "lo");
	}
}
#else
#define xra1403_dbg_show NULL
#endif

static int xra1403_probe(struct spi_device *spi)
{
	struct xra1403 *xra;
	struct gpio_desc *reset_gpio;
	int ret;

	xra = devm_kzalloc(&spi->dev, sizeof(*xra), GFP_KERNEL);
	if (!xra)
		return -ENOMEM;

	/* bring the chip out of reset if reset pin is provided*/
	reset_gpio = devm_gpiod_get_optional(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(reset_gpio))
		dev_warn(&spi->dev, "Could not get reset-gpios\n");

	xra->chip.direction_input = xra1403_direction_input;
	xra->chip.direction_output = xra1403_direction_output;
	xra->chip.get_direction = xra1403_get_direction;
	xra->chip.get = xra1403_get;
	xra->chip.set = xra1403_set;

	xra->chip.dbg_show = xra1403_dbg_show;

	xra->chip.ngpio = 16;
	xra->chip.label = "xra1403";

	xra->chip.base = -1;
	xra->chip.can_sleep = true;
	xra->chip.parent = &spi->dev;
	xra->chip.owner = THIS_MODULE;

	xra->regmap = devm_regmap_init_spi(spi, &xra1403_regmap_cfg);
	if (IS_ERR(xra->regmap)) {
		ret = PTR_ERR(xra->regmap);
		dev_err(&spi->dev, "Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	return devm_gpiochip_add_data(&spi->dev, &xra->chip, xra);
}

static const struct spi_device_id xra1403_ids[] = {
	{ "xra1403" },
	{},
};
MODULE_DEVICE_TABLE(spi, xra1403_ids);

static const struct of_device_id xra1403_spi_of_match[] = {
	{ .compatible = "exar,xra1403" },
	{},
};
MODULE_DEVICE_TABLE(of, xra1403_spi_of_match);

static struct spi_driver xra1403_driver = {
	.probe    = xra1403_probe,
	.id_table = xra1403_ids,
	.driver   = {
		.name           = "xra1403",
		.of_match_table = of_match_ptr(xra1403_spi_of_match),
	},
};

module_spi_driver(xra1403_driver);

MODULE_AUTHOR("Nandor Han <nandor.han@ge.com>");
MODULE_AUTHOR("Semi Malinen <semi.malinen@ge.com>");
MODULE_DESCRIPTION("GPIO expander driver for EXAR XRA1403");
MODULE_LICENSE("GPL v2");
