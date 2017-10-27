/* Copyright (c) 2014, Sony Mobile Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This driver is for the multi-block Switch-Mode Battery Charger and Boost
 * (SMBB) hardware, found in Qualcomm PM8941 PMICs.  The charger is an
 * integrated, single-cell lithium-ion battery charger.
 *
 * Sub-components:
 *  - Charger core
 *  - Buck
 *  - DC charge-path
 *  - USB charge-path
 *  - Battery interface
 *  - Boost (not implemented)
 *  - Misc
 *  - HF-Buck
 */

#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/extcon-provider.h>
#include <linux/regulator/driver.h>

#define SMBB_CHG_VMAX		0x040
#define SMBB_CHG_VSAFE		0x041
#define SMBB_CHG_CFG		0x043
#define SMBB_CHG_IMAX		0x044
#define SMBB_CHG_ISAFE		0x045
#define SMBB_CHG_VIN_MIN	0x047
#define SMBB_CHG_CTRL		0x049
#define CTRL_EN			BIT(7)
#define SMBB_CHG_VBAT_WEAK	0x052
#define SMBB_CHG_IBAT_TERM_CHG	0x05b
#define IBAT_TERM_CHG_IEOC	BIT(7)
#define IBAT_TERM_CHG_IEOC_BMS	BIT(7)
#define IBAT_TERM_CHG_IEOC_CHG	0
#define SMBB_CHG_VBAT_DET	0x05d
#define SMBB_CHG_TCHG_MAX_EN	0x060
#define TCHG_MAX_EN		BIT(7)
#define SMBB_CHG_WDOG_TIME	0x062
#define SMBB_CHG_WDOG_EN	0x065
#define WDOG_EN			BIT(7)

#define SMBB_BUCK_REG_MODE	0x174
#define BUCK_REG_MODE		BIT(0)
#define BUCK_REG_MODE_VBAT	BIT(0)
#define BUCK_REG_MODE_VSYS	0

#define SMBB_BAT_PRES_STATUS	0x208
#define PRES_STATUS_BAT_PRES	BIT(7)
#define SMBB_BAT_TEMP_STATUS	0x209
#define TEMP_STATUS_OK		BIT(7)
#define TEMP_STATUS_HOT		BIT(6)
#define SMBB_BAT_BTC_CTRL	0x249
#define BTC_CTRL_COMP_EN	BIT(7)
#define BTC_CTRL_COLD_EXT	BIT(1)
#define BTC_CTRL_HOT_EXT_N	BIT(0)

#define SMBB_USB_IMAX		0x344
#define SMBB_USB_OTG_CTL	0x348
#define OTG_CTL_EN		BIT(0)
#define SMBB_USB_ENUM_TIMER_STOP 0x34e
#define ENUM_TIMER_STOP		BIT(0)
#define SMBB_USB_SEC_ACCESS	0x3d0
#define SEC_ACCESS_MAGIC	0xa5
#define SMBB_USB_REV_BST	0x3ed
#define REV_BST_CHG_GONE	BIT(7)

#define SMBB_DC_IMAX		0x444

#define SMBB_MISC_REV2		0x601
#define SMBB_MISC_BOOT_DONE	0x642
#define BOOT_DONE		BIT(7)

#define STATUS_USBIN_VALID	BIT(0) /* USB connection is valid */
#define STATUS_DCIN_VALID	BIT(1) /* DC connection is valid */
#define STATUS_BAT_HOT		BIT(2) /* Battery temp 1=Hot, 0=Cold */
#define STATUS_BAT_OK		BIT(3) /* Battery temp OK */
#define STATUS_BAT_PRESENT	BIT(4) /* Battery is present */
#define STATUS_CHG_DONE		BIT(5) /* Charge cycle is complete */
#define STATUS_CHG_TRKL		BIT(6) /* Trickle charging */
#define STATUS_CHG_FAST		BIT(7) /* Fast charging */
#define STATUS_CHG_GONE		BIT(8) /* No charger is connected */

enum smbb_attr {
	ATTR_BAT_ISAFE,
	ATTR_BAT_IMAX,
	ATTR_USBIN_IMAX,
	ATTR_DCIN_IMAX,
	ATTR_BAT_VSAFE,
	ATTR_BAT_VMAX,
	ATTR_BAT_VMIN,
	ATTR_CHG_VDET,
	ATTR_VIN_MIN,
	_ATTR_CNT,
};

