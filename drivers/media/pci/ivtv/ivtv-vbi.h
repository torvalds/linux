/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Vertical Blank Interval support functions
    Copyright (C) 2004-2007  Hans Verkuil <hverkuil@xs4all.nl>

 */

#ifndef IVTV_VBI_H
#define IVTV_VBI_H

ssize_t
ivtv_write_vbi_from_user(struct ivtv *itv,
			 const struct v4l2_sliced_vbi_data __user *sliced,
			 size_t count);
void ivtv_process_vbi_data(struct ivtv *itv, struct ivtv_buffer *buf,
			   u64 pts_stamp, int streamtype);
int ivtv_used_line(struct ivtv *itv, int line, int field);
void ivtv_disable_cc(struct ivtv *itv);
void ivtv_set_vbi(unsigned long arg);
void ivtv_vbi_work_handler(struct ivtv *itv);

#endif
