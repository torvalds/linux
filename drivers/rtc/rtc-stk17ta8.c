// SPDX-License-Identifier: GPL-2.0-only
/*
 * A RTC driver for the Simtek STK17TA8
 *
 * By Thomas Hommel <thomas.hommel@ge.com>
 *
 * Based on the DS1553 driver from
 * Atsushi Nemoto <anemo@mba.ocn.ne.jp>
 */

#include <linux/bcd.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/module.h>

#define RTC_REG_SIZE		0x20000
#define RTC_OFFSET		0x1fff0

#define RTC_FLAGS		(RTC_OFFSET + 0)
#define RTC_CENTURY		(RTC_OFFSET + 1)
#define RTC_SECONDS_ALARM	(RTC_OFFSET + 2)
#define RTC_MINUTES_ALARM	(RTC_OFFSET + 3)
#define RTC_HOURS_ALARM		(RTC_OFFSET + 4)
#define RTC_DATE_ALARM		(RTC_OFFSET + 5)
#define RTC_INTERRUPTS		(RTC_OFFSET + 6)
#define RTC_WATCHDOG		(RTC_OFFSET + 7)
#define RTC_CALIBRATION		(RTC_OFFSET + 8)
#define RTC_SECONDS		(RTC_OFFSET + 9)
#define RTC_MINUTES		(RTC_OFFSET + 10)
#define RTC_HOURS		(RTC_OFFSET + 11)
#define RTC_DAY			(RTC_OFFSET + 12)
#define RTC_DATE		(RTC_OFFSET + 13)
#define RTC_MONTH		(RTC_OFFSET + 14)
#define RTC_YEAR		(RTC_OFFSET + 15)

#define RTC_SECONDS_MASK	0x7f
#define RTC_DAY_MASK		0x07
#define RTC_CAL_MASK		0x3f

/* Bits in the Calibration register */
#define RTC_STOP		0x80

/* Bits in the Flags register */
#define RTC_FLAGS_AF		0x40
#define RTC_FLAGS_PF		0x20
#define RTC_WRITE		0x02
#define RTC_READ		0x01

/* Bits in the Interrupts register */
#define RTC_INTS_AIE		0x40

struct rtc_plat_data {
	struct rtc_device *rtc;
	void __iomem *ioaddr;
	unsigned long last_jiffies;
	int irq;
	unsigned int irqen;
	int alrm_sec;
	int alrm_min;
	int alrm_hour;
	int alrm_mday;
	spinlock_t lock;
};

static int stk17ta8_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);
	void __iomem *ioaddr = pdata->ioaddr;
	u8 flags;

	flags = readb(pdata->ioaddr + RTC_FLAGS);
	writeb(flags | RTC_WRITE, pdata->ioaddr + RTC_FLAGS);

	writeb(bin2bcd(tm->tm_year % 100), ioaddr + RTC_YEAR);
	writeb(bin2bcd(tm->tm_mon + 1), ioaddr + RTC_MONTH);
	writeb(bin2bcd(tm->tm_wday) & RTC_DAY_MASK, ioaddr + RTC_DAY);
	writeb(bin2bcd(tm->tm_mday), ioaddr + RTC_DATE);
	writeb(bin2bcd(tm->tm_hour), ioaddr + RTC_HOURS);
	writeb(bin2bcd(tm->tm_min), ioaddr + RTC_MINUTES);
	writeb(bin2bcd(tm->tm_sec) & RTC_SECONDS_MASK, ioaddr + RTC_SECONDS);
	writeb(bin2bcd((tm->tm_year + 1900) / 100), ioaddr + RTC_CENTURY);

	writeb(flags & ~RTC_WRITE, pdata->ioaddr + RTC_FLAGS);
	return 0;
}

