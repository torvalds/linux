// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Andes ATCRTC100 real time clock.
 *
 * Copyright (C) 2025 Andes Technology Corporation
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>
#include <linux/rtc.h>
#include <linux/workqueue.h>

/* Register Offsets */
#define RTC_ID		0x00	/* ID and Revision Register */
#define RTC_RSV		0x04	/* Reserved Register */
#define RTC_CNT		0x10	/* Counter Register */
#define RTC_ALM		0x14	/* Alarm Register */
#define RTC_CR		0x18	/* Control Register */
#define RTC_STA		0x1C	/* Status Register */
#define RTC_TRIM	0x20	/* Digital Trimming Register */

/* RTC_ID Register */
#define ID_MSK		GENMASK(31, 8)
#define ID_ATCRTC100	0x030110

/* RTC_CNT and RTC_ALM Register Fields */
#define SEC_MSK		GENMASK(5, 0)
#define MIN_MSK		GENMASK(11, 6)
#define HOUR_MSK	GENMASK(16, 12)
#define DAY_MSK		GENMASK(31, 17)
#define RTC_SEC_GET(x)	FIELD_GET(SEC_MSK, x)
#define RTC_MIN_GET(x)	FIELD_GET(MIN_MSK, x)
#define RTC_HOUR_GET(x)	FIELD_GET(HOUR_MSK, x)
#define RTC_DAY_GET(x)	FIELD_GET(DAY_MSK, x)
#define RTC_SEC_SET(x)	FIELD_PREP(SEC_MSK, x)
#define RTC_MIN_SET(x)	FIELD_PREP(MIN_MSK, x)
#define RTC_HOUR_SET(x)	FIELD_PREP(HOUR_MSK, x)
#define RTC_DAY_SET(x)	FIELD_PREP(DAY_MSK, x)

/* RTC_CR Register Bits */
#define RTC_EN		BIT(0)	/* RTC Enable */
#define ALARM_WAKEUP	BIT(1)	/* Alarm Wakeup Enable */
#define ALARM_INT	BIT(2)	/* Alarm Interrupt Enable */
#define DAY_INT		BIT(3)	/* Day Interrupt Enable */
#define HOUR_INT	BIT(4)	/* Hour Interrupt Enable */
#define MIN_INT		BIT(5)	/* Minute Interrupt Enable */
#define SEC_INT		BIT(6)	/* Second Periodic Interrupt Enable */
#define HSEC_INT	BIT(7)	/* Half-Second Periodic Interrupt Enable */

/* RTC_STA Register Bits */
#define WRITE_DONE	BIT(16)	/* Register write completion status */

/* Time conversion macro */
#define ATCRTC_TIME_TO_SEC(D, H, M, S)	\
	((time64_t)(D) * 86400 + (H) * 3600 + (M) * 60 + (S))

/* Timeout for waiting for the write_done bit */
#define ATCRTC_TIMEOUT_US		1000000
#define ATCRTC_TIMEOUT_USLEEP_MIN	20
#define ATCRTC_TIMEOUT_USLEEP_MAX	30

struct atcrtc_dev {
	struct rtc_device	*rtc_dev;
	struct regmap		*regmap;
	struct work_struct	rtc_work;
	unsigned int		alarm_irq;
	bool			alarm_en;
};

static const struct regmap_config atcrtc_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = RTC_TRIM,
	.cache_type = REGCACHE_NONE,
};

/**
 * atcrtc_check_write_done - Wait for RTC registers to be synchronized.
 * @rtc: Pointer to the atcrtc_dev structure.
 *
 * The WriteDone bit in the status register indicates the synchronization
 * progress of RTC register updates. This bit is cleared to zero whenever
 * any RTC control register (Counter, Alarm, Control, etc.) is written.
 * It returns to one only after all previous updates have been fully
 * synchronized to the RTC clock domain. This function polls the WriteDone
 * bit with a timeout to ensure the device is ready for the next operation.
 *
 * Return: 0 on success, or -EBUSY on timeout.
 */
static int atcrtc_check_write_done(struct atcrtc_dev *rtc)
{
	unsigned int val;

	/*
	 * Using read_poll_timeout is more efficient than a manual loop
	 * with usleep_range.
	 */
	return regmap_read_poll_timeout(rtc->regmap, RTC_STA, val,
					val & WRITE_DONE,
					ATCRTC_TIMEOUT_USLEEP_MIN,
					ATCRTC_TIMEOUT_US);
}

