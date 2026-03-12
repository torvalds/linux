// SPDX-License-Identifier: GPL-2.0
/*
 * max22007.c - MAX22007 DAC driver
 *
 * Driver for Analog Devices MAX22007 Digital to Analog Converter.
 *
 * Copyright (c) 2026 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/device/devres.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/kstrtox.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include <dt-bindings/iio/addac/adi,ad74413r.h>
struct device;

#define MAX22007_NUM_CHANNELS				4
#define MAX22007_REV_ID_REG				0x00
#define MAX22007_STAT_INTR_REG				0x01
#define MAX22007_INTERRUPT_EN_REG			0x02
#define MAX22007_CONFIG_REG				0x03
#define MAX22007_CONTROL_REG				0x04
#define MAX22007_CHANNEL_MODE_REG			0x05
#define MAX22007_SOFT_RESET_REG				0x06
#define MAX22007_DAC_CHANNEL_REG(ch)			(0x07 + (ch))
#define MAX22007_GPIO_CTRL_REG				0x0B
#define MAX22007_GPIO_DATA_REG				0x0C
#define MAX22007_GPI_EDGE_INT_CTRL_REG			0x0D
#define MAX22007_GPI_INT_STATUS_REG			0x0E

/* Channel mask definitions */
#define     MAX22007_CH_MODE_CH_MASK(ch)		BIT(12 + (ch))
#define     MAX22007_CH_PWRON_CH_MASK(ch)		BIT(8 + (ch))
#define     MAX22007_DAC_LATCH_MODE_MASK(ch)		BIT(12 + (ch))
#define     MAX22007_LDAC_UPDATE_MASK(ch)		BIT(12 + (ch))
#define     MAX22007_SW_RST_MASK			BIT(8)
#define     MAX22007_SW_CLR_MASK			BIT(12)
#define     MAX22007_SOFT_RESET_BITS_MASK		(MAX22007_SW_RST_MASK | \
							 MAX22007_SW_CLR_MASK)
#define     MAX22007_DAC_DATA_MASK			GENMASK(15, 4)
#define     MAX22007_DAC_MAX_RAW			GENMASK(11, 0)
#define     MAX22007_CRC8_POLYNOMIAL			0x8C
#define     MAX22007_CRC_EN_MASK			BIT(0)
#define     MAX22007_RW_MASK				BIT(0)
#define     MAX22007_CRC_OVERHEAD			1
#define     MAX22007_NUM_SUPPLIES			3
#define     MAX22007_REF_MV				2500

/* Field value preparation macros with masking */
#define     MAX22007_CH_PWR_VAL(ch, val)		(((val) & 0x1) << (8 + (ch)))
#define     MAX22007_CH_MODE_VAL(ch, val)		(((val) & 0x1) << (12 + (ch)))
#define     MAX22007_DAC_LATCH_MODE_VAL(ch, val)	(((val) & 0x1) << (12 + (ch)))

static u8 max22007_crc8_table[CRC8_TABLE_SIZE];

static const char * const max22007_supply_names[MAX22007_NUM_SUPPLIES] = {
	"vdd",
	"hvdd",
	"hvss",
};

struct max22007_state {
	struct spi_device *spi;
	struct regmap *regmap;
	struct iio_chan_spec *iio_chans;
	u8 tx_buf[4] __aligned(IIO_DMA_MINALIGN);
	u8 rx_buf[4];
};

static int max22007_spi_read(void *context, const void *reg, size_t reg_size,
			     void *val, size_t val_size)
{
	struct max22007_state *st = context;
	u8 calculated_crc, received_crc;
	u8 rx_buf[4];
	u8 reg_byte;
	int ret;

	if (reg_size != 1)
		return -EINVAL;

	if (val_size == 0 || val_size > 3)
		return -EINVAL;

	memcpy(&reg_byte, reg, 1);

	ret = spi_write_then_read(st->spi, &reg_byte, 1, rx_buf,
				  val_size + MAX22007_CRC_OVERHEAD);
	if (ret) {
		dev_err(&st->spi->dev, "SPI transfer failed: %d\n", ret);
		return ret;
	}

	calculated_crc = crc8(max22007_crc8_table, &reg_byte, 1, 0x00);
	calculated_crc = crc8(max22007_crc8_table, rx_buf, 2, calculated_crc);
	received_crc = rx_buf[val_size];

	if (calculated_crc != received_crc) {
		dev_err(&st->spi->dev, "CRC mismatch on read register %02x\n", reg_byte);
		return -EIO;
	}

	memcpy(val, rx_buf, val_size);

	return 0;
}

static int max22007_spi_write(void *context, const void *data, size_t count)
{
	struct max22007_state *st = context;
	struct spi_transfer xfer = {
		.tx_buf = st->tx_buf,
		.rx_buf = st->rx_buf,
	};

	if (count + MAX22007_CRC_OVERHEAD > sizeof(st->tx_buf))
		return -EINVAL;

	memset(st->tx_buf, 0, sizeof(st->tx_buf));

	xfer.len = count + MAX22007_CRC_OVERHEAD;

	memcpy(st->tx_buf, data, count);
	st->tx_buf[count] = crc8(max22007_crc8_table, st->tx_buf,
				 sizeof(st->tx_buf) - 1, 0x00);

	return spi_sync_transfer(st->spi, &xfer, 1);
}

