/*
 * Driver for the NVIDIA Tegra pinmux
 *
 * Copyright (c) 2011, NVIDIA CORPORATION.  All rights reserved.
 *
 * Derived from code:
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2010 NVIDIA Corporation
 * Copyright (C) 2009-2011 ST-Ericsson AB
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>

#include <mach/pinconf-tegra.h>

#include "pinctrl-tegra.h"

#define DRIVER_NAME "tegra-pinmux-disabled"

struct tegra_pmx {
	struct device *dev;
	struct pinctrl_dev *pctl;

	const struct tegra_pinctrl_soc_data *soc;

	int nbanks;
	void __iomem **regs;
};

static inline u32 pmx_readl(struct tegra_pmx *pmx, u32 bank, u32 reg)
{
	return readl(pmx->regs[bank] + reg);
}

static inline void pmx_writel(struct tegra_pmx *pmx, u32 val, u32 bank, u32 reg)
{
	writel(val, pmx->regs[bank] + reg);
}

static int tegra_pinctrl_list_groups(struct pinctrl_dev *pctldev,
				     unsigned group)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	if (group >= pmx->soc->ngroups)
		return -EINVAL;

	return 0;
}

static const char *tegra_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						unsigned group)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	if (group >= pmx->soc->ngroups)
		return NULL;

	return pmx->soc->groups[group].name;
}

static int tegra_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned group,
					const unsigned **pins,
					unsigned *num_pins)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	if (group >= pmx->soc->ngroups)
		return -EINVAL;

	*pins = pmx->soc->groups[group].pins;
	*num_pins = pmx->soc->groups[group].npins;

	return 0;
}

static void tegra_pinctrl_pin_dbg_show(struct pinctrl_dev *pctldev,
				       struct seq_file *s,
				       unsigned offset)
{
	seq_printf(s, " " DRIVER_NAME);
}

static struct pinctrl_ops tegra_pinctrl_ops = {
	.list_groups = tegra_pinctrl_list_groups,
	.get_group_name = tegra_pinctrl_get_group_name,
	.get_group_pins = tegra_pinctrl_get_group_pins,
	.pin_dbg_show = tegra_pinctrl_pin_dbg_show,
};

static int tegra_pinctrl_list_funcs(struct pinctrl_dev *pctldev,
				    unsigned function)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	if (function >= pmx->soc->nfunctions)
		return -EINVAL;

	return 0;
}

static const char *tegra_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
					       unsigned function)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	if (function >= pmx->soc->nfunctions)
		return NULL;

	return pmx->soc->functions[function].name;
}

static int tegra_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
					 unsigned function,
					 const char * const **groups,
					 unsigned * const num_groups)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	if (function >= pmx->soc->nfunctions)
		return -EINVAL;

	*groups = pmx->soc->functions[function].groups;
	*num_groups = pmx->soc->functions[function].ngroups;

	return 0;
}

static int tegra_pinctrl_enable(struct pinctrl_dev *pctldev, unsigned function,
			       unsigned group)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	const struct tegra_pingroup *g;
	int i;
	u32 val;

	if (group >= pmx->soc->ngroups)
		return -EINVAL;
	g = &pmx->soc->groups[group];

	if (g->mux_reg < 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(g->funcs); i++) {
		if (g->funcs[i] == function)
			break;
	}
	if (i == ARRAY_SIZE(g->funcs))
		return -EINVAL;

	val = pmx_readl(pmx, g->mux_bank, g->mux_reg);
	val &= ~(0x3 << g->mux_bit);
	val |= i << g->mux_bit;
	pmx_writel(pmx, val, g->mux_bank, g->mux_reg);

	return 0;
}

static void tegra_pinctrl_disable(struct pinctrl_dev *pctldev,
				  unsigned function, unsigned group)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	const struct tegra_pingroup *g;
	u32 val;

	if (group >= pmx->soc->ngroups)
		return;
	g = &pmx->soc->groups[group];

	if (g->mux_reg < 0)
		return;

	val = pmx_readl(pmx, g->mux_bank, g->mux_reg);
	val &= ~(0x3 << g->mux_bit);
	val |= g->func_safe << g->mux_bit;
	pmx_writel(pmx, val, g->mux_bank, g->mux_reg);
}

static struct pinmux_ops tegra_pinmux_ops = {
	.list_functions = tegra_pinctrl_list_funcs,
	.get_function_name = tegra_pinctrl_get_func_name,
	.get_function_groups = tegra_pinctrl_get_func_groups,
	.enable = tegra_pinctrl_enable,
	.disable = tegra_pinctrl_disable,
};

static int tegra_pinconf_reg(struct tegra_pmx *pmx,
			     const struct tegra_pingroup *g,
			     enum tegra_pinconf_param param,
			     s8 *bank, s16 *reg, s8 *bit, s8 *width)
{
	switch (param) {
	case TEGRA_PINCONF_PARAM_PULL:
		*bank = g->pupd_bank;
		*reg = g->pupd_reg;
		*bit = g->pupd_bit;
		*width = 2;
		break;
	case TEGRA_PINCONF_PARAM_TRISTATE:
		*bank = g->tri_bank;
		*reg = g->tri_reg;
		*bit = g->tri_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_ENABLE_INPUT:
		*bank = g->einput_bank;
		*reg = g->einput_reg;
		*bit = g->einput_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_OPEN_DRAIN:
		*bank = g->odrain_bank;
		*reg = g->odrain_reg;
		*bit = g->odrain_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_LOCK:
		*bank = g->lock_bank;
		*reg = g->lock_reg;
		*bit = g->lock_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_IORESET:
		*bank = g->ioreset_bank;
		*reg = g->ioreset_reg;
		*bit = g->ioreset_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_HIGH_SPEED_MODE:
		*bank = g->drv_bank;
		*reg = g->drv_reg;
		*bit = g->hsm_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_SCHMITT:
		*bank = g->drv_bank;
		*reg = g->drv_reg;
		*bit = g->schmitt_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_LOW_POWER_MODE:
		*bank = g->drv_bank;
		*reg = g->drv_reg;
		*bit = g->lpmd_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_DRIVE_DOWN_STRENGTH:
		*bank = g->drv_bank;
		*reg = g->drv_reg;
		*bit = g->drvdn_bit;
		*width = g->drvdn_width;
		break;
	case TEGRA_PINCONF_PARAM_DRIVE_UP_STRENGTH:
		*bank = g->drv_bank;
		*reg = g->drv_reg;
		*bit = g->drvup_bit;
		*width = g->drvup_width;
		break;
	case TEGRA_PINCONF_PARAM_SLEW_RATE_FALLING:
		*bank = g->drv_bank;
		*reg = g->drv_reg;
		*bit = g->slwf_bit;
		*width = g->slwf_width;
		break;
	case TEGRA_PINCONF_PARAM_SLEW_RATE_RISING:
		*bank = g->drv_bank;
		*reg = g->drv_reg;
		*bit = g->slwr_bit;
		*width = g->slwr_width;
		break;
	default:
		dev_err(pmx->dev, "Invalid config param %04x\n", param);
		return -ENOTSUPP;
	}

	if (*reg < 0) {
		dev_err(pmx->dev,
			"Config param %04x not supported on group %s\n",
			param, g->name);
		return -ENOTSUPP;
	}

	return 0;
}

static int tegra_pinconf_get(struct pinctrl_dev *pctldev,
			     unsigned pin, unsigned long *config)
{
	return -ENOTSUPP;
}

static int tegra_pinconf_set(struct pinctrl_dev *pctldev,
			     unsigned pin, unsigned long config)
{
	return -ENOTSUPP;
}

static int tegra_pinconf_group_get(struct pinctrl_dev *pctldev,
				   unsigned group, unsigned long *config)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	enum tegra_pinconf_param param = TEGRA_PINCONF_UNPACK_PARAM(*config);
	u16 arg;
	const struct tegra_pingroup *g;
	int ret;
	s8 bank, bit, width;
	s16 reg;
	u32 val, mask;

	if (group >= pmx->soc->ngroups)
		return -EINVAL;
	g = &pmx->soc->groups[group];

	ret = tegra_pinconf_reg(pmx, g, param, &bank, &reg, &bit, &width);
	if (ret < 0)
		return ret;

	val = pmx_readl(pmx, bank, reg);
	mask = (1 << width) - 1;
	arg = (val >> bit) & mask;

	*config = TEGRA_PINCONF_PACK(param, arg);

	return 0;
}

static int tegra_pinconf_group_set(struct pinctrl_dev *pctldev,
				   unsigned group, unsigned long config)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	enum tegra_pinconf_param param = TEGRA_PINCONF_UNPACK_PARAM(config);
	u16 arg = TEGRA_PINCONF_UNPACK_ARG(config);
	const struct tegra_pingroup *g;
	int ret;
	s8 bank, bit, width;
	s16 reg;
	u32 val, mask;

	if (group >= pmx->soc->ngroups)
		return -EINVAL;
	g = &pmx->soc->groups[group];

	ret = tegra_pinconf_reg(pmx, g, param, &bank, &reg, &bit, &width);
	if (ret < 0)
		return ret;

	val = pmx_readl(pmx, bank, reg);

	/* LOCK can't be cleared */
	if (param == TEGRA_PINCONF_PARAM_LOCK) {
		if ((val & BIT(bit)) && !arg)
			return -EINVAL;
	}

	/* Special-case Boolean values; allow any non-zero as true */
	if (width == 1)
		arg = !!arg;

	/* Range-check user-supplied value */
	mask = (1 << width) - 1;
	if (arg & ~mask)
		return -EINVAL;

	/* Update register */
	val &= ~(mask << bit);
	val |= arg << bit;
	pmx_writel(pmx, val, bank, reg);

	return 0;
}