static irqreturn_t atcrtc_alarm_isr(int irq, void *dev)
{
	struct atcrtc_dev *rtc = dev;
	unsigned int status;

	regmap_read(rtc->regmap, RTC_STA, &status);
	if (status & ALARM_INT) {
		regmap_write(rtc->regmap, RTC_STA, ALARM_INT);
		rtc->alarm_en = false;
		schedule_work(&rtc->rtc_work);
		rtc_update_irq(rtc->rtc_dev, 1, RTC_AF | RTC_IRQF);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int atcrtc_alarm_irq_enable(struct device *dev, unsigned int enable)
{
	struct atcrtc_dev *rtc = dev_get_drvdata(dev);
	unsigned int mask;
	int ret;

	ret = atcrtc_check_write_done(rtc);
	if (ret)
		return ret;

	mask = ALARM_WAKEUP | ALARM_INT;
	regmap_update_bits(rtc->regmap, RTC_CR, mask, enable ? mask : 0);

	return 0;
}

static void atcrtc_alarm_clear(struct work_struct *work)
{
	struct atcrtc_dev *rtc =
		container_of(work, struct atcrtc_dev, rtc_work);
	int ret;

	rtc_lock(rtc->rtc_dev);

	if (!rtc->alarm_en) {
		ret = atcrtc_check_write_done(rtc);
		if (ret)
			dev_info(&rtc->rtc_dev->dev,
				 "failed to sync before clearing alarm: %d\n",
				 ret);
		else
			regmap_update_bits(rtc->regmap, RTC_CR,
					   ALARM_WAKEUP | ALARM_INT, 0);
	}
	rtc_unlock(rtc->rtc_dev);
}

static int atcrtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct atcrtc_dev *rtc = dev_get_drvdata(dev);
	time64_t time;
	unsigned int rtc_cnt;

	if (!regmap_test_bits(rtc->regmap, RTC_CR, RTC_EN))
		return -EIO;

	regmap_read(rtc->regmap, RTC_CNT, &rtc_cnt);
	time = ATCRTC_TIME_TO_SEC(RTC_DAY_GET(rtc_cnt),
				  RTC_HOUR_GET(rtc_cnt),
				  RTC_MIN_GET(rtc_cnt),
				  RTC_SEC_GET(rtc_cnt));
	rtc_time64_to_tm(time, tm);

	return 0;
}

static int atcrtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct atcrtc_dev *rtc = dev_get_drvdata(dev);
	time64_t time;
	unsigned int counter;
	unsigned int day;
	int ret;

	time = rtc_tm_to_time64(tm);
	day = div_s64(time, 86400);
	counter = RTC_DAY_SET(day) |
		  RTC_HOUR_SET(tm->tm_hour) |
		  RTC_MIN_SET(tm->tm_min) |
		  RTC_SEC_SET(tm->tm_sec);
	ret = atcrtc_check_write_done(rtc);
	if (ret)
		return ret;
	regmap_write(rtc->regmap, RTC_CNT, counter);

	ret = atcrtc_check_write_done(rtc);
	if (ret)
		return ret;
	regmap_update_bits(rtc->regmap, RTC_CR, RTC_EN, RTC_EN);

	return 0;
}

static int atcrtc_read_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct atcrtc_dev *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &wkalrm->time;
	unsigned int rtc_alarm;

	wkalrm->enabled = regmap_test_bits(rtc->regmap, RTC_CR, ALARM_INT);
	regmap_read(rtc->regmap, RTC_ALM, &rtc_alarm);
	tm->tm_hour = RTC_HOUR_GET(rtc_alarm);
	tm->tm_min = RTC_MIN_GET(rtc_alarm);
	tm->tm_sec = RTC_SEC_GET(rtc_alarm);

	/* The RTC alarm does not support day/month/year fields */
	tm->tm_mday = -1;
	tm->tm_mon = -1;
	tm->tm_year = -1;

	return 0;
}

static int atcrtc_set_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct atcrtc_dev *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &wkalrm->time;
	unsigned int rtc_alarm;
	int ret;

	/* Disable alarm first before setting a new one */
	ret = atcrtc_alarm_irq_enable(dev, 0);
	if (ret)
		return ret;

	rtc->alarm_en = false;

	rtc_alarm = RTC_SEC_SET(tm->tm_sec) |
		    RTC_MIN_SET(tm->tm_min) |
		    RTC_HOUR_SET(tm->tm_hour);

	ret = atcrtc_check_write_done(rtc);
	if (ret)
		return ret;

	regmap_write(rtc->regmap, RTC_ALM, rtc_alarm);

	rtc->alarm_en = wkalrm->enabled;
	ret = atcrtc_alarm_irq_enable(dev, wkalrm->enabled);

	return ret;
}

