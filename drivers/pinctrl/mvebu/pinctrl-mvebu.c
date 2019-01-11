/*
 * Marvell MVEBU pinctrl core driver
 *
 * Authors: Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *          Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pinctrl-mvebu.h"

#define MPPS_PER_REG	8
#define MPP_BITS	4
#define MPP_MASK	0xf

struct mvebu_pinctrl_function {
	const char *name;
	const char **groups;
	unsigned num_groups;
};

struct mvebu_pinctrl_group {
	const char *name;
	const struct mvebu_mpp_ctrl *ctrl;
	struct mvebu_mpp_ctrl_data *data;
	struct mvebu_mpp_ctrl_setting *settings;
	unsigned num_settings;
	unsigned gid;
	unsigned *pins;
	unsigned npins;
};

struct mvebu_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctldev;
	struct pinctrl_desc desc;
	struct mvebu_pinctrl_group *groups;
	unsigned num_groups;
	struct mvebu_pinctrl_function *functions;
	unsigned num_functions;
	u8 variant;
};

int mvebu_mmio_mpp_ctrl_get(struct mvebu_mpp_ctrl_data *data,
			     unsigned int pid, unsigned long *config)
{
	unsigned off = (pid / MVEBU_MPPS_PER_REG) * MVEBU_MPP_BITS;
	unsigned shift = (pid % MVEBU_MPPS_PER_REG) * MVEBU_MPP_BITS;

	*config = (readl(data->base + off) >> shift) & MVEBU_MPP_MASK;

	return 0;
}

int mvebu_mmio_mpp_ctrl_set(struct mvebu_mpp_ctrl_data *data,
			     unsigned int pid, unsigned long config)
{
	unsigned off = (pid / MVEBU_MPPS_PER_REG) * MVEBU_MPP_BITS;
	unsigned shift = (pid % MVEBU_MPPS_PER_REG) * MVEBU_MPP_BITS;
	unsigned long reg;

	reg = readl(data->base + off) & ~(MVEBU_MPP_MASK << shift);
	writel(reg | (config << shift), data->base + off);

	return 0;
}

static struct mvebu_pinctrl_group *mvebu_pinctrl_find_group_by_pid(
	struct mvebu_pinctrl *pctl, unsigned pid)
{
	unsigned n;
	for (n = 0; n < pctl->num_groups; n++) {
		if (pid >= pctl->groups[n].pins[0] &&
		    pid < pctl->groups[n].pins[0] +
			pctl->groups[n].npins)
			return &pctl->groups[n];
	}
	return NULL;
}

static struct mvebu_pinctrl_group *mvebu_pinctrl_find_group_by_name(
	struct mvebu_pinctrl *pctl, const char *name)
{
	unsigned n;
	for (n = 0; n < pctl->num_groups; n++) {
		if (strcmp(name, pctl->groups[n].name) == 0)
			return &pctl->groups[n];
	}
	return NULL;
}

static struct mvebu_mpp_ctrl_setting *mvebu_pinctrl_find_setting_by_val(
	struct mvebu_pinctrl *pctl, struct mvebu_pinctrl_group *grp,
	unsigned long config)
{
	unsigned n;
	for (n = 0; n < grp->num_settings; n++) {
		if (config == grp->settings[n].val) {
			if (!pctl->variant || (pctl->variant &
					       grp->settings[n].variant))
				return &grp->settings[n];
		}
	}
	return NULL;
}

static struct mvebu_mpp_ctrl_setting *mvebu_pinctrl_find_setting_by_name(
	struct mvebu_pinctrl *pctl, struct mvebu_pinctrl_group *grp,
	const char *name)
{
	unsigned n;
	for (n = 0; n < grp->num_settings; n++) {
		if (strcmp(name, grp->settings[n].name) == 0) {
			if (!pctl->variant || (pctl->variant &
					       grp->settings[n].variant))
				return &grp->settings[n];
		}
	}
	return NULL;
}

static struct mvebu_mpp_ctrl_setting *mvebu_pinctrl_find_gpio_setting(
	struct mvebu_pinctrl *pctl, struct mvebu_pinctrl_group *grp)
{
	unsigned n;
	for (n = 0; n < grp->num_settings; n++) {
		if (grp->settings[n].flags &
			(MVEBU_SETTING_GPO | MVEBU_SETTING_GPI)) {
			if (!pctl->variant || (pctl->variant &
						grp->settings[n].variant))
				return &grp->settings[n];
		}
	}
	return NULL;
}

static struct mvebu_pinctrl_function *mvebu_pinctrl_find_function_by_name(
	struct mvebu_pinctrl *pctl, const char *name)
{
	unsigned n;
	for (n = 0; n < pctl->num_functions; n++) {
		if (strcmp(name, pctl->functions[n].name) == 0)
			return &pctl->functions[n];
	}
	return NULL;
}

static int mvebu_pinconf_group_get(struct pinctrl_dev *pctldev,
				unsigned gid, unsigned long *config)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mvebu_pinctrl_group *grp = &pctl->groups[gid];

	if (!grp->ctrl)
		return -EINVAL;

	return grp->ctrl->mpp_get(grp->data, grp->pins[0], config);
}

static int mvebu_pinconf_group_set(struct pinctrl_dev *pctldev,
				unsigned gid, unsigned long *configs,
				unsigned num_configs)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mvebu_pinctrl_group *grp = &pctl->groups[gid];
	int i, ret;

	if (!grp->ctrl)
		return -EINVAL;

	for (i = 0; i < num_configs; i++) {
		ret = grp->ctrl->mpp_set(grp->data, grp->pins[0], configs[i]);
		if (ret)
			return ret;
	} /* for each config */

	return 0;
}

