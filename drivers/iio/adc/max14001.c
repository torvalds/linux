// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/*
 * Analog Devices MAX14001/MAX14002 ADC driver
 *
 * Copyright (C) 2023-2025 Analog Devices Inc.
 * Copyright (C) 2023 Kim Seer Paller <kimseer.paller@analog.com>
 * Copyright (c) 2025 Marilene Andrade Garcia <marilene.agarcia@gmail.com>
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/MAX14001-MAX14002.pdf
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitrev.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/units.h>
#include <asm/byteorder.h>

#include <linux/iio/iio.h>
#include <linux/iio/types.h>

/* MAX14001 Registers Address */
#define MAX14001_REG_ADC		0x00
#define MAX14001_REG_FADC		0x01
#define MAX14001_REG_FLAGS		0x02
#define MAX14001_REG_FLTEN		0x03
#define MAX14001_REG_THL		0x04
#define MAX14001_REG_THU		0x05
#define MAX14001_REG_INRR		0x06
#define MAX14001_REG_INRT		0x07
#define MAX14001_REG_INRP		0x08
#define MAX14001_REG_CFG		0x09
#define MAX14001_REG_ENBL		0x0A
#define MAX14001_REG_ACT		0x0B
#define MAX14001_REG_WEN		0x0C

#define MAX14001_REG_VERIFICATION(x)	((x) + 0x10)

#define MAX14001_REG_CFG_BIT_EXRF	BIT(5)

#define MAX14001_REG_WEN_VALUE_WRITE	0x294

#define MAX14001_MASK_ADDR		GENMASK(15, 11)
#define MAX14001_MASK_WR		BIT(10)
#define MAX14001_MASK_DATA		GENMASK(9, 0)

struct max14001_state {
	const struct max14001_chip_info *chip_info;
	struct spi_device *spi;
	struct regmap *regmap;
	int vref_mV;
	bool spi_hw_has_lsb_first;

	/*
	 * The following buffers will be bit-reversed during device
	 * communication, because the device transmits and receives data
	 * LSB-first.
	 * DMA (thus cache coherency maintenance) requires the transfer
	 * buffers to live in their own cache lines.
	 */
	union {
		__be16 be;
		__le16 le;
	} spi_tx_buffer __aligned(IIO_DMA_MINALIGN);

	union {
		__be16 be;
		__le16 le;
	} spi_rx_buffer;
};

struct max14001_chip_info {
	const char *name;
};

static int max14001_read(void *context, unsigned int reg, unsigned int *val)
{
	struct max14001_state *st = context;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = &st->spi_tx_buffer,
			.len = sizeof(st->spi_tx_buffer),
			.cs_change = 1,
		}, {
			.rx_buf = &st->spi_rx_buffer,
			.len = sizeof(st->spi_rx_buffer),
		},
	};
	int ret;
	unsigned int addr, data;

	/*
	 * Prepare SPI transmit buffer 16 bit-value and reverse bit order
	 * to align with the LSB-first input on SDI port in order to meet
	 * the device communication requirements. If the controller supports
	 * SPI_LSB_FIRST, this step will be handled by the SPI controller.
	 */
	addr = FIELD_PREP(MAX14001_MASK_ADDR, reg);

	if (st->spi_hw_has_lsb_first)
		st->spi_tx_buffer.le = cpu_to_le16(addr);
	else
		st->spi_tx_buffer.be = cpu_to_be16(bitrev16(addr));

	ret = spi_sync_transfer(st->spi, xfers, ARRAY_SIZE(xfers));
	if (ret)
		return ret;

	/*
	 * Convert received 16-bit value to cpu-endian format and reverse
	 * bit order. If the controller supports SPI_LSB_FIRST, this step
	 * will be handled by the SPI controller.
	 */
	if (st->spi_hw_has_lsb_first)
		data = le16_to_cpu(st->spi_rx_buffer.le);
	else
		data = bitrev16(be16_to_cpu(st->spi_rx_buffer.be));

	*val = FIELD_GET(MAX14001_MASK_DATA, data);

	return 0;
}

