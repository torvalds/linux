// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Mali-C55 ISP Driver - Video capture devices
 *
 * Copyright (C) 2025 Ideas on Board Oy
 */

#include <linux/cleanup.h>
#include <linux/minmax.h>
#include <linux/lockdep.h>
#include <linux/pm_runtime.h>
#include <linux/string.h>
#include <linux/videodev2.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "mali-c55-common.h"
#include "mali-c55-registers.h"

static const struct mali_c55_format_info mali_c55_fmts[] = {
	/*
	 * This table is missing some entries which need further work or
	 * investigation:
	 *
	 * Base mode 5 is "Generic Data"
	 * Base mode 8 is a backwards V4L2_PIX_FMT_XYUV32 - no V4L2 equivalent
	 * Base mode 9 seems to have no V4L2 equivalent
	 * Base mode 17, 19 and 20 describe formats which seem to have no V4L2
	 * equivalent
	 */
	{
		.fourcc = V4L2_PIX_FMT_XBGR32,
		.mbus_codes = {
			MEDIA_BUS_FMT_RGB121212_1X36,
			MEDIA_BUS_FMT_RGB202020_1X60,
		},
		.is_raw = false,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_RGB32,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT0
		}
	},
	{
		.fourcc = V4L2_PIX_FMT_ARGB2101010,
		.mbus_codes = {
			MEDIA_BUS_FMT_RGB121212_1X36,
			MEDIA_BUS_FMT_RGB202020_1X60,
		},
		.is_raw = false,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_A2R10G10B10,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT0
		}
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB565,
		.mbus_codes = {
			MEDIA_BUS_FMT_RGB121212_1X36,
			MEDIA_BUS_FMT_RGB202020_1X60,
		},
		.is_raw = false,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_RGB565,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT0
		}
	},
	{
		.fourcc = V4L2_PIX_FMT_BGR24,
		.mbus_codes = {
			MEDIA_BUS_FMT_RGB121212_1X36,
			MEDIA_BUS_FMT_RGB202020_1X60,
		},
		.is_raw = false,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_RGB24,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT0
		}
	},
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.mbus_codes = {
			MEDIA_BUS_FMT_YUV10_1X30,
		},
		.is_raw = false,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_YUY2,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT0
		}
	},
	{
		.fourcc = V4L2_PIX_FMT_UYVY,
		.mbus_codes = {
			MEDIA_BUS_FMT_YUV10_1X30,
		},
		.is_raw = false,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_UYVY,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT0
		}
	},
	{
		.fourcc = V4L2_PIX_FMT_Y210,
		.mbus_codes = {
			MEDIA_BUS_FMT_YUV10_1X30,
		},
		.is_raw = false,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_Y210,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT0
		}
	},
	/*
	 * This is something of a hack, the ISP thinks it's running NV12M but
	 * by setting uv_plane = 0 we simply discard that planes and only output
	 * the Y-plane.
	 */
	{
		.fourcc = V4L2_PIX_FMT_GREY,
		.mbus_codes = {
			MEDIA_BUS_FMT_YUV10_1X30,
		},
		.is_raw = false,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_NV12_21,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT0
		}
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12M,
		.mbus_codes = {
			MEDIA_BUS_FMT_YUV10_1X30,
		},
		.is_raw = false,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_NV12_21,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT1
		}
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21M,
		.mbus_codes = {
			MEDIA_BUS_FMT_YUV10_1X30,
		},
		.is_raw = false,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_NV12_21,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT2
		}
	},
	/*
	 * RAW uncompressed formats are all packed in 16 bpp.
	 * TODO: Expand this list to encompass all possible RAW formats.
	 */
	{
		.fourcc = V4L2_PIX_FMT_SRGGB16,
		.mbus_codes = {
			MEDIA_BUS_FMT_SRGGB16_1X16,
		},
		.is_raw = true,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_RAW16,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT0
		}
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR16,
		.mbus_codes = {
			MEDIA_BUS_FMT_SBGGR16_1X16,
		},
		.is_raw = true,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_RAW16,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT0
		}
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG16,
		.mbus_codes = {
			MEDIA_BUS_FMT_SGBRG16_1X16,
		},
		.is_raw = true,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_RAW16,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT0
		}
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG16,
		.mbus_codes = {
			MEDIA_BUS_FMT_SGRBG16_1X16,
		},
		.is_raw = true,
		.registers = {
			.base_mode = MALI_C55_OUTPUT_RAW16,
			.uv_plane = MALI_C55_OUTPUT_PLANE_ALT0
		}
	},
};

