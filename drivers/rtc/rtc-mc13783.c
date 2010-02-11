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
	int ret;

	seconds = secs % 86400;
	days = secs / 86400;

	mc13783_lock(priv->mc13783);

	/*
	 * first write seconds=0 to prevent a day switch between writing days
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

	ret = mc13783_ackirq(priv->mc13783, MC13783_IRQ_RTCRST);
	if (unlikely(ret))
		goto out;

	ret = mc13783_unmask(priv->mc13783, MC13783_IRQ_RTCRST);
out:
	priv->valid = !ret;

	mc13783_unlock(priv->mc13783);

	return ret;
}

static irqreturn_t mc13783_rtc_update_handler(int irq, void *dev)
{
	struct mc13783_rtc *priv = dev;
	struct mc13783 *mc13783 = priv->mc13783;

	dev_dbg(&priv->rtc->dev, "1HZ\n");

	rtc_update_irq(priv->rtc, 1, RTC_IRQF | RTC_UF);

	mc13783_ackirq(mc13783, irq);

	return IRQ_HANDLED;
}

static int mc13783_rtc_update_irq_enable(struct device *dev,
		unsigned int enabled)
{
	struct mc13783_rtc *priv = dev_get_drvdata(dev);
	int ret = -ENODATA;

	mc13783_lock(priv->mc13783);
	if (!priv->valid)
		goto out;

	ret = (enabled ? mc13783_unmask : mc13783_mask)(priv->mc13783,
			MC13783_IRQ_1HZ);
out:
	mc13783_unlock(priv->mc13783);

	return ret;
}

static const struct rtc_class_ops mc13783_rtc_ops = {
	.read_time = mc13783_rtc_read_time,
	.set_mmss = mc13783_rtc_set_mmss,
	.update_irq_enable = mc13783_rtc_update_irq_enable,
};

static irqreturn_t mc13783_rtc_reset_handler(int irq, void *dev)
{
	struct mc13783_rtc *priv = dev;
	struct mc13783 *mc13783 = priv->mc13783;

	dev_dbg(&priv->rtc->dev, "RTCRST\n");
	priv->valid = 0;

	mc13783_mask(mc13783, irq);

	return IRQ_HANDLED;
}

static int __devinit mc13783_rtc_probe(struct platform_device *pdev)
{
	int ret;
	struct mc13783_rtc *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mc13783 = dev_get_drvdata(pdev->dev.parent);
	platform_set_drvdata(pdev, priv);

	priv->valid = 1;

	mc13783_lock(priv->mc13783);

	ret = mc13783_irq_request(priv->mc13783, MC13783_IRQ_RTCRST,
			mc13783_rtc_reset_handler, DRIVER_NAME, priv);
	if (ret)
		goto err_reset_irq_request;

	ret = mc13783_irq_request_nounmask(priv->mc13783, MC13783_IRQ_1HZ,
			mc13783_rtc_update_handler, DRIVER_NAME, priv);
	if (ret)
		goto err_update_irq_request;

	mc13783_unlock(priv->mc13783);

	priv->rtc = rtc_device_register(pdev->name,
			&pdev->dev, &mc13783_rtc_ops, THIS_MODULE);

	if (IS_ERR(priv->rtc)) {
		ret = PTR_ERR(priv->rtc);

		mc13783_lock(priv->mc13783);

		mc13783_irq_free(priv->mc13783, MC13783_IRQ_1HZ, priv);
err_update_irq_request:

		mc13783_irq_free(priv->mc13783, MC13783_IRQ_RTCRST, priv);
err_reset_irq_request:

		mc13783_unlock(priv->mc13783);

		platform_set_drvdata(pdev, NULL);
		kfree(priv);
	}

	return ret;
}

static int __devexit mc13783_rtc_remove(struct platform_device *pdev)
{
	struct mc13783_rtc *priv = platform_get_drvdata(pdev);

	rtc_device_unregister(priv->rtc);

	mc13783_lock(priv->mc13783);

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
