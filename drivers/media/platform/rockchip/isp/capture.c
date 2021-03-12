// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

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

#define STREAM_MAX_MP_RSZ_OUTPUT_WIDTH		4416
#define STREAM_MAX_MP_RSZ_OUTPUT_HEIGHT		3312
#define STREAM_MAX_SP_RSZ_OUTPUT_WIDTH		1920
#define STREAM_MAX_SP_RSZ_OUTPUT_HEIGHT		1080
#define STREAM_MIN_RSZ_OUTPUT_WIDTH		32
#define STREAM_MIN_RSZ_OUTPUT_HEIGHT		16
#define STREAM_OUTPUT_STEP_WISE			8

#define STREAM_MIN_MP_SP_INPUT_WIDTH		32
#define STREAM_MIN_MP_SP_INPUT_HEIGHT		32

/* Get xsubs and ysubs for fourcc formats
 *
 * @xsubs: horizontal color samples in a 4*4 matrix, for yuv
 * @ysubs: vertical color samples in a 4*4 matrix, for yuv
 */
int rkisp_fcc_xysubs(u32 fcc, u32 *xsubs, u32 *ysubs)
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
	case V4L2_PIX_FMT_UYVY:
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
	case V4L2_PIX_FMT_FBCG:
		*xsubs = 2;
		*ysubs = 2;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int rkisp_mbus_code_xysubs(u32 code, u32 *xsubs, u32 *ysubs)
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

static const struct capture_fmt mp_fmts[] = {
	/* yuv422 */
	{
		.fourcc = V4L2_PIX_FMT_UYVY,
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
		.fourcc = V4L2_PIX_FMT_UYVY,
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
	},
	/* fbcg */
	{
		.fourcc = V4L2_PIX_FMT_FBCG,
		.fmt_type = FMT_FBCGAIN,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 2,
		.uv_swap = 0,
		.write_format = MI_CTRL_SP_WRITE_SPLA,
		.output_format = MI_CTRL_SP_OUTPUT_YUV420,
	}
};

struct stream_config rkisp_mp_stream_config = {
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
		.y_base_ad_shd = CIF_MI_MP_Y_BASE_AD_SHD,
	},
};

struct stream_config rkisp_sp_stream_config = {
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
		.y_base_ad_shd = CIF_MI_SP_Y_BASE_AD_SHD,
	},
};

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
	rkisp_fcc_xysubs(fmt->fourcc, &xsubs, &ysubs);
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
		    fmt->fmt_type == FMT_BAYER &&
		    stream->id == RKISP_STREAM_DMATX2)
			height += RKMODULE_EXTEND_LINE;

		if ((dev->isp_ver == ISP_V20 ||
		     dev->isp_ver == ISP_V21) &&
		    !dev->csi_dev.memory &&
		    fmt->fmt_type == FMT_BAYER &&
		    stream->id != RKISP_STREAM_MP &&
		    stream->id != RKISP_STREAM_SP)
			/* compact mode need bytesperline 4byte align */
			bytesperline = ALIGN(width * fmt->bpp[i] / 8, 256);
		else
			bytesperline = width * DIV_ROUND_UP(fmt->bpp[i], 8);
		/* stride is only available for sp stream and y plane */
		if (stream->id != RKISP_STREAM_SP || i != 0 ||
		    plane_fmt->bytesperline < bytesperline)
			plane_fmt->bytesperline = bytesperline;

		plane_fmt->sizeimage = plane_fmt->bytesperline * height;

		/* uv address is y size offset need 64 align */
		if (fmt->fmt_type == FMT_FBCGAIN && i == 0)
			plane_fmt->sizeimage = ALIGN(plane_fmt->sizeimage, 64);

		imagsize += plane_fmt->sizeimage;
	}

	/* convert to non-MPLANE format.
	 * it's important since we want to unify none-MPLANE
	 * and MPLANE.
	 */
	if (fmt->mplanes == 1 || fmt->fmt_type == FMT_FBCGAIN)
		pixm->plane_fmt[0].sizeimage = imagsize;

	if (!try && !stream->start_stream && !stream->streaming) {
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

	if (vb2_is_streaming(&node->buf_queue)) {
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

void rkisp_unregister_stream_vdev(struct rkisp_stream *stream)
{
	media_entity_cleanup(&stream->vnode.vdev.entity);
	video_unregister_device(&stream->vnode.vdev);
}

int rkisp_register_stream_vdev(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkisp_vdev_node *node;
	struct media_entity *source, *sink;
	int ret = 0, pad;

	mutex_init(&stream->apilock);
	node = vdev_to_node(vdev);

	vdev->ioctl_ops = &rkisp_v4l2_ioctl_ops;
	vdev->release = video_device_release_empty;
	vdev->fops = &rkisp_fops;
	vdev->minor = -1;
	vdev->v4l2_dev = v4l2_dev;
	vdev->lock = &stream->apilock;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
				V4L2_CAP_STREAMING;
	video_set_drvdata(vdev, stream);
	vdev->vfl_dir = VFL_DIR_RX;
	node->pad.flags = MEDIA_PAD_FL_SINK;
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

int rkisp_register_stream_vdevs(struct rkisp_device *dev)
{
	struct rkisp_capture_device *cap_dev = &dev->cap_dev;
	int ret = 0;

	memset(cap_dev, 0, sizeof(*cap_dev));
	cap_dev->ispdev = dev;
	atomic_set(&cap_dev->refcnt, 0);

	if (dev->isp_ver <= ISP_V13)
		ret = rkisp_register_stream_v1x(dev);
	else if (dev->isp_ver == ISP_V20)
		ret = rkisp_register_stream_v20(dev);
	else if (dev->isp_ver == ISP_V21)
		ret = rkisp_register_stream_v21(dev);
	return ret;
}

void rkisp_unregister_stream_vdevs(struct rkisp_device *dev)
{
	if (dev->isp_ver <= ISP_V13)
		rkisp_unregister_stream_v1x(dev);
	else if (dev->isp_ver == ISP_V20)
		rkisp_unregister_stream_v20(dev);
	else if (dev->isp_ver == ISP_V21)
		rkisp_unregister_stream_v21(dev);
}

void rkisp_mi_isr(u32 mis_val, struct rkisp_device *dev)
{
	if (dev->isp_ver <= ISP_V13)
		rkisp_mi_v1x_isr(mis_val, dev);
	else if (dev->isp_ver == ISP_V20)
		rkisp_mi_v20_isr(mis_val, dev);
	else if (dev->isp_ver == ISP_V21)
		rkisp_mi_v21_isr(mis_val, dev);
}
