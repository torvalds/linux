/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, Collabora
 *
 * Author: Benjamin Gaignard <benjamin.gaignard@collabora.com>
 */

#ifndef _ROCKCHIP_VPU981_REGS_H_
#define _ROCKCHIP_VPU981_REGS_H_

#include "hantro.h"

#define AV1_SWREG(nr)	((nr) * 4)

#define AV1_DEC_REG(b, s, m) \
	((const struct hantro_reg) { \
		.base = AV1_SWREG(b), \
		.shift = s, \
		.mask = m, \
	})

#define AV1_REG_INTERRUPT		AV1_SWREG(1)
#define AV1_REG_INTERRUPT_DEC_RDY_INT	BIT(12)

#define AV1_REG_CONFIG			AV1_SWREG(2)
#define AV1_REG_CONFIG_DEC_CLK_GATE_E	BIT(10)

#define av1_dec_e			AV1_DEC_REG(1, 0, 0x1)
#define av1_dec_abort_e			AV1_DEC_REG(1, 5, 0x1)
#define av1_dec_tile_int_e		AV1_DEC_REG(1, 7, 0x1)

#define av1_dec_clk_gate_e		AV1_DEC_REG(2, 10, 0x1)

#define av1_dec_out_ec_bypass		AV1_DEC_REG(3, 8,  0x1)
#define av1_write_mvs_e			AV1_DEC_REG(3, 12, 0x1)
#define av1_filtering_dis		AV1_DEC_REG(3, 14, 0x1)
#define av1_dec_out_dis			AV1_DEC_REG(3, 15, 0x1)
#define av1_dec_out_ec_byte_word	AV1_DEC_REG(3, 16, 0x1)
#define av1_skip_mode			AV1_DEC_REG(3, 26, 0x1)
#define av1_dec_mode			AV1_DEC_REG(3, 27, 0x1f)

#define av1_ref_frames			AV1_DEC_REG(4, 0, 0xf)
#define av1_pic_height_in_cbs		AV1_DEC_REG(4, 6, 0x1fff)
#define av1_pic_width_in_cbs		AV1_DEC_REG(4, 19, 0x1fff)

#define av1_ref_scaling_enable		AV1_DEC_REG(5, 0, 0x1)
#define av1_filt_level_base_gt32	AV1_DEC_REG(5, 1, 0x1)
#define av1_error_resilient		AV1_DEC_REG(5, 2, 0x1)
#define av1_force_interger_mv		AV1_DEC_REG(5, 3, 0x1)
#define av1_allow_intrabc		AV1_DEC_REG(5, 4, 0x1)
#define av1_allow_screen_content_tools	AV1_DEC_REG(5, 5, 0x1)
#define av1_reduced_tx_set_used		AV1_DEC_REG(5, 6, 0x1)
#define av1_enable_dual_filter		AV1_DEC_REG(5, 7, 0x1)
#define av1_enable_jnt_comp		AV1_DEC_REG(5, 8, 0x1)
#define av1_allow_filter_intra		AV1_DEC_REG(5, 9, 0x1)
#define av1_enable_intra_edge_filter	AV1_DEC_REG(5, 10, 0x1)
#define av1_tempor_mvp_e		AV1_DEC_REG(5, 11, 0x1)
#define av1_allow_interintra		AV1_DEC_REG(5, 12, 0x1)
#define av1_allow_masked_compound	AV1_DEC_REG(5, 13, 0x1)
#define av1_enable_cdef			AV1_DEC_REG(5, 14, 0x1)
#define av1_switchable_motion_mode	AV1_DEC_REG(5, 15, 0x1)
#define av1_show_frame			AV1_DEC_REG(5, 16, 0x1)
#define av1_superres_is_scaled		AV1_DEC_REG(5, 17, 0x1)
#define av1_allow_warp			AV1_DEC_REG(5, 18, 0x1)
#define av1_disable_cdf_update		AV1_DEC_REG(5, 19, 0x1)
#define av1_preskip_segid		AV1_DEC_REG(5, 20, 0x1)
#define av1_delta_lf_present		AV1_DEC_REG(5, 21, 0x1)
#define av1_delta_lf_multi		AV1_DEC_REG(5, 22, 0x1)
#define av1_delta_lf_res_log		AV1_DEC_REG(5, 23, 0x3)
#define av1_strm_start_bit		AV1_DEC_REG(5, 25, 0x7f)

