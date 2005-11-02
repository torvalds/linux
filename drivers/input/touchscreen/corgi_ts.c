/*
 *  Touchscreen driver for Sharp SL-C7xx and SL-Cxx00 models
 *
 *  Copyright (c) 2004-2005 Richard Purdie
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */


#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/irq.h>

#include <asm/arch/sharpsl.h>
#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>


#define PWR_MODE_ACTIVE		0
#define PWR_MODE_SUSPEND	1

#define X_AXIS_MAX		3830
#define X_AXIS_MIN		150
#define Y_AXIS_MAX		3830
#define Y_AXIS_MIN		190
#define PRESSURE_MIN		0
#define PRESSURE_MAX		15000

struct ts_event {
	short pressure;
	short x;
	short y;
};

struct corgi_ts {
	struct input_dev *input;
	struct timer_list timer;
	struct ts_event tc;
	int pendown;
	int power_mode;
	int irq_gpio;
	struct corgits_machinfo *machinfo;
};

#ifdef CONFIG_PXA25x
#define CCNT(a)		asm volatile ("mrc p14, 0, %0, C1, C0, 0" : "=r"(a))
#define PMNC_GET(x)	asm volatile ("mrc p14, 0, %0, C0, C0, 0" : "=r"(x))
#define PMNC_SET(x)	asm volatile ("mcr p14, 0, %0, C0, C0, 0" : : "r"(x))
#endif
#ifdef CONFIG_PXA27x
#define CCNT(a)		asm volatile ("mrc p14, 0, %0, C1, C1, 0" : "=r"(a))
#define PMNC_GET(x)	asm volatile ("mrc p14, 0, %0, C0, C1, 0" : "=r"(x))
#define PMNC_SET(x)	asm volatile ("mcr p14, 0, %0, C0, C1, 0" : : "r"(x))
#endif

/* ADS7846 Touch Screen Controller bit definitions */
#define ADSCTRL_PD0		(1u << 0)	/* PD0 */
#define ADSCTRL_PD1		(1u << 1)	/* PD1 */
#define ADSCTRL_DFR		(1u << 2)	/* SER/DFR */
#define ADSCTRL_MOD		(1u << 3)	/* Mode */
#define ADSCTRL_ADR_SH	4	/* Address setting */
#define ADSCTRL_STS		(1u << 7)	/* Start Bit */

/* External Functions */
extern unsigned int get_clk_frequency_khz(int info);

static unsigned long calc_waittime(struct corgi_ts *corgi_ts)
{
	unsigned long hsync_len = corgi_ts->machinfo->get_hsync_len();

	if (hsync_len)
		return get_clk_frequency_khz(0)*1000/hsync_len;
	else
		return 0;
}

static int sync_receive_data_send_cmd(struct corgi_ts *corgi_ts, int doRecive, int doSend,
		unsigned int address, unsigned long wait_time)
{
	unsigned long timer1 = 0, timer2, pmnc = 0;
	int pos = 0;

	if (wait_time && doSend) {
		PMNC_GET(pmnc);
		if (!(pmnc & 0x01))
			PMNC_SET(0x01);

		/* polling HSync */
		corgi_ts->machinfo->wait_hsync();
		/* get CCNT */
		CCNT(timer1);
	}

	if (doRecive)
		pos = corgi_ssp_ads7846_get();

	if (doSend) {
		int cmd = ADSCTRL_PD0 | ADSCTRL_PD1 | (address << ADSCTRL_ADR_SH) | ADSCTRL_STS;
		/* dummy command */
		corgi_ssp_ads7846_put(cmd);
		corgi_ssp_ads7846_get();

		if (wait_time) {
			/* Wait after HSync */
			CCNT(timer2);
			if (timer2-timer1 > wait_time) {
				/* too slow - timeout, try again */
				corgi_ts->machinfo->wait_hsync();
				/* get OSCR */
				CCNT(timer1);
				/* Wait after HSync */
				CCNT(timer2);
			}
			while (timer2 - timer1 < wait_time)
				CCNT(timer2);
		}
		corgi_ssp_ads7846_put(cmd);
		if (wait_time && !(pmnc & 0x01))
			PMNC_SET(pmnc);
	}
	return pos;
}

