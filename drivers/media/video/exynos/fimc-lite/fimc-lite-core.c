/*
 * Register interface file for Samsung Camera Interface (FIMC-Lite) driver
 *
 * Copyright (c) 2011 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#if defined(CONFIG_MEDIA_CONTROLLER) && defined(CONFIG_ARCH_EXYNOS5)
#include <mach/videonode.h>
#include <media/exynos_mc.h>
#endif
#include "fimc-lite-core.h"

#define MODULE_NAME			"exynos-fimc-lite"
#define DEFAULT_FLITE_SINK_WIDTH	800
#define DEFAULT_FLITE_SINK_HEIGHT	480
#define CAMIF_TOP_CLK			"camif_top"

static struct flite_fmt flite_formats[] = {
	{
		.name		= "YUV422 8-bit 1 plane(UYVY)",
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.depth		= { 16 },
		.code		= V4L2_MBUS_FMT_UYVY8_2X8,
		.fmt_reg	= FLITE_REG_CIGCTRL_YUV422_1P,
		.is_yuv		= 1,
	}, {
		.name		= "YUV422 8-bit 1 plane(VYUY)",
		.pixelformat	= V4L2_PIX_FMT_VYUY,
		.depth		= { 16 },
		.code		= V4L2_MBUS_FMT_VYUY8_2X8,
		.fmt_reg	= FLITE_REG_CIGCTRL_YUV422_1P,
		.is_yuv		= 1,
	}, {
		.name		= "YUV422 8-bit 1 plane(YUYV)",
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.depth		= { 16 },
		.code		= V4L2_MBUS_FMT_YUYV8_2X8,
		.fmt_reg	= FLITE_REG_CIGCTRL_YUV422_1P,
		.is_yuv		= 1,
	}, {
		.name		= "YUV422 8-bit 1 plane(YVYU)",
		.pixelformat	= V4L2_PIX_FMT_YVYU,
		.depth		= { 16 },
		.code		= V4L2_MBUS_FMT_YVYU8_2X8,
		.fmt_reg	= FLITE_REG_CIGCTRL_YUV422_1P,
		.is_yuv		= 1,
	}, {
		.name		= "RAW8(GRBG)",
		.pixelformat	= V4L2_PIX_FMT_SGRBG8,
		.depth		= { 8 },
		.code		= V4L2_MBUS_FMT_SGRBG8_1X8,
		.fmt_reg	= FLITE_REG_CIGCTRL_RAW8,
		.is_yuv		= 0,
	}, {
		.name		= "RAW10(GRBG)",
		.pixelformat	= V4L2_PIX_FMT_SGRBG10,
		.depth		= { 10 },
		.code		= V4L2_MBUS_FMT_SGRBG10_1X10,
		.fmt_reg	= FLITE_REG_CIGCTRL_RAW10,
		.is_yuv		= 0,
	}, {
		.name		= "RAW12(GRBG)",
		.pixelformat	= V4L2_PIX_FMT_SGRBG12,
		.depth		= { 12 },
		.code		= V4L2_MBUS_FMT_SGRBG12_1X12,
		.fmt_reg	= FLITE_REG_CIGCTRL_RAW12,
		.is_yuv		= 0,
	}, {
		.name		= "User Defined(JPEG)",
		.code		= V4L2_MBUS_FMT_JPEG_1X8,
		.depth		= { 8 },
		.fmt_reg	= FLITE_REG_CIGCTRL_USER(1),
		.is_yuv		= 0,
	},
};

#if defined(CONFIG_MEDIA_CONTROLLER) && defined(CONFIG_ARCH_EXYNOS5)
static struct flite_variant variant = {
	.max_w			= 8192,
	.max_h			= 8192,
	.align_win_offs_w	= 2,
	.align_out_w		= 8,
	.align_out_offs_w	= 8,
};

static struct flite_fmt *get_format(int index)
{
	return &flite_formats[index];
}

static struct flite_fmt *find_format(u32 *pixelformat, u32 *mbus_code, int index)
{
	struct flite_fmt *fmt, *def_fmt = NULL;
	unsigned int i;

	if (index >= ARRAY_SIZE(flite_formats))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(flite_formats); ++i) {
		fmt = get_format(i);
		if (pixelformat && fmt->pixelformat == *pixelformat)
			return fmt;
		if (mbus_code && fmt->code == *mbus_code)
			return fmt;
		if (index == i)
			def_fmt = fmt;
	}
	return def_fmt;

}
#endif

inline struct flite_fmt const *find_flite_format(struct v4l2_mbus_framefmt *mf)
{
	int num_fmt = ARRAY_SIZE(flite_formats);

	while (num_fmt--)
		if (mf->code == flite_formats[num_fmt].code)
			break;
	if (num_fmt < 0)
		return NULL;

	return &flite_formats[num_fmt];
}

static int flite_s_mbus_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct flite_dev *flite = v4l2_get_subdevdata(sd);
	struct flite_fmt const *f_fmt = find_flite_format(mf);
	struct flite_frame *f_frame = &flite->s_frame;

	flite_dbg("w: %d, h: %d", mf->width, mf->height);

	if (unlikely(!f_fmt)) {
		flite_err("f_fmt is null");
		return -EINVAL;
	}

	flite->mbus_fmt = *mf;

	/*
	 * These are the datas from fimc
	 * If you want to crop the image, you can use s_crop
	 */
	f_frame->o_width = mf->width;
	f_frame->o_height = mf->height;
	f_frame->width = mf->width;
	f_frame->height = mf->height;
	f_frame->offs_h = 0;
	f_frame->offs_v = 0;

	return 0;
}

static int flite_g_mbus_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct flite_dev *flite = v4l2_get_subdevdata(sd);

	mf = &flite->mbus_fmt;

	return 0;
}

static int flite_subdev_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *cc)
{
	struct flite_dev *flite = v4l2_get_subdevdata(sd);
	struct flite_frame *f;

	f = &flite->s_frame;

	cc->bounds.left		= 0;
	cc->bounds.top		= 0;
	cc->bounds.width	= f->o_width;
	cc->bounds.height	= f->o_height;
	cc->defrect		= cc->bounds;

	return 0;
}

static int flite_subdev_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *crop)
{
	struct flite_dev *flite = v4l2_get_subdevdata(sd);
	struct flite_frame *f;

	f = &flite->s_frame;

	crop->c.left	= f->offs_h;
	crop->c.top	= f->offs_v;
	crop->c.width	= f->width;
	crop->c.height	= f->height;

	return 0;
}

static int flite_subdev_s_crop(struct v4l2_subdev *sd, struct v4l2_crop *crop)
{
	struct flite_dev *flite = v4l2_get_subdevdata(sd);
	struct flite_frame *f;

	f = &flite->s_frame;

	if (crop->c.left + crop->c.width > f->o_width) {
		flite_err("Unsupported crop width");
		return -EINVAL;
	}
	if (crop->c.top + crop->c.height > f->o_height) {
		flite_err("Unsupported crop height");
		return -EINVAL;
	}

	f->width = crop->c.width;
	f->height = crop->c.height;
	f->offs_h = crop->c.left;
	f->offs_v = crop->c.top;

	flite_dbg("width : %d, height : %d, offs_h : %d, off_v : %dn",
			f->width, f->height, f->offs_h, f->offs_v);

	return 0;
}

