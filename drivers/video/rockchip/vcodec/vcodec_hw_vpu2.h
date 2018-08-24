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

#ifndef __ARCH_ARM_MACH_ROCKCHIP_VCODEC_HW_VPU2_H
#define __ARCH_ARM_MACH_ROCKCHIP_VCODEC_HW_VPU2_H

#include "vcodec_hw_info.h"

/* hardware information */
#define REG_NUM_VPU2_DEC		(159)
#define REG_NUM_VPU2_DEC_START		(50)
#define REG_NUM_VPU2_DEC_END		(159)
#define REG_NUM_VPU2_PP			(41)
#define REG_NUM_VPU2_DEC_PP		(159)
#define REG_NUM_VPU2_ENC		(184)
#define REG_NUM_VPU2_DEC_OFFSET		(0x400)

/* enable and gating register */
#define VPU2_REG_EN_ENC			103
#define VPU2_REG_ENC_GATE		109
#define VPU2_REG_ENC_GATE_BIT		BIT(4)

#define VPU2_REG_EN_DEC			57
#define VPU2_REG_DEC_GATE		57
#define VPU2_REG_DEC_GATE_BIT		BIT(4)
#define VPU2_REG_EN_PP			41
#define VPU2_REG_PP_GATE		1
#define VPU2_REG_PP_GATE_BIT		BIT(8)
#define VPU2_REG_EN_DEC_PP		57
#define VPU2_REG_DEC_PP_GATE		57
#define VPU2_REG_DEC_PP_GATE_BIT	BIT(4)

/* interrupt and error status register */
#define VPU2_DEC_INTERRUPT_REGISTER	55
#define VPU2_DEC_INTERRUPT_BIT		BIT(0)
#define VPU2_DEC_READY_BIT		BIT(4)
#define VPU2_DEC_BUS_ERROR_BIT		BIT(5)
#define VPU2_DEC_BUFFER_EMPTY_BIT	BIT(6)
#define VPU2_DEC_ASO_ERROR_BIT		BIT(8)
#define VPU2_DEC_SLICE_DONE_BIT		BIT(9)
#define VPU2_DEC_STREAM_ERROR_BIT	BIT(12)
#define VPU2_DEC_TIMEOUT_BIT		BIT(13)
#define VPU2_DEC_ERR_MASK		(VPU2_DEC_BUS_ERROR_BIT \
					| VPU2_DEC_BUFFER_EMPTY_BIT \
					| VPU2_DEC_STREAM_ERROR_BIT \
					| VPU2_DEC_TIMEOUT_BIT)

/*enable and soft reset register*/
#define VPU2_REG_DEC_RESET		58
#define VPU2_REG_DEC_RESET_BIT		BIT(0)

#define VPU2_PP_INTERRUPT_REGISTER	40
#define VPU2_PP_INTERRUPT_BIT		BIT(0)
#define VPU2_PP_READY_BIT		BIT(2)
#define VPU2_PP_BUS_ERROR_BIT		BIT(3)
#define VPU2_PP_ERR_MASK		VPU2_PP_BUS_ERROR_BIT
#define VPU2_PP_PIPELINE_REGISTER	41
#define VPU2_PP_PIPELINE_MODE_BIT	BIT(4)

#define VPU2_ENC_INTERRUPT_REGISTER	109
#define VPU2_ENC_INTERRUPT_BIT		BIT(0)
#define VPU2_ENC_READY_BIT		BIT(1)
#define VPU2_ENC_BUS_ERROR_BIT		BIT(4)
#define VPU2_ENC_BUFFER_FULL_BIT	BIT(5)
#define VPU2_ENC_TIMEOUT_BIT		BIT(6)
#define VPU2_ENC_ERR_MASK		(VPU2_ENC_BUS_ERROR_BIT \
					| VPU2_ENC_BUFFER_FULL_BIT \
					| VPU2_ENC_TIMEOUT_BIT)

