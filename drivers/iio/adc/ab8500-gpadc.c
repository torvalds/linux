// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Arun R Murthy <arun.murthy@stericsson.com>
 * Author: Daniel Willerud <daniel.willerud@stericsson.com>
 * Author: Johan Palsson <johan.palsson@stericsson.com>
 * Author: M'boumba Cedric Madianga
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * AB8500 General Purpose ADC driver. The AB8500 uses reference voltages:
 * VinVADC, and VADC relative to GND to do its job. It monitors main and backup
 * battery voltages, AC (mains) voltage, USB cable voltage, as well as voltages
 * representing the temperature of the chip die and battery, accessory
 * detection by resistance measurements using relative voltages and GSM burst
 * information.
 *
 * Some of the voltages are measured on external pins on the IC, such as
 * battery temperature or "ADC aux" 1 and 2. Other voltages are internal rails
 * from other parts of the ASIC such as main charger voltage, main and battery
 * backup voltage or USB VBUS voltage. For this reason drivers for other
 * parts of the system are required to obtain handles to the ADC to do work
 * for them and the IIO driver provides arbitration among these consumers.
 */
#include <linux/init.h>
#include <linux/bits.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/completion.h>
#include <linux/regulator/consumer.h>
#include <linux/random.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>

/* GPADC register offsets and bit definitions */

#define AB8500_GPADC_CTRL1_REG		0x00
/* GPADC control register 1 bits */
#define AB8500_GPADC_CTRL1_DISABLE		0x00
#define AB8500_GPADC_CTRL1_ENABLE		BIT(0)
#define AB8500_GPADC_CTRL1_TRIG_ENA		BIT(1)
#define AB8500_GPADC_CTRL1_START_SW_CONV	BIT(2)
#define AB8500_GPADC_CTRL1_BTEMP_PULL_UP	BIT(3)
/* 0 = use rising edge, 1 = use falling edge */
#define AB8500_GPADC_CTRL1_TRIG_EDGE		BIT(4)
/* 0 = use VTVOUT, 1 = use VRTC as pull-up supply for battery temp NTC */
#define AB8500_GPADC_CTRL1_PUPSUPSEL		BIT(5)
#define AB8500_GPADC_CTRL1_BUF_ENA		BIT(6)
#define AB8500_GPADC_CTRL1_ICHAR_ENA		BIT(7)

#define AB8500_GPADC_CTRL2_REG		0x01
#define AB8500_GPADC_CTRL3_REG		0x02
/*
 * GPADC control register 2 and 3 bits
 * the bit layout is the same for SW and HW conversion set-up
 */
#define AB8500_GPADC_CTRL2_AVG_1		0x00
#define AB8500_GPADC_CTRL2_AVG_4		BIT(5)
#define AB8500_GPADC_CTRL2_AVG_8		BIT(6)
#define AB8500_GPADC_CTRL2_AVG_16		(BIT(5) | BIT(6))

enum ab8500_gpadc_channel {
	AB8500_GPADC_CHAN_UNUSED = 0x00,
	AB8500_GPADC_CHAN_BAT_CTRL = 0x01,
	AB8500_GPADC_CHAN_BAT_TEMP = 0x02,
	/* This is not used on AB8505 */
	AB8500_GPADC_CHAN_MAIN_CHARGER = 0x03,
	AB8500_GPADC_CHAN_ACC_DET_1 = 0x04,
	AB8500_GPADC_CHAN_ACC_DET_2 = 0x05,
	AB8500_GPADC_CHAN_ADC_AUX_1 = 0x06,
	AB8500_GPADC_CHAN_ADC_AUX_2 = 0x07,
	AB8500_GPADC_CHAN_VBAT_A = 0x08,
	AB8500_GPADC_CHAN_VBUS = 0x09,
	AB8500_GPADC_CHAN_MAIN_CHARGER_CURRENT = 0x0a,
	AB8500_GPADC_CHAN_USB_CHARGER_CURRENT = 0x0b,
	AB8500_GPADC_CHAN_BACKUP_BAT = 0x0c,
	/* Only on AB8505 */
	AB8505_GPADC_CHAN_DIE_TEMP = 0x0d,
	AB8500_GPADC_CHAN_ID = 0x0e,
	AB8500_GPADC_CHAN_INTERNAL_TEST_1 = 0x0f,
	AB8500_GPADC_CHAN_INTERNAL_TEST_2 = 0x10,
	AB8500_GPADC_CHAN_INTERNAL_TEST_3 = 0x11,
	/* FIXME: Applicable to all ASIC variants? */
	AB8500_GPADC_CHAN_XTAL_TEMP = 0x12,
	AB8500_GPADC_CHAN_VBAT_TRUE_MEAS = 0x13,
	/* FIXME: Doesn't seem to work with pure AB8500 */
	AB8500_GPADC_CHAN_BAT_CTRL_AND_IBAT = 0x1c,
	AB8500_GPADC_CHAN_VBAT_MEAS_AND_IBAT = 0x1d,
	AB8500_GPADC_CHAN_VBAT_TRUE_MEAS_AND_IBAT = 0x1e,
	AB8500_GPADC_CHAN_BAT_TEMP_AND_IBAT = 0x1f,
	/*
	 * Virtual channel used only for ibat conversion to ampere.
	 * Battery current conversion (ibat) cannot be requested as a
	 * single conversion but it is always requested in combination
	 * with other input requests.
	 */
	AB8500_GPADC_CHAN_IBAT_VIRTUAL = 0xFF,
};

#define AB8500_GPADC_AUTO_TIMER_REG	0x03

#define AB8500_GPADC_STAT_REG		0x04
#define AB8500_GPADC_STAT_BUSY		BIT(0)

#define AB8500_GPADC_MANDATAL_REG	0x05
#define AB8500_GPADC_MANDATAH_REG	0x06
#define AB8500_GPADC_AUTODATAL_REG	0x07
#define AB8500_GPADC_AUTODATAH_REG	0x08
#define AB8500_GPADC_MUX_CTRL_REG	0x09
#define AB8540_GPADC_MANDATA2L_REG	0x09
#define AB8540_GPADC_MANDATA2H_REG	0x0A
#define AB8540_GPADC_APEAAX_REG		0x10
#define AB8540_GPADC_APEAAT_REG		0x11
#define AB8540_GPADC_APEAAM_REG		0x12
#define AB8540_GPADC_APEAAH_REG		0x13
#define AB8540_GPADC_APEAAL_REG		0x14

