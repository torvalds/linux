/*
 * Copyright (C) 2011-2012 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/clk.h>

/* These register offsets are relative to LP (Low Power) range */
#define SNVS_LPCR		0x04
#define SNVS_LPSR		0x18
#define SNVS_LPSRTCMR		0x1c
#define SNVS_LPSRTCLR		0x20
#define SNVS_LPTAR		0x24
#define SNVS_LPPGDR		0x30

#define SNVS_LPCR_SRTC_ENV	(1 << 0)
#define SNVS_LPCR_LPTA_EN	(1 << 1)
#define SNVS_LPCR_LPWUI_EN	(1 << 3)
#define SNVS_LPSR_LPTA		(1 << 0)

#define SNVS_LPPGDR_INIT	0x41736166
#define CNTR_TO_SECS_SH		15

struct snvs_rtc_data {
	struct rtc_device *rtc;
	void __iomem *ioaddr;
	int irq;
	spinlock_t lock;
	struct clk *clk;
};

static u32 rtc_read_lp_counter(void __iomem *ioaddr)
{
	u64 read1, read2;

	do {
		read1 = readl(ioaddr + SNVS_LPSRTCMR);
		read1 <<= 32;
		read1 |= readl(ioaddr + SNVS_LPSRTCLR);

		read2 = readl(ioaddr + SNVS_LPSRTCMR);
		read2 <<= 32;
		read2 |= readl(ioaddr + SNVS_LPSRTCLR);
	} while (read1 != read2);

	/* Convert 47-bit counter to 32-bit raw second count */
	return (u32) (read1 >> CNTR_TO_SECS_SH);
}

static void rtc_write_sync_lp(void __iomem *ioaddr)
{
	u32 count1, count2, count3;
	int i;

	/* Wait for 3 CKIL cycles */
	for (i = 0; i < 3; i++) {
		do {
			count1 = readl(ioaddr + SNVS_LPSRTCLR);
			count2 = readl(ioaddr + SNVS_LPSRTCLR);
		} while (count1 != count2);

		/* Now wait until counter value changes */
		do {
			do {
				count2 = readl(ioaddr + SNVS_LPSRTCLR);
				count3 = readl(ioaddr + SNVS_LPSRTCLR);
			} while (count2 != count3);
		} while (count3 == count1);
	}
}

static int snvs_rtc_enable(struct snvs_rtc_data *data, bool enable)
{
	unsigned long flags;
	int timeout = 1000;
	u32 lpcr;

	spin_lock_irqsave(&data->lock, flags);

	lpcr = readl(data->ioaddr + SNVS_LPCR);
	if (enable)
		lpcr |= SNVS_LPCR_SRTC_ENV;
	else
		lpcr &= ~SNVS_LPCR_SRTC_ENV;
	writel(lpcr, data->ioaddr + SNVS_LPCR);

	spin_unlock_irqrestore(&data->lock, flags);

	while (--timeout) {
		lpcr = readl(data->ioaddr + SNVS_LPCR);

		if (enable) {
			if (lpcr & SNVS_LPCR_SRTC_ENV)
				break;
		} else {
			if (!(lpcr & SNVS_LPCR_SRTC_ENV))
				break;
		}
	}

	if (!timeout)
		return -ETIMEDOUT;

	return 0;
}

static int snvs_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct snvs_rtc_data *data = dev_get_drvdata(dev);
	unsigned long time = rtc_read_lp_counter(data->ioaddr);

	rtc_time_to_tm(time, tm);

	return 0;
}

static int snvs_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct snvs_rtc_data *data = dev_get_drvdata(dev);
	unsigned long time;

	rtc_tm_to_time(tm, &time);

	/* Disable RTC first */
	snvs_rtc_enable(data, false);

	/* Write 32-bit time to 47-bit timer, leaving 15 LSBs blank */
	writel(time << CNTR_TO_SECS_SH, data->ioaddr + SNVS_LPSRTCLR);
	writel(time >> (32 - CNTR_TO_SECS_SH), data->ioaddr + SNVS_LPSRTCMR);

	/* Enable RTC again */
	snvs_rtc_enable(data, true);

	return 0;
}

static int snvs_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct snvs_rtc_data *data = dev_get_drvdata(dev);
	u32 lptar, lpsr;

	lptar = readl(data->ioaddr + SNVS_LPTAR);
	rtc_time_to_tm(lptar, &alrm->time);

	lpsr = readl(data->ioaddr + SNVS_LPSR);
	alrm->pending = (lpsr & SNVS_LPSR_LPTA) ? 1 : 0;

	return 0;
}

static int snvs_rtc_alarm_irq_enable(struct device *dev, unsigned int enable)
{
	struct snvs_rtc_data *data = dev_get_drvdata(dev);
	u32 lpcr;
	unsigned long flags;

	spin_lock_irqsave(&data->lock, flags);

	lpcr = readl(data->ioaddr + SNVS_LPCR);
	if (enable)
		lpcr |= (SNVS_LPCR_LPTA_EN | SNVS_LPCR_LPWUI_EN);
	else
		lpcr &= ~(SNVS_LPCR_LPTA_EN | SNVS_LPCR_LPWUI_EN);
	writel(lpcr, data->ioaddr + SNVS_LPCR);

	spin_unlock_irqrestore(&data->lock, flags);

	rtc_write_sync_lp(data->ioaddr);

	return 0;
}

