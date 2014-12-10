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

#define OMAP_RTC_ALARM2_SECONDS_REG	0x80
#define OMAP_RTC_ALARM2_MINUTES_REG	0x84
#define OMAP_RTC_ALARM2_HOURS_REG	0x88
#define OMAP_RTC_ALARM2_DAYS_REG	0x8c
#define OMAP_RTC_ALARM2_MONTHS_REG	0x90
#define OMAP_RTC_ALARM2_YEARS_REG	0x94

#define OMAP_RTC_PMIC_REG		0x98

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
#define OMAP_RTC_STATUS_ALARM2		BIT(7)
#define OMAP_RTC_STATUS_ALARM		BIT(6)
#define OMAP_RTC_STATUS_1D_EVENT	BIT(5)
#define OMAP_RTC_STATUS_1H_EVENT	BIT(4)
#define OMAP_RTC_STATUS_1M_EVENT	BIT(3)
#define OMAP_RTC_STATUS_1S_EVENT	BIT(2)
#define OMAP_RTC_STATUS_RUN		BIT(1)
#define OMAP_RTC_STATUS_BUSY		BIT(0)

/* OMAP_RTC_INTERRUPTS_REG bit fields: */
#define OMAP_RTC_INTERRUPTS_IT_ALARM2	BIT(4)
#define OMAP_RTC_INTERRUPTS_IT_ALARM	BIT(3)
#define OMAP_RTC_INTERRUPTS_IT_TIMER	BIT(2)

/* OMAP_RTC_OSC_REG bit fields: */
#define OMAP_RTC_OSC_32KCLK_EN		BIT(6)

/* OMAP_RTC_IRQWAKEEN bit fields: */
#define OMAP_RTC_IRQWAKEEN_ALARM_WAKEEN	BIT(1)

/* OMAP_RTC_PMIC bit fields: */
#define OMAP_RTC_PMIC_POWER_EN_EN	BIT(16)

/* OMAP_RTC_KICKER values */
#define	KICK0_VALUE			0x83e70b13
#define	KICK1_VALUE			0x95a4f1e0

struct omap_rtc_device_type {
	bool has_32kclk_en;
	bool has_kicker;
	bool has_irqwakeen;
	bool has_pmic_mode;
	bool has_power_up_reset;
};

struct omap_rtc {
	struct rtc_device *rtc;
	void __iomem *base;
	int irq_alarm;
	int irq_timer;
	u8 interrupts_reg;
	bool is_pmic_controller;
	const struct omap_rtc_device_type *type;
};

static inline u8 rtc_read(struct omap_rtc *rtc, unsigned int reg)
{
	return readb(rtc->base + reg);
}

static inline u32 rtc_readl(struct omap_rtc *rtc, unsigned int reg)
{
	return readl(rtc->base + reg);
}

static inline void rtc_write(struct omap_rtc *rtc, unsigned int reg, u8 val)
{
	writeb(val, rtc->base + reg);
}

static inline void rtc_writel(struct omap_rtc *rtc, unsigned int reg, u32 val)
{
	writel(val, rtc->base + reg);
}

/* we rely on the rtc framework to handle locking (rtc->ops_lock),
 * so the only other requirement is that register accesses which
 * require BUSY to be clear are made with IRQs locally disabled
 */
static void rtc_wait_not_busy(struct omap_rtc *rtc)
{
	int	count = 0;
	u8	status;

	/* BUSY may stay active for 1/32768 second (~30 usec) */
	for (count = 0; count < 50; count++) {
		status = rtc_read(rtc, OMAP_RTC_STATUS_REG);
		if ((status & (u8)OMAP_RTC_STATUS_BUSY) == 0)
			break;
		udelay(1);
	}
	/* now we have ~15 usec to read/write various registers */
}

static irqreturn_t rtc_irq(int irq, void *dev_id)
{
	struct omap_rtc		*rtc = dev_id;
	unsigned long		events = 0;
	u8			irq_data;

	irq_data = rtc_read(rtc, OMAP_RTC_STATUS_REG);

	/* alarm irq? */
	if (irq_data & OMAP_RTC_STATUS_ALARM) {
		rtc_write(rtc, OMAP_RTC_STATUS_REG, OMAP_RTC_STATUS_ALARM);
		events |= RTC_IRQF | RTC_AF;
	}

	/* 1/sec periodic/update irq? */
	if (irq_data & OMAP_RTC_STATUS_1S_EVENT)
		events |= RTC_IRQF | RTC_UF;

	rtc_update_irq(rtc->rtc, 1, events);

	return IRQ_HANDLED;
}

