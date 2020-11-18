/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-meta-cap.h - meta capture support functions.
 */
#ifndef _VIVID_META_CAP_H_
#define _VIVID_META_CAP_H_

#define VIVID_META_CLOCK_UNIT	10 /* 100 MHz */

struct vivid_uvc_meta_buf {
	__u64 ns;
	__u16 sof;
	__u8 length;
	__u8 flags;
	__u8 buf[10]; /* PTS(4)+STC(4)+SOF(2) */
} __packed;

void vivid_meta_cap_fillbuff(struct vivid_dev *dev,
			     struct vivid_buffer *buf, u64 soe);

int vidioc_enum_fmt_meta_cap(struct file *file, void  *priv,
			     struct v4l2_fmtdesc *f);

int vidioc_g_fmt_meta_cap(struct file *file, void *priv,
			  struct v4l2_format *f);

extern const struct vb2_ops vivid_meta_cap_qops;

#endif
