/*
 * BQ27xxx battery driver
 *
 * Copyright (C) 2008 Rodolfo Giometti <giometti@linux.it>
 * Copyright (C) 2008 Eurotech S.p.A. <info@eurotech.it>
 * Copyright (C) 2010-2011 Lars-Peter Clausen <lars@metafoo.de>
 * Copyright (C) 2011 Pali Rohár <pali.rohar@gmail.com>
 *
 * Based on a previous work by Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Datasheets:
 * http://www.ti.com/product/bq27000
 * http://www.ti.com/product/bq27200
 * http://www.ti.com/product/bq27010
 * http://www.ti.com/product/bq27210
 * http://www.ti.com/product/bq27500
 * http://www.ti.com/product/bq27510-g3
 * http://www.ti.com/product/bq27520-g4
 * http://www.ti.com/product/bq27530-g1
 * http://www.ti.com/product/bq27531-g1
 * http://www.ti.com/product/bq27541-g1
 * http://www.ti.com/product/bq27542-g1
 * http://www.ti.com/product/bq27546-g1
 * http://www.ti.com/product/bq27742-g1
 * http://www.ti.com/product/bq27545-g1
 * http://www.ti.com/product/bq27421-g1
 * http://www.ti.com/product/bq27425-g1
 * http://www.ti.com/product/bq27411-g1
 * http://www.ti.com/product/bq27621-g1
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <asm/unaligned.h>

#include <linux/power/bq27xxx_battery.h>

#define DRIVER_VERSION		"1.2.0"

#define BQ27XXX_MANUFACTURER	"Texas Instruments"

/* BQ27XXX Flags */
#define BQ27XXX_FLAG_DSC	BIT(0)
#define BQ27XXX_FLAG_SOCF	BIT(1) /* State-of-Charge threshold final */
#define BQ27XXX_FLAG_SOC1	BIT(2) /* State-of-Charge threshold 1 */
#define BQ27XXX_FLAG_FC		BIT(9)
#define BQ27XXX_FLAG_OTD	BIT(14)
#define BQ27XXX_FLAG_OTC	BIT(15)
#define BQ27XXX_FLAG_UT		BIT(14)
#define BQ27XXX_FLAG_OT		BIT(15)

/* BQ27000 has different layout for Flags register */
#define BQ27000_FLAG_EDVF	BIT(0) /* Final End-of-Discharge-Voltage flag */
#define BQ27000_FLAG_EDV1	BIT(1) /* First End-of-Discharge-Voltage flag */
#define BQ27000_FLAG_CI		BIT(4) /* Capacity Inaccurate flag */
#define BQ27000_FLAG_FC		BIT(5)
#define BQ27000_FLAG_CHGS	BIT(7) /* Charge state flag */

#define BQ27XXX_RS			(20) /* Resistor sense mOhm */
#define BQ27XXX_POWER_CONSTANT		(29200) /* 29.2 µV^2 * 1000 */
#define BQ27XXX_CURRENT_CONSTANT	(3570) /* 3.57 µV * 1000 */

struct bq27xxx_device_info;
struct bq27xxx_access_methods {
	int (*read)(struct bq27xxx_device_info *di, u8 reg, bool single);
};

#define INVALID_REG_ADDR	0xff

/*
 * bq27xxx_reg_index - Register names
 *
 * These are indexes into a device's register mapping array.
 */
enum bq27xxx_reg_index {
	BQ27XXX_REG_CTRL = 0,	/* Control */
	BQ27XXX_REG_TEMP,	/* Temperature */
	BQ27XXX_REG_INT_TEMP,	/* Internal Temperature */
	BQ27XXX_REG_VOLT,	/* Voltage */
	BQ27XXX_REG_AI,		/* Average Current */
	BQ27XXX_REG_FLAGS,	/* Flags */
	BQ27XXX_REG_TTE,	/* Time-to-Empty */
	BQ27XXX_REG_TTF,	/* Time-to-Full */
	BQ27XXX_REG_TTES,	/* Time-to-Empty Standby */
	BQ27XXX_REG_TTECP,	/* Time-to-Empty at Constant Power */
	BQ27XXX_REG_NAC,	/* Nominal Available Capacity */
	BQ27XXX_REG_FCC,	/* Full Charge Capacity */
	BQ27XXX_REG_CYCT,	/* Cycle Count */
	BQ27XXX_REG_AE,		/* Available Energy */
	BQ27XXX_REG_SOC,	/* State-of-Charge */
	BQ27XXX_REG_DCAP,	/* Design Capacity */
	BQ27XXX_REG_AP,		/* Average Power */
};

struct bq27xxx_reg_cache {
	int temperature;
	int time_to_empty;
	int time_to_empty_avg;
	int time_to_full;
	int charge_full;
	int cycle_count;
	int capacity;
	int energy;
	int flags;
	int power_avg;
	int health;
};

