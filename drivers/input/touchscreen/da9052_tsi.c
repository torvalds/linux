/*
 * TSI driver for Dialog DA9052
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */
#include <linux/module.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>

#include <linux/mfd/da9052/reg.h>
#include <linux/mfd/da9052/da9052.h>

#define TSI_PEN_DOWN_STATUS 0x40

struct da9052_tsi {
	struct da9052 *da9052;
	struct input_dev *dev;
	struct delayed_work ts_pen_work;
	struct mutex mutex;
	bool stopped;
	bool adc_on;
};

static void da9052_ts_adc_toggle(struct da9052_tsi *tsi, bool on)
{
	da9052_reg_update(tsi->da9052, DA9052_TSI_CONT_A_REG, 1 << 0, on);
	tsi->adc_on = on;
}

static irqreturn_t da9052_ts_pendwn_irq(int irq, void *data)
{
	struct da9052_tsi *tsi = data;

	if (!tsi->stopped) {
		/* Mask PEN_DOWN event and unmask TSI_READY event */
		da9052_disable_irq_nosync(tsi->da9052, DA9052_IRQ_PENDOWN);
		da9052_enable_irq(tsi->da9052, DA9052_IRQ_TSIREADY);

		da9052_ts_adc_toggle(tsi, true);

		schedule_delayed_work(&tsi->ts_pen_work, HZ / 50);
	}

	return IRQ_HANDLED;
}

static void da9052_ts_read(struct da9052_tsi *tsi)
{
	struct input_dev *input = tsi->dev;
	int ret;
	u16 x, y, z;
	u8 v;

	ret = da9052_reg_read(tsi->da9052, DA9052_TSI_X_MSB_REG);
	if (ret < 0)
		return;

	x = (u16) ret;

	ret = da9052_reg_read(tsi->da9052, DA9052_TSI_Y_MSB_REG);
	if (ret < 0)
		return;

	y = (u16) ret;

	ret = da9052_reg_read(tsi->da9052, DA9052_TSI_Z_MSB_REG);
	if (ret < 0)
		return;

	z = (u16) ret;

	ret = da9052_reg_read(tsi->da9052, DA9052_TSI_LSB_REG);
	if (ret < 0)
		return;

	v = (u8) ret;

	x = ((x << 2) & 0x3fc) | (v & 0x3);
	y = ((y << 2) & 0x3fc) | ((v & 0xc) >> 2);
	z = ((z << 2) & 0x3fc) | ((v & 0x30) >> 4);

	input_report_key(input, BTN_TOUCH, 1);
	input_report_abs(input, ABS_X, x);
	input_report_abs(input, ABS_Y, y);
	input_report_abs(input, ABS_PRESSURE, z);
	input_sync(input);
}

static irqreturn_t da9052_ts_datardy_irq(int irq, void *data)
{
	struct da9052_tsi *tsi = data;

	da9052_ts_read(tsi);

	return IRQ_HANDLED;
}

static void da9052_ts_pen_work(struct work_struct *work)
{
	struct da9052_tsi *tsi = container_of(work, struct da9052_tsi,
					      ts_pen_work.work);
	if (!tsi->stopped) {
		int ret = da9052_reg_read(tsi->da9052, DA9052_TSI_LSB_REG);
		if (ret < 0 || (ret & TSI_PEN_DOWN_STATUS)) {
			/* Pen is still DOWN (or read error) */
			schedule_delayed_work(&tsi->ts_pen_work, HZ / 50);
		} else {
			struct input_dev *input = tsi->dev;

			/* Pen UP */
			da9052_ts_adc_toggle(tsi, false);

			/* Report Pen UP */
			input_report_key(input, BTN_TOUCH, 0);
			input_report_abs(input, ABS_PRESSURE, 0);
			input_sync(input);

			/*
			 * FIXME: Fixes the unhandled irq issue when quick
			 * pen down and pen up events occurs
			 */
			ret = da9052_reg_update(tsi->da9052,
						DA9052_EVENT_B_REG, 0xC0, 0xC0);
			if (ret < 0)
				return;

			/* Mask TSI_READY event and unmask PEN_DOWN event */
			da9052_disable_irq(tsi->da9052, DA9052_IRQ_TSIREADY);
			da9052_enable_irq(tsi->da9052, DA9052_IRQ_PENDOWN);
		}
	}
}

static int da9052_ts_configure_gpio(struct da9052 *da9052)
{
	int error;

	error = da9052_reg_update(da9052, DA9052_GPIO_2_3_REG, 0x30, 0);
	if (error < 0)
		return error;

	error = da9052_reg_update(da9052, DA9052_GPIO_4_5_REG, 0x33, 0);
	if (error < 0)
		return error;

	error = da9052_reg_update(da9052, DA9052_GPIO_6_7_REG, 0x33, 0);
	if (error < 0)
		return error;

	return 0;
}

static int da9052_configure_tsi(struct da9052_tsi *tsi)
{
	int error;

	error = da9052_ts_configure_gpio(tsi->da9052);
	if (error)
		return error;

	/* Measure TSI sample every 1ms */
	error = da9052_reg_update(tsi->da9052, DA9052_ADC_CONT_REG,
				  1 << 6, 1 << 6);
	if (error < 0)
		return error;

	/* TSI_DELAY: 3 slots, TSI_SKIP: 0 slots, TSI_MODE: XYZP */
	error = da9052_reg_update(tsi->da9052, DA9052_TSI_CONT_A_REG, 0xFC, 0xC0);
	if (error < 0)
		return error;

	/* Supply TSIRef through LD09 */
	error = da9052_reg_write(tsi->da9052, DA9052_LDO9_REG, 0x59);
	if (error < 0)
		return error;

	return 0;
}

