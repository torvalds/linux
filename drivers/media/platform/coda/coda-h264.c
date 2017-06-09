/*
 * Coda multi-standard codec IP - H.264 helper functions
 *
 * Copyright (C) 2012 Vista Silicon S.L.
 *    Javier Martin, <javier.martin@vista-silicon.com>
 *    Xavier Duret
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/videodev2.h>
#include <coda.h>

static const u8 coda_filler_size[8] = { 0, 7, 14, 13, 12, 11, 10, 9 };

static const u8 *coda_find_nal_header(const u8 *buf, const u8 *end)
{
	u32 val = 0xffffffff;

	do {
		val = val << 8 | *buf++;
		if (buf >= end)
			return NULL;
	} while (val != 0x00000001);

	return buf;
}

int coda_sps_parse_profile(struct coda_ctx *ctx, struct vb2_buffer *vb)
{
	const u8 *buf = vb2_plane_vaddr(vb, 0);
	const u8 *end = buf + vb2_get_plane_payload(vb, 0);

	/* Find SPS header */
	do {
		buf = coda_find_nal_header(buf, end);
		if (!buf)
			return -EINVAL;
	} while ((*buf++ & 0x1f) != 0x7);

	ctx->params.h264_profile_idc = buf[0];
	ctx->params.h264_level_idc = buf[2];

	return 0;
}

int coda_h264_filler_nal(int size, char *p)
{
	if (size < 6)
		return -EINVAL;

	p[0] = 0x00;
	p[1] = 0x00;
	p[2] = 0x00;
	p[3] = 0x01;
	p[4] = 0x0c;
	memset(p + 5, 0xff, size - 6);
	/* Add rbsp stop bit and trailing at the end */
	p[size - 1] = 0x80;

	return 0;
}

int coda_h264_padding(int size, char *p)
{
	int nal_size;
	int diff;

	diff = size - (size & ~0x7);
	if (diff == 0)
		return 0;

	nal_size = coda_filler_size[diff];
	coda_h264_filler_nal(nal_size, p);

	return nal_size;
}

int coda_h264_profile(int profile_idc)
{
	switch (profile_idc) {
	case 66: return V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
	case 77: return V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;
	case 88: return V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED;
	case 100: return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
	default: return -EINVAL;
	}
}

int coda_h264_level(int level_idc)
{
	switch (level_idc) {
	case 10: return V4L2_MPEG_VIDEO_H264_LEVEL_1_0;
	case 9:  return V4L2_MPEG_VIDEO_H264_LEVEL_1B;
	case 11: return V4L2_MPEG_VIDEO_H264_LEVEL_1_1;
	case 12: return V4L2_MPEG_VIDEO_H264_LEVEL_1_2;
	case 13: return V4L2_MPEG_VIDEO_H264_LEVEL_1_3;
	case 20: return V4L2_MPEG_VIDEO_H264_LEVEL_2_0;
	case 21: return V4L2_MPEG_VIDEO_H264_LEVEL_2_1;
	case 22: return V4L2_MPEG_VIDEO_H264_LEVEL_2_2;
	case 30: return V4L2_MPEG_VIDEO_H264_LEVEL_3_0;
	case 31: return V4L2_MPEG_VIDEO_H264_LEVEL_3_1;
	case 32: return V4L2_MPEG_VIDEO_H264_LEVEL_3_2;
	case 40: return V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
	case 41: return V4L2_MPEG_VIDEO_H264_LEVEL_4_1;
	default: return -EINVAL;
	}
}
