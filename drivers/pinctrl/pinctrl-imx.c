/*
 * Core driver for the imx pin controller
 *
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Linaro Ltd.
 *
 * Author: Dong Aisheng <dong.aisheng@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>

#include "core.h"
#include "pinctrl-imx.h"

#define IMX_PMX_DUMP(info, p, m, c, n)			\
{							\
	int i, j;					\
	printk(KERN_DEBUG "Format: Pin Mux Config\n");	\
	for (i = 0; i < n; i++) {			\
		j = p[i];				\
		printk(KERN_DEBUG "%s %d 0x%lx\n",	\
			info->pins[j].name,		\
			m[i], c[i]);			\
	}						\
}

/* The bits in CONFIG cell defined in binding doc*/
#define IMX_NO_PAD_CTL	0x80000000	/* no pin config need */
#define IMX_PAD_SION 0x40000000		/* set SION */

/**
 * @dev: a pointer back to containing device
 * @base: the offset to the controller in virtual memory
 */
struct imx_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	void __iomem *base;
	const struct imx_pinctrl_soc_info *info;
};

static const struct imx_pin_reg *imx_find_pin_reg(
				const struct imx_pinctrl_soc_info *info,
				unsigned pin, bool is_mux, unsigned mux)
{
	const struct imx_pin_reg *pin_reg = NULL;
	int i;

	for (i = 0; i < info->npin_regs; i++) {
		pin_reg = &info->pin_regs[i];
		if (pin_reg->pid != pin)
			continue;
		if (!is_mux)
			break;
		else if (pin_reg->mux_mode == (mux & IMX_MUX_MASK))
			break;
	}

	if (i == info->npin_regs) {
		dev_err(info->dev, "Pin(%s): unable to find pin reg map\n",
			info->pins[pin].name);
		return NULL;
	}

	return pin_reg;
}

static const inline struct imx_pin_group *imx_pinctrl_find_group_by_name(
				const struct imx_pinctrl_soc_info *info,
				const char *name)
{
	const struct imx_pin_group *grp = NULL;
	int i;

	for (i = 0; i < info->ngroups; i++) {
		if (!strcmp(info->groups[i].name, name)) {
			grp = &info->groups[i];
			break;
		}
	}

	return grp;
}

static int imx_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;

	return info->ngroups;
}

static const char *imx_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned selector)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;

	return info->groups[selector].name;
}

static int imx_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
			       const unsigned **pins,
			       unsigned *npins)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;

	if (selector >= info->ngroups)
		return -EINVAL;

	*pins = info->groups[selector].pins;
	*npins = info->groups[selector].npins;

	return 0;
}

static void imx_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
		   unsigned offset)
{
	seq_printf(s, "%s", dev_name(pctldev->dev));
}

static int imx_dt_node_to_map(struct pinctrl_dev *pctldev,
			struct device_node *np,
			struct pinctrl_map **map, unsigned *num_maps)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	const struct imx_pin_group *grp;
	struct pinctrl_map *new_map;
	struct device_node *parent;
	int map_num = 1;
	int i, j;

	/*
	 * first find the group of this node and check if we need create
	 * config maps for pins
	 */
	grp = imx_pinctrl_find_group_by_name(info, np->name);
	if (!grp) {
		dev_err(info->dev, "unable to find group for node %s\n",
			np->name);
		return -EINVAL;
	}

	for (i = 0; i < grp->npins; i++) {
		if (!(grp->configs[i] & IMX_NO_PAD_CTL))
			map_num++;
	}

	new_map = kmalloc(sizeof(struct pinctrl_map) * map_num, GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	*map = new_map;
	*num_maps = map_num;

	/* create mux map */
	parent = of_get_parent(np);
	if (!parent) {
		kfree(new_map);
		return -EINVAL;
	}
	new_map[0].type = PIN_MAP_TYPE_MUX_GROUP;
	new_map[0].data.mux.function = parent->name;
	new_map[0].data.mux.group = np->name;
	of_node_put(parent);

	/* create config map */
	new_map++;
	for (i = j = 0; i < grp->npins; i++) {
		if (!(grp->configs[i] & IMX_NO_PAD_CTL)) {
			new_map[j].type = PIN_MAP_TYPE_CONFIGS_PIN;
			new_map[j].data.configs.group_or_pin =
					pin_get_name(pctldev, grp->pins[i]);
			new_map[j].data.configs.configs = &grp->configs[i];
			new_map[j].data.configs.num_configs = 1;
			j++;
		}
	}

	dev_dbg(pctldev->dev, "maps: function %s group %s num %d\n",
		(*map)->data.mux.function, (*map)->data.mux.group, map_num);

	return 0;
}

