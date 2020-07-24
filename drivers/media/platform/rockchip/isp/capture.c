/*
 * Rockchip isp1 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include "dev.h"
#include "regs.h"

#define CIF_ISP_REQ_BUFS_MIN			0
#define CIF_ISP_REQ_BUFS_MAX			8

#define STREAM_PAD_SINK				0
#define STREAM_PAD_SOURCE			1

#define STREAM_MAX_MP_RSZ_OUTPUT_WIDTH		4416
#define STREAM_MAX_MP_RSZ_OUTPUT_HEIGHT		3312
#define STREAM_MAX_SP_RSZ_OUTPUT_WIDTH		1920
#define STREAM_MAX_SP_RSZ_OUTPUT_HEIGHT		1080
#define STREAM_MIN_RSZ_OUTPUT_WIDTH		32
#define STREAM_MIN_RSZ_OUTPUT_HEIGHT		16
#define STREAM_OUTPUT_STEP_WISE			8

#define STREAM_MAX_MP_SP_INPUT_WIDTH STREAM_MAX_MP_RSZ_OUTPUT_WIDTH
#define STREAM_MAX_MP_SP_INPUT_HEIGHT STREAM_MAX_MP_RSZ_OUTPUT_HEIGHT
#define STREAM_MIN_MP_SP_INPUT_WIDTH		32
#define STREAM_MIN_MP_SP_INPUT_HEIGHT		32

static int mi_frame_end(struct rkisp_stream *stream);
static void rkisp_buf_queue(struct vb2_buffer *vb);

/* Get xsubs and ysubs for fourcc formats
 *
 * @xsubs: horizontal color samples in a 4*4 matrix, for yuv
 * @ysubs: vertical color samples in a 4*4 matrix, for yuv
 */
int fcc_xysubs(u32 fcc, u32 *xsubs, u32 *ysubs)
{
	switch (fcc) {
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_YUV444M:
		*xsubs = 1;
		*ysubs = 1;
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_YVU422M:
		*xsubs = 2;
		*ysubs = 1;
		break;
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21M:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		*xsubs = 2;
		*ysubs = 2;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mbus_code_xysubs(u32 code, u32 *xsubs, u32 *ysubs)
{
	switch (code) {
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YVYU8_1X16:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_VYUY8_1X16:
		*xsubs = 2;
		*ysubs = 1;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mbus_code_sp_in_fmt(u32 in_mbus_code, u32 out_fourcc,
			       u32 *format)
{
	switch (in_mbus_code) {
	case MEDIA_BUS_FMT_YUYV8_2X8:
		*format = MI_CTRL_SP_INPUT_YUV422;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Only SP can support output format of YCbCr4:0:0,
	 * and the input format of SP must be YCbCr4:0:0
	 * when outputting YCbCr4:0:0.
	 * The output format of isp is YCbCr4:2:2,
	 * so the CbCr data is discarded here.
	 */
	if (out_fourcc == V4L2_PIX_FMT_GREY)
		*format = MI_CTRL_SP_INPUT_YUV400;

	return 0;
}

static const struct capture_fmt mp_fmts[] = {
	/* yuv422 */
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.fmt_type = FMT_YUV,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUVINT,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 4, 4 },
		.cplanes = 3,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV61,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.uv_swap = 1,
		.write_format = MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV422M,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 8, 8 },
		.cplanes = 3,
		.mplanes = 3,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	},
	/* yuv420 */
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.uv_swap = 1,
		.write_format = MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV21M,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 2,
		.uv_swap = 1,
		.write_format = MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12M,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 2,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUV_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV420,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 8, 8 },
		.cplanes = 3,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	},
	/* yuv444 */
	{
		.fourcc = V4L2_PIX_FMT_YUV444M,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 8, 8 },
		.cplanes = 3,
		.mplanes = 3,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	},
	/* raw */
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
	},
};

static const struct capture_fmt sp_fmts[] = {
	/* yuv422 */
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.fmt_type = FMT_YUV,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_SP_WRITE_INT,
		.output_format = MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 8, 8 },
		.cplanes = 3,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_SP_WRITE_PLA,
		.output_format = MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_SP_WRITE_SPLA,
		.output_format = MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV61,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.uv_swap = 1,
		.write_format = MI_CTRL_SP_WRITE_SPLA,
		.output_format = MI_CTRL_SP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV422M,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 8, 8 },
		.cplanes = 3,
		.mplanes = 3,
		.uv_swap = 0,
		.write_format = MI_CTRL_SP_WRITE_PLA,
		.output_format = MI_CTRL_SP_OUTPUT_YUV422,
	},
	/* yuv420 */
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.uv_swap = 1,
		.write_format = MI_CTRL_SP_WRITE_SPLA,
		.output_format = MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_SP_WRITE_SPLA,
		.output_format = MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV21M,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 2,
		.uv_swap = 1,
		.write_format = MI_CTRL_SP_WRITE_SPLA,
		.output_format = MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12M,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 2,
		.uv_swap = 0,
		.write_format = MI_CTRL_SP_WRITE_SPLA,
		.output_format = MI_CTRL_SP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV420,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 8, 8 },
		.cplanes = 3,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_SP_WRITE_PLA,
		.output_format = MI_CTRL_SP_OUTPUT_YUV420,
	},
	/* yuv444 */
	{
		.fourcc = V4L2_PIX_FMT_YUV444M,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 8, 8 },
		.cplanes = 3,
		.mplanes = 3,
		.uv_swap = 0,
		.write_format = MI_CTRL_SP_WRITE_PLA,
		.output_format = MI_CTRL_SP_OUTPUT_YUV444,
	},
	/* yuv400 */
	{
		.fourcc = V4L2_PIX_FMT_GREY,
		.fmt_type = FMT_YUV,
		.bpp = { 8 },
		.cplanes = 1,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_SP_WRITE_PLA,
		.output_format = MI_CTRL_SP_OUTPUT_YUV400,
	},
	/* rgb */
	{
		.fourcc = V4L2_PIX_FMT_XBGR32,
		.fmt_type = FMT_RGB,
		.bpp = { 32 },
		.mplanes = 1,
		.write_format = MI_CTRL_SP_WRITE_PLA,
		.output_format = MI_CTRL_SP_OUTPUT_RGB888,
	}, {
		.fourcc = V4L2_PIX_FMT_RGB565,
		.fmt_type = FMT_RGB,
		.bpp = { 16 },
		.mplanes = 1,
		.write_format = MI_CTRL_SP_WRITE_PLA,
		.output_format = MI_CTRL_SP_OUTPUT_RGB565,
	}
};

static const struct capture_fmt dmatx_fmts[] = {
	/* raw */
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
	},
};

static struct stream_config rkisp_mp_stream_config = {
	.fmts = mp_fmts,
	.fmt_size = ARRAY_SIZE(mp_fmts),
	/* constraints */
	.max_rsz_width = STREAM_MAX_MP_RSZ_OUTPUT_WIDTH,
	.max_rsz_height = STREAM_MAX_MP_RSZ_OUTPUT_HEIGHT,
	.min_rsz_width = STREAM_MIN_RSZ_OUTPUT_WIDTH,
	.min_rsz_height = STREAM_MIN_RSZ_OUTPUT_HEIGHT,
	.frame_end_id = CIF_MI_MP_FRAME,
	/* registers */
	.rsz = {
		.ctrl = CIF_MRSZ_CTRL,
		.scale_hy = CIF_MRSZ_SCALE_HY,
		.scale_hcr = CIF_MRSZ_SCALE_HCR,
		.scale_hcb = CIF_MRSZ_SCALE_HCB,
		.scale_vy = CIF_MRSZ_SCALE_VY,
		.scale_vc = CIF_MRSZ_SCALE_VC,
		.scale_lut = CIF_MRSZ_SCALE_LUT,
		.scale_lut_addr = CIF_MRSZ_SCALE_LUT_ADDR,
		.scale_hy_shd = CIF_MRSZ_SCALE_HY_SHD,
		.scale_hcr_shd = CIF_MRSZ_SCALE_HCR_SHD,
		.scale_hcb_shd = CIF_MRSZ_SCALE_HCB_SHD,
		.scale_vy_shd = CIF_MRSZ_SCALE_VY_SHD,
		.scale_vc_shd = CIF_MRSZ_SCALE_VC_SHD,
		.phase_hy = CIF_MRSZ_PHASE_HY,
		.phase_hc = CIF_MRSZ_PHASE_HC,
		.phase_vy = CIF_MRSZ_PHASE_VY,
		.phase_vc = CIF_MRSZ_PHASE_VC,
		.ctrl_shd = CIF_MRSZ_CTRL_SHD,
		.phase_hy_shd = CIF_MRSZ_PHASE_HY_SHD,
		.phase_hc_shd = CIF_MRSZ_PHASE_HC_SHD,
		.phase_vy_shd = CIF_MRSZ_PHASE_VY_SHD,
		.phase_vc_shd = CIF_MRSZ_PHASE_VC_SHD,
	},
	.dual_crop = {
		.ctrl = CIF_DUAL_CROP_CTRL,
		.yuvmode_mask = CIF_DUAL_CROP_MP_MODE_YUV,
		.rawmode_mask = CIF_DUAL_CROP_MP_MODE_RAW,
		.h_offset = CIF_DUAL_CROP_M_H_OFFS,
		.v_offset = CIF_DUAL_CROP_M_V_OFFS,
		.h_size = CIF_DUAL_CROP_M_H_SIZE,
		.v_size = CIF_DUAL_CROP_M_V_SIZE,
	},
	.mi = {
		.y_size_init = CIF_MI_MP_Y_SIZE_INIT,
		.cb_size_init = CIF_MI_MP_CB_SIZE_INIT,
		.cr_size_init = CIF_MI_MP_CR_SIZE_INIT,
		.y_base_ad_init = CIF_MI_MP_Y_BASE_AD_INIT,
		.cb_base_ad_init = CIF_MI_MP_CB_BASE_AD_INIT,
		.cr_base_ad_init = CIF_MI_MP_CR_BASE_AD_INIT,
		.y_offs_cnt_init = CIF_MI_MP_Y_OFFS_CNT_INIT,
		.cb_offs_cnt_init = CIF_MI_MP_CB_OFFS_CNT_INIT,
		.cr_offs_cnt_init = CIF_MI_MP_CR_OFFS_CNT_INIT,
	},
};

static struct stream_config rkisp_sp_stream_config = {
	.fmts = sp_fmts,
	.fmt_size = ARRAY_SIZE(sp_fmts),
	/* constraints */
	.max_rsz_width = STREAM_MAX_SP_RSZ_OUTPUT_WIDTH,
	.max_rsz_height = STREAM_MAX_SP_RSZ_OUTPUT_HEIGHT,
	.min_rsz_width = STREAM_MIN_RSZ_OUTPUT_WIDTH,
	.min_rsz_height = STREAM_MIN_RSZ_OUTPUT_HEIGHT,
	.frame_end_id = CIF_MI_SP_FRAME,
	/* registers */
	.rsz = {
		.ctrl = CIF_SRSZ_CTRL,
		.scale_hy = CIF_SRSZ_SCALE_HY,
		.scale_hcr = CIF_SRSZ_SCALE_HCR,
		.scale_hcb = CIF_SRSZ_SCALE_HCB,
		.scale_vy = CIF_SRSZ_SCALE_VY,
		.scale_vc = CIF_SRSZ_SCALE_VC,
		.scale_lut = CIF_SRSZ_SCALE_LUT,
		.scale_lut_addr = CIF_SRSZ_SCALE_LUT_ADDR,
		.scale_hy_shd = CIF_SRSZ_SCALE_HY_SHD,
		.scale_hcr_shd = CIF_SRSZ_SCALE_HCR_SHD,
		.scale_hcb_shd = CIF_SRSZ_SCALE_HCB_SHD,
		.scale_vy_shd = CIF_SRSZ_SCALE_VY_SHD,
		.scale_vc_shd = CIF_SRSZ_SCALE_VC_SHD,
		.phase_hy = CIF_SRSZ_PHASE_HY,
		.phase_hc = CIF_SRSZ_PHASE_HC,
		.phase_vy = CIF_SRSZ_PHASE_VY,
		.phase_vc = CIF_SRSZ_PHASE_VC,
		.ctrl_shd = CIF_SRSZ_CTRL_SHD,
		.phase_hy_shd = CIF_SRSZ_PHASE_HY_SHD,
		.phase_hc_shd = CIF_SRSZ_PHASE_HC_SHD,
		.phase_vy_shd = CIF_SRSZ_PHASE_VY_SHD,
		.phase_vc_shd = CIF_SRSZ_PHASE_VC_SHD,
	},
	.dual_crop = {
		.ctrl = CIF_DUAL_CROP_CTRL,
		.yuvmode_mask = CIF_DUAL_CROP_SP_MODE_YUV,
		.rawmode_mask = CIF_DUAL_CROP_SP_MODE_RAW,
		.h_offset = CIF_DUAL_CROP_S_H_OFFS,
		.v_offset = CIF_DUAL_CROP_S_V_OFFS,
		.h_size = CIF_DUAL_CROP_S_H_SIZE,
		.v_size = CIF_DUAL_CROP_S_V_SIZE,
	},
	.mi = {
		.y_size_init = CIF_MI_SP_Y_SIZE_INIT,
		.cb_size_init = CIF_MI_SP_CB_SIZE_INIT,
		.cr_size_init = CIF_MI_SP_CR_SIZE_INIT,
		.y_base_ad_init = CIF_MI_SP_Y_BASE_AD_INIT,
		.cb_base_ad_init = CIF_MI_SP_CB_BASE_AD_INIT,
		.cr_base_ad_init = CIF_MI_SP_CR_BASE_AD_INIT,
		.y_offs_cnt_init = CIF_MI_SP_Y_OFFS_CNT_INIT,
		.cb_offs_cnt_init = CIF_MI_SP_CB_OFFS_CNT_INIT,
		.cr_offs_cnt_init = CIF_MI_SP_CR_OFFS_CNT_INIT,
	},
};

