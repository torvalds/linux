/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_SM_MT8195_H__
#define __MDP_SM_MT8195_H__

#include "mtk-mdp3-type.h"

/*
 * ISP-MDP generic output information
 * MD5 of the target SCP prebuild:
 *     a49ec487e458b5971880f1b63dc2a9d5
 */

#define IMG_MAX_SUBFRAMES_8195	20

struct img_comp_frame_8195 {
	u32 output_disable;
	u32 bypass;
	u32 in_width;
	u32 in_height;
	u32 out_width;
	u32 out_height;
	struct img_crop crop;
	u32 in_total_width;
	u32 out_total_width;
} __packed;

struct img_comp_subfrm_8195 {
	u32 tile_disable;
	struct img_region in;
	struct img_region out;
	struct img_offset luma;
	struct img_offset chroma;
	s32 out_vertical; /* Output vertical index */
	s32 out_horizontal; /* Output horizontal index */
} __packed;

struct mdp_rdma_subfrm_8195 {
	u32 offset[IMG_MAX_PLANES];
	u32 offset_0_p;
	u32 src;
	u32 clip;
	u32 clip_ofst;
	u32 in_tile_xleft;
	u32 in_tile_ytop;
} __packed;

struct mdp_rdma_data_8195 {
	u32 src_ctrl;
	u32 comp_ctrl;
	u32 control;
	u32 iova[IMG_MAX_PLANES];
	u32 iova_end[IMG_MAX_PLANES];
	u32 mf_bkgd;
	u32 mf_bkgd_in_pxl;
	u32 sf_bkgd;
	u32 ufo_dec_y;
	u32 ufo_dec_c;
	u32 transform;
	u32 dmabuf_con0;
	u32 ultra_th_high_con0;
	u32 ultra_th_low_con0;
	u32 dmabuf_con1;
	u32 ultra_th_high_con1;
	u32 ultra_th_low_con1;
	u32 dmabuf_con2;
	u32 ultra_th_high_con2;
	u32 ultra_th_low_con2;
	u32 dmabuf_con3;
	struct mdp_rdma_subfrm_8195 subfrms[IMG_MAX_SUBFRAMES_8195];
} __packed;

struct mdp_fg_subfrm_8195 {
	u32 info_0;
	u32 info_1;
} __packed;

struct mdp_fg_data_8195 {
	u32 ctrl_0;
	u32 ck_en;
	struct mdp_fg_subfrm_8195 subfrms[IMG_MAX_SUBFRAMES_8195];
} __packed;

struct mdp_hdr_subfrm_8195 {
	u32 win_size;
	u32 src;
	u32 clip_ofst0;
	u32 clip_ofst1;
	u32 hist_ctrl_0;
	u32 hist_ctrl_1;
	u32 hdr_top;
	u32 hist_addr;
} __packed;

struct mdp_hdr_data_8195 {
	u32 top;
	u32 relay;
	struct mdp_hdr_subfrm_8195   subfrms[IMG_MAX_SUBFRAMES_8195];
} __packed;

struct mdp_aal_subfrm_8195 {
	u32 src;
	u32 clip;
	u32 clip_ofst;
} __packed;

struct mdp_aal_data_8195 {
	u32 cfg_main;
	u32 cfg;
	struct mdp_aal_subfrm_8195   subfrms[IMG_MAX_SUBFRAMES_8195];
} __packed;

struct mdp_rsz_subfrm_8195 {
	u32 control2;
	u32 src;
	u32 clip;
	u32 hdmirx_en;
	u32 luma_h_int_ofst;
	u32 luma_h_sub_ofst;
	u32 luma_v_int_ofst;
	u32 luma_v_sub_ofst;
	u32 chroma_h_int_ofst;
	u32 chroma_h_sub_ofst;
	u32 rsz_switch;
	u32 merge_cfg;
} __packed;

struct mdp_rsz_data_8195 {
	u32 coeff_step_x;
	u32 coeff_step_y;
	u32 control1;
	u32 control2;
	u32 etc_control;
	u32 prz_enable;
	u32 ibse_softclip;
	u32 tap_adapt;
	u32 ibse_gaincontrol1;
	u32 ibse_gaincontrol2;
	u32 ibse_ylevel_1;
	u32 ibse_ylevel_2;
	u32 ibse_ylevel_3;
	u32 ibse_ylevel_4;
	u32 ibse_ylevel_5;
	struct mdp_rsz_subfrm_8195 subfrms[IMG_MAX_SUBFRAMES_8195];
} __packed;

