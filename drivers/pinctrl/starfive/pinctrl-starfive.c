// SPDX-License-Identifier: GPL-2.0
/*
 * Pinctrl / GPIO driver for StarFive JH7110 SoC
 *
 * Copyright (C) 2022 Shanghai StarFive Technology Co., Ltd.
 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/module.h>


#include "../core.h"
#include "../pinconf.h"
#include "../pinmux.h"
#include "pinctrl-starfive.h"


static inline const struct group_desc *starfive_pinctrl_find_group_by_name(
				struct pinctrl_dev *pctldev,
				const char *name)
{
	const struct group_desc *grp = NULL;
	int i;

	for (i = 0; i < pctldev->num_groups; i++) {
		grp = pinctrl_generic_get_group(pctldev, i);
		if (grp && !strcmp(grp->name, name))
			break;
	}

	return grp;
}

static void starfive_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			unsigned int offset)
{
	seq_printf(s, "%s", dev_name(pctldev->dev));
}

static int starfive_dt_node_to_map(struct pinctrl_dev *pctldev,
			struct device_node *np,
			struct pinctrl_map **map, unsigned int *num_maps)
{
	struct starfive_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	const struct group_desc *grp;
	struct pinctrl_map *new_map;
	struct device_node *parent;
	struct starfive_pin *pin;
	int map_num = 1;
	int i, j;

	grp = starfive_pinctrl_find_group_by_name(pctldev, np->name);
	if (!grp) {
		dev_err(pctl->dev, "unable to find group for node %pOFn\n", np);
		return -EINVAL;
	}

	map_num = grp->num_pins + 1;
	new_map = kmalloc_array(map_num, sizeof(struct pinctrl_map),
				GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	*map = new_map;
	*num_maps = map_num;

	parent = of_get_parent(np);
	if (!parent) {
		kfree(new_map);
		return -EINVAL;
	}
	new_map[0].type = PIN_MAP_TYPE_MUX_GROUP;
	new_map[0].data.mux.function = parent->name;
	new_map[0].data.mux.group = np->name;
	of_node_put(parent);

	new_map++;
	for (i = j = 0; i < grp->num_pins; i++) {
		pin = &((struct starfive_pin *)(grp->data))[i];

		new_map[j].type = PIN_MAP_TYPE_CONFIGS_PIN;
		new_map[j].data.configs.group_or_pin =
					pin_get_name(pctldev, pin->pin);
		new_map[j].data.configs.configs =
					&pin->pin_config.io_config;
		new_map[j].data.configs.num_configs = 1;
		j++;
	}

	return 0;
}

static void starfive_dt_free_map(struct pinctrl_dev *pctldev,
				struct pinctrl_map *map, unsigned int num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops starfive_pctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.pin_dbg_show = starfive_pin_dbg_show,
	.dt_node_to_map = starfive_dt_node_to_map,
	.dt_free_map = starfive_dt_free_map,
};


static int starfive_pmx_set(struct pinctrl_dev *pctldev, unsigned int selector,
			unsigned int group)
{
	struct starfive_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	const struct starfive_pinctrl_soc_info *info = pctl->info;
	struct function_desc *func;
	struct group_desc *grp;
	struct starfive_pin *pin;
	unsigned int npins;
	int i, err;

	grp = pinctrl_generic_get_group(pctldev, group);
	if (!grp)
		return -EINVAL;

	func = pinmux_generic_get_function(pctldev, selector);
	if (!func)
		return -EINVAL;

	npins = grp->num_pins;

	dev_dbg(pctl->dev, "enable function %s group %s\n",
		func->name, grp->name);

	for (i = 0; i < npins; i++) {
		pin = &((struct starfive_pin *)(grp->data))[i];
		if (info->starfive_pmx_set_one_pin_mux) {
			err = info->starfive_pmx_set_one_pin_mux(pctl, pin);
			if (err)
				return err;
		}
	}

	return 0;
}

const struct pinmux_ops starfive_pmx_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = starfive_pmx_set,
};


static int starfive_pinconf_get(struct pinctrl_dev *pctldev,
				unsigned int pin_id, unsigned long *config)
{
	struct starfive_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	const struct starfive_pinctrl_soc_info *info = pctl->info;

	if (info->starfive_pinconf_get)
		return info->starfive_pinconf_get(pctldev, pin_id, config);

	return 0;
}

static int starfive_pinconf_set(struct pinctrl_dev *pctldev,
				unsigned int pin_id, unsigned long *configs,
				unsigned int num_configs)
{
	struct starfive_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	const struct starfive_pinctrl_soc_info *info = pctl->info;


	if (info->starfive_pinconf_set)
		return info->starfive_pinconf_set(pctldev, pin_id,
				configs, num_configs);
	return 0;
}

static void starfive_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned int pin_id)
{
	struct starfive_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	const struct starfive_pin_reg *pin_reg;
	unsigned long config;
	int ret;

	pin_reg = &pctl->pin_regs[pin_id];
	if (pin_reg->io_conf_reg == -1) {
		seq_puts(s, "N/A");
		return;
	}

	ret = starfive_pinconf_get(pctldev, pin_id, &config);
	if (ret)
		return;
	seq_printf(s, "0x%lx", config);
}

static void starfive_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned int group)
{
	struct group_desc *grp;
	unsigned long config;
	const char *name;
	int i, ret;

	if (group >= pctldev->num_groups)
		return;

	seq_puts(s, "\n");
	grp = pinctrl_generic_get_group(pctldev, group);
	if (!grp)
		return;

	for (i = 0; i < grp->num_pins; i++) {
		struct starfive_pin *pin = &((struct starfive_pin *)(grp->data))[i];

		name = pin_get_name(pctldev, pin->pin);
		ret = starfive_pinconf_get(pctldev, pin->pin, &config);
		if (ret)
			return;
		seq_printf(s, "  %s: 0x%lx\n", name, config);
	}
}

static const struct pinconf_ops starfive_pinconf_ops = {
	.pin_config_get = starfive_pinconf_get,
	.pin_config_set = starfive_pinconf_set,
	.pin_config_dbg_show = starfive_pinconf_dbg_show,
	.pin_config_group_dbg_show = starfive_pinconf_group_dbg_show,
};


static int starfive_pinctrl_parse_groups(struct device_node *np,
					struct group_desc *grp,
					struct starfive_pinctrl *pctl,
					u32 index)
{
	const struct starfive_pinctrl_soc_info *info = pctl->info;
	struct starfive_pin *pin_data;
	struct device_node *child;
	int *pins_id;
	int psize, pin_size;
	int size = 0;
	int offset = 0;
	const __be32 *list;
	int j, child_num_pins;

	pin_size = STARFIVE_PINS_SIZE;

	/* Initialise group */
	grp->name = np->name;

	for_each_child_of_node(np, child) {
		list = of_get_property(child, "sf,pins", &psize);
		if (!list) {
			dev_err(pctl->dev,
				"no sf,pins and pins property in node %pOF\n", np);
			return -EINVAL;
		}
		size += psize;
	}

	if (!size || size % pin_size) {
		dev_err(pctl->dev,
			"Invalid sf,pins or pins property in node %pOF\n", np);
		return -EINVAL;
	}

	grp->num_pins = size / pin_size;
	grp->data = devm_kcalloc(pctl->dev,
				 grp->num_pins, sizeof(struct starfive_pin),
				 GFP_KERNEL);
	grp->pins = devm_kcalloc(pctl->dev,
				 grp->num_pins, sizeof(int),
				 GFP_KERNEL);
	if (!grp->pins || !grp->data)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		list = of_get_property(child, "sf,pins", &psize);
		if (!list) {
			dev_err(pctl->dev,
				"no sf,pins and pins property in node %pOF\n", np);
			return -EINVAL;
		}

		child_num_pins = psize / pin_size;

		for (j = 0; j < child_num_pins; j++) {
			pin_data = &((struct starfive_pin *)(grp->data))[j + offset];
			pins_id =  &(grp->pins)[j + offset];

			if (!info->starfive_pinctrl_parse_pin) {
				dev_err(pctl->dev, "pinmux ops lacks necessary functions\n");
				return -EINVAL;
			}

			info->starfive_pinctrl_parse_pin(pctl, pins_id, pin_data, list, child);
			list++;
		}
		offset += j;
	}

	return 0;
}

