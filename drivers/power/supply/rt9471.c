// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Richtek Technology Corp.
 *
 * Authors: Alina Yu <alina_yu@richtek.com>
 *          ChiYuan Huang <cy_huang@richtek.com>
 */

#include <linux/bits.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kstrtox.h>
#include <linux/linear_range.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/sysfs.h>

#define RT9471_REG_OTGCFG	0x00
#define RT9471_REG_TOP		0x01
#define RT9471_REG_FUNC		0x02
#define RT9471_REG_IBUS		0x03
#define RT9471_REG_VBUS		0x04
#define RT9471_REG_PRECHG	0x05
#define RT9471_REG_VCHG		0x07
#define RT9471_REG_ICHG		0x08
#define RT9471_REG_CHGTMR	0x09
#define RT9471_REG_EOC		0x0A
#define RT9471_REG_INFO		0x0B
#define RT9471_REG_JEITA	0x0C
#define RT9471_REG_PUMP_EXP	0x0D
#define	RT9471_REG_DPDMDET	0x0E
#define RT9471_REG_ICSTAT	0x0F
#define	RT9471_REG_STAT0	0x10
#define RT9471_REG_STAT1	0x11
#define RT9471_REG_STAT2	0x12
#define RT9471_REG_IRQ0		0x20
#define RT9471_REG_MASK0	0x30

#define RT9471_OTGCV_MASK	GENMASK(7, 6)
#define RT9471_OTGCC_MASK	BIT(0)
#define RT9471_OTGEN_MASK	BIT(1)
#define RT9471_CHGFAULT_MASK	GENMASK(4, 1)

#define RT9471_NUM_IRQ_REGS	4
#define RT9471_OTGCV_MINUV	4850000
#define RT9471_OTGCV_STEPUV	150000
#define RT9471_NUM_VOTG		4
#define RT9471_VCHG_MAXUV	4700000
#define RT9471_ICHG_MAXUA	3150000

/* Device ID */
#define RT9470_DEVID		0x09
#define RT9470D_DEVID		0x0A
#define RT9471_DEVID		0x0D
#define RT9471D_DEVID		0x0E

/* IRQ number */
#define RT9471_IRQ_BC12_DONE	0
#define RT9471_IRQ_DETACH	1
#define RT9471_IRQ_RECHG	2
#define RT9471_IRQ_CHG_DONE	3
#define RT9471_IRQ_BG_CHG	4
#define RT9471_IRQ_IE0C		5
#define RT9471_IRQ_CHG_RDY	6
#define RT9471_IRQ_VBUS_GD	7
#define RT9471_IRQ_CHG_BATOV	9
#define RT9471_IRQ_CHG_SYSOV	10
#define RT9471_IRQ_CHG_TOUT	11
#define RT9471_IRQ_CHG_BUSUV	12
#define RT9471_IRQ_CHG_THREG	13
#define RT9471_IRQ_CHG_AICR	14
#define RT9471_IRQ_CHG_MIVR	15
#define RT9471_IRQ_SYS_SHORT	16
#define RT9471_IRQ_SYS_MIN	17
#define RT9471_IRQ_AICC_DONE	18
#define RT9471_IRQ_PE_DONE	19
#define RT9471_IRQ_JEITA_COLD	20
#define RT9471_IRQ_JEITA_COOL	21
#define RT9471_IRQ_JEITA_WARM	22
#define RT9471_IRQ_JEITA_HOT	23
#define RT9471_IRQ_OTG_FAULT	24
#define RT9471_IRQ_OTG_LBP	25
#define RT9471_IRQ_OTG_CC	26
#define RT9471_IRQ_WDT		29
#define RT9471_IRQ_VAC_OV	30
#define RT9471_IRQ_OTP		31

enum rt9471_fields {
	F_WDT = 0,
	F_WDT_RST,
	F_CHG_EN,
	F_HZ,
	F_BATFET_DIS,
	F_AICR,
	F_AICC_EN,
	F_MIVR,
	F_IPRE_CHG,
	F_VPRE_CHG,
	F_VBAT_REG,
	F_ICHG_REG,
	F_EOC_RST,
	F_TE,
	F_IEOC_CHG,
	F_DEVICE_ID,
	F_REG_RST,
	F_BC12_EN,
	F_IC_STAT,
	F_PORT_STAT,
	F_ST_CHG_DONE,
	F_ST_CHG_RDY,
	F_ST_VBUS_GD,
	F_MAX_FIELDS
};

