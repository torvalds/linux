/**
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

#ifndef __ARCH_ARM_MACH_ROCKCHIP_VCODEC_HW_VPU_H
#define __ARCH_ARM_MACH_ROCKCHIP_VCODEC_HW_VPU_H

#include "vcodec_hw_info.h"

/* hardware information */
#define REG_NUM_9190_DEC		(60)
#define REG_NUM_9190_PP			(41)
#define REG_NUM_9190_DEC_PP		(REG_NUM_9190_DEC + REG_NUM_9190_PP)

#define REG_NUM_DEC_PP			(REG_NUM_9190_DEC + REG_NUM_9190_PP)

#define REG_NUM_ENC_8270		(96)
#define REG_SIZE_ENC_8270		(0x200)
#define REG_NUM_ENC_4831		(164)
#define REG_SIZE_ENC_4831		(0x400)


/* enable and gating register */
#define VPU_REG_EN_ENC			14
#define VPU_REG_ENC_GATE		2
#define VPU_REG_ENC_GATE_BIT		BIT(4)

#define VPU_REG_EN_DEC			1
#define VPU_REG_DEC_GATE		2
#define VPU_REG_DEC_GATE_BIT		BIT(10)
#define VPU_REG_EN_PP			0
#define VPU_REG_PP_GATE			1
#define VPU_REG_PP_GATE_BIT		BIT(8)
#define VPU_REG_EN_DEC_PP		1
#define VPU_REG_DEC_PP_GATE		61
#define VPU_REG_DEC_PP_GATE_BIT		BIT(8)

/* interrupt and error status register */
#define VPU_DEC_INTERRUPT_REGISTER      1
#define VPU_DEC_INTERRUPT_BIT	        BIT(8)
#define VPU_DEC_READY_BIT	        BIT(12)
#define VPU_DEC_BUS_ERROR_BIT		BIT(13)
#define VPU_DEC_BUFFER_EMPTY_BIT	BIT(14)
#define VPU_DEC_ASO_ERROR_BIT		BIT(15)
#define VPU_DEC_STREAM_ERROR_BIT	BIT(16)
#define VPU_DEC_SLICE_DONE_BIT		BIT(17)
#define VPU_DEC_TIMEOUT_BIT		BIT(18)
#define VPU_DEC_ERR_MASK		(VPU_DEC_BUS_ERROR_BIT \
					|VPU_DEC_BUFFER_EMPTY_BIT \
					|VPU_DEC_STREAM_ERROR_BIT \
					|VPU_DEC_TIMEOUT_BIT)

#define VPU_PP_INTERRUPT_REGISTER	60
#define VPU_PP_PIPELINE_MODE_BIT	BIT(1)
#define VPU_PP_INTERRUPT_BIT		BIT(8)
#define VPU_PP_READY_BIT		BIT(12)
#define VPU_PP_BUS_ERROR_BIT		BIT(13)
#define VPU_PP_ERR_MASK			VPU_PP_BUS_ERROR_BIT

#define VPU_ENC_INTERRUPT_REGISTER	1
#define VPU_ENC_INTERRUPT_BIT		BIT(0)
#define VPU_ENC_READY_BIT		BIT(2)
#define VPU_ENC_BUS_ERROR_BIT		BIT(3)
#define VPU_ENC_BUFFER_FULL_BIT		BIT(5)
#define VPU_ENC_TIMEOUT_BIT		BIT(6)
#define VPU_ENC_ERR_MASK		(VPU_ENC_BUS_ERROR_BIT \
					|VPU_ENC_BUFFER_FULL_BIT \
					|VPU_ENC_TIMEOUT_BIT)

static const enum FORMAT_TYPE vpu_dec_fmt_tbl[] = {
	[0]  = FMT_H264D,
	[1]  = FMT_MPEG4D,
	[2]  = FMT_H263D,
	[3]  = FMT_JPEGD,
	[4]  = FMT_VC1D,
	[5]  = FMT_MPEG2D,
	[6]  = FMT_MPEG1D,
	[7]  = FMT_VP6D,
	[8]  = FMT_TYPE_BUTT,
	[9]  = FMT_VP7D,
	[10] = FMT_VP8D,
	[11] = FMT_AVSD,
	[12] = FMT_TYPE_BUTT,
	[13] = FMT_TYPE_BUTT,
	[14] = FMT_TYPE_BUTT,
	[15] = FMT_TYPE_BUTT,
};

static enum FORMAT_TYPE vpu_dec_get_fmt(u32 *regs)
{
	u32 fmt_id = (regs[3] >> 28) & 0xf;
	enum FORMAT_TYPE type = vpu_dec_fmt_tbl[fmt_id];
	return type;
}

