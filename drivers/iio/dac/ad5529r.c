// SPDX-License-Identifier: GPL-2.0-only
/*
 * AD5529R Digital-to-Analog Converter Driver
 * 16-Channel, 12/16-Bit, 40V High Voltage Precision DAC
 *
 * Copyright 2026 Analog Devices Inc.
 * Author: Janani Sunil <janani.sunil@analog.com>
 */

#include <linux/array_size.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/units.h>

#include <linux/iio/iio.h>

#define AD5529R_REG_INTERFACE_CONFIG_A		0x00
#define   AD5529R_INTERFACE_CONFIG_A_SW_RESET	(BIT(7) | BIT(0))
#define   AD5529R_INTERFACE_CONFIG_A_ADDR_ASCENSION	BIT(5)
#define   AD5529R_INTERFACE_CONFIG_A_SDO_ENABLE	BIT(4)
#define AD5529R_REG_DEVICE_CONFIG		0x02
#define AD5529R_REG_CHIP_GRADE			0x06
#define AD5529R_REG_SCRATCH_PAD			0x0A
#define AD5529R_REG_SPI_REVISION		0x0B
#define AD5529R_REG_VENDOR_H			0x0D
#define AD5529R_REG_STREAM_MODE			0x0E
#define AD5529R_REG_INTERFACE_STATUS_A		0x11
#define AD5529R_REG_MULTI_DAC_CH_SEL		0x14
#define AD5529R_REG_OUT_RANGE_BASE		0x3C
#define AD5529R_REG_OUT_RANGE(ch)		(AD5529R_REG_OUT_RANGE_BASE + (ch) * 2)
#define AD5529R_REG_DAC_INPUT_A_BASE		0x148
#define AD5529R_REG_DAC_INPUT_A(ch)		(AD5529R_REG_DAC_INPUT_A_BASE + (ch) * 2)
#define AD5529R_REG_DAC_DATA_READBACK_BASE	0x16A
#define AD5529R_REG_TSENS_ALERT_FLAG		0x18C
#define AD5529R_REG_TSENS_SHTD_FLAG		0x18E
#define AD5529R_REG_FUNC_BUSY			0x1A0
#define AD5529R_REG_REF_SEL			0x1A2
#define   AD5529R_REF_SEL_INTERNAL_REF		BIT(0)
#define AD5529R_REG_INIT_CRC_ERR_STAT		0x1A4
#define AD5529R_REG_MULTI_DAC_HOTPATH_SW_LDAC	0x1A8

#define AD5529R_MAX_REGISTER			0x232
#define AD5529R_8BIT_REG_MAX			0x13
#define AD5529R_MAX_CHANNELS			16
#define AD5529R_SPI_READ_FLAG			0x80
#define AD5529R_ADDR_SHIFT			12

struct ad5529r_model_data {
	const char *model_name;
	unsigned int resolution;
};

#define AD5529R_DAC_CHANNEL(chan) ((struct iio_chan_spec) {		\
	.type = IIO_VOLTAGE,						\
	.indexed = 1,							\
	.output = 1,							\
	.channel = (chan),						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
			      BIT(IIO_CHAN_INFO_SCALE) |		\
			      BIT(IIO_CHAN_INFO_OFFSET),		\
})

static const char * const ad5529r_supply_names[] = {
	"vdd-io",
	"vdd-5v",
	"vdd-hv0",
	"vdd-hv1",
	"vdd-hv2",
	"vdd-hv3",
};

static const char * const ad5529r_vss_supply_names[] = {
	"vss-hv0",
	"vss-hv1",
	"vss-hv2",
	"vss-hv3",
};

static const struct ad5529r_model_data ad5529r_16bit_model_data = {
	.model_name = "ad5529r-16",
	.resolution = 16,
};

static const struct ad5529r_model_data ad5529r_12bit_model_data = {
	.model_name = "ad5529r-12",
	.resolution = 12,
};

