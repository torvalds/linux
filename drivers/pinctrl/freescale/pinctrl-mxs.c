// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2012 Freescale Semiconductor, Inc.

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "../core.h"
#include "pinctrl-mxs.h"

#define SUFFIX_LEN	4

struct mxs_pinctrl_data {
	struct device *dev;
	struct pinctrl_dev *pctl;
	void __iomem *base;
	struct mxs_pinctrl_soc_data *soc;
};

static int mxs_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct mxs_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	return d->soc->ngroups;
}

static const char *mxs_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned group)
{
	struct mxs_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	return d->soc->groups[group].name;
}

static int mxs_get_group_pins(struct pinctrl_dev *pctldev, unsigned group,
			      const unsigned **pins, unsigned *num_pins)
{
	struct mxs_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	*pins = d->soc->groups[group].pins;
	*num_pins = d->soc->groups[group].npins;

	return 0;
}

static void mxs_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			     unsigned offset)
{
	seq_printf(s, " %s", dev_name(pctldev->dev));
}

static int mxs_dt_node_to_map(struct pinctrl_dev *pctldev,
			      struct device_node *np,
			      struct pinctrl_map **map, unsigned *num_maps)
{
	struct pinctrl_map *new_map;
	char *group = NULL;
	unsigned new_num = 1;
	unsigned long config = 0;
	unsigned long *pconfig;
	int length = strlen(np->name) + SUFFIX_LEN;
	bool purecfg = false;
	u32 val, reg;
	int ret, i = 0;

	/* Check for pin config node which has no 'reg' property */
	if (of_property_read_u32(np, "reg", &reg))
		purecfg = true;

	ret = of_property_read_u32(np, "fsl,drive-strength", &val);
	if (!ret)
		config = val | MA_PRESENT;
	ret = of_property_read_u32(np, "fsl,voltage", &val);
	if (!ret)
		config |= val << VOL_SHIFT | VOL_PRESENT;
	ret = of_property_read_u32(np, "fsl,pull-up", &val);
	if (!ret)
		config |= val << PULL_SHIFT | PULL_PRESENT;

	/* Check for group node which has both mux and config settings */
	if (!purecfg && config)
		new_num = 2;

	new_map = kcalloc(new_num, sizeof(*new_map), GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	if (!purecfg) {
		new_map[i].type = PIN_MAP_TYPE_MUX_GROUP;
		new_map[i].data.mux.function = np->name;

		/* Compose group name */
		group = kzalloc(length, GFP_KERNEL);
		if (!group) {
			ret = -ENOMEM;
			goto free;
		}
		snprintf(group, length, "%s.%d", np->name, reg);
		new_map[i].data.mux.group = group;
		i++;
	}

	if (config) {
		pconfig = kmemdup(&config, sizeof(config), GFP_KERNEL);
		if (!pconfig) {
			ret = -ENOMEM;
			goto free_group;
		}

		new_map[i].type = PIN_MAP_TYPE_CONFIGS_GROUP;
		new_map[i].data.configs.group_or_pin = purecfg ? np->name :
								 group;
		new_map[i].data.configs.configs = pconfig;
		new_map[i].data.configs.num_configs = 1;
	}

	*map = new_map;
	*num_maps = new_num;

	return 0;

free_group:
	if (!purecfg)
		kfree(group);
free:
	kfree(new_map);
	return ret;
}

static void mxs_dt_free_map(struct pinctrl_dev *pctldev,
			    struct pinctrl_map *map, unsigned num_maps)
{
	u32 i;

	for (i = 0; i < num_maps; i++) {
		if (map[i].type == PIN_MAP_TYPE_MUX_GROUP)
			kfree(map[i].data.mux.group);
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_GROUP)
			kfree(map[i].data.configs.configs);
	}

	kfree(map);
}

