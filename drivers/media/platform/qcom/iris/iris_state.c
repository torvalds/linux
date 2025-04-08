// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <media/v4l2-mem2mem.h>

#include "iris_instance.h"

static bool iris_allow_inst_state_change(struct iris_inst *inst,
					 enum iris_inst_state req_state)
{
	switch (inst->state) {
	case IRIS_INST_INIT:
		if (req_state == IRIS_INST_INPUT_STREAMING ||
		    req_state == IRIS_INST_OUTPUT_STREAMING ||
		    req_state == IRIS_INST_DEINIT)
			return true;
		return false;
	case IRIS_INST_INPUT_STREAMING:
		if (req_state == IRIS_INST_INIT ||
		    req_state == IRIS_INST_STREAMING ||
		    req_state == IRIS_INST_DEINIT)
			return true;
		return false;
	case IRIS_INST_OUTPUT_STREAMING:
		if (req_state == IRIS_INST_INIT ||
		    req_state == IRIS_INST_STREAMING ||
		    req_state == IRIS_INST_DEINIT)
			return true;
		return false;
	case IRIS_INST_STREAMING:
		if (req_state == IRIS_INST_INPUT_STREAMING ||
		    req_state == IRIS_INST_OUTPUT_STREAMING ||
		    req_state == IRIS_INST_DEINIT)
			return true;
		return false;
	case IRIS_INST_DEINIT:
		if (req_state == IRIS_INST_INIT)
			return true;
		return false;
	default:
		return false;
	}
}

int iris_inst_change_state(struct iris_inst *inst,
			   enum iris_inst_state request_state)
{
	if (inst->state == IRIS_INST_ERROR)
		return 0;

	if (inst->state == request_state)
		return 0;

	if (request_state == IRIS_INST_ERROR)
		goto change_state;

	if (!iris_allow_inst_state_change(inst, request_state))
		return -EINVAL;

change_state:
	inst->state = request_state;
	dev_dbg(inst->core->dev, "state changed from %x to %x\n",
		inst->state, request_state);

	return 0;
}

int iris_inst_state_change_streamon(struct iris_inst *inst, u32 plane)
{
	enum iris_inst_state new_state = IRIS_INST_ERROR;

	if (V4L2_TYPE_IS_OUTPUT(plane)) {
		if (inst->state == IRIS_INST_INIT)
			new_state = IRIS_INST_INPUT_STREAMING;
		else if (inst->state == IRIS_INST_OUTPUT_STREAMING)
			new_state = IRIS_INST_STREAMING;
	} else if (V4L2_TYPE_IS_CAPTURE(plane)) {
		if (inst->state == IRIS_INST_INIT)
			new_state = IRIS_INST_OUTPUT_STREAMING;
		else if (inst->state == IRIS_INST_INPUT_STREAMING)
			new_state = IRIS_INST_STREAMING;
	}

	return iris_inst_change_state(inst, new_state);
}

int iris_inst_state_change_streamoff(struct iris_inst *inst, u32 plane)
{
	enum iris_inst_state new_state = IRIS_INST_ERROR;

	if (V4L2_TYPE_IS_OUTPUT(plane)) {
		if (inst->state == IRIS_INST_INPUT_STREAMING)
			new_state = IRIS_INST_INIT;
		else if (inst->state == IRIS_INST_STREAMING)
			new_state = IRIS_INST_OUTPUT_STREAMING;
	} else if (V4L2_TYPE_IS_CAPTURE(plane)) {
		if (inst->state == IRIS_INST_OUTPUT_STREAMING)
			new_state = IRIS_INST_INIT;
		else if (inst->state == IRIS_INST_STREAMING)
			new_state = IRIS_INST_INPUT_STREAMING;
	}

	return iris_inst_change_state(inst, new_state);
}

static bool iris_inst_allow_sub_state(struct iris_inst *inst, enum iris_inst_sub_state sub_state)
{
	if (!sub_state)
		return true;

	switch (inst->state) {
	case IRIS_INST_INIT:
		if (sub_state & IRIS_INST_SUB_LOAD_RESOURCES)
			return true;
		return false;
	case IRIS_INST_INPUT_STREAMING:
		if (sub_state & (IRIS_INST_SUB_FIRST_IPSC | IRIS_INST_SUB_DRC |
			IRIS_INST_SUB_DRAIN | IRIS_INST_SUB_INPUT_PAUSE))
			return true;
		return false;
	case IRIS_INST_OUTPUT_STREAMING:
		if (sub_state & (IRIS_INST_SUB_DRC_LAST |
			IRIS_INST_SUB_DRAIN_LAST | IRIS_INST_SUB_OUTPUT_PAUSE))
			return true;
		return false;
	case IRIS_INST_STREAMING:
		if (sub_state & (IRIS_INST_SUB_DRC | IRIS_INST_SUB_DRAIN |
			IRIS_INST_SUB_DRC_LAST | IRIS_INST_SUB_DRAIN_LAST |
			IRIS_INST_SUB_INPUT_PAUSE | IRIS_INST_SUB_OUTPUT_PAUSE))
			return true;
		return false;
	case IRIS_INST_DEINIT:
		if (sub_state & (IRIS_INST_SUB_DRC | IRIS_INST_SUB_DRAIN |
			IRIS_INST_SUB_DRC_LAST | IRIS_INST_SUB_DRAIN_LAST |
			IRIS_INST_SUB_INPUT_PAUSE | IRIS_INST_SUB_OUTPUT_PAUSE))
			return true;
		return false;
	default:
		return false;
	}
}

