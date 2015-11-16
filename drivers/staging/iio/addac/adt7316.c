/*
 * ADT7316 digital temperature sensor driver supporting ADT7316/7/8 ADT7516/7/9
 *
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/events.h>
#include <linux/iio/sysfs.h>
#include "adt7316.h"

/*
 * ADT7316 registers definition
 */
#define ADT7316_INT_STAT1		0x0
#define ADT7316_INT_STAT2		0x1
#define ADT7316_LSB_IN_TEMP_VDD		0x3
#define ADT7316_LSB_IN_TEMP_MASK	0x3
#define ADT7316_LSB_VDD_MASK		0xC
#define ADT7316_LSB_VDD_OFFSET		2
#define ADT7316_LSB_EX_TEMP_AIN		0x4
#define ADT7316_LSB_EX_TEMP_MASK	0x3
#define ADT7516_LSB_AIN_SHIFT		2
#define ADT7316_AD_MSB_DATA_BASE        0x6
#define ADT7316_AD_MSB_DATA_REGS        3
#define ADT7516_AD_MSB_DATA_REGS        6
#define ADT7316_MSB_VDD			0x6
#define ADT7316_MSB_IN_TEMP		0x7
#define ADT7316_MSB_EX_TEMP		0x8
#define ADT7516_MSB_AIN1		0x8
#define ADT7516_MSB_AIN2		0x9
#define ADT7516_MSB_AIN3		0xA
#define ADT7516_MSB_AIN4		0xB
#define ADT7316_DA_DATA_BASE		0x10
#define ADT7316_DA_MSB_DATA_REGS	4
#define ADT7316_LSB_DAC_A		0x10
#define ADT7316_MSB_DAC_A		0x11
#define ADT7316_LSB_DAC_B		0x12
#define ADT7316_MSB_DAC_B		0x13
#define ADT7316_LSB_DAC_C		0x14
#define ADT7316_MSB_DAC_C		0x15
#define ADT7316_LSB_DAC_D		0x16
#define ADT7316_MSB_DAC_D		0x17
#define ADT7316_CONFIG1			0x18
#define ADT7316_CONFIG2			0x19
#define ADT7316_CONFIG3			0x1A
#define ADT7316_LDAC_CONFIG		0x1B
#define ADT7316_DAC_CONFIG		0x1C
#define ADT7316_INT_MASK1		0x1D
#define ADT7316_INT_MASK2		0x1E
#define ADT7316_IN_TEMP_OFFSET		0x1F
#define ADT7316_EX_TEMP_OFFSET		0x20
#define ADT7316_IN_ANALOG_TEMP_OFFSET	0x21
#define ADT7316_EX_ANALOG_TEMP_OFFSET	0x22
#define ADT7316_VDD_HIGH		0x23
#define ADT7316_VDD_LOW			0x24
#define ADT7316_IN_TEMP_HIGH		0x25
#define ADT7316_IN_TEMP_LOW		0x26
#define ADT7316_EX_TEMP_HIGH		0x27
#define ADT7316_EX_TEMP_LOW		0x28
#define ADT7516_AIN2_HIGH		0x2B
#define ADT7516_AIN2_LOW		0x2C
#define ADT7516_AIN3_HIGH		0x2D
#define ADT7516_AIN3_LOW		0x2E
#define ADT7516_AIN4_HIGH		0x2F
#define ADT7516_AIN4_LOW		0x30
#define ADT7316_DEVICE_ID		0x4D
#define ADT7316_MANUFACTURE_ID		0x4E
#define ADT7316_DEVICE_REV		0x4F
#define ADT7316_SPI_LOCK_STAT		0x7F

/*
 * ADT7316 config1
 */
#define ADT7316_EN			0x1
#define ADT7516_SEL_EX_TEMP		0x4
#define ADT7516_SEL_AIN1_2_EX_TEMP_MASK	0x6
#define ADT7516_SEL_AIN3		0x8
#define ADT7316_INT_EN			0x20
#define ADT7316_INT_POLARITY		0x40
#define ADT7316_PD			0x80

/*
 * ADT7316 config2
 */
#define ADT7316_AD_SINGLE_CH_MASK	0x3
#define ADT7516_AD_SINGLE_CH_MASK	0x7
#define ADT7316_AD_SINGLE_CH_VDD	0
#define ADT7316_AD_SINGLE_CH_IN		1
#define ADT7316_AD_SINGLE_CH_EX		2
#define ADT7516_AD_SINGLE_CH_AIN1	2
#define ADT7516_AD_SINGLE_CH_AIN2	3
#define ADT7516_AD_SINGLE_CH_AIN3	4
#define ADT7516_AD_SINGLE_CH_AIN4	5
#define ADT7316_AD_SINGLE_CH_MODE	0x10
#define ADT7316_DISABLE_AVERAGING	0x20
#define ADT7316_EN_SMBUS_TIMEOUT	0x40
#define ADT7316_RESET			0x80

/*
 * ADT7316 config3
 */
#define ADT7316_ADCLK_22_5		0x1
#define ADT7316_DA_HIGH_RESOLUTION	0x2
#define ADT7316_DA_EN_VIA_DAC_LDCA	0x4
#define ADT7516_AIN_IN_VREF		0x10
#define ADT7316_EN_IN_TEMP_PROP_DACA	0x20
#define ADT7316_EN_EX_TEMP_PROP_DACB	0x40

/*
 * ADT7316 DAC config
 */
#define ADT7316_DA_2VREF_CH_MASK	0xF
#define ADT7316_DA_EN_MODE_MASK		0x30
#define ADT7316_DA_EN_MODE_SINGLE	0x00
#define ADT7316_DA_EN_MODE_AB_CD	0x10
#define ADT7316_DA_EN_MODE_ABCD		0x20
#define ADT7316_DA_EN_MODE_LDAC		0x30
#define ADT7316_VREF_BYPASS_DAC_AB	0x40
#define ADT7316_VREF_BYPASS_DAC_CD	0x80

/*
 * ADT7316 LDAC config
 */
#define ADT7316_LDAC_EN_DA_MASK		0xF
#define ADT7316_DAC_IN_VREF		0x10
#define ADT7516_DAC_AB_IN_VREF		0x10
#define ADT7516_DAC_CD_IN_VREF		0x20
#define ADT7516_DAC_IN_VREF_OFFSET	4
#define ADT7516_DAC_IN_VREF_MASK	0x30

/*
 * ADT7316 INT_MASK2
 */
#define ADT7316_INT_MASK2_VDD		0x10

/*
 * ADT7316 value masks
 */
#define ADT7316_VALUE_MASK		0xfff
#define ADT7316_T_VALUE_SIGN		0x400
#define ADT7316_T_VALUE_FLOAT_OFFSET	2
#define ADT7316_T_VALUE_FLOAT_MASK	0x2

/*
 * Chip ID
 */
#define ID_ADT7316		0x1
#define ID_ADT7317		0x2
#define ID_ADT7318		0x3
#define ID_ADT7516		0x11
#define ID_ADT7517		0x12
#define ID_ADT7519		0x14

#define ID_FAMILY_MASK		0xF0
#define ID_ADT73XX		0x0
#define ID_ADT75XX		0x10

/*
 * struct adt7316_chip_info - chip specific information
 */

struct adt7316_chip_info {
	struct adt7316_bus	bus;
	u16			ldac_pin;
	u16			int_mask;	/* 0x2f */
	u8			config1;
	u8			config2;
	u8			config3;
	u8			dac_config;	/* DAC config */
	u8			ldac_config;	/* LDAC config */
	u8			dac_bits;	/* 8, 10, 12 */
	u8			id;		/* chip id */
};

/*
 * Logic interrupt mask for user application to enable
 * interrupts.
 */
