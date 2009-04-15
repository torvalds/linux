/*
 *  cx18 init/start/stop/exit stream functions
 *
 *  Derived from ivtv-streams.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@radix.net>
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

u32 cx18_find_handle(struct cx18 *cx);
struct cx18_stream *cx18_handle_to_stream(struct cx18 *cx, u32 handle);
int cx18_streams_setup(struct cx18 *cx);
int cx18_streams_register(struct cx18 *cx);
void cx18_streams_cleanup(struct cx18 *cx, int unregister);

/* Related to submission of buffers to firmware */
static inline void cx18_stream_load_fw_queue(struct cx18_stream *s)
{
	struct cx18 *cx = s->cx;
	queue_work(cx->out_work_queue, &s->out_work_order);
}

static inline void cx18_stream_put_buf_fw(struct cx18_stream *s,
					  struct cx18_buffer *buf)
{
	/* Put buf on q_free; the out work handler will move buf(s) to q_busy */
	cx18_enqueue(s, buf, &s->q_free);
	cx18_stream_load_fw_queue(s);
}

void cx18_out_work_handler(struct work_struct *work);

/* Capture related */
int cx18_start_v4l2_encode_stream(struct cx18_stream *s);
int cx18_stop_v4l2_encode_stream(struct cx18_stream *s, int gop_end);

void cx18_stop_all_captures(struct cx18 *cx);