static const struct rtc_class_ops rtc_ops = {
	.read_time = atcrtc_read_time,
	.set_time = atcrtc_set_time,
	.read_alarm = atcrtc_read_alarm,
	.set_alarm = atcrtc_set_alarm,
	.alarm_irq_enable = atcrtc_alarm_irq_enable,
};

static int atcrtc_probe(struct platform_device *pdev)
{
	struct atcrtc_dev *atcrtc_dev;
	void __iomem *reg_base;
	unsigned int rtc_id;
	int ret;

	atcrtc_dev = devm_kzalloc(&pdev->dev, sizeof(*atcrtc_dev), GFP_KERNEL);
	if (!atcrtc_dev)
		return -ENOMEM;
	platform_set_drvdata(pdev, atcrtc_dev);

	reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg_base))
		return dev_err_probe(&pdev->dev, PTR_ERR(reg_base),
				     "Failed to map I/O space\n");

	atcrtc_dev->regmap = devm_regmap_init_mmio(&pdev->dev,
						   reg_base,
						   &atcrtc_regmap_config);
	if (IS_ERR(atcrtc_dev->regmap))
		return dev_err_probe(&pdev->dev, PTR_ERR(atcrtc_dev->regmap),
				     "Failed to initialize regmap\n");

	regmap_read(atcrtc_dev->regmap, RTC_ID, &rtc_id);
	if (FIELD_GET(ID_MSK, rtc_id) != ID_ATCRTC100)
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "Failed to initialize RTC: unsupported hardware ID 0x%x\n",
				     rtc_id);

	ret = platform_get_irq(pdev, 1);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to get IRQ for alarm\n");
	atcrtc_dev->alarm_irq = ret;

	ret = devm_request_irq(&pdev->dev,
			       atcrtc_dev->alarm_irq,
			       atcrtc_alarm_isr,
			       0,
			       "atcrtc_alarm",
			       atcrtc_dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to request IRQ %d for alarm\n",
				     atcrtc_dev->alarm_irq);

	atcrtc_dev->rtc_dev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(atcrtc_dev->rtc_dev))
		return dev_err_probe(&pdev->dev, PTR_ERR(atcrtc_dev->rtc_dev),
				     "Failed to allocate RTC device\n");

	set_bit(RTC_FEATURE_ALARM, atcrtc_dev->rtc_dev->features);
	ret = device_init_wakeup(&pdev->dev, true);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to initialize wake capability\n");

	ret = dev_pm_set_wake_irq(&pdev->dev, atcrtc_dev->alarm_irq);
	if (ret) {
		device_init_wakeup(&pdev->dev, false);
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to set wake IRQ\n");
	}

	atcrtc_dev->rtc_dev->ops = &rtc_ops;

	INIT_WORK(&atcrtc_dev->rtc_work, atcrtc_alarm_clear);
	return devm_rtc_register_device(atcrtc_dev->rtc_dev);
}

static int atcrtc_resume(struct device *dev)
{
	struct atcrtc_dev *rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(rtc->alarm_irq);

	return 0;
}

static int atcrtc_suspend(struct device *dev)
{
	struct atcrtc_dev *rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(rtc->alarm_irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(atcrtc_pm_ops, atcrtc_suspend, atcrtc_resume);

static const struct of_device_id atcrtc_dt_match[] = {
	{ .compatible = "andestech,atcrtc100" },
	{ },
};
MODULE_DEVICE_TABLE(of, atcrtc_dt_match);

static struct platform_driver atcrtc_platform_driver = {
	.driver = {
		.name = "atcrtc100",
		.of_match_table = atcrtc_dt_match,
		.pm = pm_sleep_ptr(&atcrtc_pm_ops),
	},
	.probe = atcrtc_probe,
};

module_platform_driver(atcrtc_platform_driver);

MODULE_AUTHOR("CL Wang <cl634@andestech.com>");
MODULE_DESCRIPTION("Andes ATCRTC100 driver");
MODULE_LICENSE("GPL");
