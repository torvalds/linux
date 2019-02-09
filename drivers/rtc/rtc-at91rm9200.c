/*
 *	Real Time Clock interface for Linux on Atmel AT91RM9200
 *
 *	Copyright (C) 2002 Rick Bronson
 *
 *	Converted to RTC class model by Andrew Victor
 *
 *	Ported to Linux 2.6 by Steven Scholz
 *	Based on s3c2410-rtc.c Simtec Electronics
 *
 *	Based on sa1100-rtc.c by Nils Faerber
 *	Based on rtc.c by Paul Gortmaker
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */

#include <linux/bcd.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/spinlock.h>
#include <linux/suspend.h>
#include <linux/time.h>
#include <linux/uaccess.h>

#include "rtc-at91rm9200.h"

#define at91_rtc_read(field) \
	readl_relaxed(at91_rtc_regs + field)
#define at91_rtc_write(field, val) \
	writel_relaxed((val), at91_rtc_regs + field)

struct at91_rtc_config {
	bool use_shadow_imr;
};

static const struct at91_rtc_config *at91_rtc_config;
static DECLARE_COMPLETION(at91_rtc_updated);
static DECLARE_COMPLETION(at91_rtc_upd_rdy);
static void __iomem *at91_rtc_regs;
static int irq;
static DEFINE_SPINLOCK(at91_rtc_lock);
static u32 at91_rtc_shadow_imr;
static bool suspended;
static DEFINE_SPINLOCK(suspended_lock);
static unsigned long cached_events;
static u32 at91_rtc_imr;
static struct clk *sclk;

static void at91_rtc_write_ier(u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(&at91_rtc_lock, flags);
	at91_rtc_shadow_imr |= mask;
	at91_rtc_write(AT91_RTC_IER, mask);
	spin_unlock_irqrestore(&at91_rtc_lock, flags);
}

static void at91_rtc_write_idr(u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(&at91_rtc_lock, flags);
	at91_rtc_write(AT91_RTC_IDR, mask);
	/*
	 * Register read back (of any RTC-register) needed to make sure
	 * IDR-register write has reached the peripheral before updating
	 * shadow mask.
	 *
	 * Note that there is still a possibility that the mask is updated
	 * before interrupts have actually been disabled in hardware. The only
	 * way to be certain would be to poll the IMR-register, which is is
	 * the very register we are trying to emulate. The register read back
	 * is a reasonable heuristic.
	 */
	at91_rtc_read(AT91_RTC_SR);
	at91_rtc_shadow_imr &= ~mask;
	spin_unlock_irqrestore(&at91_rtc_lock, flags);
}

static u32 at91_rtc_read_imr(void)
{
	unsigned long flags;
	u32 mask;

	if (at91_rtc_config->use_shadow_imr) {
		spin_lock_irqsave(&at91_rtc_lock, flags);
		mask = at91_rtc_shadow_imr;
		spin_unlock_irqrestore(&at91_rtc_lock, flags);
	} else {
		mask = at91_rtc_read(AT91_RTC_IMR);
	}

	return mask;
}

/*
 * Decode time/date into rtc_time structure
 */
static void at91_rtc_decodetime(unsigned int timereg, unsigned int calreg,
				struct rtc_time *tm)
{
	unsigned int time, date;

	/* must read twice in case it changes */
	do {
		time = at91_rtc_read(timereg);
		date = at91_rtc_read(calreg);
	} while ((time != at91_rtc_read(timereg)) ||
			(date != at91_rtc_read(calreg)));

	tm->tm_sec  = bcd2bin((time & AT91_RTC_SEC) >> 0);
	tm->tm_min  = bcd2bin((time & AT91_RTC_MIN) >> 8);
	tm->tm_hour = bcd2bin((time & AT91_RTC_HOUR) >> 16);

	/*
	 * The Calendar Alarm register does not have a field for
	 * the year - so these will return an invalid value.
	 */
	tm->tm_year  = bcd2bin(date & AT91_RTC_CENT) * 100;	/* century */
	tm->tm_year += bcd2bin((date & AT91_RTC_YEAR) >> 8);	/* year */

	tm->tm_wday = bcd2bin((date & AT91_RTC_DAY) >> 21) - 1;	/* day of the week [0-6], Sunday=0 */
	tm->tm_mon  = bcd2bin((date & AT91_RTC_MONTH) >> 16) - 1;
	tm->tm_mday = bcd2bin((date & AT91_RTC_DATE) >> 24);
}

/*
 * Read current time and date in RTC
 */
