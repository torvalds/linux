// SPDX-License-Identifier: GPL-2.0+
/*
 * Device driver for monitoring ambient light intensity in (lux) and proximity
 * detection (prox) within the TAOS TSL2X7X family of devices.
 *
 * Copyright (c) 2012, TAOS Corporation.
 * Copyright (c) 2017-2018 Brian Masney <masneyb@onstation.org>
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include "tsl2x7x.h"

/* Cal defs */
#define PROX_STAT_CAL			0
#define PROX_STAT_SAMP			1
#define MAX_SAMPLES_CAL			200

/* TSL2X7X Device ID */
#define TRITON_ID			0x00
#define SWORDFISH_ID			0x30
#define HALIBUT_ID			0x20

/* Lux calculation constants */
#define TSL2X7X_LUX_CALC_OVER_FLOW	65535

/*
 * TAOS Register definitions - Note: depending on device, some of these register
 * are not used and the register address is benign.
 */

/* 2X7X register offsets */
#define TSL2X7X_MAX_CONFIG_REG		16

/* Device Registers and Masks */
#define TSL2X7X_CNTRL			0x00
#define TSL2X7X_ALS_TIME		0X01
#define TSL2X7X_PRX_TIME		0x02
#define TSL2X7X_WAIT_TIME		0x03
#define TSL2X7X_ALS_MINTHRESHLO		0X04
#define TSL2X7X_ALS_MINTHRESHHI		0X05
#define TSL2X7X_ALS_MAXTHRESHLO		0X06
#define TSL2X7X_ALS_MAXTHRESHHI		0X07
#define TSL2X7X_PRX_MINTHRESHLO		0X08
#define TSL2X7X_PRX_MINTHRESHHI		0X09
#define TSL2X7X_PRX_MAXTHRESHLO		0X0A
#define TSL2X7X_PRX_MAXTHRESHHI		0X0B
#define TSL2X7X_PERSISTENCE		0x0C
#define TSL2X7X_ALS_PRX_CONFIG		0x0D
#define TSL2X7X_PRX_COUNT		0x0E
#define TSL2X7X_GAIN			0x0F
#define TSL2X7X_NOTUSED			0x10
#define TSL2X7X_REVID			0x11
#define TSL2X7X_CHIPID			0x12
#define TSL2X7X_STATUS			0x13
#define TSL2X7X_ALS_CHAN0LO		0x14
#define TSL2X7X_ALS_CHAN0HI		0x15
#define TSL2X7X_ALS_CHAN1LO		0x16
#define TSL2X7X_ALS_CHAN1HI		0x17
#define TSL2X7X_PRX_LO			0x18
#define TSL2X7X_PRX_HI			0x19

/* tsl2X7X cmd reg masks */
#define TSL2X7X_CMD_REG			0x80
#define TSL2X7X_CMD_SPL_FN		0x60
#define TSL2X7X_CMD_REPEAT_PROTO	0x00
#define TSL2X7X_CMD_AUTOINC_PROTO	0x20

#define TSL2X7X_CMD_PROX_INT_CLR	0X05
#define TSL2X7X_CMD_ALS_INT_CLR		0x06
#define TSL2X7X_CMD_PROXALS_INT_CLR	0X07

/* tsl2X7X cntrl reg masks */
#define TSL2X7X_CNTL_ADC_ENBL		0x02
#define TSL2X7X_CNTL_PWR_ON		0x01

/* tsl2X7X status reg masks */
#define TSL2X7X_STA_ADC_VALID		0x01
#define TSL2X7X_STA_PRX_VALID		0x02
#define TSL2X7X_STA_ADC_PRX_VALID	(TSL2X7X_STA_ADC_VALID | \
					 TSL2X7X_STA_PRX_VALID)
#define TSL2X7X_STA_ALS_INTR		0x10
#define TSL2X7X_STA_PRX_INTR		0x20

/* tsl2X7X cntrl reg masks */
#define TSL2X7X_CNTL_REG_CLEAR		0x00
#define TSL2X7X_CNTL_PROX_INT_ENBL	0X20
#define TSL2X7X_CNTL_ALS_INT_ENBL	0X10
#define TSL2X7X_CNTL_WAIT_TMR_ENBL	0X08
#define TSL2X7X_CNTL_PROX_DET_ENBL	0X04
#define TSL2X7X_CNTL_PWRON		0x01
#define TSL2X7X_CNTL_ALSPON_ENBL	0x03
#define TSL2X7X_CNTL_INTALSPON_ENBL	0x13
#define TSL2X7X_CNTL_PROXPON_ENBL	0x0F
#define TSL2X7X_CNTL_INTPROXPON_ENBL	0x2F

#define TSL2X7X_MIN_ITIME		3

/* TAOS txx2x7x Device family members */
enum {
	tsl2571,
	tsl2671,
	tmd2671,
	tsl2771,
	tmd2771,
	tsl2572,
	tsl2672,
	tmd2672,
	tsl2772,
	tmd2772
};

enum {
	TSL2X7X_CHIP_UNKNOWN = 0,
	TSL2X7X_CHIP_WORKING = 1,
	TSL2X7X_CHIP_SUSPENDED = 2
};

/* Per-device data */
struct tsl2x7x_als_info {
	u16 als_ch0;
	u16 als_ch1;
	u16 lux;
};

struct tsl2x7x_chip_info {
	int chan_table_elements;
	struct iio_chan_spec		channel[4];
	const struct iio_info		*info;
};

struct tsl2X7X_chip {
	kernel_ulong_t id;
	struct mutex prox_mutex;
	struct mutex als_mutex;
	struct i2c_client *client;
	u16 prox_data;
	struct tsl2x7x_als_info als_cur_info;
	struct tsl2x7x_settings settings;
	struct tsl2X7X_platform_data *pdata;
	int als_time_scale;
	int als_saturation;
	int tsl2x7x_chip_status;
	u8 tsl2x7x_config[TSL2X7X_MAX_CONFIG_REG];
	const struct tsl2x7x_chip_info	*chip_info;
	const struct iio_info *info;
	s64 event_timestamp;
	/*
	 * This structure is intentionally large to accommodate
	 * updates via sysfs.
	 * Sized to 9 = max 8 segments + 1 termination segment
	 */
	struct tsl2x7x_lux tsl2x7x_device_lux[TSL2X7X_MAX_LUX_TABLE_SIZE];
};

