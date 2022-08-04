/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 */

#ifndef _ROCKCHIP_DRM_VOP_H
#define _ROCKCHIP_DRM_VOP_H

#include <drm/drm_plane.h>
#include <drm/drm_modes.h>

/*
 * major: IP major version, used for IP structure
 * minor: big feature change under same structure
 */
#define VOP_VERSION(major, minor)	((major) << 8 | (minor))
#define VOP_MAJOR(version)		((version) >> 8)
#define VOP_MINOR(version)		((version) & 0xff)

#define VOP_VERSION_RK3568	VOP_VERSION(0x40, 0x15)
#define VOP_VERSION_RK3588	VOP_VERSION(0x40, 0x17)

#define ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE	BIT(0)
#define ROCKCHIP_OUTPUT_DUAL_CHANNEL_ODD_EVEN_MODE	BIT(1)
#define ROCKCHIP_OUTPUT_DATA_SWAP			BIT(2)
/* MIPI DSI DataStream(cmd) mode on rk3588 */
#define ROCKCHIP_OUTPUT_MIPI_DS_MODE			BIT(3)

#define AFBDC_FMT_RGB565	0x0
#define AFBDC_FMT_U8U8U8U8	0x5
#define AFBDC_FMT_U8U8U8	0x4

#define VOP_FEATURE_OUTPUT_RGB10	BIT(0)
#define VOP_FEATURE_INTERNAL_RGB	BIT(1)
#define VOP_FEATURE_ALPHA_SCALE		BIT(2)
#define VOP_FEATURE_HDR10		BIT(3)
#define VOP_FEATURE_NEXT_HDR		BIT(4)
/* a feature to splice two windows and two vps to support resolution > 4096 */
#define VOP_FEATURE_SPLICE		BIT(5)
#define VOP_FEATURE_OVERSCAN		BIT(6)

#define VOP_FEATURE_OUTPUT_10BIT	VOP_FEATURE_OUTPUT_RGB10


#define WIN_FEATURE_HDR2SDR		BIT(0)
#define WIN_FEATURE_SDR2HDR		BIT(1)
#define WIN_FEATURE_PRE_OVERLAY		BIT(2)
#define WIN_FEATURE_AFBDC		BIT(3)
#define WIN_FEATURE_CLUSTER_MAIN	BIT(4)
#define WIN_FEATURE_CLUSTER_SUB		BIT(5)
/* Left win in splice mode */
#define WIN_FEATURE_SPLICE_LEFT		BIT(6)
/* a mirror win can only get fb address
 * from source win:
 * Cluster1---->Cluster0
 * Esmart1 ---->Esmart0
 * Smart1  ---->Smart0
 * This is a feather on rk3566
 */
#define WIN_FEATURE_MIRROR		BIT(6)
#define WIN_FEATURE_MULTI_AREA		BIT(7)


#define VOP2_SOC_VARIANT		4

#define ROCKCHIP_DSC_PPS_SIZE_BYTE	88

enum vop_vp_id {
	ROCKCHIP_VOP_VP0 = 0,
	ROCKCHIP_VOP_VP1,
	ROCKCHIP_VOP_VP2,
	ROCKCHIP_VOP_VP3,
};

enum vop_win_phy_id {
	ROCKCHIP_VOP_WIN0 = 0,
	ROCKCHIP_VOP_WIN1,
	ROCKCHIP_VOP_WIN2,
	ROCKCHIP_VOP_WIN3,
	ROCKCHIP_VOP_PHY_ID_INVALID = -1,
};

enum bcsh_out_mode {
	BCSH_OUT_MODE_BLACK,
	BCSH_OUT_MODE_BLUE,
	BCSH_OUT_MODE_COLOR_BAR,
	BCSH_OUT_MODE_NORMAL_VIDEO,
};

enum cabc_stage_mode {
	LAST_FRAME_PWM_VAL	= 0x0,
	CUR_FRAME_PWM_VAL	= 0x1,
	STAGE_BY_STAGE		= 0x2
};

enum cabc_stage_up_mode {
	MUL_MODE,
	ADD_MODE,
};

/*
 *  the delay number of a window in different mode.
 */
enum vop2_win_dly_mode {
	VOP2_DLY_MODE_DEFAULT,   /**< default mode */
	VOP2_DLY_MODE_HISO_S,    /** HDR in SDR out mode, as a SDR window */
	VOP2_DLY_MODE_HIHO_H,    /** HDR in HDR out mode, as a HDR window */
	VOP2_DLY_MODE_MAX,
};

/*
 * vop2 dsc id
 */
#define ROCKCHIP_VOP2_DSC_8K	0
#define ROCKCHIP_VOP2_DSC_4K	1

/*
 * vop2 internal power domain id,
 * should be all none zero, 0 will be
 * treat as invalid;
 */
#define VOP2_PD_CLUSTER0	BIT(0)
#define VOP2_PD_CLUSTER1	BIT(1)
#define VOP2_PD_CLUSTER2	BIT(2)
#define VOP2_PD_CLUSTER3	BIT(3)
#define VOP2_PD_DSC_8K		BIT(5)
#define VOP2_PD_DSC_4K		BIT(6)
#define VOP2_PD_ESMART		BIT(7)

/*
 * vop2 submem power gate,
 * should be all none zero, 0 will be
 * treat as invalid;
 */
#define VOP2_MEM_PG_VP0		BIT(0)
#define VOP2_MEM_PG_VP1		BIT(1)
#define VOP2_MEM_PG_VP2		BIT(2)
#define VOP2_MEM_PG_VP3		BIT(3)
#define VOP2_MEM_PG_DB0		BIT(4)
#define VOP2_MEM_PG_DB1		BIT(5)
#define VOP2_MEM_PG_DB2		BIT(6)
#define VOP2_MEM_PG_WB		BIT(7)

#define DSP_BG_SWAP		0x1
#define DSP_RB_SWAP		0x2
#define DSP_RG_SWAP		0x4
#define DSP_DELTA_SWAP		0x8

enum vop_csc_format {
	CSC_BT601L,
	CSC_BT709L,
	CSC_BT601F,
	CSC_BT2020,
};

enum vop_csc_mode {
	CSC_RGB,
	CSC_YUV,
};

enum vop_data_format {
	VOP_FMT_ARGB8888 = 0,
	VOP_FMT_RGB888,
	VOP_FMT_RGB565 = 2,
	VOP_FMT_YUYV = 2,
	VOP_FMT_YUV420SP = 4,
	VOP_FMT_YUV422SP,
	VOP_FMT_YUV444SP,
};