struct bq27xxx_device_info {
	struct device		*dev;
	int			id;
	enum bq27xxx_chip	chip;

	struct bq27xxx_reg_cache cache;
	int charge_design_full;

	unsigned long last_update;
	struct delayed_work work;

	struct power_supply	*bat;

	struct bq27xxx_access_methods bus;

	struct mutex lock;

	u8 *regs;
};

/* Register mappings */
static u8 bq27000_regs[] = {
	0x00,	/* CONTROL	*/
	0x06,	/* TEMP		*/
	INVALID_REG_ADDR,	/* INT TEMP - NA*/
	0x08,	/* VOLT		*/
	0x14,	/* AVG CURR	*/
	0x0a,	/* FLAGS	*/
	0x16,	/* TTE		*/
	0x18,	/* TTF		*/
	0x1c,	/* TTES		*/
	0x26,	/* TTECP	*/
	0x0c,	/* NAC		*/
	0x12,	/* LMD(FCC)	*/
	0x2a,	/* CYCT		*/
	0x22,	/* AE		*/
	0x0b,	/* SOC(RSOC)	*/
	0x76,	/* DCAP(ILMD)	*/
	0x24,	/* AP		*/
};

static u8 bq27010_regs[] = {
	0x00,	/* CONTROL	*/
	0x06,	/* TEMP		*/
	INVALID_REG_ADDR,	/* INT TEMP - NA*/
	0x08,	/* VOLT		*/
	0x14,	/* AVG CURR	*/
	0x0a,	/* FLAGS	*/
	0x16,	/* TTE		*/
	0x18,	/* TTF		*/
	0x1c,	/* TTES		*/
	0x26,	/* TTECP	*/
	0x0c,	/* NAC		*/
	0x12,	/* LMD(FCC)	*/
	0x2a,	/* CYCT		*/
	INVALID_REG_ADDR,	/* AE - NA	*/
	0x0b,	/* SOC(RSOC)	*/
	0x76,	/* DCAP(ILMD)	*/
	INVALID_REG_ADDR,	/* AP - NA	*/
};

static u8 bq27500_regs[] = {
	0x00,	/* CONTROL	*/
	0x06,	/* TEMP		*/
	0x28,	/* INT TEMP	*/
	0x08,	/* VOLT		*/
	0x14,	/* AVG CURR	*/
	0x0a,	/* FLAGS	*/
	0x16,	/* TTE		*/
	INVALID_REG_ADDR,	/* TTF - NA	*/
	0x1a,	/* TTES		*/
	INVALID_REG_ADDR,	/* TTECP - NA	*/
	0x0c,	/* NAC		*/
	0x12,	/* LMD(FCC)	*/
	0x1e,	/* CYCT		*/
	INVALID_REG_ADDR,	/* AE - NA	*/
	0x20,	/* SOC(RSOC)	*/
	0x2e,	/* DCAP(ILMD)	*/
	INVALID_REG_ADDR,	/* AP - NA	*/
};

static u8 bq27530_regs[] = {
	0x00,	/* CONTROL	*/
	0x06,	/* TEMP		*/
	0x32,	/* INT TEMP	*/
	0x08,	/* VOLT		*/
	0x14,	/* AVG CURR	*/
	0x0a,	/* FLAGS	*/
	0x16,	/* TTE		*/
	INVALID_REG_ADDR,	/* TTF - NA	*/
	INVALID_REG_ADDR,	/* TTES - NA	*/
	INVALID_REG_ADDR,	/* TTECP - NA	*/
	0x0c,	/* NAC		*/
	0x12,	/* LMD(FCC)	*/
	0x2a,	/* CYCT		*/
	INVALID_REG_ADDR,	/* AE - NA	*/
	0x2c,	/* SOC(RSOC)	*/
	INVALID_REG_ADDR,	/* DCAP - NA	*/
	0x24,	/* AP		*/
};

static u8 bq27541_regs[] = {
	0x00,	/* CONTROL	*/
	0x06,	/* TEMP		*/
	0x28,	/* INT TEMP	*/
	0x08,	/* VOLT		*/
	0x14,	/* AVG CURR	*/
	0x0a,	/* FLAGS	*/
	0x16,	/* TTE		*/
	INVALID_REG_ADDR,	/* TTF - NA	*/
	INVALID_REG_ADDR,	/* TTES - NA	*/
	INVALID_REG_ADDR,	/* TTECP - NA	*/
	0x0c,	/* NAC		*/
	0x12,	/* LMD(FCC)	*/
	0x2a,	/* CYCT		*/
	INVALID_REG_ADDR,	/* AE - NA	*/
	0x2c,	/* SOC(RSOC)	*/
	0x3c,	/* DCAP		*/
	0x76,	/* AP		*/
};

