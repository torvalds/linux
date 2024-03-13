/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Jernej Skrabec <jernej.skrabec@siol.net> */

#ifndef _SUN8I_FORMATS_H_
#define _SUN8I_FORMATS_H_

#include <linux/videodev2.h>

#define ROTATE_FLAG_YUV    BIT(0)
#define ROTATE_FLAG_OUTPUT BIT(1)

struct rotate_format {
	u32 fourcc;
	u32 hw_format;
	int planes;
	int bpp[3];
	int hsub;
	int vsub;
	unsigned int flags;
};

const struct rotate_format *rotate_find_format(u32 pixelformat);
int rotate_enum_fmt(struct v4l2_fmtdesc *f, bool dst);

#endif
