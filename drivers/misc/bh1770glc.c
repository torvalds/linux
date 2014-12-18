/*
 * This file is part of the ROHM BH1770GLC / OSRAM SFH7770 sensor driver.
 * Chip is combined proximity and ambient light sensor.
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Samu Onkalo <samu.p.onkalo@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/i2c/bh1770glc.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/slab.h>

#define BH1770_ALS_CONTROL	0x80 /* ALS operation mode control */
#define BH1770_PS_CONTROL	0x81 /* PS operation mode control */
#define BH1770_I_LED		0x82 /* active LED and LED1, LED2 current */
#define BH1770_I_LED3		0x83 /* LED3 current setting */
#define BH1770_ALS_PS_MEAS	0x84 /* Forced mode trigger */
#define BH1770_PS_MEAS_RATE	0x85 /* PS meas. rate at stand alone mode */
#define BH1770_ALS_MEAS_RATE	0x86 /* ALS meas. rate at stand alone mode */
#define BH1770_PART_ID		0x8a /* Part number and revision ID */
#define BH1770_MANUFACT_ID	0x8b /* Manufacturerer ID */
#define BH1770_ALS_DATA_0	0x8c /* ALS DATA low byte */
#define BH1770_ALS_DATA_1	0x8d /* ALS DATA high byte */
#define BH1770_ALS_PS_STATUS	0x8e /* Measurement data and int status */
#define BH1770_PS_DATA_LED1	0x8f /* PS data from LED1 */
#define BH1770_PS_DATA_LED2	0x90 /* PS data from LED2 */
#define BH1770_PS_DATA_LED3	0x91 /* PS data from LED3 */
#define BH1770_INTERRUPT	0x92 /* Interrupt setting */
#define BH1770_PS_TH_LED1	0x93 /* PS interrupt threshold for LED1 */
#define BH1770_PS_TH_LED2	0x94 /* PS interrupt threshold for LED2 */
#define BH1770_PS_TH_LED3	0x95 /* PS interrupt threshold for LED3 */
#define BH1770_ALS_TH_UP_0	0x96 /* ALS upper threshold low byte */
#define BH1770_ALS_TH_UP_1	0x97 /* ALS upper threshold high byte */
#define BH1770_ALS_TH_LOW_0	0x98 /* ALS lower threshold low byte */
#define BH1770_ALS_TH_LOW_1	0x99 /* ALS lower threshold high byte */

/* MANUFACT_ID */
#define BH1770_MANUFACT_ROHM	0x01
#define BH1770_MANUFACT_OSRAM	0x03

/* PART_ID */
#define BH1770_PART		0x90
#define BH1770_PART_MASK	0xf0
#define BH1770_REV_MASK		0x0f
#define BH1770_REV_SHIFT	0
#define BH1770_REV_0		0x00
#define BH1770_REV_1		0x01

/* Operating modes for both */
#define BH1770_STANDBY		0x00
#define BH1770_FORCED		0x02
#define BH1770_STANDALONE	0x03
#define BH1770_SWRESET		(0x01 << 2)

#define BH1770_PS_TRIG_MEAS	(1 << 0)
#define BH1770_ALS_TRIG_MEAS	(1 << 1)

/* Interrupt control */
#define BH1770_INT_OUTPUT_MODE	(1 << 3) /* 0 = latched */
#define BH1770_INT_POLARITY	(1 << 2) /* 1 = active high */
#define BH1770_INT_ALS_ENA	(1 << 1)
#define BH1770_INT_PS_ENA	(1 << 0)

/* Interrupt status */
#define BH1770_INT_LED1_DATA	(1 << 0)
#define BH1770_INT_LED1_INT	(1 << 1)
#define BH1770_INT_LED2_DATA	(1 << 2)
#define BH1770_INT_LED2_INT	(1 << 3)
#define BH1770_INT_LED3_DATA	(1 << 4)
#define BH1770_INT_LED3_INT	(1 << 5)
#define BH1770_INT_LEDS_INT	((1 << 1) | (1 << 3) | (1 << 5))
#define BH1770_INT_ALS_DATA	(1 << 6)
#define BH1770_INT_ALS_INT	(1 << 7)

/* Led channels */
#define BH1770_LED1		0x00

#define BH1770_DISABLE		0
#define BH1770_ENABLE		1
#define BH1770_PROX_CHANNELS	1

#define BH1770_LUX_DEFAULT_RATE	1 /* Index to lux rate table */
#define BH1770_PROX_DEFAULT_RATE 1 /* Direct HW value =~ 50Hz */
#define BH1770_PROX_DEF_RATE_THRESH 6 /* Direct HW value =~ 5 Hz */
#define BH1770_STARTUP_DELAY	50
#define BH1770_RESET_TIME	10
#define BH1770_TIMEOUT		2100 /* Timeout in 2.1 seconds */

#define BH1770_LUX_RANGE	65535
#define BH1770_PROX_RANGE	255
#define BH1770_COEF_SCALER	1024
#define BH1770_CALIB_SCALER	8192
#define BH1770_LUX_NEUTRAL_CALIB_VALUE (1 * BH1770_CALIB_SCALER)
#define BH1770_LUX_DEF_THRES	1000
#define BH1770_PROX_DEF_THRES	70
#define BH1770_PROX_DEF_ABS_THRES   100
#define BH1770_DEFAULT_PERSISTENCE  10
#define BH1770_PROX_MAX_PERSISTENCE 50
#define BH1770_LUX_GA_SCALE	16384
#define BH1770_LUX_CF_SCALE	2048 /* CF ChipFactor */
#define BH1770_NEUTRAL_CF	BH1770_LUX_CF_SCALE
#define BH1770_LUX_CORR_SCALE	4096