static int at91_rtc_readtime(struct device *dev, struct rtc_time *tm)
{
	at91_rtc_decodetime(AT91_RTC_TIMR, AT91_RTC_CALR, tm);
	tm->tm_yday = rtc_year_days(tm->tm_mday, tm->tm_mon, tm->tm_year);
	tm->tm_year = tm->tm_year - 1900;

	dev_dbg(dev, "%s(): %4d-%02d-%02d %02d:%02d:%02d\n", __func__,
		1900 + tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	return 0;
}

/*
 * Set current time and date in RTC
 */
static int at91_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	unsigned long cr;

	dev_dbg(dev, "%s(): %4d-%02d-%02d %02d:%02d:%02d\n", __func__,
		1900 + tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	wait_for_completion(&at91_rtc_upd_rdy);

	/* Stop Time/Calendar from counting */
	cr = at91_rtc_read(AT91_RTC_CR);
	at91_rtc_write(AT91_RTC_CR, cr | AT91_RTC_UPDCAL | AT91_RTC_UPDTIM);

	at91_rtc_write_ier(AT91_RTC_ACKUPD);
	wait_for_completion(&at91_rtc_updated);	/* wait for ACKUPD interrupt */
	at91_rtc_write_idr(AT91_RTC_ACKUPD);

	at91_rtc_write(AT91_RTC_TIMR,
			  bin2bcd(tm->tm_sec) << 0
			| bin2bcd(tm->tm_min) << 8
			| bin2bcd(tm->tm_hour) << 16);

	at91_rtc_write(AT91_RTC_CALR,
			  bin2bcd((tm->tm_year + 1900) / 100)	/* century */
			| bin2bcd(tm->tm_year % 100) << 8	/* year */
			| bin2bcd(tm->tm_mon + 1) << 16		/* tm_mon starts at zero */
			| bin2bcd(tm->tm_wday + 1) << 21	/* day of the week [0-6], Sunday=0 */
			| bin2bcd(tm->tm_mday) << 24);

	/* Restart Time/Calendar */
	cr = at91_rtc_read(AT91_RTC_CR);
	at91_rtc_write(AT91_RTC_SCCR, AT91_RTC_SECEV);
	at91_rtc_write(AT91_RTC_CR, cr & ~(AT91_RTC_UPDCAL | AT91_RTC_UPDTIM));
	at91_rtc_write_ier(AT91_RTC_SECEV);

	return 0;
}

/*
 * Read alarm time and date in RTC
 */
static int at91_rtc_readalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_time *tm = &alrm->time;

	at91_rtc_decodetime(AT91_RTC_TIMALR, AT91_RTC_CALALR, tm);
	tm->tm_year = -1;

	alrm->enabled = (at91_rtc_read_imr() & AT91_RTC_ALARM)
			? 1 : 0;

	dev_dbg(dev, "%s(): %02d-%02d %02d:%02d:%02d %sabled\n", __func__,
		tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
		alrm->enabled ? "en" : "dis");

	return 0;
}

/*
 * Set alarm time and date in RTC
 */
static int at91_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_time tm;

	at91_rtc_decodetime(AT91_RTC_TIMR, AT91_RTC_CALR, &tm);

	tm.tm_mon = alrm->time.tm_mon;
	tm.tm_mday = alrm->time.tm_mday;
	tm.tm_hour = alrm->time.tm_hour;
	tm.tm_min = alrm->time.tm_min;
	tm.tm_sec = alrm->time.tm_sec;

	at91_rtc_write_idr(AT91_RTC_ALARM);
	at91_rtc_write(AT91_RTC_TIMALR,
		  bin2bcd(tm.tm_sec) << 0
		| bin2bcd(tm.tm_min) << 8
		| bin2bcd(tm.tm_hour) << 16
		| AT91_RTC_HOUREN | AT91_RTC_MINEN | AT91_RTC_SECEN);
	at91_rtc_write(AT91_RTC_CALALR,
		  bin2bcd(tm.tm_mon + 1) << 16		/* tm_mon starts at zero */
		| bin2bcd(tm.tm_mday) << 24
		| AT91_RTC_DATEEN | AT91_RTC_MTHEN);

	if (alrm->enabled) {
		at91_rtc_write(AT91_RTC_SCCR, AT91_RTC_ALARM);
		at91_rtc_write_ier(AT91_RTC_ALARM);
	}

	dev_dbg(dev, "%s(): %4d-%02d-%02d %02d:%02d:%02d\n", __func__,
		tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour,
		tm.tm_min, tm.tm_sec);

	return 0;
}

