/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    ioctl control functions
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@kernel.org>

 */

#ifndef IVTV_CONTROLS_H
#define IVTV_CONTROLS_H

extern const struct cx2341x_handler_ops ivtv_cxhdl_ops;
extern const struct v4l2_ctrl_ops ivtv_hdl_out_ops;
int ivtv_g_pts_frame(struct ivtv *itv, s64 *pts, s64 *frame);

#endif
