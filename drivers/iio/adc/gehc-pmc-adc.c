// SPDX-License-Identifier: GPL-2.0
/*
 * The GE HealthCare PMC ADC is a 16-Channel (Voltage and current), 16-Bit
 * ADC with an I2C Interface.
 *
 * Copyright (C) 2024, GE HealthCare
 *
 * Authors:
 * Herve Codina <herve.codina@bootlin.com>
 */
#include <dt-bindings/iio/adc/gehc,pmc-adc.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

struct pmc_adc {
	struct i2c_client *client;
};

#define PMC_ADC_CMD_REQUEST_PROTOCOL_VERSION	0x01
#define PMC_ADC_CMD_READ_VOLTAGE(_ch)		(0x10 | (_ch))
#define PMC_ADC_CMD_READ_CURRENT(_ch)		(0x20 | (_ch))

#define PMC_ADC_VOLTAGE_CHANNEL(_ch, _ds_name) {			\
	.type = IIO_VOLTAGE,						\
	.indexed = 1,							\
	.channel = (_ch),						\
	.address = PMC_ADC_CMD_READ_VOLTAGE(_ch),			\
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),		\
	.datasheet_name = (_ds_name),					\
}

#define PMC_ADC_CURRENT_CHANNEL(_ch, _ds_name) {			\
	.type = IIO_CURRENT,						\
	.indexed = 1,							\
	.channel = (_ch),						\
	.address = PMC_ADC_CMD_READ_CURRENT(_ch),			\
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),		\
	.datasheet_name = (_ds_name),					\
}

static const struct iio_chan_spec pmc_adc_channels[] = {
	PMC_ADC_VOLTAGE_CHANNEL(0, "CH0_V"),
	PMC_ADC_VOLTAGE_CHANNEL(1, "CH1_V"),
	PMC_ADC_VOLTAGE_CHANNEL(2, "CH2_V"),
	PMC_ADC_VOLTAGE_CHANNEL(3, "CH3_V"),
	PMC_ADC_VOLTAGE_CHANNEL(4, "CH4_V"),
	PMC_ADC_VOLTAGE_CHANNEL(5, "CH5_V"),
	PMC_ADC_VOLTAGE_CHANNEL(6, "CH6_V"),
	PMC_ADC_VOLTAGE_CHANNEL(7, "CH7_V"),
	PMC_ADC_VOLTAGE_CHANNEL(8, "CH8_V"),
	PMC_ADC_VOLTAGE_CHANNEL(9, "CH9_V"),
	PMC_ADC_VOLTAGE_CHANNEL(10, "CH10_V"),
	PMC_ADC_VOLTAGE_CHANNEL(11, "CH11_V"),
	PMC_ADC_VOLTAGE_CHANNEL(12, "CH12_V"),
	PMC_ADC_VOLTAGE_CHANNEL(13, "CH13_V"),
	PMC_ADC_VOLTAGE_CHANNEL(14, "CH14_V"),
	PMC_ADC_VOLTAGE_CHANNEL(15, "CH15_V"),

	PMC_ADC_CURRENT_CHANNEL(0, "CH0_I"),
	PMC_ADC_CURRENT_CHANNEL(1, "CH1_I"),
	PMC_ADC_CURRENT_CHANNEL(2, "CH2_I"),
	PMC_ADC_CURRENT_CHANNEL(3, "CH3_I"),
	PMC_ADC_CURRENT_CHANNEL(4, "CH4_I"),
	PMC_ADC_CURRENT_CHANNEL(5, "CH5_I"),
	PMC_ADC_CURRENT_CHANNEL(6, "CH6_I"),
	PMC_ADC_CURRENT_CHANNEL(7, "CH7_I"),
	PMC_ADC_CURRENT_CHANNEL(8, "CH8_I"),
	PMC_ADC_CURRENT_CHANNEL(9, "CH9_I"),
	PMC_ADC_CURRENT_CHANNEL(10, "CH10_I"),
	PMC_ADC_CURRENT_CHANNEL(11, "CH11_I"),
	PMC_ADC_CURRENT_CHANNEL(12, "CH12_I"),
	PMC_ADC_CURRENT_CHANNEL(13, "CH13_I"),
	PMC_ADC_CURRENT_CHANNEL(14, "CH14_I"),
	PMC_ADC_CURRENT_CHANNEL(15, "CH15_I"),
};

