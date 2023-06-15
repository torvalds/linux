// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 Capture ISI subdev driver for i.MX8QXP/QM platform
 *
 * ISI is a Image Sensor Interface of i.MX8QXP/QM platform, which
 * used to process image from camera sensor to memory or DC
 *
 * Copyright (c) 2019 NXP Semiconductor
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/minmax.h>
#include <linux/pm_runtime.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>

#include "imx8-isi-core.h"
#include "imx8-isi-regs.h"

/* Keep the first entry matching MXC_ISI_DEF_PIXEL_FORMAT */
static const struct mxc_isi_format_info mxc_isi_formats[] = {
	/* YUV formats */
	{
		.mbus_code	= MEDIA_BUS_FMT_YUV8_1X24,
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.type		= MXC_ISI_VIDEO_CAP | MXC_ISI_VIDEO_M2M_OUT
				| MXC_ISI_VIDEO_M2M_CAP,
		.isi_in_format	= CHNL_MEM_RD_CTRL_IMG_TYPE_YUV422_1P8P,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_YUV422_1P8P,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_YUV,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUV8_1X24,
		.fourcc		= V4L2_PIX_FMT_YUVA32,
		.type		= MXC_ISI_VIDEO_CAP | MXC_ISI_VIDEO_M2M_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_YUV444_1P8,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 32 },
		.encoding	= MXC_ISI_ENC_YUV,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUV8_1X24,
		.fourcc		= V4L2_PIX_FMT_NV12,
		.type		= MXC_ISI_VIDEO_CAP | MXC_ISI_VIDEO_M2M_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_YUV420_2P8P,
		.color_planes	= 2,
		.mem_planes	= 1,
		.depth		= { 8, 16 },
		.hsub		= 2,
		.vsub		= 2,
		.encoding	= MXC_ISI_ENC_YUV,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUV8_1X24,
		.fourcc		= V4L2_PIX_FMT_NV12M,
		.type		= MXC_ISI_VIDEO_CAP | MXC_ISI_VIDEO_M2M_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_YUV420_2P8P,
		.mem_planes	= 2,
		.color_planes	= 2,
		.depth		= { 8, 16 },
		.hsub		= 2,
		.vsub		= 2,
		.encoding	= MXC_ISI_ENC_YUV,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUV8_1X24,
		.fourcc		= V4L2_PIX_FMT_NV16,
		.type		= MXC_ISI_VIDEO_CAP | MXC_ISI_VIDEO_M2M_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_YUV422_2P8P,
		.color_planes	= 2,
		.mem_planes	= 1,
		.depth		= { 8, 16 },
		.hsub		= 2,
		.vsub		= 1,
		.encoding	= MXC_ISI_ENC_YUV,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUV8_1X24,
		.fourcc		= V4L2_PIX_FMT_NV16M,
		.type		= MXC_ISI_VIDEO_CAP | MXC_ISI_VIDEO_M2M_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_YUV422_2P8P,
		.mem_planes	= 2,
		.color_planes	= 2,
		.depth		= { 8, 16 },
		.hsub		= 2,
		.vsub		= 1,
		.encoding	= MXC_ISI_ENC_YUV,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUV8_1X24,
		.fourcc		= V4L2_PIX_FMT_YUV444M,
		.type		= MXC_ISI_VIDEO_CAP | MXC_ISI_VIDEO_M2M_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_YUV444_3P8P,
		.mem_planes	= 3,
		.color_planes	= 3,
		.depth		= { 8, 8, 8 },
		.hsub		= 1,
		.vsub		= 1,
		.encoding	= MXC_ISI_ENC_YUV,
	},
	/* RGB formats */
	{
		.mbus_code	= MEDIA_BUS_FMT_RGB888_1X24,
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.type		= MXC_ISI_VIDEO_CAP | MXC_ISI_VIDEO_M2M_OUT
				| MXC_ISI_VIDEO_M2M_CAP,
		.isi_in_format	= CHNL_MEM_RD_CTRL_IMG_TYPE_RGB565,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RGB565,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RGB,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_RGB888_1X24,
		.fourcc		= V4L2_PIX_FMT_RGB24,
		.type		= MXC_ISI_VIDEO_CAP | MXC_ISI_VIDEO_M2M_OUT
				| MXC_ISI_VIDEO_M2M_CAP,
		.isi_in_format	= CHNL_MEM_RD_CTRL_IMG_TYPE_BGR8P,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_BGR888P,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 24 },
		.encoding	= MXC_ISI_ENC_RGB,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_RGB888_1X24,
		.fourcc		= V4L2_PIX_FMT_BGR24,
		.type		= MXC_ISI_VIDEO_CAP | MXC_ISI_VIDEO_M2M_OUT
				| MXC_ISI_VIDEO_M2M_CAP,
		.isi_in_format	= CHNL_MEM_RD_CTRL_IMG_TYPE_RGB8P,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RGB888P,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 24 },
		.encoding	= MXC_ISI_ENC_RGB,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_RGB888_1X24,
		.fourcc		= V4L2_PIX_FMT_XBGR32,
		.type		= MXC_ISI_VIDEO_CAP | MXC_ISI_VIDEO_M2M_OUT
				| MXC_ISI_VIDEO_M2M_CAP,
		.isi_in_format	= CHNL_MEM_RD_CTRL_IMG_TYPE_XBGR8,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_XRGB888,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 32 },
		.encoding	= MXC_ISI_ENC_RGB,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_RGB888_1X24,
		.fourcc		= V4L2_PIX_FMT_ABGR32,
		.type		= MXC_ISI_VIDEO_CAP | MXC_ISI_VIDEO_M2M_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_ARGB8888,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 32 },
		.encoding	= MXC_ISI_ENC_RGB,
	},
	/*
	 * RAW formats
	 *
	 * The ISI shifts the 10-bit and 12-bit formats left by 6 and 4 bits
	 * when using CHNL_IMG_CTRL_FORMAT_RAW10 or MXC_ISI_OUT_FMT_RAW12
	 * respectively, to align the bits to the left and pad with zeros in
	 * the LSBs. The corresponding V4L2 formats are however right-aligned,
	 * we have to use CHNL_IMG_CTRL_FORMAT_RAW16 to avoid the left shift.
	 */
	{
		.mbus_code	= MEDIA_BUS_FMT_Y8_1X8,
		.fourcc		= V4L2_PIX_FMT_GREY,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW8,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 8 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_Y10_1X10,
		.fourcc		= V4L2_PIX_FMT_Y10,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_Y12_1X12,
		.fourcc		= V4L2_PIX_FMT_Y12,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_Y14_1X14,
		.fourcc		= V4L2_PIX_FMT_Y14,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.fourcc		= V4L2_PIX_FMT_SBGGR8,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW8,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 8 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
		.fourcc		= V4L2_PIX_FMT_SGBRG8,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW8,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 8 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.fourcc		= V4L2_PIX_FMT_SGRBG8,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW8,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 8 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
		.fourcc		= V4L2_PIX_FMT_SRGGB8,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW8,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 8 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.fourcc		= V4L2_PIX_FMT_SBGGR10,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
		.fourcc		= V4L2_PIX_FMT_SGBRG10,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.fourcc		= V4L2_PIX_FMT_SGRBG10,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.fourcc		= V4L2_PIX_FMT_SRGGB10,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR12_1X12,
		.fourcc		= V4L2_PIX_FMT_SBGGR12,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG12_1X12,
		.fourcc		= V4L2_PIX_FMT_SGBRG12,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG12_1X12,
		.fourcc		= V4L2_PIX_FMT_SGRBG12,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB12_1X12,
		.fourcc		= V4L2_PIX_FMT_SRGGB12,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR14_1X14,
		.fourcc		= V4L2_PIX_FMT_SBGGR14,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG14_1X14,
		.fourcc		= V4L2_PIX_FMT_SGBRG14,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG14_1X14,
		.fourcc		= V4L2_PIX_FMT_SGRBG14,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB14_1X14,
		.fourcc		= V4L2_PIX_FMT_SRGGB14,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW16,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 16 },
		.encoding	= MXC_ISI_ENC_RAW,
	},
	/* JPEG */
	{
		.mbus_code	= MEDIA_BUS_FMT_JPEG_1X8,
		.fourcc		= V4L2_PIX_FMT_MJPEG,
		.type		= MXC_ISI_VIDEO_CAP,
		.isi_out_format	= CHNL_IMG_CTRL_FORMAT_RAW8,
		.mem_planes	= 1,
		.color_planes	= 1,
		.depth		= { 8 },
		.encoding	= MXC_ISI_ENC_RAW,
	}
};

