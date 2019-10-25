/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */

#ifndef _KOMEDA_FORMAT_CAPS_H_
#define _KOMEDA_FORMAT_CAPS_H_

#include <linux/types.h>
#include <uapi/drm/drm_fourcc.h>
#include <drm/drm_fourcc.h>

#define AFBC(x)		DRM_FORMAT_MOD_ARM_AFBC(x)

/* afbc layerout */
#define AFBC_16x16(x)	AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | (x))
#define AFBC_32x8(x)	AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 | (x))

/* afbc features */
#define _YTR		AFBC_FORMAT_MOD_YTR
#define _SPLIT		AFBC_FORMAT_MOD_SPLIT
#define _SPARSE		AFBC_FORMAT_MOD_SPARSE
#define _CBR		AFBC_FORMAT_MOD_CBR
#define _TILED		AFBC_FORMAT_MOD_TILED
#define _SC		AFBC_FORMAT_MOD_SC

/* layer_type */
#define KOMEDA_FMT_RICH_LAYER		BIT(0)
#define KOMEDA_FMT_SIMPLE_LAYER		BIT(1)
#define KOMEDA_FMT_WB_LAYER		BIT(2)

#define AFBC_TH_LAYOUT_ALIGNMENT	8
#define AFBC_HEADER_SIZE		16
#define AFBC_SUPERBLK_ALIGNMENT		128
#define AFBC_SUPERBLK_PIXELS		256
#define AFBC_BODY_START_ALIGNMENT	1024
#define AFBC_TH_BODY_START_ALIGNMENT	4096

/**
 * struct komeda_format_caps
 *
 * komeda_format_caps is for describing ARM display specific features and
 * limitations for a specific format, and format_caps will be linked into
 * &komeda_framebuffer like a extension of &drm_format_info.
 *
 * NOTE: one fourcc may has two different format_caps items for fourcc and
 * fourcc+modifier
 *
 * @hw_id: hw format id, hw specific value.
 * @fourcc: drm fourcc format.
 * @supported_layer_types: indicate which layer supports this format
 * @supported_rots: allowed rotations for this format
 * @supported_afbc_layouts: supported afbc layerout
 * @supported_afbc_features: supported afbc features
 */
struct komeda_format_caps {
	u32 hw_id;
	u32 fourcc;
	u32 supported_layer_types;
	u32 supported_rots;
	u32 supported_afbc_layouts;
	u64 supported_afbc_features;
};

/**
 * struct komeda_format_caps_table - format_caps mananger
 *
 * @n_formats: the size of format_caps list.
 * @format_caps: format_caps list.
 * @format_mod_supported: Optional. Some HW may have special requirements or
 * limitations which can not be described by format_caps, this func supply HW
 * the ability to do the further HW specific check.
 */
struct komeda_format_caps_table {
	u32 n_formats;
	const struct komeda_format_caps *format_caps;
	bool (*format_mod_supported)(const struct komeda_format_caps *caps,
				     u32 layer_type, u64 modifier, u32 rot);
};

extern u64 komeda_supported_modifiers[];

static inline const char *komeda_get_format_name(u32 fourcc, u64 modifier)
{
	struct drm_format_name_buf buf;
	static char name[64];

	snprintf(name, sizeof(name), "%s with modifier: 0x%llx.",
		 drm_get_format_name(fourcc, &buf), modifier);

	return name;
}

const struct komeda_format_caps *
komeda_get_format_caps(struct komeda_format_caps_table *table,
		       u32 fourcc, u64 modifier);

u32 komeda_get_afbc_format_bpp(const struct drm_format_info *info,
			       u64 modifier);

u32 *komeda_get_layer_fourcc_list(struct komeda_format_caps_table *table,
				  u32 layer_type, u32 *n_fmts);

void komeda_put_fourcc_list(u32 *fourcc_list);

bool komeda_format_mod_supported(struct komeda_format_caps_table *table,
				 u32 layer_type, u32 fourcc, u64 modifier,
				 u32 rot);

#endif