#define PROX_ABOVE_THRESHOLD	1
#define PROX_BELOW_THRESHOLD	0

#define PROX_IGNORE_LUX_LIMIT	500

struct bh1770_chip {
	struct bh1770_platform_data	*pdata;
	char				chipname[10];
	u8				revision;
	struct i2c_client		*client;
	struct regulator_bulk_data	regs[2];
	struct mutex			mutex; /* avoid parallel access */
	wait_queue_head_t		wait;

	bool			int_mode_prox;
	bool			int_mode_lux;
	struct delayed_work	prox_work;
	u32	lux_cf; /* Chip specific factor */
	u32	lux_ga;
	u32	lux_calib;
	int	lux_rate_index;
	u32	lux_corr;
	u16	lux_data_raw;
	u16	lux_threshold_hi;
	u16	lux_threshold_lo;
	u16	lux_thres_hi_onchip;
	u16	lux_thres_lo_onchip;
	bool	lux_wait_result;

	int	prox_enable_count;
	u16	prox_coef;
	u16	prox_const;
	int	prox_rate;
	int	prox_rate_threshold;
	u8	prox_persistence;
	u8	prox_persistence_counter;
	u8	prox_data;
	u8	prox_threshold;
	u8	prox_threshold_hw;
	bool	prox_force_update;
	u8	prox_abs_thres;
	u8	prox_led;
};

static const char reg_vcc[] = "Vcc";
static const char reg_vleds[] = "Vleds";

/*
 * Supported stand alone rates in ms from chip data sheet
 * {10, 20, 30, 40, 70, 100, 200, 500, 1000, 2000};
 */
static const s16 prox_rates_hz[] = {100, 50, 33, 25, 14, 10, 5, 2};
static const s16 prox_rates_ms[] = {10, 20, 30, 40, 70, 100, 200, 500};

/* Supported IR-led currents in mA */
static const u8 prox_curr_ma[] = {5, 10, 20, 50, 100, 150, 200};

/*
 * Supported stand alone rates in ms from chip data sheet
 * {100, 200, 500, 1000, 2000};
 */
static const s16 lux_rates_hz[] = {10, 5, 2, 1, 0};

/*
 * interrupt control functions are called while keeping chip->mutex
 * excluding module probe / remove
 */
static inline int bh1770_lux_interrupt_control(struct bh1770_chip *chip,
					int lux)
{
	chip->int_mode_lux = lux;
	/* Set interrupt modes, interrupt active low, latched */
	return i2c_smbus_write_byte_data(chip->client,
					BH1770_INTERRUPT,
					(lux << 1) | chip->int_mode_prox);
}

static inline int bh1770_prox_interrupt_control(struct bh1770_chip *chip,
					int ps)
{
	chip->int_mode_prox = ps;
	return i2c_smbus_write_byte_data(chip->client,
					BH1770_INTERRUPT,
					(chip->int_mode_lux << 1) | (ps << 0));
}

/* chip->mutex is always kept here */
static int bh1770_lux_rate(struct bh1770_chip *chip, int rate_index)
{
	/* sysfs may call this when the chip is powered off */
	if (pm_runtime_suspended(&chip->client->dev))
		return 0;

	/* Proper proximity response needs fastest lux rate (100ms) */
	if (chip->prox_enable_count)
		rate_index = 0;

	return i2c_smbus_write_byte_data(chip->client,
					BH1770_ALS_MEAS_RATE,
					rate_index);
}

static int bh1770_prox_rate(struct bh1770_chip *chip, int mode)
{
	int rate;

	rate = (mode == PROX_ABOVE_THRESHOLD) ?
		chip->prox_rate_threshold : chip->prox_rate;

	return i2c_smbus_write_byte_data(chip->client,
					BH1770_PS_MEAS_RATE,
					rate);
}

/* InfraredLED is controlled by the chip during proximity scanning */
static inline int bh1770_led_cfg(struct bh1770_chip *chip)
{
	/* LED cfg, current for leds 1 and 2 */
	return i2c_smbus_write_byte_data(chip->client,
					BH1770_I_LED,
					(BH1770_LED1 << 6) |
					(BH1770_LED_5mA << 3) |
					chip->prox_led);
}

/*
 * Following two functions converts raw ps values from HW to normalized
 * values. Purpose is to compensate differences between different sensor
 * versions and variants so that result means about the same between
 * versions.
 */
static inline u8 bh1770_psraw_to_adjusted(struct bh1770_chip *chip, u8 psraw)
{
	u16 adjusted;
	adjusted = (u16)(((u32)(psraw + chip->prox_const) * chip->prox_coef) /
		BH1770_COEF_SCALER);
	if (adjusted > BH1770_PROX_RANGE)
		adjusted = BH1770_PROX_RANGE;
	return adjusted;
}

static inline u8 bh1770_psadjusted_to_raw(struct bh1770_chip *chip, u8 ps)
{
	u16 raw;

	raw = (((u32)ps * BH1770_COEF_SCALER) / chip->prox_coef);
	if (raw > chip->prox_const)
		raw = raw - chip->prox_const;
	else
		raw = 0;
	return raw;
}

/*
 * Following two functions converts raw lux values from HW to normalized
 * values. Purpose is to compensate differences between different sensor
 * versions and variants so that result means about the same between
 * versions. Chip->mutex is kept when this is called.
 */
