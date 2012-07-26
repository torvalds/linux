/*
 * rtc-tps65910.c -- TPS65910 Real Time Clock interface
 *
 * Copyright (C) 2010 Mistral Solutions Pvt Ltd. <www.mistralsolutions.com>
 * Author: Umesh K <umeshk@mistralsolutions.com>
 *
 * Based on rtc-twl.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/mfd/tps65910.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

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
#define ALL_TIME_REGS				6

/*
 * Supports 1 byte read from TPS65910 RTC register.
 */
static int tps65910_rtc_read_u8(u8 *data, u8 reg)
{
	int ret;

	ret = tps65910_i2c_read_u8(TPS65910_I2C_ID0, data, reg);

	if (ret < 0)
		pr_err("tps65910_rtc: Could not read TPS65910"
				"register %X - error %d\n", reg, ret);
	return ret;
}

/*
 * Supports 1 byte write to TPS65910 RTC registers.
 */
static int tps65910_rtc_write_u8(u8 data, u8 reg)
{
	int ret;

	ret = tps65910_i2c_write_u8(TPS65910_I2C_ID0, data, reg);
	if (ret < 0)
		pr_err("tps65910_rtc: Could not write TPS65910"
				"register %X - error %d\n", reg, ret);
	return ret;
}

/*
 * Cache the value for timer/alarm interrupts register; this is
 * only changed by callers holding rtc ops lock (or resume).
 */
static unsigned char rtc_irq_bits;

/*
 * Enable 1/second update and/or alarm interrupts.
 */
static int set_rtc_irq_bit(unsigned char bit)
{
	unsigned char val;
	int ret;

	val = rtc_irq_bits | bit;
	val |= bit;
	ret = tps65910_rtc_write_u8(val, TPS65910_REG_RTC_INTERRUPTS);
	if (ret == 0)
		rtc_irq_bits = val;

	return ret;
}

/*
 * Disable update and/or alarm interrupts.
 */
static int mask_rtc_irq_bit(unsigned char bit)
{
	unsigned char val;
	int ret;

	val = rtc_irq_bits & ~bit;
	ret = tps65910_rtc_write_u8(val, TPS65910_REG_RTC_INTERRUPTS);
	if (ret == 0)
		rtc_irq_bits = val;

	return ret;
}

static int tps65910_rtc_alarm_irq_enable(struct device *dev, unsigned enabled)
{
	int ret;

	if (enabled)
		ret = set_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_ALARM_M);
	else
		ret = mask_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_ALARM_M);

	return ret;
}

static int tps65910_rtc_update_irq_enable(struct device *dev, unsigned enabled)
{
	int ret;

	if (enabled)
		ret = set_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);
	else
		ret = mask_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);

	return ret;
}

#if 1 /* Debugging periodic interrupts */
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
	struct rtc_device *rtc = dev_get_drvdata(dev);

	if (freq < 0 || freq > 3)
		return -EINVAL;

	rtc->irq_freq = freq;
	/* set rtc irq freq to user defined value */
	set_rtc_irq_bit(freq);

	return 0;
}
#endif

/*
 * Gets current TPS65910 RTC time and date parameters.
 *
 * The RTC's time/alarm representation is not what gmtime(3) requires
 * Linux to use:
 *
 *  - Months are 1..12 vs Linux 0-11
 *  - Years are 0..99 vs Linux 1900..N (we assume 21st century)
 */
