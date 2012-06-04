/*
 * AD5760, AD5780, AD5781, AD5790, AD5791 Voltage Output Digital to Analog
 * Converter
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include "dac.h"
#include "ad5791.h"

static int ad5791_spi_write(struct spi_device *spi, u8 addr, u32 val)
{
	union {
		u32 d32;
		u8 d8[4];
	} data;

	data.d32 = cpu_to_be32(AD5791_CMD_WRITE |
			      AD5791_ADDR(addr) |
			      (val & AD5791_DAC_MASK));

	return spi_write(spi, &data.d8[1], 3);
}

static int ad5791_spi_read(struct spi_device *spi, u8 addr, u32 *val)
{
	union {
		u32 d32;
		u8 d8[4];
	} data[3];
	int ret;
	struct spi_message msg;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = &data[0].d8[1],
			.bits_per_word = 8,
			.len = 3,
			.cs_change = 1,
		}, {
			.tx_buf = &data[1].d8[1],
			.rx_buf = &data[2].d8[1],
			.bits_per_word = 8,
			.len = 3,
		},
	};

	data[0].d32 = cpu_to_be32(AD5791_CMD_READ |
			      AD5791_ADDR(addr));
	data[1].d32 = cpu_to_be32(AD5791_ADDR(AD5791_ADDR_NOOP));

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(spi, &msg);

	*val = be32_to_cpu(data[2].d32);

	return ret;
}

#define AD5791_CHAN(bits, shift) {			\
	.type = IIO_VOLTAGE,				\
	.output = 1,					\
	.indexed = 1,					\
	.address = AD5791_ADDR_DAC0,			\
	.channel = 0,					\
	.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |	\
		IIO_CHAN_INFO_SCALE_SHARED_BIT |	\
		IIO_CHAN_INFO_OFFSET_SHARED_BIT,	\
	.scan_type = IIO_ST('u', bits, 24, shift)	\
}

static const struct iio_chan_spec ad5791_channels[] = {
	[ID_AD5760] = AD5791_CHAN(16, 4),
	[ID_AD5780] = AD5791_CHAN(18, 2),
	[ID_AD5781] = AD5791_CHAN(18, 2),
	[ID_AD5791] = AD5791_CHAN(20, 0)
};

static ssize_t ad5791_read_powerdown_mode(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad5791_state *st = iio_priv(indio_dev);

	const char mode[][14] = {"6kohm_to_gnd", "three_state"};

	return sprintf(buf, "%s\n", mode[st->pwr_down_mode]);
}

static ssize_t ad5791_write_powerdown_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad5791_state *st = iio_priv(indio_dev);
	int ret;

	if (sysfs_streq(buf, "6kohm_to_gnd"))
		st->pwr_down_mode = AD5791_DAC_PWRDN_6K;
	else if (sysfs_streq(buf, "three_state"))
		st->pwr_down_mode = AD5791_DAC_PWRDN_3STATE;
	else
		ret = -EINVAL;

	return ret ? ret : len;
}

static ssize_t ad5791_read_dac_powerdown(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad5791_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", st->pwr_down);
}

static ssize_t ad5791_write_dac_powerdown(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	long readin;
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad5791_state *st = iio_priv(indio_dev);

	ret = strict_strtol(buf, 10, &readin);
	if (ret)
		return ret;

	if (readin == 0) {
		st->pwr_down = false;
		st->ctrl &= ~(AD5791_CTRL_OPGND | AD5791_CTRL_DACTRI);
	} else if (readin == 1) {
		st->pwr_down = true;
		if (st->pwr_down_mode == AD5791_DAC_PWRDN_6K)
			st->ctrl |= AD5791_CTRL_OPGND;
		else if (st->pwr_down_mode == AD5791_DAC_PWRDN_3STATE)
			st->ctrl |= AD5791_CTRL_DACTRI;
	} else
		ret = -EINVAL;

	ret = ad5791_spi_write(st->spi, AD5791_ADDR_CTRL, st->ctrl);

	return ret ? ret : len;
}

static IIO_DEVICE_ATTR(out_voltage_powerdown_mode, S_IRUGO |
			S_IWUSR, ad5791_read_powerdown_mode,
			ad5791_write_powerdown_mode, 0);

static IIO_CONST_ATTR(out_voltage_powerdown_mode_available,
			"6kohm_to_gnd three_state");

#define IIO_DEV_ATTR_DAC_POWERDOWN(_num, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(out_voltage##_num##_powerdown,			\
			S_IRUGO | S_IWUSR, _show, _store, _addr)

static IIO_DEV_ATTR_DAC_POWERDOWN(0, ad5791_read_dac_powerdown,
				   ad5791_write_dac_powerdown, 0);

static struct attribute *ad5791_attributes[] = {
	&iio_dev_attr_out_voltage0_powerdown.dev_attr.attr,
	&iio_dev_attr_out_voltage_powerdown_mode.dev_attr.attr,
	&iio_const_attr_out_voltage_powerdown_mode_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad5791_attribute_group = {
	.attrs = ad5791_attributes,
};

static int ad5791_get_lin_comp(unsigned int span)
{
	if (span <= 10000)
		return AD5791_LINCOMP_0_10;
	else if (span <= 12000)
		return AD5791_LINCOMP_10_12;
	else if (span <= 16000)
		return AD5791_LINCOMP_12_16;
	else if (span <= 19000)
		return AD5791_LINCOMP_16_19;
	else
		return AD5791_LINCOMP_19_20;
}

static int ad5780_get_lin_comp(unsigned int span)
{
	if (span <= 10000)
		return AD5780_LINCOMP_0_10;
	else
		return AD5780_LINCOMP_10_20;
}
static const struct ad5791_chip_info ad5791_chip_info_tbl[] = {
	[ID_AD5760] = {
		.get_lin_comp = ad5780_get_lin_comp,
	},
	[ID_AD5780] = {
		.get_lin_comp = ad5780_get_lin_comp,
	},
	[ID_AD5781] = {
		.get_lin_comp = ad5791_get_lin_comp,
	},
	[ID_AD5791] = {
		.get_lin_comp = ad5791_get_lin_comp,
	},
};

static int ad5791_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad5791_state *st = iio_priv(indio_dev);
	u64 val64;
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = ad5791_spi_read(st->spi, chan->address, val);
		if (ret)
			return ret;
		*val &= AD5791_DAC_MASK;
		*val >>= chan->scan_type.shift;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = (((u64)st->vref_mv) * 1000000ULL) >> chan->scan_type.realbits;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OFFSET:
		val64 = (((u64)st->vref_neg_mv) << chan->scan_type.realbits);
		do_div(val64, st->vref_mv);
		*val = -val64;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}

};


static int ad5791_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long mask)
{
	struct ad5791_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		val &= AD5791_RES_MASK(chan->scan_type.realbits);
		val <<= chan->scan_type.shift;

		return ad5791_spi_write(st->spi, chan->address, val);

	default:
		return -EINVAL;
	}
}

static const struct iio_info ad5791_info = {
	.read_raw = &ad5791_read_raw,
	.write_raw = &ad5791_write_raw,
	.attrs = &ad5791_attribute_group,
	.driver_module = THIS_MODULE,
};

static int __devinit ad5791_probe(struct spi_device *spi)
{
	struct ad5791_platform_data *pdata = spi->dev.platform_data;
	struct iio_dev *indio_dev;
	struct ad5791_state *st;
	int ret, pos_voltage_uv = 0, neg_voltage_uv = 0;

	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	st = iio_priv(indio_dev);
	st->reg_vdd = regulator_get(&spi->dev, "vdd");
	if (!IS_ERR(st->reg_vdd)) {
		ret = regulator_enable(st->reg_vdd);
		if (ret)
			goto error_put_reg_pos;

		pos_voltage_uv = regulator_get_voltage(st->reg_vdd);
	}

	st->reg_vss = regulator_get(&spi->dev, "vss");
	if (!IS_ERR(st->reg_vss)) {
		ret = regulator_enable(st->reg_vss);
		if (ret)
			goto error_put_reg_neg;

		neg_voltage_uv = regulator_get_voltage(st->reg_vss);
	}

	st->pwr_down = true;
	st->spi = spi;

	if (!IS_ERR(st->reg_vss) && !IS_ERR(st->reg_vdd)) {
		st->vref_mv = (pos_voltage_uv + neg_voltage_uv) / 1000;
		st->vref_neg_mv = neg_voltage_uv / 1000;
	} else if (pdata) {
		st->vref_mv = pdata->vref_pos_mv + pdata->vref_neg_mv;
		st->vref_neg_mv = pdata->vref_neg_mv;
	} else {
		dev_warn(&spi->dev, "reference voltage unspecified\n");
	}

	ret = ad5791_spi_write(spi, AD5791_ADDR_SW_CTRL, AD5791_SWCTRL_RESET);
	if (ret)
		goto error_disable_reg_neg;

	st->chip_info =	&ad5791_chip_info_tbl[spi_get_device_id(spi)
					      ->driver_data];


	st->ctrl = AD5761_CTRL_LINCOMP(st->chip_info->get_lin_comp(st->vref_mv))
		  | ((pdata && pdata->use_rbuf_gain2) ? 0 : AD5791_CTRL_RBUF) |
		  AD5791_CTRL_BIN2SC;

	ret = ad5791_spi_write(spi, AD5791_ADDR_CTRL, st->ctrl |
		AD5791_CTRL_OPGND | AD5791_CTRL_DACTRI);
	if (ret)
		goto error_disable_reg_neg;

	spi_set_drvdata(spi, indio_dev);
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &ad5791_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels
		= &ad5791_channels[spi_get_device_id(spi)->driver_data];
	indio_dev->num_channels = 1;
	indio_dev->name = spi_get_device_id(st->spi)->name;
	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_disable_reg_neg;

	return 0;

error_disable_reg_neg:
	if (!IS_ERR(st->reg_vss))
		regulator_disable(st->reg_vss);
error_put_reg_neg:
	if (!IS_ERR(st->reg_vss))
		regulator_put(st->reg_vss);

	if (!IS_ERR(st->reg_vdd))
		regulator_disable(st->reg_vdd);
error_put_reg_pos:
	if (!IS_ERR(st->reg_vdd))
		regulator_put(st->reg_vdd);
	iio_device_free(indio_dev);
error_ret:

	return ret;
}

static int __devexit ad5791_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad5791_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (!IS_ERR(st->reg_vdd)) {
		regulator_disable(st->reg_vdd);
		regulator_put(st->reg_vdd);
	}

	if (!IS_ERR(st->reg_vss)) {
		regulator_disable(st->reg_vss);
		regulator_put(st->reg_vss);
	}
	iio_device_free(indio_dev);

	return 0;
}

static const struct spi_device_id ad5791_id[] = {
	{"ad5760", ID_AD5760},
	{"ad5780", ID_AD5780},
	{"ad5781", ID_AD5781},
	{"ad5790", ID_AD5791},
	{"ad5791", ID_AD5791},
	{}
};
MODULE_DEVICE_TABLE(spi, ad5791_id);

static struct spi_driver ad5791_driver = {
	.driver = {
		   .name = "ad5791",
		   .owner = THIS_MODULE,
		   },
	.probe = ad5791_probe,
	.remove = __devexit_p(ad5791_remove),
	.id_table = ad5791_id,
};
module_spi_driver(ad5791_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD5760/AD5780/AD5781/AD5790/AD5791 DAC");
MODULE_LICENSE("GPL v2");