static int bh1770_prox_set_threshold(struct bh1770_chip *chip)
{
	u8 tmp = 0;

	/* sysfs may call this when the chip is powered off */
	if (pm_runtime_suspended(&chip->client->dev))
		return 0;

	tmp = bh1770_psadjusted_to_raw(chip, chip->prox_threshold);
	chip->prox_threshold_hw = tmp;

	return	i2c_smbus_write_byte_data(chip->client, BH1770_PS_TH_LED1,
					tmp);
}

static inline u16 bh1770_lux_raw_to_adjusted(struct bh1770_chip *chip, u16 raw)
{
	u32 lux;
	lux = ((u32)raw * chip->lux_corr) / BH1770_LUX_CORR_SCALE;
	return min(lux, (u32)BH1770_LUX_RANGE);
}

static inline u16 bh1770_lux_adjusted_to_raw(struct bh1770_chip *chip,
					u16 adjusted)
{
	return (u32)adjusted * BH1770_LUX_CORR_SCALE / chip->lux_corr;
}

/* chip->mutex is kept when this is called */
static int bh1770_lux_update_thresholds(struct bh1770_chip *chip,
					u16 threshold_hi, u16 threshold_lo)
{
	u8 data[4];
	int ret;

	/* sysfs may call this when the chip is powered off */
	if (pm_runtime_suspended(&chip->client->dev))
		return 0;

	/*
	 * Compensate threshold values with the correction factors if not
	 * set to minimum or maximum.
	 * Min & max values disables interrupts.
	 */
	if (threshold_hi != BH1770_LUX_RANGE && threshold_hi != 0)
		threshold_hi = bh1770_lux_adjusted_to_raw(chip, threshold_hi);

	if (threshold_lo != BH1770_LUX_RANGE && threshold_lo != 0)
		threshold_lo = bh1770_lux_adjusted_to_raw(chip, threshold_lo);

	if (chip->lux_thres_hi_onchip == threshold_hi &&
	    chip->lux_thres_lo_onchip == threshold_lo)
		return 0;

	chip->lux_thres_hi_onchip = threshold_hi;
	chip->lux_thres_lo_onchip = threshold_lo;

	data[0] = threshold_hi;
	data[1] = threshold_hi >> 8;
	data[2] = threshold_lo;
	data[3] = threshold_lo >> 8;

	ret = i2c_smbus_write_i2c_block_data(chip->client,
					BH1770_ALS_TH_UP_0,
					ARRAY_SIZE(data),
					data);
	return ret;
}

static int bh1770_lux_get_result(struct bh1770_chip *chip)
{
	u16 data;
	int ret;

	ret = i2c_smbus_read_byte_data(chip->client, BH1770_ALS_DATA_0);
	if (ret < 0)
		return ret;

	data = ret & 0xff;
	ret = i2c_smbus_read_byte_data(chip->client, BH1770_ALS_DATA_1);
	if (ret < 0)
		return ret;

	chip->lux_data_raw = data | ((ret & 0xff) << 8);

	return 0;
}

/* Calculate correction value which contains chip and device specific parts */
static u32 bh1770_get_corr_value(struct bh1770_chip *chip)
{
	u32 tmp;
	/* Impact of glass attenuation correction */
	tmp = (BH1770_LUX_CORR_SCALE * chip->lux_ga) / BH1770_LUX_GA_SCALE;
	/* Impact of chip factor correction */
	tmp = (tmp * chip->lux_cf) / BH1770_LUX_CF_SCALE;
	/* Impact of Device specific calibration correction */
	tmp = (tmp * chip->lux_calib) / BH1770_CALIB_SCALER;
	return tmp;
}

static int bh1770_lux_read_result(struct bh1770_chip *chip)
{
	bh1770_lux_get_result(chip);
	return bh1770_lux_raw_to_adjusted(chip, chip->lux_data_raw);
}

/*
 * Chip on / off functions are called while keeping mutex except probe
 * or remove phase
 */
static int bh1770_chip_on(struct bh1770_chip *chip)
{
	int ret = regulator_bulk_enable(ARRAY_SIZE(chip->regs),
					chip->regs);
	if (ret < 0)
		return ret;

	usleep_range(BH1770_STARTUP_DELAY, BH1770_STARTUP_DELAY * 2);

	/* Reset the chip */
	i2c_smbus_write_byte_data(chip->client, BH1770_ALS_CONTROL,
				BH1770_SWRESET);
	usleep_range(BH1770_RESET_TIME, BH1770_RESET_TIME * 2);

	/*
	 * ALS is started always since proximity needs als results
	 * for realibility estimation.
	 * Let's assume dark until the first ALS measurement is ready.
	 */
	chip->lux_data_raw = 0;
	chip->prox_data = 0;
	ret = i2c_smbus_write_byte_data(chip->client,
					BH1770_ALS_CONTROL, BH1770_STANDALONE);

	/* Assume reset defaults */
	chip->lux_thres_hi_onchip = BH1770_LUX_RANGE;
	chip->lux_thres_lo_onchip = 0;

	return ret;
}

static void bh1770_chip_off(struct bh1770_chip *chip)
{
	i2c_smbus_write_byte_data(chip->client,
					BH1770_INTERRUPT, BH1770_DISABLE);
	i2c_smbus_write_byte_data(chip->client,
				BH1770_ALS_CONTROL, BH1770_STANDBY);
	i2c_smbus_write_byte_data(chip->client,
				BH1770_PS_CONTROL, BH1770_STANDBY);
	regulator_bulk_disable(ARRAY_SIZE(chip->regs), chip->regs);
}