/* Different devices require different coefficents */
static const struct tsl2x7x_lux tsl2x71_lux_table[TSL2X7X_DEF_LUX_TABLE_SZ] = {
	{ 14461,   611,   1211 },
	{ 18540,   352,    623 },
	{     0,     0,      0 },
};

static const struct tsl2x7x_lux tmd2x71_lux_table[TSL2X7X_DEF_LUX_TABLE_SZ] = {
	{ 11635,   115,    256 },
	{ 15536,    87,    179 },
	{     0,     0,      0 },
};

static const struct tsl2x7x_lux tsl2x72_lux_table[TSL2X7X_DEF_LUX_TABLE_SZ] = {
	{ 14013,   466,   917 },
	{ 18222,   310,   552 },
	{     0,     0,     0 },
};

static const struct tsl2x7x_lux tmd2x72_lux_table[TSL2X7X_DEF_LUX_TABLE_SZ] = {
	{ 13218,   130,   262 },
	{ 17592,   92,    169 },
	{     0,     0,     0 },
};

static const struct tsl2x7x_lux *tsl2x7x_default_lux_table_group[] = {
	[tsl2571] =	tsl2x71_lux_table,
	[tsl2671] =	tsl2x71_lux_table,
	[tmd2671] =	tmd2x71_lux_table,
	[tsl2771] =	tsl2x71_lux_table,
	[tmd2771] =	tmd2x71_lux_table,
	[tsl2572] =	tsl2x72_lux_table,
	[tsl2672] =	tsl2x72_lux_table,
	[tmd2672] =	tmd2x72_lux_table,
	[tsl2772] =	tsl2x72_lux_table,
	[tmd2772] =	tmd2x72_lux_table,
};

static const struct tsl2x7x_settings tsl2x7x_default_settings = {
	.als_time = 255, /* 2.73 ms */
	.als_gain = 0,
	.prox_time = 255, /* 2.73 ms */
	.prox_gain = 0,
	.wait_time = 255,
	.als_prox_config = 0,
	.als_gain_trim = 1000,
	.als_cal_target = 150,
	.als_persistence = 1,
	.als_interrupt_en = false,
	.als_thresh_low = 200,
	.als_thresh_high = 256,
	.prox_persistence = 1,
	.prox_interrupt_en = false,
	.prox_thres_low  = 0,
	.prox_thres_high = 512,
	.prox_max_samples_cal = 30,
	.prox_pulse_count = 8,
	.prox_diode = TSL2X7X_DIODE1,
	.prox_power = TSL2X7X_100_mA
};

static const s16 tsl2x7x_als_gain[] = {
	1,
	8,
	16,
	120
};

static const s16 tsl2x7x_prox_gain[] = {
	1,
	2,
	4,
	8
};

/* Channel variations */
enum {
	ALS,
	PRX,
	ALSPRX,
	PRX2,
	ALSPRX2,
};

static const u8 device_channel_config[] = {
	ALS,
	PRX,
	PRX,
	ALSPRX,
	ALSPRX,
	ALS,
	PRX2,
	PRX2,
	ALSPRX2,
	ALSPRX2
};

static int tsl2x7x_read_status(struct tsl2X7X_chip *chip)
{
	int ret;

	ret = i2c_smbus_read_byte_data(chip->client,
				       TSL2X7X_CMD_REG | TSL2X7X_STATUS);
	if (ret < 0)
		dev_err(&chip->client->dev,
			"%s: failed to read STATUS register: %d\n", __func__,
			ret);

	return ret;
}

static int tsl2x7x_write_control_reg(struct tsl2X7X_chip *chip, u8 data)
{
	int ret;

	ret = i2c_smbus_write_byte_data(chip->client,
					TSL2X7X_CMD_REG | TSL2X7X_CNTRL, data);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to write to control register %x: %d\n",
			__func__, data, ret);
	}

	return ret;
}

static int tsl2x7x_read_autoinc_regs(struct tsl2X7X_chip *chip, int lower_reg,
				     int upper_reg)
{
	u8 buf[2];
	int ret;

	ret = i2c_smbus_write_byte(chip->client,
				   TSL2X7X_CMD_REG | TSL2X7X_CMD_AUTOINC_PROTO |
				   lower_reg);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to enable auto increment protocol: %d\n",
			__func__, ret);
		return ret;
	}

	ret = i2c_smbus_read_byte_data(chip->client,
				       TSL2X7X_CMD_REG | lower_reg);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to read from register %x: %d\n", __func__,
			lower_reg, ret);
		return ret;
	}
	buf[0] = ret;

	ret = i2c_smbus_read_byte_data(chip->client,
				       TSL2X7X_CMD_REG | upper_reg);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to read from register %x: %d\n", __func__,
			upper_reg, ret);
		return ret;
	}
	buf[1] = ret;

	ret = i2c_smbus_write_byte(chip->client,
				   TSL2X7X_CMD_REG | TSL2X7X_CMD_REPEAT_PROTO |
				   lower_reg);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to enable repeated byte protocol: %d\n",
			__func__, ret);
		return ret;
	}

	return le16_to_cpup((const __le16 *)&buf[0]);
}

/**
 * tsl2x7x_get_lux() - Reads and calculates current lux value.
 * @indio_dev:	pointer to IIO device
 *
 * The raw ch0 and ch1 values of the ambient light sensed in the last
 * integration cycle are read from the device. Time scale factor array values
 * are adjusted based on the integration time. The raw values are multiplied
 * by a scale factor, and device gain is obtained using gain index. Limit
 * checks are done next, then the ratio of a multiple of ch1 value, to the
 * ch0 value, is calculated. Array tsl2x7x_device_lux[] is then scanned to
 * find the first ratio value that is just above the ratio we just calculated.
 * The ch0 and ch1 multiplier constants in the array are then used along with
 * the time scale factor array values, to calculate the lux.
 */
