/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021-2022 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 */

#ifndef __MGB4_IO_H__
#define __MGB4_IO_H__

#include <linux/math64.h>
#include <media/v4l2-dev.h>
#include "mgb4_core.h"

/* Register access error indication */
#define MGB4_ERR_NO_REG        0xFFFFFFFE
/* Frame buffer addresses greater than 0xFFFFFFFA indicate HW errors */
#define MGB4_ERR_QUEUE_TIMEOUT 0xFFFFFFFD
#define MGB4_ERR_QUEUE_EMPTY   0xFFFFFFFC
#define MGB4_ERR_QUEUE_FULL    0xFFFFFFFB

#define MGB4_PERIOD(numerator, denominator) \
	((u32)div_u64((MGB4_HW_FREQ * (u64)(numerator)), (denominator)))

struct mgb4_frame_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

static inline struct mgb4_frame_buffer *to_frame_buffer(struct vb2_v4l2_buffer *vbuf)
{
	return container_of(vbuf, struct mgb4_frame_buffer, vb);
}

static inline bool has_yuv_and_timeperframe(struct mgb4_regs *video)
{
	u32 status = mgb4_read_reg(video, 0xD0);

	return (status & (1U << 8));
}

#define has_yuv(video) has_yuv_and_timeperframe(video)
#define has_timeperframe(video) has_yuv_and_timeperframe(video)

static inline u32 pixel_size(struct v4l2_dv_timings *timings)
{
	struct v4l2_bt_timings *bt = &timings->bt;

	u32 height = bt->height + bt->vfrontporch + bt->vsync + bt->vbackporch;
	u32 width = bt->width + bt->hfrontporch + bt->hsync + bt->hbackporch;

	return width * height;
}

#endif
