// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Rockchip Electronics Co., Ltd. */

#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include "dev.h"
#include "regs.h"

/*
 *        |--mainpath----[wrap]--------->enc(or ddr)
 *        |   |->mainpath_4x4sampling--->ddr
 *output->|->bypasspath----------------->ddr
 *        |   |->bypasspath_4x4sampling->ddr
 *        |->selfpath------------------->ddr
 *        |->lumapath------------------->ddr
 */

#define CIF_ISP_REQ_BUFS_MIN 0

static int mi_frame_end(struct rkisp_stream *stream);
static int mi_frame_start(struct rkisp_stream *stream, u32 mis);
static int rkisp_create_dummy_buf(struct rkisp_stream *stream);

static const struct capture_fmt bp_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_UYVY,
		.fmt_type = FMT_YUV,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.write_format = ISP3X_BP_FORMAT_INT,
		.output_format = ISP3X_BP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.write_format = ISP3X_BP_FORMAT_SPLA,
		.output_format = ISP3X_BP_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.write_format = ISP3X_BP_FORMAT_SPLA,
		.output_format = ISP3X_BP_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12M,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 2,
		.write_format = ISP3X_BP_FORMAT_SPLA,
		.output_format = ISP3X_BP_OUTPUT_YUV420,
	}
};

static const struct capture_fmt luma_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_GREY,
		.fmt_type = FMT_YUV,
		.bpp = { 8 },
		.cplanes = 1,
		.mplanes = 1,
	},
};

static struct stream_config rkisp_luma_stream_config = {
	.fmts = luma_fmts,
	.fmt_size = ARRAY_SIZE(luma_fmts),
	.frame_end_id = 0,
};

static struct stream_config rkisp_bp_stream_config = {
	.fmts = bp_fmts,
	.fmt_size = ARRAY_SIZE(bp_fmts),
	.max_rsz_width = CIF_ISP_INPUT_W_MAX_V32,
	.max_rsz_height = CIF_ISP_INPUT_H_MAX_V32,
	.min_rsz_width = CIF_ISP_INPUT_W_MIN,
	.min_rsz_height = CIF_ISP_INPUT_H_MIN,
	.frame_end_id = ISP3X_MI_BP_FRAME,
	.rsz = {
		.ctrl = ISP32_BP_RESIZE_CTRL,
		.scale_hy = ISP32_BP_RESIZE_SCALE_HY,
		.scale_hcr = ISP32_BP_RESIZE_SCALE_HCR,
		.scale_hcb = ISP32_BP_RESIZE_SCALE_HCB,
		.scale_vy = ISP32_BP_RESIZE_SCALE_VY,
		.scale_vc = ISP32_BP_RESIZE_SCALE_VC,
		.scale_lut = ISP32_BP_RESIZE_SCALE_LUT,
		.scale_lut_addr = ISP32_BP_RESIZE_SCALE_LUT_ADDR,
		.scale_hy_shd = ISP32_BP_RESIZE_SCALE_HY_SHD,
		.scale_hcr_shd = ISP32_BP_RESIZE_SCALE_HCR_SHD,
		.scale_hcb_shd = ISP32_BP_RESIZE_SCALE_HCB_SHD,
		.scale_vy_shd = ISP32_BP_RESIZE_SCALE_VY_SHD,
		.scale_vc_shd = ISP32_BP_RESIZE_SCALE_VC_SHD,
		.phase_hy = ISP32_BP_RESIZE_PHASE_HY_SHD,
		.phase_hc = ISP32_BP_RESIZE_PHASE_HC_SHD,
		.phase_vy = ISP32_BP_RESIZE_PHASE_VY_SHD,
		.phase_vc = ISP32_BP_RESIZE_PHASE_VC_SHD,
		.ctrl_shd = ISP32_BP_RESIZE_CTRL_SHD,
		.phase_hy_shd = ISP32_BP_RESIZE_PHASE_HY_SHD,
		.phase_hc_shd = ISP32_BP_RESIZE_PHASE_HC_SHD,
		.phase_vy_shd = ISP32_BP_RESIZE_PHASE_VY_SHD,
		.phase_vc_shd = ISP32_BP_RESIZE_PHASE_VC_SHD,
	},
	.dual_crop = {
		.ctrl = ISP3X_DUAL_CROP_CTRL,
		.yuvmode_mask = ISP3X_DUAL_CROP_FBC_MODE,
		.h_offset = ISP3X_DUAL_CROP_FBC_H_OFFS,
		.v_offset = ISP3X_DUAL_CROP_FBC_V_OFFS,
		.h_size = ISP3X_DUAL_CROP_FBC_H_SIZE,
		.v_size = ISP3X_DUAL_CROP_FBC_V_SIZE,
	},
	.mi = {
		.y_size_init = ISP3X_MI_BP_WR_Y_SIZE,
		.cb_size_init = ISP3X_MI_BP_WR_CB_SIZE,
		.y_base_ad_init = ISP3X_MI_BP_WR_Y_BASE,
		.cb_base_ad_init = ISP3X_MI_BP_WR_CB_BASE,
		.y_offs_cnt_init = ISP3X_MI_BP_WR_Y_OFFS_CNT,
		.cb_offs_cnt_init = ISP3X_MI_BP_WR_CB_OFFS_CNT,
		.y_base_ad_shd = ISP3X_MI_BP_WR_Y_BASE_SHD,
		.y_pic_size = ISP3X_MI_BP_WR_Y_PIC_SIZE,
	},
};

static struct stream_config rkisp_bpds_stream_config = {
	.fmts = bp_fmts,
	.fmt_size = ARRAY_SIZE(bp_fmts),
	.frame_end_id = ISP32_MI_BPDS_FRAME,
	.mi = {
		.ctrl = ISP32_MI_BPDS_WR_CTRL,
		.length = ISP32_MI_BPDS_WR_Y_LLENGTH,
		.y_size_init = ISP32_MI_BPDS_WR_Y_SIZE,
		.cb_size_init = ISP32_MI_BPDS_WR_CB_SIZE,
		.y_base_ad_init = ISP32_MI_BPDS_WR_Y_BASE,
		.cb_base_ad_init = ISP32_MI_BPDS_WR_CB_BASE,
		.y_offs_cnt_init = ISP32_MI_BPDS_WR_Y_OFFS_CNT,
		.cb_offs_cnt_init = ISP32_MI_BPDS_WR_CB_OFFS_CNT,
		.y_base_ad_shd = ISP32_MI_BPDS_WR_Y_BASE_SHD,
		.y_pic_size = ISP32_MI_BPDS_WR_Y_PIC_SIZE,
	},
};

static struct stream_config rkisp_mpds_stream_config = {
	.fmts = bp_fmts,
	.fmt_size = ARRAY_SIZE(bp_fmts),
	.frame_end_id = ISP32_MI_MPDS_FRAME,
	.mi = {
		.ctrl = ISP32_MI_MPDS_WR_CTRL,
		.length = ISP32_MI_MPDS_WR_Y_LLENGTH,
		.y_size_init = ISP32_MI_MPDS_WR_Y_SIZE,
		.cb_size_init = ISP32_MI_MPDS_WR_CB_SIZE,
		.y_base_ad_init = ISP32_MI_MPDS_WR_Y_BASE,
		.cb_base_ad_init = ISP32_MI_MPDS_WR_CB_BASE,
		.y_offs_cnt_init = ISP32_MI_MPDS_WR_Y_OFFS_CNT,
		.cb_offs_cnt_init = ISP32_MI_MPDS_WR_CB_OFFS_CNT,
		.y_base_ad_shd = ISP32_MI_MPDS_WR_Y_BASE_SHD,
		.y_pic_size = ISP32_MI_MPDS_WR_Y_PIC_SIZE,
	},
};