static u8 bq27545_regs[] = {
	0x00,	/* CONTROL	*/
	0x06,	/* TEMP		*/
	0x28,	/* INT TEMP	*/
	0x08,	/* VOLT		*/
	0x14,	/* AVG CURR	*/
	0x0a,	/* FLAGS	*/
	0x16,	/* TTE		*/
	INVALID_REG_ADDR,	/* TTF - NA	*/
	INVALID_REG_ADDR,	/* TTES - NA	*/
	INVALID_REG_ADDR,	/* TTECP - NA	*/
	0x0c,	/* NAC		*/
	0x12,	/* LMD(FCC)	*/
	0x2a,	/* CYCT		*/
	INVALID_REG_ADDR,	/* AE - NA	*/
	0x2c,	/* SOC(RSOC)	*/
	INVALID_REG_ADDR,	/* DCAP - NA */
	0x24,	/* AP		*/
};

static u8 bq27421_regs[] = {
	0x00,	/* CONTROL	*/
	0x02,	/* TEMP		*/
	0x1e,	/* INT TEMP	*/
	0x04,	/* VOLT		*/
	0x10,	/* AVG CURR	*/
	0x06,	/* FLAGS	*/
	INVALID_REG_ADDR,	/* TTE - NA	*/
	INVALID_REG_ADDR,	/* TTF - NA	*/
	INVALID_REG_ADDR,	/* TTES - NA	*/
	INVALID_REG_ADDR,	/* TTECP - NA	*/
	0x08,	/* NAC		*/
	0x0e,	/* FCC		*/
	INVALID_REG_ADDR,	/* CYCT - NA	*/
	INVALID_REG_ADDR,	/* AE - NA	*/
	0x1c,	/* SOC		*/
	0x3c,	/* DCAP		*/
	0x18,	/* AP		*/
};

static u8 *bq27xxx_regs[] __maybe_unused = {
	[BQ27000] = bq27000_regs,
	[BQ27010] = bq27010_regs,
	[BQ27500] = bq27500_regs,
	[BQ27530] = bq27530_regs,
	[BQ27541] = bq27541_regs,
	[BQ27545] = bq27545_regs,
	[BQ27421] = bq27421_regs,
};

static enum power_supply_property bq27000_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq27010_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq27500_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq27530_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq27541_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq27545_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_property bq27421_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

#define BQ27XXX_PROP(_id, _prop)		\
	[_id] = {				\
		.props = _prop,			\
		.size = ARRAY_SIZE(_prop),	\
	}

static struct {
	enum power_supply_property *props;
	size_t size;
} bq27xxx_battery_props[] = {
	BQ27XXX_PROP(BQ27000, bq27000_battery_props),
	BQ27XXX_PROP(BQ27010, bq27010_battery_props),
	BQ27XXX_PROP(BQ27500, bq27500_battery_props),
	BQ27XXX_PROP(BQ27530, bq27530_battery_props),
	BQ27XXX_PROP(BQ27541, bq27541_battery_props),
	BQ27XXX_PROP(BQ27545, bq27545_battery_props),
	BQ27XXX_PROP(BQ27421, bq27421_battery_props),
};

static unsigned int poll_interval = 360;
module_param(poll_interval, uint, 0644);
MODULE_PARM_DESC(poll_interval,
		 "battery poll interval in seconds - 0 disables polling");

/*
 * Common code for BQ27xxx devices
 */

static inline int bq27xxx_read(struct bq27xxx_device_info *di, int reg_index,
			       bool single)
{
	/* Reports EINVAL for invalid/missing registers */
	if (!di || di->regs[reg_index] == INVALID_REG_ADDR)
		return -EINVAL;

	return di->bus.read(di, di->regs[reg_index], single);
}

/*
 * Return the battery State-of-Charge
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_read_soc(struct bq27xxx_device_info *di)
{
	int soc;

	soc = bq27xxx_read(di, BQ27XXX_REG_SOC, false);

	if (soc < 0)
		dev_dbg(di->dev, "error reading State-of-Charge\n");

	return soc;
}

/*
 * Return a battery charge value in µAh
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_read_charge(struct bq27xxx_device_info *di, u8 reg)
{
	int charge;

	charge = bq27xxx_read(di, reg, false);
	if (charge < 0) {
		dev_dbg(di->dev, "error reading charge register %02x: %d\n",
			reg, charge);
		return charge;
	}

	if (di->chip == BQ27000 || di->chip == BQ27010)
		charge *= BQ27XXX_CURRENT_CONSTANT / BQ27XXX_RS;
	else
		charge *= 1000;

	return charge;
}

/*
 * Return the battery Nominal available capacity in µAh
 * Or < 0 if something fails.
 */
static inline int bq27xxx_battery_read_nac(struct bq27xxx_device_info *di)
{
	int flags;

	if (di->chip == BQ27000 || di->chip == BQ27010) {
		flags = bq27xxx_read(di, BQ27XXX_REG_FLAGS, true);
		if (flags >= 0 && (flags & BQ27000_FLAG_CI))
			return -ENODATA;
	}

	return bq27xxx_battery_read_charge(di, BQ27XXX_REG_NAC);
}

