/*
 * Pistachio SoC Reset Controller driver
 *
 * Copyright (C) 2015 Imagination Technologies Ltd.
 *
 * Author: Damien Horsley <Damien.Horsley@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>

#include <dt-bindings/reset/pistachio-resets.h>

#define	PISTACHIO_SOFT_RESET		0

struct pistachio_reset_data {
	struct reset_controller_dev	rcdev;
	struct regmap			*periph_regs;
};

static inline int pistachio_reset_shift(unsigned long id)
{
	switch (id) {
	case PISTACHIO_RESET_I2C0:
	case PISTACHIO_RESET_I2C1:
	case PISTACHIO_RESET_I2C2:
	case PISTACHIO_RESET_I2C3:
	case PISTACHIO_RESET_I2S_IN:
	case PISTACHIO_RESET_PRL_OUT:
	case PISTACHIO_RESET_SPDIF_OUT:
	case PISTACHIO_RESET_SPI:
	case PISTACHIO_RESET_PWM_PDM:
	case PISTACHIO_RESET_UART0:
	case PISTACHIO_RESET_UART1:
	case PISTACHIO_RESET_QSPI:
	case PISTACHIO_RESET_MDC:
	case PISTACHIO_RESET_SDHOST:
	case PISTACHIO_RESET_ETHERNET:
	case PISTACHIO_RESET_IR:
	case PISTACHIO_RESET_HASH:
	case PISTACHIO_RESET_TIMER:
		return id;
	case PISTACHIO_RESET_I2S_OUT:
	case PISTACHIO_RESET_SPDIF_IN:
	case PISTACHIO_RESET_EVT:
		return id + 6;
	case PISTACHIO_RESET_USB_H:
	case PISTACHIO_RESET_USB_PR:
	case PISTACHIO_RESET_USB_PHY_PR:
	case PISTACHIO_RESET_USB_PHY_PON:
		return id + 7;
	default:
		return -EINVAL;
	}
}

static int pistachio_reset_assert(struct reset_controller_dev *rcdev,
				  unsigned long id)
{
	struct pistachio_reset_data *rd;
	u32 mask;
	int shift;

	rd = container_of(rcdev, struct pistachio_reset_data, rcdev);
	shift = pistachio_reset_shift(id);
	if (shift < 0)
		return shift;
	mask = BIT(shift);

	return regmap_update_bits(rd->periph_regs, PISTACHIO_SOFT_RESET,
				  mask, mask);
}

static int pistachio_reset_deassert(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	struct pistachio_reset_data *rd;
	u32 mask;
	int shift;

	rd = container_of(rcdev, struct pistachio_reset_data, rcdev);
	shift = pistachio_reset_shift(id);
	if (shift < 0)
		return shift;
	mask = BIT(shift);

	return regmap_update_bits(rd->periph_regs, PISTACHIO_SOFT_RESET,
				  mask, 0);
}

static const struct reset_control_ops pistachio_reset_ops = {
	.assert		= pistachio_reset_assert,
	.deassert	= pistachio_reset_deassert,
};

static int pistachio_reset_probe(struct platform_device *pdev)
{
	struct pistachio_reset_data *rd;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;

	rd = devm_kzalloc(dev, sizeof(*rd), GFP_KERNEL);
	if (!rd)
		return -ENOMEM;

	rd->periph_regs = syscon_node_to_regmap(np->parent);
	if (IS_ERR(rd->periph_regs))
		return PTR_ERR(rd->periph_regs);

	rd->rcdev.owner = THIS_MODULE;
	rd->rcdev.nr_resets = PISTACHIO_RESET_MAX + 1;
	rd->rcdev.ops = &pistachio_reset_ops;
	rd->rcdev.of_node = np;

	return devm_reset_controller_register(dev, &rd->rcdev);
}

static const struct of_device_id pistachio_reset_dt_ids[] = {
	 { .compatible = "img,pistachio-reset", },
	 { /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, pistachio_reset_dt_ids);

static struct platform_driver pistachio_reset_driver = {
	.probe	= pistachio_reset_probe,
	.driver = {
		.name		= "pistachio-reset",
		.of_match_table	= pistachio_reset_dt_ids,
	},
};
module_platform_driver(pistachio_reset_driver);

MODULE_AUTHOR("Damien Horsley <Damien.Horsley@imgtec.com>");
MODULE_DESCRIPTION("Pistacho Reset Controller Driver");
MODULE_LICENSE("GPL v2");
