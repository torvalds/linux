// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices LTC4283 GPIO driver
 *
 * Copyright 2025 Analog Devices Inc.
 */

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/regmap.h>

#define LTC4283_PINS_MAX			8
#define LTC4283_PGIOX_START_NR			4
#define LTC4283_INPUT_STATUS			0x02
#define LTC4283_PGIO_CONFIG			0x10
#define   LTC4283_PGIO_CFG_MASK(pin) \
	GENMASK(((pin) - LTC4283_PGIOX_START_NR) * 2 + 1, (((pin) - LTC4283_PGIOX_START_NR) * 2))
#define LTC4283_PGIO_CONFIG_2			0x11

#define LTC4283_ADIO_CONFIG			0x12
/* starts at bit 4 */
#define   LTC4283_ADIOX_CONFIG_MASK(pin)	BIT((pin) + 4)
#define LTC4283_PGIO_DIR_IN			3
#define LTC4283_PGIO_DIR_OUT			2

struct ltc4283_gpio {
	struct gpio_chip gpio_chip;
	struct regmap *regmap;
};

static int ltc4283_pgio_get_direction(const struct ltc4283_gpio *st, unsigned int off)
{
	unsigned int val;
	int ret;

	ret = regmap_read(st->regmap, LTC4283_PGIO_CONFIG, &val);
	if (ret)
		return ret;

	val = field_get(LTC4283_PGIO_CFG_MASK(off), val);
	if (val == LTC4283_PGIO_DIR_IN)
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int ltc4283_gpio_get_direction(struct gpio_chip *gc, unsigned int off)
{
	struct ltc4283_gpio *st = gpiochip_get_data(gc);
	unsigned int val;
	int ret;

	if (off >= LTC4283_PGIOX_START_NR)
		return ltc4283_pgio_get_direction(st, off);

	ret = regmap_read(st->regmap, LTC4283_ADIO_CONFIG, &val);
	if (ret)
		return ret;

	if (val & LTC4283_ADIOX_CONFIG_MASK(off))
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int ltc4283_gpio_direction_set(const struct ltc4283_gpio *st,
				      unsigned int off, bool input)
{
	if (off >= LTC4283_PGIOX_START_NR) {
		unsigned int val = LTC4283_PGIO_DIR_OUT;

		if (input)
			val = LTC4283_PGIO_DIR_IN;

		val = field_prep(LTC4283_PGIO_CFG_MASK(off), val);
		return regmap_update_bits(st->regmap, LTC4283_PGIO_CONFIG,
					  LTC4283_PGIO_CFG_MASK(off), val);
	}

	return regmap_update_bits(st->regmap, LTC4283_ADIO_CONFIG,
				  LTC4283_ADIOX_CONFIG_MASK(off),
				  field_prep(LTC4283_ADIOX_CONFIG_MASK(off), input));
}

static int __ltc4283_gpio_set_value(const struct ltc4283_gpio *st,
				    unsigned int off, int val)
{
	u32 reg = off < LTC4283_PGIOX_START_NR ? LTC4283_ADIO_CONFIG : LTC4283_PGIO_CONFIG_2;

	return regmap_update_bits(st->regmap, reg, BIT(off),
				  field_prep(BIT(off), !!val));
}

static int ltc4283_gpio_direction_input(struct gpio_chip *gc, unsigned int off)
{
	struct ltc4283_gpio *st = gpiochip_get_data(gc);

	return ltc4283_gpio_direction_set(st, off, true);
}

static int ltc4283_gpio_direction_output(struct gpio_chip *gc, unsigned int off, int val)
{
	struct ltc4283_gpio *st = gpiochip_get_data(gc);
	int ret;

	ret = ltc4283_gpio_direction_set(st, off, false);
	if (ret)
		return ret;

	return __ltc4283_gpio_set_value(st, off, val);
}

static int ltc4283_gpio_get_value(struct gpio_chip *gc, unsigned int off)
{
	struct ltc4283_gpio *st = gpiochip_get_data(gc);
	unsigned int val, reg;
	int ret, dir;

	dir = ltc4283_gpio_get_direction(gc, off);
	if (dir < 0)
		return dir;

	if (dir == GPIO_LINE_DIRECTION_IN) {
		ret = regmap_read(st->regmap, LTC4283_INPUT_STATUS, &val);
		if (ret)
			return ret;

		/* ADIO1 is at bit 3. */
		if (off < LTC4283_PGIOX_START_NR)
			return !!(val & BIT(3 - off));

		/* PGIO1 is at bit 7. */
		return !!(val & BIT(7 - (off - LTC4283_PGIOX_START_NR)));
	}

	if (off < LTC4283_PGIOX_START_NR)
		reg = LTC4283_ADIO_CONFIG;
	else
		reg = LTC4283_PGIO_CONFIG_2;

	ret = regmap_read(st->regmap, reg, &val);
	if (ret)
		return ret;

	return !!(val & BIT(off));
}

static int ltc4283_gpio_set_value(struct gpio_chip *gc, unsigned int off, int val)
{
	struct ltc4283_gpio *st = gpiochip_get_data(gc);

	return __ltc4283_gpio_set_value(st, off, val);
}

static int ltc4283_init_valid_mask(struct gpio_chip *gc, unsigned long *valid_mask,
				   unsigned int ngpios)
{
	unsigned long *mask = dev_get_platdata(gc->parent);

	bitmap_copy(valid_mask, mask, ngpios);
	return 0;
}

static int ltc4283_gpio_probe(struct auxiliary_device *adev,
			      const struct auxiliary_device_id *id)
{
	struct device *dev = &adev->dev;
	struct ltc4283_gpio *st;
	struct gpio_chip *gc;

	st = devm_kzalloc(dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->regmap = dev_get_regmap(dev->parent, NULL);
	if (!st->regmap)
		return dev_err_probe(dev, -ENODEV,
				     "Failed to get regmap\n");

	gc = &st->gpio_chip;
	gc->parent = dev;
	gc->get_direction = ltc4283_gpio_get_direction;
	gc->direction_input = ltc4283_gpio_direction_input;
	gc->direction_output = ltc4283_gpio_direction_output;
	gc->get = ltc4283_gpio_get_value;
	gc->set = ltc4283_gpio_set_value;
	gc->init_valid_mask = ltc4283_init_valid_mask;
	gc->can_sleep = true;

	gc->base = -1;
	gc->ngpio = LTC4283_PINS_MAX;
	gc->label = adev->name;
	gc->owner = THIS_MODULE;

	return devm_gpiochip_add_data(dev, &st->gpio_chip, st);
}

static const struct auxiliary_device_id ltc4283_aux_id_table[] = {
	{ "ltc4283.gpio" },
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, ltc4283_aux_id_table);

static struct auxiliary_driver ltc4283_gpio_driver = {
	.probe = ltc4283_gpio_probe,
	.id_table = ltc4283_aux_id_table,
};
module_auxiliary_driver(ltc4283_gpio_driver);

MODULE_AUTHOR("Nuno Sá <nuno.sa@analog.com>");
MODULE_DESCRIPTION("GPIO LTC4283 Driver");
MODULE_LICENSE("GPL");
