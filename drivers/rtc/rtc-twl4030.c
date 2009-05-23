/*
 * rtc-twl4030.c -- TWL4030 Real Time Clock interface
 *
 * Copyright (C) 2007 MontaVista Software, Inc
 * Author: Alexandre Rusev <source@mvista.com>
 *
 * Based on original TI driver twl4030-rtc.c
 *   Copyright (C) 2006 Texas Instruments, Inc.
 *
 * Based on rtc-omap.c
 *   Copyright (C) 2003 MontaVista Software, Inc.
 *   Author: George G. Davis <gdavis@mvista.com> or <source@mvista.com>
 *   Copyright (C) 2006 David Brownell
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

#include <linux/i2c/twl4030.h>


/*
 * RTC block register offsets (use TWL_MODULE_RTC)
 */
#define REG_SECONDS_REG                          0x00
#define REG_MINUTES_REG                          0x01
#define REG_HOURS_REG                            0x02
#define REG_DAYS_REG                             0x03
#define REG_MONTHS_REG                           0x04
#define REG_YEARS_REG                            0x05
#define REG_WEEKS_REG                            0x06

#define REG_ALARM_SECONDS_REG                    0x07
#define REG_ALARM_MINUTES_REG                    0x08
#define REG_ALARM_HOURS_REG                      0x09
#define REG_ALARM_DAYS_REG                       0x0A
#define REG_ALARM_MONTHS_REG                     0x0B
#define REG_ALARM_YEARS_REG                      0x0C

#define REG_RTC_CTRL_REG                         0x0D
#define REG_RTC_STATUS_REG                       0x0E
#define REG_RTC_INTERRUPTS_REG                   0x0F

#define REG_RTC_COMP_LSB_REG                     0x10
#define REG_RTC_COMP_MSB_REG                     0x11

/* RTC_CTRL_REG bitfields */
#define BIT_RTC_CTRL_REG_STOP_RTC_M              0x01
#define BIT_RTC_CTRL_REG_ROUND_30S_M             0x02
#define BIT_RTC_CTRL_REG_AUTO_COMP_M             0x04
#define BIT_RTC_CTRL_REG_MODE_12_24_M            0x08
#define BIT_RTC_CTRL_REG_TEST_MODE_M             0x10
#define BIT_RTC_CTRL_REG_SET_32_COUNTER_M        0x20
#define BIT_RTC_CTRL_REG_GET_TIME_M              0x40

/* RTC_STATUS_REG bitfields */
#define BIT_RTC_STATUS_REG_RUN_M                 0x02
#define BIT_RTC_STATUS_REG_1S_EVENT_M            0x04
#define BIT_RTC_STATUS_REG_1M_EVENT_M            0x08
#define BIT_RTC_STATUS_REG_1H_EVENT_M            0x10
#define BIT_RTC_STATUS_REG_1D_EVENT_M            0x20
#define BIT_RTC_STATUS_REG_ALARM_M               0x40
#define BIT_RTC_STATUS_REG_POWER_UP_M            0x80

/* RTC_INTERRUPTS_REG bitfields */
#define BIT_RTC_INTERRUPTS_REG_EVERY_M           0x03
#define BIT_RTC_INTERRUPTS_REG_IT_TIMER_M        0x04
#define BIT_RTC_INTERRUPTS_REG_IT_ALARM_M        0x08


/* REG_SECONDS_REG through REG_YEARS_REG is how many registers? */
#define ALL_TIME_REGS		6

/*----------------------------------------------------------------------*/

/*
 * Supports 1 byte read from TWL4030 RTC register.
 */
static int twl4030_rtc_read_u8(u8 *data, u8 reg)
{
	int ret;

	ret = twl4030_i2c_read_u8(TWL4030_MODULE_RTC, data, reg);
	if (ret < 0)
		pr_err("twl4030_rtc: Could not read TWL4030"
		       "register %X - error %d\n", reg, ret);
	return ret;
}