enum rt9471_ranges {
	RT9471_RANGE_AICR = 0,
	RT9471_RANGE_MIVR,
	RT9471_RANGE_IPRE,
	RT9471_RANGE_VCHG,
	RT9471_RANGE_ICHG,
	RT9471_RANGE_IEOC,
	RT9471_MAX_RANGES
};

enum {
	RT9471_PORTSTAT_APPLE_10W = 8,
	RT9471_PORTSTAT_SAMSUNG_10W,
	RT9471_PORTSTAT_APPLE_5W,
	RT9471_PORTSTAT_APPLE_12W,
	RT9471_PORTSTAT_NSTD,
	RT9471_PORTSTAT_SDP,
	RT9471_PORTSTAT_CDP,
	RT9471_PORTSTAT_DCP,
};

struct rt9471_chip {
	struct device *dev;
	struct regmap *regmap;
	struct regmap_field *rm_fields[F_MAX_FIELDS];
	struct regmap_irq_chip_data *irq_chip_data;
	struct regulator_dev *otg_rdev;
	struct power_supply *psy;
	struct power_supply_desc psy_desc;
	struct mutex var_lock;
	enum power_supply_usb_type psy_usb_type;
	int psy_usb_curr;
};

static const struct reg_field rt9471_reg_fields[F_MAX_FIELDS] = {
	[F_WDT]		= REG_FIELD(RT9471_REG_TOP, 0, 0),
	[F_WDT_RST]	= REG_FIELD(RT9471_REG_TOP, 1, 1),
	[F_CHG_EN]	= REG_FIELD(RT9471_REG_FUNC, 0, 0),
	[F_HZ]		= REG_FIELD(RT9471_REG_FUNC, 5, 5),
	[F_BATFET_DIS]	= REG_FIELD(RT9471_REG_FUNC, 7, 7),
	[F_AICR]	= REG_FIELD(RT9471_REG_IBUS, 0, 5),
	[F_AICC_EN]	= REG_FIELD(RT9471_REG_IBUS, 7, 7),
	[F_MIVR]	= REG_FIELD(RT9471_REG_VBUS, 0, 3),
	[F_IPRE_CHG]	= REG_FIELD(RT9471_REG_PRECHG, 0, 3),
	[F_VPRE_CHG]	= REG_FIELD(RT9471_REG_PRECHG, 4, 6),
	[F_VBAT_REG]	= REG_FIELD(RT9471_REG_VCHG, 0, 6),
	[F_ICHG_REG]	= REG_FIELD(RT9471_REG_ICHG, 0, 5),
	[F_EOC_RST]	= REG_FIELD(RT9471_REG_EOC, 0, 0),
	[F_TE]		= REG_FIELD(RT9471_REG_EOC, 1, 1),
	[F_IEOC_CHG]	= REG_FIELD(RT9471_REG_EOC, 4, 7),
	[F_DEVICE_ID]	= REG_FIELD(RT9471_REG_INFO, 3, 6),
	[F_REG_RST]	= REG_FIELD(RT9471_REG_INFO, 7, 7),
	[F_BC12_EN]	= REG_FIELD(RT9471_REG_DPDMDET, 7, 7),
	[F_IC_STAT]	= REG_FIELD(RT9471_REG_ICSTAT, 0, 3),
	[F_PORT_STAT]	= REG_FIELD(RT9471_REG_ICSTAT, 4, 7),
	[F_ST_CHG_DONE]	= REG_FIELD(RT9471_REG_STAT0, 3, 3),
	[F_ST_CHG_RDY]	= REG_FIELD(RT9471_REG_STAT0, 6, 6),
	[F_ST_VBUS_GD]	= REG_FIELD(RT9471_REG_STAT0, 7, 7),
};

static const struct linear_range rt9471_chg_ranges[RT9471_MAX_RANGES] = {
	[RT9471_RANGE_AICR] = { .min = 50000,	.min_sel = 1, .max_sel = 63, .step = 50000 },
	[RT9471_RANGE_MIVR] = { .min = 3900000,	.min_sel = 0, .max_sel = 15, .step = 100000 },
	[RT9471_RANGE_IPRE] = { .min = 50000,	.min_sel = 0, .max_sel = 15, .step = 50000 },
	[RT9471_RANGE_VCHG] = { .min = 3900000,	.min_sel = 0, .max_sel = 80, .step = 10000 },
	[RT9471_RANGE_ICHG] = { .min = 0,	.min_sel = 0, .max_sel = 63, .step = 50000 },
	[RT9471_RANGE_IEOC] = { .min = 50000,	.min_sel = 0, .max_sel = 15, .step = 50000 },
};

