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

#define CIF_ISP_REQ_BUFS_MIN 0

static int mi_frame_end(struct rkisp_stream *stream, u32 state);
static int mi_frame_start(struct rkisp_stream *stream, u32 mis);

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
		.output_format = ISP32_MI_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUV_SPLA,
		.output_format = ISP32_MI_OUTPUT_YUV422,
	}, {
		.fourcc = V4L2_PIX_FMT_NV61,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.uv_swap = 1,
		.write_format = MI_CTRL_MP_WRITE_YUV_SPLA,
		.output_format = ISP32_MI_OUTPUT_YUV422,
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
		.output_format = ISP32_MI_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUV_SPLA,
		.output_format = ISP32_MI_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV21M,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 2,
		.uv_swap = 1,
		.write_format = MI_CTRL_MP_WRITE_YUV_SPLA,
		.output_format = ISP32_MI_OUTPUT_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12M,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 2,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUV_SPLA,
		.output_format = ISP32_MI_OUTPUT_YUV420,
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
		.fourcc = V4L2_PIX_FMT_RGB565,
		.fmt_type = FMT_RGB,
		.bpp = { 16 },
		.mplanes = 1,
		.write_format = MI_CTRL_SP_WRITE_PLA,
		.output_format = MI_CTRL_SP_OUTPUT_RGB565,
	},
};

static const struct capture_fmt fbc_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_FBC0,
		.fmt_type = FMT_FBC,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.write_format = ISP3X_MPFBC_YUV420,
	}, {
		.fourcc = V4L2_PIX_FMT_FBC2,
		.fmt_type = FMT_FBC,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.write_format = ISP3X_MPFBC_YUV422,
	}
};

static const struct capture_fmt bp_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_UYVY,
		.fmt_type = FMT_YUV,
		.bpp = { 16 },
		.cplanes = 1,
		.mplanes = 1,
		.write_format = ISP3X_BP_FORMAT_INT,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.write_format = ISP3X_BP_FORMAT_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 1,
		.write_format = ISP3X_BP_FORMAT_SPLA,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12M,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 16 },
		.cplanes = 2,
		.mplanes = 2,
		.write_format = ISP3X_BP_FORMAT_SPLA,
	}
};

static struct stream_config rkisp_fbc_stream_config = {
	.fmts = fbc_fmts,
	.fmt_size = ARRAY_SIZE(fbc_fmts),
	.frame_end_id = ISP3X_MI_MPFBC_FRAME,
	.dual_crop = {
		.ctrl = ISP3X_DUAL_CROP_CTRL,
		.yuvmode_mask = ISP3X_DUAL_CROP_FBC_MODE,
		.h_offset = ISP3X_DUAL_CROP_FBC_H_OFFS,
		.v_offset = ISP3X_DUAL_CROP_FBC_V_OFFS,
		.h_size = ISP3X_DUAL_CROP_FBC_H_SIZE,
		.v_size = ISP3X_DUAL_CROP_FBC_V_SIZE,
	},
	.mi = {
		.y_base_ad_init = ISP3X_MPFBC_HEAD_PTR,
		.cb_base_ad_init = ISP3X_MPFBC_PAYL_PTR,
		.y_base_ad_shd = ISP3X_MPFBC_HEAD_PTR,
	},
};

static struct stream_config rkisp_bp_stream_config = {
	.fmts = bp_fmts,
	.fmt_size = ARRAY_SIZE(bp_fmts),
	.frame_end_id = ISP3X_MI_BP_FRAME,
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
	},
};

static inline bool bp_is_stream_stopped(struct rkisp_stream *stream)
{
	u32 ret, en = ISP3X_BP_ENABLE;
	bool is_direct = true;

	if (!stream->ispdev->hw_dev->is_single)
		is_direct = false;
	ret = rkisp_read(stream->ispdev, ISP3X_MI_BP_WR_CTRL, is_direct);

	return !(ret & en);
}

static bool fbc_is_stream_stopped(struct rkisp_stream *stream)
{
	u32 ret, en = ISP3X_MPFBC_EN_SHD;
	bool is_direct = true;

	if (!stream->ispdev->hw_dev->is_single) {
		is_direct = false;
		en = ISP3X_MPFBC_EN;
	}

	ret = rkisp_read(stream->ispdev, ISP3X_MPFBC_CTRL, is_direct);

	return !(ret & en);
}

