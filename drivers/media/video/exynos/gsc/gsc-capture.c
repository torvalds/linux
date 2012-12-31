/* linux/drivers/media/video/exynos/gsc/gsc-capture.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung EXYNOS5 SoC series G-scaler driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/string.h>
#include <linux/i2c.h>
#include <media/v4l2-ioctl.h>
#include <media/exynos_gscaler.h>

#include "gsc-core.h"

static int gsc_capture_queue_setup(struct vb2_queue *vq, unsigned int *num_buffers,
		       unsigned int *num_planes, unsigned long sizes[],
		       void *allocators[])
{
	struct gsc_ctx *ctx = vq->drv_priv;
	struct gsc_fmt *fmt = ctx->d_frame.fmt;
	int i;

	if (!fmt)
		return -EINVAL;

	*num_planes = fmt->num_planes;

	for (i = 0; i < fmt->num_planes; i++) {
		sizes[i] = get_plane_size(&ctx->d_frame, i);
		allocators[i] = ctx->gsc_dev->alloc_ctx;
	}

	return 0;
}
static int gsc_capture_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct gsc_ctx *ctx = vq->drv_priv;
	struct gsc_dev *gsc = ctx->gsc_dev;
	struct gsc_frame *frame = &ctx->d_frame;
	int i;

	if (frame->fmt == NULL)
		return -EINVAL;

	for (i = 0; i < frame->fmt->num_planes; i++) {
		unsigned long size = frame->payload[i];

		if (vb2_plane_size(vb, i) < size) {
			v4l2_err(ctx->gsc_dev->cap.vfd,
				 "User buffer too small (%ld < %ld)\n",
				 vb2_plane_size(vb, i), size);
			return -EINVAL;
		}
		vb2_set_plane_payload(vb, i, size);
	}

	if (frame->cacheable)
		gsc->vb2->cache_flush(vb, frame->fmt->num_planes);

	return 0;
}

int gsc_cap_pipeline_s_stream(struct gsc_dev *gsc, int on)
{
	struct gsc_pipeline *p = &gsc->pipeline;
	int ret = 0;

	if ((!p->sensor || !p->flite) && (!p->disp))
		return -ENODEV;

	if (on) {
		ret = v4l2_subdev_call(p->sd_gsc, video, s_stream, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
		if (p->disp) {
			ret = v4l2_subdev_call(p->disp, video, s_stream, 1);
			if (ret < 0 && ret != -ENOIOCTLCMD)
				return ret;
		} else {
			ret = v4l2_subdev_call(p->flite, video, s_stream, 1);
			if (ret < 0 && ret != -ENOIOCTLCMD)
				return ret;
			ret = v4l2_subdev_call(p->csis, video, s_stream, 1);
			if (ret < 0 && ret != -ENOIOCTLCMD)
				return ret;
			ret = v4l2_subdev_call(p->sensor, video, s_stream, 1);
		}
	} else {
		ret = v4l2_subdev_call(p->sd_gsc, video, s_stream, 0);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
		if (p->disp) {
			ret = v4l2_subdev_call(p->disp, video, s_stream, 0);
			if (ret < 0 && ret != -ENOIOCTLCMD)
				return ret;
		} else {
			ret = v4l2_subdev_call(p->sensor, video, s_stream, 0);
			if (ret < 0 && ret != -ENOIOCTLCMD)
				return ret;
			ret = v4l2_subdev_call(p->csis, video, s_stream, 0);
			if (ret < 0 && ret != -ENOIOCTLCMD)
				return ret;
			ret = v4l2_subdev_call(p->flite, video, s_stream, 0);
		}
	}

	return ret == -ENOIOCTLCMD ? 0 : ret;
}

static int gsc_capture_set_addr(struct vb2_buffer *vb)
{
	struct gsc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct gsc_dev *gsc = ctx->gsc_dev;
	int ret;

	ret = gsc_prepare_addr(ctx, vb, &ctx->d_frame, &ctx->d_frame.addr);
	if (ret) {
		gsc_err("Prepare G-Scaler address failed\n");
		return -EINVAL;
	}

	gsc_hw_set_output_addr(gsc, &ctx->d_frame.addr, vb->v4l2_buf.index);

	return 0;
}

static void gsc_capture_buf_queue(struct vb2_buffer *vb)
{
	struct gsc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct gsc_dev *gsc = ctx->gsc_dev;
	struct gsc_capture_device *cap = &gsc->cap;
	struct exynos_md *mdev = gsc->mdev[MDEV_CAPTURE];
	int min_bufs, ret;
	unsigned long flags;

	spin_lock_irqsave(&gsc->slock, flags);
	ret = gsc_capture_set_addr(vb);
	if (ret)
		gsc_err("Failed to prepare output addr");

	if (!test_bit(ST_CAPT_SUSPENDED, &gsc->state)) {
		gsc_dbg("buf_index : %d", vb->v4l2_buf.index);
		gsc_hw_set_output_buf_masking(gsc, vb->v4l2_buf.index, 0);
	}

	min_bufs = cap->reqbufs_cnt > 1 ? 2 : 1;

	if (vb2_is_streaming(&cap->vbq) &&
		(gsc_hw_get_nr_unmask_bits(gsc) >= min_bufs) &&
		!test_bit(ST_CAPT_STREAM, &gsc->state)) {
		if (!test_and_set_bit(ST_CAPT_PIPE_STREAM, &gsc->state)) {
			spin_unlock_irqrestore(&gsc->slock, flags);
			if (!mdev->is_flite_on)
				gsc_cap_pipeline_s_stream(gsc, 1);
			else
				v4l2_subdev_call(gsc->cap.sd_cap, video,
							s_stream, 1);
			return;
		}

		if (!test_bit(ST_CAPT_STREAM, &gsc->state)) {
			gsc_info("G-Scaler h/w enable control");
			gsc_hw_enable_control(gsc, true);
			set_bit(ST_CAPT_STREAM, &gsc->state);
		}
	}
	spin_unlock_irqrestore(&gsc->slock, flags);

	return;
}

static int gsc_capture_get_scaler_factor(u32 src, u32 tar, u32 *ratio)
{
	u32 sh = 3;
	tar *= 4;
	if (tar >= src) {
		*ratio = 1;
		return 0;
	}

	while (--sh) {
		u32 tmp = 1 << sh;
		if (src >= tar * tmp)
			*ratio = sh;
	}
	return 0;
}

static int gsc_capture_scaler_info(struct gsc_ctx *ctx)
{
	struct gsc_frame *s_frame = &ctx->s_frame;
	struct gsc_frame *d_frame = &ctx->d_frame;
	struct gsc_scaler *sc = &ctx->scaler;

	gsc_capture_get_scaler_factor(s_frame->crop.width, d_frame->crop.width,
				      &sc->pre_hratio);
	gsc_capture_get_scaler_factor(s_frame->crop.height, d_frame->crop.width,
				      &sc->pre_vratio);

	sc->main_hratio = (s_frame->crop.width << 16) / d_frame->crop.width;
	sc->main_vratio = (s_frame->crop.height << 16) / d_frame->crop.height;

	gsc_info("src width : %d, src height : %d, dst width : %d,\
		dst height : %d", s_frame->crop.width, s_frame->crop.height,\
		d_frame->crop.width, d_frame->crop.height);
	gsc_info("pre_hratio : 0x%x, pre_vratio : 0x%x, main_hratio : 0x%lx,\
			main_vratio : 0x%lx", sc->pre_hratio,\
			sc->pre_vratio, sc->main_hratio, sc->main_vratio);

	return 0;
}

static int gsc_capture_subdev_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct gsc_dev *gsc = v4l2_get_subdevdata(sd);
	struct gsc_capture_device *cap = &gsc->cap;
	struct gsc_ctx *ctx = cap->ctx;

	gsc_info("");

	gsc_hw_set_frm_done_irq_mask(gsc, false);
	gsc_hw_set_overflow_irq_mask(gsc, false);
	gsc_hw_set_one_frm_mode(gsc, false);
	gsc_hw_set_gsc_irq_enable(gsc, true);

	if (gsc->pipeline.disp)
		gsc_hw_set_sysreg_writeback(ctx);
	else
		gsc_hw_set_sysreg_camif(true);

	gsc_hw_set_input_path(ctx);
	gsc_hw_set_in_size(ctx);
	gsc_hw_set_in_image_format(ctx);
	gsc_hw_set_output_path(ctx);
	gsc_hw_set_out_size(ctx);
	gsc_hw_set_out_image_format(ctx);
	gsc_hw_set_global_alpha(ctx);

	gsc_capture_scaler_info(ctx);
	gsc_hw_set_prescaler(ctx);
	gsc_hw_set_mainscaler(ctx);

	set_bit(ST_CAPT_PEND, &gsc->state);

	gsc_hw_enable_control(gsc, true);
	set_bit(ST_CAPT_STREAM, &gsc->state);

	return 0;
}

static int gsc_capture_start_streaming(struct vb2_queue *q)
{
	struct gsc_ctx *ctx = q->drv_priv;
	struct gsc_dev *gsc = ctx->gsc_dev;
	struct gsc_capture_device *cap = &gsc->cap;
	struct exynos_md *mdev = gsc->mdev[MDEV_CAPTURE];
	int min_bufs;

	gsc_hw_set_sw_reset(gsc);
	gsc_wait_reset(gsc);
	gsc_hw_set_output_buf_mask_all(gsc);

	min_bufs = cap->reqbufs_cnt > 1 ? 2 : 1;
	if ((gsc_hw_get_nr_unmask_bits(gsc) >= min_bufs) &&
		!test_bit(ST_CAPT_STREAM, &gsc->state)) {
		if (!test_and_set_bit(ST_CAPT_PIPE_STREAM, &gsc->state)) {
			gsc_info("");
			if (!mdev->is_flite_on)
				gsc_cap_pipeline_s_stream(gsc, 1);
			else
				v4l2_subdev_call(gsc->cap.sd_cap, video,
							s_stream, 1);
		}
	}

	return 0;
}

static int gsc_capture_state_cleanup(struct gsc_dev *gsc)
{
	struct exynos_md *mdev = gsc->mdev[MDEV_CAPTURE];
	unsigned long flags;
	bool streaming;

	spin_lock_irqsave(&gsc->slock, flags);
	streaming = gsc->state & (1 << ST_CAPT_PIPE_STREAM);

	gsc->state &= ~(1 << ST_CAPT_RUN | 1 << ST_CAPT_STREAM |
			1 << ST_CAPT_PIPE_STREAM | 1 << ST_CAPT_PEND);

	set_bit(ST_CAPT_SUSPENDED, &gsc->state);
	spin_unlock_irqrestore(&gsc->slock, flags);

	if (streaming) {
		if (mdev->is_flite_on)
			return gsc_cap_pipeline_s_stream(gsc, 0);
		else
			return v4l2_subdev_call(gsc->cap.sd_cap, video,
							s_stream, 0);
	} else {
		return 0;
	}
}

static int gsc_cap_stop_capture(struct gsc_dev *gsc)
{
	int ret;
	if (!gsc_cap_active(gsc)) {
		gsc_warn("already stopped\n");
		return 0;
	}
	gsc_info("G-Scaler h/w disable control");
	gsc_hw_enable_control(gsc, false);
	clear_bit(ST_CAPT_STREAM, &gsc->state);
	ret = gsc_wait_operating(gsc);
	if (ret) {
		gsc_err("GSCALER_OP_STATUS is operating\n");
		return ret;
	}

	return gsc_capture_state_cleanup(gsc);
}

static int gsc_capture_stop_streaming(struct vb2_queue *q)
{
	struct gsc_ctx *ctx = q->drv_priv;
	struct gsc_dev *gsc = ctx->gsc_dev;

	if (!gsc_cap_active(gsc))
		return -EINVAL;

	return gsc_cap_stop_capture(gsc);
}

static struct vb2_ops gsc_capture_qops = {
	.queue_setup		= gsc_capture_queue_setup,
	.buf_prepare		= gsc_capture_buf_prepare,
	.buf_queue		= gsc_capture_buf_queue,
	.wait_prepare		= gsc_unlock,
	.wait_finish		= gsc_lock,
	.start_streaming	= gsc_capture_start_streaming,
	.stop_streaming		= gsc_capture_stop_streaming,
};

/*
 * The video node ioctl operations
 */
