/*
 * sabresd_battery.c - Maxim 8903 USB/Adapter Charger Driver
 *
 * Copyright (C) 2011 Samsung Electronics
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc.
 * Based on max8903_charger.c
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/power/sabresd_battery.h>
#include <linux/slab.h>

#define	BATTERY_UPDATE_INTERVAL	5 /*seconds*/
#define LOW_VOLT_THRESHOLD	2800000
#define HIGH_VOLT_THRESHOLD	4200000
#define ADC_SAMPLE_COUNT	6

struct max8903_data {
	struct max8903_pdata *pdata;
	struct device *dev;
	struct power_supply *psy;
	struct power_supply_desc psy_desc;
	struct power_supply *usb;
	struct power_supply_desc usb_desc;
	bool fault;
	bool usb_in;
	bool ta_in;
	bool chg_state;
	struct delayed_work work;
	unsigned int interval;
	unsigned short thermal_raw;
	int voltage_uV;
	int current_uA;
	int battery_status;
	int charger_online;
	int charger_voltage_uV;
	int real_capacity;
	int percent;
	int old_percent;
	int usb_charger_online;
	int first_delay_count;
	struct power_supply *bat;
	struct power_supply_desc bat_desc;
	struct power_supply	detect_usb;
};

typedef struct {
	u32 voltage;
	u32 percent;
} battery_capacity , *pbattery_capacity;

static int offset_discharger;
static int offset_charger;
static int offset_usb_charger;

static battery_capacity chargingTable[] = {
	{4050,	99},
	{4040,	98},
	{4020,	97},
	{4010,	96},
	{3990,	95},
	{3980,	94},
	{3970,	93},
	{3960,	92},
	{3950,	91},
	{3940,	90},
	{3930,	85},
	{3920,	81},
	{3910,	77},
	{3900,	73},
	{3890,	70},
	{3860,	65},
	{3830,	60},
	{3780,	55},
	{3760,	50},
	{3740,	45},
	{3720,	40},
	{3700,	35},
	{3680,	30},
	{3660,	25},
	{3640,	20},
	{3620,	17},
	{3600,	14},
	{3580,	13},
	{3560,	12},
	{3540,	11},
	{3520,	10},
	{3500,	9},
	{3480,	8},
	{3460,	7},
	{3440,	6},
	{3430,	5},
	{3420,	4},
	{3020,	0},
};

static battery_capacity dischargingTable[] = {
	{4050, 100},
	{4035,	99},
	{4020,	98},
	{4010,	97},
	{4000,	96},
	{3990,	96},
	{3980,	95},
	{3970,	92},
	{3960,	91},
	{3950,	90},
	{3940,	88},
	{3930,	86},
	{3920,	84},
	{3910,	82},
	{3900,	80},
	{3890,	74},
	{3860,	69},
	{3830,	64},
	{3780,	59},
	{3760,	54},
	{3740,	49},
	{3720,	44},
	{3700,	39},
	{3680,	34},
	{3660,	29},
	{3640,	24},
	{3620,	19},
	{3600,	14},
	{3580,	13},
	{3560,	12},
	{3540,	11},
	{3520,	10},
	{3500,	9},
	{3480,	8},
	{3460,	7},
	{3440,	6},
	{3430,	5},
	{3420,	4},
	{3020,	0},
};

u32 calibrate_battery_capability_percent(struct max8903_data *data)
{
	u8 i;
	pbattery_capacity pTable;
	u32 tableSize;

	if (data->battery_status  == POWER_SUPPLY_STATUS_DISCHARGING) {
		pTable = dischargingTable;
		tableSize = sizeof(dischargingTable)/
			sizeof(dischargingTable[0]);
	} else {
		pTable = chargingTable;
		tableSize = sizeof(chargingTable)/
			sizeof(chargingTable[0]);
	}
	for (i = 0; i < tableSize; i++) {
		if (data->voltage_uV >= pTable[i].voltage)
			return	pTable[i].percent;
	}

	return 0;
}

static enum power_supply_property max8903_charger_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property max8903_battery_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
};