#define ADT7316_IN_TEMP_HIGH_INT_MASK	0x1
#define ADT7316_IN_TEMP_LOW_INT_MASK	0x2
#define ADT7316_EX_TEMP_HIGH_INT_MASK	0x4
#define ADT7316_EX_TEMP_LOW_INT_MASK	0x8
#define ADT7316_EX_TEMP_FAULT_INT_MASK	0x10
#define ADT7516_AIN1_INT_MASK		0x4
#define ADT7516_AIN2_INT_MASK		0x20
#define ADT7516_AIN3_INT_MASK		0x40
#define ADT7516_AIN4_INT_MASK		0x80
#define ADT7316_VDD_INT_MASK		0x100
#define ADT7316_TEMP_INT_MASK		0x1F
#define ADT7516_AIN_INT_MASK		0xE0
#define ADT7316_TEMP_AIN_INT_MASK	\
	(ADT7316_TEMP_INT_MASK)

/*
 * struct adt7316_chip_info - chip specific information
 */

struct adt7316_limit_regs {
	u16	data_high;
	u16	data_low;
};

static ssize_t adt7316_show_enabled(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "%d\n", !!(chip->config1 & ADT7316_EN));
}

static ssize_t _adt7316_store_enabled(struct adt7316_chip_info *chip,
		int enable)
{
	u8 config1;
	int ret;

	if (enable)
		config1 = chip->config1 | ADT7316_EN;
	else
		config1 = chip->config1 & ~ADT7316_EN;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG1, config1);
	if (ret)
		return -EIO;

	chip->config1 = config1;

	return ret;

}

static ssize_t adt7316_store_enabled(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	int enable;

	if (buf[0] == '1')
		enable = 1;
	else
		enable = 0;

	if (_adt7316_store_enabled(chip, enable) < 0)
		return -EIO;

	return len;
}

static IIO_DEVICE_ATTR(enabled, S_IRUGO | S_IWUSR,
		adt7316_show_enabled,
		adt7316_store_enabled,
		0);

static ssize_t adt7316_show_select_ex_temp(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	if ((chip->id & ID_FAMILY_MASK) != ID_ADT75XX)
		return -EPERM;

	return sprintf(buf, "%d\n", !!(chip->config1 & ADT7516_SEL_EX_TEMP));
}

static ssize_t adt7316_store_select_ex_temp(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 config1;
	int ret;

	if ((chip->id & ID_FAMILY_MASK) != ID_ADT75XX)
		return -EPERM;

	config1 = chip->config1 & (~ADT7516_SEL_EX_TEMP);
	if (buf[0] == '1')
		config1 |= ADT7516_SEL_EX_TEMP;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG1, config1);
	if (ret)
		return -EIO;

	chip->config1 = config1;

	return len;
}

static IIO_DEVICE_ATTR(select_ex_temp, S_IRUGO | S_IWUSR,
		adt7316_show_select_ex_temp,
		adt7316_store_select_ex_temp,
		0);

static ssize_t adt7316_show_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	if (chip->config2 & ADT7316_AD_SINGLE_CH_MODE)
		return sprintf(buf, "single_channel\n");

	return sprintf(buf, "round_robin\n");
}

static ssize_t adt7316_store_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 config2;
	int ret;

	config2 = chip->config2 & (~ADT7316_AD_SINGLE_CH_MODE);
	if (!memcmp(buf, "single_channel", 14))
		config2 |= ADT7316_AD_SINGLE_CH_MODE;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG2, config2);
	if (ret)
		return -EIO;

	chip->config2 = config2;

	return len;
}

static IIO_DEVICE_ATTR(mode, S_IRUGO | S_IWUSR,
		adt7316_show_mode,
		adt7316_store_mode,
		0);

static ssize_t adt7316_show_all_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "single_channel\nround_robin\n");
}

static IIO_DEVICE_ATTR(all_modes, S_IRUGO, adt7316_show_all_modes, NULL, 0);

static ssize_t adt7316_show_ad_channel(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	if (!(chip->config2 & ADT7316_AD_SINGLE_CH_MODE))
		return -EPERM;

	switch (chip->config2 & ADT7516_AD_SINGLE_CH_MASK) {
	case ADT7316_AD_SINGLE_CH_VDD:
		return sprintf(buf, "0 - VDD\n");
	case ADT7316_AD_SINGLE_CH_IN:
		return sprintf(buf, "1 - Internal Temperature\n");
	case ADT7316_AD_SINGLE_CH_EX:
		if (((chip->id & ID_FAMILY_MASK) == ID_ADT75XX) &&
			(chip->config1 & ADT7516_SEL_AIN1_2_EX_TEMP_MASK) == 0)
			return sprintf(buf, "2 - AIN1\n");

		return sprintf(buf, "2 - External Temperature\n");
	case ADT7516_AD_SINGLE_CH_AIN2:
		if ((chip->config1 & ADT7516_SEL_AIN1_2_EX_TEMP_MASK) == 0)
			return sprintf(buf, "3 - AIN2\n");

		return sprintf(buf, "N/A\n");
	case ADT7516_AD_SINGLE_CH_AIN3:
		if (chip->config1 & ADT7516_SEL_AIN3)
			return sprintf(buf, "4 - AIN3\n");

		return sprintf(buf, "N/A\n");
	case ADT7516_AD_SINGLE_CH_AIN4:
		return sprintf(buf, "5 - AIN4\n");
	default:
		return sprintf(buf, "N/A\n");
	}
}

static ssize_t adt7316_store_ad_channel(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 config2;
	u8 data;
	int ret;

	if (!(chip->config2 & ADT7316_AD_SINGLE_CH_MODE))
		return -EPERM;

	ret = kstrtou8(buf, 10, &data);
	if (ret)
		return -EINVAL;

	if ((chip->id & ID_FAMILY_MASK) == ID_ADT75XX) {
		if (data > 5)
			return -EINVAL;

		config2 = chip->config2 & (~ADT7516_AD_SINGLE_CH_MASK);
	} else {
		if (data > 2)
			return -EINVAL;

		config2 = chip->config2 & (~ADT7316_AD_SINGLE_CH_MASK);
	}


	config2 |= data;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG2, config2);
	if (ret)
		return -EIO;

	chip->config2 = config2;

	return len;
}

static IIO_DEVICE_ATTR(ad_channel, S_IRUGO | S_IWUSR,
		adt7316_show_ad_channel,
		adt7316_store_ad_channel,
		0);

static ssize_t adt7316_show_all_ad_channels(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	if (!(chip->config2 & ADT7316_AD_SINGLE_CH_MODE))
		return -EPERM;

	if ((chip->id & ID_FAMILY_MASK) == ID_ADT75XX)
		return sprintf(buf, "0 - VDD\n1 - Internal Temperature\n"
				"2 - External Temperature or AIN1\n"
				"3 - AIN2\n4 - AIN3\n5 - AIN4\n");
	else
		return sprintf(buf, "0 - VDD\n1 - Internal Temperature\n"
				"2 - External Temperature\n");
}

static IIO_DEVICE_ATTR(all_ad_channels, S_IRUGO,
		adt7316_show_all_ad_channels, NULL, 0);

static ssize_t adt7316_show_disable_averaging(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "%d\n",
		!!(chip->config2 & ADT7316_DISABLE_AVERAGING));
}

static ssize_t adt7316_store_disable_averaging(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 config2;
	int ret;

	config2 = chip->config2 & (~ADT7316_DISABLE_AVERAGING);
	if (buf[0] == '1')
		config2 |= ADT7316_DISABLE_AVERAGING;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG2, config2);
	if (ret)
		return -EIO;

	chip->config2 = config2;

	return len;
}

static IIO_DEVICE_ATTR(disable_averaging, S_IRUGO | S_IWUSR,
		adt7316_show_disable_averaging,
		adt7316_store_disable_averaging,
		0);

