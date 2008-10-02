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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/completion.h>

#include <asm/uaccess.h>
#include <mach/at91_rtc.h>


#define AT91_RTC_FREQ		1
#define AT91_RTC_EPOCH		1900UL	/* just like arch/arm/common/rtctime.c */

static DECLARE_COMPLETION(at91_rtc_updated);
static unsigned int at91_alarm_year = AT91_RTC_EPOCH;

/*
 * Decode time/date into rtc_time structure
 */
static void at91_rtc_decodetime(unsigned int timereg, unsigned int calreg,
				struct rtc_time *tm)
{
	unsigned int time, date;

	/* must read twice in case it changes */
	do {
		time = at91_sys_read(timereg);
		date = at91_sys_read(calreg);
	} while ((time != at91_sys_read(timereg)) ||
			(date != at91_sys_read(calreg)));

	tm->tm_sec  = BCD2BIN((time & AT91_RTC_SEC) >> 0);
	tm->tm_min  = BCD2BIN((time & AT91_RTC_MIN) >> 8);
	tm->tm_hour = BCD2BIN((time & AT91_RTC_HOUR) >> 16);

	/*
	 * The Calendar Alarm register does not have a field for
	 * the year - so these will return an invalid value.  When an
	 * alarm is set, at91_alarm_year wille store the current year.
	 */
	tm->tm_year  = BCD2BIN(date & AT91_RTC_CENT) * 100;	/* century */
	tm->tm_year += BCD2BIN((date & AT91_RTC_YEAR) >> 8);	/* year */

	tm->tm_wday = BCD2BIN((date & AT91_RTC_DAY) >> 21) - 1;	/* day of the week [0-6], Sunday=0 */
	tm->tm_mon  = BCD2BIN((date & AT91_RTC_MONTH) >> 16) - 1;
	tm->tm_mday = BCD2BIN((date & AT91_RTC_DATE) >> 24);
}

/*
 * Read current time and date in RTC
 */