extern u32 max11801_read_adc(void);

static void max8903_charger_update_status(struct max8903_data *data)
{
	if (data->ta_in) {
		data->charger_online = 1;
	} else if (data->usb_in) {
		data->usb_charger_online = 1;
	} else {
		data->charger_online = 0;
		data->usb_charger_online = 0;
	}

	if (!data->charger_online && !data->usb_charger_online) {
		data->battery_status = POWER_SUPPLY_STATUS_DISCHARGING;
	} else if (gpio_get_value(data->pdata->chg) == 0) {
		data->battery_status = POWER_SUPPLY_STATUS_CHARGING;
	} else if ((data->ta_in || data->usb_in) &&
		gpio_get_value(data->pdata->chg) > 0) {
		if (!data->pdata->feature_flag) {
			if (data->percent >= 99)
				data->battery_status = POWER_SUPPLY_STATUS_FULL;
			else
				data->battery_status =
				POWER_SUPPLY_STATUS_NOT_CHARGING;
		} else {
			data->battery_status = POWER_SUPPLY_STATUS_FULL;
		}
	}
}

u32 calibration_voltage(struct max8903_data *data)
{
	u32 voltage_data = 0;
	int i;
	int offset;

	if (!data->charger_online && !data->usb_charger_online)
		offset = offset_discharger;
	else if (data->usb_charger_online)
		offset = offset_usb_charger;
	else if (data->charger_online)
		offset = offset_charger;

	/* simple average */
	for (i = 0; i < ADC_SAMPLE_COUNT; i++)
		voltage_data += max11801_read_adc()-offset;
	voltage_data = voltage_data / ADC_SAMPLE_COUNT;
	dev_dbg(data->dev, "volt: %d\n", voltage_data);

	return voltage_data;
}

static void max8903_battery_update_status(struct max8903_data *data)
{
	if (!data->pdata->feature_flag) {
		data->voltage_uV = calibration_voltage(data);
		data->percent = calibrate_battery_capability_percent(data);
		if (data->percent != data->old_percent) {
			data->old_percent = data->percent;
			power_supply_changed(data->bat);
		}
		 /*
		  * because boot time gap between led framwork and charger
		  * framwork,when system boots with charger attatched,
		  * charger led framwork loses the first charger online event,
		  * add once extra power_supply_changed can fix this issure
		  */
		if (data->first_delay_count < 200) {
			data->first_delay_count = data->first_delay_count + 1;
			power_supply_changed(data->bat);
		}
	}
}

