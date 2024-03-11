// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
 * Author: Yong Deng <yong.deng@magewell.com>
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <linux/of.h>
#include <linux/regmap.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>

#include "sun6i_csi.h"
#include "sun6i_csi_bridge.h"
#include "sun6i_csi_capture.h"
#include "sun6i_csi_reg.h"

/* Helpers */

void sun6i_csi_capture_dimensions(struct sun6i_csi_device *csi_dev,
				  unsigned int *width, unsigned int *height)
{
	if (width)
		*width = csi_dev->capture.format.fmt.pix.width;
	if (height)
		*height = csi_dev->capture.format.fmt.pix.height;
}

void sun6i_csi_capture_format(struct sun6i_csi_device *csi_dev,
			      u32 *pixelformat, u32 *field)
{
	if (pixelformat)
		*pixelformat = csi_dev->capture.format.fmt.pix.pixelformat;

	if (field)
		*field = csi_dev->capture.format.fmt.pix.field;
}

/* Format */

static const struct sun6i_csi_capture_format sun6i_csi_capture_formats[] = {
	/* Bayer */
	{
		.pixelformat		= V4L2_PIX_FMT_SBGGR8,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SGBRG8,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SGRBG8,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SRGGB8,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SBGGR10,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_10,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_10,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SGBRG10,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_10,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_10,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SGRBG10,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_10,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_10,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SRGGB10,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_10,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_10,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SBGGR12,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_12,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_12,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SGBRG12,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_12,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_12,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SGRBG12,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_12,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_12,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SRGGB12,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_12,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_12,
	},
	/* RGB */
	{
		.pixelformat		= V4L2_PIX_FMT_RGB565,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RGB565,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RGB565,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_RGB565X,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RGB565,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RGB565,
	},
	/* YUV422 */
	{
		.pixelformat		= V4L2_PIX_FMT_YUYV,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
		.input_format_raw	= true,
		.hsize_len_factor	= 2,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_YVYU,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
		.input_format_raw	= true,
		.hsize_len_factor	= 2,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_UYVY,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
		.input_format_raw	= true,
		.hsize_len_factor	= 2,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_VYUY,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
		.input_format_raw	= true,
		.hsize_len_factor	= 2,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_NV16,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV422SP,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV422SP,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_NV61,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV422SP,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV422SP,
		.input_yuv_seq_invert	= true,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_YUV422P,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV422P,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV422P,
	},
	/* YUV420 */
	{
		.pixelformat		= V4L2_PIX_FMT_NV12_16L16,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV420MB,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV420MB,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_NV12,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV420SP,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV420SP,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_NV21,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV420SP,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV420SP,
		.input_yuv_seq_invert	= true,
	},

	{
		.pixelformat		= V4L2_PIX_FMT_YUV420,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV420P,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV420P,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_YVU420,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV420P,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV420P,
		.input_yuv_seq_invert	= true,
	},
	/* Compressed */
	{
		.pixelformat		= V4L2_PIX_FMT_JPEG,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
	},
};

const
struct sun6i_csi_capture_format *sun6i_csi_capture_format_find(u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sun6i_csi_capture_formats); i++)
		if (sun6i_csi_capture_formats[i].pixelformat == pixelformat)
			return &sun6i_csi_capture_formats[i];

	return NULL;
}

/* RAW formats need an exact match between pixel and mbus formats. */
static const
struct sun6i_csi_capture_format_match sun6i_csi_capture_format_matches[] = {
	/* YUV420 */
	{
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_1X16,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_YVYU,
		.mbus_code	= MEDIA_BUS_FMT_YVYU8_2X8,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_YVYU,
		.mbus_code	= MEDIA_BUS_FMT_YVYU8_1X16,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_2X8,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_1X16,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_VYUY,
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_2X8,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_VYUY,
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_1X16,
	},
	/* RGB */
	{
		.pixelformat	= V4L2_PIX_FMT_RGB565,
		.mbus_code	= MEDIA_BUS_FMT_RGB565_2X8_LE,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_RGB565X,
		.mbus_code	= MEDIA_BUS_FMT_RGB565_2X8_BE,
	},
	/* Bayer */
	{
		.pixelformat	= V4L2_PIX_FMT_SBGGR8,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_SGBRG8,
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_SGRBG8,
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_SRGGB8,
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_SBGGR10,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_SGBRG10,
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_SGRBG10,
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_SRGGB10,
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_SBGGR12,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR12_1X12,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_SGBRG12,
		.mbus_code	= MEDIA_BUS_FMT_SGBRG12_1X12,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_SGRBG12,
		.mbus_code	= MEDIA_BUS_FMT_SGRBG12_1X12,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_SRGGB12,
		.mbus_code	= MEDIA_BUS_FMT_SRGGB12_1X12,
	},
	/* Compressed */
	{
		.pixelformat	= V4L2_PIX_FMT_JPEG,
		.mbus_code	= MEDIA_BUS_FMT_JPEG_1X8,
	},
};