static void mvebu_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
					struct seq_file *s, unsigned gid)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mvebu_pinctrl_group *grp = &pctl->groups[gid];
	struct mvebu_mpp_ctrl_setting *curr;
	unsigned long config;
	unsigned n;

	if (mvebu_pinconf_group_get(pctldev, gid, &config))
		return;

	curr = mvebu_pinctrl_find_setting_by_val(pctl, grp, config);

	if (curr) {
		seq_printf(s, "current: %s", curr->name);
		if (curr->subname)
			seq_printf(s, "(%s)", curr->subname);
		if (curr->flags & (MVEBU_SETTING_GPO | MVEBU_SETTING_GPI)) {
			seq_putc(s, '(');
			if (curr->flags & MVEBU_SETTING_GPI)
				seq_putc(s, 'i');
			if (curr->flags & MVEBU_SETTING_GPO)
				seq_putc(s, 'o');
			seq_putc(s, ')');
		}
	} else {
		seq_puts(s, "current: UNKNOWN");
	}

	if (grp->num_settings > 1) {
		seq_puts(s, ", available = [");
		for (n = 0; n < grp->num_settings; n++) {
			if (curr == &grp->settings[n])
				continue;

			/* skip unsupported settings for this variant */
			if (pctl->variant &&
			    !(pctl->variant & grp->settings[n].variant))
				continue;

			seq_printf(s, " %s", grp->settings[n].name);
			if (grp->settings[n].subname)
				seq_printf(s, "(%s)", grp->settings[n].subname);
			if (grp->settings[n].flags &
				(MVEBU_SETTING_GPO | MVEBU_SETTING_GPI)) {
				seq_putc(s, '(');
				if (grp->settings[n].flags & MVEBU_SETTING_GPI)
					seq_putc(s, 'i');
				if (grp->settings[n].flags & MVEBU_SETTING_GPO)
					seq_putc(s, 'o');
				seq_putc(s, ')');
			}
		}
		seq_puts(s, " ]");
	}
}

static const struct pinconf_ops mvebu_pinconf_ops = {
	.pin_config_group_get = mvebu_pinconf_group_get,
	.pin_config_group_set = mvebu_pinconf_group_set,
	.pin_config_group_dbg_show = mvebu_pinconf_group_dbg_show,
};

static int mvebu_pinmux_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->num_functions;
}

static const char *mvebu_pinmux_get_func_name(struct pinctrl_dev *pctldev,
					unsigned fid)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->functions[fid].name;
}

static int mvebu_pinmux_get_groups(struct pinctrl_dev *pctldev, unsigned fid,
				const char * const **groups,
				unsigned * const num_groups)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctl->functions[fid].groups;
	*num_groups = pctl->functions[fid].num_groups;
	return 0;
}

