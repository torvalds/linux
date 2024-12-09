// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Amlogic Meson Reset Auxiliary driver
 *
 * Copyright (c) 2024 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/auxiliary_bus.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>

#include "reset-meson.h"
#include <soc/amlogic/reset-meson-aux.h>

static DEFINE_IDA(meson_rst_aux_ida);

static const struct meson_reset_param meson_a1_audio_param = {
	.reset_ops	= &meson_reset_toggle_ops,
	.reset_num	= 32,
	.level_offset	= 0x28,
};

static const struct meson_reset_param meson_a1_audio_vad_param = {
	.reset_ops	= &meson_reset_toggle_ops,
	.reset_num	= 6,
	.level_offset	= 0x8,
};

static const struct meson_reset_param meson_g12a_audio_param = {
	.reset_ops	= &meson_reset_toggle_ops,
	.reset_num	= 26,
	.level_offset	= 0x24,
};

static const struct meson_reset_param meson_sm1_audio_param = {
	.reset_ops	= &meson_reset_toggle_ops,
	.reset_num	= 39,
	.level_offset	= 0x28,
};

static const struct auxiliary_device_id meson_reset_aux_ids[] = {
	{
		.name = "a1-audio-clkc.rst-a1",
		.driver_data = (kernel_ulong_t)&meson_a1_audio_param,
	}, {
		.name = "a1-audio-clkc.rst-a1-vad",
		.driver_data = (kernel_ulong_t)&meson_a1_audio_vad_param,
	}, {
		.name = "axg-audio-clkc.rst-g12a",
		.driver_data = (kernel_ulong_t)&meson_g12a_audio_param,
	}, {
		.name = "axg-audio-clkc.rst-sm1",
		.driver_data = (kernel_ulong_t)&meson_sm1_audio_param,
	}, {}
};
MODULE_DEVICE_TABLE(auxiliary, meson_reset_aux_ids);

static int meson_reset_aux_probe(struct auxiliary_device *adev,
				 const struct auxiliary_device_id *id)
{
	const struct meson_reset_param *param =
		(const struct meson_reset_param *)(id->driver_data);
	struct regmap *map;

	map = dev_get_regmap(adev->dev.parent, NULL);
	if (!map)
		return -EINVAL;

	return meson_reset_controller_register(&adev->dev, map, param);
}

static struct auxiliary_driver meson_reset_aux_driver = {
	.probe		= meson_reset_aux_probe,
	.id_table	= meson_reset_aux_ids,
};
module_auxiliary_driver(meson_reset_aux_driver);

static void meson_rst_aux_release(struct device *dev)
{
	struct auxiliary_device *adev = to_auxiliary_dev(dev);

	ida_free(&meson_rst_aux_ida, adev->id);
	kfree(adev);
}

static void meson_rst_aux_unregister_adev(void *_adev)
{
	struct auxiliary_device *adev = _adev;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

int devm_meson_rst_aux_register(struct device *dev,
				const char *adev_name)
{
	struct auxiliary_device *adev;
	int ret;

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;

	ret = ida_alloc(&meson_rst_aux_ida, GFP_KERNEL);
	if (ret < 0)
		goto adev_free;

	adev->id = ret;
	adev->name = adev_name;
	adev->dev.parent = dev;
	adev->dev.release = meson_rst_aux_release;
	device_set_of_node_from_dev(&adev->dev, dev);

	ret = auxiliary_device_init(adev);
	if (ret)
		goto ida_free;

	ret = __auxiliary_device_add(adev, dev->driver->name);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	return devm_add_action_or_reset(dev, meson_rst_aux_unregister_adev,
					adev);

ida_free:
	ida_free(&meson_rst_aux_ida, adev->id);
adev_free:
	kfree(adev);
	return ret;
}
EXPORT_SYMBOL_GPL(devm_meson_rst_aux_register);

MODULE_DESCRIPTION("Amlogic Meson Reset Auxiliary driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(MESON_RESET);
