// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013-2014 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Copyright (c) 2014,2017 The Linux Foundation. All rights reserved.
 */

#include "adreno_gpu.h"
#include "a6xx_gpu.h"
#include "a6xx.xml.h"

/* gbif reglist is same as a640: */
static const struct adreno_reglist a640_gbif[] = {
	{ REG_A6XX_GBIF_QSB_SIDE0, 0x00071620 },
	{ REG_A6XX_GBIF_QSB_SIDE1, 0x00071620 },
	{ REG_A6XX_GBIF_QSB_SIDE2, 0x00071620 },
	{ REG_A6XX_GBIF_QSB_SIDE3, 0x00071620 },
	{ },
};

static const struct adreno_reglist a730_hwcg[] = {
	{ REG_A6XX_RBBM_CLOCK_CNTL_SP0, 0x02222222 },
	{ REG_A6XX_RBBM_CLOCK_CNTL2_SP0, 0x02022222 },
	{ REG_A6XX_RBBM_CLOCK_HYST_SP0, 0x0000f3cf },
	{ REG_A6XX_RBBM_CLOCK_DELAY_SP0, 0x00000080 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_TP0, 0x22222220 },
	{ REG_A6XX_RBBM_CLOCK_CNTL2_TP0, 0x22222222 },
	{ REG_A6XX_RBBM_CLOCK_CNTL3_TP0, 0x22220222 },
	{ REG_A6XX_RBBM_CLOCK_CNTL4_TP0, 0x00222222 },
	{ REG_A6XX_RBBM_CLOCK_HYST_TP0, 0x77777777 },
	{ REG_A6XX_RBBM_CLOCK_HYST2_TP0, 0x77777777 },
	{ REG_A6XX_RBBM_CLOCK_HYST3_TP0, 0x77777777 },
	{ REG_A6XX_RBBM_CLOCK_HYST4_TP0, 0x00077777 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_TP0, 0x11111111 },
	{ REG_A6XX_RBBM_CLOCK_DELAY2_TP0, 0x11111111 },
	{ REG_A6XX_RBBM_CLOCK_DELAY3_TP0, 0x11111111 },
	{ REG_A6XX_RBBM_CLOCK_DELAY4_TP0, 0x00011111 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_UCHE, 0x22222222 },
	{ REG_A6XX_RBBM_CLOCK_HYST_UCHE, 0x00000004 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_UCHE, 0x00000002 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_RB0, 0x22222222 },
	{ REG_A6XX_RBBM_CLOCK_CNTL2_RB0, 0x01002222 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_CCU0, 0x00002220 },
	{ REG_A6XX_RBBM_CLOCK_HYST_RB_CCU0, 0x44000f00 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_RAC, 0x25222022 },
	{ REG_A6XX_RBBM_CLOCK_CNTL2_RAC, 0x00555555 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_RAC, 0x00000011 },
	{ REG_A6XX_RBBM_CLOCK_HYST_RAC, 0x00440044 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_TSE_RAS_RBBM, 0x04222222 },
	{ REG_A7XX_RBBM_CLOCK_MODE2_GRAS, 0x00000222 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_GRAS, 0x00222222 },
	{ REG_A6XX_RBBM_CLOCK_MODE_GPC, 0x02222223 },
	{ REG_A6XX_RBBM_CLOCK_MODE_VFD, 0x00002222 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_GPC, 0x00222222 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_VFD, 0x00002222 },
	{ REG_A6XX_RBBM_CLOCK_HYST_TSE_RAS_RBBM, 0x00000000 },
	{ REG_A6XX_RBBM_CLOCK_HYST_GPC, 0x04104004 },
	{ REG_A6XX_RBBM_CLOCK_HYST_VFD, 0x00000000 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_TSE_RAS_RBBM, 0x00004000 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_GPC, 0x00000200 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_VFD, 0x00002222 },
	{ REG_A6XX_RBBM_CLOCK_MODE_HLSQ, 0x00002222 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_HLSQ, 0x00000000 },
	{ REG_A6XX_RBBM_CLOCK_HYST_HLSQ, 0x00000000 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_HLSQ_2, 0x00000002 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_LRZ, 0x55555552 },
	{ REG_A7XX_RBBM_CLOCK_MODE_CP, 0x00000223 },
	{ REG_A6XX_RBBM_CLOCK_CNTL, 0x8aa8aa82 },
	{ REG_A6XX_RBBM_ISDB_CNT, 0x00000182 },
	{ REG_A6XX_RBBM_RAC_THRESHOLD_CNT, 0x00000000 },
	{ REG_A6XX_RBBM_SP_HYST_CNT, 0x00000000 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_GMU_GX, 0x00000222 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_GMU_GX, 0x00000111 },
	{ REG_A6XX_RBBM_CLOCK_HYST_GMU_GX, 0x00000555 },
	{},
};