static bool max22007_reg_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX22007_REV_ID_REG:
	case MAX22007_STAT_INTR_REG:
	case MAX22007_CONFIG_REG:
	case MAX22007_CONTROL_REG:
	case MAX22007_CHANNEL_MODE_REG:
	case MAX22007_SOFT_RESET_REG:
	case MAX22007_GPIO_CTRL_REG:
	case MAX22007_GPIO_DATA_REG:
	case MAX22007_GPI_EDGE_INT_CTRL_REG:
	case MAX22007_GPI_INT_STATUS_REG:
		return true;
	case MAX22007_DAC_CHANNEL_REG(0) ... MAX22007_DAC_CHANNEL_REG(MAX22007_NUM_CHANNELS - 1):
		return true;
	default:
		return false;
	}
}

static bool max22007_reg_writable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX22007_CONFIG_REG:
	case MAX22007_CONTROL_REG:
	case MAX22007_CHANNEL_MODE_REG:
	case MAX22007_SOFT_RESET_REG:
	case MAX22007_GPIO_CTRL_REG:
	case MAX22007_GPIO_DATA_REG:
	case MAX22007_GPI_EDGE_INT_CTRL_REG:
		return true;
	case MAX22007_DAC_CHANNEL_REG(0) ... MAX22007_DAC_CHANNEL_REG(MAX22007_NUM_CHANNELS - 1):
		return true;
	default:
		return false;
	}
}

static const struct regmap_bus max22007_regmap_bus = {
	.read = max22007_spi_read,
	.write = max22007_spi_write,
	.read_flag_mask = MAX22007_RW_MASK,
	.reg_format_endian_default = REGMAP_ENDIAN_BIG,
	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

static const struct regmap_config max22007_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.reg_shift = -1,
	.readable_reg = max22007_reg_readable,
	.writeable_reg = max22007_reg_writable,
	.max_register = 0x0E,
};

static int max22007_write_channel_data(struct max22007_state *st,
				       unsigned int channel, int data)
{
	unsigned int reg_val;

	if (data < 0 || data > MAX22007_DAC_MAX_RAW)
		return -EINVAL;

	reg_val = FIELD_PREP(MAX22007_DAC_DATA_MASK, data);

	return regmap_write(st->regmap, MAX22007_DAC_CHANNEL_REG(channel), reg_val);
}

static int max22007_read_channel_data(struct max22007_state *st,
				      unsigned int channel, int *data)
{
	unsigned int reg_val;
	int ret;

	ret = regmap_read(st->regmap, MAX22007_DAC_CHANNEL_REG(channel), &reg_val);
	if (ret)
		return ret;

	*data = FIELD_GET(MAX22007_DAC_DATA_MASK, reg_val);

	return 0;
}

static int max22007_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct max22007_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = max22007_read_channel_data(st, chan->channel, val);
		if (ret)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_VOLTAGE)
			*val = 5 * MAX22007_REF_MV;  /* 5 * Vref in mV */
		else
			*val = 25;  /* Vref / (2 * Rsense) = MAX22007_REF_MV / 100 */
		*val2 = 12;  /* 12-bit DAC resolution */
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int max22007_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct max22007_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return max22007_write_channel_data(st, chan->channel, val);
	default:
		return -EINVAL;
	}
}

static const struct iio_info max22007_info = {
	.read_raw = max22007_read_raw,
	.write_raw = max22007_write_raw,
};

static ssize_t max22007_read_dac_powerdown(struct iio_dev *indio_dev,
					   uintptr_t private,
					   const struct iio_chan_spec *chan,
					   char *buf)
{
	struct max22007_state *st = iio_priv(indio_dev);
	unsigned int reg_val;
	bool powerdown;
	int ret;

	ret = regmap_read(st->regmap, MAX22007_CHANNEL_MODE_REG, &reg_val);
	if (ret)
		return ret;

	powerdown = !(reg_val & MAX22007_CH_PWRON_CH_MASK(chan->channel));

	return sysfs_emit(buf, "%d\n", powerdown);
}

static ssize_t max22007_write_dac_powerdown(struct iio_dev *indio_dev,
					    uintptr_t private,
					    const struct iio_chan_spec *chan,
					    const char *buf, size_t len)
{
	struct max22007_state *st = iio_priv(indio_dev);
	bool powerdown;
	int ret;

	ret = kstrtobool(buf, &powerdown);
	if (ret)
		return ret;

	ret = regmap_update_bits(st->regmap, MAX22007_CHANNEL_MODE_REG,
				 MAX22007_CH_PWRON_CH_MASK(chan->channel),
				 MAX22007_CH_PWR_VAL(chan->channel, powerdown ? 0 : 1));
	if (ret)
		return ret;

	return len;
}