enum ad5529r_output_range {
	AD5529R_RANGE_0V_5V,
	AD5529R_RANGE_0V_10V,
	AD5529R_RANGE_0V_20V,
	AD5529R_RANGE_0V_40V,
	AD5529R_RANGE_M5V_5V,
	AD5529R_RANGE_M10V_10V,
	AD5529R_RANGE_M15V_15V,
	AD5529R_RANGE_M20V_20V,
};

static const s32 ad5529r_output_ranges_mV[8][2] = {
	[AD5529R_RANGE_0V_5V] = { 0, 5000 },
	[AD5529R_RANGE_0V_10V] = { 0, 10000 },
	[AD5529R_RANGE_0V_20V] = { 0, 20000 },
	[AD5529R_RANGE_0V_40V] = { 0, 40000 },
	[AD5529R_RANGE_M5V_5V] = { -5000, 5000 },
	[AD5529R_RANGE_M10V_10V] = { -10000, 10000 },
	[AD5529R_RANGE_M15V_15V] = { -15000, 15000 },
	[AD5529R_RANGE_M20V_20V] = { -20000, 20000 },
};

struct ad5529r_state {
	const struct ad5529r_model_data *model_data;
	struct regmap *regmap_8bit;
	struct regmap *regmap_16bit;
	struct iio_chan_spec channels[AD5529R_MAX_CHANNELS];
	unsigned int num_channels;
	enum ad5529r_output_range output_range_idx[AD5529R_MAX_CHANNELS];
};

static const struct regmap_range ad5529r_8bit_readable_ranges[] = {
	regmap_reg_range(AD5529R_REG_INTERFACE_CONFIG_A, AD5529R_REG_CHIP_GRADE),
	regmap_reg_range(AD5529R_REG_SCRATCH_PAD, AD5529R_REG_VENDOR_H),
	regmap_reg_range(AD5529R_REG_STREAM_MODE, AD5529R_REG_INTERFACE_STATUS_A),
};

static const struct regmap_range ad5529r_16bit_readable_ranges[] = {
	regmap_reg_range(AD5529R_REG_MULTI_DAC_CH_SEL, AD5529R_REG_INIT_CRC_ERR_STAT),
	regmap_reg_range(AD5529R_REG_MULTI_DAC_HOTPATH_SW_LDAC, AD5529R_MAX_REGISTER),
};

static const struct regmap_access_table ad5529r_8bit_readable_table = {
	.yes_ranges = ad5529r_8bit_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(ad5529r_8bit_readable_ranges),
};

static const struct regmap_access_table ad5529r_16bit_readable_table = {
	.yes_ranges = ad5529r_16bit_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(ad5529r_16bit_readable_ranges),
};

static const struct regmap_range ad5529r_8bit_read_only_ranges[] = {
	regmap_reg_range(AD5529R_REG_DEVICE_CONFIG, AD5529R_REG_CHIP_GRADE),
	regmap_reg_range(AD5529R_REG_SPI_REVISION, AD5529R_REG_VENDOR_H),
};

static const struct regmap_range ad5529r_16bit_read_only_ranges[] = {
	regmap_reg_range(AD5529R_REG_DAC_DATA_READBACK_BASE,
			 AD5529R_REG_DAC_DATA_READBACK_BASE +
			 (AD5529R_MAX_CHANNELS - 1) * 2),
	regmap_reg_range(AD5529R_REG_TSENS_ALERT_FLAG, AD5529R_REG_TSENS_SHTD_FLAG),
	regmap_reg_range(AD5529R_REG_FUNC_BUSY, AD5529R_REG_FUNC_BUSY),
	regmap_reg_range(AD5529R_REG_INIT_CRC_ERR_STAT, AD5529R_REG_INIT_CRC_ERR_STAT),
};

