// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Tony Lindgren <tony@atomide.com>
 *
 * Rewritten for Linux IIO framework with some code based on
 * earlier driver found in the Motorola Linux kernel:
 *
 * Copyright (C) 2009-2010 Motorola, Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <linux/iio/buffer.h>
#include <linux/iio/driver.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/mfd/motorola-cpcap.h>

/* Register CPCAP_REG_ADCC1 bits */
#define CPCAP_BIT_ADEN_AUTO_CLR		BIT(15)	/* Currently unused */
#define CPCAP_BIT_CAL_MODE		BIT(14) /* Set with BIT_RAND0 */
#define CPCAP_BIT_ADC_CLK_SEL1		BIT(13)	/* Currently unused */
#define CPCAP_BIT_ADC_CLK_SEL0		BIT(12)	/* Currently unused */
#define CPCAP_BIT_ATOX			BIT(11)
#define CPCAP_BIT_ATO3			BIT(10)
#define CPCAP_BIT_ATO2			BIT(9)
#define CPCAP_BIT_ATO1			BIT(8)
#define CPCAP_BIT_ATO0			BIT(7)
#define CPCAP_BIT_ADA2			BIT(6)
#define CPCAP_BIT_ADA1			BIT(5)
#define CPCAP_BIT_ADA0			BIT(4)
#define CPCAP_BIT_AD_SEL1		BIT(3)	/* Set for bank1 */
#define CPCAP_BIT_RAND1			BIT(2)	/* Set for channel 16 & 17 */
#define CPCAP_BIT_RAND0			BIT(1)	/* Set with CAL_MODE */
#define CPCAP_BIT_ADEN			BIT(0)	/* Currently unused */

#define CPCAP_REG_ADCC1_DEFAULTS	(CPCAP_BIT_ADEN_AUTO_CLR | \
					 CPCAP_BIT_ADC_CLK_SEL0 |  \
					 CPCAP_BIT_RAND1)

/* Register CPCAP_REG_ADCC2 bits */
#define CPCAP_BIT_CAL_FACTOR_ENABLE	BIT(15)	/* Currently unused */
#define CPCAP_BIT_BATDETB_EN		BIT(14)	/* Currently unused */
#define CPCAP_BIT_ADTRIG_ONESHOT	BIT(13)	/* Set for !TIMING_IMM */
#define CPCAP_BIT_ASC			BIT(12)	/* Set for TIMING_IMM */
#define CPCAP_BIT_ATOX_PS_FACTOR	BIT(11)
#define CPCAP_BIT_ADC_PS_FACTOR1	BIT(10)
#define CPCAP_BIT_ADC_PS_FACTOR0	BIT(9)
#define CPCAP_BIT_AD4_SELECT		BIT(8)	/* Currently unused */
#define CPCAP_BIT_ADC_BUSY		BIT(7)	/* Currently unused */
#define CPCAP_BIT_THERMBIAS_EN		BIT(6)	/* Bias for AD0_BATTDETB */
#define CPCAP_BIT_ADTRIG_DIS		BIT(5)	/* Disable interrupt */
#define CPCAP_BIT_LIADC			BIT(4)	/* Currently unused */
#define CPCAP_BIT_TS_REFEN		BIT(3)	/* Currently unused */
#define CPCAP_BIT_TS_M2			BIT(2)	/* Currently unused */
#define CPCAP_BIT_TS_M1			BIT(1)	/* Currently unused */
#define CPCAP_BIT_TS_M0			BIT(0)	/* Currently unused */

#define CPCAP_REG_ADCC2_DEFAULTS	(CPCAP_BIT_AD4_SELECT | \
					 CPCAP_BIT_ADTRIG_DIS | \
					 CPCAP_BIT_LIADC | \
					 CPCAP_BIT_TS_M2 | \
					 CPCAP_BIT_TS_M1)

#define CPCAP_MAX_TEMP_LVL		27
#define CPCAP_FOUR_POINT_TWO_ADC	801
#define ST_ADC_CAL_CHRGI_HIGH_THRESHOLD	530
#define ST_ADC_CAL_CHRGI_LOW_THRESHOLD	494
#define ST_ADC_CAL_BATTI_HIGH_THRESHOLD	530
#define ST_ADC_CAL_BATTI_LOW_THRESHOLD	494
#define ST_ADC_CALIBRATE_DIFF_THRESHOLD	3

#define CPCAP_ADC_MAX_RETRIES		5	/* Calibration */

/**
 * struct cpcap_adc_ato - timing settings for cpcap adc
 *
 * Unfortunately no cpcap documentation available, please document when
 * using these.
 */
