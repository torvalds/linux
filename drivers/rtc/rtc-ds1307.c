/*
 * rtc-ds1307.c - RTC driver for some mostly-compatible I2C chips.
 *
 *  Copyright (C) 2005 James Chapman (ds1337 core)
 *  Copyright (C) 2006 David Brownell
 *  Copyright (C) 2009 Matthias Fuchs (rx8025 support)
 *  Copyright (C) 2012 Bertrand Achard (nvram access fixes)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/bcd.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/rtc/ds1307.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>

/*
 * We can't determine type by probing, but if we expect pre-Linux code
 * to have set the chip up as a clock (turning on the oscillator and
 * setting the date and time), Linux can ignore the non-clock features.
 * That's a natural job for a factory or repair bench.
 */
enum ds_type {
	ds_1307,
	ds_1337,
	ds_1338,
	ds_1339,
	ds_1340,
	ds_1388,
	ds_3231,
	m41t0,
	m41t00,
	mcp794xx,
	rx_8025,
	last_ds_type /* always last */
	/* rs5c372 too?  different address... */
};


/* RTC registers don't differ much, except for the century flag */
#define DS1307_REG_SECS		0x00	/* 00-59 */
#	define DS1307_BIT_CH		0x80
#	define DS1340_BIT_nEOSC		0x80
#	define MCP794XX_BIT_ST		0x80
#define DS1307_REG_MIN		0x01	/* 00-59 */
#	define M41T0_BIT_OF		0x80
#define DS1307_REG_HOUR		0x02	/* 00-23, or 1-12{am,pm} */
#	define DS1307_BIT_12HR		0x40	/* in REG_HOUR */
#	define DS1307_BIT_PM		0x20	/* in REG_HOUR */
#	define DS1340_BIT_CENTURY_EN	0x80	/* in REG_HOUR */
#	define DS1340_BIT_CENTURY	0x40	/* in REG_HOUR */
#define DS1307_REG_WDAY		0x03	/* 01-07 */
#	define MCP794XX_BIT_VBATEN	0x08
#define DS1307_REG_MDAY		0x04	/* 01-31 */
#define DS1307_REG_MONTH	0x05	/* 01-12 */
#	define DS1337_BIT_CENTURY	0x80	/* in REG_MONTH */
#define DS1307_REG_YEAR		0x06	/* 00-99 */

/*
 * Other registers (control, status, alarms, trickle charge, NVRAM, etc)
 * start at 7, and they differ a LOT. Only control and status matter for
 * basic RTC date and time functionality; be careful using them.
 */
#define DS1307_REG_CONTROL	0x07		/* or ds1338 */
#	define DS1307_BIT_OUT		0x80
#	define DS1338_BIT_OSF		0x20
#	define DS1307_BIT_SQWE		0x10
#	define DS1307_BIT_RS1		0x02
#	define DS1307_BIT_RS0		0x01
#define DS1337_REG_CONTROL	0x0e
#	define DS1337_BIT_nEOSC		0x80
#	define DS1339_BIT_BBSQI		0x20
#	define DS3231_BIT_BBSQW		0x40 /* same as BBSQI */
#	define DS1337_BIT_RS2		0x10
#	define DS1337_BIT_RS1		0x08
#	define DS1337_BIT_INTCN		0x04
#	define DS1337_BIT_A2IE		0x02
#	define DS1337_BIT_A1IE		0x01
#define DS1340_REG_CONTROL	0x07
#	define DS1340_BIT_OUT		0x80
#	define DS1340_BIT_FT		0x40
#	define DS1340_BIT_CALIB_SIGN	0x20
#	define DS1340_M_CALIBRATION	0x1f
#define DS1340_REG_FLAG		0x09
#	define DS1340_BIT_OSF		0x80
#define DS1337_REG_STATUS	0x0f
#	define DS1337_BIT_OSF		0x80
#	define DS3231_BIT_EN32KHZ	0x08
#	define DS1337_BIT_A2I		0x02
#	define DS1337_BIT_A1I		0x01
#define DS1339_REG_ALARM1_SECS	0x07

#define DS13XX_TRICKLE_CHARGER_MAGIC	0xa0

#define RX8025_REG_CTRL1	0x0e
#	define RX8025_BIT_2412		0x20
#define RX8025_REG_CTRL2	0x0f
#	define RX8025_BIT_PON		0x10
#	define RX8025_BIT_VDET		0x40
#	define RX8025_BIT_XST		0x20


struct ds1307 {
	u8			offset; /* register's offset */
	u8			regs[11];
	u16			nvram_offset;
	struct bin_attribute	*nvram;
	enum ds_type		type;
	unsigned long		flags;
#define HAS_NVRAM	0		/* bit 0 == sysfs file active */
#define HAS_ALARM	1		/* bit 1 == irq claimed */
	struct device		*dev;
	struct regmap		*regmap;
	const char		*name;
	int			irq;
	struct rtc_device	*rtc;
#ifdef CONFIG_COMMON_CLK
	struct clk_hw		clks[2];
#endif
};

struct chip_desc {
	unsigned		alarm:1;
	u16			nvram_offset;
	u16			nvram_size;
	u16			trickle_charger_reg;
	u8			trickle_charger_setup;
	u8			(*do_trickle_setup)(struct ds1307 *, uint32_t,
						    bool);
};

static u8 do_trickle_setup_ds1339(struct ds1307 *, uint32_t ohms, bool diode);

static struct chip_desc chips[last_ds_type] = {
	[ds_1307] = {
		.nvram_offset	= 8,
		.nvram_size	= 56,
	},
	[ds_1337] = {
		.alarm		= 1,
	},
	[ds_1338] = {
		.nvram_offset	= 8,
		.nvram_size	= 56,
	},
	[ds_1339] = {
		.alarm		= 1,
		.trickle_charger_reg = 0x10,
		.do_trickle_setup = &do_trickle_setup_ds1339,
	},
	[ds_1340] = {
		.trickle_charger_reg = 0x08,
	},
	[ds_1388] = {
		.trickle_charger_reg = 0x0a,
	},
	[ds_3231] = {
		.alarm		= 1,
	},
	[mcp794xx] = {
		.alarm		= 1,
		/* this is battery backed SRAM */
		.nvram_offset	= 0x20,
		.nvram_size	= 0x40,
	},
};

