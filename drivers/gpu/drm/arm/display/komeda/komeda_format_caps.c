// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */

#include <linux/slab.h>
#include "komeda_format_caps.h"
#include "malidp_utils.h"

const struct komeda_format_caps *
komeda_get_format_caps(struct komeda_format_caps_table *table,
		       u32 fourcc, u64 modifier)
{
	const struct komeda_format_caps *caps;
	u64 afbc_features = modifier & ~(AFBC_FORMAT_MOD_BLOCK_SIZE_MASK);
	u32 afbc_layout = modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK;
	int id;

	for (id = 0; id < table->n_formats; id++) {
		caps = &table->format_caps[id];

		if (fourcc != caps->fourcc)
			continue;

		if ((modifier == 0ULL) && (caps->supported_afbc_layouts == 0))
			return caps;

		if (has_bits(afbc_features, caps->supported_afbc_features) &&
		    has_bit(afbc_layout, caps->supported_afbc_layouts))
			return caps;
	}

	return NULL;
}

u32 komeda_get_afbc_format_bpp(const struct drm_format_info *info, u64 modifier)
{
	u32 bpp;

	switch (info->format) {
	case DRM_FORMAT_YUV420_8BIT:
		bpp = 12;
		break;
	case DRM_FORMAT_YUV420_10BIT:
		bpp = 15;
		break;
	default:
		bpp = info->cpp[0] * 8;
		break;
	}

	return bpp;
}

/* Two assumptions
 * 1. RGB always has YTR
 * 2. Tiled RGB always has SC
 */
u64 komeda_supported_modifiers[] = {
	/* AFBC_16x16 + features: YUV+RGB both */
	AFBC_16x16(0),
	/* SPARSE */
	AFBC_16x16(_SPARSE),
	/* YTR + (SPARSE) */
	AFBC_16x16(_YTR | _SPARSE),
	AFBC_16x16(_YTR),
	/* SPLIT + SPARSE + YTR RGB only */
	/* split mode is only allowed for sparse mode */
	AFBC_16x16(_SPLIT | _SPARSE | _YTR),
	/* TILED + (SPARSE) */
	/* TILED YUV format only */
	AFBC_16x16(_TILED | _SPARSE),
	AFBC_16x16(_TILED),
	/* TILED + SC + (SPLIT+SPARSE | SPARSE) + (YTR) */
	AFBC_16x16(_TILED | _SC | _SPLIT | _SPARSE | _YTR),
	AFBC_16x16(_TILED | _SC | _SPARSE | _YTR),
	AFBC_16x16(_TILED | _SC | _YTR),
	/* AFBC_32x8 + features: which are RGB formats only */
	/* YTR + (SPARSE) */
	AFBC_32x8(_YTR | _SPARSE),
	AFBC_32x8(_YTR),
	/* SPLIT + SPARSE + (YTR) */
	/* split mode is only allowed for sparse mode */
	AFBC_32x8(_SPLIT | _SPARSE | _YTR),
	/* TILED + SC + (SPLIT+SPARSE | SPARSE) + YTR */
	AFBC_32x8(_TILED | _SC | _SPLIT | _SPARSE | _YTR),
	AFBC_32x8(_TILED | _SC | _SPARSE | _YTR),
	AFBC_32x8(_TILED | _SC | _YTR),
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

bool komeda_format_mod_supported(struct komeda_format_caps_table *table,
				 u32 layer_type, u32 fourcc, u64 modifier,
				 u32 rot)
{
	const struct komeda_format_caps *caps;

	caps = komeda_get_format_caps(table, fourcc, modifier);
	if (!caps)
		return false;

	if (!(caps->supported_layer_types & layer_type))
		return false;

	if (table->format_mod_supported)
		return table->format_mod_supported(caps, layer_type, modifier,
						   rot);

	return true;
}

u32 *komeda_get_layer_fourcc_list(struct komeda_format_caps_table *table,
				  u32 layer_type, u32 *n_fmts)
{
	const struct komeda_format_caps *cap;
	u32 *fmts;
	int i, j, n = 0;

	fmts = kcalloc(table->n_formats, sizeof(u32), GFP_KERNEL);
	if (!fmts)
		return NULL;

	for (i = 0; i < table->n_formats; i++) {
		cap = &table->format_caps[i];
		if (!(layer_type & cap->supported_layer_types) ||
		    (cap->fourcc == 0))
			continue;

		/* one fourcc may has two caps items in table (afbc/none-afbc),
		 * so check the existing list to avoid adding a duplicated one.
		 */
		for (j = n - 1; j >= 0; j--)
			if (fmts[j] == cap->fourcc)
				break;

		if (j < 0)
			fmts[n++] = cap->fourcc;
	}

	if (n_fmts)
		*n_fmts = n;

	return fmts;
}

void komeda_put_fourcc_list(u32 *fourcc_list)
{
	kfree(fourcc_list);
}
