/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: PC Chen <pc.chen@mediatek.com>
 *		   Tiffany Lin <tiffany.lin@mediatek.com>
 */

#ifndef _VDEC_DRV_IF_H_
#define _VDEC_DRV_IF_H_

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_dec.h"
#include "mtk_vcodec_util.h"


/**
 * enum vdec_fb_status  - decoder frame buffer status
 * @FB_ST_NORMAL: initial state
 * @FB_ST_DISPLAY: frame buffer is ready to be displayed
 * @FB_ST_FREE: frame buffer is not used by decoder any more
 */
enum vdec_fb_status {
	FB_ST_NORMAL		= 0,
	FB_ST_DISPLAY		= (1 << 0),
	FB_ST_FREE		= (1 << 1)
};

/* For GET_PARAM_DISP_FRAME_BUFFER and GET_PARAM_FREE_FRAME_BUFFER,
 * the caller does not own the returned buffer. The buffer will not be
 *				released before vdec_if_deinit.
 * GET_PARAM_DISP_FRAME_BUFFER	: get next displayable frame buffer,
 *				struct vdec_fb**
 * GET_PARAM_FREE_FRAME_BUFFER	: get non-referenced framebuffer, vdec_fb**
 * GET_PARAM_PIC_INFO		: get picture info, struct vdec_pic_info*
 * GET_PARAM_CROP_INFO		: get crop info, struct v4l2_crop*
 * GET_PARAM_DPB_SIZE		: get dpb size, unsigned int*
 */
enum vdec_get_param_type {
	GET_PARAM_DISP_FRAME_BUFFER,
	GET_PARAM_FREE_FRAME_BUFFER,
	GET_PARAM_PIC_INFO,
	GET_PARAM_CROP_INFO,
	GET_PARAM_DPB_SIZE
};

/**
 * struct vdec_fb_node  - decoder frame buffer node
 * @list	: list to hold this node
 * @fb	: point to frame buffer (vdec_fb), fb could point to frame buffer and
 *	working buffer this is for maintain buffers in different state
 */
struct vdec_fb_node {
	struct list_head list;
	struct vdec_fb *fb;
};

extern const struct vdec_common_if vdec_h264_if;
extern const struct vdec_common_if vdec_vp8_if;
extern const struct vdec_common_if vdec_vp9_if;

/**
 * vdec_if_init() - initialize decode driver
 * @ctx	: [in] v4l2 context
 * @fourcc	: [in] video format fourcc, V4L2_PIX_FMT_H264/VP8/VP9..
 */
int vdec_if_init(struct mtk_vcodec_ctx *ctx, unsigned int fourcc);

/**
 * vdec_if_deinit() - deinitialize decode driver
 * @ctx	: [in] v4l2 context
 *
 */
void vdec_if_deinit(struct mtk_vcodec_ctx *ctx);

/**
 * vdec_if_decode() - trigger decode
 * @ctx	: [in] v4l2 context
 * @bs	: [in] input bitstream
 * @fb	: [in] frame buffer to store decoded frame, when null means parse
 *	header only
 * @res_chg	: [out] resolution change happens if current bs have different
 *	picture width/height
 * Note: To flush the decoder when reaching EOF, set input bitstream as NULL.
 *
 * Return: 0 on success. -EIO on unrecoverable error.
 */
int vdec_if_decode(struct mtk_vcodec_ctx *ctx, struct mtk_vcodec_mem *bs,
		   struct vdec_fb *fb, bool *res_chg);

/**
 * vdec_if_get_param() - get driver's parameter
 * @ctx	: [in] v4l2 context
 * @type	: [in] input parameter type
 * @out	: [out] buffer to store query result
 */
int vdec_if_get_param(struct mtk_vcodec_ctx *ctx, enum vdec_get_param_type type,
		      void *out);

#endif
