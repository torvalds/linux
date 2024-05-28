// SPDX-License-Identifier: GPL-2.0-only
/*
 * EPSON TOYOCOM RTC-7301SF/DG Driver
 *
 * Copyright (c) 2016 Akinobu Mita <akinobu.mita@gmail.com>
 *
 * Based on rtc-rp5c01.c
 *
 * Datasheet: http://www5.epsondevice.com/en/products/parallel/rtc7301sf.html
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#define DRV_NAME "rtc-r7301"

#define RTC7301_1_SEC		0x0	/* Bank 0 and Band 1 */
#define RTC7301_10_SEC		0x1	/* Bank 0 and Band 1 */
#define RTC7301_AE		BIT(3)
#define RTC7301_1_MIN		0x2	/* Bank 0 and Band 1 */
#define RTC7301_10_MIN		0x3	/* Bank 0 and Band 1 */
#define RTC7301_1_HOUR		0x4	/* Bank 0 and Band 1 */
#define RTC7301_10_HOUR		0x5	/* Bank 0 and Band 1 */
#define RTC7301_DAY_OF_WEEK	0x6	/* Bank 0 and Band 1 */
#define RTC7301_1_DAY		0x7	/* Bank 0 and Band 1 */
#define RTC7301_10_DAY		0x8	/* Bank 0 and Band 1 */
#define RTC7301_1_MONTH		0x9	/* Bank 0 */
#define RTC7301_10_MONTH	0xa	/* Bank 0 */
#define RTC7301_1_YEAR		0xb	/* Bank 0 */
#define RTC7301_10_YEAR		0xc	/* Bank 0 */
#define RTC7301_100_YEAR	0xd	/* Bank 0 */
#define RTC7301_1000_YEAR	0xe	/* Bank 0 */
#define RTC7301_ALARM_CONTROL	0xe	/* Bank 1 */
#define RTC7301_ALARM_CONTROL_AIE	BIT(0)
#define RTC7301_ALARM_CONTROL_AF	BIT(1)
#define RTC7301_TIMER_CONTROL	0xe	/* Bank 2 */
#define RTC7301_TIMER_CONTROL_TIE	BIT(0)
#define RTC7301_TIMER_CONTROL_TF	BIT(1)
#define RTC7301_CONTROL		0xf	/* All banks */
#define RTC7301_CONTROL_BUSY		BIT(0)
#define RTC7301_CONTROL_STOP		BIT(1)
#define RTC7301_CONTROL_BANK_SEL_0	BIT(2)
#define RTC7301_CONTROL_BANK_SEL_1	BIT(3)

struct rtc7301_priv {
	struct regmap *regmap;
	int irq;
	spinlock_t lock;
	u8 bank;
};

/*
 * When the device is memory-mapped, some platforms pack the registers into
 * 32-bit access using the lower 8 bits at each 4-byte stride, while others
 * expose them as simply consecutive bytes.
 */
static const struct regmap_config rtc7301_regmap_32_config = {
	.reg_bits = 32,
	.val_bits = 8,
	.reg_stride = 4,
};

static const struct regmap_config rtc7301_regmap_8_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_stride = 1,
};

static u8 rtc7301_read(struct rtc7301_priv *priv, unsigned int reg)
{
	int reg_stride = regmap_get_reg_stride(priv->regmap);
	unsigned int val;

	regmap_read(priv->regmap, reg_stride * reg, &val);

	return val & 0xf;
}

static void rtc7301_write(struct rtc7301_priv *priv, u8 val, unsigned int reg)
{
	int reg_stride = regmap_get_reg_stride(priv->regmap);

	regmap_write(priv->regmap, reg_stride * reg, val);
}

static void rtc7301_update_bits(struct rtc7301_priv *priv, unsigned int reg,
				u8 mask, u8 val)
{
	int reg_stride = regmap_get_reg_stride(priv->regmap);

	regmap_update_bits(priv->regmap, reg_stride * reg, mask, val);
}

