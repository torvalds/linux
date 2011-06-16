/*
 *  Touchscreen driver for UCB1x00-based touchscreens
 *
 *  Copyright (C) 2001 Russell King, All Rights Reserved.
 *  Copyright (C) 2005 Pavel Machek
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 21-Jan-2002 <jco@ict.es> :
 *
 * Added support for synchronous A/D mode. This mode is useful to
 * avoid noise induced in the touchpanel by the LCD, provided that
 * the UCB1x00 has a valid LCD sync signal routed to its ADCSYNC pin.
 * It is important to note that the signal connected to the ADCSYNC
 * pin should provide pulses even when the LCD is blanked, otherwise
 * a pen touch needed to unblank the LCD will never be read.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/input.h>
#include <linux/device.h>
#include <linux/freezer.h>
#include <linux/slab.h>
#include <linux/kthread.h>

#include <mach/dma.h>
#include <mach/collie.h>
#include <asm/mach-types.h>

#include "ucb1x00.h"


struct ucb1x00_ts {
	struct input_dev	*idev;
	struct ucb1x00		*ucb;

	wait_queue_head_t	irq_wait;
	struct task_struct	*rtask;
	u16			x_res;
	u16			y_res;

	unsigned int		restart:1;
	unsigned int		adcsync:1;
};

static int adcsync;

static inline void ucb1x00_ts_evt_add(struct ucb1x00_ts *ts, u16 pressure, u16 x, u16 y)
{
	struct input_dev *idev = ts->idev;

	input_report_abs(idev, ABS_X, x);
	input_report_abs(idev, ABS_Y, y);
	input_report_abs(idev, ABS_PRESSURE, pressure);
	input_sync(idev);
}

static inline void ucb1x00_ts_event_release(struct ucb1x00_ts *ts)
{
	struct input_dev *idev = ts->idev;

	input_report_abs(idev, ABS_PRESSURE, 0);
	input_sync(idev);
}

/*
 * Switch to interrupt mode.
 */
static inline void ucb1x00_ts_mode_int(struct ucb1x00_ts *ts)
{
	ucb1x00_reg_write(ts->ucb, UCB_TS_CR,
			UCB_TS_CR_TSMX_POW | UCB_TS_CR_TSPX_POW |
			UCB_TS_CR_TSMY_GND | UCB_TS_CR_TSPY_GND |
			UCB_TS_CR_MODE_INT);
}

/*
 * Switch to pressure mode, and read pressure.  We don't need to wait
 * here, since both plates are being driven.
 */
static inline unsigned int ucb1x00_ts_read_pressure(struct ucb1x00_ts *ts)
{
	if (machine_is_collie()) {
		ucb1x00_io_write(ts->ucb, COLLIE_TC35143_GPIO_TBL_CHK, 0);
		ucb1x00_reg_write(ts->ucb, UCB_TS_CR,
				  UCB_TS_CR_TSPX_POW | UCB_TS_CR_TSMX_POW |
				  UCB_TS_CR_MODE_POS | UCB_TS_CR_BIAS_ENA);

		udelay(55);

		return ucb1x00_adc_read(ts->ucb, UCB_ADC_INP_AD2, ts->adcsync);
	} else {
		ucb1x00_reg_write(ts->ucb, UCB_TS_CR,
				  UCB_TS_CR_TSMX_POW | UCB_TS_CR_TSPX_POW |
				  UCB_TS_CR_TSMY_GND | UCB_TS_CR_TSPY_GND |
				  UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);

		return ucb1x00_adc_read(ts->ucb, UCB_ADC_INP_TSPY, ts->adcsync);
	}
}

/*
 * Switch to X position mode and measure Y plate.  We switch the plate
 * configuration in pressure mode, then switch to position mode.  This
 * gives a faster response time.  Even so, we need to wait about 55us
 * for things to stabilise.
 */
static inline unsigned int ucb1x00_ts_read_xpos(struct ucb1x00_ts *ts)
{
	if (machine_is_collie())
		ucb1x00_io_write(ts->ucb, 0, COLLIE_TC35143_GPIO_TBL_CHK);
	else {
		ucb1x00_reg_write(ts->ucb, UCB_TS_CR,
				  UCB_TS_CR_TSMX_GND | UCB_TS_CR_TSPX_POW |
				  UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);
		ucb1x00_reg_write(ts->ucb, UCB_TS_CR,
				  UCB_TS_CR_TSMX_GND | UCB_TS_CR_TSPX_POW |
				  UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);
	}
	ucb1x00_reg_write(ts->ucb, UCB_TS_CR,
			UCB_TS_CR_TSMX_GND | UCB_TS_CR_TSPX_POW |
			UCB_TS_CR_MODE_POS | UCB_TS_CR_BIAS_ENA);

	udelay(55);

	return ucb1x00_adc_read(ts->ucb, UCB_ADC_INP_TSPY, ts->adcsync);
}

