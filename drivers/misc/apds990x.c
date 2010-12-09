/*
 * This file is part of the APDS990x sensor driver.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
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
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/i2c/apds990x.h>

/* Register map */
#define APDS990X_ENABLE	 0x00 /* Enable of states and interrupts */
#define APDS990X_ATIME	 0x01 /* ALS ADC time  */
#define APDS990X_PTIME	 0x02 /* Proximity ADC time  */
#define APDS990X_WTIME	 0x03 /* Wait time  */
#define APDS990X_AILTL	 0x04 /* ALS interrupt low threshold low byte */
#define APDS990X_AILTH	 0x05 /* ALS interrupt low threshold hi byte */
#define APDS990X_AIHTL	 0x06 /* ALS interrupt hi threshold low byte */
#define APDS990X_AIHTH	 0x07 /* ALS interrupt hi threshold hi byte */
#define APDS990X_PILTL	 0x08 /* Proximity interrupt low threshold low byte */
#define APDS990X_PILTH	 0x09 /* Proximity interrupt low threshold hi byte */
#define APDS990X_PIHTL	 0x0a /* Proximity interrupt hi threshold low byte */
#define APDS990X_PIHTH	 0x0b /* Proximity interrupt hi threshold hi byte */
#define APDS990X_PERS	 0x0c /* Interrupt persistence filters */
#define APDS990X_CONFIG	 0x0d /* Configuration */
#define APDS990X_PPCOUNT 0x0e /* Proximity pulse count */
#define APDS990X_CONTROL 0x0f /* Gain control register */
#define APDS990X_REV	 0x11 /* Revision Number */
#define APDS990X_ID	 0x12 /* Device ID */
#define APDS990X_STATUS	 0x13 /* Device status */
#define APDS990X_CDATAL	 0x14 /* Clear ADC low data register */
#define APDS990X_CDATAH	 0x15 /* Clear ADC high data register */
#define APDS990X_IRDATAL 0x16 /* IR ADC low data register */
#define APDS990X_IRDATAH 0x17 /* IR ADC high data register */
#define APDS990X_PDATAL	 0x18 /* Proximity ADC low data register */
#define APDS990X_PDATAH	 0x19 /* Proximity ADC high data register */

/* Control */
#define APDS990X_MAX_AGAIN	3

/* Enable register */
#define APDS990X_EN_PIEN	(0x1 << 5)
#define APDS990X_EN_AIEN	(0x1 << 4)
#define APDS990X_EN_WEN		(0x1 << 3)
#define APDS990X_EN_PEN		(0x1 << 2)
#define APDS990X_EN_AEN		(0x1 << 1)
#define APDS990X_EN_PON		(0x1 << 0)
#define APDS990X_EN_DISABLE_ALL 0

/* Status register */
#define APDS990X_ST_PINT	(0x1 << 5)
#define APDS990X_ST_AINT	(0x1 << 4)

/* I2C access types */
#define APDS990x_CMD_TYPE_MASK	(0x03 << 5)
#define APDS990x_CMD_TYPE_RB	(0x00 << 5) /* Repeated byte */
#define APDS990x_CMD_TYPE_INC	(0x01 << 5) /* Auto increment */
#define APDS990x_CMD_TYPE_SPE	(0x03 << 5) /* Special function */

#define APDS990x_ADDR_SHIFT	0
#define APDS990x_CMD		0x80

/* Interrupt ack commands */
#define APDS990X_INT_ACK_ALS	0x6
#define APDS990X_INT_ACK_PS	0x5
#define APDS990X_INT_ACK_BOTH	0x7

/* ptime */
#define APDS990X_PTIME_DEFAULT	0xff /* Recommended conversion time 2.7ms*/

/* wtime */
#define APDS990X_WTIME_DEFAULT	0xee /* ~50ms wait time */

#define APDS990X_TIME_TO_ADC	1024 /* One timetick as ADC count value */

/* Persistence */
#define APDS990X_APERS_SHIFT	0
#define APDS990X_PPERS_SHIFT	4

/* Supported ID:s */
#define APDS990X_ID_0		0x0
#define APDS990X_ID_4		0x4
#define APDS990X_ID_29		0x29

/* pgain and pdiode settings */
#define APDS_PGAIN_1X	       0x0
#define APDS_PDIODE_IR	       0x2

#define APDS990X_LUX_OUTPUT_SCALE 10

/* Reverse chip factors for threshold calculation */
struct reverse_factors {
	u32 afactor;
	int cf1;
	int irf1;
	int cf2;
	int irf2;
};

struct apds990x_chip {
	struct apds990x_platform_data	*pdata;
	struct i2c_client		*client;
	struct mutex			mutex; /* avoid parallel access */
	struct regulator_bulk_data	regs[2];
	wait_queue_head_t		wait;

	int	prox_en;
	bool	prox_continuous_mode;
	bool	lux_wait_fresh_res;