struct smbb_charger {
	unsigned int revision;
	unsigned int addr;
	struct device *dev;
	struct extcon_dev *edev;

	bool dc_disabled;
	bool jeita_ext_temp;
	unsigned long status;
	struct mutex statlock;

	unsigned int attr[_ATTR_CNT];

	struct power_supply *usb_psy;
	struct power_supply *dc_psy;
	struct power_supply *bat_psy;
	struct regmap *regmap;

	struct regulator_desc otg_rdesc;
	struct regulator_dev *otg_reg;
};

static const unsigned int smbb_usb_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_NONE,
};

static int smbb_vbat_weak_fn(unsigned int index)
{
	return 2100000 + index * 100000;
}

static int smbb_vin_fn(unsigned int index)
{
	if (index > 42)
		return 5600000 + (index - 43) * 200000;
	return 3400000 + index * 50000;
}

static int smbb_vmax_fn(unsigned int index)
{
	return 3240000 + index * 10000;
}

static int smbb_vbat_det_fn(unsigned int index)
{
	return 3240000 + index * 20000;
}

static int smbb_imax_fn(unsigned int index)
{
	if (index < 2)
		return 100000 + index * 50000;
	return index * 100000;
}

static int smbb_bat_imax_fn(unsigned int index)
{
	return index * 50000;
}

static unsigned int smbb_hw_lookup(unsigned int val, int (*fn)(unsigned int))
{
	unsigned int widx;
	unsigned int sel;

	for (widx = sel = 0; (*fn)(widx) <= val; ++widx)
		sel = widx;

	return sel;
}

static const struct smbb_charger_attr {
	const char *name;
	unsigned int reg;
	unsigned int safe_reg;
	unsigned int max;
	unsigned int min;
	unsigned int fail_ok;
	int (*hw_fn)(unsigned int);
} smbb_charger_attrs[] = {
	[ATTR_BAT_ISAFE] = {
		.name = "qcom,fast-charge-safe-current",
		.reg = SMBB_CHG_ISAFE,
		.max = 3000000,
		.min = 200000,
		.hw_fn = smbb_bat_imax_fn,
		.fail_ok = 1,
	},
	[ATTR_BAT_IMAX] = {
		.name = "qcom,fast-charge-current-limit",
		.reg = SMBB_CHG_IMAX,
		.safe_reg = SMBB_CHG_ISAFE,
		.max = 3000000,
		.min = 200000,
		.hw_fn = smbb_bat_imax_fn,
	},
	[ATTR_DCIN_IMAX] = {
		.name = "qcom,dc-current-limit",
		.reg = SMBB_DC_IMAX,
		.max = 2500000,
		.min = 100000,
		.hw_fn = smbb_imax_fn,
	},
	[ATTR_BAT_VSAFE] = {
		.name = "qcom,fast-charge-safe-voltage",
		.reg = SMBB_CHG_VSAFE,
		.max = 5000000,
		.min = 3240000,
		.hw_fn = smbb_vmax_fn,
		.fail_ok = 1,
	},
	[ATTR_BAT_VMAX] = {
		.name = "qcom,fast-charge-high-threshold-voltage",
		.reg = SMBB_CHG_VMAX,
		.safe_reg = SMBB_CHG_VSAFE,
		.max = 5000000,
		.min = 3240000,
		.hw_fn = smbb_vmax_fn,
	},
	[ATTR_BAT_VMIN] = {
		.name = "qcom,fast-charge-low-threshold-voltage",
		.reg = SMBB_CHG_VBAT_WEAK,
		.max = 3600000,
		.min = 2100000,
		.hw_fn = smbb_vbat_weak_fn,
	},
	[ATTR_CHG_VDET] = {
		.name = "qcom,auto-recharge-threshold-voltage",
		.reg = SMBB_CHG_VBAT_DET,
		.max = 5000000,
		.min = 3240000,
		.hw_fn = smbb_vbat_det_fn,
	},
	[ATTR_VIN_MIN] = {
		.name = "qcom,minimum-input-voltage",
		.reg = SMBB_CHG_VIN_MIN,
		.max = 9600000,
		.min = 4200000,
		.hw_fn = smbb_vin_fn,
	},
	[ATTR_USBIN_IMAX] = {
		.name = "usb-charge-current-limit",
		.reg = SMBB_USB_IMAX,
		.max = 2500000,
		.min = 100000,
		.hw_fn = smbb_imax_fn,
	},
};

