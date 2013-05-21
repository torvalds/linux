/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 * Author: Arun R Murthy <arun.murthy@stericsson.com>
 * Author: Daniel Willerud <daniel.willerud@stericsson.com>
 * Author: Johan Palsson <johan.palsson@stericsson.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/completion.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>

/*
 * GPADC register offsets
 * Bank : 0x0A
 */
#define AB8500_GPADC_CTRL1_REG		0x00
#define AB8500_GPADC_CTRL2_REG		0x01
#define AB8500_GPADC_CTRL3_REG		0x02
#define AB8500_GPADC_AUTO_TIMER_REG	0x03
#define AB8500_GPADC_STAT_REG		0x04
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
#define AB8500_GPADC_CAL_1		0x0F
#define AB8500_GPADC_CAL_2		0x10
#define AB8500_GPADC_CAL_3		0x11
#define AB8500_GPADC_CAL_4		0x12
#define AB8500_GPADC_CAL_5		0x13
#define AB8500_GPADC_CAL_6		0x14
#define AB8500_GPADC_CAL_7		0x15
/* New calibration for 8540 */
#define AB8540_GPADC_OTP4_REG_7	0x38
#define AB8540_GPADC_OTP4_REG_6	0x39
#define AB8540_GPADC_OTP4_REG_5	0x3A

/* gpadc constants */
#define EN_VINTCORE12			0x04
#define EN_VTVOUT			0x02
#define EN_GPADC			0x01
#define DIS_GPADC			0x00
#define AVG_1				0x00
#define AVG_4				0x20
#define AVG_8				0x40
#define AVG_16				0x60
#define ADC_SW_CONV			0x04
#define EN_ICHAR			0x80
#define BTEMP_PULL_UP			0x08
#define EN_BUF				0x40
#define DIS_ZERO			0x00
#define GPADC_BUSY			0x01
#define EN_FALLING			0x10
#define EN_TRIG_EDGE			0x02
#define EN_VBIAS_XTAL_TEMP		0x02

/* GPADC constants from AB8500 spec, UM0836 */
#define ADC_RESOLUTION			1024
#define ADC_CH_BTEMP_MIN		0
#define ADC_CH_BTEMP_MAX		1350
#define ADC_CH_DIETEMP_MIN		0
#define ADC_CH_DIETEMP_MAX		1350
#define ADC_CH_CHG_V_MIN		0
#define ADC_CH_CHG_V_MAX		20030
#define ADC_CH_ACCDET2_MIN		0
#define ADC_CH_ACCDET2_MAX		2500
#define ADC_CH_VBAT_MIN			2300
#define ADC_CH_VBAT_MAX			4800
#define ADC_CH_CHG_I_MIN		0
#define ADC_CH_CHG_I_MAX		1500
#define ADC_CH_BKBAT_MIN		0
#define ADC_CH_BKBAT_MAX		3200

/* GPADC constants from AB8540 spec */
#define ADC_CH_IBAT_MIN			(-6000) /* mA range measured by ADC for ibat*/
#define ADC_CH_IBAT_MAX			6000
#define ADC_CH_IBAT_MIN_V		(-60)	/* mV range measured by ADC for ibat*/
#define ADC_CH_IBAT_MAX_V		60
#define IBAT_VDROP_L			(-56)  /* mV */
#define IBAT_VDROP_H			56

/* This is used to not lose precision when dividing to get gain and offset */
#define CALIB_SCALE		1000
/*
 * Number of bits shift used to not lose precision
 * when dividing to get ibat gain.
 */
#define CALIB_SHIFT_IBAT	20

/* Time in ms before disabling regulator */
#define GPADC_AUDOSUSPEND_DELAY		1

#define CONVERSION_TIME			500 /* ms */

enum cal_channels {
	ADC_INPUT_VMAIN = 0,
	ADC_INPUT_BTEMP,
	ADC_INPUT_VBAT,
	ADC_INPUT_IBAT,
	NBR_CAL_INPUTS,
};

/**
 * struct adc_cal_data - Table for storing gain and offset for the calibrated
 * ADC channels
 * @gain:		Gain of the ADC channel
 * @offset:		Offset of the ADC channel
 */
struct adc_cal_data {
	s64 gain;
	s64 offset;
	u16 otp_calib_hi;
	u16 otp_calib_lo;
};

