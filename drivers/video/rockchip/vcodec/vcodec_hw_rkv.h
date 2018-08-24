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

#ifndef __ARCH_ARM_MACH_ROCKCHIP_VCODEC_HW_RKV_H
#define __ARCH_ARM_MACH_ROCKCHIP_VCODEC_HW_RKV_H

#include "vcodec_hw_info.h"

/* hardware information */
#define REG_NUM_HEVC_DEC		(68)
#define REG_NUM_RKV_DEC			(78)

/* enable and gating register */
#define RKV_REG_EN_DEC			1
#define RKV_REG_DEC_GATING_BIT		BIT(1)

/* interrupt and error status register */
#define HEVC_INTERRUPT_REGISTER		1
#define HEVC_INTERRUPT_BIT		BIT(8)
#define HEVC_DEC_INT_RAW_BIT		BIT(9)
#define HEVC_READY_BIT			BIT(12)
#define HEVC_DEC_BUS_ERROR_BIT		BIT(13)
#define HEVC_DEC_STR_ERROR_BIT		BIT(14)
#define HEVC_DEC_TIMEOUT_BIT		BIT(15)
#define HEVC_DEC_BUFFER_EMPTY_BIT	BIT(16)
#define HEVC_DEC_COLMV_ERROR_BIT	BIT(17)
#define HEVC_DEC_ERR_MASK		(HEVC_DEC_BUS_ERROR_BIT \
					| HEVC_DEC_STR_ERROR_BIT \
					| HEVC_DEC_TIMEOUT_BIT \
					| HEVC_DEC_BUFFER_EMPTY_BIT \
					| HEVC_DEC_COLMV_ERROR_BIT)

#define RKV_DEC_INTERRUPT_REGISTER	1
#define RKV_DEC_INTERRUPT_BIT		BIT(8)
#define RKV_DEC_INT_RAW_BIT		BIT(9)
#define RKV_DEC_READY_BIT		BIT(12)
#define RKV_DEC_BUS_ERROR_BIT		BIT(13)
#define RKV_DEC_STR_ERROR_BIT		BIT(14)
#define RKV_DEC_TIMEOUT_BIT		BIT(15)
#define RKV_DEC_BUFFER_EMPTY_BIT	BIT(16)
#define RKV_DEC_COLMV_ERROR_BIT		BIT(17)
#define RKV_DEC_ERR_MASK		(RKV_DEC_BUS_ERROR_BIT \
					| RKV_DEC_STR_ERROR_BIT \
					| RKV_DEC_TIMEOUT_BIT \
					| RKV_DEC_BUFFER_EMPTY_BIT \
					| RKV_DEC_COLMV_ERROR_BIT)

/* enable and soft reset register */
#define RKV_REG_DEC_RESET		1
#define RKV_REG_DEC_RESET_BIT		BIT(20)

static const enum FORMAT_TYPE rkv_dec_fmt_tbl[] = {
	[0]  = FMT_H265D,
	[1]  = FMT_H264D,
	[2]  = FMT_VP9D,
	[3]  = FMT_TYPE_BUTT,
};

static enum FORMAT_TYPE rkv_dec_get_fmt(u32 *regs)
{
	u32 fmt_id = (regs[2] >> 20) & 0x3;
	enum FORMAT_TYPE type = rkv_dec_fmt_tbl[fmt_id];
	return type;
}

