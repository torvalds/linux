/*
 * Device driver for monitoring ambient light intensity in (lux)
 * and proximity detection (prox) within the TAOS TSL2X7X family of devices.
 *
 * Copyright (c) 2012, TAOS Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA        02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include "tsl2x7x.h"

/* Cal defs*/
#define PROX_STAT_CAL        0
#define PROX_STAT_SAMP       1
#define MAX_SAMPLES_CAL      200

/* TSL2X7X Device ID */
#define TRITON_ID    0x00
#define SWORDFISH_ID 0x30
#define HALIBUT_ID   0x20

/* Lux calculation constants */
#define TSL2X7X_LUX_CALC_OVER_FLOW     65535

/* TAOS Register definitions - note:
 * depending on device, some of these register are not used and the
 * register address is benign.
 */
/* 2X7X register offsets */
#define TSL2X7X_MAX_CONFIG_REG         16

/* Device Registers and Masks */
#define TSL2X7X_CNTRL                  0x00
#define TSL2X7X_ALS_TIME               0X01
#define TSL2X7X_PRX_TIME               0x02
#define TSL2X7X_WAIT_TIME              0x03
#define TSL2X7X_ALS_MINTHRESHLO        0X04
#define TSL2X7X_ALS_MINTHRESHHI        0X05
#define TSL2X7X_ALS_MAXTHRESHLO        0X06
#define TSL2X7X_ALS_MAXTHRESHHI        0X07
#define TSL2X7X_PRX_MINTHRESHLO        0X08
#define TSL2X7X_PRX_MINTHRESHHI        0X09
#define TSL2X7X_PRX_MAXTHRESHLO        0X0A
#define TSL2X7X_PRX_MAXTHRESHHI        0X0B
#define TSL2X7X_PERSISTENCE            0x0C
#define TSL2X7X_PRX_CONFIG             0x0D
#define TSL2X7X_PRX_COUNT              0x0E
#define TSL2X7X_GAIN                   0x0F
#define TSL2X7X_NOTUSED                0x10
#define TSL2X7X_REVID                  0x11
#define TSL2X7X_CHIPID                 0x12
#define TSL2X7X_STATUS                 0x13
#define TSL2X7X_ALS_CHAN0LO            0x14
#define TSL2X7X_ALS_CHAN0HI            0x15
#define TSL2X7X_ALS_CHAN1LO            0x16
#define TSL2X7X_ALS_CHAN1HI            0x17
#define TSL2X7X_PRX_LO                 0x18
#define TSL2X7X_PRX_HI                 0x19

/* tsl2X7X cmd reg masks */
#define TSL2X7X_CMD_REG                0x80
#define TSL2X7X_CMD_SPL_FN             0x60

#define TSL2X7X_CMD_PROX_INT_CLR       0X05
#define TSL2X7X_CMD_ALS_INT_CLR        0x06
#define TSL2X7X_CMD_PROXALS_INT_CLR    0X07

/* tsl2X7X cntrl reg masks */
#define TSL2X7X_CNTL_ADC_ENBL          0x02
#define TSL2X7X_CNTL_PWR_ON            0x01

/* tsl2X7X status reg masks */
#define TSL2X7X_STA_ADC_VALID          0x01
#define TSL2X7X_STA_PRX_VALID          0x02
#define TSL2X7X_STA_ADC_PRX_VALID      (TSL2X7X_STA_ADC_VALID |\
					TSL2X7X_STA_PRX_VALID)
#define TSL2X7X_STA_ALS_INTR           0x10
#define TSL2X7X_STA_PRX_INTR           0x20

/* tsl2X7X cntrl reg masks */
#define TSL2X7X_CNTL_REG_CLEAR         0x00
#define TSL2X7X_CNTL_PROX_INT_ENBL     0X20
#define TSL2X7X_CNTL_ALS_INT_ENBL      0X10
#define TSL2X7X_CNTL_WAIT_TMR_ENBL     0X08
#define TSL2X7X_CNTL_PROX_DET_ENBL     0X04
#define TSL2X7X_CNTL_PWRON             0x01
#define TSL2X7X_CNTL_ALSPON_ENBL       0x03
#define TSL2X7X_CNTL_INTALSPON_ENBL    0x13
#define TSL2X7X_CNTL_PROXPON_ENBL      0x0F
#define TSL2X7X_CNTL_INTPROXPON_ENBL   0x2F

/*Prox diode to use */
#define TSL2X7X_DIODE0                 0x10
#define TSL2X7X_DIODE1                 0x20
#define TSL2X7X_DIODE_BOTH             0x30

/* LED Power */
#define TSL2X7X_mA100                  0x00
#define TSL2X7X_mA50                   0x40
#define TSL2X7X_mA25                   0x80
#define TSL2X7X_mA13                   0xD0
#define TSL2X7X_MAX_TIMER_CNT          (0xFF)

#define TSL2X7X_MIN_ITIME 3

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

struct tsl2x7x_parse_result {
	int integer;
	int fract;
};

/* Per-device data */
struct tsl2x7x_als_info {
	u16 als_ch0;
	u16 als_ch1;
	u16 lux;
};

struct tsl2x7x_prox_stat {
	int min;
	int max;
	int mean;
	unsigned long stddev;
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
	struct tsl2x7x_settings tsl2x7x_settings;
	struct tsl2X7X_platform_data *pdata;
	int als_time_scale;
	int als_saturation;
	int tsl2x7x_chip_status;
	u8 tsl2x7x_config[TSL2X7X_MAX_CONFIG_REG];
	const struct tsl2x7x_chip_info	*chip_info;
	const struct iio_info *info;
	s64 event_timestamp;
	/* This structure is intentionally large to accommodate
	 * updates via sysfs. */
	/* Sized to 9 = max 8 segments + 1 termination segment */
	struct tsl2x7x_lux tsl2x7x_device_lux[TSL2X7X_MAX_LUX_TABLE_SIZE];
};

/* Different devices require different coefficents */
static const struct tsl2x7x_lux tsl2x71_lux_table[] = {
	{ 14461,   611,   1211 },
	{ 18540,   352,    623 },
	{     0,     0,      0 },
};

static const struct tsl2x7x_lux tmd2x71_lux_table[] = {
	{ 11635,   115,    256 },
	{ 15536,    87,    179 },
	{     0,     0,      0 },
};

static const struct tsl2x7x_lux tsl2x72_lux_table[] = {
	{ 14013,   466,   917 },
	{ 18222,   310,   552 },
	{     0,     0,     0 },
};

static const struct tsl2x7x_lux tmd2x72_lux_table[] = {
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
	.als_time = 219, /* 101 ms */
	.als_gain = 0,
	.prx_time = 254, /* 5.4 ms */
	.prox_gain = 1,
	.wait_time = 245,
	.prox_config = 0,
	.als_gain_trim = 1000,
	.als_cal_target = 150,
	.als_thresh_low = 200,
	.als_thresh_high = 256,
	.persistence = 255,
	.interrupts_en = 0,
	.prox_thres_low  = 0,
	.prox_thres_high = 512,
	.prox_max_samples_cal = 30,
	.prox_pulse_count = 8
};