void mali_c55_cap_dev_write(struct mali_c55_cap_dev *cap_dev, unsigned int addr,
			    u32 val)
{
	mali_c55_ctx_write(cap_dev->mali_c55, addr + cap_dev->reg_offset, val);
}

static u32 mali_c55_cap_dev_read(struct mali_c55_cap_dev *cap_dev, unsigned int addr)
{
	return mali_c55_ctx_read(cap_dev->mali_c55, addr + cap_dev->reg_offset);
}

static void mali_c55_cap_dev_update_bits(struct mali_c55_cap_dev *cap_dev,
					 unsigned int addr, u32 mask, u32 val)
{
	u32 orig, tmp;

	orig = mali_c55_cap_dev_read(cap_dev, addr);

	tmp = orig & ~mask;
	tmp |= val & mask;

	if (tmp != orig)
		mali_c55_cap_dev_write(cap_dev, addr, tmp);
}

static bool
mali_c55_mbus_code_can_produce_fmt(const struct mali_c55_format_info *fmt,
				   u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(fmt->mbus_codes); i++) {
		if (fmt->mbus_codes[i] == code)
			return true;
	}

	return false;
}

bool mali_c55_format_is_raw(unsigned int mbus_code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mali_c55_fmts); i++) {
		if (mali_c55_fmts[i].mbus_codes[0] == mbus_code)
			return mali_c55_fmts[i].is_raw;
	}

	return false;
}

static const struct mali_c55_format_info *
mali_c55_format_from_pix(const u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mali_c55_fmts); i++) {
		if (mali_c55_fmts[i].fourcc == pixelformat)
			return &mali_c55_fmts[i];
	}

	/*
	 * If we find no matching pixelformat, we'll just default to the first
	 * one for now.
	 */

	return &mali_c55_fmts[0];
}

static const char * const capture_device_names[] = {
	"mali-c55 fr",
	"mali-c55 ds",
};

static int mali_c55_link_validate(struct media_link *link)
{
	struct video_device *vdev =
		media_entity_to_video_device(link->sink->entity);
	struct mali_c55_cap_dev *cap_dev = video_get_drvdata(vdev);
	struct v4l2_subdev *sd =
		media_entity_to_v4l2_subdev(link->source->entity);
	const struct v4l2_pix_format_mplane *pix_mp;
	const struct mali_c55_format_info *cap_fmt;
	struct v4l2_subdev_format sd_fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = link->source->index,
	};
	int ret;

	ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &sd_fmt);
	if (ret)
		return ret;

	pix_mp = &cap_dev->format.format;
	cap_fmt = cap_dev->format.info;

	if (sd_fmt.format.width != pix_mp->width ||
	    sd_fmt.format.height != pix_mp->height) {
		dev_dbg(cap_dev->mali_c55->dev,
			"link '%s':%u -> '%s':%u not valid: %ux%u != %ux%u\n",
			link->source->entity->name, link->source->index,
			link->sink->entity->name, link->sink->index,
			sd_fmt.format.width, sd_fmt.format.height,
			pix_mp->width, pix_mp->height);
		return -EPIPE;
	}

	if (!mali_c55_mbus_code_can_produce_fmt(cap_fmt, sd_fmt.format.code)) {
		dev_dbg(cap_dev->mali_c55->dev,
			"link '%s':%u -> '%s':%u not valid: mbus_code 0x%04x cannot produce pixel format %p4cc\n",
			link->source->entity->name, link->source->index,
			link->sink->entity->name, link->sink->index,
			sd_fmt.format.code, &pix_mp->pixelformat);
		return -EPIPE;
	}

	return 0;
}

static const struct media_entity_operations mali_c55_media_ops = {
	.link_validate = mali_c55_link_validate,
};

static int mali_c55_vb2_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
				    unsigned int *num_planes, unsigned int sizes[],
				    struct device *alloc_devs[])
{
	struct mali_c55_cap_dev *cap_dev = q->drv_priv;
	unsigned int i;

