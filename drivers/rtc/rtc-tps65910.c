/*
 *	Real Time Clock driver for Wolfson Microelectronics tps65910
 *
 *	Copyright (C) 2009 Wolfson Microelectronics PLC.
 *
 *  Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/bcd.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/completion.h>
#include <linux/mfd/tps65910.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>


/* RTC Definitions */
/* RTC_CTRL_REG bitfields */
#define BIT_RTC_CTRL_REG_STOP_RTC_M		0x01
#define BIT_RTC_CTRL_REG_ROUND_30S_M		0x02
#define BIT_RTC_CTRL_REG_AUTO_COMP_M		0x04
#define BIT_RTC_CTRL_REG_MODE_12_24_M		0x08
#define BIT_RTC_CTRL_REG_TEST_MODE_M		0x10
#define BIT_RTC_CTRL_REG_SET_32_COUNTER_M	0x20
#define BIT_RTC_CTRL_REG_GET_TIME_M		0x40
#define BIT_RTC_CTRL_REG_RTC_V_OPT_M		0x80

/* RTC_STATUS_REG bitfields */
#define BIT_RTC_STATUS_REG_RUN_M		0x02
#define BIT_RTC_STATUS_REG_1S_EVENT_M		0x04
#define BIT_RTC_STATUS_REG_1M_EVENT_M		0x08
#define BIT_RTC_STATUS_REG_1H_EVENT_M		0x10
#define BIT_RTC_STATUS_REG_1D_EVENT_M		0x20
#define BIT_RTC_STATUS_REG_ALARM_M		0x40
#define BIT_RTC_STATUS_REG_POWER_UP_M		0x80

/* RTC_INTERRUPTS_REG bitfields */
#define BIT_RTC_INTERRUPTS_REG_EVERY_M		0x03
#define BIT_RTC_INTERRUPTS_REG_IT_TIMER_M	0x04
#define BIT_RTC_INTERRUPTS_REG_IT_ALARM_M	0x08

/* DEVCTRL bitfields */
#define BIT_RTC_PWDN				0x40

/* REG_SECONDS_REG through REG_YEARS_REG is how many registers? */
#define ALL_TIME_REGS				7
#define ALL_ALM_REGS				6


#define RTC_SET_TIME_RETRIES	5
#define RTC_GET_TIME_RETRIES	5


struct tps65910_rtc {
	struct tps65910 *tps65910;
	struct rtc_device *rtc;
	unsigned int alarm_enabled:1;
};

/*
 * Read current time and date in RTC
 */
static int tps65910_rtc_readtime(struct device *dev, struct rtc_time *tm)
{
	struct tps65910_rtc *tps65910_rtc = dev_get_drvdata(dev);
	struct tps65910 *tps65910 = tps65910_rtc->tps65910;
	int ret;
	int count = 0;
	unsigned char rtc_data[ALL_TIME_REGS + 1];
	u8 rtc_ctl;

	/*Dummy read*/	
	ret = tps65910_reg_read(tps65910, TPS65910_RTC_CTRL);
	
	/* Has the RTC been programmed? */
	ret = tps65910_reg_read(tps65910, TPS65910_RTC_CTRL);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}

	rtc_ctl = ret & (~BIT_RTC_CTRL_REG_RTC_V_OPT_M);

	ret = tps65910_reg_write(tps65910, TPS65910_RTC_CTRL, rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to write RTC control: %d\n", ret);
		return ret;
	}

	
	/* Read twice to make sure we don't read a corrupt, partially
	 * incremented, value.
	 */
	do {
		ret = tps65910_bulk_read(tps65910, TPS65910_SECONDS,
				       ALL_TIME_REGS, rtc_data);
		if (ret != 0)
			continue;

		tm->tm_sec = bcd2bin(rtc_data[0]);
		tm->tm_min = bcd2bin(rtc_data[1]);
		tm->tm_hour = bcd2bin(rtc_data[2]);
		tm->tm_mday = bcd2bin(rtc_data[3]);
		tm->tm_mon = bcd2bin(rtc_data[4]) - 1;
		tm->tm_year = bcd2bin(rtc_data[5]) + 100;	
		tm->tm_wday = bcd2bin(rtc_data[6]);

		dev_dbg(dev, "RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
			1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday, tm->tm_wday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);
		
		return ret;

	} while (++count < RTC_GET_TIME_RETRIES);
	dev_err(dev, "Timed out reading current time\n");

	return -EIO;

}

