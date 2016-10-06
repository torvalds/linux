/*
 * bq2415x charger driver
 *
 * Copyright (C) 2011-2013  Pali Rohár <pali.rohar@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Datasheets:
 * http://www.ti.com/product/bq24150
 * http://www.ti.com/product/bq24150a
 * http://www.ti.com/product/bq24152
 * http://www.ti.com/product/bq24153
 * http://www.ti.com/product/bq24153a
 * http://www.ti.com/product/bq24155
 * http://www.ti.com/product/bq24157s
 * http://www.ti.com/product/bq24158
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/acpi.h>

#include <linux/power/bq2415x_charger.h>

/* timeout for resetting chip timer */
#define BQ2415X_TIMER_TIMEOUT		10

#define BQ2415X_REG_STATUS		0x00
#define BQ2415X_REG_CONTROL		0x01
#define BQ2415X_REG_VOLTAGE		0x02
#define BQ2415X_REG_VENDER		0x03
#define BQ2415X_REG_CURRENT		0x04

/* reset state for all registers */
#define BQ2415X_RESET_STATUS		BIT(6)
#define BQ2415X_RESET_CONTROL		(BIT(4)|BIT(5))
#define BQ2415X_RESET_VOLTAGE		(BIT(1)|BIT(3))
#define BQ2415X_RESET_CURRENT		(BIT(0)|BIT(3)|BIT(7))

/* status register */
#define BQ2415X_BIT_TMR_RST		7
#define BQ2415X_BIT_OTG			7
#define BQ2415X_BIT_EN_STAT		6
#define BQ2415X_MASK_STAT		(BIT(4)|BIT(5))
#define BQ2415X_SHIFT_STAT		4
#define BQ2415X_BIT_BOOST		3
#define BQ2415X_MASK_FAULT		(BIT(0)|BIT(1)|BIT(2))
#define BQ2415X_SHIFT_FAULT		0

/* control register */
#define BQ2415X_MASK_LIMIT		(BIT(6)|BIT(7))
#define BQ2415X_SHIFT_LIMIT		6
#define BQ2415X_MASK_VLOWV		(BIT(4)|BIT(5))
#define BQ2415X_SHIFT_VLOWV		4
#define BQ2415X_BIT_TE			3
#define BQ2415X_BIT_CE			2
#define BQ2415X_BIT_HZ_MODE		1
#define BQ2415X_BIT_OPA_MODE		0

/* voltage register */
#define BQ2415X_MASK_VO		(BIT(2)|BIT(3)|BIT(4)|BIT(5)|BIT(6)|BIT(7))
#define BQ2415X_SHIFT_VO		2
#define BQ2415X_BIT_OTG_PL		1
#define BQ2415X_BIT_OTG_EN		0

/* vender register */
#define BQ2415X_MASK_VENDER		(BIT(5)|BIT(6)|BIT(7))
#define BQ2415X_SHIFT_VENDER		5
#define BQ2415X_MASK_PN			(BIT(3)|BIT(4))
#define BQ2415X_SHIFT_PN		3
#define BQ2415X_MASK_REVISION		(BIT(0)|BIT(1)|BIT(2))
#define BQ2415X_SHIFT_REVISION		0

/* current register */
#define BQ2415X_MASK_RESET		BIT(7)
#define BQ2415X_MASK_VI_CHRG		(BIT(4)|BIT(5)|BIT(6))
#define BQ2415X_SHIFT_VI_CHRG		4
/* N/A					BIT(3) */
#define BQ2415X_MASK_VI_TERM		(BIT(0)|BIT(1)|BIT(2))
#define BQ2415X_SHIFT_VI_TERM		0


enum bq2415x_command {
	BQ2415X_TIMER_RESET,
	BQ2415X_OTG_STATUS,
	BQ2415X_STAT_PIN_STATUS,
	BQ2415X_STAT_PIN_ENABLE,
	BQ2415X_STAT_PIN_DISABLE,
	BQ2415X_CHARGE_STATUS,
	BQ2415X_BOOST_STATUS,
	BQ2415X_FAULT_STATUS,

	BQ2415X_CHARGE_TERMINATION_STATUS,
	BQ2415X_CHARGE_TERMINATION_ENABLE,
	BQ2415X_CHARGE_TERMINATION_DISABLE,
	BQ2415X_CHARGER_STATUS,
	BQ2415X_CHARGER_ENABLE,
	BQ2415X_CHARGER_DISABLE,
	BQ2415X_HIGH_IMPEDANCE_STATUS,
	BQ2415X_HIGH_IMPEDANCE_ENABLE,
	BQ2415X_HIGH_IMPEDANCE_DISABLE,
	BQ2415X_BOOST_MODE_STATUS,
	BQ2415X_BOOST_MODE_ENABLE,
	BQ2415X_BOOST_MODE_DISABLE,

	BQ2415X_OTG_LEVEL,
	BQ2415X_OTG_ACTIVATE_HIGH,
	BQ2415X_OTG_ACTIVATE_LOW,
	BQ2415X_OTG_PIN_STATUS,
	BQ2415X_OTG_PIN_ENABLE,
	BQ2415X_OTG_PIN_DISABLE,

	BQ2415X_VENDER_CODE,
	BQ2415X_PART_NUMBER,
	BQ2415X_REVISION,
};

enum bq2415x_chip {
	BQUNKNOWN,
	BQ24150,
	BQ24150A,
	BQ24151,
	BQ24151A,
	BQ24152,
	BQ24153,
	BQ24153A,
	BQ24155,
	BQ24156,
	BQ24156A,
	BQ24157S,
	BQ24158,
};

static char *bq2415x_chip_name[] = {
	"unknown",
	"bq24150",
	"bq24150a",
	"bq24151",
	"bq24151a",
	"bq24152",
	"bq24153",
	"bq24153a",
	"bq24155",
	"bq24156",
	"bq24156a",
	"bq24157s",
	"bq24158",
};

struct bq2415x_device {
	struct device *dev;
	struct bq2415x_platform_data init_data;
	struct power_supply *charger;
	struct power_supply_desc charger_desc;
	struct delayed_work work;
	struct device_node *notify_node;
	struct notifier_block nb;
	enum bq2415x_mode reported_mode;/* mode reported by hook function */
	enum bq2415x_mode mode;		/* currently configured mode */
	enum bq2415x_chip chip;
	const char *timer_error;
	char *model;
	char *name;
	int autotimer;	/* 1 - if driver automatically reset timer, 0 - not */
	int automode;	/* 1 - enabled, 0 - disabled; -1 - not supported */
	int id;
};

/* each registered chip must have unique id */
static DEFINE_IDR(bq2415x_id);

static DEFINE_MUTEX(bq2415x_id_mutex);
static DEFINE_MUTEX(bq2415x_timer_mutex);
static DEFINE_MUTEX(bq2415x_i2c_mutex);

/**** i2c read functions ****/

/* read value from register */
static int bq2415x_i2c_read(struct bq2415x_device *bq, u8 reg)
{
	struct i2c_client *client = to_i2c_client(bq->dev);
	struct i2c_msg msg[2];
	u8 val;
	int ret;

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = sizeof(reg);
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = &val;
	msg[1].len = sizeof(val);

	mutex_lock(&bq2415x_i2c_mutex);
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	mutex_unlock(&bq2415x_i2c_mutex);

	if (ret < 0)
		return ret;

	return val;
}

