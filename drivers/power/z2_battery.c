/*
 * Battery measurement code for Zipit Z2
 *
 * Copyright (C) 2009 Peter Edwards <sweetlilmre@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/z2_battery.h>

#define	Z2_DEFAULT_NAME	"Z2"

struct z2_charger {
	struct z2_battery_info	*info;
	int			bat_status;
	struct i2c_client	*client;
	struct power_supply	batt_ps;
	struct mutex		work_lock;
	struct work_struct	bat_work;
};

static unsigned long z2_read_bat(struct z2_charger *charger)
{
	int data;
	data = i2c_smbus_read_byte_data(charger->client,
					charger->info->batt_I2C_reg);
	if (data < 0)
		return 0;

	return data * charger->info->batt_mult / charger->info->batt_div;
}

static int z2_batt_get_property(struct power_supply *batt_ps,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct z2_charger *charger = container_of(batt_ps, struct z2_charger,
						batt_ps);
	struct z2_battery_info *info = charger->info;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = charger->bat_status;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = info->batt_tech;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (info->batt_I2C_reg >= 0)
			val->intval = z2_read_bat(charger);
		else
			return -EINVAL;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (info->max_voltage >= 0)
			val->intval = info->max_voltage;
		else
			return -EINVAL;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		if (info->min_voltage >= 0)
			val->intval = info->min_voltage;
		else
			return -EINVAL;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void z2_batt_ext_power_changed(struct power_supply *batt_ps)
{
	struct z2_charger *charger = container_of(batt_ps, struct z2_charger,
						batt_ps);
	schedule_work(&charger->bat_work);
}

static void z2_batt_update(struct z2_charger *charger)
{
	int old_status = charger->bat_status;
	struct z2_battery_info *info;

	info = charger->info;

	mutex_lock(&charger->work_lock);

	charger->bat_status = (info->charge_gpio >= 0) ?
		(gpio_get_value(info->charge_gpio) ?
		POWER_SUPPLY_STATUS_CHARGING :
		POWER_SUPPLY_STATUS_DISCHARGING) :
		POWER_SUPPLY_STATUS_UNKNOWN;

	if (old_status != charger->bat_status) {
		pr_debug("%s: %i -> %i\n", charger->batt_ps.name, old_status,
			charger->bat_status);
		power_supply_changed(&charger->batt_ps);
	}

	mutex_unlock(&charger->work_lock);
}

static void z2_batt_work(struct work_struct *work)
{
	struct z2_charger *charger;
	charger = container_of(work, struct z2_charger, bat_work);
	z2_batt_update(charger);
}

static irqreturn_t z2_charge_switch_irq(int irq, void *devid)
{
	struct z2_charger *charger = devid;
	schedule_work(&charger->bat_work);
	return IRQ_HANDLED;
}

static int z2_batt_ps_init(struct z2_charger *charger, int props)
{
	int i = 0;
	enum power_supply_property *prop;
	struct z2_battery_info *info = charger->info;

	if (info->charge_gpio >= 0)
		props++;	/* POWER_SUPPLY_PROP_STATUS */
	if (info->batt_tech >= 0)
		props++;	/* POWER_SUPPLY_PROP_TECHNOLOGY */
	if (info->batt_I2C_reg >= 0)
		props++;	/* POWER_SUPPLY_PROP_VOLTAGE_NOW */
	if (info->max_voltage >= 0)
		props++;	/* POWER_SUPPLY_PROP_VOLTAGE_MAX */
	if (info->min_voltage >= 0)
		props++;	/* POWER_SUPPLY_PROP_VOLTAGE_MIN */

	prop = kzalloc(props * sizeof(*prop), GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	prop[i++] = POWER_SUPPLY_PROP_PRESENT;
	if (info->charge_gpio >= 0)
		prop[i++] = POWER_SUPPLY_PROP_STATUS;
	if (info->batt_tech >= 0)
		prop[i++] = POWER_SUPPLY_PROP_TECHNOLOGY;
	if (info->batt_I2C_reg >= 0)
		prop[i++] = POWER_SUPPLY_PROP_VOLTAGE_NOW;
	if (info->max_voltage >= 0)
		prop[i++] = POWER_SUPPLY_PROP_VOLTAGE_MAX;
	if (info->min_voltage >= 0)
		prop[i++] = POWER_SUPPLY_PROP_VOLTAGE_MIN;

	if (!info->batt_name) {
		dev_info(&charger->client->dev,
				"Please consider setting proper battery "
				"name in platform definition file, falling "
				"back to name \" Z2_DEFAULT_NAME \"\n");
		charger->batt_ps.name = Z2_DEFAULT_NAME;
	} else
		charger->batt_ps.name = info->batt_name;

	charger->batt_ps.properties		= prop;
	charger->batt_ps.num_properties		= props;
	charger->batt_ps.type			= POWER_SUPPLY_TYPE_BATTERY;
	charger->batt_ps.get_property		= z2_batt_get_property;
	charger->batt_ps.external_power_changed	= z2_batt_ext_power_changed;
	charger->batt_ps.use_for_apm		= 1;

	return 0;
}