static bool sun6i_csi_capture_format_match(u32 pixelformat, u32 mbus_code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sun6i_csi_capture_format_matches); i++) {
		const struct sun6i_csi_capture_format_match *match =
			&sun6i_csi_capture_format_matches[i];

		if (match->pixelformat == pixelformat &&
		    match->mbus_code == mbus_code)
			return true;
	}

	return false;
}

/* Capture */

static void
sun6i_csi_capture_buffer_configure(struct sun6i_csi_device *csi_dev,
				   struct sun6i_csi_buffer *csi_buffer)
{
	struct regmap *regmap = csi_dev->regmap;
	const struct v4l2_format_info *info;
	struct vb2_buffer *vb2_buffer;
	unsigned int width, height;
	dma_addr_t address;
	u32 pixelformat;

	vb2_buffer = &csi_buffer->v4l2_buffer.vb2_buf;
	address = vb2_dma_contig_plane_dma_addr(vb2_buffer, 0);

	regmap_write(regmap, SUN6I_CSI_CH_FIFO0_ADDR_REG,
		     SUN6I_CSI_ADDR_VALUE(address));

	sun6i_csi_capture_dimensions(csi_dev, &width, &height);
	sun6i_csi_capture_format(csi_dev, &pixelformat, NULL);

	info = v4l2_format_info(pixelformat);
	/* Unsupported formats are single-plane, so we can stop here. */
	if (!info)
		return;

	if (info->comp_planes > 1) {
		address += info->bpp[0] * width * height;

		regmap_write(regmap, SUN6I_CSI_CH_FIFO1_ADDR_REG,
			     SUN6I_CSI_ADDR_VALUE(address));
	}

	if (info->comp_planes > 2) {
		address += info->bpp[1] * DIV_ROUND_UP(width, info->hdiv) *
			   DIV_ROUND_UP(height, info->vdiv);

		regmap_write(regmap, SUN6I_CSI_CH_FIFO2_ADDR_REG,
			     SUN6I_CSI_ADDR_VALUE(address));
	}
}

void sun6i_csi_capture_configure(struct sun6i_csi_device *csi_dev)
{
	struct regmap *regmap = csi_dev->regmap;
	const struct sun6i_csi_capture_format *format;
	const struct v4l2_format_info *info;
	u32 hsize_len, vsize_len;
	u32 luma_line, chroma_line = 0;
	u32 pixelformat, field;
	u32 width, height;

	sun6i_csi_capture_dimensions(csi_dev, &width, &height);
	sun6i_csi_capture_format(csi_dev, &pixelformat, &field);

	format = sun6i_csi_capture_format_find(pixelformat);
	if (WARN_ON(!format))
		return;

	hsize_len = width;
	vsize_len = height;

	/*
	 * When using 8-bit raw input/output (for packed YUV), we need to adapt
	 * the width to account for the difference in bpp when it's not 8-bit.
	 */
	if (format->hsize_len_factor)
		hsize_len *= format->hsize_len_factor;

	regmap_write(regmap, SUN6I_CSI_CH_HSIZE_REG,
		     SUN6I_CSI_CH_HSIZE_LEN(hsize_len) |
		     SUN6I_CSI_CH_HSIZE_START(0));

	regmap_write(regmap, SUN6I_CSI_CH_VSIZE_REG,
		     SUN6I_CSI_CH_VSIZE_LEN(vsize_len) |
		     SUN6I_CSI_CH_VSIZE_START(0));

	switch (pixelformat) {
	case V4L2_PIX_FMT_RGB565X:
		luma_line = width * 2;
		break;
	case V4L2_PIX_FMT_NV12_16L16:
		luma_line = width;
		chroma_line = width;
		break;
	case V4L2_PIX_FMT_JPEG:
		luma_line = width;
		break;
	default:
		info = v4l2_format_info(pixelformat);
		if (WARN_ON(!info))
			return;

		luma_line = width * info->bpp[0];

		if (info->comp_planes > 1)
			chroma_line = width * info->bpp[1] / info->hdiv;
		break;
	}

	regmap_write(regmap, SUN6I_CSI_CH_BUF_LEN_REG,
		     SUN6I_CSI_CH_BUF_LEN_CHROMA_LINE(chroma_line) |
		     SUN6I_CSI_CH_BUF_LEN_LUMA_LINE(luma_line));
}