static bool bp_is_stream_stopped(struct rkisp_stream *stream)
{
	u32 en = ISP32_BP_EN_OUT_SHD;
	u32 reg = ISP32_MI_WR_CTRL2_SHD;
	bool is_direct = true;

	if (!stream->ispdev->hw_dev->is_single) {
		is_direct = false;
		en = ISP3X_BP_ENABLE;
		reg = ISP3X_MI_BP_WR_CTRL;
	}

	return !(rkisp_read(stream->ispdev, reg, is_direct) & en);
}

static bool bpds_is_stream_stopped(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_stream *st = &dev->cap_dev.stream[RKISP_STREAM_BP];
	u32 ret, en = ISP32_BPDS_EN_OUT_SHD;
	u32 reg = ISP32_MI_WR_CTRL2_SHD;
	bool is_direct = true;

	if (!dev->hw_dev->is_single) {
		is_direct = false;
		en = ISP32_DS_ENABLE;
		reg = ISP32_MI_BPDS_WR_CTRL;
	}
	ret = rkisp_read(dev, reg, is_direct);
	return (!st->is_pause && bp_is_stream_stopped(st)) || !(ret & en);
}

static bool mpds_is_stream_stopped(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_stream *st = &dev->cap_dev.stream[RKISP_STREAM_MP];
	u32 ret, en = ISP32_MPDS_EN_OUT_SHD;
	u32 reg = ISP32_MI_WR_CTRL2_SHD;
	bool is_direct = true;

	if (!dev->hw_dev->is_single) {
		is_direct = false;
		en = ISP32_DS_ENABLE;
		reg = ISP32_MI_MPDS_WR_CTRL;
	}
	ret = rkisp_read(dev, reg, is_direct);
	return (!st->is_pause && mp_is_stream_stopped(st)) || !(ret & en);
}

static void stream_self_update(struct rkisp_stream *stream)
{
	u32 mask = ISP3X_MPSELF_UPD | ISP3X_SPSELF_UPD |
		   ISP3X_BPSELF_UPD | ISP32_MPDSSELF_FORCE_UPD |
		   ISP32_BPDSSELF_FORCE_UPD;
	u32 val;

	switch (stream->id) {
	case RKISP_STREAM_MP:
		val = ISP3X_MPSELF_UPD;
		break;
	case RKISP_STREAM_SP:
		val = ISP3X_SPSELF_UPD;
		break;
	case RKISP_STREAM_BP:
		val = ISP3X_BPSELF_UPD;
		break;
	case RKISP_STREAM_MPDS:
		val = ISP32_MPDSSELF_FORCE_UPD;
		break;
	case RKISP_STREAM_BPDS:
		val = ISP32_BPDSSELF_FORCE_UPD;
		break;
	default:
		return;
	}

	rkisp_set_bits(stream->ispdev, ISP3X_MI_WR_CTRL2, mask, val, true);
}

static int get_stream_irq_mask(struct rkisp_stream *stream)
{
	int ret;

	switch (stream->id) {
	case RKISP_STREAM_SP:
		ret = ISP_FRAME_SP;
		break;
	case RKISP_STREAM_BP:
		ret = ISP_FRAME_BP;
		break;
	case RKISP_STREAM_MP:
		ret = ISP_FRAME_MP;
		break;
	default:
		ret = 0;
	}

	return ret;
}

/* configure dual-crop unit */
static int rkisp_stream_config_dcrop(struct rkisp_stream *stream, bool async)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_rect *dcrop = &stream->dcrop;
	struct v4l2_rect *input_win;

	/* dual-crop unit get data from isp */
	input_win = rkisp_get_isp_sd_win(&dev->isp_sdev);

	if (dcrop->width == input_win->width &&
	    dcrop->height == input_win->height &&
	    dcrop->left == 0 && dcrop->top == 0) {
		rkisp_disable_dcrop(stream, async);
		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "stream %d crop disabled\n", stream->id);
		return 0;
	}

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "stream %d crop: %dx%d -> %dx%d\n", stream->id,
		 input_win->width, input_win->height,
		 dcrop->width, dcrop->height);

	rkisp_config_dcrop(stream, dcrop, async);

	return 0;
}

/* configure scale unit */
static int rkisp_stream_config_rsz(struct rkisp_stream *stream, bool async)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_pix_format_mplane output_fmt = stream->out_fmt;
	struct ispsd_out_fmt *input_isp_fmt =
			rkisp_get_ispsd_out_fmt(&dev->isp_sdev);
	struct v4l2_rect in_y, in_c, out_y, out_c;

	if (input_isp_fmt->fmt_type == FMT_BAYER)
		goto disable;

	/* set input and output sizes for scale calculation
	 * input/output yuv422
	 */
	in_y.width = stream->dcrop.width;
	in_y.height = stream->dcrop.height;
	in_c.width = in_y.width / 2;
	in_c.height = in_y.height;

	out_y.width = output_fmt.width;
	out_y.height = output_fmt.height;
	out_c.width = out_y.width / 2;
	out_c.height = out_y.height;
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
	rkisp_config_rsz(stream, &in_y, &in_c, &out_y, &out_c, async);

	return 0;

disable:
	rkisp_disable_rsz(stream, async);

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
	u32 bus = 16, burst;
	int i;

	/* y/c base addr: burstN * bus alignment */
	cb_offs = y_size;
	cr_offs = cr_size ? (cb_size + cb_offs) : 0;

	if (!(cb_offs % (bus * 16)) && !(cr_offs % (bus * 16)))
		burst = CIF_MI_CTRL_BURST_LEN_LUM_16 |
			CIF_MI_CTRL_BURST_LEN_CHROM_16;
	else if (!(cb_offs % (bus * 8)) && !(cr_offs % (bus * 8)))
		burst = CIF_MI_CTRL_BURST_LEN_LUM_8 |
			CIF_MI_CTRL_BURST_LEN_CHROM_8;
	else
		burst = CIF_MI_CTRL_BURST_LEN_LUM_4 |
			CIF_MI_CTRL_BURST_LEN_CHROM_4;

	if (cb_offs % (bus * 4) || cr_offs % (bus * 4))
		v4l2_warn(&dev->v4l2_dev,
			"%dx%d fmt:0x%x not support, should be %d aligned\n",
			stream->out_fmt.width,
			stream->out_fmt.height,
			stream->out_fmt.pixelformat,
			(cr_offs == 0) ? bus * 4 : bus * 16);

	stream->burst = burst;
	for (i = 0; i <= RKISP_STREAM_SP; i++)
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
	struct rkisp_device *dev = stream->ispdev;
	struct capture_fmt *fmt = &stream->out_isp_fmt;
	struct v4l2_pix_format_mplane *out_fmt = &stream->out_fmt;
	u32 val, mask, height = out_fmt->height;

       /*
	* NOTE: plane_fmt[0].sizeimage is total size of all planes for single
	* memory plane formats, so calculate the size explicitly.
	*/
	if (dev->cap_dev.wrap_line) {
		height = dev->cap_dev.wrap_line;
		rkisp_clear_bits(dev, 0x1814, BIT(0), false);
	}
	val = out_fmt->plane_fmt[0].bytesperline;
	rkisp_write(dev, ISP3X_MI_MP_WR_Y_LLENGTH, val, false);
	val /= DIV_ROUND_UP(fmt->bpp[0], 8);
	val *= height;
	rkisp_write(dev, stream->config->mi.y_pic_size, val, false);
	val = out_fmt->plane_fmt[0].bytesperline * height;
	rkisp_write(dev, stream->config->mi.y_size_init, val, false);

	val = out_fmt->plane_fmt[1].sizeimage;
	if (dev->cap_dev.wrap_line)
		val = out_fmt->plane_fmt[0].bytesperline * height / 2;
	rkisp_write(dev, stream->config->mi.cb_size_init, val, false);

	val = out_fmt->plane_fmt[2].sizeimage;
	if (dev->cap_dev.wrap_line)
		val = out_fmt->plane_fmt[0].bytesperline * height / 2;
	rkisp_write(dev, stream->config->mi.cr_size_init, val, false);

	val = stream->out_isp_fmt.uv_swap ? ISP3X_MI_XTD_FORMAT_MP_UV_SWAP : 0;
	mask = ISP3X_MI_XTD_FORMAT_MP_UV_SWAP;
	rkisp_set_bits(dev, ISP3X_MI_WR_XTD_FORMAT_CTRL, mask, val, false);

	mask = ISP3X_MPFBC_FORCE_UPD | ISP3X_MP_YUV_MODE;
	val = rkisp_read_reg_cache(dev, ISP3X_MPFBC_CTRL) & ~mask;
	if (stream->out_fmt.pixelformat == V4L2_PIX_FMT_NV21 ||
	    stream->out_fmt.pixelformat == V4L2_PIX_FMT_NV12 ||
	    stream->out_fmt.pixelformat == V4L2_PIX_FMT_NV21M ||
	    stream->out_fmt.pixelformat == V4L2_PIX_FMT_NV12M ||
	    stream->out_fmt.pixelformat == V4L2_PIX_FMT_YUV420)
		val |= ISP3X_SEPERATE_YUV_CFG;
	else
		val |= ISP3X_SEPERATE_YUV_CFG | ISP3X_MP_YUV_MODE;
	rkisp_write(dev, ISP3X_MPFBC_CTRL, val, false);

	val = stream->out_isp_fmt.output_format;
	rkisp_write(dev, ISP32_MI_MP_WR_CTRL, val, false);

	val = calc_burst_len(stream) | CIF_MI_CTRL_INIT_BASE_EN |
		CIF_MI_CTRL_INIT_OFFSET_EN | CIF_MI_MP_AUTOUPDATE_ENABLE |
		stream->out_isp_fmt.write_format;
	mask = GENMASK(19, 16) | MI_CTRL_MP_FMT_MASK;
	rkisp_set_bits(dev, ISP3X_MI_WR_CTRL, mask, val, false);

	mi_frame_end_int_enable(stream);
	/* set up first buffer */
	mi_frame_end(stream);

	rkisp_write(dev, stream->config->mi.y_offs_cnt_init, 0, false);
	rkisp_write(dev, stream->config->mi.cb_offs_cnt_init, 0, false);
	rkisp_write(dev, stream->config->mi.cr_offs_cnt_init, 0, false);
	return 0;
}

