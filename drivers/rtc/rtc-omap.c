/*
 * TI OMAP1 Real Time Clock interface for Linux
 *
 * Copyright (C) 2003 MontaVista Software, Inc.
 * Author: George G. Davis <gdavis@mvista.com> or <source@mvista.com>
 *
 * Copyright (C) 2006 David Brownell (new RTC framework)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/mach/time.h>


/* The OMAP1 RTC is a year/month/day/hours/minutes/seconds BCD clock
 * with century-range alarm matching, driven by the 32kHz clock.
 *
 * The main user-visible ways it differs from PC RTCs are by omitting
 * "don't care" alarm fields and sub-second periodic IRQs, and having
 * an autoadjust mechanism to calibrate to the true oscillator rate.
 *
 * Board-specific wiring options include using split power mode with
 * RTC_OFF_NOFF used as the reset signal (so the RTC won't be reset),
 * and wiring RTC_WAKE_INT (so the RTC alarm can wake the system from
 * low power modes).  See the BOARD-SPECIFIC CUSTOMIZATION comment.
 */

#define OMAP_RTC_BASE			0xfffb4800

/* RTC registers */
#define OMAP_RTC_SECONDS_REG		0x00
#define OMAP_RTC_MINUTES_REG		0x04
#define OMAP_RTC_HOURS_REG		0x08
#define OMAP_RTC_DAYS_REG		0x0C
#define OMAP_RTC_MONTHS_REG		0x10
#define OMAP_RTC_YEARS_REG		0x14
#define OMAP_RTC_WEEKS_REG		0x18

#define OMAP_RTC_ALARM_SECONDS_REG	0x20
#define OMAP_RTC_ALARM_MINUTES_REG	0x24
#define OMAP_RTC_ALARM_HOURS_REG	0x28
#define OMAP_RTC_ALARM_DAYS_REG		0x2c
#define OMAP_RTC_ALARM_MONTHS_REG	0x30
#define OMAP_RTC_ALARM_YEARS_REG	0x34

#define OMAP_RTC_CTRL_REG		0x40
#define OMAP_RTC_STATUS_REG		0x44
#define OMAP_RTC_INTERRUPTS_REG		0x48

#define OMAP_RTC_COMP_LSB_REG		0x4c
#define OMAP_RTC_COMP_MSB_REG		0x50
#define OMAP_RTC_OSC_REG		0x54

/* OMAP_RTC_CTRL_REG bit fields: */
#define OMAP_RTC_CTRL_SPLIT		(1<<7)
#define OMAP_RTC_CTRL_DISABLE		(1<<6)
#define OMAP_RTC_CTRL_SET_32_COUNTER	(1<<5)
#define OMAP_RTC_CTRL_TEST		(1<<4)
#define OMAP_RTC_CTRL_MODE_12_24	(1<<3)
#define OMAP_RTC_CTRL_AUTO_COMP		(1<<2)
#define OMAP_RTC_CTRL_ROUND_30S		(1<<1)
#define OMAP_RTC_CTRL_STOP		(1<<0)

/* OMAP_RTC_STATUS_REG bit fields: */
#define OMAP_RTC_STATUS_POWER_UP        (1<<7)
#define OMAP_RTC_STATUS_ALARM           (1<<6)
#define OMAP_RTC_STATUS_1D_EVENT        (1<<5)
#define OMAP_RTC_STATUS_1H_EVENT        (1<<4)
#define OMAP_RTC_STATUS_1M_EVENT        (1<<3)
#define OMAP_RTC_STATUS_1S_EVENT        (1<<2)
#define OMAP_RTC_STATUS_RUN             (1<<1)
#define OMAP_RTC_STATUS_BUSY            (1<<0)

/* OMAP_RTC_INTERRUPTS_REG bit fields: */
#define OMAP_RTC_INTERRUPTS_IT_ALARM    (1<<3)
#define OMAP_RTC_INTERRUPTS_IT_TIMER    (1<<2)