static ssize_t adt7316_show_enable_smbus_timeout(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "%d\n",
		!!(chip->config2 & ADT7316_EN_SMBUS_TIMEOUT));
}

static ssize_t adt7316_store_enable_smbus_timeout(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 config2;
	int ret;

	config2 = chip->config2 & (~ADT7316_EN_SMBUS_TIMEOUT);
	if (buf[0] == '1')
		config2 |= ADT7316_EN_SMBUS_TIMEOUT;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG2, config2);
	if (ret)
		return -EIO;

	chip->config2 = config2;

	return len;
}

static IIO_DEVICE_ATTR(enable_smbus_timeout, S_IRUGO | S_IWUSR,
		adt7316_show_enable_smbus_timeout,
		adt7316_store_enable_smbus_timeout,
		0);

static ssize_t adt7316_show_powerdown(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "%d\n", !!(chip->config1 & ADT7316_PD));
}

static ssize_t adt7316_store_powerdown(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 config1;
	int ret;

	config1 = chip->config1 & (~ADT7316_PD);
	if (buf[0] == '1')
		config1 |= ADT7316_PD;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG1, config1);
	if (ret)
		return -EIO;

	chip->config1 = config1;

	return len;
}

static IIO_DEVICE_ATTR(powerdown, S_IRUGO | S_IWUSR,
		adt7316_show_powerdown,
		adt7316_store_powerdown,
		0);

static ssize_t adt7316_show_fast_ad_clock(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "%d\n", !!(chip->config3 & ADT7316_ADCLK_22_5));
}

static ssize_t adt7316_store_fast_ad_clock(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 config3;
	int ret;

	config3 = chip->config3 & (~ADT7316_ADCLK_22_5);
	if (buf[0] == '1')
		config3 |= ADT7316_ADCLK_22_5;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG3, config3);
	if (ret)
		return -EIO;

	chip->config3 = config3;

	return len;
}

static IIO_DEVICE_ATTR(fast_ad_clock, S_IRUGO | S_IWUSR,
		adt7316_show_fast_ad_clock,
		adt7316_store_fast_ad_clock,
		0);

static ssize_t adt7316_show_da_high_resolution(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	if (chip->config3 & ADT7316_DA_HIGH_RESOLUTION) {
		if (chip->id == ID_ADT7316 || chip->id == ID_ADT7516)
			return sprintf(buf, "1 (12 bits)\n");
		else if (chip->id == ID_ADT7317 || chip->id == ID_ADT7517)
			return sprintf(buf, "1 (10 bits)\n");
	}

	return sprintf(buf, "0 (8 bits)\n");
}

static ssize_t adt7316_store_da_high_resolution(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 config3;
	int ret;

	chip->dac_bits = 8;

	if (buf[0] == '1') {
		config3 = chip->config3 | ADT7316_DA_HIGH_RESOLUTION;
		if (chip->id == ID_ADT7316 || chip->id == ID_ADT7516)
			chip->dac_bits = 12;
		else if (chip->id == ID_ADT7317 || chip->id == ID_ADT7517)
			chip->dac_bits = 10;
	} else
		config3 = chip->config3 & (~ADT7316_DA_HIGH_RESOLUTION);

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG3, config3);
	if (ret)
		return -EIO;

	chip->config3 = config3;

	return len;
}

static IIO_DEVICE_ATTR(da_high_resolution, S_IRUGO | S_IWUSR,
		adt7316_show_da_high_resolution,
		adt7316_store_da_high_resolution,
		0);

static ssize_t adt7316_show_AIN_internal_Vref(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	if ((chip->id & ID_FAMILY_MASK) != ID_ADT75XX)
		return -EPERM;

	return sprintf(buf, "%d\n",
		!!(chip->config3 & ADT7516_AIN_IN_VREF));
}

static ssize_t adt7316_store_AIN_internal_Vref(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 config3;
	int ret;

	if ((chip->id & ID_FAMILY_MASK) != ID_ADT75XX)
		return -EPERM;

	if (buf[0] != '1')
		config3 = chip->config3 & (~ADT7516_AIN_IN_VREF);
	else
		config3 = chip->config3 | ADT7516_AIN_IN_VREF;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG3, config3);
	if (ret)
		return -EIO;

	chip->config3 = config3;

	return len;
}

static IIO_DEVICE_ATTR(AIN_internal_Vref, S_IRUGO | S_IWUSR,
		adt7316_show_AIN_internal_Vref,
		adt7316_store_AIN_internal_Vref,
		0);


static ssize_t adt7316_show_enable_prop_DACA(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "%d\n",
		!!(chip->config3 & ADT7316_EN_IN_TEMP_PROP_DACA));
}

static ssize_t adt7316_store_enable_prop_DACA(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 config3;
	int ret;

	config3 = chip->config3 & (~ADT7316_EN_IN_TEMP_PROP_DACA);
	if (buf[0] == '1')
		config3 |= ADT7316_EN_IN_TEMP_PROP_DACA;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG3, config3);
	if (ret)
		return -EIO;

	chip->config3 = config3;

	return len;
}

static IIO_DEVICE_ATTR(enable_proportion_DACA, S_IRUGO | S_IWUSR,
		adt7316_show_enable_prop_DACA,
		adt7316_store_enable_prop_DACA,
		0);

static ssize_t adt7316_show_enable_prop_DACB(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "%d\n",
		!!(chip->config3 & ADT7316_EN_EX_TEMP_PROP_DACB));
}

static ssize_t adt7316_store_enable_prop_DACB(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 config3;
	int ret;

	config3 = chip->config3 & (~ADT7316_EN_EX_TEMP_PROP_DACB);
	if (buf[0] == '1')
		config3 |= ADT7316_EN_EX_TEMP_PROP_DACB;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG3, config3);
	if (ret)
		return -EIO;

	chip->config3 = config3;

	return len;
}

static IIO_DEVICE_ATTR(enable_proportion_DACB, S_IRUGO | S_IWUSR,
		adt7316_show_enable_prop_DACB,
		adt7316_store_enable_prop_DACB,
		0);

static ssize_t adt7316_show_DAC_2Vref_ch_mask(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "0x%x\n",
		chip->dac_config & ADT7316_DA_2VREF_CH_MASK);
}

static ssize_t adt7316_store_DAC_2Vref_ch_mask(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 dac_config;
	u8 data;
	int ret;

	ret = kstrtou8(buf, 16, &data);
	if (ret || data > ADT7316_DA_2VREF_CH_MASK)
		return -EINVAL;

	dac_config = chip->dac_config & (~ADT7316_DA_2VREF_CH_MASK);
	dac_config |= data;

	ret = chip->bus.write(chip->bus.client, ADT7316_DAC_CONFIG, dac_config);
	if (ret)
		return -EIO;

	chip->dac_config = dac_config;

	return len;
}

static IIO_DEVICE_ATTR(DAC_2Vref_channels_mask, S_IRUGO | S_IWUSR,
		adt7316_show_DAC_2Vref_ch_mask,
		adt7316_store_DAC_2Vref_ch_mask,
		0);

static ssize_t adt7316_show_DAC_update_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	if (!(chip->config3 & ADT7316_DA_EN_VIA_DAC_LDCA))
		return sprintf(buf, "manual\n");

	switch (chip->dac_config & ADT7316_DA_EN_MODE_MASK) {
	case ADT7316_DA_EN_MODE_SINGLE:
		return sprintf(buf,
			"0 - auto at any MSB DAC writing\n");
	case ADT7316_DA_EN_MODE_AB_CD:
		return sprintf(buf,
			"1 - auto at MSB DAC AB and CD writing\n");
	case ADT7316_DA_EN_MODE_ABCD:
		return sprintf(buf,
			"2 - auto at MSB DAC ABCD writing\n");
	default: /* ADT7316_DA_EN_MODE_LDAC */
		return sprintf(buf, "3 - manual\n");
	}
}

