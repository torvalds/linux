// SPDX-License-Identifier: GPL-2.0-only
/*
 * Texas Instruments ADS112C04 16-bit I2C ADC driver
 *
 * Copyright (c) 2026 Kyle Hsieh <kylehsieh1995@gmail.com>
 *
 * Datasheet: https://www.ti.com/lit/ds/symlink/ads112c04.pdf
 * Based on TI Reference Code and standard Linux IIO framework.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/types.h>
#include <linux/units.h>

#include <linux/iio/iio.h>

#define ADS112C04_CMD_RESET		0x06
#define ADS112C04_CMD_START_SYNC	0x08
#define ADS112C04_CMD_RDATA		0x10
#define ADS112C04_CMD_RREG(reg)		(0x20 | ((reg) << 2))
#define ADS112C04_CMD_WREG(reg)		(0x40 | ((reg) << 2))

#define ADS112C04_REG_CONFIG0			0x00
#define   ADS112C04_CONF0_MUX			  GENMASK(7, 4)
#define     ADS112C04_CONF0_MUX_AIN0_AIN1	    0
#define     ADS112C04_CONF0_MUX_AIN_SINGLE_BASE	    8
#define   ADS112C04_CONF0_GAIN			  GENMASK(3, 1)
#define     ADS112C04_CONF0_GAIN_X1		    0
#define   ADS112C04_CONF0_PGA_BYPASS		  BIT(0)

#define ADS112C04_REG_CONFIG1			0x01
#define   ADS112C04_CONF1_DR			  GENMASK(7, 5)
#define     ADS112C04_CONF1_DR_20SPS		    0
#define   ADS112C04_CONF1_MODE			  BIT(4)
#define     ADS112C04_CONF1_MODE_NORMAL		    0
#define   ADS112C04_CONF1_CM			  BIT(3)
#define     ADS112C04_CONF1_CM_SINGLE_SHOT	    0
#define   ADS112C04_CONF1_VREF			  GENMASK(2, 1)
#define     ADS112C04_CONF1_VREF_INTERNAL	    0
#define     ADS112C04_CONF1_VREF_EXTERNAL	    1
#define     ADS112C04_CONF1_VREF_AVDD		    2
#define   ADS112C04_CONF1_TS			  BIT(0)
#define     ADS112C04_CONF1_TS_DISABLED		    0

#define ADS112C04_REG_CONFIG2			0x02
#define   ADS112C04_CONF2_DRDY			  BIT(7)

#define ADS112C04_INT_REF_mV			2048

#define ADS112C04_MAX_CHANNELS		12

#define ADS112C04_RESOLUTION_BITS	16

enum {
	ADS112C04_VREF_SOURCE_INTERNAL,
	ADS112C04_VREF_SOURCE_EXTERNAL,
	ADS112C04_VREF_SOURCE_AVDD,
};

static const char * const ads112c04_vref_names[] = {
	[ADS112C04_VREF_SOURCE_INTERNAL] = "internal",
	[ADS112C04_VREF_SOURCE_EXTERNAL] = "external",
	[ADS112C04_VREF_SOURCE_AVDD]     = "avdd",
};

static const u8 ads112c04_vref_reg_val[] = {
	[ADS112C04_VREF_SOURCE_INTERNAL] = ADS112C04_CONF1_VREF_INTERNAL,
	[ADS112C04_VREF_SOURCE_EXTERNAL] = ADS112C04_CONF1_VREF_EXTERNAL,
	[ADS112C04_VREF_SOURCE_AVDD]     = ADS112C04_CONF1_VREF_AVDD,
};

/* Indexed by [AINP][AINN], -1 means the combination is not available. */
static const s8 ads112c04_diff_mux[4][4] = {
	{ -1,  0,  1,  2 },
	{  3, -1,  4,  5 },
	{ -1, -1, -1,  6 },
	{ -1, -1,  7, -1 },
};

struct ads112c04_state {
	struct i2c_client *client;
	/* Protects concurrent ADC reads and device configuration */
	struct mutex lock;
	struct completion completion;
	u32 avdd_mV;
	u32 ext_ref_mV;
	u8 *vref_source;	/* one entry per channel, indexed by scan order */
	u8 config0;
	u8 config1;
};

static int ads112c04_write_cmd(struct i2c_client *client, u8 cmd)
{
	return i2c_smbus_write_byte(client, cmd);
}

