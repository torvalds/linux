/*
 * AD5624R, AD5644R, AD5664R Digital to analog convertors spi driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/delay.h>

#include "../iio.h"
#include "../sysfs.h"
#include "dac.h"
#include "ad5624r.h"

/**
 * struct ad5624r_state - device related storage
 * @indio_dev:	associated industrial IO device
 * @us:		spi device
 **/
struct ad5624r_state {
	struct iio_dev *indio_dev;
	struct spi_device *us;
	int data_len;
	int ldac_mode;
	int dac_power_mode[AD5624R_DAC_CHANNELS];
	int internal_ref;
};

static int ad5624r_spi_write(struct spi_device *spi,
			     u8 cmd, u8 addr, u16 val, u8 len)
{
	u32 data;
	u8 msg[3];

	/*
	 * The input shift register is 24 bits wide. The first two bits are don't care bits.
	 * The next three are the command bits, C2 to C0, followed by the 3-bit DAC address,
	 * A2 to A0, and then the 16-, 14-, 12-bit data-word. The data-word comprises the 16-,
	 * 14-, 12-bit input code followed by 0, 2, or 4 don't care bits, for the AD5664R,
	 * AD5644R, and AD5624R, respectively.
	 */
	data = (0 << 22) | (cmd << 19) | (addr << 16) | (val << (16 - len));
	msg[0] = data >> 16;
	msg[1] = data >> 8;
	msg[2] = data;

	return spi_write(spi, msg, 3);
}

static ssize_t ad5624r_write_dac(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	long readin;
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5624r_state *st = indio_dev->dev_data;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = strict_strtol(buf, 10, &readin);
	if (ret)
		return ret;

	ret = ad5624r_spi_write(st->us, AD5624R_CMD_WRITE_INPUT_N_UPDATE_N,
				this_attr->address, readin, st->data_len);
	return ret ? ret : len;
}

static ssize_t ad5624r_read_ldac_mode(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5624r_state *st = indio_dev->dev_data;

	return sprintf(buf, "%x\n", st->ldac_mode);
}

static ssize_t ad5624r_write_ldac_mode(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	long readin;
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5624r_state *st = indio_dev->dev_data;

	ret = strict_strtol(buf, 16, &readin);
	if (ret)
		return ret;

	ret = ad5624r_spi_write(st->us, AD5624R_CMD_LDAC_SETUP, 0,
				readin & 0xF, 16);
	st->ldac_mode = readin & 0xF;

	return ret ? ret : len;
}

static ssize_t ad5624r_read_dac_power_mode(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5624r_state *st = indio_dev->dev_data;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	return sprintf(buf, "%d\n", st->dac_power_mode[this_attr->address]);
}

static ssize_t ad5624r_write_dac_power_mode(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	long readin;
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5624r_state *st = indio_dev->dev_data;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = strict_strtol(buf, 10, &readin);
	if (ret)
		return ret;

	ret = ad5624r_spi_write(st->us, AD5624R_CMD_POWERDOWN_DAC, 0,
				((readin & 0x3) << 4) |
				(1 << this_attr->address), 16);

	st->dac_power_mode[this_attr->address] = readin & 0x3;

	return ret ? ret : len;
}

static ssize_t ad5624r_read_internal_ref_mode(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5624r_state *st = indio_dev->dev_data;

	return sprintf(buf, "%d\n", st->internal_ref);
}

static ssize_t ad5624r_write_internal_ref_mode(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t len)
{
	long readin;
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad5624r_state *st = indio_dev->dev_data;

	ret = strict_strtol(buf, 10, &readin);
	if (ret)
		return ret;

	ret = ad5624r_spi_write(st->us, AD5624R_CMD_INTERNAL_REFER_SETUP, 0,
				!!readin, 16);

	st->internal_ref = !!readin;

	return ret ? ret : len;
}

static IIO_DEV_ATTR_OUT_RAW(0, ad5624r_write_dac, AD5624R_ADDR_DAC0);
static IIO_DEV_ATTR_OUT_RAW(1, ad5624r_write_dac, AD5624R_ADDR_DAC1);
static IIO_DEV_ATTR_OUT_RAW(2, ad5624r_write_dac, AD5624R_ADDR_DAC2);
static IIO_DEV_ATTR_OUT_RAW(3, ad5624r_write_dac, AD5624R_ADDR_DAC3);