static ssize_t adt7316_store_DAC_update_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 dac_config;
	u8 data;
	int ret;

	if (!(chip->config3 & ADT7316_DA_EN_VIA_DAC_LDCA))
		return -EPERM;

	ret = kstrtou8(buf, 10, &data);
	if (ret || data > ADT7316_DA_EN_MODE_MASK)
		return -EINVAL;

	dac_config = chip->dac_config & (~ADT7316_DA_EN_MODE_MASK);
	dac_config |= data;

	ret = chip->bus.write(chip->bus.client, ADT7316_DAC_CONFIG, dac_config);
	if (ret)
		return -EIO;

	chip->dac_config = dac_config;

	return len;
}

static IIO_DEVICE_ATTR(DAC_update_mode, S_IRUGO | S_IWUSR,
		adt7316_show_DAC_update_mode,
		adt7316_store_DAC_update_mode,
		0);

static ssize_t adt7316_show_all_DAC_update_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	if (chip->config3 & ADT7316_DA_EN_VIA_DAC_LDCA)
		return sprintf(buf, "0 - auto at any MSB DAC writing\n"
				"1 - auto at MSB DAC AB and CD writing\n"
				"2 - auto at MSB DAC ABCD writing\n"
				"3 - manual\n");
	else
		return sprintf(buf, "manual\n");
}

static IIO_DEVICE_ATTR(all_DAC_update_modes, S_IRUGO,
		adt7316_show_all_DAC_update_modes, NULL, 0);


static ssize_t adt7316_store_update_DAC(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 ldac_config;
	u8 data;
	int ret;

	if (chip->config3 & ADT7316_DA_EN_VIA_DAC_LDCA) {
		if ((chip->dac_config & ADT7316_DA_EN_MODE_MASK) !=
			ADT7316_DA_EN_MODE_LDAC)
			return -EPERM;

		ret = kstrtou8(buf, 16, &data);
		if (ret || data > ADT7316_LDAC_EN_DA_MASK)
			return -EINVAL;

		ldac_config = chip->ldac_config & (~ADT7316_LDAC_EN_DA_MASK);
		ldac_config |= data;

		ret = chip->bus.write(chip->bus.client, ADT7316_LDAC_CONFIG,
			ldac_config);
		if (ret)
			return -EIO;
	} else {
		gpio_set_value(chip->ldac_pin, 0);
		gpio_set_value(chip->ldac_pin, 1);
	}

	return len;
}

static IIO_DEVICE_ATTR(update_DAC, S_IRUGO | S_IWUSR,
		NULL,
		adt7316_store_update_DAC,
		0);

static ssize_t adt7316_show_DA_AB_Vref_bypass(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	if ((chip->id & ID_FAMILY_MASK) == ID_ADT75XX)
		return -EPERM;

	return sprintf(buf, "%d\n",
		!!(chip->dac_config & ADT7316_VREF_BYPASS_DAC_AB));
}

static ssize_t adt7316_store_DA_AB_Vref_bypass(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 dac_config;
	int ret;

	if ((chip->id & ID_FAMILY_MASK) == ID_ADT75XX)
		return -EPERM;

	dac_config = chip->dac_config & (~ADT7316_VREF_BYPASS_DAC_AB);
	if (buf[0] == '1')
		dac_config |= ADT7316_VREF_BYPASS_DAC_AB;

	ret = chip->bus.write(chip->bus.client, ADT7316_DAC_CONFIG, dac_config);
	if (ret)
		return -EIO;

	chip->dac_config = dac_config;

	return len;
}

static IIO_DEVICE_ATTR(DA_AB_Vref_bypass, S_IRUGO | S_IWUSR,
		adt7316_show_DA_AB_Vref_bypass,
		adt7316_store_DA_AB_Vref_bypass,
		0);

static ssize_t adt7316_show_DA_CD_Vref_bypass(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	if ((chip->id & ID_FAMILY_MASK) == ID_ADT75XX)
		return -EPERM;

	return sprintf(buf, "%d\n",
		!!(chip->dac_config & ADT7316_VREF_BYPASS_DAC_CD));
}

static ssize_t adt7316_store_DA_CD_Vref_bypass(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 dac_config;
	int ret;

	if ((chip->id & ID_FAMILY_MASK) == ID_ADT75XX)
		return -EPERM;

	dac_config = chip->dac_config & (~ADT7316_VREF_BYPASS_DAC_CD);
	if (buf[0] == '1')
		dac_config |= ADT7316_VREF_BYPASS_DAC_CD;

	ret = chip->bus.write(chip->bus.client, ADT7316_DAC_CONFIG, dac_config);
	if (ret)
		return -EIO;

	chip->dac_config = dac_config;

	return len;
}

static IIO_DEVICE_ATTR(DA_CD_Vref_bypass, S_IRUGO | S_IWUSR,
		adt7316_show_DA_CD_Vref_bypass,
		adt7316_store_DA_CD_Vref_bypass,
		0);

static ssize_t adt7316_show_DAC_internal_Vref(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	if ((chip->id & ID_FAMILY_MASK) == ID_ADT75XX)
		return sprintf(buf, "0x%x\n",
			(chip->dac_config & ADT7516_DAC_IN_VREF_MASK) >>
			ADT7516_DAC_IN_VREF_OFFSET);
	else
		return sprintf(buf, "%d\n",
			!!(chip->dac_config & ADT7316_DAC_IN_VREF));
}

static ssize_t adt7316_store_DAC_internal_Vref(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 ldac_config;
	u8 data;
	int ret;

	if ((chip->id & ID_FAMILY_MASK) == ID_ADT75XX) {
		ret = kstrtou8(buf, 16, &data);
		if (ret || data > 3)
			return -EINVAL;

		ldac_config = chip->ldac_config & (~ADT7516_DAC_IN_VREF_MASK);
		if (data & 0x1)
			ldac_config |= ADT7516_DAC_AB_IN_VREF;
		else if (data & 0x2)
			ldac_config |= ADT7516_DAC_CD_IN_VREF;
	} else {
		ret = kstrtou8(buf, 16, &data);
		if (ret)
			return -EINVAL;

		ldac_config = chip->ldac_config & (~ADT7316_DAC_IN_VREF);
		if (data)
			ldac_config = chip->ldac_config | ADT7316_DAC_IN_VREF;
	}

	ret = chip->bus.write(chip->bus.client, ADT7316_LDAC_CONFIG,
			ldac_config);
	if (ret)
		return -EIO;

	chip->ldac_config = ldac_config;

	return len;
}

static IIO_DEVICE_ATTR(DAC_internal_Vref, S_IRUGO | S_IWUSR,
		adt7316_show_DAC_internal_Vref,
		adt7316_store_DAC_internal_Vref,
		0);