static const struct pinctrl_ops mxs_pinctrl_ops = {
	.get_groups_count = mxs_get_groups_count,
	.get_group_name = mxs_get_group_name,
	.get_group_pins = mxs_get_group_pins,
	.pin_dbg_show = mxs_pin_dbg_show,
	.dt_node_to_map = mxs_dt_node_to_map,
	.dt_free_map = mxs_dt_free_map,
};

static int mxs_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct mxs_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	return d->soc->nfunctions;
}

static const char *mxs_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
					     unsigned function)
{
	struct mxs_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	return d->soc->functions[function].name;
}

static int mxs_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
				       unsigned group,
				       const char * const **groups,
				       unsigned * const num_groups)
{
	struct mxs_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	*groups = d->soc->functions[group].groups;
	*num_groups = d->soc->functions[group].ngroups;

	return 0;
}

static void mxs_pinctrl_rmwl(u32 value, u32 mask, u8 shift, void __iomem *reg)
{
	u32 tmp;

	tmp = readl(reg);
	tmp &= ~(mask << shift);
	tmp |= value << shift;
	writel(tmp, reg);
}

static int mxs_pinctrl_set_mux(struct pinctrl_dev *pctldev, unsigned selector,
			       unsigned group)
{
	struct mxs_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);
	struct mxs_group *g = &d->soc->groups[group];
	void __iomem *reg;
	u8 bank, shift;
	u16 pin;
	u32 i;

	for (i = 0; i < g->npins; i++) {
		bank = PINID_TO_BANK(g->pins[i]);
		pin = PINID_TO_PIN(g->pins[i]);
		reg = d->base + d->soc->regs->muxsel;
		reg += bank * 0x20 + pin / 16 * 0x10;
		shift = pin % 16 * 2;

		mxs_pinctrl_rmwl(g->muxsel[i], 0x3, shift, reg);
	}

	return 0;
}

static const struct pinmux_ops mxs_pinmux_ops = {
	.get_functions_count = mxs_pinctrl_get_funcs_count,
	.get_function_name = mxs_pinctrl_get_func_name,
	.get_function_groups = mxs_pinctrl_get_func_groups,
	.set_mux = mxs_pinctrl_set_mux,
};

static int mxs_pinconf_get(struct pinctrl_dev *pctldev,
			   unsigned pin, unsigned long *config)
{
	return -ENOTSUPP;
}

static int mxs_pinconf_set(struct pinctrl_dev *pctldev,
			   unsigned pin, unsigned long *configs,
			   unsigned num_configs)
{
	return -ENOTSUPP;
}

static int mxs_pinconf_group_get(struct pinctrl_dev *pctldev,
				 unsigned group, unsigned long *config)
{
	struct mxs_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);

	*config = d->soc->groups[group].config;

	return 0;
}

static int mxs_pinconf_group_set(struct pinctrl_dev *pctldev,
				 unsigned group, unsigned long *configs,
				 unsigned num_configs)
{
	struct mxs_pinctrl_data *d = pinctrl_dev_get_drvdata(pctldev);
	struct mxs_group *g = &d->soc->groups[group];
	void __iomem *reg;
	u8 ma, vol, pull, bank, shift;
	u16 pin;
	u32 i;
	int n;
	unsigned long config;

	for (n = 0; n < num_configs; n++) {
		config = configs[n];

		ma = PIN_CONFIG_TO_MA(config);
		vol = PIN_CONFIG_TO_VOL(config);
		pull = PIN_CONFIG_TO_PULL(config);

		for (i = 0; i < g->npins; i++) {
			bank = PINID_TO_BANK(g->pins[i]);
			pin = PINID_TO_PIN(g->pins[i]);

			/* drive */
			reg = d->base + d->soc->regs->drive;
			reg += bank * 0x40 + pin / 8 * 0x10;

			/* mA */
			if (config & MA_PRESENT) {
				shift = pin % 8 * 4;
				mxs_pinctrl_rmwl(ma, 0x3, shift, reg);
			}

			/* vol */
			if (config & VOL_PRESENT) {
				shift = pin % 8 * 4 + 2;
				if (vol)
					writel(1 << shift, reg + SET);
				else
					writel(1 << shift, reg + CLR);
			}

			/* pull */
			if (config & PULL_PRESENT) {
				reg = d->base + d->soc->regs->pull;
				reg += bank * 0x10;
				shift = pin;
				if (pull)
					writel(1 << shift, reg + SET);
				else
					writel(1 << shift, reg + CLR);
			}
		}

		/* cache the config value for mxs_pinconf_group_get() */
		g->config = config;

	} /* for each config */

	return 0;
}

