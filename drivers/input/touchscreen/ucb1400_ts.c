/*
 *  Philips UCB1400 touchscreen driver
 *
 *  Author:	Nicolas Pitre
 *  Created:	September 25, 2006
 *  Copyright:	MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This code is heavily based on ucb1x00-*.c copyrighted by Russell King
 * covering the UCB1100, UCB1200 and UCB1300..  Support for the UCB1400 has
 * been made separate from ucb1x00-core/ucb1x00-ts on Russell's request.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/slab.h>
#include <linux/kthread.h>

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/ac97_codec.h>


/*
 * Interesting UCB1400 AC-link registers
 */

#define UCB_IE_RIS		0x5e
#define UCB_IE_FAL		0x60
#define UCB_IE_STATUS		0x62
#define UCB_IE_CLEAR		0x62
#define UCB_IE_ADC		(1 << 11)
#define UCB_IE_TSPX		(1 << 12)

#define UCB_TS_CR		0x64
#define UCB_TS_CR_TSMX_POW	(1 << 0)
#define UCB_TS_CR_TSPX_POW	(1 << 1)
#define UCB_TS_CR_TSMY_POW	(1 << 2)
#define UCB_TS_CR_TSPY_POW	(1 << 3)
#define UCB_TS_CR_TSMX_GND	(1 << 4)
#define UCB_TS_CR_TSPX_GND	(1 << 5)
#define UCB_TS_CR_TSMY_GND	(1 << 6)
#define UCB_TS_CR_TSPY_GND	(1 << 7)
#define UCB_TS_CR_MODE_INT	(0 << 8)
#define UCB_TS_CR_MODE_PRES	(1 << 8)
#define UCB_TS_CR_MODE_POS	(2 << 8)
#define UCB_TS_CR_BIAS_ENA	(1 << 11)
#define UCB_TS_CR_TSPX_LOW	(1 << 12)
#define UCB_TS_CR_TSMX_LOW	(1 << 13)

#define UCB_ADC_CR		0x66
#define UCB_ADC_SYNC_ENA	(1 << 0)
#define UCB_ADC_VREFBYP_CON	(1 << 1)
#define UCB_ADC_INP_TSPX	(0 << 2)
#define UCB_ADC_INP_TSMX	(1 << 2)
#define UCB_ADC_INP_TSPY	(2 << 2)
#define UCB_ADC_INP_TSMY	(3 << 2)
#define UCB_ADC_INP_AD0		(4 << 2)
#define UCB_ADC_INP_AD1		(5 << 2)
#define UCB_ADC_INP_AD2		(6 << 2)
#define UCB_ADC_INP_AD3		(7 << 2)
#define UCB_ADC_EXT_REF		(1 << 5)
#define UCB_ADC_START		(1 << 7)
#define UCB_ADC_ENA		(1 << 15)

#define UCB_ADC_DATA		0x68
#define UCB_ADC_DAT_VALID	(1 << 15)
#define UCB_ADC_DAT_VALUE(x)	((x) & 0x3ff)

#define UCB_ID			0x7e
#define UCB_ID_1400             0x4304


struct ucb1400 {
	ac97_t			*ac97;
	struct input_dev	*ts_idev;

	int			irq;

	wait_queue_head_t	ts_wait;
	struct task_struct	*ts_task;

	unsigned int		irq_pending;	/* not bit field shared */
	unsigned int		ts_restart:1;
	unsigned int		adcsync:1;
};

static int adcsync;

static inline u16 ucb1400_reg_read(struct ucb1400 *ucb, u16 reg)
{
	return ucb->ac97->bus->ops->read(ucb->ac97, reg);
}

static inline void ucb1400_reg_write(struct ucb1400 *ucb, u16 reg, u16 val)
{
	ucb->ac97->bus->ops->write(ucb->ac97, reg, val);
}

static inline void ucb1400_adc_enable(struct ucb1400 *ucb)
{
	ucb1400_reg_write(ucb, UCB_ADC_CR, UCB_ADC_ENA);
}

