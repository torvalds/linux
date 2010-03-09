/*
 * Real Time Clock driver for Freescale MC13783 PMIC
 *
 * (C) 2009 Sascha Hauer, Pengutronix
 * (C) 2009 Uwe Kleine-Koenig, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mfd/mc13783.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtc.h>

#define DRIVER_NAME "mc13783-rtc"

#define MC13783_RTCTOD	20
#define MC13783_RTCTODA	21
#define MC13783_RTCDAY	22
#define MC13783_RTCDAYA	23

struct mc13783_rtc {
	struct rtc_device *rtc;
	struct mc13783 *mc13783;
	int valid;
};

static int mc13783_rtc_irq_enable_unlocked(struct device *dev,
		unsigned int enabled, int irq)
{
	struct mc13783_rtc *priv = dev_get_drvdata(dev);
	int (*func)(struct mc13783 *mc13783, int irq);

	if (!priv->valid)
		return -ENODATA;

	func = enabled ? mc13783_irq_unmask : mc13783_irq_mask;
	return func(priv->mc13783, irq);
}

static int mc13783_rtc_irq_enable(struct device *dev,
		unsigned int enabled, int irq)
{
	struct mc13783_rtc *priv = dev_get_drvdata(dev);
	int ret;

	mc13783_lock(priv->mc13783);

	ret = mc13783_rtc_irq_enable_unlocked(dev, enabled, irq);

	mc13783_unlock(priv->mc13783);

	return ret;
}

static int mc13783_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct mc13783_rtc *priv = dev_get_drvdata(dev);
	unsigned int seconds, days1, days2;
	unsigned long s1970;
	int ret;

	mc13783_lock(priv->mc13783);

	if (!priv->valid) {
		ret = -ENODATA;
		goto out;
	}

	ret = mc13783_reg_read(priv->mc13783, MC13783_RTCDAY, &days1);
	if (unlikely(ret))
		goto out;

	ret = mc13783_reg_read(priv->mc13783, MC13783_RTCTOD, &seconds);
	if (unlikely(ret))
		goto out;

	ret = mc13783_reg_read(priv->mc13783, MC13783_RTCDAY, &days2);
out:
	mc13783_unlock(priv->mc13783);

	if (ret)
		return ret;

	if (days2 == days1 + 1) {
		if (seconds >= 86400 / 2)
			days2 = days1;
		else
			days1 = days2;
	}

	if (days1 != days2)
		return -EIO;

	s1970 = days1 * 86400 + seconds;

	rtc_time_to_tm(s1970, tm);

	return rtc_valid_tm(tm);
}

static int mc13783_rtc_set_mmss(struct device *dev, unsigned long secs)
{
	struct mc13783_rtc *priv = dev_get_drvdata(dev);
	unsigned int seconds, days;
	unsigned int alarmseconds;
	int ret;

	seconds = secs % 86400;
	days = secs / 86400;

	mc13783_lock(priv->mc13783);

	/*
	 * temporarily invalidate alarm to prevent triggering it when the day is
	 * already updated while the time isn't yet.
	 */
	ret = mc13783_reg_read(priv->mc13783, MC13783_RTCTODA, &alarmseconds);
	if (unlikely(ret))
		goto out;

	if (alarmseconds < 86400) {
		ret = mc13783_reg_write(priv->mc13783,
				MC13783_RTCTODA, 0x1ffff);
		if (unlikely(ret))
			goto out;
	}

	/*
	 * write seconds=0 to prevent a day switch between writing days
	 * and seconds below
	 */
	ret = mc13783_reg_write(priv->mc13783, MC13783_RTCTOD, 0);
	if (unlikely(ret))
		goto out;

	ret = mc13783_reg_write(priv->mc13783, MC13783_RTCDAY, days);
	if (unlikely(ret))
		goto out;

	ret = mc13783_reg_write(priv->mc13783, MC13783_RTCTOD, seconds);
	if (unlikely(ret))
		goto out;

	/* restore alarm */
	if (alarmseconds < 86400) {
		ret = mc13783_reg_write(priv->mc13783,
				MC13783_RTCTODA, alarmseconds);
		if (unlikely(ret))
			goto out;
	}

	ret = mc13783_irq_ack(priv->mc13783, MC13783_IRQ_RTCRST);
	if (unlikely(ret))
		goto out;

	ret = mc13783_irq_unmask(priv->mc13783, MC13783_IRQ_RTCRST);
