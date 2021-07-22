// SPDX-License-Identifier: GPL-2.0+
/*
 * si1133.c - Support for Silabs SI1133 combined ambient
 * light and UV index sensors
 *
 * Copyright 2018 Maxime Roussin-Belanger <maxime.roussinbelanger@gmail.com>
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include <linux/util_macros.h>

#include <asm/unaligned.h>

#define SI1133_REG_PART_ID		0x00
#define SI1133_REG_REV_ID		0x01
#define SI1133_REG_MFR_ID		0x02
#define SI1133_REG_INFO0		0x03
#define SI1133_REG_INFO1		0x04

#define SI1133_PART_ID			0x33

#define SI1133_REG_HOSTIN0		0x0A
#define SI1133_REG_COMMAND		0x0B
#define SI1133_REG_IRQ_ENABLE		0x0F
#define SI1133_REG_RESPONSE1		0x10
#define SI1133_REG_RESPONSE0		0x11
#define SI1133_REG_IRQ_STATUS		0x12
#define SI1133_REG_MEAS_RATE		0x1A

#define SI1133_IRQ_CHANNEL_ENABLE	0xF

#define SI1133_CMD_RESET_CTR		0x00
#define SI1133_CMD_RESET_SW		0x01
#define SI1133_CMD_FORCE		0x11
#define SI1133_CMD_START_AUTONOMOUS	0x13
#define SI1133_CMD_PARAM_SET		0x80
#define SI1133_CMD_PARAM_QUERY		0x40
#define SI1133_CMD_PARAM_MASK		0x3F

#define SI1133_CMD_ERR_MASK		BIT(4)
#define SI1133_CMD_SEQ_MASK		0xF
#define SI1133_MAX_CMD_CTR		0xF

#define SI1133_PARAM_REG_CHAN_LIST	0x01
#define SI1133_PARAM_REG_ADCCONFIG(x)	((x) * 4) + 2
#define SI1133_PARAM_REG_ADCSENS(x)	((x) * 4) + 3
#define SI1133_PARAM_REG_ADCPOST(x)	((x) * 4) + 4

#define SI1133_ADCMUX_MASK 0x1F

#define SI1133_ADCCONFIG_DECIM_RATE(x)	(x) << 5

#define SI1133_ADCSENS_SCALE_MASK 0x70
#define SI1133_ADCSENS_SCALE_SHIFT 4
#define SI1133_ADCSENS_HSIG_MASK BIT(7)
#define SI1133_ADCSENS_HSIG_SHIFT 7
#define SI1133_ADCSENS_HW_GAIN_MASK 0xF
#define SI1133_ADCSENS_NB_MEAS(x)	fls(x) << SI1133_ADCSENS_SCALE_SHIFT

#define SI1133_ADCPOST_24BIT_EN BIT(6)
#define SI1133_ADCPOST_POSTSHIFT_BITQTY(x) (x & GENMASK(2, 0)) << 3

#define SI1133_PARAM_ADCMUX_SMALL_IR	0x0
#define SI1133_PARAM_ADCMUX_MED_IR	0x1
#define SI1133_PARAM_ADCMUX_LARGE_IR	0x2
#define SI1133_PARAM_ADCMUX_WHITE	0xB
#define SI1133_PARAM_ADCMUX_LARGE_WHITE	0xD
#define SI1133_PARAM_ADCMUX_UV		0x18
#define SI1133_PARAM_ADCMUX_UV_DEEP	0x19

#define SI1133_ERR_INVALID_CMD		0x0
#define SI1133_ERR_INVALID_LOCATION_CMD 0x1
#define SI1133_ERR_SATURATION_ADC_OR_OVERFLOW_ACCUMULATION 0x2
#define SI1133_ERR_OUTPUT_BUFFER_OVERFLOW 0x3

#define SI1133_COMPLETION_TIMEOUT_MS	500

#define SI1133_CMD_MINSLEEP_US_LOW	5000
#define SI1133_CMD_MINSLEEP_US_HIGH	7500
#define SI1133_CMD_TIMEOUT_MS		25
#define SI1133_CMD_LUX_TIMEOUT_MS	5000
#define SI1133_CMD_TIMEOUT_US		SI1133_CMD_TIMEOUT_MS * 1000

#define SI1133_REG_HOSTOUT(x)		(x) + 0x13

#define SI1133_MEASUREMENT_FREQUENCY 1250

#define SI1133_X_ORDER_MASK            0x0070
#define SI1133_Y_ORDER_MASK            0x0007
#define si1133_get_x_order(m)          ((m) & SI1133_X_ORDER_MASK) >> 4
#define si1133_get_y_order(m)          ((m) & SI1133_Y_ORDER_MASK)

#define SI1133_LUX_ADC_MASK		0xE
#define SI1133_ADC_THRESHOLD		16000
#define SI1133_INPUT_FRACTION_HIGH	7
#define SI1133_INPUT_FRACTION_LOW	15
#define SI1133_LUX_OUTPUT_FRACTION	12
#define SI1133_LUX_BUFFER_SIZE		9
#define SI1133_MEASURE_BUFFER_SIZE	3

static const int si1133_scale_available[] = {
	1, 2, 4, 8, 16, 32, 64, 128};

static IIO_CONST_ATTR(scale_available, "1 2 4 8 16 32 64 128");

static IIO_CONST_ATTR_INT_TIME_AVAIL("0.0244 0.0488 0.0975 0.195 0.390 0.780 "
				     "1.560 3.120 6.24 12.48 25.0 50.0");

/* A.K.A. HW_GAIN in datasheet */
enum si1133_int_time {
	    _24_4_us = 0,
	    _48_8_us = 1,
	    _97_5_us = 2,
	   _195_0_us = 3,
	   _390_0_us = 4,
	   _780_0_us = 5,
	 _1_560_0_us = 6,
	 _3_120_0_us = 7,
	 _6_240_0_us = 8,
	_12_480_0_us = 9,
	_25_ms = 10,
	_50_ms = 11,
};

