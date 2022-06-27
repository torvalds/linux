// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VPU codec driver
 *
 * Copyright (c) 2014 Rockchip Electronics Co., Ltd.
 *	Hertz Wong <hertz.wong@rock-chips.com>
 *	Herman Chen <herman.chen@rock-chips.com>
 *
 * Copyright (C) 2014 Google, Inc.
 *	Tomasz Figa <tfiga@chromium.org>
 */

#include <linux/types.h>
#include <linux/sort.h>

#include <media/v4l2-mem2mem.h>

#include "hantro_hw.h"
#include "hantro_v4l2.h"

#define VDPU_SWREG(nr)			((nr) * 4)

#define VDPU_REG_DEC_OUT_BASE		VDPU_SWREG(63)
#define VDPU_REG_RLC_VLC_BASE		VDPU_SWREG(64)
#define VDPU_REG_QTABLE_BASE		VDPU_SWREG(61)
#define VDPU_REG_DIR_MV_BASE		VDPU_SWREG(62)
#define VDPU_REG_REFER_BASE(i)		(VDPU_SWREG(84 + (i)))
#define VDPU_REG_DEC_E(v)		((v) ? BIT(0) : 0)

#define VDPU_REG_DEC_ADV_PRE_DIS(v)	((v) ? BIT(11) : 0)
#define VDPU_REG_DEC_SCMD_DIS(v)	((v) ? BIT(10) : 0)
#define VDPU_REG_FILTERING_DIS(v)	((v) ? BIT(8) : 0)
#define VDPU_REG_PIC_FIXED_QUANT(v)	((v) ? BIT(7) : 0)
#define VDPU_REG_DEC_LATENCY(v)		(((v) << 1) & GENMASK(6, 1))

#define VDPU_REG_INIT_QP(v)		(((v) << 25) & GENMASK(30, 25))
#define VDPU_REG_STREAM_LEN(v)		(((v) << 0) & GENMASK(23, 0))

#define VDPU_REG_APF_THRESHOLD(v)	(((v) << 17) & GENMASK(30, 17))
#define VDPU_REG_STARTMB_X(v)		(((v) << 8) & GENMASK(16, 8))
#define VDPU_REG_STARTMB_Y(v)		(((v) << 0) & GENMASK(7, 0))

#define VDPU_REG_DEC_MODE(v)		(((v) << 0) & GENMASK(3, 0))

#define VDPU_REG_DEC_STRENDIAN_E(v)	((v) ? BIT(5) : 0)
#define VDPU_REG_DEC_STRSWAP32_E(v)	((v) ? BIT(4) : 0)
#define VDPU_REG_DEC_OUTSWAP32_E(v)	((v) ? BIT(3) : 0)
#define VDPU_REG_DEC_INSWAP32_E(v)	((v) ? BIT(2) : 0)
#define VDPU_REG_DEC_OUT_ENDIAN(v)	((v) ? BIT(1) : 0)
#define VDPU_REG_DEC_IN_ENDIAN(v)	((v) ? BIT(0) : 0)

#define VDPU_REG_DEC_DATA_DISC_E(v)	((v) ? BIT(22) : 0)
#define VDPU_REG_DEC_MAX_BURST(v)	(((v) << 16) & GENMASK(20, 16))
#define VDPU_REG_DEC_AXI_WR_ID(v)	(((v) << 8) & GENMASK(15, 8))
#define VDPU_REG_DEC_AXI_RD_ID(v)	(((v) << 0) & GENMASK(7, 0))

#define VDPU_REG_START_CODE_E(v)	((v) ? BIT(22) : 0)
#define VDPU_REG_CH_8PIX_ILEAV_E(v)	((v) ? BIT(21) : 0)
#define VDPU_REG_RLC_MODE_E(v)		((v) ? BIT(20) : 0)
#define VDPU_REG_PIC_INTERLACE_E(v)	((v) ? BIT(17) : 0)
#define VDPU_REG_PIC_FIELDMODE_E(v)	((v) ? BIT(16) : 0)
#define VDPU_REG_PIC_TOPFIELD_E(v)	((v) ? BIT(13) : 0)
#define VDPU_REG_WRITE_MVS_E(v)		((v) ? BIT(10) : 0)
#define VDPU_REG_SEQ_MBAFF_E(v)		((v) ? BIT(7) : 0)
#define VDPU_REG_PICORD_COUNT_E(v)	((v) ? BIT(6) : 0)
#define VDPU_REG_DEC_TIMEOUT_E(v)	((v) ? BIT(5) : 0)
#define VDPU_REG_DEC_CLK_GATE_E(v)	((v) ? BIT(4) : 0)

