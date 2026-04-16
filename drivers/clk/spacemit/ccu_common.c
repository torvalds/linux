// SPDX-License-Identifier: GPL-2.0-only

#include <linux/clk-provider.h>
#include <linux/device/devres.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <soc/spacemit/ccu.h>

#include "ccu_common.h"

static DEFINE_IDA(auxiliary_ids);
static int spacemit_ccu_register(struct device *dev,
				 struct regmap *regmap,
				 struct regmap *lock_regmap,
				 const struct spacemit_ccu_data *data)
{
	struct clk_hw_onecell_data *clk_data;
	int i, ret;

	/* Nothing to do if the CCU does not implement any clocks */
	if (!data->hws)
		return 0;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, data->num),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = data->num;

	for (i = 0; i < data->num; i++) {
		struct clk_hw *hw = data->hws[i];
		struct ccu_common *common;
		const char *name;

		if (!hw) {
			clk_data->hws[i] = ERR_PTR(-ENOENT);
			continue;
		}

		name = hw->init->name;

		common = hw_to_ccu_common(hw);
		common->regmap		= regmap;
		common->lock_regmap	= lock_regmap;

		ret = devm_clk_hw_register(dev, hw);
		if (ret) {
			dev_err(dev, "Cannot register clock %d - %s\n",
				i, name);
			return ret;
		}

		clk_data->hws[i] = hw;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_data);
	if (ret)
		dev_err(dev, "failed to add clock hardware provider (%d)\n", ret);

	return ret;
}

static void spacemit_cadev_release(struct device *dev)
{
	struct auxiliary_device *adev = to_auxiliary_dev(dev);

	ida_free(&auxiliary_ids, adev->id);
	kfree(to_spacemit_ccu_adev(adev));
}

static void spacemit_adev_unregister(void *data)
{
	struct auxiliary_device *adev = data;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

static int spacemit_ccu_reset_register(struct device *dev,
				       struct regmap *regmap,
				       const char *reset_name)
{
	struct spacemit_ccu_adev *cadev;
	struct auxiliary_device *adev;
	int ret;

	/* Nothing to do if the CCU does not implement a reset controller */
	if (!reset_name)
		return 0;

	cadev = kzalloc_obj(*cadev);
	if (!cadev)
		return -ENOMEM;

	cadev->regmap = regmap;

	adev = &cadev->adev;
	adev->name = reset_name;
	adev->dev.parent = dev;
	adev->dev.release = spacemit_cadev_release;
	adev->dev.of_node = dev->of_node;
	ret = ida_alloc(&auxiliary_ids, GFP_KERNEL);
	if (ret < 0)
		goto err_free_cadev;
	adev->id = ret;

	ret = auxiliary_device_init(adev);
	if (ret)
		goto err_free_aux_id;

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	return devm_add_action_or_reset(dev, spacemit_adev_unregister, adev);

err_free_aux_id:
	ida_free(&auxiliary_ids, adev->id);
err_free_cadev:
	kfree(cadev);

	return ret;
}

int spacemit_ccu_probe(struct platform_device *pdev, const char *compat)
{
	struct regmap *base_regmap, *lock_regmap = NULL;
	const struct spacemit_ccu_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	base_regmap = device_node_to_regmap(dev->of_node);
	if (IS_ERR(base_regmap))
		return dev_err_probe(dev, PTR_ERR(base_regmap),
				     "failed to get regmap\n");

	/*
	 * The lock status of PLLs locate in MPMU region, while PLLs themselves
	 * are in APBS region. Reference to MPMU syscon is required to check PLL
	 * status.
	 */
	if (compat && of_device_is_compatible(dev->of_node, compat)) {
		struct device_node *mpmu = of_parse_phandle(dev->of_node,
							    "spacemit,mpmu", 0);
		if (!mpmu)
			return dev_err_probe(dev, -ENODEV,
					     "Cannot parse MPMU region\n");

		lock_regmap = device_node_to_regmap(mpmu);
		of_node_put(mpmu);

		if (IS_ERR(lock_regmap))
			return dev_err_probe(dev, PTR_ERR(lock_regmap),
					     "failed to get lock regmap\n");
	}

	data = of_device_get_match_data(dev);

	ret = spacemit_ccu_register(dev, base_regmap, lock_regmap, data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register clocks\n");

	ret = spacemit_ccu_reset_register(dev, base_regmap, data->reset_name);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register resets\n");

	return 0;
}
EXPORT_SYMBOL_NS_GPL(spacemit_ccu_probe, "CLK_SPACEMIT");

MODULE_DESCRIPTION("SpacemiT CCU common clock driver");
MODULE_LICENSE("GPL");
