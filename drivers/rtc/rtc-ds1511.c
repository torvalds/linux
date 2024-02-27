// SPDX-License-Identifier: GPL-2.0-only
/*
 * An rtc driver for the Dallas DS1511
 *
 * Copyright (C) 2006 Atsushi Nemoto <anemo@mba.ocn.ne.jp>
 * Copyright (C) 2007 Andrew Sharp <andy.sharp@lsi.com>
 *
 * Real time clock driver for the Dallas 1511 chip, which also
 * contains a watchdog timer.  There is a tiny amount of code that
 * platform code could use to mess with the watchdog device a little
 * bit, but not a full watchdog driver.
 */

#include <linux/bcd.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/module.h>

#define DS1511_SEC		0x0
#define DS1511_MIN		0x1
#define DS1511_HOUR		0x2
#define DS1511_DOW		0x3
#define DS1511_DOM		0x4
#define DS1511_MONTH		0x5
#define DS1511_YEAR		0x6
#define DS1511_CENTURY		0x7
#define DS1511_AM1_SEC		0x8
#define DS1511_AM2_MIN		0x9
#define DS1511_AM3_HOUR		0xa
#define DS1511_AM4_DATE		0xb
#define DS1511_WD_MSEC		0xc
#define DS1511_WD_SEC		0xd
#define DS1511_CONTROL_A	0xe
#define DS1511_CONTROL_B	0xf
#define DS1511_RAMADDR_LSB	0x10
#define DS1511_RAMDATA		0x13

#define DS1511_BLF1	0x80
#define DS1511_BLF2	0x40
#define DS1511_PRS	0x20
#define DS1511_PAB	0x10
#define DS1511_TDF	0x08
#define DS1511_KSF	0x04
#define DS1511_WDF	0x02
#define DS1511_IRQF	0x01
#define DS1511_TE	0x80
#define DS1511_CS	0x40
#define DS1511_BME	0x20
#define DS1511_TPE	0x10
#define DS1511_TIE	0x08
#define DS1511_KIE	0x04
#define DS1511_WDE	0x02
#define DS1511_WDS	0x01
#define DS1511_RAM_MAX	0x100

struct rtc_plat_data {
	struct rtc_device *rtc;
	void __iomem *ioaddr;		/* virtual base address */
	int irq;
	unsigned int irqen;
	int alrm_sec;
	int alrm_min;
	int alrm_hour;
	int alrm_mday;
	spinlock_t lock;
};

static DEFINE_SPINLOCK(ds1511_lock);

static __iomem char *ds1511_base;
static u32 reg_spacing = 1;

static noinline void rtc_write(uint8_t val, uint32_t reg)
{
	writeb(val, ds1511_base + (reg * reg_spacing));
}

static noinline uint8_t rtc_read(uint32_t reg)
{
	return readb(ds1511_base + (reg * reg_spacing));
}

static inline void rtc_disable_update(void)
{
	rtc_write((rtc_read(DS1511_CONTROL_B) & ~DS1511_TE), DS1511_CONTROL_B);
}

static void rtc_enable_update(void)
{
	rtc_write((rtc_read(DS1511_CONTROL_B) | DS1511_TE), DS1511_CONTROL_B);
}

static int ds1511_rtc_set_time(struct device *dev, struct rtc_time *rtc_tm)
{
	u8 mon, day, dow, hrs, min, sec, yrs, cen;
	unsigned long flags;

	yrs = rtc_tm->tm_year % 100;
	cen = 19 + rtc_tm->tm_year / 100;
	mon = rtc_tm->tm_mon + 1;   /* tm_mon starts at zero */
	day = rtc_tm->tm_mday;
	dow = rtc_tm->tm_wday & 0x7; /* automatic BCD */
	hrs = rtc_tm->tm_hour;
	min = rtc_tm->tm_min;
	sec = rtc_tm->tm_sec;

	/*
	 * each register is a different number of valid bits
	 */
	sec = bin2bcd(sec) & 0x7f;
	min = bin2bcd(min) & 0x7f;
	hrs = bin2bcd(hrs) & 0x3f;
	day = bin2bcd(day) & 0x3f;
	mon = bin2bcd(mon) & 0x1f;
	yrs = bin2bcd(yrs) & 0xff;
	cen = bin2bcd(cen) & 0xff;

	spin_lock_irqsave(&ds1511_lock, flags);
	rtc_disable_update();
	rtc_write(cen, DS1511_CENTURY);
	rtc_write(yrs, DS1511_YEAR);
	rtc_write((rtc_read(DS1511_MONTH) & 0xe0) | mon, DS1511_MONTH);
	rtc_write(day, DS1511_DOM);
	rtc_write(hrs, DS1511_HOUR);
	rtc_write(min, DS1511_MIN);
	rtc_write(sec, DS1511_SEC);
	rtc_write(dow, DS1511_DOW);
	rtc_enable_update();
	spin_unlock_irqrestore(&ds1511_lock, flags);

	return 0;
}

