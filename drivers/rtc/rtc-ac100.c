/*
 * RTC Driver for X-Powers AC100
 *
 * Copyright (c) 2016 Chen-Yu Tsai
 *
 * Chen-Yu Tsai <wens@csie.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/bcd.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/ac100.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/rtc.h>
#include <linux/types.h>

/* Control register */
#define AC100_RTC_CTRL_24HOUR	BIT(0)

/* RTC */
#define AC100_RTC_SEC_MASK	GENMASK(6, 0)
#define AC100_RTC_MIN_MASK	GENMASK(6, 0)
#define AC100_RTC_HOU_MASK	GENMASK(5, 0)
#define AC100_RTC_WEE_MASK	GENMASK(2, 0)
#define AC100_RTC_DAY_MASK	GENMASK(5, 0)
#define AC100_RTC_MON_MASK	GENMASK(4, 0)
#define AC100_RTC_YEA_MASK	GENMASK(7, 0)
#define AC100_RTC_YEA_LEAP	BIT(15)
#define AC100_RTC_UPD_TRIGGER	BIT(15)

/* Alarm (wall clock) */
#define AC100_ALM_INT_ENABLE	BIT(0)

#define AC100_ALM_SEC_MASK	GENMASK(6, 0)
#define AC100_ALM_MIN_MASK	GENMASK(6, 0)
#define AC100_ALM_HOU_MASK	GENMASK(5, 0)
#define AC100_ALM_WEE_MASK	GENMASK(2, 0)
#define AC100_ALM_DAY_MASK	GENMASK(5, 0)
#define AC100_ALM_MON_MASK	GENMASK(4, 0)
#define AC100_ALM_YEA_MASK	GENMASK(7, 0)
#define AC100_ALM_ENABLE_FLAG	BIT(15)
#define AC100_ALM_UPD_TRIGGER	BIT(15)

/*
 * The year parameter passed to the driver is usually an offset relative to
 * the year 1900. This macro is used to convert this offset to another one
 * relative to the minimum year allowed by the hardware.
 *
 * The year range is 1970 - 2069. This range is selected to match Allwinner's
 * driver.
 */
#define AC100_YEAR_MIN				1970
#define AC100_YEAR_MAX				2069
#define AC100_YEAR_OFF				(AC100_YEAR_MIN - 1900)

struct ac100_rtc_dev {
	struct rtc_device *rtc;
	struct device *dev;
	struct regmap *regmap;
	int irq;
	unsigned long alarm;
};

static int ac100_rtc_get_time(struct device *dev, struct rtc_time *rtc_tm)
{
	struct ac100_rtc_dev *chip = dev_get_drvdata(dev);
	struct regmap *regmap = chip->regmap;
	u16 reg[7];
	int ret;

	ret = regmap_bulk_read(regmap, AC100_RTC_SEC, reg, 7);
	if (ret)
		return ret;

	rtc_tm->tm_sec  = bcd2bin(reg[0] & AC100_RTC_SEC_MASK);
	rtc_tm->tm_min  = bcd2bin(reg[1] & AC100_RTC_MIN_MASK);
	rtc_tm->tm_hour = bcd2bin(reg[2] & AC100_RTC_HOU_MASK);
	rtc_tm->tm_wday = bcd2bin(reg[3] & AC100_RTC_WEE_MASK);
	rtc_tm->tm_mday = bcd2bin(reg[4] & AC100_RTC_DAY_MASK);
	rtc_tm->tm_mon  = bcd2bin(reg[5] & AC100_RTC_MON_MASK) - 1;
	rtc_tm->tm_year = bcd2bin(reg[6] & AC100_RTC_YEA_MASK) +
			  AC100_YEAR_OFF;

	return rtc_valid_tm(rtc_tm);
}