static int starfive_pinctrl_parse_functions(struct device_node *np,
					struct starfive_pinctrl *pctl,
					u32 index)
{
	struct pinctrl_dev *pctldev = pctl->pctl_dev;
	struct device_node *child;
	struct function_desc *func;
	struct group_desc *grp;
	u32 i = 0;
	int ret;

	func = pinmux_generic_get_function(pctldev, index);
	if (!func)
		return -EINVAL;

	func->name = np->name;
	func->num_group_names = of_get_child_count(np);
	if (func->num_group_names == 0) {
		dev_err(pctl->dev, "no groups defined in %pOF\n", np);
		return -EINVAL;
	}
	func->group_names = devm_kcalloc(pctl->dev, func->num_group_names,
					 sizeof(char *), GFP_KERNEL);
	if (!func->group_names)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		func->group_names[i] = child->name;
		grp = devm_kzalloc(pctl->dev, sizeof(struct group_desc),
				   GFP_KERNEL);
		if (!grp) {
			of_node_put(child);
			return -ENOMEM;
		}

		mutex_lock(&pctl->mutex);
		radix_tree_insert(&pctldev->pin_group_tree,
				  pctl->group_index++, grp);
		mutex_unlock(&pctl->mutex);

		ret = starfive_pinctrl_parse_groups(child, grp, pctl, i++);
		if (ret < 0) {
			dev_err(pctl->dev, "parse groups failed\n");
			return ret;
		}
	}

	return 0;
}

static int starfive_pinctrl_probe_dt(struct platform_device *pdev,
				struct starfive_pinctrl *pctl)
{
	struct device_node *np = pdev->dev.of_node;
	struct pinctrl_dev *pctldev = pctl->pctl_dev;
	u32 nfuncs = 1;
	u32 i = 0;

	if (!np)
		return -ENODEV;