static int tsl2x7x_get_lux(struct iio_dev *indio_dev)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	struct tsl2x7x_lux *p;
	u32 lux, ratio;
	u64 lux64;
	int ret;

	mutex_lock(&chip->als_mutex);

	if (chip->tsl2x7x_chip_status != TSL2X7X_CHIP_WORKING) {
		dev_err(&chip->client->dev, "%s: device is not enabled\n",
			__func__);
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = tsl2x7x_read_status(chip);
	if (ret < 0)
		goto out_unlock;

	if (!(ret & TSL2X7X_STA_ADC_VALID)) {
		dev_err(&chip->client->dev,
			"%s: data not valid yet\n", __func__);
		ret = chip->als_cur_info.lux; /* return LAST VALUE */
		goto out_unlock;
	}

	ret = tsl2x7x_read_autoinc_regs(chip, TSL2X7X_ALS_CHAN0LO,
					TSL2X7X_ALS_CHAN0HI);
	if (ret < 0)
		goto out_unlock;
	chip->als_cur_info.als_ch0 = ret;

	ret = tsl2x7x_read_autoinc_regs(chip, TSL2X7X_ALS_CHAN1LO,
					TSL2X7X_ALS_CHAN1HI);
	if (ret < 0)
		goto out_unlock;
	chip->als_cur_info.als_ch1 = ret;

	if (chip->als_cur_info.als_ch0 >= chip->als_saturation ||
	    chip->als_cur_info.als_ch1 >= chip->als_saturation) {
		lux = TSL2X7X_LUX_CALC_OVER_FLOW;
		goto return_max;
	}

	if (!chip->als_cur_info.als_ch0) {
		/* have no data, so return LAST VALUE */
		ret = chip->als_cur_info.lux;
		goto out_unlock;
	}

	/* calculate ratio */
	ratio = (chip->als_cur_info.als_ch1 << 15) / chip->als_cur_info.als_ch0;

	/* convert to unscaled lux using the pointer to the table */
	p = (struct tsl2x7x_lux *)chip->tsl2x7x_device_lux;
	while (p->ratio != 0 && p->ratio < ratio)
		p++;

	if (p->ratio == 0) {
		lux = 0;
	} else {
		lux = DIV_ROUND_UP(chip->als_cur_info.als_ch0 * p->ch0,
				   tsl2x7x_als_gain[chip->settings.als_gain]) -
		      DIV_ROUND_UP(chip->als_cur_info.als_ch1 * p->ch1,
				   tsl2x7x_als_gain[chip->settings.als_gain]);
	}

	/* adjust for active time scale */
	if (chip->als_time_scale == 0)
		lux = 0;
	else
		lux = (lux + (chip->als_time_scale >> 1)) /
			chip->als_time_scale;

	/*
	 * adjust for active gain scale. The tsl2x7x_device_lux tables have a
	 * factor of 256 built-in. User-specified gain provides a multiplier.
	 * Apply user-specified gain before shifting right to retain precision.
	 * Use 64 bits to avoid overflow on multiplication. Then go back to
	 * 32 bits before division to avoid using div_u64().
	 */

	lux64 = lux;
	lux64 = lux64 * chip->settings.als_gain_trim;
	lux64 >>= 8;
	lux = lux64;
	lux = (lux + 500) / 1000;

	if (lux > TSL2X7X_LUX_CALC_OVER_FLOW) /* check for overflow */
		lux = TSL2X7X_LUX_CALC_OVER_FLOW;

	/* Update the structure with the latest lux. */
return_max:
	chip->als_cur_info.lux = lux;
	ret = lux;

out_unlock:
	mutex_unlock(&chip->als_mutex);

	return ret;
}

/**
 * tsl2x7x_get_prox() - Reads proximity data registers and updates
 *                      chip->prox_data.
 *
 * @indio_dev:	pointer to IIO device
 */
static int tsl2x7x_get_prox(struct iio_dev *indio_dev)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int ret;

	mutex_lock(&chip->prox_mutex);

	ret = tsl2x7x_read_status(chip);
	if (ret < 0)
		goto prox_poll_err;

	switch (chip->id) {
	case tsl2571:
	case tsl2671:
	case tmd2671:
	case tsl2771:
	case tmd2771:
		if (!(ret & TSL2X7X_STA_ADC_VALID)) {
			ret = -EINVAL;
			goto prox_poll_err;
		}
		break;
	case tsl2572:
	case tsl2672:
	case tmd2672:
	case tsl2772:
	case tmd2772:
		if (!(ret & TSL2X7X_STA_PRX_VALID)) {
			ret = -EINVAL;
			goto prox_poll_err;
		}
		break;
	}

	ret = tsl2x7x_read_autoinc_regs(chip, TSL2X7X_PRX_LO, TSL2X7X_PRX_HI);
	if (ret < 0)
		goto prox_poll_err;
	chip->prox_data = ret;

prox_poll_err:
	mutex_unlock(&chip->prox_mutex);

	return ret;
}

/**
 * tsl2x7x_defaults() - Populates the device nominal operating parameters
 *                      with those provided by a 'platform' data struct or
 *                      with prefined defaults.
 *
 * @chip:               pointer to device structure.
 */
static void tsl2x7x_defaults(struct tsl2X7X_chip *chip)
{
	/* If Operational settings defined elsewhere.. */
	if (chip->pdata && chip->pdata->platform_default_settings)
		memcpy(&chip->settings, chip->pdata->platform_default_settings,
		       sizeof(tsl2x7x_default_settings));
	else
		memcpy(&chip->settings, &tsl2x7x_default_settings,
		       sizeof(tsl2x7x_default_settings));

	/* Load up the proper lux table. */
	if (chip->pdata && chip->pdata->platform_lux_table[0].ratio != 0)
		memcpy(chip->tsl2x7x_device_lux,
		       chip->pdata->platform_lux_table,
		       sizeof(chip->pdata->platform_lux_table));
	else
		memcpy(chip->tsl2x7x_device_lux,
		       tsl2x7x_default_lux_table_group[chip->id],
		       TSL2X7X_DEFAULT_TABLE_BYTES);
}

/**
 * tsl2x7x_als_calibrate() -	Obtain single reading and calculate
 *                              the als_gain_trim.
 *
 * @indio_dev:	pointer to IIO device
 */
static int tsl2x7x_als_calibrate(struct iio_dev *indio_dev)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int ret, lux_val;

	ret = i2c_smbus_read_byte_data(chip->client,
				       TSL2X7X_CMD_REG | TSL2X7X_CNTRL);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to read from the CNTRL register\n",
			__func__);
		return ret;
	}

	if ((ret & (TSL2X7X_CNTL_ADC_ENBL | TSL2X7X_CNTL_PWR_ON))
			!= (TSL2X7X_CNTL_ADC_ENBL | TSL2X7X_CNTL_PWR_ON)) {
		dev_err(&chip->client->dev,
			"%s: Device is not powered on and/or ADC is not enabled\n",
			__func__);
		return -EINVAL;
	} else if ((ret & TSL2X7X_STA_ADC_VALID) != TSL2X7X_STA_ADC_VALID) {
		dev_err(&chip->client->dev,
			"%s: The two ADC channels have not completed an integration cycle\n",
			__func__);
		return -ENODATA;
	}

	lux_val = tsl2x7x_get_lux(indio_dev);
	if (lux_val < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to get lux\n", __func__);
		return lux_val;
	}

	ret = (chip->settings.als_cal_target * chip->settings.als_gain_trim) /
			lux_val;
	if (ret < 250 || ret > 4000)
		return -ERANGE;

	chip->settings.als_gain_trim = ret;

	return ret;
}

