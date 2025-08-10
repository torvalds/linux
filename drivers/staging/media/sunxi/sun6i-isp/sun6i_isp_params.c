// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-v4l2.h>

#include "sun6i_isp.h"
#include "sun6i_isp_params.h"
#include "sun6i_isp_reg.h"
#include "uapi/sun6i-isp-config.h"

/* Params */

static const struct sun6i_isp_params_config sun6i_isp_params_config_default = {
	.modules_used = SUN6I_ISP_MODULE_BAYER,

	.bayer = {
		.offset_r	= 32,
		.offset_gr	= 32,
		.offset_gb	= 32,
		.offset_b	= 32,

		.gain_r		= 256,
		.gain_gr	= 256,
		.gain_gb	= 256,
		.gain_b		= 256,

	},

	.bdnf = {
		.in_dis_min		= 8,
		.in_dis_max		= 16,

		.coefficients_g		= { 15, 4, 1 },
		.coefficients_rb	= { 15, 4 },
	},
};

static void sun6i_isp_params_configure_ob(struct sun6i_isp_device *isp_dev)
{
	unsigned int width, height;

	sun6i_isp_proc_dimensions(isp_dev, &width, &height);

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_OB_SIZE_REG,
			     SUN6I_ISP_OB_SIZE_WIDTH(width) |
			     SUN6I_ISP_OB_SIZE_HEIGHT(height));

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_OB_VALID_REG,
			     SUN6I_ISP_OB_VALID_WIDTH(width) |
			     SUN6I_ISP_OB_VALID_HEIGHT(height));

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_OB_SRC0_VALID_START_REG,
			     SUN6I_ISP_OB_SRC0_VALID_START_HORZ(0) |
			     SUN6I_ISP_OB_SRC0_VALID_START_VERT(0));
}

static void sun6i_isp_params_configure_ae(struct sun6i_isp_device *isp_dev)
{
	/* These are default values that need to be set to get an output. */

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_AE_CFG_REG,
			     SUN6I_ISP_AE_CFG_LOW_BRI_TH(0xff) |
			     SUN6I_ISP_AE_CFG_HORZ_NUM(8) |
			     SUN6I_ISP_AE_CFG_HIGH_BRI_TH(0xf00) |
			     SUN6I_ISP_AE_CFG_VERT_NUM(8));
}

static void
sun6i_isp_params_configure_bayer(struct sun6i_isp_device *isp_dev,
				 const struct sun6i_isp_params_config *config)
{
	const struct sun6i_isp_params_config_bayer *bayer = &config->bayer;

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_BAYER_OFFSET0_REG,
			     SUN6I_ISP_BAYER_OFFSET0_R(bayer->offset_r) |
			     SUN6I_ISP_BAYER_OFFSET0_GR(bayer->offset_gr));

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_BAYER_OFFSET1_REG,
			     SUN6I_ISP_BAYER_OFFSET1_GB(bayer->offset_gb) |
			     SUN6I_ISP_BAYER_OFFSET1_B(bayer->offset_b));

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_BAYER_GAIN0_REG,
			     SUN6I_ISP_BAYER_GAIN0_R(bayer->gain_r) |
			     SUN6I_ISP_BAYER_GAIN0_GR(bayer->gain_gr));

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_BAYER_GAIN1_REG,
			     SUN6I_ISP_BAYER_GAIN1_GB(bayer->gain_gb) |
			     SUN6I_ISP_BAYER_GAIN1_B(bayer->gain_b));
}

static void sun6i_isp_params_configure_wb(struct sun6i_isp_device *isp_dev)
{
	/* These are default values that need to be set to get an output. */

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_WB_GAIN0_REG,
			     SUN6I_ISP_WB_GAIN0_R(256) |
			     SUN6I_ISP_WB_GAIN0_GR(256));

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_WB_GAIN1_REG,
			     SUN6I_ISP_WB_GAIN1_GB(256) |
			     SUN6I_ISP_WB_GAIN1_B(256));

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_WB_CFG_REG,
			     SUN6I_ISP_WB_CFG_CLIP(0xfff));
}

