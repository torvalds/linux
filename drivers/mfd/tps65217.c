// SPDX-License-Identifier: GPL-2.0-only
/*
 * tps65217.c
 *
 * TPS65217 chip family multi-function driver
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - https://www.ti.com/
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <linux/mfd/core.h>
#include <linux/mfd/tps65217.h>

static const struct resource charger_resources[] = {
	DEFINE_RES_IRQ_NAMED(TPS65217_IRQ_AC, "AC"),
	DEFINE_RES_IRQ_NAMED(TPS65217_IRQ_USB, "USB"),
};

static const struct resource pb_resources[] = {
	DEFINE_RES_IRQ_NAMED(TPS65217_IRQ_PB, "PB"),
};

static void tps65217_irq_lock(struct irq_data *data)
{
	struct tps65217 *tps = irq_data_get_irq_chip_data(data);

	mutex_lock(&tps->irq_lock);
}

static void tps65217_irq_sync_unlock(struct irq_data *data)
{
	struct tps65217 *tps = irq_data_get_irq_chip_data(data);
	int ret;

	ret = tps65217_set_bits(tps, TPS65217_REG_INT, TPS65217_INT_MASK,
				tps->irq_mask, TPS65217_PROTECT_NONE);
	if (ret != 0)
		dev_err(tps->dev, "Failed to sync IRQ masks\n");

	mutex_unlock(&tps->irq_lock);
}

static void tps65217_irq_enable(struct irq_data *data)
{
	struct tps65217 *tps = irq_data_get_irq_chip_data(data);
	u8 mask = BIT(data->hwirq) << TPS65217_INT_SHIFT;

	tps->irq_mask &= ~mask;
}

static void tps65217_irq_disable(struct irq_data *data)
{
	struct tps65217 *tps = irq_data_get_irq_chip_data(data);
	u8 mask = BIT(data->hwirq) << TPS65217_INT_SHIFT;

	tps->irq_mask |= mask;
}

static struct irq_chip tps65217_irq_chip = {
	.name			= "tps65217",
	.irq_bus_lock		= tps65217_irq_lock,
	.irq_bus_sync_unlock	= tps65217_irq_sync_unlock,
	.irq_enable		= tps65217_irq_enable,
	.irq_disable		= tps65217_irq_disable,
};

static struct mfd_cell tps65217s[] = {
	{
		.name = "tps65217-pmic",
		.of_compatible = "ti,tps65217-pmic",
	},
	{
		.name = "tps65217-bl",
		.of_compatible = "ti,tps65217-bl",
	},
	{
		.name = "tps65217-charger",
		.num_resources = ARRAY_SIZE(charger_resources),
		.resources = charger_resources,
		.of_compatible = "ti,tps65217-charger",
	},
	{
		.name = "tps65217-pwrbutton",
		.num_resources = ARRAY_SIZE(pb_resources),
		.resources = pb_resources,
		.of_compatible = "ti,tps65217-pwrbutton",
	},
};

static irqreturn_t tps65217_irq_thread(int irq, void *data)
{
	struct tps65217 *tps = data;
	unsigned int status;
	bool handled = false;
	int i;
	int ret;

	ret = tps65217_reg_read(tps, TPS65217_REG_INT, &status);
	if (ret < 0) {
		dev_err(tps->dev, "Failed to read IRQ status: %d\n",
			ret);
		return IRQ_NONE;
	}

	for (i = 0; i < TPS65217_NUM_IRQ; i++) {
		if (status & BIT(i)) {
			handle_nested_irq(irq_find_mapping(tps->irq_domain, i));
			handled = true;
		}
	}

	if (handled)
		return IRQ_HANDLED;

	return IRQ_NONE;
}

static int tps65217_irq_map(struct irq_domain *h, unsigned int virq,
			irq_hw_number_t hw)
{
	struct tps65217 *tps = h->host_data;

	irq_set_chip_data(virq, tps);
	irq_set_chip_and_handler(virq, &tps65217_irq_chip, handle_edge_irq);
	irq_set_nested_thread(virq, 1);
	irq_set_parent(virq, tps->irq);
	irq_set_noprobe(virq);

	return 0;
}

static const struct irq_domain_ops tps65217_irq_domain_ops = {
	.map = tps65217_irq_map,
};

static int tps65217_irq_init(struct tps65217 *tps, int irq)
{
	int ret;

	mutex_init(&tps->irq_lock);
	tps->irq = irq;

	/* Mask all interrupt sources */
	tps->irq_mask = TPS65217_INT_MASK;
	tps65217_set_bits(tps, TPS65217_REG_INT, TPS65217_INT_MASK,
			  TPS65217_INT_MASK, TPS65217_PROTECT_NONE);

	tps->irq_domain = irq_domain_add_linear(tps->dev->of_node,
		TPS65217_NUM_IRQ, &tps65217_irq_domain_ops, tps);
	if (!tps->irq_domain) {
		dev_err(tps->dev, "Could not create IRQ domain\n");
		return -ENOMEM;
	}

	ret = devm_request_threaded_irq(tps->dev, irq, NULL,
					tps65217_irq_thread, IRQF_ONESHOT,
					"tps65217-irq", tps);
	if (ret) {
		dev_err(tps->dev, "Failed to request IRQ %d: %d\n",
			irq, ret);
		return ret;
	}

	enable_irq_wake(irq);

	return 0;
}