/* chip->mutex is kept when this is called */
static int bh1770_prox_mode_control(struct bh1770_chip *chip)
{
	if (chip->prox_enable_count) {
		chip->prox_force_update = true; /* Force immediate update */

		bh1770_lux_rate(chip, chip->lux_rate_index);
		bh1770_prox_set_threshold(chip);
		bh1770_led_cfg(chip);
		bh1770_prox_rate(chip, PROX_BELOW_THRESHOLD);
		bh1770_prox_interrupt_control(chip, BH1770_ENABLE);
		i2c_smbus_write_byte_data(chip->client,
					BH1770_PS_CONTROL, BH1770_STANDALONE);
	} else {
		chip->prox_data = 0;
		bh1770_lux_rate(chip, chip->lux_rate_index);
		bh1770_prox_interrupt_control(chip, BH1770_DISABLE);
		i2c_smbus_write_byte_data(chip->client,
					BH1770_PS_CONTROL, BH1770_STANDBY);
	}
	return 0;
}

/* chip->mutex is kept when this is called */
static int bh1770_prox_read_result(struct bh1770_chip *chip)
{
	int ret;
	bool above;
	u8 mode;

	ret = i2c_smbus_read_byte_data(chip->client, BH1770_PS_DATA_LED1);
	if (ret < 0)
		goto out;

	if (ret > chip->prox_threshold_hw)
		above = true;
	else
		above = false;

	/*
	 * when ALS levels goes above limit, proximity result may be
	 * false proximity. Thus ignore the result. With real proximity
	 * there is a shadow causing low als levels.
	 */
	if (chip->lux_data_raw > PROX_IGNORE_LUX_LIMIT)
		ret = 0;

	chip->prox_data = bh1770_psraw_to_adjusted(chip, ret);

	/* Strong proximity level or force mode requires immediate response */
	if (chip->prox_data >= chip->prox_abs_thres ||
	    chip->prox_force_update)
		chip->prox_persistence_counter = chip->prox_persistence;

	chip->prox_force_update = false;

	/* Persistence filttering to reduce false proximity events */
	if (likely(above)) {
		if (chip->prox_persistence_counter < chip->prox_persistence) {
			chip->prox_persistence_counter++;
			ret = -ENODATA;
		} else {
			mode = PROX_ABOVE_THRESHOLD;
			ret = 0;
		}
	} else {
		chip->prox_persistence_counter = 0;
		mode = PROX_BELOW_THRESHOLD;
		chip->prox_data = 0;
		ret = 0;
	}

	/* Set proximity detection rate based on above or below value */
	if (ret == 0) {
		bh1770_prox_rate(chip, mode);
		sysfs_notify(&chip->client->dev.kobj, NULL, "prox0_raw");
	}
out:
	return ret;
}

static int bh1770_detect(struct bh1770_chip *chip)
{
	struct i2c_client *client = chip->client;
	s32 ret;
	u8 manu, part;

	ret = i2c_smbus_read_byte_data(client, BH1770_MANUFACT_ID);
	if (ret < 0)
		goto error;
	manu = (u8)ret;

	ret = i2c_smbus_read_byte_data(client, BH1770_PART_ID);
	if (ret < 0)
		goto error;
	part = (u8)ret;

	chip->revision = (part & BH1770_REV_MASK) >> BH1770_REV_SHIFT;
	chip->prox_coef = BH1770_COEF_SCALER;
	chip->prox_const = 0;
	chip->lux_cf = BH1770_NEUTRAL_CF;

	if ((manu == BH1770_MANUFACT_ROHM) &&
	    ((part & BH1770_PART_MASK) == BH1770_PART)) {
		snprintf(chip->chipname, sizeof(chip->chipname), "BH1770GLC");
		return 0;
	}

	if ((manu == BH1770_MANUFACT_OSRAM) &&
	    ((part & BH1770_PART_MASK) == BH1770_PART)) {
		snprintf(chip->chipname, sizeof(chip->chipname), "SFH7770");
		/* Values selected by comparing different versions */
		chip->prox_coef = 819; /* 0.8 * BH1770_COEF_SCALER */
		chip->prox_const = 40;
		return 0;
	}

	ret = -ENODEV;
error:
	dev_dbg(&client->dev, "BH1770 or SFH7770 not found\n");

	return ret;
}

/*
 * This work is re-scheduled at every proximity interrupt.
 * If this work is running, it means that there hasn't been any
 * proximity interrupt in time. Situation is handled as no-proximity.
 * It would be nice to have low-threshold interrupt or interrupt
 * when measurement and hi-threshold are both 0. But neither of those exists.
 * This is a workaroud for missing HW feature.
 */

static void bh1770_prox_work(struct work_struct *work)
{
	struct bh1770_chip *chip =
		container_of(work, struct bh1770_chip, prox_work.work);

	mutex_lock(&chip->mutex);
	bh1770_prox_read_result(chip);
	mutex_unlock(&chip->mutex);
}