static int omap_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct omap_rtc *rtc = dev_get_drvdata(dev);
	u8 reg, irqwake_reg = 0;

	local_irq_disable();
	rtc_wait_not_busy(rtc);
	reg = rtc_read(rtc, OMAP_RTC_INTERRUPTS_REG);
	if (rtc->type->has_irqwakeen)
		irqwake_reg = rtc_read(rtc, OMAP_RTC_IRQWAKEEN);

	if (enabled) {
		reg |= OMAP_RTC_INTERRUPTS_IT_ALARM;
		irqwake_reg |= OMAP_RTC_IRQWAKEEN_ALARM_WAKEEN;
	} else {
		reg &= ~OMAP_RTC_INTERRUPTS_IT_ALARM;
		irqwake_reg &= ~OMAP_RTC_IRQWAKEEN_ALARM_WAKEEN;
	}
	rtc_wait_not_busy(rtc);
	rtc_write(rtc, OMAP_RTC_INTERRUPTS_REG, reg);
	if (rtc->type->has_irqwakeen)
		rtc_write(rtc, OMAP_RTC_IRQWAKEEN, irqwake_reg);
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

static void omap_rtc_read_time_raw(struct omap_rtc *rtc, struct rtc_time *tm)
{
	tm->tm_sec = rtc_read(rtc, OMAP_RTC_SECONDS_REG);
	tm->tm_min = rtc_read(rtc, OMAP_RTC_MINUTES_REG);
	tm->tm_hour = rtc_read(rtc, OMAP_RTC_HOURS_REG);
	tm->tm_mday = rtc_read(rtc, OMAP_RTC_DAYS_REG);
	tm->tm_mon = rtc_read(rtc, OMAP_RTC_MONTHS_REG);
	tm->tm_year = rtc_read(rtc, OMAP_RTC_YEARS_REG);
}

static int omap_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct omap_rtc *rtc = dev_get_drvdata(dev);

	/* we don't report wday/yday/isdst ... */
	local_irq_disable();
	rtc_wait_not_busy(rtc);
	omap_rtc_read_time_raw(rtc, tm);
	local_irq_enable();

	bcd2tm(tm);
	return 0;
}

static int omap_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct omap_rtc *rtc = dev_get_drvdata(dev);

	if (tm2bcd(tm) < 0)
		return -EINVAL;
	local_irq_disable();
	rtc_wait_not_busy(rtc);

	rtc_write(rtc, OMAP_RTC_YEARS_REG, tm->tm_year);
	rtc_write(rtc, OMAP_RTC_MONTHS_REG, tm->tm_mon);
	rtc_write(rtc, OMAP_RTC_DAYS_REG, tm->tm_mday);
	rtc_write(rtc, OMAP_RTC_HOURS_REG, tm->tm_hour);
	rtc_write(rtc, OMAP_RTC_MINUTES_REG, tm->tm_min);
	rtc_write(rtc, OMAP_RTC_SECONDS_REG, tm->tm_sec);

	local_irq_enable();

	return 0;
}

static int omap_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct omap_rtc *rtc = dev_get_drvdata(dev);

	local_irq_disable();
	rtc_wait_not_busy(rtc);

	alm->time.tm_sec = rtc_read(rtc, OMAP_RTC_ALARM_SECONDS_REG);
	alm->time.tm_min = rtc_read(rtc, OMAP_RTC_ALARM_MINUTES_REG);
	alm->time.tm_hour = rtc_read(rtc, OMAP_RTC_ALARM_HOURS_REG);
	alm->time.tm_mday = rtc_read(rtc, OMAP_RTC_ALARM_DAYS_REG);
	alm->time.tm_mon = rtc_read(rtc, OMAP_RTC_ALARM_MONTHS_REG);
	alm->time.tm_year = rtc_read(rtc, OMAP_RTC_ALARM_YEARS_REG);

	local_irq_enable();

	bcd2tm(&alm->time);
	alm->enabled = !!(rtc_read(rtc, OMAP_RTC_INTERRUPTS_REG)
			& OMAP_RTC_INTERRUPTS_IT_ALARM);

	return 0;
}

