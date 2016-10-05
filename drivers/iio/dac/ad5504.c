/*
 * AD5504, AD5501 High Voltage Digital to Analog Converter
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
#include <linux/bitops.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/dac/ad5504.h>

#define AD5504_RES_MASK			GENMASK(11, 0)
#define AD5504_CMD_READ			BIT(15)
#define AD5504_CMD_WRITE		0
#define AD5504_ADDR(addr)		((addr) << 12)

/* Registers */
#define AD5504_ADDR_NOOP		0
#define AD5504_ADDR_DAC(x)		((x) + 1)
#define AD5504_ADDR_ALL_DAC		5
#define AD5504_ADDR_CTRL		7

/* Control Register */
#define AD5504_DAC_PWR(ch)		((ch) << 2)
#define AD5504_DAC_PWRDWN_MODE(mode)	((mode) << 6)
#define AD5504_DAC_PWRDN_20K		0
#define AD5504_DAC_PWRDN_3STATE		1

/**
 * struct ad5446_state - driver instance specific data
 * @spi:			spi_device
 * @reg:		supply regulator
 * @vref_mv:		actual reference voltage used
 * @pwr_down_mask	power down mask
 * @pwr_down_mode	current power down mode
 * @data:		transfer buffer
 */
struct ad5504_state {
	struct spi_device		*spi;
	struct regulator		*reg;
	unsigned short			vref_mv;
	unsigned			pwr_down_mask;
	unsigned			pwr_down_mode;

	__be16				data[2] ____cacheline_aligned;
};

/**
 * ad5504_supported_device_ids:
 */

enum ad5504_supported_device_ids {
	ID_AD5504,
	ID_AD5501,
};

static int ad5504_spi_write(struct ad5504_state *st, u8 addr, u16 val)
{
	st->data[0] = cpu_to_be16(AD5504_CMD_WRITE | AD5504_ADDR(addr) |
			      (val & AD5504_RES_MASK));

	return spi_write(st->spi, &st->data[0], 2);
}

static int ad5504_spi_read(struct ad5504_state *st, u8 addr)
{
	int ret;
	struct spi_transfer t = {
	    .tx_buf = &st->data[0],
	    .rx_buf = &st->data[1],
	    .len = 2,
	};

	st->data[0] = cpu_to_be16(AD5504_CMD_READ | AD5504_ADDR(addr));
	ret = spi_sync_transfer(st->spi, &t, 1);
	if (ret < 0)
		return ret;

	return be16_to_cpu(st->data[1]) & AD5504_RES_MASK;
}

static int ad5504_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad5504_state *st = iio_priv(indio_dev);
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = ad5504_spi_read(st, chan->address);
		if (ret < 0)
			return ret;

		*val = ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_mv;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	}
	return -EINVAL;
}

static int ad5504_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct ad5504_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val >= (1 << chan->scan_type.realbits) || val < 0)
			return -EINVAL;

		return ad5504_spi_write(st, chan->address, val);
	default:
		return -EINVAL;
	}
}

static const char * const ad5504_powerdown_modes[] = {
	"20kohm_to_gnd",
	"three_state",
};

static int ad5504_get_powerdown_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan)
{
	struct ad5504_state *st = iio_priv(indio_dev);

	return st->pwr_down_mode;
}

static int ad5504_set_powerdown_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int mode)
{
	struct ad5504_state *st = iio_priv(indio_dev);

	st->pwr_down_mode = mode;

	return 0;
}

static const struct iio_enum ad5504_powerdown_mode_enum = {
	.items = ad5504_powerdown_modes,
	.num_items = ARRAY_SIZE(ad5504_powerdown_modes),
	.get = ad5504_get_powerdown_mode,
	.set = ad5504_set_powerdown_mode,
};

static ssize_t ad5504_read_dac_powerdown(struct iio_dev *indio_dev,
	uintptr_t private, const struct iio_chan_spec *chan, char *buf)
{
	struct ad5504_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%d\n",
			!(st->pwr_down_mask & (1 << chan->channel)));
}