static unsigned int ucb1400_adc_read(struct ucb1400 *ucb, u16 adc_channel)
{
	unsigned int val;

	if (ucb->adcsync)
		adc_channel |= UCB_ADC_SYNC_ENA;

	ucb1400_reg_write(ucb, UCB_ADC_CR, UCB_ADC_ENA | adc_channel);
	ucb1400_reg_write(ucb, UCB_ADC_CR, UCB_ADC_ENA | adc_channel | UCB_ADC_START);

	for (;;) {
		val = ucb1400_reg_read(ucb, UCB_ADC_DATA);
		if (val & UCB_ADC_DAT_VALID)
			break;
		/* yield to other processes */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
	}

	return UCB_ADC_DAT_VALUE(val);
}

static inline void ucb1400_adc_disable(struct ucb1400 *ucb)
{
	ucb1400_reg_write(ucb, UCB_ADC_CR, 0);
}

/* Switch to interrupt mode. */
static inline void ucb1400_ts_mode_int(struct ucb1400 *ucb)
{
	ucb1400_reg_write(ucb, UCB_TS_CR,
			UCB_TS_CR_TSMX_POW | UCB_TS_CR_TSPX_POW |
			UCB_TS_CR_TSMY_GND | UCB_TS_CR_TSPY_GND |
			UCB_TS_CR_MODE_INT);
}

/*
 * Switch to pressure mode, and read pressure.  We don't need to wait
 * here, since both plates are being driven.
 */
static inline unsigned int ucb1400_ts_read_pressure(struct ucb1400 *ucb)
{
	ucb1400_reg_write(ucb, UCB_TS_CR,
			UCB_TS_CR_TSMX_POW | UCB_TS_CR_TSPX_POW |
			UCB_TS_CR_TSMY_GND | UCB_TS_CR_TSPY_GND |
			UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);
	return ucb1400_adc_read(ucb, UCB_ADC_INP_TSPY);
}

/*
 * Switch to X position mode and measure Y plate.  We switch the plate
 * configuration in pressure mode, then switch to position mode.  This
 * gives a faster response time.  Even so, we need to wait about 55us
 * for things to stabilise.
 */
static inline unsigned int ucb1400_ts_read_xpos(struct ucb1400 *ucb)
{
	ucb1400_reg_write(ucb, UCB_TS_CR,
			UCB_TS_CR_TSMX_GND | UCB_TS_CR_TSPX_POW |
			UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);
	ucb1400_reg_write(ucb, UCB_TS_CR,
			UCB_TS_CR_TSMX_GND | UCB_TS_CR_TSPX_POW |
			UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);
	ucb1400_reg_write(ucb, UCB_TS_CR,
			UCB_TS_CR_TSMX_GND | UCB_TS_CR_TSPX_POW |
			UCB_TS_CR_MODE_POS | UCB_TS_CR_BIAS_ENA);

	udelay(55);

	return ucb1400_adc_read(ucb, UCB_ADC_INP_TSPY);
}

/*
 * Switch to Y position mode and measure X plate.  We switch the plate
 * configuration in pressure mode, then switch to position mode.  This
 * gives a faster response time.  Even so, we need to wait about 55us
 * for things to stabilise.
 */
static inline unsigned int ucb1400_ts_read_ypos(struct ucb1400 *ucb)
{
	ucb1400_reg_write(ucb, UCB_TS_CR,
			UCB_TS_CR_TSMY_GND | UCB_TS_CR_TSPY_POW |
			UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);
	ucb1400_reg_write(ucb, UCB_TS_CR,
			UCB_TS_CR_TSMY_GND | UCB_TS_CR_TSPY_POW |
			UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);
	ucb1400_reg_write(ucb, UCB_TS_CR,
			UCB_TS_CR_TSMY_GND | UCB_TS_CR_TSPY_POW |
			UCB_TS_CR_MODE_POS | UCB_TS_CR_BIAS_ENA);

	udelay(55);

	return ucb1400_adc_read(ucb, UCB_ADC_INP_TSPX);
}

/*
 * Switch to X plate resistance mode.  Set MX to ground, PX to
 * supply.  Measure current.
 */