/*
 * Switch to Y position mode and measure X plate.  We switch the plate
 * configuration in pressure mode, then switch to position mode.  This
 * gives a faster response time.  Even so, we need to wait about 55us
 * for things to stabilise.
 */
static inline unsigned int ucb1x00_ts_read_ypos(struct ucb1x00_ts *ts)
{
	if (machine_is_collie())
		ucb1x00_io_write(ts->ucb, 0, COLLIE_TC35143_GPIO_TBL_CHK);
	else {
		ucb1x00_reg_write(ts->ucb, UCB_TS_CR,
				  UCB_TS_CR_TSMY_GND | UCB_TS_CR_TSPY_POW |
				  UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);
		ucb1x00_reg_write(ts->ucb, UCB_TS_CR,
				  UCB_TS_CR_TSMY_GND | UCB_TS_CR_TSPY_POW |
				  UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);
	}

	ucb1x00_reg_write(ts->ucb, UCB_TS_CR,
			UCB_TS_CR_TSMY_GND | UCB_TS_CR_TSPY_POW |
			UCB_TS_CR_MODE_POS | UCB_TS_CR_BIAS_ENA);

	udelay(55);

	return ucb1x00_adc_read(ts->ucb, UCB_ADC_INP_TSPX, ts->adcsync);
}

/*
 * Switch to X plate resistance mode.  Set MX to ground, PX to
 * supply.  Measure current.
 */
static inline unsigned int ucb1x00_ts_read_xres(struct ucb1x00_ts *ts)
{
	ucb1x00_reg_write(ts->ucb, UCB_TS_CR,
			UCB_TS_CR_TSMX_GND | UCB_TS_CR_TSPX_POW |
			UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);
	return ucb1x00_adc_read(ts->ucb, 0, ts->adcsync);
}

/*
 * Switch to Y plate resistance mode.  Set MY to ground, PY to
 * supply.  Measure current.
 */
static inline unsigned int ucb1x00_ts_read_yres(struct ucb1x00_ts *ts)
{
	ucb1x00_reg_write(ts->ucb, UCB_TS_CR,
			UCB_TS_CR_TSMY_GND | UCB_TS_CR_TSPY_POW |
			UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);
	return ucb1x00_adc_read(ts->ucb, 0, ts->adcsync);
}

static inline int ucb1x00_ts_pen_down(struct ucb1x00_ts *ts)
{
	unsigned int val = ucb1x00_reg_read(ts->ucb, UCB_TS_CR);

	if (machine_is_collie())
		return (!(val & (UCB_TS_CR_TSPX_LOW)));
	else
		return (val & (UCB_TS_CR_TSPX_LOW | UCB_TS_CR_TSMX_LOW));
}

/*
 * This is a RT kernel thread that handles the ADC accesses
 * (mainly so we can use semaphores in the UCB1200 core code
 * to serialise accesses to the ADC).
 */
static int ucb1x00_thread(void *_ts)
{
	struct ucb1x00_ts *ts = _ts;
	DECLARE_WAITQUEUE(wait, current);
	int valid = 0;

	set_freezable();
	add_wait_queue(&ts->irq_wait, &wait);
	while (!kthread_should_stop()) {
		unsigned int x, y, p;
		signed long timeout;

		ts->restart = 0;

		ucb1x00_adc_enable(ts->ucb);

		x = ucb1x00_ts_read_xpos(ts);
		y = ucb1x00_ts_read_ypos(ts);
		p = ucb1x00_ts_read_pressure(ts);

		/*
		 * Switch back to interrupt mode.
		 */
		ucb1x00_ts_mode_int(ts);
		ucb1x00_adc_disable(ts->ucb);

		msleep(10);

		ucb1x00_enable(ts->ucb);


		if (ucb1x00_ts_pen_down(ts)) {
			set_current_state(TASK_INTERRUPTIBLE);

			ucb1x00_enable_irq(ts->ucb, UCB_IRQ_TSPX, machine_is_collie() ? UCB_RISING : UCB_FALLING);
			ucb1x00_disable(ts->ucb);

			/*
			 * If we spat out a valid sample set last time,
			 * spit out a "pen off" sample here.
			 */
			if (valid) {
				ucb1x00_ts_event_release(ts);
				valid = 0;
			}

			timeout = MAX_SCHEDULE_TIMEOUT;
		} else {
			ucb1x00_disable(ts->ucb);

			/*
			 * Filtering is policy.  Policy belongs in user
			 * space.  We therefore leave it to user space
			 * to do any filtering they please.
			 */
			if (!ts->restart) {
				ucb1x00_ts_evt_add(ts, p, x, y);
				valid = 1;
			}

			set_current_state(TASK_INTERRUPTIBLE);
			timeout = HZ / 100;
		}

		try_to_freeze();

		schedule_timeout(timeout);
	}

	remove_wait_queue(&ts->irq_wait, &wait);

	ts->rtask = NULL;
	return 0;
}

/*
 * We only detect touch screen _touches_ with this interrupt
 * handler, and even then we just schedule our task.
 */