static int gsc_vidioc_querycap_capture(struct file *file, void *priv,
				       struct v4l2_capability *cap)
{
	struct gsc_dev *gsc = video_drvdata(file);

	strncpy(cap->driver, gsc->pdev->name, sizeof(cap->driver) - 1);
	strncpy(cap->card, gsc->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->capabilities = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE_MPLANE;

	return 0;
}

static int gsc_capture_enum_fmt_mplane(struct file *file, void *priv,
				    struct v4l2_fmtdesc *f)
{
	return gsc_enum_fmt_mplane(f);
}

static int gsc_capture_try_fmt_mplane(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct gsc_dev *gsc = video_drvdata(file);

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	return gsc_try_fmt_mplane(gsc->cap.ctx, f);
}

static int gsc_capture_s_fmt_mplane(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct gsc_dev *gsc = video_drvdata(file);
	struct gsc_ctx *ctx = gsc->cap.ctx;
	struct gsc_frame *frame;
	struct v4l2_pix_format_mplane *pix;
	int i, ret = 0;

	ret = gsc_capture_try_fmt_mplane(file, fh, f);
	if (ret)
		return ret;

	if (vb2_is_streaming(&gsc->cap.vbq)) {
		gsc_err("queue (%d) busy", f->type);
		return -EBUSY;
	}

	frame = &ctx->d_frame;

	pix = &f->fmt.pix_mp;
	frame->fmt = find_format(&pix->pixelformat, NULL, 0);
	if (!frame->fmt)
		return -EINVAL;

	for (i = 0; i < frame->fmt->nr_comp; i++)
		frame->payload[i] =
			pix->plane_fmt[i].bytesperline * pix->height;

	gsc_set_frame_size(frame, pix->width, pix->height);

	gsc_info("f_w: %d, f_h: %d", frame->f_width, frame->f_height);

	return 0;
}

static int gsc_capture_g_fmt_mplane(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct gsc_dev *gsc = video_drvdata(file);
	struct gsc_ctx *ctx = gsc->cap.ctx;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	return gsc_g_fmt_mplane(ctx, f);
}

static int gsc_capture_reqbufs(struct file *file, void *priv,
			    struct v4l2_requestbuffers *reqbufs)
{
	struct gsc_dev *gsc = video_drvdata(file);
	struct gsc_capture_device *cap = &gsc->cap;
	struct gsc_frame *frame;
	int ret;

