// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Richtek RT9455WSC battery charger.
 *
 * Copyright (C) 2015 Intel Corporation
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/power_supply.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/usb/phy.h>
#include <linux/regmap.h>

#define RT9455_MANUFACTURER			"Richtek"
#define RT9455_MODEL_NAME			"RT9455"
#define RT9455_DRIVER_NAME			"rt9455-charger"

#define RT9455_IRQ_NAME				"interrupt"

#define RT9455_PWR_RDY_DELAY			1 /* 1 second */
#define RT9455_MAX_CHARGING_TIME		21600 /* 6 hrs */
#define RT9455_BATT_PRESENCE_DELAY		60 /* 60 seconds */

#define RT9455_CHARGE_MODE			0x00
#define RT9455_BOOST_MODE			0x01

#define RT9455_FAULT				0x03

#define RT9455_IAICR_100MA			0x00
#define RT9455_IAICR_500MA			0x01
#define RT9455_IAICR_NO_LIMIT			0x03

#define RT9455_CHARGE_DISABLE			0x00
#define RT9455_CHARGE_ENABLE			0x01

#define RT9455_PWR_FAULT			0x00
#define RT9455_PWR_GOOD				0x01

#define RT9455_REG_CTRL1			0x00 /* CTRL1 reg address */
#define RT9455_REG_CTRL2			0x01 /* CTRL2 reg address */
#define RT9455_REG_CTRL3			0x02 /* CTRL3 reg address */
#define RT9455_REG_DEV_ID			0x03 /* DEV_ID reg address */
#define RT9455_REG_CTRL4			0x04 /* CTRL4 reg address */
#define RT9455_REG_CTRL5			0x05 /* CTRL5 reg address */
#define RT9455_REG_CTRL6			0x06 /* CTRL6 reg address */
#define RT9455_REG_CTRL7			0x07 /* CTRL7 reg address */
#define RT9455_REG_IRQ1				0x08 /* IRQ1 reg address */
#define RT9455_REG_IRQ2				0x09 /* IRQ2 reg address */
#define RT9455_REG_IRQ3				0x0A /* IRQ3 reg address */
#define RT9455_REG_MASK1			0x0B /* MASK1 reg address */
#define RT9455_REG_MASK2			0x0C /* MASK2 reg address */
#define RT9455_REG_MASK3			0x0D /* MASK3 reg address */

enum rt9455_fields {
	F_STAT, F_BOOST, F_PWR_RDY, F_OTG_PIN_POLARITY, /* CTRL1 reg fields */

	F_IAICR, F_TE_SHDN_EN, F_HIGHER_OCP, F_TE, F_IAICR_INT, F_HIZ,
	F_OPA_MODE, /* CTRL2 reg fields */

	F_VOREG, F_OTG_PL, F_OTG_EN, /* CTRL3 reg fields */

	F_VENDOR_ID, F_CHIP_REV, /* DEV_ID reg fields */

	F_RST, /* CTRL4 reg fields */

	F_TMR_EN, F_MIVR, F_IPREC, F_IEOC_PERCENTAGE, /* CTRL5 reg fields*/

	F_IAICR_SEL, F_ICHRG, F_VPREC, /* CTRL6 reg fields */

	F_BATD_EN, F_CHG_EN, F_VMREG, /* CTRL7 reg fields */

	F_TSDI, F_VINOVPI, F_BATAB, /* IRQ1 reg fields */

	F_CHRVPI, F_CHBATOVI, F_CHTERMI, F_CHRCHGI, F_CH32MI, F_CHTREGI,
	F_CHMIVRI, /* IRQ2 reg fields */

	F_BSTBUSOVI, F_BSTOLI, F_BSTLOWVI, F_BST32SI, /* IRQ3 reg fields */

	F_TSDM, F_VINOVPIM, F_BATABM, /* MASK1 reg fields */

	F_CHRVPIM, F_CHBATOVIM, F_CHTERMIM, F_CHRCHGIM, F_CH32MIM, F_CHTREGIM,
	F_CHMIVRIM, /* MASK2 reg fields */

	F_BSTVINOVIM, F_BSTOLIM, F_BSTLOWVIM, F_BST32SIM, /* MASK3 reg fields */

	F_MAX_FIELDS
};

static const struct reg_field rt9455_reg_fields[] = {
	[F_STAT]		= REG_FIELD(RT9455_REG_CTRL1, 4, 5),
	[F_BOOST]		= REG_FIELD(RT9455_REG_CTRL1, 3, 3),
	[F_PWR_RDY]		= REG_FIELD(RT9455_REG_CTRL1, 2, 2),
	[F_OTG_PIN_POLARITY]	= REG_FIELD(RT9455_REG_CTRL1, 1, 1),

	[F_IAICR]		= REG_FIELD(RT9455_REG_CTRL2, 6, 7),
	[F_TE_SHDN_EN]		= REG_FIELD(RT9455_REG_CTRL2, 5, 5),
	[F_HIGHER_OCP]		= REG_FIELD(RT9455_REG_CTRL2, 4, 4),
	[F_TE]			= REG_FIELD(RT9455_REG_CTRL2, 3, 3),
	[F_IAICR_INT]		= REG_FIELD(RT9455_REG_CTRL2, 2, 2),
	[F_HIZ]			= REG_FIELD(RT9455_REG_CTRL2, 1, 1),
	[F_OPA_MODE]		= REG_FIELD(RT9455_REG_CTRL2, 0, 0),

	[F_VOREG]		= REG_FIELD(RT9455_REG_CTRL3, 2, 7),
	[F_OTG_PL]		= REG_FIELD(RT9455_REG_CTRL3, 1, 1),
	[F_OTG_EN]		= REG_FIELD(RT9455_REG_CTRL3, 0, 0),

	[F_VENDOR_ID]		= REG_FIELD(RT9455_REG_DEV_ID, 4, 7),
	[F_CHIP_REV]		= REG_FIELD(RT9455_REG_DEV_ID, 0, 3),

	[F_RST]			= REG_FIELD(RT9455_REG_CTRL4, 7, 7),

	[F_TMR_EN]		= REG_FIELD(RT9455_REG_CTRL5, 7, 7),
	[F_MIVR]		= REG_FIELD(RT9455_REG_CTRL5, 4, 5),
	[F_IPREC]		= REG_FIELD(RT9455_REG_CTRL5, 2, 3),
	[F_IEOC_PERCENTAGE]	= REG_FIELD(RT9455_REG_CTRL5, 0, 1),

	[F_IAICR_SEL]		= REG_FIELD(RT9455_REG_CTRL6, 7, 7),
	[F_ICHRG]		= REG_FIELD(RT9455_REG_CTRL6, 4, 6),
	[F_VPREC]		= REG_FIELD(RT9455_REG_CTRL6, 0, 2),

	[F_BATD_EN]		= REG_FIELD(RT9455_REG_CTRL7, 6, 6),
	[F_CHG_EN]		= REG_FIELD(RT9455_REG_CTRL7, 4, 4),
	[F_VMREG]		= REG_FIELD(RT9455_REG_CTRL7, 0, 3),

	[F_TSDI]		= REG_FIELD(RT9455_REG_IRQ1, 7, 7),
	[F_VINOVPI]		= REG_FIELD(RT9455_REG_IRQ1, 6, 6),
	[F_BATAB]		= REG_FIELD(RT9455_REG_IRQ1, 0, 0),

	[F_CHRVPI]		= REG_FIELD(RT9455_REG_IRQ2, 7, 7),
	[F_CHBATOVI]		= REG_FIELD(RT9455_REG_IRQ2, 5, 5),
	[F_CHTERMI]		= REG_FIELD(RT9455_REG_IRQ2, 4, 4),
	[F_CHRCHGI]		= REG_FIELD(RT9455_REG_IRQ2, 3, 3),
	[F_CH32MI]		= REG_FIELD(RT9455_REG_IRQ2, 2, 2),
	[F_CHTREGI]		= REG_FIELD(RT9455_REG_IRQ2, 1, 1),
	[F_CHMIVRI]		= REG_FIELD(RT9455_REG_IRQ2, 0, 0),

	[F_BSTBUSOVI]		= REG_FIELD(RT9455_REG_IRQ3, 7, 7),
	[F_BSTOLI]		= REG_FIELD(RT9455_REG_IRQ3, 6, 6),
	[F_BSTLOWVI]		= REG_FIELD(RT9455_REG_IRQ3, 5, 5),
	[F_BST32SI]		= REG_FIELD(RT9455_REG_IRQ3, 3, 3),

	[F_TSDM]		= REG_FIELD(RT9455_REG_MASK1, 7, 7),
	[F_VINOVPIM]		= REG_FIELD(RT9455_REG_MASK1, 6, 6),
	[F_BATABM]		= REG_FIELD(RT9455_REG_MASK1, 0, 0),

	[F_CHRVPIM]		= REG_FIELD(RT9455_REG_MASK2, 7, 7),
	[F_CHBATOVIM]		= REG_FIELD(RT9455_REG_MASK2, 5, 5),
	[F_CHTERMIM]		= REG_FIELD(RT9455_REG_MASK2, 4, 4),
	[F_CHRCHGIM]		= REG_FIELD(RT9455_REG_MASK2, 3, 3),
	[F_CH32MIM]		= REG_FIELD(RT9455_REG_MASK2, 2, 2),
	[F_CHTREGIM]		= REG_FIELD(RT9455_REG_MASK2, 1, 1),
	[F_CHMIVRIM]		= REG_FIELD(RT9455_REG_MASK2, 0, 0),

	[F_BSTVINOVIM]		= REG_FIELD(RT9455_REG_MASK3, 7, 7),
	[F_BSTOLIM]		= REG_FIELD(RT9455_REG_MASK3, 6, 6),
	[F_BSTLOWVIM]		= REG_FIELD(RT9455_REG_MASK3, 5, 5),
	[F_BST32SIM]		= REG_FIELD(RT9455_REG_MASK3, 3, 3),
};