/*
 * OTP register offsets
 * Bank : 0x15
 */
#define AB8500_GPADC_CAL_1	0x0F
#define AB8500_GPADC_CAL_2	0x10
#define AB8500_GPADC_CAL_3	0x11
#define AB8500_GPADC_CAL_4	0x12
#define AB8500_GPADC_CAL_5	0x13
#define AB8500_GPADC_CAL_6	0x14
#define AB8500_GPADC_CAL_7	0x15
/* New calibration for 8540 */
#define AB8540_GPADC_OTP4_REG_7	0x38
#define AB8540_GPADC_OTP4_REG_6	0x39
#define AB8540_GPADC_OTP4_REG_5	0x3A

#define AB8540_GPADC_DIS_ZERO	0x00
#define AB8540_GPADC_EN_VBIAS_XTAL_TEMP	0x02

/* GPADC constants from AB8500 spec, UM0836 */
#define AB8500_ADC_RESOLUTION		1024
#define AB8500_ADC_CH_BTEMP_MIN		0
#define AB8500_ADC_CH_BTEMP_MAX		1350
#define AB8500_ADC_CH_DIETEMP_MIN	0
#define AB8500_ADC_CH_DIETEMP_MAX	1350
#define AB8500_ADC_CH_CHG_V_MIN		0
#define AB8500_ADC_CH_CHG_V_MAX		20030
#define AB8500_ADC_CH_ACCDET2_MIN	0
#define AB8500_ADC_CH_ACCDET2_MAX	2500
#define AB8500_ADC_CH_VBAT_MIN		2300
#define AB8500_ADC_CH_VBAT_MAX		4800
#define AB8500_ADC_CH_CHG_I_MIN		0
#define AB8500_ADC_CH_CHG_I_MAX		1500
#define AB8500_ADC_CH_BKBAT_MIN		0
#define AB8500_ADC_CH_BKBAT_MAX		3200

/* GPADC constants from AB8540 spec */
#define AB8500_ADC_CH_IBAT_MIN		(-6000) /* mA range measured by ADC for ibat */
#define AB8500_ADC_CH_IBAT_MAX		6000
#define AB8500_ADC_CH_IBAT_MIN_V	(-60)	/* mV range measured by ADC for ibat */
#define AB8500_ADC_CH_IBAT_MAX_V	60
#define AB8500_GPADC_IBAT_VDROP_L	(-56)  /* mV */
#define AB8500_GPADC_IBAT_VDROP_H	56

/* This is used to not lose precision when dividing to get gain and offset */
#define AB8500_GPADC_CALIB_SCALE	1000
/*
 * Number of bits shift used to not lose precision
 * when dividing to get ibat gain.
 */
#define AB8500_GPADC_CALIB_SHIFT_IBAT	20

/* Time in ms before disabling regulator */
#define AB8500_GPADC_AUTOSUSPEND_DELAY	1

#define AB8500_GPADC_CONVERSION_TIME	500 /* ms */

enum ab8500_cal_channels {
	AB8500_CAL_VMAIN = 0,
	AB8500_CAL_BTEMP,
	AB8500_CAL_VBAT,
	AB8500_CAL_IBAT,
	AB8500_CAL_NR,
};

/**
 * struct ab8500_adc_cal_data - Table for storing gain and offset for the
 * calibrated ADC channels
 * @gain: Gain of the ADC channel
 * @offset: Offset of the ADC channel
 * @otp_calib_hi: Calibration from OTP
 * @otp_calib_lo: Calibration from OTP
 */
struct ab8500_adc_cal_data {
	s64 gain;
	s64 offset;
	u16 otp_calib_hi;
	u16 otp_calib_lo;
};

/**
 * struct ab8500_gpadc_chan_info - per-channel GPADC info
 * @name: name of the channel
 * @id: the internal AB8500 ID number for the channel
 * @hardware_control: indicate that we want to use hardware ADC control
 * on this channel, the default is software ADC control. Hardware control
 * is normally only used to test the battery voltage during GSM bursts
 * and needs a hardware trigger on the GPADCTrig pin of the ASIC.
 * @falling_edge: indicate that we want to trigger on falling edge
 * rather than rising edge, rising edge is the default
 * @avg_sample: how many samples to average: must be 1, 4, 8 or 16.
 * @trig_timer: how long to wait for the trigger, in 32kHz periods:
 * 0 .. 255 periods
 */
struct ab8500_gpadc_chan_info {
	const char *name;
	u8 id;
	bool hardware_control;
	bool falling_edge;
	u8 avg_sample;
	u8 trig_timer;
};

/**
 * struct ab8500_gpadc - AB8500 GPADC device information
 * @dev: pointer to the containing device
 * @ab8500: pointer to the parent AB8500 device
 * @chans: internal per-channel information container
 * @nchans: number of channels
 * @complete: pointer to the completion that indicates
 * the completion of an gpadc conversion cycle
 * @vddadc: pointer to the regulator supplying VDDADC
 * @irq_sw: interrupt number that is used by gpadc for software ADC conversion
 * @irq_hw: interrupt number that is used by gpadc for hardware ADC conversion
 * @cal_data: array of ADC calibration data structs
 */
struct ab8500_gpadc {
	struct device *dev;
	struct ab8500 *ab8500;
	struct ab8500_gpadc_chan_info *chans;
	unsigned int nchans;
	struct completion complete;
	struct regulator *vddadc;
	int irq_sw;
	int irq_hw;
	struct ab8500_adc_cal_data cal_data[AB8500_CAL_NR];
};

static struct ab8500_gpadc_chan_info *
ab8500_gpadc_get_channel(struct ab8500_gpadc *gpadc, u8 chan)
{
	struct ab8500_gpadc_chan_info *ch;
	int i;

	for (i = 0; i < gpadc->nchans; i++) {
		ch = &gpadc->chans[i];
		if (ch->id == chan)
			break;
	}
	if (i == gpadc->nchans)
		return NULL;

	return ch;
}

/**
 * ab8500_gpadc_ad_to_voltage() - Convert a raw ADC value to a voltage
 * @gpadc: GPADC instance
 * @ch: the sampled channel this raw value is coming from
 * @ad_value: the raw value
 */
