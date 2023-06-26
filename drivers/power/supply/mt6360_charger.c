// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/devm-helpers.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/linear_range.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

#define MT6360_PMU_CHG_CTRL1	0x311
#define MT6360_PMU_CHG_CTRL2	0x312
#define MT6360_PMU_CHG_CTRL3	0x313
#define MT6360_PMU_CHG_CTRL4	0x314
#define MT6360_PMU_CHG_CTRL5	0x315
#define MT6360_PMU_CHG_CTRL6	0x316
#define MT6360_PMU_CHG_CTRL7	0x317
#define MT6360_PMU_CHG_CTRL8	0x318
#define MT6360_PMU_CHG_CTRL9	0x319
#define MT6360_PMU_CHG_CTRL10	0x31A
#define MT6360_PMU_DEVICE_TYPE	0x322
#define MT6360_PMU_USB_STATUS1	0x327
#define MT6360_PMU_CHG_STAT	0x34A
#define MT6360_PMU_CHG_CTRL19	0x361
#define MT6360_PMU_FOD_STAT	0x3E7

/* MT6360_PMU_CHG_CTRL1 */
#define MT6360_FSLP_SHFT	(3)
#define MT6360_FSLP_MASK	BIT(MT6360_FSLP_SHFT)
#define MT6360_OPA_MODE_SHFT	(0)
#define MT6360_OPA_MODE_MASK	BIT(MT6360_OPA_MODE_SHFT)
/* MT6360_PMU_CHG_CTRL2 */
#define MT6360_IINLMTSEL_SHFT	(2)
#define MT6360_IINLMTSEL_MASK	GENMASK(3, 2)
/* MT6360_PMU_CHG_CTRL3 */
#define MT6360_IAICR_SHFT	(2)
#define MT6360_IAICR_MASK	GENMASK(7, 2)
#define MT6360_ILIM_EN_MASK	BIT(0)
/* MT6360_PMU_CHG_CTRL4 */
#define MT6360_VOREG_SHFT	(1)
#define MT6360_VOREG_MASK	GENMASK(7, 1)
/* MT6360_PMU_CHG_CTRL5 */
#define MT6360_VOBST_MASK	GENMASK(7, 2)
/* MT6360_PMU_CHG_CTRL6 */
#define MT6360_VMIVR_SHFT      (1)
#define MT6360_VMIVR_MASK      GENMASK(7, 1)
/* MT6360_PMU_CHG_CTRL7 */
#define MT6360_ICHG_SHFT	(2)
#define MT6360_ICHG_MASK	GENMASK(7, 2)
/* MT6360_PMU_CHG_CTRL8 */
#define MT6360_IPREC_SHFT	(0)
#define MT6360_IPREC_MASK	GENMASK(3, 0)
/* MT6360_PMU_CHG_CTRL9 */
#define MT6360_IEOC_SHFT	(4)
#define MT6360_IEOC_MASK	GENMASK(7, 4)
/* MT6360_PMU_CHG_CTRL10 */
#define MT6360_OTG_OC_MASK	GENMASK(3, 0)
/* MT6360_PMU_DEVICE_TYPE */
#define MT6360_USBCHGEN_MASK	BIT(7)
/* MT6360_PMU_USB_STATUS1 */
#define MT6360_USB_STATUS_SHFT	(4)
#define MT6360_USB_STATUS_MASK	GENMASK(6, 4)
/* MT6360_PMU_CHG_STAT */
#define MT6360_CHG_STAT_SHFT	(6)
#define MT6360_CHG_STAT_MASK	GENMASK(7, 6)
#define MT6360_VBAT_LVL_MASK	BIT(5)
/* MT6360_PMU_CHG_CTRL19 */
#define MT6360_VINOVP_SHFT	(5)
#define MT6360_VINOVP_MASK	GENMASK(6, 5)
/* MT6360_PMU_FOD_STAT */
#define MT6360_CHRDET_EXT_MASK	BIT(4)