const struct mxc_isi_format_info *
mxc_isi_format_by_fourcc(u32 fourcc, enum mxc_isi_video_type type)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mxc_isi_formats); i++) {
		const struct mxc_isi_format_info *fmt = &mxc_isi_formats[i];

		if (fmt->fourcc == fourcc && fmt->type & type)
			return fmt;
	}

	return NULL;
}

const struct mxc_isi_format_info *
mxc_isi_format_enum(unsigned int index, enum mxc_isi_video_type type)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mxc_isi_formats); i++) {
		const struct mxc_isi_format_info *fmt = &mxc_isi_formats[i];

		if (!(fmt->type & type))
			continue;

		if (!index)
			return fmt;

		index--;
	}

	return NULL;
}

const struct mxc_isi_format_info *
mxc_isi_format_try(struct mxc_isi_pipe *pipe, struct v4l2_pix_format_mplane *pix,
		   enum mxc_isi_video_type type)
{
	const struct mxc_isi_format_info *fmt;
	unsigned int max_width;
	unsigned int i;

	max_width = pipe->id == pipe->isi->pdata->num_channels - 1
		  ? MXC_ISI_MAX_WIDTH_UNCHAINED
		  : MXC_ISI_MAX_WIDTH_CHAINED;

	fmt = mxc_isi_format_by_fourcc(pix->pixelformat, type);
	if (!fmt)
		fmt = &mxc_isi_formats[0];

	pix->width = clamp(pix->width, MXC_ISI_MIN_WIDTH, max_width);
	pix->height = clamp(pix->height, MXC_ISI_MIN_HEIGHT, MXC_ISI_MAX_HEIGHT);
	pix->pixelformat = fmt->fourcc;
	pix->field = V4L2_FIELD_NONE;

	if (pix->colorspace == V4L2_COLORSPACE_DEFAULT) {
		pix->colorspace = MXC_ISI_DEF_COLOR_SPACE;
		pix->ycbcr_enc = MXC_ISI_DEF_YCBCR_ENC;
		pix->quantization = MXC_ISI_DEF_QUANTIZATION;
		pix->xfer_func = MXC_ISI_DEF_XFER_FUNC;
	}

	if (pix->ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT)
		pix->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(pix->colorspace);
	if (pix->quantization == V4L2_QUANTIZATION_DEFAULT) {
		bool is_rgb = fmt->encoding == MXC_ISI_ENC_RGB;

		pix->quantization =
			V4L2_MAP_QUANTIZATION_DEFAULT(is_rgb, pix->colorspace,
						      pix->ycbcr_enc);
	}
	if (pix->xfer_func == V4L2_XFER_FUNC_DEFAULT)
		pix->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(pix->colorspace);

	pix->num_planes = fmt->mem_planes;

