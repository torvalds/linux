// SPDX-License-Identifier: GPL-2.0-only
/*
 * AD5721, AD5721R, AD5761, AD5761R, Voltage Output Digital to Analog Converter
 *
 * Copyright 2016 Qtechnology A/S
 * 2016 Ricardo Ribalda <ribalda@kernel.org>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/bitops.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_data/ad5761.h>

#define AD5761_ADDR(addr)		((addr & 0xf) << 16)
#define AD5761_ADDR_NOOP		0x0
#define AD5761_ADDR_DAC_WRITE		0x3
#define AD5761_ADDR_CTRL_WRITE_REG	0x4
#define AD5761_ADDR_SW_DATA_RESET	0x7
#define AD5761_ADDR_DAC_READ		0xb
#define AD5761_ADDR_CTRL_READ_REG	0xc
#define AD5761_ADDR_SW_FULL_RESET	0xf

#define AD5761_CTRL_USE_INTVREF		BIT(5)
#define AD5761_CTRL_ETS			BIT(6)

/**
 * struct ad5761_chip_info - chip specific information
 * @int_vref:	Value of the internal reference voltage in mV - 0 if external
 *		reference voltage is used
 * @channel:	channel specification
*/

struct ad5761_chip_info {
	unsigned long int_vref;
	const struct iio_chan_spec channel;
};

struct ad5761_range_params {
	int m;
	int c;
};

enum ad5761_supported_device_ids {
	ID_AD5721,
	ID_AD5721R,
	ID_AD5761,
	ID_AD5761R,
};

/**
 * struct ad5761_state - driver instance specific data
 * @spi:		spi_device
 * @vref_reg:		reference voltage regulator
 * @use_intref:		true when the internal voltage reference is used
 * @vref:		actual voltage reference in mVolts
 * @range:		output range mode used
 * @lock:		lock to protect the data buffer during SPI ops
 * @data:		cache aligned spi buffer
 */
struct ad5761_state {
	struct spi_device		*spi;
	struct regulator		*vref_reg;
	struct mutex			lock;

	bool use_intref;
	int vref;
	enum ad5761_voltage_range range;

	/*
	 * DMA (thus cache coherency maintenance) may require the
	 * transfer buffers to live in their own cache lines.
	 */
	union {
		__be32 d32;
		u8 d8[4];
	} data[3] __aligned(IIO_DMA_MINALIGN);
};

static const struct ad5761_range_params ad5761_range_params[] = {
	[AD5761_VOLTAGE_RANGE_M10V_10V] = {
		.m = 80,
		.c = 40,
	},
	[AD5761_VOLTAGE_RANGE_0V_10V] = {
		.m = 40,
		.c = 0,
	},
	[AD5761_VOLTAGE_RANGE_M5V_5V] = {
		.m = 40,
		.c = 20,
	},
	[AD5761_VOLTAGE_RANGE_0V_5V] = {
		.m = 20,
		.c = 0,
	},
	[AD5761_VOLTAGE_RANGE_M2V5_7V5] = {
		.m = 40,
		.c = 10,
	},
	[AD5761_VOLTAGE_RANGE_M3V_3V] = {
		.m = 24,
		.c = 12,
	},
	[AD5761_VOLTAGE_RANGE_0V_16V] = {
		.m = 64,
		.c = 0,
	},
	[AD5761_VOLTAGE_RANGE_0V_20V] = {
		.m = 80,
		.c = 0,
	},
};

static int _ad5761_spi_write(struct ad5761_state *st, u8 addr, u16 val)
{
	st->data[0].d32 = cpu_to_be32(AD5761_ADDR(addr) | val);

	return spi_write(st->spi, &st->data[0].d8[1], 3);
}

static int ad5761_spi_write(struct iio_dev *indio_dev, u8 addr, u16 val)
{
	struct ad5761_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);
	ret = _ad5761_spi_write(st, addr, val);
	mutex_unlock(&st->lock);

	return ret;
}

