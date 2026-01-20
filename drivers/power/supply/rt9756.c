// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (C) 2025 Richtek Technology Corp.
//
// Authors: ChiYuan Huang <cy_huang@richtek.com>

#include <linux/atomic.h>
#include <linux/cleanup.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/linear_range.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/sysfs.h>
#include <linux/util_macros.h>

#define RT9756_REG_INTFLAG1	0x0B
#define RT9756_REG_INTFLAG2	0x0D
#define RT9756_REG_INTFLAG3	0x0F
#define RT9756_REG_ADCCTL	0x11
#define RT9756_REG_VBUSADC	0x12
#define RT9756_REG_BC12FLAG	0x45
#define RT9756_REG_INTFLAG4	0x49

/* Flag1 */
#define RT9756_EVT_BUSOVP	BIT(3)
#define RT9756_EVT_BUSOCP	BIT(2)
#define RT9756_EVT_BUSUCP	BIT(0)
/* Flag2 */
#define RT9756_EVT_BATOVP	BIT(7)
#define RT9756_EVT_BATOCP	BIT(6)
#define RT9756_EVT_TDIEOTP	BIT(3)
#define RT9756_EVT_VBUSLOW_ERR	BIT(2)
#define RT9756_EVT_VAC_INSERT	BIT(0)
/* Flag3 */
#define RT9756_EVT_WDT		BIT(5)
#define RT9756_EVT_VAC_UVLO	BIT(4)
/* ADCCTL */
#define RT9756_ADCEN_MASK	BIT(7)
#define RT9756_ADCONCE_MASK	BIT(6)
/* Bc12_flag */
#define RT9756_EVT_BC12_DONE	BIT(3)
/* Flag4 */
#define RT9756_EVT_OUTOVP	BIT(0)

#define RICHTEK_DEVID		7
#define RT9756_REVID		0
#define RT9756A_REVID		1
#define RT9757_REVID		2
#define RT9757A_REVID		3
#define RT9756_ADC_CONVTIME	1200
#define RT9756_ADC_MAXWAIT	16000

enum rt9756_model {
	MODEL_RT9756 = 0,
	MODEL_RT9757,
	MODEL_RT9770,
	MODEL_MAX
};

enum rt9756_adc_chan {
	ADC_VBUS = 0,
	ADC_IBUS,
	ADC_VBAT,
	ADC_IBAT,
	ADC_TDIE,
	ADC_MAX_CHANNEL
};

enum rt9756_usb_type {
	USB_NO_VBUS = 0,
	USB_SDP = 2,
	USB_NSTD,
	USB_DCP,
	USB_CDP,
	MAX_USB_TYPE
};

enum rt9756_fields {
	F_VBATOVP = 0,
	F_VBATOVP_EN,
	F_IBATOCP,
	F_IBATOCP_EN,
	F_VBUSOVP,
	F_VBUSOVP_EN,
	F_IBUSOCP,
	F_IBUSOCP_EN,
	F_SWITCHING,
	F_REG_RST,
	F_CHG_EN,
	F_OP_MODE,
	F_WDT_DIS,
	F_WDT_TMR,
	F_DEV_ID,
	F_BC12_EN,
	F_USB_STATE,
	F_VBUS_STATE,
	F_IBAT_RSEN,
	F_REVISION,
	F_MAX_FIELD
};

enum rt9756_ranges {
	R_VBATOVP = 0,
	R_IBATOCP,
	R_VBUSOVP,
	R_IBUSOCP,
	R_MAX_RANGE
};