/*
 * Supports 1 byte write to TWL4030 RTC registers.
 */
static int twl4030_rtc_write_u8(u8 data, u8 reg)
{
	int ret;

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_RTC, data, reg);
	if (ret < 0)
		pr_err("twl4030_rtc: Could not write TWL4030"
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
	val &= ~BIT_RTC_INTERRUPTS_REG_EVERY_M;
	ret = twl4030_rtc_write_u8(val, REG_RTC_INTERRUPTS_REG);
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
	ret = twl4030_rtc_write_u8(val, REG_RTC_INTERRUPTS_REG);
	if (ret == 0)
		rtc_irq_bits = val;

	return ret;
}

static int twl4030_rtc_alarm_irq_enable(struct device *dev, unsigned enabled)
{
	int ret;

	if (enabled)
		ret = set_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_ALARM_M);
	else
		ret = mask_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_ALARM_M);

	return ret;
}

static int twl4030_rtc_update_irq_enable(struct device *dev, unsigned enabled)
{
	int ret;

	if (enabled)
		ret = set_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);
	else
		ret = mask_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);

	return ret;
}

/*
 * Gets current TWL4030 RTC time and date parameters.
 *
 * The RTC's time/alarm representation is not what gmtime(3) requires
 * Linux to use:
 *
 *  - Months are 1..12 vs Linux 0-11
 *  - Years are 0..99 vs Linux 1900..N (we assume 21st century)
 */
static int twl4030_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char rtc_data[ALL_TIME_REGS + 1];
	int ret;
	u8 save_control;

	ret = twl4030_rtc_read_u8(&save_control, REG_RTC_CTRL_REG);
	if (ret < 0)
		return ret;

	save_control |= BIT_RTC_CTRL_REG_GET_TIME_M;

	ret = twl4030_rtc_write_u8(save_control, REG_RTC_CTRL_REG);
	if (ret < 0)
		return ret;

	ret = twl4030_i2c_read(TWL4030_MODULE_RTC, rtc_data,
			       REG_SECONDS_REG, ALL_TIME_REGS);

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

	return ret;
}

static int twl4030_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char save_control;
	unsigned char rtc_data[ALL_TIME_REGS + 1];
	int ret;

	rtc_data[1] = bin2bcd(tm->tm_sec);
	rtc_data[2] = bin2bcd(tm->tm_min);
	rtc_data[3] = bin2bcd(tm->tm_hour);
	rtc_data[4] = bin2bcd(tm->tm_mday);
	rtc_data[5] = bin2bcd(tm->tm_mon + 1);
	rtc_data[6] = bin2bcd(tm->tm_year - 100);

	/* Stop RTC while updating the TC registers */
	ret = twl4030_rtc_read_u8(&save_control, REG_RTC_CTRL_REG);
	if (ret < 0)
		goto out;

	save_control &= ~BIT_RTC_CTRL_REG_STOP_RTC_M;
	twl4030_rtc_write_u8(save_control, REG_RTC_CTRL_REG);
	if (ret < 0)
		goto out;

	/* update all the time registers in one shot */
	ret = twl4030_i2c_write(TWL4030_MODULE_RTC, rtc_data,
			REG_SECONDS_REG, ALL_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "rtc_set_time error %d\n", ret);
		goto out;
	}

	/* Start back RTC */
	save_control |= BIT_RTC_CTRL_REG_STOP_RTC_M;
	ret = twl4030_rtc_write_u8(save_control, REG_RTC_CTRL_REG);

out:
	return ret;
}

/*
 * Gets current TWL4030 RTC alarm time.
 */