/* Integration time in milliseconds, nanoseconds */
static const int si1133_int_time_table[][2] = {
	[_24_4_us] = {0, 24400},
	[_48_8_us] = {0, 48800},
	[_97_5_us] = {0, 97500},
	[_195_0_us] = {0, 195000},
	[_390_0_us] = {0, 390000},
	[_780_0_us] = {0, 780000},
	[_1_560_0_us] = {1, 560000},
	[_3_120_0_us] = {3, 120000},
	[_6_240_0_us] = {6, 240000},
	[_12_480_0_us] = {12, 480000},
	[_25_ms] = {25, 000000},
	[_50_ms] = {50, 000000},
};

static const struct regmap_range si1133_reg_ranges[] = {
	regmap_reg_range(0x00, 0x02),
	regmap_reg_range(0x0A, 0x0B),
	regmap_reg_range(0x0F, 0x0F),
	regmap_reg_range(0x10, 0x12),
	regmap_reg_range(0x13, 0x2C),
};

static const struct regmap_range si1133_reg_ro_ranges[] = {
	regmap_reg_range(0x00, 0x02),
	regmap_reg_range(0x10, 0x2C),
};

static const struct regmap_range si1133_precious_ranges[] = {
	regmap_reg_range(0x12, 0x12),
};

static const struct regmap_access_table si1133_write_ranges_table = {
	.yes_ranges	= si1133_reg_ranges,
	.n_yes_ranges	= ARRAY_SIZE(si1133_reg_ranges),
	.no_ranges	= si1133_reg_ro_ranges,
	.n_no_ranges	= ARRAY_SIZE(si1133_reg_ro_ranges),
};

static const struct regmap_access_table si1133_read_ranges_table = {
	.yes_ranges	= si1133_reg_ranges,
	.n_yes_ranges	= ARRAY_SIZE(si1133_reg_ranges),
};

static const struct regmap_access_table si1133_precious_table = {
	.yes_ranges	= si1133_precious_ranges,
	.n_yes_ranges	= ARRAY_SIZE(si1133_precious_ranges),
};

static const struct regmap_config si1133_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0x2C,

	.wr_table = &si1133_write_ranges_table,
	.rd_table = &si1133_read_ranges_table,

	.precious_table = &si1133_precious_table,
};

struct si1133_data {
	struct regmap *regmap;
	struct i2c_client *client;