static const struct reg_field rt9756_chg_fields[F_MAX_FIELD] = {
	[F_VBATOVP]	= REG_FIELD(0x08, 0, 4),
	[F_VBATOVP_EN]	= REG_FIELD(0x08, 7, 7),
	[F_IBATOCP]	= REG_FIELD(0x09, 0, 5),
	[F_IBATOCP_EN]	= REG_FIELD(0x09, 7, 7),
	[F_VBUSOVP]	= REG_FIELD(0x06, 0, 5),
	[F_VBUSOVP_EN]	= REG_FIELD(0x06, 7, 7),
	[F_IBUSOCP]	= REG_FIELD(0x07, 0, 4),
	[F_IBUSOCP_EN]	= REG_FIELD(0x07, 5, 5),
	[F_SWITCHING]	= REG_FIELD(0x5c, 7, 7),
	[F_REG_RST]	= REG_FIELD(0x00, 7, 7),
	[F_CHG_EN]	= REG_FIELD(0x00, 6, 6),
	[F_OP_MODE]	= REG_FIELD(0x00, 5, 5),
	[F_WDT_DIS]	= REG_FIELD(0x00, 3, 3),
	[F_WDT_TMR]	= REG_FIELD(0x00, 0, 2),
	[F_DEV_ID]	= REG_FIELD(0x03, 0, 3),
	[F_BC12_EN]	= REG_FIELD(0x44, 7, 7),
	[F_USB_STATE]	= REG_FIELD(0x46, 5, 7),
	[F_VBUS_STATE]	= REG_FIELD(0x4c, 0, 0),
	[F_IBAT_RSEN]	= REG_FIELD(0x5e, 0, 1),
	[F_REVISION]	= REG_FIELD(0x62, 0, 1),
};

static const struct reg_field rt9770_chg_fields[F_MAX_FIELD] = {
	[F_VBATOVP]	= REG_FIELD(0x08, 0, 4),
	[F_VBATOVP_EN]	= REG_FIELD(0x08, 7, 7),
	[F_IBATOCP]	= REG_FIELD(0x09, 0, 5),
	[F_IBATOCP_EN]	= REG_FIELD(0x09, 7, 7),
	[F_VBUSOVP]	= REG_FIELD(0x06, 0, 5),
	[F_VBUSOVP_EN]	= REG_FIELD(0x06, 7, 7),
	[F_IBUSOCP]	= REG_FIELD(0x07, 0, 4),
	[F_IBUSOCP_EN]	= REG_FIELD(0x07, 5, 5),
	[F_SWITCHING]	= REG_FIELD(0x5c, 7, 7),
	[F_REG_RST]	= REG_FIELD(0x00, 7, 7),
	[F_CHG_EN]	= REG_FIELD(0x00, 6, 6),
	[F_OP_MODE]	= REG_FIELD(0x00, 5, 5),
	[F_WDT_DIS]	= REG_FIELD(0x00, 3, 3),
	[F_WDT_TMR]	= REG_FIELD(0x00, 0, 2),
	[F_DEV_ID]	= REG_FIELD(0x60, 0, 3),
	[F_BC12_EN]	= REG_FIELD(0x03, 7, 7),
	[F_USB_STATE]	= REG_FIELD(0x02, 5, 7),
	[F_VBUS_STATE]	= REG_FIELD(0x4c, 0, 0),
	[F_IBAT_RSEN]	= REG_FIELD(0x5e, 0, 1),
	[F_REVISION]	= REG_FIELD(0x62, 3, 7),
};

/* All converted to microvolt or microamp */
static const struct linear_range rt9756_chg_ranges[R_MAX_RANGE] = {
	LINEAR_RANGE_IDX(R_VBATOVP, 4200000, 0, 31, 25000),
	LINEAR_RANGE_IDX(R_IBATOCP, 2000000, 0, 63, 100000),
	LINEAR_RANGE_IDX(R_VBUSOVP, 3000000, 0, 63, 50000),
	LINEAR_RANGE_IDX(R_IBUSOCP, 1000000, 0, 31, 250000),
};

struct charger_event {
	unsigned int flag1;
	unsigned int flag2;
	unsigned int flag3;
	unsigned int flag4;
};

struct rt9756_data {
	struct device *dev;
	struct regmap *regmap;
	struct regmap_field *rm_fields[F_MAX_FIELD];
	struct power_supply *psy;
	struct power_supply *bat_psy;
	struct mutex adc_lock;
	struct power_supply_desc psy_desc;
	struct power_supply_desc bat_psy_desc;
	struct charger_event chg_evt;
	unsigned int rg_resistor;
	unsigned int real_resistor;
	enum rt9756_model model;
	atomic_t usb_type;
};