static const struct i2c_device_id ds1307_id[] = {
	{ "ds1307", ds_1307 },
	{ "ds1337", ds_1337 },
	{ "ds1338", ds_1338 },
	{ "ds1339", ds_1339 },
	{ "ds1388", ds_1388 },
	{ "ds1340", ds_1340 },
	{ "ds3231", ds_3231 },
	{ "m41t0", m41t0 },
	{ "m41t00", m41t00 },
	{ "mcp7940x", mcp794xx },
	{ "mcp7941x", mcp794xx },
	{ "pt7c4338", ds_1307 },
	{ "rx8025", rx_8025 },
	{ "isl12057", ds_1337 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ds1307_id);

#ifdef CONFIG_OF
static const struct of_device_id ds1307_of_match[] = {
	{
		.compatible = "dallas,ds1307",
		.data = (void *)ds_1307
	},
	{
		.compatible = "dallas,ds1337",
		.data = (void *)ds_1337
	},
	{
		.compatible = "dallas,ds1338",
		.data = (void *)ds_1338
	},
	{
		.compatible = "dallas,ds1339",
		.data = (void *)ds_1339
	},
	{
		.compatible = "dallas,ds1388",
		.data = (void *)ds_1388
	},
	{
		.compatible = "dallas,ds1340",
		.data = (void *)ds_1340
	},
	{
		.compatible = "maxim,ds3231",
		.data = (void *)ds_3231
	},
	{
		.compatible = "st,m41t0",
		.data = (void *)m41t00
	},
	{
		.compatible = "st,m41t00",
		.data = (void *)m41t00
	},
	{
		.compatible = "microchip,mcp7940x",
		.data = (void *)mcp794xx
	},
	{
		.compatible = "microchip,mcp7941x",
		.data = (void *)mcp794xx
	},
	{
		.compatible = "pericom,pt7c4338",
		.data = (void *)ds_1307
	},
	{
		.compatible = "epson,rx8025",
		.data = (void *)rx_8025
	},
	{
		.compatible = "isil,isl12057",
		.data = (void *)ds_1337
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ds1307_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id ds1307_acpi_ids[] = {
	{ .id = "DS1307", .driver_data = ds_1307 },
	{ .id = "DS1337", .driver_data = ds_1337 },
	{ .id = "DS1338", .driver_data = ds_1338 },
	{ .id = "DS1339", .driver_data = ds_1339 },
	{ .id = "DS1388", .driver_data = ds_1388 },
	{ .id = "DS1340", .driver_data = ds_1340 },
	{ .id = "DS3231", .driver_data = ds_3231 },
	{ .id = "M41T0", .driver_data = m41t0 },
	{ .id = "M41T00", .driver_data = m41t00 },
	{ .id = "MCP7940X", .driver_data = mcp794xx },
	{ .id = "MCP7941X", .driver_data = mcp794xx },
	{ .id = "PT7C4338", .driver_data = ds_1307 },
	{ .id = "RX8025", .driver_data = rx_8025 },
	{ .id = "ISL12057", .driver_data = ds_1337 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, ds1307_acpi_ids);
#endif

/*
 * The ds1337 and ds1339 both have two alarms, but we only use the first
 * one (with a "seconds" field).  For ds1337 we expect nINTA is our alarm
 * signal; ds1339 chips have only one alarm signal.
 */
static irqreturn_t ds1307_irq(int irq, void *dev_id)
{
	struct ds1307		*ds1307 = dev_id;
	struct mutex		*lock = &ds1307->rtc->ops_lock;
	int			stat, control, ret;

	mutex_lock(lock);
	ret = regmap_read(ds1307->regmap, DS1337_REG_STATUS, &stat);
	if (ret)
		goto out;

	if (stat & DS1337_BIT_A1I) {
		stat &= ~DS1337_BIT_A1I;
		regmap_write(ds1307->regmap, DS1337_REG_STATUS, stat);

		ret = regmap_read(ds1307->regmap, DS1337_REG_CONTROL, &control);
		if (ret)
			goto out;

		control &= ~DS1337_BIT_A1IE;
		regmap_write(ds1307->regmap, DS1337_REG_CONTROL, control);

		rtc_update_irq(ds1307->rtc, 1, RTC_AF | RTC_IRQF);
	}

out:
	mutex_unlock(lock);

	return IRQ_HANDLED;
}

/*----------------------------------------------------------------------*/

static int ds1307_get_time(struct device *dev, struct rtc_time *t)
{
	struct ds1307	*ds1307 = dev_get_drvdata(dev);
	int		tmp, ret;

	/* read the RTC date and time registers all at once */
	ret = regmap_bulk_read(ds1307->regmap, ds1307->offset, ds1307->regs, 7);
	if (ret) {
		dev_err(dev, "%s error %d\n", "read", ret);
		return ret;
	}

	dev_dbg(dev, "%s: %7ph\n", "read", ds1307->regs);

	/* if oscillator fail bit is set, no data can be trusted */
	if (ds1307->type == m41t0 &&
	    ds1307->regs[DS1307_REG_MIN] & M41T0_BIT_OF) {
		dev_warn_once(dev, "oscillator failed, set time!\n");
		return -EINVAL;
	}

	t->tm_sec = bcd2bin(ds1307->regs[DS1307_REG_SECS] & 0x7f);
	t->tm_min = bcd2bin(ds1307->regs[DS1307_REG_MIN] & 0x7f);
	tmp = ds1307->regs[DS1307_REG_HOUR] & 0x3f;
	t->tm_hour = bcd2bin(tmp);
	t->tm_wday = bcd2bin(ds1307->regs[DS1307_REG_WDAY] & 0x07) - 1;
	t->tm_mday = bcd2bin(ds1307->regs[DS1307_REG_MDAY] & 0x3f);
	tmp = ds1307->regs[DS1307_REG_MONTH] & 0x1f;
	t->tm_mon = bcd2bin(tmp) - 1;
	t->tm_year = bcd2bin(ds1307->regs[DS1307_REG_YEAR]) + 100;

#ifdef CONFIG_RTC_DRV_DS1307_CENTURY
	switch (ds1307->type) {
	case ds_1337:
	case ds_1339:
	case ds_3231:
		if (ds1307->regs[DS1307_REG_MONTH] & DS1337_BIT_CENTURY)
			t->tm_year += 100;
		break;
	case ds_1340:
		if (ds1307->regs[DS1307_REG_HOUR] & DS1340_BIT_CENTURY)
			t->tm_year += 100;
		break;
	default:
		break;
	}
#endif

	dev_dbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"read", t->tm_sec, t->tm_min,
		t->tm_hour, t->tm_mday,
		t->tm_mon, t->tm_year, t->tm_wday);

	/* initial clock setting can be undefined */
	return rtc_valid_tm(t);
}

static int ds1307_set_time(struct device *dev, struct rtc_time *t)
{
	struct ds1307	*ds1307 = dev_get_drvdata(dev);
	int		result;
	int		tmp;
	u8		*buf = ds1307->regs;

	dev_dbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"write", t->tm_sec, t->tm_min,
		t->tm_hour, t->tm_mday,
		t->tm_mon, t->tm_year, t->tm_wday);

#ifdef CONFIG_RTC_DRV_DS1307_CENTURY
	if (t->tm_year < 100)
		return -EINVAL;

	switch (ds1307->type) {
	case ds_1337:
	case ds_1339:
	case ds_3231:
	case ds_1340:
		if (t->tm_year > 299)
			return -EINVAL;
	default:
		if (t->tm_year > 199)
			return -EINVAL;
		break;
	}
#else
	if (t->tm_year < 100 || t->tm_year > 199)
		return -EINVAL;
#endif

	buf[DS1307_REG_SECS] = bin2bcd(t->tm_sec);
	buf[DS1307_REG_MIN] = bin2bcd(t->tm_min);
	buf[DS1307_REG_HOUR] = bin2bcd(t->tm_hour);
	buf[DS1307_REG_WDAY] = bin2bcd(t->tm_wday + 1);
	buf[DS1307_REG_MDAY] = bin2bcd(t->tm_mday);
	buf[DS1307_REG_MONTH] = bin2bcd(t->tm_mon + 1);

	/* assume 20YY not 19YY */
	tmp = t->tm_year - 100;
	buf[DS1307_REG_YEAR] = bin2bcd(tmp);

	switch (ds1307->type) {
	case ds_1337:
	case ds_1339:
	case ds_3231:
		if (t->tm_year > 199)
			buf[DS1307_REG_MONTH] |= DS1337_BIT_CENTURY;
		break;
	case ds_1340:
		buf[DS1307_REG_HOUR] |= DS1340_BIT_CENTURY_EN;
		if (t->tm_year > 199)
			buf[DS1307_REG_HOUR] |= DS1340_BIT_CENTURY;
		break;
	case mcp794xx:
		/*
		 * these bits were cleared when preparing the date/time
		 * values and need to be set again before writing the
		 * buffer out to the device.
		 */
		buf[DS1307_REG_SECS] |= MCP794XX_BIT_ST;
		buf[DS1307_REG_WDAY] |= MCP794XX_BIT_VBATEN;
		break;
	default:
		break;
	}

	dev_dbg(dev, "%s: %7ph\n", "write", buf);

	result = regmap_bulk_write(ds1307->regmap, ds1307->offset, buf, 7);
	if (result) {
		dev_err(dev, "%s error %d\n", "write", result);
		return result;
	}
	return 0;
}

static int ds1337_read_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct ds1307		*ds1307 = dev_get_drvdata(dev);
	int			ret;

	if (!test_bit(HAS_ALARM, &ds1307->flags))
		return -EINVAL;

	/* read all ALARM1, ALARM2, and status registers at once */
	ret = regmap_bulk_read(ds1307->regmap, DS1339_REG_ALARM1_SECS,
			       ds1307->regs, 9);
	if (ret) {
		dev_err(dev, "%s error %d\n", "alarm read", ret);
		return ret;
	}

	dev_dbg(dev, "%s: %4ph, %3ph, %2ph\n", "alarm read",
		&ds1307->regs[0], &ds1307->regs[4], &ds1307->regs[7]);

	/*
	 * report alarm time (ALARM1); assume 24 hour and day-of-month modes,
	 * and that all four fields are checked matches
	 */
	t->time.tm_sec = bcd2bin(ds1307->regs[0] & 0x7f);
	t->time.tm_min = bcd2bin(ds1307->regs[1] & 0x7f);
	t->time.tm_hour = bcd2bin(ds1307->regs[2] & 0x3f);
	t->time.tm_mday = bcd2bin(ds1307->regs[3] & 0x3f);

	/* ... and status */
	t->enabled = !!(ds1307->regs[7] & DS1337_BIT_A1IE);
	t->pending = !!(ds1307->regs[8] & DS1337_BIT_A1I);

	dev_dbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, enabled=%d, pending=%d\n",
		"alarm read", t->time.tm_sec, t->time.tm_min,
		t->time.tm_hour, t->time.tm_mday,
		t->enabled, t->pending);

	return 0;
}