static int at91_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	dev_dbg(dev, "%s(): cmd=%08x\n", __func__, enabled);

	if (enabled) {
		at91_rtc_write(AT91_RTC_SCCR, AT91_RTC_ALARM);
		at91_rtc_write_ier(AT91_RTC_ALARM);
	} else
		at91_rtc_write_idr(AT91_RTC_ALARM);

	return 0;
}
/*
 * Provide additional RTC information in /proc/driver/rtc
 */
static int at91_rtc_proc(struct device *dev, struct seq_file *seq)
{
	unsigned long imr = at91_rtc_read_imr();

	seq_printf(seq, "update_IRQ\t: %s\n",
			(imr & AT91_RTC_ACKUPD) ? "yes" : "no");
	seq_printf(seq, "periodic_IRQ\t: %s\n",
			(imr & AT91_RTC_SECEV) ? "yes" : "no");

	return 0;
}

/*
 * IRQ handler for the RTC
 */
static irqreturn_t at91_rtc_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct rtc_device *rtc = platform_get_drvdata(pdev);
	unsigned int rtsr;
	unsigned long events = 0;
	int ret = IRQ_NONE;

	spin_lock(&suspended_lock);
	rtsr = at91_rtc_read(AT91_RTC_SR) & at91_rtc_read_imr();
	if (rtsr) {		/* this interrupt is shared!  Is it ours? */
		if (rtsr & AT91_RTC_ALARM)
			events |= (RTC_AF | RTC_IRQF);
		if (rtsr & AT91_RTC_SECEV) {
			complete(&at91_rtc_upd_rdy);
			at91_rtc_write_idr(AT91_RTC_SECEV);
		}
		if (rtsr & AT91_RTC_ACKUPD)
			complete(&at91_rtc_updated);

		at91_rtc_write(AT91_RTC_SCCR, rtsr);	/* clear status reg */

		if (!suspended) {
			rtc_update_irq(rtc, 1, events);

			dev_dbg(&pdev->dev, "%s(): num=%ld, events=0x%02lx\n",
				__func__, events >> 8, events & 0x000000FF);
		} else {
			cached_events |= events;
			at91_rtc_write_idr(at91_rtc_imr);
			pm_system_wakeup();
		}

		ret = IRQ_HANDLED;
	}
	spin_unlock(&suspended_lock);

	return ret;
}

static const struct at91_rtc_config at91rm9200_config = {
};

static const struct at91_rtc_config at91sam9x5_config = {
	.use_shadow_imr	= true,
};

#ifdef CONFIG_OF
static const struct of_device_id at91_rtc_dt_ids[] = {
	{
		.compatible = "atmel,at91rm9200-rtc",
		.data = &at91rm9200_config,
	}, {
		.compatible = "atmel,at91sam9x5-rtc",
		.data = &at91sam9x5_config,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, at91_rtc_dt_ids);
#endif

static const struct at91_rtc_config *
at91_rtc_get_config(struct platform_device *pdev)
{
	const struct of_device_id *match;

	if (pdev->dev.of_node) {
		match = of_match_node(at91_rtc_dt_ids, pdev->dev.of_node);
		if (!match)
			return NULL;
		return (const struct at91_rtc_config *)match->data;
	}

	return &at91rm9200_config;
}

static const struct rtc_class_ops at91_rtc_ops = {
	.read_time	= at91_rtc_readtime,
	.set_time	= at91_rtc_settime,
	.read_alarm	= at91_rtc_readalarm,
	.set_alarm	= at91_rtc_setalarm,
	.proc		= at91_rtc_proc,
	.alarm_irq_enable = at91_rtc_alarm_irq_enable,
};

/*
 * Initialize and install RTC driver
 */
static int __init at91_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;
	struct resource *regs;
	int ret = 0;

	at91_rtc_config = at91_rtc_get_config(pdev);
	if (!at91_rtc_config)
		return -ENODEV;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "no mmio resource defined\n");
		return -ENXIO;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource defined\n");
		return -ENXIO;
	}

	at91_rtc_regs = devm_ioremap(&pdev->dev, regs->start,
				     resource_size(regs));
	if (!at91_rtc_regs) {
		dev_err(&pdev->dev, "failed to map registers, aborting.\n");
		return -ENOMEM;
	}

	rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);
	platform_set_drvdata(pdev, rtc);

	sclk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sclk))
		return PTR_ERR(sclk);

	ret = clk_prepare_enable(sclk);
	if (ret) {
		dev_err(&pdev->dev, "Could not enable slow clock\n");
		return ret;
	}

	at91_rtc_write(AT91_RTC_CR, 0);
	at91_rtc_write(AT91_RTC_MR, 0);		/* 24 hour mode */

	/* Disable all interrupts */
	at91_rtc_write_idr(AT91_RTC_ACKUPD | AT91_RTC_ALARM |
					AT91_RTC_SECEV | AT91_RTC_TIMEV |
					AT91_RTC_CALEV);

	ret = devm_request_irq(&pdev->dev, irq, at91_rtc_interrupt,
			       IRQF_SHARED | IRQF_COND_SUSPEND,
			       "at91_rtc", pdev);
	if (ret) {
		dev_err(&pdev->dev, "IRQ %d already in use.\n", irq);
		goto err_clk;
	}

	/* cpu init code should really have flagged this device as
	 * being wake-capable; if it didn't, do that here.
	 */
	if (!device_can_wakeup(&pdev->dev))
		device_init_wakeup(&pdev->dev, 1);

	rtc->ops = &at91_rtc_ops;
	rtc->range_min = RTC_TIMESTAMP_BEGIN_1900;
	rtc->range_max = RTC_TIMESTAMP_END_2099;
	ret = rtc_register_device(rtc);
	if (ret)
		goto err_clk;

	/* enable SECEV interrupt in order to initialize at91_rtc_upd_rdy
	 * completion.
	 */
	at91_rtc_write_ier(AT91_RTC_SECEV);

	dev_info(&pdev->dev, "AT91 Real Time Clock driver.\n");
	return 0;