/**
 * struct ab8500_gpadc - AB8500 GPADC device information
 * @dev:			pointer to the struct device
 * @node:			a list of AB8500 GPADCs, hence prepared for
				reentrance
 * @parent:			pointer to the struct ab8500
 * @ab8500_gpadc_complete:	pointer to the struct completion, to indicate
 *				the completion of gpadc conversion
 * @ab8500_gpadc_lock:		structure of type mutex
 * @regu:			pointer to the struct regulator
 * @irq_sw:			interrupt number that is used by gpadc for Sw
 *				conversion
 * @irq_hw:			interrupt number that is used by gpadc for Hw
 *				conversion
 * @cal_data			array of ADC calibration data structs
 */
struct ab8500_gpadc {
	struct device *dev;
	struct list_head node;
	struct ab8500 *parent;
	struct completion ab8500_gpadc_complete;
	struct mutex ab8500_gpadc_lock;
	struct regulator *regu;
	int irq_sw;
	int irq_hw;
	struct adc_cal_data cal_data[NBR_CAL_INPUTS];
};

static LIST_HEAD(ab8500_gpadc_list);

/**
 * ab8500_gpadc_get() - returns a reference to the primary AB8500 GPADC
 * (i.e. the first GPADC in the instance list)
 */
struct ab8500_gpadc *ab8500_gpadc_get(char *name)
{
	struct ab8500_gpadc *gpadc;

	list_for_each_entry(gpadc, &ab8500_gpadc_list, node) {
		if (!strcmp(name, dev_name(gpadc->dev)))
		    return gpadc;
	}