#define VDPU_REG_PRED_BC_TAP_0_0(v)	(((v) << 22) & GENMASK(31, 22))
#define VDPU_REG_PRED_BC_TAP_0_1(v)	(((v) << 12) & GENMASK(21, 12))
#define VDPU_REG_PRED_BC_TAP_0_2(v)	(((v) << 2) & GENMASK(11, 2))

#define VDPU_REG_REFBU_E(v)		((v) ? BIT(31) : 0)

#define VDPU_REG_PINIT_RLIST_F9(v)	(((v) << 25) & GENMASK(29, 25))
#define VDPU_REG_PINIT_RLIST_F8(v)	(((v) << 20) & GENMASK(24, 20))
#define VDPU_REG_PINIT_RLIST_F7(v)	(((v) << 15) & GENMASK(19, 15))
#define VDPU_REG_PINIT_RLIST_F6(v)	(((v) << 10) & GENMASK(14, 10))
#define VDPU_REG_PINIT_RLIST_F5(v)	(((v) << 5) & GENMASK(9, 5))
#define VDPU_REG_PINIT_RLIST_F4(v)	(((v) << 0) & GENMASK(4, 0))

#define VDPU_REG_PINIT_RLIST_F15(v)	(((v) << 25) & GENMASK(29, 25))
#define VDPU_REG_PINIT_RLIST_F14(v)	(((v) << 20) & GENMASK(24, 20))
#define VDPU_REG_PINIT_RLIST_F13(v)	(((v) << 15) & GENMASK(19, 15))
#define VDPU_REG_PINIT_RLIST_F12(v)	(((v) << 10) & GENMASK(14, 10))
#define VDPU_REG_PINIT_RLIST_F11(v)	(((v) << 5) & GENMASK(9, 5))
#define VDPU_REG_PINIT_RLIST_F10(v)	(((v) << 0) & GENMASK(4, 0))

#define VDPU_REG_REFER1_NBR(v)		(((v) << 16) & GENMASK(31, 16))
#define VDPU_REG_REFER0_NBR(v)		(((v) << 0) & GENMASK(15, 0))

#define VDPU_REG_REFER3_NBR(v)		(((v) << 16) & GENMASK(31, 16))
#define VDPU_REG_REFER2_NBR(v)		(((v) << 0) & GENMASK(15, 0))

#define VDPU_REG_REFER5_NBR(v)		(((v) << 16) & GENMASK(31, 16))
#define VDPU_REG_REFER4_NBR(v)		(((v) << 0) & GENMASK(15, 0))

#define VDPU_REG_REFER7_NBR(v)		(((v) << 16) & GENMASK(31, 16))
#define VDPU_REG_REFER6_NBR(v)		(((v) << 0) & GENMASK(15, 0))

#define VDPU_REG_REFER9_NBR(v)		(((v) << 16) & GENMASK(31, 16))
#define VDPU_REG_REFER8_NBR(v)		(((v) << 0) & GENMASK(15, 0))

#define VDPU_REG_REFER11_NBR(v)		(((v) << 16) & GENMASK(31, 16))
#define VDPU_REG_REFER10_NBR(v)		(((v) << 0) & GENMASK(15, 0))

#define VDPU_REG_REFER13_NBR(v)		(((v) << 16) & GENMASK(31, 16))
#define VDPU_REG_REFER12_NBR(v)		(((v) << 0) & GENMASK(15, 0))

