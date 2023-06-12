/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_SM_MT8183_H__
#define __MDP_SM_MT8183_H__

#include "mtk-mdp3-type.h"

/*
 * ISP-MDP generic output information
 * MD5 of the target SCP prebuild:
 *     2d995ddb5c3b0cf26e96d6a823481886
 */

#define IMG_MAX_SUBFRAMES_8183      14

struct img_comp_frame_8183 {
	u32 output_disable:1;
	u32 bypass:1;
	u16 in_width;
	u16 in_height;
	u16 out_width;
	u16 out_height;
	struct img_crop crop;
	u16 in_total_width;
	u16 out_total_width;
} __packed;

struct img_comp_subfrm_8183 {
	u32 tile_disable:1;
	struct img_region in;
	struct img_region out;
	struct img_offset luma;
	struct img_offset chroma;
	s16 out_vertical; /* Output vertical index */
	s16 out_horizontal; /* Output horizontal index */
} __packed;

struct mdp_rdma_subfrm_8183 {
	u32 offset[IMG_MAX_PLANES];
	u32 offset_0_p;
	u32 src;
	u32 clip;
	u32 clip_ofst;
} __packed;

struct mdp_rdma_data_8183 {
	u32 src_ctrl;
	u32 control;
	u32 iova[IMG_MAX_PLANES];
	u32 iova_end[IMG_MAX_PLANES];
	u32 mf_bkgd;
	u32 mf_bkgd_in_pxl;
	u32 sf_bkgd;
	u32 ufo_dec_y;
	u32 ufo_dec_c;
	u32 transform;
	struct mdp_rdma_subfrm_8183 subfrms[IMG_MAX_SUBFRAMES_8183];
} __packed;

struct mdp_rsz_subfrm_8183 {
	u32 control2;
	u32 src;
	u32 clip;
} __packed;

struct mdp_rsz_data_8183 {
	u32 coeff_step_x;
	u32 coeff_step_y;
	u32 control1;
	u32 control2;
	struct mdp_rsz_subfrm_8183 subfrms[IMG_MAX_SUBFRAMES_8183];
} __packed;

struct mdp_wrot_subfrm_8183 {
	u32 offset[IMG_MAX_PLANES];
	u32 src;
	u32 clip;
	u32 clip_ofst;
	u32 main_buf;
} __packed;

struct mdp_wrot_data_8183 {
	u32 iova[IMG_MAX_PLANES];
	u32 control;
	u32 stride[IMG_MAX_PLANES];
	u32 mat_ctrl;
	u32 fifo_test;
	u32 filter;
	struct mdp_wrot_subfrm_8183 subfrms[IMG_MAX_SUBFRAMES_8183];
} __packed;

struct mdp_wdma_subfrm_8183 {
	u32 offset[IMG_MAX_PLANES];
	u32 src;
	u32 clip;
	u32 clip_ofst;
} __packed;

struct mdp_wdma_data_8183 {
	u32 wdma_cfg;
	u32 iova[IMG_MAX_PLANES];
	u32 w_in_byte;
	u32 uv_stride;
	struct mdp_wdma_subfrm_8183 subfrms[IMG_MAX_SUBFRAMES_8183];
} __packed;

struct isp_data_8183 {
	u64 dl_flags; /* 1 << (enum mdp_comp_type) */
	u32 smxi_iova[4];
	u32 cq_idx;
	u32 cq_iova;
	u32 tpipe_iova[IMG_MAX_SUBFRAMES_8183];
} __packed;

struct img_compparam_8183 {
	u16 type; /* enum mdp_comp_id */
	u16 id; /* engine alias_id */
	u32 input;
	u32 outputs[IMG_MAX_HW_OUTPUTS];
	u32 num_outputs;
	struct img_comp_frame_8183 frame;
	struct img_comp_subfrm_8183 subfrms[IMG_MAX_SUBFRAMES_8183];
	u32 num_subfrms;
	union {
		struct mdp_rdma_data_8183 rdma;
		struct mdp_rsz_data_8183 rsz;
		struct mdp_wrot_data_8183 wrot;
		struct mdp_wdma_data_8183 wdma;
		struct isp_data_8183 isp;
	};
} __packed;

struct img_config_8183 {
	struct img_compparam_8183 components[IMG_MAX_COMPONENTS];
	u32 num_components;
	struct img_mmsys_ctrl ctrls[IMG_MAX_SUBFRAMES_8183];
	u32 num_subfrms;
} __packed;

#endif  /* __MDP_SM_MT8183_H__ */