/*
 * Set current time and date in RTC
 */
static int tps65910_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct tps65910_rtc *tps65910_rtc = dev_get_drvdata(dev);
	struct tps65910 *tps65910 = tps65910_rtc->tps65910;
	int ret;
	u8 rtc_ctl;	
	unsigned char rtc_data[ALL_TIME_REGS + 1];
	
	rtc_data[0] = bin2bcd(tm->tm_sec);
	rtc_data[1] = bin2bcd(tm->tm_min);
	rtc_data[2] = bin2bcd(tm->tm_hour);
	rtc_data[3] = bin2bcd(tm->tm_mday);
	rtc_data[4] = bin2bcd(tm->tm_mon + 1);
	rtc_data[5] = bin2bcd(tm->tm_year - 100);
	rtc_data[6] = bin2bcd(tm->tm_wday);

	/*Dummy read*/	
	ret = tps65910_reg_read(tps65910, TPS65910_RTC_CTRL);
	
	/* Stop RTC while updating the TC registers */
	ret = tps65910_reg_read(tps65910, TPS65910_RTC_CTRL);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}
	
	rtc_ctl = ret & (~BIT_RTC_CTRL_REG_STOP_RTC_M);

	ret = tps65910_reg_write(tps65910, TPS65910_RTC_CTRL, rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to write RTC control: %d\n", ret);
		return ret;
	}
	
	/* update all the time registers in one shot */
	ret = tps65910_bulk_write(tps65910, TPS65910_SECONDS,
				       ALL_TIME_REGS, rtc_data);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC times: %d\n", ret);
		return ret;
	}
	
	/*Dummy read*/	
	ret = tps65910_reg_read(tps65910, TPS65910_RTC_CTRL);
	
	/* Start RTC again */
	ret = tps65910_reg_read(tps65910, TPS65910_RTC_CTRL);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}
	
	rtc_ctl = ret | BIT_RTC_CTRL_REG_STOP_RTC_M;

	ret = tps65910_reg_write(tps65910, TPS65910_RTC_CTRL, rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to write RTC control: %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * Read alarm time and date in RTC
 */
static int tps65910_rtc_readalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct tps65910_rtc *tps65910_rtc = dev_get_drvdata(dev);
	int ret;
	unsigned char alrm_data[ALL_ALM_REGS + 1];

	ret = tps65910_bulk_read(tps65910_rtc->tps65910, TPS65910_ALARM_SECONDS,
			       ALL_ALM_REGS, alrm_data);
	if (ret != 0) {
		dev_err(dev, "Failed to read alarm time: %d\n", ret);
		return ret;
	}

	/* some of these fields may be wildcard/"match all" */
	alrm->time.tm_sec = bcd2bin(alrm_data[0]);
	alrm->time.tm_min = bcd2bin(alrm_data[1]);
	alrm->time.tm_hour = bcd2bin(alrm_data[2]);
	alrm->time.tm_mday = bcd2bin(alrm_data[3]);
	alrm->time.tm_mon = bcd2bin(alrm_data[4]) - 1;
	alrm->time.tm_year = bcd2bin(alrm_data[5]) + 100;

	ret = tps65910_reg_read(tps65910_rtc->tps65910, TPS65910_RTC_INTERRUPTS);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}

	if (ret & BIT_RTC_INTERRUPTS_REG_IT_ALARM_M)
		alrm->enabled = 1;
	else
		alrm->enabled = 0;

	return 0;
}

static int tps65910_rtc_stop_alarm(struct tps65910_rtc *tps65910_rtc)
{
	tps65910_rtc->alarm_enabled = 0;

	return tps65910_clear_bits(tps65910_rtc->tps65910, TPS65910_RTC_INTERRUPTS,
			       BIT_RTC_INTERRUPTS_REG_IT_ALARM_M);

}

static int tps65910_rtc_start_alarm(struct tps65910_rtc *tps65910_rtc)
{
	tps65910_rtc->alarm_enabled = 1;

	return tps65910_set_bits(tps65910_rtc->tps65910, TPS65910_RTC_INTERRUPTS,
			       BIT_RTC_INTERRUPTS_REG_IT_ALARM_M);

}