static ssize_t adt7316_show_ad(struct adt7316_chip_info *chip,
		int channel, char *buf)
{
	u16 data;
	u8 msb, lsb;
	char sign = ' ';
	int ret;

	if ((chip->config2 & ADT7316_AD_SINGLE_CH_MODE) &&
		channel != (chip->config2 & ADT7516_AD_SINGLE_CH_MASK))
		return -EPERM;

	switch (channel) {
	case ADT7316_AD_SINGLE_CH_IN:
		ret = chip->bus.read(chip->bus.client,
			ADT7316_LSB_IN_TEMP_VDD, &lsb);
		if (ret)
			return -EIO;

		ret = chip->bus.read(chip->bus.client,
			ADT7316_AD_MSB_DATA_BASE + channel, &msb);
		if (ret)
			return -EIO;

		data = msb << ADT7316_T_VALUE_FLOAT_OFFSET;
		data |= lsb & ADT7316_LSB_IN_TEMP_MASK;
		break;
	case ADT7316_AD_SINGLE_CH_VDD:
		ret = chip->bus.read(chip->bus.client,
			ADT7316_LSB_IN_TEMP_VDD, &lsb);
		if (ret)
			return -EIO;

		ret = chip->bus.read(chip->bus.client,

			ADT7316_AD_MSB_DATA_BASE + channel, &msb);
		if (ret)
			return -EIO;

		data = msb << ADT7316_T_VALUE_FLOAT_OFFSET;
		data |= (lsb & ADT7316_LSB_VDD_MASK) >> ADT7316_LSB_VDD_OFFSET;
		return sprintf(buf, "%d\n", data);
	default: /* ex_temp and ain */
		ret = chip->bus.read(chip->bus.client,
			ADT7316_LSB_EX_TEMP_AIN, &lsb);
		if (ret)
			return -EIO;

		ret = chip->bus.read(chip->bus.client,
			ADT7316_AD_MSB_DATA_BASE + channel, &msb);
		if (ret)
			return -EIO;

		data = msb << ADT7316_T_VALUE_FLOAT_OFFSET;
		data |= lsb & (ADT7316_LSB_EX_TEMP_MASK <<
			(ADT7516_LSB_AIN_SHIFT * (channel -
			(ADT7316_MSB_EX_TEMP - ADT7316_AD_MSB_DATA_BASE))));

		if ((chip->id & ID_FAMILY_MASK) == ID_ADT75XX)
			return sprintf(buf, "%d\n", data);

		break;
	}

	if (data & ADT7316_T_VALUE_SIGN) {
		/* convert supplement to positive value */
		data = (ADT7316_T_VALUE_SIGN << 1) - data;
		sign = '-';
	}

	return sprintf(buf, "%c%d.%.2d\n", sign,
		(data >> ADT7316_T_VALUE_FLOAT_OFFSET),
		(data & ADT7316_T_VALUE_FLOAT_MASK) * 25);
}

static ssize_t adt7316_show_VDD(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_ad(chip, ADT7316_AD_SINGLE_CH_VDD, buf);
}
static IIO_DEVICE_ATTR(VDD, S_IRUGO, adt7316_show_VDD, NULL, 0);

static ssize_t adt7316_show_in_temp(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_ad(chip, ADT7316_AD_SINGLE_CH_IN, buf);
}

static IIO_DEVICE_ATTR(in_temp, S_IRUGO, adt7316_show_in_temp, NULL, 0);

static ssize_t adt7316_show_ex_temp_AIN1(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_ad(chip, ADT7316_AD_SINGLE_CH_EX, buf);
}

static IIO_DEVICE_ATTR(ex_temp_AIN1, S_IRUGO, adt7316_show_ex_temp_AIN1,
		NULL, 0);
static IIO_DEVICE_ATTR(ex_temp, S_IRUGO, adt7316_show_ex_temp_AIN1, NULL, 0);

static ssize_t adt7316_show_AIN2(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_ad(chip, ADT7516_AD_SINGLE_CH_AIN2, buf);
}
static IIO_DEVICE_ATTR(AIN2, S_IRUGO, adt7316_show_AIN2, NULL, 0);

static ssize_t adt7316_show_AIN3(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_ad(chip, ADT7516_AD_SINGLE_CH_AIN3, buf);
}
static IIO_DEVICE_ATTR(AIN3, S_IRUGO, adt7316_show_AIN3, NULL, 0);

static ssize_t adt7316_show_AIN4(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_ad(chip, ADT7516_AD_SINGLE_CH_AIN4, buf);
}
static IIO_DEVICE_ATTR(AIN4, S_IRUGO, adt7316_show_AIN4, NULL, 0);

static ssize_t adt7316_show_temp_offset(struct adt7316_chip_info *chip,
		int offset_addr, char *buf)
{
	int data;
	u8 val;
	int ret;

	ret = chip->bus.read(chip->bus.client, offset_addr, &val);
	if (ret)
		return -EIO;

	data = (int)val;
	if (val & 0x80)
		data -= 256;

	return sprintf(buf, "%d\n", data);
}

static ssize_t adt7316_store_temp_offset(struct adt7316_chip_info *chip,
		int offset_addr, const char *buf, size_t len)
{
	int data;
	u8 val;
	int ret;

	ret = kstrtoint(buf, 10, &data);
	if (ret || data > 127 || data < -128)
		return -EINVAL;

	if (data < 0)
		data += 256;

	val = (u8)data;

	ret = chip->bus.write(chip->bus.client, offset_addr, val);
	if (ret)
		return -EIO;

	return len;
}

static ssize_t adt7316_show_in_temp_offset(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_temp_offset(chip, ADT7316_IN_TEMP_OFFSET, buf);
}

static ssize_t adt7316_store_in_temp_offset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_store_temp_offset(chip, ADT7316_IN_TEMP_OFFSET, buf,
			len);
}

static IIO_DEVICE_ATTR(in_temp_offset, S_IRUGO | S_IWUSR,
		adt7316_show_in_temp_offset,
		adt7316_store_in_temp_offset, 0);

static ssize_t adt7316_show_ex_temp_offset(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_temp_offset(chip, ADT7316_EX_TEMP_OFFSET, buf);
}

static ssize_t adt7316_store_ex_temp_offset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_store_temp_offset(chip, ADT7316_EX_TEMP_OFFSET, buf,
			len);
}

static IIO_DEVICE_ATTR(ex_temp_offset, S_IRUGO | S_IWUSR,
		adt7316_show_ex_temp_offset,
		adt7316_store_ex_temp_offset, 0);

static ssize_t adt7316_show_in_analog_temp_offset(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_temp_offset(chip,
			ADT7316_IN_ANALOG_TEMP_OFFSET, buf);
}

static ssize_t adt7316_store_in_analog_temp_offset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_store_temp_offset(chip,
			ADT7316_IN_ANALOG_TEMP_OFFSET, buf, len);
}

static IIO_DEVICE_ATTR(in_analog_temp_offset, S_IRUGO | S_IWUSR,
		adt7316_show_in_analog_temp_offset,
		adt7316_store_in_analog_temp_offset, 0);

static ssize_t adt7316_show_ex_analog_temp_offset(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_temp_offset(chip,
			ADT7316_EX_ANALOG_TEMP_OFFSET, buf);
}

static ssize_t adt7316_store_ex_analog_temp_offset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_store_temp_offset(chip,
			ADT7316_EX_ANALOG_TEMP_OFFSET, buf, len);
}

static IIO_DEVICE_ATTR(ex_analog_temp_offset, S_IRUGO | S_IWUSR,
		adt7316_show_ex_analog_temp_offset,
		adt7316_store_ex_analog_temp_offset, 0);

static ssize_t adt7316_show_DAC(struct adt7316_chip_info *chip,
		int channel, char *buf)
{
	u16 data;
	u8 msb, lsb, offset;
	int ret;

	if (channel >= ADT7316_DA_MSB_DATA_REGS ||
		(channel == 0 &&
		(chip->config3 & ADT7316_EN_IN_TEMP_PROP_DACA)) ||
		(channel == 1 &&
		(chip->config3 & ADT7316_EN_EX_TEMP_PROP_DACB)))
		return -EPERM;

	offset = chip->dac_bits - 8;

	if (chip->dac_bits > 8) {
		ret = chip->bus.read(chip->bus.client,
			ADT7316_DA_DATA_BASE + channel * 2, &lsb);
		if (ret)
			return -EIO;
	}

	ret = chip->bus.read(chip->bus.client,
		ADT7316_DA_DATA_BASE + 1 + channel * 2, &msb);
	if (ret)
		return -EIO;

	data = (msb << offset) + (lsb & ((1 << offset) - 1));

	return sprintf(buf, "%d\n", data);
}