static int _ad5761_spi_read(struct ad5761_state *st, u8 addr, u16 *val)
{
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = &st->data[0].d8[1],
			.bits_per_word = 8,
			.len = 3,
			.cs_change = true,
		}, {
			.tx_buf = &st->data[1].d8[1],
			.rx_buf = &st->data[2].d8[1],
			.bits_per_word = 8,
			.len = 3,
		},
	};

	st->data[0].d32 = cpu_to_be32(AD5761_ADDR(addr));
	st->data[1].d32 = cpu_to_be32(AD5761_ADDR(AD5761_ADDR_NOOP));

	ret = spi_sync_transfer(st->spi, xfers, ARRAY_SIZE(xfers));

	*val = be32_to_cpu(st->data[2].d32);

	return ret;
}

static int ad5761_spi_read(struct iio_dev *indio_dev, u8 addr, u16 *val)
{
	struct ad5761_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);
	ret = _ad5761_spi_read(st, addr, val);
	mutex_unlock(&st->lock);

	return ret;
}

static int ad5761_spi_set_range(struct ad5761_state *st,
				enum ad5761_voltage_range range)
{
	u16 aux;
	int ret;

	aux = (range & 0x7) | AD5761_CTRL_ETS;

	if (st->use_intref)
		aux |= AD5761_CTRL_USE_INTVREF;

	ret = _ad5761_spi_write(st, AD5761_ADDR_SW_FULL_RESET, 0);
	if (ret)
		return ret;

	ret = _ad5761_spi_write(st, AD5761_ADDR_CTRL_WRITE_REG, aux);
	if (ret)
		return ret;

	st->range = range;

	return 0;
}

static int ad5761_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long mask)
{
	struct ad5761_state *st;
	int ret;
	u16 aux;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = ad5761_spi_read(indio_dev, AD5761_ADDR_DAC_READ, &aux);
		if (ret)
			return ret;
		*val = aux >> chan->scan_type.shift;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		st = iio_priv(indio_dev);
		*val = st->vref * ad5761_range_params[st->range].m;
		*val /= 10;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		st = iio_priv(indio_dev);
		*val = -(1 << chan->scan_type.realbits);
		*val *=	ad5761_range_params[st->range].c;
		*val /=	ad5761_range_params[st->range].m;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad5761_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long mask)
{
	u16 aux;

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	if (val2 || (val << chan->scan_type.shift) > 0xffff || val < 0)
		return -EINVAL;

	aux = val << chan->scan_type.shift;

	return ad5761_spi_write(indio_dev, AD5761_ADDR_DAC_WRITE, aux);
}

static const struct iio_info ad5761_info = {
	.read_raw = &ad5761_read_raw,
	.write_raw = &ad5761_write_raw,
};

#define AD5761_CHAN(_bits) {				\
	.type = IIO_VOLTAGE,				\
	.output = 1,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
		BIT(IIO_CHAN_INFO_OFFSET),		\
	.scan_type = {					\
		.sign = 'u',				\
		.realbits = (_bits),			\
		.storagebits = 16,			\
		.shift = 16 - (_bits),			\
	},						\
}

static const struct ad5761_chip_info ad5761_chip_infos[] = {
	[ID_AD5721] = {
		.int_vref = 0,
		.channel = AD5761_CHAN(12),
	},
	[ID_AD5721R] = {
		.int_vref = 2500,
		.channel = AD5761_CHAN(12),
	},
	[ID_AD5761] = {
		.int_vref = 0,
		.channel = AD5761_CHAN(16),
	},
	[ID_AD5761R] = {
		.int_vref = 2500,
		.channel = AD5761_CHAN(16),
	},
};

