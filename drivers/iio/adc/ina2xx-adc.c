/*
 * INA2XX Current and Power Monitors
 *
 * Copyright 2015 Baylibre SAS.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on linux/drivers/iio/adc/ad7291.c
 * Copyright 2010-2011 Analog Devices Inc.
 *
 * Based on linux/drivers/hwmon/ina2xx.c
 * Copyright 2012 Lothar Felten <l-felten@ti.com>
 *
 * Licensed under the GPL-2 or later.
 *
 * IIO driver for INA219-220-226-230-231
 *
 * Configurable 7-bit I2C slave address from 0x40 to 0x4F
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/sysfs.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/sched/task.h>
#include <linux/util_macros.h>

#include <linux/platform_data/ina2xx.h>

/* INA2XX registers definition */
#define INA2XX_CONFIG                   0x00
#define INA2XX_SHUNT_VOLTAGE            0x01	/* readonly */
#define INA2XX_BUS_VOLTAGE              0x02	/* readonly */
#define INA2XX_POWER                    0x03	/* readonly */
#define INA2XX_CURRENT                  0x04	/* readonly */
#define INA2XX_CALIBRATION              0x05

#define INA226_MASK_ENABLE		0x06
#define INA226_CVRF			BIT(3)

#define INA2XX_MAX_REGISTERS            8

/* settings - depend on use case */
#define INA219_CONFIG_DEFAULT           0x399F	/* PGA=1/8, BRNG=32V */
#define INA219_DEFAULT_IT		532
#define INA219_DEFAULT_BRNG             1   /* 32V */
#define INA219_DEFAULT_PGA              125 /* 1000/8 */
#define INA226_CONFIG_DEFAULT           0x4327
#define INA226_DEFAULT_AVG              4
#define INA226_DEFAULT_IT		1110

#define INA2XX_RSHUNT_DEFAULT           10000

/*
 * bit masks for reading the settings in the configuration register
 * FIXME: use regmap_fields.
 */
#define INA2XX_MODE_MASK	GENMASK(3, 0)

/* Gain for VShunt: 1/8 (default), 1/4, 1/2, 1 */
#define INA219_PGA_MASK		GENMASK(12, 11)
#define INA219_SHIFT_PGA(val)	((val) << 11)

/* VBus range: 32V (default), 16V */
#define INA219_BRNG_MASK	BIT(13)
#define INA219_SHIFT_BRNG(val)	((val) << 13)

/* Averaging for VBus/VShunt/Power */
#define INA226_AVG_MASK		GENMASK(11, 9)
#define INA226_SHIFT_AVG(val)	((val) << 9)

/* Integration time for VBus */
#define INA219_ITB_MASK		GENMASK(10, 7)
#define INA219_SHIFT_ITB(val)	((val) << 7)
#define INA226_ITB_MASK		GENMASK(8, 6)
#define INA226_SHIFT_ITB(val)	((val) << 6)

/* Integration time for VShunt */
#define INA219_ITS_MASK		GENMASK(6, 3)
#define INA219_SHIFT_ITS(val)	((val) << 3)
#define INA226_ITS_MASK		GENMASK(5, 3)
#define INA226_SHIFT_ITS(val)	((val) << 3)

/* INA219 Bus voltage register, low bits are flags */
#define INA219_OVF		BIT(0)
#define INA219_CNVR		BIT(1)
#define INA219_BUS_VOLTAGE_SHIFT	3

/* Cosmetic macro giving the sampling period for a full P=UxI cycle */
#define SAMPLING_PERIOD(c)	((c->int_time_vbus + c->int_time_vshunt) \
				 * c->avg)

static bool ina2xx_is_writeable_reg(struct device *dev, unsigned int reg)
{
	return (reg == INA2XX_CONFIG) || (reg > INA2XX_CURRENT);
}

static bool ina2xx_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return (reg != INA2XX_CONFIG);
}

static inline bool is_signed_reg(unsigned int reg)
{
	return (reg == INA2XX_SHUNT_VOLTAGE) || (reg == INA2XX_CURRENT);
}

static const struct regmap_config ina2xx_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = INA2XX_MAX_REGISTERS,
	.writeable_reg = ina2xx_is_writeable_reg,
	.volatile_reg = ina2xx_is_volatile_reg,
};

