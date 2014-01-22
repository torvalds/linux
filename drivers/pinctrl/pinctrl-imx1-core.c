/*
 * Core driver for the imx pin controller in imx1/21/27
 *
 * Copyright (C) 2013 Pengutronix
 * Author: Markus Pargmann <mpa@pengutronix.de>
 *
 * Based on pinctrl-imx.c:
 *	Author: Dong Aisheng <dong.aisheng@linaro.org>
 *	Copyright (C) 2012 Freescale Semiconductor, Inc.
 *	Copyright (C) 2012 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/bitops.h>
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
#include "pinctrl-imx1.h"

struct imx1_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	void __iomem *base;
	const struct imx1_pinctrl_soc_info *info;
};

/*
 * MX1 register offsets
 */

#define MX1_DDIR 0x00
#define MX1_OCR 0x04
#define MX1_ICONFA 0x0c
#define MX1_ICONFB 0x14
#define MX1_GIUS 0x20
#define MX1_GPR 0x38
#define MX1_PUEN 0x40

#define MX1_PORT_STRIDE 0x100


/*
 * MUX_ID format defines
 */
#define MX1_MUX_FUNCTION(val) (BIT(0) & val)
#define MX1_MUX_GPIO(val) ((BIT(1) & val) >> 1)
#define MX1_MUX_DIR(val) ((BIT(2) & val) >> 2)
#define MX1_MUX_OCONF(val) (((BIT(4) | BIT(5)) & val) >> 4)
#define MX1_MUX_ICONFA(val) (((BIT(8) | BIT(9)) & val) >> 8)
#define MX1_MUX_ICONFB(val) (((BIT(10) | BIT(11)) & val) >> 10)


/*
 * IMX1 IOMUXC manages the pins based on ports. Each port has 32 pins. IOMUX
 * control register are seperated into function, output configuration, input
 * configuration A, input configuration B, GPIO in use and data direction.
 *
 * Those controls that are represented by 1 bit have a direct mapping between
 * bit position and pin id. If they are represented by 2 bit, the lower 16 pins
 * are in the first register and the upper 16 pins in the second (next)
 * register. pin_id is stored in bit (pin_id%16)*2 and the bit above.
 */

/*
 * Calculates the register offset from a pin_id
 */
static void __iomem *imx1_mem(struct imx1_pinctrl *ipctl, unsigned int pin_id)
{
	unsigned int port = pin_id / 32;
	return ipctl->base + port * MX1_PORT_STRIDE;
}

/*
 * Write to a register with 2 bits per pin. The function will automatically
 * use the next register if the pin is managed in the second register.
 */
static void imx1_write_2bit(struct imx1_pinctrl *ipctl, unsigned int pin_id,
		u32 value, u32 reg_offset)
{
	void __iomem *reg = imx1_mem(ipctl, pin_id) + reg_offset;
	int offset = (pin_id % 16) * 2; /* offset, regardless of register used */
	int mask = ~(0x3 << offset); /* Mask for 2 bits at offset */
	u32 old_val;
	u32 new_val;

	/* Use the next register if the pin's port pin number is >=16 */
	if (pin_id % 32 >= 16)
		reg += 0x04;

	dev_dbg(ipctl->dev, "write: register 0x%p offset %d value 0x%x\n",
			reg, offset, value);

	/* Get current state of pins */
	old_val = readl(reg);
	old_val &= mask;

	new_val = value & 0x3; /* Make sure value is really 2 bit */
	new_val <<= offset;
	new_val |= old_val;/* Set new state for pin_id */

	writel(new_val, reg);
}

static void imx1_write_bit(struct imx1_pinctrl *ipctl, unsigned int pin_id,
		u32 value, u32 reg_offset)
{
	void __iomem *reg = imx1_mem(ipctl, pin_id) + reg_offset;
	int offset = pin_id % 32;
	int mask = ~BIT_MASK(offset);
	u32 old_val;
	u32 new_val;

	/* Get current state of pins */
	old_val = readl(reg);
	old_val &= mask;

	new_val = value & 0x1; /* Make sure value is really 1 bit */
	new_val <<= offset;
	new_val |= old_val;/* Set new state for pin_id */

	writel(new_val, reg);
}

static int imx1_read_2bit(struct imx1_pinctrl *ipctl, unsigned int pin_id,
		u32 reg_offset)
{
	void __iomem *reg = imx1_mem(ipctl, pin_id) + reg_offset;
	int offset = (pin_id % 16) * 2;

	/* Use the next register if the pin's port pin number is >=16 */
	if (pin_id % 32 >= 16)
		reg += 0x04;

	return (readl(reg) & (BIT(offset) | BIT(offset+1))) >> offset;
}