static int tsl2x7x_chip_on(struct iio_dev *indio_dev)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int ret, i, als_count, als_time;
	u8 *dev_reg, reg_val;

	/* Non calculated parameters */
	chip->tsl2x7x_config[TSL2X7X_PRX_TIME] = chip->settings.prox_time;
	chip->tsl2x7x_config[TSL2X7X_WAIT_TIME] = chip->settings.wait_time;
	chip->tsl2x7x_config[TSL2X7X_ALS_PRX_CONFIG] =
		chip->settings.als_prox_config;

	chip->tsl2x7x_config[TSL2X7X_ALS_MINTHRESHLO] =
		(chip->settings.als_thresh_low) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_ALS_MINTHRESHHI] =
		(chip->settings.als_thresh_low >> 8) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_ALS_MAXTHRESHLO] =
		(chip->settings.als_thresh_high) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_ALS_MAXTHRESHHI] =
		(chip->settings.als_thresh_high >> 8) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_PERSISTENCE] =
		(chip->settings.prox_persistence & 0xFF) << 4 |
		(chip->settings.als_persistence & 0xFF);

	chip->tsl2x7x_config[TSL2X7X_PRX_COUNT] =
			chip->settings.prox_pulse_count;
	chip->tsl2x7x_config[TSL2X7X_PRX_MINTHRESHLO] =
			(chip->settings.prox_thres_low) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_PRX_MINTHRESHHI] =
			(chip->settings.prox_thres_low >> 8) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_PRX_MAXTHRESHLO] =
			(chip->settings.prox_thres_high) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_PRX_MAXTHRESHHI] =
			(chip->settings.prox_thres_high >> 8) & 0xFF;

	/* and make sure we're not already on */
	if (chip->tsl2x7x_chip_status == TSL2X7X_CHIP_WORKING) {
		/* if forcing a register update - turn off, then on */
		dev_info(&chip->client->dev, "device is already enabled\n");
		return -EINVAL;
	}

	/* determine als integration register */
	als_count = (chip->settings.als_time * 100 + 135) / 270;
	if (!als_count)
		als_count = 1; /* ensure at least one cycle */

	/* convert back to time (encompasses overrides) */
	als_time = (als_count * 27 + 5) / 10;
	chip->tsl2x7x_config[TSL2X7X_ALS_TIME] = 256 - als_count;

	/* Set the gain based on tsl2x7x_settings struct */
	chip->tsl2x7x_config[TSL2X7X_GAIN] =
		(chip->settings.als_gain & 0xFF) |
		((chip->settings.prox_gain & 0xFF) << 2) |
		(chip->settings.prox_diode << 4) |
		(chip->settings.prox_power << 6);

	/* set chip struct re scaling and saturation */
	chip->als_saturation = als_count * 922; /* 90% of full scale */
	chip->als_time_scale = (als_time + 25) / 50;

	/*
	 * TSL2X7X Specific power-on / adc enable sequence
	 * Power on the device 1st.
	 */
	ret = tsl2x7x_write_control_reg(chip, TSL2X7X_CNTL_PWR_ON);
	if (ret < 0)
		return ret;

	/*
	 * Use the following shadow copy for our delay before enabling ADC.
	 * Write all the registers.
	 */
	for (i = 0, dev_reg = chip->tsl2x7x_config;
			i < TSL2X7X_MAX_CONFIG_REG; i++) {
		int reg = TSL2X7X_CMD_REG + i;

		ret = i2c_smbus_write_byte_data(chip->client, reg,
						*dev_reg++);
		if (ret < 0) {
			dev_err(&chip->client->dev,
				"%s: failed to write to register %x: %d\n",
				__func__, reg, ret);
			return ret;
		}
	}

	/* Power-on settling time */
	usleep_range(3000, 3500);

	reg_val = TSL2X7X_CNTL_PWR_ON | TSL2X7X_CNTL_ADC_ENBL |
		  TSL2X7X_CNTL_PROX_DET_ENBL;
	if (chip->settings.als_interrupt_en)
		reg_val |= TSL2X7X_CNTL_ALS_INT_ENBL;
	if (chip->settings.prox_interrupt_en)
		reg_val |= TSL2X7X_CNTL_PROX_INT_ENBL;

	ret = tsl2x7x_write_control_reg(chip, reg_val);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte(chip->client,
				   TSL2X7X_CMD_REG | TSL2X7X_CMD_SPL_FN |
				   TSL2X7X_CMD_PROXALS_INT_CLR);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed to clear interrupt status: %d\n",
			__func__, ret);
		return ret;
	}

	chip->tsl2x7x_chip_status = TSL2X7X_CHIP_WORKING;

	return ret;
}

static int tsl2x7x_chip_off(struct iio_dev *indio_dev)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);

	/* turn device off */
	chip->tsl2x7x_chip_status = TSL2X7X_CHIP_SUSPENDED;
	return tsl2x7x_write_control_reg(chip, 0x00);
}

/**
 * tsl2x7x_invoke_change - power cycle the device to implement the user
 *                         parameters
 * @indio_dev:	pointer to IIO device
 *
 * Obtain and lock both ALS and PROX resources, determine and save device state
 * (On/Off), cycle device to implement updated parameter, put device back into
 * proper state, and unlock resource.
 */
static int tsl2x7x_invoke_change(struct iio_dev *indio_dev)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int device_status = chip->tsl2x7x_chip_status;
	int ret;

	mutex_lock(&chip->als_mutex);
	mutex_lock(&chip->prox_mutex);

	if (device_status == TSL2X7X_CHIP_WORKING) {
		ret = tsl2x7x_chip_off(indio_dev);
		if (ret < 0)
			goto unlock;
	}

	ret = tsl2x7x_chip_on(indio_dev);

unlock:
	mutex_unlock(&chip->prox_mutex);
	mutex_unlock(&chip->als_mutex);

	return ret;
}

