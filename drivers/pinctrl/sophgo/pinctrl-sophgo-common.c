// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo SoCs pinctrl common ops.
 *
 * Copyright (C) 2024 Inochi Amaoto <inochiama@outlook.com>
 *
 */

#include <linux/bsearch.h>
#include <linux/cleanup.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>

#include "../pinctrl-utils.h"
#include "../pinconf.h"
#include "../pinmux.h"

#include "pinctrl-sophgo.h"

static u16 sophgo_dt_get_pin(u32 value)
{
	return value;
}

static int sophgo_cmp_pin(const void *key, const void *pivot)
{
	const struct sophgo_pin *pin = pivot;
	int pin_id = (long)key;
	int pivid = pin->id;

	return pin_id - pivid;
}

const struct sophgo_pin *sophgo_get_pin(struct sophgo_pinctrl *pctrl,
					unsigned long pin_id)
{
	return bsearch((void *)pin_id, pctrl->data->pindata, pctrl->data->npins,
		       pctrl->data->pinsize, sophgo_cmp_pin);
}

static int sophgo_verify_pinmux_config(struct sophgo_pinctrl *pctrl,
				       const struct sophgo_pin_mux_config *config)
{
	if (pctrl->data->cfg_ops->verify_pinmux_config)
		return pctrl->data->cfg_ops->verify_pinmux_config(config);
	return 0;
}

static int sophgo_verify_pin_group(struct sophgo_pinctrl *pctrl,
				   const struct sophgo_pin_mux_config *config,
				   unsigned int npins)
{
	if (pctrl->data->cfg_ops->verify_pin_group)
		return pctrl->data->cfg_ops->verify_pin_group(config, npins);
	return 0;
}

static int sophgo_dt_node_to_map_post(struct device_node *cur,
				      struct sophgo_pinctrl *pctrl,
				      struct sophgo_pin_mux_config *config,
				      unsigned int npins)
{
	if (pctrl->data->cfg_ops->dt_node_to_map_post)
		return pctrl->data->cfg_ops->dt_node_to_map_post(cur, pctrl,
								 config, npins);
	return 0;
}

int sophgo_pctrl_dt_node_to_map(struct pinctrl_dev *pctldev, struct device_node *np,
				struct pinctrl_map **maps, unsigned int *num_maps)
{
	struct sophgo_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = pctrl->dev;
	struct device_node *child;
	struct pinctrl_map *map;
	const char **grpnames;
	const char *grpname;
	int ngroups = 0;
	int nmaps = 0;
	int ret;

	for_each_available_child_of_node(np, child)
		ngroups += 1;

	grpnames = devm_kcalloc(dev, ngroups, sizeof(*grpnames), GFP_KERNEL);
	if (!grpnames)
		return -ENOMEM;