/* read value from register, apply mask and right shift it */
static int bq2415x_i2c_read_mask(struct bq2415x_device *bq, u8 reg,
				 u8 mask, u8 shift)
{
	int ret;

	if (shift > 8)
		return -EINVAL;

	ret = bq2415x_i2c_read(bq, reg);
	if (ret < 0)
		return ret;
	return (ret & mask) >> shift;
}

/* read value from register and return one specified bit */
static int bq2415x_i2c_read_bit(struct bq2415x_device *bq, u8 reg, u8 bit)
{
	if (bit > 8)
		return -EINVAL;
	return bq2415x_i2c_read_mask(bq, reg, BIT(bit), bit);
}

/**** i2c write functions ****/

/* write value to register */
static int bq2415x_i2c_write(struct bq2415x_device *bq, u8 reg, u8 val)
{
	struct i2c_client *client = to_i2c_client(bq->dev);
	struct i2c_msg msg[1];
	u8 data[2];
	int ret;

	data[0] = reg;
	data[1] = val;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = data;
	msg[0].len = ARRAY_SIZE(data);

	mutex_lock(&bq2415x_i2c_mutex);
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	mutex_unlock(&bq2415x_i2c_mutex);

	/* i2c_transfer returns number of messages transferred */
	if (ret < 0)
		return ret;
	else if (ret != 1)
		return -EIO;

	return 0;
}

/* read value from register, change it with mask left shifted and write back */
static int bq2415x_i2c_write_mask(struct bq2415x_device *bq, u8 reg, u8 val,
				  u8 mask, u8 shift)
{
	int ret;

	if (shift > 8)
		return -EINVAL;

	ret = bq2415x_i2c_read(bq, reg);
	if (ret < 0)
		return ret;

	ret &= ~mask;
	ret |= val << shift;

	return bq2415x_i2c_write(bq, reg, ret);
}

/* change only one bit in register */
static int bq2415x_i2c_write_bit(struct bq2415x_device *bq, u8 reg,
				 bool val, u8 bit)
{
	if (bit > 8)
		return -EINVAL;
	return bq2415x_i2c_write_mask(bq, reg, val, BIT(bit), bit);
}

/**** global functions ****/

/* exec command function */
static int bq2415x_exec_command(struct bq2415x_device *bq,
				enum bq2415x_command command)
{
	int ret;

	switch (command) {
	case BQ2415X_TIMER_RESET:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_STATUS,
				1, BQ2415X_BIT_TMR_RST);
	case BQ2415X_OTG_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_STATUS,
				BQ2415X_BIT_OTG);
	case BQ2415X_STAT_PIN_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_STATUS,
				BQ2415X_BIT_EN_STAT);
	case BQ2415X_STAT_PIN_ENABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_STATUS, 1,
				BQ2415X_BIT_EN_STAT);
	case BQ2415X_STAT_PIN_DISABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_STATUS, 0,
				BQ2415X_BIT_EN_STAT);
	case BQ2415X_CHARGE_STATUS:
		return bq2415x_i2c_read_mask(bq, BQ2415X_REG_STATUS,
				BQ2415X_MASK_STAT, BQ2415X_SHIFT_STAT);
	case BQ2415X_BOOST_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_STATUS,
				BQ2415X_BIT_BOOST);
	case BQ2415X_FAULT_STATUS:
		return bq2415x_i2c_read_mask(bq, BQ2415X_REG_STATUS,
			BQ2415X_MASK_FAULT, BQ2415X_SHIFT_FAULT);

	case BQ2415X_CHARGE_TERMINATION_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_CONTROL,
				BQ2415X_BIT_TE);
	case BQ2415X_CHARGE_TERMINATION_ENABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				1, BQ2415X_BIT_TE);
	case BQ2415X_CHARGE_TERMINATION_DISABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				0, BQ2415X_BIT_TE);
	case BQ2415X_CHARGER_STATUS:
		ret = bq2415x_i2c_read_bit(bq, BQ2415X_REG_CONTROL,
			BQ2415X_BIT_CE);
		if (ret < 0)
			return ret;
		return ret > 0 ? 0 : 1;
	case BQ2415X_CHARGER_ENABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				0, BQ2415X_BIT_CE);
	case BQ2415X_CHARGER_DISABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				1, BQ2415X_BIT_CE);
	case BQ2415X_HIGH_IMPEDANCE_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_CONTROL,
				BQ2415X_BIT_HZ_MODE);
	case BQ2415X_HIGH_IMPEDANCE_ENABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				1, BQ2415X_BIT_HZ_MODE);
	case BQ2415X_HIGH_IMPEDANCE_DISABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				0, BQ2415X_BIT_HZ_MODE);
	case BQ2415X_BOOST_MODE_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_CONTROL,
				BQ2415X_BIT_OPA_MODE);
	case BQ2415X_BOOST_MODE_ENABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				1, BQ2415X_BIT_OPA_MODE);
	case BQ2415X_BOOST_MODE_DISABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_CONTROL,
				0, BQ2415X_BIT_OPA_MODE);

	case BQ2415X_OTG_LEVEL:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_VOLTAGE,
				BQ2415X_BIT_OTG_PL);
	case BQ2415X_OTG_ACTIVATE_HIGH:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_VOLTAGE,
				1, BQ2415X_BIT_OTG_PL);
	case BQ2415X_OTG_ACTIVATE_LOW:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_VOLTAGE,
				0, BQ2415X_BIT_OTG_PL);
	case BQ2415X_OTG_PIN_STATUS:
		return bq2415x_i2c_read_bit(bq, BQ2415X_REG_VOLTAGE,
				BQ2415X_BIT_OTG_EN);
	case BQ2415X_OTG_PIN_ENABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_VOLTAGE,
				1, BQ2415X_BIT_OTG_EN);
	case BQ2415X_OTG_PIN_DISABLE:
		return bq2415x_i2c_write_bit(bq, BQ2415X_REG_VOLTAGE,
				0, BQ2415X_BIT_OTG_EN);

	case BQ2415X_VENDER_CODE:
		return bq2415x_i2c_read_mask(bq, BQ2415X_REG_VENDER,
			BQ2415X_MASK_VENDER, BQ2415X_SHIFT_VENDER);
	case BQ2415X_PART_NUMBER:
		return bq2415x_i2c_read_mask(bq, BQ2415X_REG_VENDER,
				BQ2415X_MASK_PN, BQ2415X_SHIFT_PN);
	case BQ2415X_REVISION:
		return bq2415x_i2c_read_mask(bq, BQ2415X_REG_VENDER,
			BQ2415X_MASK_REVISION, BQ2415X_SHIFT_REVISION);
	}
	return -EINVAL;
}

/* detect chip type */
static enum bq2415x_chip bq2415x_detect_chip(struct bq2415x_device *bq)
{
	struct i2c_client *client = to_i2c_client(bq->dev);
	int ret = bq2415x_exec_command(bq, BQ2415X_PART_NUMBER);

	if (ret < 0)
		return ret;

