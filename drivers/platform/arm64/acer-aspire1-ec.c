// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024, Nikita Travkin <nikita@trvn.ru> */

#include <asm-generic/unaligned.h>
#include <drm/drm_bridge.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/usb/typec_mux.h>
#include <linux/workqueue_types.h>

#define MILLI_TO_MICRO			1000

#define ASPIRE_EC_EVENT			0x05

#define ASPIRE_EC_EVENT_WATCHDOG	0x20
#define ASPIRE_EC_EVENT_KBD_BKL_ON	0x57
#define ASPIRE_EC_EVENT_KBD_BKL_OFF	0x58
#define ASPIRE_EC_EVENT_LID_CLOSE	0x9b
#define ASPIRE_EC_EVENT_LID_OPEN	0x9c
#define ASPIRE_EC_EVENT_BKL_UNBLANKED	0x9d
#define ASPIRE_EC_EVENT_BKL_BLANKED	0x9e
#define ASPIRE_EC_EVENT_FG_INF_CHG	0x85
#define ASPIRE_EC_EVENT_FG_STA_CHG	0xc6
#define ASPIRE_EC_EVENT_HPD_DIS		0xa3
#define ASPIRE_EC_EVENT_HPD_CON		0xa4

#define ASPIRE_EC_FG_DYNAMIC		0x07
#define ASPIRE_EC_FG_STATIC		0x08

#define ASPIRE_EC_FG_FLAG_PRESENT	BIT(0)
#define ASPIRE_EC_FG_FLAG_FULL		BIT(1)
#define ASPIRE_EC_FG_FLAG_DISCHARGING	BIT(2)
#define ASPIRE_EC_FG_FLAG_CHARGING	BIT(3)

#define ASPIRE_EC_RAM_READ		0x20
#define ASPIRE_EC_RAM_WRITE		0x21

#define ASPIRE_EC_RAM_WATCHDOG		0x19
#define ASPIRE_EC_WATCHDOG_BIT		BIT(6)

#define ASPIRE_EC_RAM_KBD_MODE		0x43

#define ASPIRE_EC_RAM_KBD_FN_EN		BIT(0)
#define ASPIRE_EC_RAM_KBD_MEDIA_ON_TOP	BIT(5)
#define ASPIRE_EC_RAM_KBD_ALWAYS_SET	BIT(6)
#define ASPIRE_EC_RAM_KBD_NUM_LAYER_EN	BIT(7)

#define ASPIRE_EC_RAM_KBD_MODE_2	0x60

#define ASPIRE_EC_RAM_KBD_MEDIA_NOTIFY	BIT(3)

#define ASPIRE_EC_RAM_HPD_STATUS	0xf4
#define ASPIRE_EC_HPD_CONNECTED		0x03

#define ASPIRE_EC_RAM_LID_STATUS	0x4c
#define ASPIRE_EC_LID_OPEN		BIT(6)

#define ASPIRE_EC_RAM_ADP		0x40
#define ASPIRE_EC_AC_STATUS		BIT(0)

struct aspire_ec {
	struct i2c_client *client;
	struct power_supply *bat_psy;
	struct power_supply *adp_psy;
	struct input_dev *idev;

	bool bridge_configured;
	struct drm_bridge bridge;
	struct work_struct work;
};

static int aspire_ec_ram_read(struct i2c_client *client, u8 off, u8 *data, u8 data_len)
{
	i2c_smbus_write_byte_data(client, ASPIRE_EC_RAM_READ, off);
	i2c_smbus_read_i2c_block_data(client, ASPIRE_EC_RAM_READ, data_len, data);
	return 0;
}

static int aspire_ec_ram_write(struct i2c_client *client, u8 off, u8 data)
{
	u8 tmp[2] = {off, data};

	i2c_smbus_write_i2c_block_data(client, ASPIRE_EC_RAM_WRITE, sizeof(tmp), tmp);
	return 0;
}