#define	av1_stream_len			AV1_DEC_REG(6, 0, 0xffffffff)

#define av1_delta_q_present		AV1_DEC_REG(7, 0, 0x1)
#define av1_delta_q_res_log		AV1_DEC_REG(7, 1, 0x3)
#define av1_cdef_damping		AV1_DEC_REG(7, 3, 0x3)
#define av1_cdef_bits			AV1_DEC_REG(7, 5, 0x3)
#define av1_apply_grain			AV1_DEC_REG(7, 7, 0x1)
#define av1_num_y_points_b		AV1_DEC_REG(7, 8, 0x1)
#define av1_num_cb_points_b		AV1_DEC_REG(7, 9, 0x1)
#define av1_num_cr_points_b		AV1_DEC_REG(7, 10, 0x1)
#define av1_overlap_flag		AV1_DEC_REG(7, 11, 0x1)
#define av1_clip_to_restricted_range	AV1_DEC_REG(7, 12, 0x1)
#define av1_chroma_scaling_from_luma	AV1_DEC_REG(7, 13, 0x1)
#define av1_random_seed			AV1_DEC_REG(7, 14, 0xffff)
#define av1_blackwhite_e		AV1_DEC_REG(7, 30, 0x1)

#define av1_scaling_shift		AV1_DEC_REG(8, 0, 0xf)
#define av1_bit_depth_c_minus8		AV1_DEC_REG(8, 4, 0x3)
#define av1_bit_depth_y_minus8		AV1_DEC_REG(8, 6, 0x3)
#define av1_quant_base_qindex		AV1_DEC_REG(8, 8, 0xff)
#define av1_idr_pic_e			AV1_DEC_REG(8, 16, 0x1)
#define av1_superres_pic_width		AV1_DEC_REG(8, 17, 0x7fff)

#define av1_ref4_sign_bias		AV1_DEC_REG(9, 2, 0x1)
#define av1_ref5_sign_bias		AV1_DEC_REG(9, 3, 0x1)
#define av1_ref6_sign_bias		AV1_DEC_REG(9, 4, 0x1)
#define av1_mf1_type			AV1_DEC_REG(9, 5, 0x7)
#define av1_mf2_type			AV1_DEC_REG(9, 8, 0x7)
#define av1_mf3_type			AV1_DEC_REG(9, 11, 0x7)
#define av1_scale_denom_minus9		AV1_DEC_REG(9, 14, 0x7)
#define av1_last_active_seg		AV1_DEC_REG(9, 17, 0x7)
#define av1_context_update_tile_id	AV1_DEC_REG(9, 20, 0xfff)

#define av1_tile_transpose		AV1_DEC_REG(10, 0, 0x1)
#define av1_tile_enable			AV1_DEC_REG(10, 1, 0x1)
#define av1_multicore_full_width	AV1_DEC_REG(10,	2, 0xff)
#define av1_num_tile_rows_8k		AV1_DEC_REG(10, 10, 0x7f)
#define av1_num_tile_cols_8k		AV1_DEC_REG(10, 17, 0x7f)
#define av1_multicore_tile_start_x	AV1_DEC_REG(10, 24, 0xff)

#define av1_use_temporal3_mvs		AV1_DEC_REG(11, 0, 0x1)
#define av1_use_temporal2_mvs		AV1_DEC_REG(11, 1, 0x1)
#define av1_use_temporal1_mvs		AV1_DEC_REG(11, 2, 0x1)
#define av1_use_temporal0_mvs		AV1_DEC_REG(11, 3, 0x1)
#define av1_comp_pred_mode		AV1_DEC_REG(11, 4, 0x3)
#define av1_high_prec_mv_e		AV1_DEC_REG(11, 7, 0x1)
#define av1_mcomp_filt_type		AV1_DEC_REG(11, 8, 0x7)
#define av1_multicore_expect_context_update	AV1_DEC_REG(11, 11, 0x1)
#define av1_multicore_sbx_offset	AV1_DEC_REG(11, 12, 0x7f)
#define av1_multicore_tile_col		AV1_DEC_REG(11, 19, 0x7f)
#define av1_transform_mode		AV1_DEC_REG(11, 27, 0x7)
#define av1_dec_tile_size_mag		AV1_DEC_REG(11, 30, 0x3)

