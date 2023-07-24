// SPDX-License-Identifier: GPL-2.0+
/*
 * TI OMAP Real Time Clock interface for Linux
 *
 * Copyright (C) 2003 MontaVista Software, Inc.
 * Author: George G. Davis <gdavis@mvista.com> or <source@mvista.com>
 *
 * Copyright (C) 2006 David Brownell (new RTC framework)
 * Copyright (C) 2014 Johan Hovold <johan@kernel.org>
 */

#include <linux/bcd.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/rtc.h>
#include <linux/rtc/rtc-omap.h>

/*
 * The OMAP RTC is a year/month/day/hours/minutes/seconds BCD clock
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

#define OMAP_RTC_SCRATCH0_REG		0x60
#define OMAP_RTC_SCRATCH1_REG		0x64
#define OMAP_RTC_SCRATCH2_REG		0x68

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
#define OMAP_RTC_OSC_SEL_32KCLK_SRC	BIT(3)
#define OMAP_RTC_OSC_OSC32K_GZ_DISABLE	BIT(4)

/* OMAP_RTC_IRQWAKEEN bit fields: */
#define OMAP_RTC_IRQWAKEEN_ALARM_WAKEEN	BIT(1)

/* OMAP_RTC_PMIC bit fields: */
#define OMAP_RTC_PMIC_POWER_EN_EN	BIT(16)
#define OMAP_RTC_PMIC_EXT_WKUP_EN(x)	BIT(x)
#define OMAP_RTC_PMIC_EXT_WKUP_POL(x)	BIT(4 + x)

/* OMAP_RTC_KICKER values */
#define	KICK0_VALUE			0x83e70b13
#define	KICK1_VALUE			0x95a4f1e0

struct omap_rtc;

struct omap_rtc_device_type {
	bool has_32kclk_en;
	bool has_irqwakeen;
	bool has_pmic_mode;
	bool has_power_up_reset;
	void (*lock)(struct omap_rtc *rtc);
	void (*unlock)(struct omap_rtc *rtc);
};