static struct stream_config rkisp1_dmatx0_stream_config = {
	.fmts = dmatx_fmts,
	.fmt_size = ARRAY_SIZE(dmatx_fmts),
};

static struct stream_config rkisp2_dmatx0_stream_config = {
	.fmts = dmatx_fmts,
	.fmt_size = ARRAY_SIZE(dmatx_fmts),
	.frame_end_id = MI_RAW0_WR_FRAME,
	.mi = {
		.y_size_init = MI_RAW0_WR_SIZE,
		.y_base_ad_init = MI_RAW0_WR_BASE,
		.y_base_ad_shd = MI_RAW0_WR_BASE_SHD,
	},
	.dma = {
		.ctrl = CSI2RX_RAW0_WR_CTRL,
		.pic_size = CSI2RX_RAW0_WR_PIC_SIZE,
		.pic_offs = CSI2RX_RAW0_WR_PIC_OFF,
	},
};

static struct stream_config rkisp2_dmatx1_stream_config = {
	.fmts = dmatx_fmts,
	.fmt_size = ARRAY_SIZE(dmatx_fmts),
	.frame_end_id = MI_RAW1_WR_FRAME,
	.mi = {
		.y_size_init = MI_RAW1_WR_SIZE,
		.y_base_ad_init = MI_RAW1_WR_BASE,
		.y_base_ad_shd = MI_RAW1_WR_BASE_SHD,
	},
	.dma = {
		.ctrl = CSI2RX_RAW1_WR_CTRL,
		.pic_size = CSI2RX_RAW1_WR_PIC_SIZE,
		.pic_offs = CSI2RX_RAW1_WR_PIC_OFF,
	},
};

static struct stream_config rkisp2_dmatx2_stream_config = {
	.fmts = dmatx_fmts,
	.fmt_size = ARRAY_SIZE(dmatx_fmts),
	.frame_end_id = MI_RAW2_WR_FRAME,
	.mi = {
		.y_size_init = MI_RAW2_WR_SIZE,
		.y_base_ad_init = MI_RAW2_WR_BASE,
		.y_base_ad_shd = MI_RAW2_WR_BASE_SHD,
	},
	.dma = {
		.ctrl = CSI2RX_RAW2_WR_CTRL,
		.pic_size = CSI2RX_RAW2_WR_PIC_SIZE,
		.pic_offs = CSI2RX_RAW2_WR_PIC_OFF,
	},
};

static struct stream_config rkisp2_dmatx3_stream_config = {
	.fmts = dmatx_fmts,
	.fmt_size = ARRAY_SIZE(dmatx_fmts),
	.frame_end_id = MI_RAW3_WR_FRAME,
	.mi = {
		.y_size_init = MI_RAW3_WR_SIZE,
		.y_base_ad_init = MI_RAW3_WR_BASE,
		.y_base_ad_shd = MI_RAW3_WR_BASE_SHD,
	},
	.dma = {
		.ctrl = CSI2RX_RAW3_WR_CTRL,
		.pic_size = CSI2RX_RAW3_WR_PIC_SIZE,
		.pic_offs = CSI2RX_RAW3_WR_PIC_OFF,
	},
};

static int hdr_dma_frame(struct rkisp_device *dev)
{
	int max_dma;

	switch (dev->hdr.op_mode) {
	case HDR_FRAMEX2_DDR:
	case HDR_LINEX2_DDR:
	case HDR_RDBK_FRAME1:
		max_dma = 1;
		break;
	case HDR_FRAMEX3_DDR:
	case HDR_LINEX3_DDR:
	case HDR_RDBK_FRAME2:
		max_dma = 2;
		break;
	case HDR_RDBK_FRAME3:
		max_dma = HDR_DMA_MAX;
		break;
	case HDR_LINEX2_NO_DDR:
	case HDR_NORMAL:
	default:
		max_dma = 0;
	}
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s %d\n", __func__, max_dma);
	return max_dma;
}

static const
struct capture_fmt *find_fmt(struct rkisp_stream *stream, const u32 pixelfmt)
{
	const struct capture_fmt *fmt;
	int i;

	for (i = 0; i < stream->config->fmt_size; i++) {
		fmt = &stream->config->fmts[i];
		if (fmt->fourcc == pixelfmt)
			return fmt;
	}
	return NULL;
}

/* configure dual-crop unit */
static int rkisp_config_dcrop(struct rkisp_stream *stream, bool async)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_rect *dcrop = &stream->dcrop;
	struct v4l2_rect *input_win;

	/* dual-crop unit get data from isp */
	input_win = rkisp_get_isp_sd_win(&dev->isp_sdev);

	if (dcrop->width == input_win->width &&
	    dcrop->height == input_win->height &&
	    dcrop->left == 0 && dcrop->top == 0) {
		disable_dcrop(stream, async);
		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "stream %d crop disabled\n", stream->id);
		return 0;
	}

	config_dcrop(stream, dcrop, async);

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "stream %d crop: %dx%d -> %dx%d\n", stream->id,
		 input_win->width, input_win->height,
		 dcrop->width, dcrop->height);

	return 0;
}

/* configure scale unit */
static int rkisp_config_rsz(struct rkisp_stream *stream, bool async)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_pix_format_mplane output_fmt = stream->out_fmt;
	struct capture_fmt *output_isp_fmt = &stream->out_isp_fmt;
	struct ispsd_out_fmt *input_isp_fmt =
			rkisp_get_ispsd_out_fmt(&dev->isp_sdev);
	struct v4l2_rect in_y, in_c, out_y, out_c;
	u32 xsubs_in = 1, ysubs_in = 1;
	u32 xsubs_out = 1, ysubs_out = 1;

	if (input_isp_fmt->fmt_type == FMT_BAYER ||
	    dev->br_dev.en)
		goto disable;

	/* set input and output sizes for scale calculation */
	in_y.width = stream->dcrop.width;
	in_y.height = stream->dcrop.height;
	out_y.width = output_fmt.width;
	out_y.height = output_fmt.height;

	/* The size of Cb,Cr are related to the format */
	if (mbus_code_xysubs(input_isp_fmt->mbus_code, &xsubs_in, &ysubs_in)) {
		v4l2_err(&dev->v4l2_dev, "Not xsubs/ysubs found\n");
		return -EINVAL;
	}
	in_c.width = in_y.width / xsubs_in;
	in_c.height = in_y.height / ysubs_in;

	if (output_isp_fmt->fmt_type == FMT_YUV) {
		fcc_xysubs(output_isp_fmt->fourcc, &xsubs_out, &ysubs_out);
		out_c.width = out_y.width / xsubs_out;
		out_c.height = out_y.height / ysubs_out;
	} else {
		out_c.width = out_y.width / xsubs_in;
		out_c.height = out_y.height / ysubs_in;
	}

	if (in_c.width == out_c.width && in_c.height == out_c.height)
		goto disable;

	/* set RSZ input and output */
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "stream %d rsz/scale: %dx%d -> %dx%d\n",
		 stream->id, stream->dcrop.width, stream->dcrop.height,
		 output_fmt.width, output_fmt.height);
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "chroma scaling %dx%d -> %dx%d\n",
		 in_c.width, in_c.height, out_c.width, out_c.height);

	/* calculate and set scale */
	config_rsz(stream, &in_y, &in_c, &out_y, &out_c, async);

	if (rkisp_debug)
		dump_rsz_regs(stream);

	return 0;

disable:
	disable_rsz(stream, async);

	return 0;
}

/***************************** stream operations*******************************/

/*
 * memory base addresses should be with respect
 * to the burst alignment restriction for AXI.
 */
static u32 calc_burst_len(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	u32 y_size = stream->out_fmt.plane_fmt[0].bytesperline *
		stream->out_fmt.height;
	u32 cb_size = stream->out_fmt.plane_fmt[1].sizeimage;
	u32 cr_size = stream->out_fmt.plane_fmt[2].sizeimage;
	u32 cb_offs, cr_offs;
	u32 bus, burst;
	int i;

	/* MI128bit and MI64bit */
	bus = 8;
	if (dev->isp_ver == ISP_V12 ||
	    dev->isp_ver == ISP_V13 ||
	    dev->isp_ver == ISP_V20)
		bus = 16;

	/* y/c base addr: burstN * bus alignment */
	cb_offs = y_size;
	cr_offs = cr_size ? (cb_size + cb_offs) : 0;

	if (!(cb_offs % (bus * 16)) &&
		!(cr_offs % (bus * 16)))
		burst = CIF_MI_CTRL_BURST_LEN_LUM_16 |
			CIF_MI_CTRL_BURST_LEN_CHROM_16;
	else if (!(cb_offs % (bus * 8)) &&
		!(cr_offs % (bus * 8)))
		burst = CIF_MI_CTRL_BURST_LEN_LUM_8 |
			CIF_MI_CTRL_BURST_LEN_CHROM_8;
	else
		burst = CIF_MI_CTRL_BURST_LEN_LUM_4 |
			CIF_MI_CTRL_BURST_LEN_CHROM_4;

	if (cb_offs % (bus * 4) ||
		cr_offs % (bus * 4))
		v4l2_warn(&dev->v4l2_dev,
			"%dx%d fmt:0x%x not support, should be %d aligned\n",
			stream->out_fmt.width,
			stream->out_fmt.height,
			stream->out_fmt.pixelformat,
			(cr_offs == 0) ? bus * 4 : bus * 16);

	stream->burst = burst;
	for (i = 0; i < RKISP_MAX_STREAM; i++)
		if (burst > dev->cap_dev.stream[i].burst)
			burst = dev->cap_dev.stream[i].burst;

	if (stream->interlaced) {
		if (!stream->out_fmt.width % (bus * 16))
			stream->burst = CIF_MI_CTRL_BURST_LEN_LUM_16 |
				CIF_MI_CTRL_BURST_LEN_CHROM_16;
		else if (!stream->out_fmt.width % (bus * 8))
			stream->burst = CIF_MI_CTRL_BURST_LEN_LUM_8 |
				CIF_MI_CTRL_BURST_LEN_CHROM_8;
		else
			stream->burst = CIF_MI_CTRL_BURST_LEN_LUM_4 |
				CIF_MI_CTRL_BURST_LEN_CHROM_4;
		if (stream->out_fmt.width % (bus * 4))
			v4l2_warn(&dev->v4l2_dev,
				"interlaced: width should be %d aligned\n",
				bus * 4);
		burst = min(stream->burst, burst);
		stream->burst = burst;
	}

	return burst;
}

/*
 * configure memory interface for mainpath
 * This should only be called when stream-on
 */
static int mp_config_mi(struct rkisp_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;

       /*
	* NOTE: plane_fmt[0].sizeimage is total size of all planes for single
	* memory plane formats, so calculate the size explicitly.
	*/
	mi_set_y_size(stream, stream->out_fmt.plane_fmt[0].bytesperline *
			 stream->out_fmt.height);
	mi_set_cb_size(stream, stream->out_fmt.plane_fmt[1].sizeimage);
	mi_set_cr_size(stream, stream->out_fmt.plane_fmt[2].sizeimage);
	mi_frame_end_int_enable(stream);
	if (stream->out_isp_fmt.uv_swap)
		mp_set_uv_swap(base);

	config_mi_ctrl(stream, calc_burst_len(stream));
	mp_mi_ctrl_set_format(base, stream->out_isp_fmt.write_format);
	mp_mi_ctrl_autoupdate_en(base);

	/* set up first buffer */
	mi_frame_end(stream);
	return 0;
}

/*
 * configure memory interface for selfpath
 * This should only be called when stream-on
 */
static int sp_config_mi(struct rkisp_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;
	struct rkisp_device *dev = stream->ispdev;
	struct capture_fmt *output_isp_fmt = &stream->out_isp_fmt;
	struct ispsd_out_fmt *input_isp_fmt =
			rkisp_get_ispsd_out_fmt(&dev->isp_sdev);
	u32 sp_in_fmt;

	if (mbus_code_sp_in_fmt(input_isp_fmt->mbus_code,
				output_isp_fmt->fourcc, &sp_in_fmt)) {
		v4l2_err(&dev->v4l2_dev, "Can't find the input format\n");
		return -EINVAL;
	}

       /*
	* NOTE: plane_fmt[0].sizeimage is total size of all planes for single
	* memory plane formats, so calculate the size explicitly.
	*/
	mi_set_y_size(stream, stream->out_fmt.plane_fmt[0].bytesperline *
		      stream->out_fmt.height);
	mi_set_cb_size(stream, stream->out_fmt.plane_fmt[1].sizeimage);
	mi_set_cr_size(stream, stream->out_fmt.plane_fmt[2].sizeimage);

	sp_set_y_width(base, stream->out_fmt.width);
	if (stream->interlaced) {
		stream->u.sp.vir_offs =
			stream->out_fmt.plane_fmt[0].bytesperline;
		sp_set_y_height(base, stream->out_fmt.height / 2);
		sp_set_y_line_length(base, stream->u.sp.y_stride * 2);
	} else {
		sp_set_y_height(base, stream->out_fmt.height);
		sp_set_y_line_length(base, stream->u.sp.y_stride);
	}

	mi_frame_end_int_enable(stream);
	if (output_isp_fmt->uv_swap)
		sp_set_uv_swap(base);

	config_mi_ctrl(stream, calc_burst_len(stream));
	sp_mi_ctrl_set_format(base, stream->out_isp_fmt.write_format |
			      sp_in_fmt | output_isp_fmt->output_format);

	sp_mi_ctrl_autoupdate_en(base);

	/* set up first buffer */
	mi_frame_end(stream);
	return 0;
}

