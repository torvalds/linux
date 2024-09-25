// SPDX-License-Identifier: GPL-2.0-only
/*
 * ST M48T59 RTC driver
 *
 * Copyright (c) 2007 Wind River Systems, Inc.
 *
 * Author: Mark Zhan <rongkai.zhan@windriver.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/rtc/m48t59.h>
#include <linux/bcd.h>
#include <linux/slab.h>

#ifndef NO_IRQ
#define NO_IRQ	(-1)
#endif

#define M48T59_READ(reg) (pdata->read_byte(dev, pdata->offset + reg))
#define M48T59_WRITE(val, reg) \
	(pdata->write_byte(dev, pdata->offset + reg, val))

#define M48T59_SET_BITS(mask, reg)	\
	M48T59_WRITE((M48T59_READ(reg) | (mask)), (reg))
#define M48T59_CLEAR_BITS(mask, reg)	\
	M48T59_WRITE((M48T59_READ(reg) & ~(mask)), (reg))

struct m48t59_private {
	void __iomem *ioaddr;
	int irq;
	struct rtc_device *rtc;
	spinlock_t lock; /* serialize the NVRAM and RTC access */
};

/*
 * This is the generic access method when the chip is memory-mapped
 */
static void
m48t59_mem_writeb(struct device *dev, u32 ofs, u8 val)
{
	struct m48t59_private *m48t59 = dev_get_drvdata(dev);

	writeb(val, m48t59->ioaddr+ofs);
}

static u8
m48t59_mem_readb(struct device *dev, u32 ofs)
{
	struct m48t59_private *m48t59 = dev_get_drvdata(dev);

	return readb(m48t59->ioaddr+ofs);
}

/*
 * NOTE: M48T59 only uses BCD mode
 */
static int m48t59_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct m48t59_plat_data *pdata = dev_get_platdata(dev);
	struct m48t59_private *m48t59 = dev_get_drvdata(dev);
	unsigned long flags;
	u8 val;

	spin_lock_irqsave(&m48t59->lock, flags);
	/* Issue the READ command */
	M48T59_SET_BITS(M48T59_CNTL_READ, M48T59_CNTL);

	tm->tm_year	= bcd2bin(M48T59_READ(M48T59_YEAR));
	/* tm_mon is 0-11 */
	tm->tm_mon	= bcd2bin(M48T59_READ(M48T59_MONTH)) - 1;
	tm->tm_mday	= bcd2bin(M48T59_READ(M48T59_MDAY));

	val = M48T59_READ(M48T59_WDAY);
	if ((pdata->type == M48T59RTC_TYPE_M48T59) &&
	    (val & M48T59_WDAY_CEB) && (val & M48T59_WDAY_CB)) {
		dev_dbg(dev, "Century bit is enabled\n");
		tm->tm_year += 100;	/* one century */
	}
#ifdef CONFIG_SPARC
	/* Sun SPARC machines count years since 1968 */
	tm->tm_year += 68;
#endif

	tm->tm_wday	= bcd2bin(val & 0x07);
	tm->tm_hour	= bcd2bin(M48T59_READ(M48T59_HOUR) & 0x3F);
	tm->tm_min	= bcd2bin(M48T59_READ(M48T59_MIN) & 0x7F);
	tm->tm_sec	= bcd2bin(M48T59_READ(M48T59_SEC) & 0x7F);

	/* Clear the READ bit */
	M48T59_CLEAR_BITS(M48T59_CNTL_READ, M48T59_CNTL);
	spin_unlock_irqrestore(&m48t59->lock, flags);

	dev_dbg(dev, "RTC read time %ptR\n", tm);
	return 0;
}

static int m48t59_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct m48t59_plat_data *pdata = dev_get_platdata(dev);
	struct m48t59_private *m48t59 = dev_get_drvdata(dev);
	unsigned long flags;
	u8 val = 0;
	int year = tm->tm_year;

#ifdef CONFIG_SPARC
	/* Sun SPARC machines count years since 1968 */
	year -= 68;