	frame = ctx_get_frame(cap->ctx, reqbufs->type);
	frame->cacheable = cap->ctx->gsc_ctrls.cacheable->val;
	gsc->vb2->set_cacheable(gsc->alloc_ctx, frame->cacheable);

	ret = vb2_reqbufs(&cap->vbq, reqbufs);
	if (!ret)
		cap->reqbufs_cnt = reqbufs->count;

	return ret;

}

static int gsc_capture_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct gsc_dev *gsc = video_drvdata(file);
	struct gsc_capture_device *cap = &gsc->cap;

	return vb2_querybuf(&cap->vbq, buf);
}

static int gsc_capture_qbuf(struct file *file, void *priv,
			  struct v4l2_buffer *buf)
{
	struct gsc_dev *gsc = video_drvdata(file);
	struct gsc_capture_device *cap = &gsc->cap;

	return vb2_qbuf(&cap->vbq, buf);
}

static int gsc_capture_dqbuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct gsc_dev *gsc = video_drvdata(file);
	return vb2_dqbuf(&gsc->cap.vbq, buf,
		file->f_flags & O_NONBLOCK);
}

static int gsc_capture_cropcap(struct file *file, void *fh,
			    struct v4l2_cropcap *cr)
{
	struct gsc_dev *gsc = video_drvdata(file);
	struct gsc_ctx *ctx = gsc->cap.ctx;

