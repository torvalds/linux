// SPDX-License-Identifier: GPL-2.0-only
/*
 * Vishay VEML6075 UVA and UVB light sensor
 *
 * Copyright 2023 Javier Carrasco <javier.carrasco.cruz@gmail.com>
 *
 * 7-bit I2C slave, address 0x10
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/units.h>

#include <linux/iio/iio.h>

#define VEML6075_CMD_CONF	0x00 /* configuration register */
#define VEML6075_CMD_UVA	0x07 /* UVA channel */
#define VEML6075_CMD_UVB	0x09 /* UVB channel */
#define VEML6075_CMD_COMP1	0x0A /* visible light compensation */
#define VEML6075_CMD_COMP2	0x0B /* infrarred light compensation */
#define VEML6075_CMD_ID		0x0C /* device ID */

#define VEML6075_CONF_IT	GENMASK(6, 4) /* intregration time */
#define VEML6075_CONF_HD	BIT(3) /* dynamic setting */
#define VEML6075_CONF_TRIG	BIT(2) /* trigger */
#define VEML6075_CONF_AF	BIT(1) /* active force enable */
#define VEML6075_CONF_SD	BIT(0) /* shutdown */

#define VEML6075_IT_50_MS	0x00
#define VEML6075_IT_100_MS	0x01
#define VEML6075_IT_200_MS	0x02
#define VEML6075_IT_400_MS	0x03
#define VEML6075_IT_800_MS	0x04

#define VEML6075_AF_DISABLE	0x00
#define VEML6075_AF_ENABLE	0x01

#define VEML6075_SD_DISABLE	0x00
#define VEML6075_SD_ENABLE	0x01

/* Open-air coefficients and responsivity */
#define VEML6075_A_COEF		2220
#define VEML6075_B_COEF		1330
#define VEML6075_C_COEF		2950
#define VEML6075_D_COEF		1740
#define VEML6075_UVA_RESP	1461
#define VEML6075_UVB_RESP	2591

static const int veml6075_it_ms[] = { 50, 100, 200, 400, 800 };

struct veml6075_data {
	struct i2c_client *client;
	struct regmap *regmap;
	/*
	 * prevent integration time modification and triggering
	 * measurements while a measurement is underway.
	 */
	struct mutex lock;
};

/* channel number */
enum veml6075_chan {
	CH_UVA,
	CH_UVB,
};

static const struct iio_chan_spec veml6075_channels[] = {
	{
		.type = IIO_INTENSITY,
		.channel = CH_UVA,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_UVA,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME),
	},
	{
		.type = IIO_INTENSITY,
		.channel = CH_UVB,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_UVB,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME),
	},
	{
		.type = IIO_UVINDEX,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME),
	},
};

static int veml6075_request_measurement(struct veml6075_data *data)
{
	int ret, conf, int_time;

	ret = regmap_read(data->regmap, VEML6075_CMD_CONF, &conf);
	if (ret < 0)
		return ret;

	/* disable shutdown and trigger measurement */
	ret = regmap_write(data->regmap, VEML6075_CMD_CONF,
			   (conf | VEML6075_CONF_TRIG) & ~VEML6075_CONF_SD);
	if (ret < 0)
		return ret;

	/*
	 * A measurement requires between 1.30 and 1.40 times the integration
	 * time for all possible configurations. Using a 1.50 factor simplifies
	 * operations and ensures reliability under all circumstances.
	 */
	int_time = veml6075_it_ms[FIELD_GET(VEML6075_CONF_IT, conf)];
	msleep(int_time + (int_time / 2));

	/* shutdown again, data registers are still accessible */
	return regmap_update_bits(data->regmap, VEML6075_CMD_CONF,
				  VEML6075_CONF_SD, VEML6075_CONF_SD);
}

static int veml6075_uva_comp(int raw_uva, int comp1, int comp2)
{
	int comp1a_c, comp2a_c, uva_comp;

	comp1a_c = (comp1 * VEML6075_A_COEF) / 1000U;
	comp2a_c = (comp2 * VEML6075_B_COEF) / 1000U;
	uva_comp = raw_uva - comp1a_c - comp2a_c;

	return clamp_val(uva_comp, 0, U16_MAX);
}