static void imx_dt_free_map(struct pinctrl_dev *pctldev,
				struct pinctrl_map *map, unsigned num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops imx_pctrl_ops = {
	.get_groups_count = imx_get_groups_count,
	.get_group_name = imx_get_group_name,
	.get_group_pins = imx_get_group_pins,
	.pin_dbg_show = imx_pin_dbg_show,
	.dt_node_to_map = imx_dt_node_to_map,
	.dt_free_map = imx_dt_free_map,

};

static int imx_pmx_enable(struct pinctrl_dev *pctldev, unsigned selector,
			   unsigned group)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	const struct imx_pin_reg *pin_reg;
	const unsigned *pins, *mux;
	unsigned int npins, pin_id;
	int i;

	/*
	 * Configure the mux mode for each pin in the group for a specific
	 * function.
	 */
	pins = info->groups[group].pins;
	npins = info->groups[group].npins;
	mux = info->groups[group].mux_mode;

	WARN_ON(!pins || !npins || !mux);

	dev_dbg(ipctl->dev, "enable function %s group %s\n",
		info->functions[selector].name, info->groups[group].name);

	for (i = 0; i < npins; i++) {
		pin_id = pins[i];

		pin_reg = imx_find_pin_reg(info, pin_id, 1, mux[i]);
		if (!pin_reg)
			return -EINVAL;

		if (!pin_reg->mux_reg) {
			dev_err(ipctl->dev, "Pin(%s) does not support mux function\n",
				info->pins[pin_id].name);
			return -EINVAL;
		}

		writel(mux[i], ipctl->base + pin_reg->mux_reg);
		dev_dbg(ipctl->dev, "write: offset 0x%x val 0x%x\n",
			pin_reg->mux_reg, mux[i]);

		/* some pins also need select input setting, set it if found */
		if (pin_reg->input_reg) {
			writel(pin_reg->input_val, ipctl->base + pin_reg->input_reg);
			dev_dbg(ipctl->dev,
				"==>select_input: offset 0x%x val 0x%x\n",
				pin_reg->input_reg, pin_reg->input_val);
		}
	}

	return 0;
}

static int imx_pmx_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;

	return info->nfunctions;
}

static const char *imx_pmx_get_func_name(struct pinctrl_dev *pctldev,
					  unsigned selector)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;

	return info->functions[selector].name;
}

static int imx_pmx_get_groups(struct pinctrl_dev *pctldev, unsigned selector,
			       const char * const **groups,
			       unsigned * const num_groups)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;

	*groups = info->functions[selector].groups;
	*num_groups = info->functions[selector].num_groups;

	return 0;
}

static const struct pinmux_ops imx_pmx_ops = {
	.get_functions_count = imx_pmx_get_funcs_count,
	.get_function_name = imx_pmx_get_func_name,
	.get_function_groups = imx_pmx_get_groups,
	.enable = imx_pmx_enable,
};

static int imx_pinconf_get(struct pinctrl_dev *pctldev,
			     unsigned pin_id, unsigned long *config)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	const struct imx_pin_reg *pin_reg;

	pin_reg = imx_find_pin_reg(info, pin_id, 0, 0);
	if (!pin_reg)
		return -EINVAL;

	if (!pin_reg->conf_reg) {
		dev_err(info->dev, "Pin(%s) does not support config function\n",
			info->pins[pin_id].name);
		return -EINVAL;
	}

	*config = readl(ipctl->base + pin_reg->conf_reg);

	return 0;
}

static int imx_pinconf_set(struct pinctrl_dev *pctldev,
			     unsigned pin_id, unsigned long config)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	const struct imx_pin_reg *pin_reg;

	pin_reg = imx_find_pin_reg(info, pin_id, 0, 0);
	if (!pin_reg)
		return -EINVAL;

	if (!pin_reg->conf_reg) {
		dev_err(info->dev, "Pin(%s) does not support config function\n",
			info->pins[pin_id].name);
		return -EINVAL;
	}

	dev_dbg(ipctl->dev, "pinconf set pin %s\n",
		info->pins[pin_id].name);

	writel(config, ipctl->base + pin_reg->conf_reg);
	dev_dbg(ipctl->dev, "write: offset 0x%x val 0x%lx\n",
		pin_reg->conf_reg, config);

	return 0;
}