static int ds1337_set_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct ds1307		*ds1307 = dev_get_drvdata(dev);
	unsigned char		*buf = ds1307->regs;
	u8			control, status;
	int			ret;

	if (!test_bit(HAS_ALARM, &ds1307->flags))
		return -EINVAL;

	dev_dbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, enabled=%d, pending=%d\n",
		"alarm set", t->time.tm_sec, t->time.tm_min,
		t->time.tm_hour, t->time.tm_mday,
		t->enabled, t->pending);

	/* read current status of both alarms and the chip */
	ret = regmap_bulk_read(ds1307->regmap, DS1339_REG_ALARM1_SECS, buf, 9);
	if (ret) {
		dev_err(dev, "%s error %d\n", "alarm write", ret);
		return ret;
	}
	control = ds1307->regs[7];
	status = ds1307->regs[8];

	dev_dbg(dev, "%s: %4ph, %3ph, %02x %02x\n", "alarm set (old status)",
		&ds1307->regs[0], &ds1307->regs[4], control, status);

	/* set ALARM1, using 24 hour and day-of-month modes */
	buf[0] = bin2bcd(t->time.tm_sec);
	buf[1] = bin2bcd(t->time.tm_min);
	buf[2] = bin2bcd(t->time.tm_hour);
	buf[3] = bin2bcd(t->time.tm_mday);

	/* set ALARM2 to non-garbage */
	buf[4] = 0;
	buf[5] = 0;
	buf[6] = 0;

	/* disable alarms */
	buf[7] = control & ~(DS1337_BIT_A1IE | DS1337_BIT_A2IE);
	buf[8] = status & ~(DS1337_BIT_A1I | DS1337_BIT_A2I);

	ret = regmap_bulk_write(ds1307->regmap, DS1339_REG_ALARM1_SECS, buf, 9);
	if (ret) {
		dev_err(dev, "can't set alarm time\n");
		return ret;
	}

	/* optionally enable ALARM1 */
	if (t->enabled) {
		dev_dbg(dev, "alarm IRQ armed\n");
		buf[7] |= DS1337_BIT_A1IE;	/* only ALARM1 is used */
		regmap_write(ds1307->regmap, DS1337_REG_CONTROL, buf[7]);
	}

	return 0;
}

static int ds1307_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct ds1307		*ds1307 = dev_get_drvdata(dev);
	int			control, ret;

	if (!test_bit(HAS_ALARM, &ds1307->flags))
		return -ENOTTY;

	ret = regmap_read(ds1307->regmap, DS1337_REG_CONTROL, &control);
	if (ret)
		return ret;

	if (enabled)
		control |= DS1337_BIT_A1IE;
	else
		control &= ~DS1337_BIT_A1IE;

	return regmap_write(ds1307->regmap, DS1337_REG_CONTROL, control);
}

static const struct rtc_class_ops ds13xx_rtc_ops = {
	.read_time	= ds1307_get_time,
	.set_time	= ds1307_set_time,
	.read_alarm	= ds1337_read_alarm,
	.set_alarm	= ds1337_set_alarm,
	.alarm_irq_enable = ds1307_alarm_irq_enable,
};

/*----------------------------------------------------------------------*/

/*
 * Alarm support for mcp794xx devices.
 */

#define MCP794XX_REG_WEEKDAY		0x3
#define MCP794XX_REG_WEEKDAY_WDAY_MASK	0x7
#define MCP794XX_REG_CONTROL		0x07
#	define MCP794XX_BIT_ALM0_EN	0x10
#	define MCP794XX_BIT_ALM1_EN	0x20
#define MCP794XX_REG_ALARM0_BASE	0x0a
#define MCP794XX_REG_ALARM0_CTRL	0x0d
#define MCP794XX_REG_ALARM1_BASE	0x11
#define MCP794XX_REG_ALARM1_CTRL	0x14
#	define MCP794XX_BIT_ALMX_IF	(1 << 3)
#	define MCP794XX_BIT_ALMX_C0	(1 << 4)
#	define MCP794XX_BIT_ALMX_C1	(1 << 5)
#	define MCP794XX_BIT_ALMX_C2	(1 << 6)
#	define MCP794XX_BIT_ALMX_POL	(1 << 7)
#	define MCP794XX_MSK_ALMX_MATCH	(MCP794XX_BIT_ALMX_C0 | \
					 MCP794XX_BIT_ALMX_C1 | \
					 MCP794XX_BIT_ALMX_C2)

