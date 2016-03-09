/*
 * Core functions for TI TPS65912x PMICs
 *
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether expressed or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
 *
 * Based on the TPS65218 driver and the previous TPS65912 driver by
 * Margarita Olaya Cabrera <magi@slimlogic.co.uk>
 */

#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/module.h>

#include <linux/mfd/tps65912.h>

static const struct mfd_cell tps65912_cells[] = {
	{ .name = "tps65912-regulator", },
	{ .name = "tps65912-gpio", },
};

static const struct regmap_irq tps65912_irqs[] = {
	/* INT_STS IRQs */
	REGMAP_IRQ_REG(TPS65912_IRQ_PWRHOLD_F, 0, TPS65912_INT_STS_PWRHOLD_F),
	REGMAP_IRQ_REG(TPS65912_IRQ_VMON, 0, TPS65912_INT_STS_VMON),
	REGMAP_IRQ_REG(TPS65912_IRQ_PWRON, 0, TPS65912_INT_STS_PWRON),
	REGMAP_IRQ_REG(TPS65912_IRQ_PWRON_LP, 0, TPS65912_INT_STS_PWRON_LP),
	REGMAP_IRQ_REG(TPS65912_IRQ_PWRHOLD_R, 0, TPS65912_INT_STS_PWRHOLD_R),
	REGMAP_IRQ_REG(TPS65912_IRQ_HOTDIE, 0, TPS65912_INT_STS_HOTDIE),
	REGMAP_IRQ_REG(TPS65912_IRQ_GPIO1_R, 0, TPS65912_INT_STS_GPIO1_R),
	REGMAP_IRQ_REG(TPS65912_IRQ_GPIO1_F, 0, TPS65912_INT_STS_GPIO1_F),
	/* INT_STS2 IRQs */
	REGMAP_IRQ_REG(TPS65912_IRQ_GPIO2_R, 1, TPS65912_INT_STS2_GPIO2_R),
	REGMAP_IRQ_REG(TPS65912_IRQ_GPIO2_F, 1, TPS65912_INT_STS2_GPIO2_F),
	REGMAP_IRQ_REG(TPS65912_IRQ_GPIO3_R, 1, TPS65912_INT_STS2_GPIO3_R),
	REGMAP_IRQ_REG(TPS65912_IRQ_GPIO3_F, 1, TPS65912_INT_STS2_GPIO3_F),
	REGMAP_IRQ_REG(TPS65912_IRQ_GPIO4_R, 1, TPS65912_INT_STS2_GPIO4_R),
	REGMAP_IRQ_REG(TPS65912_IRQ_GPIO4_F, 1, TPS65912_INT_STS2_GPIO4_F),
	REGMAP_IRQ_REG(TPS65912_IRQ_GPIO5_R, 1, TPS65912_INT_STS2_GPIO5_R),
	REGMAP_IRQ_REG(TPS65912_IRQ_GPIO5_F, 1, TPS65912_INT_STS2_GPIO5_F),
	/* INT_STS3 IRQs */
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_DCDC1, 2, TPS65912_INT_STS3_PGOOD_DCDC1),
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_DCDC2, 2, TPS65912_INT_STS3_PGOOD_DCDC2),
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_DCDC3, 2, TPS65912_INT_STS3_PGOOD_DCDC3),
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_DCDC4, 2, TPS65912_INT_STS3_PGOOD_DCDC4),
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_LDO1, 2, TPS65912_INT_STS3_PGOOD_LDO1),
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_LDO2, 2, TPS65912_INT_STS3_PGOOD_LDO2),
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_LDO3, 2, TPS65912_INT_STS3_PGOOD_LDO3),
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_LDO4, 2, TPS65912_INT_STS3_PGOOD_LDO4),
	/* INT_STS4 IRQs */
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_LDO5, 3, TPS65912_INT_STS4_PGOOD_LDO5),
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_LDO6, 3, TPS65912_INT_STS4_PGOOD_LDO6),
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_LDO7, 3, TPS65912_INT_STS4_PGOOD_LDO7),
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_LDO8, 3, TPS65912_INT_STS4_PGOOD_LDO8),
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_LDO9, 3, TPS65912_INT_STS4_PGOOD_LDO9),
	REGMAP_IRQ_REG(TPS65912_IRQ_PGOOD_LDO10, 3, TPS65912_INT_STS4_PGOOD_LDO10),
};

static struct regmap_irq_chip tps65912_irq_chip = {
	.name = "tps65912",
	.irqs = tps65912_irqs,
	.num_irqs = ARRAY_SIZE(tps65912_irqs),
	.num_regs = 4,
	.irq_reg_stride = 2,
	.mask_base = TPS65912_INT_MSK,
	.status_base = TPS65912_INT_STS,
	.ack_base = TPS65912_INT_STS,
	.init_ack_masked = true,
};

int tps65912_device_init(struct tps65912 *tps)
{
	int ret;

	ret = regmap_add_irq_chip(tps->regmap, tps->irq, IRQF_ONESHOT, 0,
				  &tps65912_irq_chip, &tps->irq_data);
	if (ret)
		return ret;

	ret = mfd_add_devices(tps->dev, PLATFORM_DEVID_AUTO, tps65912_cells,
			      ARRAY_SIZE(tps65912_cells), NULL, 0,
			      regmap_irq_get_domain(tps->irq_data));
	if (ret) {
		regmap_del_irq_chip(tps->irq, tps->irq_data);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(tps65912_device_init);

int tps65912_device_exit(struct tps65912 *tps)
{
	regmap_del_irq_chip(tps->irq, tps->irq_data);

	return 0;
}
EXPORT_SYMBOL_GPL(tps65912_device_exit);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("TPS65912x MFD Driver");
MODULE_LICENSE("GPL v2");