static int mbus_code_sp_in_fmt(u32 in_mbus_code, u32 out_fourcc, u32 *format)
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

/*
 * configure memory interface for selfpath
 * This should only be called when stream-on
 */
static int sp_config_mi(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_pix_format_mplane *out_fmt = &stream->out_fmt;
	struct ispsd_out_fmt *input_isp_fmt =
			rkisp_get_ispsd_out_fmt(&dev->isp_sdev);
	u32 sp_in_fmt, val, mask;

	if (mbus_code_sp_in_fmt(input_isp_fmt->mbus_code,
				out_fmt->pixelformat, &sp_in_fmt)) {
		v4l2_err(&dev->v4l2_dev, "Can't find the input format\n");
		return -EINVAL;
	}

       /*
	* NOTE: plane_fmt[0].sizeimage is total size of all planes for single
	* memory plane formats, so calculate the size explicitly.
	*/
	val = stream->u.sp.y_stride;
	rkisp_write(dev, ISP3X_MI_SP_WR_Y_LLENGTH, val, false);
	val *= out_fmt->height;
	rkisp_write(dev, stream->config->mi.y_pic_size, val, false);
	val = out_fmt->plane_fmt[0].bytesperline * out_fmt->height;
	rkisp_write(dev, stream->config->mi.y_size_init, val, false);

	val = out_fmt->plane_fmt[1].sizeimage;
	rkisp_write(dev, stream->config->mi.cb_size_init, val, false);

	val = out_fmt->plane_fmt[2].sizeimage;
	rkisp_write(dev, stream->config->mi.cr_size_init, val, false);

	val = stream->out_isp_fmt.uv_swap ? ISP3X_MI_XTD_FORMAT_SP_UV_SWAP : 0;
	mask = ISP3X_MI_XTD_FORMAT_SP_UV_SWAP;
	rkisp_set_bits(dev, ISP3X_MI_WR_XTD_FORMAT_CTRL, mask, val, false);

	mask = ISP3X_MPFBC_FORCE_UPD | ISP3X_SP_YUV_MODE;
	val = rkisp_read_reg_cache(dev, ISP3X_MPFBC_CTRL) & ~mask;
	if (stream->out_fmt.pixelformat == V4L2_PIX_FMT_NV21 ||
	    stream->out_fmt.pixelformat == V4L2_PIX_FMT_NV12 ||
	    stream->out_fmt.pixelformat == V4L2_PIX_FMT_NV21M ||
	    stream->out_fmt.pixelformat == V4L2_PIX_FMT_NV12M ||
	    stream->out_fmt.pixelformat == V4L2_PIX_FMT_YUV420)
		val |= ISP3X_SEPERATE_YUV_CFG;
	else
		val |= ISP3X_SEPERATE_YUV_CFG | ISP3X_SP_YUV_MODE;
	rkisp_write(dev, ISP3X_MPFBC_CTRL, val, false);

	val = calc_burst_len(stream) | CIF_MI_CTRL_INIT_BASE_EN |
		CIF_MI_CTRL_INIT_OFFSET_EN | stream->out_isp_fmt.write_format |
		sp_in_fmt | stream->out_isp_fmt.output_format |
		CIF_MI_SP_AUTOUPDATE_ENABLE;
	mask = GENMASK(19, 16) | MI_CTRL_SP_FMT_MASK;
	rkisp_set_bits(dev, ISP3X_MI_WR_CTRL, mask, val, false);

	mi_frame_end_int_enable(stream);
	/* set up first buffer */
	mi_frame_end(stream);

	rkisp_write(dev, stream->config->mi.y_offs_cnt_init, 0, false);
	rkisp_write(dev, stream->config->mi.cb_offs_cnt_init, 0, false);
	rkisp_write(dev, stream->config->mi.cr_offs_cnt_init, 0, false);
	return 0;
}

static int bp_config_mi(struct rkisp_stream *stream)
{
	struct v4l2_pix_format_mplane *out_fmt = &stream->out_fmt;
	struct capture_fmt *fmt = &stream->out_isp_fmt;
	struct rkisp_device *dev = stream->ispdev;
	u32 val, mask;

       /*
	* NOTE: plane_fmt[0].sizeimage is total size of all planes for single
	* memory plane formats, so calculate the size explicitly.
	*/
	val = out_fmt->plane_fmt[0].bytesperline;
	rkisp_write(dev, ISP3X_MI_BP_WR_Y_LLENGTH, val, false);
	val /= DIV_ROUND_UP(fmt->bpp[0], 8);
	val *= out_fmt->height;
	rkisp_write(dev, stream->config->mi.y_pic_size, val, false);
	val = out_fmt->plane_fmt[0].bytesperline * out_fmt->height;
	rkisp_write(dev, stream->config->mi.y_size_init, val, false);

	val = out_fmt->plane_fmt[1].sizeimage;
	rkisp_write(dev, stream->config->mi.cb_size_init, val, false);

	mask = ISP3X_MPFBC_FORCE_UPD | ISP3X_BP_YUV_MODE;
	val = rkisp_read_reg_cache(dev, ISP3X_MPFBC_CTRL) & ~mask;

	if (out_fmt->pixelformat == V4L2_PIX_FMT_NV12 ||
	    out_fmt->pixelformat == V4L2_PIX_FMT_NV12M)
		val |= ISP3X_SEPERATE_YUV_CFG;
	else
		val |= ISP3X_SEPERATE_YUV_CFG | ISP3X_BP_YUV_MODE;
	rkisp_write(dev, ISP3X_MPFBC_CTRL, val, false);
	val = CIF_MI_CTRL_INIT_BASE_EN | CIF_MI_CTRL_INIT_OFFSET_EN;
	rkisp_set_bits(dev, ISP3X_MI_WR_CTRL, 0, val, false);
	mi_frame_end_int_enable(stream);
	/* set up first buffer */
	mi_frame_end(stream);

	rkisp_write(dev, stream->config->mi.y_offs_cnt_init, 0, false);
	rkisp_write(dev, stream->config->mi.cb_offs_cnt_init, 0, false);
	return 0;
}

