/*
 * Battery driver for One Laptop Per Child board.
 *
 *	Copyright © 2006-2010  David Woodhouse <dwmw2@infradead.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/olpc-ec.h>
#include <asm/olpc.h>


#define EC_BAT_VOLTAGE	0x10	/* uint16_t,	*9.76/32,    mV   */
#define EC_BAT_CURRENT	0x11	/* int16_t,	*15.625/120, mA   */
#define EC_BAT_ACR	0x12	/* int16_t,	*6250/15,    µAh  */
#define EC_BAT_TEMP	0x13	/* uint16_t,	*100/256,   °C  */
#define EC_AMB_TEMP	0x14	/* uint16_t,	*100/256,   °C  */
#define EC_BAT_STATUS	0x15	/* uint8_t,	bitmask */
#define EC_BAT_SOC	0x16	/* uint8_t,	percentage */
#define EC_BAT_SERIAL	0x17	/* uint8_t[6] */
#define EC_BAT_EEPROM	0x18	/* uint8_t adr as input, uint8_t output */
#define EC_BAT_ERRCODE	0x1f	/* uint8_t,	bitmask */

#define BAT_STAT_PRESENT	0x01
#define BAT_STAT_FULL		0x02
#define BAT_STAT_LOW		0x04
#define BAT_STAT_DESTROY	0x08
#define BAT_STAT_AC		0x10
#define BAT_STAT_CHARGING	0x20
#define BAT_STAT_DISCHARGING	0x40
#define BAT_STAT_TRICKLE	0x80

#define BAT_ERR_INFOFAIL	0x02
#define BAT_ERR_OVERVOLTAGE	0x04
#define BAT_ERR_OVERTEMP	0x05
#define BAT_ERR_GAUGESTOP	0x06
#define BAT_ERR_OUT_OF_CONTROL	0x07
#define BAT_ERR_ID_FAIL		0x09
#define BAT_ERR_ACR_FAIL	0x10

#define BAT_ADDR_MFR_TYPE	0x5F

struct olpc_battery_data {
	struct power_supply *olpc_ac;
	struct power_supply *olpc_bat;
	char bat_serial[17];
	bool new_proto;
	bool little_endian;
};

/*********************************************************************
 *		Power
 *********************************************************************/

