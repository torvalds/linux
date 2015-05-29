/*
 * RTC driver for the Armada 38x Marvell SoCs
 *
 * Copyright (C) 2015 Marvell
 *
 * Gregory Clement <gregory.clement@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#define RTC_STATUS	    0x0
#define RTC_STATUS_ALARM1	    BIT(0)
#define RTC_STATUS_ALARM2	    BIT(1)
#define RTC_IRQ1_CONF	    0x4
#define RTC_IRQ1_AL_EN		    BIT(0)
#define RTC_IRQ1_FREQ_EN	    BIT(1)
#define RTC_IRQ1_FREQ_1HZ	    BIT(2)
#define RTC_TIME	    0xC
#define RTC_ALARM1	    0x10

#define SOC_RTC_INTERRUPT   0x8
#define SOC_RTC_ALARM1		BIT(0)
#define SOC_RTC_ALARM2		BIT(1)
#define SOC_RTC_ALARM1_MASK	BIT(2)
#define SOC_RTC_ALARM2_MASK	BIT(3)

struct armada38x_rtc {
	struct rtc_device   *rtc_dev;
	void __iomem	    *regs;
	void __iomem	    *regs_soc;
	spinlock_t	    lock;
	/*
	 * While setting the time, the RTC TIME register should not be
	 * accessed. Setting the RTC time involves sleeping during
	 * 100ms, so a mutex instead of a spinlock is used to protect
	 * it
	 */
	struct mutex	    mutex_time;
	int		    irq;
};

/*
 * According to the datasheet, the OS should wait 5us after every
 * register write to the RTC hard macro so that the required update
 * can occur without holding off the system bus
 */
static void rtc_delayed_write(u32 val, struct armada38x_rtc *rtc, int offset)
{
	writel(val, rtc->regs + offset);
	udelay(5);
}

static int armada38x_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	unsigned long time, time_check, flags;

	mutex_lock(&rtc->mutex_time);
	time = readl(rtc->regs + RTC_TIME);
	/*
	 * WA for failing time set attempts. As stated in HW ERRATA if
	 * more than one second between two time reads is detected
	 * then read once again.
	 */
	time_check = readl(rtc->regs + RTC_TIME);
	if ((time_check - time) > 1)
		time_check = readl(rtc->regs + RTC_TIME);

	mutex_unlock(&rtc->mutex_time);

	rtc_time_to_tm(time_check, tm);

	return 0;
}

static int armada38x_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	int ret = 0;
	unsigned long time, flags;

	ret = rtc_tm_to_time(tm, &time);

	if (ret)
		goto out;
	/*
	 * Setting the RTC time not always succeeds. According to the
	 * errata we need to first write on the status register and
	 * then wait for 100ms before writing to the time register to be
	 * sure that the data will be taken into account.
	 */
	mutex_lock(&rtc->mutex_time);
	rtc_delayed_write(0, rtc, RTC_STATUS);
	msleep(100);
	rtc_delayed_write(time, rtc, RTC_TIME);
	mutex_unlock(&rtc->mutex_time);

out:
	return ret;
}

static int armada38x_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	unsigned long time, flags;
	u32 val;

	spin_lock_irqsave(&rtc->lock, flags);

	time = readl(rtc->regs + RTC_ALARM1);
	val = readl(rtc->regs + RTC_IRQ1_CONF) & RTC_IRQ1_AL_EN;

	spin_unlock_irqrestore(&rtc->lock, flags);

	alrm->enabled = val ? 1 : 0;
	rtc_time_to_tm(time,  &alrm->time);

	return 0;
}

static int armada38x_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	unsigned long time, flags;
	int ret = 0;
	u32 val;

	ret = rtc_tm_to_time(&alrm->time, &time);

	if (ret)
		goto out;

	spin_lock_irqsave(&rtc->lock, flags);

	rtc_delayed_write(time, rtc, RTC_ALARM1);

	if (alrm->enabled) {
			rtc_delayed_write(RTC_IRQ1_AL_EN, rtc, RTC_IRQ1_CONF);
			val = readl(rtc->regs_soc + SOC_RTC_INTERRUPT);
			writel(val | SOC_RTC_ALARM1_MASK,
			       rtc->regs_soc + SOC_RTC_INTERRUPT);
	}

	spin_unlock_irqrestore(&rtc->lock, flags);

out:
	return ret;
}

static int armada38x_rtc_alarm_irq_enable(struct device *dev,
					 unsigned int enabled)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&rtc->lock, flags);

	if (enabled)
		rtc_delayed_write(RTC_IRQ1_AL_EN, rtc, RTC_IRQ1_CONF);
	else
		rtc_delayed_write(0, rtc, RTC_IRQ1_CONF);

	spin_unlock_irqrestore(&rtc->lock, flags);

	return 0;
}

