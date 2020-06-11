/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-meta-out.h - meta output support functions.
 */
#ifndef _VIVID_META_OUT_H_
#define _VIVID_META_OUT_H_

struct vivid_meta_out_buf {
	u16	brightness;
	u16	contrast;
	u16	saturation;
	s16	hue;
};

void vivid_meta_out_process(struct vivid_dev *dev, struct vivid_buffer *buf);
int vidioc_enum_fmt_meta_out(struct file *file, void  *priv,
			     struct v4l2_fmtdesc *f);
int vidioc_g_fmt_meta_out(struct file *file, void *priv,
			  struct v4l2_format *f);
int vidioc_s_fmt_meta_out(struct file *file, void *priv,
			  struct v4l2_format *f);

extern const struct vb2_ops vivid_meta_out_qops;

#endif