static int read_xydata(struct corgi_ts *corgi_ts)
{
	unsigned int x, y, z1, z2;
	unsigned long flags, wait_time;

	/* critical section */
	local_irq_save(flags);
	corgi_ssp_ads7846_lock();
	wait_time = calc_waittime(corgi_ts);

	/* Y-axis */
	sync_receive_data_send_cmd(corgi_ts, 0, 1, 1u, wait_time);

	/* Y-axis */
	sync_receive_data_send_cmd(corgi_ts, 1, 1, 1u, wait_time);

	/* X-axis */
	y = sync_receive_data_send_cmd(corgi_ts, 1, 1, 5u, wait_time);

	/* Z1 */
	x = sync_receive_data_send_cmd(corgi_ts, 1, 1, 3u, wait_time);

	/* Z2 */
	z1 = sync_receive_data_send_cmd(corgi_ts, 1, 1, 4u, wait_time);
	z2 = sync_receive_data_send_cmd(corgi_ts, 1, 0, 4u, wait_time);

	/* Power-Down Enable */
	corgi_ssp_ads7846_put((1u << ADSCTRL_ADR_SH) | ADSCTRL_STS);
	corgi_ssp_ads7846_get();

	corgi_ssp_ads7846_unlock();
	local_irq_restore(flags);

	if (x== 0 || y == 0 || z1 == 0 || (x * (z2 - z1) / z1) >= 15000) {
		corgi_ts->tc.pressure = 0;
		return 0;
	}

	corgi_ts->tc.x = x;
	corgi_ts->tc.y = y;
	corgi_ts->tc.pressure = (x * (z2 - z1)) / z1;
	return 1;
}

static void new_data(struct corgi_ts *corgi_ts, struct pt_regs *regs)
{
	if (corgi_ts->power_mode != PWR_MODE_ACTIVE)
		return;

	if (!corgi_ts->tc.pressure && corgi_ts->pendown == 0)
		return;

	input_regs(corgi_ts->input, regs);
	input_report_abs(corgi_ts->input, ABS_X, corgi_ts->tc.x);
	input_report_abs(corgi_ts->input, ABS_Y, corgi_ts->tc.y);
	input_report_abs(corgi_ts->input, ABS_PRESSURE, corgi_ts->tc.pressure);
	input_report_key(corgi_ts->input, BTN_TOUCH, (corgi_ts->pendown != 0));
	input_sync(corgi_ts->input);
}

static void ts_interrupt_main(struct corgi_ts *corgi_ts, int isTimer, struct pt_regs *regs)
{
	if ((GPLR(IRQ_TO_GPIO(corgi_ts->irq_gpio)) & GPIO_bit(IRQ_TO_GPIO(corgi_ts->irq_gpio))) == 0) {
		/* Disable Interrupt */
		set_irq_type(corgi_ts->irq_gpio, IRQT_NOEDGE);
		if (read_xydata(corgi_ts)) {
			corgi_ts->pendown = 1;
			new_data(corgi_ts, regs);
		}
		mod_timer(&corgi_ts->timer, jiffies + HZ / 100);
	} else {
		if (corgi_ts->pendown == 1 || corgi_ts->pendown == 2) {
			mod_timer(&corgi_ts->timer, jiffies + HZ / 100);
			corgi_ts->pendown++;
			return;
		}

		if (corgi_ts->pendown) {
			corgi_ts->tc.pressure = 0;
			new_data(corgi_ts, regs);
		}

		/* Enable Falling Edge */
		set_irq_type(corgi_ts->irq_gpio, IRQT_FALLING);
		corgi_ts->pendown = 0;
	}
}

static void corgi_ts_timer(unsigned long data)
{
	struct corgi_ts *corgits_data = (struct corgi_ts *) data;
	ts_interrupt_main(corgits_data, 1, NULL);
}

static irqreturn_t ts_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct corgi_ts *corgits_data = dev_id;
	ts_interrupt_main(corgits_data, 0, regs);
	return IRQ_HANDLED;
}

#ifdef CONFIG_PM
static int corgits_suspend(struct device *dev, pm_message_t state)
{
	struct corgi_ts *corgi_ts = dev_get_drvdata(dev);

	if (corgi_ts->pendown) {
		del_timer_sync(&corgi_ts->timer);
		corgi_ts->tc.pressure = 0;
		new_data(corgi_ts, NULL);
		corgi_ts->pendown = 0;
	}
	corgi_ts->power_mode = PWR_MODE_SUSPEND;

	corgi_ssp_ads7846_putget((1u << ADSCTRL_ADR_SH) | ADSCTRL_STS);

	return 0;
}

