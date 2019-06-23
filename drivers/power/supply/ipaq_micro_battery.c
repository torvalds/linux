// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * h3xxx atmel micro companion support, battery subdevice
 * based on previous kernel 2.4 version
 * Author : Alessandro Gardich <gremlin@gremlin.it>
 * Author : Linus Walleij <linus.walleij@linaro.org>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mfd/ipaq-micro.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>

#define BATT_PERIOD 100000 /* 100 seconds in milliseconds */

#define MICRO_BATT_CHEM_ALKALINE	0x01
#define MICRO_BATT_CHEM_NICD		0x02
#define MICRO_BATT_CHEM_NIMH		0x03
#define MICRO_BATT_CHEM_LION		0x04
#define MICRO_BATT_CHEM_LIPOLY		0x05
#define MICRO_BATT_CHEM_NOT_INSTALLED	0x06
#define MICRO_BATT_CHEM_UNKNOWN		0xff

#define MICRO_BATT_STATUS_HIGH		0x01
#define MICRO_BATT_STATUS_LOW		0x02
#define MICRO_BATT_STATUS_CRITICAL	0x04
#define MICRO_BATT_STATUS_CHARGING	0x08
#define MICRO_BATT_STATUS_CHARGEMAIN	0x10
#define MICRO_BATT_STATUS_DEAD		0x20 /* Battery will not charge */
#define MICRO_BATT_STATUS_NOTINSTALLED	0x20 /* For expansion pack batteries */
#define MICRO_BATT_STATUS_FULL		0x40 /* Battery fully charged */
#define MICRO_BATT_STATUS_NOBATTERY	0x80
#define MICRO_BATT_STATUS_UNKNOWN	0xff

struct micro_battery {
	struct ipaq_micro *micro;
	struct workqueue_struct *wq;
	struct delayed_work update;
	u8 ac;
	u8 chemistry;
	unsigned int voltage;
	u16 temperature;
	u8 flag;
};

static void micro_battery_work(struct work_struct *work)
{
	struct micro_battery *mb = container_of(work,
				struct micro_battery, update.work);
	struct ipaq_micro_msg msg_battery = {
		.id = MSG_BATTERY,
	};
	struct ipaq_micro_msg msg_sensor = {
		.id = MSG_THERMAL_SENSOR,
	};

	/* First send battery message */
	ipaq_micro_tx_msg_sync(mb->micro, &msg_battery);
	if (msg_battery.rx_len < 4)
		pr_info("ERROR");

	/*
	 * Returned message format:
	 * byte 0:   0x00 = Not plugged in
	 *           0x01 = AC adapter plugged in
	 * byte 1:   chemistry
	 * byte 2:   voltage LSB
	 * byte 3:   voltage MSB
	 * byte 4:   flags
	 * byte 5-9: same for battery 2
	 */
	mb->ac = msg_battery.rx_data[0];
	mb->chemistry = msg_battery.rx_data[1];
	mb->voltage = ((((unsigned short)msg_battery.rx_data[3] << 8) +
			msg_battery.rx_data[2]) * 5000L) * 1000 / 1024;
	mb->flag = msg_battery.rx_data[4];

	if (msg_battery.rx_len == 9)
		pr_debug("second battery ignored\n");

	/* Then read the sensor */
	ipaq_micro_tx_msg_sync(mb->micro, &msg_sensor);
	mb->temperature = msg_sensor.rx_data[1] << 8 | msg_sensor.rx_data[0];

	queue_delayed_work(mb->wq, &mb->update, msecs_to_jiffies(BATT_PERIOD));
}

static int get_capacity(struct power_supply *b)
{
	struct micro_battery *mb = dev_get_drvdata(b->dev.parent);

	switch (mb->flag & 0x07) {
	case MICRO_BATT_STATUS_HIGH:
		return 100;
		break;
	case MICRO_BATT_STATUS_LOW:
		return 50;
		break;
	case MICRO_BATT_STATUS_CRITICAL:
		return 5;
		break;
	default:
		break;
	}
	return 0;
}

static int get_status(struct power_supply *b)
{
	struct micro_battery *mb = dev_get_drvdata(b->dev.parent);

	if (mb->flag == MICRO_BATT_STATUS_UNKNOWN)
		return POWER_SUPPLY_STATUS_UNKNOWN;

	if (mb->flag & MICRO_BATT_STATUS_FULL)
		return POWER_SUPPLY_STATUS_FULL;

	if ((mb->flag & MICRO_BATT_STATUS_CHARGING) ||
		(mb->flag & MICRO_BATT_STATUS_CHARGEMAIN))
		return POWER_SUPPLY_STATUS_CHARGING;

	return POWER_SUPPLY_STATUS_DISCHARGING;
}