static int rt9471_set_value_by_field_range(struct rt9471_chip *chip,
					   enum rt9471_fields field,
					   enum rt9471_ranges range, int val)
{
	unsigned int sel;

	if (val < 0)
		return -EINVAL;

	linear_range_get_selector_within(rt9471_chg_ranges + range, val, &sel);

	return regmap_field_write(chip->rm_fields[field], sel);
}


static int rt9471_get_value_by_field_range(struct rt9471_chip *chip,
					   enum rt9471_fields field,
					   enum rt9471_ranges range, int *val)
{
	unsigned int sel, rvalue;
	int ret;

	ret = regmap_field_read(chip->rm_fields[field], &sel);
	if (ret)
		return ret;

	ret = linear_range_get_value(rt9471_chg_ranges + range, sel, &rvalue);
	if (ret)
		return ret;

	*val = rvalue;
	return 0;
}

static int rt9471_set_ieoc(struct rt9471_chip *chip, int microamp)
{
	int ret;

	if (microamp == 0)
		return regmap_field_write(chip->rm_fields[F_TE], 0);

	ret = rt9471_set_value_by_field_range(chip, F_IEOC_CHG, RT9471_RANGE_IEOC, microamp);
	if (ret)
		return ret;

	/* After applying the new IEOC value, enable charge termination */
	return regmap_field_write(chip->rm_fields[F_TE], 1);
}

static int rt9471_get_ieoc(struct rt9471_chip *chip, int *microamp)
{
	unsigned int chg_term_enable;
	int ret;

	ret = regmap_field_read(chip->rm_fields[F_TE], &chg_term_enable);
	if (ret)
		return ret;

	if (!chg_term_enable) {
		*microamp = 0;
		return 0;
	}

	return rt9471_get_value_by_field_range(chip, F_IEOC_CHG, RT9471_RANGE_IEOC, microamp);
}

static int rt9471_get_status(struct rt9471_chip *chip, int *status)
{
	unsigned int chg_ready, chg_done, fault_stat;
	int ret;

	ret = regmap_field_read(chip->rm_fields[F_ST_CHG_RDY], &chg_ready);
	if (ret)
		return ret;

	ret = regmap_field_read(chip->rm_fields[F_ST_CHG_DONE], &chg_done);
	if (ret)
		return ret;

	ret = regmap_read(chip->regmap, RT9471_REG_STAT1, &fault_stat);
	if (ret)
		return ret;

	fault_stat &= RT9471_CHGFAULT_MASK;

	if (chg_ready && chg_done)
		*status = POWER_SUPPLY_STATUS_FULL;
	else if (chg_ready && fault_stat)
		*status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	else if (chg_ready && !fault_stat)
		*status = POWER_SUPPLY_STATUS_CHARGING;
	else
		*status = POWER_SUPPLY_STATUS_DISCHARGING;

	return 0;
}

static int rt9471_get_vbus_good(struct rt9471_chip *chip, int *stat)
{
	unsigned int vbus_gd;
	int ret;

	ret = regmap_field_read(chip->rm_fields[F_ST_VBUS_GD], &vbus_gd);
	if (ret)
		return ret;

	*stat = vbus_gd;
	return 0;
}

static int rt9471_get_usb_type(struct rt9471_chip *chip, int *usb_type)
{
	mutex_lock(&chip->var_lock);
	*usb_type = chip->psy_usb_type;
	mutex_unlock(&chip->var_lock);

	return 0;
}

static int rt9471_get_usb_type_current(struct rt9471_chip *chip,
					      int *microamp)
{
	mutex_lock(&chip->var_lock);
	*microamp = chip->psy_usb_curr;
	mutex_unlock(&chip->var_lock);

	return 0;
}

static enum power_supply_property rt9471_charger_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static enum power_supply_usb_type rt9471_charger_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,
};

static int rt9471_charger_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
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