static const s16 tsl2X7X_als_gainadj[] = {
	1,
	8,
	16,
	120
};

static const s16 tsl2X7X_prx_gainadj[] = {
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

/**
 * tsl2x7x_i2c_read() - Read a byte from a register.
 * @client:	i2c client
 * @reg:	device register to read from
 * @*val:	pointer to location to store register contents.
 *
 */
static int
tsl2x7x_i2c_read(struct i2c_client *client, u8 reg, u8 *val)
{
	int ret = 0;

	/* select register to write */
	ret = i2c_smbus_write_byte(client, (TSL2X7X_CMD_REG | reg));
	if (ret < 0) {
		dev_err(&client->dev, "failed to write register %x\n", reg);
		return ret;
	}

	/* read the data */
	ret = i2c_smbus_read_byte(client);
	if (ret >= 0)
		*val = (u8)ret;
	else
		dev_err(&client->dev, "failed to read register %x\n", reg);

	return ret;
}

/**
 * tsl2x7x_get_lux() - Reads and calculates current lux value.
 * @indio_dev:	pointer to IIO device
 *
 * The raw ch0 and ch1 values of the ambient light sensed in the last
 * integration cycle are read from the device.
 * Time scale factor array values are adjusted based on the integration time.
 * The raw values are multiplied by a scale factor, and device gain is obtained
 * using gain index. Limit checks are done next, then the ratio of a multiple
 * of ch1 value, to the ch0 value, is calculated. Array tsl2x7x_device_lux[]
 * is then scanned to find the first ratio value that is just above the ratio
 * we just calculated. The ch0 and ch1 multiplier constants in the array are
 * then used along with the time scale factor array values, to calculate the
 * lux.
 */
static int tsl2x7x_get_lux(struct iio_dev *indio_dev)
{
	u16 ch0, ch1; /* separated ch0/ch1 data from device */
	u32 lux; /* raw lux calculated from device data */
	u64 lux64;
	u32 ratio;
	u8 buf[4];
	struct tsl2x7x_lux *p;
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int i, ret;
	u32 ch0lux = 0;
	u32 ch1lux = 0;

	if (mutex_trylock(&chip->als_mutex) == 0)
		return chip->als_cur_info.lux; /* busy, so return LAST VALUE */

	if (chip->tsl2x7x_chip_status != TSL2X7X_CHIP_WORKING) {
		/* device is not enabled */
		dev_err(&chip->client->dev, "%s: device is not enabled\n",
				__func__);
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = tsl2x7x_i2c_read(chip->client,
		(TSL2X7X_CMD_REG | TSL2X7X_STATUS), &buf[0]);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: Failed to read STATUS Reg\n", __func__);
		goto out_unlock;
	}
	/* is data new & valid */
	if (!(buf[0] & TSL2X7X_STA_ADC_VALID)) {
		dev_err(&chip->client->dev,
			"%s: data not valid yet\n", __func__);
		ret = chip->als_cur_info.lux; /* return LAST VALUE */
		goto out_unlock;
	}

	for (i = 0; i < 4; i++) {
		ret = tsl2x7x_i2c_read(chip->client,
			(TSL2X7X_CMD_REG | (TSL2X7X_ALS_CHAN0LO + i)),
			&buf[i]);
		if (ret < 0) {
			dev_err(&chip->client->dev,
				"failed to read. err=%x\n", ret);
			goto out_unlock;
		}
	}

	/* clear any existing interrupt status */
	ret = i2c_smbus_write_byte(chip->client,
		(TSL2X7X_CMD_REG |
				TSL2X7X_CMD_SPL_FN |
				TSL2X7X_CMD_ALS_INT_CLR));
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"i2c_write_command failed - err = %d\n", ret);
		goto out_unlock; /* have no data, so return failure */
	}

	/* extract ALS/lux data */
	ch0 = le16_to_cpup((const __le16 *)&buf[0]);
	ch1 = le16_to_cpup((const __le16 *)&buf[2]);

	chip->als_cur_info.als_ch0 = ch0;
	chip->als_cur_info.als_ch1 = ch1;

	if ((ch0 >= chip->als_saturation) || (ch1 >= chip->als_saturation)) {
		lux = TSL2X7X_LUX_CALC_OVER_FLOW;
		goto return_max;
	}

	if (!ch0) {
		/* have no data, so return LAST VALUE */
		ret = chip->als_cur_info.lux;
		goto out_unlock;
	}
	/* calculate ratio */
	ratio = (ch1 << 15) / ch0;
	/* convert to unscaled lux using the pointer to the table */
	p = (struct tsl2x7x_lux *) chip->tsl2x7x_device_lux;
	while (p->ratio != 0 && p->ratio < ratio)
		p++;

	if (p->ratio == 0) {
		lux = 0;
	} else {
		ch0lux = DIV_ROUND_UP(ch0 * p->ch0,
			tsl2X7X_als_gainadj[chip->tsl2x7x_settings.als_gain]);
		ch1lux = DIV_ROUND_UP(ch1 * p->ch1,
			tsl2X7X_als_gainadj[chip->tsl2x7x_settings.als_gain]);
		lux = ch0lux - ch1lux;
	}

	/* note: lux is 31 bit max at this point */
	if (ch1lux > ch0lux) {
		dev_dbg(&chip->client->dev, "ch1lux > ch0lux-return last value\n");
		ret = chip->als_cur_info.lux;
		goto out_unlock;
	}

	/* adjust for active time scale */
	if (chip->als_time_scale == 0)
		lux = 0;
	else
		lux = (lux + (chip->als_time_scale >> 1)) /
			chip->als_time_scale;

	/* adjust for active gain scale
	 * The tsl2x7x_device_lux tables have a factor of 256 built-in.
	 * User-specified gain provides a multiplier.
	 * Apply user-specified gain before shifting right to retain precision.
	 * Use 64 bits to avoid overflow on multiplication.
	 * Then go back to 32 bits before division to avoid using div_u64().
	 */

	lux64 = lux;
	lux64 = lux64 * chip->tsl2x7x_settings.als_gain_trim;
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
	int i;
	int ret;
	u8 status;
	u8 chdata[2];
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);

	if (mutex_trylock(&chip->prox_mutex) == 0) {
		dev_err(&chip->client->dev,
			"%s: Can't get prox mutex\n", __func__);
		return -EBUSY;
	}

	ret = tsl2x7x_i2c_read(chip->client,
		(TSL2X7X_CMD_REG | TSL2X7X_STATUS), &status);
	if (ret < 0) {
		dev_err(&chip->client->dev, "i2c err=%d\n", ret);
		goto prox_poll_err;
	}

	switch (chip->id) {
	case tsl2571:
	case tsl2671:
	case tmd2671:
	case tsl2771:
	case tmd2771:
		if (!(status & TSL2X7X_STA_ADC_VALID))
			goto prox_poll_err;
	break;
	case tsl2572:
	case tsl2672:
	case tmd2672:
	case tsl2772:
	case tmd2772:
		if (!(status & TSL2X7X_STA_PRX_VALID))
			goto prox_poll_err;
	break;
	}

	for (i = 0; i < 2; i++) {
		ret = tsl2x7x_i2c_read(chip->client,
			(TSL2X7X_CMD_REG |
					(TSL2X7X_PRX_LO + i)), &chdata[i]);
		if (ret < 0)
			goto prox_poll_err;
	}

	chip->prox_data =
			le16_to_cpup((const __le16 *)&chdata[0]);

