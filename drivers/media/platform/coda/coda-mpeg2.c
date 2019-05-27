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