/* State */

static void sun6i_csi_capture_state_cleanup(struct sun6i_csi_device *csi_dev,
					    bool error)
{
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	struct sun6i_csi_buffer **csi_buffer_states[] = {
		&state->pending, &state->current, &state->complete,
	};
	struct sun6i_csi_buffer *csi_buffer;
	struct vb2_buffer *vb2_buffer;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&state->lock, flags);

	for (i = 0; i < ARRAY_SIZE(csi_buffer_states); i++) {
		csi_buffer = *csi_buffer_states[i];
		if (!csi_buffer)
			continue;

		vb2_buffer = &csi_buffer->v4l2_buffer.vb2_buf;
		vb2_buffer_done(vb2_buffer, error ? VB2_BUF_STATE_ERROR :
				VB2_BUF_STATE_QUEUED);

		*csi_buffer_states[i] = NULL;
	}

	list_for_each_entry(csi_buffer, &state->queue, list) {
		vb2_buffer = &csi_buffer->v4l2_buffer.vb2_buf;
		vb2_buffer_done(vb2_buffer, error ? VB2_BUF_STATE_ERROR :
				VB2_BUF_STATE_QUEUED);
	}

	INIT_LIST_HEAD(&state->queue);

	spin_unlock_irqrestore(&state->lock, flags);
}

void sun6i_csi_capture_state_update(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	struct sun6i_csi_buffer *csi_buffer;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);

	if (list_empty(&state->queue))
		goto complete;

	if (state->pending)
		goto complete;

	csi_buffer = list_first_entry(&state->queue, struct sun6i_csi_buffer,
				      list);

	sun6i_csi_capture_buffer_configure(csi_dev, csi_buffer);

	list_del(&csi_buffer->list);

	state->pending = csi_buffer;

complete:
	spin_unlock_irqrestore(&state->lock, flags);
}

static void sun6i_csi_capture_state_complete(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);

	if (!state->pending)
		goto complete;

	state->complete = state->current;
	state->current = state->pending;
	state->pending = NULL;

	if (state->complete) {
		struct sun6i_csi_buffer *csi_buffer = state->complete;
		struct vb2_buffer *vb2_buffer =
			&csi_buffer->v4l2_buffer.vb2_buf;

		vb2_buffer->timestamp = ktime_get_ns();
		csi_buffer->v4l2_buffer.sequence = state->sequence;

		vb2_buffer_done(vb2_buffer, VB2_BUF_STATE_DONE);

		state->complete = NULL;
	}

complete:
	spin_unlock_irqrestore(&state->lock, flags);
}

void sun6i_csi_capture_frame_done(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);
	state->sequence++;
	spin_unlock_irqrestore(&state->lock, flags);
}

void sun6i_csi_capture_sync(struct sun6i_csi_device *csi_dev)
{
	sun6i_csi_capture_state_complete(csi_dev);
	sun6i_csi_capture_state_update(csi_dev);
}

/* Queue */

