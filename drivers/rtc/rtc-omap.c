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
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>

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
 * low power modes) for OMAP1 boards (OMAP-L138 has this built into
 * the SoC). See the BOARD-SPECIFIC CUSTOMIZATION comment.
 */

#define	DRIVER_NAME			"omap_rtc"

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

#define OMAP_RTC_KICK0_REG		0x6c
#define OMAP_RTC_KICK1_REG		0x70

#define OMAP_RTC_IRQWAKEEN		0x7c

/* OMAP_RTC_CTRL_REG bit fields: */
#define OMAP_RTC_CTRL_SPLIT		BIT(7)
#define OMAP_RTC_CTRL_DISABLE		BIT(6)
#define OMAP_RTC_CTRL_SET_32_COUNTER	BIT(5)
#define OMAP_RTC_CTRL_TEST		BIT(4)
#define OMAP_RTC_CTRL_MODE_12_24	BIT(3)
#define OMAP_RTC_CTRL_AUTO_COMP		BIT(2)
#define OMAP_RTC_CTRL_ROUND_30S		BIT(1)
#define OMAP_RTC_CTRL_STOP		BIT(0)

/* OMAP_RTC_STATUS_REG bit fields: */
#define OMAP_RTC_STATUS_POWER_UP	BIT(7)
#define OMAP_RTC_STATUS_ALARM		BIT(6)
#define OMAP_RTC_STATUS_1D_EVENT	BIT(5)
#define OMAP_RTC_STATUS_1H_EVENT	BIT(4)
#define OMAP_RTC_STATUS_1M_EVENT	BIT(3)
#define OMAP_RTC_STATUS_1S_EVENT	BIT(2)
#define OMAP_RTC_STATUS_RUN		BIT(1)
#define OMAP_RTC_STATUS_BUSY		BIT(0)

/* OMAP_RTC_INTERRUPTS_REG bit fields: */
#define OMAP_RTC_INTERRUPTS_IT_ALARM	BIT(3)
#define OMAP_RTC_INTERRUPTS_IT_TIMER	BIT(2)

/* OMAP_RTC_OSC_REG bit fields: */
#define OMAP_RTC_OSC_32KCLK_EN		BIT(6)

/* OMAP_RTC_IRQWAKEEN bit fields: */
#define OMAP_RTC_IRQWAKEEN_ALARM_WAKEEN	BIT(1)

/* OMAP_RTC_KICKER values */
#define	KICK0_VALUE			0x83e70b13
#define	KICK1_VALUE			0x95a4f1e0

#define	OMAP_RTC_HAS_KICKER		BIT(0)

/*
 * Few RTC IP revisions has special WAKE-EN Register to enable Wakeup
 * generation for event Alarm.
 */
#define	OMAP_RTC_HAS_IRQWAKEEN		BIT(1)

/*
 * Some RTC IP revisions (like those in AM335x and DRA7x) need
 * the 32KHz clock to be explicitly enabled.
 */
#define OMAP_RTC_HAS_32KCLK_EN		BIT(2)

static void __iomem	*rtc_base;

#define rtc_read(addr)		readb(rtc_base + (addr))
#define rtc_write(val, addr)	writeb(val, rtc_base + (addr))

#define rtc_writel(val, addr)	writel(val, rtc_base + (addr))


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

static int omap_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	u8 reg, irqwake_reg = 0;
	struct platform_device *pdev = to_platform_device(dev);
	const struct platform_device_id *id_entry =
					platform_get_device_id(pdev);

	local_irq_disable();
	rtc_wait_not_busy();
	reg = rtc_read(OMAP_RTC_INTERRUPTS_REG);
	if (id_entry->driver_data & OMAP_RTC_HAS_IRQWAKEEN)
		irqwake_reg = rtc_read(OMAP_RTC_IRQWAKEEN);

	if (enabled) {
		reg |= OMAP_RTC_INTERRUPTS_IT_ALARM;
		irqwake_reg |= OMAP_RTC_IRQWAKEEN_ALARM_WAKEEN;
	} else {
		reg &= ~OMAP_RTC_INTERRUPTS_IT_ALARM;
		irqwake_reg &= ~OMAP_RTC_IRQWAKEEN_ALARM_WAKEEN;
	}
	rtc_wait_not_busy();
	rtc_write(reg, OMAP_RTC_INTERRUPTS_REG);
	if (id_entry->driver_data & OMAP_RTC_HAS_IRQWAKEEN)
		rtc_write(irqwake_reg, OMAP_RTC_IRQWAKEEN);
	local_irq_enable();

	return 0;
}

/* this hardware doesn't support "don't care" alarm fields */
static int tm2bcd(struct rtc_time *tm)
{
	if (rtc_valid_tm(tm) != 0)
		return -EINVAL;

	tm->tm_sec = bin2bcd(tm->tm_sec);
	tm->tm_min = bin2bcd(tm->tm_min);
	tm->tm_hour = bin2bcd(tm->tm_hour);
	tm->tm_mday = bin2bcd(tm->tm_mday);

	tm->tm_mon = bin2bcd(tm->tm_mon + 1);

	/* epoch == 1900 */
	if (tm->tm_year < 100 || tm->tm_year > 199)
		return -EINVAL;
	tm->tm_year = bin2bcd(tm->tm_year - 100);

	return 0;
}

