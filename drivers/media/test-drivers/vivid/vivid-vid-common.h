/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-vid-common.h - common video support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _VIVID_VID_COMMON_H_
#define _VIVID_VID_COMMON_H_

typedef int (*fmtfunc)(struct file *file, void *priv, struct v4l2_format *f);

/*
 * Conversion function that converts a single-planar format to a
 * single-plane multiplanar format.
 */
void fmt_sp2mp(const struct v4l2_format *sp_fmt, struct v4l2_format *mp_fmt);
int fmt_sp2mp_func(struct file *file, void *priv,
		struct v4l2_format *f, fmtfunc func);

extern const struct v4l2_dv_timings_cap vivid_dv_timings_cap;

const struct vivid_fmt *vivid_get_format(struct vivid_dev *dev, u32 pixelformat);

bool vivid_vid_can_loop(struct vivid_dev *dev);
void vivid_send_source_change(struct vivid_dev *dev, unsigned type);

int vivid_vid_adjust_sel(unsigned flags, struct v4l2_rect *r);

int vivid_enum_fmt_vid(struct file *file, void  *priv, struct v4l2_fmtdesc *f);
int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *id);
int vidioc_g_dv_timings(struct file *file, void *_fh, struct v4l2_dv_timings *timings);
int vidioc_enum_dv_timings(struct file *file, void *_fh, struct v4l2_enum_dv_timings *timings);
int vidioc_dv_timings_cap(struct file *file, void *_fh, struct v4l2_dv_timings_cap *cap);
int vidioc_g_edid(struct file *file, void *_fh, struct v4l2_edid *edid);
int vidioc_subscribe_event(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub);

#endif
