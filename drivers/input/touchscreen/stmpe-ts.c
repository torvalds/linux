/*
 * STMicroelectronics STMPE811 Touchscreen Driver
 *
 * (C) 2010 Luotao Fu <l.fu@pengutronix.de>
 * All rights reserved.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>

#include <linux/mfd/stmpe.h>

/* Register layouts and functionalities are identical on all stmpexxx variants
 * with touchscreen controller
 */
#define STMPE_REG_INT_STA		0x0B
#define STMPE_REG_ADC_CTRL1		0x20
#define STMPE_REG_ADC_CTRL2		0x21
#define STMPE_REG_TSC_CTRL		0x40
#define STMPE_REG_TSC_CFG		0x41
#define STMPE_REG_FIFO_TH		0x4A
#define STMPE_REG_FIFO_STA		0x4B
#define STMPE_REG_FIFO_SIZE		0x4C
#define STMPE_REG_TSC_DATA_XYZ		0x52
#define STMPE_REG_TSC_FRACTION_Z	0x56
#define STMPE_REG_TSC_I_DRIVE		0x58

#define OP_MOD_XYZ			0

#define STMPE_TSC_CTRL_TSC_EN		(1<<0)

#define STMPE_FIFO_STA_RESET		(1<<0)

#define STMPE_IRQ_TOUCH_DET		0

#define SAMPLE_TIME(x)			((x & 0xf) << 4)
#define MOD_12B(x)			((x & 0x1) << 3)
#define REF_SEL(x)			((x & 0x1) << 1)
#define ADC_FREQ(x)			(x & 0x3)
#define AVE_CTRL(x)			((x & 0x3) << 6)
#define DET_DELAY(x)			((x & 0x7) << 3)
#define SETTLING(x)			(x & 0x7)
#define FRACTION_Z(x)			(x & 0x7)
#define I_DRIVE(x)			(x & 0x1)
#define OP_MODE(x)			((x & 0x7) << 1)

#define STMPE_TS_NAME			"stmpe-ts"
#define XY_MASK				0xfff

struct stmpe_touch {
	struct stmpe *stmpe;
	struct input_dev *idev;
	struct delayed_work work;
	struct device *dev;
	u8 sample_time;
	u8 mod_12b;
	u8 ref_sel;
	u8 adc_freq;
	u8 ave_ctrl;
	u8 touch_det_delay;
	u8 settling;
	u8 fraction_z;
	u8 i_drive;
};

static int __stmpe_reset_fifo(struct stmpe *stmpe)
{
	int ret;

	ret = stmpe_set_bits(stmpe, STMPE_REG_FIFO_STA,
			STMPE_FIFO_STA_RESET, STMPE_FIFO_STA_RESET);
	if (ret)
		return ret;

	return stmpe_set_bits(stmpe, STMPE_REG_FIFO_STA,
			STMPE_FIFO_STA_RESET, 0);
}

static void stmpe_work(struct work_struct *work)
{
	int int_sta;
	u32 timeout = 40;

	struct stmpe_touch *ts =
	    container_of(work, struct stmpe_touch, work.work);

	int_sta = stmpe_reg_read(ts->stmpe, STMPE_REG_INT_STA);

	/*
	 * touch_det sometimes get desasserted or just get stuck. This appears
	 * to be a silicon bug, We still have to clearify this with the
	 * manufacture. As a workaround We release the key anyway if the
	 * touch_det keeps coming in after 4ms, while the FIFO contains no value
	 * during the whole time.
	 */
	while ((int_sta & (1 << STMPE_IRQ_TOUCH_DET)) && (timeout > 0)) {
		timeout--;
		int_sta = stmpe_reg_read(ts->stmpe, STMPE_REG_INT_STA);
		udelay(100);
	}

	/* reset the FIFO before we report release event */
	__stmpe_reset_fifo(ts->stmpe);

	input_report_abs(ts->idev, ABS_PRESSURE, 0);
	input_sync(ts->idev);
}