#define VDPU_REG_REFER15_NBR(v)		(((v) << 16) & GENMASK(31, 16))
#define VDPU_REG_REFER14_NBR(v)		(((v) << 0) & GENMASK(15, 0))

#define VDPU_REG_BINIT_RLIST_F5(v)	(((v) << 25) & GENMASK(29, 25))
#define VDPU_REG_BINIT_RLIST_F4(v)	(((v) << 20) & GENMASK(24, 20))
#define VDPU_REG_BINIT_RLIST_F3(v)	(((v) << 15) & GENMASK(19, 15))
#define VDPU_REG_BINIT_RLIST_F2(v)	(((v) << 10) & GENMASK(14, 10))
#define VDPU_REG_BINIT_RLIST_F1(v)	(((v) << 5) & GENMASK(9, 5))
#define VDPU_REG_BINIT_RLIST_F0(v)	(((v) << 0) & GENMASK(4, 0))

#define VDPU_REG_BINIT_RLIST_F11(v)	(((v) << 25) & GENMASK(29, 25))
#define VDPU_REG_BINIT_RLIST_F10(v)	(((v) << 20) & GENMASK(24, 20))
#define VDPU_REG_BINIT_RLIST_F9(v)	(((v) << 15) & GENMASK(19, 15))
#define VDPU_REG_BINIT_RLIST_F8(v)	(((v) << 10) & GENMASK(14, 10))
#define VDPU_REG_BINIT_RLIST_F7(v)	(((v) << 5) & GENMASK(9, 5))
#define VDPU_REG_BINIT_RLIST_F6(v)	(((v) << 0) & GENMASK(4, 0))

#define VDPU_REG_BINIT_RLIST_F15(v)	(((v) << 15) & GENMASK(19, 15))
#define VDPU_REG_BINIT_RLIST_F14(v)	(((v) << 10) & GENMASK(14, 10))
#define VDPU_REG_BINIT_RLIST_F13(v)	(((v) << 5) & GENMASK(9, 5))
#define VDPU_REG_BINIT_RLIST_F12(v)	(((v) << 0) & GENMASK(4, 0))

#define VDPU_REG_BINIT_RLIST_B5(v)	(((v) << 25) & GENMASK(29, 25))
#define VDPU_REG_BINIT_RLIST_B4(v)	(((v) << 20) & GENMASK(24, 20))
#define VDPU_REG_BINIT_RLIST_B3(v)	(((v) << 15) & GENMASK(19, 15))
#define VDPU_REG_BINIT_RLIST_B2(v)	(((v) << 10) & GENMASK(14, 10))
#define VDPU_REG_BINIT_RLIST_B1(v)	(((v) << 5) & GENMASK(9, 5))
#define VDPU_REG_BINIT_RLIST_B0(v)	(((v) << 0) & GENMASK(4, 0))

#define VDPU_REG_BINIT_RLIST_B11(v)	(((v) << 25) & GENMASK(29, 25))
#define VDPU_REG_BINIT_RLIST_B10(v)	(((v) << 20) & GENMASK(24, 20))
#define VDPU_REG_BINIT_RLIST_B9(v)	(((v) << 15) & GENMASK(19, 15))
#define VDPU_REG_BINIT_RLIST_B8(v)	(((v) << 10) & GENMASK(14, 10))
#define VDPU_REG_BINIT_RLIST_B7(v)	(((v) << 5) & GENMASK(9, 5))
#define VDPU_REG_BINIT_RLIST_B6(v)	(((v) << 0) & GENMASK(4, 0))

#define VDPU_REG_BINIT_RLIST_B15(v)	(((v) << 15) & GENMASK(19, 15))
#define VDPU_REG_BINIT_RLIST_B14(v)	(((v) << 10) & GENMASK(14, 10))
#define VDPU_REG_BINIT_RLIST_B13(v)	(((v) << 5) & GENMASK(9, 5))
#define VDPU_REG_BINIT_RLIST_B12(v)	(((v) << 0) & GENMASK(4, 0))

