/*
 *  cx18 file operation functions
 *
 *  Derived from ivtv-fileops.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

/* Testing/Debugging */
int cx18_v4l2_open(struct file *filp);
ssize_t cx18_v4l2_read(struct file *filp, char __user *buf, size_t count,
		      loff_t *pos);
ssize_t cx18_v4l2_write(struct file *filp, const char __user *buf, size_t count,
		       loff_t *pos);
int cx18_v4l2_close(struct file *filp);
unsigned int cx18_v4l2_enc_poll(struct file *filp, poll_table *wait);
int cx18_start_capture(struct cx18_open_id *id);
void cx18_stop_capture(struct cx18_open_id *id, int gop_end);
void cx18_mute(struct cx18 *cx);
void cx18_unmute(struct cx18 *cx);

