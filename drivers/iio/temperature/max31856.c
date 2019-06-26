// SPDX-License-Identifier: GPL-2.0
/* max31856.c
 *
 * Maxim MAX31856 thermocouple sensor driver
 *
 * Copyright (C) 2018-2019 Rockwell Collins
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <dt-bindings/iio/temperature/thermocouple.h>
/*
 * The MSB of the register value determines whether the following byte will
 * be written or read. If it is 0, one or more byte reads will follow.
 */
#define MAX31856_RD_WR_BIT         BIT(7)

#define MAX31856_CR0_AUTOCONVERT   BIT(7)
#define MAX31856_CR0_1SHOT         BIT(6)
#define MAX31856_CR0_OCFAULT       BIT(4)
#define MAX31856_CR0_OCFAULT_MASK  GENMASK(5, 4)
#define MAX31856_TC_TYPE_MASK      GENMASK(3, 0)
#define MAX31856_FAULT_OVUV        BIT(1)
#define MAX31856_FAULT_OPEN        BIT(0)

/* The MAX31856 registers */
#define MAX31856_CR0_REG           0x00
#define MAX31856_CR1_REG           0x01
#define MAX31856_MASK_REG          0x02
#define MAX31856_CJHF_REG          0x03
#define MAX31856_CJLF_REG          0x04
#define MAX31856_LTHFTH_REG        0x05
#define MAX31856_LTHFTL_REG        0x06
#define MAX31856_LTLFTH_REG        0x07
#define MAX31856_LTLFTL_REG        0x08
#define MAX31856_CJTO_REG          0x09
#define MAX31856_CJTH_REG          0x0A
#define MAX31856_CJTL_REG          0x0B
#define MAX31856_LTCBH_REG         0x0C
#define MAX31856_LTCBM_REG         0x0D
#define MAX31856_LTCBL_REG         0x0E
#define MAX31856_SR_REG            0x0F

static const struct iio_chan_spec max31856_channels[] = {
	{	/* Thermocouple Temperature */
		.type = IIO_TEMP,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
	},
	{	/* Cold Junction Temperature */
		.type = IIO_TEMP,
		.channel2 = IIO_MOD_TEMP_AMBIENT,
		.modified = 1,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
	},
};

struct max31856_data {
	struct spi_device *spi;
	u32 thermocouple_type;
};

static int max31856_read(struct max31856_data *data, u8 reg,
			 u8 val[], unsigned int read_size)
{
	return spi_write_then_read(data->spi, &reg, 1, val, read_size);
}

static int max31856_write(struct max31856_data *data, u8 reg,
			  unsigned int val)
{
	u8 buf[2];

	buf[0] = reg | (MAX31856_RD_WR_BIT);
	buf[1] = val;

	return spi_write(data->spi, buf, 2);
}

static int max31856_init(struct max31856_data *data)
{
	int ret;
	u8 reg_cr0_val, reg_cr1_val;

	/* Start by changing to Off mode before making changes as
	 * some settings are recommended to be set only when the device
	 * is off
	 */
	ret = max31856_read(data, MAX31856_CR0_REG, &reg_cr0_val, 1);
	if (ret)
		return ret;

	reg_cr0_val &= ~MAX31856_CR0_AUTOCONVERT;
	ret = max31856_write(data, MAX31856_CR0_REG, reg_cr0_val);
	if (ret)
		return ret;

	/* Set thermocouple type based on dts property */
	ret = max31856_read(data, MAX31856_CR1_REG, &reg_cr1_val, 1);
	if (ret)
		return ret;

	reg_cr1_val &= ~MAX31856_TC_TYPE_MASK;
	reg_cr1_val |= data->thermocouple_type;
	ret = max31856_write(data, MAX31856_CR1_REG, reg_cr1_val);
	if (ret)
		return ret;

	/*
	 * Enable Open circuit fault detection
	 * Read datasheet for more information: Table 4.
	 * Value 01 means : Enabled (Once every 16 conversions)
	 */
	reg_cr0_val &= ~MAX31856_CR0_OCFAULT_MASK;
	reg_cr0_val |= MAX31856_CR0_OCFAULT;

	/* Set Auto Conversion Mode */
	reg_cr0_val &= ~MAX31856_CR0_1SHOT;
	reg_cr0_val |= MAX31856_CR0_AUTOCONVERT;

	return max31856_write(data, MAX31856_CR0_REG, reg_cr0_val);
}

