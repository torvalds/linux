// SPDX-License-Identifier: GPL-2.0+
// Copyright 2015 IBM Corp.

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/io.h>

struct aspeed_rtc {
	struct rtc_device *rtc_dev;
	void __iomem *base;
	spinlock_t irq_lock; /* interrupt enable register lock */
	struct mutex write_mutex; /* serialize registers write */
};

#define RTC_TIME	0x00
#define RTC_YEAR	0x04
#define RTC_ALARM	0x08
#define RTC_CTRL	0x10
#define RTC_ALARM_STATUS	0x14

#define RTC_ENABLE		BIT(0)
#define RTC_UNLOCK		BIT(1)
#define RTC_ALARM_MODE		BIT(2)
#define RTC_ALARM_SEC_ENABLE	BIT(3)
#define RTC_ALARM_MIN_ENABLE	BIT(4)
#define RTC_ALARM_HOUR_ENABLE	BIT(5)
#define RTC_ALARM_MDAY_ENABLE	BIT(6)

#define RTC_ALARM_SEC_CB_STATUS	BIT(0)
#define RTC_ALARM_MIN_STATUS	BIT(1)
#define RTC_ALARM_HOUR_STATUS	BIT(2)
#define RTC_ALARM_MDAY_STATUS	BIT(3)

/*
 * enable a rtc interrupt
 */
static void aspeed_rtc_int_enable(struct aspeed_rtc *rtc, u32 intr)
{
	unsigned long flags;

	spin_lock_irqsave(&rtc->irq_lock, flags);
	writel(readl(rtc->base + RTC_CTRL) | intr, rtc->base + RTC_CTRL);
	spin_unlock_irqrestore(&rtc->irq_lock, flags);
}

/*
 * disable a rtc interrupt
 */
static void aspeed_rtc_int_disable(struct aspeed_rtc *rtc, u32 intr)
{
	unsigned long flags;

	spin_lock_irqsave(&rtc->irq_lock, flags);
	writel(readl(rtc->base + RTC_CTRL) & ~intr, rtc->base + RTC_CTRL);
	spin_unlock_irqrestore(&rtc->irq_lock, flags);
}

/*
 * clean a rtc interrupt status
 */
static void aspeed_rtc_clean_alarm(struct aspeed_rtc *rtc)
{
	unsigned long flags;

	spin_lock_irqsave(&rtc->irq_lock, flags);
	writel(readl(rtc->base + RTC_ALARM_STATUS), rtc->base + RTC_ALARM_STATUS);
	spin_unlock_irqrestore(&rtc->irq_lock, flags);
}

static int aspeed_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct aspeed_rtc *rtc = dev_get_drvdata(dev);
	unsigned int cent, year;
	u32 reg1, reg2;

	if (!(readl(rtc->base + RTC_CTRL) & RTC_ENABLE)) {
		dev_dbg(dev, "%s failing as rtc disabled\n", __func__);
		return -EINVAL;
	}

	do {
		reg2 = readl(rtc->base + RTC_YEAR);
		reg1 = readl(rtc->base + RTC_TIME);
	} while (reg2 != readl(rtc->base + RTC_YEAR));

	tm->tm_mday = (reg1 >> 24) & 0x1f;
	tm->tm_hour = (reg1 >> 16) & 0x1f;
	tm->tm_min = (reg1 >> 8) & 0x3f;
	tm->tm_sec = (reg1 >> 0) & 0x3f;

	cent = (reg2 >> 16) & 0x1f;
	year = (reg2 >> 8) & 0x7f;
	tm->tm_mon = ((reg2 >>  0) & 0x0f) - 1;
	tm->tm_year = year + (cent * 100) - 1900;

	dev_dbg(dev, "%s %ptR", __func__, tm);

	return 0;
}

static int aspeed_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct aspeed_rtc *rtc = dev_get_drvdata(dev);
	u32 reg1, reg2, ctrl;
	int year, cent;

	cent = (tm->tm_year + 1900) / 100;
	year = tm->tm_year % 100;

	reg1 = (tm->tm_mday << 24) | (tm->tm_hour << 16) | (tm->tm_min << 8) |
		tm->tm_sec;

	reg2 = ((cent & 0x1f) << 16) | ((year & 0x7f) << 8) |
		((tm->tm_mon + 1) & 0xf);

	ctrl = readl(rtc->base + RTC_CTRL);
	writel(ctrl | RTC_UNLOCK, rtc->base + RTC_CTRL);

	writel(reg1, rtc->base + RTC_TIME);
	writel(reg2, rtc->base + RTC_YEAR);

	/* Re-lock and ensure enable is set now that a time is programmed */
	writel(ctrl | RTC_ENABLE, rtc->base + RTC_CTRL);

	return 0;
}

