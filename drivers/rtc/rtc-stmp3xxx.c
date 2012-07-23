/*
 * Freescale STMP37XX/STMP378X Real Time Clock driver
 *
 * Copyright (c) 2007 Sigmatel, Inc.
 * Peter Hartley, <peter.hartley@sigmatel.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 * Copyright 2011 Wolfram Sang, Pengutronix e.K.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/of_device.h>

#include <mach/common.h>

#define STMP3XXX_RTC_CTRL			0x0
#define STMP3XXX_RTC_CTRL_SET			0x4
#define STMP3XXX_RTC_CTRL_CLR			0x8
#define STMP3XXX_RTC_CTRL_ALARM_IRQ_EN		0x00000001
#define STMP3XXX_RTC_CTRL_ONEMSEC_IRQ_EN	0x00000002
#define STMP3XXX_RTC_CTRL_ALARM_IRQ		0x00000004

#define STMP3XXX_RTC_STAT			0x10
#define STMP3XXX_RTC_STAT_STALE_SHIFT		16
#define STMP3XXX_RTC_STAT_RTC_PRESENT		0x80000000

#define STMP3XXX_RTC_SECONDS			0x30

#define STMP3XXX_RTC_ALARM			0x40

#define STMP3XXX_RTC_PERSISTENT0		0x60
#define STMP3XXX_RTC_PERSISTENT0_SET		0x64
#define STMP3XXX_RTC_PERSISTENT0_CLR		0x68
#define STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE_EN	0x00000002
#define STMP3XXX_RTC_PERSISTENT0_ALARM_EN	0x00000004
#define STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE	0x00000080

struct stmp3xxx_rtc_data {
	struct rtc_device *rtc;
	void __iomem *io;
	int irq_alarm;
};

static void stmp3xxx_wait_time(struct stmp3xxx_rtc_data *rtc_data)
{
	/*
	 * The datasheet doesn't say which way round the
	 * NEW_REGS/STALE_REGS bitfields go. In fact it's 0x1=P0,
	 * 0x2=P1, .., 0x20=P5, 0x40=ALARM, 0x80=SECONDS
	 */
	while (readl(rtc_data->io + STMP3XXX_RTC_STAT) &
			(0x80 << STMP3XXX_RTC_STAT_STALE_SHIFT))
		cpu_relax();
}

/* Time read/write */
static int stmp3xxx_rtc_gettime(struct device *dev, struct rtc_time *rtc_tm)
{
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev);

	stmp3xxx_wait_time(rtc_data);
	rtc_time_to_tm(readl(rtc_data->io + STMP3XXX_RTC_SECONDS), rtc_tm);
	return 0;
}

static int stmp3xxx_rtc_set_mmss(struct device *dev, unsigned long t)
{
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev);

	writel(t, rtc_data->io + STMP3XXX_RTC_SECONDS);
	stmp3xxx_wait_time(rtc_data);
	return 0;
}

/* interrupt(s) handler */
static irqreturn_t stmp3xxx_rtc_interrupt(int irq, void *dev_id)
{
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev_id);
	u32 status = readl(rtc_data->io + STMP3XXX_RTC_CTRL);

	if (status & STMP3XXX_RTC_CTRL_ALARM_IRQ) {
		writel(STMP3XXX_RTC_CTRL_ALARM_IRQ,
				rtc_data->io + STMP3XXX_RTC_CTRL_CLR);
		rtc_update_irq(rtc_data->rtc, 1, RTC_AF | RTC_IRQF);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int stmp3xxx_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev);

	if (enabled) {
		writel(STMP3XXX_RTC_PERSISTENT0_ALARM_EN |
				STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE_EN,
				rtc_data->io + STMP3XXX_RTC_PERSISTENT0_SET);
		writel(STMP3XXX_RTC_CTRL_ALARM_IRQ_EN,
				rtc_data->io + STMP3XXX_RTC_CTRL_SET);
	} else {
		writel(STMP3XXX_RTC_PERSISTENT0_ALARM_EN |
				STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE_EN,
				rtc_data->io + STMP3XXX_RTC_PERSISTENT0_CLR);
		writel(STMP3XXX_RTC_CTRL_ALARM_IRQ_EN,
				rtc_data->io + STMP3XXX_RTC_CTRL_CLR);
	}
	return 0;
}

static int stmp3xxx_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev);

	rtc_time_to_tm(readl(rtc_data->io + STMP3XXX_RTC_ALARM), &alm->time);
	return 0;
}

static int stmp3xxx_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned long t;
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev);

	rtc_tm_to_time(&alm->time, &t);
	writel(t, rtc_data->io + STMP3XXX_RTC_ALARM);

	stmp3xxx_alarm_irq_enable(dev, alm->enabled);

	return 0;
}

