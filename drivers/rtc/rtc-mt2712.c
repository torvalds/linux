// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: Ran Bi <ran.bi@mediatek.com>
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#define MT2712_BBPU		0x0000
#define MT2712_BBPU_CLRPKY	BIT(4)
#define MT2712_BBPU_RELOAD	BIT(5)
#define MT2712_BBPU_CBUSY	BIT(6)
#define MT2712_BBPU_KEY		(0x43 << 8)

#define MT2712_IRQ_STA		0x0004
#define MT2712_IRQ_STA_AL	BIT(0)
#define MT2712_IRQ_STA_TC	BIT(1)

#define MT2712_IRQ_EN		0x0008
#define MT2712_IRQ_EN_AL	BIT(0)
#define MT2712_IRQ_EN_TC	BIT(1)
#define MT2712_IRQ_EN_ONESHOT	BIT(2)

#define MT2712_CII_EN		0x000c

#define MT2712_AL_MASK		0x0010
#define MT2712_AL_MASK_DOW	BIT(4)

#define MT2712_TC_SEC		0x0014
#define MT2712_TC_MIN		0x0018
#define MT2712_TC_HOU		0x001c
#define MT2712_TC_DOM		0x0020
#define MT2712_TC_DOW		0x0024
#define MT2712_TC_MTH		0x0028
#define MT2712_TC_YEA		0x002c

#define MT2712_AL_SEC		0x0030
#define MT2712_AL_MIN		0x0034
#define MT2712_AL_HOU		0x0038
#define MT2712_AL_DOM		0x003c
#define MT2712_AL_DOW		0x0040
#define MT2712_AL_MTH		0x0044
#define MT2712_AL_YEA		0x0048

#define MT2712_SEC_MASK		0x003f
#define MT2712_MIN_MASK		0x003f
#define MT2712_HOU_MASK		0x001f
#define MT2712_DOM_MASK		0x001f
#define MT2712_DOW_MASK		0x0007
#define MT2712_MTH_MASK		0x000f
#define MT2712_YEA_MASK		0x007f

#define MT2712_POWERKEY1	0x004c
#define MT2712_POWERKEY2	0x0050
#define MT2712_POWERKEY1_KEY	0xa357
#define MT2712_POWERKEY2_KEY	0x67d2

#define MT2712_CON0		0x005c
#define MT2712_CON1		0x0060

#define MT2712_PROT		0x0070
#define MT2712_PROT_UNLOCK1	0x9136
#define MT2712_PROT_UNLOCK2	0x586a

#define MT2712_WRTGR		0x0078

#define MT2712_RTC_TIMESTAMP_END_2127	4985971199LL

struct mt2712_rtc {
	struct rtc_device	*rtc;
	void __iomem		*base;
	int			irq;
	u8			irq_wake_enabled;
	u8			powerlost;
};

static inline u32 mt2712_readl(struct mt2712_rtc *mt2712_rtc, u32 reg)
{
	return readl(mt2712_rtc->base + reg);
}

static inline void mt2712_writel(struct mt2712_rtc *mt2712_rtc,
				 u32 reg, u32 val)
{
	writel(val, mt2712_rtc->base + reg);
}

static void mt2712_rtc_write_trigger(struct mt2712_rtc *mt2712_rtc)
{
	unsigned long timeout = jiffies + HZ / 10;

	mt2712_writel(mt2712_rtc, MT2712_WRTGR, 1);
	while (1) {
		if (!(mt2712_readl(mt2712_rtc, MT2712_BBPU)
					& MT2712_BBPU_CBUSY))
			break;

		if (time_after(jiffies, timeout)) {
			dev_err(&mt2712_rtc->rtc->dev,
				"%s time out!\n", __func__);
			break;
		}
		cpu_relax();
	}
}

static void mt2712_rtc_writeif_unlock(struct mt2712_rtc *mt2712_rtc)
{
	mt2712_writel(mt2712_rtc, MT2712_PROT, MT2712_PROT_UNLOCK1);
	mt2712_rtc_write_trigger(mt2712_rtc);
	mt2712_writel(mt2712_rtc, MT2712_PROT, MT2712_PROT_UNLOCK2);
	mt2712_rtc_write_trigger(mt2712_rtc);
}