/* uV */
#define MT6360_VMIVR_MIN	3900000
#define MT6360_VMIVR_MAX	13400000
#define MT6360_VMIVR_STEP	100000
/* uA */
#define MT6360_ICHG_MIN		100000
#define MT6360_ICHG_MAX		5000000
#define MT6360_ICHG_STEP	100000
/* uV */
#define MT6360_VOREG_MIN	3900000
#define MT6360_VOREG_MAX	4710000
#define MT6360_VOREG_STEP	10000
/* uA */
#define MT6360_AICR_MIN		100000
#define MT6360_AICR_MAX		3250000
#define MT6360_AICR_STEP	50000
/* uA */
#define MT6360_IPREC_MIN	100000
#define MT6360_IPREC_MAX	850000
#define MT6360_IPREC_STEP	50000
/* uA */
#define MT6360_IEOC_MIN		100000
#define MT6360_IEOC_MAX		850000
#define MT6360_IEOC_STEP	50000

enum {
	MT6360_RANGE_VMIVR,
	MT6360_RANGE_ICHG,
	MT6360_RANGE_VOREG,
	MT6360_RANGE_AICR,
	MT6360_RANGE_IPREC,
	MT6360_RANGE_IEOC,
	MT6360_RANGE_MAX,
};

static const struct linear_range mt6360_chg_range[MT6360_RANGE_MAX] = {
	LINEAR_RANGE_IDX(MT6360_RANGE_VMIVR, 3900000, 0, 0x5F, 100000),
	LINEAR_RANGE_IDX(MT6360_RANGE_ICHG, 100000, 0, 0x31, 100000),
	LINEAR_RANGE_IDX(MT6360_RANGE_VOREG, 3900000, 0, 0x51, 10000),
	LINEAR_RANGE_IDX(MT6360_RANGE_AICR, 100000, 0, 0x3F, 50000),
	LINEAR_RANGE_IDX(MT6360_RANGE_IPREC, 100000, 0, 0x0F, 50000),
	LINEAR_RANGE_IDX(MT6360_RANGE_IEOC, 100000, 0, 0x0F, 50000),
};

struct mt6360_chg_info {
	struct device *dev;
	struct regmap *regmap;
	struct power_supply_desc psy_desc;
	struct power_supply *psy;
	struct regulator_dev *otg_rdev;
	struct mutex chgdet_lock;
	u32 vinovp;
	bool pwr_rdy;
	bool bc12_en;
	int psy_usb_type;
	struct work_struct chrdet_work;
};

enum mt6360_iinlmtsel {
	MT6360_IINLMTSEL_AICR_3250 = 0,
	MT6360_IINLMTSEL_CHG_TYPE,
	MT6360_IINLMTSEL_AICR,
	MT6360_IINLMTSEL_LOWER_LEVEL,
};

enum mt6360_pmu_chg_type {
	MT6360_CHG_TYPE_NOVBUS = 0,
	MT6360_CHG_TYPE_UNDER_GOING,
	MT6360_CHG_TYPE_SDP,
	MT6360_CHG_TYPE_SDPNSTD,
	MT6360_CHG_TYPE_DCP,
	MT6360_CHG_TYPE_CDP,
	MT6360_CHG_TYPE_DISABLE_BC12,
	MT6360_CHG_TYPE_MAX,
};

static enum power_supply_usb_type mt6360_charger_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
};

static int mt6360_get_chrdet_ext_stat(struct mt6360_chg_info *mci,
					     bool *pwr_rdy)
{
	int ret;
	unsigned int regval;

	ret = regmap_read(mci->regmap, MT6360_PMU_FOD_STAT, &regval);
	if (ret < 0)
		return ret;
	*pwr_rdy = (regval & MT6360_CHRDET_EXT_MASK) ? true : false;
	return 0;
}

static int mt6360_charger_get_online(struct mt6360_chg_info *mci,
				     union power_supply_propval *val)
{
	int ret;
	bool pwr_rdy;

	ret = mt6360_get_chrdet_ext_stat(mci, &pwr_rdy);
	if (ret < 0)
		return ret;
	val->intval = pwr_rdy ? true : false;
	return 0;
}

static int mt6360_charger_get_status(struct mt6360_chg_info *mci,
				     union power_supply_propval *val)
{
	int status, ret;
	unsigned int regval;
	bool pwr_rdy;

	ret = mt6360_get_chrdet_ext_stat(mci, &pwr_rdy);
	if (ret < 0)
		return ret;
	if (!pwr_rdy) {
		status = POWER_SUPPLY_STATUS_DISCHARGING;
		goto out;
	}

