// SPDX-License-Identifier: GPL-2.0-only
/* ADC driver for AXP20X and AXP22X PMICs
 *
 * Copyright (c) 2016 Free Electrons NextThing Co.
 *	Quentin Schulz <quentin.schulz@free-electrons.com>
 */

#include <linux/unaligned.h>
#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/thermal.h>

#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/iio/machine.h>
#include <linux/mfd/axp20x.h>

#define AXP192_ADC_EN1_MASK			GENMASK(7, 0)
#define AXP192_ADC_EN2_MASK			(GENMASK(3, 0) | BIT(7))

#define AXP20X_ADC_EN1_MASK			GENMASK(7, 0)
#define AXP20X_ADC_EN2_MASK			(GENMASK(3, 2) | BIT(7))

#define AXP22X_ADC_EN1_MASK			(GENMASK(7, 5) | BIT(0))

#define AXP717_ADC_EN1_MASK			GENMASK(7, 0)

#define AXP192_GPIO30_IN_RANGE_GPIO0		BIT(0)
#define AXP192_GPIO30_IN_RANGE_GPIO1		BIT(1)
#define AXP192_GPIO30_IN_RANGE_GPIO2		BIT(2)
#define AXP192_GPIO30_IN_RANGE_GPIO3		BIT(3)

#define AXP20X_GPIO10_IN_RANGE_GPIO0		BIT(0)
#define AXP20X_GPIO10_IN_RANGE_GPIO1		BIT(1)

#define AXP20X_ADC_RATE_MASK			GENMASK(7, 6)
#define AXP20X_ADC_RATE_HZ(x)			((ilog2((x) / 25) << 6) & AXP20X_ADC_RATE_MASK)

#define AXP22X_ADC_RATE_HZ(x)			((ilog2((x) / 100) << 6) & AXP20X_ADC_RATE_MASK)

#define AXP717_ADC_DATA_TS			0x00
#define AXP717_ADC_DATA_TEMP			0x01
#define AXP717_ADC_DATA_VMID			0x02
#define AXP717_ADC_DATA_BKUP_BATT		0x03

#define AXP717_ADC_DATA_MASK			GENMASK(13, 0)

#define AXP813_V_I_ADC_RATE_MASK		GENMASK(5, 4)
#define AXP813_ADC_RATE_MASK			(AXP20X_ADC_RATE_MASK | AXP813_V_I_ADC_RATE_MASK)
#define AXP813_TS_GPIO0_ADC_RATE_HZ(x)		AXP20X_ADC_RATE_HZ(x)
#define AXP813_V_I_ADC_RATE_HZ(x)		((ilog2((x) / 100) << 4) & AXP813_V_I_ADC_RATE_MASK)
#define AXP813_ADC_RATE_HZ(x)			(AXP20X_ADC_RATE_HZ(x) | AXP813_V_I_ADC_RATE_HZ(x))

#define AXP20X_ADC_CHANNEL(_channel, _name, _type, _reg)	\
	{							\
		.type = _type,					\
		.indexed = 1,					\
		.channel = _channel,				\
		.address = _reg,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
				      BIT(IIO_CHAN_INFO_SCALE),	\
		.datasheet_name = _name,			\
	}

#define AXP20X_ADC_CHANNEL_OFFSET(_channel, _name, _type, _reg) \
	{							\
		.type = _type,					\
		.indexed = 1,					\
		.channel = _channel,				\
		.address = _reg,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
				      BIT(IIO_CHAN_INFO_SCALE) |\
				      BIT(IIO_CHAN_INFO_OFFSET),\
		.datasheet_name = _name,			\
	}

struct axp_data;

struct axp20x_adc_iio {
	struct regmap		*regmap;
	const struct axp_data	*data;
};

enum axp192_adc_channel_v {
	AXP192_ACIN_V = 0,
	AXP192_VBUS_V,
	AXP192_TS_IN,
	AXP192_GPIO0_V,
	AXP192_GPIO1_V,
	AXP192_GPIO2_V,
	AXP192_GPIO3_V,
	AXP192_IPSOUT_V,
	AXP192_BATT_V,
};

enum axp192_adc_channel_i {
	AXP192_ACIN_I = 0,
	AXP192_VBUS_I,
	AXP192_BATT_CHRG_I,
	AXP192_BATT_DISCHRG_I,
};

enum axp20x_adc_channel_v {
	AXP20X_ACIN_V = 0,
	AXP20X_VBUS_V,
	AXP20X_TS_IN,
	AXP20X_GPIO0_V,
	AXP20X_GPIO1_V,
	AXP20X_IPSOUT_V,
	AXP20X_BATT_V,
};

enum axp20x_adc_channel_i {
	AXP20X_ACIN_I = 0,
	AXP20X_VBUS_I,
	AXP20X_BATT_CHRG_I,
	AXP20X_BATT_DISCHRG_I,
};

enum axp22x_adc_channel_v {
	AXP22X_TS_IN = 0,
	AXP22X_BATT_V,
};

enum axp22x_adc_channel_i {
	AXP22X_BATT_CHRG_I = 1,
	AXP22X_BATT_DISCHRG_I,
};