enum ina2xx_ids { ina219, ina226 };

struct ina2xx_config {
	u16 config_default;
	int calibration_value;
	int shunt_voltage_lsb;	/* nV */
	int bus_voltage_shift;	/* position of lsb */
	int bus_voltage_lsb;	/* uV */
	/* fixed relation between current and power lsb, uW/uA */
	int power_lsb_factor;
	enum ina2xx_ids chip_id;
};

struct ina2xx_chip_info {
	struct regmap *regmap;
	struct task_struct *task;
	const struct ina2xx_config *config;
	struct mutex state_lock;
	unsigned int shunt_resistor_uohm;
	int avg;
	int int_time_vbus; /* Bus voltage integration time uS */
	int int_time_vshunt; /* Shunt voltage integration time uS */
	int range_vbus; /* Bus voltage maximum in V */
	int pga_gain_vshunt; /* Shunt voltage PGA gain */
	bool allow_async_readout;
	/* data buffer needs space for channel data and timestamp */
	struct {
		u16 chan[4];
		u64 ts __aligned(8);
	} scan;
};

static const struct ina2xx_config ina2xx_config[] = {
	[ina219] = {
		.config_default = INA219_CONFIG_DEFAULT,
		.calibration_value = 4096,
		.shunt_voltage_lsb = 10000,
		.bus_voltage_shift = INA219_BUS_VOLTAGE_SHIFT,
		.bus_voltage_lsb = 4000,
		.power_lsb_factor = 20,
		.chip_id = ina219,
	},
	[ina226] = {
		.config_default = INA226_CONFIG_DEFAULT,
		.calibration_value = 2048,
		.shunt_voltage_lsb = 2500,
		.bus_voltage_shift = 0,
		.bus_voltage_lsb = 1250,
		.power_lsb_factor = 25,
		.chip_id = ina226,
	},
};

static int ina2xx_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	int ret;
	struct ina2xx_chip_info *chip = iio_priv(indio_dev);
	unsigned int regval;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_read(chip->regmap, chan->address, &regval);
		if (ret)
			return ret;

		if (is_signed_reg(chan->address))
			*val = (s16) regval;
		else
			*val  = regval;

		if (chan->address == INA2XX_BUS_VOLTAGE)
			*val >>= chip->config->bus_voltage_shift;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*val = chip->avg;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		if (chan->address == INA2XX_SHUNT_VOLTAGE)
			*val2 = chip->int_time_vshunt;
		else
			*val2 = chip->int_time_vbus;

		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_CHAN_INFO_SAMP_FREQ:
		/*
		 * Sample freq is read only, it is a consequence of
		 * 1/AVG*(CT_bus+CT_shunt).
		 */
		*val = DIV_ROUND_CLOSEST(1000000, SAMPLING_PERIOD(chip));

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		switch (chan->address) {
		case INA2XX_SHUNT_VOLTAGE:
			/* processed (mV) = raw * lsb(nV) / 1000000 */
			*val = chip->config->shunt_voltage_lsb;
			*val2 = 1000000;
			return IIO_VAL_FRACTIONAL;

		case INA2XX_BUS_VOLTAGE:
			/* processed (mV) = raw * lsb (uV) / 1000 */
			*val = chip->config->bus_voltage_lsb;
			*val2 = 1000;
			return IIO_VAL_FRACTIONAL;

		case INA2XX_CURRENT:
			/*
			 * processed (mA) = raw * current_lsb (mA)
			 * current_lsb (mA) = shunt_voltage_lsb (nV) /
			 *                    shunt_resistor (uOhm)
			 */
			*val = chip->config->shunt_voltage_lsb;
			*val2 = chip->shunt_resistor_uohm;
			return IIO_VAL_FRACTIONAL;

		case INA2XX_POWER:
			/*
			 * processed (mW) = raw * power_lsb (mW)
			 * power_lsb (mW) = power_lsb_factor (mW/mA) *
			 *                  current_lsb (mA)
			 */
			*val = chip->config->power_lsb_factor *
			       chip->config->shunt_voltage_lsb;
			*val2 = chip->shunt_resistor_uohm;
			return IIO_VAL_FRACTIONAL;
		}
		return -EINVAL;

	case IIO_CHAN_INFO_HARDWAREGAIN:
		switch (chan->address) {
		case INA2XX_SHUNT_VOLTAGE:
			*val = chip->pga_gain_vshunt;
			*val2 = 1000;
			return IIO_VAL_FRACTIONAL;

		case INA2XX_BUS_VOLTAGE:
			*val = chip->range_vbus == 32 ? 1 : 2;
			return IIO_VAL_INT;
		}
		return -EINVAL;
	}

	return -EINVAL;
}

