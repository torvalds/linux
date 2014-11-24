/*
 * Driver for the ST Microelectronics SPEAr pinmux
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * Inspired from:
 * - U300 Pinctl drivers
 * - Tegra Pinctl drivers
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "pinctrl-spear.h"

#define DRIVER_NAME "spear-pinmux"

static void muxregs_endisable(struct spear_pmx *pmx,
		struct spear_muxreg *muxregs, u8 count, bool enable)
{
	struct spear_muxreg *muxreg;
	u32 val, temp, j;

	for (j = 0; j < count; j++) {
		muxreg = &muxregs[j];

		val = pmx_readl(pmx, muxreg->reg);
		val &= ~muxreg->mask;

		if (enable)
			temp = muxreg->val;
		else
			temp = ~muxreg->val;

		val |= muxreg->mask & temp;
		pmx_writel(pmx, val, muxreg->reg);
	}
}

static int set_mode(struct spear_pmx *pmx, int mode)
{
	struct spear_pmx_mode *pmx_mode = NULL;
	int i;
	u32 val;

	if (!pmx->machdata->pmx_modes || !pmx->machdata->npmx_modes)
		return -EINVAL;

	for (i = 0; i < pmx->machdata->npmx_modes; i++) {
		if (pmx->machdata->pmx_modes[i]->mode == (1 << mode)) {
			pmx_mode = pmx->machdata->pmx_modes[i];
			break;
		}
	}

	if (!pmx_mode)
		return -EINVAL;

	val = pmx_readl(pmx, pmx_mode->reg);
	val &= ~pmx_mode->mask;
	val |= pmx_mode->val;
	pmx_writel(pmx, val, pmx_mode->reg);

	pmx->machdata->mode = pmx_mode->mode;
	dev_info(pmx->dev, "Configured Mode: %s with id: %x\n\n",
			pmx_mode->name ? pmx_mode->name : "no_name",
			pmx_mode->reg);

	return 0;
}

void pmx_init_gpio_pingroup_addr(struct spear_gpio_pingroup *gpio_pingroup,
				 unsigned count, u16 reg)
{
	int i, j;

	for (i = 0; i < count; i++)
		for (j = 0; j < gpio_pingroup[i].nmuxregs; j++)
			gpio_pingroup[i].muxregs[j].reg = reg;
}

void pmx_init_addr(struct spear_pinctrl_machdata *machdata, u16 reg)
{
	struct spear_pingroup *pgroup;
	struct spear_modemux *modemux;
	int i, j, group;

	for (group = 0; group < machdata->ngroups; group++) {
		pgroup = machdata->groups[group];

		for (i = 0; i < pgroup->nmodemuxs; i++) {
			modemux = &pgroup->modemuxs[i];

			for (j = 0; j < modemux->nmuxregs; j++)
				if (modemux->muxregs[j].reg == 0xFFFF)
					modemux->muxregs[j].reg = reg;
		}
	}
}

static int spear_pinctrl_get_groups_cnt(struct pinctrl_dev *pctldev)
{
	struct spear_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->machdata->ngroups;
}

static const char *spear_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
		unsigned group)
{
	struct spear_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->machdata->groups[group]->name;
}

static int spear_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
		unsigned group, const unsigned **pins, unsigned *num_pins)
{
	struct spear_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	*pins = pmx->machdata->groups[group]->pins;
	*num_pins = pmx->machdata->groups[group]->npins;

	return 0;
}

static void spear_pinctrl_pin_dbg_show(struct pinctrl_dev *pctldev,
		struct seq_file *s, unsigned offset)
{
	seq_printf(s, " " DRIVER_NAME);
}

static int spear_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
					struct device_node *np_config,
					struct pinctrl_map **map,
					unsigned *num_maps)
{
	struct spear_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct device_node *np;
	struct property *prop;
	const char *function, *group;
	int ret, index = 0, count = 0;

	/* calculate number of maps required */
	for_each_child_of_node(np_config, np) {
		ret = of_property_read_string(np, "st,function", &function);
		if (ret < 0)
			return ret;

		ret = of_property_count_strings(np, "st,pins");
		if (ret < 0)
			return ret;

		count += ret;
	}

	if (!count) {
		dev_err(pmx->dev, "No child nodes passed via DT\n");
		return -ENODEV;
	}

	*map = kzalloc(sizeof(**map) * count, GFP_KERNEL);
	if (!*map)
		return -ENOMEM;

	for_each_child_of_node(np_config, np) {
		of_property_read_string(np, "st,function", &function);
		of_property_for_each_string(np, "st,pins", prop, group) {
			(*map)[index].type = PIN_MAP_TYPE_MUX_GROUP;
			(*map)[index].data.mux.group = group;
			(*map)[index].data.mux.function = function;
			index++;
		}
	}

	*num_maps = count;

	return 0;
}

static void spear_pinctrl_dt_free_map(struct pinctrl_dev *pctldev,
				      struct pinctrl_map *map,
				      unsigned num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops spear_pinctrl_ops = {
	.get_groups_count = spear_pinctrl_get_groups_cnt,
	.get_group_name = spear_pinctrl_get_group_name,
	.get_group_pins = spear_pinctrl_get_group_pins,
	.pin_dbg_show = spear_pinctrl_pin_dbg_show,
	.dt_node_to_map = spear_pinctrl_dt_node_to_map,
	.dt_free_map = spear_pinctrl_dt_free_map,
};

static int spear_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct spear_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->machdata->nfunctions;
}

static const char *spear_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
		unsigned function)
{
	struct spear_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->machdata->functions[function]->name;
}

