// SPDX-License-Identifier: GPL-2.0
/*
 * IIO driver for Maxim MAX34409/34408 ADC, 4-Channels/2-Channels, 8bits, I2C
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/MAX34408-MAX34409.pdf
 *
 * TODO: ALERT interrupt, Overcurrent delay, Shutdown delay
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/regmap.h>

#include <linux/iio/iio.h>
#include <linux/iio/types.h>

#define MAX34408_STATUS_REG		0x0
#define MAX34408_CONTROL_REG		0x1
#define MAX34408_OCDELAY_REG		0x2
#define MAX34408_SDDELAY_REG		0x3

#define MAX34408_ADC1_REG		0x4
#define MAX34408_ADC2_REG		0x5
/* ADC3 & ADC4 always returns 0x0 on 34408 */
#define MAX34409_ADC3_REG		0x6
#define MAX34409_ADC4_REG		0x7

#define MAX34408_OCT1_REG		0x8
#define MAX34408_OCT2_REG		0x9
#define MAX34409_OCT3_REG		0xA
#define MAX34409_OCT4_REG		0xB

#define MAX34408_DID_REG		0xC
#define MAX34408_DCYY_REG		0xD
#define MAX34408_DCWW_REG		0xE

/* Bit masks for status register */
#define MAX34408_STATUS_OC_MSK		GENMASK(1, 0)
#define MAX34409_STATUS_OC_MSK		GENMASK(3, 0)
#define MAX34408_STATUS_SHTDN		BIT(4)
#define MAX34408_STATUS_ENA		BIT(5)

/* Bit masks for control register */
#define MAX34408_CONTROL_AVG0		BIT(0)
#define MAX34408_CONTROL_AVG1		BIT(1)
#define MAX34408_CONTROL_AVG2		BIT(2)
#define MAX34408_CONTROL_ALERT		BIT(3)

#define MAX34408_DEFAULT_AVG		0x4

/* Bit masks for over current delay */
#define MAX34408_OCDELAY_OCD_MSK	GENMASK(6, 0)
#define MAX34408_OCDELAY_RESET		BIT(7)

/* Bit masks for shutdown delay */
#define MAX34408_SDDELAY_SHD_MSK	GENMASK(6, 0)
#define MAX34408_SDDELAY_RESET		BIT(7)

#define MAX34408_DEFAULT_RSENSE		1000

/**
 * struct max34408_data - max34408/max34409 specific data.
 * @regmap:	device register map.
 * @dev:	max34408 device.
 * @lock:	lock for protecting access to device hardware registers, mostly
 *		for read modify write cycles for control registers.
 * @input_rsense:	Rsense values in uOhm, will be overwritten by
 *			values from channel nodes.
 */
struct max34408_data {
	struct regmap *regmap;
	struct device *dev;
	struct mutex lock;
	u32 input_rsense[4];
};

static const struct regmap_config max34408_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= MAX34408_DCWW_REG,
};

struct max34408_adc_model_data {
	const char *model_name;
	const struct iio_chan_spec *channels;
	const int num_channels;
};

#define MAX34008_CHANNEL(_index, _address)			\
	{							\
		.type = IIO_CURRENT,				\
		.info_mask_separate	= BIT(IIO_CHAN_INFO_RAW) | \
					  BIT(IIO_CHAN_INFO_SCALE) | \
					  BIT(IIO_CHAN_INFO_OFFSET), \
		.channel = (_index),				\
		.address = (_address),				\
		.indexed = 1,					\
	}

static const struct iio_chan_spec max34408_channels[] = {
	MAX34008_CHANNEL(0, MAX34408_ADC1_REG),
	MAX34008_CHANNEL(1, MAX34408_ADC2_REG),
};

static const struct iio_chan_spec max34409_channels[] = {
	MAX34008_CHANNEL(0, MAX34408_ADC1_REG),
	MAX34008_CHANNEL(1, MAX34408_ADC2_REG),
	MAX34008_CHANNEL(2, MAX34409_ADC3_REG),
	MAX34008_CHANNEL(3, MAX34409_ADC4_REG),
};