static int rt9471_charger_set_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       const union power_supply_propval *val)
{
	struct rt9471_chip *chip = power_supply_get_drvdata(psy);
	int value = val->intval;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		return regmap_field_write(chip->rm_fields[F_CHG_EN], !!value);
	case POWER_SUPPLY_PROP_ONLINE:
		return regmap_field_write(chip->rm_fields[F_HZ], !value);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return rt9471_set_value_by_field_range(chip, F_ICHG_REG, RT9471_RANGE_ICHG, value);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return rt9471_set_value_by_field_range(chip, F_VBAT_REG, RT9471_RANGE_VCHG, value);
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return rt9471_set_value_by_field_range(chip, F_AICR, RT9471_RANGE_AICR, value);
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		return rt9471_set_value_by_field_range(chip, F_MIVR, RT9471_RANGE_MIVR, value);
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		return rt9471_set_value_by_field_range(chip, F_IPRE_CHG, RT9471_RANGE_IPRE, value);
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return rt9471_set_ieoc(chip, val->intval);
	default:
		return -EINVAL;
	}
}

static const char * const rt9471_manufacturer	= "Richtek Technology Corp.";
static const char * const rt9471_model		= "RT9471";

static int rt9471_charger_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct rt9471_chip *chip = power_supply_get_drvdata(psy);
	int *pvalue = &val->intval;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		return rt9471_get_status(chip, pvalue);
	case POWER_SUPPLY_PROP_ONLINE:
		return rt9471_get_vbus_good(chip, pvalue);
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return rt9471_get_usb_type_current(chip, pvalue);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return rt9471_get_value_by_field_range(chip, F_ICHG_REG, RT9471_RANGE_ICHG, pvalue);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		*pvalue = RT9471_ICHG_MAXUA;
		return 0;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return rt9471_get_value_by_field_range(chip, F_VBAT_REG, RT9471_RANGE_VCHG, pvalue);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = RT9471_VCHG_MAXUV;
		return 0;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return rt9471_get_value_by_field_range(chip, F_AICR, RT9471_RANGE_AICR, pvalue);
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		return rt9471_get_value_by_field_range(chip, F_MIVR, RT9471_RANGE_MIVR, pvalue);
	case POWER_SUPPLY_PROP_USB_TYPE:
		return rt9471_get_usb_type(chip, pvalue);
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		return rt9471_get_value_by_field_range(chip, F_IPRE_CHG, RT9471_RANGE_IPRE, pvalue);
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		return rt9471_get_ieoc(chip, pvalue);
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = rt9471_model;
		return 0;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = rt9471_manufacturer;
		return 0;
	default:
		return -ENODATA;
	}
}

static irqreturn_t rt9471_vbus_gd_handler(int irqno, void *devid)
{
	struct rt9471_chip *chip = devid;

	power_supply_changed(chip->psy);

	return IRQ_HANDLED;
}

static irqreturn_t rt9471_detach_handler(int irqno, void *devid)
{
	struct rt9471_chip *chip = devid;
	unsigned int vbus_gd;
	int ret;

	ret = regmap_field_read(chip->rm_fields[F_ST_VBUS_GD], &vbus_gd);
	if (ret)
		return IRQ_NONE;

	/* Only focus on really detached */
	if (vbus_gd)
		return IRQ_HANDLED;

	mutex_lock(&chip->var_lock);
	chip->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
	chip->psy_usb_curr = 0;
	mutex_unlock(&chip->var_lock);

	power_supply_changed(chip->psy);

	return IRQ_HANDLED;
}

static irqreturn_t rt9471_bc12_done_handler(int irqno, void *devid)
{
	struct rt9471_chip *chip = devid;
	enum power_supply_usb_type usb_type;
	unsigned int port_stat;
	int usb_curr, ret;

	ret = regmap_field_read(chip->rm_fields[F_PORT_STAT], &port_stat);
	if (ret)
		return IRQ_NONE;

	switch (port_stat) {
	case RT9471_PORTSTAT_APPLE_10W:
		usb_type = POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID;
		usb_curr = 2000000;
		break;
	case RT9471_PORTSTAT_APPLE_5W:
		usb_type = POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID;
		usb_curr = 1000000;
		break;
	case RT9471_PORTSTAT_APPLE_12W:
		usb_type = POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID;
		usb_curr = 2400000;
		break;
	case RT9471_PORTSTAT_SAMSUNG_10W:
		usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		usb_curr = 2000000;
		break;
	case RT9471_PORTSTAT_DCP:
		usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		usb_curr = 1500000;
		break;
	case RT9471_PORTSTAT_NSTD:
	case RT9471_PORTSTAT_SDP:
		usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		usb_curr = 500000;
		break;
	case RT9471_PORTSTAT_CDP:
		usb_type = POWER_SUPPLY_USB_TYPE_CDP;
		usb_curr = 1500000;
		break;
	default:
		usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		usb_curr = 0;
		break;
	}

	mutex_lock(&chip->var_lock);
	chip->psy_usb_type = usb_type;
	chip->psy_usb_curr = usb_curr;
	mutex_unlock(&chip->var_lock);

	power_supply_changed(chip->psy);

	return IRQ_HANDLED;
}