prox_poll_err:

	mutex_unlock(&chip->prox_mutex);

	return chip->prox_data;
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
		memcpy(&(chip->tsl2x7x_settings),
			chip->pdata->platform_default_settings,
			sizeof(tsl2x7x_default_settings));
	else
		memcpy(&(chip->tsl2x7x_settings),
			&tsl2x7x_default_settings,
			sizeof(tsl2x7x_default_settings));

	/* Load up the proper lux table. */
	if (chip->pdata && chip->pdata->platform_lux_table[0].ratio != 0)
		memcpy(chip->tsl2x7x_device_lux,
			chip->pdata->platform_lux_table,
			sizeof(chip->pdata->platform_lux_table));
	else
		memcpy(chip->tsl2x7x_device_lux,
		(struct tsl2x7x_lux *)tsl2x7x_default_lux_table_group[chip->id],
				MAX_DEFAULT_TABLE_BYTES);
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
	u8 reg_val;
	int gain_trim_val;
	int ret;
	int lux_val;

	ret = i2c_smbus_write_byte(chip->client,
			(TSL2X7X_CMD_REG | TSL2X7X_CNTRL));
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"failed to write CNTRL register, ret=%d\n", ret);
		return ret;
	}

	reg_val = i2c_smbus_read_byte(chip->client);
	if ((reg_val & (TSL2X7X_CNTL_ADC_ENBL | TSL2X7X_CNTL_PWR_ON))
		!= (TSL2X7X_CNTL_ADC_ENBL | TSL2X7X_CNTL_PWR_ON)) {
		dev_err(&chip->client->dev,
			"%s: failed: ADC not enabled\n", __func__);
		return -1;
	}

	ret = i2c_smbus_write_byte(chip->client,
			(TSL2X7X_CMD_REG | TSL2X7X_CNTRL));
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"failed to write ctrl reg: ret=%d\n", ret);
		return ret;
	}

	reg_val = i2c_smbus_read_byte(chip->client);
	if ((reg_val & TSL2X7X_STA_ADC_VALID) != TSL2X7X_STA_ADC_VALID) {
		dev_err(&chip->client->dev,
			"%s: failed: STATUS - ADC not valid.\n", __func__);
		return -ENODATA;
	}

	lux_val = tsl2x7x_get_lux(indio_dev);
	if (lux_val < 0) {
		dev_err(&chip->client->dev,
		"%s: failed to get lux\n", __func__);
		return lux_val;
	}

	gain_trim_val =  ((chip->tsl2x7x_settings.als_cal_target)
			* chip->tsl2x7x_settings.als_gain_trim) / lux_val;
	if ((gain_trim_val < 250) || (gain_trim_val > 4000))
		return -ERANGE;

	chip->tsl2x7x_settings.als_gain_trim = gain_trim_val;
	dev_info(&chip->client->dev,
		"%s als_calibrate completed\n", chip->client->name);

	return (int) gain_trim_val;
}

static int tsl2x7x_chip_on(struct iio_dev *indio_dev)
{
	int i;
	int ret = 0;
	u8 *dev_reg;
	u8 utmp;
	int als_count;
	int als_time;
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	u8 reg_val = 0;

	if (chip->pdata && chip->pdata->power_on)
		chip->pdata->power_on(indio_dev);

	/* Non calculated parameters */
	chip->tsl2x7x_config[TSL2X7X_PRX_TIME] =
			chip->tsl2x7x_settings.prx_time;
	chip->tsl2x7x_config[TSL2X7X_WAIT_TIME] =
			chip->tsl2x7x_settings.wait_time;
	chip->tsl2x7x_config[TSL2X7X_PRX_CONFIG] =
			chip->tsl2x7x_settings.prox_config;

	chip->tsl2x7x_config[TSL2X7X_ALS_MINTHRESHLO] =
		(chip->tsl2x7x_settings.als_thresh_low) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_ALS_MINTHRESHHI] =
		(chip->tsl2x7x_settings.als_thresh_low >> 8) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_ALS_MAXTHRESHLO] =
		(chip->tsl2x7x_settings.als_thresh_high) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_ALS_MAXTHRESHHI] =
		(chip->tsl2x7x_settings.als_thresh_high >> 8) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_PERSISTENCE] =
		chip->tsl2x7x_settings.persistence;

	chip->tsl2x7x_config[TSL2X7X_PRX_COUNT] =
			chip->tsl2x7x_settings.prox_pulse_count;
	chip->tsl2x7x_config[TSL2X7X_PRX_MINTHRESHLO] =
			(chip->tsl2x7x_settings.prox_thres_low) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_PRX_MINTHRESHHI] =
			(chip->tsl2x7x_settings.prox_thres_low >> 8) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_PRX_MAXTHRESHLO] =
			(chip->tsl2x7x_settings.prox_thres_high) & 0xFF;
	chip->tsl2x7x_config[TSL2X7X_PRX_MAXTHRESHHI] =
			(chip->tsl2x7x_settings.prox_thres_high >> 8) & 0xFF;

	/* and make sure we're not already on */
	if (chip->tsl2x7x_chip_status == TSL2X7X_CHIP_WORKING) {
		/* if forcing a register update - turn off, then on */
		dev_info(&chip->client->dev, "device is already enabled\n");
		return -EINVAL;
	}

	/* determine als integration register */
	als_count = (chip->tsl2x7x_settings.als_time * 100 + 135) / 270;
	if (!als_count)
		als_count = 1; /* ensure at least one cycle */

	/* convert back to time (encompasses overrides) */
	als_time = (als_count * 27 + 5) / 10;
	chip->tsl2x7x_config[TSL2X7X_ALS_TIME] = 256 - als_count;

	/* Set the gain based on tsl2x7x_settings struct */
	chip->tsl2x7x_config[TSL2X7X_GAIN] =
		(chip->tsl2x7x_settings.als_gain |
			(TSL2X7X_mA100 | TSL2X7X_DIODE1)
			| ((chip->tsl2x7x_settings.prox_gain) << 2));

	/* set chip struct re scaling and saturation */
	chip->als_saturation = als_count * 922; /* 90% of full scale */
	chip->als_time_scale = (als_time + 25) / 50;

	/* TSL2X7X Specific power-on / adc enable sequence
	 * Power on the device 1st. */
	utmp = TSL2X7X_CNTL_PWR_ON;
	ret = i2c_smbus_write_byte_data(chip->client,
		TSL2X7X_CMD_REG | TSL2X7X_CNTRL, utmp);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed on CNTRL reg.\n", __func__);
		return ret;
	}

	/* Use the following shadow copy for our delay before enabling ADC.
	 * Write all the registers. */
	for (i = 0, dev_reg = chip->tsl2x7x_config;
			i < TSL2X7X_MAX_CONFIG_REG; i++) {
		ret = i2c_smbus_write_byte_data(chip->client,
				TSL2X7X_CMD_REG + i, *dev_reg++);
		if (ret < 0) {
			dev_err(&chip->client->dev,
				"failed on write to reg %d.\n", i);
			return ret;
		}
	}

	mdelay(3);	/* Power-on settling time */

	/* NOW enable the ADC
	 * initialize the desired mode of operation */
	utmp = TSL2X7X_CNTL_PWR_ON |
			TSL2X7X_CNTL_ADC_ENBL |
			TSL2X7X_CNTL_PROX_DET_ENBL;
	ret = i2c_smbus_write_byte_data(chip->client,
			TSL2X7X_CMD_REG | TSL2X7X_CNTRL, utmp);
	if (ret < 0) {
		dev_err(&chip->client->dev,
			"%s: failed on 2nd CTRL reg.\n", __func__);
		return ret;
	}

	chip->tsl2x7x_chip_status = TSL2X7X_CHIP_WORKING;

	if (chip->tsl2x7x_settings.interrupts_en != 0) {
		dev_info(&chip->client->dev, "Setting Up Interrupt(s)\n");

		reg_val = TSL2X7X_CNTL_PWR_ON | TSL2X7X_CNTL_ADC_ENBL;
		if ((chip->tsl2x7x_settings.interrupts_en == 0x20) ||
			(chip->tsl2x7x_settings.interrupts_en == 0x30))
			reg_val |= TSL2X7X_CNTL_PROX_DET_ENBL;

		reg_val |= chip->tsl2x7x_settings.interrupts_en;
		ret = i2c_smbus_write_byte_data(chip->client,
			(TSL2X7X_CMD_REG | TSL2X7X_CNTRL), reg_val);
		if (ret < 0)
			dev_err(&chip->client->dev,
				"%s: failed in tsl2x7x_IOCTL_INT_SET.\n",
				__func__);

		/* Clear out any initial interrupts  */
		ret = i2c_smbus_write_byte(chip->client,
			TSL2X7X_CMD_REG | TSL2X7X_CMD_SPL_FN |
			TSL2X7X_CMD_PROXALS_INT_CLR);
		if (ret < 0) {
			dev_err(&chip->client->dev,
				"%s: Failed to clear Int status\n",
				__func__);
		return ret;
		}
	}

	return ret;
}