static int tps65910_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct tps65910_rtc *tps65910_rtc = dev_get_drvdata(dev);
	int ret;
	unsigned char alrm_data[ALL_TIME_REGS + 1];

	ret = tps65910_rtc_stop_alarm(tps65910_rtc);
	if (ret < 0) {
		dev_err(dev, "Failed to stop alarm: %d\n", ret);
		return ret;
	}

	alrm_data[0] = bin2bcd(alrm->time.tm_sec);
	alrm_data[1] = bin2bcd(alrm->time.tm_min);
	alrm_data[2] = bin2bcd(alrm->time.tm_hour);
	alrm_data[3] = bin2bcd(alrm->time.tm_mday);
	alrm_data[4] = bin2bcd(alrm->time.tm_mon + 1);
	alrm_data[5] = bin2bcd(alrm->time.tm_year - 100);

	ret = tps65910_bulk_write(tps65910_rtc->tps65910, TPS65910_ALARM_SECONDS,
			       ALL_ALM_REGS, alrm_data);
	if (ret != 0) {
		dev_err(dev, "Failed to read alarm time: %d\n", ret);
		return ret;
	}

	if (alrm->enabled) {
		ret = tps65910_rtc_start_alarm(tps65910_rtc);
		if (ret < 0) {
			dev_err(dev, "Failed to start alarm: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int tps65910_rtc_alarm_irq_enable(struct device *dev,
				       unsigned int enabled)
{
	struct tps65910_rtc *tps65910_rtc = dev_get_drvdata(dev);

	if (enabled)
		return tps65910_rtc_start_alarm(tps65910_rtc);
	else
		return tps65910_rtc_stop_alarm(tps65910_rtc);
}

static int tps65910_rtc_update_irq_enable(struct device *dev,
				       unsigned int enabled)
{
	struct tps65910_rtc *tps65910_rtc = dev_get_drvdata(dev);

	if (enabled)
		return tps65910_set_bits(tps65910_rtc->tps65910, TPS65910_RTC_INTERRUPTS,
			       BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);
	else
		return tps65910_clear_bits(tps65910_rtc->tps65910, TPS65910_RTC_INTERRUPTS,
			       BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);
}

/*
 * We will just handle setting the frequency and make use the framework for
 * reading the periodic interupts.
 *
 * @freq: Current periodic IRQ freq:
 * bit 0: every second
 * bit 1: every minute
 * bit 2: every hour
 * bit 3: every day
 */
static int tps65910_rtc_irq_set_freq(struct device *dev, int freq)
{	
	struct tps65910_rtc *tps65910_rtc = dev_get_drvdata(dev);
	int ret;	
	u8 rtc_ctl;	
	
	if (freq < 0 || freq > 3)
		return -EINVAL;

	ret = tps65910_reg_read(tps65910_rtc->tps65910, TPS65910_RTC_INTERRUPTS);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC interrupt: %d\n", ret);
		return ret;
	}
	
	rtc_ctl = ret | freq;
	
	ret = tps65910_reg_write(tps65910_rtc->tps65910, TPS65910_RTC_INTERRUPTS, rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to write RTC control: %d\n", ret);
		return ret;
	}
	
	return ret;
}

static irqreturn_t tps65910_alm_irq(int irq, void *data)
{
	struct tps65910_rtc *tps65910_rtc = data;
	int ret;
	u8 rtc_ctl;

	/*Dummy read -- mandatory for status register*/
	ret = tps65910_reg_read(tps65910_rtc->tps65910, TPS65910_RTC_STATUS);
	if (ret < 0) {
		printk("%s:Failed to read RTC status: %d\n", __func__, ret);
		return ret;
	}
		
	ret = tps65910_reg_read(tps65910_rtc->tps65910, TPS65910_RTC_STATUS);
	if (ret < 0) {
		printk("%s:Failed to read RTC status: %d\n", __func__, ret);
		return ret;
	}
	rtc_ctl = ret&0xff;

	//The alarm interrupt keeps its low level, until the micro-controller write 1 in the ALARM bit of the RTC_STATUS_REG register.	
	ret = tps65910_reg_write(tps65910_rtc->tps65910, TPS65910_RTC_STATUS,rtc_ctl);
	if (ret < 0) {
		printk("%s:Failed to read RTC status: %d\n", __func__, ret);
		return ret;
	}
	
	rtc_update_irq(tps65910_rtc->rtc, 1, RTC_IRQF | RTC_AF);
	
	printk("%s:irq=%d,rtc_ctl=0x%x\n",__func__,irq,rtc_ctl);
	return IRQ_HANDLED;
}

static irqreturn_t tps65910_per_irq(int irq, void *data)
{
	struct tps65910_rtc *tps65910_rtc = data;
	
	rtc_update_irq(tps65910_rtc->rtc, 1, RTC_IRQF | RTC_UF);

	//printk("%s:irq=%d\n",__func__,irq);
	return IRQ_HANDLED;
}

static const struct rtc_class_ops tps65910_rtc_ops = {
	.read_time = tps65910_rtc_readtime,
	//.set_mmss = tps65910_rtc_set_mmss,
	.set_time = tps65910_rtc_set_time,
	.read_alarm = tps65910_rtc_readalarm,
	.set_alarm = tps65910_rtc_setalarm,
	.alarm_irq_enable = tps65910_rtc_alarm_irq_enable,
	//.update_irq_enable = tps65910_rtc_update_irq_enable,
	//.irq_set_freq = tps65910_rtc_irq_set_freq,
};

#ifdef CONFIG_PM
/* Turn off the alarm if it should not be a wake source. */
static int tps65910_rtc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tps65910_rtc *tps65910_rtc = dev_get_drvdata(&pdev->dev);
	int ret;
	
	if (tps65910_rtc->alarm_enabled && device_may_wakeup(&pdev->dev))
		ret = tps65910_rtc_start_alarm(tps65910_rtc);
	else
		ret = tps65910_rtc_stop_alarm(tps65910_rtc);

	if (ret != 0)
		dev_err(&pdev->dev, "Failed to update RTC alarm: %d\n", ret);

	return 0;
}