static irqreturn_t rt9471_wdt_handler(int irqno, void *devid)
{
	struct rt9471_chip *chip = devid;
	int ret;

	ret = regmap_field_write(chip->rm_fields[F_WDT_RST], 1);

	return ret ? IRQ_NONE : IRQ_HANDLED;
}

static irqreturn_t rt9471_otg_fault_handler(int irqno, void *devid)
{
	struct rt9471_chip *chip = devid;

	regulator_notifier_call_chain(chip->otg_rdev, REGULATOR_EVENT_FAIL, NULL);

	return IRQ_HANDLED;
}

#define RT9471_IRQ_DESC(_name, _hwirq) \
{ \
	.name = #_name, \
	.hwirq = _hwirq, \
	.handler = rt9471_##_name##_handler, \
}

static int rt9471_register_interrupts(struct rt9471_chip *chip)
{
	struct device *dev = chip->dev;
	static const struct {
		char *name;
		int hwirq;
		irq_handler_t handler;
	} chg_irqs[] = {
		RT9471_IRQ_DESC(vbus_gd, RT9471_IRQ_VBUS_GD),
		RT9471_IRQ_DESC(detach, RT9471_IRQ_DETACH),
		RT9471_IRQ_DESC(bc12_done, RT9471_IRQ_BC12_DONE),
		RT9471_IRQ_DESC(wdt, RT9471_IRQ_WDT),
		RT9471_IRQ_DESC(otg_fault, RT9471_IRQ_OTG_FAULT),
	}, *curr;
	int i, virq, ret;

	for (i = 0; i < ARRAY_SIZE(chg_irqs); i++) {
		curr = chg_irqs + i;

		virq = regmap_irq_get_virq(chip->irq_chip_data, curr->hwirq);
		if (virq <= 0)
			return virq;

		ret = devm_request_threaded_irq(dev, virq, NULL, curr->handler,
						IRQF_ONESHOT, curr->name, chip);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to register IRQ (%s)\n",
					     curr->name);
	}

	return 0;
}

static const struct regulator_ops rt9471_otg_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.set_current_limit = regulator_set_current_limit_regmap,
	.get_current_limit = regulator_get_current_limit_regmap,
};

static const unsigned int rt9471_otg_microamp[] = { 500000, 1200000, };

static const struct regulator_desc rt9471_otg_rdesc = {
	.of_match = of_match_ptr("usb-otg-vbus-regulator"),
	.name = "rt9471-otg-vbus",
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.ops = &rt9471_otg_ops,
	.min_uV = RT9471_OTGCV_MINUV,
	.uV_step = RT9471_OTGCV_STEPUV,
	.n_voltages = RT9471_NUM_VOTG,
	.curr_table = rt9471_otg_microamp,
	.n_current_limits = ARRAY_SIZE(rt9471_otg_microamp),
	.enable_mask = RT9471_OTGEN_MASK,
	.enable_reg = RT9471_REG_FUNC,
	.vsel_reg = RT9471_REG_OTGCFG,
	.vsel_mask = RT9471_OTGCV_MASK,
	.csel_reg = RT9471_REG_OTGCFG,
	.csel_mask = RT9471_OTGCC_MASK,
};

static int rt9471_register_otg_regulator(struct rt9471_chip *chip)
{
	struct device *dev = chip->dev;
	struct regulator_config cfg = { .dev = dev, .driver_data = chip };

	chip->otg_rdev = devm_regulator_register(dev, &rt9471_otg_rdesc, &cfg);

	return PTR_ERR_OR_ZERO(chip->otg_rdev);
}

static inline struct rt9471_chip *psy_device_to_chip(struct device *dev)
{
	return power_supply_get_drvdata(to_power_supply(dev));
}

