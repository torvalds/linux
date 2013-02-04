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

/* gpadc constants */
#define EN_VINTCORE12			0x04
#define EN_VTVOUT			0x02
#define EN_GPADC			0x01
#define DIS_GPADC			0x00
#define SW_AVG_16			0x60
#define ADC_SW_CONV			0x04
#define EN_ICHAR			0x80
#define BTEMP_PULL_UP			0x08
#define EN_BUF				0x40
#define DIS_ZERO			0x00
#define GPADC_BUSY			0x01

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

/* This is used to not lose precision when dividing to get gain and offset */
#define CALIB_SCALE			1000

/* Time in ms before disabling regulator */
#define GPADC_AUDOSUSPEND_DELAY		1

enum cal_channels {
	ADC_INPUT_VMAIN = 0,
	ADC_INPUT_BTEMP,
	ADC_INPUT_VBAT,
	NBR_CAL_INPUTS,
};

/**
 * struct adc_cal_data - Table for storing gain and offset for the calibrated
 * ADC channels
 * @gain:		Gain of the ADC channel
 * @offset:		Offset of the ADC channel
 */
struct adc_cal_data {
	u64 gain;
	u64 offset;
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
 * @irq:			interrupt number that is used by gpadc
 * @cal_data			array of ADC calibration data structs
 */
struct ab8500_gpadc {
	struct device *dev;
	struct list_head node;
	struct ab8500 *parent;
	struct completion ab8500_gpadc_complete;
	struct mutex ab8500_gpadc_lock;
	struct regulator *regu;
	int irq;
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
 * ab8500_gpadc_convert() - gpadc conversion
 * @channel:	analog channel to be converted to digital data
 *
 * This function converts the selected analog i/p to digital
 * data.
 */
int ab8500_gpadc_convert(struct ab8500_gpadc *gpadc, u8 channel)
{
	int ad_value;
	int voltage;

	ad_value = ab8500_gpadc_read_raw(gpadc, channel);
	if (ad_value < 0) {
		dev_err(gpadc->dev, "GPADC raw value failed ch: %d\n", channel);
		return ad_value;
	}

	voltage = ab8500_gpadc_ad_to_voltage(gpadc, channel, ad_value);

	if (voltage < 0)
		dev_err(gpadc->dev, "GPADC to voltage conversion failed ch:"
			" %d AD: 0x%x\n", channel, ad_value);

	return voltage;
}
EXPORT_SYMBOL(ab8500_gpadc_convert);

/**
 * ab8500_gpadc_read_raw() - gpadc read
 * @channel:	analog channel to be read
 *
 * This function obtains the raw ADC value, this then needs
 * to be converted by calling ab8500_gpadc_ad_to_voltage()
 */
int ab8500_gpadc_read_raw(struct ab8500_gpadc *gpadc, u8 channel)
{
	int ret;
	int looplimit = 0;
	u8 val, low_data, high_data;

	if (!gpadc)
		return -ENODEV;

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
	ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
		AB8500_GPADC, AB8500_GPADC_CTRL1_REG, EN_GPADC, EN_GPADC);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc_conversion: enable gpadc failed\n");
		goto out;
	}

