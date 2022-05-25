// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-vmalloc.h>
#include "vpu.h"
#include "vpu_core.h"
#include "vpu_v4l2.h"
#include "vpu_msgs.h"
#include "vpu_helpers.h"

void vpu_inst_lock(struct vpu_inst *inst)
{
	mutex_lock(&inst->lock);
}

void vpu_inst_unlock(struct vpu_inst *inst)
{
	mutex_unlock(&inst->lock);
}

dma_addr_t vpu_get_vb_phy_addr(struct vb2_buffer *vb, u32 plane_no)
{
	if (plane_no >= vb->num_planes)
		return 0;
	return vb2_dma_contig_plane_dma_addr(vb, plane_no) +
			vb->planes[plane_no].data_offset;
}

unsigned int vpu_get_vb_length(struct vb2_buffer *vb, u32 plane_no)
{
	if (plane_no >= vb->num_planes)
		return 0;
	return vb2_plane_size(vb, plane_no) - vb->planes[plane_no].data_offset;
}

void vpu_set_buffer_state(struct vb2_v4l2_buffer *vbuf, unsigned int state)
{
	struct vpu_vb2_buffer *vpu_buf = to_vpu_vb2_buffer(vbuf);

	vpu_buf->state = state;
}

unsigned int vpu_get_buffer_state(struct vb2_v4l2_buffer *vbuf)
{
	struct vpu_vb2_buffer *vpu_buf = to_vpu_vb2_buffer(vbuf);

	return vpu_buf->state;
}

void vpu_v4l2_set_error(struct vpu_inst *inst)
{
	struct vb2_queue *src_q;
	struct vb2_queue *dst_q;

	vpu_inst_lock(inst);
	dev_err(inst->dev, "some error occurs in codec\n");
	if (inst->fh.m2m_ctx) {
		src_q = v4l2_m2m_get_src_vq(inst->fh.m2m_ctx);
		dst_q = v4l2_m2m_get_dst_vq(inst->fh.m2m_ctx);
		src_q->error = 1;
		dst_q->error = 1;
		wake_up(&src_q->done_wq);
		wake_up(&dst_q->done_wq);
	}
	vpu_inst_unlock(inst);
}

int vpu_notify_eos(struct vpu_inst *inst)
{
	static const struct v4l2_event ev = {
		.id = 0,
		.type = V4L2_EVENT_EOS
	};

	vpu_trace(inst->dev, "[%d]\n", inst->id);
	v4l2_event_queue_fh(&inst->fh, &ev);

	return 0;
}

int vpu_notify_source_change(struct vpu_inst *inst)
{
	static const struct v4l2_event ev = {
		.id = 0,
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION
	};

	vpu_trace(inst->dev, "[%d]\n", inst->id);
	v4l2_event_queue_fh(&inst->fh, &ev);
	return 0;
}

int vpu_set_last_buffer_dequeued(struct vpu_inst *inst)
{
	struct vb2_queue *q;

	if (!inst || !inst->fh.m2m_ctx)
		return -EINVAL;

	q = v4l2_m2m_get_dst_vq(inst->fh.m2m_ctx);
	if (!list_empty(&q->done_list))
		return -EINVAL;

	if (q->last_buffer_dequeued)
		return 0;
	vpu_trace(inst->dev, "last buffer dequeued\n");
	q->last_buffer_dequeued = true;
	wake_up(&q->done_wq);
	vpu_notify_eos(inst);
	return 0;
}

bool vpu_is_source_empty(struct vpu_inst *inst)
{
	struct v4l2_m2m_buffer *buf = NULL;

	if (!inst->fh.m2m_ctx)
		return true;
	v4l2_m2m_for_each_src_buf(inst->fh.m2m_ctx, buf) {
		if (vpu_get_buffer_state(&buf->vb) == VPU_BUF_STATE_IDLE)
			return false;
	}
	return true;
}