enum vop_dsc_interface_mode {
	VOP_DSC_IF_DISABLE = 0,
	VOP_DSC_IF_HDMI = 1,
	VOP_DSC_IF_MIPI_DS_MODE = 2,
	VOP_DSC_IF_MIPI_VIDEO_MODE = 3,
};

struct vop_reg_data {
	uint32_t offset;
	uint32_t value;
};

struct vop_reg {
	uint32_t mask;
	uint32_t offset:17;
	uint32_t shift:5;
	uint32_t begin_minor:4;
	uint32_t end_minor:4;
	uint32_t reserved:2;
	uint32_t major:3;
	uint32_t write_mask:1;
};

struct vop_csc {
	struct vop_reg y2r_en;
	struct vop_reg r2r_en;
	struct vop_reg r2y_en;
	struct vop_reg csc_mode;

	uint32_t y2r_offset;
	uint32_t r2r_offset;
	uint32_t r2y_offset;
};

struct vop_rect {
	int width;
	int height;
};

struct vop_ctrl {
	struct vop_reg version;
	struct vop_reg standby;
	struct vop_reg dma_stop;
	struct vop_reg axi_outstanding_max_num;
	struct vop_reg axi_max_outstanding_en;
	struct vop_reg htotal_pw;
	struct vop_reg hact_st_end;
	struct vop_reg vtotal_pw;
	struct vop_reg vact_st_end;
	struct vop_reg vact_st_end_f1;
	struct vop_reg vs_st_end_f1;
	struct vop_reg hpost_st_end;
	struct vop_reg vpost_st_end;
	struct vop_reg vpost_st_end_f1;
	struct vop_reg post_scl_factor;
	struct vop_reg post_scl_ctrl;
	struct vop_reg dsp_interlace;
	struct vop_reg global_regdone_en;
	struct vop_reg auto_gate_en;
	struct vop_reg post_lb_mode;
	struct vop_reg dsp_layer_sel;
	struct vop_reg overlay_mode;
	struct vop_reg core_dclk_div;
	struct vop_reg dclk_ddr;
	struct vop_reg p2i_en;
	struct vop_reg hdmi_dclk_out_en;
	struct vop_reg rgb_en;
	struct vop_reg lvds_en;
	struct vop_reg edp_en;
	struct vop_reg hdmi_en;
	struct vop_reg mipi_en;
	struct vop_reg data01_swap;
	struct vop_reg mipi_dual_channel_en;
	struct vop_reg dp_en;
	struct vop_reg dclk_pol;
	struct vop_reg pin_pol;
	struct vop_reg rgb_dclk_pol;
	struct vop_reg rgb_pin_pol;
	struct vop_reg lvds_dclk_pol;
	struct vop_reg lvds_pin_pol;
	struct vop_reg hdmi_dclk_pol;
	struct vop_reg hdmi_pin_pol;
	struct vop_reg edp_dclk_pol;
	struct vop_reg edp_pin_pol;
	struct vop_reg mipi_dclk_pol;
	struct vop_reg mipi_pin_pol;
	struct vop_reg dp_dclk_pol;
	struct vop_reg dp_pin_pol;
	struct vop_reg dither_down_sel;
	struct vop_reg dither_down_mode;
	struct vop_reg dither_down_en;
	struct vop_reg pre_dither_down_en;
	struct vop_reg dither_up_en;

	struct vop_reg sw_dac_sel;
	struct vop_reg tve_sw_mode;
	struct vop_reg tve_dclk_pol;
	struct vop_reg tve_dclk_en;
	struct vop_reg sw_genlock;
	struct vop_reg sw_uv_offset_en;
	struct vop_reg dsp_out_yuv;
	struct vop_reg dsp_data_swap;
	struct vop_reg yuv_clip;
	struct vop_reg dsp_ccir656_avg;
	struct vop_reg dsp_black;
	struct vop_reg dsp_blank;
	struct vop_reg dsp_outzero;
	struct vop_reg update_gamma_lut;
	struct vop_reg lut_buffer_index;
	struct vop_reg dsp_lut_en;

	struct vop_reg out_mode;

	struct vop_reg xmirror;
	struct vop_reg ymirror;
	struct vop_reg dsp_background;

	/* AFBDC */
	struct vop_reg afbdc_en;
	struct vop_reg afbdc_sel;
	struct vop_reg afbdc_format;
	struct vop_reg afbdc_hreg_block_split;
	struct vop_reg afbdc_pic_size;
	struct vop_reg afbdc_hdr_ptr;
	struct vop_reg afbdc_rstn;
	struct vop_reg afbdc_pic_vir_width;
	struct vop_reg afbdc_pic_offset;
	struct vop_reg afbdc_axi_ctrl;

	/* BCSH */
	struct vop_reg bcsh_brightness;
	struct vop_reg bcsh_contrast;
	struct vop_reg bcsh_sat_con;
	struct vop_reg bcsh_sin_hue;
	struct vop_reg bcsh_cos_hue;
	struct vop_reg bcsh_r2y_csc_mode;
	struct vop_reg bcsh_r2y_en;
	struct vop_reg bcsh_y2r_csc_mode;
	struct vop_reg bcsh_y2r_en;
	struct vop_reg bcsh_color_bar;
	struct vop_reg bcsh_out_mode;
	struct vop_reg bcsh_en;

	/* HDR */
	struct vop_reg level2_overlay_en;
	struct vop_reg alpha_hard_calc;
	struct vop_reg hdr2sdr_en;
	struct vop_reg hdr2sdr_en_win0_csc;
	struct vop_reg hdr2sdr_src_min;
	struct vop_reg hdr2sdr_src_max;
	struct vop_reg hdr2sdr_normfaceetf;
	struct vop_reg hdr2sdr_dst_min;
	struct vop_reg hdr2sdr_dst_max;
	struct vop_reg hdr2sdr_normfacgamma;

	struct vop_reg bt1886eotf_pre_conv_en;
	struct vop_reg rgb2rgb_pre_conv_en;
	struct vop_reg rgb2rgb_pre_conv_mode;
	struct vop_reg st2084oetf_pre_conv_en;
	struct vop_reg bt1886eotf_post_conv_en;
	struct vop_reg rgb2rgb_post_conv_en;
	struct vop_reg rgb2rgb_post_conv_mode;
	struct vop_reg st2084oetf_post_conv_en;
	struct vop_reg win_csc_mode_sel;

