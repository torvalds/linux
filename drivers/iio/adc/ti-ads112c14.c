// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIO driver for Texas Instruments ADS112C14 and similar ADCs.
 *
 * Copyright (C) 2026 Texas Instruments Incorporated - https://www.ti.com/
 * Copyright (C) 2026 Baylibre Inc.
 *
 * Datasheet: https://www.ti.com/lit/ds/symlink/ads122c14.pdf
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/units.h>

/* Datasheet t_d(RST) - time to wait after reset before next I2C use. */
#define ADS112C14_DELAY_RESET_US 500

#define ADS112C14_CMD_RDATA	0x00
#define ADS112C14_CMD_RREG	0x40
#define ADS112C14_CMD_WREG	0x80

#define ADS112C14_REG_DEVICE_ID				0x00
#define   ADS112C14_DEVICE_ID_BITS			GENMASK(3, 0)

#define ADS112C14_REG_REVISION_ID			0x01

#define ADS112C14_REG_STATUS_MSB			0x02
#define   ADS112C14_STATUS_MSB_RESETN			BIT(7)
#define   ADS112C14_STATUS_MSB_AVDD_UVN			BIT(6)
#define   ADS112C14_STATUS_MSB_REF_UVN			BIT(5)
#define   ADS112C14_STATUS_MSB_REG_MAP_CRC_FAULTN	BIT(3)
#define   ADS112C14_STATUS_MSB_MEM_FAULTN		BIT(2)
#define   ADS112C14_STATUS_MSB_REG_WRITE_FAULTN		BIT(1)
#define   ADS112C14_STATUS_MSB_DRDY			BIT(0)

#define ADS112C14_REG_STATUS_LSB			0x03
#define   ADS112C14_STATUS_LSB_CONV_COUNT		GENMASK(7, 4)
#define   ADS112C14_STATUS_LSB_GPIO3_DAT_IN		BIT(3)
#define   ADS112C14_STATUS_LSB_GPIO2_DAT_IN		BIT(2)
#define   ADS112C14_STATUS_LSB_GPIO1_DAT_IN		BIT(1)
#define   ADS112C14_STATUS_LSB_GPIO0_DAT_IN		BIT(0)

#define ADS112C14_REG_CONVERSION_CTRL			0x04
#define   ADS112C14_CONVERSION_CTRL_RESET		GENMASK(7, 2)
#define   ADS112C14_CONVERSION_CTRL_START		BIT(1)
#define   ADS112C14_CONVERSION_CTRL_STOP		BIT(0)

#define ADS112C14_REG_DEVICE_CFG			0x05
#define   ADS112C14_DEVICE_CFG_PWDN			BIT(7)
#define   ADS112C14_DEVICE_CFG_STBY_MODE		BIT(6)
#define   ADS112C14_DEVICE_CFG_BOCS			GENMASK(5, 4)
#define   ADS112C14_DEVICE_CFG_CLK_SEL			BIT(3)
#define   ADS112C14_DEVICE_CFG_CONV_MODE		BIT(2)
#define     ADS112C14_DEVICE_CFG_CONV_MODE_CONTINUOUS	  0
#define     ADS112C14_DEVICE_CFG_CONV_MODE_SINGLE_SHOT	  1
#define   ADS112C14_DEVICE_CFG_SPEED_MODE		GENMASK(1, 0)

#define ADS112C14_REG_DATA_RATE_CFG			0x06
#define   ADS112C14_DATA_RATE_CFG_DELAY			GENMASK(7, 4)
#define   ADS112C14_DATA_RATE_CFG_GC_EN			BIT(3)
#define   ADS112C14_DATA_RATE_CFG_FLTR_OSR		GENMASK(2, 0)

#define ADS112C14_REG_MUX_CFG				0x07
#define   ADS112C14_MUX_CFG_AINP			GENMASK(7, 4)
#define   ADS112C14_MUX_CFG_AINN			GENMASK(3, 0)

#define ADS112C14_REG_GAIN_CFG				0x08
#define   ADS112C14_GAIN_CFG_SPARE			BIT(7)
#define   ADS112C14_GAIN_CFG_SYS_MON			GENMASK(6, 4)
#define   ADS112C14_GAIN_CFG_GAIN			GENMASK(3, 0)