const struct vpu_format *vpu_try_fmt_common(struct vpu_inst *inst, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	u32 type = f->type;
	u32 stride = 1;
	u32 bytesperline;
	u32 sizeimage;
	const struct vpu_format *fmt;
	const struct vpu_core_resources *res;
	int i;

	fmt = vpu_helper_find_format(inst, type, pixmp->pixelformat);
	if (!fmt) {
		fmt = vpu_helper_enum_format(inst, type, 0);
		if (!fmt)
			return NULL;
		pixmp->pixelformat = fmt->pixfmt;
	}

	res = vpu_get_resource(inst);
	if (res)
		stride = res->stride;
	if (pixmp->width)
		pixmp->width = vpu_helper_valid_frame_width(inst, pixmp->width);
	if (pixmp->height)
		pixmp->height = vpu_helper_valid_frame_height(inst, pixmp->height);
	pixmp->flags = fmt->flags;
	pixmp->num_planes = fmt->num_planes;
	if (pixmp->field == V4L2_FIELD_ANY)
		pixmp->field = V4L2_FIELD_NONE;
	for (i = 0; i < pixmp->num_planes; i++) {
		bytesperline = max_t(s32, pixmp->plane_fmt[i].bytesperline, 0);
		sizeimage = vpu_helper_get_plane_size(pixmp->pixelformat,
						      pixmp->width,
						      pixmp->height,
						      i,
						      stride,
						      pixmp->field > V4L2_FIELD_NONE ? 1 : 0,
						      &bytesperline);
		sizeimage = max_t(s32, pixmp->plane_fmt[i].sizeimage, sizeimage);
		pixmp->plane_fmt[i].bytesperline = bytesperline;
		pixmp->plane_fmt[i].sizeimage = sizeimage;
	}

	return fmt;
}

static bool vpu_check_ready(struct vpu_inst *inst, u32 type)
{
	if (!inst)
		return false;
	if (inst->state == VPU_CODEC_STATE_DEINIT || inst->id < 0)
		return false;
	if (!inst->ops->check_ready)
		return true;
	return call_vop(inst, check_ready, type);
}

int vpu_process_output_buffer(struct vpu_inst *inst)
{
	struct v4l2_m2m_buffer *buf = NULL;
	struct vb2_v4l2_buffer *vbuf = NULL;

	if (!inst || !inst->fh.m2m_ctx)
		return -EINVAL;

	if (!vpu_check_ready(inst, inst->out_format.type))
		return -EINVAL;

	v4l2_m2m_for_each_src_buf(inst->fh.m2m_ctx, buf) {
		vbuf = &buf->vb;
		if (vpu_get_buffer_state(vbuf) == VPU_BUF_STATE_IDLE)
			break;
		vbuf = NULL;
	}

	if (!vbuf)
		return -EINVAL;

	dev_dbg(inst->dev, "[%d]frame id = %d / %d\n",
		inst->id, vbuf->sequence, inst->sequence);
	return call_vop(inst, process_output, &vbuf->vb2_buf);
}

int vpu_process_capture_buffer(struct vpu_inst *inst)
{
	struct v4l2_m2m_buffer *buf = NULL;
	struct vb2_v4l2_buffer *vbuf = NULL;

	if (!inst || !inst->fh.m2m_ctx)
		return -EINVAL;

	if (!vpu_check_ready(inst, inst->cap_format.type))
		return -EINVAL;

	v4l2_m2m_for_each_dst_buf(inst->fh.m2m_ctx, buf) {
		vbuf = &buf->vb;
		if (vpu_get_buffer_state(vbuf) == VPU_BUF_STATE_IDLE)
			break;
		vbuf = NULL;
	}
	if (!vbuf)
		return -EINVAL;

	return call_vop(inst, process_capture, &vbuf->vb2_buf);
}

struct vb2_v4l2_buffer *vpu_next_src_buf(struct vpu_inst *inst)
{
	struct vb2_v4l2_buffer *src_buf = v4l2_m2m_next_src_buf(inst->fh.m2m_ctx);

	if (!src_buf || vpu_get_buffer_state(src_buf) == VPU_BUF_STATE_IDLE)
		return NULL;

	while (vpu_vb_is_codecconfig(src_buf)) {
		v4l2_m2m_src_buf_remove(inst->fh.m2m_ctx);
		vpu_set_buffer_state(src_buf, VPU_BUF_STATE_IDLE);
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);