	return ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(ab8500_gpadc_get);

/**
 * ab8500_gpadc_ad_to_voltage() - Convert a raw ADC value to a voltage
 */
int ab8500_gpadc_ad_to_voltage(struct ab8500_gpadc *gpadc, u8 channel,
	int ad_value)
{
	int res;

	switch (channel) {
	case MAIN_CHARGER_V:
		/* For some reason we don't have calibrated data */
		if (!gpadc->cal_data[ADC_INPUT_VMAIN].gain) {
			res = ADC_CH_CHG_V_MIN + (ADC_CH_CHG_V_MAX -
				ADC_CH_CHG_V_MIN) * ad_value /
				ADC_RESOLUTION;
			break;
		}
		/* Here we can use the calibrated data */
		res = (int) (ad_value * gpadc->cal_data[ADC_INPUT_VMAIN].gain +
			gpadc->cal_data[ADC_INPUT_VMAIN].offset) / CALIB_SCALE;
		break;

	case XTAL_TEMP:
	case BAT_CTRL:
	case BTEMP_BALL:
	case ACC_DETECT1:
	case ADC_AUX1:
	case ADC_AUX2:
		/* For some reason we don't have calibrated data */
		if (!gpadc->cal_data[ADC_INPUT_BTEMP].gain) {
			res = ADC_CH_BTEMP_MIN + (ADC_CH_BTEMP_MAX -
				ADC_CH_BTEMP_MIN) * ad_value /
				ADC_RESOLUTION;
			break;
		}
		/* Here we can use the calibrated data */
		res = (int) (ad_value * gpadc->cal_data[ADC_INPUT_BTEMP].gain +
			gpadc->cal_data[ADC_INPUT_BTEMP].offset) / CALIB_SCALE;
		break;

	case MAIN_BAT_V:
	case VBAT_TRUE_MEAS:
		/* For some reason we don't have calibrated data */
		if (!gpadc->cal_data[ADC_INPUT_VBAT].gain) {
			res = ADC_CH_VBAT_MIN + (ADC_CH_VBAT_MAX -
				ADC_CH_VBAT_MIN) * ad_value /
				ADC_RESOLUTION;
			break;
		}
		/* Here we can use the calibrated data */
		res = (int) (ad_value * gpadc->cal_data[ADC_INPUT_VBAT].gain +
			gpadc->cal_data[ADC_INPUT_VBAT].offset) / CALIB_SCALE;
		break;

	case DIE_TEMP:
		res = ADC_CH_DIETEMP_MIN +
			(ADC_CH_DIETEMP_MAX - ADC_CH_DIETEMP_MIN) * ad_value /
			ADC_RESOLUTION;
		break;

	case ACC_DETECT2:
		res = ADC_CH_ACCDET2_MIN +
			(ADC_CH_ACCDET2_MAX - ADC_CH_ACCDET2_MIN) * ad_value /
			ADC_RESOLUTION;
		break;

	case VBUS_V:
		res = ADC_CH_CHG_V_MIN +
			(ADC_CH_CHG_V_MAX - ADC_CH_CHG_V_MIN) * ad_value /
			ADC_RESOLUTION;
		break;

	case MAIN_CHARGER_C:
	case USB_CHARGER_C:
		res = ADC_CH_CHG_I_MIN +
			(ADC_CH_CHG_I_MAX - ADC_CH_CHG_I_MIN) * ad_value /
			ADC_RESOLUTION;
		break;

	case BK_BAT_V:
		res = ADC_CH_BKBAT_MIN +
			(ADC_CH_BKBAT_MAX - ADC_CH_BKBAT_MIN) * ad_value /
			ADC_RESOLUTION;
		break;

	case IBAT_VIRTUAL_CHANNEL:
		/* For some reason we don't have calibrated data */
		if (!gpadc->cal_data[ADC_INPUT_IBAT].gain) {
			res = ADC_CH_IBAT_MIN + (ADC_CH_IBAT_MAX -
				ADC_CH_IBAT_MIN) * ad_value /
				ADC_RESOLUTION;
			break;
		}
		/* Here we can use the calibrated data */
		res = (int) (ad_value * gpadc->cal_data[ADC_INPUT_IBAT].gain +
				gpadc->cal_data[ADC_INPUT_IBAT].offset)
				>> CALIB_SHIFT_IBAT;
		break;

	default:
		dev_err(gpadc->dev,
			"unknown channel, not possible to convert\n");
		res = -EINVAL;
		break;

	}
	return res;
}
EXPORT_SYMBOL(ab8500_gpadc_ad_to_voltage);

/**
 * ab8500_gpadc_sw_hw_convert() - gpadc conversion
 * @channel:	analog channel to be converted to digital data
 * @avg_sample:  number of ADC sample to average
 * @trig_egde:  selected ADC trig edge
 * @trig_timer: selected ADC trigger delay timer
 * @conv_type: selected conversion type (HW or SW conversion)
 *
 * This function converts the selected analog i/p to digital
 * data.
 */
int ab8500_gpadc_sw_hw_convert(struct ab8500_gpadc *gpadc, u8 channel,
		u8 avg_sample, u8 trig_edge, u8 trig_timer, u8 conv_type)
{
	int ad_value;
	int voltage;

	ad_value = ab8500_gpadc_read_raw(gpadc, channel, avg_sample,
			trig_edge, trig_timer, conv_type);
/* On failure retry a second time */
	if (ad_value < 0)
		ad_value = ab8500_gpadc_read_raw(gpadc, channel, avg_sample,
			trig_edge, trig_timer, conv_type);
if (ad_value < 0) {
		dev_err(gpadc->dev, "GPADC raw value failed ch: %d\n",
				channel);
		return ad_value;
	}

	voltage = ab8500_gpadc_ad_to_voltage(gpadc, channel, ad_value);
	if (voltage < 0)
		dev_err(gpadc->dev, "GPADC to voltage conversion failed ch:"
			" %d AD: 0x%x\n", channel, ad_value);

	return voltage;
}
EXPORT_SYMBOL(ab8500_gpadc_sw_hw_convert);

/**
 * ab8500_gpadc_read_raw() - gpadc read
 * @channel:	analog channel to be read
 * @avg_sample:  number of ADC sample to average
 * @trig_edge:  selected trig edge
 * @trig_timer: selected ADC trigger delay timer
 * @conv_type: selected conversion type (HW or SW conversion)
 *
 * This function obtains the raw ADC value for an hardware conversion,
 * this then needs to be converted by calling ab8500_gpadc_ad_to_voltage()
 */
int ab8500_gpadc_read_raw(struct ab8500_gpadc *gpadc, u8 channel,
		u8 avg_sample, u8 trig_edge, u8 trig_timer, u8 conv_type)
{
	int raw_data;
	raw_data = ab8500_gpadc_double_read_raw(gpadc, channel,
			avg_sample, trig_edge, trig_timer, conv_type, NULL);
	return raw_data;
}

int ab8500_gpadc_double_read_raw(struct ab8500_gpadc *gpadc, u8 channel,
		u8 avg_sample, u8 trig_edge, u8 trig_timer, u8 conv_type,
		int *ibat)
{
	int ret;
	int looplimit = 0;
	unsigned long completion_timeout;
	u8 val, low_data, high_data, low_data2, high_data2;
	u8 val_reg1 = 0;
	unsigned int delay_min = 0;
	unsigned int delay_max = 0;
	u8 data_low_addr, data_high_addr;

	if (!gpadc)
		return -ENODEV;

	/* check if convertion is supported */
	if ((gpadc->irq_sw < 0) && (conv_type == ADC_SW))
		return -ENOTSUPP;
	if ((gpadc->irq_hw < 0) && (conv_type == ADC_HW))
		return -ENOTSUPP;

	mutex_lock(&gpadc->ab8500_gpadc_lock);
	/* Enable VTVout LDO this is required for GPADC */
	pm_runtime_get_sync(gpadc->dev);

	/* Check if ADC is not busy, lock and proceed */
	do {
		ret = abx500_get_register_interruptible(gpadc->dev,
			AB8500_GPADC, AB8500_GPADC_STAT_REG, &val);
		if (ret < 0)
			goto out;
		if (!(val & GPADC_BUSY))
			break;
		msleep(10);
	} while (++looplimit < 10);
	if (looplimit >= 10 && (val & GPADC_BUSY)) {
		dev_err(gpadc->dev, "gpadc_conversion: GPADC busy");
		ret = -EINVAL;
		goto out;
	}

	/* Enable GPADC */
	val_reg1 |= EN_GPADC;

	/* Select the channel source and set average samples */
	switch (avg_sample) {
	case SAMPLE_1:
		val = channel | AVG_1;
		break;
	case SAMPLE_4:
		val = channel | AVG_4;
		break;
	case SAMPLE_8:
		val = channel | AVG_8;
		break;
	default:
		val = channel | AVG_16;
		break;
	}

	if (conv_type == ADC_HW) {
		ret = abx500_set_register_interruptible(gpadc->dev,
				AB8500_GPADC, AB8500_GPADC_CTRL3_REG, val);
		val_reg1 |= EN_TRIG_EDGE;
		if (trig_edge)
			val_reg1 |= EN_FALLING;
	}
	else
		ret = abx500_set_register_interruptible(gpadc->dev,
				AB8500_GPADC, AB8500_GPADC_CTRL2_REG, val);
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
	switch (channel) {
	case MAIN_CHARGER_C:
	case USB_CHARGER_C:
		val_reg1 |= EN_BUF | EN_ICHAR;
		break;
	case BTEMP_BALL:
		if (!is_ab8500_2p0_or_earlier(gpadc->parent)) {
			val_reg1 |= EN_BUF | BTEMP_PULL_UP;
			/*
			* Delay might be needed for ABB8500 cut 3.0, if not,
			* remove when hardware will be availible
			*/
			delay_min = 1000; /* Delay in micro seconds */
			delay_max = 10000; /* large range to optimise sleep mode */
			break;
		}
		/* Intentional fallthrough */
	default:
		val_reg1 |= EN_BUF;
		break;
	}

	/* Write configuration to register */
	ret = abx500_set_register_interruptible(gpadc->dev,
		AB8500_GPADC, AB8500_GPADC_CTRL1_REG, val_reg1);
	if (ret < 0) {
		dev_err(gpadc->dev,
			"gpadc_conversion: set Control register failed\n");
		goto out;
	}

	if (delay_min != 0)
		usleep_range(delay_min, delay_max);

	if (conv_type == ADC_HW) {
		/* Set trigger delay timer */
		ret = abx500_set_register_interruptible(gpadc->dev,
			AB8500_GPADC, AB8500_GPADC_AUTO_TIMER_REG, trig_timer);
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
			ADC_SW_CONV, ADC_SW_CONV);
		if (ret < 0) {
			dev_err(gpadc->dev,
				"gpadc_conversion: start s/w conv failed\n");
			goto out;
		}
		completion_timeout = msecs_to_jiffies(CONVERSION_TIME);
		data_low_addr = AB8500_GPADC_MANDATAL_REG;
		data_high_addr = AB8500_GPADC_MANDATAH_REG;
	}

