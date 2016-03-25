/*
 * Copyright (C) 2016 Oleksij Rempel <linux@rempel-privat.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

/* Miscellaneous registers */
/* Interrupt Location Register */
#define HW_ILR			0x00
#define BM_RTCALF		BIT(1)
#define BM_RTCCIF		BIT(0)

/* Clock Control Register */
#define HW_CCR			0x08
/* Calibration counter disable */
#define BM_CCALOFF		BIT(4)
/* Reset internal oscillator divider */
#define BM_CTCRST		BIT(1)
/* Clock Enable */
#define BM_CLKEN		BIT(0)

/* Counter Increment Interrupt Register */
#define HW_CIIR			0x0C
#define BM_CIIR_IMYEAR		BIT(7)
#define BM_CIIR_IMMON		BIT(6)
#define BM_CIIR_IMDOY		BIT(5)
#define BM_CIIR_IMDOW		BIT(4)
#define BM_CIIR_IMDOM		BIT(3)
#define BM_CIIR_IMHOUR		BIT(2)
#define BM_CIIR_IMMIN		BIT(1)
#define BM_CIIR_IMSEC		BIT(0)

/* Alarm Mask Register */
#define HW_AMR			0x10
#define BM_AMR_IMYEAR		BIT(7)
#define BM_AMR_IMMON		BIT(6)
#define BM_AMR_IMDOY		BIT(5)
#define BM_AMR_IMDOW		BIT(4)
#define BM_AMR_IMDOM		BIT(3)
#define BM_AMR_IMHOUR		BIT(2)
#define BM_AMR_IMMIN		BIT(1)
#define BM_AMR_IMSEC		BIT(0)
#define BM_AMR_OFF		0xff

/* Consolidated time registers */
#define HW_CTIME0		0x14
#define BM_CTIME0_DOW_S		24
#define BM_CTIME0_DOW_M		0x7
#define BM_CTIME0_HOUR_S	16
#define BM_CTIME0_HOUR_M	0x1f
#define BM_CTIME0_MIN_S		8
#define BM_CTIME0_MIN_M		0x3f
#define BM_CTIME0_SEC_S		0
#define BM_CTIME0_SEC_M		0x3f

#define HW_CTIME1		0x18
#define BM_CTIME1_YEAR_S	16
#define BM_CTIME1_YEAR_M	0xfff
#define BM_CTIME1_MON_S		8
#define BM_CTIME1_MON_M		0xf
#define BM_CTIME1_DOM_S		0
#define BM_CTIME1_DOM_M		0x1f

#define HW_CTIME2		0x1C
#define BM_CTIME2_DOY_S		0
#define BM_CTIME2_DOY_M		0xfff

/* Time counter registers */
#define HW_SEC			0x20
#define HW_MIN			0x24
#define HW_HOUR			0x28
#define HW_DOM			0x2C
#define HW_DOW			0x30
#define HW_DOY			0x34
#define HW_MONTH		0x38
#define HW_YEAR			0x3C

#define HW_CALIBRATION		0x40
#define BM_CALDIR_BACK		BIT(17)
#define BM_CALVAL_M		0x1ffff

/* General purpose registers */
#define HW_GPREG0		0x44
#define HW_GPREG1		0x48
#define HW_GPREG2		0x4C
#define HW_GPREG3		0x50
#define HW_GPREG4		0x54

/* Alarm register group */
#define HW_ALSEC		0x60
#define HW_ALMIN		0x64
#define HW_ALHOUR		0x68
#define HW_ALDOM		0x6C
#define HW_ALDOW		0x70
#define HW_ALDOY		0x74
#define HW_ALMON		0x78
#define HW_ALYEAR		0x7C

struct asm9260_rtc_priv {
	struct device		*dev;
	void __iomem		*iobase;
	struct rtc_device	*rtc;
	struct clk		*clk;
	/* io lock */
	spinlock_t		lock;
};

static irqreturn_t asm9260_rtc_irq(int irq, void *dev_id)
{
	struct asm9260_rtc_priv *priv = dev_id;
	u32 isr;
	unsigned long events = 0;

	isr = ioread32(priv->iobase + HW_CIIR);
	if (!isr)
		return IRQ_NONE;

	iowrite32(0, priv->iobase + HW_CIIR);

	events |= RTC_AF | RTC_IRQF;

	rtc_update_irq(priv->rtc, 1, events);

	return IRQ_HANDLED;
}

