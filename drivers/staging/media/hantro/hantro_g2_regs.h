/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, Collabora
 *
 * Author: Benjamin Gaignard <benjamin.gaignard@collabora.com>
 */

#ifndef HANTRO_G2_REGS_H_
#define HANTRO_G2_REGS_H_

#include "hantro.h"

#define G2_SWREG(nr)	((nr) * 4)

#define G2_DEC_REG(b, s, m) \
	((const struct hantro_reg) { \
		.base = G2_SWREG(b), \
		.shift = s, \
		.mask = m, \
	})

#define G2_REG_VERSION			G2_SWREG(0)

#define G2_REG_INTERRUPT		G2_SWREG(1)
#define G2_REG_INTERRUPT_DEC_RDY_INT	BIT(12)
#define G2_REG_INTERRUPT_DEC_ABORT_E	BIT(5)
#define G2_REG_INTERRUPT_DEC_IRQ_DIS	BIT(4)
#define G2_REG_INTERRUPT_DEC_E		BIT(0)

#define HEVC_DEC_MODE			0xc
#define VP9_DEC_MODE			0xd

#define BUS_WIDTH_32			0
#define BUS_WIDTH_64			1
#define BUS_WIDTH_128			2
#define BUS_WIDTH_256			3

#define g2_strm_swap		G2_DEC_REG(2, 28, 0xf)
#define g2_strm_swap_old	G2_DEC_REG(2, 27, 0x1f)
#define g2_pic_swap		G2_DEC_REG(2, 22, 0x1f)
#define g2_dirmv_swap		G2_DEC_REG(2, 20, 0xf)
#define g2_dirmv_swap_old	G2_DEC_REG(2, 17, 0x1f)
#define g2_tab0_swap_old	G2_DEC_REG(2, 12, 0x1f)
#define g2_tab1_swap_old	G2_DEC_REG(2, 7, 0x1f)
#define g2_tab2_swap_old	G2_DEC_REG(2, 2, 0x1f)

#define g2_mode			G2_DEC_REG(3, 27, 0x1f)
#define g2_compress_swap	G2_DEC_REG(3, 20, 0xf)
#define g2_ref_compress_bypass	G2_DEC_REG(3, 17, 0x1)
#define g2_out_rs_e		G2_DEC_REG(3, 16, 0x1)
#define g2_out_dis		G2_DEC_REG(3, 15, 0x1)
#define g2_out_filtering_dis	G2_DEC_REG(3, 14, 0x1)
#define g2_write_mvs_e		G2_DEC_REG(3, 12, 0x1)
#define g2_tab3_swap_old	G2_DEC_REG(3, 7, 0x1f)
#define g2_rscan_swap		G2_DEC_REG(3, 2, 0x1f)

#define g2_pic_width_in_cbs	G2_DEC_REG(4, 19, 0x1fff)
#define g2_pic_height_in_cbs	G2_DEC_REG(4, 6,  0x1fff)
#define g2_num_ref_frames	G2_DEC_REG(4, 0,  0x1f)

#define g2_start_bit		G2_DEC_REG(5, 25, 0x7f)
#define g2_scaling_list_e	G2_DEC_REG(5, 24, 0x1)
#define g2_cb_qp_offset		G2_DEC_REG(5, 19, 0x1f)
#define g2_cr_qp_offset		G2_DEC_REG(5, 14, 0x1f)
#define g2_sign_data_hide	G2_DEC_REG(5, 12, 0x1)
#define g2_tempor_mvp_e		G2_DEC_REG(5, 11, 0x1)
#define g2_max_cu_qpd_depth	G2_DEC_REG(5, 5,  0x3f)
#define g2_cu_qpd_e		G2_DEC_REG(5, 4,  0x1)
#define g2_pix_shift		G2_DEC_REG(5, 0,  0xf)

#define g2_stream_len		G2_DEC_REG(6, 0,  0xffffffff)