	/* Lock protecting one command at a time can be processed */
	struct mutex mutex;

	int rsp_seq;
	u8 scan_mask;
	u8 adc_sens[6];
	u8 adc_config[6];

	struct completion completion;
};

struct si1133_coeff {
	s16 info;
	u16 mag;
};

struct si1133_lux_coeff {
	struct si1133_coeff coeff_high[4];
	struct si1133_coeff coeff_low[9];
};

static const struct si1133_lux_coeff lux_coeff = {
	{
		{  0,   209},
		{ 1665,  93},
		{ 2064,  65},
		{-2671, 234}
	},
	{
		{    0,     0},
		{ 1921, 29053},
		{-1022, 36363},
		{ 2320, 20789},
		{ -367, 57909},
		{-1774, 38240},
		{ -608, 46775},
		{-1503, 51831},
		{-1886, 58928}
	}
};

static int si1133_calculate_polynomial_inner(s32 input, u8 fraction, u16 mag,
					     s8 shift)
{
	return ((input << fraction) / mag) << shift;
}

static int si1133_calculate_output(s32 x, s32 y, u8 x_order, u8 y_order,
				   u8 input_fraction, s8 sign,
				   const struct si1133_coeff *coeffs)
{
	s8 shift;
	int x1 = 1;
	int x2 = 1;
	int y1 = 1;
	int y2 = 1;

	shift = ((u16)coeffs->info & 0xFF00) >> 8;
	shift ^= 0xFF;
	shift += 1;
	shift = -shift;

	if (x_order > 0) {
		x1 = si1133_calculate_polynomial_inner(x, input_fraction,
						       coeffs->mag, shift);
		if (x_order > 1)
			x2 = x1;
	}

	if (y_order > 0) {
		y1 = si1133_calculate_polynomial_inner(y, input_fraction,
						       coeffs->mag, shift);
		if (y_order > 1)
			y2 = y1;
	}

	return sign * x1 * x2 * y1 * y2;
}

/*
 * The algorithm is from:
 * https://siliconlabs.github.io/Gecko_SDK_Doc/efm32zg/html/si1133_8c_source.html#l00716
 */
static int si1133_calc_polynomial(s32 x, s32 y, u8 input_fraction, u8 num_coeff,
				  const struct si1133_coeff *coeffs)
{
	u8 x_order, y_order;
	u8 counter;
	s8 sign;
	int output = 0;

	for (counter = 0; counter < num_coeff; counter++) {
		if (coeffs->info < 0)
			sign = -1;
		else
			sign = 1;

		x_order = si1133_get_x_order(coeffs->info);
		y_order = si1133_get_y_order(coeffs->info);

		if ((x_order == 0) && (y_order == 0))
			output +=
			       sign * coeffs->mag << SI1133_LUX_OUTPUT_FRACTION;
		else
			output += si1133_calculate_output(x, y, x_order,
							  y_order,
							  input_fraction, sign,
							  coeffs);
		coeffs++;
	}

	return abs(output);
}

static int si1133_cmd_reset_sw(struct si1133_data *data)
{
	struct device *dev = &data->client->dev;
	unsigned int resp;
	unsigned long timeout;
	int err;

	err = regmap_write(data->regmap, SI1133_REG_COMMAND,
			   SI1133_CMD_RESET_SW);
	if (err)
		return err;

	timeout = jiffies + msecs_to_jiffies(SI1133_CMD_TIMEOUT_MS);
	while (true) {
		err = regmap_read(data->regmap, SI1133_REG_RESPONSE0, &resp);
		if (err == -ENXIO) {
			usleep_range(SI1133_CMD_MINSLEEP_US_LOW,
				     SI1133_CMD_MINSLEEP_US_HIGH);
			continue;
		}

		if ((resp & SI1133_MAX_CMD_CTR) == SI1133_MAX_CMD_CTR)
			break;

		if (time_after(jiffies, timeout)) {
			dev_warn(dev, "Timeout on reset ctr resp: %d\n", resp);
			return -ETIMEDOUT;
		}
	}

	if (!err)
		data->rsp_seq = SI1133_MAX_CMD_CTR;

	return err;
}