static irqreturn_t rtc_irq_handler_thread(int irq, void *data)
{
	struct mt2712_rtc *mt2712_rtc = data;
	u16 irqsta;

	/* Clear interrupt */
	irqsta = mt2712_readl(mt2712_rtc, MT2712_IRQ_STA);
	if (irqsta & MT2712_IRQ_STA_AL) {
		rtc_update_irq(mt2712_rtc->rtc, 1, RTC_IRQF | RTC_AF);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void __mt2712_rtc_read_time(struct mt2712_rtc *mt2712_rtc,
				   struct rtc_time *tm, int *sec)
{
	tm->tm_sec  = mt2712_readl(mt2712_rtc, MT2712_TC_SEC)
			& MT2712_SEC_MASK;
	tm->tm_min  = mt2712_readl(mt2712_rtc, MT2712_TC_MIN)
			& MT2712_MIN_MASK;
	tm->tm_hour = mt2712_readl(mt2712_rtc, MT2712_TC_HOU)
			& MT2712_HOU_MASK;
	tm->tm_mday = mt2712_readl(mt2712_rtc, MT2712_TC_DOM)
			& MT2712_DOM_MASK;
	tm->tm_mon  = (mt2712_readl(mt2712_rtc, MT2712_TC_MTH) - 1)
			& MT2712_MTH_MASK;
	tm->tm_year = (mt2712_readl(mt2712_rtc, MT2712_TC_YEA) + 100)
			& MT2712_YEA_MASK;

	*sec = mt2712_readl(mt2712_rtc, MT2712_TC_SEC) & MT2712_SEC_MASK;
}

static int mt2712_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct mt2712_rtc *mt2712_rtc = dev_get_drvdata(dev);
	int sec;

	if (mt2712_rtc->powerlost)
		return -EINVAL;

	do {
		__mt2712_rtc_read_time(mt2712_rtc, tm, &sec);
	} while (sec < tm->tm_sec);	/* SEC has carried */

	return 0;
}

static int mt2712_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct mt2712_rtc *mt2712_rtc = dev_get_drvdata(dev);

	mt2712_writel(mt2712_rtc, MT2712_TC_SEC, tm->tm_sec  & MT2712_SEC_MASK);
	mt2712_writel(mt2712_rtc, MT2712_TC_MIN, tm->tm_min  & MT2712_MIN_MASK);
	mt2712_writel(mt2712_rtc, MT2712_TC_HOU, tm->tm_hour & MT2712_HOU_MASK);
	mt2712_writel(mt2712_rtc, MT2712_TC_DOM, tm->tm_mday & MT2712_DOM_MASK);
	mt2712_writel(mt2712_rtc, MT2712_TC_MTH,
		      (tm->tm_mon + 1) & MT2712_MTH_MASK);
	mt2712_writel(mt2712_rtc, MT2712_TC_YEA,
		      (tm->tm_year - 100) & MT2712_YEA_MASK);

	mt2712_rtc_write_trigger(mt2712_rtc);

	if (mt2712_rtc->powerlost)
		mt2712_rtc->powerlost = false;

	return 0;
}

static int mt2712_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct mt2712_rtc *mt2712_rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &alm->time;
	u16 irqen;

	irqen = mt2712_readl(mt2712_rtc, MT2712_IRQ_EN);
	alm->enabled = !!(irqen & MT2712_IRQ_EN_AL);

	tm->tm_sec  = mt2712_readl(mt2712_rtc, MT2712_AL_SEC) & MT2712_SEC_MASK;
	tm->tm_min  = mt2712_readl(mt2712_rtc, MT2712_AL_MIN) & MT2712_MIN_MASK;
	tm->tm_hour = mt2712_readl(mt2712_rtc, MT2712_AL_HOU) & MT2712_HOU_MASK;
	tm->tm_mday = mt2712_readl(mt2712_rtc, MT2712_AL_DOM) & MT2712_DOM_MASK;
	tm->tm_mon  = (mt2712_readl(mt2712_rtc, MT2712_AL_MTH) - 1)
		      & MT2712_MTH_MASK;
	tm->tm_year = (mt2712_readl(mt2712_rtc, MT2712_AL_YEA) + 100)
		      & MT2712_YEA_MASK;

	return 0;
}