	switch (client->addr) {
	case 0x6b:
		switch (ret) {
		case 0:
			if (bq->chip == BQ24151A)
				return bq->chip;
			return BQ24151;
		case 1:
			if (bq->chip == BQ24150A ||
				bq->chip == BQ24152 ||
				bq->chip == BQ24155)
				return bq->chip;
			return BQ24150;
		case 2:
			if (bq->chip == BQ24153A)
				return bq->chip;
			return BQ24153;
		default:
			return BQUNKNOWN;
		}
		break;

	case 0x6a:
		switch (ret) {
		case 0:
			if (bq->chip == BQ24156A)
				return bq->chip;
			return BQ24156;
		case 2:
			if (bq->chip == BQ24157S)
				return bq->chip;
			return BQ24158;
		default:
			return BQUNKNOWN;
		}
		break;
	}

	return BQUNKNOWN;
}

/* detect chip revision */
static int bq2415x_detect_revision(struct bq2415x_device *bq)
{
	int ret = bq2415x_exec_command(bq, BQ2415X_REVISION);
	int chip = bq2415x_detect_chip(bq);

	if (ret < 0 || chip < 0)
		return -1;

	switch (chip) {
	case BQ24150:
	case BQ24150A:
	case BQ24151:
	case BQ24151A:
	case BQ24152:
		if (ret >= 0 && ret <= 3)
			return ret;
		return -1;
	case BQ24153:
	case BQ24153A:
	case BQ24156:
	case BQ24156A:
	case BQ24157S:
	case BQ24158:
		if (ret == 3)
			return 0;
		else if (ret == 1)
			return 1;
		return -1;
	case BQ24155:
		if (ret == 3)
			return 3;
		return -1;
	case BQUNKNOWN:
		return -1;
	}

	return -1;
}

/* return chip vender code */
static int bq2415x_get_vender_code(struct bq2415x_device *bq)
{
	int ret;

	ret = bq2415x_exec_command(bq, BQ2415X_VENDER_CODE);
	if (ret < 0)
		return 0;

	/* convert to binary */
	return (ret & 0x1) +
	       ((ret >> 1) & 0x1) * 10 +
	       ((ret >> 2) & 0x1) * 100;
}

/* reset all chip registers to default state */
static void bq2415x_reset_chip(struct bq2415x_device *bq)
{
	bq2415x_i2c_write(bq, BQ2415X_REG_CURRENT, BQ2415X_RESET_CURRENT);
	bq2415x_i2c_write(bq, BQ2415X_REG_VOLTAGE, BQ2415X_RESET_VOLTAGE);
	bq2415x_i2c_write(bq, BQ2415X_REG_CONTROL, BQ2415X_RESET_CONTROL);
	bq2415x_i2c_write(bq, BQ2415X_REG_STATUS, BQ2415X_RESET_STATUS);
	bq->timer_error = NULL;
}

/**** properties functions ****/

/* set current limit in mA */
static int bq2415x_set_current_limit(struct bq2415x_device *bq, int mA)
{
	int val;

	if (mA <= 100)
		val = 0;
	else if (mA <= 500)
		val = 1;
	else if (mA <= 800)
		val = 2;
	else
		val = 3;

	return bq2415x_i2c_write_mask(bq, BQ2415X_REG_CONTROL, val,
			BQ2415X_MASK_LIMIT, BQ2415X_SHIFT_LIMIT);
}

/* get current limit in mA */
static int bq2415x_get_current_limit(struct bq2415x_device *bq)
{
	int ret;

	ret = bq2415x_i2c_read_mask(bq, BQ2415X_REG_CONTROL,
			BQ2415X_MASK_LIMIT, BQ2415X_SHIFT_LIMIT);
	if (ret < 0)
		return ret;
	else if (ret == 0)
		return 100;
	else if (ret == 1)
		return 500;
	else if (ret == 2)
		return 800;
	else if (ret == 3)
		return 1800;
	return -EINVAL;
}

/* set weak battery voltage in mV */
static int bq2415x_set_weak_battery_voltage(struct bq2415x_device *bq, int mV)
{
	int val;

	/* round to 100mV */
	if (mV <= 3400 + 50)
		val = 0;
	else if (mV <= 3500 + 50)
		val = 1;
	else if (mV <= 3600 + 50)
		val = 2;
	else
		val = 3;

	return bq2415x_i2c_write_mask(bq, BQ2415X_REG_CONTROL, val,
			BQ2415X_MASK_VLOWV, BQ2415X_SHIFT_VLOWV);
}

/* get weak battery voltage in mV */
static int bq2415x_get_weak_battery_voltage(struct bq2415x_device *bq)
{
	int ret;

	ret = bq2415x_i2c_read_mask(bq, BQ2415X_REG_CONTROL,
			BQ2415X_MASK_VLOWV, BQ2415X_SHIFT_VLOWV);
	if (ret < 0)
		return ret;
	return 100 * (34 + ret);
}

/* set battery regulation voltage in mV */
static int bq2415x_set_battery_regulation_voltage(struct bq2415x_device *bq,
						  int mV)
{
	int val = (mV/10 - 350) / 2;

	/*
	 * According to datasheet, maximum battery regulation voltage is
	 * 4440mV which is b101111 = 47.
	 */
	if (val < 0)
		val = 0;
	else if (val > 47)
		return -EINVAL;

	return bq2415x_i2c_write_mask(bq, BQ2415X_REG_VOLTAGE, val,
			BQ2415X_MASK_VO, BQ2415X_SHIFT_VO);
}

/* get battery regulation voltage in mV */
static int bq2415x_get_battery_regulation_voltage(struct bq2415x_device *bq)
{
	int ret = bq2415x_i2c_read_mask(bq, BQ2415X_REG_VOLTAGE,
			BQ2415X_MASK_VO, BQ2415X_SHIFT_VO);

	if (ret < 0)
		return ret;
	return 10 * (350 + 2*ret);
}

/* set charge current in mA (platform data must provide resistor sense) */
static int bq2415x_set_charge_current(struct bq2415x_device *bq, int mA)
{
	int val;

	if (bq->init_data.resistor_sense <= 0)
		return -EINVAL;

	val = (mA * bq->init_data.resistor_sense - 37400) / 6800;
	if (val < 0)
		val = 0;
	else if (val > 7)
		val = 7;

	return bq2415x_i2c_write_mask(bq, BQ2415X_REG_CURRENT, val,
			BQ2415X_MASK_VI_CHRG | BQ2415X_MASK_RESET,
			BQ2415X_SHIFT_VI_CHRG);
}

/* get charge current in mA (platform data must provide resistor sense) */
static int bq2415x_get_charge_current(struct bq2415x_device *bq)
{
	int ret;

	if (bq->init_data.resistor_sense <= 0)
		return -EINVAL;

	ret = bq2415x_i2c_read_mask(bq, BQ2415X_REG_CURRENT,
			BQ2415X_MASK_VI_CHRG, BQ2415X_SHIFT_VI_CHRG);
	if (ret < 0)
		return ret;
	return (37400 + 6800*ret) / bq->init_data.resistor_sense;
}

/* set termination current in mA (platform data must provide resistor sense) */
static int bq2415x_set_termination_current(struct bq2415x_device *bq, int mA)
{
	int val;

	if (bq->init_data.resistor_sense <= 0)
		return -EINVAL;

	val = (mA * bq->init_data.resistor_sense - 3400) / 3400;
	if (val < 0)
		val = 0;
	else if (val > 7)
		val = 7;

	return bq2415x_i2c_write_mask(bq, BQ2415X_REG_CURRENT, val,
			BQ2415X_MASK_VI_TERM | BQ2415X_MASK_RESET,
			BQ2415X_SHIFT_VI_TERM);
}