static int ac100_rtc_set_time(struct device *dev, struct rtc_time *rtc_tm)
{
	struct ac100_rtc_dev *chip = dev_get_drvdata(dev);
	struct regmap *regmap = chip->regmap;
	int year;
	u16 reg[8];

	/* our RTC has a limited year range... */
	year = rtc_tm->tm_year - AC100_YEAR_OFF;
	if (year < 0 || year > (AC100_YEAR_MAX - 1900)) {
		dev_err(dev, "rtc only supports year in range %d - %d\n",
			AC100_YEAR_MIN, AC100_YEAR_MAX);
		return -EINVAL;
	}

	/* convert to BCD */
	reg[0] = bin2bcd(rtc_tm->tm_sec)     & AC100_RTC_SEC_MASK;
	reg[1] = bin2bcd(rtc_tm->tm_min)     & AC100_RTC_MIN_MASK;
	reg[2] = bin2bcd(rtc_tm->tm_hour)    & AC100_RTC_HOU_MASK;
	reg[3] = bin2bcd(rtc_tm->tm_wday)    & AC100_RTC_WEE_MASK;
	reg[4] = bin2bcd(rtc_tm->tm_mday)    & AC100_RTC_DAY_MASK;
	reg[5] = bin2bcd(rtc_tm->tm_mon + 1) & AC100_RTC_MON_MASK;
	reg[6] = bin2bcd(year)		     & AC100_RTC_YEA_MASK;
	/* trigger write */
	reg[7] = AC100_RTC_UPD_TRIGGER;

	/* Is it a leap year? */
	if (is_leap_year(year + AC100_YEAR_OFF + 1900))
		reg[6] |= AC100_RTC_YEA_LEAP;

	return regmap_bulk_write(regmap, AC100_RTC_SEC, reg, 8);
}

static int ac100_rtc_alarm_irq_enable(struct device *dev, unsigned int en)
{
	struct ac100_rtc_dev *chip = dev_get_drvdata(dev);
	struct regmap *regmap = chip->regmap;
	unsigned int val;

	val = en ? AC100_ALM_INT_ENABLE : 0;

	return regmap_write(regmap, AC100_ALM_INT_ENA, val);
}

static int ac100_rtc_get_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct ac100_rtc_dev *chip = dev_get_drvdata(dev);
	struct regmap *regmap = chip->regmap;
	struct rtc_time *alrm_tm = &alrm->time;
	u16 reg[7];
	unsigned int val;
	int ret;

	ret = regmap_read(regmap, AC100_ALM_INT_ENA, &val);
	if (ret)
		return ret;

	alrm->enabled = !!(val & AC100_ALM_INT_ENABLE);

	ret = regmap_bulk_read(regmap, AC100_ALM_SEC, reg, 7);
	if (ret)
		return ret;

	alrm_tm->tm_sec  = bcd2bin(reg[0] & AC100_ALM_SEC_MASK);
	alrm_tm->tm_min  = bcd2bin(reg[1] & AC100_ALM_MIN_MASK);
	alrm_tm->tm_hour = bcd2bin(reg[2] & AC100_ALM_HOU_MASK);
	alrm_tm->tm_wday = bcd2bin(reg[3] & AC100_ALM_WEE_MASK);
	alrm_tm->tm_mday = bcd2bin(reg[4] & AC100_ALM_DAY_MASK);
	alrm_tm->tm_mon  = bcd2bin(reg[5] & AC100_ALM_MON_MASK) - 1;
	alrm_tm->tm_year = bcd2bin(reg[6] & AC100_ALM_YEA_MASK) +
			   AC100_YEAR_OFF;

	return 0;
}

static int ac100_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct ac100_rtc_dev *chip = dev_get_drvdata(dev);
	struct regmap *regmap = chip->regmap;
	struct rtc_time *alrm_tm = &alrm->time;
	u16 reg[8];
	int year;
	int ret;

	/* our alarm has a limited year range... */
	year = alrm_tm->tm_year - AC100_YEAR_OFF;
	if (year < 0 || year > (AC100_YEAR_MAX - 1900)) {
		dev_err(dev, "alarm only supports year in range %d - %d\n",
			AC100_YEAR_MIN, AC100_YEAR_MAX);
		return -EINVAL;
	}

	/* convert to BCD */
	reg[0] = (bin2bcd(alrm_tm->tm_sec)  & AC100_ALM_SEC_MASK) |
			AC100_ALM_ENABLE_FLAG;
	reg[1] = (bin2bcd(alrm_tm->tm_min)  & AC100_ALM_MIN_MASK) |
			AC100_ALM_ENABLE_FLAG;
	reg[2] = (bin2bcd(alrm_tm->tm_hour) & AC100_ALM_HOU_MASK) |
			AC100_ALM_ENABLE_FLAG;
	/* Do not enable weekday alarm */
	reg[3] = bin2bcd(alrm_tm->tm_wday) & AC100_ALM_WEE_MASK;
	reg[4] = (bin2bcd(alrm_tm->tm_mday) & AC100_ALM_DAY_MASK) |
			AC100_ALM_ENABLE_FLAG;
	reg[5] = (bin2bcd(alrm_tm->tm_mon + 1)  & AC100_ALM_MON_MASK) |
			AC100_ALM_ENABLE_FLAG;
	reg[6] = (bin2bcd(year) & AC100_ALM_YEA_MASK) |
			AC100_ALM_ENABLE_FLAG;
	/* trigger write */
	reg[7] = AC100_ALM_UPD_TRIGGER;

	ret = regmap_bulk_write(regmap, AC100_ALM_SEC, reg, 8);
	if (ret)
		return ret;

	return ac100_rtc_alarm_irq_enable(dev, alrm->enabled);
}