static int mt2712_rtc_alarm_irq_enable(struct device *dev,
				       unsigned int enabled)
{
	struct mt2712_rtc *mt2712_rtc = dev_get_drvdata(dev);
	u16 irqen;

	irqen = mt2712_readl(mt2712_rtc, MT2712_IRQ_EN);
	if (enabled)
		irqen |= MT2712_IRQ_EN_AL;
	else
		irqen &= ~MT2712_IRQ_EN_AL;
	mt2712_writel(mt2712_rtc, MT2712_IRQ_EN, irqen);
	mt2712_rtc_write_trigger(mt2712_rtc);

	return 0;
}

static int mt2712_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct mt2712_rtc *mt2712_rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &alm->time;

	dev_dbg(&mt2712_rtc->rtc->dev, "set al time: %ptR, alm en: %d\n",
		tm, alm->enabled);

	mt2712_writel(mt2712_rtc, MT2712_AL_SEC,
		      (mt2712_readl(mt2712_rtc, MT2712_AL_SEC)
		       & ~(MT2712_SEC_MASK)) | (tm->tm_sec  & MT2712_SEC_MASK));
	mt2712_writel(mt2712_rtc, MT2712_AL_MIN,
		      (mt2712_readl(mt2712_rtc, MT2712_AL_MIN)
		       & ~(MT2712_MIN_MASK)) | (tm->tm_min  & MT2712_MIN_MASK));
	mt2712_writel(mt2712_rtc, MT2712_AL_HOU,
		      (mt2712_readl(mt2712_rtc, MT2712_AL_HOU)
		       & ~(MT2712_HOU_MASK)) | (tm->tm_hour & MT2712_HOU_MASK));
	mt2712_writel(mt2712_rtc, MT2712_AL_DOM,
		      (mt2712_readl(mt2712_rtc, MT2712_AL_DOM)
		       & ~(MT2712_DOM_MASK)) | (tm->tm_mday & MT2712_DOM_MASK));
	mt2712_writel(mt2712_rtc, MT2712_AL_MTH,
		      (mt2712_readl(mt2712_rtc, MT2712_AL_MTH)
		       & ~(MT2712_MTH_MASK))
		      | ((tm->tm_mon + 1) & MT2712_MTH_MASK));
	mt2712_writel(mt2712_rtc, MT2712_AL_YEA,
		      (mt2712_readl(mt2712_rtc, MT2712_AL_YEA)
		       & ~(MT2712_YEA_MASK))
		      | ((tm->tm_year - 100) & MT2712_YEA_MASK));

	/* mask day of week */
	mt2712_writel(mt2712_rtc, MT2712_AL_MASK, MT2712_AL_MASK_DOW);
	mt2712_rtc_write_trigger(mt2712_rtc);

	mt2712_rtc_alarm_irq_enable(dev, alm->enabled);

	return 0;
}

/* Init RTC register */
static void mt2712_rtc_hw_init(struct mt2712_rtc *mt2712_rtc)
{
	u32 p1, p2;

	mt2712_writel(mt2712_rtc, MT2712_BBPU,
		      MT2712_BBPU_KEY | MT2712_BBPU_RELOAD);

	mt2712_writel(mt2712_rtc, MT2712_CII_EN, 0);
	mt2712_writel(mt2712_rtc, MT2712_AL_MASK, 0);
	/* necessary before set MT2712_POWERKEY */
	mt2712_writel(mt2712_rtc, MT2712_CON0, 0x4848);
	mt2712_writel(mt2712_rtc, MT2712_CON1, 0x0048);

	mt2712_rtc_write_trigger(mt2712_rtc);

	p1 = mt2712_readl(mt2712_rtc, MT2712_POWERKEY1);
	p2 = mt2712_readl(mt2712_rtc, MT2712_POWERKEY2);
	if (p1 != MT2712_POWERKEY1_KEY || p2 != MT2712_POWERKEY2_KEY) {
		mt2712_rtc->powerlost = true;
		dev_dbg(&mt2712_rtc->rtc->dev,
			"powerkey not set (lost power)\n");
	} else {
		mt2712_rtc->powerlost = false;
	}

	/* RTC need POWERKEY1/2 match, then goto normal work mode */
	mt2712_writel(mt2712_rtc, MT2712_POWERKEY1, MT2712_POWERKEY1_KEY);
	mt2712_writel(mt2712_rtc, MT2712_POWERKEY2, MT2712_POWERKEY2_KEY);
	mt2712_rtc_write_trigger(mt2712_rtc);

	mt2712_rtc_writeif_unlock(mt2712_rtc);
}