#define rtc_read(addr)		omap_readb(OMAP_RTC_BASE + (addr))
#define rtc_write(val, addr)	omap_writeb(val, OMAP_RTC_BASE + (addr))


/* platform_bus isn't hotpluggable, so for static linkage it'd be safe
 * to get rid of probe() and remove() code ... too bad the driver struct
 * remembers probe(), that's about 25% of the runtime footprint!!
 */
#ifndef	MODULE
#undef	__devexit
#undef	__devexit_p
#define	__devexit	__exit
#define	__devexit_p	__exit_p
#endif


/* we rely on the rtc framework to handle locking (rtc->ops_lock),
 * so the only other requirement is that register accesses which
 * require BUSY to be clear are made with IRQs locally disabled
 */
static void rtc_wait_not_busy(void)
{
	int	count = 0;
	u8	status;

	/* BUSY may stay active for 1/32768 second (~30 usec) */
	for (count = 0; count < 50; count++) {
		status = rtc_read(OMAP_RTC_STATUS_REG);
		if ((status & (u8)OMAP_RTC_STATUS_BUSY) == 0)
			break;
		udelay(1);
	}
	/* now we have ~15 usec to read/write various registers */
}

static irqreturn_t rtc_irq(int irq, void *rtc)
{
	unsigned long		events = 0;
	u8			irq_data;

	irq_data = rtc_read(OMAP_RTC_STATUS_REG);

	/* alarm irq? */
	if (irq_data & OMAP_RTC_STATUS_ALARM) {
		rtc_write(OMAP_RTC_STATUS_ALARM, OMAP_RTC_STATUS_REG);
		events |= RTC_IRQF | RTC_AF;
	}

	/* 1/sec periodic/update irq? */
	if (irq_data & OMAP_RTC_STATUS_1S_EVENT)
		events |= RTC_IRQF | RTC_UF;

	rtc_update_irq(rtc, 1, events);

	return IRQ_HANDLED;
}

#ifdef	CONFIG_RTC_INTF_DEV

static int
omap_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	u8 reg;

	switch (cmd) {
	case RTC_AIE_OFF:
	case RTC_AIE_ON:
	case RTC_UIE_OFF:
	case RTC_UIE_ON:
		break;
	default:
		return -ENOIOCTLCMD;
	}

	local_irq_disable();
	rtc_wait_not_busy();
	reg = rtc_read(OMAP_RTC_INTERRUPTS_REG);
	switch (cmd) {
	/* AIE = Alarm Interrupt Enable */
	case RTC_AIE_OFF:
		reg &= ~OMAP_RTC_INTERRUPTS_IT_ALARM;
		break;
	case RTC_AIE_ON:
		reg |= OMAP_RTC_INTERRUPTS_IT_ALARM;
		break;
	/* UIE = Update Interrupt Enable (1/second) */
	case RTC_UIE_OFF:
		reg &= ~OMAP_RTC_INTERRUPTS_IT_TIMER;
		break;
	case RTC_UIE_ON:
		reg |= OMAP_RTC_INTERRUPTS_IT_TIMER;
		break;
	}
	rtc_wait_not_busy();
	rtc_write(reg, OMAP_RTC_INTERRUPTS_REG);
	local_irq_enable();

	return 0;
}

#else
#define	omap_rtc_ioctl	NULL
#endif

/* this hardware doesn't support "don't care" alarm fields */
static int tm2bcd(struct rtc_time *tm)
{
	if (rtc_valid_tm(tm) != 0)
		return -EINVAL;

	tm->tm_sec = BIN2BCD(tm->tm_sec);
	tm->tm_min = BIN2BCD(tm->tm_min);
	tm->tm_hour = BIN2BCD(tm->tm_hour);
	tm->tm_mday = BIN2BCD(tm->tm_mday);

	tm->tm_mon = BIN2BCD(tm->tm_mon + 1);

	/* epoch == 1900 */
	if (tm->tm_year < 100 || tm->tm_year > 199)
		return -EINVAL;
	tm->tm_year = BIN2BCD(tm->tm_year - 100);

	return 0;
}

