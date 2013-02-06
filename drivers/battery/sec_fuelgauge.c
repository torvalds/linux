/*
 *  sec_fuelgauge.c
 *  Samsung Mobile Fuel Gauge Driver
 *
 *  Copyright (C) 2012 Samsung Electronics
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define DEBUG

#include <linux/battery/sec_fuelgauge.h>

/* capacity is  0.1% unit */
static void sec_fg_get_scaled_capacity(
				struct sec_fuelgauge_info *fuelgauge,
				union power_supply_propval *val)
{
	val->intval = (val->intval < fuelgauge->pdata->capacity_min) ?
		0 : ((val->intval - fuelgauge->pdata->capacity_min) * 1000 /
		(fuelgauge->pdata->capacity_max -
		fuelgauge->pdata->capacity_min));

	dev_dbg(&fuelgauge->client->dev,
		"%s: scaled capacity (%d.%d)\n",
		__func__, val->intval/10, val->intval%10);
}

/* capacity is integer */
static void sec_fg_get_atomic_capacity(
				struct sec_fuelgauge_info *fuelgauge,
				union power_supply_propval *val)
{
	if (fuelgauge->capacity_old < val->intval)
		val->intval = fuelgauge->capacity_old + 1;
	else if (fuelgauge->capacity_old > val->intval)
		val->intval = fuelgauge->capacity_old - 1;

	/* updated old capacity */
	fuelgauge->capacity_old = val->intval;
}

static int sec_fg_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct sec_fuelgauge_info *fuelgauge =
		container_of(psy, struct sec_fuelgauge_info, psy_fg);

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CURRENT_AVG:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_TEMP:
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		if (!sec_hal_fg_get_property(fuelgauge->client, psp, val))
			return -EINVAL;
		if (psp == POWER_SUPPLY_PROP_CAPACITY) {
			if (fuelgauge->pdata->capacity_calculation_type &
				SEC_FUELGAUGE_CAPACITY_TYPE_SCALE)
				sec_fg_get_scaled_capacity(fuelgauge, val);

			/* capacity should be between 0% and 100%
			 * (0.1% degree)
			 */
			if (val->intval > 1000)
				val->intval = 1000;
			if (val->intval < 0)
				val->intval = 0;

			/* get only integer part */
			val->intval /= 10;

			/* (Only for atomic capacity)
			 * In initial time, capacity_old is 0.
			 * and in resume from sleep,
			 * capacity_old is too different from actual soc.
			 * should update capacity_old
			 * by val->intval in booting or resume.
			 */
			if (fuelgauge->initial_update_of_soc) {
				/* updated old capacity */
				fuelgauge->capacity_old = val->intval;
				fuelgauge->initial_update_of_soc = false;
				break;
			}

			if (fuelgauge->pdata->capacity_calculation_type &
				SEC_FUELGAUGE_CAPACITY_TYPE_ATOMIC)
				sec_fg_get_atomic_capacity(fuelgauge, val);
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sec_fg_set_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    const union power_supply_propval *val)
{
	struct sec_fuelgauge_info *fuelgauge =
		container_of(psy, struct sec_fuelgauge_info, psy_fg);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (val->intval == POWER_SUPPLY_STATUS_FULL)
			sec_hal_fg_full_charged(fuelgauge->client);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		fuelgauge->cable_type = val->intval;
		if (val->intval == POWER_SUPPLY_TYPE_BATTERY)
			fuelgauge->is_charging = false;
		else
			fuelgauge->is_charging = true;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (val->intval == SEC_FUELGAUGE_CAPACITY_TYPE_RESET) {
			if (!sec_hal_fg_reset(fuelgauge->client))
				return -EINVAL;
			else
				break;
		}
	case POWER_SUPPLY_PROP_TEMP:
	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		if (!sec_hal_fg_set_property(fuelgauge->client, psp, val))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void sec_fg_isr_work(struct work_struct *work)
{
	struct sec_fuelgauge_info *fuelgauge =
		container_of(work, struct sec_fuelgauge_info, isr_work.work);
	bool is_fuel_alerted_now;

	is_fuel_alerted_now =
		sec_hal_fg_is_fuelalerted(fuelgauge->client);

	dev_info(&fuelgauge->client->dev,
		"%s: Fuel-alert %salerted!\n",
		__func__, is_fuel_alerted_now ? "" : "NOT ");

	if (is_fuel_alerted_now)
		wake_lock(&fuelgauge->fuel_alert_wake_lock);
	else
		wake_unlock(&fuelgauge->fuel_alert_wake_lock);

	if (!(fuelgauge->pdata->repeated_fuelalert) &&
		(fuelgauge->is_fuel_alerted == is_fuel_alerted_now)) {
		dev_dbg(&fuelgauge->client->dev,
			"%s: Fuel-alert Repeated (%d)\n",
			__func__, fuelgauge->is_fuel_alerted);
		return;
	}

	/* process for fuel gauge chip */
	sec_hal_fg_fuelalert_process(fuelgauge, is_fuel_alerted_now);

	/* process for others */
	fuelgauge->pdata->fuelalert_process(is_fuel_alerted_now);

	fuelgauge->is_fuel_alerted = is_fuel_alerted_now;
}

static irqreturn_t sec_fg_irq_thread(int irq, void *irq_data)
{
	struct sec_fuelgauge_info *fuelgauge = irq_data;

	if (fuelgauge->pdata->fuel_alert_soc >= 0)
		schedule_delayed_work(&fuelgauge->isr_work, 0);

	return IRQ_HANDLED;
}

static int sec_fg_create_attrs(struct device *dev)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(sec_fg_attrs); i++) {
		rc = device_create_file(dev, &sec_fg_attrs[i]);
		if (rc)
			goto create_attrs_failed;
	}
	goto create_attrs_succeed;