	/* MCU OUTPUT */
	struct vop_reg mcu_pix_total;
	struct vop_reg mcu_cs_pst;
	struct vop_reg mcu_cs_pend;
	struct vop_reg mcu_rw_pst;
	struct vop_reg mcu_rw_pend;
	struct vop_reg mcu_clk_sel;
	struct vop_reg mcu_hold_mode;
	struct vop_reg mcu_frame_st;
	struct vop_reg mcu_rs;
	struct vop_reg mcu_bypass;
	struct vop_reg mcu_type;
	struct vop_reg mcu_rw_bypass_port;

	/* bt1120 */
	struct vop_reg bt1120_yc_swap;
	struct vop_reg bt1120_en;

	/* bt656 */
	struct vop_reg bt656_en;

	struct vop_reg reg_done_frm;
	struct vop_reg cfg_done;
};

struct vop_intr {
	const int *intrs;
	uint32_t nintrs;
	struct vop_reg line_flag_num[2];
	struct vop_reg enable;
	struct vop_reg clear;
	struct vop_reg status;
};

struct vop_scl_extension {
	struct vop_reg cbcr_vsd_mode;
	struct vop_reg cbcr_vsu_mode;
	struct vop_reg cbcr_hsd_mode;
	struct vop_reg cbcr_ver_scl_mode;
	struct vop_reg cbcr_hor_scl_mode;
	struct vop_reg yrgb_vsd_mode;
	struct vop_reg yrgb_vsu_mode;
	struct vop_reg yrgb_hsd_mode;
	struct vop_reg yrgb_ver_scl_mode;
	struct vop_reg yrgb_hor_scl_mode;
	struct vop_reg line_load_mode;
	struct vop_reg cbcr_axi_gather_num;
	struct vop_reg yrgb_axi_gather_num;
	struct vop_reg vsd_cbcr_gt2;
	struct vop_reg vsd_cbcr_gt4;
	struct vop_reg vsd_yrgb_gt2;
	struct vop_reg vsd_yrgb_gt4;
	struct vop_reg bic_coe_sel;
	struct vop_reg cbcr_axi_gather_en;
	struct vop_reg yrgb_axi_gather_en;
	struct vop_reg lb_mode;
};

struct vop_scl_regs {
	const struct vop_scl_extension *ext;

	struct vop_reg scale_yrgb_x;
	struct vop_reg scale_yrgb_y;
	struct vop_reg scale_cbcr_x;
	struct vop_reg scale_cbcr_y;
};

struct vop_afbc {
	struct vop_reg enable;
	struct vop_reg win_sel;
	struct vop_reg format;
	struct vop_reg rb_swap;
	struct vop_reg uv_swap;
	struct vop_reg auto_gating_en;
	struct vop_reg rotate;
	struct vop_reg block_split_en;
	struct vop_reg pic_vir_width;
	struct vop_reg tile_num;
	struct vop_reg pic_offset;
	struct vop_reg pic_size;
	struct vop_reg dsp_offset;
	struct vop_reg transform_offset;
	struct vop_reg hdr_ptr;
	struct vop_reg half_block_en;
	struct vop_reg xmirror;
	struct vop_reg ymirror;
	struct vop_reg rotate_270;
	struct vop_reg rotate_90;
	struct vop_reg rstn;
};

struct vop_csc_table {
	const uint32_t *y2r_bt601;
	const uint32_t *y2r_bt601_12_235;
	const uint32_t *y2r_bt601_10bit;
	const uint32_t *y2r_bt601_10bit_12_235;
	const uint32_t *r2y_bt601;
	const uint32_t *r2y_bt601_12_235;
	const uint32_t *r2y_bt601_10bit;
	const uint32_t *r2y_bt601_10bit_12_235;

	const uint32_t *y2r_bt709;
	const uint32_t *y2r_bt709_10bit;
	const uint32_t *r2y_bt709;
	const uint32_t *r2y_bt709_10bit;

	const uint32_t *y2r_bt2020;
	const uint32_t *r2y_bt2020;

	const uint32_t *r2r_bt709_to_bt2020;
	const uint32_t *r2r_bt2020_to_bt709;
};

struct vop_hdr_table {
	const uint32_t hdr2sdr_eetf_oetf_y0_offset;
	const uint32_t hdr2sdr_eetf_oetf_y1_offset;
	const uint32_t *hdr2sdr_eetf_yn;
	const uint32_t *hdr2sdr_bt1886oetf_yn;
	const uint32_t hdr2sdr_sat_y0_offset;
	const uint32_t hdr2sdr_sat_y1_offset;
	const uint32_t *hdr2sdr_sat_yn;

	const uint32_t hdr2sdr_src_range_min;
	const uint32_t hdr2sdr_src_range_max;
	const uint32_t hdr2sdr_normfaceetf;
	const uint32_t hdr2sdr_dst_range_min;
	const uint32_t hdr2sdr_dst_range_max;
	const uint32_t hdr2sdr_normfacgamma;

	const uint32_t sdr2hdr_eotf_oetf_y0_offset;
	const uint32_t sdr2hdr_eotf_oetf_y1_offset;
	const uint32_t *sdr2hdr_bt1886eotf_yn_for_hlg_hdr;
	const uint32_t *sdr2hdr_bt1886eotf_yn_for_bt2020;
	const uint32_t *sdr2hdr_bt1886eotf_yn_for_hdr;
	const uint32_t *sdr2hdr_st2084oetf_yn_for_hlg_hdr;
	const uint32_t *sdr2hdr_st2084oetf_yn_for_bt2020;
	const uint32_t *sdr2hdr_st2084oetf_yn_for_hdr;
	const uint32_t sdr2hdr_oetf_dx_dxpow1_offset;
	const uint32_t *sdr2hdr_st2084oetf_dxn_pow2;
	const uint32_t *sdr2hdr_st2084oetf_dxn;
	const uint32_t sdr2hdr_oetf_xn1_offset;
	const uint32_t *sdr2hdr_st2084oetf_xn;
};

enum {
	VOP_CSC_Y2R_BT601,
	VOP_CSC_Y2R_BT709,
	VOP_CSC_Y2R_BT2020,
	VOP_CSC_R2Y_BT601,
	VOP_CSC_R2Y_BT709,
	VOP_CSC_R2Y_BT2020,
	VOP_CSC_R2R_BT2020_TO_BT709,
	VOP_CSC_R2R_BT709_TO_2020,
};

enum _vop_overlay_mode {
	VOP_RGB_DOMAIN,
	VOP_YUV_DOMAIN
};

enum _vop_sdr2hdr_func {
	SDR2HDR_FOR_BT2020,
	SDR2HDR_FOR_HDR,
	SDR2HDR_FOR_HLG_HDR,
};