struct cpcap_adc_ato {
	unsigned short ato_in;
	unsigned short atox_in;
	unsigned short adc_ps_factor_in;
	unsigned short atox_ps_factor_in;
	unsigned short ato_out;
	unsigned short atox_out;
	unsigned short adc_ps_factor_out;
	unsigned short atox_ps_factor_out;
};

/**
 * struct cpcap-adc - cpcap adc device driver data
 * @reg: cpcap regmap
 * @dev: struct device
 * @vendor: cpcap vendor
 * @irq: interrupt
 * @lock: mutex
 * @ato: request timings
 * @wq_data_avail: work queue
 * @done: work done
 */
struct cpcap_adc {
	struct regmap *reg;
	struct device *dev;
	u16 vendor;
	int irq;
	struct mutex lock;	/* ADC register access lock */
	const struct cpcap_adc_ato *ato;
	wait_queue_head_t wq_data_avail;
	bool done;
};

/**
 * enum cpcap_adc_channel - cpcap adc channels
 */
enum cpcap_adc_channel {
	/* Bank0 channels */
	CPCAP_ADC_AD0,		/* Battery temperature */
	CPCAP_ADC_BATTP,	/* Battery voltage */
	CPCAP_ADC_VBUS,		/* USB VBUS voltage */
	CPCAP_ADC_AD3,		/* Die temperature when charging */
	CPCAP_ADC_BPLUS_AD4,	/* Another battery or system voltage */
	CPCAP_ADC_CHG_ISENSE,	/* Calibrated charge current */
	CPCAP_ADC_BATTI,	/* Calibrated system current */
	CPCAP_ADC_USB_ID,	/* USB OTG ID, unused on droid 4? */

	/* Bank1 channels */
	CPCAP_ADC_AD8,		/* Seems unused */
	CPCAP_ADC_AD9,		/* Seems unused */
	CPCAP_ADC_LICELL,	/* Maybe system voltage? Always 3V */
	CPCAP_ADC_HV_BATTP,	/* Another battery detection? */
	CPCAP_ADC_TSX1_AD12,	/* Seems unused, for touchscreen? */
	CPCAP_ADC_TSX2_AD13,	/* Seems unused, for touchscreen? */
	CPCAP_ADC_TSY1_AD14,	/* Seems unused, for touchscreen? */
	CPCAP_ADC_TSY2_AD15,	/* Seems unused, for touchscreen? */

	/* Remuxed channels using bank0 entries */
	CPCAP_ADC_BATTP_PI16,	/* Alternative mux mode for BATTP */
	CPCAP_ADC_BATTI_PI17,	/* Alternative mux mode for BATTI */

	CPCAP_ADC_CHANNEL_NUM,
};

/**
 * enum cpcap_adc_timing - cpcap adc timing options
 *
 * CPCAP_ADC_TIMING_IMM seems to be immediate with no timings.
 * Please document when using.
 */
enum cpcap_adc_timing {
	CPCAP_ADC_TIMING_IMM,
	CPCAP_ADC_TIMING_IN,
	CPCAP_ADC_TIMING_OUT,
};

/**
 * struct cpcap_adc_phasing_tbl - cpcap phasing table
 * @offset: offset in the phasing table
 * @multiplier: multiplier in the phasing table
 * @divider: divider in the phasing table
 * @min: minimum value
 * @max: maximum value
 */
struct cpcap_adc_phasing_tbl {
	short offset;
	unsigned short multiplier;
	unsigned short divider;
	short min;
	short max;
};

/**
 * struct cpcap_adc_conversion_tbl - cpcap conversion table
 * @conv_type: conversion type
 * @align_offset: align offset
 * @conv_offset: conversion offset
 * @cal_offset: calibration offset
 * @multiplier: conversion multiplier
 * @divider: conversion divider
 */
struct cpcap_adc_conversion_tbl {
	enum iio_chan_info_enum conv_type;
	int align_offset;
	int conv_offset;
	int cal_offset;
	int multiplier;
	int divider;
};

/**
 * struct cpcap_adc_request - cpcap adc request
 * @channel: request channel
 * @phase_tbl: channel phasing table
 * @conv_tbl: channel conversion table
 * @bank_index: channel index within the bank
 * @timing: timing settings
 * @result: result
 */
struct cpcap_adc_request {
	int channel;
	const struct cpcap_adc_phasing_tbl *phase_tbl;
	const struct cpcap_adc_conversion_tbl *conv_tbl;
	int bank_index;
	enum cpcap_adc_timing timing;
	int result;
};