static int tsl2x7x_prox_cal(struct iio_dev *indio_dev)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int prox_history[MAX_SAMPLES_CAL + 1];
	int i, ret, mean, max, sample_sum;

	if (chip->settings.prox_max_samples_cal < 1 ||
	    chip->settings.prox_max_samples_cal > MAX_SAMPLES_CAL)
		return -EINVAL;

	for (i = 0; i < chip->settings.prox_max_samples_cal; i++) {
		usleep_range(15000, 17500);
		ret = tsl2x7x_get_prox(indio_dev);
		if (ret < 0)
			return ret;

		prox_history[i] = chip->prox_data;
	}

	sample_sum = 0;
	max = INT_MIN;
	for (i = 0; i < chip->settings.prox_max_samples_cal; i++) {
		sample_sum += prox_history[i];
		max = max(max, prox_history[i]);
	}
	mean = sample_sum / chip->settings.prox_max_samples_cal;

	chip->settings.prox_thres_high = (max << 1) - mean;

	return tsl2x7x_invoke_change(indio_dev);
}

static ssize_t
in_illuminance0_calibscale_available_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct tsl2X7X_chip *chip = iio_priv(dev_to_iio_dev(dev));

	switch (chip->id) {
	case tsl2571:
	case tsl2671:
	case tmd2671:
	case tsl2771:
	case tmd2771:
		return snprintf(buf, PAGE_SIZE, "%s\n", "1 8 16 128");
	}

	return snprintf(buf, PAGE_SIZE, "%s\n", "1 8 16 120");
}

static IIO_CONST_ATTR(in_proximity0_calibscale_available, "1 2 4 8");

static IIO_CONST_ATTR(in_intensity0_integration_time_available,
		".00272 - .696");

static ssize_t in_illuminance0_target_input_show(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct tsl2X7X_chip *chip = iio_priv(dev_to_iio_dev(dev));

	return snprintf(buf, PAGE_SIZE, "%d\n", chip->settings.als_cal_target);
}

static ssize_t in_illuminance0_target_input_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	unsigned long value;
	int ret;

	if (kstrtoul(buf, 0, &value))
		return -EINVAL;

	if (value)
		chip->settings.als_cal_target = value;

	ret = tsl2x7x_invoke_change(indio_dev);
	if (ret < 0)
		return ret;

	return len;
}

static ssize_t in_illuminance0_calibrate_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	bool value;
	int ret;

	if (strtobool(buf, &value))
		return -EINVAL;

	if (value) {
		ret = tsl2x7x_als_calibrate(indio_dev);
		if (ret < 0)
			return ret;
	}

	ret = tsl2x7x_invoke_change(indio_dev);
	if (ret < 0)
		return ret;

	return len;
}

static ssize_t in_illuminance0_lux_table_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct tsl2X7X_chip *chip = iio_priv(dev_to_iio_dev(dev));
	int i = 0;
	int offset = 0;

	while (i < TSL2X7X_MAX_LUX_TABLE_SIZE) {
		offset += snprintf(buf + offset, PAGE_SIZE, "%u,%u,%u,",
			chip->tsl2x7x_device_lux[i].ratio,
			chip->tsl2x7x_device_lux[i].ch0,
			chip->tsl2x7x_device_lux[i].ch1);
		if (chip->tsl2x7x_device_lux[i].ratio == 0) {
			/*
			 * We just printed the first "0" entry.
			 * Now get rid of the extra "," and break.
			 */
			offset--;
			break;
		}
		i++;
	}

	offset += snprintf(buf + offset, PAGE_SIZE, "\n");
	return offset;
}

static ssize_t in_illuminance0_lux_table_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int value[ARRAY_SIZE(chip->tsl2x7x_device_lux) * 3 + 1];
	int n, ret;

	get_options(buf, ARRAY_SIZE(value), value);

	/*
	 * We now have an array of ints starting at value[1], and
	 * enumerated by value[0].
	 * We expect each group of three ints is one table entry,
	 * and the last table entry is all 0.
	 */
	n = value[0];
	if ((n % 3) || n < 6 ||
	    n > ((ARRAY_SIZE(chip->tsl2x7x_device_lux) - 1) * 3))
		return -EINVAL;

	if ((value[(n - 2)] | value[(n - 1)] | value[n]) != 0)
		return -EINVAL;

	if (chip->tsl2x7x_chip_status == TSL2X7X_CHIP_WORKING) {
		ret = tsl2x7x_chip_off(indio_dev);
		if (ret < 0)
			return ret;
	}

	/* Zero out the table */
	memset(chip->tsl2x7x_device_lux, 0, sizeof(chip->tsl2x7x_device_lux));
	memcpy(chip->tsl2x7x_device_lux, &value[1], (value[0] * 4));

	ret = tsl2x7x_invoke_change(indio_dev);
	if (ret < 0)
		return ret;

	return len;
}

static ssize_t in_proximity0_calibrate_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	bool value;
	int ret;

	if (strtobool(buf, &value))
		return -EINVAL;

	if (value) {
		ret = tsl2x7x_prox_cal(indio_dev);
		if (ret < 0)
			return ret;
	}

	ret = tsl2x7x_invoke_change(indio_dev);
	if (ret < 0)
		return ret;

	return len;
}

static int tsl2x7x_read_interrupt_config(struct iio_dev *indio_dev,
					 const struct iio_chan_spec *chan,
					 enum iio_event_type type,
					 enum iio_event_direction dir)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);

	if (chan->type == IIO_INTENSITY)
		return chip->settings.als_interrupt_en;
	else
		return chip->settings.prox_interrupt_en;
}

static int tsl2x7x_write_interrupt_config(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir,
					  int val)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);

	if (chan->type == IIO_INTENSITY)
		chip->settings.als_interrupt_en = val ? true : false;
	else
		chip->settings.prox_interrupt_en = val ? true : false;

	return tsl2x7x_invoke_change(indio_dev);
}

