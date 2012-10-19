/*
 * TI LP8788 MFD - battery charger driver
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/err.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/mfd/lp8788.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

/* register address */
#define LP8788_CHG_STATUS		0x07
#define LP8788_CHG_IDCIN		0x13
#define LP8788_CHG_IBATT		0x14
#define LP8788_CHG_VTERM		0x15
#define LP8788_CHG_EOC			0x16

/* mask/shift bits */
#define LP8788_CHG_INPUT_STATE_M	0x03	/* Addr 07h */
#define LP8788_CHG_STATE_M		0x3C
#define LP8788_CHG_STATE_S		2
#define LP8788_NO_BATT_M		BIT(6)
#define LP8788_BAD_BATT_M		BIT(7)
#define LP8788_CHG_IBATT_M		0x1F	/* Addr 14h */
#define LP8788_CHG_VTERM_M		0x0F	/* Addr 15h */
#define LP8788_CHG_EOC_LEVEL_M		0x30	/* Addr 16h */
#define LP8788_CHG_EOC_LEVEL_S		4
#define LP8788_CHG_EOC_TIME_M		0x0E
#define LP8788_CHG_EOC_TIME_S		1
#define LP8788_CHG_EOC_MODE_M		BIT(0)

#define LP8788_CHARGER_NAME		"charger"
#define LP8788_BATTERY_NAME		"main_batt"

#define LP8788_CHG_START		0x11
#define LP8788_CHG_END			0x1C

#define LP8788_BUF_SIZE			40
#define LP8788_ISEL_MAX			23
#define LP8788_ISEL_STEP		50
#define LP8788_VTERM_MIN		4100
#define LP8788_VTERM_STEP		25
#define LP8788_MAX_BATT_CAPACITY	100
#define LP8788_MAX_CHG_IRQS		11

enum lp8788_charging_state {
	LP8788_OFF,
	LP8788_WARM_UP,
	LP8788_LOW_INPUT = 0x3,
	LP8788_PRECHARGE,
	LP8788_CC,
	LP8788_CV,
	LP8788_MAINTENANCE,
	LP8788_BATTERY_FAULT,
	LP8788_SYSTEM_SUPPORT = 0xC,
	LP8788_HIGH_CURRENT = 0xF,
	LP8788_MAX_CHG_STATE,
};

enum lp8788_charger_adc_sel {
	LP8788_VBATT,
	LP8788_BATT_TEMP,
	LP8788_NUM_CHG_ADC,
};

enum lp8788_charger_input_state {
	LP8788_SYSTEM_SUPPLY = 1,
	LP8788_FULL_FUNCTION,
};

/*
 * struct lp8788_chg_irq
 * @which        : lp8788 interrupt id
 * @virq         : Linux IRQ number from irq_domain
 */
struct lp8788_chg_irq {
	enum lp8788_int_id which;
	int virq;
};

/*
 * struct lp8788_charger
 * @lp           : used for accessing the registers of mfd lp8788 device
 * @charger      : power supply driver for the battery charger
 * @battery      : power supply driver for the battery
 * @charger_work : work queue for charger input interrupts
 * @chan         : iio channels for getting adc values
 *                 eg) battery voltage, capacity and temperature
 * @irqs         : charger dedicated interrupts
 * @num_irqs     : total numbers of charger interrupts
 * @pdata        : charger platform specific data
 */
struct lp8788_charger {
	struct lp8788 *lp;
	struct power_supply charger;
	struct power_supply battery;
	struct work_struct charger_work;
	struct iio_channel *chan[LP8788_NUM_CHG_ADC];
	struct lp8788_chg_irq irqs[LP8788_MAX_CHG_IRQS];
	int num_irqs;
	struct lp8788_charger_platform_data *pdata;
};

static char *battery_supplied_to[] = {
	LP8788_BATTERY_NAME,
};

