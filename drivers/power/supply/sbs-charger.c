// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016, Prodys S.L.
 *
 * This adds support for sbs-charger compilant chips as defined here:
 * http://sbs-forum.org/specs/sbc110.pdf
 *
 * Implemetation based on sbs-battery.c
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/power_supply.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/bitops.h>
#include <linux/devm-helpers.h>

#define SBS_CHARGER_REG_SPEC_INFO		0x11
#define SBS_CHARGER_REG_STATUS			0x13
#define SBS_CHARGER_REG_ALARM_WARNING		0x16

#define SBS_CHARGER_STATUS_CHARGE_INHIBITED	BIT(0)
#define SBS_CHARGER_STATUS_RES_COLD		BIT(9)
#define SBS_CHARGER_STATUS_RES_HOT		BIT(10)
#define SBS_CHARGER_STATUS_BATTERY_PRESENT	BIT(14)
#define SBS_CHARGER_STATUS_AC_PRESENT		BIT(15)

#define SBS_CHARGER_POLL_TIME			500

struct sbs_info {
	struct i2c_client		*client;
	struct power_supply		*power_supply;
	struct regmap			*regmap;
	struct delayed_work		work;
	unsigned int			last_state;
};

static int sbs_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct sbs_info *chip = power_supply_get_drvdata(psy);
	unsigned int reg;

	reg = chip->last_state;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = !!(reg & SBS_CHARGER_STATUS_BATTERY_PRESENT);
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = !!(reg & SBS_CHARGER_STATUS_AC_PRESENT);
		break;

	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;

		if (!(reg & SBS_CHARGER_STATUS_BATTERY_PRESENT))
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else if (reg & SBS_CHARGER_STATUS_AC_PRESENT &&
			 !(reg & SBS_CHARGER_STATUS_CHARGE_INHIBITED))
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;

		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (reg & SBS_CHARGER_STATUS_RES_COLD)
			val->intval = POWER_SUPPLY_HEALTH_COLD;
		if (reg & SBS_CHARGER_STATUS_RES_HOT)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;

		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int sbs_check_state(struct sbs_info *chip)
{
	unsigned int reg;
	int ret;

	ret = regmap_read(chip->regmap, SBS_CHARGER_REG_STATUS, &reg);
	if (!ret && reg != chip->last_state) {
		chip->last_state = reg;
		power_supply_changed(chip->power_supply);
		return 1;
	}

	return 0;
}

static void sbs_delayed_work(struct work_struct *work)
{
	struct sbs_info *chip = container_of(work, struct sbs_info, work.work);

	sbs_check_state(chip);

	schedule_delayed_work(&chip->work,
			      msecs_to_jiffies(SBS_CHARGER_POLL_TIME));
}

static irqreturn_t sbs_irq_thread(int irq, void *data)
{
	struct sbs_info *chip = data;
	int ret;

	ret = sbs_check_state(chip);

	return ret ? IRQ_HANDLED : IRQ_NONE;
}

static enum power_supply_property sbs_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
};

static bool sbs_readable_reg(struct device *dev, unsigned int reg)
{
	return reg >= SBS_CHARGER_REG_SPEC_INFO;
}

static bool sbs_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SBS_CHARGER_REG_STATUS:
		return true;
	}

	return false;
}

static const struct regmap_config sbs_regmap = {
	.reg_bits	= 8,
	.val_bits	= 16,
	.max_register	= SBS_CHARGER_REG_ALARM_WARNING,
	.readable_reg	= sbs_readable_reg,
	.volatile_reg	= sbs_volatile_reg,
	.val_format_endian = REGMAP_ENDIAN_LITTLE, /* since based on SMBus */
};

static const struct power_supply_desc sbs_desc = {
	.name = "sbs-charger",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = sbs_properties,
	.num_properties = ARRAY_SIZE(sbs_properties),
	.get_property = sbs_get_property,
};

static int sbs_probe(struct i2c_client *client)
{
	struct power_supply_config psy_cfg = {};
	struct sbs_info *chip;
	int ret, val;

	chip = devm_kzalloc(&client->dev, sizeof(struct sbs_info), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	psy_cfg.fwnode = dev_fwnode(&client->dev);
	psy_cfg.drv_data = chip;

	i2c_set_clientdata(client, chip);

	chip->regmap = devm_regmap_init_i2c(client, &sbs_regmap);
	if (IS_ERR(chip->regmap))
		return PTR_ERR(chip->regmap);

	/*
	 * Before we register, we need to make sure we can actually talk
	 * to the battery.
	 */
	ret = regmap_read(chip->regmap, SBS_CHARGER_REG_STATUS, &val);
	if (ret)
		return dev_err_probe(&client->dev, ret, "Failed to get device status\n");
	chip->last_state = val;

	chip->power_supply = devm_power_supply_register(&client->dev, &sbs_desc, &psy_cfg);
	if (IS_ERR(chip->power_supply))
		return dev_err_probe(&client->dev, PTR_ERR(chip->power_supply),
				     "Failed to register power supply\n");

	/*
	 * The sbs-charger spec doesn't impose the use of an interrupt. So in
	 * the case it wasn't provided we use polling in order get the charger's
	 * status.
	 */
	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, sbs_irq_thread,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					dev_name(&client->dev), chip);
		if (ret)
			return dev_err_probe(&client->dev, ret, "Failed to request irq\n");
	} else {
		ret = devm_delayed_work_autocancel(&client->dev, &chip->work,
						   sbs_delayed_work);
		if (ret)
			return dev_err_probe(&client->dev, ret,
					     "Failed to init work for polling\n");

		schedule_delayed_work(&chip->work,
				      msecs_to_jiffies(SBS_CHARGER_POLL_TIME));
	}

	dev_info(&client->dev,
		 "%s: smart charger device registered\n", client->name);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id sbs_dt_ids[] = {
	{ .compatible = "sbs,sbs-charger" },
	{ },
};
MODULE_DEVICE_TABLE(of, sbs_dt_ids);
#endif

static const struct i2c_device_id sbs_id[] = {
	{ "sbs-charger" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sbs_id);

static struct i2c_driver sbs_driver = {
	.probe		= sbs_probe,
	.id_table	= sbs_id,
	.driver = {
		.name	= "sbs-charger",
		.of_match_table = of_match_ptr(sbs_dt_ids),
	},
};
module_i2c_driver(sbs_driver);

MODULE_AUTHOR("Nicolas Saenz Julienne <nicolassaenzj@gmail.com>");
MODULE_DESCRIPTION("SBS smart charger driver");
MODULE_LICENSE("GPL v2");