static irqreturn_t mcp794xx_irq(int irq, void *dev_id)
{
	struct ds1307           *ds1307 = dev_id;
	struct mutex            *lock = &ds1307->rtc->ops_lock;
	int reg, ret;

	mutex_lock(lock);

	/* Check and clear alarm 0 interrupt flag. */
	ret = regmap_read(ds1307->regmap, MCP794XX_REG_ALARM0_CTRL, &reg);
	if (ret)
		goto out;
	if (!(reg & MCP794XX_BIT_ALMX_IF))
		goto out;
	reg &= ~MCP794XX_BIT_ALMX_IF;
	ret = regmap_write(ds1307->regmap, MCP794XX_REG_ALARM0_CTRL, reg);
	if (ret)
		goto out;

	/* Disable alarm 0. */
	ret = regmap_read(ds1307->regmap, MCP794XX_REG_CONTROL, &reg);
	if (ret)
		goto out;
	reg &= ~MCP794XX_BIT_ALM0_EN;
	ret = regmap_write(ds1307->regmap, MCP794XX_REG_CONTROL, reg);
	if (ret)
		goto out;

	rtc_update_irq(ds1307->rtc, 1, RTC_AF | RTC_IRQF);

out:
	mutex_unlock(lock);

	return IRQ_HANDLED;
}

static int mcp794xx_read_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct ds1307 *ds1307 = dev_get_drvdata(dev);
	u8 *regs = ds1307->regs;
	int ret;

	if (!test_bit(HAS_ALARM, &ds1307->flags))
		return -EINVAL;

	/* Read control and alarm 0 registers. */
	ret = regmap_bulk_read(ds1307->regmap, MCP794XX_REG_CONTROL, regs, 10);
	if (ret)
		return ret;

	t->enabled = !!(regs[0] & MCP794XX_BIT_ALM0_EN);

	/* Report alarm 0 time assuming 24-hour and day-of-month modes. */
	t->time.tm_sec = bcd2bin(ds1307->regs[3] & 0x7f);
	t->time.tm_min = bcd2bin(ds1307->regs[4] & 0x7f);
	t->time.tm_hour = bcd2bin(ds1307->regs[5] & 0x3f);
	t->time.tm_wday = bcd2bin(ds1307->regs[6] & 0x7) - 1;
	t->time.tm_mday = bcd2bin(ds1307->regs[7] & 0x3f);
	t->time.tm_mon = bcd2bin(ds1307->regs[8] & 0x1f) - 1;
	t->time.tm_year = -1;
	t->time.tm_yday = -1;
	t->time.tm_isdst = -1;

	dev_dbg(dev, "%s, sec=%d min=%d hour=%d wday=%d mday=%d mon=%d "
		"enabled=%d polarity=%d irq=%d match=%d\n", __func__,
		t->time.tm_sec, t->time.tm_min, t->time.tm_hour,
		t->time.tm_wday, t->time.tm_mday, t->time.tm_mon, t->enabled,
		!!(ds1307->regs[6] & MCP794XX_BIT_ALMX_POL),
		!!(ds1307->regs[6] & MCP794XX_BIT_ALMX_IF),
		(ds1307->regs[6] & MCP794XX_MSK_ALMX_MATCH) >> 4);

	return 0;
}

static int mcp794xx_set_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct ds1307 *ds1307 = dev_get_drvdata(dev);
	unsigned char *regs = ds1307->regs;
	int ret;

	if (!test_bit(HAS_ALARM, &ds1307->flags))
		return -EINVAL;

	dev_dbg(dev, "%s, sec=%d min=%d hour=%d wday=%d mday=%d mon=%d "
		"enabled=%d pending=%d\n", __func__,
		t->time.tm_sec, t->time.tm_min, t->time.tm_hour,
		t->time.tm_wday, t->time.tm_mday, t->time.tm_mon,
		t->enabled, t->pending);

	/* Read control and alarm 0 registers. */
	ret = regmap_bulk_read(ds1307->regmap, MCP794XX_REG_CONTROL, regs, 10);
	if (ret)
		return ret;

	/* Set alarm 0, using 24-hour and day-of-month modes. */
	regs[3] = bin2bcd(t->time.tm_sec);
	regs[4] = bin2bcd(t->time.tm_min);
	regs[5] = bin2bcd(t->time.tm_hour);
	regs[6] = bin2bcd(t->time.tm_wday + 1);
	regs[7] = bin2bcd(t->time.tm_mday);
	regs[8] = bin2bcd(t->time.tm_mon + 1);

	/* Clear the alarm 0 interrupt flag. */
	regs[6] &= ~MCP794XX_BIT_ALMX_IF;
	/* Set alarm match: second, minute, hour, day, date, month. */
	regs[6] |= MCP794XX_MSK_ALMX_MATCH;
	/* Disable interrupt. We will not enable until completely programmed */
	regs[0] &= ~MCP794XX_BIT_ALM0_EN;

	ret = regmap_bulk_write(ds1307->regmap, MCP794XX_REG_CONTROL, regs, 10);
	if (ret)
		return ret;

	if (!t->enabled)
		return 0;
	regs[0] |= MCP794XX_BIT_ALM0_EN;
	return regmap_write(ds1307->regmap, MCP794XX_REG_CONTROL, regs[0]);
}

static int mcp794xx_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct ds1307 *ds1307 = dev_get_drvdata(dev);
	int reg, ret;

	if (!test_bit(HAS_ALARM, &ds1307->flags))
		return -EINVAL;

	ret = regmap_read(ds1307->regmap, MCP794XX_REG_CONTROL, &reg);
	if (ret)
		return ret;

	if (enabled)
		reg |= MCP794XX_BIT_ALM0_EN;
	else
		reg &= ~MCP794XX_BIT_ALM0_EN;

	return regmap_write(ds1307->regmap, MCP794XX_REG_CONTROL, reg);
}

static const struct rtc_class_ops mcp794xx_rtc_ops = {
	.read_time	= ds1307_get_time,
	.set_time	= ds1307_set_time,
	.read_alarm	= mcp794xx_read_alarm,
	.set_alarm	= mcp794xx_set_alarm,
	.alarm_irq_enable = mcp794xx_alarm_irq_enable,
};

/*----------------------------------------------------------------------*/

static ssize_t
ds1307_nvram_read(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct ds1307		*ds1307;
	int			result;

	ds1307 = dev_get_drvdata(kobj_to_dev(kobj));

	result = regmap_bulk_read(ds1307->regmap, ds1307->nvram_offset + off,
				  buf, count);
	if (result)
		dev_err(ds1307->dev, "%s error %d\n", "nvram read", result);
	return result;
}

