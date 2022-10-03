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

#define VIMC_LENS_MAX_FOCUS_POS	1023
#define VIMC_LENS_MAX_FOCUS_STEP	1

struct vimc_lens_device {
	struct vimc_ent_device ved;
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	u32 focus_absolute;
};

static const struct v4l2_subdev_core_ops vimc_lens_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_ops vimc_lens_ops = {
	.core = &vimc_lens_core_ops
};

static int vimc_lens_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vimc_lens_device *vlens =
		container_of(ctrl->handler, struct vimc_lens_device, hdl);
	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE) {
		vlens->focus_absolute = ctrl->val;
		return 0;
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops vimc_lens_ctrl_ops = {
	.s_ctrl = vimc_lens_s_ctrl,
};

static struct vimc_ent_device *vimc_lens_add(struct vimc_device *vimc,
					     const char *vcfg_name)
{
	struct v4l2_device *v4l2_dev = &vimc->v4l2_dev;
	struct vimc_lens_device *vlens;
	int ret;

	/* Allocate the vlens struct */
	vlens = kzalloc(sizeof(*vlens), GFP_KERNEL);
	if (!vlens)
		return ERR_PTR(-ENOMEM);

	v4l2_ctrl_handler_init(&vlens->hdl, 1);

	v4l2_ctrl_new_std(&vlens->hdl, &vimc_lens_ctrl_ops,
			  V4L2_CID_FOCUS_ABSOLUTE, 0,
			  VIMC_LENS_MAX_FOCUS_POS, VIMC_LENS_MAX_FOCUS_STEP, 0);
	vlens->sd.ctrl_handler = &vlens->hdl;
	if (vlens->hdl.error) {
		ret = vlens->hdl.error;
		goto err_free_vlens;
	}
	vlens->ved.dev = vimc->mdev.dev;

	ret = vimc_ent_sd_register(&vlens->ved, &vlens->sd, v4l2_dev,
				   vcfg_name, MEDIA_ENT_F_LENS, 0,
				   NULL, &vimc_lens_ops);
	if (ret)
		goto err_free_hdl;

	return &vlens->ved;

err_free_hdl:
	v4l2_ctrl_handler_free(&vlens->hdl);
err_free_vlens:
	kfree(vlens);

	return ERR_PTR(ret);
}

static void vimc_lens_release(struct vimc_ent_device *ved)
{
	struct vimc_lens_device *vlens =
		container_of(ved, struct vimc_lens_device, ved);

	v4l2_ctrl_handler_free(&vlens->hdl);
	media_entity_cleanup(vlens->ved.ent);
	kfree(vlens);
}

struct vimc_ent_type vimc_lens_type = {
	.add = vimc_lens_add,
	.release = vimc_lens_release
};