/*
 * Return the battery Full Charge Capacity in µAh
 * Or < 0 if something fails.
 */
static inline int bq27xxx_battery_read_fcc(struct bq27xxx_device_info *di)
{
	return bq27xxx_battery_read_charge(di, BQ27XXX_REG_FCC);
}

/*
 * Return the Design Capacity in µAh
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_read_dcap(struct bq27xxx_device_info *di)
{
	int dcap;

	dcap = bq27xxx_read(di, BQ27XXX_REG_DCAP, false);

	if (dcap < 0) {
		dev_dbg(di->dev, "error reading initial last measured discharge\n");
		return dcap;
	}

	if (di->chip == BQ27000 || di->chip == BQ27010)
		dcap *= BQ27XXX_CURRENT_CONSTANT / BQ27XXX_RS;
	else
		dcap *= 1000;

	return dcap;
}

/*
 * Return the battery Available energy in µWh
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_read_energy(struct bq27xxx_device_info *di)
{
	int ae;

	ae = bq27xxx_read(di, BQ27XXX_REG_AE, false);
	if (ae < 0) {
		dev_dbg(di->dev, "error reading available energy\n");
		return ae;
	}

	if (di->chip == BQ27000 || di->chip == BQ27010)
		ae *= BQ27XXX_POWER_CONSTANT / BQ27XXX_RS;
	else
		ae *= 1000;

	return ae;
}

/*
 * Return the battery temperature in tenths of degree Kelvin
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_read_temperature(struct bq27xxx_device_info *di)
{
	int temp;

	temp = bq27xxx_read(di, BQ27XXX_REG_TEMP, false);
	if (temp < 0) {
		dev_err(di->dev, "error reading temperature\n");
		return temp;
	}

	if (di->chip == BQ27000 || di->chip == BQ27010)
		temp = 5 * temp / 2;

	return temp;
}

/*
 * Return the battery Cycle count total
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_read_cyct(struct bq27xxx_device_info *di)
{
	int cyct;

	cyct = bq27xxx_read(di, BQ27XXX_REG_CYCT, false);
	if (cyct < 0)
		dev_err(di->dev, "error reading cycle count total\n");

	return cyct;
}

/*
 * Read a time register.
 * Return < 0 if something fails.
 */
static int bq27xxx_battery_read_time(struct bq27xxx_device_info *di, u8 reg)
{
	int tval;

	tval = bq27xxx_read(di, reg, false);
	if (tval < 0) {
		dev_dbg(di->dev, "error reading time register %02x: %d\n",
			reg, tval);
		return tval;
	}

	if (tval == 65535)
		return -ENODATA;

	return tval * 60;
}

/*
 * Read an average power register.
 * Return < 0 if something fails.
 */
static int bq27xxx_battery_read_pwr_avg(struct bq27xxx_device_info *di)
{
	int tval;

	tval = bq27xxx_read(di, BQ27XXX_REG_AP, false);
	if (tval < 0) {
		dev_err(di->dev, "error reading average power register  %02x: %d\n",
			BQ27XXX_REG_AP, tval);
		return tval;
	}

	if (di->chip == BQ27000 || di->chip == BQ27010)
		return (tval * BQ27XXX_POWER_CONSTANT) / BQ27XXX_RS;
	else
		return tval;
}

/*
 * Returns true if a battery over temperature condition is detected
 */
static bool bq27xxx_battery_overtemp(struct bq27xxx_device_info *di, u16 flags)
{
	if (di->chip == BQ27500 || di->chip == BQ27541 || di->chip == BQ27545)
		return flags & (BQ27XXX_FLAG_OTC | BQ27XXX_FLAG_OTD);
	if (di->chip == BQ27530 || di->chip == BQ27421)
		return flags & BQ27XXX_FLAG_OT;

	return false;
}

/*
 * Returns true if a battery under temperature condition is detected
 */
static bool bq27xxx_battery_undertemp(struct bq27xxx_device_info *di, u16 flags)
{
	if (di->chip == BQ27530 || di->chip == BQ27421)
		return flags & BQ27XXX_FLAG_UT;

	return false;
}

/*
 * Returns true if a low state of charge condition is detected
 */
static bool bq27xxx_battery_dead(struct bq27xxx_device_info *di, u16 flags)
{
	if (di->chip == BQ27000 || di->chip == BQ27010)
		return flags & (BQ27000_FLAG_EDV1 | BQ27000_FLAG_EDVF);
	else
		return flags & (BQ27XXX_FLAG_SOC1 | BQ27XXX_FLAG_SOCF);
}

/*
 * Read flag register.
 * Return < 0 if something fails.
 */
