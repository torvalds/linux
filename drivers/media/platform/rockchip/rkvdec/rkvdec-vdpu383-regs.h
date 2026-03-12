/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Video Decoder VDPU383 driver registers description
 *
 * Copyright (C) 2025 Collabora, Ltd.
 *  Detlev Casanova <detlev.casanova@collabora.com>
 */

#ifndef _RKVDEC_VDPU838_REGS_H_
#define _RKVDEC_VDPU838_REGS_H_

#include <linux/types.h>

#define VDPU383_OFFSET_COMMON_REGS		(8 * sizeof(u32))
#define VDPU383_OFFSET_CODEC_PARAMS_REGS	(64 * sizeof(u32))
#define VDPU383_OFFSET_COMMON_ADDR_REGS		(128 * sizeof(u32))
#define VDPU383_OFFSET_CODEC_ADDR_REGS		(168 * sizeof(u32))
#define VDPU383_OFFSET_POC_HIGHBIT_REGS		(200 * sizeof(u32))

#define VDPU383_MODE_HEVC	0
#define VDPU383_MODE_H264	1

#define VDPU383_TIMEOUT_1080p		(0xffffff)
#define VDPU383_TIMEOUT_4K		(0x2cfffff)
#define VDPU383_TIMEOUT_8K		(0x4ffffff)
#define VDPU383_TIMEOUT_MAX		(0xffffffff)

#define VDPU383_LINK_TIMEOUT_THRESHOLD	0x54

#define VDPU383_LINK_IP_ENABLE		0x58
#define VDPU383_IP_CRU_MODE		BIT(24)

#define VDPU383_LINK_DEC_ENABLE		0x40
#define VDPU383_DEC_E_BIT		BIT(0)

#define VDPU383_LINK_INT_EN		0x048
#define VDPU383_INT_EN_IRQ		BIT(0)
#define VDPU383_INT_EN_LINE_IRQ		BIT(1)

#define VDPU383_LINK_STA_INT		0x04c
#define VDPU383_STA_INT_DEC_RDY_STA	BIT(0)
#define VDPU383_STA_INT_SOFTRESET_RDY	(BIT(10) | BIT(11))
#define VDPU383_STA_INT_ALL		0x3ff

struct vdpu383_regs_common {
	u32 reg008_dec_mode;

	struct {
		u32 fbc_e			: 1;
		u32 tile_e			: 1;
		u32 reserve0			: 2;
		u32 buf_empty_en		: 1;
		u32 scale_down_en		: 1;
		u32 reserve1			: 1;
		u32 pix_range_det_e		: 1;
		u32 av1_fgs_en			: 1;
		u32 reserve2			: 7;
		u32 line_irq_en			: 1;
		u32 out_cbcr_swap		: 1;
		u32 fbc_force_uncompress	: 1;
		u32 fbc_sparse_mode		: 1;
		u32 reserve3			: 12;
	} reg009_important_en;

	struct {
		u32 strmd_auto_gating_e		: 1;
		u32 inter_auto_gating_e		: 1;
		u32 intra_auto_gating_e		: 1;
		u32 transd_auto_gating_e	: 1;
		u32 recon_auto_gating_e		: 1;
		u32 filterd_auto_gating_e	: 1;
		u32 bus_auto_gating_e		: 1;
		u32 ctrl_auto_gating_e		: 1;
		u32 rcb_auto_gating_e		: 1;
		u32 err_prc_auto_gating_e	: 1;
		u32 reserve0			: 22;
	} reg010_block_gating_en;

	struct {
		u32 reserve0			: 9;
		u32 dec_timeout_dis		: 1;
		u32 reserve1			: 22;
	} reg011_cfg_para;

	struct {
		u32 reserve0			: 7;
		u32 cache_hash_mask		: 25;
	} reg012_cache_hash_mask;

	u32 reg013_core_timeout_threshold;

	struct {
		u32 dec_line_irq_step		: 16;
		u32 dec_line_offset_y_st	: 16;
	} reg014_line_irq_ctrl;

	struct {
		u32 rkvdec_frame_rdy_sta	: 1;
		u32 rkvdec_strm_error_sta	: 1;
		u32 rkvdec_core_timeout_sta	: 1;
		u32 rkvdec_ip_timeout_sta	: 1;
		u32 rkvdec_bus_error_sta	: 1;
		u32 rkvdec_buffer_empty_sta	: 1;
		u32 rkvdec_colmv_ref_error_sta	: 1;
		u32 rkvdec_error_spread_sta	: 1;
		u32 create_core_timeout_sta	: 1;
		u32 wlast_miss_match_sta	: 1;
		u32 rkvdec_core_rst_rdy_sta	: 1;
		u32 rkvdec_ip_rst_rdy_sta	: 1;
		u32 force_busidle_rdy_sta	: 1;
		u32 ltb_pause_rdy_sta		: 1;
		u32 ltb_end_flag		: 1;
		u32 unsupport_decmode_error_sta	: 1;
		u32 wmask_bits			: 15;
		u32 reserve0			: 1;
	} reg015_irq_sta;

	struct {
		u32 error_proc_disable		: 1;
		u32 reserve0			: 7;
		u32 error_spread_disable	: 1;
		u32 reserve1			: 15;
		u32 roi_error_ctu_cal_en	: 1;
		u32 reserve2			: 7;
	} reg016_error_ctrl_set;

	struct {
		u32 roi_x_ctu_offset_st		: 12;
		u32 reserve0			: 4;
		u32 roi_y_ctu_offset_st		: 12;
		u32 reserve1			: 4;
	} reg017_err_roi_ctu_offset_start;