#define av1_seg_quant_sign		AV1_DEC_REG(12, 2, 0xff)
#define av1_max_cb_size			AV1_DEC_REG(12, 10, 0x7)
#define av1_min_cb_size			AV1_DEC_REG(12, 13, 0x7)
#define av1_comp_pred_fixed_ref		AV1_DEC_REG(12, 16, 0x7)
#define av1_multicore_tile_width	AV1_DEC_REG(12, 19, 0x7f)
#define av1_pic_height_pad		AV1_DEC_REG(12, 26, 0x7)
#define av1_pic_width_pad		AV1_DEC_REG(12, 29, 0x7)

#define av1_segment_e			AV1_DEC_REG(13, 0, 0x1)
#define av1_segment_upd_e		AV1_DEC_REG(13, 1, 0x1)
#define av1_segment_temp_upd_e		AV1_DEC_REG(13, 2, 0x1)
#define av1_comp_pred_var_ref0_av1	AV1_DEC_REG(13, 3, 0x7)
#define av1_comp_pred_var_ref1_av1	AV1_DEC_REG(13, 6, 0x7)
#define av1_lossless_e			AV1_DEC_REG(13, 9, 0x1)
#define av1_qp_delta_ch_ac_av1		AV1_DEC_REG(13, 11, 0x7f)
#define av1_qp_delta_ch_dc_av1		AV1_DEC_REG(13, 18, 0x7f)
#define av1_qp_delta_y_dc_av1		AV1_DEC_REG(13, 25, 0x7f)

#define av1_quant_seg0			AV1_DEC_REG(14, 0, 0xff)
#define av1_filt_level_seg0		AV1_DEC_REG(14, 8, 0x3f)
#define av1_skip_seg0			AV1_DEC_REG(14, 14, 0x1)
#define av1_refpic_seg0			AV1_DEC_REG(14, 15, 0xf)
#define av1_filt_level_delta0_seg0	AV1_DEC_REG(14, 19, 0x7f)
#define av1_filt_level0			AV1_DEC_REG(14, 26, 0x3f)

#define av1_quant_seg1			AV1_DEC_REG(15, 0, 0xff)
#define av1_filt_level_seg1		AV1_DEC_REG(15, 8, 0x3f)
#define av1_skip_seg1			AV1_DEC_REG(15, 14, 0x1)
#define av1_refpic_seg1			AV1_DEC_REG(15, 15, 0xf)
#define av1_filt_level_delta0_seg1	AV1_DEC_REG(15, 19, 0x7f)
#define av1_filt_level1			AV1_DEC_REG(15, 26, 0x3f)

#define av1_quant_seg2			AV1_DEC_REG(16, 0, 0xff)
#define av1_filt_level_seg2		AV1_DEC_REG(16, 8, 0x3f)
#define av1_skip_seg2			AV1_DEC_REG(16, 14, 0x1)
#define av1_refpic_seg2			AV1_DEC_REG(16, 15, 0xf)
#define av1_filt_level_delta0_seg2	AV1_DEC_REG(16, 19, 0x7f)
#define av1_filt_level2			AV1_DEC_REG(16, 26, 0x3f)

#define av1_quant_seg3			AV1_DEC_REG(17, 0, 0xff)
#define av1_filt_level_seg3		AV1_DEC_REG(17, 8, 0x3f)
#define av1_skip_seg3			AV1_DEC_REG(17, 14, 0x1)
#define av1_refpic_seg3			AV1_DEC_REG(17, 15, 0xf)
#define av1_filt_level_delta0_seg3	AV1_DEC_REG(17, 19, 0x7f)
#define av1_filt_level3			AV1_DEC_REG(17, 26, 0x3f)

#define av1_quant_seg4			AV1_DEC_REG(18, 0, 0xff)
#define av1_filt_level_seg4		AV1_DEC_REG(18, 8, 0x3f)
#define av1_skip_seg4			AV1_DEC_REG(18, 14, 0x1)
#define av1_refpic_seg4			AV1_DEC_REG(18, 15, 0xf)
#define av1_filt_level_delta0_seg4	AV1_DEC_REG(18, 19, 0x7f)
#define av1_lr_type			AV1_DEC_REG(18, 26, 0x3f)