static int spear_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
		unsigned function, const char *const **groups,
		unsigned * const ngroups)
{
	struct spear_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);

	*groups = pmx->machdata->functions[function]->groups;
	*ngroups = pmx->machdata->functions[function]->ngroups;

	return 0;
}

static int spear_pinctrl_endisable(struct pinctrl_dev *pctldev,
		unsigned function, unsigned group, bool enable)
{
	struct spear_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	const struct spear_pingroup *pgroup;
	const struct spear_modemux *modemux;
	int i;
	bool found = false;

	pgroup = pmx->machdata->groups[group];

	for (i = 0; i < pgroup->nmodemuxs; i++) {
		modemux = &pgroup->modemuxs[i];

		/* SoC have any modes */
		if (pmx->machdata->modes_supported) {
			if (!(pmx->machdata->mode & modemux->modes))
				continue;
		}

		found = true;
		muxregs_endisable(pmx, modemux->muxregs, modemux->nmuxregs,
				enable);
	}

	if (!found) {
		dev_err(pmx->dev, "pinmux group: %s not supported\n",
				pgroup->name);
		return -ENODEV;
	}

	return 0;
}

static int spear_pinctrl_set_mux(struct pinctrl_dev *pctldev, unsigned function,
		unsigned group)
{
	return spear_pinctrl_endisable(pctldev, function, group, true);
}

/* gpio with pinmux */
static struct spear_gpio_pingroup *get_gpio_pingroup(struct spear_pmx *pmx,
		unsigned pin)
{
	struct spear_gpio_pingroup *gpio_pingroup;
	int i, j;

	if (!pmx->machdata->gpio_pingroups)
		return NULL;

	for (i = 0; i < pmx->machdata->ngpio_pingroups; i++) {
		gpio_pingroup = &pmx->machdata->gpio_pingroups[i];

		for (j = 0; j < gpio_pingroup->npins; j++) {
			if (gpio_pingroup->pins[j] == pin)
				return gpio_pingroup;
		}
	}

	return NULL;
}

static int gpio_request_endisable(struct pinctrl_dev *pctldev,
		struct pinctrl_gpio_range *range, unsigned offset, bool enable)
{
	struct spear_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct spear_pinctrl_machdata *machdata = pmx->machdata;
	struct spear_gpio_pingroup *gpio_pingroup;

	/*
	 * Some SoC have configuration options applicable to group of pins,
	 * rather than a single pin.
	 */
	gpio_pingroup = get_gpio_pingroup(pmx, offset);
	if (gpio_pingroup)
		muxregs_endisable(pmx, gpio_pingroup->muxregs,
				gpio_pingroup->nmuxregs, enable);

	/*
	 * SoC may need some extra configurations, or configurations for single
	 * pin
	 */
	if (machdata->gpio_request_endisable)
		machdata->gpio_request_endisable(pmx, offset, enable);

	return 0;
}

static int gpio_request_enable(struct pinctrl_dev *pctldev,
		struct pinctrl_gpio_range *range, unsigned offset)
{
	return gpio_request_endisable(pctldev, range, offset, true);
}

static void gpio_disable_free(struct pinctrl_dev *pctldev,
		struct pinctrl_gpio_range *range, unsigned offset)
{
	gpio_request_endisable(pctldev, range, offset, false);
}

static const struct pinmux_ops spear_pinmux_ops = {
	.get_functions_count = spear_pinctrl_get_funcs_count,
	.get_function_name = spear_pinctrl_get_func_name,
	.get_function_groups = spear_pinctrl_get_func_groups,
	.set_mux = spear_pinctrl_set_mux,
	.gpio_request_enable = gpio_request_enable,
	.gpio_disable_free = gpio_disable_free,
};

static struct pinctrl_desc spear_pinctrl_desc = {
	.name = DRIVER_NAME,
	.pctlops = &spear_pinctrl_ops,
	.pmxops = &spear_pinmux_ops,
	.owner = THIS_MODULE,
};

int spear_pinctrl_probe(struct platform_device *pdev,
			struct spear_pinctrl_machdata *machdata)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct spear_pmx *pmx;

	if (!machdata)
		return -ENODEV;

	pmx = devm_kzalloc(&pdev->dev, sizeof(*pmx), GFP_KERNEL);
	if (!pmx) {
		dev_err(&pdev->dev, "Can't alloc spear_pmx\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pmx->vbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pmx->vbase))
		return PTR_ERR(pmx->vbase);

	pmx->dev = &pdev->dev;
	pmx->machdata = machdata;

	/* configure mode, if supported by SoC */
	if (machdata->modes_supported) {
		int mode = 0;

		if (of_property_read_u32(np, "st,pinmux-mode", &mode)) {
			dev_err(&pdev->dev, "OF: pinmux mode not passed\n");
			return -EINVAL;
		}

		if (set_mode(pmx, mode)) {
			dev_err(&pdev->dev, "OF: Couldn't configure mode: %x\n",
					mode);
			return -EINVAL;
		}
	}

	platform_set_drvdata(pdev, pmx);

	spear_pinctrl_desc.pins = machdata->pins;
	spear_pinctrl_desc.npins = machdata->npins;

	pmx->pctl = pinctrl_register(&spear_pinctrl_desc, &pdev->dev, pmx);
	if (!pmx->pctl) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return -ENODEV;
	}

	return 0;
}

int spear_pinctrl_remove(struct platform_device *pdev)
{
	struct spear_pmx *pmx = platform_get_drvdata(pdev);

	pinctrl_unregister(pmx->pctl);

	return 0;
}
