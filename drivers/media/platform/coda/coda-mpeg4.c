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