static int pmc_adc_read_raw_ch(struct pmc_adc *pmc_adc, u8 cmd, int *val)
{
	s32 ret;

	ret = i2c_smbus_read_word_swapped(pmc_adc->client, cmd);
	if (ret < 0) {
		dev_err(&pmc_adc->client->dev, "i2c read word failed (%d)\n", ret);
		return ret;
	}

	*val = sign_extend32(ret, 15);
	return 0;
}

static int pmc_adc_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct pmc_adc *pmc_adc = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		/* Values are directly read in mV or mA */
		ret = pmc_adc_read_raw_ch(pmc_adc, chan->address, val);
		if (ret)
			return ret;
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int pmc_adc_fwnode_xlate(struct iio_dev *indio_dev,
				const struct fwnode_reference_args *iiospec)
{
	enum iio_chan_type expected_type;
	unsigned int i;

	/*
	 * args[0]: Acquisition type (i.e. voltage or current)
	 * args[1]: PMC ADC channel number
	 */
	if (iiospec->nargs != 2)
		return -EINVAL;

	switch (iiospec->args[0]) {
	case GEHC_PMC_ADC_VOLTAGE:
		expected_type = IIO_VOLTAGE;
		break;
	case GEHC_PMC_ADC_CURRENT:
		expected_type = IIO_CURRENT;
		break;
	default:
		dev_err(&indio_dev->dev, "Invalid channel type %llu\n",
			iiospec->args[0]);
		return -EINVAL;
	}

	for (i = 0; i < indio_dev->num_channels; i++)
		if (indio_dev->channels[i].type == expected_type &&
		    indio_dev->channels[i].channel == iiospec->args[1])
			return i;

	dev_err(&indio_dev->dev, "Invalid channel type %llu number %llu\n",
		iiospec->args[0], iiospec->args[1]);
	return -EINVAL;
}

static const struct iio_info pmc_adc_info = {
	.read_raw = pmc_adc_read_raw,
	.fwnode_xlate = pmc_adc_fwnode_xlate,
};

static const char *const pmc_adc_regulator_names[] = {
	"vdd",
	"vdda",
	"vddio",
	"vref",
};

static int pmc_adc_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct pmc_adc *pmc_adc;
	struct clk *clk;
	s32 val;
	int ret;

	ret = devm_regulator_bulk_get_enable(&client->dev, ARRAY_SIZE(pmc_adc_regulator_names),
					     pmc_adc_regulator_names);
	if (ret)
		return dev_err_probe(&client->dev, ret, "Failed to get regulators\n");

	clk = devm_clk_get_optional_enabled(&client->dev, "osc");
	if (IS_ERR(clk))
		return dev_err_probe(&client->dev, PTR_ERR(clk), "Failed to get osc clock\n");

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*pmc_adc));
	if (!indio_dev)
		return -ENOMEM;

	pmc_adc = iio_priv(indio_dev);
	pmc_adc->client = client;

	val = i2c_smbus_read_byte_data(pmc_adc->client, PMC_ADC_CMD_REQUEST_PROTOCOL_VERSION);
	if (val < 0)
		return dev_err_probe(&client->dev, val, "Failed to get protocol version\n");

	if (val != 0x01)
		return dev_err_probe(&client->dev, -EINVAL,
				     "Unsupported protocol version 0x%02x\n", val);

	indio_dev->name = "pmc_adc";
	indio_dev->info = &pmc_adc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = pmc_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(pmc_adc_channels);

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct of_device_id pmc_adc_of_match[] = {
	{ .compatible = "gehc,pmc-adc"},
	{ }
};
MODULE_DEVICE_TABLE(of, pmc_adc_of_match);

static const struct i2c_device_id pmc_adc_id_table[] = {
	{ "pmc-adc" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pmc_adc_id_table);

static struct i2c_driver pmc_adc_i2c_driver = {
	.driver  = {
		.name = "pmc-adc",
		.of_match_table = pmc_adc_of_match,
	},
	.id_table = pmc_adc_id_table,
	.probe  = pmc_adc_probe,
};

module_i2c_driver(pmc_adc_i2c_driver);

MODULE_AUTHOR("Herve Codina <herve.codina@bootlin.com>");
MODULE_DESCRIPTION("GE HealthCare PMC ADC driver");
MODULE_LICENSE("GPL");