static int veml6075_uvb_comp(int raw_uvb, int comp1, int comp2)
{
	int comp1b_c, comp2b_c, uvb_comp;

	comp1b_c = (comp1 * VEML6075_C_COEF) / 1000U;
	comp2b_c = (comp2 * VEML6075_D_COEF) / 1000U;
	uvb_comp = raw_uvb - comp1b_c - comp2b_c;

	return clamp_val(uvb_comp, 0, U16_MAX);
}

static int veml6075_read_comp(struct veml6075_data *data, int *c1, int *c2)
{
	int ret;

	ret = regmap_read(data->regmap, VEML6075_CMD_COMP1, c1);
	if (ret < 0)
		return ret;

	return regmap_read(data->regmap, VEML6075_CMD_COMP2, c2);
}

static int veml6075_read_uv_direct(struct veml6075_data *data, int chan,
				   int *val)
{
	int c1, c2, ret;

	guard(mutex)(&data->lock);

	ret = veml6075_request_measurement(data);
	if (ret < 0)
		return ret;

	ret = veml6075_read_comp(data, &c1, &c2);
	if (ret < 0)
		return ret;

	switch (chan) {
	case CH_UVA:
		ret = regmap_read(data->regmap, VEML6075_CMD_UVA, val);
		if (ret < 0)
			return ret;

		*val = veml6075_uva_comp(*val, c1, c2);
		return IIO_VAL_INT;
	case CH_UVB:
		ret = regmap_read(data->regmap, VEML6075_CMD_UVB, val);
		if (ret < 0)
			return ret;

		*val = veml6075_uvb_comp(*val, c1, c2);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int veml6075_read_int_time_index(struct veml6075_data *data)
{
	int ret, conf, int_index;

	ret = regmap_read(data->regmap, VEML6075_CMD_CONF, &conf);
	if (ret < 0)
		return ret;

	int_index = FIELD_GET(VEML6075_CONF_IT, conf);
	if (int_index >= ARRAY_SIZE(veml6075_it_ms))
		return -EINVAL;

	return int_index;
}

static int veml6075_read_int_time_ms(struct veml6075_data *data, int *val)
{
	int int_index;

	guard(mutex)(&data->lock);
	int_index = veml6075_read_int_time_index(data);
	if (int_index < 0)
		return int_index;

	*val = veml6075_it_ms[int_index];

	return IIO_VAL_INT;
}

static int veml6075_get_uvi_micro(struct veml6075_data *data, int uva_comp,
				  int uvb_comp)
{
	int uvia_micro = uva_comp * VEML6075_UVA_RESP;
	int uvib_micro = uvb_comp * VEML6075_UVB_RESP;
	int int_index;

	int_index = veml6075_read_int_time_index(data);
	if (int_index < 0)
		return int_index;

	switch (int_index) {
	case VEML6075_IT_50_MS:
		return uvia_micro + uvib_micro;
	case VEML6075_IT_100_MS:
	case VEML6075_IT_200_MS:
	case VEML6075_IT_400_MS:
	case VEML6075_IT_800_MS:
		return (uvia_micro + uvib_micro) / (2 << int_index);
	default:
		return -EINVAL;
	}
}

static int veml6075_read_uvi(struct veml6075_data *data, int *val, int *val2)
{
	int ret, c1, c2, uva, uvb, uvi_micro;

	guard(mutex)(&data->lock);

	ret = veml6075_request_measurement(data);
	if (ret < 0)
		return ret;

	ret = veml6075_read_comp(data, &c1, &c2);
	if (ret < 0)
		return ret;

	ret = regmap_read(data->regmap, VEML6075_CMD_UVA, &uva);
	if (ret < 0)
		return ret;

	ret = regmap_read(data->regmap, VEML6075_CMD_UVB, &uvb);
	if (ret < 0)
		return ret;

	uvi_micro = veml6075_get_uvi_micro(data, veml6075_uva_comp(uva, c1, c2),
					   veml6075_uvb_comp(uvb, c1, c2));
	if (uvi_micro < 0)
		return uvi_micro;

	*val = uvi_micro / MICRO;
	*val2 = uvi_micro % MICRO;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int veml6075_read_responsivity(int chan, int *val, int *val2)
{
	/* scale = 1 / resp */
	switch (chan) {
	case CH_UVA:
		/* resp = 0.93 c/uW/cm2: scale = 1.75268817 */
		*val = 1;
		*val2 = 75268817;
		return IIO_VAL_INT_PLUS_NANO;
	case CH_UVB:
		/* resp = 2.1 c/uW/cm2: scale = 0.476190476 */
		*val = 0;
		*val2 = 476190476;
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static int veml6075_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		*length = ARRAY_SIZE(veml6075_it_ms);
		*vals = veml6075_it_ms;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_LIST;

	default:
		return -EINVAL;
	}
}

static int veml6075_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct veml6075_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return veml6075_read_uv_direct(data, chan->channel, val);
	case IIO_CHAN_INFO_PROCESSED:
		return veml6075_read_uvi(data, val, val2);
	case IIO_CHAN_INFO_INT_TIME:
		return veml6075_read_int_time_ms(data, val);
	case IIO_CHAN_INFO_SCALE:
		return veml6075_read_responsivity(chan->channel, val, val2);
	default:
		return -EINVAL;
	}
}

static int veml6075_write_int_time_ms(struct veml6075_data *data, int val)
{
	int i = ARRAY_SIZE(veml6075_it_ms);

	guard(mutex)(&data->lock);

	while (i-- > 0) {
		if (val == veml6075_it_ms[i])
			break;
	}
	if (i < 0)
		return -EINVAL;

	return regmap_update_bits(data->regmap, VEML6075_CMD_CONF,
				  VEML6075_CONF_IT,
				  FIELD_PREP(VEML6075_CONF_IT, i));
}

static int veml6075_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct veml6075_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		return veml6075_write_int_time_ms(data, val);
	default:
		return -EINVAL;
	}
}