#define GET_MASK(fid)	(BIT(rt9455_reg_fields[fid].msb + 1) - \
			 BIT(rt9455_reg_fields[fid].lsb))

/*
 * Each array initialised below shows the possible real-world values for a
 * group of bits belonging to RT9455 registers. The arrays are sorted in
 * ascending order. The index of each real-world value represents the value
 * that is encoded in the group of bits belonging to RT9455 registers.
 */
/* REG06[6:4] (ICHRG) in uAh */
static const int rt9455_ichrg_values[] = {
	 500000,  650000,  800000,  950000, 1100000, 1250000, 1400000, 1550000
};

/*
 * When the charger is in charge mode, REG02[7:2] represent battery regulation
 * voltage.
 */
/* REG02[7:2] (VOREG) in uV */
static const int rt9455_voreg_values[] = {
	3500000, 3520000, 3540000, 3560000, 3580000, 3600000, 3620000, 3640000,
	3660000, 3680000, 3700000, 3720000, 3740000, 3760000, 3780000, 3800000,
	3820000, 3840000, 3860000, 3880000, 3900000, 3920000, 3940000, 3960000,
	3980000, 4000000, 4020000, 4040000, 4060000, 4080000, 4100000, 4120000,
	4140000, 4160000, 4180000, 4200000, 4220000, 4240000, 4260000, 4280000,
	4300000, 4330000, 4350000, 4370000, 4390000, 4410000, 4430000, 4450000,
	4450000, 4450000, 4450000, 4450000, 4450000, 4450000, 4450000, 4450000,
	4450000, 4450000, 4450000, 4450000, 4450000, 4450000, 4450000, 4450000
};

/*
 * When the charger is in boost mode, REG02[7:2] represent boost output
 * voltage.
 */
/* REG02[7:2] (Boost output voltage) in uV */
static const int rt9455_boost_voltage_values[] = {
	4425000, 4450000, 4475000, 4500000, 4525000, 4550000, 4575000, 4600000,
	4625000, 4650000, 4675000, 4700000, 4725000, 4750000, 4775000, 4800000,
	4825000, 4850000, 4875000, 4900000, 4925000, 4950000, 4975000, 5000000,
	5025000, 5050000, 5075000, 5100000, 5125000, 5150000, 5175000, 5200000,
	5225000, 5250000, 5275000, 5300000, 5325000, 5350000, 5375000, 5400000,
	5425000, 5450000, 5475000, 5500000, 5525000, 5550000, 5575000, 5600000,
	5600000, 5600000, 5600000, 5600000, 5600000, 5600000, 5600000, 5600000,
	5600000, 5600000, 5600000, 5600000, 5600000, 5600000, 5600000, 5600000,
};

/* REG07[3:0] (VMREG) in uV */
static const int rt9455_vmreg_values[] = {
	4200000, 4220000, 4240000, 4260000, 4280000, 4300000, 4320000, 4340000,
	4360000, 4380000, 4400000, 4430000, 4450000, 4450000, 4450000, 4450000
};

/* REG05[5:4] (IEOC_PERCENTAGE) */
static const int rt9455_ieoc_percentage_values[] = {
	10, 30, 20, 30
};

/* REG05[1:0] (MIVR) in uV */
static const int rt9455_mivr_values[] = {
	4000000, 4250000, 4500000, 5000000
};

/* REG05[1:0] (IAICR) in uA */
static const int rt9455_iaicr_values[] = {
	100000, 500000, 1000000, 2000000
};

struct rt9455_info {
	struct i2c_client		*client;
	struct regmap			*regmap;
	struct regmap_field		*regmap_fields[F_MAX_FIELDS];
	struct power_supply		*charger;
#if IS_ENABLED(CONFIG_USB_PHY)
	struct usb_phy			*usb_phy;
	struct notifier_block		nb;
#endif
	struct delayed_work		pwr_rdy_work;
	struct delayed_work		max_charging_time_work;
	struct delayed_work		batt_presence_work;
	u32				voreg;
	u32				boost_voltage;
};

/*
 * Iterate through each element of the 'tbl' array until an element whose value
 * is greater than v is found. Return the index of the respective element,
 * or the index of the last element in the array, if no such element is found.
 */
static unsigned int rt9455_find_idx(const int tbl[], int tbl_size, int v)
{
	int i;

	/*
	 * No need to iterate until the last index in the table because
	 * if no element greater than v is found in the table,
	 * or if only the last element is greater than v,
	 * function returns the index of the last element.
	 */
	for (i = 0; i < tbl_size - 1; i++)
		if (v <= tbl[i])
			return i;

	return (tbl_size - 1);
}

static int rt9455_get_field_val(struct rt9455_info *info,
				enum rt9455_fields field,
				const int tbl[], int tbl_size, int *val)
{
	unsigned int v;
	int ret;

	ret = regmap_field_read(info->regmap_fields[field], &v);
	if (ret)
		return ret;

	v = (v >= tbl_size) ? (tbl_size - 1) : v;
	*val = tbl[v];

	return 0;
}

static int rt9455_set_field_val(struct rt9455_info *info,
				enum rt9455_fields field,
				const int tbl[], int tbl_size, int val)
{
	unsigned int idx = rt9455_find_idx(tbl, tbl_size, val);

	return regmap_field_write(info->regmap_fields[field], idx);
}

