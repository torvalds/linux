/* SPDX-License-Identifier: GPL-2.0 OR MIT
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __AMDGPU_VIDEO_CODECS_H__
#define __AMDGPU_VIDEO_CODECS_H__

#include <linux/types.h>

#define codec_info_build(type, width, height, level) \
			 .codec_type = type,\
			 .max_width = width,\
			 .max_height = height,\
			 .max_pixels_per_frame = height * width,\
			 .max_level = level,

struct amdgpu_video_codec_info {
	u32 codec_type;
	u32 max_width;
	u32 max_height;
	u32 max_pixels_per_frame;
	u32 max_level;
};

struct amdgpu_video_codecs {
	const u32 codec_count;
	const struct amdgpu_video_codec_info *codec_array;
};
#endif