	/* Chip parameters */
	struct	apds990x_chip_factors	cf;
	struct	reverse_factors		rcf;
	u16	atime;		/* als integration time */
	u16	arate;		/* als reporting rate */
	u16	a_max_result;	/* Max possible ADC value with current atime */
	u8	again_meas;	/* Gain used in last measurement */
	u8	again_next;	/* Next calculated gain */
	u8	pgain;
	u8	pdiode;
	u8	pdrive;
	u8	lux_persistence;
	u8	prox_persistence;

	u32	lux_raw;
	u32	lux;
	u16	lux_clear;
	u16	lux_ir;
	u16	lux_calib;
	u32	lux_thres_hi;
	u32	lux_thres_lo;

	u32	prox_thres;
	u16	prox_data;
	u16	prox_calib;

	char	chipname[10];
	u8	revision;
};

#define APDS_CALIB_SCALER		8192
#define APDS_LUX_NEUTRAL_CALIB_VALUE	(1 * APDS_CALIB_SCALER)
#define APDS_PROX_NEUTRAL_CALIB_VALUE	(1 * APDS_CALIB_SCALER)

#define APDS_PROX_DEF_THRES		600
#define APDS_PROX_HYSTERESIS		50
#define APDS_LUX_DEF_THRES_HI		101
#define APDS_LUX_DEF_THRES_LO		100
#define APDS_DEFAULT_PROX_PERS		1

#define APDS_TIMEOUT			2000
#define APDS_STARTUP_DELAY		25000 /* us */
#define APDS_RANGE			65535
#define APDS_PROX_RANGE			1023
#define APDS_LUX_GAIN_LO_LIMIT		100
#define APDS_LUX_GAIN_LO_LIMIT_STRICT	25

#define TIMESTEP			87 /* 2.7ms is about 87 / 32 */
#define TIME_STEP_SCALER		32

#define APDS_LUX_AVERAGING_TIME		50 /* tolerates 50/60Hz ripple */
#define APDS_LUX_DEFAULT_RATE		200

static const u8 again[]	= {1, 8, 16, 120}; /* ALS gain steps */
static const u8 ir_currents[]	= {100, 50, 25, 12}; /* IRled currents in mA */

/* Following two tables must match i.e 10Hz rate means 1 as persistence value */
static const u16 arates_hz[] = {10, 5, 2, 1};
static const u8 apersis[] = {1, 2, 4, 5};

/* Regulators */
static const char reg_vcc[] = "Vdd";
static const char reg_vled[] = "Vled";

static int apds990x_read_byte(struct apds990x_chip *chip, u8 reg, u8 *data)
{
	struct i2c_client *client = chip->client;
	s32 ret;

	reg &= ~APDS990x_CMD_TYPE_MASK;
	reg |= APDS990x_CMD | APDS990x_CMD_TYPE_RB;

	ret = i2c_smbus_read_byte_data(client, reg);
	*data = ret;
	return (int)ret;
}

static int apds990x_read_word(struct apds990x_chip *chip, u8 reg, u16 *data)
{
	struct i2c_client *client = chip->client;
	s32 ret;

	reg &= ~APDS990x_CMD_TYPE_MASK;
	reg |= APDS990x_CMD | APDS990x_CMD_TYPE_INC;

	ret = i2c_smbus_read_word_data(client, reg);
	*data = ret;
	return (int)ret;
}

static int apds990x_write_byte(struct apds990x_chip *chip, u8 reg, u8 data)
{
	struct i2c_client *client = chip->client;
	s32 ret;

	reg &= ~APDS990x_CMD_TYPE_MASK;
	reg |= APDS990x_CMD | APDS990x_CMD_TYPE_RB;

	ret = i2c_smbus_write_byte_data(client, reg, data);
	return (int)ret;
}

static int apds990x_write_word(struct apds990x_chip *chip, u8 reg, u16 data)
{
	struct i2c_client *client = chip->client;
	s32 ret;

	reg &= ~APDS990x_CMD_TYPE_MASK;
	reg |= APDS990x_CMD | APDS990x_CMD_TYPE_INC;

	ret = i2c_smbus_write_word_data(client, reg, data);
	return (int)ret;
}

static int apds990x_mode_on(struct apds990x_chip *chip)
{
	/* ALS is mandatory, proximity optional */
	u8 reg = APDS990X_EN_AIEN | APDS990X_EN_PON | APDS990X_EN_AEN |
		APDS990X_EN_WEN;

	if (chip->prox_en)
		reg |= APDS990X_EN_PIEN | APDS990X_EN_PEN;

	return apds990x_write_byte(chip, APDS990X_ENABLE, reg);
}

