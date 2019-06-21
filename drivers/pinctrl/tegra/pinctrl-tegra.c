/*
 * Driver for the NVIDIA Tegra pinmux
 *
 * Copyright (c) 2011-2012, NVIDIA CORPORATION.  All rights reserved.
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
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/slab.h>

#include "../core.h"
#include "../pinctrl-utils.h"
#include "pinctrl-tegra.h"

static inline u32 pmx_readl(struct tegra_pmx *pmx, u32 bank, u32 reg)
{
	return readl(pmx->regs[bank] + reg);
}

static inline void pmx_writel(struct tegra_pmx *pmx, u32 val, u32 bank, u32 reg)
{
	writel(val, pmx->regs[bank] + reg);
}

static int tegra_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->soc->ngroups;
}

static const char *tegra_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						unsigned group)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->soc->groups[group].name;
}

static int tegra_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned group,
					const unsigned **pins,
					unsigned *num_pins)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	*pins = pmx->soc->groups[group].pins;
	*num_pins = pmx->soc->groups[group].npins;

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void tegra_pinctrl_pin_dbg_show(struct pinctrl_dev *pctldev,
				       struct seq_file *s,
				       unsigned offset)
{
	seq_printf(s, " %s", dev_name(pctldev->dev));
}
#endif

static const struct cfg_param {
	const char *property;
	enum tegra_pinconf_param param;
} cfg_params[] = {
	{"nvidia,pull",			TEGRA_PINCONF_PARAM_PULL},
	{"nvidia,tristate",		TEGRA_PINCONF_PARAM_TRISTATE},
	{"nvidia,enable-input",		TEGRA_PINCONF_PARAM_ENABLE_INPUT},
	{"nvidia,open-drain",		TEGRA_PINCONF_PARAM_OPEN_DRAIN},
	{"nvidia,lock",			TEGRA_PINCONF_PARAM_LOCK},
	{"nvidia,io-reset",		TEGRA_PINCONF_PARAM_IORESET},
	{"nvidia,rcv-sel",		TEGRA_PINCONF_PARAM_RCV_SEL},
	{"nvidia,io-hv",		TEGRA_PINCONF_PARAM_RCV_SEL},
	{"nvidia,high-speed-mode",	TEGRA_PINCONF_PARAM_HIGH_SPEED_MODE},
	{"nvidia,schmitt",		TEGRA_PINCONF_PARAM_SCHMITT},
	{"nvidia,low-power-mode",	TEGRA_PINCONF_PARAM_LOW_POWER_MODE},
	{"nvidia,pull-down-strength",	TEGRA_PINCONF_PARAM_DRIVE_DOWN_STRENGTH},
	{"nvidia,pull-up-strength",	TEGRA_PINCONF_PARAM_DRIVE_UP_STRENGTH},
	{"nvidia,slew-rate-falling",	TEGRA_PINCONF_PARAM_SLEW_RATE_FALLING},
	{"nvidia,slew-rate-rising",	TEGRA_PINCONF_PARAM_SLEW_RATE_RISING},
	{"nvidia,drive-type",		TEGRA_PINCONF_PARAM_DRIVE_TYPE},
};

static int tegra_pinctrl_dt_subnode_to_map(struct pinctrl_dev *pctldev,
					   struct device_node *np,
					   struct pinctrl_map **map,
					   unsigned *reserved_maps,
					   unsigned *num_maps)
{
	struct device *dev = pctldev->dev;
	int ret, i;
	const char *function;
	u32 val;
	unsigned long config;
	unsigned long *configs = NULL;
	unsigned num_configs = 0;
	unsigned reserve;
	struct property *prop;
	const char *group;

	ret = of_property_read_string(np, "nvidia,function", &function);
	if (ret < 0) {
		/* EINVAL=missing, which is fine since it's optional */
		if (ret != -EINVAL)
			dev_err(dev,
				"could not parse property nvidia,function\n");
		function = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(cfg_params); i++) {
		ret = of_property_read_u32(np, cfg_params[i].property, &val);
		if (!ret) {
			config = TEGRA_PINCONF_PACK(cfg_params[i].param, val);
			ret = pinctrl_utils_add_config(pctldev, &configs,
					&num_configs, config);
			if (ret < 0)
				goto exit;
		/* EINVAL=missing, which is fine since it's optional */
		} else if (ret != -EINVAL) {
			dev_err(dev, "could not parse property %s\n",
				cfg_params[i].property);
		}
	}

	reserve = 0;
	if (function != NULL)
		reserve++;
	if (num_configs)
		reserve++;
	ret = of_property_count_strings(np, "nvidia,pins");
	if (ret < 0) {
		dev_err(dev, "could not parse property nvidia,pins\n");
		goto exit;
	}
	reserve *= ret;

	ret = pinctrl_utils_reserve_map(pctldev, map, reserved_maps,
					num_maps, reserve);
	if (ret < 0)
		goto exit;

	of_property_for_each_string(np, "nvidia,pins", prop, group) {
		if (function) {
			ret = pinctrl_utils_add_map_mux(pctldev, map,
					reserved_maps, num_maps, group,
					function);
			if (ret < 0)
				goto exit;
		}

		if (num_configs) {
			ret = pinctrl_utils_add_map_configs(pctldev, map,
					reserved_maps, num_maps, group,
					configs, num_configs,
					PIN_MAP_TYPE_CONFIGS_GROUP);
			if (ret < 0)
				goto exit;
		}
	}

	ret = 0;

