/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021-2022 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 */

#ifndef __MGB4_IO_H__
#define __MGB4_IO_H__

#include <media/v4l2-dev.h>

#define MGB4_DEFAULT_WIDTH     1280
#define MGB4_DEFAULT_HEIGHT    640
#define MGB4_DEFAULT_PERIOD    (125000000 / 60)

/* Register access error indication */
#define MGB4_ERR_NO_REG        0xFFFFFFFE
/* Frame buffer addresses greater than 0xFFFFFFFA indicate HW errors */
#define MGB4_ERR_QUEUE_TIMEOUT 0xFFFFFFFD
#define MGB4_ERR_QUEUE_EMPTY   0xFFFFFFFC
#define MGB4_ERR_QUEUE_FULL    0xFFFFFFFB

struct mgb4_frame_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

static inline struct mgb4_frame_buffer *to_frame_buffer(struct vb2_v4l2_buffer *vbuf)
{
	return container_of(vbuf, struct mgb4_frame_buffer, vb);
}

#endif
