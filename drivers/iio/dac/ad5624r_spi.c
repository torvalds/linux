/*
 * AD5624R, AD5644R, AD5664R Digital to analog convertors spi driver
 *
 * Copyright 2010-2011 Analog Devices Inc.
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

#include "ad5624r.h"

static int ad5624r_spi_write(struct spi_device *spi,
			     u8 cmd, u8 addr, u16 val, u8 len)
{
	u32 data;
	u8 msg[3];

	/*
	 * The input shift register is 24 bits wide. The first two bits are
	 * don't care bits. The next three are the command bits, C2 to C0,
	 * followed by the 3-bit DAC address, A2 to A0, and then the
	 * 16-, 14-, 12-bit data-word. The data-word comprises the 16-,
	 * 14-, 12-bit input code followed by 0, 2, or 4 don't care bits,
	 * for the AD5664R, AD5644R, and AD5624R, respectively.
	 */
	data = (0 << 22) | (cmd << 19) | (addr << 16) | (val << (16 - len));
	msg[0] = data >> 16;
	msg[1] = data >> 8;
	msg[2] = data;

	return spi_write(spi, msg, 3);
}

static int ad5624r_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad5624r_state *st = iio_priv(indio_dev);
	unsigned long scale_uv;

	switch (m) {
	case IIO_CHAN_INFO_SCALE:
		scale_uv = (st->vref_mv * 1000) >> chan->scan_type.realbits;
		*val =  scale_uv / 1000;
		*val2 = (scale_uv % 1000) * 1000;
		return IIO_VAL_INT_PLUS_MICRO;

	}
	return -EINVAL;
}

static int ad5624r_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct ad5624r_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val >= (1 << chan->scan_type.realbits) || val < 0)
			return -EINVAL;

		return ad5624r_spi_write(st->us,
				AD5624R_CMD_WRITE_INPUT_N_UPDATE_N,
				chan->address, val,
				chan->scan_type.shift);
	default:
		ret = -EINVAL;
	}

	return -EINVAL;
}

static const char * const ad5624r_powerdown_modes[] = {
	"1kohm_to_gnd",
	"100kohm_to_gnd",
	"three_state"
};

static int ad5624r_get_powerdown_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan)
{
	struct ad5624r_state *st = iio_priv(indio_dev);

	return st->pwr_down_mode;
}

static int ad5624r_set_powerdown_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int mode)
{
	struct ad5624r_state *st = iio_priv(indio_dev);

	st->pwr_down_mode = mode;

	return 0;
}

static const struct iio_enum ad5624r_powerdown_mode_enum = {
	.items = ad5624r_powerdown_modes,
	.num_items = ARRAY_SIZE(ad5624r_powerdown_modes),
	.get = ad5624r_get_powerdown_mode,
	.set = ad5624r_set_powerdown_mode,
};

static ssize_t ad5624r_read_dac_powerdown(struct iio_dev *indio_dev,
	uintptr_t private, const struct iio_chan_spec *chan, char *buf)
{
	struct ad5624r_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%d\n",
			!!(st->pwr_down_mask & (1 << chan->channel)));
}

static ssize_t ad5624r_write_dac_powerdown(struct iio_dev *indio_dev,
	uintptr_t private, const struct iio_chan_spec *chan, const char *buf,
	size_t len)
{
	bool pwr_down;
	int ret;
	struct ad5624r_state *st = iio_priv(indio_dev);

	ret = strtobool(buf, &pwr_down);
	if (ret)
		return ret;

	if (pwr_down)
		st->pwr_down_mask |= (1 << chan->channel);
	else
		st->pwr_down_mask &= ~(1 << chan->channel);

	ret = ad5624r_spi_write(st->us, AD5624R_CMD_POWERDOWN_DAC, 0,
				(st->pwr_down_mode << 4) |
				st->pwr_down_mask, 16);

	return ret ? ret : len;
}

static const struct iio_info ad5624r_info = {
	.write_raw = ad5624r_write_raw,
	.read_raw = ad5624r_read_raw,
	.driver_module = THIS_MODULE,
};

