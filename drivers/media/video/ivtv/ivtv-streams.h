/*
    init/start/stop/exit stream functions
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef IVTV_STREAMS_H
#define IVTV_STREAMS_H

int ivtv_streams_setup(struct ivtv *itv);
int ivtv_streams_register(struct ivtv *itv);
void ivtv_streams_cleanup(struct ivtv *itv);

/* Capture related */
int ivtv_start_v4l2_encode_stream(struct ivtv_stream *s);
int ivtv_stop_v4l2_encode_stream(struct ivtv_stream *s, int gop_end);
int ivtv_start_v4l2_decode_stream(struct ivtv_stream *s, int gop_offset);
int ivtv_stop_v4l2_decode_stream(struct ivtv_stream *s, int flags, u64 pts);

void ivtv_stop_all_captures(struct ivtv *itv);
int ivtv_passthrough_mode(struct ivtv *itv, int enable);

#endif