struct rt975x_dev_data {
	const struct regmap_config *regmap_config;
	const struct reg_field *reg_fields;
	const struct reg_sequence *init_regs;
	size_t num_init_regs;
	int (*check_device_model)(struct rt9756_data *data);
};

static int rt9756_get_value_field_range(struct rt9756_data *data, enum rt9756_fields en_field,
					enum rt9756_fields field, enum rt9756_ranges rsel, int *val)
{
	const struct linear_range *range = rt9756_chg_ranges + rsel;
	unsigned int enable, selector, value;
	int ret;

	ret = regmap_field_read(data->rm_fields[en_field], &enable);
	if (ret)
		return ret;

	if (!enable) {
		*val = 0;
		return 0;
	}

	ret = regmap_field_read(data->rm_fields[field], &selector);
	if (ret)
		return ret;

	ret = linear_range_get_value(range, selector, &value);
	if (ret)
		return ret;

	*val = (int)value;

	return 0;
}

static int rt9756_set_value_field_range(struct rt9756_data *data, enum rt9756_fields en_field,
					enum rt9756_fields field, enum rt9756_ranges rsel, int val)
{
	const struct linear_range *range = rt9756_chg_ranges + rsel;
	unsigned int selector, value;
	int ret;

	if (!val)
		return regmap_field_write(data->rm_fields[en_field], 0);

	value = (unsigned int)val;
	linear_range_get_selector_within(range, value, &selector);
	ret = regmap_field_write(data->rm_fields[field], selector);
	if (ret)
		return ret;

	return regmap_field_write(data->rm_fields[en_field], 1);
}

static int rt9756_get_adc(struct rt9756_data *data, enum rt9756_adc_chan chan,
			  int *val)
{
	struct regmap *regmap = data->regmap;
	unsigned int reg_addr = RT9756_REG_VBUSADC + chan * 2;
	unsigned int mask = RT9756_ADCEN_MASK | RT9756_ADCONCE_MASK;
	unsigned int shift = 0, adc_cntl;
	__be16 raws;
	int scale, offset = 0, ret;

	guard(mutex)(&data->adc_lock);

	ret = regmap_update_bits(regmap, RT9756_REG_ADCCTL, mask, mask);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(regmap, RT9756_REG_ADCCTL, adc_cntl,
				       !(adc_cntl & RT9756_ADCEN_MASK),
				       RT9756_ADC_CONVTIME, RT9756_ADC_MAXWAIT);
	if (ret && ret != -ETIMEDOUT)
		return ret;

	ret = regmap_raw_read(regmap, reg_addr, &raws, sizeof(raws));
	if (ret)
		return ret;

	/*
	 * TDIE LSB 1'c, others LSB 1000uV or 1000uA.
	 * Rsense ratio is needed for IBAT channel
	 */
	if (chan == ADC_TDIE) {
		scale = 10;
		shift = 8;
		offset = -40;
	} else if (chan == ADC_IBAT)
		scale = 1000 * data->rg_resistor / data->real_resistor;
	else
		scale = 1000;

	*val = ((be16_to_cpu(raws) >> shift) + offset) * scale;

	return regmap_update_bits(regmap, RT9756_REG_ADCCTL, mask, 0);
}

static int rt9756_get_switching_state(struct rt9756_data *data, int *status)
{
	unsigned int switching_state;
	int ret;

	ret = regmap_field_read(data->rm_fields[F_SWITCHING], &switching_state);
	if (ret)
		return ret;

	if (switching_state)
		*status = POWER_SUPPLY_STATUS_CHARGING;
	else
		*status = POWER_SUPPLY_STATUS_NOT_CHARGING;

	return 0;
}