static const struct iio_chan_spec_ext_info ad5624r_ext_info[] = {
	{
		.name = "powerdown",
		.read = ad5624r_read_dac_powerdown,
		.write = ad5624r_write_dac_powerdown,
	},
	IIO_ENUM("powerdown_mode", true, &ad5624r_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", &ad5624r_powerdown_mode_enum),
	{ },
};

#define AD5624R_CHANNEL(_chan, _bits) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.output = 1, \
	.channel = (_chan), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.address = (_chan), \
	.scan_type = IIO_ST('u', (_bits), 16, 16 - (_bits)), \
	.ext_info = ad5624r_ext_info, \
}

#define DECLARE_AD5624R_CHANNELS(_name, _bits) \
	const struct iio_chan_spec _name##_channels[] = { \
		AD5624R_CHANNEL(0, _bits), \
		AD5624R_CHANNEL(1, _bits), \
		AD5624R_CHANNEL(2, _bits), \
		AD5624R_CHANNEL(3, _bits), \
}

static DECLARE_AD5624R_CHANNELS(ad5624r, 12);
static DECLARE_AD5624R_CHANNELS(ad5644r, 14);
static DECLARE_AD5624R_CHANNELS(ad5664r, 16);

static const struct ad5624r_chip_info ad5624r_chip_info_tbl[] = {
	[ID_AD5624R3] = {
		.channels = ad5624r_channels,
		.int_vref_mv = 1250,
	},
	[ID_AD5624R5] = {
		.channels = ad5624r_channels,
		.int_vref_mv = 2500,
	},
	[ID_AD5644R3] = {
		.channels = ad5644r_channels,
		.int_vref_mv = 1250,
	},
	[ID_AD5644R5] = {
		.channels = ad5644r_channels,
		.int_vref_mv = 2500,
	},
	[ID_AD5664R3] = {
		.channels = ad5664r_channels,
		.int_vref_mv = 1250,
	},
	[ID_AD5664R5] = {
		.channels = ad5664r_channels,
		.int_vref_mv = 2500,
	},
};

static int ad5624r_probe(struct spi_device *spi)
{
	struct ad5624r_state *st;
	struct iio_dev *indio_dev;
	int ret, voltage_uv = 0;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	st = iio_priv(indio_dev);
	st->reg = devm_regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			return ret;

		ret = regulator_get_voltage(st->reg);
		if (ret < 0)
			goto error_disable_reg;

		voltage_uv = ret;
	}

	spi_set_drvdata(spi, indio_dev);
	st->chip_info =
		&ad5624r_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	if (voltage_uv)
		st->vref_mv = voltage_uv / 1000;
	else
		st->vref_mv = st->chip_info->int_vref_mv;

	st->us = spi;

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &ad5624r_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->chip_info->channels;
	indio_dev->num_channels = AD5624R_DAC_CHANNELS;

	ret = ad5624r_spi_write(spi, AD5624R_CMD_INTERNAL_REFER_SETUP, 0,
				!!voltage_uv, 16);
	if (ret)
		goto error_disable_reg;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_disable_reg;

	return 0;

error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);

	return ret;
}

static int ad5624r_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad5624r_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);

	return 0;
}

static const struct spi_device_id ad5624r_id[] = {
	{"ad5624r3", ID_AD5624R3},
	{"ad5644r3", ID_AD5644R3},
	{"ad5664r3", ID_AD5664R3},
	{"ad5624r5", ID_AD5624R5},
	{"ad5644r5", ID_AD5644R5},
	{"ad5664r5", ID_AD5664R5},
	{}
};
MODULE_DEVICE_TABLE(spi, ad5624r_id);

static struct spi_driver ad5624r_driver = {
	.driver = {
		   .name = "ad5624r",
		   .owner = THIS_MODULE,
		   },
	.probe = ad5624r_probe,
	.remove = ad5624r_remove,
	.id_table = ad5624r_id,
};
module_spi_driver(ad5624r_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices AD5624/44/64R DAC spi driver");
MODULE_LICENSE("GPL v2");