	/* Select the channel source and set average samples to 16 */
	ret = abx500_set_register_interruptible(gpadc->dev, AB8500_GPADC,
		AB8500_GPADC_CTRL2_REG, (channel | SW_AVG_16));
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
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
			AB8500_GPADC, AB8500_GPADC_CTRL1_REG,
			EN_BUF | EN_ICHAR,
			EN_BUF | EN_ICHAR);
		break;
	case BTEMP_BALL:
		if (!is_ab8500_2p0_or_earlier(gpadc->parent)) {
			/* Turn on btemp pull-up on ABB 3.0 */
			ret = abx500_mask_and_set_register_interruptible(
				gpadc->dev,
				AB8500_GPADC, AB8500_GPADC_CTRL1_REG,
				EN_BUF | BTEMP_PULL_UP,
				EN_BUF | BTEMP_PULL_UP);

		 /*
		  * Delay might be needed for ABB8500 cut 3.0, if not, remove
		  * when hardware will be available
		  */
			usleep_range(1000, 1000);
			break;
		}
		/* Intentional fallthrough */
	default:
		ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
			AB8500_GPADC, AB8500_GPADC_CTRL1_REG, EN_BUF, EN_BUF);
		break;
	}
	if (ret < 0) {
		dev_err(gpadc->dev,
			"gpadc_conversion: select falling edge failed\n");
		goto out;
	}

	ret = abx500_mask_and_set_register_interruptible(gpadc->dev,
		AB8500_GPADC, AB8500_GPADC_CTRL1_REG, ADC_SW_CONV, ADC_SW_CONV);
	if (ret < 0) {
		dev_err(gpadc->dev,
			"gpadc_conversion: start s/w conversion failed\n");
		goto out;
	}
	/* wait for completion of conversion */
	if (!wait_for_completion_timeout(&gpadc->ab8500_gpadc_complete, 2*HZ)) {
		dev_err(gpadc->dev,
			"timeout: didn't receive GPADC conversion interrupt\n");
		ret = -EINVAL;
		goto out;
	}

	/* Read the converted RAW data */
	ret = abx500_get_register_interruptible(gpadc->dev, AB8500_GPADC,
		AB8500_GPADC_MANDATAL_REG, &low_data);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc_conversion: read low data failed\n");
		goto out;
	}

	ret = abx500_get_register_interruptible(gpadc->dev, AB8500_GPADC,
		AB8500_GPADC_MANDATAH_REG, &high_data);
	if (ret < 0) {
		dev_err(gpadc->dev,
			"gpadc_conversion: read high data failed\n");
		goto out;
	}

	/* Disable GPADC */
	ret = abx500_set_register_interruptible(gpadc->dev, AB8500_GPADC,
		AB8500_GPADC_CTRL1_REG, DIS_GPADC);
	if (ret < 0) {
		dev_err(gpadc->dev, "gpadc_conversion: disable gpadc failed\n");
		goto out;
	}

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
 * ab8500_bm_gpswadcconvend_handler() - isr for s/w gpadc conversion completion
 * @irq:	irq number
 * @data:	pointer to the data passed during request irq
 *
 * This is a interrupt service routine for s/w gpadc conversion completion.
 * Notifies the gpadc completion is completed and the converted raw value
 * can be read from the registers.
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_bm_gpswadcconvend_handler(int irq, void *_gpadc)
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

