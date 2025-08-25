// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <media/v4l2-mem2mem.h>

#include "iris_common.h"
#include "iris_ctrls.h"
#include "iris_instance.h"
#include "iris_power.h"

int iris_vb2_buffer_to_driver(struct vb2_buffer *vb2, struct iris_buffer *buf)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb2);

	buf->type = iris_v4l2_type_to_driver(vb2->type);
	buf->index = vb2->index;
	buf->fd = vb2->planes[0].m.fd;
	buf->buffer_size = vb2->planes[0].length;
	buf->data_offset = vb2->planes[0].data_offset;
	buf->data_size = vb2->planes[0].bytesused - vb2->planes[0].data_offset;
	buf->flags = vbuf->flags;
	buf->timestamp = vb2->timestamp;
	buf->attr = 0;

	return 0;
}

void iris_set_ts_metadata(struct iris_inst *inst, struct vb2_v4l2_buffer *vbuf)
{
	u32 mask = V4L2_BUF_FLAG_TIMECODE | V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	struct vb2_buffer *vb = &vbuf->vb2_buf;
	u64 ts_us = vb->timestamp;

	if (inst->metadata_idx >= ARRAY_SIZE(inst->tss))
		inst->metadata_idx = 0;

	do_div(ts_us, NSEC_PER_USEC);

	inst->tss[inst->metadata_idx].flags = vbuf->flags & mask;
	inst->tss[inst->metadata_idx].tc = vbuf->timecode;
	inst->tss[inst->metadata_idx].ts_us = ts_us;
	inst->tss[inst->metadata_idx].ts_ns = vb->timestamp;

	inst->metadata_idx++;
}

int iris_process_streamon_input(struct iris_inst *inst)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	enum iris_inst_sub_state set_sub_state = 0;
	int ret;

	iris_scale_power(inst);

	ret = hfi_ops->session_start(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	if (inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE) {
		ret = iris_inst_change_sub_state(inst, IRIS_INST_SUB_INPUT_PAUSE, 0);
		if (ret)
			return ret;
	}

	if (inst->domain == DECODER &&
	    (inst->sub_state & IRIS_INST_SUB_DRC ||
	     inst->sub_state & IRIS_INST_SUB_DRAIN ||
	     inst->sub_state & IRIS_INST_SUB_FIRST_IPSC)) {
		if (!(inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE)) {
			if (hfi_ops->session_pause) {
				ret = hfi_ops->session_pause(inst,
							     V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
				if (ret)
					return ret;
			}
			set_sub_state = IRIS_INST_SUB_INPUT_PAUSE;
		}
	}

	ret = iris_inst_state_change_streamon(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	inst->last_buffer_dequeued = false;

	return iris_inst_change_sub_state(inst, 0, set_sub_state);
}

int iris_process_streamon_output(struct iris_inst *inst)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	bool drain_active = false, drc_active = false;
	enum iris_inst_sub_state clear_sub_state = 0;
	int ret = 0;

	iris_scale_power(inst);

	drain_active = inst->sub_state & IRIS_INST_SUB_DRAIN &&
		inst->sub_state & IRIS_INST_SUB_DRAIN_LAST;

	drc_active = inst->sub_state & IRIS_INST_SUB_DRC &&
		inst->sub_state & IRIS_INST_SUB_DRC_LAST;

	if (drc_active)
		clear_sub_state = IRIS_INST_SUB_DRC | IRIS_INST_SUB_DRC_LAST;
	else if (drain_active)
		clear_sub_state = IRIS_INST_SUB_DRAIN | IRIS_INST_SUB_DRAIN_LAST;

	if (inst->domain == DECODER && inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE) {
		ret = iris_alloc_and_queue_input_int_bufs(inst);
		if (ret)
			return ret;
		ret = iris_set_stage(inst, STAGE);
		if (ret)
			return ret;
		ret = iris_set_pipe(inst, PIPE);
		if (ret)
			return ret;
	}

	if (inst->state == IRIS_INST_INPUT_STREAMING &&
	    inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE) {
		if (!drain_active)
			ret = hfi_ops->session_resume_drc(inst,
							  V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
		else if (hfi_ops->session_resume_drain)
			ret = hfi_ops->session_resume_drain(inst,
							    V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
		if (ret)
			return ret;
		clear_sub_state |= IRIS_INST_SUB_INPUT_PAUSE;
	}

	if (inst->sub_state & IRIS_INST_SUB_FIRST_IPSC)
		clear_sub_state |= IRIS_INST_SUB_FIRST_IPSC;

	ret = hfi_ops->session_start(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (ret)
		return ret;

	if (inst->sub_state & IRIS_INST_SUB_OUTPUT_PAUSE)
		clear_sub_state |= IRIS_INST_SUB_OUTPUT_PAUSE;

	ret = iris_inst_state_change_streamon(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (ret)
		return ret;

	inst->last_buffer_dequeued = false;

	return iris_inst_change_sub_state(inst, clear_sub_state, 0);
}

static void iris_flush_deferred_buffers(struct iris_inst *inst,
					enum iris_buffer_type type)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_m2m_buffer *buffer, *n;
	struct iris_buffer *buf;

	if (type == BUF_INPUT) {
		v4l2_m2m_for_each_src_buf_safe(m2m_ctx, buffer, n) {
			buf = to_iris_buffer(&buffer->vb);
			if (buf->attr & BUF_ATTR_DEFERRED) {
				if (!(buf->attr & BUF_ATTR_BUFFER_DONE)) {
					buf->attr |= BUF_ATTR_BUFFER_DONE;
					buf->data_size = 0;
					iris_vb2_buffer_done(inst, buf);
				}
			}
		}
	} else {
		v4l2_m2m_for_each_dst_buf_safe(m2m_ctx, buffer, n) {
			buf = to_iris_buffer(&buffer->vb);
			if (buf->attr & BUF_ATTR_DEFERRED) {
				if (!(buf->attr & BUF_ATTR_BUFFER_DONE)) {
					buf->attr |= BUF_ATTR_BUFFER_DONE;
					buf->data_size = 0;
					iris_vb2_buffer_done(inst, buf);
				}
			}
		}
	}
}

static void iris_kill_session(struct iris_inst *inst)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;

	if (!inst->session_id)
		return;

	hfi_ops->session_close(inst);
	iris_inst_change_state(inst, IRIS_INST_ERROR);
}

int iris_session_streamoff(struct iris_inst *inst, u32 plane)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	enum iris_buffer_type buffer_type;
	int ret;

	switch (plane) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		buffer_type = BUF_INPUT;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		buffer_type = BUF_OUTPUT;
		break;
	default:
		return -EINVAL;
	}

	ret = hfi_ops->session_stop(inst, plane);
	if (ret)
		goto error;

	ret = iris_inst_state_change_streamoff(inst, plane);
	if (ret)
		goto error;

	iris_flush_deferred_buffers(inst, buffer_type);

	return 0;

error:
	iris_kill_session(inst);
	iris_flush_deferred_buffers(inst, buffer_type);

	return ret;
}