static void sun6i_isp_params_configure_base(struct sun6i_isp_device *isp_dev)
{
	sun6i_isp_params_configure_ae(isp_dev);
	sun6i_isp_params_configure_ob(isp_dev);
	sun6i_isp_params_configure_wb(isp_dev);
}

static void
sun6i_isp_params_configure_bdnf(struct sun6i_isp_device *isp_dev,
				const struct sun6i_isp_params_config *config)
{
	const struct sun6i_isp_params_config_bdnf *bdnf = &config->bdnf;

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_BDNF_CFG_REG,
			     SUN6I_ISP_BDNF_CFG_IN_DIS_MIN(bdnf->in_dis_min) |
			     SUN6I_ISP_BDNF_CFG_IN_DIS_MAX(bdnf->in_dis_max));

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_BDNF_COEF_RB_REG,
			     SUN6I_ISP_BDNF_COEF_RB(0, bdnf->coefficients_rb[0]) |
			     SUN6I_ISP_BDNF_COEF_RB(1, bdnf->coefficients_rb[1]) |
			     SUN6I_ISP_BDNF_COEF_RB(2, bdnf->coefficients_rb[2]) |
			     SUN6I_ISP_BDNF_COEF_RB(3, bdnf->coefficients_rb[3]) |
			     SUN6I_ISP_BDNF_COEF_RB(4, bdnf->coefficients_rb[4]));

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_BDNF_COEF_G_REG,
			     SUN6I_ISP_BDNF_COEF_G(0, bdnf->coefficients_g[0]) |
			     SUN6I_ISP_BDNF_COEF_G(1, bdnf->coefficients_g[1]) |
			     SUN6I_ISP_BDNF_COEF_G(2, bdnf->coefficients_g[2]) |
			     SUN6I_ISP_BDNF_COEF_G(3, bdnf->coefficients_g[3]) |
			     SUN6I_ISP_BDNF_COEF_G(4, bdnf->coefficients_g[4]) |
			     SUN6I_ISP_BDNF_COEF_G(5, bdnf->coefficients_g[5]) |
			     SUN6I_ISP_BDNF_COEF_G(6, bdnf->coefficients_g[6]));
}

static void
sun6i_isp_params_configure_modules(struct sun6i_isp_device *isp_dev,
				   const struct sun6i_isp_params_config *config)
{
	u32 value;

	if (config->modules_used & SUN6I_ISP_MODULE_BDNF)
		sun6i_isp_params_configure_bdnf(isp_dev, config);

	if (config->modules_used & SUN6I_ISP_MODULE_BAYER)
		sun6i_isp_params_configure_bayer(isp_dev, config);

	value = sun6i_isp_load_read(isp_dev, SUN6I_ISP_MODULE_EN_REG);
	/* Clear all modules but keep input configuration. */
	value &= SUN6I_ISP_MODULE_EN_SRC0 | SUN6I_ISP_MODULE_EN_SRC1;

	if (config->modules_used & SUN6I_ISP_MODULE_BDNF)
		value |= SUN6I_ISP_MODULE_EN_BDNF;

	/* Bayer stage is always enabled. */

	sun6i_isp_load_write(isp_dev, SUN6I_ISP_MODULE_EN_REG, value);
}

void sun6i_isp_params_configure(struct sun6i_isp_device *isp_dev)
{
	struct sun6i_isp_params_state *state = &isp_dev->params.state;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);

	sun6i_isp_params_configure_base(isp_dev);

	/* Default config is only applied at the very first stream start. */
	if (state->configured)
		goto complete;

	sun6i_isp_params_configure_modules(isp_dev,
					   &sun6i_isp_params_config_default);

	state->configured = true;

complete:
	spin_unlock_irqrestore(&state->lock, flags);
}

/* State */

