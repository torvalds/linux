/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Holmes Chiou <holmes.chiou@mediatek.com>
 *         Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MTK_IMG_IPI_H__
#define __MTK_IMG_IPI_H__

#include <linux/types.h>

/*
 * ISP-MDP generic input information
 * MD5 of the target SCP blob:
 *     6da52bdcf4bf76a0983b313e1d4745d6
 */

#define IMG_MAX_HW_INPUTS	3

#define IMG_MAX_HW_OUTPUTS	4

#define IMG_MAX_PLANES		3

#define IMG_IPI_INIT    1
#define IMG_IPI_DEINIT  2
#define IMG_IPI_FRAME   3
#define IMG_IPI_DEBUG   4

struct img_timeval {
	u32 tv_sec;
	u32 tv_usec;
} __packed;

struct img_addr {
	u64 va; /* Used for Linux OS access */
	u32 pa; /* Used for CM4 access */
	u32 iova; /* Used for IOMMU HW access */
} __packed;

struct tuning_addr {
	u64	present;
	u32	pa;	/* Used for CM4 access */
	u32	iova;	/* Used for IOMMU HW access */
} __packed;

struct img_sw_addr {
	u64 va; /* Used for APMCU access */
	u32 pa; /* Used for CM4 access */
} __packed;

struct img_plane_format {
	u32 size;
	u16 stride;
} __packed;

struct img_pix_format {
	u16 width;
	u16 height;
	u32 colorformat; /* enum mdp_color */
	u16 ycbcr_prof; /* enum mdp_ycbcr_profile */
	struct img_plane_format plane_fmt[IMG_MAX_PLANES];
} __packed;

struct img_image_buffer {
	struct img_pix_format format;
	u32 iova[IMG_MAX_PLANES];
	/* enum mdp_buffer_usage, FD or advanced ISP usages */
	u32 usage;
} __packed;

#define IMG_SUBPIXEL_SHIFT	20

struct img_crop {
	s16 left;
	s16 top;
	u16 width;
	u16 height;
	u32 left_subpix;
	u32 top_subpix;
	u32 width_subpix;
	u32 height_subpix;
} __packed;

#define IMG_CTRL_FLAG_HFLIP	BIT(0)
#define IMG_CTRL_FLAG_DITHER	BIT(1)
#define IMG_CTRL_FLAG_SHARPNESS	BIT(4)
#define IMG_CTRL_FLAG_HDR	BIT(5)
#define IMG_CTRL_FLAG_DRE	BIT(6)

struct img_input {
	struct img_image_buffer buffer;
	u16 flags; /* HDR, DRE, dither */
} __packed;

struct img_output {
	struct img_image_buffer buffer;
	struct img_crop crop;
	s16 rotation;
	u16 flags; /* H-flip, sharpness, dither */
} __packed;

struct img_ipi_frameparam {
	u32 index;
	u32 frame_no;
	struct img_timeval timestamp;
	u8 type; /* enum mdp_stream_type */
	u8 state;
	u8 num_inputs;
	u8 num_outputs;
	u64 drv_data;
	struct img_input inputs[IMG_MAX_HW_INPUTS];
	struct img_output outputs[IMG_MAX_HW_OUTPUTS];
	struct tuning_addr tuning_data;
	struct img_addr subfrm_data;
	struct img_sw_addr config_data;
	struct img_sw_addr self_data;
} __packed;

struct img_sw_buffer {
	u64	handle;		/* Used for APMCU access */
	u32	scp_addr;	/* Used for CM4 access */
} __packed;

struct img_ipi_param {
	u8 usage;
	struct img_sw_buffer frm_param;
} __packed;

struct img_frameparam {
	struct list_head list_entry;
	struct img_ipi_frameparam frameparam;
};

/* ISP-MDP generic output information */

struct img_comp_frame {
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

struct img_region {
	s16 left;
	s16 right;
	s16 top;
	s16 bottom;
} __packed;

struct img_offset {
	s16 left;
	s16 top;
	u32 left_subpix;
	u32 top_subpix;
} __packed;

struct img_comp_subfrm {
	u32 tile_disable:1;
	struct img_region in;
	struct img_region out;
	struct img_offset luma;
	struct img_offset chroma;
	s16 out_vertical; /* Output vertical index */
	s16 out_horizontal; /* Output horizontal index */
} __packed;

#define IMG_MAX_SUBFRAMES	14

struct mdp_rdma_subfrm {
	u32 offset[IMG_MAX_PLANES];
	u32 offset_0_p;
	u32 src;
	u32 clip;
	u32 clip_ofst;
} __packed;

struct mdp_rdma_data {
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
	struct mdp_rdma_subfrm subfrms[IMG_MAX_SUBFRAMES];
} __packed;

struct mdp_rsz_subfrm {
	u32 control2;
	u32 src;
	u32 clip;
} __packed;

struct mdp_rsz_data {
	u32 coeff_step_x;
	u32 coeff_step_y;
	u32 control1;
	u32 control2;
	struct mdp_rsz_subfrm subfrms[IMG_MAX_SUBFRAMES];
} __packed;

struct mdp_wrot_subfrm {
	u32 offset[IMG_MAX_PLANES];
	u32 src;
	u32 clip;
	u32 clip_ofst;
	u32 main_buf;
} __packed;

struct mdp_wrot_data {
	u32 iova[IMG_MAX_PLANES];
	u32 control;
	u32 stride[IMG_MAX_PLANES];
	u32 mat_ctrl;
	u32 fifo_test;
	u32 filter;
	struct mdp_wrot_subfrm subfrms[IMG_MAX_SUBFRAMES];
} __packed;

struct mdp_wdma_subfrm {
	u32 offset[IMG_MAX_PLANES];
	u32 src;
	u32 clip;
	u32 clip_ofst;
} __packed;

struct mdp_wdma_data {
	u32 wdma_cfg;
	u32 iova[IMG_MAX_PLANES];
	u32 w_in_byte;
	u32 uv_stride;
	struct mdp_wdma_subfrm subfrms[IMG_MAX_SUBFRAMES];
} __packed;

struct isp_data {
	u64 dl_flags; /* 1 << (enum mdp_comp_type) */
	u32 smxi_iova[4];
	u32 cq_idx;
	u32 cq_iova;
	u32 tpipe_iova[IMG_MAX_SUBFRAMES];
} __packed;

struct img_compparam {
	u16 type; /* enum mdp_comp_type */
	u16 id; /* enum mtk_mdp_comp_id */
	u32 input;
	u32 outputs[IMG_MAX_HW_OUTPUTS];
	u32 num_outputs;
	struct img_comp_frame frame;
	struct img_comp_subfrm subfrms[IMG_MAX_SUBFRAMES];
	u32 num_subfrms;
	union {
		struct mdp_rdma_data rdma;
		struct mdp_rsz_data rsz;
		struct mdp_wrot_data wrot;
		struct mdp_wdma_data wdma;
		struct isp_data isp;
	};
} __packed;

#define IMG_MAX_COMPONENTS	20

struct img_mux {
	u32 reg;
	u32 value;
	u32 subsys_id;
};

struct img_mmsys_ctrl {
	struct img_mux sets[IMG_MAX_COMPONENTS * 2];
	u32 num_sets;
};

struct img_config {
	struct img_compparam components[IMG_MAX_COMPONENTS];
	u32 num_components;
	struct img_mmsys_ctrl ctrls[IMG_MAX_SUBFRAMES];
	u32 num_subfrms;
} __packed;

#endif  /* __MTK_IMG_IPI_H__ */
