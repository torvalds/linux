/*
 * Real Time Clock driver for Freescale MC13XXX PMIC
 *
 * (C) 2009 Sascha Hauer, Pengutronix
 * (C) 2009 Uwe Kleine-Koenig, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mfd/mc13xxx.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/rtc.h>

#define DRIVER_NAME "mc13xxx-rtc"

#define MC13XXX_RTCTOD	20
#define MC13XXX_RTCTODA	21
#define MC13XXX_RTCDAY	22
#define MC13XXX_RTCDAYA	23

#define SEC_PER_DAY	(24 * 60 * 60)

struct mc13xxx_rtc {
	struct rtc_device *rtc;
	struct mc13xxx *mc13xxx;
	int valid;
};

static int mc13xxx_rtc_irq_enable_unlocked(struct device *dev,
		unsigned int enabled, int irq)
{
	struct mc13xxx_rtc *priv = dev_get_drvdata(dev);
	int (*func)(struct mc13xxx *mc13xxx, int irq);

	if (!priv->valid)
		return -ENODATA;

	func = enabled ? mc13xxx_irq_unmask : mc13xxx_irq_mask;
	return func(priv->mc13xxx, irq);
}

static int mc13xxx_rtc_alarm_irq_enable(struct device *dev,
					unsigned int enabled)
{
	struct mc13xxx_rtc *priv = dev_get_drvdata(dev);
	int ret;

	mc13xxx_lock(priv->mc13xxx);

	ret = mc13xxx_rtc_irq_enable_unlocked(dev, enabled, MC13XXX_IRQ_TODA);

	mc13xxx_unlock(priv->mc13xxx);

	return ret;
}

static int mc13xxx_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct mc13xxx_rtc *priv = dev_get_drvdata(dev);
	unsigned int seconds, days1, days2;

	if (!priv->valid)
		return -ENODATA;

	do {
		int ret;

		ret = mc13xxx_reg_read(priv->mc13xxx, MC13XXX_RTCDAY, &days1);
		if (ret)
			return ret;

		ret = mc13xxx_reg_read(priv->mc13xxx, MC13XXX_RTCTOD, &seconds);
		if (ret)
			return ret;

		ret = mc13xxx_reg_read(priv->mc13xxx, MC13XXX_RTCDAY, &days2);
		if (ret)
			return ret;
	} while (days1 != days2);

	rtc_time_to_tm(days1 * SEC_PER_DAY + seconds, tm);

	return rtc_valid_tm(tm);
}

static int mc13xxx_rtc_set_mmss(struct device *dev, unsigned long secs)
{
	struct mc13xxx_rtc *priv = dev_get_drvdata(dev);
	unsigned int seconds, days;
	unsigned int alarmseconds;
	int ret;

	seconds = secs % SEC_PER_DAY;
	days = secs / SEC_PER_DAY;

	mc13xxx_lock(priv->mc13xxx);

	/*
	 * temporarily invalidate alarm to prevent triggering it when the day is
	 * already updated while the time isn't yet.
	 */
	ret = mc13xxx_reg_read(priv->mc13xxx, MC13XXX_RTCTODA, &alarmseconds);
	if (unlikely(ret))
		goto out;

	if (alarmseconds < SEC_PER_DAY) {
		ret = mc13xxx_reg_write(priv->mc13xxx,
				MC13XXX_RTCTODA, 0x1ffff);
		if (unlikely(ret))
			goto out;
	}

	/*
	 * write seconds=0 to prevent a day switch between writing days
	 * and seconds below
	 */
	ret = mc13xxx_reg_write(priv->mc13xxx, MC13XXX_RTCTOD, 0);
	if (unlikely(ret))
		goto out;

	ret = mc13xxx_reg_write(priv->mc13xxx, MC13XXX_RTCDAY, days);
	if (unlikely(ret))
		goto out;

	ret = mc13xxx_reg_write(priv->mc13xxx, MC13XXX_RTCTOD, seconds);
	if (unlikely(ret))
		goto out;

	/* restore alarm */
	if (alarmseconds < SEC_PER_DAY) {
		ret = mc13xxx_reg_write(priv->mc13xxx,
				MC13XXX_RTCTODA, alarmseconds);
		if (unlikely(ret))
			goto out;
	}

	if (!priv->valid) {
		ret = mc13xxx_irq_ack(priv->mc13xxx, MC13XXX_IRQ_RTCRST);
		if (unlikely(ret))
			goto out;

		ret = mc13xxx_irq_unmask(priv->mc13xxx, MC13XXX_IRQ_RTCRST);
	}