/*
 * Available averaging rates for ina226. The indices correspond with
 * the bit values expected by the chip (according to the ina226 datasheet,
 * table 3 AVG bit settings, found at
 * https://www.ti.com/lit/ds/symlink/ina226.pdf.
 */
static const int ina226_avg_tab[] = { 1, 4, 16, 64, 128, 256, 512, 1024 };

static int ina226_set_average(struct ina2xx_chip_info *chip, unsigned int val,
			      unsigned int *config)
{
	int bits;

	if (val > 1024 || val < 1)
		return -EINVAL;

	bits = find_closest(val, ina226_avg_tab,
			    ARRAY_SIZE(ina226_avg_tab));

	chip->avg = ina226_avg_tab[bits];

	*config &= ~INA226_AVG_MASK;
	*config |= INA226_SHIFT_AVG(bits) & INA226_AVG_MASK;

	return 0;
}

/* Conversion times in uS */
static const int ina226_conv_time_tab[] = { 140, 204, 332, 588, 1100,
					    2116, 4156, 8244 };

static int ina226_set_int_time_vbus(struct ina2xx_chip_info *chip,
				    unsigned int val_us, unsigned int *config)
{
	int bits;

	if (val_us > 8244 || val_us < 140)
		return -EINVAL;

	bits = find_closest(val_us, ina226_conv_time_tab,
			    ARRAY_SIZE(ina226_conv_time_tab));

	chip->int_time_vbus = ina226_conv_time_tab[bits];

	*config &= ~INA226_ITB_MASK;
	*config |= INA226_SHIFT_ITB(bits) & INA226_ITB_MASK;

	return 0;
}

static int ina226_set_int_time_vshunt(struct ina2xx_chip_info *chip,
				      unsigned int val_us, unsigned int *config)
{
	int bits;

	if (val_us > 8244 || val_us < 140)
		return -EINVAL;

	bits = find_closest(val_us, ina226_conv_time_tab,
			    ARRAY_SIZE(ina226_conv_time_tab));

	chip->int_time_vshunt = ina226_conv_time_tab[bits];

	*config &= ~INA226_ITS_MASK;
	*config |= INA226_SHIFT_ITS(bits) & INA226_ITS_MASK;

	return 0;
}

/* Conversion times in uS. */
static const int ina219_conv_time_tab_subsample[] = { 84, 148, 276, 532 };
static const int ina219_conv_time_tab_average[] = { 532, 1060, 2130, 4260,
						    8510, 17020, 34050, 68100};

static int ina219_lookup_int_time(unsigned int *val_us, int *bits)
{
	if (*val_us > 68100 || *val_us < 84)
		return -EINVAL;

	if (*val_us <= 532) {
		*bits = find_closest(*val_us, ina219_conv_time_tab_subsample,
				    ARRAY_SIZE(ina219_conv_time_tab_subsample));
		*val_us = ina219_conv_time_tab_subsample[*bits];
	} else {
		*bits = find_closest(*val_us, ina219_conv_time_tab_average,
				    ARRAY_SIZE(ina219_conv_time_tab_average));
		*val_us = ina219_conv_time_tab_average[*bits];
		*bits |= 0x8;
	}

	return 0;
}

static int ina219_set_int_time_vbus(struct ina2xx_chip_info *chip,
				    unsigned int val_us, unsigned int *config)
{
	int bits, ret;
	unsigned int val_us_best = val_us;

	ret = ina219_lookup_int_time(&val_us_best, &bits);
	if (ret)
		return ret;

	chip->int_time_vbus = val_us_best;

	*config &= ~INA219_ITB_MASK;
	*config |= INA219_SHIFT_ITB(bits) & INA219_ITB_MASK;

	return 0;
}

