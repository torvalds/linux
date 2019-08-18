// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include "malidp_io.h"
#include "komeda_dev.h"

static int d71_enum_resources(struct komeda_dev *mdev)
{
	/* TODO add enum resources */
	return -1;
}

#define __HW_ID(__group, __format) \
	((((__group) & 0x7) << 3) | ((__format) & 0x7))

#define RICH		KOMEDA_FMT_RICH_LAYER
#define SIMPLE		KOMEDA_FMT_SIMPLE_LAYER
#define RICH_SIMPLE	(KOMEDA_FMT_RICH_LAYER | KOMEDA_FMT_SIMPLE_LAYER)
#define RICH_WB		(KOMEDA_FMT_RICH_LAYER | KOMEDA_FMT_WB_LAYER)
#define RICH_SIMPLE_WB	(RICH_SIMPLE | KOMEDA_FMT_WB_LAYER)

#define Rot_0		DRM_MODE_ROTATE_0
#define Flip_H_V	(DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y | Rot_0)
#define Rot_ALL_H_V	(DRM_MODE_ROTATE_MASK | Flip_H_V)

#define LYT_NM		BIT(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16)
#define LYT_WB		BIT(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8)
#define LYT_NM_WB	(LYT_NM | LYT_WB)

#define AFB_TH		AFBC(_TILED | _SPARSE)
#define AFB_TH_SC_YTR	AFBC(_TILED | _SC | _SPARSE | _YTR)
#define AFB_TH_SC_YTR_BS AFBC(_TILED | _SC | _SPARSE | _YTR | _SPLIT)