#define VDPU_REG_PINIT_RLIST_F3(v)	(((v) << 15) & GENMASK(19, 15))
#define VDPU_REG_PINIT_RLIST_F2(v)	(((v) << 10) & GENMASK(14, 10))
#define VDPU_REG_PINIT_RLIST_F1(v)	(((v) << 5) & GENMASK(9, 5))
#define VDPU_REG_PINIT_RLIST_F0(v)	(((v) << 0) & GENMASK(4, 0))

#define VDPU_REG_REFER_LTERM_E(v)	(((v) << 0) & GENMASK(31, 0))

#define VDPU_REG_REFER_VALID_E(v)	(((v) << 0) & GENMASK(31, 0))

#define VDPU_REG_STRM_START_BIT(v)	(((v) << 0) & GENMASK(5, 0))

#define VDPU_REG_CH_QP_OFFSET2(v)	(((v) << 22) & GENMASK(26, 22))
#define VDPU_REG_CH_QP_OFFSET(v)	(((v) << 17) & GENMASK(21, 17))
#define VDPU_REG_PIC_MB_HEIGHT_P(v)	(((v) << 9) & GENMASK(16, 9))
#define VDPU_REG_PIC_MB_WIDTH(v)	(((v) << 0) & GENMASK(8, 0))

#define VDPU_REG_WEIGHT_BIPR_IDC(v)	(((v) << 16) & GENMASK(17, 16))
#define VDPU_REG_REF_FRAMES(v)		(((v) << 0) & GENMASK(4, 0))

#define VDPU_REG_FILT_CTRL_PRES(v)	((v) ? BIT(31) : 0)
#define VDPU_REG_RDPIC_CNT_PRES(v)	((v) ? BIT(30) : 0)
#define VDPU_REG_FRAMENUM_LEN(v)	(((v) << 16) & GENMASK(20, 16))
#define VDPU_REG_FRAMENUM(v)		(((v) << 0) & GENMASK(15, 0))

#define VDPU_REG_REFPIC_MK_LEN(v)	(((v) << 16) & GENMASK(26, 16))
#define VDPU_REG_IDR_PIC_ID(v)		(((v) << 0) & GENMASK(15, 0))

#define VDPU_REG_PPS_ID(v)		(((v) << 24) & GENMASK(31, 24))
#define VDPU_REG_REFIDX1_ACTIVE(v)	(((v) << 19) & GENMASK(23, 19))
#define VDPU_REG_REFIDX0_ACTIVE(v)	(((v) << 14) & GENMASK(18, 14))
#define VDPU_REG_POC_LENGTH(v)		(((v) << 0) & GENMASK(7, 0))

#define VDPU_REG_IDR_PIC_E(v)		((v) ? BIT(8) : 0)
#define VDPU_REG_DIR_8X8_INFER_E(v)	((v) ? BIT(7) : 0)
#define VDPU_REG_BLACKWHITE_E(v)	((v) ? BIT(6) : 0)
#define VDPU_REG_CABAC_E(v)		((v) ? BIT(5) : 0)
#define VDPU_REG_WEIGHT_PRED_E(v)	((v) ? BIT(4) : 0)
#define VDPU_REG_CONST_INTRA_E(v)	((v) ? BIT(3) : 0)
#define VDPU_REG_8X8TRANS_FLAG_E(v)	((v) ? BIT(2) : 0)
#define VDPU_REG_TYPE1_QUANT_E(v)	((v) ? BIT(1) : 0)
#define VDPU_REG_FIELDPIC_FLAG_E(v)	((v) ? BIT(0) : 0)