static int imx1_read_bit(struct imx1_pinctrl *ipctl, unsigned int pin_id,
		u32 reg_offset)
{
	void __iomem *reg = imx1_mem(ipctl, pin_id) + reg_offset;
	int offset = pin_id % 32;

	return !!(readl(reg) & BIT(offset));
}

static const inline struct imx1_pin_group *imx1_pinctrl_find_group_by_name(
				const struct imx1_pinctrl_soc_info *info,
				const char *name)
{
	const struct imx1_pin_group *grp = NULL;
	int i;

	for (i = 0; i < info->ngroups; i++) {
		if (!strcmp(info->groups[i].name, name)) {
			grp = &info->groups[i];
			break;
		}
	}

	return grp;
}

static int imx1_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct imx1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx1_pinctrl_soc_info *info = ipctl->info;

	return info->ngroups;
}

static const char *imx1_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned selector)
{
	struct imx1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx1_pinctrl_soc_info *info = ipctl->info;

	return info->groups[selector].name;
}

static int imx1_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
			       const unsigned int **pins,
			       unsigned *npins)
{
	struct imx1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx1_pinctrl_soc_info *info = ipctl->info;

	if (selector >= info->ngroups)
		return -EINVAL;

	*pins = info->groups[selector].pin_ids;
	*npins = info->groups[selector].npins;

	return 0;
}

static void imx1_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
		   unsigned offset)
{
	struct imx1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);

	seq_printf(s, "GPIO %d, function %d, direction %d, oconf %d, iconfa %d, iconfb %d",
			imx1_read_bit(ipctl, offset, MX1_GIUS),
			imx1_read_bit(ipctl, offset, MX1_GPR),
			imx1_read_bit(ipctl, offset, MX1_DDIR),
			imx1_read_2bit(ipctl, offset, MX1_OCR),
			imx1_read_2bit(ipctl, offset, MX1_ICONFA),
			imx1_read_2bit(ipctl, offset, MX1_ICONFB));
}

static int imx1_dt_node_to_map(struct pinctrl_dev *pctldev,
			struct device_node *np,
			struct pinctrl_map **map, unsigned *num_maps)
{
	struct imx1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx1_pinctrl_soc_info *info = ipctl->info;
	const struct imx1_pin_group *grp;
	struct pinctrl_map *new_map;
	struct device_node *parent;
	int map_num = 1;
	int i, j;

	/*
	 * first find the group of this node and check if we need create
	 * config maps for pins
	 */
	grp = imx1_pinctrl_find_group_by_name(info, np->name);
	if (!grp) {
		dev_err(info->dev, "unable to find group for node %s\n",
			np->name);
		return -EINVAL;
	}

	for (i = 0; i < grp->npins; i++)
		map_num++;

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
		new_map[j].type = PIN_MAP_TYPE_CONFIGS_PIN;
		new_map[j].data.configs.group_or_pin =
				pin_get_name(pctldev, grp->pins[i].pin_id);
		new_map[j].data.configs.configs = &grp->pins[i].config;
		new_map[j].data.configs.num_configs = 1;
		j++;
	}

	dev_dbg(pctldev->dev, "maps: function %s group %s num %d\n",
		(*map)->data.mux.function, (*map)->data.mux.group, map_num);

	return 0;
}

static void imx1_dt_free_map(struct pinctrl_dev *pctldev,
				struct pinctrl_map *map, unsigned num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops imx1_pctrl_ops = {
	.get_groups_count = imx1_get_groups_count,
	.get_group_name = imx1_get_group_name,
	.get_group_pins = imx1_get_group_pins,
	.pin_dbg_show = imx1_pin_dbg_show,
	.dt_node_to_map = imx1_dt_node_to_map,
	.dt_free_map = imx1_dt_free_map,

};

static int imx1_pmx_enable(struct pinctrl_dev *pctldev, unsigned selector,
			   unsigned group)
{
	struct imx1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx1_pinctrl_soc_info *info = ipctl->info;
	const struct imx1_pin *pins;
	unsigned int npins;
	int i;

	/*
	 * Configure the mux mode for each pin in the group for a specific
	 * function.
	 */
	pins = info->groups[group].pins;
	npins = info->groups[group].npins;

	WARN_ON(!pins || !npins);

	dev_dbg(ipctl->dev, "enable function %s group %s\n",
		info->functions[selector].name, info->groups[group].name);

	for (i = 0; i < npins; i++) {
		unsigned int mux = pins[i].mux_id;
		unsigned int pin_id = pins[i].pin_id;
		unsigned int afunction = MX1_MUX_FUNCTION(mux);
		unsigned int gpio_in_use = MX1_MUX_GPIO(mux);
		unsigned int direction = MX1_MUX_DIR(mux);
		unsigned int gpio_oconf = MX1_MUX_OCONF(mux);
		unsigned int gpio_iconfa = MX1_MUX_ICONFA(mux);
		unsigned int gpio_iconfb = MX1_MUX_ICONFB(mux);

		dev_dbg(pctldev->dev, "%s, pin 0x%x, function %d, gpio %d, direction %d, oconf %d, iconfa %d, iconfb %d\n",
				__func__, pin_id, afunction, gpio_in_use,
				direction, gpio_oconf, gpio_iconfa,
				gpio_iconfb);

		imx1_write_bit(ipctl, pin_id, gpio_in_use, MX1_GIUS);
		imx1_write_bit(ipctl, pin_id, direction, MX1_DDIR);

		if (gpio_in_use) {
			imx1_write_2bit(ipctl, pin_id, gpio_oconf, MX1_OCR);
			imx1_write_2bit(ipctl, pin_id, gpio_iconfa,
					MX1_ICONFA);
			imx1_write_2bit(ipctl, pin_id, gpio_iconfb,
					MX1_ICONFB);
		} else {
			imx1_write_bit(ipctl, pin_id, afunction, MX1_GPR);
		}
	}

	return 0;
}

static int imx1_pmx_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct imx1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx1_pinctrl_soc_info *info = ipctl->info;

	return info->nfunctions;
}