exit:
	kfree(configs);
	return ret;
}

static int tegra_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
					struct device_node *np_config,
					struct pinctrl_map **map,
					unsigned *num_maps)
{
	unsigned reserved_maps;
	struct device_node *np;
	int ret;

	reserved_maps = 0;
	*map = NULL;
	*num_maps = 0;

	for_each_child_of_node(np_config, np) {
		ret = tegra_pinctrl_dt_subnode_to_map(pctldev, np, map,
						      &reserved_maps, num_maps);
		if (ret < 0) {
			pinctrl_utils_free_map(pctldev, *map,
				*num_maps);
			of_node_put(np);
			return ret;
		}
	}

	return 0;
}

static const struct pinctrl_ops tegra_pinctrl_ops = {
	.get_groups_count = tegra_pinctrl_get_groups_count,
	.get_group_name = tegra_pinctrl_get_group_name,
	.get_group_pins = tegra_pinctrl_get_group_pins,
#ifdef CONFIG_DEBUG_FS
	.pin_dbg_show = tegra_pinctrl_pin_dbg_show,
#endif
	.dt_node_to_map = tegra_pinctrl_dt_node_to_map,
	.dt_free_map = pinctrl_utils_free_map,
};

static int tegra_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->soc->nfunctions;
}

static const char *tegra_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
					       unsigned function)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->soc->functions[function].name;
}

static int tegra_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
					 unsigned function,
					 const char * const **groups,
					 unsigned * const num_groups)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	*groups = pmx->soc->functions[function].groups;
	*num_groups = pmx->soc->functions[function].ngroups;

	return 0;
}

static int tegra_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				 unsigned function,
				 unsigned group)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	const struct tegra_pingroup *g;
	int i;
	u32 val;

	g = &pmx->soc->groups[group];

	if (WARN_ON(g->mux_reg < 0))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(g->funcs); i++) {
		if (g->funcs[i] == function)
			break;
	}
	if (WARN_ON(i == ARRAY_SIZE(g->funcs)))
		return -EINVAL;

	val = pmx_readl(pmx, g->mux_bank, g->mux_reg);
	val &= ~(0x3 << g->mux_bit);
	val |= i << g->mux_bit;
	pmx_writel(pmx, val, g->mux_bank, g->mux_reg);

	return 0;
}

static const struct pinmux_ops tegra_pinmux_ops = {
	.get_functions_count = tegra_pinctrl_get_funcs_count,
	.get_function_name = tegra_pinctrl_get_func_name,
	.get_function_groups = tegra_pinctrl_get_func_groups,
	.set_mux = tegra_pinctrl_set_mux,
};