static int olpc_ac_get_prop(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	int ret = 0;
	uint8_t status;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = olpc_ec_cmd(EC_BAT_STATUS, NULL, 0, &status, 1);
		if (ret)
			return ret;

		val->intval = !!(status & BAT_STAT_AC);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static enum power_supply_property olpc_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static const struct power_supply_desc olpc_ac_desc = {
	.name = "olpc-ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = olpc_ac_props,
	.num_properties = ARRAY_SIZE(olpc_ac_props),
	.get_property = olpc_ac_get_prop,
};

static int olpc_bat_get_status(struct olpc_battery_data *data,
		union power_supply_propval *val, uint8_t ec_byte)
{
	if (data->new_proto) {
		if (ec_byte & (BAT_STAT_CHARGING | BAT_STAT_TRICKLE))
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (ec_byte & BAT_STAT_DISCHARGING)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (ec_byte & BAT_STAT_FULL)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else /* er,... */
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
	} else {
		/* Older EC didn't report charge/discharge bits */
		if (!(ec_byte & BAT_STAT_AC)) /* No AC means discharging */
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (ec_byte & BAT_STAT_FULL)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else /* Not _necessarily_ true but EC doesn't tell all yet */
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
	}

	return 0;
}

static int olpc_bat_get_health(union power_supply_propval *val)
{
	uint8_t ec_byte;
	int ret;

	ret = olpc_ec_cmd(EC_BAT_ERRCODE, NULL, 0, &ec_byte, 1);
	if (ret)
		return ret;

	switch (ec_byte) {
	case 0:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;

	case BAT_ERR_OVERTEMP:
		val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		break;

	case BAT_ERR_OVERVOLTAGE:
		val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		break;

	case BAT_ERR_INFOFAIL:
	case BAT_ERR_OUT_OF_CONTROL:
	case BAT_ERR_ID_FAIL:
	case BAT_ERR_ACR_FAIL:
		val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		break;

	default:
		/* Eep. We don't know this failure code */
		ret = -EIO;
	}

	return ret;
}

static int olpc_bat_get_mfr(union power_supply_propval *val)
{
	uint8_t ec_byte;
	int ret;

	ec_byte = BAT_ADDR_MFR_TYPE;
	ret = olpc_ec_cmd(EC_BAT_EEPROM, &ec_byte, 1, &ec_byte, 1);
	if (ret)
		return ret;

	switch (ec_byte >> 4) {
	case 1:
		val->strval = "Gold Peak";
		break;
	case 2:
		val->strval = "BYD";
		break;
	default:
		val->strval = "Unknown";
		break;
	}

	return ret;
}

static int olpc_bat_get_tech(union power_supply_propval *val)
{
	uint8_t ec_byte;
	int ret;

	ec_byte = BAT_ADDR_MFR_TYPE;
	ret = olpc_ec_cmd(EC_BAT_EEPROM, &ec_byte, 1, &ec_byte, 1);
	if (ret)
		return ret;

	switch (ec_byte & 0xf) {
	case 1:
		val->intval = POWER_SUPPLY_TECHNOLOGY_NiMH;
		break;
	case 2:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LiFe;
		break;
	default:
		val->intval = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
		break;
	}

	return ret;
}

static int olpc_bat_get_charge_full_design(union power_supply_propval *val)
{
	uint8_t ec_byte;
	union power_supply_propval tech;
	int ret, mfr;

	ret = olpc_bat_get_tech(&tech);
	if (ret)
		return ret;

	ec_byte = BAT_ADDR_MFR_TYPE;
	ret = olpc_ec_cmd(EC_BAT_EEPROM, &ec_byte, 1, &ec_byte, 1);
	if (ret)
		return ret;

	mfr = ec_byte >> 4;

	switch (tech.intval) {
	case POWER_SUPPLY_TECHNOLOGY_NiMH:
		switch (mfr) {
		case 1: /* Gold Peak */
			val->intval = 3000000*.8;
			break;
		default:
			return -EIO;
		}
		break;

	case POWER_SUPPLY_TECHNOLOGY_LiFe:
		switch (mfr) {
		case 1: /* Gold Peak, fall through */
		case 2: /* BYD */
			val->intval = 2800000;
			break;
		default:
			return -EIO;
		}
		break;

	default:
		return -EIO;
	}

	return ret;
}

static int olpc_bat_get_charge_now(union power_supply_propval *val)
{
	uint8_t soc;
	union power_supply_propval full;
	int ret;

	ret = olpc_ec_cmd(EC_BAT_SOC, NULL, 0, &soc, 1);
	if (ret)
		return ret;

	ret = olpc_bat_get_charge_full_design(&full);
	if (ret)
		return ret;

	val->intval = soc * (full.intval / 100);
	return 0;
}

static int olpc_bat_get_voltage_max_design(union power_supply_propval *val)
{
	uint8_t ec_byte;
	union power_supply_propval tech;
	int mfr;
	int ret;

	ret = olpc_bat_get_tech(&tech);
	if (ret)
		return ret;

	ec_byte = BAT_ADDR_MFR_TYPE;
	ret = olpc_ec_cmd(EC_BAT_EEPROM, &ec_byte, 1, &ec_byte, 1);
	if (ret)
		return ret;

	mfr = ec_byte >> 4;

	switch (tech.intval) {
	case POWER_SUPPLY_TECHNOLOGY_NiMH:
		switch (mfr) {
		case 1: /* Gold Peak */
			val->intval = 6000000;
			break;
		default:
			return -EIO;
		}
		break;

	case POWER_SUPPLY_TECHNOLOGY_LiFe:
		switch (mfr) {
		case 1: /* Gold Peak */
			val->intval = 6400000;
			break;
		case 2: /* BYD */
			val->intval = 6500000;
			break;
		default:
			return -EIO;
		}
		break;

	default:
		return -EIO;
	}

	return ret;
}

static u16 ecword_to_cpu(struct olpc_battery_data *data, u16 ec_word)
{
	if (data->little_endian)
		return le16_to_cpu((__force __le16)ec_word);
	else
		return be16_to_cpu((__force __be16)ec_word);
}

/*********************************************************************
 *		Battery properties
 *********************************************************************/
static int olpc_bat_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	struct olpc_battery_data *data = power_supply_get_drvdata(psy);
	int ret = 0;
	u16 ec_word;
	uint8_t ec_byte;
	__be64 ser_buf;

	ret = olpc_ec_cmd(EC_BAT_STATUS, NULL, 0, &ec_byte, 1);
	if (ret)
		return ret;

	/* Theoretically there's a race here -- the battery could be
	   removed immediately after we check whether it's present, and
	   then we query for some other property of the now-absent battery.
	   It doesn't matter though -- the EC will return the last-known
	   information, and it's as if we just ran that _little_ bit faster
	   and managed to read it out before the battery went away. */
	if (!(ec_byte & (BAT_STAT_PRESENT | BAT_STAT_TRICKLE)) &&
			psp != POWER_SUPPLY_PROP_PRESENT)
		return -ENODEV;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = olpc_bat_get_status(data, val, ec_byte);
		if (ret)
			return ret;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		if (ec_byte & BAT_STAT_TRICKLE)
			val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		else if (ec_byte & BAT_STAT_CHARGING)
			val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
		else
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = !!(ec_byte & (BAT_STAT_PRESENT |
					    BAT_STAT_TRICKLE));
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (ec_byte & BAT_STAT_DESTROY)
			val->intval = POWER_SUPPLY_HEALTH_DEAD;
		else {
			ret = olpc_bat_get_health(val);
			if (ret)
				return ret;
		}
		break;

	case POWER_SUPPLY_PROP_MANUFACTURER:
		ret = olpc_bat_get_mfr(val);
		if (ret)
			return ret;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		ret = olpc_bat_get_tech(val);
		if (ret)
			return ret;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = olpc_ec_cmd(EC_BAT_VOLTAGE, NULL, 0, (void *)&ec_word, 2);
		if (ret)
			return ret;

		val->intval = ecword_to_cpu(data, ec_word) * 9760L / 32;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = olpc_ec_cmd(EC_BAT_CURRENT, NULL, 0, (void *)&ec_word, 2);
		if (ret)
			return ret;

		val->intval = ecword_to_cpu(data, ec_word) * 15625L / 120;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = olpc_ec_cmd(EC_BAT_SOC, NULL, 0, &ec_byte, 1);
		if (ret)
			return ret;
		val->intval = ec_byte;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		if (ec_byte & BAT_STAT_FULL)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else if (ec_byte & BAT_STAT_LOW)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = olpc_bat_get_charge_full_design(val);
		if (ret)
			return ret;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = olpc_bat_get_charge_now(val);
		if (ret)
			return ret;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = olpc_ec_cmd(EC_BAT_TEMP, NULL, 0, (void *)&ec_word, 2);
		if (ret)
			return ret;

		val->intval = ecword_to_cpu(data, ec_word) * 10 / 256;
		break;
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		ret = olpc_ec_cmd(EC_AMB_TEMP, NULL, 0, (void *)&ec_word, 2);
		if (ret)
			return ret;

		val->intval = (int)ecword_to_cpu(data, ec_word) * 10 / 256;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		ret = olpc_ec_cmd(EC_BAT_ACR, NULL, 0, (void *)&ec_word, 2);
		if (ret)
			return ret;

		val->intval = ecword_to_cpu(data, ec_word) * 6250 / 15;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		ret = olpc_ec_cmd(EC_BAT_SERIAL, NULL, 0, (void *)&ser_buf, 8);
		if (ret)
			return ret;

		sprintf(data->bat_serial, "%016llx", (long long)be64_to_cpu(ser_buf));
		val->strval = data->bat_serial;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		ret = olpc_bat_get_voltage_max_design(val);
		if (ret)
			return ret;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum power_supply_property olpc_xo1_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_AMBIENT,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
};