/* Phasing table for channels. Note that channels 16 & 17 use BATTP and BATTI */
static const struct cpcap_adc_phasing_tbl bank_phasing[] = {
	/* Bank0 */
	[CPCAP_ADC_AD0] =          {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_BATTP] =        {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_VBUS] =         {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_AD3] =          {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_BPLUS_AD4] =    {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_CHG_ISENSE] =   {0, 0x80, 0x80, -512,  511},
	[CPCAP_ADC_BATTI] =        {0, 0x80, 0x80, -512,  511},
	[CPCAP_ADC_USB_ID] =       {0, 0x80, 0x80,    0, 1023},

	/* Bank1 */
	[CPCAP_ADC_AD8] =          {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_AD9] =          {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_LICELL] =       {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_HV_BATTP] =     {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_TSX1_AD12] =    {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_TSX2_AD13] =    {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_TSY1_AD14] =    {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_TSY2_AD15] =    {0, 0x80, 0x80,    0, 1023},
};

/*
 * Conversion table for channels. Updated during init based on calibration.
 * Here too channels 16 & 17 use BATTP and BATTI.
 */
static struct cpcap_adc_conversion_tbl bank_conversion[] = {
	/* Bank0 */
	[CPCAP_ADC_AD0] = {
		IIO_CHAN_INFO_PROCESSED,    0,    0, 0,     1,    1,
	},
	[CPCAP_ADC_BATTP] = {
		IIO_CHAN_INFO_PROCESSED,    0, 2400, 0,  2300, 1023,
	},
	[CPCAP_ADC_VBUS] = {
		IIO_CHAN_INFO_PROCESSED,    0,    0, 0, 10000, 1023,
	},
	[CPCAP_ADC_AD3] = {
		IIO_CHAN_INFO_PROCESSED,    0,    0, 0,     1,    1,
		},
	[CPCAP_ADC_BPLUS_AD4] = {
		IIO_CHAN_INFO_PROCESSED,    0, 2400, 0,  2300, 1023,
	},
	[CPCAP_ADC_CHG_ISENSE] = {
		IIO_CHAN_INFO_PROCESSED, -512,    2, 0,  5000, 1023,
	},
	[CPCAP_ADC_BATTI] = {
		IIO_CHAN_INFO_PROCESSED, -512,    2, 0,  5000, 1023,
	},
	[CPCAP_ADC_USB_ID] = {
		IIO_CHAN_INFO_RAW,          0,    0, 0,     1,    1,
	},

	/* Bank1 */
	[CPCAP_ADC_AD8] = {
		IIO_CHAN_INFO_RAW,          0,    0, 0,     1,    1,
	},
	[CPCAP_ADC_AD9] = {
		IIO_CHAN_INFO_RAW,          0,    0, 0,     1,    1,
	},
	[CPCAP_ADC_LICELL] = {
		IIO_CHAN_INFO_PROCESSED,    0,    0, 0,  3400, 1023,
	},
	[CPCAP_ADC_HV_BATTP] = {
		IIO_CHAN_INFO_RAW,          0,    0, 0,     1,    1,
	},
	[CPCAP_ADC_TSX1_AD12] = {
		IIO_CHAN_INFO_RAW,          0,    0, 0,     1,    1,
	},
	[CPCAP_ADC_TSX2_AD13] = {
		IIO_CHAN_INFO_RAW,          0,    0, 0,     1,    1,
	},
	[CPCAP_ADC_TSY1_AD14] = {
		IIO_CHAN_INFO_RAW,          0,    0, 0,     1,    1,
	},
	[CPCAP_ADC_TSY2_AD15] = {
		IIO_CHAN_INFO_RAW,          0,    0, 0,     1,    1,
	},
};

/*
 * Temperature lookup table of register values to milliCelcius.
 * REVISIT: Check the duplicate 0x3ff entry in a freezer
 */
static const int temp_map[CPCAP_MAX_TEMP_LVL][2] = {
	{ 0x03ff, -40000 },
	{ 0x03ff, -35000 },
	{ 0x03ef, -30000 },
	{ 0x03b2, -25000 },
	{ 0x036c, -20000 },
	{ 0x0320, -15000 },
	{ 0x02d0, -10000 },
	{ 0x027f, -5000 },
	{ 0x022f, 0 },
	{ 0x01e4, 5000 },
	{ 0x019f, 10000 },
	{ 0x0161, 15000 },
	{ 0x012b, 20000 },
	{ 0x00fc, 25000 },
	{ 0x00d4, 30000 },
	{ 0x00b2, 35000 },
	{ 0x0095, 40000 },
	{ 0x007d, 45000 },
	{ 0x0069, 50000 },
	{ 0x0059, 55000 },
	{ 0x004b, 60000 },
	{ 0x003f, 65000 },
	{ 0x0036, 70000 },
	{ 0x002e, 75000 },
	{ 0x0027, 80000 },
	{ 0x0022, 85000 },
	{ 0x001d, 90000 },
};

