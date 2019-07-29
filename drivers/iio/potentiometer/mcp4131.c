/*
 * Industrial I/O driver for Microchip digital potentiometers
 *
 * Copyright (c) 2016 Slawomir Stepien
 * Based on: Peter Rosin's code from mcp4531.c
 *
 * Datasheet: http://ww1.microchip.com/downloads/en/DeviceDoc/22060b.pdf
 *
 * DEVID	#Wipers	#Positions	Resistor Opts (kOhm)
 * mcp4131	1	129		5, 10, 50, 100
 * mcp4132	1	129		5, 10, 50, 100
 * mcp4141	1	129		5, 10, 50, 100
 * mcp4142	1	129		5, 10, 50, 100
 * mcp4151	1	257		5, 10, 50, 100
 * mcp4152	1	257		5, 10, 50, 100
 * mcp4161	1	257		5, 10, 50, 100
 * mcp4162	1	257		5, 10, 50, 100
 * mcp4231	2	129		5, 10, 50, 100
 * mcp4232	2	129		5, 10, 50, 100
 * mcp4241	2	129		5, 10, 50, 100
 * mcp4242	2	129		5, 10, 50, 100
 * mcp4251	2	257		5, 10, 50, 100
 * mcp4252	2	257		5, 10, 50, 100
 * mcp4261	2	257		5, 10, 50, 100
 * mcp4262	2	257		5, 10, 50, 100
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 * TODO:
 * 1. Write wiper setting to EEPROM for EEPROM capable models.
 */

#include <linux/cache.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/iio/iio.h>
#include <linux/iio/types.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/spi/spi.h>

#define MCP4131_WRITE		(0x00 << 2)
#define MCP4131_READ		(0x03 << 2)

#define MCP4131_WIPER_SHIFT	4
#define MCP4131_CMDERR(r)	((r[0]) & 0x02)
#define MCP4131_RAW(r)		((r[0]) == 0xff ? 0x100 : (r[1]))

struct mcp4131_cfg {
	int wipers;
	int max_pos;
	int kohms;
};

enum mcp4131_type {
	MCP413x_502 = 0,
	MCP413x_103,
	MCP413x_503,
	MCP413x_104,
	MCP414x_502,
	MCP414x_103,
	MCP414x_503,
	MCP414x_104,
	MCP415x_502,
	MCP415x_103,
	MCP415x_503,
	MCP415x_104,
	MCP416x_502,
	MCP416x_103,
	MCP416x_503,
	MCP416x_104,
	MCP423x_502,
	MCP423x_103,
	MCP423x_503,
	MCP423x_104,
	MCP424x_502,
	MCP424x_103,
	MCP424x_503,
	MCP424x_104,
	MCP425x_502,
	MCP425x_103,
	MCP425x_503,
	MCP425x_104,
	MCP426x_502,
	MCP426x_103,
	MCP426x_503,
	MCP426x_104,
};