static ssize_t
ds1307_nvram_write(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct ds1307		*ds1307;
	int			result;

	ds1307 = dev_get_drvdata(kobj_to_dev(kobj));

	result = regmap_bulk_write(ds1307->regmap, ds1307->nvram_offset + off,
				   buf, count);
	if (result) {
		dev_err(ds1307->dev, "%s error %d\n", "nvram write", result);
		return result;
	}
	return count;
}


/*----------------------------------------------------------------------*/

static u8 do_trickle_setup_ds1339(struct ds1307 *ds1307,
				  uint32_t ohms, bool diode)
{
	u8 setup = (diode) ? DS1307_TRICKLE_CHARGER_DIODE :
		DS1307_TRICKLE_CHARGER_NO_DIODE;

	switch (ohms) {
	case 250:
		setup |= DS1307_TRICKLE_CHARGER_250_OHM;
		break;
	case 2000:
		setup |= DS1307_TRICKLE_CHARGER_2K_OHM;
		break;
	case 4000:
		setup |= DS1307_TRICKLE_CHARGER_4K_OHM;
		break;
	default:
		dev_warn(ds1307->dev,
			 "Unsupported ohm value %u in dt\n", ohms);
		return 0;
	}
	return setup;
}

static void ds1307_trickle_init(struct ds1307 *ds1307,
				struct chip_desc *chip)
{
	uint32_t ohms = 0;
	bool diode = true;

	if (!chip->do_trickle_setup)
		goto out;
	if (device_property_read_u32(ds1307->dev, "trickle-resistor-ohms",
				     &ohms))
		goto out;
	if (device_property_read_bool(ds1307->dev, "trickle-diode-disable"))
		diode = false;
	chip->trickle_charger_setup = chip->do_trickle_setup(ds1307,
							     ohms, diode);
out:
	return;
}

/*----------------------------------------------------------------------*/

#ifdef CONFIG_RTC_DRV_DS1307_HWMON

/*
 * Temperature sensor support for ds3231 devices.
 */

#define DS3231_REG_TEMPERATURE	0x11

/*
 * A user-initiated temperature conversion is not started by this function,
 * so the temperature is updated once every 64 seconds.
 */
static int ds3231_hwmon_read_temp(struct device *dev, s32 *mC)
{
	struct ds1307 *ds1307 = dev_get_drvdata(dev);
	u8 temp_buf[2];
	s16 temp;
	int ret;

	ret = regmap_bulk_read(ds1307->regmap, DS3231_REG_TEMPERATURE,
			       temp_buf, sizeof(temp_buf));
	if (ret)
		return ret;
	/*
	 * Temperature is represented as a 10-bit code with a resolution of
	 * 0.25 degree celsius and encoded in two's complement format.
	 */
	temp = (temp_buf[0] << 8) | temp_buf[1];
	temp >>= 6;
	*mC = temp * 250;

	return 0;
}

static ssize_t ds3231_hwmon_show_temp(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;
	s32 temp;

	ret = ds3231_hwmon_read_temp(dev, &temp);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", temp);
}
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, ds3231_hwmon_show_temp,
			NULL, 0);

static struct attribute *ds3231_hwmon_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ds3231_hwmon);

static void ds1307_hwmon_register(struct ds1307 *ds1307)
{
	struct device *dev;

	if (ds1307->type != ds_3231)
		return;

	dev = devm_hwmon_device_register_with_groups(ds1307->dev, ds1307->name,
						ds1307, ds3231_hwmon_groups);
	if (IS_ERR(dev)) {
		dev_warn(ds1307->dev, "unable to register hwmon device %ld\n",
			 PTR_ERR(dev));
	}
}

#else

static void ds1307_hwmon_register(struct ds1307 *ds1307)
{
}

#endif /* CONFIG_RTC_DRV_DS1307_HWMON */

/*----------------------------------------------------------------------*/

/*
 * Square-wave output support for DS3231
 * Datasheet: https://datasheets.maximintegrated.com/en/ds/DS3231.pdf
 */
#ifdef CONFIG_COMMON_CLK

enum {
	DS3231_CLK_SQW = 0,
	DS3231_CLK_32KHZ,
};

#define clk_sqw_to_ds1307(clk)	\
	container_of(clk, struct ds1307, clks[DS3231_CLK_SQW])
#define clk_32khz_to_ds1307(clk)	\
	container_of(clk, struct ds1307, clks[DS3231_CLK_32KHZ])

static int ds3231_clk_sqw_rates[] = {
	1,
	1024,
	4096,
	8192,
};

static int ds1337_write_control(struct ds1307 *ds1307, u8 mask, u8 value)
{
	struct mutex *lock = &ds1307->rtc->ops_lock;
	int control;
	int ret;

	mutex_lock(lock);

	ret = regmap_read(ds1307->regmap, DS1337_REG_CONTROL, &control);
	if (ret)
		goto out;

	control &= ~mask;
	control |= value;

	ret = regmap_write(ds1307->regmap, DS1337_REG_CONTROL, control);
out:
	mutex_unlock(lock);

	return ret;
}

static unsigned long ds3231_clk_sqw_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct ds1307 *ds1307 = clk_sqw_to_ds1307(hw);
	int control, ret;
	int rate_sel = 0;

	ret = regmap_read(ds1307->regmap, DS1337_REG_CONTROL, &control);
	if (ret)
		return ret;
	if (control & DS1337_BIT_RS1)
		rate_sel += 1;
	if (control & DS1337_BIT_RS2)
		rate_sel += 2;

	return ds3231_clk_sqw_rates[rate_sel];
}

static long ds3231_clk_sqw_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *prate)
{
	int i;

	for (i = ARRAY_SIZE(ds3231_clk_sqw_rates) - 1; i >= 0; i--) {
		if (ds3231_clk_sqw_rates[i] <= rate)
			return ds3231_clk_sqw_rates[i];
	}

	return 0;
}

static int ds3231_clk_sqw_set_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long parent_rate)
{
	struct ds1307 *ds1307 = clk_sqw_to_ds1307(hw);
	int control = 0;
	int rate_sel;

	for (rate_sel = 0; rate_sel < ARRAY_SIZE(ds3231_clk_sqw_rates);
			rate_sel++) {
		if (ds3231_clk_sqw_rates[rate_sel] == rate)
			break;
	}

	if (rate_sel == ARRAY_SIZE(ds3231_clk_sqw_rates))
		return -EINVAL;

	if (rate_sel & 1)
		control |= DS1337_BIT_RS1;
	if (rate_sel & 2)
		control |= DS1337_BIT_RS2;

	return ds1337_write_control(ds1307, DS1337_BIT_RS1 | DS1337_BIT_RS2,
				control);
}

static int ds3231_clk_sqw_prepare(struct clk_hw *hw)
{
	struct ds1307 *ds1307 = clk_sqw_to_ds1307(hw);

	return ds1337_write_control(ds1307, DS1337_BIT_INTCN, 0);
}