static int da9052_ts_input_open(struct input_dev *input_dev)
{
	struct da9052_tsi *tsi = input_get_drvdata(input_dev);

	tsi->stopped = false;
	mb();

	/* Unmask PEN_DOWN event */
	da9052_enable_irq(tsi->da9052, DA9052_IRQ_PENDOWN);

	/* Enable Pen Detect Circuit */
	return da9052_reg_update(tsi->da9052, DA9052_TSI_CONT_A_REG,
				 1 << 1, 1 << 1);
}

static void da9052_ts_input_close(struct input_dev *input_dev)
{
	struct da9052_tsi *tsi = input_get_drvdata(input_dev);

	tsi->stopped = true;
	mb();
	da9052_disable_irq(tsi->da9052, DA9052_IRQ_PENDOWN);
	cancel_delayed_work_sync(&tsi->ts_pen_work);

	if (tsi->adc_on) {
		da9052_disable_irq(tsi->da9052, DA9052_IRQ_TSIREADY);
		da9052_ts_adc_toggle(tsi, false);

		/*
		 * If ADC was on that means that pendwn IRQ was disabled
		 * twice and we need to enable it to keep enable/disable
		 * counter balanced. IRQ is still off though.
		 */
		da9052_enable_irq(tsi->da9052, DA9052_IRQ_PENDOWN);
	}

	/* Disable Pen Detect Circuit */
	da9052_reg_update(tsi->da9052, DA9052_TSI_CONT_A_REG, 1 << 1, 0);
}

static int da9052_ts_probe(struct platform_device *pdev)
{
	struct da9052 *da9052;
	struct da9052_tsi *tsi;
	struct input_dev *input_dev;
	int error;

	da9052 = dev_get_drvdata(pdev->dev.parent);
	if (!da9052)
		return -EINVAL;

	tsi = kzalloc(sizeof(struct da9052_tsi), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!tsi || !input_dev) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	tsi->da9052 = da9052;
	tsi->dev = input_dev;
	tsi->stopped = true;
	INIT_DELAYED_WORK(&tsi->ts_pen_work, da9052_ts_pen_work);

	input_dev->id.version = 0x0101;
	input_dev->id.vendor = 0x15B6;
	input_dev->id.product = 0x9052;
	input_dev->name = "Dialog DA9052 TouchScreen Driver";
	input_dev->dev.parent = &pdev->dev;
	input_dev->open = da9052_ts_input_open;
	input_dev->close = da9052_ts_input_close;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);

	input_set_abs_params(input_dev, ABS_X, 0, 1023, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 1023, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 1023, 0, 0);

	input_set_drvdata(input_dev, tsi);

	/* Disable Pen Detect Circuit */
	da9052_reg_update(tsi->da9052, DA9052_TSI_CONT_A_REG, 1 << 1, 0);

	/* Disable ADC */
	da9052_ts_adc_toggle(tsi, false);

	error = da9052_request_irq(tsi->da9052, DA9052_IRQ_PENDOWN,
				"pendown-irq", da9052_ts_pendwn_irq, tsi);
	if (error) {
		dev_err(tsi->da9052->dev,
			"Failed to register PENDWN IRQ: %d\n", error);
		goto err_free_mem;
	}

	error = da9052_request_irq(tsi->da9052, DA9052_IRQ_TSIREADY,
				"tsiready-irq", da9052_ts_datardy_irq, tsi);
	if (error) {
		dev_err(tsi->da9052->dev,
			"Failed to register TSIRDY IRQ :%d\n", error);
		goto err_free_pendwn_irq;
	}

	/* Mask PEN_DOWN and TSI_READY events */
	da9052_disable_irq(tsi->da9052, DA9052_IRQ_PENDOWN);
	da9052_disable_irq(tsi->da9052, DA9052_IRQ_TSIREADY);

	error = da9052_configure_tsi(tsi);
	if (error)
		goto err_free_datardy_irq;

	error = input_register_device(tsi->dev);
	if (error)
		goto err_free_datardy_irq;

	platform_set_drvdata(pdev, tsi);

	return 0;

err_free_datardy_irq:
	da9052_free_irq(tsi->da9052, DA9052_IRQ_TSIREADY, tsi);
err_free_pendwn_irq:
	da9052_free_irq(tsi->da9052, DA9052_IRQ_PENDOWN, tsi);
err_free_mem:
	kfree(tsi);
	input_free_device(input_dev);

	return error;
}

static int  da9052_ts_remove(struct platform_device *pdev)
{
	struct da9052_tsi *tsi = platform_get_drvdata(pdev);

	da9052_reg_write(tsi->da9052, DA9052_LDO9_REG, 0x19);

	da9052_free_irq(tsi->da9052, DA9052_IRQ_TSIREADY, tsi);
	da9052_free_irq(tsi->da9052, DA9052_IRQ_PENDOWN, tsi);

	input_unregister_device(tsi->dev);
	kfree(tsi);

	return 0;
}

static struct platform_driver da9052_tsi_driver = {
	.probe	= da9052_ts_probe,
	.remove	= da9052_ts_remove,
	.driver	= {
		.name	= "da9052-tsi",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(da9052_tsi_driver);

MODULE_DESCRIPTION("Touchscreen driver for Dialog Semiconductor DA9052");
MODULE_AUTHOR("Anthony Olech <Anthony.Olech@diasemi.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9052-tsi");