	if (cr->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	cr->bounds.left		= 0;
	cr->bounds.top		= 0;
	cr->bounds.width	= ctx->d_frame.f_width;
	cr->bounds.height	= ctx->d_frame.f_height;
	cr->defrect		= cr->bounds;

	return 0;
}

static int gsc_capture_enum_input(struct file *file, void *priv,
			       struct v4l2_input *i)
{
	struct gsc_dev *gsc = video_drvdata(file);
	struct exynos_platform_gscaler *pdata = gsc->pdata;
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

static int gsc_capture_s_input(struct file *file, void *priv, unsigned int i)
{
	return i == 0 ? 0 : -EINVAL;
}

static int gsc_capture_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

int gsc_capture_ctrls_create(struct gsc_dev *gsc)
{
	int ret;

	if (WARN_ON(gsc->cap.ctx == NULL))
		return -ENXIO;
	if (gsc->cap.ctx->ctrls_rdy)
		return 0;
	ret = gsc_ctrls_create(gsc->cap.ctx);
	if (ret)
		return ret;

	return 0;
}

void gsc_cap_pipeline_prepare(struct gsc_dev *gsc, struct media_entity *me)
{
	struct media_entity_graph graph;
	struct v4l2_subdev *sd;

	media_entity_graph_walk_start(&graph, me);

	while ((me = media_entity_graph_walk_next(&graph))) {
		gsc_info("me->name : %s", me->name);
		if (media_entity_type(me) != MEDIA_ENT_T_V4L2_SUBDEV)
			continue;
		sd = media_entity_to_v4l2_subdev(me);

		switch (sd->grp_id) {
		case GSC_CAP_GRP_ID:
			gsc->pipeline.sd_gsc = sd;
			break;
		case FLITE_GRP_ID:
			gsc->pipeline.flite = sd;
			break;
		case SENSOR_GRP_ID:
			gsc->pipeline.sensor = sd;
			break;
		case CSIS_GRP_ID:
			gsc->pipeline.csis = sd;
			break;
		case FIMD_GRP_ID:
			gsc->pipeline.disp = sd;
			break;
		default:
			gsc_err("Unsupported group id");
			break;
		}
	}

	gsc_info("gsc->pipeline.sd_gsc : 0x%p", gsc->pipeline.sd_gsc);
	gsc_info("gsc->pipeline.flite : 0x%p", gsc->pipeline.flite);
	gsc_info("gsc->pipeline.sensor : 0x%p", gsc->pipeline.sensor);
	gsc_info("gsc->pipeline.csis : 0x%p", gsc->pipeline.csis);
	gsc_info("gsc->pipeline.disp : 0x%p", gsc->pipeline.disp);
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

int gsc_cap_pipeline_s_power(struct gsc_dev *gsc, int state)
{
	int ret = 0;

	if (!gsc->pipeline.sensor || !gsc->pipeline.flite)
		return -ENXIO;

	if (state) {
		ret = __subdev_set_power(gsc->pipeline.flite, 1);
		if (ret && ret != -ENXIO)
			return ret;
		ret = __subdev_set_power(gsc->pipeline.csis, 1);
		if (ret && ret != -ENXIO)
			return ret;
		ret = __subdev_set_power(gsc->pipeline.sensor, 1);
	} else {
		ret = __subdev_set_power(gsc->pipeline.flite, 0);
		if (ret && ret != -ENXIO)
			return ret;
		ret = __subdev_set_power(gsc->pipeline.sensor, 0);
		if (ret && ret != -ENXIO)
			return ret;
		ret = __subdev_set_power(gsc->pipeline.csis, 0);
	}
	return ret == -ENXIO ? 0 : ret;
}

static void gsc_set_cam_clock(struct gsc_dev *gsc, bool on)
{
	struct v4l2_subdev *sd = NULL;
	struct gsc_sensor_info *s_info = NULL;

	if (gsc->pipeline.sensor) {
		sd = gsc->pipeline.sensor;
		s_info = v4l2_get_subdev_hostdata(sd);
	}
	if (on) {
		clk_enable(gsc->clock);
		if (gsc->pipeline.sensor)
			clk_enable(s_info->camclk);
	} else {
		clk_disable(gsc->clock);
		if (gsc->pipeline.sensor)
			clk_disable(s_info->camclk);
	}
}

static int __gsc_cap_pipeline_initialize(struct gsc_dev *gsc,
					 struct media_entity *me, bool prep)
{
	struct exynos_md *mdev = gsc->mdev[MDEV_CAPTURE];
	int ret = 0;

	if (prep) {
		gsc_cap_pipeline_prepare(gsc, me);
		if ((!gsc->pipeline.sensor || !gsc->pipeline.flite) &&
				!gsc->pipeline.disp)
			return -EINVAL;
	}

	gsc_set_cam_clock(gsc, true);

	if (!mdev->is_flite_on && gsc->pipeline.sensor && gsc->pipeline.flite)
		ret = gsc_cap_pipeline_s_power(gsc, 1);

	return ret;
}

int gsc_cap_pipeline_initialize(struct gsc_dev *gsc, struct media_entity *me,
				bool prep)
{
	int ret;

	mutex_lock(&me->parent->graph_mutex);
	ret =  __gsc_cap_pipeline_initialize(gsc, me, prep);
	mutex_unlock(&me->parent->graph_mutex);

	return ret;
}
static int gsc_capture_open(struct file *file)
{
	struct gsc_dev *gsc = video_drvdata(file);
	int ret = v4l2_fh_open(file);

	if (ret)
		return ret;

	if (gsc_m2m_opened(gsc) || gsc_out_opened(gsc) || gsc_cap_opened(gsc)) {
		v4l2_fh_release(file);
		return -EBUSY;
	}

	set_bit(ST_CAPT_OPEN, &gsc->state);
	pm_runtime_get_sync(&gsc->pdev->dev);

	if (++gsc->cap.refcnt == 1) {
		ret = gsc_cap_pipeline_initialize(gsc, &gsc->cap.vfd->entity, true);
		if (ret < 0) {
			gsc_err("gsc pipeline initialization failed\n");
			goto err;
		}

		ret = gsc_capture_ctrls_create(gsc);
		if (ret) {
			gsc_err("failed to create controls\n");
			goto err;
		}
	}

	gsc_info("pid: %d, state: 0x%lx", task_pid_nr(current), gsc->state);

	return 0;

err:
	pm_runtime_put_sync(&gsc->pdev->dev);
	v4l2_fh_release(file);
	clear_bit(ST_CAPT_OPEN, &gsc->state);
	return ret;
}

int __gsc_cap_pipeline_shutdown(struct gsc_dev *gsc)
{
	struct exynos_md *mdev = gsc->mdev[MDEV_CAPTURE];
	int ret = 0;

	if (!mdev->is_flite_on && gsc->pipeline.sensor && gsc->pipeline.flite)
		ret = gsc_cap_pipeline_s_power(gsc, 0);

	if (ret && ret != -ENXIO)
		gsc_set_cam_clock(gsc, false);

	return ret == -ENXIO ? 0 : ret;
}

int gsc_cap_pipeline_shutdown(struct gsc_dev *gsc)
{
	struct media_entity *me = &gsc->cap.vfd->entity;
	int ret;

	mutex_lock(&me->parent->graph_mutex);
	ret = __gsc_cap_pipeline_shutdown(gsc);
	mutex_unlock(&me->parent->graph_mutex);

	return ret;
}
static int gsc_capture_close(struct file *file)
{
	struct gsc_dev *gsc = video_drvdata(file);

	gsc_info("pid: %d, state: 0x%lx", task_pid_nr(current), gsc->state);

	if (--gsc->cap.refcnt == 0) {
		clear_bit(ST_CAPT_OPEN, &gsc->state);
		gsc_info("G-Scaler h/w disable control");
		gsc_hw_enable_control(gsc, false);
		clear_bit(ST_CAPT_STREAM, &gsc->state);
		gsc_cap_pipeline_shutdown(gsc);
		clear_bit(ST_CAPT_SUSPENDED, &gsc->state);
	}

	pm_runtime_put(&gsc->pdev->dev);

	if (gsc->cap.refcnt == 0) {
		vb2_queue_release(&gsc->cap.vbq);
		gsc_ctrls_delete(gsc->cap.ctx);
	}

	return v4l2_fh_release(file);
}

static unsigned int gsc_capture_poll(struct file *file,
				      struct poll_table_struct *wait)
{
	struct gsc_dev *gsc = video_drvdata(file);

	return vb2_poll(&gsc->cap.vbq, file, wait);
}

static int gsc_capture_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gsc_dev *gsc = video_drvdata(file);

	return vb2_mmap(&gsc->cap.vbq, vma);
}

static int gsc_cap_link_validate(struct gsc_dev *gsc)
{
	struct gsc_capture_device *cap = &gsc->cap;
	struct v4l2_subdev_format sink_fmt, src_fmt;
	struct v4l2_subdev *sd;
	struct media_pad *pad;
	int ret;

	/* Get the source pad connected with gsc-video */
	pad =  media_entity_remote_source(&cap->vd_pad);
	if (pad == NULL)
		return -EPIPE;
	/* Get the subdev of source pad */
	sd = media_entity_to_v4l2_subdev(pad->entity);

	while (1) {
		/* Find sink pad of the subdev*/
		pad = &sd->entity.pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;
		if (sd == cap->sd_cap) {
			struct gsc_frame *gf = &cap->ctx->s_frame;
			sink_fmt.format.width = gf->crop.width;
			sink_fmt.format.height = gf->crop.height;
			sink_fmt.format.code = gf->fmt ? gf->fmt->mbus_code : 0;
		} else {
			sink_fmt.pad = pad->index;
			sink_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
			ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &sink_fmt);
			if (ret < 0 && ret != -ENOIOCTLCMD) {
				gsc_err("failed %s subdev get_fmt", sd->name);
				return -EPIPE;
			}
		}
		gsc_info("sink sd name : %s", sd->name);
		/* Get the source pad connected with remote sink pad */
		pad = media_entity_remote_source(pad);
		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		/* Get the subdev of source pad */
		sd = media_entity_to_v4l2_subdev(pad->entity);
		gsc_info("source sd name : %s", sd->name);

		src_fmt.pad = pad->index;
		src_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &src_fmt);
		if (ret < 0 && ret != -ENOIOCTLCMD) {
			gsc_err("failed %s subdev get_fmt", sd->name);
			return -EPIPE;
		}

