// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic Pin Control Driver for Board-Level Mux Chips
 * Copyright 2026 NXP
 */

#include <linux/cleanup.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/mux/consumer.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>

#include "core.h"
#include "pinmux.h"
#include "pinctrl-utils.h"

struct mux_pin_function {
	struct mux_state *mux_state;
};

struct mux_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;

	/* mutex protect [pinctrl|pinmux]_generic functions */
	struct mutex lock;
};

static int
mux_pinmux_dt_node_to_map(struct pinctrl_dev *pctldev,
			  struct device_node *np_config,
			  struct pinctrl_map **maps, unsigned int *num_maps)
{
	unsigned int num_reserved_maps = 0;
	int ret;

	*maps = NULL;
	*num_maps = 0;

	if (pinctrl_get_group_selector(pctldev, np_config->name) < 0)
		return -ENODEV;

	ret = pinctrl_utils_reserve_map(pctldev, maps, &num_reserved_maps,
					num_maps, 1);
	if (ret)
		return ret;

	ret = pinctrl_utils_add_map_mux(pctldev, maps, &num_reserved_maps,
					num_maps, np_config->name,
					np_config->name);
	if (ret < 0) {
		pinctrl_utils_free_map(pctldev, *maps, *num_maps);
		*maps = NULL;
		*num_maps = 0;
		return ret;
	}

	return 0;
}

static int mux_pinctrl_probe_dt(struct pinctrl_dev *pctldev,
				struct device_node *np)
{
	struct device *dev = pctldev->dev;

	for_each_available_child_of_node_scoped(np, grp) {
		struct mux_pin_function *function;
		const char **group_names;
		int ret;

		function = devm_kzalloc(dev, sizeof(*function), GFP_KERNEL);
		if (!function)
			return -ENOMEM;

		group_names = devm_kcalloc(dev, 1, sizeof(*group_names), GFP_KERNEL);
		if (!group_names)
			return -ENOMEM;

		group_names[0] = grp->name;

		function->mux_state = devm_mux_state_get_from_np(dev, NULL, grp);
		if (IS_ERR(function->mux_state))
			return dev_err_probe(dev, PTR_ERR(function->mux_state),
					     "failed to get mux-state for %pOFn\n",
					     grp);

		ret = pinctrl_generic_add_group(pctldev, grp->name, NULL, 0, NULL);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "failed to add group %pOFn\n", grp);

		ret = pinmux_generic_add_function(pctldev, grp->name, group_names,
						  1, function);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "failed to add function %pOFn\n", grp);
	}

	return 0;
}

static const struct pinctrl_ops mux_pinctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = mux_pinmux_dt_node_to_map,
	.dt_free_map = pinctrl_utils_free_map,
};

static int mux_pinmux_set_mux(struct pinctrl_dev *pctldev,
			      unsigned int func_selector,
			      unsigned int group_selector)
{
	struct mux_pinctrl *mpctl = pinctrl_dev_get_drvdata(pctldev);
	const struct function_desc *function;
	struct mux_pin_function *func;
	int ret;

	guard(mutex)(&mpctl->lock);

	function = pinmux_generic_get_function(pctldev, func_selector);
	func = function->data;

	ret = mux_state_try_select(func->mux_state);
	if (ret)
		return ret;

	return 0;
}

static void mux_pinmux_release_mux(struct pinctrl_dev *pctldev,
				   unsigned int func_selector,
				   unsigned int group_selector)
{
	struct mux_pinctrl *mpctl = pinctrl_dev_get_drvdata(pctldev);
	const struct function_desc *function;
	struct mux_pin_function *func;

	guard(mutex)(&mpctl->lock);

	function = pinmux_generic_get_function(pctldev, func_selector);
	func = function->data;

	mux_state_deselect(func->mux_state);
}

static const struct pinmux_ops mux_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = mux_pinmux_set_mux,
	.release_mux = mux_pinmux_release_mux,
};

static int mux_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mux_pinctrl *mpctl;
	struct pinctrl_desc *pctl_desc;
	int ret;

	mpctl = devm_kzalloc(dev, sizeof(*mpctl), GFP_KERNEL);
	if (!mpctl)
		return -ENOMEM;

	mpctl->dev = dev;

	platform_set_drvdata(pdev, mpctl);

	pctl_desc = devm_kzalloc(dev, sizeof(*pctl_desc), GFP_KERNEL);
	if (!pctl_desc)
		return -ENOMEM;

	ret = devm_mutex_init(dev, &mpctl->lock);
	if (ret)
		return ret;

	pctl_desc->name = dev_name(dev);
	pctl_desc->owner = THIS_MODULE;
	pctl_desc->pctlops = &mux_pinctrl_ops;
	pctl_desc->pmxops = &mux_pinmux_ops;

	ret = devm_pinctrl_register_and_init(dev, pctl_desc, mpctl,
					     &mpctl->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register pinctrl.\n");

	ret = mux_pinctrl_probe_dt(mpctl->pctl, dev->of_node);
	if (ret)
		return ret;

	ret = pinctrl_enable(mpctl->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable pinctrl.\n");

	return 0;
}

static const struct of_device_id mux_pinctrl_of_match[] = {
	{ .compatible = "pinctrl-multiplexer" },
	{ }
};
MODULE_DEVICE_TABLE(of, mux_pinctrl_of_match);

static struct platform_driver mux_pinctrl_driver = {
	.driver = {
		.name = "generic-pinctrl-mux",
		.of_match_table = mux_pinctrl_of_match,
	},
	.probe = mux_pinctrl_probe,
};
module_platform_driver(mux_pinctrl_driver);

MODULE_AUTHOR("Frank Li <Frank.Li@nxp.com>");
MODULE_DESCRIPTION("Generic Pin Control Driver for Board-Level Mux Chips");
MODULE_LICENSE("GPL");
