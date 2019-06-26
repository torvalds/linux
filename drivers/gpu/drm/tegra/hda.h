// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2019 NVIDIA Corporation
 */

#ifndef DRM_TEGRA_HDA_H
#define DRM_TEGRA_HDA_H 1

#include <linux/types.h>

struct tegra_hda_format {
	unsigned int sample_rate;
	unsigned int channels;
	unsigned int bits;
	bool pcm;
};

void tegra_hda_parse_format(unsigned int format, struct tegra_hda_format *fmt);

#endif