	struct {
		u32 roi_x_ctu_offset_end	: 12;
		u32 reserve0			: 4;
		u32 roi_y_ctu_offset_end	: 12;
		u32 reserve1			: 4;
	} reg018_err_roi_ctu_offset_end;

	struct {
		u32 avs2_ref_error_field	: 1;
		u32 avs2_ref_error_topfield	: 1;
		u32 ref_error_topfield_used	: 1;
		u32 ref_error_botfield_used	: 1;
		u32 reserve0			: 28;
	} reg019_error_ref_info;

	u32 reg020_cabac_error_en_lowbits;
	u32 reg021_cabac_error_en_highbits;

	u32 reg022_reserved;

	struct {
		u32 fill_y			: 10;
		u32 fill_u			: 10;
		u32 fill_v			: 10;
		u32 reserve0			: 2;
	} reg023_invalid_pixel_fill;

	u32 reg024_026_reserved[3];

	struct {
		u32 reserve0			: 4;
		u32 ctu_align_wr_en		: 1;
		u32 reserve1			: 27;
	} reg027_align_en;

	struct {
		u32 axi_perf_work_e		: 1;
		u32 reserve0			: 2;
		u32 axi_cnt_type		: 1;
		u32 rd_latency_id		: 8;
		u32 reserve1			: 4;
		u32 rd_latency_thr		: 12;
		u32 reserve2			: 4;
	} reg028_debug_perf_latency_ctrl0;

	struct {
		u32 addr_align_type		: 2;
		u32 ar_cnt_id_type		: 1;
		u32 aw_cnt_id_type		: 1;
		u32 ar_count_id			: 8;
		u32 reserve0			: 4;
		u32 aw_count_id			: 8;
		u32 rd_band_width_mode		: 1;
		u32 reserve1			: 7;
	} reg029_debug_perf_latency_ctrl1;

	struct {
		u32 axi_wr_qos_level		: 4;
		u32 reserve0			: 4;
		u32 axi_wr_qos			: 4;
		u32 reserve1			: 4;
		u32 axi_rd_qos_level		: 4;
		u32 reserve2			: 4;
		u32 axi_rd_qos			: 4;
		u32 reserve3			: 4;
	} reg030_qos_ctrl;
};

struct vdpu383_regs_common_addr {
	u32 reg128_strm_base;
	u32 reg129_rps_base;
	u32 reg130_cabactbl_base;
	u32 reg131_gbl_base;
	u32 reg132_scanlist_addr;
	u32 reg133_scale_down_base;
	u32 reg134_fgs_base;
	u32 reg135_139_reserved[5];

	struct rcb_info {
		u32 offset;
		u32 size;
	} reg140_162_rcb_info[11];
};

struct vdpu383_regs_h26x_addr {
	u32 reg168_decout_base;
	u32 reg169_error_ref_base;
	u32 reg170_185_ref_base[16];
	u32 reg186_191_reserved[6];
	u32 reg192_payload_st_cur_base;
	u32 reg193_fbc_payload_offset;
	u32 reg194_payload_st_error_ref_base;
	u32 reg195_210_payload_st_ref_base[16];
	u32 reg211_215_reserved[5];
	u32 reg216_colmv_cur_base;
	u32 reg217_232_colmv_ref_base[16];
};

struct vdpu383_regs_h26x_params {
	u32 reg064_start_decoder;
	u32 reg065_strm_start_bit;
	u32 reg066_stream_len;
	u32 reg067_global_len;
	u32 reg068_hor_virstride;
	u32 reg069_raster_uv_hor_virstride;
	u32 reg070_y_virstride;
	u32 reg071_scl_ref_hor_virstride;
	u32 reg072_scl_ref_raster_uv_hor_virstride;
	u32 reg073_scl_ref_virstride;
	u32 reg074_fgs_ref_hor_virstride;
	u32 reg075_079_reserved[5];
	u32 reg080_error_ref_hor_virstride;
	u32 reg081_error_ref_raster_uv_hor_virstride;
	u32 reg082_error_ref_virstride;
	u32 reg083_ref0_hor_virstride;
	u32 reg084_ref0_raster_uv_hor_virstride;
	u32 reg085_ref0_virstride;
	u32 reg086_ref1_hor_virstride;
	u32 reg087_ref1_raster_uv_hor_virstride;
	u32 reg088_ref1_virstride;
	u32 reg089_ref2_hor_virstride;
	u32 reg090_ref2_raster_uv_hor_virstride;
	u32 reg091_ref2_virstride;
	u32 reg092_ref3_hor_virstride;
	u32 reg093_ref3_raster_uv_hor_virstride;
	u32 reg094_ref3_virstride;
	u32 reg095_ref4_hor_virstride;
	u32 reg096_ref4_raster_uv_hor_virstride;
	u32 reg097_ref4_virstride;
	u32 reg098_ref5_hor_virstride;
	u32 reg099_ref5_raster_uv_hor_virstride;
	u32 reg100_ref5_virstride;
	u32 reg101_ref6_hor_virstride;
	u32 reg102_ref6_raster_uv_hor_virstride;
	u32 reg103_ref6_virstride;
	u32 reg104_ref7_hor_virstride;
	u32 reg105_ref7_raster_uv_hor_virstride;
	u32 reg106_ref7_virstride;
};

struct vdpu383_regs_h26x {
	struct vdpu383_regs_common		common;		/* 8-30 */
	struct vdpu383_regs_h26x_params		h26x_params;	/* 64-74, 80-106 */
	struct vdpu383_regs_common_addr		common_addr;	/* 128-134, 140-161 */
	struct vdpu383_regs_h26x_addr		h26x_addr;	/* 168-185, 192-210, 216-232 */
} __packed;

#endif /* __RKVDEC_VDPU838_REGS_H__ */