		src_buf = v4l2_m2m_next_src_buf(inst->fh.m2m_ctx);
		if (!src_buf || vpu_get_buffer_state(src_buf) == VPU_BUF_STATE_IDLE)
			return NULL;
	}

	return src_buf;
}

void vpu_skip_frame(struct vpu_inst *inst, int count)
{
	struct vb2_v4l2_buffer *src_buf;
	enum vb2_buffer_state state;
	int i = 0;

	if (count <= 0)
		return;

	while (i < count) {
		src_buf = v4l2_m2m_src_buf_remove(inst->fh.m2m_ctx);
		if (!src_buf || vpu_get_buffer_state(src_buf) == VPU_BUF_STATE_IDLE)
			return;
		if (vpu_get_buffer_state(src_buf) == VPU_BUF_STATE_DECODED)
			state = VB2_BUF_STATE_DONE;
		else
			state = VB2_BUF_STATE_ERROR;
		i++;
		vpu_set_buffer_state(src_buf, VPU_BUF_STATE_IDLE);
		v4l2_m2m_buf_done(src_buf, state);
	}
}

struct vb2_v4l2_buffer *vpu_find_buf_by_sequence(struct vpu_inst *inst, u32 type, u32 sequence)
{
	struct v4l2_m2m_buffer *buf = NULL;
	struct vb2_v4l2_buffer *vbuf = NULL;

	if (!inst || !inst->fh.m2m_ctx)
		return NULL;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		v4l2_m2m_for_each_src_buf(inst->fh.m2m_ctx, buf) {
			vbuf = &buf->vb;
			if (vbuf->sequence == sequence)
				break;
			vbuf = NULL;
		}
	} else {
		v4l2_m2m_for_each_dst_buf(inst->fh.m2m_ctx, buf) {
			vbuf = &buf->vb;
			if (vbuf->sequence == sequence)
				break;
			vbuf = NULL;
		}
	}

	return vbuf;
}

struct vb2_v4l2_buffer *vpu_find_buf_by_idx(struct vpu_inst *inst, u32 type, u32 idx)
{
	struct v4l2_m2m_buffer *buf = NULL;
	struct vb2_v4l2_buffer *vbuf = NULL;

	if (!inst || !inst->fh.m2m_ctx)
		return NULL;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		v4l2_m2m_for_each_src_buf(inst->fh.m2m_ctx, buf) {
			vbuf = &buf->vb;
			if (vbuf->vb2_buf.index == idx)
				break;
			vbuf = NULL;
		}
	} else {
		v4l2_m2m_for_each_dst_buf(inst->fh.m2m_ctx, buf) {
			vbuf = &buf->vb;
			if (vbuf->vb2_buf.index == idx)
				break;
			vbuf = NULL;
		}
	}

	return vbuf;
}

int vpu_get_num_buffers(struct vpu_inst *inst, u32 type)
{
	struct vb2_queue *q;

	if (!inst || !inst->fh.m2m_ctx)
		return -EINVAL;

	if (V4L2_TYPE_IS_OUTPUT(type))
		q = v4l2_m2m_get_src_vq(inst->fh.m2m_ctx);
	else
		q = v4l2_m2m_get_dst_vq(inst->fh.m2m_ctx);

	return q->num_buffers;
}

static void vpu_m2m_device_run(void *priv)
{
}

static void vpu_m2m_job_abort(void *priv)
{
	struct vpu_inst *inst = priv;
	struct v4l2_m2m_ctx *m2m_ctx = inst->fh.m2m_ctx;

	v4l2_m2m_job_finish(m2m_ctx->m2m_dev, m2m_ctx);
}

static const struct v4l2_m2m_ops vpu_m2m_ops = {
	.device_run = vpu_m2m_device_run,
	.job_abort = vpu_m2m_job_abort
};

static int vpu_vb2_queue_setup(struct vb2_queue *vq,
			       unsigned int *buf_count,
			       unsigned int *plane_count,
			       unsigned int psize[],
			       struct device *allocators[])
{
	struct vpu_inst *inst = vb2_get_drv_priv(vq);
	struct vpu_format *cur_fmt;
	int i;

	cur_fmt = vpu_get_format(inst, vq->type);