#define ADS112C14_REG_REFERENCE_CFG			0x09
#define   ADS112C14_REFERENCE_CFG_REF_UV_EN		BIT(7)
#define   ADS112C14_REFERENCE_CFG_REFP_BUF_EN		BIT(5)
#define   ADS112C14_REFERENCE_CFG_REFN_BUF_EN		BIT(4)
#define   ADS112C14_REFERENCE_CFG_REF_VAL		BIT(2)
#define     ADS112C14_REFERENCE_CFG_REF_VAL_1_25V	  0
#define     ADS112C14_REFERENCE_CFG_REF_VAL_2_5V	  1
#define   ADS112C14_REFERENCE_CFG_REF_SEL		GENMASK(1, 0)
#define     ADS112C14_REFERENCE_CFG_REF_SEL_INTERNAL	  0
#define     ADS112C14_REFERENCE_CFG_REF_SEL_EXTERNAL	  1
#define     ADS112C14_REFERENCE_CFG_REF_SEL_AVDD	  2

#define ADS112C14_REG_DIGITAL_CFG			0x0A
#define   ADS112C14_DIGITAL_CFG_REG_MAP_CRC_EN		BIT(6)
#define   ADS112C14_DIGITAL_CFG_I2C_CRC_EN		BIT(5)
#define   ADS112C14_DIGITAL_CFG_STATUS_EN		BIT(4)
#define   ADS112C14_DIGITAL_CFG_FAULT_PIN_BEHAVIOR	BIT(3)
#define   ADS112C14_DIGITAL_CFG_CODING			BIT(1)

#define ADS112C14_REG_GPIO_CFG				0x0B
#define   ADS112C14_GPIO_CFG_GPIO3_CFG			GENMASK(7, 6)
#define   ADS112C14_GPIO_CFG_GPIO2_CFG			GENMASK(5, 4)
#define   ADS112C14_GPIO_CFG_GPIO1_CFG			GENMASK(3, 2)
#define   ADS112C14_GPIO_CFG_GPIO0_CFG			GENMASK(1, 0)

#define ADS112C14_REG_GPIO_DATA_OUTPUT			0x0C
#define   ADS112C14_GPIO_DATA_OUTPUT_GPIO3_SRC		BIT(7)
#define   ADS112C14_GPIO_DATA_OUTPUT_GPIO2_SRC		BIT(6)
#define   ADS112C14_GPIO_DATA_OUTPUT_GPIO3_DAT_OUT	BIT(3)
#define   ADS112C14_GPIO_DATA_OUTPUT_GPIO2_DAT_OUT	BIT(2)
#define   ADS112C14_GPIO_DATA_OUTPUT_GPIO1_DAT_OUT	BIT(1)
#define   ADS112C14_GPIO_DATA_OUTPUT_GPIO0_DAT_OUT	BIT(0)

#define ADS112C14_REG_IDAC_MAG_CFG			0x0D
#define   ADS112C14_IDAC_MAG_CFG_I2MAG			GENMASK(7, 4)
#define   ADS112C14_IDAC_MAG_CFG_I1MAG			GENMASK(3, 0)

#define ADS112C14_REG_IDAC_MUX_CFG			0x0E
#define   ADS112C14_IDAC_MUX_CFG_IUNIT			BIT(7)
#define   ADS112C14_IDAC_MUX_CFG_I2MUX			GENMASK(6, 4)
#define   ADS112C14_IDAC_MUX_CFG_I1MUX			GENMASK(2, 0)

#define ADS112C14_REG_REG_MAP_CRC			0x0F

#define ADS112C14_INT_REF0_mV				1250
#define ADS112C14_INT_REF1_mV				2500

struct ads112c14_chip_info {
	const char *name;
	u8 device_id;
	u32 resolution_bits;
};

/* Fixed channels for system monitor measurements. */

#define ADS112C14_SYS_MON_CHANNEL_BASE 100

enum {
	ADS112C14_SYS_MON_CHANNEL_TEMP = ADS112C14_SYS_MON_CHANNEL_BASE,
	ADS112C14_SYS_MON_CHANNEL_EXT_REF,
	ADS112C14_SYS_MON_CHANNEL_AVDD,
	ADS112C14_SYS_MON_CHANNEL_DVDD,
	ADS112C14_SYS_MON_CHANNEL_SHORT,
};

static const struct iio_chan_spec ads112c14_sys_mon_channels[] = {
	{
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = ADS112C14_SYS_MON_CHANNEL_TEMP,
		.address = 2,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
				    | BIT(IIO_CHAN_INFO_SCALE)
				    | BIT(IIO_CHAN_INFO_OFFSET),
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = ADS112C14_SYS_MON_CHANNEL_EXT_REF,
		.address = 3,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
				    | BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = ADS112C14_SYS_MON_CHANNEL_AVDD,
		.address = 4,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
				    | BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = ADS112C14_SYS_MON_CHANNEL_DVDD,
		.address = 5,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
				    | BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = ADS112C14_SYS_MON_CHANNEL_SHORT,
		.channel2 = ADS112C14_SYS_MON_CHANNEL_SHORT,
		.differential = 1,
		.address = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
				    | BIT(IIO_CHAN_INFO_SCALE),
	},
};