	ret = regmap_read(mci->regmap, MT6360_PMU_CHG_STAT, &regval);
	if (ret < 0)
		return ret;
	regval &= MT6360_CHG_STAT_MASK;
	regval >>= MT6360_CHG_STAT_SHFT;
	switch (regval) {
	case 0x0:
		status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case 0x1:
		status = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case 0x2:
		status = POWER_SUPPLY_STATUS_FULL;
		break;
	default:
		ret = -EIO;
	}
out:
	if (!ret)
		val->intval = status;
	return ret;
}

static int mt6360_charger_get_charge_type(struct mt6360_chg_info *mci,
					  union power_supply_propval *val)
{
	int type, ret;
	unsigned int regval;
	u8 chg_stat;

	ret = regmap_read(mci->regmap, MT6360_PMU_CHG_STAT, &regval);
	if (ret < 0)
		return ret;

	chg_stat = (regval & MT6360_CHG_STAT_MASK) >> MT6360_CHG_STAT_SHFT;
	switch (chg_stat) {
	case 0x01: /* Charge in Progress */
		if (regval & MT6360_VBAT_LVL_MASK)
			type = POWER_SUPPLY_CHARGE_TYPE_FAST;
		else
			type = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	case 0x00: /* Not Charging */
	case 0x02: /* Charge Done */
	case 0x03: /* Charge Fault */
	default:
		type = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	}

	val->intval = type;
	return 0;
}

static int mt6360_charger_get_ichg(struct mt6360_chg_info *mci,
				   union power_supply_propval *val)
{
	int ret;
	u32 sel, value;

	ret = regmap_read(mci->regmap, MT6360_PMU_CHG_CTRL7, &sel);
	if (ret < 0)
		return ret;
	sel = (sel & MT6360_ICHG_MASK) >> MT6360_ICHG_SHFT;
	ret = linear_range_get_value(&mt6360_chg_range[MT6360_RANGE_ICHG], sel, &value);
	if (!ret)
		val->intval = value;
	return ret;
}

static int mt6360_charger_get_max_ichg(struct mt6360_chg_info *mci,
				       union power_supply_propval *val)
{
	val->intval = MT6360_ICHG_MAX;
	return 0;
}

static int mt6360_charger_get_cv(struct mt6360_chg_info *mci,
				 union power_supply_propval *val)
{
	int ret;
	u32 sel, value;

	ret = regmap_read(mci->regmap, MT6360_PMU_CHG_CTRL4, &sel);
	if (ret < 0)
		return ret;
	sel = (sel & MT6360_VOREG_MASK) >> MT6360_VOREG_SHFT;
	ret = linear_range_get_value(&mt6360_chg_range[MT6360_RANGE_VOREG], sel, &value);
	if (!ret)
		val->intval = value;
	return ret;
}

static int mt6360_charger_get_max_cv(struct mt6360_chg_info *mci,
				     union power_supply_propval *val)
{
	val->intval = MT6360_VOREG_MAX;
	return 0;
}

static int mt6360_charger_get_aicr(struct mt6360_chg_info *mci,
				   union power_supply_propval *val)
{
	int ret;
	u32 sel, value;

	ret = regmap_read(mci->regmap, MT6360_PMU_CHG_CTRL3, &sel);
	if (ret < 0)
		return ret;
	sel = (sel & MT6360_IAICR_MASK) >> MT6360_IAICR_SHFT;
	ret = linear_range_get_value(&mt6360_chg_range[MT6360_RANGE_AICR], sel, &value);
	if (!ret)
		val->intval = value;
	return ret;
}

static int mt6360_charger_get_mivr(struct mt6360_chg_info *mci,
				   union power_supply_propval *val)
{
	int ret;
	u32 sel, value;

	ret = regmap_read(mci->regmap, MT6360_PMU_CHG_CTRL6, &sel);
	if (ret < 0)
		return ret;
	sel = (sel & MT6360_VMIVR_MASK) >> MT6360_VMIVR_SHFT;
	ret = linear_range_get_value(&mt6360_chg_range[MT6360_RANGE_VMIVR], sel, &value);
	if (!ret)
		val->intval = value;
	return ret;
}