static int flite_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct flite_dev *flite = v4l2_get_subdevdata(sd);
	u32 index = flite->pdata->active_cam_index;
	struct s3c_platform_camera *cam = NULL;
	u32 int_src = 0;
	unsigned long flags;
	int ret = 0;

	if (!(flite->output & FLITE_OUTPUT_MEM)) {
		if (enable)
			flite_hw_reset(flite);
		cam = flite->pdata->cam[index];
	}

	spin_lock_irqsave(&flite->slock, flags);

	if (test_bit(FLITE_ST_SUSPEND, &flite->state))
		goto s_stream_unlock;

	if (enable) {
		flite_hw_set_cam_channel(flite);
		flite_hw_set_cam_source_size(flite);

		if (!(flite->output & FLITE_OUTPUT_MEM)) {
			flite_info("@local out start@");
			flite_hw_set_camera_type(flite, cam);
			flite_hw_set_config_irq(flite, cam);
			if (IS_ERR_OR_NULL(cam)) {
				flite_err("cam is null");
				goto s_stream_unlock;
			}
			if (cam->use_isp)
				flite_hw_set_output_dma(flite, false);
			int_src = FLITE_REG_CIGCTRL_IRQ_OVFEN0_ENABLE |
				FLITE_REG_CIGCTRL_IRQ_LASTEN0_ENABLE |
				FLITE_REG_CIGCTRL_IRQ_ENDEN0_DISABLE |
				FLITE_REG_CIGCTRL_IRQ_STARTEN0_DISABLE;
		} else {
			flite_info("@mem out start@");
			flite_hw_set_sensor_type(flite);
			flite_hw_set_inverse_polarity(flite);
			set_bit(FLITE_ST_PEND, &flite->state);
			flite_hw_set_output_dma(flite, true);
			int_src = FLITE_REG_CIGCTRL_IRQ_OVFEN0_ENABLE |
				FLITE_REG_CIGCTRL_IRQ_LASTEN0_ENABLE |
				FLITE_REG_CIGCTRL_IRQ_ENDEN0_ENABLE |
				FLITE_REG_CIGCTRL_IRQ_STARTEN0_DISABLE;
			flite_hw_set_out_order(flite);
			flite_hw_set_output_size(flite);
			flite_hw_set_dma_offset(flite);
		}
		ret = flite_hw_set_source_format(flite);
		if (unlikely(ret < 0))
			goto s_stream_unlock;

		flite_hw_set_interrupt_source(flite, int_src);
		flite_hw_set_window_offset(flite);
		flite_hw_set_capture_start(flite);

		set_bit(FLITE_ST_STREAM, &flite->state);
	} else {
		if (test_bit(FLITE_ST_STREAM, &flite->state)) {
			flite_hw_set_capture_stop(flite);
			spin_unlock_irqrestore(&flite->slock, flags);
			ret = wait_event_timeout(flite->irq_queue,
			!test_bit(FLITE_ST_STREAM, &flite->state), HZ/20); /* 50 ms */
			if (unlikely(!ret)) {
				v4l2_err(sd, "wait timeout\n");
				ret = -EBUSY;
			}
			return ret;
		} else {
			goto s_stream_unlock;
		}
	}
s_stream_unlock:
	spin_unlock_irqrestore(&flite->slock, flags);
	return ret;
}

static irqreturn_t flite_irq_handler(int irq, void *priv)
{
	struct flite_dev *flite = priv;
#if defined(CONFIG_MEDIA_CONTROLLER) && defined(CONFIG_ARCH_EXYNOS5)
	struct flite_buffer *buf;
#endif
	u32 int_src = 0;

	flite_hw_get_int_src(flite, &int_src);
	flite_hw_clear_irq(flite);

	spin_lock(&flite->slock);

	switch (int_src & FLITE_REG_CISTATUS_IRQ_MASK) {
	case FLITE_REG_CISTATUS_IRQ_SRC_OVERFLOW:
		clear_bit(FLITE_ST_RUN, &flite->state);
		flite_err("overflow generated");
		break;
	case FLITE_REG_CISTATUS_IRQ_SRC_LASTCAPEND:
		flite_hw_set_last_capture_end_clear(flite);
		flite_info("last capture end");
		clear_bit(FLITE_ST_STREAM, &flite->state);
		wake_up(&flite->irq_queue);
		break;
	case FLITE_REG_CISTATUS_IRQ_SRC_FRMSTART:
		flite_dbg("frame start");
		break;
	case FLITE_REG_CISTATUS_IRQ_SRC_FRMEND:
		set_bit(FLITE_ST_RUN, &flite->state);
		flite_dbg("frame end");
		break;
	}
#if defined(CONFIG_MEDIA_CONTROLLER) && defined(CONFIG_ARCH_EXYNOS5)
	if (flite->output & FLITE_OUTPUT_MEM) {
		if (!list_empty(&flite->active_buf_q)) {
			buf = active_queue_pop(flite);
			if (!test_bit(FLITE_ST_RUN, &flite->state)) {
				flite_info("error interrupt");
				vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
				goto unlock;
			}
			vb2_buffer_done(&buf->vb, VB2_BUF_STATE_DONE);
			flite_dbg("done_index : %d", buf->vb.v4l2_buf.index);
		}
		if (!list_empty(&flite->pending_buf_q)) {
			buf = pending_queue_pop(flite);
			flite_hw_set_output_addr(flite, &buf->paddr,
					buf->vb.v4l2_buf.index);
			active_queue_add(flite, buf);
		}
		if (flite->active_buf_cnt == 0)
			clear_bit(FLITE_ST_RUN, &flite->state);
	}
unlock:
#endif
	spin_unlock(&flite->slock);

	return IRQ_HANDLED;
}

static int flite_s_power(struct v4l2_subdev *sd, int on)
{
	struct flite_dev *flite = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (on) {
		pm_runtime_get_sync(&flite->pdev->dev);
		set_bit(FLITE_ST_POWER, &flite->state);
	} else {
		pm_runtime_put_sync(&flite->pdev->dev);
		clear_bit(FLITE_ST_POWER, &flite->state);
	}

	return ret;
}

#if defined(CONFIG_MEDIA_CONTROLLER) && defined(CONFIG_ARCH_EXYNOS5)
static int flite_subdev_enum_mbus_code(struct v4l2_subdev *sd,
				       struct v4l2_subdev_fh *fh,
				       struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(flite_formats))
		return -EINVAL;

	code->code = flite_formats[code->index].code;

	return 0;
}

static struct v4l2_mbus_framefmt *__flite_get_format(
		struct flite_dev *flite, struct v4l2_subdev_fh *fh,
		u32 pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return fh ? v4l2_subdev_get_try_format(fh, pad) : NULL;
	else
		return &flite->mbus_fmt;
}

static void flite_try_format(struct flite_dev *flite, struct v4l2_subdev_fh *fh,
			     struct v4l2_mbus_framefmt *fmt,
			     enum v4l2_subdev_format_whence which)
{
	struct flite_fmt const *ffmt;
	struct flite_frame *f = &flite->s_frame;
	ffmt = find_flite_format(fmt);
	if (ffmt == NULL)
		ffmt = &flite_formats[1];

	fmt->code = ffmt->code;
	fmt->width = clamp_t(u32, fmt->width, 1, variant.max_w);
	fmt->height = clamp_t(u32, fmt->height, 1, variant.max_h);

	f->offs_h = f->offs_v = 0;
	f->width = f->o_width = fmt->width;
	f->height = f->o_height = fmt->height;

	fmt->colorspace = V4L2_COLORSPACE_JPEG;
	fmt->field = V4L2_FIELD_NONE;
}

static int flite_subdev_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_format *fmt)
{
	struct flite_dev *flite = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;

	mf = __flite_get_format(flite, fh, fmt->pad, fmt->which);
	if (mf == NULL) {
		flite_err("__flite_get_format is null");
		return -EINVAL;
	}

	fmt->format = *mf;

	if (fmt->pad != FLITE_PAD_SINK) {
		struct flite_frame *f = &flite->s_frame;
		fmt->format.width = f->width;
		fmt->format.height = f->height;
	}

	return 0;
}

static int flite_subdev_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_format *fmt)
{
	struct flite_dev *flite = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;

	if (fmt->pad != FLITE_PAD_SINK)
		return -EPERM;

	mf = __flite_get_format(flite, fh, fmt->pad, fmt->which);
	if (mf == NULL) {
		flite_err("__flite_get_format is null");
		return -EINVAL;
	}

	flite_try_format(flite, fh, &fmt->format, fmt->which);
	*mf = fmt->format;

	return 0;
}

static void flite_try_crop(struct flite_dev *flite, struct v4l2_subdev_crop *crop)
{
	struct flite_frame *f_frame = flite_get_frame(flite, crop->pad);

	u32 max_left = f_frame->o_width - crop->rect.width;
	u32 max_top = f_frame->o_height - crop->rect.height;
	u32 crop_max_w = f_frame->o_width - crop->rect.left;
	u32 crop_max_h = f_frame->o_height - crop->rect.top;

	crop->rect.left = clamp_t(u32, crop->rect.left, 0, max_left);
	crop->rect.top = clamp_t(u32, crop->rect.top, 0, max_top);
	crop->rect.width = clamp_t(u32, crop->rect.width, 2, crop_max_w);
	crop->rect.height = clamp_t(u32, crop->rect.height, 1, crop_max_h);
}