struct omap_rtc {
	struct rtc_device *rtc;
	void __iomem *base;
	struct clk *clk;
	int irq_alarm;
	int irq_timer;
	u8 interrupts_reg;
	bool is_pmic_controller;
	bool has_ext_clk;
	bool is_suspending;
	const struct omap_rtc_device_type *type;
	struct pinctrl_dev *pctldev;
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

static void am3352_rtc_unlock(struct omap_rtc *rtc)
{
	rtc_writel(rtc, OMAP_RTC_KICK0_REG, KICK0_VALUE);
	rtc_writel(rtc, OMAP_RTC_KICK1_REG, KICK1_VALUE);
}

static void am3352_rtc_lock(struct omap_rtc *rtc)
{
	rtc_writel(rtc, OMAP_RTC_KICK0_REG, 0);
	rtc_writel(rtc, OMAP_RTC_KICK1_REG, 0);
}

static void default_rtc_unlock(struct omap_rtc *rtc)
{
}

static void default_rtc_lock(struct omap_rtc *rtc)
{
}

/*
 * We rely on the rtc framework to handle locking (rtc->ops_lock),
 * so the only other requirement is that register accesses which
 * require BUSY to be clear are made with IRQs locally disabled
 */
static void rtc_wait_not_busy(struct omap_rtc *rtc)
{
	int count;
	u8 status;

	/* BUSY may stay active for 1/32768 second (~30 usec) */
	for (count = 0; count < 50; count++) {
		status = rtc_read(rtc, OMAP_RTC_STATUS_REG);
		if (!(status & OMAP_RTC_STATUS_BUSY))
			break;
		udelay(1);
	}
	/* now we have ~15 usec to read/write various registers */
}

static irqreturn_t rtc_irq(int irq, void *dev_id)
{
	struct omap_rtc	*rtc = dev_id;
	unsigned long events = 0;
	u8 irq_data;

	irq_data = rtc_read(rtc, OMAP_RTC_STATUS_REG);

	/* alarm irq? */
	if (irq_data & OMAP_RTC_STATUS_ALARM) {
		rtc->type->unlock(rtc);
		rtc_write(rtc, OMAP_RTC_STATUS_REG, OMAP_RTC_STATUS_ALARM);
		rtc->type->lock(rtc);
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
	rtc->type->unlock(rtc);
	rtc_write(rtc, OMAP_RTC_INTERRUPTS_REG, reg);
	if (rtc->type->has_irqwakeen)
		rtc_write(rtc, OMAP_RTC_IRQWAKEEN, irqwake_reg);
	rtc->type->lock(rtc);
	local_irq_enable();

	return 0;
}

/* this hardware doesn't support "don't care" alarm fields */
static void tm2bcd(struct rtc_time *tm)
{
	tm->tm_sec = bin2bcd(tm->tm_sec);
	tm->tm_min = bin2bcd(tm->tm_min);
	tm->tm_hour = bin2bcd(tm->tm_hour);
	tm->tm_mday = bin2bcd(tm->tm_mday);

	tm->tm_mon = bin2bcd(tm->tm_mon + 1);
	tm->tm_year = bin2bcd(tm->tm_year - 100);
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

	tm2bcd(tm);

	local_irq_disable();
	rtc_wait_not_busy(rtc);

	rtc->type->unlock(rtc);
	rtc_write(rtc, OMAP_RTC_YEARS_REG, tm->tm_year);
	rtc_write(rtc, OMAP_RTC_MONTHS_REG, tm->tm_mon);
	rtc_write(rtc, OMAP_RTC_DAYS_REG, tm->tm_mday);
	rtc_write(rtc, OMAP_RTC_HOURS_REG, tm->tm_hour);
	rtc_write(rtc, OMAP_RTC_MINUTES_REG, tm->tm_min);
	rtc_write(rtc, OMAP_RTC_SECONDS_REG, tm->tm_sec);
	rtc->type->lock(rtc);

	local_irq_enable();

	return 0;
}

static int omap_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct omap_rtc *rtc = dev_get_drvdata(dev);
	u8 interrupts;

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

	interrupts = rtc_read(rtc, OMAP_RTC_INTERRUPTS_REG);
	alm->enabled = !!(interrupts & OMAP_RTC_INTERRUPTS_IT_ALARM);

	return 0;
}

static int omap_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct omap_rtc *rtc = dev_get_drvdata(dev);
	u8 reg, irqwake_reg = 0;

	tm2bcd(&alm->time);

	local_irq_disable();
	rtc_wait_not_busy(rtc);

	rtc->type->unlock(rtc);
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
	rtc->type->lock(rtc);

	local_irq_enable();

	return 0;
}

static struct omap_rtc *omap_rtc_power_off_rtc;

/**
 * omap_rtc_power_off_program: Set the pmic power off sequence. The RTC
 * generates pmic_pwr_enable control, which can be used to control an external
 * PMIC.
 */
int omap_rtc_power_off_program(struct device *dev)
{
	struct omap_rtc *rtc = omap_rtc_power_off_rtc;
	struct rtc_time tm;
	unsigned long now;
	int seconds;
	u32 val;

	rtc->type->unlock(rtc);
	/* enable pmic_power_en control */
	val = rtc_readl(rtc, OMAP_RTC_PMIC_REG);
	rtc_writel(rtc, OMAP_RTC_PMIC_REG, val | OMAP_RTC_PMIC_POWER_EN_EN);

again:
	/* Clear any existing ALARM2 event */
	rtc_writel(rtc, OMAP_RTC_STATUS_REG, OMAP_RTC_STATUS_ALARM2);

	/* set alarm one second from now */
	omap_rtc_read_time_raw(rtc, &tm);
	seconds = tm.tm_sec;
	bcd2tm(&tm);
	now = rtc_tm_to_time64(&tm);
	rtc_time64_to_tm(now + 1, &tm);

	tm2bcd(&tm);

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

	/* Retry in case roll over happened before alarm was armed. */
	if (rtc_read(rtc, OMAP_RTC_SECONDS_REG) != seconds) {
		val = rtc_read(rtc, OMAP_RTC_STATUS_REG);
		if (!(val & OMAP_RTC_STATUS_ALARM2))
			goto again;
	}

	rtc->type->lock(rtc);

	return 0;
}
EXPORT_SYMBOL(omap_rtc_power_off_program);