static int si1133_parse_response_err(struct device *dev, u32 resp, u8 cmd)
{
	resp &= 0xF;

	switch (resp) {
	case SI1133_ERR_OUTPUT_BUFFER_OVERFLOW:
		dev_warn(dev, "Output buffer overflow: 0x%02x\n", cmd);
		return -EOVERFLOW;
	case SI1133_ERR_SATURATION_ADC_OR_OVERFLOW_ACCUMULATION:
		dev_warn(dev, "Saturation of the ADC or overflow of accumulation: 0x%02x\n",
			 cmd);
		return -EOVERFLOW;
	case SI1133_ERR_INVALID_LOCATION_CMD:
		dev_warn(dev,
			 "Parameter access to an invalid location: 0x%02x\n",
			 cmd);
		return -EINVAL;
	case SI1133_ERR_INVALID_CMD:
		dev_warn(dev, "Invalid command 0x%02x\n", cmd);
		return -EINVAL;
	default:
		dev_warn(dev, "Unknown error 0x%02x\n", cmd);
		return -EINVAL;
	}
}

static int si1133_cmd_reset_counter(struct si1133_data *data)
{
	int err = regmap_write(data->regmap, SI1133_REG_COMMAND,
			       SI1133_CMD_RESET_CTR);
	if (err)
		return err;

	data->rsp_seq = 0;

	return 0;
}

static int si1133_command(struct si1133_data *data, u8 cmd)
{
	struct device *dev = &data->client->dev;
	u32 resp;
	int err;
	int expected_seq;

	mutex_lock(&data->mutex);

	expected_seq = (data->rsp_seq + 1) & SI1133_MAX_CMD_CTR;

	if (cmd == SI1133_CMD_FORCE)
		reinit_completion(&data->completion);

	err = regmap_write(data->regmap, SI1133_REG_COMMAND, cmd);
	if (err) {
		dev_warn(dev, "Failed to write command 0x%02x, ret=%d\n", cmd,
			 err);
		goto out;
	}

	if (cmd == SI1133_CMD_FORCE) {
		/* wait for irq */
		if (!wait_for_completion_timeout(&data->completion,
			msecs_to_jiffies(SI1133_COMPLETION_TIMEOUT_MS))) {
			err = -ETIMEDOUT;
			goto out;
		}
		err = regmap_read(data->regmap, SI1133_REG_RESPONSE0, &resp);
		if (err)
			goto out;
	} else {
		err = regmap_read_poll_timeout(data->regmap,
					       SI1133_REG_RESPONSE0, resp,
					       (resp & SI1133_CMD_SEQ_MASK) ==
					       expected_seq ||
					       (resp & SI1133_CMD_ERR_MASK),
					       SI1133_CMD_MINSLEEP_US_LOW,
					       SI1133_CMD_TIMEOUT_MS * 1000);
		if (err) {
			dev_warn(dev,
				 "Failed to read command 0x%02x, ret=%d\n",
				 cmd, err);
			goto out;
		}
	}

	if (resp & SI1133_CMD_ERR_MASK) {
		err = si1133_parse_response_err(dev, resp, cmd);
		si1133_cmd_reset_counter(data);
	} else {
		data->rsp_seq = expected_seq;
	}

out:
	mutex_unlock(&data->mutex);

	return err;
}

static int si1133_param_set(struct si1133_data *data, u8 param, u32 value)
{
	int err = regmap_write(data->regmap, SI1133_REG_HOSTIN0, value);

	if (err)
		return err;

	return si1133_command(data, SI1133_CMD_PARAM_SET |
			      (param & SI1133_CMD_PARAM_MASK));
}

static int si1133_param_query(struct si1133_data *data, u8 param, u32 *result)
{
	int err = si1133_command(data, SI1133_CMD_PARAM_QUERY |
				 (param & SI1133_CMD_PARAM_MASK));
	if (err)
		return err;

	return regmap_read(data->regmap, SI1133_REG_RESPONSE1, result);
}

#define SI1133_CHANNEL(_ch, _type) \
	.type = _type, \
	.channel = _ch, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME) | \
		BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_HARDWAREGAIN), \

