// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pm_runtime.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "iris_vidc.h"
#include "iris_instance.h"
#include "iris_vdec.h"
#include "iris_venc.h"
#include "iris_vb2.h"
#include "iris_vpu_buffer.h"
#include "iris_platform_common.h"

#define IRIS_DRV_NAME "iris_driver"
#define IRIS_BUS_NAME "platform:iris_icc"
#define STEP_WIDTH 1
#define STEP_HEIGHT 1

static void iris_v4l2_fh_init(struct iris_inst *inst, struct file *filp)
{
	if (inst->domain == ENCODER)
		v4l2_fh_init(&inst->fh, inst->core->vdev_enc);
	else if (inst->domain == DECODER)
		v4l2_fh_init(&inst->fh, inst->core->vdev_dec);
	inst->fh.ctrl_handler = &inst->ctrl_handler;
	v4l2_fh_add(&inst->fh, filp);
}

static void iris_v4l2_fh_deinit(struct iris_inst *inst, struct file *filp)
{
	v4l2_fh_del(&inst->fh, filp);
	inst->fh.ctrl_handler = NULL;
	v4l2_fh_exit(&inst->fh);
}

static void iris_add_session(struct iris_inst *inst)
{
	struct iris_core *core = inst->core;
	struct iris_inst *iter;
	u32 count = 0;

	mutex_lock(&core->lock);

	list_for_each_entry(iter, &core->instances, list)
		count++;

	if (count < core->iris_platform_data->max_session_count)
		list_add_tail(&inst->list, &core->instances);

	mutex_unlock(&core->lock);
}

static void iris_remove_session(struct iris_inst *inst)
{
	struct iris_core *core = inst->core;
	struct iris_inst *iter, *temp;

	mutex_lock(&core->lock);
	list_for_each_entry_safe(iter, temp, &core->instances, list) {
		if (iter->session_id == inst->session_id) {
			list_del_init(&iter->list);
			break;
		}
	}
	mutex_unlock(&core->lock);
}

static inline struct iris_inst *iris_get_inst(struct file *filp)
{
	return container_of(file_to_v4l2_fh(filp), struct iris_inst, fh);
}

static void iris_m2m_device_run(void *priv)
{
}

static void iris_m2m_job_abort(void *priv)
{
	struct iris_inst *inst = priv;
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;

	v4l2_m2m_job_finish(inst->m2m_dev, m2m_ctx);
}

static const struct v4l2_m2m_ops iris_m2m_ops = {
	.device_run = iris_m2m_device_run,
	.job_abort = iris_m2m_job_abort,
};

static int
iris_m2m_queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	struct iris_inst *inst = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->ops = inst->core->iris_vb2_ops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->drv_priv = inst;
	src_vq->buf_struct_size = sizeof(struct iris_buffer);
	src_vq->min_reqbufs_allocation = MIN_BUFFERS;
	src_vq->dev = inst->core->dev;
	src_vq->lock = &inst->ctx_q_lock;
	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->ops = inst->core->iris_vb2_ops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->drv_priv = inst;
	dst_vq->buf_struct_size = sizeof(struct iris_buffer);
	dst_vq->min_reqbufs_allocation = MIN_BUFFERS;
	dst_vq->dev = inst->core->dev;
	dst_vq->lock = &inst->ctx_q_lock;

	return vb2_queue_init(dst_vq);
}