static ssize_t adt7316_store_DAC(struct adt7316_chip_info *chip,
		int channel, const char *buf, size_t len)
{
	u8 msb, lsb, offset;
	u16 data;
	int ret;

	if (channel >= ADT7316_DA_MSB_DATA_REGS ||
		(channel == 0 &&
		(chip->config3 & ADT7316_EN_IN_TEMP_PROP_DACA)) ||
		(channel == 1 &&
		(chip->config3 & ADT7316_EN_EX_TEMP_PROP_DACB)))
		return -EPERM;

	offset = chip->dac_bits - 8;

	ret = kstrtou16(buf, 10, &data);
	if (ret || data >= (1 << chip->dac_bits))
		return -EINVAL;

	if (chip->dac_bits > 8) {
		lsb = data & (1 << offset);
		ret = chip->bus.write(chip->bus.client,
			ADT7316_DA_DATA_BASE + channel * 2, lsb);
		if (ret)
			return -EIO;
	}

	msb = data >> offset;
	ret = chip->bus.write(chip->bus.client,
		ADT7316_DA_DATA_BASE + 1 + channel * 2, msb);
	if (ret)
		return -EIO;

	return len;
}

static ssize_t adt7316_show_DAC_A(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_DAC(chip, 0, buf);
}

static ssize_t adt7316_store_DAC_A(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_store_DAC(chip, 0, buf, len);
}

static IIO_DEVICE_ATTR(DAC_A, S_IRUGO | S_IWUSR, adt7316_show_DAC_A,
		adt7316_store_DAC_A, 0);

static ssize_t adt7316_show_DAC_B(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_DAC(chip, 1, buf);
}

static ssize_t adt7316_store_DAC_B(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_store_DAC(chip, 1, buf, len);
}

static IIO_DEVICE_ATTR(DAC_B, S_IRUGO | S_IWUSR, adt7316_show_DAC_B,
		adt7316_store_DAC_B, 0);

static ssize_t adt7316_show_DAC_C(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_DAC(chip, 2, buf);
}

static ssize_t adt7316_store_DAC_C(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_store_DAC(chip, 2, buf, len);
}

static IIO_DEVICE_ATTR(DAC_C, S_IRUGO | S_IWUSR, adt7316_show_DAC_C,
		adt7316_store_DAC_C, 0);

static ssize_t adt7316_show_DAC_D(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_show_DAC(chip, 3, buf);
}

static ssize_t adt7316_store_DAC_D(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return adt7316_store_DAC(chip, 3, buf, len);
}

static IIO_DEVICE_ATTR(DAC_D, S_IRUGO | S_IWUSR, adt7316_show_DAC_D,
		adt7316_store_DAC_D, 0);

static ssize_t adt7316_show_device_id(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 id;
	int ret;

	ret = chip->bus.read(chip->bus.client, ADT7316_DEVICE_ID, &id);
	if (ret)
		return -EIO;

	return sprintf(buf, "%d\n", id);
}

static IIO_DEVICE_ATTR(device_id, S_IRUGO, adt7316_show_device_id, NULL, 0);

static ssize_t adt7316_show_manufactorer_id(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 id;
	int ret;

	ret = chip->bus.read(chip->bus.client, ADT7316_MANUFACTURE_ID, &id);
	if (ret)
		return -EIO;

	return sprintf(buf, "%d\n", id);
}

static IIO_DEVICE_ATTR(manufactorer_id, S_IRUGO,
		adt7316_show_manufactorer_id, NULL, 0);

static ssize_t adt7316_show_device_rev(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 rev;
	int ret;

	ret = chip->bus.read(chip->bus.client, ADT7316_DEVICE_REV, &rev);
	if (ret)
		return -EIO;

	return sprintf(buf, "%d\n", rev);
}

static IIO_DEVICE_ATTR(device_rev, S_IRUGO, adt7316_show_device_rev, NULL, 0);

static ssize_t adt7316_show_bus_type(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 stat;
	int ret;

	ret = chip->bus.read(chip->bus.client, ADT7316_SPI_LOCK_STAT, &stat);
	if (ret)
		return -EIO;

	if (stat)
		return sprintf(buf, "spi\n");

	return sprintf(buf, "i2c\n");
}

static IIO_DEVICE_ATTR(bus_type, S_IRUGO, adt7316_show_bus_type, NULL, 0);

static struct attribute *adt7316_attributes[] = {
	&iio_dev_attr_all_modes.dev_attr.attr,
	&iio_dev_attr_mode.dev_attr.attr,
	&iio_dev_attr_enabled.dev_attr.attr,
	&iio_dev_attr_ad_channel.dev_attr.attr,
	&iio_dev_attr_all_ad_channels.dev_attr.attr,
	&iio_dev_attr_disable_averaging.dev_attr.attr,
	&iio_dev_attr_enable_smbus_timeout.dev_attr.attr,
	&iio_dev_attr_powerdown.dev_attr.attr,
	&iio_dev_attr_fast_ad_clock.dev_attr.attr,
	&iio_dev_attr_da_high_resolution.dev_attr.attr,
	&iio_dev_attr_enable_proportion_DACA.dev_attr.attr,
	&iio_dev_attr_enable_proportion_DACB.dev_attr.attr,
	&iio_dev_attr_DAC_2Vref_channels_mask.dev_attr.attr,
	&iio_dev_attr_DAC_update_mode.dev_attr.attr,
	&iio_dev_attr_all_DAC_update_modes.dev_attr.attr,
	&iio_dev_attr_update_DAC.dev_attr.attr,
	&iio_dev_attr_DA_AB_Vref_bypass.dev_attr.attr,
	&iio_dev_attr_DA_CD_Vref_bypass.dev_attr.attr,
	&iio_dev_attr_DAC_internal_Vref.dev_attr.attr,
	&iio_dev_attr_VDD.dev_attr.attr,
	&iio_dev_attr_in_temp.dev_attr.attr,
	&iio_dev_attr_ex_temp.dev_attr.attr,
	&iio_dev_attr_in_temp_offset.dev_attr.attr,
	&iio_dev_attr_ex_temp_offset.dev_attr.attr,
	&iio_dev_attr_in_analog_temp_offset.dev_attr.attr,
	&iio_dev_attr_ex_analog_temp_offset.dev_attr.attr,
	&iio_dev_attr_DAC_A.dev_attr.attr,
	&iio_dev_attr_DAC_B.dev_attr.attr,
	&iio_dev_attr_DAC_C.dev_attr.attr,
	&iio_dev_attr_DAC_D.dev_attr.attr,
	&iio_dev_attr_device_id.dev_attr.attr,
	&iio_dev_attr_manufactorer_id.dev_attr.attr,
	&iio_dev_attr_device_rev.dev_attr.attr,
	&iio_dev_attr_bus_type.dev_attr.attr,
	NULL,
};

static const struct attribute_group adt7316_attribute_group = {
	.attrs = adt7316_attributes,
};

