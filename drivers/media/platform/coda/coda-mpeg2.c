// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Coda multi-standard codec IP - MPEG-2 helper functions
 *
 * Copyright (C) 2019 Pengutronix, Philipp Zabel
 */

#include <linux/kernel.h>
#include <linux/videodev2.h>
#include "coda.h"

int coda_mpeg2_profile(int profile_idc)
{
	switch (profile_idc) {
	case 5:
		return V4L2_MPEG_VIDEO_MPEG2_PROFILE_SIMPLE;
	case 4:
		return V4L2_MPEG_VIDEO_MPEG2_PROFILE_MAIN;
	case 3:
		return V4L2_MPEG_VIDEO_MPEG2_PROFILE_SNR_SCALABLE;
	case 2:
		return V4L2_MPEG_VIDEO_MPEG2_PROFILE_SPATIALLY_SCALABLE;
	case 1:
		return V4L2_MPEG_VIDEO_MPEG2_PROFILE_HIGH;
	default:
		return -EINVAL;
	}
}

int coda_mpeg2_level(int level_idc)
{
	switch (level_idc) {
	case 10:
		return V4L2_MPEG_VIDEO_MPEG2_LEVEL_LOW;
	case 8:
		return V4L2_MPEG_VIDEO_MPEG2_LEVEL_MAIN;
	case 6:
		return V4L2_MPEG_VIDEO_MPEG2_LEVEL_HIGH_1440;
	case 4:
		return V4L2_MPEG_VIDEO_MPEG2_LEVEL_HIGH;
	default:
		return -EINVAL;
	}
}

/*
 * Check if the buffer starts with the MPEG-2 sequence header (with or without
 * quantization matrix) and extension header, for example:
 *
 *   00 00 01 b3 2d 01 e0 34 08 8b a3 81
 *               10 11 11 12 12 12 13 13 13 13 14 14 14 14 14 15
 *               15 15 15 15 15 16 16 16 16 16 16 16 17 17 17 17
 *               17 17 17 17 18 18 18 19 18 18 18 19 1a 1a 1a 1a
 *               19 1b 1b 1b 1b 1b 1c 1c 1c 1c 1e 1e 1e 1f 1f 21
 *   00 00 01 b5 14 8a 00 01 00 00
 *
 * or:
 *
 *   00 00 01 b3 08 00 40 15 ff ff e0 28
 *   00 00 01 b5 14 8a 00 01 00 00
 *
 * Returns the detected header size in bytes or 0.
 */
u32 coda_mpeg2_parse_headers(struct coda_ctx *ctx, u8 *buf, u32 size)
{
	static const u8 sequence_header_start[4] = { 0x00, 0x00, 0x01, 0xb3 };
	static const union {
		u8 extension_start[4];
		u8 start_code_prefix[3];
	} u = { { 0x00, 0x00, 0x01, 0xb5 } };

	if (size < 22 ||
	    memcmp(buf, sequence_header_start, 4) != 0)
		return 0;

	if ((size == 22 ||
	     (size >= 25 && memcmp(buf + 22, u.start_code_prefix, 3) == 0)) &&
	    memcmp(buf + 12, u.extension_start, 4) == 0)
		return 22;

	if ((size == 86 ||
	     (size > 89 && memcmp(buf + 86, u.start_code_prefix, 3) == 0)) &&
	    memcmp(buf + 76, u.extension_start, 4) == 0)
		return 86;

	return 0;
}