out:
	priv->valid = !ret;

	mc13783_unlock(priv->mc13783);

	return ret;
}

static int mc13783_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct mc13783_rtc *priv = dev_get_drvdata(dev);
	unsigned seconds, days;
	unsigned long s1970;
	int enabled, pending;
	int ret;

	mc13783_lock(priv->mc13783);

	ret = mc13783_reg_read(priv->mc13783, MC13783_RTCTODA, &seconds);
	if (unlikely(ret))
		goto out;
	if (seconds >= 86400) {
		ret = -ENODATA;
		goto out;
	}

	ret = mc13783_reg_read(priv->mc13783, MC13783_RTCDAY, &days);
	if (unlikely(ret))
		goto out;

	ret = mc13783_irq_status(priv->mc13783, MC13783_IRQ_TODA,
			&enabled, &pending);

out:
	mc13783_unlock(priv->mc13783);

	if (ret)
		return ret;

	alarm->enabled = enabled;
	alarm->pending = pending;

	s1970 = days * 86400 + seconds;

	rtc_time_to_tm(s1970, &alarm->time);
	dev_dbg(dev, "%s: %lu\n", __func__, s1970);

	return 0;
}

static int mc13783_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct mc13783_rtc *priv = dev_get_drvdata(dev);
	unsigned long s1970;
	unsigned seconds, days;
	int ret;

	mc13783_lock(priv->mc13783);

	/* disable alarm to prevent false triggering */
	ret = mc13783_reg_write(priv->mc13783, MC13783_RTCTODA, 0x1ffff);
	if (unlikely(ret))
		goto out;

	ret = mc13783_irq_ack(priv->mc13783, MC13783_IRQ_TODA);
	if (unlikely(ret))
		goto out;

	ret = rtc_tm_to_time(&alarm->time, &s1970);
	if (unlikely(ret))
		goto out;

	dev_dbg(dev, "%s: o%2.s %lu\n", __func__, alarm->enabled ? "n" : "ff",
			s1970);

	ret = mc13783_rtc_irq_enable_unlocked(dev, alarm->enabled,
			MC13783_IRQ_TODA);
	if (unlikely(ret))
		goto out;

	seconds = s1970 % 86400;
	days = s1970 / 86400;

	ret = mc13783_reg_write(priv->mc13783, MC13783_RTCDAYA, days);
	if (unlikely(ret))
		goto out;

	ret = mc13783_reg_write(priv->mc13783, MC13783_RTCTODA, seconds);

out:
	mc13783_unlock(priv->mc13783);

	return ret;
}

static irqreturn_t mc13783_rtc_alarm_handler(int irq, void *dev)
{
	struct mc13783_rtc *priv = dev;
	struct mc13783 *mc13783 = priv->mc13783;

	dev_dbg(&priv->rtc->dev, "Alarm\n");

	rtc_update_irq(priv->rtc, 1, RTC_IRQF | RTC_AF);

	mc13783_irq_ack(mc13783, irq);

	return IRQ_HANDLED;
}

static irqreturn_t mc13783_rtc_update_handler(int irq, void *dev)
{
	struct mc13783_rtc *priv = dev;
	struct mc13783 *mc13783 = priv->mc13783;

	dev_dbg(&priv->rtc->dev, "1HZ\n");

	rtc_update_irq(priv->rtc, 1, RTC_IRQF | RTC_UF);

	mc13783_irq_ack(mc13783, irq);

	return IRQ_HANDLED;
}

static int mc13783_rtc_update_irq_enable(struct device *dev,
		unsigned int enabled)
{
	return mc13783_rtc_irq_enable(dev, enabled, MC13783_IRQ_1HZ);
}

static int mc13783_rtc_alarm_irq_enable(struct device *dev,
		unsigned int enabled)
{
	return mc13783_rtc_irq_enable(dev, enabled, MC13783_IRQ_TODA);
}