/* get termination current in mA (platform data must provide resistor sense) */
static int bq2415x_get_termination_current(struct bq2415x_device *bq)
{
	int ret;

	if (bq->init_data.resistor_sense <= 0)
		return -EINVAL;

	ret = bq2415x_i2c_read_mask(bq, BQ2415X_REG_CURRENT,
			BQ2415X_MASK_VI_TERM, BQ2415X_SHIFT_VI_TERM);
	if (ret < 0)
		return ret;
	return (3400 + 3400*ret) / bq->init_data.resistor_sense;
}

/* set default value of property */
#define bq2415x_set_default_value(bq, prop) \
	do { \
		int ret = 0; \
		if (bq->init_data.prop != -1) \
			ret = bq2415x_set_##prop(bq, bq->init_data.prop); \
		if (ret < 0) \
			return ret; \
	} while (0)

/* set default values of all properties */
static int bq2415x_set_defaults(struct bq2415x_device *bq)
{
	bq2415x_exec_command(bq, BQ2415X_BOOST_MODE_DISABLE);
	bq2415x_exec_command(bq, BQ2415X_CHARGER_DISABLE);
	bq2415x_exec_command(bq, BQ2415X_CHARGE_TERMINATION_DISABLE);

	bq2415x_set_default_value(bq, current_limit);
	bq2415x_set_default_value(bq, weak_battery_voltage);
	bq2415x_set_default_value(bq, battery_regulation_voltage);

	if (bq->init_data.resistor_sense > 0) {
		bq2415x_set_default_value(bq, charge_current);
		bq2415x_set_default_value(bq, termination_current);
		bq2415x_exec_command(bq, BQ2415X_CHARGE_TERMINATION_ENABLE);
	}

	bq2415x_exec_command(bq, BQ2415X_CHARGER_ENABLE);
	return 0;
}

/**** charger mode functions ****/

/* set charger mode */
static int bq2415x_set_mode(struct bq2415x_device *bq, enum bq2415x_mode mode)
{
	int ret = 0;
	int charger = 0;
	int boost = 0;

	if (mode == BQ2415X_MODE_BOOST)
		boost = 1;
	else if (mode != BQ2415X_MODE_OFF)
		charger = 1;

	if (!charger)
		ret = bq2415x_exec_command(bq, BQ2415X_CHARGER_DISABLE);

	if (!boost)
		ret = bq2415x_exec_command(bq, BQ2415X_BOOST_MODE_DISABLE);

	if (ret < 0)
		return ret;

	switch (mode) {
	case BQ2415X_MODE_OFF:
		dev_dbg(bq->dev, "changing mode to: Offline\n");
		ret = bq2415x_set_current_limit(bq, 100);
		break;
	case BQ2415X_MODE_NONE:
		dev_dbg(bq->dev, "changing mode to: N/A\n");
		ret = bq2415x_set_current_limit(bq, 100);
		break;
	case BQ2415X_MODE_HOST_CHARGER:
		dev_dbg(bq->dev, "changing mode to: Host/HUB charger\n");
		ret = bq2415x_set_current_limit(bq, 500);
		break;
	case BQ2415X_MODE_DEDICATED_CHARGER:
		dev_dbg(bq->dev, "changing mode to: Dedicated charger\n");
		ret = bq2415x_set_current_limit(bq, 1800);
		break;
	case BQ2415X_MODE_BOOST: /* Boost mode */
		dev_dbg(bq->dev, "changing mode to: Boost\n");
		ret = bq2415x_set_current_limit(bq, 100);
		break;
	}

	if (ret < 0)
		return ret;

	if (charger)
		ret = bq2415x_exec_command(bq, BQ2415X_CHARGER_ENABLE);
	else if (boost)
		ret = bq2415x_exec_command(bq, BQ2415X_BOOST_MODE_ENABLE);

	if (ret < 0)
		return ret;

	bq2415x_set_default_value(bq, weak_battery_voltage);
	bq2415x_set_default_value(bq, battery_regulation_voltage);

	bq->mode = mode;
	sysfs_notify(&bq->charger->dev.kobj, NULL, "mode");

	return 0;

}

static bool bq2415x_update_reported_mode(struct bq2415x_device *bq, int mA)
{
	enum bq2415x_mode mode;

	if (mA == 0)
		mode = BQ2415X_MODE_OFF;
	else if (mA < 500)
		mode = BQ2415X_MODE_NONE;
	else if (mA < 1800)
		mode = BQ2415X_MODE_HOST_CHARGER;
	else
		mode = BQ2415X_MODE_DEDICATED_CHARGER;

	if (bq->reported_mode == mode)
		return false;

	bq->reported_mode = mode;
	return true;
}

static int bq2415x_notifier_call(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct bq2415x_device *bq =
		container_of(nb, struct bq2415x_device, nb);
	struct power_supply *psy = v;
	union power_supply_propval prop;
	int ret;

	if (val != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	/* Ignore event if it was not send by notify_node/notify_device */
	if (bq->notify_node) {
		if (!psy->dev.parent ||
		    psy->dev.parent->of_node != bq->notify_node)
			return NOTIFY_OK;
	} else if (bq->init_data.notify_device) {
		if (strcmp(psy->desc->name, bq->init_data.notify_device) != 0)
			return NOTIFY_OK;
	}

	dev_dbg(bq->dev, "notifier call was called\n");

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_CURRENT_MAX,
			&prop);
	if (ret != 0)
		return NOTIFY_OK;

	if (!bq2415x_update_reported_mode(bq, prop.intval))
		return NOTIFY_OK;

	/* if automode is not enabled do not tell about reported_mode */
	if (bq->automode < 1)
		return NOTIFY_OK;

	schedule_delayed_work(&bq->work, 0);

	return NOTIFY_OK;
}

/**** timer functions ****/

/* enable/disable auto resetting chip timer */
static void bq2415x_set_autotimer(struct bq2415x_device *bq, int state)
{
	mutex_lock(&bq2415x_timer_mutex);

	if (bq->autotimer == state) {
		mutex_unlock(&bq2415x_timer_mutex);
		return;
	}

	bq->autotimer = state;

	if (state) {
		schedule_delayed_work(&bq->work, BQ2415X_TIMER_TIMEOUT * HZ);
		bq2415x_exec_command(bq, BQ2415X_TIMER_RESET);
		bq->timer_error = NULL;
	} else {
		cancel_delayed_work_sync(&bq->work);
	}

	mutex_unlock(&bq2415x_timer_mutex);
}

/* called by bq2415x_timer_work on timer error */
static void bq2415x_timer_error(struct bq2415x_device *bq, const char *msg)
{
	bq->timer_error = msg;
	sysfs_notify(&bq->charger->dev.kobj, NULL, "timer");
	dev_err(bq->dev, "%s\n", msg);
	if (bq->automode > 0)
		bq->automode = 0;
	bq2415x_set_mode(bq, BQ2415X_MODE_OFF);
	bq2415x_set_autotimer(bq, 0);
}

