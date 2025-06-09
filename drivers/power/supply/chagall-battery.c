// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/array_size.h>
#include <linux/delay.h>
#include <linux/devm-helpers.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

#define CHAGALL_REG_LED_AMBER				0x60
#define CHAGALL_REG_LED_WHITE				0x70
#define CHAGALL_REG_BATTERY_TEMPERATURE			0xa2
#define CHAGALL_REG_BATTERY_VOLTAGE			0xa4
#define CHAGALL_REG_BATTERY_CURRENT			0xa6
#define CHAGALL_REG_BATTERY_CAPACITY			0xa8
#define CHAGALL_REG_BATTERY_CHARGING_CURRENT		0xaa
#define CHAGALL_REG_BATTERY_CHARGING_VOLTAGE		0xac
#define CHAGALL_REG_BATTERY_STATUS			0xae
#define   BATTERY_DISCHARGING				BIT(6)
#define   BATTERY_FULL_CHARGED				BIT(5)
#define   BATTERY_FULL_DISCHARGED			BIT(4)
#define CHAGALL_REG_BATTERY_REMAIN_CAPACITY		0xb0
#define CHAGALL_REG_BATTERY_FULL_CAPACITY		0xb2
#define CHAGALL_REG_MAX_COUNT				0xb4

#define CHAGALL_BATTERY_DATA_REFRESH			5000
#define TEMP_CELSIUS_OFFSET				2731

static const struct regmap_config chagall_battery_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = CHAGALL_REG_MAX_COUNT,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

struct chagall_battery_data {
	struct regmap *regmap;
	struct led_classdev amber_led;
	struct led_classdev white_led;
	struct power_supply *battery;
	struct delayed_work poll_work;
	u16 last_state;
};

static void chagall_led_set_brightness_amber(struct led_classdev *led,
					     enum led_brightness brightness)
{
	struct chagall_battery_data *cg =
		container_of(led, struct chagall_battery_data, amber_led);

	regmap_write(cg->regmap, CHAGALL_REG_LED_AMBER, brightness);
}

static void chagall_led_set_brightness_white(struct led_classdev *led,
					     enum led_brightness brightness)
{
	struct chagall_battery_data *cg =
		container_of(led, struct chagall_battery_data, white_led);

	regmap_write(cg->regmap, CHAGALL_REG_LED_WHITE, brightness);
}

static const enum power_supply_property chagall_battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
};

static const unsigned int chagall_battery_prop_offs[] = {
	[POWER_SUPPLY_PROP_STATUS] = CHAGALL_REG_BATTERY_STATUS,
	[POWER_SUPPLY_PROP_VOLTAGE_NOW] = CHAGALL_REG_BATTERY_VOLTAGE,
	[POWER_SUPPLY_PROP_VOLTAGE_MAX] = CHAGALL_REG_BATTERY_CHARGING_VOLTAGE,
	[POWER_SUPPLY_PROP_CURRENT_NOW] = CHAGALL_REG_BATTERY_CURRENT,
	[POWER_SUPPLY_PROP_CURRENT_MAX] = CHAGALL_REG_BATTERY_CHARGING_CURRENT,
	[POWER_SUPPLY_PROP_CAPACITY] = CHAGALL_REG_BATTERY_CAPACITY,
	[POWER_SUPPLY_PROP_TEMP] = CHAGALL_REG_BATTERY_TEMPERATURE,
	[POWER_SUPPLY_PROP_CHARGE_FULL] = CHAGALL_REG_BATTERY_FULL_CAPACITY,
	[POWER_SUPPLY_PROP_CHARGE_NOW] = CHAGALL_REG_BATTERY_REMAIN_CAPACITY,
};

static int chagall_battery_get_value(struct chagall_battery_data *cg,
				     enum power_supply_property psp, u32 *val)
{
	if (psp >= ARRAY_SIZE(chagall_battery_prop_offs))
		return -EINVAL;
	if (!chagall_battery_prop_offs[psp])
		return -EINVAL;

	/* Battery data is stored in 2 consecutive registers with little-endian */
	return regmap_bulk_read(cg->regmap, chagall_battery_prop_offs[psp], val, 2);
}

static int chagall_battery_get_status(u32 status_reg)
{
	if (status_reg & BATTERY_FULL_CHARGED)
		return POWER_SUPPLY_STATUS_FULL;
	else if (status_reg & BATTERY_DISCHARGING)
		return POWER_SUPPLY_STATUS_DISCHARGING;
	else
		return POWER_SUPPLY_STATUS_CHARGING;
}