static int rt9455_register_reset(struct rt9455_info *info)
{
	struct device *dev = &info->client->dev;
	unsigned int v;
	int ret, limit = 100;

	ret = regmap_field_write(info->regmap_fields[F_RST], 0x01);
	if (ret) {
		dev_err(dev, "Failed to set RST bit\n");
		return ret;
	}

	/*
	 * To make sure that reset operation has finished, loop until RST bit
	 * is set to 0.
	 */
	do {
		ret = regmap_field_read(info->regmap_fields[F_RST], &v);
		if (ret) {
			dev_err(dev, "Failed to read RST bit\n");
			return ret;
		}

		if (!v)
			break;

		usleep_range(10, 100);
	} while (--limit);

	if (!limit)
		return -EIO;

	return 0;
}

/* Charger power supply property routines */
static enum power_supply_property rt9455_charger_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static char *rt9455_charger_supplied_to[] = {
	"main-battery",
};

static int rt9455_charger_get_status(struct rt9455_info *info,
				     union power_supply_propval *val)
{
	unsigned int v, pwr_rdy;
	int ret;

	ret = regmap_field_read(info->regmap_fields[F_PWR_RDY],
				&pwr_rdy);
	if (ret) {
		dev_err(&info->client->dev, "Failed to read PWR_RDY bit\n");
		return ret;
	}

	/*
	 * If PWR_RDY bit is unset, the battery is discharging. Otherwise,
	 * STAT bits value must be checked.
	 */
	if (!pwr_rdy) {
		val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		return 0;
	}

	ret = regmap_field_read(info->regmap_fields[F_STAT], &v);
	if (ret) {
		dev_err(&info->client->dev, "Failed to read STAT bits\n");
		return ret;
	}

	switch (v) {
	case 0:
		/*
		 * If PWR_RDY bit is set, but STAT bits value is 0, the charger
		 * may be in one of the following cases:
		 * 1. CHG_EN bit is 0.
		 * 2. CHG_EN bit is 1 but the battery is not connected.
		 * In any of these cases, POWER_SUPPLY_STATUS_NOT_CHARGING is
		 * returned.
		 */
		val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	case 1:
		val->intval = POWER_SUPPLY_STATUS_CHARGING;
		return 0;
	case 2:
		val->intval = POWER_SUPPLY_STATUS_FULL;
		return 0;
	default:
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		return 0;
	}
}

static int rt9455_charger_get_health(struct rt9455_info *info,
				     union power_supply_propval *val)
{
	struct device *dev = &info->client->dev;
	unsigned int v;
	int ret;

	val->intval = POWER_SUPPLY_HEALTH_GOOD;

	ret = regmap_read(info->regmap, RT9455_REG_IRQ1, &v);
	if (ret) {
		dev_err(dev, "Failed to read IRQ1 register\n");
		return ret;
	}

	if (v & GET_MASK(F_TSDI)) {
		val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		return 0;
	}
	if (v & GET_MASK(F_VINOVPI)) {
		val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		return 0;
	}
	if (v & GET_MASK(F_BATAB)) {
		val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		return 0;
	}

	ret = regmap_read(info->regmap, RT9455_REG_IRQ2, &v);
	if (ret) {
		dev_err(dev, "Failed to read IRQ2 register\n");
		return ret;
	}

	if (v & GET_MASK(F_CHBATOVI)) {
		val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		return 0;
	}
	if (v & GET_MASK(F_CH32MI)) {
		val->intval = POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE;
		return 0;
	}

	ret = regmap_read(info->regmap, RT9455_REG_IRQ3, &v);
	if (ret) {
		dev_err(dev, "Failed to read IRQ3 register\n");
		return ret;
	}

	if (v & GET_MASK(F_BSTBUSOVI)) {
		val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		return 0;
	}
	if (v & GET_MASK(F_BSTOLI)) {
		val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		return 0;
	}
	if (v & GET_MASK(F_BSTLOWVI)) {
		val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		return 0;
	}
	if (v & GET_MASK(F_BST32SI)) {
		val->intval = POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE;
		return 0;
	}

	ret = regmap_field_read(info->regmap_fields[F_STAT], &v);
	if (ret) {
		dev_err(dev, "Failed to read STAT bits\n");
		return ret;
	}

	if (v == RT9455_FAULT) {
		val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		return 0;
	}

	return 0;
}

static int rt9455_charger_get_battery_presence(struct rt9455_info *info,
					       union power_supply_propval *val)
{
	unsigned int v;
	int ret;

	ret = regmap_field_read(info->regmap_fields[F_BATAB], &v);
	if (ret) {
		dev_err(&info->client->dev, "Failed to read BATAB bit\n");
		return ret;
	}

	/*
	 * Since BATAB is 1 when battery is NOT present and 0 otherwise,
	 * !BATAB is returned.
	 */
	val->intval = !v;

	return 0;
}

static int rt9455_charger_get_online(struct rt9455_info *info,
				     union power_supply_propval *val)
{
	unsigned int v;
	int ret;

	ret = regmap_field_read(info->regmap_fields[F_PWR_RDY], &v);
	if (ret) {
		dev_err(&info->client->dev, "Failed to read PWR_RDY bit\n");
		return ret;
	}

	val->intval = (int)v;

	return 0;
}

static int rt9455_charger_get_current(struct rt9455_info *info,
				      union power_supply_propval *val)
{
	int curr;
	int ret;

	ret = rt9455_get_field_val(info, F_ICHRG,
				   rt9455_ichrg_values,
				   ARRAY_SIZE(rt9455_ichrg_values),
				   &curr);
	if (ret) {
		dev_err(&info->client->dev, "Failed to read ICHRG value\n");
		return ret;
	}

	val->intval = curr;

	return 0;
}

static int rt9455_charger_get_current_max(struct rt9455_info *info,
					  union power_supply_propval *val)
{
	int idx = ARRAY_SIZE(rt9455_ichrg_values) - 1;

	val->intval = rt9455_ichrg_values[idx];

	return 0;
}

static int rt9455_charger_get_voltage(struct rt9455_info *info,
				      union power_supply_propval *val)
{
	int voltage;
	int ret;

	ret = rt9455_get_field_val(info, F_VOREG,
				   rt9455_voreg_values,
				   ARRAY_SIZE(rt9455_voreg_values),
				   &voltage);
	if (ret) {
		dev_err(&info->client->dev, "Failed to read VOREG value\n");
		return ret;
	}

	val->intval = voltage;

	return 0;
}

static int rt9455_charger_get_voltage_max(struct rt9455_info *info,
					  union power_supply_propval *val)
{
	int idx = ARRAY_SIZE(rt9455_vmreg_values) - 1;

	val->intval = rt9455_vmreg_values[idx];

	return 0;
}

static int rt9455_charger_get_term_current(struct rt9455_info *info,
					   union power_supply_propval *val)
{
	struct device *dev = &info->client->dev;
	int ichrg, ieoc_percentage, ret;

	ret = rt9455_get_field_val(info, F_ICHRG,
				   rt9455_ichrg_values,
				   ARRAY_SIZE(rt9455_ichrg_values),
				   &ichrg);
	if (ret) {
		dev_err(dev, "Failed to read ICHRG value\n");
		return ret;
	}

	ret = rt9455_get_field_val(info, F_IEOC_PERCENTAGE,
				   rt9455_ieoc_percentage_values,
				   ARRAY_SIZE(rt9455_ieoc_percentage_values),
				   &ieoc_percentage);
	if (ret) {
		dev_err(dev, "Failed to read IEOC value\n");
		return ret;
	}

	val->intval = ichrg * ieoc_percentage / 100;

	return 0;
}

