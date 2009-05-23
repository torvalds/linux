/*
 * wm97xx-core.c  --  Touch screen driver core for Wolfson WM9705, WM9712
 *                    and WM9713 AC97 Codecs.
 *
 * Copyright 2003, 2004, 2005, 2006, 2007, 2008 Wolfson Microelectronics PLC.
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 * Parts Copyright : Ian Molton <spyro@f2s.com>
 *                   Andrew Zabolotny <zap@homelink.ru>
 *                   Russell King <rmk@arm.linux.org.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 * Notes:
 *
 *  Features:
 *       - supports WM9705, WM9712, WM9713
 *       - polling mode
 *       - continuous mode (arch-dependent)
 *       - adjustable rpu/dpp settings
 *       - adjustable pressure current
 *       - adjustable sample settle delay
 *       - 4 and 5 wire touchscreens (5 wire is WM9712 only)
 *       - pen down detection
 *       - battery monitor
 *       - sample AUX adcs
 *       - power management
 *       - codec GPIO
 *       - codec event notification
 * Todo
 *       - Support for async sampling control for noisy LCDs.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/pm.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/workqueue.h>
#include <linux/wm97xx.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#define TS_NAME			"wm97xx"
#define WM_CORE_VERSION		"1.00"
#define DEFAULT_PRESSURE	0xb0c0


/*
 * Touchscreen absolute values
 *
 * These parameters are used to help the input layer discard out of
 * range readings and reduce jitter etc.
 *
 *   o min, max:- indicate the min and max values your touch screen returns
 *   o fuzz:- use a higher number to reduce jitter
 *
 * The default values correspond to Mainstone II in QVGA mode
 *
 * Please read
 * Documentation/input/input-programming.txt for more details.
 */

static int abs_x[3] = {350, 3900, 5};
module_param_array(abs_x, int, NULL, 0);
MODULE_PARM_DESC(abs_x, "Touchscreen absolute X min, max, fuzz");

static int abs_y[3] = {320, 3750, 40};
module_param_array(abs_y, int, NULL, 0);
MODULE_PARM_DESC(abs_y, "Touchscreen absolute Y min, max, fuzz");

static int abs_p[3] = {0, 150, 4};
module_param_array(abs_p, int, NULL, 0);
MODULE_PARM_DESC(abs_p, "Touchscreen absolute Pressure min, max, fuzz");

/*
 * wm97xx IO access, all IO locking done by AC97 layer
 */
int wm97xx_reg_read(struct wm97xx *wm, u16 reg)
{
	if (wm->ac97)
		return wm->ac97->bus->ops->read(wm->ac97, reg);
	else
		return -1;
}
EXPORT_SYMBOL_GPL(wm97xx_reg_read);

void wm97xx_reg_write(struct wm97xx *wm, u16 reg, u16 val)
{
	/* cache digitiser registers */
	if (reg >= AC97_WM9713_DIG1 && reg <= AC97_WM9713_DIG3)
		wm->dig[(reg - AC97_WM9713_DIG1) >> 1] = val;

	/* cache gpio regs */
	if (reg >= AC97_GPIO_CFG && reg <= AC97_MISC_AFE)
		wm->gpio[(reg - AC97_GPIO_CFG) >> 1] = val;

	/* wm9713 irq reg */
	if (reg == 0x5a)
		wm->misc = val;

	if (wm->ac97)
		wm->ac97->bus->ops->write(wm->ac97, reg, val);
}
EXPORT_SYMBOL_GPL(wm97xx_reg_write);

/**
 * wm97xx_read_aux_adc - Read the aux adc.
 * @wm: wm97xx device.
 * @adcsel: codec ADC to be read
 *
 * Reads the selected AUX ADC.
 */