static enum power_supply_property lp8788_charger_prop[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static enum power_supply_property lp8788_battery_prop[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_TEMP,
};

static bool lp8788_is_charger_detected(struct lp8788_charger *pchg)
{
	u8 data;

	lp8788_read_byte(pchg->lp, LP8788_CHG_STATUS, &data);
	data &= LP8788_CHG_INPUT_STATE_M;

	return data == LP8788_SYSTEM_SUPPLY || data == LP8788_FULL_FUNCTION;
}

static int lp8788_charger_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct lp8788_charger *pchg = dev_get_drvdata(psy->dev->parent);
	u8 read;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = lp8788_is_charger_detected(pchg);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		lp8788_read_byte(pchg->lp, LP8788_CHG_IDCIN, &read);
		val->intval = LP8788_ISEL_STEP *
				(min_t(int, read, LP8788_ISEL_MAX) + 1);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int lp8788_get_battery_status(struct lp8788_charger *pchg,
				union power_supply_propval *val)
{
	enum lp8788_charging_state state;
	u8 data;
	int ret;

	ret = lp8788_read_byte(pchg->lp, LP8788_CHG_STATUS, &data);
	if (ret)
		return ret;

	state = (data & LP8788_CHG_STATE_M) >> LP8788_CHG_STATE_S;
	switch (state) {
	case LP8788_OFF:
		val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case LP8788_PRECHARGE:
	case LP8788_CC:
	case LP8788_CV:
	case LP8788_HIGH_CURRENT:
		val->intval = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case LP8788_MAINTENANCE:
		val->intval = POWER_SUPPLY_STATUS_FULL;
		break;
	default:
		val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	}

	return 0;
}

static int lp8788_get_battery_health(struct lp8788_charger *pchg,
				union power_supply_propval *val)
{
	u8 data;
	int ret;

	ret = lp8788_read_byte(pchg->lp, LP8788_CHG_STATUS, &data);
	if (ret)
		return ret;

	if (data & LP8788_NO_BATT_M)
		val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
	else if (data & LP8788_BAD_BATT_M)
		val->intval = POWER_SUPPLY_HEALTH_DEAD;
	else
		val->intval = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int lp8788_get_battery_present(struct lp8788_charger *pchg,
				union power_supply_propval *val)
{
	u8 data;
	int ret;

	ret = lp8788_read_byte(pchg->lp, LP8788_CHG_STATUS, &data);
	if (ret)
		return ret;

	val->intval = !(data & LP8788_NO_BATT_M);
	return 0;
}

static int lp8788_get_vbatt_adc(struct lp8788_charger *pchg, int *result)
{
	struct iio_channel *channel = pchg->chan[LP8788_VBATT];

	if (!channel)
		return -EINVAL;

	return iio_read_channel_processed(channel, result);
}

static int lp8788_get_battery_voltage(struct lp8788_charger *pchg,
				union power_supply_propval *val)
{
	return lp8788_get_vbatt_adc(pchg, &val->intval);
}

static int lp8788_get_battery_capacity(struct lp8788_charger *pchg,
				union power_supply_propval *val)
{
	struct lp8788 *lp = pchg->lp;
	struct lp8788_charger_platform_data *pdata = pchg->pdata;
	unsigned int max_vbatt;
	int vbatt;
	enum lp8788_charging_state state;
	u8 data;
	int ret;

	if (!pdata)
		return -EINVAL;

	max_vbatt = pdata->max_vbatt_mv;
	if (max_vbatt == 0)
		return -EINVAL;

	ret = lp8788_read_byte(lp, LP8788_CHG_STATUS, &data);
	if (ret)
		return ret;

	state = (data & LP8788_CHG_STATE_M) >> LP8788_CHG_STATE_S;

	if (state == LP8788_MAINTENANCE) {
		val->intval = LP8788_MAX_BATT_CAPACITY;
	} else {
		ret = lp8788_get_vbatt_adc(pchg, &vbatt);
		if (ret)
			return ret;

		val->intval = (vbatt * LP8788_MAX_BATT_CAPACITY) / max_vbatt;
		val->intval = min(val->intval, LP8788_MAX_BATT_CAPACITY);
	}

	return 0;
}

static int lp8788_get_battery_temperature(struct lp8788_charger *pchg,
				union power_supply_propval *val)
{
	struct iio_channel *channel = pchg->chan[LP8788_BATT_TEMP];
	int result;
	int ret;

	if (!channel)
		return -EINVAL;

	ret = iio_read_channel_processed(channel, &result);
	if (ret < 0)
		return -EINVAL;

	/* unit: 0.1 'C */
	val->intval = result * 10;

	return 0;
}

static int lp8788_get_battery_charging_current(struct lp8788_charger *pchg,
				union power_supply_propval *val)
{
	u8 read;

	lp8788_read_byte(pchg->lp, LP8788_CHG_IBATT, &read);
	read &= LP8788_CHG_IBATT_M;
	val->intval = LP8788_ISEL_STEP *
			(min_t(int, read, LP8788_ISEL_MAX) + 1);

	return 0;
}

static int lp8788_get_charging_termination_voltage(struct lp8788_charger *pchg,
				union power_supply_propval *val)
{
	u8 read;

	lp8788_read_byte(pchg->lp, LP8788_CHG_VTERM, &read);
	read &= LP8788_CHG_VTERM_M;
	val->intval = LP8788_VTERM_MIN + LP8788_VTERM_STEP * read;

	return 0;
}

static int lp8788_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct lp8788_charger *pchg = dev_get_drvdata(psy->dev->parent);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		return lp8788_get_battery_status(pchg, val);
	case POWER_SUPPLY_PROP_HEALTH:
		return lp8788_get_battery_health(pchg, val);
	case POWER_SUPPLY_PROP_PRESENT:
		return lp8788_get_battery_present(pchg, val);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return lp8788_get_battery_voltage(pchg, val);
	case POWER_SUPPLY_PROP_CAPACITY:
		return lp8788_get_battery_capacity(pchg, val);
	case POWER_SUPPLY_PROP_TEMP:
		return lp8788_get_battery_temperature(pchg, val);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		return lp8788_get_battery_charging_current(pchg, val);
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		return lp8788_get_charging_termination_voltage(pchg, val);
	default:
		return -EINVAL;
	}
}

