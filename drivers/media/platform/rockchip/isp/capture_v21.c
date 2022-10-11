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
#include <media/videobuf2-dma-sg.h>
#include "dev.h"
#include "regs.h"

#define CIF_ISP_REQ_BUFS_MIN			0

static int mi_frame_end(struct rkisp_stream *stream);
static void rkisp_buf_queue(struct vb2_buffer *vb);

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
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 4, 4 },
		.cplanes = 3,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
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
	}, {
		.fourcc = V4L2_PIX_FMT_YUV422M,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 8, 8 },
		.cplanes = 3,
		.mplanes = 3,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
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
	}, {
		.fourcc = V4L2_PIX_FMT_YUV420,
		.fmt_type = FMT_YUV,
		.bpp = { 8, 8, 8 },
		.cplanes = 3,
		.mplanes = 1,
		.uv_swap = 0,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.output_format = ISP32_MI_OUTPUT_YUV420,
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
		.output_format = 0,
	},
	/* raw */
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.output_format = 0,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.output_format = 0,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.output_format = 0,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.fmt_type = FMT_BAYER,
		.bpp = { 8 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_YUV_PLA_OR_RAW8,
		.output_format = 0,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
		.output_format = 0,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
		.output_format = 0,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
		.output_format = 0,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.fmt_type = FMT_BAYER,
		.bpp = { 10 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
		.output_format = 0,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
		.output_format = 0,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
		.output_format = 0,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
		.output_format = 0,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
		.write_format = MI_CTRL_MP_WRITE_RAW12,
		.output_format = 0,
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
		.fourcc = V4L2_PIX_FMT_GREY,
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
		.fourcc = V4L2_PIX_FMT_Y10,
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
	}, {
		.fourcc = V4L2_PIX_FMT_Y12,
		.fmt_type = FMT_BAYER,
		.bpp = { 12 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.fmt_type = FMT_YUV,
		.bpp = { 16 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_YVYU,
		.fmt_type = FMT_YUV,
		.bpp = { 16 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_UYVY,
		.fmt_type = FMT_YUV,
		.bpp = { 16 },
		.mplanes = 1,
	}, {
		.fourcc = V4L2_PIX_FMT_VYUY,
		.fmt_type = FMT_YUV,
		.bpp = { 16 },
		.mplanes = 1,
	}, {
		.fourcc = V4l2_PIX_FMT_EBD8,
		.fmt_type = FMT_EBD,
		.bpp = { 8 },
		.mplanes = 1,
	}, {
		.fourcc = V4l2_PIX_FMT_SPD16,
		.fmt_type = FMT_SPD,
		.bpp = { 16 },
		.mplanes = 1,
	}
};

static struct stream_config rkisp2_dmatx0_stream_config = {
	.fmts = dmatx_fmts,
	.fmt_size = ARRAY_SIZE(dmatx_fmts),
	.frame_end_id = MI_RAW0_WR_FRAME,
	.mi = {
		.y_size_init = MI_RAW0_WR_SIZE,
		.y_base_ad_init = MI_RAW0_WR_BASE,
		.y_base_ad_shd = MI_RAW0_WR_BASE_SHD,
		.length = MI_RAW0_WR_LENGTH,
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
		.length = MI_RAW1_WR_LENGTH,
	},
	.dma = {
		.ctrl = CSI2RX_RAW1_WR_CTRL,
		.pic_size = CSI2RX_RAW1_WR_PIC_SIZE,
		.pic_offs = CSI2RX_RAW1_WR_PIC_OFF,
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
		.length = MI_RAW3_WR_LENGTH,
	},
	.dma = {
		.ctrl = CSI2RX_RAW3_WR_CTRL,
		.pic_size = CSI2RX_RAW3_WR_PIC_SIZE,
		.pic_offs = CSI2RX_RAW3_WR_PIC_OFF,
	},
};

static bool is_rdbk_stream(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	bool en = false;

	if ((dev->hdr.op_mode == HDR_RDBK_FRAME1 &&
	     stream->id == RKISP_STREAM_DMATX2) ||
	    (dev->hdr.op_mode == HDR_RDBK_FRAME2 &&
	     (stream->id == RKISP_STREAM_DMATX2 ||
	      stream->id == RKISP_STREAM_DMATX0)))
		en = true;
	return en;
}

static bool is_hdr_stream(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	bool en = false;

	if (stream->id == RKISP_STREAM_DMATX0 &&
	    (dev->hdr.op_mode == HDR_FRAMEX2_DDR ||
	     dev->hdr.op_mode == HDR_LINEX2_DDR))
		en = true;
	return en;
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

	rkisp_config_dcrop(stream, dcrop, async);

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "stream %d crop: %dx%d -> %dx%d\n", stream->id,
		 input_win->width, input_win->height,
		 dcrop->width, dcrop->height);

	return 0;
}

/* configure scale unit */
static int rkisp_stream_config_rsz(struct rkisp_stream *stream, bool async)
{
	struct rkisp_device *dev = stream->ispdev;
	struct v4l2_pix_format_mplane output_fmt = stream->out_fmt;
	struct capture_fmt *output_isp_fmt = &stream->out_isp_fmt;
	struct ispsd_out_fmt *input_isp_fmt =
			rkisp_get_ispsd_out_fmt(&dev->isp_sdev);
	struct v4l2_rect in_y, in_c, out_y, out_c;
	u32 xsubs_in = 1, ysubs_in = 1;
	u32 xsubs_out = 1, ysubs_out = 1;

	if (input_isp_fmt->fmt_type == FMT_BAYER)
		goto disable;

	/* set input and output sizes for scale calculation */
	in_y.width = stream->dcrop.width;
	in_y.height = stream->dcrop.height;
	out_y.width = output_fmt.width;
	out_y.height = output_fmt.height;

	/* The size of Cb,Cr are related to the format */
	if (rkisp_mbus_code_xysubs(input_isp_fmt->mbus_code, &xsubs_in, &ysubs_in)) {
		v4l2_err(&dev->v4l2_dev, "Not xsubs/ysubs found\n");
		return -EINVAL;
	}
	in_c.width = in_y.width / xsubs_in;
	in_c.height = in_y.height / ysubs_in;

	if (output_isp_fmt->fmt_type == FMT_YUV) {
		rkisp_fcc_xysubs(output_isp_fmt->fourcc, &xsubs_out, &ysubs_out);
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
	rkisp_config_rsz(stream, &in_y, &in_c, &out_y, &out_c, async);

	if (rkisp_debug)
		rkisp_dump_rsz_regs(stream);

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

       /*
	* NOTE: plane_fmt[0].sizeimage is total size of all planes for single
	* memory plane formats, so calculate the size explicitly.
	*/
	rkisp_write(dev, stream->config->mi.y_size_init,
		    stream->out_fmt.plane_fmt[0].bytesperline *
		    stream->out_fmt.height, false);
	rkisp_write(dev, stream->config->mi.cb_size_init,
		    stream->out_fmt.plane_fmt[1].sizeimage, false);
	rkisp_write(dev, stream->config->mi.cr_size_init,
		    stream->out_fmt.plane_fmt[2].sizeimage, false);

	rkisp_set_bits(dev, CIF_MI_XTD_FORMAT_CTRL, CIF_MI_XTD_FMT_CTRL_MP_CB_CR_SWAP,
		 stream->out_isp_fmt.uv_swap ? CIF_MI_XTD_FMT_CTRL_MP_CB_CR_SWAP : 0, false);

	rkisp_set_bits(dev, CIF_MI_CTRL, GENMASK(19, 16) | MI_CTRL_MP_FMT_MASK,
			calc_burst_len(stream) | CIF_MI_CTRL_INIT_BASE_EN |
			CIF_MI_CTRL_INIT_OFFSET_EN | CIF_MI_MP_AUTOUPDATE_ENABLE |
			stream->out_isp_fmt.write_format, false);
	mi_frame_end_int_enable(stream);
	/* set up first buffer */
	mi_frame_end(stream);
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
	struct capture_fmt *output_isp_fmt = &stream->out_isp_fmt;
	struct ispsd_out_fmt *input_isp_fmt =
			rkisp_get_ispsd_out_fmt(&dev->isp_sdev);
	u32 sp_in_fmt, mul = 1;

	if (mbus_code_sp_in_fmt(input_isp_fmt->mbus_code,
				output_isp_fmt->fourcc, &sp_in_fmt)) {
		v4l2_err(&dev->v4l2_dev, "Can't find the input format\n");
		return -EINVAL;
	}

       /*
	* NOTE: plane_fmt[0].sizeimage is total size of all planes for single
	* memory plane formats, so calculate the size explicitly.
	*/
	rkisp_write(dev, stream->config->mi.y_size_init,
		    stream->out_fmt.plane_fmt[0].bytesperline *
		    stream->out_fmt.height, false);
	rkisp_write(dev, stream->config->mi.cb_size_init,
		    stream->out_fmt.plane_fmt[1].sizeimage, false);
	rkisp_write(dev, stream->config->mi.cr_size_init,
		    stream->out_fmt.plane_fmt[2].sizeimage, false);
	rkisp_write(dev, CIF_MI_SP_Y_PIC_WIDTH, stream->out_fmt.width, false);
	if (stream->interlaced) {
		mul = 2;
		stream->u.sp.vir_offs = stream->out_fmt.plane_fmt[0].bytesperline;
	}
	rkisp_write(dev, CIF_MI_SP_Y_PIC_HEIGHT, stream->out_fmt.height / mul, false);
	rkisp_write(dev, CIF_MI_SP_Y_LLENGTH, stream->u.sp.y_stride * mul, false);

	rkisp_set_bits(dev, CIF_MI_XTD_FORMAT_CTRL, CIF_MI_XTD_FMT_CTRL_SP_CB_CR_SWAP,
		output_isp_fmt->uv_swap ? CIF_MI_XTD_FMT_CTRL_SP_CB_CR_SWAP : 0, false);

	rkisp_set_bits(dev, CIF_MI_CTRL, GENMASK(19, 16) | MI_CTRL_SP_FMT_MASK,
			calc_burst_len(stream) | CIF_MI_CTRL_INIT_BASE_EN |
			CIF_MI_CTRL_INIT_OFFSET_EN | stream->out_isp_fmt.write_format |
			sp_in_fmt | output_isp_fmt->output_format |
			CIF_MI_SP_AUTOUPDATE_ENABLE, false);
	mi_frame_end_int_enable(stream);
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

	if (!csi->sink[CSI_SRC_CH4 - 1].linked || stream->streaming)
		return -EBUSY;

	if (!dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2_DPHY)) {
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
	mi_set_y_size(stream, in_size);
	mi_frame_end(stream);
	mi_frame_end_int_enable(stream);
	mi_wr_ctrl2(base, SW_RAW3_WR_AUTOUPD);
	mi_raw_length(stream);
	vc = csi->sink[CSI_SRC_CH4 - 1].index;
	raw_wr_ctrl(stream,
		SW_CSI_RAW_WR_CH_EN(vc) |
		stream->memory |
		SW_CSI_RAW_WR_EN_ORG);
	stream->u.dmatx.is_config = true;
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

	if (!csi->sink[CSI_SRC_CH3 - 1].linked || stream->streaming)
		return -EBUSY;

	if (!dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2_DPHY)) {
		v4l2_err(&dev->v4l2_dev,
			 "only mipi sensor support rawwr2 path\n");
		return -EINVAL;
	}

	if (!stream->u.dmatx.is_config) {
		stream->u.dmatx.is_config = true;
		atomic_set(&stream->sequence, 0);
		in_size = stream->out_fmt.plane_fmt[0].sizeimage;
		raw_wr_set_pic_size(stream,
				    stream->out_fmt.width,
				    stream->out_fmt.height);
		raw_wr_set_pic_offs(stream, 0);
		mi_set_y_size(stream, in_size);
		mi_frame_end(stream);
		mi_frame_end_int_enable(stream);
		mi_wr_ctrl2(base, SW_RAW1_WR_AUTOUPD);
		mi_raw_length(stream);
		vc = csi->sink[CSI_SRC_CH3 - 1].index;
		/* short frame for esp mode */
		if (dev->hdr.esp_mode != HDR_NORMAL_VC)
			vc = 2;
		val = SW_CSI_RAW_WR_CH_EN(vc);
		val |= stream->memory;
		if (dev->hdr.op_mode != HDR_NORMAL)
			val |= SW_CSI_RAW_WR_EN_ORG;
		raw_wr_ctrl(stream, val);
	}
	return 0;
}

static int dmatx0_config_mi(struct rkisp_stream *stream)
{
	void __iomem *base = stream->ispdev->base_addr;
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_csi_device *csi = &dev->csi_dev;
	u32 val, in_size;
	u8 vc;

	if (!csi->sink[CSI_SRC_CH1 - 1].linked || stream->streaming)
		return -EBUSY;

	if (!dev->active_sensor ||
	    (dev->active_sensor &&
	     dev->active_sensor->mbus.type != V4L2_MBUS_CSI2_DPHY)) {
		if (stream->id == RKISP_STREAM_DMATX0)
			v4l2_err(&dev->v4l2_dev,
				 "only mipi sensor support rawwr0 path\n");
		return -EINVAL;
	}

	if (!stream->u.dmatx.is_config) {
		stream->u.dmatx.is_config = true;
		atomic_set(&stream->sequence, 0);
		in_size = stream->out_fmt.plane_fmt[0].sizeimage;
		raw_wr_set_pic_size(stream,
				    stream->out_fmt.width,
				    stream->out_fmt.height);
		raw_wr_set_pic_offs(stream, 0);
		mi_set_y_size(stream, in_size);
		mi_frame_end(stream);
		mi_frame_end_int_enable(stream);
		mi_wr_ctrl2(base, SW_RAW0_WR_AUTOUPD);
		mi_raw_length(stream);
		vc = csi->sink[CSI_SRC_CH1 - 1].index;
		/* long frame for esp mode */
		if (dev->hdr.esp_mode != HDR_NORMAL_VC)
			vc = 1;
		val = SW_CSI_RAW_WR_CH_EN(vc);
		val |= stream->memory;
		if (dev->hdr.op_mode != HDR_NORMAL)
			val |= SW_CSI_RAW_WR_EN_ORG;
		raw_wr_ctrl(stream, val);
	}

	return 0;
}

static void mp_enable_mi(struct rkisp_stream *stream)
{
	struct capture_fmt *isp_fmt = &stream->out_isp_fmt;
	u32 val = CIF_MI_CTRL_MP_ENABLE;

	if (isp_fmt->fmt_type == FMT_BAYER)
		val = CIF_MI_CTRL_RAW_ENABLE;
	rkisp_set_bits(stream->ispdev, CIF_MI_CTRL,
			CIF_MI_CTRL_MP_ENABLE | CIF_MI_CTRL_RAW_ENABLE, val, false);
}

static void sp_enable_mi(struct rkisp_stream *stream)
{
	rkisp_set_bits(stream->ispdev, CIF_MI_CTRL, 0,
			CIF_MI_CTRL_SP_ENABLE, false);
}

static void dmatx_enable_mi(struct rkisp_stream *stream)
{
	raw_wr_enable(stream);
}

static void mp_disable_mi(struct rkisp_stream *stream)
{
	rkisp_clear_bits(stream->ispdev, CIF_MI_CTRL,
			 CIF_MI_CTRL_MP_ENABLE | CIF_MI_CTRL_RAW_ENABLE, false);
}

static void sp_disable_mi(struct rkisp_stream *stream)
{
	rkisp_clear_bits(stream->ispdev, CIF_MI_CTRL, CIF_MI_CTRL_SP_ENABLE, false);
}

static void update_dmatx_v2(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_dummy_buffer *buf = NULL;
	u8 index;

	if (stream->next_buf) {
		mi_set_y_addr(stream, stream->next_buf->buff_addr[RKISP_PLANE_Y]);
	} else {
		if (stream->id == RKISP_STREAM_DMATX0)
			index = dev->hdr.index[HDR_DMA0];
		else if (stream->id == RKISP_STREAM_DMATX2)
			index = dev->hdr.index[HDR_DMA2];

		if ((stream->id == RKISP_STREAM_DMATX0 ||
		     stream->id == RKISP_STREAM_DMATX2)) {
			buf = hdr_dqbuf(&dev->hdr.q_tx[index]);
			if (IS_HDR_RDBK(dev->hdr.op_mode) &&
			    !dev->dmarx_dev.trigger)
				hdr_qbuf(&dev->hdr.q_rx[index], buf);
			else
				hdr_qbuf(&dev->hdr.q_tx[index], buf);
		}
		if (!buf && dev->hw_dev->dummy_buf.mem_priv) {
			buf = &dev->hw_dev->dummy_buf;
			stream->dbg.frameloss++;
		}
		if (buf)
			mi_set_y_addr(stream, buf->dma_addr);
	}
	v4l2_dbg(2, rkisp_debug, &dev->v4l2_dev,
		 "%s stream:%d Y:0x%x SHD:0x%x\n",
		 __func__, stream->id,
		 rkisp_read(dev, stream->config->mi.y_base_ad_init, true),
		 rkisp_read(dev, stream->config->mi.y_base_ad_shd, true));
}

/* Update buffer info to memory interface, it's called in interrupt */
static void update_mi(struct rkisp_stream *stream)
{
	struct rkisp_dummy_buffer *dummy_buf = &stream->ispdev->hw_dev->dummy_buf;
	struct rkisp_device *dev = stream->ispdev;

	/* The dummy space allocated by dma_alloc_coherent is used, we can
	 * throw data to it if there is no available buffer.
	 */
	if (stream->next_buf) {
		rkisp_write(dev, stream->config->mi.y_base_ad_init,
			    stream->next_buf->buff_addr[RKISP_PLANE_Y], false);
		rkisp_write(dev, stream->config->mi.cb_base_ad_init,
			    stream->next_buf->buff_addr[RKISP_PLANE_CB], false);
		rkisp_write(dev, stream->config->mi.cr_base_ad_init,
			    stream->next_buf->buff_addr[RKISP_PLANE_CR], false);
		/* mp/sp single buf updated at readback for multidevice */
		if (!dev->hw_dev->is_single) {
			stream->curr_buf = stream->next_buf;
			stream->next_buf = NULL;
		}
	} else if (dummy_buf->mem_priv) {
		rkisp_write(dev, stream->config->mi.y_base_ad_init,
			    dummy_buf->dma_addr, false);
		rkisp_write(dev, stream->config->mi.cb_base_ad_init,
			    dummy_buf->dma_addr, false);
		rkisp_write(dev, stream->config->mi.cr_base_ad_init,
			    dummy_buf->dma_addr, false);
		stream->dbg.frameloss++;
	}

	mi_set_y_offset(stream, 0);
	mi_set_cb_offset(stream, 0);
	mi_set_cr_offset(stream, 0);
	v4l2_dbg(2, rkisp_debug, &stream->ispdev->v4l2_dev,
		 "%s stream:%d Y:0x%x CB:0x%x CR:0x%x | Y_SHD:0x%x\n",
		 __func__, stream->id,
		 rkisp_read(dev, stream->config->mi.y_base_ad_init, false),
		 rkisp_read(dev, stream->config->mi.cb_base_ad_init, false),
		 rkisp_read(dev, stream->config->mi.cr_base_ad_init, false),
		 rkisp_read(dev, stream->config->mi.y_base_ad_shd, true));
}

static void mp_stop_mi(struct rkisp_stream *stream)
{
	stream->ops->disable_mi(stream);
}

static void sp_stop_mi(struct rkisp_stream *stream)
{
	stream->ops->disable_mi(stream);
}

static void dmatx_stop_mi(struct rkisp_stream *stream)
{
	raw_wr_disable(stream);
	stream->u.dmatx.is_config = false;
}

static struct streams_ops rkisp_mp_streams_ops = {
	.config_mi = mp_config_mi,
	.enable_mi = mp_enable_mi,
	.disable_mi = mp_disable_mi,
	.stop_mi = mp_stop_mi,
	.set_data_path = stream_data_path,
	.is_stream_stopped = mp_is_stream_stopped,
	.update_mi = update_mi,
	.frame_end = mi_frame_end,
};

static struct streams_ops rkisp_sp_streams_ops = {
	.config_mi = sp_config_mi,
	.enable_mi = sp_enable_mi,
	.disable_mi = sp_disable_mi,
	.stop_mi = sp_stop_mi,
	.set_data_path = stream_data_path,
	.is_stream_stopped = sp_is_stream_stopped,
	.update_mi = update_mi,
	.frame_end = mi_frame_end,
};

static struct streams_ops rkisp2_dmatx0_streams_ops = {
	.config_mi = dmatx0_config_mi,
	.enable_mi = dmatx_enable_mi,
	.stop_mi = dmatx_stop_mi,
	.is_stream_stopped = dmatx0_is_stream_stopped,
	.update_mi = update_dmatx_v2,
	.frame_end = mi_frame_end,
};

static struct streams_ops rkisp2_dmatx2_streams_ops = {
	.config_mi = dmatx2_config_mi,
	.enable_mi = dmatx_enable_mi,
	.stop_mi = dmatx_stop_mi,
	.is_stream_stopped = dmatx2_is_stream_stopped,
	.update_mi = update_dmatx_v2,
	.frame_end = mi_frame_end,
};

static struct streams_ops rkisp2_dmatx3_streams_ops = {
	.config_mi = dmatx3_config_mi,
	.enable_mi = dmatx_enable_mi,
	.stop_mi = dmatx_stop_mi,
	.is_stream_stopped = dmatx3_is_stream_stopped,
	.update_mi = update_dmatx_v2,
	.frame_end = mi_frame_end,
};

static void rdbk_frame_end(struct rkisp_stream *stream)
{
	struct rkisp_device *isp_dev = stream->ispdev;
	struct rkisp_capture_device *cap = &isp_dev->cap_dev;
	struct rkisp_sensor_info *sensor = isp_dev->active_sensor;
	u32 denominator = sensor->fi.interval.denominator;
	u32 numerator = sensor->fi.interval.numerator;
	u64 l_ts, s_ts;
	int ret, fps = -1, time = 30000000;

	if (stream->id != RKISP_STREAM_DMATX2)
		return;

	if (isp_dev->hdr.op_mode == HDR_RDBK_FRAME1) {
		rkisp_stream_buf_done(stream, cap->rdbk_buf[RDBK_S]);
		cap->rdbk_buf[RDBK_S] = NULL;
		return;
	}

	if (denominator && numerator)
		time = numerator * 1000 / denominator * 1000 * 1000;

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

		cap->rdbk_buf[RDBK_S]->vb.sequence = cap->rdbk_buf[RDBK_L]->vb.sequence;
		rkisp_stream_buf_done(&cap->stream[RKISP_STREAM_DMATX0], cap->rdbk_buf[RDBK_L]);
		rkisp_stream_buf_done(stream, cap->rdbk_buf[RDBK_S]);
	} else {
		v4l2_err(&isp_dev->v4l2_dev, "lost long frames\n");
		goto RDBK_FRM_UNMATCH;
	}

	cap->rdbk_buf[RDBK_L] = NULL;
	cap->rdbk_buf[RDBK_S] = NULL;
	return;

RDBK_FRM_UNMATCH:
	stream->dbg.frameloss++;
	if (cap->rdbk_buf[RDBK_L])
		rkisp_buf_queue(&cap->rdbk_buf[RDBK_L]->vb.vb2_buf);
	if (cap->rdbk_buf[RDBK_S])
		rkisp_buf_queue(&cap->rdbk_buf[RDBK_S]->vb.vb2_buf);

	cap->rdbk_buf[RDBK_L] = NULL;
	cap->rdbk_buf[RDBK_S] = NULL;
}

/*
 * This function is called when a frame end come. The next frame
 * is processing and we should set up buffer for next-next frame,
 * otherwise it will overflow.
 */
static int mi_frame_end(struct rkisp_stream *stream)
{
	struct rkisp_device *dev = stream->ispdev;
	struct rkisp_capture_device *cap = &dev->cap_dev;
	struct capture_fmt *isp_fmt = &stream->out_isp_fmt;
	bool interlaced = stream->interlaced;
	unsigned long lock_flags = 0;
	int i = 0;

	if (!stream->next_buf && stream->streaming &&
	    dev->dmarx_dev.trigger == T_MANUAL &&
	    is_rdbk_stream(stream))
		v4l2_info(&dev->v4l2_dev,
			  "tx stream:%d lose frame:%d, isp state:0x%x frame:%d\n",
			  stream->id, atomic_read(&stream->sequence) - 1,
			  dev->isp_state, dev->dmarx_dev.cur_frame.id);

	if (stream->curr_buf &&
	    (!interlaced ||
	     (stream->u.sp.field_rec == RKISP_FIELD_ODD &&
	      stream->u.sp.field == RKISP_FIELD_EVEN))) {
		struct vb2_buffer *vb2_buf = &stream->curr_buf->vb.vb2_buf;
		u64 ns = 0;

		/* Dequeue a filled buffer */
		for (i = 0; i < isp_fmt->mplanes; i++) {
			u32 payload_size =
				stream->out_fmt.plane_fmt[i].sizeimage;
			vb2_set_plane_payload(vb2_buf, i, payload_size);
		}
		if (stream->id == RKISP_STREAM_MP ||
		    stream->id == RKISP_STREAM_SP) {
			rkisp_dmarx_get_frame(dev, &i, NULL, &ns, true);
			atomic_set(&stream->sequence, i);
			stream->curr_buf->vb.sequence = i;
		} else {
			stream->curr_buf->vb.sequence =
				atomic_read(&stream->sequence) - 1;
		}
		if (!ns)
			ns = ktime_get_ns();
		vb2_buf->timestamp = ns;

		ns = ktime_get_ns();
		stream->dbg.interval = ns - stream->dbg.timestamp;
		stream->dbg.timestamp = ns;
		stream->dbg.id = stream->curr_buf->vb.sequence;
		if (stream->id == RKISP_STREAM_MP || stream->id == RKISP_STREAM_SP)
			stream->dbg.delay = ns - dev->isp_sdev.frm_timestamp;

		if (!stream->streaming) {
			vb2_buffer_done(vb2_buf, VB2_BUF_STATE_ERROR);
		} else if (is_rdbk_stream(stream) &&
			   dev->dmarx_dev.trigger == T_MANUAL) {
			if (stream->id == RKISP_STREAM_DMATX0) {
				if (cap->rdbk_buf[RDBK_L]) {
					v4l2_err(&dev->v4l2_dev,
						 "multiple long data in hdr frame\n");
					rkisp_buf_queue(&cap->rdbk_buf[RDBK_L]->vb.vb2_buf);
				}
				cap->rdbk_buf[RDBK_L] = stream->curr_buf;
			} else {
				if (cap->rdbk_buf[RDBK_S]) {
					v4l2_err(&dev->v4l2_dev,
						 "multiple short data in hdr frame\n");
					rkisp_buf_queue(&cap->rdbk_buf[RDBK_S]->vb.vb2_buf);
				}
				cap->rdbk_buf[RDBK_S] = stream->curr_buf;
				rdbk_frame_end(stream);
			}
		} else {
			rkisp_stream_buf_done(stream, stream->curr_buf);
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

	if (!dev->dmarx_dev.trigger &&
	    (is_rdbk_stream(stream) || is_hdr_stream(stream))) {
		stream->streaming = false;
		return;
	}

	stream->stopping = true;
	if ((!dev->hw_dev->is_single && stream->id != RKISP_STREAM_MP &&
	     stream->id != RKISP_STREAM_SP) || dev->hw_dev->is_single)
		stream->ops->stop_mi(stream);

	if (stream->id == RKISP_STREAM_MP || stream->id == RKISP_STREAM_SP) {
		hdr_stop_dmatx(dev);
		if (IS_HDR_RDBK(dev->rd_mode)) {
			spin_lock_irqsave(&dev->hw_dev->rdbk_lock, lock_flags);
			if (dev->hw_dev->cur_dev_id != dev->dev_id || dev->hw_dev->is_idle)
				is_wait = false;
			if (atomic_read(&dev->cap_dev.refcnt) == 1 && !is_wait)
				dev->isp_state = ISP_STOP;
			spin_unlock_irqrestore(&dev->hw_dev->rdbk_lock, lock_flags);
		}
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
	if (stream->id == RKISP_STREAM_MP || stream->id == RKISP_STREAM_SP) {
		stream->ops->disable_mi(stream);
		rkisp_disable_dcrop(stream, true);
		rkisp_disable_rsz(stream, true);
		ret = (stream->id == RKISP_STREAM_MP) ?
			ISP_FRAME_MP : ISP_FRAME_SP;
		dev->irq_ends_mask &= ~ret;
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
	struct rkisp_device *dev = stream->ispdev;
	bool is_update = false;
	int ret;

	if (stream->id == RKISP_STREAM_MP || stream->id == RKISP_STREAM_SP) {
		is_update = (stream->id == RKISP_STREAM_MP) ?
			!dev->cap_dev.stream[RKISP_STREAM_SP].streaming :
			!dev->cap_dev.stream[RKISP_STREAM_MP].streaming;
	}

	if (stream->ops->set_data_path)
		stream->ops->set_data_path(stream);
	ret = stream->ops->config_mi(stream);
	if (ret)
		return ret;

	stream->ops->enable_mi(stream);
	if (stream->id == RKISP_STREAM_MP || stream->id == RKISP_STREAM_SP)
		hdr_config_dmatx(dev);
	if (is_update)
		dev->irq_ends_mask |=
			(stream->id == RKISP_STREAM_MP) ? ISP_FRAME_MP : ISP_FRAME_SP;
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
	struct sg_table *sgt;
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

	hdr_destroy_buf(dev);
	rkisp_free_common_dummy_buf(dev);
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

	mutex_lock(&dev->hw_dev->dev_lock);

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s %d\n", __func__, stream->id);

	if (!stream->streaming)
		goto end;

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
	tasklet_disable(&stream->buf_done_tasklet);
end:
	mutex_unlock(&dev->hw_dev->dev_lock);
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

	ret = rkisp_stream_config_rsz(stream, async);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "config rsz failed with error %d\n", ret);
		return ret;
	}

	/*
	 * can't be async now, otherwise the latter started stream fails to
	 * produce mi interrupt.
	 */
	ret = rkisp_stream_config_dcrop(stream, false);
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

	mutex_lock(&dev->hw_dev->dev_lock);

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s %d\n", __func__, stream->id);

	if (WARN_ON(stream->streaming)) {
		mutex_unlock(&dev->hw_dev->dev_lock);
		return -EBUSY;
	}

	memset(&stream->dbg, 0, sizeof(stream->dbg));
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
	    dev->active_sensor->fmt[0].format.field == V4L2_FIELD_INTERLACED) {
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
		strlcpy(vdev->name, SP_VDEV_NAME,
			sizeof(vdev->name));
		stream->ops = &rkisp_sp_streams_ops;
		stream->config = &rkisp_sp_stream_config;
		stream->config->fmts = sp_fmts;
		stream->config->fmt_size = ARRAY_SIZE(sp_fmts);
		break;
	case RKISP_STREAM_DMATX0:
		strlcpy(vdev->name, DMATX0_VDEV_NAME,
			sizeof(vdev->name));
		stream->ops = &rkisp2_dmatx0_streams_ops;
		stream->config = &rkisp2_dmatx0_stream_config;
		break;
	case RKISP_STREAM_DMATX2:
		strlcpy(vdev->name, DMATX2_VDEV_NAME,
			sizeof(vdev->name));
		stream->ops = &rkisp2_dmatx2_streams_ops;
		stream->config = &rkisp2_dmatx1_stream_config;
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
		stream->config->fmts = mp_fmts;
		stream->config->fmt_size = ARRAY_SIZE(mp_fmts);
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

int rkisp_register_stream_v21(struct rkisp_device *dev)
{
	struct rkisp_capture_device *cap_dev = &dev->cap_dev;
	int ret;

	ret = rkisp_stream_init(dev, RKISP_STREAM_MP);
	if (ret < 0)
		goto err;
	ret = rkisp_stream_init(dev, RKISP_STREAM_SP);
	if (ret < 0)
		goto err_free_mp;
	ret = rkisp_stream_init(dev, RKISP_STREAM_DMATX0);
	if (ret < 0)
		goto err_free_sp;
	ret = rkisp_stream_init(dev, RKISP_STREAM_DMATX2);
	if (ret < 0)
		goto err_free_tx0;
	ret = rkisp_stream_init(dev, RKISP_STREAM_DMATX3);
	if (ret < 0)
		goto err_free_tx2;

	return 0;
err_free_tx2:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_DMATX2]);
err_free_tx0:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_DMATX0]);
err_free_sp:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_SP]);
err_free_mp:
	rkisp_unregister_stream_vdev(&cap_dev->stream[RKISP_STREAM_MP]);
