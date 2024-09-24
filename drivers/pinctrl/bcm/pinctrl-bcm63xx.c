// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for BCM63xx GPIO unit (pinctrl + GPIO)
 *
 * Copyright (C) 2021 Álvaro Fernández Rojas <noltari@gmail.com>
 * Copyright (C) 2016 Jonas Gorski <jonas.gorski@gmail.com>
 */

#include <linux/gpio/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "pinctrl-bcm63xx.h"

#define BCM63XX_BANK_SIZE	4

#define BCM63XX_DIROUT_REG	0x04
#define BCM63XX_DATA_REG	0x0c

static int bcm63xx_reg_mask_xlate(struct gpio_regmap *gpio,
				  unsigned int base, unsigned int offset,
				  unsigned int *reg, unsigned int *mask)
{
	unsigned int line = offset % BCM63XX_BANK_GPIOS;
	unsigned int stride = offset / BCM63XX_BANK_GPIOS;

	*reg = base - stride * BCM63XX_BANK_SIZE;
	*mask = BIT(line);

	return 0;
}

static const struct of_device_id bcm63xx_gpio_of_match[] = {
	{ .compatible = "brcm,bcm6318-gpio", },
	{ .compatible = "brcm,bcm6328-gpio", },
	{ .compatible = "brcm,bcm6358-gpio", },
	{ .compatible = "brcm,bcm6362-gpio", },
	{ .compatible = "brcm,bcm6368-gpio", },
	{ .compatible = "brcm,bcm63268-gpio", },
	{ /* sentinel */ }
};

static int bcm63xx_gpio_probe(struct device *dev, struct device_node *node,
			      const struct bcm63xx_pinctrl_soc *soc,
			      struct bcm63xx_pinctrl *pc)
{
	struct gpio_regmap_config grc = {0};

	grc.parent = dev;
	grc.fwnode = &node->fwnode;
	grc.ngpio = soc->ngpios;
	grc.ngpio_per_reg = BCM63XX_BANK_GPIOS;
	grc.regmap = pc->regs;
	grc.reg_dat_base = BCM63XX_DATA_REG;
	grc.reg_dir_out_base = BCM63XX_DIROUT_REG;
	grc.reg_set_base = BCM63XX_DATA_REG;
	grc.reg_mask_xlate = bcm63xx_reg_mask_xlate;

	return PTR_ERR_OR_ZERO(devm_gpio_regmap_register(dev, &grc));
}

int bcm63xx_pinctrl_probe(struct platform_device *pdev,
			  const struct bcm63xx_pinctrl_soc *soc,
			  void *driver_data)
{
	struct device *dev = &pdev->dev;
	struct bcm63xx_pinctrl *pc;
	int err;

	pc = devm_kzalloc(dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	platform_set_drvdata(pdev, pc);

	pc->dev = dev;
	pc->driver_data = driver_data;

	pc->regs = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(pc->regs))
		return PTR_ERR(pc->regs);

	pc->pctl_desc.name = dev_name(dev);
	pc->pctl_desc.pins = soc->pins;
	pc->pctl_desc.npins = soc->npins;
	pc->pctl_desc.pctlops = soc->pctl_ops;
	pc->pctl_desc.pmxops = soc->pmx_ops;
	pc->pctl_desc.owner = THIS_MODULE;

	pc->pctl_dev = devm_pinctrl_register(dev, &pc->pctl_desc, pc);
	if (IS_ERR(pc->pctl_dev))
		return PTR_ERR(pc->pctl_dev);

	for_each_child_of_node_scoped(dev->parent->of_node, node) {
		if (of_match_node(bcm63xx_gpio_of_match, node)) {
			err = bcm63xx_gpio_probe(dev, node, soc, pc);
			if (err) {
				dev_err(dev, "could not add GPIO chip\n");
				return err;
			}
		}
	}

	return 0;
}