static int ab8500_gpadc_ad_to_voltage(struct ab8500_gpadc *gpadc,
				      enum ab8500_gpadc_channel ch,
				      int ad_value)
{
	int res;

	switch (ch) {
	case AB8500_GPADC_CHAN_MAIN_CHARGER:
		/* No calibration data available: just interpolate */
		if (!gpadc->cal_data[AB8500_CAL_VMAIN].gain) {
			res = AB8500_ADC_CH_CHG_V_MIN + (AB8500_ADC_CH_CHG_V_MAX -
				AB8500_ADC_CH_CHG_V_MIN) * ad_value /
				AB8500_ADC_RESOLUTION;
			break;
		}
		/* Here we can use calibration */
		res = (int) (ad_value * gpadc->cal_data[AB8500_CAL_VMAIN].gain +
			gpadc->cal_data[AB8500_CAL_VMAIN].offset) / AB8500_GPADC_CALIB_SCALE;
		break;

	case AB8500_GPADC_CHAN_BAT_CTRL:
	case AB8500_GPADC_CHAN_BAT_TEMP:
	case AB8500_GPADC_CHAN_ACC_DET_1:
	case AB8500_GPADC_CHAN_ADC_AUX_1:
	case AB8500_GPADC_CHAN_ADC_AUX_2:
	case AB8500_GPADC_CHAN_XTAL_TEMP:
		/* No calibration data available: just interpolate */
		if (!gpadc->cal_data[AB8500_CAL_BTEMP].gain) {
			res = AB8500_ADC_CH_BTEMP_MIN + (AB8500_ADC_CH_BTEMP_MAX -
				AB8500_ADC_CH_BTEMP_MIN) * ad_value /
				AB8500_ADC_RESOLUTION;
			break;
		}
		/* Here we can use calibration */
		res = (int) (ad_value * gpadc->cal_data[AB8500_CAL_BTEMP].gain +
			gpadc->cal_data[AB8500_CAL_BTEMP].offset) / AB8500_GPADC_CALIB_SCALE;
		break;

	case AB8500_GPADC_CHAN_VBAT_A:
	case AB8500_GPADC_CHAN_VBAT_TRUE_MEAS:
		/* No calibration data available: just interpolate */
		if (!gpadc->cal_data[AB8500_CAL_VBAT].gain) {
			res = AB8500_ADC_CH_VBAT_MIN + (AB8500_ADC_CH_VBAT_MAX -
				AB8500_ADC_CH_VBAT_MIN) * ad_value /
				AB8500_ADC_RESOLUTION;
			break;
		}
		/* Here we can use calibration */
		res = (int) (ad_value * gpadc->cal_data[AB8500_CAL_VBAT].gain +
			gpadc->cal_data[AB8500_CAL_VBAT].offset) / AB8500_GPADC_CALIB_SCALE;
		break;

	case AB8505_GPADC_CHAN_DIE_TEMP:
		res = AB8500_ADC_CH_DIETEMP_MIN +
			(AB8500_ADC_CH_DIETEMP_MAX - AB8500_ADC_CH_DIETEMP_MIN) * ad_value /
			AB8500_ADC_RESOLUTION;
		break;

	case AB8500_GPADC_CHAN_ACC_DET_2:
		res = AB8500_ADC_CH_ACCDET2_MIN +
			(AB8500_ADC_CH_ACCDET2_MAX - AB8500_ADC_CH_ACCDET2_MIN) * ad_value /
			AB8500_ADC_RESOLUTION;
		break;

	case AB8500_GPADC_CHAN_VBUS:
		res = AB8500_ADC_CH_CHG_V_MIN +
			(AB8500_ADC_CH_CHG_V_MAX - AB8500_ADC_CH_CHG_V_MIN) * ad_value /
			AB8500_ADC_RESOLUTION;
		break;

	case AB8500_GPADC_CHAN_MAIN_CHARGER_CURRENT:
	case AB8500_GPADC_CHAN_USB_CHARGER_CURRENT:
		res = AB8500_ADC_CH_CHG_I_MIN +
			(AB8500_ADC_CH_CHG_I_MAX - AB8500_ADC_CH_CHG_I_MIN) * ad_value /
			AB8500_ADC_RESOLUTION;
		break;

	case AB8500_GPADC_CHAN_BACKUP_BAT:
		res = AB8500_ADC_CH_BKBAT_MIN +
			(AB8500_ADC_CH_BKBAT_MAX - AB8500_ADC_CH_BKBAT_MIN) * ad_value /
			AB8500_ADC_RESOLUTION;
		break;

	case AB8500_GPADC_CHAN_IBAT_VIRTUAL:
		/* No calibration data available: just interpolate */
		if (!gpadc->cal_data[AB8500_CAL_IBAT].gain) {
			res = AB8500_ADC_CH_IBAT_MIN + (AB8500_ADC_CH_IBAT_MAX -
				AB8500_ADC_CH_IBAT_MIN) * ad_value /
				AB8500_ADC_RESOLUTION;
			break;
		}
		/* Here we can use calibration */
		res = (int) (ad_value * gpadc->cal_data[AB8500_CAL_IBAT].gain +
				gpadc->cal_data[AB8500_CAL_IBAT].offset)
				>> AB8500_GPADC_CALIB_SHIFT_IBAT;
		break;

	default:
		dev_err(gpadc->dev,
			"unknown channel ID: %d, not possible to convert\n",
			ch);
		res = -EINVAL;
		break;

	}

	return res;
}