	if (*plane_count) {
		if (*plane_count != cur_fmt->num_planes)
			return -EINVAL;
		for (i = 0; i < cur_fmt->num_planes; i++) {
			if (psize[i] < cur_fmt->sizeimage[i])
				return -EINVAL;
		}
		return 0;
	}

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		*buf_count = max_t(unsigned int, *buf_count, inst->min_buffer_out);
	else
		*buf_count = max_t(unsigned int, *buf_count, inst->min_buffer_cap);
	*plane_count = cur_fmt->num_planes;
	for (i = 0; i < cur_fmt->num_planes; i++)
		psize[i] = cur_fmt->sizeimage[i];

	return 0;
}

static int vpu_vb2_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vpu_set_buffer_state(vbuf, VPU_BUF_STATE_IDLE);
	return 0;
}

static int vpu_vb2_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;

	return 0;
}

static int vpu_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct vpu_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpu_format *cur_fmt;
	u32 i;

	cur_fmt = vpu_get_format(inst, vb->type);
	for (i = 0; i < cur_fmt->num_planes; i++) {
		if (vpu_get_vb_length(vb, i) < cur_fmt->sizeimage[i]) {
			dev_dbg(inst->dev, "[%d] %s buf[%d] is invalid\n",
				inst->id, vpu_type_name(vb->type), vb->index);
			vpu_set_buffer_state(vbuf, VPU_BUF_STATE_ERROR);
		}
	}

	return 0;
}

static void vpu_vb2_buf_finish(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpu_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_queue *q = vb->vb2_queue;

	if (vbuf->flags & V4L2_BUF_FLAG_LAST)
		vpu_notify_eos(inst);

	if (list_empty(&q->done_list))
		call_void_vop(inst, on_queue_empty, q->type);
}

void vpu_vb2_buffers_return(struct vpu_inst *inst, unsigned int type, enum vb2_buffer_state state)
{
	struct vb2_v4l2_buffer *buf;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		while ((buf = v4l2_m2m_src_buf_remove(inst->fh.m2m_ctx))) {
			vpu_set_buffer_state(buf, VPU_BUF_STATE_IDLE);
			v4l2_m2m_buf_done(buf, state);
		}
	} else {
		while ((buf = v4l2_m2m_dst_buf_remove(inst->fh.m2m_ctx))) {
			vpu_set_buffer_state(buf, VPU_BUF_STATE_IDLE);
			v4l2_m2m_buf_done(buf, state);
		}
	}
}

static int vpu_vb2_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct vpu_inst *inst = vb2_get_drv_priv(q);
	struct vpu_format *fmt = vpu_get_format(inst, q->type);
	int ret;

	vpu_inst_unlock(inst);
	ret = vpu_inst_register(inst);
	vpu_inst_lock(inst);
	if (ret) {
		vpu_vb2_buffers_return(inst, q->type, VB2_BUF_STATE_QUEUED);
		return ret;
	}

	vpu_trace(inst->dev, "[%d] %s %c%c%c%c %dx%d %u(%u) %u(%u) %u(%u) %d\n",
		  inst->id, vpu_type_name(q->type),
		  fmt->pixfmt,
		  fmt->pixfmt >> 8,
		  fmt->pixfmt >> 16,
		  fmt->pixfmt >> 24,
		  fmt->width, fmt->height,
		  fmt->sizeimage[0], fmt->bytesperline[0],
		  fmt->sizeimage[1], fmt->bytesperline[1],
		  fmt->sizeimage[2], fmt->bytesperline[2],
		  q->num_buffers);
	call_void_vop(inst, start, q->type);
	vb2_clear_last_buffer_dequeued(q);

	return 0;
}

static void vpu_vb2_stop_streaming(struct vb2_queue *q)
{
	struct vpu_inst *inst = vb2_get_drv_priv(q);

	vpu_trace(inst->dev, "[%d] %s\n", inst->id, vpu_type_name(q->type));

	call_void_vop(inst, stop, q->type);
	vpu_vb2_buffers_return(inst, q->type, VB2_BUF_STATE_ERROR);
	if (V4L2_TYPE_IS_OUTPUT(q->type))
		inst->sequence = 0;
}