/*
 * omap_rtc_poweroff: RTC-controlled power off
 *
 * The RTC can be used to control an external PMIC via the pmic_power_en pin,
 * which can be configured to transition to OFF on ALARM2 events.
 *
 * Notes:
 * The one-second alarm offset is the shortest offset possible as the alarm
 * registers must be set before the next timer update and the offset
 * calculation is too heavy for everything to be done within a single access
 * period (~15 us).
 *
 * Called with local interrupts disabled.
 */
static void omap_rtc_power_off(void)
{
	struct rtc_device *rtc = omap_rtc_power_off_rtc->rtc;
	u32 val;

	omap_rtc_power_off_program(rtc->dev.parent);

	/* Set PMIC power enable and EXT_WAKEUP in case PB power on is used */
	omap_rtc_power_off_rtc->type->unlock(omap_rtc_power_off_rtc);
	val = rtc_readl(omap_rtc_power_off_rtc, OMAP_RTC_PMIC_REG);
	val |= OMAP_RTC_PMIC_POWER_EN_EN | OMAP_RTC_PMIC_EXT_WKUP_POL(0) |
			OMAP_RTC_PMIC_EXT_WKUP_EN(0);
	rtc_writel(omap_rtc_power_off_rtc, OMAP_RTC_PMIC_REG, val);
	omap_rtc_power_off_rtc->type->lock(omap_rtc_power_off_rtc);

	/*
	 * Wait for alarm to trigger (within one second) and external PMIC to
	 * power off the system. Add a 500 ms margin for external latencies
	 * (e.g. debounce circuits).
	 */
	mdelay(1500);
}

static const struct rtc_class_ops omap_rtc_ops = {
	.read_time	= omap_rtc_read_time,
	.set_time	= omap_rtc_set_time,
	.read_alarm	= omap_rtc_read_alarm,
	.set_alarm	= omap_rtc_set_alarm,
	.alarm_irq_enable = omap_rtc_alarm_irq_enable,
};

static const struct omap_rtc_device_type omap_rtc_default_type = {
	.has_power_up_reset = true,
	.lock		= default_rtc_lock,
	.unlock		= default_rtc_unlock,
};

static const struct omap_rtc_device_type omap_rtc_am3352_type = {
	.has_32kclk_en	= true,
	.has_irqwakeen	= true,
	.has_pmic_mode	= true,
	.lock		= am3352_rtc_lock,
	.unlock		= am3352_rtc_unlock,
};

static const struct omap_rtc_device_type omap_rtc_da830_type = {
	.lock		= am3352_rtc_lock,
	.unlock		= am3352_rtc_unlock,
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

static const struct pinctrl_pin_desc rtc_pins_desc[] = {
	PINCTRL_PIN(0, "ext_wakeup0"),
	PINCTRL_PIN(1, "ext_wakeup1"),
	PINCTRL_PIN(2, "ext_wakeup2"),
	PINCTRL_PIN(3, "ext_wakeup3"),
};

static int rtc_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return 0;
}

static const char *rtc_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
					unsigned int group)
{
	return NULL;
}

static const struct pinctrl_ops rtc_pinctrl_ops = {
	.get_groups_count = rtc_pinctrl_get_groups_count,
	.get_group_name = rtc_pinctrl_get_group_name,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinconf_generic_dt_free_map,
};

#define PIN_CONFIG_ACTIVE_HIGH		(PIN_CONFIG_END + 1)

static const struct pinconf_generic_params rtc_params[] = {
	{"ti,active-high", PIN_CONFIG_ACTIVE_HIGH, 0},
};

