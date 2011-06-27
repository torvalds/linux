/*
 * AD5504, AD5501 High Voltage Digital to Analog Converter
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
#include "ad5504.h"

static int ad5504_spi_write(struct spi_device *spi, u8 addr, u16 val)
{
	u16 tmp = cpu_to_be16(AD5504_CMD_WRITE |
			      AD5504_ADDR(addr) |
			      (val & AD5504_RES_MASK));

	return spi_write(spi, (u8 *)&tmp, 2);
}

static int ad5504_spi_read(struct spi_device *spi, u8 addr, u16 *val)
{
	u16 tmp = cpu_to_be16(AD5504_CMD_READ | AD5504_ADDR(addr));
	int ret;
	struct spi_transfer	t = {
			.tx_buf		= &tmp,
			.rx_buf		= val,
			.len		= 2,
		};
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	ret = spi_sync(spi, &m);

	*val = be16_to_cpu(*val) & AD5504_RES_MASK;

	return ret;
}

static ssize_t ad5504_write_dac(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5504_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	long readin;
	int ret;

	ret = strict_strtol(buf, 10, &readin);
	if (ret)
		return ret;

	ret = ad5504_spi_write(st->spi, this_attr->address, readin);
	return ret ? ret : len;
}

static ssize_t ad5504_read_dac(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5504_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	u16 val;

	ret = ad5504_spi_read(st->spi, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", val);
}

static ssize_t ad5504_read_powerdown_mode(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5504_state *st = iio_priv(indio_dev);

	const char mode[][14] = {"20kohm_to_gnd", "three_state"};

	return sprintf(buf, "%s\n", mode[st->pwr_down_mode]);
}

static ssize_t ad5504_write_powerdown_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5504_state *st = iio_priv(indio_dev);
	int ret;

	if (sysfs_streq(buf, "20kohm_to_gnd"))
		st->pwr_down_mode = AD5504_DAC_PWRDN_20K;
	else if (sysfs_streq(buf, "three_state"))
		st->pwr_down_mode = AD5504_DAC_PWRDN_3STATE;
	else
		ret = -EINVAL;

	return ret ? ret : len;
}

static ssize_t ad5504_read_dac_powerdown(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5504_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	return sprintf(buf, "%d\n",
			!(st->pwr_down_mask & (1 << this_attr->address)));
}

static ssize_t ad5504_write_dac_powerdown(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	long readin;
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5504_state *st = iio_priv(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = strict_strtol(buf, 10, &readin);
	if (ret)
		return ret;

	if (readin == 0)
		st->pwr_down_mask |= (1 << this_attr->address);
	else if (readin == 1)
		st->pwr_down_mask &= ~(1 << this_attr->address);
	else
		ret = -EINVAL;

	ret = ad5504_spi_write(st->spi, AD5504_ADDR_CTRL,
				AD5504_DAC_PWRDWN_MODE(st->pwr_down_mode) |
				AD5504_DAC_PWR(st->pwr_down_mask));

	/* writes to the CTRL register must be followed by a NOOP */
	ad5504_spi_write(st->spi, AD5504_ADDR_NOOP, 0);

	return ret ? ret : len;
}

static ssize_t ad5504_show_scale(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5504_state *st = iio_priv(indio_dev);
	/* Corresponds to Vref / 2^(bits) */
	unsigned int scale_uv = (st->vref_mv * 1000) >> AD5505_BITS;

	return sprintf(buf, "%d.%03d\n", scale_uv / 1000, scale_uv % 1000);
}
static IIO_DEVICE_ATTR(out_scale, S_IRUGO, ad5504_show_scale, NULL, 0);

#define IIO_DEV_ATTR_OUT_RW_RAW(_num, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(out##_num##_raw,				\
			S_IRUGO | S_IWUSR, _show, _store, _addr)

static IIO_DEV_ATTR_OUT_RW_RAW(0, ad5504_read_dac,
	ad5504_write_dac, AD5504_ADDR_DAC0);
static IIO_DEV_ATTR_OUT_RW_RAW(1, ad5504_read_dac,
	ad5504_write_dac, AD5504_ADDR_DAC1);
static IIO_DEV_ATTR_OUT_RW_RAW(2, ad5504_read_dac,
	ad5504_write_dac, AD5504_ADDR_DAC2);
static IIO_DEV_ATTR_OUT_RW_RAW(3, ad5504_read_dac,
	ad5504_write_dac, AD5504_ADDR_DAC3);

static IIO_DEVICE_ATTR(out_powerdown_mode, S_IRUGO |
			S_IWUSR, ad5504_read_powerdown_mode,
			ad5504_write_powerdown_mode, 0);

static IIO_CONST_ATTR(out_powerdown_mode_available,
			"20kohm_to_gnd three_state");

#define IIO_DEV_ATTR_DAC_POWERDOWN(_num, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(out##_num##_powerdown,				\
			S_IRUGO | S_IWUSR, _show, _store, _addr)

static IIO_DEV_ATTR_DAC_POWERDOWN(0, ad5504_read_dac_powerdown,
				   ad5504_write_dac_powerdown, 0);
static IIO_DEV_ATTR_DAC_POWERDOWN(1, ad5504_read_dac_powerdown,
				   ad5504_write_dac_powerdown, 1);
static IIO_DEV_ATTR_DAC_POWERDOWN(2, ad5504_read_dac_powerdown,
				   ad5504_write_dac_powerdown, 2);
static IIO_DEV_ATTR_DAC_POWERDOWN(3, ad5504_read_dac_powerdown,
				   ad5504_write_dac_powerdown, 3);

