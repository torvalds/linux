/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-vbi-out.h - vbi output support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _VIVID_VBI_OUT_H_
#define _VIVID_VBI_OUT_H_

void vivid_sliced_vbi_out_process(struct vivid_dev *dev, struct vivid_buffer *buf);
int vidioc_g_fmt_vbi_out(struct file *file, void *priv,
					struct v4l2_format *f);
int vidioc_s_fmt_vbi_out(struct file *file, void *priv,
					struct v4l2_format *f);
int vidioc_g_fmt_sliced_vbi_out(struct file *file, void *priv, struct v4l2_format *fmt);
int vidioc_try_fmt_sliced_vbi_out(struct file *file, void *priv, struct v4l2_format *fmt);
int vidioc_s_fmt_sliced_vbi_out(struct file *file, void *priv, struct v4l2_format *fmt);

extern const struct vb2_ops vivid_vbi_out_qops;

#endif