static int tsl2x7x_write_event_value(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     enum iio_event_info info,
				     int val, int val2)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int ret = -EINVAL, y, z, filter_delay;
	u8 time;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (chan->type == IIO_INTENSITY) {
			switch (dir) {
			case IIO_EV_DIR_RISING:
				chip->settings.als_thresh_high = val;
				ret = 0;
				break;
			case IIO_EV_DIR_FALLING:
				chip->settings.als_thresh_low = val;
				ret = 0;
				break;
			default:
				break;
			}
		} else {
			switch (dir) {
			case IIO_EV_DIR_RISING:
				chip->settings.prox_thres_high = val;
				ret = 0;
				break;
			case IIO_EV_DIR_FALLING:
				chip->settings.prox_thres_low = val;
				ret = 0;
				break;
			default:
				break;
			}
		}
		break;
	case IIO_EV_INFO_PERIOD:
		if (chan->type == IIO_INTENSITY)
			time = chip->settings.als_time;
		else
			time = chip->settings.prox_time;

		y = (TSL2X7X_MAX_TIMER_CNT - time) + 1;
		z = y * TSL2X7X_MIN_ITIME;

		filter_delay = DIV_ROUND_UP((val * 1000) + val2, z);

		if (chan->type == IIO_INTENSITY)
			chip->settings.als_persistence = filter_delay;
		else
			chip->settings.prox_persistence = filter_delay;
		ret = 0;
		break;
	default:
		break;
	}

	if (ret < 0)
		return ret;

	return tsl2x7x_invoke_change(indio_dev);
}

static int tsl2x7x_read_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int *val, int *val2)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int ret = -EINVAL, filter_delay, mult;
	u8 time;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (chan->type == IIO_INTENSITY) {
			switch (dir) {
			case IIO_EV_DIR_RISING:
				*val = chip->settings.als_thresh_high;
				ret = IIO_VAL_INT;
				break;
			case IIO_EV_DIR_FALLING:
				*val = chip->settings.als_thresh_low;
				ret = IIO_VAL_INT;
				break;
			default:
				break;
			}
		} else {
			switch (dir) {
			case IIO_EV_DIR_RISING:
				*val = chip->settings.prox_thres_high;
				ret = IIO_VAL_INT;
				break;
			case IIO_EV_DIR_FALLING:
				*val = chip->settings.prox_thres_low;
				ret = IIO_VAL_INT;
				break;
			default:
				break;
			}
		}
		break;
	case IIO_EV_INFO_PERIOD:
		if (chan->type == IIO_INTENSITY) {
			time = chip->settings.als_time;
			mult = chip->settings.als_persistence;
		} else {
			time = chip->settings.prox_time;
			mult = chip->settings.prox_persistence;
		}

		/* Determine integration time */
		*val = (TSL2X7X_MAX_TIMER_CNT - time) + 1;
		*val2 = *val * TSL2X7X_MIN_ITIME;
		filter_delay = *val2 * mult;
		*val = filter_delay / 1000;
		*val2 = filter_delay % 1000;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	default:
		break;
	}

	return ret;
}

static int tsl2x7x_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val,
			    int *val2,
			    long mask)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int ret = -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_LIGHT:
			tsl2x7x_get_lux(indio_dev);
			*val = chip->als_cur_info.lux;
			ret = IIO_VAL_INT;
			break;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_INTENSITY:
			tsl2x7x_get_lux(indio_dev);
			if (chan->channel == 0)
				*val = chip->als_cur_info.als_ch0;
			else
				*val = chip->als_cur_info.als_ch1;
			ret = IIO_VAL_INT;
			break;
		case IIO_PROXIMITY:
			tsl2x7x_get_prox(indio_dev);
			*val = chip->prox_data;
			ret = IIO_VAL_INT;
			break;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		if (chan->type == IIO_LIGHT)
			*val = tsl2x7x_als_gain[chip->settings.als_gain];
		else
			*val = tsl2x7x_prox_gain[chip->settings.prox_gain];
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		*val = chip->settings.als_gain_trim;
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_INT_TIME:
		*val = (TSL2X7X_MAX_TIMER_CNT - chip->settings.als_time) + 1;
		*val2 = ((*val * TSL2X7X_MIN_ITIME) % 1000) / 1000;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int tsl2x7x_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val,
			     int val2,
			     long mask)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		if (chan->type == IIO_INTENSITY) {
			switch (val) {
			case 1:
				chip->settings.als_gain = 0;
				break;
			case 8:
				chip->settings.als_gain = 1;
				break;
			case 16:
				chip->settings.als_gain = 2;
				break;
			case 120:
				switch (chip->id) {
				case tsl2572:
				case tsl2672:
				case tmd2672:
				case tsl2772:
				case tmd2772:
					return -EINVAL;
				}
				chip->settings.als_gain = 3;
				break;
			case 128:
				switch (chip->id) {
				case tsl2571:
				case tsl2671:
				case tmd2671:
				case tsl2771:
				case tmd2771:
					return -EINVAL;
				}
				chip->settings.als_gain = 3;
				break;
			default:
				return -EINVAL;
			}
		} else {
			switch (val) {
			case 1:
				chip->settings.prox_gain = 0;
				break;
			case 2:
				chip->settings.prox_gain = 1;
				break;
			case 4:
				chip->settings.prox_gain = 2;
				break;
			case 8:
				chip->settings.prox_gain = 3;
				break;
			default:
				return -EINVAL;
			}
		}
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		chip->settings.als_gain_trim = val;
		break;
	case IIO_CHAN_INFO_INT_TIME:
		chip->settings.als_time =
			TSL2X7X_MAX_TIMER_CNT - (val2 / TSL2X7X_MIN_ITIME);
		break;
	default:
		return -EINVAL;
	}

	return tsl2x7x_invoke_change(indio_dev);
}

static DEVICE_ATTR_RO(in_illuminance0_calibscale_available);

static DEVICE_ATTR_RW(in_illuminance0_target_input);

static DEVICE_ATTR_WO(in_illuminance0_calibrate);

static DEVICE_ATTR_WO(in_proximity0_calibrate);

static DEVICE_ATTR_RW(in_illuminance0_lux_table);

/* Use the default register values to identify the Taos device */
static int tsl2x7x_device_id_verif(int id, int target)
{
	switch (target) {
	case tsl2571:
	case tsl2671:
	case tsl2771:
		return (id & 0xf0) == TRITON_ID;
	case tmd2671:
	case tmd2771:
		return (id & 0xf0) == HALIBUT_ID;
	case tsl2572:
	case tsl2672:
	case tmd2672:
	case tsl2772:
	case tmd2772:
		return (id & 0xf0) == SWORDFISH_ID;
	}

	return -EINVAL;
}