static int ads112c04_read_reg(struct i2c_client *client, u8 reg, u8 *val)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, ADS112C04_CMD_RREG(reg));
	if (ret < 0)
		return ret;

	*val = ret;

	return 0;
}

static int ads112c04_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(client, ADS112C04_CMD_WREG(reg), val);
}

static int ads112c04_wait_for_data(struct ads112c04_state *st)
{
	int ret, err;
	u8 val;

	if (st->client->irq > 0) {
		/* Timeout is 100ms (slowest data rate is 20 SPS) */
		if (!wait_for_completion_timeout(&st->completion, msecs_to_jiffies(100)))
			return -ETIMEDOUT;

		return 0;
	}

	ret = read_poll_timeout(ads112c04_read_reg, err,
				err < 0 || (val & ADS112C04_CONF2_DRDY),
				1 * USEC_PER_MSEC, 100 * USEC_PER_MSEC, false,
				st->client, ADS112C04_REG_CONFIG2, &val);
	if (err < 0)
		return err;

	return ret;
}

static int ads112c04_read_data(struct ads112c04_state *st, int *val)
{
	int ret;

	ret = i2c_smbus_read_word_swapped(st->client, ADS112C04_CMD_RDATA);
	if (ret < 0)
		return ret;

	*val = sign_extend32(ret, ADS112C04_RESOLUTION_BITS - 1);

	return 0;
}

static int ads112c04_get_adc_result(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    int *val)
{
	struct ads112c04_state *st = iio_priv(indio_dev);
	unsigned int idx = chan - indio_dev->channels;
	u8 new_config0, new_config1;
	int ret;

	new_config0 = st->config0;
	FIELD_MODIFY(ADS112C04_CONF0_MUX, &new_config0, chan->address);

	if (st->config0 != new_config0) {
		ret = ads112c04_write_reg(st->client, ADS112C04_REG_CONFIG0,
					  new_config0);
		if (ret < 0)
			return ret;

		st->config0 = new_config0;
	}

	new_config1 = st->config1;
	FIELD_MODIFY(ADS112C04_CONF1_VREF, &new_config1,
		     ads112c04_vref_reg_val[st->vref_source[idx]]);

	if (st->config1 != new_config1) {
		ret = ads112c04_write_reg(st->client, ADS112C04_REG_CONFIG1,
					  new_config1);
		if (ret < 0)
			return ret;

		st->config1 = new_config1;
	}

	reinit_completion(&st->completion);

	ret = ads112c04_write_cmd(st->client, ADS112C04_CMD_START_SYNC);
	if (ret < 0)
		return ret;

	ret = ads112c04_wait_for_data(st);
	if (ret < 0)
		return ret;

	return ads112c04_read_data(st, val);
}

static int ads112c04_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct ads112c04_state *st = iio_priv(indio_dev);
	unsigned int idx = chan - indio_dev->channels;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);
		ret = ads112c04_get_adc_result(indio_dev, chan, val);
		mutex_unlock(&st->lock);

		if (ret < 0)
			return ret;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		switch (st->vref_source[idx]) {
		case ADS112C04_VREF_SOURCE_EXTERNAL:
			*val = st->ext_ref_mV;
			break;
		case ADS112C04_VREF_SOURCE_AVDD:
			*val = st->avdd_mV;
			break;
		default:
			*val = ADS112C04_INT_REF_mV;
			break;
		}
		*val2 = ADS112C04_RESOLUTION_BITS - 1;
		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}
}

static irqreturn_t ads112c04_irq_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct ads112c04_state *st = iio_priv(indio_dev);

	complete(&st->completion);

	return IRQ_HANDLED;
}

static const struct iio_info ads112c04_info = {
	.read_raw = ads112c04_read_raw,
};

static int ads112c04_parse_vref_source(struct fwnode_handle *child)
{
	if (!fwnode_property_present(child, "reference-sources"))
		return ADS112C04_VREF_SOURCE_INTERNAL;

	return fwnode_property_match_property_string(child, "reference-sources",
						    ads112c04_vref_names,
						    ARRAY_SIZE(ads112c04_vref_names));
}

