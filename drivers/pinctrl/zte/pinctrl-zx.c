/*
 * Copyright (C) 2017 Sanechips Technology Co., Ltd.
 * Copyright 2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "../core.h"
#include "../pinctrl-utils.h"
#include "../pinmux.h"
#include "pinctrl-zx.h"

#define ZX_PULL_DOWN		BIT(0)
#define ZX_PULL_UP		BIT(1)
#define ZX_INPUT_ENABLE		BIT(3)
#define ZX_DS_SHIFT		4
#define ZX_DS_MASK		(0x7 << ZX_DS_SHIFT)
#define ZX_DS_VALUE(x)		(((x) << ZX_DS_SHIFT) & ZX_DS_MASK)
#define ZX_SLEW			BIT(8)

struct zx_pinctrl {
	struct pinctrl_dev *pctldev;
	struct device *dev;
	void __iomem *base;
	void __iomem *aux_base;
	spinlock_t lock;
	struct zx_pinctrl_soc_info *info;
};

static int zx_dt_node_to_map(struct pinctrl_dev *pctldev,
			     struct device_node *np_config,
			     struct pinctrl_map **map, u32 *num_maps)
{
	return pinconf_generic_dt_node_to_map(pctldev, np_config, map,
					      num_maps, PIN_MAP_TYPE_INVALID);
}

static const struct pinctrl_ops zx_pinctrl_ops = {
	.dt_node_to_map = zx_dt_node_to_map,
	.dt_free_map = pinctrl_utils_free_map,
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
};

#define NONAON_MVAL 2

static int zx_set_mux(struct pinctrl_dev *pctldev, unsigned int func_selector,
		      unsigned int group_selector)
{
	struct zx_pinctrl *zpctl = pinctrl_dev_get_drvdata(pctldev);
	struct zx_pinctrl_soc_info *info = zpctl->info;
	const struct pinctrl_pin_desc *pindesc = info->pins + group_selector;
	struct zx_pin_data *data = pindesc->drv_data;
	struct zx_mux_desc *mux;
	u32 mask, offset, bitpos;
	struct function_desc *func;
	unsigned long flags;
	u32 val, mval;

	/* Skip reserved pin */
	if (!data)
		return -EINVAL;

	mux = data->muxes;
	mask = (1 << data->width) - 1;
	offset = data->offset;
	bitpos = data->bitpos;

	func = pinmux_generic_get_function(pctldev, func_selector);
	if (!func)
		return -EINVAL;

	while (mux->name) {
		if (strcmp(mux->name, func->name) == 0)
			break;
		mux++;
	}

	/* Found mux value to be written */
	mval = mux->muxval;

	spin_lock_irqsave(&zpctl->lock, flags);

	if (data->aon_pin) {
		/*
		 * It's an AON pin, whose mux register offset and bit position
		 * can be caluculated from pin number.  Each register covers 16
		 * pins, and each pin occupies 2 bits.
		 */
		u16 aoffset = pindesc->number / 16 * 4;
		u16 abitpos = (pindesc->number % 16) * 2;

		if (mval & AON_MUX_FLAG) {
			/*
			 * This is a mux value that needs to be written into
			 * AON pinmux register.  Write it and then we're done.
			 */
			val = readl(zpctl->aux_base + aoffset);
			val &= ~(0x3 << abitpos);
			val |= (mval & 0x3) << abitpos;
			writel(val, zpctl->aux_base + aoffset);
		} else {
			/*
			 * It's a mux value that needs to be written into TOP
			 * pinmux register.
			 */
			val = readl(zpctl->base + offset);
			val &= ~(mask << bitpos);
			val |= (mval & mask) << bitpos;
			writel(val, zpctl->base + offset);

			/*
			 * In this case, the AON pinmux register needs to be
			 * set up to select non-AON function.
			 */
			val = readl(zpctl->aux_base + aoffset);
			val &= ~(0x3 << abitpos);
			val |= NONAON_MVAL << abitpos;
			writel(val, zpctl->aux_base + aoffset);
		}

	} else {
		/*
		 * This is a TOP pin, and we only need to set up TOP pinmux
		 * register and then we're done with it.
		 */
		val = readl(zpctl->base + offset);
		val &= ~(mask << bitpos);
		val |= (mval & mask) << bitpos;
		writel(val, zpctl->base + offset);
	}

	spin_unlock_irqrestore(&zpctl->lock, flags);

	return 0;
}

