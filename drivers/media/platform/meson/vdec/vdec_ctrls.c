// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#include "vdec_ctrls.h"

static int vdec_op_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct amvdec_session *sess =
	      container_of(ctrl->handler, struct amvdec_session, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		ctrl->val = sess->dpb_size;
		break;
	default:
		return -EINVAL;
	};

	return 0;
}

static const struct v4l2_ctrl_ops vdec_ctrl_ops = {
	.g_volatile_ctrl = vdec_op_g_volatile_ctrl,
};

int amvdec_init_ctrls(struct v4l2_ctrl_handler *ctrl_handler)
{
	int ret;
	struct v4l2_ctrl *ctrl;

	ret = v4l2_ctrl_handler_init(ctrl_handler, 1);
	if (ret)
		return ret;

	ctrl = v4l2_ctrl_new_std(ctrl_handler, &vdec_ctrl_ops,
		V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 1, 32, 1, 1);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	ret = ctrl_handler->error;
	if (ret) {
		v4l2_ctrl_handler_free(ctrl_handler);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(amvdec_init_ctrls);