static int stk17ta8_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);
	void __iomem *ioaddr = pdata->ioaddr;
	unsigned int year, month, day, hour, minute, second, week;
	unsigned int century;
	u8 flags;

	/* give enough time to update RTC in case of continuous read */
	if (pdata->last_jiffies == jiffies)
		msleep(1);
	pdata->last_jiffies = jiffies;

	flags = readb(pdata->ioaddr + RTC_FLAGS);
	writeb(flags | RTC_READ, ioaddr + RTC_FLAGS);
	second = readb(ioaddr + RTC_SECONDS) & RTC_SECONDS_MASK;
	minute = readb(ioaddr + RTC_MINUTES);
	hour = readb(ioaddr + RTC_HOURS);
	day = readb(ioaddr + RTC_DATE);
	week = readb(ioaddr + RTC_DAY) & RTC_DAY_MASK;
	month = readb(ioaddr + RTC_MONTH);
	year = readb(ioaddr + RTC_YEAR);
	century = readb(ioaddr + RTC_CENTURY);
	writeb(flags & ~RTC_READ, ioaddr + RTC_FLAGS);
	tm->tm_sec = bcd2bin(second);
	tm->tm_min = bcd2bin(minute);
	tm->tm_hour = bcd2bin(hour);
	tm->tm_mday = bcd2bin(day);
	tm->tm_wday = bcd2bin(week);
	tm->tm_mon = bcd2bin(month) - 1;
	/* year is 1900 + tm->tm_year */
	tm->tm_year = bcd2bin(year) + bcd2bin(century) * 100 - 1900;

	return 0;
}

static void stk17ta8_rtc_update_alarm(struct rtc_plat_data *pdata)
{
	void __iomem *ioaddr = pdata->ioaddr;
	unsigned long irqflags;
	u8 flags;

	spin_lock_irqsave(&pdata->lock, irqflags);

	flags = readb(ioaddr + RTC_FLAGS);
	writeb(flags | RTC_WRITE, ioaddr + RTC_FLAGS);

	writeb(pdata->alrm_mday < 0 || (pdata->irqen & RTC_UF) ?
	       0x80 : bin2bcd(pdata->alrm_mday),
	       ioaddr + RTC_DATE_ALARM);
	writeb(pdata->alrm_hour < 0 || (pdata->irqen & RTC_UF) ?
	       0x80 : bin2bcd(pdata->alrm_hour),
	       ioaddr + RTC_HOURS_ALARM);
	writeb(pdata->alrm_min < 0 || (pdata->irqen & RTC_UF) ?
	       0x80 : bin2bcd(pdata->alrm_min),
	       ioaddr + RTC_MINUTES_ALARM);
	writeb(pdata->alrm_sec < 0 || (pdata->irqen & RTC_UF) ?
	       0x80 : bin2bcd(pdata->alrm_sec),
	       ioaddr + RTC_SECONDS_ALARM);
	writeb(pdata->irqen ? RTC_INTS_AIE : 0, ioaddr + RTC_INTERRUPTS);
	readb(ioaddr + RTC_FLAGS);	/* clear interrupts */
	writeb(flags & ~RTC_WRITE, ioaddr + RTC_FLAGS);
	spin_unlock_irqrestore(&pdata->lock, irqflags);
}

static int stk17ta8_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);

	if (pdata->irq <= 0)
		return -EINVAL;
	pdata->alrm_mday = alrm->time.tm_mday;
	pdata->alrm_hour = alrm->time.tm_hour;
	pdata->alrm_min = alrm->time.tm_min;
	pdata->alrm_sec = alrm->time.tm_sec;
	if (alrm->enabled)
		pdata->irqen |= RTC_AF;
	stk17ta8_rtc_update_alarm(pdata);
	return 0;
}

static int stk17ta8_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);

	if (pdata->irq <= 0)
		return -EINVAL;
	alrm->time.tm_mday = pdata->alrm_mday < 0 ? 0 : pdata->alrm_mday;
	alrm->time.tm_hour = pdata->alrm_hour < 0 ? 0 : pdata->alrm_hour;
	alrm->time.tm_min = pdata->alrm_min < 0 ? 0 : pdata->alrm_min;
	alrm->time.tm_sec = pdata->alrm_sec < 0 ? 0 : pdata->alrm_sec;
	alrm->enabled = (pdata->irqen & RTC_AF) ? 1 : 0;
	return 0;
}

static irqreturn_t stk17ta8_rtc_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;
	unsigned long events = 0;

	spin_lock(&pdata->lock);
	/* read and clear interrupt */
	if (readb(ioaddr + RTC_FLAGS) & RTC_FLAGS_AF) {
		events = RTC_IRQF;
		if (readb(ioaddr + RTC_SECONDS_ALARM) & 0x80)
			events |= RTC_UF;
		else
			events |= RTC_AF;
		rtc_update_irq(pdata->rtc, 1, events);
	}
	spin_unlock(&pdata->lock);
	return events ? IRQ_HANDLED : IRQ_NONE;
}