static int __flite_get_crop(struct flite_dev *flite, struct v4l2_subdev_fh *fh,
			    unsigned int pad, enum v4l2_subdev_format_whence which,
			    struct v4l2_rect *crop)
{
	struct flite_frame *frame = &flite->s_frame;

	if (which == V4L2_SUBDEV_FORMAT_TRY) {
		crop = v4l2_subdev_get_try_crop(fh, pad);
	} else {
		crop->left = frame->offs_h;
		crop->top = frame->offs_v;
		crop->width = frame->width;
		crop->height = frame->height;
	}

	return 0;
}

static int flite_subdev_get_crop(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_crop *crop)
{
	struct flite_dev *flite = v4l2_get_subdevdata(sd);
	struct v4l2_rect fcrop;

	fcrop.left = fcrop.top = fcrop.width = fcrop.height = 0;

	if (crop->pad != FLITE_PAD_SINK) {
		flite_err("crop is supported only sink pad");
		return -EINVAL;
	}

	__flite_get_crop(flite, fh, crop->pad, crop->which, &fcrop);
	crop->rect = fcrop;

	return 0;
}

static int flite_subdev_set_crop(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_crop *crop)
{
	struct flite_dev *flite = v4l2_get_subdevdata(sd);
	struct flite_frame *f_frame = flite_get_frame(flite, crop->pad);

	if (!(flite->output & FLITE_OUTPUT_MEM) && (crop->pad != FLITE_PAD_SINK)) {
		flite_err("crop is supported only sink pad");
		return -EINVAL;
	}

	flite_try_crop(flite, crop);

	if (crop->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		f_frame->offs_h = crop->rect.left;
		f_frame->offs_v = crop->rect.top;
		f_frame->width = crop->rect.width;
		f_frame->height = crop->rect.height;
	}

	return 0;
}

static int flite_init_formats(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;
	struct flite_dev *flite = v4l2_get_subdevdata(sd);

	if (!test_bit(FLITE_ST_SUBDEV_OPEN, &flite->state)) {
		flite->s_frame.fmt = get_format(2);
		memset(&format, 0, sizeof(format));
		format.pad = FLITE_PAD_SINK;
		format.which = fh ?
			V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
		format.format.code = flite->s_frame.fmt->code;
		format.format.width = DEFAULT_FLITE_SINK_WIDTH;
		format.format.height = DEFAULT_FLITE_SINK_HEIGHT;

		flite_subdev_set_fmt(sd, fh, &format);

		flite->d_frame.fmt = get_format(2);
		set_bit(FLITE_ST_SUBDEV_OPEN, &flite->state);
	}

	return 0;
}

static int flite_subdev_close(struct v4l2_subdev *sd,
			      struct v4l2_subdev_fh *fh)
{
	struct flite_dev *flite = v4l2_get_subdevdata(sd);

	flite_info("");
	clear_bit(FLITE_ST_SUBDEV_OPEN, &flite->state);
	return 0;
}

static int flite_subdev_registered(struct v4l2_subdev *sd)
{
	flite_dbg("");
	return 0;
}

static void flite_subdev_unregistered(struct v4l2_subdev *sd)
{
	flite_dbg("");
}

static const struct v4l2_subdev_internal_ops flite_v4l2_internal_ops = {
	.open = flite_init_formats,
	.close = flite_subdev_close,
	.registered = flite_subdev_registered,
	.unregistered = flite_subdev_unregistered,
};

static int flite_link_setup(struct media_entity *entity,
			    const struct media_pad *local,
			    const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct flite_dev *flite = v4l2_get_subdevdata(sd);

	flite_info("");
	switch (local->index | media_entity_type(remote->entity)) {
	case FLITE_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (flite->input != FLITE_INPUT_NONE) {
				flite_err("link is busy");
				return -EBUSY;
			}
			if (remote->index == CSIS_PAD_SOURCE)
				flite->input = FLITE_INPUT_CSIS;
			else
				flite->input = FLITE_INPUT_SENSOR;
		} else {
			flite->input = FLITE_INPUT_NONE;
		}
		break;

	case FLITE_PAD_SOURCE_PREV | MEDIA_ENT_T_V4L2_SUBDEV: /* fall through */
	case FLITE_PAD_SOURCE_CAMCORD | MEDIA_ENT_T_V4L2_SUBDEV:
		if (flags & MEDIA_LNK_FL_ENABLED)
			flite->output |= FLITE_OUTPUT_GSC;
		else
			flite->output &= ~FLITE_OUTPUT_GSC;
		break;
	case FLITE_PAD_SOURCE_MEM | MEDIA_ENT_T_DEVNODE:
		if (flags & MEDIA_LNK_FL_ENABLED)
			flite->output |= FLITE_OUTPUT_MEM;
		else
			flite->output &= ~FLITE_OUTPUT_MEM;
		break;
	default:
		flite_err("ERR link");
		return -EINVAL;
	}

	return 0;
}

static const struct media_entity_operations flite_media_ops = {
	.link_setup = flite_link_setup,
};

static struct v4l2_subdev_pad_ops flite_pad_ops = {
	.enum_mbus_code = flite_subdev_enum_mbus_code,
	.get_fmt	= flite_subdev_get_fmt,
	.set_fmt	= flite_subdev_set_fmt,
	.get_crop	= flite_subdev_get_crop,
	.set_crop	= flite_subdev_set_crop,
};

static void flite_pipeline_prepare(struct flite_dev *flite, struct media_entity *me)
{
	struct media_entity_graph graph;
	struct v4l2_subdev *sd;

	media_entity_graph_walk_start(&graph, me);

	while ((me = media_entity_graph_walk_next(&graph))) {
		flite_info("me->name : %s", me->name);
		if (media_entity_type(me) != MEDIA_ENT_T_V4L2_SUBDEV)
			continue;
		sd = media_entity_to_v4l2_subdev(me);
		switch (sd->grp_id) {
		case FLITE_GRP_ID:
			flite->pipeline.flite = sd;
			break;
		case SENSOR_GRP_ID:
			flite->pipeline.sensor = sd;
			break;
		case CSIS_GRP_ID:
			flite->pipeline.csis = sd;
			break;
		default:
			flite_warn("Another link's group id");
			break;
		}
	}

	flite_info("flite->pipeline.flite : 0x%p", flite->pipeline.flite);
	flite_info("flite->pipeline.sensor : 0x%p", flite->pipeline.sensor);
	flite_info("flite->pipeline.csis : 0x%p", flite->pipeline.csis);
}

static void flite_set_cam_clock(struct flite_dev *flite, bool on)
{
	struct v4l2_subdev *sd = flite->pipeline.sensor;

	clk_enable(flite->gsc_clk);
	if (flite->pipeline.sensor) {
		struct flite_sensor_info *s_info = v4l2_get_subdev_hostdata(sd);
		on ? clk_enable(s_info->camclk) : clk_disable(s_info->camclk);
	}
}

static int __subdev_set_power(struct v4l2_subdev *sd, int on)
{
	int *use_count;
	int ret;

	if (sd == NULL)
		return -ENXIO;

	use_count = &sd->entity.use_count;
	if (on && (*use_count)++ > 0)
		return 0;
	else if (!on && (*use_count == 0 || --(*use_count) > 0))
		return 0;
	ret = v4l2_subdev_call(sd, core, s_power, on);

	return ret != -ENOIOCTLCMD ? ret : 0;
}

static int flite_pipeline_s_power(struct flite_dev *flite, int state)
{
	int ret = 0;

	if (!flite->pipeline.sensor)
		return -ENXIO;

	if (state) {
		ret = __subdev_set_power(flite->pipeline.flite, 1);
		if (ret && ret != -ENXIO)
			return ret;
		ret = __subdev_set_power(flite->pipeline.csis, 1);
		if (ret && ret != -ENXIO)
			return ret;
		ret = __subdev_set_power(flite->pipeline.sensor, 1);
	} else {
		ret = __subdev_set_power(flite->pipeline.flite, 0);
		if (ret && ret != -ENXIO)
			return ret;
		ret = __subdev_set_power(flite->pipeline.sensor, 0);
		if (ret && ret != -ENXIO)
			return ret;
		ret = __subdev_set_power(flite->pipeline.csis, 0);
	}
	return ret == -ENXIO ? 0 : ret;
}

