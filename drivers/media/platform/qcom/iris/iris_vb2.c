// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>

#include "iris_common.h"
#include "iris_instance.h"
#include "iris_vb2.h"
#include "iris_vdec.h"
#include "iris_venc.h"
#include "iris_power.h"

static int iris_check_inst_mbpf(struct iris_inst *inst)
{
	struct platform_inst_caps *caps;
	u32 mbpf, max_mbpf;

	caps = inst->core->iris_platform_data->inst_caps;
	max_mbpf = caps->max_mbpf;
	mbpf = iris_get_mbpf(inst);
	if (mbpf > max_mbpf)
		return -ENOMEM;

	return 0;
}

static int iris_check_resolution_supported(struct iris_inst *inst)
{
	u32 width, height, min_width, min_height, max_width, max_height;
	struct platform_inst_caps *caps;

	caps = inst->core->iris_platform_data->inst_caps;
	width = inst->fmt_src->fmt.pix_mp.width;
	height = inst->fmt_src->fmt.pix_mp.height;

	min_width = caps->min_frame_width;
	max_width = caps->max_frame_width;
	min_height = caps->min_frame_height;
	max_height = caps->max_frame_height;

	if (!(min_width <= width && width <= max_width) ||
	    !(min_height <= height && height <= max_height))
		return -EINVAL;

	return 0;
}

static int iris_check_session_supported(struct iris_inst *inst)
{
	struct iris_core *core = inst->core;
	struct iris_inst *instance = NULL;
	bool found = false;
	int ret;

	list_for_each_entry(instance, &core->instances, list) {
		if (instance == inst)
			found = true;
	}

	if (!found) {
		ret = -EINVAL;
		goto exit;
	}

	ret = iris_check_core_mbpf(inst);
	if (ret)
		goto exit;

	ret = iris_check_inst_mbpf(inst);
	if (ret)
		goto exit;

	ret = iris_check_resolution_supported(inst);
	if (ret)
		goto exit;

	return 0;
exit:
	dev_err(inst->core->dev, "current session not supported(%d)\n", ret);

	return ret;
}

int iris_vb2_buf_init(struct vb2_buffer *vb2)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb2);
	struct iris_buffer *buf = to_iris_buffer(vbuf);

	buf->device_addr = vb2_dma_contig_plane_dma_addr(vb2, 0);

	return 0;
}

int iris_vb2_queue_setup(struct vb2_queue *q,
			 unsigned int *num_buffers, unsigned int *num_planes,
			 unsigned int sizes[], struct device *alloc_devs[])
{
	struct iris_inst *inst;
	struct iris_core *core;
	struct v4l2_format *f;
	int ret = 0;

	inst = vb2_get_drv_priv(q);

	mutex_lock(&inst->lock);
	if (inst->state == IRIS_INST_ERROR) {
		ret = -EBUSY;
		goto unlock;
	}

	core = inst->core;
	f = V4L2_TYPE_IS_OUTPUT(q->type) ? inst->fmt_src : inst->fmt_dst;

	if (*num_planes) {
		if (*num_planes != f->fmt.pix_mp.num_planes ||
		    sizes[0] < f->fmt.pix_mp.plane_fmt[0].sizeimage)
			ret = -EINVAL;
		goto unlock;
	}

	ret = iris_check_session_supported(inst);
	if (ret)
		goto unlock;

	if (!inst->once_per_session_set) {
		inst->once_per_session_set = true;

		ret = core->hfi_ops->session_open(inst);
		if (ret) {
			ret = -EINVAL;
			dev_err(core->dev, "session open failed\n");
			goto unlock;
		}

		ret = iris_inst_change_state(inst, IRIS_INST_INIT);
		if (ret)
			goto unlock;
	}

	*num_planes = 1;
	sizes[0] = f->fmt.pix_mp.plane_fmt[0].sizeimage;

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}

int iris_vb2_start_streaming(struct vb2_queue *q, unsigned int count)
{
	enum iris_buffer_type buf_type;
	struct iris_inst *inst;
	int ret = 0;

	inst = vb2_get_drv_priv(q);

	mutex_lock(&inst->lock);
	if (inst->state == IRIS_INST_ERROR) {
		ret = -EBUSY;
		goto error;
	}

	if (!V4L2_TYPE_IS_OUTPUT(q->type) &&
	    !V4L2_TYPE_IS_CAPTURE(q->type)) {
		ret = -EINVAL;
		goto error;
	}

	iris_scale_power(inst);

	ret = iris_check_session_supported(inst);
	if (ret)
		goto error;

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		if (inst->domain == DECODER)
			ret = iris_vdec_streamon_input(inst);
		else
			ret = iris_venc_streamon_input(inst);
	} else if (V4L2_TYPE_IS_CAPTURE(q->type)) {
		if (inst->domain == DECODER)
			ret = iris_vdec_streamon_output(inst);
		else
			ret = iris_venc_streamon_output(inst);
	}
	if (ret)
		goto error;

	buf_type = iris_v4l2_type_to_driver(q->type);

	if (inst->domain == DECODER) {
		if (inst->state == IRIS_INST_STREAMING)
			ret = iris_queue_internal_deferred_buffers(inst, BUF_DPB);
		if (!ret)
			ret = iris_queue_deferred_buffers(inst, buf_type);
	} else {
		if (inst->state == IRIS_INST_STREAMING) {
			ret = iris_queue_deferred_buffers(inst, BUF_INPUT);
			if (!ret)
				ret = iris_queue_deferred_buffers(inst, BUF_OUTPUT);
		}
	}

	if (ret)
		goto error;

	mutex_unlock(&inst->lock);

	return ret;

