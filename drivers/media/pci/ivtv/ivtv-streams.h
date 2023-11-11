/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    init/start/stop/exit stream functions
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

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