static int __flite_pipeline_initialize(struct flite_dev *flite,
					 struct media_entity *me, bool prep)
{
	int ret = 0;

	if (prep)
		flite_pipeline_prepare(flite, me);

	if (!flite->pipeline.sensor)
		return -EINVAL;

	flite_set_cam_clock(flite, true);

	if (flite->pipeline.sensor)
		ret = flite_pipeline_s_power(flite, 1);

	return ret;
}

static int flite_pipeline_initialize(struct flite_dev *flite,
				struct media_entity *me, bool prep)
{
	int ret;

	mutex_lock(&me->parent->graph_mutex);
	ret =  __flite_pipeline_initialize(flite, me, prep);
	mutex_unlock(&me->parent->graph_mutex);

	return ret;
}

static int flite_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct flite_dev *flite = ctrl_to_dev(ctrl);

	switch (ctrl->id) {
	case V4L2_CID_CACHEABLE:
		user_to_drv(flite->flite_ctrls.cacheable, ctrl->val);
		break;
	default:
		flite_err("unsupported ctrl id");
		return -EINVAL;
	}

	return 0;
}

const struct v4l2_ctrl_ops flite_ctrl_ops = {
	.s_ctrl = flite_s_ctrl,
};

static const struct v4l2_ctrl_config flite_custom_ctrl[] = {
	{
		.ops = &flite_ctrl_ops,
		.id = V4L2_CID_CACHEABLE,
		.name = "Set cacheable",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 1,
		.def = true,
	},
};

static int flite_ctrls_create(struct flite_dev *flite)
{
	if (flite->ctrls_rdy)
		return 0;

	v4l2_ctrl_handler_init(&flite->ctrl_handler, FLITE_MAX_CTRL_NUM);
	flite->flite_ctrls.cacheable = v4l2_ctrl_new_custom(&flite->ctrl_handler,
					&flite_custom_ctrl[0], NULL);
	flite->ctrls_rdy = flite->ctrl_handler.error == 0;

	if (flite->ctrl_handler.error) {
		int err = flite->ctrl_handler.error;
		v4l2_ctrl_handler_free(&flite->ctrl_handler);
		flite_err("Failed to flite control hander create");
		return err;
	}

	return 0;
}

static void flite_ctrls_delete(struct flite_dev *flite)
{
	if (flite->ctrls_rdy) {
		v4l2_ctrl_handler_free(&flite->ctrl_handler);
		flite->ctrls_rdy = false;
	}
}

static int flite_open(struct file *file)
{
	struct flite_dev *flite = video_drvdata(file);
	int ret = v4l2_fh_open(file);

	if (ret)
		return ret;

	if (test_bit(FLITE_ST_OPEN, &flite->state)) {
		v4l2_fh_release(file);
		return -EBUSY;
	}

	set_bit(FLITE_ST_OPEN, &flite->state);

	if (++flite->refcnt == 1) {
		ret = flite_pipeline_initialize(flite, &flite->vfd->entity, true);
		if (ret < 0) {
			flite_err("flite pipeline initialization failed\n");
			goto err;
		}

		ret = flite_ctrls_create(flite);
		if (ret) {
			flite_err("failed to create controls\n");
			goto err;
		}

	}

	flite_info("pid: %d, state: 0x%lx", task_pid_nr(current), flite->state);

	return 0;

err:
	v4l2_fh_release(file);
	clear_bit(FLITE_ST_OPEN, &flite->state);
	return ret;
}

int __flite_pipeline_shutdown(struct flite_dev *flite)
{
	int ret = 0;

	if (flite->pipeline.sensor)
		ret = flite_pipeline_s_power(flite, 0);

	if (ret && ret != -ENXIO)
		flite_set_cam_clock(flite, false);

	return ret == -ENXIO ? 0 : ret;
}

int flite_pipeline_shutdown(struct flite_dev *flite)
{
	struct media_entity *me = &flite->vfd->entity;
	int ret;

	mutex_lock(&me->parent->graph_mutex);
	ret = __flite_pipeline_shutdown(flite);
	mutex_unlock(&me->parent->graph_mutex);

	return ret;
}

static int flite_close(struct file *file)
{
	struct flite_dev *flite = video_drvdata(file);
	struct flite_buffer *buf;

	flite_info("pid: %d, state: 0x%lx", task_pid_nr(current), flite->state);

	if (--flite->refcnt == 0) {
		clear_bit(FLITE_ST_OPEN, &flite->state);
		flite_info("FIMC-LITE h/w disable control");
		flite_hw_set_capture_stop(flite);
		clear_bit(FLITE_ST_STREAM, &flite->state);
		flite_pipeline_shutdown(flite);
		clear_bit(FLITE_ST_SUSPEND, &flite->state);
	}

	if (flite->refcnt == 0) {
		while (!list_empty(&flite->pending_buf_q)) {
			flite_info("clean pending q");
			buf = pending_queue_pop(flite);
			vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
		}

		while (!list_empty(&flite->active_buf_q)) {
			flite_info("clean active q");
			buf = active_queue_pop(flite);
			vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
		}
		vb2_queue_release(&flite->vbq);
		flite_ctrls_delete(flite);
	}

	return v4l2_fh_release(file);
}

static unsigned int flite_poll(struct file *file,
				      struct poll_table_struct *wait)
{
	struct flite_dev *flite = video_drvdata(file);

	return vb2_poll(&flite->vbq, file, wait);
}

static int flite_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct flite_dev *flite = video_drvdata(file);

	return vb2_mmap(&flite->vbq, vma);
}

/*
 * videobuf2 operations
 */

