/*
 * "RTT as Real Time Clock" driver for AT91SAM9 SoC family
 *
 * (C) 2007 Michel Benoit
 *
 * Based on rtc-at91rm9200.c by Rick Bronson
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/slab.h>

#include <mach/board.h>
#include <mach/at91_rtt.h>
#include <mach/cpu.h>


/*
 * This driver uses two configurable hardware resources that live in the
 * AT91SAM9 backup power domain (intended to be powered at all times)
 * to implement the Real Time Clock interfaces
 *
 *  - A "Real-time Timer" (RTT) counts up in seconds from a base time.
 *    We can't assign the counter value (CRTV) ... but we can reset it.
 *
 *  - One of the "General Purpose Backup Registers" (GPBRs) holds the
 *    base time, normally an offset from the beginning of the POSIX
 *    epoch (1970-Jan-1 00:00:00 UTC).  Some systems also include the
 *    local timezone's offset.
 *
 * The RTC's value is the RTT counter plus that offset.  The RTC's alarm
 * is likewise a base (ALMV) plus that offset.
 *
 * Not all RTTs will be used as RTCs; some systems have multiple RTTs to
 * choose from, or a "real" RTC module.  All systems have multiple GPBR
 * registers available, likewise usable for more than "RTC" support.
 */

/*
 * We store ALARM_DISABLED in ALMV to record that no alarm is set.
 * It's also the reset value for that field.
 */
#define ALARM_DISABLED	((u32)~0)


struct sam9_rtc {
	void __iomem		*rtt;
	struct rtc_device	*rtcdev;
	u32			imr;
};