		gsc_info("src_width : %d, src_height : %d, src_code : %d",
			src_fmt.format.width, src_fmt.format.height,
			src_fmt.format.code);
		gsc_info("sink_width : %d, sink_height : %d, sink_code : %d",
			sink_fmt.format.width, sink_fmt.format.height,
			sink_fmt.format.code);

		if (src_fmt.format.width != sink_fmt.format.width ||
		    src_fmt.format.height != sink_fmt.format.height ||
		    src_fmt.format.code != sink_fmt.format.code) {
			gsc_err("mismatch sink and source");
			return -EPIPE;
		}
	}

	return 0;
}

static int gsc_capture_streamon(struct file *file, void *priv,
				enum v4l2_buf_type type)
{
	struct gsc_dev *gsc = video_drvdata(file);
	struct gsc_pipeline *p = &gsc->pipeline;
	int ret;

	if (gsc_cap_active(gsc))
		return -EBUSY;

	if (p->disp)
		media_entity_pipeline_start(&p->disp->entity, p->pipe);
	else if (p->sensor)
		media_entity_pipeline_start(&p->sensor->entity, p->pipe);

	ret = gsc_cap_link_validate(gsc);
	if (ret)
		return ret;

	return vb2_streamon(&gsc->cap.vbq, type);
}

static int gsc_capture_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct gsc_dev *gsc = video_drvdata(file);
	struct v4l2_subdev *sd;
	struct gsc_pipeline *p = &gsc->pipeline;
	int ret;

	if (p->disp) {
		sd = gsc->pipeline.disp;
	} else if (p->sensor) {
		sd = gsc->pipeline.sensor;
	} else {
		gsc_err("Error pipeline");
		return -EPIPE;
	}

	ret = vb2_streamoff(&gsc->cap.vbq, type);
	if (ret == 0) {
		if (p->disp)
			media_entity_pipeline_stop(&p->disp->entity);
		else if (p->sensor)
			media_entity_pipeline_stop(&p->sensor->entity);
	}

	return ret;
}

static struct v4l2_subdev *gsc_cap_remote_subdev(struct gsc_dev *gsc, u32 *pad)
{
	struct media_pad *remote;

	remote = media_entity_remote_source(&gsc->cap.vd_pad);