static int max14001_write(struct max14001_state *st, unsigned int reg, unsigned int val)
{
	unsigned int addr;

	/*
	 * Prepare SPI transmit buffer 16 bit-value and reverse bit order
	 * to align with the LSB-first input on SDI port in order to meet
	 * the device communication requirements. If the controller supports
	 * SPI_LSB_FIRST, this step will be handled by the SPI controller.
	 */
	addr = FIELD_PREP(MAX14001_MASK_ADDR, reg) |
	       FIELD_PREP(MAX14001_MASK_WR, 1) |
	       FIELD_PREP(MAX14001_MASK_DATA, val);

	if (st->spi_hw_has_lsb_first)
		st->spi_tx_buffer.le = cpu_to_le16(addr);
	else
		st->spi_tx_buffer.be = cpu_to_be16(bitrev16(addr));

	return spi_write(st->spi, &st->spi_tx_buffer, sizeof(st->spi_tx_buffer));
}

static int max14001_write_single_reg(void *context, unsigned int reg, unsigned int val)
{
	struct max14001_state *st = context;
	int ret;

	/* Enable writing to the SPI register. */
	ret = max14001_write(st, MAX14001_REG_WEN, MAX14001_REG_WEN_VALUE_WRITE);
	if (ret)
		return ret;

	/* Writing data into SPI register. */
	ret = max14001_write(st, reg, val);
	if (ret)
		return ret;

	/* Disable writing to the SPI register. */
	return max14001_write(st, MAX14001_REG_WEN, 0);
}

static int max14001_write_verification_reg(struct max14001_state *st, unsigned int reg)
{
	unsigned int val;
	int ret;

	ret = regmap_read(st->regmap, reg, &val);
	if (ret)
		return ret;

	return max14001_write(st, MAX14001_REG_VERIFICATION(reg), val);
}

static int max14001_disable_mv_fault(struct max14001_state *st)
{
	unsigned int reg;
	int ret;

	/* Enable writing to the SPI registers. */
	ret = max14001_write(st, MAX14001_REG_WEN, MAX14001_REG_WEN_VALUE_WRITE);
	if (ret)
		return ret;

	/*
	 * Reads all registers and writes the values to their appropriate
	 * verification registers to clear the Memory Validation fault.
	 */
	for (reg = MAX14001_REG_FLTEN; reg <= MAX14001_REG_ENBL; reg++) {
		ret = max14001_write_verification_reg(st, reg);
		if (ret)
			return ret;
	}

	/* Disable writing to the SPI registers. */
	return max14001_write(st, MAX14001_REG_WEN, 0);
}

static int max14001_debugfs_reg_access(struct iio_dev *indio_dev,
				       unsigned int reg, unsigned int writeval,
				       unsigned int *readval)
{
	struct max14001_state *st = iio_priv(indio_dev);

	if (readval)
		return regmap_read(st->regmap, reg, readval);

	return regmap_write(st->regmap, reg, writeval);
}