static void bcd2tm(struct rtc_time *tm)
{
	tm->tm_sec = bcd2bin(tm->tm_sec);
	tm->tm_min = bcd2bin(tm->tm_min);
	tm->tm_hour = bcd2bin(tm->tm_hour);
	tm->tm_mday = bcd2bin(tm->tm_mday);
	tm->tm_mon = bcd2bin(tm->tm_mon) - 1;
	/* epoch == 1900 */
	tm->tm_year = bcd2bin(tm->tm_year) + 100;
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
	u8 reg, irqwake_reg = 0;
	struct platform_device *pdev = to_platform_device(dev);
	const struct platform_device_id *id_entry =
					platform_get_device_id(pdev);

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
	if (id_entry->driver_data & OMAP_RTC_HAS_IRQWAKEEN)
		irqwake_reg = rtc_read(OMAP_RTC_IRQWAKEEN);

	if (alm->enabled) {
		reg |= OMAP_RTC_INTERRUPTS_IT_ALARM;
		irqwake_reg |= OMAP_RTC_IRQWAKEEN_ALARM_WAKEEN;
	} else {
		reg &= ~OMAP_RTC_INTERRUPTS_IT_ALARM;
		irqwake_reg &= ~OMAP_RTC_IRQWAKEEN_ALARM_WAKEEN;
	}
	rtc_write(reg, OMAP_RTC_INTERRUPTS_REG);
	if (id_entry->driver_data & OMAP_RTC_HAS_IRQWAKEEN)
		rtc_write(irqwake_reg, OMAP_RTC_IRQWAKEEN);

	local_irq_enable();

	return 0;
}

static struct rtc_class_ops omap_rtc_ops = {
	.read_time	= omap_rtc_read_time,
	.set_time	= omap_rtc_set_time,
	.read_alarm	= omap_rtc_read_alarm,
	.set_alarm	= omap_rtc_set_alarm,
	.alarm_irq_enable = omap_rtc_alarm_irq_enable,
};

static int omap_rtc_alarm;
static int omap_rtc_timer;

#define	OMAP_RTC_DATA_AM3352_IDX	1
#define	OMAP_RTC_DATA_DA830_IDX		2