/* delayed work function for auto resetting chip timer */
static void bq2415x_timer_work(struct work_struct *work)
{
	struct bq2415x_device *bq = container_of(work, struct bq2415x_device,
						 work.work);
	int ret;
	int error;
	int boost;

	if (bq->automode > 0 && (bq->reported_mode != bq->mode)) {
		sysfs_notify(&bq->charger->dev.kobj, NULL, "reported_mode");
		bq2415x_set_mode(bq, bq->reported_mode);
	}

	if (!bq->autotimer)
		return;

	ret = bq2415x_exec_command(bq, BQ2415X_TIMER_RESET);
	if (ret < 0) {
		bq2415x_timer_error(bq, "Resetting timer failed");
		return;
	}

	boost = bq2415x_exec_command(bq, BQ2415X_BOOST_MODE_STATUS);
	if (boost < 0) {
		bq2415x_timer_error(bq, "Unknown error");
		return;
	}

	error = bq2415x_exec_command(bq, BQ2415X_FAULT_STATUS);
	if (error < 0) {
		bq2415x_timer_error(bq, "Unknown error");
		return;
	}

	if (boost) {
		switch (error) {
		/* Non fatal errors, chip is OK */
		case 0: /* No error */
			break;
		case 6: /* Timer expired */
			dev_err(bq->dev, "Timer expired\n");
			break;
		case 3: /* Battery voltage too low */
			dev_err(bq->dev, "Battery voltage to low\n");
			break;

		/* Fatal errors, disable and reset chip */
		case 1: /* Overvoltage protection (chip fried) */
			bq2415x_timer_error(bq,
				"Overvoltage protection (chip fried)");
			return;
		case 2: /* Overload */
			bq2415x_timer_error(bq, "Overload");
			return;
		case 4: /* Battery overvoltage protection */
			bq2415x_timer_error(bq,
				"Battery overvoltage protection");
			return;
		case 5: /* Thermal shutdown (too hot) */
			bq2415x_timer_error(bq,
					"Thermal shutdown (too hot)");
			return;
		case 7: /* N/A */
			bq2415x_timer_error(bq, "Unknown error");
			return;
		}
	} else {
		switch (error) {
		/* Non fatal errors, chip is OK */
		case 0: /* No error */
			break;
		case 2: /* Sleep mode */
			dev_err(bq->dev, "Sleep mode\n");
			break;
		case 3: /* Poor input source */
			dev_err(bq->dev, "Poor input source\n");
			break;
		case 6: /* Timer expired */
			dev_err(bq->dev, "Timer expired\n");
			break;
		case 7: /* No battery */
			dev_err(bq->dev, "No battery\n");
			break;

		/* Fatal errors, disable and reset chip */
		case 1: /* Overvoltage protection (chip fried) */
			bq2415x_timer_error(bq,
				"Overvoltage protection (chip fried)");
			return;
		case 4: /* Battery overvoltage protection */
			bq2415x_timer_error(bq,
				"Battery overvoltage protection");
			return;
		case 5: /* Thermal shutdown (too hot) */
			bq2415x_timer_error(bq,
				"Thermal shutdown (too hot)");
			return;
		}
	}

	schedule_delayed_work(&bq->work, BQ2415X_TIMER_TIMEOUT * HZ);
}

/**** power supply interface code ****/

static enum power_supply_property bq2415x_power_supply_props[] = {
	/* TODO: maybe add more power supply properties */
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_MODEL_NAME,
};

