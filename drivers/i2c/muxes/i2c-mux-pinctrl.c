/*
 * I2C multiplexer using pinctrl API
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include "../../pinctrl/core.h"

struct i2c_mux_pinctrl {
	struct pinctrl *pinctrl;
	struct pinctrl_state *states[];
};

static int i2c_mux_pinctrl_select(struct i2c_mux_core *muxc, u32 chan)
{
	struct i2c_mux_pinctrl *mux = i2c_mux_priv(muxc);

	return pinctrl_select_state(mux->pinctrl, mux->states[chan]);
}

static int i2c_mux_pinctrl_deselect(struct i2c_mux_core *muxc, u32 chan)
{
	return i2c_mux_pinctrl_select(muxc, muxc->num_adapters);
}

static struct i2c_adapter *i2c_mux_pinctrl_root_adapter(
	struct pinctrl_state *state)
{
	struct i2c_adapter *root = NULL;
	struct pinctrl_setting *setting;
	struct i2c_adapter *pin_root;

	list_for_each_entry(setting, &state->settings, node) {
		pin_root = i2c_root_adapter(setting->pctldev->dev);
		if (!pin_root)
			return NULL;
		if (!root)
			root = pin_root;
		else if (root != pin_root)
			return NULL;
	}

	return root;
}

static struct i2c_adapter *i2c_mux_pinctrl_parent_adapter(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *parent_np;
	struct i2c_adapter *parent;

	parent_np = of_parse_phandle(np, "i2c-parent", 0);
	if (!parent_np) {
		dev_err(dev, "Cannot parse i2c-parent\n");
		return ERR_PTR(-ENODEV);
	}
	parent = of_find_i2c_adapter_by_node(parent_np);
	of_node_put(parent_np);
	if (!parent)
		return ERR_PTR(-EPROBE_DEFER);

	return parent;
}

static int i2c_mux_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct i2c_mux_core *muxc;
	struct i2c_mux_pinctrl *mux;
	struct i2c_adapter *parent;
	struct i2c_adapter *root;
	int num_names, i, ret;
	const char *name;

	num_names = of_property_count_strings(np, "pinctrl-names");
	if (num_names < 0) {
		dev_err(dev, "Cannot parse pinctrl-names: %d\n",
			num_names);
		return num_names;
	}

	parent = i2c_mux_pinctrl_parent_adapter(dev);
	if (IS_ERR(parent))
		return PTR_ERR(parent);

	muxc = i2c_mux_alloc(parent, dev, num_names,
			     struct_size(mux, states, num_names),
			     0, i2c_mux_pinctrl_select, NULL);
	if (!muxc) {
		ret = -ENOMEM;
		goto err_put_parent;
	}
	mux = i2c_mux_priv(muxc);

	platform_set_drvdata(pdev, muxc);

	mux->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(mux->pinctrl)) {
		ret = PTR_ERR(mux->pinctrl);
		dev_err(dev, "Cannot get pinctrl: %d\n", ret);
		goto err_put_parent;
	}

	for (i = 0; i < num_names; i++) {
		ret = of_property_read_string_index(np, "pinctrl-names", i,
						    &name);
		if (ret < 0) {
			dev_err(dev, "Cannot parse pinctrl-names: %d\n", ret);
			goto err_put_parent;
		}

		mux->states[i] = pinctrl_lookup_state(mux->pinctrl, name);
		if (IS_ERR(mux->states[i])) {
			ret = PTR_ERR(mux->states[i]);
			dev_err(dev, "Cannot look up pinctrl state %s: %d\n",
				name, ret);
			goto err_put_parent;
		}

		if (strcmp(name, "idle"))
			continue;

		if (i != num_names - 1) {
			dev_err(dev, "idle state must be last\n");
			ret = -EINVAL;
			goto err_put_parent;
		}
		muxc->deselect = i2c_mux_pinctrl_deselect;
	}

	root = i2c_root_adapter(&muxc->parent->dev);

	muxc->mux_locked = true;
	for (i = 0; i < num_names; i++) {
		if (root != i2c_mux_pinctrl_root_adapter(mux->states[i])) {
			muxc->mux_locked = false;
			break;
		}
	}
	if (muxc->mux_locked)
		dev_info(dev, "mux-locked i2c mux\n");

	/* Do not add any adapter for the idle state (if it's there at all). */
	for (i = 0; i < num_names - !!muxc->deselect; i++) {
		ret = i2c_mux_add_adapter(muxc, 0, i, 0);
		if (ret)
			goto err_del_adapter;
	}

	return 0;

err_del_adapter:
	i2c_mux_del_adapters(muxc);
err_put_parent:
	i2c_put_adapter(parent);

	return ret;
}

static int i2c_mux_pinctrl_remove(struct platform_device *pdev)
{
	struct i2c_mux_core *muxc = platform_get_drvdata(pdev);

	i2c_mux_del_adapters(muxc);
	i2c_put_adapter(muxc->parent);

	return 0;
}

static const struct of_device_id i2c_mux_pinctrl_of_match[] = {
	{ .compatible = "i2c-mux-pinctrl", },
	{},
};
MODULE_DEVICE_TABLE(of, i2c_mux_pinctrl_of_match);

static struct platform_driver i2c_mux_pinctrl_driver = {
	.driver	= {
		.name	= "i2c-mux-pinctrl",
		.of_match_table = of_match_ptr(i2c_mux_pinctrl_of_match),
	},
	.probe	= i2c_mux_pinctrl_probe,
	.remove	= i2c_mux_pinctrl_remove,
};
module_platform_driver(i2c_mux_pinctrl_driver);

MODULE_DESCRIPTION("pinctrl-based I2C multiplexer driver");
MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:i2c-mux-pinctrl");