static void bcd2tm(struct rtc_time *tm)
{
	tm->tm_sec = BCD2BIN(tm->tm_sec);
	tm->tm_min = BCD2BIN(tm->tm_min);
	tm->tm_hour = BCD2BIN(tm->tm_hour);
	tm->tm_mday = BCD2BIN(tm->tm_mday);
	tm->tm_mon = BCD2BIN(tm->tm_mon) - 1;
	/* epoch == 1900 */
	tm->tm_year = BCD2BIN(tm->tm_year) + 100;
}


static int omap_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	/* we don't report wday/yday/isdst ... */
	local_irq_disable();
	rtc_wait_not_busy();

	tm->tm_sec = rtc_read(OMAP_RTC_SECONDS_REG);
	tm->tm_min = rtc_read(OMAP_RTC_MINUTES_REG);
	tm->tm_hour = rtc_read(OMAP_RTC_HOURS_REG);
	tm->tm_mday = rtc_read(OMAP_RTC_DAYS_REG);
	tm->tm_mon = rtc_read(OMAP_RTC_MONTHS_REG);
	tm->tm_year = rtc_read(OMAP_RTC_YEARS_REG);

	local_irq_enable();

	bcd2tm(tm);
	return 0;
}

static int omap_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	if (tm2bcd(tm) < 0)
		return -EINVAL;
	local_irq_disable();
	rtc_wait_not_busy();

	rtc_write(tm->tm_year, OMAP_RTC_YEARS_REG);
	rtc_write(tm->tm_mon, OMAP_RTC_MONTHS_REG);
	rtc_write(tm->tm_mday, OMAP_RTC_DAYS_REG);
	rtc_write(tm->tm_hour, OMAP_RTC_HOURS_REG);
	rtc_write(tm->tm_min, OMAP_RTC_MINUTES_REG);
	rtc_write(tm->tm_sec, OMAP_RTC_SECONDS_REG);

	local_irq_enable();

	return 0;
}

static int omap_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	local_irq_disable();
	rtc_wait_not_busy();

	alm->time.tm_sec = rtc_read(OMAP_RTC_ALARM_SECONDS_REG);
	alm->time.tm_min = rtc_read(OMAP_RTC_ALARM_MINUTES_REG);
	alm->time.tm_hour = rtc_read(OMAP_RTC_ALARM_HOURS_REG);
	alm->time.tm_mday = rtc_read(OMAP_RTC_ALARM_DAYS_REG);
	alm->time.tm_mon = rtc_read(OMAP_RTC_ALARM_MONTHS_REG);
	alm->time.tm_year = rtc_read(OMAP_RTC_ALARM_YEARS_REG);

	local_irq_enable();

	bcd2tm(&alm->time);
	alm->enabled = !!(rtc_read(OMAP_RTC_INTERRUPTS_REG)
			& OMAP_RTC_INTERRUPTS_IT_ALARM);

	return 0;
}

static int omap_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	u8 reg;

	if (tm2bcd(&alm->time) < 0)
		return -EINVAL;

	local_irq_disable();
	rtc_wait_not_busy();

	rtc_write(alm->time.tm_year, OMAP_RTC_ALARM_YEARS_REG);
	rtc_write(alm->time.tm_mon, OMAP_RTC_ALARM_MONTHS_REG);
	rtc_write(alm->time.tm_mday, OMAP_RTC_ALARM_DAYS_REG);
	rtc_write(alm->time.tm_hour, OMAP_RTC_ALARM_HOURS_REG);
	rtc_write(alm->time.tm_min, OMAP_RTC_ALARM_MINUTES_REG);
	rtc_write(alm->time.tm_sec, OMAP_RTC_ALARM_SECONDS_REG);

	reg = rtc_read(OMAP_RTC_INTERRUPTS_REG);
	if (alm->enabled)
		reg |= OMAP_RTC_INTERRUPTS_IT_ALARM;
	else
		reg &= ~OMAP_RTC_INTERRUPTS_IT_ALARM;
	rtc_write(reg, OMAP_RTC_INTERRUPTS_REG);

	local_irq_enable();

	return 0;
}