static enum FORMAT_TYPE vpu_pp_get_fmt(u32 *regs)
{
	return FMT_PP;
}

static const enum FORMAT_TYPE vpu_enc_fmt_tbl[] = {
	[0]  = FMT_TYPE_BUTT,
	[1]  = FMT_VP8E,
	[2]  = FMT_JPEGE,
	[3]  = FMT_H264E,
};

static enum FORMAT_TYPE vpu_enc_get_fmt(u32 *regs)
{
	u32 fmt_id = (regs[VPU_REG_EN_ENC] >> 1) & 0x3;
	enum FORMAT_TYPE type = vpu_enc_fmt_tbl[fmt_id];
	return type;
}

static struct vpu_task_info task_vpu[TASK_TYPE_BUTT] = {
	{
		.name = "vpu_enc",
		.reg_rlc = 11,
		.reg_en = VPU_REG_EN_ENC,
		.reg_irq = VPU_ENC_INTERRUPT_REGISTER,
		.reg_len = -1,
		.reg_dir_mv = -1,
		.reg_pps = -1,
		.reg_pipe = -1,
		.enable_mask = 0x6,
		.gating_mask = 0,
		.pipe_mask = 0,
		.irq_mask = VPU_ENC_INTERRUPT_BIT,
		.ready_mask = VPU_ENC_READY_BIT,
		.error_mask = VPU_ENC_ERR_MASK,
		.get_fmt = vpu_enc_get_fmt,
	},
	{
		.name = "vpu_dec",
		.reg_rlc = 12,
		.reg_en = VPU_REG_EN_DEC,
		.reg_irq = VPU_DEC_INTERRUPT_REGISTER,
		.reg_len = 12,
		.reg_dir_mv = 41,
		.reg_pps = -1,
		.reg_pipe = VPU_PP_INTERRUPT_REGISTER,
		.enable_mask = 0,
		.gating_mask = 0,
		.pipe_mask = VPU_PP_PIPELINE_MODE_BIT,
		.irq_mask = VPU_DEC_INTERRUPT_BIT,
		.ready_mask = VPU_DEC_READY_BIT,
		.error_mask = VPU_DEC_ERR_MASK,
		.get_fmt = vpu_dec_get_fmt,
	},
	{
		.name = "vpu_pp",
		.reg_en = VPU_REG_EN_PP,
		.reg_irq = VPU_PP_INTERRUPT_REGISTER,
		.reg_len = -1,
		.reg_dir_mv = -1,
		.reg_pps = -1,
		.reg_pipe = VPU_PP_INTERRUPT_REGISTER,
		.enable_mask = 0,
		.gating_mask = 0,
		.pipe_mask = VPU_PP_PIPELINE_MODE_BIT,
		.irq_mask = VPU_PP_INTERRUPT_BIT,
		.ready_mask = VPU_PP_READY_BIT,
		.error_mask = VPU_PP_ERR_MASK,
		.get_fmt = vpu_pp_get_fmt,
	},
	{
		.name = "vpu_dec_pp",
		.reg_rlc = 12,
		.reg_en = VPU_REG_EN_DEC,
		.reg_irq = VPU_DEC_INTERRUPT_REGISTER,
		.reg_len = 12,
		.reg_dir_mv = 41,
		.reg_pps = -1,
		.reg_pipe = VPU_PP_INTERRUPT_REGISTER,
		.enable_mask = 0,
		.gating_mask = 0,
		.pipe_mask = VPU_PP_PIPELINE_MODE_BIT,
		.irq_mask = VPU_DEC_INTERRUPT_BIT,
		.ready_mask = VPU_DEC_READY_BIT,
		.error_mask = VPU_DEC_ERR_MASK,
		.get_fmt = vpu_dec_get_fmt,
	},
};

static struct vpu_hw_info hw_vpu_8270 = {
	.hw_id		= VPU_ID_8270,

	.enc_offset	= 0x0,
	.enc_reg_num	= REG_NUM_ENC_8270,
	.enc_io_size	= REG_NUM_ENC_8270 * 4,

	.dec_offset	= REG_SIZE_ENC_8270,
	.dec_reg_num	= REG_NUM_9190_DEC_PP,
	.dec_io_size	= REG_NUM_9190_DEC_PP * 4,

	.base_dec	= 0,
	.base_pp	= VPU_PP_INTERRUPT_REGISTER,
	.base_dec_pp	= 0,
	.end_dec	= REG_NUM_9190_DEC,
	.end_pp		= REG_NUM_9190_DEC_PP,
	.end_dec_pp	= REG_NUM_9190_DEC_PP,
};

