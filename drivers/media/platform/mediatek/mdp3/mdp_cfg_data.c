// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#include "mtk-img-ipi.h"
#include "mtk-mdp3-cfg.h"
#include "mtk-mdp3-core.h"
#include "mtk-mdp3-comp.h"
#include "mtk-mdp3-regs.h"

enum mt8183_mdp_comp_id {
	/* ISP */
	MT8183_MDP_COMP_WPEI = 0,
	MT8183_MDP_COMP_WPEO,           /* 1 */
	MT8183_MDP_COMP_WPEI2,          /* 2 */
	MT8183_MDP_COMP_WPEO2,          /* 3 */
	MT8183_MDP_COMP_ISP_IMGI,       /* 4 */
	MT8183_MDP_COMP_ISP_IMGO,       /* 5 */
	MT8183_MDP_COMP_ISP_IMG2O,      /* 6 */

	/* IPU */
	MT8183_MDP_COMP_IPUI,           /* 7 */
	MT8183_MDP_COMP_IPUO,           /* 8 */

	/* MDP */
	MT8183_MDP_COMP_CAMIN,          /* 9 */
	MT8183_MDP_COMP_CAMIN2,         /* 10 */
	MT8183_MDP_COMP_RDMA0,          /* 11 */
	MT8183_MDP_COMP_AAL0,           /* 12 */
	MT8183_MDP_COMP_CCORR0,         /* 13 */
	MT8183_MDP_COMP_RSZ0,           /* 14 */
	MT8183_MDP_COMP_RSZ1,           /* 15 */
	MT8183_MDP_COMP_TDSHP0,         /* 16 */
	MT8183_MDP_COMP_COLOR0,         /* 17 */
	MT8183_MDP_COMP_PATH0_SOUT,     /* 18 */
	MT8183_MDP_COMP_PATH1_SOUT,     /* 19 */
	MT8183_MDP_COMP_WROT0,          /* 20 */
	MT8183_MDP_COMP_WDMA,           /* 21 */

	/* Dummy Engine */
	MT8183_MDP_COMP_RDMA1,          /* 22 */
	MT8183_MDP_COMP_RSZ2,           /* 23 */
	MT8183_MDP_COMP_TDSHP1,         /* 24 */
	MT8183_MDP_COMP_WROT1,          /* 25 */
};

static const struct of_device_id mt8183_mdp_probe_infra[MDP_INFRA_MAX] = {
	[MDP_INFRA_MMSYS] = { .compatible = "mediatek,mt8183-mmsys" },
	[MDP_INFRA_MUTEX] = { .compatible = "mediatek,mt8183-disp-mutex" },
	[MDP_INFRA_SCP] = { .compatible = "mediatek,mt8183-scp" }
};

static const struct mdp_platform_config mt8183_plat_cfg = {
	.rdma_support_10bit		= true,
	.rdma_rsz1_sram_sharing		= true,
	.rdma_upsample_repeat_only	= true,
	.rsz_disable_dcm_small_sample	= false,
	.wrot_filter_constraint		= false,
};

static const u32 mt8183_mutex_idx[MDP_MAX_COMP_COUNT] = {
	[MDP_COMP_RDMA0] = MUTEX_MOD_IDX_MDP_RDMA0,
	[MDP_COMP_RSZ0] = MUTEX_MOD_IDX_MDP_RSZ0,
	[MDP_COMP_RSZ1] = MUTEX_MOD_IDX_MDP_RSZ1,
	[MDP_COMP_TDSHP0] = MUTEX_MOD_IDX_MDP_TDSHP0,
	[MDP_COMP_WROT0] = MUTEX_MOD_IDX_MDP_WROT0,
	[MDP_COMP_WDMA] = MUTEX_MOD_IDX_MDP_WDMA,
	[MDP_COMP_AAL0] = MUTEX_MOD_IDX_MDP_AAL0,
	[MDP_COMP_CCORR0] = MUTEX_MOD_IDX_MDP_CCORR0,
};