#define CPCAP_CHAN(_type, _index, _address, _datasheet_name) {	\
	.type = (_type), \
	.address = (_address), \
	.indexed = 1, \
	.channel = (_index), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_PROCESSED), \
	.scan_index = (_index), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 10, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	}, \
	.datasheet_name = (_datasheet_name), \
}

/*
 * The datasheet names are from Motorola mapphone Linux kernel except
 * for the last two which might be uncalibrated charge voltage and
 * current.
 */
static const struct iio_chan_spec cpcap_adc_channels[] = {
	/* Bank0 */
	CPCAP_CHAN(IIO_TEMP,    0, CPCAP_REG_ADCD0,  "battdetb"),
	CPCAP_CHAN(IIO_VOLTAGE, 1, CPCAP_REG_ADCD1,  "battp"),
	CPCAP_CHAN(IIO_VOLTAGE, 2, CPCAP_REG_ADCD2,  "vbus"),
	CPCAP_CHAN(IIO_TEMP,    3, CPCAP_REG_ADCD3,  "ad3"),
	CPCAP_CHAN(IIO_VOLTAGE, 4, CPCAP_REG_ADCD4,  "ad4"),
	CPCAP_CHAN(IIO_CURRENT, 5, CPCAP_REG_ADCD5,  "chg_isense"),
	CPCAP_CHAN(IIO_CURRENT, 6, CPCAP_REG_ADCD6,  "batti"),
	CPCAP_CHAN(IIO_VOLTAGE, 7, CPCAP_REG_ADCD7,  "usb_id"),

	/* Bank1 */
	CPCAP_CHAN(IIO_CURRENT, 8, CPCAP_REG_ADCD0,  "ad8"),
	CPCAP_CHAN(IIO_VOLTAGE, 9, CPCAP_REG_ADCD1,  "ad9"),
	CPCAP_CHAN(IIO_VOLTAGE, 10, CPCAP_REG_ADCD2, "licell"),
	CPCAP_CHAN(IIO_VOLTAGE, 11, CPCAP_REG_ADCD3, "hv_battp"),
	CPCAP_CHAN(IIO_VOLTAGE, 12, CPCAP_REG_ADCD4, "tsx1_ad12"),
	CPCAP_CHAN(IIO_VOLTAGE, 13, CPCAP_REG_ADCD5, "tsx2_ad13"),
	CPCAP_CHAN(IIO_VOLTAGE, 14, CPCAP_REG_ADCD6, "tsy1_ad14"),
	CPCAP_CHAN(IIO_VOLTAGE, 15, CPCAP_REG_ADCD7, "tsy2_ad15"),

	/* There are two registers with multiplexed functionality */
	CPCAP_CHAN(IIO_VOLTAGE, 16, CPCAP_REG_ADCD0, "chg_vsense"),
	CPCAP_CHAN(IIO_CURRENT, 17, CPCAP_REG_ADCD1, "batti2"),
};

static irqreturn_t cpcap_adc_irq_thread(int irq, void *data)
{
	struct iio_dev *indio_dev = data;
	struct cpcap_adc *ddata = iio_priv(indio_dev);
	int error;

	error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC2,
				   CPCAP_BIT_ADTRIG_DIS,
				   CPCAP_BIT_ADTRIG_DIS);
	if (error)
		return IRQ_NONE;

	ddata->done = true;
	wake_up_interruptible(&ddata->wq_data_avail);

	return IRQ_HANDLED;
}

/* ADC calibration functions */
static void cpcap_adc_setup_calibrate(struct cpcap_adc *ddata,
				      enum cpcap_adc_channel chan)
{
	unsigned int value = 0;
	unsigned long timeout = jiffies + msecs_to_jiffies(3000);
	int error;

	if ((chan != CPCAP_ADC_CHG_ISENSE) &&
	    (chan != CPCAP_ADC_BATTI))
		return;

	value |= CPCAP_BIT_CAL_MODE | CPCAP_BIT_RAND0;
	value |= ((chan << 4) &
		  (CPCAP_BIT_ADA2 | CPCAP_BIT_ADA1 | CPCAP_BIT_ADA0));

