/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vivid-vid-cap.h - video capture support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _VIVID_VID_CAP_H_
#define _VIVID_VID_CAP_H_

void vivid_update_quality(struct vivid_dev *dev);
void vivid_update_format_cap(struct vivid_dev *dev, bool keep_controls);
void vivid_update_outputs(struct vivid_dev *dev);
void vivid_update_connected_outputs(struct vivid_dev *dev);
enum tpg_video_aspect vivid_get_video_aspect(const struct vivid_dev *dev);

extern const v4l2_std_id vivid_standard[];
extern const char * const vivid_ctrl_standard_strings[];

extern const struct vb2_ops vivid_vid_cap_qops;

int vivid_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f);
int vivid_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f);
int vivid_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f);
int vidioc_g_fmt_vid_cap_mplane(struct file *file, void *priv, struct v4l2_format *f);
int vidioc_try_fmt_vid_cap_mplane(struct file *file, void *priv, struct v4l2_format *f);
int vidioc_s_fmt_vid_cap_mplane(struct file *file, void *priv, struct v4l2_format *f);
int vidioc_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f);
int vidioc_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f);
int vidioc_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f);
int vivid_vid_cap_g_selection(struct file *file, void *priv, struct v4l2_selection *sel);
int vivid_vid_cap_s_selection(struct file *file, void *priv, struct v4l2_selection *s);
int vivid_vid_cap_g_pixelaspect(struct file *file, void *priv, int type, struct v4l2_fract *f);
int vidioc_enum_fmt_vid_overlay(struct file *file, void  *priv, struct v4l2_fmtdesc *f);
int vidioc_g_fmt_vid_overlay(struct file *file, void *priv, struct v4l2_format *f);
int vidioc_try_fmt_vid_overlay(struct file *file, void *priv, struct v4l2_format *f);
int vidioc_s_fmt_vid_overlay(struct file *file, void *priv, struct v4l2_format *f);
int vidioc_enum_input(struct file *file, void *priv, struct v4l2_input *inp);
int vidioc_g_input(struct file *file, void *priv, unsigned *i);
int vidioc_s_input(struct file *file, void *priv, unsigned i);
int vidioc_enumaudio(struct file *file, void *priv, struct v4l2_audio *vin);
int vidioc_g_audio(struct file *file, void *priv, struct v4l2_audio *vin);
int vidioc_s_audio(struct file *file, void *priv, const struct v4l2_audio *vin);
int vivid_video_g_frequency(struct file *file, void *priv, struct v4l2_frequency *vf);
int vivid_video_s_frequency(struct file *file, void *priv, const struct v4l2_frequency *vf);
int vivid_video_s_tuner(struct file *file, void *priv, const struct v4l2_tuner *vt);
int vivid_video_g_tuner(struct file *file, void *priv, struct v4l2_tuner *vt);
int vidioc_querystd(struct file *file, void *priv, v4l2_std_id *id);
int vivid_vid_cap_s_std(struct file *file, void *priv, v4l2_std_id id);
int vivid_vid_cap_s_dv_timings(struct file *file, void *priv, struct v4l2_dv_timings *timings);
int vidioc_query_dv_timings(struct file *file, void *priv, struct v4l2_dv_timings *timings);
int vidioc_s_edid(struct file *file, void *priv, struct v4l2_edid *edid);
int vidioc_enum_framesizes(struct file *file, void *priv, struct v4l2_frmsizeenum *fsize);
int vidioc_enum_frameintervals(struct file *file, void *priv, struct v4l2_frmivalenum *fival);
int vivid_vid_cap_g_parm(struct file *file, void *priv, struct v4l2_streamparm *parm);
int vivid_vid_cap_s_parm(struct file *file, void *priv, struct v4l2_streamparm *parm);

#endif
