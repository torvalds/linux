// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

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