err:
	return ret;
}

void rkisp_unregister_stream_v21(struct rkisp_device *dev)
{
	struct rkisp_capture_device *cap_dev = &dev->cap_dev;
	struct rkisp_stream *stream;

	stream = &cap_dev->stream[RKISP_STREAM_MP];
	rkisp_unregister_stream_vdev(stream);
	stream = &cap_dev->stream[RKISP_STREAM_SP];
	rkisp_unregister_stream_vdev(stream);
	stream = &cap_dev->stream[RKISP_STREAM_DMATX0];
	rkisp_unregister_stream_vdev(stream);
	stream = &cap_dev->stream[RKISP_STREAM_DMATX2];
	rkisp_unregister_stream_vdev(stream);
	stream = &cap_dev->stream[RKISP_STREAM_DMATX3];
	rkisp_unregister_stream_vdev(stream);
}

/****************  Interrupter Handler ****************/

void rkisp_mi_v21_isr(u32 mis_val, struct rkisp_device *dev)
{
	struct rkisp_stream *stream;
	unsigned int i;
	static u8 end_tx0, end_tx2;

	v4l2_dbg(3, rkisp_debug, &dev->v4l2_dev,
		 "mi isr:0x%x\n", mis_val);

	if (mis_val & CIF_MI_DMA_READY)
		rkisp_dmarx_isr(mis_val, dev);

	for (i = 0; i < RKISP_MAX_STREAM; ++i) {
		stream = &dev->cap_dev.stream[i];

		if (!(mis_val & CIF_MI_FRAME(stream)))
			continue;

		if (i == RKISP_STREAM_DMATX0)
			end_tx0 = true;
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
			if (!dev->hw_dev->is_single &&
			    (stream->id == RKISP_STREAM_MP || stream->id == RKISP_STREAM_SP)) {
				stream->stopping = false;
				stream->streaming = false;
				stream->ops->disable_mi(stream);
				wake_up(&stream->done);
			} else if (stream->ops->is_stream_stopped(stream)) {
				stream->stopping = false;
				stream->streaming = false;
				wake_up(&stream->done);
			}
			if (i == RKISP_STREAM_MP) {
				end_tx0 = false;
				end_tx2 = false;
			}
		} else {
			mi_frame_end(stream);
			if (dev->dmarx_dev.trigger == T_AUTO &&
			    ((dev->hdr.op_mode == HDR_RDBK_FRAME1 && end_tx2) ||
			     (dev->hdr.op_mode == HDR_RDBK_FRAME2 && end_tx2 && end_tx0))) {
				end_tx0 = false;
				end_tx2 = false;
				rkisp_trigger_read_back(dev, false, false, false);
			}
		}
	}

	if (mis_val & CIF_MI_MP_FRAME) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_MP];
		if (!stream->streaming)
			dev->irq_ends_mask &= ~ISP_FRAME_MP;
		else
			dev->irq_ends_mask |= ISP_FRAME_MP;
		rkisp_check_idle(dev, ISP_FRAME_MP);
	}
	if (mis_val & CIF_MI_SP_FRAME) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_SP];
		if (!stream->streaming)
			dev->irq_ends_mask &= ~ISP_FRAME_SP;
		else
			dev->irq_ends_mask |= ISP_FRAME_SP;
		rkisp_check_idle(dev, ISP_FRAME_SP);
	}
}