static struct rtc_class_ops omap_rtc_ops = {
	.ioctl		= omap_rtc_ioctl,
	.read_time	= omap_rtc_read_time,
	.set_time	= omap_rtc_set_time,
	.read_alarm	= omap_rtc_read_alarm,
	.set_alarm	= omap_rtc_set_alarm,
};

static int omap_rtc_alarm;
static int omap_rtc_timer;

static int __devinit omap_rtc_probe(struct platform_device *pdev)
{
	struct resource		*res, *mem;
	struct rtc_device	*rtc;
	u8			reg, new_ctrl;

	omap_rtc_timer = platform_get_irq(pdev, 0);
	if (omap_rtc_timer <= 0) {
		pr_debug("%s: no update irq?\n", pdev->name);
		return -ENOENT;
	}

	omap_rtc_alarm = platform_get_irq(pdev, 1);
	if (omap_rtc_alarm <= 0) {
		pr_debug("%s: no alarm irq?\n", pdev->name);
		return -ENOENT;
	}

	/* NOTE:  using static mapping for RTC registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res && res->start != OMAP_RTC_BASE) {
		pr_debug("%s: RTC registers at %08x, expected %08x\n",
			pdev->name, (unsigned) res->start, OMAP_RTC_BASE);
		return -ENOENT;
	}

	if (res)
		mem = request_mem_region(res->start,
				res->end - res->start + 1,
				pdev->name);
	else
		mem = NULL;
	if (!mem) {
		pr_debug("%s: RTC registers at %08x are not free\n",
			pdev->name, OMAP_RTC_BASE);
		return -EBUSY;
	}

	rtc = rtc_device_register(pdev->name, &pdev->dev,
			&omap_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		pr_debug("%s: can't register RTC device, err %ld\n",
			pdev->name, PTR_ERR(rtc));
		goto fail;
	}
	platform_set_drvdata(pdev, rtc);
	dev_set_devdata(&rtc->dev, mem);

	/* clear pending irqs, and set 1/second periodic,
	 * which we'll use instead of update irqs
	 */
	rtc_write(0, OMAP_RTC_INTERRUPTS_REG);

	/* clear old status */
	reg = rtc_read(OMAP_RTC_STATUS_REG);
	if (reg & (u8) OMAP_RTC_STATUS_POWER_UP) {
		pr_info("%s: RTC power up reset detected\n",
			pdev->name);
		rtc_write(OMAP_RTC_STATUS_POWER_UP, OMAP_RTC_STATUS_REG);
	}
	if (reg & (u8) OMAP_RTC_STATUS_ALARM)
		rtc_write(OMAP_RTC_STATUS_ALARM, OMAP_RTC_STATUS_REG);

	/* handle periodic and alarm irqs */
	if (request_irq(omap_rtc_timer, rtc_irq, IRQF_DISABLED,
			rtc->dev.bus_id, rtc)) {
		pr_debug("%s: RTC timer interrupt IRQ%d already claimed\n",
			pdev->name, omap_rtc_timer);
		goto fail0;
	}
	if (request_irq(omap_rtc_alarm, rtc_irq, IRQF_DISABLED,
			rtc->dev.bus_id, rtc)) {
		pr_debug("%s: RTC alarm interrupt IRQ%d already claimed\n",
			pdev->name, omap_rtc_alarm);
		goto fail1;
	}

	/* On boards with split power, RTC_ON_NOFF won't reset the RTC */
	reg = rtc_read(OMAP_RTC_CTRL_REG);
	if (reg & (u8) OMAP_RTC_CTRL_STOP)
		pr_info("%s: already running\n", pdev->name);

	/* force to 24 hour mode */
	new_ctrl = reg & ~(OMAP_RTC_CTRL_SPLIT|OMAP_RTC_CTRL_AUTO_COMP);
	new_ctrl |= OMAP_RTC_CTRL_STOP;

	/* BOARD-SPECIFIC CUSTOMIZATION CAN GO HERE:
	 *
	 *  - Boards wired so that RTC_WAKE_INT does something, and muxed
	 *    right (W13_1610_RTC_WAKE_INT is the default after chip reset),
	 *    should initialize the device wakeup flag appropriately.
	 *
	 *  - Boards wired so RTC_ON_nOFF is used as the reset signal,
	 *    rather than nPWRON_RESET, should forcibly enable split
	 *    power mode.  (Some chip errata report that RTC_CTRL_SPLIT
	 *    is write-only, and always reads as zero...)
	 */
	device_init_wakeup(&pdev->dev, 0);

	if (new_ctrl & (u8) OMAP_RTC_CTRL_SPLIT)
		pr_info("%s: split power mode\n", pdev->name);

	if (reg != new_ctrl)
		rtc_write(new_ctrl, OMAP_RTC_CTRL_REG);

	return 0;

fail1:
	free_irq(omap_rtc_timer, NULL);
fail0:
	rtc_device_unregister(rtc);
fail:
	release_resource(mem);
	return -EIO;
}