static int aspeed_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct aspeed_rtc *rtc = dev_get_drvdata(dev);
	unsigned int alarm_enable;

	alarm_enable = RTC_ALARM_SEC_ENABLE | RTC_ALARM_MIN_ENABLE |
		       RTC_ALARM_HOUR_ENABLE | RTC_ALARM_MDAY_ENABLE;
	if (enabled)
		aspeed_rtc_int_enable(rtc, alarm_enable);
	else
		aspeed_rtc_int_disable(rtc, alarm_enable);

	return 0;
}

static int aspeed_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct aspeed_rtc *rtc = dev_get_drvdata(dev);
	u32 reg1, reg2;
	unsigned int alarm_enable;
	unsigned int alarm_status;

	if (!(readl(rtc->base + RTC_CTRL) & RTC_ENABLE)) {
		dev_dbg(dev, "%s failing as rtc disabled\n", __func__);
		return -EINVAL;
	}

	do {
		reg2 = readl(rtc->base + RTC_YEAR);
		reg1 = readl(rtc->base + RTC_TIME);
	} while (reg1 != readl(rtc->base + RTC_TIME));

	/* read alarm value */
	alarm->time.tm_mday = (reg1 >> 24) & 0x1f;
	alarm->time.tm_hour = (reg1 >> 16) & 0x1f;
	alarm->time.tm_min = (reg1 >> 8) & 0x3f;
	alarm->time.tm_sec = (reg1 >> 0) & 0x3f;

	/* don't allow the ALARM read to mess up ALARM_STATUS */
	mutex_lock(&rtc->write_mutex);

	alarm_enable = RTC_ALARM_SEC_ENABLE | RTC_ALARM_MIN_ENABLE |
		       RTC_ALARM_HOUR_ENABLE | RTC_ALARM_MDAY_ENABLE;
	/* alarm is enabled if the interrupt is enabled */
	if (readl(rtc->base + RTC_CTRL) & alarm_enable)
		alarm->enabled = true;
	else
		alarm->enabled = false;

	alarm_status = RTC_ALARM_SEC_CB_STATUS | RTC_ALARM_MIN_STATUS |
		       RTC_ALARM_HOUR_STATUS | RTC_ALARM_MDAY_STATUS;
	if (readl(rtc->base + RTC_ALARM_STATUS) & alarm_status)
		alarm->pending = true;
	else
		alarm->pending = false;

	mutex_unlock(&rtc->write_mutex);

	return 0;
}

static int aspeed_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct aspeed_rtc *rtc = dev_get_drvdata(dev);
	unsigned int alarm_enable;
	u32 reg;

	if (!(readl(rtc->base + RTC_CTRL) & RTC_ENABLE)) {
		dev_dbg(dev, "%s failing as rtc disabled\n", __func__);
		return -EINVAL;
	}

	/* don't allow the ALARM read to mess up ALARM_STATUS */
	mutex_lock(&rtc->write_mutex);

	/* write the new alarm time */
	reg  = (((alarm->time.tm_mday >> 24) & 0x1f) | ((alarm->time.tm_hour >> 16) & 0x1f) |
		((alarm->time.tm_min >> 8) & 0x3f)  | ((alarm->time.tm_sec >> 0) & 0x3f));
	writel(reg, rtc->base + RTC_ALARM);

	alarm_enable = RTC_ALARM_SEC_ENABLE | RTC_ALARM_MIN_ENABLE |
		       RTC_ALARM_HOUR_ENABLE | RTC_ALARM_MDAY_ENABLE;
	/* alarm is enabled if the interrupt is enabled */
	if (alarm->enabled)
		aspeed_rtc_int_enable(rtc, alarm_enable);
	else
		aspeed_rtc_int_disable(rtc, alarm_enable);

	mutex_unlock(&rtc->write_mutex);

	return 0;
}