static int rtc7301_wait_while_busy(struct rtc7301_priv *priv)
{
	int retries = 100;

	while (retries-- > 0) {
		u8 val;

		val = rtc7301_read(priv, RTC7301_CONTROL);
		if (!(val & RTC7301_CONTROL_BUSY))
			return 0;

		udelay(300);
	}

	return -ETIMEDOUT;
}

static void rtc7301_stop(struct rtc7301_priv *priv)
{
	rtc7301_update_bits(priv, RTC7301_CONTROL, RTC7301_CONTROL_STOP,
			    RTC7301_CONTROL_STOP);
}

static void rtc7301_start(struct rtc7301_priv *priv)
{
	rtc7301_update_bits(priv, RTC7301_CONTROL, RTC7301_CONTROL_STOP, 0);
}

static void rtc7301_select_bank(struct rtc7301_priv *priv, u8 bank)
{
	u8 val = 0;

	if (bank == priv->bank)
		return;

	if (bank & BIT(0))
		val |= RTC7301_CONTROL_BANK_SEL_0;
	if (bank & BIT(1))
		val |= RTC7301_CONTROL_BANK_SEL_1;

	rtc7301_update_bits(priv, RTC7301_CONTROL,
			    RTC7301_CONTROL_BANK_SEL_0 |
			    RTC7301_CONTROL_BANK_SEL_1, val);

	priv->bank = bank;
}

static void rtc7301_get_time(struct rtc7301_priv *priv, struct rtc_time *tm,
			     bool alarm)
{
	int year;

	tm->tm_sec = rtc7301_read(priv, RTC7301_1_SEC);
	tm->tm_sec += (rtc7301_read(priv, RTC7301_10_SEC) & ~RTC7301_AE) * 10;
	tm->tm_min = rtc7301_read(priv, RTC7301_1_MIN);
	tm->tm_min += (rtc7301_read(priv, RTC7301_10_MIN) & ~RTC7301_AE) * 10;
	tm->tm_hour = rtc7301_read(priv, RTC7301_1_HOUR);
	tm->tm_hour += (rtc7301_read(priv, RTC7301_10_HOUR) & ~RTC7301_AE) * 10;
	tm->tm_mday = rtc7301_read(priv, RTC7301_1_DAY);
	tm->tm_mday += (rtc7301_read(priv, RTC7301_10_DAY) & ~RTC7301_AE) * 10;

	if (alarm) {
		tm->tm_wday = -1;
		tm->tm_mon = -1;
		tm->tm_year = -1;
		tm->tm_yday = -1;
		tm->tm_isdst = -1;
		return;
	}

	tm->tm_wday = (rtc7301_read(priv, RTC7301_DAY_OF_WEEK) & ~RTC7301_AE);
	tm->tm_mon = rtc7301_read(priv, RTC7301_10_MONTH) * 10 +
		     rtc7301_read(priv, RTC7301_1_MONTH) - 1;
	year = rtc7301_read(priv, RTC7301_1000_YEAR) * 1000 +
	       rtc7301_read(priv, RTC7301_100_YEAR) * 100 +
	       rtc7301_read(priv, RTC7301_10_YEAR) * 10 +
	       rtc7301_read(priv, RTC7301_1_YEAR);

	tm->tm_year = year - 1900;
}

