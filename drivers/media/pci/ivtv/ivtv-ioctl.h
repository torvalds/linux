/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    ioctl system call
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@kernel.org>

 */

#ifndef IVTV_IOCTL_H
#define IVTV_IOCTL_H

struct ivtv;

u16 ivtv_service2vbi(int type);
void ivtv_expand_service_set(struct v4l2_sliced_vbi_format *fmt, int is_pal);
u16 ivtv_get_service_set(struct v4l2_sliced_vbi_format *fmt);
void ivtv_set_osd_alpha(struct ivtv *itv);
int ivtv_set_speed(struct ivtv *itv, int speed);
void ivtv_set_funcs(struct video_device *vdev);
void ivtv_s_std_enc(struct ivtv *itv, v4l2_std_id std);
void ivtv_s_std_dec(struct ivtv *itv, v4l2_std_id std);
int ivtv_do_s_frequency(struct ivtv_stream *s, const struct v4l2_frequency *vf);
int ivtv_do_s_input(struct ivtv *itv, unsigned int inp);

#endif
