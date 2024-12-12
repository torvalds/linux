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

static const struct iio_chan_spec ad7091r5_channels_irq[] = {
	AD7091R_CHANNEL(0, 12, ad7091r_events, ARRAY_SIZE(ad7091r_events)),
	AD7091R_CHANNEL(1, 12, ad7091r_events, ARRAY_SIZE(ad7091r_events)),
	AD7091R_CHANNEL(2, 12, ad7091r_events, ARRAY_SIZE(ad7091r_events)),
	AD7091R_CHANNEL(3, 12, ad7091r_events, ARRAY_SIZE(ad7091r_events)),
};

static const struct iio_chan_spec ad7091r5_channels_noirq[] = {
	AD7091R_CHANNEL(0, 12, NULL, 0),
	AD7091R_CHANNEL(1, 12, NULL, 0),
	AD7091R_CHANNEL(2, 12, NULL, 0),
	AD7091R_CHANNEL(3, 12, NULL, 0),
};

static int ad7091r5_set_mode(struct ad7091r_state *st, enum ad7091r_mode mode)
{
	int ret, conf;

	switch (mode) {
	case AD7091R_MODE_SAMPLE:
		conf = 0;
		break;
	case AD7091R_MODE_COMMAND:
		conf = AD7091R_REG_CONF_CMD;
		break;
	case AD7091R_MODE_AUTOCYCLE:
		conf = AD7091R_REG_CONF_AUTO;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(st->map, AD7091R_REG_CONF,
				 AD7091R_REG_CONF_MODE_MASK, conf);
	if (ret)
		return ret;

	st->mode = mode;

	return 0;
}

static unsigned int ad7091r5_reg_result_chan_id(unsigned int val)
{
	return AD7091R5_REG_RESULT_CH_ID(val);
}

static const struct ad7091r_chip_info ad7091r5_chip_info_irq = {
	.name = "ad7091r-5",
	.channels = ad7091r5_channels_irq,
	.num_channels = ARRAY_SIZE(ad7091r5_channels_irq),
	.vref_mV = 2500,
	.reg_result_chan_id = &ad7091r5_reg_result_chan_id,
	.set_mode = &ad7091r5_set_mode,
};

static const struct ad7091r_chip_info ad7091r5_chip_info_noirq = {
	.name = "ad7091r-5",
	.channels = ad7091r5_channels_noirq,
	.num_channels = ARRAY_SIZE(ad7091r5_channels_noirq),
	.vref_mV = 2500,
	.reg_result_chan_id = &ad7091r5_reg_result_chan_id,
	.set_mode = &ad7091r5_set_mode,
};

static const struct regmap_config ad7091r_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.writeable_reg = ad7091r_writeable_reg,
	.volatile_reg = ad7091r_volatile_reg,
};

static void ad7091r5_regmap_init(struct ad7091r_state *st,
				 const struct regmap_config *regmap_conf)
{
	struct i2c_client *i2c = container_of(st->dev, struct i2c_client, dev);

	st->map = devm_regmap_init_i2c(i2c, regmap_conf);
}

static struct ad7091r_init_info ad7091r5_init_info = {
	.info_irq = &ad7091r5_chip_info_irq,
	.info_no_irq = &ad7091r5_chip_info_noirq,
	.regmap_config = &ad7091r_regmap_config,
	.init_adc_regmap = &ad7091r5_regmap_init
};

static int ad7091r5_i2c_probe(struct i2c_client *i2c)
{
	const struct ad7091r_init_info *init_info;

	init_info = i2c_get_match_data(i2c);
	if (!init_info)
		return -EINVAL;

	return ad7091r_probe(&i2c->dev, init_info, i2c->irq);
}

static const struct of_device_id ad7091r5_dt_ids[] = {
	{ .compatible = "adi,ad7091r5", .data = &ad7091r5_init_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7091r5_dt_ids);

static const struct i2c_device_id ad7091r5_i2c_ids[] = {
	{ "ad7091r5", (kernel_ulong_t)&ad7091r5_init_info },
	{ }
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
MODULE_IMPORT_NS(IIO_AD7091R);