	if (*num_planes) {
		if (*num_planes != cap_dev->format.format.num_planes)
			return -EINVAL;

		for (i = 0; i < cap_dev->format.format.num_planes; i++)
			if (sizes[i] < cap_dev->format.format.plane_fmt[i].sizeimage)
				return -EINVAL;
	} else {
		*num_planes = cap_dev->format.format.num_planes;
		for (i = 0; i < cap_dev->format.format.num_planes; i++)
			sizes[i] = cap_dev->format.format.plane_fmt[i].sizeimage;
	}

	return 0;
}

static void mali_c55_buf_queue(struct vb2_buffer *vb)
{
	struct mali_c55_cap_dev *cap_dev = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct mali_c55_buffer *buf = container_of(vbuf,
						   struct mali_c55_buffer, vb);
	unsigned int i;

	buf->planes_pending = cap_dev->format.format.num_planes;

	for (i = 0; i < cap_dev->format.format.num_planes; i++) {
		unsigned long size = cap_dev->format.format.plane_fmt[i].sizeimage;

		vb2_set_plane_payload(vb, i, size);
	}

	buf->vb.field = V4L2_FIELD_NONE;

	guard(spinlock)(&cap_dev->buffers.lock);
	list_add_tail(&buf->queue, &cap_dev->buffers.input);
}

static int mali_c55_buf_init(struct vb2_buffer *vb)
{
	struct mali_c55_cap_dev *cap_dev = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct mali_c55_buffer *buf = container_of(vbuf,
						   struct mali_c55_buffer, vb);
	unsigned int i;

	for (i = 0; i < cap_dev->format.format.num_planes; i++)
		buf->addrs[i] = vb2_dma_contig_plane_dma_addr(vb, i);

	return 0;
}

void mali_c55_set_next_buffer(struct mali_c55_cap_dev *cap_dev)
{
	struct v4l2_pix_format_mplane *pix_mp;
	struct mali_c55_buffer *buf;
	dma_addr_t *addrs;

	scoped_guard(spinlock, &cap_dev->buffers.lock) {
		buf = list_first_entry_or_null(&cap_dev->buffers.input,
					       struct mali_c55_buffer, queue);
		if (buf)
			list_del(&buf->queue);
	}

	if (!buf) {
		/*
		 * If we underflow then we can tell the ISP that we don't want
		 * to write out the next frame.
		 */
		mali_c55_cap_dev_update_bits(cap_dev,
					     MALI_C55_REG_Y_WRITER_MODE,
					     MALI_C55_WRITER_FRAME_WRITE_MASK,
					     0x00);
		mali_c55_cap_dev_update_bits(cap_dev,
					     MALI_C55_REG_UV_WRITER_MODE,
					     MALI_C55_WRITER_FRAME_WRITE_MASK,
					     0x00);
		return;
	}

	pix_mp = &cap_dev->format.format;

	mali_c55_cap_dev_update_bits(cap_dev, MALI_C55_REG_Y_WRITER_MODE,
				     MALI_C55_WRITER_FRAME_WRITE_MASK,
				     MALI_C55_WRITER_FRAME_WRITE_ENABLE);
	if (cap_dev->format.info->registers.uv_plane)
		mali_c55_cap_dev_update_bits(cap_dev,
					     MALI_C55_REG_UV_WRITER_MODE,
					     MALI_C55_WRITER_FRAME_WRITE_MASK,
					     MALI_C55_WRITER_FRAME_WRITE_ENABLE);

	addrs = buf->addrs;
	mali_c55_cap_dev_write(cap_dev,
			       MALI_C55_REG_Y_WRITER_BANKS_BASE,
			       addrs[MALI_C55_PLANE_Y]);
	mali_c55_cap_dev_write(cap_dev,
			       MALI_C55_REG_UV_WRITER_BANKS_BASE,
			       addrs[MALI_C55_PLANE_UV]);

	mali_c55_cap_dev_write(cap_dev,
			       MALI_C55_REG_Y_WRITER_OFFSET,
			       pix_mp->plane_fmt[MALI_C55_PLANE_Y].bytesperline);
	mali_c55_cap_dev_write(cap_dev,
			       MALI_C55_REG_UV_WRITER_OFFSET,
			       pix_mp->plane_fmt[MALI_C55_PLANE_UV].bytesperline);

	guard(spinlock)(&cap_dev->buffers.processing_lock);
	list_add_tail(&buf->queue, &cap_dev->buffers.processing);
}