static irqreturn_t aspire_ec_irq_handler(int irq, void *data)
{
	struct aspire_ec *ec = data;
	int id;
	u8 tmp;

	/*
	 * The original ACPI firmware actually has a small sleep in the handler.
	 *
	 * It seems like in most cases it's not needed but when the device
	 * just exits suspend, our i2c driver has a brief time where data
	 * transfer is not possible yet. So this delay allows us to suppress
	 * quite a bunch of spurious error messages in dmesg. Thus it's kept.
	 */
	usleep_range(15000, 30000);

	id = i2c_smbus_read_byte_data(ec->client, ASPIRE_EC_EVENT);
	if (id < 0) {
		dev_err(&ec->client->dev, "Failed to read event id: %pe\n", ERR_PTR(id));
		return IRQ_HANDLED;
	}

	switch (id) {
	case 0x0: /* No event */
		break;

	case ASPIRE_EC_EVENT_WATCHDOG:
		/*
		 * Here acpi responds to the event and clears some bit.
		 * Notify (\_SB.I2C3.BAT1, 0x81) // Information Change
		 * Notify (\_SB.I2C3.ADP1, 0x80) // Status Change
		 */
		aspire_ec_ram_read(ec->client, ASPIRE_EC_RAM_WATCHDOG, &tmp, sizeof(tmp));
		tmp &= ~ASPIRE_EC_WATCHDOG_BIT;
		aspire_ec_ram_write(ec->client, ASPIRE_EC_RAM_WATCHDOG, tmp);
		break;

	case ASPIRE_EC_EVENT_LID_CLOSE:
		/* Notify (\_SB.LID0, 0x80) // Status Change */
		input_report_switch(ec->idev, SW_LID, 1);
		input_sync(ec->idev);
		break;

	case ASPIRE_EC_EVENT_LID_OPEN:
		/* Notify (\_SB.LID0, 0x80) // Status Change */
		input_report_switch(ec->idev, SW_LID, 0);
		input_sync(ec->idev);
		break;

	case ASPIRE_EC_EVENT_FG_INF_CHG:
		/* Notify (\_SB.I2C3.BAT1, 0x81) // Information Change */
		fallthrough;
	case ASPIRE_EC_EVENT_FG_STA_CHG:
		/* Notify (\_SB.I2C3.BAT1, 0x80) // Status Change */
		power_supply_changed(ec->bat_psy);
		power_supply_changed(ec->adp_psy);
		break;

	case ASPIRE_EC_EVENT_HPD_DIS:
		if (ec->bridge_configured)
			drm_bridge_hpd_notify(&ec->bridge, connector_status_disconnected);
		break;

	case ASPIRE_EC_EVENT_HPD_CON:
		if (ec->bridge_configured)
			drm_bridge_hpd_notify(&ec->bridge, connector_status_connected);
		break;

	case ASPIRE_EC_EVENT_BKL_BLANKED:
	case ASPIRE_EC_EVENT_BKL_UNBLANKED:
		/* Display backlight blanked on FN+F6. No action needed. */
		break;

	case ASPIRE_EC_EVENT_KBD_BKL_ON:
	case ASPIRE_EC_EVENT_KBD_BKL_OFF:
		/*
		 * There is a keyboard backlight connector on Aspire 1 that is
		 * controlled by FN+F8. There is no kb backlight on the device though.
		 * Seems like this is used on other devices like Acer Spin 7.
		 * No action needed.
		 */
		break;

	default:
		dev_warn(&ec->client->dev, "Unknown event id=0x%x\n", id);
	}

	return IRQ_HANDLED;
}

/*
 * Power supply.
 */

struct aspire_ec_bat_psy_static_data {
	u8 unk1;
	u8 flags;
	__le16 unk2;
	__le16 voltage_design;
	__le16 capacity_full;
	__le16 unk3;
	__le16 serial;
	u8 model_id;
	u8 vendor_id;
} __packed;