static int dmatx3_config_mi(struct rkisp_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_csi_device *csi = &dev->csi_dev;
	u32 in_size;
	u8 vc;

	if (!csi->sink[CSI_SRC_CH4 - 1].linked ||
	    stream->streaming)
		return -EBUSY;

	if (!dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2)) {
		v4l2_err(&dev->v4l2_dev,
			 "only mipi sensor support rawwr3\n");
		return -EINVAL;
	}
	atomic_set(&stream->sequence, 0);
	in_size = stream->out_fmt.plane_fmt[0].sizeimage;
	raw_wr_set_pic_size(stream,
			    stream->out_fmt.width,
			    stream->out_fmt.height);
	raw_wr_set_pic_offs(stream, 0);

	vc = csi->sink[CSI_SRC_CH4 - 1].index;
	raw_wr_ctrl(stream,
		SW_CSI_RAW_WR_CH_EN(vc) |
		csi->memory |
		SW_CSI_RAW_WR_EN_ORG);
	mi_set_y_size(stream, in_size);
	mi_frame_end(stream);
	mi_frame_end_int_enable(stream);
	mi_wr_ctrl2(base, SW_RAW3_WR_AUTOUPD);

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "rawwr3 %dx%d ctrl:0x%x\n",
		 stream->out_fmt.width,
		 stream->out_fmt.height,
		 readl(base + CSI2RX_RAW3_WR_CTRL));
	return 0;
}

static int dmatx2_config_mi(struct rkisp_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_csi_device *csi = &dev->csi_dev;
	u32 val, in_size;
	u8 vc;

	if (!csi->sink[CSI_SRC_CH3 - 1].linked ||
	    stream->streaming)
		return -EBUSY;

	if (!dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2)) {
		v4l2_err(&dev->v4l2_dev,
			 "only mipi sensor support rawwr2 path\n");
		return -EINVAL;
	}

	if (!stream->u.dmatx.is_config) {
		atomic_set(&stream->sequence, 0);
		in_size = stream->out_fmt.plane_fmt[0].sizeimage;
		raw_wr_set_pic_size(stream,
				    stream->out_fmt.width,
				    stream->out_fmt.height);
		raw_wr_set_pic_offs(stream, 0);
		raw_rd_set_pic_size(base,
				    stream->out_fmt.width,
				    stream->out_fmt.height);
		vc = csi->sink[CSI_SRC_CH3 - 1].index;
		val = SW_CSI_RAW_WR_CH_EN(vc);
		val |= csi->memory;
		if (dev->hdr.op_mode != HDR_NORMAL)
			val |= SW_CSI_RAW_WR_EN_ORG;
		raw_wr_ctrl(stream, val);
		mi_set_y_size(stream, in_size);
		mi_frame_end(stream);
		mi_frame_end_int_enable(stream);
		mi_wr_ctrl2(base, SW_RAW2_WR_AUTOUPD);
		stream->u.dmatx.is_config = true;
	}
	return 0;
}

static int dmatx1_config_mi(struct rkisp_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_csi_device *csi = &dev->csi_dev;
	u32 val, in_size;
	u8 vc;

	if (!csi->sink[CSI_SRC_CH2 - 1].linked ||
	    stream->streaming)
		return -EBUSY;

	if (!dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2)) {
		if (stream->id == RKISP_STREAM_DMATX1)
			v4l2_err(&dev->v4l2_dev,
				 "only mipi sensor support dmatx1 path\n");
		return -EINVAL;
	}

	if (!stream->u.dmatx.is_config) {
		atomic_set(&stream->sequence, 0);
		in_size = stream->out_fmt.plane_fmt[0].sizeimage;
		raw_wr_set_pic_size(stream,
				    stream->out_fmt.width,
				    stream->out_fmt.height);
		raw_wr_set_pic_offs(stream, 0);
		vc = csi->sink[CSI_SRC_CH2 - 1].index;
		val = SW_CSI_RAW_WR_CH_EN(vc);
		val |= csi->memory;
		if (dev->hdr.op_mode != HDR_NORMAL)
			val |= SW_CSI_RAW_WR_EN_ORG;
		raw_wr_ctrl(stream, val);
		mi_set_y_size(stream, in_size);
		mi_frame_end(stream);
		mi_frame_end_int_enable(stream);
		mi_wr_ctrl2(base, SW_RAW1_WR_AUTOUPD);
		stream->u.dmatx.is_config = true;
	}
	return 0;
}

static int dmatx0_config_mi(struct rkisp_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_csi_device *csi = &dev->csi_dev;
	struct rkisp_stream *dmatx =
		&dev->cap_dev.stream[RKISP_STREAM_DMATX0];
	u32 val, in_size;
	u8 vc;

	if (!csi->sink[CSI_SRC_CH1 - 1].linked ||
	    dmatx->streaming)
		return -EBUSY;

	if (!dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2)) {
		if (stream->id == RKISP_STREAM_DMATX0)
			v4l2_err(&dev->v4l2_dev,
				 "only mipi sensor support rawwr0 path\n");
		return -EINVAL;
	}

	/* dmatx0 output size equal to sensor input size */
	in_size = dmatx->out_fmt.plane_fmt[0].sizeimage;
	if (dev->isp_ver == ISP_V20) {
		if (dmatx->u.dmatx.is_config)
			return 0;
		atomic_set(&dmatx->sequence, 0);
		raw_wr_set_pic_size(dmatx,
				    dmatx->out_fmt.width,
				    dmatx->out_fmt.height);
		raw_wr_set_pic_offs(dmatx, 0);
		vc = csi->sink[CSI_SRC_CH1 - 1].index;
		val = SW_CSI_RAW_WR_CH_EN(vc);
		val |= csi->memory;
		if (dev->hdr.op_mode != HDR_NORMAL)
			val |= SW_CSI_RAW_WR_EN_ORG;
		raw_wr_ctrl(dmatx, val);
		mi_set_y_size(dmatx, in_size);
		mi_frame_end(dmatx);
		mi_frame_end_int_enable(dmatx);
		mi_wr_ctrl2(base, SW_RAW0_WR_AUTOUPD);
		dmatx->u.dmatx.is_config = true;
	} else {
		dmatx0_set_pic_size(base,
				    dmatx->out_fmt.width,
				    dmatx->out_fmt.height);
		dmatx0_set_pic_off(base, 0);
		dmatx0_ctrl(base,
			    CIF_ISP_CSI0_DMATX0_VC(2) |
			    CIF_ISP_CSI0_DMATX0_SIMG_SWP |
			    CIF_ISP_CSI0_DMATX0_SIMG_MODE);
		mi_raw0_set_size(base, in_size);
		mi_raw0_set_offs(base, 0);
		mi_raw0_set_length(base, 0);
		mi_raw0_set_irq_offs(base, 0);
		/* dummy buf for dmatx0 first address shadow */
		mi_raw0_set_addr(base, stream->dummy_buf.dma_addr);
		mi_ctrl2(base, CIF_MI_CTRL2_MIPI_RAW0_AUTO_UPDATE);
		if (stream->id == RKISP_STREAM_DMATX0)
			stream->u.dmatx.pre_stop = false;

		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "rawwr0 %dx%d size:%d ctrl:0x%x\n",
			 dmatx->out_fmt.width,
			 dmatx->out_fmt.height, in_size,
			 readl(base + CIF_ISP_CSI0_DMATX0_CTRL));
	}

	return 0;
}

static void mp_enable_mi(struct rkisp_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;
	struct capture_fmt *isp_fmt = &stream->out_isp_fmt;

	mi_ctrl_mp_disable(base);
	if (isp_fmt->fmt_type == FMT_BAYER)
		mi_ctrl_mpraw_enable(base);
	else if (isp_fmt->fmt_type == FMT_YUV)
		mi_ctrl_mpyuv_enable(base);
}

static void sp_enable_mi(struct rkisp_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;

	mi_ctrl_spyuv_enable(base);
}

static void dmatx_enable_mi(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	void __iomem *base = dev->base_addr;

	if (dev->isp_ver == ISP_V20)
		raw_wr_enable(stream);
	else
		mi_mipi_raw0_enable(base);
}

static void mp_disable_mi(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	void __iomem *base = dev->base_addr;

	mi_ctrl_mp_disable(base);
	if (dev->isp_ver == ISP_V20)
		hdr_stop_dmatx(dev);
}

static void sp_disable_mi(struct rkisp_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;

	mi_ctrl_spyuv_disable(base);
}

static void update_dmatx_v1(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	void __iomem *base = dev->base_addr;

	if (stream->next_buf)
		mi_raw0_set_addr(base,
			stream->next_buf->buff_addr[RKISP_PLANE_Y]);
	else
		mi_raw0_set_addr(base, stream->dummy_buf.dma_addr);
	v4l2_dbg(2, rkisp_debug, &dev->v4l2_dev,
		 "%s stream:%d Y:0x%x SHD:0x%x\n",
		 __func__, stream->id,
		 readl(base + CIF_MI_RAW0_BASE_AD_INIT),
		 readl(base + CIF_MI_RAW0_BASE_AS_SHD));
}

static void update_dmatx_v2(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	void __iomem *base = dev->base_addr;
	struct rkisp_dummy_buffer *buf = NULL;
	u8 index;

	if (stream->next_buf) {
		mi_set_y_addr(stream,
			      stream->next_buf->buff_addr[RKISP_PLANE_Y]);
	} else {
		if (stream->id == RKISP_STREAM_DMATX0)
			index = dev->hdr.index[HDR_DMA0];
		else if (stream->id == RKISP_STREAM_DMATX1)
			index = dev->hdr.index[HDR_DMA1];
		else if (stream->id == RKISP_STREAM_DMATX2)
			index = dev->hdr.index[HDR_DMA2];

		if ((stream->id == RKISP_STREAM_DMATX0 ||
		     stream->id == RKISP_STREAM_DMATX1 ||
		     stream->id == RKISP_STREAM_DMATX2)) {
			buf = hdr_dqbuf(&dev->hdr.q_tx[index]);
			if (IS_HDR_RDBK(dev->hdr.op_mode) &&
			    !dev->dmarx_dev.trigger)
				hdr_qbuf(&dev->hdr.q_rx[index], buf);
			else
				hdr_qbuf(&dev->hdr.q_tx[index], buf);
		}
		if (!buf && stream->dummy_buf.mem_priv)
			buf = &stream->dummy_buf;
		if (buf)
			mi_set_y_addr(stream, buf->dma_addr);
	}
	v4l2_dbg(2, rkisp_debug, &dev->v4l2_dev,
		 "%s stream:%d Y:0x%x SHD:0x%x\n",
		 __func__, stream->id,
		 readl(base + stream->config->mi.y_base_ad_init),
		 readl(base + stream->config->mi.y_base_ad_shd));
}

/* Update buffer info to memory interface, it's called in interrupt */
static void update_mi(struct rkisp_stream *stream)
{
	struct rkisp_dummy_buffer *dummy_buf = &stream->dummy_buf;
	void __iomem *base = stream->ispdev->base_addr;

	/* The dummy space allocated by dma_alloc_coherent is used, we can
	 * throw data to it if there is no available buffer.
	 */
	if (stream->next_buf) {
		mi_set_y_addr(stream,
			stream->next_buf->buff_addr[RKISP_PLANE_Y]);
		mi_set_cb_addr(stream,
			stream->next_buf->buff_addr[RKISP_PLANE_CB]);
		mi_set_cr_addr(stream,
			stream->next_buf->buff_addr[RKISP_PLANE_CR]);
	} else {
		mi_set_y_addr(stream, dummy_buf->dma_addr);
		mi_set_cb_addr(stream, dummy_buf->dma_addr);
		mi_set_cr_addr(stream, dummy_buf->dma_addr);
	}

	mi_set_y_offset(stream, 0);
	mi_set_cb_offset(stream, 0);
	mi_set_cr_offset(stream, 0);
	v4l2_dbg(2, rkisp_debug, &stream->ispdev->v4l2_dev,
		 "%s stream:%d Y:0x%x CB:0x%x CR:0x%x\n",
		 __func__, stream->id,
		 readl(base + stream->config->mi.y_base_ad_init),
		 readl(base + stream->config->mi.cb_base_ad_init),
		 readl(base + stream->config->mi.cr_base_ad_init));
}

static void mp_stop_mi(struct rkisp_stream *stream)
{
	if (!stream->streaming)
		return;
	mi_frame_end_int_clear(stream);
	stream->ops->disable_mi(stream);
}

static void sp_stop_mi(struct rkisp_stream *stream)
{
	if (!stream->streaming)
		return;
	mi_frame_end_int_clear(stream);
	stream->ops->disable_mi(stream);
}

static void dmatx_stop_mi(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	void __iomem *base = dev->base_addr;

	if (dev->isp_ver == ISP_V20)
		raw_wr_disable(stream);
	else
		mi_mipi_raw0_disable(base);
	stream->u.dmatx.is_config = false;
}

static struct streams_ops rkisp_mp_streams_ops = {
	.config_mi = mp_config_mi,
	.enable_mi = mp_enable_mi,
	.disable_mi = mp_disable_mi,
	.stop_mi = mp_stop_mi,
	.set_data_path = mp_set_data_path,
	.is_stream_stopped = mp_is_stream_stopped,
	.update_mi = update_mi,
};

static struct streams_ops rkisp_sp_streams_ops = {
	.config_mi = sp_config_mi,
	.enable_mi = sp_enable_mi,
	.disable_mi = sp_disable_mi,
	.stop_mi = sp_stop_mi,
	.set_data_path = sp_set_data_path,
	.is_stream_stopped = sp_is_stream_stopped,
	.update_mi = update_mi,
};

static struct streams_ops rkisp1_dmatx0_streams_ops = {
	.config_mi = dmatx0_config_mi,
	.enable_mi = dmatx_enable_mi,
	.stop_mi = dmatx_stop_mi,
	.update_mi = update_dmatx_v1,
};

static struct streams_ops rkisp2_dmatx0_streams_ops = {
	.config_mi = dmatx0_config_mi,
	.enable_mi = dmatx_enable_mi,
	.stop_mi = dmatx_stop_mi,
	.is_stream_stopped = dmatx0_is_stream_stopped,
	.update_mi = update_dmatx_v2,
};