#define av1_quant_seg5			AV1_DEC_REG(19, 0, 0xff)
#define av1_filt_level_seg5		AV1_DEC_REG(19, 8, 0x3f)
#define av1_skip_seg5			AV1_DEC_REG(19, 14, 0x1)
#define av1_refpic_seg5			AV1_DEC_REG(19, 15, 0xf)
#define av1_filt_level_delta0_seg5	AV1_DEC_REG(19, 19, 0x7f)
#define av1_lr_unit_size		AV1_DEC_REG(19, 26, 0x3f)

#define av1_filt_level_delta1_seg0	AV1_DEC_REG(20, 0, 0x7f)
#define av1_filt_level_delta2_seg0	AV1_DEC_REG(20, 7, 0x7f)
#define av1_filt_level_delta3_seg0	AV1_DEC_REG(20, 14, 0x7f)
#define av1_global_mv_seg0		AV1_DEC_REG(20, 21, 0x1)
#define av1_mf1_last_offset		AV1_DEC_REG(20, 22, 0x1ff)

#define av1_filt_level_delta1_seg1	AV1_DEC_REG(21, 0, 0x7f)
#define av1_filt_level_delta2_seg1	AV1_DEC_REG(21, 7, 0x7f)
#define av1_filt_level_delta3_seg1	AV1_DEC_REG(21, 14, 0x7f)
#define av1_global_mv_seg1		AV1_DEC_REG(21, 21, 0x1)
#define av1_mf1_last2_offset		AV1_DEC_REG(21, 22, 0x1ff)

#define av1_filt_level_delta1_seg2	AV1_DEC_REG(22, 0, 0x7f)
#define av1_filt_level_delta2_seg2	AV1_DEC_REG(22, 7, 0x7f)
#define av1_filt_level_delta3_seg2	AV1_DEC_REG(22, 14, 0x7f)
#define av1_global_mv_seg2		AV1_DEC_REG(22, 21, 0x1)
#define av1_mf1_last3_offset		AV1_DEC_REG(22, 22, 0x1ff)

#define av1_filt_level_delta1_seg3	AV1_DEC_REG(23, 0, 0x7f)
#define av1_filt_level_delta2_seg3	AV1_DEC_REG(23, 7, 0x7f)
#define av1_filt_level_delta3_seg3	AV1_DEC_REG(23, 14, 0x7f)
#define av1_global_mv_seg3		AV1_DEC_REG(23, 21, 0x1)
#define av1_mf1_golden_offset		AV1_DEC_REG(23, 22, 0x1ff)

#define av1_filt_level_delta1_seg4	AV1_DEC_REG(24, 0, 0x7f)
#define av1_filt_level_delta2_seg4	AV1_DEC_REG(24, 7, 0x7f)
#define av1_filt_level_delta3_seg4	AV1_DEC_REG(24, 14, 0x7f)
#define av1_global_mv_seg4		AV1_DEC_REG(24, 21, 0x1)
#define av1_mf1_bwdref_offset		AV1_DEC_REG(24, 22, 0x1ff)

#define av1_filt_level_delta1_seg5	AV1_DEC_REG(25, 0, 0x7f)
#define av1_filt_level_delta2_seg5	AV1_DEC_REG(25, 7, 0x7f)
#define av1_filt_level_delta3_seg5	AV1_DEC_REG(25, 14, 0x7f)
#define av1_global_mv_seg5		AV1_DEC_REG(25, 21, 0x1)
#define av1_mf1_altref2_offset		AV1_DEC_REG(25, 22, 0x1ff)

#define av1_filt_level_delta1_seg6	AV1_DEC_REG(26, 0, 0x7f)
#define av1_filt_level_delta2_seg6	AV1_DEC_REG(26, 7, 0x7f)
#define av1_filt_level_delta3_seg6	AV1_DEC_REG(26, 14, 0x7f)
#define av1_global_mv_seg6		AV1_DEC_REG(26, 21, 0x1)
#define av1_mf1_altref_offset		AV1_DEC_REG(26, 22, 0x1ff)

#define av1_filt_level_delta1_seg7	AV1_DEC_REG(27, 0, 0x7f)
#define av1_filt_level_delta2_seg7	AV1_DEC_REG(27, 7, 0x7f)
#define av1_filt_level_delta3_seg7	AV1_DEC_REG(27, 14, 0x7f)
#define av1_global_mv_seg7		AV1_DEC_REG(27, 21, 0x1)
#define av1_mf2_last_offset		AV1_DEC_REG(27, 22, 0x1ff)