out:
	priv->valid = !ret;

	mc13xxx_unlock(priv->mc13xxx);

	return ret;
}

static int mc13xxx_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct mc13xxx_rtc *priv = dev_get_drvdata(dev);
	unsigned seconds, days;
	unsigned long s1970;
	int enabled, pending;
	int ret;

	mc13xxx_lock(priv->mc13xxx);

	ret = mc13xxx_reg_read(priv->mc13xxx, MC13XXX_RTCTODA, &seconds);
	if (unlikely(ret))
		goto out;
	if (seconds >= SEC_PER_DAY) {
		ret = -ENODATA;
		goto out;
	}

	ret = mc13xxx_reg_read(priv->mc13xxx, MC13XXX_RTCDAY, &days);
	if (unlikely(ret))
		goto out;

	ret = mc13xxx_irq_status(priv->mc13xxx, MC13XXX_IRQ_TODA,
			&enabled, &pending);

out:
	mc13xxx_unlock(priv->mc13xxx);

	if (ret)
		return ret;

	alarm->enabled = enabled;
	alarm->pending = pending;

	s1970 = days * SEC_PER_DAY + seconds;

	rtc_time_to_tm(s1970, &alarm->time);
	dev_dbg(dev, "%s: %lu\n", __func__, s1970);

	return 0;
}

static int mc13xxx_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct mc13xxx_rtc *priv = dev_get_drvdata(dev);
	unsigned long s1970;
	unsigned seconds, days;
	int ret;

	mc13xxx_lock(priv->mc13xxx);

	/* disable alarm to prevent false triggering */
	ret = mc13xxx_reg_write(priv->mc13xxx, MC13XXX_RTCTODA, 0x1ffff);
	if (unlikely(ret))
		goto out;

	ret = mc13xxx_irq_ack(priv->mc13xxx, MC13XXX_IRQ_TODA);
	if (unlikely(ret))
		goto out;

	ret = rtc_tm_to_time(&alarm->time, &s1970);
	if (unlikely(ret))
		goto out;

	dev_dbg(dev, "%s: o%2.s %lu\n", __func__, alarm->enabled ? "n" : "ff",
			s1970);

	ret = mc13xxx_rtc_irq_enable_unlocked(dev, alarm->enabled,
			MC13XXX_IRQ_TODA);
	if (unlikely(ret))
		goto out;

	seconds = s1970 % SEC_PER_DAY;
	days = s1970 / SEC_PER_DAY;

	ret = mc13xxx_reg_write(priv->mc13xxx, MC13XXX_RTCDAYA, days);
	if (unlikely(ret))
		goto out;

	ret = mc13xxx_reg_write(priv->mc13xxx, MC13XXX_RTCTODA, seconds);

out:
	mc13xxx_unlock(priv->mc13xxx);

	return ret;
}

static irqreturn_t mc13xxx_rtc_alarm_handler(int irq, void *dev)
{
	struct mc13xxx_rtc *priv = dev;
	struct mc13xxx *mc13xxx = priv->mc13xxx;

	rtc_update_irq(priv->rtc, 1, RTC_IRQF | RTC_AF);

	mc13xxx_irq_ack(mc13xxx, irq);

	return IRQ_HANDLED;
}