static void tegra_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				   struct seq_file *s, unsigned offset)
{
}

static void tegra_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
					 struct seq_file *s, unsigned selector)
{
}

struct pinconf_ops tegra_pinconf_ops = {
	.pin_config_get = tegra_pinconf_get,
	.pin_config_set = tegra_pinconf_set,
	.pin_config_group_get = tegra_pinconf_group_get,
	.pin_config_group_set = tegra_pinconf_group_set,
	.pin_config_dbg_show = tegra_pinconf_dbg_show,
	.pin_config_group_dbg_show = tegra_pinconf_group_dbg_show,
};

static struct pinctrl_gpio_range tegra_pinctrl_gpio_range = {
	.name = "Tegra GPIOs",
	.id = 0,
	.base = 0,
};

static struct pinctrl_desc tegra_pinctrl_desc = {
	.name = DRIVER_NAME,
	.pctlops = &tegra_pinctrl_ops,
	.pmxops = &tegra_pinmux_ops,
	.confops = &tegra_pinconf_ops,
	.owner = THIS_MODULE,
};

static struct of_device_id tegra_pinctrl_of_match[] __devinitdata = {
#ifdef CONFIG_PINCTRL_TEGRA20
	{
		.compatible = "nvidia,tegra20-pinmux-disabled",
		.data = tegra20_pinctrl_init,
	},
#endif
#ifdef CONFIG_PINCTRL_TEGRA30
	{
		.compatible = "nvidia,tegra30-pinmux-disabled",
		.data = tegra30_pinctrl_init,
	},
#endif
	{},
};