#endif

	dev_dbg(dev, "RTC set time %04d-%02d-%02d %02d/%02d/%02d\n",
		year + 1900, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	if (year < 0)
		return -EINVAL;

	spin_lock_irqsave(&m48t59->lock, flags);
	/* Issue the WRITE command */
	M48T59_SET_BITS(M48T59_CNTL_WRITE, M48T59_CNTL);

	M48T59_WRITE((bin2bcd(tm->tm_sec) & 0x7F), M48T59_SEC);
	M48T59_WRITE((bin2bcd(tm->tm_min) & 0x7F), M48T59_MIN);
	M48T59_WRITE((bin2bcd(tm->tm_hour) & 0x3F), M48T59_HOUR);
	M48T59_WRITE((bin2bcd(tm->tm_mday) & 0x3F), M48T59_MDAY);
	/* tm_mon is 0-11 */
	M48T59_WRITE((bin2bcd(tm->tm_mon + 1) & 0x1F), M48T59_MONTH);
	M48T59_WRITE(bin2bcd(year % 100), M48T59_YEAR);

	if (pdata->type == M48T59RTC_TYPE_M48T59 && (year >= 100))
		val = (M48T59_WDAY_CEB | M48T59_WDAY_CB);
	val |= (bin2bcd(tm->tm_wday) & 0x07);
	M48T59_WRITE(val, M48T59_WDAY);

	/* Clear the WRITE bit */
	M48T59_CLEAR_BITS(M48T59_CNTL_WRITE, M48T59_CNTL);
	spin_unlock_irqrestore(&m48t59->lock, flags);
	return 0;
}

/*
 * Read alarm time and date in RTC
 */
static int m48t59_rtc_readalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct m48t59_plat_data *pdata = dev_get_platdata(dev);
	struct m48t59_private *m48t59 = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time;
	unsigned long flags;
	u8 val;

	/* If no irq, we don't support ALARM */
	if (m48t59->irq == NO_IRQ)
		return -EIO;

	spin_lock_irqsave(&m48t59->lock, flags);
	/* Issue the READ command */
	M48T59_SET_BITS(M48T59_CNTL_READ, M48T59_CNTL);

	tm->tm_year = bcd2bin(M48T59_READ(M48T59_YEAR));
#ifdef CONFIG_SPARC
	/* Sun SPARC machines count years since 1968 */
	tm->tm_year += 68;
#endif
	/* tm_mon is 0-11 */
	tm->tm_mon = bcd2bin(M48T59_READ(M48T59_MONTH)) - 1;

	val = M48T59_READ(M48T59_WDAY);
	if ((val & M48T59_WDAY_CEB) && (val & M48T59_WDAY_CB))
		tm->tm_year += 100;	/* one century */

	tm->tm_mday = bcd2bin(M48T59_READ(M48T59_ALARM_DATE));
	tm->tm_hour = bcd2bin(M48T59_READ(M48T59_ALARM_HOUR));
	tm->tm_min = bcd2bin(M48T59_READ(M48T59_ALARM_MIN));
	tm->tm_sec = bcd2bin(M48T59_READ(M48T59_ALARM_SEC));

	/* Clear the READ bit */
	M48T59_CLEAR_BITS(M48T59_CNTL_READ, M48T59_CNTL);
	spin_unlock_irqrestore(&m48t59->lock, flags);

	dev_dbg(dev, "RTC read alarm time %ptR\n", tm);
	return rtc_valid_tm(tm);
}

/*
 * Set alarm time and date in RTC
 */
static int m48t59_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct m48t59_plat_data *pdata = dev_get_platdata(dev);
	struct m48t59_private *m48t59 = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time;
	u8 mday, hour, min, sec;
	unsigned long flags;
	int year = tm->tm_year;

#ifdef CONFIG_SPARC
	/* Sun SPARC machines count years since 1968 */
	year -= 68;
#endif

	/* If no irq, we don't support ALARM */
	if (m48t59->irq == NO_IRQ)
		return -EIO;

	if (year < 0)
		return -EINVAL;

	/*
	 * 0xff means "always match"
	 */
	mday = tm->tm_mday;
	mday = (mday >= 1 && mday <= 31) ? bin2bcd(mday) : 0xff;
	if (mday == 0xff)
		mday = M48T59_READ(M48T59_MDAY);

	hour = tm->tm_hour;
	hour = (hour < 24) ? bin2bcd(hour) : 0x00;

	min = tm->tm_min;
	min = (min < 60) ? bin2bcd(min) : 0x00;

	sec = tm->tm_sec;
	sec = (sec < 60) ? bin2bcd(sec) : 0x00;

	spin_lock_irqsave(&m48t59->lock, flags);
	/* Issue the WRITE command */
	M48T59_SET_BITS(M48T59_CNTL_WRITE, M48T59_CNTL);

	M48T59_WRITE(mday, M48T59_ALARM_DATE);
	M48T59_WRITE(hour, M48T59_ALARM_HOUR);
	M48T59_WRITE(min, M48T59_ALARM_MIN);
	M48T59_WRITE(sec, M48T59_ALARM_SEC);

	/* Clear the WRITE bit */
	M48T59_CLEAR_BITS(M48T59_CNTL_WRITE, M48T59_CNTL);
	spin_unlock_irqrestore(&m48t59->lock, flags);

	dev_dbg(dev, "RTC set alarm time %04d-%02d-%02d %02d/%02d/%02d\n",
		year + 1900, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);
	return 0;
}