static const struct regmap_access_table ad5529r_8bit_writeable_table = {
	.no_ranges = ad5529r_8bit_read_only_ranges,
	.n_no_ranges = ARRAY_SIZE(ad5529r_8bit_read_only_ranges),
};

static const struct regmap_access_table ad5529r_16bit_writeable_table = {
	.no_ranges = ad5529r_16bit_read_only_ranges,
	.n_no_ranges = ARRAY_SIZE(ad5529r_16bit_read_only_ranges),
};

static struct regmap *ad5529r_get_regmap(struct ad5529r_state *st,
					 unsigned int reg)
{
	if (reg <= AD5529R_8BIT_REG_MAX)
		return st->regmap_8bit;

	return st->regmap_16bit;
}

static int ad5529r_reset(struct ad5529r_state *st)
{
	struct regmap *map = st->regmap_8bit;
	struct device *dev = regmap_get_device(map);
	struct reset_control *rst;
	int ret;

	rst = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(rst))
		return PTR_ERR(rst);

	if (rst) {
		ret = reset_control_assert(rst);
		if (ret)
			return ret;

		/* Minimum reset low width (t_reset) is 20 ns per datasheet. */
		ndelay(20);

		ret = reset_control_deassert(rst);
		if (ret)
			return ret;
	} else {
		ret = regmap_write(map, AD5529R_REG_INTERFACE_CONFIG_A,
				   AD5529R_INTERFACE_CONFIG_A_SW_RESET);
		if (ret)
			return ret;
	}

	/*
	 * Wait 10 ms for digital initialization to complete.
	 * Per datasheet, Interface Status A register NOT_READY_ERR bit is
	 * set if SPI transactions are attempted before digital initialization
	 * completes.
	 */
	fsleep(10 * USEC_PER_MSEC);

	return regmap_write(map, AD5529R_REG_INTERFACE_CONFIG_A,
			    AD5529R_INTERFACE_CONFIG_A_SDO_ENABLE |
			    AD5529R_INTERFACE_CONFIG_A_ADDR_ASCENSION);
}

static int ad5529r_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct ad5529r_state *st = iio_priv(indio_dev);
	enum ad5529r_output_range range_idx;
	unsigned int reg_addr, reg_val_h;
	int ret, span_mV;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		/*
		 * Read from DAC_INPUT_A register rather than DAC_DATA_READBACK.
		 * The DAC operates in transparent mode and directly reflects
		 * whatever value is written to the INPUT_A register.
		 */
		reg_addr = AD5529R_REG_DAC_INPUT_A(chan->channel);
		ret = regmap_read(st->regmap_16bit, reg_addr, &reg_val_h);
		if (ret)
			return ret;

		*val = reg_val_h;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		range_idx = st->output_range_idx[chan->channel];

		/*
		 * The datasheet specifies a 4.096 V external reference,
		 * matching the nominal output voltage of the internal
		 * reference.
		 */
		span_mV = ad5529r_output_ranges_mV[range_idx][1] -
			  ad5529r_output_ranges_mV[range_idx][0];
		*val = span_mV;
		*val2 = st->model_data->resolution;

		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		range_idx = st->output_range_idx[chan->channel];

		if (ad5529r_output_ranges_mV[range_idx][0] < 0)
			*val = -BIT(st->model_data->resolution - 1);
		else
			*val = 0;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad5529r_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct ad5529r_state *st = iio_priv(indio_dev);
	unsigned int reg_addr;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!in_range(val, 0, BIT(st->model_data->resolution)))
			return -EINVAL;

		reg_addr = AD5529R_REG_DAC_INPUT_A(chan->channel);

		return regmap_write(st->regmap_16bit, reg_addr, val);
	default:
		return -EINVAL;
	}
}

static int ad5529r_find_output_range(const s32 *vals)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(ad5529r_output_ranges_mV); i++) {
		const s32 *range = ad5529r_output_ranges_mV[i];

		if (vals[0] == range[0] * (MICRO / MILLI) &&
		    vals[1] == range[1] * (MICRO / MILLI))
			return i;
	}

	return -EINVAL;
}