static int ds_config_mi(struct rkisp_stream *stream)
{
	struct capture_fmt *fmt = &stream->out_isp_fmt;
	struct v4l2_pix_format_mplane *out_fmt = &stream->out_fmt;
	struct rkisp_device *dev = stream->ispdev;
	u32 val;

	val = out_fmt->plane_fmt[0].bytesperline;
	rkisp_write(dev, stream->config->mi.length, val, false);
	val /= DIV_ROUND_UP(fmt->bpp[0], 8);
	val *= out_fmt->height;
	rkisp_write(dev, stream->config->mi.y_pic_size, val, false);
	val = out_fmt->plane_fmt[0].bytesperline * out_fmt->height;
	rkisp_write(dev, stream->config->mi.y_size_init, val, false);

	val = out_fmt->plane_fmt[1].sizeimage;
	rkisp_write(dev, stream->config->mi.cb_size_init, val, false);

	val = CIF_MI_CTRL_INIT_BASE_EN | CIF_MI_CTRL_INIT_OFFSET_EN;
	rkisp_set_bits(dev, ISP3X_MI_WR_CTRL, 0, val, false);

	mi_frame_end_int_enable(stream);

	mi_frame_end(stream);

	rkisp_write(dev, stream->config->mi.y_offs_cnt_init, 0, false);
	rkisp_write(dev, stream->config->mi.cb_offs_cnt_init, 0, false);
	return 0;
}

static void mp_enable_mi(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_stream *t = &dev->cap_dev.stream[stream->conn_id];
	struct capture_fmt *isp_fmt = &stream->out_isp_fmt;
	u32 mask = CIF_MI_CTRL_MP_ENABLE | CIF_MI_CTRL_RAW_ENABLE;
	u32 val = CIF_MI_CTRL_MP_ENABLE;

	if (isp_fmt->fmt_type == FMT_BAYER)
		val = CIF_MI_CTRL_RAW_ENABLE;
	rkisp_set_bits(stream->ispdev, ISP3X_MI_WR_CTRL, mask, val, false);

	/* enable bpds path output */
	if (t->streaming && !t->is_pause)
		t->ops->enable_mi(t);
}

static void sp_enable_mi(struct rkisp_stream *stream)
{
	rkisp_set_bits(stream->ispdev, ISP3X_MI_WR_CTRL,
			0, CIF_MI_CTRL_SP_ENABLE, false);
}

static void bp_enable_mi(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_stream *t = &dev->cap_dev.stream[stream->conn_id];

	u32 val = stream->out_isp_fmt.write_format |
		  stream->out_isp_fmt.output_format |
		  ISP3X_BP_ENABLE | ISP3X_BP_AUTO_UPD;

	rkisp_write(stream->ispdev, ISP3X_MI_BP_WR_CTRL, val, false);

	/* enable bpds path output */
	if (t->streaming && !t->is_pause)
		t->ops->enable_mi(t);
}

static void ds_enable_mi(struct rkisp_stream *stream)
{
	u32 val = stream->out_isp_fmt.write_format |
		  stream->out_isp_fmt.output_format |
		  ISP32_DS_ENABLE | ISP32_DS_AUTO_UPD;

	rkisp_write(stream->ispdev, stream->config->mi.ctrl, val, false);
}

static void mp_disable_mi(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_stream *t = &dev->cap_dev.stream[stream->conn_id];
	u32 mask = CIF_MI_CTRL_MP_ENABLE | CIF_MI_CTRL_RAW_ENABLE;

	rkisp_set_bits(dev, 0x1814, 0, BIT(0), false);
	rkisp_clear_bits(stream->ispdev, ISP3X_MI_WR_CTRL, mask, false);

	/* disable mpds path output */
	if (!stream->is_pause && t->streaming)
		t->ops->disable_mi(t);
}

static void sp_disable_mi(struct rkisp_stream *stream)
{
	rkisp_clear_bits(stream->ispdev, ISP3X_MI_WR_CTRL, CIF_MI_CTRL_SP_ENABLE, false);
}

static void bp_disable_mi(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_stream *t = &dev->cap_dev.stream[stream->conn_id];

	rkisp_clear_bits(stream->ispdev, ISP3X_MI_BP_WR_CTRL, ISP3X_BP_ENABLE, false);

	/* disable bpds path output */
	if (!stream->is_pause && t->streaming)
		t->ops->disable_mi(t);
}

static void ds_disable_mi(struct rkisp_stream *stream)
{
	rkisp_clear_bits(stream->ispdev, stream->config->mi.ctrl, ISP32_DS_ENABLE, false);
}

static void update_mi(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_dummy_buffer *dummy_buf = &stream->dummy_buf;
	u32 val, reg;
	bool is_cr_cfg = false;

	if (stream->id == RKISP_STREAM_MP || stream->id == RKISP_STREAM_SP)
		is_cr_cfg = true;

	if (stream->next_buf) {
		reg = stream->config->mi.y_base_ad_init;
		val = stream->next_buf->buff_addr[RKISP_PLANE_Y];
		rkisp_write(dev, reg, val, false);

		reg = stream->config->mi.cb_base_ad_init;
		val = stream->next_buf->buff_addr[RKISP_PLANE_CB];
		rkisp_write(dev, reg, val, false);

		if (is_cr_cfg) {
			reg = stream->config->mi.cr_base_ad_init;
			val = stream->next_buf->buff_addr[RKISP_PLANE_CR];
			rkisp_write(dev, reg, val, false);
		}

		if (stream->is_pause) {
			/* single sensor mode with pingpong buffer:
			 * if mi on, addr will auto update at frame end
			 * else addr need update by SELF_UPD.
			 *
			 * multi sensor mode with single buffer:
			 * mi and buffer will update by readback.
			 */
			if (dev->hw_dev->is_single &&
			    stream->ops->is_stream_stopped(stream)) {
				/* isp no start and mi close, force to enable it */
				if (!ISP3X_ISP_OUT_LINE(rkisp_read(dev, ISP3X_ISP_DEBUG2, true))) {
					stream->ops->enable_mi(stream);
					stream->is_pause = false;
					stream_self_update(stream);
					if (!stream->curr_buf) {
						stream->curr_buf = stream->next_buf;
						stream->next_buf = NULL;
					}
				}
			}
			if (stream->is_pause) {
				stream->ops->enable_mi(stream);
				stream->is_pause = false;
			}
		}

		/* single buf force updated at readback for multidevice */
		if (!dev->hw_dev->is_single) {
			stream->curr_buf = stream->next_buf;
			stream->next_buf = NULL;
		}
	} else if (dummy_buf->mem_priv) {
		/* wrap buf ENC */
		val = dummy_buf->dma_addr;
		reg = stream->config->mi.y_base_ad_init;
		rkisp_write(dev, reg, val, false);
		val += stream->out_fmt.plane_fmt[0].bytesperline * dev->cap_dev.wrap_line;
		reg = stream->config->mi.cb_base_ad_init;
		rkisp_write(dev, reg, val, false);
		if (is_cr_cfg) {
			reg = stream->config->mi.cr_base_ad_init;
			rkisp_write(dev, reg, val, false);
		}
	} else if (stream->is_using_resmem) {
		/* resmem for fast stream NV12 output */
		dma_addr_t max_addr = dev->resmem_addr + dev->resmem_size;
		u32 bytesperline = stream->out_fmt.plane_fmt[0].bytesperline;
		u32 buf_size = bytesperline * ALIGN(stream->out_fmt.height, 16) * 3 / 2;

		reg = stream->config->mi.y_base_ad_init;
		val = dev->resmem_addr_curr;
		rkisp_write(dev, reg, val, false);

		reg = stream->config->mi.cb_base_ad_init;
		val += bytesperline * stream->out_fmt.height;
		rkisp_write(dev, reg, val, false);

		if (dev->resmem_addr_curr + buf_size * 2 <= max_addr)
			dev->resmem_addr_curr += buf_size;
	} else if (!stream->is_pause) {
		stream->is_pause = true;
		stream->ops->disable_mi(stream);
		/* no buf, force to close mi */
		if (!stream->curr_buf)
			stream_self_update(stream);
	}

	v4l2_dbg(2, rkisp_debug, &dev->v4l2_dev,
		 "%s stream:%d Y:0x%x CB:0x%x | Y_SHD:0x%x\n",
		 __func__, stream->id,
		 rkisp_read(dev, stream->config->mi.y_base_ad_init, false),
		 rkisp_read(dev, stream->config->mi.cb_base_ad_init, false),
		 rkisp_read(dev, stream->config->mi.y_base_ad_shd, true));
}