static int snvs_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct snvs_rtc_data *data = dev_get_drvdata(dev);
	struct rtc_time *alrm_tm = &alrm->time;
	unsigned long time;
	unsigned long flags;
	u32 lpcr;

	rtc_tm_to_time(alrm_tm, &time);

	spin_lock_irqsave(&data->lock, flags);

	/* Have to clear LPTA_EN before programming new alarm time in LPTAR */
	lpcr = readl(data->ioaddr + SNVS_LPCR);
	lpcr &= ~SNVS_LPCR_LPTA_EN;
	writel(lpcr, data->ioaddr + SNVS_LPCR);

	spin_unlock_irqrestore(&data->lock, flags);

	writel(time, data->ioaddr + SNVS_LPTAR);

	/* Clear alarm interrupt status bit */
	writel(SNVS_LPSR_LPTA, data->ioaddr + SNVS_LPSR);

	return snvs_rtc_alarm_irq_enable(dev, alrm->enabled);
}

static const struct rtc_class_ops snvs_rtc_ops = {
	.read_time = snvs_rtc_read_time,
	.set_time = snvs_rtc_set_time,
	.read_alarm = snvs_rtc_read_alarm,
	.set_alarm = snvs_rtc_set_alarm,
	.alarm_irq_enable = snvs_rtc_alarm_irq_enable,
};

static irqreturn_t snvs_rtc_irq_handler(int irq, void *dev_id)
{
	struct device *dev = dev_id;
	struct snvs_rtc_data *data = dev_get_drvdata(dev);
	u32 lpsr;
	u32 events = 0;

	lpsr = readl(data->ioaddr + SNVS_LPSR);

	if (lpsr & SNVS_LPSR_LPTA) {
		events |= (RTC_AF | RTC_IRQF);

		/* RTC alarm should be one-shot */
		snvs_rtc_alarm_irq_enable(dev, 0);

		rtc_update_irq(data->rtc, 1, events);
	}

	/* clear interrupt status */
	writel(lpsr, data->ioaddr + SNVS_LPSR);

	return events ? IRQ_HANDLED : IRQ_NONE;
}

static int snvs_rtc_probe(struct platform_device *pdev)
{
	struct snvs_rtc_data *data;
	struct resource *res;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->ioaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->ioaddr))
		return PTR_ERR(data->ioaddr);

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0)
		return data->irq;

	data->clk = devm_clk_get(&pdev->dev, "snvs-rtc");
	if (IS_ERR(data->clk)) {
		data->clk = NULL;
	} else {
		ret = clk_prepare_enable(data->clk);
		if (ret) {
			dev_err(&pdev->dev,
				"Could not prepare or enable the snvs clock\n");
			return ret;
		}
	}

	platform_set_drvdata(pdev, data);

	spin_lock_init(&data->lock);

	/* Initialize glitch detect */
	writel(SNVS_LPPGDR_INIT, data->ioaddr + SNVS_LPPGDR);

	/* Clear interrupt status */
	writel(0xffffffff, data->ioaddr + SNVS_LPSR);

	/* Enable RTC */
	snvs_rtc_enable(data, true);

	device_init_wakeup(&pdev->dev, true);

	ret = devm_request_irq(&pdev->dev, data->irq, snvs_rtc_irq_handler,
			       IRQF_SHARED, "rtc alarm", &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq %d: %d\n",
			data->irq, ret);
		goto error_rtc_device_register;
	}

	data->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
					&snvs_rtc_ops, THIS_MODULE);
	if (IS_ERR(data->rtc)) {
		ret = PTR_ERR(data->rtc);
		dev_err(&pdev->dev, "failed to register rtc: %d\n", ret);
		goto error_rtc_device_register;
	}

	return 0;

error_rtc_device_register:
	if (data->clk)
		clk_disable_unprepare(data->clk);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int snvs_rtc_suspend(struct device *dev)
{
	struct snvs_rtc_data *data = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(data->irq);

	if (data->clk)
		clk_disable_unprepare(data->clk);

	return 0;
}

static int snvs_rtc_resume(struct device *dev)
{
	struct snvs_rtc_data *data = dev_get_drvdata(dev);
	int ret;

	if (device_may_wakeup(dev))
		disable_irq_wake(data->irq);

	if (data->clk) {
		ret = clk_prepare_enable(data->clk);
		if (ret)
			return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops snvs_rtc_pm_ops = {
	.suspend_noirq = snvs_rtc_suspend,
	.resume_noirq = snvs_rtc_resume,
};

static const struct of_device_id snvs_dt_ids[] = {
	{ .compatible = "fsl,sec-v4.0-mon-rtc-lp", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, snvs_dt_ids);

static struct platform_driver snvs_rtc_driver = {
	.driver = {
		.name	= "snvs_rtc",
		.owner	= THIS_MODULE,
		.pm	= &snvs_rtc_pm_ops,
		.of_match_table = snvs_dt_ids,
	},
	.probe		= snvs_rtc_probe,
};
module_platform_driver(snvs_rtc_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("Freescale SNVS RTC Driver");
MODULE_LICENSE("GPL");