int wm97xx_read_aux_adc(struct wm97xx *wm, u16 adcsel)
{
	int power_adc = 0, auxval;
	u16 power = 0;

	/* get codec */
	mutex_lock(&wm->codec_mutex);

	/* When the touchscreen is not in use, we may have to power up
	 * the AUX ADC before we can use sample the AUX inputs->
	 */
	if (wm->id == WM9713_ID2 &&
	    (power = wm97xx_reg_read(wm, AC97_EXTENDED_MID)) & 0x8000) {
		power_adc = 1;
		wm97xx_reg_write(wm, AC97_EXTENDED_MID, power & 0x7fff);
	}

	/* Prepare the codec for AUX reading */
	wm->codec->aux_prepare(wm);

	/* Turn polling mode on to read AUX ADC */
	wm->pen_probably_down = 1;
	wm->codec->poll_sample(wm, adcsel, &auxval);

	if (power_adc)
		wm97xx_reg_write(wm, AC97_EXTENDED_MID, power | 0x8000);

	wm->codec->dig_restore(wm);

	wm->pen_probably_down = 0;

	mutex_unlock(&wm->codec_mutex);
	return auxval & 0xfff;
}
EXPORT_SYMBOL_GPL(wm97xx_read_aux_adc);

/**
 * wm97xx_get_gpio - Get the status of a codec GPIO.
 * @wm: wm97xx device.
 * @gpio: gpio
 *
 * Get the status of a codec GPIO pin
 */

enum wm97xx_gpio_status wm97xx_get_gpio(struct wm97xx *wm, u32 gpio)
{
	u16 status;
	enum wm97xx_gpio_status ret;

	mutex_lock(&wm->codec_mutex);
	status = wm97xx_reg_read(wm, AC97_GPIO_STATUS);

	if (status & gpio)
		ret = WM97XX_GPIO_HIGH;
	else
		ret = WM97XX_GPIO_LOW;

