// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * An hwmon driver for the NXP MC34VR500 PMIC
 *
 * Author: Mario Kicherer <dev@kicherer.org>
 */

#include <linux/bits.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#define MC34VR500_I2C_ADDR		0x08
#define MC34VR500_DEVICEID_VALUE	0x14

/* INTSENSE0 */
#define ENS_BIT		BIT(0)
#define LOWVINS_BIT	BIT(1)
#define THERM110S_BIT	BIT(2)
#define THERM120S_BIT	BIT(3)
#define THERM125S_BIT	BIT(4)
#define THERM130S_BIT	BIT(5)

#define MC34VR500_DEVICEID	0x00

#define MC34VR500_SILICONREVID	0x03
#define MC34VR500_FABID		0x04
#define MC34VR500_INTSTAT0	0x05
#define MC34VR500_INTMASK0	0x06
#define MC34VR500_INTSENSE0	0x07

struct mc34vr500_data {
	struct device *hwmon_dev;
	struct regmap *regmap;
};

static irqreturn_t mc34vr500_process_interrupt(int irq, void *userdata)
{
	struct mc34vr500_data *data = (struct mc34vr500_data *)userdata;
	unsigned int reg;
	int ret;

	ret = regmap_read(data->regmap, MC34VR500_INTSTAT0, &reg);
	if (ret < 0)
		return IRQ_HANDLED;

	if (reg) {
		if (reg & LOWVINS_BIT)
			hwmon_notify_event(data->hwmon_dev, hwmon_in,
					   hwmon_in_min_alarm, 0);

		if (reg & THERM110S_BIT)
			hwmon_notify_event(data->hwmon_dev, hwmon_temp,
					   hwmon_temp_max_alarm, 0);

		if (reg & THERM120S_BIT)
			hwmon_notify_event(data->hwmon_dev, hwmon_temp,
					   hwmon_temp_crit_alarm, 0);

		if (reg & THERM130S_BIT)
			hwmon_notify_event(data->hwmon_dev, hwmon_temp,
					   hwmon_temp_emergency_alarm, 0);

		/* write 1 to clear */
		regmap_write(data->regmap, MC34VR500_INTSTAT0, LOWVINS_BIT |
			     THERM110S_BIT | THERM120S_BIT | THERM130S_BIT);
	}

	return IRQ_HANDLED;
}

static umode_t mc34vr500_is_visible(const void *data,
				    enum hwmon_sensor_types type,
				    u32 attr, int channel)
{
	switch (attr) {
	case hwmon_in_min_alarm:
	case hwmon_temp_max_alarm:
	case hwmon_temp_crit_alarm:
	case hwmon_temp_emergency_alarm:
		return 0444;
	default:
		break;
	}

	return 0;
}

static int mc34vr500_alarm_read(struct mc34vr500_data *data, int index,
				long *val)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(data->regmap, MC34VR500_INTSENSE0, &reg);
	if (ret < 0)
		return ret;

	*val = !!(reg & index);

	return 0;
}

static int mc34vr500_read(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long *val)
{
	struct mc34vr500_data *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_min_alarm:
			return mc34vr500_alarm_read(data, LOWVINS_BIT, val);
		default:
			return -EOPNOTSUPP;
		}
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_max_alarm:
			return mc34vr500_alarm_read(data, THERM110S_BIT, val);
		case hwmon_temp_crit_alarm:
			return mc34vr500_alarm_read(data, THERM120S_BIT, val);
		case hwmon_temp_emergency_alarm:
			return mc34vr500_alarm_read(data, THERM130S_BIT, val);
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_channel_info * const mc34vr500_info[] = {
	HWMON_CHANNEL_INFO(in, HWMON_I_MIN_ALARM),
	HWMON_CHANNEL_INFO(temp, HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM
			   | HWMON_T_EMERGENCY_ALARM),
	NULL,
};

static const struct hwmon_ops mc34vr500_hwmon_ops = {
	.is_visible = mc34vr500_is_visible,
	.read = mc34vr500_read,
};

static const struct hwmon_chip_info mc34vr500_chip_info = {
	.ops = &mc34vr500_hwmon_ops,
	.info = mc34vr500_info,
};

static const struct regmap_config mc34vr500_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MC34VR500_INTSENSE0,
};

static int mc34vr500_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct mc34vr500_data *data;
	struct device *hwmon_dev;
	int ret;
	unsigned int reg, revid, fabid;
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &mc34vr500_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	data = devm_kzalloc(dev, sizeof(struct mc34vr500_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = regmap;

	ret = regmap_read(regmap, MC34VR500_DEVICEID, &reg);
	if (ret < 0)
		return ret;

	if (reg != MC34VR500_DEVICEID_VALUE)
		return -ENODEV;

	ret = regmap_read(regmap, MC34VR500_SILICONREVID, &revid);
	if (ret < 0)
		return ret;

	ret = regmap_read(regmap, MC34VR500_FABID, &fabid);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "mc34vr500: revid 0x%x fabid 0x%x\n", revid, fabid);

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data,
							 &mc34vr500_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	data->hwmon_dev = hwmon_dev;

	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						mc34vr500_process_interrupt,
						IRQF_TRIGGER_RISING |
						IRQF_ONESHOT |
						IRQF_SHARED,
						dev_name(dev), data);
		if (ret)
			return ret;

		/* write 1 to clear interrupts */
		ret = regmap_write(regmap, MC34VR500_INTSTAT0, LOWVINS_BIT |
				   THERM110S_BIT | THERM120S_BIT |
				   THERM130S_BIT);
		if (ret)
			return ret;

		/* unmask interrupts */
		ret = regmap_write(regmap, MC34VR500_INTMASK0,
				   (unsigned int) ~(LOWVINS_BIT | THERM110S_BIT |
						    THERM120S_BIT | THERM130S_BIT));
		if (ret)
			return ret;
	}

	return 0;
}

static const struct i2c_device_id mc34vr500_id[] = {
	{ "mc34vr500" },
	{ },
};
MODULE_DEVICE_TABLE(i2c, mc34vr500_id);

static const struct of_device_id __maybe_unused mc34vr500_of_match[] = {
	{ .compatible = "nxp,mc34vr500" },
	{ },
};
MODULE_DEVICE_TABLE(of, mc34vr500_of_match);

static struct i2c_driver mc34vr500_driver = {
	.driver = {
		   .name = "mc34vr500",
		   .of_match_table = of_match_ptr(mc34vr500_of_match),
		    },
	.probe = mc34vr500_probe,
	.id_table = mc34vr500_id,
};

module_i2c_driver(mc34vr500_driver);

MODULE_AUTHOR("Mario Kicherer <dev@kicherer.org>");

MODULE_DESCRIPTION("MC34VR500 driver");
MODULE_LICENSE("GPL");
