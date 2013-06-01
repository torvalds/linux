/*
 * drivers/rtc/rtc-ricoh619.c
 *
 * Real time clock driver for RICOH RC5T619 power management chip.
 *
 * Copyright (C) 2012-2013 RICOH COMPANY,LTD
 *
 * Based on code
 *  Copyright (C) 2011 NVIDIA Corporation  
 *
 * this program is free software; you can redistribute it and/or modify
 * it under the terms of the gnu general public license as published by
 * the free software foundation; either version 2 of the license, or
 * (at your option) any later version.
 *
 * this program is distributed in the hope that it will be useful, but without
 * any warranty; without even the implied warranty of merchantability or
 * fitness for a particular purpose.  see the gnu general public license for
 * more details.
 *
 * you should have received a copy of the gnu general public license
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.  
 *
 */

/* #define debug		1 */
/* #define verbose_debug	1 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/ricoh619.h>
#include <linux/rtc/rtc-ricoh619.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/slab.h>

struct ricoh619_rtc {
	unsigned long		epoch_start;
	int			irq;
	struct rtc_device	*rtc;
	bool			irq_en;
};

static int ricoh619_read_regs(struct device *dev, int reg, int len,
	uint8_t *val)
{
	int ret;

	ret = ricoh619_bulk_reads(dev->parent, reg, len, val);
	if (ret < 0) {
		dev_err(dev->parent, "\n %s failed reading from 0x%02x\n",
			__func__, reg);
		WARN_ON(1);
	}
	return ret;
}

static int ricoh619_write_regs(struct device *dev, int reg, int len,
	uint8_t *val)
{
	int ret;
	ret = ricoh619_bulk_writes(dev->parent, reg, len, val);
	if (ret < 0) {
		dev_err(dev->parent, "\n %s failed writing\n", __func__);
		WARN_ON(1);
	}

	return ret;
}

static int ricoh619_rtc_valid_tm(struct device *dev, struct rtc_time *tm)
{
	if (tm->tm_year >= (rtc_year_offset + 99)
		|| tm->tm_mon > 12
		|| tm->tm_mday < 1
		|| tm->tm_mday > rtc_month_days(tm->tm_mon,
			tm->tm_year + os_ref_year)
		|| tm->tm_hour >= 24
		|| tm->tm_min >= 60
		|| tm->tm_sec >= 60) {
		dev_err(dev->parent, "\n returning error due to time"
		"%d/%d/%d %d:%d:%d", tm->tm_mon, tm->tm_mday,
		tm->tm_year, tm->tm_hour, tm->tm_min, tm->tm_sec);
		return -EINVAL;
	}
	return 0;
}

static u8 dec2bcd(u8 dec)
{
	return ((dec/10)<<4)+(dec%10);
}

static u8 bcd2dec(u8 bcd)
{
	return (bcd >> 4)*10+(bcd & 0xf);
}

static void convert_bcd_to_decimal(u8 *buf, u8 len)
{
	int i = 0;
	for (i = 0; i < len; i++)
		buf[i] = bcd2dec(buf[i]);
}

static void convert_decimal_to_bcd(u8 *buf, u8 len)
{
	int i = 0;
	for (i = 0; i < len; i++)
		buf[i] = dec2bcd(buf[i]);
}

static void print_time(struct device *dev, struct rtc_time *tm)
{
	dev_info(dev, "rtc-time : %d/%d/%d %d:%d\n",
		(tm->tm_mon + 1), tm->tm_mday, (tm->tm_year + os_ref_year),
		tm->tm_hour, tm->tm_min);
}

static int ricoh619_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	u8 buff[7];
	int err;
	err = ricoh619_read_regs(dev, rtc_seconds_reg, sizeof(buff), buff);
	if (err < 0) {
		dev_err(dev, "\n %s :: failed to read time\n", __FILE__);
		return err;
	}
	convert_bcd_to_decimal(buff, sizeof(buff));
	tm->tm_sec  = buff[0];
	tm->tm_min  = buff[1];
	tm->tm_hour = buff[2];
	tm->tm_wday = buff[3];
	tm->tm_mday = buff[4];
	tm->tm_mon  = buff[5] - 1;
	tm->tm_year = buff[6] + rtc_year_offset;
//	print_time(dev, tm);
	return ricoh619_rtc_valid_tm(dev, tm);
}

static int ricoh619_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	u8 buff[7];
	int err;

//	print_time(dev, tm);
	buff[0] = tm->tm_sec;
	buff[1] = tm->tm_min;
	buff[2] = tm->tm_hour;
	buff[3] = tm->tm_wday;
	buff[4] = tm->tm_mday;
	buff[5] = tm->tm_mon + 1;
	buff[6] = tm->tm_year - rtc_year_offset;

	convert_decimal_to_bcd(buff, sizeof(buff));
	err = ricoh619_write_regs(dev, rtc_seconds_reg, sizeof(buff), buff);
	if (err < 0) {
		dev_err(dev->parent, "\n failed to program new time\n");
		return err;
	}

	return 0;
}
static int ricoh619_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm);

static int ricoh619_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct ricoh619_rtc *rtc = dev_get_drvdata(dev);
	unsigned long seconds;
	u8 buff[6];
	int err;
	struct rtc_time tm;

	if (rtc->irq == -1)
		return -EIO;

	rtc_tm_to_time(&alrm->time, &seconds);
	err = ricoh619_rtc_read_time(dev, &tm);
	if (err) {
		dev_err(dev, "\n failed to read time\n");
		return err;
	}
	rtc_tm_to_time(&tm, &rtc->epoch_start);

	dev_info(dev->parent, "\n setting alarm to requested time::\n");
//	print_time(dev->parent, &alrm->time);

	if (WARN_ON(alrm->enabled && (seconds < rtc->epoch_start))) {
		dev_err(dev->parent, "\n can't set alarm to requested time\n");
		return -EINVAL;
	}

	if (alrm->enabled && !rtc->irq_en)
		rtc->irq_en = true;
	else if (!alrm->enabled && rtc->irq_en)
		rtc->irq_en = false;

	buff[0] = alrm->time.tm_sec;
	buff[1] = alrm->time.tm_min;
	buff[2] = alrm->time.tm_hour;
	buff[3] = alrm->time.tm_mday;
	buff[4] = alrm->time.tm_mon + 1;
	buff[5] = alrm->time.tm_year - rtc_year_offset;
	convert_decimal_to_bcd(buff, sizeof(buff));
	buff[3] |= 0x80;	/* set DAL_EXT */
	err = ricoh619_write_regs(dev, rtc_alarm_y_sec, sizeof(buff), buff);
	if (err) {
		dev_err(dev->parent, "\n unable to set alarm\n");
		return -EBUSY;
	}

	err = ricoh619_read_regs(dev, rtc_ctrl2, 1, buff);
	if (err) {
		dev_err(dev->parent, "unable to read rtc_ctrl2 reg\n");
		return -EBUSY;
	}

	buff[1] = buff[0] & ~0x81; /* to clear alarm-D flag, and set adjustment parameter */
	buff[0] = 0x60; /* to enable alarm_d and 24-hour format */
	err = ricoh619_write_regs(dev, rtc_ctrl1, 2, buff);
	if (err) {
		dev_err(dev, "failed programming rtc ctrl regs\n");
		return -EBUSY;
	}