static u16 apds990x_lux_to_threshold(struct apds990x_chip *chip, u32 lux)
{
	u32 thres;
	u32 cpl;
	u32 ir;

	if (lux == 0)
		return 0;
	else if (lux == APDS_RANGE)
		return APDS_RANGE;

	/*
	 * Reported LUX value is a combination of the IR and CLEAR channel
	 * values. However, interrupt threshold is only for clear channel.
	 * This function approximates needed HW threshold value for a given
	 * LUX value in the current lightning type.
	 * IR level compared to visible light varies heavily depending on the
	 * source of the light
	 *
	 * Calculate threshold value for the next measurement period.
	 * Math: threshold = lux * cpl where
	 * cpl = atime * again / (glass_attenuation * device_factor)
	 * (count-per-lux)
	 *
	 * First remove calibration. Division by four is to avoid overflow
	 */
	lux = lux * (APDS_CALIB_SCALER / 4) / (chip->lux_calib / 4);

	/* Multiplication by 64 is to increase accuracy */
	cpl = ((u32)chip->atime * (u32)again[chip->again_next] *
		APDS_PARAM_SCALE * 64) / (chip->cf.ga * chip->cf.df);

	thres = lux * cpl / 64;
	/*
	 * Convert IR light from the latest result to match with
	 * new gain step. This helps to adapt with the current
	 * source of light.
	 */
	ir = (u32)chip->lux_ir * (u32)again[chip->again_next] /
		(u32)again[chip->again_meas];

	/*
	 * Compensate count with IR light impact
	 * IAC1 > IAC2 (see apds990x_get_lux for formulas)
	 */
	if (chip->lux_clear * APDS_PARAM_SCALE >=
		chip->rcf.afactor * chip->lux_ir)
		thres = (chip->rcf.cf1 * thres + chip->rcf.irf1 * ir) /
			APDS_PARAM_SCALE;
	else
		thres = (chip->rcf.cf2 * thres + chip->rcf.irf2 * ir) /
			APDS_PARAM_SCALE;

	if (thres >= chip->a_max_result)
		thres = chip->a_max_result - 1;
	return thres;
}

static inline int apds990x_set_atime(struct apds990x_chip *chip, u32 time_ms)
{
	u8 reg_value;

	chip->atime = time_ms;
	/* Formula is specified in the data sheet */
	reg_value = 256 - ((time_ms * TIME_STEP_SCALER) / TIMESTEP);
	/* Calculate max ADC value for given integration time */
	chip->a_max_result = (u16)(256 - reg_value) * APDS990X_TIME_TO_ADC;
	return apds990x_write_byte(chip, APDS990X_ATIME, reg_value);
}

/* Called always with mutex locked */
static int apds990x_refresh_pthres(struct apds990x_chip *chip, int data)
{
	int ret, lo, hi;

	/* If the chip is not in use, don't try to access it */
	if (pm_runtime_suspended(&chip->client->dev))
		return 0;

	if (data < chip->prox_thres) {
		lo = 0;
		hi = chip->prox_thres;
	} else {
		lo = chip->prox_thres - APDS_PROX_HYSTERESIS;
		if (chip->prox_continuous_mode)
			hi = chip->prox_thres;
		else
			hi = APDS_RANGE;
	}

	ret = apds990x_write_word(chip, APDS990X_PILTL, lo);
	ret |= apds990x_write_word(chip, APDS990X_PIHTL, hi);
	return ret;
}

/* Called always with mutex locked */
static int apds990x_refresh_athres(struct apds990x_chip *chip)
{
	int ret;
	/* If the chip is not in use, don't try to access it */
	if (pm_runtime_suspended(&chip->client->dev))
		return 0;

	ret = apds990x_write_word(chip, APDS990X_AILTL,
			apds990x_lux_to_threshold(chip, chip->lux_thres_lo));
	ret |= apds990x_write_word(chip, APDS990X_AIHTL,
			apds990x_lux_to_threshold(chip, chip->lux_thres_hi));

	return ret;
}

/* Called always with mutex locked */
static void apds990x_force_a_refresh(struct apds990x_chip *chip)
{
	/* This will force ALS interrupt after the next measurement. */
	apds990x_write_word(chip, APDS990X_AILTL, APDS_LUX_DEF_THRES_LO);
	apds990x_write_word(chip, APDS990X_AIHTL, APDS_LUX_DEF_THRES_HI);
}

/* Called always with mutex locked */
static void apds990x_force_p_refresh(struct apds990x_chip *chip)
{
	/* This will force proximity interrupt after the next measurement. */
	apds990x_write_word(chip, APDS990X_PILTL, APDS_PROX_DEF_THRES - 1);
	apds990x_write_word(chip, APDS990X_PIHTL, APDS_PROX_DEF_THRES);
}