#define av1_cb_offset			AV1_DEC_REG(28, 0, 0x1ff)
#define av1_cb_luma_mult		AV1_DEC_REG(28, 9, 0xff)
#define av1_cb_mult			AV1_DEC_REG(28, 17, 0xff)
#define	av1_quant_delta_v_dc		AV1_DEC_REG(28, 25, 0x7f)

#define av1_cr_offset			AV1_DEC_REG(29, 0, 0x1ff)
#define av1_cr_luma_mult		AV1_DEC_REG(29, 9, 0xff)
#define av1_cr_mult			AV1_DEC_REG(29, 17, 0xff)
#define	av1_quant_delta_v_ac		AV1_DEC_REG(29, 25, 0x7f)

#define av1_filt_ref_adj_5		AV1_DEC_REG(30, 0, 0x7f)
#define av1_filt_ref_adj_4		AV1_DEC_REG(30, 7, 0x7f)
#define av1_filt_mb_adj_1		AV1_DEC_REG(30, 14, 0x7f)
#define av1_filt_mb_adj_0		AV1_DEC_REG(30, 21, 0x7f)
#define av1_filt_sharpness		AV1_DEC_REG(30, 28, 0x7)

#define av1_quant_seg6			AV1_DEC_REG(31, 0, 0xff)
#define av1_filt_level_seg6		AV1_DEC_REG(31, 8, 0x3f)
#define av1_skip_seg6			AV1_DEC_REG(31, 14, 0x1)
#define av1_refpic_seg6			AV1_DEC_REG(31, 15, 0xf)
#define av1_filt_level_delta0_seg6	AV1_DEC_REG(31, 19, 0x7f)
#define av1_skip_ref0			AV1_DEC_REG(31, 26, 0xf)

#define av1_quant_seg7			AV1_DEC_REG(32, 0, 0xff)
#define av1_filt_level_seg7		AV1_DEC_REG(32, 8, 0x3f)
#define av1_skip_seg7			AV1_DEC_REG(32, 14, 0x1)
#define av1_refpic_seg7			AV1_DEC_REG(32, 15, 0xf)
#define av1_filt_level_delta0_seg7	AV1_DEC_REG(32, 19, 0x7f)
#define av1_skip_ref1			AV1_DEC_REG(32, 26, 0xf)

#define av1_ref0_height			AV1_DEC_REG(33, 0, 0xffff)
#define av1_ref0_width			AV1_DEC_REG(33, 16, 0xffff)

#define av1_ref1_height			AV1_DEC_REG(34, 0, 0xffff)
#define av1_ref1_width			AV1_DEC_REG(34, 16, 0xffff)

#define av1_ref2_height			AV1_DEC_REG(35, 0, 0xffff)
#define av1_ref2_width			AV1_DEC_REG(35, 16, 0xffff)

#define av1_ref0_ver_scale		AV1_DEC_REG(36, 0, 0xffff)
#define av1_ref0_hor_scale		AV1_DEC_REG(36, 16, 0xffff)

#define av1_ref1_ver_scale		AV1_DEC_REG(37, 0, 0xffff)
#define av1_ref1_hor_scale		AV1_DEC_REG(37, 16, 0xffff)

#define av1_ref2_ver_scale		AV1_DEC_REG(38, 0, 0xffff)
#define av1_ref2_hor_scale		AV1_DEC_REG(38, 16, 0xffff)

#define av1_ref3_ver_scale		AV1_DEC_REG(39, 0, 0xffff)
#define av1_ref3_hor_scale		AV1_DEC_REG(39, 16, 0xffff)

#define av1_ref4_ver_scale		AV1_DEC_REG(40, 0, 0xffff)
#define av1_ref4_hor_scale		AV1_DEC_REG(40, 16, 0xffff)

#define av1_ref5_ver_scale		AV1_DEC_REG(41, 0, 0xffff)
#define av1_ref5_hor_scale		AV1_DEC_REG(41, 16, 0xffff)

#define av1_ref6_ver_scale		AV1_DEC_REG(42, 0, 0xffff)
#define av1_ref6_hor_scale		AV1_DEC_REG(42, 16, 0xffff)

#define av1_ref3_height			AV1_DEC_REG(43, 0, 0xffff)
#define av1_ref3_width			AV1_DEC_REG(43, 16, 0xffff)

#define av1_ref4_height			AV1_DEC_REG(44, 0, 0xffff)
#define av1_ref4_width			AV1_DEC_REG(44, 16, 0xffff)