static ssize_t sysoff_enable_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct rt9471_chip *chip = psy_device_to_chip(dev);
	unsigned int sysoff_enable;
	int ret;

	ret = regmap_field_read(chip->rm_fields[F_BATFET_DIS], &sysoff_enable);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", sysoff_enable);
}

static ssize_t sysoff_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct rt9471_chip *chip = psy_device_to_chip(dev);
	unsigned int tmp;
	int ret;

	ret = kstrtouint(buf, 10, &tmp);
	if (ret)
		return ret;

	ret = regmap_field_write(chip->rm_fields[F_BATFET_DIS], !!tmp);
	if (ret)
		return ret;

	return count;
}

static ssize_t port_detect_enable_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct rt9471_chip *chip = psy_device_to_chip(dev);
	unsigned int bc12_enable;
	int ret;

	ret = regmap_field_read(chip->rm_fields[F_BC12_EN], &bc12_enable);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", bc12_enable);
}

static ssize_t port_detect_enable_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct rt9471_chip *chip = psy_device_to_chip(dev);
	unsigned int tmp;
	int ret;

	ret = kstrtouint(buf, 10, &tmp);
	if (ret)
		return ret;

	ret = regmap_field_write(chip->rm_fields[F_BC12_EN], !!tmp);
	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(sysoff_enable);
static DEVICE_ATTR_RW(port_detect_enable);

static struct attribute *rt9471_sysfs_attrs[] = {
	&dev_attr_sysoff_enable.attr,
	&dev_attr_port_detect_enable.attr,
	NULL
};

ATTRIBUTE_GROUPS(rt9471_sysfs);

static int rt9471_register_psy(struct rt9471_chip *chip)
{
	struct device *dev = chip->dev;
	struct power_supply_desc *desc = &chip->psy_desc;
	struct power_supply_config cfg = {};
	char *psy_name;

	cfg.drv_data = chip;
	cfg.of_node = dev->of_node;
	cfg.attr_grp = rt9471_sysfs_groups;

	psy_name = devm_kasprintf(dev, GFP_KERNEL, "rt9471-%s", dev_name(dev));
	if (!psy_name)
		return -ENOMEM;

	desc->name = psy_name;
	desc->type = POWER_SUPPLY_TYPE_USB;
	desc->usb_types = rt9471_charger_usb_types;
	desc->num_usb_types = ARRAY_SIZE(rt9471_charger_usb_types);
	desc->properties = rt9471_charger_properties;
	desc->num_properties = ARRAY_SIZE(rt9471_charger_properties);
	desc->get_property = rt9471_charger_get_property;
	desc->set_property = rt9471_charger_set_property;
	desc->property_is_writeable = rt9471_charger_property_is_writeable;

	chip->psy = devm_power_supply_register(dev, desc, &cfg);

	return PTR_ERR_OR_ZERO(chip->psy);
}

static const struct regmap_irq rt9471_regmap_irqs[] = {
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_BC12_DONE, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_DETACH, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_RECHG, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_CHG_DONE, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_BG_CHG, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_IE0C, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_CHG_RDY, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_VBUS_GD, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_CHG_BATOV, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_CHG_SYSOV, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_CHG_TOUT, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_CHG_BUSUV, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_CHG_THREG, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_CHG_AICR, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_CHG_MIVR, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_SYS_SHORT, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_SYS_MIN, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_AICC_DONE, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_PE_DONE, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_JEITA_COLD, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_JEITA_COOL, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_JEITA_WARM, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_JEITA_HOT, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_OTG_FAULT, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_OTG_LBP, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_OTG_CC, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_WDT, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_VAC_OV, 8),
	REGMAP_IRQ_REG_LINE(RT9471_IRQ_OTP, 8),
};

static const struct regmap_irq_chip rt9471_irq_chip = {
	.name = "rt9471-irqs",
	.status_base = RT9471_REG_IRQ0,
	.mask_base = RT9471_REG_MASK0,
	.num_regs = RT9471_NUM_IRQ_REGS,
	.irqs = rt9471_regmap_irqs,
	.num_irqs = ARRAY_SIZE(rt9471_regmap_irqs),
};