static void sun6i_isp_params_state_cleanup(struct sun6i_isp_device *isp_dev,
					   bool error)
{
	struct sun6i_isp_params_state *state = &isp_dev->params.state;
	struct sun6i_isp_buffer *isp_buffer;
	struct vb2_buffer *vb2_buffer;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);

	if (state->pending) {
		vb2_buffer = &state->pending->v4l2_buffer.vb2_buf;
		vb2_buffer_done(vb2_buffer, error ? VB2_BUF_STATE_ERROR :
				VB2_BUF_STATE_QUEUED);

		state->pending = NULL;
	}

	list_for_each_entry(isp_buffer, &state->queue, list) {
		vb2_buffer = &isp_buffer->v4l2_buffer.vb2_buf;
		vb2_buffer_done(vb2_buffer, error ? VB2_BUF_STATE_ERROR :
				VB2_BUF_STATE_QUEUED);
	}

	INIT_LIST_HEAD(&state->queue);

	spin_unlock_irqrestore(&state->lock, flags);
}

void sun6i_isp_params_state_update(struct sun6i_isp_device *isp_dev,
				   bool *update)
{
	struct sun6i_isp_params_state *state = &isp_dev->params.state;
	struct sun6i_isp_buffer *isp_buffer;
	struct vb2_buffer *vb2_buffer;
	const struct sun6i_isp_params_config *config;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);

	if (list_empty(&state->queue))
		goto complete;

	if (state->pending)
		goto complete;

	isp_buffer = list_first_entry(&state->queue, struct sun6i_isp_buffer,
				      list);

	vb2_buffer = &isp_buffer->v4l2_buffer.vb2_buf;
	config = vb2_plane_vaddr(vb2_buffer, 0);

	sun6i_isp_params_configure_modules(isp_dev, config);

	list_del(&isp_buffer->list);

	state->pending = isp_buffer;

	if (update)
		*update = true;

complete:
	spin_unlock_irqrestore(&state->lock, flags);
}

void sun6i_isp_params_state_complete(struct sun6i_isp_device *isp_dev)
{
	struct sun6i_isp_params_state *state = &isp_dev->params.state;
	struct sun6i_isp_buffer *isp_buffer;
	struct vb2_buffer *vb2_buffer;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);

	if (!state->pending)
		goto complete;

	isp_buffer = state->pending;
	vb2_buffer = &isp_buffer->v4l2_buffer.vb2_buf;

	vb2_buffer->timestamp = ktime_get_ns();

	/* Parameters will be applied starting from the next frame. */
	isp_buffer->v4l2_buffer.sequence = isp_dev->capture.state.sequence + 1;

	vb2_buffer_done(vb2_buffer, VB2_BUF_STATE_DONE);

	state->pending = NULL;

complete:
	spin_unlock_irqrestore(&state->lock, flags);
}

/* Queue */

static int sun6i_isp_params_queue_setup(struct vb2_queue *queue,
					unsigned int *buffers_count,
					unsigned int *planes_count,
					unsigned int sizes[],
					struct device *alloc_devs[])
{
	struct sun6i_isp_device *isp_dev = vb2_get_drv_priv(queue);
	unsigned int size = isp_dev->params.format.fmt.meta.buffersize;

	if (*planes_count)
		return sizes[0] < size ? -EINVAL : 0;

	*planes_count = 1;
	sizes[0] = size;

	return 0;
}