static const struct mdp_comp_data mt8183_mdp_comp_data[MDP_MAX_COMP_COUNT] = {
	[MDP_COMP_WPEI] = {
		{MDP_COMP_TYPE_WPEI, 0, MT8183_MDP_COMP_WPEI},
		{0, 0, 0}
	},
	[MDP_COMP_WPEO] = {
		{MDP_COMP_TYPE_EXTO, 2, MT8183_MDP_COMP_WPEO},
		{0, 0, 0}
	},
	[MDP_COMP_WPEI2] = {
		{MDP_COMP_TYPE_WPEI, 1, MT8183_MDP_COMP_WPEI2},
		{0, 0, 0}
	},
	[MDP_COMP_WPEO2] = {
		{MDP_COMP_TYPE_EXTO, 3, MT8183_MDP_COMP_WPEO2},
		{0, 0, 0}
	},
	[MDP_COMP_ISP_IMGI] = {
		{MDP_COMP_TYPE_IMGI, 0, MT8183_MDP_COMP_ISP_IMGI},
		{0, 0, 4}
	},
	[MDP_COMP_ISP_IMGO] = {
		{MDP_COMP_TYPE_EXTO, 0, MT8183_MDP_COMP_ISP_IMGO},
		{0, 0, 4}
	},
	[MDP_COMP_ISP_IMG2O] = {
		{MDP_COMP_TYPE_EXTO, 1, MT8183_MDP_COMP_ISP_IMG2O},
		{0, 0, 0}
	},
	[MDP_COMP_CAMIN] = {
		{MDP_COMP_TYPE_DL_PATH, 0, MT8183_MDP_COMP_CAMIN},
		{2, 2, 1}
	},
	[MDP_COMP_CAMIN2] = {
		{MDP_COMP_TYPE_DL_PATH, 1, MT8183_MDP_COMP_CAMIN2},
		{2, 4, 1}
	},
	[MDP_COMP_RDMA0] = {
		{MDP_COMP_TYPE_RDMA, 0, MT8183_MDP_COMP_RDMA0},
		{2, 0, 0}
	},
	[MDP_COMP_CCORR0] = {
		{MDP_COMP_TYPE_CCORR, 0, MT8183_MDP_COMP_CCORR0},
		{1, 0, 0}
	},
	[MDP_COMP_RSZ0] = {
		{MDP_COMP_TYPE_RSZ, 0, MT8183_MDP_COMP_RSZ0},
		{1, 0, 0}
	},
	[MDP_COMP_RSZ1] = {
		{MDP_COMP_TYPE_RSZ, 1, MT8183_MDP_COMP_RSZ1},
		{1, 0, 0}
	},
	[MDP_COMP_TDSHP0] = {
		{MDP_COMP_TYPE_TDSHP, 0, MT8183_MDP_COMP_TDSHP0},
		{0, 0, 0}
	},
	[MDP_COMP_PATH0_SOUT] = {
		{MDP_COMP_TYPE_PATH, 0, MT8183_MDP_COMP_PATH0_SOUT},
		{0, 0, 0}
	},
	[MDP_COMP_PATH1_SOUT] = {
		{MDP_COMP_TYPE_PATH, 1, MT8183_MDP_COMP_PATH1_SOUT},
		{0, 0, 0}
	},
	[MDP_COMP_WROT0] = {
		{MDP_COMP_TYPE_WROT, 0, MT8183_MDP_COMP_WROT0},
		{1, 0, 0}
	},
	[MDP_COMP_WDMA] = {
		{MDP_COMP_TYPE_WDMA, 0, MT8183_MDP_COMP_WDMA},
		{1, 0, 0}
	},
};

static const struct of_device_id mt8183_sub_comp_dt_ids[] = {
	{
		.compatible = "mediatek,mt8183-mdp3-wdma",
		.data = (void *)MDP_COMP_TYPE_PATH,
	}, {
		.compatible = "mediatek,mt8183-mdp3-wrot",
		.data = (void *)MDP_COMP_TYPE_PATH,
	},
	{}
};

/*
 * All 10-bit related formats are not added in the basic format list,
 * please add the corresponding format settings before use.
 */
