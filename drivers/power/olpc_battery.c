/*
 * Battery driver for One Laptop Per Child board.
 *
 *	Copyright © 2006  David Woodhouse <dwmw2@infradead.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <asm/olpc.h>


#define EC_BAT_VOLTAGE	0x10	/* uint16_t,	*9.76/32,    mV   */
#define EC_BAT_CURRENT	0x11	/* int16_t,	*15.625/120, mA   */
#define EC_BAT_ACR	0x12
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

#define BAT_ERR_INFOFAIL	0x02
#define BAT_ERR_OVERVOLTAGE	0x04
#define BAT_ERR_OVERTEMP	0x05
#define BAT_ERR_GAUGESTOP	0x06
#define BAT_ERR_OUT_OF_CONTROL	0x07
#define BAT_ERR_ID_FAIL		0x09
#define BAT_ERR_ACR_FAIL	0x10

#define BAT_ADDR_MFR_TYPE	0x5F

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

static struct power_supply olpc_ac = {
	.name = "olpc-ac",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = olpc_ac_props,
	.num_properties = ARRAY_SIZE(olpc_ac_props),
	.get_property = olpc_ac_get_prop,
};

/*********************************************************************
 *		Battery properties
 *********************************************************************/
static int olpc_bat_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	int ret = 0;
	int16_t ec_word;
	uint8_t ec_byte;

	ret = olpc_ec_cmd(EC_BAT_STATUS, NULL, 0, &ec_byte, 1);
	if (ret)
		return ret;

	/* Theoretically there's a race here -- the battery could be
	   removed immediately after we check whether it's present, and
	   then we query for some other property of the now-absent battery.
	   It doesn't matter though -- the EC will return the last-known
	   information, and it's as if we just ran that _little_ bit faster
	   and managed to read it out before the battery went away. */
	if (!(ec_byte & BAT_STAT_PRESENT) && psp != POWER_SUPPLY_PROP_PRESENT)
		return -ENODEV;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (olpc_platform_info.ecver > 0x44) {
			if (ec_byte & BAT_STAT_CHARGING)
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
			break;
		}
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = !!(ec_byte & BAT_STAT_PRESENT);
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (ec_byte & BAT_STAT_DESTROY)
			val->intval = POWER_SUPPLY_HEALTH_DEAD;
		else {
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
				return -EIO;
			}
		}
		break;

	case POWER_SUPPLY_PROP_MANUFACTURER:
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
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
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
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		ret = olpc_ec_cmd(EC_BAT_VOLTAGE, NULL, 0, (void *)&ec_word, 2);
		if (ret)
			return ret;

		ec_word = be16_to_cpu(ec_word);
		val->intval = ec_word * 9760L / 32;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = olpc_ec_cmd(EC_BAT_CURRENT, NULL, 0, (void *)&ec_word, 2);
		if (ret)
			return ret;

		ec_word = be16_to_cpu(ec_word);
		val->intval = ec_word * 15625L / 120;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = olpc_ec_cmd(EC_BAT_SOC, NULL, 0, &ec_byte, 1);
		if (ret)
			return ret;
		val->intval = ec_byte;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = olpc_ec_cmd(EC_BAT_TEMP, NULL, 0, (void *)&ec_word, 2);
		if (ret)
			return ret;
		ec_word = be16_to_cpu(ec_word);
		val->intval = ec_word * 100 / 256;
		break;
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		ret = olpc_ec_cmd(EC_AMB_TEMP, NULL, 0, (void *)&ec_word, 2);
		if (ret)
			return ret;

		ec_word = be16_to_cpu(ec_word);
		val->intval = ec_word * 100 / 256;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum power_supply_property olpc_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_AMBIENT,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

/*********************************************************************
 *		Initialisation
 *********************************************************************/

static struct platform_device *bat_pdev;

static struct power_supply olpc_bat = {
	.properties = olpc_bat_props,
	.num_properties = ARRAY_SIZE(olpc_bat_props),
	.get_property = olpc_bat_get_property,
	.use_for_apm = 1,
};

void olpc_battery_trigger_uevent(unsigned long cause)
{
	if (cause & EC_SCI_SRC_ACPWR)
		kobject_uevent(&olpc_ac.dev->kobj, KOBJ_CHANGE);
	if (cause & (EC_SCI_SRC_BATERR|EC_SCI_SRC_BATSOC|EC_SCI_SRC_BATTERY))
		kobject_uevent(&olpc_bat.dev->kobj, KOBJ_CHANGE);
}

static int __init olpc_bat_init(void)
{
	int ret = 0;
	uint8_t status;

	if (!olpc_platform_info.ecver)
		return -ENXIO;
	if (olpc_platform_info.ecver < 0x43) {
		printk(KERN_NOTICE "OLPC EC version 0x%02x too old for battery driver.\n", olpc_platform_info.ecver);
		return -ENXIO;
	}

	ret = olpc_ec_cmd(EC_BAT_STATUS, NULL, 0, &status, 1);
	if (ret)
		return ret;

	/* Ignore the status. It doesn't actually matter */

	bat_pdev = platform_device_register_simple("olpc-battery", 0, NULL, 0);
	if (IS_ERR(bat_pdev))
		return PTR_ERR(bat_pdev);

	ret = power_supply_register(&bat_pdev->dev, &olpc_ac);
	if (ret)
		goto ac_failed;

	olpc_bat.name = bat_pdev->name;

	ret = power_supply_register(&bat_pdev->dev, &olpc_bat);
	if (ret)
		goto battery_failed;

	goto success;

battery_failed:
	power_supply_unregister(&olpc_ac);
ac_failed:
	platform_device_unregister(bat_pdev);
success:
	return ret;
}

static void __exit olpc_bat_exit(void)
{
	power_supply_unregister(&olpc_bat);
	power_supply_unregister(&olpc_ac);
	platform_device_unregister(bat_pdev);
}

module_init(olpc_bat_init);
module_exit(olpc_bat_exit);

MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Battery driver for One Laptop Per Child 'XO' machine");
