// SPDX-License-Identifier: GPL-2.0+

#include <linux/devm-helpers.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/power_supply.h>
#include <linux/types.h>

/* Register Addresses (B=byte; W=word; S=string) */
#define REGB_STATUS			0x02
#define REGW_VOLTAGE_NOW		0x20
#define REGW_CURRENT_NOW		0x24
#define REGW_CAPACITY			0x28
#define REGW_CHARGE_NOW			0x2a
#define REGW_CHARGE_FULL		0x2c
#define REGW_CYCLE_COUNT		0x3a
#define REGW_CHARGE_FULL_DESIGN		0x3c
#define REGW_VOLTAGE_MAX_DESIGN		0x3e
#define REGW_SERIAL_NUMBER		0x44
#define REGS_MANUFACTURER		0x46
#define REGS_MODEL_NAME			0x52
#define REGS_TECHNOLOGY			0x5a
#define REGB_ONLINE			0x67

struct srt_ec_device {
	struct i2c_client *client;

	struct power_supply *bat;
	struct power_supply *psy;

	struct gpio_desc *enable_gpiod;
	struct delayed_work poll_work;

	unsigned int technology;
	unsigned int capacity;

	const char *serial;
	char manufacturer[13];
	char model_name[10];
};

static const enum power_supply_property srt_bat_power_supply_props[] = {
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static const enum power_supply_property srt_psy_power_supply_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
};

