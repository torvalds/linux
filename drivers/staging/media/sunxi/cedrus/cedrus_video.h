/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cedrus VPU driver
 *
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 * Copyright (C) 2018 Bootlin
 *
 * Based on the vim2m driver, that is:
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 */

#ifndef _CEDRUS_VIDEO_H_
#define _CEDRUS_VIDEO_H_

struct cedrus_format {
	u32		pixelformat;
	u32		directions;
	unsigned int	capabilities;
};

extern const struct v4l2_ioctl_ops cedrus_ioctl_ops;

int cedrus_queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq);
void cedrus_prepare_format(struct v4l2_pix_format *pix_fmt);

#endif