/**
 * mali_c55_set_plane_done - mark the plane as written and process the buffer if
 *			     both planes are finished.
 * @cap_dev:  pointer to the fr or ds pipe output
 * @plane:    the plane to mark as completed
 *
 * The Mali C55 ISP has muliplanar outputs for some formats that come with two
 * separate "buffer write completed" interrupts - we need to flag each plane's
 * completion and check whether both planes are done - if so, complete the buf
 * in vb2.
 */
void mali_c55_set_plane_done(struct mali_c55_cap_dev *cap_dev,
			     enum mali_c55_planes plane)
{
	struct mali_c55_isp *isp = &cap_dev->mali_c55->isp;
	struct mali_c55_buffer *buf;

	scoped_guard(spinlock, &cap_dev->buffers.processing_lock) {
		buf = list_first_entry_or_null(&cap_dev->buffers.processing,
					       struct mali_c55_buffer, queue);

		/*
		 * If the stream was stopped, the buffer might have been sent
		 * back to userspace already.
		 */
		if (!buf || --buf->planes_pending)
			return;

		list_del(&buf->queue);
	}

	/* If the other plane is also done... */
	buf->vb.vb2_buf.timestamp = ktime_get_boottime_ns();
	buf->vb.sequence = isp->frame_sequence++;
	vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

static void mali_c55_cap_dev_stream_disable(struct mali_c55_cap_dev *cap_dev)
{
	mali_c55_cap_dev_update_bits(cap_dev, MALI_C55_REG_Y_WRITER_MODE,
				     MALI_C55_WRITER_FRAME_WRITE_MASK, 0x00);
	mali_c55_cap_dev_update_bits(cap_dev, MALI_C55_REG_UV_WRITER_MODE,
				     MALI_C55_WRITER_FRAME_WRITE_MASK, 0x00);
}

static void mali_c55_cap_dev_stream_enable(struct mali_c55_cap_dev *cap_dev)
{
	/*
	 * The Mali ISP can hold up to 5 buffer addresses and simply cycle
	 * through them, but it's not clear to me that the vb2 queue _guarantees_
	 * it will queue buffers to the driver in a fixed order, and ensuring
	 * we call vb2_buffer_done() for the right buffer seems to me to add
	 * pointless complexity given in multi-context mode we'd need to
	 * re-write those registers every frame anyway...so we tell the ISP to
	 * use a single register and update it for each frame.
	 */
	mali_c55_cap_dev_update_bits(cap_dev,
				     MALI_C55_REG_Y_WRITER_BANKS_CONFIG,
				     MALI_C55_REG_Y_WRITER_MAX_BANKS_MASK, 0);
	mali_c55_cap_dev_update_bits(cap_dev,
				     MALI_C55_REG_UV_WRITER_BANKS_CONFIG,
				     MALI_C55_REG_UV_WRITER_MAX_BANKS_MASK, 0);

	mali_c55_set_next_buffer(cap_dev);
}

static void mali_c55_cap_dev_return_buffers(struct mali_c55_cap_dev *cap_dev,
					    enum vb2_buffer_state state)
{
	struct mali_c55_buffer *buf, *tmp;

	scoped_guard(spinlock, &cap_dev->buffers.lock) {
		list_for_each_entry_safe(buf, tmp, &cap_dev->buffers.input,
					 queue) {
			list_del(&buf->queue);
			vb2_buffer_done(&buf->vb.vb2_buf, state);
		}
	}

