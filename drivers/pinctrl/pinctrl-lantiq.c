// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/pinctrl/pinctrl-lantiq.c
 *  based on linux/drivers/pinctrl/pinctrl-pxa3xx.c
 *
 *  Copyright (C) 2012 John Crispin <john@phrozen.org>
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "pinctrl-lantiq.h"

static int ltq_get_group_count(struct pinctrl_dev *pctrldev)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	return info->num_grps;
}

static const char *ltq_get_group_name(struct pinctrl_dev *pctrldev,
					 unsigned selector)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	if (selector >= info->num_grps)
		return NULL;
	return info->grps[selector].name;
}

static int ltq_get_group_pins(struct pinctrl_dev *pctrldev,
				 unsigned selector,
				 const unsigned **pins,
				 unsigned *num_pins)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	if (selector >= info->num_grps)
		return -EINVAL;
	*pins = info->grps[selector].pins;
	*num_pins = info->grps[selector].npins;
	return 0;
}

static void ltq_pinctrl_dt_free_map(struct pinctrl_dev *pctldev,
				    struct pinctrl_map *map, unsigned num_maps)
{
	int i;

	for (i = 0; i < num_maps; i++)
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_PIN ||
		    map[i].type == PIN_MAP_TYPE_CONFIGS_GROUP)
			kfree(map[i].data.configs.configs);
	kfree(map);
}

static void ltq_pinctrl_pin_dbg_show(struct pinctrl_dev *pctldev,
					struct seq_file *s,
					unsigned offset)
{
	seq_printf(s, " %s", dev_name(pctldev->dev));
}

static void ltq_pinctrl_dt_subnode_to_map(struct pinctrl_dev *pctldev,
				struct device_node *np,
				struct pinctrl_map **map)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctldev);
	struct property *pins = of_find_property(np, "lantiq,pins", NULL);
	struct property *groups = of_find_property(np, "lantiq,groups", NULL);
	unsigned long configs[3];
	unsigned num_configs = 0;
	struct property *prop;
	const char *group, *pin;
	const char *function;
	int ret, i;

	if (!pins && !groups) {
		dev_err(pctldev->dev, "%pOFn defines neither pins nor groups\n",
			np);
		return;
	}

	if (pins && groups) {
		dev_err(pctldev->dev, "%pOFn defines both pins and groups\n",
			np);
		return;
	}

	ret = of_property_read_string(np, "lantiq,function", &function);
	if (groups && !ret) {
		of_property_for_each_string(np, "lantiq,groups", prop, group) {
			(*map)->type = PIN_MAP_TYPE_MUX_GROUP;
			(*map)->name = function;
			(*map)->data.mux.group = group;
			(*map)->data.mux.function = function;
			(*map)++;
		}
	}

	for (i = 0; i < info->num_params; i++) {
		u32 val;
		int ret = of_property_read_u32(np,
				info->params[i].property, &val);
		if (!ret)
			configs[num_configs++] =
				LTQ_PINCONF_PACK(info->params[i].param,
				val);
	}

	if (!num_configs)
		return;

	of_property_for_each_string(np, "lantiq,pins", prop, pin) {
		(*map)->data.configs.configs = kmemdup(configs,
					num_configs * sizeof(unsigned long),
					GFP_KERNEL);
		(*map)->type = PIN_MAP_TYPE_CONFIGS_PIN;
		(*map)->name = pin;
		(*map)->data.configs.group_or_pin = pin;
		(*map)->data.configs.num_configs = num_configs;
		(*map)++;
	}
	of_property_for_each_string(np, "lantiq,groups", prop, group) {
		(*map)->data.configs.configs = kmemdup(configs,
					num_configs * sizeof(unsigned long),
					GFP_KERNEL);
		(*map)->type = PIN_MAP_TYPE_CONFIGS_GROUP;
		(*map)->name = group;
		(*map)->data.configs.group_or_pin = group;
		(*map)->data.configs.num_configs = num_configs;
		(*map)++;
	}
}

static int ltq_pinctrl_dt_subnode_size(struct device_node *np)
{
	int ret;

	ret = of_property_count_strings(np, "lantiq,groups");
	if (ret < 0)
		ret = of_property_count_strings(np, "lantiq,pins");
	return ret;
}

static int ltq_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
				      struct device_node *np_config,
				      struct pinctrl_map **map,
				      unsigned *num_maps)
{
	struct pinctrl_map *tmp;
	struct device_node *np;
	int max_maps = 0;

	for_each_child_of_node(np_config, np)
		max_maps += ltq_pinctrl_dt_subnode_size(np);
	*map = kzalloc(array3_size(max_maps, sizeof(struct pinctrl_map), 2),
		       GFP_KERNEL);
	if (!*map)
		return -ENOMEM;
	tmp = *map;

	for_each_child_of_node(np_config, np)
		ltq_pinctrl_dt_subnode_to_map(pctldev, np, &tmp);
	*num_maps = ((int)(tmp - *map));

	return 0;
}

static const struct pinctrl_ops ltq_pctrl_ops = {
	.get_groups_count	= ltq_get_group_count,
	.get_group_name		= ltq_get_group_name,
	.get_group_pins		= ltq_get_group_pins,
	.pin_dbg_show		= ltq_pinctrl_pin_dbg_show,
	.dt_node_to_map		= ltq_pinctrl_dt_node_to_map,
	.dt_free_map		= ltq_pinctrl_dt_free_map,
};