static const struct iio_chan_spec si1133_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.channel = 0,
	},
	{
		SI1133_CHANNEL(SI1133_PARAM_ADCMUX_WHITE, IIO_INTENSITY)
		.channel2 = IIO_MOD_LIGHT_BOTH,
	},
	{
		SI1133_CHANNEL(SI1133_PARAM_ADCMUX_LARGE_WHITE, IIO_INTENSITY)
		.channel2 = IIO_MOD_LIGHT_BOTH,
		.extend_name = "large",
	},
	{
		SI1133_CHANNEL(SI1133_PARAM_ADCMUX_SMALL_IR, IIO_INTENSITY)
		.extend_name = "small",
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_IR,
	},
	{
		SI1133_CHANNEL(SI1133_PARAM_ADCMUX_MED_IR, IIO_INTENSITY)
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_IR,
	},
	{
		SI1133_CHANNEL(SI1133_PARAM_ADCMUX_LARGE_IR, IIO_INTENSITY)
		.extend_name = "large",
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_IR,
	},
	{
		SI1133_CHANNEL(SI1133_PARAM_ADCMUX_UV, IIO_UVINDEX)
	},
	{
		SI1133_CHANNEL(SI1133_PARAM_ADCMUX_UV_DEEP, IIO_UVINDEX)
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_DUV,
	}
};

static int si1133_get_int_time_index(int milliseconds, int nanoseconds)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(si1133_int_time_table); i++) {
		if (milliseconds == si1133_int_time_table[i][0] &&
		    nanoseconds == si1133_int_time_table[i][1])
			return i;
	}
	return -EINVAL;
}

static int si1133_set_integration_time(struct si1133_data *data, u8 adc,
				       int milliseconds, int nanoseconds)
{
	int index;

	index = si1133_get_int_time_index(milliseconds, nanoseconds);
	if (index < 0)
		return index;

	data->adc_sens[adc] &= 0xF0;
	data->adc_sens[adc] |= index;

	return si1133_param_set(data, SI1133_PARAM_REG_ADCSENS(0),
				data->adc_sens[adc]);
}

static int si1133_set_chlist(struct si1133_data *data, u8 scan_mask)
{
	/* channel list already set, no need to reprogram */
	if (data->scan_mask == scan_mask)
		return 0;

	data->scan_mask = scan_mask;

	return si1133_param_set(data, SI1133_PARAM_REG_CHAN_LIST, scan_mask);
}

static int si1133_chan_set_adcconfig(struct si1133_data *data, u8 adc,
				     u8 adc_config)
{
	int err;

	err = si1133_param_set(data, SI1133_PARAM_REG_ADCCONFIG(adc),
			       adc_config);
	if (err)
		return err;

	data->adc_config[adc] = adc_config;

	return 0;
}

static int si1133_update_adcconfig(struct si1133_data *data, uint8_t adc,
				   u8 mask, u8 shift, u8 value)
{
	u32 adc_config;
	int err;

	err = si1133_param_query(data, SI1133_PARAM_REG_ADCCONFIG(adc),
				 &adc_config);
	if (err)
		return err;

	adc_config &= ~mask;
	adc_config |= (value << shift);

	return si1133_chan_set_adcconfig(data, adc, adc_config);
}

static int si1133_set_adcmux(struct si1133_data *data, u8 adc, u8 mux)
{
	if ((mux & data->adc_config[adc]) == mux)
		return 0; /* mux already set to correct value */

	return si1133_update_adcconfig(data, adc, SI1133_ADCMUX_MASK, 0, mux);
}

static int si1133_force_measurement(struct si1133_data *data)
{
	return si1133_command(data, SI1133_CMD_FORCE);
}

static int si1133_bulk_read(struct si1133_data *data, u8 start_reg, u8 length,
			    u8 *buffer)
{
	int err;

	err = si1133_force_measurement(data);
	if (err)
		return err;

	return regmap_bulk_read(data->regmap, start_reg, buffer, length);
}

