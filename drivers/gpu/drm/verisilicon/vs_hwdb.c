// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 */

#include <linux/errno.h>

#include <drm/drm_fourcc.h>

#include "vs_dc_top_regs.h"
#include "vs_hwdb.h"

static const u32 vs_primary_formats_array_no_yuv444[] = {
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_XBGR4444,
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_BGRX4444,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,
	/* TODO: non-RGB formats */
};

static const u32 vs_primary_formats_array_with_yuv444[] = {
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_XBGR4444,
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_BGRX4444,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,
	/* TODO: non-RGB formats */
};

static const struct vs_formats vs_formats_no_yuv444 = {
	.primary_array = vs_primary_formats_array_no_yuv444,
	.primary_num = ARRAY_SIZE(vs_primary_formats_array_no_yuv444)
};

static const struct vs_formats vs_formats_with_yuv444 = {
	.primary_array = vs_primary_formats_array_with_yuv444,
	.primary_num = ARRAY_SIZE(vs_primary_formats_array_with_yuv444)
};

static struct vs_chip_identity vs_chip_identities[] = {
	{
		.model = 0x8200,
		.revision = 0x5720,
		.customer_id = ~0U,

		.display_count = 2,
		.max_cursor_size = 64,
		.formats = &vs_formats_no_yuv444,
	},
	{
		.model = 0x8200,
		.revision = 0x5721,
		.customer_id = 0x30B,

		.display_count = 2,
		.max_cursor_size = 64,
		.formats = &vs_formats_no_yuv444,
	},
	{
		.model = 0x8200,
		.revision = 0x5720,
		.customer_id = 0x310,

		.display_count = 2,
		.max_cursor_size = 64,
		.formats = &vs_formats_with_yuv444,
	},
	{
		.model = 0x8200,
		.revision = 0x5720,
		.customer_id = 0x311,

		.display_count = 2,
		.max_cursor_size = 64,
		.formats = &vs_formats_no_yuv444,
	},
};

int vs_fill_chip_identity(struct regmap *regs,
			  struct vs_chip_identity *ident)
{
	u32 model;
	u32 revision;
	u32 customer_id;
	int i;

	regmap_read(regs, VSDC_TOP_CHIP_MODEL, &model);
	regmap_read(regs, VSDC_TOP_CHIP_REV, &revision);
	regmap_read(regs, VSDC_TOP_CHIP_CUSTOMER_ID, &customer_id);

	for (i = 0; i < ARRAY_SIZE(vs_chip_identities); i++) {
		if (vs_chip_identities[i].model == model &&
		    vs_chip_identities[i].revision == revision &&
		    (vs_chip_identities[i].customer_id == customer_id ||
		     vs_chip_identities[i].customer_id == ~0U)) {
			memcpy(ident, &vs_chip_identities[i], sizeof(*ident));
			ident->customer_id = customer_id;
			return 0;
		}
	}

	return -EINVAL;
}