enum axp717_adc_channel_v {
	AXP717_BATT_V = 0,
	AXP717_TS_IN,
	AXP717_VBUS_V,
	AXP717_VSYS_V,
	AXP717_DIE_TEMP_V,
	AXP717_VMID_V = 6,
	AXP717_BKUP_BATT_V,
};

enum axp717_adc_channel_i {
	AXP717_BATT_CHRG_I = 5,
};

enum axp813_adc_channel_v {
	AXP813_TS_IN = 0,
	AXP813_GPIO0_V,
	AXP813_BATT_V,
};

static struct iio_map axp20x_maps[] = {
	{
		.consumer_dev_name = "axp20x-usb-power-supply",
		.consumer_channel = "vbus_v",
		.adc_channel_label = "vbus_v",
	}, {
		.consumer_dev_name = "axp20x-usb-power-supply",
		.consumer_channel = "vbus_i",
		.adc_channel_label = "vbus_i",
	}, {
		.consumer_dev_name = "axp20x-ac-power-supply",
		.consumer_channel = "acin_v",
		.adc_channel_label = "acin_v",
	}, {
		.consumer_dev_name = "axp20x-ac-power-supply",
		.consumer_channel = "acin_i",
		.adc_channel_label = "acin_i",
	}, {
		.consumer_dev_name = "axp20x-battery-power-supply",
		.consumer_channel = "batt_v",
		.adc_channel_label = "batt_v",
	}, {
		.consumer_dev_name = "axp20x-battery-power-supply",
		.consumer_channel = "batt_chrg_i",
		.adc_channel_label = "batt_chrg_i",
	}, {
		.consumer_dev_name = "axp20x-battery-power-supply",
		.consumer_channel = "batt_dischrg_i",
		.adc_channel_label = "batt_dischrg_i",
	}, { /* sentinel */ }
};

static struct iio_map axp22x_maps[] = {
	{
		.consumer_dev_name = "axp20x-battery-power-supply",
		.consumer_channel = "batt_v",
		.adc_channel_label = "batt_v",
	}, {
		.consumer_dev_name = "axp20x-battery-power-supply",
		.consumer_channel = "batt_chrg_i",
		.adc_channel_label = "batt_chrg_i",
	}, {
		.consumer_dev_name = "axp20x-battery-power-supply",
		.consumer_channel = "batt_dischrg_i",
		.adc_channel_label = "batt_dischrg_i",
	}, { /* sentinel */ }
};

static struct iio_map axp717_maps[] = {
	{
		.consumer_dev_name = "axp20x-usb-power-supply",
		.consumer_channel = "vbus_v",
		.adc_channel_label = "vbus_v",
	}, {
		.consumer_dev_name = "axp20x-battery-power-supply",
		.consumer_channel = "batt_v",
		.adc_channel_label = "batt_v",
	}, {
		.consumer_dev_name = "axp20x-battery-power-supply",
		.consumer_channel = "batt_chrg_i",
		.adc_channel_label = "batt_chrg_i",
	},
};

/*
 * Channels are mapped by physical system. Their channels share the same index.
 * i.e. acin_i is in_current0_raw and acin_v is in_voltage0_raw.
 * The only exception is for the battery. batt_v will be in_voltage6_raw and
 * charge current in_current6_raw and discharge current will be in_current7_raw.
 */
static const struct iio_chan_spec axp192_adc_channels[] = {
	AXP20X_ADC_CHANNEL(AXP192_ACIN_V, "acin_v", IIO_VOLTAGE,
			   AXP20X_ACIN_V_ADC_H),
	AXP20X_ADC_CHANNEL(AXP192_ACIN_I, "acin_i", IIO_CURRENT,
			   AXP20X_ACIN_I_ADC_H),
	AXP20X_ADC_CHANNEL(AXP192_VBUS_V, "vbus_v", IIO_VOLTAGE,
			   AXP20X_VBUS_V_ADC_H),
	AXP20X_ADC_CHANNEL(AXP192_VBUS_I, "vbus_i", IIO_CURRENT,
			   AXP20X_VBUS_I_ADC_H),
	{
		.type = IIO_TEMP,
		.address = AXP20X_TEMP_ADC_H,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
		.datasheet_name = "pmic_temp",
	},
	AXP20X_ADC_CHANNEL_OFFSET(AXP192_GPIO0_V, "gpio0_v", IIO_VOLTAGE,
				  AXP20X_GPIO0_V_ADC_H),
	AXP20X_ADC_CHANNEL_OFFSET(AXP192_GPIO1_V, "gpio1_v", IIO_VOLTAGE,
				  AXP20X_GPIO1_V_ADC_H),
	AXP20X_ADC_CHANNEL_OFFSET(AXP192_GPIO2_V, "gpio2_v", IIO_VOLTAGE,
				  AXP192_GPIO2_V_ADC_H),
	AXP20X_ADC_CHANNEL_OFFSET(AXP192_GPIO3_V, "gpio3_v", IIO_VOLTAGE,
				  AXP192_GPIO3_V_ADC_H),
	AXP20X_ADC_CHANNEL(AXP192_IPSOUT_V, "ipsout_v", IIO_VOLTAGE,
			   AXP20X_IPSOUT_V_HIGH_H),
	AXP20X_ADC_CHANNEL(AXP192_BATT_V, "batt_v", IIO_VOLTAGE,
			   AXP20X_BATT_V_H),
	AXP20X_ADC_CHANNEL(AXP192_BATT_CHRG_I, "batt_chrg_i", IIO_CURRENT,
			   AXP20X_BATT_CHRG_I_H),
	AXP20X_ADC_CHANNEL(AXP192_BATT_DISCHRG_I, "batt_dischrg_i", IIO_CURRENT,
			   AXP20X_BATT_DISCHRG_I_H),
	AXP20X_ADC_CHANNEL(AXP192_TS_IN, "ts_v", IIO_VOLTAGE,
			   AXP20X_TS_IN_H),
};