/**
 * tps65217_reg_read: Read a single tps65217 register.
 *
 * @tps: Device to read from.
 * @reg: Register to read.
 * @val: Contians the value
 */
int tps65217_reg_read(struct tps65217 *tps, unsigned int reg,
			unsigned int *val)
{
	return regmap_read(tps->regmap, reg, val);
}
EXPORT_SYMBOL_GPL(tps65217_reg_read);

/**
 * tps65217_reg_write: Write a single tps65217 register.
 *
 * @tps: Device to write to.
 * @reg: Register to write to.
 * @val: Value to write.
 * @level: Password protected level
 */
int tps65217_reg_write(struct tps65217 *tps, unsigned int reg,
			unsigned int val, unsigned int level)
{
	int ret;
	unsigned int xor_reg_val;

	switch (level) {
	case TPS65217_PROTECT_NONE:
		return regmap_write(tps->regmap, reg, val);
	case TPS65217_PROTECT_L1:
		xor_reg_val = reg ^ TPS65217_PASSWORD_REGS_UNLOCK;
		ret = regmap_write(tps->regmap, TPS65217_REG_PASSWORD,
							xor_reg_val);
		if (ret < 0)
			return ret;

		return regmap_write(tps->regmap, reg, val);
	case TPS65217_PROTECT_L2:
		xor_reg_val = reg ^ TPS65217_PASSWORD_REGS_UNLOCK;
		ret = regmap_write(tps->regmap, TPS65217_REG_PASSWORD,
							xor_reg_val);
		if (ret < 0)
			return ret;
		ret = regmap_write(tps->regmap, reg, val);
		if (ret < 0)
			return ret;
		ret = regmap_write(tps->regmap, TPS65217_REG_PASSWORD,
							xor_reg_val);
		if (ret < 0)
			return ret;
		return regmap_write(tps->regmap, reg, val);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(tps65217_reg_write);

/**
 * tps65217_update_bits: Modify bits w.r.t mask, val and level.
 *
 * @tps: Device to write to.
 * @reg: Register to read-write to.
 * @mask: Mask.
 * @val: Value to write.
 * @level: Password protected level
 */
static int tps65217_update_bits(struct tps65217 *tps, unsigned int reg,
		unsigned int mask, unsigned int val, unsigned int level)
{
	int ret;
	unsigned int data;

	ret = tps65217_reg_read(tps, reg, &data);
	if (ret) {
		dev_err(tps->dev, "Read from reg 0x%x failed\n", reg);
		return ret;
	}

	data &= ~mask;
	data |= val & mask;

	ret = tps65217_reg_write(tps, reg, data, level);
	if (ret)
		dev_err(tps->dev, "Write for reg 0x%x failed\n", reg);

	return ret;
}

int tps65217_set_bits(struct tps65217 *tps, unsigned int reg,
		unsigned int mask, unsigned int val, unsigned int level)
{
	return tps65217_update_bits(tps, reg, mask, val, level);
}
EXPORT_SYMBOL_GPL(tps65217_set_bits);

int tps65217_clear_bits(struct tps65217 *tps, unsigned int reg,
		unsigned int mask, unsigned int level)
{
	return tps65217_update_bits(tps, reg, mask, 0, level);
}
EXPORT_SYMBOL_GPL(tps65217_clear_bits);

static bool tps65217_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TPS65217_REG_INT:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tps65217_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = TPS65217_REG_MAX,
	.volatile_reg = tps65217_volatile_reg,
};

static const struct of_device_id tps65217_of_match[] = {
	{ .compatible = "ti,tps65217"},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, tps65217_of_match);

static int tps65217_probe(struct i2c_client *client)
{
	struct tps65217 *tps;
	unsigned int version;
	bool status_off = false;
	int ret;

	status_off = of_property_read_bool(client->dev.of_node,
					   "ti,pmic-shutdown-controller");

	tps = devm_kzalloc(&client->dev, sizeof(*tps), GFP_KERNEL);
	if (!tps)
		return -ENOMEM;

	i2c_set_clientdata(client, tps);
	tps->dev = &client->dev;

	tps->regmap = devm_regmap_init_i2c(client, &tps65217_regmap_config);
	if (IS_ERR(tps->regmap)) {
		ret = PTR_ERR(tps->regmap);
		dev_err(tps->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	if (client->irq) {
		tps65217_irq_init(tps, client->irq);
	} else {
		int i;

		/* Don't tell children about IRQ resources which won't fire */
		for (i = 0; i < ARRAY_SIZE(tps65217s); i++)
			tps65217s[i].num_resources = 0;
	}

	ret = devm_mfd_add_devices(tps->dev, -1, tps65217s,
				   ARRAY_SIZE(tps65217s), NULL, 0,
				   tps->irq_domain);
	if (ret < 0) {
		dev_err(tps->dev, "mfd_add_devices failed: %d\n", ret);
		return ret;
	}

	ret = tps65217_reg_read(tps, TPS65217_REG_CHIPID, &version);
	if (ret < 0) {
		dev_err(tps->dev, "Failed to read revision register: %d\n",
			ret);
		return ret;
	}

	/* Set the PMIC to shutdown on PWR_EN toggle */
	if (status_off) {
		ret = tps65217_set_bits(tps, TPS65217_REG_STATUS,
				TPS65217_STATUS_OFF, TPS65217_STATUS_OFF,
				TPS65217_PROTECT_NONE);
		if (ret)
			dev_warn(tps->dev, "unable to set the status OFF\n");
	}

	dev_info(tps->dev, "TPS65217 ID %#x version 1.%d\n",
			(version & TPS65217_CHIPID_CHIP_MASK) >> 4,
			version & TPS65217_CHIPID_REV_MASK);

	return 0;
}

static void tps65217_remove(struct i2c_client *client)
{
	struct tps65217 *tps = i2c_get_clientdata(client);
	unsigned int virq;
	int i;

	for (i = 0; i < TPS65217_NUM_IRQ; i++) {
		virq = irq_find_mapping(tps->irq_domain, i);
		if (virq)
			irq_dispose_mapping(virq);
	}

	irq_domain_remove(tps->irq_domain);
	tps->irq_domain = NULL;
}

static const struct i2c_device_id tps65217_id_table[] = {
	{"tps65217", TPS65217},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, tps65217_id_table);

static struct i2c_driver tps65217_driver = {
	.driver		= {
		.name	= "tps65217",
		.of_match_table = tps65217_of_match,
	},
	.id_table	= tps65217_id_table,
	.probe_new	= tps65217_probe,
	.remove		= tps65217_remove,
};

static int __init tps65217_init(void)
{
	return i2c_add_driver(&tps65217_driver);
}
subsys_initcall(tps65217_init);

static void __exit tps65217_exit(void)
{
	i2c_del_driver(&tps65217_driver);
}
module_exit(tps65217_exit);

MODULE_AUTHOR("AnilKumar Ch <anilkumar@ti.com>");
MODULE_DESCRIPTION("TPS65217 chip family multi-function driver");
MODULE_LICENSE("GPL v2");