static irqreturn_t ac100_rtc_irq(int irq, void *data)
{
	struct ac100_rtc_dev *chip = data;
	struct regmap *regmap = chip->regmap;
	unsigned int val = 0;
	int ret;

	mutex_lock(&chip->rtc->ops_lock);

	/* read status */
	ret = regmap_read(regmap, AC100_ALM_INT_STA, &val);
	if (ret)
		goto out;

	if (val & AC100_ALM_INT_ENABLE) {
		/* signal rtc framework */
		rtc_update_irq(chip->rtc, 1, RTC_AF | RTC_IRQF);

		/* clear status */
		ret = regmap_write(regmap, AC100_ALM_INT_STA, val);
		if (ret)
			goto out;

		/* disable interrupt */
		ret = ac100_rtc_alarm_irq_enable(chip->dev, 0);
		if (ret)
			goto out;
	}

out:
	mutex_unlock(&chip->rtc->ops_lock);
	return IRQ_HANDLED;
}

static const struct rtc_class_ops ac100_rtc_ops = {
	.read_time	  = ac100_rtc_get_time,
	.set_time	  = ac100_rtc_set_time,
	.read_alarm	  = ac100_rtc_get_alarm,
	.set_alarm	  = ac100_rtc_set_alarm,
	.alarm_irq_enable = ac100_rtc_alarm_irq_enable,
};

static int ac100_rtc_probe(struct platform_device *pdev)
{
	struct ac100_dev *ac100 = dev_get_drvdata(pdev->dev.parent);
	struct ac100_rtc_dev *chip;
	int ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	platform_set_drvdata(pdev, chip);
	chip->dev = &pdev->dev;
	chip->regmap = ac100->regmap;

	chip->irq = platform_get_irq(pdev, 0);
	if (chip->irq < 0) {
		dev_err(&pdev->dev, "No IRQ resource\n");
		return chip->irq;
	}

	ret = devm_request_threaded_irq(&pdev->dev, chip->irq, NULL,
					ac100_rtc_irq,
					IRQF_SHARED | IRQF_ONESHOT,
					dev_name(&pdev->dev), chip);
	if (ret) {
		dev_err(&pdev->dev, "Could not request IRQ\n");
		return ret;
	}

	/* always use 24 hour mode */
	regmap_write_bits(chip->regmap, AC100_RTC_CTRL, AC100_RTC_CTRL_24HOUR,
			  AC100_RTC_CTRL_24HOUR);

	/* disable counter alarm interrupt */
	regmap_write(chip->regmap, AC100_ALM_INT_ENA, 0);

	/* clear counter alarm pending interrupts */
	regmap_write(chip->regmap, AC100_ALM_INT_STA, AC100_ALM_INT_ENABLE);

	chip->rtc = devm_rtc_device_register(&pdev->dev, "rtc-ac100",
					     &ac100_rtc_ops, THIS_MODULE);
	if (IS_ERR(chip->rtc)) {
		dev_err(&pdev->dev, "unable to register device\n");
		return PTR_ERR(chip->rtc);
	}

	dev_info(&pdev->dev, "RTC enabled\n");

	return 0;
}

static const struct of_device_id ac100_rtc_match[] = {
	{ .compatible = "x-powers,ac100-rtc" },
	{ },
};
MODULE_DEVICE_TABLE(of, ac100_rtc_match);

static struct platform_driver ac100_rtc_driver = {
	.probe		= ac100_rtc_probe,
	.driver		= {
		.name		= "ac100-rtc",
		.of_match_table	= of_match_ptr(ac100_rtc_match),
	},
};
module_platform_driver(ac100_rtc_driver);

MODULE_DESCRIPTION("X-Powers AC100 RTC driver");
MODULE_AUTHOR("Chen-Yu Tsai <wens@csie.org>");
MODULE_LICENSE("GPL v2");