static int get_stream_irq_mask(struct rkisp_stream *stream)
{
	int ret;

	switch (stream->id) {
	case RKISP_STREAM_SP:
		ret = ISP_FRAME_SP;
		break;
	case RKISP_STREAM_FBC:
		ret = ISP_FRAME_MPFBC;
		break;
	case RKISP_STREAM_BP:
		ret = ISP_FRAME_BP;
		break;
	case RKISP_STREAM_MP:
	default:
		ret = ISP_FRAME_MP;
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
	    dcrop->left == 0 && dcrop->top == 0 &&
	    !dev->hw_dev->is_unite) {
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

	if (input_isp_fmt->fmt_type == FMT_BAYER ||
	    stream->id == RKISP_STREAM_FBC ||
	    stream->id == RKISP_STREAM_BP)
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
	struct v4l2_pix_format_mplane *out_fmt = &stream->out_fmt;
	bool is_unite = dev->hw_dev->is_unite;
	u32 val, mask;

       /*
	* NOTE: plane_fmt[0].sizeimage is total size of all planes for single
	* memory plane formats, so calculate the size explicitly.
	*/
	val = out_fmt->plane_fmt[0].bytesperline * out_fmt->height;
	rkisp_unite_write(dev, stream->config->mi.y_size_init, val, false, is_unite);

	val = out_fmt->plane_fmt[1].sizeimage;
	rkisp_unite_write(dev, stream->config->mi.cb_size_init, val, false, is_unite);

	val = out_fmt->plane_fmt[2].sizeimage;
	rkisp_unite_write(dev, stream->config->mi.cr_size_init, val, false, is_unite);

	val = is_unite ? out_fmt->width / 2 : out_fmt->width;
	rkisp_unite_write(dev, ISP3X_MI_MP_WR_Y_PIC_WIDTH, val, false, is_unite);

	val = out_fmt->height;
	rkisp_unite_write(dev, ISP3X_MI_MP_WR_Y_PIC_HEIGHT, val, false, is_unite);

	val = out_fmt->plane_fmt[0].bytesperline;
	rkisp_unite_write(dev, ISP3X_MI_MP_WR_Y_LLENGTH, val, false, is_unite);

	val = stream->out_isp_fmt.uv_swap ? ISP3X_MI_XTD_FORMAT_MP_UV_SWAP : 0;
	mask = ISP3X_MI_XTD_FORMAT_MP_UV_SWAP;
	rkisp_unite_set_bits(dev, ISP3X_MI_WR_XTD_FORMAT_CTRL, mask, val, false, is_unite);

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
	rkisp_unite_write(dev, ISP3X_MPFBC_CTRL, val, false, is_unite);

	val = calc_burst_len(stream) | CIF_MI_CTRL_INIT_BASE_EN |
		CIF_MI_CTRL_INIT_OFFSET_EN | CIF_MI_MP_AUTOUPDATE_ENABLE |
		stream->out_isp_fmt.write_format;
	mask = GENMASK(19, 16) | MI_CTRL_MP_FMT_MASK;
	rkisp_unite_set_bits(dev, ISP3X_MI_WR_CTRL, mask, val, false, is_unite);

	mi_frame_end_int_enable(stream);
	/* set up first buffer */
	mi_frame_end(stream, FRAME_INIT);
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
	bool is_unite = dev->hw_dev->is_unite;
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
	val = out_fmt->plane_fmt[0].bytesperline * out_fmt->height;
	rkisp_unite_write(dev, stream->config->mi.y_size_init, val, false, is_unite);

	val = out_fmt->plane_fmt[1].sizeimage;
	rkisp_unite_write(dev, stream->config->mi.cb_size_init, val, false, is_unite);

	val = out_fmt->plane_fmt[2].sizeimage;
	rkisp_unite_write(dev, stream->config->mi.cr_size_init, val, false, is_unite);

	val = is_unite ? out_fmt->width / 2 : out_fmt->width;
	rkisp_unite_write(dev, ISP3X_MI_SP_WR_Y_PIC_WIDTH, val, false, is_unite);

	val = out_fmt->height;
	rkisp_unite_write(dev, ISP3X_MI_SP_WR_Y_PIC_HEIGHT, val, false, is_unite);

	val = stream->u.sp.y_stride;
	rkisp_unite_write(dev, ISP3X_MI_SP_WR_Y_LLENGTH, val, false, is_unite);

	val = stream->out_isp_fmt.uv_swap ? ISP3X_MI_XTD_FORMAT_SP_UV_SWAP : 0;
	mask = ISP3X_MI_XTD_FORMAT_SP_UV_SWAP;
	rkisp_unite_set_bits(dev, ISP3X_MI_WR_XTD_FORMAT_CTRL, mask, val, false, is_unite);

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
	rkisp_unite_write(dev, ISP3X_MPFBC_CTRL, val, false, is_unite);

	val = calc_burst_len(stream) | CIF_MI_CTRL_INIT_BASE_EN |
		CIF_MI_CTRL_INIT_OFFSET_EN | stream->out_isp_fmt.write_format |
		sp_in_fmt | stream->out_isp_fmt.output_format |
		CIF_MI_SP_AUTOUPDATE_ENABLE;
	mask = GENMASK(19, 16) | MI_CTRL_SP_FMT_MASK;
	rkisp_unite_set_bits(dev, ISP3X_MI_WR_CTRL, mask, val, false, is_unite);

	mi_frame_end_int_enable(stream);
	/* set up first buffer */
	mi_frame_end(stream, FRAME_INIT);
	return 0;
}

static int fbc_config_mi(struct rkisp_stream *stream)
{
	/* yuv422 is 16*2, yuv420 is 16*1.5 */
	u32 mult = stream->out_isp_fmt.write_format ? 32 : 24;
	u32 h = ALIGN(stream->out_fmt.height, 16);
	u32 w = ALIGN(stream->out_fmt.width, 16);
	u32 offs = ALIGN(w * h / 16, RK_MPP_ALIGN);
	bool is_unite = stream->ispdev->hw_dev->is_unite;

	rkisp_write(stream->ispdev, ISP3X_MPFBC_HEAD_OFFSET, offs, false);
	rkisp_unite_write(stream->ispdev, ISP3X_MPFBC_VIR_WIDTH, w, false, is_unite);
	rkisp_unite_write(stream->ispdev, ISP3X_MPFBC_PAYL_WIDTH, w, false, is_unite);
	rkisp_unite_write(stream->ispdev, ISP3X_MPFBC_VIR_HEIGHT, h, false, is_unite);
	if (is_unite) {
		u32 left_w = (stream->out_fmt.width / 2) & ~0xf;

		offs += left_w * mult;
		rkisp_next_write(stream->ispdev, ISP3X_MPFBC_HEAD_OFFSET, offs, false);
	}
	rkisp_unite_set_bits(stream->ispdev, ISP3X_MI_WR_CTRL, 0,
			     CIF_MI_CTRL_INIT_BASE_EN | CIF_MI_CTRL_INIT_OFFSET_EN,
			     false, is_unite);
	mi_frame_end_int_enable(stream);
	/* set up first buffer */
	mi_frame_end(stream, FRAME_INIT);
	return 0;
}

static int bp_config_mi(struct rkisp_stream *stream)
{
	struct v4l2_pix_format_mplane *out_fmt = &stream->out_fmt;
	struct rkisp_device *dev = stream->ispdev;
	bool is_unite = dev->hw_dev->is_unite;
	u32 val, mask;

       /*
	* NOTE: plane_fmt[0].sizeimage is total size of all planes for single
	* memory plane formats, so calculate the size explicitly.
	*/
	val = out_fmt->plane_fmt[0].bytesperline * out_fmt->height;
	rkisp_unite_write(dev, stream->config->mi.y_size_init, val, false, is_unite);

	val = out_fmt->plane_fmt[1].sizeimage;
	rkisp_unite_write(dev, stream->config->mi.cb_size_init, val, false, is_unite);

	val = is_unite ? out_fmt->width / 2 : out_fmt->width;
	rkisp_unite_write(dev, ISP3X_MI_BP_WR_Y_PIC_WIDTH, val, false, is_unite);

	val = out_fmt->height;
	rkisp_unite_write(dev, ISP3X_MI_BP_WR_Y_PIC_HEIGHT, val, false, is_unite);

	val = out_fmt->plane_fmt[0].bytesperline;
	rkisp_unite_write(dev, ISP3X_MI_BP_WR_Y_LLENGTH, val, false, is_unite);

	mask = ISP3X_MPFBC_FORCE_UPD | ISP3X_BP_YUV_MODE;
	val = rkisp_read_reg_cache(dev, ISP3X_MPFBC_CTRL) & ~mask;

	if (out_fmt->pixelformat == V4L2_PIX_FMT_NV12 ||
	    out_fmt->pixelformat == V4L2_PIX_FMT_NV12M)
		val |= ISP3X_SEPERATE_YUV_CFG;
	else
		val |= ISP3X_SEPERATE_YUV_CFG | ISP3X_BP_YUV_MODE;
	rkisp_unite_write(dev, ISP3X_MPFBC_CTRL, val, false, is_unite);
	val = CIF_MI_CTRL_INIT_BASE_EN | CIF_MI_CTRL_INIT_OFFSET_EN;
	rkisp_unite_set_bits(dev, ISP3X_MI_WR_CTRL, 0, val, false, is_unite);
	mi_frame_end_int_enable(stream);
	/* set up first buffer */
	mi_frame_end(stream, FRAME_INIT);
	return 0;
}

static void mp_enable_mi(struct rkisp_stream *stream)
{
	struct capture_fmt *isp_fmt = &stream->out_isp_fmt;
	u32 mask = CIF_MI_CTRL_MP_ENABLE | CIF_MI_CTRL_RAW_ENABLE;
	u32 val = CIF_MI_CTRL_MP_ENABLE;

	if (isp_fmt->fmt_type == FMT_BAYER)
		val = CIF_MI_CTRL_RAW_ENABLE;
	rkisp_unite_set_bits(stream->ispdev, ISP3X_MI_WR_CTRL, mask, val,
			     false, stream->ispdev->hw_dev->is_unite);
}

static void sp_enable_mi(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct capture_fmt *fmt = &stream->out_isp_fmt;
	u32 val = CIF_MI_CTRL_SP_ENABLE;
	u32 mask = CIF_MI_SP_Y_FULL_YUV2RGB | CIF_MI_SP_CBCR_FULL_YUV2RGB;

	if (fmt->fmt_type == FMT_RGB &&
	    dev->isp_sdev.quantization == V4L2_QUANTIZATION_FULL_RANGE)
		val |= mask;
	rkisp_unite_set_bits(stream->ispdev, ISP3X_MI_WR_CTRL,
			     mask, val, false,
			     stream->ispdev->hw_dev->is_unite);
}

static void fbc_enable_mi(struct rkisp_stream *stream)
{
	u32 val, mask = ISP3X_MPFBC_FORCE_UPD | ISP3X_MPFBC_YUV_MASK |
			ISP3X_MPFBC_SPARSE_MODE;
	bool is_unite = stream->ispdev->hw_dev->is_unite;

	/* config no effect immediately, read back is shadow, get config value from cache */
	val = rkisp_read_reg_cache(stream->ispdev, ISP3X_MPFBC_CTRL) & ~mask;
	val |= stream->out_isp_fmt.write_format | ISP3X_HEAD_OFFSET_EN | ISP3X_MPFBC_EN;
	rkisp_unite_write(stream->ispdev, ISP3X_MPFBC_CTRL, val, false, is_unite);
}

static void bp_enable_mi(struct rkisp_stream *stream)
{
	u32 val = stream->out_isp_fmt.write_format |
		ISP3X_BP_ENABLE | ISP3X_BP_AUTO_UPD;

	rkisp_unite_write(stream->ispdev, ISP3X_MI_BP_WR_CTRL, val, false,
			  stream->ispdev->hw_dev->is_unite);
}

static void mp_disable_mi(struct rkisp_stream *stream)
{
	u32 mask = CIF_MI_CTRL_MP_ENABLE | CIF_MI_CTRL_RAW_ENABLE;

	rkisp_unite_clear_bits(stream->ispdev, ISP3X_MI_WR_CTRL, mask, false,
			       stream->ispdev->hw_dev->is_unite);
}

static void sp_disable_mi(struct rkisp_stream *stream)
{
	rkisp_unite_clear_bits(stream->ispdev, ISP3X_MI_WR_CTRL, CIF_MI_CTRL_SP_ENABLE,
			       false, stream->ispdev->hw_dev->is_unite);
}

static void fbc_disable_mi(struct rkisp_stream *stream)
{
	u32 mask = ISP3X_MPFBC_FORCE_UPD | ISP3X_MPFBC_EN;

	rkisp_unite_clear_bits(stream->ispdev, ISP3X_MPFBC_CTRL, mask,
			       false, stream->ispdev->hw_dev->is_unite);
}

static void bp_disable_mi(struct rkisp_stream *stream)
{
	rkisp_unite_clear_bits(stream->ispdev, ISP3X_MI_BP_WR_CTRL, ISP3X_BP_ENABLE,
			       false, stream->ispdev->hw_dev->is_unite);
}

static void update_mi(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_dummy_buffer *dummy_buf = &dev->hw_dev->dummy_buf;
	u32 val, reg;

	if (stream->next_buf) {
		reg = stream->config->mi.y_base_ad_init;
		val = stream->next_buf->buff_addr[RKISP_PLANE_Y];
		rkisp_write(dev, reg, val, false);

		reg = stream->config->mi.cb_base_ad_init;
		val = stream->next_buf->buff_addr[RKISP_PLANE_CB];
		rkisp_write(dev, reg, val, false);

		if (stream->id != RKISP_STREAM_FBC && stream->id != RKISP_STREAM_BP) {
			reg = stream->config->mi.cr_base_ad_init;
			val = stream->next_buf->buff_addr[RKISP_PLANE_CR];
			rkisp_write(dev, reg, val, false);
		}

		if (dev->hw_dev->is_unite) {
			u32 mult = stream->id != RKISP_STREAM_FBC ? 1 :
				   (stream->out_isp_fmt.write_format ? 32 : 24);
			u32 div = stream->out_isp_fmt.fourcc == V4L2_PIX_FMT_UYVY ? 1 : 2;

			reg = stream->config->mi.y_base_ad_init;
			val = stream->next_buf->buff_addr[RKISP_PLANE_Y];
			val += ((stream->out_fmt.width / div) & ~0xf);
			rkisp_next_write(dev, reg, val, false);

			reg = stream->config->mi.cb_base_ad_init;
			val = stream->next_buf->buff_addr[RKISP_PLANE_CB];
			val += ((stream->out_fmt.width / div) & ~0xf) * mult;
			rkisp_next_write(dev, reg, val, false);

			if (stream->id != RKISP_STREAM_FBC && stream->id != RKISP_STREAM_BP) {
				reg = stream->config->mi.cr_base_ad_init;
				val = stream->next_buf->buff_addr[RKISP_PLANE_CR];
				val += ((stream->out_fmt.width / div) & ~0xf);
				rkisp_next_write(dev, reg, val, false);
			}
		}

		/* single buf updated at readback for multidevice */
		if (!dev->hw_dev->is_single) {
			stream->curr_buf = stream->next_buf;
			stream->next_buf = NULL;
		}
	} else if (dummy_buf->mem_priv) {
		stream->dbg.frameloss++;
		val = dummy_buf->dma_addr;
		reg = stream->config->mi.y_base_ad_init;
		rkisp_unite_write(dev, reg, val, false, dev->hw_dev->is_unite);
		reg = stream->config->mi.cb_base_ad_init;
		rkisp_unite_write(dev, reg, val, false, dev->hw_dev->is_unite);
		reg = stream->config->mi.cr_base_ad_init;
		if (stream->id != RKISP_STREAM_FBC && stream->id != RKISP_STREAM_BP)
			rkisp_unite_write(dev, reg, val, false, dev->hw_dev->is_unite);
	}

	if (stream->id != RKISP_STREAM_FBC) {
		reg = stream->config->mi.y_offs_cnt_init;
		rkisp_unite_write(dev, reg, 0, false, dev->hw_dev->is_unite);
		reg = stream->config->mi.cb_offs_cnt_init;
		rkisp_unite_write(dev, reg, 0, false, dev->hw_dev->is_unite);
		reg = stream->config->mi.cr_offs_cnt_init;
		if (stream->id != RKISP_STREAM_BP)
			rkisp_unite_write(dev, reg, 0, false, dev->hw_dev->is_unite);
	}

	v4l2_dbg(2, rkisp_debug, &dev->v4l2_dev,
		 "%s stream:%d Y:0x%x CB:0x%x | Y_SHD:0x%x\n",
		 __func__, stream->id,
		 rkisp_read(dev, stream->config->mi.y_base_ad_init, false),
		 rkisp_read(dev, stream->config->mi.cb_base_ad_init, false),
		 rkisp_read(dev, stream->config->mi.y_base_ad_shd, true));
	if (dev->hw_dev->is_unite)
		v4l2_dbg(2, rkisp_debug, &dev->v4l2_dev,
			 "%s stream:%d Y:0x%x CB:0x%x | Y_SHD:0x%x, right\n",
			 __func__, stream->id,
			 rkisp_next_read(dev, stream->config->mi.y_base_ad_init, false),
			 rkisp_next_read(dev, stream->config->mi.cb_base_ad_init, false),
			 rkisp_next_read(dev, stream->config->mi.y_base_ad_shd, true));
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

static struct streams_ops rkisp_fbc_streams_ops = {
	.config_mi = fbc_config_mi,
	.enable_mi = fbc_enable_mi,
	.disable_mi = fbc_disable_mi,
	.is_stream_stopped = fbc_is_stream_stopped,
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

static void stream_self_update(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	u32 val, mask = ISP3X_MPSELF_UPD | ISP3X_SPSELF_UPD | ISP3X_BPSELF_UPD;
	bool is_unite = dev->hw_dev->is_unite;

	if (stream->id == RKISP_STREAM_FBC) {
		val = ISP3X_MPFBC_FORCE_UPD;
		rkisp_unite_set_bits(dev, ISP3X_MPFBC_CTRL, 0, val, false, is_unite);
		return;
	}

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
	default:
		return;
	}

	rkisp_unite_set_bits(dev, ISP3X_MI_WR_CTRL2, mask, val, false, is_unite);
}

static int mi_frame_start(struct rkisp_stream *stream, u32 mis)
{
	struct rkisp_device *dev = stream->ispdev;
	unsigned long lock_flags = 0;

	/* readback start to update stream buf if null */
	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	if (stream->streaming && !mis && !stream->curr_buf) {
		if (!stream->next_buf && !list_empty(&stream->buf_queue)) {
			stream->next_buf = list_first_entry(&stream->buf_queue,
							    struct rkisp_buffer, queue);
			list_del(&stream->next_buf->queue);
			stream->ops->update_mi(stream);
		}
		if (dev->hw_dev->is_single && stream->next_buf) {
			stream->curr_buf = stream->next_buf;
			stream->next_buf = NULL;
			stream_self_update(stream);
		}
	}
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);

	return 0;
}

/*
 * This function is called when a frame end come. The next frame
 * is processing and we should set up buffer for next-next frame,
 * otherwise it will overflow.
 */
static int mi_frame_end(struct rkisp_stream *stream, u32 state)
{
	struct rkisp_device *dev = stream->ispdev;
	struct capture_fmt *isp_fmt = &stream->out_isp_fmt;
	struct rkisp_buffer *buf = NULL;
	unsigned long lock_flags = 0;
	int i = 0;

	if (stream->id == RKISP_STREAM_VIR)
		return 0;

	if (dev->cap_dev.is_done_early &&
	    (state == FRAME_IRQ || state == FRAME_WORK)) {
		spin_lock_irqsave(&stream->vbq_lock, lock_flags);
		if (state == FRAME_IRQ && stream->curr_buf)
			stream->frame_early = false;
		else
			stream->frame_early = true;
		buf = stream->curr_buf;
		stream->curr_buf = NULL;
		spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
		if ((!stream->frame_early && state == FRAME_WORK) ||
		    (stream->frame_early && state == FRAME_IRQ))
			goto end;
	} else {
		buf = stream->curr_buf;
	}

	if (buf) {
		struct rkisp_stream *vir = &dev->cap_dev.stream[RKISP_STREAM_VIR];
		struct vb2_buffer *vb2_buf = &buf->vb.vb2_buf;
		u64 ns = 0;

		/* Dequeue a filled buffer */
		for (i = 0; i < isp_fmt->mplanes; i++) {
			u32 payload_size = stream->out_fmt.plane_fmt[i].sizeimage;

			vb2_set_plane_payload(vb2_buf, i, payload_size);
		}

		rkisp_dmarx_get_frame(dev, &i, NULL, &ns, true);
		buf->vb.sequence = i;
		if (!ns)
			ns = ktime_get_ns();
		vb2_buf->timestamp = ns;

		ns = ktime_get_ns();
		stream->dbg.interval = ns - stream->dbg.timestamp;
		stream->dbg.timestamp = ns;
		stream->dbg.id = buf->vb.sequence;
		stream->dbg.delay = ns - dev->isp_sdev.frm_timestamp;

		if (vir->streaming && vir->conn_id == stream->id) {
			spin_lock_irqsave(&vir->vbq_lock, lock_flags);
			list_add_tail(&buf->queue,
				      &dev->cap_dev.vir_cpy.queue);
			spin_unlock_irqrestore(&vir->vbq_lock, lock_flags);
			if (!completion_done(&dev->cap_dev.vir_cpy.cmpl))
				complete(&dev->cap_dev.vir_cpy.cmpl);
		} else {
			rkisp_stream_buf_done(stream, buf);
		}
	}

end:
	if (state == FRAME_WORK)
		return 0;
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
	unsigned long lock_flags = 0;
	int ret = 0;
	bool is_wait = dev->hw_dev->is_shutdown ? false : true;

	stream->stopping = true;
	if (dev->hw_dev->is_single)
		stream->ops->disable_mi(stream);
	if (IS_HDR_RDBK(dev->rd_mode)) {
		spin_lock_irqsave(&dev->hw_dev->rdbk_lock, lock_flags);
		if (dev->hw_dev->cur_dev_id != dev->dev_id || dev->hw_dev->is_idle) {
			is_wait = false;
			stream->ops->disable_mi(stream);
			/* force update to close */
			if (dev->hw_dev->is_single)
				stream_self_update(stream);
		}
		if (atomic_read(&dev->cap_dev.refcnt) == 1 && !is_wait)
			dev->isp_state = ISP_STOP;
		spin_unlock_irqrestore(&dev->hw_dev->rdbk_lock, lock_flags);
	}
	if (is_wait && !stream->ops->is_stream_stopped(stream)) {
		ret = wait_event_timeout(stream->done,
					 !stream->streaming,
					 msecs_to_jiffies(500));
		if (!ret)
			v4l2_warn(v4l2_dev, "%s id:%d timeout\n",
				  __func__, stream->id);
	}

	stream->stopping = false;
	stream->streaming = false;
	stream->ops->disable_mi(stream);
	rkisp_disable_dcrop(stream, true);
	if (stream->id == RKISP_STREAM_MP || stream->id == RKISP_STREAM_SP)
		rkisp_disable_rsz(stream, true);
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
	int ret;

	if (stream->ops->set_data_path)
		stream->ops->set_data_path(stream);
	ret = stream->ops->config_mi(stream);
	if (ret)
		return ret;

	stream->ops->enable_mi(stream);
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
	unsigned long lock_flags = 0;
	struct v4l2_pix_format_mplane *pixm = &stream->out_fmt;
	struct capture_fmt *isp_fmt = &stream->out_isp_fmt;
	struct sg_table *sgt;
	u32 height, size, offset;
	int i;

	memset(ispbuf->buff_addr, 0, sizeof(ispbuf->buff_addr));
	for (i = 0; i < isp_fmt->mplanes; i++) {
		vb2_plane_vaddr(vb, i);
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
			/* FBC mode calculate payload offset */
			height = (isp_fmt->fmt_type == FMT_FBC) ?
				ALIGN(pixm->height, 16) >> 4 : pixm->height;
			size = (i == 0) ?
				pixm->plane_fmt[i].bytesperline * height :
				pixm->plane_fmt[i].sizeimage;
			offset = (isp_fmt->fmt_type == FMT_FBC) ?
				ALIGN(size, RK_MPP_ALIGN) : size;
			ispbuf->buff_addr[i + 1] =
				ispbuf->buff_addr[i] + offset;
		}
	}

	v4l2_dbg(2, rkisp_debug, &stream->ispdev->v4l2_dev,
		 "stream:%d queue buf:0x%x\n",
		 stream->id, ispbuf->buff_addr[0]);

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	list_add_tail(&ispbuf->queue, &stream->buf_queue);
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static int rkisp_create_dummy_buf(struct rkisp_stream *stream)
{
	return rkisp_alloc_common_dummy_buf(stream->ispdev);
}

static void rkisp_destroy_dummy_buf(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;

	rkisp_free_common_dummy_buf(dev);
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
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	while (!list_empty(&stream->buf_done_list)) {
		buf = list_first_entry(&stream->buf_done_list,
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

	mutex_lock(&dev->hw_dev->dev_lock);

	v4l2_dbg(1, rkisp_debug, v4l2_dev, "%s %s %d\n",
		 __func__, node->vdev.name, stream->id);

	if (!stream->streaming)
		goto end;

	if (stream->id == RKISP_STREAM_VIR) {
		stream->stopping = true;
		wait_event_timeout(stream->done,
				   stream->frame_end,
				   msecs_to_jiffies(500));
		stream->streaming = false;
		stream->stopping = false;
		destroy_buf_queue(stream, VB2_BUF_STATE_ERROR);

		if (!completion_done(&dev->cap_dev.vir_cpy.cmpl))
			complete(&dev->cap_dev.vir_cpy.cmpl);
		stream->conn_id = -1;
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
	tasklet_disable(&stream->buf_done_tasklet);
end:
	mutex_unlock(&dev->hw_dev->dev_lock);
}

static void vir_cpy_image(struct work_struct *work)
{
	struct rkisp_vir_cpy *cpy =
	container_of(work, struct rkisp_vir_cpy, work);
	struct rkisp_stream *vir = cpy->stream;
	struct rkisp_buffer *src_buf = NULL;
	unsigned long lock_flags = 0;
	u32 i;

	v4l2_dbg(1, rkisp_debug, &vir->ispdev->v4l2_dev,
		 "%s enter\n", __func__);

	vir->streaming = true;
	spin_lock_irqsave(&vir->vbq_lock, lock_flags);
	if (!list_empty(&cpy->queue)) {
		src_buf = list_first_entry(&cpy->queue,
				struct rkisp_buffer, queue);
		list_del(&src_buf->queue);
	}
	spin_unlock_irqrestore(&vir->vbq_lock, lock_flags);

	while (src_buf || vir->streaming) {
		if (vir->stopping || !vir->streaming)
			goto end;

		if (!src_buf)
			wait_for_completion(&cpy->cmpl);

		vir->frame_end = false;
		spin_lock_irqsave(&vir->vbq_lock, lock_flags);

		if (!src_buf && !list_empty(&cpy->queue)) {
			src_buf = list_first_entry(&cpy->queue,
					struct rkisp_buffer, queue);
			list_del(&src_buf->queue);
		}

		if (src_buf && !vir->curr_buf && !list_empty(&vir->buf_queue)) {
			vir->curr_buf = list_first_entry(&vir->buf_queue,
					struct rkisp_buffer, queue);
			list_del(&vir->curr_buf->queue);
		}
		spin_unlock_irqrestore(&vir->vbq_lock, lock_flags);

		if (!vir->curr_buf || !src_buf)
			goto end;

		for (i = 0; i < vir->out_isp_fmt.mplanes; i++) {
			u32 payload_size = vir->out_fmt.plane_fmt[i].sizeimage;
			void *src = vb2_plane_vaddr(&src_buf->vb.vb2_buf, i);
			void *dst = vb2_plane_vaddr(&vir->curr_buf->vb.vb2_buf, i);

			if (!src || !dst)
				break;
			vb2_set_plane_payload(&vir->curr_buf->vb.vb2_buf, i, payload_size);
			memcpy(dst, src, payload_size);
		}

		vir->curr_buf->vb.sequence = src_buf->vb.sequence;
		vir->curr_buf->vb.vb2_buf.timestamp = src_buf->vb.vb2_buf.timestamp;
		vb2_buffer_done(&vir->curr_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		vir->curr_buf = NULL;
end:
		if (src_buf)
			vb2_buffer_done(&src_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		src_buf = NULL;
		spin_lock_irqsave(&vir->vbq_lock, lock_flags);

		if (!list_empty(&cpy->queue)) {
			src_buf = list_first_entry(&cpy->queue,
					struct rkisp_buffer, queue);
			list_del(&src_buf->queue);
		} else if (vir->stopping) {
			vir->streaming = false;
		}

		spin_unlock_irqrestore(&vir->vbq_lock, lock_flags);
	}

	vir->frame_end = true;

	if (vir->stopping) {
		vir->stopping = false;
		vir->streaming = false;
		wake_up(&vir->done);
	}

	v4l2_dbg(1, rkisp_debug, &vir->ispdev->v4l2_dev,
		 "%s exit\n", __func__);
}

static int rkisp_stream_start(struct rkisp_stream *stream)
{
	struct v4l2_device *v4l2_dev = &stream->ispdev->v4l2_dev;
	struct rkisp_device *dev = stream->ispdev;
	bool async = false;
	int ret;

	async = (stream->id == RKISP_STREAM_MP) ?
		dev->cap_dev.stream[RKISP_STREAM_SP].streaming :
		dev->cap_dev.stream[RKISP_STREAM_MP].streaming;

	/*
	 * can't be async now, otherwise the latter started stream fails to
	 * produce mi interrupt.
	 */
	ret = rkisp_stream_config_dcrop(stream, false);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "config dcrop failed with error %d\n", ret);
		return ret;
	}

	if (stream->id == RKISP_STREAM_FBC || stream->id == RKISP_STREAM_BP)
		goto end;

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
	int ret = -1;

	mutex_lock(&dev->hw_dev->dev_lock);

	v4l2_dbg(1, rkisp_debug, v4l2_dev, "%s %s id:%d\n",
		 __func__, node->vdev.name, stream->id);

	if (WARN_ON(stream->streaming)) {
		mutex_unlock(&dev->hw_dev->dev_lock);
		return -EBUSY;
	}

	if (stream->id == RKISP_STREAM_VIR) {
		struct rkisp_stream *t = &dev->cap_dev.stream[stream->conn_id];

		if (t->streaming) {
			INIT_WORK(&dev->cap_dev.vir_cpy.work, vir_cpy_image);
			init_completion(&dev->cap_dev.vir_cpy.cmpl);
			INIT_LIST_HEAD(&dev->cap_dev.vir_cpy.queue);
			dev->cap_dev.vir_cpy.stream = stream;
			schedule_work(&dev->cap_dev.vir_cpy.work);
			ret = 0;
		} else {
			v4l2_err(&dev->v4l2_dev,
				 "no stream enable for iqtool\n");
			destroy_buf_queue(stream, VB2_BUF_STATE_QUEUED);
			ret = -EINVAL;
		}

		mutex_unlock(&dev->hw_dev->dev_lock);

		return ret;
	}

	memset(&stream->dbg, 0, sizeof(stream->dbg));
	atomic_inc(&dev->cap_dev.refcnt);
	if (!dev->isp_inp || !stream->linked) {
		v4l2_err(v4l2_dev, "check %s link or isp input\n", node->vdev.name);
		goto buffer_done;
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
	tasklet_enable(&stream->buf_done_tasklet);
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
		stream->config->fmts = sp_fmts;
		stream->config->fmt_size = ARRAY_SIZE(sp_fmts);
		break;
	case RKISP_STREAM_FBC:
		strscpy(vdev->name, FBC_VDEV_NAME, sizeof(vdev->name));
		stream->ops = &rkisp_fbc_streams_ops;
		stream->config = &rkisp_fbc_stream_config;
		break;
	case RKISP_STREAM_BP:
		strscpy(vdev->name, BP_VDEV_NAME, sizeof(vdev->name));
		stream->ops = &rkisp_bp_streams_ops;
		stream->config = &rkisp_bp_stream_config;
		break;
	case RKISP_STREAM_VIR:
		strscpy(vdev->name, VIR_VDEV_NAME, sizeof(vdev->name));
		stream->ops = NULL;
		stream->config = &rkisp_mp_stream_config;
		stream->conn_id = -1;
		break;
	default:
		strscpy(vdev->name, MP_VDEV_NAME, sizeof(vdev->name));
		stream->ops = &rkisp_mp_streams_ops;
		stream->config = &rkisp_mp_stream_config;
		stream->config->fmts = mp_fmts;
		stream->config->fmt_size = ARRAY_SIZE(mp_fmts);
		if (dev->br_dev.linked)
			stream->linked = false;
	}

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

int rkisp_register_stream_v30(struct rkisp_device *dev)
{
	struct rkisp_capture_device *cap_dev = &dev->cap_dev;
	int ret;

	ret = rkisp_stream_init(dev, RKISP_STREAM_MP);
	if (ret < 0)
		goto err;
	ret = rkisp_stream_init(dev, RKISP_STREAM_SP);
	if (ret < 0)
		goto err_free_mp;
	ret = rkisp_stream_init(dev, RKISP_STREAM_FBC);
	if (ret < 0)
		goto err_free_sp;
	ret = rkisp_stream_init(dev, RKISP_STREAM_VIR);
	if (ret < 0)
		goto err_free_fbc;
#ifdef RKISP_STREAM_BP_EN
	ret = rkisp_stream_init(dev, RKISP_STREAM_BP);
	if (ret < 0)
		goto err_free_vir;
#endif
	return 0;
#ifdef RKISP_STREAM_BP_EN
err_free_vir:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_VIR]);
#endif
err_free_fbc:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_FBC]);
err_free_sp:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_SP]);
err_free_mp:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_MP]);
err:
	return ret;
}