static int max8903_battery_get_property(struct power_supply *bat,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct max8903_data *di = bat->drv_data;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		if (gpio_get_value(di->pdata->chg) == 0) {
			di->battery_status = POWER_SUPPLY_STATUS_CHARGING;
		} else if ((di->ta_in || di->usb_in) &&
			gpio_get_value(di->pdata->chg) > 0) {
			if (!di->pdata->feature_flag) {
				if (di->percent >= 99)
					di->battery_status =
					POWER_SUPPLY_STATUS_FULL;
				else
					di->battery_status =
					POWER_SUPPLY_STATUS_NOT_CHARGING;
			} else {
				di->battery_status = POWER_SUPPLY_STATUS_FULL;
			}
		}
		val->intval = di->battery_status;
		return 0;
	default:
		break;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = di->voltage_uV;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = HIGH_VOLT_THRESHOLD;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = LOW_VOLT_THRESHOLD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = di->percent < 0 ? 0 :
				(di->percent > 100 ? 100 : di->percent);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		if (di->fault)
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		if (di->battery_status == POWER_SUPPLY_STATUS_FULL)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else if (di->percent <= 15)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int max8903_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct max8903_data *data = psy->drv_data;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
		if (data->ta_in)
			val->intval = 1;
		data->charger_online = val->intval;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int max8903_get_usb_property(struct power_supply *usb,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct max8903_data *data = usb->drv_data;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = 0;
		if (data->usb_in)
			val->intval = 1;
		data->usb_charger_online = val->intval;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static irqreturn_t max8903_dcin(int irq, void *_data)
{
	struct max8903_data *data = _data;
	struct max8903_pdata *pdata = data->pdata;
	bool ta_in = false;

	if (pdata->dok)
		ta_in = gpio_get_value(pdata->dok) ? false : true;

	if (ta_in == data->ta_in)
		return IRQ_HANDLED;

	data->ta_in = ta_in;
	dev_info(data->dev, "TA(DC-IN) Charger %s.\n", ta_in ?
			"Connected" : "Disconnected");
	max8903_charger_update_status(data);
	power_supply_changed(data->psy);
	power_supply_changed(data->bat);

	return IRQ_HANDLED;
}

static irqreturn_t max8903_usbin(int irq, void *_data)
{
	struct max8903_data *data = _data;
	struct max8903_pdata *pdata = data->pdata;
	bool usb_in = false;

	if (pdata->uok)
		usb_in = gpio_get_value(pdata->uok) ? false : true;
	if (usb_in == data->usb_in)
		return IRQ_HANDLED;
	data->usb_in = usb_in;
	dev_info(data->dev, "USB Charger %s.\n", usb_in ?
			"Connected" : "Disconnected");
	max8903_charger_update_status(data);
	power_supply_changed(data->bat);
	power_supply_changed(data->usb);

	return IRQ_HANDLED;
}

static irqreturn_t max8903_fault(int irq, void *_data)
{
	struct max8903_data *data = _data;
	struct max8903_pdata *pdata = data->pdata;
	bool fault;

	fault = gpio_get_value(pdata->flt) ? false : true;

	if (fault == data->fault)
		return IRQ_HANDLED;
	data->fault = fault;

	if (fault)
		dev_err(data->dev, "Charger suffers a fault and stops.\n");
	else
		dev_err(data->dev, "Charger recovered from a fault.\n");
	max8903_charger_update_status(data);
	power_supply_changed(data->psy);
	power_supply_changed(data->bat);
	power_supply_changed(data->usb);

	return IRQ_HANDLED;
}

static irqreturn_t max8903_chg(int irq, void *_data)
{
	struct max8903_data *data = _data;
	struct max8903_pdata *pdata = data->pdata;
	int chg_state;

	chg_state = gpio_get_value(pdata->chg) ? false : true;

	if (chg_state == data->chg_state)
		return IRQ_HANDLED;
	data->chg_state = chg_state;
	max8903_charger_update_status(data);
	power_supply_changed(data->psy);
	power_supply_changed(data->bat);
	power_supply_changed(data->usb);

	return IRQ_HANDLED;
}

static void max8903_battery_work(struct work_struct *work)
{
	struct max8903_data *data;

	data = container_of(work, struct max8903_data, work.work);
	data->interval = HZ * BATTERY_UPDATE_INTERVAL;

	max8903_charger_update_status(data);
	max8903_battery_update_status(data);
	dev_dbg(data->dev, "battery voltage: %4d mV\n", data->voltage_uV);
	dev_dbg(data->dev, "charger online status: %d\n",
		data->charger_online);
	dev_dbg(data->dev, "battery status : %d\n" , data->battery_status);
	dev_dbg(data->dev, "battery capacity percent: %3d\n", data->percent);
	dev_dbg(data->dev, "data->usb_in: %x , data->ta_in: %x\n",
		data->usb_in, data->ta_in);
	/* reschedule for the next time */
	schedule_delayed_work(&data->work, data->interval);
}

static ssize_t max8903_voltage_offset_discharger_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "read offset_discharger:%04d\n",
		offset_discharger);
}

static ssize_t max8903_voltage_offset_discharger_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	int ret;
	unsigned long data;

	ret = kstrtoul(buf, 10, &data);
	offset_discharger = (int)data;
	pr_info("read offset_discharger:%04d\n", offset_discharger);

	return count;
}

static ssize_t max8903_voltage_offset_charger_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "read offset_charger:%04d\n",
		offset_charger);
}