static inline bool lp8788_is_valid_charger_register(u8 addr)
{
	return addr >= LP8788_CHG_START && addr <= LP8788_CHG_END;
}

static int lp8788_update_charger_params(struct lp8788_charger *pchg)
{
	struct lp8788 *lp = pchg->lp;
	struct lp8788_charger_platform_data *pdata = pchg->pdata;
	struct lp8788_chg_param *param;
	int i;
	int ret;

	if (!pdata || !pdata->chg_params) {
		dev_info(lp->dev, "skip updating charger parameters\n");
		return 0;
	}

	/* settting charging parameters */
	for (i = 0; i < pdata->num_chg_params; i++) {
		param = pdata->chg_params + i;

		if (!param)
			continue;

		if (lp8788_is_valid_charger_register(param->addr)) {
			ret = lp8788_write_byte(lp, param->addr, param->val);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int lp8788_psy_register(struct platform_device *pdev,
				struct lp8788_charger *pchg)
{
	pchg->charger.name = LP8788_CHARGER_NAME;
	pchg->charger.type = POWER_SUPPLY_TYPE_MAINS;
	pchg->charger.properties = lp8788_charger_prop;
	pchg->charger.num_properties = ARRAY_SIZE(lp8788_charger_prop);
	pchg->charger.get_property = lp8788_charger_get_property;
	pchg->charger.supplied_to = battery_supplied_to;
	pchg->charger.num_supplicants = ARRAY_SIZE(battery_supplied_to);

	if (power_supply_register(&pdev->dev, &pchg->charger))
		return -EPERM;

	pchg->battery.name = LP8788_BATTERY_NAME;
	pchg->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	pchg->battery.properties = lp8788_battery_prop;
	pchg->battery.num_properties = ARRAY_SIZE(lp8788_battery_prop);
	pchg->battery.get_property = lp8788_battery_get_property;

	if (power_supply_register(&pdev->dev, &pchg->battery))
		return -EPERM;

	return 0;
}

static void lp8788_psy_unregister(struct lp8788_charger *pchg)
{
	power_supply_unregister(&pchg->battery);
	power_supply_unregister(&pchg->charger);
}

static void lp8788_charger_event(struct work_struct *work)
{
	struct lp8788_charger *pchg =
		container_of(work, struct lp8788_charger, charger_work);
	struct lp8788_charger_platform_data *pdata = pchg->pdata;
	enum lp8788_charger_event event = lp8788_is_charger_detected(pchg);

	pdata->charger_event(pchg->lp, event);
}

static bool lp8788_find_irq_id(struct lp8788_charger *pchg, int virq, int *id)
{
	bool found;
	int i;

	for (i = 0; i < pchg->num_irqs; i++) {
		if (pchg->irqs[i].virq == virq) {
			*id = pchg->irqs[i].which;
			found = true;
			break;
		}
	}

	return found;
}

static irqreturn_t lp8788_charger_irq_thread(int virq, void *ptr)
{
	struct lp8788_charger *pchg = ptr;
	struct lp8788_charger_platform_data *pdata = pchg->pdata;
	int id = -1;

	if (!lp8788_find_irq_id(pchg, virq, &id))
		return IRQ_NONE;

	switch (id) {
	case LP8788_INT_CHG_INPUT_STATE:
	case LP8788_INT_CHG_STATE:
	case LP8788_INT_EOC:
	case LP8788_INT_BATT_LOW:
	case LP8788_INT_NO_BATT:
		power_supply_changed(&pchg->charger);
		power_supply_changed(&pchg->battery);
		break;
	default:
		break;
	}

	/* report charger dectection event if used */
	if (!pdata)
		goto irq_handled;

	if (pdata->charger_event && id == LP8788_INT_CHG_INPUT_STATE)
		schedule_work(&pchg->charger_work);

irq_handled:
	return IRQ_HANDLED;
}

static int lp8788_set_irqs(struct platform_device *pdev,
			struct lp8788_charger *pchg, const char *name)
{
	struct resource *r;
	struct irq_domain *irqdm = pchg->lp->irqdm;
	int irq_start;
	int irq_end;
	int virq;
	int nr_irq;
	int i;
	int ret;

	/* no error even if no irq resource */
	r = platform_get_resource_byname(pdev, IORESOURCE_IRQ, name);
	if (!r)
		return 0;

	irq_start = r->start;
	irq_end = r->end;

	for (i = irq_start; i <= irq_end; i++) {
		nr_irq = pchg->num_irqs;

		virq = irq_create_mapping(irqdm, i);
		pchg->irqs[nr_irq].virq = virq;
		pchg->irqs[nr_irq].which = i;
		pchg->num_irqs++;

		ret = request_threaded_irq(virq, NULL,
					lp8788_charger_irq_thread,
					0, name, pchg);
		if (ret)
			break;
	}

	if (i <= irq_end)
		goto err_free_irq;

	return 0;

err_free_irq:
	for (i = 0; i < pchg->num_irqs; i++)
		free_irq(pchg->irqs[i].virq, pchg);
	return ret;
}

static int lp8788_irq_register(struct platform_device *pdev,
				struct lp8788_charger *pchg)
{
	struct lp8788 *lp = pchg->lp;
	const char *name[] = {
		LP8788_CHG_IRQ, LP8788_PRSW_IRQ, LP8788_BATT_IRQ
	};
	int i;
	int ret;

	INIT_WORK(&pchg->charger_work, lp8788_charger_event);
	pchg->num_irqs = 0;

	for (i = 0; i < ARRAY_SIZE(name); i++) {
		ret = lp8788_set_irqs(pdev, pchg, name[i]);
		if (ret) {
			dev_warn(lp->dev, "irq setup failed: %s\n", name[i]);
			return ret;
		}
	}

	if (pchg->num_irqs > LP8788_MAX_CHG_IRQS) {
		dev_err(lp->dev, "invalid total number of irqs: %d\n",
			pchg->num_irqs);
		return -EINVAL;
	}


	return 0;
}

static void lp8788_irq_unregister(struct platform_device *pdev,
				  struct lp8788_charger *pchg)
{
	int i;
	int irq;

	for (i = 0; i < pchg->num_irqs; i++) {
		irq = pchg->irqs[i].virq;
		if (!irq)
			continue;

		free_irq(irq, pchg);
	}
}

static void lp8788_setup_adc_channel(const char *consumer_name,
				struct lp8788_charger *pchg)
{
	struct lp8788_charger_platform_data *pdata = pchg->pdata;
	struct device *dev = pchg->lp->dev;
	struct iio_channel *chan;
	enum lp8788_adc_id id;
	const char *chan_name[LPADC_MAX] = {
		[LPADC_VBATT_5P5] = "vbatt-5p5",
		[LPADC_VBATT_6P0] = "vbatt-6p0",
		[LPADC_VBATT_5P0] = "vbatt-5p0",
		[LPADC_ADC1]      = "adc1",
		[LPADC_ADC2]      = "adc2",
		[LPADC_ADC3]      = "adc3",
		[LPADC_ADC4]      = "adc4",
	};

	if (!pdata)
		return;

	id = pdata->vbatt_adc;
	switch (id) {
	case LPADC_VBATT_5P5:
	case LPADC_VBATT_6P0:
	case LPADC_VBATT_5P0:
		chan = iio_channel_get(consumer_name, chan_name[id]);
		pchg->chan[LP8788_VBATT] = IS_ERR(chan) ? NULL : chan;
		break;
	default:
		dev_err(dev, "invalid ADC id for VBATT: %d\n", id);
		pchg->chan[LP8788_VBATT] = NULL;
		break;
	}

	id = pdata->batt_temp_adc;
	switch (id) {
	case LPADC_ADC1:
	case LPADC_ADC2:
	case LPADC_ADC3:
	case LPADC_ADC4:
		chan = iio_channel_get(consumer_name, chan_name[id]);
		pchg->chan[LP8788_BATT_TEMP] = IS_ERR(chan) ? NULL : chan;
		break;
	default:
		dev_err(dev, "invalid ADC id for BATT_TEMP : %d\n", id);
		pchg->chan[LP8788_BATT_TEMP] = NULL;
		break;
	}
}

static void lp8788_release_adc_channel(struct lp8788_charger *pchg)
{
	int i;

	for (i = 0; i < LP8788_NUM_CHG_ADC; i++) {
		if (!pchg->chan[i])
			continue;

		iio_channel_release(pchg->chan[i]);
		pchg->chan[i] = NULL;
	}
}

static ssize_t lp8788_show_charger_status(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lp8788_charger *pchg = dev_get_drvdata(dev);
	enum lp8788_charging_state state;
	char *desc[LP8788_MAX_CHG_STATE] = {
		[LP8788_OFF] = "CHARGER OFF",
		[LP8788_WARM_UP] = "WARM UP",
		[LP8788_LOW_INPUT] = "LOW INPUT STATE",
		[LP8788_PRECHARGE] = "CHARGING - PRECHARGE",
		[LP8788_CC] = "CHARGING - CC",
		[LP8788_CV] = "CHARGING - CV",
		[LP8788_MAINTENANCE] = "NO CHARGING - MAINTENANCE",
		[LP8788_BATTERY_FAULT] = "BATTERY FAULT",
		[LP8788_SYSTEM_SUPPORT] = "SYSTEM SUPPORT",
		[LP8788_HIGH_CURRENT] = "HIGH CURRENT",
	};
	u8 data;

	lp8788_read_byte(pchg->lp, LP8788_CHG_STATUS, &data);
	state = (data & LP8788_CHG_STATE_M) >> LP8788_CHG_STATE_S;

	return scnprintf(buf, LP8788_BUF_SIZE, "%s\n", desc[state]);
}

static ssize_t lp8788_show_eoc_time(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lp8788_charger *pchg = dev_get_drvdata(dev);
	char *stime[] = { "400ms", "5min", "10min", "15min",
			"20min", "25min", "30min" "No timeout" };
	u8 val;

	lp8788_read_byte(pchg->lp, LP8788_CHG_EOC, &val);
	val = (val & LP8788_CHG_EOC_TIME_M) >> LP8788_CHG_EOC_TIME_S;

	return scnprintf(buf, LP8788_BUF_SIZE, "End Of Charge Time: %s\n",
			stime[val]);
}

static ssize_t lp8788_show_eoc_level(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lp8788_charger *pchg = dev_get_drvdata(dev);
	char *abs_level[] = { "25mA", "49mA", "75mA", "98mA" };
	char *relative_level[] = { "5%", "10%", "15%", "20%" };
	char *level;
	u8 val;
	u8 mode;

	lp8788_read_byte(pchg->lp, LP8788_CHG_EOC, &val);

	mode = val & LP8788_CHG_EOC_MODE_M;
	val = (val & LP8788_CHG_EOC_LEVEL_M) >> LP8788_CHG_EOC_LEVEL_S;
	level = mode ? abs_level[val] : relative_level[val];

	return scnprintf(buf, LP8788_BUF_SIZE, "End Of Charge Level: %s\n",
			level);
}

static DEVICE_ATTR(charger_status, S_IRUSR, lp8788_show_charger_status, NULL);
static DEVICE_ATTR(eoc_time, S_IRUSR, lp8788_show_eoc_time, NULL);
static DEVICE_ATTR(eoc_level, S_IRUSR, lp8788_show_eoc_level, NULL);

static struct attribute *lp8788_charger_attr[] = {
	&dev_attr_charger_status.attr,
	&dev_attr_eoc_time.attr,
	&dev_attr_eoc_level.attr,
	NULL,
};

static const struct attribute_group lp8788_attr_group = {
	.attrs = lp8788_charger_attr,
};

static __devinit int lp8788_charger_probe(struct platform_device *pdev)
{
	struct lp8788 *lp = dev_get_drvdata(pdev->dev.parent);
	struct lp8788_charger *pchg;
	int ret;

	pchg = devm_kzalloc(lp->dev, sizeof(struct lp8788_charger), GFP_KERNEL);
	if (!pchg)
		return -ENOMEM;

	pchg->lp = lp;
	pchg->pdata = lp->pdata ? lp->pdata->chg_pdata : NULL;
	platform_set_drvdata(pdev, pchg);

	ret = lp8788_update_charger_params(pchg);
	if (ret)
		return ret;

	lp8788_setup_adc_channel(pdev->name, pchg);

	ret = lp8788_psy_register(pdev, pchg);
	if (ret)
		return ret;

	ret = sysfs_create_group(&pdev->dev.kobj, &lp8788_attr_group);
	if (ret) {
		lp8788_psy_unregister(pchg);
		return ret;
	}

	ret = lp8788_irq_register(pdev, pchg);
	if (ret)
		dev_warn(lp->dev, "failed to register charger irq: %d\n", ret);

	return 0;
}

static int __devexit lp8788_charger_remove(struct platform_device *pdev)
{
	struct lp8788_charger *pchg = platform_get_drvdata(pdev);

	flush_work(&pchg->charger_work);
	lp8788_irq_unregister(pdev, pchg);
	sysfs_remove_group(&pdev->dev.kobj, &lp8788_attr_group);
	lp8788_psy_unregister(pchg);
	lp8788_release_adc_channel(pchg);

	return 0;
}

static struct platform_driver lp8788_charger_driver = {
	.probe = lp8788_charger_probe,
	.remove = __devexit_p(lp8788_charger_remove),
	.driver = {
		.name = LP8788_DEV_CHARGER,
		.owner = THIS_MODULE,
	},
};
module_platform_driver(lp8788_charger_driver);

MODULE_DESCRIPTION("TI LP8788 Charger Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lp8788-charger");
