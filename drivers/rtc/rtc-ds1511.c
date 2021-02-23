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

enum ds1511reg {
	DS1511_SEC = 0x0,
	DS1511_MIN = 0x1,
	DS1511_HOUR = 0x2,
	DS1511_DOW = 0x3,
	DS1511_DOM = 0x4,
	DS1511_MONTH = 0x5,
	DS1511_YEAR = 0x6,
	DS1511_CENTURY = 0x7,
	DS1511_AM1_SEC = 0x8,
	DS1511_AM2_MIN = 0x9,
	DS1511_AM3_HOUR = 0xa,
	DS1511_AM4_DATE = 0xb,
	DS1511_WD_MSEC = 0xc,
	DS1511_WD_SEC = 0xd,
	DS1511_CONTROL_A = 0xe,
	DS1511_CONTROL_B = 0xf,
	DS1511_RAMADDR_LSB = 0x10,
	DS1511_RAMDATA = 0x13
};

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

#define RTC_CMD		DS1511_CONTROL_B
#define RTC_CMD1	DS1511_CONTROL_A

#define RTC_ALARM_SEC	DS1511_AM1_SEC
#define RTC_ALARM_MIN	DS1511_AM2_MIN
#define RTC_ALARM_HOUR	DS1511_AM3_HOUR
#define RTC_ALARM_DATE	DS1511_AM4_DATE

#define RTC_SEC		DS1511_SEC
#define RTC_MIN		DS1511_MIN
#define RTC_HOUR	DS1511_HOUR
#define RTC_DOW		DS1511_DOW
#define RTC_DOM		DS1511_DOM
#define RTC_MON		DS1511_MONTH
#define RTC_YEAR	DS1511_YEAR
#define RTC_CENTURY	DS1511_CENTURY

#define RTC_TIE	DS1511_TIE
#define RTC_TE	DS1511_TE

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

static noinline void
rtc_write(uint8_t val, uint32_t reg)
{
	writeb(val, ds1511_base + (reg * reg_spacing));
}

static inline void
rtc_write_alarm(uint8_t val, enum ds1511reg reg)
{
	rtc_write((val | 0x80), reg);
}

static noinline uint8_t
rtc_read(enum ds1511reg reg)
{
	return readb(ds1511_base + (reg * reg_spacing));
}

static inline void
rtc_disable_update(void)
{
	rtc_write((rtc_read(RTC_CMD) & ~RTC_TE), RTC_CMD);
}

static void
rtc_enable_update(void)
{
	rtc_write((rtc_read(RTC_CMD) | RTC_TE), RTC_CMD);
}

/*
 * #define DS1511_WDOG_RESET_SUPPORT
 *
 * Uncomment this if you want to use these routines in
 * some platform code.
 */
#ifdef DS1511_WDOG_RESET_SUPPORT
/*
 * just enough code to set the watchdog timer so that it
 * will reboot the system
 */
void
ds1511_wdog_set(unsigned long deciseconds)
{
	/*
	 * the wdog timer can take 99.99 seconds
	 */
	deciseconds %= 10000;
	/*
	 * set the wdog values in the wdog registers
	 */
	rtc_write(bin2bcd(deciseconds % 100), DS1511_WD_MSEC);
	rtc_write(bin2bcd(deciseconds / 100), DS1511_WD_SEC);
	/*
	 * set wdog enable and wdog 'steering' bit to issue a reset
	 */
	rtc_write(rtc_read(RTC_CMD) | DS1511_WDE | DS1511_WDS, RTC_CMD);
}

void
ds1511_wdog_disable(void)
{
	/*
	 * clear wdog enable and wdog 'steering' bits
	 */
	rtc_write(rtc_read(RTC_CMD) & ~(DS1511_WDE | DS1511_WDS), RTC_CMD);
	/*
	 * clear the wdog counter
	 */
	rtc_write(0, DS1511_WD_MSEC);
	rtc_write(0, DS1511_WD_SEC);
}
#endif

/*
 * set the rtc chip's idea of the time.
 * stupidly, some callers call with year unmolested;
 * and some call with  year = year - 1900.  thanks.
 */