static void rtc7301_write_time(struct rtc7301_priv *priv, struct rtc_time *tm,
			       bool alarm)
{
	int year;

	rtc7301_write(priv, tm->tm_sec % 10, RTC7301_1_SEC);
	rtc7301_write(priv, tm->tm_sec / 10, RTC7301_10_SEC);

	rtc7301_write(priv, tm->tm_min % 10, RTC7301_1_MIN);
	rtc7301_write(priv, tm->tm_min / 10, RTC7301_10_MIN);

	rtc7301_write(priv, tm->tm_hour % 10, RTC7301_1_HOUR);
	rtc7301_write(priv, tm->tm_hour / 10, RTC7301_10_HOUR);

	rtc7301_write(priv, tm->tm_mday % 10, RTC7301_1_DAY);
	rtc7301_write(priv, tm->tm_mday / 10, RTC7301_10_DAY);

	/* Don't care for alarm register */
	rtc7301_write(priv, alarm ? RTC7301_AE : tm->tm_wday,
		      RTC7301_DAY_OF_WEEK);

	if (alarm)
		return;

	rtc7301_write(priv, (tm->tm_mon + 1) % 10, RTC7301_1_MONTH);
	rtc7301_write(priv, (tm->tm_mon + 1) / 10, RTC7301_10_MONTH);

	year = tm->tm_year + 1900;

	rtc7301_write(priv, year % 10, RTC7301_1_YEAR);
	rtc7301_write(priv, (year / 10) % 10, RTC7301_10_YEAR);
	rtc7301_write(priv, (year / 100) % 10, RTC7301_100_YEAR);
	rtc7301_write(priv, year / 1000, RTC7301_1000_YEAR);
}

static void rtc7301_alarm_irq(struct rtc7301_priv *priv, unsigned int enabled)
{
	rtc7301_update_bits(priv, RTC7301_ALARM_CONTROL,
			    RTC7301_ALARM_CONTROL_AF |
			    RTC7301_ALARM_CONTROL_AIE,
			    enabled ? RTC7301_ALARM_CONTROL_AIE : 0);
}

static int rtc7301_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rtc7301_priv *priv = dev_get_drvdata(dev);
	unsigned long flags;
	int err;

	spin_lock_irqsave(&priv->lock, flags);

	rtc7301_select_bank(priv, 0);

	err = rtc7301_wait_while_busy(priv);
	if (!err)
		rtc7301_get_time(priv, tm, false);

	spin_unlock_irqrestore(&priv->lock, flags);

	return err;
}