/*
 * Handle commands from user-space
 */
static int m48t59_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct m48t59_plat_data *pdata = dev_get_platdata(dev);
	struct m48t59_private *m48t59 = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&m48t59->lock, flags);
	if (enabled)
		M48T59_WRITE(M48T59_INTR_AFE, M48T59_INTR);
	else
		M48T59_WRITE(0x00, M48T59_INTR);
	spin_unlock_irqrestore(&m48t59->lock, flags);

	return 0;
}

static int m48t59_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct m48t59_plat_data *pdata = dev_get_platdata(dev);
	struct m48t59_private *m48t59 = dev_get_drvdata(dev);
	unsigned long flags;
	u8 val;

	spin_lock_irqsave(&m48t59->lock, flags);
	val = M48T59_READ(M48T59_FLAGS);
	spin_unlock_irqrestore(&m48t59->lock, flags);

	seq_printf(seq, "battery\t\t: %s\n",
		 (val & M48T59_FLAGS_BF) ? "low" : "normal");
	return 0;
}

/*
 * IRQ handler for the RTC
 */
static irqreturn_t m48t59_rtc_interrupt(int irq, void *dev_id)
{
	struct device *dev = (struct device *)dev_id;
	struct m48t59_plat_data *pdata = dev_get_platdata(dev);
	struct m48t59_private *m48t59 = dev_get_drvdata(dev);
	u8 event;

	spin_lock(&m48t59->lock);
	event = M48T59_READ(M48T59_FLAGS);
	spin_unlock(&m48t59->lock);

	if (event & M48T59_FLAGS_AF) {
		rtc_update_irq(m48t59->rtc, 1, (RTC_AF | RTC_IRQF));
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static const struct rtc_class_ops m48t59_rtc_ops = {
	.read_time	= m48t59_rtc_read_time,
	.set_time	= m48t59_rtc_set_time,
	.read_alarm	= m48t59_rtc_readalarm,
	.set_alarm	= m48t59_rtc_setalarm,
	.proc		= m48t59_rtc_proc,
	.alarm_irq_enable = m48t59_rtc_alarm_irq_enable,
};

static int m48t59_nvram_read(void *priv, unsigned int offset, void *val,
			     size_t size)
{
	struct platform_device *pdev = priv;
	struct device *dev = &pdev->dev;
	struct m48t59_plat_data *pdata = dev_get_platdata(&pdev->dev);
	struct m48t59_private *m48t59 = platform_get_drvdata(pdev);
	ssize_t cnt = 0;
	unsigned long flags;
	u8 *buf = val;

	spin_lock_irqsave(&m48t59->lock, flags);

	for (; cnt < size; cnt++)
		*buf++ = M48T59_READ(cnt);

	spin_unlock_irqrestore(&m48t59->lock, flags);

	return 0;
}

static int m48t59_nvram_write(void *priv, unsigned int offset, void *val,
			      size_t size)
{
	struct platform_device *pdev = priv;
	struct device *dev = &pdev->dev;
	struct m48t59_plat_data *pdata = dev_get_platdata(&pdev->dev);
	struct m48t59_private *m48t59 = platform_get_drvdata(pdev);
	ssize_t cnt = 0;
	unsigned long flags;
	u8 *buf = val;

	spin_lock_irqsave(&m48t59->lock, flags);

	for (; cnt < size; cnt++)
		M48T59_WRITE(*buf++, cnt);

	spin_unlock_irqrestore(&m48t59->lock, flags);

	return 0;
}

static int m48t59_rtc_probe(struct platform_device *pdev)
{
	struct m48t59_plat_data *pdata = dev_get_platdata(&pdev->dev);
	struct m48t59_private *m48t59 = NULL;
	struct resource *res;
	int ret = -ENOMEM;
	struct nvmem_config nvmem_cfg = {
		.name = "m48t59-",
		.word_size = 1,
		.stride = 1,
		.reg_read = m48t59_nvram_read,
		.reg_write = m48t59_nvram_write,
		.priv = pdev,
	};

	/* This chip could be memory-mapped or I/O-mapped */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		res = platform_get_resource(pdev, IORESOURCE_IO, 0);
		if (!res)
			return -EINVAL;
	}

	if (res->flags & IORESOURCE_IO) {
		/* If we are I/O-mapped, the platform should provide
		 * the operations accessing chip registers.
		 */
		if (!pdata || !pdata->write_byte || !pdata->read_byte)
			return -EINVAL;
	} else if (res->flags & IORESOURCE_MEM) {
		/* we are memory-mapped */
		if (!pdata) {
			pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata),
						GFP_KERNEL);
			if (!pdata)
				return -ENOMEM;
			/* Ensure we only kmalloc platform data once */
			pdev->dev.platform_data = pdata;
		}
		if (!pdata->type)
			pdata->type = M48T59RTC_TYPE_M48T59;

		/* Try to use the generic memory read/write ops */
		if (!pdata->write_byte)
			pdata->write_byte = m48t59_mem_writeb;
		if (!pdata->read_byte)
			pdata->read_byte = m48t59_mem_readb;
	}

	m48t59 = devm_kzalloc(&pdev->dev, sizeof(*m48t59), GFP_KERNEL);
	if (!m48t59)
		return -ENOMEM;

	m48t59->ioaddr = pdata->ioaddr;

	if (!m48t59->ioaddr) {
		/* ioaddr not mapped externally */
		m48t59->ioaddr = devm_ioremap(&pdev->dev, res->start,
						resource_size(res));
		if (!m48t59->ioaddr)
			return ret;
	}

	/* Try to get irq number. We also can work in
	 * the mode without IRQ.
	 */
	m48t59->irq = platform_get_irq_optional(pdev, 0);
	if (m48t59->irq <= 0)
		m48t59->irq = NO_IRQ;

	if (m48t59->irq != NO_IRQ) {
		ret = devm_request_irq(&pdev->dev, m48t59->irq,
				m48t59_rtc_interrupt, IRQF_SHARED,
				"rtc-m48t59", &pdev->dev);
		if (ret)
			return ret;
	}

	m48t59->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(m48t59->rtc))
		return PTR_ERR(m48t59->rtc);

	switch (pdata->type) {
	case M48T59RTC_TYPE_M48T59:
		pdata->offset = 0x1ff0;
		break;
	case M48T59RTC_TYPE_M48T02:
		clear_bit(RTC_FEATURE_ALARM, m48t59->rtc->features);
		pdata->offset = 0x7f0;
		break;
	case M48T59RTC_TYPE_M48T08:
		clear_bit(RTC_FEATURE_ALARM, m48t59->rtc->features);
		pdata->offset = 0x1ff0;
		break;
	default:
		dev_err(&pdev->dev, "Unknown RTC type\n");
		return -ENODEV;
	}

	spin_lock_init(&m48t59->lock);
	platform_set_drvdata(pdev, m48t59);

	m48t59->rtc->ops = &m48t59_rtc_ops;
	m48t59->rtc->range_min = RTC_TIMESTAMP_BEGIN_1900;
	m48t59->rtc->range_max = RTC_TIMESTAMP_END_2099;

	nvmem_cfg.size = pdata->offset;
	ret = devm_rtc_nvmem_register(m48t59->rtc, &nvmem_cfg);
	if (ret)
		return ret;

	ret = devm_rtc_register_device(m48t59->rtc);
	if (ret)
		return ret;

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:rtc-m48t59");

static struct platform_driver m48t59_rtc_driver = {
	.driver		= {
		.name	= "rtc-m48t59",
	},
	.probe		= m48t59_rtc_probe,
};

module_platform_driver(m48t59_rtc_driver);

MODULE_AUTHOR("Mark Zhan <rongkai.zhan@windriver.com>");
MODULE_DESCRIPTION("M48T59/M48T02/M48T08 RTC driver");
MODULE_LICENSE("GPL");