	/* wait for completion of conversion */
	if (!wait_for_completion_timeout(&gpadc->ab8500_gpadc_complete,
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
		dev_err(gpadc->dev, "gpadc_conversion: read low data failed\n");
		goto out;
	}

	ret = abx500_get_register_interruptible(gpadc->dev,
		AB8500_GPADC, data_high_addr, &high_data);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc_conversion: read high data failed\n");
		goto out;
	}

	/* Check if double convertion is required */
	if ((channel == BAT_CTRL_AND_IBAT) ||
			(channel == VBAT_MEAS_AND_IBAT) ||
			(channel == VBAT_TRUE_MEAS_AND_IBAT) ||
			(channel == BAT_TEMP_AND_IBAT)) {

		if (conv_type == ADC_HW) {
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
		AB8500_GPADC_CTRL1_REG, DIS_GPADC);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc_conversion: disable gpadc failed\n");
		goto out;
	}

	/* Disable VTVout LDO this is required for GPADC */
	pm_runtime_mark_last_busy(gpadc->dev);
	pm_runtime_put_autosuspend(gpadc->dev);

	mutex_unlock(&gpadc->ab8500_gpadc_lock);

	return (high_data << 8) | low_data;

out:
	/*
	 * It has shown to be needed to turn off the GPADC if an error occurs,
	 * otherwise we might have problem when waiting for the busy bit in the
	 * GPADC status register to go low. In V1.1 there wait_for_completion
	 * seems to timeout when waiting for an interrupt.. Not seen in V2.0
	 */
	(void) abx500_set_register_interruptible(gpadc->dev, AB8500_GPADC,
		AB8500_GPADC_CTRL1_REG, DIS_GPADC);
	pm_runtime_put(gpadc->dev);
	mutex_unlock(&gpadc->ab8500_gpadc_lock);
	dev_err(gpadc->dev,
		"gpadc_conversion: Failed to AD convert channel %d\n", channel);
	return ret;
}
EXPORT_SYMBOL(ab8500_gpadc_read_raw);