static const struct rtc_class_ops mc13783_rtc_ops = {
	.read_time = mc13783_rtc_read_time,
	.set_mmss = mc13783_rtc_set_mmss,
	.read_alarm = mc13783_rtc_read_alarm,
	.set_alarm = mc13783_rtc_set_alarm,
	.alarm_irq_enable = mc13783_rtc_alarm_irq_enable,
	.update_irq_enable = mc13783_rtc_update_irq_enable,
};

static irqreturn_t mc13783_rtc_reset_handler(int irq, void *dev)
{
	struct mc13783_rtc *priv = dev;
	struct mc13783 *mc13783 = priv->mc13783;

	dev_dbg(&priv->rtc->dev, "RTCRST\n");
	priv->valid = 0;

	mc13783_irq_mask(mc13783, irq);

	return IRQ_HANDLED;
}

static int __devinit mc13783_rtc_probe(struct platform_device *pdev)
{
	int ret;
	struct mc13783_rtc *priv;
	int rtcrst_pending;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mc13783 = dev_get_drvdata(pdev->dev.parent);
	platform_set_drvdata(pdev, priv);

	mc13783_lock(priv->mc13783);

	ret = mc13783_irq_request(priv->mc13783, MC13783_IRQ_RTCRST,
			mc13783_rtc_reset_handler, DRIVER_NAME, priv);
	if (ret)
		goto err_reset_irq_request;

	ret = mc13783_irq_status(priv->mc13783, MC13783_IRQ_RTCRST,
			NULL, &rtcrst_pending);
	if (ret)
		goto err_reset_irq_status;

	priv->valid = !rtcrst_pending;

	ret = mc13783_irq_request_nounmask(priv->mc13783, MC13783_IRQ_1HZ,
			mc13783_rtc_update_handler, DRIVER_NAME, priv);
	if (ret)
		goto err_update_irq_request;

	ret = mc13783_irq_request_nounmask(priv->mc13783, MC13783_IRQ_TODA,
			mc13783_rtc_alarm_handler, DRIVER_NAME, priv);
	if (ret)
		goto err_alarm_irq_request;

	priv->rtc = rtc_device_register(pdev->name,
			&pdev->dev, &mc13783_rtc_ops, THIS_MODULE);
	if (IS_ERR(priv->rtc)) {
		ret = PTR_ERR(priv->rtc);

		mc13783_irq_free(priv->mc13783, MC13783_IRQ_TODA, priv);
err_alarm_irq_request:

		mc13783_irq_free(priv->mc13783, MC13783_IRQ_1HZ, priv);
err_update_irq_request:

err_reset_irq_status:

		mc13783_irq_free(priv->mc13783, MC13783_IRQ_RTCRST, priv);
err_reset_irq_request:

		platform_set_drvdata(pdev, NULL);
		kfree(priv);
	}

	mc13783_unlock(priv->mc13783);

	return ret;
}

static int __devexit mc13783_rtc_remove(struct platform_device *pdev)
{
	struct mc13783_rtc *priv = platform_get_drvdata(pdev);

	mc13783_lock(priv->mc13783);

	rtc_device_unregister(priv->rtc);

	mc13783_irq_free(priv->mc13783, MC13783_IRQ_TODA, priv);
	mc13783_irq_free(priv->mc13783, MC13783_IRQ_1HZ, priv);
	mc13783_irq_free(priv->mc13783, MC13783_IRQ_RTCRST, priv);

	mc13783_unlock(priv->mc13783);

	platform_set_drvdata(pdev, NULL);

	kfree(priv);

	return 0;
}

static struct platform_driver mc13783_rtc_driver = {
	.remove = __devexit_p(mc13783_rtc_remove),
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init mc13783_rtc_init(void)
{
	return platform_driver_probe(&mc13783_rtc_driver, &mc13783_rtc_probe);
}
module_init(mc13783_rtc_init);

static void __exit mc13783_rtc_exit(void)
{
	platform_driver_unregister(&mc13783_rtc_driver);
}
module_exit(mc13783_rtc_exit);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("RTC driver for Freescale MC13783 PMIC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
