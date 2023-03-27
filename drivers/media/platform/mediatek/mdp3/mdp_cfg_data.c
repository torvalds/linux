// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#include "mtk-mdp3-core.h"

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

const struct mtk_mdp_driver_data mt8183_mdp_driver_data = {
	.mdp_probe_infra = mt8183_mdp_probe_infra,
	.mdp_cfg = &mt8183_plat_cfg,
	.mdp_mutex_table_idx = mt8183_mutex_idx,
};