static int set_mirror_flip(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	u32 val = 0;

	if (!stream->is_mf_upd)
		return 0;

	stream->is_mf_upd = false;
	if (dev->cap_dev.is_mirror)
		rkisp_set_bits(dev, ISP3X_ISP_CTRL0, 0, ISP32_MIR_ENABLE, false);
	else
		rkisp_clear_bits(dev, ISP3X_ISP_CTRL0, ISP32_MIR_ENABLE, false);

	switch (stream->id) {
	case RKISP_STREAM_SP:
		val = ISP32_SP_WR_V_FLIP;
		break;
	case RKISP_STREAM_BP:
		val = ISP32_BP_WR_V_FLIP;
		break;
	case RKISP_STREAM_MPDS:
		val = ISP32_MPDS_WR_V_FLIP;
		break;
	case RKISP_STREAM_BPDS:
		val = ISP32_BPDS_WR_V_FIIP;
		break;
	default:
		val = ISP32_MP_WR_V_FLIP;
		if (dev->cap_dev.wrap_line) {
			stream->is_flip = false;
			v4l2_warn(&dev->v4l2_dev, "flip not support width wrap function\n");
			return -EINVAL;
		}
	}

	if (stream->is_flip)
		rkisp_set_bits(dev, ISP32_MI_WR_VFLIP_CTRL, 0, val, false);
	else
		rkisp_clear_bits(dev, ISP32_MI_WR_VFLIP_CTRL, val, false);
	return 0;
}

