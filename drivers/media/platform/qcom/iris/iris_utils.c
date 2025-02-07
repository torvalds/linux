// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pm_runtime.h>

#include "iris_instance.h"
#include "iris_utils.h"

int iris_get_mbpf(struct iris_inst *inst)
{
	struct v4l2_format *inp_f = inst->fmt_src;
	u32 height = max(inp_f->fmt.pix_mp.height, inst->crop.height);
	u32 width = max(inp_f->fmt.pix_mp.width, inst->crop.width);

	return NUM_MBS_PER_FRAME(height, width);
}

int iris_wait_for_session_response(struct iris_inst *inst)
{
	struct iris_core *core = inst->core;
	u32 hw_response_timeout_val;
	int ret;

	hw_response_timeout_val = core->iris_platform_data->hw_response_timeout;

	mutex_unlock(&inst->lock);
	ret = wait_for_completion_timeout(&inst->completion,
					  msecs_to_jiffies(hw_response_timeout_val));
	mutex_lock(&inst->lock);
	if (!ret)
		return -ETIMEDOUT;

	return 0;
}

struct iris_inst *iris_get_instance(struct iris_core *core, u32 session_id)
{
	struct iris_inst *inst;

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		if (inst->session_id == session_id) {
			mutex_unlock(&core->lock);
			return inst;
		}
	}

	mutex_unlock(&core->lock);
	return NULL;
}