static int omap_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct omap_rtc *rtc = dev_get_drvdata(dev);
	u8 reg, irqwake_reg = 0;

	if (tm2bcd(&alm->time) < 0)
		return -EINVAL;

	local_irq_disable();
	rtc_wait_not_busy(rtc);

	rtc_write(rtc, OMAP_RTC_ALARM_YEARS_REG, alm->time.tm_year);
	rtc_write(rtc, OMAP_RTC_ALARM_MONTHS_REG, alm->time.tm_mon);
	rtc_write(rtc, OMAP_RTC_ALARM_DAYS_REG, alm->time.tm_mday);
	rtc_write(rtc, OMAP_RTC_ALARM_HOURS_REG, alm->time.tm_hour);
	rtc_write(rtc, OMAP_RTC_ALARM_MINUTES_REG, alm->time.tm_min);
	rtc_write(rtc, OMAP_RTC_ALARM_SECONDS_REG, alm->time.tm_sec);

	reg = rtc_read(rtc, OMAP_RTC_INTERRUPTS_REG);
	if (rtc->type->has_irqwakeen)
		irqwake_reg = rtc_read(rtc, OMAP_RTC_IRQWAKEEN);

	if (alm->enabled) {
		reg |= OMAP_RTC_INTERRUPTS_IT_ALARM;
		irqwake_reg |= OMAP_RTC_IRQWAKEEN_ALARM_WAKEEN;
	} else {
		reg &= ~OMAP_RTC_INTERRUPTS_IT_ALARM;
		irqwake_reg &= ~OMAP_RTC_IRQWAKEEN_ALARM_WAKEEN;
	}
	rtc_write(rtc, OMAP_RTC_INTERRUPTS_REG, reg);
	if (rtc->type->has_irqwakeen)
		rtc_write(rtc, OMAP_RTC_IRQWAKEEN, irqwake_reg);

	local_irq_enable();

	return 0;
}

static struct omap_rtc *omap_rtc_power_off_rtc;

/*
 * omap_rtc_poweroff: RTC-controlled power off
 *
 * The RTC can be used to control an external PMIC via the pmic_power_en pin,
 * which can be configured to transition to OFF on ALARM2 events.
 *
 * Notes:
 * The two-second alarm offset is the shortest offset possible as the alarm
 * registers must be set before the next timer update and the offset
 * calculation is too heavy for everything to be done within a single access
 * period (~15 us).
 *
 * Called with local interrupts disabled.
 */
static void omap_rtc_power_off(void)
{
	struct omap_rtc *rtc = omap_rtc_power_off_rtc;
	struct rtc_time tm;
	unsigned long now;
	u32 val;

	/* enable pmic_power_en control */
	val = rtc_readl(rtc, OMAP_RTC_PMIC_REG);
	rtc_writel(rtc, OMAP_RTC_PMIC_REG, val | OMAP_RTC_PMIC_POWER_EN_EN);

	/* set alarm two seconds from now */
	omap_rtc_read_time_raw(rtc, &tm);
	bcd2tm(&tm);
	rtc_tm_to_time(&tm, &now);
	rtc_time_to_tm(now + 2, &tm);

	if (tm2bcd(&tm) < 0) {
		dev_err(&rtc->rtc->dev, "power off failed\n");
		return;
	}

	rtc_wait_not_busy(rtc);

	rtc_write(rtc, OMAP_RTC_ALARM2_SECONDS_REG, tm.tm_sec);
	rtc_write(rtc, OMAP_RTC_ALARM2_MINUTES_REG, tm.tm_min);
	rtc_write(rtc, OMAP_RTC_ALARM2_HOURS_REG, tm.tm_hour);
	rtc_write(rtc, OMAP_RTC_ALARM2_DAYS_REG, tm.tm_mday);
	rtc_write(rtc, OMAP_RTC_ALARM2_MONTHS_REG, tm.tm_mon);
	rtc_write(rtc, OMAP_RTC_ALARM2_YEARS_REG, tm.tm_year);

	/*
	 * enable ALARM2 interrupt
	 *
	 * NOTE: this fails on AM3352 if rtc_write (writeb) is used
	 */
	val = rtc_read(rtc, OMAP_RTC_INTERRUPTS_REG);
	rtc_writel(rtc, OMAP_RTC_INTERRUPTS_REG,
			val | OMAP_RTC_INTERRUPTS_IT_ALARM2);

	/*
	 * Wait for alarm to trigger (within two seconds) and external PMIC to
	 * power off the system. Add a 500 ms margin for external latencies
	 * (e.g. debounce circuits).
	 */
	mdelay(2500);
}