static int rt9756_get_charger_health(struct rt9756_data *data)
{
	struct charger_event *evt = &data->chg_evt;

	if (evt->flag2 & RT9756_EVT_VBUSLOW_ERR)
		return POWER_SUPPLY_HEALTH_UNDERVOLTAGE;

	if (evt->flag1 & RT9756_EVT_BUSOVP || evt->flag2 & RT9756_EVT_BATOVP ||
	    evt->flag4 & RT9756_EVT_OUTOVP)
		return POWER_SUPPLY_HEALTH_OVERVOLTAGE;

	if (evt->flag1 & RT9756_EVT_BUSOCP || evt->flag2 & RT9756_EVT_BATOCP)
		return POWER_SUPPLY_HEALTH_OVERCURRENT;

	if (evt->flag1 & RT9756_EVT_BUSUCP)
		return POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;

	if (evt->flag2 & RT9756_EVT_TDIEOTP)
		return POWER_SUPPLY_HEALTH_OVERHEAT;

	if (evt->flag3 & RT9756_EVT_WDT)
		return POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE;

	return POWER_SUPPLY_HEALTH_GOOD;
}

static int rt9756_get_charger_online(struct rt9756_data *data, int *val)
{
	unsigned int online;
	int ret;

	ret = regmap_field_read(data->rm_fields[F_VBUS_STATE], &online);
	if (ret)
		return ret;

	*val = !!online;
	return 0;
}

static int rt9756_get_vbus_ovp(struct rt9756_data *data, int *val)
{
	unsigned int opmode;
	int ovpval, ret;

	/* operating mode -> 0 bypass, 1 div2 */
	ret = regmap_field_read(data->rm_fields[F_OP_MODE], &opmode);
	if (ret)
		return ret;

	ret = rt9756_get_value_field_range(data, F_VBUSOVP_EN, F_VBUSOVP, R_VBUSOVP, &ovpval);
	if (ret)
		return ret;

	*val = opmode ? ovpval * 2 : ovpval;
	return 0;
}

static int rt9756_set_vbus_ovp(struct rt9756_data *data, int val)
{
	unsigned int opmode;
	int ret;

	/* operating mode -> 0 bypass, 1 div2 */
	ret = regmap_field_read(data->rm_fields[F_OP_MODE], &opmode);
	if (ret)
		return ret;

	return rt9756_set_value_field_range(data, F_VBUSOVP_EN, F_VBUSOVP, R_VBUSOVP,
					    opmode ? val / 2 : val);
}

static const char * const rt9756_manufacturer = "Richtek Technology Corp.";
static const char * const rt9756_model[MODEL_MAX] =  { "RT9756", "RT9757", "RT9770" };

static int rt9756_psy_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct rt9756_data *data = power_supply_get_drvdata(psy);
	int *pval = &val->intval;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		return rt9756_get_switching_state(data, pval);
	case POWER_SUPPLY_PROP_HEALTH:
		*pval = rt9756_get_charger_health(data);
		return 0;
	case POWER_SUPPLY_PROP_ONLINE:
		return rt9756_get_charger_online(data, pval);
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return rt9756_get_vbus_ovp(data, pval);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return rt9756_get_adc(data, ADC_VBUS, pval);
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return rt9756_get_value_field_range(data, F_IBUSOCP_EN, F_IBUSOCP, R_IBUSOCP, pval);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return rt9756_get_adc(data, ADC_IBUS, pval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		return rt9756_get_value_field_range(data, F_VBATOVP_EN, F_VBATOVP, R_VBATOVP, pval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		return rt9756_get_value_field_range(data, F_IBATOCP_EN, F_IBATOCP, R_IBATOCP, pval);
	case POWER_SUPPLY_PROP_TEMP:
		return rt9756_get_adc(data, ADC_TDIE, pval);
	case POWER_SUPPLY_PROP_USB_TYPE:
		*pval = atomic_read(&data->usb_type);
		return 0;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = rt9756_model[data->model];
		return 0;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = rt9756_manufacturer;
		return 0;
	default:
		return -ENODATA;
	}
}