static const struct adreno_reglist a740_hwcg[] = {
	{ REG_A6XX_RBBM_CLOCK_CNTL_SP0, 0x02222222 },
	{ REG_A6XX_RBBM_CLOCK_CNTL2_SP0, 0x22022222 },
	{ REG_A6XX_RBBM_CLOCK_HYST_SP0, 0x003cf3cf },
	{ REG_A6XX_RBBM_CLOCK_DELAY_SP0, 0x00000080 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_TP0, 0x22222220 },
	{ REG_A6XX_RBBM_CLOCK_CNTL2_TP0, 0x22222222 },
	{ REG_A6XX_RBBM_CLOCK_CNTL3_TP0, 0x22222222 },
	{ REG_A6XX_RBBM_CLOCK_CNTL4_TP0, 0x00222222 },
	{ REG_A6XX_RBBM_CLOCK_HYST_TP0, 0x77777777 },
	{ REG_A6XX_RBBM_CLOCK_HYST2_TP0, 0x77777777 },
	{ REG_A6XX_RBBM_CLOCK_HYST3_TP0, 0x77777777 },
	{ REG_A6XX_RBBM_CLOCK_HYST4_TP0, 0x00077777 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_TP0, 0x11111111 },
	{ REG_A6XX_RBBM_CLOCK_DELAY2_TP0, 0x11111111 },
	{ REG_A6XX_RBBM_CLOCK_DELAY3_TP0, 0x11111111 },
	{ REG_A6XX_RBBM_CLOCK_DELAY4_TP0, 0x00011111 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_UCHE, 0x22222222 },
	{ REG_A6XX_RBBM_CLOCK_CNTL2_UCHE, 0x00222222 },
	{ REG_A6XX_RBBM_CLOCK_HYST_UCHE, 0x00000444 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_UCHE, 0x00000222 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_RB0, 0x22222222 },
	{ REG_A6XX_RBBM_CLOCK_CNTL2_RB0, 0x01002222 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_CCU0, 0x00002220 },
	{ REG_A6XX_RBBM_CLOCK_HYST_RB_CCU0, 0x44000f00 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_RAC, 0x25222022 },
	{ REG_A6XX_RBBM_CLOCK_CNTL2_RAC, 0x00555555 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_RAC, 0x00000011 },
	{ REG_A6XX_RBBM_CLOCK_HYST_RAC, 0x00440044 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_TSE_RAS_RBBM, 0x04222222 },
	{ REG_A7XX_RBBM_CLOCK_MODE2_GRAS, 0x00000222 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_GRAS, 0x00222222 },
	{ REG_A6XX_RBBM_CLOCK_MODE_GPC, 0x02222223 },
	{ REG_A6XX_RBBM_CLOCK_MODE_VFD, 0x00222222 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_GPC, 0x00222222 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_VFD, 0x00002222 },
	{ REG_A6XX_RBBM_CLOCK_HYST_TSE_RAS_RBBM, 0x00000000 },
	{ REG_A6XX_RBBM_CLOCK_HYST_GPC, 0x04104004 },
	{ REG_A6XX_RBBM_CLOCK_HYST_VFD, 0x00000000 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_TSE_RAS_RBBM, 0x00000000 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_GPC, 0x00000200 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_VFD, 0x00000000 },
	{ REG_A6XX_RBBM_CLOCK_MODE_HLSQ, 0x00002222 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_HLSQ, 0x00000000 },
	{ REG_A6XX_RBBM_CLOCK_HYST_HLSQ, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_MODE_BV_LRZ, 0x55555552 },
	{ REG_A7XX_RBBM_CLOCK_HYST2_VFD, 0x00000000 },
	{ REG_A7XX_RBBM_CLOCK_MODE_CP, 0x00000222 },
	{ REG_A6XX_RBBM_CLOCK_CNTL, 0x8aa8aa82 },
	{ REG_A6XX_RBBM_ISDB_CNT, 0x00000182 },
	{ REG_A6XX_RBBM_RAC_THRESHOLD_CNT, 0x00000000 },
	{ REG_A6XX_RBBM_SP_HYST_CNT, 0x00000000 },
	{ REG_A6XX_RBBM_CLOCK_CNTL_GMU_GX, 0x00000222 },
	{ REG_A6XX_RBBM_CLOCK_DELAY_GMU_GX, 0x00000111 },
	{ REG_A6XX_RBBM_CLOCK_HYST_GMU_GX, 0x00000555 },
	{},
};