	error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC1,
				   CPCAP_BIT_CAL_MODE | CPCAP_BIT_ATOX |
				   CPCAP_BIT_ATO3 | CPCAP_BIT_ATO2 |
				   CPCAP_BIT_ATO1 | CPCAP_BIT_ATO0 |
				   CPCAP_BIT_ADA2 | CPCAP_BIT_ADA1 |
				   CPCAP_BIT_ADA0 | CPCAP_BIT_AD_SEL1 |
				   CPCAP_BIT_RAND1 | CPCAP_BIT_RAND0,
				   value);
	if (error)
		return;

	error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC2,
				   CPCAP_BIT_ATOX_PS_FACTOR |
				   CPCAP_BIT_ADC_PS_FACTOR1 |
				   CPCAP_BIT_ADC_PS_FACTOR0,
				   0);
	if (error)
		return;

	error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC2,
				   CPCAP_BIT_ADTRIG_DIS,
				   CPCAP_BIT_ADTRIG_DIS);
	if (error)
		return;

	error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC2,
				   CPCAP_BIT_ASC,
				   CPCAP_BIT_ASC);
	if (error)
		return;

	do {
		schedule_timeout_uninterruptible(1);
		error = regmap_read(ddata->reg, CPCAP_REG_ADCC2, &value);
		if (error)
			return;
	} while ((value & CPCAP_BIT_ASC) && time_before(jiffies, timeout));

	if (value & CPCAP_BIT_ASC)
		dev_err(ddata->dev,
			"Timeout waiting for calibration to complete\n");

	error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC1,
				   CPCAP_BIT_CAL_MODE, 0);
	if (error)
		return;
}

static int cpcap_adc_calibrate_one(struct cpcap_adc *ddata,
				   int channel,
				   u16 calibration_register,
				   int lower_threshold,
				   int upper_threshold)
{
	unsigned int calibration_data[2];
	unsigned short cal_data_diff;
	int i, error;

	for (i = 0; i < CPCAP_ADC_MAX_RETRIES; i++) {
		calibration_data[0]  = 0;
		calibration_data[1]  = 0;
		cal_data_diff = 0;
		cpcap_adc_setup_calibrate(ddata, channel);
		error = regmap_read(ddata->reg, calibration_register,
				    &calibration_data[0]);
		if (error)
			return error;
		cpcap_adc_setup_calibrate(ddata, channel);
		error = regmap_read(ddata->reg, calibration_register,
				    &calibration_data[1]);
		if (error)
			return error;

		if (calibration_data[0] > calibration_data[1])
			cal_data_diff =
				calibration_data[0] - calibration_data[1];
		else
			cal_data_diff =
				calibration_data[1] - calibration_data[0];

		if (((calibration_data[1] >= lower_threshold) &&
		     (calibration_data[1] <= upper_threshold) &&
		     (cal_data_diff <= ST_ADC_CALIBRATE_DIFF_THRESHOLD)) ||
		    (ddata->vendor == CPCAP_VENDOR_TI)) {
			bank_conversion[channel].cal_offset =
				((short)calibration_data[1] * -1) + 512;
			dev_dbg(ddata->dev, "ch%i calibration complete: %i\n",
				channel, bank_conversion[channel].cal_offset);
			break;
		}
		usleep_range(5000, 10000);
	}

	return 0;
}

static int cpcap_adc_calibrate(struct cpcap_adc *ddata)
{
	int error;

	error = cpcap_adc_calibrate_one(ddata, CPCAP_ADC_CHG_ISENSE,
					CPCAP_REG_ADCAL1,
					ST_ADC_CAL_CHRGI_LOW_THRESHOLD,
					ST_ADC_CAL_CHRGI_HIGH_THRESHOLD);
	if (error)
		return error;

	error = cpcap_adc_calibrate_one(ddata, CPCAP_ADC_BATTI,
					CPCAP_REG_ADCAL2,
					ST_ADC_CAL_BATTI_LOW_THRESHOLD,
					ST_ADC_CAL_BATTI_HIGH_THRESHOLD);
	if (error)
		return error;

	return 0;
}

/* ADC setup, read and scale functions */
static void cpcap_adc_setup_bank(struct cpcap_adc *ddata,
				 struct cpcap_adc_request *req)
{
	const struct cpcap_adc_ato *ato = ddata->ato;
	unsigned short value1 = 0;
	unsigned short value2 = 0;
	int error;

	if (!ato)
		return;