static int max31856_thermocouple_read(struct max31856_data *data,
				      struct iio_chan_spec const *chan,
				      int *val)
{
	int ret, offset_cjto;
	u8 reg_val[3];

	switch (chan->channel2) {
	case IIO_NO_MOD:
		/*
		 * Multibyte Read
		 * MAX31856_LTCBH_REG, MAX31856_LTCBM_REG, MAX31856_LTCBL_REG
		 */
		ret = max31856_read(data, MAX31856_LTCBH_REG, reg_val, 3);
		if (ret)
			return ret;
		/* Skip last 5 dead bits of LTCBL */
		*val = (reg_val[0] << 16 | reg_val[1] << 8 | reg_val[2]) >> 5;
		/* Check 7th bit of LTCBH reg. value for sign*/
		if (reg_val[0] & 0x80)
			*val -= 0x80000;
		break;

	case IIO_MOD_TEMP_AMBIENT:
		/*
		 * Multibyte Read
		 * MAX31856_CJTO_REG, MAX31856_CJTH_REG, MAX31856_CJTL_REG
		 */
		ret = max31856_read(data, MAX31856_CJTO_REG, reg_val, 3);
		if (ret)
			return ret;
		/* Get Cold Junction Temp. offset register value */
		offset_cjto = reg_val[0];
		/* Get CJTH and CJTL value and skip last 2 dead bits of CJTL */
		*val = (reg_val[1] << 8 | reg_val[2]) >> 2;
		/* As per datasheet add offset into CJTH and CJTL */
		*val += offset_cjto;
		/* Check 7th bit of CJTH reg. value for sign */
		if (reg_val[1] & 0x80)
			*val -= 0x4000;
		break;

	default:
		return -EINVAL;
	}

	ret = max31856_read(data, MAX31856_SR_REG, reg_val, 1);
	if (ret)
		return ret;
	/* Check for over/under voltage or open circuit fault */
	if (reg_val[0] & (MAX31856_FAULT_OVUV | MAX31856_FAULT_OPEN))
		return -EIO;

	return ret;
}

static int max31856_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct max31856_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = max31856_thermocouple_read(data, chan, val);
		if (ret)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->channel2) {
		case IIO_MOD_TEMP_AMBIENT:
			/* Cold junction Temp. Data resolution is 0.015625 */
			*val = 15;
			*val2 = 625000; /* 1000 * 0.015625 */
			ret = IIO_VAL_INT_PLUS_MICRO;
			break;
		default:
			/* Thermocouple Temp. Data resolution is 0.0078125 */
			*val = 7;
			*val2 = 812500; /* 1000 * 0.0078125) */
			return IIO_VAL_INT_PLUS_MICRO;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static ssize_t show_fault(struct device *dev, u8 faultbit, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct max31856_data *data = iio_priv(indio_dev);
	u8 reg_val;
	int ret;
	bool fault;

	ret = max31856_read(data, MAX31856_SR_REG, &reg_val, 1);
	if (ret)
		return ret;

	fault = reg_val & faultbit;

	return sprintf(buf, "%d\n", fault);
}

static ssize_t show_fault_ovuv(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	return show_fault(dev, MAX31856_FAULT_OVUV, buf);
}

static ssize_t show_fault_oc(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	return show_fault(dev, MAX31856_FAULT_OPEN, buf);
}

static IIO_DEVICE_ATTR(fault_ovuv, 0444, show_fault_ovuv, NULL, 0);
static IIO_DEVICE_ATTR(fault_oc, 0444, show_fault_oc, NULL, 0);

static struct attribute *max31856_attributes[] = {
	&iio_dev_attr_fault_ovuv.dev_attr.attr,
	&iio_dev_attr_fault_oc.dev_attr.attr,
	NULL,
};

static const struct attribute_group max31856_group = {
	.attrs = max31856_attributes,
};

static const struct iio_info max31856_info = {
	.read_raw = max31856_read_raw,
	.attrs = &max31856_group,
};

static int max31856_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct iio_dev *indio_dev;
	struct max31856_data *data;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->spi = spi;

	spi_set_drvdata(spi, indio_dev);

	indio_dev->info = &max31856_info;
	indio_dev->name = id->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = max31856_channels;
	indio_dev->num_channels = ARRAY_SIZE(max31856_channels);

	ret = of_property_read_u32(spi->dev.of_node, "thermocouple-type",
				   &data->thermocouple_type);

	if (ret) {
		dev_info(&spi->dev,
			 "Could not read thermocouple type DT property, configuring as a K-Type\n");
		data->thermocouple_type = THERMOCOUPLE_TYPE_K;
	}

	/*
	 * no need to translate values as the supported types
	 * have the same value as the #defines
	 */
	switch (data->thermocouple_type) {
	case THERMOCOUPLE_TYPE_B:
	case THERMOCOUPLE_TYPE_E:
	case THERMOCOUPLE_TYPE_J:
	case THERMOCOUPLE_TYPE_K:
	case THERMOCOUPLE_TYPE_N:
	case THERMOCOUPLE_TYPE_R:
	case THERMOCOUPLE_TYPE_S:
	case THERMOCOUPLE_TYPE_T:
		break;
	default:
		dev_err(&spi->dev,
			"error: thermocouple-type %u not supported by max31856\n"
			, data->thermocouple_type);
		return -EINVAL;
	}

	ret = max31856_init(data);
	if (ret) {
		dev_err(&spi->dev, "error: Failed to configure max31856\n");
		return ret;
	}

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id max31856_id[] = {
	{ "max31856", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, max31856_id);

static const struct of_device_id max31856_of_match[] = {
	{ .compatible = "maxim,max31856" },
	{ }
};
MODULE_DEVICE_TABLE(of, max31856_of_match);

static struct spi_driver max31856_driver = {
	.driver = {
		.name = "max31856",
		.of_match_table = max31856_of_match,
	},
	.probe = max31856_probe,
	.id_table = max31856_id,
};
module_spi_driver(max31856_driver);

MODULE_AUTHOR("Paresh Chaudhary <paresh.chaudhary@rockwellcollins.com>");
MODULE_AUTHOR("Patrick Havelange <patrick.havelange@essensium.com>");
MODULE_DESCRIPTION("Maxim MAX31856 thermocouple sensor driver");
MODULE_LICENSE("GPL");