static const struct iio_info veml6075_info = {
	.read_avail = veml6075_read_avail,
	.read_raw = veml6075_read_raw,
	.write_raw = veml6075_write_raw,
};

static bool veml6075_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case VEML6075_CMD_CONF:
	case VEML6075_CMD_UVA:
	case VEML6075_CMD_UVB:
	case VEML6075_CMD_COMP1:
	case VEML6075_CMD_COMP2:
	case VEML6075_CMD_ID:
		return true;
	default:
		return false;
	}
}

static bool veml6075_writable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case VEML6075_CMD_CONF:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config veml6075_regmap_config = {
	.name = "veml6075",
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = VEML6075_CMD_ID,
	.readable_reg = veml6075_readable_reg,
	.writeable_reg = veml6075_writable_reg,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static int veml6075_probe(struct i2c_client *client)
{
	struct veml6075_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int config, ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &veml6075_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	data = iio_priv(indio_dev);
	data->client = client;
	data->regmap = regmap;

	mutex_init(&data->lock);

	indio_dev->name = "veml6075";
	indio_dev->info = &veml6075_info;
	indio_dev->channels = veml6075_channels;
	indio_dev->num_channels = ARRAY_SIZE(veml6075_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_regulator_get_enable(&client->dev, "vdd");
	if (ret < 0)
		return ret;

	/* default: 100ms integration time, active force enable, shutdown */
	config = FIELD_PREP(VEML6075_CONF_IT, VEML6075_IT_100_MS) |
		FIELD_PREP(VEML6075_CONF_AF, VEML6075_AF_ENABLE) |
		FIELD_PREP(VEML6075_CONF_SD, VEML6075_SD_ENABLE);
	ret = regmap_write(data->regmap, VEML6075_CMD_CONF, config);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id veml6075_id[] = {
	{ "veml6075" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, veml6075_id);

static const struct of_device_id veml6075_of_match[] = {
	{ .compatible = "vishay,veml6075" },
	{}
};
MODULE_DEVICE_TABLE(of, veml6075_of_match);

static struct i2c_driver veml6075_driver = {
	.driver = {
		.name   = "veml6075",
		.of_match_table = veml6075_of_match,
	},
	.probe = veml6075_probe,
	.id_table = veml6075_id,
};

module_i2c_driver(veml6075_driver);

MODULE_AUTHOR("Javier Carrasco <javier.carrasco.cruz@gmail.com>");
MODULE_DESCRIPTION("Vishay VEML6075 UVA and UVB light sensor driver");
MODULE_LICENSE("GPL");