enum _vop_rgb2rgb_conv_mode {
	BT709_TO_BT2020,
	BT2020_TO_BT709,
};

enum _MCU_IOCTL {
	MCU_WRCMD = 0,
	MCU_WRDATA,
	MCU_SETBYPASS,
};

struct vop_win_phy {
	const struct vop_scl_regs *scl;
	const uint32_t *data_formats;
	uint32_t nformats;

	struct vop_reg gate;
	struct vop_reg enable;
	struct vop_reg format;
	struct vop_reg interlace_read;
	struct vop_reg fmt_10;
	struct vop_reg fmt_yuyv;
	struct vop_reg csc_mode;
	struct vop_reg xmirror;
	struct vop_reg ymirror;
	struct vop_reg rb_swap;
	struct vop_reg act_info;
	struct vop_reg dsp_info;
	struct vop_reg dsp_st;
	struct vop_reg yrgb_mst;
	struct vop_reg uv_mst;
	struct vop_reg yrgb_vir;
	struct vop_reg uv_vir;

	struct vop_reg channel;
	struct vop_reg dst_alpha_ctl;
	struct vop_reg src_alpha_ctl;
	struct vop_reg alpha_mode;
	struct vop_reg alpha_en;
	struct vop_reg alpha_pre_mul;
	struct vop_reg global_alpha_val;
	struct vop_reg color_key;
	struct vop_reg color_key_en;
};

struct vop_win_data {
	uint32_t base;
	enum drm_plane_type type;
	const struct vop_win_phy *phy;
	const struct vop_win_phy **area;
	const uint64_t *format_modifiers;
	const struct vop_csc *csc;
	unsigned int area_size;
	u64 feature;
};

struct vop2_cluster_regs {
	struct vop_reg enable;
	struct vop_reg afbc_enable;
	struct vop_reg lb_mode;

	struct vop_reg src_color_ctrl;
	struct vop_reg dst_color_ctrl;
	struct vop_reg src_alpha_ctrl;
	struct vop_reg dst_alpha_ctrl;
};

struct vop2_scl_regs {
	struct vop_reg scale_yrgb_x;
	struct vop_reg scale_yrgb_y;
	struct vop_reg scale_cbcr_x;
	struct vop_reg scale_cbcr_y;
	struct vop_reg yrgb_hor_scl_mode;
	struct vop_reg yrgb_hscl_filter_mode;
	struct vop_reg yrgb_ver_scl_mode;
	struct vop_reg yrgb_vscl_filter_mode;
	struct vop_reg cbcr_ver_scl_mode;
	struct vop_reg cbcr_hscl_filter_mode;
	struct vop_reg cbcr_hor_scl_mode;
	struct vop_reg cbcr_vscl_filter_mode;
	struct vop_reg vsd_cbcr_gt2;
	struct vop_reg vsd_cbcr_gt4;
	struct vop_reg vsd_yrgb_gt2;
	struct vop_reg vsd_yrgb_gt4;
	struct vop_reg bic_coe_sel;
};

struct vop2_win_regs {
	const struct vop2_scl_regs *scl;
	const struct vop2_cluster_regs *cluster;
	const struct vop_afbc *afbc;

	struct vop_reg gate;
	struct vop_reg enable;
	struct vop_reg format;
	struct vop_reg csc_mode;
	struct vop_reg xmirror;
	struct vop_reg ymirror;
	struct vop_reg rb_swap;
	struct vop_reg uv_swap;
	struct vop_reg act_info;
	struct vop_reg dsp_info;
	struct vop_reg dsp_st;
	struct vop_reg yrgb_mst;
	struct vop_reg uv_mst;
	struct vop_reg yrgb_vir;
	struct vop_reg uv_vir;
	struct vop_reg yuv_clip;
	struct vop_reg lb_mode;
	struct vop_reg y2r_en;
	struct vop_reg r2y_en;
	struct vop_reg channel;
	struct vop_reg dst_alpha_ctl;
	struct vop_reg src_alpha_ctl;
	struct vop_reg alpha_mode;
	struct vop_reg alpha_en;
	struct vop_reg global_alpha_val;
	struct vop_reg color_key;
	struct vop_reg color_key_en;
	struct vop_reg dither_up;
	struct vop_reg axi_id;
	struct vop_reg axi_yrgb_id;
	struct vop_reg axi_uv_id;
};

struct vop2_video_port_regs {
	struct vop_reg cfg_done;
	struct vop_reg overlay_mode;
	struct vop_reg dsp_background;
	struct vop_reg port_mux;
	struct vop_reg out_mode;
	struct vop_reg standby;
	struct vop_reg dsp_interlace;
	struct vop_reg dsp_filed_pol;
	struct vop_reg dsp_data_swap;
	struct vop_reg dsp_x_mir_en;
	struct vop_reg post_dsp_out_r2y;
	struct vop_reg pre_scan_htiming;
	struct vop_reg htotal_pw;
	struct vop_reg hact_st_end;
	struct vop_reg dsp_vtotal;
	struct vop_reg sw_dsp_vtotal_imd;
	struct vop_reg dsp_vs_end;
	struct vop_reg vact_st_end;
	struct vop_reg vact_st_end_f1;
	struct vop_reg vs_st_end_f1;
	struct vop_reg hpost_st_end;
	struct vop_reg vpost_st_end;
	struct vop_reg vpost_st_end_f1;
	struct vop_reg post_scl_factor;
	struct vop_reg post_scl_ctrl;
	struct vop_reg dither_down_sel;
	struct vop_reg dither_down_mode;
	struct vop_reg dither_down_en;
	struct vop_reg pre_dither_down_en;
	struct vop_reg dither_up_en;
	struct vop_reg bg_dly;

	struct vop_reg core_dclk_div;
	struct vop_reg p2i_en;
	struct vop_reg dual_channel_en;
	struct vop_reg dual_channel_swap;
	struct vop_reg dsp_lut_en;

	struct vop_reg dclk_div2;
	struct vop_reg dclk_div2_phase_lock;