static void imx_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				   struct seq_file *s, unsigned pin_id)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	const struct imx_pin_reg *pin_reg;
	unsigned long config;

	pin_reg = imx_find_pin_reg(info, pin_id, 0, 0);
	if (!pin_reg || !pin_reg->conf_reg) {
		seq_printf(s, "N/A");
		return;
	}

	config = readl(ipctl->base + pin_reg->conf_reg);
	seq_printf(s, "0x%lx", config);
}

static void imx_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
					 struct seq_file *s, unsigned group)
{
	struct imx_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx_pinctrl_soc_info *info = ipctl->info;
	struct imx_pin_group *grp;
	unsigned long config;
	const char *name;
	int i, ret;

	if (group > info->ngroups)
		return;

	seq_printf(s, "\n");
	grp = &info->groups[group];
	for (i = 0; i < grp->npins; i++) {
		name = pin_get_name(pctldev, grp->pins[i]);
		ret = imx_pinconf_get(pctldev, grp->pins[i], &config);
		if (ret)
			return;
		seq_printf(s, "%s: 0x%lx", name, config);
	}
}

static const struct pinconf_ops imx_pinconf_ops = {
	.pin_config_get = imx_pinconf_get,
	.pin_config_set = imx_pinconf_set,
	.pin_config_dbg_show = imx_pinconf_dbg_show,
	.pin_config_group_dbg_show = imx_pinconf_group_dbg_show,
};

static struct pinctrl_desc imx_pinctrl_desc = {
	.pctlops = &imx_pctrl_ops,
	.pmxops = &imx_pmx_ops,
	.confops = &imx_pinconf_ops,
	.owner = THIS_MODULE,
};

/* decode pin id and mux from pin function id got from device tree*/
static int imx_pinctrl_get_pin_id_and_mux(const struct imx_pinctrl_soc_info *info,
				unsigned int pin_func_id, unsigned int *pin_id,
				unsigned int *mux)
{
	if (pin_func_id > info->npin_regs)
		return -EINVAL;

	*pin_id = info->pin_regs[pin_func_id].pid;
	*mux = info->pin_regs[pin_func_id].mux_mode;

	return 0;
}

static int imx_pinctrl_parse_groups(struct device_node *np,
				    struct imx_pin_group *grp,
				    struct imx_pinctrl_soc_info *info,
				    u32 index)
{
	unsigned int pin_func_id;
	int ret, size;
	const __be32 *list;
	int i, j;
	u32 config;

	dev_dbg(info->dev, "group(%d): %s\n", index, np->name);

	/* Initialise group */
	grp->name = np->name;

	/*
	 * the binding format is fsl,pins = <PIN_FUNC_ID CONFIG ...>,
	 * do sanity check and calculate pins number
	 */
	list = of_get_property(np, "fsl,pins", &size);
	/* we do not check return since it's safe node passed down */
	size /= sizeof(*list);
	if (!size || size % 2) {
		dev_err(info->dev, "wrong pins number or pins and configs should be pairs\n");
		return -EINVAL;
	}

	grp->npins = size / 2;
	grp->pins = devm_kzalloc(info->dev, grp->npins * sizeof(unsigned int),
				GFP_KERNEL);
	grp->mux_mode = devm_kzalloc(info->dev, grp->npins * sizeof(unsigned int),
				GFP_KERNEL);
	grp->configs = devm_kzalloc(info->dev, grp->npins * sizeof(unsigned long),
				GFP_KERNEL);
	for (i = 0, j = 0; i < size; i += 2, j++) {
		pin_func_id = be32_to_cpu(*list++);
		ret = imx_pinctrl_get_pin_id_and_mux(info, pin_func_id,
					&grp->pins[j], &grp->mux_mode[j]);
		if (ret) {
			dev_err(info->dev, "get invalid pin function id\n");
			return -EINVAL;
		}
		/* SION bit is in mux register */
		config = be32_to_cpu(*list++);
		if (config & IMX_PAD_SION)
			grp->mux_mode[j] |= IOMUXC_CONFIG_SION;
		grp->configs[j] = config & ~IMX_PAD_SION;
	}

#ifdef DEBUG
	IMX_PMX_DUMP(info, grp->pins, grp->mux_mode, grp->configs, grp->npins);
#endif

	return 0;
}