static int tsl2x7x_chip_off(struct iio_dev *indio_dev)
{
	int ret;
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);

	/* turn device off */
	chip->tsl2x7x_chip_status = TSL2X7X_CHIP_SUSPENDED;

	ret = i2c_smbus_write_byte_data(chip->client,
		TSL2X7X_CMD_REG | TSL2X7X_CNTRL, 0x00);

	if (chip->pdata && chip->pdata->power_off)
		chip->pdata->power_off(chip->client);

	return ret;
}

/**
 * tsl2x7x_invoke_change
 * @indio_dev:	pointer to IIO device
 *
 * Obtain and lock both ALS and PROX resources,
 * determine and save device state (On/Off),
 * cycle device to implement updated parameter,
 * put device back into proper state, and unlock
 * resource.
 */
static
int tsl2x7x_invoke_change(struct iio_dev *indio_dev)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int device_status = chip->tsl2x7x_chip_status;

	mutex_lock(&chip->als_mutex);
	mutex_lock(&chip->prox_mutex);

	if (device_status == TSL2X7X_CHIP_WORKING)
		tsl2x7x_chip_off(indio_dev);

	tsl2x7x_chip_on(indio_dev);

	if (device_status != TSL2X7X_CHIP_WORKING)
		tsl2x7x_chip_off(indio_dev);

	mutex_unlock(&chip->prox_mutex);
	mutex_unlock(&chip->als_mutex);

	return 0;
}

static
void tsl2x7x_prox_calculate(int *data, int length,
		struct tsl2x7x_prox_stat *statP)
{
	int i;
	int sample_sum;
	int tmp;

	if (!length)
		length = 1;

	sample_sum = 0;
	statP->min = INT_MAX;
	statP->max = INT_MIN;
	for (i = 0; i < length; i++) {
		sample_sum += data[i];
		statP->min = min(statP->min, data[i]);
		statP->max = max(statP->max, data[i]);
	}

	statP->mean = sample_sum / length;
	sample_sum = 0;
	for (i = 0; i < length; i++) {
		tmp = data[i] - statP->mean;
		sample_sum += tmp * tmp;
	}
	statP->stddev = int_sqrt((long)sample_sum)/length;
}

/**
 * tsl2x7x_prox_cal() - Calculates std. and sets thresholds.
 * @indio_dev:	pointer to IIO device
 *
 * Calculates a standard deviation based on the samples,
 * and sets the threshold accordingly.
 */
static void tsl2x7x_prox_cal(struct iio_dev *indio_dev)
{
	int prox_history[MAX_SAMPLES_CAL + 1];
	int i;
	struct tsl2x7x_prox_stat prox_stat_data[2];
	struct tsl2x7x_prox_stat *calP;
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	u8 tmp_irq_settings;
	u8 current_state = chip->tsl2x7x_chip_status;

	if (chip->tsl2x7x_settings.prox_max_samples_cal > MAX_SAMPLES_CAL) {
		dev_err(&chip->client->dev,
			"max prox samples cal is too big: %d\n",
			chip->tsl2x7x_settings.prox_max_samples_cal);
		chip->tsl2x7x_settings.prox_max_samples_cal = MAX_SAMPLES_CAL;
	}

	/* have to stop to change settings */
	tsl2x7x_chip_off(indio_dev);

	/* Enable proximity detection save just in case prox not wanted yet*/
	tmp_irq_settings = chip->tsl2x7x_settings.interrupts_en;
	chip->tsl2x7x_settings.interrupts_en |= TSL2X7X_CNTL_PROX_INT_ENBL;

	/*turn on device if not already on*/
	tsl2x7x_chip_on(indio_dev);

	/*gather the samples*/
	for (i = 0; i < chip->tsl2x7x_settings.prox_max_samples_cal; i++) {
		mdelay(15);
		tsl2x7x_get_prox(indio_dev);
		prox_history[i] = chip->prox_data;
		dev_info(&chip->client->dev, "2 i=%d prox data= %d\n",
			i, chip->prox_data);
	}

	tsl2x7x_chip_off(indio_dev);
	calP = &prox_stat_data[PROX_STAT_CAL];
	tsl2x7x_prox_calculate(prox_history,
		chip->tsl2x7x_settings.prox_max_samples_cal, calP);
	chip->tsl2x7x_settings.prox_thres_high = (calP->max << 1) - calP->mean;

	dev_info(&chip->client->dev, " cal min=%d mean=%d max=%d\n",
		calP->min, calP->mean, calP->max);
	dev_info(&chip->client->dev,
		"%s proximity threshold set to %d\n",
		chip->client->name, chip->tsl2x7x_settings.prox_thres_high);

	/* back to the way they were */
	chip->tsl2x7x_settings.interrupts_en = tmp_irq_settings;
	if (current_state == TSL2X7X_CHIP_WORKING)
		tsl2x7x_chip_on(indio_dev);
}