static int ds1511_rtc_set_time(struct device *dev, struct rtc_time *rtc_tm)
{
	u8 mon, day, dow, hrs, min, sec, yrs, cen;
	unsigned long flags;

	/*
	 * won't have to change this for a while
	 */
	if (rtc_tm->tm_year < 1900)
		rtc_tm->tm_year += 1900;

	if (rtc_tm->tm_year < 1970)
		return -EINVAL;

	yrs = rtc_tm->tm_year % 100;
	cen = rtc_tm->tm_year / 100;
	mon = rtc_tm->tm_mon + 1;   /* tm_mon starts at zero */
	day = rtc_tm->tm_mday;
	dow = rtc_tm->tm_wday & 0x7; /* automatic BCD */
	hrs = rtc_tm->tm_hour;
	min = rtc_tm->tm_min;
	sec = rtc_tm->tm_sec;

	if ((mon > 12) || (day == 0))
		return -EINVAL;

	if (day > rtc_month_days(rtc_tm->tm_mon, rtc_tm->tm_year))
		return -EINVAL;

	if ((hrs >= 24) || (min >= 60) || (sec >= 60))
		return -EINVAL;

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
	rtc_write(cen, RTC_CENTURY);
	rtc_write(yrs, RTC_YEAR);
	rtc_write((rtc_read(RTC_MON) & 0xe0) | mon, RTC_MON);
	rtc_write(day, RTC_DOM);
	rtc_write(hrs, RTC_HOUR);
	rtc_write(min, RTC_MIN);
	rtc_write(sec, RTC_SEC);
	rtc_write(dow, RTC_DOW);
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

	rtc_tm->tm_sec = rtc_read(RTC_SEC) & 0x7f;
	rtc_tm->tm_min = rtc_read(RTC_MIN) & 0x7f;
	rtc_tm->tm_hour = rtc_read(RTC_HOUR) & 0x3f;
	rtc_tm->tm_mday = rtc_read(RTC_DOM) & 0x3f;
	rtc_tm->tm_wday = rtc_read(RTC_DOW) & 0x7;
	rtc_tm->tm_mon = rtc_read(RTC_MON) & 0x1f;
	rtc_tm->tm_year = rtc_read(RTC_YEAR) & 0x7f;
	century = rtc_read(RTC_CENTURY);

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

/*
 * write the alarm register settings
 *
 * we only have the use to interrupt every second, otherwise
 * known as the update interrupt, or the interrupt if the whole
 * date/hours/mins/secs matches.  the ds1511 has many more
 * permutations, but the kernel doesn't.
 */
static void
ds1511_rtc_update_alarm(struct rtc_plat_data *pdata)
{
	unsigned long flags;

	spin_lock_irqsave(&pdata->lock, flags);
	rtc_write(pdata->alrm_mday < 0 || (pdata->irqen & RTC_UF) ?
	       0x80 : bin2bcd(pdata->alrm_mday) & 0x3f,
	       RTC_ALARM_DATE);
	rtc_write(pdata->alrm_hour < 0 || (pdata->irqen & RTC_UF) ?
	       0x80 : bin2bcd(pdata->alrm_hour) & 0x3f,
	       RTC_ALARM_HOUR);
	rtc_write(pdata->alrm_min < 0 || (pdata->irqen & RTC_UF) ?
	       0x80 : bin2bcd(pdata->alrm_min) & 0x7f,
	       RTC_ALARM_MIN);
	rtc_write(pdata->alrm_sec < 0 || (pdata->irqen & RTC_UF) ?
	       0x80 : bin2bcd(pdata->alrm_sec) & 0x7f,
	       RTC_ALARM_SEC);
	rtc_write(rtc_read(RTC_CMD) | (pdata->irqen ? RTC_TIE : 0), RTC_CMD);
	rtc_read(RTC_CMD1);	/* clear interrupts */
	spin_unlock_irqrestore(&pdata->lock, flags);
}

static int
ds1511_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
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

	ds1511_rtc_update_alarm(pdata);
	return 0;
}

static int
ds1511_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
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

static irqreturn_t
ds1511_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	unsigned long events = 0;

	spin_lock(&pdata->lock);
	/*
	 * read and clear interrupt
	 */
	if (rtc_read(RTC_CMD1) & DS1511_IRQF) {
		events = RTC_IRQF;
		if (rtc_read(RTC_ALARM_SEC) & 0x80)
			events |= RTC_UF;
		else
			events |= RTC_AF;
		rtc_update_irq(pdata->rtc, 1, events);
	}
	spin_unlock(&pdata->lock);
	return events ? IRQ_HANDLED : IRQ_NONE;
}

static int ds1511_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);

	if (pdata->irq <= 0)
		return -EINVAL;
	if (enabled)
		pdata->irqen |= RTC_AF;
	else
		pdata->irqen &= ~RTC_AF;
	ds1511_rtc_update_alarm(pdata);
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
	rtc_write(DS1511_BME, RTC_CMD);
	rtc_write(0, RTC_CMD1);
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
	if (rtc_read(RTC_CMD1) & DS1511_BLF1)
		dev_warn(&pdev->dev, "voltage-low detected.\n");

	spin_lock_init(&pdata->lock);
	platform_set_drvdata(pdev, pdata);

	pdata->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(pdata->rtc))
		return PTR_ERR(pdata->rtc);

	pdata->rtc->ops = &ds1511_rtc_ops;

	ret = devm_rtc_register_device(pdata->rtc);
	if (ret)
		return ret;

	devm_rtc_nvmem_register(pdata->rtc, &ds1511_nvmem_cfg);

	/*
	 * if the platform has an interrupt in mind for this device,
	 * then by all means, set it
	 */
	if (pdata->irq > 0) {
		rtc_read(RTC_CMD1);
		if (devm_request_irq(&pdev->dev, pdata->irq, ds1511_interrupt,
			IRQF_SHARED, pdev->name, pdev) < 0) {

			dev_warn(&pdev->dev, "interrupt not available.\n");
			pdata->irq = 0;
		}
	}

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