	for (i = 0; i < fmt->color_planes; ++i) {
		struct v4l2_plane_pix_format *plane = &pix->plane_fmt[i];
		unsigned int bpl;

		/* The pitch must be identical for all planes. */
		if (i == 0)
			bpl = clamp(plane->bytesperline,
				    pix->width * fmt->depth[0] / 8,
				    65535U);
		else
			bpl = pix->plane_fmt[0].bytesperline;

		plane->bytesperline = bpl;

		plane->sizeimage = plane->bytesperline * pix->height;
		if (i >= 1)
			plane->sizeimage /= fmt->vsub;
	}

	/*
	 * For single-planar pixel formats with multiple color planes,
	 * concatenate the size of all planes and clear all planes but the
	 * first one.
	 */
	if (fmt->color_planes != fmt->mem_planes) {
		for (i = 1; i < fmt->color_planes; ++i) {
			struct v4l2_plane_pix_format *plane = &pix->plane_fmt[i];

			pix->plane_fmt[0].sizeimage += plane->sizeimage;
			plane->bytesperline = 0;
			plane->sizeimage = 0;
		}
	}

	return fmt;
}

/* -----------------------------------------------------------------------------
 * videobuf2 queue operations
 */

static void mxc_isi_video_frame_write_done(struct mxc_isi_pipe *pipe,
					   u32 status)
{
	struct mxc_isi_video *video = &pipe->video;
	struct device *dev = pipe->isi->dev;
	struct mxc_isi_buffer *next_buf;
	struct mxc_isi_buffer *buf;
	enum mxc_isi_buf_id buf_id;

	spin_lock(&video->buf_lock);

	/*
	 * The ISI hardware handles buffers using a ping-pong mechanism with
	 * two sets of destination addresses (with shadow registers to allow
	 * programming addresses for all planes atomically) named BUF1 and
	 * BUF2. Addresses can be loaded and copied to shadow registers at any
	 * at any time.
	 *
	 * The hardware keeps track of which buffer is being written to and
	 * automatically switches to the other buffer at frame end, copying the
	 * corresponding address to another set of shadow registers that track
	 * the address being written to. The active buffer tracking bits are
	 * accessible through the CHNL_STS register.
	 *
	 *  BUF1        BUF2  |   Event   | Action
	 *                    |           |
	 *                    |           | Program initial buffers
	 *                    |           | B0 in BUF1, B1 in BUF2
	 *                    | Start ISI |
	 * +----+             |           |
	 * | B0 |             |           |
	 * +----+             |           |
	 *             +----+ | FRM IRQ 0 | B0 complete, BUF2 now active
	 *             | B1 | |           | Program B2 in BUF1
	 *             +----+ |           |
	 * +----+             | FRM IRQ 1 | B1 complete, BUF1 now active
	 * | B2 |             |           | Program B3 in BUF2
	 * +----+             |           |
	 *             +----+ | FRM IRQ 2 | B2 complete, BUF2 now active
	 *             | B3 | |           | Program B4 in BUF1
	 *             +----+ |           |
	 * +----+             | FRM IRQ 3 | B3 complete, BUF1 now active
	 * | B4 |             |           | Program B5 in BUF2
	 * +----+             |           |
	 *        ...         |           |
	 *
	 * Races between address programming and buffer switching can be
	 * detected by checking if a frame end interrupt occurred after
	 * programming the addresses.
	 *
	 * As none of the shadow registers are accessible, races can occur
	 * between address programming and buffer switching. It is possible to
	 * detect the race condition by checking if a frame end interrupt
	 * occurred after programming the addresses, but impossible to
	 * determine if the race has been won or lost.
	 *
	 * In addition to this, we need to use discard buffers if no pending
	 * buffers are available. To simplify handling of discard buffer, we
	 * need to allocate three of them, as two can be active concurrently
	 * and we need to still be able to get hold of a next buffer. The logic
	 * could be improved to use two buffers only, but as all discard
	 * buffers share the same memory, an additional buffer is cheap.
	 */

	/* Check which buffer has just completed. */
	buf_id = pipe->isi->pdata->buf_active_reverse
	       ? (status & CHNL_STS_BUF1_ACTIVE ? MXC_ISI_BUF2 : MXC_ISI_BUF1)
	       : (status & CHNL_STS_BUF1_ACTIVE ? MXC_ISI_BUF1 : MXC_ISI_BUF2);

	buf = list_first_entry_or_null(&video->out_active,
				       struct mxc_isi_buffer, list);

	/* Safety check, this should really never happen. */
	if (!buf) {
		dev_warn(dev, "trying to access empty active list\n");
		goto done;
	}

	/*
	 * If the buffer that has completed doesn't match the buffer on the
	 * front of the active list, it means we have lost one frame end
	 * interrupt (or possibly a large odd number of interrupts, although
	 * quite unlikely).
	 *
	 * For instance, if IRQ1 is lost and we handle IRQ2, both B1 and B2
	 * have been completed, but B3 hasn't been programmed, BUF2 still
	 * addresses B1 and the ISI is now writing in B1 instead of B3. We
	 * can't complete B2 as that would result in out-of-order completion.
	 *
	 * The only option is to ignore this interrupt and try again. When IRQ3
	 * will be handled, we will complete B1 and be in sync again.
	 */
	if (buf->id != buf_id) {
		dev_dbg(dev, "buffer ID mismatch (expected %u, got %u), skipping\n",
			buf->id, buf_id);

		/*
		 * Increment the frame count by two to account for the missed
		 * and the ignored interrupts.
		 */
		video->frame_count += 2;
		goto done;
	}

	/* Pick the next buffer and queue it to the hardware. */
	next_buf = list_first_entry_or_null(&video->out_pending,
					    struct mxc_isi_buffer, list);
	if (!next_buf) {
		next_buf = list_first_entry_or_null(&video->out_discard,
						    struct mxc_isi_buffer, list);

		/* Safety check, this should never happen. */
		if (!next_buf) {
			dev_warn(dev, "trying to access empty discard list\n");
			goto done;
		}
	}

	mxc_isi_channel_set_outbuf(pipe, next_buf->dma_addrs, buf_id);
	next_buf->id = buf_id;

	/*
	 * Check if we have raced with the end of frame interrupt. If so, we
	 * can't tell if the ISI has recorded the new address, or is still
	 * using the previous buffer. We must assume the latter as that is the
	 * worst case.
	 *
	 * For instance, if we are handling IRQ1 and now detect the FRM
	 * interrupt, assume B2 has completed and the ISI has switched to BUF2
	 * using B1 just before we programmed B3. Unlike in the previous race
	 * condition, B3 has been programmed and will be written to the next
	 * time the ISI switches to BUF2. We can however handle this exactly as
	 * the first race condition, as we'll program B3 (still at the head of
	 * the pending list) when handling IRQ3.
	 */
	status = mxc_isi_channel_irq_status(pipe, false);
	if (status & CHNL_STS_FRM_STRD) {
		dev_dbg(dev, "raced with frame end interrupt\n");
		video->frame_count += 2;
		goto done;
	}

	/*
	 * The next buffer has been queued successfully, move it to the active
	 * list, and complete the current buffer.
	 */
	list_move_tail(&next_buf->list, &video->out_active);

	if (!buf->discard) {
		list_del_init(&buf->list);
		buf->v4l2_buf.sequence = video->frame_count;
		buf->v4l2_buf.vb2_buf.timestamp = ktime_get_ns();
		vb2_buffer_done(&buf->v4l2_buf.vb2_buf, VB2_BUF_STATE_DONE);
	} else {
		list_move_tail(&buf->list, &video->out_discard);
	}

	video->frame_count++;

done:
	spin_unlock(&video->buf_lock);
}

