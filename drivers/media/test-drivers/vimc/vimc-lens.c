// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vimc-lens.c Virtual Media Controller Driver
 * Copyright (C) 2022 Google, Inc
 * Author: yunkec@google.com (Yunke Cao)
 */

#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

#include "vimc-common.h"

#define VIMC_LEN_MAX_FOCUS_POS	1023
#define VIMC_LEN_MAX_FOCUS_STEP	1

struct vimc_len_device {
	struct vimc_ent_device ved;
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	u32 focus_absolute;
};

static const struct v4l2_subdev_core_ops vimc_len_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_ops vimc_len_ops = {
	.core = &vimc_len_core_ops
};

static int vimc_len_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vimc_len_device *vlen =
		container_of(ctrl->handler, struct vimc_len_device, hdl);
	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE) {
		vlen->focus_absolute = ctrl->val;
		return 0;
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops vimc_len_ctrl_ops = {
	.s_ctrl = vimc_len_s_ctrl,
};

static struct vimc_ent_device *vimc_len_add(struct vimc_device *vimc,
					    const char *vcfg_name)
{
	struct v4l2_device *v4l2_dev = &vimc->v4l2_dev;
	struct vimc_len_device *vlen;
	int ret;

	/* Allocate the vlen struct */
	vlen = kzalloc(sizeof(*vlen), GFP_KERNEL);
	if (!vlen)
		return ERR_PTR(-ENOMEM);

	v4l2_ctrl_handler_init(&vlen->hdl, 1);

	v4l2_ctrl_new_std(&vlen->hdl, &vimc_len_ctrl_ops,
			  V4L2_CID_FOCUS_ABSOLUTE, 0,
			  VIMC_LEN_MAX_FOCUS_POS, VIMC_LEN_MAX_FOCUS_STEP, 0);
	vlen->sd.ctrl_handler = &vlen->hdl;
	if (vlen->hdl.error) {
		ret = vlen->hdl.error;
		goto err_free_vlen;
	}
	vlen->ved.dev = vimc->mdev.dev;

	ret = vimc_ent_sd_register(&vlen->ved, &vlen->sd, v4l2_dev,
				   vcfg_name, MEDIA_ENT_F_LENS, 0,
				   NULL, &vimc_len_ops);
	if (ret)
		goto err_free_hdl;

	return &vlen->ved;

err_free_hdl:
	v4l2_ctrl_handler_free(&vlen->hdl);
err_free_vlen:
	kfree(vlen);

	return ERR_PTR(ret);
}

static void vimc_len_release(struct vimc_ent_device *ved)
{
	struct vimc_len_device *vlen =
		container_of(ved, struct vimc_len_device, ved);

	v4l2_ctrl_handler_free(&vlen->hdl);
	media_entity_cleanup(vlen->ved.ent);
	kfree(vlen);
}

struct vimc_ent_type vimc_len_type = {
	.add = vimc_len_add,
	.release = vimc_len_release
};