static int tegra_pinconf_reg(struct tegra_pmx *pmx,
			     const struct tegra_pingroup *g,
			     enum tegra_pinconf_param param,
			     bool report_err,
			     s8 *bank, s32 *reg, s8 *bit, s8 *width)
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
		*bank = g->mux_bank;
		*reg = g->mux_reg;
		*bit = g->einput_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_OPEN_DRAIN:
		*bank = g->mux_bank;
		*reg = g->mux_reg;
		*bit = g->odrain_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_LOCK:
		*bank = g->mux_bank;
		*reg = g->mux_reg;
		*bit = g->lock_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_IORESET:
		*bank = g->mux_bank;
		*reg = g->mux_reg;
		*bit = g->ioreset_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_RCV_SEL:
		*bank = g->mux_bank;
		*reg = g->mux_reg;
		*bit = g->rcv_sel_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_HIGH_SPEED_MODE:
		if (pmx->soc->hsm_in_mux) {
			*bank = g->mux_bank;
			*reg = g->mux_reg;
		} else {
			*bank = g->drv_bank;
			*reg = g->drv_reg;
		}
		*bit = g->hsm_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_SCHMITT:
		if (pmx->soc->schmitt_in_mux) {
			*bank = g->mux_bank;
			*reg = g->mux_reg;
		} else {
			*bank = g->drv_bank;
			*reg = g->drv_reg;
		}
		*bit = g->schmitt_bit;
		*width = 1;
		break;
	case TEGRA_PINCONF_PARAM_LOW_POWER_MODE:
		*bank = g->drv_bank;
		*reg = g->drv_reg;
		*bit = g->lpmd_bit;
		*width = 2;
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
	case TEGRA_PINCONF_PARAM_DRIVE_TYPE:
		if (pmx->soc->drvtype_in_mux) {
			*bank = g->mux_bank;
			*reg = g->mux_reg;
		} else {
			*bank = g->drv_bank;
			*reg = g->drv_reg;
		}
		*bit = g->drvtype_bit;
		*width = 2;
		break;
	default:
		dev_err(pmx->dev, "Invalid config param %04x\n", param);
		return -ENOTSUPP;
	}

	if (*reg < 0 || *bit < 0)  {
		if (report_err) {
			const char *prop = "unknown";
			int i;

			for (i = 0; i < ARRAY_SIZE(cfg_params); i++) {
				if (cfg_params[i].param == param) {
					prop = cfg_params[i].property;
					break;
				}
			}

			dev_err(pmx->dev,
				"Config param %04x (%s) not supported on group %s\n",
				param, prop, g->name);
		}
		return -ENOTSUPP;
	}

	return 0;
}

static int tegra_pinconf_get(struct pinctrl_dev *pctldev,
			     unsigned pin, unsigned long *config)
{
	dev_err(pctldev->dev, "pin_config_get op not supported\n");
	return -ENOTSUPP;
}

static int tegra_pinconf_set(struct pinctrl_dev *pctldev,
			     unsigned pin, unsigned long *configs,
			     unsigned num_configs)
{
	dev_err(pctldev->dev, "pin_config_set op not supported\n");
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
	s32 reg;
	u32 val, mask;

	g = &pmx->soc->groups[group];

	ret = tegra_pinconf_reg(pmx, g, param, true, &bank, &reg, &bit,
				&width);
	if (ret < 0)
		return ret;

	val = pmx_readl(pmx, bank, reg);
	mask = (1 << width) - 1;
	arg = (val >> bit) & mask;

	*config = TEGRA_PINCONF_PACK(param, arg);

	return 0;
}