static int sun6i_csi_capture_queue_setup(struct vb2_queue *queue,
					 unsigned int *buffers_count,
					 unsigned int *planes_count,
					 unsigned int sizes[],
					 struct device *alloc_devs[])
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(queue);
	unsigned int size = csi_dev->capture.format.fmt.pix.sizeimage;

	if (*planes_count)
		return sizes[0] < size ? -EINVAL : 0;

	*planes_count = 1;
	sizes[0] = size;

	return 0;
}

static int sun6i_csi_capture_buffer_prepare(struct vb2_buffer *buffer)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(buffer->vb2_queue);
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct v4l2_device *v4l2_dev = csi_dev->v4l2_dev;
	struct vb2_v4l2_buffer *v4l2_buffer = to_vb2_v4l2_buffer(buffer);
	unsigned long size = capture->format.fmt.pix.sizeimage;

	if (vb2_plane_size(buffer, 0) < size) {
		v4l2_err(v4l2_dev, "buffer too small (%lu < %lu)\n",
			 vb2_plane_size(buffer, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(buffer, 0, size);

	v4l2_buffer->field = capture->format.fmt.pix.field;

	return 0;
}

static void sun6i_csi_capture_buffer_queue(struct vb2_buffer *buffer)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(buffer->vb2_queue);
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	struct vb2_v4l2_buffer *v4l2_buffer = to_vb2_v4l2_buffer(buffer);
	struct sun6i_csi_buffer *csi_buffer =
		container_of(v4l2_buffer, struct sun6i_csi_buffer, v4l2_buffer);
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);
	list_add_tail(&csi_buffer->list, &state->queue);
	spin_unlock_irqrestore(&state->lock, flags);
}

static int sun6i_csi_capture_start_streaming(struct vb2_queue *queue,
					     unsigned int count)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(queue);
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	struct video_device *video_dev = &csi_dev->capture.video_dev;
	struct v4l2_subdev *subdev = &csi_dev->bridge.subdev;
	int ret;

	state->sequence = 0;

	ret = video_device_pipeline_alloc_start(video_dev);
	if (ret < 0)
		goto error_state;

	state->streaming = true;

	ret = v4l2_subdev_call(subdev, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD)
		goto error_streaming;

	return 0;

error_streaming:
	state->streaming = false;

	video_device_pipeline_stop(video_dev);

error_state:
	sun6i_csi_capture_state_cleanup(csi_dev, false);

	return ret;
}

static void sun6i_csi_capture_stop_streaming(struct vb2_queue *queue)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(queue);
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	struct video_device *video_dev = &csi_dev->capture.video_dev;
	struct v4l2_subdev *subdev = &csi_dev->bridge.subdev;

	v4l2_subdev_call(subdev, video, s_stream, 0);

	state->streaming = false;

	video_device_pipeline_stop(video_dev);

	sun6i_csi_capture_state_cleanup(csi_dev, true);
}