	map = kcalloc(ngroups * 2, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	ngroups = 0;
	guard(mutex)(&pctrl->mutex);
	for_each_available_child_of_node(np, child) {
		int npins = of_property_count_u32_elems(child, "pinmux");
		unsigned int *pins;
		struct sophgo_pin_mux_config *pinmuxs;
		u32 config;
		int i;

		if (npins < 1) {
			dev_err(dev, "invalid pinctrl group %pOFn.%pOFn\n",
				np, child);
			ret = -EINVAL;
			goto dt_failed;
		}

		grpname = devm_kasprintf(dev, GFP_KERNEL, "%pOFn.%pOFn",
					 np, child);
		if (!grpname) {
			ret = -ENOMEM;
			goto dt_failed;
		}

		grpnames[ngroups++] = grpname;

		pins = devm_kcalloc(dev, npins, sizeof(*pins), GFP_KERNEL);
		if (!pins) {
			ret = -ENOMEM;
			goto dt_failed;
		}

		pinmuxs = devm_kcalloc(dev, npins, sizeof(*pinmuxs), GFP_KERNEL);
		if (!pinmuxs) {
			ret = -ENOMEM;
			goto dt_failed;
		}

		for (i = 0; i < npins; i++) {
			ret = of_property_read_u32_index(child, "pinmux",
							 i, &config);
			if (ret)
				goto dt_failed;

			pins[i] = sophgo_dt_get_pin(config);
			pinmuxs[i].config = config;
			pinmuxs[i].pin = sophgo_get_pin(pctrl, pins[i]);

			if (!pinmuxs[i].pin) {
				dev_err(dev, "failed to get pin %d\n", pins[i]);
				ret = -ENODEV;
				goto dt_failed;
			}

			ret = sophgo_verify_pinmux_config(pctrl, &pinmuxs[i]);
			if (ret) {
				dev_err(dev, "group %s pin %d is invalid\n",
					grpname, i);
				goto dt_failed;
			}
		}

		ret = sophgo_verify_pin_group(pctrl, pinmuxs, npins);
		if (ret) {
			dev_err(dev, "group %s is invalid\n", grpname);
			goto dt_failed;
		}

		ret = sophgo_dt_node_to_map_post(child, pctrl, pinmuxs, npins);
		if (ret)
			goto dt_failed;

		map[nmaps].type = PIN_MAP_TYPE_MUX_GROUP;
		map[nmaps].data.mux.function = np->name;
		map[nmaps].data.mux.group = grpname;
		nmaps += 1;

		ret = pinconf_generic_parse_dt_config(child, pctldev,
						      &map[nmaps].data.configs.configs,
						      &map[nmaps].data.configs.num_configs);
		if (ret) {
			dev_err(dev, "failed to parse pin config of group %s: %d\n",
				grpname, ret);
			goto dt_failed;
		}

		ret = pinctrl_generic_add_group(pctldev, grpname,
						pins, npins, pinmuxs);
		if (ret < 0) {
			dev_err(dev, "failed to add group %s: %d\n", grpname, ret);
			goto dt_failed;
		}

		/* don't create a map if there are no pinconf settings */
		if (map[nmaps].data.configs.num_configs == 0)
			continue;

		map[nmaps].type = PIN_MAP_TYPE_CONFIGS_GROUP;
		map[nmaps].data.configs.group_or_pin = grpname;
		nmaps += 1;
	}

	ret = pinmux_generic_add_function(pctldev, np->name,
					  grpnames, ngroups, NULL);
	if (ret < 0) {
		dev_err(dev, "error adding function %s: %d\n", np->name, ret);
		goto function_failed;
	}

	*maps = map;
	*num_maps = nmaps;

	return 0;

dt_failed:
	of_node_put(child);
function_failed:
	pinctrl_utils_free_map(pctldev, map, nmaps);
	return ret;
}

int sophgo_pmx_set_mux(struct pinctrl_dev *pctldev,
		       unsigned int fsel, unsigned int gsel)
{
	struct sophgo_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct group_desc *group;
	const struct sophgo_pin_mux_config *configs;
	unsigned int i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	configs = group->data;

	for (i = 0; i < group->grp.npins; i++) {
		const struct sophgo_pin *pin = configs[i].pin;
		u32 value = configs[i].config;

		guard(raw_spinlock_irqsave)(&pctrl->lock);

		pctrl->data->cfg_ops->set_pinmux_config(pctrl, pin, value);
	}

	return 0;
}

static int sophgo_pin_set_config(struct sophgo_pinctrl *pctrl,
				 unsigned int pin_id,
				 u32 value, u32 mask)
{
	const struct sophgo_pin *pin = sophgo_get_pin(pctrl, pin_id);

	if (!pin)
		return -EINVAL;

	guard(raw_spinlock_irqsave)(&pctrl->lock);

	return pctrl->data->cfg_ops->set_pinconf_config(pctrl, pin, value, mask);
}

int sophgo_pconf_set(struct pinctrl_dev *pctldev, unsigned int pin_id,
		     unsigned long *configs, unsigned int num_configs)
{
	struct sophgo_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct sophgo_pin *pin = sophgo_get_pin(pctrl, pin_id);
	u32 value, mask;

	if (!pin)
		return -ENODEV;

	if (pctrl->data->cfg_ops->compute_pinconf_config(pctrl, pin,
							 configs, num_configs,
							 &value, &mask))
		return -ENOTSUPP;

	return sophgo_pin_set_config(pctrl, pin_id, value, mask);
}

int sophgo_pconf_group_set(struct pinctrl_dev *pctldev, unsigned int gsel,
			   unsigned long *configs, unsigned int num_configs)
{
	struct sophgo_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct group_desc *group;
	const struct sophgo_pin_mux_config *pinmuxs;
	u32 value, mask;
	int i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	pinmuxs = group->data;

	if (pctrl->data->cfg_ops->compute_pinconf_config(pctrl, pinmuxs[0].pin,
							 configs, num_configs,
							 &value, &mask))
		return -ENOTSUPP;

	for (i = 0; i < group->grp.npins; i++)
		sophgo_pin_set_config(pctrl,  group->grp.pins[i], value, mask);

	return 0;
}

u32 sophgo_pinctrl_typical_pull_down(struct sophgo_pinctrl *pctrl,
				     const struct sophgo_pin *pin,
				     const u32 *power_cfg)
{
	return pctrl->data->vddio_ops->get_pull_down(pin, power_cfg);
}

u32 sophgo_pinctrl_typical_pull_up(struct sophgo_pinctrl *pctrl,
				   const struct sophgo_pin *pin,
				   const u32 *power_cfg)
{
	return pctrl->data->vddio_ops->get_pull_up(pin, power_cfg);
}

int sophgo_pinctrl_oc2reg(struct sophgo_pinctrl *pctrl,
			  const struct sophgo_pin *pin,
			  const u32 *power_cfg, u32 target)
{
	const u32 *map;
	int i, len;

	if (!pctrl->data->vddio_ops->get_oc_map)
		return -ENOTSUPP;

	len = pctrl->data->vddio_ops->get_oc_map(pin, power_cfg, &map);
	if (len < 0)
		return len;

	for (i = 0; i < len; i++) {
		if (map[i] >= target)
			return i;
	}

	return -EINVAL;
}

int sophgo_pinctrl_reg2oc(struct sophgo_pinctrl *pctrl,
			  const struct sophgo_pin *pin,
			  const u32 *power_cfg, u32 reg)
{
	const u32 *map;
	int len;

	if (!pctrl->data->vddio_ops->get_oc_map)
		return -ENOTSUPP;

	len = pctrl->data->vddio_ops->get_oc_map(pin, power_cfg, &map);
	if (len < 0)
		return len;

	if (reg >= len)
		return -EINVAL;

	return map[reg];
}

int sophgo_pinctrl_schmitt2reg(struct sophgo_pinctrl *pctrl,
			       const struct sophgo_pin *pin,
			       const u32 *power_cfg, u32 target)
{
	const u32 *map;
	int i, len;

	if (!pctrl->data->vddio_ops->get_schmitt_map)
		return -ENOTSUPP;

	len = pctrl->data->vddio_ops->get_schmitt_map(pin, power_cfg, &map);
	if (len < 0)
		return len;

	for (i = 0; i < len; i++) {
		if (map[i] == target)
			return i;
	}

	return -EINVAL;
}

int sophgo_pinctrl_reg2schmitt(struct sophgo_pinctrl *pctrl,
			       const struct sophgo_pin *pin,
			       const u32 *power_cfg, u32 reg)
{
	const u32 *map;
	int len;

	if (!pctrl->data->vddio_ops->get_schmitt_map)
		return -ENOTSUPP;

	len = pctrl->data->vddio_ops->get_schmitt_map(pin, power_cfg, &map);
	if (len < 0)
		return len;

	if (reg >= len)
		return -EINVAL;

	return map[reg];
}

int sophgo_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sophgo_pinctrl *pctrl;
	const struct sophgo_pinctrl_data *pctrl_data;
	int ret;