/* XO-1.5 does not have ambient temperature property */
static enum power_supply_property olpc_xo15_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
};

/* EEPROM reading goes completely around the power_supply API, sadly */

#define EEPROM_START	0x20
#define EEPROM_END	0x80
#define EEPROM_SIZE	(EEPROM_END - EEPROM_START)

static ssize_t olpc_bat_eeprom_read(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	uint8_t ec_byte;
	int ret;
	int i;

	for (i = 0; i < count; i++) {
		ec_byte = EEPROM_START + off + i;
		ret = olpc_ec_cmd(EC_BAT_EEPROM, &ec_byte, 1, &buf[i], 1);
		if (ret) {
			pr_err("olpc-battery: "
			       "EC_BAT_EEPROM cmd @ 0x%x failed - %d!\n",
			       ec_byte, ret);
			return -EIO;
		}
	}

	return count;
}

static struct bin_attribute olpc_bat_eeprom = {
	.attr = {
		.name = "eeprom",
		.mode = S_IRUGO,
	},
	.size = EEPROM_SIZE,
	.read = olpc_bat_eeprom_read,
};

/* Allow userspace to see the specific error value pulled from the EC */

static ssize_t olpc_bat_error_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint8_t ec_byte;
	ssize_t ret;

	ret = olpc_ec_cmd(EC_BAT_ERRCODE, NULL, 0, &ec_byte, 1);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", ec_byte);
}

static struct device_attribute olpc_bat_error = {
	.attr = {
		.name = "error",
		.mode = S_IRUGO,
	},
	.show = olpc_bat_error_read,
};

static struct attribute *olpc_bat_sysfs_attrs[] = {
	&olpc_bat_error.attr,
	NULL
};

static struct bin_attribute *olpc_bat_sysfs_bin_attrs[] = {
	&olpc_bat_eeprom,
	NULL
};

static const struct attribute_group olpc_bat_sysfs_group = {
	.attrs = olpc_bat_sysfs_attrs,
	.bin_attrs = olpc_bat_sysfs_bin_attrs,

};

