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

struct meson_reset_adev {
	struct auxiliary_device adev;
	struct regmap *map;
};

#define to_meson_reset_adev(_adev) \
	container_of((_adev), struct meson_reset_adev, adev)

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
	struct meson_reset_adev *raux =
		to_meson_reset_adev(adev);

	return meson_reset_controller_register(&adev->dev, raux->map, param);
}

static struct auxiliary_driver meson_reset_aux_driver = {
	.probe		= meson_reset_aux_probe,
	.id_table	= meson_reset_aux_ids,
};
module_auxiliary_driver(meson_reset_aux_driver);

static void meson_rst_aux_release(struct device *dev)
{
	struct auxiliary_device *adev = to_auxiliary_dev(dev);
	struct meson_reset_adev *raux =
		to_meson_reset_adev(adev);

	ida_free(&meson_rst_aux_ida, adev->id);
	kfree(raux);
}

static void meson_rst_aux_unregister_adev(void *_adev)
{
	struct auxiliary_device *adev = _adev;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

int devm_meson_rst_aux_register(struct device *dev,
				struct regmap *map,
				const char *adev_name)
{
	struct meson_reset_adev *raux;
	struct auxiliary_device *adev;
	int ret;

	raux = kzalloc(sizeof(*raux), GFP_KERNEL);
	if (!raux)
		return -ENOMEM;

	ret = ida_alloc(&meson_rst_aux_ida, GFP_KERNEL);
	if (ret < 0)
		goto raux_free;

	raux->map = map;

	adev = &raux->adev;
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
raux_free:
	kfree(raux);
	return ret;
}
EXPORT_SYMBOL_GPL(devm_meson_rst_aux_register);

MODULE_DESCRIPTION("Amlogic Meson Reset Auxiliary driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(MESON_RESET);