static int ad5529r_parse_channel_ranges(struct device *dev,
					struct ad5529r_state *st)
{
	unsigned long channel_mask = 0;

	device_for_each_child_node_scoped(dev, child) {
		int ret, range_idx;
		u32 ch;

		if (st->num_channels == ARRAY_SIZE(st->channels))
			return dev_err_probe(dev, -ENOSPC, "Too many channels\n");

		ret = fwnode_property_read_u32(child, "reg", &ch);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Missing reg property in channel node\n");

		if (ch >= AD5529R_MAX_CHANNELS)
			return dev_err_probe(dev, -ECHRNG,
					     "Channel %u exceeds maximum %u\n",
					     ch, AD5529R_MAX_CHANNELS - 1);

		if (__test_and_set_bit(ch, &channel_mask))
			return dev_err_probe(dev, -EEXIST,
					     "Duplicate channel %u\n", ch);

		if (fwnode_property_present(child, "output-range-microvolt")) {
			s32 vals[2];

			/*
			 * DT stores cells as raw 32-bit values; signed endpoints are
			 * encoded by dtc in two's-complement and then interpreted
			 * here as s32.
			 */
			ret = fwnode_property_read_u32_array(child,
							     "output-range-microvolt",
							     (u32 *)vals, ARRAY_SIZE(vals));
			if (ret < 0)
				return dev_err_probe(dev, ret,
						     "Failed to read range for ch %u\n",
						     ch);

			range_idx = ad5529r_find_output_range(vals);
			if (range_idx < 0)
				return dev_err_probe(dev, range_idx,
						     "Invalid range [%d %d] for ch %u\n",
						     vals[0], vals[1], ch);
		} else {
			range_idx = AD5529R_RANGE_0V_5V;
		}

		st->output_range_idx[ch] = range_idx;
		ret = regmap_write(st->regmap_16bit,
				   AD5529R_REG_OUT_RANGE(ch), range_idx);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to configure range for ch %u\n",
					     ch);

		st->channels[st->num_channels++] = AD5529R_DAC_CHANNEL(ch);
	}

	return 0;
}

static int ad5529r_reg_access(struct iio_dev *indio_dev,
			      unsigned int reg,
			      unsigned int writeval,
			      unsigned int *readval)
{
	struct ad5529r_state *st = iio_priv(indio_dev);

	if (readval)
		return regmap_read(ad5529r_get_regmap(st, reg), reg, readval);

	return regmap_write(ad5529r_get_regmap(st, reg), reg, writeval);
}

static const struct iio_info ad5529r_info = {
	.read_raw = ad5529r_read_raw,
	.write_raw = ad5529r_write_raw,
	.debugfs_reg_access = ad5529r_reg_access,
};