static int ds1511_rtc_read_time(struct device *dev, struct rtc_time *rtc_tm)
{
	unsigned int century;
	unsigned long flags;

	spin_lock_irqsave(&ds1511_lock, flags);
	rtc_disable_update();

	rtc_tm->tm_sec = rtc_read(DS1511_SEC) & 0x7f;
	rtc_tm->tm_min = rtc_read(DS1511_MIN) & 0x7f;
	rtc_tm->tm_hour = rtc_read(DS1511_HOUR) & 0x3f;
	rtc_tm->tm_mday = rtc_read(DS1511_DOM) & 0x3f;
	rtc_tm->tm_wday = rtc_read(DS1511_DOW) & 0x7;
	rtc_tm->tm_mon = rtc_read(DS1511_MONTH) & 0x1f;
	rtc_tm->tm_year = rtc_read(DS1511_YEAR) & 0x7f;
	century = rtc_read(DS1511_CENTURY);

	rtc_enable_update();
	spin_unlock_irqrestore(&ds1511_lock, flags);

	rtc_tm->tm_sec = bcd2bin(rtc_tm->tm_sec);
	rtc_tm->tm_min = bcd2bin(rtc_tm->tm_min);
	rtc_tm->tm_hour = bcd2bin(rtc_tm->tm_hour);
	rtc_tm->tm_mday = bcd2bin(rtc_tm->tm_mday);
	rtc_tm->tm_wday = bcd2bin(rtc_tm->tm_wday);
	rtc_tm->tm_mon = bcd2bin(rtc_tm->tm_mon);
	rtc_tm->tm_year = bcd2bin(rtc_tm->tm_year);
	century = bcd2bin(century) * 100;

	/*
	 * Account for differences between how the RTC uses the values
	 * and how they are defined in a struct rtc_time;
	 */
	century += rtc_tm->tm_year;
	rtc_tm->tm_year = century - 1900;

	rtc_tm->tm_mon--;

	return 0;
}

static void ds1511_rtc_alarm_enable(unsigned int enabled)
{
	rtc_write(rtc_read(DS1511_CONTROL_B) | (enabled ? DS1511_TIE : 0), DS1511_CONTROL_B);
}

static int ds1511_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);
	unsigned long flags;

	pdata->alrm_mday = alrm->time.tm_mday;
	pdata->alrm_hour = alrm->time.tm_hour;
	pdata->alrm_min = alrm->time.tm_min;
	pdata->alrm_sec = alrm->time.tm_sec;
	if (alrm->enabled)
		pdata->irqen |= RTC_AF;

	spin_lock_irqsave(&pdata->lock, flags);
	rtc_write(pdata->alrm_mday < 0 ? 0x80 : bin2bcd(pdata->alrm_mday) & 0x3f,
	       DS1511_AM4_DATE);
	rtc_write(pdata->alrm_hour < 0 ? 0x80 : bin2bcd(pdata->alrm_hour) & 0x3f,
	       DS1511_AM3_HOUR);
	rtc_write(pdata->alrm_min < 0 ? 0x80 : bin2bcd(pdata->alrm_min) & 0x7f,
	       DS1511_AM2_MIN);
	rtc_write(pdata->alrm_sec < 0 ? 0x80 : bin2bcd(pdata->alrm_sec) & 0x7f,
	       DS1511_AM1_SEC);
	ds1511_rtc_alarm_enable(alrm->enabled);

	rtc_read(DS1511_CONTROL_A);	/* clear interrupts */
	spin_unlock_irqrestore(&pdata->lock, flags);

	return 0;
}

static int ds1511_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);

	alrm->time.tm_mday = pdata->alrm_mday < 0 ? 0 : pdata->alrm_mday;
	alrm->time.tm_hour = pdata->alrm_hour < 0 ? 0 : pdata->alrm_hour;
	alrm->time.tm_min = pdata->alrm_min < 0 ? 0 : pdata->alrm_min;
	alrm->time.tm_sec = pdata->alrm_sec < 0 ? 0 : pdata->alrm_sec;
	alrm->enabled = (pdata->irqen & RTC_AF) ? 1 : 0;
	return 0;
}