static int smbb_charger_attr_write(struct smbb_charger *chg,
		enum smbb_attr which, unsigned int val)
{
	const struct smbb_charger_attr *prop;
	unsigned int wval;
	unsigned int out;
	int rc;

	prop = &smbb_charger_attrs[which];

	if (val > prop->max || val < prop->min) {
		dev_err(chg->dev, "value out of range for %s [%u:%u]\n",
			prop->name, prop->min, prop->max);
		return -EINVAL;
	}

	if (prop->safe_reg) {
		rc = regmap_read(chg->regmap,
				chg->addr + prop->safe_reg, &wval);
		if (rc) {
			dev_err(chg->dev,
				"unable to read safe value for '%s'\n",
				prop->name);
			return rc;
		}

		wval = prop->hw_fn(wval);

		if (val > wval) {
			dev_warn(chg->dev,
				"%s above safe value, clamping at %u\n",
				prop->name, wval);
			val = wval;
		}
	}

	wval = smbb_hw_lookup(val, prop->hw_fn);

	rc = regmap_write(chg->regmap, chg->addr + prop->reg, wval);
	if (rc) {
		dev_err(chg->dev, "unable to update %s", prop->name);
		return rc;
	}
	out = prop->hw_fn(wval);
	if (out != val) {
		dev_warn(chg->dev,
			"%s inaccurate, rounded to %u\n",
			prop->name, out);
	}

	dev_dbg(chg->dev, "%s <= %d\n", prop->name, out);

	chg->attr[which] = out;

	return 0;
}

static int smbb_charger_attr_read(struct smbb_charger *chg,
		enum smbb_attr which)
{
	const struct smbb_charger_attr *prop;
	unsigned int val;
	int rc;

	prop = &smbb_charger_attrs[which];

	rc = regmap_read(chg->regmap, chg->addr + prop->reg, &val);
	if (rc) {
		dev_err(chg->dev, "failed to read %s\n", prop->name);
		return rc;
	}
	val = prop->hw_fn(val);
	dev_dbg(chg->dev, "%s => %d\n", prop->name, val);

	chg->attr[which] = val;

	return 0;
}

static int smbb_charger_attr_parse(struct smbb_charger *chg,
		enum smbb_attr which)
{
	const struct smbb_charger_attr *prop;
	unsigned int val;
	int rc;

	prop = &smbb_charger_attrs[which];

	rc = of_property_read_u32(chg->dev->of_node, prop->name, &val);
	if (rc == 0) {
		rc = smbb_charger_attr_write(chg, which, val);
		if (!rc || !prop->fail_ok)
			return rc;
	}
	return smbb_charger_attr_read(chg, which);
}

static void smbb_set_line_flag(struct smbb_charger *chg, int irq, int flag)
{
	bool state;
	int ret;

	ret = irq_get_irqchip_state(irq, IRQCHIP_STATE_LINE_LEVEL, &state);
	if (ret < 0) {
		dev_err(chg->dev, "failed to read irq line\n");
		return;
	}

	mutex_lock(&chg->statlock);
	if (state)
		chg->status |= flag;
	else
		chg->status &= ~flag;
	mutex_unlock(&chg->statlock);

	dev_dbg(chg->dev, "status = %03lx\n", chg->status);
}