static void set_params(struct hantro_ctx *ctx, struct vb2_v4l2_buffer *src_buf)
{
	const struct hantro_h264_dec_ctrls *ctrls = &ctx->h264_dec.ctrls;
	const struct v4l2_ctrl_h264_decode_params *dec_param = ctrls->decode;
	const struct v4l2_ctrl_h264_sps *sps = ctrls->sps;
	const struct v4l2_ctrl_h264_pps *pps = ctrls->pps;
	struct hantro_dev *vpu = ctx->dev;
	u32 reg;

	reg = VDPU_REG_DEC_ADV_PRE_DIS(0) |
	      VDPU_REG_DEC_SCMD_DIS(0) |
	      VDPU_REG_FILTERING_DIS(0) |
	      VDPU_REG_PIC_FIXED_QUANT(0) |
	      VDPU_REG_DEC_LATENCY(0);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(50));

	reg = VDPU_REG_INIT_QP(pps->pic_init_qp_minus26 + 26) |
	      VDPU_REG_STREAM_LEN(vb2_get_plane_payload(&src_buf->vb2_buf, 0));
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(51));

	reg = VDPU_REG_APF_THRESHOLD(8) |
	      VDPU_REG_STARTMB_X(0) |
	      VDPU_REG_STARTMB_Y(0);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(52));

	reg = VDPU_REG_DEC_MODE(0);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(53));

	reg = VDPU_REG_DEC_STRENDIAN_E(1) |
	      VDPU_REG_DEC_STRSWAP32_E(1) |
	      VDPU_REG_DEC_OUTSWAP32_E(1) |
	      VDPU_REG_DEC_INSWAP32_E(1) |
	      VDPU_REG_DEC_OUT_ENDIAN(1) |
	      VDPU_REG_DEC_IN_ENDIAN(0);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(54));

	reg = VDPU_REG_DEC_DATA_DISC_E(0) |
	      VDPU_REG_DEC_MAX_BURST(16) |
	      VDPU_REG_DEC_AXI_WR_ID(0) |
	      VDPU_REG_DEC_AXI_RD_ID(0xff);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(56));

	reg = VDPU_REG_START_CODE_E(1) |
	      VDPU_REG_CH_8PIX_ILEAV_E(0) |
	      VDPU_REG_RLC_MODE_E(0) |
	      VDPU_REG_PIC_INTERLACE_E(!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY) &&
				       (sps->flags & V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD ||
					dec_param->flags & V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC)) |
	      VDPU_REG_PIC_FIELDMODE_E(dec_param->flags & V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC) |
	      VDPU_REG_PIC_TOPFIELD_E(!(dec_param->flags & V4L2_H264_DECODE_PARAM_FLAG_BOTTOM_FIELD)) |
	      VDPU_REG_WRITE_MVS_E((sps->profile_idc > 66) && dec_param->nal_ref_idc) |
	      VDPU_REG_SEQ_MBAFF_E(sps->flags & V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD) |
	      VDPU_REG_PICORD_COUNT_E(sps->profile_idc > 66) |
	      VDPU_REG_DEC_TIMEOUT_E(1) |
	      VDPU_REG_DEC_CLK_GATE_E(1);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(57));

	reg = VDPU_REG_PRED_BC_TAP_0_0(1) |
	      VDPU_REG_PRED_BC_TAP_0_1((u32)-5) |
	      VDPU_REG_PRED_BC_TAP_0_2(20);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(59));

	reg = VDPU_REG_REFBU_E(0);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(65));

	reg = VDPU_REG_STRM_START_BIT(0);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(109));

	reg = VDPU_REG_CH_QP_OFFSET2(pps->second_chroma_qp_index_offset) |
	      VDPU_REG_CH_QP_OFFSET(pps->chroma_qp_index_offset) |
	      VDPU_REG_PIC_MB_HEIGHT_P(MB_HEIGHT(ctx->src_fmt.height)) |
	      VDPU_REG_PIC_MB_WIDTH(MB_WIDTH(ctx->src_fmt.width));
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(110));

	reg = VDPU_REG_WEIGHT_BIPR_IDC(pps->weighted_bipred_idc) |
	      VDPU_REG_REF_FRAMES(sps->max_num_ref_frames);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(111));

	reg = VDPU_REG_FILT_CTRL_PRES(pps->flags & V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT) |
	      VDPU_REG_RDPIC_CNT_PRES(pps->flags & V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT) |
	      VDPU_REG_FRAMENUM_LEN(sps->log2_max_frame_num_minus4 + 4) |
	      VDPU_REG_FRAMENUM(dec_param->frame_num);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(112));

	reg = VDPU_REG_REFPIC_MK_LEN(dec_param->dec_ref_pic_marking_bit_size) |
	      VDPU_REG_IDR_PIC_ID(dec_param->idr_pic_id);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(113));

	reg = VDPU_REG_PPS_ID(pps->pic_parameter_set_id) |
	      VDPU_REG_REFIDX1_ACTIVE(pps->num_ref_idx_l1_default_active_minus1 + 1) |
	      VDPU_REG_REFIDX0_ACTIVE(pps->num_ref_idx_l0_default_active_minus1 + 1) |
	      VDPU_REG_POC_LENGTH(dec_param->pic_order_cnt_bit_size);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(114));

	reg = VDPU_REG_IDR_PIC_E(dec_param->flags & V4L2_H264_DECODE_PARAM_FLAG_IDR_PIC) |
	      VDPU_REG_DIR_8X8_INFER_E(sps->flags & V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE) |
	      VDPU_REG_BLACKWHITE_E(sps->profile_idc >= 100 && sps->chroma_format_idc == 0) |
	      VDPU_REG_CABAC_E(pps->flags & V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE) |
	      VDPU_REG_WEIGHT_PRED_E(pps->flags & V4L2_H264_PPS_FLAG_WEIGHTED_PRED) |
	      VDPU_REG_CONST_INTRA_E(pps->flags & V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED) |
	      VDPU_REG_8X8TRANS_FLAG_E(pps->flags & V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE) |
	      VDPU_REG_TYPE1_QUANT_E(pps->flags & V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT) |
	      VDPU_REG_FIELDPIC_FLAG_E(!(sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY));
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(115));
}