static int imx_pinctrl_parse_functions(struct device_node *np,
				       struct imx_pinctrl_soc_info *info,
				       u32 index)
{
	struct device_node *child;
	struct imx_pmx_func *func;
	struct imx_pin_group *grp;
	int ret;
	static u32 grp_index;
	u32 i = 0;

	dev_dbg(info->dev, "parse function(%d): %s\n", index, np->name);

	func = &info->functions[index];

	/* Initialise function */
	func->name = np->name;
	func->num_groups = of_get_child_count(np);
	if (func->num_groups <= 0) {
		dev_err(info->dev, "no groups defined\n");
		return -EINVAL;
	}
	func->groups = devm_kzalloc(info->dev,
			func->num_groups * sizeof(char *), GFP_KERNEL);

	for_each_child_of_node(np, child) {
		func->groups[i] = child->name;
		grp = &info->groups[grp_index++];
		ret = imx_pinctrl_parse_groups(child, grp, info, i++);
		if (ret)
			return ret;
	}

	return 0;
}

static int imx_pinctrl_probe_dt(struct platform_device *pdev,
				struct imx_pinctrl_soc_info *info)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	int ret;
	u32 nfuncs = 0;
	u32 i = 0;

	if (!np)
		return -ENODEV;

	nfuncs = of_get_child_count(np);
	if (nfuncs <= 0) {
		dev_err(&pdev->dev, "no functions defined\n");
		return -EINVAL;
	}

	info->nfunctions = nfuncs;
	info->functions = devm_kzalloc(&pdev->dev, nfuncs * sizeof(struct imx_pmx_func),
					GFP_KERNEL);
	if (!info->functions)
		return -ENOMEM;

	info->ngroups = 0;
	for_each_child_of_node(np, child)
		info->ngroups += of_get_child_count(child);
	info->groups = devm_kzalloc(&pdev->dev, info->ngroups * sizeof(struct imx_pin_group),
					GFP_KERNEL);
	if (!info->groups)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		ret = imx_pinctrl_parse_functions(child, info, i++);
		if (ret) {
			dev_err(&pdev->dev, "failed to parse function\n");
			return ret;
		}
	}

	return 0;
}

int imx_pinctrl_probe(struct platform_device *pdev,
		      struct imx_pinctrl_soc_info *info)
{
	struct imx_pinctrl *ipctl;
	struct resource *res;
	int ret;

	if (!info || !info->pins || !info->npins
		  || !info->pin_regs || !info->npin_regs) {
		dev_err(&pdev->dev, "wrong pinctrl info\n");
		return -EINVAL;
	}
	info->dev = &pdev->dev;

	/* Create state holders etc for this driver */
	ipctl = devm_kzalloc(&pdev->dev, sizeof(*ipctl), GFP_KERNEL);
	if (!ipctl)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOENT;

	ipctl->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ipctl->base))
		return PTR_ERR(ipctl->base);

	imx_pinctrl_desc.name = dev_name(&pdev->dev);
	imx_pinctrl_desc.pins = info->pins;
	imx_pinctrl_desc.npins = info->npins;

	ret = imx_pinctrl_probe_dt(pdev, info);
	if (ret) {
		dev_err(&pdev->dev, "fail to probe dt properties\n");
		return ret;
	}

	ipctl->info = info;
	ipctl->dev = info->dev;
	platform_set_drvdata(pdev, ipctl);
	ipctl->pctl = pinctrl_register(&imx_pinctrl_desc, &pdev->dev, ipctl);
	if (!ipctl->pctl) {
		dev_err(&pdev->dev, "could not register IMX pinctrl driver\n");
		return -EINVAL;
	}

	dev_info(&pdev->dev, "initialized IMX pinctrl driver\n");

	return 0;
}

int imx_pinctrl_remove(struct platform_device *pdev)
{
	struct imx_pinctrl *ipctl = platform_get_drvdata(pdev);

	pinctrl_unregister(ipctl->pctl);

	return 0;
}