static void vpu_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpu_inst *inst = vb2_get_drv_priv(vb->vb2_queue);

	if (V4L2_TYPE_IS_OUTPUT(vb->type))
		vbuf->sequence = inst->sequence++;

	v4l2_m2m_buf_queue(inst->fh.m2m_ctx, vbuf);
	vpu_process_output_buffer(inst);
	vpu_process_capture_buffer(inst);
}

static const struct vb2_ops vpu_vb2_ops = {
	.queue_setup        = vpu_vb2_queue_setup,
	.buf_init           = vpu_vb2_buf_init,
	.buf_out_validate   = vpu_vb2_buf_out_validate,
	.buf_prepare        = vpu_vb2_buf_prepare,
	.buf_finish         = vpu_vb2_buf_finish,
	.start_streaming    = vpu_vb2_start_streaming,
	.stop_streaming     = vpu_vb2_stop_streaming,
	.buf_queue          = vpu_vb2_buf_queue,
	.wait_prepare       = vb2_ops_wait_prepare,
	.wait_finish        = vb2_ops_wait_finish,
};

static int vpu_m2m_queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	struct vpu_inst *inst = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	inst->out_format.type = src_vq->type;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->ops = &vpu_vb2_ops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	if (inst->type == VPU_CORE_TYPE_DEC && inst->use_stream_buffer)
		src_vq->mem_ops = &vb2_vmalloc_memops;
	src_vq->drv_priv = inst;
	src_vq->buf_struct_size = sizeof(struct vpu_vb2_buffer);
	src_vq->min_buffers_needed = 1;
	src_vq->dev = inst->vpu->dev;
	src_vq->lock = &inst->lock;
	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	inst->cap_format.type = dst_vq->type;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->ops = &vpu_vb2_ops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	if (inst->type == VPU_CORE_TYPE_ENC && inst->use_stream_buffer)
		dst_vq->mem_ops = &vb2_vmalloc_memops;
	dst_vq->drv_priv = inst;
	dst_vq->buf_struct_size = sizeof(struct vpu_vb2_buffer);
	dst_vq->min_buffers_needed = 1;
	dst_vq->dev = inst->vpu->dev;
	dst_vq->lock = &inst->lock;
	ret = vb2_queue_init(dst_vq);
	if (ret) {
		vb2_queue_release(src_vq);
		return ret;
	}

	return 0;
}

static int vpu_v4l2_release(struct vpu_inst *inst)
{
	vpu_trace(inst->vpu->dev, "%p\n", inst);

	vpu_release_core(inst->core);
	put_device(inst->dev);

	if (inst->workqueue) {
		cancel_work_sync(&inst->msg_work);
		destroy_workqueue(inst->workqueue);
		inst->workqueue = NULL;
	}

	v4l2_ctrl_handler_free(&inst->ctrl_handler);
	mutex_destroy(&inst->lock);
	v4l2_fh_del(&inst->fh);
	v4l2_fh_exit(&inst->fh);

	call_void_vop(inst, cleanup);

	return 0;
}

