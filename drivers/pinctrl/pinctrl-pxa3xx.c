/*
 *  linux/drivers/pinctrl/pinctrl-pxa3xx.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 *
 *  Copyright (C) 2011, Marvell Technology Group Ltd.
 *
 *  Author: Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "pinctrl-pxa3xx.h"

static struct pinctrl_gpio_range pxa3xx_pinctrl_gpio_range = {
	.name		= "PXA3xx GPIO",
	.id		= 0,
	.base		= 0,
	.pin_base	= 0,
};

static int pxa3xx_get_groups_count(struct pinctrl_dev *pctrldev)
{
	struct pxa3xx_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);

	return info->num_grps;
}

static const char *pxa3xx_get_group_name(struct pinctrl_dev *pctrldev,
					 unsigned selector)
{
	struct pxa3xx_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);

	return info->grps[selector].name;
}

static int pxa3xx_get_group_pins(struct pinctrl_dev *pctrldev,
				 unsigned selector,
				 const unsigned **pins,
				 unsigned *num_pins)
{
	struct pxa3xx_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);

	*pins = info->grps[selector].pins;
	*num_pins = info->grps[selector].npins;
	return 0;
}

static struct pinctrl_ops pxa3xx_pctrl_ops = {
	.get_groups_count = pxa3xx_get_groups_count,
	.get_group_name	= pxa3xx_get_group_name,
	.get_group_pins	= pxa3xx_get_group_pins,
};

static int pxa3xx_pmx_get_funcs_count(struct pinctrl_dev *pctrldev)
{
	struct pxa3xx_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);

	return info->num_funcs;
}

static const char *pxa3xx_pmx_get_func_name(struct pinctrl_dev *pctrldev,
					    unsigned func)
{
	struct pxa3xx_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	return info->funcs[func].name;
}

static int pxa3xx_pmx_get_groups(struct pinctrl_dev *pctrldev, unsigned func,
				 const char * const **groups,
				 unsigned * const num_groups)
{
	struct pxa3xx_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	*groups = info->funcs[func].groups;
	*num_groups = info->funcs[func].num_groups;
	return 0;
}

/* Return function number. If failure, return negative value. */
static int match_mux(struct pxa3xx_mfp_pin *mfp, unsigned mux)
{
	int i;
	for (i = 0; i < PXA3xx_MAX_MUX; i++) {
		if (mfp->func[i] == mux)
			break;
	}
	if (i >= PXA3xx_MAX_MUX)
		return -EINVAL;
	return i;
}

/* check whether current pin configuration is valid. Negative for failure */
static int match_group_mux(struct pxa3xx_pin_group *grp,
			   struct pxa3xx_pinmux_info *info,
			   unsigned mux)
{
	int i, pin, ret = 0;
	for (i = 0; i < grp->npins; i++) {
		pin = grp->pins[i];
		ret = match_mux(&info->mfp[pin], mux);
		if (ret < 0) {
			dev_err(info->dev, "Can't find mux %d on pin%d\n",
				mux, pin);
			break;
		}
	}
	return ret;
}

static int pxa3xx_pmx_enable(struct pinctrl_dev *pctrldev, unsigned func,
			     unsigned group)
{
	struct pxa3xx_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	struct pxa3xx_pin_group *pin_grp = &info->grps[group];
	unsigned int data;
	int i, mfpr, pin, pin_func;

	if (!pin_grp->npins ||
		(match_group_mux(pin_grp, info, pin_grp->mux) < 0)) {
		dev_err(info->dev, "Failed to set the pin group: %d\n", group);
		return -EINVAL;
	}
	for (i = 0; i < pin_grp->npins; i++) {
		pin = pin_grp->pins[i];
		pin_func = match_mux(&info->mfp[pin], pin_grp->mux);
		mfpr = info->mfp[pin].mfpr;
		data = readl_relaxed(info->virt_base + mfpr);
		data &= ~MFPR_FUNC_MASK;
		data |= pin_func;
		writel_relaxed(data, info->virt_base + mfpr);
	}
	return 0;
}

static int pxa3xx_pmx_request_gpio(struct pinctrl_dev *pctrldev,
				   struct pinctrl_gpio_range *range,
				   unsigned pin)
{
	struct pxa3xx_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	unsigned int data;
	int pin_func, mfpr;

	pin_func = match_mux(&info->mfp[pin], PXA3xx_MUX_GPIO);
	if (pin_func < 0) {
		dev_err(info->dev, "No GPIO function on pin%d (%s)\n",
			pin, info->pads[pin].name);
		return -EINVAL;
	}
	mfpr = info->mfp[pin].mfpr;
	/* write gpio function into mfpr register */
	data = readl_relaxed(info->virt_base + mfpr) & ~MFPR_FUNC_MASK;
	data |= pin_func;
	writel_relaxed(data, info->virt_base + mfpr);
	return 0;
}

static struct pinmux_ops pxa3xx_pmx_ops = {
	.get_functions_count	= pxa3xx_pmx_get_funcs_count,
	.get_function_name	= pxa3xx_pmx_get_func_name,
	.get_function_groups	= pxa3xx_pmx_get_groups,
	.enable			= pxa3xx_pmx_enable,
	.gpio_request_enable	= pxa3xx_pmx_request_gpio,
};

int pxa3xx_pinctrl_register(struct platform_device *pdev,
			    struct pxa3xx_pinmux_info *info)
{
	struct pinctrl_desc *desc;
	struct resource *res;

	if (!info || !info->cputype)
		return -EINVAL;
	desc = info->desc;
	desc->pins = info->pads;
	desc->npins = info->num_pads;
	desc->pctlops = &pxa3xx_pctrl_ops;
	desc->pmxops = &pxa3xx_pmx_ops;
	info->dev = &pdev->dev;
	pxa3xx_pinctrl_gpio_range.npins = info->num_gpio;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOENT;
	info->virt_base = devm_request_and_ioremap(&pdev->dev, res);
	if (!info->virt_base)
		return -ENOMEM;
	info->pctrl = pinctrl_register(desc, &pdev->dev, info);
	if (!info->pctrl) {
		dev_err(&pdev->dev, "failed to register PXA pinmux driver\n");
		return -EINVAL;
	}
	pinctrl_add_gpio_range(info->pctrl, &pxa3xx_pinctrl_gpio_range);
	platform_set_drvdata(pdev, info);
	return 0;
}

int pxa3xx_pinctrl_unregister(struct platform_device *pdev)
{
	struct pxa3xx_pinmux_info *info = platform_get_drvdata(pdev);

	pinctrl_unregister(info->pctrl);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static int __init pxa3xx_pinctrl_init(void)
{
	pr_info("pxa3xx-pinctrl: PXA3xx pinctrl driver initializing\n");
	return 0;
}
core_initcall_sync(pxa3xx_pinctrl_init);

static void __exit pxa3xx_pinctrl_exit(void)
{
}
module_exit(pxa3xx_pinctrl_exit);

MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_DESCRIPTION("PXA3xx pin control driver");
MODULE_LICENSE("GPL v2");
