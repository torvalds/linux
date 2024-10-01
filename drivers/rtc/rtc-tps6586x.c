// SPDX-License-Identifier: GPL-2.0-only
/*
 * rtc-tps6586x.c: RTC driver for TI PMIC TPS6586X
 *
 * Copyright (c) 2012, NVIDIA Corporation.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mfd/tps6586x.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/rtc.h>
#include <linux/slab.h>

#define RTC_CTRL			0xc0
#define POR_RESET_N			BIT(7)
#define OSC_SRC_SEL			BIT(6)
#define RTC_ENABLE			BIT(5)	/* enables alarm */
#define RTC_BUF_ENABLE			BIT(4)	/* 32 KHz buffer enable */
#define PRE_BYPASS			BIT(3)	/* 0=1KHz or 1=32KHz updates */
#define CL_SEL_MASK			(BIT(2)|BIT(1))
#define CL_SEL_POS			1
#define RTC_ALARM1_HI			0xc1
#define RTC_COUNT4			0xc6

/* start a PMU RTC access by reading the register prior to the RTC_COUNT4 */
#define RTC_COUNT4_DUMMYREAD		0xc5

/*only 14-bits width in second*/
#define ALM1_VALID_RANGE_IN_SEC		0x3FFF

#define TPS6586X_RTC_CL_SEL_1_5PF	0x0
#define TPS6586X_RTC_CL_SEL_6_5PF	0x1
#define TPS6586X_RTC_CL_SEL_7_5PF	0x2
#define TPS6586X_RTC_CL_SEL_12_5PF	0x3

struct tps6586x_rtc {
	struct device		*dev;
	struct rtc_device	*rtc;
	int			irq;
	bool			irq_en;
};

static inline struct device *to_tps6586x_dev(struct device *dev)
{
	return dev->parent;
}

static int tps6586x_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct device *tps_dev = to_tps6586x_dev(dev);
	unsigned long long ticks = 0;
	time64_t seconds;
	u8 buff[6];
	int ret;
	int i;

	ret = tps6586x_reads(tps_dev, RTC_COUNT4_DUMMYREAD, sizeof(buff), buff);
	if (ret < 0) {
		dev_err(dev, "read counter failed with err %d\n", ret);
		return ret;
	}

	for (i = 1; i < sizeof(buff); i++) {
		ticks <<= 8;
		ticks |= buff[i];
	}

	seconds = ticks >> 10;
	rtc_time64_to_tm(seconds, tm);

	return 0;
}

static int tps6586x_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct device *tps_dev = to_tps6586x_dev(dev);
	unsigned long long ticks;
	time64_t seconds;
	u8 buff[5];
	int ret;

	seconds = rtc_tm_to_time64(tm);

	ticks = (unsigned long long)seconds << 10;
	buff[0] = (ticks >> 32) & 0xff;
	buff[1] = (ticks >> 24) & 0xff;
	buff[2] = (ticks >> 16) & 0xff;
	buff[3] = (ticks >> 8) & 0xff;
	buff[4] = ticks & 0xff;

	/* Disable RTC before changing time */
	ret = tps6586x_clr_bits(tps_dev, RTC_CTRL, RTC_ENABLE);
	if (ret < 0) {
		dev_err(dev, "failed to clear RTC_ENABLE\n");
		return ret;
	}

	ret = tps6586x_writes(tps_dev, RTC_COUNT4, sizeof(buff), buff);
	if (ret < 0) {
		dev_err(dev, "failed to program new time\n");
		return ret;
	}

	/* Enable RTC */
	ret = tps6586x_set_bits(tps_dev, RTC_CTRL, RTC_ENABLE);
	if (ret < 0) {
		dev_err(dev, "failed to set RTC_ENABLE\n");
		return ret;
	}
	return 0;
}

static int tps6586x_rtc_alarm_irq_enable(struct device *dev,
			 unsigned int enabled)
{
	struct tps6586x_rtc *rtc = dev_get_drvdata(dev);

	if (enabled && !rtc->irq_en) {
		enable_irq(rtc->irq);
		rtc->irq_en = true;
	} else if (!enabled && rtc->irq_en)  {
		disable_irq(rtc->irq);
		rtc->irq_en = false;
	}
	return 0;
}

static int tps6586x_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct device *tps_dev = to_tps6586x_dev(dev);
	time64_t seconds;
	unsigned long ticks;
	unsigned long rtc_current_time;
	unsigned long long rticks = 0;
	u8 buff[3];
	u8 rbuff[6];
	int ret;
	int i;

	seconds = rtc_tm_to_time64(&alrm->time);

	ret = tps6586x_rtc_alarm_irq_enable(dev, alrm->enabled);
	if (ret < 0) {
		dev_err(dev, "can't set alarm irq, err %d\n", ret);
		return ret;
	}

	ret = tps6586x_reads(tps_dev, RTC_COUNT4_DUMMYREAD,
			sizeof(rbuff), rbuff);
	if (ret < 0) {
		dev_err(dev, "read counter failed with err %d\n", ret);
		return ret;
	}

	for (i = 1; i < sizeof(rbuff); i++) {
		rticks <<= 8;
		rticks |= rbuff[i];
	}

	rtc_current_time = rticks >> 10;
	if ((seconds - rtc_current_time) > ALM1_VALID_RANGE_IN_SEC)
		seconds = rtc_current_time - 1;

	ticks = (unsigned long long)seconds << 10;
	buff[0] = (ticks >> 16) & 0xff;
	buff[1] = (ticks >> 8) & 0xff;
	buff[2] = ticks & 0xff;

	ret = tps6586x_writes(tps_dev, RTC_ALARM1_HI, sizeof(buff), buff);
	if (ret)
		dev_err(dev, "programming alarm failed with err %d\n", ret);

	return ret;
}