static const struct vb2_ops sun6i_csi_capture_queue_ops = {
	.queue_setup		= sun6i_csi_capture_queue_setup,
	.buf_prepare		= sun6i_csi_capture_buffer_prepare,
	.buf_queue		= sun6i_csi_capture_buffer_queue,
	.start_streaming	= sun6i_csi_capture_start_streaming,
	.stop_streaming		= sun6i_csi_capture_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

/* V4L2 Device */

static void sun6i_csi_capture_format_prepare(struct v4l2_format *format)
{
	struct v4l2_pix_format *pix_format = &format->fmt.pix;
	const struct v4l2_format_info *info;
	unsigned int width, height;

	v4l_bound_align_image(&pix_format->width, SUN6I_CSI_CAPTURE_WIDTH_MIN,
			      SUN6I_CSI_CAPTURE_WIDTH_MAX, 1,
			      &pix_format->height, SUN6I_CSI_CAPTURE_HEIGHT_MIN,
			      SUN6I_CSI_CAPTURE_HEIGHT_MAX, 1, 0);

	if (!sun6i_csi_capture_format_find(pix_format->pixelformat))
		pix_format->pixelformat =
			sun6i_csi_capture_formats[0].pixelformat;

	width = pix_format->width;
	height = pix_format->height;

	info = v4l2_format_info(pix_format->pixelformat);

	switch (pix_format->pixelformat) {
	case V4L2_PIX_FMT_NV12_16L16:
		pix_format->bytesperline = width * 12 / 8;
		pix_format->sizeimage = pix_format->bytesperline * height;
		break;
	case V4L2_PIX_FMT_JPEG:
		pix_format->bytesperline = width;
		pix_format->sizeimage = pix_format->bytesperline * height;
		break;
	default:
		v4l2_fill_pixfmt(pix_format, pix_format->pixelformat,
				 width, height);
		break;
	}

	if (pix_format->field == V4L2_FIELD_ANY)
		pix_format->field = V4L2_FIELD_NONE;

	if (pix_format->pixelformat == V4L2_PIX_FMT_JPEG)
		pix_format->colorspace = V4L2_COLORSPACE_JPEG;
	else if (info && info->pixel_enc == V4L2_PIXEL_ENC_BAYER)
		pix_format->colorspace = V4L2_COLORSPACE_RAW;
	else
		pix_format->colorspace = V4L2_COLORSPACE_SRGB;

	pix_format->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pix_format->quantization = V4L2_QUANTIZATION_DEFAULT;
	pix_format->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static int sun6i_csi_capture_querycap(struct file *file, void *private,
				      struct v4l2_capability *capability)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct video_device *video_dev = &csi_dev->capture.video_dev;

	strscpy(capability->driver, SUN6I_CSI_NAME, sizeof(capability->driver));
	strscpy(capability->card, video_dev->name, sizeof(capability->card));
	snprintf(capability->bus_info, sizeof(capability->bus_info),
		 "platform:%s", dev_name(csi_dev->dev));

	return 0;
}

static int sun6i_csi_capture_enum_fmt(struct file *file, void *private,
				      struct v4l2_fmtdesc *fmtdesc)
{
	u32 index = fmtdesc->index;

	if (index >= ARRAY_SIZE(sun6i_csi_capture_formats))
		return -EINVAL;

	fmtdesc->pixelformat = sun6i_csi_capture_formats[index].pixelformat;

	return 0;
}

static int sun6i_csi_capture_g_fmt(struct file *file, void *private,
				   struct v4l2_format *format)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);

	*format = csi_dev->capture.format;

	return 0;
}

static int sun6i_csi_capture_s_fmt(struct file *file, void *private,
				   struct v4l2_format *format)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_csi_capture *capture = &csi_dev->capture;

	if (vb2_is_busy(&capture->queue))
		return -EBUSY;

	sun6i_csi_capture_format_prepare(format);

	csi_dev->capture.format = *format;

	return 0;
}

static int sun6i_csi_capture_try_fmt(struct file *file, void *private,
				     struct v4l2_format *format)
{
	sun6i_csi_capture_format_prepare(format);

	return 0;
}

static int sun6i_csi_capture_enum_input(struct file *file, void *private,
					struct v4l2_input *input)
{
	if (input->index != 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(input->name, "Camera", sizeof(input->name));

	return 0;
}

static int sun6i_csi_capture_g_input(struct file *file, void *private,
				     unsigned int *index)
{
	*index = 0;

	return 0;
}

static int sun6i_csi_capture_s_input(struct file *file, void *private,
				     unsigned int index)
{
	if (index != 0)
		return -EINVAL;

	return 0;
}

static const struct v4l2_ioctl_ops sun6i_csi_capture_ioctl_ops = {
	.vidioc_querycap		= sun6i_csi_capture_querycap,

	.vidioc_enum_fmt_vid_cap	= sun6i_csi_capture_enum_fmt,
	.vidioc_g_fmt_vid_cap		= sun6i_csi_capture_g_fmt,
	.vidioc_s_fmt_vid_cap		= sun6i_csi_capture_s_fmt,
	.vidioc_try_fmt_vid_cap		= sun6i_csi_capture_try_fmt,

	.vidioc_enum_input		= sun6i_csi_capture_enum_input,
	.vidioc_g_input			= sun6i_csi_capture_g_input,
	.vidioc_s_input			= sun6i_csi_capture_s_input,

	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* V4L2 File */

static int sun6i_csi_capture_open(struct file *file)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	int ret;

	if (mutex_lock_interruptible(&capture->lock))
		return -ERESTARTSYS;

	ret = v4l2_pipeline_pm_get(&capture->video_dev.entity);
	if (ret < 0)
		goto error_lock;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto error_pipeline;

	mutex_unlock(&capture->lock);

	return 0;

error_pipeline:
	v4l2_pipeline_pm_put(&capture->video_dev.entity);

error_lock:
	mutex_unlock(&capture->lock);

	return ret;
}

static int sun6i_csi_capture_close(struct file *file)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_csi_capture *capture = &csi_dev->capture;