static int bq2415x_power_supply_get_property(struct power_supply *psy,
					     enum power_supply_property psp,
					     union power_supply_propval *val)
{
	struct bq2415x_device *bq = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = bq2415x_exec_command(bq, BQ2415X_CHARGE_STATUS);
		if (ret < 0)
			return ret;
		else if (ret == 0) /* Ready */
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else if (ret == 1) /* Charge in progress */
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (ret == 2) /* Charge done */
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = bq->model;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int bq2415x_power_supply_init(struct bq2415x_device *bq)
{
	int ret;
	int chip;
	char revstr[8];
	struct power_supply_config psy_cfg = { .drv_data = bq, };

	bq->charger_desc.name = bq->name;
	bq->charger_desc.type = POWER_SUPPLY_TYPE_USB;
	bq->charger_desc.properties = bq2415x_power_supply_props;
	bq->charger_desc.num_properties =
			ARRAY_SIZE(bq2415x_power_supply_props);
	bq->charger_desc.get_property = bq2415x_power_supply_get_property;

	ret = bq2415x_detect_chip(bq);
	if (ret < 0)
		chip = BQUNKNOWN;
	else
		chip = ret;

	ret = bq2415x_detect_revision(bq);
	if (ret < 0)
		strcpy(revstr, "unknown");
	else
		sprintf(revstr, "1.%d", ret);

	bq->model = kasprintf(GFP_KERNEL,
				"chip %s, revision %s, vender code %.3d",
				bq2415x_chip_name[chip], revstr,
				bq2415x_get_vender_code(bq));
	if (!bq->model) {
		dev_err(bq->dev, "failed to allocate model name\n");
		return -ENOMEM;
	}

	bq->charger = power_supply_register(bq->dev, &bq->charger_desc,
					    &psy_cfg);
	if (IS_ERR(bq->charger)) {
		kfree(bq->model);
		return PTR_ERR(bq->charger);
	}

	return 0;
}

static void bq2415x_power_supply_exit(struct bq2415x_device *bq)
{
	bq->autotimer = 0;
	if (bq->automode > 0)
		bq->automode = 0;
	cancel_delayed_work_sync(&bq->work);
	power_supply_unregister(bq->charger);
	kfree(bq->model);
}

/**** additional sysfs entries for power supply interface ****/

/* show *_status entries */
static ssize_t bq2415x_sysfs_show_status(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq2415x_device *bq = power_supply_get_drvdata(psy);
	enum bq2415x_command command;
	int ret;

	if (strcmp(attr->attr.name, "otg_status") == 0)
		command = BQ2415X_OTG_STATUS;
	else if (strcmp(attr->attr.name, "charge_status") == 0)
		command = BQ2415X_CHARGE_STATUS;
	else if (strcmp(attr->attr.name, "boost_status") == 0)
		command = BQ2415X_BOOST_STATUS;
	else if (strcmp(attr->attr.name, "fault_status") == 0)
		command = BQ2415X_FAULT_STATUS;
	else
		return -EINVAL;

	ret = bq2415x_exec_command(bq, command);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", ret);
}

/*
 * set timer entry:
 *    auto - enable auto mode
 *    off - disable auto mode
 *    (other values) - reset chip timer
 */
static ssize_t bq2415x_sysfs_set_timer(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq2415x_device *bq = power_supply_get_drvdata(psy);
	int ret = 0;

	if (strncmp(buf, "auto", 4) == 0)
		bq2415x_set_autotimer(bq, 1);
	else if (strncmp(buf, "off", 3) == 0)
		bq2415x_set_autotimer(bq, 0);
	else
		ret = bq2415x_exec_command(bq, BQ2415X_TIMER_RESET);

	if (ret < 0)
		return ret;
	return count;
}

/* show timer entry (auto or off) */
static ssize_t bq2415x_sysfs_show_timer(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq2415x_device *bq = power_supply_get_drvdata(psy);

	if (bq->timer_error)
		return sprintf(buf, "%s\n", bq->timer_error);

	if (bq->autotimer)
		return sprintf(buf, "auto\n");
	return sprintf(buf, "off\n");
}

/*
 * set mode entry:
 *    auto - if automode is supported, enable it and set mode to reported
 *    none - disable charger and boost mode
 *    host - charging mode for host/hub chargers (current limit 500mA)
 *    dedicated - charging mode for dedicated chargers (unlimited current limit)
 *    boost - disable charger and enable boost mode
 */
static ssize_t bq2415x_sysfs_set_mode(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq2415x_device *bq = power_supply_get_drvdata(psy);
	enum bq2415x_mode mode;
	int ret = 0;

	if (strncmp(buf, "auto", 4) == 0) {
		if (bq->automode < 0)
			return -EINVAL;
		bq->automode = 1;
		mode = bq->reported_mode;
	} else if (strncmp(buf, "off", 3) == 0) {
		if (bq->automode > 0)
			bq->automode = 0;
		mode = BQ2415X_MODE_OFF;
	} else if (strncmp(buf, "none", 4) == 0) {
		if (bq->automode > 0)
			bq->automode = 0;
		mode = BQ2415X_MODE_NONE;
	} else if (strncmp(buf, "host", 4) == 0) {
		if (bq->automode > 0)
			bq->automode = 0;
		mode = BQ2415X_MODE_HOST_CHARGER;
	} else if (strncmp(buf, "dedicated", 9) == 0) {
		if (bq->automode > 0)
			bq->automode = 0;
		mode = BQ2415X_MODE_DEDICATED_CHARGER;
	} else if (strncmp(buf, "boost", 5) == 0) {
		if (bq->automode > 0)
			bq->automode = 0;
		mode = BQ2415X_MODE_BOOST;
	} else if (strncmp(buf, "reset", 5) == 0) {
		bq2415x_reset_chip(bq);
		bq2415x_set_defaults(bq);
		if (bq->automode <= 0)
			return count;
		bq->automode = 1;
		mode = bq->reported_mode;
	} else {
		return -EINVAL;
	}

	ret = bq2415x_set_mode(bq, mode);
	if (ret < 0)
		return ret;
	return count;
}

/* show mode entry (auto, none, host, dedicated or boost) */
static ssize_t bq2415x_sysfs_show_mode(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq2415x_device *bq = power_supply_get_drvdata(psy);
	ssize_t ret = 0;

	if (bq->automode > 0)
		ret += sprintf(buf+ret, "auto (");

	switch (bq->mode) {
	case BQ2415X_MODE_OFF:
		ret += sprintf(buf+ret, "off");
		break;
	case BQ2415X_MODE_NONE:
		ret += sprintf(buf+ret, "none");
		break;
	case BQ2415X_MODE_HOST_CHARGER:
		ret += sprintf(buf+ret, "host");
		break;
	case BQ2415X_MODE_DEDICATED_CHARGER:
		ret += sprintf(buf+ret, "dedicated");
		break;
	case BQ2415X_MODE_BOOST:
		ret += sprintf(buf+ret, "boost");
		break;
	}

	if (bq->automode > 0)
		ret += sprintf(buf+ret, ")");

	ret += sprintf(buf+ret, "\n");
	return ret;
}

/* show reported_mode entry (none, host, dedicated or boost) */
static ssize_t bq2415x_sysfs_show_reported_mode(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq2415x_device *bq = power_supply_get_drvdata(psy);

	if (bq->automode < 0)
		return -EINVAL;

	switch (bq->reported_mode) {
	case BQ2415X_MODE_OFF:
		return sprintf(buf, "off\n");
	case BQ2415X_MODE_NONE:
		return sprintf(buf, "none\n");
	case BQ2415X_MODE_HOST_CHARGER:
		return sprintf(buf, "host\n");
	case BQ2415X_MODE_DEDICATED_CHARGER:
		return sprintf(buf, "dedicated\n");
	case BQ2415X_MODE_BOOST:
		return sprintf(buf, "boost\n");
	}

	return -EINVAL;
}

/* directly set raw value to chip register, format: 'register value' */
static ssize_t bq2415x_sysfs_set_registers(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf,
					   size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq2415x_device *bq = power_supply_get_drvdata(psy);
	ssize_t ret = 0;
	unsigned int reg;
	unsigned int val;

	if (sscanf(buf, "%x %x", &reg, &val) != 2)
		return -EINVAL;

	if (reg > 4 || val > 255)
		return -EINVAL;

	ret = bq2415x_i2c_write(bq, reg, val);
	if (ret < 0)
		return ret;
	return count;
}

/* print value of chip register, format: 'register=value' */
static ssize_t bq2415x_sysfs_print_reg(struct bq2415x_device *bq,
				       u8 reg,
				       char *buf)
{
	int ret = bq2415x_i2c_read(bq, reg);

	if (ret < 0)
		return sprintf(buf, "%#.2x=error %d\n", reg, ret);
	return sprintf(buf, "%#.2x=%#.2x\n", reg, ret);
}

/* show all raw values of chip register, format per line: 'register=value' */
static ssize_t bq2415x_sysfs_show_registers(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq2415x_device *bq = power_supply_get_drvdata(psy);
	ssize_t ret = 0;

	ret += bq2415x_sysfs_print_reg(bq, BQ2415X_REG_STATUS, buf+ret);
	ret += bq2415x_sysfs_print_reg(bq, BQ2415X_REG_CONTROL, buf+ret);
	ret += bq2415x_sysfs_print_reg(bq, BQ2415X_REG_VOLTAGE, buf+ret);
	ret += bq2415x_sysfs_print_reg(bq, BQ2415X_REG_VENDER, buf+ret);
	ret += bq2415x_sysfs_print_reg(bq, BQ2415X_REG_CURRENT, buf+ret);
	return ret;
}

/* set current and voltage limit entries (in mA or mV) */
static ssize_t bq2415x_sysfs_set_limit(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq2415x_device *bq = power_supply_get_drvdata(psy);
	long val;
	int ret;

	if (kstrtol(buf, 10, &val) < 0)
		return -EINVAL;

	if (strcmp(attr->attr.name, "current_limit") == 0)
		ret = bq2415x_set_current_limit(bq, val);
	else if (strcmp(attr->attr.name, "weak_battery_voltage") == 0)
		ret = bq2415x_set_weak_battery_voltage(bq, val);
	else if (strcmp(attr->attr.name, "battery_regulation_voltage") == 0)
		ret = bq2415x_set_battery_regulation_voltage(bq, val);
	else if (strcmp(attr->attr.name, "charge_current") == 0)
		ret = bq2415x_set_charge_current(bq, val);
	else if (strcmp(attr->attr.name, "termination_current") == 0)
		ret = bq2415x_set_termination_current(bq, val);
	else
		return -EINVAL;

	if (ret < 0)
		return ret;
	return count;
}

/* show current and voltage limit entries (in mA or mV) */
static ssize_t bq2415x_sysfs_show_limit(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq2415x_device *bq = power_supply_get_drvdata(psy);
	int ret;

	if (strcmp(attr->attr.name, "current_limit") == 0)
		ret = bq2415x_get_current_limit(bq);
	else if (strcmp(attr->attr.name, "weak_battery_voltage") == 0)
		ret = bq2415x_get_weak_battery_voltage(bq);
	else if (strcmp(attr->attr.name, "battery_regulation_voltage") == 0)
		ret = bq2415x_get_battery_regulation_voltage(bq);
	else if (strcmp(attr->attr.name, "charge_current") == 0)
		ret = bq2415x_get_charge_current(bq);
	else if (strcmp(attr->attr.name, "termination_current") == 0)
		ret = bq2415x_get_termination_current(bq);
	else
		return -EINVAL;

	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", ret);
}

/* set *_enable entries */
static ssize_t bq2415x_sysfs_set_enable(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq2415x_device *bq = power_supply_get_drvdata(psy);
	enum bq2415x_command command;
	long val;
	int ret;

	if (kstrtol(buf, 10, &val) < 0)
		return -EINVAL;

	if (strcmp(attr->attr.name, "charge_termination_enable") == 0)
		command = val ? BQ2415X_CHARGE_TERMINATION_ENABLE :
			BQ2415X_CHARGE_TERMINATION_DISABLE;
	else if (strcmp(attr->attr.name, "high_impedance_enable") == 0)
		command = val ? BQ2415X_HIGH_IMPEDANCE_ENABLE :
			BQ2415X_HIGH_IMPEDANCE_DISABLE;
	else if (strcmp(attr->attr.name, "otg_pin_enable") == 0)
		command = val ? BQ2415X_OTG_PIN_ENABLE :
			BQ2415X_OTG_PIN_DISABLE;
	else if (strcmp(attr->attr.name, "stat_pin_enable") == 0)
		command = val ? BQ2415X_STAT_PIN_ENABLE :
			BQ2415X_STAT_PIN_DISABLE;
	else
		return -EINVAL;

	ret = bq2415x_exec_command(bq, command);
	if (ret < 0)
		return ret;
	return count;
}

/* show *_enable entries */
static ssize_t bq2415x_sysfs_show_enable(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq2415x_device *bq = power_supply_get_drvdata(psy);
	enum bq2415x_command command;
	int ret;

	if (strcmp(attr->attr.name, "charge_termination_enable") == 0)
		command = BQ2415X_CHARGE_TERMINATION_STATUS;
	else if (strcmp(attr->attr.name, "high_impedance_enable") == 0)
		command = BQ2415X_HIGH_IMPEDANCE_STATUS;
	else if (strcmp(attr->attr.name, "otg_pin_enable") == 0)
		command = BQ2415X_OTG_PIN_STATUS;
	else if (strcmp(attr->attr.name, "stat_pin_enable") == 0)
		command = BQ2415X_STAT_PIN_STATUS;
	else
		return -EINVAL;

	ret = bq2415x_exec_command(bq, command);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", ret);
}

static DEVICE_ATTR(current_limit, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_limit, bq2415x_sysfs_set_limit);
static DEVICE_ATTR(weak_battery_voltage, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_limit, bq2415x_sysfs_set_limit);
static DEVICE_ATTR(battery_regulation_voltage, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_limit, bq2415x_sysfs_set_limit);
static DEVICE_ATTR(charge_current, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_limit, bq2415x_sysfs_set_limit);
static DEVICE_ATTR(termination_current, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_limit, bq2415x_sysfs_set_limit);

static DEVICE_ATTR(charge_termination_enable, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_enable, bq2415x_sysfs_set_enable);
static DEVICE_ATTR(high_impedance_enable, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_enable, bq2415x_sysfs_set_enable);
static DEVICE_ATTR(otg_pin_enable, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_enable, bq2415x_sysfs_set_enable);
static DEVICE_ATTR(stat_pin_enable, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_enable, bq2415x_sysfs_set_enable);

static DEVICE_ATTR(reported_mode, S_IRUGO,
		bq2415x_sysfs_show_reported_mode, NULL);
static DEVICE_ATTR(mode, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_mode, bq2415x_sysfs_set_mode);
static DEVICE_ATTR(timer, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_timer, bq2415x_sysfs_set_timer);

static DEVICE_ATTR(registers, S_IWUSR | S_IRUGO,
		bq2415x_sysfs_show_registers, bq2415x_sysfs_set_registers);

static DEVICE_ATTR(otg_status, S_IRUGO, bq2415x_sysfs_show_status, NULL);
static DEVICE_ATTR(charge_status, S_IRUGO, bq2415x_sysfs_show_status, NULL);
static DEVICE_ATTR(boost_status, S_IRUGO, bq2415x_sysfs_show_status, NULL);
static DEVICE_ATTR(fault_status, S_IRUGO, bq2415x_sysfs_show_status, NULL);

static struct attribute *bq2415x_sysfs_attributes[] = {
	/*
	 * TODO: some (appropriate) of these attrs should be switched to
	 * use power supply class props.
	 */
	&dev_attr_current_limit.attr,
	&dev_attr_weak_battery_voltage.attr,
	&dev_attr_battery_regulation_voltage.attr,
	&dev_attr_charge_current.attr,
	&dev_attr_termination_current.attr,