static const struct attribute_group *olpc_bat_sysfs_groups[] = {
	&olpc_bat_sysfs_group,
	NULL
};

/*********************************************************************
 *		Initialisation
 *********************************************************************/

static struct power_supply_desc olpc_bat_desc = {
	.name = "olpc-battery",
	.get_property = olpc_bat_get_property,
	.use_for_apm = 1,
};

static int olpc_battery_suspend(struct platform_device *pdev,
				pm_message_t state)
{
	struct olpc_battery_data *data = platform_get_drvdata(pdev);

	if (device_may_wakeup(&data->olpc_ac->dev))
		olpc_ec_wakeup_set(EC_SCI_SRC_ACPWR);
	else
		olpc_ec_wakeup_clear(EC_SCI_SRC_ACPWR);

	if (device_may_wakeup(&data->olpc_bat->dev))
		olpc_ec_wakeup_set(EC_SCI_SRC_BATTERY | EC_SCI_SRC_BATSOC
				   | EC_SCI_SRC_BATERR);
	else
		olpc_ec_wakeup_clear(EC_SCI_SRC_BATTERY | EC_SCI_SRC_BATSOC
				     | EC_SCI_SRC_BATERR);

	return 0;
}

static int olpc_battery_probe(struct platform_device *pdev)
{
	struct power_supply_config bat_psy_cfg = {};
	struct power_supply_config ac_psy_cfg = {};
	struct olpc_battery_data *data;
	uint8_t status;
	uint8_t ecver;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	platform_set_drvdata(pdev, data);

	/* See if the EC is already there and get the EC revision */
	ret = olpc_ec_cmd(EC_FIRMWARE_REV, NULL, 0, &ecver, 1);
	if (ret)
		return ret;

	if (of_find_compatible_node(NULL, NULL, "olpc,xo1.75-ec")) {
		/* XO 1.75 */
		data->new_proto = true;
		data->little_endian = true;
	} else if (ecver > 0x44) {
		/* XO 1 or 1.5 with a new EC firmware. */
		data->new_proto = true;
	} else if (ecver < 0x44) {
		/*
		 * We've seen a number of EC protocol changes; this driver
		 * requires the latest EC protocol, supported by 0x44 and above.
		 */
		printk(KERN_NOTICE "OLPC EC version 0x%02x too old for "
			"battery driver.\n", ecver);
		return -ENXIO;
	}

	ret = olpc_ec_cmd(EC_BAT_STATUS, NULL, 0, &status, 1);
	if (ret)
		return ret;

	/* Ignore the status. It doesn't actually matter */

	ac_psy_cfg.of_node = pdev->dev.of_node;
	ac_psy_cfg.drv_data = data;

	data->olpc_ac = devm_power_supply_register(&pdev->dev, &olpc_ac_desc,
								&ac_psy_cfg);
	if (IS_ERR(data->olpc_ac))
		return PTR_ERR(data->olpc_ac);

	if (of_device_is_compatible(pdev->dev.of_node, "olpc,xo1.5-battery")) {
		/* XO-1.5 */
		olpc_bat_desc.properties = olpc_xo15_bat_props;
		olpc_bat_desc.num_properties = ARRAY_SIZE(olpc_xo15_bat_props);
	} else {
		/* XO-1 */
		olpc_bat_desc.properties = olpc_xo1_bat_props;
		olpc_bat_desc.num_properties = ARRAY_SIZE(olpc_xo1_bat_props);
	}

	bat_psy_cfg.of_node = pdev->dev.of_node;
	bat_psy_cfg.drv_data = data;
	bat_psy_cfg.attr_grp = olpc_bat_sysfs_groups;

	data->olpc_bat = devm_power_supply_register(&pdev->dev, &olpc_bat_desc,
								&bat_psy_cfg);
	if (IS_ERR(data->olpc_bat))
		return PTR_ERR(data->olpc_bat);

	if (olpc_ec_wakeup_available()) {
		device_set_wakeup_capable(&data->olpc_ac->dev, true);
		device_set_wakeup_capable(&data->olpc_bat->dev, true);
	}

	return 0;
}

static const struct of_device_id olpc_battery_ids[] = {
	{ .compatible = "olpc,xo1-battery" },
	{ .compatible = "olpc,xo1.5-battery" },
	{}
};
MODULE_DEVICE_TABLE(of, olpc_battery_ids);

static struct platform_driver olpc_battery_driver = {
	.driver = {
		.name = "olpc-battery",
		.of_match_table = olpc_battery_ids,
	},
	.probe = olpc_battery_probe,
	.suspend = olpc_battery_suspend,
};

module_platform_driver(olpc_battery_driver);

MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Battery driver for One Laptop Per Child 'XO' machine");
