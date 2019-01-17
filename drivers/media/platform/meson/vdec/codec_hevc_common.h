/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#ifndef __MESON_VDEC_HEVC_COMMON_H_
#define __MESON_VDEC_HEVC_COMMON_H_

#include "vdec.h"

#define PARSER_CMD_SKIP_CFG_0 0x0000090b
#define PARSER_CMD_SKIP_CFG_1 0x1b14140f
#define PARSER_CMD_SKIP_CFG_2 0x001b1910
static const u16 vdec_hevc_parser_cmd[] = {
	0x0401,	0x8401,	0x0800,	0x0402,
	0x9002,	0x1423,	0x8CC3,	0x1423,
	0x8804,	0x9825,	0x0800,	0x04FE,
	0x8406,	0x8411,	0x1800,	0x8408,
	0x8409,	0x8C2A,	0x9C2B,	0x1C00,
	0x840F,	0x8407,	0x8000,	0x8408,
	0x2000,	0xA800,	0x8410,	0x04DE,
	0x840C,	0x840D,	0xAC00,	0xA000,
	0x08C0,	0x08E0,	0xA40E,	0xFC00,
	0x7C00
};

/* Returns 1 if we must use framebuffer compression */
static inline int codec_hevc_use_fbc(u32 pixfmt, int is_10bit)
{
	return pixfmt == V4L2_PIX_FMT_AM21C || is_10bit;
}

/* Returns 1 if we are decoding 10-bit but outputting 8-bit NV12 */
static inline int codec_hevc_use_downsample(u32 pixfmt, int is_10bit)
{
	return pixfmt == V4L2_PIX_FMT_NV12M && is_10bit;
}

/**
 * Configure decode head read mode
 */
void codec_hevc_setup_decode_head(struct amvdec_session *sess, int is_10bit);

void codec_hevc_free_fbc_buffers(struct amvdec_session *sess);

int codec_hevc_setup_buffers(struct amvdec_session *sess, int is_10bit);

#endif