	&dev_attr_charge_termination_enable.attr,
	&dev_attr_high_impedance_enable.attr,
	&dev_attr_otg_pin_enable.attr,
	&dev_attr_stat_pin_enable.attr,

	&dev_attr_reported_mode.attr,
	&dev_attr_mode.attr,
	&dev_attr_timer.attr,

	&dev_attr_registers.attr,

	&dev_attr_otg_status.attr,
	&dev_attr_charge_status.attr,
	&dev_attr_boost_status.attr,
	&dev_attr_fault_status.attr,
	NULL,
};

static const struct attribute_group bq2415x_sysfs_attr_group = {
	.attrs = bq2415x_sysfs_attributes,
};

static int bq2415x_sysfs_init(struct bq2415x_device *bq)
{
	return sysfs_create_group(&bq->charger->dev.kobj,
			&bq2415x_sysfs_attr_group);
}

static void bq2415x_sysfs_exit(struct bq2415x_device *bq)
{
	sysfs_remove_group(&bq->charger->dev.kobj, &bq2415x_sysfs_attr_group);
}

/* main bq2415x probe function */
static int bq2415x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int ret;
	int num;
	char *name = NULL;
	struct bq2415x_device *bq;
	struct device_node *np = client->dev.of_node;
	struct bq2415x_platform_data *pdata = client->dev.platform_data;
	const struct acpi_device_id *acpi_id = NULL;
	struct power_supply *notify_psy = NULL;
	union power_supply_propval prop;

	if (!np && !pdata && !ACPI_HANDLE(&client->dev)) {
		dev_err(&client->dev, "Neither devicetree, nor platform data, nor ACPI support\n");
		return -ENODEV;
	}

	/* Get new ID for the new device */
	mutex_lock(&bq2415x_id_mutex);
	num = idr_alloc(&bq2415x_id, client, 0, 0, GFP_KERNEL);
	mutex_unlock(&bq2415x_id_mutex);
	if (num < 0)
		return num;

	if (id) {
		name = kasprintf(GFP_KERNEL, "%s-%d", id->name, num);
	} else if (ACPI_HANDLE(&client->dev)) {
		acpi_id =
			acpi_match_device(client->dev.driver->acpi_match_table,
					  &client->dev);
		name = kasprintf(GFP_KERNEL, "%s-%d", acpi_id->id, num);
	}
	if (!name) {
		dev_err(&client->dev, "failed to allocate device name\n");
		ret = -ENOMEM;
		goto error_1;
	}

	bq = devm_kzalloc(&client->dev, sizeof(*bq), GFP_KERNEL);
	if (!bq) {
		ret = -ENOMEM;
		goto error_2;
	}

	i2c_set_clientdata(client, bq);

	bq->id = num;
	bq->dev = &client->dev;
	if (id)
		bq->chip = id->driver_data;
	else if (ACPI_HANDLE(bq->dev))
		bq->chip = acpi_id->driver_data;
	bq->name = name;
	bq->mode = BQ2415X_MODE_OFF;
	bq->reported_mode = BQ2415X_MODE_OFF;
	bq->autotimer = 0;
	bq->automode = 0;