int iris_open(struct file *filp)
{
	struct iris_core *core = video_drvdata(filp);
	struct video_device *vdev;
	struct iris_inst *inst;
	u32 session_type;
	int ret;

	vdev = video_devdata(filp);
	if (strcmp(vdev->name, "qcom-iris-decoder") == 0)
		session_type = DECODER;
	else if (strcmp(vdev->name, "qcom-iris-encoder") == 0)
		session_type = ENCODER;
	else
		return -EINVAL;

	ret = pm_runtime_resume_and_get(core->dev);
	if (ret < 0)
		return ret;

	ret = iris_core_init(core);
	if (ret) {
		dev_err(core->dev, "core init failed\n");
		pm_runtime_put_sync(core->dev);
		return ret;
	}

	pm_runtime_put_sync(core->dev);

	inst = core->iris_platform_data->get_instance();
	if (!inst)
		return -ENOMEM;

	inst->core = core;
	inst->domain = session_type;
	inst->session_id = hash32_ptr(inst);
	inst->state = IRIS_INST_DEINIT;

	mutex_init(&inst->lock);
	mutex_init(&inst->ctx_q_lock);

	INIT_LIST_HEAD(&inst->buffers[BUF_BIN].list);
	INIT_LIST_HEAD(&inst->buffers[BUF_ARP].list);
	INIT_LIST_HEAD(&inst->buffers[BUF_COMV].list);
	INIT_LIST_HEAD(&inst->buffers[BUF_NON_COMV].list);
	INIT_LIST_HEAD(&inst->buffers[BUF_LINE].list);
	INIT_LIST_HEAD(&inst->buffers[BUF_DPB].list);
	INIT_LIST_HEAD(&inst->buffers[BUF_PERSIST].list);
	INIT_LIST_HEAD(&inst->buffers[BUF_SCRATCH_1].list);
	INIT_LIST_HEAD(&inst->buffers[BUF_SCRATCH_2].list);
	INIT_LIST_HEAD(&inst->buffers[BUF_VPSS].list);
	init_completion(&inst->completion);
	init_completion(&inst->flush_completion);

	iris_v4l2_fh_init(inst, filp);

	inst->m2m_dev = v4l2_m2m_init(&iris_m2m_ops);
	if (IS_ERR_OR_NULL(inst->m2m_dev)) {
		ret = -EINVAL;
		goto fail_v4l2_fh_deinit;
	}

	inst->m2m_ctx = v4l2_m2m_ctx_init(inst->m2m_dev, inst, iris_m2m_queue_init);
	if (IS_ERR_OR_NULL(inst->m2m_ctx)) {
		ret = -EINVAL;
		goto fail_m2m_release;
	}

	if (inst->domain == DECODER)
		ret = iris_vdec_inst_init(inst);
	else if (inst->domain == ENCODER)
		ret = iris_venc_inst_init(inst);
	if (ret)
		goto fail_m2m_ctx_release;

	iris_add_session(inst);

	inst->fh.m2m_ctx = inst->m2m_ctx;

	return 0;

fail_m2m_ctx_release:
	v4l2_m2m_ctx_release(inst->m2m_ctx);
fail_m2m_release:
	v4l2_m2m_release(inst->m2m_dev);
fail_v4l2_fh_deinit:
	iris_v4l2_fh_deinit(inst, filp);
	mutex_destroy(&inst->ctx_q_lock);
	mutex_destroy(&inst->lock);
	kfree(inst);

	return ret;
}

static void iris_session_close(struct iris_inst *inst)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	bool wait_for_response = true;
	int ret;

	if (inst->state == IRIS_INST_DEINIT)
		return;

	reinit_completion(&inst->completion);

	ret = hfi_ops->session_close(inst);
	if (ret)
		wait_for_response = false;

	if (wait_for_response)
		iris_wait_for_session_response(inst, false);
}

static void iris_check_num_queued_internal_buffers(struct iris_inst *inst, u32 plane)
{
	const struct iris_platform_data *platform_data = inst->core->iris_platform_data;
	struct iris_buffer *buf, *next;
	struct iris_buffers *buffers;
	const u32 *internal_buf_type;
	u32 internal_buffer_count, i;
	u32 count = 0;

	if (V4L2_TYPE_IS_OUTPUT(plane)) {
		internal_buf_type = platform_data->dec_ip_int_buf_tbl;
		internal_buffer_count = platform_data->dec_ip_int_buf_tbl_size;
	} else {
		internal_buf_type = platform_data->dec_op_int_buf_tbl;
		internal_buffer_count = platform_data->dec_op_int_buf_tbl_size;
	}

	for (i = 0; i < internal_buffer_count; i++) {
		buffers = &inst->buffers[internal_buf_type[i]];
		count = 0;
		list_for_each_entry_safe(buf, next, &buffers->list, list)
			count++;
		if (count)
			dev_err(inst->core->dev, "%d buffer of type %d not released",
				count, internal_buf_type[i]);
	}

	if (inst->domain == DECODER)
		buffers = &inst->buffers[BUF_PERSIST];
	else
		buffers = &inst->buffers[BUF_ARP];

	count = 0;
	list_for_each_entry_safe(buf, next, &buffers->list, list)
		count++;
	if (count)
		dev_err(inst->core->dev, "%d buffer of type %d not released",
			count, inst->domain == DECODER ? BUF_PERSIST : BUF_ARP);
}