static struct attribute *adt7516_attributes[] = {
	&iio_dev_attr_all_modes.dev_attr.attr,
	&iio_dev_attr_mode.dev_attr.attr,
	&iio_dev_attr_select_ex_temp.dev_attr.attr,
	&iio_dev_attr_enabled.dev_attr.attr,
	&iio_dev_attr_ad_channel.dev_attr.attr,
	&iio_dev_attr_all_ad_channels.dev_attr.attr,
	&iio_dev_attr_disable_averaging.dev_attr.attr,
	&iio_dev_attr_enable_smbus_timeout.dev_attr.attr,
	&iio_dev_attr_powerdown.dev_attr.attr,
	&iio_dev_attr_fast_ad_clock.dev_attr.attr,
	&iio_dev_attr_AIN_internal_Vref.dev_attr.attr,
	&iio_dev_attr_da_high_resolution.dev_attr.attr,
	&iio_dev_attr_enable_proportion_DACA.dev_attr.attr,
	&iio_dev_attr_enable_proportion_DACB.dev_attr.attr,
	&iio_dev_attr_DAC_2Vref_channels_mask.dev_attr.attr,
	&iio_dev_attr_DAC_update_mode.dev_attr.attr,
	&iio_dev_attr_all_DAC_update_modes.dev_attr.attr,
	&iio_dev_attr_update_DAC.dev_attr.attr,
	&iio_dev_attr_DA_AB_Vref_bypass.dev_attr.attr,
	&iio_dev_attr_DA_CD_Vref_bypass.dev_attr.attr,
	&iio_dev_attr_DAC_internal_Vref.dev_attr.attr,
	&iio_dev_attr_VDD.dev_attr.attr,
	&iio_dev_attr_in_temp.dev_attr.attr,
	&iio_dev_attr_ex_temp_AIN1.dev_attr.attr,
	&iio_dev_attr_AIN2.dev_attr.attr,
	&iio_dev_attr_AIN3.dev_attr.attr,
	&iio_dev_attr_AIN4.dev_attr.attr,
	&iio_dev_attr_in_temp_offset.dev_attr.attr,
	&iio_dev_attr_ex_temp_offset.dev_attr.attr,
	&iio_dev_attr_in_analog_temp_offset.dev_attr.attr,
	&iio_dev_attr_ex_analog_temp_offset.dev_attr.attr,
	&iio_dev_attr_DAC_A.dev_attr.attr,
	&iio_dev_attr_DAC_B.dev_attr.attr,
	&iio_dev_attr_DAC_C.dev_attr.attr,
	&iio_dev_attr_DAC_D.dev_attr.attr,
	&iio_dev_attr_device_id.dev_attr.attr,
	&iio_dev_attr_manufactorer_id.dev_attr.attr,
	&iio_dev_attr_device_rev.dev_attr.attr,
	&iio_dev_attr_bus_type.dev_attr.attr,
	NULL,
};

static const struct attribute_group adt7516_attribute_group = {
	.attrs = adt7516_attributes,
};

static irqreturn_t adt7316_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct adt7316_chip_info *chip = iio_priv(indio_dev);
	u8 stat1, stat2;
	int ret;
	s64 time;

	ret = chip->bus.read(chip->bus.client, ADT7316_INT_STAT1, &stat1);
	if (!ret) {
		if ((chip->id & ID_FAMILY_MASK) != ID_ADT75XX)
			stat1 &= 0x1F;

		time = iio_get_time_ns();
		if (stat1 & BIT(0))
			iio_push_event(indio_dev,
				       IIO_UNMOD_EVENT_CODE(IIO_TEMP, 0,
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_RISING),
				       time);
		if (stat1 & BIT(1))
			iio_push_event(indio_dev,
				       IIO_UNMOD_EVENT_CODE(IIO_TEMP, 0,
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_FALLING),
				       time);
		if (stat1 & BIT(2))
			iio_push_event(indio_dev,
				       IIO_UNMOD_EVENT_CODE(IIO_TEMP, 1,
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_RISING),
				       time);
		if (stat1 & BIT(3))
			iio_push_event(indio_dev,
				       IIO_UNMOD_EVENT_CODE(IIO_TEMP, 1,
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_FALLING),
				       time);
		if (stat1 & BIT(5))
			iio_push_event(indio_dev,
				       IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE, 1,
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_EITHER),
				       time);
		if (stat1 & BIT(6))
			iio_push_event(indio_dev,
				       IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE, 2,
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_EITHER),
				       time);
		if (stat1 & BIT(7))
			iio_push_event(indio_dev,
				       IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE, 3,
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_EITHER),
				       time);
		}
	ret = chip->bus.read(chip->bus.client, ADT7316_INT_STAT2, &stat2);
	if (!ret) {
		if (stat2 & ADT7316_INT_MASK2_VDD)
			iio_push_event(indio_dev,
				       IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE,
							    0,
							    IIO_EV_TYPE_THRESH,
							    IIO_EV_DIR_RISING),
				       iio_get_time_ns());
	}

	return IRQ_HANDLED;
}

/*
 * Show mask of enabled interrupts in Hex.
 */
static ssize_t adt7316_show_int_mask(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "0x%x\n", chip->int_mask);
}

/*
 * Set 1 to the mask in Hex to enabled interrupts.
 */
static ssize_t adt7316_set_int_mask(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u16 data;
	int ret;
	u8 mask;

	ret = kstrtou16(buf, 16, &data);
	if (ret || data >= ADT7316_VDD_INT_MASK + 1)
		return -EINVAL;

	if (data & ADT7316_VDD_INT_MASK)
		mask = 0;			/* enable vdd int */
	else
		mask = ADT7316_INT_MASK2_VDD;	/* disable vdd int */

	ret = chip->bus.write(chip->bus.client, ADT7316_INT_MASK2, mask);
	if (!ret) {
		chip->int_mask &= ~ADT7316_VDD_INT_MASK;
		chip->int_mask |= data & ADT7316_VDD_INT_MASK;
	}

	if (data & ADT7316_TEMP_AIN_INT_MASK) {
		if ((chip->id & ID_FAMILY_MASK) == ID_ADT73XX)
			/* mask in reg is opposite, set 1 to disable */
			mask = (~data) & ADT7316_TEMP_INT_MASK;
		else
			/* mask in reg is opposite, set 1 to disable */
			mask = (~data) & ADT7316_TEMP_AIN_INT_MASK;
	}
	ret = chip->bus.write(chip->bus.client, ADT7316_INT_MASK1, mask);

	chip->int_mask = mask;

	return len;
}
static inline ssize_t adt7316_show_ad_bound(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 val;
	int data;
	int ret;

	if ((chip->id & ID_FAMILY_MASK) == ID_ADT73XX &&
		this_attr->address > ADT7316_EX_TEMP_LOW)
		return -EPERM;

	ret = chip->bus.read(chip->bus.client, this_attr->address, &val);
	if (ret)
		return -EIO;

	data = (int)val;

	if (!((chip->id & ID_FAMILY_MASK) == ID_ADT75XX &&
		(chip->config1 & ADT7516_SEL_AIN1_2_EX_TEMP_MASK) == 0)) {
		if (data & 0x80)
			data -= 256;
	}

	return sprintf(buf, "%d\n", data);
}

static inline ssize_t adt7316_set_ad_bound(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	int data;
	u8 val;
	int ret;

	if ((chip->id & ID_FAMILY_MASK) == ID_ADT73XX &&
		this_attr->address > ADT7316_EX_TEMP_LOW)
		return -EPERM;

	ret = kstrtoint(buf, 10, &data);
	if (ret)
		return -EINVAL;

	if ((chip->id & ID_FAMILY_MASK) == ID_ADT75XX &&
		(chip->config1 & ADT7516_SEL_AIN1_2_EX_TEMP_MASK) == 0) {
		if (data > 255 || data < 0)
			return -EINVAL;
	} else {
		if (data > 127 || data < -128)
			return -EINVAL;

		if (data < 0)
			data += 256;
	}

	val = (u8)data;

	ret = chip->bus.write(chip->bus.client, this_attr->address, val);
	if (ret)
		return -EIO;

	return len;
}

static ssize_t adt7316_show_int_enabled(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return sprintf(buf, "%d\n", !!(chip->config1 & ADT7316_INT_EN));
}

static ssize_t adt7316_set_int_enabled(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_to_iio_dev(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);
	u8 config1;
	int ret;

	config1 = chip->config1 & (~ADT7316_INT_EN);
	if (buf[0] == '1')
		config1 |= ADT7316_INT_EN;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG1, config1);
	if (ret)
		return -EIO;

	chip->config1 = config1;

	return len;
}