static int twl4030_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned char rtc_data[ALL_TIME_REGS + 1];
	int ret;

	ret = twl4030_i2c_read(TWL4030_MODULE_RTC, rtc_data,
			       REG_ALARM_SECONDS_REG, ALL_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_alarm error %d\n", ret);
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

static int twl4030_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned char alarm_data[ALL_TIME_REGS + 1];
	int ret;

	ret = twl4030_rtc_alarm_irq_enable(dev, 0);
	if (ret)
		goto out;

	alarm_data[1] = bin2bcd(alm->time.tm_sec);
	alarm_data[2] = bin2bcd(alm->time.tm_min);
	alarm_data[3] = bin2bcd(alm->time.tm_hour);
	alarm_data[4] = bin2bcd(alm->time.tm_mday);
	alarm_data[5] = bin2bcd(alm->time.tm_mon + 1);
	alarm_data[6] = bin2bcd(alm->time.tm_year - 100);

	/* update all the alarm registers in one shot */
	ret = twl4030_i2c_write(TWL4030_MODULE_RTC, alarm_data,
			REG_ALARM_SECONDS_REG, ALL_TIME_REGS);
	if (ret) {
		dev_err(dev, "rtc_set_alarm error %d\n", ret);
		goto out;
	}

	if (alm->enabled)
		ret = twl4030_rtc_alarm_irq_enable(dev, 1);
out:
	return ret;
}

static irqreturn_t twl4030_rtc_interrupt(int irq, void *rtc)
{
	unsigned long events = 0;
	int ret = IRQ_NONE;
	int res;
	u8 rd_reg;

#ifdef CONFIG_LOCKDEP
	/* WORKAROUND for lockdep forcing IRQF_DISABLED on us, which
	 * we don't want and can't tolerate.  Although it might be
	 * friendlier not to borrow this thread context...
	 */
	local_irq_enable();
#endif

	res = twl4030_rtc_read_u8(&rd_reg, REG_RTC_STATUS_REG);
	if (res)
		goto out;
	/*
	 * Figure out source of interrupt: ALARM or TIMER in RTC_STATUS_REG.
	 * only one (ALARM or RTC) interrupt source may be enabled
	 * at time, we also could check our results
	 * by reading RTS_INTERRUPTS_REGISTER[IT_TIMER,IT_ALARM]
	 */
	if (rd_reg & BIT_RTC_STATUS_REG_ALARM_M)
		events |= RTC_IRQF | RTC_AF;
	else
		events |= RTC_IRQF | RTC_UF;

	res = twl4030_rtc_write_u8(rd_reg | BIT_RTC_STATUS_REG_ALARM_M,
				   REG_RTC_STATUS_REG);
	if (res)
		goto out;

	/* Clear on Read enabled. RTC_IT bit of TWL4030_INT_PWR_ISR1
	 * needs 2 reads to clear the interrupt. One read is done in
	 * do_twl4030_pwrirq(). Doing the second read, to clear
	 * the bit.
	 *
	 * FIXME the reason PWR_ISR1 needs an extra read is that
	 * RTC_IF retriggered until we cleared REG_ALARM_M above.
	 * But re-reading like this is a bad hack; by doing so we
	 * risk wrongly clearing status for some other IRQ (losing
	 * the interrupt).  Be smarter about handling RTC_UF ...
	 */
	res = twl4030_i2c_read_u8(TWL4030_MODULE_INT,
			&rd_reg, TWL4030_INT_PWR_ISR1);
	if (res)
		goto out;

	/* Notify RTC core on event */
	rtc_update_irq(rtc, 1, events);

	ret = IRQ_HANDLED;
out:
	return ret;
}

static struct rtc_class_ops twl4030_rtc_ops = {
	.read_time	= twl4030_rtc_read_time,
	.set_time	= twl4030_rtc_set_time,
	.read_alarm	= twl4030_rtc_read_alarm,
	.set_alarm	= twl4030_rtc_set_alarm,
	.alarm_irq_enable = twl4030_rtc_alarm_irq_enable,
	.update_irq_enable = twl4030_rtc_update_irq_enable,
};

/*----------------------------------------------------------------------*/