/* Enable the alarm if it should be enabled (in case it was disabled to
 * prevent use as a wake source).
 */
static int tps65910_rtc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tps65910_rtc *tps65910_rtc = dev_get_drvdata(&pdev->dev);
	int ret;

	if (tps65910_rtc->alarm_enabled) {
		ret = tps65910_rtc_start_alarm(tps65910_rtc);
		if (ret != 0)
			dev_err(&pdev->dev,
				"Failed to restart RTC alarm: %d\n", ret);
	}

	return 0;
}

/* Unconditionally disable the alarm */
static int tps65910_rtc_freeze(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tps65910_rtc *tps65910_rtc = dev_get_drvdata(&pdev->dev);
	int ret;
	
	ret = tps65910_rtc_stop_alarm(tps65910_rtc);
	if (ret != 0)
		dev_err(&pdev->dev, "Failed to stop RTC alarm: %d\n", ret);

	return 0;
}
#else
#define tps65910_rtc_suspend NULL
#define tps65910_rtc_resume NULL
#define tps65910_rtc_freeze NULL
#endif

struct platform_device *g_pdev;
static int tps65910_rtc_probe(struct platform_device *pdev)
{
	struct tps65910 *tps65910 = dev_get_drvdata(pdev->dev.parent);
	struct tps65910_rtc *tps65910_rtc;
	int per_irq;
	int alm_irq;
	int ret = 0;
	u8 rtc_ctl;
	
	struct rtc_time tm;
	struct rtc_time tm_def = {	//	2012.1.1 12:00:00 Saturday
			.tm_wday = 6,
			.tm_year = 111,
			.tm_mon = 0,
			.tm_mday = 1,
			.tm_hour = 12,
			.tm_min = 0,
			.tm_sec = 0,
		};
	
	tps65910_rtc = kzalloc(sizeof(*tps65910_rtc), GFP_KERNEL);
	if (tps65910_rtc == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, tps65910_rtc);
	tps65910_rtc->tps65910 = tps65910;
	per_irq = tps65910->irq_base + TPS65910_IRQ_RTC_PERIOD;
	alm_irq = tps65910->irq_base + TPS65910_IRQ_RTC_ALARM;
	
	/* Take rtc out of reset */
	ret = tps65910_reg_read(tps65910, TPS65910_DEVCTRL);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read TPS65910_DEVCTRL: %d\n", ret);
		return ret;
	}

	if(ret & BIT_RTC_PWDN)
	{
		rtc_ctl = ret & (~BIT_RTC_PWDN);

		ret = tps65910_reg_write(tps65910, TPS65910_DEVCTRL, rtc_ctl);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to write RTC control: %d\n", ret);
			return ret;
		}
	}
	
	/*start rtc default*/
	ret = tps65910_reg_read(tps65910, TPS65910_RTC_CTRL);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}

	if(!(ret & BIT_RTC_CTRL_REG_STOP_RTC_M))
	{
		rtc_ctl = ret | BIT_RTC_CTRL_REG_STOP_RTC_M;

		ret = tps65910_reg_write(tps65910, TPS65910_RTC_CTRL, rtc_ctl);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to write RTC control: %d\n", ret);
			return ret;
		}
	}
	
	ret = tps65910_reg_read(tps65910, TPS65910_RTC_STATUS);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read RTC status: %d\n", ret);
		return ret;
	}
		
	/*set init time*/
	ret = tps65910_rtc_readtime(&pdev->dev, &tm);
	if (ret)
	{
		dev_err(&pdev->dev, "Failed to read RTC time\n");
		return ret;
	}
	
	ret = rtc_valid_tm(&tm);
	if (ret) {
		dev_err(&pdev->dev,"invalid date/time and init time\n");
		tps65910_rtc_set_time(&pdev->dev, &tm_def); // 2011-01-01 12:00:00
		dev_info(&pdev->dev, "set RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
				1900 + tm_def.tm_year, tm_def.tm_mon + 1, tm_def.tm_mday, tm_def.tm_wday,
				tm_def.tm_hour, tm_def.tm_min, tm_def.tm_sec);
	}

	device_init_wakeup(&pdev->dev, 1);

	tps65910_rtc->rtc = rtc_device_register("tps65910", &pdev->dev,
					      &tps65910_rtc_ops, THIS_MODULE);
	if (IS_ERR(tps65910_rtc->rtc)) {
		ret = PTR_ERR(tps65910_rtc->rtc);
		goto err;
	}

	/*request rtc and alarm irq of tps65910*/
	ret = request_threaded_irq(per_irq, NULL, tps65910_per_irq,
				   IRQF_TRIGGER_RISING, "RTC period",
				   tps65910_rtc);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request periodic IRQ %d: %d\n",
			per_irq, ret);
	}

	ret = request_threaded_irq(alm_irq, NULL, tps65910_alm_irq,
				   IRQF_TRIGGER_RISING, "RTC alarm",
				   tps65910_rtc);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request alarm IRQ %d: %d\n",
			alm_irq, ret);
	}

	//for rtc irq test
	//tps65910_set_bits(tps65910_rtc->tps65910, TPS65910_RTC_INTERRUPTS,
	//			       BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);

	enable_irq_wake(alm_irq); // so tps65910 alarm irq can wake up system
	g_pdev = pdev;
	
	printk("%s:ok\n",__func__);
	
	return 0;

