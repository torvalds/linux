// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Coda multi-standard codec IP - MPEG-4 helper functions
 *
 * Copyright (C) 2019 Pengutronix, Philipp Zabel
 */

#include <linux/kernel.h>
#include <linux/videodev2.h>

#include "coda.h"

int coda_mpeg4_profile(int profile_idc)
{
	switch (profile_idc) {
	case 0:
		return V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE;
	case 15:
		return V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_SIMPLE;
	case 2:
		return V4L2_MPEG_VIDEO_MPEG4_PROFILE_CORE;
	case 1:
		return V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE_SCALABLE;
	case 11:
		return V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_CODING_EFFICIENCY;
	default:
		return -EINVAL;
	}
}

int coda_mpeg4_level(int level_idc)
{
	switch (level_idc) {
	case 0:
		return V4L2_MPEG_VIDEO_MPEG4_LEVEL_0;
	case 1:
		return V4L2_MPEG_VIDEO_MPEG4_LEVEL_1;
	case 2:
		return V4L2_MPEG_VIDEO_MPEG4_LEVEL_2;
	case 3:
		return V4L2_MPEG_VIDEO_MPEG4_LEVEL_3;
	case 4:
		return V4L2_MPEG_VIDEO_MPEG4_LEVEL_4;
	case 5:
		return V4L2_MPEG_VIDEO_MPEG4_LEVEL_5;
	default:
		return -EINVAL;
	}
}

/*
 * Check if the buffer starts with the MPEG-4 visual object sequence and visual
 * object headers, for example:
 *
 *   00 00 01 b0 f1
 *   00 00 01 b5 a9 13 00 00 01 00 00 00 01 20 08
 *               d4 8d 88 00 f5 04 04 08 14 30 3f
 *
 * Returns the detected header size in bytes or 0.
 */
u32 coda_mpeg4_parse_headers(struct coda_ctx *ctx, u8 *buf, u32 size)
{
	static const u8 vos_start[4] = { 0x00, 0x00, 0x01, 0xb0 };
	static const union {
		u8 vo_start[4];
		u8 start_code_prefix[3];
	} u = { { 0x00, 0x00, 0x01, 0xb5 } };

	if (size < 30 ||
	    memcmp(buf, vos_start, 4) != 0 ||
	    memcmp(buf + 5, u.vo_start, 4) != 0)
		return 0;

	if (size == 30 ||
	    (size >= 33 && memcmp(buf + 30, u.start_code_prefix, 3) == 0))
		return 30;

	if (size == 31 ||
	    (size >= 34 && memcmp(buf + 31, u.start_code_prefix, 3) == 0))
		return 31;

	if (size == 32 ||
	    (size >= 35 && memcmp(buf + 32, u.start_code_prefix, 3) == 0))
		return 32;

	return 0;
}