	guard(spinlock)(&cap_dev->buffers.processing_lock);
	list_for_each_entry_safe(buf, tmp, &cap_dev->buffers.processing, queue) {
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
}

static void mali_c55_cap_dev_format_configure(struct mali_c55_cap_dev *cap_dev)
{
	const struct mali_c55_format_info *capture_format = cap_dev->format.info;
	const struct v4l2_pix_format_mplane *pix_mp = &cap_dev->format.format;
	const struct v4l2_format_info *info;

	info = v4l2_format_info(pix_mp->pixelformat);
	if (WARN_ON(!info))
		return;

	mali_c55_cap_dev_write(cap_dev, MALI_C55_REG_Y_WRITER_MODE,
			       capture_format->registers.base_mode);
	mali_c55_cap_dev_write(cap_dev, MALI_C55_REG_ACTIVE_OUT_Y_SIZE,
			       MALI_C55_REG_ACTIVE_OUT_SIZE_W(pix_mp->width) |
			       MALI_C55_REG_ACTIVE_OUT_SIZE_H(pix_mp->height));

	if (info->mem_planes > 1) {
		mali_c55_cap_dev_write(cap_dev, MALI_C55_REG_UV_WRITER_MODE,
				       capture_format->registers.base_mode);
		mali_c55_cap_dev_update_bits(cap_dev,
			MALI_C55_REG_UV_WRITER_MODE,
			MALI_C55_WRITER_SUBMODE_MASK,
			MALI_C55_WRITER_SUBMODE(capture_format->registers.uv_plane));

		mali_c55_cap_dev_write(cap_dev, MALI_C55_REG_ACTIVE_OUT_UV_SIZE,
				MALI_C55_REG_ACTIVE_OUT_SIZE_W(pix_mp->width) |
				MALI_C55_REG_ACTIVE_OUT_SIZE_H(pix_mp->height));
	}

	if (info->pixel_enc == V4L2_PIXEL_ENC_YUV) {
		mali_c55_cap_dev_write(cap_dev, MALI_C55_REG_CS_CONV_CONFIG,
				       MALI_C55_CS_CONV_MATRIX_MASK);

		if (info->hdiv > 1)
			mali_c55_cap_dev_update_bits(cap_dev,
				MALI_C55_REG_CS_CONV_CONFIG,
				MALI_C55_CS_CONV_HORZ_DOWNSAMPLE_MASK,
				MALI_C55_CS_CONV_HORZ_DOWNSAMPLE_ENABLE);
		if (info->vdiv > 1)
			mali_c55_cap_dev_update_bits(cap_dev,
				MALI_C55_REG_CS_CONV_CONFIG,
				MALI_C55_CS_CONV_VERT_DOWNSAMPLE_MASK,
				MALI_C55_CS_CONV_VERT_DOWNSAMPLE_ENABLE);
		if (info->hdiv > 1 || info->vdiv > 1)
			mali_c55_cap_dev_update_bits(cap_dev,
				MALI_C55_REG_CS_CONV_CONFIG,
				MALI_C55_CS_CONV_FILTER_MASK,
				MALI_C55_CS_CONV_FILTER_ENABLE);
	}
}

static int mali_c55_vb2_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct mali_c55_cap_dev *cap_dev = q->drv_priv;
	struct mali_c55 *mali_c55 = cap_dev->mali_c55;
	struct mali_c55_resizer *rsz = cap_dev->rsz;
	struct mali_c55_isp *isp = &mali_c55->isp;
	int ret;

	guard(mutex)(&isp->capture_lock);

	ret = pm_runtime_resume_and_get(mali_c55->dev);
	if (ret)
		goto err_return_buffers;

	ret = video_device_pipeline_alloc_start(&cap_dev->vdev);
	if (ret) {
		dev_dbg(mali_c55->dev, "%s failed to start media pipeline\n",
			cap_dev->vdev.name);
		goto err_pm_put;
	}

	mali_c55_cap_dev_format_configure(cap_dev);
	mali_c55_cap_dev_stream_enable(cap_dev);

	ret = v4l2_subdev_enable_streams(&rsz->sd, MALI_C55_RSZ_SOURCE_PAD,
					 BIT(0));
	if (ret)
		goto err_disable_cap_dev;

	if (mali_c55_pipeline_ready(mali_c55)) {
		ret = v4l2_subdev_enable_streams(&mali_c55->isp.sd,
						 MALI_C55_ISP_PAD_SOURCE_VIDEO,
						 BIT(0));
		if (ret < 0)
			goto err_disable_rsz;
	}