static int max34408_read_adc_avg(struct max34408_data *max34408,
				 const struct iio_chan_spec *chan, int *val)
{
	unsigned int ctrl;
	int rc;

	guard(mutex)(&max34408->lock);
	rc = regmap_read(max34408->regmap, MAX34408_CONTROL_REG, (u32 *)&ctrl);
	if (rc)
		return rc;

	/* set averaging (0b100) default values*/
	rc = regmap_write(max34408->regmap, MAX34408_CONTROL_REG,
			  MAX34408_DEFAULT_AVG);
	if (rc) {
		dev_err(max34408->dev,
			"Error (%d) writing control register\n", rc);
		return rc;
	}

	rc = regmap_read(max34408->regmap, chan->address, val);
	if (rc)
		return rc;

	/* back to old values */
	rc = regmap_write(max34408->regmap, MAX34408_CONTROL_REG, ctrl);
	if (rc)
		dev_err(max34408->dev,
			"Error (%d) writing control register\n", rc);

	return rc;
}

static int max34408_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct max34408_data *max34408 = iio_priv(indio_dev);
	int rc;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		rc = max34408_read_adc_avg(max34408, chan, val);
		if (rc)
			return rc;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/*
		 * calculate current for 8bit ADC with Rsense
		 * value.
		 * 10 mV * 1000 / Rsense uOhm = max current
		 * (max current * adc val * 1000) / (2^8 - 1) mA
		 */
		*val = 10000 / max34408->input_rsense[chan->channel];
		*val2 = 8;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static const struct iio_info max34408_info = {
	.read_raw	= max34408_read_raw,
};

static const struct max34408_adc_model_data max34408_model_data = {
	.model_name = "max34408",
	.channels = max34408_channels,
	.num_channels = 2,
};

static const struct max34408_adc_model_data max34409_model_data = {
	.model_name = "max34409",
	.channels = max34409_channels,
	.num_channels = 4,
};

static int max34408_probe(struct i2c_client *client)
{
	const struct max34408_adc_model_data *model_data;
	struct device *dev = &client->dev;
	struct max34408_data *max34408;
	struct fwnode_handle *node;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int rc, i = 0;

	model_data = i2c_get_match_data(client);
	if (!model_data)
		return -EINVAL;

	regmap = devm_regmap_init_i2c(client, &max34408_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err_probe(dev, PTR_ERR(regmap),
			      "regmap_init failed\n");
		return PTR_ERR(regmap);
	}

	indio_dev = devm_iio_device_alloc(dev, sizeof(*max34408));
	if (!indio_dev)
		return -ENOMEM;

	max34408 = iio_priv(indio_dev);
	max34408->regmap = regmap;
	max34408->dev = dev;
	mutex_init(&max34408->lock);

	device_for_each_child_node(dev, node) {
		fwnode_property_read_u32(node, "maxim,rsense-val-micro-ohms",
					 &max34408->input_rsense[i]);
		i++;
	}

	/* disable ALERT and averaging */
	rc = regmap_write(max34408->regmap, MAX34408_CONTROL_REG, 0x0);
	if (rc)
		return rc;

	indio_dev->channels = model_data->channels;
	indio_dev->num_channels = model_data->num_channels;
	indio_dev->name = model_data->model_name;

	indio_dev->info = &max34408_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id max34408_of_match[] = {
	{
		.compatible = "maxim,max34408",
		.data = &max34408_model_data,
	},
	{
		.compatible = "maxim,max34409",
		.data = &max34409_model_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, max34408_of_match);

static const struct i2c_device_id max34408_id[] = {
	{ "max34408", (kernel_ulong_t)&max34408_model_data },
	{ "max34409", (kernel_ulong_t)&max34409_model_data },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max34408_id);

static struct i2c_driver max34408_driver = {
	.driver = {
		.name   = "max34408",
		.of_match_table = max34408_of_match,
	},
	.probe = max34408_probe,
	.id_table = max34408_id,
};
module_i2c_driver(max34408_driver);

MODULE_AUTHOR("Ivan Mikhaylov <fr0st61te@gmail.com>");
MODULE_DESCRIPTION("Maxim MAX34408/34409 ADC driver");
MODULE_LICENSE("GPL");