static struct rtc_class_ops stmp3xxx_rtc_ops = {
	.alarm_irq_enable =
			  stmp3xxx_alarm_irq_enable,
	.read_time	= stmp3xxx_rtc_gettime,
	.set_mmss	= stmp3xxx_rtc_set_mmss,
	.read_alarm	= stmp3xxx_rtc_read_alarm,
	.set_alarm	= stmp3xxx_rtc_set_alarm,
};

static int stmp3xxx_rtc_remove(struct platform_device *pdev)
{
	struct stmp3xxx_rtc_data *rtc_data = platform_get_drvdata(pdev);

	if (!rtc_data)
		return 0;

	writel(STMP3XXX_RTC_CTRL_ALARM_IRQ_EN,
			rtc_data->io + STMP3XXX_RTC_CTRL_CLR);
	free_irq(rtc_data->irq_alarm, &pdev->dev);
	rtc_device_unregister(rtc_data->rtc);
	platform_set_drvdata(pdev, NULL);
	iounmap(rtc_data->io);
	kfree(rtc_data);

	return 0;
}

static int stmp3xxx_rtc_probe(struct platform_device *pdev)
{
	struct stmp3xxx_rtc_data *rtc_data;
	struct resource *r;
	int err;

	rtc_data = kzalloc(sizeof *rtc_data, GFP_KERNEL);
	if (!rtc_data)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "failed to get resource\n");
		err = -ENXIO;
		goto out_free;
	}

	rtc_data->io = ioremap(r->start, resource_size(r));
	if (!rtc_data->io) {
		dev_err(&pdev->dev, "ioremap failed\n");
		err = -EIO;
		goto out_free;
	}

	rtc_data->irq_alarm = platform_get_irq(pdev, 0);

	if (!(readl(STMP3XXX_RTC_STAT + rtc_data->io) &
			STMP3XXX_RTC_STAT_RTC_PRESENT)) {
		dev_err(&pdev->dev, "no device onboard\n");
		err = -ENODEV;
		goto out_remap;
	}

	platform_set_drvdata(pdev, rtc_data);

	mxs_reset_block(rtc_data->io);
	writel(STMP3XXX_RTC_PERSISTENT0_ALARM_EN |
			STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE_EN |
			STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE,
			rtc_data->io + STMP3XXX_RTC_PERSISTENT0_CLR);

	writel(STMP3XXX_RTC_CTRL_ONEMSEC_IRQ_EN |
			STMP3XXX_RTC_CTRL_ALARM_IRQ_EN,
			rtc_data->io + STMP3XXX_RTC_CTRL_CLR);

	rtc_data->rtc = rtc_device_register(pdev->name, &pdev->dev,
				&stmp3xxx_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc_data->rtc)) {
		err = PTR_ERR(rtc_data->rtc);
		goto out_remap;
	}

	err = request_irq(rtc_data->irq_alarm, stmp3xxx_rtc_interrupt, 0,
			"RTC alarm", &pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Cannot claim IRQ%d\n",
			rtc_data->irq_alarm);
		goto out_irq_alarm;
	}

	return 0;

out_irq_alarm:
	rtc_device_unregister(rtc_data->rtc);
out_remap:
	platform_set_drvdata(pdev, NULL);
	iounmap(rtc_data->io);
out_free:
	kfree(rtc_data);
	return err;
}

#ifdef CONFIG_PM
static int stmp3xxx_rtc_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}

static int stmp3xxx_rtc_resume(struct platform_device *dev)
{
	struct stmp3xxx_rtc_data *rtc_data = platform_get_drvdata(dev);

	mxs_reset_block(rtc_data->io);
	writel(STMP3XXX_RTC_PERSISTENT0_ALARM_EN |
			STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE_EN |
			STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE,
			rtc_data->io + STMP3XXX_RTC_PERSISTENT0_CLR);
	return 0;
}
#else
#define stmp3xxx_rtc_suspend	NULL
#define stmp3xxx_rtc_resume	NULL
#endif

static const struct of_device_id rtc_dt_ids[] = {
	{ .compatible = "fsl,stmp3xxx-rtc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtc_dt_ids);

static struct platform_driver stmp3xxx_rtcdrv = {
	.probe		= stmp3xxx_rtc_probe,
	.remove		= stmp3xxx_rtc_remove,
	.suspend	= stmp3xxx_rtc_suspend,
	.resume		= stmp3xxx_rtc_resume,
	.driver		= {
		.name	= "stmp3xxx-rtc",
		.owner	= THIS_MODULE,
		.of_match_table = rtc_dt_ids,
	},
};

module_platform_driver(stmp3xxx_rtcdrv);

MODULE_DESCRIPTION("STMP3xxx RTC Driver");
MODULE_AUTHOR("dmitry pervushin <dpervushin@embeddedalley.com> and "
		"Wolfram Sang <w.sang@pengutronix.de>");
MODULE_LICENSE("GPL");