static struct streams_ops rkisp2_dmatx1_streams_ops = {
	.config_mi = dmatx1_config_mi,
	.enable_mi = dmatx_enable_mi,
	.stop_mi = dmatx_stop_mi,
	.is_stream_stopped = dmatx1_is_stream_stopped,
	.update_mi = update_dmatx_v2,
};

static struct streams_ops rkisp2_dmatx2_streams_ops = {
	.config_mi = dmatx2_config_mi,
	.enable_mi = dmatx_enable_mi,
	.stop_mi = dmatx_stop_mi,
	.is_stream_stopped = dmatx2_is_stream_stopped,
	.update_mi = update_dmatx_v2,
};

static struct streams_ops rkisp2_dmatx3_streams_ops = {
	.config_mi = dmatx3_config_mi,
	.enable_mi = dmatx_enable_mi,
	.stop_mi = dmatx_stop_mi,
	.is_stream_stopped = dmatx3_is_stream_stopped,
	.update_mi = update_dmatx_v2,
};

static void rdbk_frame_end(struct rkisp_stream *stream)
{
	struct rkisp_device *isp_dev = stream->ispdev;
	struct rkisp_capture_device *cap = &isp_dev->cap_dev;
	struct rkisp_sensor_info *sensor = isp_dev->active_sensor;
	u32 denominator = sensor->fi.interval.denominator;
	u32 numerator = sensor->fi.interval.numerator;
	u64 l_ts, m_ts, s_ts;
	int ret, max_dma, fps = -1, time = 30000000;

	if (stream->id != RKISP_STREAM_DMATX2)
		return;

	if (denominator && numerator)
		time = numerator * 1000 / denominator * 1000 * 1000;

	max_dma = hdr_dma_frame(isp_dev);
	if (max_dma == 3) {
		if (cap->rdbk_buf[RDBK_L] && cap->rdbk_buf[RDBK_M] &&
		    cap->rdbk_buf[RDBK_S]) {
			l_ts = cap->rdbk_buf[RDBK_L]->vb.vb2_buf.timestamp;
			m_ts = cap->rdbk_buf[RDBK_M]->vb.vb2_buf.timestamp;
			s_ts = cap->rdbk_buf[RDBK_S]->vb.vb2_buf.timestamp;

			if ((m_ts - l_ts) > time || (s_ts - m_ts) > time) {
				ret = v4l2_subdev_call(sensor->sd,
					video, g_frame_interval, &sensor->fi);
				if (!ret) {
					denominator = sensor->fi.interval.denominator;
					numerator = sensor->fi.interval.numerator;
					time = numerator * 1000 / denominator * 1000 * 1000;
					if (numerator)
						fps = denominator / numerator;
				}
				if ((m_ts - l_ts) > time || (s_ts - m_ts) > time) {
					v4l2_err(&isp_dev->v4l2_dev,
						 "timestamp no match, s:%lld m:%lld l:%lld, fps:%d\n",
						 s_ts, m_ts, l_ts, fps);
					goto RDBK_FRM_UNMATCH;
				}
			}

			if (m_ts < l_ts || s_ts < m_ts) {
				v4l2_err(&isp_dev->v4l2_dev,
					 "s/m/l frame err, timestamp s:%lld m:%lld l:%lld\n",
					 s_ts, m_ts, l_ts);
				goto RDBK_FRM_UNMATCH;
			}

			cap->rdbk_buf[RDBK_S]->vb.sequence =
				cap->rdbk_buf[RDBK_L]->vb.sequence;
			cap->rdbk_buf[RDBK_M]->vb.sequence =
				cap->rdbk_buf[RDBK_L]->vb.sequence;
			vb2_buffer_done(&cap->rdbk_buf[RDBK_L]->vb.vb2_buf,
				VB2_BUF_STATE_DONE);
			vb2_buffer_done(&cap->rdbk_buf[RDBK_M]->vb.vb2_buf,
				VB2_BUF_STATE_DONE);
			vb2_buffer_done(&cap->rdbk_buf[RDBK_S]->vb.vb2_buf,
				VB2_BUF_STATE_DONE);
		} else {
			v4l2_err(&isp_dev->v4l2_dev, "lost long or middle frames\n");
			goto RDBK_FRM_UNMATCH;
		}
	} else if (max_dma == 2) {
		if (cap->rdbk_buf[RDBK_L] && cap->rdbk_buf[RDBK_S]) {
			l_ts = cap->rdbk_buf[RDBK_L]->vb.vb2_buf.timestamp;
			s_ts = cap->rdbk_buf[RDBK_S]->vb.vb2_buf.timestamp;

			if ((s_ts - l_ts) > time) {
				ret = v4l2_subdev_call(sensor->sd,
					video, g_frame_interval, &sensor->fi);
				if (!ret) {
					denominator = sensor->fi.interval.denominator;
					numerator = sensor->fi.interval.numerator;
					time = numerator * 1000 / denominator * 1000 * 1000;
					if (numerator)
						fps = denominator / numerator;
				}
				if ((s_ts - l_ts) > time) {
					v4l2_err(&isp_dev->v4l2_dev,
						 "timestamp no match, s:%lld l:%lld, fps:%d\n",
						 s_ts, l_ts, fps);
					goto RDBK_FRM_UNMATCH;
				}
			}

			if (s_ts < l_ts) {
				v4l2_err(&isp_dev->v4l2_dev,
					 "s/l frame err, timestamp s:%lld l:%lld\n",
					 s_ts, l_ts);
				goto RDBK_FRM_UNMATCH;
			}

			cap->rdbk_buf[RDBK_S]->vb.sequence =
				cap->rdbk_buf[RDBK_L]->vb.sequence;
			vb2_buffer_done(&cap->rdbk_buf[RDBK_L]->vb.vb2_buf,
				VB2_BUF_STATE_DONE);
			vb2_buffer_done(&cap->rdbk_buf[RDBK_S]->vb.vb2_buf,
				VB2_BUF_STATE_DONE);
		} else {
			v4l2_err(&isp_dev->v4l2_dev, "lost long frames\n");
			goto RDBK_FRM_UNMATCH;
		}
	} else {
		vb2_buffer_done(&cap->rdbk_buf[RDBK_S]->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

	cap->rdbk_buf[RDBK_L] = NULL;
	cap->rdbk_buf[RDBK_M] = NULL;
	cap->rdbk_buf[RDBK_S] = NULL;
	return;

RDBK_FRM_UNMATCH:
	if (cap->rdbk_buf[RDBK_L])
		rkisp_buf_queue(&cap->rdbk_buf[RDBK_L]->vb.vb2_buf);
	if (cap->rdbk_buf[RDBK_M])
		rkisp_buf_queue(&cap->rdbk_buf[RDBK_M]->vb.vb2_buf);
	if (cap->rdbk_buf[RDBK_S])
		rkisp_buf_queue(&cap->rdbk_buf[RDBK_S]->vb.vb2_buf);

	cap->rdbk_buf[RDBK_L] = NULL;
	cap->rdbk_buf[RDBK_M] = NULL;
	cap->rdbk_buf[RDBK_S] = NULL;
}

/*
 * This function is called when a frame end come. The next frame
 * is processing and we should set up buffer for next-next frame,
 * otherwise it will overflow.
 */
static int mi_frame_end(struct rkisp_stream *stream)
{
	struct rkisp_device *isp_dev = stream->ispdev;
	struct rkisp_capture_device *cap = &isp_dev->cap_dev;
	struct capture_fmt *isp_fmt = &stream->out_isp_fmt;
	bool interlaced = stream->interlaced;
	unsigned long lock_flags = 0;
	int i = 0;

	if (!stream->next_buf && stream->streaming &&
	    IS_HDR_RDBK(isp_dev->hdr.op_mode) &&
	    isp_dev->dmarx_dev.trigger == T_MANUAL &&
	    (stream->id == RKISP_STREAM_DMATX0 ||
	     stream->id == RKISP_STREAM_DMATX1 ||
	     stream->id == RKISP_STREAM_DMATX2))
		v4l2_info(&isp_dev->v4l2_dev,
			  "tx stream:%d lose frame:%d, isp state:0x%x frame:%d\n",
			  stream->id, atomic_read(&stream->sequence) - 1,
			  isp_dev->isp_state, isp_dev->dmarx_dev.cur_frame.id);

	if (stream->curr_buf &&
		(!interlaced ||
		(stream->u.sp.field_rec == RKISP_FIELD_ODD &&
		stream->u.sp.field == RKISP_FIELD_EVEN))) {
		u64 ns = 0;

		/* Dequeue a filled buffer */
		for (i = 0; i < isp_fmt->mplanes; i++) {
			u32 payload_size =
				stream->out_fmt.plane_fmt[i].sizeimage;
			vb2_set_plane_payload(
				&stream->curr_buf->vb.vb2_buf, i,
				payload_size);
		}
		if (stream->id == RKISP_STREAM_MP ||
		    stream->id == RKISP_STREAM_SP)
			rkisp_dmarx_get_frame(isp_dev,
					      &stream->curr_buf->vb.sequence,
					      &ns, false);
		else
			stream->curr_buf->vb.sequence =
				atomic_read(&stream->sequence) - 1;
		if (!ns)
			ns = ktime_get_ns();
		stream->curr_buf->vb.vb2_buf.timestamp = ns;

		if (!IS_HDR_RDBK(isp_dev->hdr.op_mode)) {
			vb2_buffer_done(&stream->curr_buf->vb.vb2_buf,
					VB2_BUF_STATE_DONE);
		} else {
			if (stream->id == RKISP_STREAM_DMATX0) {
				if (cap->rdbk_buf[RDBK_L]) {
					v4l2_err(&isp_dev->v4l2_dev,
						"multiple long data in hdr frame\n");
					rkisp_buf_queue(&cap->rdbk_buf[RDBK_L]->vb.vb2_buf);
				}
				cap->rdbk_buf[RDBK_L] = stream->curr_buf;
			} else if (stream->id == RKISP_STREAM_DMATX1) {
				if (cap->rdbk_buf[RDBK_M]) {
					v4l2_err(&isp_dev->v4l2_dev,
						"multiple middle data in hdr frame\n");
					rkisp_buf_queue(&cap->rdbk_buf[RDBK_M]->vb.vb2_buf);
				}
				cap->rdbk_buf[RDBK_M] = stream->curr_buf;
			} else if (stream->id == RKISP_STREAM_DMATX2) {
				if (cap->rdbk_buf[RDBK_S]) {
					v4l2_err(&isp_dev->v4l2_dev,
						"multiple short data in hdr frame\n");
					rkisp_buf_queue(&cap->rdbk_buf[RDBK_S]->vb.vb2_buf);
				}
				cap->rdbk_buf[RDBK_S] = stream->curr_buf;
				rdbk_frame_end(stream);
			} else {
				vb2_buffer_done(&stream->curr_buf->vb.vb2_buf,
						VB2_BUF_STATE_DONE);
			}
		}

		stream->curr_buf = NULL;
	}

	if (!interlaced ||
		(stream->curr_buf == stream->next_buf &&
		stream->u.sp.field == RKISP_FIELD_ODD)) {
		/* Next frame is writing to it
		 * Interlaced: odd field next buffer address
		 */
		stream->curr_buf = stream->next_buf;
		stream->next_buf = NULL;

		/* Set up an empty buffer for the next-next frame */
		spin_lock_irqsave(&stream->vbq_lock, lock_flags);
		if (!list_empty(&stream->buf_queue)) {
			stream->next_buf =
				list_first_entry(&stream->buf_queue,
						 struct rkisp_buffer,
						 queue);
			list_del(&stream->next_buf->queue);
		}
		spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
	} else if (stream->u.sp.field_rec == RKISP_FIELD_ODD &&
		stream->u.sp.field == RKISP_FIELD_EVEN) {
		/* Interlaced: event field next buffer address */
		if (stream->next_buf) {
			stream->next_buf->buff_addr[RKISP_PLANE_Y] +=
				stream->u.sp.vir_offs;
			stream->next_buf->buff_addr[RKISP_PLANE_CB] +=
				stream->u.sp.vir_offs;
			stream->next_buf->buff_addr[RKISP_PLANE_CR] +=
				stream->u.sp.vir_offs;
		}
		stream->curr_buf = stream->next_buf;
	}

	stream->ops->update_mi(stream);

	if (interlaced)
		stream->u.sp.field_rec = stream->u.sp.field;

	return 0;
}

/***************************** vb2 operations*******************************/

static int rkisp_create_hdr_buf(struct rkisp_device *dev)
{
	int i, j, max_dma, max_buf = 1;
	struct rkisp_dummy_buffer *buf;
	struct rkisp_stream *stream;
	u32 size;

	stream = &dev->cap_dev.stream[RKISP_STREAM_DMATX0];
	size = stream->out_fmt.plane_fmt[0].sizeimage;
	max_dma = hdr_dma_frame(dev);
	/* hdr read back mode using base and shd address
	 * this support multi-buffer
	 */
	if (IS_HDR_RDBK(dev->hdr.op_mode)) {
		if (!dev->dmarx_dev.trigger)
			max_buf = HDR_MAX_DUMMY_BUF;
		else
			max_buf = 0;
	}
	for (i = 0; i < max_dma; i++) {
		for (j = 0; j < max_buf; j++) {
			buf = &dev->hdr.dummy_buf[i][j];
			buf->size = size;
			if (rkisp_alloc_buffer(dev->dev, buf) < 0) {
				v4l2_err(&dev->v4l2_dev,
					"Failed to allocate the memory for hdr buffer\n");
				return -ENOMEM;
			}
			hdr_qbuf(&dev->hdr.q_tx[i], buf);
			v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
				 "hdr buf[%d][%d]:0x%x\n",
				 i, j, (u32)buf->dma_addr);
		}
		dev->hdr.index[i] = i;
	}
	/*
	 * normal: q_tx[0] to dma0
	 *	   q_tx[1] to dma1
	 * rdbk1: using dma2
		   q_tx[0] to dma2
	 * rdbk2: using dma0 (as M), dma2 (as S)
	 *	   q_tx[0] to dma0
	 *	   q_tx[1] to dma2
	 * rdbk3: using dma0 (as M), dam1 (as L), dma2 (as S)
	 *	   q_tx[0] to dma0
	 *	   q_tx[1] to dma1
	 *	   q_tx[2] to dma2
	 */
	if (dev->hdr.op_mode == HDR_RDBK_FRAME1) {
		dev->hdr.index[HDR_DMA2] = 0;
		dev->hdr.index[HDR_DMA0] = 1;
		dev->hdr.index[HDR_DMA1] = 2;
	} else if (dev->hdr.op_mode == HDR_RDBK_FRAME2) {
		dev->hdr.index[HDR_DMA0] = 0;
		dev->hdr.index[HDR_DMA2] = 1;
		dev->hdr.index[HDR_DMA1] = 2;
	}

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "hdr buf index dma0:%d dma1:%d dma2:%d\n",
		 dev->hdr.index[HDR_DMA0],
		 dev->hdr.index[HDR_DMA1],
		 dev->hdr.index[HDR_DMA2]);
	return 0;
}