int iris_inst_change_sub_state(struct iris_inst *inst,
			       enum iris_inst_sub_state clear_sub_state,
			       enum iris_inst_sub_state set_sub_state)
{
	enum iris_inst_sub_state prev_sub_state;

	if (inst->state == IRIS_INST_ERROR)
		return 0;

	if (!clear_sub_state && !set_sub_state)
		return 0;

	if ((clear_sub_state & set_sub_state) ||
	    set_sub_state > IRIS_INST_MAX_SUB_STATE_VALUE ||
	    clear_sub_state > IRIS_INST_MAX_SUB_STATE_VALUE)
		return -EINVAL;

	prev_sub_state = inst->sub_state;

	if (!iris_inst_allow_sub_state(inst, set_sub_state))
		return -EINVAL;

	inst->sub_state |= set_sub_state;
	inst->sub_state &= ~clear_sub_state;

	if (inst->sub_state != prev_sub_state)
		dev_dbg(inst->core->dev, "sub_state changed from %x to %x\n",
			prev_sub_state, inst->sub_state);

	return 0;
}

int iris_inst_sub_state_change_drc(struct iris_inst *inst)
{
	enum iris_inst_sub_state set_sub_state = 0;

	if (inst->sub_state & IRIS_INST_SUB_DRC)
		return -EINVAL;

	if (inst->state == IRIS_INST_INPUT_STREAMING ||
	    inst->state == IRIS_INST_INIT)
		set_sub_state = IRIS_INST_SUB_FIRST_IPSC | IRIS_INST_SUB_INPUT_PAUSE;
	else
		set_sub_state = IRIS_INST_SUB_DRC | IRIS_INST_SUB_INPUT_PAUSE;

	return iris_inst_change_sub_state(inst, 0, set_sub_state);
}

int iris_inst_sub_state_change_drain_last(struct iris_inst *inst)
{
	enum iris_inst_sub_state set_sub_state;

	if (inst->sub_state & IRIS_INST_SUB_DRAIN_LAST)
		return -EINVAL;

	if (!(inst->sub_state & IRIS_INST_SUB_DRAIN))
		return -EINVAL;

	set_sub_state = IRIS_INST_SUB_DRAIN_LAST | IRIS_INST_SUB_OUTPUT_PAUSE;

	return iris_inst_change_sub_state(inst, 0, set_sub_state);
}

int iris_inst_sub_state_change_drc_last(struct iris_inst *inst)
{
	enum iris_inst_sub_state set_sub_state;

	if (inst->sub_state & IRIS_INST_SUB_DRC_LAST)
		return -EINVAL;

	if (!(inst->sub_state & IRIS_INST_SUB_DRC) ||
	    !(inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE))
		return -EINVAL;

	if (inst->sub_state & IRIS_INST_SUB_FIRST_IPSC)
		return 0;

	set_sub_state = IRIS_INST_SUB_DRC_LAST | IRIS_INST_SUB_OUTPUT_PAUSE;

	return iris_inst_change_sub_state(inst, 0, set_sub_state);
}

int iris_inst_sub_state_change_pause(struct iris_inst *inst, u32 plane)
{
	enum iris_inst_sub_state set_sub_state;

	if (V4L2_TYPE_IS_OUTPUT(plane)) {
		if (inst->sub_state & IRIS_INST_SUB_DRC &&
		    !(inst->sub_state & IRIS_INST_SUB_DRC_LAST))
			return -EINVAL;

		if (inst->sub_state & IRIS_INST_SUB_DRAIN &&
		    !(inst->sub_state & IRIS_INST_SUB_DRAIN_LAST))
			return -EINVAL;

		set_sub_state = IRIS_INST_SUB_INPUT_PAUSE;
	} else {
		set_sub_state = IRIS_INST_SUB_OUTPUT_PAUSE;
	}

	return iris_inst_change_sub_state(inst, 0, set_sub_state);
}

static inline bool iris_drc_pending(struct iris_inst *inst)
{
	return inst->sub_state & IRIS_INST_SUB_DRC &&
		inst->sub_state & IRIS_INST_SUB_DRC_LAST;
}

static inline bool iris_drain_pending(struct iris_inst *inst)
{
	return inst->sub_state & IRIS_INST_SUB_DRAIN &&
		inst->sub_state & IRIS_INST_SUB_DRAIN_LAST;
}

bool iris_allow_cmd(struct iris_inst *inst, u32 cmd)
{
	struct vb2_queue *src_q = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	struct vb2_queue *dst_q = v4l2_m2m_get_dst_vq(inst->m2m_ctx);

	if (cmd == V4L2_DEC_CMD_START) {
		if (vb2_is_streaming(src_q) || vb2_is_streaming(dst_q))
			if (iris_drc_pending(inst) || iris_drain_pending(inst))
				return true;
	} else if (cmd == V4L2_DEC_CMD_STOP) {
		if (vb2_is_streaming(src_q))
			if (inst->sub_state != IRIS_INST_SUB_DRAIN)
				return true;
	}

	return false;
}