static const struct mdp_format mt8183_formats[] = {
	{
		.pixelformat	= V4L2_PIX_FMT_GREY,
		.mdp_color	= MDP_COLOR_GREY,
		.depth		= { 8 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_RGB565X,
		.mdp_color	= MDP_COLOR_BGR565,
		.depth		= { 16 },
		.row_depth	= { 16 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_RGB565,
		.mdp_color	= MDP_COLOR_RGB565,
		.depth		= { 16 },
		.row_depth	= { 16 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_RGB24,
		.mdp_color	= MDP_COLOR_RGB888,
		.depth		= { 24 },
		.row_depth	= { 24 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_BGR24,
		.mdp_color	= MDP_COLOR_BGR888,
		.depth		= { 24 },
		.row_depth	= { 24 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_ABGR32,
		.mdp_color	= MDP_COLOR_BGRA8888,
		.depth		= { 32 },
		.row_depth	= { 32 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_ARGB32,
		.mdp_color	= MDP_COLOR_ARGB8888,
		.depth		= { 32 },
		.row_depth	= { 32 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.mdp_color	= MDP_COLOR_UYVY,
		.depth		= { 16 },
		.row_depth	= { 16 },
		.num_planes	= 1,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_VYUY,
		.mdp_color	= MDP_COLOR_VYUY,
		.depth		= { 16 },
		.row_depth	= { 16 },
		.num_planes	= 1,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.mdp_color	= MDP_COLOR_YUYV,
		.depth		= { 16 },
		.row_depth	= { 16 },
		.num_planes	= 1,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YVYU,
		.mdp_color	= MDP_COLOR_YVYU,
		.depth		= { 16 },
		.row_depth	= { 16 },
		.num_planes	= 1,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YUV420,
		.mdp_color	= MDP_COLOR_I420,
		.depth		= { 12 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YVU420,
		.mdp_color	= MDP_COLOR_YV12,
		.depth		= { 12 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV12,
		.mdp_color	= MDP_COLOR_NV12,
		.depth		= { 12 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV21,
		.mdp_color	= MDP_COLOR_NV21,
		.depth		= { 12 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV16,
		.mdp_color	= MDP_COLOR_NV16,
		.depth		= { 16 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV61,
		.mdp_color	= MDP_COLOR_NV61,
		.depth		= { 16 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV24,
		.mdp_color	= MDP_COLOR_NV24,
		.depth		= { 24 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV42,
		.mdp_color	= MDP_COLOR_NV42,
		.depth		= { 24 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_MT21C,
		.mdp_color	= MDP_COLOR_420_BLK_UFO,
		.depth		= { 8, 4 },
		.row_depth	= { 8, 8 },
		.num_planes	= 2,
		.walign		= 4,
		.halign		= 5,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_MM21,
		.mdp_color	= MDP_COLOR_420_BLK,
		.depth		= { 8, 4 },
		.row_depth	= { 8, 8 },
		.num_planes	= 2,
		.walign		= 4,
		.halign		= 5,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV12M,
		.mdp_color	= MDP_COLOR_NV12,
		.depth		= { 8, 4 },
		.row_depth	= { 8, 8 },
		.num_planes	= 2,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV21M,
		.mdp_color	= MDP_COLOR_NV21,
		.depth		= { 8, 4 },
		.row_depth	= { 8, 8 },
		.num_planes	= 2,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV16M,
		.mdp_color	= MDP_COLOR_NV16,
		.depth		= { 8, 8 },
		.row_depth	= { 8, 8 },
		.num_planes	= 2,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV61M,
		.mdp_color	= MDP_COLOR_NV61,
		.depth		= { 8, 8 },
		.row_depth	= { 8, 8 },
		.num_planes	= 2,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YUV420M,
		.mdp_color	= MDP_COLOR_I420,
		.depth		= { 8, 2, 2 },
		.row_depth	= { 8, 4, 4 },
		.num_planes	= 3,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YVU420M,
		.mdp_color	= MDP_COLOR_YV12,
		.depth		= { 8, 2, 2 },
		.row_depth	= { 8, 4, 4 },
		.num_planes	= 3,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}
};

static const struct mdp_limit mt8183_mdp_def_limit = {
	.out_limit = {
		.wmin	= 16,
		.hmin	= 16,
		.wmax	= 8176,
		.hmax	= 8176,
	},
	.cap_limit = {
		.wmin	= 2,
		.hmin	= 2,
		.wmax	= 8176,
		.hmax	= 8176,
	},
	.h_scale_up_max = 32,
	.v_scale_up_max = 32,
	.h_scale_down_max = 20,
	.v_scale_down_max = 128,
};

static const struct mdp_pipe_info mt8183_pipe_info[] = {
	[MDP_PIPE_WPEI] = {MDP_PIPE_WPEI, 0},
	[MDP_PIPE_WPEI2] = {MDP_PIPE_WPEI2, 1},
	[MDP_PIPE_IMGI] = {MDP_PIPE_IMGI, 2},
	[MDP_PIPE_RDMA0] = {MDP_PIPE_RDMA0, 3}
};

const struct mtk_mdp_driver_data mt8183_mdp_driver_data = {
	.mdp_plat_id = MT8183,
	.mdp_probe_infra = mt8183_mdp_probe_infra,
	.mdp_cfg = &mt8183_plat_cfg,
	.mdp_mutex_table_idx = mt8183_mutex_idx,
	.comp_data = mt8183_mdp_comp_data,
	.comp_data_len = ARRAY_SIZE(mt8183_mdp_comp_data),
	.mdp_sub_comp_dt_ids = mt8183_sub_comp_dt_ids,
	.format = mt8183_formats,
	.format_len = ARRAY_SIZE(mt8183_formats),
	.def_limit = &mt8183_mdp_def_limit,
	.pipe_info = mt8183_pipe_info,
	.pipe_info_len = ARRAY_SIZE(mt8183_pipe_info),
};

s32 mdp_cfg_get_id_inner(struct mdp_dev *mdp_dev, enum mtk_mdp_comp_id id)
{
	if (!mdp_dev)
		return MDP_COMP_NONE;
	if (id <= MDP_COMP_NONE || id >= MDP_MAX_COMP_COUNT)
		return MDP_COMP_NONE;

	return mdp_dev->mdp_data->comp_data[id].match.inner_id;
}

enum mtk_mdp_comp_id mdp_cfg_get_id_public(struct mdp_dev *mdp_dev, s32 inner_id)
{
	enum mtk_mdp_comp_id public_id = MDP_COMP_NONE;
	u32 i;

	if (IS_ERR(mdp_dev) || !inner_id)
		goto err_public_id;

	for (i = 0; i < MDP_MAX_COMP_COUNT; i++) {
		if (mdp_dev->mdp_data->comp_data[i].match.inner_id == inner_id) {
			public_id = i;
			return public_id;
		}
	}

err_public_id:
	return public_id;
}