static irqreturn_t stmpe_ts_handler(int irq, void *data)
{
	u8 data_set[4];
	int x, y, z;
	struct stmpe_touch *ts = data;

	/*
	 * Cancel scheduled polling for release if we have new value
	 * available. Wait if the polling is already running.
	 */
	cancel_delayed_work_sync(&ts->work);

	/*
	 * The FIFO sometimes just crashes and stops generating interrupts. This
	 * appears to be a silicon bug. We still have to clearify this with
	 * the manufacture. As a workaround we disable the TSC while we are
	 * collecting data and flush the FIFO after reading
	 */
	stmpe_set_bits(ts->stmpe, STMPE_REG_TSC_CTRL,
				STMPE_TSC_CTRL_TSC_EN, 0);

	stmpe_block_read(ts->stmpe, STMPE_REG_TSC_DATA_XYZ, 4, data_set);

	x = (data_set[0] << 4) | (data_set[1] >> 4);
	y = ((data_set[1] & 0xf) << 8) | data_set[2];
	z = data_set[3];

	input_report_abs(ts->idev, ABS_X, x);
	input_report_abs(ts->idev, ABS_Y, y);
	input_report_abs(ts->idev, ABS_PRESSURE, z);
	input_sync(ts->idev);

       /* flush the FIFO after we have read out our values. */
	__stmpe_reset_fifo(ts->stmpe);

	/* reenable the tsc */
	stmpe_set_bits(ts->stmpe, STMPE_REG_TSC_CTRL,
			STMPE_TSC_CTRL_TSC_EN, STMPE_TSC_CTRL_TSC_EN);

	/* start polling for touch_det to detect release */
	schedule_delayed_work(&ts->work, HZ / 50);

	return IRQ_HANDLED;
}

static int stmpe_init_hw(struct stmpe_touch *ts)
{
	int ret;
	u8 adc_ctrl1, adc_ctrl1_mask, tsc_cfg, tsc_cfg_mask;
	struct stmpe *stmpe = ts->stmpe;
	struct device *dev = ts->dev;

	ret = stmpe_enable(stmpe, STMPE_BLOCK_TOUCHSCREEN | STMPE_BLOCK_ADC);
	if (ret) {
		dev_err(dev, "Could not enable clock for ADC and TS\n");
		return ret;
	}

	adc_ctrl1 = SAMPLE_TIME(ts->sample_time) | MOD_12B(ts->mod_12b) |
		REF_SEL(ts->ref_sel);
	adc_ctrl1_mask = SAMPLE_TIME(0xff) | MOD_12B(0xff) | REF_SEL(0xff);

	ret = stmpe_set_bits(stmpe, STMPE_REG_ADC_CTRL1,
			adc_ctrl1_mask, adc_ctrl1);
	if (ret) {
		dev_err(dev, "Could not setup ADC\n");
		return ret;
	}

	ret = stmpe_set_bits(stmpe, STMPE_REG_ADC_CTRL2,
			ADC_FREQ(0xff), ADC_FREQ(ts->adc_freq));
	if (ret) {
		dev_err(dev, "Could not setup ADC\n");
		return ret;
	}

	tsc_cfg = AVE_CTRL(ts->ave_ctrl) | DET_DELAY(ts->touch_det_delay) |
			SETTLING(ts->settling);
	tsc_cfg_mask = AVE_CTRL(0xff) | DET_DELAY(0xff) | SETTLING(0xff);

	ret = stmpe_set_bits(stmpe, STMPE_REG_TSC_CFG, tsc_cfg_mask, tsc_cfg);
	if (ret) {
		dev_err(dev, "Could not config touch\n");
		return ret;
	}

	ret = stmpe_set_bits(stmpe, STMPE_REG_TSC_FRACTION_Z,
			FRACTION_Z(0xff), FRACTION_Z(ts->fraction_z));
	if (ret) {
		dev_err(dev, "Could not config touch\n");
		return ret;
	}

	ret = stmpe_set_bits(stmpe, STMPE_REG_TSC_I_DRIVE,
			I_DRIVE(0xff), I_DRIVE(ts->i_drive));
	if (ret) {
		dev_err(dev, "Could not config touch\n");
		return ret;
	}

	/* set FIFO to 1 for single point reading */
	ret = stmpe_reg_write(stmpe, STMPE_REG_FIFO_TH, 1);
	if (ret) {
		dev_err(dev, "Could not set FIFO\n");
		return ret;
	}

	ret = stmpe_set_bits(stmpe, STMPE_REG_TSC_CTRL,
			OP_MODE(0xff), OP_MODE(OP_MOD_XYZ));
	if (ret) {
		dev_err(dev, "Could not set mode\n");
		return ret;
	}

	return 0;
}

static int stmpe_ts_open(struct input_dev *dev)
{
	struct stmpe_touch *ts = input_get_drvdata(dev);
	int ret = 0;

	ret = __stmpe_reset_fifo(ts->stmpe);
	if (ret)
		return ret;

	return stmpe_set_bits(ts->stmpe, STMPE_REG_TSC_CTRL,
			STMPE_TSC_CTRL_TSC_EN, STMPE_TSC_CTRL_TSC_EN);
}

static void stmpe_ts_close(struct input_dev *dev)
{
	struct stmpe_touch *ts = input_get_drvdata(dev);

	cancel_delayed_work_sync(&ts->work);

	stmpe_set_bits(ts->stmpe, STMPE_REG_TSC_CTRL,
			STMPE_TSC_CTRL_TSC_EN, 0);
}

