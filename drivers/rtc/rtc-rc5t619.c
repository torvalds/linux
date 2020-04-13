// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/rtc/rtc-rc5t619.c
 *
 * Real time clock driver for RICOH RC5T619 power management chip.
 *
 * Copyright (C) 2019 Andreas Kemnade
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mfd/rn5t618.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/irqdomain.h>

struct rc5t619_rtc {
	int			irq;
	struct rtc_device	*rtc;
	struct rn5t618 *rn5t618;
};

#define CTRL1_ALARM_ENABLED 0x40
#define CTRL1_24HR 0x20
#define CTRL1_PERIODIC_MASK 0xf

#define CTRL2_PON 0x10
#define CTRL2_ALARM_STATUS 0x80
#define CTRL2_CTFG 0x4
#define CTRL2_CTC 0x1

#define MONTH_CENTFLAG 0x80
#define HOUR_PMFLAG 0x20
#define MDAY_DAL_EXT 0x80

static uint8_t rtc5t619_12hour_bcd2bin(uint8_t hour)
{
	if (hour & HOUR_PMFLAG) {
		hour = bcd2bin(hour & ~HOUR_PMFLAG);
		return hour == 12 ? 12 : 12 + hour;
	}

	hour = bcd2bin(hour);
	return hour == 12 ? 0 : hour;
}

static uint8_t rtc5t619_12hour_bin2bcd(uint8_t hour)
{
	if (!hour)
		return 0x12;

	if (hour < 12)
		return bin2bcd(hour);

	if (hour == 12)
		return 0x12 | HOUR_PMFLAG;

	return bin2bcd(hour - 12) | HOUR_PMFLAG;
}

static int rc5t619_rtc_periodic_disable(struct device *dev)
{
	struct rc5t619_rtc *rtc = dev_get_drvdata(dev);
	int err;

	/* disable function */
	err = regmap_update_bits(rtc->rn5t618->regmap,
				 RN5T618_RTC_CTRL1, CTRL1_PERIODIC_MASK, 0);
	if (err < 0)
		return err;

	/* clear alarm flag and CTFG */
	err = regmap_update_bits(rtc->rn5t618->regmap, RN5T618_RTC_CTRL2,
				 CTRL2_ALARM_STATUS | CTRL2_CTFG | CTRL2_CTC,
				 0);
	if (err < 0)
		return err;

	return 0;
}

/* things to be done once after power on */
static int rc5t619_rtc_pon_setup(struct device *dev)
{
	struct rc5t619_rtc *rtc = dev_get_drvdata(dev);
	int err;
	unsigned int reg_data;

	err = regmap_read(rtc->rn5t618->regmap, RN5T618_RTC_CTRL2, &reg_data);
	if (err < 0)
		return err;

	/* clear VDET PON */
	reg_data &= ~(CTRL2_PON | CTRL2_CTC | 0x4a);	/* 0101-1011 */
	reg_data |= 0x20;	/* 0010-0000 */
	err = regmap_write(rtc->rn5t618->regmap, RN5T618_RTC_CTRL2, reg_data);
	if (err < 0)
		return err;

	/* clearing RTC Adjust register */
	err = regmap_write(rtc->rn5t618->regmap, RN5T618_RTC_ADJUST, 0);
	if (err)
		return err;

	return regmap_update_bits(rtc->rn5t618->regmap,
					RN5T618_RTC_CTRL1,
					CTRL1_24HR, CTRL1_24HR);
}

static int rc5t619_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rc5t619_rtc *rtc = dev_get_drvdata(dev);
	u8 buff[7];
	int err;
	int cent_flag;
	unsigned int ctrl1;
	unsigned int ctrl2;

	err = regmap_read(rtc->rn5t618->regmap, RN5T618_RTC_CTRL2, &ctrl2);
	if (err < 0)
		return err;

	if (ctrl2 & CTRL2_PON)
		return -EINVAL;

	err = regmap_read(rtc->rn5t618->regmap, RN5T618_RTC_CTRL1, &ctrl1);
	if (err < 0)
		return err;

	err = regmap_bulk_read(rtc->rn5t618->regmap, RN5T618_RTC_SECONDS,
			       buff, sizeof(buff));
	if (err < 0)
		return err;

	if (buff[5] & MONTH_CENTFLAG)
		cent_flag = 1;
	else
		cent_flag = 0;

	tm->tm_sec  = bcd2bin(buff[0]);
	tm->tm_min  = bcd2bin(buff[1]);

	if (ctrl1 & CTRL1_24HR)
		tm->tm_hour = bcd2bin(buff[2]);
	else
		tm->tm_hour = rtc5t619_12hour_bcd2bin(buff[2]);

	tm->tm_wday = bcd2bin(buff[3]);
	tm->tm_mday = bcd2bin(buff[4]);
	tm->tm_mon  = bcd2bin(buff[5] & 0x1f) - 1; /* back to system 0-11 */
	tm->tm_year = bcd2bin(buff[6]) + 100 * cent_flag;

	return 0;
}