static struct vpu_hw_info hw_vpu_4831 = {
	.hw_id		= VPU_ID_4831,

	.enc_offset	= 0x0,
	.enc_reg_num	= REG_NUM_ENC_4831,
	.enc_io_size	= REG_NUM_ENC_4831 * 4,

	.dec_offset	= REG_SIZE_ENC_4831,
	.dec_reg_num	= REG_NUM_9190_DEC_PP,
	.dec_io_size	= REG_NUM_9190_DEC_PP * 4,

	.base_dec	= 0,
	.base_pp	= VPU_PP_INTERRUPT_REGISTER,
	.base_dec_pp	= 0,
	.end_dec	= REG_NUM_9190_DEC,
	.end_pp		= REG_NUM_9190_DEC_PP,
	.end_dec_pp	= REG_NUM_9190_DEC_PP,
};

static struct vpu_hw_info hw_vpu_9190 = {
	.hw_id		= VPU_DEC_ID_9190,

	.enc_offset	= 0x0,
	.enc_reg_num	= 0,
	.enc_io_size	= 0,

	.dec_offset	= 0,
	.dec_reg_num	= REG_NUM_9190_DEC_PP,
	.dec_io_size	= REG_NUM_9190_DEC_PP * 4,

	.base_dec	= 0,
	.base_pp	= VPU_PP_INTERRUPT_REGISTER,
	.base_dec_pp	= 0,
	.end_dec	= REG_NUM_9190_DEC,
	.end_pp		= REG_NUM_9190_DEC_PP,
	.end_dec_pp	= REG_NUM_9190_DEC_PP,
};

/*
 * file handle translate information
 */
DEF_FMT_TRANS_TBL(vpu_jpegd,
		  12, 13, 14, 40, 66, 67
);

DEF_FMT_TRANS_TBL(vpu_h264d,
		  12, 13, 14, 15, 16, 17, 18, 19,
		  20, 21, 22, 23, 24, 25, 26, 27,
		  28, 29, 40, 41
);

DEF_FMT_TRANS_TBL(vpu_vp6d,
		  12, 13, 14, 18, 27, 40
);

DEF_FMT_TRANS_TBL(vpu_vp8d,
		  10, 12, 13, 14, 18, 19, 22, 23,
		  24, 25, 26, 27, 28, 29, 40
);

DEF_FMT_TRANS_TBL(vpu_vc1d,
		  12, 13, 14, 15, 16, 17, 27, 41
);

DEF_FMT_TRANS_TBL(vpu_avsd,
		  12, 13, 14, 15, 16, 17, 40, 41, 45
);

DEF_FMT_TRANS_TBL(vpu_defaultd,
		  12, 13, 14, 15, 16, 17, 40, 41
);

DEF_FMT_TRANS_TBL(vpu_default_pp,
		  63, 64, 65, 66, 67, 73, 74
);

DEF_FMT_TRANS_TBL(vpu_vp8e,
		  5, 6, 7, 8, 9, 10, 11, 12, 13, 16, 17, 26, 51, 52, 58, 59
);

DEF_FMT_TRANS_TBL(vpu_defaulte,
		  5, 6, 7, 8, 9, 10, 11, 12, 13, 51
);

static const struct vpu_trans_info trans_vpu[FMT_TYPE_BUTT] = {
	SETUP_FMT_TBL(FMT_JPEGD, vpu_jpegd),
	SETUP_FMT_TBL(FMT_H263D, vpu_defaultd),
	SETUP_FMT_TBL(FMT_H264D, vpu_h264d),
	EMPTY_FMT_TBL(FMT_H265D),

	SETUP_FMT_TBL(FMT_MPEG1D, vpu_defaultd),
	SETUP_FMT_TBL(FMT_MPEG2D, vpu_defaultd),
	SETUP_FMT_TBL(FMT_MPEG4D, vpu_defaultd),

	SETUP_FMT_TBL(FMT_VP6D, vpu_vp6d),
	SETUP_FMT_TBL(FMT_VP7D, vpu_defaultd),
	SETUP_FMT_TBL(FMT_VP8D, vpu_vp8d),
	EMPTY_FMT_TBL(FMT_VP9D),

	SETUP_FMT_TBL(FMT_VC1D, vpu_vc1d),
	SETUP_FMT_TBL(FMT_AVSD, vpu_avsd),

	SETUP_FMT_TBL(FMT_PP, vpu_default_pp),

	SETUP_FMT_TBL(FMT_JPEGE, vpu_defaulte),
	SETUP_FMT_TBL(FMT_H264E, vpu_defaulte),
	SETUP_FMT_TBL(FMT_VP8E, vpu_vp8e),
};

#endif