static void luma_frame_readout(unsigned long arg)
{
	struct rkisp_stream *stream =
		(struct rkisp_stream *)arg;
	struct rkisp_device *dev = stream->ispdev;
	unsigned long lock_flags = 0;
	int timeout = 8;
	u32 i, val, *data, seq;
	u64 ns = 0;

	stream->frame_end = false;
	if (stream->stopping)
		goto end;

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (!stream->curr_buf && !list_empty(&stream->buf_queue)) {
		stream->curr_buf = list_first_entry(&stream->buf_queue,
						    struct rkisp_buffer, queue);
		list_del(&stream->curr_buf->queue);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);

	if (!stream->curr_buf) {
		v4l2_warn(&dev->v4l2_dev, "%s no buf\n", __func__);
		return;
	}

	rkisp_write(dev, ISP32_YNR_LUMA_RCTRL, ISP32_YNR_LUMA_RDBK_ST, true);
	rkisp_dmarx_get_frame(dev, &seq, NULL, &ns, true);
	while (timeout--) {
		val = rkisp_read(dev, ISP32_YNR_LUMA_RCTRL, true);
		if (val & ISP32_YNR_LUMA_RDBK_RDY)
			break;
	}
	if (!timeout) {
		v4l2_err(&dev->v4l2_dev, "%s no ready\n", __func__);
		return;
	}

	val = stream->out_fmt.width * stream->out_fmt.height / 4;
	data = stream->curr_buf->vaddr[0];
	for (i = 0; i < val; i++) {
		*data = rkisp_read(dev, ISP32_YNR_LUMA_RDATA, true);
		data++;
	}
	if (!ns)
		ns = ktime_get_ns();
	stream->curr_buf->vb.vb2_buf.timestamp = ns;
	stream->curr_buf->vb.sequence = seq;
	vb2_set_plane_payload(&stream->curr_buf->vb.vb2_buf, 0, val * 4);
	vb2_buffer_done(&stream->curr_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	stream->curr_buf = NULL;
end:
	stream->frame_end = true;
	if (stream->stopping) {
		stream->stopping = false;
		stream->streaming = false;
		wake_up(&stream->done);
	}
}

static int luma_frame_end(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	u32 val;

	if (!stream->streaming)
		return 0;

	val = rkisp_read(dev, ISP3X_YNR_GLOBAL_CTRL, true);
	if (!(val & ISP3X_YNR_EN_SHD)) {
		v4l2_warn(&dev->v4l2_dev, "%s YNR(0x%x) is off\n", __func__, val);
		return -EINVAL;
	}

	if (IS_HDR_RDBK(dev->rd_mode))
		luma_frame_readout((unsigned long)stream);
	else
		tasklet_schedule(&dev->cap_dev.rd_tasklet);

	return 0;
}

static int mp_set_wrap(struct rkisp_stream *stream, int line)
{
	struct rkisp_device *dev = stream->ispdev;
	int ret = 0;

	dev->cap_dev.wrap_line = line;
	if (stream->is_pre_on &&
	    stream->streaming &&
	    !stream->dummy_buf.mem_priv) {
		ret = rkisp_create_dummy_buf(stream);
		if (ret)
			return ret;
		stream->ops->config_mi(stream);
		if (stream->is_pause) {
			stream->ops->enable_mi(stream);
			if (!ISP3X_ISP_OUT_LINE(rkisp_read(dev, ISP3X_ISP_DEBUG2, true)))
				stream_self_update(stream);
			stream->is_pause = false;
		}
	}
	return ret;
}

static struct streams_ops rkisp_mp_streams_ops = {
	.config_mi = mp_config_mi,
	.enable_mi = mp_enable_mi,
	.disable_mi = mp_disable_mi,
	.set_data_path = stream_data_path,
	.is_stream_stopped = mp_is_stream_stopped,
	.update_mi = update_mi,
	.frame_end = mi_frame_end,
	.frame_start = mi_frame_start,
	.set_wrap = mp_set_wrap,
};

static struct streams_ops rkisp_sp_streams_ops = {
	.config_mi = sp_config_mi,
	.enable_mi = sp_enable_mi,
	.disable_mi = sp_disable_mi,
	.set_data_path = stream_data_path,
	.is_stream_stopped = sp_is_stream_stopped,
	.update_mi = update_mi,
	.frame_end = mi_frame_end,
	.frame_start = mi_frame_start,
};

static struct streams_ops rkisp_bp_streams_ops = {
	.config_mi = bp_config_mi,
	.enable_mi = bp_enable_mi,
	.disable_mi = bp_disable_mi,
	.is_stream_stopped = bp_is_stream_stopped,
	.update_mi = update_mi,
	.frame_end = mi_frame_end,
	.frame_start = mi_frame_start,
};

static struct streams_ops rkisp_bpds_streams_ops = {
	.config_mi = ds_config_mi,
	.enable_mi = ds_enable_mi,
	.disable_mi = ds_disable_mi,
	.is_stream_stopped = bpds_is_stream_stopped,
	.update_mi = update_mi,
	.frame_end = mi_frame_end,
	.frame_start = mi_frame_start,
};

static struct streams_ops rkisp_mpds_streams_ops = {
	.config_mi = ds_config_mi,
	.enable_mi = ds_enable_mi,
	.disable_mi = ds_disable_mi,
	.is_stream_stopped = mpds_is_stream_stopped,
	.update_mi = update_mi,
	.frame_end = mi_frame_end,
	.frame_start = mi_frame_start,
};

static struct streams_ops rkisp_luma_streams_ops = {
	.frame_end = luma_frame_end,
};

static int mi_frame_start(struct rkisp_stream *stream, u32 mis)
{
	unsigned long lock_flags = 0;

	if (mis && stream->streaming) {
		rkisp_rockit_buf_done(stream, ROCKIT_DVBM_START);
		rkisp_rockit_ctrl_fps(stream);
	}

	/* readback start to update stream buf if null */
	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (stream->streaming) {
		/* only dynamic clipping and scaling at readback */
		if (!mis && stream->is_crop_upd) {
			rkisp_stream_config_dcrop(stream, false);
			rkisp_stream_config_rsz(stream, false);
			stream->is_crop_upd = false;
		}
		/* update buf for mulit sensor at readback */
		if (!mis && !stream->ispdev->hw_dev->is_single &&
		    !stream->curr_buf &&
		    !list_empty(&stream->buf_queue)) {
			stream->next_buf = list_first_entry(&stream->buf_queue,
							struct rkisp_buffer, queue);
			list_del(&stream->next_buf->queue);
			stream->ops->update_mi(stream);
		}
		/* check frame loss */
		if (mis && stream->ops->is_stream_stopped(stream))
			stream->dbg.frameloss++;
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);

	return 0;
}

/*
 * This function is called when a frame end come. The next frame
 * is processing and we should set up buffer for next-next frame,
 * otherwise it will overflow.
 */
static int mi_frame_end(struct rkisp_stream *stream)
{
	struct capture_fmt *isp_fmt = &stream->out_isp_fmt;
	unsigned long lock_flags = 0;
	u32 i;

	set_mirror_flip(stream);

	if (stream->curr_buf) {
		struct vb2_buffer *vb2_buf = &stream->curr_buf->vb.vb2_buf;

		for (i = 0; i < isp_fmt->mplanes; i++) {
			u32 payload_size = stream->out_fmt.plane_fmt[i].sizeimage;

			vb2_set_plane_payload(vb2_buf, i, payload_size);
		}

		if (vb2_buf->memory)
			vb2_buffer_done(vb2_buf, VB2_BUF_STATE_DONE);
		else
			rkisp_rockit_buf_done(stream, ROCKIT_DVBM_END);
	}

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	stream->curr_buf = stream->next_buf;
	stream->next_buf = NULL;
	if (!list_empty(&stream->buf_queue)) {
		stream->next_buf = list_first_entry(&stream->buf_queue,
						    struct rkisp_buffer, queue);
		list_del(&stream->next_buf->queue);
	}
	stream->ops->update_mi(stream);
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
	return 0;
}

/***************************** vb2 operations*******************************/

/*
 * Set flags and wait, it should stop in interrupt.
 * If it didn't, stop it by force.
 */
static void rkisp_stream_stop(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	int ret = 0;

	stream->stopping = true;
	stream->is_pause = false;
	if (dev->hw_dev->is_single && stream->ops->disable_mi)
		stream->ops->disable_mi(stream);
	if (dev->isp_state & ISP_START &&
	    !stream->ops->is_stream_stopped(stream)) {
		ret = wait_event_timeout(stream->done,
					 !stream->streaming,
					 msecs_to_jiffies(500));
		if (!ret)
			v4l2_warn(v4l2_dev, "%s id:%d timeout\n",
				  __func__, stream->id);
	}

	stream->stopping = false;
	stream->streaming = false;
	if (stream->ops->disable_mi)
		stream->ops->disable_mi(stream);
	if (stream->id == RKISP_STREAM_MP ||
	    stream->id == RKISP_STREAM_SP ||
	    stream->id == RKISP_STREAM_BP) {
		rkisp_disable_dcrop(stream, true);
		rkisp_disable_rsz(stream, true);
	}
	ret = get_stream_irq_mask(stream);
	dev->irq_ends_mask &= ~ret;

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
	struct rkisp_device *dev = stream->ispdev;
	bool is_update = atomic_read(&dev->cap_dev.refcnt) > 1 ? false : true;
	int ret;

	if (stream->ops->set_data_path)
		stream->ops->set_data_path(stream);
	if (stream->ops->config_mi) {
		ret = stream->ops->config_mi(stream);
		if (ret)
			return ret;
	}
	if (stream->ops->enable_mi)
		stream->ops->enable_mi(stream);

	stream_self_update(stream);

	if (is_update)
		dev->irq_ends_mask |= get_stream_irq_mask(stream);
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
		u32 height = pixm->height;

		if (dev->cap_dev.wrap_line && stream->id == RKISP_STREAM_MP)
			height = dev->cap_dev.wrap_line;
		plane_fmt = &pixm->plane_fmt[i];
		/* height to align with 16 when allocating memory
		 * so that Rockchip encoder can use DMA buffer directly
		 */
		sizes[i] = (isp_fmt->fmt_type == FMT_YUV) ?
			plane_fmt->sizeimage / height *
			ALIGN(height, 16) :
			plane_fmt->sizeimage;
	}

	rkisp_chk_tb_over(dev);
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev, "%s %s count %d, size %d\n",
		 stream->vnode.vdev.name, v4l2_type_names[queue->type], *num_buffers, sizes[0]);

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
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_pix_format_mplane *pixm = &stream->out_fmt;
	struct capture_fmt *isp_fmt = &stream->out_isp_fmt;
	unsigned long lock_flags = 0;
	struct sg_table *sgt;
	u32 height, offset;
	int i;

	memset(ispbuf->buff_addr, 0, sizeof(ispbuf->buff_addr));
	for (i = 0; i < isp_fmt->mplanes; i++) {
		ispbuf->vaddr[i] = vb2_plane_vaddr(vb, i);

		if (stream->ispdev->hw_dev->is_dma_sg_ops) {
			sgt = vb2_dma_sg_plane_desc(vb, i);
			ispbuf->buff_addr[i] = sg_dma_address(sgt->sgl);
		} else {
			ispbuf->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);
		}
	}
	/*
	 * NOTE: plane_fmt[0].sizeimage is total size of all planes for single
	 * memory plane formats, so calculate the size explicitly.
	 */
	if (isp_fmt->mplanes == 1) {
		for (i = 0; i < isp_fmt->cplanes - 1; i++) {
			height = pixm->height;
			if (dev->cap_dev.wrap_line && stream->id == RKISP_STREAM_MP)
				height = dev->cap_dev.wrap_line;
			offset = (i == 0) ?
				pixm->plane_fmt[i].bytesperline * height :
				pixm->plane_fmt[i].sizeimage;
			ispbuf->buff_addr[i + 1] =
				ispbuf->buff_addr[i] + offset;
		}
	}

	v4l2_dbg(2, rkisp_debug, &dev->v4l2_dev,
		 "stream:%d queue buf:0x%x\n",
		 stream->id, ispbuf->buff_addr[0]);

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	/* single sensor with pingpong buf, update next if need */
	if (stream->ispdev->hw_dev->is_single &&
	    stream->id != RKISP_STREAM_VIR &&
	    stream->id != RKISP_STREAM_LUMA &&
	    stream->streaming && !stream->next_buf) {
		stream->next_buf = ispbuf;
		stream->ops->update_mi(stream);
	} else {
		list_add_tail(&ispbuf->queue, &stream->buf_queue);
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static int rkisp_create_dummy_buf(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_dummy_buffer *buf = &stream->dummy_buf;
	int ret;

	/* mainpath for warp default */
	if (!dev->cap_dev.wrap_line || stream->id != RKISP_STREAM_MP)
		return 0;

	buf->size = dev->isp_sdev.in_crop.width * dev->cap_dev.wrap_line * 2;
	if (stream->out_isp_fmt.output_format == ISP32_MI_OUTPUT_YUV420)
		buf->size = buf->size - buf->size / 4;
	buf->size = stream->out_fmt.plane_fmt[0].sizeimage;
	buf->is_need_dbuf = true;
	ret = rkisp_alloc_buffer(stream->ispdev, buf);
	if (ret == 0) {
		ret = rkisp_dvbm_init(stream);
		if (ret < 0)
			rkisp_free_buffer(dev, buf);
	}

	return ret;
}

static void rkisp_destroy_dummy_buf(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;

	if (!dev->cap_dev.wrap_line || stream->id != RKISP_STREAM_MP)
		return;
	rkisp_dvbm_deinit();
	rkisp_free_buffer(dev, &stream->dummy_buf);
}

static void destroy_buf_queue(struct rkisp_stream *stream,
			      enum vb2_buffer_state state)
{
	unsigned long lock_flags = 0;
	struct rkisp_buffer *buf;

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
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
		if (buf->vb.vb2_buf.memory)
			vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	rkisp_rockit_buf_free(stream);
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static void rkisp_stop_streaming(struct vb2_queue *queue)
{
	struct rkisp_stream *stream = queue->drv_priv;
	struct rkisp_vdev_node *node = &stream->vnode;
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	int ret;

	mutex_lock(&dev->hw_dev->dev_lock);

	v4l2_dbg(1, rkisp_debug, v4l2_dev, "%s %s %d\n",
		 __func__, node->vdev.name, stream->id);

	if (!stream->streaming)
		goto end;

	if (stream->id == RKISP_STREAM_LUMA) {
		stream->stopping = true;
		wait_event_timeout(stream->done,
				   stream->frame_end,
				   msecs_to_jiffies(500));
		stream->streaming = false;
		stream->stopping = false;
		destroy_buf_queue(stream, VB2_BUF_STATE_ERROR);
		tasklet_disable(&dev->cap_dev.rd_tasklet);
		goto end;
	}

	rkisp_stream_stop(stream);
	/* call to the other devices */
	media_pipeline_stop(&node->vdev.entity);
	ret = dev->pipe.set_stream(&dev->pipe, false);
	if (ret < 0)
		v4l2_err(v4l2_dev, "pipeline stream-off failed:%d\n", ret);

	/* release buffers */
	destroy_buf_queue(stream, VB2_BUF_STATE_ERROR);

	ret = dev->pipe.close(&dev->pipe);
	if (ret < 0)
		v4l2_err(v4l2_dev, "pipeline close failed error:%d\n", ret);
	rkisp_destroy_dummy_buf(stream);
	atomic_dec(&dev->cap_dev.refcnt);

end:
	mutex_unlock(&dev->hw_dev->dev_lock);

	if (stream->is_pre_on) {
		stream->is_pre_on = false;
		v4l2_pipeline_pm_put(&stream->vnode.vdev.entity);
	}
}

static int rkisp_stream_start(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	bool async = false;
	int ret;

	if (stream->id == RKISP_STREAM_MPDS || stream->id == RKISP_STREAM_BPDS)
		goto end;

	async = (dev->cap_dev.stream[RKISP_STREAM_MP].streaming ||
		 dev->cap_dev.stream[RKISP_STREAM_SP].streaming ||
		 dev->cap_dev.stream[RKISP_STREAM_BP].streaming);

	/*
	 * can't be async now, otherwise the latter started stream fails to
	 * produce mi interrupt.
	 */
	ret = rkisp_stream_config_dcrop(stream, false);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "config dcrop failed with error %d\n", ret);
		return ret;
	}

	ret = rkisp_stream_config_rsz(stream, async);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "config rsz failed with error %d\n", ret);
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
	int ret = -EINVAL;

	mutex_lock(&dev->hw_dev->dev_lock);

	v4l2_dbg(1, rkisp_debug, v4l2_dev, "%s %s id:%d\n",
		 __func__, node->vdev.name, stream->id);

	if (WARN_ON(stream->streaming)) {
		mutex_unlock(&dev->hw_dev->dev_lock);
		if (stream->is_pre_on)
			return 0;
		else
			return -EBUSY;
	}

	memset(&stream->dbg, 0, sizeof(stream->dbg));

	if (stream->id == RKISP_STREAM_LUMA) {
		tasklet_enable(&dev->cap_dev.rd_tasklet);
		stream->streaming = true;
		goto end;
	}

	atomic_inc(&dev->cap_dev.refcnt);
	if (!dev->isp_inp || !stream->linked) {
		v4l2_err(v4l2_dev, "check %s link or isp input\n", node->vdev.name);
		goto buffer_done;
	}

	if (stream->id == RKISP_STREAM_MPDS || stream->id == RKISP_STREAM_BPDS) {
		struct rkisp_stream *t = &dev->cap_dev.stream[stream->conn_id];

		if (!t->streaming) {
			v4l2_err(v4l2_dev, "%s from %s no start\n",
				 node->vdev.name, t->vnode.vdev.name);
			goto buffer_done;
		}
	}

	if (atomic_read(&dev->cap_dev.refcnt) == 1 &&
	    (dev->isp_inp & INP_CIF)) {
		/* update sensor info when first streaming */
		ret = rkisp_update_sensor_info(dev);
		if (ret < 0) {
			v4l2_err(v4l2_dev, "update sensor info failed %d\n", ret);
			goto buffer_done;
		}
	}

	ret = rkisp_create_dummy_buf(stream);
	if (ret < 0)
		goto buffer_done;

	if (count == 0 && !stream->dummy_buf.mem_priv &&
	    list_empty(&stream->buf_queue)) {
		v4l2_err(v4l2_dev, "no buf for %s\n", node->vdev.name);
		ret = -EINVAL;
		goto buffer_done;
	}

	/* enable clocks/power-domains */
	ret = dev->pipe.open(&dev->pipe, &node->vdev.entity, true);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "open isp pipeline failed %d\n", ret);
		goto destroy_dummy_buf;
	}

	/* configure stream hardware to start */
	ret = rkisp_stream_start(stream);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "start %s failed\n", node->vdev.name);
		goto close_pipe;
	}

	/* start sub-devices */
	ret = dev->pipe.set_stream(&dev->pipe, true);
	if (ret < 0)
		goto stop_stream;

	ret = media_pipeline_start(&node->vdev.entity, &dev->pipe.pipe);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "start pipeline failed %d\n", ret);
		goto pipe_stream_off;
	}
