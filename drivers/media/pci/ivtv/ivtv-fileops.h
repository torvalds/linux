/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    file operation functions
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@kernel.org>

 */

#ifndef IVTV_FILEOPS_H
#define IVTV_FILEOPS_H

/* Testing/Debugging */
int ivtv_v4l2_open(struct file *filp);
ssize_t ivtv_v4l2_read(struct file *filp, char __user *buf, size_t count,
		      loff_t * pos);
ssize_t ivtv_v4l2_write(struct file *filp, const char __user *buf, size_t count,
		       loff_t * pos);
int ivtv_v4l2_close(struct file *filp);
__poll_t ivtv_v4l2_enc_poll(struct file *filp, poll_table * wait);
__poll_t ivtv_v4l2_dec_poll(struct file *filp, poll_table * wait);
int ivtv_start_capture(struct ivtv_open_id *id);
void ivtv_stop_capture(struct ivtv_open_id *id, int gop_end);
int ivtv_start_decoding(struct ivtv_open_id *id, int speed);
void ivtv_mute(struct ivtv *itv);
void ivtv_unmute(struct ivtv *itv);

/* Utilities */
/* Shared with ivtv-alsa module */
int ivtv_claim_stream(struct ivtv_open_id *id, int type);
void ivtv_release_stream(struct ivtv_stream *s);

#endif