static const u32 a730_protect_regs[] = {
	A6XX_PROTECT_RDONLY(0x00000, 0x04ff),
	A6XX_PROTECT_RDONLY(0x0050b, 0x0058),
	A6XX_PROTECT_NORDWR(0x0050e, 0x0000),
	A6XX_PROTECT_NORDWR(0x00510, 0x0000),
	A6XX_PROTECT_NORDWR(0x00534, 0x0000),
	A6XX_PROTECT_RDONLY(0x005fb, 0x009d),
	A6XX_PROTECT_NORDWR(0x00699, 0x01e9),
	A6XX_PROTECT_NORDWR(0x008a0, 0x0008),
	A6XX_PROTECT_NORDWR(0x008ab, 0x0024),
	/* 0x008d0-0x008dd and 0x008e0-0x008e6 are unprotected on purpose for tools like perfetto */
	A6XX_PROTECT_NORDWR(0x008de, 0x0001),
	A6XX_PROTECT_RDONLY(0x008e7, 0x014b),
	A6XX_PROTECT_NORDWR(0x00900, 0x004d),
	A6XX_PROTECT_NORDWR(0x0098d, 0x00b2),
	A6XX_PROTECT_NORDWR(0x00a41, 0x01be),
	A6XX_PROTECT_NORDWR(0x00df0, 0x0001),
	A6XX_PROTECT_NORDWR(0x00e01, 0x0000),
	A6XX_PROTECT_NORDWR(0x00e07, 0x0008),
	A6XX_PROTECT_NORDWR(0x03c00, 0x00c3),
	A6XX_PROTECT_RDONLY(0x03cc4, 0x1fff),
	A6XX_PROTECT_NORDWR(0x08630, 0x01cf),
	A6XX_PROTECT_NORDWR(0x08e00, 0x0000),
	A6XX_PROTECT_NORDWR(0x08e08, 0x0000),
	A6XX_PROTECT_NORDWR(0x08e50, 0x001f),
	A6XX_PROTECT_NORDWR(0x08e80, 0x0280),
	A6XX_PROTECT_NORDWR(0x09624, 0x01db),
	A6XX_PROTECT_NORDWR(0x09e40, 0x0000),
	A6XX_PROTECT_NORDWR(0x09e64, 0x000d),
	A6XX_PROTECT_NORDWR(0x09e78, 0x0187),
	A6XX_PROTECT_NORDWR(0x0a630, 0x01cf),
	A6XX_PROTECT_NORDWR(0x0ae02, 0x0000),
	A6XX_PROTECT_NORDWR(0x0ae50, 0x000f),
	A6XX_PROTECT_NORDWR(0x0ae66, 0x0003),
	A6XX_PROTECT_NORDWR(0x0ae6f, 0x0003),
	A6XX_PROTECT_NORDWR(0x0b604, 0x0003),
	A6XX_PROTECT_NORDWR(0x0ec00, 0x0fff),
	A6XX_PROTECT_RDONLY(0x0fc00, 0x1fff),
	A6XX_PROTECT_NORDWR(0x18400, 0x0053),
	A6XX_PROTECT_RDONLY(0x18454, 0x0004),
	A6XX_PROTECT_NORDWR(0x18459, 0x1fff),
	A6XX_PROTECT_NORDWR(0x1a459, 0x1fff),
	A6XX_PROTECT_NORDWR(0x1c459, 0x1fff),
	A6XX_PROTECT_NORDWR(0x1f400, 0x0443),
	A6XX_PROTECT_RDONLY(0x1f844, 0x007b),
	A6XX_PROTECT_NORDWR(0x1f860, 0x0000),
	A6XX_PROTECT_NORDWR(0x1f878, 0x002a),
	/* CP_PROTECT_REG[45, 46] are left untouched! */
	0,
	0,
	A6XX_PROTECT_NORDWR(0x1f8c0, 0x00000),
};
DECLARE_ADRENO_PROTECT(a730_protect, 48);