int flite_pipeline_s_stream(struct flite_dev *flite, int on)
{
	struct flite_pipeline *p = &flite->pipeline;
	int ret = 0;

	if (!p->sensor)
		return -ENODEV;

	if (on) {
		ret = v4l2_subdev_call(p->flite, video, s_stream, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
		ret = v4l2_subdev_call(p->csis, video, s_stream, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
		ret = v4l2_subdev_call(p->sensor, video, s_stream, 1);
	} else {
		ret = v4l2_subdev_call(p->sensor, video, s_stream, 0);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
		ret = v4l2_subdev_call(p->csis, video, s_stream, 0);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
		ret = v4l2_subdev_call(p->flite, video, s_stream, 0);
	}

	return ret == -ENOIOCTLCMD ? 0 : ret;
}

static int flite_start_streaming(struct vb2_queue *q)
{
	struct flite_dev *flite = q->drv_priv;

	flite_hw_reset(flite);
	flite->active_buf_cnt = 0;
	flite->pending_buf_cnt = 0;

	flite->mdev->is_flite_on= true;
	return 0;
}

static int flite_state_cleanup(struct flite_dev *flite)
{
	unsigned long flags;
	bool streaming;

	spin_lock_irqsave(&flite->slock, flags);
	streaming = flite->state & (1 << FLITE_ST_PIPE_STREAM);

	flite->state &= ~(1 << FLITE_ST_RUN | 1 << FLITE_ST_STREAM |
			1 << FLITE_ST_PIPE_STREAM | 1 << FLITE_ST_PEND);

	set_bit(FLITE_ST_SUSPEND, &flite->state);
	spin_unlock_irqrestore(&flite->slock, flags);

	if (streaming)
		return flite_pipeline_s_stream(flite, 0);
	else
		return 0;
}

static int flite_stop_capture(struct flite_dev *flite)
{
	if (!flite_active(flite)) {
		flite_warn("already stopped\n");
		return 0;
	}
	flite_info("FIMC-Lite H/W disable control");
	flite_hw_set_capture_stop(flite);
	clear_bit(FLITE_ST_STREAM, &flite->state);

	return flite_state_cleanup(flite);
}

static int flite_stop_streaming(struct vb2_queue *q)
{
	struct flite_dev *flite = q->drv_priv;

	if (!flite_active(flite))
		return -EINVAL;

	flite->mdev->is_flite_on= false;

	return flite_stop_capture(flite);
}

static u32 get_plane_size(struct flite_frame *frame, unsigned int plane)
{
	if (!frame) {
		flite_err("frame is null");
		return 0;
	}

	return frame->payload;
}

static int flite_queue_setup(struct vb2_queue *vq, unsigned int *num_buffers,
		       unsigned int *num_planes, unsigned long sizes[],
		       void *allocators[])
{
	struct flite_dev *flite = vq->drv_priv;
	struct flite_fmt *fmt = flite->d_frame.fmt;

	if (!fmt)
		return -EINVAL;

	*num_planes = 1;

	sizes[0] = get_plane_size(&flite->d_frame, 0);
	allocators[0] = flite->alloc_ctx;

	return 0;
}

static int flite_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct flite_dev *flite = vq->drv_priv;
	struct flite_frame *frame = &flite->d_frame;
	unsigned long size;

	if (frame->fmt == NULL)
		return -EINVAL;

	size = frame->payload;

	if (vb2_plane_size(vb, 0) < size) {
		v4l2_err(flite->vfd, "User buffer too small (%ld < %ld)\n",
			 vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}
	vb2_set_plane_payload(vb, 0, size);

	if (frame->cacheable)
		flite->vb2->cache_flush(vb, 1);

	return 0;
}

/* The color format (nr_comp, num_planes) must be already configured. */
int flite_prepare_addr(struct flite_dev *flite, struct vb2_buffer *vb,
		     struct flite_frame *frame, struct flite_addr *addr)
{
	if (IS_ERR(vb) || IS_ERR(frame)) {
		flite_err("Invalid argument");
		return -EINVAL;
	}

	addr->y = flite->vb2->plane_addr(vb, 0);

	flite_dbg("ADDR: y= 0x%X", addr->y);

	return 0;
}


static void flite_buf_queue(struct vb2_buffer *vb)
{
	struct flite_buffer *buf = container_of(vb, struct flite_buffer, vb);
	struct flite_dev *flite = vb2_get_drv_priv(vb->vb2_queue);
	int min_bufs;
	unsigned long flags;

	spin_lock_irqsave(&flite->slock, flags);
	flite_prepare_addr(flite, &buf->vb, &flite->d_frame, &buf->paddr);

	min_bufs = flite->reqbufs_cnt > 1 ? 2 : 1;

	if (flite->active_buf_cnt < FLITE_MAX_OUT_BUFS) {
		active_queue_add(flite, buf);
		flite_hw_set_output_addr(flite, &buf->paddr, vb->v4l2_buf.index);
	} else {
		pending_queue_add(flite, buf);
	}

	if (vb2_is_streaming(&flite->vbq) &&
		(flite->pending_buf_cnt >= min_bufs) &&
		!test_bit(FLITE_ST_STREAM, &flite->state)) {
		if (!test_and_set_bit(FLITE_ST_PIPE_STREAM, &flite->state)) {
			spin_unlock_irqrestore(&flite->slock, flags);
			flite_pipeline_s_stream(flite, 1);
			return;
		}

		if (!test_bit(FLITE_ST_STREAM, &flite->state)) {
			flite_info("G-Scaler h/w enable control");
			flite_hw_set_capture_start(flite);
			set_bit(FLITE_ST_STREAM, &flite->state);
		}
	}
	spin_unlock_irqrestore(&flite->slock, flags);

	return;
}

static struct vb2_ops flite_qops = {
	.queue_setup		= flite_queue_setup,
	.buf_prepare		= flite_buf_prepare,
	.buf_queue		= flite_buf_queue,
	.wait_prepare		= flite_unlock,
	.wait_finish		= flite_lock,
	.start_streaming	= flite_start_streaming,
	.stop_streaming		= flite_stop_streaming,
};

/*
 * The video node ioctl operations
 */
static int flite_vidioc_querycap(struct file *file, void *priv,
				       struct v4l2_capability *cap)
{
	struct flite_dev *flite = video_drvdata(file);

	strncpy(cap->driver, flite->pdev->name, sizeof(cap->driver) - 1);
	strncpy(cap->card, flite->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->capabilities = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE_MPLANE;

	return 0;
}

static int flite_enum_fmt_mplane(struct file *file, void *priv,
					struct v4l2_fmtdesc *f)
{
	struct flite_fmt *fmt;

	fmt = find_format(NULL, NULL, f->index);
	if (!fmt)
		return -EINVAL;

	strncpy(f->description, fmt->name, sizeof(f->description) - 1);
	f->pixelformat = fmt->pixelformat;

	return 0;
}

static int flite_try_fmt_mplane(struct file *file, void *fh, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct flite_fmt *fmt;
	u32 max_w, max_h, mod_x, mod_y;
	u32 min_w, min_h, tmp_w, tmp_h;
	int i;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	flite_dbg("user put w: %d, h: %d", pix_mp->width, pix_mp->height);

	fmt = find_format(&pix_mp->pixelformat, NULL, 0);
	if (!fmt) {
		flite_err("pixelformat format (0x%X) invalid\n", pix_mp->pixelformat);
		return -EINVAL;
	}

	max_w = variant.max_w;
	max_h = variant.max_h;
	min_w = min_h = mod_y = 0;

	if (fmt->is_yuv)
		mod_x = ffs(variant.align_out_w / 2) - 1;
	else
		mod_x = ffs(variant.align_out_w) - 1;

	flite_dbg("mod_x: %d, mod_y: %d, max_w: %d, max_h = %d",
	     mod_x, mod_y, max_w, max_h);
	/* To check if image size is modified to adjust parameter against
	   hardware abilities */
	tmp_w = pix_mp->width;
	tmp_h = pix_mp->height;

	v4l_bound_align_image(&pix_mp->width, min_w, max_w, mod_x,
		&pix_mp->height, min_h, max_h, mod_y, 0);
	if (tmp_w != pix_mp->width || tmp_h != pix_mp->height)
		flite_info("Image size has been modified from %dx%d to %dx%d",
			 tmp_w, tmp_h, pix_mp->width, pix_mp->height);

	pix_mp->num_planes = 1;

	for (i = 0; i < pix_mp->num_planes; ++i) {
		int bpl = (pix_mp->width * fmt->depth[i]) >> 3;
		pix_mp->plane_fmt[i].bytesperline = bpl;
		pix_mp->plane_fmt[i].sizeimage = bpl * pix_mp->height;

		flite_dbg("[%d]: bpl: %d, sizeimage: %d",
		    i, bpl, pix_mp->plane_fmt[i].sizeimage);
	}

	return 0;
}

void flite_set_frame_size(struct flite_frame *frame, int width, int height)
{
	frame->o_width	= width;
	frame->o_height	= height;
	frame->width = width;
	frame->height = height;
	frame->offs_h = 0;
	frame->offs_v = 0;
}

static int flite_s_fmt_mplane(struct file *file, void *fh, struct v4l2_format *f)
{
	struct flite_dev *flite = video_drvdata(file);
	struct flite_frame *frame;
	struct v4l2_pix_format_mplane *pix;
	int ret = 0;

	ret = flite_try_fmt_mplane(file, fh, f);
	if (ret)
		return ret;

	if (vb2_is_streaming(&flite->vbq)) {
		flite_err("queue (%d) busy", f->type);
		return -EBUSY;
	}

	frame = &flite->d_frame;

	pix = &f->fmt.pix_mp;
	frame->fmt = find_format(&pix->pixelformat, NULL, 0);
	if (!frame->fmt)
		return -EINVAL;

	frame->payload = pix->plane_fmt[0].bytesperline * pix->height;
	flite_set_frame_size(frame, pix->width, pix->height);

	flite_info("f_w: %d, f_h: %d", frame->o_width, frame->o_height);

	return 0;
}

static int flite_g_fmt_mplane(struct file *file, void *fh, struct v4l2_format *f)
{
	struct flite_dev *flite = video_drvdata(file);
	struct flite_frame *frame;
	struct v4l2_pix_format_mplane *pix_mp;
	int i;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	frame = &flite->d_frame;

	if (IS_ERR(frame))
		return PTR_ERR(frame);

	pix_mp = &f->fmt.pix_mp;

	pix_mp->width		= frame->o_width;
	pix_mp->height		= frame->o_height;
	pix_mp->field		= V4L2_FIELD_NONE;
	pix_mp->pixelformat	= frame->fmt->pixelformat;
	pix_mp->colorspace	= V4L2_COLORSPACE_JPEG;
	pix_mp->num_planes	= 1;

	for (i = 0; i < pix_mp->num_planes; ++i) {
		pix_mp->plane_fmt[i].bytesperline = (frame->o_width *
			frame->fmt->depth[i]) / 8;
		pix_mp->plane_fmt[i].sizeimage = pix_mp->plane_fmt[i].bytesperline *
			frame->o_height;
	}

	return 0;
}

static int flite_reqbufs(struct file *file, void *priv,
			    struct v4l2_requestbuffers *reqbufs)
{
	struct flite_dev *flite = video_drvdata(file);
	struct flite_frame *frame;
	int ret;

	frame = &flite->d_frame;
	frame->cacheable = flite->flite_ctrls.cacheable->val;
	flite->vb2->set_cacheable(flite->alloc_ctx, frame->cacheable);

	ret = vb2_reqbufs(&flite->vbq, reqbufs);
	if (!ret)
		flite->reqbufs_cnt = reqbufs->count;

	return ret;
}

static int flite_querybuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct flite_dev *flite = video_drvdata(file);

	return vb2_querybuf(&flite->vbq, buf);
}

static int flite_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct flite_dev *flite = video_drvdata(file);

	return vb2_qbuf(&flite->vbq, buf);
}

static int flite_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct flite_dev *flite = video_drvdata(file);

	return vb2_dqbuf(&flite->vbq, buf, file->f_flags & O_NONBLOCK);
}