/**
 * ab8500_bm_gpadcconvend_handler() - isr for gpadc conversion completion
 * @irq:	irq number
 * @data:	pointer to the data passed during request irq
 *
 * This is a interrupt service routine for gpadc conversion completion.
 * Notifies the gpadc completion is completed and the converted raw value
 * can be read from the registers.
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_bm_gpadcconvend_handler(int irq, void *_gpadc)
{
	struct ab8500_gpadc *gpadc = _gpadc;

	complete(&gpadc->ab8500_gpadc_complete);

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
	struct ab8500 *ab8500;

	ab8500 = gpadc->parent;

	/* First we read all OTP registers and store the error code */
	for (i = 0; i < ARRAY_SIZE(otp_cal_regs); i++) {
		ret[i] = abx500_get_register_interruptible(gpadc->dev,
			AB8500_OTP_EMUL, otp_cal_regs[i],  &gpadc_cal[i]);
		if (ret[i] < 0)
			dev_err(gpadc->dev, "%s: read otp reg 0x%02x failed\n",
				__func__, otp_cal_regs[i]);
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

	if (is_ab8540(ab8500)) {
		/* Calculate gain and offset for VMAIN if all reads succeeded*/
		if (!(ret[1] < 0 || ret[2] < 0)) {
			vmain_high = (((gpadc_cal[1] & 0xFF) << 2) |
				((gpadc_cal[2] & 0xC0) >> 6));
			vmain_low = ((gpadc_cal[2] & 0x3E) >> 1);

			gpadc->cal_data[ADC_INPUT_VMAIN].otp_calib_hi =
				(u16)vmain_high;
			gpadc->cal_data[ADC_INPUT_VMAIN].otp_calib_lo =
				(u16)vmain_low;

			gpadc->cal_data[ADC_INPUT_VMAIN].gain = CALIB_SCALE *
				(19500 - 315) / (vmain_high - vmain_low);
			gpadc->cal_data[ADC_INPUT_VMAIN].offset = CALIB_SCALE *
				19500 - (CALIB_SCALE * (19500 - 315) /
				(vmain_high - vmain_low)) * vmain_high;
		} else {
		gpadc->cal_data[ADC_INPUT_VMAIN].gain = 0;
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

			gpadc->cal_data[ADC_INPUT_IBAT].otp_calib_hi =
				(u16)ibat_high;
			gpadc->cal_data[ADC_INPUT_IBAT].otp_calib_lo =
				(u16)ibat_low;

			V_gain = ((IBAT_VDROP_H - IBAT_VDROP_L)
				<< CALIB_SHIFT_IBAT) / (ibat_high - ibat_low);

			V_offset = (IBAT_VDROP_H << CALIB_SHIFT_IBAT) -
				(((IBAT_VDROP_H - IBAT_VDROP_L) <<
				CALIB_SHIFT_IBAT) / (ibat_high - ibat_low))
				* ibat_high;
			/*
			 * Result obtained is in mV (at a scale factor),
			 * we need to calculate gain and offset to get mA
			 */
			V2A_gain = (ADC_CH_IBAT_MAX - ADC_CH_IBAT_MIN)/
				(ADC_CH_IBAT_MAX_V - ADC_CH_IBAT_MIN_V);
			V2A_offset = ((ADC_CH_IBAT_MAX_V * ADC_CH_IBAT_MIN -
				ADC_CH_IBAT_MAX * ADC_CH_IBAT_MIN_V)
				<< CALIB_SHIFT_IBAT)
				/ (ADC_CH_IBAT_MAX_V - ADC_CH_IBAT_MIN_V);

			gpadc->cal_data[ADC_INPUT_IBAT].gain = V_gain * V2A_gain;
			gpadc->cal_data[ADC_INPUT_IBAT].offset = V_offset *
				V2A_gain + V2A_offset;
		} else {
			gpadc->cal_data[ADC_INPUT_IBAT].gain = 0;
		}

		dev_dbg(gpadc->dev, "IBAT gain %llu offset %llu\n",
			gpadc->cal_data[ADC_INPUT_IBAT].gain,
			gpadc->cal_data[ADC_INPUT_IBAT].offset);
	} else {
		/* Calculate gain and offset for VMAIN if all reads succeeded */
		if (!(ret[0] < 0 || ret[1] < 0 || ret[2] < 0)) {
			vmain_high = (((gpadc_cal[0] & 0x03) << 8) |
				((gpadc_cal[1] & 0x3F) << 2) |
				((gpadc_cal[2] & 0xC0) >> 6));
			vmain_low = ((gpadc_cal[2] & 0x3E) >> 1);

			gpadc->cal_data[ADC_INPUT_VMAIN].otp_calib_hi =
				(u16)vmain_high;
			gpadc->cal_data[ADC_INPUT_VMAIN].otp_calib_lo =
				(u16)vmain_low;

			gpadc->cal_data[ADC_INPUT_VMAIN].gain = CALIB_SCALE *
				(19500 - 315) / (vmain_high - vmain_low);

			gpadc->cal_data[ADC_INPUT_VMAIN].offset = CALIB_SCALE *
				19500 - (CALIB_SCALE * (19500 - 315) /
				(vmain_high - vmain_low)) * vmain_high;
		} else {
			gpadc->cal_data[ADC_INPUT_VMAIN].gain = 0;
		}
	}

	/* Calculate gain and offset for BTEMP if all reads succeeded */
	if (!(ret[2] < 0 || ret[3] < 0 || ret[4] < 0)) {
		btemp_high = (((gpadc_cal[2] & 0x01) << 9) |
			(gpadc_cal[3] << 1) | ((gpadc_cal[4] & 0x80) >> 7));
		btemp_low = ((gpadc_cal[4] & 0x7C) >> 2);

		gpadc->cal_data[ADC_INPUT_BTEMP].otp_calib_hi = (u16)btemp_high;
		gpadc->cal_data[ADC_INPUT_BTEMP].otp_calib_lo = (u16)btemp_low;

		gpadc->cal_data[ADC_INPUT_BTEMP].gain =
			CALIB_SCALE * (1300 - 21) / (btemp_high - btemp_low);
		gpadc->cal_data[ADC_INPUT_BTEMP].offset = CALIB_SCALE * 1300 -
			(CALIB_SCALE * (1300 - 21) / (btemp_high - btemp_low))
			* btemp_high;
	} else {
		gpadc->cal_data[ADC_INPUT_BTEMP].gain = 0;
	}

	/* Calculate gain and offset for VBAT if all reads succeeded */
	if (!(ret[4] < 0 || ret[5] < 0 || ret[6] < 0)) {
		vbat_high = (((gpadc_cal[4] & 0x03) << 8) | gpadc_cal[5]);
		vbat_low = ((gpadc_cal[6] & 0xFC) >> 2);

		gpadc->cal_data[ADC_INPUT_VBAT].otp_calib_hi = (u16)vbat_high;
		gpadc->cal_data[ADC_INPUT_VBAT].otp_calib_lo = (u16)vbat_low;

		gpadc->cal_data[ADC_INPUT_VBAT].gain = CALIB_SCALE *
			(4700 - 2380) /	(vbat_high - vbat_low);
		gpadc->cal_data[ADC_INPUT_VBAT].offset = CALIB_SCALE * 4700 -
			(CALIB_SCALE * (4700 - 2380) /
			(vbat_high - vbat_low)) * vbat_high;
	} else {
		gpadc->cal_data[ADC_INPUT_VBAT].gain = 0;
	}

	dev_dbg(gpadc->dev, "VMAIN gain %llu offset %llu\n",
		gpadc->cal_data[ADC_INPUT_VMAIN].gain,
		gpadc->cal_data[ADC_INPUT_VMAIN].offset);

	dev_dbg(gpadc->dev, "BTEMP gain %llu offset %llu\n",
		gpadc->cal_data[ADC_INPUT_BTEMP].gain,
		gpadc->cal_data[ADC_INPUT_BTEMP].offset);

	dev_dbg(gpadc->dev, "VBAT gain %llu offset %llu\n",
		gpadc->cal_data[ADC_INPUT_VBAT].gain,
		gpadc->cal_data[ADC_INPUT_VBAT].offset);
}

