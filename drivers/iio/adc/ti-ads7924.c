// SPDX-License-Identifier: GPL-2.0
/*
 * IIO driver for Texas Instruments ADS7924 ADC, 12-bit, 4-Channels, I2C
 *
 * Author: Hugo Villeneuve <hvilleneuve@dimonoff.com>
 * Copyright 2022 DimOnOff
 *
 * based on iio/adc/ti-ads1015.c
 * Copyright (c) 2016, Intel Corporation.
 *
 * Datasheet: https://www.ti.com/lit/gpn/ads7924
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>
#include <linux/iio/types.h>

#define ADS7924_CHANNELS	 4
#define ADS7924_BITS		12
#define ADS7924_DATA_SHIFT	 4

/* Registers. */
#define ADS7924_MODECNTRL_REG	0x00
#define ADS7924_INTCNTRL_REG	0x01
#define ADS7924_DATA0_U_REG	0x02
#define ADS7924_DATA0_L_REG	0x03
#define ADS7924_DATA1_U_REG	0x04
#define ADS7924_DATA1_L_REG	0x05
#define ADS7924_DATA2_U_REG	0x06
#define ADS7924_DATA2_L_REG	0x07
#define ADS7924_DATA3_U_REG	0x08
#define ADS7924_DATA3_L_REG	0x09
#define ADS7924_ULR0_REG	0x0A
#define ADS7924_LLR0_REG	0x0B
#define ADS7924_ULR1_REG	0x0C
#define ADS7924_LLR1_REG	0x0D
#define ADS7924_ULR2_REG	0x0E
#define ADS7924_LLR2_REG	0x0F
#define ADS7924_ULR3_REG	0x10
#define ADS7924_LLR3_REG	0x11
#define ADS7924_INTCONFIG_REG	0x12
#define ADS7924_SLPCONFIG_REG	0x13
#define ADS7924_ACQCONFIG_REG	0x14
#define ADS7924_PWRCONFIG_REG	0x15
#define ADS7924_RESET_REG	0x16

/*
 * Register address INC bit: when set to '1', the register address is
 * automatically incremented after every register read which allows convenient
 * reading of multiple registers. Set INC to '0' when reading a single register.
 */
#define ADS7924_AUTO_INCREMENT_BIT	BIT(7)

#define ADS7924_MODECNTRL_MODE_MASK	GENMASK(7, 2)

#define ADS7924_MODECNTRL_SEL_MASK	GENMASK(1, 0)

#define ADS7924_CFG_INTPOL_BIT		1
#define ADS7924_CFG_INTTRIG_BIT		0

#define ADS7924_CFG_INTPOL_MASK		BIT(ADS7924_CFG_INTPOL_BIT)
#define ADS7924_CFG_INTTRIG_MASK	BIT(ADS7924_CFG_INTTRIG_BIT)

/* Interrupt pin polarity */
#define ADS7924_CFG_INTPOL_LOW		0
#define ADS7924_CFG_INTPOL_HIGH		1

/* Interrupt pin signaling */
#define ADS7924_CFG_INTTRIG_LEVEL	0
#define ADS7924_CFG_INTTRIG_EDGE	1

/* Mode control values */
#define ADS7924_MODECNTRL_IDLE			0x00
#define ADS7924_MODECNTRL_AWAKE			0x20
#define ADS7924_MODECNTRL_MANUAL_SINGLE		0x30
#define ADS7924_MODECNTRL_MANUAL_SCAN		0x32
#define ADS7924_MODECNTRL_AUTO_SINGLE		0x31
#define ADS7924_MODECNTRL_AUTO_SCAN		0x33
#define ADS7924_MODECNTRL_AUTO_SINGLE_SLEEP	0x39
#define ADS7924_MODECNTRL_AUTO_SCAN_SLEEP	0x3B
#define ADS7924_MODECNTRL_AUTO_BURST_SLEEP	0x3F

#define ADS7924_ACQTIME_MASK	GENMASK(4, 0)

#define ADS7924_PWRUPTIME_MASK	GENMASK(4, 0)

/*
 * The power-up time is allowed to elapse whenever the device has been shutdown
 * in idle mode. Power-up time can allow external circuits, such as an
 * operational amplifier, between the MUXOUT and ADCIN pins to turn on.
 * The nominal time programmed by the PUTIME[4:0] register bits is given by:
 *     t PU = PWRUPTIME[4:0] × 2 μs
 * If a power-up time is not required, set the bits to '0' to effectively bypass.
 */
#define ADS7924_PWRUPTIME_US 0 /* Bypass (0us). */

/*
 * Acquisition Time according to ACQTIME[4:0] register bits.
 * The Acquisition Time is given by:
 *     t ACQ = (ACQTIME[4:0] × 2 μs) + 6 μs
 * Using default value of 0 for ACQTIME[4:0] results in a minimum acquisition
 * time of 6us.
 */
#define ADS7924_ACQTIME_US 6