static int flite_link_validate(struct flite_dev *flite)
{
	struct v4l2_subdev_format sink_fmt, src_fmt;
	struct v4l2_subdev *sd;
	struct media_pad *pad;
	int ret;

	/* Get the source pad connected with flite-video */
	pad =  media_entity_remote_source(&flite->vd_pad);
	if (pad == NULL)
		return -EPIPE;
	/* Get the subdev of source pad */
	sd = media_entity_to_v4l2_subdev(pad->entity);

	while (1) {
		/* Find sink pad of the subdev*/
		pad = &sd->entity.pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;
		if (sd == flite->sd_flite) {
			struct flite_frame *f = &flite->s_frame;
			sink_fmt.format.width = f->o_width;
			sink_fmt.format.height = f->o_height;
			sink_fmt.format.code = f->fmt ? f->fmt->code : 0;
		} else {
			sink_fmt.pad = pad->index;
			sink_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
			ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &sink_fmt);
			if (ret < 0 && ret != -ENOIOCTLCMD) {
				flite_err("failed %s subdev get_fmt", sd->name);
				return -EPIPE;
			}
		}
		flite_info("sink sd name : %s", sd->name);
		/* Get the source pad connected with remote sink pad */
		pad = media_entity_remote_source(pad);
		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		/* Get the subdev of source pad */
		sd = media_entity_to_v4l2_subdev(pad->entity);
		flite_info("source sd name : %s", sd->name);

		src_fmt.pad = pad->index;
		src_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &src_fmt);
		if (ret < 0 && ret != -ENOIOCTLCMD) {
			flite_err("failed %s subdev get_fmt", sd->name);
			return -EPIPE;
		}

		flite_info("src_width : %d, src_height : %d, src_code : %d",
			src_fmt.format.width, src_fmt.format.height,
			src_fmt.format.code);
		flite_info("sink_width : %d, sink_height : %d, sink_code : %d",
			sink_fmt.format.width, sink_fmt.format.height,
			sink_fmt.format.code);

		if (src_fmt.format.width != sink_fmt.format.width ||
		    src_fmt.format.height != sink_fmt.format.height ||
		    src_fmt.format.code != sink_fmt.format.code) {
			flite_err("mismatch sink and source");
			return -EPIPE;
		}
	}

	return 0;
}
static int flite_streamon(struct file *file, void *priv, enum v4l2_buf_type type)
{
	struct flite_dev *flite = video_drvdata(file);
	struct flite_pipeline *p = &flite->pipeline;
	int ret;

	if (flite_active(flite))
		return -EBUSY;

	if (p->sensor) {
		media_entity_pipeline_start(&p->sensor->entity, p->pipe);
	} else {
		flite_err("Error pipeline");
		return -EPIPE;
	}

	ret = flite_link_validate(flite);
	if (ret)
		return ret;

	return vb2_streamon(&flite->vbq, type);
}

static int flite_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct flite_dev *flite = video_drvdata(file);
	struct v4l2_subdev *sd = flite->pipeline.sensor;
	int ret;

	ret = vb2_streamoff(&flite->vbq, type);
	if (ret == 0)
		media_entity_pipeline_stop(&sd->entity);
	return ret;
}

static int flite_enum_input(struct file *file, void *priv,
			       struct v4l2_input *i)
{
	struct flite_dev *flite = video_drvdata(file);
	struct exynos_platform_flite *pdata = flite->pdata;
	struct exynos_isp_info *isp_info;

	if (i->index >= MAX_CAMIF_CLIENTS)
		return -EINVAL;

	isp_info = pdata->isp_info[i->index];
	if (isp_info == NULL)
		return -EINVAL;

	i->type = V4L2_INPUT_TYPE_CAMERA;

	strncpy(i->name, isp_info->board_info->type, 32);

	return 0;

}

static int flite_s_input(struct file *file, void *priv, unsigned int i)
{
	return i == 0 ? 0 : -EINVAL;
}

static int flite_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}


static const struct v4l2_ioctl_ops flite_capture_ioctl_ops = {
	.vidioc_querycap		= flite_vidioc_querycap,

	.vidioc_enum_fmt_vid_cap_mplane	= flite_enum_fmt_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= flite_try_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= flite_s_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= flite_g_fmt_mplane,

	.vidioc_reqbufs			= flite_reqbufs,
	.vidioc_querybuf		= flite_querybuf,

	.vidioc_qbuf			= flite_qbuf,
	.vidioc_dqbuf			= flite_dqbuf,

	.vidioc_streamon		= flite_streamon,
	.vidioc_streamoff		= flite_streamoff,

	.vidioc_enum_input		= flite_enum_input,
	.vidioc_s_input			= flite_s_input,
	.vidioc_g_input			= flite_g_input,
};

static const struct v4l2_file_operations flite_fops = {
	.owner		= THIS_MODULE,
	.open		= flite_open,
	.release	= flite_close,
	.poll		= flite_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= flite_mmap,
};

static int flite_config_camclk(struct flite_dev *flite,
		struct exynos_isp_info *isp_info, int i)
{
	struct clk *camclk;
	struct clk *srclk;

	camclk = clk_get(&flite->pdev->dev, isp_info->cam_clk_name);
	if (IS_ERR_OR_NULL(camclk)) {
		flite_err("failed to get cam clk");
		return -ENXIO;
	}
	flite->sensor[i].camclk = camclk;

	srclk = clk_get(&flite->pdev->dev, isp_info->cam_srclk_name);
	if (IS_ERR_OR_NULL(srclk)) {
		clk_put(camclk);
		flite_err("failed to get cam source clk\n");
		return -ENXIO;
	}
	clk_set_parent(camclk, srclk);
	clk_set_rate(camclk, isp_info->clk_frequency);
	clk_put(srclk);

	flite->gsc_clk = clk_get(&flite->pdev->dev, "gscl");
	if (IS_ERR_OR_NULL(flite->gsc_clk)) {
		flite_err("failed to get gscl clk");
		return -ENXIO;
	}

	return 0;
}

static struct v4l2_subdev *flite_register_sensor(struct flite_dev *flite,
		int i)
{
	struct exynos_platform_flite *pdata = flite->pdata;
	struct exynos_isp_info *isp_info = pdata->isp_info[i];
	struct exynos_md *mdev = flite->mdev;
	struct i2c_adapter *adapter;
	struct v4l2_subdev *sd = NULL;

	adapter = i2c_get_adapter(isp_info->i2c_bus_num);
	if (!adapter)
		return NULL;
	sd = v4l2_i2c_new_subdev_board(&mdev->v4l2_dev, adapter,
				       isp_info->board_info, NULL);
	if (IS_ERR_OR_NULL(sd)) {
		v4l2_err(&mdev->v4l2_dev, "Failed to acquire subdev\n");
		return NULL;
	}
	v4l2_set_subdev_hostdata(sd, &flite->sensor[i]);
	sd->grp_id = SENSOR_GRP_ID;

	v4l2_info(&mdev->v4l2_dev, "Registered sensor subdevice %s\n",
		  isp_info->board_info->type);

	return sd;
}