#ifdef CONFIG_DEBUG_FS
static const struct pin_config_item rtc_conf_items[ARRAY_SIZE(rtc_params)] = {
	PCONFDUMP(PIN_CONFIG_ACTIVE_HIGH, "input active high", NULL, false),
};
#endif

static int rtc_pinconf_get(struct pinctrl_dev *pctldev,
			unsigned int pin, unsigned long *config)
{
	struct omap_rtc *rtc = pinctrl_dev_get_drvdata(pctldev);
	unsigned int param = pinconf_to_config_param(*config);
	u32 val;
	u16 arg = 0;

	val = rtc_readl(rtc, OMAP_RTC_PMIC_REG);

	switch (param) {
	case PIN_CONFIG_INPUT_ENABLE:
		if (!(val & OMAP_RTC_PMIC_EXT_WKUP_EN(pin)))
			return -EINVAL;
		break;
	case PIN_CONFIG_ACTIVE_HIGH:
		if (val & OMAP_RTC_PMIC_EXT_WKUP_POL(pin))
			return -EINVAL;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int rtc_pinconf_set(struct pinctrl_dev *pctldev,
			unsigned int pin, unsigned long *configs,
			unsigned int num_configs)
{
	struct omap_rtc *rtc = pinctrl_dev_get_drvdata(pctldev);
	u32 val;
	unsigned int param;
	u32 param_val;
	int i;

	val = rtc_readl(rtc, OMAP_RTC_PMIC_REG);

	/* active low by default */
	val |= OMAP_RTC_PMIC_EXT_WKUP_POL(pin);

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		param_val = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_INPUT_ENABLE:
			if (param_val)
				val |= OMAP_RTC_PMIC_EXT_WKUP_EN(pin);
			else
				val &= ~OMAP_RTC_PMIC_EXT_WKUP_EN(pin);
			break;
		case PIN_CONFIG_ACTIVE_HIGH:
			val &= ~OMAP_RTC_PMIC_EXT_WKUP_POL(pin);
			break;
		default:
			dev_err(&rtc->rtc->dev, "Property %u not supported\n",
				param);
			return -ENOTSUPP;
		}
	}

	rtc->type->unlock(rtc);
	rtc_writel(rtc, OMAP_RTC_PMIC_REG, val);
	rtc->type->lock(rtc);

	return 0;
}

static const struct pinconf_ops rtc_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = rtc_pinconf_get,
	.pin_config_set = rtc_pinconf_set,
};

static struct pinctrl_desc rtc_pinctrl_desc = {
	.pins = rtc_pins_desc,
	.npins = ARRAY_SIZE(rtc_pins_desc),
	.pctlops = &rtc_pinctrl_ops,
	.confops = &rtc_pinconf_ops,
	.custom_params = rtc_params,
	.num_custom_params = ARRAY_SIZE(rtc_params),
#ifdef CONFIG_DEBUG_FS
	.custom_conf_items = rtc_conf_items,
#endif
	.owner = THIS_MODULE,
};

static int omap_rtc_scratch_read(void *priv, unsigned int offset, void *_val,
				 size_t bytes)
{
	struct omap_rtc	*rtc = priv;
	u32 *val = _val;
	int i;

	for (i = 0; i < bytes / 4; i++)
		val[i] = rtc_readl(rtc,
				   OMAP_RTC_SCRATCH0_REG + offset + (i * 4));

	return 0;
}

static int omap_rtc_scratch_write(void *priv, unsigned int offset, void *_val,
				  size_t bytes)
{
	struct omap_rtc	*rtc = priv;
	u32 *val = _val;
	int i;

	rtc->type->unlock(rtc);
	for (i = 0; i < bytes / 4; i++)
		rtc_writel(rtc,
			   OMAP_RTC_SCRATCH0_REG + offset + (i * 4), val[i]);
	rtc->type->lock(rtc);

	return 0;
}