static void ds3231_clk_sqw_unprepare(struct clk_hw *hw)
{
	struct ds1307 *ds1307 = clk_sqw_to_ds1307(hw);

	ds1337_write_control(ds1307, DS1337_BIT_INTCN, DS1337_BIT_INTCN);
}

static int ds3231_clk_sqw_is_prepared(struct clk_hw *hw)
{
	struct ds1307 *ds1307 = clk_sqw_to_ds1307(hw);
	int control, ret;

	ret = regmap_read(ds1307->regmap, DS1337_REG_CONTROL, &control);
	if (ret)
		return ret;

	return !(control & DS1337_BIT_INTCN);
}

static const struct clk_ops ds3231_clk_sqw_ops = {
	.prepare = ds3231_clk_sqw_prepare,
	.unprepare = ds3231_clk_sqw_unprepare,
	.is_prepared = ds3231_clk_sqw_is_prepared,
	.recalc_rate = ds3231_clk_sqw_recalc_rate,
	.round_rate = ds3231_clk_sqw_round_rate,
	.set_rate = ds3231_clk_sqw_set_rate,
};

static unsigned long ds3231_clk_32khz_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	return 32768;
}

static int ds3231_clk_32khz_control(struct ds1307 *ds1307, bool enable)
{
	struct mutex *lock = &ds1307->rtc->ops_lock;
	int status;
	int ret;

	mutex_lock(lock);

	ret = regmap_read(ds1307->regmap, DS1337_REG_STATUS, &status);
	if (ret)
		goto out;

	if (enable)
		status |= DS3231_BIT_EN32KHZ;
	else
		status &= ~DS3231_BIT_EN32KHZ;

	ret = regmap_write(ds1307->regmap, DS1337_REG_STATUS, status);
out:
	mutex_unlock(lock);

	return ret;
}

static int ds3231_clk_32khz_prepare(struct clk_hw *hw)
{
	struct ds1307 *ds1307 = clk_32khz_to_ds1307(hw);

	return ds3231_clk_32khz_control(ds1307, true);
}

static void ds3231_clk_32khz_unprepare(struct clk_hw *hw)
{
	struct ds1307 *ds1307 = clk_32khz_to_ds1307(hw);

	ds3231_clk_32khz_control(ds1307, false);
}

static int ds3231_clk_32khz_is_prepared(struct clk_hw *hw)
{
	struct ds1307 *ds1307 = clk_32khz_to_ds1307(hw);
	int status, ret;

	ret = regmap_read(ds1307->regmap, DS1337_REG_STATUS, &status);
	if (ret)
		return ret;

	return !!(status & DS3231_BIT_EN32KHZ);
}

static const struct clk_ops ds3231_clk_32khz_ops = {
	.prepare = ds3231_clk_32khz_prepare,
	.unprepare = ds3231_clk_32khz_unprepare,
	.is_prepared = ds3231_clk_32khz_is_prepared,
	.recalc_rate = ds3231_clk_32khz_recalc_rate,
};

static struct clk_init_data ds3231_clks_init[] = {
	[DS3231_CLK_SQW] = {
		.name = "ds3231_clk_sqw",
		.ops = &ds3231_clk_sqw_ops,
	},
	[DS3231_CLK_32KHZ] = {
		.name = "ds3231_clk_32khz",
		.ops = &ds3231_clk_32khz_ops,
	},
};

static int ds3231_clks_register(struct ds1307 *ds1307)
{
	struct device_node *node = ds1307->dev->of_node;
	struct clk_onecell_data	*onecell;
	int i;

	onecell = devm_kzalloc(ds1307->dev, sizeof(*onecell), GFP_KERNEL);
	if (!onecell)
		return -ENOMEM;

	onecell->clk_num = ARRAY_SIZE(ds3231_clks_init);
	onecell->clks = devm_kcalloc(ds1307->dev, onecell->clk_num,
				     sizeof(onecell->clks[0]), GFP_KERNEL);
	if (!onecell->clks)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(ds3231_clks_init); i++) {
		struct clk_init_data init = ds3231_clks_init[i];

		/*
		 * Interrupt signal due to alarm conditions and square-wave
		 * output share same pin, so don't initialize both.
		 */
		if (i == DS3231_CLK_SQW && test_bit(HAS_ALARM, &ds1307->flags))
			continue;

		/* optional override of the clockname */
		of_property_read_string_index(node, "clock-output-names", i,
						&init.name);
		ds1307->clks[i].init = &init;

		onecell->clks[i] = devm_clk_register(ds1307->dev,
						     &ds1307->clks[i]);
		if (IS_ERR(onecell->clks[i]))
			return PTR_ERR(onecell->clks[i]);
	}

	if (!node)
		return 0;

	of_clk_add_provider(node, of_clk_src_onecell_get, onecell);

	return 0;
}

static void ds1307_clks_register(struct ds1307 *ds1307)
{
	int ret;

	if (ds1307->type != ds_3231)
		return;

	ret = ds3231_clks_register(ds1307);
	if (ret) {
		dev_warn(ds1307->dev, "unable to register clock device %d\n",
			 ret);
	}
}

#else

static void ds1307_clks_register(struct ds1307 *ds1307)
{
}

#endif /* CONFIG_COMMON_CLK */

static const struct regmap_config regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x12,
};

static int ds1307_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ds1307		*ds1307;
	int			err = -ENODEV;
	int			tmp, wday;
	struct chip_desc	*chip;
	bool			want_irq = false;
	bool			ds1307_can_wakeup_device = false;
	unsigned char		*buf;
	struct ds1307_platform_data *pdata = dev_get_platdata(&client->dev);
	struct rtc_time		tm;
	unsigned long		timestamp;

	irq_handler_t	irq_handler = ds1307_irq;

	static const int	bbsqi_bitpos[] = {
		[ds_1337] = 0,
		[ds_1339] = DS1339_BIT_BBSQI,
		[ds_3231] = DS3231_BIT_BBSQW,
	};
	const struct rtc_class_ops *rtc_ops = &ds13xx_rtc_ops;

	ds1307 = devm_kzalloc(&client->dev, sizeof(struct ds1307), GFP_KERNEL);
	if (!ds1307)
		return -ENOMEM;

	dev_set_drvdata(&client->dev, ds1307);
	ds1307->dev = &client->dev;
	ds1307->name = client->name;
	ds1307->irq = client->irq;

	ds1307->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(ds1307->regmap)) {
		dev_err(ds1307->dev, "regmap allocation failed\n");
		return PTR_ERR(ds1307->regmap);
	}

	i2c_set_clientdata(client, ds1307);

	if (client->dev.of_node) {
		ds1307->type = (enum ds_type)
			of_device_get_match_data(&client->dev);
		chip = &chips[ds1307->type];
	} else if (id) {
		chip = &chips[id->driver_data];
		ds1307->type = id->driver_data;
	} else {
		const struct acpi_device_id *acpi_id;

		acpi_id = acpi_match_device(ACPI_PTR(ds1307_acpi_ids),
					    ds1307->dev);
		if (!acpi_id)
			return -ENODEV;
		chip = &chips[acpi_id->driver_data];
		ds1307->type = acpi_id->driver_data;
	}

	if (!pdata)
		ds1307_trickle_init(ds1307, chip);
	else if (pdata->trickle_charger_setup)
		chip->trickle_charger_setup = pdata->trickle_charger_setup;

	if (chip->trickle_charger_setup && chip->trickle_charger_reg) {
		dev_dbg(ds1307->dev,
			"writing trickle charger info 0x%x to 0x%x\n",
		    DS13XX_TRICKLE_CHARGER_MAGIC | chip->trickle_charger_setup,
		    chip->trickle_charger_reg);
		regmap_write(ds1307->regmap, chip->trickle_charger_reg,
		    DS13XX_TRICKLE_CHARGER_MAGIC |
		    chip->trickle_charger_setup);
	}

	buf = ds1307->regs;