static irqreturn_t armada38x_rtc_alarm_irq(int irq, void *data)
{
	struct armada38x_rtc *rtc = data;
	u32 val;
	int event = RTC_IRQF | RTC_AF;

	dev_dbg(&rtc->rtc_dev->dev, "%s:irq(%d)\n", __func__, irq);

	spin_lock(&rtc->lock);

	val = readl(rtc->regs_soc + SOC_RTC_INTERRUPT);

	writel(val & ~SOC_RTC_ALARM1, rtc->regs_soc + SOC_RTC_INTERRUPT);
	val = readl(rtc->regs + RTC_IRQ1_CONF);
	/* disable all the interrupts for alarm 1 */
	rtc_delayed_write(0, rtc, RTC_IRQ1_CONF);
	/* Ack the event */
	rtc_delayed_write(RTC_STATUS_ALARM1, rtc, RTC_STATUS);

	spin_unlock(&rtc->lock);

	if (val & RTC_IRQ1_FREQ_EN) {
		if (val & RTC_IRQ1_FREQ_1HZ)
			event |= RTC_UF;
		else
			event |= RTC_PF;
	}

	rtc_update_irq(rtc->rtc_dev, 1, event);

	return IRQ_HANDLED;
}

static struct rtc_class_ops armada38x_rtc_ops = {
	.read_time = armada38x_rtc_read_time,
	.set_time = armada38x_rtc_set_time,
	.read_alarm = armada38x_rtc_read_alarm,
	.set_alarm = armada38x_rtc_set_alarm,
	.alarm_irq_enable = armada38x_rtc_alarm_irq_enable,
};

static __init int armada38x_rtc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct armada38x_rtc *rtc;
	int ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(struct armada38x_rtc),
			    GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	spin_lock_init(&rtc->lock);
	mutex_init(&rtc->mutex_time);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rtc");
	rtc->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rtc->regs))
		return PTR_ERR(rtc->regs);
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rtc-soc");
	rtc->regs_soc = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rtc->regs_soc))
		return PTR_ERR(rtc->regs_soc);

	rtc->irq = platform_get_irq(pdev, 0);

	if (rtc->irq < 0) {
		dev_err(&pdev->dev, "no irq\n");
		return rtc->irq;
	}
	if (devm_request_irq(&pdev->dev, rtc->irq, armada38x_rtc_alarm_irq,
				0, pdev->name, rtc) < 0) {
		dev_warn(&pdev->dev, "Interrupt not available.\n");
		rtc->irq = -1;
		/*
		 * If there is no interrupt available then we can't
		 * use the alarm
		 */
		armada38x_rtc_ops.set_alarm = NULL;
		armada38x_rtc_ops.alarm_irq_enable = NULL;
	}
	platform_set_drvdata(pdev, rtc);
	if (rtc->irq != -1)
		device_init_wakeup(&pdev->dev, 1);

	rtc->rtc_dev = devm_rtc_device_register(&pdev->dev, pdev->name,
					&armada38x_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc->rtc_dev)) {
		ret = PTR_ERR(rtc->rtc_dev);
		dev_err(&pdev->dev, "Failed to register RTC device: %d\n", ret);
		return ret;
	}
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int armada38x_rtc_suspend(struct device *dev)
{
	if (device_may_wakeup(dev)) {
		struct armada38x_rtc *rtc = dev_get_drvdata(dev);

		return enable_irq_wake(rtc->irq);
	}

	return 0;
}

static int armada38x_rtc_resume(struct device *dev)
{
	if (device_may_wakeup(dev)) {
		struct armada38x_rtc *rtc = dev_get_drvdata(dev);

		return disable_irq_wake(rtc->irq);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(armada38x_rtc_pm_ops,
			 armada38x_rtc_suspend, armada38x_rtc_resume);

#ifdef CONFIG_OF
static const struct of_device_id armada38x_rtc_of_match_table[] = {
	{ .compatible = "marvell,armada-380-rtc", },
	{}
};
#endif

static struct platform_driver armada38x_rtc_driver = {
	.driver		= {
		.name	= "armada38x-rtc",
		.pm	= &armada38x_rtc_pm_ops,
		.of_match_table = of_match_ptr(armada38x_rtc_of_match_table),
	},
};

module_platform_driver_probe(armada38x_rtc_driver, armada38x_rtc_probe);

MODULE_DESCRIPTION("Marvell Armada 38x RTC driver");
MODULE_AUTHOR("Gregory CLEMENT <gregory.clement@free-electrons.com>");
MODULE_LICENSE("GPL");
