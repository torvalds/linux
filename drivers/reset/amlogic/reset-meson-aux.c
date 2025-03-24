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

#include "reset-meson.h"

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

MODULE_DESCRIPTION("Amlogic Meson Reset Auxiliary driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS("MESON_RESET");
