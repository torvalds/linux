/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MTK_MDP3_TYPE_H__
#define __MTK_MDP3_TYPE_H__

#include <linux/types.h>

#define IMG_MAX_HW_INPUTS	3
#define IMG_MAX_HW_OUTPUTS	4
#define IMG_MAX_PLANES		3
#define IMG_MAX_COMPONENTS	20

struct img_crop {
	s32 left;
	s32 top;
	u32 width;
	u32 height;
	u32 left_subpix;
	u32 top_subpix;
	u32 width_subpix;
	u32 height_subpix;
} __packed;

struct img_region {
	s32 left;
	s32 right;
	s32 top;
	s32 bottom;
} __packed;

struct img_offset {
	s32 left;
	s32 top;
	u32 left_subpix;
	u32 top_subpix;
} __packed;

struct img_mux {
	u32 reg;
	u32 value;
	u32 subsys_id;
} __packed;

struct img_mmsys_ctrl {
	struct img_mux sets[IMG_MAX_COMPONENTS * 2];
	u32 num_sets;
} __packed;

#endif  /* __MTK_MDP3_TYPE_H__ */