static const struct iio_chan_spec_ext_info max22007_ext_info[] = {
	{
		.name = "powerdown",
		.read = max22007_read_dac_powerdown,
		.write = max22007_write_dac_powerdown,
		.shared = IIO_SEPARATE,
	},
	{ }
};

static int max22007_parse_channel_cfg(struct max22007_state *st, u8 *num_channels)
{
	struct device *dev = &st->spi->dev;
	int ret, num_chan;
	int i = 0;
	u32 reg;

	num_chan = device_get_child_node_count(dev);
	if (!num_chan)
		return dev_err_probe(dev, -ENODEV, "no channels configured\n");

	st->iio_chans = devm_kcalloc(dev, num_chan, sizeof(*st->iio_chans), GFP_KERNEL);
	if (!st->iio_chans)
		return -ENOMEM;

	device_for_each_child_node_scoped(dev, child) {
		u32 ch_func;
		enum iio_chan_type chan_type;

		ret = fwnode_property_read_u32(child, "reg", &reg);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to read reg property of %pfwP\n", child);

		if (reg >= MAX22007_NUM_CHANNELS)
			return dev_err_probe(dev, -EINVAL,
					     "reg out of range in %pfwP\n", child);

		ret = fwnode_property_read_u32(child, "adi,ch-func", &ch_func);
		if (ret)
			return dev_err_probe(dev, ret,
					     "missing adi,ch-func property for %pfwP\n", child);

		switch (ch_func) {
		case CH_FUNC_VOLTAGE_OUTPUT:
			chan_type = IIO_VOLTAGE;
			break;
		case CH_FUNC_CURRENT_OUTPUT:
			chan_type = IIO_CURRENT;
			break;
		default:
			return dev_err_probe(dev, -EINVAL,
					     "invalid adi,ch-func %u for %pfwP\n",
					     ch_func, child);
		}

		st->iio_chans[i++] = (struct iio_chan_spec) {
			.output = 1,
			.indexed = 1,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
					      BIT(IIO_CHAN_INFO_SCALE),
			.ext_info = max22007_ext_info,
			.channel = reg,
			.type = chan_type,
		};

		ret = regmap_update_bits(st->regmap, MAX22007_CHANNEL_MODE_REG,
					 MAX22007_CH_MODE_CH_MASK(reg),
					 MAX22007_CH_MODE_VAL(reg, ch_func - 1));
		if (ret)
			return ret;

		/* Set DAC to transparent mode (immediate update) */
		ret = regmap_update_bits(st->regmap, MAX22007_CONFIG_REG,
					 MAX22007_DAC_LATCH_MODE_MASK(reg),
					 MAX22007_DAC_LATCH_MODE_VAL(reg, 1));
		if (ret)
			return ret;
	}

	*num_channels = num_chan;

	return 0;
}

static int max22007_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct gpio_desc *reset_gpio;
	struct max22007_state *st;
	struct iio_dev *indio_dev;
	u8 num_channels;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;

	crc8_populate_lsb(max22007_crc8_table, MAX22007_CRC8_POLYNOMIAL);

	st->regmap = devm_regmap_init(dev, &max22007_regmap_bus, st,
					 &max22007_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap),
				     "Failed to initialize regmap\n");

	ret = devm_regulator_bulk_get_enable(dev, MAX22007_NUM_SUPPLIES,
					     max22007_supply_names);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get and enable regulators\n");

	reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(reset_gpio))
		return dev_err_probe(dev, PTR_ERR(reset_gpio),
				     "Failed to get reset GPIO\n");

	if (reset_gpio) {
		gpiod_set_value_cansleep(reset_gpio, 1);
		usleep_range(1000, 5000);
		gpiod_set_value_cansleep(reset_gpio, 0);
		usleep_range(1000, 5000);
	} else {
		ret = regmap_write(st->regmap, MAX22007_SOFT_RESET_REG,
				   MAX22007_SOFT_RESET_BITS_MASK);
		if (ret)
			return ret;
	}

	ret = regmap_set_bits(st->regmap, MAX22007_CONFIG_REG,
			      MAX22007_CRC_EN_MASK);
	if (ret)
		return ret;

	ret = max22007_parse_channel_cfg(st, &num_channels);
	if (ret)
		return ret;

	indio_dev->info = &max22007_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->iio_chans;
	indio_dev->num_channels = num_channels;
	indio_dev->name = "max22007";

	return devm_iio_device_register(dev, indio_dev);
}

static const struct spi_device_id max22007_id[] = {
	{ "max22007" },
	{ }
};
MODULE_DEVICE_TABLE(spi, max22007_id);

static const struct of_device_id max22007_of_match[] = {
	{ .compatible = "adi,max22007" },
	{ }
};
MODULE_DEVICE_TABLE(of, max22007_of_match);

static struct spi_driver max22007_driver = {
	.driver = {
		.name = "max22007",
		.of_match_table = max22007_of_match,
	},
	.probe = max22007_probe,
	.id_table = max22007_id,
};
module_spi_driver(max22007_driver);

MODULE_AUTHOR("Janani Sunil <janani.sunil@analog.com>");
MODULE_DESCRIPTION("Analog Devices MAX22007 DAC");
MODULE_LICENSE("GPL");
