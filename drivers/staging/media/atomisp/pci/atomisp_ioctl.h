/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */

#ifndef	__ATOMISP_IOCTL_H__
#define	__ATOMISP_IOCTL_H__

#include "ia_css.h"

struct atomisp_device;
struct atomisp_video_pipe;

extern const struct atomisp_format_bridge atomisp_output_fmts[];

const struct
atomisp_format_bridge *atomisp_get_format_bridge(unsigned int pixelformat);

const struct
atomisp_format_bridge *atomisp_get_format_bridge_from_mbus(u32 mbus_code);

int atomisp_pipe_check(struct atomisp_video_pipe *pipe, bool streaming_ok);

int atomisp_alloc_css_stat_bufs(struct atomisp_sub_device *asd,
				uint16_t stream_id);

int atomisp_start_streaming(struct vb2_queue *vq, unsigned int count);
void atomisp_stop_streaming(struct vb2_queue *vq);

extern const struct v4l2_ioctl_ops atomisp_ioctl_ops;

/* compat_ioctl for 32bit userland app and 64bit kernel */
long atomisp_compat_ioctl32(struct file *file,
			    unsigned int cmd, unsigned long arg);

#endif /* __ATOMISP_IOCTL_H__ */