static int rt9455_charger_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct rt9455_info *info = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		return rt9455_charger_get_status(info, val);
	case POWER_SUPPLY_PROP_HEALTH:
		return rt9455_charger_get_health(info, val);
	case POWER_SUPPLY_PROP_PRESENT:
		return rt9455_charger_get_battery_presence(info, val);
	case POWER_SUPPLY_PROP_ONLINE:
		return rt9455_charger_get_online(info, val);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return rt9455_charger_get_current(info, val);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		return rt9455_charger_get_current_max(info, val);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return rt9455_charger_get_voltage(info, val);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		return rt9455_charger_get_voltage_max(info, val);
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return rt9455_charger_get_term_current(info, val);
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = RT9455_MODEL_NAME;
		return 0;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = RT9455_MANUFACTURER;
		return 0;
	default:
		return -ENODATA;
	}
}

static int rt9455_hw_init(struct rt9455_info *info, u32 ichrg,
			  u32 ieoc_percentage,
			  u32 mivr, u32 iaicr)
{
	struct device *dev = &info->client->dev;
	int idx, ret;

	ret = rt9455_register_reset(info);
	if (ret) {
		dev_err(dev, "Power On Reset failed\n");
		return ret;
	}

	/* Set TE bit in order to enable end of charge detection */
	ret = regmap_field_write(info->regmap_fields[F_TE], 1);
	if (ret) {
		dev_err(dev, "Failed to set TE bit\n");
		return ret;
	}

	/* Set TE_SHDN_EN bit in order to enable end of charge detection */
	ret = regmap_field_write(info->regmap_fields[F_TE_SHDN_EN], 1);
	if (ret) {
		dev_err(dev, "Failed to set TE_SHDN_EN bit\n");
		return ret;
	}

	/*
	 * Set BATD_EN bit in order to enable battery detection
	 * when charging is done
	 */
	ret = regmap_field_write(info->regmap_fields[F_BATD_EN], 1);
	if (ret) {
		dev_err(dev, "Failed to set BATD_EN bit\n");
		return ret;
	}

	/*
	 * Disable Safety Timer. In charge mode, this timer terminates charging
	 * if no read or write via I2C is done within 32 minutes. This timer
	 * avoids overcharging the baterry when the OS is not loaded and the
	 * charger is connected to a power source.
	 * In boost mode, this timer triggers BST32SI interrupt if no read or
	 * write via I2C is done within 32 seconds.
	 * When the OS is loaded and the charger driver is inserted, it is used
	 * delayed_work, named max_charging_time_work, to avoid overcharging
	 * the battery.
	 */
	ret = regmap_field_write(info->regmap_fields[F_TMR_EN], 0x00);
	if (ret) {
		dev_err(dev, "Failed to disable Safety Timer\n");
		return ret;
	}

	/* Set ICHRG to value retrieved from device-specific data */
	ret = rt9455_set_field_val(info, F_ICHRG,
				   rt9455_ichrg_values,
				   ARRAY_SIZE(rt9455_ichrg_values), ichrg);
	if (ret) {
		dev_err(dev, "Failed to set ICHRG value\n");
		return ret;
	}

	/* Set IEOC Percentage to value retrieved from device-specific data */
	ret = rt9455_set_field_val(info, F_IEOC_PERCENTAGE,
				   rt9455_ieoc_percentage_values,
				   ARRAY_SIZE(rt9455_ieoc_percentage_values),
				   ieoc_percentage);
	if (ret) {
		dev_err(dev, "Failed to set IEOC Percentage value\n");
		return ret;
	}

	/* Set VOREG to value retrieved from device-specific data */
	ret = rt9455_set_field_val(info, F_VOREG,
				   rt9455_voreg_values,
				   ARRAY_SIZE(rt9455_voreg_values),
				   info->voreg);
	if (ret) {
		dev_err(dev, "Failed to set VOREG value\n");
		return ret;
	}

	/* Set VMREG value to maximum (4.45V). */
	idx = ARRAY_SIZE(rt9455_vmreg_values) - 1;
	ret = rt9455_set_field_val(info, F_VMREG,
				   rt9455_vmreg_values,
				   ARRAY_SIZE(rt9455_vmreg_values),
				   rt9455_vmreg_values[idx]);
	if (ret) {
		dev_err(dev, "Failed to set VMREG value\n");
		return ret;
	}

	/*
	 * Set MIVR to value retrieved from device-specific data.
	 * If no value is specified, default value for MIVR is 4.5V.
	 */
	if (mivr == -1)
		mivr = 4500000;

	ret = rt9455_set_field_val(info, F_MIVR,
				   rt9455_mivr_values,
				   ARRAY_SIZE(rt9455_mivr_values), mivr);
	if (ret) {
		dev_err(dev, "Failed to set MIVR value\n");
		return ret;
	}

	/*
	 * Set IAICR to value retrieved from device-specific data.
	 * If no value is specified, default value for IAICR is 500 mA.
	 */
	if (iaicr == -1)
		iaicr = 500000;

	ret = rt9455_set_field_val(info, F_IAICR,
				   rt9455_iaicr_values,
				   ARRAY_SIZE(rt9455_iaicr_values), iaicr);
	if (ret) {
		dev_err(dev, "Failed to set IAICR value\n");
		return ret;
	}

	/*
	 * Set IAICR_INT bit so that IAICR value is determined by IAICR bits
	 * and not by OTG pin.
	 */
	ret = regmap_field_write(info->regmap_fields[F_IAICR_INT], 0x01);
	if (ret) {
		dev_err(dev, "Failed to set IAICR_INT bit\n");
		return ret;
	}

	/*
	 * Disable CHMIVRI interrupt. Because the driver sets MIVR value,
	 * CHMIVRI is triggered, but there is no action to be taken by the
	 * driver when CHMIVRI is triggered.
	 */
	ret = regmap_field_write(info->regmap_fields[F_CHMIVRIM], 0x01);
	if (ret) {
		dev_err(dev, "Failed to mask CHMIVRI interrupt\n");
		return ret;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_USB_PHY)
/*
 * Before setting the charger into boost mode, boost output voltage is
 * set. This is needed because boost output voltage may differ from battery
 * regulation voltage. F_VOREG bits represent either battery regulation voltage
 * or boost output voltage, depending on the mode the charger is. Both battery
 * regulation voltage and boost output voltage are read from DT/ACPI during
 * probe.
 */
static int rt9455_set_boost_voltage_before_boost_mode(struct rt9455_info *info)
{
	struct device *dev = &info->client->dev;
	int ret;

	ret = rt9455_set_field_val(info, F_VOREG,
				   rt9455_boost_voltage_values,
				   ARRAY_SIZE(rt9455_boost_voltage_values),
				   info->boost_voltage);
	if (ret) {
		dev_err(dev, "Failed to set boost output voltage value\n");
		return ret;
	}

	return 0;
}
#endif

/*
 * Before setting the charger into charge mode, battery regulation voltage is
 * set. This is needed because boost output voltage may differ from battery
 * regulation voltage. F_VOREG bits represent either battery regulation voltage
 * or boost output voltage, depending on the mode the charger is. Both battery
 * regulation voltage and boost output voltage are read from DT/ACPI during
 * probe.
 */
static int rt9455_set_voreg_before_charge_mode(struct rt9455_info *info)
{
	struct device *dev = &info->client->dev;
	int ret;

	ret = rt9455_set_field_val(info, F_VOREG,
				   rt9455_voreg_values,
				   ARRAY_SIZE(rt9455_voreg_values),
				   info->voreg);
	if (ret) {
		dev_err(dev, "Failed to set VOREG value\n");
		return ret;
	}

	return 0;
}

static int rt9455_irq_handler_check_irq1_register(struct rt9455_info *info,
						  bool *_is_battery_absent,
						  bool *_alert_userspace)
{
	unsigned int irq1, mask1, mask2;
	struct device *dev = &info->client->dev;
	bool is_battery_absent = false;
	bool alert_userspace = false;
	int ret;

	ret = regmap_read(info->regmap, RT9455_REG_IRQ1, &irq1);
	if (ret) {
		dev_err(dev, "Failed to read IRQ1 register\n");
		return ret;
	}

	ret = regmap_read(info->regmap, RT9455_REG_MASK1, &mask1);
	if (ret) {
		dev_err(dev, "Failed to read MASK1 register\n");
		return ret;
	}

	if (irq1 & GET_MASK(F_TSDI)) {
		dev_err(dev, "Thermal shutdown fault occurred\n");
		alert_userspace = true;
	}

	if (irq1 & GET_MASK(F_VINOVPI)) {
		dev_err(dev, "Overvoltage input occurred\n");
		alert_userspace = true;
	}

	if (irq1 & GET_MASK(F_BATAB)) {
		dev_err(dev, "Battery absence occurred\n");
		is_battery_absent = true;
		alert_userspace = true;

		if ((mask1 & GET_MASK(F_BATABM)) == 0) {
			ret = regmap_field_write(info->regmap_fields[F_BATABM],
						 0x01);
			if (ret) {
				dev_err(dev, "Failed to mask BATAB interrupt\n");
				return ret;
			}
		}

		ret = regmap_read(info->regmap, RT9455_REG_MASK2, &mask2);
		if (ret) {
			dev_err(dev, "Failed to read MASK2 register\n");
			return ret;
		}

		if (mask2 & GET_MASK(F_CHTERMIM)) {
			ret = regmap_field_write(
				info->regmap_fields[F_CHTERMIM], 0x00);
			if (ret) {
				dev_err(dev, "Failed to unmask CHTERMI interrupt\n");
				return ret;
			}
		}

		if (mask2 & GET_MASK(F_CHRCHGIM)) {
			ret = regmap_field_write(
				info->regmap_fields[F_CHRCHGIM], 0x00);
			if (ret) {
				dev_err(dev, "Failed to unmask CHRCHGI interrupt\n");
				return ret;
			}
		}

		/*
		 * When the battery is absent, max_charging_time_work is
		 * cancelled, since no charging is done.
		 */
		cancel_delayed_work_sync(&info->max_charging_time_work);
		/*
		 * Since no interrupt is triggered when the battery is
		 * reconnected, max_charging_time_work is not rescheduled.
		 * Therefore, batt_presence_work is scheduled to check whether
		 * the battery is still absent or not.
		 */
		queue_delayed_work(system_power_efficient_wq,
				   &info->batt_presence_work,
				   RT9455_BATT_PRESENCE_DELAY * HZ);
	}

	*_is_battery_absent = is_battery_absent;

	if (alert_userspace)
		*_alert_userspace = alert_userspace;

	return 0;
}

static int rt9455_irq_handler_check_irq2_register(struct rt9455_info *info,
						  bool is_battery_absent,
						  bool *_alert_userspace)
{
	unsigned int irq2, mask2;
	struct device *dev = &info->client->dev;
	bool alert_userspace = false;
	int ret;

	ret = regmap_read(info->regmap, RT9455_REG_IRQ2, &irq2);
	if (ret) {
		dev_err(dev, "Failed to read IRQ2 register\n");
		return ret;
	}

	ret = regmap_read(info->regmap, RT9455_REG_MASK2, &mask2);
	if (ret) {
		dev_err(dev, "Failed to read MASK2 register\n");
		return ret;
	}

	if (irq2 & GET_MASK(F_CHRVPI)) {
		dev_dbg(dev, "Charger fault occurred\n");
		/*
		 * CHRVPI bit is set in 2 cases:
		 * 1. when the power source is connected to the charger.
		 * 2. when the power source is disconnected from the charger.
		 * To identify the case, PWR_RDY bit is checked. Because
		 * PWR_RDY bit is set / cleared after CHRVPI interrupt is
		 * triggered, it is used delayed_work to later read PWR_RDY bit.
		 * Also, do not set to true alert_userspace, because there is no
		 * need to notify userspace when CHRVPI interrupt has occurred.
		 * Userspace will be notified after PWR_RDY bit is read.
		 */
		queue_delayed_work(system_power_efficient_wq,
				   &info->pwr_rdy_work,
				   RT9455_PWR_RDY_DELAY * HZ);
	}
	if (irq2 & GET_MASK(F_CHBATOVI)) {
		dev_err(dev, "Battery OVP occurred\n");
		alert_userspace = true;
	}
	if (irq2 & GET_MASK(F_CHTERMI)) {
		dev_dbg(dev, "Charge terminated\n");
		if (!is_battery_absent) {
			if ((mask2 & GET_MASK(F_CHTERMIM)) == 0) {
				ret = regmap_field_write(
					info->regmap_fields[F_CHTERMIM], 0x01);
				if (ret) {
					dev_err(dev, "Failed to mask CHTERMI interrupt\n");
					return ret;
				}
				/*
				 * Update MASK2 value, since CHTERMIM bit is
				 * set.
				 */
				mask2 = mask2 | GET_MASK(F_CHTERMIM);
			}
			cancel_delayed_work_sync(&info->max_charging_time_work);
			alert_userspace = true;
		}
	}
	if (irq2 & GET_MASK(F_CHRCHGI)) {
		dev_dbg(dev, "Recharge request\n");
		ret = regmap_field_write(info->regmap_fields[F_CHG_EN],
					 RT9455_CHARGE_ENABLE);
		if (ret) {
			dev_err(dev, "Failed to enable charging\n");
			return ret;
		}
		if (mask2 & GET_MASK(F_CHTERMIM)) {
			ret = regmap_field_write(
				info->regmap_fields[F_CHTERMIM], 0x00);
			if (ret) {
				dev_err(dev, "Failed to unmask CHTERMI interrupt\n");
				return ret;
			}
			/* Update MASK2 value, since CHTERMIM bit is cleared. */
			mask2 = mask2 & ~GET_MASK(F_CHTERMIM);
		}
		if (!is_battery_absent) {
			/*
			 * No need to check whether the charger is connected to
			 * power source when CHRCHGI is received, since CHRCHGI
			 * is not triggered if the charger is not connected to
			 * the power source.
			 */
			queue_delayed_work(system_power_efficient_wq,
					   &info->max_charging_time_work,
					   RT9455_MAX_CHARGING_TIME * HZ);
			alert_userspace = true;
		}
	}
	if (irq2 & GET_MASK(F_CH32MI)) {
		dev_err(dev, "Charger fault. 32 mins timeout occurred\n");
		alert_userspace = true;
	}
	if (irq2 & GET_MASK(F_CHTREGI)) {
		dev_warn(dev,
			 "Charger warning. Thermal regulation loop active\n");
		alert_userspace = true;
	}
	if (irq2 & GET_MASK(F_CHMIVRI)) {
		dev_dbg(dev,
			"Charger warning. Input voltage MIVR loop active\n");
	}

	if (alert_userspace)
		*_alert_userspace = alert_userspace;

	return 0;
}

static int rt9455_irq_handler_check_irq3_register(struct rt9455_info *info,
						  bool *_alert_userspace)
{
	unsigned int irq3, mask3;
	struct device *dev = &info->client->dev;
	bool alert_userspace = false;
	int ret;

	ret = regmap_read(info->regmap, RT9455_REG_IRQ3, &irq3);
	if (ret) {
		dev_err(dev, "Failed to read IRQ3 register\n");
		return ret;
	}

	ret = regmap_read(info->regmap, RT9455_REG_MASK3, &mask3);
	if (ret) {
		dev_err(dev, "Failed to read MASK3 register\n");
		return ret;
	}

	if (irq3 & GET_MASK(F_BSTBUSOVI)) {
		dev_err(dev, "Boost fault. Overvoltage input occurred\n");
		alert_userspace = true;
	}
	if (irq3 & GET_MASK(F_BSTOLI)) {
		dev_err(dev, "Boost fault. Overload\n");
		alert_userspace = true;
	}
	if (irq3 & GET_MASK(F_BSTLOWVI)) {
		dev_err(dev, "Boost fault. Battery voltage too low\n");
		alert_userspace = true;
	}
	if (irq3 & GET_MASK(F_BST32SI)) {
		dev_err(dev, "Boost fault. 32 seconds timeout occurred.\n");
		alert_userspace = true;
	}

	if (alert_userspace) {
		dev_info(dev, "Boost fault occurred, therefore the charger goes into charge mode\n");
		ret = rt9455_set_voreg_before_charge_mode(info);
		if (ret) {
			dev_err(dev, "Failed to set VOREG before entering charge mode\n");
			return ret;
		}
		ret = regmap_field_write(info->regmap_fields[F_OPA_MODE],
					 RT9455_CHARGE_MODE);
		if (ret) {
			dev_err(dev, "Failed to set charger in charge mode\n");
			return ret;
		}
		*_alert_userspace = alert_userspace;
	}

	return 0;
}

static irqreturn_t rt9455_irq_handler_thread(int irq, void *data)
{
	struct rt9455_info *info = data;
	struct device *dev;
	bool alert_userspace = false;
	bool is_battery_absent = false;
	unsigned int status;
	int ret;

	if (!info)
		return IRQ_NONE;

	dev = &info->client->dev;

	if (irq != info->client->irq) {
		dev_err(dev, "Interrupt is not for RT9455 charger\n");
		return IRQ_NONE;
	}

	ret = regmap_field_read(info->regmap_fields[F_STAT], &status);
	if (ret) {
		dev_err(dev, "Failed to read STAT bits\n");
		return IRQ_HANDLED;
	}
	dev_dbg(dev, "Charger status is %d\n", status);

	/*
	 * Each function that processes an IRQ register receives as output
	 * parameter alert_userspace pointer. alert_userspace is set to true
	 * in such a function only if an interrupt has occurred in the
	 * respective interrupt register. This way, it is avoided the following
	 * case: interrupt occurs only in IRQ1 register,
	 * rt9455_irq_handler_check_irq1_register() function sets to true
	 * alert_userspace, but rt9455_irq_handler_check_irq2_register()
	 * and rt9455_irq_handler_check_irq3_register() functions set to false
	 * alert_userspace and power_supply_changed() is never called.
	 */
	ret = rt9455_irq_handler_check_irq1_register(info, &is_battery_absent,
						     &alert_userspace);
	if (ret) {
		dev_err(dev, "Failed to handle IRQ1 register\n");
		return IRQ_HANDLED;
	}

	ret = rt9455_irq_handler_check_irq2_register(info, is_battery_absent,
						     &alert_userspace);
	if (ret) {
		dev_err(dev, "Failed to handle IRQ2 register\n");
		return IRQ_HANDLED;
	}

	ret = rt9455_irq_handler_check_irq3_register(info, &alert_userspace);
	if (ret) {
		dev_err(dev, "Failed to handle IRQ3 register\n");
		return IRQ_HANDLED;
	}

	if (alert_userspace) {
		/*
		 * Sometimes, an interrupt occurs while rt9455_probe() function
		 * is executing and power_supply_register() is not yet called.
		 * Do not call power_supply_changed() in this case.
		 */
		if (info->charger)
			power_supply_changed(info->charger);
	}

	return IRQ_HANDLED;
}

static int rt9455_discover_charger(struct rt9455_info *info, u32 *ichrg,
				   u32 *ieoc_percentage,
				   u32 *mivr, u32 *iaicr)
{
	struct device *dev = &info->client->dev;
	int ret;

	if (!dev->of_node && !ACPI_HANDLE(dev)) {
		dev_err(dev, "No support for either device tree or ACPI\n");
		return -EINVAL;
	}
	/*
	 * ICHRG, IEOC_PERCENTAGE, VOREG and boost output voltage are mandatory
	 * parameters.
	 */
	ret = device_property_read_u32(dev, "richtek,output-charge-current",
				       ichrg);
	if (ret) {
		dev_err(dev, "Error: missing \"output-charge-current\" property\n");
		return ret;
	}

	ret = device_property_read_u32(dev, "richtek,end-of-charge-percentage",
				       ieoc_percentage);
	if (ret) {
		dev_err(dev, "Error: missing \"end-of-charge-percentage\" property\n");
		return ret;
	}

	ret = device_property_read_u32(dev,
				       "richtek,battery-regulation-voltage",
				       &info->voreg);
	if (ret) {
		dev_err(dev, "Error: missing \"battery-regulation-voltage\" property\n");
		return ret;
	}

	ret = device_property_read_u32(dev, "richtek,boost-output-voltage",
				       &info->boost_voltage);
	if (ret) {
		dev_err(dev, "Error: missing \"boost-output-voltage\" property\n");
		return ret;
	}

	/*
	 * MIVR and IAICR are optional parameters. Do not return error if one of
	 * them is not present in ACPI table or device tree specification.
	 */
	device_property_read_u32(dev, "richtek,min-input-voltage-regulation",
				 mivr);
	device_property_read_u32(dev, "richtek,avg-input-current-regulation",
				 iaicr);

	return 0;
}

#if IS_ENABLED(CONFIG_USB_PHY)
static int rt9455_usb_event_none(struct rt9455_info *info,
				 u8 opa_mode, u8 iaicr)
{
	struct device *dev = &info->client->dev;
	int ret;

	if (opa_mode == RT9455_BOOST_MODE) {
		ret = rt9455_set_voreg_before_charge_mode(info);
		if (ret) {
			dev_err(dev, "Failed to set VOREG before entering charge mode\n");
			return ret;
		}
		/*
		 * If the charger is in boost mode, and it has received
		 * USB_EVENT_NONE, this means the consumer device powered by the
		 * charger is not connected anymore.
		 * In this case, the charger goes into charge mode.
		 */
		dev_dbg(dev, "USB_EVENT_NONE received, therefore the charger goes into charge mode\n");
		ret = regmap_field_write(info->regmap_fields[F_OPA_MODE],
					 RT9455_CHARGE_MODE);
		if (ret) {
			dev_err(dev, "Failed to set charger in charge mode\n");
			return NOTIFY_DONE;
		}
	}

	dev_dbg(dev, "USB_EVENT_NONE received, therefore IAICR is set to its minimum value\n");
	if (iaicr != RT9455_IAICR_100MA) {
		ret = regmap_field_write(info->regmap_fields[F_IAICR],
					 RT9455_IAICR_100MA);
		if (ret) {
			dev_err(dev, "Failed to set IAICR value\n");
			return NOTIFY_DONE;
		}
	}

	return NOTIFY_OK;
}

static int rt9455_usb_event_vbus(struct rt9455_info *info,
				 u8 opa_mode, u8 iaicr)
{
	struct device *dev = &info->client->dev;
	int ret;

	if (opa_mode == RT9455_BOOST_MODE) {
		ret = rt9455_set_voreg_before_charge_mode(info);
		if (ret) {
			dev_err(dev, "Failed to set VOREG before entering charge mode\n");
			return ret;
		}
		/*
		 * If the charger is in boost mode, and it has received
		 * USB_EVENT_VBUS, this means the consumer device powered by the
		 * charger is not connected anymore.
		 * In this case, the charger goes into charge mode.
		 */
		dev_dbg(dev, "USB_EVENT_VBUS received, therefore the charger goes into charge mode\n");
		ret = regmap_field_write(info->regmap_fields[F_OPA_MODE],
					 RT9455_CHARGE_MODE);
		if (ret) {
			dev_err(dev, "Failed to set charger in charge mode\n");
			return NOTIFY_DONE;
		}
	}

	dev_dbg(dev, "USB_EVENT_VBUS received, therefore IAICR is set to 500 mA\n");
	if (iaicr != RT9455_IAICR_500MA) {
		ret = regmap_field_write(info->regmap_fields[F_IAICR],
					 RT9455_IAICR_500MA);
		if (ret) {
			dev_err(dev, "Failed to set IAICR value\n");
			return NOTIFY_DONE;
		}
	}

	return NOTIFY_OK;
}

static int rt9455_usb_event_id(struct rt9455_info *info,
			       u8 opa_mode, u8 iaicr)
{
	struct device *dev = &info->client->dev;
	int ret;

	if (opa_mode == RT9455_CHARGE_MODE) {
		ret = rt9455_set_boost_voltage_before_boost_mode(info);
		if (ret) {
			dev_err(dev, "Failed to set boost output voltage before entering boost mode\n");
			return ret;
		}
		/*
		 * If the charger is in charge mode, and it has received
		 * USB_EVENT_ID, this means a consumer device is connected and
		 * it should be powered by the charger.
		 * In this case, the charger goes into boost mode.
		 */
		dev_dbg(dev, "USB_EVENT_ID received, therefore the charger goes into boost mode\n");
		ret = regmap_field_write(info->regmap_fields[F_OPA_MODE],
					 RT9455_BOOST_MODE);
		if (ret) {
			dev_err(dev, "Failed to set charger in boost mode\n");
			return NOTIFY_DONE;
		}
	}

	dev_dbg(dev, "USB_EVENT_ID received, therefore IAICR is set to its minimum value\n");
	if (iaicr != RT9455_IAICR_100MA) {
		ret = regmap_field_write(info->regmap_fields[F_IAICR],
					 RT9455_IAICR_100MA);
		if (ret) {
			dev_err(dev, "Failed to set IAICR value\n");
			return NOTIFY_DONE;
		}
	}

	return NOTIFY_OK;
}

static int rt9455_usb_event_charger(struct rt9455_info *info,
				    u8 opa_mode, u8 iaicr)
{
	struct device *dev = &info->client->dev;
	int ret;

	if (opa_mode == RT9455_BOOST_MODE) {
		ret = rt9455_set_voreg_before_charge_mode(info);
		if (ret) {
			dev_err(dev, "Failed to set VOREG before entering charge mode\n");
			return ret;
		}
		/*
		 * If the charger is in boost mode, and it has received
		 * USB_EVENT_CHARGER, this means the consumer device powered by
		 * the charger is not connected anymore.
		 * In this case, the charger goes into charge mode.
		 */
		dev_dbg(dev, "USB_EVENT_CHARGER received, therefore the charger goes into charge mode\n");
		ret = regmap_field_write(info->regmap_fields[F_OPA_MODE],
					 RT9455_CHARGE_MODE);
		if (ret) {
			dev_err(dev, "Failed to set charger in charge mode\n");
			return NOTIFY_DONE;
		}
	}

	dev_dbg(dev, "USB_EVENT_CHARGER received, therefore IAICR is set to no current limit\n");
	if (iaicr != RT9455_IAICR_NO_LIMIT) {
		ret = regmap_field_write(info->regmap_fields[F_IAICR],
					 RT9455_IAICR_NO_LIMIT);
		if (ret) {
			dev_err(dev, "Failed to set IAICR value\n");
			return NOTIFY_DONE;
		}
	}

	return NOTIFY_OK;
}

static int rt9455_usb_event(struct notifier_block *nb,
			    unsigned long event, void *power)
{
	struct rt9455_info *info = container_of(nb, struct rt9455_info, nb);
	struct device *dev = &info->client->dev;
	unsigned int opa_mode, iaicr;
	int ret;

	/*
	 * Determine whether the charger is in charge mode
	 * or in boost mode.
	 */
	ret = regmap_field_read(info->regmap_fields[F_OPA_MODE],
				&opa_mode);
	if (ret) {
		dev_err(dev, "Failed to read OPA_MODE value\n");
		return NOTIFY_DONE;
	}

	ret = regmap_field_read(info->regmap_fields[F_IAICR],
				&iaicr);
	if (ret) {
		dev_err(dev, "Failed to read IAICR value\n");
		return NOTIFY_DONE;
	}

	dev_dbg(dev, "Received USB event %lu\n", event);
	switch (event) {
	case USB_EVENT_NONE:
		return rt9455_usb_event_none(info, opa_mode, iaicr);
	case USB_EVENT_VBUS:
		return rt9455_usb_event_vbus(info, opa_mode, iaicr);
	case USB_EVENT_ID:
		return rt9455_usb_event_id(info, opa_mode, iaicr);
	case USB_EVENT_CHARGER:
		return rt9455_usb_event_charger(info, opa_mode, iaicr);
	default:
		dev_err(dev, "Unknown USB event\n");
	}
	return NOTIFY_DONE;
}
#endif

static void rt9455_pwr_rdy_work_callback(struct work_struct *work)
{
	struct rt9455_info *info = container_of(work, struct rt9455_info,
						pwr_rdy_work.work);
	struct device *dev = &info->client->dev;
	unsigned int pwr_rdy;
	int ret;

	ret = regmap_field_read(info->regmap_fields[F_PWR_RDY], &pwr_rdy);
	if (ret) {
		dev_err(dev, "Failed to read PWR_RDY bit\n");
		return;
	}
	switch (pwr_rdy) {
	case RT9455_PWR_FAULT:
		dev_dbg(dev, "Charger disconnected from power source\n");
		cancel_delayed_work_sync(&info->max_charging_time_work);
		break;
	case RT9455_PWR_GOOD:
		dev_dbg(dev, "Charger connected to power source\n");
		ret = regmap_field_write(info->regmap_fields[F_CHG_EN],
					 RT9455_CHARGE_ENABLE);
		if (ret) {
			dev_err(dev, "Failed to enable charging\n");
			return;
		}
		queue_delayed_work(system_power_efficient_wq,
				   &info->max_charging_time_work,
				   RT9455_MAX_CHARGING_TIME * HZ);
		break;
	}
	/*
	 * Notify userspace that the charger has been either connected to or
	 * disconnected from the power source.
	 */
	power_supply_changed(info->charger);
}

static void rt9455_max_charging_time_work_callback(struct work_struct *work)
{
	struct rt9455_info *info = container_of(work, struct rt9455_info,
						max_charging_time_work.work);
	struct device *dev = &info->client->dev;
	int ret;

	dev_err(dev, "Battery has been charging for at least 6 hours and is not yet fully charged. Battery is dead, therefore charging is disabled.\n");
	ret = regmap_field_write(info->regmap_fields[F_CHG_EN],
				 RT9455_CHARGE_DISABLE);
	if (ret)
		dev_err(dev, "Failed to disable charging\n");
}

static void rt9455_batt_presence_work_callback(struct work_struct *work)
{
	struct rt9455_info *info = container_of(work, struct rt9455_info,
						batt_presence_work.work);
	struct device *dev = &info->client->dev;
	unsigned int irq1, mask1;
	int ret;

	ret = regmap_read(info->regmap, RT9455_REG_IRQ1, &irq1);
	if (ret) {
		dev_err(dev, "Failed to read IRQ1 register\n");
		return;
	}

	/*
	 * If the battery is still absent, batt_presence_work is rescheduled.
	 * Otherwise, max_charging_time is scheduled.
	 */
	if (irq1 & GET_MASK(F_BATAB)) {
		queue_delayed_work(system_power_efficient_wq,
				   &info->batt_presence_work,
				   RT9455_BATT_PRESENCE_DELAY * HZ);
	} else {
		queue_delayed_work(system_power_efficient_wq,
				   &info->max_charging_time_work,
				   RT9455_MAX_CHARGING_TIME * HZ);

		ret = regmap_read(info->regmap, RT9455_REG_MASK1, &mask1);
		if (ret) {
			dev_err(dev, "Failed to read MASK1 register\n");
			return;
		}

		if (mask1 & GET_MASK(F_BATABM)) {
			ret = regmap_field_write(info->regmap_fields[F_BATABM],
						 0x00);
			if (ret)
				dev_err(dev, "Failed to unmask BATAB interrupt\n");
		}
		/*
		 * Notify userspace that the battery is now connected to the
		 * charger.
		 */
		power_supply_changed(info->charger);
	}
}

static const struct power_supply_desc rt9455_charger_desc = {
	.name			= RT9455_DRIVER_NAME,
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= rt9455_charger_properties,
	.num_properties		= ARRAY_SIZE(rt9455_charger_properties),
	.get_property		= rt9455_charger_get_property,
};

static bool rt9455_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT9455_REG_DEV_ID:
	case RT9455_REG_IRQ1:
	case RT9455_REG_IRQ2:
	case RT9455_REG_IRQ3:
		return false;
	default:
		return true;
	}
}