	mutex_unlock(&wm->codec_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(wm97xx_get_gpio);

/**
 * wm97xx_set_gpio - Set the status of a codec GPIO.
 * @wm: wm97xx device.
 * @gpio: gpio
 *
 *
 * Set the status of a codec GPIO pin
 */

void wm97xx_set_gpio(struct wm97xx *wm, u32 gpio,
				enum wm97xx_gpio_status status)
{
	u16 reg;

	mutex_lock(&wm->codec_mutex);
	reg = wm97xx_reg_read(wm, AC97_GPIO_STATUS);

	if (status & WM97XX_GPIO_HIGH)
		reg |= gpio;
	else
		reg &= ~gpio;

	if (wm->id == WM9712_ID2)
		wm97xx_reg_write(wm, AC97_GPIO_STATUS, reg << 1);
	else
		wm97xx_reg_write(wm, AC97_GPIO_STATUS, reg);
	mutex_unlock(&wm->codec_mutex);
}
EXPORT_SYMBOL_GPL(wm97xx_set_gpio);

/*
 * Codec GPIO pin configuration, this sets pin direction, polarity,
 * stickyness and wake up.
 */
void wm97xx_config_gpio(struct wm97xx *wm, u32 gpio, enum wm97xx_gpio_dir dir,
		   enum wm97xx_gpio_pol pol, enum wm97xx_gpio_sticky sticky,
		   enum wm97xx_gpio_wake wake)
{
	u16 reg;

	mutex_lock(&wm->codec_mutex);
	reg = wm97xx_reg_read(wm, AC97_GPIO_POLARITY);

	if (pol == WM97XX_GPIO_POL_HIGH)
		reg |= gpio;
	else
		reg &= ~gpio;

	wm97xx_reg_write(wm, AC97_GPIO_POLARITY, reg);
	reg = wm97xx_reg_read(wm, AC97_GPIO_STICKY);

	if (sticky == WM97XX_GPIO_STICKY)
		reg |= gpio;
	else
		reg &= ~gpio;

	wm97xx_reg_write(wm, AC97_GPIO_STICKY, reg);
	reg = wm97xx_reg_read(wm, AC97_GPIO_WAKEUP);

	if (wake == WM97XX_GPIO_WAKE)
		reg |= gpio;
	else
		reg &= ~gpio;

	wm97xx_reg_write(wm, AC97_GPIO_WAKEUP, reg);
	reg = wm97xx_reg_read(wm, AC97_GPIO_CFG);

	if (dir == WM97XX_GPIO_IN)
		reg |= gpio;
	else
		reg &= ~gpio;

	wm97xx_reg_write(wm, AC97_GPIO_CFG, reg);
	mutex_unlock(&wm->codec_mutex);
}
EXPORT_SYMBOL_GPL(wm97xx_config_gpio);

/*
 * Configure the WM97XX_PRP value to use while system is suspended.
 * If a value other than 0 is set then WM97xx pen detection will be
 * left enabled in the configured mode while the system is in suspend,
 * the device has users and suspend has not been disabled via the
 * wakeup sysfs entries.
 *
 * @wm:   WM97xx device to configure
 * @mode: WM97XX_PRP value to configure while suspended
 */
void wm97xx_set_suspend_mode(struct wm97xx *wm, u16 mode)
{
	wm->suspend_mode = mode;
	device_init_wakeup(&wm->input_dev->dev, mode != 0);
}
EXPORT_SYMBOL_GPL(wm97xx_set_suspend_mode);

/*
 * Handle a pen down interrupt.
 */
static void wm97xx_pen_irq_worker(struct work_struct *work)
{
	struct wm97xx *wm = container_of(work, struct wm97xx, pen_event_work);
	int pen_was_down = wm->pen_is_down;

	/* do we need to enable the touch panel reader */
	if (wm->id == WM9705_ID2) {
		if (wm97xx_reg_read(wm, AC97_WM97XX_DIGITISER_RD) &
					WM97XX_PEN_DOWN)
			wm->pen_is_down = 1;
		else
			wm->pen_is_down = 0;
	} else {
		u16 status, pol;
		mutex_lock(&wm->codec_mutex);
		status = wm97xx_reg_read(wm, AC97_GPIO_STATUS);
		pol = wm97xx_reg_read(wm, AC97_GPIO_POLARITY);

		if (WM97XX_GPIO_13 & pol & status) {
			wm->pen_is_down = 1;
			wm97xx_reg_write(wm, AC97_GPIO_POLARITY, pol &
						~WM97XX_GPIO_13);
		} else {
			wm->pen_is_down = 0;
			wm97xx_reg_write(wm, AC97_GPIO_POLARITY, pol |
					 WM97XX_GPIO_13);
		}

		if (wm->id == WM9712_ID2)
			wm97xx_reg_write(wm, AC97_GPIO_STATUS, (status &
						~WM97XX_GPIO_13) << 1);
		else
			wm97xx_reg_write(wm, AC97_GPIO_STATUS, status &
						~WM97XX_GPIO_13);
		mutex_unlock(&wm->codec_mutex);
	}

	/* If the system is not using continuous mode or it provides a
	 * pen down operation then we need to schedule polls while the
	 * pen is down.  Otherwise the machine driver is responsible
	 * for scheduling reads.
	 */
	if (!wm->mach_ops->acc_enabled || wm->mach_ops->acc_pen_down) {
		if (wm->pen_is_down && !pen_was_down) {
			/* Data is not availiable immediately on pen down */
			queue_delayed_work(wm->ts_workq, &wm->ts_reader, 1);
		}

		/* Let ts_reader report the pen up for debounce. */
		if (!wm->pen_is_down && pen_was_down)
			wm->pen_is_down = 1;
	}

	if (!wm->pen_is_down && wm->mach_ops->acc_enabled)
		wm->mach_ops->acc_pen_up(wm);

	wm->mach_ops->irq_enable(wm, 1);
}

/*
 * Codec PENDOWN irq handler
 *
 * We have to disable the codec interrupt in the handler because it
 * can take upto 1ms to clear the interrupt source. We schedule a task
 * in a work queue to do the actual interaction with the chip.  The
 * interrupt is then enabled again in the slow handler when the source
 * has been cleared.
 */
static irqreturn_t wm97xx_pen_interrupt(int irq, void *dev_id)
{
	struct wm97xx *wm = dev_id;

	if (!work_pending(&wm->pen_event_work)) {
		wm->mach_ops->irq_enable(wm, 0);
		queue_work(wm->ts_workq, &wm->pen_event_work);
	}

	return IRQ_HANDLED;
}

/*
 * initialise pen IRQ handler and workqueue
 */
static int wm97xx_init_pen_irq(struct wm97xx *wm)
{
	u16 reg;

	/* If an interrupt is supplied an IRQ enable operation must also be
	 * provided. */
	BUG_ON(!wm->mach_ops->irq_enable);

	if (request_irq(wm->pen_irq, wm97xx_pen_interrupt, IRQF_SHARED,
			"wm97xx-pen", wm)) {
		dev_err(wm->dev,
			"Failed to register pen down interrupt, polling");
		wm->pen_irq = 0;
		return -EINVAL;
	}

	/* Configure GPIO as interrupt source on WM971x */
	if (wm->id != WM9705_ID2) {
		BUG_ON(!wm->mach_ops->irq_gpio);
		reg = wm97xx_reg_read(wm, AC97_MISC_AFE);
		wm97xx_reg_write(wm, AC97_MISC_AFE,
				reg & ~(wm->mach_ops->irq_gpio));
		reg = wm97xx_reg_read(wm, 0x5a);
		wm97xx_reg_write(wm, 0x5a, reg & ~0x0001);
	}

	return 0;
}

static int wm97xx_read_samples(struct wm97xx *wm)
{
	struct wm97xx_data data;
	int rc;

	mutex_lock(&wm->codec_mutex);

	if (wm->mach_ops && wm->mach_ops->acc_enabled)
		rc = wm->mach_ops->acc_pen_down(wm);
	else
		rc = wm->codec->poll_touch(wm, &data);

	if (rc & RC_PENUP) {
		if (wm->pen_is_down) {
			wm->pen_is_down = 0;
			dev_dbg(wm->dev, "pen up\n");
			input_report_abs(wm->input_dev, ABS_PRESSURE, 0);
			input_report_key(wm->input_dev, BTN_TOUCH, 0);
			input_sync(wm->input_dev);
		} else if (!(rc & RC_AGAIN)) {
			/* We need high frequency updates only while
			* pen is down, the user never will be able to
			* touch screen faster than a few times per
			* second... On the other hand, when the user
			* is actively working with the touchscreen we
			* don't want to lose the quick response. So we
			* will slowly increase sleep time after the
			* pen is up and quicky restore it to ~one task
			* switch when pen is down again.
			*/
			if (wm->ts_reader_interval < HZ / 10)
				wm->ts_reader_interval++;
		}

	} else if (rc & RC_VALID) {
		dev_dbg(wm->dev,
			"pen down: x=%x:%d, y=%x:%d, pressure=%x:%d\n",
			data.x >> 12, data.x & 0xfff, data.y >> 12,
			data.y & 0xfff, data.p >> 12, data.p & 0xfff);
		input_report_abs(wm->input_dev, ABS_X, data.x & 0xfff);
		input_report_abs(wm->input_dev, ABS_Y, data.y & 0xfff);
		input_report_abs(wm->input_dev, ABS_PRESSURE, data.p & 0xfff);
		input_report_key(wm->input_dev, BTN_TOUCH, 1);
		input_sync(wm->input_dev);
		wm->pen_is_down = 1;
		wm->ts_reader_interval = wm->ts_reader_min_interval;
	} else if (rc & RC_PENDOWN) {
		dev_dbg(wm->dev, "pen down\n");
		wm->pen_is_down = 1;
		wm->ts_reader_interval = wm->ts_reader_min_interval;
	}

	mutex_unlock(&wm->codec_mutex);
	return rc;
}

/*
* The touchscreen sample reader.
*/
static void wm97xx_ts_reader(struct work_struct *work)
{
	int rc;
	struct wm97xx *wm = container_of(work, struct wm97xx, ts_reader.work);

	BUG_ON(!wm->codec);

	do {
		rc = wm97xx_read_samples(wm);
	} while (rc & RC_AGAIN);

	if (wm->pen_is_down || !wm->pen_irq)
		queue_delayed_work(wm->ts_workq, &wm->ts_reader,
				   wm->ts_reader_interval);
}

/**
 * wm97xx_ts_input_open - Open the touch screen input device.
 * @idev:	Input device to be opened.
 *
 * Called by the input sub system to open a wm97xx touchscreen device.
 * Starts the touchscreen thread and touch digitiser.
 */
static int wm97xx_ts_input_open(struct input_dev *idev)
{
	struct wm97xx *wm = input_get_drvdata(idev);

	wm->ts_workq = create_singlethread_workqueue("kwm97xx");
	if (wm->ts_workq == NULL) {
		dev_err(wm->dev,
			"Failed to create workqueue\n");
		return -EINVAL;
	}

	/* start digitiser */
	if (wm->mach_ops && wm->mach_ops->acc_enabled)
		wm->codec->acc_enable(wm, 1);
	wm->codec->dig_enable(wm, 1);

	INIT_DELAYED_WORK(&wm->ts_reader, wm97xx_ts_reader);
	INIT_WORK(&wm->pen_event_work, wm97xx_pen_irq_worker);

	wm->ts_reader_min_interval = HZ >= 100 ? HZ / 100 : 1;
	if (wm->ts_reader_min_interval < 1)
		wm->ts_reader_min_interval = 1;
	wm->ts_reader_interval = wm->ts_reader_min_interval;

	wm->pen_is_down = 0;
	if (wm->pen_irq)
		wm97xx_init_pen_irq(wm);
	else
		dev_err(wm->dev, "No IRQ specified\n");

	/* If we either don't have an interrupt for pen down events or
	 * failed to acquire it then we need to poll.
	 */
	if (wm->pen_irq == 0)
		queue_delayed_work(wm->ts_workq, &wm->ts_reader,
				   wm->ts_reader_interval);

	return 0;
}

/**
 * wm97xx_ts_input_close - Close the touch screen input device.
 * @idev:	Input device to be closed.
 *
 * Called by the input sub system to close a wm97xx touchscreen
 * device.  Kills the touchscreen thread and stops the touch
 * digitiser.
 */

static void wm97xx_ts_input_close(struct input_dev *idev)
{
	struct wm97xx *wm = input_get_drvdata(idev);
	u16 reg;

	if (wm->pen_irq) {
		/* Return the interrupt to GPIO usage (disabling it) */
		if (wm->id != WM9705_ID2) {
			BUG_ON(!wm->mach_ops->irq_gpio);
			reg = wm97xx_reg_read(wm, AC97_MISC_AFE);
			wm97xx_reg_write(wm, AC97_MISC_AFE,
					 reg | wm->mach_ops->irq_gpio);
		}

		free_irq(wm->pen_irq, wm);
	}

	wm->pen_is_down = 0;

	/* Balance out interrupt disables/enables */
	if (cancel_work_sync(&wm->pen_event_work))
		wm->mach_ops->irq_enable(wm, 1);

	/* ts_reader rearms itself so we need to explicitly stop it
	 * before we destroy the workqueue.
	 */
	cancel_delayed_work_sync(&wm->ts_reader);

	destroy_workqueue(wm->ts_workq);

	/* stop digitiser */
	wm->codec->dig_enable(wm, 0);
	if (wm->mach_ops && wm->mach_ops->acc_enabled)
		wm->codec->acc_enable(wm, 0);
}

static int wm97xx_probe(struct device *dev)
{
	struct wm97xx *wm;
	int ret = 0, id = 0;

	wm = kzalloc(sizeof(struct wm97xx), GFP_KERNEL);
	if (!wm)
		return -ENOMEM;
	mutex_init(&wm->codec_mutex);

	wm->dev = dev;
	dev->driver_data = wm;
	wm->ac97 = to_ac97_t(dev);

	/* check that we have a supported codec */
	id = wm97xx_reg_read(wm, AC97_VENDOR_ID1);
	if (id != WM97XX_ID1) {
		dev_err(dev, "Device with vendor %04x is not a wm97xx\n", id);
		ret = -ENODEV;
		goto alloc_err;
	}

	wm->id = wm97xx_reg_read(wm, AC97_VENDOR_ID2);

	dev_info(wm->dev, "detected a wm97%02x codec\n", wm->id & 0xff);

	switch (wm->id & 0xff) {
#ifdef CONFIG_TOUCHSCREEN_WM9705
	case 0x05:
		wm->codec = &wm9705_codec;
		break;
#endif
#ifdef CONFIG_TOUCHSCREEN_WM9712
	case 0x12:
		wm->codec = &wm9712_codec;
		break;
#endif
#ifdef CONFIG_TOUCHSCREEN_WM9713
	case 0x13:
		wm->codec = &wm9713_codec;
		break;
#endif
	default:
		dev_err(wm->dev, "Support for wm97%02x not compiled in.\n",
			wm->id & 0xff);
		ret = -ENODEV;
		goto alloc_err;
	}

	/* set up physical characteristics */
	wm->codec->phy_init(wm);

	/* load gpio cache */
	wm->gpio[0] = wm97xx_reg_read(wm, AC97_GPIO_CFG);
	wm->gpio[1] = wm97xx_reg_read(wm, AC97_GPIO_POLARITY);
	wm->gpio[2] = wm97xx_reg_read(wm, AC97_GPIO_STICKY);
	wm->gpio[3] = wm97xx_reg_read(wm, AC97_GPIO_WAKEUP);
	wm->gpio[4] = wm97xx_reg_read(wm, AC97_GPIO_STATUS);
	wm->gpio[5] = wm97xx_reg_read(wm, AC97_MISC_AFE);

	wm->input_dev = input_allocate_device();
	if (wm->input_dev == NULL) {
		ret = -ENOMEM;
		goto alloc_err;
	}

	/* set up touch configuration */
	wm->input_dev->name = "wm97xx touchscreen";
	wm->input_dev->phys = "wm97xx";
	wm->input_dev->open = wm97xx_ts_input_open;
	wm->input_dev->close = wm97xx_ts_input_close;

	__set_bit(EV_ABS, wm->input_dev->evbit);
	__set_bit(EV_KEY, wm->input_dev->evbit);
	__set_bit(BTN_TOUCH, wm->input_dev->keybit);

	input_set_abs_params(wm->input_dev, ABS_X, abs_x[0], abs_x[1],
			     abs_x[2], 0);
	input_set_abs_params(wm->input_dev, ABS_Y, abs_y[0], abs_y[1],
			     abs_y[2], 0);
	input_set_abs_params(wm->input_dev, ABS_PRESSURE, abs_p[0], abs_p[1],
			     abs_p[2], 0);

	input_set_drvdata(wm->input_dev, wm);
	wm->input_dev->dev.parent = dev;

	ret = input_register_device(wm->input_dev);
	if (ret < 0)
		goto dev_alloc_err;

	/* register our battery device */
	wm->battery_dev = platform_device_alloc("wm97xx-battery", -1);
	if (!wm->battery_dev) {
		ret = -ENOMEM;
		goto batt_err;
	}
	platform_set_drvdata(wm->battery_dev, wm);
	wm->battery_dev->dev.parent = dev;
	ret = platform_device_add(wm->battery_dev);
	if (ret < 0)
		goto batt_reg_err;

	/* register our extended touch device (for machine specific
	 * extensions) */
	wm->touch_dev = platform_device_alloc("wm97xx-touch", -1);
	if (!wm->touch_dev) {
		ret = -ENOMEM;
		goto touch_err;
	}
	platform_set_drvdata(wm->touch_dev, wm);
	wm->touch_dev->dev.parent = dev;
	ret = platform_device_add(wm->touch_dev);
	if (ret < 0)
		goto touch_reg_err;

	return ret;

 touch_reg_err:
	platform_device_put(wm->touch_dev);
 touch_err:
	platform_device_unregister(wm->battery_dev);
	wm->battery_dev = NULL;
 batt_reg_err:
	platform_device_put(wm->battery_dev);
 batt_err:
	input_unregister_device(wm->input_dev);
	wm->input_dev = NULL;
 dev_alloc_err:
	input_free_device(wm->input_dev);
 alloc_err:
	kfree(wm);

	return ret;
}

static int wm97xx_remove(struct device *dev)
{
	struct wm97xx *wm = dev_get_drvdata(dev);

	platform_device_unregister(wm->battery_dev);
	platform_device_unregister(wm->touch_dev);
	input_unregister_device(wm->input_dev);
	kfree(wm);

	return 0;
}

#ifdef CONFIG_PM
static int wm97xx_suspend(struct device *dev, pm_message_t state)
{
	struct wm97xx *wm = dev_get_drvdata(dev);
	u16 reg;
	int suspend_mode;

	if (device_may_wakeup(&wm->input_dev->dev))
		suspend_mode = wm->suspend_mode;
	else
		suspend_mode = 0;

	if (wm->input_dev->users)
		cancel_delayed_work_sync(&wm->ts_reader);

	/* Power down the digitiser (bypassing the cache for resume) */
	reg = wm97xx_reg_read(wm, AC97_WM97XX_DIGITISER2);
	reg &= ~WM97XX_PRP_DET_DIG;
	if (wm->input_dev->users)
		reg |= suspend_mode;
	wm->ac97->bus->ops->write(wm->ac97, AC97_WM97XX_DIGITISER2, reg);

	/* WM9713 has an additional power bit - turn it off if there
	 * are no users or if suspend mode is zero. */
	if (wm->id == WM9713_ID2 &&
	    (!wm->input_dev->users || !suspend_mode)) {
		reg = wm97xx_reg_read(wm, AC97_EXTENDED_MID) | 0x8000;
		wm97xx_reg_write(wm, AC97_EXTENDED_MID, reg);
	}

	return 0;
}

static int wm97xx_resume(struct device *dev)
{
	struct wm97xx *wm = dev_get_drvdata(dev);

	/* restore digitiser and gpios */
	if (wm->id == WM9713_ID2) {
		wm97xx_reg_write(wm, AC97_WM9713_DIG1, wm->dig[0]);
		wm97xx_reg_write(wm, 0x5a, wm->misc);
		if (wm->input_dev->users) {
			u16 reg;
			reg = wm97xx_reg_read(wm, AC97_EXTENDED_MID) & 0x7fff;
			wm97xx_reg_write(wm, AC97_EXTENDED_MID, reg);
		}
	}

	wm97xx_reg_write(wm, AC97_WM9713_DIG2, wm->dig[1]);
	wm97xx_reg_write(wm, AC97_WM9713_DIG3, wm->dig[2]);

	wm97xx_reg_write(wm, AC97_GPIO_CFG, wm->gpio[0]);
	wm97xx_reg_write(wm, AC97_GPIO_POLARITY, wm->gpio[1]);
	wm97xx_reg_write(wm, AC97_GPIO_STICKY, wm->gpio[2]);
	wm97xx_reg_write(wm, AC97_GPIO_WAKEUP, wm->gpio[3]);
	wm97xx_reg_write(wm, AC97_GPIO_STATUS, wm->gpio[4]);
	wm97xx_reg_write(wm, AC97_MISC_AFE, wm->gpio[5]);

	if (wm->input_dev->users && !wm->pen_irq) {
		wm->ts_reader_interval = wm->ts_reader_min_interval;
		queue_delayed_work(wm->ts_workq, &wm->ts_reader,
				   wm->ts_reader_interval);
	}

	return 0;
}

#else
#define wm97xx_suspend		NULL
#define wm97xx_resume		NULL
#endif

/*
 * Machine specific operations
 */
int wm97xx_register_mach_ops(struct wm97xx *wm,
			     struct wm97xx_mach_ops *mach_ops)
{
	mutex_lock(&wm->codec_mutex);
	if (wm->mach_ops) {
		mutex_unlock(&wm->codec_mutex);
		return -EINVAL;
	}
	wm->mach_ops = mach_ops;
	mutex_unlock(&wm->codec_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(wm97xx_register_mach_ops);

void wm97xx_unregister_mach_ops(struct wm97xx *wm)
{
	mutex_lock(&wm->codec_mutex);
	wm->mach_ops = NULL;
	mutex_unlock(&wm->codec_mutex);
}
EXPORT_SYMBOL_GPL(wm97xx_unregister_mach_ops);

static struct device_driver wm97xx_driver = {
	.name =		"wm97xx-ts",
	.bus =		&ac97_bus_type,
	.owner =	THIS_MODULE,
	.probe =	wm97xx_probe,
	.remove =	wm97xx_remove,
	.suspend =	wm97xx_suspend,
	.resume =	wm97xx_resume,
};

static int __init wm97xx_init(void)
{
	return driver_register(&wm97xx_driver);
}

static void __exit wm97xx_exit(void)
{
	driver_unregister(&wm97xx_driver);
}

module_init(wm97xx_init);
module_exit(wm97xx_exit);

/* Module information */
MODULE_AUTHOR("Liam Girdwood <lrg@slimlogic.co.uk>");
MODULE_DESCRIPTION("WM97xx Core - Touch Screen / AUX ADC / GPIO Driver");
MODULE_LICENSE("GPL");