static int tps65910_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char rtc_data[ALL_TIME_REGS + 1];
	int ret;
	u8 save_control;

	tps65910_rtc_read_u8(&save_control, TPS65910_REG_RTC_CTRL);
	ret = tps65910_rtc_read_u8(&save_control, TPS65910_REG_RTC_CTRL);
	if (ret < 0)
		return ret;

	save_control &= ~BIT_RTC_CTRL_REG_RTC_V_OPT_M;

	ret = tps65910_rtc_write_u8(save_control, TPS65910_REG_RTC_CTRL);
	if (ret < 0)
		return ret;

	ret = tps65910_rtc_read_u8(&rtc_data[0], TPS65910_REG_SECONDS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_read_u8(&rtc_data[1], TPS65910_REG_MINUTES);
	if (ret < 0) {
		dev_err(dev, "rtc_read_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_read_u8(&rtc_data[2], TPS65910_REG_HOURS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_read_u8(&rtc_data[3], TPS65910_REG_DAYS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_read_u8(&rtc_data[4], TPS65910_REG_MONTHS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_read_u8(&rtc_data[5], TPS65910_REG_YEARS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_time error %d\n", ret);
		return ret;
	}

	tm->tm_sec = bcd2bin(rtc_data[0]);
	tm->tm_min = bcd2bin(rtc_data[1]);
	tm->tm_hour = bcd2bin(rtc_data[2]);
	tm->tm_mday = bcd2bin(rtc_data[3]);
	tm->tm_mon = bcd2bin(rtc_data[4]) - 1;
	tm->tm_year = bcd2bin(rtc_data[5]) + 100;

	DBG("%s [%d]tm_wday=%d \n",__FUNCTION__,__LINE__,tm->tm_wday);
	DBG("%s [%d]tm_sec=%d \n",__FUNCTION__,__LINE__,tm->tm_sec);
	DBG("%s [%d]tm_min=%d \n",__FUNCTION__,__LINE__,tm->tm_min);
	DBG("%s [%d]tm_hour=%d \n",__FUNCTION__,__LINE__,tm->tm_hour);
	DBG("%s [%d]tm_mday=%d \n",__FUNCTION__,__LINE__,tm->tm_mday);
	DBG("%s [%d]tm_mon=%d \n",__FUNCTION__,__LINE__,tm->tm_mon);
	DBG("%s [%d]tm_year=%d \n",__FUNCTION__,__LINE__,tm->tm_year);

	return ret;
}