static int rtc7301_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rtc7301_priv *priv = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	rtc7301_stop(priv);
	udelay(300);
	rtc7301_select_bank(priv, 0);
	rtc7301_write_time(priv, tm, false);
	rtc7301_start(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int rtc7301_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct rtc7301_priv *priv = dev_get_drvdata(dev);
	unsigned long flags;
	u8 alrm_ctrl;

	if (priv->irq <= 0)
		return -EINVAL;

	spin_lock_irqsave(&priv->lock, flags);

	rtc7301_select_bank(priv, 1);
	rtc7301_get_time(priv, &alarm->time, true);

	alrm_ctrl = rtc7301_read(priv, RTC7301_ALARM_CONTROL);

	alarm->enabled = !!(alrm_ctrl & RTC7301_ALARM_CONTROL_AIE);
	alarm->pending = !!(alrm_ctrl & RTC7301_ALARM_CONTROL_AF);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int rtc7301_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct rtc7301_priv *priv = dev_get_drvdata(dev);
	unsigned long flags;

	if (priv->irq <= 0)
		return -EINVAL;

	spin_lock_irqsave(&priv->lock, flags);

	rtc7301_select_bank(priv, 1);
	rtc7301_write_time(priv, &alarm->time, true);
	rtc7301_alarm_irq(priv, alarm->enabled);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int rtc7301_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct rtc7301_priv *priv = dev_get_drvdata(dev);
	unsigned long flags;

	if (priv->irq <= 0)
		return -EINVAL;

	spin_lock_irqsave(&priv->lock, flags);

	rtc7301_select_bank(priv, 1);
	rtc7301_alarm_irq(priv, enabled);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static const struct rtc_class_ops rtc7301_rtc_ops = {
	.read_time	= rtc7301_read_time,
	.set_time	= rtc7301_set_time,
	.read_alarm	= rtc7301_read_alarm,
	.set_alarm	= rtc7301_set_alarm,
	.alarm_irq_enable = rtc7301_alarm_irq_enable,
};

static irqreturn_t rtc7301_irq_handler(int irq, void *dev_id)
{
	struct rtc_device *rtc = dev_id;
	struct rtc7301_priv *priv = dev_get_drvdata(rtc->dev.parent);
	irqreturn_t ret = IRQ_NONE;
	u8 alrm_ctrl;

	spin_lock(&priv->lock);

	rtc7301_select_bank(priv, 1);

	alrm_ctrl = rtc7301_read(priv, RTC7301_ALARM_CONTROL);
	if (alrm_ctrl & RTC7301_ALARM_CONTROL_AF) {
		ret = IRQ_HANDLED;
		rtc7301_alarm_irq(priv, false);
		rtc_update_irq(rtc, 1, RTC_IRQF | RTC_AF);
	}

	spin_unlock(&priv->lock);

	return ret;
}

static void rtc7301_init(struct rtc7301_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	rtc7301_select_bank(priv, 2);
	rtc7301_write(priv, 0, RTC7301_TIMER_CONTROL);

	spin_unlock_irqrestore(&priv->lock, flags);
}

static int __init rtc7301_rtc_probe(struct platform_device *dev)
{
	void __iomem *regs;
	struct rtc7301_priv *priv;
	struct rtc_device *rtc;
	static const struct regmap_config *mapconf;
	int ret;
	u32 val;

	priv = devm_kzalloc(&dev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(dev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	ret = device_property_read_u32(&dev->dev, "reg-io-width", &val);
	if (ret)
		/* Default to 32bit accesses */
		val = 4;

	switch (val) {
	case 1:
		mapconf = &rtc7301_regmap_8_config;
		break;
	case 4:
		mapconf = &rtc7301_regmap_32_config;
		break;
	default:
		dev_err(&dev->dev, "invalid reg-io-width %d\n", val);
		return -EINVAL;
	}

	priv->regmap = devm_regmap_init_mmio(&dev->dev, regs,
					     mapconf);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->irq = platform_get_irq(dev, 0);

	spin_lock_init(&priv->lock);
	priv->bank = -1;

	rtc7301_init(priv);

	platform_set_drvdata(dev, priv);

	rtc = devm_rtc_device_register(&dev->dev, DRV_NAME, &rtc7301_rtc_ops,
				       THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	if (priv->irq > 0) {
		ret = devm_request_irq(&dev->dev, priv->irq,
				       rtc7301_irq_handler, IRQF_SHARED,
				       dev_name(&dev->dev), rtc);
		if (ret) {
			priv->irq = 0;
			dev_err(&dev->dev, "unable to request IRQ\n");
		} else {
			device_set_wakeup_capable(&dev->dev, true);
		}
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int rtc7301_suspend(struct device *dev)
{
	struct rtc7301_priv *priv = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(priv->irq);

	return 0;
}

static int rtc7301_resume(struct device *dev)
{
	struct rtc7301_priv *priv = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(priv->irq);

	return 0;
}

#endif

static SIMPLE_DEV_PM_OPS(rtc7301_pm_ops, rtc7301_suspend, rtc7301_resume);

static const struct of_device_id rtc7301_dt_match[] = {
	{ .compatible = "epson,rtc7301sf" },
	{ .compatible = "epson,rtc7301dg" },
	{}
};
MODULE_DEVICE_TABLE(of, rtc7301_dt_match);

static struct platform_driver rtc7301_rtc_driver = {
	.driver	= {
		.name = DRV_NAME,
		.of_match_table = rtc7301_dt_match,
		.pm = &rtc7301_pm_ops,
	},
};

module_platform_driver_probe(rtc7301_rtc_driver, rtc7301_rtc_probe);

MODULE_AUTHOR("Akinobu Mita <akinobu.mita@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EPSON TOYOCOM RTC-7301SF/DG Driver");
MODULE_ALIAS("platform:rtc-r7301");