	struct vop_reg hdr10_en;
	struct vop_reg hdr_lut_update_en;
	struct vop_reg hdr_lut_mode;
	struct vop_reg hdr_lut_mst;
	struct vop_reg sdr2hdr_eotf_en;
	struct vop_reg sdr2hdr_r2r_en;
	struct vop_reg sdr2hdr_r2r_mode;
	struct vop_reg sdr2hdr_oetf_en;
	struct vop_reg sdr2hdr_bypass_en;
	struct vop_reg sdr2hdr_auto_gating_en;
	struct vop_reg sdr2hdr_path_en;
	struct vop_reg hdr2sdr_en;
	struct vop_reg hdr2sdr_bypass_en;
	struct vop_reg hdr2sdr_auto_gating_en;
	struct vop_reg hdr2sdr_src_min;
	struct vop_reg hdr2sdr_src_max;
	struct vop_reg hdr2sdr_normfaceetf;
	struct vop_reg hdr2sdr_dst_min;
	struct vop_reg hdr2sdr_dst_max;
	struct vop_reg hdr2sdr_normfacgamma;
	uint32_t hdr2sdr_eetf_oetf_y0_offset;
	uint32_t hdr2sdr_sat_y0_offset;
	uint32_t sdr2hdr_eotf_oetf_y0_offset;
	uint32_t sdr2hdr_oetf_dx_pow1_offset;
	uint32_t sdr2hdr_oetf_xn1_offset;
	struct vop_reg hdr_src_color_ctrl;
	struct vop_reg hdr_dst_color_ctrl;
	struct vop_reg hdr_src_alpha_ctrl;
	struct vop_reg hdr_dst_alpha_ctrl;
	struct vop_reg bg_mix_ctrl;

	/* BCSH */
	struct vop_reg bcsh_brightness;
	struct vop_reg bcsh_contrast;
	struct vop_reg bcsh_sat_con;
	struct vop_reg bcsh_sin_hue;
	struct vop_reg bcsh_cos_hue;
	struct vop_reg bcsh_r2y_csc_mode;
	struct vop_reg bcsh_r2y_en;
	struct vop_reg bcsh_y2r_csc_mode;
	struct vop_reg bcsh_y2r_en;
	struct vop_reg bcsh_out_mode;
	struct vop_reg bcsh_en;

	/* 3d lut */
	struct vop_reg cubic_lut_en;
	struct vop_reg cubic_lut_update_en;
	struct vop_reg cubic_lut_mst;

	/* cru */
	struct vop_reg dclk_core_div;
	struct vop_reg dclk_out_div;
	struct vop_reg dclk_src_sel;

	struct vop_reg splice_en;

	struct vop_reg edpi_wms_hold_en;
	struct vop_reg edpi_te_en;
	struct vop_reg edpi_wms_fs;
	struct vop_reg gamma_update_en;
	struct vop_reg lut_dma_rid;
};

struct vop2_power_domain_regs {
	struct vop_reg pd;
	struct vop_reg status;
	struct vop_reg bisr_en_status;
	struct vop_reg pmu_status;
};

struct vop2_dsc_regs {
	/* DSC SYS CTRL */
	struct vop_reg dsc_port_sel;
	struct vop_reg dsc_man_mode;
	struct vop_reg dsc_interface_mode;
	struct vop_reg dsc_pixel_num;
	struct vop_reg dsc_pxl_clk_div;
	struct vop_reg dsc_cds_clk_div;
	struct vop_reg dsc_txp_clk_div;
	struct vop_reg dsc_init_dly_mode;
	struct vop_reg dsc_scan_en;
	struct vop_reg dsc_halt_en;
	struct vop_reg rst_deassert;
	struct vop_reg dsc_flush;
	struct vop_reg dsc_cfg_done;
	struct vop_reg dsc_init_dly_num;
	struct vop_reg scan_timing_para_imd_en;
	struct vop_reg dsc_htotal_pw;
	struct vop_reg dsc_hact_st_end;
	struct vop_reg dsc_vtotal;
	struct vop_reg dsc_vs_end;
	struct vop_reg dsc_vact_st_end;
	struct vop_reg dsc_error_status;

	/* DSC encoder */
	struct vop_reg dsc_pps0_3;
	struct vop_reg dsc_en;
	struct vop_reg dsc_rbit;
	struct vop_reg dsc_rbyt;
	struct vop_reg dsc_flal;
	struct vop_reg dsc_mer;
	struct vop_reg dsc_epb;
	struct vop_reg dsc_epl;
	struct vop_reg dsc_nslc;
	struct vop_reg dsc_sbo;
	struct vop_reg dsc_ifep;
	struct vop_reg dsc_pps_upd;
	struct vop_reg dsc_status;
	struct vop_reg dsc_ecw;
};

struct vop2_wb_regs {
	struct vop_reg enable;
	struct vop_reg format;
	struct vop_reg dither_en;
	struct vop_reg r2y_en;
	struct vop_reg yrgb_mst;
	struct vop_reg uv_mst;
	struct vop_reg vp_id;
	struct vop_reg fifo_throd;
	struct vop_reg scale_x_factor;
	struct vop_reg scale_x_en;
	struct vop_reg scale_y_en;
	struct vop_reg axi_yrgb_id;
	struct vop_reg axi_uv_id;
};

struct vop2_power_domain_data {
	uint8_t id;
	uint8_t parent_id;
	/*
	 * @module_id_mask: module id of which module this power domain is belongs to.
	 * PD_CLUSTER0,1,2,3 only belongs to CLUSTER0/1/2/3, PD_Esmart0 shared by Esmart1/2/3
	 */
	uint32_t module_id_mask;

	const struct vop2_power_domain_regs *regs;
};

/*
 * connector interface(RGB/HDMI/eDP/DP/MIPI) data
 */
struct vop2_connector_if_data {
	u32 id;
	const char *clk_src_name;
	const char *clk_parent_name;
	const char *pixclk_name;
	const char *dclk_name;
	u32 post_proc_div_shift;
	u32 if_div_shift;
	u32 if_div_yuv420_shift;
	u32 bus_div_shift;
	u32 pixel_clk_div_shift;
};

struct vop2_win_data {
	const char *name;
	uint8_t phys_id;
	uint8_t splice_win_id;
	uint8_t pd_id;
	uint8_t axi_id;
	uint8_t axi_yrgb_id;
	uint8_t axi_uv_id;

	uint32_t base;
	enum drm_plane_type type;

	uint32_t nformats;
	const uint32_t *formats;
	const uint64_t *format_modifiers;
	const unsigned int supported_rotations;

	const struct vop2_win_regs *regs;
	const struct vop2_win_regs **area;
	unsigned int area_size;

	/*
	 * vertical/horizontal scale up/down filter mode
	 */
	const u8 hsu_filter_mode;
	const u8 hsd_filter_mode;
	const u8 vsu_filter_mode;
	const u8 vsd_filter_mode;
	/**
	 * @layer_sel_id: defined by register OVERLAY_LAYER_SEL of VOP2
	 */
	int layer_sel_id;
	uint64_t feature;

