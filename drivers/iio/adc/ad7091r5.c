// SPDX-License-Identifier: GPL-2.0
/*
 * AD7091R5 Analog to Digital converter driver
 *
 * Copyright 2014-2019 Analog Devices Inc.
 */

#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "ad7091r-base.h"

static const struct iio_event_spec ad7091r5_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_HYSTERESIS),
	},
};

#define AD7091R_CHANNEL(idx, bits, ev, num_ev) { \
	.type = IIO_VOLTAGE, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.indexed = 1, \
	.channel = idx, \
	.event_spec = ev, \
	.num_event_specs = num_ev, \
	.scan_type.storagebits = 16, \
	.scan_type.realbits = bits, \
}
static const struct iio_chan_spec ad7091r5_channels_irq[] = {
	AD7091R_CHANNEL(0, 12, ad7091r5_events, ARRAY_SIZE(ad7091r5_events)),
	AD7091R_CHANNEL(1, 12, ad7091r5_events, ARRAY_SIZE(ad7091r5_events)),
	AD7091R_CHANNEL(2, 12, ad7091r5_events, ARRAY_SIZE(ad7091r5_events)),
	AD7091R_CHANNEL(3, 12, ad7091r5_events, ARRAY_SIZE(ad7091r5_events)),
};

static const struct iio_chan_spec ad7091r5_channels_noirq[] = {
	AD7091R_CHANNEL(0, 12, NULL, 0),
	AD7091R_CHANNEL(1, 12, NULL, 0),
	AD7091R_CHANNEL(2, 12, NULL, 0),
	AD7091R_CHANNEL(3, 12, NULL, 0),
};

static const struct ad7091r_chip_info ad7091r5_chip_info_irq = {
	.channels = ad7091r5_channels_irq,
	.num_channels = ARRAY_SIZE(ad7091r5_channels_irq),
	.vref_mV = 2500,
};

static const struct ad7091r_chip_info ad7091r5_chip_info_noirq = {
	.channels = ad7091r5_channels_noirq,
	.num_channels = ARRAY_SIZE(ad7091r5_channels_noirq),
	.vref_mV = 2500,
};

static int ad7091r5_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	const struct ad7091r_chip_info *chip_info;
	struct regmap *map = devm_regmap_init_i2c(i2c, &ad7091r_regmap_config);

	if (IS_ERR(map))
		return PTR_ERR(map);

	if (i2c->irq)
		chip_info = &ad7091r5_chip_info_irq;
	else
		chip_info = &ad7091r5_chip_info_noirq;

	return ad7091r_probe(&i2c->dev, id->name, chip_info, map, i2c->irq);
}

static const struct of_device_id ad7091r5_dt_ids[] = {
	{ .compatible = "adi,ad7091r5" },
	{},
};
MODULE_DEVICE_TABLE(of, ad7091r5_dt_ids);

static const struct i2c_device_id ad7091r5_i2c_ids[] = {
	{"ad7091r5", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, ad7091r5_i2c_ids);

static struct i2c_driver ad7091r5_driver = {
	.driver = {
		.name = "ad7091r5",
		.of_match_table = ad7091r5_dt_ids,
	},
	.probe = ad7091r5_i2c_probe,
	.id_table = ad7091r5_i2c_ids,
};
module_i2c_driver(ad7091r5_driver);

MODULE_AUTHOR("Beniamin Bia <beniamin.bia@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7091R5 multi-channel ADC driver");
MODULE_LICENSE("GPL v2");