err_clk:
	clk_disable_unprepare(sclk);

	return ret;
}

/*
 * Disable and remove the RTC driver
 */
static int __exit at91_rtc_remove(struct platform_device *pdev)
{
	/* Disable all interrupts */
	at91_rtc_write_idr(AT91_RTC_ACKUPD | AT91_RTC_ALARM |
					AT91_RTC_SECEV | AT91_RTC_TIMEV |
					AT91_RTC_CALEV);

	clk_disable_unprepare(sclk);

	return 0;
}

static void at91_rtc_shutdown(struct platform_device *pdev)
{
	/* Disable all interrupts */
	at91_rtc_write(AT91_RTC_IDR, AT91_RTC_ACKUPD | AT91_RTC_ALARM |
					AT91_RTC_SECEV | AT91_RTC_TIMEV |
					AT91_RTC_CALEV);
}

#ifdef CONFIG_PM_SLEEP

/* AT91RM9200 RTC Power management control */

static int at91_rtc_suspend(struct device *dev)
{
	/* this IRQ is shared with DBGU and other hardware which isn't
	 * necessarily doing PM like we are...
	 */
	at91_rtc_write(AT91_RTC_SCCR, AT91_RTC_ALARM);

	at91_rtc_imr = at91_rtc_read_imr()
			& (AT91_RTC_ALARM|AT91_RTC_SECEV);
	if (at91_rtc_imr) {
		if (device_may_wakeup(dev)) {
			unsigned long flags;

			enable_irq_wake(irq);

			spin_lock_irqsave(&suspended_lock, flags);
			suspended = true;
			spin_unlock_irqrestore(&suspended_lock, flags);
		} else {
			at91_rtc_write_idr(at91_rtc_imr);
		}
	}
	return 0;
}

static int at91_rtc_resume(struct device *dev)
{
	struct rtc_device *rtc = dev_get_drvdata(dev);

	if (at91_rtc_imr) {
		if (device_may_wakeup(dev)) {
			unsigned long flags;

			spin_lock_irqsave(&suspended_lock, flags);

			if (cached_events) {
				rtc_update_irq(rtc, 1, cached_events);
				cached_events = 0;
			}

			suspended = false;
			spin_unlock_irqrestore(&suspended_lock, flags);

			disable_irq_wake(irq);
		}
		at91_rtc_write_ier(at91_rtc_imr);
	}
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(at91_rtc_pm_ops, at91_rtc_suspend, at91_rtc_resume);

static struct platform_driver at91_rtc_driver = {
	.remove		= __exit_p(at91_rtc_remove),
	.shutdown	= at91_rtc_shutdown,
	.driver		= {
		.name	= "at91_rtc",
		.pm	= &at91_rtc_pm_ops,
		.of_match_table = of_match_ptr(at91_rtc_dt_ids),
	},
};

module_platform_driver_probe(at91_rtc_driver, at91_rtc_probe);

MODULE_AUTHOR("Rick Bronson");
MODULE_DESCRIPTION("RTC driver for Atmel AT91RM9200");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:at91_rtc");