void rkisp_unregister_stream_v30(struct rkisp_device *dev)
{
	struct rkisp_capture_device *cap_dev = &dev->cap_dev;
	struct rkisp_stream *stream;

	stream = &cap_dev->stream[RKISP_STREAM_MP];
	rkisp_unregister_stream_vdev(stream);
	stream = &cap_dev->stream[RKISP_STREAM_SP];
	rkisp_unregister_stream_vdev(stream);
	stream = &cap_dev->stream[RKISP_STREAM_FBC];
	rkisp_unregister_stream_vdev(stream);
	stream = &cap_dev->stream[RKISP_STREAM_VIR];
	rkisp_unregister_stream_vdev(stream);
#ifdef RKISP_STREAM_BP_EN
	stream = &cap_dev->stream[RKISP_STREAM_BP];
	rkisp_unregister_stream_vdev(stream);
#endif
}

/****************  Interrupter Handler ****************/

void rkisp_mi_v30_isr(u32 mis_val, struct rkisp_device *dev)
{
	struct rkisp_stream *stream;
	unsigned int i;

	if (dev->hw_dev->is_unite) {
		u32 val = rkisp_read(dev, ISP3X_MI_RIS, true);

		if (val) {
			rkisp_write(dev, ISP3X_MI_ICR, val, true);
			v4l2_dbg(3, rkisp_debug, &dev->v4l2_dev,
				 "left mi isr:0x%x\n", val);
		}
	}
	v4l2_dbg(3, rkisp_debug, &dev->v4l2_dev,
		 "mi isr:0x%x\n", mis_val);

	rkisp_bridge_isr(&mis_val, dev);

	for (i = 0; i < RKISP_MAX_STREAM; ++i) {
		stream = &dev->cap_dev.stream[i];

		if (!(mis_val & CIF_MI_FRAME(stream)) ||
		    stream->id == RKISP_STREAM_VIR)
			continue;

		mi_frame_end_int_clear(stream);

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
			mi_frame_end(stream, FRAME_IRQ);
		}
	}

	if (mis_val & ISP3X_MI_MP_FRAME) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_MP];
		if (!stream->streaming)
			dev->irq_ends_mask &= ~ISP_FRAME_MP;
		rkisp_check_idle(dev, ISP_FRAME_MP);
	}
	if (mis_val & ISP3X_MI_SP_FRAME) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_SP];
		if (!stream->streaming)
			dev->irq_ends_mask &= ~ISP_FRAME_SP;
		rkisp_check_idle(dev, ISP_FRAME_SP);
	}
	if (mis_val & ISP3X_MI_MPFBC_FRAME) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_FBC];
		if (!stream->streaming)
			dev->irq_ends_mask &= ~ISP_FRAME_MPFBC;
		rkisp_check_idle(dev, ISP_FRAME_MPFBC);
	}
	if (mis_val & ISP3X_MI_BP_FRAME) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_BP];
		if (!stream->streaming)
			dev->irq_ends_mask &= ~ISP_FRAME_BP;
		rkisp_check_idle(dev, ISP_FRAME_BP);
	}
}

void rkisp_mipi_v30_isr(unsigned int phy, unsigned int packet,
			unsigned int overflow, unsigned int state,
			struct rkisp_device *dev)
{
	if (state & GENMASK(19, 17))
		v4l2_warn(&dev->v4l2_dev, "RD_SIZE_ERR:0x%08x\n", state);
	if (state & ISP21_MIPI_DROP_FRM)
		v4l2_warn(&dev->v4l2_dev, "MIPI drop frame\n");
}