static int rt9756_psy_set_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	struct rt9756_data *data = power_supply_get_drvdata(psy);
	int intval = val->intval;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		memset(&data->chg_evt, 0, sizeof(data->chg_evt));
		return regmap_field_write(data->rm_fields[F_CHG_EN], !!intval);
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return rt9756_set_vbus_ovp(data, intval);
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return rt9756_set_value_field_range(data, F_IBUSOCP_EN, F_IBUSOCP, R_IBUSOCP,
						    intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		return rt9756_set_value_field_range(data, F_VBATOVP_EN, F_VBATOVP, R_VBATOVP,
						    intval);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		return rt9756_set_value_field_range(data, F_IBATOCP_EN, F_IBATOCP, R_IBATOCP,
						    intval);
	case POWER_SUPPLY_PROP_USB_TYPE:
		return regmap_field_write(data->rm_fields[F_BC12_EN], !!intval);
	default:
		return -EINVAL;
	}
}

static const enum power_supply_property rt9756_psy_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static int rt9756_bat_psy_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct rt9756_data *data = power_supply_get_drvdata(psy);
	int *pval = &val->intval;

	switch (psp) {
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		*pval = POWER_SUPPLY_TECHNOLOGY_LION;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return rt9756_get_adc(data, ADC_VBAT, pval);
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return rt9756_get_adc(data, ADC_IBAT, pval);
	default:
		return -ENODATA;
	}
}

static const enum power_supply_property rt9756_bat_psy_properties[] = {
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

static int rt9756_psy_property_is_writeable(struct power_supply *psy,
					    enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_USB_TYPE:
		return 1;
	default:
		return 0;
	}
}

static const unsigned int rt9756_wdt_millisecond[] = {
	500, 1000, 5000, 30000, 40000, 80000, 128000, 255000
};

static ssize_t watchdog_timer_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = to_power_supply(dev);
	struct rt9756_data *data = power_supply_get_drvdata(psy);
	unsigned int wdt_tmr_now = 0, wdt_sel, wdt_dis;
	int ret;

	ret = regmap_field_read(data->rm_fields[F_WDT_DIS], &wdt_dis);
	if (ret)
		return ret;

	if (!wdt_dis) {
		ret = regmap_field_read(data->rm_fields[F_WDT_TMR], &wdt_sel);
		if (ret)
			return ret;

		wdt_tmr_now = rt9756_wdt_millisecond[wdt_sel];
	}

	return sysfs_emit(buf, "%d\n", wdt_tmr_now);
}

static ssize_t watchdog_timer_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct power_supply *psy = to_power_supply(dev);
	struct rt9756_data *data = power_supply_get_drvdata(psy);
	unsigned int wdt_set, wdt_sel;
	int ret;

	ret = kstrtouint(buf, 10, &wdt_set);
	if (ret)
		return ret;

	ret = regmap_field_write(data->rm_fields[F_WDT_DIS], 1);
	if (ret)
		return ret;

	wdt_sel = find_closest(wdt_set, rt9756_wdt_millisecond,
			       ARRAY_SIZE(rt9756_wdt_millisecond));

	ret = regmap_field_write(data->rm_fields[F_WDT_TMR], wdt_sel);
	if (ret)
		return ret;

	if (wdt_set) {
		ret = regmap_field_write(data->rm_fields[F_WDT_DIS], 0);
		if (ret)
			return ret;
	}

	return count;
}

static const char * const rt9756_opmode_str[] = { "bypass", "div2" };

static ssize_t operation_mode_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = to_power_supply(dev);
	struct rt9756_data *data = power_supply_get_drvdata(psy);
	unsigned int opmode;
	int ret;

	ret = regmap_field_read(data->rm_fields[F_OP_MODE], &opmode);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%s\n", rt9756_opmode_str[opmode]);
}

static ssize_t operation_mode_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct power_supply *psy = to_power_supply(dev);
	struct rt9756_data *data = power_supply_get_drvdata(psy);
	int index, ret;

	index = sysfs_match_string(rt9756_opmode_str, buf);
	if (index < 0)
		return index;

	ret = regmap_field_write(data->rm_fields[F_OP_MODE], index);

	return ret ?: count;
}

static DEVICE_ATTR_RW(watchdog_timer);
static DEVICE_ATTR_RW(operation_mode);

static struct attribute *rt9756_sysfs_attrs[] = {
	&dev_attr_watchdog_timer.attr,
	&dev_attr_operation_mode.attr,
	NULL
};
ATTRIBUTE_GROUPS(rt9756_sysfs);