	if (remote == NULL ||
	    media_entity_type(remote->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
		return NULL;

	if (pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

static int gsc_capture_g_crop(struct file *file, void *fh, struct v4l2_crop *crop)
{
	struct gsc_dev *gsc = video_drvdata(file);
	struct v4l2_subdev_format format;
	struct v4l2_subdev *subdev;
	u32 pad;
	int ret;

	subdev = gsc_cap_remote_subdev(gsc, &pad);
	if (subdev == NULL)
		return -EINVAL;

	/* Try the get crop operation first and fallback to get format if not
	 * implemented.
	 */
	ret = v4l2_subdev_call(subdev, video, g_crop, crop);
	if (ret != -ENOIOCTLCMD)
		return ret;

	format.pad = pad;
	format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &format);
	if (ret < 0)
		return ret == -ENOIOCTLCMD ? -EINVAL : ret;

	crop->c.left = 0;
	crop->c.top = 0;
	crop->c.width = format.format.width;
	crop->c.height = format.format.height;

	return 0;
}

static int gsc_capture_s_crop(struct file *file, void *fh, struct v4l2_crop *crop)
{
	struct gsc_dev *gsc = video_drvdata(file);
	struct v4l2_subdev *subdev;
	int ret;

	subdev = gsc_cap_remote_subdev(gsc, NULL);
	if (subdev == NULL)
		return -EINVAL;

	ret = v4l2_subdev_call(subdev, video, s_crop, crop);

	return ret == -ENOIOCTLCMD ? -EINVAL : ret;
}


static const struct v4l2_ioctl_ops gsc_capture_ioctl_ops = {
	.vidioc_querycap		= gsc_vidioc_querycap_capture,

	.vidioc_enum_fmt_vid_cap_mplane	= gsc_capture_enum_fmt_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= gsc_capture_try_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= gsc_capture_s_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= gsc_capture_g_fmt_mplane,

	.vidioc_reqbufs			= gsc_capture_reqbufs,
	.vidioc_querybuf		= gsc_capture_querybuf,

	.vidioc_qbuf			= gsc_capture_qbuf,
	.vidioc_dqbuf			= gsc_capture_dqbuf,

	.vidioc_streamon		= gsc_capture_streamon,
	.vidioc_streamoff		= gsc_capture_streamoff,

	.vidioc_g_crop			= gsc_capture_g_crop,
	.vidioc_s_crop			= gsc_capture_s_crop,
	.vidioc_cropcap			= gsc_capture_cropcap,

	.vidioc_enum_input		= gsc_capture_enum_input,
	.vidioc_s_input			= gsc_capture_s_input,
	.vidioc_g_input			= gsc_capture_g_input,
};

static const struct v4l2_file_operations gsc_capture_fops = {
	.owner		= THIS_MODULE,
	.open		= gsc_capture_open,
	.release	= gsc_capture_close,
	.poll		= gsc_capture_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= gsc_capture_mmap,
};

/*
 * __gsc_cap_get_format - helper function for getting gscaler format
 * @res   : pointer to resizer private structure
 * @pad   : pad number
 * @fh    : V4L2 subdev file handle
 * @which : wanted subdev format
 * return zero
 */
static struct v4l2_mbus_framefmt *__gsc_cap_get_format(struct gsc_dev *gsc,
				struct v4l2_subdev_fh *fh, unsigned int pad,
				enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(fh, pad);
	else
		return &gsc->cap.mbus_fmt[pad];
}
static void gsc_cap_check_limit_size(struct gsc_dev *gsc, unsigned int pad,
				   struct v4l2_mbus_framefmt *fmt)
{
	struct gsc_variant *variant = gsc->variant;
	struct gsc_ctx *ctx = gsc->cap.ctx;
	u32 min_w, min_h, max_w, max_h;

	switch (pad) {
	case GSC_PAD_SINK:
		if (gsc_cap_opened(gsc) &&
		    (ctx->gsc_ctrls.rotate->val == 90 ||
		    ctx->gsc_ctrls.rotate->val == 270)) {
			min_w = variant->pix_min->real_w;
			min_h = variant->pix_min->real_h;
			max_w = variant->pix_max->real_rot_en_w;
			max_h = variant->pix_max->real_rot_en_h;
		} else {
			min_w = variant->pix_min->real_w;
			min_h = variant->pix_min->real_h;
			max_w = variant->pix_max->real_rot_dis_w;
			max_h = variant->pix_max->real_rot_dis_h;
		}
		break;

	case GSC_PAD_SOURCE:
		min_w = variant->pix_min->target_rot_dis_w;
		min_h = variant->pix_min->target_rot_dis_h;
		max_w = variant->pix_max->target_rot_dis_w;
		max_h = variant->pix_max->target_rot_dis_h;
		break;
	}

	fmt->width = clamp_t(u32, fmt->width, min_w, max_w);
	fmt->height = clamp_t(u32, fmt->height , min_h, max_h);
}
static void gsc_cap_try_format(struct gsc_dev *gsc,
			       struct v4l2_subdev_fh *fh, unsigned int pad,
			       struct v4l2_mbus_framefmt *fmt,
			       enum v4l2_subdev_format_whence which)
{
	struct gsc_fmt *gfmt;

	gfmt = find_format(NULL, &fmt->code, 0);
	WARN_ON(!gfmt);

	if (pad == GSC_PAD_SINK) {
		struct gsc_ctx *ctx = gsc->cap.ctx;
		struct gsc_frame *frame = &ctx->s_frame;

		frame->fmt = gfmt;
	}

	gsc_cap_check_limit_size(gsc, pad, fmt);

	fmt->colorspace = V4L2_COLORSPACE_JPEG;
	fmt->field = V4L2_FIELD_NONE;
}

static int gsc_capture_subdev_set_fmt(struct v4l2_subdev *sd,
				      struct v4l2_subdev_fh *fh,
				      struct v4l2_subdev_format *fmt)
{
	struct gsc_dev *gsc = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;
	struct gsc_ctx *ctx = gsc->cap.ctx;
	struct gsc_frame *frame;

	mf = __gsc_cap_get_format(gsc, fh, fmt->pad, fmt->which);
	if (mf == NULL)
		return -EINVAL;

	gsc_cap_try_format(gsc, fh, fmt->pad, &fmt->format, fmt->which);
	*mf = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	frame = gsc_capture_get_frame(ctx, fmt->pad);

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		frame->crop.left = 0;
		frame->crop.top = 0;
		frame->f_width = mf->width;
		frame->f_height = mf->height;
		frame->crop.width = mf->width;
		frame->crop.height = mf->height;
	}
	gsc_dbg("offs_h : %d, offs_v : %d, f_width : %d, f_height :%d,\
				width : %d, height : %d", frame->crop.left,\
				frame->crop.top, frame->f_width,
				frame->f_height,\
				frame->crop.width, frame->crop.height);

	return 0;
}

static int gsc_capture_subdev_get_fmt(struct v4l2_subdev *sd,
				      struct v4l2_subdev_fh *fh,
				      struct v4l2_subdev_format *fmt)
{
	struct gsc_dev *gsc = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *mf;

	mf = __gsc_cap_get_format(gsc, fh, fmt->pad, fmt->which);
	if (mf == NULL)
		return -EINVAL;

	fmt->format = *mf;

	return 0;
}

static int __gsc_cap_get_crop(struct gsc_dev *gsc, struct v4l2_subdev_fh *fh,
			      unsigned int pad, enum v4l2_subdev_format_whence which,
				struct v4l2_rect *crop)
{
	struct gsc_ctx *ctx = gsc->cap.ctx;
	struct gsc_frame *frame = gsc_capture_get_frame(ctx, pad);

	if (which == V4L2_SUBDEV_FORMAT_TRY) {
		crop = v4l2_subdev_get_try_crop(fh, pad);
	} else {
		crop->left = frame->crop.left;
		crop->top = frame->crop.top;
		crop->width = frame->crop.width;
		crop->height = frame->crop.height;
	}

	return 0;
}

static void gsc_cap_try_crop(struct gsc_dev *gsc, struct v4l2_rect *crop,
				u32 pad)
{
	struct gsc_variant *variant = gsc->variant;
	struct gsc_ctx *ctx = gsc->cap.ctx;
	struct gsc_frame *frame = gsc_capture_get_frame(ctx, pad);

	u32 crop_min_w = variant->pix_min->target_rot_dis_w;
	u32 crop_min_h = variant->pix_min->target_rot_dis_h;
	u32 crop_max_w = frame->f_width;
	u32 crop_max_h = frame->f_height;

	crop->left = clamp_t(u32, crop->left, 0, crop_max_w - crop_min_w);
	crop->top = clamp_t(u32, crop->top, 0, crop_max_h - crop_min_h);
	crop->width = clamp_t(u32, crop->width, crop_min_w, crop_max_w);
	crop->height = clamp_t(u32, crop->height, crop_min_h, crop_max_h);
}

static int gsc_capture_subdev_set_crop(struct v4l2_subdev *sd,
				       struct v4l2_subdev_fh *fh,
				       struct v4l2_subdev_crop *crop)
{
	struct gsc_dev *gsc = v4l2_get_subdevdata(sd);
	struct gsc_ctx *ctx = gsc->cap.ctx;
	struct gsc_frame *frame = gsc_capture_get_frame(ctx, crop->pad);

	gsc_cap_try_crop(gsc, &crop->rect, crop->pad);

	if (crop->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		frame->crop = crop->rect;

	return 0;
}

static int gsc_capture_subdev_get_crop(struct v4l2_subdev *sd,
				       struct v4l2_subdev_fh *fh,
				       struct v4l2_subdev_crop *crop)
{
	struct gsc_dev *gsc = v4l2_get_subdevdata(sd);
	struct v4l2_rect gcrop = {0, };

	__gsc_cap_get_crop(gsc, fh, crop->pad, crop->which, &gcrop);
	crop->rect = gcrop;

	return 0;
}

static struct v4l2_subdev_pad_ops gsc_cap_subdev_pad_ops = {
	.get_fmt = gsc_capture_subdev_get_fmt,
	.set_fmt = gsc_capture_subdev_set_fmt,
	.get_crop = gsc_capture_subdev_get_crop,
	.set_crop = gsc_capture_subdev_set_crop,
};

static struct v4l2_subdev_video_ops gsc_cap_subdev_video_ops = {
	.s_stream = gsc_capture_subdev_s_stream,
};

static struct v4l2_subdev_ops gsc_cap_subdev_ops = {
	.pad = &gsc_cap_subdev_pad_ops,
	.video = &gsc_cap_subdev_video_ops,
};

static int gsc_capture_init_formats(struct v4l2_subdev *sd,
				    struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;
	struct gsc_dev *gsc = v4l2_get_subdevdata(sd);
	struct gsc_ctx *ctx = gsc->cap.ctx;

	ctx->s_frame.fmt = get_format(2);
	memset(&format, 0, sizeof(format));
	format.pad = GSC_PAD_SINK;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = ctx->s_frame.fmt->mbus_code;
	format.format.width = DEFAULT_GSC_SINK_WIDTH;
	format.format.height = DEFAULT_GSC_SINK_HEIGHT;
	gsc_capture_subdev_set_fmt(sd, fh, &format);

	/* G-scaler should not propagate, because it is possible that sink
	 * format different from source format. But the operation of source pad
	 * is not needed.
	 */
	ctx->d_frame.fmt = get_format(2);

	return 0;
}

static int gsc_capture_subdev_close(struct v4l2_subdev *sd,
				    struct v4l2_subdev_fh *fh)
{
	gsc_dbg("");

	return 0;
}

static int gsc_capture_subdev_registered(struct v4l2_subdev *sd)
{
	gsc_dbg("");

	return 0;
}

static void gsc_capture_subdev_unregistered(struct v4l2_subdev *sd)
{
	gsc_dbg("");
}

static const struct v4l2_subdev_internal_ops gsc_cap_v4l2_internal_ops = {
	.open = gsc_capture_init_formats,
	.close = gsc_capture_subdev_close,
	.registered = gsc_capture_subdev_registered,
	.unregistered = gsc_capture_subdev_unregistered,
};

static int gsc_capture_link_setup(struct media_entity *entity,
				  const struct media_pad *local,
				  const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct gsc_dev *gsc = v4l2_get_subdevdata(sd);
	struct gsc_capture_device *cap = &gsc->cap;

	gsc_info("");
	switch (local->index | media_entity_type(remote->entity)) {
	case GSC_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (cap->input != 0)
				return -EBUSY;
			/* Write-Back link enabled */
			if (!strcmp(remote->entity->name, FIMD_MODULE_NAME)) {
				gsc->cap.sd_disp =
					media_entity_to_v4l2_subdev(remote->entity);
				gsc->cap.sd_disp->grp_id = FIMD_GRP_ID;
				cap->ctx->in_path = GSC_WRITEBACK;
				cap->input |= GSC_IN_FIMD_WRITEBACK;
			} else if (remote->index == FLITE_PAD_SOURCE_PREV) {
				cap->input |= GSC_IN_FLITE_PREVIEW;
			} else {
				cap->input |= GSC_IN_FLITE_CAMCORDING;
			}
		} else {
			cap->input = GSC_IN_NONE;
		}
		break;
	case GSC_PAD_SOURCE | MEDIA_ENT_T_DEVNODE:
		/* gsc-cap always write to memory */
		break;
	}

	return 0;
}

static const struct media_entity_operations gsc_cap_media_ops = {
	.link_setup = gsc_capture_link_setup,
};

static int gsc_capture_create_subdev(struct gsc_dev *gsc)
{
	struct v4l2_device *v4l2_dev;
	struct v4l2_subdev *sd;
	int ret;

	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd)
	       return -ENOMEM;

	v4l2_subdev_init(sd, &gsc_cap_subdev_ops);
	sd->flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "gsc-cap-subdev.%d", gsc->id);