static void mxc_isi_video_free_discard_buffers(struct mxc_isi_video *video)
{
	unsigned int i;

	for (i = 0; i < video->pix.num_planes; i++) {
		struct mxc_isi_dma_buffer *buf = &video->discard_buffer[i];

		if (!buf->addr)
			continue;

		dma_free_coherent(video->pipe->isi->dev, buf->size, buf->addr,
				  buf->dma);
		buf->addr = NULL;
	}
}

static int mxc_isi_video_alloc_discard_buffers(struct mxc_isi_video *video)
{
	unsigned int i, j;

	/* Allocate memory for each plane. */
	for (i = 0; i < video->pix.num_planes; i++) {
		struct mxc_isi_dma_buffer *buf = &video->discard_buffer[i];

		buf->size = PAGE_ALIGN(video->pix.plane_fmt[i].sizeimage);
		buf->addr = dma_alloc_coherent(video->pipe->isi->dev, buf->size,
					       &buf->dma, GFP_DMA | GFP_KERNEL);
		if (!buf->addr) {
			mxc_isi_video_free_discard_buffers(video);
			return -ENOMEM;
		}

		dev_dbg(video->pipe->isi->dev,
			"discard buffer plane %u: %zu bytes @%pad (CPU address %p)\n",
			i, buf->size, &buf->dma, buf->addr);
	}

	/* Fill the DMA addresses in the discard buffers. */
	for (i = 0; i < ARRAY_SIZE(video->buf_discard); ++i) {
		struct mxc_isi_buffer *buf = &video->buf_discard[i];

		buf->discard = true;

		for (j = 0; j < video->pix.num_planes; ++j)
			buf->dma_addrs[j] = video->discard_buffer[j].dma;
	}

	return 0;
}

static int mxc_isi_video_validate_format(struct mxc_isi_video *video)
{
	const struct v4l2_mbus_framefmt *format;
	const struct mxc_isi_format_info *info;
	struct v4l2_subdev_state *state;
	struct v4l2_subdev *sd = &video->pipe->sd;
	int ret = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	info = mxc_isi_format_by_fourcc(video->pix.pixelformat,
					MXC_ISI_VIDEO_CAP);
	format = v4l2_subdev_get_try_format(sd, state, MXC_ISI_PIPE_PAD_SOURCE);

	if (format->code != info->mbus_code ||
	    format->width != video->pix.width ||
	    format->height != video->pix.height) {
		dev_dbg(video->pipe->isi->dev,
			"%s: configuration mismatch, 0x%04x/%ux%u != 0x%04x/%ux%u\n",
			__func__, format->code, format->width, format->height,
			info->mbus_code, video->pix.width, video->pix.height);
		ret = -EINVAL;
	}

	v4l2_subdev_unlock_state(state);

	return ret;
}

static void mxc_isi_video_return_buffers(struct mxc_isi_video *video,
					 enum vb2_buffer_state state)
{
	struct mxc_isi_buffer *buf;

	spin_lock_irq(&video->buf_lock);

	while (!list_empty(&video->out_active)) {
		buf = list_first_entry(&video->out_active,
				       struct mxc_isi_buffer, list);
		list_del_init(&buf->list);
		if (buf->discard)
			continue;

		vb2_buffer_done(&buf->v4l2_buf.vb2_buf, state);
	}

	while (!list_empty(&video->out_pending)) {
		buf = list_first_entry(&video->out_pending,
				       struct mxc_isi_buffer, list);
		list_del_init(&buf->list);
		vb2_buffer_done(&buf->v4l2_buf.vb2_buf, state);
	}

	while (!list_empty(&video->out_discard)) {
		buf = list_first_entry(&video->out_discard,
				       struct mxc_isi_buffer, list);
		list_del_init(&buf->list);
	}