static void mxs_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				 struct seq_file *s, unsigned pin)
{
	/* Not support */
}

static void mxs_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
				       struct seq_file *s, unsigned group)
{
	unsigned long config;

	if (!mxs_pinconf_group_get(pctldev, group, &config))
		seq_printf(s, "0x%lx", config);
}

static const struct pinconf_ops mxs_pinconf_ops = {
	.pin_config_get = mxs_pinconf_get,
	.pin_config_set = mxs_pinconf_set,
	.pin_config_group_get = mxs_pinconf_group_get,
	.pin_config_group_set = mxs_pinconf_group_set,
	.pin_config_dbg_show = mxs_pinconf_dbg_show,
	.pin_config_group_dbg_show = mxs_pinconf_group_dbg_show,
};

static struct pinctrl_desc mxs_pinctrl_desc = {
	.pctlops = &mxs_pinctrl_ops,
	.pmxops = &mxs_pinmux_ops,
	.confops = &mxs_pinconf_ops,
	.owner = THIS_MODULE,
};

static int mxs_pinctrl_parse_group(struct platform_device *pdev,
				   struct device_node *np, int idx,
				   const char **out_name)
{
	struct mxs_pinctrl_data *d = platform_get_drvdata(pdev);
	struct mxs_group *g = &d->soc->groups[idx];
	struct property *prop;
	const char *propname = "fsl,pinmux-ids";
	char *group;
	int length = strlen(np->name) + SUFFIX_LEN;
	u32 val, i;

	group = devm_kzalloc(&pdev->dev, length, GFP_KERNEL);
	if (!group)
		return -ENOMEM;
	if (of_property_read_u32(np, "reg", &val))
		snprintf(group, length, "%s", np->name);
	else
		snprintf(group, length, "%s.%d", np->name, val);
	g->name = group;

	prop = of_find_property(np, propname, &length);
	if (!prop)
		return -EINVAL;
	g->npins = length / sizeof(u32);

	g->pins = devm_kcalloc(&pdev->dev, g->npins, sizeof(*g->pins),
			       GFP_KERNEL);
	if (!g->pins)
		return -ENOMEM;

	g->muxsel = devm_kcalloc(&pdev->dev, g->npins, sizeof(*g->muxsel),
				 GFP_KERNEL);
	if (!g->muxsel)
		return -ENOMEM;

	of_property_read_u32_array(np, propname, g->pins, g->npins);
	for (i = 0; i < g->npins; i++) {
		g->muxsel[i] = MUXID_TO_MUXSEL(g->pins[i]);
		g->pins[i] = MUXID_TO_PINID(g->pins[i]);
	}

	if (out_name)
		*out_name = g->name;

	return 0;
}

static bool is_mxs_gpio(struct device_node *child)
{
	return of_device_is_compatible(child, "fsl,imx23-gpio") ||
	       of_device_is_compatible(child, "fsl,imx28-gpio");
}

static int mxs_pinctrl_probe_dt(struct platform_device *pdev,
				struct mxs_pinctrl_data *d)
{
	struct mxs_pinctrl_soc_data *soc = d->soc;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	struct mxs_function *f;
	const char *fn, *fnull = "";
	int i = 0, idxf = 0, idxg = 0;
	int ret;
	u32 val;

	val = of_get_child_count(np);
	if (val == 0) {
		dev_err(&pdev->dev, "no group is defined\n");
		return -ENOENT;
	}