int vpu_v4l2_open(struct file *file, struct vpu_inst *inst)
{
	struct vpu_dev *vpu = video_drvdata(file);
	struct vpu_func *func;
	int ret = 0;

	if (!inst || !inst->ops)
		return -EINVAL;

	if (inst->type == VPU_CORE_TYPE_ENC)
		func = &vpu->encoder;
	else
		func = &vpu->decoder;

	atomic_set(&inst->ref_count, 0);
	vpu_inst_get(inst);
	inst->vpu = vpu;
	inst->core = vpu_request_core(vpu, inst->type);
	if (inst->core)
		inst->dev = get_device(inst->core->dev);
	mutex_init(&inst->lock);
	INIT_LIST_HEAD(&inst->cmd_q);
	inst->id = VPU_INST_NULL_ID;
	inst->release = vpu_v4l2_release;
	inst->pid = current->pid;
	inst->tgid = current->tgid;
	inst->min_buffer_cap = 2;
	inst->min_buffer_out = 2;
	v4l2_fh_init(&inst->fh, func->vfd);
	v4l2_fh_add(&inst->fh);

	ret = call_vop(inst, ctrl_init);
	if (ret)
		goto error;

	inst->fh.m2m_ctx = v4l2_m2m_ctx_init(func->m2m_dev, inst, vpu_m2m_queue_init);
	if (IS_ERR(inst->fh.m2m_ctx)) {
		dev_err(vpu->dev, "v4l2_m2m_ctx_init fail\n");
		ret = PTR_ERR(inst->fh.m2m_ctx);
		goto error;
	}

	inst->fh.ctrl_handler = &inst->ctrl_handler;
	file->private_data = &inst->fh;
	inst->state = VPU_CODEC_STATE_DEINIT;
	inst->workqueue = alloc_workqueue("vpu_inst", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (inst->workqueue) {
		INIT_WORK(&inst->msg_work, vpu_inst_run_work);
		ret = kfifo_init(&inst->msg_fifo,
				 inst->msg_buffer,
				 rounddown_pow_of_two(sizeof(inst->msg_buffer)));
		if (ret) {
			destroy_workqueue(inst->workqueue);
			inst->workqueue = NULL;
		}
	}
	vpu_trace(vpu->dev, "tgid = %d, pid = %d, type = %s, inst = %p\n",
		  inst->tgid, inst->pid, vpu_core_type_desc(inst->type), inst);

	return 0;
error:
	vpu_inst_put(inst);
	return ret;
}

int vpu_v4l2_close(struct file *file)
{
	struct vpu_dev *vpu = video_drvdata(file);
	struct vpu_inst *inst = to_inst(file);

	vpu_trace(vpu->dev, "tgid = %d, pid = %d, inst = %p\n", inst->tgid, inst->pid, inst);

	vpu_inst_lock(inst);
	if (inst->fh.m2m_ctx) {
		v4l2_m2m_ctx_release(inst->fh.m2m_ctx);
		inst->fh.m2m_ctx = NULL;
	}
	vpu_inst_unlock(inst);

	call_void_vop(inst, release);
	vpu_inst_unregister(inst);
	vpu_inst_put(inst);

	return 0;
}

int vpu_add_func(struct vpu_dev *vpu, struct vpu_func *func)
{
	struct video_device *vfd;
	int ret;

	if (!vpu || !func)
		return -EINVAL;

	if (func->vfd)
		return 0;

	func->m2m_dev = v4l2_m2m_init(&vpu_m2m_ops);
	if (IS_ERR(func->m2m_dev)) {
		dev_err(vpu->dev, "v4l2_m2m_init fail\n");
		func->vfd = NULL;
		return PTR_ERR(func->m2m_dev);
	}

	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_m2m_release(func->m2m_dev);
		dev_err(vpu->dev, "alloc vpu decoder video device fail\n");
		return -ENOMEM;
	}
	vfd->release = video_device_release;
	vfd->vfl_dir = VFL_DIR_M2M;
	vfd->v4l2_dev = &vpu->v4l2_dev;
	vfd->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	if (func->type == VPU_CORE_TYPE_ENC) {
		strscpy(vfd->name, "amphion-vpu-encoder", sizeof(vfd->name));
		vfd->fops = venc_get_fops();
		vfd->ioctl_ops = venc_get_ioctl_ops();
	} else {
		strscpy(vfd->name, "amphion-vpu-decoder", sizeof(vfd->name));
		vfd->fops = vdec_get_fops();
		vfd->ioctl_ops = vdec_get_ioctl_ops();
	}

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret) {
		video_device_release(vfd);
		v4l2_m2m_release(func->m2m_dev);
		return ret;
	}
	video_set_drvdata(vfd, vpu);
	func->vfd = vfd;

	ret = v4l2_m2m_register_media_controller(func->m2m_dev, func->vfd, func->function);
	if (ret) {
		v4l2_m2m_release(func->m2m_dev);
		func->m2m_dev = NULL;
		video_unregister_device(func->vfd);
		func->vfd = NULL;
		return ret;
	}

	return 0;
}

void vpu_remove_func(struct vpu_func *func)
{
	if (!func)
		return;

	if (func->m2m_dev) {
		v4l2_m2m_unregister_media_controller(func->m2m_dev);
		v4l2_m2m_release(func->m2m_dev);
		func->m2m_dev = NULL;
	}
	if (func->vfd) {
		video_unregister_device(func->vfd);
		func->vfd = NULL;
	}
}