static int ab8500_gpadc_runtime_suspend(struct device *dev)
{
	struct ab8500_gpadc *gpadc = dev_get_drvdata(dev);

	regulator_disable(gpadc->regu);
	return 0;
}

static int ab8500_gpadc_runtime_resume(struct device *dev)
{
	struct ab8500_gpadc *gpadc = dev_get_drvdata(dev);
	int ret;

	ret = regulator_enable(gpadc->regu);
	if (ret)
		dev_err(dev, "Failed to enable vtvout LDO: %d\n", ret);
	return ret;
}

static int ab8500_gpadc_runtime_idle(struct device *dev)
{
	pm_runtime_suspend(dev);
	return 0;
}

static int ab8500_gpadc_suspend(struct device *dev)
{
	struct ab8500_gpadc *gpadc = dev_get_drvdata(dev);

	mutex_lock(&gpadc->ab8500_gpadc_lock);

	pm_runtime_get_sync(dev);

	regulator_disable(gpadc->regu);
	return 0;
}

static int ab8500_gpadc_resume(struct device *dev)
{
	struct ab8500_gpadc *gpadc = dev_get_drvdata(dev);

	regulator_enable(gpadc->regu);

	pm_runtime_mark_last_busy(gpadc->dev);
	pm_runtime_put_autosuspend(gpadc->dev);

	mutex_unlock(&gpadc->ab8500_gpadc_lock);
	return 0;
}