static irqreturn_t ds1511_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	unsigned long events = 0;

	spin_lock(&pdata->lock);
	/*
	 * read and clear interrupt
	 */
	if (rtc_read(DS1511_CONTROL_A) & DS1511_IRQF) {
		events = RTC_IRQF | RTC_AF;
		rtc_update_irq(pdata->rtc, 1, events);
	}
	spin_unlock(&pdata->lock);
	return events ? IRQ_HANDLED : IRQ_NONE;
}

static int ds1511_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&pdata->lock, flags);
	ds1511_rtc_alarm_enable(enabled);
	spin_unlock_irqrestore(&pdata->lock, flags);

	return 0;
}

static const struct rtc_class_ops ds1511_rtc_ops = {
	.read_time		= ds1511_rtc_read_time,
	.set_time		= ds1511_rtc_set_time,
	.read_alarm		= ds1511_rtc_read_alarm,
	.set_alarm		= ds1511_rtc_set_alarm,
	.alarm_irq_enable	= ds1511_rtc_alarm_irq_enable,
};

static int ds1511_nvram_read(void *priv, unsigned int pos, void *buf,
			     size_t size)
{
	int i;

	rtc_write(pos, DS1511_RAMADDR_LSB);
	for (i = 0; i < size; i++)
		*(char *)buf++ = rtc_read(DS1511_RAMDATA);

	return 0;
}

static int ds1511_nvram_write(void *priv, unsigned int pos, void *buf,
			      size_t size)
{
	int i;

	rtc_write(pos, DS1511_RAMADDR_LSB);
	for (i = 0; i < size; i++)
		rtc_write(*(char *)buf++, DS1511_RAMDATA);

	return 0;
}

static int ds1511_rtc_probe(struct platform_device *pdev)
{
	struct rtc_plat_data *pdata;
	int ret = 0;
	struct nvmem_config ds1511_nvmem_cfg = {
		.name = "ds1511_nvram",
		.word_size = 1,
		.stride = 1,
		.size = DS1511_RAM_MAX,
		.reg_read = ds1511_nvram_read,
		.reg_write = ds1511_nvram_write,
		.priv = &pdev->dev,
	};

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	ds1511_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ds1511_base))
		return PTR_ERR(ds1511_base);
	pdata->ioaddr = ds1511_base;
	pdata->irq = platform_get_irq(pdev, 0);

	/*
	 * turn on the clock and the crystal, etc.
	 */
	rtc_write(DS1511_BME, DS1511_CONTROL_B);
	rtc_write(0, DS1511_CONTROL_A);
	/*
	 * clear the wdog counter
	 */
	rtc_write(0, DS1511_WD_MSEC);
	rtc_write(0, DS1511_WD_SEC);
	/*
	 * start the clock
	 */
	rtc_enable_update();

	/*
	 * check for a dying bat-tree
	 */
	if (rtc_read(DS1511_CONTROL_A) & DS1511_BLF1)
		dev_warn(&pdev->dev, "voltage-low detected.\n");

	spin_lock_init(&pdata->lock);
	platform_set_drvdata(pdev, pdata);

	pdata->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(pdata->rtc))
		return PTR_ERR(pdata->rtc);

	pdata->rtc->ops = &ds1511_rtc_ops;

	/*
	 * if the platform has an interrupt in mind for this device,
	 * then by all means, set it
	 */
	if (pdata->irq > 0) {
		rtc_read(DS1511_CONTROL_A);
		if (devm_request_irq(&pdev->dev, pdata->irq, ds1511_interrupt,
			IRQF_SHARED, pdev->name, pdev) < 0) {

			dev_warn(&pdev->dev, "interrupt not available.\n");
			pdata->irq = 0;
		}
	}

	if (pdata->irq == 0)
		clear_bit(RTC_FEATURE_ALARM, pdata->rtc->features);

	ret = devm_rtc_register_device(pdata->rtc);
	if (ret)
		return ret;

	devm_rtc_nvmem_register(pdata->rtc, &ds1511_nvmem_cfg);

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:ds1511");

static struct platform_driver ds1511_rtc_driver = {
	.probe		= ds1511_rtc_probe,
	.driver		= {
		.name	= "ds1511",
	},
};

module_platform_driver(ds1511_rtc_driver);

MODULE_AUTHOR("Andrew Sharp <andy.sharp@lsi.com>");
MODULE_DESCRIPTION("Dallas DS1511 RTC driver");
MODULE_LICENSE("GPL");
