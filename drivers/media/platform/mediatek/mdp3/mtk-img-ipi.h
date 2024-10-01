/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Holmes Chiou <holmes.chiou@mediatek.com>
 *         Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MTK_IMG_IPI_H__
#define __MTK_IMG_IPI_H__

#include <linux/err.h>
#include "mdp_sm_mt8183.h"
#include "mdp_sm_mt8195.h"
#include "mtk-mdp3-type.h"

/* ISP-MDP generic input information */

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
	u32 stride;
} __packed;

struct img_pix_format {
	u32 width;
	u32 height;
	u32 colorformat; /* enum mdp_color */
	u32 ycbcr_prof; /* enum mdp_ycbcr_profile */
	struct img_plane_format plane_fmt[IMG_MAX_PLANES];
} __packed;

struct img_image_buffer {
	struct img_pix_format format;
	u32 iova[IMG_MAX_PLANES];
	/* enum mdp_buffer_usage, FD or advanced ISP usages */
	u32 usage;
} __packed;

#define IMG_SUBPIXEL_SHIFT	20

#define IMG_CTRL_FLAG_HFLIP	BIT(0)
#define IMG_CTRL_FLAG_DITHER	BIT(1)
#define IMG_CTRL_FLAG_SHARPNESS	BIT(4)
#define IMG_CTRL_FLAG_HDR	BIT(5)
#define IMG_CTRL_FLAG_DRE	BIT(6)

struct img_input {
	struct img_image_buffer buffer;
	u32 flags; /* HDR, DRE, dither */
} __packed;

struct img_output {
	struct img_image_buffer buffer;
	struct img_crop crop;
	s32 rotation;
	u32 flags; /* H-flip, sharpness, dither */
} __packed;

struct img_ipi_frameparam {
	u32 index;
	u32 frame_no;
	struct img_timeval timestamp;
	u32 type; /* enum mdp_stream_type */
	u32 state;
	u32 num_inputs;
	u32 num_outputs;
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
	u32 usage;
	struct img_sw_buffer frm_param;
} __packed;

struct img_frameparam {
	struct list_head list_entry;
	struct img_ipi_frameparam frameparam;
} __packed;

/* Platform config indicator */
#define MT8183 8183
#define MT8188 8195
#define MT8195 8195

#define CFG_CHECK(plat, p_id) ((plat) == (p_id))

#define _CFG_OFST(plat, cfg, ofst) ((void *)(&((cfg)->config_##plat) + (ofst)))
#define CFG_OFST(plat, cfg, ofst) \
	(IS_ERR_OR_NULL(cfg) ? NULL : _CFG_OFST(plat, cfg, ofst))

#define _CFG_ADDR(plat, cfg, mem) (&((cfg)->config_##plat.mem))
#define CFG_ADDR(plat, cfg, mem) \
	(IS_ERR_OR_NULL(cfg) ? NULL : _CFG_ADDR(plat, cfg, mem))

#define _CFG_GET(plat, cfg, mem) ((cfg)->config_##plat.mem)
#define CFG_GET(plat, cfg, mem) \
	(IS_ERR_OR_NULL(cfg) ? 0 : _CFG_GET(plat, cfg, mem))

#define _CFG_COMP(plat, comp, mem) ((comp)->comp_##plat.mem)
#define CFG_COMP(plat, comp, mem) \
	(IS_ERR_OR_NULL(comp) ? 0 : _CFG_COMP(plat, comp, mem))

struct img_config {
	union {
		struct img_config_8183 config_8183;
		struct img_config_8195 config_8195;
	};
} __packed;

struct img_compparam {
	union {
		struct img_compparam_8183 comp_8183;
		struct img_compparam_8195 comp_8195;
	};
} __packed;

#endif  /* __MTK_IMG_IPI_H__ */