static int asm9260_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct asm9260_rtc_priv *priv = dev_get_drvdata(dev);
	u32 ctime0, ctime1, ctime2;
	unsigned long irq_flags;

	spin_lock_irqsave(&priv->lock, irq_flags);
	ctime0 = ioread32(priv->iobase + HW_CTIME0);
	ctime1 = ioread32(priv->iobase + HW_CTIME1);
	ctime2 = ioread32(priv->iobase + HW_CTIME2);

	if (ctime1 != ioread32(priv->iobase + HW_CTIME1)) {
		/*
		 * woops, counter flipped right now. Now we are safe
		 * to reread.
		 */
		ctime0 = ioread32(priv->iobase + HW_CTIME0);
		ctime1 = ioread32(priv->iobase + HW_CTIME1);
		ctime2 = ioread32(priv->iobase + HW_CTIME2);
	}
	spin_unlock_irqrestore(&priv->lock, irq_flags);

	tm->tm_sec  = (ctime0 >> BM_CTIME0_SEC_S)  & BM_CTIME0_SEC_M;
	tm->tm_min  = (ctime0 >> BM_CTIME0_MIN_S)  & BM_CTIME0_MIN_M;
	tm->tm_hour = (ctime0 >> BM_CTIME0_HOUR_S) & BM_CTIME0_HOUR_M;
	tm->tm_wday = (ctime0 >> BM_CTIME0_DOW_S)  & BM_CTIME0_DOW_M;

	tm->tm_mday = (ctime1 >> BM_CTIME1_DOM_S)  & BM_CTIME1_DOM_M;
	tm->tm_mon  = (ctime1 >> BM_CTIME1_MON_S)  & BM_CTIME1_MON_M;
	tm->tm_year = (ctime1 >> BM_CTIME1_YEAR_S) & BM_CTIME1_YEAR_M;

	tm->tm_yday = (ctime2 >> BM_CTIME2_DOY_S)  & BM_CTIME2_DOY_M;

	return 0;
}

static int asm9260_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct asm9260_rtc_priv *priv = dev_get_drvdata(dev);
	unsigned long irq_flags;

	spin_lock_irqsave(&priv->lock, irq_flags);
	/*
	 * make sure SEC counter will not flip other counter on write time,
	 * real value will be written at the enf of sequence.
	 */
	iowrite32(0, priv->iobase + HW_SEC);

	iowrite32(tm->tm_year, priv->iobase + HW_YEAR);
	iowrite32(tm->tm_mon,  priv->iobase + HW_MONTH);
	iowrite32(tm->tm_mday, priv->iobase + HW_DOM);
	iowrite32(tm->tm_wday, priv->iobase + HW_DOW);
	iowrite32(tm->tm_yday, priv->iobase + HW_DOY);
	iowrite32(tm->tm_hour, priv->iobase + HW_HOUR);
	iowrite32(tm->tm_min,  priv->iobase + HW_MIN);
	iowrite32(tm->tm_sec,  priv->iobase + HW_SEC);
	spin_unlock_irqrestore(&priv->lock, irq_flags);

	return 0;
}

static int asm9260_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct asm9260_rtc_priv *priv = dev_get_drvdata(dev);
	unsigned long irq_flags;

	spin_lock_irqsave(&priv->lock, irq_flags);
	alrm->time.tm_year = ioread32(priv->iobase + HW_ALYEAR);
	alrm->time.tm_mon  = ioread32(priv->iobase + HW_ALMON);
	alrm->time.tm_mday = ioread32(priv->iobase + HW_ALDOM);
	alrm->time.tm_wday = ioread32(priv->iobase + HW_ALDOW);
	alrm->time.tm_yday = ioread32(priv->iobase + HW_ALDOY);
	alrm->time.tm_hour = ioread32(priv->iobase + HW_ALHOUR);
	alrm->time.tm_min  = ioread32(priv->iobase + HW_ALMIN);
	alrm->time.tm_sec  = ioread32(priv->iobase + HW_ALSEC);

	alrm->enabled = ioread32(priv->iobase + HW_AMR) ? 1 : 0;
	alrm->pending = ioread32(priv->iobase + HW_CIIR) ? 1 : 0;
	spin_unlock_irqrestore(&priv->lock, irq_flags);

	return rtc_valid_tm(&alrm->time);
}