static ssize_t max8903_voltage_offset_charger_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	int ret;
	unsigned long data;

	ret = kstrtoul(buf, 10, &data);
	offset_charger = (int)data;
	pr_info("read offset_charger:%04d\n", offset_charger);
	return count;
}

static ssize_t max8903_voltage_offset_usb_charger_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "read offset_usb_charger:%04d\n",
		offset_usb_charger);
}

static ssize_t max8903_voltage_offset_usb_charger_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	int ret;
	unsigned long data;

	ret = kstrtoul(buf, 10, &data);
	offset_usb_charger = (int)data;
	pr_info("read offset_charger:%04d\n", offset_usb_charger);

	return count;
}

static struct device_attribute max8903_discharger_dev_attr = {
	.attr = {
		 .name = "max8903_ctl_offset_discharger",
		 .mode = S_IRUSR | S_IWUSR,
		 },
	.show = max8903_voltage_offset_discharger_show,
	.store = max8903_voltage_offset_discharger_store,
};

static struct device_attribute max8903_charger_dev_attr = {
	.attr = {
		 .name = "max8903_ctl_offset_charger",
		 .mode = S_IRUSR | S_IWUSR,
		 },
	.show = max8903_voltage_offset_charger_show,
	.store = max8903_voltage_offset_charger_store,
};

static struct device_attribute max8903_usb_charger_dev_attr = {
	.attr = {
		 .name = "max8903_ctl_offset_usb_charger",
		 .mode = S_IRUSR | S_IWUSR,
		 },
	.show = max8903_voltage_offset_usb_charger_show,
	.store = max8903_voltage_offset_usb_charger_store,
};

