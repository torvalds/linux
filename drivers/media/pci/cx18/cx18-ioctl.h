/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  cx18 ioctl system call
 *
 *  Derived from ivtv-ioctl.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@kernel.org>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 */

u16 cx18_service2vbi(int type);
void cx18_expand_service_set(struct v4l2_sliced_vbi_format *fmt, int is_pal);
u16 cx18_get_service_set(struct v4l2_sliced_vbi_format *fmt);
void cx18_set_funcs(struct video_device *vdev);

struct cx18;
int cx18_do_s_std(struct cx18 *cx, v4l2_std_id std);
int cx18_do_s_frequency(struct cx18 *cx, const struct v4l2_frequency *vf);
int cx18_do_s_input(struct cx18 *cx, unsigned int inp);