static int ad5529r_probe(struct spi_device *spi)
{
	struct regmap_config regmap_16bit_cfg;
	struct regmap_config regmap_8bit_cfg;
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ad5529r_state *st;
	bool external_vref;
	u32 dev_addr;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->model_data = spi_get_device_match_data(spi);
	if (!st->model_data)
		return dev_err_probe(dev, -ENODATA,
				     "Failed to identify device variant\n");

	if (device_property_present(dev, "spi-device-addr")) {
		ret = device_property_read_u32(dev, "spi-device-addr", &dev_addr);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to read spi-device-addr\n");

		if (dev_addr > 3)
			return dev_err_probe(dev, -EADDRNOTAVAIL,
					     "spi-device-addr %u out of range [0, 3]\n",
					     dev_addr);
	} else {
		dev_addr = 0;
	}

	regmap_8bit_cfg = (struct regmap_config) {
		.name = "ad5529r-8bit",
		.reg_bits = 16,
		.val_bits = 8,
		.max_register = AD5529R_8BIT_REG_MAX,
		.read_flag_mask = AD5529R_SPI_READ_FLAG,
		.rd_table = &ad5529r_8bit_readable_table,
		.wr_table = &ad5529r_8bit_writeable_table,
		.reg_base = dev_addr << AD5529R_ADDR_SHIFT,
	};
	regmap_16bit_cfg = (struct regmap_config) {
		.name = "ad5529r-16bit",
		.reg_bits = 16,
		.val_bits = 16,
		.max_register = AD5529R_MAX_REGISTER,
		.read_flag_mask = AD5529R_SPI_READ_FLAG,
		.val_format_endian = REGMAP_ENDIAN_LITTLE,
		.rd_table = &ad5529r_16bit_readable_table,
		.wr_table = &ad5529r_16bit_writeable_table,
		.reg_stride = 2,
		.reg_base = dev_addr << AD5529R_ADDR_SHIFT,
	};

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(ad5529r_supply_names),
					     ad5529r_supply_names);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to get and enable regulators\n");

	for (unsigned int i = 0; i < ARRAY_SIZE(ad5529r_vss_supply_names); i++) {
		ret = devm_regulator_get_enable_optional(dev,
							 ad5529r_vss_supply_names[i]);
		if (ret && ret != -ENODEV)
			return dev_err_probe(dev, ret,
					     "Failed to get and enable %s regulator\n",
					     ad5529r_vss_supply_names[i]);
	}

	ret = devm_regulator_get_enable_optional(dev, "vref");
	if (ret == -ENODEV)
		external_vref = false;
	else if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to get and enable vref regulator\n");
	else
		external_vref = true;

	/* Wait 10 ms after power-up before the first SPI transaction. */
	fsleep(10 * USEC_PER_MSEC);

	st->regmap_8bit = devm_regmap_init_spi(spi, &regmap_8bit_cfg);
	if (IS_ERR(st->regmap_8bit))
		return dev_err_probe(dev, PTR_ERR(st->regmap_8bit),
				     "Failed to initialize 8-bit regmap\n");

	st->regmap_16bit = devm_regmap_init_spi(spi, &regmap_16bit_cfg);
	if (IS_ERR(st->regmap_16bit))
		return dev_err_probe(dev, PTR_ERR(st->regmap_16bit),
				     "Failed to initialize 16-bit regmap\n");

	ret = ad5529r_reset(st);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to reset device\n");

	ret = regmap_assign_bits(st->regmap_16bit, AD5529R_REG_REF_SEL,
				 AD5529R_REF_SEL_INTERNAL_REF,
				 !external_vref);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to configure reference\n");

	ret = ad5529r_parse_channel_ranges(dev, st);
	if (ret)
		return ret;

	indio_dev->name = st->model_data->model_name;
	indio_dev->info = &ad5529r_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->channels;
	indio_dev->num_channels = st->num_channels;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ad5529r_of_match[] = {
	{ .compatible = "adi,ad5529r-16", .data = &ad5529r_16bit_model_data },
	{ .compatible = "adi,ad5529r-12", .data = &ad5529r_12bit_model_data },
	{ }
};
MODULE_DEVICE_TABLE(of, ad5529r_of_match);

static const struct spi_device_id ad5529r_id[] = {
	{
		.name = "ad5529r-16",
		.driver_data = (kernel_ulong_t)&ad5529r_16bit_model_data,
	},
	{
		.name = "ad5529r-12",
		.driver_data = (kernel_ulong_t)&ad5529r_12bit_model_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(spi, ad5529r_id);

static struct spi_driver ad5529r_driver = {
	.driver = {
		.name = "ad5529r",
		.of_match_table = ad5529r_of_match,
	},
	.probe = ad5529r_probe,
	.id_table = ad5529r_id,
};
module_spi_driver(ad5529r_driver);

MODULE_AUTHOR("Janani Sunil <janani.sunil@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD5529R 12/16-bit DAC driver");
MODULE_LICENSE("GPL");