/* The conversion time is always 4μs and cannot be programmed by the user. */
#define ADS7924_CONVTIME_US 4

#define ADS7924_TOTAL_CONVTIME_US (ADS7924_PWRUPTIME_US + ADS7924_ACQTIME_US + \
				   ADS7924_CONVTIME_US)

#define ADS7924_V_CHAN(_chan, _addr) {				\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = _chan,					\
	.address = _addr,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), 		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.datasheet_name = "AIN"#_chan,				\
}

struct ads7924_data {
	struct device *dev;
	struct regmap *regmap;
	struct regulator *vref_reg;

	/* GPIO descriptor for device hard-reset pin. */
	struct gpio_desc *reset_gpio;

	/*
	 * Protects ADC ops, e.g: concurrent sysfs/buffered
	 * data reads, configuration updates
	 */
	struct mutex lock;

	/*
	 * Set to true when the ADC is switched to the continuous-conversion
	 * mode and exits from a power-down state. This flag is used to avoid
	 * getting the stale result from the conversion register.
	 */
	bool conv_invalid;
};

static bool ads7924_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADS7924_MODECNTRL_REG:
	case ADS7924_INTCNTRL_REG:
	case ADS7924_ULR0_REG:
	case ADS7924_LLR0_REG:
	case ADS7924_ULR1_REG:
	case ADS7924_LLR1_REG:
	case ADS7924_ULR2_REG:
	case ADS7924_LLR2_REG:
	case ADS7924_ULR3_REG:
	case ADS7924_LLR3_REG:
	case ADS7924_INTCONFIG_REG:
	case ADS7924_SLPCONFIG_REG:
	case ADS7924_ACQCONFIG_REG:
	case ADS7924_PWRCONFIG_REG:
	case ADS7924_RESET_REG:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config ads7924_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = ADS7924_RESET_REG,
	.writeable_reg = ads7924_is_writeable_reg,
};

static const struct iio_chan_spec ads7924_channels[] = {
	ADS7924_V_CHAN(0, ADS7924_DATA0_U_REG),
	ADS7924_V_CHAN(1, ADS7924_DATA1_U_REG),
	ADS7924_V_CHAN(2, ADS7924_DATA2_U_REG),
	ADS7924_V_CHAN(3, ADS7924_DATA3_U_REG),
};

static int ads7924_get_adc_result(struct ads7924_data *data,
				  struct iio_chan_spec const *chan, int *val)
{
	int ret;
	__be16 be_val;

	if (chan->channel < 0 || chan->channel >= ADS7924_CHANNELS)
		return -EINVAL;

	if (data->conv_invalid) {
		int conv_time;

		conv_time = ADS7924_TOTAL_CONVTIME_US;
		/* Allow 10% for internal clock inaccuracy. */
		conv_time += conv_time / 10;
		usleep_range(conv_time, conv_time + 1);
		data->conv_invalid = false;
	}

	ret = regmap_raw_read(data->regmap, ADS7924_AUTO_INCREMENT_BIT |
			      chan->address, &be_val, sizeof(be_val));
	if (ret)
		return ret;

	*val = be16_to_cpu(be_val) >> ADS7924_DATA_SHIFT;

	return 0;
}

static int ads7924_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	int ret, vref_uv;
	struct ads7924_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->lock);
		ret = ads7924_get_adc_result(data, chan, val);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		vref_uv = regulator_get_voltage(data->vref_reg);
		if (vref_uv < 0)
			return vref_uv;

		*val =  vref_uv / 1000; /* Convert reg voltage to mV */
		*val2 = ADS7924_BITS;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static const struct iio_info ads7924_info = {
	.read_raw = ads7924_read_raw,
};

static int ads7924_get_channels_config(struct i2c_client *client,
				       struct iio_dev *indio_dev)
{
	struct ads7924_data *priv = iio_priv(indio_dev);
	struct device *dev = priv->dev;
	struct fwnode_handle *node;
	int num_channels = 0;

	device_for_each_child_node(dev, node) {
		u32 pval;
		unsigned int channel;

		if (fwnode_property_read_u32(node, "reg", &pval)) {
			dev_err(dev, "invalid reg on %pfw\n", node);
			continue;
		}

		channel = pval;
		if (channel >= ADS7924_CHANNELS) {
			dev_err(dev, "invalid channel index %d on %pfw\n",
				channel, node);
			continue;
		}

		num_channels++;
	}

	if (!num_channels)
		return -EINVAL;

	return 0;
}