	INIT_LIST_HEAD(&video->out_active);
	INIT_LIST_HEAD(&video->out_pending);
	INIT_LIST_HEAD(&video->out_discard);

	spin_unlock_irq(&video->buf_lock);
}

static void mxc_isi_video_queue_first_buffers(struct mxc_isi_video *video)
{
	unsigned int discard;
	unsigned int i;

	lockdep_assert_held(&video->buf_lock);

	/*
	 * Queue two ISI channel output buffers. We are not guaranteed to have
	 * any buffer in the pending list when this function is called from the
	 * system resume handler. Use pending buffers as much as possible, and
	 * use discard buffers to fill the remaining slots.
	 */

	/* How many discard buffers do we need to queue first ? */
	discard = list_empty(&video->out_pending) ? 2
		: list_is_singular(&video->out_pending) ? 1
		: 0;

	for (i = 0; i < 2; ++i) {
		enum mxc_isi_buf_id buf_id = i == 0 ? MXC_ISI_BUF1
					   : MXC_ISI_BUF2;
		struct mxc_isi_buffer *buf;
		struct list_head *list;

		list = i < discard ? &video->out_discard : &video->out_pending;
		buf = list_first_entry(list, struct mxc_isi_buffer, list);

		mxc_isi_channel_set_outbuf(video->pipe, buf->dma_addrs, buf_id);
		buf->id = buf_id;
		list_move_tail(&buf->list, &video->out_active);
	}
}

static inline struct mxc_isi_buffer *to_isi_buffer(struct vb2_v4l2_buffer *v4l2_buf)
{
	return container_of(v4l2_buf, struct mxc_isi_buffer, v4l2_buf);
}

int mxc_isi_video_queue_setup(const struct v4l2_pix_format_mplane *format,
			      const struct mxc_isi_format_info *info,
			      unsigned int *num_buffers,
			      unsigned int *num_planes, unsigned int sizes[])
{
	unsigned int i;

	if (*num_planes) {
		if (*num_planes != info->mem_planes)
			return -EINVAL;

		for (i = 0; i < info->mem_planes; ++i) {
			if (sizes[i] < format->plane_fmt[i].sizeimage)
				return -EINVAL;
		}

		return 0;
	}

	*num_planes = info->mem_planes;

	for (i = 0; i < info->mem_planes; ++i)
		sizes[i] = format->plane_fmt[i].sizeimage;

	return 0;
}

void mxc_isi_video_buffer_init(struct vb2_buffer *vb2, dma_addr_t dma_addrs[3],
			       const struct mxc_isi_format_info *info,
			       const struct v4l2_pix_format_mplane *pix)
{
	unsigned int i;

	for (i = 0; i < info->mem_planes; ++i)
		dma_addrs[i] = vb2_dma_contig_plane_dma_addr(vb2, i);

	/*
	 * For single-planar pixel formats with multiple color planes, split
	 * the buffer into color planes.
	 */
	if (info->color_planes != info->mem_planes) {
		unsigned int size = pix->plane_fmt[0].bytesperline * pix->height;

		for (i = 1; i < info->color_planes; ++i) {
			unsigned int vsub = i > 1 ? info->vsub : 1;

			dma_addrs[i] = dma_addrs[i - 1] + size / vsub;
		}
	}
}

int mxc_isi_video_buffer_prepare(struct mxc_isi_dev *isi, struct vb2_buffer *vb2,
				 const struct mxc_isi_format_info *info,
				 const struct v4l2_pix_format_mplane *pix)
{
	unsigned int i;

	for (i = 0; i < info->mem_planes; i++) {
		unsigned long size = pix->plane_fmt[i].sizeimage;

		if (vb2_plane_size(vb2, i) < size) {
			dev_err(isi->dev, "User buffer too small (%ld < %ld)\n",
				vb2_plane_size(vb2, i), size);
			return -EINVAL;
		}

		vb2_set_plane_payload(vb2, i, size);
	}

	return 0;
}

static int mxc_isi_vb2_queue_setup(struct vb2_queue *q,
				   unsigned int *num_buffers,
				   unsigned int *num_planes,
				   unsigned int sizes[],
				   struct device *alloc_devs[])
{
	struct mxc_isi_video *video = vb2_get_drv_priv(q);

	return mxc_isi_video_queue_setup(&video->pix, video->fmtinfo,
					 num_buffers, num_planes, sizes);
}

static int mxc_isi_vb2_buffer_init(struct vb2_buffer *vb2)
{
	struct mxc_isi_buffer *buf = to_isi_buffer(to_vb2_v4l2_buffer(vb2));
	struct mxc_isi_video *video = vb2_get_drv_priv(vb2->vb2_queue);

	mxc_isi_video_buffer_init(vb2, buf->dma_addrs, video->fmtinfo,
				  &video->pix);

	return 0;
}

static int mxc_isi_vb2_buffer_prepare(struct vb2_buffer *vb2)
{
	struct mxc_isi_video *video = vb2_get_drv_priv(vb2->vb2_queue);

	return mxc_isi_video_buffer_prepare(video->pipe->isi, vb2,
					    video->fmtinfo, &video->pix);
}

static void mxc_isi_vb2_buffer_queue(struct vb2_buffer *vb2)
{
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb2);
	struct mxc_isi_buffer *buf = to_isi_buffer(v4l2_buf);
	struct mxc_isi_video *video = vb2_get_drv_priv(vb2->vb2_queue);

	spin_lock_irq(&video->buf_lock);
	list_add_tail(&buf->list, &video->out_pending);
	spin_unlock_irq(&video->buf_lock);
}

