// SPDX-License-Identifier: GPL-2.0-only
/*
 * Motorola CPCAP PMIC RTC driver
 *
 * Based on cpcap-regulator.c from Motorola Linux kernel tree
 * Copyright (C) 2009 Motorola, Inc.
 *
 * Rewritten for mainline kernel
 *  - use DT
 *  - use regmap
 *  - use standard interrupt framework
 *  - use managed device resources
 *  - remove custom "secure clock daemon" helpers
 *
 * Copyright (C) 2017 Sebastian Reichel <sre@kernel.org>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/mfd/motorola-cpcap.h>
#include <linux/slab.h>
#include <linux/sched.h>

#define SECS_PER_DAY 86400
#define DAY_MASK  0x7FFF
#define TOD1_MASK 0x00FF
#define TOD2_MASK 0x01FF

struct cpcap_time {
	int day;
	int tod1;
	int tod2;
};

struct cpcap_rtc {
	struct regmap *regmap;
	struct rtc_device *rtc_dev;
	u16 vendor;
	int alarm_irq;
	bool alarm_enabled;
	int update_irq;
	bool update_enabled;
};

static void cpcap2rtc_time(struct rtc_time *rtc, struct cpcap_time *cpcap)
{
	unsigned long int tod;
	unsigned long int time;

	tod = (cpcap->tod1 & TOD1_MASK) | ((cpcap->tod2 & TOD2_MASK) << 8);
	time = tod + ((cpcap->day & DAY_MASK) * SECS_PER_DAY);

	rtc_time64_to_tm(time, rtc);
}

static void rtc2cpcap_time(struct cpcap_time *cpcap, struct rtc_time *rtc)
{
	unsigned long time;

	time = rtc_tm_to_time64(rtc);

	cpcap->day = time / SECS_PER_DAY;
	time %= SECS_PER_DAY;
	cpcap->tod2 = (time >> 8) & TOD2_MASK;
	cpcap->tod1 = time & TOD1_MASK;
}

static int cpcap_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct cpcap_rtc *rtc = dev_get_drvdata(dev);

	if (rtc->alarm_enabled == enabled)
		return 0;

	if (enabled)
		enable_irq(rtc->alarm_irq);
	else
		disable_irq(rtc->alarm_irq);

	rtc->alarm_enabled = !!enabled;

	return 0;
}

static int cpcap_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct cpcap_rtc *rtc;
	struct cpcap_time cpcap_tm;
	int temp_tod2;
	int ret;

	rtc = dev_get_drvdata(dev);

	ret = regmap_read(rtc->regmap, CPCAP_REG_TOD2, &temp_tod2);
	ret |= regmap_read(rtc->regmap, CPCAP_REG_DAY, &cpcap_tm.day);
	ret |= regmap_read(rtc->regmap, CPCAP_REG_TOD1, &cpcap_tm.tod1);
	ret |= regmap_read(rtc->regmap, CPCAP_REG_TOD2, &cpcap_tm.tod2);

	if (temp_tod2 > cpcap_tm.tod2)
		ret |= regmap_read(rtc->regmap, CPCAP_REG_DAY, &cpcap_tm.day);

	if (ret) {
		dev_err(dev, "Failed to read time\n");
		return -EIO;
	}

	cpcap2rtc_time(tm, &cpcap_tm);

	return 0;
}

static int cpcap_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct cpcap_rtc *rtc;
	struct cpcap_time cpcap_tm;
	int ret = 0;

	rtc = dev_get_drvdata(dev);

	rtc2cpcap_time(&cpcap_tm, tm);

	if (rtc->alarm_enabled)
		disable_irq(rtc->alarm_irq);
	if (rtc->update_enabled)
		disable_irq(rtc->update_irq);

	if (rtc->vendor == CPCAP_VENDOR_ST) {
		/* The TOD1 and TOD2 registers MUST be written in this order
		 * for the change to properly set.
		 */
		ret |= regmap_update_bits(rtc->regmap, CPCAP_REG_TOD1,
					  TOD1_MASK, cpcap_tm.tod1);
		ret |= regmap_update_bits(rtc->regmap, CPCAP_REG_TOD2,
					  TOD2_MASK, cpcap_tm.tod2);
		ret |= regmap_update_bits(rtc->regmap, CPCAP_REG_DAY,
					  DAY_MASK, cpcap_tm.day);
	} else {
		/* Clearing the upper lower 8 bits of the TOD guarantees that
		 * the upper half of TOD (TOD2) will not increment for 0xFF RTC
		 * ticks (255 seconds).  During this time we can safely write
		 * to DAY, TOD2, then TOD1 (in that order) and expect RTC to be
		 * synchronized to the exact time requested upon the final write
		 * to TOD1.
		 */
		ret |= regmap_update_bits(rtc->regmap, CPCAP_REG_TOD1,
					  TOD1_MASK, 0);
		ret |= regmap_update_bits(rtc->regmap, CPCAP_REG_DAY,
					  DAY_MASK, cpcap_tm.day);
		ret |= regmap_update_bits(rtc->regmap, CPCAP_REG_TOD2,
					  TOD2_MASK, cpcap_tm.tod2);
		ret |= regmap_update_bits(rtc->regmap, CPCAP_REG_TOD1,
					  TOD1_MASK, cpcap_tm.tod1);
	}

	if (rtc->update_enabled)
		enable_irq(rtc->update_irq);
	if (rtc->alarm_enabled)
		enable_irq(rtc->alarm_irq);

	return ret;
}