/* This is threaded irq handler */
static irqreturn_t bh1770_irq(int irq, void *data)
{
	struct bh1770_chip *chip = data;
	int status;
	int rate = 0;

	mutex_lock(&chip->mutex);
	status = i2c_smbus_read_byte_data(chip->client, BH1770_ALS_PS_STATUS);

	/* Acknowledge interrupt by reading this register */
	i2c_smbus_read_byte_data(chip->client, BH1770_INTERRUPT);

	/*
	 * Check if there is fresh data available for als.
	 * If this is the very first data, update thresholds after that.
	 */
	if (status & BH1770_INT_ALS_DATA) {
		bh1770_lux_get_result(chip);
		if (unlikely(chip->lux_wait_result)) {
			chip->lux_wait_result = false;
			wake_up(&chip->wait);
			bh1770_lux_update_thresholds(chip,
						chip->lux_threshold_hi,
						chip->lux_threshold_lo);
		}
	}

	/* Disable interrupt logic to guarantee acknowledgement */
	i2c_smbus_write_byte_data(chip->client, BH1770_INTERRUPT,
				  (0 << 1) | (0 << 0));

	if ((status & BH1770_INT_ALS_INT))
		sysfs_notify(&chip->client->dev.kobj, NULL, "lux0_input");

	if (chip->int_mode_prox && (status & BH1770_INT_LEDS_INT)) {
		rate = prox_rates_ms[chip->prox_rate_threshold];
		bh1770_prox_read_result(chip);
	}

	/* Re-enable interrupt logic */
	i2c_smbus_write_byte_data(chip->client, BH1770_INTERRUPT,
				  (chip->int_mode_lux << 1) |
				  (chip->int_mode_prox << 0));
	mutex_unlock(&chip->mutex);

	/*
	 * Can't cancel work while keeping mutex since the work uses the
	 * same mutex.
	 */
	if (rate) {
		/*
		 * Simulate missing no-proximity interrupt 50ms after the
		 * next expected interrupt time.
		 */
		cancel_delayed_work_sync(&chip->prox_work);
		schedule_delayed_work(&chip->prox_work,
				msecs_to_jiffies(rate + 50));
	}
	return IRQ_HANDLED;
}

static ssize_t bh1770_power_state_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;
	ssize_t ret;

	ret = kstrtoul(buf, 0, &value);
	if (ret)
		return ret;

	mutex_lock(&chip->mutex);
	if (value) {
		pm_runtime_get_sync(dev);

		ret = bh1770_lux_rate(chip, chip->lux_rate_index);
		if (ret < 0) {
			pm_runtime_put(dev);
			goto leave;
		}

		ret = bh1770_lux_interrupt_control(chip, BH1770_ENABLE);
		if (ret < 0) {
			pm_runtime_put(dev);
			goto leave;
		}

		/* This causes interrupt after the next measurement cycle */
		bh1770_lux_update_thresholds(chip, BH1770_LUX_DEF_THRES,
					BH1770_LUX_DEF_THRES);
		/* Inform that we are waiting for a result from ALS */
		chip->lux_wait_result = true;
		bh1770_prox_mode_control(chip);
	} else if (!pm_runtime_suspended(dev)) {
		pm_runtime_put(dev);
	}
	ret = count;
leave:
	mutex_unlock(&chip->mutex);
	return ret;
}

static ssize_t bh1770_power_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", !pm_runtime_suspended(dev));
}

static ssize_t bh1770_lux_result_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	ssize_t ret;
	long timeout;

	if (pm_runtime_suspended(dev))
		return -EIO; /* Chip is not enabled at all */

	timeout = wait_event_interruptible_timeout(chip->wait,
					!chip->lux_wait_result,
					msecs_to_jiffies(BH1770_TIMEOUT));
	if (!timeout)
		return -EIO;

	mutex_lock(&chip->mutex);
	ret = sprintf(buf, "%d\n", bh1770_lux_read_result(chip));
	mutex_unlock(&chip->mutex);

	return ret;
}

static ssize_t bh1770_lux_range_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", BH1770_LUX_RANGE);
}

static ssize_t bh1770_prox_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 0, &value);
	if (ret)
		return ret;

	mutex_lock(&chip->mutex);
	/* Assume no proximity. Sensor will tell real state soon */
	if (!chip->prox_enable_count)
		chip->prox_data = 0;

	if (value)
		chip->prox_enable_count++;
	else if (chip->prox_enable_count > 0)
		chip->prox_enable_count--;
	else
		goto leave;

	/* Run control only when chip is powered on */
	if (!pm_runtime_suspended(dev))
		bh1770_prox_mode_control(chip);
leave:
	mutex_unlock(&chip->mutex);
	return count;
}

static ssize_t bh1770_prox_enable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	ssize_t len;

	mutex_lock(&chip->mutex);
	len = sprintf(buf, "%d\n", chip->prox_enable_count);
	mutex_unlock(&chip->mutex);
	return len;
}

static ssize_t bh1770_prox_result_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&chip->mutex);
	if (chip->prox_enable_count && !pm_runtime_suspended(dev))
		ret = sprintf(buf, "%d\n", chip->prox_data);
	else
		ret = -EIO;
	mutex_unlock(&chip->mutex);
	return ret;
}

static ssize_t bh1770_prox_range_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", BH1770_PROX_RANGE);
}

static ssize_t bh1770_get_prox_rate_avail(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int i;
	int pos = 0;
	for (i = 0; i < ARRAY_SIZE(prox_rates_hz); i++)
		pos += sprintf(buf + pos, "%d ", prox_rates_hz[i]);
	sprintf(buf + pos - 1, "\n");
	return pos;
}

static ssize_t bh1770_get_prox_rate_above(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", prox_rates_hz[chip->prox_rate_threshold]);
}

static ssize_t bh1770_get_prox_rate_below(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", prox_rates_hz[chip->prox_rate]);
}