static int tps6586x_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct device *tps_dev = to_tps6586x_dev(dev);
	unsigned long ticks;
	time64_t seconds;
	u8 buff[3];
	int ret;

	ret = tps6586x_reads(tps_dev, RTC_ALARM1_HI, sizeof(buff), buff);
	if (ret) {
		dev_err(dev, "read RTC_ALARM1_HI failed with err %d\n", ret);
		return ret;
	}

	ticks = (buff[0] << 16) | (buff[1] << 8) | buff[2];
	seconds = ticks >> 10;

	rtc_time64_to_tm(seconds, &alrm->time);
	return 0;
}

static const struct rtc_class_ops tps6586x_rtc_ops = {
	.read_time	= tps6586x_rtc_read_time,
	.set_time	= tps6586x_rtc_set_time,
	.set_alarm	= tps6586x_rtc_set_alarm,
	.read_alarm	= tps6586x_rtc_read_alarm,
	.alarm_irq_enable = tps6586x_rtc_alarm_irq_enable,
};

static irqreturn_t tps6586x_rtc_irq(int irq, void *data)
{
	struct tps6586x_rtc *rtc = data;

	rtc_update_irq(rtc->rtc, 1, RTC_IRQF | RTC_AF);
	return IRQ_HANDLED;
}

static int tps6586x_rtc_probe(struct platform_device *pdev)
{
	struct device *tps_dev = to_tps6586x_dev(&pdev->dev);
	struct tps6586x_rtc *rtc;
	int ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->dev = &pdev->dev;
	rtc->irq = platform_get_irq(pdev, 0);

	/* 1 kHz tick mode, enable tick counting */
	ret = tps6586x_update(tps_dev, RTC_CTRL,
		RTC_ENABLE | OSC_SRC_SEL |
		((TPS6586X_RTC_CL_SEL_1_5PF << CL_SEL_POS) & CL_SEL_MASK),
		RTC_ENABLE | OSC_SRC_SEL | PRE_BYPASS | CL_SEL_MASK);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to start counter\n");
		return ret;
	}

	device_init_wakeup(&pdev->dev, 1);

	platform_set_drvdata(pdev, rtc);
	rtc->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc->rtc)) {
		ret = PTR_ERR(rtc->rtc);
		goto fail_rtc_register;
	}

	rtc->rtc->ops = &tps6586x_rtc_ops;
	rtc->rtc->range_max = (1ULL << 30) - 1; /* 30-bit seconds */
	rtc->rtc->start_secs = mktime64(2009, 1, 1, 0, 0, 0);
	rtc->rtc->set_start_time = true;

	irq_set_status_flags(rtc->irq, IRQ_NOAUTOEN);

	ret = devm_request_threaded_irq(&pdev->dev, rtc->irq, NULL,
				tps6586x_rtc_irq,
				IRQF_ONESHOT,
				dev_name(&pdev->dev), rtc);
	if (ret < 0) {
		dev_err(&pdev->dev, "request IRQ(%d) failed with ret %d\n",
				rtc->irq, ret);
		goto fail_rtc_register;
	}

	ret = devm_rtc_register_device(rtc->rtc);
	if (ret)
		goto fail_rtc_register;

	return 0;

fail_rtc_register:
	tps6586x_update(tps_dev, RTC_CTRL, 0,
		RTC_ENABLE | OSC_SRC_SEL | PRE_BYPASS | CL_SEL_MASK);
	return ret;
};

static int tps6586x_rtc_remove(struct platform_device *pdev)
{
	struct device *tps_dev = to_tps6586x_dev(&pdev->dev);

	tps6586x_update(tps_dev, RTC_CTRL, 0,
		RTC_ENABLE | OSC_SRC_SEL | PRE_BYPASS | CL_SEL_MASK);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tps6586x_rtc_suspend(struct device *dev)
{
	struct tps6586x_rtc *rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(rtc->irq);
	return 0;
}

static int tps6586x_rtc_resume(struct device *dev)
{
	struct tps6586x_rtc *rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(rtc->irq);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(tps6586x_pm_ops, tps6586x_rtc_suspend,
			tps6586x_rtc_resume);

static struct platform_driver tps6586x_rtc_driver = {
	.driver	= {
		.name	= "tps6586x-rtc",
		.pm	= &tps6586x_pm_ops,
	},
	.probe	= tps6586x_rtc_probe,
	.remove	= tps6586x_rtc_remove,
};
module_platform_driver(tps6586x_rtc_driver);

MODULE_ALIAS("platform:tps6586x-rtc");
MODULE_DESCRIPTION("TI TPS6586x RTC driver");
MODULE_AUTHOR("Laxman dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
