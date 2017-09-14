/*
 * Copyright (C) 2015 Fuzhou Rockchip Electronics Co., Ltd
 * author: chenhengming chm@rock-chips.com
 *	   Alpha Lin, alpha.lin@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ROCKCHIP_VCODEC_SERVICE_H__
#define __ROCKCHIP_VCODEC_SERVICE_H__

enum VPU_DEC_FMT {
	VPU_DEC_FMT_H264,
	VPU_DEC_FMT_MPEG4,
	VPU_DEC_FMT_H263,
	VPU_DEC_FMT_JPEG,
	VPU_DEC_FMT_VC1,
	VPU_DEC_FMT_MPEG2,
	VPU_DEC_FMT_MPEG1,
	VPU_DEC_FMT_VP6,
	VPU_DEC_FMT_RESERV0,
	VPU_DEC_FMT_VP7,
	VPU_DEC_FMT_VP8,
	VPU_DEC_FMT_AVS,
	VPU_DEC_FMT_RES
};

enum vcodec_device_id {
	VCODEC_DEVICE_ID_VPU,
	VCODEC_DEVICE_ID_HEVC,
	VCODEC_DEVICE_ID_COMBO,
	VCODEC_DEVICE_ID_RKVDEC,
	VCODEC_DEVICE_ID_BUTT
};

enum VPU_CLIENT_TYPE {
	VPU_ENC                 = 0x0,
	VPU_DEC                 = 0x1,
	VPU_PP                  = 0x2,
	VPU_DEC_PP              = 0x3,
	VPU_TYPE_BUTT,
};

/* struct for process session which connect to vpu */
struct vpu_session {
	enum VPU_CLIENT_TYPE type;
	/* a linked list of data so we can access them for debugging */
	struct list_head list_session;
	/* a linked list of register data waiting for process */
	struct list_head waiting;
	/* a linked list of register data in processing */
	struct list_head running;
	/* a linked list of register data processed */
	struct list_head done;
	wait_queue_head_t wait;
	pid_t pid;
	atomic_t task_running;
};

#endif