static void mxc_isi_video_init_channel(struct mxc_isi_video *video)
{
	struct mxc_isi_pipe *pipe = video->pipe;

	mxc_isi_channel_get(pipe);

	mutex_lock(video->ctrls.handler.lock);
	mxc_isi_channel_set_alpha(pipe, video->ctrls.alpha);
	mxc_isi_channel_set_flip(pipe, video->ctrls.hflip, video->ctrls.vflip);
	mutex_unlock(video->ctrls.handler.lock);

	mxc_isi_channel_set_output_format(pipe, video->fmtinfo, &video->pix);
}

static int mxc_isi_vb2_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct mxc_isi_video *video = vb2_get_drv_priv(q);
	unsigned int i;
	int ret;

	/* Initialize the ISI channel. */
	mxc_isi_video_init_channel(video);

	spin_lock_irq(&video->buf_lock);

	/* Add the discard buffers to the out_discard list. */
	for (i = 0; i < ARRAY_SIZE(video->buf_discard); ++i) {
		struct mxc_isi_buffer *buf = &video->buf_discard[i];

		list_add_tail(&buf->list, &video->out_discard);
	}

	/* Queue the first buffers. */
	mxc_isi_video_queue_first_buffers(video);

	/* Clear frame count */
	video->frame_count = 0;

	spin_unlock_irq(&video->buf_lock);

	ret = mxc_isi_pipe_enable(video->pipe);
	if (ret)
		goto error;

	return 0;