static int ina219_set_int_time_vshunt(struct ina2xx_chip_info *chip,
				      unsigned int val_us, unsigned int *config)
{
	int bits, ret;
	unsigned int val_us_best = val_us;

	ret = ina219_lookup_int_time(&val_us_best, &bits);
	if (ret)
		return ret;

	chip->int_time_vshunt = val_us_best;

	*config &= ~INA219_ITS_MASK;
	*config |= INA219_SHIFT_ITS(bits) & INA219_ITS_MASK;

	return 0;
}

static const int ina219_vbus_range_tab[] = { 1, 2 };
static int ina219_set_vbus_range_denom(struct ina2xx_chip_info *chip,
				       unsigned int range,
				       unsigned int *config)
{
	if (range == 1)
		chip->range_vbus = 32;
	else if (range == 2)
		chip->range_vbus = 16;
	else
		return -EINVAL;

	*config &= ~INA219_BRNG_MASK;
	*config |= INA219_SHIFT_BRNG(range == 1 ? 1 : 0) & INA219_BRNG_MASK;

	return 0;
}

static const int ina219_vshunt_gain_tab[] = { 125, 250, 500, 1000 };
static const int ina219_vshunt_gain_frac[] = {
	125, 1000, 250, 1000, 500, 1000, 1000, 1000 };

static int ina219_set_vshunt_pga_gain(struct ina2xx_chip_info *chip,
				      unsigned int gain,
				      unsigned int *config)
{
	int bits;

	if (gain < 125 || gain > 1000)
		return -EINVAL;

	bits = find_closest(gain, ina219_vshunt_gain_tab,
			    ARRAY_SIZE(ina219_vshunt_gain_tab));

	chip->pga_gain_vshunt = ina219_vshunt_gain_tab[bits];
	bits = 3 - bits;

	*config &= ~INA219_PGA_MASK;
	*config |= INA219_SHIFT_PGA(bits) & INA219_PGA_MASK;

	return 0;
}

static int ina2xx_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		switch (chan->address) {
		case INA2XX_SHUNT_VOLTAGE:
			*type = IIO_VAL_FRACTIONAL;
			*length = sizeof(ina219_vshunt_gain_frac) / sizeof(int);
			*vals = ina219_vshunt_gain_frac;
			return IIO_AVAIL_LIST;

		case INA2XX_BUS_VOLTAGE:
			*type = IIO_VAL_INT;
			*length = sizeof(ina219_vbus_range_tab) / sizeof(int);
			*vals = ina219_vbus_range_tab;
			return IIO_AVAIL_LIST;
		}
	}

	return -EINVAL;
}

static int ina2xx_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ina2xx_chip_info *chip = iio_priv(indio_dev);
	unsigned int config, tmp;
	int ret;

	if (iio_buffer_enabled(indio_dev))
		return -EBUSY;

	mutex_lock(&chip->state_lock);

	ret = regmap_read(chip->regmap, INA2XX_CONFIG, &config);
	if (ret)
		goto err;

	tmp = config;

	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		ret = ina226_set_average(chip, val, &tmp);
		break;

	case IIO_CHAN_INFO_INT_TIME:
		if (chip->config->chip_id == ina226) {
			if (chan->address == INA2XX_SHUNT_VOLTAGE)
				ret = ina226_set_int_time_vshunt(chip, val2,
								 &tmp);
			else
				ret = ina226_set_int_time_vbus(chip, val2,
							       &tmp);
		} else {
			if (chan->address == INA2XX_SHUNT_VOLTAGE)
				ret = ina219_set_int_time_vshunt(chip, val2,
								 &tmp);
			else
				ret = ina219_set_int_time_vbus(chip, val2,
							       &tmp);
		}
		break;

	case IIO_CHAN_INFO_HARDWAREGAIN:
		if (chan->address == INA2XX_SHUNT_VOLTAGE)
			ret = ina219_set_vshunt_pga_gain(chip, val * 1000 +
							 val2 / 1000, &tmp);
		else
			ret = ina219_set_vbus_range_denom(chip, val, &tmp);
		break;

	default:
		ret = -EINVAL;
	}

	if (!ret && (tmp != config))
		ret = regmap_write(chip->regmap, INA2XX_CONFIG, tmp);