err:
	kfree(tps65910_rtc);
	return ret;
}

static int __devexit tps65910_rtc_remove(struct platform_device *pdev)
{
	struct tps65910_rtc *tps65910_rtc = platform_get_drvdata(pdev);
	int per_irq = tps65910_rtc->tps65910->irq_base + TPS65910_IRQ_RTC_PERIOD;
	int alm_irq = tps65910_rtc->tps65910->irq_base + TPS65910_IRQ_RTC_ALARM;

	free_irq(alm_irq, tps65910_rtc);
	free_irq(per_irq, tps65910_rtc);
	rtc_device_unregister(tps65910_rtc->rtc);
	kfree(tps65910_rtc);

	return 0;
}

static const struct dev_pm_ops tps65910_rtc_pm_ops = {
	.suspend = tps65910_rtc_suspend,
	.resume = tps65910_rtc_resume,

	.freeze = tps65910_rtc_freeze,
	.thaw = tps65910_rtc_resume,
	.restore = tps65910_rtc_resume,

	.poweroff = tps65910_rtc_suspend,
};

static struct platform_driver tps65910_rtc_driver = {
	.probe = tps65910_rtc_probe,
	.remove = __devexit_p(tps65910_rtc_remove),
	.driver = {
		.name = "tps65910-rtc",
		.pm = &tps65910_rtc_pm_ops,
	},
};