end:
	mutex_unlock(&dev->hw_dev->dev_lock);
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
	mutex_unlock(&dev->hw_dev->dev_lock);
	return ret;
}

static const struct vb2_ops rkisp_vb2_ops = {
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
	q->mem_ops = stream->ispdev->hw_dev->mem_ops;
	q->buf_struct_size = sizeof(struct rkisp_buffer);
	q->min_buffers_needed = CIF_ISP_REQ_BUFS_MIN;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &stream->apilock;
	q->dev = stream->ispdev->hw_dev->dev;
	q->allow_cache_hints = 1;
	q->bidirectional = 1;
	if (stream->ispdev->hw_dev->is_dma_contig)
		q->dma_attrs = DMA_ATTR_FORCE_CONTIGUOUS;
	q->gfp_flags = GFP_DMA32;
	return vb2_queue_init(q);
}

static int rkisp_stream_init(struct rkisp_device *dev, u32 id)
{
	struct rkisp_capture_device *cap_dev = &dev->cap_dev;
	struct rkisp_stream *stream;
	struct video_device *vdev;
	struct rkisp_vdev_node *node;
	int ret = 0;

	stream = &cap_dev->stream[id];
	stream->id = id;
	stream->ispdev = dev;
	vdev = &stream->vnode.vdev;

	INIT_LIST_HEAD(&stream->buf_queue);
	init_waitqueue_head(&stream->done);
	spin_lock_init(&stream->vbq_lock);
	stream->linked = true;

	switch (id) {
	case RKISP_STREAM_SP:
		strscpy(vdev->name, SP_VDEV_NAME, sizeof(vdev->name));
		stream->ops = &rkisp_sp_streams_ops;
		stream->config = &rkisp_sp_stream_config;
		break;
	case RKISP_STREAM_BP:
		strscpy(vdev->name, BP_VDEV_NAME, sizeof(vdev->name));
		stream->ops = &rkisp_bp_streams_ops;
		stream->config = &rkisp_bp_stream_config;
		stream->conn_id = RKISP_STREAM_BPDS;
		break;
	case RKISP_STREAM_BPDS:
		strscpy(vdev->name, BPDS_VDEV_NAME, sizeof(vdev->name));
		stream->ops = &rkisp_bpds_streams_ops;
		stream->config = &rkisp_bpds_stream_config;
		stream->conn_id = RKISP_STREAM_BP;
		break;
	case RKISP_STREAM_MPDS:
		strscpy(vdev->name, MPDS_VDEV_NAME, sizeof(vdev->name));
		stream->ops = &rkisp_mpds_streams_ops;
		stream->config = &rkisp_mpds_stream_config;
		stream->conn_id = RKISP_STREAM_MP;
		break;
	case RKISP_STREAM_LUMA:
		strscpy(vdev->name, LUMA_VDEV_NAME, sizeof(vdev->name));
		stream->ops = &rkisp_luma_streams_ops;
		stream->config = &rkisp_luma_stream_config;
		stream->conn_id = RKISP_STREAM_LUMA;
		tasklet_init(&cap_dev->rd_tasklet,
			     luma_frame_readout,
			     (unsigned long)stream);
		tasklet_disable(&cap_dev->rd_tasklet);
		break;
	default:
		strscpy(vdev->name, MP_VDEV_NAME, sizeof(vdev->name));
		stream->ops = &rkisp_mp_streams_ops;
		stream->config = &rkisp_mp_stream_config;
		stream->conn_id = RKISP_STREAM_MPDS;
	}

	rockit_isp_ops.rkisp_stream_start = rkisp_stream_start;
	rockit_isp_ops.rkisp_stream_stop = rkisp_stream_stop;

	node = vdev_to_node(vdev);
	rkisp_init_vb2_queue(&node->buf_queue, stream,
			     V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
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

int rkisp_register_stream_v32(struct rkisp_device *dev)
{
	struct rkisp_capture_device *cap_dev = &dev->cap_dev;
	int ret;

	rkisp_dvbm_get(dev);

	rkisp_rockit_dev_init(dev);

	ret = rkisp_stream_init(dev, RKISP_STREAM_MP);
	if (ret < 0)
		goto err;
	ret = rkisp_stream_init(dev, RKISP_STREAM_SP);
	if (ret < 0)
		goto err_free_mp;
	ret = rkisp_stream_init(dev, RKISP_STREAM_BP);
	if (ret < 0)
		goto err_free_sp;
	ret = rkisp_stream_init(dev, RKISP_STREAM_MPDS);
	if (ret < 0)
		goto err_free_bp;
	ret = rkisp_stream_init(dev, RKISP_STREAM_BPDS);
	if (ret < 0)
		goto err_free_mpds;
	ret = rkisp_stream_init(dev, RKISP_STREAM_LUMA);
	if (ret < 0)
		goto err_free_bpds;
	return 0;
err_free_bpds:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_BPDS]);
err_free_mpds:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_MPDS]);
err_free_bp:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_BP]);
err_free_sp:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_SP]);
err_free_mp:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_MP]);
err:
	return ret;
}