static void set_ref(struct hantro_ctx *ctx)
{
	const struct v4l2_h264_reference *b0_reflist, *b1_reflist, *p_reflist;
	struct hantro_dev *vpu = ctx->dev;
	u32 reg;
	int i;

	b0_reflist = ctx->h264_dec.reflists.b0;
	b1_reflist = ctx->h264_dec.reflists.b1;
	p_reflist = ctx->h264_dec.reflists.p;

	reg = VDPU_REG_PINIT_RLIST_F9(p_reflist[9].index) |
	      VDPU_REG_PINIT_RLIST_F8(p_reflist[8].index) |
	      VDPU_REG_PINIT_RLIST_F7(p_reflist[7].index) |
	      VDPU_REG_PINIT_RLIST_F6(p_reflist[6].index) |
	      VDPU_REG_PINIT_RLIST_F5(p_reflist[5].index) |
	      VDPU_REG_PINIT_RLIST_F4(p_reflist[4].index);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(74));

	reg = VDPU_REG_PINIT_RLIST_F15(p_reflist[15].index) |
	      VDPU_REG_PINIT_RLIST_F14(p_reflist[14].index) |
	      VDPU_REG_PINIT_RLIST_F13(p_reflist[13].index) |
	      VDPU_REG_PINIT_RLIST_F12(p_reflist[12].index) |
	      VDPU_REG_PINIT_RLIST_F11(p_reflist[11].index) |
	      VDPU_REG_PINIT_RLIST_F10(p_reflist[10].index);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(75));

	reg = VDPU_REG_REFER1_NBR(hantro_h264_get_ref_nbr(ctx, 1)) |
	      VDPU_REG_REFER0_NBR(hantro_h264_get_ref_nbr(ctx, 0));
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(76));

	reg = VDPU_REG_REFER3_NBR(hantro_h264_get_ref_nbr(ctx, 3)) |
	      VDPU_REG_REFER2_NBR(hantro_h264_get_ref_nbr(ctx, 2));
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(77));

	reg = VDPU_REG_REFER5_NBR(hantro_h264_get_ref_nbr(ctx, 5)) |
	      VDPU_REG_REFER4_NBR(hantro_h264_get_ref_nbr(ctx, 4));
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(78));

	reg = VDPU_REG_REFER7_NBR(hantro_h264_get_ref_nbr(ctx, 7)) |
	      VDPU_REG_REFER6_NBR(hantro_h264_get_ref_nbr(ctx, 6));
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(79));

	reg = VDPU_REG_REFER9_NBR(hantro_h264_get_ref_nbr(ctx, 9)) |
	      VDPU_REG_REFER8_NBR(hantro_h264_get_ref_nbr(ctx, 8));
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(80));

	reg = VDPU_REG_REFER11_NBR(hantro_h264_get_ref_nbr(ctx, 11)) |
	      VDPU_REG_REFER10_NBR(hantro_h264_get_ref_nbr(ctx, 10));
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(81));

	reg = VDPU_REG_REFER13_NBR(hantro_h264_get_ref_nbr(ctx, 13)) |
	      VDPU_REG_REFER12_NBR(hantro_h264_get_ref_nbr(ctx, 12));
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(82));

	reg = VDPU_REG_REFER15_NBR(hantro_h264_get_ref_nbr(ctx, 15)) |
	      VDPU_REG_REFER14_NBR(hantro_h264_get_ref_nbr(ctx, 14));
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(83));

	reg = VDPU_REG_BINIT_RLIST_F5(b0_reflist[5].index) |
	      VDPU_REG_BINIT_RLIST_F4(b0_reflist[4].index) |
	      VDPU_REG_BINIT_RLIST_F3(b0_reflist[3].index) |
	      VDPU_REG_BINIT_RLIST_F2(b0_reflist[2].index) |
	      VDPU_REG_BINIT_RLIST_F1(b0_reflist[1].index) |
	      VDPU_REG_BINIT_RLIST_F0(b0_reflist[0].index);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(100));

	reg = VDPU_REG_BINIT_RLIST_F11(b0_reflist[11].index) |
	      VDPU_REG_BINIT_RLIST_F10(b0_reflist[10].index) |
	      VDPU_REG_BINIT_RLIST_F9(b0_reflist[9].index) |
	      VDPU_REG_BINIT_RLIST_F8(b0_reflist[8].index) |
	      VDPU_REG_BINIT_RLIST_F7(b0_reflist[7].index) |
	      VDPU_REG_BINIT_RLIST_F6(b0_reflist[6].index);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(101));

	reg = VDPU_REG_BINIT_RLIST_F15(b0_reflist[15].index) |
	      VDPU_REG_BINIT_RLIST_F14(b0_reflist[14].index) |
	      VDPU_REG_BINIT_RLIST_F13(b0_reflist[13].index) |
	      VDPU_REG_BINIT_RLIST_F12(b0_reflist[12].index);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(102));

	reg = VDPU_REG_BINIT_RLIST_B5(b1_reflist[5].index) |
	      VDPU_REG_BINIT_RLIST_B4(b1_reflist[4].index) |
	      VDPU_REG_BINIT_RLIST_B3(b1_reflist[3].index) |
	      VDPU_REG_BINIT_RLIST_B2(b1_reflist[2].index) |
	      VDPU_REG_BINIT_RLIST_B1(b1_reflist[1].index) |
	      VDPU_REG_BINIT_RLIST_B0(b1_reflist[0].index);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(103));

	reg = VDPU_REG_BINIT_RLIST_B11(b1_reflist[11].index) |
	      VDPU_REG_BINIT_RLIST_B10(b1_reflist[10].index) |
	      VDPU_REG_BINIT_RLIST_B9(b1_reflist[9].index) |
	      VDPU_REG_BINIT_RLIST_B8(b1_reflist[8].index) |
	      VDPU_REG_BINIT_RLIST_B7(b1_reflist[7].index) |
	      VDPU_REG_BINIT_RLIST_B6(b1_reflist[6].index);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(104));

	reg = VDPU_REG_BINIT_RLIST_B15(b1_reflist[15].index) |
	      VDPU_REG_BINIT_RLIST_B14(b1_reflist[14].index) |
	      VDPU_REG_BINIT_RLIST_B13(b1_reflist[13].index) |
	      VDPU_REG_BINIT_RLIST_B12(b1_reflist[12].index);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(105));

	reg = VDPU_REG_PINIT_RLIST_F3(p_reflist[3].index) |
	      VDPU_REG_PINIT_RLIST_F2(p_reflist[2].index) |
	      VDPU_REG_PINIT_RLIST_F1(p_reflist[1].index) |
	      VDPU_REG_PINIT_RLIST_F0(p_reflist[0].index);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(106));

	reg = VDPU_REG_REFER_LTERM_E(ctx->h264_dec.dpb_longterm);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(107));

	reg = VDPU_REG_REFER_VALID_E(ctx->h264_dec.dpb_valid);
	vdpu_write_relaxed(vpu, reg, VDPU_SWREG(108));

	/* Set up addresses of DPB buffers. */
	for (i = 0; i < HANTRO_H264_DPB_SIZE; i++) {
		dma_addr_t dma_addr = hantro_h264_get_ref_buf(ctx, i);

		vdpu_write_relaxed(vpu, dma_addr, VDPU_REG_REFER_BASE(i));
	}
}