struct ads112c14_data {
	const struct ads112c14_chip_info *chip_info;
	struct regmap *regmap;
};

static bool ads112c14_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADS112C14_REG_DEVICE_ID:
	case ADS112C14_REG_REVISION_ID:
	case ADS112C14_REG_STATUS_LSB:
		return false;
	default:
		return true;
	}
}

static bool ads112c14_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADS112C14_REG_STATUS_MSB:
	case ADS112C14_REG_STATUS_LSB:
	case ADS112C14_REG_CONVERSION_CTRL:
		return true;
	default:
		return false;
	}
}

static const struct reg_default ads112c14_reg_defaults[] = {
	{ ADS112C14_REG_DEVICE_CFG, 0 },
	{ ADS112C14_REG_DATA_RATE_CFG, 0 },
	{ ADS112C14_REG_MUX_CFG, 0 },
	{ ADS112C14_REG_GAIN_CFG, FIELD_PREP_CONST(ADS112C14_GAIN_CFG_GAIN, 1) },
	{ ADS112C14_REG_REFERENCE_CFG, 0 },
	{ ADS112C14_REG_DIGITAL_CFG, 0 },
	{ ADS112C14_REG_GPIO_CFG, 0 },
	{ ADS112C14_REG_GPIO_DATA_OUTPUT, 0 },
	{ ADS112C14_REG_IDAC_MAG_CFG, 0 },
	{ ADS112C14_REG_IDAC_MUX_CFG, FIELD_PREP_CONST(ADS112C14_IDAC_MUX_CFG_I2MUX, 1) },
};

static const struct regmap_config ads112c14_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.read_flag_mask = ADS112C14_CMD_RREG,
	.write_flag_mask = ADS112C14_CMD_WREG,
	.max_register = ADS112C14_REG_REG_MAP_CRC,
	.writeable_reg = ads112c14_writeable_reg,
	.volatile_reg = ads112c14_volatile_reg,
	.reg_defaults = ads112c14_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ads112c14_reg_defaults),
	.cache_type = REGCACHE_MAPLE,
};

static int ads112c14_prepare_sys_mon_channel(struct ads112c14_data *data,
					     const struct iio_chan_spec *chan)
{
	int ret;

	/* TODO: GAIN is useful for shorted PGA inputs. */
	/* All SYS_MON channels use GAIN of 1 to keep it simple. */
	ret = regmap_update_bits(data->regmap, ADS112C14_REG_GAIN_CFG,
				 ADS112C14_GAIN_CFG_SYS_MON |
				 ADS112C14_GAIN_CFG_GAIN,
				 FIELD_PREP(ADS112C14_GAIN_CFG_SYS_MON, chan->address) |
				 FIELD_PREP(ADS112C14_GAIN_CFG_GAIN, 1));
	if (ret)
		return ret;

	/* All SYS_MON channels use signed data to keep it simple. */
	ret = regmap_clear_bits(data->regmap, ADS112C14_REG_DIGITAL_CFG,
				ADS112C14_DIGITAL_CFG_CODING);
	if (ret)
		return ret;

	/*
	 * REVISIT: if we implement regulator support for the REFOUT pin, we
	 * might need to make this voltage match what is required by that. In
	 * that case, we could also adjust GAIN so that we still get the same
	 * range.
	 */
	/*
	 * NB: SYS_MON channels ignore REF_SEL except for the shorted input
	 * channel, so we set it here to internal reference to be consistent.
	 * If we ever need to make a measurement of shorted input with other
	 * reference source, we could add additional channels for that.
	 */
	ret = regmap_update_bits(data->regmap, ADS112C14_REG_REFERENCE_CFG,
				 ADS112C14_REFERENCE_CFG_REF_VAL |
				 ADS112C14_REFERENCE_CFG_REF_SEL,
				 FIELD_PREP(ADS112C14_REFERENCE_CFG_REF_VAL,
					    ADS112C14_REFERENCE_CFG_REF_VAL_2_5V) |
				 FIELD_PREP(ADS112C14_REFERENCE_CFG_REF_SEL,
					    ADS112C14_REFERENCE_CFG_REF_SEL_INTERNAL));
	if (ret)
		return ret;

	return 0;
}