static int at91_rtc_readtime(struct device *dev, struct rtc_time *tm)
{
	at91_rtc_decodetime(AT91_RTC_TIMR, AT91_RTC_CALR, tm);
	tm->tm_yday = rtc_year_days(tm->tm_mday, tm->tm_mon, tm->tm_year);
	tm->tm_year = tm->tm_year - 1900;

	pr_debug("%s(): %4d-%02d-%02d %02d:%02d:%02d\n", __func__,
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

	pr_debug("%s(): %4d-%02d-%02d %02d:%02d:%02d\n", __func__,
		1900 + tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	/* Stop Time/Calendar from counting */
	cr = at91_sys_read(AT91_RTC_CR);
	at91_sys_write(AT91_RTC_CR, cr | AT91_RTC_UPDCAL | AT91_RTC_UPDTIM);

	at91_sys_write(AT91_RTC_IER, AT91_RTC_ACKUPD);
	wait_for_completion(&at91_rtc_updated);	/* wait for ACKUPD interrupt */
	at91_sys_write(AT91_RTC_IDR, AT91_RTC_ACKUPD);

	at91_sys_write(AT91_RTC_TIMR,
			  BIN2BCD(tm->tm_sec) << 0
			| BIN2BCD(tm->tm_min) << 8
			| BIN2BCD(tm->tm_hour) << 16);

	at91_sys_write(AT91_RTC_CALR,
			  BIN2BCD((tm->tm_year + 1900) / 100)	/* century */
			| BIN2BCD(tm->tm_year % 100) << 8	/* year */
			| BIN2BCD(tm->tm_mon + 1) << 16		/* tm_mon starts at zero */
			| BIN2BCD(tm->tm_wday + 1) << 21	/* day of the week [0-6], Sunday=0 */
			| BIN2BCD(tm->tm_mday) << 24);

	/* Restart Time/Calendar */
	cr = at91_sys_read(AT91_RTC_CR);
	at91_sys_write(AT91_RTC_CR, cr & ~(AT91_RTC_UPDCAL | AT91_RTC_UPDTIM));

	return 0;
}

/*
 * Read alarm time and date in RTC
 */
static int at91_rtc_readalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_time *tm = &alrm->time;

	at91_rtc_decodetime(AT91_RTC_TIMALR, AT91_RTC_CALALR, tm);
	tm->tm_yday = rtc_year_days(tm->tm_mday, tm->tm_mon, tm->tm_year);
	tm->tm_year = at91_alarm_year - 1900;

	alrm->enabled = (at91_sys_read(AT91_RTC_IMR) & AT91_RTC_ALARM)
			? 1 : 0;

	pr_debug("%s(): %4d-%02d-%02d %02d:%02d:%02d\n", __func__,
		1900 + tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	return 0;
}

/*
 * Set alarm time and date in RTC
 */
static int at91_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_time tm;

	at91_rtc_decodetime(AT91_RTC_TIMR, AT91_RTC_CALR, &tm);

	at91_alarm_year = tm.tm_year;

	tm.tm_hour = alrm->time.tm_hour;
	tm.tm_min = alrm->time.tm_min;
	tm.tm_sec = alrm->time.tm_sec;

	at91_sys_write(AT91_RTC_IDR, AT91_RTC_ALARM);
	at91_sys_write(AT91_RTC_TIMALR,
		  BIN2BCD(tm.tm_sec) << 0
		| BIN2BCD(tm.tm_min) << 8
		| BIN2BCD(tm.tm_hour) << 16
		| AT91_RTC_HOUREN | AT91_RTC_MINEN | AT91_RTC_SECEN);
	at91_sys_write(AT91_RTC_CALALR,
		  BIN2BCD(tm.tm_mon + 1) << 16		/* tm_mon starts at zero */
		| BIN2BCD(tm.tm_mday) << 24
		| AT91_RTC_DATEEN | AT91_RTC_MTHEN);

	if (alrm->enabled) {
		at91_sys_write(AT91_RTC_SCCR, AT91_RTC_ALARM);
		at91_sys_write(AT91_RTC_IER, AT91_RTC_ALARM);
	}

	pr_debug("%s(): %4d-%02d-%02d %02d:%02d:%02d\n", __func__,
		at91_alarm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour,
		tm.tm_min, tm.tm_sec);

	return 0;
}

/*
 * Handle commands from user-space
 */
static int at91_rtc_ioctl(struct device *dev, unsigned int cmd,
			unsigned long arg)
{
	int ret = 0;

	pr_debug("%s(): cmd=%08x, arg=%08lx.\n", __func__, cmd, arg);

	/* important:  scrub old status before enabling IRQs */
	switch (cmd) {
	case RTC_AIE_OFF:	/* alarm off */
		at91_sys_write(AT91_RTC_IDR, AT91_RTC_ALARM);
		break;
	case RTC_AIE_ON:	/* alarm on */
		at91_sys_write(AT91_RTC_SCCR, AT91_RTC_ALARM);
		at91_sys_write(AT91_RTC_IER, AT91_RTC_ALARM);
		break;
	case RTC_UIE_OFF:	/* update off */
		at91_sys_write(AT91_RTC_IDR, AT91_RTC_SECEV);
		break;
	case RTC_UIE_ON:	/* update on */
		at91_sys_write(AT91_RTC_SCCR, AT91_RTC_SECEV);
		at91_sys_write(AT91_RTC_IER, AT91_RTC_SECEV);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

/*
 * Provide additional RTC information in /proc/driver/rtc
 */
static int at91_rtc_proc(struct device *dev, struct seq_file *seq)
{
	unsigned long imr = at91_sys_read(AT91_RTC_IMR);

	seq_printf(seq, "update_IRQ\t: %s\n",
			(imr & AT91_RTC_ACKUPD) ? "yes" : "no");
	seq_printf(seq, "periodic_IRQ\t: %s\n",
			(imr & AT91_RTC_SECEV) ? "yes" : "no");
	seq_printf(seq, "periodic_freq\t: %ld\n",
			(unsigned long) AT91_RTC_FREQ);

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

	rtsr = at91_sys_read(AT91_RTC_SR) & at91_sys_read(AT91_RTC_IMR);
	if (rtsr) {		/* this interrupt is shared!  Is it ours? */
		if (rtsr & AT91_RTC_ALARM)
			events |= (RTC_AF | RTC_IRQF);
		if (rtsr & AT91_RTC_SECEV)
			events |= (RTC_UF | RTC_IRQF);
		if (rtsr & AT91_RTC_ACKUPD)
			complete(&at91_rtc_updated);

		at91_sys_write(AT91_RTC_SCCR, rtsr);	/* clear status reg */

		rtc_update_irq(rtc, 1, events);

		pr_debug("%s(): num=%ld, events=0x%02lx\n", __func__,
			events >> 8, events & 0x000000FF);

		return IRQ_HANDLED;
	}
	return IRQ_NONE;		/* not handled */
}

static const struct rtc_class_ops at91_rtc_ops = {
	.ioctl		= at91_rtc_ioctl,
	.read_time	= at91_rtc_readtime,
	.set_time	= at91_rtc_settime,
	.read_alarm	= at91_rtc_readalarm,
	.set_alarm	= at91_rtc_setalarm,
	.proc		= at91_rtc_proc,
};

/*
 * Initialize and install RTC driver
 */
static int __init at91_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;
	int ret;

	at91_sys_write(AT91_RTC_CR, 0);
	at91_sys_write(AT91_RTC_MR, 0);		/* 24 hour mode */

	/* Disable all interrupts */
	at91_sys_write(AT91_RTC_IDR, AT91_RTC_ACKUPD | AT91_RTC_ALARM |
					AT91_RTC_SECEV | AT91_RTC_TIMEV |
					AT91_RTC_CALEV);

	ret = request_irq(AT91_ID_SYS, at91_rtc_interrupt,
				IRQF_DISABLED | IRQF_SHARED,
				"at91_rtc", pdev);
	if (ret) {
		printk(KERN_ERR "at91_rtc: IRQ %d already in use.\n",
				AT91_ID_SYS);
		return ret;
	}

	/* cpu init code should really have flagged this device as
	 * being wake-capable; if it didn't, do that here.
	 */
	if (!device_can_wakeup(&pdev->dev))
		device_init_wakeup(&pdev->dev, 1);

	rtc = rtc_device_register(pdev->name, &pdev->dev,
				&at91_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		free_irq(AT91_ID_SYS, pdev);
		return PTR_ERR(rtc);
	}
	platform_set_drvdata(pdev, rtc);

	printk(KERN_INFO "AT91 Real Time Clock driver.\n");
	return 0;
}

/*
 * Disable and remove the RTC driver
 */
static int __exit at91_rtc_remove(struct platform_device *pdev)
{
	struct rtc_device *rtc = platform_get_drvdata(pdev);

	/* Disable all interrupts */
	at91_sys_write(AT91_RTC_IDR, AT91_RTC_ACKUPD | AT91_RTC_ALARM |
					AT91_RTC_SECEV | AT91_RTC_TIMEV |
					AT91_RTC_CALEV);
	free_irq(AT91_ID_SYS, pdev);

	rtc_device_unregister(rtc);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM

/* AT91RM9200 RTC Power management control */

static u32 at91_rtc_imr;

static int at91_rtc_suspend(struct platform_device *pdev, pm_message_t state)
{
	/* this IRQ is shared with DBGU and other hardware which isn't
	 * necessarily doing PM like we are...
	 */
	at91_rtc_imr = at91_sys_read(AT91_RTC_IMR)
			& (AT91_RTC_ALARM|AT91_RTC_SECEV);
	if (at91_rtc_imr) {
		if (device_may_wakeup(&pdev->dev))
			enable_irq_wake(AT91_ID_SYS);
		else
			at91_sys_write(AT91_RTC_IDR, at91_rtc_imr);
	}
	return 0;
}

static int at91_rtc_resume(struct platform_device *pdev)
{
	if (at91_rtc_imr) {
		if (device_may_wakeup(&pdev->dev))
			disable_irq_wake(AT91_ID_SYS);
		else
			at91_sys_write(AT91_RTC_IER, at91_rtc_imr);
	}
	return 0;
}
#else
#define at91_rtc_suspend NULL
#define at91_rtc_resume  NULL
#endif

static struct platform_driver at91_rtc_driver = {
	.remove		= __exit_p(at91_rtc_remove),
	.suspend	= at91_rtc_suspend,
	.resume		= at91_rtc_resume,
	.driver		= {
		.name	= "at91_rtc",
		.owner	= THIS_MODULE,
	},
};

static int __init at91_rtc_init(void)
{
	return platform_driver_probe(&at91_rtc_driver, at91_rtc_probe);
}

static void __exit at91_rtc_exit(void)
{
	platform_driver_unregister(&at91_rtc_driver);
}

module_init(at91_rtc_init);
module_exit(at91_rtc_exit);

MODULE_AUTHOR("Rick Bronson");
MODULE_DESCRIPTION("RTC driver for Atmel AT91RM9200");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:at91_rtc");
