// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 Anshul Dalal <anshulusr@gmail.com>
 *
 * Driver for Microchip MCP4801, MCP4802, MCP4811, MCP4812, MCP4821 and MCP4822
 *
 * Based on the work of:
 *	Michael Welling (MCP4922 Driver)
 *
 * Datasheet:
 *	MCP48x1: https://ww1.microchip.com/downloads/en/DeviceDoc/22244B.pdf
 *	MCP48x2: https://ww1.microchip.com/downloads/en/DeviceDoc/20002249B.pdf
 *
 * TODO:
 *	- Configurable gain
 *	- Regulator control
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>
#include <linux/iio/types.h>

#include <linux/unaligned.h>

#define MCP4821_ACTIVE_MODE BIT(12)
#define MCP4802_SECOND_CHAN BIT(15)

/* DAC uses an internal Voltage reference of 4.096V at a gain of 2x */
#define MCP4821_2X_GAIN_VREF_MV 4096

enum mcp4821_supported_drvice_ids {
	ID_MCP4801,
	ID_MCP4802,
	ID_MCP4811,
	ID_MCP4812,
	ID_MCP4821,
	ID_MCP4822,
};

struct mcp4821_state {
	struct spi_device *spi;
	u16 dac_value[2];
};

struct mcp4821_chip_info {
	const char *name;
	int num_channels;
	const struct iio_chan_spec channels[2];
};

#define MCP4821_CHAN(channel_id, resolution)                          \
	{                                                             \
		.type = IIO_VOLTAGE, .output = 1, .indexed = 1,       \
		.channel = (channel_id),                              \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),         \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.scan_type = {                                        \
			.realbits = (resolution),                     \
			.shift = 12 - (resolution),                   \
		},                                                    \
	}

static const struct mcp4821_chip_info mcp4821_chip_info_table[6] = {
	[ID_MCP4801] = {
			.name = "mcp4801",
			.num_channels = 1,
			.channels = {
				MCP4821_CHAN(0, 8),
			},
	},
	[ID_MCP4802] = {
			.name = "mcp4802",
			.num_channels = 2,
			.channels = {
				MCP4821_CHAN(0, 8),
				MCP4821_CHAN(1, 8),
			},
	},
	[ID_MCP4811] = {
			.name = "mcp4811",
			.num_channels = 1,
			.channels = {
				MCP4821_CHAN(0, 10),
			},
	},
	[ID_MCP4812] = {
			.name = "mcp4812",
			.num_channels = 2,
			.channels = {
				MCP4821_CHAN(0, 10),
				MCP4821_CHAN(1, 10),
			},
	},
	[ID_MCP4821] = {
			.name = "mcp4821",
			.num_channels = 1,
			.channels = {
				MCP4821_CHAN(0, 12),
			},
	},
	[ID_MCP4822] = {
			.name = "mcp4822",
			.num_channels = 2,
			.channels = {
				MCP4821_CHAN(0, 12),
				MCP4821_CHAN(1, 12),
			},
	},
};

static int mcp4821_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct mcp4821_state *state;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		state = iio_priv(indio_dev);
		*val = state->dac_value[chan->channel];
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = MCP4821_2X_GAIN_VREF_MV;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int mcp4821_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct mcp4821_state *state = iio_priv(indio_dev);
	u16 write_val;
	__be16 write_buffer;
	int ret;

	if (val2 != 0)
		return -EINVAL;

	if (val < 0 || val >= BIT(chan->scan_type.realbits))
		return -EINVAL;

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	write_val = MCP4821_ACTIVE_MODE | val << chan->scan_type.shift;
	if (chan->channel)
		write_val |= MCP4802_SECOND_CHAN;

	write_buffer = cpu_to_be16(write_val);
	ret = spi_write(state->spi, &write_buffer, sizeof(write_buffer));
	if (ret) {
		dev_err(&state->spi->dev, "Failed to write to device: %d", ret);
		return ret;
	}

	state->dac_value[chan->channel] = val;

	return 0;
}

static const struct iio_info mcp4821_info = {
	.read_raw = &mcp4821_read_raw,
	.write_raw = &mcp4821_write_raw,
};

static int mcp4821_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct mcp4821_state *state;
	const struct mcp4821_chip_info *info;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*state));
	if (indio_dev == NULL)
		return -ENOMEM;

	state = iio_priv(indio_dev);
	state->spi = spi;

	info = spi_get_device_match_data(spi);
	indio_dev->name = info->name;
	indio_dev->info = &mcp4821_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = info->channels;
	indio_dev->num_channels = info->num_channels;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

#define MCP4821_COMPATIBLE(of_compatible, id)        \
	{                                            \
		.compatible = of_compatible,         \
		.data = &mcp4821_chip_info_table[id] \
	}

static const struct of_device_id mcp4821_of_table[] = {
	MCP4821_COMPATIBLE("microchip,mcp4801", ID_MCP4801),
	MCP4821_COMPATIBLE("microchip,mcp4802", ID_MCP4802),
	MCP4821_COMPATIBLE("microchip,mcp4811", ID_MCP4811),
	MCP4821_COMPATIBLE("microchip,mcp4812", ID_MCP4812),
	MCP4821_COMPATIBLE("microchip,mcp4821", ID_MCP4821),
	MCP4821_COMPATIBLE("microchip,mcp4822", ID_MCP4822),
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, mcp4821_of_table);

static const struct spi_device_id mcp4821_id_table[] = {
	{ "mcp4801", (kernel_ulong_t)&mcp4821_chip_info_table[ID_MCP4801]},
	{ "mcp4802", (kernel_ulong_t)&mcp4821_chip_info_table[ID_MCP4802]},
	{ "mcp4811", (kernel_ulong_t)&mcp4821_chip_info_table[ID_MCP4811]},
	{ "mcp4812", (kernel_ulong_t)&mcp4821_chip_info_table[ID_MCP4812]},
	{ "mcp4821", (kernel_ulong_t)&mcp4821_chip_info_table[ID_MCP4821]},
	{ "mcp4822", (kernel_ulong_t)&mcp4821_chip_info_table[ID_MCP4822]},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(spi, mcp4821_id_table);

static struct spi_driver mcp4821_driver = {
	.driver = {
		.name = "mcp4821",
		.of_match_table = mcp4821_of_table,
	},
	.probe = mcp4821_probe,
	.id_table = mcp4821_id_table,
};
module_spi_driver(mcp4821_driver);

MODULE_AUTHOR("Anshul Dalal <anshulusr@gmail.com>");
MODULE_DESCRIPTION("Microchip MCP4821 DAC Driver");
MODULE_LICENSE("GPL");