static inline unsigned int ucb1400_ts_read_xres(struct ucb1400 *ucb)
{
	ucb1400_reg_write(ucb, UCB_TS_CR,
			UCB_TS_CR_TSMX_GND | UCB_TS_CR_TSPX_POW |
			UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);
	return ucb1400_adc_read(ucb, 0);
}

/*
 * Switch to Y plate resistance mode.  Set MY to ground, PY to
 * supply.  Measure current.
 */
static inline unsigned int ucb1400_ts_read_yres(struct ucb1400 *ucb)
{
	ucb1400_reg_write(ucb, UCB_TS_CR,
			UCB_TS_CR_TSMY_GND | UCB_TS_CR_TSPY_POW |
			UCB_TS_CR_MODE_PRES | UCB_TS_CR_BIAS_ENA);
	return ucb1400_adc_read(ucb, 0);
}

static inline int ucb1400_ts_pen_down(struct ucb1400 *ucb)
{
	unsigned short val = ucb1400_reg_read(ucb, UCB_TS_CR);
	return (val & (UCB_TS_CR_TSPX_LOW | UCB_TS_CR_TSMX_LOW));
}

static inline void ucb1400_ts_irq_enable(struct ucb1400 *ucb)
{
	ucb1400_reg_write(ucb, UCB_IE_CLEAR, UCB_IE_TSPX);
	ucb1400_reg_write(ucb, UCB_IE_CLEAR, 0);
	ucb1400_reg_write(ucb, UCB_IE_FAL, UCB_IE_TSPX);
}

static inline void ucb1400_ts_irq_disable(struct ucb1400 *ucb)
{
	ucb1400_reg_write(ucb, UCB_IE_FAL, 0);
}

static void ucb1400_ts_evt_add(struct input_dev *idev, u16 pressure, u16 x, u16 y)
{
	input_report_abs(idev, ABS_X, x);
	input_report_abs(idev, ABS_Y, y);
	input_report_abs(idev, ABS_PRESSURE, pressure);
	input_sync(idev);
}

static void ucb1400_ts_event_release(struct input_dev *idev)
{
	input_report_abs(idev, ABS_PRESSURE, 0);
	input_sync(idev);
}

static void ucb1400_handle_pending_irq(struct ucb1400 *ucb)
{
	unsigned int isr;

	isr = ucb1400_reg_read(ucb, UCB_IE_STATUS);
	ucb1400_reg_write(ucb, UCB_IE_CLEAR, isr);
	ucb1400_reg_write(ucb, UCB_IE_CLEAR, 0);

	if (isr & UCB_IE_TSPX)
		ucb1400_ts_irq_disable(ucb);
	else
		printk(KERN_ERR "ucb1400: unexpected IE_STATUS = %#x\n", isr);

	enable_irq(ucb->irq);
}

static int ucb1400_ts_thread(void *_ucb)
{
	struct ucb1400 *ucb = _ucb;
	struct task_struct *tsk = current;
	int valid = 0;

	tsk->policy = SCHED_FIFO;
	tsk->rt_priority = 1;

	while (!kthread_should_stop()) {
		unsigned int x, y, p;
		long timeout;

		ucb->ts_restart = 0;

		if (ucb->irq_pending) {
			ucb->irq_pending = 0;
			ucb1400_handle_pending_irq(ucb);
		}

		ucb1400_adc_enable(ucb);
		x = ucb1400_ts_read_xpos(ucb);
		y = ucb1400_ts_read_ypos(ucb);
		p = ucb1400_ts_read_pressure(ucb);
		ucb1400_adc_disable(ucb);

		/* Switch back to interrupt mode. */
		ucb1400_ts_mode_int(ucb);

		msleep(10);

		if (ucb1400_ts_pen_down(ucb)) {
			ucb1400_ts_irq_enable(ucb);

			/*
			 * If we spat out a valid sample set last time,
			 * spit out a "pen off" sample here.
			 */
			if (valid) {
				ucb1400_ts_event_release(ucb->ts_idev);
				valid = 0;
			}

			timeout = MAX_SCHEDULE_TIMEOUT;
		} else {
			valid = 1;
			ucb1400_ts_evt_add(ucb->ts_idev, p, x, y);
			timeout = msecs_to_jiffies(10);
		}

		wait_event_interruptible_timeout(ucb->ts_wait,
			ucb->irq_pending || ucb->ts_restart || kthread_should_stop(),
			timeout);
		try_to_freeze();
	}

	/* Send the "pen off" if we are stopping with the pen still active */
	if (valid)
		ucb1400_ts_event_release(ucb->ts_idev);

	ucb->ts_task = NULL;
	return 0;
}

