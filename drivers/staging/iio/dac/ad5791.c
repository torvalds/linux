/*
 * AD5760, AD5780, AD5781, AD5791 Voltage Output Digital to Analog Converter
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/regulator/consumer.h>

#include "../iio.h"
#include "../sysfs.h"
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

static ssize_t ad5791_write_dac(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5791_state *st = iio_dev_get_devdata(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	long readin;
	int ret;

	ret = strict_strtol(buf, 10, &readin);
	if (ret)
		return ret;

	readin += (1 << (st->chip_info->bits - 1));
	readin &= AD5791_RES_MASK(st->chip_info->bits);
	readin <<= st->chip_info->left_shift;

	ret = ad5791_spi_write(st->spi, this_attr->address, readin);
	return ret ? ret : len;
}

static ssize_t ad5791_read_dac(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5791_state *st = iio_dev_get_devdata(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	int val;

	ret = ad5791_spi_read(st->spi, this_attr->address, &val);
	if (ret)
		return ret;

	val &= AD5791_DAC_MASK;
	val >>= st->chip_info->left_shift;
	val -= (1 << (st->chip_info->bits - 1));

	return sprintf(buf, "%d\n", val);
}

static ssize_t ad5791_read_powerdown_mode(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5791_state *st = iio_dev_get_devdata(indio_dev);

	const char mode[][14] = {"6kohm_to_gnd", "three_state"};

	return sprintf(buf, "%s\n", mode[st->pwr_down_mode]);
}

static ssize_t ad5791_write_powerdown_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5791_state *st = iio_dev_get_devdata(indio_dev);
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
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5791_state *st = iio_dev_get_devdata(indio_dev);

	return sprintf(buf, "%d\n", st->pwr_down);
}

static ssize_t ad5791_write_dac_powerdown(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	long readin;
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5791_state *st = iio_dev_get_devdata(indio_dev);

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

static ssize_t ad5791_show_scale(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5791_state *st = iio_dev_get_devdata(indio_dev);
	/* Corresponds to Vref / 2^(bits) */
	unsigned int scale_uv = (st->vref_mv * 1000) >> st->chip_info->bits;

	return sprintf(buf, "%d.%03d\n", scale_uv / 1000, scale_uv % 1000);
}
static IIO_DEVICE_ATTR(out_scale, S_IRUGO, ad5791_show_scale, NULL, 0);

static ssize_t ad5791_show_name(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5791_state *st = iio_dev_get_devdata(indio_dev);

	return sprintf(buf, "%s\n", spi_get_device_id(st->spi)->name);
}
static IIO_DEVICE_ATTR(name, S_IRUGO, ad5791_show_name, NULL, 0);

#define IIO_DEV_ATTR_OUT_RW_RAW(_num, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(out##_num##_raw,				\
			S_IRUGO | S_IWUSR, _show, _store, _addr)

static IIO_DEV_ATTR_OUT_RW_RAW(0, ad5791_read_dac,
	ad5791_write_dac, AD5791_ADDR_DAC0);

static IIO_DEVICE_ATTR(out_powerdown_mode, S_IRUGO |
			S_IWUSR, ad5791_read_powerdown_mode,
			ad5791_write_powerdown_mode, 0);

static IIO_CONST_ATTR(out_powerdown_mode_available,
			"6kohm_to_gnd three_state");

#define IIO_DEV_ATTR_DAC_POWERDOWN(_num, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(out##_num##_powerdown,				\
			S_IRUGO | S_IWUSR, _show, _store, _addr)

static IIO_DEV_ATTR_DAC_POWERDOWN(0, ad5791_read_dac_powerdown,
				   ad5791_write_dac_powerdown, 0);