static struct platform_device_id omap_rtc_devtype[] = {
	{
		.name	= DRIVER_NAME,
	},
	[OMAP_RTC_DATA_AM3352_IDX] = {
		.name	= "am3352-rtc",
		.driver_data = OMAP_RTC_HAS_KICKER | OMAP_RTC_HAS_IRQWAKEEN |
			       OMAP_RTC_HAS_32KCLK_EN,
	},
	[OMAP_RTC_DATA_DA830_IDX] = {
		.name	= "da830-rtc",
		.driver_data = OMAP_RTC_HAS_KICKER,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, omap_rtc_devtype);

static const struct of_device_id omap_rtc_of_match[] = {
	{	.compatible	= "ti,da830-rtc",
		.data		= &omap_rtc_devtype[OMAP_RTC_DATA_DA830_IDX],
	},
	{	.compatible	= "ti,am3352-rtc",
		.data		= &omap_rtc_devtype[OMAP_RTC_DATA_AM3352_IDX],
	},
	{},
};
MODULE_DEVICE_TABLE(of, omap_rtc_of_match);

static int __init omap_rtc_probe(struct platform_device *pdev)
{
	struct resource		*res;
	struct rtc_device	*rtc;
	u8			reg, new_ctrl;
	const struct platform_device_id *id_entry;
	const struct of_device_id *of_id;

	of_id = of_match_device(omap_rtc_of_match, &pdev->dev);
	if (of_id)
		pdev->id_entry = of_id->data;

	id_entry = platform_get_device_id(pdev);
	if (!id_entry) {
		dev_err(&pdev->dev, "no matching device entry\n");
		return -ENODEV;
	}

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

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rtc_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rtc_base))
		return PTR_ERR(rtc_base);

	/* Enable the clock/module so that we can access the registers */
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	if (id_entry->driver_data & OMAP_RTC_HAS_KICKER) {
		rtc_writel(KICK0_VALUE, OMAP_RTC_KICK0_REG);
		rtc_writel(KICK1_VALUE, OMAP_RTC_KICK1_REG);
	}

	device_init_wakeup(&pdev->dev, true);

	rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
			&omap_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		pr_debug("%s: can't register RTC device, err %ld\n",
			pdev->name, PTR_ERR(rtc));
		goto fail0;
	}
	platform_set_drvdata(pdev, rtc);

	/*
	 * disable interrupts
	 *
	 * NOTE: ALARM2 is not cleared on AM3352 if rtc_write (writeb) is used
	 */
	rtc_writel(0, OMAP_RTC_INTERRUPTS_REG);

	/* enable RTC functional clock */
	if (id_entry->driver_data & OMAP_RTC_HAS_32KCLK_EN) {
		reg = rtc_read(OMAP_RTC_OSC_REG);
		rtc_writel(reg | OMAP_RTC_OSC_32KCLK_EN, OMAP_RTC_OSC_REG);
	}

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
	if (devm_request_irq(&pdev->dev, omap_rtc_timer, rtc_irq, 0,
			dev_name(&rtc->dev), rtc)) {
		pr_debug("%s: RTC timer interrupt IRQ%d already claimed\n",
			pdev->name, omap_rtc_timer);
		goto fail0;
	}
	if ((omap_rtc_timer != omap_rtc_alarm) &&
		(devm_request_irq(&pdev->dev, omap_rtc_alarm, rtc_irq, 0,
			dev_name(&rtc->dev), rtc))) {
		pr_debug("%s: RTC alarm interrupt IRQ%d already claimed\n",
			pdev->name, omap_rtc_alarm);
		goto fail0;
	}

	/* On boards with split power, RTC_ON_NOFF won't reset the RTC */
	reg = rtc_read(OMAP_RTC_CTRL_REG);
	if (reg & (u8) OMAP_RTC_CTRL_STOP)
		pr_info("%s: already running\n", pdev->name);

	/* force to 24 hour mode */
	new_ctrl = reg & (OMAP_RTC_CTRL_SPLIT|OMAP_RTC_CTRL_AUTO_COMP);
	new_ctrl |= OMAP_RTC_CTRL_STOP;

	/* BOARD-SPECIFIC CUSTOMIZATION CAN GO HERE:
	 *
	 *  - Device wake-up capability setting should come through chip
	 *    init logic. OMAP1 boards should initialize the "wakeup capable"
	 *    flag in the platform device if the board is wired right for
	 *    being woken up by RTC alarm. For OMAP-L138, this capability
	 *    is built into the SoC by the "Deep Sleep" capability.
	 *
	 *  - Boards wired so RTC_ON_nOFF is used as the reset signal,
	 *    rather than nPWRON_RESET, should forcibly enable split
	 *    power mode.  (Some chip errata report that RTC_CTRL_SPLIT
	 *    is write-only, and always reads as zero...)
	 */

	if (new_ctrl & (u8) OMAP_RTC_CTRL_SPLIT)
		pr_info("%s: split power mode\n", pdev->name);

	if (reg != new_ctrl)
		rtc_write(new_ctrl, OMAP_RTC_CTRL_REG);

	return 0;

fail0:
	device_init_wakeup(&pdev->dev, false);
	if (id_entry->driver_data & OMAP_RTC_HAS_KICKER)
		rtc_writel(0, OMAP_RTC_KICK0_REG);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return -EIO;
}

static int __exit omap_rtc_remove(struct platform_device *pdev)
{
	const struct platform_device_id *id_entry =
				platform_get_device_id(pdev);

	device_init_wakeup(&pdev->dev, 0);

	/* leave rtc running, but disable irqs */
	rtc_write(0, OMAP_RTC_INTERRUPTS_REG);

	if (id_entry->driver_data & OMAP_RTC_HAS_KICKER)
		rtc_writel(0, OMAP_RTC_KICK0_REG);

	/* Disable the clock/module */
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static u8 irqstat;

static int omap_rtc_suspend(struct device *dev)
{
	irqstat = rtc_read(OMAP_RTC_INTERRUPTS_REG);

	/* FIXME the RTC alarm is not currently acting as a wakeup event
	 * source on some platforms, and in fact this enable() call is just
	 * saving a flag that's never used...
	 */
	if (device_may_wakeup(dev))
		enable_irq_wake(omap_rtc_alarm);
	else
		rtc_write(0, OMAP_RTC_INTERRUPTS_REG);

	/* Disable the clock/module */
	pm_runtime_put_sync(dev);

	return 0;
}

static int omap_rtc_resume(struct device *dev)
{
	/* Enable the clock/module so that we can access the registers */
	pm_runtime_get_sync(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(omap_rtc_alarm);
	else
		rtc_write(irqstat, OMAP_RTC_INTERRUPTS_REG);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(omap_rtc_pm_ops, omap_rtc_suspend, omap_rtc_resume);

static void omap_rtc_shutdown(struct platform_device *pdev)
{
	rtc_write(0, OMAP_RTC_INTERRUPTS_REG);
}

MODULE_ALIAS("platform:omap_rtc");
static struct platform_driver omap_rtc_driver = {
	.remove		= __exit_p(omap_rtc_remove),
	.shutdown	= omap_rtc_shutdown,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.pm	= &omap_rtc_pm_ops,
		.of_match_table = omap_rtc_of_match,
	},
	.id_table	= omap_rtc_devtype,
};

module_platform_driver_probe(omap_rtc_driver, omap_rtc_probe);

MODULE_AUTHOR("George G. Davis (and others)");
MODULE_LICENSE("GPL");