/*
 * A restriction with interrupts exists when using the ucb1400, as
 * the codec read/write routines may sleep while waiting for codec
 * access completion and uses semaphores for access control to the
 * AC97 bus.  A complete codec read cycle could take  anywhere from
 * 60 to 100uSec so we *definitely* don't want to spin inside the
 * interrupt handler waiting for codec access.  So, we handle the
 * interrupt by scheduling a RT kernel thread to run in process
 * context instead of interrupt context.
 */
static irqreturn_t ucb1400_hard_irq(int irqnr, void *devid)
{
	struct ucb1400 *ucb = devid;

	if (irqnr == ucb->irq) {
		disable_irq(ucb->irq);
		ucb->irq_pending = 1;
		wake_up(&ucb->ts_wait);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int ucb1400_ts_open(struct input_dev *idev)
{
	struct ucb1400 *ucb = idev->private;
	int ret = 0;

	BUG_ON(ucb->ts_task);

	ucb->ts_task = kthread_run(ucb1400_ts_thread, ucb, "UCB1400_ts");
	if (IS_ERR(ucb->ts_task)) {
		ret = PTR_ERR(ucb->ts_task);
		ucb->ts_task = NULL;
	}

	return ret;
}

static void ucb1400_ts_close(struct input_dev *idev)
{
	struct ucb1400 *ucb = idev->private;

	if (ucb->ts_task)
		kthread_stop(ucb->ts_task);

	ucb1400_ts_irq_disable(ucb);
	ucb1400_reg_write(ucb, UCB_TS_CR, 0);
}

#ifdef CONFIG_PM
static int ucb1400_ts_resume(struct device *dev)
{
	struct ucb1400 *ucb = dev_get_drvdata(dev);

	if (ucb->ts_task) {
		/*
		 * Restart the TS thread to ensure the
		 * TS interrupt mode is set up again
		 * after sleep.
		 */
		ucb->ts_restart = 1;
		wake_up(&ucb->ts_wait);
	}
	return 0;
}
#else
#define ucb1400_ts_resume NULL
#endif

#ifndef NO_IRQ
#define NO_IRQ	0
#endif

/*
 * Try to probe our interrupt, rather than relying on lots of
 * hard-coded machine dependencies.
 */
static int ucb1400_detect_irq(struct ucb1400 *ucb)
{
	unsigned long mask, timeout;

	mask = probe_irq_on();
	if (!mask) {
		probe_irq_off(mask);
		return -EBUSY;
	}

	/* Enable the ADC interrupt. */
	ucb1400_reg_write(ucb, UCB_IE_RIS, UCB_IE_ADC);
	ucb1400_reg_write(ucb, UCB_IE_FAL, UCB_IE_ADC);
	ucb1400_reg_write(ucb, UCB_IE_CLEAR, 0xffff);
	ucb1400_reg_write(ucb, UCB_IE_CLEAR, 0);

	/* Cause an ADC interrupt. */
	ucb1400_reg_write(ucb, UCB_ADC_CR, UCB_ADC_ENA);
	ucb1400_reg_write(ucb, UCB_ADC_CR, UCB_ADC_ENA | UCB_ADC_START);

	/* Wait for the conversion to complete. */
	timeout = jiffies + HZ/2;
	while (!(ucb1400_reg_read(ucb, UCB_ADC_DATA) & UCB_ADC_DAT_VALID)) {
		cpu_relax();
		if (time_after(jiffies, timeout)) {
			printk(KERN_ERR "ucb1400: timed out in IRQ probe\n");
			probe_irq_off(mask);
			return -ENODEV;
		}
	}
	ucb1400_reg_write(ucb, UCB_ADC_CR, 0);

	/* Disable and clear interrupt. */
	ucb1400_reg_write(ucb, UCB_IE_RIS, 0);
	ucb1400_reg_write(ucb, UCB_IE_FAL, 0);
	ucb1400_reg_write(ucb, UCB_IE_CLEAR, 0xffff);
	ucb1400_reg_write(ucb, UCB_IE_CLEAR, 0);

	/* Read triggered interrupt. */
	ucb->irq = probe_irq_off(mask);
	if (ucb->irq < 0 || ucb->irq == NO_IRQ)
		return -ENODEV;

	return 0;
}

static int ucb1400_ts_probe(struct device *dev)
{
	struct ucb1400 *ucb;
	struct input_dev *idev;
	int error, id, x_res, y_res;

	ucb = kzalloc(sizeof(struct ucb1400), GFP_KERNEL);
	idev = input_allocate_device();
	if (!ucb || !idev) {
		error = -ENOMEM;
		goto err_free_devs;
	}

	ucb->ts_idev = idev;
	ucb->adcsync = adcsync;
	ucb->ac97 = to_ac97_t(dev);
	init_waitqueue_head(&ucb->ts_wait);

	id = ucb1400_reg_read(ucb, UCB_ID);
	if (id != UCB_ID_1400) {
		error = -ENODEV;
		goto err_free_devs;
	}

	error = ucb1400_detect_irq(ucb);
	if (error) {
		printk(KERN_ERR "UCB1400: IRQ probe failed\n");
		goto err_free_devs;
	}

	error = request_irq(ucb->irq, ucb1400_hard_irq, IRQF_TRIGGER_RISING,
				"UCB1400", ucb);
	if (error) {
		printk(KERN_ERR "ucb1400: unable to grab irq%d: %d\n",
				ucb->irq, error);
		goto err_free_devs;
	}
	printk(KERN_DEBUG "UCB1400: found IRQ %d\n", ucb->irq);

	idev->private		= ucb;
	idev->cdev.dev		= dev;
	idev->name		= "UCB1400 touchscreen interface";
	idev->id.vendor		= ucb1400_reg_read(ucb, AC97_VENDOR_ID1);
	idev->id.product	= id;
	idev->open		= ucb1400_ts_open;
	idev->close		= ucb1400_ts_close;
	idev->evbit[0]		= BIT(EV_ABS);

	ucb1400_adc_enable(ucb);
	x_res = ucb1400_ts_read_xres(ucb);
	y_res = ucb1400_ts_read_yres(ucb);
	ucb1400_adc_disable(ucb);
	printk(KERN_DEBUG "UCB1400: x/y = %d/%d\n", x_res, y_res);

	input_set_abs_params(idev, ABS_X, 0, x_res, 0, 0);
	input_set_abs_params(idev, ABS_Y, 0, y_res, 0, 0);
	input_set_abs_params(idev, ABS_PRESSURE, 0, 0, 0, 0);

	error = input_register_device(idev);
	if (error)
		goto err_free_irq;

	dev_set_drvdata(dev, ucb);
	return 0;

 err_free_irq:
	free_irq(ucb->irq, ucb);
 err_free_devs:
	input_free_device(idev);
	kfree(ucb);
	return error;
}

static int ucb1400_ts_remove(struct device *dev)
{
	struct ucb1400 *ucb = dev_get_drvdata(dev);

	free_irq(ucb->irq, ucb);
	input_unregister_device(ucb->ts_idev);
	dev_set_drvdata(dev, NULL);
	kfree(ucb);
	return 0;
}

static struct device_driver ucb1400_ts_driver = {
	.owner		= THIS_MODULE,
	.bus		= &ac97_bus_type,
	.probe		= ucb1400_ts_probe,
	.remove		= ucb1400_ts_remove,
	.resume		= ucb1400_ts_resume,
};

static int __init ucb1400_ts_init(void)
{
	return driver_register(&ucb1400_ts_driver);
}

static void __exit ucb1400_ts_exit(void)
{
	driver_unregister(&ucb1400_ts_driver);
}

module_param(adcsync, int, 0444);

module_init(ucb1400_ts_init);
module_exit(ucb1400_ts_exit);

MODULE_DESCRIPTION("Philips UCB1400 touchscreen driver");
MODULE_LICENSE("GPL");