static int __devexit omap_rtc_remove(struct platform_device *pdev)
{
	struct rtc_device	*rtc = platform_get_drvdata(pdev);;

	device_init_wakeup(&pdev->dev, 0);

	/* leave rtc running, but disable irqs */
	rtc_write(0, OMAP_RTC_INTERRUPTS_REG);

	free_irq(omap_rtc_timer, rtc);
	free_irq(omap_rtc_alarm, rtc);

	release_resource(dev_get_devdata(&rtc->dev));
	rtc_device_unregister(rtc);
	return 0;
}

#ifdef CONFIG_PM

static u8 irqstat;

static int omap_rtc_suspend(struct platform_device *pdev, pm_message_t state)
{
	irqstat = rtc_read(OMAP_RTC_INTERRUPTS_REG);

	/* FIXME the RTC alarm is not currently acting as a wakeup event
	 * source, and in fact this enable() call is just saving a flag
	 * that's never used...
	 */
	if (device_may_wakeup(&pdev->dev))
		enable_irq_wake(omap_rtc_alarm);
	else
		rtc_write(0, OMAP_RTC_INTERRUPTS_REG);

	return 0;
}

static int omap_rtc_resume(struct platform_device *pdev)
{
	if (device_may_wakeup(&pdev->dev))
		disable_irq_wake(omap_rtc_alarm);
	else
		rtc_write(irqstat, OMAP_RTC_INTERRUPTS_REG);
	return 0;
}

#else
#define omap_rtc_suspend NULL
#define omap_rtc_resume  NULL
#endif

static void omap_rtc_shutdown(struct platform_device *pdev)
{
	rtc_write(0, OMAP_RTC_INTERRUPTS_REG);
}

MODULE_ALIAS("omap_rtc");
static struct platform_driver omap_rtc_driver = {
	.probe		= omap_rtc_probe,
	.remove		= __devexit_p(omap_rtc_remove),
	.suspend	= omap_rtc_suspend,
	.resume		= omap_rtc_resume,
	.shutdown	= omap_rtc_shutdown,
	.driver		= {
		.name	= "omap_rtc",
		.owner	= THIS_MODULE,
	},
};

static int __init rtc_init(void)
{
	return platform_driver_register(&omap_rtc_driver);
}
module_init(rtc_init);

static void __exit rtc_exit(void)
{
	platform_driver_unregister(&omap_rtc_driver);
}
module_exit(rtc_exit);

MODULE_AUTHOR("George G. Davis (and others)");
MODULE_LICENSE("GPL");