static struct komeda_format_caps d71_format_caps_table[] = {
	/*   HW_ID    |        fourcc        | tile_sz |   layer_types |   rots    | afbc_layouts | afbc_features */
	/* ABGR_2101010*/
	{__HW_ID(0, 0),	DRM_FORMAT_ARGB2101010,	1,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(0, 1),	DRM_FORMAT_ABGR2101010,	1,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(0, 1),	DRM_FORMAT_ABGR2101010,	1,	RICH_SIMPLE,	Rot_ALL_H_V,	LYT_NM_WB, AFB_TH_SC_YTR_BS}, /* afbc */
	{__HW_ID(0, 2),	DRM_FORMAT_RGBA1010102,	1,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(0, 3),	DRM_FORMAT_BGRA1010102,	1,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	/* ABGR_8888*/
	{__HW_ID(1, 0),	DRM_FORMAT_ARGB8888,	1,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(1, 1),	DRM_FORMAT_ABGR8888,	1,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(1, 1),	DRM_FORMAT_ABGR8888,	1,	RICH_SIMPLE,	Rot_ALL_H_V,	LYT_NM_WB, AFB_TH_SC_YTR_BS}, /* afbc */
	{__HW_ID(1, 2),	DRM_FORMAT_RGBA8888,	1,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(1, 3),	DRM_FORMAT_BGRA8888,	1,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	/* XBGB_8888 */
	{__HW_ID(2, 0),	DRM_FORMAT_XRGB8888,	1,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(2, 1),	DRM_FORMAT_XBGR8888,	1,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(2, 2),	DRM_FORMAT_RGBX8888,	1,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(2, 3),	DRM_FORMAT_BGRX8888,	1,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	/* BGR_888 */ /* none-afbc RGB888 doesn't support rotation and flip */
	{__HW_ID(3, 0),	DRM_FORMAT_RGB888,	1,	RICH_SIMPLE_WB,	Rot_0,			0, 0},
	{__HW_ID(3, 1),	DRM_FORMAT_BGR888,	1,	RICH_SIMPLE_WB,	Rot_0,			0, 0},
	{__HW_ID(3, 1),	DRM_FORMAT_BGR888,	1,	RICH_SIMPLE,	Rot_ALL_H_V,	LYT_NM_WB, AFB_TH_SC_YTR_BS}, /* afbc */
	/* BGR 16bpp */
	{__HW_ID(4, 0),	DRM_FORMAT_RGBA5551,	1,	RICH_SIMPLE,	Flip_H_V,		0, 0},
	{__HW_ID(4, 1),	DRM_FORMAT_ABGR1555,	1,	RICH_SIMPLE,	Flip_H_V,		0, 0},
	{__HW_ID(4, 1),	DRM_FORMAT_ABGR1555,	1,	RICH_SIMPLE,	Rot_ALL_H_V,	LYT_NM_WB, AFB_TH_SC_YTR}, /* afbc */
	{__HW_ID(4, 2),	DRM_FORMAT_RGB565,	1,	RICH_SIMPLE,	Flip_H_V,		0, 0},
	{__HW_ID(4, 3),	DRM_FORMAT_BGR565,	1,	RICH_SIMPLE,	Flip_H_V,		0, 0},
	{__HW_ID(4, 3),	DRM_FORMAT_BGR565,	1,	RICH_SIMPLE,	Rot_ALL_H_V,	LYT_NM_WB, AFB_TH_SC_YTR}, /* afbc */
	{__HW_ID(4, 4), DRM_FORMAT_R8,		1,	SIMPLE,		Rot_0,			0, 0},
	/* YUV 444/422/420 8bit  */
	{__HW_ID(5, 0),	0 /*XYUV8888*/,		1,	0,		0,			0, 0},
	/* XYUV unsupported*/
	{__HW_ID(5, 1),	DRM_FORMAT_YUYV,	1,	RICH,		Rot_ALL_H_V,	LYT_NM, AFB_TH}, /* afbc */
	{__HW_ID(5, 2),	DRM_FORMAT_YUYV,	1,	RICH,		Flip_H_V,		0, 0},
	{__HW_ID(5, 3),	DRM_FORMAT_UYVY,	1,	RICH,		Flip_H_V,		0, 0},
	{__HW_ID(5, 4),	0, /*X0L0 */		2,		0,			0, 0}, /* Y0L0 unsupported */
	{__HW_ID(5, 6),	DRM_FORMAT_NV12,	1,	RICH,		Flip_H_V,		0, 0},
	{__HW_ID(5, 6),	0/*DRM_FORMAT_YUV420_8BIT*/,	1,	RICH,	Rot_ALL_H_V,	LYT_NM, AFB_TH}, /* afbc */
	{__HW_ID(5, 7),	DRM_FORMAT_YUV420,	1,	RICH,		Flip_H_V,		0, 0},
	/* YUV 10bit*/
	{__HW_ID(6, 0),	0,/*XVYU2101010*/	1,	0,		0,			0, 0},/* VYV30 unsupported */
	{__HW_ID(6, 6),	0/*DRM_FORMAT_X0L2*/,	2,	RICH,		Flip_H_V,		0, 0},
	{__HW_ID(6, 7),	0/*DRM_FORMAT_P010*/,	1,	RICH,		Flip_H_V,		0, 0},
	{__HW_ID(6, 7),	0/*DRM_FORMAT_YUV420_10BIT*/, 1,	RICH,	Rot_ALL_H_V,	LYT_NM, AFB_TH},
};

static void d71_init_fmt_tbl(struct komeda_dev *mdev)
{
	struct komeda_format_caps_table *table = &mdev->fmt_tbl;

	table->format_caps = d71_format_caps_table;
	table->n_formats = ARRAY_SIZE(d71_format_caps_table);
}

static struct komeda_dev_funcs d71_chip_funcs = {
	.init_format_table = d71_init_fmt_tbl,
	.enum_resources	= d71_enum_resources,
	.cleanup	= NULL,
};

#define GLB_ARCH_ID		0x000
#define GLB_CORE_ID		0x004
#define GLB_CORE_INFO		0x008

struct komeda_dev_funcs *
d71_identify(u32 __iomem *reg_base, struct komeda_chip_info *chip)
{
	chip->arch_id	= malidp_read32(reg_base, GLB_ARCH_ID);
	chip->core_id	= malidp_read32(reg_base, GLB_CORE_ID);
	chip->core_info	= malidp_read32(reg_base, GLB_CORE_INFO);

	return &d71_chip_funcs;
}