int iris_close(struct file *filp)
{
	struct iris_inst *inst = iris_get_inst(filp);

	v4l2_ctrl_handler_free(&inst->ctrl_handler);
	v4l2_m2m_ctx_release(inst->m2m_ctx);
	v4l2_m2m_release(inst->m2m_dev);
	mutex_lock(&inst->lock);
	if (inst->domain == DECODER)
		iris_vdec_inst_deinit(inst);
	else if (inst->domain == ENCODER)
		iris_venc_inst_deinit(inst);
	iris_session_close(inst);
	iris_inst_change_state(inst, IRIS_INST_DEINIT);
	iris_v4l2_fh_deinit(inst, filp);
	iris_destroy_all_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	iris_destroy_all_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	iris_check_num_queued_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	iris_check_num_queued_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	iris_remove_session(inst);
	mutex_unlock(&inst->lock);
	mutex_destroy(&inst->ctx_q_lock);
	mutex_destroy(&inst->lock);
	kfree(inst);

	return 0;
}

static int iris_enum_fmt(struct file *filp, void *fh, struct v4l2_fmtdesc *f)
{
	struct iris_inst *inst = iris_get_inst(filp);

	if (inst->domain == DECODER)
		return iris_vdec_enum_fmt(inst, f);
	else if (inst->domain == ENCODER)
		return iris_venc_enum_fmt(inst, f);
	else
		return -EINVAL;
}

static int iris_try_fmt_vid_mplane(struct file *filp, void *fh, struct v4l2_format *f)
{
	struct iris_inst *inst = iris_get_inst(filp);
	int ret = 0;

	mutex_lock(&inst->lock);

	if (inst->domain == DECODER)
		ret = iris_vdec_try_fmt(inst, f);
	else if (inst->domain == ENCODER)
		ret = iris_venc_try_fmt(inst, f);

	mutex_unlock(&inst->lock);

	return ret;
}

static int iris_s_fmt_vid_mplane(struct file *filp, void *fh, struct v4l2_format *f)
{
	struct iris_inst *inst = iris_get_inst(filp);
	int ret = 0;

	mutex_lock(&inst->lock);

	if (inst->domain == DECODER)
		ret = iris_vdec_s_fmt(inst, f);
	else if (inst->domain == ENCODER)
		ret = iris_venc_s_fmt(inst, f);

	mutex_unlock(&inst->lock);

	return ret;
}

static int iris_g_fmt_vid_mplane(struct file *filp, void *fh, struct v4l2_format *f)
{
	struct iris_inst *inst = iris_get_inst(filp);
	int ret = 0;

	mutex_lock(&inst->lock);
	if (V4L2_TYPE_IS_OUTPUT(f->type))
		*f = *inst->fmt_src;
	else if (V4L2_TYPE_IS_CAPTURE(f->type))
		*f = *inst->fmt_dst;
	else
		ret = -EINVAL;

	mutex_unlock(&inst->lock);

	return ret;
}

static int iris_enum_framesizes(struct file *filp, void *fh,
				struct v4l2_frmsizeenum *fsize)
{
	struct iris_inst *inst = iris_get_inst(filp);
	struct platform_inst_caps *caps;
	int ret = 0;

	if (fsize->index)
		return -EINVAL;

	if (inst->domain == DECODER)
		ret = iris_vdec_validate_format(inst, fsize->pixel_format);
	else
		ret = iris_venc_validate_format(inst, fsize->pixel_format);