static int bh1770_prox_rate_validate(int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(prox_rates_hz) - 1; i++)
		if (rate >= prox_rates_hz[i])
			break;
	return i;
}

static ssize_t bh1770_set_prox_rate_above(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 0, &value);
	if (ret)
		return ret;

	mutex_lock(&chip->mutex);
	chip->prox_rate_threshold = bh1770_prox_rate_validate(value);
	mutex_unlock(&chip->mutex);
	return count;
}

static ssize_t bh1770_set_prox_rate_below(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 0, &value);
	if (ret)
		return ret;

	mutex_lock(&chip->mutex);
	chip->prox_rate = bh1770_prox_rate_validate(value);
	mutex_unlock(&chip->mutex);
	return count;
}

static ssize_t bh1770_get_prox_thres(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->prox_threshold);
}

static ssize_t bh1770_set_prox_thres(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 0, &value);
	if (ret)
		return ret;

	if (value > BH1770_PROX_RANGE)
		return -EINVAL;

	mutex_lock(&chip->mutex);
	chip->prox_threshold = value;
	ret = bh1770_prox_set_threshold(chip);
	mutex_unlock(&chip->mutex);
	if (ret < 0)
		return ret;
	return count;
}

static ssize_t bh1770_prox_persistence_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct bh1770_chip *chip = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", chip->prox_persistence);
}

static ssize_t bh1770_prox_persistence_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct bh1770_chip *chip = dev_get_drvdata(dev);
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 0, &value);
	if (ret)
		return ret;

	if (value > BH1770_PROX_MAX_PERSISTENCE)
		return -EINVAL;

	chip->prox_persistence = value;

	return len;
}

static ssize_t bh1770_prox_abs_thres_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct bh1770_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", chip->prox_abs_thres);
}

static ssize_t bh1770_prox_abs_thres_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct bh1770_chip *chip = dev_get_drvdata(dev);
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 0, &value);
	if (ret)
		return ret;

	if (value > BH1770_PROX_RANGE)
		return -EINVAL;

	chip->prox_abs_thres = value;

	return len;
}

static ssize_t bh1770_chip_id_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%s rev %d\n", chip->chipname, chip->revision);
}

static ssize_t bh1770_lux_calib_default_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", BH1770_CALIB_SCALER);
}

static ssize_t bh1770_lux_calib_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct bh1770_chip *chip = dev_get_drvdata(dev);
	ssize_t len;

	mutex_lock(&chip->mutex);
	len = sprintf(buf, "%u\n", chip->lux_calib);
	mutex_unlock(&chip->mutex);
	return len;
}

static ssize_t bh1770_lux_calib_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct bh1770_chip *chip = dev_get_drvdata(dev);
	unsigned long value;
	u32 old_calib;
	u32 new_corr;
	int ret;

	ret = kstrtoul(buf, 0, &value);
	if (ret)
		return ret;

	mutex_lock(&chip->mutex);
	old_calib = chip->lux_calib;
	chip->lux_calib = value;
	new_corr = bh1770_get_corr_value(chip);
	if (new_corr == 0) {
		chip->lux_calib = old_calib;
		mutex_unlock(&chip->mutex);
		return -EINVAL;
	}
	chip->lux_corr = new_corr;
	/* Refresh thresholds on HW after changing correction value */
	bh1770_lux_update_thresholds(chip, chip->lux_threshold_hi,
				chip->lux_threshold_lo);

	mutex_unlock(&chip->mutex);

	return len;
}

static ssize_t bh1770_get_lux_rate_avail(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int i;
	int pos = 0;
	for (i = 0; i < ARRAY_SIZE(lux_rates_hz); i++)
		pos += sprintf(buf + pos, "%d ", lux_rates_hz[i]);
	sprintf(buf + pos - 1, "\n");
	return pos;
}

static ssize_t bh1770_get_lux_rate(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", lux_rates_hz[chip->lux_rate_index]);
}

static ssize_t bh1770_set_lux_rate(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	unsigned long rate_hz;
	int ret, i;

	ret = kstrtoul(buf, 0, &rate_hz);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(lux_rates_hz) - 1; i++)
		if (rate_hz >= lux_rates_hz[i])
			break;

	mutex_lock(&chip->mutex);
	chip->lux_rate_index = i;
	ret = bh1770_lux_rate(chip, i);
	mutex_unlock(&chip->mutex);

	if (ret < 0)
		return ret;

	return count;
}

static ssize_t bh1770_get_lux_thresh_above(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->lux_threshold_hi);
}

static ssize_t bh1770_get_lux_thresh_below(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->lux_threshold_lo);
}

static ssize_t bh1770_set_lux_thresh(struct bh1770_chip *chip, u16 *target,
				const char *buf)
{
	unsigned long thresh;
	int ret;

	ret = kstrtoul(buf, 0, &thresh);
	if (ret)
		return ret;

	if (thresh > BH1770_LUX_RANGE)
		return -EINVAL;

	mutex_lock(&chip->mutex);
	*target = thresh;
	/*
	 * Don't update values in HW if we are still waiting for
	 * first interrupt to come after device handle open call.
	 */
	if (!chip->lux_wait_result)
		ret = bh1770_lux_update_thresholds(chip,
						chip->lux_threshold_hi,
						chip->lux_threshold_lo);
	mutex_unlock(&chip->mutex);
	return ret;

}

static ssize_t bh1770_set_lux_thresh_above(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	int ret = bh1770_set_lux_thresh(chip, &chip->lux_threshold_hi, buf);
	if (ret < 0)
		return ret;
	return len;
}