static const char *imx1_pmx_get_func_name(struct pinctrl_dev *pctldev,
					  unsigned selector)
{
	struct imx1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx1_pinctrl_soc_info *info = ipctl->info;

	return info->functions[selector].name;
}

static int imx1_pmx_get_groups(struct pinctrl_dev *pctldev, unsigned selector,
			       const char * const **groups,
			       unsigned * const num_groups)
{
	struct imx1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx1_pinctrl_soc_info *info = ipctl->info;

	*groups = info->functions[selector].groups;
	*num_groups = info->functions[selector].num_groups;

	return 0;
}

static const struct pinmux_ops imx1_pmx_ops = {
	.get_functions_count = imx1_pmx_get_funcs_count,
	.get_function_name = imx1_pmx_get_func_name,
	.get_function_groups = imx1_pmx_get_groups,
	.enable = imx1_pmx_enable,
};

static int imx1_pinconf_get(struct pinctrl_dev *pctldev,
			     unsigned pin_id, unsigned long *config)
{
	struct imx1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);

	*config = imx1_read_bit(ipctl, pin_id, MX1_PUEN);

	return 0;
}

static int imx1_pinconf_set(struct pinctrl_dev *pctldev,
			     unsigned pin_id, unsigned long *configs,
			     unsigned num_configs)
{
	struct imx1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx1_pinctrl_soc_info *info = ipctl->info;
	int i;

	for (i = 0; i != num_configs; ++i) {
		imx1_write_bit(ipctl, pin_id, configs[i] & 0x01, MX1_PUEN);

		dev_dbg(ipctl->dev, "pinconf set pullup pin %s\n",
			info->pins[pin_id].name);
	}

	return 0;
}

static void imx1_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				   struct seq_file *s, unsigned pin_id)
{
	unsigned long config;

	imx1_pinconf_get(pctldev, pin_id, &config);
	seq_printf(s, "0x%lx", config);
}

static void imx1_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
					 struct seq_file *s, unsigned group)
{
	struct imx1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct imx1_pinctrl_soc_info *info = ipctl->info;
	struct imx1_pin_group *grp;
	unsigned long config;
	const char *name;
	int i, ret;

	if (group > info->ngroups)
		return;

	seq_puts(s, "\n");
	grp = &info->groups[group];
	for (i = 0; i < grp->npins; i++) {
		name = pin_get_name(pctldev, grp->pins[i].pin_id);
		ret = imx1_pinconf_get(pctldev, grp->pins[i].pin_id, &config);
		if (ret)
			return;
		seq_printf(s, "%s: 0x%lx", name, config);
	}
}

static const struct pinconf_ops imx1_pinconf_ops = {
	.pin_config_get = imx1_pinconf_get,
	.pin_config_set = imx1_pinconf_set,
	.pin_config_dbg_show = imx1_pinconf_dbg_show,
	.pin_config_group_dbg_show = imx1_pinconf_group_dbg_show,
};

static struct pinctrl_desc imx1_pinctrl_desc = {
	.pctlops = &imx1_pctrl_ops,
	.pmxops = &imx1_pmx_ops,
	.confops = &imx1_pinconf_ops,
	.owner = THIS_MODULE,
};

