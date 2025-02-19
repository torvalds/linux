// SPDX-License-Identifier: GPL-2.0-only
/*
 * LTC2632 Digital to analog convertors spi driver
 *
 * Copyright 2017 Maxime Roussin-Bélanger
 * expanded by Silvan Murer <silvan.murer@gmail.com>
 */

#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>

#include <linux/unaligned.h>

#define LTC2632_CMD_WRITE_INPUT_N               0x0
#define LTC2632_CMD_UPDATE_DAC_N                0x1
#define LTC2632_CMD_WRITE_INPUT_N_UPDATE_ALL    0x2
#define LTC2632_CMD_WRITE_INPUT_N_UPDATE_N      0x3
#define LTC2632_CMD_POWERDOWN_DAC_N             0x4
#define LTC2632_CMD_POWERDOWN_CHIP              0x5
#define LTC2632_CMD_INTERNAL_REFER              0x6
#define LTC2632_CMD_EXTERNAL_REFER              0x7

/**
 * struct ltc2632_chip_info - chip specific information
 * @channels:		channel spec for the DAC
 * @num_channels:	DAC channel count of the chip
 * @vref_mv:		internal reference voltage
 */
struct ltc2632_chip_info {
	const struct iio_chan_spec *channels;
	const size_t num_channels;
	const int vref_mv;
};

/**
 * struct ltc2632_state - driver instance specific data
 * @spi_dev:			pointer to the spi_device struct
 * @powerdown_cache_mask:	used to show current channel powerdown state
 * @vref_mv:			used reference voltage (internal or external)
 */
struct ltc2632_state {
	struct spi_device *spi_dev;
	unsigned int powerdown_cache_mask;
	int vref_mv;
};

enum ltc2632_supported_device_ids {
	ID_LTC2632L12,
	ID_LTC2632L10,
	ID_LTC2632L8,
	ID_LTC2632H12,
	ID_LTC2632H10,
	ID_LTC2632H8,
	ID_LTC2634L12,
	ID_LTC2634L10,
	ID_LTC2634L8,
	ID_LTC2634H12,
	ID_LTC2634H10,
	ID_LTC2634H8,
	ID_LTC2636L12,
	ID_LTC2636L10,
	ID_LTC2636L8,
	ID_LTC2636H12,
	ID_LTC2636H10,
	ID_LTC2636H8,
};

static int ltc2632_spi_write(struct spi_device *spi,
			     u8 cmd, u8 addr, u16 val, u8 shift)
{
	u32 data;
	u8 msg[3];

	/*
	 * The input shift register is 24 bits wide.
	 * The next four are the command bits, C3 to C0,
	 * followed by the 4-bit DAC address, A3 to A0, and then the
	 * 12-, 10-, 8-bit data-word. The data-word comprises the 12-,
	 * 10-, 8-bit input code followed by 4, 6, or 8 don't care bits.
	 */
	data = (cmd << 20) | (addr << 16) | (val << shift);
	put_unaligned_be24(data, &msg[0]);

	return spi_write(spi, msg, sizeof(msg));
}

static int ltc2632_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val,
			    int *val2,
			    long m)
{
	const struct ltc2632_state *st = iio_priv(indio_dev);

	switch (m) {
	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_mv;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	}
	return -EINVAL;
}

static int ltc2632_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val,
			     int val2,
			     long mask)
{
	struct ltc2632_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val >= (1 << chan->scan_type.realbits) || val < 0)
			return -EINVAL;

		return ltc2632_spi_write(st->spi_dev,
					 LTC2632_CMD_WRITE_INPUT_N_UPDATE_N,
					 chan->address, val,
					 chan->scan_type.shift);
	default:
		return -EINVAL;
	}
}

static ssize_t ltc2632_read_dac_powerdown(struct iio_dev *indio_dev,
					  uintptr_t private,
					  const struct iio_chan_spec *chan,
					  char *buf)
{
	struct ltc2632_state *st = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n",
			  !!(st->powerdown_cache_mask & (1 << chan->channel)));
}

static ssize_t ltc2632_write_dac_powerdown(struct iio_dev *indio_dev,
					   uintptr_t private,
					   const struct iio_chan_spec *chan,
					   const char *buf,
					   size_t len)
{
	bool pwr_down;
	int ret;
	struct ltc2632_state *st = iio_priv(indio_dev);

	ret = kstrtobool(buf, &pwr_down);
	if (ret)
		return ret;

	if (pwr_down)
		st->powerdown_cache_mask |= (1 << chan->channel);
	else
		st->powerdown_cache_mask &= ~(1 << chan->channel);

	ret = ltc2632_spi_write(st->spi_dev,
				LTC2632_CMD_POWERDOWN_DAC_N,
				chan->channel, 0, 0);

	return ret ? ret : len;
}

static const struct iio_info ltc2632_info = {
	.write_raw	= ltc2632_write_raw,
	.read_raw	= ltc2632_read_raw,
};

static const struct iio_chan_spec_ext_info ltc2632_ext_info[] = {
	{
		.name = "powerdown",
		.read = ltc2632_read_dac_powerdown,
		.write = ltc2632_write_dac_powerdown,
		.shared = IIO_SEPARATE,
	},
	{ },
};