static int ab8500_gpadc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct ab8500_gpadc *gpadc;

	gpadc = kzalloc(sizeof(struct ab8500_gpadc), GFP_KERNEL);
	if (!gpadc) {
		dev_err(&pdev->dev, "Error: No memory\n");
		return -ENOMEM;
	}

	gpadc->irq_sw = platform_get_irq_byname(pdev, "SW_CONV_END");
	if (gpadc->irq_sw < 0)
		dev_err(gpadc->dev, "failed to get platform sw_conv_end irq\n");

	gpadc->irq_hw = platform_get_irq_byname(pdev, "HW_CONV_END");
	if (gpadc->irq_hw < 0)
		dev_err(gpadc->dev, "failed to get platform hw_conv_end irq\n");

	gpadc->dev = &pdev->dev;
	gpadc->parent = dev_get_drvdata(pdev->dev.parent);
	mutex_init(&gpadc->ab8500_gpadc_lock);

	/* Initialize completion used to notify completion of conversion */
	init_completion(&gpadc->ab8500_gpadc_complete);

	/* Register interrupts */
	if (gpadc->irq_sw >= 0) {
		ret = request_threaded_irq(gpadc->irq_sw, NULL,
			ab8500_bm_gpadcconvend_handler,
			IRQF_NO_SUSPEND | IRQF_SHARED, "ab8500-gpadc-sw",
			gpadc);
		if (ret < 0) {
			dev_err(gpadc->dev,
				"Failed to register interrupt irq: %d\n",
				gpadc->irq_sw);
			goto fail;
		}
	}

	if (gpadc->irq_hw >= 0) {
		ret = request_threaded_irq(gpadc->irq_hw, NULL,
			ab8500_bm_gpadcconvend_handler,
			IRQF_NO_SUSPEND | IRQF_SHARED, "ab8500-gpadc-hw",
			gpadc);
		if (ret < 0) {
			dev_err(gpadc->dev,
				"Failed to register interrupt irq: %d\n",
				gpadc->irq_hw);
			goto fail_irq;
		}
	}

	/* VTVout LDO used to power up ab8500-GPADC */
	gpadc->regu = devm_regulator_get(&pdev->dev, "vddadc");
	if (IS_ERR(gpadc->regu)) {
		ret = PTR_ERR(gpadc->regu);
		dev_err(gpadc->dev, "failed to get vtvout LDO\n");
		goto fail_irq;
	}

	platform_set_drvdata(pdev, gpadc);

	ret = regulator_enable(gpadc->regu);
	if (ret) {
		dev_err(gpadc->dev, "Failed to enable vtvout LDO: %d\n", ret);
		goto fail_enable;
	}

	pm_runtime_set_autosuspend_delay(gpadc->dev, GPADC_AUDOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(gpadc->dev);
	pm_runtime_set_active(gpadc->dev);
	pm_runtime_enable(gpadc->dev);

	ab8500_gpadc_read_calibration_data(gpadc);
	list_add_tail(&gpadc->node, &ab8500_gpadc_list);
	dev_dbg(gpadc->dev, "probe success\n");

	return 0;

fail_enable:
fail_irq:
	free_irq(gpadc->irq_sw, gpadc);
	free_irq(gpadc->irq_hw, gpadc);
fail:
	kfree(gpadc);
	gpadc = NULL;
	return ret;
}