	if (ret)
		return ret;

	caps = inst->core->iris_platform_data->inst_caps;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = caps->min_frame_width;
	fsize->stepwise.max_width = caps->max_frame_width;
	fsize->stepwise.step_width = STEP_WIDTH;
	fsize->stepwise.min_height = caps->min_frame_height;
	fsize->stepwise.max_height = caps->max_frame_height;
	fsize->stepwise.step_height = STEP_HEIGHT;

	return ret;
}

static int iris_enum_frameintervals(struct file *filp, void *fh,
				    struct v4l2_frmivalenum *fival)

{
	struct iris_inst *inst = iris_get_inst(filp);
	struct iris_core *core = inst->core;
	struct platform_inst_caps *caps;
	u32 fps, mbpf;
	int ret = 0;

	if (inst->domain == DECODER)
		return -ENOTTY;

	if (fival->index)
		return -EINVAL;

	ret = iris_venc_validate_format(inst, fival->pixel_format);
	if (ret)
		return ret;

	if (!fival->width || !fival->height)
		return -EINVAL;

	caps = inst->core->iris_platform_data->inst_caps;
	if (fival->width > caps->max_frame_width ||
	    fival->width < caps->min_frame_width ||
	    fival->height > caps->max_frame_height ||
	    fival->height < caps->min_frame_height)
		return -EINVAL;

	mbpf = NUM_MBS_PER_FRAME(fival->height, fival->width);
	fps = DIV_ROUND_UP(core->iris_platform_data->max_core_mbps, mbpf);

	fival->type = V4L2_FRMIVAL_TYPE_STEPWISE;
	fival->stepwise.min.numerator = 1;
	fival->stepwise.min.denominator =
			min_t(u32, fps, MAXIMUM_FPS);
	fival->stepwise.max.numerator = 1;
	fival->stepwise.max.denominator = 1;
	fival->stepwise.step.numerator = 1;
	fival->stepwise.step.denominator = MAXIMUM_FPS;

	return 0;
}

static int iris_querycap(struct file *filp, void *fh, struct v4l2_capability *cap)
{
	struct iris_inst *inst = iris_get_inst(filp);

	strscpy(cap->driver, IRIS_DRV_NAME, sizeof(cap->driver));

	if (inst->domain == DECODER)
		strscpy(cap->card, "Iris Decoder", sizeof(cap->card));
	else
		strscpy(cap->card, "Iris Encoder", sizeof(cap->card));

	return 0;
}

static int iris_g_selection(struct file *filp, void *fh, struct v4l2_selection *s)
{
	struct iris_inst *inst = iris_get_inst(filp);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    inst->domain == DECODER)
		return -EINVAL;

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    inst->domain == ENCODER)
		return -EINVAL;

	if (inst->domain == DECODER) {
		switch (s->target) {
		case V4L2_SEL_TGT_CROP_BOUNDS:
		case V4L2_SEL_TGT_CROP_DEFAULT:
		case V4L2_SEL_TGT_CROP:
		case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		case V4L2_SEL_TGT_COMPOSE_PADDED:
		case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		case V4L2_SEL_TGT_COMPOSE:
			s->r.left = inst->crop.left;
			s->r.top = inst->crop.top;
			s->r.width = inst->crop.width;
			s->r.height = inst->crop.height;
			break;
		default:
			return -EINVAL;
		}
	} else if (inst->domain == ENCODER) {
		switch (s->target) {
		case V4L2_SEL_TGT_CROP_BOUNDS:
		case V4L2_SEL_TGT_CROP_DEFAULT:
			s->r.width = inst->fmt_src->fmt.pix_mp.width;
			s->r.height = inst->fmt_src->fmt.pix_mp.height;
			break;
		case V4L2_SEL_TGT_CROP:
			s->r.width = inst->crop.width;
			s->r.height = inst->crop.height;
			break;
		default:
			return -EINVAL;
		}
		s->r.left = inst->crop.left;
		s->r.top = inst->crop.top;
	}

	return 0;
}