static int mt6360_charger_get_iprechg(struct mt6360_chg_info *mci,
				      union power_supply_propval *val)
{
	int ret;
	u32 sel, value;

	ret = regmap_read(mci->regmap, MT6360_PMU_CHG_CTRL8, &sel);
	if (ret < 0)
		return ret;
	sel = (sel & MT6360_IPREC_MASK) >> MT6360_IPREC_SHFT;
	ret = linear_range_get_value(&mt6360_chg_range[MT6360_RANGE_IPREC], sel, &value);
	if (!ret)
		val->intval = value;
	return ret;
}

static int mt6360_charger_get_ieoc(struct mt6360_chg_info *mci,
				   union power_supply_propval *val)
{
	int ret;
	u32 sel, value;

	ret = regmap_read(mci->regmap, MT6360_PMU_CHG_CTRL9, &sel);
	if (ret < 0)
		return ret;
	sel = (sel & MT6360_IEOC_MASK) >> MT6360_IEOC_SHFT;
	ret = linear_range_get_value(&mt6360_chg_range[MT6360_RANGE_IEOC], sel, &value);
	if (!ret)
		val->intval = value;
	return ret;
}

static int mt6360_charger_set_online(struct mt6360_chg_info *mci,
				     const union power_supply_propval *val)
{
	u8 force_sleep = val->intval ? 0 : 1;

	return regmap_update_bits(mci->regmap,
				  MT6360_PMU_CHG_CTRL1,
				  MT6360_FSLP_MASK,
				  force_sleep << MT6360_FSLP_SHFT);
}

static int mt6360_charger_set_ichg(struct mt6360_chg_info *mci,
				   const union power_supply_propval *val)
{
	u32 sel;

	linear_range_get_selector_within(&mt6360_chg_range[MT6360_RANGE_ICHG], val->intval, &sel);
	return regmap_update_bits(mci->regmap,
				  MT6360_PMU_CHG_CTRL7,
				  MT6360_ICHG_MASK,
				  sel << MT6360_ICHG_SHFT);
}

static int mt6360_charger_set_cv(struct mt6360_chg_info *mci,
				 const union power_supply_propval *val)
{
	u32 sel;

	linear_range_get_selector_within(&mt6360_chg_range[MT6360_RANGE_VOREG], val->intval, &sel);
	return regmap_update_bits(mci->regmap,
				  MT6360_PMU_CHG_CTRL4,
				  MT6360_VOREG_MASK,
				  sel << MT6360_VOREG_SHFT);
}

static int mt6360_charger_set_aicr(struct mt6360_chg_info *mci,
				   const union power_supply_propval *val)
{
	u32 sel;

	linear_range_get_selector_within(&mt6360_chg_range[MT6360_RANGE_AICR], val->intval, &sel);
	return regmap_update_bits(mci->regmap,
				  MT6360_PMU_CHG_CTRL3,
				  MT6360_IAICR_MASK,
				  sel << MT6360_IAICR_SHFT);
}

static int mt6360_charger_set_mivr(struct mt6360_chg_info *mci,
				   const union power_supply_propval *val)
{
	u32 sel;

	linear_range_get_selector_within(&mt6360_chg_range[MT6360_RANGE_VMIVR], val->intval, &sel);
	return regmap_update_bits(mci->regmap,
				  MT6360_PMU_CHG_CTRL3,
				  MT6360_VMIVR_MASK,
				  sel << MT6360_VMIVR_SHFT);
}

static int mt6360_charger_set_iprechg(struct mt6360_chg_info *mci,
				      const union power_supply_propval *val)
{
	u32 sel;

	linear_range_get_selector_within(&mt6360_chg_range[MT6360_RANGE_IPREC], val->intval, &sel);
	return regmap_update_bits(mci->regmap,
				  MT6360_PMU_CHG_CTRL8,
				  MT6360_IPREC_MASK,
				  sel << MT6360_IPREC_SHFT);
}

static int mt6360_charger_set_ieoc(struct mt6360_chg_info *mci,
				   const union power_supply_propval *val)
{
	u32 sel;

	linear_range_get_selector_within(&mt6360_chg_range[MT6360_RANGE_IEOC], val->intval, &sel);
	return regmap_update_bits(mci->regmap,
				  MT6360_PMU_CHG_CTRL9,
				  MT6360_IEOC_MASK,
				  sel << MT6360_IEOC_SHFT);
}