static bool rt9455_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT9455_REG_DEV_ID:
	case RT9455_REG_CTRL5:
	case RT9455_REG_CTRL6:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config rt9455_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.writeable_reg	= rt9455_is_writeable_reg,
	.volatile_reg	= rt9455_is_volatile_reg,
	.max_register	= RT9455_REG_MASK3,
	.cache_type	= REGCACHE_RBTREE,
};

static int rt9455_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct device *dev = &client->dev;
	struct rt9455_info *info;
	struct power_supply_config rt9455_charger_config = {};
	/*
	 * Mandatory device-specific data values. Also, VOREG and boost output
	 * voltage are mandatory values, but they are stored in rt9455_info
	 * structure.
	 */
	u32 ichrg, ieoc_percentage;
	/* Optional device-specific data values. */
	u32 mivr = -1, iaicr = -1;
	int i, ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "No support for SMBUS_BYTE_DATA\n");
		return -ENODEV;
	}
	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->client = client;
	i2c_set_clientdata(client, info);

	info->regmap = devm_regmap_init_i2c(client,
					    &rt9455_regmap_config);
	if (IS_ERR(info->regmap)) {
		dev_err(dev, "Failed to initialize register map\n");
		return -EINVAL;
	}

	for (i = 0; i < F_MAX_FIELDS; i++) {
		info->regmap_fields[i] =
			devm_regmap_field_alloc(dev, info->regmap,
						rt9455_reg_fields[i]);
		if (IS_ERR(info->regmap_fields[i])) {
			dev_err(dev,
				"Failed to allocate regmap field = %d\n", i);
			return PTR_ERR(info->regmap_fields[i]);
		}
	}

	ret = rt9455_discover_charger(info, &ichrg, &ieoc_percentage,
				      &mivr, &iaicr);
	if (ret) {
		dev_err(dev, "Failed to discover charger\n");
		return ret;
	}