static IIO_DEVICE_ATTR(int_mask,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_int_mask, adt7316_set_int_mask,
		       0);
static IIO_DEVICE_ATTR(in_temp_high_value,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_ad_bound, adt7316_set_ad_bound,
		       ADT7316_IN_TEMP_HIGH);
static IIO_DEVICE_ATTR(in_temp_low_value,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_ad_bound, adt7316_set_ad_bound,
		       ADT7316_IN_TEMP_LOW);
static IIO_DEVICE_ATTR(ex_temp_high_value,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_ad_bound, adt7316_set_ad_bound,
		       ADT7316_EX_TEMP_HIGH);
static IIO_DEVICE_ATTR(ex_temp_low_value,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_ad_bound, adt7316_set_ad_bound,
		       ADT7316_EX_TEMP_LOW);

/* NASTY duplication to be fixed */
static IIO_DEVICE_ATTR(ex_temp_ain1_high_value,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_ad_bound, adt7316_set_ad_bound,
		       ADT7316_EX_TEMP_HIGH);
static IIO_DEVICE_ATTR(ex_temp_ain1_low_value,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_ad_bound, adt7316_set_ad_bound,
		       ADT7316_EX_TEMP_LOW);
static IIO_DEVICE_ATTR(ain2_high_value,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_ad_bound, adt7316_set_ad_bound,
		       ADT7516_AIN2_HIGH);
static IIO_DEVICE_ATTR(ain2_low_value,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_ad_bound, adt7316_set_ad_bound,
		       ADT7516_AIN2_LOW);
static IIO_DEVICE_ATTR(ain3_high_value,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_ad_bound, adt7316_set_ad_bound,
		       ADT7516_AIN3_HIGH);
static IIO_DEVICE_ATTR(ain3_low_value,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_ad_bound, adt7316_set_ad_bound,
		       ADT7516_AIN3_LOW);
static IIO_DEVICE_ATTR(ain4_high_value,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_ad_bound, adt7316_set_ad_bound,
		       ADT7516_AIN4_HIGH);
static IIO_DEVICE_ATTR(ain4_low_value,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_ad_bound, adt7316_set_ad_bound,
		       ADT7516_AIN4_LOW);
static IIO_DEVICE_ATTR(int_enabled,
		       S_IRUGO | S_IWUSR,
		       adt7316_show_int_enabled,
		       adt7316_set_int_enabled, 0);

static struct attribute *adt7316_event_attributes[] = {
	&iio_dev_attr_int_mask.dev_attr.attr,
	&iio_dev_attr_in_temp_high_value.dev_attr.attr,
	&iio_dev_attr_in_temp_low_value.dev_attr.attr,
	&iio_dev_attr_ex_temp_high_value.dev_attr.attr,
	&iio_dev_attr_ex_temp_low_value.dev_attr.attr,
	&iio_dev_attr_int_enabled.dev_attr.attr,
	NULL,
};

static struct attribute_group adt7316_event_attribute_group = {
	.attrs = adt7316_event_attributes,
	.name = "events",
};

static struct attribute *adt7516_event_attributes[] = {
	&iio_dev_attr_int_mask.dev_attr.attr,
	&iio_dev_attr_in_temp_high_value.dev_attr.attr,
	&iio_dev_attr_in_temp_low_value.dev_attr.attr,
	&iio_dev_attr_ex_temp_ain1_high_value.dev_attr.attr,
	&iio_dev_attr_ex_temp_ain1_low_value.dev_attr.attr,
	&iio_dev_attr_ain2_high_value.dev_attr.attr,
	&iio_dev_attr_ain2_low_value.dev_attr.attr,
	&iio_dev_attr_ain3_high_value.dev_attr.attr,
	&iio_dev_attr_ain3_low_value.dev_attr.attr,
	&iio_dev_attr_ain4_high_value.dev_attr.attr,
	&iio_dev_attr_ain4_low_value.dev_attr.attr,
	&iio_dev_attr_int_enabled.dev_attr.attr,
	NULL,
};

static struct attribute_group adt7516_event_attribute_group = {
	.attrs = adt7516_event_attributes,
	.name = "events",
};

#ifdef CONFIG_PM_SLEEP
static int adt7316_disable(struct device *dev)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return _adt7316_store_enabled(chip, 0);
}

static int adt7316_enable(struct device *dev)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct adt7316_chip_info *chip = iio_priv(dev_info);

	return _adt7316_store_enabled(chip, 1);
}

SIMPLE_DEV_PM_OPS(adt7316_pm_ops, adt7316_disable, adt7316_enable);
EXPORT_SYMBOL_GPL(adt7316_pm_ops);
#endif

static const struct iio_info adt7316_info = {
	.attrs = &adt7316_attribute_group,
	.event_attrs = &adt7316_event_attribute_group,
	.driver_module = THIS_MODULE,
};

static const struct iio_info adt7516_info = {
	.attrs = &adt7516_attribute_group,
	.event_attrs = &adt7516_event_attribute_group,
	.driver_module = THIS_MODULE,
};

/*
 * device probe and remove
 */
int adt7316_probe(struct device *dev, struct adt7316_bus *bus,
		const char *name)
{
	struct adt7316_chip_info *chip;
	struct iio_dev *indio_dev;
	unsigned short *adt7316_platform_data = dev->platform_data;
	int ret = 0;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;
	chip = iio_priv(indio_dev);
	/* this is only used for device removal purposes */
	dev_set_drvdata(dev, indio_dev);

	chip->bus = *bus;

	if (name[4] == '3')
		chip->id = ID_ADT7316 + (name[6] - '6');
	else if (name[4] == '5')
		chip->id = ID_ADT7516 + (name[6] - '6');
	else
		return -ENODEV;

	chip->ldac_pin = adt7316_platform_data[1];
	if (chip->ldac_pin) {
		chip->config3 |= ADT7316_DA_EN_VIA_DAC_LDCA;
		if ((chip->id & ID_FAMILY_MASK) == ID_ADT75XX)
			chip->config1 |= ADT7516_SEL_AIN3;
	}
	chip->int_mask = ADT7316_TEMP_INT_MASK | ADT7316_VDD_INT_MASK;
	if ((chip->id & ID_FAMILY_MASK) == ID_ADT75XX)
		chip->int_mask |= ADT7516_AIN_INT_MASK;

	indio_dev->dev.parent = dev;
	if ((chip->id & ID_FAMILY_MASK) == ID_ADT75XX)
		indio_dev->info = &adt7516_info;
	else
		indio_dev->info = &adt7316_info;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (chip->bus.irq > 0) {
		if (adt7316_platform_data[0])
			chip->bus.irq_flags = adt7316_platform_data[0];

		ret = devm_request_threaded_irq(dev, chip->bus.irq,
						NULL,
						adt7316_event_handler,
						chip->bus.irq_flags |
						IRQF_ONESHOT,
						indio_dev->name,
						indio_dev);
		if (ret)
			return ret;

		if (chip->bus.irq_flags & IRQF_TRIGGER_HIGH)
			chip->config1 |= ADT7316_INT_POLARITY;
	}

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG1, chip->config1);
	if (ret)
		return -EIO;

	ret = chip->bus.write(chip->bus.client, ADT7316_CONFIG3, chip->config3);
	if (ret)
		return -EIO;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return ret;

	dev_info(dev, "%s temperature sensor, ADC and DAC registered.\n",
			indio_dev->name);

	return 0;
}
EXPORT_SYMBOL(adt7316_probe);

MODULE_AUTHOR("Sonic Zhang <sonic.zhang@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADT7316/7/8 and ADT7516/7/9 digital temperature sensor, ADC and DAC driver");
MODULE_LICENSE("GPL v2");