	return 0;

err_disable_rsz:
	v4l2_subdev_disable_streams(&rsz->sd, MALI_C55_RSZ_SOURCE_PAD, BIT(0));
err_disable_cap_dev:
	mali_c55_cap_dev_stream_disable(cap_dev);
	video_device_pipeline_stop(&cap_dev->vdev);
err_pm_put:
	pm_runtime_put_autosuspend(mali_c55->dev);
err_return_buffers:
	mali_c55_cap_dev_return_buffers(cap_dev, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void mali_c55_vb2_stop_streaming(struct vb2_queue *q)
{
	struct mali_c55_cap_dev *cap_dev = q->drv_priv;
	struct mali_c55 *mali_c55 = cap_dev->mali_c55;
	struct mali_c55_resizer *rsz = cap_dev->rsz;
	struct mali_c55_isp *isp = &mali_c55->isp;

	guard(mutex)(&isp->capture_lock);

	if (mali_c55_pipeline_ready(mali_c55)) {
		if (v4l2_subdev_is_streaming(&isp->sd))
			v4l2_subdev_disable_streams(&isp->sd,
						    MALI_C55_ISP_PAD_SOURCE_VIDEO,
						    BIT(0));
	}

	v4l2_subdev_disable_streams(&rsz->sd, MALI_C55_RSZ_SOURCE_PAD, BIT(0));
	mali_c55_cap_dev_stream_disable(cap_dev);
	mali_c55_cap_dev_return_buffers(cap_dev, VB2_BUF_STATE_ERROR);
	video_device_pipeline_stop(&cap_dev->vdev);
	pm_runtime_put_autosuspend(mali_c55->dev);
}

static const struct vb2_ops mali_c55_vb2_ops = {
	.queue_setup		= &mali_c55_vb2_queue_setup,
	.buf_queue		= &mali_c55_buf_queue,
	.buf_init		= &mali_c55_buf_init,
	.start_streaming	= &mali_c55_vb2_start_streaming,
	.stop_streaming		= &mali_c55_vb2_stop_streaming,
};

static const struct v4l2_file_operations mali_c55_v4l2_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

static void mali_c55_try_fmt(struct v4l2_pix_format_mplane *pix_mp)
{
	const struct mali_c55_format_info *capture_format;
	const struct v4l2_format_info *info;
	struct v4l2_plane_pix_format *plane, *y_plane;
	unsigned int padding;
	unsigned int i;

	capture_format = mali_c55_format_from_pix(pix_mp->pixelformat);
	pix_mp->pixelformat = capture_format->fourcc;

	pix_mp->width = clamp(pix_mp->width, MALI_C55_MIN_WIDTH,
			      MALI_C55_MAX_WIDTH);
	pix_mp->height = clamp(pix_mp->height, MALI_C55_MIN_HEIGHT,
			       MALI_C55_MAX_HEIGHT);

	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->colorspace = V4L2_COLORSPACE_DEFAULT;
	pix_mp->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pix_mp->quantization = V4L2_QUANTIZATION_DEFAULT;

	info = v4l2_format_info(pix_mp->pixelformat);
	pix_mp->num_planes = info->mem_planes;
	memset(pix_mp->plane_fmt, 0, sizeof(pix_mp->plane_fmt));

	y_plane = &pix_mp->plane_fmt[0];
	y_plane->bytesperline = clamp(y_plane->bytesperline,
				      info->bpp[0] * pix_mp->width, 65535U);

	/*
	 * The ISP requires that the stride be aligned to 16-bytes. This is not
	 * detailed in the documentation but has been verified experimentally.
	 */
	y_plane->bytesperline = ALIGN(y_plane->bytesperline, 16);
	y_plane->sizeimage = y_plane->bytesperline * pix_mp->height;

	padding = y_plane->bytesperline - (pix_mp->width * info->bpp[0]);

	for (i = 1; i < info->comp_planes; i++) {
		plane = &pix_mp->plane_fmt[i];

		plane->bytesperline = DIV_ROUND_UP(info->bpp[i] * pix_mp->width,
						   info->hdiv) + padding;
		plane->sizeimage = DIV_ROUND_UP(plane->bytesperline *
						pix_mp->height, info->vdiv);
	}

	if (info->mem_planes == 1) {
		for (i = 1; i < info->comp_planes; i++) {
			plane = &pix_mp->plane_fmt[i];
			y_plane->sizeimage += plane->sizeimage;
		}
	}
}

static int mali_c55_try_fmt_vid_cap_mplane(struct file *file, void *fh,
					   struct v4l2_format *f)
{
	mali_c55_try_fmt(&f->fmt.pix_mp);

	return 0;
}

static void mali_c55_set_format(struct mali_c55_cap_dev *cap_dev,
				struct v4l2_pix_format_mplane *pix_mp)
{
	mali_c55_try_fmt(pix_mp);

	cap_dev->format.format = *pix_mp;
	cap_dev->format.info = mali_c55_format_from_pix(pix_mp->pixelformat);
}

static int mali_c55_s_fmt_vid_cap_mplane(struct file *file, void *fh,
					 struct v4l2_format *f)
{
	struct mali_c55_cap_dev *cap_dev = video_drvdata(file);

	if (vb2_is_busy(&cap_dev->queue))
		return -EBUSY;

	mali_c55_set_format(cap_dev, &f->fmt.pix_mp);

	return 0;
}

static int mali_c55_g_fmt_vid_cap_mplane(struct file *file, void *fh,
					 struct v4l2_format *f)
{
	struct mali_c55_cap_dev *cap_dev = video_drvdata(file);

	f->fmt.pix_mp = cap_dev->format.format;

	return 0;
}

static int mali_c55_enum_fmt_vid_cap_mplane(struct file *file, void *fh,
					    struct v4l2_fmtdesc *f)
{
	struct mali_c55_cap_dev *cap_dev = video_drvdata(file);
	unsigned int j = 0;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mali_c55_fmts); i++) {
		if (f->mbus_code &&
		    !mali_c55_mbus_code_can_produce_fmt(&mali_c55_fmts[i],
							f->mbus_code))
			continue;

		/* Downscale pipe can't output RAW formats */
		if (mali_c55_fmts[i].is_raw &&
		    cap_dev->reg_offset == MALI_C55_CAP_DEV_DS_REG_OFFSET)
			continue;

		if (j++ == f->index) {
			f->pixelformat = mali_c55_fmts[i].fourcc;
			return 0;
		}
	}