static ssize_t ad5504_write_dac_powerdown(struct iio_dev *indio_dev,
	uintptr_t private, const struct iio_chan_spec *chan, const char *buf,
	size_t len)
{
	bool pwr_down;
	int ret;
	struct ad5504_state *st = iio_priv(indio_dev);

	ret = strtobool(buf, &pwr_down);
	if (ret)
		return ret;

	if (pwr_down)
		st->pwr_down_mask |= (1 << chan->channel);
	else
		st->pwr_down_mask &= ~(1 << chan->channel);

	ret = ad5504_spi_write(st, AD5504_ADDR_CTRL,
				AD5504_DAC_PWRDWN_MODE(st->pwr_down_mode) |
				AD5504_DAC_PWR(st->pwr_down_mask));

	/* writes to the CTRL register must be followed by a NOOP */
	ad5504_spi_write(st, AD5504_ADDR_NOOP, 0);

	return ret ? ret : len;
}

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
	iio_push_event(private,
		       IIO_UNMOD_EVENT_CODE(IIO_TEMP,
					    0,
					    IIO_EV_TYPE_THRESH,
					    IIO_EV_DIR_RISING),
		       iio_get_time_ns((struct iio_dev *)private));

	return IRQ_HANDLED;
}

static const struct iio_info ad5504_info = {
	.write_raw = ad5504_write_raw,
	.read_raw = ad5504_read_raw,
	.event_attrs = &ad5504_ev_attribute_group,
	.driver_module = THIS_MODULE,
};

static const struct iio_chan_spec_ext_info ad5504_ext_info[] = {
	{
		.name = "powerdown",
		.read = ad5504_read_dac_powerdown,
		.write = ad5504_write_dac_powerdown,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("powerdown_mode", IIO_SHARED_BY_TYPE,
		 &ad5504_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", &ad5504_powerdown_mode_enum),
	{ },
};

#define AD5504_CHANNEL(_chan) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.output = 1, \
	.channel = (_chan), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.address = AD5504_ADDR_DAC(_chan), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 12, \
		.storagebits = 16, \
	}, \
	.ext_info = ad5504_ext_info, \
}

static const struct iio_chan_spec ad5504_channels[] = {
	AD5504_CHANNEL(0),
	AD5504_CHANNEL(1),
	AD5504_CHANNEL(2),
	AD5504_CHANNEL(3),
};

static int ad5504_probe(struct spi_device *spi)
{
	struct ad5504_platform_data *pdata = spi->dev.platform_data;
	struct iio_dev *indio_dev;
	struct ad5504_state *st;
	struct regulator *reg;
	int ret, voltage_uv = 0;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	reg = devm_regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(reg)) {
		ret = regulator_enable(reg);
		if (ret)
			return ret;

		ret = regulator_get_voltage(reg);
		if (ret < 0)
			goto error_disable_reg;

		voltage_uv = ret;
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
	indio_dev->info = &ad5504_info;
	if (spi_get_device_id(st->spi)->driver_data == ID_AD5501)
		indio_dev->num_channels = 1;
	else
		indio_dev->num_channels = 4;
	indio_dev->channels = ad5504_channels;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (spi->irq) {
		ret = devm_request_threaded_irq(&spi->dev, spi->irq,
					   NULL,
					   &ad5504_event_handler,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					   spi_get_device_id(st->spi)->name,
					   indio_dev);
		if (ret)
			goto error_disable_reg;
	}

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_disable_reg;

	return 0;

error_disable_reg:
	if (!IS_ERR(reg))
		regulator_disable(reg);

	return ret;
}

static int ad5504_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad5504_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);

	return 0;
}

static const struct spi_device_id ad5504_id[] = {
	{"ad5504", ID_AD5504},
	{"ad5501", ID_AD5501},
	{}
};
MODULE_DEVICE_TABLE(spi, ad5504_id);

static struct spi_driver ad5504_driver = {
	.driver = {
		   .name = "ad5504",
		   },
	.probe = ad5504_probe,
	.remove = ad5504_remove,
	.id_table = ad5504_id,
};
module_spi_driver(ad5504_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD5501/AD5501 DAC");
MODULE_LICENSE("GPL v2");