static int ltq_pmx_func_count(struct pinctrl_dev *pctrldev)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);

	return info->num_funcs;
}

static const char *ltq_pmx_func_name(struct pinctrl_dev *pctrldev,
					 unsigned selector)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);

	if (selector >= info->num_funcs)
		return NULL;

	return info->funcs[selector].name;
}

static int ltq_pmx_get_groups(struct pinctrl_dev *pctrldev,
				unsigned func,
				const char * const **groups,
				unsigned * const num_groups)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);

	*groups = info->funcs[func].groups;
	*num_groups = info->funcs[func].num_groups;

	return 0;
}

/* Return function number. If failure, return negative value. */
static int match_mux(const struct ltq_mfp_pin *mfp, unsigned mux)
{
	int i;
	for (i = 0; i < LTQ_MAX_MUX; i++) {
		if (mfp->func[i] == mux)
			break;
	}
	if (i >= LTQ_MAX_MUX)
		return -EINVAL;
	return i;
}

/* dont assume .mfp is linearly mapped. find the mfp with the correct .pin */
static int match_mfp(const struct ltq_pinmux_info *info, int pin)
{
	int i;
	for (i = 0; i < info->num_mfp; i++) {
		if (info->mfp[i].pin == pin)
			return i;
	}
	return -1;
}

/* check whether current pin configuration is valid. Negative for failure */
static int match_group_mux(const struct ltq_pin_group *grp,
			   const struct ltq_pinmux_info *info,
			   unsigned mux)
{
	int i, pin, ret = 0;
	for (i = 0; i < grp->npins; i++) {
		pin = match_mfp(info, grp->pins[i]);
		if (pin < 0) {
			dev_err(info->dev, "could not find mfp for pin %d\n",
				grp->pins[i]);
			return -EINVAL;
		}
		ret = match_mux(&info->mfp[pin], mux);
		if (ret < 0) {
			dev_err(info->dev, "Can't find mux %d on pin%d\n",
				mux, pin);
			break;
		}
	}
	return ret;
}

static int ltq_pmx_set(struct pinctrl_dev *pctrldev,
		       unsigned func,
		       unsigned group)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	const struct ltq_pin_group *pin_grp = &info->grps[group];
	int i, pin, pin_func, ret;

	if (!pin_grp->npins ||
		(match_group_mux(pin_grp, info, pin_grp->mux) < 0)) {
		dev_err(info->dev, "Failed to set the pin group: %s\n",
			info->grps[group].name);
		return -EINVAL;
	}
	for (i = 0; i < pin_grp->npins; i++) {
		pin = match_mfp(info, pin_grp->pins[i]);
		if (pin < 0) {
			dev_err(info->dev, "could not find mfp for pin %d\n",
				pin_grp->pins[i]);
			return -EINVAL;
		}
		pin_func = match_mux(&info->mfp[pin], pin_grp->mux);
		ret = info->apply_mux(pctrldev, pin, pin_func);
		if (ret) {
			dev_err(info->dev,
				"failed to apply mux %d for pin %d\n",
				pin_func, pin);
			return ret;
		}
	}
	return 0;
}

static int ltq_pmx_gpio_request_enable(struct pinctrl_dev *pctrldev,
				struct pinctrl_gpio_range *range,
				unsigned pin)
{
	struct ltq_pinmux_info *info = pinctrl_dev_get_drvdata(pctrldev);
	int mfp = match_mfp(info, pin);
	int pin_func;

	if (mfp < 0) {
		dev_err(info->dev, "could not find mfp for pin %d\n", pin);
		return -EINVAL;
	}

	pin_func = match_mux(&info->mfp[mfp], 0);
	if (pin_func < 0) {
		dev_err(info->dev, "No GPIO function on pin%d\n", mfp);
		return -EINVAL;
	}

	return info->apply_mux(pctrldev, mfp, pin_func);
}

static const struct pinmux_ops ltq_pmx_ops = {
	.get_functions_count	= ltq_pmx_func_count,
	.get_function_name	= ltq_pmx_func_name,
	.get_function_groups	= ltq_pmx_get_groups,
	.set_mux		= ltq_pmx_set,
	.gpio_request_enable	= ltq_pmx_gpio_request_enable,
};

/*
 * allow different socs to register with the generic part of the lanti
 * pinctrl code
 */
int ltq_pinctrl_register(struct platform_device *pdev,
				struct ltq_pinmux_info *info)
{
	struct pinctrl_desc *desc;

	if (!info)
		return -EINVAL;
	desc = info->desc;
	desc->pctlops = &ltq_pctrl_ops;
	desc->pmxops = &ltq_pmx_ops;
	info->dev = &pdev->dev;

	info->pctrl = devm_pinctrl_register(&pdev->dev, desc, info);
	if (IS_ERR(info->pctrl)) {
		dev_err(&pdev->dev, "failed to register LTQ pinmux driver\n");
		return PTR_ERR(info->pctrl);
	}
	platform_set_drvdata(pdev, info);
	return 0;
}