return err;
}

static int ricoh619_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	u8 buff[6];
	int err;

	err = ricoh619_read_regs(dev, rtc_alarm_y_sec, sizeof(buff), buff);
	if (err)
		return err;
	buff[3] &= ~0x80;	/* clear DAL_EXT */
	convert_bcd_to_decimal(buff, sizeof(buff));

	alrm->time.tm_sec  = buff[0];
	alrm->time.tm_min  = buff[1];
	alrm->time.tm_hour = buff[2];
	alrm->time.tm_mday = buff[3];
	alrm->time.tm_mon  = buff[4] - 1;
	alrm->time.tm_year = buff[5] + rtc_year_offset;

//	dev_info(dev->parent, "\n getting alarm time::\n");
//	print_time(dev, &alrm->time);

	return 0;
}

static const struct rtc_class_ops ricoh619_rtc_ops = {
	.read_time	= ricoh619_rtc_read_time,
	.set_time	= ricoh619_rtc_set_time,
	.set_alarm	= ricoh619_rtc_set_alarm,
	.read_alarm	= ricoh619_rtc_read_alarm,
};

static irqreturn_t ricoh619_rtc_irq(int irq, void *data)
{
	struct device *dev = data;
	struct ricoh619_rtc *rtc = dev_get_drvdata(dev);
	u8 reg;
	int err;

	/* clear alarm-D status bits.*/
	err = ricoh619_read_regs(dev, rtc_ctrl2, 1, &reg);
	if (err)
		dev_err(dev->parent, "unable to read rtc_ctrl2 reg\n");

	/* to clear alarm-D flag, and set adjustment parameter */
	reg &= ~0x81;
	err = ricoh619_write_regs(dev, rtc_ctrl2, 1, &reg);
	if (err)
		dev_err(dev->parent, "unable to program rtc_status reg\n");

	rtc_update_irq(rtc->rtc, 1, RTC_IRQF | RTC_AF);
	return IRQ_HANDLED;
}

