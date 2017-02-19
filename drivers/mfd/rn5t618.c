/*
 * MFD core driver for Ricoh RN5T618 PMIC
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 * Copyright (C) 2016 Toradex AG
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rn5t618.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

static const struct mfd_cell rn5t618_cells[] = {
	{ .name = "rn5t618-regulator" },
	{ .name = "rn5t618-wdt" },
};

static bool rn5t618_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RN5T618_WATCHDOGCNT:
	case RN5T618_DCIRQ:
	case RN5T618_ILIMDATAH ... RN5T618_AIN0DATAL:
	case RN5T618_IR_ADC1 ... RN5T618_IR_ADC3:
	case RN5T618_IR_GPR:
	case RN5T618_IR_GPF:
	case RN5T618_MON_IOIN:
	case RN5T618_INTMON:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rn5t618_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.volatile_reg	= rn5t618_volatile_reg,
	.max_register	= RN5T618_MAX_REG,
	.cache_type	= REGCACHE_RBTREE,
};

static struct rn5t618 *rn5t618_pm_power_off;
static struct notifier_block rn5t618_restart_handler;

static void rn5t618_trigger_poweroff_sequence(bool repower)
{
	/* disable automatic repower-on */
	regmap_update_bits(rn5t618_pm_power_off->regmap, RN5T618_REPCNT,
			   RN5T618_REPCNT_REPWRON,
			   repower ? RN5T618_REPCNT_REPWRON : 0);
	/* start power-off sequence */
	regmap_update_bits(rn5t618_pm_power_off->regmap, RN5T618_SLPCNT,
			   RN5T618_SLPCNT_SWPWROFF, RN5T618_SLPCNT_SWPWROFF);
}

static void rn5t618_power_off(void)
{
	rn5t618_trigger_poweroff_sequence(false);
}

static int rn5t618_restart(struct notifier_block *this,
			    unsigned long mode, void *cmd)
{
	rn5t618_trigger_poweroff_sequence(true);

	/*
	 * Re-power factor detection on PMIC side is not instant. 1ms
	 * proved to be enough time until reset takes effect.
	 */
	mdelay(1);

	return NOTIFY_DONE;
}

static const struct of_device_id rn5t618_of_match[] = {
	{ .compatible = "ricoh,rn5t567", .data = (void *)RN5T567 },
	{ .compatible = "ricoh,rn5t618", .data = (void *)RN5T618 },
	{ .compatible = "ricoh,rc5t619", .data = (void *)RC5T619 },
	{ }
};
MODULE_DEVICE_TABLE(of, rn5t618_of_match);

static int rn5t618_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	const struct of_device_id *of_id;
	struct rn5t618 *priv;
	int ret;

	of_id = of_match_device(rn5t618_of_match, &i2c->dev);
	if (!of_id) {
		dev_err(&i2c->dev, "Failed to find matching DT ID\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&i2c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_set_clientdata(i2c, priv);
	priv->variant = (long)of_id->data;

	priv->regmap = devm_regmap_init_i2c(i2c, &rn5t618_regmap_config);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(&i2c->dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	ret = devm_mfd_add_devices(&i2c->dev, -1, rn5t618_cells,
				   ARRAY_SIZE(rn5t618_cells), NULL, 0, NULL);
	if (ret) {
		dev_err(&i2c->dev, "failed to add sub-devices: %d\n", ret);
		return ret;
	}

	rn5t618_pm_power_off = priv;
	if (of_device_is_system_power_controller(i2c->dev.of_node)) {
		if (!pm_power_off)
			pm_power_off = rn5t618_power_off;
		else
			dev_warn(&i2c->dev, "Poweroff callback already assigned\n");
	}

	rn5t618_restart_handler.notifier_call = rn5t618_restart;
	rn5t618_restart_handler.priority = 192;

	ret = register_restart_handler(&rn5t618_restart_handler);
	if (ret) {
		dev_err(&i2c->dev, "cannot register restart handler, %d\n", ret);
		return ret;
	}

	return 0;
}

static int rn5t618_i2c_remove(struct i2c_client *i2c)
{
	struct rn5t618 *priv = i2c_get_clientdata(i2c);

	if (priv == rn5t618_pm_power_off) {
		rn5t618_pm_power_off = NULL;
		pm_power_off = NULL;
	}

	return 0;
}

static const struct i2c_device_id rn5t618_i2c_id[] = {
	{ }
};
MODULE_DEVICE_TABLE(i2c, rn5t618_i2c_id);

static struct i2c_driver rn5t618_i2c_driver = {
	.driver = {
		.name = "rn5t618",
		.of_match_table = of_match_ptr(rn5t618_of_match),
	},
	.probe = rn5t618_i2c_probe,
	.remove = rn5t618_i2c_remove,
	.id_table = rn5t618_i2c_id,
};

module_i2c_driver(rn5t618_i2c_driver);

MODULE_AUTHOR("Beniamino Galvani <b.galvani@gmail.com>");
MODULE_DESCRIPTION("Ricoh RN5T567/618 MFD driver");
MODULE_LICENSE("GPL v2");