static int corgits_resume(struct device *dev)
{
	struct corgi_ts *corgi_ts = dev_get_drvdata(dev);

	corgi_ssp_ads7846_putget((4u << ADSCTRL_ADR_SH) | ADSCTRL_STS);
	/* Enable Falling Edge */
	set_irq_type(corgi_ts->irq_gpio, IRQT_FALLING);
	corgi_ts->power_mode = PWR_MODE_ACTIVE;

	return 0;
}
#else
#define corgits_suspend		NULL
#define corgits_resume		NULL
#endif

static int __init corgits_probe(struct device *dev)
{
	struct corgi_ts *corgi_ts;
	struct platform_device *pdev = to_platform_device(dev);
	struct input_dev *input_dev;
	int err = -ENOMEM;

	corgi_ts = kzalloc(sizeof(struct corgi_ts), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!corgi_ts || !input_dev)
		goto fail;

	dev_set_drvdata(dev, corgi_ts);

	corgi_ts->machinfo = dev->platform_data;
	corgi_ts->irq_gpio = platform_get_irq(pdev, 0);

	if (corgi_ts->irq_gpio < 0) {
		err = -ENODEV;
		goto fail;
	}

	corgi_ts->input = input_dev;

	init_timer(&corgi_ts->timer);
	corgi_ts->timer.data = (unsigned long) corgi_ts;
	corgi_ts->timer.function = corgi_ts_timer;

	input_dev->name = "Corgi Touchscreen";
	input_dev->phys = "corgits/input0";
	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0002;
	input_dev->id.version = 0x0100;
	input_dev->cdev.dev = dev;
	input_dev->private = corgi_ts;

	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	input_dev->keybit[LONG(BTN_TOUCH)] = BIT(BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X, X_AXIS_MIN, X_AXIS_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, Y_AXIS_MIN, Y_AXIS_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, PRESSURE_MIN, PRESSURE_MAX, 0, 0);

	pxa_gpio_mode(IRQ_TO_GPIO(corgi_ts->irq_gpio) | GPIO_IN);

	/* Initiaize ADS7846 Difference Reference mode */
	corgi_ssp_ads7846_putget((1u << ADSCTRL_ADR_SH) | ADSCTRL_STS);
	mdelay(5);
	corgi_ssp_ads7846_putget((3u << ADSCTRL_ADR_SH) | ADSCTRL_STS);
	mdelay(5);
	corgi_ssp_ads7846_putget((4u << ADSCTRL_ADR_SH) | ADSCTRL_STS);
	mdelay(5);
	corgi_ssp_ads7846_putget((5u << ADSCTRL_ADR_SH) | ADSCTRL_STS);
	mdelay(5);

	if (request_irq(corgi_ts->irq_gpio, ts_interrupt, SA_INTERRUPT, "ts", corgi_ts)) {
		err = -EBUSY;
		goto fail;
	}

	input_register_device(corgi_ts->input);

	corgi_ts->power_mode = PWR_MODE_ACTIVE;

	/* Enable Falling Edge */
	set_irq_type(corgi_ts->irq_gpio, IRQT_FALLING);

	return 0;

 fail:	input_free_device(input_dev);
	kfree(corgi_ts);
	return err;

}

static int corgits_remove(struct device *dev)
{
	struct corgi_ts *corgi_ts = dev_get_drvdata(dev);

	free_irq(corgi_ts->irq_gpio, NULL);
	del_timer_sync(&corgi_ts->timer);
	corgi_ts->machinfo->put_hsync();
	input_unregister_device(corgi_ts->input);
	kfree(corgi_ts);
	return 0;
}

static struct device_driver corgits_driver = {
	.name		= "corgi-ts",
	.bus		= &platform_bus_type,
	.probe		= corgits_probe,
	.remove		= corgits_remove,
	.suspend	= corgits_suspend,
	.resume		= corgits_resume,
};

static int __devinit corgits_init(void)
{
	return driver_register(&corgits_driver);
}

static void __exit corgits_exit(void)
{
	driver_unregister(&corgits_driver);
}

module_init(corgits_init);
module_exit(corgits_exit);

MODULE_AUTHOR("Richard Purdie <rpurdie@rpsys.net>");
MODULE_DESCRIPTION("Corgi TouchScreen Driver");
MODULE_LICENSE("GPL");