create_attrs_failed:
	dev_err(dev, "%s: failed (%d)\n", __func__, rc);
	while (i--)
		device_remove_file(dev, &sec_fg_attrs[i]);
create_attrs_succeed:
	return rc;
}

ssize_t sec_fg_show_attrs(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	const ptrdiff_t offset = attr - sec_fg_attrs;
	int i = 0;

	switch (offset) {
	case FG_REG:
	case FG_DATA:
	case FG_REGS:
		i = sec_hal_fg_show_attrs(dev, offset, buf);
		break;
	default:
		i = -EINVAL;
		break;
	}

	return i;
}

ssize_t sec_fg_store_attrs(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	const ptrdiff_t offset = attr - sec_fg_attrs;
	int ret = 0;

	switch (offset) {
	case FG_REG:
	case FG_DATA:
		ret = sec_hal_fg_store_attrs(dev, offset, buf, count);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int __devinit sec_fuelgauge_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct sec_fuelgauge_info *fuelgauge;
	int ret = 0;

	dev_dbg(&client->dev,
		"%s: SEC Fuelgauge Driver Loading\n", __func__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	fuelgauge = kzalloc(sizeof(*fuelgauge), GFP_KERNEL);
	if (!fuelgauge)
		return -ENOMEM;

	mutex_init(&fuelgauge->fg_lock);

	fuelgauge->client = client;
	fuelgauge->pdata = client->dev.platform_data;

	i2c_set_clientdata(client, fuelgauge);

	fuelgauge->psy_fg.name		= "sec-fuelgauge";
	fuelgauge->psy_fg.type		= POWER_SUPPLY_TYPE_BATTERY;
	fuelgauge->psy_fg.get_property	= sec_fg_get_property;
	fuelgauge->psy_fg.set_property	= sec_fg_set_property;
	fuelgauge->psy_fg.properties	= sec_fuelgauge_props;
	fuelgauge->psy_fg.num_properties =
		ARRAY_SIZE(sec_fuelgauge_props);

	if (!fuelgauge->pdata->fg_gpio_init()) {
		dev_err(&client->dev,
			"%s: Failed to Initialize GPIO\n", __func__);
		goto err_free;
	}

	if (!sec_hal_fg_init(fuelgauge->client)) {
		dev_err(&client->dev,
			"%s: Failed to Initialize Fuelgauge\n", __func__);
		goto err_free;
	}

	ret = power_supply_register(&client->dev, &fuelgauge->psy_fg);
	if (ret) {
		dev_err(&client->dev,
			"%s: Failed to Register psy_fg\n", __func__);
		goto err_free;
	}

	if (fuelgauge->pdata->fg_irq) {
		ret = request_threaded_irq(fuelgauge->pdata->fg_irq,
				NULL, sec_fg_irq_thread,
				fuelgauge->pdata->fg_irq_attr,
				"fuelgauge-irq", fuelgauge);
		if (ret) {
			dev_err(&client->dev,
				"%s: Failed to Reqeust IRQ\n", __func__);
			return ret;
		}

		ret = enable_irq_wake(fuelgauge->pdata->fg_irq);
		if (ret < 0)
			dev_err(&client->dev,
				"%s: Failed to Enable Wakeup Source(%d)\n",
				__func__, ret);

		INIT_DELAYED_WORK_DEFERRABLE(
			&fuelgauge->isr_work, sec_fg_isr_work);
	}

	fuelgauge->is_fuel_alerted = false;
	if (fuelgauge->pdata->fuel_alert_soc >= 0) {
		if (sec_hal_fg_fuelalert_init(fuelgauge->client,
			fuelgauge->pdata->fuel_alert_soc))
			wake_lock_init(&fuelgauge->fuel_alert_wake_lock,
				WAKE_LOCK_SUSPEND, "fuel_alerted");
		else {
			dev_err(&client->dev,
				"%s: Failed to Initialize Fuel-alert\n",
				__func__);
			goto err_irq;
		}
	}

	fuelgauge->initial_update_of_soc = true;

	ret = sec_fg_create_attrs(fuelgauge->psy_fg.dev);
	if (ret) {
		dev_err(&client->dev,
			"%s : Failed to create_attrs\n", __func__);
		goto err_irq;
	}

	dev_dbg(&client->dev,
		"%s: SEC Fuelgauge Driver Loaded\n", __func__);
	return 0;

err_irq:
	wake_lock_destroy(&fuelgauge->fuel_alert_wake_lock);
err_free:
	kfree(fuelgauge);

	return ret;
}

static int __devexit sec_fuelgauge_remove(
						struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);

	if (fuelgauge->pdata->fuel_alert_soc >= 0)
		wake_lock_destroy(&fuelgauge->fuel_alert_wake_lock);

	return 0;
}

static int sec_fuelgauge_suspend(
				struct i2c_client *client, pm_message_t state)
{
	if (!sec_hal_fg_suspend(client))
		dev_err(&client->dev,
			"%s: Failed to Suspend Fuelgauge\n", __func__);

	return 0;
}

static int sec_fuelgauge_resume(struct i2c_client *client)
{
	struct sec_fuelgauge_info *fuelgauge = i2c_get_clientdata(client);

	if (!sec_hal_fg_resume(client))
		dev_err(&client->dev,
			"%s: Failed to Resume Fuelgauge\n", __func__);

	fuelgauge->initial_update_of_soc = true;

	return 0;
}

static void sec_fuelgauge_shutdown(struct i2c_client *client)
{
}

static const struct i2c_device_id sec_fuelgauge_id[] = {
	{"sec-fuelgauge", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sec_fuelgauge_id);

static struct i2c_driver sec_fuelgauge_driver = {
	.driver = {
		   .name = "sec-fuelgauge",
		   },
	.probe	= sec_fuelgauge_probe,
	.remove	= __devexit_p(sec_fuelgauge_remove),
	.suspend    = sec_fuelgauge_suspend,
	.resume		= sec_fuelgauge_resume,
	.shutdown   = sec_fuelgauge_shutdown,
	.id_table   = sec_fuelgauge_id,
};

static int __init sec_fuelgauge_init(void)
{
	return i2c_add_driver(&sec_fuelgauge_driver);
}

static void __exit sec_fuelgauge_exit(void)
{
	i2c_del_driver(&sec_fuelgauge_driver);
}

module_init(sec_fuelgauge_init);
module_exit(sec_fuelgauge_exit);

MODULE_DESCRIPTION("Samsung Fuel Gauge Driver");
MODULE_AUTHOR("Samsung Electronics");
MODULE_LICENSE("GPL");