void rkisp_mipi_v21_isr(unsigned int phy, unsigned int packet,
			unsigned int overflow, unsigned int state,
			struct rkisp_device *dev)
{
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct rkisp_stream *stream;
	u32 packet_err = PACKET_ERR_F_BNDRY_MATCG | PACKET_ERR_F_SEQ |
		PACKET_ERR_FRAME_DATA | PACKET_ERR_ECC_1BIT |
		PACKET_ERR_ECC_2BIT | PACKET_ERR_CHECKSUM;
	u32 state_err = RAW_WR_SIZE_ERR | RAW_RD_SIZE_ERR;
	int i, id;

	v4l2_dbg(3, rkisp_debug, &dev->v4l2_dev,
		 "csi state:0x%x\n", state);
	dev->csi_dev.irq_cnt++;
	if (phy && (dev->isp_inp & INP_CSI) &&
	    dev->csi_dev.err_cnt++ < RKISP_CONTI_ERR_MAX)
		v4l2_warn(v4l2_dev, "MIPI error: phy: 0x%08x\n", phy);
	if ((packet & packet_err) && (dev->isp_inp & INP_CSI) &&
	    dev->csi_dev.err_cnt < RKISP_CONTI_ERR_MAX) {
		if (packet & 0xfff)
			dev->csi_dev.err_cnt++;
		v4l2_warn(v4l2_dev, "MIPI error: packet: 0x%08x\n", packet);
	}
	if (overflow && dev->csi_dev.err_cnt++ < RKISP_CONTI_ERR_MAX)
		v4l2_warn(v4l2_dev, "MIPI error: overflow: 0x%08x\n", overflow);
	if (state & state_err)
		v4l2_warn(v4l2_dev, "MIPI error: size: 0x%08x\n", state);
	if (state & ISP21_MIPI_DROP_FRM)
		v4l2_warn(v4l2_dev, "MIPI drop frame\n");