static int rc5t619_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rc5t619_rtc *rtc = dev_get_drvdata(dev);
	u8 buff[7];
	int err;
	int cent_flag;
	unsigned int ctrl1;
	unsigned int ctrl2;

	err = regmap_read(rtc->rn5t618->regmap, RN5T618_RTC_CTRL2, &ctrl2);
	if (err < 0)
		return err;

	if (ctrl2 & CTRL2_PON)
		rc5t619_rtc_pon_setup(dev);

	err = regmap_read(rtc->rn5t618->regmap, RN5T618_RTC_CTRL1, &ctrl1);
	if (err < 0)
		return err;

	if (tm->tm_year >= 100)
		cent_flag = 1;
	else
		cent_flag = 0;

	buff[0] = bin2bcd(tm->tm_sec);
	buff[1] = bin2bcd(tm->tm_min);

	if (ctrl1 & CTRL1_24HR)
		buff[2] = bin2bcd(tm->tm_hour);
	else
		buff[2] = rtc5t619_12hour_bin2bcd(tm->tm_hour);

	buff[3] = bin2bcd(tm->tm_wday);
	buff[4] = bin2bcd(tm->tm_mday);
	buff[5] = bin2bcd(tm->tm_mon + 1);	/* system set 0-11 */
	buff[6] = bin2bcd(tm->tm_year - cent_flag * 100);

	if (cent_flag)
		buff[5] |= MONTH_CENTFLAG;

	err = regmap_bulk_write(rtc->rn5t618->regmap, RN5T618_RTC_SECONDS,
				buff, sizeof(buff));
	if (err < 0) {
		dev_err(dev, "failed to program new time: %d\n", err);
		return err;
	}

	return 0;
}

/* 0-disable, 1-enable */
static int rc5t619_rtc_alarm_enable(struct device *dev, unsigned int enabled)
{
	struct rc5t619_rtc *rtc = dev_get_drvdata(dev);

	return regmap_update_bits(rtc->rn5t618->regmap,
			RN5T618_RTC_CTRL1,
			CTRL1_ALARM_ENABLED,
			enabled ? CTRL1_ALARM_ENABLED : 0);
}

static int rc5t619_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rc5t619_rtc *rtc = dev_get_drvdata(dev);
	u8 buff[6];
	unsigned int buff_cent;
	int err;
	int cent_flag;
	unsigned int ctrl1;

	err = regmap_read(rtc->rn5t618->regmap, RN5T618_RTC_CTRL1, &ctrl1);
	if (err)
		return err;

	err = regmap_read(rtc->rn5t618->regmap, RN5T618_RTC_MONTH, &buff_cent);
	if (err < 0) {
		dev_err(dev, "failed to read time: %d\n", err);
		return err;
	}

	if (buff_cent & MONTH_CENTFLAG)
		cent_flag = 1;
	else
		cent_flag = 0;

	err = regmap_bulk_read(rtc->rn5t618->regmap, RN5T618_RTC_ALARM_Y_SEC,
			       buff, sizeof(buff));
	if (err)
		return err;

	buff[3] = buff[3] & 0x3f;

	alrm->time.tm_sec  = bcd2bin(buff[0]);
	alrm->time.tm_min  = bcd2bin(buff[1]);

	if (ctrl1 & CTRL1_24HR)
		alrm->time.tm_hour = bcd2bin(buff[2]);
	else
		alrm->time.tm_hour = rtc5t619_12hour_bcd2bin(buff[2]);

	alrm->time.tm_mday = bcd2bin(buff[3]);
	alrm->time.tm_mon  = bcd2bin(buff[4]) - 1;
	alrm->time.tm_year = bcd2bin(buff[5]) + 100 * cent_flag;
	alrm->enabled = !!(ctrl1 & CTRL1_ALARM_ENABLED);
	dev_dbg(dev, "read alarm: %ptR\n", &alrm->time);

	return 0;
}

static int rc5t619_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rc5t619_rtc *rtc = dev_get_drvdata(dev);
	u8 buff[6];
	int err;
	int cent_flag;
	unsigned int ctrl1;

	err = regmap_read(rtc->rn5t618->regmap, RN5T618_RTC_CTRL1, &ctrl1);
	if (err)
		return err;

	err = rc5t619_rtc_alarm_enable(dev, 0);
	if (err < 0)
		return err;

	if (rtc->irq == -1)
		return -EINVAL;

	if (alrm->enabled == 0)
		return 0;

	if (alrm->time.tm_year >= 100)
		cent_flag = 1;
	else
		cent_flag = 0;

	alrm->time.tm_mon += 1;
	buff[0] = bin2bcd(alrm->time.tm_sec);
	buff[1] = bin2bcd(alrm->time.tm_min);

	if (ctrl1 & CTRL1_24HR)
		buff[2] = bin2bcd(alrm->time.tm_hour);
	else
		buff[2] = rtc5t619_12hour_bin2bcd(alrm->time.tm_hour);

	buff[3] = bin2bcd(alrm->time.tm_mday);
	buff[4] = bin2bcd(alrm->time.tm_mon);
	buff[5] = bin2bcd(alrm->time.tm_year - 100 * cent_flag);
	buff[3] |= MDAY_DAL_EXT;

	err = regmap_bulk_write(rtc->rn5t618->regmap, RN5T618_RTC_ALARM_Y_SEC,
				buff, sizeof(buff));
	if (err < 0)
		return err;

	return rc5t619_rtc_alarm_enable(dev, alrm->enabled);
}