	mutex_lock(&capture->lock);

	_vb2_fop_release(file, NULL);
	v4l2_pipeline_pm_put(&capture->video_dev.entity);

	mutex_unlock(&capture->lock);

	return 0;
}

static const struct v4l2_file_operations sun6i_csi_capture_fops = {
	.owner		= THIS_MODULE,
	.open		= sun6i_csi_capture_open,
	.release	= sun6i_csi_capture_close,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
	.poll		= vb2_fop_poll
};

/* Media Entity */

static int sun6i_csi_capture_link_validate(struct media_link *link)
{
	struct video_device *video_dev =
		media_entity_to_video_device(link->sink->entity);
	struct sun6i_csi_device *csi_dev = video_get_drvdata(video_dev);
	struct v4l2_device *v4l2_dev = csi_dev->v4l2_dev;
	const struct sun6i_csi_capture_format *capture_format;
	const struct sun6i_csi_bridge_format *bridge_format;
	unsigned int capture_width, capture_height;
	unsigned int bridge_width, bridge_height;
	const struct v4l2_format_info *format_info;
	u32 pixelformat, capture_field;
	u32 mbus_code, bridge_field;
	bool match;

	sun6i_csi_capture_dimensions(csi_dev, &capture_width, &capture_height);

	sun6i_csi_capture_format(csi_dev, &pixelformat, &capture_field);
	capture_format = sun6i_csi_capture_format_find(pixelformat);
	if (WARN_ON(!capture_format))
		return -EINVAL;

	sun6i_csi_bridge_dimensions(csi_dev, &bridge_width, &bridge_height);

	sun6i_csi_bridge_format(csi_dev, &mbus_code, &bridge_field);
	bridge_format = sun6i_csi_bridge_format_find(mbus_code);
	if (WARN_ON(!bridge_format))
		return -EINVAL;

	/* No cropping/scaling is supported. */
	if (capture_width != bridge_width || capture_height != bridge_height) {
		v4l2_err(v4l2_dev,
			 "invalid input/output dimensions: %ux%u/%ux%u\n",
			 bridge_width, bridge_height, capture_width,
			 capture_height);
		return -EINVAL;
	}

	format_info = v4l2_format_info(pixelformat);
	/* Some formats are not listed. */
	if (!format_info)
		return 0;

	if (format_info->pixel_enc == V4L2_PIXEL_ENC_BAYER &&
	    bridge_format->input_format != SUN6I_CSI_INPUT_FMT_RAW)
		goto invalid;

	if (format_info->pixel_enc == V4L2_PIXEL_ENC_RGB &&
	    bridge_format->input_format != SUN6I_CSI_INPUT_FMT_RAW)
		goto invalid;

	if (format_info->pixel_enc == V4L2_PIXEL_ENC_YUV) {
		if (bridge_format->input_format != SUN6I_CSI_INPUT_FMT_YUV420 &&
		    bridge_format->input_format != SUN6I_CSI_INPUT_FMT_YUV422)
			goto invalid;

		/* YUV420 input can't produce YUV422 output. */
		if (bridge_format->input_format == SUN6I_CSI_INPUT_FMT_YUV420 &&
		    format_info->vdiv == 1)
			goto invalid;
	}

	/* With raw input mode, we need a 1:1 match between input and output. */
	if (bridge_format->input_format == SUN6I_CSI_INPUT_FMT_RAW ||
	    capture_format->input_format_raw) {
		match = sun6i_csi_capture_format_match(pixelformat, mbus_code);
		if (!match)
			goto invalid;
	}

	return 0;

invalid:
	v4l2_err(v4l2_dev, "invalid input/output format combination\n");
	return -EINVAL;
}