static const struct pinmux_ops zx_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = zx_set_mux,
};

static int zx_pin_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
			     unsigned long *config)
{
	struct zx_pinctrl *zpctl = pinctrl_dev_get_drvdata(pctldev);
	struct zx_pinctrl_soc_info *info = zpctl->info;
	const struct pinctrl_pin_desc *pindesc = info->pins + pin;
	struct zx_pin_data *data = pindesc->drv_data;
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 val;

	/* Skip reserved pin */
	if (!data)
		return -EINVAL;

	val = readl(zpctl->aux_base + data->coffset);
	val = val >> data->cbitpos;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_DOWN:
		val &= ZX_PULL_DOWN;
		val = !!val;
		if (val == 0)
			return -EINVAL;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		val &= ZX_PULL_UP;
		val = !!val;
		if (val == 0)
			return -EINVAL;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		val &= ZX_INPUT_ENABLE;
		val = !!val;
		if (val == 0)
			return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		val &= ZX_DS_MASK;
		val = val >> ZX_DS_SHIFT;
		break;
	case PIN_CONFIG_SLEW_RATE:
		val &= ZX_SLEW;
		val = !!val;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, val);

	return 0;
}

static int zx_pin_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
			     unsigned long *configs, unsigned int num_configs)
{
	struct zx_pinctrl *zpctl = pinctrl_dev_get_drvdata(pctldev);
	struct zx_pinctrl_soc_info *info = zpctl->info;
	const struct pinctrl_pin_desc *pindesc = info->pins + pin;
	struct zx_pin_data *data = pindesc->drv_data;
	enum pin_config_param param;
	u32 val, arg;
	int i;

	/* Skip reserved pin */
	if (!data)
		return -EINVAL;

	val = readl(zpctl->aux_base + data->coffset);

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_DOWN:
			val |= ZX_PULL_DOWN << data->cbitpos;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			val |= ZX_PULL_UP << data->cbitpos;
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			val |= ZX_INPUT_ENABLE << data->cbitpos;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			val &= ~(ZX_DS_MASK << data->cbitpos);
			val |= ZX_DS_VALUE(arg) << data->cbitpos;
			break;
		case PIN_CONFIG_SLEW_RATE:
			if (arg)
				val |= ZX_SLEW << data->cbitpos;
			else
				val &= ~ZX_SLEW << data->cbitpos;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	writel(val, zpctl->aux_base + data->coffset);
	return 0;
}

static const struct pinconf_ops zx_pinconf_ops = {
	.pin_config_set = zx_pin_config_set,
	.pin_config_get = zx_pin_config_get,
	.is_generic = true,
};