static ssize_t tsl2x7x_power_state_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tsl2X7X_chip *chip = iio_priv(dev_to_iio_dev(dev));

	return snprintf(buf, PAGE_SIZE, "%d\n", chip->tsl2x7x_chip_status);
}

static ssize_t tsl2x7x_power_state_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	bool value;

	if (strtobool(buf, &value))
		return -EINVAL;

	if (value)
		tsl2x7x_chip_on(indio_dev);
	else
		tsl2x7x_chip_off(indio_dev);

	return len;
}

static ssize_t tsl2x7x_gain_available_show(struct device *dev,
	struct device_attribute *attr, char *buf)
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

static ssize_t tsl2x7x_prox_gain_available_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
		return snprintf(buf, PAGE_SIZE, "%s\n", "1 2 4 8");
}

static ssize_t tsl2x7x_als_time_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tsl2X7X_chip *chip = iio_priv(dev_to_iio_dev(dev));
	int y, z;

	y = (TSL2X7X_MAX_TIMER_CNT - (u8)chip->tsl2x7x_settings.als_time) + 1;
	z = y * TSL2X7X_MIN_ITIME;
	y /= 1000;
	z %= 1000;

	return snprintf(buf, PAGE_SIZE, "%d.%03d\n", y, z);
}

static ssize_t tsl2x7x_als_time_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	struct tsl2x7x_parse_result result;
	int ret;

	ret = iio_str_to_fixpoint(buf, 100, &result.integer, &result.fract);
	if (ret)
		return ret;

	result.fract /= 3;
	chip->tsl2x7x_settings.als_time =
			(TSL2X7X_MAX_TIMER_CNT - (u8)result.fract);

	dev_info(&chip->client->dev, "%s: als time = %d",
		__func__, chip->tsl2x7x_settings.als_time);

	tsl2x7x_invoke_change(indio_dev);

	return IIO_VAL_INT_PLUS_MICRO;
}

static IIO_CONST_ATTR(in_illuminance0_integration_time_available,
		".00272 - .696");

static ssize_t tsl2x7x_als_cal_target_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tsl2X7X_chip *chip = iio_priv(dev_to_iio_dev(dev));

	return snprintf(buf, PAGE_SIZE, "%d\n",
			chip->tsl2x7x_settings.als_cal_target);
}

static ssize_t tsl2x7x_als_cal_target_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	unsigned long value;

	if (kstrtoul(buf, 0, &value))
		return -EINVAL;

	if (value)
		chip->tsl2x7x_settings.als_cal_target = value;

	tsl2x7x_invoke_change(indio_dev);

	return len;
}

/* persistence settings */
static ssize_t tsl2x7x_als_persistence_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tsl2X7X_chip *chip = iio_priv(dev_to_iio_dev(dev));
	int y, z, filter_delay;

	/* Determine integration time */
	y = (TSL2X7X_MAX_TIMER_CNT - (u8)chip->tsl2x7x_settings.als_time) + 1;
	z = y * TSL2X7X_MIN_ITIME;
	filter_delay = z * (chip->tsl2x7x_settings.persistence & 0x0F);
	y = filter_delay / 1000;
	z = filter_delay % 1000;

	return snprintf(buf, PAGE_SIZE, "%d.%03d\n", y, z);
}

static ssize_t tsl2x7x_als_persistence_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	struct tsl2x7x_parse_result result;
	int y, z, filter_delay;
	int ret;

	ret = iio_str_to_fixpoint(buf, 100, &result.integer, &result.fract);
	if (ret)
		return ret;

	y = (TSL2X7X_MAX_TIMER_CNT - (u8)chip->tsl2x7x_settings.als_time) + 1;
	z = y * TSL2X7X_MIN_ITIME;

	filter_delay =
		DIV_ROUND_UP((result.integer * 1000) + result.fract, z);

	chip->tsl2x7x_settings.persistence &= 0xF0;
	chip->tsl2x7x_settings.persistence |= (filter_delay & 0x0F);

	dev_info(&chip->client->dev, "%s: als persistence = %d",
		__func__, filter_delay);

	tsl2x7x_invoke_change(indio_dev);

	return IIO_VAL_INT_PLUS_MICRO;
}

static ssize_t tsl2x7x_prox_persistence_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tsl2X7X_chip *chip = iio_priv(dev_to_iio_dev(dev));
	int y, z, filter_delay;

	/* Determine integration time */
	y = (TSL2X7X_MAX_TIMER_CNT - (u8)chip->tsl2x7x_settings.prx_time) + 1;
	z = y * TSL2X7X_MIN_ITIME;
	filter_delay = z * ((chip->tsl2x7x_settings.persistence & 0xF0) >> 4);
	y = filter_delay / 1000;
	z = filter_delay % 1000;

	return snprintf(buf, PAGE_SIZE, "%d.%03d\n", y, z);
}

static ssize_t tsl2x7x_prox_persistence_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	struct tsl2x7x_parse_result result;
	int y, z, filter_delay;
	int ret;

	ret = iio_str_to_fixpoint(buf, 100, &result.integer, &result.fract);
	if (ret)
		return ret;

	y = (TSL2X7X_MAX_TIMER_CNT - (u8)chip->tsl2x7x_settings.prx_time) + 1;
	z = y * TSL2X7X_MIN_ITIME;

	filter_delay =
		DIV_ROUND_UP((result.integer * 1000) + result.fract, z);

	chip->tsl2x7x_settings.persistence &= 0x0F;
	chip->tsl2x7x_settings.persistence |= ((filter_delay << 4) & 0xF0);

	dev_info(&chip->client->dev, "%s: prox persistence = %d",
		__func__, filter_delay);

	tsl2x7x_invoke_change(indio_dev);

	return IIO_VAL_INT_PLUS_MICRO;
}

static ssize_t tsl2x7x_do_calibrate(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	bool value;

	if (strtobool(buf, &value))
		return -EINVAL;

	if (value)
		tsl2x7x_als_calibrate(indio_dev);

	tsl2x7x_invoke_change(indio_dev);

	return len;
}