static int mvebu_pinmux_set(struct pinctrl_dev *pctldev, unsigned fid,
			    unsigned gid)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mvebu_pinctrl_function *func = &pctl->functions[fid];
	struct mvebu_pinctrl_group *grp = &pctl->groups[gid];
	struct mvebu_mpp_ctrl_setting *setting;
	int ret;
	unsigned long config;

	setting = mvebu_pinctrl_find_setting_by_name(pctl, grp,
						     func->name);
	if (!setting) {
		dev_err(pctl->dev,
			"unable to find setting %s in group %s\n",
			func->name, func->groups[gid]);
		return -EINVAL;
	}

	config = setting->val;
	ret = mvebu_pinconf_group_set(pctldev, grp->gid, &config, 1);
	if (ret) {
		dev_err(pctl->dev, "cannot set group %s to %s\n",
			func->groups[gid], func->name);
		return ret;
	}

	return 0;
}

static int mvebu_pinmux_gpio_request_enable(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range, unsigned offset)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mvebu_pinctrl_group *grp;
	struct mvebu_mpp_ctrl_setting *setting;
	unsigned long config;

	grp = mvebu_pinctrl_find_group_by_pid(pctl, offset);
	if (!grp)
		return -EINVAL;

	if (grp->ctrl->mpp_gpio_req)
		return grp->ctrl->mpp_gpio_req(grp->data, offset);

	setting = mvebu_pinctrl_find_gpio_setting(pctl, grp);
	if (!setting)
		return -ENOTSUPP;

	config = setting->val;

	return mvebu_pinconf_group_set(pctldev, grp->gid, &config, 1);
}

static int mvebu_pinmux_gpio_set_direction(struct pinctrl_dev *pctldev,
	   struct pinctrl_gpio_range *range, unsigned offset, bool input)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct mvebu_pinctrl_group *grp;
	struct mvebu_mpp_ctrl_setting *setting;

	grp = mvebu_pinctrl_find_group_by_pid(pctl, offset);
	if (!grp)
		return -EINVAL;

	if (grp->ctrl->mpp_gpio_dir)
		return grp->ctrl->mpp_gpio_dir(grp->data, offset, input);

	setting = mvebu_pinctrl_find_gpio_setting(pctl, grp);
	if (!setting)
		return -ENOTSUPP;

	if ((input && (setting->flags & MVEBU_SETTING_GPI)) ||
	    (!input && (setting->flags & MVEBU_SETTING_GPO)))
		return 0;

	return -ENOTSUPP;
}

static const struct pinmux_ops mvebu_pinmux_ops = {
	.get_functions_count = mvebu_pinmux_get_funcs_count,
	.get_function_name = mvebu_pinmux_get_func_name,
	.get_function_groups = mvebu_pinmux_get_groups,
	.gpio_request_enable = mvebu_pinmux_gpio_request_enable,
	.gpio_set_direction = mvebu_pinmux_gpio_set_direction,
	.set_mux = mvebu_pinmux_set,
};

static int mvebu_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	return pctl->num_groups;
}

static const char *mvebu_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						unsigned gid)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	return pctl->groups[gid].name;
}

static int mvebu_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned gid, const unsigned **pins,
					unsigned *num_pins)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	*pins = pctl->groups[gid].pins;
	*num_pins = pctl->groups[gid].npins;
	return 0;
}

static int mvebu_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
					struct device_node *np,
					struct pinctrl_map **map,
					unsigned *num_maps)
{
	struct mvebu_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct property *prop;
	const char *function;
	const char *group;
	int ret, nmaps, n;

	*map = NULL;
	*num_maps = 0;

	ret = of_property_read_string(np, "marvell,function", &function);
	if (ret) {
		dev_err(pctl->dev,
			"missing marvell,function in node %s\n", np->name);
		return 0;
	}

	nmaps = of_property_count_strings(np, "marvell,pins");
	if (nmaps < 0) {
		dev_err(pctl->dev,
			"missing marvell,pins in node %s\n", np->name);
		return 0;
	}

	*map = kmalloc_array(nmaps, sizeof(**map), GFP_KERNEL);
	if (!*map)
		return -ENOMEM;

	n = 0;
	of_property_for_each_string(np, "marvell,pins", prop, group) {
		struct mvebu_pinctrl_group *grp =
			mvebu_pinctrl_find_group_by_name(pctl, group);

		if (!grp) {
			dev_err(pctl->dev, "unknown pin %s", group);
			continue;
		}

		if (!mvebu_pinctrl_find_setting_by_name(pctl, grp, function)) {
			dev_err(pctl->dev, "unsupported function %s on pin %s",
				function, group);
			continue;
		}

		(*map)[n].type = PIN_MAP_TYPE_MUX_GROUP;
		(*map)[n].data.mux.group = group;
		(*map)[n].data.mux.function = function;
		n++;
	}

	*num_maps = nmaps;

	return 0;
}

