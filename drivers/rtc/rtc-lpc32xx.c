/*
 * Copyright (C) 2010 NXP Semiconductors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/io.h>

/*
 * Clock and Power control register offsets
 */
#define LPC32XX_RTC_UCOUNT		0x00
#define LPC32XX_RTC_DCOUNT		0x04
#define LPC32XX_RTC_MATCH0		0x08
#define LPC32XX_RTC_MATCH1		0x0C
#define LPC32XX_RTC_CTRL		0x10
#define LPC32XX_RTC_INTSTAT		0x14
#define LPC32XX_RTC_KEY			0x18
#define LPC32XX_RTC_SRAM		0x80

#define LPC32XX_RTC_CTRL_MATCH0		(1 << 0)
#define LPC32XX_RTC_CTRL_MATCH1		(1 << 1)
#define LPC32XX_RTC_CTRL_ONSW_MATCH0	(1 << 2)
#define LPC32XX_RTC_CTRL_ONSW_MATCH1	(1 << 3)
#define LPC32XX_RTC_CTRL_SW_RESET	(1 << 4)
#define LPC32XX_RTC_CTRL_CNTR_DIS	(1 << 6)
#define LPC32XX_RTC_CTRL_ONSW_FORCE_HI	(1 << 7)

#define LPC32XX_RTC_INTSTAT_MATCH0	(1 << 0)
#define LPC32XX_RTC_INTSTAT_MATCH1	(1 << 1)
#define LPC32XX_RTC_INTSTAT_ONSW	(1 << 2)

#define LPC32XX_RTC_KEY_ONSW_LOADVAL	0xB5C13F27

#define RTC_NAME "rtc-lpc32xx"

#define rtc_readl(dev, reg) \
	__raw_readl((dev)->rtc_base + (reg))
#define rtc_writel(dev, reg, val) \
	__raw_writel((val), (dev)->rtc_base + (reg))

struct lpc32xx_rtc {
	void __iomem *rtc_base;
	int irq;
	unsigned char alarm_enabled;
	struct rtc_device *rtc;
	spinlock_t lock;
};

static int lpc32xx_rtc_read_time(struct device *dev, struct rtc_time *time)
{
	unsigned long elapsed_sec;
	struct lpc32xx_rtc *rtc = dev_get_drvdata(dev);

	elapsed_sec = rtc_readl(rtc, LPC32XX_RTC_UCOUNT);
	rtc_time_to_tm(elapsed_sec, time);

	return rtc_valid_tm(time);
}

static int lpc32xx_rtc_set_mmss(struct device *dev, unsigned long secs)
{
	struct lpc32xx_rtc *rtc = dev_get_drvdata(dev);
	u32 tmp;

	spin_lock_irq(&rtc->lock);

	/* RTC must be disabled during count update */
	tmp = rtc_readl(rtc, LPC32XX_RTC_CTRL);
	rtc_writel(rtc, LPC32XX_RTC_CTRL, tmp | LPC32XX_RTC_CTRL_CNTR_DIS);
	rtc_writel(rtc, LPC32XX_RTC_UCOUNT, secs);
	rtc_writel(rtc, LPC32XX_RTC_DCOUNT, 0xFFFFFFFF - secs);
	rtc_writel(rtc, LPC32XX_RTC_CTRL, tmp &= ~LPC32XX_RTC_CTRL_CNTR_DIS);

	spin_unlock_irq(&rtc->lock);

	return 0;
}

static int lpc32xx_rtc_read_alarm(struct device *dev,
	struct rtc_wkalrm *wkalrm)
{
	struct lpc32xx_rtc *rtc = dev_get_drvdata(dev);

	rtc_time_to_tm(rtc_readl(rtc, LPC32XX_RTC_MATCH0), &wkalrm->time);
	wkalrm->enabled = rtc->alarm_enabled;
	wkalrm->pending = !!(rtc_readl(rtc, LPC32XX_RTC_INTSTAT) &
		LPC32XX_RTC_INTSTAT_MATCH0);

	return rtc_valid_tm(&wkalrm->time);
}

static int lpc32xx_rtc_set_alarm(struct device *dev,
	struct rtc_wkalrm *wkalrm)
{
	struct lpc32xx_rtc *rtc = dev_get_drvdata(dev);
	unsigned long alarmsecs;
	u32 tmp;
	int ret;

	ret = rtc_tm_to_time(&wkalrm->time, &alarmsecs);
	if (ret < 0) {
		dev_warn(dev, "Failed to convert time: %d\n", ret);
		return ret;
	}

	spin_lock_irq(&rtc->lock);

	/* Disable alarm during update */
	tmp = rtc_readl(rtc, LPC32XX_RTC_CTRL);
	rtc_writel(rtc, LPC32XX_RTC_CTRL, tmp & ~LPC32XX_RTC_CTRL_MATCH0);