static int si1133_measure(struct si1133_data *data,
			  struct iio_chan_spec const *chan,
			  int *val)
{
	int err;

	u8 buffer[SI1133_MEASURE_BUFFER_SIZE];

	err = si1133_set_adcmux(data, 0, chan->channel);
	if (err)
		return err;

	/* Deactivate lux measurements if they were active */
	err = si1133_set_chlist(data, BIT(0));
	if (err)
		return err;

	err = si1133_bulk_read(data, SI1133_REG_HOSTOUT(0), sizeof(buffer),
			       buffer);
	if (err)
		return err;

	*val = sign_extend32(get_unaligned_be24(&buffer[0]), 23);

	return err;
}

static irqreturn_t si1133_threaded_irq_handler(int irq, void *private)
{
	struct iio_dev *iio_dev = private;
	struct si1133_data *data = iio_priv(iio_dev);
	u32 irq_status;
	int err;

	err = regmap_read(data->regmap, SI1133_REG_IRQ_STATUS, &irq_status);
	if (err) {
		dev_err_ratelimited(&iio_dev->dev, "Error reading IRQ\n");
		goto out;
	}

	if (irq_status != data->scan_mask)
		return IRQ_NONE;

out:
	complete(&data->completion);

	return IRQ_HANDLED;
}

static int si1133_scale_to_swgain(int scale_integer, int scale_fractional)
{
	scale_integer = find_closest(scale_integer, si1133_scale_available,
				     ARRAY_SIZE(si1133_scale_available));
	if (scale_integer < 0 ||
	    scale_integer > ARRAY_SIZE(si1133_scale_available) ||
	    scale_fractional != 0)
		return -EINVAL;

	return scale_integer;
}

static int si1133_chan_set_adcsens(struct si1133_data *data, u8 adc,
				   u8 adc_sens)
{
	int err;

	err = si1133_param_set(data, SI1133_PARAM_REG_ADCSENS(adc), adc_sens);
	if (err)
		return err;

	data->adc_sens[adc] = adc_sens;

	return 0;
}

static int si1133_update_adcsens(struct si1133_data *data, u8 mask,
				 u8 shift, u8 value)
{
	int err;
	u32 adc_sens;

	err = si1133_param_query(data, SI1133_PARAM_REG_ADCSENS(0),
				 &adc_sens);
	if (err)
		return err;

	adc_sens &= ~mask;
	adc_sens |= (value << shift);

	return si1133_chan_set_adcsens(data, 0, adc_sens);
}

static int si1133_get_lux(struct si1133_data *data, int *val)
{
	int err;
	int lux;
	s32 high_vis;
	s32 low_vis;
	s32 ir;
	u8 buffer[SI1133_LUX_BUFFER_SIZE];

	/* Activate lux channels */
	err = si1133_set_chlist(data, SI1133_LUX_ADC_MASK);
	if (err)
		return err;

	err = si1133_bulk_read(data, SI1133_REG_HOSTOUT(0),
			       SI1133_LUX_BUFFER_SIZE, buffer);
	if (err)
		return err;

	high_vis = sign_extend32(get_unaligned_be24(&buffer[0]), 23);

	low_vis = sign_extend32(get_unaligned_be24(&buffer[3]), 23);

	ir = sign_extend32(get_unaligned_be24(&buffer[6]), 23);

	if (high_vis > SI1133_ADC_THRESHOLD || ir > SI1133_ADC_THRESHOLD)
		lux = si1133_calc_polynomial(high_vis, ir,
					     SI1133_INPUT_FRACTION_HIGH,
					     ARRAY_SIZE(lux_coeff.coeff_high),
					     &lux_coeff.coeff_high[0]);
	else
		lux = si1133_calc_polynomial(low_vis, ir,
					     SI1133_INPUT_FRACTION_LOW,
					     ARRAY_SIZE(lux_coeff.coeff_low),
					     &lux_coeff.coeff_low[0]);

	*val = lux >> SI1133_LUX_OUTPUT_FRACTION;

	return err;
}