static const char * const aspire_ec_bat_psy_battery_model[] = {
	"AP18C4K",
	"AP18C8K",
	"AP19B8K",
	"AP16M4J",
	"AP16M5J",
};

static const char * const aspire_ec_bat_psy_battery_vendor[] = {
	"SANYO",
	"SONY",
	"PANASONIC",
	"SAMSUNG",
	"SIMPLO",
	"MOTOROLA",
	"CELXPERT",
	"LGC",
	"GETAC",
	"MURATA",
};

struct aspire_ec_bat_psy_dynamic_data {
	u8 unk1;
	u8 flags;
	u8 unk2;
	__le16 capacity_now;
	__le16 voltage_now;
	__le16 current_now;
	__le16 unk3;
	__le16 unk4;
} __packed;

static int aspire_ec_bat_psy_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct aspire_ec *ec = power_supply_get_drvdata(psy);
	struct aspire_ec_bat_psy_static_data sdat;
	struct aspire_ec_bat_psy_dynamic_data ddat;
	int str_index = 0;

	i2c_smbus_read_i2c_block_data(ec->client, ASPIRE_EC_FG_STATIC, sizeof(sdat), (u8 *)&sdat);
	i2c_smbus_read_i2c_block_data(ec->client, ASPIRE_EC_FG_DYNAMIC, sizeof(ddat), (u8 *)&ddat);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		if (ddat.flags & ASPIRE_EC_FG_FLAG_CHARGING)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (ddat.flags & ASPIRE_EC_FG_FLAG_DISCHARGING)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (ddat.flags & ASPIRE_EC_FG_FLAG_FULL)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = get_unaligned_le16(&ddat.voltage_now) * MILLI_TO_MICRO;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = le16_to_cpu(sdat.voltage_design) * MILLI_TO_MICRO;
		break;

	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = get_unaligned_le16(&ddat.capacity_now) * MILLI_TO_MICRO;
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = le16_to_cpu(sdat.capacity_full) * MILLI_TO_MICRO;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = get_unaligned_le16(&ddat.capacity_now) * 100;
		val->intval /= le16_to_cpu(sdat.capacity_full);
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = (s16)get_unaligned_le16(&ddat.current_now) * MILLI_TO_MICRO;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = !!(ddat.flags & ASPIRE_EC_FG_FLAG_PRESENT);
		break;

	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		str_index = sdat.model_id - 1;

		if (str_index >= 0 && str_index < ARRAY_SIZE(aspire_ec_bat_psy_battery_model))
			val->strval = aspire_ec_bat_psy_battery_model[str_index];
		else
			val->strval = "Unknown";
		break;

	case POWER_SUPPLY_PROP_MANUFACTURER:
		str_index = sdat.vendor_id - 3; /* ACPI uses 3 as an offset here. */

		if (str_index >= 0 && str_index < ARRAY_SIZE(aspire_ec_bat_psy_battery_vendor))
			val->strval = aspire_ec_bat_psy_battery_vendor[str_index];
		else
			val->strval = "Unknown";
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static enum power_supply_property aspire_ec_bat_psy_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static const struct power_supply_desc aspire_ec_bat_psy_desc = {
	.name		= "aspire-ec-bat",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.get_property	= aspire_ec_bat_psy_get_property,
	.properties	= aspire_ec_bat_psy_props,
	.num_properties	= ARRAY_SIZE(aspire_ec_bat_psy_props),
};

static int aspire_ec_adp_psy_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct aspire_ec *ec = power_supply_get_drvdata(psy);
	u8 tmp;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		aspire_ec_ram_read(ec->client, ASPIRE_EC_RAM_ADP, &tmp, sizeof(tmp));
		val->intval = !!(tmp & ASPIRE_EC_AC_STATUS);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static enum power_supply_property aspire_ec_adp_psy_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static const struct power_supply_desc aspire_ec_adp_psy_desc = {
	.name		= "aspire-ec-adp",
	.type		= POWER_SUPPLY_TYPE_MAINS,
	.get_property	= aspire_ec_adp_psy_get_property,
	.properties	= aspire_ec_adp_psy_props,
	.num_properties	= ARRAY_SIZE(aspire_ec_adp_psy_props),
};