	unsigned int max_upscale_factor;
	unsigned int max_downscale_factor;
	const uint8_t dly[VOP2_DLY_MODE_MAX];
};

struct dsc_error_info {
	u32 dsc_error_val;
	char dsc_error_info[50];
};

struct vop2_dsc_data {
	uint8_t id;
	uint8_t pd_id;
	uint8_t max_slice_num;
	uint8_t max_linebuf_depth;	/* used to generate the bitstream */
	uint8_t min_bits_per_pixel;	/* bit num after encoder compress */
	const char *dsc_txp_clk_src_name;
	const char *dsc_txp_clk_name;
	const char *dsc_pxl_clk_name;
	const char *dsc_cds_clk_name;
	const struct vop2_dsc_regs *regs;
};

struct vop2_wb_data {
	uint32_t nformats;
	const uint32_t *formats;
	struct vop_rect max_output;
	const struct vop2_wb_regs *regs;
	uint32_t fifo_depth;
};

struct vop2_video_port_data {
	char id;
	uint8_t splice_vp_id;
	uint16_t lut_dma_rid;
	uint32_t feature;
	uint64_t soc_id[VOP2_SOC_VARIANT];
	uint16_t gamma_lut_len;
	uint16_t cubic_lut_len;
	unsigned long dclk_max;
	struct vop_rect max_output;
	const u8 pre_scan_max_dly[4];
	const struct vop_intr *intr;
	const struct vop_hdr_table *hdr_table;
	const struct vop2_video_port_regs *regs;
};

struct vop2_layer_regs {
	struct vop_reg layer_sel;
};

/**
 * struct vop2_layer_data - The logic graphic layer in vop2
 *
 * The zorder:
 *   LAYERn
 *   LAYERn-1
 *     .
 *     .
 *     .
 *   LAYER5
 *   LAYER4
 *   LAYER3
 *   LAYER2
 *   LAYER1
 *   LAYER0
 *
 * Each layer can select a unused window as input than feed to
 * mixer for overlay.
 *
 * The pipeline in vop2:
 *
 * win-->layer-->mixer-->vp--->connector(RGB/LVDS/HDMI/MIPI)
 *
 */
struct vop2_layer_data {
	char id;
	const struct vop2_layer_regs *regs;
};

struct vop_grf_ctrl {
	struct vop_reg grf_dclk_inv;
	struct vop_reg grf_bt1120_clk_inv;
	struct vop_reg grf_bt656_clk_inv;
	struct vop_reg grf_edp0_en;
	struct vop_reg grf_edp1_en;
	struct vop_reg grf_hdmi0_en;
	struct vop_reg grf_hdmi1_en;
	struct vop_reg grf_hdmi0_dsc_en;
	struct vop_reg grf_hdmi1_dsc_en;
	struct vop_reg grf_hdmi0_pin_pol;
	struct vop_reg grf_hdmi1_pin_pol;
};

struct vop_data {
	const struct vop_reg_data *init_table;
	unsigned int table_size;
	const struct vop_ctrl *ctrl;
	const struct vop_intr *intr;
	const struct vop_win_data *win;
	const struct vop_csc_table *csc_table;
	const struct vop_hdr_table *hdr_table;
	const struct vop_grf_ctrl *grf_ctrl;
	unsigned int win_size;
	uint32_t version;
	struct vop_rect max_input;
	struct vop_rect max_output;
	u64 feature;
	u64 soc_id;
	u8 vop_id;
};

struct vop2_ctrl {
	struct vop_reg cfg_done_en;
	struct vop_reg wb_cfg_done;
	struct vop_reg auto_gating_en;
	struct vop_reg ovl_cfg_done_port;
	struct vop_reg ovl_port_mux_cfg_done_imd;
	struct vop_reg ovl_port_mux_cfg;
	struct vop_reg if_ctrl_cfg_done_imd;
	struct vop_reg version;
	struct vop_reg standby;
	struct vop_reg dma_stop;
	struct vop_reg lut_dma_en;
	struct vop_reg axi_outstanding_max_num;
	struct vop_reg axi_max_outstanding_en;
	struct vop_reg hdmi_dclk_out_en;
	struct vop_reg rgb_en;
	struct vop_reg hdmi0_en;
	struct vop_reg hdmi1_en;
	struct vop_reg dp0_en;
	struct vop_reg dp1_en;
	struct vop_reg edp0_en;
	struct vop_reg edp1_en;
	struct vop_reg mipi0_en;
	struct vop_reg mipi1_en;
	struct vop_reg lvds0_en;
	struct vop_reg lvds1_en;
	struct vop_reg bt656_en;
	struct vop_reg bt1120_en;
	struct vop_reg dclk_pol;
	struct vop_reg pin_pol;
	struct vop_reg rgb_dclk_pol;
	struct vop_reg rgb_pin_pol;
	struct vop_reg lvds_dclk_pol;
	struct vop_reg lvds_pin_pol;
	struct vop_reg hdmi_dclk_pol;
	struct vop_reg hdmi_pin_pol;
	struct vop_reg edp_dclk_pol;
	struct vop_reg edp_pin_pol;
	struct vop_reg mipi_dclk_pol;
	struct vop_reg mipi_pin_pol;
	struct vop_reg dp0_dclk_pol;
	struct vop_reg dp0_pin_pol;
	struct vop_reg dp1_dclk_pol;
	struct vop_reg dp1_pin_pol;

	/* This will be reference by win_phy_id */
	struct vop_reg win_vp_id[16];
	struct vop_reg win_dly[16];

	/* connector mux */
	struct vop_reg rgb_mux;
	struct vop_reg hdmi0_mux;
	struct vop_reg hdmi1_mux;
	struct vop_reg dp0_mux;
	struct vop_reg dp1_mux;
	struct vop_reg edp0_mux;
	struct vop_reg edp1_mux;
	struct vop_reg mipi0_mux;
	struct vop_reg mipi1_mux;
	struct vop_reg lvds0_mux;
	struct vop_reg lvds1_mux;

	struct vop_reg lvds_dual_en;
	struct vop_reg lvds_dual_mode;
	struct vop_reg lvds_dual_channel_swap;

	struct vop_reg dp_dual_en;
	struct vop_reg edp_dual_en;
	struct vop_reg hdmi_dual_en;
	struct vop_reg mipi_dual_en;

	struct vop_reg hdmi0_dclk_div;
	struct vop_reg hdmi0_pixclk_div;
	struct vop_reg edp0_dclk_div;
	struct vop_reg edp0_pixclk_div;