/* Called always with mutex locked */
static int apds990x_calc_again(struct apds990x_chip *chip)
{
	int curr_again = chip->again_meas;
	int next_again = chip->again_meas;
	int ret = 0;

	/* Calculate suitable als gain */
	if (chip->lux_clear == chip->a_max_result)
		next_again -= 2; /* ALS saturated. Decrease gain by 2 steps */
	else if (chip->lux_clear > chip->a_max_result / 2)
		next_again--;
	else if (chip->lux_clear < APDS_LUX_GAIN_LO_LIMIT_STRICT)
		next_again += 2; /* Too dark. Increase gain by 2 steps */
	else if (chip->lux_clear < APDS_LUX_GAIN_LO_LIMIT)
		next_again++;

	/* Limit gain to available range */
	if (next_again < 0)
		next_again = 0;
	else if (next_again > APDS990X_MAX_AGAIN)
		next_again = APDS990X_MAX_AGAIN;

	/* Let's check can we trust the measured result */
	if (chip->lux_clear == chip->a_max_result)
		/* Result can be totally garbage due to saturation */
		ret = -ERANGE;
	else if (next_again != curr_again &&
		chip->lux_clear < APDS_LUX_GAIN_LO_LIMIT_STRICT)
		/*
		 * Gain is changed and measurement result is very small.
		 * Result can be totally garbage due to underflow
		 */
		ret = -ERANGE;

	chip->again_next = next_again;
	apds990x_write_byte(chip, APDS990X_CONTROL,
			(chip->pdrive << 6) |
			(chip->pdiode << 4) |
			(chip->pgain << 2) |
			(chip->again_next << 0));

	/*
	 * Error means bad result -> re-measurement is needed. The forced
	 * refresh uses fastest possible persistence setting to get result
	 * as soon as possible.
	 */
	if (ret < 0)
		apds990x_force_a_refresh(chip);
	else
		apds990x_refresh_athres(chip);

	return ret;
}

/* Called always with mutex locked */
static int apds990x_get_lux(struct apds990x_chip *chip, int clear, int ir)
{
	int iac, iac1, iac2; /* IR adjusted counts */
	u32 lpc; /* Lux per count */

	/* Formulas:
	 * iac1 = CF1 * CLEAR_CH - IRF1 * IR_CH
	 * iac2 = CF2 * CLEAR_CH - IRF2 * IR_CH
	 */
	iac1 = (chip->cf.cf1 * clear - chip->cf.irf1 * ir) / APDS_PARAM_SCALE;
	iac2 = (chip->cf.cf2 * clear - chip->cf.irf2 * ir) / APDS_PARAM_SCALE;

	iac = max(iac1, iac2);
	iac = max(iac, 0);

	lpc = APDS990X_LUX_OUTPUT_SCALE * (chip->cf.df * chip->cf.ga) /
		(u32)(again[chip->again_meas] * (u32)chip->atime);

	return (iac * lpc) / APDS_PARAM_SCALE;
}

static int apds990x_ack_int(struct apds990x_chip *chip, u8 mode)
{
	struct i2c_client *client = chip->client;
	s32 ret;
	u8 reg = APDS990x_CMD | APDS990x_CMD_TYPE_SPE;

	switch (mode & (APDS990X_ST_AINT | APDS990X_ST_PINT)) {
	case APDS990X_ST_AINT:
		reg |= APDS990X_INT_ACK_ALS;
		break;
	case APDS990X_ST_PINT:
		reg |= APDS990X_INT_ACK_PS;
		break;
	default:
		reg |= APDS990X_INT_ACK_BOTH;
		break;
	}

	ret = i2c_smbus_read_byte_data(client, reg);
	return (int)ret;
}

static irqreturn_t apds990x_irq(int irq, void *data)
{
	struct apds990x_chip *chip = data;
	u8 status;

	apds990x_read_byte(chip, APDS990X_STATUS, &status);
	apds990x_ack_int(chip, status);

	mutex_lock(&chip->mutex);
	if (!pm_runtime_suspended(&chip->client->dev)) {
		if (status & APDS990X_ST_AINT) {
			apds990x_read_word(chip, APDS990X_CDATAL,
					&chip->lux_clear);
			apds990x_read_word(chip, APDS990X_IRDATAL,
					&chip->lux_ir);
			/* Store used gain for calculations */
			chip->again_meas = chip->again_next;

			chip->lux_raw = apds990x_get_lux(chip,
							chip->lux_clear,
							chip->lux_ir);

			if (apds990x_calc_again(chip) == 0) {
				/* Result is valid */
				chip->lux = chip->lux_raw;
				chip->lux_wait_fresh_res = false;
				wake_up(&chip->wait);
				sysfs_notify(&chip->client->dev.kobj,
					NULL, "lux0_input");
			}
		}

		if ((status & APDS990X_ST_PINT) && chip->prox_en) {
			u16 clr_ch;

			apds990x_read_word(chip, APDS990X_CDATAL, &clr_ch);
			/*
			 * If ALS channel is saturated at min gain,
			 * proximity gives false posivite values.
			 * Just ignore them.
			 */
			if (chip->again_meas == 0 &&
				clr_ch == chip->a_max_result)
				chip->prox_data = 0;
			else
				apds990x_read_word(chip,
						APDS990X_PDATAL,
						&chip->prox_data);

			apds990x_refresh_pthres(chip, chip->prox_data);
			if (chip->prox_data < chip->prox_thres)
				chip->prox_data = 0;
			else if (!chip->prox_continuous_mode)
				chip->prox_data = APDS_PROX_RANGE;
			sysfs_notify(&chip->client->dev.kobj,
				NULL, "prox0_raw");
		}
	}
	mutex_unlock(&chip->mutex);
	return IRQ_HANDLED;
}