static int imx1_pinctrl_parse_groups(struct device_node *np,
				    struct imx1_pin_group *grp,
				    struct imx1_pinctrl_soc_info *info,
				    u32 index)
{
	int size;
	const __be32 *list;
	int i;

	dev_dbg(info->dev, "group(%d): %s\n", index, np->name);

	/* Initialise group */
	grp->name = np->name;

	/*
	 * the binding format is fsl,pins = <PIN MUX_ID CONFIG>
	 */
	list = of_get_property(np, "fsl,pins", &size);
	/* we do not check return since it's safe node passed down */
	if (!size || size % 12) {
		dev_notice(info->dev, "Not a valid fsl,pins property (%s)\n",
				np->name);
		return -EINVAL;
	}

	grp->npins = size / 12;
	grp->pins = devm_kzalloc(info->dev,
			grp->npins * sizeof(struct imx1_pin), GFP_KERNEL);
	grp->pin_ids = devm_kzalloc(info->dev,
			grp->npins * sizeof(unsigned int), GFP_KERNEL);

	if (!grp->pins || !grp->pin_ids)
		return -ENOMEM;

	for (i = 0; i < grp->npins; i++) {
		grp->pins[i].pin_id = be32_to_cpu(*list++);
		grp->pins[i].mux_id = be32_to_cpu(*list++);
		grp->pins[i].config = be32_to_cpu(*list++);

		grp->pin_ids[i] = grp->pins[i].pin_id;
	}

	return 0;
}

static int imx1_pinctrl_parse_functions(struct device_node *np,
				       struct imx1_pinctrl_soc_info *info,
				       u32 index)
{
	struct device_node *child;
	struct imx1_pmx_func *func;
	struct imx1_pin_group *grp;
	int ret;
	static u32 grp_index;
	u32 i = 0;

	dev_dbg(info->dev, "parse function(%d): %s\n", index, np->name);

	func = &info->functions[index];

	/* Initialise function */
	func->name = np->name;
	func->num_groups = of_get_child_count(np);
	if (func->num_groups <= 0)
		return -EINVAL;

	func->groups = devm_kzalloc(info->dev,
			func->num_groups * sizeof(char *), GFP_KERNEL);

	if (!func->groups)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		func->groups[i] = child->name;
		grp = &info->groups[grp_index++];
		ret = imx1_pinctrl_parse_groups(child, grp, info, i++);
		if (ret == -ENOMEM)
			return ret;
	}

	return 0;
}

static int imx1_pinctrl_parse_dt(struct platform_device *pdev,
		struct imx1_pinctrl *pctl, struct imx1_pinctrl_soc_info *info)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	int ret;
	u32 nfuncs = 0;
	u32 ngroups = 0;
	u32 ifunc = 0;

	if (!np)
		return -ENODEV;

	for_each_child_of_node(np, child) {
		++nfuncs;
		ngroups += of_get_child_count(child);
	}

	if (!nfuncs) {
		dev_err(&pdev->dev, "No pin functions defined\n");
		return -EINVAL;
	}

	info->nfunctions = nfuncs;
	info->functions = devm_kzalloc(&pdev->dev,
			nfuncs * sizeof(struct imx1_pmx_func), GFP_KERNEL);

	info->ngroups = ngroups;
	info->groups = devm_kzalloc(&pdev->dev,
			ngroups * sizeof(struct imx1_pin_group), GFP_KERNEL);


	if (!info->functions || !info->groups)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		ret = imx1_pinctrl_parse_functions(child, info, ifunc++);
		if (ret == -ENOMEM)
			return -ENOMEM;
	}

	return 0;
}

int imx1_pinctrl_core_probe(struct platform_device *pdev,
		      struct imx1_pinctrl_soc_info *info)
{
	struct imx1_pinctrl *ipctl;
	struct resource *res;
	struct pinctrl_desc *pctl_desc;
	int ret;

	if (!info || !info->pins || !info->npins) {
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

	ipctl->base = devm_ioremap_nocache(&pdev->dev, res->start,
			resource_size(res));
	if (!ipctl->base)
		return -ENOMEM;

	pctl_desc = &imx1_pinctrl_desc;
	pctl_desc->name = dev_name(&pdev->dev);
	pctl_desc->pins = info->pins;
	pctl_desc->npins = info->npins;

	ret = imx1_pinctrl_parse_dt(pdev, ipctl, info);
	if (ret) {
		dev_err(&pdev->dev, "fail to probe dt properties\n");
		return ret;
	}

	ipctl->info = info;
	ipctl->dev = info->dev;
	platform_set_drvdata(pdev, ipctl);
	ipctl->pctl = pinctrl_register(pctl_desc, &pdev->dev, ipctl);
	if (!ipctl->pctl) {
		dev_err(&pdev->dev, "could not register IMX pinctrl driver\n");
		return -EINVAL;
	}

	dev_info(&pdev->dev, "initialized IMX pinctrl driver\n");

	return 0;
}

int imx1_pinctrl_core_remove(struct platform_device *pdev)
{
	struct imx1_pinctrl *ipctl = platform_get_drvdata(pdev);

	pinctrl_unregister(ipctl->pctl);

	return 0;
}