void hdr_destroy_buf(struct rkisp_device *dev)
{
	int i, j, max_dma, max_buf = 1;
	struct rkisp_dummy_buffer *buf;

	if (atomic_read(&dev->cap_dev.refcnt) > 1 ||
	    !dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2))
		return;

	atomic_set(&dev->hdr.refcnt, 0);
	max_dma = hdr_dma_frame(dev);
	if (IS_HDR_RDBK(dev->hdr.op_mode)) {
		if (!dev->dmarx_dev.trigger)
			max_buf = HDR_MAX_DUMMY_BUF;
		else
			max_buf = 0;
	}
	for (i = 0; i < max_dma; i++) {
		buf = dev->hdr.rx_cur_buf[i];
		if (buf) {
			rkisp_free_buffer(dev->dev, buf);
			dev->hdr.rx_cur_buf[i] = NULL;
		}

		for (j = 0; j < max_buf; j++) {
			buf = hdr_dqbuf(&dev->hdr.q_tx[i]);
			if (buf)
				rkisp_free_buffer(dev->dev, buf);
			buf = hdr_dqbuf(&dev->hdr.q_rx[i]);
			if (buf)
				rkisp_free_buffer(dev->dev, buf);
		}
	}
}

int hdr_update_dmatx_buf(struct rkisp_device *dev)
{
	void __iomem *base = dev->base_addr;
	struct rkisp_stream *dmatx;
	struct rkisp_dummy_buffer *buf;
	u8 index;

	if (!dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2))
		return 0;

	dmatx = &dev->cap_dev.stream[RKISP_STREAM_DMATX0];
	mi_frame_end(dmatx);

	dmatx = &dev->cap_dev.stream[RKISP_STREAM_DMATX1];
	mi_frame_end(dmatx);

	dmatx = &dev->cap_dev.stream[RKISP_STREAM_DMATX2];
	mi_frame_end(dmatx);

	if (dev->dmarx_dev.trigger)
		goto end;

	/* for rawrd auto trigger mode, config first buf */
	index = dev->hdr.index[HDR_DMA0];
	buf = hdr_dqbuf(&dev->hdr.q_rx[index]);
	if (buf) {
		mi_raw0_rd_set_addr(base, buf->dma_addr);
		dev->hdr.rx_cur_buf[index] = buf;
	} else {
		mi_raw0_rd_set_addr(base,
			readl(base + MI_RAW0_WR_BASE_SHD));
	}

	index = dev->hdr.index[HDR_DMA1];
	buf = hdr_dqbuf(&dev->hdr.q_rx[index]);
	if (buf) {
		mi_raw1_rd_set_addr(base, buf->dma_addr);
		dev->hdr.rx_cur_buf[index] = buf;
	} else {
		mi_raw1_rd_set_addr(base,
			readl(base + MI_RAW1_WR_BASE_SHD));
	}

	index = dev->hdr.index[HDR_DMA2];
	buf = hdr_dqbuf(&dev->hdr.q_rx[index]);
	if (buf) {
		mi_raw2_rd_set_addr(base, buf->dma_addr);
		dev->hdr.rx_cur_buf[index] = buf;
	} else {
		mi_raw2_rd_set_addr(base,
			readl(base + MI_RAW2_WR_BASE_SHD));
	}

end:
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "CSI2RX CTRL0:0x%x CTRL1:0x%x\n"
		 "WR CTRL RAW0:0x%x RAW1:0x%x RAW2:0x%x\n"
		 "RD CTRL:0x%x\n",
		 readl(base + CSI2RX_CTRL0),
		 readl(base + CSI2RX_CTRL1),
		 readl(base + CSI2RX_RAW0_WR_CTRL),
		 readl(base + CSI2RX_RAW1_WR_CTRL),
		 readl(base + CSI2RX_RAW2_WR_CTRL),
		 readl(base + CSI2RX_RAW_RD_CTRL));
	return 0;
}

int hdr_config_dmatx(struct rkisp_device *dev)
{
	if (atomic_inc_return(&dev->hdr.refcnt) > 1 ||
	    !dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2))
		return 0;

	rkisp_create_hdr_buf(dev);

	if (dev->hdr.op_mode == HDR_FRAMEX2_DDR ||
	    dev->hdr.op_mode == HDR_LINEX2_DDR ||
	    dev->hdr.op_mode == HDR_FRAMEX3_DDR ||
	    dev->hdr.op_mode == HDR_LINEX3_DDR ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME2 ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME3)
		dmatx0_config_mi(&dev->cap_dev.stream[RKISP_STREAM_DMATX0]);
	if (dev->hdr.op_mode == HDR_FRAMEX3_DDR ||
	    dev->hdr.op_mode == HDR_LINEX3_DDR ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME3)
		dmatx1_config_mi(&dev->cap_dev.stream[RKISP_STREAM_DMATX1]);
	if (dev->hdr.op_mode == HDR_RDBK_FRAME1 ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME2 ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME3)
		dmatx2_config_mi(&dev->cap_dev.stream[RKISP_STREAM_DMATX2]);

	if (IS_HDR_RDBK(dev->hdr.op_mode))
		raw_rd_ctrl(dev->base_addr, dev->csi_dev.memory << 2);
	return 0;
}

void hdr_stop_dmatx(struct rkisp_device *dev)
{
	struct rkisp_stream *stream;

	if (atomic_dec_return(&dev->hdr.refcnt) ||
	    !dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2))
		return;

	if (dev->hdr.op_mode == HDR_FRAMEX2_DDR ||
	    dev->hdr.op_mode == HDR_LINEX2_DDR ||
	    dev->hdr.op_mode == HDR_FRAMEX3_DDR ||
	    dev->hdr.op_mode == HDR_LINEX3_DDR ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME2 ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME3) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_DMATX0];
		raw_wr_disable(stream);
		stream->u.dmatx.is_config = false;
	}
	if (dev->hdr.op_mode == HDR_FRAMEX3_DDR ||
	    dev->hdr.op_mode == HDR_LINEX3_DDR ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME3) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_DMATX1];
		raw_wr_disable(stream);
		stream->u.dmatx.is_config = false;
	}
	if (dev->hdr.op_mode == HDR_RDBK_FRAME1 ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME2 ||
	    dev->hdr.op_mode == HDR_RDBK_FRAME3) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_DMATX2];
		raw_wr_disable(stream);
		stream->u.dmatx.is_config = false;
	}
}

/*
 * Set flags and wait, it should stop in interrupt.
 * If it didn't, stop it by force.
 */
static void rkisp_stream_stop(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	int ret = 0;

	if (dev->isp_ver == ISP_V20 &&
	    !dev->dmarx_dev.trigger &&
	    ((dev->hdr.op_mode == HDR_RDBK_FRAME1 &&
	      stream->id == RKISP_STREAM_DMATX2) ||
	     (dev->hdr.op_mode == HDR_RDBK_FRAME2 &&
	      (stream->id == RKISP_STREAM_DMATX2 ||
	       stream->id == RKISP_STREAM_DMATX0)) ||
	     (dev->hdr.op_mode == HDR_RDBK_FRAME3 &&
	      (stream->id == RKISP_STREAM_DMATX2 ||
	       stream->id == RKISP_STREAM_DMATX1 ||
	       stream->id == RKISP_STREAM_DMATX0)))) {
		stream->streaming = false;
		return;
	}

	stream->stopping = true;
	stream->ops->stop_mi(stream);
	if ((dev->isp_state & ISP_START) &&
	    dev->isp_inp != INP_DMARX_ISP) {
		ret = wait_event_timeout(stream->done,
					 !stream->streaming,
					 msecs_to_jiffies(1000));
		if (!ret)
			v4l2_warn(v4l2_dev, "%s id:%d timeout\n",
				  __func__, stream->id);
	}

	stream->stopping = false;
	stream->streaming = false;

	if (stream->id == RKISP_STREAM_MP ||
	    stream->id == RKISP_STREAM_SP) {
		disable_dcrop(stream, true);
		disable_rsz(stream, true);
	}

	stream->burst =
		CIF_MI_CTRL_BURST_LEN_LUM_16 |
		CIF_MI_CTRL_BURST_LEN_CHROM_16;
	stream->interlaced = false;
}

/*
 * Most of registers inside rockchip isp1 have shadow register since
 * they must be not changed during processing a frame.
 * Usually, each sub-module updates its shadow register after
 * processing the last pixel of a frame.
 */
static int rkisp_start(struct rkisp_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;
	struct rkisp_device *dev = stream->ispdev;
	bool is_update = false;
	int ret;

	/*
	 * MP/SP/MPFBC/DMATX need mi_cfg_upd to update shadow reg
	 * MP/SP/MPFBC will update each other when frame end, but
	 * MPFBC will limit MP/SP function: resize need to close,
	 * output yuv format only 422 and 420 than two-plane mode,
	 * and 422 or 420 is limit to MPFBC output format,
	 * default 422. MPFBC need start before MP/SP.
	 * DMATX will not update MP/SP/MPFBC, so it need update
	 * togeter with other.
	 */
	if (stream->id == RKISP_STREAM_MP ||
	    stream->id == RKISP_STREAM_SP) {
		is_update = (stream->id == RKISP_STREAM_MP) ?
			!dev->cap_dev.stream[RKISP_STREAM_SP].streaming :
			!dev->cap_dev.stream[RKISP_STREAM_MP].streaming;
	}

	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13)
		dmatx0_config_mi(stream);

	/* only MP support HDR mode, SP want to with HDR need
	 * to start after MP.
	 */
	if (dev->isp_ver == ISP_V20 &&
	    stream->id == RKISP_STREAM_MP)
		hdr_config_dmatx(dev);

	if (stream->ops->set_data_path)
		stream->ops->set_data_path(base);
	ret = stream->ops->config_mi(stream);
	if (ret)
		return ret;

	stream->ops->enable_mi(stream);
	/* It's safe to config ACTIVE and SHADOW regs for the
	 * first stream. While when the second is starting, do NOT
	 * force_cfg_update() because it also update the first one.
	 *
	 * The latter case would drop one more buf(that is 2) since
	 * there's not buf in shadow when the second FE received. This's
	 * also required because the sencond FE maybe corrupt especially
	 * when run at 120fps.
	 */
	if (is_update && !dev->br_dev.en) {
		rkisp_stats_first_ddr_config(&dev->stats_vdev);
		force_cfg_update(base);
		mi_frame_end(stream);
		if (dev->isp_ver == ISP_V20)
			hdr_update_dmatx_buf(dev);
	}
	stream->streaming = true;

	return 0;
}

static int rkisp_queue_setup(struct vb2_queue *queue,
			      unsigned int *num_buffers,
			      unsigned int *num_planes,
			      unsigned int sizes[],
			      struct device *alloc_ctxs[])
{
	struct rkisp_stream *stream = queue->drv_priv;
	struct rkisp_device *dev = stream->ispdev;
	const struct v4l2_pix_format_mplane *pixm = NULL;
	const struct capture_fmt *isp_fmt = NULL;
	u32 i;

	pixm = &stream->out_fmt;
	isp_fmt = &stream->out_isp_fmt;
	*num_planes = isp_fmt->mplanes;

	for (i = 0; i < isp_fmt->mplanes; i++) {
		const struct v4l2_plane_pix_format *plane_fmt;

		plane_fmt = &pixm->plane_fmt[i];
		/* height to align with 16 when allocating memory
		 * so that Rockchip encoder can use DMA buffer directly
		 */
		sizes[i] = (isp_fmt->fmt_type == FMT_YUV) ?
			plane_fmt->sizeimage / pixm->height *
			ALIGN(pixm->height, 16) :
			plane_fmt->sizeimage;
	}

	rkisp_chk_tb_over(dev);
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev, "%s count %d, size %d\n",
		 v4l2_type_names[queue->type], *num_buffers, sizes[0]);

	return 0;
}

/*
 * The vb2_buffer are stored in rkisp_buffer, in order to unify
 * mplane buffer and none-mplane buffer.
 */
