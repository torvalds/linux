/*
 * Copyright (C) 2012-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __VDOA_H__
#define __VDOA_H__

#define VDOA_PFS_YUYV (1)
#define VDOA_PFS_NV12 (0)


struct vfield_buf {
	u32	prev_veba;
	u32	cur_veba;
	u32	next_veba;
	u32	vubo;
};

struct vframe_buf {
	u32	veba;
	u32	vubo;
};

struct vdoa_params {
	u32	width;
	u32	height;
	int	vpu_stride;
	int	interlaced;
	int	scan_order;
	int	ipu_num;
	int	band_lines;
	int	band_mode;
	int	pfs;
	u32	ieba0;
	u32	ieba1;
	u32	ieba2;
	struct	vframe_buf vframe_buf;
	struct	vfield_buf vfield_buf;
};
struct vdoa_ipu_buf {
	u32	ieba0;
	u32	ieba1;
	u32	iubo;
};

struct vdoa_info;
typedef void *vdoa_handle_t;

int vdoa_setup(vdoa_handle_t handle, struct vdoa_params *params);
void vdoa_get_output_buf(vdoa_handle_t handle, struct vdoa_ipu_buf *buf);
int  vdoa_start(vdoa_handle_t handle, int timeout_ms);
void vdoa_stop(vdoa_handle_t handle);
void vdoa_get_handle(vdoa_handle_t *handle);
void vdoa_put_handle(vdoa_handle_t *handle);
#endif