#define g2_cabac_init_present	G2_DEC_REG(7, 31, 0x1)
#define g2_weight_pred_e	G2_DEC_REG(7, 28, 0x1)
#define g2_weight_bipr_idc	G2_DEC_REG(7, 26, 0x3)
#define g2_filter_over_slices	G2_DEC_REG(7, 25, 0x1)
#define g2_filter_over_tiles	G2_DEC_REG(7, 24, 0x1)
#define g2_asym_pred_e		G2_DEC_REG(7, 23, 0x1)
#define g2_sao_e		G2_DEC_REG(7, 22, 0x1)
#define g2_pcm_filt_d		G2_DEC_REG(7, 21, 0x1)
#define g2_slice_chqp_present	G2_DEC_REG(7, 20, 0x1)
#define g2_dependent_slice	G2_DEC_REG(7, 19, 0x1)
#define g2_filter_override	G2_DEC_REG(7, 18, 0x1)
#define g2_strong_smooth_e	G2_DEC_REG(7, 17, 0x1)
#define g2_filt_offset_beta	G2_DEC_REG(7, 12, 0x1f)
#define g2_filt_offset_tc	G2_DEC_REG(7, 7,  0x1f)
#define g2_slice_hdr_ext_e	G2_DEC_REG(7, 6,  0x1)
#define g2_slice_hdr_ext_bits	G2_DEC_REG(7, 3,  0x7)

#define g2_const_intra_e	G2_DEC_REG(8, 31, 0x1)
#define g2_filt_ctrl_pres	G2_DEC_REG(8, 30, 0x1)
#define g2_bit_depth_y		G2_DEC_REG(8, 21, 0xf)
#define g2_bit_depth_c		G2_DEC_REG(8, 17, 0xf)
#define g2_idr_pic_e		G2_DEC_REG(8, 16, 0x1)
#define g2_bit_depth_pcm_y	G2_DEC_REG(8, 12, 0xf)
#define g2_bit_depth_pcm_c	G2_DEC_REG(8, 8,  0xf)
#define g2_bit_depth_y_minus8	G2_DEC_REG(8, 6,  0x3)
#define g2_bit_depth_c_minus8	G2_DEC_REG(8, 4,  0x3)
#define g2_rs_out_bit_depth	G2_DEC_REG(8, 4,  0xf)
#define g2_output_8_bits	G2_DEC_REG(8, 3,  0x1)
#define g2_output_format	G2_DEC_REG(8, 0,  0x7)
#define g2_pp_pix_shift		G2_DEC_REG(8, 0,  0xf)

#define g2_refidx1_active	G2_DEC_REG(9, 19, 0x1f)
#define g2_refidx0_active	G2_DEC_REG(9, 14, 0x1f)
#define g2_hdr_skip_length	G2_DEC_REG(9, 0,  0x3fff)

#define g2_start_code_e		G2_DEC_REG(10, 31, 0x1)
#define g2_init_qp_old		G2_DEC_REG(10, 25, 0x3f)
#define g2_init_qp		G2_DEC_REG(10, 24, 0x3f)
#define g2_num_tile_cols_old	G2_DEC_REG(10, 20, 0x1f)
#define g2_num_tile_cols	G2_DEC_REG(10, 19, 0x1f)
#define g2_num_tile_rows_old	G2_DEC_REG(10, 15, 0x1f)
#define g2_num_tile_rows	G2_DEC_REG(10, 14, 0x1f)
#define g2_tile_e		G2_DEC_REG(10, 1,  0x1)
#define g2_entropy_sync_e	G2_DEC_REG(10, 0,  0x1)

#define vp9_transform_mode	G2_DEC_REG(11, 27, 0x7)
#define vp9_filt_sharpness	G2_DEC_REG(11, 21, 0x7)
#define vp9_mcomp_filt_type	G2_DEC_REG(11,  8, 0x7)
#define vp9_high_prec_mv_e	G2_DEC_REG(11,  7, 0x1)
#define vp9_comp_pred_mode	G2_DEC_REG(11,  4, 0x3)
#define vp9_gref_sign_bias	G2_DEC_REG(11,  2, 0x1)
#define vp9_aref_sign_bias	G2_DEC_REG(11,  0, 0x1)

#define g2_refer_lterm_e	G2_DEC_REG(12, 16, 0xffff)
#define g2_min_cb_size		G2_DEC_REG(12, 13, 0x7)
#define g2_max_cb_size		G2_DEC_REG(12, 10, 0x7)
#define g2_min_pcm_size		G2_DEC_REG(12, 7,  0x7)
#define g2_max_pcm_size		G2_DEC_REG(12, 4,  0x7)
#define g2_pcm_e		G2_DEC_REG(12, 3,  0x1)
#define g2_transform_skip	G2_DEC_REG(12, 2,  0x1)
#define g2_transq_bypass	G2_DEC_REG(12, 1,  0x1)
#define g2_list_mod_e		G2_DEC_REG(12, 0,  0x1)