	switch (req->channel) {
	case CPCAP_ADC_AD0:
		value2 |= CPCAP_BIT_THERMBIAS_EN;
		error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC2,
					   CPCAP_BIT_THERMBIAS_EN,
					   value2);
		if (error)
			return;
		usleep_range(800, 1000);
		break;
	case CPCAP_ADC_AD8 ... CPCAP_ADC_TSY2_AD15:
		value1 |= CPCAP_BIT_AD_SEL1;
		break;
	case CPCAP_ADC_BATTP_PI16 ... CPCAP_ADC_BATTI_PI17:
		value1 |= CPCAP_BIT_RAND1;
	default:
		break;
	}

	switch (req->timing) {
	case CPCAP_ADC_TIMING_IN:
		value1 |= ato->ato_in;
		value1 |= ato->atox_in;
		value2 |= ato->adc_ps_factor_in;
		value2 |= ato->atox_ps_factor_in;
		break;
	case CPCAP_ADC_TIMING_OUT:
		value1 |= ato->ato_out;
		value1 |= ato->atox_out;
		value2 |= ato->adc_ps_factor_out;
		value2 |= ato->atox_ps_factor_out;
		break;

	case CPCAP_ADC_TIMING_IMM:
	default:
		break;
	}

	error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC1,
				   CPCAP_BIT_CAL_MODE | CPCAP_BIT_ATOX |
				   CPCAP_BIT_ATO3 | CPCAP_BIT_ATO2 |
				   CPCAP_BIT_ATO1 | CPCAP_BIT_ATO0 |
				   CPCAP_BIT_ADA2 | CPCAP_BIT_ADA1 |
				   CPCAP_BIT_ADA0 | CPCAP_BIT_AD_SEL1 |
				   CPCAP_BIT_RAND1 | CPCAP_BIT_RAND0,
				   value1);
	if (error)
		return;

	error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC2,
				   CPCAP_BIT_ATOX_PS_FACTOR |
				   CPCAP_BIT_ADC_PS_FACTOR1 |
				   CPCAP_BIT_ADC_PS_FACTOR0 |
				   CPCAP_BIT_THERMBIAS_EN,
				   value2);
	if (error)
		return;

	if (req->timing == CPCAP_ADC_TIMING_IMM) {
		error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC2,
					   CPCAP_BIT_ADTRIG_DIS,
					   CPCAP_BIT_ADTRIG_DIS);
		if (error)
			return;

		error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC2,
					   CPCAP_BIT_ASC,
					   CPCAP_BIT_ASC);
		if (error)
			return;
	} else {
		error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC2,
					   CPCAP_BIT_ADTRIG_ONESHOT,
					   CPCAP_BIT_ADTRIG_ONESHOT);
		if (error)
			return;

		error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC2,
					   CPCAP_BIT_ADTRIG_DIS, 0);
		if (error)
			return;
	}
}

static int cpcap_adc_start_bank(struct cpcap_adc *ddata,
				struct cpcap_adc_request *req)
{
	int i, error;

	req->timing = CPCAP_ADC_TIMING_IMM;
	ddata->done = false;

	for (i = 0; i < CPCAP_ADC_MAX_RETRIES; i++) {
		cpcap_adc_setup_bank(ddata, req);
		error = wait_event_interruptible_timeout(ddata->wq_data_avail,
							 ddata->done,
							 msecs_to_jiffies(50));
		if (error > 0)
			return 0;

		if (error == 0) {
			error = -ETIMEDOUT;
			continue;
		}

		if (error < 0)
			return error;
	}

	return error;
}

static int cpcap_adc_stop_bank(struct cpcap_adc *ddata)
{
	int error;

	error = regmap_update_bits(ddata->reg, CPCAP_REG_ADCC1,
				   0xffff,
				   CPCAP_REG_ADCC1_DEFAULTS);
	if (error)
		return error;

	return regmap_update_bits(ddata->reg, CPCAP_REG_ADCC2,
				  0xffff,
				  CPCAP_REG_ADCC2_DEFAULTS);
}

static void cpcap_adc_phase(struct cpcap_adc_request *req)
{
	const struct cpcap_adc_conversion_tbl *conv_tbl = req->conv_tbl;
	const struct cpcap_adc_phasing_tbl *phase_tbl = req->phase_tbl;
	int index = req->channel;

	/* Remuxed channels 16 and 17 use BATTP and BATTI entries */
	switch (req->channel) {
	case CPCAP_ADC_BATTP:
	case CPCAP_ADC_BATTP_PI16:
		index = req->bank_index;
		req->result -= phase_tbl[index].offset;
		req->result -= CPCAP_FOUR_POINT_TWO_ADC;
		req->result *= phase_tbl[index].multiplier;
		if (phase_tbl[index].divider == 0)
			return;
		req->result /= phase_tbl[index].divider;
		req->result += CPCAP_FOUR_POINT_TWO_ADC;
		break;
	case CPCAP_ADC_BATTI_PI17:
		index = req->bank_index;
		/* fallthrough */
	default:
		req->result += conv_tbl[index].cal_offset;
		req->result += conv_tbl[index].align_offset;
		req->result *= phase_tbl[index].multiplier;
		if (phase_tbl[index].divider == 0)
			return;
		req->result /= phase_tbl[index].divider;
		req->result += phase_tbl[index].offset;
		break;
	}

	if (req->result < phase_tbl[index].min)
		req->result = phase_tbl[index].min;
	else if (req->result > phase_tbl[index].max)
		req->result = phase_tbl[index].max;
}