static int iris_s_selection(struct file *filp, void *fh, struct v4l2_selection *s)
{
	struct iris_inst *inst = iris_get_inst(filp);

	if (inst->domain == DECODER)
		return -EINVAL;
	else if (inst->domain == ENCODER)
		return iris_venc_s_selection(inst, s);

	return -EINVAL;
}

static int iris_subscribe_event(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub)
{
	struct iris_inst *inst = container_of(fh, struct iris_inst, fh);

	if (inst->domain == DECODER)
		return iris_vdec_subscribe_event(inst, sub);
	else if (inst->domain == ENCODER)
		return iris_venc_subscribe_event(inst, sub);

	return -EINVAL;
}

static int iris_s_parm(struct file *filp, void *fh, struct v4l2_streamparm *a)
{
	struct iris_inst *inst = iris_get_inst(filp);

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
	    a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	if (inst->domain == ENCODER)
		return iris_venc_s_param(inst, a);
	else
		return -EINVAL;
}

static int iris_g_parm(struct file *filp, void *fh, struct v4l2_streamparm *a)
{
	struct iris_inst *inst = iris_get_inst(filp);

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
	    a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	if (inst->domain == ENCODER)
		return iris_venc_g_param(inst, a);
	else
		return -EINVAL;
}

static int iris_dec_cmd(struct file *filp, void *fh,
			struct v4l2_decoder_cmd *dec)
{
	struct iris_inst *inst = iris_get_inst(filp);
	int ret = 0;

	mutex_lock(&inst->lock);

	ret = v4l2_m2m_ioctl_decoder_cmd(filp, fh, dec);
	if (ret)
		goto unlock;

	if (inst->state == IRIS_INST_DEINIT)
		goto unlock;