static int apds990x_configure(struct apds990x_chip *chip)
{
	/* It is recommended to use disabled mode during these operations */
	apds990x_write_byte(chip, APDS990X_ENABLE, APDS990X_EN_DISABLE_ALL);

	/* conversion and wait times for different state machince states */
	apds990x_write_byte(chip, APDS990X_PTIME, APDS990X_PTIME_DEFAULT);
	apds990x_write_byte(chip, APDS990X_WTIME, APDS990X_WTIME_DEFAULT);
	apds990x_set_atime(chip, APDS_LUX_AVERAGING_TIME);

	apds990x_write_byte(chip, APDS990X_CONFIG, 0);

	/* Persistence levels */
	apds990x_write_byte(chip, APDS990X_PERS,
			(chip->lux_persistence << APDS990X_APERS_SHIFT) |
			(chip->prox_persistence << APDS990X_PPERS_SHIFT));

	apds990x_write_byte(chip, APDS990X_PPCOUNT, chip->pdata->ppcount);

	/* Start with relatively small gain */
	chip->again_meas = 1;
	chip->again_next = 1;
	apds990x_write_byte(chip, APDS990X_CONTROL,
			(chip->pdrive << 6) |
			(chip->pdiode << 4) |
			(chip->pgain << 2) |
			(chip->again_next << 0));
	return 0;
}

static int apds990x_detect(struct apds990x_chip *chip)
{
	struct i2c_client *client = chip->client;
	int ret;
	u8 id;

	ret = apds990x_read_byte(chip, APDS990X_ID, &id);
	if (ret < 0) {
		dev_err(&client->dev, "ID read failed\n");
		return ret;
	}

	ret = apds990x_read_byte(chip, APDS990X_REV, &chip->revision);
	if (ret < 0) {
		dev_err(&client->dev, "REV read failed\n");
		return ret;
	}

	switch (id) {
	case APDS990X_ID_0:
	case APDS990X_ID_4:
	case APDS990X_ID_29:
		snprintf(chip->chipname, sizeof(chip->chipname), "APDS-990x");
		break;
	default:
		ret = -ENODEV;
		break;
	}
	return ret;
}

static int apds990x_chip_on(struct apds990x_chip *chip)
{
	int err	 = regulator_bulk_enable(ARRAY_SIZE(chip->regs),
					chip->regs);
	if (err < 0)
		return err;

	usleep_range(APDS_STARTUP_DELAY, 2 * APDS_STARTUP_DELAY);

	/* Refresh all configs in case of regulators were off */
	chip->prox_data = 0;
	apds990x_configure(chip);
	apds990x_mode_on(chip);
	return 0;
}

static int apds990x_chip_off(struct apds990x_chip *chip)
{
	apds990x_write_byte(chip, APDS990X_ENABLE, APDS990X_EN_DISABLE_ALL);
	regulator_bulk_disable(ARRAY_SIZE(chip->regs), chip->regs);
	return 0;
}

static ssize_t apds990x_lux_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip = dev_get_drvdata(dev);
	ssize_t ret;
	u32 result;
	long timeout;

	if (pm_runtime_suspended(dev))
		return -EIO;

	timeout = wait_event_interruptible_timeout(chip->wait,
						!chip->lux_wait_fresh_res,
						msecs_to_jiffies(APDS_TIMEOUT));
	if (!timeout)
		return -EIO;

	mutex_lock(&chip->mutex);
	result = (chip->lux * chip->lux_calib) / APDS_CALIB_SCALER;
	if (result > (APDS_RANGE * APDS990X_LUX_OUTPUT_SCALE))
		result = APDS_RANGE * APDS990X_LUX_OUTPUT_SCALE;

	ret = sprintf(buf, "%d.%d\n",
		result / APDS990X_LUX_OUTPUT_SCALE,
		result % APDS990X_LUX_OUTPUT_SCALE);
	mutex_unlock(&chip->mutex);
	return ret;
}

static DEVICE_ATTR(lux0_input, S_IRUGO, apds990x_lux_show, NULL);

static ssize_t apds990x_lux_range_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", APDS_RANGE);
}

static DEVICE_ATTR(lux0_sensor_range, S_IRUGO, apds990x_lux_range_show, NULL);

static ssize_t apds990x_lux_calib_format_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", APDS_CALIB_SCALER);
}

static DEVICE_ATTR(lux0_calibscale_default, S_IRUGO,
		apds990x_lux_calib_format_show, NULL);

static ssize_t apds990x_lux_calib_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", chip->lux_calib);
}

static ssize_t apds990x_lux_calib_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip = dev_get_drvdata(dev);
	unsigned long value;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	if (chip->lux_calib > APDS_RANGE)
		return -EINVAL;

	chip->lux_calib = value;

	return len;
}

static DEVICE_ATTR(lux0_calibscale, S_IRUGO | S_IWUSR, apds990x_lux_calib_show,
		apds990x_lux_calib_store);

static ssize_t apds990x_rate_avail(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int i;
	int pos = 0;
	for (i = 0; i < ARRAY_SIZE(arates_hz); i++)
		pos += sprintf(buf + pos, "%d ", arates_hz[i]);
	sprintf(buf + pos - 1, "\n");
	return pos;
}

static ssize_t apds990x_rate_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->arate);
}