static irqreturn_t tsl2x7x_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	s64 timestamp = iio_get_time_ns(indio_dev);
	int ret;

	ret = tsl2x7x_read_status(chip);
	if (ret < 0)
		return IRQ_HANDLED;

	/* What type of interrupt do we need to process */
	if (ret & TSL2X7X_STA_PRX_INTR) {
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
						    timestamp);
	}

	if (ret & TSL2X7X_STA_ALS_INTR) {
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_LIGHT,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
			       timestamp);
	}

	ret = i2c_smbus_write_byte(chip->client,
				   TSL2X7X_CMD_REG | TSL2X7X_CMD_SPL_FN |
				   TSL2X7X_CMD_PROXALS_INT_CLR);
	if (ret < 0)
		dev_err(&chip->client->dev,
			"%s: failed to clear interrupt status: %d\n",
			__func__, ret);

	return IRQ_HANDLED;
}

static struct attribute *tsl2x7x_ALS_device_attrs[] = {
	&dev_attr_in_illuminance0_calibscale_available.attr,
	&iio_const_attr_in_intensity0_integration_time_available
		.dev_attr.attr,
	&dev_attr_in_illuminance0_target_input.attr,
	&dev_attr_in_illuminance0_calibrate.attr,
	&dev_attr_in_illuminance0_lux_table.attr,
	NULL
};

static struct attribute *tsl2x7x_PRX_device_attrs[] = {
	&dev_attr_in_proximity0_calibrate.attr,
	NULL
};

static struct attribute *tsl2x7x_ALSPRX_device_attrs[] = {
	&dev_attr_in_illuminance0_calibscale_available.attr,
	&iio_const_attr_in_intensity0_integration_time_available
		.dev_attr.attr,
	&dev_attr_in_illuminance0_target_input.attr,
	&dev_attr_in_illuminance0_calibrate.attr,
	&dev_attr_in_illuminance0_lux_table.attr,
	NULL
};

static struct attribute *tsl2x7x_PRX2_device_attrs[] = {
	&dev_attr_in_proximity0_calibrate.attr,
	&iio_const_attr_in_proximity0_calibscale_available.dev_attr.attr,
	NULL
};

static struct attribute *tsl2x7x_ALSPRX2_device_attrs[] = {
	&dev_attr_in_illuminance0_calibscale_available.attr,
	&iio_const_attr_in_intensity0_integration_time_available
		.dev_attr.attr,
	&dev_attr_in_illuminance0_target_input.attr,
	&dev_attr_in_illuminance0_calibrate.attr,
	&dev_attr_in_illuminance0_lux_table.attr,
	&dev_attr_in_proximity0_calibrate.attr,
	&iio_const_attr_in_proximity0_calibscale_available.dev_attr.attr,
	NULL
};

static const struct attribute_group tsl2X7X_device_attr_group_tbl[] = {
	[ALS] = {
		.attrs = tsl2x7x_ALS_device_attrs,
	},
	[PRX] = {
		.attrs = tsl2x7x_PRX_device_attrs,
	},
	[ALSPRX] = {
		.attrs = tsl2x7x_ALSPRX_device_attrs,
	},
	[PRX2] = {
		.attrs = tsl2x7x_PRX2_device_attrs,
	},
	[ALSPRX2] = {
		.attrs = tsl2x7x_ALSPRX2_device_attrs,
	},
};

static const struct iio_info tsl2X7X_device_info[] = {
	[ALS] = {
		.attrs = &tsl2X7X_device_attr_group_tbl[ALS],
		.read_raw = &tsl2x7x_read_raw,
		.write_raw = &tsl2x7x_write_raw,
		.read_event_value = &tsl2x7x_read_event_value,
		.write_event_value = &tsl2x7x_write_event_value,
		.read_event_config = &tsl2x7x_read_interrupt_config,
		.write_event_config = &tsl2x7x_write_interrupt_config,
	},
	[PRX] = {
		.attrs = &tsl2X7X_device_attr_group_tbl[PRX],
		.read_raw = &tsl2x7x_read_raw,
		.write_raw = &tsl2x7x_write_raw,
		.read_event_value = &tsl2x7x_read_event_value,
		.write_event_value = &tsl2x7x_write_event_value,
		.read_event_config = &tsl2x7x_read_interrupt_config,
		.write_event_config = &tsl2x7x_write_interrupt_config,
	},
	[ALSPRX] = {
		.attrs = &tsl2X7X_device_attr_group_tbl[ALSPRX],
		.read_raw = &tsl2x7x_read_raw,
		.write_raw = &tsl2x7x_write_raw,
		.read_event_value = &tsl2x7x_read_event_value,
		.write_event_value = &tsl2x7x_write_event_value,
		.read_event_config = &tsl2x7x_read_interrupt_config,
		.write_event_config = &tsl2x7x_write_interrupt_config,
	},
	[PRX2] = {
		.attrs = &tsl2X7X_device_attr_group_tbl[PRX2],
		.read_raw = &tsl2x7x_read_raw,
		.write_raw = &tsl2x7x_write_raw,
		.read_event_value = &tsl2x7x_read_event_value,
		.write_event_value = &tsl2x7x_write_event_value,
		.read_event_config = &tsl2x7x_read_interrupt_config,
		.write_event_config = &tsl2x7x_write_interrupt_config,
	},
	[ALSPRX2] = {
		.attrs = &tsl2X7X_device_attr_group_tbl[ALSPRX2],
		.read_raw = &tsl2x7x_read_raw,
		.write_raw = &tsl2x7x_write_raw,
		.read_event_value = &tsl2x7x_read_event_value,
		.write_event_value = &tsl2x7x_write_event_value,
		.read_event_config = &tsl2x7x_read_interrupt_config,
		.write_event_config = &tsl2x7x_write_interrupt_config,
	},
};