static int stk17ta8_rtc_alarm_irq_enable(struct device *dev,
	unsigned int enabled)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);

	if (pdata->irq <= 0)
		return -EINVAL;
	if (enabled)
		pdata->irqen |= RTC_AF;
	else
		pdata->irqen &= ~RTC_AF;
	stk17ta8_rtc_update_alarm(pdata);
	return 0;
}

static const struct rtc_class_ops stk17ta8_rtc_ops = {
	.read_time		= stk17ta8_rtc_read_time,
	.set_time		= stk17ta8_rtc_set_time,
	.read_alarm		= stk17ta8_rtc_read_alarm,
	.set_alarm		= stk17ta8_rtc_set_alarm,
	.alarm_irq_enable	= stk17ta8_rtc_alarm_irq_enable,
};

static int stk17ta8_nvram_read(void *priv, unsigned int pos, void *val,
			       size_t bytes)
{
	struct rtc_plat_data *pdata = priv;
	void __iomem *ioaddr = pdata->ioaddr;
	u8 *buf = val;

	for (; bytes; bytes--)
		*buf++ = readb(ioaddr + pos++);
	return 0;
}

static int stk17ta8_nvram_write(void *priv, unsigned int pos, void *val,
				size_t bytes)
{
	struct rtc_plat_data *pdata = priv;
	void __iomem *ioaddr = pdata->ioaddr;
	u8 *buf = val;

	for (; bytes; bytes--)
		writeb(*buf++, ioaddr + pos++);
	return 0;
}

static int stk17ta8_rtc_probe(struct platform_device *pdev)
{
	unsigned int cal;
	unsigned int flags;
	struct rtc_plat_data *pdata;
	void __iomem *ioaddr;
	int ret = 0;
	struct nvmem_config nvmem_cfg = {
		.name = "stk17ta8_nvram",
		.word_size = 1,
		.stride = 1,
		.size = RTC_OFFSET,
		.reg_read = stk17ta8_nvram_read,
		.reg_write = stk17ta8_nvram_write,
	};

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	ioaddr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ioaddr))
		return PTR_ERR(ioaddr);
	pdata->ioaddr = ioaddr;
	pdata->irq = platform_get_irq(pdev, 0);

	/* turn RTC on if it was not on */
	cal = readb(ioaddr + RTC_CALIBRATION);
	if (cal & RTC_STOP) {
		cal &= RTC_CAL_MASK;
		flags = readb(ioaddr + RTC_FLAGS);
		writeb(flags | RTC_WRITE, ioaddr + RTC_FLAGS);
		writeb(cal, ioaddr + RTC_CALIBRATION);
		writeb(flags & ~RTC_WRITE, ioaddr + RTC_FLAGS);
	}
	if (readb(ioaddr + RTC_FLAGS) & RTC_FLAGS_PF)
		dev_warn(&pdev->dev, "voltage-low detected.\n");

	spin_lock_init(&pdata->lock);
	pdata->last_jiffies = jiffies;
	platform_set_drvdata(pdev, pdata);
	if (pdata->irq > 0) {
		writeb(0, ioaddr + RTC_INTERRUPTS);
		if (devm_request_irq(&pdev->dev, pdata->irq,
				stk17ta8_rtc_interrupt,
				IRQF_SHARED,
				pdev->name, pdev) < 0) {
			dev_warn(&pdev->dev, "interrupt not available.\n");
			pdata->irq = 0;
		}
	}

	pdata->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(pdata->rtc))
		return PTR_ERR(pdata->rtc);

	pdata->rtc->ops = &stk17ta8_rtc_ops;

	nvmem_cfg.priv = pdata;
	ret = devm_rtc_nvmem_register(pdata->rtc, &nvmem_cfg);
	if (ret)
		return ret;

	return devm_rtc_register_device(pdata->rtc);
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:stk17ta8");

static struct platform_driver stk17ta8_rtc_driver = {
	.probe		= stk17ta8_rtc_probe,
	.driver		= {
		.name	= "stk17ta8",
	},
};

module_platform_driver(stk17ta8_rtc_driver);

MODULE_AUTHOR("Thomas Hommel <thomas.hommel@ge.com>");
MODULE_DESCRIPTION("Simtek STK17TA8 RTC driver");
MODULE_LICENSE("GPL");