static int __devinit z2_batt_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int ret = 0;
	int props = 1;	/* POWER_SUPPLY_PROP_PRESENT */
	struct z2_charger *charger;
	struct z2_battery_info *info = client->dev.platform_data;

	if (info == NULL) {
		dev_err(&client->dev,
			"Please set platform device platform_data"
			" to a valid z2_battery_info pointer!\n");
		return -EINVAL;
	}

	charger = kzalloc(sizeof(*charger), GFP_KERNEL);
	if (charger == NULL)
		return -ENOMEM;

	charger->bat_status = POWER_SUPPLY_STATUS_UNKNOWN;
	charger->info = info;
	charger->client = client;
	i2c_set_clientdata(client, charger);

	mutex_init(&charger->work_lock);

	if (info->charge_gpio >= 0 && gpio_is_valid(info->charge_gpio)) {
		ret = gpio_request(info->charge_gpio, "BATT CHRG");
		if (ret)
			goto err;

		ret = gpio_direction_input(info->charge_gpio);
		if (ret)
			goto err2;

		irq_set_irq_type(gpio_to_irq(info->charge_gpio),
				 IRQ_TYPE_EDGE_BOTH);
		ret = request_irq(gpio_to_irq(info->charge_gpio),
				z2_charge_switch_irq, IRQF_DISABLED,
				"AC Detect", charger);
		if (ret)
			goto err3;
	}

	ret = z2_batt_ps_init(charger, props);
	if (ret)
		goto err3;

	INIT_WORK(&charger->bat_work, z2_batt_work);

	ret = power_supply_register(&client->dev, &charger->batt_ps);
	if (ret)
		goto err4;

	schedule_work(&charger->bat_work);

	return 0;

err4:
	kfree(charger->batt_ps.properties);
err3:
	if (info->charge_gpio >= 0 && gpio_is_valid(info->charge_gpio))
		free_irq(gpio_to_irq(info->charge_gpio), charger);
err2:
	if (info->charge_gpio >= 0 && gpio_is_valid(info->charge_gpio))
		gpio_free(info->charge_gpio);
err:
	kfree(charger);
	return ret;
}

static int __devexit z2_batt_remove(struct i2c_client *client)
{
	struct z2_charger *charger = i2c_get_clientdata(client);
	struct z2_battery_info *info = charger->info;

	cancel_work_sync(&charger->bat_work);
	power_supply_unregister(&charger->batt_ps);

	kfree(charger->batt_ps.properties);
	if (info->charge_gpio >= 0 && gpio_is_valid(info->charge_gpio)) {
		free_irq(gpio_to_irq(info->charge_gpio), charger);
		gpio_free(info->charge_gpio);
	}

	kfree(charger);

	return 0;
}

#ifdef CONFIG_PM
static int z2_batt_suspend(struct i2c_client *client, pm_message_t state)
{
	struct z2_charger *charger = i2c_get_clientdata(client);

	flush_work_sync(&charger->bat_work);
	return 0;
}

static int z2_batt_resume(struct i2c_client *client)
{
	struct z2_charger *charger = i2c_get_clientdata(client);

	schedule_work(&charger->bat_work);
	return 0;
}
#else
#define z2_batt_suspend NULL
#define z2_batt_resume NULL
#endif

static const struct i2c_device_id z2_batt_id[] = {
	{ "aer915", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, z2_batt_id);

static struct i2c_driver z2_batt_driver = {
	.driver	= {
		.name	= "z2-battery",
		.owner	= THIS_MODULE,
	},
	.probe		= z2_batt_probe,
	.remove		= z2_batt_remove,
	.suspend	= z2_batt_suspend,
	.resume		= z2_batt_resume,
	.id_table	= z2_batt_id,
};

static int __init z2_batt_init(void)
{
	return i2c_add_driver(&z2_batt_driver);
}

static void __exit z2_batt_exit(void)
{
	i2c_del_driver(&z2_batt_driver);
}

module_init(z2_batt_init);
module_exit(z2_batt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Peter Edwards <sweetlilmre@gmail.com>");
MODULE_DESCRIPTION("Zipit Z2 battery driver");