static IIO_DEVICE_ATTR(ldac_mode, S_IRUGO | S_IWUSR, ad5624r_read_ldac_mode,
		       ad5624r_write_ldac_mode, 0);
static IIO_DEVICE_ATTR(internal_ref, S_IRUGO | S_IWUSR,
		       ad5624r_read_internal_ref_mode,
		       ad5624r_write_internal_ref_mode, 0);

#define IIO_DEV_ATTR_DAC_POWER_MODE(_num, _show, _store, _addr)			\
	IIO_DEVICE_ATTR(dac_power_mode_##_num, S_IRUGO | S_IWUSR, _show, _store, _addr)

static IIO_DEV_ATTR_DAC_POWER_MODE(0, ad5624r_read_dac_power_mode,
				   ad5624r_write_dac_power_mode, 0);
static IIO_DEV_ATTR_DAC_POWER_MODE(1, ad5624r_read_dac_power_mode,
				   ad5624r_write_dac_power_mode, 1);
static IIO_DEV_ATTR_DAC_POWER_MODE(2, ad5624r_read_dac_power_mode,
				   ad5624r_write_dac_power_mode, 2);
static IIO_DEV_ATTR_DAC_POWER_MODE(3, ad5624r_read_dac_power_mode,
				   ad5624r_write_dac_power_mode, 3);

static struct attribute *ad5624r_attributes[] = {
	&iio_dev_attr_out0_raw.dev_attr.attr,
	&iio_dev_attr_out1_raw.dev_attr.attr,
	&iio_dev_attr_out2_raw.dev_attr.attr,
	&iio_dev_attr_out3_raw.dev_attr.attr,
	&iio_dev_attr_dac_power_mode_0.dev_attr.attr,
	&iio_dev_attr_dac_power_mode_1.dev_attr.attr,
	&iio_dev_attr_dac_power_mode_2.dev_attr.attr,
	&iio_dev_attr_dac_power_mode_3.dev_attr.attr,
	&iio_dev_attr_ldac_mode.dev_attr.attr,
	&iio_dev_attr_internal_ref.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad5624r_attribute_group = {
	.attrs = ad5624r_attributes,
};

static int __devinit ad5624r_probe(struct spi_device *spi)
{
	struct ad5624r_state *st;
	int ret = 0;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	spi_set_drvdata(spi, st);

	st->data_len = spi_get_device_id(spi)->driver_data;

	st->us = spi;
	st->indio_dev = iio_allocate_device();
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_st;
	}
	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->num_interrupt_lines = 0;
	st->indio_dev->event_attrs = NULL;

	st->indio_dev->attrs = &ad5624r_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_free_dev;

	spi->mode = SPI_MODE_0;
	spi_setup(spi);

	return 0;

error_free_dev:
	iio_free_device(st->indio_dev);
error_free_st:
	kfree(st);
error_ret:
	return ret;
}

static int __devexit ad5624r_remove(struct spi_device *spi)
{
	struct ad5624r_state *st = spi_get_drvdata(spi);

	iio_device_unregister(st->indio_dev);
	kfree(st);

	return 0;
}

static const struct spi_device_id ad5624r_id[] = {
	{"ad5624r", 12},
	{"ad5644r", 14},
	{"ad5664r", 16},
	{}
};

static struct spi_driver ad5624r_driver = {
	.driver = {
		   .name = "ad5624r",
		   .owner = THIS_MODULE,
		   },
	.probe = ad5624r_probe,
	.remove = __devexit_p(ad5624r_remove),
	.id_table = ad5624r_id,
};

static __init int ad5624r_spi_init(void)
{
	return spi_register_driver(&ad5624r_driver);
}
module_init(ad5624r_spi_init);

static __exit void ad5624r_spi_exit(void)
{
	spi_unregister_driver(&ad5624r_driver);
}
module_exit(ad5624r_spi_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices AD5624/44/64R DAC spi driver");
MODULE_LICENSE("GPL v2");