static int rt9756_register_psy(struct rt9756_data *data)
{
	struct power_supply_desc *desc = &data->psy_desc;
	struct power_supply_desc *bat_desc = &data->bat_psy_desc;
	struct power_supply_config cfg = {}, bat_cfg = {};
	struct device *dev = data->dev;
	char *psy_name, *bat_psy_name, **supplied_to;

	bat_cfg.drv_data = data;
	bat_cfg.fwnode = dev_fwnode(dev);

	bat_psy_name = devm_kasprintf(dev, GFP_KERNEL, "rt9756-%s-battery", dev_name(dev));
	if (!bat_psy_name)
		return -ENOMEM;

	bat_desc->name = bat_psy_name;
	bat_desc->type = POWER_SUPPLY_TYPE_BATTERY;
	bat_desc->properties = rt9756_bat_psy_properties;
	bat_desc->num_properties = ARRAY_SIZE(rt9756_bat_psy_properties);
	bat_desc->get_property = rt9756_bat_psy_get_property;

	data->bat_psy = devm_power_supply_register(dev, bat_desc, &bat_cfg);
	if (IS_ERR(data->bat_psy))
		return dev_err_probe(dev, PTR_ERR(data->bat_psy), "Failed to register battery\n");

	supplied_to = devm_kzalloc(dev, sizeof(*supplied_to), GFP_KERNEL);
	if (!supplied_to)
		return -ENOMEM;

	/* Link charger psy to battery psy */
	supplied_to[0] = bat_psy_name;

	cfg.drv_data = data;
	cfg.fwnode = dev_fwnode(dev);
	cfg.attr_grp = rt9756_sysfs_groups;
	cfg.supplied_to = supplied_to;
	cfg.num_supplicants = 1;

	psy_name = devm_kasprintf(dev, GFP_KERNEL, "rt9756-%s", dev_name(dev));
	if (!psy_name)
		return -ENOMEM;

	desc->name = psy_name;
	desc->type = POWER_SUPPLY_TYPE_USB;
	desc->usb_types = BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN) | BIT(POWER_SUPPLY_USB_TYPE_SDP) |
			  BIT(POWER_SUPPLY_USB_TYPE_DCP) | BIT(POWER_SUPPLY_USB_TYPE_CDP);
	desc->properties = rt9756_psy_properties;
	desc->num_properties = ARRAY_SIZE(rt9756_psy_properties);
	desc->property_is_writeable = rt9756_psy_property_is_writeable;
	desc->get_property = rt9756_psy_get_property;
	desc->set_property = rt9756_psy_set_property;

	data->psy = devm_power_supply_register(dev, desc, &cfg);

	return PTR_ERR_OR_ZERO(data->psy);
}

static int rt9756_get_usb_type(struct rt9756_data *data)
{
	unsigned int type;
	int report_type, ret;

	ret = regmap_field_read(data->rm_fields[F_USB_STATE], &type);
	if (ret)
		return ret;

	switch (type) {
	case USB_SDP:
	case USB_NSTD:
		report_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case USB_DCP:
		report_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	case USB_CDP:
		report_type = POWER_SUPPLY_USB_TYPE_CDP;
		break;
	case USB_NO_VBUS:
	default:
		report_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		break;
	}

	atomic_set(&data->usb_type, report_type);
	return 0;
}