error:
	mxc_isi_channel_put(video->pipe);
	mxc_isi_video_return_buffers(video, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void mxc_isi_vb2_stop_streaming(struct vb2_queue *q)
{
	struct mxc_isi_video *video = vb2_get_drv_priv(q);

	mxc_isi_pipe_disable(video->pipe);
	mxc_isi_channel_put(video->pipe);

	mxc_isi_video_return_buffers(video, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops mxc_isi_vb2_qops = {
	.queue_setup		= mxc_isi_vb2_queue_setup,
	.buf_init		= mxc_isi_vb2_buffer_init,
	.buf_prepare		= mxc_isi_vb2_buffer_prepare,
	.buf_queue		= mxc_isi_vb2_buffer_queue,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.start_streaming	= mxc_isi_vb2_start_streaming,
	.stop_streaming		= mxc_isi_vb2_stop_streaming,
};

/* -----------------------------------------------------------------------------
 * V4L2 controls
 */

static inline struct mxc_isi_video *ctrl_to_isi_video(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct mxc_isi_video, ctrls.handler);
}

static int mxc_isi_video_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mxc_isi_video *video = ctrl_to_isi_video(ctrl);

	switch (ctrl->id) {
	case V4L2_CID_ALPHA_COMPONENT:
		video->ctrls.alpha = ctrl->val;
		break;
	case V4L2_CID_VFLIP:
		video->ctrls.vflip = ctrl->val;
		break;
	case V4L2_CID_HFLIP:
		video->ctrls.hflip = ctrl->val;
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops mxc_isi_video_ctrl_ops = {
	.s_ctrl = mxc_isi_video_s_ctrl,
};

static int mxc_isi_video_ctrls_create(struct mxc_isi_video *video)
{
	struct v4l2_ctrl_handler *handler = &video->ctrls.handler;
	int ret;

	v4l2_ctrl_handler_init(handler, 3);

	v4l2_ctrl_new_std(handler, &mxc_isi_video_ctrl_ops,
			  V4L2_CID_ALPHA_COMPONENT, 0, 255, 1, 0);

	v4l2_ctrl_new_std(handler, &mxc_isi_video_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(handler, &mxc_isi_video_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		v4l2_ctrl_handler_free(handler);
		return ret;
	}

	video->vdev.ctrl_handler = handler;

	return 0;
}

static void mxc_isi_video_ctrls_delete(struct mxc_isi_video *video)
{
	v4l2_ctrl_handler_free(&video->ctrls.handler);
}

/* -----------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int mxc_isi_video_querycap(struct file *file, void *priv,
				  struct v4l2_capability *cap)
{
	strscpy(cap->driver, MXC_ISI_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, MXC_ISI_CAPTURE, sizeof(cap->card));

	return 0;
}

static int mxc_isi_video_enum_fmt(struct file *file, void *priv,
				  struct v4l2_fmtdesc *f)
{
	const struct mxc_isi_format_info *fmt;
	unsigned int index = f->index;
	unsigned int i;

	if (f->mbus_code) {
		/*
		 * If a media bus code is specified, only enumerate formats
		 * compatible with it.
		 */
		for (i = 0; i < ARRAY_SIZE(mxc_isi_formats); i++) {
			fmt = &mxc_isi_formats[i];
			if (fmt->mbus_code != f->mbus_code)
				continue;

			if (index == 0)
				break;

			index--;
		}

		if (i == ARRAY_SIZE(mxc_isi_formats))
			return -EINVAL;
	} else {
		/* Otherwise, enumerate all formatS. */
		if (f->index >= ARRAY_SIZE(mxc_isi_formats))
			return -EINVAL;

		fmt = &mxc_isi_formats[f->index];
	}

	f->pixelformat = fmt->fourcc;
	f->flags |= V4L2_FMT_FLAG_CSC_COLORSPACE | V4L2_FMT_FLAG_CSC_YCBCR_ENC
		 |  V4L2_FMT_FLAG_CSC_QUANTIZATION | V4L2_FMT_FLAG_CSC_XFER_FUNC;

	return 0;
}

static int mxc_isi_video_g_fmt(struct file *file, void *fh,
			       struct v4l2_format *f)
{
	struct mxc_isi_video *video = video_drvdata(file);

	f->fmt.pix_mp = video->pix;

	return 0;
}

static int mxc_isi_video_try_fmt(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct mxc_isi_video *video = video_drvdata(file);

	mxc_isi_format_try(video->pipe, &f->fmt.pix_mp, MXC_ISI_VIDEO_CAP);
	return 0;
}

static int mxc_isi_video_s_fmt(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct mxc_isi_video *video = video_drvdata(file);
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;

	if (vb2_is_busy(&video->vb2_q))
		return -EBUSY;

	video->fmtinfo = mxc_isi_format_try(video->pipe, pix, MXC_ISI_VIDEO_CAP);
	video->pix = *pix;

	return 0;
}

static int mxc_isi_video_streamon(struct file *file, void *priv,
				  enum v4l2_buf_type type)
{
	struct mxc_isi_video *video = video_drvdata(file);
	struct media_device *mdev = &video->pipe->isi->media_dev;
	struct media_pipeline *pipe;
	int ret;

	if (vb2_queue_is_busy(&video->vb2_q, file))
		return -EBUSY;

	/*
	 * Get a pipeline for the video node and start it. This must be done
	 * here and not in the queue .start_streaming() handler, so that
	 * pipeline start errors can be reported from VIDIOC_STREAMON and not
	 * delayed until subsequent VIDIOC_QBUF calls.
	 */
	mutex_lock(&mdev->graph_mutex);

	ret = mxc_isi_pipe_acquire(video->pipe, &mxc_isi_video_frame_write_done);
	if (ret) {
		mutex_unlock(&mdev->graph_mutex);
		return ret;
	}

	pipe = media_entity_pipeline(&video->vdev.entity) ? : &video->pipe->pipe;

	ret = __video_device_pipeline_start(&video->vdev, pipe);
	if (ret) {
		mutex_unlock(&mdev->graph_mutex);
		goto err_release;
	}

	mutex_unlock(&mdev->graph_mutex);

	/* Verify that the video format matches the output of the subdev. */
	ret = mxc_isi_video_validate_format(video);
	if (ret)
		goto err_stop;

	/* Allocate buffers for discard operation. */
	ret = mxc_isi_video_alloc_discard_buffers(video);
	if (ret)
		goto err_stop;

	ret = vb2_streamon(&video->vb2_q, type);
	if (ret)
		goto err_free;

	video->is_streaming = true;

	return 0;

err_free:
	mxc_isi_video_free_discard_buffers(video);
err_stop:
	video_device_pipeline_stop(&video->vdev);
err_release:
	mxc_isi_pipe_release(video->pipe);
	return ret;
}

static void mxc_isi_video_cleanup_streaming(struct mxc_isi_video *video)
{
	lockdep_assert_held(&video->lock);

	if (!video->is_streaming)
		return;

	mxc_isi_video_free_discard_buffers(video);
	video_device_pipeline_stop(&video->vdev);
	mxc_isi_pipe_release(video->pipe);

	video->is_streaming = false;
}

static int mxc_isi_video_streamoff(struct file *file, void *priv,
				   enum v4l2_buf_type type)
{
	struct mxc_isi_video *video = video_drvdata(file);
	int ret;

	ret = vb2_ioctl_streamoff(file, priv, type);
	if (ret)
		return ret;

	mxc_isi_video_cleanup_streaming(video);

	return 0;
}

static int mxc_isi_video_enum_framesizes(struct file *file, void *priv,
					 struct v4l2_frmsizeenum *fsize)
{
	struct mxc_isi_video *video = video_drvdata(file);
	const struct mxc_isi_format_info *info;
	unsigned int max_width;
	unsigned int h_align;
	unsigned int v_align;

	if (fsize->index)
		return -EINVAL;

	info = mxc_isi_format_by_fourcc(fsize->pixel_format, MXC_ISI_VIDEO_CAP);
	if (!info)
		return -EINVAL;

	h_align = max_t(unsigned int, info->hsub, 1);
	v_align = max_t(unsigned int, info->vsub, 1);

	max_width = video->pipe->id == video->pipe->isi->pdata->num_channels - 1
		  ? MXC_ISI_MAX_WIDTH_UNCHAINED
		  : MXC_ISI_MAX_WIDTH_CHAINED;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = ALIGN(MXC_ISI_MIN_WIDTH, h_align);
	fsize->stepwise.min_height = ALIGN(MXC_ISI_MIN_HEIGHT, v_align);
	fsize->stepwise.max_width = ALIGN_DOWN(max_width, h_align);
	fsize->stepwise.max_height = ALIGN_DOWN(MXC_ISI_MAX_HEIGHT, v_align);
	fsize->stepwise.step_width = h_align;
	fsize->stepwise.step_height = v_align;

	/*
	 * The width can be further restricted due to line buffer sharing
	 * between pipelines when scaling, but we have no way to know here if
	 * the scaler will be used.
	 */

	return 0;
}

static const struct v4l2_ioctl_ops mxc_isi_video_ioctl_ops = {
	.vidioc_querycap		= mxc_isi_video_querycap,

	.vidioc_enum_fmt_vid_cap	= mxc_isi_video_enum_fmt,
	.vidioc_try_fmt_vid_cap_mplane	= mxc_isi_video_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane	= mxc_isi_video_s_fmt,
	.vidioc_g_fmt_vid_cap_mplane	= mxc_isi_video_g_fmt,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,

	.vidioc_streamon		= mxc_isi_video_streamon,
	.vidioc_streamoff		= mxc_isi_video_streamoff,

	.vidioc_enum_framesizes		= mxc_isi_video_enum_framesizes,

	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/* -----------------------------------------------------------------------------
 * Video device file operations
 */

static int mxc_isi_video_open(struct file *file)
{
	struct mxc_isi_video *video = video_drvdata(file);
	int ret;

	ret = v4l2_fh_open(file);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(video->pipe->isi->dev);
	if (ret) {
		v4l2_fh_release(file);
		return ret;
	}

	return 0;
}

static int mxc_isi_video_release(struct file *file)
{
	struct mxc_isi_video *video = video_drvdata(file);
	int ret;

	ret = vb2_fop_release(file);
	if (ret)
		dev_err(video->pipe->isi->dev, "%s fail\n", __func__);

	mutex_lock(&video->lock);
	mxc_isi_video_cleanup_streaming(video);
	mutex_unlock(&video->lock);

	pm_runtime_put(video->pipe->isi->dev);
	return ret;
}

static const struct v4l2_file_operations mxc_isi_video_fops = {
	.owner		= THIS_MODULE,
	.open		= mxc_isi_video_open,
	.release	= mxc_isi_video_release,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
};

/* -----------------------------------------------------------------------------
 * Suspend & resume
 */

void mxc_isi_video_suspend(struct mxc_isi_pipe *pipe)
{
	struct mxc_isi_video *video = &pipe->video;

	if (!video->is_streaming)
		return;

	mxc_isi_pipe_disable(pipe);
	mxc_isi_channel_put(pipe);

	spin_lock_irq(&video->buf_lock);

	/*
	 * Move the active buffers back to the pending or discard list. We must
	 * iterate the active list backward and move the buffers to the head of
	 * the pending list to preserve the buffer queueing order.
	 */
	while (!list_empty(&video->out_active)) {
		struct mxc_isi_buffer *buf =
			list_last_entry(&video->out_active,
					struct mxc_isi_buffer, list);

		if (buf->discard)
			list_move(&buf->list, &video->out_discard);
		else
			list_move(&buf->list, &video->out_pending);
	}

	spin_unlock_irq(&video->buf_lock);
}

int mxc_isi_video_resume(struct mxc_isi_pipe *pipe)
{
	struct mxc_isi_video *video = &pipe->video;

	if (!video->is_streaming)
		return 0;

	mxc_isi_video_init_channel(video);

	spin_lock_irq(&video->buf_lock);
	mxc_isi_video_queue_first_buffers(video);
	spin_unlock_irq(&video->buf_lock);

	return mxc_isi_pipe_enable(pipe);
}

/* -----------------------------------------------------------------------------
 * Registration
 */

int mxc_isi_video_register(struct mxc_isi_pipe *pipe,
			   struct v4l2_device *v4l2_dev)
{
	struct mxc_isi_video *video = &pipe->video;
	struct v4l2_pix_format_mplane *pix = &video->pix;
	struct video_device *vdev = &video->vdev;
	struct vb2_queue *q = &video->vb2_q;
	int ret = -ENOMEM;

	video->pipe = pipe;

	mutex_init(&video->lock);
	spin_lock_init(&video->buf_lock);

	pix->width = MXC_ISI_DEF_WIDTH;
	pix->height = MXC_ISI_DEF_HEIGHT;
	pix->pixelformat = MXC_ISI_DEF_PIXEL_FORMAT;
	pix->colorspace = MXC_ISI_DEF_COLOR_SPACE;
	pix->ycbcr_enc = MXC_ISI_DEF_YCBCR_ENC;
	pix->quantization = MXC_ISI_DEF_QUANTIZATION;
	pix->xfer_func = MXC_ISI_DEF_XFER_FUNC;
	video->fmtinfo = mxc_isi_format_try(video->pipe, pix, MXC_ISI_VIDEO_CAP);

	memset(vdev, 0, sizeof(*vdev));
	snprintf(vdev->name, sizeof(vdev->name), "mxc_isi.%d.capture", pipe->id);

	vdev->fops	= &mxc_isi_video_fops;
	vdev->ioctl_ops	= &mxc_isi_video_ioctl_ops;
	vdev->v4l2_dev	= v4l2_dev;
	vdev->minor	= -1;
	vdev->release	= video_device_release_empty;
	vdev->queue	= q;
	vdev->lock	= &video->lock;

	vdev->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE_MPLANE
			  | V4L2_CAP_IO_MC;
	video_set_drvdata(vdev, video);

	INIT_LIST_HEAD(&video->out_pending);
	INIT_LIST_HEAD(&video->out_active);
	INIT_LIST_HEAD(&video->out_discard);

	memset(q, 0, sizeof(*q));
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = video;
	q->ops = &mxc_isi_vb2_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct mxc_isi_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed = 2;
	q->lock = &video->lock;
	q->dev = pipe->isi->dev;

	ret = vb2_queue_init(q);
	if (ret)
		goto err_free_ctx;

	video->pad.flags = MEDIA_PAD_FL_SINK;
	vdev->entity.function = MEDIA_ENT_F_PROC_VIDEO_SCALER;
	ret = media_entity_pads_init(&vdev->entity, 1, &video->pad);
	if (ret)
		goto err_free_ctx;

	ret = mxc_isi_video_ctrls_create(video);
	if (ret)
		goto err_me_cleanup;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_ctrl_free;

	ret = media_create_pad_link(&pipe->sd.entity,
				    MXC_ISI_PIPE_PAD_SOURCE,
				    &vdev->entity, 0,
				    MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);
	if (ret)
		goto err_video_unreg;

	return 0;

err_video_unreg:
	video_unregister_device(vdev);
err_ctrl_free:
	mxc_isi_video_ctrls_delete(video);
err_me_cleanup:
	media_entity_cleanup(&vdev->entity);
err_free_ctx:
	return ret;
}

void mxc_isi_video_unregister(struct mxc_isi_pipe *pipe)
{
	struct mxc_isi_video *video = &pipe->video;
	struct video_device *vdev = &video->vdev;

	mutex_lock(&video->lock);

	if (video_is_registered(vdev)) {
		video_unregister_device(vdev);
		mxc_isi_video_ctrls_delete(video);
		media_entity_cleanup(&vdev->entity);
	}

	mutex_unlock(&video->lock);
}