static int apds990x_set_arate(struct apds990x_chip *chip, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(arates_hz); i++)
		if (rate >= arates_hz[i])
			break;

	if (i == ARRAY_SIZE(arates_hz))
		return -EINVAL;

	/* Pick up corresponding persistence value */
	chip->lux_persistence = apersis[i];
	chip->arate = arates_hz[i];

	/* If the chip is not in use, don't try to access it */
	if (pm_runtime_suspended(&chip->client->dev))
		return 0;

	/* Persistence levels */
	return apds990x_write_byte(chip, APDS990X_PERS,
			(chip->lux_persistence << APDS990X_APERS_SHIFT) |
			(chip->prox_persistence << APDS990X_PPERS_SHIFT));
}

static ssize_t apds990x_rate_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;
	int ret;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	mutex_lock(&chip->mutex);
	ret = apds990x_set_arate(chip, value);
	mutex_unlock(&chip->mutex);

	if (ret < 0)
		return ret;
	return len;
}

static DEVICE_ATTR(lux0_rate_avail, S_IRUGO, apds990x_rate_avail, NULL);

static DEVICE_ATTR(lux0_rate, S_IRUGO | S_IWUSR, apds990x_rate_show,
						 apds990x_rate_store);

static ssize_t apds990x_prox_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	if (pm_runtime_suspended(dev) || !chip->prox_en)
		return -EIO;

	mutex_lock(&chip->mutex);
	ret = sprintf(buf, "%d\n", chip->prox_data);
	mutex_unlock(&chip->mutex);
	return ret;
}

static DEVICE_ATTR(prox0_raw, S_IRUGO, apds990x_prox_show, NULL);

static ssize_t apds990x_prox_range_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", APDS_PROX_RANGE);
}

static DEVICE_ATTR(prox0_sensor_range, S_IRUGO, apds990x_prox_range_show, NULL);

static ssize_t apds990x_prox_enable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->prox_en);
}

static ssize_t apds990x_prox_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	mutex_lock(&chip->mutex);

	if (!chip->prox_en)
		chip->prox_data = 0;

	if (value)
		chip->prox_en++;
	else if (chip->prox_en > 0)
		chip->prox_en--;

	if (!pm_runtime_suspended(dev))
		apds990x_mode_on(chip);
	mutex_unlock(&chip->mutex);
	return len;
}

static DEVICE_ATTR(prox0_raw_en, S_IRUGO | S_IWUSR, apds990x_prox_enable_show,
						   apds990x_prox_enable_store);

static const char reporting_modes[][9] = {"trigger", "periodic"};

static ssize_t apds990x_prox_reporting_mode_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%s\n",
		reporting_modes[!!chip->prox_continuous_mode]);
}

static ssize_t apds990x_prox_reporting_mode_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);

	if (sysfs_streq(buf, reporting_modes[0]))
		chip->prox_continuous_mode = 0;
	else if (sysfs_streq(buf, reporting_modes[1]))
		chip->prox_continuous_mode = 1;
	else
		return -EINVAL;
	return len;
}

static DEVICE_ATTR(prox0_reporting_mode, S_IRUGO | S_IWUSR,
		apds990x_prox_reporting_mode_show,
		apds990x_prox_reporting_mode_store);

static ssize_t apds990x_prox_reporting_avail_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s %s\n", reporting_modes[0], reporting_modes[1]);
}

static DEVICE_ATTR(prox0_reporting_mode_avail, S_IRUGO | S_IWUSR,
		apds990x_prox_reporting_avail_show, NULL);


static ssize_t apds990x_lux_thresh_above_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->lux_thres_hi);
}

static ssize_t apds990x_lux_thresh_below_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->lux_thres_lo);
}

static ssize_t apds990x_set_lux_thresh(struct apds990x_chip *chip, u32 *target,
				const char *buf)
{
	int ret = 0;
	unsigned long thresh;

	if (strict_strtoul(buf, 0, &thresh))
		return -EINVAL;

	if (thresh > APDS_RANGE)
		return -EINVAL;

	mutex_lock(&chip->mutex);
	*target = thresh;
	/*
	 * Don't update values in HW if we are still waiting for
	 * first interrupt to come after device handle open call.
	 */
	if (!chip->lux_wait_fresh_res)
		apds990x_refresh_athres(chip);
	mutex_unlock(&chip->mutex);
	return ret;

}

static ssize_t apds990x_lux_thresh_above_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	int ret = apds990x_set_lux_thresh(chip, &chip->lux_thres_hi, buf);
	if (ret < 0)
		return ret;
	return len;
}

static ssize_t apds990x_lux_thresh_below_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	int ret = apds990x_set_lux_thresh(chip, &chip->lux_thres_lo, buf);
	if (ret < 0)
		return ret;
	return len;
}

static DEVICE_ATTR(lux0_thresh_above_value, S_IRUGO | S_IWUSR,
		apds990x_lux_thresh_above_show,
		apds990x_lux_thresh_above_store);

static DEVICE_ATTR(lux0_thresh_below_value, S_IRUGO | S_IWUSR,
		apds990x_lux_thresh_below_show,
		apds990x_lux_thresh_below_store);