static int max14001_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct max14001_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_read(st->regmap, MAX14001_REG_ADC, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_mV;
		*val2 = 10;

		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static const struct regmap_range max14001_regmap_rd_range[] = {
	regmap_reg_range(MAX14001_REG_ADC, MAX14001_REG_ENBL),
	regmap_reg_range(MAX14001_REG_WEN, MAX14001_REG_WEN),
	regmap_reg_range(MAX14001_REG_VERIFICATION(MAX14001_REG_FLTEN),
			 MAX14001_REG_VERIFICATION(MAX14001_REG_ENBL)),
};

static const struct regmap_access_table max14001_regmap_rd_table = {
	.yes_ranges = max14001_regmap_rd_range,
	.n_yes_ranges = ARRAY_SIZE(max14001_regmap_rd_range),
};

static const struct regmap_range max14001_regmap_wr_range[] = {
	regmap_reg_range(MAX14001_REG_FLTEN, MAX14001_REG_WEN),
	regmap_reg_range(MAX14001_REG_VERIFICATION(MAX14001_REG_FLTEN),
			 MAX14001_REG_VERIFICATION(MAX14001_REG_ENBL)),
};

static const struct regmap_access_table max14001_regmap_wr_table = {
	.yes_ranges = max14001_regmap_wr_range,
	.n_yes_ranges = ARRAY_SIZE(max14001_regmap_wr_range),
};

static const struct regmap_config max14001_regmap_config = {
	.reg_read = max14001_read,
	.reg_write = max14001_write_single_reg,
	.max_register = MAX14001_REG_VERIFICATION(MAX14001_REG_ENBL),
	.rd_table = &max14001_regmap_rd_table,
	.wr_table = &max14001_regmap_wr_table,
};

static const struct iio_info max14001_info = {
	.read_raw = max14001_read_raw,
	.debugfs_reg_access = max14001_debugfs_reg_access,
};

static const struct iio_chan_spec max14001_channel[] = {
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
	},
};

static int max14001_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct max14001_state *st;
	int ret;
	bool use_ext_vrefin = false;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;
	st->spi_hw_has_lsb_first = spi->mode & SPI_LSB_FIRST;
	st->chip_info = spi_get_device_match_data(spi);
	if (!st->chip_info)
		return -EINVAL;

	indio_dev->name = st->chip_info->name;
	indio_dev->info = &max14001_info;
	indio_dev->channels = max14001_channel;
	indio_dev->num_channels = ARRAY_SIZE(max14001_channel);
	indio_dev->modes = INDIO_DIRECT_MODE;

	st->regmap = devm_regmap_init(dev, NULL, st, &max14001_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap), "Failed to initialize regmap\n");

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable Vdd supply\n");

	ret = devm_regulator_get_enable(dev, "vddl");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable Vddl supply\n");

	ret = devm_regulator_get_enable_read_voltage(dev, "refin");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "Failed to get REFIN voltage\n");

	if (ret == -ENODEV)
		ret = 1250000;
	else
		use_ext_vrefin = true;
	st->vref_mV = ret / (MICRO / MILLI);

	if (use_ext_vrefin) {
		/*
		 * Configure the MAX14001/MAX14002 to use an external voltage
		 * reference source by setting the bit 5 of the configuration register.
		 */
		ret = regmap_set_bits(st->regmap, MAX14001_REG_CFG,
				      MAX14001_REG_CFG_BIT_EXRF);
		if (ret)
			return dev_err_probe(dev, ret,
			       "Failed to set External REFIN in Configuration Register\n");
	}

	ret = max14001_disable_mv_fault(st);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to disable MV Fault\n");

	return devm_iio_device_register(dev, indio_dev);
}

static struct max14001_chip_info max14001_chip_info = {
	.name = "max14001",
};

static struct max14001_chip_info max14002_chip_info = {
	.name = "max14002",
};

static const struct spi_device_id max14001_id_table[] = {
	{ "max14001", (kernel_ulong_t)&max14001_chip_info },
	{ "max14002", (kernel_ulong_t)&max14002_chip_info },
	{ }
};

static const struct of_device_id max14001_of_match[] = {
	{ .compatible = "adi,max14001", .data = &max14001_chip_info },
	{ .compatible = "adi,max14002", .data = &max14002_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, max14001_of_match);

static struct spi_driver max14001_driver = {
	.driver = {
		.name = "max14001",
		.of_match_table = max14001_of_match,
	},
	.probe = max14001_probe,
	.id_table = max14001_id_table,
};
module_spi_driver(max14001_driver);

MODULE_AUTHOR("Kim Seer Paller <kimseer.paller@analog.com>");
MODULE_AUTHOR("Marilene Andrade Garcia <marilene.agarcia@gmail.com>");
MODULE_DESCRIPTION("Analog Devices MAX14001/MAX14002 ADCs driver");
MODULE_LICENSE("GPL");