static int bq27xxx_battery_read_health(struct bq27xxx_device_info *di)
{
	int flags;

	flags = bq27xxx_read(di, BQ27XXX_REG_FLAGS, false);
	if (flags < 0) {
		dev_err(di->dev, "error reading flag register:%d\n", flags);
		return flags;
	}

	/* Unlikely but important to return first */
	if (unlikely(bq27xxx_battery_overtemp(di, flags)))
		return POWER_SUPPLY_HEALTH_OVERHEAT;
	if (unlikely(bq27xxx_battery_undertemp(di, flags)))
		return POWER_SUPPLY_HEALTH_COLD;
	if (unlikely(bq27xxx_battery_dead(di, flags)))
		return POWER_SUPPLY_HEALTH_DEAD;

	return POWER_SUPPLY_HEALTH_GOOD;
}

static void bq27xxx_battery_update(struct bq27xxx_device_info *di)
{
	struct bq27xxx_reg_cache cache = {0, };
	bool has_ci_flag = di->chip == BQ27000 || di->chip == BQ27010;
	bool has_singe_flag = di->chip == BQ27000 || di->chip == BQ27010;

	cache.flags = bq27xxx_read(di, BQ27XXX_REG_FLAGS, has_singe_flag);
	if ((cache.flags & 0xff) == 0xff)
		cache.flags = -1; /* read error */
	if (cache.flags >= 0) {
		cache.temperature = bq27xxx_battery_read_temperature(di);
		if (has_ci_flag && (cache.flags & BQ27000_FLAG_CI)) {
			dev_info(di->dev, "battery is not calibrated! ignoring capacity values\n");
			cache.capacity = -ENODATA;
			cache.energy = -ENODATA;
			cache.time_to_empty = -ENODATA;
			cache.time_to_empty_avg = -ENODATA;
			cache.time_to_full = -ENODATA;
			cache.charge_full = -ENODATA;
			cache.health = -ENODATA;
		} else {
			if (di->regs[BQ27XXX_REG_TTE] != INVALID_REG_ADDR)
				cache.time_to_empty = bq27xxx_battery_read_time(di, BQ27XXX_REG_TTE);
			if (di->regs[BQ27XXX_REG_TTECP] != INVALID_REG_ADDR)
				cache.time_to_empty_avg = bq27xxx_battery_read_time(di, BQ27XXX_REG_TTECP);
			if (di->regs[BQ27XXX_REG_TTF] != INVALID_REG_ADDR)
				cache.time_to_full = bq27xxx_battery_read_time(di, BQ27XXX_REG_TTF);
			cache.charge_full = bq27xxx_battery_read_fcc(di);
			cache.capacity = bq27xxx_battery_read_soc(di);
			if (di->regs[BQ27XXX_REG_AE] != INVALID_REG_ADDR)
				cache.energy = bq27xxx_battery_read_energy(di);
			cache.health = bq27xxx_battery_read_health(di);
		}
		if (di->regs[BQ27XXX_REG_CYCT] != INVALID_REG_ADDR)
			cache.cycle_count = bq27xxx_battery_read_cyct(di);
		if (di->regs[BQ27XXX_REG_AP] != INVALID_REG_ADDR)
			cache.power_avg = bq27xxx_battery_read_pwr_avg(di);

		/* We only have to read charge design full once */
		if (di->charge_design_full <= 0)
			di->charge_design_full = bq27xxx_battery_read_dcap(di);
	}

	if (di->cache.capacity != cache.capacity)
		power_supply_changed(di->bat);

	if (memcmp(&di->cache, &cache, sizeof(cache)) != 0)
		di->cache = cache;

	di->last_update = jiffies;
}

static void bq27xxx_battery_poll(struct work_struct *work)
{
	struct bq27xxx_device_info *di =
			container_of(work, struct bq27xxx_device_info,
				     work.work);

	bq27xxx_battery_update(di);

	if (poll_interval > 0) {
		/* The timer does not have to be accurate. */
		set_timer_slack(&di->work.timer, poll_interval * HZ / 4);
		schedule_delayed_work(&di->work, poll_interval * HZ);
	}
}

/*
 * Return the battery average current in µA
 * Note that current can be negative signed as well
 * Or 0 if something fails.
 */
static int bq27xxx_battery_current(struct bq27xxx_device_info *di,
				   union power_supply_propval *val)
{
	int curr;
	int flags;

	curr = bq27xxx_read(di, BQ27XXX_REG_AI, false);
	if (curr < 0) {
		dev_err(di->dev, "error reading current\n");
		return curr;
	}

	if (di->chip == BQ27000 || di->chip == BQ27010) {
		flags = bq27xxx_read(di, BQ27XXX_REG_FLAGS, false);
		if (flags & BQ27000_FLAG_CHGS) {
			dev_dbg(di->dev, "negative current!\n");
			curr = -curr;
		}

		val->intval = curr * BQ27XXX_CURRENT_CONSTANT / BQ27XXX_RS;
	} else {
		/* Other gauges return signed value */
		val->intval = (int)((s16)curr) * 1000;
	}