static const struct rtc_class_ops mt2712_rtc_ops = {
	.read_time	= mt2712_rtc_read_time,
	.set_time	= mt2712_rtc_set_time,
	.read_alarm	= mt2712_rtc_read_alarm,
	.set_alarm	= mt2712_rtc_set_alarm,
	.alarm_irq_enable = mt2712_rtc_alarm_irq_enable,
};

static int mt2712_rtc_probe(struct platform_device *pdev)
{
	struct mt2712_rtc *mt2712_rtc;
	int ret;

	mt2712_rtc = devm_kzalloc(&pdev->dev,
				  sizeof(struct mt2712_rtc), GFP_KERNEL);
	if (!mt2712_rtc)
		return -ENOMEM;

	mt2712_rtc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mt2712_rtc->base))
		return PTR_ERR(mt2712_rtc->base);

	/* rtc hw init */
	mt2712_rtc_hw_init(mt2712_rtc);

	mt2712_rtc->irq = platform_get_irq(pdev, 0);
	if (mt2712_rtc->irq < 0)
		return mt2712_rtc->irq;

	platform_set_drvdata(pdev, mt2712_rtc);

	mt2712_rtc->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(mt2712_rtc->rtc))
		return PTR_ERR(mt2712_rtc->rtc);

	ret = devm_request_threaded_irq(&pdev->dev, mt2712_rtc->irq, NULL,
					rtc_irq_handler_thread,
					IRQF_ONESHOT | IRQF_TRIGGER_LOW,
					dev_name(&mt2712_rtc->rtc->dev),
					mt2712_rtc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request alarm IRQ: %d: %d\n",
			mt2712_rtc->irq, ret);
		return ret;
	}

	device_init_wakeup(&pdev->dev, true);

	mt2712_rtc->rtc->ops = &mt2712_rtc_ops;
	mt2712_rtc->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	mt2712_rtc->rtc->range_max = MT2712_RTC_TIMESTAMP_END_2127;

	return devm_rtc_register_device(mt2712_rtc->rtc);
}

#ifdef CONFIG_PM_SLEEP
static int mt2712_rtc_suspend(struct device *dev)
{
	int wake_status = 0;
	struct mt2712_rtc *mt2712_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		wake_status = enable_irq_wake(mt2712_rtc->irq);
		if (!wake_status)
			mt2712_rtc->irq_wake_enabled = true;
	}

	return 0;
}

static int mt2712_rtc_resume(struct device *dev)
{
	int wake_status = 0;
	struct mt2712_rtc *mt2712_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev) && mt2712_rtc->irq_wake_enabled) {
		wake_status = disable_irq_wake(mt2712_rtc->irq);
		if (!wake_status)
			mt2712_rtc->irq_wake_enabled = false;
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(mt2712_pm_ops, mt2712_rtc_suspend,
			 mt2712_rtc_resume);
#endif

static const struct of_device_id mt2712_rtc_of_match[] = {
	{ .compatible = "mediatek,mt2712-rtc", },
	{ },
};

MODULE_DEVICE_TABLE(of, mt2712_rtc_of_match);

static struct platform_driver mt2712_rtc_driver = {
	.driver = {
		.name = "mt2712-rtc",
		.of_match_table = mt2712_rtc_of_match,
#ifdef CONFIG_PM_SLEEP
		.pm = &mt2712_pm_ops,
#endif
	},
	.probe  = mt2712_rtc_probe,
};

module_platform_driver(mt2712_rtc_driver);

MODULE_DESCRIPTION("MediaTek MT2712 SoC based RTC Driver");
MODULE_AUTHOR("Ran Bi <ran.bi@mediatek.com>");
MODULE_LICENSE("GPL");