	return -EINVAL;
}

static int mali_c55_querycap(struct file *file, void *fh,
			     struct v4l2_capability *cap)
{
	strscpy(cap->driver, MALI_C55_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, "ARM Mali-C55 ISP", sizeof(cap->card));

	return 0;
}

static const struct v4l2_ioctl_ops mali_c55_v4l2_ioctl_ops = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_try_fmt_vid_cap_mplane = mali_c55_try_fmt_vid_cap_mplane,
	.vidioc_s_fmt_vid_cap_mplane = mali_c55_s_fmt_vid_cap_mplane,
	.vidioc_g_fmt_vid_cap_mplane = mali_c55_g_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_cap = mali_c55_enum_fmt_vid_cap_mplane,
	.vidioc_querycap = mali_c55_querycap,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int mali_c55_register_cap_dev(struct mali_c55 *mali_c55,
				     enum mali_c55_cap_devs cap_dev_id)
{
	struct mali_c55_cap_dev *cap_dev = &mali_c55->cap_devs[cap_dev_id];
	struct v4l2_pix_format_mplane pix_mp;
	struct video_device *vdev;
	struct vb2_queue *vb2q;
	int ret;

	vdev = &cap_dev->vdev;
	vb2q = &cap_dev->queue;

	cap_dev->mali_c55 = mali_c55;
	mutex_init(&cap_dev->lock);
	INIT_LIST_HEAD(&cap_dev->buffers.input);
	INIT_LIST_HEAD(&cap_dev->buffers.processing);
	spin_lock_init(&cap_dev->buffers.lock);
	spin_lock_init(&cap_dev->buffers.processing_lock);

	switch (cap_dev_id) {
	case MALI_C55_CAP_DEV_FR:
		cap_dev->rsz = &mali_c55->resizers[MALI_C55_RSZ_FR];
		cap_dev->reg_offset = MALI_C55_CAP_DEV_FR_REG_OFFSET;
		break;
	case MALI_C55_CAP_DEV_DS:
		cap_dev->rsz = &mali_c55->resizers[MALI_C55_RSZ_DS];
		cap_dev->reg_offset = MALI_C55_CAP_DEV_DS_REG_OFFSET;
		break;
	default:
		ret = -EINVAL;
		goto err_destroy_mutex;
	}