static ssize_t tsl2x7x_luxtable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tsl2X7X_chip *chip = iio_priv(dev_to_iio_dev(dev));
	int i = 0;
	int offset = 0;

	while (i < (TSL2X7X_MAX_LUX_TABLE_SIZE * 3)) {
		offset += snprintf(buf + offset, PAGE_SIZE, "%u,%u,%u,",
			chip->tsl2x7x_device_lux[i].ratio,
			chip->tsl2x7x_device_lux[i].ch0,
			chip->tsl2x7x_device_lux[i].ch1);
		if (chip->tsl2x7x_device_lux[i].ratio == 0) {
			/* We just printed the first "0" entry.
			 * Now get rid of the extra "," and break. */
			offset--;
			break;
		}
		i++;
	}

	offset += snprintf(buf + offset, PAGE_SIZE, "\n");
	return offset;
}

static ssize_t tsl2x7x_luxtable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int value[ARRAY_SIZE(chip->tsl2x7x_device_lux)*3 + 1];
	int n;

	get_options(buf, ARRAY_SIZE(value), value);

	/* We now have an array of ints starting at value[1], and
	 * enumerated by value[0].
	 * We expect each group of three ints is one table entry,
	 * and the last table entry is all 0.
	 */
	n = value[0];
	if ((n % 3) || n < 6 ||
			n > ((ARRAY_SIZE(chip->tsl2x7x_device_lux) - 1) * 3)) {
		dev_info(dev, "LUX TABLE INPUT ERROR 1 Value[0]=%d\n", n);
		return -EINVAL;
	}

	if ((value[(n - 2)] | value[(n - 1)] | value[n]) != 0) {
		dev_info(dev, "LUX TABLE INPUT ERROR 2 Value[0]=%d\n", n);
		return -EINVAL;
	}

	if (chip->tsl2x7x_chip_status == TSL2X7X_CHIP_WORKING)
		tsl2x7x_chip_off(indio_dev);

	/* Zero out the table */
	memset(chip->tsl2x7x_device_lux, 0, sizeof(chip->tsl2x7x_device_lux));
	memcpy(chip->tsl2x7x_device_lux, &value[1], (value[0] * 4));

	tsl2x7x_invoke_change(indio_dev);

	return len;
}

static ssize_t tsl2x7x_do_prox_calibrate(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	bool value;

	if (strtobool(buf, &value))
		return -EINVAL;

	if (value)
		tsl2x7x_prox_cal(indio_dev);

	tsl2x7x_invoke_change(indio_dev);

	return len;
}

static int tsl2x7x_read_interrupt_config(struct iio_dev *indio_dev,
					 const struct iio_chan_spec *chan,
					 enum iio_event_type type,
					 enum iio_event_direction dir)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int ret;

	if (chan->type == IIO_INTENSITY)
		ret = !!(chip->tsl2x7x_settings.interrupts_en & 0x10);
	else
		ret = !!(chip->tsl2x7x_settings.interrupts_en & 0x20);

	return ret;
}

static int tsl2x7x_write_interrupt_config(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir,
					  int val)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);

	if (chan->type == IIO_INTENSITY) {
		if (val)
			chip->tsl2x7x_settings.interrupts_en |= 0x10;
		else
			chip->tsl2x7x_settings.interrupts_en &= 0x20;
	} else {
		if (val)
			chip->tsl2x7x_settings.interrupts_en |= 0x20;
		else
			chip->tsl2x7x_settings.interrupts_en &= 0x10;
	}

	tsl2x7x_invoke_change(indio_dev);

	return 0;
}

static int tsl2x7x_write_thresh(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				enum iio_event_info info,
				int val, int val2)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);

	if (chan->type == IIO_INTENSITY) {
		switch (dir) {
		case IIO_EV_DIR_RISING:
			chip->tsl2x7x_settings.als_thresh_high = val;
			break;
		case IIO_EV_DIR_FALLING:
			chip->tsl2x7x_settings.als_thresh_low = val;
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (dir) {
		case IIO_EV_DIR_RISING:
			chip->tsl2x7x_settings.prox_thres_high = val;
			break;
		case IIO_EV_DIR_FALLING:
			chip->tsl2x7x_settings.prox_thres_low = val;
			break;
		default:
			return -EINVAL;
		}
	}

	tsl2x7x_invoke_change(indio_dev);

	return 0;
}

static int tsl2x7x_read_thresh(struct iio_dev *indio_dev,
			       const struct iio_chan_spec *chan,
			       enum iio_event_type type,
			       enum iio_event_direction dir,
				   enum iio_event_info info,
			       int *val, int *val2)
{
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);

	if (chan->type == IIO_INTENSITY) {
		switch (dir) {
		case IIO_EV_DIR_RISING:
			*val = chip->tsl2x7x_settings.als_thresh_high;
			break;
		case IIO_EV_DIR_FALLING:
			*val = chip->tsl2x7x_settings.als_thresh_low;
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (dir) {
		case IIO_EV_DIR_RISING:
			*val = chip->tsl2x7x_settings.prox_thres_high;
			break;
		case IIO_EV_DIR_FALLING:
			*val = chip->tsl2x7x_settings.prox_thres_low;
			break;
		default:
			return -EINVAL;
		}
	}

	return IIO_VAL_INT;
}

static int tsl2x7x_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val,
			    int *val2,
			    long mask)
{
	int ret = -EINVAL;
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);

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
			*val =
			tsl2X7X_als_gainadj[chip->tsl2x7x_settings.als_gain];
		else
			*val =
			tsl2X7X_prx_gainadj[chip->tsl2x7x_settings.prox_gain];
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		*val = chip->tsl2x7x_settings.als_gain_trim;
		ret = IIO_VAL_INT;
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
				chip->tsl2x7x_settings.als_gain = 0;
				break;
			case 8:
				chip->tsl2x7x_settings.als_gain = 1;
				break;
			case 16:
				chip->tsl2x7x_settings.als_gain = 2;
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
				chip->tsl2x7x_settings.als_gain = 3;
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
				chip->tsl2x7x_settings.als_gain = 3;
				break;
			default:
				return -EINVAL;
			}
		} else {
			switch (val) {
			case 1:
				chip->tsl2x7x_settings.prox_gain = 0;
				break;
			case 2:
				chip->tsl2x7x_settings.prox_gain = 1;
				break;
			case 4:
				chip->tsl2x7x_settings.prox_gain = 2;
				break;
			case 8:
				chip->tsl2x7x_settings.prox_gain = 3;
				break;
			default:
				return -EINVAL;
			}
		}
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		chip->tsl2x7x_settings.als_gain_trim = val;
		break;

	default:
		return -EINVAL;
	}

	tsl2x7x_invoke_change(indio_dev);

	return 0;
}