	return 0;
}

static int bq27xxx_battery_status(struct bq27xxx_device_info *di,
				  union power_supply_propval *val)
{
	int status;

	if (di->chip == BQ27000 || di->chip == BQ27010) {
		if (di->cache.flags & BQ27000_FLAG_FC)
			status = POWER_SUPPLY_STATUS_FULL;
		else if (di->cache.flags & BQ27000_FLAG_CHGS)
			status = POWER_SUPPLY_STATUS_CHARGING;
		else if (power_supply_am_i_supplied(di->bat))
			status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else
			status = POWER_SUPPLY_STATUS_DISCHARGING;
	} else {
		if (di->cache.flags & BQ27XXX_FLAG_FC)
			status = POWER_SUPPLY_STATUS_FULL;
		else if (di->cache.flags & BQ27XXX_FLAG_DSC)
			status = POWER_SUPPLY_STATUS_DISCHARGING;
		else
			status = POWER_SUPPLY_STATUS_CHARGING;
	}

	val->intval = status;

	return 0;
}

static int bq27xxx_battery_capacity_level(struct bq27xxx_device_info *di,
					  union power_supply_propval *val)
{
	int level;

	if (di->chip == BQ27000 || di->chip == BQ27010) {
		if (di->cache.flags & BQ27000_FLAG_FC)
			level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else if (di->cache.flags & BQ27000_FLAG_EDV1)
			level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else if (di->cache.flags & BQ27000_FLAG_EDVF)
			level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else
			level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	} else {
		if (di->cache.flags & BQ27XXX_FLAG_FC)
			level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else if (di->cache.flags & BQ27XXX_FLAG_SOC1)
			level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else if (di->cache.flags & BQ27XXX_FLAG_SOCF)
			level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else
			level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	}

	val->intval = level;

	return 0;
}

/*
 * Return the battery Voltage in millivolts
 * Or < 0 if something fails.
 */
static int bq27xxx_battery_voltage(struct bq27xxx_device_info *di,
				   union power_supply_propval *val)
{
	int volt;

	volt = bq27xxx_read(di, BQ27XXX_REG_VOLT, false);
	if (volt < 0) {
		dev_err(di->dev, "error reading voltage\n");
		return volt;
	}

	val->intval = volt * 1000;

	return 0;
}

static int bq27xxx_simple_value(int value,
				union power_supply_propval *val)
{
	if (value < 0)
		return value;

	val->intval = value;

	return 0;
}