static ssize_t apds990x_prox_threshold_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->prox_thres);
}

static ssize_t apds990x_prox_threshold_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	if ((value > APDS_RANGE) || (value == 0) ||
		(value < APDS_PROX_HYSTERESIS))
		return -EINVAL;

	mutex_lock(&chip->mutex);
	chip->prox_thres = value;

	apds990x_force_p_refresh(chip);
	mutex_unlock(&chip->mutex);
	return len;
}

static DEVICE_ATTR(prox0_thresh_above_value, S_IRUGO | S_IWUSR,
		apds990x_prox_threshold_show,
		apds990x_prox_threshold_store);

static ssize_t apds990x_power_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", !pm_runtime_suspended(dev));
	return 0;
}

static ssize_t apds990x_power_state_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;
	if (value) {
		pm_runtime_get_sync(dev);
		mutex_lock(&chip->mutex);
		chip->lux_wait_fresh_res = true;
		apds990x_force_a_refresh(chip);
		apds990x_force_p_refresh(chip);
		mutex_unlock(&chip->mutex);
	} else {
		if (!pm_runtime_suspended(dev))
			pm_runtime_put(dev);
	}
	return len;
}

static DEVICE_ATTR(power_state, S_IRUGO | S_IWUSR,
		apds990x_power_state_show,
		apds990x_power_state_store);

static ssize_t apds990x_chip_id_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%s %d\n", chip->chipname, chip->revision);
}

static DEVICE_ATTR(chip_id, S_IRUGO, apds990x_chip_id_show, NULL);

static struct attribute *sysfs_attrs_ctrl[] = {
	&dev_attr_lux0_calibscale.attr,
	&dev_attr_lux0_calibscale_default.attr,
	&dev_attr_lux0_input.attr,
	&dev_attr_lux0_sensor_range.attr,
	&dev_attr_lux0_rate.attr,
	&dev_attr_lux0_rate_avail.attr,
	&dev_attr_lux0_thresh_above_value.attr,
	&dev_attr_lux0_thresh_below_value.attr,
	&dev_attr_prox0_raw_en.attr,
	&dev_attr_prox0_raw.attr,
	&dev_attr_prox0_sensor_range.attr,
	&dev_attr_prox0_thresh_above_value.attr,
	&dev_attr_prox0_reporting_mode.attr,
	&dev_attr_prox0_reporting_mode_avail.attr,
	&dev_attr_chip_id.attr,
	&dev_attr_power_state.attr,
	NULL
};

static struct attribute_group apds990x_attribute_group[] = {
	{.attrs = sysfs_attrs_ctrl },
};

static int __devinit apds990x_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct apds990x_chip *chip;
	int err;

	chip = kzalloc(sizeof *chip, GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);
	chip->client  = client;

	init_waitqueue_head(&chip->wait);
	mutex_init(&chip->mutex);
	chip->pdata	= client->dev.platform_data;

	if (chip->pdata == NULL) {
		dev_err(&client->dev, "platform data is mandatory\n");
		err = -EINVAL;
		goto fail1;
	}

	if (chip->pdata->cf.ga == 0) {
		/* set uncovered sensor default parameters */
		chip->cf.ga = 1966; /* 0.48 * APDS_PARAM_SCALE */
		chip->cf.cf1 = 4096; /* 1.00 * APDS_PARAM_SCALE */
		chip->cf.irf1 = 9134; /* 2.23 * APDS_PARAM_SCALE */
		chip->cf.cf2 = 2867; /* 0.70 * APDS_PARAM_SCALE */
		chip->cf.irf2 = 5816; /* 1.42 * APDS_PARAM_SCALE */
		chip->cf.df = 52;
	} else {
		chip->cf = chip->pdata->cf;
	}

	/* precalculate inverse chip factors for threshold control */
	chip->rcf.afactor =
		(chip->cf.irf1 - chip->cf.irf2) * APDS_PARAM_SCALE /
		(chip->cf.cf1 - chip->cf.cf2);
	chip->rcf.cf1 = APDS_PARAM_SCALE * APDS_PARAM_SCALE /
		chip->cf.cf1;
	chip->rcf.irf1 = chip->cf.irf1 * APDS_PARAM_SCALE /
		chip->cf.cf1;
	chip->rcf.cf2 = APDS_PARAM_SCALE * APDS_PARAM_SCALE /
		chip->cf.cf2;
	chip->rcf.irf2 = chip->cf.irf2 * APDS_PARAM_SCALE /
		chip->cf.cf2;

	/* Set something to start with */
	chip->lux_thres_hi = APDS_LUX_DEF_THRES_HI;
	chip->lux_thres_lo = APDS_LUX_DEF_THRES_LO;
	chip->lux_calib = APDS_LUX_NEUTRAL_CALIB_VALUE;

	chip->prox_thres = APDS_PROX_DEF_THRES;
	chip->pdrive = chip->pdata->pdrive;
	chip->pdiode = APDS_PDIODE_IR;
	chip->pgain = APDS_PGAIN_1X;
	chip->prox_calib = APDS_PROX_NEUTRAL_CALIB_VALUE;
	chip->prox_persistence = APDS_DEFAULT_PROX_PERS;
	chip->prox_continuous_mode = false;

	chip->regs[0].supply = reg_vcc;
	chip->regs[1].supply = reg_vled;

	err = regulator_bulk_get(&client->dev,
				 ARRAY_SIZE(chip->regs), chip->regs);
	if (err < 0) {
		dev_err(&client->dev, "Cannot get regulators\n");
		goto fail1;
	}

	err = regulator_bulk_enable(ARRAY_SIZE(chip->regs), chip->regs);
	if (err < 0) {
		dev_err(&client->dev, "Cannot enable regulators\n");
		goto fail2;
	}

	usleep_range(APDS_STARTUP_DELAY, 2 * APDS_STARTUP_DELAY);

	err = apds990x_detect(chip);
	if (err < 0) {
		dev_err(&client->dev, "APDS990X not found\n");
		goto fail3;
	}

	pm_runtime_set_active(&client->dev);

	apds990x_configure(chip);
	apds990x_set_arate(chip, APDS_LUX_DEFAULT_RATE);
	apds990x_mode_on(chip);

	pm_runtime_enable(&client->dev);

	if (chip->pdata->setup_resources) {
		err = chip->pdata->setup_resources();
		if (err) {
			err = -EINVAL;
			goto fail3;
		}
	}

	err = sysfs_create_group(&chip->client->dev.kobj,
				apds990x_attribute_group);
	if (err < 0) {
		dev_err(&chip->client->dev, "Sysfs registration failed\n");
		goto fail4;
	}

	err = request_threaded_irq(client->irq, NULL,
				apds990x_irq,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_LOW |
				IRQF_ONESHOT,
				"apds990x", chip);
	if (err) {
		dev_err(&client->dev, "could not get IRQ %d\n",
			client->irq);
		goto fail5;
	}
	return err;