static int micro_batt_get_property(struct power_supply *b,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct micro_battery *mb = dev_get_drvdata(b->dev.parent);

	switch (psp) {
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		switch (mb->chemistry) {
		case MICRO_BATT_CHEM_NICD:
			val->intval = POWER_SUPPLY_TECHNOLOGY_NiCd;
			break;
		case MICRO_BATT_CHEM_NIMH:
			val->intval = POWER_SUPPLY_TECHNOLOGY_NiMH;
			break;
		case MICRO_BATT_CHEM_LION:
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
			break;
		case MICRO_BATT_CHEM_LIPOLY:
			val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
			break;
		default:
			val->intval = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
			break;
		};
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = get_status(b);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = 4700000;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = get_capacity(b);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = mb->temperature;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = mb->voltage;
		break;
	default:
		return -EINVAL;
	};

	return 0;
}

static int micro_ac_get_property(struct power_supply *b,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	struct micro_battery *mb = dev_get_drvdata(b->dev.parent);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = mb->ac;
		break;
	default:
		return -EINVAL;
	};

	return 0;
}

static enum power_supply_property micro_batt_power_props[] = {
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static const struct power_supply_desc micro_batt_power_desc = {
	.name			= "main-battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= micro_batt_power_props,
	.num_properties		= ARRAY_SIZE(micro_batt_power_props),
	.get_property		= micro_batt_get_property,
	.use_for_apm		= 1,
};

static enum power_supply_property micro_ac_power_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static const struct power_supply_desc micro_ac_power_desc = {
	.name			= "ac",
	.type			= POWER_SUPPLY_TYPE_MAINS,
	.properties		= micro_ac_power_props,
	.num_properties		= ARRAY_SIZE(micro_ac_power_props),
	.get_property		= micro_ac_get_property,
};

static struct power_supply *micro_batt_power, *micro_ac_power;

static int micro_batt_probe(struct platform_device *pdev)
{
	struct micro_battery *mb;
	int ret;

	mb = devm_kzalloc(&pdev->dev, sizeof(*mb), GFP_KERNEL);
	if (!mb)
		return -ENOMEM;

	mb->micro = dev_get_drvdata(pdev->dev.parent);
	mb->wq = alloc_workqueue("ipaq-battery-wq", WQ_MEM_RECLAIM, 0);
	if (!mb->wq)
		return -ENOMEM;

	INIT_DELAYED_WORK(&mb->update, micro_battery_work);
	platform_set_drvdata(pdev, mb);
	queue_delayed_work(mb->wq, &mb->update, 1);

	micro_batt_power = power_supply_register(&pdev->dev,
						 &micro_batt_power_desc, NULL);
	if (IS_ERR(micro_batt_power)) {
		ret = PTR_ERR(micro_batt_power);
		goto batt_err;
	}

	micro_ac_power = power_supply_register(&pdev->dev,
					       &micro_ac_power_desc, NULL);
	if (IS_ERR(micro_ac_power)) {
		ret = PTR_ERR(micro_ac_power);
		goto ac_err;
	}

	dev_info(&pdev->dev, "iPAQ micro battery driver\n");
	return 0;

ac_err:
	power_supply_unregister(micro_batt_power);
batt_err:
	cancel_delayed_work_sync(&mb->update);
	destroy_workqueue(mb->wq);
	return ret;
}

static int micro_batt_remove(struct platform_device *pdev)

{
	struct micro_battery *mb = platform_get_drvdata(pdev);

	power_supply_unregister(micro_ac_power);
	power_supply_unregister(micro_batt_power);
	cancel_delayed_work_sync(&mb->update);
	destroy_workqueue(mb->wq);

	return 0;
}

static int __maybe_unused micro_batt_suspend(struct device *dev)
{
	struct micro_battery *mb = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&mb->update);
	return 0;
}

static int __maybe_unused micro_batt_resume(struct device *dev)
{
	struct micro_battery *mb = dev_get_drvdata(dev);

	queue_delayed_work(mb->wq, &mb->update, msecs_to_jiffies(BATT_PERIOD));
	return 0;
}

static const struct dev_pm_ops micro_batt_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(micro_batt_suspend, micro_batt_resume)
};

static struct platform_driver micro_batt_device_driver = {
	.driver		= {
		.name	= "ipaq-micro-battery",
		.pm	= &micro_batt_dev_pm_ops,
	},
	.probe		= micro_batt_probe,
	.remove		= micro_batt_remove,
};
module_platform_driver(micro_batt_device_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("driver for iPAQ Atmel micro battery");
MODULE_ALIAS("platform:ipaq-micro-battery");