static int si1133_read_raw(struct iio_dev *iio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct si1133_data *data = iio_priv(iio_dev);
	u8 adc_sens = data->adc_sens[0];
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_LIGHT:
			err = si1133_get_lux(data, val);
			if (err)
				return err;

			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_INTENSITY:
		case IIO_UVINDEX:
			err = si1133_measure(data, chan, val);
			if (err)
				return err;

			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_INT_TIME:
		switch (chan->type) {
		case IIO_INTENSITY:
		case IIO_UVINDEX:
			adc_sens &= SI1133_ADCSENS_HW_GAIN_MASK;

			*val = si1133_int_time_table[adc_sens][0];
			*val2 = si1133_int_time_table[adc_sens][1];
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_INTENSITY:
		case IIO_UVINDEX:
			adc_sens &= SI1133_ADCSENS_SCALE_MASK;
			adc_sens >>= SI1133_ADCSENS_SCALE_SHIFT;

			*val = BIT(adc_sens);

			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_HARDWAREGAIN:
		switch (chan->type) {
		case IIO_INTENSITY:
		case IIO_UVINDEX:
			adc_sens >>= SI1133_ADCSENS_HSIG_SHIFT;

			*val = adc_sens;

			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int si1133_write_raw(struct iio_dev *iio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct si1133_data *data = iio_priv(iio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_INTENSITY:
		case IIO_UVINDEX:
			val = si1133_scale_to_swgain(val, val2);
			if (val < 0)
				return val;

			return si1133_update_adcsens(data,
						     SI1133_ADCSENS_SCALE_MASK,
						     SI1133_ADCSENS_SCALE_SHIFT,
						     val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_INT_TIME:
		return si1133_set_integration_time(data, 0, val, val2);
	case IIO_CHAN_INFO_HARDWAREGAIN:
		switch (chan->type) {
		case IIO_INTENSITY:
		case IIO_UVINDEX:
			if (val != 0 && val != 1)
				return -EINVAL;

			return si1133_update_adcsens(data,
						     SI1133_ADCSENS_HSIG_MASK,
						     SI1133_ADCSENS_HSIG_SHIFT,
						     val);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static struct attribute *si1133_attributes[] = {
	&iio_const_attr_integration_time_available.dev_attr.attr,
	&iio_const_attr_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group si1133_attribute_group = {
	.attrs = si1133_attributes,
};

static const struct iio_info si1133_info = {
	.read_raw = si1133_read_raw,
	.write_raw = si1133_write_raw,
	.attrs = &si1133_attribute_group,
};

/*
 * si1133_init_lux_channels - Configure 3 different channels(adc) (1,2 and 3)
 * The channel configuration for the lux measurement was taken from :
 * https://siliconlabs.github.io/Gecko_SDK_Doc/efm32zg/html/si1133_8c_source.html#l00578
 *
 * Reserved the channel 0 for the other raw measurements
 */
static int si1133_init_lux_channels(struct si1133_data *data)
{
	int err;

	err = si1133_chan_set_adcconfig(data, 1,
					SI1133_ADCCONFIG_DECIM_RATE(1) |
					SI1133_PARAM_ADCMUX_LARGE_WHITE);
	if (err)
		return err;

	err = si1133_param_set(data, SI1133_PARAM_REG_ADCPOST(1),
			       SI1133_ADCPOST_24BIT_EN |
			       SI1133_ADCPOST_POSTSHIFT_BITQTY(0));
	if (err)
		return err;
	err = si1133_chan_set_adcsens(data, 1, SI1133_ADCSENS_HSIG_MASK |
				      SI1133_ADCSENS_NB_MEAS(64) | _48_8_us);
	if (err)
		return err;

	err = si1133_chan_set_adcconfig(data, 2,
					SI1133_ADCCONFIG_DECIM_RATE(1) |
					SI1133_PARAM_ADCMUX_LARGE_WHITE);
	if (err)
		return err;

	err = si1133_param_set(data, SI1133_PARAM_REG_ADCPOST(2),
			       SI1133_ADCPOST_24BIT_EN |
			       SI1133_ADCPOST_POSTSHIFT_BITQTY(2));
	if (err)
		return err;

	err = si1133_chan_set_adcsens(data, 2, SI1133_ADCSENS_HSIG_MASK |
				      SI1133_ADCSENS_NB_MEAS(1) | _3_120_0_us);
	if (err)
		return err;

	err = si1133_chan_set_adcconfig(data, 3,
					SI1133_ADCCONFIG_DECIM_RATE(1) |
					SI1133_PARAM_ADCMUX_MED_IR);
	if (err)
		return err;

	err = si1133_param_set(data, SI1133_PARAM_REG_ADCPOST(3),
			       SI1133_ADCPOST_24BIT_EN |
			       SI1133_ADCPOST_POSTSHIFT_BITQTY(2));
	if (err)
		return err;

	return  si1133_chan_set_adcsens(data, 3, SI1133_ADCSENS_HSIG_MASK |
					SI1133_ADCSENS_NB_MEAS(64) | _48_8_us);
}

static int si1133_initialize(struct si1133_data *data)
{
	int err;

	err = si1133_cmd_reset_sw(data);
	if (err)
		return err;

	/* Turn off autonomous mode */
	err = si1133_param_set(data, SI1133_REG_MEAS_RATE, 0);
	if (err)
		return err;

	err = si1133_init_lux_channels(data);
	if (err)
		return err;

	return regmap_write(data->regmap, SI1133_REG_IRQ_ENABLE,
			    SI1133_IRQ_CHANNEL_ENABLE);
}

static int si1133_validate_ids(struct iio_dev *iio_dev)
{
	struct si1133_data *data = iio_priv(iio_dev);

	unsigned int part_id, rev_id, mfr_id;
	int err;

	err = regmap_read(data->regmap, SI1133_REG_PART_ID, &part_id);
	if (err)
		return err;

	err = regmap_read(data->regmap, SI1133_REG_REV_ID, &rev_id);
	if (err)
		return err;

	err = regmap_read(data->regmap, SI1133_REG_MFR_ID, &mfr_id);
	if (err)
		return err;

	dev_info(&iio_dev->dev,
		 "Device ID part 0x%02x rev 0x%02x mfr 0x%02x\n",
		 part_id, rev_id, mfr_id);
	if (part_id != SI1133_PART_ID) {
		dev_err(&iio_dev->dev,
			"Part ID mismatch got 0x%02x, expected 0x%02x\n",
			part_id, SI1133_PART_ID);
		return -ENODEV;
	}

	return 0;
}

static int si1133_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct si1133_data *data;
	struct iio_dev *iio_dev;
	int err;

	iio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!iio_dev)
		return -ENOMEM;

	data = iio_priv(iio_dev);

	init_completion(&data->completion);

	data->regmap = devm_regmap_init_i2c(client, &si1133_regmap_config);
	if (IS_ERR(data->regmap)) {
		err = PTR_ERR(data->regmap);
		dev_err(&client->dev, "Failed to initialise regmap: %d\n", err);
		return err;
	}

	i2c_set_clientdata(client, iio_dev);
	data->client = client;

	iio_dev->name = id->name;
	iio_dev->channels = si1133_channels;
	iio_dev->num_channels = ARRAY_SIZE(si1133_channels);
	iio_dev->info = &si1133_info;
	iio_dev->modes = INDIO_DIRECT_MODE;

	mutex_init(&data->mutex);

	err = si1133_validate_ids(iio_dev);
	if (err)
		return err;

	err = si1133_initialize(data);
	if (err) {
		dev_err(&client->dev,
			"Error when initializing chip: %d\n", err);
		return err;
	}

	if (!client->irq) {
		dev_err(&client->dev,
			"Required interrupt not provided, cannot proceed\n");
		return -EINVAL;
	}

	err = devm_request_threaded_irq(&client->dev, client->irq,
					NULL,
					si1133_threaded_irq_handler,
					IRQF_ONESHOT | IRQF_SHARED,
					client->name, iio_dev);
	if (err) {
		dev_warn(&client->dev, "Request irq %d failed: %i\n",
			 client->irq, err);
		return err;
	}

	return devm_iio_device_register(&client->dev, iio_dev);
}

static const struct i2c_device_id si1133_ids[] = {
	{ "si1133", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si1133_ids);

static struct i2c_driver si1133_driver = {
	.driver = {
	    .name   = "si1133",
	},
	.probe  = si1133_probe,
	.id_table = si1133_ids,
};

module_i2c_driver(si1133_driver);

MODULE_AUTHOR("Maxime Roussin-Belanger <maxime.roussinbelanger@gmail.com>");
MODULE_DESCRIPTION("Silabs SI1133, UV index sensor and ambient light sensor driver");
MODULE_LICENSE("GPL");