static int __devinit tegra_pinctrl_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	tegra_pinctrl_soc_initf initf = NULL;
	struct tegra_pmx *pmx;
	struct resource *res;
	int i;

	match = of_match_device(tegra_pinctrl_of_match, &pdev->dev);
	if (match)
		initf = (tegra_pinctrl_soc_initf)match->data;
#ifdef CONFIG_PINCTRL_TEGRA20
	if (!initf)
		initf = tegra20_pinctrl_init;
#endif
	if (!initf) {
		dev_err(&pdev->dev,
			"Could not determine SoC-specific init func\n");
		return -EINVAL;
	}

	pmx = devm_kzalloc(&pdev->dev, sizeof(*pmx), GFP_KERNEL);
	if (!pmx) {
		dev_err(&pdev->dev, "Can't alloc tegra_pmx\n");
		return -ENOMEM;
	}
	pmx->dev = &pdev->dev;

	(*initf)(&pmx->soc);

	tegra_pinctrl_gpio_range.npins = pmx->soc->ngpios;
	tegra_pinctrl_desc.pins = pmx->soc->pins;
	tegra_pinctrl_desc.npins = pmx->soc->npins;

	for (i = 0; ; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			break;
	}
	pmx->nbanks = i;

	pmx->regs = devm_kzalloc(&pdev->dev, pmx->nbanks * sizeof(*pmx->regs),
				 GFP_KERNEL);
	if (!pmx->regs) {
		dev_err(&pdev->dev, "Can't alloc regs pointer\n");
		return -ENODEV;
	}

	for (i = 0; i < pmx->nbanks; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			dev_err(&pdev->dev, "Missing MEM resource\n");
			return -ENODEV;
		}

		if (!devm_request_mem_region(&pdev->dev, res->start,
					    resource_size(res),
					    dev_name(&pdev->dev))) {
			dev_err(&pdev->dev,
				"Couldn't request MEM resource %d\n", i);
			return -ENODEV;
		}

		pmx->regs[i] = devm_ioremap(&pdev->dev, res->start,
					    resource_size(res));
		if (!pmx->regs[i]) {
			dev_err(&pdev->dev, "Couldn't ioremap regs %d\n", i);
			return -ENODEV;
		}
	}

	pmx->pctl = pinctrl_register(&tegra_pinctrl_desc, &pdev->dev, pmx);
	if (IS_ERR(pmx->pctl)) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return PTR_ERR(pmx->pctl);
	}

	pinctrl_add_gpio_range(pmx->pctl, &tegra_pinctrl_gpio_range);

	platform_set_drvdata(pdev, pmx);

	dev_dbg(&pdev->dev, "Probed Tegra pinctrl driver\n");

	return 0;
}

static int __devexit tegra_pinctrl_remove(struct platform_device *pdev)
{
	struct tegra_pmx *pmx = platform_get_drvdata(pdev);

	pinctrl_remove_gpio_range(pmx->pctl, &tegra_pinctrl_gpio_range);
	pinctrl_unregister(pmx->pctl);

	return 0;
}

static struct platform_driver tegra_pinctrl_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = tegra_pinctrl_of_match,
	},
	.probe = tegra_pinctrl_probe,
	.remove = __devexit_p(tegra_pinctrl_remove),
};

static int __init tegra_pinctrl_init(void)
{
	return platform_driver_register(&tegra_pinctrl_driver);
}
arch_initcall(tegra_pinctrl_init);

static void __exit tegra_pinctrl_exit(void)
{
	platform_driver_unregister(&tegra_pinctrl_driver);
}
module_exit(tegra_pinctrl_exit);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, tegra_pinctrl_of_match);