	gsc->cap.sd_pads[GSC_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	gsc->cap.sd_pads[GSC_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&sd->entity, GSC_PADS_NUM,
				gsc->cap.sd_pads, 0);
	if (ret)
		goto err_ent;

	sd->internal_ops = &gsc_cap_v4l2_internal_ops;
	sd->entity.ops = &gsc_cap_media_ops;
	sd->grp_id = GSC_CAP_GRP_ID;
	v4l2_dev = &gsc->mdev[MDEV_CAPTURE]->v4l2_dev;

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret)
		goto err_sub;

	gsc->mdev[MDEV_CAPTURE]->gsc_cap_sd[gsc->id] = sd;
	gsc->cap.sd_cap = sd;
	v4l2_set_subdevdata(sd, gsc);
	gsc_capture_init_formats(sd, NULL);

	return 0;

err_sub:
	media_entity_cleanup(&sd->entity);
err_ent:
	kfree(sd);
	return ret;
}

static int gsc_capture_create_link(struct gsc_dev *gsc)
{
	struct media_entity *source, *sink;
	struct exynos_platform_gscaler *pdata = gsc->pdata;
	struct exynos_isp_info *isp_info;
	u32 num_clients = pdata->num_clients;
	int ret, i;
	enum cam_port id;

	/* GSC-SUBDEV ------> GSC-VIDEO (Always link enable) */
	source = &gsc->cap.sd_cap->entity;
	sink = &gsc->cap.vfd->entity;
	if (source && sink) {
		ret = media_entity_create_link(source, GSC_PAD_SOURCE, sink,
				0, MEDIA_LNK_FL_IMMUTABLE |
				MEDIA_LNK_FL_ENABLED);
		if (ret) {
			gsc_err("failed link flite to gsc\n");
			return ret;
		}
	}
	for (i = 0; i < num_clients; i++) {
		isp_info = pdata->isp_info[i];
		id = isp_info->cam_port;
		/* FIMC-LITE ------> GSC-SUBDEV (ITU & MIPI common) */
		source = &gsc->cap.sd_flite[id]->entity;
		sink = &gsc->cap.sd_cap->entity;
		if (source && sink) {
			if (pdata->cam_preview)
				ret = media_entity_create_link(source,
						FLITE_PAD_SOURCE_PREV,
						sink, GSC_PAD_SINK, 0);
			if (!ret && pdata->cam_camcording)
				ret = media_entity_create_link(source,
						FLITE_PAD_SOURCE_CAMCORD,
						sink, GSC_PAD_SINK, 0);
			if (ret) {
				gsc_err("failed link flite to gsc\n");
				return ret;
			}
		}
	}

	return 0;
}

static struct v4l2_subdev *gsc_cap_register_sensor(struct gsc_dev *gsc, int i)
{
	struct exynos_md *mdev = gsc->mdev[MDEV_CAPTURE];
	struct v4l2_subdev *sd = NULL;