	rtc_writel(rtc, LPC32XX_RTC_MATCH0, alarmsecs);

	rtc->alarm_enabled = wkalrm->enabled;
	if (wkalrm->enabled) {
		rtc_writel(rtc, LPC32XX_RTC_INTSTAT,
			   LPC32XX_RTC_INTSTAT_MATCH0);
		rtc_writel(rtc, LPC32XX_RTC_CTRL, tmp |
			   LPC32XX_RTC_CTRL_MATCH0);
	}

	spin_unlock_irq(&rtc->lock);

	return 0;
}

static int lpc32xx_rtc_alarm_irq_enable(struct device *dev,
	unsigned int enabled)
{
	struct lpc32xx_rtc *rtc = dev_get_drvdata(dev);
	u32 tmp;

	spin_lock_irq(&rtc->lock);
	tmp = rtc_readl(rtc, LPC32XX_RTC_CTRL);

	if (enabled) {
		rtc->alarm_enabled = 1;
		tmp |= LPC32XX_RTC_CTRL_MATCH0;
	} else {
		rtc->alarm_enabled = 0;
		tmp &= ~LPC32XX_RTC_CTRL_MATCH0;
	}

	rtc_writel(rtc, LPC32XX_RTC_CTRL, tmp);
	spin_unlock_irq(&rtc->lock);

	return 0;
}

static irqreturn_t lpc32xx_rtc_alarm_interrupt(int irq, void *dev)
{
	struct lpc32xx_rtc *rtc = dev;

	spin_lock(&rtc->lock);

	/* Disable alarm interrupt */
	rtc_writel(rtc, LPC32XX_RTC_CTRL,
		rtc_readl(rtc, LPC32XX_RTC_CTRL) &
			  ~LPC32XX_RTC_CTRL_MATCH0);
	rtc->alarm_enabled = 0;

	/*
	 * Write a large value to the match value so the RTC won't
	 * keep firing the match status
	 */
	rtc_writel(rtc, LPC32XX_RTC_MATCH0, 0xFFFFFFFF);
	rtc_writel(rtc, LPC32XX_RTC_INTSTAT, LPC32XX_RTC_INTSTAT_MATCH0);

	spin_unlock(&rtc->lock);

	rtc_update_irq(rtc->rtc, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops lpc32xx_rtc_ops = {
	.read_time		= lpc32xx_rtc_read_time,
	.set_mmss		= lpc32xx_rtc_set_mmss,
	.read_alarm		= lpc32xx_rtc_read_alarm,
	.set_alarm		= lpc32xx_rtc_set_alarm,
	.alarm_irq_enable	= lpc32xx_rtc_alarm_irq_enable,
};

static int __devinit lpc32xx_rtc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct lpc32xx_rtc *rtc;
	resource_size_t size;
	int rtcirq;
	u32 tmp;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Can't get memory resource\n");
		return -ENOENT;
	}

	rtcirq = platform_get_irq(pdev, 0);
	if (rtcirq < 0 || rtcirq >= NR_IRQS) {
		dev_warn(&pdev->dev, "Can't get interrupt resource\n");
		rtcirq = -1;
	}

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (unlikely(!rtc)) {
		dev_err(&pdev->dev, "Can't allocate memory\n");
		return -ENOMEM;
	}
	rtc->irq = rtcirq;

	size = resource_size(res);

	if (!devm_request_mem_region(&pdev->dev, res->start, size,
				     pdev->name)) {
		dev_err(&pdev->dev, "RTC registers are not free\n");
		return -EBUSY;
	}

	rtc->rtc_base = devm_ioremap(&pdev->dev, res->start, size);
	if (!rtc->rtc_base) {
		dev_err(&pdev->dev, "Can't map memory\n");
		return -ENOMEM;
	}

	spin_lock_init(&rtc->lock);

	/*
	 * The RTC is on a separate power domain and can keep it's state
	 * across a chip power cycle. If the RTC has never been previously
	 * setup, then set it up now for the first time.
	 */
	tmp = rtc_readl(rtc, LPC32XX_RTC_CTRL);
	if (rtc_readl(rtc, LPC32XX_RTC_KEY) != LPC32XX_RTC_KEY_ONSW_LOADVAL) {
		tmp &= ~(LPC32XX_RTC_CTRL_SW_RESET |
			LPC32XX_RTC_CTRL_CNTR_DIS |
			LPC32XX_RTC_CTRL_MATCH0 |
			LPC32XX_RTC_CTRL_MATCH1 |
			LPC32XX_RTC_CTRL_ONSW_MATCH0 |
			LPC32XX_RTC_CTRL_ONSW_MATCH1 |
			LPC32XX_RTC_CTRL_ONSW_FORCE_HI);
		rtc_writel(rtc, LPC32XX_RTC_CTRL, tmp);

		/* Clear latched interrupt states */
		rtc_writel(rtc, LPC32XX_RTC_MATCH0, 0xFFFFFFFF);
		rtc_writel(rtc, LPC32XX_RTC_INTSTAT,
			   LPC32XX_RTC_INTSTAT_MATCH0 |
			   LPC32XX_RTC_INTSTAT_MATCH1 |
			   LPC32XX_RTC_INTSTAT_ONSW);

		/* Write key value to RTC so it won't reload on reset */
		rtc_writel(rtc, LPC32XX_RTC_KEY,
			   LPC32XX_RTC_KEY_ONSW_LOADVAL);
	} else {
		rtc_writel(rtc, LPC32XX_RTC_CTRL,
			   tmp & ~LPC32XX_RTC_CTRL_MATCH0);
	}

	platform_set_drvdata(pdev, rtc);

	rtc->rtc = rtc_device_register(RTC_NAME, &pdev->dev, &lpc32xx_rtc_ops,
		THIS_MODULE);
	if (IS_ERR(rtc->rtc)) {
		dev_err(&pdev->dev, "Can't get RTC\n");
		platform_set_drvdata(pdev, NULL);
		return PTR_ERR(rtc->rtc);
	}

	/*
	 * IRQ is enabled after device registration in case alarm IRQ
	 * is pending upon suspend exit.
	 */
	if (rtc->irq >= 0) {
		if (devm_request_irq(&pdev->dev, rtc->irq,
				     lpc32xx_rtc_alarm_interrupt,
				     IRQF_DISABLED, pdev->name, rtc) < 0) {
			dev_warn(&pdev->dev, "Can't request interrupt.\n");
			rtc->irq = -1;
		} else {
			device_init_wakeup(&pdev->dev, 1);
		}
	}

	return 0;
}