static const struct rtc_class_ops rc5t619_rtc_ops = {
	.read_time	= rc5t619_rtc_read_time,
	.set_time	= rc5t619_rtc_set_time,
	.set_alarm	= rc5t619_rtc_set_alarm,
	.read_alarm	= rc5t619_rtc_read_alarm,
	.alarm_irq_enable = rc5t619_rtc_alarm_enable,
};

static int rc5t619_rtc_alarm_flag_clr(struct device *dev)
{
	struct rc5t619_rtc *rtc = dev_get_drvdata(dev);

	/* clear alarm-D status bits.*/
	return regmap_update_bits(rtc->rn5t618->regmap,
				RN5T618_RTC_CTRL2,
				CTRL2_ALARM_STATUS | CTRL2_CTC, 0);
}

static irqreturn_t rc5t619_rtc_irq(int irq, void *data)
{
	struct device *dev = data;
	struct rc5t619_rtc *rtc = dev_get_drvdata(dev);

	rc5t619_rtc_alarm_flag_clr(dev);

	rtc_update_irq(rtc->rtc, 1, RTC_IRQF | RTC_AF);
	return IRQ_HANDLED;
}

static int rc5t619_rtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rn5t618 *rn5t618 = dev_get_drvdata(pdev->dev.parent);
	struct rc5t619_rtc *rtc;
	unsigned int ctrl2;
	int err;

	rtc = devm_kzalloc(dev, sizeof(*rtc), GFP_KERNEL);
	if (IS_ERR(rtc)) {
		err = PTR_ERR(rtc);
		return -ENOMEM;
	}

	rtc->rn5t618 = rn5t618;

	dev_set_drvdata(dev, rtc);
	rtc->irq = -1;

	if (rn5t618->irq_data)
		rtc->irq = regmap_irq_get_virq(rn5t618->irq_data,
					       RN5T618_IRQ_RTC);

	if (rtc->irq  < 0)
		rtc->irq = -1;

	err = regmap_read(rtc->rn5t618->regmap, RN5T618_RTC_CTRL2, &ctrl2);
	if (err < 0)
		return err;

	/* disable rtc periodic function */
	err = rc5t619_rtc_periodic_disable(&pdev->dev);
	if (err)
		return err;

	if (ctrl2 & CTRL2_PON) {
		err = rc5t619_rtc_alarm_flag_clr(&pdev->dev);
		if (err)
			return err;
	}

	rtc->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc->rtc)) {
		err = PTR_ERR(rtc->rtc);
		dev_err(dev, "RTC device register: err %d\n", err);
		return err;
	}

	rtc->rtc->ops = &rc5t619_rtc_ops;
	rtc->rtc->range_min = RTC_TIMESTAMP_BEGIN_1900;
	rtc->rtc->range_max = RTC_TIMESTAMP_END_2099;

	/* set interrupt and enable it */
	if (rtc->irq != -1) {
		err = devm_request_threaded_irq(&pdev->dev, rtc->irq, NULL,
						rc5t619_rtc_irq,
						IRQF_ONESHOT,
						"rtc-rc5t619",
						&pdev->dev);
		if (err < 0) {
			dev_err(&pdev->dev, "request IRQ:%d fail\n", rtc->irq);
			rtc->irq = -1;

			err = rc5t619_rtc_alarm_enable(&pdev->dev, 0);
			if (err)
				return err;

		} else {
			/* enable wake */
			device_init_wakeup(&pdev->dev, 1);
			enable_irq_wake(rtc->irq);
		}
	} else {
		/* system don't want to using alarm interrupt, so close it */
		err = rc5t619_rtc_alarm_enable(&pdev->dev, 0);
		if (err)
			return err;

		dev_warn(&pdev->dev, "rc5t619 interrupt is disabled\n");
	}

	return rtc_register_device(rtc->rtc);
}

static struct platform_driver rc5t619_rtc_driver = {
	.driver	= {
		.name	= "rc5t619-rtc",
	},
	.probe	= rc5t619_rtc_probe,
};

module_platform_driver(rc5t619_rtc_driver);
MODULE_ALIAS("platform:rc5t619-rtc");
MODULE_DESCRIPTION("RICOH RC5T619 RTC driver");
MODULE_LICENSE("GPL");