static const struct iio_chan_spec axp20x_adc_channels[] = {
	AXP20X_ADC_CHANNEL(AXP20X_ACIN_V, "acin_v", IIO_VOLTAGE,
			   AXP20X_ACIN_V_ADC_H),
	AXP20X_ADC_CHANNEL(AXP20X_ACIN_I, "acin_i", IIO_CURRENT,
			   AXP20X_ACIN_I_ADC_H),
	AXP20X_ADC_CHANNEL(AXP20X_VBUS_V, "vbus_v", IIO_VOLTAGE,
			   AXP20X_VBUS_V_ADC_H),
	AXP20X_ADC_CHANNEL(AXP20X_VBUS_I, "vbus_i", IIO_CURRENT,
			   AXP20X_VBUS_I_ADC_H),
	{
		.type = IIO_TEMP,
		.address = AXP20X_TEMP_ADC_H,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
		.datasheet_name = "pmic_temp",
	},
	AXP20X_ADC_CHANNEL_OFFSET(AXP20X_GPIO0_V, "gpio0_v", IIO_VOLTAGE,
				  AXP20X_GPIO0_V_ADC_H),
	AXP20X_ADC_CHANNEL_OFFSET(AXP20X_GPIO1_V, "gpio1_v", IIO_VOLTAGE,
				  AXP20X_GPIO1_V_ADC_H),
	AXP20X_ADC_CHANNEL(AXP20X_IPSOUT_V, "ipsout_v", IIO_VOLTAGE,
			   AXP20X_IPSOUT_V_HIGH_H),
	AXP20X_ADC_CHANNEL(AXP20X_BATT_V, "batt_v", IIO_VOLTAGE,
			   AXP20X_BATT_V_H),
	AXP20X_ADC_CHANNEL(AXP20X_BATT_CHRG_I, "batt_chrg_i", IIO_CURRENT,
			   AXP20X_BATT_CHRG_I_H),
	AXP20X_ADC_CHANNEL(AXP20X_BATT_DISCHRG_I, "batt_dischrg_i", IIO_CURRENT,
			   AXP20X_BATT_DISCHRG_I_H),
	AXP20X_ADC_CHANNEL(AXP20X_TS_IN, "ts_v", IIO_VOLTAGE,
			   AXP20X_TS_IN_H),
};

static const struct iio_chan_spec axp22x_adc_channels[] = {
	{
		.type = IIO_TEMP,
		.address = AXP22X_PMIC_TEMP_H,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
		.datasheet_name = "pmic_temp",
	},
	AXP20X_ADC_CHANNEL(AXP22X_BATT_V, "batt_v", IIO_VOLTAGE,
			   AXP20X_BATT_V_H),
	AXP20X_ADC_CHANNEL(AXP22X_BATT_CHRG_I, "batt_chrg_i", IIO_CURRENT,
			   AXP20X_BATT_CHRG_I_H),
	AXP20X_ADC_CHANNEL(AXP22X_BATT_DISCHRG_I, "batt_dischrg_i", IIO_CURRENT,
			   AXP20X_BATT_DISCHRG_I_H),
	AXP20X_ADC_CHANNEL(AXP22X_TS_IN, "ts_v", IIO_VOLTAGE,
			   AXP22X_TS_ADC_H),
};

/*
 * Scale and offset is unknown for temp, ts, batt_chrg_i, vmid_v, and
 * bkup_batt_v channels. Leaving scale and offset undefined for now.
 */
static const struct iio_chan_spec axp717_adc_channels[] = {
	AXP20X_ADC_CHANNEL(AXP717_BATT_V, "batt_v", IIO_VOLTAGE,
			   AXP717_BATT_V_H),
	AXP20X_ADC_CHANNEL(AXP717_TS_IN, "ts_v", IIO_VOLTAGE,
			   AXP717_ADC_DATA_H),
	AXP20X_ADC_CHANNEL(AXP717_VBUS_V, "vbus_v", IIO_VOLTAGE,
			   AXP717_VBUS_V_H),
	AXP20X_ADC_CHANNEL(AXP717_VSYS_V, "vsys_v", IIO_VOLTAGE,
			   AXP717_VSYS_V_H),
	AXP20X_ADC_CHANNEL(AXP717_DIE_TEMP_V, "pmic_temp", IIO_TEMP,
			   AXP717_ADC_DATA_H),
	AXP20X_ADC_CHANNEL(AXP717_BATT_CHRG_I, "batt_chrg_i", IIO_CURRENT,
			   AXP717_BATT_CHRG_I_H),
	AXP20X_ADC_CHANNEL(AXP717_VMID_V, "vmid_v", IIO_VOLTAGE,
			   AXP717_ADC_DATA_H),
	AXP20X_ADC_CHANNEL(AXP717_BKUP_BATT_V, "bkup_batt_v", IIO_VOLTAGE,
			   AXP717_ADC_DATA_H),
};