	if (!iris_allow_cmd(inst, dec->cmd)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (dec->cmd == V4L2_DEC_CMD_START)
		ret = iris_vdec_start_cmd(inst);
	else if (dec->cmd == V4L2_DEC_CMD_STOP)
		ret = iris_vdec_stop_cmd(inst);
	else
		ret = -EINVAL;

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static int iris_enc_cmd(struct file *filp, void *fh,
			struct v4l2_encoder_cmd *enc)
{
	struct iris_inst *inst = iris_get_inst(filp);
	int ret = 0;

	mutex_lock(&inst->lock);

	ret = v4l2_m2m_ioctl_encoder_cmd(filp, fh, enc);
	if (ret)
		goto unlock;

	if (inst->state == IRIS_INST_DEINIT)
		goto unlock;

	if (!iris_allow_cmd(inst, enc->cmd)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (enc->cmd == V4L2_ENC_CMD_START)
		ret = iris_venc_start_cmd(inst);
	else if (enc->cmd == V4L2_ENC_CMD_STOP)
		ret = iris_venc_stop_cmd(inst);
	else
		ret = -EINVAL;

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

static struct v4l2_file_operations iris_v4l2_file_ops = {
	.owner                          = THIS_MODULE,
	.open                           = iris_open,
	.release                        = iris_close,
	.unlocked_ioctl                 = video_ioctl2,
	.poll                           = v4l2_m2m_fop_poll,
	.mmap                           = v4l2_m2m_fop_mmap,
};

static const struct vb2_ops iris_vb2_ops = {
	.buf_init                       = iris_vb2_buf_init,
	.queue_setup                    = iris_vb2_queue_setup,
	.start_streaming                = iris_vb2_start_streaming,
	.stop_streaming                 = iris_vb2_stop_streaming,
	.buf_prepare                    = iris_vb2_buf_prepare,
	.buf_out_validate               = iris_vb2_buf_out_validate,
	.buf_queue                      = iris_vb2_buf_queue,
};

static const struct v4l2_ioctl_ops iris_v4l2_ioctl_ops_dec = {
	.vidioc_enum_fmt_vid_cap        = iris_enum_fmt,
	.vidioc_enum_fmt_vid_out        = iris_enum_fmt,
	.vidioc_try_fmt_vid_cap_mplane  = iris_try_fmt_vid_mplane,
	.vidioc_try_fmt_vid_out_mplane  = iris_try_fmt_vid_mplane,
	.vidioc_s_fmt_vid_cap_mplane    = iris_s_fmt_vid_mplane,
	.vidioc_s_fmt_vid_out_mplane    = iris_s_fmt_vid_mplane,
	.vidioc_g_fmt_vid_cap_mplane    = iris_g_fmt_vid_mplane,
	.vidioc_g_fmt_vid_out_mplane    = iris_g_fmt_vid_mplane,
	.vidioc_enum_framesizes         = iris_enum_framesizes,
	.vidioc_reqbufs                 = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf                = v4l2_m2m_ioctl_querybuf,
	.vidioc_create_bufs             = v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf             = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_expbuf                  = v4l2_m2m_ioctl_expbuf,
	.vidioc_qbuf                    = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf                   = v4l2_m2m_ioctl_dqbuf,
	.vidioc_remove_bufs             = v4l2_m2m_ioctl_remove_bufs,
	.vidioc_querycap                = iris_querycap,
	.vidioc_g_selection             = iris_g_selection,
	.vidioc_subscribe_event         = iris_subscribe_event,
	.vidioc_unsubscribe_event       = v4l2_event_unsubscribe,
	.vidioc_streamon                = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff               = v4l2_m2m_ioctl_streamoff,
	.vidioc_try_decoder_cmd         = v4l2_m2m_ioctl_try_decoder_cmd,
	.vidioc_decoder_cmd             = iris_dec_cmd,
};

static const struct v4l2_ioctl_ops iris_v4l2_ioctl_ops_enc = {
	.vidioc_enum_fmt_vid_cap        = iris_enum_fmt,
	.vidioc_enum_fmt_vid_out        = iris_enum_fmt,
	.vidioc_try_fmt_vid_cap_mplane  = iris_try_fmt_vid_mplane,
	.vidioc_try_fmt_vid_out_mplane  = iris_try_fmt_vid_mplane,
	.vidioc_s_fmt_vid_cap_mplane    = iris_s_fmt_vid_mplane,
	.vidioc_s_fmt_vid_out_mplane    = iris_s_fmt_vid_mplane,
	.vidioc_g_fmt_vid_cap_mplane    = iris_g_fmt_vid_mplane,
	.vidioc_g_fmt_vid_out_mplane    = iris_g_fmt_vid_mplane,
	.vidioc_enum_framesizes         = iris_enum_framesizes,
	.vidioc_enum_frameintervals     = iris_enum_frameintervals,
	.vidioc_querycap                = iris_querycap,
	.vidioc_subscribe_event         = iris_subscribe_event,
	.vidioc_unsubscribe_event       = v4l2_event_unsubscribe,
	.vidioc_g_selection             = iris_g_selection,
	.vidioc_s_selection             = iris_s_selection,
	.vidioc_s_parm                  = iris_s_parm,
	.vidioc_g_parm                  = iris_g_parm,
	.vidioc_streamon                = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff               = v4l2_m2m_ioctl_streamoff,
	.vidioc_reqbufs                 = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf                = v4l2_m2m_ioctl_querybuf,
	.vidioc_create_bufs             = v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf             = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_expbuf                  = v4l2_m2m_ioctl_expbuf,
	.vidioc_qbuf                    = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf                   = v4l2_m2m_ioctl_dqbuf,
	.vidioc_remove_bufs             = v4l2_m2m_ioctl_remove_bufs,
	.vidioc_try_encoder_cmd         = v4l2_m2m_ioctl_try_encoder_cmd,
	.vidioc_encoder_cmd             = iris_enc_cmd,
};

void iris_init_ops(struct iris_core *core)
{
	core->iris_v4l2_file_ops = &iris_v4l2_file_ops;
	core->iris_vb2_ops = &iris_vb2_ops;
	core->iris_v4l2_ioctl_ops_dec = &iris_v4l2_ioctl_ops_dec;
	core->iris_v4l2_ioctl_ops_enc = &iris_v4l2_ioctl_ops_enc;
}