#ifdef CONFIG_OF
/*
 * For devices with no IRQ directly connected to the SoC, the RTC chip
 * can be forced as a wakeup source by stating that explicitly in
 * the device's .dts file using the "wakeup-source" boolean property.
 * If the "wakeup-source" property is set, don't request an IRQ.
 * This will guarantee the 'wakealarm' sysfs entry is available on the device,
 * if supported by the RTC.
 */
	if (of_property_read_bool(client->dev.of_node, "wakeup-source")) {
		ds1307_can_wakeup_device = true;
	}
	/* Intersil ISL12057 DT backward compatibility */
	if (of_property_read_bool(client->dev.of_node,
				  "isil,irq2-can-wakeup-machine")) {
		ds1307_can_wakeup_device = true;
	}
#endif

	switch (ds1307->type) {
	case ds_1337:
	case ds_1339:
	case ds_3231:
		/* get registers that the "rtc" read below won't read... */
		err = regmap_bulk_read(ds1307->regmap, DS1337_REG_CONTROL,
				       buf, 2);
		if (err) {
			dev_dbg(ds1307->dev, "read error %d\n", err);
			goto exit;
		}

		/* oscillator off?  turn it on, so clock can tick. */
		if (ds1307->regs[0] & DS1337_BIT_nEOSC)
			ds1307->regs[0] &= ~DS1337_BIT_nEOSC;

		/*
		 * Using IRQ or defined as wakeup-source?
		 * Disable the square wave and both alarms.
		 * For some variants, be sure alarms can trigger when we're
		 * running on Vbackup (BBSQI/BBSQW)
		 */
		if (chip->alarm && (ds1307->irq > 0 ||
				    ds1307_can_wakeup_device)) {
			ds1307->regs[0] |= DS1337_BIT_INTCN
					| bbsqi_bitpos[ds1307->type];
			ds1307->regs[0] &= ~(DS1337_BIT_A2IE | DS1337_BIT_A1IE);

			want_irq = true;
		}

		regmap_write(ds1307->regmap, DS1337_REG_CONTROL,
			     ds1307->regs[0]);

		/* oscillator fault?  clear flag, and warn */
		if (ds1307->regs[1] & DS1337_BIT_OSF) {
			regmap_write(ds1307->regmap, DS1337_REG_STATUS,
				     ds1307->regs[1] & ~DS1337_BIT_OSF);
			dev_warn(ds1307->dev, "SET TIME!\n");
		}
		break;

	case rx_8025:
		err = regmap_bulk_read(ds1307->regmap,
				       RX8025_REG_CTRL1 << 4 | 0x08, buf, 2);
		if (err) {
			dev_dbg(ds1307->dev, "read error %d\n", err);
			goto exit;
		}

		/* oscillator off?  turn it on, so clock can tick. */
		if (!(ds1307->regs[1] & RX8025_BIT_XST)) {
			ds1307->regs[1] |= RX8025_BIT_XST;
			regmap_write(ds1307->regmap,
				     RX8025_REG_CTRL2 << 4 | 0x08,
				     ds1307->regs[1]);
			dev_warn(ds1307->dev,
				 "oscillator stop detected - SET TIME!\n");
		}

		if (ds1307->regs[1] & RX8025_BIT_PON) {
			ds1307->regs[1] &= ~RX8025_BIT_PON;
			regmap_write(ds1307->regmap,
				     RX8025_REG_CTRL2 << 4 | 0x08,
				     ds1307->regs[1]);
			dev_warn(ds1307->dev, "power-on detected\n");
		}

		if (ds1307->regs[1] & RX8025_BIT_VDET) {
			ds1307->regs[1] &= ~RX8025_BIT_VDET;
			regmap_write(ds1307->regmap,
				     RX8025_REG_CTRL2 << 4 | 0x08,
				     ds1307->regs[1]);
			dev_warn(ds1307->dev, "voltage drop detected\n");
		}

		/* make sure we are running in 24hour mode */
		if (!(ds1307->regs[0] & RX8025_BIT_2412)) {
			u8 hour;

			/* switch to 24 hour mode */
			regmap_write(ds1307->regmap,
				     RX8025_REG_CTRL1 << 4 | 0x08,
				     ds1307->regs[0] | RX8025_BIT_2412);

			err = regmap_bulk_read(ds1307->regmap,
					       RX8025_REG_CTRL1 << 4 | 0x08,
					       buf, 2);
			if (err) {
				dev_dbg(ds1307->dev, "read error %d\n", err);
				goto exit;
			}

			/* correct hour */
			hour = bcd2bin(ds1307->regs[DS1307_REG_HOUR]);
			if (hour == 12)
				hour = 0;
			if (ds1307->regs[DS1307_REG_HOUR] & DS1307_BIT_PM)
				hour += 12;

			regmap_write(ds1307->regmap,
				     DS1307_REG_HOUR << 4 | 0x08, hour);
		}
		break;
	case ds_1388:
		ds1307->offset = 1; /* Seconds starts at 1 */
		break;
	case mcp794xx:
		rtc_ops = &mcp794xx_rtc_ops;
		if (chip->alarm && (ds1307->irq > 0 ||
				    ds1307_can_wakeup_device)) {
			irq_handler = mcp794xx_irq;
			want_irq = true;
		}
		break;
	default:
		break;
	}

