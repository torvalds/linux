// SPDX-License-Identifier: GPL-2.0
/*
 * Pinctrl / GPIO driver for StarFive JH7110 SoC
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/clk.h>
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
#include <linux/reset.h>

#include "../core.h"
#include "../pinctrl-utils.h"
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
			struct pinctrl_map **maps, unsigned int *num_maps)
{
	struct starfive_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = sfp->gc.parent;
	const struct starfive_pinctrl_soc_info *info = sfp->info;
	struct starfive_pin *pin_data;
	struct device_node *child;
	struct pinctrl_map *map;
	struct group_desc *grp;
	const char **pgnames;
	const char *grpname;
	int ngroups;
	int nmaps;
	int ret;
	int *pins_id;
	int psize, pin_size;
	int size = 0;
	int offset = 0;
	const __be32 *list;
	int i, child_num_pins;

	nmaps = 0;
	ngroups = 0;
	pin_size = STARFIVE_PINS_SIZE;

	for_each_child_of_node(np, child) {
		list = of_get_property(child, "starfive,pins", &psize);
		if (!list) {
			dev_err(sfp->dev,
				"no starfive,pins and pins property in node %pOF\n", np);
			return -EINVAL;
		}
		size += psize;
	}

	if (!size || size % pin_size) {
		dev_err(sfp->dev,
			"Invalid starfive,pins or pins property in node %pOF\n", np);
		return -EINVAL;
	}

	nmaps = size / pin_size * 2;
	ngroups = size / pin_size;

	pgnames = devm_kcalloc(dev, ngroups, sizeof(*pgnames), GFP_KERNEL);
	if (!pgnames)
		return -ENOMEM;

	map = kcalloc(nmaps, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	grp = devm_kzalloc(sfp->dev, sizeof(struct group_desc),
				   GFP_KERNEL);
	if (!grp) {
		of_node_put(child);
		return -ENOMEM;
	}

	grp->data = devm_kcalloc(sfp->dev,
				 ngroups, sizeof(struct starfive_pin),
				 GFP_KERNEL);
	grp->pins = devm_kcalloc(sfp->dev,
				 ngroups, sizeof(int),
				 GFP_KERNEL);
	if (!grp->pins || !grp->data)
		return -ENOMEM;

	nmaps = 0;
	ngroups = 0;
	mutex_lock(&sfp->mutex);

	for_each_child_of_node(np, child) {
		grpname = devm_kasprintf(dev, GFP_KERNEL, "%pOFn.%pOFn", np, child);
		if (!grpname) {
			ret = -ENOMEM;
			goto put_child;
		}

		pgnames[ngroups++] = grpname;
		map[nmaps].type = PIN_MAP_TYPE_MUX_GROUP;
		map[nmaps].data.mux.function = np->name;
		map[nmaps].data.mux.group = grpname;
		nmaps += 1;


		list = of_get_property(child, "starfive,pins", &psize);
		if (!list) {
			dev_err(sfp->dev,
				"no starfive,pins and pins property in node %pOF\n", np);
			goto put_child;
		}
		child_num_pins = psize / pin_size;
		grp->name = grpname;
		grp->num_pins = child_num_pins;
		for (i = 0; i < child_num_pins; i++) {
			pin_data = &((struct starfive_pin *)(grp->data))[i + offset];
			pins_id =  &(grp->pins)[i + offset];

			if (!info->starfive_pinctrl_parse_pin) {
				dev_err(sfp->dev,
						"pinmux ops lacks necessary functions\n");
				goto put_child;
			}

			info->starfive_pinctrl_parse_pin(sfp,
					pins_id, pin_data, list, child);
			map[nmaps].type = PIN_MAP_TYPE_CONFIGS_PIN;
			map[nmaps].data.configs.group_or_pin =
						pin_get_name(pctldev, pin_data->pin);
			map[nmaps].data.configs.configs =
						&pin_data->pin_config.io_config;
			map[nmaps].data.configs.num_configs = 1;
			nmaps += 1;

			list++;
		}
		offset += i;

		ret = pinctrl_generic_add_group(pctldev,
				grpname, pins_id, child_num_pins, pin_data);
		if (ret < 0) {
			dev_err(dev, "error adding group %s: %d\n", grpname, ret);
			goto put_child;
		}
	}

	ret = pinmux_generic_add_function(pctldev, np->name, pgnames, ngroups, NULL);
	if (ret < 0) {
		dev_err(dev, "error adding function %s: %d\n", np->name, ret);
		goto free_map;
	}

	*maps = map;
	*num_maps = nmaps;
	mutex_unlock(&sfp->mutex);
	return 0;

put_child:
	of_node_put(child);
free_map:
	pinctrl_utils_free_map(pctldev, map, nmaps);
	mutex_unlock(&sfp->mutex);
	return ret;
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

static void starfive_disable_clock(void *data)
{
	clk_disable_unprepare(data);
}

int starfive_pinctrl_probe(struct platform_device *pdev,
		      const struct starfive_pinctrl_soc_info *info)
{
	struct device *dev = &pdev->dev;
	struct pinctrl_desc *starfive_pinctrl_desc;
	struct starfive_pinctrl *pctl;
	struct resource *res;
	struct reset_control *rst;
	struct clk *clk;
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

	clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "could not get clock\n");

	rst = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "could not get reset\n");

	if (clk) {
		ret = clk_prepare_enable(clk);
		if (ret)
			return dev_err_probe(dev, ret, "could not enable clock\n");

		ret = devm_add_action_or_reset(dev, starfive_disable_clock, clk);
		if (ret)
			return ret;
	}

	/*
	 * We don't want to assert reset and risk undoing pin muxing for the
	 * early boot serial console, but let's make sure the reset line is
	 * deasserted in case someone runs a really minimal bootloader.
	 */
	ret = reset_control_deassert(rst);
	if (ret)
		return dev_err_probe(dev, ret, "could not deassert reset\n");

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

MODULE_DESCRIPTION("Pinctrl driver for StarFive JH7110 SoC");
MODULE_AUTHOR("Jenny Zhang");
MODULE_AUTHOR("Jianlong Huang <jianlong.huang@starfivetech.com>");
MODULE_LICENSE("GPL");