static const struct rtc_class_ops aspeed_rtc_ops = {
	.read_time		= aspeed_rtc_read_time,
	.set_time		= aspeed_rtc_set_time,
	.alarm_irq_enable	= aspeed_rtc_alarm_irq_enable,
	.read_alarm		= aspeed_rtc_read_alarm,
	.set_alarm		= aspeed_rtc_set_alarm,
};

static irqreturn_t aspeed_rtc_irq(int irq, void *dev_id)
{
	struct aspeed_rtc *rtc = dev_id;
	unsigned int alarm_enable;

	alarm_enable = RTC_ALARM_SEC_ENABLE | RTC_ALARM_MIN_ENABLE |
		       RTC_ALARM_HOUR_ENABLE | RTC_ALARM_MDAY_ENABLE;
	aspeed_rtc_int_disable(rtc, alarm_enable);
	aspeed_rtc_clean_alarm(rtc);

	return IRQ_HANDLED;
}

static int aspeed_rtc_probe(struct platform_device *pdev)
{
	struct aspeed_rtc *rtc;
	unsigned int irq;
	int rc;
	u32 ctrl;

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rtc->base)) {
		dev_err(&pdev->dev, "cannot ioremap resource for rtc\n");
		return PTR_ERR(rtc->base);
	}

	rtc->rtc_dev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc->rtc_dev))
		return PTR_ERR(rtc->rtc_dev);

	spin_lock_init(&rtc->irq_lock);
	mutex_init(&rtc->write_mutex);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	rc = devm_request_irq(&pdev->dev, irq, aspeed_rtc_irq,
			      0, pdev->name, rtc);
	if (rc) {
		dev_err(&pdev->dev, "interrupt number %d is not available.\n", irq);
		goto err;
	}

	platform_set_drvdata(pdev, rtc);

	device_init_wakeup(&pdev->dev, true);

	rtc->rtc_dev->ops = &aspeed_rtc_ops;
	rtc->rtc_dev->range_min = RTC_TIMESTAMP_BEGIN_1900;
	rtc->rtc_dev->range_max = 38814989399LL; /* 3199-12-31 23:59:59 */

	/*
	 * In devm_rtc_register_device,
	 * rtc_hctosys read time from RTC to check hardware status.
	 * In rtc_read_time, run aspeed_rtc_read_time and check the rtc_time.
	 * As a result, need to enable and initialize RTC time.
	 *
	 * Enable and unlock RTC to initialize RTC time to 1970-01-01T01:01:01
	 * and re-lock and ensure enable is set now that a time is programmed.
	 */
	ctrl = readl(rtc->base + RTC_CTRL);
	writel(ctrl | RTC_UNLOCK | RTC_ENABLE, rtc->base + RTC_CTRL);
	/*
	 * Initial value set to year:70,mon:0,mday:1,hour:1,min:1,sec:1
	 * rtc_valid_tm check whether in suitable range or not.
	 */
	writel(0x01010101, rtc->base + RTC_TIME);
	writel(0x00134601, rtc->base + RTC_YEAR);
	writel(ctrl | RTC_ENABLE, rtc->base + RTC_CTRL);

	rc = devm_rtc_register_device(rtc->rtc_dev);
	if (rc) {
		dev_err(&pdev->dev, "can't register rtc device\n");
		goto err;
	}

	return 0;

err:
	return rc;
}

static const struct of_device_id aspeed_rtc_match[] = {
	{ .compatible = "aspeed,ast2400-rtc", },
	{ .compatible = "aspeed,ast2500-rtc", },
	{ .compatible = "aspeed,ast2600-rtc", },
	{ .compatible = "aspeed,ast2700-rtc", },
	{}
};
MODULE_DEVICE_TABLE(of, aspeed_rtc_match);

static struct platform_driver aspeed_rtc_driver = {
	.driver = {
		.name = "aspeed-rtc",
		.of_match_table = aspeed_rtc_match,
	},
};

module_platform_driver_probe(aspeed_rtc_driver, aspeed_rtc_probe);

MODULE_DESCRIPTION("ASPEED RTC driver");
MODULE_AUTHOR("Joel Stanley <joel@jms.id.au>");
MODULE_LICENSE("GPL");
