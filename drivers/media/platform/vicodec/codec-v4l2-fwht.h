/* SPDX-License-Identifier: LGPL-2.1 */
/*
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef CODEC_V4L2_FWHT_H
#define CODEC_V4L2_FWHT_H

#include "codec-fwht.h"

struct v4l2_fwht_pixfmt_info {
	u32 id;
	unsigned int bytesperline_mult;
	unsigned int sizeimage_mult;
	unsigned int sizeimage_div;
	unsigned int luma_alpha_step;
	unsigned int chroma_step;
	/* Chroma plane subsampling */
	unsigned int width_div;
	unsigned int height_div;
	unsigned int components_num;
};

struct v4l2_fwht_state {
	const struct v4l2_fwht_pixfmt_info *info;
	unsigned int width;
	unsigned int height;
	unsigned int gop_size;
	unsigned int gop_cnt;
	u16 i_frame_qp;
	u16 p_frame_qp;

	enum v4l2_colorspace colorspace;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_xfer_func xfer_func;
	enum v4l2_quantization quantization;

	struct fwht_raw_frame ref_frame;
	u8 *compressed_frame;
};

const struct v4l2_fwht_pixfmt_info *v4l2_fwht_find_pixfmt(u32 pixelformat);
const struct v4l2_fwht_pixfmt_info *v4l2_fwht_get_pixfmt(u32 idx);

int v4l2_fwht_encode(struct v4l2_fwht_state *state, u8 *p_in, u8 *p_out);
int v4l2_fwht_decode(struct v4l2_fwht_state *state, u8 *p_in, u8 *p_out);

#endif