static struct vpu_task_info task_rkv[TASK_TYPE_BUTT] = {
	{
		.name = "invalid",
		.reg_en = 0,
		.reg_irq = 0,
		.reg_len = 0,
		.reg_dir_mv = 0,
		.reg_pps = -1,
		.reg_reset = -1,
		.reg_pipe = 0,
		.enable_mask = 0,
		.gating_mask = 0,
		.pipe_mask = 0,
		.irq_mask = 0,
		.ready_mask = 0,
		.error_mask = 0,
		.reset_mask = 0,
		.get_fmt = NULL,
	},
	{
		.name = "rkvdec",
		.reg_rlc = 4,
		.reg_en = RKV_REG_EN_DEC,
		.reg_irq = RKV_DEC_INTERRUPT_REGISTER,
		.reg_len = 4,
		.reg_dir_mv = 52,
		.reg_pps = 42,
		.reg_reset = RKV_REG_DEC_RESET,
		.reg_pipe = 0,
		.enable_mask = 0,
		.gating_mask = RKV_REG_DEC_GATING_BIT,
		.irq_mask = HEVC_INTERRUPT_BIT,
		.pipe_mask = 0,
		.ready_mask = HEVC_READY_BIT,
		.error_mask = HEVC_DEC_ERR_MASK,
		.reset_mask = RKV_REG_DEC_RESET_BIT,
		.get_fmt = rkv_dec_get_fmt,
	},
	{
		.name = "invalid",
		.reg_en = 0,
		.reg_irq = 0,
		.reg_len = 0,
		.reg_dir_mv = 0,
		.reg_pps = -1,
		.reg_reset = -1,
		.reg_pipe = 0,
		.enable_mask = 0,
		.gating_mask = 0,
		.pipe_mask = 0,
		.irq_mask = 0,
		.ready_mask = 0,
		.error_mask = 0,
		.reset_mask = 0,
		.get_fmt = NULL,
	},
	{
		.name = "invalid",
		.reg_en = 0,
		.reg_irq = 0,
		.reg_len = 0,
		.reg_dir_mv = 0,
		.reg_pps = -1,
		.reg_reset = -1,
		.reg_pipe = 0,
		.enable_mask = 0,
		.gating_mask = 0,
		.pipe_mask = 0,
		.irq_mask = 0,
		.ready_mask = 0,
		.error_mask = 0,
		.reset_mask = 0,
		.get_fmt = NULL,
	},};

static struct vpu_hw_info hw_rkhevc = {
	.hw_id		= HEVC_ID,

	.enc_offset	= 0,
	.enc_reg_num	= 0,
	.enc_io_size	= 0,

	.dec_offset	= 0,
	.dec_reg_num	= REG_NUM_HEVC_DEC,
	.dec_io_size	= REG_NUM_HEVC_DEC * 4,

	/* NOTE: can not write to register 0 */
	.base_dec	= 1,
	.base_pp	= 0,
	.base_dec_pp	= 0,
	.end_dec	= REG_NUM_HEVC_DEC,
	.end_pp		= 0,
	.end_dec_pp	= 0,
};

static struct vpu_hw_info hw_rkvdec = {
	.hw_id		= RKV_DEC_ID,

	.enc_offset	= 0,
	.enc_reg_num	= 0,
	.enc_io_size	= 0,

	.dec_offset	= 0x0,
	.dec_reg_num	= REG_NUM_RKV_DEC,
	.dec_io_size	= REG_NUM_RKV_DEC * 4,

	/* NOTE: can not write to register 0 */
	.base_dec	= 1,
	.base_pp	= 0,
	.base_dec_pp	= 0,
	.end_dec	= REG_NUM_RKV_DEC,
	.end_pp		= 0,
	.end_dec_pp	= 0,
};

/*
 * file handle translate information
 */
DEF_FMT_TRANS_TBL(rkv_h264d,
		  4,  6,  7,  10, 11, 12, 13, 14,
		  15, 16, 17, 18, 19, 20, 21, 22,
		  23, 24, 41, 42, 43, 48, 75
);

DEF_FMT_TRANS_TBL(rkv_h265d,
		  4,  6,  7,  10, 11, 12, 13, 14,
		  15, 16, 17, 18, 19, 20, 21, 22,
		  23, 24, 42, 43
);

DEF_FMT_TRANS_TBL(rkv_vp9d,
		  4,  6,  7,  11, 12, 13, 14, 15,
		  16, 52
);

const struct vpu_trans_info trans_rkv[FMT_TYPE_BUTT] = {
	EMPTY_FMT_TBL(FMT_JPEGD),
	EMPTY_FMT_TBL(FMT_H263D),
	SETUP_FMT_TBL(FMT_H264D, rkv_h264d),
	SETUP_FMT_TBL(FMT_H265D, rkv_h265d),

	EMPTY_FMT_TBL(FMT_MPEG1D),
	EMPTY_FMT_TBL(FMT_MPEG2D),
	EMPTY_FMT_TBL(FMT_MPEG4D),

	EMPTY_FMT_TBL(FMT_VP6D),
	EMPTY_FMT_TBL(FMT_VP7D),
	EMPTY_FMT_TBL(FMT_VP8D),
	SETUP_FMT_TBL(FMT_VP9D, rkv_vp9d),

	EMPTY_FMT_TBL(FMT_PP),

	EMPTY_FMT_TBL(FMT_VC1D),
	EMPTY_FMT_TBL(FMT_AVSD),

	EMPTY_FMT_TBL(FMT_JPEGE),
	EMPTY_FMT_TBL(FMT_H264E),
	EMPTY_FMT_TBL(FMT_VP8E),
};

#endif