static void ab8500_gpadc_read_calibration_data(struct ab8500_gpadc *gpadc)
{
	int i;
	int ret[ARRAY_SIZE(otp_cal_regs)];
	u8 gpadc_cal[ARRAY_SIZE(otp_cal_regs)];

	int vmain_high, vmain_low;
	int btemp_high, btemp_low;
	int vbat_high, vbat_low;

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
	 * Data bits:
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

	/* Calculate gain and offset for VMAIN if all reads succeeded */
	if (!(ret[0] < 0 || ret[1] < 0 || ret[2] < 0)) {
		vmain_high = (((gpadc_cal[0] & 0x03) << 8) |
			((gpadc_cal[1] & 0x3F) << 2) |
			((gpadc_cal[2] & 0xC0) >> 6));

		vmain_low = ((gpadc_cal[2] & 0x3E) >> 1);

		gpadc->cal_data[ADC_INPUT_VMAIN].gain = CALIB_SCALE *
			(19500 - 315) /	(vmain_high - vmain_low);

		gpadc->cal_data[ADC_INPUT_VMAIN].offset = CALIB_SCALE * 19500 -
			(CALIB_SCALE * (19500 - 315) /
			 (vmain_high - vmain_low)) * vmain_high;
	} else {
		gpadc->cal_data[ADC_INPUT_VMAIN].gain = 0;
	}

	/* Calculate gain and offset for BTEMP if all reads succeeded */
	if (!(ret[2] < 0 || ret[3] < 0 || ret[4] < 0)) {
		btemp_high = (((gpadc_cal[2] & 0x01) << 9) |
			(gpadc_cal[3] << 1) |
			((gpadc_cal[4] & 0x80) >> 7));

		btemp_low = ((gpadc_cal[4] & 0x7C) >> 2);

		gpadc->cal_data[ADC_INPUT_BTEMP].gain =
			CALIB_SCALE * (1300 - 21) / (btemp_high - btemp_low);

		gpadc->cal_data[ADC_INPUT_BTEMP].offset = CALIB_SCALE * 1300 -
			(CALIB_SCALE * (1300 - 21) /
			(btemp_high - btemp_low)) * btemp_high;
	} else {
		gpadc->cal_data[ADC_INPUT_BTEMP].gain = 0;
	}

	/* Calculate gain and offset for VBAT if all reads succeeded */
	if (!(ret[4] < 0 || ret[5] < 0 || ret[6] < 0)) {
		vbat_high = (((gpadc_cal[4] & 0x03) << 8) | gpadc_cal[5]);
		vbat_low = ((gpadc_cal[6] & 0xFC) >> 2);

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

	regulator_enable(gpadc->regu);
	return 0;
}

static int ab8500_gpadc_runtime_idle(struct device *dev)
{
	struct ab8500_gpadc *gpadc = dev_get_drvdata(dev);

	pm_runtime_suspend(dev);
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

	gpadc->irq = platform_get_irq_byname(pdev, "SW_CONV_END");
	if (gpadc->irq < 0) {
		dev_err(&pdev->dev, "failed to get platform irq-%d\n",
			gpadc->irq);
		ret = gpadc->irq;
		goto fail;
	}

	gpadc->dev = &pdev->dev;
	gpadc->parent = dev_get_drvdata(pdev->dev.parent);
	mutex_init(&gpadc->ab8500_gpadc_lock);

	/* Initialize completion used to notify completion of conversion */
	init_completion(&gpadc->ab8500_gpadc_complete);

	/* Register interrupt  - SwAdcComplete */
	ret = request_threaded_irq(gpadc->irq, NULL,
		ab8500_bm_gpswadcconvend_handler,
		IRQF_ONESHOT | IRQF_NO_SUSPEND | IRQF_SHARED,
				"ab8500-gpadc", gpadc);
	if (ret < 0) {
		dev_err(gpadc->dev, "Failed to register interrupt, irq: %d\n",
			gpadc->irq);
		goto fail;
	}

	/* VTVout LDO used to power up ab8500-GPADC */
	gpadc->regu = regulator_get(&pdev->dev, "vddadc");
	if (IS_ERR(gpadc->regu)) {
		ret = PTR_ERR(gpadc->regu);
		dev_err(gpadc->dev, "failed to get vtvout LDO\n");
		goto fail_irq;
	}

	platform_set_drvdata(pdev, gpadc);

	regulator_enable(gpadc->regu);

	pm_runtime_set_autosuspend_delay(gpadc->dev, GPADC_AUDOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(gpadc->dev);
	pm_runtime_set_active(gpadc->dev);
	pm_runtime_enable(gpadc->dev);

	ab8500_gpadc_read_calibration_data(gpadc);
	list_add_tail(&gpadc->node, &ab8500_gpadc_list);
	dev_dbg(gpadc->dev, "probe success\n");
	return 0;
fail_irq:
	free_irq(gpadc->irq, gpadc);
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
	free_irq(gpadc->irq, gpadc);

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

subsys_initcall_sync(ab8500_gpadc_init);
module_exit(ab8500_gpadc_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Arun R Murthy, Daniel Willerud, Johan Palsson");
MODULE_ALIAS("platform:ab8500_gpadc");
MODULE_DESCRIPTION("AB8500 GPADC driver");