static int __devexit lpc32xx_rtc_remove(struct platform_device *pdev)
{
	struct lpc32xx_rtc *rtc = platform_get_drvdata(pdev);

	if (rtc->irq >= 0)
		device_init_wakeup(&pdev->dev, 0);

	platform_set_drvdata(pdev, NULL);
	rtc_device_unregister(rtc->rtc);

	return 0;
}

#ifdef CONFIG_PM
static int lpc32xx_rtc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lpc32xx_rtc *rtc = platform_get_drvdata(pdev);

	if (rtc->irq >= 0) {
		if (device_may_wakeup(&pdev->dev))
			enable_irq_wake(rtc->irq);
		else
			disable_irq_wake(rtc->irq);
	}

	return 0;
}

static int lpc32xx_rtc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lpc32xx_rtc *rtc = platform_get_drvdata(pdev);

	if (rtc->irq >= 0 && device_may_wakeup(&pdev->dev))
		disable_irq_wake(rtc->irq);

	return 0;
}

/* Unconditionally disable the alarm */
static int lpc32xx_rtc_freeze(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lpc32xx_rtc *rtc = platform_get_drvdata(pdev);

	spin_lock_irq(&rtc->lock);

	rtc_writel(rtc, LPC32XX_RTC_CTRL,
		rtc_readl(rtc, LPC32XX_RTC_CTRL) &
			  ~LPC32XX_RTC_CTRL_MATCH0);

	spin_unlock_irq(&rtc->lock);

	return 0;
}

static int lpc32xx_rtc_thaw(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct lpc32xx_rtc *rtc = platform_get_drvdata(pdev);

	if (rtc->alarm_enabled) {
		spin_lock_irq(&rtc->lock);

		rtc_writel(rtc, LPC32XX_RTC_CTRL,
			   rtc_readl(rtc, LPC32XX_RTC_CTRL) |
			   LPC32XX_RTC_CTRL_MATCH0);

		spin_unlock_irq(&rtc->lock);
	}

	return 0;
}

static const struct dev_pm_ops lpc32xx_rtc_pm_ops = {
	.suspend = lpc32xx_rtc_suspend,
	.resume = lpc32xx_rtc_resume,
	.freeze = lpc32xx_rtc_freeze,
	.thaw = lpc32xx_rtc_thaw,
	.restore = lpc32xx_rtc_resume
};

#define LPC32XX_RTC_PM_OPS (&lpc32xx_rtc_pm_ops)
#else
#define LPC32XX_RTC_PM_OPS NULL
#endif

static struct platform_driver lpc32xx_rtc_driver = {
	.probe		= lpc32xx_rtc_probe,
	.remove		= __devexit_p(lpc32xx_rtc_remove),
	.driver = {
		.name	= RTC_NAME,
		.owner	= THIS_MODULE,
		.pm	= LPC32XX_RTC_PM_OPS
	},
};

module_platform_driver(lpc32xx_rtc_driver);

MODULE_AUTHOR("Kevin Wells <wellsk40@gmail.com");
MODULE_DESCRIPTION("RTC driver for the LPC32xx SoC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtc-lpc32xx");