static DEVICE_ATTR(power_state, S_IRUGO | S_IWUSR,
		tsl2x7x_power_state_show, tsl2x7x_power_state_store);

static DEVICE_ATTR(in_proximity0_calibscale_available, S_IRUGO,
		tsl2x7x_prox_gain_available_show, NULL);

static DEVICE_ATTR(in_illuminance0_calibscale_available, S_IRUGO,
		tsl2x7x_gain_available_show, NULL);

static DEVICE_ATTR(in_illuminance0_integration_time, S_IRUGO | S_IWUSR,
		tsl2x7x_als_time_show, tsl2x7x_als_time_store);

static DEVICE_ATTR(in_illuminance0_target_input, S_IRUGO | S_IWUSR,
		tsl2x7x_als_cal_target_show, tsl2x7x_als_cal_target_store);

static DEVICE_ATTR(in_illuminance0_calibrate, S_IWUSR, NULL,
		tsl2x7x_do_calibrate);

static DEVICE_ATTR(in_proximity0_calibrate, S_IWUSR, NULL,
		tsl2x7x_do_prox_calibrate);

static DEVICE_ATTR(in_illuminance0_lux_table, S_IRUGO | S_IWUSR,
		tsl2x7x_luxtable_show, tsl2x7x_luxtable_store);

static DEVICE_ATTR(in_intensity0_thresh_period, S_IRUGO | S_IWUSR,
		tsl2x7x_als_persistence_show, tsl2x7x_als_persistence_store);

static DEVICE_ATTR(in_proximity0_thresh_period, S_IRUGO | S_IWUSR,
		tsl2x7x_prox_persistence_show, tsl2x7x_prox_persistence_store);

/* Use the default register values to identify the Taos device */
static int tsl2x7x_device_id(unsigned char *id, int target)
{
	switch (target) {
	case tsl2571:
	case tsl2671:
	case tsl2771:
		return (*id & 0xf0) == TRITON_ID;
	case tmd2671:
	case tmd2771:
		return (*id & 0xf0) == HALIBUT_ID;
	case tsl2572:
	case tsl2672:
	case tmd2672:
	case tsl2772:
	case tmd2772:
		return (*id & 0xf0) == SWORDFISH_ID;
	}

	return -EINVAL;
}

static irqreturn_t tsl2x7x_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	s64 timestamp = iio_get_time_ns();
	int ret;
	u8 value;

	value = i2c_smbus_read_byte_data(chip->client,
		TSL2X7X_CMD_REG | TSL2X7X_STATUS);

	/* What type of interrupt do we need to process */
	if (value & TSL2X7X_STA_PRX_INTR) {
		tsl2x7x_get_prox(indio_dev); /* freshen data for ABI */
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
						    timestamp);
	}

	if (value & TSL2X7X_STA_ALS_INTR) {
		tsl2x7x_get_lux(indio_dev); /* freshen data for ABI */
		iio_push_event(indio_dev,
		       IIO_UNMOD_EVENT_CODE(IIO_LIGHT,
					    0,
					    IIO_EV_TYPE_THRESH,
					    IIO_EV_DIR_EITHER),
					    timestamp);
	}
	/* Clear interrupt now that we have handled it. */
	ret = i2c_smbus_write_byte(chip->client,
		TSL2X7X_CMD_REG | TSL2X7X_CMD_SPL_FN |
		TSL2X7X_CMD_PROXALS_INT_CLR);
	if (ret < 0)
		dev_err(&chip->client->dev,
			"Failed to clear irq from event handler. err = %d\n",
			ret);

	return IRQ_HANDLED;
}

static struct attribute *tsl2x7x_ALS_device_attrs[] = {
	&dev_attr_power_state.attr,
	&dev_attr_in_illuminance0_calibscale_available.attr,
	&dev_attr_in_illuminance0_integration_time.attr,
	&iio_const_attr_in_illuminance0_integration_time_available.dev_attr.attr,
	&dev_attr_in_illuminance0_target_input.attr,
	&dev_attr_in_illuminance0_calibrate.attr,
	&dev_attr_in_illuminance0_lux_table.attr,
	NULL
};

static struct attribute *tsl2x7x_PRX_device_attrs[] = {
	&dev_attr_power_state.attr,
	&dev_attr_in_proximity0_calibrate.attr,
	NULL
};

static struct attribute *tsl2x7x_ALSPRX_device_attrs[] = {
	&dev_attr_power_state.attr,
	&dev_attr_in_illuminance0_calibscale_available.attr,
	&dev_attr_in_illuminance0_integration_time.attr,
	&iio_const_attr_in_illuminance0_integration_time_available.dev_attr.attr,
	&dev_attr_in_illuminance0_target_input.attr,
	&dev_attr_in_illuminance0_calibrate.attr,
	&dev_attr_in_illuminance0_lux_table.attr,
	&dev_attr_in_proximity0_calibrate.attr,
	NULL
};

static struct attribute *tsl2x7x_PRX2_device_attrs[] = {
	&dev_attr_power_state.attr,
	&dev_attr_in_proximity0_calibrate.attr,
	&dev_attr_in_proximity0_calibscale_available.attr,
	NULL
};

static struct attribute *tsl2x7x_ALSPRX2_device_attrs[] = {
	&dev_attr_power_state.attr,
	&dev_attr_in_illuminance0_calibscale_available.attr,
	&dev_attr_in_illuminance0_integration_time.attr,
	&iio_const_attr_in_illuminance0_integration_time_available.dev_attr.attr,
	&dev_attr_in_illuminance0_target_input.attr,
	&dev_attr_in_illuminance0_calibrate.attr,
	&dev_attr_in_illuminance0_lux_table.attr,
	&dev_attr_in_proximity0_calibrate.attr,
	&dev_attr_in_proximity0_calibscale_available.attr,
	NULL
};

static struct attribute *tsl2X7X_ALS_event_attrs[] = {
	&dev_attr_in_intensity0_thresh_period.attr,
	NULL,
};
static struct attribute *tsl2X7X_PRX_event_attrs[] = {
	&dev_attr_in_proximity0_thresh_period.attr,
	NULL,
};

