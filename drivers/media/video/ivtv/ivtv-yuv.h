/*
    yuv support

    Copyright (C) 2007  Ian Armstrong <ian@iarmst.demon.co.uk>

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

#ifndef IVTV_YUV_H
#define IVTV_YUV_H

#define IVTV_YUV_BUFFER_UV_OFFSET 0x65400	/* Offset to UV Buffer */

/* Offset to filter table in firmware */
#define IVTV_YUV_HORIZONTAL_FILTER_OFFSET 0x025d8
#define IVTV_YUV_VERTICAL_FILTER_OFFSET 0x03358

#define IVTV_YUV_UPDATE_HORIZONTAL  0x01
#define IVTV_YUV_UPDATE_VERTICAL    0x02
#define IVTV_YUV_UPDATE_INVALID     0x04

extern const u32 yuv_offset[IVTV_YUV_BUFFERS];

int ivtv_yuv_filter_check(struct ivtv *itv);
void ivtv_yuv_setup_stream_frame(struct ivtv *itv);
int ivtv_yuv_udma_stream_frame(struct ivtv *itv, void __user *src);
void ivtv_yuv_frame_complete(struct ivtv *itv);
int ivtv_yuv_prep_frame(struct ivtv *itv, struct ivtv_dma_frame *args);
void ivtv_yuv_close(struct ivtv *itv);
void ivtv_yuv_work_handler(struct ivtv *itv);

#endif