#define hevc_min_trb_size		G2_DEC_REG(13, 13, 0x7)
#define hevc_max_trb_size		G2_DEC_REG(13, 10, 0x7)
#define hevc_max_intra_hierdepth	G2_DEC_REG(13, 7,  0x7)
#define hevc_max_inter_hierdepth	G2_DEC_REG(13, 4,  0x7)
#define hevc_parallel_merge		G2_DEC_REG(13, 0,  0xf)

#define hevc_rlist_f0		G2_DEC_REG(14, 0,  0x1f)
#define hevc_rlist_f1		G2_DEC_REG(14, 10, 0x1f)
#define hevc_rlist_f2		G2_DEC_REG(14, 20, 0x1f)
#define hevc_rlist_b0		G2_DEC_REG(14, 5,  0x1f)
#define hevc_rlist_b1		G2_DEC_REG(14, 15, 0x1f)
#define hevc_rlist_b2		G2_DEC_REG(14, 25, 0x1f)

#define hevc_rlist_f3		G2_DEC_REG(15, 0,  0x1f)
#define hevc_rlist_f4		G2_DEC_REG(15, 10, 0x1f)
#define hevc_rlist_f5		G2_DEC_REG(15, 20, 0x1f)
#define hevc_rlist_b3		G2_DEC_REG(15, 5,  0x1f)
#define hevc_rlist_b4		G2_DEC_REG(15, 15, 0x1f)
#define hevc_rlist_b5		G2_DEC_REG(15, 25, 0x1f)

#define hevc_rlist_f6		G2_DEC_REG(16, 0,  0x1f)
#define hevc_rlist_f7		G2_DEC_REG(16, 10, 0x1f)
#define hevc_rlist_f8		G2_DEC_REG(16, 20, 0x1f)
#define hevc_rlist_b6		G2_DEC_REG(16, 5,  0x1f)
#define hevc_rlist_b7		G2_DEC_REG(16, 15, 0x1f)
#define hevc_rlist_b8		G2_DEC_REG(16, 25, 0x1f)

#define hevc_rlist_f9		G2_DEC_REG(17, 0,  0x1f)
#define hevc_rlist_f10		G2_DEC_REG(17, 10, 0x1f)
#define hevc_rlist_f11		G2_DEC_REG(17, 20, 0x1f)
#define hevc_rlist_b9		G2_DEC_REG(17, 5,  0x1f)
#define hevc_rlist_b10		G2_DEC_REG(17, 15, 0x1f)
#define hevc_rlist_b11		G2_DEC_REG(17, 25, 0x1f)

#define hevc_rlist_f12		G2_DEC_REG(18, 0,  0x1f)
#define hevc_rlist_f13		G2_DEC_REG(18, 10, 0x1f)
#define hevc_rlist_f14		G2_DEC_REG(18, 20, 0x1f)
#define hevc_rlist_b12		G2_DEC_REG(18, 5,  0x1f)
#define hevc_rlist_b13		G2_DEC_REG(18, 15, 0x1f)
#define hevc_rlist_b14		G2_DEC_REG(18, 25, 0x1f)

#define hevc_rlist_f15		G2_DEC_REG(19, 0,  0x1f)
#define hevc_rlist_b15		G2_DEC_REG(19, 5,  0x1f)

#define g2_partial_ctb_x	G2_DEC_REG(20, 31, 0x1)
#define g2_partial_ctb_y	G2_DEC_REG(20, 30, 0x1)
#define g2_pic_width_4x4	G2_DEC_REG(20, 16, 0xfff)
#define g2_pic_height_4x4	G2_DEC_REG(20, 0,  0xfff)

#define vp9_qp_delta_y_dc	G2_DEC_REG(13, 23, 0x3f)
#define vp9_qp_delta_ch_dc	G2_DEC_REG(13, 17, 0x3f)
#define vp9_qp_delta_ch_ac	G2_DEC_REG(13, 11, 0x3f)
#define vp9_last_sign_bias	G2_DEC_REG(13, 10, 0x1)
#define vp9_lossless_e		G2_DEC_REG(13,  9, 0x1)
#define vp9_comp_pred_var_ref1	G2_DEC_REG(13,  7, 0x3)
#define vp9_comp_pred_var_ref0	G2_DEC_REG(13,  5, 0x3)
#define vp9_comp_pred_fixed_ref	G2_DEC_REG(13,  3, 0x3)
#define vp9_segment_temp_upd_e	G2_DEC_REG(13,  2, 0x1)
#define vp9_segment_upd_e	G2_DEC_REG(13,  1, 0x1)
#define vp9_segment_e		G2_DEC_REG(13,  0, 0x1)

