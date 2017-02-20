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
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#define RTC_STATUS	    0x0
#define RTC_STATUS_ALARM1	    BIT(0)
#define RTC_STATUS_ALARM2	    BIT(1)
#define RTC_IRQ1_CONF	    0x4
#define RTC_IRQ_AL_EN		    BIT(0)
#define RTC_IRQ_FREQ_EN		    BIT(1)
#define RTC_IRQ_FREQ_1HZ	    BIT(2)

#define RTC_TIME	    0xC
#define RTC_ALARM1	    0x10
#define RTC_38X_BRIDGE_TIMING_CTL   0x0
#define RTC_38X_PERIOD_OFFS		0
#define RTC_38X_PERIOD_MASK		(0x3FF << RTC_38X_PERIOD_OFFS)
#define RTC_38X_READ_DELAY_OFFS		26
#define RTC_38X_READ_DELAY_MASK		(0x1F << RTC_38X_READ_DELAY_OFFS)

#define SOC_RTC_INTERRUPT	    0x8
#define SOC_RTC_ALARM1			BIT(0)
#define SOC_RTC_ALARM2			BIT(1)
#define SOC_RTC_ALARM1_MASK		BIT(2)
#define SOC_RTC_ALARM2_MASK		BIT(3)

#define SAMPLE_NR 100

struct value_to_freq {
	u32 value;
	u8 freq;
};

struct armada38x_rtc {
	struct rtc_device   *rtc_dev;
	void __iomem	    *regs;
	void __iomem	    *regs_soc;
	spinlock_t	    lock;
	int		    irq;
	struct value_to_freq *val_to_freq;
	struct armada38x_rtc_data *data;
};

#define ALARM1	0
#define ALARM_REG(base, alarm)	 ((base) + (alarm) * sizeof(u32))

struct armada38x_rtc_data {
	/* Initialize the RTC-MBUS bridge timing */
	void (*update_mbus_timing)(struct armada38x_rtc *rtc);
	u32 (*read_rtc_reg)(struct armada38x_rtc *rtc, u8 rtc_reg);
	void (*clear_isr)(struct armada38x_rtc *rtc);
	void (*unmask_interrupt)(struct armada38x_rtc *rtc);
	u32 alarm;
};

/*
 * According to the datasheet, the OS should wait 5us after every
 * register write to the RTC hard macro so that the required update
 * can occur without holding off the system bus
 * According to errata RES-3124064, Write to any RTC register
 * may fail. As a workaround, before writing to RTC
 * register, issue a dummy write of 0x0 twice to RTC Status
 * register.
 */

static void rtc_delayed_write(u32 val, struct armada38x_rtc *rtc, int offset)
{
	writel(0, rtc->regs + RTC_STATUS);
	writel(0, rtc->regs + RTC_STATUS);
	writel(val, rtc->regs + offset);
	udelay(5);
}

/* Update RTC-MBUS bridge timing parameters */
static void rtc_update_38x_mbus_timing_params(struct armada38x_rtc *rtc)
{
	u32 reg;

	reg = readl(rtc->regs_soc + RTC_38X_BRIDGE_TIMING_CTL);
	reg &= ~RTC_38X_PERIOD_MASK;
	reg |= 0x3FF << RTC_38X_PERIOD_OFFS; /* Maximum value */
	reg &= ~RTC_38X_READ_DELAY_MASK;
	reg |= 0x1F << RTC_38X_READ_DELAY_OFFS; /* Maximum value */
	writel(reg, rtc->regs_soc + RTC_38X_BRIDGE_TIMING_CTL);
}