#if defined(CONFIG_OF)
static const struct of_device_id max8903_dt_ids[] = {
	{ .compatible = "fsl,max8903-charger", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, max8903_dt_ids);

static struct max8903_pdata *max8903_of_populate_pdata(
		struct device *dev)
{
	struct device_node *of_node = dev->of_node;
	struct max8903_pdata *pdata = dev->platform_data;

	if (!of_node || pdata)
		return pdata;

	pdata = devm_kzalloc(dev, sizeof(struct max8903_pdata),
				GFP_KERNEL);
	if (!pdata)
		return pdata;

	if (of_get_property(of_node, "fsl,dcm_always_high", NULL))
		pdata->dcm_always_high = true;
	if (of_get_property(of_node, "fsl,dc_valid", NULL))
		pdata->dc_valid = true;
	if (of_get_property(of_node, "fsl,usb_valid", NULL))
		pdata->usb_valid = true;
	if (of_get_property(of_node, "fsl,adc_disable", NULL))
		pdata->feature_flag = true;

	if (pdata->dc_valid) {
		pdata->dok = of_get_named_gpio(of_node, "dok_input", 0);
		if (!gpio_is_valid(pdata->dok)) {
			dev_err(dev, "pin pdata->dok: invalid gpio %d\n", pdata->dok);
			return NULL;
		}
	}
	if (pdata->usb_valid) {
		pdata->uok = of_get_named_gpio(of_node, "uok_input", 0);
		if (!gpio_is_valid(pdata->uok)) {
			dev_err(dev, "pin pdata->uok: invalid gpio %d\n", pdata->uok);
			return NULL;
		}
	}
	pdata->chg = of_get_named_gpio(of_node, "chg_input", 0);
	if (!gpio_is_valid(pdata->chg)) {
		dev_err(dev, "pin pdata->chg: invalid gpio %d\n", pdata->chg);
		return NULL;
	}
	pdata->flt = of_get_named_gpio(of_node, "flt_input", 0);
	if (!gpio_is_valid(pdata->flt)) {
		dev_err(dev, "pin pdata->flt: invalid gpio %d\n", pdata->flt);
		return NULL;
	}
	/* no need check offset without adc converter */
	if (!pdata->feature_flag) {
		if (of_property_read_u32(of_node, "offset-charger",
			&offset_charger))
			dev_err(dev, "Not setting offset-charger in dts!\n");

		if (of_property_read_u32(of_node, "offset-discharger",
			&offset_discharger))
			dev_err(dev, "Not setting offset-discharger in dts!\n");

		if (of_property_read_u32(of_node, "offset-usb-charger",
			&offset_usb_charger))
			dev_err(dev, "Not setting offset-usb-charger in dts!\n");
	}

	return pdata;
}
#endif

static int max8903_probe(struct platform_device *pdev)
{
	struct max8903_data *data;
	struct device *dev = &pdev->dev;
	struct max8903_pdata *pdata = pdev->dev.platform_data;
	struct power_supply_config psy_cfg = {};
	int ret = 0;
	int gpio = 0;
	int ta_in = 0;
	int usb_in = 0;

	data = devm_kzalloc(dev, sizeof(struct max8903_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		pdata = max8903_of_populate_pdata(&pdev->dev);
		if (!pdata)
			return -EINVAL;
	}

	data->first_delay_count = 0;
	data->pdata = pdata;
	data->dev = dev;
	data->usb_in = 0;
	data->ta_in = 0;
	platform_set_drvdata(pdev, data);

	if (pdata->dc_valid == false && pdata->usb_valid == false) {
		dev_err(dev, "No valid power sources.\n");
		ret = -EINVAL;
		goto err;
	}
	if (pdata->dc_valid) {
		if (pdata->dok && pdata->dcm_always_high) {
			gpio = pdata->dok;
			ret = gpio_request_one(gpio, GPIOF_IN, "max8903-DOK");
			if (ret) {
				dev_err(dev, "request max8903-DOK error!!\n");
				goto err;
			}
			ta_in = gpio_get_value(gpio) ? 0 : 1;
		} else {
			dev_err(dev, "When DC is wired, DOK and DCM should be"
				" wired as well or set dcm always high!\n");
			ret = -EINVAL;
			goto err;
		}
	}

	if (pdata->usb_valid) {
		if (pdata->uok) {
			gpio = pdata->uok;
			ret = gpio_request_one(gpio, GPIOF_IN, "max8903-UOK");
			if (ret) {
				dev_err(dev, "request max8903-UOK error!!\n");
				goto err;
			}
			usb_in = gpio_get_value(gpio) ? 0 : 1;
		} else {
			dev_err(dev, "When USB is wired, UOK should be wired"
				" as well.\n");
			ret = -EINVAL;
			goto err;
		}
	}

	if (pdata->chg) {
		ret = gpio_request_one(pdata->chg, GPIOF_IN, "max8903-CHG");
		if (ret) {
			dev_err(dev, "request max8903-CHG error!!\n");
			goto err;
		}
	}

	if (pdata->flt) {
		ret = gpio_request_one(pdata->flt, GPIOF_IN, "max8903-FLT");
		if (ret) {
			dev_err(dev, "request max8903-FLT error!!\n");
			goto err;
		}
	}

	data->fault = false;
	data->ta_in = ta_in;
	data->usb_in = usb_in;
	data->psy_desc.name = "max8903-ac";
	data->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;
	data->psy_desc.get_property = max8903_get_property;
	data->psy_desc.properties = max8903_charger_props;
	data->psy_desc.num_properties = ARRAY_SIZE(max8903_charger_props);

	psy_cfg.drv_data = data;

	data->psy = power_supply_register(dev, &data->psy_desc, &psy_cfg);
	if (IS_ERR(data->psy)) {
		dev_err(dev, "failed: power supply register.\n");
		goto err_psy;
	}

	data->usb_desc.name = "max8903-usb";
	data->usb_desc.type = POWER_SUPPLY_TYPE_USB;
	data->usb_desc.get_property = max8903_get_usb_property;
	data->usb_desc.properties = max8903_charger_props;
	data->usb_desc.num_properties = ARRAY_SIZE(max8903_charger_props);
	data->usb = power_supply_register(dev, &data->usb_desc, &psy_cfg);
	if (IS_ERR(data->usb)) {
		dev_err(dev, "failed: power supply register.\n");
		goto err_psy;
	}

	data->bat_desc.name = "max8903-charger";
	data->bat_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	data->bat_desc.properties = max8903_battery_props;
	data->bat_desc.num_properties = ARRAY_SIZE(max8903_battery_props);
	data->bat_desc.get_property = max8903_battery_get_property;
	data->bat_desc.use_for_apm = 1;
	data->bat = power_supply_register(&pdev->dev, &data->bat_desc, &psy_cfg);
	if (IS_ERR(data->bat)) {
		dev_err(data->dev, "failed to register battery\n");
		goto battery_failed;
	}

	INIT_DELAYED_WORK(&data->work, max8903_battery_work);
	schedule_delayed_work(&data->work, data->interval);

	if (pdata->dc_valid) {
		ret = request_threaded_irq(gpio_to_irq(pdata->dok), NULL,
			max8903_dcin, IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING | IRQF_ONESHOT, "MAX8903 DC IN",
			data);
		if (ret) {
			dev_err(dev, "Cannot request irq %d for DC (%d)\n",
					gpio_to_irq(pdata->dok), ret);
			goto err_dc_irq;
		}
	}

	if (pdata->usb_valid) {
		ret = request_threaded_irq(gpio_to_irq(pdata->uok), NULL,
			max8903_usbin, IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING | IRQF_ONESHOT, "MAX8903 USB IN",
			data);
		if (ret) {
			dev_err(dev, "Cannot request irq %d for USB (%d)\n",
					gpio_to_irq(pdata->uok), ret);
			goto err_usb_irq;
		}
	}

	if (pdata->flt) {
		ret = request_threaded_irq(gpio_to_irq(pdata->flt), NULL,
			max8903_fault, IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING | IRQF_ONESHOT, "MAX8903 Fault",
			data);
		if (ret) {
			dev_err(dev, "Cannot request irq %d for Fault (%d)\n",
					gpio_to_irq(pdata->flt), ret);
			goto err_flt_irq;
		}
	}

	if (pdata->chg) {
		ret = request_threaded_irq(gpio_to_irq(pdata->chg), NULL,
			max8903_chg, IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING | IRQF_ONESHOT, "MAX8903 Status",
			data);
		if (ret) {
			dev_err(dev, "Cannot request irq %d for Status (%d)\n",
					gpio_to_irq(pdata->flt), ret);
			goto err_chg_irq;
		}
	}

	ret = device_create_file(&pdev->dev, &max8903_discharger_dev_attr);
	if (ret)
		dev_err(&pdev->dev, "create device file failed!\n");
	ret = device_create_file(&pdev->dev, &max8903_charger_dev_attr);
	if (ret)
		dev_err(&pdev->dev, "create device file failed!\n");
	ret = device_create_file(&pdev->dev, &max8903_usb_charger_dev_attr);
	if (ret)
		dev_err(&pdev->dev, "create device file failed!\n");

	device_set_wakeup_capable(&pdev->dev, true);

	max8903_charger_update_status(data);
	max8903_battery_update_status(data);

	return 0;
err_psy:
	power_supply_unregister(data->psy);
battery_failed:
	power_supply_unregister(data->bat);
err_usb_irq:
	if (pdata->usb_valid)
		free_irq(gpio_to_irq(pdata->uok), data);
	cancel_delayed_work(&data->work);
err_dc_irq:
	if (pdata->dc_valid)
		free_irq(gpio_to_irq(pdata->dok), data);
	cancel_delayed_work(&data->work);
err_flt_irq:
	if (pdata->usb_valid)
		free_irq(gpio_to_irq(pdata->uok), data);
	cancel_delayed_work(&data->work);
err_chg_irq:
	if (pdata->dc_valid)
		free_irq(gpio_to_irq(pdata->dok), data);
	cancel_delayed_work(&data->work);
err:
	if (pdata->uok)
		gpio_free(pdata->uok);
	if (pdata->dok)
		gpio_free(pdata->dok);
	if (pdata->flt)
		gpio_free(pdata->flt);
	if (pdata->chg)
		gpio_free(pdata->chg);
	return ret;
}

static int max8903_remove(struct platform_device *pdev)
{
	struct max8903_data *data = platform_get_drvdata(pdev);
	if (data) {
		struct max8903_pdata *pdata = data->pdata;

		cancel_delayed_work_sync(&data->work);
		power_supply_unregister(data->psy);
		power_supply_unregister(data->usb);
		power_supply_unregister(data->bat);

		if (pdata->flt) {
			free_irq(gpio_to_irq(pdata->flt), data);
			gpio_free(pdata->flt);
		}
		if (pdata->usb_valid && pdata->uok) {
			free_irq(gpio_to_irq(pdata->uok), data);
			gpio_free(pdata->uok);
		}
		if (pdata->dc_valid) {
			if (pdata->dok) {
				free_irq(gpio_to_irq(pdata->dok), data);
				gpio_free(pdata->dok);
			} else if (pdata->chg) {
				free_irq(gpio_to_irq(pdata->chg), data);
				gpio_free(pdata->chg);
			}
		}

		device_remove_file(&pdev->dev, &max8903_discharger_dev_attr);
		device_remove_file(&pdev->dev, &max8903_charger_dev_attr);
		device_remove_file(&pdev->dev, &max8903_usb_charger_dev_attr);

		platform_set_drvdata(pdev, NULL);
		kfree(data);
	}

	return 0;
}

static int max8903_suspend(struct platform_device *pdev,
				  pm_message_t state)
{
	struct max8903_data *data = platform_get_drvdata(pdev);
	int irq;
	if (data) {
		struct max8903_pdata *pdata = data->pdata;
		if (pdata) {
			if (pdata->dc_valid && device_may_wakeup(&pdev->dev)) {
				irq = gpio_to_irq(pdata->dok);
				enable_irq_wake(irq);
			}

			if (pdata->usb_valid && device_may_wakeup(&pdev->dev)) {
				irq = gpio_to_irq(pdata->uok);
				enable_irq_wake(irq);
			}
			cancel_delayed_work(&data->work);
		}
	}
	return 0;
}

static int max8903_resume(struct platform_device *pdev)
{
	struct max8903_data *data = platform_get_drvdata(pdev);
	bool ta_in = false;
	bool usb_in = false;
	int irq;

	if (data) {
		struct max8903_pdata *pdata = data->pdata;

		if (pdata) {
			if (pdata->dok)
				ta_in = gpio_get_value(pdata->dok) ? false : true;
			if (pdata->uok)
				usb_in = gpio_get_value(pdata->uok) ? false : true;

			if (ta_in != data->ta_in) {
				data->ta_in = ta_in;
				dev_info(data->dev, "TA(DC-IN) Charger %s.\n", ta_in ?
				"Connected" : "Disconnected");
				max8903_charger_update_status(data);
				power_supply_changed(data->psy);
			}

			if (usb_in != data->usb_in) {
				data->usb_in = usb_in;
				dev_info(data->dev, "USB Charger %s.\n", usb_in ?
				"Connected" : "Disconnected");
				max8903_charger_update_status(data);
				power_supply_changed(data->usb);
			}

			if (pdata->dc_valid && device_may_wakeup(&pdev->dev)) {
				irq = gpio_to_irq(pdata->dok);
				disable_irq_wake(irq);
			}
			if (pdata->usb_valid && device_may_wakeup(&pdev->dev)) {
				irq = gpio_to_irq(pdata->uok);
				disable_irq_wake(irq);
			}

			schedule_delayed_work(&data->work,
			BATTERY_UPDATE_INTERVAL);
		}
	}

	return 0;
}

static struct platform_driver max8903_driver = {
	.probe	= max8903_probe,
	.remove	= max8903_remove,
	.suspend = max8903_suspend,
	.resume = max8903_resume,
	.driver = {
		.name	= "max8903-charger",
		.owner	= THIS_MODULE,
		.of_match_table = max8903_dt_ids,
	},
};
module_platform_driver(max8903_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("Sabresd Battery Driver");
MODULE_ALIAS("sabresd_battery");