	sd = mdev->sensor_sd[i];
	if (!sd)
		return NULL;

	v4l2_set_subdev_hostdata(sd, &gsc->cap.sensor[i]);

	return sd;
}

static int gsc_cap_register_sensor_entities(struct gsc_dev *gsc)
{
	struct exynos_platform_gscaler *pdata = gsc->pdata;
	u32 num_clients = pdata->num_clients;
	int i;

	for (i = 0; i < num_clients; i++) {
		gsc->cap.sensor[i].pdata = pdata->isp_info[i];
		gsc->cap.sensor[i].sd = gsc_cap_register_sensor(gsc, i);
		if (IS_ERR_OR_NULL(gsc->cap.sensor[i].sd)) {
			gsc_err("failed to get register sensor");
			return -EINVAL;
		}
	}

	return 0;
}

static int gsc_cap_config_camclk(struct gsc_dev *gsc,
		struct exynos_isp_info *isp_info, int i)
{
	struct gsc_capture_device *gsc_cap = &gsc->cap;
	struct clk *camclk;
	struct clk *srclk;

	camclk = clk_get(&gsc->pdev->dev, isp_info->cam_clk_name);
	if (IS_ERR_OR_NULL(camclk)) {
		gsc_err("failed to get cam clk");
		return -ENXIO;
	}
	gsc_cap->sensor[i].camclk = camclk;

	srclk = clk_get(&gsc->pdev->dev, isp_info->cam_srclk_name);
	if (IS_ERR_OR_NULL(srclk)) {
		clk_put(camclk);
		gsc_err("failed to get cam source clk\n");
		return -ENXIO;
	}
	clk_set_parent(camclk, srclk);
	clk_set_rate(camclk, isp_info->clk_frequency);
	clk_put(srclk);

	return 0;
}

int gsc_register_capture_device(struct gsc_dev *gsc)
{
	struct video_device *vfd;
	struct gsc_capture_device *gsc_cap;
	struct gsc_ctx *ctx;
	struct vb2_queue *q;
	struct exynos_platform_gscaler *pdata = gsc->pdata;
	struct exynos_isp_info *isp_info;
	int ret = -ENOMEM;
	int i;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->gsc_dev	 = gsc;
	ctx->in_path	 = GSC_CAMERA;
	ctx->out_path	 = GSC_DMA;
	ctx->state	 = GSC_CTX_CAP;

	vfd = video_device_alloc();
	if (!vfd) {
		printk("Failed to allocate video device\n");
		goto err_ctx_alloc;
	}

	snprintf(vfd->name, sizeof(vfd->name), "%s.capture",
		 dev_name(&gsc->pdev->dev));

	vfd->fops	= &gsc_capture_fops;
	vfd->ioctl_ops	= &gsc_capture_ioctl_ops;
	vfd->v4l2_dev	= &gsc->mdev[MDEV_CAPTURE]->v4l2_dev;
	vfd->minor	= -1;
	vfd->release	= video_device_release;
	vfd->lock	= &gsc->lock;
	video_set_drvdata(vfd, gsc);

	gsc_cap	= &gsc->cap;
	gsc_cap->vfd = vfd;
	gsc_cap->refcnt = 0;
	gsc_cap->active_buf_cnt = 0;
	gsc_cap->reqbufs_cnt  = 0;

	spin_lock_init(&ctx->slock);
	gsc_cap->ctx = ctx;

	q = &gsc->cap.vbq;
	memset(q, 0, sizeof(*q));
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = gsc->cap.ctx;
	q->ops = &gsc_capture_qops;
	q->mem_ops = gsc->vb2->ops;

	vb2_queue_init(q);

	/* Get mipi-csis and fimc-lite subdev ptr using mdev */
	for (i = 0; i < FLITE_MAX_ENTITIES; i++)
		gsc->cap.sd_flite[i] = gsc->mdev[MDEV_CAPTURE]->flite_sd[i];

	for (i = 0; i < CSIS_MAX_ENTITIES; i++)
		gsc->cap.sd_csis[i] = gsc->mdev[MDEV_CAPTURE]->csis_sd[i];

	for (i = 0; i < pdata->num_clients; i++) {
		isp_info = pdata->isp_info[i];
		ret = gsc_cap_config_camclk(gsc, isp_info, i);
		if (ret) {
			gsc_err("failed setup cam clk");
			goto err_ctx_alloc;
		}
	}

	ret = gsc_cap_register_sensor_entities(gsc);
	if (ret) {
		gsc_err("failed register sensor entities");
		goto err_clk;
	}

	ret = video_register_device(vfd, VFL_TYPE_GRABBER,
				    EXYNOS_VIDEONODE_GSC_CAP(gsc->id));
	if (ret) {
		gsc_err("failed to register video device");
		goto err_clk;
	}

	gsc->cap.vd_pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&vfd->entity, 1, &gsc->cap.vd_pad, 0);
	if (ret) {
		gsc_err("failed to initialize entity");
		goto err_ent;
	}

	ret = gsc_capture_create_subdev(gsc);
	if (ret) {
		gsc_err("failed create subdev");
		goto err_sd_reg;
	}

	ret = gsc_capture_create_link(gsc);
	if (ret) {
		gsc_err("failed create link");
		goto err_sd_reg;
	}

	vfd->ctrl_handler = &ctx->ctrl_handler;
	gsc_dbg("gsc capture driver registered as /dev/video%d", vfd->num);

	return 0;

err_sd_reg:
	media_entity_cleanup(&vfd->entity);
err_ent:
	video_device_release(vfd);
err_clk:
	for (i = 0; i < pdata->num_clients; i++)
		clk_put(gsc_cap->sensor[i].camclk);
err_ctx_alloc:
	kfree(ctx);

	return ret;
}

static void gsc_capture_destroy_subdev(struct gsc_dev *gsc)
{
	struct v4l2_subdev *sd = gsc->cap.sd_cap;

	if (!sd)
		return;
	media_entity_cleanup(&sd->entity);
	v4l2_device_unregister_subdev(sd);
	kfree(sd);
	sd = NULL;
}

void gsc_unregister_capture_device(struct gsc_dev *gsc)
{
	struct video_device *vfd = gsc->cap.vfd;

	if (vfd) {
		media_entity_cleanup(&vfd->entity);
		/* Can also be called if video device was
		   not registered */
		video_unregister_device(vfd);
	}
	gsc_capture_destroy_subdev(gsc);
	kfree(gsc->cap.ctx);
	gsc->cap.ctx = NULL;
}