struct mdp_tdshp_subfrm_8195 {
	u32 src;
	u32 clip;
	u32 clip_ofst;
	u32 hist_cfg_0;
	u32 hist_cfg_1;
} __packed;

struct mdp_tdshp_data_8195 {
	u32 cfg;
	struct mdp_tdshp_subfrm_8195 subfrms[IMG_MAX_SUBFRAMES_8195];
} __packed;

struct mdp_color_subfrm_8195 {
	u32 in_hsize;
	u32 in_vsize;
} __packed;

struct mdp_color_data_8195 {
	u32 start;
	struct mdp_color_subfrm_8195 subfrms[IMG_MAX_SUBFRAMES_8195];
} __packed;

struct mdp_ovl_subfrm_8195 {
	u32 L0_src_size;
	u32 roi_size;
} __packed;

struct mdp_ovl_data_8195 {
	u32 L0_con;
	u32 src_con;
	struct mdp_ovl_subfrm_8195 subfrms[IMG_MAX_SUBFRAMES_8195];
} __packed;

struct mdp_pad_subfrm_8195 {
	u32 pic_size;
} __packed;

struct mdp_pad_data_8195 {
	struct mdp_pad_subfrm_8195 subfrms[IMG_MAX_SUBFRAMES_8195];
} __packed;

struct mdp_tcc_subfrm_8195 {
	u32 pic_size;
} __packed;

struct mdp_tcc_data_8195 {
	struct mdp_tcc_subfrm_8195 subfrms[IMG_MAX_SUBFRAMES_8195];
} __packed;

struct mdp_wrot_subfrm_8195 {
	u32 offset[IMG_MAX_PLANES];
	u32 src;
	u32 clip;
	u32 clip_ofst;
	u32 main_buf;
} __packed;

struct mdp_wrot_data_8195 {
	u32 iova[IMG_MAX_PLANES];
	u32 control;
	u32 stride[IMG_MAX_PLANES];
	u32 mat_ctrl;
	u32 fifo_test;
	u32 filter;
	u32 pre_ultra;
	u32 framesize;
	u32 afbc_yuvtrans;
	u32 scan_10bit;
	u32 pending_zero;
	u32 bit_number;
	u32 pvric;
	u32 vpp02vpp1;
	struct mdp_wrot_subfrm_8195 subfrms[IMG_MAX_SUBFRAMES_8195];
} __packed;

struct mdp_wdma_subfrm_8195 {
	u32 offset[IMG_MAX_PLANES];
	u32 src;
	u32 clip;
	u32 clip_ofst;
} __packed;

struct mdp_wdma_data_8195 {
	u32 wdma_cfg;
	u32 iova[IMG_MAX_PLANES];
	u32 w_in_byte;
	u32 uv_stride;
	struct mdp_wdma_subfrm_8195 subfrms[IMG_MAX_SUBFRAMES_8195];
} __packed;

struct isp_data_8195 {
	u64 dl_flags; /* 1 << (enum mdp_comp_type) */
	u32 smxi_iova[4];
	u32 cq_idx;
	u32 cq_iova;
	u32 tpipe_iova[IMG_MAX_SUBFRAMES_8195];
} __packed;

struct img_compparam_8195 {
	u32 type; /* enum mdp_comp_id */
	u32 id; /* engine alias_id */
	u32 input;
	u32 outputs[IMG_MAX_HW_OUTPUTS];
	u32 num_outputs;
	struct img_comp_frame_8195 frame;
	struct img_comp_subfrm_8195 subfrms[IMG_MAX_SUBFRAMES_8195];
	u32 num_subfrms;
	union {
		struct mdp_rdma_data_8195 rdma;
		struct mdp_fg_data_8195 fg;
		struct mdp_hdr_data_8195 hdr;
		struct mdp_aal_data_8195 aal;
		struct mdp_rsz_data_8195 rsz;
		struct mdp_tdshp_data_8195 tdshp;
		struct mdp_color_data_8195 color;
		struct mdp_ovl_data_8195 ovl;
		struct mdp_pad_data_8195 pad;
		struct mdp_tcc_data_8195 tcc;
		struct mdp_wrot_data_8195 wrot;
		struct mdp_wdma_data_8195 wdma;
		struct isp_data_8195 isp;
	};
} __packed;

struct img_config_8195 {
	struct img_compparam_8195 components[IMG_MAX_COMPONENTS];
	u32 num_components;
	struct img_mmsys_ctrl ctrls[IMG_MAX_SUBFRAMES_8195];
	u32 num_subfrms;
} __packed;

#endif  /* __MDP_SM_MT8195_H__ */