static void stmpe_ts_get_platform_info(struct platform_device *pdev,
					struct stmpe_touch *ts)
{
	struct stmpe *stmpe = dev_get_drvdata(pdev->dev.parent);
	struct device_node *np = pdev->dev.of_node;
	struct stmpe_ts_platform_data *ts_pdata = NULL;

	ts->stmpe = stmpe;

	if (stmpe->pdata && stmpe->pdata->ts) {
		ts_pdata = stmpe->pdata->ts;

		ts->sample_time = ts_pdata->sample_time;
		ts->mod_12b = ts_pdata->mod_12b;
		ts->ref_sel = ts_pdata->ref_sel;
		ts->adc_freq = ts_pdata->adc_freq;
		ts->ave_ctrl = ts_pdata->ave_ctrl;
		ts->touch_det_delay = ts_pdata->touch_det_delay;
		ts->settling = ts_pdata->settling;
		ts->fraction_z = ts_pdata->fraction_z;
		ts->i_drive = ts_pdata->i_drive;
	} else if (np) {
		u32 val;

		if (!of_property_read_u32(np, "st,sample-time", &val))
			ts->sample_time = val;
		if (!of_property_read_u32(np, "st,mod-12b", &val))
			ts->mod_12b = val;
		if (!of_property_read_u32(np, "st,ref-sel", &val))
			ts->ref_sel = val;
		if (!of_property_read_u32(np, "st,adc-freq", &val))
			ts->adc_freq = val;
		if (!of_property_read_u32(np, "st,ave-ctrl", &val))
			ts->ave_ctrl = val;
		if (!of_property_read_u32(np, "st,touch-det-delay", &val))
			ts->touch_det_delay = val;
		if (!of_property_read_u32(np, "st,settling", &val))
			ts->settling = val;
		if (!of_property_read_u32(np, "st,fraction-z", &val))
			ts->fraction_z = val;
		if (!of_property_read_u32(np, "st,i-drive", &val))
			ts->i_drive = val;
	}
}

static int stmpe_input_probe(struct platform_device *pdev)
{
	struct stmpe_touch *ts;
	struct input_dev *idev;
	int error;
	int ts_irq;

	ts_irq = platform_get_irq_byname(pdev, "FIFO_TH");
	if (ts_irq < 0)
		return ts_irq;

	ts = devm_kzalloc(&pdev->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	idev = devm_input_allocate_device(&pdev->dev);
	if (!idev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ts);
	ts->idev = idev;
	ts->dev = &pdev->dev;

	stmpe_ts_get_platform_info(pdev, ts);

	INIT_DELAYED_WORK(&ts->work, stmpe_work);

	error = devm_request_threaded_irq(&pdev->dev, ts_irq,
					  NULL, stmpe_ts_handler,
					  IRQF_ONESHOT, STMPE_TS_NAME, ts);
	if (error) {
		dev_err(&pdev->dev, "Failed to request IRQ %d\n", ts_irq);
		return error;
	}

	error = stmpe_init_hw(ts);
	if (error)
		return error;

	idev->name = STMPE_TS_NAME;
	idev->phys = STMPE_TS_NAME"/input0";
	idev->id.bustype = BUS_I2C;
	idev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	idev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	idev->open = stmpe_ts_open;
	idev->close = stmpe_ts_close;

	input_set_drvdata(idev, ts);

	input_set_abs_params(idev, ABS_X, 0, XY_MASK, 0, 0);
	input_set_abs_params(idev, ABS_Y, 0, XY_MASK, 0, 0);
	input_set_abs_params(idev, ABS_PRESSURE, 0x0, 0xff, 0, 0);

	error = input_register_device(idev);
	if (error) {
		dev_err(&pdev->dev, "Could not register input device\n");
		return error;
	}

	return 0;
}

static int stmpe_ts_remove(struct platform_device *pdev)
{
	struct stmpe_touch *ts = platform_get_drvdata(pdev);

	stmpe_disable(ts->stmpe, STMPE_BLOCK_TOUCHSCREEN);

	return 0;
}

static struct platform_driver stmpe_ts_driver = {
	.driver = {
		   .name = STMPE_TS_NAME,
		   .owner = THIS_MODULE,
		   },
	.probe = stmpe_input_probe,
	.remove = stmpe_ts_remove,
};
module_platform_driver(stmpe_ts_driver);

MODULE_AUTHOR("Luotao Fu <l.fu@pengutronix.de>");
MODULE_DESCRIPTION("STMPEXXX touchscreen driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" STMPE_TS_NAME);
