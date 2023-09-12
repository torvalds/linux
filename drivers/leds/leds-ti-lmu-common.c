// SPDX-License-Identifier: GPL-2.0
// Copyright 2015 Texas Instruments
// Copyright 2018 Sebastian Reichel
// Copyright 2018 Pavel Machek <pavel@ucw.cz>
// TI LMU LED common framework, based on previous work from
// Milo Kim <milo.kim@ti.com>

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/property.h>

#include <linux/leds-ti-lmu-common.h>

static const unsigned int ramp_table[16] = {2048, 262000, 524000, 1049000,
				2090000, 4194000, 8389000, 16780000, 33550000,
				41940000, 50330000, 58720000, 67110000,
				83880000, 100660000, 117440000};

static int ti_lmu_common_update_brightness(struct ti_lmu_bank *lmu_bank,
					   int brightness)
{
	struct regmap *regmap = lmu_bank->regmap;
	u8 reg, val;
	int ret;

	/*
	 * Brightness register update
	 *
	 * 11 bit dimming: update LSB bits and write MSB byte.
	 *		   MSB brightness should be shifted.
	 *  8 bit dimming: write MSB byte.
	 */
	if (lmu_bank->max_brightness == MAX_BRIGHTNESS_11BIT) {
		reg = lmu_bank->lsb_brightness_reg;
		ret = regmap_update_bits(regmap, reg,
					 LMU_11BIT_LSB_MASK,
					 brightness);
		if (ret)
			return ret;

		val = brightness >> LMU_11BIT_MSB_SHIFT;
	} else {
		val = brightness;
	}

	reg = lmu_bank->msb_brightness_reg;

	return regmap_write(regmap, reg, val);
}

int ti_lmu_common_set_brightness(struct ti_lmu_bank *lmu_bank, int brightness)
{
	return ti_lmu_common_update_brightness(lmu_bank, brightness);
}
EXPORT_SYMBOL(ti_lmu_common_set_brightness);

static unsigned int ti_lmu_common_convert_ramp_to_index(unsigned int usec)
{
	int size = ARRAY_SIZE(ramp_table);
	int i;

	if (usec <= ramp_table[0])
		return 0;

	if (usec > ramp_table[size - 1])
		return size - 1;

	for (i = 1; i < size; i++) {
		if (usec == ramp_table[i])
			return i;

		/* Find an approximate index by looking up the table */
		if (usec > ramp_table[i - 1] && usec < ramp_table[i]) {
			if (usec - ramp_table[i - 1] < ramp_table[i] - usec)
				return i - 1;
			else
				return i;
		}
	}

	return 0;
}

int ti_lmu_common_set_ramp(struct ti_lmu_bank *lmu_bank)
{
	struct regmap *regmap = lmu_bank->regmap;
	u8 ramp, ramp_up, ramp_down;

	if (lmu_bank->ramp_up_usec == 0 && lmu_bank->ramp_down_usec == 0) {
		ramp_up = 0;
		ramp_down = 0;
	} else {
		ramp_up = ti_lmu_common_convert_ramp_to_index(lmu_bank->ramp_up_usec);
		ramp_down = ti_lmu_common_convert_ramp_to_index(lmu_bank->ramp_down_usec);
	}

	ramp = (ramp_up << 4) | ramp_down;

	return regmap_write(regmap, lmu_bank->runtime_ramp_reg, ramp);

}
EXPORT_SYMBOL(ti_lmu_common_set_ramp);

int ti_lmu_common_get_ramp_params(struct device *dev,
				  struct fwnode_handle *child,
				  struct ti_lmu_bank *lmu_data)
{
	int ret;

	ret = fwnode_property_read_u32(child, "ramp-up-us",
				 &lmu_data->ramp_up_usec);
	if (ret)
		dev_warn(dev, "ramp-up-us property missing\n");


	ret = fwnode_property_read_u32(child, "ramp-down-us",
				 &lmu_data->ramp_down_usec);
	if (ret)
		dev_warn(dev, "ramp-down-us property missing\n");

	return 0;
}
EXPORT_SYMBOL(ti_lmu_common_get_ramp_params);

int ti_lmu_common_get_brt_res(struct device *dev, struct fwnode_handle *child,
				  struct ti_lmu_bank *lmu_data)
{
	int ret;

	ret = device_property_read_u32(dev, "ti,brightness-resolution",
				       &lmu_data->max_brightness);
	if (ret)
		ret = fwnode_property_read_u32(child,
					       "ti,brightness-resolution",
					       &lmu_data->max_brightness);
	if (lmu_data->max_brightness <= 0) {
		lmu_data->max_brightness = MAX_BRIGHTNESS_8BIT;
		return ret;
	}

	if (lmu_data->max_brightness > MAX_BRIGHTNESS_11BIT)
			lmu_data->max_brightness = MAX_BRIGHTNESS_11BIT;


	return 0;
}
EXPORT_SYMBOL(ti_lmu_common_get_brt_res);

MODULE_DESCRIPTION("TI LMU common LED framework");
MODULE_AUTHOR("Sebastian Reichel");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("ti-lmu-led-common");