#if IS_ENABLED(CONFIG_USB_PHY)
	info->usb_phy = devm_usb_get_phy(dev, USB_PHY_TYPE_USB2);
	if (IS_ERR(info->usb_phy)) {
		dev_err(dev, "Failed to get USB transceiver\n");
	} else {
		info->nb.notifier_call = rt9455_usb_event;
		ret = usb_register_notifier(info->usb_phy, &info->nb);
		if (ret) {
			dev_err(dev, "Failed to register USB notifier\n");
			/*
			 * If usb_register_notifier() fails, set notifier_call
			 * to NULL, to avoid calling usb_unregister_notifier().
			 */
			info->nb.notifier_call = NULL;
		}
	}
#endif

	INIT_DEFERRABLE_WORK(&info->pwr_rdy_work, rt9455_pwr_rdy_work_callback);
	INIT_DEFERRABLE_WORK(&info->max_charging_time_work,
			     rt9455_max_charging_time_work_callback);
	INIT_DEFERRABLE_WORK(&info->batt_presence_work,
			     rt9455_batt_presence_work_callback);

	rt9455_charger_config.of_node		= dev->of_node;
	rt9455_charger_config.drv_data		= info;
	rt9455_charger_config.supplied_to	= rt9455_charger_supplied_to;
	rt9455_charger_config.num_supplicants	=
					ARRAY_SIZE(rt9455_charger_supplied_to);
	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					rt9455_irq_handler_thread,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					RT9455_DRIVER_NAME, info);
	if (ret) {
		dev_err(dev, "Failed to register IRQ handler\n");
		goto put_usb_notifier;
	}

	ret = rt9455_hw_init(info, ichrg, ieoc_percentage, mivr, iaicr);
	if (ret) {
		dev_err(dev, "Failed to set charger to its default values\n");
		goto put_usb_notifier;
	}

	info->charger = devm_power_supply_register(dev, &rt9455_charger_desc,
						   &rt9455_charger_config);
	if (IS_ERR(info->charger)) {
		dev_err(dev, "Failed to register charger\n");
		ret = PTR_ERR(info->charger);
		goto put_usb_notifier;
	}

	return 0;