static irqreturn_t smbb_usb_valid_handler(int irq, void *_data)
{
	struct smbb_charger *chg = _data;

	smbb_set_line_flag(chg, irq, STATUS_USBIN_VALID);
	extcon_set_state_sync(chg->edev, EXTCON_USB,
				chg->status & STATUS_USBIN_VALID);
	power_supply_changed(chg->usb_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbb_dc_valid_handler(int irq, void *_data)
{
	struct smbb_charger *chg = _data;

	smbb_set_line_flag(chg, irq, STATUS_DCIN_VALID);
	if (!chg->dc_disabled)
		power_supply_changed(chg->dc_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbb_bat_temp_handler(int irq, void *_data)
{
	struct smbb_charger *chg = _data;
	unsigned int val;
	int rc;

	rc = regmap_read(chg->regmap, chg->addr + SMBB_BAT_TEMP_STATUS, &val);
	if (rc)
		return IRQ_HANDLED;

	mutex_lock(&chg->statlock);
	if (val & TEMP_STATUS_OK) {
		chg->status |= STATUS_BAT_OK;
	} else {
		chg->status &= ~STATUS_BAT_OK;
		if (val & TEMP_STATUS_HOT)
			chg->status |= STATUS_BAT_HOT;
	}
	mutex_unlock(&chg->statlock);

	power_supply_changed(chg->bat_psy);
	return IRQ_HANDLED;
}

static irqreturn_t smbb_bat_present_handler(int irq, void *_data)
{
	struct smbb_charger *chg = _data;

	smbb_set_line_flag(chg, irq, STATUS_BAT_PRESENT);
	power_supply_changed(chg->bat_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbb_chg_done_handler(int irq, void *_data)
{
	struct smbb_charger *chg = _data;

	smbb_set_line_flag(chg, irq, STATUS_CHG_DONE);
	power_supply_changed(chg->bat_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbb_chg_gone_handler(int irq, void *_data)
{
	struct smbb_charger *chg = _data;

	smbb_set_line_flag(chg, irq, STATUS_CHG_GONE);
	power_supply_changed(chg->bat_psy);
	power_supply_changed(chg->usb_psy);
	if (!chg->dc_disabled)
		power_supply_changed(chg->dc_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbb_chg_fast_handler(int irq, void *_data)
{
	struct smbb_charger *chg = _data;

	smbb_set_line_flag(chg, irq, STATUS_CHG_FAST);
	power_supply_changed(chg->bat_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smbb_chg_trkl_handler(int irq, void *_data)
{
	struct smbb_charger *chg = _data;

	smbb_set_line_flag(chg, irq, STATUS_CHG_TRKL);
	power_supply_changed(chg->bat_psy);

	return IRQ_HANDLED;
}

static const struct smbb_irq {
	const char *name;
	irqreturn_t (*handler)(int, void *);
} smbb_charger_irqs[] = {
	{ "chg-done", smbb_chg_done_handler },
	{ "chg-fast", smbb_chg_fast_handler },
	{ "chg-trkl", smbb_chg_trkl_handler },
	{ "bat-temp-ok", smbb_bat_temp_handler },
	{ "bat-present", smbb_bat_present_handler },
	{ "chg-gone", smbb_chg_gone_handler },
	{ "usb-valid", smbb_usb_valid_handler },
	{ "dc-valid", smbb_dc_valid_handler },
};

static int smbb_usbin_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smbb_charger *chg = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		mutex_lock(&chg->statlock);
		val->intval = !(chg->status & STATUS_CHG_GONE) &&
				(chg->status & STATUS_USBIN_VALID);
		mutex_unlock(&chg->statlock);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		val->intval = chg->attr[ATTR_USBIN_IMAX];
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		val->intval = 2500000;
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int smbb_usbin_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smbb_charger *chg = power_supply_get_drvdata(psy);
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		rc = smbb_charger_attr_write(chg, ATTR_USBIN_IMAX,
				val->intval);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int smbb_dcin_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smbb_charger *chg = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		mutex_lock(&chg->statlock);
		val->intval = !(chg->status & STATUS_CHG_GONE) &&
				(chg->status & STATUS_DCIN_VALID);
		mutex_unlock(&chg->statlock);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		val->intval = chg->attr[ATTR_DCIN_IMAX];
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		val->intval = 2500000;
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int smbb_dcin_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smbb_charger *chg = power_supply_get_drvdata(psy);
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		rc = smbb_charger_attr_write(chg, ATTR_DCIN_IMAX,
				val->intval);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int smbb_charger_writable_property(struct power_supply *psy,
		enum power_supply_property psp)
{
	return psp == POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT;
}

static int smbb_battery_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smbb_charger *chg = power_supply_get_drvdata(psy);
	unsigned long status;
	int rc = 0;

	mutex_lock(&chg->statlock);
	status = chg->status;
	mutex_unlock(&chg->statlock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (status & STATUS_CHG_GONE)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (!(status & (STATUS_DCIN_VALID | STATUS_USBIN_VALID)))
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (status & STATUS_CHG_DONE)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else if (!(status & STATUS_BAT_OK))
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (status & (STATUS_CHG_FAST | STATUS_CHG_TRKL))
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else /* everything is ok for charging, but we are not... */
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (status & STATUS_BAT_OK)
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		else if (status & STATUS_BAT_HOT)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else
			val->intval = POWER_SUPPLY_HEALTH_COLD;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		if (status & STATUS_CHG_FAST)
			val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
		else if (status & STATUS_CHG_TRKL)
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		else
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = !!(status & STATUS_BAT_PRESENT);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = chg->attr[ATTR_BAT_IMAX];
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = chg->attr[ATTR_BAT_VMAX];
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		/* this charger is a single-cell lithium-ion battery charger
		* only.  If you hook up some other technology, there will be
		* fireworks.
		*/
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = 3000000; /* single-cell li-ion low end */
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int smbb_battery_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smbb_charger *chg = power_supply_get_drvdata(psy);
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smbb_charger_attr_write(chg, ATTR_BAT_IMAX, val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smbb_charger_attr_write(chg, ATTR_BAT_VMAX, val->intval);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int smbb_battery_writable_property(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return 1;
	default:
		return 0;
	}
}

static enum power_supply_property smbb_charger_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
};

static enum power_supply_property smbb_battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_TECHNOLOGY,
};

static const struct reg_off_mask_default {
	unsigned int offset;
	unsigned int mask;
	unsigned int value;
	unsigned int rev_mask;
} smbb_charger_setup[] = {
	/* The bootloader is supposed to set this... make sure anyway. */
	{ SMBB_MISC_BOOT_DONE, BOOT_DONE, BOOT_DONE },

	/* Disable software timer */
	{ SMBB_CHG_TCHG_MAX_EN, TCHG_MAX_EN, 0 },

	/* Clear and disable watchdog */
	{ SMBB_CHG_WDOG_TIME, 0xff, 160 },
	{ SMBB_CHG_WDOG_EN, WDOG_EN, 0 },

	/* Use charger based EoC detection */
	{ SMBB_CHG_IBAT_TERM_CHG, IBAT_TERM_CHG_IEOC, IBAT_TERM_CHG_IEOC_CHG },

	/* Disable GSM PA load adjustment.
	* The PA signal is incorrectly connected on v2.
	*/
	{ SMBB_CHG_CFG, 0xff, 0x00, BIT(3) },

	/* Use VBAT (not VSYS) to compensate for IR drop during fast charging */
	{ SMBB_BUCK_REG_MODE, BUCK_REG_MODE, BUCK_REG_MODE_VBAT },

	/* Enable battery temperature comparators */
	{ SMBB_BAT_BTC_CTRL, BTC_CTRL_COMP_EN, BTC_CTRL_COMP_EN },

	/* Stop USB enumeration timer */
	{ SMBB_USB_ENUM_TIMER_STOP, ENUM_TIMER_STOP, ENUM_TIMER_STOP },

#if 0 /* FIXME supposedly only to disable hardware ARB termination */
	{ SMBB_USB_SEC_ACCESS, SEC_ACCESS_MAGIC },
	{ SMBB_USB_REV_BST, 0xff, REV_BST_CHG_GONE },
#endif

	/* Stop USB enumeration timer, again */
	{ SMBB_USB_ENUM_TIMER_STOP, ENUM_TIMER_STOP, ENUM_TIMER_STOP },

	/* Enable charging */
	{ SMBB_CHG_CTRL, CTRL_EN, CTRL_EN },
};

static char *smbb_bif[] = { "smbb-bif" };

static const struct power_supply_desc bat_psy_desc = {
	.name = "smbb-bif",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = smbb_battery_properties,
	.num_properties = ARRAY_SIZE(smbb_battery_properties),
	.get_property = smbb_battery_get_property,
	.set_property = smbb_battery_set_property,
	.property_is_writeable = smbb_battery_writable_property,
};

static const struct power_supply_desc usb_psy_desc = {
	.name = "smbb-usbin",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = smbb_charger_properties,
	.num_properties = ARRAY_SIZE(smbb_charger_properties),
	.get_property = smbb_usbin_get_property,
	.set_property = smbb_usbin_set_property,
	.property_is_writeable = smbb_charger_writable_property,
};

static const struct power_supply_desc dc_psy_desc = {
	.name = "smbb-dcin",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = smbb_charger_properties,
	.num_properties = ARRAY_SIZE(smbb_charger_properties),
	.get_property = smbb_dcin_get_property,
	.set_property = smbb_dcin_set_property,
	.property_is_writeable = smbb_charger_writable_property,
};

static int smbb_chg_otg_enable(struct regulator_dev *rdev)
{
	struct smbb_charger *chg = rdev_get_drvdata(rdev);
	int rc;

	rc = regmap_update_bits(chg->regmap, chg->addr + SMBB_USB_OTG_CTL,
				OTG_CTL_EN, OTG_CTL_EN);
	if (rc)
		dev_err(chg->dev, "failed to update OTG_CTL\n");
	return rc;
}

static int smbb_chg_otg_disable(struct regulator_dev *rdev)
{
	struct smbb_charger *chg = rdev_get_drvdata(rdev);
	int rc;

	rc = regmap_update_bits(chg->regmap, chg->addr + SMBB_USB_OTG_CTL,
				OTG_CTL_EN, 0);
	if (rc)
		dev_err(chg->dev, "failed to update OTG_CTL\n");
	return rc;
}

static int smbb_chg_otg_is_enabled(struct regulator_dev *rdev)
{
	struct smbb_charger *chg = rdev_get_drvdata(rdev);
	unsigned int value = 0;
	int rc;

	rc = regmap_read(chg->regmap, chg->addr + SMBB_USB_OTG_CTL, &value);
	if (rc)
		dev_err(chg->dev, "failed to read OTG_CTL\n");

	return !!(value & OTG_CTL_EN);
}

static const struct regulator_ops smbb_chg_otg_ops = {
	.enable = smbb_chg_otg_enable,
	.disable = smbb_chg_otg_disable,
	.is_enabled = smbb_chg_otg_is_enabled,
};

static int smbb_charger_probe(struct platform_device *pdev)
{
	struct power_supply_config bat_cfg = {};
	struct power_supply_config usb_cfg = {};
	struct power_supply_config dc_cfg = {};
	struct smbb_charger *chg;
	struct regulator_config config = { };
	int rc, i;

	chg = devm_kzalloc(&pdev->dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;

	chg->dev = &pdev->dev;
	mutex_init(&chg->statlock);

	chg->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chg->regmap) {
		dev_err(&pdev->dev, "failed to locate regmap\n");
		return -ENODEV;
	}

	rc = of_property_read_u32(pdev->dev.of_node, "reg", &chg->addr);
	if (rc) {
		dev_err(&pdev->dev, "missing or invalid 'reg' property\n");
		return rc;
	}

	rc = regmap_read(chg->regmap, chg->addr + SMBB_MISC_REV2, &chg->revision);
	if (rc) {
		dev_err(&pdev->dev, "unable to read revision\n");
		return rc;
	}

	chg->revision += 1;
	if (chg->revision != 2 && chg->revision != 3) {
		dev_err(&pdev->dev, "v1 hardware not supported\n");
		return -ENODEV;
	}
	dev_info(&pdev->dev, "Initializing SMBB rev %u", chg->revision);

	chg->dc_disabled = of_property_read_bool(pdev->dev.of_node, "qcom,disable-dc");

	for (i = 0; i < _ATTR_CNT; ++i) {
		rc = smbb_charger_attr_parse(chg, i);
		if (rc) {
			dev_err(&pdev->dev, "failed to parse/apply settings\n");
			return rc;
		}
	}

	bat_cfg.drv_data = chg;
	bat_cfg.of_node = pdev->dev.of_node;
	chg->bat_psy = devm_power_supply_register(&pdev->dev,
						  &bat_psy_desc,
						  &bat_cfg);
	if (IS_ERR(chg->bat_psy)) {
		dev_err(&pdev->dev, "failed to register battery\n");
		return PTR_ERR(chg->bat_psy);
	}

	usb_cfg.drv_data = chg;
	usb_cfg.supplied_to = smbb_bif;
	usb_cfg.num_supplicants = ARRAY_SIZE(smbb_bif);
	chg->usb_psy = devm_power_supply_register(&pdev->dev,
						  &usb_psy_desc,
						  &usb_cfg);
	if (IS_ERR(chg->usb_psy)) {
		dev_err(&pdev->dev, "failed to register USB power supply\n");
		return PTR_ERR(chg->usb_psy);
	}

	chg->edev = devm_extcon_dev_allocate(&pdev->dev, smbb_usb_extcon_cable);
	if (IS_ERR(chg->edev)) {
		dev_err(&pdev->dev, "failed to allocate extcon device\n");
		return -ENOMEM;
	}

	rc = devm_extcon_dev_register(&pdev->dev, chg->edev);
	if (rc < 0) {
		dev_err(&pdev->dev, "failed to register extcon device\n");
		return rc;
	}

	if (!chg->dc_disabled) {
		dc_cfg.drv_data = chg;
		dc_cfg.supplied_to = smbb_bif;
		dc_cfg.num_supplicants = ARRAY_SIZE(smbb_bif);
		chg->dc_psy = devm_power_supply_register(&pdev->dev,
							 &dc_psy_desc,
							 &dc_cfg);
		if (IS_ERR(chg->dc_psy)) {
			dev_err(&pdev->dev, "failed to register DC power supply\n");
			return PTR_ERR(chg->dc_psy);
		}
	}

	for (i = 0; i < ARRAY_SIZE(smbb_charger_irqs); ++i) {
		int irq;

		irq = platform_get_irq_byname(pdev, smbb_charger_irqs[i].name);
		if (irq < 0) {
			dev_err(&pdev->dev, "failed to get irq '%s'\n",
				smbb_charger_irqs[i].name);
			return irq;
		}

		smbb_charger_irqs[i].handler(irq, chg);

		rc = devm_request_threaded_irq(&pdev->dev, irq, NULL,
				smbb_charger_irqs[i].handler, IRQF_ONESHOT,
				smbb_charger_irqs[i].name, chg);
		if (rc) {
			dev_err(&pdev->dev, "failed to request irq '%s'\n",
				smbb_charger_irqs[i].name);
			return rc;
		}
	}

	/*
	 * otg regulator is used to control VBUS voltage direction
	 * when USB switches between host and gadget mode
	 */
	chg->otg_rdesc.id = -1;
	chg->otg_rdesc.name = "otg-vbus";
	chg->otg_rdesc.ops = &smbb_chg_otg_ops;
	chg->otg_rdesc.owner = THIS_MODULE;
	chg->otg_rdesc.type = REGULATOR_VOLTAGE;
	chg->otg_rdesc.supply_name = "usb-otg-in";
	chg->otg_rdesc.of_match = "otg-vbus";

	config.dev = &pdev->dev;
	config.driver_data = chg;

	chg->otg_reg = devm_regulator_register(&pdev->dev, &chg->otg_rdesc,
					       &config);
	if (IS_ERR(chg->otg_reg))
		return PTR_ERR(chg->otg_reg);

	chg->jeita_ext_temp = of_property_read_bool(pdev->dev.of_node,
			"qcom,jeita-extended-temp-range");

	/* Set temperature range to [35%:70%] or [25%:80%] accordingly */
	rc = regmap_update_bits(chg->regmap, chg->addr + SMBB_BAT_BTC_CTRL,
			BTC_CTRL_COLD_EXT | BTC_CTRL_HOT_EXT_N,
			chg->jeita_ext_temp ?
				BTC_CTRL_COLD_EXT :
				BTC_CTRL_HOT_EXT_N);
	if (rc) {
		dev_err(&pdev->dev,
			"unable to set %s temperature range\n",
			chg->jeita_ext_temp ? "JEITA extended" : "normal");
		return rc;
	}

	for (i = 0; i < ARRAY_SIZE(smbb_charger_setup); ++i) {
		const struct reg_off_mask_default *r = &smbb_charger_setup[i];

		if (r->rev_mask & BIT(chg->revision))
			continue;

		rc = regmap_update_bits(chg->regmap, chg->addr + r->offset,
				r->mask, r->value);
		if (rc) {
			dev_err(&pdev->dev,
				"unable to initializing charging, bailing\n");
			return rc;
		}
	}

	platform_set_drvdata(pdev, chg);

	return 0;
}

static int smbb_charger_remove(struct platform_device *pdev)
{
	struct smbb_charger *chg;

	chg = platform_get_drvdata(pdev);

	regmap_update_bits(chg->regmap, chg->addr + SMBB_CHG_CTRL, CTRL_EN, 0);

	return 0;
}

static const struct of_device_id smbb_charger_id_table[] = {
	{ .compatible = "qcom,pm8941-charger" },
	{ }
};
MODULE_DEVICE_TABLE(of, smbb_charger_id_table);

static struct platform_driver smbb_charger_driver = {
	.probe	  = smbb_charger_probe,
	.remove	 = smbb_charger_remove,
	.driver	 = {
		.name   = "qcom-smbb",
		.of_match_table = smbb_charger_id_table,
	},
};
module_platform_driver(smbb_charger_driver);

MODULE_DESCRIPTION("Qualcomm Switch-Mode Battery Charger and Boost driver");
MODULE_LICENSE("GPL v2");