static const uint32_t a7xx_pwrup_reglist_regs[] = {
	REG_A6XX_UCHE_TRAP_BASE,
	REG_A6XX_UCHE_TRAP_BASE + 1,
	REG_A6XX_UCHE_WRITE_THRU_BASE,
	REG_A6XX_UCHE_WRITE_THRU_BASE + 1,
	REG_A6XX_UCHE_GMEM_RANGE_MIN,
	REG_A6XX_UCHE_GMEM_RANGE_MIN + 1,
	REG_A6XX_UCHE_GMEM_RANGE_MAX,
	REG_A6XX_UCHE_GMEM_RANGE_MAX + 1,
	REG_A6XX_UCHE_CACHE_WAYS,
	REG_A6XX_UCHE_MODE_CNTL,
	REG_A6XX_RB_NC_MODE_CNTL,
	REG_A6XX_RB_CMP_DBG_ECO_CNTL,
	REG_A6XX_RB_CONTEXT_SWITCH_GMEM_SAVE_RESTORE_ENABLE,
	REG_A6XX_UCHE_GBIF_GX_CONFIG,
	REG_A6XX_UCHE_CLIENT_PF,
	REG_A6XX_TPL1_DBG_ECO_CNTL1,
};

DECLARE_ADRENO_REGLIST_LIST(a7xx_pwrup_reglist);

/* Applicable for X185, A750 */
static const u32 a750_ifpc_reglist_regs[] = {
	REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE(0),
	REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE(1),
	REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE(2),
	REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE(3),
	REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE(4),
	REG_A6XX_RBBM_PERFCTR_CNTL,
	REG_A6XX_TPL1_NC_MODE_CNTL,
	REG_A6XX_SP_NC_MODE_CNTL,
	REG_A6XX_CP_DBG_ECO_CNTL,
	REG_A6XX_CP_PROTECT_CNTL,
	REG_A6XX_CP_PROTECT(0),
	REG_A6XX_CP_PROTECT(1),
	REG_A6XX_CP_PROTECT(2),
	REG_A6XX_CP_PROTECT(3),
	REG_A6XX_CP_PROTECT(4),
	REG_A6XX_CP_PROTECT(5),
	REG_A6XX_CP_PROTECT(6),
	REG_A6XX_CP_PROTECT(7),
	REG_A6XX_CP_PROTECT(8),
	REG_A6XX_CP_PROTECT(9),
	REG_A6XX_CP_PROTECT(10),
	REG_A6XX_CP_PROTECT(11),
	REG_A6XX_CP_PROTECT(12),
	REG_A6XX_CP_PROTECT(13),
	REG_A6XX_CP_PROTECT(14),
	REG_A6XX_CP_PROTECT(15),
	REG_A6XX_CP_PROTECT(16),
	REG_A6XX_CP_PROTECT(17),
	REG_A6XX_CP_PROTECT(18),
	REG_A6XX_CP_PROTECT(19),
	REG_A6XX_CP_PROTECT(20),
	REG_A6XX_CP_PROTECT(21),
	REG_A6XX_CP_PROTECT(22),
	REG_A6XX_CP_PROTECT(23),
	REG_A6XX_CP_PROTECT(24),
	REG_A6XX_CP_PROTECT(25),
	REG_A6XX_CP_PROTECT(26),
	REG_A6XX_CP_PROTECT(27),
	REG_A6XX_CP_PROTECT(28),
	REG_A6XX_CP_PROTECT(29),
	REG_A6XX_CP_PROTECT(30),
	REG_A6XX_CP_PROTECT(31),
	REG_A6XX_CP_PROTECT(32),
	REG_A6XX_CP_PROTECT(33),
	REG_A6XX_CP_PROTECT(34),
	REG_A6XX_CP_PROTECT(35),
	REG_A6XX_CP_PROTECT(36),
	REG_A6XX_CP_PROTECT(37),
	REG_A6XX_CP_PROTECT(38),
	REG_A6XX_CP_PROTECT(39),
	REG_A6XX_CP_PROTECT(40),
	REG_A6XX_CP_PROTECT(41),
	REG_A6XX_CP_PROTECT(42),
	REG_A6XX_CP_PROTECT(43),
	REG_A6XX_CP_PROTECT(44),
	REG_A6XX_CP_PROTECT(45),
	REG_A6XX_CP_PROTECT(46),
	REG_A6XX_CP_PROTECT(47),
};

DECLARE_ADRENO_REGLIST_LIST(a750_ifpc_reglist);

static const struct adreno_reglist_pipe a7xx_dyn_pwrup_reglist_regs[] = {
	{ REG_A7XX_GRAS_NC_MODE_CNTL, 0, BIT(PIPE_BV) | BIT(PIPE_BR) },
};