static int flite_register_sensor_entities(struct flite_dev *flite)
{
	struct exynos_platform_flite *pdata = flite->pdata;
	u32 num_clients = pdata->num_clients;
	int i;

	for (i = 0; i < num_clients; i++) {
		flite->sensor[i].pdata = pdata->isp_info[i];
		flite->sensor[i].sd = flite_register_sensor(flite, i);
		if (IS_ERR_OR_NULL(flite->sensor[i].sd)) {
			flite_err("failed to get register sensor");
			return -EINVAL;
		}
		flite->mdev->sensor_sd[i] = flite->sensor[i].sd;
	}

	return 0;
}

static int flite_create_subdev(struct flite_dev *flite, struct v4l2_subdev *sd)
{
	struct v4l2_device *v4l2_dev;
	int ret;

	sd->flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	flite->pads[FLITE_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	flite->pads[FLITE_PAD_SOURCE_PREV].flags = MEDIA_PAD_FL_SOURCE;
	flite->pads[FLITE_PAD_SOURCE_CAMCORD].flags = MEDIA_PAD_FL_SOURCE;
	flite->pads[FLITE_PAD_SOURCE_MEM].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_init(&sd->entity, FLITE_PADS_NUM,
				flite->pads, 0);
	if (ret)
		goto err_ent;

	sd->internal_ops = &flite_v4l2_internal_ops;
	sd->entity.ops = &flite_media_ops;
	sd->grp_id = FLITE_GRP_ID;
	v4l2_dev = &flite->mdev->v4l2_dev;
	flite->mdev->flite_sd[flite->id] = sd;

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret)
		goto err_sub;

	flite_init_formats(sd, NULL);

	return 0;

err_sub:
	media_entity_cleanup(&sd->entity);
err_ent:
	return ret;
}

static int flite_create_link(struct flite_dev *flite)
{
	struct media_entity *source, *sink;
	struct exynos_platform_flite *pdata = flite->pdata;
	struct exynos_isp_info *isp_info;
	u32 num_clients = pdata->num_clients;
	int ret, i;
	enum cam_port id;

	/* FIMC-LITE-SUBDEV ------> FIMC-LITE-VIDEO (Always link enable) */
	source = &flite->sd_flite->entity;
	sink = &flite->vfd->entity;
	if (source && sink) {
		ret = media_entity_create_link(source, FLITE_PAD_SOURCE_MEM, sink,
				0, 0);
		if (ret) {
			flite_err("failed link flite-subdev to flite-video\n");
			return ret;
		}
	}
	/* link sensor to mipi-csis */
	for (i = 0; i < num_clients; i++) {
		isp_info = pdata->isp_info[i];
		id = isp_info->cam_port;
		switch (isp_info->bus_type) {
		case CAM_TYPE_ITU:
			/*	SENSOR ------> FIMC-LITE	*/
			source = &flite->sensor[i].sd->entity;
			sink = &flite->sd_flite->entity;
			if (source && sink) {
				ret = media_entity_create_link(source, 0,
					      sink, FLITE_PAD_SINK, 0);
				if (ret) {
					flite_err("failed link sensor to flite\n");
					return ret;
				}
			}
			break;
		case CAM_TYPE_MIPI:
			/*	SENSOR ------> MIPI-CSI2	*/
			source = &flite->sensor[i].sd->entity;
			sink = &flite->sd_csis->entity;
			if (source && sink) {
				ret = media_entity_create_link(source, 0,
					      sink, CSIS_PAD_SINK, 0);
				if (ret) {
					flite_err("failed link sensor to csis\n");
					return ret;
				}
			}
			/*	MIPI-CSI2 ------> FIMC-LITE	*/
			source = &flite->sd_csis->entity;
			sink = &flite->sd_flite->entity;
			if (source && sink) {
				ret = media_entity_create_link(source,
						CSIS_PAD_SOURCE,
						sink, FLITE_PAD_SINK, 0);
				if (ret) {
					flite_err("failed link csis to flite\n");
					return ret;
				}
			}
			break;
		}
	}

	flite->input = FLITE_INPUT_NONE;
	flite->output = FLITE_OUTPUT_NONE;

	return 0;
}
static int flite_register_video_device(struct flite_dev *flite)
{
	struct video_device *vfd;
	struct vb2_queue *q;
	int ret = -ENOMEM;

	vfd = video_device_alloc();
	if (!vfd) {
		printk("Failed to allocate video device\n");
		return ret;
	}

	snprintf(vfd->name, sizeof(vfd->name), "%s", dev_name(&flite->pdev->dev));

	vfd->fops	= &flite_fops;
	vfd->ioctl_ops	= &flite_capture_ioctl_ops;
	vfd->v4l2_dev	= &flite->mdev->v4l2_dev;
	vfd->minor	= -1;
	vfd->release	= video_device_release;
	vfd->lock	= &flite->lock;
	video_set_drvdata(vfd, flite);

	flite->vfd = vfd;
	flite->refcnt = 0;
	flite->reqbufs_cnt  = 0;
	INIT_LIST_HEAD(&flite->active_buf_q);
	INIT_LIST_HEAD(&flite->pending_buf_q);

	q = &flite->vbq;
	memset(q, 0, sizeof(*q));
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = flite;
	q->ops = &flite_qops;
	q->mem_ops = flite->vb2->ops;

	vb2_queue_init(q);

	ret = video_register_device(vfd, VFL_TYPE_GRABBER,
				    EXYNOS_VIDEONODE_FLITE(flite->id));
	if (ret) {
		flite_err("failed to register video device");
		goto err_vfd_alloc;
	}

	flite->vd_pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&vfd->entity, 1, &flite->vd_pad, 0);
	if (ret) {
		flite_err("failed to initialize entity");
		goto err_unreg_video;
	}

	vfd->ctrl_handler = &flite->ctrl_handler;
	flite_dbg("flite video-device driver registered as /dev/video%d", vfd->num);

	return 0;

err_unreg_video:
	video_unregister_device(vfd);
err_vfd_alloc:
	video_device_release(vfd);

	return ret;
}

static int flite_get_md_callback(struct device *dev, void *p)
{
	struct exynos_md **md_list = p;
	struct exynos_md *md = NULL;

	md = dev_get_drvdata(dev);

	if (md)
		*(md_list + md->id) = md;

	return 0; /* non-zero value stops iteration */
}

static struct exynos_md *flite_get_capture_md(enum mdev_node node)
{
	struct device_driver *drv;
	struct exynos_md *md[MDEV_MAX_NUM] = {NULL,};
	int ret;

	drv = driver_find(MDEV_MODULE_NAME, &platform_bus_type);
	if (!drv)
		return ERR_PTR(-ENODEV);

	ret = driver_for_each_device(drv, NULL, &md[0],
				     flite_get_md_callback);
	put_driver(drv);

	return ret ? NULL : md[node];

}

static void flite_destroy_subdev(struct flite_dev *flite)
{
	struct v4l2_subdev *sd = flite->sd_flite;

	if (!sd)
		return;
	media_entity_cleanup(&sd->entity);
	v4l2_device_unregister_subdev(sd);
	kfree(sd);
	sd = NULL;
}

void flite_unregister_device(struct flite_dev *flite)
{
	struct video_device *vfd = flite->vfd;

	if (vfd) {
		media_entity_cleanup(&vfd->entity);
		/* Can also be called if video device was
		   not registered */
		video_unregister_device(vfd);
	}
	flite_destroy_subdev(flite);
}
#endif

static int flite_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct flite_dev *flite = v4l2_get_subdevdata(sd);

	if (test_bit(FLITE_ST_STREAM, &flite->state))
		flite_s_stream(sd, false);
	if (test_bit(FLITE_ST_POWER, &flite->state))
		flite_s_power(sd, false);

	set_bit(FLITE_ST_SUSPEND, &flite->state);

	return 0;
}

static int flite_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct flite_dev *flite = v4l2_get_subdevdata(sd);

	if (test_bit(FLITE_ST_POWER, &flite->state))
		flite_s_power(sd, true);
	if (test_bit(FLITE_ST_STREAM, &flite->state))
		flite_s_stream(sd, true);

	clear_bit(FLITE_ST_SUSPEND, &flite->state);

	return 0;
}

static int flite_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct flite_dev *flite = v4l2_get_subdevdata(sd);
	unsigned long flags;

#if defined(CONFIG_MEDIA_CONTROLLER) && defined(CONFIG_ARCH_EXYNOS5)
	flite->vb2->suspend(flite->alloc_ctx);
	clk_disable(flite->camif_clk);