static int __devinit twl4030_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;
	int ret = 0;
	int irq = platform_get_irq(pdev, 0);
	u8 rd_reg;

	if (irq <= 0)
		return -EINVAL;

	rtc = rtc_device_register(pdev->name,
				  &pdev->dev, &twl4030_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		ret = PTR_ERR(rtc);
		dev_err(&pdev->dev, "can't register RTC device, err %ld\n",
			PTR_ERR(rtc));
		goto out0;

	}

	platform_set_drvdata(pdev, rtc);

	ret = twl4030_rtc_read_u8(&rd_reg, REG_RTC_STATUS_REG);
	if (ret < 0)
		goto out1;

	if (rd_reg & BIT_RTC_STATUS_REG_POWER_UP_M)
		dev_warn(&pdev->dev, "Power up reset detected.\n");

	if (rd_reg & BIT_RTC_STATUS_REG_ALARM_M)
		dev_warn(&pdev->dev, "Pending Alarm interrupt detected.\n");

	/* Clear RTC Power up reset and pending alarm interrupts */
	ret = twl4030_rtc_write_u8(rd_reg, REG_RTC_STATUS_REG);
	if (ret < 0)
		goto out1;

	ret = request_irq(irq, twl4030_rtc_interrupt,
				IRQF_TRIGGER_RISING,
				dev_name(&rtc->dev), rtc);
	if (ret < 0) {
		dev_err(&pdev->dev, "IRQ is not free.\n");
		goto out1;
	}

	/* Check RTC module status, Enable if it is off */
	ret = twl4030_rtc_read_u8(&rd_reg, REG_RTC_CTRL_REG);
	if (ret < 0)
		goto out2;

	if (!(rd_reg & BIT_RTC_CTRL_REG_STOP_RTC_M)) {
		dev_info(&pdev->dev, "Enabling TWL4030-RTC.\n");
		rd_reg = BIT_RTC_CTRL_REG_STOP_RTC_M;
		ret = twl4030_rtc_write_u8(rd_reg, REG_RTC_CTRL_REG);
		if (ret < 0)
			goto out2;
	}

	/* init cached IRQ enable bits */
	ret = twl4030_rtc_read_u8(&rtc_irq_bits, REG_RTC_INTERRUPTS_REG);
	if (ret < 0)
		goto out2;

	return ret;

out2:
	free_irq(irq, rtc);
out1:
	rtc_device_unregister(rtc);
out0:
	return ret;
}

/*
 * Disable all TWL4030 RTC module interrupts.
 * Sets status flag to free.
 */
static int __devexit twl4030_rtc_remove(struct platform_device *pdev)
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

static void twl4030_rtc_shutdown(struct platform_device *pdev)
{
	/* mask timer interrupts, but leave alarm interrupts on to enable
	   power-on when alarm is triggered */
	mask_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);
}

#ifdef CONFIG_PM

static unsigned char irqstat;

static int twl4030_rtc_suspend(struct platform_device *pdev, pm_message_t state)
{
	irqstat = rtc_irq_bits;

	mask_rtc_irq_bit(BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);
	return 0;
}

static int twl4030_rtc_resume(struct platform_device *pdev)
{
	set_rtc_irq_bit(irqstat);
	return 0;
}

#else
#define twl4030_rtc_suspend NULL
#define twl4030_rtc_resume  NULL
#endif

MODULE_ALIAS("platform:twl4030_rtc");

static struct platform_driver twl4030rtc_driver = {
	.probe		= twl4030_rtc_probe,
	.remove		= __devexit_p(twl4030_rtc_remove),
	.shutdown	= twl4030_rtc_shutdown,
	.suspend	= twl4030_rtc_suspend,
	.resume		= twl4030_rtc_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "twl4030_rtc",
	},
};

static int __init twl4030_rtc_init(void)
{
	return platform_driver_register(&twl4030rtc_driver);
}
module_init(twl4030_rtc_init);

static void __exit twl4030_rtc_exit(void)
{
	platform_driver_unregister(&twl4030rtc_driver);
}
module_exit(twl4030_rtc_exit);

MODULE_AUTHOR("Texas Instruments, MontaVista Software");
MODULE_LICENSE("GPL");