DECLARE_ADRENO_REGLIST_PIPE_LIST(a7xx_dyn_pwrup_reglist);

static const struct adreno_info a7xx_gpus[] = {
	{
		.chip_ids = ADRENO_CHIP_IDS(0x07030001),
		.family = ADRENO_7XX_GEN1,
		.fw = {
			[ADRENO_FW_SQE] = "a730_sqe.fw",
			[ADRENO_FW_GMU] = "gmu_gen70000.bin",
		},
		.gmem = SZ_2M,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT |
			  ADRENO_QUIRK_HAS_HW_APRIV |
			  ADRENO_QUIRK_PREEMPTION,
		.funcs = &a7xx_gpu_funcs,
		.zapfw = "a730_zap.mdt",
		.a6xx = &(const struct a6xx_info) {
			.hwcg = a730_hwcg,
			.protect = &a730_protect,
			.pwrup_reglist = &a7xx_pwrup_reglist,
			.dyn_pwrup_reglist = &a7xx_dyn_pwrup_reglist,
			.gbif_cx = a640_gbif,
			.gmu_cgc_mode = 0x00020000,
		},
		.preempt_record_size = 2860 * SZ_1K,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x43020100),
		.family = ADRENO_7XX_GEN1,
		.fw = {
			[ADRENO_FW_SQE] = "qcom/gen71700_sqe.fw",
			[ADRENO_FW_GMU] = "qcom/gen71700_gmu.bin",
		},
		.gmem = SZ_1M,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT |
			  ADRENO_QUIRK_HAS_HW_APRIV |
			  ADRENO_QUIRK_PREEMPTION,
		.funcs = &a7xx_gpu_funcs,
		.a6xx = &(const struct a6xx_info) {
			.hwcg = a730_hwcg,
			.protect = &a730_protect,
			.pwrup_reglist = &a7xx_pwrup_reglist,
			.dyn_pwrup_reglist = &a7xx_dyn_pwrup_reglist,
			.gbif_cx = a640_gbif,
			.gmu_chipid = 0x07110000,
			.gmu_cgc_mode = 0x00020000,
			.bcms = (const struct a6xx_bcm[]) {
				{ .name = "SH0", .buswidth = 16 },
				{ .name = "MC0", .buswidth = 4 },
				{
					.name = "ACV",
					.fixed = true,
					.perfmode = BIT(3),
					.perfmode_bw = 16500000,
				},
				{ /* sentinel */ },
			},
		},
		.preempt_record_size = 1536 * SZ_1K,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x43050a01), /* "C510v2" */
		.family = ADRENO_7XX_GEN2,
		.fw = {
			[ADRENO_FW_SQE] = "a740_sqe.fw",
			[ADRENO_FW_GMU] = "gmu_gen70200.bin",
		},
		.gmem = 3 * SZ_1M,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT |
			  ADRENO_QUIRK_HAS_HW_APRIV |
			  ADRENO_QUIRK_PREEMPTION,
		.funcs = &a7xx_gpu_funcs,
		.zapfw = "a740_zap.mdt",
		.a6xx = &(const struct a6xx_info) {
			.hwcg = a740_hwcg,
			.protect = &a730_protect,
			.pwrup_reglist = &a7xx_pwrup_reglist,
			.dyn_pwrup_reglist = &a7xx_dyn_pwrup_reglist,
			.gbif_cx = a640_gbif,
			.gmu_chipid = 0x7020100,
			.gmu_cgc_mode = 0x00020202,
			.bcms = (const struct a6xx_bcm[]) {
				{ .name = "SH0", .buswidth = 16 },
				{ .name = "MC0", .buswidth = 4 },
				{
					.name = "ACV",
					.fixed = true,
					.perfmode = BIT(3),
					.perfmode_bw = 16500000,
				},
				{ /* sentinel */ },
			},
		},
		.preempt_record_size = 4192 * SZ_1K,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x43050c01), /* "C512v2" */
		.family = ADRENO_7XX_GEN2,
		.fw = {
			[ADRENO_FW_SQE] = "gen70500_sqe.fw",
			[ADRENO_FW_GMU] = "gen70500_gmu.bin",
		},
		.gmem = 3 * SZ_1M,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT |
			  ADRENO_QUIRK_HAS_HW_APRIV |
			  ADRENO_QUIRK_PREEMPTION |
			  ADRENO_QUIRK_IFPC,
		.funcs = &a7xx_gpu_funcs,
		.a6xx = &(const struct a6xx_info) {
			.hwcg = a740_hwcg,
			.protect = &a730_protect,
			.pwrup_reglist = &a7xx_pwrup_reglist,
			.dyn_pwrup_reglist = &a7xx_dyn_pwrup_reglist,
			.ifpc_reglist = &a750_ifpc_reglist,
			.gbif_cx = a640_gbif,
			.gmu_chipid = 0x7050001,
			.gmu_cgc_mode = 0x00020202,
			.bcms = (const struct a6xx_bcm[]) {
				{ .name = "SH0", .buswidth = 16 },
				{ .name = "MC0", .buswidth = 4 },
				{
					.name = "ACV",
					.fixed = true,
					.perfmode = BIT(3),
					.perfmode_bw = 16500000,
				},
				{ /* sentinel */ },
			},
		},
		.preempt_record_size = 4192 * SZ_1K,
		.speedbins = ADRENO_SPEEDBINS(
			{ 0,	0 },
			{ 59,	1 },
			{ 7,	2 },
			{ 232,	3 },
			{ 146,	4 },
		),
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x43051401), /* "C520v2" */
		.family = ADRENO_7XX_GEN3,
		.fw = {
			[ADRENO_FW_SQE] = "gen70900_sqe.fw",
			[ADRENO_FW_GMU] = "gmu_gen70900.bin",
		},
		.gmem = 3 * SZ_1M,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT |
			  ADRENO_QUIRK_HAS_HW_APRIV |
			  ADRENO_QUIRK_PREEMPTION |
			  ADRENO_QUIRK_IFPC,
		.funcs = &a7xx_gpu_funcs,
		.zapfw = "gen70900_zap.mbn",
		.a6xx = &(const struct a6xx_info) {
			.protect = &a730_protect,
			.pwrup_reglist = &a7xx_pwrup_reglist,
			.dyn_pwrup_reglist = &a7xx_dyn_pwrup_reglist,
			.ifpc_reglist = &a750_ifpc_reglist,
			.gbif_cx = a640_gbif,
			.gmu_chipid = 0x7090100,
			.gmu_cgc_mode = 0x00020202,
			.bcms = (const struct a6xx_bcm[]) {
				{ .name = "SH0", .buswidth = 16 },
				{ .name = "MC0", .buswidth = 4 },
				{
					.name = "ACV",
					.fixed = true,
					.perfmode = BIT(2),
					.perfmode_bw = 10687500,
				},
				{ /* sentinel */ },
			},
		},
		.preempt_record_size = 3572 * SZ_1K,
	}, {
		.chip_ids = ADRENO_CHIP_IDS(0x43030c00),
		.family = ADRENO_7XX_GEN2,
		.fw = {
			[ADRENO_FW_SQE] = "gen71500_sqe.fw",
			[ADRENO_FW_GMU] = "gen71500_gmu.bin",
		},
		.gmem = SZ_1M + SZ_512K,
		.inactive_period = DRM_MSM_INACTIVE_PERIOD,
		.quirks = ADRENO_QUIRK_HAS_CACHED_COHERENT |
			  ADRENO_QUIRK_HAS_HW_APRIV |
			  ADRENO_QUIRK_PREEMPTION,
		.funcs = &a7xx_gpu_funcs,
		.a6xx = &(const struct a6xx_info) {
			.hwcg = a740_hwcg,
			.protect = &a730_protect,
			.pwrup_reglist = &a7xx_pwrup_reglist,
			.dyn_pwrup_reglist = &a7xx_dyn_pwrup_reglist,
			.gbif_cx = a640_gbif,
			.gmu_chipid = 0x70f0000,
			.gmu_cgc_mode = 0x00020222,
			.bcms = (const struct a6xx_bcm[]) {
				{ .name = "SH0", .buswidth = 16 },
				{ .name = "MC0", .buswidth = 4 },
				{
					.name = "ACV",
					.fixed = true,
					.perfmode = BIT(3),
					.perfmode_bw = 16500000,
				},
				{ /* sentinel */ },
			},
		},
		.preempt_record_size = 4192 * SZ_1K,
		.speedbins = ADRENO_SPEEDBINS(
			{ 0,   0 },
			{ 294, 1 },
			{ 263, 2 },
			{ 233, 3 },
			{ 141, 4 },
		),
	}
};
DECLARE_ADRENO_GPULIST(a7xx);

static inline __always_unused void __build_asserts(void)
{
	BUILD_BUG_ON(a730_protect.count > a730_protect.count_max);
}