static int tegra_pinconf_group_set(struct pinctrl_dev *pctldev,
				   unsigned group, unsigned long *configs,
				   unsigned num_configs)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	enum tegra_pinconf_param param;
	u16 arg;
	const struct tegra_pingroup *g;
	int ret, i;
	s8 bank, bit, width;
	s32 reg;
	u32 val, mask;

	g = &pmx->soc->groups[group];

	for (i = 0; i < num_configs; i++) {
		param = TEGRA_PINCONF_UNPACK_PARAM(configs[i]);
		arg = TEGRA_PINCONF_UNPACK_ARG(configs[i]);

		ret = tegra_pinconf_reg(pmx, g, param, true, &bank, &reg, &bit,
					&width);
		if (ret < 0)
			return ret;

		val = pmx_readl(pmx, bank, reg);

		/* LOCK can't be cleared */
		if (param == TEGRA_PINCONF_PARAM_LOCK) {
			if ((val & BIT(bit)) && !arg) {
				dev_err(pctldev->dev, "LOCK bit cannot be cleared\n");
				return -EINVAL;
			}
		}

		/* Special-case Boolean values; allow any non-zero as true */
		if (width == 1)
			arg = !!arg;

		/* Range-check user-supplied value */
		mask = (1 << width) - 1;
		if (arg & ~mask) {
			dev_err(pctldev->dev,
				"config %lx: %x too big for %d bit register\n",
				configs[i], arg, width);
			return -EINVAL;
		}

		/* Update register */
		val &= ~(mask << bit);
		val |= arg << bit;
		pmx_writel(pmx, val, bank, reg);
	} /* for each config */

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void tegra_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				   struct seq_file *s, unsigned offset)
{
}

static const char *strip_prefix(const char *s)
{
	const char *comma = strchr(s, ',');
	if (!comma)
		return s;

	return comma + 1;
}

static void tegra_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
					 struct seq_file *s, unsigned group)
{
	struct tegra_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	const struct tegra_pingroup *g;
	int i, ret;
	s8 bank, bit, width;
	s32 reg;
	u32 val;

	g = &pmx->soc->groups[group];

	for (i = 0; i < ARRAY_SIZE(cfg_params); i++) {
		ret = tegra_pinconf_reg(pmx, g, cfg_params[i].param, false,
					&bank, &reg, &bit, &width);
		if (ret < 0)
			continue;

		val = pmx_readl(pmx, bank, reg);
		val >>= bit;
		val &= (1 << width) - 1;

		seq_printf(s, "\n\t%s=%u",
			   strip_prefix(cfg_params[i].property), val);
	}
}

static void tegra_pinconf_config_dbg_show(struct pinctrl_dev *pctldev,
					  struct seq_file *s,
					  unsigned long config)
{
	enum tegra_pinconf_param param = TEGRA_PINCONF_UNPACK_PARAM(config);
	u16 arg = TEGRA_PINCONF_UNPACK_ARG(config);
	const char *pname = "unknown";
	int i;

	for (i = 0; i < ARRAY_SIZE(cfg_params); i++) {
		if (cfg_params[i].param == param) {
			pname = cfg_params[i].property;
			break;
		}
	}

	seq_printf(s, "%s=%d", strip_prefix(pname), arg);
}
#endif

static const struct pinconf_ops tegra_pinconf_ops = {
	.pin_config_get = tegra_pinconf_get,
	.pin_config_set = tegra_pinconf_set,
	.pin_config_group_get = tegra_pinconf_group_get,
	.pin_config_group_set = tegra_pinconf_group_set,
#ifdef CONFIG_DEBUG_FS
	.pin_config_dbg_show = tegra_pinconf_dbg_show,
	.pin_config_group_dbg_show = tegra_pinconf_group_dbg_show,
	.pin_config_config_dbg_show = tegra_pinconf_config_dbg_show,
#endif
};

static struct pinctrl_gpio_range tegra_pinctrl_gpio_range = {
	.name = "Tegra GPIOs",
	.id = 0,
	.base = 0,
};

static struct pinctrl_desc tegra_pinctrl_desc = {
	.pctlops = &tegra_pinctrl_ops,
	.pmxops = &tegra_pinmux_ops,
	.confops = &tegra_pinconf_ops,
	.owner = THIS_MODULE,
};