static const struct iio_chan_spec axp813_adc_channels[] = {
	{
		.type = IIO_TEMP,
		.address = AXP22X_PMIC_TEMP_H,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
		.datasheet_name = "pmic_temp",
	},
	AXP20X_ADC_CHANNEL(AXP813_GPIO0_V, "gpio0_v", IIO_VOLTAGE,
			   AXP288_GP_ADC_H),
	AXP20X_ADC_CHANNEL(AXP813_BATT_V, "batt_v", IIO_VOLTAGE,
			   AXP20X_BATT_V_H),
	AXP20X_ADC_CHANNEL(AXP22X_BATT_CHRG_I, "batt_chrg_i", IIO_CURRENT,
			   AXP20X_BATT_CHRG_I_H),
	AXP20X_ADC_CHANNEL(AXP22X_BATT_DISCHRG_I, "batt_dischrg_i", IIO_CURRENT,
			   AXP20X_BATT_DISCHRG_I_H),
	AXP20X_ADC_CHANNEL(AXP813_TS_IN, "ts_v", IIO_VOLTAGE,
			   AXP288_TS_ADC_H),
};

static int axp192_adc_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan, int *val)
{
	struct axp20x_adc_iio *info = iio_priv(indio_dev);
	int ret, size;

	if (chan->type == IIO_CURRENT &&
	    (chan->channel == AXP192_BATT_CHRG_I ||
	     chan->channel == AXP192_BATT_DISCHRG_I))
		size = 13;
	else
		size = 12;

	ret = axp20x_read_variable_width(info->regmap, chan->address, size);
	if (ret < 0)
		return ret;

	*val = ret;
	return IIO_VAL_INT;
}

static int axp20x_adc_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan, int *val)
{
	struct axp20x_adc_iio *info = iio_priv(indio_dev);
	int ret, size;

	/*
	 * N.B.:  Unlike the Chinese datasheets tell, the charging current is
	 * stored on 12 bits, not 13 bits. Only discharging current is on 13
	 * bits.
	 */
	if (chan->type == IIO_CURRENT && chan->channel == AXP20X_BATT_DISCHRG_I)
		size = 13;
	else
		size = 12;

	ret = axp20x_read_variable_width(info->regmap, chan->address, size);
	if (ret < 0)
		return ret;

	*val = ret;
	return IIO_VAL_INT;
}

static int axp22x_adc_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan, int *val)
{
	struct axp20x_adc_iio *info = iio_priv(indio_dev);
	int ret;

	ret = axp20x_read_variable_width(info->regmap, chan->address, 12);
	if (ret < 0)
		return ret;

	*val = ret;
	return IIO_VAL_INT;
}

static int axp717_adc_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan, int *val)
{
	struct axp20x_adc_iio *info = iio_priv(indio_dev);
	u8 bulk_reg[2];
	int ret;

	/*
	 * A generic "ADC data" channel is used for TS, tdie, vmid,
	 * and vbackup. This channel must both first be enabled and
	 * also selected before it can be read.
	 */
	switch (chan->channel) {
	case AXP717_TS_IN:
		regmap_write(info->regmap, AXP717_ADC_DATA_SEL,
			     AXP717_ADC_DATA_TS);
		break;
	case AXP717_DIE_TEMP_V:
		regmap_write(info->regmap, AXP717_ADC_DATA_SEL,
			     AXP717_ADC_DATA_TEMP);
		break;
	case AXP717_VMID_V:
		regmap_write(info->regmap, AXP717_ADC_DATA_SEL,
			     AXP717_ADC_DATA_VMID);
		break;
	case AXP717_BKUP_BATT_V:
		regmap_write(info->regmap, AXP717_ADC_DATA_SEL,
			     AXP717_ADC_DATA_BKUP_BATT);
		break;
	default:
		break;
	}

	/*
	 * All channels are 14 bits, with the first 2 bits on the high
	 * register reserved and the remaining bits as the ADC value.
	 */
	ret = regmap_bulk_read(info->regmap, chan->address, bulk_reg, 2);
	if (ret < 0)
		return ret;

	*val = FIELD_GET(AXP717_ADC_DATA_MASK, get_unaligned_be16(bulk_reg));
	return IIO_VAL_INT;
}

static int axp813_adc_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan, int *val)
{
	struct axp20x_adc_iio *info = iio_priv(indio_dev);
	int ret;

	ret = axp20x_read_variable_width(info->regmap, chan->address, 12);
	if (ret < 0)
		return ret;

	*val = ret;
	return IIO_VAL_INT;
}