static ssize_t bh1770_set_lux_thresh_below(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct bh1770_chip *chip =  dev_get_drvdata(dev);
	int ret = bh1770_set_lux_thresh(chip, &chip->lux_threshold_lo, buf);
	if (ret < 0)
		return ret;
	return len;
}

static DEVICE_ATTR(prox0_raw_en, S_IRUGO | S_IWUSR, bh1770_prox_enable_show,
						bh1770_prox_enable_store);
static DEVICE_ATTR(prox0_thresh_above1_value, S_IRUGO | S_IWUSR,
						bh1770_prox_abs_thres_show,
						bh1770_prox_abs_thres_store);
static DEVICE_ATTR(prox0_thresh_above0_value, S_IRUGO | S_IWUSR,
						bh1770_get_prox_thres,
						bh1770_set_prox_thres);
static DEVICE_ATTR(prox0_raw, S_IRUGO, bh1770_prox_result_show, NULL);
static DEVICE_ATTR(prox0_sensor_range, S_IRUGO, bh1770_prox_range_show, NULL);
static DEVICE_ATTR(prox0_thresh_above_count, S_IRUGO | S_IWUSR,
						bh1770_prox_persistence_show,
						bh1770_prox_persistence_store);
static DEVICE_ATTR(prox0_rate_above, S_IRUGO | S_IWUSR,
						bh1770_get_prox_rate_above,
						bh1770_set_prox_rate_above);
static DEVICE_ATTR(prox0_rate_below, S_IRUGO | S_IWUSR,
						bh1770_get_prox_rate_below,
						bh1770_set_prox_rate_below);
static DEVICE_ATTR(prox0_rate_avail, S_IRUGO, bh1770_get_prox_rate_avail, NULL);

static DEVICE_ATTR(lux0_calibscale, S_IRUGO | S_IWUSR, bh1770_lux_calib_show,
						bh1770_lux_calib_store);
static DEVICE_ATTR(lux0_calibscale_default, S_IRUGO,
						bh1770_lux_calib_default_show,
						NULL);
static DEVICE_ATTR(lux0_input, S_IRUGO, bh1770_lux_result_show, NULL);
static DEVICE_ATTR(lux0_sensor_range, S_IRUGO, bh1770_lux_range_show, NULL);
static DEVICE_ATTR(lux0_rate, S_IRUGO | S_IWUSR, bh1770_get_lux_rate,
						bh1770_set_lux_rate);
static DEVICE_ATTR(lux0_rate_avail, S_IRUGO, bh1770_get_lux_rate_avail, NULL);
static DEVICE_ATTR(lux0_thresh_above_value, S_IRUGO | S_IWUSR,
						bh1770_get_lux_thresh_above,
						bh1770_set_lux_thresh_above);
static DEVICE_ATTR(lux0_thresh_below_value, S_IRUGO | S_IWUSR,
						bh1770_get_lux_thresh_below,
						bh1770_set_lux_thresh_below);
static DEVICE_ATTR(chip_id, S_IRUGO, bh1770_chip_id_show, NULL);
static DEVICE_ATTR(power_state, S_IRUGO | S_IWUSR, bh1770_power_state_show,
						 bh1770_power_state_store);


static struct attribute *sysfs_attrs[] = {
	&dev_attr_lux0_calibscale.attr,
	&dev_attr_lux0_calibscale_default.attr,
	&dev_attr_lux0_input.attr,
	&dev_attr_lux0_sensor_range.attr,
	&dev_attr_lux0_rate.attr,
	&dev_attr_lux0_rate_avail.attr,
	&dev_attr_lux0_thresh_above_value.attr,
	&dev_attr_lux0_thresh_below_value.attr,
	&dev_attr_prox0_raw.attr,
	&dev_attr_prox0_sensor_range.attr,
	&dev_attr_prox0_raw_en.attr,
	&dev_attr_prox0_thresh_above_count.attr,
	&dev_attr_prox0_rate_above.attr,
	&dev_attr_prox0_rate_below.attr,
	&dev_attr_prox0_rate_avail.attr,
	&dev_attr_prox0_thresh_above0_value.attr,
	&dev_attr_prox0_thresh_above1_value.attr,
	&dev_attr_chip_id.attr,
	&dev_attr_power_state.attr,
	NULL
};

static struct attribute_group bh1770_attribute_group = {
	.attrs = sysfs_attrs
};