static const struct reg_sequence rt9471_init_regs[] = {
	REG_SEQ0(RT9471_REG_INFO, 0x80), /* REG_RST */
	REG_SEQ0(RT9471_REG_TOP, 0xC0), /* WDT = 0 */
	REG_SEQ0(RT9471_REG_FUNC, 0x01), /* BATFET_DIS_DLY = 0 */
	REG_SEQ0(RT9471_REG_IBUS, 0x0A), /* AUTO_AICR = 0 */
	REG_SEQ0(RT9471_REG_VBUS, 0xC6), /* VAC_OVP = 14V */
	REG_SEQ0(RT9471_REG_JEITA, 0x38), /* JEITA = 0 */
	REG_SEQ0(RT9471_REG_DPDMDET, 0x31), /* BC12_EN = 0, DCP_DP_OPT = 1 */
};

static int rt9471_check_devinfo(struct rt9471_chip *chip)
{
	struct device *dev = chip->dev;
	unsigned int dev_id;
	int ret;

	ret = regmap_field_read(chip->rm_fields[F_DEVICE_ID], &dev_id);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read device_id\n");

	switch (dev_id) {
	case RT9470_DEVID:
	case RT9470D_DEVID:
	case RT9471_DEVID:
	case RT9471D_DEVID:
		return 0;
	default:
		return dev_err_probe(dev, -ENODEV, "Incorrect device id\n");
	}
}

static bool rt9471_accessible_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x00 ... 0x0F:
	case 0x10 ... 0x13:
	case 0x20 ... 0x33:
	case 0x40 ... 0xA1:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rt9471_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xA1,
	.writeable_reg = rt9471_accessible_reg,
	.readable_reg = rt9471_accessible_reg,
};

static int rt9471_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct rt9471_chip *chip;
	struct gpio_desc *ce_gpio;
	struct regmap *regmap;
	int ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;
	mutex_init(&chip->var_lock);
	i2c_set_clientdata(i2c, chip);

	/* Default pull charge enable gpio to make 'CHG_EN' by SW control only */
	ce_gpio = devm_gpiod_get_optional(dev, "charge-enable", GPIOD_OUT_HIGH);
	if (IS_ERR(ce_gpio))
		return dev_err_probe(dev, PTR_ERR(ce_gpio),
				     "Failed to config charge enable gpio\n");

	regmap = devm_regmap_init_i2c(i2c, &rt9471_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to init regmap\n");

	chip->regmap = regmap;

	ret = devm_regmap_field_bulk_alloc(dev, regmap, chip->rm_fields,
					   rt9471_reg_fields,
					   ARRAY_SIZE(rt9471_reg_fields));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to alloc regmap field\n");

	ret = rt9471_check_devinfo(chip);
	if (ret)
		return ret;

	ret = regmap_register_patch(regmap, rt9471_init_regs,
				    ARRAY_SIZE(rt9471_init_regs));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init registers\n");

	ret = devm_regmap_add_irq_chip(dev, regmap, i2c->irq,
				       IRQF_TRIGGER_FALLING | IRQF_ONESHOT, 0,
				       &rt9471_irq_chip, &chip->irq_chip_data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add IRQ chip\n");

	ret = rt9471_register_psy(chip);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register psy\n");

	ret = rt9471_register_otg_regulator(chip);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register otg\n");

	ret = rt9471_register_interrupts(chip);
	if (ret)
		return ret;

	/* After IRQs are all initialized, enable port detection by default */
	return regmap_field_write(chip->rm_fields[F_BC12_EN], 1);
}

static void rt9471_shutdown(struct i2c_client *i2c)
{
	struct rt9471_chip *chip = i2c_get_clientdata(i2c);

	/*
	 * There's no external reset pin. Do register reset to guarantee charger
	 * function is normal after shutdown
	 */
	regmap_field_write(chip->rm_fields[F_REG_RST], 1);
}

static const struct of_device_id rt9471_of_device_id[] = {
	{ .compatible = "richtek,rt9471" },
	{}
};
MODULE_DEVICE_TABLE(of, rt9471_of_device_id);

static struct i2c_driver rt9471_driver = {
	.driver = {
		.name = "rt9471",
		.of_match_table = rt9471_of_device_id,
	},
	.probe = rt9471_probe,
	.shutdown = rt9471_shutdown,
};
module_i2c_driver(rt9471_driver);

MODULE_DESCRIPTION("Richtek RT9471 charger driver");
MODULE_AUTHOR("Alina Yu <alina_yu@richtek.com>");
MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_LICENSE("GPL");