static int sun6i_isp_params_buffer_prepare(struct vb2_buffer *vb2_buffer)
{
	struct sun6i_isp_device *isp_dev =
		vb2_get_drv_priv(vb2_buffer->vb2_queue);
	struct v4l2_device *v4l2_dev = &isp_dev->v4l2.v4l2_dev;
	unsigned int size = isp_dev->params.format.fmt.meta.buffersize;

	if (vb2_plane_size(vb2_buffer, 0) < size) {
		v4l2_err(v4l2_dev, "buffer too small (%lu < %u)\n",
			 vb2_plane_size(vb2_buffer, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb2_buffer, 0, size);

	return 0;
}

static void sun6i_isp_params_buffer_queue(struct vb2_buffer *vb2_buffer)
{
	struct sun6i_isp_device *isp_dev =
		vb2_get_drv_priv(vb2_buffer->vb2_queue);
	struct sun6i_isp_params_state *state = &isp_dev->params.state;
	struct vb2_v4l2_buffer *v4l2_buffer = to_vb2_v4l2_buffer(vb2_buffer);
	struct sun6i_isp_buffer *isp_buffer =
		container_of(v4l2_buffer, struct sun6i_isp_buffer, v4l2_buffer);
	bool capture_streaming = isp_dev->capture.state.streaming;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);
	list_add_tail(&isp_buffer->list, &state->queue);
	spin_unlock_irqrestore(&state->lock, flags);

	if (state->streaming && capture_streaming)
		sun6i_isp_state_update(isp_dev, false);
}

static int sun6i_isp_params_start_streaming(struct vb2_queue *queue,
					    unsigned int count)
{
	struct sun6i_isp_device *isp_dev = vb2_get_drv_priv(queue);
	struct sun6i_isp_params_state *state = &isp_dev->params.state;
	bool capture_streaming = isp_dev->capture.state.streaming;

	state->streaming = true;

	/*
	 * Update the state as soon as possible if capture is streaming,
	 * otherwise it will be applied when capture starts streaming.
	 */

	if (capture_streaming)
		sun6i_isp_state_update(isp_dev, false);

	return 0;
}

static void sun6i_isp_params_stop_streaming(struct vb2_queue *queue)
{
	struct sun6i_isp_device *isp_dev = vb2_get_drv_priv(queue);
	struct sun6i_isp_params_state *state = &isp_dev->params.state;

	state->streaming = false;
	sun6i_isp_params_state_cleanup(isp_dev, true);
}

static const struct vb2_ops sun6i_isp_params_queue_ops = {
	.queue_setup		= sun6i_isp_params_queue_setup,
	.buf_prepare		= sun6i_isp_params_buffer_prepare,
	.buf_queue		= sun6i_isp_params_buffer_queue,
	.start_streaming	= sun6i_isp_params_start_streaming,
	.stop_streaming		= sun6i_isp_params_stop_streaming,
};

/* Video Device */

static int sun6i_isp_params_querycap(struct file *file, void *priv,
				     struct v4l2_capability *capability)
{
	struct sun6i_isp_device *isp_dev = video_drvdata(file);
	struct video_device *video_dev = &isp_dev->params.video_dev;

	strscpy(capability->driver, SUN6I_ISP_NAME, sizeof(capability->driver));
	strscpy(capability->card, video_dev->name, sizeof(capability->card));
	snprintf(capability->bus_info, sizeof(capability->bus_info),
		 "platform:%s", dev_name(isp_dev->dev));

	return 0;
}

static int sun6i_isp_params_enum_fmt(struct file *file, void *priv,
				     struct v4l2_fmtdesc *fmtdesc)
{
	struct sun6i_isp_device *isp_dev = video_drvdata(file);
	struct v4l2_meta_format *params_format =
		&isp_dev->params.format.fmt.meta;

	if (fmtdesc->index > 0)
		return -EINVAL;

	fmtdesc->pixelformat = params_format->dataformat;

	return 0;
}

static int sun6i_isp_params_g_fmt(struct file *file, void *priv,
				  struct v4l2_format *format)
{
	struct sun6i_isp_device *isp_dev = video_drvdata(file);

	*format = isp_dev->params.format;

	return 0;
}

static const struct v4l2_ioctl_ops sun6i_isp_params_ioctl_ops = {
	.vidioc_querycap		= sun6i_isp_params_querycap,

	.vidioc_enum_fmt_meta_out	= sun6i_isp_params_enum_fmt,
	.vidioc_g_fmt_meta_out		= sun6i_isp_params_g_fmt,
	.vidioc_s_fmt_meta_out		= sun6i_isp_params_g_fmt,
	.vidioc_try_fmt_meta_out	= sun6i_isp_params_g_fmt,

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

static const struct v4l2_file_operations sun6i_isp_params_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.mmap		= vb2_fop_mmap,
	.poll		= vb2_fop_poll,
};

/* Params */

int sun6i_isp_params_setup(struct sun6i_isp_device *isp_dev)
{
	struct sun6i_isp_params *params = &isp_dev->params;
	struct sun6i_isp_params_state *state = &params->state;
	struct v4l2_device *v4l2_dev = &isp_dev->v4l2.v4l2_dev;
	struct v4l2_subdev *proc_subdev = &isp_dev->proc.subdev;
	struct video_device *video_dev = &params->video_dev;
	struct vb2_queue *queue = &isp_dev->params.queue;
	struct media_pad *pad = &isp_dev->params.pad;
	struct v4l2_format *format = &isp_dev->params.format;
	struct v4l2_meta_format *params_format = &format->fmt.meta;
	int ret;

	/* State */

	INIT_LIST_HEAD(&state->queue);
	spin_lock_init(&state->lock);

	/* Media Pads */

	pad->flags = MEDIA_PAD_FL_SOURCE | MEDIA_PAD_FL_MUST_CONNECT;

	ret = media_entity_pads_init(&video_dev->entity, 1, pad);
	if (ret)
		goto error_mutex;

	/* Queue */

	mutex_init(&params->lock);

	queue->type = V4L2_BUF_TYPE_META_OUTPUT;
	queue->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	queue->buf_struct_size = sizeof(struct sun6i_isp_buffer);
	queue->ops = &sun6i_isp_params_queue_ops;
	queue->mem_ops = &vb2_vmalloc_memops;
	queue->min_queued_buffers = 1;
	queue->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	queue->lock = &params->lock;
	queue->dev = isp_dev->dev;
	queue->drv_priv = isp_dev;

	ret = vb2_queue_init(queue);
	if (ret) {
		v4l2_err(v4l2_dev, "failed to initialize vb2 queue: %d\n", ret);
		goto error_media_entity;
	}

	/* V4L2 Format */

	format->type = queue->type;
	params_format->dataformat = V4L2_META_FMT_SUN6I_ISP_PARAMS;
	params_format->buffersize = sizeof(struct sun6i_isp_params_config);

	/* Video Device */

	strscpy(video_dev->name, SUN6I_ISP_PARAMS_NAME,
		sizeof(video_dev->name));
	video_dev->device_caps = V4L2_CAP_META_OUTPUT | V4L2_CAP_STREAMING;
	video_dev->vfl_dir = VFL_DIR_TX;
	video_dev->release = video_device_release_empty;
	video_dev->fops = &sun6i_isp_params_fops;
	video_dev->ioctl_ops = &sun6i_isp_params_ioctl_ops;
	video_dev->v4l2_dev = v4l2_dev;
	video_dev->queue = queue;
	video_dev->lock = &params->lock;

	video_set_drvdata(video_dev, isp_dev);

	ret = video_register_device(video_dev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(v4l2_dev, "failed to register video device: %d\n",
			 ret);
		goto error_media_entity;
	}

	/* Media Pad Link */

	ret = media_create_pad_link(&video_dev->entity, 0,
				    &proc_subdev->entity,
				    SUN6I_ISP_PROC_PAD_SINK_PARAMS,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "failed to create %s:%u -> %s:%u link\n",
			 video_dev->entity.name, 0, proc_subdev->entity.name,
			 SUN6I_ISP_PROC_PAD_SINK_PARAMS);
		goto error_video_device;
	}

	return 0;

error_video_device:
	vb2_video_unregister_device(video_dev);

error_media_entity:
	media_entity_cleanup(&video_dev->entity);

error_mutex:
	mutex_destroy(&params->lock);

	return ret;
}

void sun6i_isp_params_cleanup(struct sun6i_isp_device *isp_dev)
{
	struct sun6i_isp_params *params = &isp_dev->params;
	struct video_device *video_dev = &params->video_dev;

	vb2_video_unregister_device(video_dev);
	media_entity_cleanup(&video_dev->entity);
	mutex_destroy(&params->lock);
}