static int ad5761_get_vref(struct ad5761_state *st,
			   const struct ad5761_chip_info *chip_info)
{
	int ret;

	st->vref_reg = devm_regulator_get_optional(&st->spi->dev, "vref");
	if (PTR_ERR(st->vref_reg) == -ENODEV) {
		/* Use Internal regulator */
		if (!chip_info->int_vref) {
			dev_err(&st->spi->dev,
				"Voltage reference not found\n");
			return -EIO;
		}

		st->use_intref = true;
		st->vref = chip_info->int_vref;
		return 0;
	}

	if (IS_ERR(st->vref_reg)) {
		dev_err(&st->spi->dev,
			"Error getting voltage reference regulator\n");
		return PTR_ERR(st->vref_reg);
	}

	ret = regulator_enable(st->vref_reg);
	if (ret) {
		dev_err(&st->spi->dev,
			 "Failed to enable voltage reference\n");
		return ret;
	}

	ret = regulator_get_voltage(st->vref_reg);
	if (ret < 0) {
		dev_err(&st->spi->dev,
			 "Failed to get voltage reference value\n");
		goto disable_regulator_vref;
	}

	if (ret < 2000000 || ret > 3000000) {
		dev_warn(&st->spi->dev,
			 "Invalid external voltage ref. value %d uV\n", ret);
		ret = -EIO;
		goto disable_regulator_vref;
	}

	st->vref = ret / 1000;
	st->use_intref = false;

	return 0;

disable_regulator_vref:
	regulator_disable(st->vref_reg);
	st->vref_reg = NULL;
	return ret;
}

static int ad5761_probe(struct spi_device *spi)
{
	struct iio_dev *iio_dev;
	struct ad5761_state *st;
	int ret;
	const struct ad5761_chip_info *chip_info =
		&ad5761_chip_infos[spi_get_device_id(spi)->driver_data];
	enum ad5761_voltage_range voltage_range = AD5761_VOLTAGE_RANGE_0V_5V;
	struct ad5761_platform_data *pdata = dev_get_platdata(&spi->dev);

	iio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!iio_dev)
		return -ENOMEM;

	st = iio_priv(iio_dev);

	st->spi = spi;
	spi_set_drvdata(spi, iio_dev);

	ret = ad5761_get_vref(st, chip_info);
	if (ret)
		return ret;

	if (pdata)
		voltage_range = pdata->voltage_range;

	mutex_init(&st->lock);

	ret = ad5761_spi_set_range(st, voltage_range);
	if (ret)
		goto disable_regulator_err;

	iio_dev->info = &ad5761_info;
	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->channels = &chip_info->channel;
	iio_dev->num_channels = 1;
	iio_dev->name = spi_get_device_id(st->spi)->name;
	ret = iio_device_register(iio_dev);
	if (ret)
		goto disable_regulator_err;

	return 0;

disable_regulator_err:
	if (!IS_ERR_OR_NULL(st->vref_reg))
		regulator_disable(st->vref_reg);

	return ret;
}

static void ad5761_remove(struct spi_device *spi)
{
	struct iio_dev *iio_dev = spi_get_drvdata(spi);
	struct ad5761_state *st = iio_priv(iio_dev);

	iio_device_unregister(iio_dev);

	if (!IS_ERR_OR_NULL(st->vref_reg))
		regulator_disable(st->vref_reg);
}

static const struct spi_device_id ad5761_id[] = {
	{"ad5721", ID_AD5721},
	{"ad5721r", ID_AD5721R},
	{"ad5761", ID_AD5761},
	{"ad5761r", ID_AD5761R},
	{}
};
MODULE_DEVICE_TABLE(spi, ad5761_id);

static struct spi_driver ad5761_driver = {
	.driver = {
		   .name = "ad5761",
		   },
	.probe = ad5761_probe,
	.remove = ad5761_remove,
	.id_table = ad5761_id,
};
module_spi_driver(ad5761_driver);

MODULE_AUTHOR("Ricardo Ribalda <ribalda@kernel.org>");
MODULE_DESCRIPTION("Analog Devices AD5721, AD5721R, AD5761, AD5761R driver");
MODULE_LICENSE("GPL v2");