static struct attribute *tsl2X7X_ALSPRX_event_attrs[] = {
	&dev_attr_in_intensity0_thresh_period.attr,
	&dev_attr_in_proximity0_thresh_period.attr,
	NULL,
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

static struct attribute_group tsl2X7X_event_attr_group_tbl[] = {
	[ALS] = {
		.attrs = tsl2X7X_ALS_event_attrs,
		.name = "events",
	},
	[PRX] = {
		.attrs = tsl2X7X_PRX_event_attrs,
		.name = "events",
	},
	[ALSPRX] = {
		.attrs = tsl2X7X_ALSPRX_event_attrs,
		.name = "events",
	},
};

static const struct iio_info tsl2X7X_device_info[] = {
	[ALS] = {
		.attrs = &tsl2X7X_device_attr_group_tbl[ALS],
		.event_attrs = &tsl2X7X_event_attr_group_tbl[ALS],
		.driver_module = THIS_MODULE,
		.read_raw = &tsl2x7x_read_raw,
		.write_raw = &tsl2x7x_write_raw,
		.read_event_value = &tsl2x7x_read_thresh,
		.write_event_value = &tsl2x7x_write_thresh,
		.read_event_config = &tsl2x7x_read_interrupt_config,
		.write_event_config = &tsl2x7x_write_interrupt_config,
	},
	[PRX] = {
		.attrs = &tsl2X7X_device_attr_group_tbl[PRX],
		.event_attrs = &tsl2X7X_event_attr_group_tbl[PRX],
		.driver_module = THIS_MODULE,
		.read_raw = &tsl2x7x_read_raw,
		.write_raw = &tsl2x7x_write_raw,
		.read_event_value = &tsl2x7x_read_thresh,
		.write_event_value = &tsl2x7x_write_thresh,
		.read_event_config = &tsl2x7x_read_interrupt_config,
		.write_event_config = &tsl2x7x_write_interrupt_config,
	},
	[ALSPRX] = {
		.attrs = &tsl2X7X_device_attr_group_tbl[ALSPRX],
		.event_attrs = &tsl2X7X_event_attr_group_tbl[ALSPRX],
		.driver_module = THIS_MODULE,
		.read_raw = &tsl2x7x_read_raw,
		.write_raw = &tsl2x7x_write_raw,
		.read_event_value = &tsl2x7x_read_thresh,
		.write_event_value = &tsl2x7x_write_thresh,
		.read_event_config = &tsl2x7x_read_interrupt_config,
		.write_event_config = &tsl2x7x_write_interrupt_config,
	},
	[PRX2] = {
		.attrs = &tsl2X7X_device_attr_group_tbl[PRX2],
		.event_attrs = &tsl2X7X_event_attr_group_tbl[PRX],
		.driver_module = THIS_MODULE,
		.read_raw = &tsl2x7x_read_raw,
		.write_raw = &tsl2x7x_write_raw,
		.read_event_value = &tsl2x7x_read_thresh,
		.write_event_value = &tsl2x7x_write_thresh,
		.read_event_config = &tsl2x7x_read_interrupt_config,
		.write_event_config = &tsl2x7x_write_interrupt_config,
	},
	[ALSPRX2] = {
		.attrs = &tsl2X7X_device_attr_group_tbl[ALSPRX2],
		.event_attrs = &tsl2X7X_event_attr_group_tbl[ALSPRX],
		.driver_module = THIS_MODULE,
		.read_raw = &tsl2x7x_read_raw,
		.write_raw = &tsl2x7x_write_raw,
		.read_event_value = &tsl2x7x_read_thresh,
		.write_event_value = &tsl2x7x_write_thresh,
		.read_event_config = &tsl2x7x_read_interrupt_config,
		.write_event_config = &tsl2x7x_write_interrupt_config,
	},
};

static const struct iio_event_spec tsl2x7x_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
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
			.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED)
			}, {
			.type = IIO_INTENSITY,
			.indexed = 1,
			.channel = 0,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
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
	int ret;
	unsigned char device_id;
	struct iio_dev *indio_dev;
	struct tsl2X7X_chip *chip;

	indio_dev = devm_iio_device_alloc(&clientp->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);
	chip->client = clientp;
	i2c_set_clientdata(clientp, indio_dev);

	ret = tsl2x7x_i2c_read(chip->client,
		TSL2X7X_CHIPID, &device_id);
	if (ret < 0)
		return ret;

	if ((!tsl2x7x_device_id(&device_id, id->driver_data)) ||
		(tsl2x7x_device_id(&device_id, id->driver_data) == -EINVAL)) {
		dev_info(&chip->client->dev,
				"%s: i2c device found does not match expected id\n",
				__func__);
		return -EINVAL;
	}

	ret = i2c_smbus_write_byte(clientp, (TSL2X7X_CMD_REG | TSL2X7X_CNTRL));
	if (ret < 0) {
		dev_err(&clientp->dev, "write to cmd reg failed. err = %d\n",
			ret);
		return ret;
	}

	/* ALS and PROX functions can be invoked via user space poll
	 * or H/W interrupt. If busy return last sample. */
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
						IRQF_TRIGGER_RISING |
						IRQF_ONESHOT,
						"TSL2X7X_event",
						indio_dev);
		if (ret) {
			dev_err(&clientp->dev,
				"%s: irq request failed", __func__);
			return ret;
		}
	}

	/* Load up the defaults */
	tsl2x7x_defaults(chip);
	/* Make sure the chip is on */
	tsl2x7x_chip_on(indio_dev);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&clientp->dev,
			"%s: iio registration failed\n", __func__);
		return ret;
	}

	dev_info(&clientp->dev, "%s Light sensor found.\n", id->name);

	return 0;
}

static int tsl2x7x_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int ret = 0;

	if (chip->tsl2x7x_chip_status == TSL2X7X_CHIP_WORKING) {
		ret = tsl2x7x_chip_off(indio_dev);
		chip->tsl2x7x_chip_status = TSL2X7X_CHIP_SUSPENDED;
	}

	if (chip->pdata && chip->pdata->platform_power) {
		pm_message_t pmm = {PM_EVENT_SUSPEND};

		chip->pdata->platform_power(dev, pmm);
	}

	return ret;
}

static int tsl2x7x_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct tsl2X7X_chip *chip = iio_priv(indio_dev);
	int ret = 0;

	if (chip->pdata && chip->pdata->platform_power) {
		pm_message_t pmm = {PM_EVENT_RESUME};

		chip->pdata->platform_power(dev, pmm);
	}

	if (chip->tsl2x7x_chip_status == TSL2X7X_CHIP_SUSPENDED)
		ret = tsl2x7x_chip_on(indio_dev);

	return ret;
}

static int tsl2x7x_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	tsl2x7x_chip_off(indio_dev);

	iio_device_unregister(indio_dev);

	return 0;
}

static struct i2c_device_id tsl2x7x_idtable[] = {
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

static const struct dev_pm_ops tsl2x7x_pm_ops = {
	.suspend = tsl2x7x_suspend,
	.resume  = tsl2x7x_resume,
};

/* Driver definition */
static struct i2c_driver tsl2x7x_driver = {
	.driver = {
		.name = "tsl2x7x",
		.pm = &tsl2x7x_pm_ops,
	},
	.id_table = tsl2x7x_idtable,
	.probe = tsl2x7x_probe,
	.remove = tsl2x7x_remove,
};

module_i2c_driver(tsl2x7x_driver);

MODULE_AUTHOR("J. August Brenner<jbrenner@taosinc.com>");
MODULE_DESCRIPTION("TAOS tsl2x7x ambient and proximity light sensor driver");
MODULE_LICENSE("GPL");