/* Looks up temperatures in a table and calculates averages if needed */
static int cpcap_adc_table_to_millicelcius(unsigned short value)
{
	int i, result = 0, alpha;

	if (value <= temp_map[CPCAP_MAX_TEMP_LVL - 1][0])
		return temp_map[CPCAP_MAX_TEMP_LVL - 1][1];

	if (value >= temp_map[0][0])
		return temp_map[0][1];

	for (i = 0; i < CPCAP_MAX_TEMP_LVL - 1; i++) {
		if ((value <= temp_map[i][0]) &&
		    (value >= temp_map[i + 1][0])) {
			if (value == temp_map[i][0]) {
				result = temp_map[i][1];
			} else if (value == temp_map[i + 1][0]) {
				result = temp_map[i + 1][1];
			} else {
				alpha = ((value - temp_map[i][0]) * 1000) /
					(temp_map[i + 1][0] - temp_map[i][0]);

				result = temp_map[i][1] +
					((alpha * (temp_map[i + 1][1] -
						 temp_map[i][1])) / 1000);
			}
			break;
		}
	}

	return result;
}

static void cpcap_adc_convert(struct cpcap_adc_request *req)
{
	const struct cpcap_adc_conversion_tbl *conv_tbl = req->conv_tbl;
	int index = req->channel;

	/* Remuxed channels 16 and 17 use BATTP and BATTI entries */
	switch (req->channel) {
	case CPCAP_ADC_BATTP_PI16:
		index = CPCAP_ADC_BATTP;
		break;
	case CPCAP_ADC_BATTI_PI17:
		index = CPCAP_ADC_BATTI;
		break;
	default:
		break;
	}

	/* No conversion for raw channels */
	if (conv_tbl[index].conv_type == IIO_CHAN_INFO_RAW)
		return;

	/* Temperatures use a lookup table instead of conversion table */
	if ((req->channel == CPCAP_ADC_AD0) ||
	    (req->channel == CPCAP_ADC_AD3)) {
		req->result =
			cpcap_adc_table_to_millicelcius(req->result);

		return;
	}

	/* All processed channels use a conversion table */
	req->result *= conv_tbl[index].multiplier;
	if (conv_tbl[index].divider == 0)
		return;
	req->result /= conv_tbl[index].divider;
	req->result += conv_tbl[index].conv_offset;
}

/*
 * REVISIT: Check if timed sampling can use multiple channels at the
 * same time. If not, replace channel_mask with just channel.
 */
static int cpcap_adc_read_bank_scaled(struct cpcap_adc *ddata,
				      struct cpcap_adc_request *req)
{
	int calibration_data, error, addr;

	if (ddata->vendor == CPCAP_VENDOR_TI) {
		error = regmap_read(ddata->reg, CPCAP_REG_ADCAL1,
				    &calibration_data);
		if (error)
			return error;
		bank_conversion[CPCAP_ADC_CHG_ISENSE].cal_offset =
			((short)calibration_data * -1) + 512;

		error = regmap_read(ddata->reg, CPCAP_REG_ADCAL2,
				    &calibration_data);
		if (error)
			return error;
		bank_conversion[CPCAP_ADC_BATTI].cal_offset =
			((short)calibration_data * -1) + 512;
	}

	addr = CPCAP_REG_ADCD0 + req->bank_index * 4;

	error = regmap_read(ddata->reg, addr, &req->result);
	if (error)
		return error;

	req->result &= 0x3ff;
	cpcap_adc_phase(req);
	cpcap_adc_convert(req);

	return 0;
}