static struct nvmem_config omap_rtc_nvmem_config = {
	.name = "omap_rtc_scratch",
	.word_size = 4,
	.stride = 4,
	.size = OMAP_RTC_KICK0_REG - OMAP_RTC_SCRATCH0_REG,
	.reg_read = omap_rtc_scratch_read,
	.reg_write = omap_rtc_scratch_write,
};

static int omap_rtc_probe(struct platform_device *pdev)
{
	struct omap_rtc	*rtc;
	u8 reg, mask, new_ctrl;
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
			of_device_is_system_power_controller(pdev->dev.of_node);
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

	rtc->clk = devm_clk_get(&pdev->dev, "ext-clk");
	if (!IS_ERR(rtc->clk))
		rtc->has_ext_clk = true;
	else
		rtc->clk = devm_clk_get(&pdev->dev, "int-clk");

	if (!IS_ERR(rtc->clk))
		clk_prepare_enable(rtc->clk);

	rtc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rtc->base)) {
		clk_disable_unprepare(rtc->clk);
		return PTR_ERR(rtc->base);
	}

	platform_set_drvdata(pdev, rtc);

	/* Enable the clock/module so that we can access the registers */
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	rtc->type->unlock(rtc);

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
	if (reg & OMAP_RTC_CTRL_STOP)
		dev_info(&pdev->dev, "already running\n");

	/* force to 24 hour mode */
	new_ctrl = reg & (OMAP_RTC_CTRL_SPLIT | OMAP_RTC_CTRL_AUTO_COMP);
	new_ctrl |= OMAP_RTC_CTRL_STOP;

	/*
	 * BOARD-SPECIFIC CUSTOMIZATION CAN GO HERE:
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

	if (new_ctrl & OMAP_RTC_CTRL_SPLIT)
		dev_info(&pdev->dev, "split power mode\n");

	if (reg != new_ctrl)
		rtc_write(rtc, OMAP_RTC_CTRL_REG, new_ctrl);

	/*
	 * If we have the external clock then switch to it so we can keep
	 * ticking across suspend.
	 */
	if (rtc->has_ext_clk) {
		reg = rtc_read(rtc, OMAP_RTC_OSC_REG);
		reg &= ~OMAP_RTC_OSC_OSC32K_GZ_DISABLE;
		reg |= OMAP_RTC_OSC_32KCLK_EN | OMAP_RTC_OSC_SEL_32KCLK_SRC;
		rtc_writel(rtc, OMAP_RTC_OSC_REG, reg);
	}

	rtc->type->lock(rtc);

	device_init_wakeup(&pdev->dev, true);

	rtc->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc->rtc)) {
		ret = PTR_ERR(rtc->rtc);
		goto err;
	}

	rtc->rtc->ops = &omap_rtc_ops;
	rtc->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc->rtc->range_max = RTC_TIMESTAMP_END_2099;
	omap_rtc_nvmem_config.priv = rtc;

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

	/* Support ext_wakeup pinconf */
	rtc_pinctrl_desc.name = dev_name(&pdev->dev);

	rtc->pctldev = pinctrl_register(&rtc_pinctrl_desc, &pdev->dev, rtc);
	if (IS_ERR(rtc->pctldev)) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		ret = PTR_ERR(rtc->pctldev);
		goto err;
	}

	ret = rtc_register_device(rtc->rtc);
	if (ret)
		goto err_deregister_pinctrl;

	rtc_nvmem_register(rtc->rtc, &omap_rtc_nvmem_config);

	if (rtc->is_pmic_controller) {
		if (!pm_power_off) {
			omap_rtc_power_off_rtc = rtc;
			pm_power_off = omap_rtc_power_off;
		}
	}

	return 0;

err_deregister_pinctrl:
	pinctrl_unregister(rtc->pctldev);