fail5:
	sysfs_remove_group(&chip->client->dev.kobj,
			&apds990x_attribute_group[0]);
fail4:
	if (chip->pdata && chip->pdata->release_resources)
		chip->pdata->release_resources();
fail3:
	regulator_bulk_disable(ARRAY_SIZE(chip->regs), chip->regs);
fail2:
	regulator_bulk_free(ARRAY_SIZE(chip->regs), chip->regs);
fail1:
	kfree(chip);
	return err;
}

static int __devexit apds990x_remove(struct i2c_client *client)
{
	struct apds990x_chip *chip = i2c_get_clientdata(client);

	free_irq(client->irq, chip);
	sysfs_remove_group(&chip->client->dev.kobj,
			apds990x_attribute_group);

	if (chip->pdata && chip->pdata->release_resources)
		chip->pdata->release_resources();

	if (!pm_runtime_suspended(&client->dev))
		apds990x_chip_off(chip);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	regulator_bulk_free(ARRAY_SIZE(chip->regs), chip->regs);

	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM
static int apds990x_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct apds990x_chip *chip = i2c_get_clientdata(client);

	apds990x_chip_off(chip);
	return 0;
}

static int apds990x_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct apds990x_chip *chip = i2c_get_clientdata(client);

	/*
	 * If we were enabled at suspend time, it is expected
	 * everything works nice and smoothly. Chip_on is enough
	 */
	apds990x_chip_on(chip);

	return 0;
}
#else
#define apds990x_suspend  NULL
#define apds990x_resume	  NULL
#define apds990x_shutdown NULL
#endif

#ifdef CONFIG_PM_RUNTIME
static int apds990x_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct apds990x_chip *chip = i2c_get_clientdata(client);

	apds990x_chip_off(chip);
	return 0;
}

static int apds990x_runtime_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct apds990x_chip *chip = i2c_get_clientdata(client);

	apds990x_chip_on(chip);
	return 0;
}

#endif

static const struct i2c_device_id apds990x_id[] = {
	{"apds990x", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, apds990x_id);

static const struct dev_pm_ops apds990x_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(apds990x_suspend, apds990x_resume)
	SET_RUNTIME_PM_OPS(apds990x_runtime_suspend,
			apds990x_runtime_resume,
			NULL)
};

static struct i2c_driver apds990x_driver = {
	.driver	 = {
		.name	= "apds990x",
		.owner	= THIS_MODULE,
		.pm	= &apds990x_pm_ops,
	},
	.probe	  = apds990x_probe,
	.remove	  = __devexit_p(apds990x_remove),
	.id_table = apds990x_id,
};

static int __init apds990x_init(void)
{
	return i2c_add_driver(&apds990x_driver);
}

static void __exit apds990x_exit(void)
{
	i2c_del_driver(&apds990x_driver);
}

MODULE_DESCRIPTION("APDS990X combined ALS and proximity sensor");
MODULE_AUTHOR("Samu Onkalo, Nokia Corporation");
MODULE_LICENSE("GPL v2");

module_init(apds990x_init);
module_exit(apds990x_exit);