err:
	mutex_unlock(&chip->state_lock);

	return ret;
}

static ssize_t ina2xx_allow_async_readout_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct ina2xx_chip_info *chip = iio_priv(dev_to_iio_dev(dev));

	return sysfs_emit(buf, "%d\n", chip->allow_async_readout);
}

static ssize_t ina2xx_allow_async_readout_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct ina2xx_chip_info *chip = iio_priv(dev_to_iio_dev(dev));
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	chip->allow_async_readout = val;

	return len;
}

/*
 * Calibration register is set to the best value, which eliminates
 * truncation errors on calculating current register in hardware.
 * According to datasheet (INA 226: eq. 3, INA219: eq. 4) the best values
 * are 2048 for ina226 and 4096 for ina219. They are hardcoded as
 * calibration_value.
 */
static int ina2xx_set_calibration(struct ina2xx_chip_info *chip)
{
	return regmap_write(chip->regmap, INA2XX_CALIBRATION,
			    chip->config->calibration_value);
}

static int set_shunt_resistor(struct ina2xx_chip_info *chip, unsigned int val)
{
	if (val == 0 || val > INT_MAX)
		return -EINVAL;

	chip->shunt_resistor_uohm = val;

	return 0;
}

static ssize_t ina2xx_shunt_resistor_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct ina2xx_chip_info *chip = iio_priv(dev_to_iio_dev(dev));
	int vals[2] = { chip->shunt_resistor_uohm, 1000000 };

	return iio_format_value(buf, IIO_VAL_FRACTIONAL, 1, vals);
}

static ssize_t ina2xx_shunt_resistor_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t len)
{
	struct ina2xx_chip_info *chip = iio_priv(dev_to_iio_dev(dev));
	int val, val_fract, ret;

	ret = iio_str_to_fixpoint(buf, 100000, &val, &val_fract);
	if (ret)
		return ret;

	ret = set_shunt_resistor(chip, val * 1000000 + val_fract);
	if (ret)
		return ret;

	return len;
}

#define INA219_CHAN(_type, _index, _address) { \
	.type = (_type), \
	.address = (_address), \
	.indexed = 1, \
	.channel = (_index), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_dir = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_index = (_index), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	} \
}

#define INA226_CHAN(_type, _index, _address) { \
	.type = (_type), \
	.address = (_address), \
	.indexed = 1, \
	.channel = (_index), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_dir = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
				   BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
	.scan_index = (_index), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	} \
}

/*
 * Sampling Freq is a consequence of the integration times of
 * the Voltage channels.
 */
#define INA219_CHAN_VOLTAGE(_index, _address, _shift) { \
	.type = IIO_VOLTAGE, \
	.address = (_address), \
	.indexed = 1, \
	.channel = (_index), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_SCALE) | \
			      BIT(IIO_CHAN_INFO_INT_TIME) | \
			      BIT(IIO_CHAN_INFO_HARDWAREGAIN), \
	.info_mask_separate_available = \
			      BIT(IIO_CHAN_INFO_HARDWAREGAIN), \
	.info_mask_shared_by_dir = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_index = (_index), \
	.scan_type = { \
		.sign = 'u', \
		.shift = _shift, \
		.realbits = 16 - _shift, \
		.storagebits = 16, \
		.endianness = IIO_LE, \
	} \
}

#define INA226_CHAN_VOLTAGE(_index, _address) { \
	.type = IIO_VOLTAGE, \
	.address = (_address), \
	.indexed = 1, \
	.channel = (_index), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_SCALE) | \
			      BIT(IIO_CHAN_INFO_INT_TIME), \
	.info_mask_shared_by_dir = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
				   BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
	.scan_index = (_index), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_LE, \
	} \
}