#define LTC2632_CHANNEL(_chan, _bits) { \
		.type = IIO_VOLTAGE, \
		.indexed = 1, \
		.output = 1, \
		.channel = (_chan), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.address = (_chan), \
		.scan_type = { \
			.realbits	= (_bits), \
			.shift		= 16 - (_bits), \
		}, \
		.ext_info = ltc2632_ext_info, \
}

#define DECLARE_LTC2632_CHANNELS(_name, _bits) \
	const struct iio_chan_spec _name ## _channels[] = { \
		LTC2632_CHANNEL(0, _bits), \
		LTC2632_CHANNEL(1, _bits), \
		LTC2632_CHANNEL(2, _bits), \
		LTC2632_CHANNEL(3, _bits), \
		LTC2632_CHANNEL(4, _bits), \
		LTC2632_CHANNEL(5, _bits), \
		LTC2632_CHANNEL(6, _bits), \
		LTC2632_CHANNEL(7, _bits), \
	}

static DECLARE_LTC2632_CHANNELS(ltc2632x12, 12);
static DECLARE_LTC2632_CHANNELS(ltc2632x10, 10);
static DECLARE_LTC2632_CHANNELS(ltc2632x8, 8);

static const struct ltc2632_chip_info ltc2632_chip_info_tbl[] = {
	[ID_LTC2632L12] = {
		.channels	= ltc2632x12_channels,
		.num_channels	= 2,
		.vref_mv	= 2500,
	},
	[ID_LTC2632L10] = {
		.channels	= ltc2632x10_channels,
		.num_channels	= 2,
		.vref_mv	= 2500,
	},
	[ID_LTC2632L8] =  {
		.channels	= ltc2632x8_channels,
		.num_channels	= 2,
		.vref_mv	= 2500,
	},
	[ID_LTC2632H12] = {
		.channels	= ltc2632x12_channels,
		.num_channels	= 2,
		.vref_mv	= 4096,
	},
	[ID_LTC2632H10] = {
		.channels	= ltc2632x10_channels,
		.num_channels	= 2,
		.vref_mv	= 4096,
	},
	[ID_LTC2632H8] =  {
		.channels	= ltc2632x8_channels,
		.num_channels	= 2,
		.vref_mv	= 4096,
	},
	[ID_LTC2634L12] = {
		.channels	= ltc2632x12_channels,
		.num_channels	= 4,
		.vref_mv	= 2500,
	},
	[ID_LTC2634L10] = {
		.channels	= ltc2632x10_channels,
		.num_channels	= 4,
		.vref_mv	= 2500,
	},
	[ID_LTC2634L8] =  {
		.channels	= ltc2632x8_channels,
		.num_channels	= 4,
		.vref_mv	= 2500,
	},
	[ID_LTC2634H12] = {
		.channels	= ltc2632x12_channels,
		.num_channels	= 4,
		.vref_mv	= 4096,
	},
	[ID_LTC2634H10] = {
		.channels	= ltc2632x10_channels,
		.num_channels	= 4,
		.vref_mv	= 4096,
	},
	[ID_LTC2634H8] =  {
		.channels	= ltc2632x8_channels,
		.num_channels	= 4,
		.vref_mv	= 4096,
	},
	[ID_LTC2636L12] = {
		.channels	= ltc2632x12_channels,
		.num_channels	= 8,
		.vref_mv	= 2500,
	},
	[ID_LTC2636L10] = {
		.channels	= ltc2632x10_channels,
		.num_channels	= 8,
		.vref_mv	= 2500,
	},
	[ID_LTC2636L8] =  {
		.channels	= ltc2632x8_channels,
		.num_channels	= 8,
		.vref_mv	= 2500,
	},
	[ID_LTC2636H12] = {
		.channels	= ltc2632x12_channels,
		.num_channels	= 8,
		.vref_mv	= 4096,
	},
	[ID_LTC2636H10] = {
		.channels	= ltc2632x10_channels,
		.num_channels	= 8,
		.vref_mv	= 4096,
	},
	[ID_LTC2636H8] =  {
		.channels	= ltc2632x8_channels,
		.num_channels	= 8,
		.vref_mv	= 4096,
	},
};

