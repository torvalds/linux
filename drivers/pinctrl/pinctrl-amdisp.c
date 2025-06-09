/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ISP Pinctrl Driver
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 */

#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "pinctrl-amdisp.h"

#define DRV_NAME		"amdisp-pinctrl"
#define GPIO_CONTROL_PIN	4
#define GPIO_OFFSET_0		0x0
#define GPIO_OFFSET_1		0x4
#define GPIO_OFFSET_2		0x50

static const u32 gpio_offset[] = {
	GPIO_OFFSET_0,
	GPIO_OFFSET_1,
	GPIO_OFFSET_2
};

struct amdisp_pinctrl_data {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	const struct amdisp_function *functions;
	unsigned int nfunctions;
	const struct amdisp_pingroup *groups;
	unsigned int ngroups;
};

static const struct amdisp_pinctrl_data amdisp_pinctrl_data = {
	.pins = amdisp_pins,
	.npins = ARRAY_SIZE(amdisp_pins),
	.functions = amdisp_functions,
	.nfunctions = ARRAY_SIZE(amdisp_functions),
	.groups = amdisp_groups,
	.ngroups = ARRAY_SIZE(amdisp_groups),
};

struct amdisp_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctrl;
	struct pinctrl_desc desc;
	struct pinctrl_gpio_range gpio_range;
	struct gpio_chip gc;
	const struct amdisp_pinctrl_data *data;
	void __iomem *gpiobase;
	raw_spinlock_t lock;
};

static int amdisp_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct amdisp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->data->ngroups;
}

static const char *amdisp_get_group_name(struct pinctrl_dev *pctldev,
					 unsigned int group)
{
	struct amdisp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->data->groups[group].name;
}

static int amdisp_get_group_pins(struct pinctrl_dev *pctldev,
				 unsigned int group,
				 const unsigned int **pins,
				 unsigned int *num_pins)
{
	struct amdisp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pctrl->data->groups[group].pins;
	*num_pins = pctrl->data->groups[group].npins;
	return 0;
}

const struct pinctrl_ops amdisp_pinctrl_ops = {
	.get_groups_count	= amdisp_get_groups_count,
	.get_group_name		= amdisp_get_group_name,
	.get_group_pins		= amdisp_get_group_pins,
};

static int amdisp_gpio_get_direction(struct gpio_chip *gc, unsigned int gpio)
{
	/* amdisp gpio only has output mode */
	return GPIO_LINE_DIRECTION_OUT;
}

static int amdisp_gpio_direction_input(struct gpio_chip *gc, unsigned int gpio)
{
	return -EOPNOTSUPP;
}

static int amdisp_gpio_direction_output(struct gpio_chip *gc, unsigned int gpio,
					int value)
{
	/* Nothing to do, amdisp gpio only has output mode */
	return 0;
}

static int amdisp_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	unsigned long flags;
	u32 pin_reg;
	struct amdisp_pinctrl *pctrl = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	pin_reg = readl(pctrl->gpiobase + gpio_offset[gpio]);
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	return !!(pin_reg & BIT(GPIO_CONTROL_PIN));
}

static void amdisp_gpio_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
	unsigned long flags;
	u32 pin_reg;
	struct amdisp_pinctrl *pctrl = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	pin_reg = readl(pctrl->gpiobase + gpio_offset[gpio]);
	if (value)
		pin_reg |= BIT(GPIO_CONTROL_PIN);
	else
		pin_reg &= ~BIT(GPIO_CONTROL_PIN);
	writel(pin_reg, pctrl->gpiobase + gpio_offset[gpio]);
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int amdisp_gpiochip_add(struct platform_device *pdev,
			       struct amdisp_pinctrl *pctrl)
{
	struct gpio_chip *gc = &pctrl->gc;
	struct pinctrl_gpio_range *grange = &pctrl->gpio_range;
	int ret;

	gc->label		= dev_name(pctrl->dev);
	gc->parent		= &pdev->dev;
	gc->names		= amdisp_range_pins_name;
	gc->request		= gpiochip_generic_request;
	gc->free		= gpiochip_generic_free;
	gc->get_direction	= amdisp_gpio_get_direction;
	gc->direction_input	= amdisp_gpio_direction_input;
	gc->direction_output	= amdisp_gpio_direction_output;
	gc->get			= amdisp_gpio_get;
	gc->set			= amdisp_gpio_set;
	gc->base		= -1;
	gc->ngpio		= ARRAY_SIZE(amdisp_range_pins);

	grange->id		= 0;
	grange->pin_base	= 0;
	grange->base		= 0;
	grange->pins		= amdisp_range_pins;
	grange->npins		= ARRAY_SIZE(amdisp_range_pins);
	grange->name		= gc->label;
	grange->gc		= gc;

	ret = devm_gpiochip_add_data(&pdev->dev, gc, pctrl);
	if (ret)
		return ret;

	pinctrl_add_gpio_range(pctrl->pctrl, grange);

	return 0;
}

static int amdisp_pinctrl_probe(struct platform_device *pdev)
{
	struct amdisp_pinctrl *pctrl;
	struct resource *res;
	int ret;

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pdev->dev.init_name = DRV_NAME;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	pctrl->gpiobase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pctrl->gpiobase))
		return PTR_ERR(pctrl->gpiobase);

	platform_set_drvdata(pdev, pctrl);

	pctrl->dev = &pdev->dev;
	pctrl->data = &amdisp_pinctrl_data;
	pctrl->desc.owner = THIS_MODULE;
	pctrl->desc.pctlops = &amdisp_pinctrl_ops;
	pctrl->desc.pmxops = NULL;
	pctrl->desc.name = dev_name(&pdev->dev);
	pctrl->desc.pins = pctrl->data->pins;
	pctrl->desc.npins = pctrl->data->npins;
	ret = devm_pinctrl_register_and_init(&pdev->dev, &pctrl->desc,
					     pctrl, &pctrl->pctrl);
	if (ret)
		return ret;

	ret = pinctrl_enable(pctrl->pctrl);
	if (ret)
		return ret;

	ret = amdisp_gpiochip_add(pdev, pctrl);
	if (ret)
		return ret;

	return 0;
}

static struct platform_driver amdisp_pinctrl_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = amdisp_pinctrl_probe,
};
module_platform_driver(amdisp_pinctrl_driver);

MODULE_AUTHOR("Benjamin Chan <benjamin.chan@amd.com>");
MODULE_AUTHOR("Pratap Nirujogi <pratap.nirujogi@amd.com>");
MODULE_DESCRIPTION("AMDISP pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