static const struct media_entity_operations sun6i_csi_capture_media_ops = {
	.link_validate = sun6i_csi_capture_link_validate
};

/* Capture */

int sun6i_csi_capture_setup(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct sun6i_csi_capture_state *state = &capture->state;
	struct v4l2_device *v4l2_dev = csi_dev->v4l2_dev;
	struct v4l2_subdev *bridge_subdev = &csi_dev->bridge.subdev;
	struct video_device *video_dev = &capture->video_dev;
	struct vb2_queue *queue = &capture->queue;
	struct media_pad *pad = &capture->pad;
	struct v4l2_format *format = &csi_dev->capture.format;
	struct v4l2_pix_format *pix_format = &format->fmt.pix;
	int ret;

	/* This may happen with multiple bridge notifier bound calls. */
	if (state->setup)
		return 0;

	/* State */

	INIT_LIST_HEAD(&state->queue);
	spin_lock_init(&state->lock);

	/* Media Entity */

	video_dev->entity.ops = &sun6i_csi_capture_media_ops;

	/* Media Pad */

	pad->flags = MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;

	ret = media_entity_pads_init(&video_dev->entity, 1, pad);
	if (ret < 0)
		return ret;

	/* Queue */

	mutex_init(&capture->lock);

	queue->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	queue->io_modes = VB2_MMAP | VB2_DMABUF;
	queue->buf_struct_size = sizeof(struct sun6i_csi_buffer);
	queue->ops = &sun6i_csi_capture_queue_ops;
	queue->mem_ops = &vb2_dma_contig_memops;
	queue->min_queued_buffers = 2;
	queue->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	queue->lock = &capture->lock;
	queue->dev = csi_dev->dev;
	queue->drv_priv = csi_dev;

	ret = vb2_queue_init(queue);
	if (ret) {
		v4l2_err(v4l2_dev, "failed to initialize vb2 queue: %d\n", ret);
		goto error_media_entity;
	}

	/* V4L2 Format */

	format->type = queue->type;
	pix_format->pixelformat = sun6i_csi_capture_formats[0].pixelformat;
	pix_format->width = 1280;
	pix_format->height = 720;
	pix_format->field = V4L2_FIELD_NONE;

	sun6i_csi_capture_format_prepare(format);

	/* Video Device */

	strscpy(video_dev->name, SUN6I_CSI_CAPTURE_NAME,
		sizeof(video_dev->name));
	video_dev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	video_dev->vfl_dir = VFL_DIR_RX;
	video_dev->release = video_device_release_empty;
	video_dev->fops = &sun6i_csi_capture_fops;
	video_dev->ioctl_ops = &sun6i_csi_capture_ioctl_ops;
	video_dev->v4l2_dev = v4l2_dev;
	video_dev->queue = queue;
	video_dev->lock = &capture->lock;

	video_set_drvdata(video_dev, csi_dev);

	ret = video_register_device(video_dev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "failed to register video device: %d\n",
			 ret);
		goto error_media_entity;
	}

	/* Media Pad Link */

	ret = media_create_pad_link(&bridge_subdev->entity,
				    SUN6I_CSI_BRIDGE_PAD_SOURCE,
				    &video_dev->entity, 0,
				    csi_dev->isp_available ? 0 :
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "failed to create %s:%u -> %s:%u link\n",
			 bridge_subdev->entity.name,
			 SUN6I_CSI_BRIDGE_PAD_SOURCE,
			 video_dev->entity.name, 0);
		goto error_video_device;
	}

	state->setup = true;

	return 0;

error_video_device:
	vb2_video_unregister_device(video_dev);

error_media_entity:
	media_entity_cleanup(&video_dev->entity);

	mutex_destroy(&capture->lock);

	return ret;
}

void sun6i_csi_capture_cleanup(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct video_device *video_dev = &capture->video_dev;

	/* This may happen if async registration failed to complete. */
	if (!capture->state.setup)
		return;

	vb2_video_unregister_device(video_dev);
	media_entity_cleanup(&video_dev->entity);
	mutex_destroy(&capture->lock);

	capture->state.setup = false;
}