static struct attribute *ad5504_attributes[] = {
	&iio_dev_attr_out0_raw.dev_attr.attr,
	&iio_dev_attr_out1_raw.dev_attr.attr,
	&iio_dev_attr_out2_raw.dev_attr.attr,
	&iio_dev_attr_out3_raw.dev_attr.attr,
	&iio_dev_attr_out0_powerdown.dev_attr.attr,
	&iio_dev_attr_out1_powerdown.dev_attr.attr,
	&iio_dev_attr_out2_powerdown.dev_attr.attr,
	&iio_dev_attr_out3_powerdown.dev_attr.attr,
	&iio_dev_attr_out_powerdown_mode.dev_attr.attr,
	&iio_const_attr_out_powerdown_mode_available.dev_attr.attr,
	&iio_dev_attr_out_scale.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad5504_attribute_group = {
	.attrs = ad5504_attributes,
};

static struct attribute *ad5501_attributes[] = {
	&iio_dev_attr_out0_raw.dev_attr.attr,
	&iio_dev_attr_out0_powerdown.dev_attr.attr,
	&iio_dev_attr_out_powerdown_mode.dev_attr.attr,
	&iio_const_attr_out_powerdown_mode_available.dev_attr.attr,
	&iio_dev_attr_out_scale.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad5501_attribute_group = {
	.attrs = ad5501_attributes,
};

static IIO_CONST_ATTR(temp0_thresh_rising_value, "110000");
static IIO_CONST_ATTR(temp0_thresh_rising_en, "1");

static struct attribute *ad5504_ev_attributes[] = {
	&iio_const_attr_temp0_thresh_rising_value.dev_attr.attr,
	&iio_const_attr_temp0_thresh_rising_en.dev_attr.attr,
	NULL,
};

static struct attribute_group ad5504_ev_attribute_group = {
	.attrs = ad5504_ev_attributes,
};

static irqreturn_t ad5504_event_handler(int irq, void *private)
{
	iio_push_event(private, 0,
		       IIO_UNMOD_EVENT_CODE(IIO_EV_CLASS_TEMP,
					    0,
					    IIO_EV_TYPE_THRESH,
					    IIO_EV_DIR_RISING),
		       iio_get_time_ns());

	return IRQ_HANDLED;
}

static const struct iio_info ad5504_info = {
	.attrs = &ad5504_attribute_group,
	.num_interrupt_lines = 1,
	.event_attrs = &ad5504_ev_attribute_group,
	.driver_module = THIS_MODULE,
};

static const struct iio_info ad5501_info = {
	.attrs = &ad5501_attribute_group,
	.num_interrupt_lines = 1,
	.event_attrs = &ad5504_ev_attribute_group,
	.driver_module = THIS_MODULE,
};

static int __devinit ad5504_probe(struct spi_device *spi)
{
	struct ad5504_platform_data *pdata = spi->dev.platform_data;
	struct iio_dev *indio_dev;
	struct ad5504_state *st;
	struct regulator *reg;
	int ret, voltage_uv = 0;

	reg = regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(reg)) {
		ret = regulator_enable(reg);
		if (ret)
			goto error_put_reg;

		voltage_uv = regulator_get_voltage(reg);
	}

	indio_dev = iio_allocate_device(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_disable_reg;
	}
	spi_set_drvdata(spi, indio_dev);
	st = iio_priv(indio_dev);
	if (voltage_uv)
		st->vref_mv = voltage_uv / 1000;
	else if (pdata)
		st->vref_mv = pdata->vref_mv;
	else
		dev_warn(&spi->dev, "reference voltage unspecified\n");

	st->reg = reg;
	st->spi = spi;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(st->spi)->name;
	if (spi_get_device_id(st->spi)->driver_data == ID_AD5501)
		indio_dev->info = &ad5501_info;
	else
		indio_dev->info = &ad5504_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_dev;

	if (spi->irq) {
		ret = request_threaded_irq(spi->irq,
					   NULL,
					   &ad5504_event_handler,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					   spi_get_device_id(st->spi)->name,
					   indio_dev);
		if (ret)
			goto error_unreg_iio_device;
	}

	return 0;

error_unreg_iio_device:
	iio_device_unregister(indio_dev);
error_free_dev:
	iio_free_device(indio_dev);
error_disable_reg:
	if (!IS_ERR(reg))
		regulator_disable(st->reg);
error_put_reg:
	if (!IS_ERR(reg))
		regulator_put(reg);

	return ret;
}

static int __devexit ad5504_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad5504_state *st = iio_priv(indio_dev);
	struct regulator *reg = st->reg;
	if (spi->irq)
		free_irq(spi->irq, indio_dev);

	iio_device_unregister(indio_dev);

	if (!IS_ERR(reg)) {
		regulator_disable(reg);
		regulator_put(reg);
	}

	return 0;
}

static const struct spi_device_id ad5504_id[] = {
	{"ad5504", ID_AD5504},
	{"ad5501", ID_AD5501},
	{}
};

static struct spi_driver ad5504_driver = {
	.driver = {
		   .name = "ad5504",
		   .owner = THIS_MODULE,
		   },
	.probe = ad5504_probe,
	.remove = __devexit_p(ad5504_remove),
	.id_table = ad5504_id,
};

static __init int ad5504_spi_init(void)
{
	return spi_register_driver(&ad5504_driver);
}
module_init(ad5504_spi_init);

static __exit void ad5504_spi_exit(void)
{
	spi_unregister_driver(&ad5504_driver);
}
module_exit(ad5504_spi_exit);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD5501/AD5501 DAC");
MODULE_LICENSE("GPL v2");