static int ab8500_gpadc_read(struct ab8500_gpadc *gpadc,
			     const struct ab8500_gpadc_chan_info *ch,
			     int *ibat)
{
	int ret;
	int looplimit = 0;
	unsigned long completion_timeout;
	u8 val;
	u8 low_data, high_data, low_data2, high_data2;
	u8 ctrl1;
	u8 ctrl23;
	unsigned int delay_min = 0;
	unsigned int delay_max = 0;
	u8 data_low_addr, data_high_addr;

	if (!gpadc)
		return -ENODEV;

	/* check if conversion is supported */
	if ((gpadc->irq_sw <= 0) && !ch->hardware_control)
		return -ENOTSUPP;
	if ((gpadc->irq_hw <= 0) && ch->hardware_control)
		return -ENOTSUPP;

	/* Enable vddadc by grabbing PM runtime */
	pm_runtime_get_sync(gpadc->dev);

	/* Check if ADC is not busy, lock and proceed */
	do {
		ret = abx500_get_register_interruptible(gpadc->dev,
			AB8500_GPADC, AB8500_GPADC_STAT_REG, &val);
		if (ret < 0)
			goto out;
		if (!(val & AB8500_GPADC_STAT_BUSY))
			break;
		msleep(20);
	} while (++looplimit < 10);
	if (looplimit >= 10 && (val & AB8500_GPADC_STAT_BUSY)) {
		dev_err(gpadc->dev, "gpadc_conversion: GPADC busy");
		ret = -EINVAL;
		goto out;
	}

	/* Enable GPADC */
	ctrl1 = AB8500_GPADC_CTRL1_ENABLE;

	/* Select the channel source and set average samples */
	switch (ch->avg_sample) {
	case 1:
		ctrl23 = ch->id | AB8500_GPADC_CTRL2_AVG_1;
		break;
	case 4:
		ctrl23 = ch->id | AB8500_GPADC_CTRL2_AVG_4;
		break;
	case 8:
		ctrl23 = ch->id | AB8500_GPADC_CTRL2_AVG_8;
		break;
	default:
		ctrl23 = ch->id | AB8500_GPADC_CTRL2_AVG_16;
		break;
	}

	if (ch->hardware_control) {
		ret = abx500_set_register_interruptible(gpadc->dev,
				AB8500_GPADC, AB8500_GPADC_CTRL3_REG, ctrl23);
		ctrl1 |= AB8500_GPADC_CTRL1_TRIG_ENA;
		if (ch->falling_edge)
			ctrl1 |= AB8500_GPADC_CTRL1_TRIG_EDGE;
	} else {
		ret = abx500_set_register_interruptible(gpadc->dev,
				AB8500_GPADC, AB8500_GPADC_CTRL2_REG, ctrl23);
	}
	if (ret < 0) {
		dev_err(gpadc->dev,
			"gpadc_conversion: set avg samples failed\n");
		goto out;
	}

	/*
	 * Enable ADC, buffering, select rising edge and enable ADC path
	 * charging current sense if it needed, ABB 3.0 needs some special
	 * treatment too.
	 */
	switch (ch->id) {
	case AB8500_GPADC_CHAN_MAIN_CHARGER_CURRENT:
	case AB8500_GPADC_CHAN_USB_CHARGER_CURRENT:
		ctrl1 |= AB8500_GPADC_CTRL1_BUF_ENA |
			AB8500_GPADC_CTRL1_ICHAR_ENA;
		break;
	case AB8500_GPADC_CHAN_BAT_TEMP:
		if (!is_ab8500_2p0_or_earlier(gpadc->ab8500)) {
			ctrl1 |= AB8500_GPADC_CTRL1_BUF_ENA |
				AB8500_GPADC_CTRL1_BTEMP_PULL_UP;
			/*
			 * Delay might be needed for ABB8500 cut 3.0, if not,
			 * remove when hardware will be available
			 */
			delay_min = 1000; /* Delay in micro seconds */
			delay_max = 10000; /* large range optimises sleepmode */
			break;
		}
		fallthrough;
	default:
		ctrl1 |= AB8500_GPADC_CTRL1_BUF_ENA;
		break;
	}

	/* Write configuration to control register 1 */
	ret = abx500_set_register_interruptible(gpadc->dev,
		AB8500_GPADC, AB8500_GPADC_CTRL1_REG, ctrl1);
	if (ret < 0) {
		dev_err(gpadc->dev,
			"gpadc_conversion: set Control register failed\n");
		goto out;
	}

	if (delay_min != 0)
		usleep_range(delay_min, delay_max);

	if (ch->hardware_control) {
		/* Set trigger delay timer */
		ret = abx500_set_register_interruptible(gpadc->dev,
			AB8500_GPADC, AB8500_GPADC_AUTO_TIMER_REG,
			ch->trig_timer);
		if (ret < 0) {
			dev_err(gpadc->dev,
				"gpadc_conversion: trig timer failed\n");
			goto out;
		}
		completion_timeout = 2 * HZ;
		data_low_addr = AB8500_GPADC_AUTODATAL_REG;
		data_high_addr = AB8500_GPADC_AUTODATAH_REG;
	} else {
		/* Start SW conversion */
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
			AB8500_GPADC, AB8500_GPADC_CTRL1_REG,
			AB8500_GPADC_CTRL1_START_SW_CONV,
			AB8500_GPADC_CTRL1_START_SW_CONV);
		if (ret < 0) {
			dev_err(gpadc->dev,
				"gpadc_conversion: start s/w conv failed\n");
			goto out;
		}
		completion_timeout = msecs_to_jiffies(AB8500_GPADC_CONVERSION_TIME);
		data_low_addr = AB8500_GPADC_MANDATAL_REG;
		data_high_addr = AB8500_GPADC_MANDATAH_REG;
	}

	/* Wait for completion of conversion */
	if (!wait_for_completion_timeout(&gpadc->complete,
			completion_timeout)) {
		dev_err(gpadc->dev,
			"timeout didn't receive GPADC conv interrupt\n");
		ret = -EINVAL;
		goto out;
	}

	/* Read the converted RAW data */
	ret = abx500_get_register_interruptible(gpadc->dev,
			AB8500_GPADC, data_low_addr, &low_data);
	if (ret < 0) {
		dev_err(gpadc->dev,
			"gpadc_conversion: read low data failed\n");
		goto out;
	}

	ret = abx500_get_register_interruptible(gpadc->dev,
		AB8500_GPADC, data_high_addr, &high_data);
	if (ret < 0) {
		dev_err(gpadc->dev,
			"gpadc_conversion: read high data failed\n");
		goto out;
	}

	/* Check if double conversion is required */
	if ((ch->id == AB8500_GPADC_CHAN_BAT_CTRL_AND_IBAT) ||
	    (ch->id == AB8500_GPADC_CHAN_VBAT_MEAS_AND_IBAT) ||
	    (ch->id == AB8500_GPADC_CHAN_VBAT_TRUE_MEAS_AND_IBAT) ||
	    (ch->id == AB8500_GPADC_CHAN_BAT_TEMP_AND_IBAT)) {

		if (ch->hardware_control) {
			/* not supported */
			ret = -ENOTSUPP;
			dev_err(gpadc->dev,
				"gpadc_conversion: only SW double conversion supported\n");
			goto out;
		} else {
			/* Read the converted RAW data 2 */
			ret = abx500_get_register_interruptible(gpadc->dev,
				AB8500_GPADC, AB8540_GPADC_MANDATA2L_REG,
				&low_data2);
			if (ret < 0) {
				dev_err(gpadc->dev,
					"gpadc_conversion: read sw low data 2 failed\n");
				goto out;
			}

			ret = abx500_get_register_interruptible(gpadc->dev,
				AB8500_GPADC, AB8540_GPADC_MANDATA2H_REG,
				&high_data2);
			if (ret < 0) {
				dev_err(gpadc->dev,
					"gpadc_conversion: read sw high data 2 failed\n");
				goto out;
			}
			if (ibat != NULL) {
				*ibat = (high_data2 << 8) | low_data2;
			} else {
				dev_warn(gpadc->dev,
					"gpadc_conversion: ibat not stored\n");
			}

		}
	}

	/* Disable GPADC */
	ret = abx500_set_register_interruptible(gpadc->dev, AB8500_GPADC,
		AB8500_GPADC_CTRL1_REG, AB8500_GPADC_CTRL1_DISABLE);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc_conversion: disable gpadc failed\n");
		goto out;
	}

	/* This eventually drops the regulator */
	pm_runtime_mark_last_busy(gpadc->dev);
	pm_runtime_put_autosuspend(gpadc->dev);

	return (high_data << 8) | low_data;