static irqreturn_t rt9756_irq_handler(int irq, void *devid)
{
	struct rt9756_data *data = devid;
	struct regmap *regmap = data->regmap;
	struct charger_event *evt = &data->chg_evt;
	unsigned int bc12_flag = 0;
	int ret;

	ret = regmap_read(regmap, RT9756_REG_INTFLAG1, &evt->flag1);
	if (ret)
		return IRQ_NONE;

	ret = regmap_read(regmap, RT9756_REG_INTFLAG2, &evt->flag2);
	if (ret)
		return IRQ_NONE;

	ret = regmap_read(regmap, RT9756_REG_INTFLAG3, &evt->flag3);
	if (ret)
		return IRQ_NONE;

	if (data->model != MODEL_RT9770) {
		ret = regmap_read(regmap, RT9756_REG_INTFLAG4, &evt->flag4);
		if (ret)
			return IRQ_NONE;

		ret = regmap_read(regmap, RT9756_REG_BC12FLAG, &bc12_flag);
		if (ret)
			return IRQ_NONE;
	}

	dev_dbg(data->dev, "events: 0x%02x,%02x,%02x,%02x,%02x\n", evt->flag1, evt->flag2,
		evt->flag3, evt->flag4, bc12_flag);

	if (evt->flag2 & RT9756_EVT_VAC_INSERT) {
		ret = regmap_field_write(data->rm_fields[F_BC12_EN], 1);
		if (ret)
			return IRQ_NONE;
	}

	if (evt->flag3 & RT9756_EVT_VAC_UVLO)
		atomic_set(&data->usb_type, POWER_SUPPLY_USB_TYPE_UNKNOWN);

	if (bc12_flag & RT9756_EVT_BC12_DONE) {
		ret = rt9756_get_usb_type(data);
		if (ret)
			return IRQ_NONE;
	}

	power_supply_changed(data->psy);

	return IRQ_HANDLED;
}

static int rt9756_config_batsense_resistor(struct rt9756_data *data)
{
	unsigned int shunt_resistor_uohms = 2000, rsense_sel;

	device_property_read_u32(data->dev, "shunt-resistor-micro-ohms", &shunt_resistor_uohms);

	if (!shunt_resistor_uohms || shunt_resistor_uohms > 5000)
		return -EINVAL;

	data->real_resistor = shunt_resistor_uohms;

	/* Always choose the larger or equal one to prevent false ocp alarm */
	if (shunt_resistor_uohms <= 1000) {
		rsense_sel = 0;
		data->rg_resistor = 1000;
	} else if (shunt_resistor_uohms <= 2000) {
		rsense_sel = 1;
		data->rg_resistor = 2000;
	} else {
		rsense_sel = 2;
		data->rg_resistor = 5000;
	}

	return regmap_field_write(data->rm_fields[F_IBAT_RSEN], rsense_sel);
}

static const struct reg_sequence rt9756_init_regs[] = {
	REG_SEQ(0x00, 0x80, 1000), /* REG_RESET */
	REG_SEQ0(0x04, 0x13), /* VACOVP/OVPGATE 12V */
	REG_SEQ0(0x00, 0x28), /* WDT_DIS = 1 */
	REG_SEQ0(0x0c, 0x02), /* MASK FLAG1 */
	REG_SEQ0(0x0e, 0x06), /* MASK FLAG2 */
	REG_SEQ0(0x10, 0xca), /* MASK FLAG3 */
	REG_SEQ0(0x44, 0xa0), /* BC12_EN */
	REG_SEQ0(0x47, 0x07), /* MASK BC12FLAG */
	REG_SEQ0(0x4a, 0xfe), /* MASK FLAG4 */
	REG_SEQ0(0x5c, 0x40), /* MASK CON_SWITCHING */
	REG_SEQ0(0x63, 0x01), /* MASK VDDA_UVLO */
};

static const struct reg_sequence rt9770_init_regs[] = {
	REG_SEQ(0x00, 0x80, 1000), /* REG_RESET */
	REG_SEQ0(0x04, 0x13), /* VACOVP/OVPGATE 12V */
	REG_SEQ0(0x00, 0x28), /* WDT_DIS = 1 */
	REG_SEQ0(0x0c, 0x02), /* MASK FLAG1 */
	REG_SEQ0(0x0e, 0x06), /* MASK FLAG2 */
	REG_SEQ0(0x10, 0xca), /* MASK FLAG3 */
	REG_SEQ0(0x5c, 0x40), /* MASK CON_SWITCHING */
	REG_SEQ0(0x63, 0x01), /* MASK VDDA_UVLO */
};

static const struct regmap_config rt9756_regmap_config = {
	.name = "rt9756",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x1ff,
};

static const struct regmap_config rt9770_regmap_config = {
	.name = "rt9770",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
};