#define av1_ref5_height			AV1_DEC_REG(45, 0, 0xffff)
#define av1_ref5_width			AV1_DEC_REG(45, 16, 0xffff)

#define av1_ref6_height			AV1_DEC_REG(46, 0, 0xffff)
#define av1_ref6_width			AV1_DEC_REG(46, 16, 0xffff)

#define av1_mf2_last2_offset		AV1_DEC_REG(47, 0, 0x1ff)
#define av1_mf2_last3_offset		AV1_DEC_REG(47, 9, 0x1ff)
#define av1_mf2_golden_offset		AV1_DEC_REG(47, 18, 0x1ff)
#define av1_qmlevel_y			AV1_DEC_REG(47, 27, 0xf)

#define av1_mf2_bwdref_offset		AV1_DEC_REG(48, 0, 0x1ff)
#define av1_mf2_altref2_offset		AV1_DEC_REG(48, 9, 0x1ff)
#define av1_mf2_altref_offset		AV1_DEC_REG(48, 18, 0x1ff)
#define av1_qmlevel_u			AV1_DEC_REG(48, 27, 0xf)

#define av1_filt_ref_adj_6		AV1_DEC_REG(49, 0, 0x7f)
#define av1_filt_ref_adj_7		AV1_DEC_REG(49, 7, 0x7f)
#define av1_qmlevel_v			AV1_DEC_REG(49, 14, 0xf)

#define av1_superres_chroma_step	AV1_DEC_REG(51, 0, 0x3fff)
#define av1_superres_luma_step		AV1_DEC_REG(51, 14, 0x3fff)

#define av1_superres_init_chroma_subpel_x	AV1_DEC_REG(52, 0, 0x3fff)
#define av1_superres_init_luma_subpel_x		AV1_DEC_REG(52, 14, 0x3fff)

#define av1_cdef_chroma_secondary_strength	AV1_DEC_REG(53, 0, 0xffff)
#define av1_cdef_luma_secondary_strength	AV1_DEC_REG(53, 16, 0xffff)

#define av1_apf_threshold		AV1_DEC_REG(55, 0, 0xffff)
#define av1_apf_single_pu_mode		AV1_DEC_REG(55, 30, 0x1)
#define av1_apf_disable			AV1_DEC_REG(55, 31, 0x1)

#define av1_dec_max_burst		AV1_DEC_REG(58, 0, 0xff)
#define av1_dec_buswidth		AV1_DEC_REG(58, 8, 0x7)
#define av1_dec_multicore_mode		AV1_DEC_REG(58, 11, 0x3)
#define av1_dec_axi_wd_id_e		AV1_DEC_REG(58,	13, 0x1)
#define av1_dec_axi_rd_id_e		AV1_DEC_REG(58, 14, 0x1)
#define av1_dec_mc_polltime		AV1_DEC_REG(58, 17, 0x3ff)
#define av1_dec_mc_pollmode		AV1_DEC_REG(58,	27, 0x3)

#define av1_filt_ref_adj_3		AV1_DEC_REG(59, 0, 0x7f)
#define av1_filt_ref_adj_2		AV1_DEC_REG(59, 7, 0x7f)
#define av1_filt_ref_adj_1		AV1_DEC_REG(59, 14, 0x7f)
#define av1_filt_ref_adj_0		AV1_DEC_REG(59, 21, 0x7f)
#define av1_ref0_sign_bias		AV1_DEC_REG(59, 28, 0x1)
#define av1_ref1_sign_bias		AV1_DEC_REG(59, 29, 0x1)
#define av1_ref2_sign_bias		AV1_DEC_REG(59, 30, 0x1)
#define av1_ref3_sign_bias		AV1_DEC_REG(59, 31, 0x1)

#define av1_cur_last_roffset		AV1_DEC_REG(184, 0, 0x1ff)
#define av1_cur_last_offset		AV1_DEC_REG(184, 9, 0x1ff)
#define av1_mf3_last_offset		AV1_DEC_REG(184, 18, 0x1ff)
#define av1_ref0_gm_mode		AV1_DEC_REG(184, 27, 0x3)

#define av1_cur_last2_roffset		AV1_DEC_REG(185, 0, 0x1ff)
#define av1_cur_last2_offset		AV1_DEC_REG(185, 9, 0x1ff)
#define av1_mf3_last2_offset		AV1_DEC_REG(185, 18, 0x1ff)
#define av1_ref1_gm_mode		AV1_DEC_REG(185, 27, 0x3)