static int srt_bat_get_value(struct i2c_client *client, int reg, int *val)
{
	int ret;

	switch (reg) {
	case REGW_CHARGE_NOW:
	case REGW_CHARGE_FULL_DESIGN:
	case REGW_CHARGE_FULL:
	case REGW_VOLTAGE_MAX_DESIGN:
	case REGW_VOLTAGE_NOW:
		ret = i2c_smbus_read_word_data(client, reg);
		if (ret < 0)
			return ret;

		*val = ret * 1000;
		break;

	case REGW_CURRENT_NOW:
		ret = i2c_smbus_read_word_data(client, reg);
		if (ret < 0)
			return ret;

		*val = (s16)ret * 1000;
		break;

	case REGW_CAPACITY:
	case REGW_CYCLE_COUNT:
		ret = i2c_smbus_read_word_data(client, reg);
		if (ret < 0)
			return ret;

		*val = ret;
		break;

	case REGB_STATUS:
		ret = i2c_smbus_read_byte_data(client, reg);
		if (ret < 0)
			return ret;

		if (ret & BIT(0))
			*val = POWER_SUPPLY_STATUS_CHARGING;
		else
			*val =  POWER_SUPPLY_STATUS_DISCHARGING;
		break;

	case REGB_ONLINE:
		ret = i2c_smbus_read_byte_data(client, reg);
		if (ret < 0)
			return ret;

		*val = (ret & BIT(1)) >> 1;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int srt_bat_power_supply_get_property(struct power_supply *psy,
					     enum power_supply_property psp,
					     union power_supply_propval *val)
{
	struct srt_ec_device *srt = power_supply_get_drvdata(psy);
	struct i2c_client *client = srt->client;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = srt->manufacturer;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = srt->model_name;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = srt->serial;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = srt_bat_get_value(client, REGW_CAPACITY, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = srt_bat_get_value(client, REGW_CHARGE_NOW, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = srt_bat_get_value(client, REGW_CHARGE_FULL, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = srt_bat_get_value(client, REGW_CHARGE_FULL_DESIGN,
					&val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = srt_bat_get_value(client, REGW_CURRENT_NOW, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		ret = srt_bat_get_value(client, REGW_CYCLE_COUNT, &val->intval);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		ret = srt_bat_get_value(client, REGB_ONLINE, &val->intval);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (srt->capacity < 100)
			ret = srt_bat_get_value(client, REGB_STATUS, &val->intval);
		else
			val->intval = POWER_SUPPLY_STATUS_FULL;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = srt->technology;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		ret = srt_bat_get_value(client, REGW_VOLTAGE_MAX_DESIGN,
					&val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = srt_bat_get_value(client, REGW_VOLTAGE_NOW, &val->intval);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int srt_psy_power_supply_get_property(struct power_supply *psy,
					     enum power_supply_property psp,
					     union power_supply_propval *val)
{
	struct srt_ec_device *srt = power_supply_get_drvdata(psy);
	struct i2c_client *client = srt->client;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_PRESENT:
		ret = i2c_smbus_read_byte_data(client, REGB_ONLINE);
		if (ret < 0)
			return ret;

		val->intval = ret & BIT(0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void srt_bat_poll_work(struct work_struct *work)
{
	struct srt_ec_device *srt =
		container_of(work, struct srt_ec_device, poll_work.work);
	int ret, capacity;

	ret = srt_bat_get_value(srt->client, REGW_CAPACITY, &capacity);
	if (!ret && capacity != srt->capacity) {
		srt->capacity = capacity;
		power_supply_changed(srt->bat);
	}

	/* continuously send uevent notification */
	schedule_delayed_work(&srt->poll_work, 30 * HZ);
}

static irqreturn_t srt_psy_detect_irq(int irq, void *dev_id)
{
	struct srt_ec_device *srt = dev_id;

	power_supply_changed(srt->psy);

	return IRQ_HANDLED;
}

static const struct power_supply_desc srt_bat_power_supply_desc = {
	.name = "surface-rt-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = srt_bat_power_supply_props,
	.num_properties = ARRAY_SIZE(srt_bat_power_supply_props),
	.get_property = srt_bat_power_supply_get_property,
	.external_power_changed = power_supply_changed,
};

static const struct power_supply_desc srt_psy_power_supply_desc = {
	.name = "surface-rt-ac-adapter",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = srt_psy_power_supply_props,
	.num_properties = ARRAY_SIZE(srt_psy_power_supply_props),
	.get_property = srt_psy_power_supply_get_property,
};

static char *battery_supplied_to[] = { "surface-rt-battery" };

static int srt_ec_probe(struct i2c_client *client)
{
	struct power_supply_config bat_cfg = {};
	struct power_supply_config psy_cfg = {};
	struct device *dev = &client->dev;
	struct srt_ec_device *srt;
	char str_buf[4];
	int ret;

	srt = devm_kzalloc(dev, sizeof(*srt), GFP_KERNEL);
	if (!srt)
		return -ENOMEM;

	i2c_set_clientdata(client, srt);
	srt->client = client;

	srt->enable_gpiod = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(srt->enable_gpiod))
		return dev_err_probe(dev, PTR_ERR(srt->enable_gpiod),
				     "failed to get enable gpio\n");

	/* wait till EC is ready */
	usleep_range(1000, 1500);

	ret = i2c_smbus_read_word_data(client, REGW_SERIAL_NUMBER);
	if (ret < 0)
		return ret;

	srt->serial = devm_kasprintf(dev, GFP_KERNEL, "%04x", ret);
	if (!srt->serial)
		return -ENOMEM;

	ret = i2c_smbus_read_i2c_block_data(client, REGS_MANUFACTURER,
					    sizeof(srt->manufacturer) - 1,
					    srt->manufacturer);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_i2c_block_data(client, REGS_MODEL_NAME,
					    sizeof(srt->model_name) - 1,
					    srt->model_name);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_i2c_block_data(client, REGS_TECHNOLOGY,
					    sizeof(str_buf), str_buf);
	if (ret < 0)
		return ret;

	if (!strncmp(str_buf, "LION", 4))
		srt->technology = POWER_SUPPLY_TECHNOLOGY_LION;
	else
		srt->technology = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;

	bat_cfg.drv_data = srt;
	bat_cfg.fwnode = dev_fwnode(dev);

	srt->bat = devm_power_supply_register(dev, &srt_bat_power_supply_desc,
					      &bat_cfg);
	if (IS_ERR(srt->bat))
		return dev_err_probe(dev, PTR_ERR(srt->bat),
				     "failed to register battery power supply\n");

	psy_cfg.drv_data = srt;
	psy_cfg.fwnode = dev_fwnode(dev);
	psy_cfg.supplied_to = battery_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(battery_supplied_to);

	srt->psy = devm_power_supply_register(dev, &srt_psy_power_supply_desc,
					      &psy_cfg);
	if (IS_ERR(srt->psy))
		return dev_err_probe(dev, PTR_ERR(srt->psy),
				     "failed to register AC power supply\n");

	ret = devm_request_threaded_irq(dev, client->irq, NULL, srt_psy_detect_irq,
					IRQF_ONESHOT, client->name, srt);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to request interrupt\n");

	ret = devm_delayed_work_autocancel(dev, &srt->poll_work, srt_bat_poll_work);
	if (ret < 0)
		return ret;

	schedule_delayed_work(&srt->poll_work, HZ);

	return 0;
}

static int srt_ec_suspend(struct device *dev)
{
	struct srt_ec_device *srt = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&srt->poll_work);

	return 0;
}

static int srt_ec_resume(struct device *dev)
{
	struct srt_ec_device *srt = dev_get_drvdata(dev);

	schedule_delayed_work(&srt->poll_work, HZ);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(srt_ec_pm_ops, srt_ec_suspend, srt_ec_resume);

static const struct of_device_id srt_ec_of_match[] = {
	{ .compatible = "microsoft,surface-rt-ec" },
	{ }
};
MODULE_DEVICE_TABLE(of, srt_ec_of_match);

static struct i2c_driver srt_ec_driver = {
	.driver = {
		.name = "surface-rt-ec",
		.of_match_table = srt_ec_of_match,
		.pm = &srt_ec_pm_ops,
	},
	.probe = srt_ec_probe,
};
module_i2c_driver(srt_ec_driver);

MODULE_AUTHOR("Jonas Schwöbel <jonasschwoebel@yahoo.de>");
MODULE_DESCRIPTION("Surface RT Embedded Controller driver");
MODULE_LICENSE("GPL");