#define rtt_readl(rtc, field) \
	__raw_readl((rtc)->rtt + AT91_RTT_ ## field)
#define rtt_writel(rtc, field, val) \
	__raw_writel((val), (rtc)->rtt + AT91_RTT_ ## field)

#define gpbr_readl(rtc) \
	at91_sys_read(AT91_GPBR + 4 * CONFIG_RTC_DRV_AT91SAM9_GPBR)
#define gpbr_writel(rtc, val) \
	at91_sys_write(AT91_GPBR + 4 * CONFIG_RTC_DRV_AT91SAM9_GPBR, (val))

/*
 * Read current time and date in RTC
 */
static int at91_rtc_readtime(struct device *dev, struct rtc_time *tm)
{
	struct sam9_rtc *rtc = dev_get_drvdata(dev);
	u32 secs, secs2;
	u32 offset;

	/* read current time offset */
	offset = gpbr_readl(rtc);
	if (offset == 0)
		return -EILSEQ;

	/* reread the counter to help sync the two clock domains */
	secs = rtt_readl(rtc, VR);
	secs2 = rtt_readl(rtc, VR);
	if (secs != secs2)
		secs = rtt_readl(rtc, VR);

	rtc_time_to_tm(offset + secs, tm);

	dev_dbg(dev, "%s: %4d-%02d-%02d %02d:%02d:%02d\n", "readtime",
		1900 + tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	return 0;
}

/*
 * Set current time and date in RTC
 */
static int at91_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	struct sam9_rtc *rtc = dev_get_drvdata(dev);
	int err;
	u32 offset, alarm, mr;
	unsigned long secs;

	dev_dbg(dev, "%s: %4d-%02d-%02d %02d:%02d:%02d\n", "settime",
		1900 + tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	err = rtc_tm_to_time(tm, &secs);
	if (err != 0)
		return err;

	mr = rtt_readl(rtc, MR);

	/* disable interrupts */
	rtt_writel(rtc, MR, mr & ~(AT91_RTT_ALMIEN | AT91_RTT_RTTINCIEN));

	/* read current time offset */
	offset = gpbr_readl(rtc);

	/* store the new base time in a battery backup register */
	secs += 1;
	gpbr_writel(rtc, secs);

	/* adjust the alarm time for the new base */
	alarm = rtt_readl(rtc, AR);
	if (alarm != ALARM_DISABLED) {
		if (offset > secs) {
			/* time jumped backwards, increase time until alarm */
			alarm += (offset - secs);
		} else if ((alarm + offset) > secs) {
			/* time jumped forwards, decrease time until alarm */
			alarm -= (secs - offset);
		} else {
			/* time jumped past the alarm, disable alarm */
			alarm = ALARM_DISABLED;
			mr &= ~AT91_RTT_ALMIEN;
		}
		rtt_writel(rtc, AR, alarm);
	}

	/* reset the timer, and re-enable interrupts */
	rtt_writel(rtc, MR, mr | AT91_RTT_RTTRST);

	return 0;
}

static int at91_rtc_readalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct sam9_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time;
	u32 alarm = rtt_readl(rtc, AR);
	u32 offset;

	offset = gpbr_readl(rtc);
	if (offset == 0)
		return -EILSEQ;

	memset(alrm, 0, sizeof(*alrm));
	if (alarm != ALARM_DISABLED && offset != 0) {
		rtc_time_to_tm(offset + alarm, tm);

		dev_dbg(dev, "%s: %4d-%02d-%02d %02d:%02d:%02d\n", "readalarm",
			1900 + tm->tm_year, tm->tm_mon, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);

		if (rtt_readl(rtc, MR) & AT91_RTT_ALMIEN)
			alrm->enabled = 1;
	}

	return 0;
}

static int at91_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct sam9_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time;
	unsigned long secs;
	u32 offset;
	u32 mr;
	int err;

	err = rtc_tm_to_time(tm, &secs);
	if (err != 0)
		return err;

	offset = gpbr_readl(rtc);
	if (offset == 0) {
		/* time is not set */
		return -EILSEQ;
	}
	mr = rtt_readl(rtc, MR);
	rtt_writel(rtc, MR, mr & ~AT91_RTT_ALMIEN);

	/* alarm in the past? finish and leave disabled */
	if (secs <= offset) {
		rtt_writel(rtc, AR, ALARM_DISABLED);
		return 0;
	}

	/* else set alarm and maybe enable it */
	rtt_writel(rtc, AR, secs - offset);
	if (alrm->enabled)
		rtt_writel(rtc, MR, mr | AT91_RTT_ALMIEN);

	dev_dbg(dev, "%s: %4d-%02d-%02d %02d:%02d:%02d\n", "setalarm",
		tm->tm_year, tm->tm_mon, tm->tm_mday, tm->tm_hour,
		tm->tm_min, tm->tm_sec);

	return 0;
}

/*
 * Handle commands from user-space
 */
static int at91_rtc_ioctl(struct device *dev, unsigned int cmd,
			unsigned long arg)
{
	struct sam9_rtc *rtc = dev_get_drvdata(dev);
	int ret = 0;
	u32 mr = rtt_readl(rtc, MR);

	dev_dbg(dev, "ioctl: cmd=%08x, arg=%08lx, mr %08x\n", cmd, arg, mr);

	switch (cmd) {
	case RTC_UIE_OFF:		/* update off */
		rtt_writel(rtc, MR, mr & ~AT91_RTT_RTTINCIEN);
		break;
	case RTC_UIE_ON:		/* update on */
		rtt_writel(rtc, MR, mr | AT91_RTT_RTTINCIEN);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

static int at91_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct sam9_rtc *rtc = dev_get_drvdata(dev);
	u32 mr = rtt_readl(rtc, MR);

	dev_dbg(dev, "alarm_irq_enable: enabled=%08x, mr %08x\n", enabled, mr);
	if (enabled)
		rtt_writel(rtc, MR, mr | AT91_RTT_ALMIEN);
	else
		rtt_writel(rtc, MR, mr & ~AT91_RTT_ALMIEN);
	return 0;
}

/*
 * Provide additional RTC information in /proc/driver/rtc
 */
static int at91_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct sam9_rtc *rtc = dev_get_drvdata(dev);
	u32 mr = mr = rtt_readl(rtc, MR);

	seq_printf(seq, "update_IRQ\t: %s\n",
			(mr & AT91_RTT_RTTINCIEN) ? "yes" : "no");
	return 0;
}

/*
 * IRQ handler for the RTC
 */
static irqreturn_t at91_rtc_interrupt(int irq, void *_rtc)
{
	struct sam9_rtc *rtc = _rtc;
	u32 sr, mr;
	unsigned long events = 0;

	/* Shared interrupt may be for another device.  Note: reading
	 * SR clears it, so we must only read it in this irq handler!
	 */
	mr = rtt_readl(rtc, MR) & (AT91_RTT_ALMIEN | AT91_RTT_RTTINCIEN);
	sr = rtt_readl(rtc, SR) & (mr >> 16);
	if (!sr)
		return IRQ_NONE;

	/* alarm status */
	if (sr & AT91_RTT_ALMS)
		events |= (RTC_AF | RTC_IRQF);

	/* timer update/increment */
	if (sr & AT91_RTT_RTTINC)
		events |= (RTC_UF | RTC_IRQF);

	rtc_update_irq(rtc->rtcdev, 1, events);

	pr_debug("%s: num=%ld, events=0x%02lx\n", __func__,
		events >> 8, events & 0x000000FF);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops at91_rtc_ops = {
	.ioctl		= at91_rtc_ioctl,
	.read_time	= at91_rtc_readtime,
	.set_time	= at91_rtc_settime,
	.read_alarm	= at91_rtc_readalarm,
	.set_alarm	= at91_rtc_setalarm,
	.proc		= at91_rtc_proc,
	.alarm_irq_enabled = at91_rtc_alarm_irq_enable,
};

/*
 * Initialize and install RTC driver
 */
static int __init at91_rtc_probe(struct platform_device *pdev)
{
	struct resource	*r;
	struct sam9_rtc	*rtc;
	int		ret;
	u32		mr;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r)
		return -ENODEV;

	rtc = kzalloc(sizeof *rtc, GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	/* platform setup code should have handled this; sigh */
	if (!device_can_wakeup(&pdev->dev))
		device_init_wakeup(&pdev->dev, 1);

	platform_set_drvdata(pdev, rtc);
	rtc->rtt = (void __force __iomem *) (AT91_VA_BASE_SYS - AT91_BASE_SYS);
	rtc->rtt += r->start;

	mr = rtt_readl(rtc, MR);

	/* unless RTT is counting at 1 Hz, re-initialize it */
	if ((mr & AT91_RTT_RTPRES) != AT91_SLOW_CLOCK) {
		mr = AT91_RTT_RTTRST | (AT91_SLOW_CLOCK & AT91_RTT_RTPRES);
		gpbr_writel(rtc, 0);
	}

	/* disable all interrupts (same as on shutdown path) */
	mr &= ~(AT91_RTT_ALMIEN | AT91_RTT_RTTINCIEN);
	rtt_writel(rtc, MR, mr);

	rtc->rtcdev = rtc_device_register(pdev->name, &pdev->dev,
				&at91_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc->rtcdev)) {
		ret = PTR_ERR(rtc->rtcdev);
		goto fail;
	}

	/* register irq handler after we know what name we'll use */
	ret = request_irq(AT91_ID_SYS, at91_rtc_interrupt,
				IRQF_DISABLED | IRQF_SHARED,
				dev_name(&rtc->rtcdev->dev), rtc);
	if (ret) {
		dev_dbg(&pdev->dev, "can't share IRQ %d?\n", AT91_ID_SYS);
		rtc_device_unregister(rtc->rtcdev);
		goto fail;
	}

	/* NOTE:  sam9260 rev A silicon has a ROM bug which resets the
	 * RTT on at least some reboots.  If you have that chip, you must
	 * initialize the time from some external source like a GPS, wall
	 * clock, discrete RTC, etc
	 */

	if (gpbr_readl(rtc) == 0)
		dev_warn(&pdev->dev, "%s: SET TIME!\n",
				dev_name(&rtc->rtcdev->dev));

	return 0;

fail:
	platform_set_drvdata(pdev, NULL);
	kfree(rtc);
	return ret;
}

/*
 * Disable and remove the RTC driver
 */
static int __exit at91_rtc_remove(struct platform_device *pdev)
{
	struct sam9_rtc	*rtc = platform_get_drvdata(pdev);
	u32		mr = rtt_readl(rtc, MR);

	/* disable all interrupts */
	rtt_writel(rtc, MR, mr & ~(AT91_RTT_ALMIEN | AT91_RTT_RTTINCIEN));
	free_irq(AT91_ID_SYS, rtc);

	rtc_device_unregister(rtc->rtcdev);

	platform_set_drvdata(pdev, NULL);
	kfree(rtc);
	return 0;
}

static void at91_rtc_shutdown(struct platform_device *pdev)
{
	struct sam9_rtc	*rtc = platform_get_drvdata(pdev);
	u32		mr = rtt_readl(rtc, MR);

	rtc->imr = mr & (AT91_RTT_ALMIEN | AT91_RTT_RTTINCIEN);
	rtt_writel(rtc, MR, mr & ~rtc->imr);
}

#ifdef CONFIG_PM

/* AT91SAM9 RTC Power management control */

static int at91_rtc_suspend(struct platform_device *pdev,
					pm_message_t state)
{
	struct sam9_rtc	*rtc = platform_get_drvdata(pdev);
	u32		mr = rtt_readl(rtc, MR);

	/*
	 * This IRQ is shared with DBGU and other hardware which isn't
	 * necessarily a wakeup event source.
	 */
	rtc->imr = mr & (AT91_RTT_ALMIEN | AT91_RTT_RTTINCIEN);
	if (rtc->imr) {
		if (device_may_wakeup(&pdev->dev) && (mr & AT91_RTT_ALMIEN)) {
			enable_irq_wake(AT91_ID_SYS);
			/* don't let RTTINC cause wakeups */
			if (mr & AT91_RTT_RTTINCIEN)
				rtt_writel(rtc, MR, mr & ~AT91_RTT_RTTINCIEN);
		} else
			rtt_writel(rtc, MR, mr & ~rtc->imr);
	}

	return 0;
}

static int at91_rtc_resume(struct platform_device *pdev)
{
	struct sam9_rtc	*rtc = platform_get_drvdata(pdev);
	u32		mr;

	if (rtc->imr) {
		if (device_may_wakeup(&pdev->dev))
			disable_irq_wake(AT91_ID_SYS);
		mr = rtt_readl(rtc, MR);
		rtt_writel(rtc, MR, mr | rtc->imr);
	}

	return 0;
}
#else
#define at91_rtc_suspend	NULL
#define at91_rtc_resume		NULL
#endif

static struct platform_driver at91_rtc_driver = {
	.driver.name	= "rtc-at91sam9",
	.driver.owner	= THIS_MODULE,
	.remove		= __exit_p(at91_rtc_remove),
	.shutdown	= at91_rtc_shutdown,
	.suspend	= at91_rtc_suspend,
	.resume		= at91_rtc_resume,
};

/* Chips can have more than one RTT module, and they can be used for more
 * than just RTCs.  So we can't just register as "the" RTT driver.
 *
 * A normal approach in such cases is to create a library to allocate and
 * free the modules.  Here we just use bus_find_device() as like such a
 * library, binding directly ... no runtime "library" footprint is needed.
 */
static int __init at91_rtc_match(struct device *dev, void *v)
{
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	/* continue searching if this isn't the RTT we need */
	if (strcmp("at91_rtt", pdev->name) != 0
			|| pdev->id != CONFIG_RTC_DRV_AT91SAM9_RTT)
		goto fail;

	/* else we found it ... but fail unless we can bind to the RTC driver */
	if (dev->driver) {
		dev_dbg(dev, "busy, can't use as RTC!\n");
		goto fail;
	}
	dev->driver = &at91_rtc_driver.driver;
	if (device_attach(dev) == 0) {
		dev_dbg(dev, "can't attach RTC!\n");
		goto fail;
	}
	ret = at91_rtc_probe(pdev);
	if (ret == 0)
		return true;

	dev_dbg(dev, "RTC probe err %d!\n", ret);
fail:
	return false;
}

static int __init at91_rtc_init(void)
{
	int status;
	struct device *rtc;

	status = platform_driver_register(&at91_rtc_driver);
	if (status)
		return status;
	rtc = bus_find_device(&platform_bus_type, NULL,
			NULL, at91_rtc_match);
	if (!rtc)
		platform_driver_unregister(&at91_rtc_driver);
	return rtc ? 0 : -ENODEV;
}
module_init(at91_rtc_init);

static void __exit at91_rtc_exit(void)
{
	platform_driver_unregister(&at91_rtc_driver);
}
module_exit(at91_rtc_exit);


MODULE_AUTHOR("Michel Benoit");
MODULE_DESCRIPTION("RTC driver for Atmel AT91SAM9x");
MODULE_LICENSE("GPL");
