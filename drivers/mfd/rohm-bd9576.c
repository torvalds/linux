// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021 ROHM Semiconductors
 *
 * ROHM BD9576MUF and BD9573MUF PMIC driver
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rohm-bd957x.h>
#include <linux/mfd/rohm-generic.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

enum {
	BD957X_REGULATOR_CELL,
	BD957X_WDT_CELL,
};

/*
 * Due to the BD9576MUF nasty IRQ behaiour we don't always populate IRQs.
 * These will be added to regulator resources only if IRQ information for the
 * PMIC is populated in device-tree.
 */
static const struct resource bd9576_regulator_irqs[] = {
	DEFINE_RES_IRQ_NAMED(BD9576_INT_THERM, "bd9576-temp"),
	DEFINE_RES_IRQ_NAMED(BD9576_INT_OVD, "bd9576-ovd"),
	DEFINE_RES_IRQ_NAMED(BD9576_INT_UVD, "bd9576-uvd"),
};

static struct mfd_cell bd9573_mfd_cells[] = {
	[BD957X_REGULATOR_CELL]	= { .name = "bd9573-regulator", },
	[BD957X_WDT_CELL]	= { .name = "bd9576-wdt", },
};

static struct mfd_cell bd9576_mfd_cells[] = {
	[BD957X_REGULATOR_CELL]	= { .name = "bd9576-regulator", },
	[BD957X_WDT_CELL]	= { .name = "bd9576-wdt", },
};

static const struct regmap_range volatile_ranges[] = {
	regmap_reg_range(BD957X_REG_SMRB_ASSERT, BD957X_REG_SMRB_ASSERT),
	regmap_reg_range(BD957X_REG_PMIC_INTERNAL_STAT,
			 BD957X_REG_PMIC_INTERNAL_STAT),
	regmap_reg_range(BD957X_REG_INT_THERM_STAT, BD957X_REG_INT_THERM_STAT),
	regmap_reg_range(BD957X_REG_INT_OVP_STAT, BD957X_REG_INT_SYS_STAT),
	regmap_reg_range(BD957X_REG_INT_MAIN_STAT, BD957X_REG_INT_MAIN_STAT),
};

static const struct regmap_access_table volatile_regs = {
	.yes_ranges = &volatile_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(volatile_ranges),
};

static struct regmap_config bd957x_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &volatile_regs,
	.max_register = BD957X_MAX_REGISTER,
	.cache_type = REGCACHE_RBTREE,
};

static struct regmap_irq bd9576_irqs[] = {
	REGMAP_IRQ_REG(BD9576_INT_THERM, 0, BD957X_MASK_INT_MAIN_THERM),
	REGMAP_IRQ_REG(BD9576_INT_OVP, 0, BD957X_MASK_INT_MAIN_OVP),
	REGMAP_IRQ_REG(BD9576_INT_SCP, 0, BD957X_MASK_INT_MAIN_SCP),
	REGMAP_IRQ_REG(BD9576_INT_OCP, 0, BD957X_MASK_INT_MAIN_OCP),
	REGMAP_IRQ_REG(BD9576_INT_OVD, 0, BD957X_MASK_INT_MAIN_OVD),
	REGMAP_IRQ_REG(BD9576_INT_UVD, 0, BD957X_MASK_INT_MAIN_UVD),
	REGMAP_IRQ_REG(BD9576_INT_UVP, 0, BD957X_MASK_INT_MAIN_UVP),
	REGMAP_IRQ_REG(BD9576_INT_SYS, 0, BD957X_MASK_INT_MAIN_SYS),
};

static struct regmap_irq_chip bd9576_irq_chip = {
	.name = "bd9576_irq",
	.irqs = &bd9576_irqs[0],
	.num_irqs = ARRAY_SIZE(bd9576_irqs),
	.status_base = BD957X_REG_INT_MAIN_STAT,
	.mask_base = BD957X_REG_INT_MAIN_MASK,
	.ack_base = BD957X_REG_INT_MAIN_STAT,
	.init_ack_masked = true,
	.num_regs = 1,
	.irq_reg_stride = 1,
};

static int bd957x_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	int ret;
	struct regmap *regmap;
	struct mfd_cell *cells;
	int num_cells;
	unsigned long chip_type;
	struct irq_domain *domain;
	bool usable_irqs;

	chip_type = (unsigned long)of_device_get_match_data(&i2c->dev);

	switch (chip_type) {
	case ROHM_CHIP_TYPE_BD9576:
		cells = bd9576_mfd_cells;
		num_cells = ARRAY_SIZE(bd9576_mfd_cells);
		usable_irqs = !!i2c->irq;
		break;
	case ROHM_CHIP_TYPE_BD9573:
		cells = bd9573_mfd_cells;
		num_cells = ARRAY_SIZE(bd9573_mfd_cells);
		/*
		 * BD9573 only supports fatal IRQs which we can not handle
		 * because SoC is going to lose the power.
		 */
		usable_irqs = false;
		break;
	default:
		dev_err(&i2c->dev, "Unknown device type");
		return -EINVAL;
	}

	regmap = devm_regmap_init_i2c(i2c, &bd957x_regmap);
	if (IS_ERR(regmap)) {
		dev_err(&i2c->dev, "Failed to initialize Regmap\n");
		return PTR_ERR(regmap);
	}

	/*
	 * BD9576 behaves badly. It kepts IRQ line asserted for the whole
	 * duration of detected HW condition (like over temperature). So we
	 * don't require IRQ to be populated.
	 * If IRQ information is not given, then we mask all IRQs and do not
	 * provide IRQ resources to regulator driver - which then just omits
	 * the notifiers.
	 */
	if (usable_irqs) {
		struct regmap_irq_chip_data *irq_data;
		struct mfd_cell *regulators;

		regulators = &bd9576_mfd_cells[BD957X_REGULATOR_CELL];
		regulators->resources = bd9576_regulator_irqs;
		regulators->num_resources = ARRAY_SIZE(bd9576_regulator_irqs);

		ret = devm_regmap_add_irq_chip(&i2c->dev, regmap, i2c->irq,
					       IRQF_ONESHOT, 0,
					       &bd9576_irq_chip, &irq_data);
		if (ret) {
			dev_err(&i2c->dev, "Failed to add IRQ chip\n");
			return ret;
		}
		domain = regmap_irq_get_domain(irq_data);
	} else {
		ret = regmap_update_bits(regmap, BD957X_REG_INT_MAIN_MASK,
					 BD957X_MASK_INT_ALL,
					 BD957X_MASK_INT_ALL);
		if (ret)
			return ret;
		domain = NULL;
	}

	ret = devm_mfd_add_devices(&i2c->dev, PLATFORM_DEVID_AUTO, cells,
				   num_cells, NULL, 0, domain);
	if (ret)
		dev_err(&i2c->dev, "Failed to create subdevices\n");

	return ret;
}

static const struct of_device_id bd957x_of_match[] = {
	{ .compatible = "rohm,bd9576", .data = (void *)ROHM_CHIP_TYPE_BD9576, },
	{ .compatible = "rohm,bd9573", .data = (void *)ROHM_CHIP_TYPE_BD9573, },
	{ },
};
MODULE_DEVICE_TABLE(of, bd957x_of_match);

static struct i2c_driver bd957x_drv = {
	.driver = {
		.name = "rohm-bd957x",
		.of_match_table = bd957x_of_match,
	},
	.probe = &bd957x_i2c_probe,
};
module_i2c_driver(bd957x_drv);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("ROHM BD9576MUF and BD9573MUF Power Management IC driver");
MODULE_LICENSE("GPL");