#define av1_cur_last3_roffset		AV1_DEC_REG(186, 0, 0x1ff)
#define av1_cur_last3_offset		AV1_DEC_REG(186, 9, 0x1ff)
#define av1_mf3_last3_offset		AV1_DEC_REG(186, 18, 0x1ff)
#define av1_ref2_gm_mode		AV1_DEC_REG(186, 27, 0x3)

#define av1_cur_golden_roffset		AV1_DEC_REG(187, 0, 0x1ff)
#define av1_cur_golden_offset		AV1_DEC_REG(187, 9, 0x1ff)
#define av1_mf3_golden_offset		AV1_DEC_REG(187, 18, 0x1ff)
#define av1_ref3_gm_mode		AV1_DEC_REG(187, 27, 0x3)

#define av1_cur_bwdref_roffset		AV1_DEC_REG(188, 0, 0x1ff)
#define av1_cur_bwdref_offset		AV1_DEC_REG(188, 9, 0x1ff)
#define av1_mf3_bwdref_offset		AV1_DEC_REG(188, 18, 0x1ff)
#define av1_ref4_gm_mode		AV1_DEC_REG(188, 27, 0x3)

#define av1_cur_altref2_roffset		AV1_DEC_REG(257, 0, 0x1ff)
#define av1_cur_altref2_offset		AV1_DEC_REG(257, 9, 0x1ff)
#define av1_mf3_altref2_offset		AV1_DEC_REG(257, 18, 0x1ff)
#define av1_ref5_gm_mode		AV1_DEC_REG(257, 27, 0x3)

#define av1_strm_buffer_len		AV1_DEC_REG(258, 0, 0xffffffff)

#define av1_strm_start_offset		AV1_DEC_REG(259, 0, 0xffffffff)

#define av1_ppd_blend_exist		AV1_DEC_REG(260, 21, 0x1)
#define av1_ppd_dith_exist		AV1_DEC_REG(260, 23, 0x1)
#define av1_ablend_crop_e		AV1_DEC_REG(260, 24, 0x1)
#define av1_pp_format_p010_e		AV1_DEC_REG(260, 25, 0x1)
#define av1_pp_format_customer1_e	AV1_DEC_REG(260, 26, 0x1)
#define av1_pp_crop_exist		AV1_DEC_REG(260, 27, 0x1)
#define av1_pp_up_level			AV1_DEC_REG(260, 28, 0x1)
#define av1_pp_down_level		AV1_DEC_REG(260, 29, 0x3)
#define av1_pp_exist			AV1_DEC_REG(260, 31, 0x1)

#define av1_cur_altref_roffset		AV1_DEC_REG(262, 0, 0x1ff)
#define av1_cur_altref_offset		AV1_DEC_REG(262, 9, 0x1ff)
#define av1_mf3_altref_offset		AV1_DEC_REG(262, 18, 0x1ff)
#define av1_ref6_gm_mode		AV1_DEC_REG(262, 27, 0x3)

#define av1_cdef_luma_primary_strength	AV1_DEC_REG(263, 0, 0xffffffff)

#define av1_cdef_chroma_primary_strength AV1_DEC_REG(264, 0, 0xffffffff)

#define av1_axi_arqos			AV1_DEC_REG(265, 0, 0xf)
#define av1_axi_awqos			AV1_DEC_REG(265, 4, 0xf)
#define av1_axi_wr_ostd_threshold	AV1_DEC_REG(265, 8, 0x3ff)
#define av1_axi_rd_ostd_threshold	AV1_DEC_REG(265, 18, 0x3ff)
#define av1_axi_wr_4k_dis		AV1_DEC_REG(265, 31, 0x1)

#define av1_128bit_mode			AV1_DEC_REG(266, 5, 0x1)
#define av1_wr_shaper_bypass		AV1_DEC_REG(266, 10, 0x1)
#define av1_error_conceal_e		AV1_DEC_REG(266, 30, 0x1)

#define av1_superres_chroma_step_invra	AV1_DEC_REG(298, 0, 0xffff)
#define av1_superres_luma_step_invra	AV1_DEC_REG(298, 16, 0xffff)

#define av1_dec_alignment		AV1_DEC_REG(314, 0, 0xffff)