put_usb_notifier:
#if IS_ENABLED(CONFIG_USB_PHY)
	if (info->nb.notifier_call)  {
		usb_unregister_notifier(info->usb_phy, &info->nb);
		info->nb.notifier_call = NULL;
	}
#endif
	return ret;
}

static int rt9455_remove(struct i2c_client *client)
{
	int ret;
	struct rt9455_info *info = i2c_get_clientdata(client);

	ret = rt9455_register_reset(info);
	if (ret)
		dev_err(&info->client->dev, "Failed to set charger to its default values\n");

#if IS_ENABLED(CONFIG_USB_PHY)
	if (info->nb.notifier_call)
		usb_unregister_notifier(info->usb_phy, &info->nb);
#endif

	cancel_delayed_work_sync(&info->pwr_rdy_work);
	cancel_delayed_work_sync(&info->max_charging_time_work);
	cancel_delayed_work_sync(&info->batt_presence_work);

	return 0;
}

static const struct i2c_device_id rt9455_i2c_id_table[] = {
	{ RT9455_DRIVER_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, rt9455_i2c_id_table);

static const struct of_device_id rt9455_of_match[] = {
	{ .compatible = "richtek,rt9455", },
	{ },
};
MODULE_DEVICE_TABLE(of, rt9455_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id rt9455_i2c_acpi_match[] = {
	{ "RT945500", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, rt9455_i2c_acpi_match);
#endif

static struct i2c_driver rt9455_driver = {
	.probe		= rt9455_probe,
	.remove		= rt9455_remove,
	.id_table	= rt9455_i2c_id_table,
	.driver = {
		.name		= RT9455_DRIVER_NAME,
		.of_match_table	= of_match_ptr(rt9455_of_match),
		.acpi_match_table = ACPI_PTR(rt9455_i2c_acpi_match),
	},
};
module_i2c_driver(rt9455_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anda-Maria Nicolae <anda-maria.nicolae@intel.com>");
MODULE_DESCRIPTION("Richtek RT9455 Charger Driver");