static u32 read_rtc_register_38x_wa(struct armada38x_rtc *rtc, u8 rtc_reg)
{
	int i, index_max = 0, max = 0;

	for (i = 0; i < SAMPLE_NR; i++) {
		rtc->val_to_freq[i].value = readl(rtc->regs + rtc_reg);
		rtc->val_to_freq[i].freq = 0;
	}

	for (i = 0; i < SAMPLE_NR; i++) {
		int j = 0;
		u32 value = rtc->val_to_freq[i].value;

		while (rtc->val_to_freq[j].freq) {
			if (rtc->val_to_freq[j].value == value) {
				rtc->val_to_freq[j].freq++;
				break;
			}
			j++;
		}

		if (!rtc->val_to_freq[j].freq) {
			rtc->val_to_freq[j].value = value;
			rtc->val_to_freq[j].freq = 1;
		}

		if (rtc->val_to_freq[j].freq > max) {
			index_max = j;
			max = rtc->val_to_freq[j].freq;
		}

		/*
		 * If a value already has half of the sample this is the most
		 * frequent one and we can stop the research right now
		 */
		if (max > SAMPLE_NR / 2)
			break;
	}

	return rtc->val_to_freq[index_max].value;
}

static void armada38x_clear_isr(struct armada38x_rtc *rtc)
{
	u32 val = readl(rtc->regs_soc + SOC_RTC_INTERRUPT);

	writel(val & ~SOC_RTC_ALARM1, rtc->regs_soc + SOC_RTC_INTERRUPT);
}

static void armada38x_unmask_interrupt(struct armada38x_rtc *rtc)
{
	u32 val = readl(rtc->regs_soc + SOC_RTC_INTERRUPT);

	writel(val | SOC_RTC_ALARM1_MASK, rtc->regs_soc + SOC_RTC_INTERRUPT);
}
static int armada38x_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	unsigned long time, flags;

	spin_lock_irqsave(&rtc->lock, flags);
	time = rtc->data->read_rtc_reg(rtc, RTC_TIME);
	spin_unlock_irqrestore(&rtc->lock, flags);

	rtc_time_to_tm(time, tm);

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

	spin_lock_irqsave(&rtc->lock, flags);
	rtc_delayed_write(time, rtc, RTC_TIME);
	spin_unlock_irqrestore(&rtc->lock, flags);

out:
	return ret;
}

static int armada38x_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	unsigned long time, flags;
	u32 reg = ALARM_REG(RTC_ALARM1, rtc->data->alarm);
	u32 reg_irq = ALARM_REG(RTC_IRQ1_CONF, rtc->data->alarm);
	u32 val;

	spin_lock_irqsave(&rtc->lock, flags);

	time = rtc->data->read_rtc_reg(rtc, reg);
	val = rtc->data->read_rtc_reg(rtc, reg_irq) & RTC_IRQ_AL_EN;

	spin_unlock_irqrestore(&rtc->lock, flags);

	alrm->enabled = val ? 1 : 0;
	rtc_time_to_tm(time,  &alrm->time);

	return 0;
}

static int armada38x_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	u32 reg = ALARM_REG(RTC_ALARM1, rtc->data->alarm);
	u32 reg_irq = ALARM_REG(RTC_IRQ1_CONF, rtc->data->alarm);
	unsigned long time, flags;
	int ret = 0;

	ret = rtc_tm_to_time(&alrm->time, &time);

	if (ret)
		goto out;

	spin_lock_irqsave(&rtc->lock, flags);

	rtc_delayed_write(time, rtc, reg);

	if (alrm->enabled) {
		rtc_delayed_write(RTC_IRQ_AL_EN, rtc, reg_irq);
		rtc->data->unmask_interrupt(rtc);
	}

	spin_unlock_irqrestore(&rtc->lock, flags);

out:
	return ret;
}

static int armada38x_rtc_alarm_irq_enable(struct device *dev,
					 unsigned int enabled)
{
	struct armada38x_rtc *rtc = dev_get_drvdata(dev);
	u32 reg_irq = ALARM_REG(RTC_IRQ1_CONF, rtc->data->alarm);
	unsigned long flags;

	spin_lock_irqsave(&rtc->lock, flags);

	if (enabled)
		rtc_delayed_write(RTC_IRQ_AL_EN, rtc, reg_irq);
	else
		rtc_delayed_write(0, rtc, reg_irq);

	spin_unlock_irqrestore(&rtc->lock, flags);

	return 0;
}