static int ab8500_gpadc_remove(struct platform_device *pdev)
{
	struct ab8500_gpadc *gpadc = platform_get_drvdata(pdev);

	/* remove this gpadc entry from the list */
	list_del(&gpadc->node);
	/* remove interrupt  - completion of Sw ADC conversion */
	if (gpadc->irq_sw >= 0)
		free_irq(gpadc->irq_sw, gpadc);
	if (gpadc->irq_hw >= 0)
		free_irq(gpadc->irq_hw, gpadc);

	pm_runtime_get_sync(gpadc->dev);
	pm_runtime_disable(gpadc->dev);

	regulator_disable(gpadc->regu);

	pm_runtime_set_suspended(gpadc->dev);

	pm_runtime_put_noidle(gpadc->dev);

	kfree(gpadc);
	gpadc = NULL;
	return 0;
}

static const struct dev_pm_ops ab8500_gpadc_pm_ops = {
	SET_RUNTIME_PM_OPS(ab8500_gpadc_runtime_suspend,
			   ab8500_gpadc_runtime_resume,
			   ab8500_gpadc_runtime_idle)
	SET_SYSTEM_SLEEP_PM_OPS(ab8500_gpadc_suspend,
				ab8500_gpadc_resume)

};

static struct platform_driver ab8500_gpadc_driver = {
	.probe = ab8500_gpadc_probe,
	.remove = ab8500_gpadc_remove,
	.driver = {
		.name = "ab8500-gpadc",
		.owner = THIS_MODULE,
		.pm = &ab8500_gpadc_pm_ops,
	},
};

static int __init ab8500_gpadc_init(void)
{
	return platform_driver_register(&ab8500_gpadc_driver);
}

static void __exit ab8500_gpadc_exit(void)
{
	platform_driver_unregister(&ab8500_gpadc_driver);
}

/**
 * ab8540_gpadc_get_otp() - returns OTP values
 *
 */
void ab8540_gpadc_get_otp(struct ab8500_gpadc *gpadc,
			u16 *vmain_l, u16 *vmain_h, u16 *btemp_l, u16 *btemp_h,
			u16 *vbat_l, u16 *vbat_h, u16 *ibat_l, u16 *ibat_h)
{
	*vmain_l = gpadc->cal_data[ADC_INPUT_VMAIN].otp_calib_lo;
	*vmain_h = gpadc->cal_data[ADC_INPUT_VMAIN].otp_calib_hi;
	*btemp_l = gpadc->cal_data[ADC_INPUT_BTEMP].otp_calib_lo;
	*btemp_h = gpadc->cal_data[ADC_INPUT_BTEMP].otp_calib_hi;
	*vbat_l = gpadc->cal_data[ADC_INPUT_VBAT].otp_calib_lo;
	*vbat_h = gpadc->cal_data[ADC_INPUT_VBAT].otp_calib_hi;
	*ibat_l = gpadc->cal_data[ADC_INPUT_IBAT].otp_calib_lo;
	*ibat_h = gpadc->cal_data[ADC_INPUT_IBAT].otp_calib_hi;
	return ;
}

subsys_initcall_sync(ab8500_gpadc_init);
module_exit(ab8500_gpadc_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Arun R Murthy, Daniel Willerud, Johan Palsson,"
		"M'boumba Cedric Madianga");
MODULE_ALIAS("platform:ab8500_gpadc");
MODULE_DESCRIPTION("AB8500 GPADC driver");