read_rtc:
	/* read RTC registers */
	err = regmap_bulk_read(ds1307->regmap, ds1307->offset, buf, 8);
	if (err) {
		dev_dbg(ds1307->dev, "read error %d\n", err);
		goto exit;
	}

	/*
	 * minimal sanity checking; some chips (like DS1340) don't
	 * specify the extra bits as must-be-zero, but there are
	 * still a few values that are clearly out-of-range.
	 */
	tmp = ds1307->regs[DS1307_REG_SECS];
	switch (ds1307->type) {
	case ds_1307:
	case m41t0:
	case m41t00:
		/* clock halted?  turn it on, so clock can tick. */
		if (tmp & DS1307_BIT_CH) {
			regmap_write(ds1307->regmap, DS1307_REG_SECS, 0);
			dev_warn(ds1307->dev, "SET TIME!\n");
			goto read_rtc;
		}
		break;
	case ds_1338:
		/* clock halted?  turn it on, so clock can tick. */
		if (tmp & DS1307_BIT_CH)
			regmap_write(ds1307->regmap, DS1307_REG_SECS, 0);

		/* oscillator fault?  clear flag, and warn */
		if (ds1307->regs[DS1307_REG_CONTROL] & DS1338_BIT_OSF) {
			regmap_write(ds1307->regmap, DS1307_REG_CONTROL,
					ds1307->regs[DS1307_REG_CONTROL] &
					~DS1338_BIT_OSF);
			dev_warn(ds1307->dev, "SET TIME!\n");
			goto read_rtc;
		}
		break;
	case ds_1340:
		/* clock halted?  turn it on, so clock can tick. */
		if (tmp & DS1340_BIT_nEOSC)
			regmap_write(ds1307->regmap, DS1307_REG_SECS, 0);

		err = regmap_read(ds1307->regmap, DS1340_REG_FLAG, &tmp);
		if (err) {
			dev_dbg(ds1307->dev, "read error %d\n", err);
			goto exit;
		}

		/* oscillator fault?  clear flag, and warn */
		if (tmp & DS1340_BIT_OSF) {
			regmap_write(ds1307->regmap, DS1340_REG_FLAG, 0);
			dev_warn(ds1307->dev, "SET TIME!\n");
		}
		break;
	case mcp794xx:
		/* make sure that the backup battery is enabled */
		if (!(ds1307->regs[DS1307_REG_WDAY] & MCP794XX_BIT_VBATEN)) {
			regmap_write(ds1307->regmap, DS1307_REG_WDAY,
				     ds1307->regs[DS1307_REG_WDAY] |
				     MCP794XX_BIT_VBATEN);
		}

		/* clock halted?  turn it on, so clock can tick. */
		if (!(tmp & MCP794XX_BIT_ST)) {
			regmap_write(ds1307->regmap, DS1307_REG_SECS,
				     MCP794XX_BIT_ST);
			dev_warn(ds1307->dev, "SET TIME!\n");
			goto read_rtc;
		}

		break;
	default:
		break;
	}

	tmp = ds1307->regs[DS1307_REG_HOUR];
	switch (ds1307->type) {
	case ds_1340:
	case m41t0:
	case m41t00:
		/*
		 * NOTE: ignores century bits; fix before deploying
		 * systems that will run through year 2100.
		 */
		break;
	case rx_8025:
		break;
	default:
		if (!(tmp & DS1307_BIT_12HR))
			break;

		/*
		 * Be sure we're in 24 hour mode.  Multi-master systems
		 * take note...
		 */
		tmp = bcd2bin(tmp & 0x1f);
		if (tmp == 12)
			tmp = 0;
		if (ds1307->regs[DS1307_REG_HOUR] & DS1307_BIT_PM)
			tmp += 12;
		regmap_write(ds1307->regmap, ds1307->offset + DS1307_REG_HOUR,
			     bin2bcd(tmp));
	}

	/*
	 * Some IPs have weekday reset value = 0x1 which might not correct
	 * hence compute the wday using the current date/month/year values
	 */
	ds1307_get_time(ds1307->dev, &tm);
	wday = tm.tm_wday;
	timestamp = rtc_tm_to_time64(&tm);
	rtc_time64_to_tm(timestamp, &tm);

	/*
	 * Check if reset wday is different from the computed wday
	 * If different then set the wday which we computed using
	 * timestamp
	 */
	if (wday != tm.tm_wday) {
		regmap_read(ds1307->regmap, MCP794XX_REG_WEEKDAY, &wday);
		wday = wday & ~MCP794XX_REG_WEEKDAY_WDAY_MASK;
		wday = wday | (tm.tm_wday + 1);
		regmap_write(ds1307->regmap, MCP794XX_REG_WEEKDAY, wday);
	}

	if (want_irq) {
		device_set_wakeup_capable(ds1307->dev, true);
		set_bit(HAS_ALARM, &ds1307->flags);
	}
	ds1307->rtc = devm_rtc_device_register(ds1307->dev, ds1307->name,
				rtc_ops, THIS_MODULE);
	if (IS_ERR(ds1307->rtc)) {
		return PTR_ERR(ds1307->rtc);
	}

	if (ds1307_can_wakeup_device && ds1307->irq <= 0) {
		/* Disable request for an IRQ */
		want_irq = false;
		dev_info(ds1307->dev,
			 "'wakeup-source' is set, request for an IRQ is disabled!\n");
		/* We cannot support UIE mode if we do not have an IRQ line */
		ds1307->rtc->uie_unsupported = 1;
	}

	if (want_irq) {
		err = devm_request_threaded_irq(ds1307->dev,
						ds1307->irq, NULL, irq_handler,
						IRQF_SHARED | IRQF_ONESHOT,
						ds1307->name, ds1307);
		if (err) {
			client->irq = 0;
			device_set_wakeup_capable(ds1307->dev, false);
			clear_bit(HAS_ALARM, &ds1307->flags);
			dev_err(ds1307->dev, "unable to request IRQ!\n");
		} else
			dev_dbg(ds1307->dev, "got IRQ %d\n", client->irq);
	}

	if (chip->nvram_size) {

		ds1307->nvram = devm_kzalloc(ds1307->dev,
					sizeof(struct bin_attribute),
					GFP_KERNEL);
		if (!ds1307->nvram) {
			dev_err(ds1307->dev,
				"cannot allocate memory for nvram sysfs\n");
		} else {

			ds1307->nvram->attr.name = "nvram";
			ds1307->nvram->attr.mode = S_IRUGO | S_IWUSR;

			sysfs_bin_attr_init(ds1307->nvram);

			ds1307->nvram->read = ds1307_nvram_read;
			ds1307->nvram->write = ds1307_nvram_write;
			ds1307->nvram->size = chip->nvram_size;
			ds1307->nvram_offset = chip->nvram_offset;

			err = sysfs_create_bin_file(&ds1307->dev->kobj,
						    ds1307->nvram);
			if (err) {
				dev_err(ds1307->dev,
					"unable to create sysfs file: %s\n",
					ds1307->nvram->attr.name);
			} else {
				set_bit(HAS_NVRAM, &ds1307->flags);
				dev_info(ds1307->dev, "%zu bytes nvram\n",
					 ds1307->nvram->size);
			}
		}
	}

	ds1307_hwmon_register(ds1307);
	ds1307_clks_register(ds1307);

	return 0;

exit:
	return err;
}

static int ds1307_remove(struct i2c_client *client)
{
	struct ds1307 *ds1307 = i2c_get_clientdata(client);

	if (test_and_clear_bit(HAS_NVRAM, &ds1307->flags))
		sysfs_remove_bin_file(&ds1307->dev->kobj, ds1307->nvram);

	return 0;
}

static struct i2c_driver ds1307_driver = {
	.driver = {
		.name	= "rtc-ds1307",
		.of_match_table = of_match_ptr(ds1307_of_match),
		.acpi_match_table = ACPI_PTR(ds1307_acpi_ids),
	},
	.probe		= ds1307_probe,
	.remove		= ds1307_remove,
	.id_table	= ds1307_id,
};

module_i2c_driver(ds1307_driver);

MODULE_DESCRIPTION("RTC driver for DS1307 and similar chips");
MODULE_LICENSE("GPL");