static const struct mcp4131_cfg mcp4131_cfg[] = {
	[MCP413x_502] = { .wipers = 1, .max_pos = 128, .kohms =   5, },
	[MCP413x_103] = { .wipers = 1, .max_pos = 128, .kohms =  10, },
	[MCP413x_503] = { .wipers = 1, .max_pos = 128, .kohms =  50, },
	[MCP413x_104] = { .wipers = 1, .max_pos = 128, .kohms = 100, },
	[MCP414x_502] = { .wipers = 1, .max_pos = 128, .kohms =   5, },
	[MCP414x_103] = { .wipers = 1, .max_pos = 128, .kohms =  10, },
	[MCP414x_503] = { .wipers = 1, .max_pos = 128, .kohms =  50, },
	[MCP414x_104] = { .wipers = 1, .max_pos = 128, .kohms = 100, },
	[MCP415x_502] = { .wipers = 1, .max_pos = 256, .kohms =   5, },
	[MCP415x_103] = { .wipers = 1, .max_pos = 256, .kohms =  10, },
	[MCP415x_503] = { .wipers = 1, .max_pos = 256, .kohms =  50, },
	[MCP415x_104] = { .wipers = 1, .max_pos = 256, .kohms = 100, },
	[MCP416x_502] = { .wipers = 1, .max_pos = 256, .kohms =   5, },
	[MCP416x_103] = { .wipers = 1, .max_pos = 256, .kohms =  10, },
	[MCP416x_503] = { .wipers = 1, .max_pos = 256, .kohms =  50, },
	[MCP416x_104] = { .wipers = 1, .max_pos = 256, .kohms = 100, },
	[MCP423x_502] = { .wipers = 2, .max_pos = 128, .kohms =   5, },
	[MCP423x_103] = { .wipers = 2, .max_pos = 128, .kohms =  10, },
	[MCP423x_503] = { .wipers = 2, .max_pos = 128, .kohms =  50, },
	[MCP423x_104] = { .wipers = 2, .max_pos = 128, .kohms = 100, },
	[MCP424x_502] = { .wipers = 2, .max_pos = 128, .kohms =   5, },
	[MCP424x_103] = { .wipers = 2, .max_pos = 128, .kohms =  10, },
	[MCP424x_503] = { .wipers = 2, .max_pos = 128, .kohms =  50, },
	[MCP424x_104] = { .wipers = 2, .max_pos = 128, .kohms = 100, },
	[MCP425x_502] = { .wipers = 2, .max_pos = 256, .kohms =   5, },
	[MCP425x_103] = { .wipers = 2, .max_pos = 256, .kohms =  10, },
	[MCP425x_503] = { .wipers = 2, .max_pos = 256, .kohms =  50, },
	[MCP425x_104] = { .wipers = 2, .max_pos = 256, .kohms = 100, },
	[MCP426x_502] = { .wipers = 2, .max_pos = 256, .kohms =   5, },
	[MCP426x_103] = { .wipers = 2, .max_pos = 256, .kohms =  10, },
	[MCP426x_503] = { .wipers = 2, .max_pos = 256, .kohms =  50, },
	[MCP426x_104] = { .wipers = 2, .max_pos = 256, .kohms = 100, },
};

struct mcp4131_data {
	struct spi_device *spi;
	const struct mcp4131_cfg *cfg;
	struct mutex lock;
	u8 buf[2] ____cacheline_aligned;
};

#define MCP4131_CHANNEL(ch) {					\
	.type = IIO_RESISTANCE,					\
	.indexed = 1,						\
	.output = 1,						\
	.channel = (ch),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec mcp4131_channels[] = {
	MCP4131_CHANNEL(0),
	MCP4131_CHANNEL(1),
};

static int mcp4131_read(struct spi_device *spi, void *buf, size_t len)
{
	struct spi_transfer t = {
		.tx_buf = buf, /* We need to send addr, cmd and 12 bits */
		.rx_buf	= buf,
		.len = len,
	};
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	return spi_sync(spi, &m);
}

static int mcp4131_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	int err;
	struct mcp4131_data *data = iio_priv(indio_dev);
	int address = chan->channel;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->lock);

		data->buf[0] = (address << MCP4131_WIPER_SHIFT) | MCP4131_READ;
		data->buf[1] = 0;

		err = mcp4131_read(data->spi, data->buf, 2);
		if (err) {
			mutex_unlock(&data->lock);
			return err;
		}

		/* Error, bad address/command combination */
		if (!MCP4131_CMDERR(data->buf)) {
			mutex_unlock(&data->lock);
			return -EIO;
		}

		*val = MCP4131_RAW(data->buf);
		mutex_unlock(&data->lock);

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = 1000 * data->cfg->kohms;
		*val2 = data->cfg->max_pos;
		return IIO_VAL_FRACTIONAL;
	}

	return -EINVAL;
}

static int mcp4131_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	int err;
	struct mcp4131_data *data = iio_priv(indio_dev);
	int address = chan->channel << MCP4131_WIPER_SHIFT;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val > data->cfg->max_pos || val < 0)
			return -EINVAL;
		break;

	default:
		return -EINVAL;
	}

	mutex_lock(&data->lock);

	data->buf[0] = address << MCP4131_WIPER_SHIFT;
	data->buf[0] |= MCP4131_WRITE | (val >> 8);
	data->buf[1] = val & 0xFF; /* 8 bits here */

	err = spi_write(data->spi, data->buf, 2);
	mutex_unlock(&data->lock);

	return err;
}

static const struct iio_info mcp4131_info = {
	.read_raw = mcp4131_read_raw,
	.write_raw = mcp4131_write_raw,
};