static int __devinit ricoh619_rtc_probe(struct platform_device *pdev)
{
	struct ricoh619_rtc_platform_data *pdata = pdev->dev.platform_data;
	struct ricoh619_rtc *rtc;
	struct rtc_time tm;
	int err;
	u8 reg;
	rtc = kzalloc(sizeof(*rtc), GFP_KERNEL);
//	printk("%s,line=%d\n", __func__,__LINE__);	

	if (!rtc)
		return -ENOMEM;

	rtc->irq = -1;

	if (!pdata) {
		dev_err(&pdev->dev, "no platform_data specified\n");
		return -EINVAL;
	}

	if (pdata->irq < 0)
		dev_err(&pdev->dev, "\n no irq specified, wakeup is disabled\n");

	dev_set_drvdata(&pdev->dev, rtc);
	device_init_wakeup(&pdev->dev, 1);
	rtc->rtc = rtc_device_register(pdev->name, &pdev->dev,
				       &ricoh619_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc->rtc)) {
		err = PTR_ERR(rtc->rtc);
		goto fail;
	}

	reg = 0x60; /* to enable alarm_d and 24-hour format */
	err = ricoh619_write_regs(&pdev->dev, rtc_ctrl1, 1, &reg);
	if (err) {
		dev_err(&pdev->dev, "failed rtc setup\n");
		return -EBUSY;
	}

	reg =  0; /* clearing RTC Adjust register */
	err = ricoh619_write_regs(&pdev->dev, rtc_adjust, 1, &reg);
	if (err) {
		dev_err(&pdev->dev, "unable to program rtc_adjust reg\n");
		return -EBUSY;
	}
	/* Set default time-1970.1.1-0h:0m:0s if PON is on */
	err = ricoh619_read_regs(&pdev->dev, rtc_ctrl2, 1, &reg);
	if (err) {
		dev_err(&pdev->dev, "\n failed to read rtc ctl2 reg\n");
		return -EBUSY;
	}
	if (reg&0x10) {
		printk("%s,PON=1 -- CTRL2=%x\n", __func__, reg);
		tm.tm_sec  = 0;
		tm.tm_min  = 0;
		tm.tm_hour = 0;
		tm.tm_wday = 4;
		tm.tm_mday = 1;
		tm.tm_mon  = 0;
		tm.tm_year = 0x70;
		/* VDET & PON = 0, others are not changed */
		reg &= ~0x50;
		err = ricoh619_write_regs(&pdev->dev, rtc_ctrl2, 1, &reg);
		if (err) {
			dev_err(&pdev->dev, "\n failed to write rtc ctl2 reg\n");
			return -EBUSY;
		}
		
	} else {
		err = ricoh619_rtc_read_time(&pdev->dev, &tm);
		if (err) {
			dev_err(&pdev->dev, "\n failed to read time\n");
			return err;
		}
	}
	if (ricoh619_rtc_valid_tm(&pdev->dev, &tm)) {
		if (pdata->time.tm_year < 2000 || pdata->time.tm_year > 2100) {
			memset(&pdata->time, 0, sizeof(pdata->time));
			pdata->time.tm_year = rtc_year_offset;
			pdata->time.tm_mday = 1;
		} else
		pdata->time.tm_year -= os_ref_year;
		err = ricoh619_rtc_set_time(&pdev->dev, &pdata->time);
		if (err) {
			dev_err(&pdev->dev, "\n failed to set time\n");
			return err;
		}
	}
	if (pdata && (pdata->irq >= 0)) {
		rtc->irq = pdata->irq + RICOH619_IRQ_DALE;
		err = request_threaded_irq(rtc->irq, NULL, ricoh619_rtc_irq,
					IRQF_ONESHOT, "rtc_ricoh619",
					&pdev->dev);
		if (err) {
			dev_err(&pdev->dev, "request IRQ:%d fail\n", rtc->irq);
			rtc->irq = -1;
		} else {
			device_init_wakeup(&pdev->dev, 1);
			enable_irq_wake(rtc->irq);
		}
	}
	return 0;

fail:
	if (!IS_ERR_OR_NULL(rtc->rtc))
		rtc_device_unregister(rtc->rtc);
	kfree(rtc);
	return err;
}

static int __devexit ricoh619_rtc_remove(struct platform_device *pdev)
{
	struct ricoh619_rtc *rtc = dev_get_drvdata(&pdev->dev);

	if (rtc->irq != -1)
		free_irq(rtc->irq, rtc);
	rtc_device_unregister(rtc->rtc);
	kfree(rtc);
	return 0;
}

static struct platform_driver ricoh619_rtc_driver = {
	.driver	= {
		.name	= "rtc_ricoh619",
		.owner	= THIS_MODULE,
	},
	.probe	= ricoh619_rtc_probe,
	.remove	= __devexit_p(ricoh619_rtc_remove),
};

static int __init ricoh619_rtc_init(void)
{
	return platform_driver_register(&ricoh619_rtc_driver);
}
subsys_initcall_sync(ricoh619_rtc_init);

static void __exit ricoh619_rtc_exit(void)
{
	platform_driver_unregister(&ricoh619_rtc_driver);
}
module_exit(ricoh619_rtc_exit);

MODULE_DESCRIPTION("RICOH RICOH619 RTC driver");
MODULE_ALIAS("platform:rtc_ricoh619");
MODULE_AUTHOR("zhangqing <zhangqing@rock-chips.com>");
MODULE_LICENSE("GPL");