static irqreturn_t mc13xxx_rtc_update_handler(int irq, void *dev)
{
	struct mc13xxx_rtc *priv = dev;
	struct mc13xxx *mc13xxx = priv->mc13xxx;

	rtc_update_irq(priv->rtc, 1, RTC_IRQF | RTC_UF);

	mc13xxx_irq_ack(mc13xxx, irq);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops mc13xxx_rtc_ops = {
	.read_time = mc13xxx_rtc_read_time,
	.set_mmss = mc13xxx_rtc_set_mmss,
	.read_alarm = mc13xxx_rtc_read_alarm,
	.set_alarm = mc13xxx_rtc_set_alarm,
	.alarm_irq_enable = mc13xxx_rtc_alarm_irq_enable,
};

static irqreturn_t mc13xxx_rtc_reset_handler(int irq, void *dev)
{
	struct mc13xxx_rtc *priv = dev;
	struct mc13xxx *mc13xxx = priv->mc13xxx;

	priv->valid = 0;

	mc13xxx_irq_mask(mc13xxx, irq);

	return IRQ_HANDLED;
}

static int __init mc13xxx_rtc_probe(struct platform_device *pdev)
{
	int ret;
	struct mc13xxx_rtc *priv;
	struct mc13xxx *mc13xxx;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mc13xxx = dev_get_drvdata(pdev->dev.parent);
	priv->mc13xxx = mc13xxx;
	priv->valid = 1;

	platform_set_drvdata(pdev, priv);

	mc13xxx_lock(mc13xxx);

	mc13xxx_irq_ack(mc13xxx, MC13XXX_IRQ_RTCRST);

	ret = mc13xxx_irq_request(mc13xxx, MC13XXX_IRQ_RTCRST,
			mc13xxx_rtc_reset_handler, DRIVER_NAME, priv);
	if (ret)
		goto err_irq_request;

	ret = mc13xxx_irq_request(mc13xxx, MC13XXX_IRQ_1HZ,
			mc13xxx_rtc_update_handler, DRIVER_NAME, priv);
	if (ret)
		goto err_irq_request;

	ret = mc13xxx_irq_request_nounmask(mc13xxx, MC13XXX_IRQ_TODA,
			mc13xxx_rtc_alarm_handler, DRIVER_NAME, priv);
	if (ret)
		goto err_irq_request;

	mc13xxx_unlock(mc13xxx);

	priv->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
					     &mc13xxx_rtc_ops, THIS_MODULE);

	return 0;

err_irq_request:
	mc13xxx_irq_free(mc13xxx, MC13XXX_IRQ_TODA, priv);
	mc13xxx_irq_free(mc13xxx, MC13XXX_IRQ_1HZ, priv);
	mc13xxx_irq_free(mc13xxx, MC13XXX_IRQ_RTCRST, priv);

	mc13xxx_unlock(mc13xxx);

	return ret;
}

static int mc13xxx_rtc_remove(struct platform_device *pdev)
{
	struct mc13xxx_rtc *priv = platform_get_drvdata(pdev);

	mc13xxx_lock(priv->mc13xxx);

	mc13xxx_irq_free(priv->mc13xxx, MC13XXX_IRQ_TODA, priv);
	mc13xxx_irq_free(priv->mc13xxx, MC13XXX_IRQ_1HZ, priv);
	mc13xxx_irq_free(priv->mc13xxx, MC13XXX_IRQ_RTCRST, priv);

	mc13xxx_unlock(priv->mc13xxx);

	return 0;
}

static const struct platform_device_id mc13xxx_rtc_idtable[] = {
	{
		.name = "mc13783-rtc",
	}, {
		.name = "mc13892-rtc",
	}, {
		.name = "mc34708-rtc",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, mc13xxx_rtc_idtable);

static struct platform_driver mc13xxx_rtc_driver = {
	.id_table = mc13xxx_rtc_idtable,
	.remove = mc13xxx_rtc_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

module_platform_driver_probe(mc13xxx_rtc_driver, &mc13xxx_rtc_probe);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("RTC driver for Freescale MC13XXX PMIC");
MODULE_LICENSE("GPL v2");