static irqreturn_t armada38x_rtc_alarm_irq(int irq, void *data)
{
	struct armada38x_rtc *rtc = data;
	u32 val;
	int event = RTC_IRQF | RTC_AF;
	u32 reg_irq = ALARM_REG(RTC_IRQ1_CONF, rtc->data->alarm);

	dev_dbg(&rtc->rtc_dev->dev, "%s:irq(%d)\n", __func__, irq);

	spin_lock(&rtc->lock);

	rtc->data->clear_isr(rtc);
	val = rtc->data->read_rtc_reg(rtc, reg_irq);
	/* disable all the interrupts for alarm*/
	rtc_delayed_write(0, rtc, reg_irq);
	/* Ack the event */
	rtc_delayed_write(1 << rtc->data->alarm, rtc, RTC_STATUS);

	spin_unlock(&rtc->lock);

	if (val & RTC_IRQ_FREQ_EN) {
		if (val & RTC_IRQ_FREQ_1HZ)
			event |= RTC_UF;
		else
			event |= RTC_PF;
	}

	rtc_update_irq(rtc->rtc_dev, 1, event);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops armada38x_rtc_ops = {
	.read_time = armada38x_rtc_read_time,
	.set_time = armada38x_rtc_set_time,
	.read_alarm = armada38x_rtc_read_alarm,
	.set_alarm = armada38x_rtc_set_alarm,
	.alarm_irq_enable = armada38x_rtc_alarm_irq_enable,
};

static const struct rtc_class_ops armada38x_rtc_ops_noirq = {
	.read_time = armada38x_rtc_read_time,
	.set_time = armada38x_rtc_set_time,
	.read_alarm = armada38x_rtc_read_alarm,
};

static const struct armada38x_rtc_data armada38x_data = {
	.update_mbus_timing = rtc_update_38x_mbus_timing_params,
	.read_rtc_reg = read_rtc_register_38x_wa,
	.clear_isr = armada38x_clear_isr,
	.unmask_interrupt = armada38x_unmask_interrupt,
	.alarm = ALARM1,
};

#ifdef CONFIG_OF
static const struct of_device_id armada38x_rtc_of_match_table[] = {
	{
		.compatible = "marvell,armada-380-rtc",
		.data = &armada38x_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, armada38x_rtc_of_match_table);
#endif

static __init int armada38x_rtc_probe(struct platform_device *pdev)
{
	const struct rtc_class_ops *ops;
	struct resource *res;
	struct armada38x_rtc *rtc;
	const struct of_device_id *match;
	int ret;

	match = of_match_device(armada38x_rtc_of_match_table, &pdev->dev);
	if (!match)
		return -ENODEV;

	rtc = devm_kzalloc(&pdev->dev, sizeof(struct armada38x_rtc),
			    GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->val_to_freq = devm_kcalloc(&pdev->dev, SAMPLE_NR,
				sizeof(struct value_to_freq), GFP_KERNEL);
	if (!rtc->val_to_freq)
		return -ENOMEM;

	spin_lock_init(&rtc->lock);

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
	}
	platform_set_drvdata(pdev, rtc);

	if (rtc->irq != -1) {
		device_init_wakeup(&pdev->dev, 1);
		ops = &armada38x_rtc_ops;
	} else {
		/*
		 * If there is no interrupt available then we can't
		 * use the alarm
		 */
		ops = &armada38x_rtc_ops_noirq;
	}
	rtc->data = (struct armada38x_rtc_data *)match->data;


	/* Update RTC-MBUS bridge timing parameters */
	rtc->data->update_mbus_timing(rtc);

	rtc->rtc_dev = devm_rtc_device_register(&pdev->dev, pdev->name,
						ops, THIS_MODULE);
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

		/* Update RTC-MBUS bridge timing parameters */
		rtc->data->update_mbus_timing(rtc);

		return disable_irq_wake(rtc->irq);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(armada38x_rtc_pm_ops,
			 armada38x_rtc_suspend, armada38x_rtc_resume);

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