	pctrl_data = device_get_match_data(dev);
	if (!pctrl_data)
		return -ENODEV;

	if (pctrl_data->npins == 0)
		return dev_err_probe(dev, -EINVAL, "invalid pin data\n");

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->pdesc.name = dev_name(dev);
	pctrl->pdesc.pins = pctrl_data->pins;
	pctrl->pdesc.npins = pctrl_data->npins;
	pctrl->pdesc.pctlops = pctrl_data->pctl_ops;
	pctrl->pdesc.pmxops = pctrl_data->pmx_ops;
	pctrl->pdesc.confops = pctrl_data->pconf_ops;
	pctrl->pdesc.owner = THIS_MODULE;

	pctrl->data = pctrl_data;
	pctrl->dev = dev;
	raw_spin_lock_init(&pctrl->lock);
	mutex_init(&pctrl->mutex);

	ret = pctrl->data->cfg_ops->pctrl_init(pdev, pctrl);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, pctrl);

	ret = devm_pinctrl_register_and_init(dev, &pctrl->pdesc,
					     pctrl, &pctrl->pctrl_dev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "fail to register pinctrl driver\n");

	return pinctrl_enable(pctrl->pctrl_dev);
}
EXPORT_SYMBOL_GPL(sophgo_pinctrl_probe);

MODULE_DESCRIPTION("Common pinctrl helper function for the Sophgo SoC");
MODULE_LICENSE("GPL");