static void rkisp_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp_buffer *ispbuf = to_rkisp_buffer(vbuf);
	struct vb2_queue *queue = vb->vb2_queue;
	struct rkisp_stream *stream = queue->drv_priv;
	unsigned long lock_flags = 0;
	struct v4l2_pix_format_mplane *pixm = &stream->out_fmt;
	struct capture_fmt *isp_fmt = &stream->out_isp_fmt;
	int i;

	memset(ispbuf->buff_addr, 0, sizeof(ispbuf->buff_addr));
	for (i = 0; i < isp_fmt->mplanes; i++) {
		ispbuf->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);
		if (stream->id ==  RKISP_STREAM_DMATX0 &&
		    stream->ispdev->isp_ver != ISP_V20) {
			/* for check dmatx to ddr complete */
			u32 sizeimage = pixm->plane_fmt[0].sizeimage;
			u32 *buf = vb2_plane_vaddr(vb, 0);

			if (buf) {
				*buf = RKISP_DMATX_CHECK;
				*(buf + sizeimage / 4 - 1) = RKISP_DMATX_CHECK;
			}
		}
	}

	/*
	 * NOTE: plane_fmt[0].sizeimage is total size of all planes for single
	 * memory plane formats, so calculate the size explicitly.
	 */
	if (isp_fmt->mplanes == 1) {
		for (i = 0; i < isp_fmt->cplanes - 1; i++) {
			ispbuf->buff_addr[i + 1] = (i == 0) ?
				ispbuf->buff_addr[i] +
				pixm->plane_fmt[i].bytesperline *
				pixm->height :
				ispbuf->buff_addr[i] +
				pixm->plane_fmt[i].sizeimage;
		}
	}

	v4l2_dbg(2, rkisp_debug, &stream->ispdev->v4l2_dev,
		 "stream:%d queue buf:0x%x\n",
		 stream->id, ispbuf->buff_addr[0]);

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);

	/* XXX: replace dummy to speed up  */
	if (stream->streaming &&
	    !stream->next_buf &&
	    !stream->interlaced &&
	    stream->id != RKISP_STREAM_DMATX0 &&
	    stream->id != RKISP_STREAM_DMATX1 &&
	    stream->id != RKISP_STREAM_DMATX2 &&
	    stream->id != RKISP_STREAM_DMATX3 &&
	    atomic_read(&stream->ispdev->isp_sdev.frm_sync_seq) == 0) {
		stream->next_buf = ispbuf;
		stream->ops->update_mi(stream);
	} else {
		list_add_tail(&ispbuf->queue, &stream->buf_queue);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static int rkisp_create_dummy_buf(struct rkisp_stream *stream)
{
	struct rkisp_dummy_buffer *dummy_buf = &stream->dummy_buf;
	struct rkisp_device *dev = stream->ispdev;

	/* get a maximum size */
	dummy_buf->size = max3(stream->out_fmt.plane_fmt[0].bytesperline *
		stream->out_fmt.height,
		stream->out_fmt.plane_fmt[1].sizeimage,
		stream->out_fmt.plane_fmt[2].sizeimage);
	if (dev->active_sensor &&
	    dev->active_sensor->mbus.type == V4L2_MBUS_CSI2 &&
	    (dev->isp_ver == ISP_V12 ||
	     dev->isp_ver == ISP_V13 ||
	     dev->isp_ver == ISP_V20)) {
		u32 in_size;
		struct rkisp_stream *dmatx =
			&dev->cap_dev.stream[RKISP_STREAM_DMATX0];

		in_size = dmatx->out_fmt.plane_fmt[0].sizeimage;
		dummy_buf->size = max(dummy_buf->size, in_size);
	}

	if (rkisp_alloc_buffer(dev->dev, dummy_buf) < 0) {
		v4l2_err(&dev->v4l2_dev,
			 "Failed to allocate the memory for dummy buffer\n");
		return -ENOMEM;
	}

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "stream:%d dummy buf:0x%x\n",
		 stream->id, (u32)dummy_buf->dma_addr);
	return 0;
}

static void rkisp_destroy_dummy_buf(struct rkisp_stream *stream)
{
	struct rkisp_dummy_buffer *dummy_buf = &stream->dummy_buf;
	struct rkisp_device *dev = stream->ispdev;

	rkisp_free_buffer(dev->dev, dummy_buf);
	if (dev->isp_ver == ISP_V20)
		hdr_destroy_buf(dev);
}

static void destroy_buf_queue(struct rkisp_stream *stream,
			      enum vb2_buffer_state state)
{
	struct rkisp_device *isp_dev = stream->ispdev;
	struct rkisp_capture_device *cap = &isp_dev->cap_dev;
	unsigned long lock_flags = 0;
	struct rkisp_buffer *buf;

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (cap->rdbk_buf[RDBK_L] && stream->id == RKISP_STREAM_DMATX0) {
		list_add_tail(&cap->rdbk_buf[RDBK_L]->queue,
			&stream->buf_queue);
		if (cap->rdbk_buf[RDBK_L] == stream->curr_buf)
			stream->curr_buf = NULL;
		if (cap->rdbk_buf[RDBK_L] == stream->next_buf)
			stream->next_buf = NULL;
		cap->rdbk_buf[RDBK_L] = NULL;
	}
	if (cap->rdbk_buf[RDBK_M] && stream->id == RKISP_STREAM_DMATX1) {
		list_add_tail(&cap->rdbk_buf[RDBK_M]->queue,
			&stream->buf_queue);
		if (cap->rdbk_buf[RDBK_M] == stream->curr_buf)
			stream->curr_buf = NULL;
		if (cap->rdbk_buf[RDBK_M] == stream->next_buf)
			stream->next_buf = NULL;
		cap->rdbk_buf[RDBK_M] = NULL;
	}
	if (cap->rdbk_buf[RDBK_S] && stream->id == RKISP_STREAM_DMATX2) {
		list_add_tail(&cap->rdbk_buf[RDBK_S]->queue,
			&stream->buf_queue);
		if (cap->rdbk_buf[RDBK_S] == stream->curr_buf)
			stream->curr_buf = NULL;
		if (cap->rdbk_buf[RDBK_S] == stream->next_buf)
			stream->next_buf = NULL;
		cap->rdbk_buf[RDBK_S] = NULL;
	}
	if (stream->curr_buf) {
		list_add_tail(&stream->curr_buf->queue, &stream->buf_queue);
		if (stream->curr_buf == stream->next_buf)
			stream->next_buf = NULL;
		stream->curr_buf = NULL;
	}
	if (stream->next_buf) {
		list_add_tail(&stream->next_buf->queue, &stream->buf_queue);
		stream->next_buf = NULL;
	}
	while (!list_empty(&stream->buf_queue)) {
		buf = list_first_entry(&stream->buf_queue,
			struct rkisp_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static void rkisp_stop_streaming(struct vb2_queue *queue)
{
	struct rkisp_stream *stream = queue->drv_priv;
	struct rkisp_vdev_node *node = &stream->vnode;
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	int ret;

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s %d\n", __func__, stream->id);

	if (!stream->streaming)
		return;

	rkisp_stream_stop(stream);
	if (stream->id == RKISP_STREAM_MP ||
	    stream->id == RKISP_STREAM_SP) {
		/* call to the other devices */
		media_pipeline_stop(&node->vdev.entity);
		ret = dev->pipe.set_stream(&dev->pipe, false);
		if (ret < 0)
			v4l2_err(v4l2_dev,
				 "pipeline stream-off failed:%d\n", ret);
	}

	/* release buffers */
	destroy_buf_queue(stream, VB2_BUF_STATE_ERROR);

	ret = dev->pipe.close(&dev->pipe);
	if (ret < 0)
		v4l2_err(v4l2_dev, "pipeline close failed error:%d\n", ret);
	rkisp_destroy_dummy_buf(stream);
	atomic_dec(&dev->cap_dev.refcnt);
}

static int rkisp_stream_start(struct rkisp_stream *stream)
{
	struct v4l2_device *v4l2_dev = &stream->ispdev->v4l2_dev;
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_stream *other = &dev->cap_dev.stream[stream->id ^ 1];
	bool async = false;
	int ret;

	/* STREAM DMATX don't have rsz and dcrop */
	if (stream->id == RKISP_STREAM_DMATX0 ||
	    stream->id == RKISP_STREAM_DMATX1 ||
	    stream->id == RKISP_STREAM_DMATX2 ||
	    stream->id == RKISP_STREAM_DMATX3)
		goto end;

	if (other->streaming)
		async = true;

	ret = rkisp_config_rsz(stream, async);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "config rsz failed with error %d\n", ret);
		return ret;
	}

	/*
	 * can't be async now, otherwise the latter started stream fails to
	 * produce mi interrupt.
	 */
	ret = rkisp_config_dcrop(stream, false);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "config dcrop failed with error %d\n", ret);
		return ret;
	}

end:
	return rkisp_start(stream);
}

static int
rkisp_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct rkisp_stream *stream = queue->drv_priv;
	struct rkisp_vdev_node *node = &stream->vnode;
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	int ret = -1;

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s %d\n", __func__, stream->id);

	if (WARN_ON(stream->streaming))
		return -EBUSY;

	atomic_inc(&dev->cap_dev.refcnt);
	if (!dev->isp_inp || !stream->linked) {
		v4l2_err(v4l2_dev, "check video link or isp input\n");
		goto buffer_done;
	}

	if (atomic_read(&dev->cap_dev.refcnt) == 1 &&
	    (dev->isp_inp & INP_CSI || dev->isp_inp & INP_DVP)) {
		/* update sensor info when first streaming */
		ret = rkisp_update_sensor_info(dev);
		if (ret < 0) {
			v4l2_err(v4l2_dev,
				 "update sensor info failed %d\n",
				 ret);
			goto buffer_done;
		}
	}

	if (dev->active_sensor &&
		dev->active_sensor->fmt[0].format.field ==
		V4L2_FIELD_INTERLACED) {
		if (stream->id != RKISP_STREAM_SP) {
			v4l2_err(v4l2_dev,
				"only selfpath support interlaced\n");
			ret = -EINVAL;
			goto buffer_done;
		}
		stream->interlaced = true;
		stream->u.sp.field = RKISP_FIELD_INVAL;
		stream->u.sp.field_rec = RKISP_FIELD_INVAL;
	}

	ret = rkisp_create_dummy_buf(stream);
	if (ret < 0)
		goto buffer_done;

	/* enable clocks/power-domains */
	ret = dev->pipe.open(&dev->pipe, &node->vdev.entity, true);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "open cif pipeline failed %d\n", ret);
		goto destroy_dummy_buf;
	}

	/* configure stream hardware to start */
	ret = rkisp_stream_start(stream);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "start streaming failed\n");
		goto close_pipe;
	}

	if (stream->id == RKISP_STREAM_MP ||
	    stream->id == RKISP_STREAM_SP) {
		/* start sub-devices */
		ret = dev->pipe.set_stream(&dev->pipe, true);
		if (ret < 0)
			goto stop_stream;

		ret = media_pipeline_start(&node->vdev.entity, &dev->pipe.pipe);
		if (ret < 0) {
			v4l2_err(&dev->v4l2_dev,
				 "start pipeline failed %d\n", ret);
			goto pipe_stream_off;
		}
	}

	return 0;

pipe_stream_off:
	dev->pipe.set_stream(&dev->pipe, false);
stop_stream:
	rkisp_stream_stop(stream);
close_pipe:
	dev->pipe.close(&dev->pipe);
destroy_dummy_buf:
	rkisp_destroy_dummy_buf(stream);
buffer_done:
	destroy_buf_queue(stream, VB2_BUF_STATE_QUEUED);
	atomic_dec(&dev->cap_dev.refcnt);
	stream->streaming = false;
	return ret;
}

static struct vb2_ops rkisp_vb2_ops = {
	.queue_setup = rkisp_queue_setup,
	.buf_queue = rkisp_buf_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = rkisp_stop_streaming,
	.start_streaming = rkisp_start_streaming,
};

static int rkisp_init_vb2_queue(struct vb2_queue *q,
				struct rkisp_stream *stream,
				enum v4l2_buf_type buf_type)
{
	q->type = buf_type;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->drv_priv = stream;
	q->ops = &rkisp_vb2_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct rkisp_buffer);
	q->min_buffers_needed = CIF_ISP_REQ_BUFS_MIN;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &stream->ispdev->apilock;
	q->dev = stream->ispdev->dev;

	return vb2_queue_init(q);
}

/*
 * Make sure max resize/output resolution is smaller than
 * isp sub device output size. This assumes it's not
 * recommended to use ISP scale-up function to get output size
 * that exceeds sensor max resolution.
 */
static void restrict_rsz_resolution(struct rkisp_device *dev,
				    const struct stream_config *config,
				    struct v4l2_rect *max_rsz)
{
	struct v4l2_rect *input_win;

	input_win = rkisp_get_isp_sd_win(&dev->isp_sdev);
	max_rsz->width = min_t(int, input_win->width, config->max_rsz_width);
	max_rsz->height = min_t(int, input_win->height, config->max_rsz_height);
}

