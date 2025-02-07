// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_instance.h"
#include "iris_vb2.h"

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

	core = inst->core;
	f = V4L2_TYPE_IS_OUTPUT(q->type) ? inst->fmt_src : inst->fmt_dst;

	if (*num_planes) {
		if (*num_planes != f->fmt.pix_mp.num_planes ||
		    sizes[0] < f->fmt.pix_mp.plane_fmt[0].sizeimage)
			ret = -EINVAL;
		goto unlock;
	}

	if (!inst->once_per_session_set) {
		inst->once_per_session_set = true;

		ret = core->hfi_ops->session_open(inst);
		if (ret) {
			ret = -EINVAL;
			dev_err(core->dev, "session open failed\n");
			goto unlock;
		}
	}

	*num_planes = 1;
	sizes[0] = f->fmt.pix_mp.plane_fmt[0].sizeimage;

unlock:
	mutex_unlock(&inst->lock);

	return ret;
}