static const struct iio_event_spec tsl2x7x_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_PERIOD) |
			BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct tsl2x7x_chip_info tsl2x7x_chip_info_tbl[] = {
	[ALS] = {
		.channel = {
			{
			.type = IIO_LIGHT,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE) |
				BIT(IIO_CHAN_INFO_CALIBBIAS),
			.event_spec = tsl2x7x_events,
			.num_event_specs = ARRAY_SIZE(tsl2x7x_events),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 1,
			},
		},
	.chan_table_elements = 3,
	.info = &tsl2X7X_device_info[ALS],
	},
	[PRX] = {
		.channel = {
			{
			.type = IIO_PROXIMITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			.event_spec = tsl2x7x_events,
			.num_event_specs = ARRAY_SIZE(tsl2x7x_events),
			},
		},
	.chan_table_elements = 1,
	.info = &tsl2X7X_device_info[PRX],
	},
	[ALSPRX] = {
		.channel = {
			{
			.type = IIO_LIGHT,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE) |
				BIT(IIO_CHAN_INFO_CALIBBIAS),
			.event_spec = tsl2x7x_events,
			.num_event_specs = ARRAY_SIZE(tsl2x7x_events),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 1,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			}, {
			.type = IIO_PROXIMITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			.event_spec = tsl2x7x_events,
			.num_event_specs = ARRAY_SIZE(tsl2x7x_events),
			},
		},
	.chan_table_elements = 4,
	.info = &tsl2X7X_device_info[ALSPRX],
	},
	[PRX2] = {
		.channel = {
			{
			.type = IIO_PROXIMITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			.event_spec = tsl2x7x_events,
			.num_event_specs = ARRAY_SIZE(tsl2x7x_events),
			},
		},
	.chan_table_elements = 1,
	.info = &tsl2X7X_device_info[PRX2],
	},
	[ALSPRX2] = {
		.channel = {
			{
			.type = IIO_LIGHT,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_INT_TIME) |
				BIT(IIO_CHAN_INFO_CALIBSCALE) |
				BIT(IIO_CHAN_INFO_CALIBBIAS),
			.event_spec = tsl2x7x_events,
			.num_event_specs = ARRAY_SIZE(tsl2x7x_events),
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 1,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			}, {
			.type = IIO_PROXIMITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_CALIBSCALE),
			.event_spec = tsl2x7x_events,
			.num_event_specs = ARRAY_SIZE(tsl2x7x_events),
			},
		},
	.chan_table_elements = 4,
	.info = &tsl2X7X_device_info[ALSPRX2],
	},
};

static int tsl2x7x_probe(struct i2c_client *clientp,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct tsl2X7X_chip *chip;
	int ret;

	indio_dev = devm_iio_device_alloc(&clientp->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);
	chip->client = clientp;
	i2c_set_clientdata(clientp, indio_dev);

	ret = i2c_smbus_read_byte_data(chip->client,
				       TSL2X7X_CMD_REG | TSL2X7X_CHIPID);
	if (ret < 0)
		return ret;

	if (tsl2x7x_device_id_verif(ret, id->driver_data) <= 0) {
		dev_info(&chip->client->dev,
			 "%s: i2c device found does not match expected id\n",
				__func__);
		return -EINVAL;
	}

	ret = i2c_smbus_write_byte(clientp, TSL2X7X_CMD_REG | TSL2X7X_CNTRL);
	if (ret < 0) {
		dev_err(&clientp->dev,
			"%s: Failed to write to CMD register: %d\n",
			__func__, ret);
		return ret;
	}

	mutex_init(&chip->als_mutex);
	mutex_init(&chip->prox_mutex);

	chip->tsl2x7x_chip_status = TSL2X7X_CHIP_UNKNOWN;
	chip->pdata = dev_get_platdata(&clientp->dev);
	chip->id = id->driver_data;
	chip->chip_info =
		&tsl2x7x_chip_info_tbl[device_channel_config[id->driver_data]];

	indio_dev->info = chip->chip_info->info;
	indio_dev->dev.parent = &clientp->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = chip->client->name;
	indio_dev->channels = chip->chip_info->channel;
	indio_dev->num_channels = chip->chip_info->chan_table_elements;

	if (clientp->irq) {
		ret = devm_request_threaded_irq(&clientp->dev, clientp->irq,
						NULL,
						&tsl2x7x_event_handler,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"TSL2X7X_event",
						indio_dev);
		if (ret) {
			dev_err(&clientp->dev,
				"%s: irq request failed\n", __func__);
			return ret;
		}
	}

	tsl2x7x_defaults(chip);
	tsl2x7x_chip_on(indio_dev);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&clientp->dev,
			"%s: iio registration failed\n", __func__);
		return ret;
	}

	return 0;
}

static int tsl2x7x_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);

	return tsl2x7x_chip_off(indio_dev);
}

static int tsl2x7x_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);

	return tsl2x7x_chip_on(indio_dev);
}

static int tsl2x7x_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	tsl2x7x_chip_off(indio_dev);

	iio_device_unregister(indio_dev);

	return 0;
}

static const struct i2c_device_id tsl2x7x_idtable[] = {
	{ "tsl2571", tsl2571 },
	{ "tsl2671", tsl2671 },
	{ "tmd2671", tmd2671 },
	{ "tsl2771", tsl2771 },
	{ "tmd2771", tmd2771 },
	{ "tsl2572", tsl2572 },
	{ "tsl2672", tsl2672 },
	{ "tmd2672", tmd2672 },
	{ "tsl2772", tsl2772 },
	{ "tmd2772", tmd2772 },
	{}
};

MODULE_DEVICE_TABLE(i2c, tsl2x7x_idtable);

static const struct of_device_id tsl2x7x_of_match[] = {
	{ .compatible = "amstaos,tsl2571" },
	{ .compatible = "amstaos,tsl2671" },
	{ .compatible = "amstaos,tmd2671" },
	{ .compatible = "amstaos,tsl2771" },
	{ .compatible = "amstaos,tmd2771" },
	{ .compatible = "amstaos,tsl2572" },
	{ .compatible = "amstaos,tsl2672" },
	{ .compatible = "amstaos,tmd2672" },
	{ .compatible = "amstaos,tsl2772" },
	{ .compatible = "amstaos,tmd2772" },
	{}
};
MODULE_DEVICE_TABLE(of, tsl2x7x_of_match);

static const struct dev_pm_ops tsl2x7x_pm_ops = {
	.suspend = tsl2x7x_suspend,
	.resume  = tsl2x7x_resume,
};

static struct i2c_driver tsl2x7x_driver = {
	.driver = {
		.name = "tsl2x7x",
		.of_match_table = tsl2x7x_of_match,
		.pm = &tsl2x7x_pm_ops,
	},
	.id_table = tsl2x7x_idtable,
	.probe = tsl2x7x_probe,
	.remove = tsl2x7x_remove,
};

module_i2c_driver(tsl2x7x_driver);

MODULE_AUTHOR("J. August Brenner <Jon.Brenner@ams.com>");
MODULE_AUTHOR("Brian Masney <masneyb@onstation.org>");
MODULE_DESCRIPTION("TAOS tsl2x7x ambient and proximity light sensor driver");
MODULE_LICENSE("GPL");