err:
	clk_disable_unprepare(rtc->clk);
	device_init_wakeup(&pdev->dev, false);
	rtc->type->lock(rtc);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int omap_rtc_remove(struct platform_device *pdev)
{
	struct omap_rtc *rtc = platform_get_drvdata(pdev);
	u8 reg;

	if (pm_power_off == omap_rtc_power_off &&
			omap_rtc_power_off_rtc == rtc) {
		pm_power_off = NULL;
		omap_rtc_power_off_rtc = NULL;
	}

	device_init_wakeup(&pdev->dev, 0);

	if (!IS_ERR(rtc->clk))
		clk_disable_unprepare(rtc->clk);

	rtc->type->unlock(rtc);
	/* leave rtc running, but disable irqs */
	rtc_write(rtc, OMAP_RTC_INTERRUPTS_REG, 0);

	if (rtc->has_ext_clk) {
		reg = rtc_read(rtc, OMAP_RTC_OSC_REG);
		reg &= ~OMAP_RTC_OSC_SEL_32KCLK_SRC;
		rtc_write(rtc, OMAP_RTC_OSC_REG, reg);
	}

	rtc->type->lock(rtc);

	/* Disable the clock/module */
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	/* Remove ext_wakeup pinconf */
	pinctrl_unregister(rtc->pctldev);

	return 0;
}

static int __maybe_unused omap_rtc_suspend(struct device *dev)
{
	struct omap_rtc *rtc = dev_get_drvdata(dev);

	rtc->interrupts_reg = rtc_read(rtc, OMAP_RTC_INTERRUPTS_REG);

	rtc->type->unlock(rtc);
	/*
	 * FIXME: the RTC alarm is not currently acting as a wakeup event
	 * source on some platforms, and in fact this enable() call is just
	 * saving a flag that's never used...
	 */
	if (device_may_wakeup(dev))
		enable_irq_wake(rtc->irq_alarm);
	else
		rtc_write(rtc, OMAP_RTC_INTERRUPTS_REG, 0);
	rtc->type->lock(rtc);

	rtc->is_suspending = true;

	return 0;
}

static int __maybe_unused omap_rtc_resume(struct device *dev)
{
	struct omap_rtc *rtc = dev_get_drvdata(dev);

	rtc->type->unlock(rtc);
	if (device_may_wakeup(dev))
		disable_irq_wake(rtc->irq_alarm);
	else
		rtc_write(rtc, OMAP_RTC_INTERRUPTS_REG, rtc->interrupts_reg);
	rtc->type->lock(rtc);

	rtc->is_suspending = false;

	return 0;
}

static int __maybe_unused omap_rtc_runtime_suspend(struct device *dev)
{
	struct omap_rtc *rtc = dev_get_drvdata(dev);

	if (rtc->is_suspending && !rtc->has_ext_clk)
		return -EBUSY;

	return 0;
}

static const struct dev_pm_ops omap_rtc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(omap_rtc_suspend, omap_rtc_resume)
	SET_RUNTIME_PM_OPS(omap_rtc_runtime_suspend, NULL, NULL)
};

static void omap_rtc_shutdown(struct platform_device *pdev)
{
	struct omap_rtc *rtc = platform_get_drvdata(pdev);
	u8 mask;

	/*
	 * Keep the ALARM interrupt enabled to allow the system to power up on
	 * alarm events.
	 */
	rtc->type->unlock(rtc);
	mask = rtc_read(rtc, OMAP_RTC_INTERRUPTS_REG);
	mask &= OMAP_RTC_INTERRUPTS_IT_ALARM;
	rtc_write(rtc, OMAP_RTC_INTERRUPTS_REG, mask);
	rtc->type->lock(rtc);
}

static struct platform_driver omap_rtc_driver = {
	.probe		= omap_rtc_probe,
	.remove		= omap_rtc_remove,
	.shutdown	= omap_rtc_shutdown,
	.driver		= {
		.name	= "omap_rtc",
		.pm	= &omap_rtc_pm_ops,
		.of_match_table = omap_rtc_of_match,
	},
	.id_table	= omap_rtc_id_table,
};

module_platform_driver(omap_rtc_driver);

MODULE_ALIAS("platform:omap_rtc");
MODULE_AUTHOR("George G. Davis (and others)");
MODULE_LICENSE("GPL");
