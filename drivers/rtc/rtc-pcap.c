/*
 *  pcap rtc code for Motorola EZX phones
 *
 *  Copyright (c) 2008 guiming zhuo <gmzhuo@gmail.com>
 *  Copyright (c) 2009 Daniel Ribeiro <drwyrm@gmail.com>
 *
 *  Based on Motorola's rtc.c Copyright (c) 2003-2005 Motorola
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mfd/ezx-pcap.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

struct pcap_rtc {
	struct pcap_chip *pcap;
	struct rtc_device *rtc;
};

static irqreturn_t pcap_rtc_irq(int irq, void *_pcap_rtc)
{
	struct pcap_rtc *pcap_rtc = _pcap_rtc;
	unsigned long rtc_events;

	if (irq == pcap_to_irq(pcap_rtc->pcap, PCAP_IRQ_1HZ))
		rtc_events = RTC_IRQF | RTC_UF;
	else if (irq == pcap_to_irq(pcap_rtc->pcap, PCAP_IRQ_TODA))
		rtc_events = RTC_IRQF | RTC_AF;
	else
		rtc_events = 0;

	rtc_update_irq(pcap_rtc->rtc, 1, rtc_events);
	return IRQ_HANDLED;
}

static int pcap_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pcap_rtc *pcap_rtc = platform_get_drvdata(pdev);
	struct rtc_time *tm = &alrm->time;
	unsigned long secs;
	u32 tod;	/* time of day, seconds since midnight */
	u32 days;	/* days since 1/1/1970 */

	ezx_pcap_read(pcap_rtc->pcap, PCAP_REG_RTC_TODA, &tod);
	secs = tod & PCAP_RTC_TOD_MASK;

	ezx_pcap_read(pcap_rtc->pcap, PCAP_REG_RTC_DAYA, &days);
	secs += (days & PCAP_RTC_DAY_MASK) * SEC_PER_DAY;

	rtc_time_to_tm(secs, tm);

	return 0;
}

static int pcap_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pcap_rtc *pcap_rtc = platform_get_drvdata(pdev);
	struct rtc_time *tm = &alrm->time;
	unsigned long secs;
	u32 tod, days;

	rtc_tm_to_time(tm, &secs);

	tod = secs % SEC_PER_DAY;
	ezx_pcap_write(pcap_rtc->pcap, PCAP_REG_RTC_TODA, tod);

	days = secs / SEC_PER_DAY;
	ezx_pcap_write(pcap_rtc->pcap, PCAP_REG_RTC_DAYA, days);

	return 0;
}

static int pcap_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pcap_rtc *pcap_rtc = platform_get_drvdata(pdev);
	unsigned long secs;
	u32 tod, days;

	ezx_pcap_read(pcap_rtc->pcap, PCAP_REG_RTC_TOD, &tod);
	secs = tod & PCAP_RTC_TOD_MASK;

	ezx_pcap_read(pcap_rtc->pcap, PCAP_REG_RTC_DAY, &days);
	secs += (days & PCAP_RTC_DAY_MASK) * SEC_PER_DAY;

	rtc_time_to_tm(secs, tm);

	return rtc_valid_tm(tm);
}

static int pcap_rtc_set_mmss(struct device *dev, unsigned long secs)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pcap_rtc *pcap_rtc = platform_get_drvdata(pdev);
	u32 tod, days;

	tod = secs % SEC_PER_DAY;
	ezx_pcap_write(pcap_rtc->pcap, PCAP_REG_RTC_TOD, tod);

	days = secs / SEC_PER_DAY;
	ezx_pcap_write(pcap_rtc->pcap, PCAP_REG_RTC_DAY, days);

	return 0;
}

static int pcap_rtc_irq_enable(struct device *dev, int pirq, unsigned int en)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pcap_rtc *pcap_rtc = platform_get_drvdata(pdev);

	if (en)
		enable_irq(pcap_to_irq(pcap_rtc->pcap, pirq));
	else
		disable_irq(pcap_to_irq(pcap_rtc->pcap, pirq));

	return 0;
}

static int pcap_rtc_alarm_irq_enable(struct device *dev, unsigned int en)
{
	return pcap_rtc_irq_enable(dev, PCAP_IRQ_TODA, en);
}

static const struct rtc_class_ops pcap_rtc_ops = {
	.read_time = pcap_rtc_read_time,
	.read_alarm = pcap_rtc_read_alarm,
	.set_alarm = pcap_rtc_set_alarm,
	.set_mmss = pcap_rtc_set_mmss,
	.alarm_irq_enable = pcap_rtc_alarm_irq_enable,
};

static int __init pcap_rtc_probe(struct platform_device *pdev)
{
	struct pcap_rtc *pcap_rtc;
	int timer_irq, alarm_irq;
	int err = -ENOMEM;

	pcap_rtc = devm_kzalloc(&pdev->dev, sizeof(struct pcap_rtc),
				GFP_KERNEL);
	if (!pcap_rtc)
		return err;

	pcap_rtc->pcap = dev_get_drvdata(pdev->dev.parent);

	platform_set_drvdata(pdev, pcap_rtc);

	pcap_rtc->rtc = devm_rtc_device_register(&pdev->dev, "pcap",
					&pcap_rtc_ops, THIS_MODULE);
	if (IS_ERR(pcap_rtc->rtc))
		return PTR_ERR(pcap_rtc->rtc);

	timer_irq = pcap_to_irq(pcap_rtc->pcap, PCAP_IRQ_1HZ);
	alarm_irq = pcap_to_irq(pcap_rtc->pcap, PCAP_IRQ_TODA);

	err = devm_request_irq(&pdev->dev, timer_irq, pcap_rtc_irq, 0,
				"RTC Timer", pcap_rtc);
	if (err)
		return err;

	err = devm_request_irq(&pdev->dev, alarm_irq, pcap_rtc_irq, 0,
				"RTC Alarm", pcap_rtc);
	if (err)
		return err;

	return 0;
}

static int __exit pcap_rtc_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver pcap_rtc_driver = {
	.remove = __exit_p(pcap_rtc_remove),
	.driver = {
		.name  = "pcap-rtc",
	},
};

module_platform_driver_probe(pcap_rtc_driver, pcap_rtc_probe);

MODULE_DESCRIPTION("Motorola pcap rtc driver");
MODULE_AUTHOR("guiming zhuo <gmzhuo@gmail.com>");
MODULE_LICENSE("GPL");
