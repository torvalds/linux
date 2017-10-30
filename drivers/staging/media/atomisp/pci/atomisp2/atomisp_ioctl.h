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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef	__ATOMISP_IOCTL_H__
#define	__ATOMISP_IOCTL_H__

#include "ia_css.h"

struct atomisp_device;
struct atomisp_video_pipe;

extern const struct atomisp_format_bridge atomisp_output_fmts[];

const struct atomisp_format_bridge *atomisp_get_format_bridge(
	unsigned int pixelformat);
#ifndef ISP2401
const struct atomisp_format_bridge *atomisp_get_format_bridge_from_mbus(
	u32 mbus_code);
#else
const struct atomisp_format_bridge *atomisp_get_format_bridge_from_mbus(u32
									mbus_code);
#endif

int atomisp_alloc_css_stat_bufs(struct atomisp_sub_device *asd,
	uint16_t stream_id);

int __atomisp_streamoff(struct file *file, void *fh, enum v4l2_buf_type type);
int __atomisp_reqbufs(struct file *file, void *fh,
		struct v4l2_requestbuffers *req);

int atomisp_reqbufs(struct file *file, void *fh,
			struct v4l2_requestbuffers *req);

enum atomisp_css_pipe_id atomisp_get_css_pipe_id(struct atomisp_sub_device
						 *asd);

void atomisp_videobuf_free_buf(struct videobuf_buffer *vb);

extern const struct v4l2_file_operations atomisp_file_fops;

extern const struct v4l2_ioctl_ops atomisp_ioctl_ops;

extern const struct v4l2_ioctl_ops atomisp_file_ioctl_ops;

unsigned int atomisp_streaming_count(struct atomisp_device *isp);

unsigned int atomisp_is_acc_enabled(struct atomisp_device *isp);
/* compat_ioctl for 32bit userland app and 64bit kernel */
long atomisp_compat_ioctl32(struct file *file,
			    unsigned int cmd, unsigned long arg);

int atomisp_stream_on_master_slave_sensor(struct atomisp_device *isp, bool isp_timeout);
#endif /* __ATOMISP_IOCTL_H__ */
