/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RP1 CFE Driver
 *
 * Copyright (c) 2021-2024 Raspberry Pi Ltd.
 * Copyright (c) 2023-2024 Ideas on Board Oy
 */
#ifndef _RP1_CFE_
#define _RP1_CFE_

#include <linux/media-bus-format.h>
#include <linux/types.h>
#include <linux/videodev2.h>

extern bool cfe_debug_verbose;

enum cfe_remap_types {
	CFE_REMAP_16BIT,
	CFE_REMAP_COMPRESSED,
	CFE_NUM_REMAP,
};

#define CFE_FORMAT_FLAG_META_OUT	BIT(0)
#define CFE_FORMAT_FLAG_META_CAP	BIT(1)
#define CFE_FORMAT_FLAG_FE_OUT		BIT(2)

struct cfe_fmt {
	u32 fourcc;
	u32 code;
	u8 depth;
	u8 csi_dt;
	u32 remap[CFE_NUM_REMAP];
	u32 flags;
};

extern const struct v4l2_mbus_framefmt cfe_default_format;

const struct cfe_fmt *find_format_by_code(u32 code);
const struct cfe_fmt *find_format_by_pix(u32 pixelformat);
u32 cfe_find_16bit_code(u32 code);
u32 cfe_find_compressed_code(u32 code);

#endif