static int mt6360_charger_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct mt6360_chg_info *mci = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = mt6360_charger_get_online(mci, val);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		ret = mt6360_charger_get_status(mci, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		ret = mt6360_charger_get_charge_type(mci, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = mt6360_charger_get_ichg(mci, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		ret = mt6360_charger_get_max_ichg(mci, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = mt6360_charger_get_cv(mci, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		ret = mt6360_charger_get_max_cv(mci, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = mt6360_charger_get_aicr(mci, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = mt6360_charger_get_mivr(mci, val);
		break;
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		ret = mt6360_charger_get_iprechg(mci, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = mt6360_charger_get_ieoc(mci, val);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = mci->psy_usb_type;
		break;
	default:
		ret = -ENODATA;
	}
	return ret;
}

static int mt6360_charger_set_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       const union power_supply_propval *val)
{
	struct mt6360_chg_info *mci = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = mt6360_charger_set_online(mci, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = mt6360_charger_set_ichg(mci, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = mt6360_charger_set_cv(mci, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = mt6360_charger_set_aicr(mci, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = mt6360_charger_set_mivr(mci, val);
		break;
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		ret = mt6360_charger_set_iprechg(mci, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = mt6360_charger_set_ieoc(mci, val);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int mt6360_charger_property_is_writeable(struct power_supply *psy,
					       enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return 1;
	default:
		return 0;
	}
}

static enum power_supply_property mt6360_charger_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static const struct power_supply_desc mt6360_charger_desc = {
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= mt6360_charger_properties,
	.num_properties		= ARRAY_SIZE(mt6360_charger_properties),
	.get_property		= mt6360_charger_get_property,
	.set_property		= mt6360_charger_set_property,
	.property_is_writeable	= mt6360_charger_property_is_writeable,
	.usb_types		= mt6360_charger_usb_types,
	.num_usb_types		= ARRAY_SIZE(mt6360_charger_usb_types),
};

static const struct regulator_ops mt6360_chg_otg_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};

static const struct regulator_desc mt6360_otg_rdesc = {
	.of_match = "usb-otg-vbus",
	.name = "usb-otg-vbus",
	.ops = &mt6360_chg_otg_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.min_uV = 4425000,
	.uV_step = 25000,
	.n_voltages = 57,
	.vsel_reg = MT6360_PMU_CHG_CTRL5,
	.vsel_mask = MT6360_VOBST_MASK,
	.enable_reg = MT6360_PMU_CHG_CTRL1,
	.enable_mask = MT6360_OPA_MODE_MASK,
};

static irqreturn_t mt6360_pmu_attach_i_handler(int irq, void *data)
{
	struct mt6360_chg_info *mci = data;
	int ret;
	unsigned int usb_status;
	int last_usb_type;

	mutex_lock(&mci->chgdet_lock);
	if (!mci->bc12_en) {
		dev_warn(mci->dev, "Received attach interrupt, bc12 disabled, ignore irq\n");
		goto out;
	}
	last_usb_type = mci->psy_usb_type;
	/* Plug in */
	ret = regmap_read(mci->regmap, MT6360_PMU_USB_STATUS1, &usb_status);
	if (ret < 0)
		goto out;
	usb_status &= MT6360_USB_STATUS_MASK;
	usb_status >>= MT6360_USB_STATUS_SHFT;
	switch (usb_status) {
	case MT6360_CHG_TYPE_NOVBUS:
		dev_dbg(mci->dev, "Received attach interrupt, no vbus\n");
		goto out;
	case MT6360_CHG_TYPE_UNDER_GOING:
		dev_dbg(mci->dev, "Received attach interrupt, under going...\n");
		goto out;
	case MT6360_CHG_TYPE_SDP:
		mci->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case MT6360_CHG_TYPE_SDPNSTD:
		mci->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case MT6360_CHG_TYPE_CDP:
		mci->psy_usb_type = POWER_SUPPLY_USB_TYPE_CDP;
		break;
	case MT6360_CHG_TYPE_DCP:
		mci->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	case MT6360_CHG_TYPE_DISABLE_BC12:
		dev_dbg(mci->dev, "Received attach interrupt, bc12 detect not enable\n");
		goto out;
	default:
		mci->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		dev_dbg(mci->dev, "Received attach interrupt, reserved address\n");
		goto out;
	}

	dev_dbg(mci->dev, "Received attach interrupt, chg_type = %d\n", mci->psy_usb_type);
	if (last_usb_type != mci->psy_usb_type)
		power_supply_changed(mci->psy);
out:
	mutex_unlock(&mci->chgdet_lock);
	return IRQ_HANDLED;
}

static void mt6360_handle_chrdet_ext_evt(struct mt6360_chg_info *mci)
{
	int ret;
	bool pwr_rdy;

	mutex_lock(&mci->chgdet_lock);
	ret = mt6360_get_chrdet_ext_stat(mci, &pwr_rdy);
	if (ret < 0)
		goto out;
	if (mci->pwr_rdy == pwr_rdy) {
		dev_dbg(mci->dev, "Received vbus interrupt, pwr_rdy is same(%d)\n", pwr_rdy);
		goto out;
	}
	mci->pwr_rdy = pwr_rdy;
	dev_dbg(mci->dev, "Received vbus interrupt, pwr_rdy = %d\n", pwr_rdy);
	if (!pwr_rdy) {
		mci->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		power_supply_changed(mci->psy);

	}
	ret = regmap_update_bits(mci->regmap,
				 MT6360_PMU_DEVICE_TYPE,
				 MT6360_USBCHGEN_MASK,
				 pwr_rdy ? MT6360_USBCHGEN_MASK : 0);
	if (ret < 0)
		goto out;
	mci->bc12_en = pwr_rdy;
out:
	mutex_unlock(&mci->chgdet_lock);
}

static void mt6360_chrdet_work(struct work_struct *work)
{
	struct mt6360_chg_info *mci = (struct mt6360_chg_info *)container_of(
				     work, struct mt6360_chg_info, chrdet_work);

	mt6360_handle_chrdet_ext_evt(mci);
}

static irqreturn_t mt6360_pmu_chrdet_ext_evt_handler(int irq, void *data)
{
	struct mt6360_chg_info *mci = data;

	mt6360_handle_chrdet_ext_evt(mci);
	return IRQ_HANDLED;
}

static int mt6360_chg_irq_register(struct platform_device *pdev)
{
	const struct {
		const char *name;
		irq_handler_t handler;
	} irq_descs[] = {
		{ "attach_i", mt6360_pmu_attach_i_handler },
		{ "chrdet_ext_evt", mt6360_pmu_chrdet_ext_evt_handler }
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(irq_descs); i++) {
		ret = platform_get_irq_byname(pdev, irq_descs[i].name);
		if (ret < 0)
			return ret;

		ret = devm_request_threaded_irq(&pdev->dev, ret, NULL,
						irq_descs[i].handler,
						IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
						irq_descs[i].name,
						platform_get_drvdata(pdev));
		if (ret < 0)
			return dev_err_probe(&pdev->dev, ret, "Failed to request %s irq\n",
					     irq_descs[i].name);
	}

	return 0;
}

static u32 mt6360_vinovp_trans_to_sel(u32 val)
{
	u32 vinovp_tbl[] = { 5500000, 6500000, 11000000, 14500000 };
	int i;

	/* Select the smaller and equal supported value */
	for (i = 0; i < ARRAY_SIZE(vinovp_tbl)-1; i++) {
		if (val < vinovp_tbl[i+1])
			break;
	}
	return i;
}

static int mt6360_chg_init_setting(struct mt6360_chg_info *mci)
{
	int ret;
	u32 sel;

	sel = mt6360_vinovp_trans_to_sel(mci->vinovp);
	ret = regmap_update_bits(mci->regmap, MT6360_PMU_CHG_CTRL19,
				  MT6360_VINOVP_MASK, sel << MT6360_VINOVP_SHFT);
	if (ret)
		return dev_err_probe(mci->dev, ret, "%s: Failed to apply vinovp\n", __func__);
	ret = regmap_update_bits(mci->regmap, MT6360_PMU_DEVICE_TYPE,
				 MT6360_USBCHGEN_MASK, 0);
	if (ret)
		return dev_err_probe(mci->dev, ret, "%s: Failed to disable bc12\n", __func__);
	ret = regmap_update_bits(mci->regmap, MT6360_PMU_CHG_CTRL2,
				 MT6360_IINLMTSEL_MASK,
				 MT6360_IINLMTSEL_AICR <<
					MT6360_IINLMTSEL_SHFT);
	if (ret)
		return dev_err_probe(mci->dev, ret,
				     "%s: Failed to switch iinlmtsel to aicr\n", __func__);
	usleep_range(5000, 6000);
	ret = regmap_update_bits(mci->regmap, MT6360_PMU_CHG_CTRL3,
				 MT6360_ILIM_EN_MASK, 0);
	if (ret)
		return dev_err_probe(mci->dev, ret,
				     "%s: Failed to disable ilim\n", __func__);
	ret = regmap_update_bits(mci->regmap, MT6360_PMU_CHG_CTRL10,
				 MT6360_OTG_OC_MASK, MT6360_OTG_OC_MASK);
	if (ret)
		return dev_err_probe(mci->dev, ret,
				     "%s: Failed to config otg oc to 3A\n", __func__);
	return 0;
}

static int mt6360_charger_probe(struct platform_device *pdev)
{
	struct mt6360_chg_info *mci;
	struct power_supply_config charger_cfg = {};
	struct regulator_config config = { };
	int ret;

	mci = devm_kzalloc(&pdev->dev, sizeof(*mci), GFP_KERNEL);
	if (!mci)
		return -ENOMEM;

	mci->dev = &pdev->dev;
	mci->vinovp = 6500000;
	mutex_init(&mci->chgdet_lock);
	platform_set_drvdata(pdev, mci);
	ret = devm_work_autocancel(&pdev->dev, &mci->chrdet_work, mt6360_chrdet_work);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to set delayed work\n");

	ret = device_property_read_u32(&pdev->dev, "richtek,vinovp-microvolt", &mci->vinovp);
	if (ret)
		dev_warn(&pdev->dev, "Failed to parse vinovp in DT, keep default 6.5v\n");

	mci->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!mci->regmap)
		return dev_err_probe(&pdev->dev, -ENODEV, "Failed to get parent regmap\n");

	ret = mt6360_chg_init_setting(mci);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to initial setting\n");

	memcpy(&mci->psy_desc, &mt6360_charger_desc, sizeof(mci->psy_desc));
	mci->psy_desc.name = dev_name(&pdev->dev);
	charger_cfg.drv_data = mci;
	charger_cfg.of_node = pdev->dev.of_node;
	mci->psy = devm_power_supply_register(&pdev->dev,
					      &mci->psy_desc, &charger_cfg);
	if (IS_ERR(mci->psy))
		return dev_err_probe(&pdev->dev, PTR_ERR(mci->psy),
				     "Failed to register power supply dev\n");


	ret = mt6360_chg_irq_register(pdev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to register irqs\n");

	config.dev = &pdev->dev;
	config.regmap = mci->regmap;
	mci->otg_rdev = devm_regulator_register(&pdev->dev, &mt6360_otg_rdesc,
						&config);
	if (IS_ERR(mci->otg_rdev))
		return PTR_ERR(mci->otg_rdev);

	schedule_work(&mci->chrdet_work);

	return 0;
}

static const struct of_device_id __maybe_unused mt6360_charger_of_id[] = {
	{ .compatible = "mediatek,mt6360-chg", },
	{},
};
MODULE_DEVICE_TABLE(of, mt6360_charger_of_id);

static const struct platform_device_id mt6360_charger_id[] = {
	{ "mt6360-chg", 0 },
	{},
};
MODULE_DEVICE_TABLE(platform, mt6360_charger_id);

static struct platform_driver mt6360_charger_driver = {
	.driver = {
		.name = "mt6360-chg",
		.of_match_table = of_match_ptr(mt6360_charger_of_id),
	},
	.probe = mt6360_charger_probe,
	.id_table = mt6360_charger_id,
};
module_platform_driver(mt6360_charger_driver);

MODULE_AUTHOR("Gene Chen <gene_chen@richtek.com>");
MODULE_DESCRIPTION("MT6360 Charger Driver");
MODULE_LICENSE("GPL");