static int mcp4131_probe(struct spi_device *spi)
{
	int err;
	struct device *dev = &spi->dev;
	unsigned long devid;
	struct mcp4131_data *data;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);
	data->spi = spi;
	data->cfg = of_device_get_match_data(&spi->dev);
	if (!data->cfg) {
		devid = spi_get_device_id(spi)->driver_data;
		data->cfg = &mcp4131_cfg[devid];
	}

	mutex_init(&data->lock);

	indio_dev->dev.parent = dev;
	indio_dev->info = &mcp4131_info;
	indio_dev->channels = mcp4131_channels;
	indio_dev->num_channels = data->cfg->wipers;
	indio_dev->name = spi_get_device_id(spi)->name;

	err = devm_iio_device_register(dev, indio_dev);
	if (err) {
		dev_info(&spi->dev, "Unable to register %s\n", indio_dev->name);
		return err;
	}

	return 0;
}

static const struct of_device_id mcp4131_dt_ids[] = {
	{ .compatible = "microchip,mcp4131-502",
		.data = &mcp4131_cfg[MCP413x_502] },
	{ .compatible = "microchip,mcp4131-103",
		.data = &mcp4131_cfg[MCP413x_103] },
	{ .compatible = "microchip,mcp4131-503",
		.data = &mcp4131_cfg[MCP413x_503] },
	{ .compatible = "microchip,mcp4131-104",
		.data = &mcp4131_cfg[MCP413x_104] },
	{ .compatible = "microchip,mcp4132-502",
		.data = &mcp4131_cfg[MCP413x_502] },
	{ .compatible = "microchip,mcp4132-103",
		.data = &mcp4131_cfg[MCP413x_103] },
	{ .compatible = "microchip,mcp4132-503",
		.data = &mcp4131_cfg[MCP413x_503] },
	{ .compatible = "microchip,mcp4132-104",
		.data = &mcp4131_cfg[MCP413x_104] },
	{ .compatible = "microchip,mcp4141-502",
		.data = &mcp4131_cfg[MCP414x_502] },
	{ .compatible = "microchip,mcp4141-103",
		.data = &mcp4131_cfg[MCP414x_103] },
	{ .compatible = "microchip,mcp4141-503",
		.data = &mcp4131_cfg[MCP414x_503] },
	{ .compatible = "microchip,mcp4141-104",
		.data = &mcp4131_cfg[MCP414x_104] },
	{ .compatible = "microchip,mcp4142-502",
		.data = &mcp4131_cfg[MCP414x_502] },
	{ .compatible = "microchip,mcp4142-103",
		.data = &mcp4131_cfg[MCP414x_103] },
	{ .compatible = "microchip,mcp4142-503",
		.data = &mcp4131_cfg[MCP414x_503] },
	{ .compatible = "microchip,mcp4142-104",
		.data = &mcp4131_cfg[MCP414x_104] },
	{ .compatible = "microchip,mcp4151-502",
		.data = &mcp4131_cfg[MCP415x_502] },
	{ .compatible = "microchip,mcp4151-103",
		.data = &mcp4131_cfg[MCP415x_103] },
	{ .compatible = "microchip,mcp4151-503",
		.data = &mcp4131_cfg[MCP415x_503] },
	{ .compatible = "microchip,mcp4151-104",
		.data = &mcp4131_cfg[MCP415x_104] },
	{ .compatible = "microchip,mcp4152-502",
		.data = &mcp4131_cfg[MCP415x_502] },
	{ .compatible = "microchip,mcp4152-103",
		.data = &mcp4131_cfg[MCP415x_103] },
	{ .compatible = "microchip,mcp4152-503",
		.data = &mcp4131_cfg[MCP415x_503] },
	{ .compatible = "microchip,mcp4152-104",
		.data = &mcp4131_cfg[MCP415x_104] },
	{ .compatible = "microchip,mcp4161-502",
		.data = &mcp4131_cfg[MCP416x_502] },
	{ .compatible = "microchip,mcp4161-103",
		.data = &mcp4131_cfg[MCP416x_103] },
	{ .compatible = "microchip,mcp4161-503",
		.data = &mcp4131_cfg[MCP416x_503] },
	{ .compatible = "microchip,mcp4161-104",
		.data = &mcp4131_cfg[MCP416x_104] },
	{ .compatible = "microchip,mcp4162-502",
		.data = &mcp4131_cfg[MCP416x_502] },
	{ .compatible = "microchip,mcp4162-103",
		.data = &mcp4131_cfg[MCP416x_103] },
	{ .compatible = "microchip,mcp4162-503",
		.data = &mcp4131_cfg[MCP416x_503] },
	{ .compatible = "microchip,mcp4162-104",
		.data = &mcp4131_cfg[MCP416x_104] },
	{ .compatible = "microchip,mcp4231-502",
		.data = &mcp4131_cfg[MCP423x_502] },
	{ .compatible = "microchip,mcp4231-103",
		.data = &mcp4131_cfg[MCP423x_103] },
	{ .compatible = "microchip,mcp4231-503",
		.data = &mcp4131_cfg[MCP423x_503] },
	{ .compatible = "microchip,mcp4231-104",
		.data = &mcp4131_cfg[MCP423x_104] },
	{ .compatible = "microchip,mcp4232-502",
		.data = &mcp4131_cfg[MCP423x_502] },
	{ .compatible = "microchip,mcp4232-103",
		.data = &mcp4131_cfg[MCP423x_103] },
	{ .compatible = "microchip,mcp4232-503",
		.data = &mcp4131_cfg[MCP423x_503] },
	{ .compatible = "microchip,mcp4232-104",
		.data = &mcp4131_cfg[MCP423x_104] },
	{ .compatible = "microchip,mcp4241-502",
		.data = &mcp4131_cfg[MCP424x_502] },
	{ .compatible = "microchip,mcp4241-103",
		.data = &mcp4131_cfg[MCP424x_103] },
	{ .compatible = "microchip,mcp4241-503",
		.data = &mcp4131_cfg[MCP424x_503] },
	{ .compatible = "microchip,mcp4241-104",
		.data = &mcp4131_cfg[MCP424x_104] },
	{ .compatible = "microchip,mcp4242-502",
		.data = &mcp4131_cfg[MCP424x_502] },
	{ .compatible = "microchip,mcp4242-103",
		.data = &mcp4131_cfg[MCP424x_103] },
	{ .compatible = "microchip,mcp4242-503",
		.data = &mcp4131_cfg[MCP424x_503] },
	{ .compatible = "microchip,mcp4242-104",
		.data = &mcp4131_cfg[MCP424x_104] },
	{ .compatible = "microchip,mcp4251-502",
		.data = &mcp4131_cfg[MCP425x_502] },
	{ .compatible = "microchip,mcp4251-103",
		.data = &mcp4131_cfg[MCP425x_103] },
	{ .compatible = "microchip,mcp4251-503",
		.data = &mcp4131_cfg[MCP425x_503] },
	{ .compatible = "microchip,mcp4251-104",
		.data = &mcp4131_cfg[MCP425x_104] },
	{ .compatible = "microchip,mcp4252-502",
		.data = &mcp4131_cfg[MCP425x_502] },
	{ .compatible = "microchip,mcp4252-103",
		.data = &mcp4131_cfg[MCP425x_103] },
	{ .compatible = "microchip,mcp4252-503",
		.data = &mcp4131_cfg[MCP425x_503] },
	{ .compatible = "microchip,mcp4252-104",
		.data = &mcp4131_cfg[MCP425x_104] },
	{ .compatible = "microchip,mcp4261-502",
		.data = &mcp4131_cfg[MCP426x_502] },
	{ .compatible = "microchip,mcp4261-103",
		.data = &mcp4131_cfg[MCP426x_103] },
	{ .compatible = "microchip,mcp4261-503",
		.data = &mcp4131_cfg[MCP426x_503] },
	{ .compatible = "microchip,mcp4261-104",
		.data = &mcp4131_cfg[MCP426x_104] },
	{ .compatible = "microchip,mcp4262-502",
		.data = &mcp4131_cfg[MCP426x_502] },
	{ .compatible = "microchip,mcp4262-103",
		.data = &mcp4131_cfg[MCP426x_103] },
	{ .compatible = "microchip,mcp4262-503",
		.data = &mcp4131_cfg[MCP426x_503] },
	{ .compatible = "microchip,mcp4262-104",
		.data = &mcp4131_cfg[MCP426x_104] },
	{}
};
MODULE_DEVICE_TABLE(of, mcp4131_dt_ids);