static int chagall_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct chagall_battery_data *cg = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;

	default:
		ret = chagall_battery_get_value(cg, psp, &val->intval);
		if (ret)
			return ret;

		switch (psp) {
		case POWER_SUPPLY_PROP_TEMP:
			val->intval -= TEMP_CELSIUS_OFFSET;
			break;

		case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		case POWER_SUPPLY_PROP_CURRENT_MAX:
		case POWER_SUPPLY_PROP_CURRENT_NOW:
		case POWER_SUPPLY_PROP_CHARGE_FULL:
		case POWER_SUPPLY_PROP_CHARGE_NOW:
			val->intval *= 1000;
			break;

		case POWER_SUPPLY_PROP_STATUS:
			val->intval = chagall_battery_get_status(val->intval);
			break;

		default:
			break;
		}

		break;
	}

	return 0;
}

static void chagall_battery_poll_work(struct work_struct *work)
{
	struct chagall_battery_data *cg =
		container_of(work, struct chagall_battery_data, poll_work.work);
	u32 state;
	int ret;

	ret = chagall_battery_get_value(cg, POWER_SUPPLY_PROP_STATUS, &state);
	if (ret)
		return;

	state = chagall_battery_get_status(state);

	if (cg->last_state != state) {
		cg->last_state = state;
		power_supply_changed(cg->battery);
	}

	/* continuously send uevent notification */
	schedule_delayed_work(&cg->poll_work,
			      msecs_to_jiffies(CHAGALL_BATTERY_DATA_REFRESH));
}

static const struct power_supply_desc chagall_battery_desc = {
	.name = "chagall-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = chagall_battery_properties,
	.num_properties = ARRAY_SIZE(chagall_battery_properties),
	.get_property = chagall_battery_get_property,
	.external_power_changed = power_supply_changed,
};

static int chagall_battery_probe(struct i2c_client *client)
{
	struct chagall_battery_data *cg;
	struct device *dev = &client->dev;
	struct power_supply_config cfg = { };
	int ret;

	cg = devm_kzalloc(dev, sizeof(*cg), GFP_KERNEL);
	if (!cg)
		return -ENOMEM;

	cfg.drv_data = cg;
	cfg.fwnode = dev_fwnode(dev);

	i2c_set_clientdata(client, cg);

	cg->regmap = devm_regmap_init_i2c(client, &chagall_battery_regmap_config);
	if (IS_ERR(cg->regmap))
		return dev_err_probe(dev, PTR_ERR(cg->regmap), "cannot allocate regmap\n");

	cg->last_state = POWER_SUPPLY_STATUS_UNKNOWN;
	cg->battery = devm_power_supply_register(dev, &chagall_battery_desc, &cfg);
	if (IS_ERR(cg->battery))
		return dev_err_probe(dev, PTR_ERR(cg->battery),
				     "failed to register power supply\n");

	cg->amber_led.name = "power::amber";
	cg->amber_led.max_brightness = 1;
	cg->amber_led.flags = LED_CORE_SUSPENDRESUME;
	cg->amber_led.brightness_set = chagall_led_set_brightness_amber;
	cg->amber_led.default_trigger = "chagall-battery-charging";

	ret = devm_led_classdev_register(dev, &cg->amber_led);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register amber LED\n");

	cg->white_led.name = "power::white";
	cg->white_led.max_brightness = 1;
	cg->white_led.flags = LED_CORE_SUSPENDRESUME;
	cg->white_led.brightness_set = chagall_led_set_brightness_white;
	cg->white_led.default_trigger = "chagall-battery-full";

	ret = devm_led_classdev_register(dev, &cg->white_led);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register white LED\n");

	led_set_brightness(&cg->amber_led, LED_OFF);
	led_set_brightness(&cg->white_led, LED_OFF);

	ret = devm_delayed_work_autocancel(dev, &cg->poll_work, chagall_battery_poll_work);
	if (ret)
		return ret;

	schedule_delayed_work(&cg->poll_work, msecs_to_jiffies(CHAGALL_BATTERY_DATA_REFRESH));

	return 0;
}

static int __maybe_unused chagall_battery_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct chagall_battery_data *cg = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&cg->poll_work);

	return 0;
}

static int __maybe_unused chagall_battery_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct chagall_battery_data *cg = i2c_get_clientdata(client);

	schedule_delayed_work(&cg->poll_work, msecs_to_jiffies(CHAGALL_BATTERY_DATA_REFRESH));

	return 0;
}

static SIMPLE_DEV_PM_OPS(chagall_battery_pm_ops,
			 chagall_battery_suspend, chagall_battery_resume);

static const struct of_device_id chagall_of_match[] = {
	{ .compatible = "pegatron,chagall-ec" },
	{ }
};
MODULE_DEVICE_TABLE(of, chagall_of_match);

static struct i2c_driver chagall_battery_driver = {
	.driver = {
		.name = "chagall-battery",
		.pm = &chagall_battery_pm_ops,
		.of_match_table = chagall_of_match,
	},
	.probe = chagall_battery_probe,
};
module_i2c_driver(chagall_battery_driver);

MODULE_AUTHOR("Svyatoslav Ryhel <clamor95@gmail.com>");
MODULE_DESCRIPTION("Pegatron Chagall fuel gauge driver");
MODULE_LICENSE("GPL");