static int rt9756_check_device_model(struct rt9756_data *data)
{
	struct device *dev = data->dev;
	unsigned int revid;
	int ret;

	ret = regmap_field_read(data->rm_fields[F_REVISION], &revid);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read revid\n");

	if (revid == RT9757_REVID || revid == RT9757A_REVID)
		data->model = MODEL_RT9757;
	else if (revid == RT9756_REVID || revid == RT9756A_REVID)
		data->model = MODEL_RT9756;
	else
		return dev_err_probe(dev, -EINVAL, "Unknown revision %d\n", revid);

	return 0;
}

static int rt9770_check_device_model(struct rt9756_data *data)
{
	data->model = MODEL_RT9770;
	return 0;
}

static int rt9756_probe(struct i2c_client *i2c)
{
	const struct rt975x_dev_data *dev_data;
	struct device *dev = &i2c->dev;
	struct rt9756_data *data;
	struct regmap *regmap;
	unsigned int devid;
	int ret;

	dev_data = device_get_match_data(dev);
	if (!dev_data)
		return dev_err_probe(dev, -EINVAL, "No device data found\n");

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	mutex_init(&data->adc_lock);
	atomic_set(&data->usb_type, POWER_SUPPLY_USB_TYPE_UNKNOWN);
	i2c_set_clientdata(i2c, data);

	regmap = devm_regmap_init_i2c(i2c, dev_data->regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to init regmap\n");

	data->regmap = regmap;

	ret = devm_regmap_field_bulk_alloc(dev, regmap, data->rm_fields, dev_data->reg_fields,
					   F_MAX_FIELD);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to alloc regmap fields\n");

	/* Richtek Device ID check */
	ret = regmap_field_read(data->rm_fields[F_DEV_ID], &devid);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read devid\n");

	if (devid != RICHTEK_DEVID)
		return dev_err_probe(dev, -ENODEV, "Incorrect VID 0x%02x\n", devid);

	/* Get specific model */
	ret = dev_data->check_device_model(data);
	if (ret)
		return ret;

	ret = regmap_register_patch(regmap, dev_data->init_regs, dev_data->num_init_regs);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init registers\n");

	ret = rt9756_config_batsense_resistor(data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to config batsense resistor\n");

	ret = rt9756_register_psy(data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init power supply\n");

	return devm_request_threaded_irq(dev, i2c->irq, NULL, rt9756_irq_handler, IRQF_ONESHOT,
					 dev_name(dev), data);
}

static void rt9756_shutdown(struct i2c_client *i2c)
{
	struct rt9756_data *data = i2c_get_clientdata(i2c);

	regmap_field_write(data->rm_fields[F_REG_RST], 1);
}

static const struct rt975x_dev_data rt9756_dev_data = {
	.regmap_config		= &rt9756_regmap_config,
	.reg_fields		= rt9756_chg_fields,
	.init_regs		= rt9756_init_regs,
	.num_init_regs		= ARRAY_SIZE(rt9756_init_regs),
	.check_device_model	= rt9756_check_device_model,
};

static const struct rt975x_dev_data rt9770_dev_data = {
	.regmap_config		= &rt9770_regmap_config,
	.reg_fields		= rt9770_chg_fields,
	.init_regs		= rt9770_init_regs,
	.num_init_regs		= ARRAY_SIZE(rt9770_init_regs),
	.check_device_model	= rt9770_check_device_model,
};

static const struct of_device_id rt9756_device_match_table[] = {
	{ .compatible = "richtek,rt9756", .data = &rt9756_dev_data },
	{ .compatible = "richtek,rt9770", .data = &rt9770_dev_data },
	{}
};
MODULE_DEVICE_TABLE(of, rt9756_device_match_table);

static struct i2c_driver rt9756_charger_driver = {
	.driver = {
		.name = "rt9756",
		.of_match_table = rt9756_device_match_table,
	},
	.probe = rt9756_probe,
	.shutdown = rt9756_shutdown,
};
module_i2c_driver(rt9756_charger_driver);

MODULE_DESCRIPTION("Richtek RT9756 charger driver");
MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_LICENSE("GPL");