static int asm9260_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct asm9260_rtc_priv *priv = dev_get_drvdata(dev);
	unsigned long irq_flags;

	spin_lock_irqsave(&priv->lock, irq_flags);
	iowrite32(alrm->time.tm_year, priv->iobase + HW_ALYEAR);
	iowrite32(alrm->time.tm_mon,  priv->iobase + HW_ALMON);
	iowrite32(alrm->time.tm_mday, priv->iobase + HW_ALDOM);
	iowrite32(alrm->time.tm_wday, priv->iobase + HW_ALDOW);
	iowrite32(alrm->time.tm_yday, priv->iobase + HW_ALDOY);
	iowrite32(alrm->time.tm_hour, priv->iobase + HW_ALHOUR);
	iowrite32(alrm->time.tm_min,  priv->iobase + HW_ALMIN);
	iowrite32(alrm->time.tm_sec,  priv->iobase + HW_ALSEC);

	iowrite32(alrm->enabled ? 0 : BM_AMR_OFF, priv->iobase + HW_AMR);
	spin_unlock_irqrestore(&priv->lock, irq_flags);

	return 0;
}

static int asm9260_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct asm9260_rtc_priv *priv = dev_get_drvdata(dev);

	iowrite32(enabled ? 0 : BM_AMR_OFF, priv->iobase + HW_AMR);
	return 0;
}

static const struct rtc_class_ops asm9260_rtc_ops = {
	.read_time		= asm9260_rtc_read_time,
	.set_time		= asm9260_rtc_set_time,
	.read_alarm		= asm9260_rtc_read_alarm,
	.set_alarm		= asm9260_rtc_set_alarm,
	.alarm_irq_enable	= asm9260_alarm_irq_enable,
};

static int asm9260_rtc_probe(struct platform_device *pdev)
{
	struct asm9260_rtc_priv *priv;
	struct device *dev = &pdev->dev;
	struct resource	*res;
	int irq_alarm, ret;
	u32 ccr;

	priv = devm_kzalloc(dev, sizeof(struct asm9260_rtc_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	platform_set_drvdata(pdev, priv);

	irq_alarm = platform_get_irq(pdev, 0);
	if (irq_alarm < 0) {
		dev_err(dev, "No alarm IRQ resource defined\n");
		return irq_alarm;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->iobase = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->iobase))
		return PTR_ERR(priv->iobase);

	priv->clk = devm_clk_get(dev, "ahb");
	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "Failed to enable clk!\n");
		return ret;
	}

	ccr = ioread32(priv->iobase + HW_CCR);
	/* if dev is not enabled, reset it */
	if ((ccr & (BM_CLKEN | BM_CTCRST)) != BM_CLKEN) {
		iowrite32(BM_CTCRST, priv->iobase + HW_CCR);
		ccr = 0;
	}

	iowrite32(BM_CLKEN | ccr, priv->iobase + HW_CCR);
	iowrite32(0, priv->iobase + HW_CIIR);
	iowrite32(BM_AMR_OFF, priv->iobase + HW_AMR);

	priv->rtc = devm_rtc_device_register(dev, dev_name(dev),
					     &asm9260_rtc_ops, THIS_MODULE);
	if (IS_ERR(priv->rtc)) {
		ret = PTR_ERR(priv->rtc);
		dev_err(dev, "Failed to register RTC device: %d\n", ret);
		goto err_return;
	}

	ret = devm_request_threaded_irq(dev, irq_alarm, NULL,
					asm9260_rtc_irq, IRQF_ONESHOT,
					dev_name(dev), priv);
	if (ret < 0) {
		dev_err(dev, "can't get irq %i, err %d\n",
			irq_alarm, ret);
		goto err_return;
	}

	return 0;

err_return:
	clk_disable_unprepare(priv->clk);
	return ret;
}

static int asm9260_rtc_remove(struct platform_device *pdev)
{
	struct asm9260_rtc_priv *priv = platform_get_drvdata(pdev);

	/* Disable alarm matching */
	iowrite32(BM_AMR_OFF, priv->iobase + HW_AMR);
	clk_disable_unprepare(priv->clk);
	return 0;
}

static const struct of_device_id asm9260_dt_ids[] = {
	{ .compatible = "alphascale,asm9260-rtc", },
	{}
};

static struct platform_driver asm9260_rtc_driver = {
	.probe		= asm9260_rtc_probe,
	.remove		= asm9260_rtc_remove,
	.driver		= {
		.name	= "asm9260-rtc",
		.owner	= THIS_MODULE,
		.of_match_table = asm9260_dt_ids,
	},
};

module_platform_driver(asm9260_rtc_driver);

MODULE_AUTHOR("Oleksij Rempel <linux@rempel-privat.de>");
MODULE_DESCRIPTION("Alphascale asm9260 SoC Realtime Clock Driver (RTC)");
MODULE_LICENSE("GPL");