#endif
	spin_lock_irqsave(&flite->slock, flags);
	set_bit(FLITE_ST_SUSPEND, &flite->state);
	spin_unlock_irqrestore(&flite->slock, flags);

	return 0;
}

static int flite_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct flite_dev *flite = v4l2_get_subdevdata(sd);
	unsigned long flags;

#if defined(CONFIG_MEDIA_CONTROLLER) && defined(CONFIG_ARCH_EXYNOS5)
	clk_enable(flite->camif_clk);
	flite->vb2->resume(flite->alloc_ctx);
#endif
	spin_lock_irqsave(&flite->slock, flags);
	clear_bit(FLITE_ST_SUSPEND, &flite->state);
	spin_unlock_irqrestore(&flite->slock, flags);

	return 0;
}

static struct v4l2_subdev_core_ops flite_core_ops = {
	.s_power = flite_s_power,
};

static struct v4l2_subdev_video_ops flite_video_ops = {
	.g_mbus_fmt	= flite_g_mbus_fmt,
	.s_mbus_fmt	= flite_s_mbus_fmt,
	.s_stream	= flite_s_stream,
	.cropcap	= flite_subdev_cropcap,
	.g_crop		= flite_subdev_g_crop,
	.s_crop		= flite_subdev_s_crop,
};

static struct v4l2_subdev_ops flite_subdev_ops = {
	.core	= &flite_core_ops,
#if defined(CONFIG_MEDIA_CONTROLLER) && defined(CONFIG_ARCH_EXYNOS5)
	.pad	= &flite_pad_ops,
#endif
	.video	= &flite_video_ops,
};

static int flite_probe(struct platform_device *pdev)
{
	struct resource *mem_res;
	struct resource *regs_res;
	struct flite_dev *flite;
	struct v4l2_subdev *sd;
	int ret = -ENODEV;
#if defined(CONFIG_MEDIA_CONTROLLER) && defined(CONFIG_ARCH_EXYNOS5)
	struct exynos_isp_info *isp_info;
	int i;
#endif
	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "platform data is NULL\n");
		return -EINVAL;
	}

	flite = kzalloc(sizeof(struct flite_dev), GFP_KERNEL);
	if (!flite)
		return -ENOMEM;

	flite->pdev = pdev;
	flite->pdata = pdev->dev.platform_data;

	flite->id = pdev->id;

	init_waitqueue_head(&flite->irq_queue);
	spin_lock_init(&flite->slock);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "Failed to get io memory region\n");
		goto err_flite;
	}

	regs_res = request_mem_region(mem_res->start, resource_size(mem_res),
				      pdev->name);
	if (!regs_res) {
		dev_err(&pdev->dev, "Failed to request io memory region\n");
		goto err_resource;
	}

	flite->regs_res = regs_res;
	flite->regs = ioremap(mem_res->start, resource_size(mem_res));
	if (!flite->regs) {
		dev_err(&pdev->dev, "Failed to remap io region\n");
		goto err_reg_region;
	}

	flite->irq = platform_get_irq(pdev, 0);
	if (flite->irq < 0) {
		dev_err(&pdev->dev, "Failed to get irq\n");
		goto err_reg_unmap;
	}

	ret = request_irq(flite->irq, flite_irq_handler, 0, dev_name(&pdev->dev), flite);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed\n");
		goto err_reg_unmap;
	}

	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd)
	       goto err_irq;
	v4l2_subdev_init(sd, &flite_subdev_ops);
	snprintf(sd->name, sizeof(sd->name), "flite-subdev.%d", flite->id);

	flite->sd_flite = sd;
	v4l2_set_subdevdata(flite->sd_flite, flite);
	if (soc_is_exynos4212() || soc_is_exynos4412())
		v4l2_set_subdev_hostdata(sd, pdev);
#if defined(CONFIG_MEDIA_CONTROLLER) && defined(CONFIG_ARCH_EXYNOS5)
#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
	flite->vb2 = &flite_vb2_cma;
#elif defined(CONFIG_VIDEOBUF2_ION)
	flite->vb2 = &flite_vb2_ion;
#endif
	mutex_init(&flite->lock);
	flite->mdev = flite_get_capture_md(MDEV_CAPTURE);
	if (IS_ERR_OR_NULL(flite->mdev))
		goto err_irq;

	flite_dbg("mdev = 0x%08x", (u32)flite->mdev);

	ret = flite_register_video_device(flite);
	if (ret)
		goto err_irq;

	/* Get mipi-csis subdev ptr using mdev */
	flite->sd_csis = flite->mdev->csis_sd[flite->id];

	for (i = 0; i < flite->pdata->num_clients; i++) {
		isp_info = flite->pdata->isp_info[i];
		ret = flite_config_camclk(flite, isp_info, i);
		if (ret) {
			flite_err("failed setup cam clk");
			goto err_vfd_alloc;
		}
	}

	ret = flite_register_sensor_entities(flite);
	if (ret) {
		flite_err("failed register sensor entities");
		goto err_clk;
	}

	ret = flite_create_subdev(flite, sd);
	if (ret) {
		flite_err("failed create subdev");
		goto err_clk;
	}

	ret = flite_create_link(flite);
	if (ret) {
		flite_err("failed create link");
		goto err_entity;
	}

	flite->alloc_ctx = flite->vb2->init(flite);
	if (IS_ERR(flite->alloc_ctx)) {
		ret = PTR_ERR(flite->alloc_ctx);
		goto err_entity;
	}

	flite->camif_clk = clk_get(&flite->pdev->dev, CAMIF_TOP_CLK);
	if (IS_ERR(flite->camif_clk)) {
		flite_err("failed to get flite.%d clock", flite->id);
		goto err_entity;
	}
	flite->mdev->is_flite_on= false;
#endif
	platform_set_drvdata(flite->pdev, flite->sd_flite);
	pm_runtime_enable(&pdev->dev);

	flite_info("FIMC-LITE%d probe success", pdev->id);

	return 0;

#if defined(CONFIG_MEDIA_CONTROLLER) && defined(CONFIG_ARCH_EXYNOS5)
err_entity:
	media_entity_cleanup(&sd->entity);
err_clk:
	for (i = 0; i < flite->pdata->num_clients; i++)
		clk_put(flite->sensor[i].camclk);
err_vfd_alloc:
	media_entity_cleanup(&flite->vfd->entity);
	video_device_release(flite->vfd);
#endif
err_irq:
	free_irq(flite->irq, flite);
err_reg_unmap:
	iounmap(flite->regs);
err_reg_region:
	release_mem_region(regs_res->start, resource_size(regs_res));
err_resource:
	release_resource(flite->regs_res);
	kfree(flite->regs_res);
err_flite:
	kfree(flite);
	return ret;
}

static int flite_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct flite_dev *flite = v4l2_get_subdevdata(sd);
	struct resource *res = flite->regs_res;

	flite_s_power(flite->sd_flite, 0);
#if defined(CONFIG_MEDIA_CONTROLLER) && defined(CONFIG_ARCH_EXYNOS5)
	flite_subdev_close(sd, NULL);
	flite_unregister_device(flite);
	flite->vb2->cleanup(flite->alloc_ctx);
#endif
	pm_runtime_disable(&pdev->dev);
	free_irq(flite->irq, flite);
	iounmap(flite->regs);
	release_mem_region(res->start, resource_size(res));
	kfree(flite);

	return 0;
}


static const struct dev_pm_ops flite_pm_ops = {
	.suspend		= flite_suspend,
	.resume			= flite_resume,
	.runtime_suspend	= flite_runtime_suspend,
	.runtime_resume		= flite_runtime_resume,
};

static struct platform_driver flite_driver = {
	.probe		= flite_probe,
	.remove	= __devexit_p(flite_remove),
	.driver = {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
		.pm	= &flite_pm_ops,
	}
};

static int __init flite_init(void)
{
	int ret = platform_driver_register(&flite_driver);
	if (ret)
		flite_err("platform_driver_register failed: %d", ret);
	return ret;
}

static void __exit flite_exit(void)
{
	platform_driver_unregister(&flite_driver);
}
module_init(flite_init);
module_exit(flite_exit);

MODULE_AUTHOR("Sky Kang<sungchun.kang@samsung.com>");
MODULE_DESCRIPTION("Exynos FIMC-Lite driver");
MODULE_LICENSE("GPL");