static const struct spi_device_id mcp4131_id[] = {
	{ "mcp4131-502", MCP413x_502 },
	{ "mcp4131-103", MCP413x_103 },
	{ "mcp4131-503", MCP413x_503 },
	{ "mcp4131-104", MCP413x_104 },
	{ "mcp4132-502", MCP413x_502 },
	{ "mcp4132-103", MCP413x_103 },
	{ "mcp4132-503", MCP413x_503 },
	{ "mcp4132-104", MCP413x_104 },
	{ "mcp4141-502", MCP414x_502 },
	{ "mcp4141-103", MCP414x_103 },
	{ "mcp4141-503", MCP414x_503 },
	{ "mcp4141-104", MCP414x_104 },
	{ "mcp4142-502", MCP414x_502 },
	{ "mcp4142-103", MCP414x_103 },
	{ "mcp4142-503", MCP414x_503 },
	{ "mcp4142-104", MCP414x_104 },
	{ "mcp4151-502", MCP415x_502 },
	{ "mcp4151-103", MCP415x_103 },
	{ "mcp4151-503", MCP415x_503 },
	{ "mcp4151-104", MCP415x_104 },
	{ "mcp4152-502", MCP415x_502 },
	{ "mcp4152-103", MCP415x_103 },
	{ "mcp4152-503", MCP415x_503 },
	{ "mcp4152-104", MCP415x_104 },
	{ "mcp4161-502", MCP416x_502 },
	{ "mcp4161-103", MCP416x_103 },
	{ "mcp4161-503", MCP416x_503 },
	{ "mcp4161-104", MCP416x_104 },
	{ "mcp4162-502", MCP416x_502 },
	{ "mcp4162-103", MCP416x_103 },
	{ "mcp4162-503", MCP416x_503 },
	{ "mcp4162-104", MCP416x_104 },
	{ "mcp4231-502", MCP423x_502 },
	{ "mcp4231-103", MCP423x_103 },
	{ "mcp4231-503", MCP423x_503 },
	{ "mcp4231-104", MCP423x_104 },
	{ "mcp4232-502", MCP423x_502 },
	{ "mcp4232-103", MCP423x_103 },
	{ "mcp4232-503", MCP423x_503 },
	{ "mcp4232-104", MCP423x_104 },
	{ "mcp4241-502", MCP424x_502 },
	{ "mcp4241-103", MCP424x_103 },
	{ "mcp4241-503", MCP424x_503 },
	{ "mcp4241-104", MCP424x_104 },
	{ "mcp4242-502", MCP424x_502 },
	{ "mcp4242-103", MCP424x_103 },
	{ "mcp4242-503", MCP424x_503 },
	{ "mcp4242-104", MCP424x_104 },
	{ "mcp4251-502", MCP425x_502 },
	{ "mcp4251-103", MCP425x_103 },
	{ "mcp4251-503", MCP425x_503 },
	{ "mcp4251-104", MCP425x_104 },
	{ "mcp4252-502", MCP425x_502 },
	{ "mcp4252-103", MCP425x_103 },
	{ "mcp4252-503", MCP425x_503 },
	{ "mcp4252-104", MCP425x_104 },
	{ "mcp4261-502", MCP426x_502 },
	{ "mcp4261-103", MCP426x_103 },
	{ "mcp4261-503", MCP426x_503 },
	{ "mcp4261-104", MCP426x_104 },
	{ "mcp4262-502", MCP426x_502 },
	{ "mcp4262-103", MCP426x_103 },
	{ "mcp4262-503", MCP426x_503 },
	{ "mcp4262-104", MCP426x_104 },
	{}
};
MODULE_DEVICE_TABLE(spi, mcp4131_id);

static struct spi_driver mcp4131_driver = {
	.driver = {
		.name	= "mcp4131",
		.of_match_table = of_match_ptr(mcp4131_dt_ids),
	},
	.probe		= mcp4131_probe,
	.id_table	= mcp4131_id,
};

module_spi_driver(mcp4131_driver);

MODULE_AUTHOR("Slawomir Stepien <sst@poczta.fm>");
MODULE_DESCRIPTION("MCP4131 digital potentiometer");
MODULE_LICENSE("GPL v2");