#define av1_ext_timeout_cycles		AV1_DEC_REG(318, 0, 0x7fffffff)
#define av1_ext_timeout_override_e	AV1_DEC_REG(318, 31, 0x1)

#define av1_timeout_cycles		AV1_DEC_REG(319, 0, 0x7fffffff)
#define av1_timeout_override_e		AV1_DEC_REG(319, 31, 0x1)

#define av1_pp_out_e			AV1_DEC_REG(320, 0, 0x1)
#define av1_pp_cr_first			AV1_DEC_REG(320, 1, 0x1)
#define av1_pp_out_mode			AV1_DEC_REG(320, 2, 0x1)
#define av1_pp_out_tile_e		AV1_DEC_REG(320, 3, 0x1)
#define av1_pp_status			AV1_DEC_REG(320, 4, 0xf)
#define av1_pp_in_blk_size		AV1_DEC_REG(320, 8, 0x7)
#define av1_pp_out_p010_fmt		AV1_DEC_REG(320, 11, 0x3)
#define av1_pp_out_rgb_fmt		AV1_DEC_REG(320, 13, 0x1f)
#define av1_rgb_range_max		AV1_DEC_REG(320, 18, 0xfff)
#define av1_pp_rgb_planar		AV1_DEC_REG(320, 30, 0x1)

#define av1_scale_hratio		AV1_DEC_REG(322, 0, 0x3ffff)
#define av1_pp_out_format		AV1_DEC_REG(322, 18, 0x1f)
#define av1_ver_scale_mode		AV1_DEC_REG(322, 23, 0x3)
#define av1_hor_scale_mode		AV1_DEC_REG(322, 25, 0x3)
#define av1_pp_in_format		AV1_DEC_REG(322, 27, 0x1f)

#define av1_pp_out_c_stride		AV1_DEC_REG(329, 0, 0xffff)
#define av1_pp_out_y_stride		AV1_DEC_REG(329, 16, 0xffff)

#define av1_pp_in_height		AV1_DEC_REG(331, 0, 0xffff)
#define av1_pp_in_width			AV1_DEC_REG(331, 16, 0xffff)

#define av1_pp_out_height		AV1_DEC_REG(332, 0, 0xffff)
#define av1_pp_out_width		AV1_DEC_REG(332, 16, 0xffff)

#define av1_pp1_dup_ver			AV1_DEC_REG(394, 0, 0xff)
#define av1_pp1_dup_hor			AV1_DEC_REG(394, 8, 0xff)
#define av1_pp0_dup_ver			AV1_DEC_REG(394, 16, 0xff)
#define av1_pp0_dup_hor			AV1_DEC_REG(394, 24, 0xff)

#define AV1_TILE_OUT_LU			(AV1_SWREG(65))
#define AV1_REFERENCE_Y(i)		(AV1_SWREG(67) + ((i) * 0x8))
#define AV1_SEGMENTATION		(AV1_SWREG(81))
#define AV1_GLOBAL_MODEL		(AV1_SWREG(83))
#define AV1_CDEF_COL			(AV1_SWREG(85))
#define AV1_SR_COL			(AV1_SWREG(89))
#define AV1_LR_COL			(AV1_SWREG(91))
#define AV1_FILM_GRAIN			(AV1_SWREG(95))
#define AV1_TILE_OUT_CH			(AV1_SWREG(99))
#define AV1_REFERENCE_CB(i)		(AV1_SWREG(101) + ((i) * 0x8))
#define AV1_TILE_OUT_MV			(AV1_SWREG(133))
#define AV1_REFERENCE_MV(i)		(AV1_SWREG(135) + ((i) * 0x8))
#define AV1_TILE_BASE			(AV1_SWREG(167))
#define AV1_INPUT_STREAM		(AV1_SWREG(169))
#define AV1_PROP_TABLE_OUT		(AV1_SWREG(171))
#define AV1_PROP_TABLE			(AV1_SWREG(173))
#define AV1_MC_SYNC_CURR		(AV1_SWREG(175))
#define AV1_MC_SYNC_LEFT		(AV1_SWREG(177))
#define AV1_DB_DATA_COL			(AV1_SWREG(179))
#define AV1_DB_CTRL_COL			(AV1_SWREG(183))
#define AV1_PP_OUT_LU			(AV1_SWREG(326))
#define AV1_PP_OUT_CH			(AV1_SWREG(328))

#endif /* _ROCKCHIP_VPU981_REGS_H_ */
