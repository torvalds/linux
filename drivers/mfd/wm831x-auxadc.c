/*
 * wm831x-auxadc.c  --  AUXADC for Wolfson WM831x PMICs
 *
 * Copyright 2009-2011 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/slab.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/pdata.h>
#include <linux/mfd/wm831x/irq.h>
#include <linux/mfd/wm831x/auxadc.h>
#include <linux/mfd/wm831x/otp.h>
#include <linux/mfd/wm831x/regulator.h>

/**
 * wm831x_auxadc_read: Read a value from the WM831x AUXADC
 *
 * @wm831x: Device to read from.
 * @input: AUXADC input to read.
 */
int wm831x_auxadc_read(struct wm831x *wm831x, enum wm831x_auxadc input)
{
	int ret, src, irq_masked, timeout;

	/* Are we using the interrupt? */
	irq_masked = wm831x_reg_read(wm831x, WM831X_INTERRUPT_STATUS_1_MASK);
	irq_masked &= WM831X_AUXADC_DATA_EINT;

	mutex_lock(&wm831x->auxadc_lock);

	ret = wm831x_set_bits(wm831x, WM831X_AUXADC_CONTROL,
			      WM831X_AUX_ENA, WM831X_AUX_ENA);
	if (ret < 0) {
		dev_err(wm831x->dev, "Failed to enable AUXADC: %d\n", ret);
		goto out;
	}

	/* We force a single source at present */
	src = input;
	ret = wm831x_reg_write(wm831x, WM831X_AUXADC_SOURCE,
			       1 << src);
	if (ret < 0) {
		dev_err(wm831x->dev, "Failed to set AUXADC source: %d\n", ret);
		goto out;
	}

	/* Clear any notification from a very late arriving interrupt */
	try_wait_for_completion(&wm831x->auxadc_done);

	ret = wm831x_set_bits(wm831x, WM831X_AUXADC_CONTROL,
			      WM831X_AUX_CVT_ENA, WM831X_AUX_CVT_ENA);
	if (ret < 0) {
		dev_err(wm831x->dev, "Failed to start AUXADC: %d\n", ret);
		goto disable;
	}

	if (irq_masked) {
		/* If we're not using interrupts then poll the
		 * interrupt status register */
		timeout = 5;
		while (timeout) {
			msleep(1);

			ret = wm831x_reg_read(wm831x,
					      WM831X_INTERRUPT_STATUS_1);
			if (ret < 0) {
				dev_err(wm831x->dev,
					"ISR 1 read failed: %d\n", ret);
				goto disable;
			}

			/* Did it complete? */
			if (ret & WM831X_AUXADC_DATA_EINT) {
				wm831x_reg_write(wm831x,
						 WM831X_INTERRUPT_STATUS_1,
						 WM831X_AUXADC_DATA_EINT);
				break;
			} else {
				dev_err(wm831x->dev,
					"AUXADC conversion timeout\n");
				ret = -EBUSY;
				goto disable;
			}
		}

		ret = wm831x_reg_read(wm831x, WM831X_AUXADC_DATA);
		if (ret < 0) {
			dev_err(wm831x->dev,
				"Failed to read AUXADC data: %d\n", ret);
			goto disable;
		}

		wm831x->auxadc_data = ret;

	} else {
		/* If we are using interrupts then wait for the
		 * interrupt to complete.  Use an extremely long
		 * timeout to handle situations with heavy load where
		 * the notification of the interrupt may be delayed by
		 * threaded IRQ handling. */
		if (!wait_for_completion_timeout(&wm831x->auxadc_done,
						 msecs_to_jiffies(500))) {
			dev_err(wm831x->dev, "Timed out waiting for AUXADC\n");
			ret = -EBUSY;
			goto disable;
		}
	}

	src = ((wm831x->auxadc_data & WM831X_AUX_DATA_SRC_MASK)
	       >> WM831X_AUX_DATA_SRC_SHIFT) - 1;

	if (src == 14)
		src = WM831X_AUX_CAL;

	if (src != input) {
		dev_err(wm831x->dev, "Data from source %d not %d\n",
			src, input);
		ret = -EINVAL;
	} else {
		ret = wm831x->auxadc_data & WM831X_AUX_DATA_MASK;
	}

disable:
	wm831x_set_bits(wm831x, WM831X_AUXADC_CONTROL, WM831X_AUX_ENA, 0);
out:
	mutex_unlock(&wm831x->auxadc_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(wm831x_auxadc_read);

static irqreturn_t wm831x_auxadc_irq(int irq, void *irq_data)
{
	struct wm831x *wm831x = irq_data;
	int ret;

	ret = wm831x_reg_read(wm831x, WM831X_AUXADC_DATA);
	if (ret < 0) {
		dev_err(wm831x->dev,
			"Failed to read AUXADC data: %d\n", ret);
		wm831x->auxadc_data = 0xffff;
	} else {
		wm831x->auxadc_data = ret;
	}

	complete(&wm831x->auxadc_done);

	return IRQ_HANDLED;
}

/**
 * wm831x_auxadc_read_uv: Read a voltage from the WM831x AUXADC
 *
 * @wm831x: Device to read from.
 * @input: AUXADC input to read.
 */
int wm831x_auxadc_read_uv(struct wm831x *wm831x, enum wm831x_auxadc input)
{
	int ret;

	ret = wm831x_auxadc_read(wm831x, input);
	if (ret < 0)
		return ret;

	ret *= 1465;

	return ret;
}
EXPORT_SYMBOL_GPL(wm831x_auxadc_read_uv);

void wm831x_auxadc_init(struct wm831x *wm831x)
{
	int ret;

	mutex_init(&wm831x->auxadc_lock);
	init_completion(&wm831x->auxadc_done);

	if (wm831x->irq_base) {
		ret = request_threaded_irq(wm831x->irq_base +
					   WM831X_IRQ_AUXADC_DATA,
					   NULL, wm831x_auxadc_irq, 0,
					   "auxadc", wm831x);
		if (ret < 0)
			dev_err(wm831x->dev, "AUXADC IRQ request failed: %d\n",
				ret);
	}
}