static int cpcap_adc_init_request(struct cpcap_adc_request *req,
				  int channel)
{
	req->channel = channel;
	req->phase_tbl = bank_phasing;
	req->conv_tbl = bank_conversion;

	switch (channel) {
	case CPCAP_ADC_AD0 ... CPCAP_ADC_USB_ID:
		req->bank_index = channel;
		break;
	case CPCAP_ADC_AD8 ... CPCAP_ADC_TSY2_AD15:
		req->bank_index = channel - 8;
		break;
	case CPCAP_ADC_BATTP_PI16:
		req->bank_index = CPCAP_ADC_BATTP;
		break;
	case CPCAP_ADC_BATTI_PI17:
		req->bank_index = CPCAP_ADC_BATTI;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cpcap_adc_read_st_die_temp(struct cpcap_adc *ddata,
				      int addr, int *val)
{
	int error;

	error = regmap_read(ddata->reg, addr, val);
	if (error)
		return error;

	*val -= 282;
	*val *= 114;
	*val += 25000;

	return 0;
}

static int cpcap_adc_read(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan,
			  int *val, int *val2, long mask)
{
	struct cpcap_adc *ddata = iio_priv(indio_dev);
	struct cpcap_adc_request req;
	int error;

	error = cpcap_adc_init_request(&req, chan->channel);
	if (error)
		return error;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&ddata->lock);
		error = cpcap_adc_start_bank(ddata, &req);
		if (error)
			goto err_unlock;
		error = regmap_read(ddata->reg, chan->address, val);
		if (error)
			goto err_unlock;
		error = cpcap_adc_stop_bank(ddata);
		if (error)
			goto err_unlock;
		mutex_unlock(&ddata->lock);
		break;
	case IIO_CHAN_INFO_PROCESSED:
		mutex_lock(&ddata->lock);
		error = cpcap_adc_start_bank(ddata, &req);
		if (error)
			goto err_unlock;
		if ((ddata->vendor == CPCAP_VENDOR_ST) &&
		    (chan->channel == CPCAP_ADC_AD3)) {
			error = cpcap_adc_read_st_die_temp(ddata,
							   chan->address,
							   &req.result);
			if (error)
				goto err_unlock;
		} else {
			error = cpcap_adc_read_bank_scaled(ddata, &req);
			if (error)
				goto err_unlock;
		}
		error = cpcap_adc_stop_bank(ddata);
		if (error)
			goto err_unlock;
		mutex_unlock(&ddata->lock);
		*val = req.result;
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_INT;

err_unlock:
	mutex_unlock(&ddata->lock);
	dev_err(ddata->dev, "error reading ADC: %i\n", error);

	return error;
}

static const struct iio_info cpcap_adc_info = {
	.read_raw = &cpcap_adc_read,
};

/*
 * Configuration for Motorola mapphone series such as droid 4.
 * Copied from the Motorola mapphone kernel tree.
 */
static const struct cpcap_adc_ato mapphone_adc = {
	.ato_in = 0x0480,
	.atox_in = 0,
	.adc_ps_factor_in = 0x0200,
	.atox_ps_factor_in = 0,
	.ato_out = 0,
	.atox_out = 0,
	.adc_ps_factor_out = 0,
	.atox_ps_factor_out = 0,
};

static const struct of_device_id cpcap_adc_id_table[] = {
	{
		.compatible = "motorola,cpcap-adc",
	},
	{
		.compatible = "motorola,mapphone-cpcap-adc",
		.data = &mapphone_adc,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, cpcap_adc_id_table);

static int cpcap_adc_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct cpcap_adc *ddata;
	struct iio_dev *indio_dev;
	int error;

	match = of_match_device(of_match_ptr(cpcap_adc_id_table),
				&pdev->dev);
	if (!match)
		return -EINVAL;

	if (!match->data) {
		dev_err(&pdev->dev, "no configuration data found\n");

		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*ddata));
	if (!indio_dev) {
		dev_err(&pdev->dev, "failed to allocate iio device\n");

		return -ENOMEM;
	}
	ddata = iio_priv(indio_dev);
	ddata->ato = match->data;
	ddata->dev = &pdev->dev;

	mutex_init(&ddata->lock);
	init_waitqueue_head(&ddata->wq_data_avail);

	indio_dev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_SOFTWARE;
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->channels = cpcap_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(cpcap_adc_channels);
	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->info = &cpcap_adc_info;

	ddata->reg = dev_get_regmap(pdev->dev.parent, NULL);
	if (!ddata->reg)
		return -ENODEV;

	error = cpcap_get_vendor(ddata->dev, ddata->reg, &ddata->vendor);
	if (error)
		return error;

	platform_set_drvdata(pdev, indio_dev);

	ddata->irq = platform_get_irq_byname(pdev, "adcdone");
	if (ddata->irq < 0)
		return -ENODEV;

	error = devm_request_threaded_irq(&pdev->dev, ddata->irq, NULL,
					  cpcap_adc_irq_thread,
					  IRQF_TRIGGER_NONE | IRQF_ONESHOT,
					  "cpcap-adc", indio_dev);
	if (error) {
		dev_err(&pdev->dev, "could not get irq: %i\n",
			error);

		return error;
	}

	error = cpcap_adc_calibrate(ddata);
	if (error)
		return error;

	dev_info(&pdev->dev, "CPCAP ADC device probed\n");

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static struct platform_driver cpcap_adc_driver = {
	.driver = {
		.name = "cpcap_adc",
		.of_match_table = of_match_ptr(cpcap_adc_id_table),
	},
	.probe = cpcap_adc_probe,
};

module_platform_driver(cpcap_adc_driver);

MODULE_ALIAS("platform:cpcap_adc");
MODULE_DESCRIPTION("CPCAP ADC driver");
MODULE_AUTHOR("Tony Lindgren <tony@atomide.com");
MODULE_LICENSE("GPL v2");