out:
	/*
	 * It has shown to be needed to turn off the GPADC if an error occurs,
	 * otherwise we might have problem when waiting for the busy bit in the
	 * GPADC status register to go low. In V1.1 there wait_for_completion
	 * seems to timeout when waiting for an interrupt.. Not seen in V2.0
	 */
	(void) abx500_set_register_interruptible(gpadc->dev, AB8500_GPADC,
		AB8500_GPADC_CTRL1_REG, AB8500_GPADC_CTRL1_DISABLE);
	pm_runtime_put(gpadc->dev);
	dev_err(gpadc->dev,
		"gpadc_conversion: Failed to AD convert channel %d\n", ch->id);

	return ret;
}

/**
 * ab8500_bm_gpadcconvend_handler() - isr for gpadc conversion completion
 * @irq: irq number
 * @data: pointer to the data passed during request irq
 *
 * This is a interrupt service routine for gpadc conversion completion.
 * Notifies the gpadc completion is completed and the converted raw value
 * can be read from the registers.
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_bm_gpadcconvend_handler(int irq, void *data)
{
	struct ab8500_gpadc *gpadc = data;

	complete(&gpadc->complete);

	return IRQ_HANDLED;
}

static int otp_cal_regs[] = {
	AB8500_GPADC_CAL_1,
	AB8500_GPADC_CAL_2,
	AB8500_GPADC_CAL_3,
	AB8500_GPADC_CAL_4,
	AB8500_GPADC_CAL_5,
	AB8500_GPADC_CAL_6,
	AB8500_GPADC_CAL_7,
};

static int otp4_cal_regs[] = {
	AB8540_GPADC_OTP4_REG_7,
	AB8540_GPADC_OTP4_REG_6,
	AB8540_GPADC_OTP4_REG_5,
};

static void ab8500_gpadc_read_calibration_data(struct ab8500_gpadc *gpadc)
{
	int i;
	int ret[ARRAY_SIZE(otp_cal_regs)];
	u8 gpadc_cal[ARRAY_SIZE(otp_cal_regs)];
	int ret_otp4[ARRAY_SIZE(otp4_cal_regs)];
	u8 gpadc_otp4[ARRAY_SIZE(otp4_cal_regs)];
	int vmain_high, vmain_low;
	int btemp_high, btemp_low;
	int vbat_high, vbat_low;
	int ibat_high, ibat_low;
	s64 V_gain, V_offset, V2A_gain, V2A_offset;

	/* First we read all OTP registers and store the error code */
	for (i = 0; i < ARRAY_SIZE(otp_cal_regs); i++) {
		ret[i] = abx500_get_register_interruptible(gpadc->dev,
			AB8500_OTP_EMUL, otp_cal_regs[i],  &gpadc_cal[i]);
		if (ret[i] < 0) {
			/* Continue anyway: maybe the other registers are OK */
			dev_err(gpadc->dev, "%s: read otp reg 0x%02x failed\n",
				__func__, otp_cal_regs[i]);
		} else {
			/* Put this in the entropy pool as device-unique */
			add_device_randomness(&ret[i], sizeof(ret[i]));
		}
	}

	/*
	 * The ADC calibration data is stored in OTP registers.
	 * The layout of the calibration data is outlined below and a more
	 * detailed description can be found in UM0836
	 *
	 * vm_h/l = vmain_high/low
	 * bt_h/l = btemp_high/low
	 * vb_h/l = vbat_high/low
	 *
	 * Data bits 8500/9540:
	 * | 7	   | 6	   | 5	   | 4	   | 3	   | 2	   | 1	   | 0
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * |						   | vm_h9 | vm_h8
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * |		   | vm_h7 | vm_h6 | vm_h5 | vm_h4 | vm_h3 | vm_h2
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * | vm_h1 | vm_h0 | vm_l4 | vm_l3 | vm_l2 | vm_l1 | vm_l0 | bt_h9
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * | bt_h8 | bt_h7 | bt_h6 | bt_h5 | bt_h4 | bt_h3 | bt_h2 | bt_h1
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * | bt_h0 | bt_l4 | bt_l3 | bt_l2 | bt_l1 | bt_l0 | vb_h9 | vb_h8
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * | vb_h7 | vb_h6 | vb_h5 | vb_h4 | vb_h3 | vb_h2 | vb_h1 | vb_h0
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * | vb_l5 | vb_l4 | vb_l3 | vb_l2 | vb_l1 | vb_l0 |
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 *
	 * Data bits 8540:
	 * OTP2
	 * | 7	   | 6	   | 5	   | 4	   | 3	   | 2	   | 1	   | 0
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * |
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * | vm_h9 | vm_h8 | vm_h7 | vm_h6 | vm_h5 | vm_h4 | vm_h3 | vm_h2
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * | vm_h1 | vm_h0 | vm_l4 | vm_l3 | vm_l2 | vm_l1 | vm_l0 | bt_h9
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * | bt_h8 | bt_h7 | bt_h6 | bt_h5 | bt_h4 | bt_h3 | bt_h2 | bt_h1
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * | bt_h0 | bt_l4 | bt_l3 | bt_l2 | bt_l1 | bt_l0 | vb_h9 | vb_h8
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * | vb_h7 | vb_h6 | vb_h5 | vb_h4 | vb_h3 | vb_h2 | vb_h1 | vb_h0
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * | vb_l5 | vb_l4 | vb_l3 | vb_l2 | vb_l1 | vb_l0 |
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 *
	 * Data bits 8540:
	 * OTP4
	 * | 7	   | 6	   | 5	   | 4	   | 3	   | 2	   | 1	   | 0
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * |					   | ib_h9 | ib_h8 | ib_h7
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * | ib_h6 | ib_h5 | ib_h4 | ib_h3 | ib_h2 | ib_h1 | ib_h0 | ib_l5
	 * |.......|.......|.......|.......|.......|.......|.......|.......
	 * | ib_l4 | ib_l3 | ib_l2 | ib_l1 | ib_l0 |
	 *
	 *
	 * Ideal output ADC codes corresponding to injected input voltages
	 * during manufacturing is:
	 *
	 * vmain_high: Vin = 19500mV / ADC ideal code = 997
	 * vmain_low:  Vin = 315mV   / ADC ideal code = 16
	 * btemp_high: Vin = 1300mV  / ADC ideal code = 985
	 * btemp_low:  Vin = 21mV    / ADC ideal code = 16
	 * vbat_high:  Vin = 4700mV  / ADC ideal code = 982
	 * vbat_low:   Vin = 2380mV  / ADC ideal code = 33
	 */

	if (is_ab8540(gpadc->ab8500)) {
		/* Calculate gain and offset for VMAIN if all reads succeeded*/
		if (!(ret[1] < 0 || ret[2] < 0)) {
			vmain_high = (((gpadc_cal[1] & 0xFF) << 2) |
				((gpadc_cal[2] & 0xC0) >> 6));
			vmain_low = ((gpadc_cal[2] & 0x3E) >> 1);

			gpadc->cal_data[AB8500_CAL_VMAIN].otp_calib_hi =
				(u16)vmain_high;
			gpadc->cal_data[AB8500_CAL_VMAIN].otp_calib_lo =
				(u16)vmain_low;

			gpadc->cal_data[AB8500_CAL_VMAIN].gain = AB8500_GPADC_CALIB_SCALE *
				(19500 - 315) / (vmain_high - vmain_low);
			gpadc->cal_data[AB8500_CAL_VMAIN].offset = AB8500_GPADC_CALIB_SCALE *
				19500 - (AB8500_GPADC_CALIB_SCALE * (19500 - 315) /
				(vmain_high - vmain_low)) * vmain_high;
		} else {
			gpadc->cal_data[AB8500_CAL_VMAIN].gain = 0;
		}

		/* Read IBAT calibration Data */
		for (i = 0; i < ARRAY_SIZE(otp4_cal_regs); i++) {
			ret_otp4[i] = abx500_get_register_interruptible(
					gpadc->dev, AB8500_OTP_EMUL,
					otp4_cal_regs[i],  &gpadc_otp4[i]);
			if (ret_otp4[i] < 0)
				dev_err(gpadc->dev,
					"%s: read otp4 reg 0x%02x failed\n",
					__func__, otp4_cal_regs[i]);
		}

		/* Calculate gain and offset for IBAT if all reads succeeded */
		if (!(ret_otp4[0] < 0 || ret_otp4[1] < 0 || ret_otp4[2] < 0)) {
			ibat_high = (((gpadc_otp4[0] & 0x07) << 7) |
				((gpadc_otp4[1] & 0xFE) >> 1));
			ibat_low = (((gpadc_otp4[1] & 0x01) << 5) |
				((gpadc_otp4[2] & 0xF8) >> 3));

			gpadc->cal_data[AB8500_CAL_IBAT].otp_calib_hi =
				(u16)ibat_high;
			gpadc->cal_data[AB8500_CAL_IBAT].otp_calib_lo =
				(u16)ibat_low;

			V_gain = ((AB8500_GPADC_IBAT_VDROP_H - AB8500_GPADC_IBAT_VDROP_L)
				<< AB8500_GPADC_CALIB_SHIFT_IBAT) / (ibat_high - ibat_low);

			V_offset = (AB8500_GPADC_IBAT_VDROP_H << AB8500_GPADC_CALIB_SHIFT_IBAT) -
				(((AB8500_GPADC_IBAT_VDROP_H - AB8500_GPADC_IBAT_VDROP_L) <<
				AB8500_GPADC_CALIB_SHIFT_IBAT) / (ibat_high - ibat_low))
				* ibat_high;
			/*
			 * Result obtained is in mV (at a scale factor),
			 * we need to calculate gain and offset to get mA
			 */
			V2A_gain = (AB8500_ADC_CH_IBAT_MAX - AB8500_ADC_CH_IBAT_MIN)/
				(AB8500_ADC_CH_IBAT_MAX_V - AB8500_ADC_CH_IBAT_MIN_V);
			V2A_offset = ((AB8500_ADC_CH_IBAT_MAX_V * AB8500_ADC_CH_IBAT_MIN -
				AB8500_ADC_CH_IBAT_MAX * AB8500_ADC_CH_IBAT_MIN_V)
				<< AB8500_GPADC_CALIB_SHIFT_IBAT)
				/ (AB8500_ADC_CH_IBAT_MAX_V - AB8500_ADC_CH_IBAT_MIN_V);

			gpadc->cal_data[AB8500_CAL_IBAT].gain =
				V_gain * V2A_gain;
			gpadc->cal_data[AB8500_CAL_IBAT].offset =
				V_offset * V2A_gain + V2A_offset;
		} else {
			gpadc->cal_data[AB8500_CAL_IBAT].gain = 0;
		}
	} else {
		/* Calculate gain and offset for VMAIN if all reads succeeded */
		if (!(ret[0] < 0 || ret[1] < 0 || ret[2] < 0)) {
			vmain_high = (((gpadc_cal[0] & 0x03) << 8) |
				((gpadc_cal[1] & 0x3F) << 2) |
				((gpadc_cal[2] & 0xC0) >> 6));
			vmain_low = ((gpadc_cal[2] & 0x3E) >> 1);

			gpadc->cal_data[AB8500_CAL_VMAIN].otp_calib_hi =
				(u16)vmain_high;
			gpadc->cal_data[AB8500_CAL_VMAIN].otp_calib_lo =
				(u16)vmain_low;

			gpadc->cal_data[AB8500_CAL_VMAIN].gain = AB8500_GPADC_CALIB_SCALE *
				(19500 - 315) / (vmain_high - vmain_low);

			gpadc->cal_data[AB8500_CAL_VMAIN].offset = AB8500_GPADC_CALIB_SCALE *
				19500 - (AB8500_GPADC_CALIB_SCALE * (19500 - 315) /
				(vmain_high - vmain_low)) * vmain_high;
		} else {
			gpadc->cal_data[AB8500_CAL_VMAIN].gain = 0;
		}
	}

	/* Calculate gain and offset for BTEMP if all reads succeeded */
	if (!(ret[2] < 0 || ret[3] < 0 || ret[4] < 0)) {
		btemp_high = (((gpadc_cal[2] & 0x01) << 9) |
			(gpadc_cal[3] << 1) | ((gpadc_cal[4] & 0x80) >> 7));
		btemp_low = ((gpadc_cal[4] & 0x7C) >> 2);

		gpadc->cal_data[AB8500_CAL_BTEMP].otp_calib_hi = (u16)btemp_high;
		gpadc->cal_data[AB8500_CAL_BTEMP].otp_calib_lo = (u16)btemp_low;

		gpadc->cal_data[AB8500_CAL_BTEMP].gain =
			AB8500_GPADC_CALIB_SCALE * (1300 - 21) / (btemp_high - btemp_low);
		gpadc->cal_data[AB8500_CAL_BTEMP].offset = AB8500_GPADC_CALIB_SCALE * 1300 -
			(AB8500_GPADC_CALIB_SCALE * (1300 - 21) / (btemp_high - btemp_low))
			* btemp_high;
	} else {
		gpadc->cal_data[AB8500_CAL_BTEMP].gain = 0;
	}

	/* Calculate gain and offset for VBAT if all reads succeeded */
	if (!(ret[4] < 0 || ret[5] < 0 || ret[6] < 0)) {
		vbat_high = (((gpadc_cal[4] & 0x03) << 8) | gpadc_cal[5]);
		vbat_low = ((gpadc_cal[6] & 0xFC) >> 2);

		gpadc->cal_data[AB8500_CAL_VBAT].otp_calib_hi = (u16)vbat_high;
		gpadc->cal_data[AB8500_CAL_VBAT].otp_calib_lo = (u16)vbat_low;

		gpadc->cal_data[AB8500_CAL_VBAT].gain = AB8500_GPADC_CALIB_SCALE *
			(4700 - 2380) /	(vbat_high - vbat_low);
		gpadc->cal_data[AB8500_CAL_VBAT].offset = AB8500_GPADC_CALIB_SCALE * 4700 -
			(AB8500_GPADC_CALIB_SCALE * (4700 - 2380) /
			(vbat_high - vbat_low)) * vbat_high;
	} else {
		gpadc->cal_data[AB8500_CAL_VBAT].gain = 0;
	}
}