static void ucb1x00_ts_irq(int idx, void *id)
{
	struct ucb1x00_ts *ts = id;

	ucb1x00_disable_irq(ts->ucb, UCB_IRQ_TSPX, UCB_FALLING);
	wake_up(&ts->irq_wait);
}

static int ucb1x00_ts_open(struct input_dev *idev)
{
	struct ucb1x00_ts *ts = input_get_drvdata(idev);
	int ret = 0;

	BUG_ON(ts->rtask);

	init_waitqueue_head(&ts->irq_wait);
	ret = ucb1x00_hook_irq(ts->ucb, UCB_IRQ_TSPX, ucb1x00_ts_irq, ts);
	if (ret < 0)
		goto out;

	/*
	 * If we do this at all, we should allow the user to
	 * measure and read the X and Y resistance at any time.
	 */
	ucb1x00_adc_enable(ts->ucb);
	ts->x_res = ucb1x00_ts_read_xres(ts);
	ts->y_res = ucb1x00_ts_read_yres(ts);
	ucb1x00_adc_disable(ts->ucb);

	ts->rtask = kthread_run(ucb1x00_thread, ts, "ktsd");
	if (!IS_ERR(ts->rtask)) {
		ret = 0;
	} else {
		ucb1x00_free_irq(ts->ucb, UCB_IRQ_TSPX, ts);
		ts->rtask = NULL;
		ret = -EFAULT;
	}

 out:
	return ret;
}

/*
 * Release touchscreen resources.  Disable IRQs.
 */
static void ucb1x00_ts_close(struct input_dev *idev)
{
	struct ucb1x00_ts *ts = input_get_drvdata(idev);

	if (ts->rtask)
		kthread_stop(ts->rtask);

	ucb1x00_enable(ts->ucb);
	ucb1x00_free_irq(ts->ucb, UCB_IRQ_TSPX, ts);
	ucb1x00_reg_write(ts->ucb, UCB_TS_CR, 0);
	ucb1x00_disable(ts->ucb);
}

#ifdef CONFIG_PM
static int ucb1x00_ts_resume(struct ucb1x00_dev *dev)
{
	struct ucb1x00_ts *ts = dev->priv;

	if (ts->rtask != NULL) {
		/*
		 * Restart the TS thread to ensure the
		 * TS interrupt mode is set up again
		 * after sleep.
		 */
		ts->restart = 1;
		wake_up(&ts->irq_wait);
	}
	return 0;
}
#else
#define ucb1x00_ts_resume NULL
#endif


/*
 * Initialisation.
 */
static int ucb1x00_ts_add(struct ucb1x00_dev *dev)
{
	struct ucb1x00_ts *ts;
	struct input_dev *idev;
	int err;

	ts = kzalloc(sizeof(struct ucb1x00_ts), GFP_KERNEL);
	idev = input_allocate_device();
	if (!ts || !idev) {
		err = -ENOMEM;
		goto fail;
	}

	ts->ucb = dev->ucb;
	ts->idev = idev;
	ts->adcsync = adcsync ? UCB_SYNC : UCB_NOSYNC;

	idev->name       = "Touchscreen panel";
	idev->id.product = ts->ucb->id;
	idev->open       = ucb1x00_ts_open;
	idev->close      = ucb1x00_ts_close;

	__set_bit(EV_ABS, idev->evbit);

	input_set_drvdata(idev, ts);

	ucb1x00_adc_enable(ts->ucb);
	ts->x_res = ucb1x00_ts_read_xres(ts);
	ts->y_res = ucb1x00_ts_read_yres(ts);
	ucb1x00_adc_disable(ts->ucb);

	input_set_abs_params(idev, ABS_X, 0, ts->x_res, 0, 0);
	input_set_abs_params(idev, ABS_Y, 0, ts->y_res, 0, 0);
	input_set_abs_params(idev, ABS_PRESSURE, 0, 0, 0, 0);

	err = input_register_device(idev);
	if (err)
		goto fail;

	dev->priv = ts;

	return 0;

 fail:
	input_free_device(idev);
	kfree(ts);
	return err;
}

static void ucb1x00_ts_remove(struct ucb1x00_dev *dev)
{
	struct ucb1x00_ts *ts = dev->priv;

	input_unregister_device(ts->idev);
	kfree(ts);
}

static struct ucb1x00_driver ucb1x00_ts_driver = {
	.add		= ucb1x00_ts_add,
	.remove		= ucb1x00_ts_remove,
	.resume		= ucb1x00_ts_resume,
};

static int __init ucb1x00_ts_init(void)
{
	return ucb1x00_register_driver(&ucb1x00_ts_driver);
}

static void __exit ucb1x00_ts_exit(void)
{
	ucb1x00_unregister_driver(&ucb1x00_ts_driver);
}

module_param(adcsync, int, 0444);
module_init(ucb1x00_ts_init);
module_exit(ucb1x00_ts_exit);

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION("UCB1x00 touchscreen driver");
MODULE_LICENSE("GPL");
