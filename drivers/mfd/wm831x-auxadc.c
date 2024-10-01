// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * wm831x-auxadc.c  --  AUXADC for Wolfson WM831x PMICs
 *
 * Copyright 2009-2011 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/slab.h>
#include <linux/list.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/pdata.h>
#include <linux/mfd/wm831x/irq.h>
#include <linux/mfd/wm831x/auxadc.h>
#include <linux/mfd/wm831x/otp.h>
#include <linux/mfd/wm831x/regulator.h>

struct wm831x_auxadc_req {
	struct list_head list;
	enum wm831x_auxadc input;
	int val;
	struct completion done;
};

static int wm831x_auxadc_read_irq(struct wm831x *wm831x,
				  enum wm831x_auxadc input)
{
	struct wm831x_auxadc_req *req;
	int ret;
	bool ena = false;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	init_completion(&req->done);
	req->input = input;
	req->val = -ETIMEDOUT;

	mutex_lock(&wm831x->auxadc_lock);

	/* Enqueue the request */
	list_add(&req->list, &wm831x->auxadc_pending);

	ena = !wm831x->auxadc_active;

	if (ena) {
		ret = wm831x_set_bits(wm831x, WM831X_AUXADC_CONTROL,
				      WM831X_AUX_ENA, WM831X_AUX_ENA);
		if (ret != 0) {
			dev_err(wm831x->dev, "Failed to enable AUXADC: %d\n",
				ret);
			goto out;
		}
	}

	/* Enable the conversion if not already running */
	if (!(wm831x->auxadc_active & (1 << input))) {
		ret = wm831x_set_bits(wm831x, WM831X_AUXADC_SOURCE,
				      1 << input, 1 << input);
		if (ret != 0) {
			dev_err(wm831x->dev,
				"Failed to set AUXADC source: %d\n", ret);
			goto out;
		}

		wm831x->auxadc_active |= 1 << input;
	}

	/* We convert at the fastest rate possible */
	if (ena) {
		ret = wm831x_set_bits(wm831x, WM831X_AUXADC_CONTROL,
				      WM831X_AUX_CVT_ENA |
				      WM831X_AUX_RATE_MASK,
				      WM831X_AUX_CVT_ENA |
				      WM831X_AUX_RATE_MASK);
		if (ret != 0) {
			dev_err(wm831x->dev, "Failed to start AUXADC: %d\n",
				ret);
			goto out;
		}
	}

	mutex_unlock(&wm831x->auxadc_lock);

	/* Wait for an interrupt */
	wait_for_completion_timeout(&req->done, msecs_to_jiffies(500));

	mutex_lock(&wm831x->auxadc_lock);
	ret = req->val;

out:
	list_del(&req->list);
	mutex_unlock(&wm831x->auxadc_lock);

	kfree(req);

	return ret;
}

static irqreturn_t wm831x_auxadc_irq(int irq, void *irq_data)
{
	struct wm831x *wm831x = irq_data;
	struct wm831x_auxadc_req *req;
	int ret, input, val;

	ret = wm831x_reg_read(wm831x, WM831X_AUXADC_DATA);
	if (ret < 0) {
		dev_err(wm831x->dev,
			"Failed to read AUXADC data: %d\n", ret);
		return IRQ_NONE;
	}

	input = ((ret & WM831X_AUX_DATA_SRC_MASK)
		 >> WM831X_AUX_DATA_SRC_SHIFT) - 1;

	if (input == 14)
		input = WM831X_AUX_CAL;

	val = ret & WM831X_AUX_DATA_MASK;

	mutex_lock(&wm831x->auxadc_lock);

	/* Disable this conversion, we're about to complete all users */
	wm831x_set_bits(wm831x, WM831X_AUXADC_SOURCE,
			1 << input, 0);
	wm831x->auxadc_active &= ~(1 << input);

	/* Turn off the entire convertor if idle */
	if (!wm831x->auxadc_active)
		wm831x_reg_write(wm831x, WM831X_AUXADC_CONTROL, 0);

	/* Wake up any threads waiting for this request */
	list_for_each_entry(req, &wm831x->auxadc_pending, list) {
		if (req->input == input) {
			req->val = val;
			complete(&req->done);
		}
	}

	mutex_unlock(&wm831x->auxadc_lock);

	return IRQ_HANDLED;
}

static int wm831x_auxadc_read_polled(struct wm831x *wm831x,
				     enum wm831x_auxadc input)
{
	int ret, src, timeout;

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

	ret = wm831x_set_bits(wm831x, WM831X_AUXADC_CONTROL,
			      WM831X_AUX_CVT_ENA, WM831X_AUX_CVT_ENA);
	if (ret < 0) {
		dev_err(wm831x->dev, "Failed to start AUXADC: %d\n", ret);
		goto disable;
	}

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

	src = ((ret & WM831X_AUX_DATA_SRC_MASK)
	       >> WM831X_AUX_DATA_SRC_SHIFT) - 1;

	if (src == 14)
		src = WM831X_AUX_CAL;

	if (src != input) {
		dev_err(wm831x->dev, "Data from source %d not %d\n",
			src, input);
		ret = -EINVAL;
	} else {
		ret &= WM831X_AUX_DATA_MASK;
	}

disable:
	wm831x_set_bits(wm831x, WM831X_AUXADC_CONTROL, WM831X_AUX_ENA, 0);
out:
	mutex_unlock(&wm831x->auxadc_lock);
	return ret;
}

/**
 * wm831x_auxadc_read: Read a value from the WM831x AUXADC
 *
 * @wm831x: Device to read from.
 * @input: AUXADC input to read.
 */
int wm831x_auxadc_read(struct wm831x *wm831x, enum wm831x_auxadc input)
{
	return wm831x->auxadc_read(wm831x, input);
}
EXPORT_SYMBOL_GPL(wm831x_auxadc_read);

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
	INIT_LIST_HEAD(&wm831x->auxadc_pending);

	if (wm831x->irq) {
		wm831x->auxadc_read = wm831x_auxadc_read_irq;

		ret = request_threaded_irq(wm831x_irq(wm831x,
						      WM831X_IRQ_AUXADC_DATA),
					   NULL, wm831x_auxadc_irq,
					   IRQF_ONESHOT,
					   "auxadc", wm831x);
		if (ret < 0) {
			dev_err(wm831x->dev, "AUXADC IRQ request failed: %d\n",
				ret);
			wm831x->auxadc_read = NULL;
		}
	}

	if (!wm831x->auxadc_read)
		wm831x->auxadc_read = wm831x_auxadc_read_polled;
}