static const enum FORMAT_TYPE vpu2_dec_fmt_tbl[] = {
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

static enum FORMAT_TYPE vpu2_dec_get_fmt(u32 *regs)
{
	u32 fmt_id = regs[53] & 0xf;
	enum FORMAT_TYPE type = vpu2_dec_fmt_tbl[fmt_id];
	return type;
}

static enum FORMAT_TYPE vpu2_pp_get_fmt(u32 *regs)
{
	return FMT_PP;
}

static const enum FORMAT_TYPE vpu2_enc_fmt_tbl[] = {
	[0]  = FMT_TYPE_BUTT,
	[1]  = FMT_VP8E,
	[2]  = FMT_JPEGE,
	[3]  = FMT_H264E,
};

static enum FORMAT_TYPE vpu2_enc_get_fmt(u32 *regs)
{
	u32 fmt_id = (regs[VPU2_REG_EN_ENC] >> 4) & 0x3;
	enum FORMAT_TYPE type = vpu2_enc_fmt_tbl[fmt_id];
	return type;
}

static struct vpu_task_info task_vpu2[TASK_TYPE_BUTT] = {
	{
		.name = "vpu2_enc",
		.reg_rlc = 48,
		.reg_en = VPU2_REG_EN_ENC,
		.reg_gating = VPU2_REG_ENC_GATE,
		.reg_irq = VPU2_ENC_INTERRUPT_REGISTER,
		.reg_len = -1,
		.reg_dir_mv = -1,
		.reg_pps = -1,
		.reg_reset = -1,
		.reg_pipe = -1,
		.enable_mask = 0x30,
		.gating_mask = VPU2_REG_ENC_GATE_BIT,
		.pipe_mask = 0,
		.irq_mask = VPU2_ENC_INTERRUPT_BIT,
		.ready_mask = VPU2_ENC_READY_BIT,
		.error_mask = VPU2_ENC_ERR_MASK,
		.reset_mask = 0,
		.get_fmt = vpu2_enc_get_fmt,
	},
	{
		.name = "vpu2_dec",
		.reg_rlc = 64,
		.reg_en = VPU2_REG_EN_DEC,
		.reg_irq = VPU2_DEC_INTERRUPT_REGISTER,
		.reg_len = 64,
		.reg_dir_mv = 62,
		.reg_pps = -1,
		.reg_reset = VPU2_REG_DEC_RESET,
		.reg_pipe = VPU2_PP_PIPELINE_REGISTER,
		.enable_mask = 0,
		.gating_mask = VPU2_REG_DEC_GATE_BIT,
		.pipe_mask = VPU2_PP_PIPELINE_MODE_BIT,
		.irq_mask = VPU2_DEC_INTERRUPT_BIT,
		.ready_mask = VPU2_DEC_READY_BIT,
		.error_mask = VPU2_DEC_ERR_MASK,
		.reset_mask = VPU2_REG_DEC_RESET_BIT,
		.get_fmt = vpu2_dec_get_fmt,
	},
	{
		.name = "vpu2_pp",
		.reg_en = VPU2_REG_EN_PP,
		.reg_irq = VPU2_PP_INTERRUPT_REGISTER,
		.reg_len = -1,
		.reg_dir_mv = -1,
		.reg_pps = -1,
		.reg_pipe = VPU2_PP_PIPELINE_REGISTER,
		.enable_mask = 0,
		.gating_mask = VPU2_REG_PP_GATE_BIT,
		.pipe_mask = VPU2_PP_PIPELINE_MODE_BIT,
		.irq_mask = VPU2_PP_INTERRUPT_BIT,
		.ready_mask = VPU2_PP_READY_BIT,
		.error_mask = VPU2_PP_ERR_MASK,
		.reset_mask = 0,
		.get_fmt = vpu2_pp_get_fmt,
	},
	{
		.name = "vpu2_dec_pp",
		.reg_rlc = 64,
		.reg_en = VPU2_REG_EN_DEC_PP,
		.reg_irq = VPU2_DEC_INTERRUPT_REGISTER,
		.reg_len = 64,
		.reg_dir_mv = 62,
		.reg_pps = -1,
		.reg_pipe = VPU2_PP_PIPELINE_REGISTER,
		.enable_mask = 0,
		.gating_mask = VPU2_REG_DEC_GATE_BIT,
		.pipe_mask = VPU2_PP_PIPELINE_MODE_BIT,
		.irq_mask = VPU2_DEC_INTERRUPT_BIT,
		.ready_mask = VPU2_DEC_READY_BIT,
		.error_mask = VPU2_DEC_ERR_MASK,
		.reset_mask = 0,
		.get_fmt = vpu2_dec_get_fmt,
	},
};

static struct vpu_hw_info hw_vpu2 = {
	.hw_id		= VPU2_ID,

	.enc_offset	= 0x0,
	.enc_reg_num	= REG_NUM_VPU2_ENC,
	.enc_io_size	= REG_NUM_VPU2_ENC * 4,

	.dec_offset	= REG_NUM_VPU2_DEC_OFFSET,
	.dec_reg_num	= REG_NUM_VPU2_DEC_PP,
	.dec_io_size	= REG_NUM_VPU2_DEC_PP * 4,

	.base_dec	= REG_NUM_VPU2_DEC_START,
	.base_pp	= 0,
	.base_dec_pp	= 0,
	.end_dec	= REG_NUM_VPU2_DEC_END,
	.end_pp		= REG_NUM_VPU2_PP,
	.end_dec_pp	= REG_NUM_VPU2_DEC_END,
};

/*
 * file handle translate information
 */
DEF_FMT_TRANS_TBL(vpu2_jpegd,
		  131, 64, 63, 61, 21, 22
);

DEF_FMT_TRANS_TBL(vpu2_h264d,
		  61, 62, 63, 64, 84, 85, 86, 87, 88, 89,
		  90, 91, 92, 93, 94, 95, 96, 97,
		  98, 99,
);

DEF_FMT_TRANS_TBL(vpu2_vp6d,
		  64, 63, 131, 136, 145, 61
);

DEF_FMT_TRANS_TBL(vpu2_vp8d,
		  149,  64,  63, 131, 136, 137, 140, 141,
		  142, 143, 144, 145, 146, 147, 61
);

DEF_FMT_TRANS_TBL(vpu2_vc1d,
		  64, 63, 131, 148, 134, 135, 145, 62
);

DEF_FMT_TRANS_TBL(vpu2_default_dec,
		  61, 62, 64, 63, 131, 148, 134, 135,
);

DEF_FMT_TRANS_TBL(vpu2_default_pp,
		  12, 13, 18, 19, 20, 21, 22
);

DEF_FMT_TRANS_TBL(vpu2_default_enc,
		  77, 78, 56, 57, 63, 64, 48, 49,
		  50, 81
);

const struct vpu_trans_info trans_vpu2[FMT_TYPE_BUTT] = {
	SETUP_FMT_TBL(FMT_JPEGD, vpu2_jpegd),
	SETUP_FMT_TBL(FMT_H263D, vpu2_default_dec),
	SETUP_FMT_TBL(FMT_H264D, vpu2_h264d),
	EMPTY_FMT_TBL(FMT_H265D),

	SETUP_FMT_TBL(FMT_MPEG1D, vpu2_default_dec),
	SETUP_FMT_TBL(FMT_MPEG2D, vpu2_default_dec),
	SETUP_FMT_TBL(FMT_MPEG4D, vpu2_default_dec),

	SETUP_FMT_TBL(FMT_VP6D, vpu2_vp6d),
	SETUP_FMT_TBL(FMT_VP7D, vpu2_default_dec),
	SETUP_FMT_TBL(FMT_VP8D, vpu2_vp8d),
	EMPTY_FMT_TBL(FMT_VP9D),

	SETUP_FMT_TBL(FMT_PP, vpu2_default_pp),

	SETUP_FMT_TBL(FMT_VC1D, vpu2_vc1d),
	SETUP_FMT_TBL(FMT_AVSD, vpu2_default_dec),

	SETUP_FMT_TBL(FMT_JPEGE, vpu2_default_enc),
	SETUP_FMT_TBL(FMT_H264E, vpu2_default_enc),
	SETUP_FMT_TBL(FMT_VP8E, vpu2_default_enc),
};

#endif