	if (np || ACPI_HANDLE(bq->dev)) {
		ret = device_property_read_u32(bq->dev,
					       "ti,current-limit",
					       &bq->init_data.current_limit);
		if (ret)
			goto error_2;
		ret = device_property_read_u32(bq->dev,
					"ti,weak-battery-voltage",
					&bq->init_data.weak_battery_voltage);
		if (ret)
			goto error_2;
		ret = device_property_read_u32(bq->dev,
				"ti,battery-regulation-voltage",
				&bq->init_data.battery_regulation_voltage);
		if (ret)
			goto error_2;
		ret = device_property_read_u32(bq->dev,
					       "ti,charge-current",
					       &bq->init_data.charge_current);
		if (ret)
			goto error_2;
		ret = device_property_read_u32(bq->dev,
				"ti,termination-current",
				&bq->init_data.termination_current);
		if (ret)
			goto error_2;
		ret = device_property_read_u32(bq->dev,
					       "ti,resistor-sense",
					       &bq->init_data.resistor_sense);
		if (ret)
			goto error_2;
		if (np)
			bq->notify_node = of_parse_phandle(np,
						"ti,usb-charger-detection", 0);
	} else {
		memcpy(&bq->init_data, pdata, sizeof(bq->init_data));
	}

	bq2415x_reset_chip(bq);

	ret = bq2415x_power_supply_init(bq);
	if (ret) {
		dev_err(bq->dev, "failed to register power supply: %d\n", ret);
		goto error_2;
	}

	ret = bq2415x_sysfs_init(bq);
	if (ret) {
		dev_err(bq->dev, "failed to create sysfs entries: %d\n", ret);
		goto error_3;
	}

	ret = bq2415x_set_defaults(bq);
	if (ret) {
		dev_err(bq->dev, "failed to set default values: %d\n", ret);
		goto error_4;
	}

	if (bq->notify_node || bq->init_data.notify_device) {
		bq->nb.notifier_call = bq2415x_notifier_call;
		ret = power_supply_reg_notifier(&bq->nb);
		if (ret) {
			dev_err(bq->dev, "failed to reg notifier: %d\n", ret);
			goto error_4;
		}

		bq->automode = 1;
		dev_info(bq->dev, "automode supported, waiting for events\n");
	} else {
		bq->automode = -1;
		dev_info(bq->dev, "automode not supported\n");
	}

	/* Query for initial reported_mode and set it */
	if (bq->nb.notifier_call) {
		if (np) {
			notify_psy = power_supply_get_by_phandle(np,
						"ti,usb-charger-detection");
			if (IS_ERR(notify_psy))
				notify_psy = NULL;
		} else if (bq->init_data.notify_device) {
			notify_psy = power_supply_get_by_name(
						bq->init_data.notify_device);
		}
	}
	if (notify_psy) {
		ret = power_supply_get_property(notify_psy,
					POWER_SUPPLY_PROP_CURRENT_MAX, &prop);
		power_supply_put(notify_psy);

		if (ret == 0) {
			bq2415x_update_reported_mode(bq, prop.intval);
			bq2415x_set_mode(bq, bq->reported_mode);
		}
	}

	INIT_DELAYED_WORK(&bq->work, bq2415x_timer_work);
	bq2415x_set_autotimer(bq, 1);

	dev_info(bq->dev, "driver registered\n");
	return 0;

error_4:
	bq2415x_sysfs_exit(bq);
error_3:
	bq2415x_power_supply_exit(bq);
error_2:
	if (bq)
		of_node_put(bq->notify_node);
	kfree(name);
error_1:
	mutex_lock(&bq2415x_id_mutex);
	idr_remove(&bq2415x_id, num);
	mutex_unlock(&bq2415x_id_mutex);

	return ret;
}

/* main bq2415x remove function */

static int bq2415x_remove(struct i2c_client *client)
{
	struct bq2415x_device *bq = i2c_get_clientdata(client);

	if (bq->nb.notifier_call)
		power_supply_unreg_notifier(&bq->nb);

	of_node_put(bq->notify_node);
	bq2415x_sysfs_exit(bq);
	bq2415x_power_supply_exit(bq);

	bq2415x_reset_chip(bq);

	mutex_lock(&bq2415x_id_mutex);
	idr_remove(&bq2415x_id, bq->id);
	mutex_unlock(&bq2415x_id_mutex);

	dev_info(bq->dev, "driver unregistered\n");

	kfree(bq->name);

	return 0;
}

static const struct i2c_device_id bq2415x_i2c_id_table[] = {
	{ "bq2415x", BQUNKNOWN },
	{ "bq24150", BQ24150 },
	{ "bq24150a", BQ24150A },
	{ "bq24151", BQ24151 },
	{ "bq24151a", BQ24151A },
	{ "bq24152", BQ24152 },
	{ "bq24153", BQ24153 },
	{ "bq24153a", BQ24153A },
	{ "bq24155", BQ24155 },
	{ "bq24156", BQ24156 },
	{ "bq24156a", BQ24156A },
	{ "bq24157s", BQ24157S },
	{ "bq24158", BQ24158 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq2415x_i2c_id_table);

#ifdef CONFIG_ACPI
static const struct acpi_device_id bq2415x_i2c_acpi_match[] = {
	{ "BQ2415X", BQUNKNOWN },
	{ "BQ241500", BQ24150 },
	{ "BQA24150", BQ24150A },
	{ "BQ241510", BQ24151 },
	{ "BQA24151", BQ24151A },
	{ "BQ241520", BQ24152 },
	{ "BQ241530", BQ24153 },
	{ "BQA24153", BQ24153A },
	{ "BQ241550", BQ24155 },
	{ "BQ241560", BQ24156 },
	{ "BQA24156", BQ24156A },
	{ "BQS24157", BQ24157S },
	{ "BQ241580", BQ24158 },
	{},
};
MODULE_DEVICE_TABLE(acpi, bq2415x_i2c_acpi_match);
#endif

#ifdef CONFIG_OF
static const struct of_device_id bq2415x_of_match_table[] = {
	{ .compatible = "ti,bq24150" },
	{ .compatible = "ti,bq24150a" },
	{ .compatible = "ti,bq24151" },
	{ .compatible = "ti,bq24151a" },
	{ .compatible = "ti,bq24152" },
	{ .compatible = "ti,bq24153" },
	{ .compatible = "ti,bq24153a" },
	{ .compatible = "ti,bq24155" },
	{ .compatible = "ti,bq24156" },
	{ .compatible = "ti,bq24156a" },
	{ .compatible = "ti,bq24157s" },
	{ .compatible = "ti,bq24158" },
	{},
};
MODULE_DEVICE_TABLE(of, bq2415x_of_match_table);
#endif

static struct i2c_driver bq2415x_driver = {
	.driver = {
		.name = "bq2415x-charger",
		.of_match_table = of_match_ptr(bq2415x_of_match_table),
		.acpi_match_table = ACPI_PTR(bq2415x_i2c_acpi_match),
	},
	.probe = bq2415x_probe,
	.remove = bq2415x_remove,
	.id_table = bq2415x_i2c_id_table,
};
module_i2c_driver(bq2415x_driver);

MODULE_AUTHOR("Pali Rohár <pali.rohar@gmail.com>");
MODULE_DESCRIPTION("bq2415x charger driver");
MODULE_LICENSE("GPL");