	cap_dev->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&cap_dev->vdev.entity, 1, &cap_dev->pad);
	if (ret) {
		mutex_destroy(&cap_dev->lock);
		goto err_destroy_mutex;
	}

	vb2q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	vb2q->io_modes = VB2_MMAP | VB2_DMABUF;
	vb2q->drv_priv = cap_dev;
	vb2q->mem_ops = &vb2_dma_contig_memops;
	vb2q->ops = &mali_c55_vb2_ops;
	vb2q->buf_struct_size = sizeof(struct mali_c55_buffer);
	vb2q->min_queued_buffers = 1;
	vb2q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vb2q->lock = &cap_dev->lock;
	vb2q->dev = mali_c55->dev;

	ret = vb2_queue_init(vb2q);
	if (ret) {
		dev_err(mali_c55->dev, "%s vb2 queue init failed\n",
			cap_dev->vdev.name);
		goto err_cleanup_media_entity;
	}

	strscpy(cap_dev->vdev.name, capture_device_names[cap_dev_id],
		sizeof(cap_dev->vdev.name));
	vdev->release = video_device_release_empty;
	vdev->fops = &mali_c55_v4l2_fops;
	vdev->ioctl_ops = &mali_c55_v4l2_ioctl_ops;
	vdev->lock = &cap_dev->lock;
	vdev->v4l2_dev = &mali_c55->v4l2_dev;
	vdev->queue = &cap_dev->queue;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
				V4L2_CAP_STREAMING | V4L2_CAP_IO_MC;
	vdev->entity.ops = &mali_c55_media_ops;
	video_set_drvdata(vdev, cap_dev);

	memset(&pix_mp, 0, sizeof(pix_mp));
	pix_mp.pixelformat = V4L2_PIX_FMT_RGB565;
	pix_mp.width = MALI_C55_DEFAULT_WIDTH;
	pix_mp.height = MALI_C55_DEFAULT_HEIGHT;
	mali_c55_set_format(cap_dev, &pix_mp);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(mali_c55->dev,
			"%s failed to register video device\n",
			cap_dev->vdev.name);
		goto err_release_vb2q;
	}

	return 0;

err_release_vb2q:
	vb2_queue_release(vb2q);
err_cleanup_media_entity:
	media_entity_cleanup(&cap_dev->vdev.entity);
err_destroy_mutex:
	mutex_destroy(&cap_dev->lock);

	return ret;
}

int mali_c55_register_capture_devs(struct mali_c55 *mali_c55)
{
	int ret;

	ret = mali_c55_register_cap_dev(mali_c55, MALI_C55_CAP_DEV_FR);
	if (ret)
		return ret;

	if (mali_c55->capabilities & MALI_C55_GPS_DS_PIPE_FITTED) {
		ret = mali_c55_register_cap_dev(mali_c55, MALI_C55_CAP_DEV_DS);
		if (ret) {
			mali_c55_unregister_capture_devs(mali_c55);
			return ret;
		}
	}

	return 0;
}

static void mali_c55_unregister_cap_dev(struct mali_c55 *mali_c55,
					enum mali_c55_cap_devs cap_dev_id)
{
	struct mali_c55_cap_dev *cap_dev = &mali_c55->cap_devs[cap_dev_id];

	if (!video_is_registered(&cap_dev->vdev))
		return;

	vb2_video_unregister_device(&cap_dev->vdev);
	media_entity_cleanup(&cap_dev->vdev.entity);
	mutex_destroy(&cap_dev->lock);
}

void mali_c55_unregister_capture_devs(struct mali_c55 *mali_c55)
{
	mali_c55_unregister_cap_dev(mali_c55, MALI_C55_CAP_DEV_FR);
	if (mali_c55->capabilities & MALI_C55_GPS_DS_PIPE_FITTED)
		mali_c55_unregister_cap_dev(mali_c55, MALI_C55_CAP_DEV_DS);
}