	/* first Y_STATE irq as csi sof event */
	if (state & (RAW0_Y_STATE | RAW1_Y_STATE)) {
		for (i = 0; i < HDR_DMA_MAX - 1; i++) {
			if (!((RAW0_Y_STATE << i) & state) ||
			    dev->csi_dev.tx_first[i])
				continue;
			dev->csi_dev.tx_first[i] = true;
			id = i ? 2 : 0;
			rkisp_csi_sof(dev, id);
			stream = &dev->cap_dev.stream[id + RKISP_STREAM_DMATX0];
			atomic_inc(&stream->sequence);
		}
	}
	if (state & (RAW0_WR_FRAME | RAW1_WR_FRAME)) {
		dev->csi_dev.err_cnt = 0;
		for (i = 0; i < HDR_DMA_MAX - 1; i++) {
			if (!((RAW0_WR_FRAME << i) & state))
				continue;
			if (!dev->csi_dev.tx_first[i]) {
				id = i ? RKISP_STREAM_DMATX2 : RKISP_STREAM_DMATX0;
				stream = &dev->cap_dev.stream[id];
				atomic_inc(&stream->sequence);
			}
			dev->csi_dev.tx_first[i] = false;
		}
	}
	if (state & ISP21_RAW3_WR_FRAME) {
		dev->csi_dev.err_cnt = 0;
		stream = &dev->cap_dev.stream[RKISP_STREAM_DMATX3];
		atomic_inc(&stream->sequence);
	}

	if (dev->csi_dev.err_cnt > RKISP_CONTI_ERR_MAX) {
		if (!(dev->isp_state & ISP_MIPI_ERROR)) {
			dev->isp_state |= ISP_MIPI_ERROR;
			rkisp_write(dev, CSI2RX_MASK_PHY, 0, true);
			rkisp_write(dev, CSI2RX_MASK_PACKET, 0, true);
			rkisp_write(dev, CSI2RX_MASK_OVERFLOW, 0, true);
			if (dev->hw_dev->monitor.is_en) {
				if (!completion_done(&dev->hw_dev->monitor.cmpl))
					complete(&dev->hw_dev->monitor.cmpl);
				dev->hw_dev->monitor.state |= ISP_MIPI_ERROR;
			}
		}
	}
}