static int axp192_adc_scale_voltage(int channel, int *val, int *val2)
{
	switch (channel) {
	case AXP192_ACIN_V:
	case AXP192_VBUS_V:
		*val = 1;
		*val2 = 700000;
		return IIO_VAL_INT_PLUS_MICRO;

	case AXP192_GPIO0_V:
	case AXP192_GPIO1_V:
	case AXP192_GPIO2_V:
	case AXP192_GPIO3_V:
		*val = 0;
		*val2 = 500000;
		return IIO_VAL_INT_PLUS_MICRO;

	case AXP192_BATT_V:
		*val = 1;
		*val2 = 100000;
		return IIO_VAL_INT_PLUS_MICRO;

	case AXP192_IPSOUT_V:
		*val = 1;
		*val2 = 400000;
		return IIO_VAL_INT_PLUS_MICRO;

	case AXP192_TS_IN:
		/* 0.8 mV per LSB */
		*val = 0;
		*val2 = 800000;
		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}
}

static int axp20x_adc_scale_voltage(int channel, int *val, int *val2)
{
	switch (channel) {
	case AXP20X_ACIN_V:
	case AXP20X_VBUS_V:
		*val = 1;
		*val2 = 700000;
		return IIO_VAL_INT_PLUS_MICRO;

	case AXP20X_GPIO0_V:
	case AXP20X_GPIO1_V:
		*val = 0;
		*val2 = 500000;
		return IIO_VAL_INT_PLUS_MICRO;

	case AXP20X_BATT_V:
		*val = 1;
		*val2 = 100000;
		return IIO_VAL_INT_PLUS_MICRO;

	case AXP20X_IPSOUT_V:
		*val = 1;
		*val2 = 400000;
		return IIO_VAL_INT_PLUS_MICRO;

	case AXP20X_TS_IN:
		/* 0.8 mV per LSB */
		*val = 0;
		*val2 = 800000;
		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}
}

static int axp22x_adc_scale_voltage(int channel, int *val, int *val2)
{
	switch (channel) {
	case AXP22X_BATT_V:
		/* 1.1 mV per LSB */
		*val = 1;
		*val2 = 100000;
		return IIO_VAL_INT_PLUS_MICRO;

	case AXP22X_TS_IN:
		/* 0.8 mV per LSB */
		*val = 0;
		*val2 = 800000;
		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}
}
static int axp813_adc_scale_voltage(int channel, int *val, int *val2)
{
	switch (channel) {
	case AXP813_GPIO0_V:
		*val = 0;
		*val2 = 800000;
		return IIO_VAL_INT_PLUS_MICRO;

	case AXP813_BATT_V:
		*val = 1;
		*val2 = 100000;
		return IIO_VAL_INT_PLUS_MICRO;

	case AXP813_TS_IN:
		/* 0.8 mV per LSB */
		*val = 0;
		*val2 = 800000;
		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}
}

static int axp20x_adc_scale_current(int channel, int *val, int *val2)
{
	switch (channel) {
	case AXP20X_ACIN_I:
		*val = 0;
		*val2 = 625000;
		return IIO_VAL_INT_PLUS_MICRO;

	case AXP20X_VBUS_I:
		*val = 0;
		*val2 = 375000;
		return IIO_VAL_INT_PLUS_MICRO;

	case AXP20X_BATT_DISCHRG_I:
	case AXP20X_BATT_CHRG_I:
		*val = 0;
		*val2 = 500000;
		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}
}