#define vp9_filt_level		G2_DEC_REG(14, 18, 0x3f)
#define vp9_refpic_seg0		G2_DEC_REG(14, 15, 0x7)
#define vp9_skip_seg0		G2_DEC_REG(14, 14, 0x1)
#define vp9_filt_level_seg0	G2_DEC_REG(14,  8, 0x3f)
#define vp9_quant_seg0		G2_DEC_REG(14,  0, 0xff)

#define vp9_refpic_seg1		G2_DEC_REG(15, 15, 0x7)
#define vp9_skip_seg1		G2_DEC_REG(15, 14, 0x1)
#define vp9_filt_level_seg1	G2_DEC_REG(15,  8, 0x3f)
#define vp9_quant_seg1		G2_DEC_REG(15,  0, 0xff)

#define vp9_refpic_seg2		G2_DEC_REG(16, 15, 0x7)
#define vp9_skip_seg2		G2_DEC_REG(16, 14, 0x1)
#define vp9_filt_level_seg2	G2_DEC_REG(16,  8, 0x3f)
#define vp9_quant_seg2		G2_DEC_REG(16,  0, 0xff)

#define vp9_refpic_seg3		G2_DEC_REG(17, 15, 0x7)
#define vp9_skip_seg3		G2_DEC_REG(17, 14, 0x1)
#define vp9_filt_level_seg3	G2_DEC_REG(17,  8, 0x3f)
#define vp9_quant_seg3		G2_DEC_REG(17,  0, 0xff)

#define vp9_refpic_seg4		G2_DEC_REG(18, 15, 0x7)
#define vp9_skip_seg4		G2_DEC_REG(18, 14, 0x1)
#define vp9_filt_level_seg4	G2_DEC_REG(18,  8, 0x3f)
#define vp9_quant_seg4		G2_DEC_REG(18,  0, 0xff)

#define vp9_refpic_seg5		G2_DEC_REG(19, 15, 0x7)
#define vp9_skip_seg5		G2_DEC_REG(19, 14, 0x1)
#define vp9_filt_level_seg5	G2_DEC_REG(19,  8, 0x3f)
#define vp9_quant_seg5		G2_DEC_REG(19,  0, 0xff)

#define hevc_cur_poc_00		G2_DEC_REG(46, 24, 0xff)
#define hevc_cur_poc_01		G2_DEC_REG(46, 16, 0xff)
#define hevc_cur_poc_02		G2_DEC_REG(46, 8,  0xff)
#define hevc_cur_poc_03		G2_DEC_REG(46, 0,  0xff)

#define hevc_cur_poc_04		G2_DEC_REG(47, 24, 0xff)
#define hevc_cur_poc_05		G2_DEC_REG(47, 16, 0xff)
#define hevc_cur_poc_06		G2_DEC_REG(47, 8,  0xff)
#define hevc_cur_poc_07		G2_DEC_REG(47, 0,  0xff)

#define hevc_cur_poc_08		G2_DEC_REG(48, 24, 0xff)
#define hevc_cur_poc_09		G2_DEC_REG(48, 16, 0xff)
#define hevc_cur_poc_10		G2_DEC_REG(48, 8,  0xff)
#define hevc_cur_poc_11		G2_DEC_REG(48, 0,  0xff)

#define hevc_cur_poc_12		G2_DEC_REG(49, 24, 0xff)
#define hevc_cur_poc_13		G2_DEC_REG(49, 16, 0xff)
#define hevc_cur_poc_14		G2_DEC_REG(49, 8,  0xff)
#define hevc_cur_poc_15		G2_DEC_REG(49, 0,  0xff)

#define vp9_refpic_seg6		G2_DEC_REG(31, 15, 0x7)
#define vp9_skip_seg6		G2_DEC_REG(31, 14, 0x1)
#define vp9_filt_level_seg6	G2_DEC_REG(31,  8, 0x3f)
#define vp9_quant_seg6		G2_DEC_REG(31,  0, 0xff)

#define vp9_refpic_seg7		G2_DEC_REG(32, 15, 0x7)
#define vp9_skip_seg7		G2_DEC_REG(32, 14, 0x1)
#define vp9_filt_level_seg7	G2_DEC_REG(32,  8, 0x3f)
#define vp9_quant_seg7		G2_DEC_REG(32,  0, 0xff)

#define vp9_lref_width		G2_DEC_REG(33, 16, 0xffff)
#define vp9_lref_height		G2_DEC_REG(33,  0, 0xffff)