static int ads112c14_single_conversion(struct ads112c14_data *data,
				       const struct iio_chan_spec *chan,
				       u8 *buf)
{
	struct i2c_client *client = to_i2c_client(regmap_get_device(data->regmap));
	u32 reg_val;
	int ret;

	if (chan->channel < ADS112C14_SYS_MON_CHANNEL_BASE) {
		/* Not implemented yet. */
		return -EINVAL;
	} else {
		ret = ads112c14_prepare_sys_mon_channel(data, chan);
		if (ret)
			return ret;
	}

	ret = regmap_write(data->regmap, ADS112C14_REG_CONVERSION_CTRL,
			   ADS112C14_CONVERSION_CTRL_START);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(data->regmap,
				       ADS112C14_REG_STATUS_MSB, reg_val,
				       FIELD_GET(ADS112C14_STATUS_MSB_DRDY, reg_val),
				       1 * USEC_PER_MSEC, 100 * USEC_PER_MSEC);
	if (ret)
		return ret;

	ret = i2c_smbus_read_i2c_block_data(client, ADS112C14_CMD_RDATA,
					    BITS_TO_BYTES(data->chip_info->resolution_bits),
					    buf);
	if (ret < 0)
		return ret;

	return 0;
}

static int ads112c14_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct ads112c14_data *data = iio_priv(indio_dev);
	u32 vref_uV, fsr_bits;

	/* Selecting V_REF source is not implemented yet. */
	vref_uV = ADS112C14_INT_REF1_mV * (MICRO / MILLI);

	if (chan->channel < ADS112C14_SYS_MON_CHANNEL_BASE) {
		/* Not implemented yet. */
		return -EINVAL;
	} else {
		/* All SYS_MON channels are using signed coding. */
		fsr_bits = data->chip_info->resolution_bits - 1;
	}

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		u8 buf[3];
		int ret;

		IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
		if (IIO_DEV_ACQUIRE_FAILED(claim))
			return -EBUSY;

		ret = ads112c14_single_conversion(data, chan, buf);
		if (ret)
			return ret;

		switch (data->chip_info->resolution_bits) {
		case 16:
			*val = get_unaligned_be16(buf);
			break;
		case 24:
			*val = get_unaligned_be24(buf);
			break;
		default:
			return -EINVAL;
		}

		*val = sign_extend32(*val, fsr_bits);

		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_TEMP) {
			/* TS_TC (typical) = 405 uV/°C */
			*val = MILLI * vref_uV / 405;
			*val2 = fsr_bits;
			return IIO_VAL_FRACTIONAL_LOG2;
		}

		*val = vref_uV / (MICRO / MILLI);

		/*
		 * Some SYS_MON channels (ext ref, AVDD, DVDD) need to be
		 * multiplied by 8 to account for internal attenuation of / 8.
		 */
		switch (chan->address) {
		case 3 ... 5:
			*val2 = fsr_bits - 3;
			break;
		default:
			*val2 = fsr_bits;
			break;
		}

		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		/* Only the temperature channel has an offset. */
		if (chan->type != IIO_TEMP)
			return -EINVAL;
		/*
		 * Die temperature [°C] = 25°C + (Measured voltage – TS_Offset) / TS_TC
		 * TS_TC (typical) = 405 uV/°C
		 * TS_Offset (typical) = 119.5 mV
		 */
		*val = div_s64((s64)(25 * 405 - 119500) * BIT(fsr_bits), vref_uV);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ads112c14_read_label(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan, char *label)
{
	const char *label_source;

	/* System monitor channels. */
	switch (chan->channel) {
	case ADS112C14_SYS_MON_CHANNEL_TEMP:
		label_source = "Internal temperature sensor";
		break;
	case ADS112C14_SYS_MON_CHANNEL_EXT_REF:
		label_source = "External reference";
		break;
	case ADS112C14_SYS_MON_CHANNEL_AVDD:
		label_source = "AVDD";
		break;
	case ADS112C14_SYS_MON_CHANNEL_DVDD:
		label_source = "DVDD";
		break;
	case ADS112C14_SYS_MON_CHANNEL_SHORT:
		label_source = "Internal short (internal reference source)";
		break;
	default:
		return -EINVAL;
	}

	return sysfs_emit(label, "%s\n", label_source);
}

static const struct iio_info ads112c14_info = {
	.read_raw = ads112c14_read_raw,
	.read_label = ads112c14_read_label,
};