static void tegra_pinctrl_clear_parked_bits(struct tegra_pmx *pmx)
{
	int i = 0;
	const struct tegra_pingroup *g;
	u32 val;

	for (i = 0; i < pmx->soc->ngroups; ++i) {
		g = &pmx->soc->groups[i];
		if (g->parked_bitmask > 0) {
			unsigned int bank, reg;

			if (g->mux_reg != -1) {
				bank = g->mux_bank;
				reg = g->mux_reg;
			} else {
				bank = g->drv_bank;
				reg = g->drv_reg;
			}

			val = pmx_readl(pmx, bank, reg);
			val &= ~g->parked_bitmask;
			pmx_writel(pmx, val, bank, reg);
		}
	}
}

static bool gpio_node_has_range(const char *compatible)
{
	struct device_node *np;
	bool has_prop = false;

	np = of_find_compatible_node(NULL, NULL, compatible);
	if (!np)
		return has_prop;

	has_prop = of_find_property(np, "gpio-ranges", NULL);

	of_node_put(np);

	return has_prop;
}

int tegra_pinctrl_probe(struct platform_device *pdev,
			const struct tegra_pinctrl_soc_data *soc_data)
{
	struct tegra_pmx *pmx;
	struct resource *res;
	int i;
	const char **group_pins;
	int fn, gn, gfn;

	pmx = devm_kzalloc(&pdev->dev, sizeof(*pmx), GFP_KERNEL);
	if (!pmx)
		return -ENOMEM;

	pmx->dev = &pdev->dev;
	pmx->soc = soc_data;

	/*
	 * Each mux group will appear in 4 functions' list of groups.
	 * This over-allocates slightly, since not all groups are mux groups.
	 */
	pmx->group_pins = devm_kcalloc(&pdev->dev,
		soc_data->ngroups * 4, sizeof(*pmx->group_pins),
		GFP_KERNEL);
	if (!pmx->group_pins)
		return -ENOMEM;

	group_pins = pmx->group_pins;
	for (fn = 0; fn < soc_data->nfunctions; fn++) {
		struct tegra_function *func = &soc_data->functions[fn];

		func->groups = group_pins;

		for (gn = 0; gn < soc_data->ngroups; gn++) {
			const struct tegra_pingroup *g = &soc_data->groups[gn];

			if (g->mux_reg == -1)
				continue;

			for (gfn = 0; gfn < 4; gfn++)
				if (g->funcs[gfn] == fn)
					break;
			if (gfn == 4)
				continue;

			BUG_ON(group_pins - pmx->group_pins >=
				soc_data->ngroups * 4);
			*group_pins++ = g->name;
			func->ngroups++;
		}
	}

	tegra_pinctrl_gpio_range.npins = pmx->soc->ngpios;
	tegra_pinctrl_desc.name = dev_name(&pdev->dev);
	tegra_pinctrl_desc.pins = pmx->soc->pins;
	tegra_pinctrl_desc.npins = pmx->soc->npins;

	for (i = 0; ; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			break;
	}
	pmx->nbanks = i;

	pmx->regs = devm_kcalloc(&pdev->dev, pmx->nbanks, sizeof(*pmx->regs),
				 GFP_KERNEL);
	if (!pmx->regs)
		return -ENOMEM;

	for (i = 0; i < pmx->nbanks; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		pmx->regs[i] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(pmx->regs[i]))
			return PTR_ERR(pmx->regs[i]);
	}

	pmx->pctl = devm_pinctrl_register(&pdev->dev, &tegra_pinctrl_desc, pmx);
	if (IS_ERR(pmx->pctl)) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return PTR_ERR(pmx->pctl);
	}

	tegra_pinctrl_clear_parked_bits(pmx);

	if (!gpio_node_has_range(pmx->soc->gpio_compatible))
		pinctrl_add_gpio_range(pmx->pctl, &tegra_pinctrl_gpio_range);

	platform_set_drvdata(pdev, pmx);

	dev_dbg(&pdev->dev, "Probed Tegra pinctrl driver\n");

	return 0;
}