static void set_buffers(struct hantro_ctx *ctx, struct vb2_v4l2_buffer *src_buf)
{
	const struct hantro_h264_dec_ctrls *ctrls = &ctx->h264_dec.ctrls;
	struct vb2_v4l2_buffer *dst_buf;
	struct hantro_dev *vpu = ctx->dev;
	dma_addr_t src_dma, dst_dma;
	size_t offset = 0;

	/* Source (stream) buffer. */
	src_dma = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	vdpu_write_relaxed(vpu, src_dma, VDPU_REG_RLC_VLC_BASE);

	/* Destination (decoded frame) buffer. */
	dst_buf = hantro_get_dst_buf(ctx);
	dst_dma = hantro_get_dec_buf_addr(ctx, &dst_buf->vb2_buf);
	/* Adjust dma addr to start at second line for bottom field */
	if (ctrls->decode->flags & V4L2_H264_DECODE_PARAM_FLAG_BOTTOM_FIELD)
		offset = ALIGN(ctx->src_fmt.width, MB_DIM);
	vdpu_write_relaxed(vpu, dst_dma + offset, VDPU_REG_DEC_OUT_BASE);

	/* Higher profiles require DMV buffer appended to reference frames. */
	if (ctrls->sps->profile_idc > 66 && ctrls->decode->nal_ref_idc) {
		unsigned int bytes_per_mb = 384;

		/* DMV buffer for monochrome start directly after Y-plane */
		if (ctrls->sps->profile_idc >= 100 &&
		    ctrls->sps->chroma_format_idc == 0)
			bytes_per_mb = 256;
		offset = bytes_per_mb * MB_WIDTH(ctx->src_fmt.width) *
			 MB_HEIGHT(ctx->src_fmt.height);

		/*
		 * DMV buffer is split in two for field encoded frames,
		 * adjust offset for bottom field
		 */
		if (ctrls->decode->flags & V4L2_H264_DECODE_PARAM_FLAG_BOTTOM_FIELD)
			offset += 32 * MB_WIDTH(ctx->src_fmt.width) *
				  MB_HEIGHT(ctx->src_fmt.height);
		vdpu_write_relaxed(vpu, dst_dma + offset, VDPU_REG_DIR_MV_BASE);
	}

	/* Auxiliary buffer prepared in hantro_g1_h264_dec_prepare_table(). */
	vdpu_write_relaxed(vpu, ctx->h264_dec.priv.dma, VDPU_REG_QTABLE_BASE);
}

int rockchip_vpu2_h264_dec_run(struct hantro_ctx *ctx)
{
	struct hantro_dev *vpu = ctx->dev;
	struct vb2_v4l2_buffer *src_buf;
	u32 reg;
	int ret;

	/* Prepare the H264 decoder context. */
	ret = hantro_h264_dec_prepare_run(ctx);
	if (ret)
		return ret;

	src_buf = hantro_get_src_buf(ctx);
	set_params(ctx, src_buf);
	set_ref(ctx);
	set_buffers(ctx, src_buf);

	hantro_end_prepare_run(ctx);

	/* Start decoding! */
	reg = vdpu_read(vpu, VDPU_SWREG(57)) | VDPU_REG_DEC_E(1);
	vdpu_write(vpu, reg, VDPU_SWREG(57));

	return 0;
}