static int bq27xxx_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	int ret = 0;
	struct bq27xxx_device_info *di = power_supply_get_drvdata(psy);

	mutex_lock(&di->lock);
	if (time_is_before_jiffies(di->last_update + 5 * HZ)) {
		cancel_delayed_work_sync(&di->work);
		bq27xxx_battery_poll(&di->work.work);
	}
	mutex_unlock(&di->lock);

	if (psp != POWER_SUPPLY_PROP_PRESENT && di->cache.flags < 0)
		return -ENODEV;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = bq27xxx_battery_status(di, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = bq27xxx_battery_voltage(di, val);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = di->cache.flags < 0 ? 0 : 1;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = bq27xxx_battery_current(di, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = bq27xxx_simple_value(di->cache.capacity, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		ret = bq27xxx_battery_capacity_level(di, val);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = bq27xxx_simple_value(di->cache.temperature, val);
		if (ret == 0)
			val->intval -= 2731; /* convert decidegree k to c */
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		ret = bq27xxx_simple_value(di->cache.time_to_empty, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		ret = bq27xxx_simple_value(di->cache.time_to_empty_avg, val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		ret = bq27xxx_simple_value(di->cache.time_to_full, val);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = bq27xxx_simple_value(bq27xxx_battery_read_nac(di), val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = bq27xxx_simple_value(di->cache.charge_full, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = bq27xxx_simple_value(di->charge_design_full, val);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		ret = bq27xxx_simple_value(di->cache.cycle_count, val);
		break;
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		ret = bq27xxx_simple_value(di->cache.energy, val);
		break;
	case POWER_SUPPLY_PROP_POWER_AVG:
		ret = bq27xxx_simple_value(di->cache.power_avg, val);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = bq27xxx_simple_value(di->cache.health, val);
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = BQ27XXX_MANUFACTURER;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static void bq27xxx_external_power_changed(struct power_supply *psy)
{
	struct bq27xxx_device_info *di = power_supply_get_drvdata(psy);

	cancel_delayed_work_sync(&di->work);
	schedule_delayed_work(&di->work, 0);
}

static int __maybe_unused bq27xxx_powersupply_init(struct bq27xxx_device_info *di,
				    const char *name)
{
	int ret;
	struct power_supply_desc *psy_desc;
	struct power_supply_config psy_cfg = { .drv_data = di, };

	psy_desc = devm_kzalloc(di->dev, sizeof(*psy_desc), GFP_KERNEL);
	if (!psy_desc)
		return -ENOMEM;

	psy_desc->name = name;
	psy_desc->type = POWER_SUPPLY_TYPE_BATTERY;
	psy_desc->properties = bq27xxx_battery_props[di->chip].props;
	psy_desc->num_properties = bq27xxx_battery_props[di->chip].size;
	psy_desc->get_property = bq27xxx_battery_get_property;
	psy_desc->external_power_changed = bq27xxx_external_power_changed;

	INIT_DELAYED_WORK(&di->work, bq27xxx_battery_poll);
	mutex_init(&di->lock);

	di->bat = power_supply_register_no_ws(di->dev, psy_desc, &psy_cfg);
	if (IS_ERR(di->bat)) {
		ret = PTR_ERR(di->bat);
		dev_err(di->dev, "failed to register battery: %d\n", ret);
		return ret;
	}

	dev_info(di->dev, "support ver. %s enabled\n", DRIVER_VERSION);

	bq27xxx_battery_update(di);

	return 0;
}

static void __maybe_unused bq27xxx_powersupply_unregister(struct bq27xxx_device_info *di)
{
	/*
	 * power_supply_unregister call bq27xxx_battery_get_property which
	 * call bq27xxx_battery_poll.
	 * Make sure that bq27xxx_battery_poll will not call
	 * schedule_delayed_work again after unregister (which cause OOPS).
	 */
	poll_interval = 0;

	cancel_delayed_work_sync(&di->work);

	power_supply_unregister(di->bat);

	mutex_destroy(&di->lock);
}

/* i2c specific code */
#ifdef CONFIG_BATTERY_BQ27XXX_I2C

/* If the system has several batteries we need a different name for each
 * of them...
 */
static DEFINE_IDR(battery_id);
static DEFINE_MUTEX(battery_mutex);

static irqreturn_t bq27xxx_battery_irq_handler_thread(int irq, void *data)
{
	struct bq27xxx_device_info *di = data;

	bq27xxx_battery_update(di);

	return IRQ_HANDLED;
}

static int bq27xxx_battery_i2c_read(struct bq27xxx_device_info *di, u8 reg,
				    bool single)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg[2];
	unsigned char data[2];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = sizeof(reg);
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = data;
	if (single)
		msg[1].len = 1;
	else
		msg[1].len = 2;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0)
		return ret;

	if (!single)
		ret = get_unaligned_le16(data);
	else
		ret = data[0];

	return ret;
}

static int bq27xxx_battery_i2c_probe(struct i2c_client *client,
				     const struct i2c_device_id *id)
{
	char *name;
	struct bq27xxx_device_info *di;
	int num;
	int retval = 0;

	/* Get new ID for the new battery device */
	mutex_lock(&battery_mutex);
	num = idr_alloc(&battery_id, client, 0, 0, GFP_KERNEL);
	mutex_unlock(&battery_mutex);
	if (num < 0)
		return num;

	name = devm_kasprintf(&client->dev, GFP_KERNEL, "%s-%d", id->name, num);
	if (!name) {
		retval = -ENOMEM;
		goto batt_failed;
	}

	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di) {
		retval = -ENOMEM;
		goto batt_failed;
	}

	di->id = num;
	di->dev = &client->dev;
	di->chip = id->driver_data;
	di->bus.read = &bq27xxx_battery_i2c_read;
	di->regs = bq27xxx_regs[di->chip];

	retval = bq27xxx_powersupply_init(di, name);
	if (retval)
		goto batt_failed;

	/* Schedule a polling after about 1 min */
	schedule_delayed_work(&di->work, 60 * HZ);

	i2c_set_clientdata(client, di);

	if (client->irq) {
		retval = devm_request_threaded_irq(&client->dev, client->irq,
				NULL, bq27xxx_battery_irq_handler_thread,
				IRQF_ONESHOT,
				name, di);
		if (retval) {
			dev_err(&client->dev,
				"Unable to register IRQ %d error %d\n",
				client->irq, retval);
			return retval;
		}
	}

	return 0;

batt_failed:
	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, num);
	mutex_unlock(&battery_mutex);

	return retval;
}

static int bq27xxx_battery_i2c_remove(struct i2c_client *client)
{
	struct bq27xxx_device_info *di = i2c_get_clientdata(client);

	bq27xxx_powersupply_unregister(di);

	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, di->id);
	mutex_unlock(&battery_mutex);

	return 0;
}

static const struct i2c_device_id bq27xxx_id[] = {
	{ "bq27200", BQ27000 },
	{ "bq27210", BQ27010 },
	{ "bq27500", BQ27500 },
	{ "bq27510", BQ27500 },
	{ "bq27520", BQ27500 },
	{ "bq27530", BQ27530 },
	{ "bq27531", BQ27530 },
	{ "bq27541", BQ27541 },
	{ "bq27542", BQ27541 },
	{ "bq27546", BQ27541 },
	{ "bq27742", BQ27541 },
	{ "bq27545", BQ27545 },
	{ "bq27421", BQ27421 },
	{ "bq27425", BQ27421 },
	{ "bq27441", BQ27421 },
	{ "bq27621", BQ27421 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq27xxx_id);

static struct i2c_driver bq27xxx_battery_i2c_driver = {
	.driver = {
		.name = "bq27xxx-battery",
	},
	.probe = bq27xxx_battery_i2c_probe,
	.remove = bq27xxx_battery_i2c_remove,
	.id_table = bq27xxx_id,
};

static inline int bq27xxx_battery_i2c_init(void)
{
	int ret = i2c_add_driver(&bq27xxx_battery_i2c_driver);

	if (ret)
		pr_err("Unable to register BQ27xxx i2c driver\n");

	return ret;
}

static inline void bq27xxx_battery_i2c_exit(void)
{
	i2c_del_driver(&bq27xxx_battery_i2c_driver);
}

#else

static inline int bq27xxx_battery_i2c_init(void) { return 0; }
static inline void bq27xxx_battery_i2c_exit(void) {};

#endif

/* platform specific code */
#ifdef CONFIG_BATTERY_BQ27XXX_PLATFORM

static int bq27xxx_battery_platform_read(struct bq27xxx_device_info *di, u8 reg,
					 bool single)
{
	struct device *dev = di->dev;
	struct bq27xxx_platform_data *pdata = dev->platform_data;
	unsigned int timeout = 3;
	int upper, lower;
	int temp;

	if (!single) {
		/* Make sure the value has not changed in between reading the
		 * lower and the upper part */
		upper = pdata->read(dev, reg + 1);
		do {
			temp = upper;
			if (upper < 0)
				return upper;

			lower = pdata->read(dev, reg);
			if (lower < 0)
				return lower;

			upper = pdata->read(dev, reg + 1);
		} while (temp != upper && --timeout);

		if (timeout == 0)
			return -EIO;

		return (upper << 8) | lower;
	}

	return pdata->read(dev, reg);
}

static int bq27xxx_battery_platform_probe(struct platform_device *pdev)
{
	struct bq27xxx_device_info *di;
	struct bq27xxx_platform_data *pdata = pdev->dev.platform_data;
	const char *name;

	if (!pdata) {
		dev_err(&pdev->dev, "no platform_data supplied\n");
		return -EINVAL;
	}

	if (!pdata->read) {
		dev_err(&pdev->dev, "no hdq read callback supplied\n");
		return -EINVAL;
	}

	if (!pdata->chip) {
		dev_err(&pdev->dev, "no device supplied\n");
		return -EINVAL;
	}

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	platform_set_drvdata(pdev, di);

	di->dev = &pdev->dev;
	di->chip = pdata->chip;
	di->regs = bq27xxx_regs[di->chip];

	name = pdata->name ?: dev_name(&pdev->dev);
	di->bus.read = &bq27xxx_battery_platform_read;

	return bq27xxx_powersupply_init(di, name);
}

static int bq27xxx_battery_platform_remove(struct platform_device *pdev)
{
	struct bq27xxx_device_info *di = platform_get_drvdata(pdev);

	bq27xxx_powersupply_unregister(di);

	return 0;
}

static struct platform_driver bq27xxx_battery_platform_driver = {
	.probe	= bq27xxx_battery_platform_probe,
	.remove = bq27xxx_battery_platform_remove,
	.driver = {
		.name = "bq27000-battery",
	},
};

static inline int bq27xxx_battery_platform_init(void)
{
	int ret = platform_driver_register(&bq27xxx_battery_platform_driver);

	if (ret)
		pr_err("Unable to register BQ27xxx platform driver\n");

	return ret;
}

static inline void bq27xxx_battery_platform_exit(void)
{
	platform_driver_unregister(&bq27xxx_battery_platform_driver);
}

#else

static inline int bq27xxx_battery_platform_init(void) { return 0; }
static inline void bq27xxx_battery_platform_exit(void) {};

#endif

/*
 * Module stuff
 */

static int __init bq27xxx_battery_init(void)
{
	int ret;

	ret = bq27xxx_battery_i2c_init();
	if (ret)
		return ret;

	ret = bq27xxx_battery_platform_init();
	if (ret)
		bq27xxx_battery_i2c_exit();

	return ret;
}
module_init(bq27xxx_battery_init);

static void __exit bq27xxx_battery_exit(void)
{
	bq27xxx_battery_platform_exit();
	bq27xxx_battery_i2c_exit();
}
module_exit(bq27xxx_battery_exit);

#ifdef CONFIG_BATTERY_BQ27XXX_PLATFORM
MODULE_ALIAS("platform:bq27000-battery");
#endif

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("BQ27xxx battery monitor driver");
MODULE_LICENSE("GPL");