	/* Count total functions and groups */
	fn = fnull;
	for_each_child_of_node(np, child) {
		if (is_mxs_gpio(child))
			continue;
		soc->ngroups++;
		/* Skip pure pinconf node */
		if (of_property_read_u32(child, "reg", &val))
			continue;
		if (strcmp(fn, child->name)) {
			fn = child->name;
			soc->nfunctions++;
		}
	}

	soc->functions = devm_kcalloc(&pdev->dev,
				      soc->nfunctions,
				      sizeof(*soc->functions),
				      GFP_KERNEL);
	if (!soc->functions)
		return -ENOMEM;

	soc->groups = devm_kcalloc(&pdev->dev,
				   soc->ngroups, sizeof(*soc->groups),
				   GFP_KERNEL);
	if (!soc->groups)
		return -ENOMEM;

	/* Count groups for each function */
	fn = fnull;
	f = &soc->functions[idxf];
	for_each_child_of_node(np, child) {
		if (is_mxs_gpio(child))
			continue;
		if (of_property_read_u32(child, "reg", &val))
			continue;
		if (strcmp(fn, child->name)) {
			struct device_node *child2;

			/*
			 * This reference is dropped by
			 * of_get_next_child(np, * child)
			 */
			of_node_get(child);

			/*
			 * The logic parsing the functions from dt currently
			 * doesn't handle if functions with the same name are
			 * not grouped together. Only the first contiguous
			 * cluster is usable for each function name. This is a
			 * bug that is not trivial to fix, but at least warn
			 * about it.
			 */
			for (child2 = of_get_next_child(np, child);
			     child2 != NULL;
			     child2 = of_get_next_child(np, child2)) {
				if (!strcmp(child2->name, fn))
					dev_warn(&pdev->dev,
						 "function nodes must be grouped by name (failed for: %s)",
						 fn);
			}

			f = &soc->functions[idxf++];
			f->name = fn = child->name;
		}
		f->ngroups++;
	}

	/* Get groups for each function */
	idxf = 0;
	fn = fnull;
	for_each_child_of_node_scoped(np, child) {
		if (is_mxs_gpio(child))
			continue;
		if (of_property_read_u32(child, "reg", &val)) {
			ret = mxs_pinctrl_parse_group(pdev, child,
						      idxg++, NULL);
			if (ret)
				return ret;
			continue;
		}

		if (strcmp(fn, child->name)) {
			f = &soc->functions[idxf++];
			f->groups = devm_kcalloc(&pdev->dev,
						 f->ngroups,
						 sizeof(*f->groups),
						 GFP_KERNEL);
			if (!f->groups)
				return -ENOMEM;
			fn = child->name;
			i = 0;
		}
		ret = mxs_pinctrl_parse_group(pdev, child, idxg++,
					      &f->groups[i++]);
		if (ret)
			return ret;
	}

	return 0;
}

int mxs_pinctrl_probe(struct platform_device *pdev,
		      struct mxs_pinctrl_soc_data *soc)
{
	struct device_node *np = pdev->dev.of_node;
	struct mxs_pinctrl_data *d;
	int ret;

	d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->dev = &pdev->dev;
	d->soc = soc;

	d->base = of_iomap(np, 0);
	if (!d->base)
		return -EADDRNOTAVAIL;

	mxs_pinctrl_desc.pins = d->soc->pins;
	mxs_pinctrl_desc.npins = d->soc->npins;
	mxs_pinctrl_desc.name = dev_name(&pdev->dev);

	platform_set_drvdata(pdev, d);

	ret = mxs_pinctrl_probe_dt(pdev, d);
	if (ret) {
		dev_err(&pdev->dev, "dt probe failed: %d\n", ret);
		goto err;
	}

	d->pctl = pinctrl_register(&mxs_pinctrl_desc, &pdev->dev, d);
	if (IS_ERR(d->pctl)) {
		dev_err(&pdev->dev, "Couldn't register MXS pinctrl driver\n");
		ret = PTR_ERR(d->pctl);
		goto err;
	}

	return 0;

err:
	iounmap(d->base);
	return ret;
}