static struct attribute *ad5791_attributes[] = {
	&iio_dev_attr_out0_raw.dev_attr.attr,
	&iio_dev_attr_out0_powerdown.dev_attr.attr,
	&iio_dev_attr_out_powerdown_mode.dev_attr.attr,
	&iio_const_attr_out_powerdown_mode_available.dev_attr.attr,
	&iio_dev_attr_out_scale.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
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
		.bits = 16,
		.left_shift = 4,
		.get_lin_comp = ad5780_get_lin_comp,
	},
	[ID_AD5780] = {
		.bits = 18,
		.left_shift = 2,
		.get_lin_comp = ad5780_get_lin_comp,
	},
	[ID_AD5781] = {
		.bits = 18,
		.left_shift = 2,
		.get_lin_comp = ad5791_get_lin_comp,
	},
	[ID_AD5791] = {
		.bits = 20,
		.left_shift = 0,
		.get_lin_comp = ad5791_get_lin_comp,
	},
};

static const struct iio_info ad5791_info = {
	.attrs = &ad5791_attribute_group,
	.driver_module = THIS_MODULE,
};

static int __devinit ad5791_probe(struct spi_device *spi)
{
	struct ad5791_platform_data *pdata = spi->dev.platform_data;
	struct ad5791_state *st;
	int ret, pos_voltage_uv = 0, neg_voltage_uv = 0;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	spi_set_drvdata(spi, st);

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

	if (!IS_ERR(st->reg_vss) && !IS_ERR(st->reg_vdd))
		st->vref_mv = (pos_voltage_uv - neg_voltage_uv) / 1000;
	else if (pdata)
		st->vref_mv = pdata->vref_pos_mv - pdata->vref_neg_mv;
	else
		dev_warn(&spi->dev, "reference voltage unspecified\n");

	ret = ad5791_spi_write(spi, AD5791_ADDR_SW_CTRL, AD5791_SWCTRL_RESET);
	if (ret)
		goto error_disable_reg_neg;

	st->chip_info =
		&ad5791_chip_info_tbl[spi_get_device_id(spi)->driver_data];


	st->ctrl = AD5761_CTRL_LINCOMP(st->chip_info->get_lin_comp(st->vref_mv))
		  | ((pdata && pdata->use_rbuf_gain2) ? 0 : AD5791_CTRL_RBUF) |
		  AD5791_CTRL_BIN2SC;

	ret = ad5791_spi_write(spi, AD5791_ADDR_CTRL, st->ctrl |
		AD5791_CTRL_OPGND | AD5791_CTRL_DACTRI);
	if (ret)
		goto error_disable_reg_neg;

	st->pwr_down = true;

	st->spi = spi;
	st->indio_dev = iio_allocate_device(0);
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_disable_reg_neg;
	}
	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->info = &ad5791_info;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_free_dev;

	return 0;

error_free_dev:
	iio_free_device(st->indio_dev);

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

	kfree(st);
error_ret:
	return ret;
}

static int __devexit ad5791_remove(struct spi_device *spi)
{
	struct ad5791_state *st = spi_get_drvdata(spi);

	iio_device_unregister(st->indio_dev);

	if (!IS_ERR(st->reg_vdd)) {
		regulator_disable(st->reg_vdd);
		regulator_put(st->reg_vdd);
	}

	if (!IS_ERR(st->reg_vss)) {
		regulator_disable(st->reg_vss);
		regulator_put(st->reg_vss);
	}

	kfree(st);

	return 0;
}

static const struct spi_device_id ad5791_id[] = {
	{"ad5760", ID_AD5760},
	{"ad5780", ID_AD5780},
	{"ad5781", ID_AD5781},
	{"ad5791", ID_AD5791},
	{}
};

static struct spi_driver ad5791_driver = {
	.driver = {
		   .name = "ad5791",
		   .owner = THIS_MODULE,
		   },
	.probe = ad5791_probe,
	.remove = __devexit_p(ad5791_remove),
	.id_table = ad5791_id,
};

static __init int ad5791_spi_init(void)
{
	return spi_register_driver(&ad5791_driver);
}
module_init(ad5791_spi_init);

static __exit void ad5791_spi_exit(void)
{
	spi_unregister_driver(&ad5791_driver);
}
module_exit(ad5791_spi_exit);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD5760/AD5780/AD5781/AD5791 DAC");
MODULE_LICENSE("GPL v2");