static int ads112c04_parse_channels(struct iio_dev *indio_dev,
				    bool *need_avdd_ref, bool *need_ext_ref)
{
	struct device *dev = indio_dev->dev.parent;
	struct ads112c04_state *st = iio_priv(indio_dev);
	struct iio_chan_spec *channels;
	u32 num_channels, pair[2], channel;
	unsigned int i;
	int ret;

	num_channels = device_get_named_child_node_count(dev, "channel");
	if (!num_channels)
		return dev_err_probe(dev, -EINVAL, "no channel subnodes found\n");

	if (num_channels > ADS112C04_MAX_CHANNELS)
		return dev_err_probe(dev, -EINVAL,
				     "num of channel nodes exceeds %d\n",
				     ADS112C04_MAX_CHANNELS);

	channels = devm_kcalloc(dev, num_channels, sizeof(*channels), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	st->vref_source = devm_kcalloc(dev, num_channels,
				       sizeof(*st->vref_source), GFP_KERNEL);
	if (!st->vref_source)
		return -ENOMEM;

	i = 0;
	device_for_each_named_child_node_scoped(dev, child, "channel") {
		const char *s_chan = "single-channel", *d_chan = "diff-channels";
		struct iio_chan_spec *spec = &channels[i];

		if (fwnode_property_present(child, "excitation-channels"))
			return dev_err_probe(dev, -EOPNOTSUPP,
					     "excitation-channels is not supported yet\n");

		ret = ads112c04_parse_vref_source(child);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "invalid reference-sources value\n");

		st->vref_source[i] = ret;

		*need_ext_ref |= st->vref_source[i] == ADS112C04_VREF_SOURCE_EXTERNAL;
		*need_avdd_ref |= st->vref_source[i] == ADS112C04_VREF_SOURCE_AVDD;

		/*
		 * REVISIT: when ti,refp-refn-resistor-ohms is implemented, a
		 * channel using an external resistor reference is effectively
		 * a resistance measurement and should use IIO_RESISTANCE.
		 */
		spec->type = IIO_VOLTAGE;
		spec->indexed = 1;
		spec->info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE);

		if (fwnode_property_present(child, s_chan)) {
			ret = fwnode_property_read_u32(child, s_chan, &channel);
			if (ret)
				return dev_err_probe(dev, ret,
						     "failed to read %s property\n", s_chan);

			if (channel > 3)
				return dev_err_probe(dev, -EINVAL, "%s must be 0-3\n", s_chan);

			spec->channel = channel;
			spec->address = ADS112C04_CONF0_MUX_AIN_SINGLE_BASE + channel;
		} else if (fwnode_property_present(child, d_chan)) {
			ret = fwnode_property_read_u32_array(child, d_chan, pair, ARRAY_SIZE(pair));
			if (ret)
				return dev_err_probe(dev, ret,
						     "failed to read %s property\n", d_chan);

			if (pair[0] > 3 || pair[1] > 3)
				return dev_err_probe(dev, -EINVAL, "%s must be 0-3\n", d_chan);

			spec->channel = pair[0];
			spec->channel2 = pair[1];
			spec->differential = 1;

			if (ads112c04_diff_mux[pair[0]][pair[1]] < 0)
				return dev_err_probe(dev, -EINVAL,
						     "invalid %s combination\n", d_chan);

			spec->address = ads112c04_diff_mux[pair[0]][pair[1]];
		} else {
			return dev_err_probe(dev, -EINVAL,
					     "channel node must have %s or %s\n", s_chan, d_chan);
		}

		i++;
	}

	indio_dev->channels = channels;
	indio_dev->num_channels = i;

	return 0;
}