static int ab8500_gpadc_read_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int *val, int *val2, long mask)
{
	struct ab8500_gpadc *gpadc = iio_priv(indio_dev);
	const struct ab8500_gpadc_chan_info *ch;
	int raw_val;
	int processed;

	ch = ab8500_gpadc_get_channel(gpadc, chan->address);
	if (!ch) {
		dev_err(gpadc->dev, "no such channel %lu\n",
			chan->address);
		return -EINVAL;
	}

	raw_val = ab8500_gpadc_read(gpadc, ch, NULL);
	if (raw_val < 0)
		return raw_val;

	if (mask == IIO_CHAN_INFO_RAW) {
		*val = raw_val;
		return IIO_VAL_INT;
	}

	if (mask == IIO_CHAN_INFO_PROCESSED) {
		processed = ab8500_gpadc_ad_to_voltage(gpadc, ch->id, raw_val);
		if (processed < 0)
			return processed;

		/* Return millivolt or milliamps or millicentigrades */
		*val = processed;
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int ab8500_gpadc_fwnode_xlate(struct iio_dev *indio_dev,
				     const struct fwnode_reference_args *iiospec)
{
	int i;

	for (i = 0; i < indio_dev->num_channels; i++)
		if (indio_dev->channels[i].channel == iiospec->args[0])
			return i;

	return -EINVAL;
}

static const struct iio_info ab8500_gpadc_info = {
	.fwnode_xlate = ab8500_gpadc_fwnode_xlate,
	.read_raw = ab8500_gpadc_read_raw,
};

static int ab8500_gpadc_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ab8500_gpadc *gpadc = iio_priv(indio_dev);

	regulator_disable(gpadc->vddadc);

	return 0;
}

static int ab8500_gpadc_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ab8500_gpadc *gpadc = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(gpadc->vddadc);
	if (ret)
		dev_err(dev, "Failed to enable vddadc: %d\n", ret);

	return ret;
}