static int ltc2632_probe(struct spi_device *spi)
{
	struct ltc2632_state *st;
	struct iio_dev *indio_dev;
	struct ltc2632_chip_info *chip_info;
	bool has_external_vref;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->spi_dev = spi;

	chip_info = (struct ltc2632_chip_info *)
			spi_get_device_id(spi)->driver_data;

	ret = devm_regulator_get_enable_read_voltage(&spi->dev, "vref");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(&spi->dev, ret,
				     "Failed to get vref regulator voltage\n");

	has_external_vref = ret != -ENODEV;
	st->vref_mv = has_external_vref ? ret / 1000 : chip_info->vref_mv;

	if (has_external_vref) {
		ret = ltc2632_spi_write(spi, LTC2632_CMD_EXTERNAL_REFER,
					0, 0, 0);
		if (ret)
			return dev_err_probe(&spi->dev, ret,
				"Set external reference command failed\n");
	} else {
		ret = ltc2632_spi_write(spi, LTC2632_CMD_INTERNAL_REFER,
					0, 0, 0);
		if (ret)
			return dev_err_probe(&spi->dev, ret,
				"Set internal reference command failed\n");
	}

	indio_dev->name = fwnode_get_name(dev_fwnode(&spi->dev)) ?: spi_get_device_id(spi)->name;
	indio_dev->info = &ltc2632_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = chip_info->channels;
	indio_dev->num_channels = chip_info->num_channels;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id ltc2632_id[] = {
	{ "ltc2632-l12", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2632L12] },
	{ "ltc2632-l10", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2632L10] },
	{ "ltc2632-l8", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2632L8] },
	{ "ltc2632-h12", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2632H12] },
	{ "ltc2632-h10", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2632H10] },
	{ "ltc2632-h8", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2632H8] },
	{ "ltc2634-l12", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2634L12] },
	{ "ltc2634-l10", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2634L10] },
	{ "ltc2634-l8", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2634L8] },
	{ "ltc2634-h12", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2634H12] },
	{ "ltc2634-h10", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2634H10] },
	{ "ltc2634-h8", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2634H8] },
	{ "ltc2636-l12", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2636L12] },
	{ "ltc2636-l10", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2636L10] },
	{ "ltc2636-l8", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2636L8] },
	{ "ltc2636-h12", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2636H12] },
	{ "ltc2636-h10", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2636H10] },
	{ "ltc2636-h8", (kernel_ulong_t)&ltc2632_chip_info_tbl[ID_LTC2636H8] },
	{}
};
MODULE_DEVICE_TABLE(spi, ltc2632_id);

static const struct of_device_id ltc2632_of_match[] = {
	{
		.compatible = "lltc,ltc2632-l12",
		.data = &ltc2632_chip_info_tbl[ID_LTC2632L12]
	}, {
		.compatible = "lltc,ltc2632-l10",
		.data = &ltc2632_chip_info_tbl[ID_LTC2632L10]
	}, {
		.compatible = "lltc,ltc2632-l8",
		.data = &ltc2632_chip_info_tbl[ID_LTC2632L8]
	}, {
		.compatible = "lltc,ltc2632-h12",
		.data = &ltc2632_chip_info_tbl[ID_LTC2632H12]
	}, {
		.compatible = "lltc,ltc2632-h10",
		.data = &ltc2632_chip_info_tbl[ID_LTC2632H10]
	}, {
		.compatible = "lltc,ltc2632-h8",
		.data = &ltc2632_chip_info_tbl[ID_LTC2632H8]
	}, {
		.compatible = "lltc,ltc2634-l12",
		.data = &ltc2632_chip_info_tbl[ID_LTC2634L12]
	}, {
		.compatible = "lltc,ltc2634-l10",
		.data = &ltc2632_chip_info_tbl[ID_LTC2634L10]
	}, {
		.compatible = "lltc,ltc2634-l8",
		.data = &ltc2632_chip_info_tbl[ID_LTC2634L8]
	}, {
		.compatible = "lltc,ltc2634-h12",
		.data = &ltc2632_chip_info_tbl[ID_LTC2634H12]
	}, {
		.compatible = "lltc,ltc2634-h10",
		.data = &ltc2632_chip_info_tbl[ID_LTC2634H10]
	}, {
		.compatible = "lltc,ltc2634-h8",
		.data = &ltc2632_chip_info_tbl[ID_LTC2634H8]
	}, {
		.compatible = "lltc,ltc2636-l12",
		.data = &ltc2632_chip_info_tbl[ID_LTC2636L12]
	}, {
		.compatible = "lltc,ltc2636-l10",
		.data = &ltc2632_chip_info_tbl[ID_LTC2636L10]
	}, {
		.compatible = "lltc,ltc2636-l8",
		.data = &ltc2632_chip_info_tbl[ID_LTC2636L8]
	}, {
		.compatible = "lltc,ltc2636-h12",
		.data = &ltc2632_chip_info_tbl[ID_LTC2636H12]
	}, {
		.compatible = "lltc,ltc2636-h10",
		.data = &ltc2632_chip_info_tbl[ID_LTC2636H10]
	}, {
		.compatible = "lltc,ltc2636-h8",
		.data = &ltc2632_chip_info_tbl[ID_LTC2636H8]
	},
	{}
};
MODULE_DEVICE_TABLE(of, ltc2632_of_match);

static struct spi_driver ltc2632_driver = {
	.driver		= {
		.name	= "ltc2632",
		.of_match_table = ltc2632_of_match,
	},
	.probe		= ltc2632_probe,
	.id_table	= ltc2632_id,
};
module_spi_driver(ltc2632_driver);

MODULE_AUTHOR("Maxime Roussin-Belanger <maxime.roussinbelanger@gmail.com>");
MODULE_DESCRIPTION("LTC2632 DAC SPI driver");
MODULE_LICENSE("GPL v2");
