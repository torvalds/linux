// SPDX-License-Identifier: GPL-2.0
/*
 * power_supply class (battery) driver for the I2C attached embedded controller
 * found on Vexia EDU ATLA 10 (9V version) tablets.
 *
 * This is based on the ACPI Battery device in the DSDT which should work
 * expect that it expects the I2C controller to be enumerated as an ACPI
 * device and the tablet's BIOS enumerates all LPSS devices as PCI devices
 * (and changing the LPSS BIOS settings from PCI -> ACPI does not work).
 *
 * Copyright (c) 2024 Hans de Goede <hansg@kernel.org>
 */

#include <linux/bits.h>
#include <linux/devm-helpers.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <asm/byteorder.h>

/* State field uses ACPI Battery spec status bits */
#define ACPI_BATTERY_STATE_DISCHARGING		BIT(0)
#define ACPI_BATTERY_STATE_CHARGING		BIT(1)

#define ATLA10_EC_BATTERY_STATE_COMMAND		0x87
#define ATLA10_EC_BATTERY_INFO_COMMAND		0x88

/* From broken ACPI battery device in DSDT */
#define ATLA10_EC_VOLTAGE_MIN_DESIGN_uV		3750000

/* Update data every 5 seconds */
#define UPDATE_INTERVAL_JIFFIES			(5 * HZ)

struct atla10_ec_battery_state {
	u8 status;			/* Using ACPI Battery spec status bits */
	u8 capacity;			/* Percent */
	__le16 charge_now_mAh;
	__le16 voltage_now_mV;
	__le16 current_now_mA;
	__le16 charge_full_mAh;
	__le16 temp;			/* centi degrees Celsius */
} __packed;

struct atla10_ec_battery_info {
	__le16 charge_full_design_mAh;
	__le16 voltage_now_mV;		/* Should be design voltage, but is not ? */
	__le16 charge_full_design2_mAh;
} __packed;

struct atla10_ec_data {
	struct i2c_client *client;
	struct power_supply *psy;
	struct delayed_work work;
	struct mutex update_lock;
	struct atla10_ec_battery_info info;
	struct atla10_ec_battery_state state;
	bool valid;			/* true if state is valid */
	unsigned long last_update;	/* In jiffies */
};

static int atla10_ec_cmd(struct atla10_ec_data *data, u8 cmd, u8 len, u8 *values)
{
	struct device *dev = &data->client->dev;
	u8 buf[I2C_SMBUS_BLOCK_MAX];
	int ret;

	ret = i2c_smbus_read_block_data(data->client, cmd, buf);
	if (ret != len) {
		dev_err(dev, "I2C command 0x%02x error: %d\n", cmd, ret);
		return -EIO;
	}

	memcpy(values, buf, len);
	return 0;
}

static int atla10_ec_update(struct atla10_ec_data *data)
{
	int ret;

	if (data->valid && time_before(jiffies, data->last_update + UPDATE_INTERVAL_JIFFIES))
		return 0;

	ret = atla10_ec_cmd(data, ATLA10_EC_BATTERY_STATE_COMMAND,
			    sizeof(data->state), (u8 *)&data->state);
	if (ret)
		return ret;

	data->last_update = jiffies;
	data->valid = true;
	return 0;
}

static int atla10_ec_psy_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct atla10_ec_data *data = power_supply_get_drvdata(psy);
	int charge_now_mAh, charge_full_mAh, ret;

	guard(mutex)(&data->update_lock);

	ret = atla10_ec_update(data);
	if (ret)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (data->state.status & ACPI_BATTERY_STATE_DISCHARGING)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (data->state.status & ACPI_BATTERY_STATE_CHARGING)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (data->state.capacity == 100)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = data->state.capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		/*
		 * The EC has a bug where it reports charge-full-design as
		 * charge-now when the battery is full. Clamp charge-now to
		 * charge-full to workaround this.
		 */
		charge_now_mAh = le16_to_cpu(data->state.charge_now_mAh);
		charge_full_mAh = le16_to_cpu(data->state.charge_full_mAh);
		val->intval = min(charge_now_mAh, charge_full_mAh) * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = le16_to_cpu(data->state.voltage_now_mV) * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = le16_to_cpu(data->state.current_now_mA) * 1000;
		/*
		 * Documentation/ABI/testing/sysfs-class-power specifies
		 * negative current for discharging.
		 */
		if (data->state.status & ACPI_BATTERY_STATE_DISCHARGING)
			val->intval = -val->intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = le16_to_cpu(data->state.charge_full_mAh) * 1000;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = le16_to_cpu(data->state.temp) / 10;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = le16_to_cpu(data->info.charge_full_design_mAh) * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = ATLA10_EC_VOLTAGE_MIN_DESIGN_uV;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void atla10_ec_external_power_changed_work(struct work_struct *work)
{
	struct atla10_ec_data *data = container_of(work, struct atla10_ec_data, work.work);

	dev_dbg(&data->client->dev, "External power changed\n");
	data->valid = false;
	power_supply_changed(data->psy);
}

static void atla10_ec_external_power_changed(struct power_supply *psy)
{
	struct atla10_ec_data *data = power_supply_get_drvdata(psy);

	/* After charger plug in/out wait 0.5s for things to stabilize */
	mod_delayed_work(system_wq, &data->work, HZ / 2);
}

static const enum power_supply_property atla10_ec_psy_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
};

static const struct power_supply_desc atla10_ec_psy_desc = {
	.name = "atla10_ec_battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = atla10_ec_psy_props,
	.num_properties = ARRAY_SIZE(atla10_ec_psy_props),
	.get_property = atla10_ec_psy_get_property,
	.external_power_changed = atla10_ec_external_power_changed,
};

static int atla10_ec_probe(struct i2c_client *client)
{
	struct power_supply_config psy_cfg = { };
	struct device *dev = &client->dev;
	struct atla10_ec_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	psy_cfg.drv_data = data;
	data->client = client;

	ret = devm_mutex_init(dev, &data->update_lock);
	if (ret)
		return ret;

	ret = devm_delayed_work_autocancel(dev, &data->work,
					   atla10_ec_external_power_changed_work);
	if (ret)
		return ret;

	ret = atla10_ec_cmd(data, ATLA10_EC_BATTERY_INFO_COMMAND,
			    sizeof(data->info), (u8 *)&data->info);
	if (ret)
		return ret;

	data->psy = devm_power_supply_register(dev, &atla10_ec_psy_desc, &psy_cfg);
	return PTR_ERR_OR_ZERO(data->psy);
}

static const struct i2c_device_id atla10_ec_id_table[] = {
	{ "vexia_atla10_ec" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, atla10_ec_id_table);

static struct i2c_driver atla10_ec_driver = {
	.driver = {
		.name = "vexia_atla10_ec",
	},
	.probe = atla10_ec_probe,
	.id_table = atla10_ec_id_table,
};
module_i2c_driver(atla10_ec_driver);

MODULE_AUTHOR("Hans de Goede <hansg@kernel.org>");
MODULE_DESCRIPTION("Battery driver for Vexia EDU ATLA 10 tablet EC");
MODULE_LICENSE("GPL");