static int rkisp_set_fmt(struct rkisp_stream *stream,
			   struct v4l2_pix_format_mplane *pixm,
			   bool try)
{
	const struct capture_fmt *fmt;
	const struct stream_config *config = stream->config;
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_stream *other_stream;
	unsigned int imagsize = 0;
	unsigned int planes;
	u32 xsubs = 1, ysubs = 1;
	unsigned int i;

	fmt = find_fmt(stream, pixm->pixelformat);
	if (!fmt) {
		v4l2_err(&stream->ispdev->v4l2_dev,
			 "nonsupport pixelformat:%c%c%c%c\n",
			 pixm->pixelformat,
			 pixm->pixelformat >> 8,
			 pixm->pixelformat >> 16,
			 pixm->pixelformat >> 24);
		return -EINVAL;
	}

	if (stream->id == RKISP_STREAM_MP ||
	    stream->id == RKISP_STREAM_SP) {
		struct v4l2_rect max_rsz;

		other_stream = (stream->id == RKISP_STREAM_MP) ?
			&dev->cap_dev.stream[RKISP_STREAM_SP] :
			&dev->cap_dev.stream[RKISP_STREAM_MP];
		/* do checks on resolution */
		restrict_rsz_resolution(stream->ispdev, config, &max_rsz);
		pixm->width = clamp_t(u32, pixm->width,
				      config->min_rsz_width, max_rsz.width);
		pixm->height = clamp_t(u32, pixm->height,
				       config->min_rsz_height, max_rsz.height);
	} else {
		other_stream =
			&stream->ispdev->cap_dev.stream[RKISP_STREAM_MP];
	}
	pixm->num_planes = fmt->mplanes;
	pixm->field = V4L2_FIELD_NONE;
	/* get quantization from ispsd */
	pixm->quantization = stream->ispdev->isp_sdev.quantization;

	/* output full range by default, take effect in isp_params */
	if (!pixm->quantization)
		pixm->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	/* can not change quantization when stream-on */
	if (other_stream->streaming)
		pixm->quantization = other_stream->out_fmt.quantization;

	/* calculate size */
	fcc_xysubs(fmt->fourcc, &xsubs, &ysubs);
	planes = fmt->cplanes ? fmt->cplanes : fmt->mplanes;
	for (i = 0; i < planes; i++) {
		struct v4l2_plane_pix_format *plane_fmt;
		unsigned int width, height, bytesperline;

		plane_fmt = pixm->plane_fmt + i;

		if (i == 0) {
			width = pixm->width;
			height = pixm->height;
		} else {
			width = pixm->width / xsubs;
			height = pixm->height / ysubs;
		}

		if (dev->isp_ver == ISP_V20 &&
		    !dev->csi_dev.memory &&
		    stream->id != RKISP_STREAM_MP &&
		    stream->id != RKISP_STREAM_SP)
			/* compact mode need bytesperline 4byte align */
			bytesperline = ALIGN(width * fmt->bpp[i] / 8, 4);
		else
			bytesperline = width * DIV_ROUND_UP(fmt->bpp[i], 8);

		/* stride is only available for sp stream and y plane */
		if (stream->id != RKISP_STREAM_SP || i != 0 ||
		    plane_fmt->bytesperline < bytesperline)
			plane_fmt->bytesperline = bytesperline;

		plane_fmt->sizeimage = plane_fmt->bytesperline * height;

		imagsize += plane_fmt->sizeimage;
	}

	/* convert to non-MPLANE format.
	 * it's important since we want to unify none-MPLANE
	 * and MPLANE.
	 */
	if (fmt->mplanes == 1)
		pixm->plane_fmt[0].sizeimage = imagsize;

	if (!try) {
		stream->out_isp_fmt = *fmt;
		stream->out_fmt = *pixm;

		if (stream->id == RKISP_STREAM_SP) {
			stream->u.sp.y_stride =
				pixm->plane_fmt[0].bytesperline /
				DIV_ROUND_UP(fmt->bpp[0], 8);
		} else if (stream->id == RKISP_STREAM_MP) {
			stream->u.mp.raw_enable = (fmt->fmt_type == FMT_BAYER);
		}

		v4l2_dbg(1, rkisp_debug, &stream->ispdev->v4l2_dev,
			 "%s: stream: %d req(%d, %d) out(%d, %d)\n", __func__,
			 stream->id, pixm->width, pixm->height,
			 stream->out_fmt.width, stream->out_fmt.height);
	}

	return 0;
}

int rkisp_fh_open(struct file *filp)
{
	struct rkisp_stream *stream = video_drvdata(filp);
	int ret;

	ret = v4l2_fh_open(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_use(&stream->vnode.vdev.entity, 1);
		if (ret < 0)
			vb2_fop_release(filp);
	}

	return ret;
}

int rkisp_fop_release(struct file *file)
{
	struct rkisp_stream *stream = video_drvdata(file);
	int ret;

	ret = vb2_fop_release(file);
	if (!ret) {
		ret = v4l2_pipeline_pm_use(&stream->vnode.vdev.entity, 0);
		if (ret < 0)
			v4l2_err(&stream->ispdev->v4l2_dev,
				 "set pipeline power failed %d\n", ret);
	}
	return ret;
}

void rkisp_set_stream_def_fmt(struct rkisp_device *dev, u32 id,
	u32 width, u32 height, u32 pixelformat)
{
	struct rkisp_stream *stream = &dev->cap_dev.stream[id];
	struct v4l2_pix_format_mplane pixm;

	memset(&pixm, 0, sizeof(pixm));
	if (pixelformat)
		pixm.pixelformat = pixelformat;
	else
		pixm.pixelformat = stream->out_isp_fmt.fourcc;
	if (!pixm.pixelformat)
		return;
	pixm.width = width;
	pixm.height = height;
	rkisp_set_fmt(stream, &pixm, false);

	stream->dcrop.left = 0;
	stream->dcrop.top = 0;
	stream->dcrop.width = width;
	stream->dcrop.height = height;
}

/************************* v4l2_file_operations***************************/
static const struct v4l2_file_operations rkisp_fops = {
	.open = rkisp_fh_open,
	.release = rkisp_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

/*
 * mp and sp v4l2_ioctl_ops
 */

static int rkisp_enum_input(struct file *file, void *priv,
			     struct v4l2_input *input)
{
	if (input->index > 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strlcpy(input->name, "Camera", sizeof(input->name));

	return 0;
}

static int rkisp_try_fmt_vid_cap_mplane(struct file *file, void *fh,
					 struct v4l2_format *f)
{
	struct rkisp_stream *stream = video_drvdata(file);

	return rkisp_set_fmt(stream, &f->fmt.pix_mp, true);
}

static int rkisp_enum_framesizes(struct file *file, void *prov,
				 struct v4l2_frmsizeenum *fsize)
{
	struct rkisp_stream *stream = video_drvdata(file);
	const struct stream_config *config = stream->config;
	struct v4l2_frmsize_stepwise *s = &fsize->stepwise;
	struct v4l2_frmsize_discrete *d = &fsize->discrete;
	const struct ispsd_out_fmt *input_isp_fmt;
	struct v4l2_rect max_rsz;

	if (fsize->index != 0)
		return -EINVAL;

	if (!find_fmt(stream, fsize->pixel_format))
		return -EINVAL;

	restrict_rsz_resolution(stream->ispdev, config, &max_rsz);

	input_isp_fmt = rkisp_get_ispsd_out_fmt(&stream->ispdev->isp_sdev);
	if (input_isp_fmt->fmt_type == FMT_BAYER) {
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		d->width = max_rsz.width;
		d->height = max_rsz.height;
	} else {
		fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
		s->min_width = STREAM_MIN_RSZ_OUTPUT_WIDTH;
		s->min_height = STREAM_MIN_RSZ_OUTPUT_HEIGHT;
		s->max_width = max_rsz.width;
		s->max_height = max_rsz.height;
		s->step_width = STREAM_OUTPUT_STEP_WISE;
		s->step_height = STREAM_OUTPUT_STEP_WISE;
	}

	return 0;
}

static int rkisp_enum_frameintervals(struct file *file, void *fh,
				     struct v4l2_frmivalenum *fival)
{
	const struct rkisp_stream *stream = video_drvdata(file);
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_sensor_info *sensor = dev->active_sensor;
	struct v4l2_subdev_frame_interval fi;
	int ret;

	if (fival->index != 0)
		return -EINVAL;

	if (!sensor) {
		/* TODO: active_sensor is NULL if using DMARX path */
		v4l2_err(&dev->v4l2_dev, "%s Not active sensor\n", __func__);
		return -ENODEV;
	}

	ret = v4l2_subdev_call(sensor->sd, video, g_frame_interval, &fi);
	if (ret && ret != -ENOIOCTLCMD) {
		return ret;
	} else if (ret == -ENOIOCTLCMD) {
		/* Set a default value for sensors not implements ioctl */
		fi.interval.numerator = 1;
		fi.interval.denominator = 30;
	}

	fival->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
	fival->stepwise.step.numerator = 1;
	fival->stepwise.step.denominator = 1;
	fival->stepwise.max.numerator = 1;
	fival->stepwise.max.denominator = 1;
	fival->stepwise.min.numerator = fi.interval.numerator;
	fival->stepwise.min.denominator = fi.interval.denominator;

	return 0;
}

static int rkisp_enum_fmt_vid_cap_mplane(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	struct rkisp_stream *stream = video_drvdata(file);
	const struct capture_fmt *fmt = NULL;

	if (f->index >= stream->config->fmt_size)
		return -EINVAL;

	fmt = &stream->config->fmts[f->index];
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int rkisp_s_fmt_vid_cap_mplane(struct file *file,
				       void *priv, struct v4l2_format *f)
{
	struct rkisp_stream *stream = video_drvdata(file);
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkisp_vdev_node *node = vdev_to_node(vdev);
	struct rkisp_device *dev = stream->ispdev;

	if (vb2_is_busy(&node->buf_queue)) {
		v4l2_err(&dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	return rkisp_set_fmt(stream, &f->fmt.pix_mp, false);
}

static int rkisp_g_fmt_vid_cap_mplane(struct file *file, void *fh,
				       struct v4l2_format *f)
{
	struct rkisp_stream *stream = video_drvdata(file);

	f->fmt.pix_mp = stream->out_fmt;

	return 0;
}

static int rkisp_g_selection(struct file *file, void *prv,
			      struct v4l2_selection *sel)
{
	struct rkisp_stream *stream = video_drvdata(file);
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_rect *dcrop = &stream->dcrop;
	struct v4l2_rect *input_win;

	input_win = rkisp_get_isp_sd_win(&dev->isp_sdev);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.width = input_win->width;
		sel->r.height = input_win->height;
		sel->r.left = 0;
		sel->r.top = 0;
		break;
	case V4L2_SEL_TGT_CROP:
		sel->r = *dcrop;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct v4l2_rect *rkisp_update_crop(struct rkisp_stream *stream,
					    struct v4l2_rect *sel,
					    const struct v4l2_rect *in)
{
	/* Not crop for MP bayer raw data and dmatx path */
	if ((stream->id == RKISP_STREAM_MP &&
	     stream->out_isp_fmt.fmt_type == FMT_BAYER) ||
	    stream->id == RKISP_STREAM_DMATX0 ||
	    stream->id == RKISP_STREAM_DMATX1 ||
	    stream->id == RKISP_STREAM_DMATX2 ||
	    stream->id == RKISP_STREAM_DMATX3) {
		sel->left = 0;
		sel->top = 0;
		sel->width = in->width;
		sel->height = in->height;
		return sel;
	}

	sel->left = ALIGN(sel->left, 2);
	sel->width = ALIGN(sel->width, 2);
	sel->left = clamp_t(u32, sel->left, 0,
			    in->width - STREAM_MIN_MP_SP_INPUT_WIDTH);
	sel->top = clamp_t(u32, sel->top, 0,
			   in->height - STREAM_MIN_MP_SP_INPUT_HEIGHT);
	sel->width = clamp_t(u32, sel->width, STREAM_MIN_MP_SP_INPUT_WIDTH,
			     in->width - sel->left);
	sel->height = clamp_t(u32, sel->height, STREAM_MIN_MP_SP_INPUT_HEIGHT,
			      in->height - sel->top);
	return sel;
}

static int rkisp_s_selection(struct file *file, void *prv,
			      struct v4l2_selection *sel)
{
	struct rkisp_stream *stream = video_drvdata(file);
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkisp_vdev_node *node = vdev_to_node(vdev);
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_rect *dcrop = &stream->dcrop;
	const struct v4l2_rect *input_win;

	if (vb2_is_busy(&node->buf_queue)) {
		v4l2_err(&dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	input_win = rkisp_get_isp_sd_win(&dev->isp_sdev);

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	if (sel->flags != 0)
		return -EINVAL;

	*dcrop = *rkisp_update_crop(stream, &sel->r, input_win);
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "stream %d crop(%d,%d)/%dx%d\n", stream->id,
		 dcrop->left, dcrop->top, dcrop->width, dcrop->height);

	return 0;
}

static int rkisp_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct rkisp_stream *stream = video_drvdata(file);
	struct device *dev = stream->ispdev->dev;
	struct video_device *vdev = video_devdata(file);

	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	snprintf(cap->driver, sizeof(cap->driver),
		 "%s_v%d", dev->driver->name,
		 stream->ispdev->isp_ver >> 4);
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", dev_name(dev));

	return 0;
}

static const struct v4l2_ioctl_ops rkisp_v4l2_ioctl_ops = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_input = rkisp_enum_input,
	.vidioc_try_fmt_vid_cap_mplane = rkisp_try_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_cap_mplane = rkisp_enum_fmt_vid_cap_mplane,
	.vidioc_s_fmt_vid_cap_mplane = rkisp_s_fmt_vid_cap_mplane,
	.vidioc_g_fmt_vid_cap_mplane = rkisp_g_fmt_vid_cap_mplane,
	.vidioc_s_selection = rkisp_s_selection,
	.vidioc_g_selection = rkisp_g_selection,
	.vidioc_querycap = rkisp_querycap,
	.vidioc_enum_frameintervals = rkisp_enum_frameintervals,
	.vidioc_enum_framesizes = rkisp_enum_framesizes,
};

static void rkisp_unregister_stream_vdev(struct rkisp_stream *stream)
{
	media_entity_cleanup(&stream->vnode.vdev.entity);
	video_unregister_device(&stream->vnode.vdev);
}

static int rkisp_register_stream_vdev(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkisp_vdev_node *node;
	struct media_entity *source, *sink;
	int ret = 0, pad;

	node = vdev_to_node(vdev);

	vdev->ioctl_ops = &rkisp_v4l2_ioctl_ops;
	vdev->release = video_device_release_empty;
	vdev->fops = &rkisp_fops;
	vdev->minor = -1;
	vdev->v4l2_dev = v4l2_dev;
	vdev->lock = &dev->apilock;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
				V4L2_CAP_STREAMING;
	video_set_drvdata(vdev, stream);
	vdev->vfl_dir = VFL_DIR_RX;
	node->pad.flags = MEDIA_PAD_FL_SINK;

	rkisp_init_vb2_queue(&node->buf_queue, stream,
			     V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	vdev->queue = &node->buf_queue;

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		v4l2_err(v4l2_dev,
			 "video_register_device failed with error %d\n", ret);
		return ret;
	}

	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret < 0)
		goto unreg;

	source = &dev->csi_dev.sd.entity;
	switch (stream->id) {
	case RKISP_STREAM_DMATX0://CSI_SRC_CH1
	case RKISP_STREAM_DMATX1://CSI_SRC_CH2
	case RKISP_STREAM_DMATX2://CSI_SRC_CH3
	case RKISP_STREAM_DMATX3://CSI_SRC_CH4
		pad = stream->id;
		dev->csi_dev.sink[pad - 1].linked = true;
		dev->csi_dev.sink[pad - 1].index = BIT(pad - 1);
		break;
	default:
		source = &dev->isp_sdev.sd.entity;
		pad = RKISP_ISP_PAD_SOURCE_PATH;
	}
	sink = &vdev->entity;
	ret = media_create_pad_link(source, pad,
		sink, 0, stream->linked);
	if (ret < 0)
		goto unreg;
	return 0;
unreg:
	video_unregister_device(vdev);
	return ret;
}

static int rkisp_stream_init(struct rkisp_device *dev, u32 id)
{
	struct rkisp_capture_device *cap_dev = &dev->cap_dev;
	struct rkisp_stream *stream;
	struct video_device *vdev;
	char *name;
	int ret = 0;

	stream = &cap_dev->stream[id];
	stream->id = id;
	stream->ispdev = dev;
	vdev = &stream->vnode.vdev;

	INIT_LIST_HEAD(&stream->buf_queue);
	init_waitqueue_head(&stream->done);
	spin_lock_init(&stream->vbq_lock);

	stream->linked = MEDIA_LNK_FL_ENABLED;
	/* isp2 disable MP/SP, enable BRIDGE default */
	if ((id == RKISP_STREAM_SP || id == RKISP_STREAM_MP) &&
	    dev->isp_ver == ISP_V20)
		stream->linked = false;

	switch (id) {
	case RKISP_STREAM_SP:
		strlcpy(vdev->name, SP_VDEV_NAME,
			sizeof(vdev->name));
		stream->ops = &rkisp_sp_streams_ops;
		stream->config = &rkisp_sp_stream_config;
		break;
	case RKISP_STREAM_DMATX0:
		if (dev->isp_ver != ISP_V20) {
			name = RAW_VDEV_NAME;
			stream->ops = &rkisp1_dmatx0_streams_ops;
			stream->config = &rkisp1_dmatx0_stream_config;
		} else {
			name = DMATX0_VDEV_NAME;
			stream->ops = &rkisp2_dmatx0_streams_ops;
			stream->config = &rkisp2_dmatx0_stream_config;
		}
		strlcpy(vdev->name, name,
			sizeof(vdev->name));
		break;
	case RKISP_STREAM_DMATX1:
		strlcpy(vdev->name, DMATX1_VDEV_NAME,
			sizeof(vdev->name));
		stream->ops = &rkisp2_dmatx1_streams_ops;
		stream->config = &rkisp2_dmatx1_stream_config;
		break;
	case RKISP_STREAM_DMATX2:
		strlcpy(vdev->name, DMATX2_VDEV_NAME,
			sizeof(vdev->name));
		stream->ops = &rkisp2_dmatx2_streams_ops;
		stream->config = &rkisp2_dmatx2_stream_config;
		break;
	case RKISP_STREAM_DMATX3:
		strlcpy(vdev->name, DMATX3_VDEV_NAME,
			sizeof(vdev->name));
		stream->ops = &rkisp2_dmatx3_streams_ops;
		stream->config = &rkisp2_dmatx3_stream_config;
		break;
	default:
		strlcpy(vdev->name, MP_VDEV_NAME,
			sizeof(vdev->name));
		stream->ops = &rkisp_mp_streams_ops;
		stream->config = &rkisp_mp_stream_config;
	}

	ret = rkisp_register_stream_vdev(stream);
	if (ret < 0)
		return ret;

	stream->streaming = false;
	stream->interlaced = false;
	stream->burst =
		CIF_MI_CTRL_BURST_LEN_LUM_16 |
		CIF_MI_CTRL_BURST_LEN_CHROM_16;
	atomic_set(&stream->sequence, 0);
	return 0;
}

int rkisp_register_stream_vdevs(struct rkisp_device *dev)
{
	struct rkisp_capture_device *cap_dev = &dev->cap_dev;
	int ret = 0;

	memset(cap_dev, 0, sizeof(*cap_dev));
	cap_dev->ispdev = dev;
	atomic_set(&cap_dev->refcnt, 0);

	ret = rkisp_stream_init(dev, RKISP_STREAM_MP);
	if (ret < 0)
		goto err;
	if (dev->isp_ver != ISP_V10_1) {
		ret = rkisp_stream_init(dev, RKISP_STREAM_SP);
		if (ret < 0)
			goto err_free_mp;
	}

	if (dev->isp_ver == ISP_V12 ||
	    dev->isp_ver == ISP_V13 ||
	    dev->isp_ver == ISP_V20) {
		ret = rkisp_stream_init(dev, RKISP_STREAM_DMATX0);
		if (ret < 0)
			goto err_free_sp;
	}

	if (dev->isp_ver == ISP_V20) {
		ret = rkisp_stream_init(dev, RKISP_STREAM_DMATX1);
		if (ret < 0)
			goto err_free_tx0;
		ret = rkisp_stream_init(dev, RKISP_STREAM_DMATX2);
		if (ret < 0)
			goto err_free_tx1;
		ret = rkisp_stream_init(dev, RKISP_STREAM_DMATX3);
		if (ret < 0)
			goto err_free_tx2;
	}

	return 0;
err_free_tx2:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_DMATX2]);
err_free_tx1:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_DMATX1]);
err_free_tx0:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_DMATX0]);
err_free_sp:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_SP]);
err_free_mp:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_MP]);
err:
	return ret;
}