#define vp9_gref_width		G2_DEC_REG(34, 16, 0xffff)
#define vp9_gref_height		G2_DEC_REG(34,  0, 0xffff)

#define vp9_aref_width		G2_DEC_REG(35, 16, 0xffff)
#define vp9_aref_height		G2_DEC_REG(35,  0, 0xffff)

#define vp9_lref_hor_scale	G2_DEC_REG(36, 16, 0xffff)
#define vp9_lref_ver_scale	G2_DEC_REG(36,  0, 0xffff)

#define vp9_gref_hor_scale	G2_DEC_REG(37, 16, 0xffff)
#define vp9_gref_ver_scale	G2_DEC_REG(37,  0, 0xffff)

#define vp9_aref_hor_scale	G2_DEC_REG(38, 16, 0xffff)
#define vp9_aref_ver_scale	G2_DEC_REG(38,  0, 0xffff)

#define vp9_filt_ref_adj_0	G2_DEC_REG(46, 24, 0x7f)
#define vp9_filt_ref_adj_1	G2_DEC_REG(46, 16, 0x7f)
#define vp9_filt_ref_adj_2	G2_DEC_REG(46,  8, 0x7f)
#define vp9_filt_ref_adj_3	G2_DEC_REG(46,  0, 0x7f)

#define vp9_filt_mb_adj_0	G2_DEC_REG(47, 24, 0x7f)
#define vp9_filt_mb_adj_1	G2_DEC_REG(47, 16, 0x7f)
#define vp9_filt_mb_adj_2	G2_DEC_REG(47,  8, 0x7f)
#define vp9_filt_mb_adj_3	G2_DEC_REG(47,  0, 0x7f)

#define g2_apf_threshold	G2_DEC_REG(55, 0, 0xffff)

#define g2_clk_gate_e		G2_DEC_REG(58, 16, 0x1)
#define g2_double_buffer_e	G2_DEC_REG(58, 15, 0x1)
#define g2_buswidth		G2_DEC_REG(58, 8,  0x7)
#define g2_max_burst		G2_DEC_REG(58, 0,  0xff)

#define g2_down_scale_e		G2_DEC_REG(184, 7, 0x1)
#define g2_down_scale_y		G2_DEC_REG(184, 2, 0x3)
#define g2_down_scale_x		G2_DEC_REG(184, 0, 0x3)

#define G2_REG_CONFIG				G2_SWREG(58)
#define G2_REG_CONFIG_DEC_CLK_GATE_E		BIT(16)
#define G2_REG_CONFIG_DEC_CLK_GATE_IDLE_E	BIT(17)

#define G2_OUT_LUMA_ADDR		(G2_SWREG(65))
#define G2_REF_LUMA_ADDR(i)		(G2_SWREG(67)  + ((i) * 0x8))
#define G2_VP9_SEGMENT_WRITE_ADDR	(G2_SWREG(79))
#define G2_VP9_SEGMENT_READ_ADDR	(G2_SWREG(81))
#define G2_OUT_CHROMA_ADDR		(G2_SWREG(99))
#define G2_REF_CHROMA_ADDR(i)		(G2_SWREG(101) + ((i) * 0x8))
#define G2_OUT_MV_ADDR			(G2_SWREG(133))
#define G2_REF_MV_ADDR(i)		(G2_SWREG(135) + ((i) * 0x8))
#define G2_TILE_SIZES_ADDR		(G2_SWREG(167))
#define G2_STREAM_ADDR			(G2_SWREG(169))
#define G2_HEVC_SCALING_LIST_ADDR	(G2_SWREG(171))
#define G2_VP9_CTX_COUNT_ADDR		(G2_SWREG(171))
#define G2_VP9_PROBS_ADDR		(G2_SWREG(173))
#define G2_RS_OUT_LUMA_ADDR		(G2_SWREG(175))
#define G2_RS_OUT_CHROMA_ADDR		(G2_SWREG(177))
#define G2_TILE_FILTER_ADDR		(G2_SWREG(179))
#define G2_TILE_SAO_ADDR		(G2_SWREG(181))
#define G2_TILE_BSD_ADDR		(G2_SWREG(183))
#define G2_DS_DST			(G2_SWREG(186))
#define G2_DS_DST_CHR			(G2_SWREG(188))

#define g2_strm_buffer_len	G2_DEC_REG(258, 0, 0xffffffff)
#define g2_strm_start_offset	G2_DEC_REG(259, 0, 0xffffffff)

#endif