/*
 * USB-C DP Alt mode HPD.
 */

static int aspire_ec_bridge_attach(struct drm_bridge *bridge, enum drm_bridge_attach_flags flags)
{
	return flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR ? 0 : -EINVAL;
}

static void aspire_ec_bridge_update_hpd_work(struct work_struct *work)
{
	struct aspire_ec *ec = container_of(work, struct aspire_ec, work);
	u8 tmp;

	aspire_ec_ram_read(ec->client, ASPIRE_EC_RAM_HPD_STATUS, &tmp, sizeof(tmp));
	if (tmp == ASPIRE_EC_HPD_CONNECTED)
		drm_bridge_hpd_notify(&ec->bridge, connector_status_connected);
	else
		drm_bridge_hpd_notify(&ec->bridge, connector_status_disconnected);
}

static void aspire_ec_bridge_hpd_enable(struct drm_bridge *bridge)
{
	struct aspire_ec *ec = container_of(bridge, struct aspire_ec, bridge);

	schedule_work(&ec->work);
}

static const struct drm_bridge_funcs aspire_ec_bridge_funcs = {
	.hpd_enable = aspire_ec_bridge_hpd_enable,
	.attach = aspire_ec_bridge_attach,
};

/*
 * Sysfs attributes.
 */

static ssize_t fn_lock_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aspire_ec *ec = i2c_get_clientdata(to_i2c_client(dev));
	u8 tmp;

	aspire_ec_ram_read(ec->client, ASPIRE_EC_RAM_KBD_MODE, &tmp, sizeof(tmp));

	return sysfs_emit(buf, "%u\n", !(tmp & ASPIRE_EC_RAM_KBD_MEDIA_ON_TOP));
}

static ssize_t fn_lock_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct aspire_ec *ec = i2c_get_clientdata(to_i2c_client(dev));
	u8 tmp;

	bool state;
	int ret;

	ret = kstrtobool(buf, &state);
	if (ret)
		return ret;

	aspire_ec_ram_read(ec->client, ASPIRE_EC_RAM_KBD_MODE, &tmp, sizeof(tmp));

	if (state)
		tmp &= ~ASPIRE_EC_RAM_KBD_MEDIA_ON_TOP;
	else
		tmp |= ASPIRE_EC_RAM_KBD_MEDIA_ON_TOP;

	aspire_ec_ram_write(ec->client, ASPIRE_EC_RAM_KBD_MODE, tmp);

	return count;
}

static DEVICE_ATTR_RW(fn_lock);

static struct attribute *aspire_ec_attrs[] = {
	&dev_attr_fn_lock.attr,
	NULL
};
ATTRIBUTE_GROUPS(aspire_ec);