void rkisp_unregister_stream_v32(struct rkisp_device *dev)
{
	struct rkisp_capture_device *cap_dev = &dev->cap_dev;
	struct rkisp_stream *stream;

	stream = &cap_dev->stream[RKISP_STREAM_MP];
	rkisp_unregister_stream_vdev(stream);
	stream = &cap_dev->stream[RKISP_STREAM_SP];
	rkisp_unregister_stream_vdev(stream);
	stream = &cap_dev->stream[RKISP_STREAM_BP];
	rkisp_unregister_stream_vdev(stream);
	stream = &cap_dev->stream[RKISP_STREAM_MPDS];
	rkisp_unregister_stream_vdev(stream);
	stream = &cap_dev->stream[RKISP_STREAM_BPDS];
	rkisp_unregister_stream_vdev(stream);
	stream = &cap_dev->stream[RKISP_STREAM_LUMA];
	rkisp_unregister_stream_vdev(stream);
}

/****************  Interrupter Handler ****************/

void rkisp_mi_v32_isr(u32 mis_val, struct rkisp_device *dev)
{
	struct rkisp_stream *stream;
	unsigned int i, seq;
	u64 ns = 0;

	v4l2_dbg(3, rkisp_debug, &dev->v4l2_dev,
		 "mi isr:0x%x\n", mis_val);

	for (i = 0; i < RKISP_MAX_STREAM; ++i) {
		stream = &dev->cap_dev.stream[i];

		if (!(mis_val & CIF_MI_FRAME(stream)))
			continue;

		mi_frame_end_int_clear(stream);

		if (i == RKISP_STREAM_MP)
			rkisp_dvbm_event(dev, CIF_MI_MP_FRAME);

		rkisp_dmarx_get_frame(dev, &seq, NULL, &ns, true);
		if (!ns)
			ns = ktime_get_ns();
		if (stream->curr_buf) {
			stream->curr_buf->vb.sequence = seq;
			stream->curr_buf->vb.vb2_buf.timestamp = ns;
		}
		ns = ktime_get_ns();
		stream->dbg.interval = ns - stream->dbg.timestamp;
		stream->dbg.delay = ns - dev->isp_sdev.frm_timestamp;
		stream->dbg.timestamp = ns;
		stream->dbg.id = seq;

		if (stream->stopping) {
			/*
			 * Make sure stream is actually stopped, whose state
			 * can be read from the shadow register, before
			 * wake_up() thread which would immediately free all
			 * frame buffers. disable_mi() takes effect at the next
			 * frame end that sync the configurations to shadow
			 * regs.
			 */
			if (!dev->hw_dev->is_single) {
				stream->stopping = false;
				stream->streaming = false;
				stream->ops->disable_mi(stream);
				wake_up(&stream->done);
			} else if (stream->ops->is_stream_stopped(stream)) {
				stream->stopping = false;
				stream->streaming = false;
				wake_up(&stream->done);
			}
		} else {
			if (stream->id != RKISP_STREAM_MP || !dev->cap_dev.wrap_line)
				mi_frame_end(stream);
		}
	}

	if (mis_val & ISP3X_MI_MP_FRAME) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_MP];
		if (!stream->streaming || stream->is_pause)
			dev->irq_ends_mask &= ~ISP_FRAME_MP;
		else
			dev->irq_ends_mask |= ISP_FRAME_MP;
		rkisp_check_idle(dev, ISP_FRAME_MP);
	}
	if (mis_val & ISP3X_MI_SP_FRAME) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_SP];
		if (!stream->streaming || stream->is_pause)
			dev->irq_ends_mask &= ~ISP_FRAME_SP;
		else
			dev->irq_ends_mask |= ISP_FRAME_SP;
		rkisp_check_idle(dev, ISP_FRAME_SP);
	}
	if (mis_val & ISP3X_MI_BP_FRAME) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_BP];
		if (!stream->streaming || stream->is_pause)
			dev->irq_ends_mask &= ~ISP_FRAME_BP;
		else
			dev->irq_ends_mask |= ISP_FRAME_BP;
		rkisp_check_idle(dev, ISP_FRAME_BP);
	}
}

void rkisp_mipi_v32_isr(unsigned int phy, unsigned int packet,
			unsigned int overflow, unsigned int state,
			struct rkisp_device *dev)
{
	if (state & GENMASK(19, 17))
		v4l2_warn(&dev->v4l2_dev, "RD_SIZE_ERR:0x%08x\n", state);
	if (state & ISP21_MIPI_DROP_FRM)
		v4l2_warn(&dev->v4l2_dev, "MIPI drop frame\n");
}