static int tps65910_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char save_control;
	unsigned char rtc_data[ALL_TIME_REGS + 1];
	int ret;

	DBG("%s [%d]tm_wday=%d \n",__FUNCTION__,__LINE__,tm->tm_wday);
	DBG("%s [%d]tm_sec=%d \n",__FUNCTION__,__LINE__,tm->tm_sec);
	DBG("%s [%d]tm_min=%d \n",__FUNCTION__,__LINE__,tm->tm_min);
	DBG("%s [%d]tm_hour=%d \n",__FUNCTION__,__LINE__,tm->tm_hour);
	DBG("%s [%d]tm_mday=%d \n",__FUNCTION__,__LINE__,tm->tm_mday);
	DBG("%s [%d]tm_mon=%d \n",__FUNCTION__,__LINE__,tm->tm_mon);
	DBG("%s [%d]tm_year=%d \n",__FUNCTION__,__LINE__,tm->tm_year);

	rtc_data[1] = bin2bcd(tm->tm_sec);
	rtc_data[2] = bin2bcd(tm->tm_min);
	rtc_data[3] = bin2bcd(tm->tm_hour);
	rtc_data[4] = bin2bcd(tm->tm_mday);
	rtc_data[5] = bin2bcd(tm->tm_mon + 1);
	rtc_data[6] = bin2bcd(tm->tm_year - 100);

	/*Dummy read*/
	ret = tps65910_rtc_read_u8(&save_control, TPS65910_REG_RTC_CTRL);

	/* Stop RTC while updating the TC registers */
	ret = tps65910_rtc_read_u8(&save_control, TPS65910_REG_RTC_CTRL);
	if (ret < 0)
		goto out;

	save_control &= ~BIT_RTC_CTRL_REG_STOP_RTC_M;

	tps65910_rtc_write_u8(save_control, TPS65910_REG_RTC_CTRL);

	/* update all the time registers in one shot */
	ret = tps65910_rtc_write_u8(rtc_data[1], TPS65910_REG_SECONDS);
	if (ret < 0) {
		dev_err(dev, "rtc_write_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_write_u8(rtc_data[2], TPS65910_REG_MINUTES);
	if (ret < 0) {
		dev_err(dev, "rtc_write_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_write_u8(rtc_data[3], TPS65910_REG_HOURS);
	if (ret < 0) {
		dev_err(dev, "rtc_write_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_write_u8(rtc_data[4], TPS65910_REG_DAYS);
	if (ret < 0) {
		dev_err(dev, "rtc_write_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_write_u8(rtc_data[5], TPS65910_REG_MONTHS);
	if (ret < 0) {
		dev_err(dev, "rtc_write_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_write_u8(rtc_data[6], TPS65910_REG_YEARS);
	if (ret < 0) {
		dev_err(dev, "rtc_write_time error %d\n", ret);
		return ret;
	}

	/*Dummy read*/
	ret = tps65910_rtc_read_u8(&save_control, TPS65910_REG_RTC_CTRL);

	ret = tps65910_rtc_read_u8(&save_control, TPS65910_REG_RTC_CTRL);
	if (ret < 0)
		goto out;
	/* Start back RTC */
	save_control |= BIT_RTC_CTRL_REG_STOP_RTC_M;
	ret = tps65910_rtc_write_u8(save_control, TPS65910_REG_RTC_CTRL);

out:
	return ret;
}

/*
 * Gets current TPS65910 RTC alarm time.
 */
static int tps65910_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned char rtc_data[ALL_TIME_REGS + 1];
	int ret;

	ret = tps65910_rtc_read_u8(&rtc_data[0], TPS65910_REG_ALARM_SECONDS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_read_u8(&rtc_data[1], TPS65910_REG_ALARM_MINUTES);
	if (ret < 0) {
		dev_err(dev, "rtc_read_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_read_u8(&rtc_data[2], TPS65910_REG_ALARM_HOURS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_read_u8(&rtc_data[3], TPS65910_REG_ALARM_DAYS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_read_u8(&rtc_data[4], TPS65910_REG_ALARM_MONTHS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_read_u8(&rtc_data[5], TPS65910_REG_ALARM_YEARS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_time error %d\n", ret);
		return ret;
	}

	/* some of these fields may be wildcard/"match all" */
	alm->time.tm_sec = bcd2bin(rtc_data[0]);
	alm->time.tm_min = bcd2bin(rtc_data[1]);
	alm->time.tm_hour = bcd2bin(rtc_data[2]);
	alm->time.tm_mday = bcd2bin(rtc_data[3]);
	alm->time.tm_mon = bcd2bin(rtc_data[4]) - 1;
	alm->time.tm_year = bcd2bin(rtc_data[5]) + 100;

	/* report cached alarm enable state */
	if (rtc_irq_bits & BIT_RTC_INTERRUPTS_REG_IT_ALARM_M)
		alm->enabled = 1;

	return ret;
}

static int tps65910_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned char alarm_data[ALL_TIME_REGS + 1];
	int ret;

	ret = tps65910_rtc_alarm_irq_enable(dev, 0);
	if (ret)
		goto out;

	alarm_data[1] = bin2bcd(alm->time.tm_sec);
	alarm_data[2] = bin2bcd(alm->time.tm_min);
	alarm_data[3] = bin2bcd(alm->time.tm_hour);
	alarm_data[4] = bin2bcd(alm->time.tm_mday);
	alarm_data[5] = bin2bcd(alm->time.tm_mon + 1);
	alarm_data[6] = bin2bcd(alm->time.tm_year - 100);

	/* update all the alarm registers in one shot */
	ret = tps65910_rtc_write_u8(alarm_data[1], TPS65910_REG_ALARM_SECONDS);
	if (ret < 0) {
		dev_err(dev, "rtc_write_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_write_u8(alarm_data[2], TPS65910_REG_ALARM_MINUTES);
	if (ret < 0) {
		dev_err(dev, "rtc_write_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_write_u8(alarm_data[3], TPS65910_REG_ALARM_HOURS);
	if (ret < 0) {
		dev_err(dev, "rtc_write_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_write_u8(alarm_data[4], TPS65910_REG_ALARM_DAYS);
	if (ret < 0) {
		dev_err(dev, "rtc_write_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_write_u8(alarm_data[5], TPS65910_REG_ALARM_MONTHS);
	if (ret < 0) {
		dev_err(dev, "rtc_write_time error %d\n", ret);
		return ret;
	}
	ret = tps65910_rtc_write_u8(alarm_data[6], TPS65910_REG_ALARM_YEARS);
	if (ret < 0) {
		dev_err(dev, "rtc_write_time error %d\n", ret);
		return ret;
	}

	if (alm->enabled)
		ret = tps65910_rtc_alarm_irq_enable(dev, 1);
out:
	return ret;
}


struct work_struct rtc_wq;
unsigned long rtc_events;
struct rtc_device *global_rtc;

void tps65910_rtc_work(void  *data)
{
	int res;
	u8 rd_reg;
	unsigned long events = 0;

	DBG("Enter::%s %d\n",__FUNCTION__,__LINE__);

	res = tps65910_rtc_read_u8(&rd_reg, TPS65910_REG_INT_STS);

	if (res < 0)
		goto out;
	/*
	 * Figure out source of interrupt: ALARM or TIMER in RTC_STATUS_REG.
	 * only one (ALARM or RTC) interrupt source may be enabled
	 * at time, we also could check our results
	 * by reading RTS_INTERRUPTS_REGISTER[IT_TIMER,IT_ALARM]
	 */
	if (rd_reg & TPS65910_RTC_ALARM_IT) {
		res = tps65910_rtc_write_u8(rd_reg | TPS65910_RTC_ALARM_IT,
				TPS65910_REG_INT_STS);
		if (res < 0)
			goto out;

		/*Dummy read -- mandatory for status register*/
		res = tps65910_rtc_read_u8(&rd_reg, TPS65910_REG_RTC_STATUS);
		mdelay(100);
		res = tps65910_rtc_read_u8(&rd_reg, TPS65910_REG_RTC_STATUS);
		res = tps65910_rtc_write_u8(rd_reg, TPS65910_REG_RTC_STATUS);

		rtc_events |= RTC_IRQF | RTC_AF;
	} else if (rd_reg & TPS65910_RTC_PERIOD_IT) {
		res = tps65910_rtc_write_u8(rd_reg | TPS65910_RTC_PERIOD_IT,
				TPS65910_REG_INT_STS);
		if (res < 0)
			goto out;

		/*Dummy read -- mandatory for status register*/
		res = tps65910_rtc_read_u8(&rd_reg, TPS65910_REG_RTC_STATUS);
		mdelay(100);
		res = tps65910_rtc_read_u8(&rd_reg, TPS65910_REG_RTC_STATUS);
		rd_reg &= 0xC3;
		res = tps65910_rtc_write_u8(rd_reg, TPS65910_REG_RTC_STATUS);
		rtc_events |= RTC_IRQF | RTC_UF;
	}
out:
	/* Notify RTC core on event */
	events = rtc_events;
	rtc_update_irq(global_rtc, 1, events);
}

static struct rtc_class_ops tps65910_rtc_ops = {
	.read_time	= tps65910_rtc_read_time,
	.set_time	= tps65910_rtc_set_time,
	.read_alarm	= tps65910_rtc_read_alarm,
	.set_alarm	= tps65910_rtc_set_alarm,
	.alarm_irq_enable = tps65910_rtc_alarm_irq_enable,
//	.update_irq_enable = tps65910_rtc_update_irq_enable,
//	.irq_set_freq	= tps65910_rtc_irq_set_freq,
};

static int __devinit tps65910_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;
	int ret = 0, stop_run = 0;
	u8 rd_reg;
	struct rtc_time tm_def = {	//	2011.1.1 12:00:00 Saturday
		.tm_wday = 6,
		.tm_year = 111,
		.tm_mon = 0,
		.tm_mday = 1,
		.tm_hour = 12,
		.tm_min = 0,
		.tm_sec = 0,
	};

	rtc = rtc_device_register(pdev->name,
			&pdev->dev, &tps65910_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc)) {
		ret = PTR_ERR(rtc);
		dev_err(&pdev->dev, "can't register TPS65910 RTC device,\
					 err %ld\n", PTR_ERR(rtc));
		goto out0;

	}
	printk(KERN_INFO "TPS65910 RTC device successfully registered\n");

	platform_set_drvdata(pdev, rtc);
	/* Take rtc out of reset */
	tps65910_rtc_read_u8(&rd_reg, TPS65910_REG_DEVCTRL);
	rd_reg &= ~BIT_RTC_PWDN;
	ret = tps65910_rtc_write_u8(rd_reg, TPS65910_REG_DEVCTRL);

	/* Dummy read to ensure that the register gets updated.
	 * Please refer tps65910 TRM table:25 for details
	 */
	stop_run = 0;
	ret = tps65910_rtc_read_u8(&rd_reg, TPS65910_REG_RTC_STATUS);
	if (ret < 0) {
		printk(KERN_ERR "TPS65910 RTC STATUS REG READ FAILED\n");
		goto out1;
	}

	if (rd_reg & BIT_RTC_STATUS_REG_POWER_UP_M) {
		dev_warn(&pdev->dev, "Power up reset detected.\n");
		//	cwz:if rtc power up reset, set default time.
		printk(KERN_INFO "TPS65910 RTC set to default time\n");
		tps65910_rtc_set_time(&rtc->dev, &tm_def);
	}
	if (!(rd_reg & BIT_RTC_STATUS_REG_RUN_M)) {
		dev_warn(&pdev->dev, "RTC stop run.\n");
		stop_run = 1;
	}
	if (rd_reg & BIT_RTC_STATUS_REG_ALARM_M)
		dev_warn(&pdev->dev, "Pending Alarm interrupt detected.\n");

	/* Clear RTC Power up reset and pending alarm interrupts */
	ret = tps65910_rtc_write_u8(rd_reg, TPS65910_REG_RTC_STATUS);
	if (ret < 0)
		goto out1;

	ret = tps65910_rtc_read_u8(&rd_reg, TPS65910_REG_INT_STS);
	if (ret < 0) {
		printk(KERN_ERR "TPS65910 RTC STATUS REG READ FAILED\n");
		goto out1;
	}

	if (rd_reg & 0x40) {
		printk(KERN_INFO "pending alarm interrupt!!! clearing!!!");
		tps65910_rtc_write_u8(rd_reg, TPS65910_REG_INT_STS);
	}

	global_rtc = rtc;

	/* Link RTC IRQ handler to TPS65910 Core */
	//tps65910_add_irq_work(TPS65910_RTC_ALARM_IRQ, tps65910_rtc_work);
	//tps65910_add_irq_work(TPS65910_RTC_PERIOD_IRQ, tps65910_rtc_work);

	/* Check RTC module status, Enable if it is off */
	if (stop_run) {
		dev_info(&pdev->dev, "Enabling TPS65910-RTC.\n");
		//	cwz:if rtc stop, set default time, then enable rtc
		printk(KERN_INFO "TPS65910 RTC set to default time\n");
		tps65910_rtc_set_time(&rtc->dev, &tm_def);
		ret = tps65910_rtc_read_u8(&rd_reg, TPS65910_REG_RTC_CTRL);
		if (ret < 0)
			goto out1;

		rd_reg |= BIT_RTC_CTRL_REG_STOP_RTC_M;
		ret = tps65910_rtc_write_u8(rd_reg, TPS65910_REG_RTC_CTRL);
		if (ret < 0)
			goto out1;
	}

	/* init cached IRQ enable bits */
	ret = tps65910_rtc_read_u8(&rtc_irq_bits, TPS65910_REG_RTC_INTERRUPTS);
	if (ret < 0)
		goto out1;

	tps65910_rtc_write_u8(0x3F, TPS65910_REG_INT_MSK);
	return ret;

out1:
	rtc_device_unregister(rtc);
out0:
	return ret;
}

/*
 * Disable all TPS65910 RTC module interrupts.
 * Sets status flag to free.
 */
static int __devexit tps65910_rtc_remove(struct platform_device *pdev)
{
	/* leave rtc running, but disable irqs */
	struct rtc_device *rtc = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	mask_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_ALARM_M);
	mask_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);


	free_irq(irq, rtc);

	rtc_device_unregister(rtc);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void tps65910_rtc_shutdown(struct platform_device *pdev)
{
	/* mask timer interrupts, but leave alarm interrupts on to enable
	 * power-on when alarm is triggered
	 */
	mask_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);
}

#ifdef CONFIG_PM

static unsigned char irqstat;

static
int tps65910_rtc_suspend(struct platform_device *pdev, pm_message_t state)
{
	irqstat = rtc_irq_bits;
	mask_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);
	return 0;
}

static int tps65910_rtc_resume(struct platform_device *pdev)
{
	set_rtc_irq_bit(irqstat);
	return 0;
}

#else
#define tps65910_rtc_suspend NULL
#define tps65910_rtc_resume  NULL
#endif


static struct platform_driver tps65910rtc_driver = {
	.probe		= tps65910_rtc_probe,
	.remove		= __devexit_p(tps65910_rtc_remove),
	.shutdown	= tps65910_rtc_shutdown,
	.suspend	= tps65910_rtc_suspend,
	.resume		= tps65910_rtc_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "tps65910_rtc",
	},
};

//extern int board_wm831x ;
static int __init tps65910_rtc_init(void)
{
//	if (board_wm831x == 1)
//	{
//		printk("board with wm831 not tps65910,so skip register tps65910\n");
//		return 0;
//	}

	return platform_driver_register(&tps65910rtc_driver);
}
module_init(tps65910_rtc_init);

static void __exit tps65910_rtc_exit(void)
{
	platform_driver_unregister(&tps65910rtc_driver);
}
module_exit(tps65910_rtc_exit);

MODULE_ALIAS("platform:tps65910_rtc");
MODULE_AUTHOR("cwz  <cwz@rockchips.com");
MODULE_LICENSE("GPL");