static int aspire_ec_probe(struct i2c_client *client)
{
	struct power_supply_config psy_cfg = {0};
	struct device *dev = &client->dev;
	struct fwnode_handle *fwnode;
	struct aspire_ec *ec;
	int ret;
	u8 tmp;

	ec = devm_kzalloc(dev, sizeof(*ec), GFP_KERNEL);
	if (!ec)
		return -ENOMEM;

	ec->client = client;
	i2c_set_clientdata(client, ec);

	/* Battery status reports */
	psy_cfg.drv_data = ec;
	ec->bat_psy = devm_power_supply_register(dev, &aspire_ec_bat_psy_desc, &psy_cfg);
	if (IS_ERR(ec->bat_psy))
		return dev_err_probe(dev, PTR_ERR(ec->bat_psy),
				     "Failed to register battery power supply\n");

	ec->adp_psy = devm_power_supply_register(dev, &aspire_ec_adp_psy_desc, &psy_cfg);
	if (IS_ERR(ec->adp_psy))
		return dev_err_probe(dev, PTR_ERR(ec->adp_psy),
				     "Failed to register AC power supply\n");

	/* Lid switch */
	ec->idev = devm_input_allocate_device(dev);
	if (!ec->idev)
		return -ENOMEM;

	ec->idev->name = "aspire-ec";
	ec->idev->phys = "aspire-ec/input0";
	input_set_capability(ec->idev, EV_SW, SW_LID);

	ret = input_register_device(ec->idev);
	if (ret)
		return dev_err_probe(dev, ret, "Input device register failed\n");

	/* Enable the keyboard fn keys */
	tmp = ASPIRE_EC_RAM_KBD_FN_EN | ASPIRE_EC_RAM_KBD_ALWAYS_SET;
	tmp |= ASPIRE_EC_RAM_KBD_MEDIA_ON_TOP;
	aspire_ec_ram_write(client, ASPIRE_EC_RAM_KBD_MODE, tmp);

	aspire_ec_ram_read(client, ASPIRE_EC_RAM_KBD_MODE_2, &tmp, sizeof(tmp));
	tmp |= ASPIRE_EC_RAM_KBD_MEDIA_NOTIFY;
	aspire_ec_ram_write(client, ASPIRE_EC_RAM_KBD_MODE_2, tmp);

	/* External Type-C display attach reports */
	fwnode = device_get_named_child_node(dev, "connector");
	if (fwnode) {
		INIT_WORK(&ec->work, aspire_ec_bridge_update_hpd_work);
		ec->bridge.funcs = &aspire_ec_bridge_funcs;
		ec->bridge.of_node = to_of_node(fwnode);
		ec->bridge.ops = DRM_BRIDGE_OP_HPD;
		ec->bridge.type = DRM_MODE_CONNECTOR_USB;

		ret = devm_drm_bridge_add(dev, &ec->bridge);
		if (ret) {
			fwnode_handle_put(fwnode);
			return dev_err_probe(dev, ret, "Failed to register drm bridge\n");
		}

		ec->bridge_configured = true;
	}

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					aspire_ec_irq_handler, IRQF_ONESHOT,
					dev_name(dev), ec);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request irq\n");

	return 0;
}

static int aspire_ec_resume(struct device *dev)
{
	struct aspire_ec *ec = i2c_get_clientdata(to_i2c_client(dev));
	u8 tmp;

	aspire_ec_ram_read(ec->client, ASPIRE_EC_RAM_LID_STATUS, &tmp, sizeof(tmp));
	input_report_switch(ec->idev, SW_LID, !!(tmp & ASPIRE_EC_LID_OPEN));
	input_sync(ec->idev);

	return 0;
}

static const struct i2c_device_id aspire_ec_id[] = {
	{ "aspire1-ec", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aspire_ec_id);

static const struct of_device_id aspire_ec_of_match[] = {
	{ .compatible = "acer,aspire1-ec", },
	{ }
};
MODULE_DEVICE_TABLE(of, aspire_ec_of_match);

static DEFINE_SIMPLE_DEV_PM_OPS(aspire_ec_pm_ops, NULL, aspire_ec_resume);

static struct i2c_driver aspire_ec_driver = {
	.driver = {
		.name = "aspire-ec",
		.of_match_table = aspire_ec_of_match,
		.pm = pm_sleep_ptr(&aspire_ec_pm_ops),
		.dev_groups = aspire_ec_groups,
	},
	.probe = aspire_ec_probe,
	.id_table = aspire_ec_id,
};
module_i2c_driver(aspire_ec_driver);

MODULE_DESCRIPTION("Acer Aspire 1 embedded controller");
MODULE_AUTHOR("Nikita Travkin <nikita@trvn.ru>");
MODULE_LICENSE("GPL");