static const struct iio_chan_spec ina226_channels[] = {
	INA226_CHAN_VOLTAGE(0, INA2XX_SHUNT_VOLTAGE),
	INA226_CHAN_VOLTAGE(1, INA2XX_BUS_VOLTAGE),
	INA226_CHAN(IIO_POWER, 2, INA2XX_POWER),
	INA226_CHAN(IIO_CURRENT, 3, INA2XX_CURRENT),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const struct iio_chan_spec ina219_channels[] = {
	INA219_CHAN_VOLTAGE(0, INA2XX_SHUNT_VOLTAGE, 0),
	INA219_CHAN_VOLTAGE(1, INA2XX_BUS_VOLTAGE, INA219_BUS_VOLTAGE_SHIFT),
	INA219_CHAN(IIO_POWER, 2, INA2XX_POWER),
	INA219_CHAN(IIO_CURRENT, 3, INA2XX_CURRENT),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static int ina2xx_conversion_ready(struct iio_dev *indio_dev)
{
	struct ina2xx_chip_info *chip = iio_priv(indio_dev);
	int ret;
	unsigned int alert;

	/*
	 * Because the timer thread and the chip conversion clock
	 * are asynchronous, the period difference will eventually
	 * result in reading V[k-1] again, or skip V[k] at time Tk.
	 * In order to resync the timer with the conversion process
	 * we check the ConVersionReadyFlag.
	 * On hardware that supports using the ALERT pin to toggle a
	 * GPIO a triggered buffer could be used instead.
	 * For now, we do an extra read of the MASK_ENABLE register (INA226)
	 * resp. the BUS_VOLTAGE register (INA219).
	 */
	if (chip->config->chip_id == ina226) {
		ret = regmap_read(chip->regmap,
				  INA226_MASK_ENABLE, &alert);
		alert &= INA226_CVRF;
	} else {
		ret = regmap_read(chip->regmap,
				  INA2XX_BUS_VOLTAGE, &alert);
		alert &= INA219_CNVR;
	}

	if (ret < 0)
		return ret;

	return !!alert;
}

static int ina2xx_work_buffer(struct iio_dev *indio_dev)
{
	struct ina2xx_chip_info *chip = iio_priv(indio_dev);
	int bit, ret, i = 0;
	s64 time;

	time = iio_get_time_ns(indio_dev);

	/*
	 * Single register reads: bulk_read will not work with ina226/219
	 * as there is no auto-increment of the register pointer.
	 */
	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		unsigned int val;

		ret = regmap_read(chip->regmap,
				  INA2XX_SHUNT_VOLTAGE + bit, &val);
		if (ret < 0)
			return ret;

		chip->scan.chan[i++] = val;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, &chip->scan, time);

	return 0;
};

static int ina2xx_capture_thread(void *data)
{
	struct iio_dev *indio_dev = data;
	struct ina2xx_chip_info *chip = iio_priv(indio_dev);
	int sampling_us = SAMPLING_PERIOD(chip);
	int ret;
	struct timespec64 next, now, delta;
	s64 delay_us;

	/*
	 * Poll a bit faster than the chip internal Fs, in case
	 * we wish to sync with the conversion ready flag.
	 */
	if (!chip->allow_async_readout)
		sampling_us -= 200;

	ktime_get_ts64(&next);

	do {
		while (!chip->allow_async_readout) {
			ret = ina2xx_conversion_ready(indio_dev);
			if (ret < 0)
				return ret;

			/*
			 * If the conversion was not yet finished,
			 * reset the reference timestamp.
			 */
			if (ret == 0)
				ktime_get_ts64(&next);
			else
				break;
		}

		ret = ina2xx_work_buffer(indio_dev);
		if (ret < 0)
			return ret;

		ktime_get_ts64(&now);

		/*
		 * Advance the timestamp for the next poll by one sampling
		 * interval, and sleep for the remainder (next - now)
		 * In case "next" has already passed, the interval is added
		 * multiple times, i.e. samples are dropped.
		 */
		do {
			timespec64_add_ns(&next, 1000 * sampling_us);
			delta = timespec64_sub(next, now);
			delay_us = div_s64(timespec64_to_ns(&delta), 1000);
		} while (delay_us <= 0);

		usleep_range(delay_us, (delay_us * 3) >> 1);

	} while (!kthread_should_stop());

	return 0;
}

static int ina2xx_buffer_enable(struct iio_dev *indio_dev)
{
	struct ina2xx_chip_info *chip = iio_priv(indio_dev);
	unsigned int sampling_us = SAMPLING_PERIOD(chip);
	struct task_struct *task;

	dev_dbg(&indio_dev->dev, "Enabling buffer w/ scan_mask %02x, freq = %d, avg =%u\n",
		(unsigned int)(*indio_dev->active_scan_mask),
		1000000 / sampling_us, chip->avg);

	dev_dbg(&indio_dev->dev, "Expected work period: %u us\n", sampling_us);
	dev_dbg(&indio_dev->dev, "Async readout mode: %d\n",
		chip->allow_async_readout);

	task = kthread_run(ina2xx_capture_thread, (void *)indio_dev,
			   "%s:%d-%uus", indio_dev->name,
			   iio_device_id(indio_dev),
			   sampling_us);
	if (IS_ERR(task))
		return PTR_ERR(task);

	chip->task = task;

	return 0;
}

static int ina2xx_buffer_disable(struct iio_dev *indio_dev)
{
	struct ina2xx_chip_info *chip = iio_priv(indio_dev);

	if (chip->task) {
		kthread_stop(chip->task);
		chip->task = NULL;
	}

	return 0;
}

static const struct iio_buffer_setup_ops ina2xx_setup_ops = {
	.postenable = &ina2xx_buffer_enable,
	.predisable = &ina2xx_buffer_disable,
};

static int ina2xx_debug_reg(struct iio_dev *indio_dev,
			    unsigned reg, unsigned writeval, unsigned *readval)
{
	struct ina2xx_chip_info *chip = iio_priv(indio_dev);

	if (!readval)
		return regmap_write(chip->regmap, reg, writeval);

	return regmap_read(chip->regmap, reg, readval);
}

/* Possible integration times for vshunt and vbus */
static IIO_CONST_ATTR_NAMED(ina219_integration_time_available,
			    integration_time_available,
			    "0.000084 0.000148 0.000276 0.000532 0.001060 0.002130 0.004260 0.008510 0.017020 0.034050 0.068100");

static IIO_CONST_ATTR_NAMED(ina226_integration_time_available,
			    integration_time_available,
			    "0.000140 0.000204 0.000332 0.000588 0.001100 0.002116 0.004156 0.008244");

static IIO_DEVICE_ATTR(in_allow_async_readout, S_IRUGO | S_IWUSR,
		       ina2xx_allow_async_readout_show,
		       ina2xx_allow_async_readout_store, 0);

static IIO_DEVICE_ATTR(in_shunt_resistor, S_IRUGO | S_IWUSR,
		       ina2xx_shunt_resistor_show,
		       ina2xx_shunt_resistor_store, 0);

static struct attribute *ina219_attributes[] = {
	&iio_dev_attr_in_allow_async_readout.dev_attr.attr,
	&iio_const_attr_ina219_integration_time_available.dev_attr.attr,
	&iio_dev_attr_in_shunt_resistor.dev_attr.attr,
	NULL,
};

static struct attribute *ina226_attributes[] = {
	&iio_dev_attr_in_allow_async_readout.dev_attr.attr,
	&iio_const_attr_ina226_integration_time_available.dev_attr.attr,
	&iio_dev_attr_in_shunt_resistor.dev_attr.attr,
	NULL,
};

static const struct attribute_group ina219_attribute_group = {
	.attrs = ina219_attributes,
};

static const struct attribute_group ina226_attribute_group = {
	.attrs = ina226_attributes,
};

static const struct iio_info ina219_info = {
	.attrs = &ina219_attribute_group,
	.read_raw = ina2xx_read_raw,
	.read_avail = ina2xx_read_avail,
	.write_raw = ina2xx_write_raw,
	.debugfs_reg_access = ina2xx_debug_reg,
};

static const struct iio_info ina226_info = {
	.attrs = &ina226_attribute_group,
	.read_raw = ina2xx_read_raw,
	.write_raw = ina2xx_write_raw,
	.debugfs_reg_access = ina2xx_debug_reg,
};

/* Initialize the configuration and calibration registers. */
static int ina2xx_init(struct ina2xx_chip_info *chip, unsigned int config)
{
	int ret = regmap_write(chip->regmap, INA2XX_CONFIG, config);
	if (ret)
		return ret;

	return ina2xx_set_calibration(chip);
}

static int ina2xx_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ina2xx_chip_info *chip;
	struct iio_dev *indio_dev;
	unsigned int val;
	enum ina2xx_ids type;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);

	/* This is only used for device removal purposes. */
	i2c_set_clientdata(client, indio_dev);

	chip->regmap = devm_regmap_init_i2c(client, &ina2xx_regmap_config);
	if (IS_ERR(chip->regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(chip->regmap);
	}

	if (client->dev.of_node)
		type = (uintptr_t)of_device_get_match_data(&client->dev);
	else
		type = id->driver_data;
	chip->config = &ina2xx_config[type];

	mutex_init(&chip->state_lock);

	if (of_property_read_u32(client->dev.of_node,
				 "shunt-resistor", &val) < 0) {
		struct ina2xx_platform_data *pdata =
		    dev_get_platdata(&client->dev);

		if (pdata)
			val = pdata->shunt_uohms;
		else
			val = INA2XX_RSHUNT_DEFAULT;
	}

	ret = set_shunt_resistor(chip, val);
	if (ret)
		return ret;

	/* Patch the current config register with default. */
	val = chip->config->config_default;

	if (id->driver_data == ina226) {
		ina226_set_average(chip, INA226_DEFAULT_AVG, &val);
		ina226_set_int_time_vbus(chip, INA226_DEFAULT_IT, &val);
		ina226_set_int_time_vshunt(chip, INA226_DEFAULT_IT, &val);
	} else {
		chip->avg = 1;
		ina219_set_int_time_vbus(chip, INA219_DEFAULT_IT, &val);
		ina219_set_int_time_vshunt(chip, INA219_DEFAULT_IT, &val);
		ina219_set_vbus_range_denom(chip, INA219_DEFAULT_BRNG, &val);
		ina219_set_vshunt_pga_gain(chip, INA219_DEFAULT_PGA, &val);
	}

	ret = ina2xx_init(chip, val);
	if (ret) {
		dev_err(&client->dev, "error configuring the device\n");
		return ret;
	}

	indio_dev->modes = INDIO_DIRECT_MODE;
	if (id->driver_data == ina226) {
		indio_dev->channels = ina226_channels;
		indio_dev->num_channels = ARRAY_SIZE(ina226_channels);
		indio_dev->info = &ina226_info;
	} else {
		indio_dev->channels = ina219_channels;
		indio_dev->num_channels = ARRAY_SIZE(ina219_channels);
		indio_dev->info = &ina219_info;
	}
	indio_dev->name = id->name;

	ret = devm_iio_kfifo_buffer_setup(&client->dev, indio_dev,
					  &ina2xx_setup_ops);
	if (ret)
		return ret;

	return iio_device_register(indio_dev);
}