	struct vop_reg hdmi1_dclk_div;
	struct vop_reg hdmi1_pixclk_div;
	struct vop_reg edp1_dclk_div;
	struct vop_reg edp1_pixclk_div;

	struct vop_reg mipi0_pixclk_div;
	struct vop_reg mipi1_pixclk_div;
	struct vop_reg mipi0_ds_mode;
	struct vop_reg mipi1_ds_mode;

	struct vop_reg src_color_ctrl;
	struct vop_reg dst_color_ctrl;
	struct vop_reg src_alpha_ctrl;
	struct vop_reg dst_alpha_ctrl;

	struct vop_reg bt1120_yc_swap;
	struct vop_reg bt656_yc_swap;
	struct vop_reg gamma_port_sel;
	struct vop_reg pd_off_imd;

	struct vop_reg otp_en;
	struct vop_reg reg_done_frm;
	struct vop_reg cfg_done;
};

/**
 * VOP2 data structe
 *
 * @version: VOP IP version
 * @win_size: hardware win number
 */
struct vop2_data {
	uint32_t version;
	uint32_t feature;
	uint8_t nr_dscs;
	uint8_t nr_dsc_ecw;
	uint8_t nr_dsc_buffer_flow;
	uint8_t nr_vps;
	uint8_t nr_mixers;
	uint8_t nr_layers;
	uint8_t nr_axi_intr;
	uint8_t nr_gammas;
	uint8_t nr_conns;
	uint8_t nr_pds;
	uint8_t nr_mem_pgs;
	bool delayed_pd;
	const struct vop_intr *axi_intr;
	const struct vop2_ctrl *ctrl;
	const struct vop2_dsc_data *dsc;
	const struct dsc_error_info *dsc_error_ecw;
	const struct dsc_error_info *dsc_error_buffer_flow;
	const struct vop2_win_data *win;
	const struct vop2_video_port_data *vp;
	const struct vop2_connector_if_data *conn;
	const struct vop2_wb_data *wb;
	const struct vop2_layer_data *layer;
	const struct vop2_power_domain_data *pd;
	const struct vop2_power_domain_data *mem_pg;
	const struct vop_csc_table *csc_table;
	const struct vop_hdr_table *hdr_table;
	const struct vop_grf_ctrl *sys_grf;
	const struct vop_grf_ctrl *grf;
	const struct vop_grf_ctrl *vo0_grf;
	const struct vop_grf_ctrl *vo1_grf;
	struct vop_rect max_input;
	struct vop_rect max_output;

	unsigned int win_size;
};

#define CVBS_PAL_VDISPLAY		288

/* interrupt define */
#define DSP_HOLD_VALID_INTR		BIT(0)
#define FS_INTR				BIT(1)
#define LINE_FLAG_INTR			BIT(2)
#define BUS_ERROR_INTR			BIT(3)
#define FS_NEW_INTR			BIT(4)
#define ADDR_SAME_INTR			BIT(5)
#define LINE_FLAG1_INTR			BIT(6)
#define WIN0_EMPTY_INTR			BIT(7)
#define WIN1_EMPTY_INTR			BIT(8)
#define WIN2_EMPTY_INTR			BIT(9)
#define WIN3_EMPTY_INTR			BIT(10)
#define HWC_EMPTY_INTR			BIT(11)
#define POST_BUF_EMPTY_INTR		BIT(12)
#define PWM_GEN_INTR			BIT(13)
#define DMA_FINISH_INTR			BIT(14)
#define FS_FIELD_INTR			BIT(15)
#define FE_INTR				BIT(16)
#define WB_UV_FIFO_FULL_INTR		BIT(17)
#define WB_YRGB_FIFO_FULL_INTR		BIT(18)
#define WB_COMPLETE_INTR		BIT(19)

#define INTR_MASK			(DSP_HOLD_VALID_INTR | FS_INTR | \
					 LINE_FLAG_INTR | BUS_ERROR_INTR | \
					 FS_NEW_INTR | LINE_FLAG1_INTR | \
					 WIN0_EMPTY_INTR | WIN1_EMPTY_INTR | \
					 WIN2_EMPTY_INTR | WIN3_EMPTY_INTR | \
					 HWC_EMPTY_INTR | \
					 POST_BUF_EMPTY_INTR | \
					 DMA_FINISH_INTR | FS_FIELD_INTR | \
					 FE_INTR)
#define DSP_HOLD_VALID_INTR_EN(x)	((x) << 4)
#define FS_INTR_EN(x)			((x) << 5)
#define LINE_FLAG_INTR_EN(x)		((x) << 6)
#define BUS_ERROR_INTR_EN(x)		((x) << 7)
#define DSP_HOLD_VALID_INTR_MASK	(1 << 4)
#define FS_INTR_MASK			(1 << 5)
#define LINE_FLAG_INTR_MASK		(1 << 6)
#define BUS_ERROR_INTR_MASK		(1 << 7)

#define INTR_CLR_SHIFT			8
#define DSP_HOLD_VALID_INTR_CLR		(1 << (INTR_CLR_SHIFT + 0))
#define FS_INTR_CLR			(1 << (INTR_CLR_SHIFT + 1))
#define LINE_FLAG_INTR_CLR		(1 << (INTR_CLR_SHIFT + 2))
#define BUS_ERROR_INTR_CLR		(1 << (INTR_CLR_SHIFT + 3))

#define DSP_LINE_NUM(x)			(((x) & 0x1fff) << 12)
#define DSP_LINE_NUM_MASK		(0x1fff << 12)

/* src alpha ctrl define */
#define SRC_FADING_VALUE(x)		(((x) & 0xff) << 24)
#define SRC_GLOBAL_ALPHA(x)		(((x) & 0xff) << 16)
#define SRC_FACTOR_M0(x)		(((x) & 0x7) << 6)
#define SRC_ALPHA_CAL_M0(x)		(((x) & 0x1) << 5)
#define SRC_BLEND_M0(x)			(((x) & 0x3) << 3)
#define SRC_ALPHA_M0(x)			(((x) & 0x1) << 2)
#define SRC_COLOR_M0(x)			(((x) & 0x1) << 1)
#define SRC_ALPHA_EN(x)			(((x) & 0x1) << 0)
/* dst alpha ctrl define */
#define DST_FACTOR_M0(x)		(((x) & 0x7) << 6)

/*
 * display output interface supported by rockchip lcdc
 */