void rkisp_unregister_stream_vdevs(struct rkisp_device *dev)
{
	struct rkisp_capture_device *cap_dev = &dev->cap_dev;
	struct rkisp_stream *stream;

	stream = &cap_dev->stream[RKISP_STREAM_MP];
	rkisp_unregister_stream_vdev(stream);
	if (dev->isp_ver != ISP_V10_1) {
		stream = &cap_dev->stream[RKISP_STREAM_SP];
		rkisp_unregister_stream_vdev(stream);
	}

	if (dev->isp_ver == ISP_V12 ||
	    dev->isp_ver == ISP_V13 ||
	    dev->isp_ver == ISP_V20) {
		stream = &cap_dev->stream[RKISP_STREAM_DMATX0];
		rkisp_unregister_stream_vdev(stream);
	}

	if (dev->isp_ver == ISP_V20) {
		stream = &cap_dev->stream[RKISP_STREAM_DMATX1];
		rkisp_unregister_stream_vdev(stream);
		stream = &cap_dev->stream[RKISP_STREAM_DMATX2];
		rkisp_unregister_stream_vdev(stream);
		stream = &cap_dev->stream[RKISP_STREAM_DMATX3];
		rkisp_unregister_stream_vdev(stream);
	}
}

struct rkisp_dummy_buffer *hdr_dqbuf(struct list_head *q)
{
	struct rkisp_dummy_buffer *buf = NULL;

	if (!list_empty(q)) {
		buf = list_first_entry(q,
			struct rkisp_dummy_buffer, queue);
		list_del(&buf->queue);
	}
	return buf;
}

void hdr_qbuf(struct list_head *q,
	      struct rkisp_dummy_buffer *buf)
{
	if (buf)
		list_add_tail(&buf->queue, q);
}

/****************  Interrupter Handler ****************/

void rkisp_mi_isr(u32 mis_val, struct rkisp_device *dev)
{
	unsigned int i;
	static u8 end_tx0, end_tx1, end_tx2;

	v4l2_dbg(3, rkisp_debug, &dev->v4l2_dev,
		 "mi isr:0x%x\n", mis_val);

	rkisp_bridge_isr(&mis_val, dev);

	if (mis_val & CIF_MI_DMA_READY)
		rkisp_dmarx_isr(mis_val, dev);

	for (i = 0; i < RKISP_MAX_STREAM; ++i) {
		struct rkisp_stream *stream = &dev->cap_dev.stream[i];

		if (!(mis_val & CIF_MI_FRAME(stream)))
			continue;

		if (i == RKISP_STREAM_DMATX0)
			end_tx0 = true;
		if (i == RKISP_STREAM_DMATX1)
			end_tx1 = true;
		if (i == RKISP_STREAM_DMATX2)
			end_tx2 = true;

		mi_frame_end_int_clear(stream);

		if (stream->stopping) {
			/*
			 * Make sure stream is actually stopped, whose state
			 * can be read from the shadow register, before
			 * wake_up() thread which would immediately free all
			 * frame buffers. stop_mi() takes effect at the next
			 * frame end that sync the configurations to shadow
			 * regs.
			 */
			if (stream->ops->is_stream_stopped(dev->base_addr)) {
				stream->stopping = false;
				stream->streaming = false;
				wake_up(&stream->done);
			}
			if (i == RKISP_STREAM_MP) {
				end_tx0 = false;
				end_tx1 = false;
				end_tx2 = false;
			}
		} else {
			mi_frame_end(stream);
			if (dev->dmarx_dev.trigger == T_AUTO &&
			    ((dev->hdr.op_mode == HDR_RDBK_FRAME1 && end_tx2) ||
			     (dev->hdr.op_mode == HDR_RDBK_FRAME2 && end_tx2 && end_tx0) ||
			     (dev->hdr.op_mode == HDR_RDBK_FRAME3 && end_tx2 && end_tx1 && end_tx0))) {
				end_tx0 = false;
				end_tx1 = false;
				end_tx2 = false;
				rkisp_trigger_read_back(&dev->csi_dev, false);
			}
		}
	}
}

void rkisp_mipi_dmatx0_end(u32 status, struct rkisp_device *dev)
{
	struct rkisp_stream *stream = &dev->cap_dev.stream[RKISP_STREAM_DMATX0];
	u32 *buf, end, vc, timeout = 100;

	vc = readl(dev->base_addr + CIF_ISP_CSI0_DATA_IDS_1) >> 14 & 0x3;
	if (!(status & 1 << vc) || !stream->streaming)
		return;

	dmatx0_enable(dev->base_addr);
	if (stream->stopping) {
		/* update dmatx buf to other stream dummy buf if other
		 * stream don't close, but dmatx is reopen.
		 * dmatx first buf will write to this.
		 */
		if (!stream->u.dmatx.pre_stop) {
			unsigned int i;
			struct rkisp_stream *other = NULL;

			for (i = 0; i < RKISP_MAX_STREAM; i++) {
				if (i != stream->id &&
					dev->cap_dev.stream[i].streaming) {
					other = &dev->cap_dev.stream[i];
					break;
				}
			}

			stream->u.dmatx.pre_stop = true;
			if (other) {
				mi_raw0_set_addr(dev->base_addr,
					other->dummy_buf.dma_addr);
				return;
			}
		}

		if (stream->u.dmatx.pre_stop) {
			dmatx0_disable(dev->base_addr);
			stream->u.dmatx.pre_stop = false;
			stream->stopping = false;
			stream->streaming = false;
			wake_up(&stream->done);
		}
	} else {
		if (stream->curr_buf) {
			/* for check dmatx to ddr complete */
			u32 sizeimage = stream->out_fmt.plane_fmt[0].sizeimage;

			buf = (u32 *)vb2_plane_vaddr(&stream->curr_buf->vb.vb2_buf, 0);
			if (!buf)
				goto out;
			end = *(buf + sizeimage / 4 - 1);
			while (end == RKISP_DMATX_CHECK) {
				udelay(1);
				end = *(buf + sizeimage / 4 - 1);
				if (timeout-- == 0) {
					/* if shd don't update
					 * check aclk_isp >= clk_isp
					 * input equal to sensor output, no crop
					 */
					v4l2_err(&dev->v4l2_dev,
						"dmatx to ddr timeout!\n"
						"base:0x%x shd:0x%x data:0x%x~0x%x\n",
						readl(dev->base_addr + CIF_MI_RAW0_BASE_AD_INIT),
						readl(dev->base_addr + CIF_MI_RAW0_BASE_AS_SHD),
						*buf, end);
					break;
				}
			}
		}
out:
		atomic_set(&stream->sequence,
			atomic_read(&stream->ispdev->isp_sdev.frm_sync_seq));
		mi_frame_end(stream);
	}
}

void rkisp_mipi_v20_isr(unsigned int phy, unsigned int packet,
			 unsigned int overflow, unsigned int state,
			 struct rkisp_device *dev)
{
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct rkisp_stream *stream;
	int i;

	v4l2_dbg(3, rkisp_debug, &dev->v4l2_dev,
		 "csi state:0x%x\n", state);
	if (phy && (dev->isp_inp & INP_CSI))
		v4l2_warn(v4l2_dev, "MIPI error: phy: 0x%08x\n", phy);
	if (packet && (dev->isp_inp & INP_CSI))
		v4l2_warn(v4l2_dev, "MIPI error: packet: 0x%08x\n", packet);
	if (overflow)
		v4l2_warn(v4l2_dev, "MIPI error: overflow: 0x%08x\n", overflow);
	if (state & 0xeff00)
		v4l2_warn(v4l2_dev, "MIPI error: size: 0x%08x\n", state);
	if (state & MIPI_DROP_FRM)
		v4l2_warn(v4l2_dev, "MIPI drop frame\n");

	/* first Y_STATE irq as csi sof event */
	if (state & (RAW0_Y_STATE | RAW1_Y_STATE | RAW2_Y_STATE)) {
		for (i = 0; i < HDR_DMA_MAX; i++) {
			if (!((RAW0_Y_STATE << i) & state) ||
			    dev->csi_dev.tx_first[i])
				continue;
			dev->csi_dev.tx_first[i] = true;
			rkisp_csi_sof(dev, i);
			stream = &dev->cap_dev.stream[i + RKISP_STREAM_DMATX0];
			atomic_inc(&stream->sequence);
		}
	}
	if (state & (RAW0_WR_FRAME | RAW1_WR_FRAME | RAW2_WR_FRAME)) {
		for (i = 0; i < HDR_DMA_MAX; i++) {
			if (!((RAW0_WR_FRAME << i) & state))
				continue;
			dev->csi_dev.tx_first[i] = false;
		}
	}

	if (state & (RAW0_Y_STATE | RAW1_Y_STATE | RAW2_Y_STATE |
	    RAW0_WR_FRAME | RAW1_WR_FRAME | RAW2_WR_FRAME))
		rkisp_luma_isr(&dev->luma_vdev, state);
}