static int ads112c14_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	const struct ads112c14_chip_info *info;
	struct iio_dev *indio_dev;
	struct ads112c14_data *data;
	u32 reg_val;
	int ret;

	info = i2c_get_match_data(client);
	if (!info)
		return dev_err_probe(dev, -ENODEV, "missing match data\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->chip_info = info;

	ret = devm_regulator_get_enable(dev, "dvdd");
	if (ret)
		return dev_err_probe(dev, ret, "failed to get dvdd regulator\n");

	ret = devm_regulator_get_enable(dev, "avdd");
	if (ret)
		return dev_err_probe(dev, ret, "failed to get avdd regulator\n");

	/* It takes some time for the internal reference to stabilize. */
	fsleep(10 * USEC_PER_MSEC);

	data->regmap = devm_regmap_init_i2c(client, &ads112c14_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "failed to init regmap\n");

	/*
	 * Write magic reset value (0x16) to ensure known state. The reset may
	 * cause an error because of failing to get the I2C ACK at the end of
	 * the message. The device still gets reset so it is safe to ignore the
	 * return value here. If something else is wrong, later read/write will
	 * likely have the same error.
	 */
	regmap_write(data->regmap, ADS112C14_REG_CONVERSION_CTRL,
		     FIELD_PREP(ADS112C14_CONVERSION_CTRL_RESET, 0x16));

	fsleep(ADS112C14_DELAY_RESET_US);

	ret = regmap_read(data->regmap, ADS112C14_REG_STATUS_MSB, &reg_val);
	if (ret)
		return ret;

	if (FIELD_GET(ADS112C14_STATUS_MSB_RESETN, reg_val))
		return dev_err_probe(dev, -EIO, "reset failed\n");

	/*
	 * Clear reset bit to prepare for next probe. And clear AVDD fault since
	 * that happens on every reset.
	 */
	ret = regmap_write(data->regmap, ADS112C14_REG_STATUS_MSB,
			   ADS112C14_STATUS_MSB_RESETN |
			   ADS112C14_STATUS_MSB_AVDD_UVN);
	if (ret)
		return ret;

	ret = regmap_read(data->regmap, ADS112C14_REG_DEVICE_ID, &reg_val);
	if (ret)
		return ret;

	if (FIELD_GET(ADS112C14_DEVICE_ID_BITS, reg_val) != info->device_id)
		dev_info(dev, "device ID mismatch, expected 0x%X, got 0x%lX\n",
			 info->device_id,
			 FIELD_GET(ADS112C14_DEVICE_ID_BITS, reg_val));

	ret = regmap_update_bits(data->regmap, ADS112C14_REG_DEVICE_CFG,
				 ADS112C14_DEVICE_CFG_CONV_MODE,
				 FIELD_PREP(ADS112C14_DEVICE_CFG_CONV_MODE,
					    ADS112C14_DEVICE_CFG_CONV_MODE_SINGLE_SHOT));
	if (ret)
		return ret;

	indio_dev->name = info->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ads112c14_sys_mon_channels;
	indio_dev->num_channels = ARRAY_SIZE(ads112c14_sys_mon_channels);
	indio_dev->info = &ads112c14_info;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct ads112c14_chip_info ads112c14_chip_info = {
	.name = "ads112c14",
	.device_id = 0xE,
	.resolution_bits = 16,
};

static const struct ads112c14_chip_info ads122c14_chip_info = {
	.name = "ads122c14",
	.device_id = 0xF,
	.resolution_bits = 24,
};

static const struct of_device_id ads112c14_of_match[] = {
	{ .compatible = "ti,ads112c14", .data = &ads112c14_chip_info },
	{ .compatible = "ti,ads122c14", .data = &ads122c14_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ads112c14_of_match);

static const struct i2c_device_id ads112c14_id[] = {
	{ .name = "ads112c14", .driver_data = (kernel_ulong_t)&ads112c14_chip_info },
	{ .name = "ads122c14", .driver_data = (kernel_ulong_t)&ads122c14_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ads112c14_id);

static struct i2c_driver ads112c14_driver = {
	.driver = {
		.name = "ads112c14",
		.of_match_table = ads112c14_of_match,
	},
	.probe = ads112c14_probe,
	.id_table = ads112c14_id,
};
module_i2c_driver(ads112c14_driver);

MODULE_AUTHOR("David Lechner (TI) <dlechner@baylibre.com>");
MODULE_DESCRIPTION("TI ADS112C14 I2C ADC driver");
MODULE_LICENSE("GPL");