#define ROCKCHIP_OUT_MODE_P888		0
#define ROCKCHIP_OUT_MODE_BT1120	0
#define ROCKCHIP_OUT_MODE_P666		1
#define ROCKCHIP_OUT_MODE_P565		2
#define ROCKCHIP_OUT_MODE_BT656		5
#define ROCKCHIP_OUT_MODE_S888		8
#define ROCKCHIP_OUT_MODE_S888_DUMMY	12
#define ROCKCHIP_OUT_MODE_YUV420	14
/* for use special outface */
#define ROCKCHIP_OUT_MODE_AAAA		15

#define ROCKCHIP_OUT_MODE_TYPE(x)	((x) >> 16)
#define ROCKCHIP_OUT_MODE(x)		((x) & 0xffff)

enum alpha_mode {
	ALPHA_STRAIGHT,
	ALPHA_INVERSE,
};

enum global_blend_mode {
	ALPHA_GLOBAL,
	ALPHA_PER_PIX,
	ALPHA_PER_PIX_GLOBAL,
};

enum alpha_cal_mode {
	ALPHA_SATURATION,
	ALPHA_NO_SATURATION,
};

enum color_mode {
	ALPHA_SRC_PRE_MUL,
	ALPHA_SRC_NO_PRE_MUL,
};

enum factor_mode {
	ALPHA_ZERO,
	ALPHA_ONE,
	ALPHA_SRC,
	ALPHA_SRC_INVERSE,
	ALPHA_SRC_GLOBAL,
	ALPHA_DST_GLOBAL,
};

enum src_factor_mode {
	SRC_FAC_ALPHA_ZERO,
	SRC_FAC_ALPHA_ONE,
	SRC_FAC_ALPHA_DST,
	SRC_FAC_ALPHA_DST_INVERSE,
	SRC_FAC_ALPHA_SRC,
	SRC_FAC_ALPHA_SRC_GLOBAL,
};

enum dst_factor_mode {
	DST_FAC_ALPHA_ZERO,
	DST_FAC_ALPHA_ONE,
	DST_FAC_ALPHA_SRC,
	DST_FAC_ALPHA_SRC_INVERSE,
	DST_FAC_ALPHA_DST,
	DST_FAC_ALPHA_DST_GLOBAL,
};

enum scale_mode {
	SCALE_NONE = 0x0,
	SCALE_UP   = 0x1,
	SCALE_DOWN = 0x2
};

enum lb_mode {
	LB_YUV_3840X5 = 0x0,
	LB_YUV_2560X8 = 0x1,
	LB_RGB_3840X2 = 0x2,
	LB_RGB_2560X4 = 0x3,
	LB_RGB_1920X5 = 0x4,
	LB_RGB_1280X8 = 0x5
};

enum sacle_up_mode {
	SCALE_UP_BIL = 0x0,
	SCALE_UP_BIC = 0x1
};

enum scale_down_mode {
	SCALE_DOWN_BIL = 0x0,
	SCALE_DOWN_AVG = 0x1
};

enum vop2_scale_up_mode {
	VOP2_SCALE_UP_NRST_NBOR,
	VOP2_SCALE_UP_BIL,
	VOP2_SCALE_UP_BIC,
};

enum vop2_scale_down_mode {
	VOP2_SCALE_DOWN_NRST_NBOR,
	VOP2_SCALE_DOWN_BIL,
	VOP2_SCALE_DOWN_AVG,
};

enum dither_down_mode {
	RGB888_TO_RGB565 = 0x0,
	RGB888_TO_RGB666 = 0x1
};

enum dither_down_mode_sel {
	DITHER_DOWN_ALLEGRO = 0x0,
	DITHER_DOWN_FRC = 0x1
};

enum vop_pol {
	HSYNC_POSITIVE = 0,
	VSYNC_POSITIVE = 1,
	DEN_NEGATIVE   = 2,
	DCLK_INVERT    = 3
};


#define FRAC_16_16(mult, div)    (((mult) << 16) / (div))
#define SCL_FT_DEFAULT_FIXPOINT_SHIFT	12
#define SCL_MAX_VSKIPLINES		4
#define MIN_SCL_FT_AFTER_VSKIP		1

static inline uint16_t scl_cal_scale(int src, int dst, int shift)
{
	return ((src * 2 - 3) << (shift - 1)) / (dst - 1);
}

static inline uint16_t scl_cal_scale2(int src, int dst)
{
	return ((src - 1) << 12) / (dst - 1);
}

#define GET_SCL_FT_BILI_DN(src, dst)	scl_cal_scale(src, dst, 12)
#define GET_SCL_FT_BILI_UP(src, dst)	scl_cal_scale(src, dst, 16)
#define GET_SCL_FT_BIC(src, dst)	scl_cal_scale(src, dst, 16)

static inline uint16_t scl_get_bili_dn_vskip(int src_h, int dst_h,
					     int vskiplines)
{
	int act_height;

	act_height = (src_h + vskiplines - 1) / vskiplines;

	if (act_height == dst_h)
		return GET_SCL_FT_BILI_DN(src_h, dst_h) / vskiplines;

	return GET_SCL_FT_BILI_DN(act_height, dst_h);
}

static inline enum scale_mode scl_get_scl_mode(int src, int dst)
{
	if (src < dst)
		return SCALE_UP;
	else if (src > dst)
		return SCALE_DOWN;

	return SCALE_NONE;
}

static inline int scl_get_vskiplines(uint32_t srch, uint32_t dsth)
{
	uint32_t vskiplines;

	for (vskiplines = SCL_MAX_VSKIPLINES; vskiplines > 1; vskiplines /= 2)
		if (srch >= vskiplines * dsth * MIN_SCL_FT_AFTER_VSKIP)
			break;

	return vskiplines;
}

static inline int scl_vop_cal_lb_mode(int width, bool is_yuv)
{
	int lb_mode;

	if (is_yuv) {
		if (width > 1280)
			lb_mode = LB_YUV_3840X5;
		else
			lb_mode = LB_YUV_2560X8;
	} else {
		if (width > 2560)
			lb_mode = LB_RGB_3840X2;
		else if (width > 1920)
			lb_mode = LB_RGB_2560X4;
		else
			lb_mode = LB_RGB_1920X5;
	}

	return lb_mode;
}

static inline int us_to_vertical_line(struct drm_display_mode *mode, int us)
{
	return us * mode->clock / mode->htotal / 1000;
}

static inline int interpolate(int x1, int y1, int x2, int y2, int x)
{
	return y1 + (y2 - y1) * (x - x1) / (x2 - x1);
}

extern const struct component_ops vop_component_ops;
extern const struct component_ops vop2_component_ops;
#endif /* _ROCKCHIP_DRM_VOP_H */