static int cpcap_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct cpcap_rtc *rtc;
	struct cpcap_time cpcap_tm;
	int ret;

	rtc = dev_get_drvdata(dev);

	alrm->enabled = rtc->alarm_enabled;

	ret = regmap_read(rtc->regmap, CPCAP_REG_DAYA, &cpcap_tm.day);
	ret |= regmap_read(rtc->regmap, CPCAP_REG_TODA2, &cpcap_tm.tod2);
	ret |= regmap_read(rtc->regmap, CPCAP_REG_TODA1, &cpcap_tm.tod1);

	if (ret) {
		dev_err(dev, "Failed to read time\n");
		return -EIO;
	}

	cpcap2rtc_time(&alrm->time, &cpcap_tm);
	return rtc_valid_tm(&alrm->time);
}

static int cpcap_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct cpcap_rtc *rtc;
	struct cpcap_time cpcap_tm;
	int ret;

	rtc = dev_get_drvdata(dev);

	rtc2cpcap_time(&cpcap_tm, &alrm->time);

	if (rtc->alarm_enabled)
		disable_irq(rtc->alarm_irq);

	ret = regmap_update_bits(rtc->regmap, CPCAP_REG_DAYA, DAY_MASK,
				 cpcap_tm.day);
	ret |= regmap_update_bits(rtc->regmap, CPCAP_REG_TODA2, TOD2_MASK,
				  cpcap_tm.tod2);
	ret |= regmap_update_bits(rtc->regmap, CPCAP_REG_TODA1, TOD1_MASK,
				  cpcap_tm.tod1);

	if (!ret) {
		enable_irq(rtc->alarm_irq);
		rtc->alarm_enabled = true;
	}

	return ret;
}

static const struct rtc_class_ops cpcap_rtc_ops = {
	.read_time		= cpcap_rtc_read_time,
	.set_time		= cpcap_rtc_set_time,
	.read_alarm		= cpcap_rtc_read_alarm,
	.set_alarm		= cpcap_rtc_set_alarm,
	.alarm_irq_enable	= cpcap_rtc_alarm_irq_enable,
};

static irqreturn_t cpcap_rtc_alarm_irq(int irq, void *data)
{
	struct cpcap_rtc *rtc = data;

	rtc_update_irq(rtc->rtc_dev, 1, RTC_AF | RTC_IRQF);
	return IRQ_HANDLED;
}

static irqreturn_t cpcap_rtc_update_irq(int irq, void *data)
{
	struct cpcap_rtc *rtc = data;

	rtc_update_irq(rtc->rtc_dev, 1, RTC_UF | RTC_IRQF);
	return IRQ_HANDLED;
}

static int cpcap_rtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cpcap_rtc *rtc;
	int err;

	rtc = devm_kzalloc(dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->regmap = dev_get_regmap(dev->parent, NULL);
	if (!rtc->regmap)
		return -ENODEV;

	platform_set_drvdata(pdev, rtc);
	rtc->rtc_dev = devm_rtc_allocate_device(dev);
	if (IS_ERR(rtc->rtc_dev))
		return PTR_ERR(rtc->rtc_dev);

	rtc->rtc_dev->ops = &cpcap_rtc_ops;
	rtc->rtc_dev->range_max = (1 << 14) * SECS_PER_DAY - 1;

	err = cpcap_get_vendor(dev, rtc->regmap, &rtc->vendor);
	if (err)
		return err;

	rtc->alarm_irq = platform_get_irq(pdev, 0);
	err = devm_request_threaded_irq(dev, rtc->alarm_irq, NULL,
					cpcap_rtc_alarm_irq, IRQF_TRIGGER_NONE,
					"rtc_alarm", rtc);
	if (err) {
		dev_err(dev, "Could not request alarm irq: %d\n", err);
		return err;
	}
	disable_irq(rtc->alarm_irq);

	/* Stock Android uses the 1 Hz interrupt for "secure clock daemon",
	 * which is not supported by the mainline kernel. The mainline kernel
	 * does not use the irq at the moment, but we explicitly request and
	 * disable it, so that its masked and does not wake up the processor
	 * every second.
	 */
	rtc->update_irq = platform_get_irq(pdev, 1);
	err = devm_request_threaded_irq(dev, rtc->update_irq, NULL,
					cpcap_rtc_update_irq, IRQF_TRIGGER_NONE,
					"rtc_1hz", rtc);
	if (err) {
		dev_err(dev, "Could not request update irq: %d\n", err);
		return err;
	}
	disable_irq(rtc->update_irq);

	err = device_init_wakeup(dev, 1);
	if (err) {
		dev_err(dev, "wakeup initialization failed (%d)\n", err);
		/* ignore error and continue without wakeup support */
	}

	return rtc_register_device(rtc->rtc_dev);
}

static const struct of_device_id cpcap_rtc_of_match[] = {
	{ .compatible = "motorola,cpcap-rtc", },
	{},
};
MODULE_DEVICE_TABLE(of, cpcap_rtc_of_match);

static struct platform_driver cpcap_rtc_driver = {
	.probe		= cpcap_rtc_probe,
	.driver		= {
		.name	= "cpcap-rtc",
		.of_match_table = cpcap_rtc_of_match,
	},
};

module_platform_driver(cpcap_rtc_driver);

MODULE_ALIAS("platform:cpcap-rtc");
MODULE_DESCRIPTION("CPCAP RTC driver");
MODULE_AUTHOR("Sebastian Reichel <sre@kernel.org>");
MODULE_LICENSE("GPL");