static void mvebu_pinctrl_dt_free_map(struct pinctrl_dev *pctldev,
				struct pinctrl_map *map, unsigned num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops mvebu_pinctrl_ops = {
	.get_groups_count = mvebu_pinctrl_get_groups_count,
	.get_group_name = mvebu_pinctrl_get_group_name,
	.get_group_pins = mvebu_pinctrl_get_group_pins,
	.dt_node_to_map = mvebu_pinctrl_dt_node_to_map,
	.dt_free_map = mvebu_pinctrl_dt_free_map,
};

static int _add_function(struct mvebu_pinctrl_function *funcs, int *funcsize,
			const char *name)
{
	if (*funcsize <= 0)
		return -EOVERFLOW;

	while (funcs->num_groups) {
		/* function already there */
		if (strcmp(funcs->name, name) == 0) {
			funcs->num_groups++;
			return -EEXIST;
		}
		funcs++;
	}

	/* append new unique function */
	funcs->name = name;
	funcs->num_groups = 1;
	(*funcsize)--;

	return 0;
}

static int mvebu_pinctrl_build_functions(struct platform_device *pdev,
					 struct mvebu_pinctrl *pctl)
{
	struct mvebu_pinctrl_function *funcs;
	int num = 0, funcsize = pctl->desc.npins;
	int n, s;

	/* we allocate functions for number of pins and hope
	 * there are fewer unique functions than pins available */
	funcs = devm_kcalloc(&pdev->dev,
			     funcsize, sizeof(struct mvebu_pinctrl_function),
			     GFP_KERNEL);
	if (!funcs)
		return -ENOMEM;

	for (n = 0; n < pctl->num_groups; n++) {
		struct mvebu_pinctrl_group *grp = &pctl->groups[n];
		for (s = 0; s < grp->num_settings; s++) {
			int ret;

			/* skip unsupported settings on this variant */
			if (pctl->variant &&
			    !(pctl->variant & grp->settings[s].variant))
				continue;

			/* check for unique functions and count groups */
			ret = _add_function(funcs, &funcsize,
					    grp->settings[s].name);
			if (ret == -EOVERFLOW)
				dev_err(&pdev->dev,
					"More functions than pins(%d)\n",
					pctl->desc.npins);
			if (ret < 0)
				continue;

			num++;
		}
	}

	pctl->num_functions = num;
	pctl->functions = funcs;

	for (n = 0; n < pctl->num_groups; n++) {
		struct mvebu_pinctrl_group *grp = &pctl->groups[n];
		for (s = 0; s < grp->num_settings; s++) {
			struct mvebu_pinctrl_function *f;
			const char **groups;

			/* skip unsupported settings on this variant */
			if (pctl->variant &&
			    !(pctl->variant & grp->settings[s].variant))
				continue;

			f = mvebu_pinctrl_find_function_by_name(pctl,
							grp->settings[s].name);

			/* allocate group name array if not done already */
			if (!f->groups) {
				f->groups = devm_kcalloc(&pdev->dev,
						 f->num_groups,
						 sizeof(char *),
						 GFP_KERNEL);
				if (!f->groups)
					return -ENOMEM;
			}

			/* find next free group name and assign current name */
			groups = f->groups;
			while (*groups)
				groups++;
			*groups = grp->name;
		}
	}

	return 0;
}

int mvebu_pinctrl_probe(struct platform_device *pdev)
{
	struct mvebu_pinctrl_soc_info *soc = dev_get_platdata(&pdev->dev);
	struct mvebu_pinctrl *pctl;
	struct pinctrl_pin_desc *pdesc;
	unsigned gid, n, k;
	unsigned size, noname = 0;
	char *noname_buf;
	void *p;
	int ret;

	if (!soc || !soc->controls || !soc->modes) {
		dev_err(&pdev->dev, "wrong pinctrl soc info\n");
		return -EINVAL;
	}

	pctl = devm_kzalloc(&pdev->dev, sizeof(struct mvebu_pinctrl),
			GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;

	pctl->desc.name = dev_name(&pdev->dev);
	pctl->desc.owner = THIS_MODULE;
	pctl->desc.pctlops = &mvebu_pinctrl_ops;
	pctl->desc.pmxops = &mvebu_pinmux_ops;
	pctl->desc.confops = &mvebu_pinconf_ops;
	pctl->variant = soc->variant;
	pctl->dev = &pdev->dev;
	platform_set_drvdata(pdev, pctl);

	/* count controls and create names for mvebu generic
	   register controls; also does sanity checks */
	pctl->num_groups = 0;
	pctl->desc.npins = 0;
	for (n = 0; n < soc->ncontrols; n++) {
		const struct mvebu_mpp_ctrl *ctrl = &soc->controls[n];

		pctl->desc.npins += ctrl->npins;
		/* initialize control's pins[] array */
		for (k = 0; k < ctrl->npins; k++)
			ctrl->pins[k] = ctrl->pid + k;

		/*
		 * We allow to pass controls with NULL name that we treat
		 * as a range of one-pin groups with generic mvebu register
		 * controls.
		 */
		if (!ctrl->name) {
			pctl->num_groups += ctrl->npins;
			noname += ctrl->npins;
		} else {
			pctl->num_groups += 1;
		}
	}

	pdesc = devm_kcalloc(&pdev->dev,
			     pctl->desc.npins,
			     sizeof(struct pinctrl_pin_desc),
			     GFP_KERNEL);
	if (!pdesc)
		return -ENOMEM;

	for (n = 0; n < pctl->desc.npins; n++)
		pdesc[n].number = n;
	pctl->desc.pins = pdesc;

	/*
	 * allocate groups and name buffers for unnamed groups.
	 */
	size = pctl->num_groups * sizeof(*pctl->groups) + noname * 8;
	p = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pctl->groups = p;
	noname_buf = p + pctl->num_groups * sizeof(*pctl->groups);

	/* assign mpp controls to groups */
	gid = 0;
	for (n = 0; n < soc->ncontrols; n++) {
		const struct mvebu_mpp_ctrl *ctrl = &soc->controls[n];
		struct mvebu_mpp_ctrl_data *data = soc->control_data ?
						   &soc->control_data[n] : NULL;

		pctl->groups[gid].gid = gid;
		pctl->groups[gid].ctrl = ctrl;
		pctl->groups[gid].data = data;
		pctl->groups[gid].name = ctrl->name;
		pctl->groups[gid].pins = ctrl->pins;
		pctl->groups[gid].npins = ctrl->npins;

		/*
		 * We treat unnamed controls as a range of one-pin groups
		 * with generic mvebu register controls. Use one group for
		 * each in this range and assign a default group name.
		 */
		if (!ctrl->name) {
			pctl->groups[gid].name = noname_buf;
			pctl->groups[gid].npins = 1;
			sprintf(noname_buf, "mpp%d", ctrl->pid+0);
			noname_buf += 8;

			for (k = 1; k < ctrl->npins; k++) {
				gid++;
				pctl->groups[gid].gid = gid;
				pctl->groups[gid].ctrl = ctrl;
				pctl->groups[gid].data = data;
				pctl->groups[gid].name = noname_buf;
				pctl->groups[gid].pins = &ctrl->pins[k];
				pctl->groups[gid].npins = 1;
				sprintf(noname_buf, "mpp%d", ctrl->pid+k);
				noname_buf += 8;
			}
		}
		gid++;
	}

	/* assign mpp modes to groups */
	for (n = 0; n < soc->nmodes; n++) {
		struct mvebu_mpp_mode *mode = &soc->modes[n];
		struct mvebu_mpp_ctrl_setting *set = &mode->settings[0];
		struct mvebu_pinctrl_group *grp;
		unsigned num_settings;
		unsigned supp_settings;

		for (num_settings = 0, supp_settings = 0; ; set++) {
			if (!set->name)
				break;

			num_settings++;

			/* skip unsupported settings for this variant */
			if (pctl->variant && !(pctl->variant & set->variant))
				continue;

			supp_settings++;

			/* find gpio/gpo/gpi settings */
			if (strcmp(set->name, "gpio") == 0)
				set->flags = MVEBU_SETTING_GPI |
					MVEBU_SETTING_GPO;
			else if (strcmp(set->name, "gpo") == 0)
				set->flags = MVEBU_SETTING_GPO;
			else if (strcmp(set->name, "gpi") == 0)
				set->flags = MVEBU_SETTING_GPI;
		}

		/* skip modes with no settings for this variant */
		if (!supp_settings)
			continue;

		grp = mvebu_pinctrl_find_group_by_pid(pctl, mode->pid);
		if (!grp) {
			dev_warn(&pdev->dev, "unknown pinctrl group %d\n",
				mode->pid);
			continue;
		}

		grp->settings = mode->settings;
		grp->num_settings = num_settings;
	}

	ret = mvebu_pinctrl_build_functions(pdev, pctl);
	if (ret) {
		dev_err(&pdev->dev, "unable to build functions\n");
		return ret;
	}

	pctl->pctldev = devm_pinctrl_register(&pdev->dev, &pctl->desc, pctl);
	if (IS_ERR(pctl->pctldev)) {
		dev_err(&pdev->dev, "unable to register pinctrl driver\n");
		return PTR_ERR(pctl->pctldev);
	}

	dev_info(&pdev->dev, "registered pinctrl driver\n");

	/* register gpio ranges */
	for (n = 0; n < soc->ngpioranges; n++)
		pinctrl_add_gpio_range(pctl->pctldev, &soc->gpioranges[n]);

	return 0;
}

/*
 * mvebu_pinctrl_simple_mmio_probe - probe a simple mmio pinctrl
 * @pdev: platform device (with platform data already attached)
 *
 * Initialise a simple (single base address) mmio pinctrl driver,
 * assigning the MMIO base address to all mvebu mpp ctrl instances.
 */
int mvebu_pinctrl_simple_mmio_probe(struct platform_device *pdev)
{
	struct mvebu_pinctrl_soc_info *soc = dev_get_platdata(&pdev->dev);
	struct mvebu_mpp_ctrl_data *mpp_data;
	struct resource *res;
	void __iomem *base;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	mpp_data = devm_kcalloc(&pdev->dev, soc->ncontrols, sizeof(*mpp_data),
				GFP_KERNEL);
	if (!mpp_data)
		return -ENOMEM;

	for (i = 0; i < soc->ncontrols; i++)
		mpp_data[i].base = base;

	soc->control_data = mpp_data;

	return mvebu_pinctrl_probe(pdev);
}

int mvebu_regmap_mpp_ctrl_get(struct mvebu_mpp_ctrl_data *data,
			      unsigned int pid, unsigned long *config)
{
	unsigned off = (pid / MVEBU_MPPS_PER_REG) * MVEBU_MPP_BITS;
	unsigned shift = (pid % MVEBU_MPPS_PER_REG) * MVEBU_MPP_BITS;
	unsigned int val;
	int err;

	err = regmap_read(data->regmap.map, data->regmap.offset + off, &val);
	if (err)
		return err;

	*config = (val >> shift) & MVEBU_MPP_MASK;

	return 0;
}

int mvebu_regmap_mpp_ctrl_set(struct mvebu_mpp_ctrl_data *data,
			      unsigned int pid, unsigned long config)
{
	unsigned off = (pid / MVEBU_MPPS_PER_REG) * MVEBU_MPP_BITS;
	unsigned shift = (pid % MVEBU_MPPS_PER_REG) * MVEBU_MPP_BITS;

	return regmap_update_bits(data->regmap.map, data->regmap.offset + off,
				  MVEBU_MPP_MASK << shift, config << shift);
}

int mvebu_pinctrl_simple_regmap_probe(struct platform_device *pdev,
				      struct device *syscon_dev, u32 offset)
{
	struct mvebu_pinctrl_soc_info *soc = dev_get_platdata(&pdev->dev);
	struct mvebu_mpp_ctrl_data *mpp_data;
	struct regmap *regmap;
	int i;

	regmap = syscon_node_to_regmap(syscon_dev->of_node);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	mpp_data = devm_kcalloc(&pdev->dev, soc->ncontrols, sizeof(*mpp_data),
				GFP_KERNEL);
	if (!mpp_data)
		return -ENOMEM;

	for (i = 0; i < soc->ncontrols; i++) {
		mpp_data[i].regmap.map = regmap;
		mpp_data[i].regmap.offset = offset;
	}

	soc->control_data = mpp_data;

	return mvebu_pinctrl_probe(pdev);
}