static int ads112c04_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct iio_dev *indio_dev;
	struct ads112c04_state *st;
	struct reset_control *reset;
	bool need_avdd_ref = false, need_ext_ref = false;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->client = client;

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	init_completion(&st->completion);

	indio_dev->name = "ads112c04";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ads112c04_info;

	/* Forward compatibility checks for unimplemented DT properties */
	if (device_property_present(dev, "refn-supply") ||
	    device_property_present(dev, "ti,refp-refn-resistor-ohms"))
		return dev_err_probe(dev, -EOPNOTSUPP,
				     "refn-supply and external resistors are not supported yet\n");

	ret = ads112c04_parse_channels(indio_dev, &need_avdd_ref, &need_ext_ref);
	if (ret)
		return ret;

	if (need_avdd_ref) {
		ret = devm_regulator_get_enable_read_voltage(dev, "avdd");
		if (ret < 0)
			return dev_err_probe(dev, ret, "failed to get avdd voltage\n");

		st->avdd_mV = ret / (MICRO / MILLI);
	} else {
		ret = devm_regulator_get_enable(dev, "avdd");
		if (ret)
			return dev_err_probe(dev, ret, "failed to get avdd regulator\n");
	}

	ret = devm_regulator_get_enable(dev, "dvdd");
	if (ret)
		return dev_err_probe(dev, ret, "failed to get dvdd regulator\n");

	if (device_property_present(dev, "refp-supply")) {
		ret = devm_regulator_get_enable_read_voltage(dev, "refp");
		if (ret < 0)
			return dev_err_probe(dev, ret, "failed to get refp voltage\n");

		st->ext_ref_mV = ret / (MICRO / MILLI);
	}

	if (need_ext_ref && !st->ext_ref_mV)
		return dev_err_probe(dev, -EINVAL,
				     "external reference measurements require refp-supply\n");

	/* Datasheet: POR releases ~500us after supplies are stable */
	fsleep(500);

	reset = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(reset))
		return dev_err_probe(dev, PTR_ERR(reset), "failed to get reset\n");

	if (reset) {
		/* Datasheet: tw(RSL), the RESET low pulse, is 250ns minimum */
		fsleep(1);

		ret = reset_control_deassert(reset);
		if (ret)
			return dev_err_probe(dev, ret, "failed to deassert reset\n");
	} else {
		ret = ads112c04_write_cmd(client, ADS112C04_CMD_RESET);
		if (ret < 0)
			return ret;
	}

	/* Datasheet: td(RSSTA) is 100ns minimum after the RESET rising edge */
	fsleep(1);

	/*
	 * Initialize CONFIG0 with all fields explicit: gain of 1 with the PGA
	 * bypassed, which allows full-scale single-ended measurements. The MUX
	 * field is updated per channel before each conversion.
	 */
	st->config0 = FIELD_PREP(ADS112C04_CONF0_MUX,
				 ADS112C04_CONF0_MUX_AIN0_AIN1) |
		      FIELD_PREP(ADS112C04_CONF0_GAIN,
				 ADS112C04_CONF0_GAIN_X1) |
		      ADS112C04_CONF0_PGA_BYPASS;

	ret = ads112c04_write_reg(client, ADS112C04_REG_CONFIG0, st->config0);
	if (ret)
		return ret;

	st->config1 = FIELD_PREP(ADS112C04_CONF1_DR,
				 ADS112C04_CONF1_DR_20SPS) |
		      FIELD_PREP(ADS112C04_CONF1_MODE,
				 ADS112C04_CONF1_MODE_NORMAL) |
		      FIELD_PREP(ADS112C04_CONF1_CM,
				 ADS112C04_CONF1_CM_SINGLE_SHOT) |
		      FIELD_PREP(ADS112C04_CONF1_VREF,
				 ADS112C04_CONF1_VREF_INTERNAL) |
		      FIELD_PREP(ADS112C04_CONF1_TS,
				 ADS112C04_CONF1_TS_DISABLED);

	ret = ads112c04_write_reg(client, ADS112C04_REG_CONFIG1, st->config1);
	if (ret)
		return ret;

	if (client->irq > 0) {
		ret = devm_request_irq(dev, client->irq, ads112c04_irq_handler, 0,
				       indio_dev->name, indio_dev);
		if (ret)
			return ret;
	}

	return devm_iio_device_register(dev, indio_dev);
}

static const struct i2c_device_id ads112c04_id[] = {
	{ .name = "ads112c04" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ads112c04_id);

static const struct of_device_id ads112c04_of_match[] = {
	{ .compatible = "ti,ads112c04" },
	{ }
};
MODULE_DEVICE_TABLE(of, ads112c04_of_match);

static struct i2c_driver ads112c04_driver = {
	.driver = {
		.name = "ads112c04",
		.of_match_table = ads112c04_of_match,
	},
	.probe = ads112c04_probe,
	.id_table = ads112c04_id,
};
module_i2c_driver(ads112c04_driver);

MODULE_AUTHOR("Kyle Hsieh <kylehsieh1995@gmail.com>");
MODULE_DESCRIPTION("Texas Instruments ADS112C04 ADC driver");
MODULE_LICENSE("GPL");