static ssize_t rtc_tps65910_test_write(struct file *file, 
			const char __user *buf, size_t count, loff_t *offset)
{
	char nr_buf[8];
	int nr = 0, ret;
	struct platform_device *pdev;	
	struct rtc_time tm;
	struct rtc_wkalrm alrm;
	struct tps65910_rtc *tps65910_rtc;
	
	if(count > 3)
		return -EFAULT;
	ret = copy_from_user(nr_buf, buf, count);
	if(ret < 0)
		return -EFAULT;

	sscanf(nr_buf, "%d", &nr);
	if(nr > 5 || nr < 0)
	{
		printk("%s:data is error\n",__func__);
		return -EFAULT;
	}

	if(!g_pdev)
		return -EFAULT;
	else
		pdev = g_pdev;

	
	tps65910_rtc = dev_get_drvdata(&pdev->dev);
	
	//test rtc time
	if(nr == 0)
	{	
		tm.tm_wday = 6;
		tm.tm_year = 111;
		tm.tm_mon = 0;
		tm.tm_mday = 1;
		tm.tm_hour = 12;
		tm.tm_min = 0;
		tm.tm_sec = 0;
	
		ret = tps65910_rtc_set_time(&pdev->dev, &tm); // 2011-01-01 12:00:00
		if (ret)
		{
			dev_err(&pdev->dev, "Failed to set RTC time\n");
			return -EFAULT;
		}

	}
	
	/*set init time*/
	ret = tps65910_rtc_readtime(&pdev->dev, &tm);
	if (ret)
		dev_err(&pdev->dev, "Failed to read RTC time\n");
	else
		dev_info(&pdev->dev, "RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
			1900 + tm.tm_year, tm.tm_mon + 1, tm.tm_mday, tm.tm_wday,
			tm.tm_hour, tm.tm_min, tm.tm_sec);
		
	if(!ret)
	printk("%s:ok\n",__func__);
	else
	printk("%s:error\n",__func__);
	

	//test rtc alarm
	if(nr == 2)
	{
		//2000-01-01 00:00:30
		if(tm.tm_sec < 30)
		{
			alrm.time.tm_sec = tm.tm_sec+30;	
			alrm.time.tm_min = tm.tm_min;
		}
		else
		{
			alrm.time.tm_sec = tm.tm_sec-30;
			alrm.time.tm_min = tm.tm_min+1;
		}
		alrm.time.tm_hour = tm.tm_hour;
		alrm.time.tm_mday = tm.tm_mday;
		alrm.time.tm_mon = tm.tm_mon;
		alrm.time.tm_year = tm.tm_year;		
		tps65910_rtc_alarm_irq_enable(&pdev->dev, 1);
		tps65910_rtc_setalarm(&pdev->dev, &alrm);

		dev_info(&pdev->dev, "Set alarm %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
				1900 + alrm.time.tm_year, alrm.time.tm_mon + 1, alrm.time.tm_mday, alrm.time.tm_wday,
				alrm.time.tm_hour, alrm.time.tm_min, alrm.time.tm_sec);
	}

	
	if(nr == 3)
	{	
		ret = tps65910_reg_read(tps65910_rtc->tps65910, TPS65910_RTC_STATUS);
		if (ret < 0) {
			printk("%s:Failed to read RTC status: %d\n", __func__, ret);
			return ret;
		}
		printk("%s:ret=0x%x\n",__func__,ret&0xff);

		ret = tps65910_reg_write(tps65910_rtc->tps65910, TPS65910_RTC_STATUS, ret&0xff);
		if (ret < 0) {
			printk("%s:Failed to read RTC status: %d\n", __func__, ret);
			return ret;
		}
	}

	if(nr == 4)
	tps65910_rtc_update_irq_enable(&pdev->dev, 1);

	if(nr == 5)
	tps65910_rtc_update_irq_enable(&pdev->dev, 0);
	
	return count;
}

static const struct file_operations rtc_tps65910_test_fops = {
	.write = rtc_tps65910_test_write,
};

static struct miscdevice rtc_tps65910_test_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "rtc_tps65910_test",
	.fops = &rtc_tps65910_test_fops,
};


static int __init tps65910_rtc_init(void)
{
	misc_register(&rtc_tps65910_test_misc);
	return platform_driver_register(&tps65910_rtc_driver);
}
subsys_initcall_sync(tps65910_rtc_init);

static void __exit tps65910_rtc_exit(void)
{	
        misc_deregister(&rtc_tps65910_test_misc);
	platform_driver_unregister(&tps65910_rtc_driver);
}
module_exit(tps65910_rtc_exit);

MODULE_DESCRIPTION("RTC driver for the tps65910 series PMICs");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:tps65910-rtc");