static struct rtc_class_ops omap_rtc_ops = {
	.read_time	= omap_rtc_read_time,
	.set_time	= omap_rtc_set_time,
	.read_alarm	= omap_rtc_read_alarm,
	.set_alarm	= omap_rtc_set_alarm,
	.alarm_irq_enable = omap_rtc_alarm_irq_enable,
};

static const struct omap_rtc_device_type omap_rtc_default_type = {
	.has_power_up_reset = true,
};

static const struct omap_rtc_device_type omap_rtc_am3352_type = {
	.has_32kclk_en	= true,
	.has_kicker	= true,
	.has_irqwakeen	= true,
	.has_pmic_mode	= true,
};

static const struct omap_rtc_device_type omap_rtc_da830_type = {
	.has_kicker	= true,
};

static const struct platform_device_id omap_rtc_id_table[] = {
	{
		.name	= "omap_rtc",
		.driver_data = (kernel_ulong_t)&omap_rtc_default_type,
	}, {
		.name	= "am3352-rtc",
		.driver_data = (kernel_ulong_t)&omap_rtc_am3352_type,
	}, {
		.name	= "da830-rtc",
		.driver_data = (kernel_ulong_t)&omap_rtc_da830_type,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, omap_rtc_id_table);

static const struct of_device_id omap_rtc_of_match[] = {
	{
		.compatible	= "ti,am3352-rtc",
		.data		= &omap_rtc_am3352_type,
	}, {
		.compatible	= "ti,da830-rtc",
		.data		= &omap_rtc_da830_type,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, omap_rtc_of_match);

static int __init omap_rtc_probe(struct platform_device *pdev)
{
	struct omap_rtc		*rtc;
	struct resource		*res;
	u8			reg, mask, new_ctrl;
	const struct platform_device_id *id_entry;
	const struct of_device_id *of_id;
	int ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	of_id = of_match_device(omap_rtc_of_match, &pdev->dev);
	if (of_id) {
		rtc->type = of_id->data;
		rtc->is_pmic_controller = rtc->type->has_pmic_mode &&
				of_property_read_bool(pdev->dev.of_node,
						"ti,system-power-controller");
	} else {
		id_entry = platform_get_device_id(pdev);
		rtc->type = (void *)id_entry->driver_data;
	}

	rtc->irq_timer = platform_get_irq(pdev, 0);
	if (rtc->irq_timer <= 0)
		return -ENOENT;

	rtc->irq_alarm = platform_get_irq(pdev, 1);
	if (rtc->irq_alarm <= 0)
		return -ENOENT;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rtc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rtc->base))
		return PTR_ERR(rtc->base);

	platform_set_drvdata(pdev, rtc);

	/* Enable the clock/module so that we can access the registers */
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	if (rtc->type->has_kicker) {
		rtc_writel(rtc, OMAP_RTC_KICK0_REG, KICK0_VALUE);
		rtc_writel(rtc, OMAP_RTC_KICK1_REG, KICK1_VALUE);
	}

	/*
	 * disable interrupts
	 *
	 * NOTE: ALARM2 is not cleared on AM3352 if rtc_write (writeb) is used
	 */
	rtc_writel(rtc, OMAP_RTC_INTERRUPTS_REG, 0);

	/* enable RTC functional clock */
	if (rtc->type->has_32kclk_en) {
		reg = rtc_read(rtc, OMAP_RTC_OSC_REG);
		rtc_writel(rtc, OMAP_RTC_OSC_REG,
				reg | OMAP_RTC_OSC_32KCLK_EN);
	}

	/* clear old status */
	reg = rtc_read(rtc, OMAP_RTC_STATUS_REG);

	mask = OMAP_RTC_STATUS_ALARM;

	if (rtc->type->has_pmic_mode)
		mask |= OMAP_RTC_STATUS_ALARM2;

	if (rtc->type->has_power_up_reset) {
		mask |= OMAP_RTC_STATUS_POWER_UP;
		if (reg & OMAP_RTC_STATUS_POWER_UP)
			dev_info(&pdev->dev, "RTC power up reset detected\n");
	}

	if (reg & mask)
		rtc_write(rtc, OMAP_RTC_STATUS_REG, reg & mask);

	/* On boards with split power, RTC_ON_NOFF won't reset the RTC */
	reg = rtc_read(rtc, OMAP_RTC_CTRL_REG);
	if (reg & (u8) OMAP_RTC_CTRL_STOP)
		dev_info(&pdev->dev, "already running\n");

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
		dev_info(&pdev->dev, "split power mode\n");

	if (reg != new_ctrl)
		rtc_write(rtc, OMAP_RTC_CTRL_REG, new_ctrl);

	device_init_wakeup(&pdev->dev, true);

	rtc->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
			&omap_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc->rtc)) {
		ret = PTR_ERR(rtc->rtc);
		goto err;
	}

	/* handle periodic and alarm irqs */
	ret = devm_request_irq(&pdev->dev, rtc->irq_timer, rtc_irq, 0,
			dev_name(&rtc->rtc->dev), rtc);
	if (ret)
		goto err;

	if (rtc->irq_timer != rtc->irq_alarm) {
		ret = devm_request_irq(&pdev->dev, rtc->irq_alarm, rtc_irq, 0,
				dev_name(&rtc->rtc->dev), rtc);
		if (ret)
			goto err;
	}

	if (rtc->is_pmic_controller) {
		if (!pm_power_off) {
			omap_rtc_power_off_rtc = rtc;
			pm_power_off = omap_rtc_power_off;
		}
	}

	return 0;