/**
 * ab8500_gpadc_parse_channel() - process devicetree channel configuration
 * @dev: pointer to containing device
 * @fwnode: fw node for the channel to configure
 * @ch: channel info to fill in
 * @iio_chan: IIO channel specification to fill in
 *
 * The devicetree will set up the channel for use with the specific device,
 * and define usage for things like AUX GPADC inputs more precisely.
 */
static int ab8500_gpadc_parse_channel(struct device *dev,
				      struct fwnode_handle *fwnode,
				      struct ab8500_gpadc_chan_info *ch,
				      struct iio_chan_spec *iio_chan)
{
	const char *name = fwnode_get_name(fwnode);
	u32 chan;
	int ret;

	ret = fwnode_property_read_u32(fwnode, "reg", &chan);
	if (ret) {
		dev_err(dev, "invalid channel number %s\n", name);
		return ret;
	}
	if (chan > AB8500_GPADC_CHAN_BAT_TEMP_AND_IBAT) {
		dev_err(dev, "%s channel number out of range %d\n", name, chan);
		return -EINVAL;
	}

	iio_chan->channel = chan;
	iio_chan->datasheet_name = name;
	iio_chan->indexed = 1;
	iio_chan->address = chan;
	iio_chan->info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_PROCESSED);
	/* Most are voltages (also temperatures), some are currents */
	if ((chan == AB8500_GPADC_CHAN_MAIN_CHARGER_CURRENT) ||
	    (chan == AB8500_GPADC_CHAN_USB_CHARGER_CURRENT))
		iio_chan->type = IIO_CURRENT;
	else
		iio_chan->type = IIO_VOLTAGE;

	ch->id = chan;

	/* Sensible defaults */
	ch->avg_sample = 16;
	ch->hardware_control = false;
	ch->falling_edge = false;
	ch->trig_timer = 0;

	return 0;
}