static int bh1770_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct bh1770_chip *chip;
	int err;

	chip = devm_kzalloc(&client->dev, sizeof *chip, GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);
	chip->client  = client;

	mutex_init(&chip->mutex);
	init_waitqueue_head(&chip->wait);
	INIT_DELAYED_WORK(&chip->prox_work, bh1770_prox_work);

	if (client->dev.platform_data == NULL) {
		dev_err(&client->dev, "platform data is mandatory\n");
		return -EINVAL;
	}

	chip->pdata		= client->dev.platform_data;
	chip->lux_calib		= BH1770_LUX_NEUTRAL_CALIB_VALUE;
	chip->lux_rate_index	= BH1770_LUX_DEFAULT_RATE;
	chip->lux_threshold_lo	= BH1770_LUX_DEF_THRES;
	chip->lux_threshold_hi	= BH1770_LUX_DEF_THRES;

	if (chip->pdata->glass_attenuation == 0)
		chip->lux_ga = BH1770_NEUTRAL_GA;
	else
		chip->lux_ga = chip->pdata->glass_attenuation;

	chip->prox_threshold	= BH1770_PROX_DEF_THRES;
	chip->prox_led		= chip->pdata->led_def_curr;
	chip->prox_abs_thres	= BH1770_PROX_DEF_ABS_THRES;
	chip->prox_persistence	= BH1770_DEFAULT_PERSISTENCE;
	chip->prox_rate_threshold = BH1770_PROX_DEF_RATE_THRESH;
	chip->prox_rate		= BH1770_PROX_DEFAULT_RATE;
	chip->prox_data		= 0;

	chip->regs[0].supply = reg_vcc;
	chip->regs[1].supply = reg_vleds;

	err = devm_regulator_bulk_get(&client->dev,
				      ARRAY_SIZE(chip->regs), chip->regs);
	if (err < 0) {
		dev_err(&client->dev, "Cannot get regulators\n");
		return err;
	}

	err = regulator_bulk_enable(ARRAY_SIZE(chip->regs),
				chip->regs);
	if (err < 0) {
		dev_err(&client->dev, "Cannot enable regulators\n");
		return err;
	}

	usleep_range(BH1770_STARTUP_DELAY, BH1770_STARTUP_DELAY * 2);
	err = bh1770_detect(chip);
	if (err < 0)
		goto fail0;

	/* Start chip */
	bh1770_chip_on(chip);
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);

	chip->lux_corr = bh1770_get_corr_value(chip);
	if (chip->lux_corr == 0) {
		dev_err(&client->dev, "Improper correction values\n");
		err = -EINVAL;
		goto fail0;
	}

	if (chip->pdata->setup_resources) {
		err = chip->pdata->setup_resources();
		if (err) {
			err = -EINVAL;
			goto fail0;
		}
	}

	err = sysfs_create_group(&chip->client->dev.kobj,
				&bh1770_attribute_group);
	if (err < 0) {
		dev_err(&chip->client->dev, "Sysfs registration failed\n");
		goto fail1;
	}

	/*
	 * Chip needs level triggered interrupt to work. However,
	 * level triggering doesn't work always correctly with power
	 * management. Select both
	 */
	err = request_threaded_irq(client->irq, NULL,
				bh1770_irq,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT |
				IRQF_TRIGGER_LOW,
				"bh1770", chip);
	if (err) {
		dev_err(&client->dev, "could not get IRQ %d\n",
			client->irq);
		goto fail2;
	}
	regulator_bulk_disable(ARRAY_SIZE(chip->regs), chip->regs);
	return err;
fail2:
	sysfs_remove_group(&chip->client->dev.kobj,
			&bh1770_attribute_group);
fail1:
	if (chip->pdata->release_resources)
		chip->pdata->release_resources();
fail0:
	regulator_bulk_disable(ARRAY_SIZE(chip->regs), chip->regs);
	return err;
}

static int bh1770_remove(struct i2c_client *client)
{
	struct bh1770_chip *chip = i2c_get_clientdata(client);

	free_irq(client->irq, chip);

	sysfs_remove_group(&chip->client->dev.kobj,
			&bh1770_attribute_group);

	if (chip->pdata->release_resources)
		chip->pdata->release_resources();

	cancel_delayed_work_sync(&chip->prox_work);

	if (!pm_runtime_suspended(&client->dev))
		bh1770_chip_off(chip);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int bh1770_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct bh1770_chip *chip = i2c_get_clientdata(client);

	bh1770_chip_off(chip);

	return 0;
}

static int bh1770_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct bh1770_chip *chip = i2c_get_clientdata(client);
	int ret = 0;

	bh1770_chip_on(chip);

	if (!pm_runtime_suspended(dev)) {
		/*
		 * If we were enabled at suspend time, it is expected
		 * everything works nice and smoothly
		 */
		ret = bh1770_lux_rate(chip, chip->lux_rate_index);
		ret |= bh1770_lux_interrupt_control(chip, BH1770_ENABLE);

		/* This causes interrupt after the next measurement cycle */
		bh1770_lux_update_thresholds(chip, BH1770_LUX_DEF_THRES,
					BH1770_LUX_DEF_THRES);
		/* Inform that we are waiting for a result from ALS */
		chip->lux_wait_result = true;
		bh1770_prox_mode_control(chip);
	}
	return ret;
}
#endif

#ifdef CONFIG_PM
static int bh1770_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct bh1770_chip *chip = i2c_get_clientdata(client);

	bh1770_chip_off(chip);

	return 0;
}

static int bh1770_runtime_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct bh1770_chip *chip = i2c_get_clientdata(client);

	bh1770_chip_on(chip);

	return 0;
}
#endif

static const struct i2c_device_id bh1770_id[] = {
	{"bh1770glc", 0 },
	{"sfh7770", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, bh1770_id);

static const struct dev_pm_ops bh1770_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(bh1770_suspend, bh1770_resume)
	SET_RUNTIME_PM_OPS(bh1770_runtime_suspend, bh1770_runtime_resume, NULL)
};

static struct i2c_driver bh1770_driver = {
	.driver	 = {
		.name	= "bh1770glc",
		.owner	= THIS_MODULE,
		.pm	= &bh1770_pm_ops,
	},
	.probe	  = bh1770_probe,
	.remove	  = bh1770_remove,
	.id_table = bh1770_id,
};

module_i2c_driver(bh1770_driver);

MODULE_DESCRIPTION("BH1770GLC / SFH7770 combined ALS and proximity sensor");
MODULE_AUTHOR("Samu Onkalo, Nokia Corporation");
MODULE_LICENSE("GPL v2");