error:
	iris_helper_buffers_done(inst, q->type, VB2_BUF_STATE_QUEUED);
	iris_inst_change_state(inst, IRIS_INST_ERROR);
	mutex_unlock(&inst->lock);

	return ret;
}

void iris_vb2_stop_streaming(struct vb2_queue *q)
{
	struct iris_inst *inst;
	int ret = 0;

	inst = vb2_get_drv_priv(q);

	if (V4L2_TYPE_IS_CAPTURE(q->type) && inst->state == IRIS_INST_INIT)
		return;

	mutex_lock(&inst->lock);

	if (!V4L2_TYPE_IS_OUTPUT(q->type) &&
	    !V4L2_TYPE_IS_CAPTURE(q->type))
		goto exit;

	ret = iris_session_streamoff(inst, q->type);
	if (ret)
		goto exit;

exit:
	iris_helper_buffers_done(inst, q->type, VB2_BUF_STATE_ERROR);
	if (ret)
		iris_inst_change_state(inst, IRIS_INST_ERROR);

	mutex_unlock(&inst->lock);
}

int iris_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct iris_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	if (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		if (vbuf->field == V4L2_FIELD_ANY)
			vbuf->field = V4L2_FIELD_NONE;
		if (vbuf->field != V4L2_FIELD_NONE)
			return -EINVAL;
	}

	if (!(inst->sub_state & IRIS_INST_SUB_DRC)) {
		if (vb->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
		    vb2_plane_size(vb, 0) < iris_get_buffer_size(inst, BUF_OUTPUT))
			return -EINVAL;
		if (vb->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
		    vb2_plane_size(vb, 0) < iris_get_buffer_size(inst, BUF_INPUT))
			return -EINVAL;
	}
	return 0;
}

int iris_vb2_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);

	v4l2_buf->field = V4L2_FIELD_NONE;

	return 0;
}

void iris_vb2_buf_queue(struct vb2_buffer *vb2)
{
	static const struct v4l2_event eos = { .type = V4L2_EVENT_EOS };
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb2);
	struct v4l2_m2m_ctx *m2m_ctx;
	struct iris_inst *inst;
	int ret = 0;

	inst = vb2_get_drv_priv(vb2->vb2_queue);

	mutex_lock(&inst->lock);
	if (inst->state == IRIS_INST_ERROR) {
		ret = -EBUSY;
		goto exit;
	}

	if (vbuf->field == V4L2_FIELD_ANY)
		vbuf->field = V4L2_FIELD_NONE;

	m2m_ctx = inst->m2m_ctx;

	if (!vb2->planes[0].bytesused && V4L2_TYPE_IS_OUTPUT(vb2->type)) {
		ret = -EINVAL;
		goto exit;
	}

	if (!inst->last_buffer_dequeued && V4L2_TYPE_IS_CAPTURE(vb2->vb2_queue->type)) {
		if ((inst->sub_state & IRIS_INST_SUB_DRC &&
		     inst->sub_state & IRIS_INST_SUB_DRC_LAST) ||
		    (inst->sub_state & IRIS_INST_SUB_DRAIN &&
		     inst->sub_state & IRIS_INST_SUB_DRAIN_LAST)) {
			vbuf->flags |= V4L2_BUF_FLAG_LAST;
			vbuf->sequence = inst->sequence_cap++;
			vbuf->field = V4L2_FIELD_NONE;
			vb2_set_plane_payload(vb2, 0, 0);
			v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);
			if (!v4l2_m2m_has_stopped(m2m_ctx)) {
				v4l2_event_queue_fh(&inst->fh, &eos);
				v4l2_m2m_mark_stopped(m2m_ctx);
			}
			inst->last_buffer_dequeued = true;
			goto exit;
		}
	}

	v4l2_m2m_buf_queue(m2m_ctx, vbuf);

	if (inst->domain == DECODER)
		ret = iris_vdec_qbuf(inst, vbuf);
	else
		ret = iris_venc_qbuf(inst, vbuf);

exit:
	if (ret) {
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
	}
	mutex_unlock(&inst->lock);
}