static int zx_pinctrl_build_state(struct platform_device *pdev)
{
	struct zx_pinctrl *zpctl = platform_get_drvdata(pdev);
	struct zx_pinctrl_soc_info *info = zpctl->info;
	struct pinctrl_dev *pctldev = zpctl->pctldev;
	struct function_desc *functions;
	int nfunctions;
	struct group_desc *groups;
	int ngroups;
	int i;

	/* Every single pin composes a group */
	ngroups = info->npins;
	groups = devm_kcalloc(&pdev->dev, ngroups, sizeof(*groups),
			      GFP_KERNEL);
	if (!groups)
		return -ENOMEM;

	for (i = 0; i < ngroups; i++) {
		const struct pinctrl_pin_desc *pindesc = info->pins + i;
		struct group_desc *group = groups + i;

		group->name = pindesc->name;
		group->pins = (int *) &pindesc->number;
		group->num_pins = 1;
		radix_tree_insert(&pctldev->pin_group_tree, i, group);
	}

	pctldev->num_groups = ngroups;

	/* Build function list from pin mux functions */
	functions = kcalloc(info->npins, sizeof(*functions), GFP_KERNEL);
	if (!functions)
		return -ENOMEM;

	nfunctions = 0;
	for (i = 0; i < info->npins; i++) {
		const struct pinctrl_pin_desc *pindesc = info->pins + i;
		struct zx_pin_data *data = pindesc->drv_data;
		struct zx_mux_desc *mux;

		/* Reserved pins do not have a drv_data at all */
		if (!data)
			continue;

		/* Loop over all muxes for the pin */
		mux = data->muxes;
		while (mux->name) {
			struct function_desc *func = functions;

			/* Search function list for given mux */
			while (func->name) {
				if (strcmp(mux->name, func->name) == 0) {
					/* Function exists */
					func->num_group_names++;
					break;
				}
				func++;
			}

			if (!func->name) {
				/* New function */
				func->name = mux->name;
				func->num_group_names = 1;
				radix_tree_insert(&pctldev->pin_function_tree,
						  nfunctions++, func);
			}

			mux++;
		}
	}

	pctldev->num_functions = nfunctions;
	functions = krealloc(functions, nfunctions * sizeof(*functions),
			     GFP_KERNEL);

	/* Find pin groups for every single function */
	for (i = 0; i < info->npins; i++) {
		const struct pinctrl_pin_desc *pindesc = info->pins + i;
		struct zx_pin_data *data = pindesc->drv_data;
		struct zx_mux_desc *mux;

		if (!data)
			continue;

		mux = data->muxes;
		while (mux->name) {
			struct function_desc *func;
			const char **group;
			int j;

			/* Find function for given mux */
			for (j = 0; j < nfunctions; j++)
				if (strcmp(functions[j].name, mux->name) == 0)
					break;

			func = functions + j;
			if (!func->group_names) {
				func->group_names = devm_kcalloc(&pdev->dev,
						func->num_group_names,
						sizeof(*func->group_names),
						GFP_KERNEL);
				if (!func->group_names) {
					kfree(functions);
					return -ENOMEM;
				}
			}

			group = func->group_names;
			while (*group)
				group++;
			*group = pindesc->name;

			mux++;
		}
	}

	return 0;
}

int zx_pinctrl_init(struct platform_device *pdev,
		    struct zx_pinctrl_soc_info *info)
{
	struct pinctrl_desc *pctldesc;
	struct zx_pinctrl *zpctl;
	struct device_node *np;
	struct resource *res;
	int ret;

	zpctl = devm_kzalloc(&pdev->dev, sizeof(*zpctl), GFP_KERNEL);
	if (!zpctl)
		return -ENOMEM;

	spin_lock_init(&zpctl->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	zpctl->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(zpctl->base))
		return PTR_ERR(zpctl->base);

	np = of_parse_phandle(pdev->dev.of_node, "zte,auxiliary-controller", 0);
	if (!np) {
		dev_err(&pdev->dev, "failed to find auxiliary controller\n");
		return -ENODEV;
	}

	zpctl->aux_base = of_iomap(np, 0);
	if (!zpctl->aux_base)
		return -ENOMEM;

	zpctl->dev = &pdev->dev;
	zpctl->info = info;

	pctldesc = devm_kzalloc(&pdev->dev, sizeof(*pctldesc), GFP_KERNEL);
	if (!pctldesc)
		return -ENOMEM;

	pctldesc->name = dev_name(&pdev->dev);
	pctldesc->owner = THIS_MODULE;
	pctldesc->pins = info->pins;
	pctldesc->npins = info->npins;
	pctldesc->pctlops = &zx_pinctrl_ops;
	pctldesc->pmxops = &zx_pinmux_ops;
	pctldesc->confops = &zx_pinconf_ops;

	zpctl->pctldev = devm_pinctrl_register(&pdev->dev, pctldesc, zpctl);
	if (IS_ERR(zpctl->pctldev)) {
		ret = PTR_ERR(zpctl->pctldev);
		dev_err(&pdev->dev, "failed to register pinctrl: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, zpctl);

	ret = zx_pinctrl_build_state(pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to build state: %d\n", ret);
		return ret;
	}

	dev_info(&pdev->dev, "initialized pinctrl driver\n");
	return 0;
}