static int ads7924_set_conv_mode(struct ads7924_data *data, int mode)
{
	int ret;
	unsigned int mode_field;
	struct device *dev = data->dev;

	/*
	 * When switching between modes, be sure to first select the Awake mode
	 * and then switch to the desired mode. This procedure ensures the
	 * internal control logic is properly synchronized.
	 */
	if (mode != ADS7924_MODECNTRL_IDLE) {
		mode_field = FIELD_PREP(ADS7924_MODECNTRL_MODE_MASK,
					ADS7924_MODECNTRL_AWAKE);

		ret = regmap_update_bits(data->regmap, ADS7924_MODECNTRL_REG,
					 ADS7924_MODECNTRL_MODE_MASK,
					 mode_field);
		if (ret) {
			dev_err(dev, "failed to set awake mode (%pe)\n",
				ERR_PTR(ret));
			return ret;
		}
	}

	mode_field = FIELD_PREP(ADS7924_MODECNTRL_MODE_MASK, mode);

	ret = regmap_update_bits(data->regmap, ADS7924_MODECNTRL_REG,
				 ADS7924_MODECNTRL_MODE_MASK, mode_field);
	if (ret)
		dev_err(dev, "failed to set mode %d (%pe)\n", mode,
			ERR_PTR(ret));

	return ret;
}

static int ads7924_reset(struct iio_dev *indio_dev)
{
	struct ads7924_data *data = iio_priv(indio_dev);

	if (data->reset_gpio) {
		gpiod_set_value(data->reset_gpio, 1); /* Assert. */
		/* Educated guess: assert time not specified in datasheet... */
		mdelay(100);
		gpiod_set_value(data->reset_gpio, 0); /* Deassert. */
		return 0;
	}

	/*
	 * A write of 10101010 to this register will generate a
	 * software reset of the ADS7924.
	 */
	return regmap_write(data->regmap, ADS7924_RESET_REG, 0b10101010);
};

static void ads7924_reg_disable(void *data)
{
	regulator_disable(data);
}

static void ads7924_set_idle_mode(void *data)
{
	ads7924_set_conv_mode(data, ADS7924_MODECNTRL_IDLE);
}

static int ads7924_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct ads7924_data *data;
	struct device *dev = &client->dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return dev_err_probe(dev, -ENOMEM,
				     "failed to allocate iio device\n");

	data = iio_priv(indio_dev);

	data->dev = dev;

	/* Initialize the reset GPIO as output with an initial value of 0. */
	data->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(data->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(data->reset_gpio),
				     "failed to get request reset GPIO\n");

	mutex_init(&data->lock);

	indio_dev->name = "ads7924";
	indio_dev->modes = INDIO_DIRECT_MODE;

	indio_dev->channels = ads7924_channels;
	indio_dev->num_channels = ARRAY_SIZE(ads7924_channels);
	indio_dev->info = &ads7924_info;

	ret = ads7924_get_channels_config(client, indio_dev);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "failed to get channels configuration\n");

	data->regmap = devm_regmap_init_i2c(client, &ads7924_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "failed to init regmap\n");

	data->vref_reg = devm_regulator_get(dev, "vref");
	if (IS_ERR(data->vref_reg))
		return dev_err_probe(dev, PTR_ERR(data->vref_reg),
				     "failed to get vref regulator\n");

	ret = regulator_enable(data->vref_reg);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to enable regulator\n");

	ret = devm_add_action_or_reset(dev, ads7924_reg_disable, data->vref_reg);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to add regulator disable action\n");

	ret = ads7924_reset(indio_dev);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "failed to reset device\n");

	ret = ads7924_set_conv_mode(data, ADS7924_MODECNTRL_AUTO_SCAN);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to set conversion mode\n");

	ret = devm_add_action_or_reset(dev, ads7924_set_idle_mode, data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to add idle mode action\n");

	/* Use minimum signal acquire time. */
	ret = regmap_update_bits(data->regmap, ADS7924_ACQCONFIG_REG,
				 ADS7924_ACQTIME_MASK,
				 FIELD_PREP(ADS7924_ACQTIME_MASK, 0));
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "failed to configure signal acquire time\n");

	/* Disable power-up time. */
	ret = regmap_update_bits(data->regmap, ADS7924_PWRCONFIG_REG,
				 ADS7924_PWRUPTIME_MASK,
				 FIELD_PREP(ADS7924_PWRUPTIME_MASK, 0));
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "failed to configure power-up time\n");

	data->conv_invalid = true;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "failed to register IIO device\n");

	return 0;
}

static const struct i2c_device_id ads7924_id[] = {
	{ "ads7924", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ads7924_id);

static const struct of_device_id ads7924_of_match[] = {
	{ .compatible = "ti,ads7924", },
	{}
};
MODULE_DEVICE_TABLE(of, ads7924_of_match);

static struct i2c_driver ads7924_driver = {
	.driver = {
		.name = "ads7924",
		.of_match_table = ads7924_of_match,
	},
	.probe_new	= ads7924_probe,
	.id_table	= ads7924_id,
};

module_i2c_driver(ads7924_driver);

MODULE_AUTHOR("Hugo Villeneuve <hvilleneuve@dimonoff.com>");
MODULE_DESCRIPTION("Texas Instruments ADS7924 ADC I2C driver");
MODULE_LICENSE("GPL");
