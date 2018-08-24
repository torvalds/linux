/**
 * Copyright (C) 2015 Fuzhou Rockchip Electronics Co., Ltd
 * author: Herman Chen herman.chen@rock-chips.com
 *         Alpha Lin, alpha.lin@rock-chips.com
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

#ifndef __ARCH_ARM_MACH_ROCKCHIP_VCODEC_HW_INFO_H
#define __ARCH_ARM_MACH_ROCKCHIP_VCODEC_HW_INFO_H

/*
 * Hardware id is for hardware detection
 * Driver will read the hardware ID register first, then try to find a mactch
 * hardware from the enum ID below.
 */
enum VPU_HW_ID {
	VPU_DEC_ID_9190		= 0x6731,
	VPU_ID_8270		= 0x8270,
	VPU_ID_4831		= 0x4831,
	HEVC_ID			= 0x6867,
	RKV_DEC_ID		= 0x6876,
	RKV_DEC_ID2             = 0x3410,
	VPU2_ID			= 0x0000,
};

/*
 * Different hardware has different feature. So we catalogue these features
 * into three class:
 *
 * 1. register io feature determined by hardware type
 *    including register offset, register file size, etc
 *
 * 2. runtime register config feature determined by task type
 *    including irq / enable / length register, bit mask, etc
 *
 * 3. file handle translate feature determined by vcodec format type
 *    register translation map table
 *
 * These three type features composite a complete codec information structure
 */

/* VPU1 and VPU2 */
#define VCODEC_DEVICE_TYPE_VPUX		0x56505558
/* VPU Combo */
#define VCODEC_DEVICE_TYPE_VPUC		0x56505543
#define VCODEC_DEVICE_TYPE_HEVC		0x56505532
#define VCODEC_DEVICE_TYPE_RKVD		0x524B5644

enum TASK_TYPE {
	TASK_ENC,
	TASK_DEC,
	TASK_PP,
	TASK_DEC_PP,
	TASK_TYPE_BUTT,
};

enum FORMAT_TYPE {
	FMT_DEC_BASE = 0,
	FMT_JPEGD = FMT_DEC_BASE,

	FMT_H263D,
	FMT_H264D,
	FMT_H265D,

	FMT_MPEG1D,
	FMT_MPEG2D,
	FMT_MPEG4D,

	FMT_VP6D,
	FMT_VP7D,
	FMT_VP8D,
	FMT_VP9D,

	FMT_VC1D,
	FMT_AVSD,

	FMT_DEC_BUTT,

	FMT_PP_BASE = FMT_DEC_BUTT,
	FMT_PP = FMT_PP_BASE,
	FMT_PP_BUTT,

	FMT_ENC_BASE = FMT_PP_BUTT,
	FMT_JPEGE = FMT_ENC_BASE,

	FMT_H264E,

	FMT_VP8E,

	FMT_ENC_BUTT,
	FMT_TYPE_BUTT = FMT_ENC_BUTT,
};

/**
 * struct for hardware task operation
 */
struct vpu_hw_info {
	enum VPU_HW_ID	hw_id;
	u32		enc_offset;
	u32		enc_reg_num;
	u32		enc_io_size;

	u32		dec_offset;
	u32		dec_reg_num;
	u32		dec_io_size;

	/*
	 * register range for enc/dec/pp/dec_pp
	 * base/end of dec/pp/dec_pp specify the register range to config
	 */
	u32		base_dec;
	u32		base_pp;
	u32		base_dec_pp;
	u32		end_dec;
	u32		end_pp;
	u32		end_dec_pp;
};

struct vpu_task_info {
	char *name;
	struct timeval start;
	struct timeval end;

	/*
	 * input stream register
	 * use for map/unmap drm buffer for avoiding
	 * cache sync issue
	 */
	int reg_rlc;
	/*
	 * task enable register
	 * use for enable hardware task process
	 *  -1 for invalid
	 */
	int reg_en;

	/* register of task auto gating, alway valid */
	int reg_gating;

	/* register of task irq, alway valid */
	int reg_irq;

	/*
	 * stream length register
	 * only valid for decoder task
	 * -1 for invalid (encoder)
	 */
	int reg_len;

	/*
	 * direct mv register
	 * special offset scale, offset multiply by 16
	 *
	 * valid on vpu & vpu2
	 * -1 for invalid
	 */
	int reg_dir_mv;

	/*
	 * pps register
	 * special register for scaling list address process
	 *
	 * valid on rkv
	 * -1 for invalid
	 */
	int reg_pps;

	/*
	 * soft reset register
	 * special register for soft reset
	 * valid on vpu & vpu2 & rkv
	 */
	int reg_reset;

	/*
	 * decoder pipeline mode register
	 *
	 * valid on vpu & vpu2
	 * -1 for invalid
	 */
	int reg_pipe;

	/* task enable bit mask for enable register */
	u32 enable_mask;

	/* task auto gating mask for enable register */
	u32 gating_mask;

	/* task pipeline mode mask for pipe register */
	u32 pipe_mask;

	/* task inturrpt bit mask for irq register */
	u32 irq_mask;

	/* task ready bit mask for irq register */
	u32 ready_mask;

	/* task error bit mask for irq register */
	u32 error_mask;

	/* task reset bit mask for reset register */
	u32 reset_mask;

	enum FORMAT_TYPE (*get_fmt)(u32 *regs);
};

struct vpu_trans_info {
	const size_t count;
	const char * const table;
};

struct vcodec_info {
	enum VPU_HW_ID			hw_id;
	struct vpu_hw_info		*hw_info;
	struct vpu_task_info		*task_info;
	const struct vpu_trans_info	*trans_info;
};

struct vcodec_device_info {
	s32 device_type;
	s8 *name;
};

#define DEF_FMT_TRANS_TBL(fmt, args...) \
	static const char trans_tbl_##fmt[] = { \
		args \
	}

#define SETUP_FMT_TBL(id, fmt) \
	[id] = { \
		.count = sizeof(trans_tbl_##fmt), \
		.table = trans_tbl_##fmt, \
	}

#define EMPTY_FMT_TBL(id) \
	[id] = { \
		.count = 0, \
		.table = NULL, \
	}

#endif
