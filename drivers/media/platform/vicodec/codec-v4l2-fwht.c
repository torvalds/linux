// SPDX-License-Identifier: LGPL-2.1
/*
 * A V4L2 frontend for the FWHT codec
 *
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/string.h>
#include <linux/videodev2.h>
#include "codec-v4l2-fwht.h"

static const struct v4l2_fwht_pixfmt_info v4l2_fwht_pixfmts[] = {
	{ V4L2_PIX_FMT_YUV420,  1, 3, 2, 1, 1, 2, 2, 3},
	{ V4L2_PIX_FMT_YVU420,  1, 3, 2, 1, 1, 2, 2, 3},
	{ V4L2_PIX_FMT_YUV422P, 1, 2, 1, 1, 1, 2, 1, 3},
	{ V4L2_PIX_FMT_NV12,    1, 3, 2, 1, 2, 2, 2, 3},
	{ V4L2_PIX_FMT_NV21,    1, 3, 2, 1, 2, 2, 2, 3},
	{ V4L2_PIX_FMT_NV16,    1, 2, 1, 1, 2, 2, 1, 3},
	{ V4L2_PIX_FMT_NV61,    1, 2, 1, 1, 2, 2, 1, 3},
	{ V4L2_PIX_FMT_NV24,    1, 3, 1, 1, 2, 1, 1, 3},
	{ V4L2_PIX_FMT_NV42,    1, 3, 1, 1, 2, 1, 1, 3},
	{ V4L2_PIX_FMT_YUYV,    2, 2, 1, 2, 4, 2, 1, 3},
	{ V4L2_PIX_FMT_YVYU,    2, 2, 1, 2, 4, 2, 1, 3},
	{ V4L2_PIX_FMT_UYVY,    2, 2, 1, 2, 4, 2, 1, 3},
	{ V4L2_PIX_FMT_VYUY,    2, 2, 1, 2, 4, 2, 1, 3},
	{ V4L2_PIX_FMT_BGR24,   3, 3, 1, 3, 3, 1, 1, 3},
	{ V4L2_PIX_FMT_RGB24,   3, 3, 1, 3, 3, 1, 1, 3},
	{ V4L2_PIX_FMT_HSV24,   3, 3, 1, 3, 3, 1, 1, 3},
	{ V4L2_PIX_FMT_BGR32,   4, 4, 1, 4, 4, 1, 1, 3},
	{ V4L2_PIX_FMT_XBGR32,  4, 4, 1, 4, 4, 1, 1, 3},
	{ V4L2_PIX_FMT_RGB32,   4, 4, 1, 4, 4, 1, 1, 3},
	{ V4L2_PIX_FMT_XRGB32,  4, 4, 1, 4, 4, 1, 1, 3},
	{ V4L2_PIX_FMT_HSV32,   4, 4, 1, 4, 4, 1, 1, 3},
	{ V4L2_PIX_FMT_ARGB32,  4, 4, 1, 4, 4, 1, 1, 4},
	{ V4L2_PIX_FMT_ABGR32,  4, 4, 1, 4, 4, 1, 1, 4},
	{ V4L2_PIX_FMT_GREY,    1, 1, 1, 1, 0, 1, 1, 1},
};

const struct v4l2_fwht_pixfmt_info *v4l2_fwht_find_pixfmt(u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(v4l2_fwht_pixfmts); i++)
		if (v4l2_fwht_pixfmts[i].id == pixelformat)
			return v4l2_fwht_pixfmts + i;
	return NULL;
}

const struct v4l2_fwht_pixfmt_info *v4l2_fwht_get_pixfmt(u32 idx)
{
	if (idx >= ARRAY_SIZE(v4l2_fwht_pixfmts))
		return NULL;
	return v4l2_fwht_pixfmts + idx;
}

int v4l2_fwht_encode(struct v4l2_fwht_state *state, u8 *p_in, u8 *p_out)
{
	unsigned int size = state->width * state->height;
	const struct v4l2_fwht_pixfmt_info *info = state->info;
	struct fwht_cframe_hdr *p_hdr;
	struct fwht_cframe cf;
	struct fwht_raw_frame rf;
	u32 encoding;
	u32 flags = 0;

	if (!info)
		return -EINVAL;
	rf.width = state->width;
	rf.height = state->height;
	rf.luma = p_in;
	rf.width_div = info->width_div;
	rf.height_div = info->height_div;
	rf.luma_alpha_step = info->luma_alpha_step;
	rf.chroma_step = info->chroma_step;
	rf.alpha = NULL;
	rf.components_num = info->components_num;

	switch (info->id) {
	case V4L2_PIX_FMT_GREY:
		rf.cb = NULL;
		rf.cr = NULL;
		break;
	case V4L2_PIX_FMT_YUV420:
		rf.cb = rf.luma + size;
		rf.cr = rf.cb + size / 4;
		break;
	case V4L2_PIX_FMT_YVU420:
		rf.cr = rf.luma + size;
		rf.cb = rf.cr + size / 4;
		break;
	case V4L2_PIX_FMT_YUV422P:
		rf.cb = rf.luma + size;
		rf.cr = rf.cb + size / 2;
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV24:
		rf.cb = rf.luma + size;
		rf.cr = rf.cb + 1;
		break;
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_NV42:
		rf.cr = rf.luma + size;
		rf.cb = rf.cr + 1;
		break;
	case V4L2_PIX_FMT_YUYV:
		rf.cb = rf.luma + 1;
		rf.cr = rf.cb + 2;
		break;
	case V4L2_PIX_FMT_YVYU:
		rf.cr = rf.luma + 1;
		rf.cb = rf.cr + 2;
		break;
	case V4L2_PIX_FMT_UYVY:
		rf.cb = rf.luma;
		rf.cr = rf.cb + 2;
		rf.luma++;
		break;
	case V4L2_PIX_FMT_VYUY:
		rf.cr = rf.luma;
		rf.cb = rf.cr + 2;
		rf.luma++;
		break;
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_HSV24:
		rf.cr = rf.luma;
		rf.cb = rf.cr + 2;
		rf.luma++;
		break;
	case V4L2_PIX_FMT_BGR24:
		rf.cb = rf.luma;
		rf.cr = rf.cb + 2;
		rf.luma++;
		break;
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_XRGB32:
	case V4L2_PIX_FMT_HSV32:
		rf.cr = rf.luma + 1;
		rf.cb = rf.cr + 2;
		rf.luma += 2;
		break;
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_XBGR32:
		rf.cb = rf.luma;
		rf.cr = rf.cb + 2;
		rf.luma++;
		break;
	case V4L2_PIX_FMT_ARGB32:
		rf.alpha = rf.luma;
		rf.cr = rf.luma + 1;
		rf.cb = rf.cr + 2;
		rf.luma += 2;
		break;
	case V4L2_PIX_FMT_ABGR32:
		rf.cb = rf.luma;
		rf.cr = rf.cb + 2;
		rf.luma++;
		rf.alpha = rf.cr + 1;
		break;
	default:
		return -EINVAL;
	}

	cf.width = state->width;
	cf.height = state->height;
	cf.i_frame_qp = state->i_frame_qp;
	cf.p_frame_qp = state->p_frame_qp;
	cf.rlc_data = (__be16 *)(p_out + sizeof(*p_hdr));

	encoding = fwht_encode_frame(&rf, &state->ref_frame, &cf,
				     !state->gop_cnt,
				     state->gop_cnt == state->gop_size - 1);
	if (!(encoding & FWHT_FRAME_PCODED))
		state->gop_cnt = 0;
	if (++state->gop_cnt >= state->gop_size)
		state->gop_cnt = 0;

	p_hdr = (struct fwht_cframe_hdr *)p_out;
	p_hdr->magic1 = FWHT_MAGIC1;
	p_hdr->magic2 = FWHT_MAGIC2;
	p_hdr->version = htonl(FWHT_VERSION);
	p_hdr->width = htonl(cf.width);
	p_hdr->height = htonl(cf.height);
	flags |= (info->components_num - 1) << FWHT_FL_COMPONENTS_NUM_OFFSET;
	if (encoding & FWHT_LUMA_UNENCODED)
		flags |= FWHT_FL_LUMA_IS_UNCOMPRESSED;
	if (encoding & FWHT_CB_UNENCODED)
		flags |= FWHT_FL_CB_IS_UNCOMPRESSED;
	if (encoding & FWHT_CR_UNENCODED)
		flags |= FWHT_FL_CR_IS_UNCOMPRESSED;
	if (encoding & FWHT_ALPHA_UNENCODED)
		flags |= FWHT_FL_ALPHA_IS_UNCOMPRESSED;
	if (rf.height_div == 1)
		flags |= FWHT_FL_CHROMA_FULL_HEIGHT;
	if (rf.width_div == 1)
		flags |= FWHT_FL_CHROMA_FULL_WIDTH;
	p_hdr->flags = htonl(flags);
	p_hdr->colorspace = htonl(state->colorspace);
	p_hdr->xfer_func = htonl(state->xfer_func);
	p_hdr->ycbcr_enc = htonl(state->ycbcr_enc);
	p_hdr->quantization = htonl(state->quantization);
	p_hdr->size = htonl(cf.size);
	state->ref_frame.width = cf.width;
	state->ref_frame.height = cf.height;
	return cf.size + sizeof(*p_hdr);
}

int v4l2_fwht_decode(struct v4l2_fwht_state *state, u8 *p_in, u8 *p_out)
{
	unsigned int size = state->width * state->height;
	unsigned int chroma_size = size;
	unsigned int i;
	u32 flags;
	struct fwht_cframe_hdr *p_hdr;
	struct fwht_cframe cf;
	u8 *p;
	unsigned int components_num = 3;
	unsigned int version;

	if (!state->info)
		return -EINVAL;

	p_hdr = (struct fwht_cframe_hdr *)p_in;
	cf.width = ntohl(p_hdr->width);
	cf.height = ntohl(p_hdr->height);

	version = ntohl(p_hdr->version);
	if (!version || version > FWHT_VERSION) {
		pr_err("version %d is not supported, current version is %d\n",
		       version, FWHT_VERSION);
		return -EINVAL;
	}

	if (p_hdr->magic1 != FWHT_MAGIC1 ||
	    p_hdr->magic2 != FWHT_MAGIC2 ||
	    (cf.width & 7) || (cf.height & 7))
		return -EINVAL;

	/* TODO: support resolution changes */
	if (cf.width != state->width || cf.height != state->height)
		return -EINVAL;

	flags = ntohl(p_hdr->flags);

	if (version == FWHT_VERSION) {
		components_num = 1 + ((flags & FWHT_FL_COMPONENTS_NUM_MSK) >>
			FWHT_FL_COMPONENTS_NUM_OFFSET);
	}

	state->colorspace = ntohl(p_hdr->colorspace);
	state->xfer_func = ntohl(p_hdr->xfer_func);
	state->ycbcr_enc = ntohl(p_hdr->ycbcr_enc);
	state->quantization = ntohl(p_hdr->quantization);
	cf.rlc_data = (__be16 *)(p_in + sizeof(*p_hdr));

	if (!(flags & FWHT_FL_CHROMA_FULL_WIDTH))
		chroma_size /= 2;
	if (!(flags & FWHT_FL_CHROMA_FULL_HEIGHT))
		chroma_size /= 2;

	fwht_decode_frame(&cf, &state->ref_frame, flags, components_num);

	/*
	 * TODO - handle the case where the compressed stream encodes a
	 * different format than the requested decoded format.
	 */
	switch (state->info->id) {
	case V4L2_PIX_FMT_GREY:
		memcpy(p_out, state->ref_frame.luma, size);
		break;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YUV422P:
		memcpy(p_out, state->ref_frame.luma, size);
		p_out += size;
		memcpy(p_out, state->ref_frame.cb, chroma_size);
		p_out += chroma_size;
		memcpy(p_out, state->ref_frame.cr, chroma_size);
		break;
	case V4L2_PIX_FMT_YVU420:
		memcpy(p_out, state->ref_frame.luma, size);
		p_out += size;
		memcpy(p_out, state->ref_frame.cr, chroma_size);
		p_out += chroma_size;
		memcpy(p_out, state->ref_frame.cb, chroma_size);
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV24:
		memcpy(p_out, state->ref_frame.luma, size);
		p_out += size;
		for (i = 0, p = p_out; i < chroma_size; i++) {
			*p++ = state->ref_frame.cb[i];
			*p++ = state->ref_frame.cr[i];
		}
		break;
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_NV42:
		memcpy(p_out, state->ref_frame.luma, size);
		p_out += size;
		for (i = 0, p = p_out; i < chroma_size; i++) {
			*p++ = state->ref_frame.cr[i];
			*p++ = state->ref_frame.cb[i];
		}
		break;
	case V4L2_PIX_FMT_YUYV:
		for (i = 0, p = p_out; i < size; i += 2) {
			*p++ = state->ref_frame.luma[i];
			*p++ = state->ref_frame.cb[i / 2];
			*p++ = state->ref_frame.luma[i + 1];
			*p++ = state->ref_frame.cr[i / 2];
		}
		break;
	case V4L2_PIX_FMT_YVYU:
		for (i = 0, p = p_out; i < size; i += 2) {
			*p++ = state->ref_frame.luma[i];
			*p++ = state->ref_frame.cr[i / 2];
			*p++ = state->ref_frame.luma[i + 1];
			*p++ = state->ref_frame.cb[i / 2];
		}
		break;
	case V4L2_PIX_FMT_UYVY:
		for (i = 0, p = p_out; i < size; i += 2) {
			*p++ = state->ref_frame.cb[i / 2];
			*p++ = state->ref_frame.luma[i];
			*p++ = state->ref_frame.cr[i / 2];
			*p++ = state->ref_frame.luma[i + 1];
		}
		break;
	case V4L2_PIX_FMT_VYUY:
		for (i = 0, p = p_out; i < size; i += 2) {
			*p++ = state->ref_frame.cr[i / 2];
			*p++ = state->ref_frame.luma[i];
			*p++ = state->ref_frame.cb[i / 2];
			*p++ = state->ref_frame.luma[i + 1];
		}
		break;
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_HSV24:
		for (i = 0, p = p_out; i < size; i++) {
			*p++ = state->ref_frame.cr[i];
			*p++ = state->ref_frame.luma[i];
			*p++ = state->ref_frame.cb[i];
		}
		break;
	case V4L2_PIX_FMT_BGR24:
		for (i = 0, p = p_out; i < size; i++) {
			*p++ = state->ref_frame.cb[i];
			*p++ = state->ref_frame.luma[i];
			*p++ = state->ref_frame.cr[i];
		}
		break;
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_XRGB32:
	case V4L2_PIX_FMT_HSV32:
		for (i = 0, p = p_out; i < size; i++) {
			*p++ = 0;
			*p++ = state->ref_frame.cr[i];
			*p++ = state->ref_frame.luma[i];
			*p++ = state->ref_frame.cb[i];
		}
		break;
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_XBGR32:
		for (i = 0, p = p_out; i < size; i++) {
			*p++ = state->ref_frame.cb[i];
			*p++ = state->ref_frame.luma[i];
			*p++ = state->ref_frame.cr[i];
			*p++ = 0;
		}
		break;
	case V4L2_PIX_FMT_ARGB32:
		for (i = 0, p = p_out; i < size; i++) {
			*p++ = state->ref_frame.alpha[i];
			*p++ = state->ref_frame.cr[i];
			*p++ = state->ref_frame.luma[i];
			*p++ = state->ref_frame.cb[i];
		}
		break;
	case V4L2_PIX_FMT_ABGR32:
		for (i = 0, p = p_out; i < size; i++) {
			*p++ = state->ref_frame.cb[i];
			*p++ = state->ref_frame.luma[i];
			*p++ = state->ref_frame.cr[i];
			*p++ = state->ref_frame.alpha[i];
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