	for (i = 0; i < nfuncs; i++) {
		struct function_desc *function;

		function = devm_kzalloc(&pdev->dev, sizeof(*function),
					GFP_KERNEL);
		if (!function)
			return -ENOMEM;

		mutex_lock(&pctl->mutex);
		radix_tree_insert(&pctldev->pin_function_tree, i, function);
		mutex_unlock(&pctl->mutex);
	}

	pctldev->num_functions = nfuncs;
	pctl->group_index = 0;
	pctldev->num_groups = of_get_child_count(np);
	starfive_pinctrl_parse_functions(np, pctl, 0);

	return 0;
}

int starfive_pinctrl_probe(struct platform_device *pdev,
		      const struct starfive_pinctrl_soc_info *info)
{
	struct device *dev = &pdev->dev;
	struct pinctrl_desc *starfive_pinctrl_desc;
	struct starfive_pinctrl *pctl;
	struct resource *res;
	int ret, i;
	u32 value;

	if (!info || !info->pins || !info->npins) {
		dev_err(&pdev->dev, "wrong pinctrl info\n");
		return -EINVAL;
	}

	pctl = devm_kzalloc(&pdev->dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;

	pctl->pin_regs = devm_kmalloc_array(&pdev->dev, info->npins,
					     sizeof(*pctl->pin_regs),
					     GFP_KERNEL);
	if (!pctl->pin_regs)
		return -ENOMEM;

	for (i = 0; i < info->npins; i++) {
		pctl->pin_regs[i].io_conf_reg = -1;
		pctl->pin_regs[i].gpo_dout_reg = -1;
		pctl->pin_regs[i].gpo_doen_reg = -1;
		pctl->pin_regs[i].func_sel_reg = -1;
		pctl->pin_regs[i].syscon_reg = -1;
		pctl->pin_regs[i].pad_sel_reg = -1;
	}

	pctl->padctl_base = devm_platform_ioremap_resource_byname(pdev, "control");
	if (IS_ERR(pctl->padctl_base))
		return PTR_ERR(pctl->padctl_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gpio");
	if (res) {
		pctl->gpio_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(pctl->gpio_base))
			return PTR_ERR(pctl->gpio_base);
	}

	if (info->starfive_iopad_sel_func) {
		ret = info->starfive_iopad_sel_func(pdev, pctl, value);
		if (ret)
			return ret;
	}

	starfive_pinctrl_desc = devm_kzalloc(&pdev->dev, sizeof(*starfive_pinctrl_desc),
					GFP_KERNEL);
	if (!starfive_pinctrl_desc)
		return -ENOMEM;

	raw_spin_lock_init(&pctl->lock);

	starfive_pinctrl_desc->name = dev_name(&pdev->dev);
	starfive_pinctrl_desc->pins = info->pins;
	starfive_pinctrl_desc->npins = info->npins;
	starfive_pinctrl_desc->pctlops = &starfive_pctrl_ops;
	starfive_pinctrl_desc->pmxops = &starfive_pmx_ops;
	starfive_pinctrl_desc->confops = &starfive_pinconf_ops;
	starfive_pinctrl_desc->owner = THIS_MODULE;

	mutex_init(&pctl->mutex);

	pctl->info = info;
	pctl->dev = &pdev->dev;
	platform_set_drvdata(pdev, pctl);
	pctl->gc.parent = dev;
	ret = devm_pinctrl_register_and_init(&pdev->dev,
					     starfive_pinctrl_desc, pctl,
					     &pctl->pctl_dev);
	if (ret) {
		dev_err(&pdev->dev,
			"could not register starfive pinctrl driver\n");
		return ret;
	}

	ret = starfive_pinctrl_probe_dt(pdev, pctl);
	if (ret) {
		dev_err(&pdev->dev,
			"fail to probe dt properties\n");
		return ret;
	}

	ret = pinctrl_enable(pctl->pctl_dev);
	if (ret) {
		dev_err(&pdev->dev,
			"pin controller failed to start\n");
		return ret;
	}

	if (info->starfive_gpio_register) {
		ret = info->starfive_gpio_register(pdev, pctl);
		if (ret) {
			dev_err(&pdev->dev,
				"starfive_gpio_register failed to register\n");
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(starfive_pinctrl_probe);

static int __maybe_unused starfive_pinctrl_suspend(struct device *dev)
{
	struct starfive_pinctrl *pctl = dev_get_drvdata(dev);

	return pinctrl_force_sleep(pctl->pctl_dev);
}

static int __maybe_unused starfive_pinctrl_resume(struct device *dev)
{
	struct starfive_pinctrl *pctl = dev_get_drvdata(dev);

	return pinctrl_force_default(pctl->pctl_dev);
}

const struct dev_pm_ops starfive_pinctrl_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(starfive_pinctrl_suspend,
					starfive_pinctrl_resume)
};
EXPORT_SYMBOL_GPL(starfive_pinctrl_pm_ops);
MODULE_DESCRIPTION("Pinctrl driver for StarFive JH7110 SoC");
MODULE_AUTHOR("jenny.zhang <jenny.zhang@starfivetech.com>");
MODULE_LICENSE("GPL v2");
