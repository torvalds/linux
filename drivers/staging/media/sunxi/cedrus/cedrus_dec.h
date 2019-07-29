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

#ifndef _CEDRUS_DEC_H_
#define _CEDRUS_DEC_H_

extern const struct v4l2_ioctl_ops cedrus_ioctl_ops;

void cedrus_device_work(struct work_struct *work);
void cedrus_device_run(void *priv);

int cedrus_queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq);

#endif