static int axp192_adc_scale(struct iio_chan_spec const *chan, int *val,
			    int *val2)
{
	switch (chan->type) {
	case IIO_VOLTAGE:
		return axp192_adc_scale_voltage(chan->channel, val, val2);

	case IIO_CURRENT:
		/*
		 * AXP192 current channels are identical to the AXP20x,
		 * therefore we can re-use the scaling function.
		 */
		return axp20x_adc_scale_current(chan->channel, val, val2);

	case IIO_TEMP:
		*val = 100;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int axp20x_adc_scale(struct iio_chan_spec const *chan, int *val,
			    int *val2)
{
	switch (chan->type) {
	case IIO_VOLTAGE:
		return axp20x_adc_scale_voltage(chan->channel, val, val2);

	case IIO_CURRENT:
		return axp20x_adc_scale_current(chan->channel, val, val2);

	case IIO_TEMP:
		*val = 100;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int axp22x_adc_scale(struct iio_chan_spec const *chan, int *val,
			    int *val2)
{
	switch (chan->type) {
	case IIO_VOLTAGE:
		return axp22x_adc_scale_voltage(chan->channel, val, val2);

	case IIO_CURRENT:
		*val = 1;
		return IIO_VAL_INT;

	case IIO_TEMP:
		*val = 100;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int axp717_adc_scale(struct iio_chan_spec const *chan, int *val,
			    int *val2)
{
	switch (chan->type) {
	case IIO_VOLTAGE:
		*val = 1;
		return IIO_VAL_INT;

	case IIO_CURRENT:
		*val = 1;
		return IIO_VAL_INT;

	case IIO_TEMP:
		*val = 100;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int axp813_adc_scale(struct iio_chan_spec const *chan, int *val,
			    int *val2)
{
	switch (chan->type) {
	case IIO_VOLTAGE:
		return axp813_adc_scale_voltage(chan->channel, val, val2);

	case IIO_CURRENT:
		*val = 1;
		return IIO_VAL_INT;

	case IIO_TEMP:
		*val = 100;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int axp192_adc_offset_voltage(struct iio_dev *indio_dev, int channel,
				     int *val)
{
	struct axp20x_adc_iio *info = iio_priv(indio_dev);
	unsigned int regval;
	int ret;

	ret = regmap_read(info->regmap, AXP192_GPIO30_IN_RANGE, &regval);
	if (ret < 0)
		return ret;

	switch (channel) {
	case AXP192_GPIO0_V:
		regval = FIELD_GET(AXP192_GPIO30_IN_RANGE_GPIO0, regval);
		break;

	case AXP192_GPIO1_V:
		regval = FIELD_GET(AXP192_GPIO30_IN_RANGE_GPIO1, regval);
		break;

	case AXP192_GPIO2_V:
		regval = FIELD_GET(AXP192_GPIO30_IN_RANGE_GPIO2, regval);
		break;

	case AXP192_GPIO3_V:
		regval = FIELD_GET(AXP192_GPIO30_IN_RANGE_GPIO3, regval);
		break;

	default:
		return -EINVAL;
	}

	*val = regval ? 700000 : 0;
	return IIO_VAL_INT;
}

static int axp20x_adc_offset_voltage(struct iio_dev *indio_dev, int channel,
				     int *val)
{
	struct axp20x_adc_iio *info = iio_priv(indio_dev);
	unsigned int regval;
	int ret;

	ret = regmap_read(info->regmap, AXP20X_GPIO10_IN_RANGE, &regval);
	if (ret < 0)
		return ret;

	switch (channel) {
	case AXP20X_GPIO0_V:
		regval = FIELD_GET(AXP20X_GPIO10_IN_RANGE_GPIO0, regval);
		break;

	case AXP20X_GPIO1_V:
		regval = FIELD_GET(AXP20X_GPIO10_IN_RANGE_GPIO1, regval);
		break;

	default:
		return -EINVAL;
	}

	*val = regval ? 700000 : 0;
	return IIO_VAL_INT;
}

static int axp192_adc_offset(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val)
{
	switch (chan->type) {
	case IIO_VOLTAGE:
		return axp192_adc_offset_voltage(indio_dev, chan->channel, val);

	case IIO_TEMP:
		*val = -1447;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int axp20x_adc_offset(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val)
{
	switch (chan->type) {
	case IIO_VOLTAGE:
		return axp20x_adc_offset_voltage(indio_dev, chan->channel, val);

	case IIO_TEMP:
		*val = -1447;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int axp192_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_OFFSET:
		return axp192_adc_offset(indio_dev, chan, val);

	case IIO_CHAN_INFO_SCALE:
		return axp192_adc_scale(chan, val, val2);

	case IIO_CHAN_INFO_RAW:
		return axp192_adc_raw(indio_dev, chan, val);

	default:
		return -EINVAL;
	}
}

static int axp20x_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_OFFSET:
		return axp20x_adc_offset(indio_dev, chan, val);

	case IIO_CHAN_INFO_SCALE:
		return axp20x_adc_scale(chan, val, val2);

	case IIO_CHAN_INFO_RAW:
		return axp20x_adc_raw(indio_dev, chan, val);

	default:
		return -EINVAL;
	}
}

static int axp22x_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_OFFSET:
		/* For PMIC temp only */
		*val = -2677;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		return axp22x_adc_scale(chan, val, val2);

	case IIO_CHAN_INFO_RAW:
		return axp22x_adc_raw(indio_dev, chan, val);

	default:
		return -EINVAL;
	}
}

static int axp717_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return axp717_adc_scale(chan, val, val2);

	case IIO_CHAN_INFO_RAW:
		return axp717_adc_raw(indio_dev, chan, val);

	default:
		return -EINVAL;
	}
}

static int axp813_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_OFFSET:
		*val = -2667;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		return axp813_adc_scale(chan, val, val2);

	case IIO_CHAN_INFO_RAW:
		return axp813_adc_raw(indio_dev, chan, val);

	default:
		return -EINVAL;
	}
}

static int axp192_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val, int val2,
			    long mask)
{
	struct axp20x_adc_iio *info = iio_priv(indio_dev);
	unsigned int regmask, regval;

	/*
	 * The AXP192 PMIC allows the user to choose between 0V and 0.7V offsets
	 * for (independently) GPIO0-3 when in ADC mode.
	 */
	if (mask != IIO_CHAN_INFO_OFFSET)
		return -EINVAL;

	if (val != 0 && val != 700000)
		return -EINVAL;

	switch (chan->channel) {
	case AXP192_GPIO0_V:
		regmask = AXP192_GPIO30_IN_RANGE_GPIO0;
		regval = FIELD_PREP(AXP192_GPIO30_IN_RANGE_GPIO0, !!val);
		break;

	case AXP192_GPIO1_V:
		regmask = AXP192_GPIO30_IN_RANGE_GPIO1;
		regval = FIELD_PREP(AXP192_GPIO30_IN_RANGE_GPIO1, !!val);
		break;

	case AXP192_GPIO2_V:
		regmask = AXP192_GPIO30_IN_RANGE_GPIO2;
		regval = FIELD_PREP(AXP192_GPIO30_IN_RANGE_GPIO2, !!val);
		break;

	case AXP192_GPIO3_V:
		regmask = AXP192_GPIO30_IN_RANGE_GPIO3;
		regval = FIELD_PREP(AXP192_GPIO30_IN_RANGE_GPIO3, !!val);
		break;

	default:
		return -EINVAL;
	}

	return regmap_update_bits(info->regmap, AXP192_GPIO30_IN_RANGE, regmask, regval);
}

static int axp20x_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val, int val2,
			    long mask)
{
	struct axp20x_adc_iio *info = iio_priv(indio_dev);
	unsigned int regmask, regval;

	/*
	 * The AXP20X PMIC allows the user to choose between 0V and 0.7V offsets
	 * for (independently) GPIO0 and GPIO1 when in ADC mode.
	 */
	if (mask != IIO_CHAN_INFO_OFFSET)
		return -EINVAL;

	if (val != 0 && val != 700000)
		return -EINVAL;

	switch (chan->channel) {
	case AXP20X_GPIO0_V:
		regmask = AXP20X_GPIO10_IN_RANGE_GPIO0;
		regval = FIELD_PREP(AXP20X_GPIO10_IN_RANGE_GPIO0, !!val);
		break;

	case AXP20X_GPIO1_V:
		regmask = AXP20X_GPIO10_IN_RANGE_GPIO1;
		regval = FIELD_PREP(AXP20X_GPIO10_IN_RANGE_GPIO1, !!val);
		break;

	default:
		return -EINVAL;
	}

	return regmap_update_bits(info->regmap, AXP20X_GPIO10_IN_RANGE, regmask, regval);
}

static const struct iio_info axp192_adc_iio_info = {
	.read_raw = axp192_read_raw,
	.write_raw = axp192_write_raw,
};

static const struct iio_info axp20x_adc_iio_info = {
	.read_raw = axp20x_read_raw,
	.write_raw = axp20x_write_raw,
};

static const struct iio_info axp22x_adc_iio_info = {
	.read_raw = axp22x_read_raw,
};

static const struct iio_info axp717_adc_iio_info = {
	.read_raw = axp717_read_raw,
};

static const struct iio_info axp813_adc_iio_info = {
	.read_raw = axp813_read_raw,
};

static int axp20x_adc_rate(struct axp20x_adc_iio *info, int rate)
{
	return regmap_update_bits(info->regmap, AXP20X_ADC_RATE,
				  AXP20X_ADC_RATE_MASK,
				  AXP20X_ADC_RATE_HZ(rate));
}

static int axp22x_adc_rate(struct axp20x_adc_iio *info, int rate)
{
	return regmap_update_bits(info->regmap, AXP20X_ADC_RATE,
				  AXP20X_ADC_RATE_MASK,
				  AXP22X_ADC_RATE_HZ(rate));
}

static int axp813_adc_rate(struct axp20x_adc_iio *info, int rate)
{
	return regmap_update_bits(info->regmap, AXP813_ADC_RATE,
				 AXP813_ADC_RATE_MASK,
				 AXP813_ADC_RATE_HZ(rate));
}

struct axp_data {
	const struct iio_info		*iio_info;
	int				num_channels;
	struct iio_chan_spec const	*channels;
	unsigned long			adc_en1;
	unsigned long			adc_en1_mask;
	unsigned long			adc_en2;
	unsigned long			adc_en2_mask;
	int				(*adc_rate)(struct axp20x_adc_iio *info,
						    int rate);
	struct iio_map			*maps;
};

static const struct axp_data axp192_data = {
	.iio_info = &axp192_adc_iio_info,
	.num_channels = ARRAY_SIZE(axp192_adc_channels),
	.channels = axp192_adc_channels,
	.adc_en1_mask = AXP192_ADC_EN1_MASK,
	.adc_en2_mask = AXP192_ADC_EN2_MASK,
	.adc_rate = axp20x_adc_rate,
	.maps = axp20x_maps,
};

static const struct axp_data axp20x_data = {
	.iio_info = &axp20x_adc_iio_info,
	.num_channels = ARRAY_SIZE(axp20x_adc_channels),
	.channels = axp20x_adc_channels,
	.adc_en1 = AXP20X_ADC_EN1,
	.adc_en1_mask = AXP20X_ADC_EN1_MASK,
	.adc_en2 = AXP20X_ADC_EN2,
	.adc_en2_mask = AXP20X_ADC_EN2_MASK,
	.adc_rate = axp20x_adc_rate,
	.maps = axp20x_maps,
};

static const struct axp_data axp22x_data = {
	.iio_info = &axp22x_adc_iio_info,
	.num_channels = ARRAY_SIZE(axp22x_adc_channels),
	.channels = axp22x_adc_channels,
	.adc_en1 = AXP20X_ADC_EN1,
	.adc_en1_mask = AXP22X_ADC_EN1_MASK,
	.adc_rate = axp22x_adc_rate,
	.maps = axp22x_maps,
};

static const struct axp_data axp717_data = {
	.iio_info = &axp717_adc_iio_info,
	.num_channels = ARRAY_SIZE(axp717_adc_channels),
	.channels = axp717_adc_channels,
	.adc_en1 = AXP717_ADC_CH_EN_CONTROL,
	.adc_en1_mask = AXP717_ADC_EN1_MASK,
	.maps = axp717_maps,
};

static const struct axp_data axp813_data = {
	.iio_info = &axp813_adc_iio_info,
	.num_channels = ARRAY_SIZE(axp813_adc_channels),
	.channels = axp813_adc_channels,
	.adc_en1 = AXP20X_ADC_EN1,
	.adc_en1_mask = AXP22X_ADC_EN1_MASK,
	.adc_rate = axp813_adc_rate,
	.maps = axp22x_maps,
};

static const struct of_device_id axp20x_adc_of_match[] = {
	{ .compatible = "x-powers,axp192-adc", .data = (void *)&axp192_data, },
	{ .compatible = "x-powers,axp209-adc", .data = (void *)&axp20x_data, },
	{ .compatible = "x-powers,axp221-adc", .data = (void *)&axp22x_data, },
	{ .compatible = "x-powers,axp717-adc", .data = (void *)&axp717_data, },
	{ .compatible = "x-powers,axp813-adc", .data = (void *)&axp813_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, axp20x_adc_of_match);

static const struct platform_device_id axp20x_adc_id_match[] = {
	{ .name = "axp192-adc", .driver_data = (kernel_ulong_t)&axp192_data, },
	{ .name = "axp20x-adc", .driver_data = (kernel_ulong_t)&axp20x_data, },
	{ .name = "axp22x-adc", .driver_data = (kernel_ulong_t)&axp22x_data, },
	{ .name = "axp717-adc", .driver_data = (kernel_ulong_t)&axp717_data, },
	{ .name = "axp813-adc", .driver_data = (kernel_ulong_t)&axp813_data, },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, axp20x_adc_id_match);

static int axp20x_probe(struct platform_device *pdev)
{
	struct axp20x_adc_iio *info;
	struct iio_dev *indio_dev;
	struct axp20x_dev *axp20x_dev;
	int ret;

	axp20x_dev = dev_get_drvdata(pdev->dev.parent);

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*info));
	if (!indio_dev)
		return -ENOMEM;

	info = iio_priv(indio_dev);
	platform_set_drvdata(pdev, indio_dev);

	info->regmap = axp20x_dev->regmap;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (!dev_fwnode(&pdev->dev)) {
		const struct platform_device_id *id;

		id = platform_get_device_id(pdev);
		info->data = (const struct axp_data *)id->driver_data;
	} else {
		struct device *dev = &pdev->dev;

		info->data = device_get_match_data(dev);
	}

	indio_dev->name = platform_get_device_id(pdev)->name;
	indio_dev->info = info->data->iio_info;
	indio_dev->num_channels = info->data->num_channels;
	indio_dev->channels = info->data->channels;

	/* Enable the ADCs on IP */
	regmap_write(info->regmap, info->data->adc_en1,
		     info->data->adc_en1_mask);

	if (info->data->adc_en2_mask)
		regmap_set_bits(info->regmap, info->data->adc_en2,
				info->data->adc_en2_mask);

	/* Configure ADCs rate */
	if (info->data->adc_rate)
		info->data->adc_rate(info, 100);

	ret = iio_map_array_register(indio_dev, info->data->maps);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register IIO maps: %d\n", ret);
		goto fail_map;
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not register the device\n");
		goto fail_register;
	}

	return 0;

fail_register:
	iio_map_array_unregister(indio_dev);

fail_map:
	regmap_write(info->regmap, info->data->adc_en1, 0);

	if (info->data->adc_en2_mask)
		regmap_write(info->regmap, info->data->adc_en2, 0);

	return ret;
}

static void axp20x_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct axp20x_adc_iio *info = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_map_array_unregister(indio_dev);

	regmap_write(info->regmap, info->data->adc_en1, 0);

	if (info->data->adc_en2_mask)
		regmap_write(info->regmap, info->data->adc_en2, 0);
}

static struct platform_driver axp20x_adc_driver = {
	.driver = {
		.name = "axp20x-adc",
		.of_match_table = axp20x_adc_of_match,
	},
	.id_table = axp20x_adc_id_match,
	.probe = axp20x_probe,
	.remove_new = axp20x_remove,
};

module_platform_driver(axp20x_adc_driver);

MODULE_DESCRIPTION("ADC driver for AXP20X and AXP22X PMICs");
MODULE_AUTHOR("Quentin Schulz <quentin.schulz@free-electrons.com>");
MODULE_LICENSE("GPL");
