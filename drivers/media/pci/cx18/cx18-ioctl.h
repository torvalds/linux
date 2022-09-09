/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  cx18 ioctl system call
 *
 *  Derived from ivtv-ioctl.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 */

u16 cx18_service2vbi(int type);
void cx18_expand_service_set(struct v4l2_sliced_vbi_format *fmt, int is_pal);
u16 cx18_get_service_set(struct v4l2_sliced_vbi_format *fmt);
void cx18_set_funcs(struct video_device *vdev);
int cx18_s_std(struct file *file, void *fh, v4l2_std_id std);
int cx18_s_frequency(struct file *file, void *fh, const struct v4l2_frequency *vf);
int cx18_s_input(struct file *file, void *fh, unsigned int inp);