static int ina2xx_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ina2xx_chip_info *chip = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	/* Powerdown */
	return regmap_update_bits(chip->regmap, INA2XX_CONFIG,
				  INA2XX_MODE_MASK, 0);
}

static const struct i2c_device_id ina2xx_id[] = {
	{"ina219", ina219},
	{"ina220", ina219},
	{"ina226", ina226},
	{"ina230", ina226},
	{"ina231", ina226},
	{}
};
MODULE_DEVICE_TABLE(i2c, ina2xx_id);

static const struct of_device_id ina2xx_of_match[] = {
	{
		.compatible = "ti,ina219",
		.data = (void *)ina219
	},
	{
		.compatible = "ti,ina220",
		.data = (void *)ina219
	},
	{
		.compatible = "ti,ina226",
		.data = (void *)ina226
	},
	{
		.compatible = "ti,ina230",
		.data = (void *)ina226
	},
	{
		.compatible = "ti,ina231",
		.data = (void *)ina226
	},
	{},
};
MODULE_DEVICE_TABLE(of, ina2xx_of_match);

static struct i2c_driver ina2xx_driver = {
	.driver = {
		   .name = KBUILD_MODNAME,
		   .of_match_table = ina2xx_of_match,
	},
	.probe = ina2xx_probe,
	.remove = ina2xx_remove,
	.id_table = ina2xx_id,
};
module_i2c_driver(ina2xx_driver);

MODULE_AUTHOR("Marc Titinger <marc.titinger@baylibre.com>");
MODULE_DESCRIPTION("Texas Instruments INA2XX ADC driver");
MODULE_LICENSE("GPL v2");