/**
 * ab8500_gpadc_parse_channels() - Parse the GPADC channels from DT
 * @gpadc: the GPADC to configure the channels for
 * @chans: the IIO channels we parsed
 * @nchans: the number of IIO channels we parsed
 */
static int ab8500_gpadc_parse_channels(struct ab8500_gpadc *gpadc,
				       struct iio_chan_spec **chans_parsed,
				       unsigned int *nchans_parsed)
{
	struct fwnode_handle *child;
	struct ab8500_gpadc_chan_info *ch;
	struct iio_chan_spec *iio_chans;
	unsigned int nchans;
	int i;

	nchans = device_get_child_node_count(gpadc->dev);
	if (!nchans) {
		dev_err(gpadc->dev, "no channel children\n");
		return -ENODEV;
	}
	dev_info(gpadc->dev, "found %d ADC channels\n", nchans);

	iio_chans = devm_kcalloc(gpadc->dev, nchans,
				 sizeof(*iio_chans), GFP_KERNEL);
	if (!iio_chans)
		return -ENOMEM;

	gpadc->chans = devm_kcalloc(gpadc->dev, nchans,
				    sizeof(*gpadc->chans), GFP_KERNEL);
	if (!gpadc->chans)
		return -ENOMEM;

	i = 0;
	device_for_each_child_node(gpadc->dev, child) {
		struct iio_chan_spec *iio_chan;
		int ret;

		ch = &gpadc->chans[i];
		iio_chan = &iio_chans[i];

		ret = ab8500_gpadc_parse_channel(gpadc->dev, child, ch,
						 iio_chan);
		if (ret) {
			fwnode_handle_put(child);
			return ret;
		}
		i++;
	}
	gpadc->nchans = nchans;
	*chans_parsed = iio_chans;
	*nchans_parsed = nchans;

	return 0;
}

static int ab8500_gpadc_probe(struct platform_device *pdev)
{
	struct ab8500_gpadc *gpadc;
	struct iio_dev *indio_dev;
	struct device *dev = &pdev->dev;
	struct iio_chan_spec *iio_chans;
	unsigned int n_iio_chans;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*gpadc));
	if (!indio_dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, indio_dev);
	gpadc = iio_priv(indio_dev);

	gpadc->dev = dev;
	gpadc->ab8500 = dev_get_drvdata(dev->parent);

	ret = ab8500_gpadc_parse_channels(gpadc, &iio_chans, &n_iio_chans);
	if (ret)
		return ret;

	gpadc->irq_sw = platform_get_irq_byname(pdev, "SW_CONV_END");
	if (gpadc->irq_sw < 0)
		return gpadc->irq_sw;

	if (is_ab8500(gpadc->ab8500)) {
		gpadc->irq_hw = platform_get_irq_byname(pdev, "HW_CONV_END");
		if (gpadc->irq_hw < 0)
			return gpadc->irq_hw;
	} else {
		gpadc->irq_hw = 0;
	}

	/* Initialize completion used to notify completion of conversion */
	init_completion(&gpadc->complete);

	/* Request interrupts */
	ret = devm_request_threaded_irq(dev, gpadc->irq_sw, NULL,
		ab8500_bm_gpadcconvend_handler,	IRQF_NO_SUSPEND | IRQF_ONESHOT,
		"ab8500-gpadc-sw", gpadc);
	if (ret < 0) {
		dev_err(dev,
			"failed to request sw conversion irq %d\n",
			gpadc->irq_sw);
		return ret;
	}

	if (gpadc->irq_hw) {
		ret = devm_request_threaded_irq(dev, gpadc->irq_hw, NULL,
			ab8500_bm_gpadcconvend_handler,	IRQF_NO_SUSPEND | IRQF_ONESHOT,
			"ab8500-gpadc-hw", gpadc);
		if (ret < 0) {
			dev_err(dev,
				"Failed to request hw conversion irq: %d\n",
				gpadc->irq_hw);
			return ret;
		}
	}

	/* The VTVout LDO used to power the AB8500 GPADC */
	gpadc->vddadc = devm_regulator_get(dev, "vddadc");
	if (IS_ERR(gpadc->vddadc))
		return dev_err_probe(dev, PTR_ERR(gpadc->vddadc),
				     "failed to get vddadc\n");

	ret = regulator_enable(gpadc->vddadc);
	if (ret) {
		dev_err(dev, "failed to enable vddadc: %d\n", ret);
		return ret;
	}

	/* Enable runtime PM */
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, AB8500_GPADC_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);

	ab8500_gpadc_read_calibration_data(gpadc);

	pm_runtime_put(dev);

	indio_dev->name = "ab8500-gpadc";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ab8500_gpadc_info;
	indio_dev->channels = iio_chans;
	indio_dev->num_channels = n_iio_chans;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		goto out_dis_pm;

	return 0;

out_dis_pm:
	pm_runtime_get_sync(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
	regulator_disable(gpadc->vddadc);

	return ret;
}

static int ab8500_gpadc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct ab8500_gpadc *gpadc = iio_priv(indio_dev);

	pm_runtime_get_sync(gpadc->dev);
	pm_runtime_put_noidle(gpadc->dev);
	pm_runtime_disable(gpadc->dev);
	regulator_disable(gpadc->vddadc);

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(ab8500_gpadc_pm_ops,
				 ab8500_gpadc_runtime_suspend,
				 ab8500_gpadc_runtime_resume, NULL);

static struct platform_driver ab8500_gpadc_driver = {
	.probe = ab8500_gpadc_probe,
	.remove = ab8500_gpadc_remove,
	.driver = {
		.name = "ab8500-gpadc",
		.pm = pm_ptr(&ab8500_gpadc_pm_ops),
	},
};
builtin_platform_driver(ab8500_gpadc_driver);