err:
	device_init_wakeup(&pdev->dev, false);
	if (rtc->type->has_kicker)
		rtc_writel(rtc, OMAP_RTC_KICK0_REG, 0);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int __exit omap_rtc_remove(struct platform_device *pdev)
{
	struct omap_rtc *rtc = platform_get_drvdata(pdev);

	if (pm_power_off == omap_rtc_power_off &&
			omap_rtc_power_off_rtc == rtc) {
		pm_power_off = NULL;
		omap_rtc_power_off_rtc = NULL;
	}

	device_init_wakeup(&pdev->dev, 0);

	/* leave rtc running, but disable irqs */
	rtc_write(rtc, OMAP_RTC_INTERRUPTS_REG, 0);

	if (rtc->type->has_kicker)
		rtc_writel(rtc, OMAP_RTC_KICK0_REG, 0);

	/* Disable the clock/module */
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int omap_rtc_suspend(struct device *dev)
{
	struct omap_rtc *rtc = dev_get_drvdata(dev);

	rtc->interrupts_reg = rtc_read(rtc, OMAP_RTC_INTERRUPTS_REG);

	/* FIXME the RTC alarm is not currently acting as a wakeup event
	 * source on some platforms, and in fact this enable() call is just
	 * saving a flag that's never used...
	 */
	if (device_may_wakeup(dev))
		enable_irq_wake(rtc->irq_alarm);
	else
		rtc_write(rtc, OMAP_RTC_INTERRUPTS_REG, 0);

	/* Disable the clock/module */
	pm_runtime_put_sync(dev);

	return 0;
}

static int omap_rtc_resume(struct device *dev)
{
	struct omap_rtc *rtc = dev_get_drvdata(dev);

	/* Enable the clock/module so that we can access the registers */
	pm_runtime_get_sync(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(rtc->irq_alarm);
	else
		rtc_write(rtc, OMAP_RTC_INTERRUPTS_REG, rtc->interrupts_reg);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(omap_rtc_pm_ops, omap_rtc_suspend, omap_rtc_resume);

static void omap_rtc_shutdown(struct platform_device *pdev)
{
	struct omap_rtc *rtc = platform_get_drvdata(pdev);

	rtc_write(rtc, OMAP_RTC_INTERRUPTS_REG, 0);
}

static struct platform_driver omap_rtc_driver = {
	.remove		= __exit_p(omap_rtc_remove),
	.shutdown	= omap_rtc_shutdown,
	.driver		= {
		.name	= "omap_rtc",
		.owner	= THIS_MODULE,
		.pm	= &omap_rtc_pm_ops,
		.of_match_table = omap_rtc_of_match,
	},
	.id_table	= omap_rtc_id_table,
};

module_platform_driver_probe(omap_rtc_driver, omap_rtc_probe);

MODULE_ALIAS("platform:omap_rtc");
MODULE_AUTHOR("George G. Davis (and others)");
MODULE_LICENSE("GPL");
