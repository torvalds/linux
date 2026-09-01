/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __ADRENO_GEN7_17_0_SNAPSHOT_H
#define __ADRENO_GEN7_17_0_SNAPSHOT_H

#include "a6xx_gpu_state.h"

/*
 * Snapshot tables for Adreno A722 (Eliza).
 * Cluster sub-arrays that are identical to A730 reference gen7_0_0_*
 * symbols; adreno_gen7_0_0_snapshot.h is included first in the TU.
 */

static const u32 gen7_17_0_rscc_registers[] = {
	0x14000, 0x14034, 0x14036, 0x14036, 0x14040, 0x14042, 0x14044, 0x14045,
	0x14047, 0x14047, 0x14080, 0x14084, 0x14089, 0x1408c, 0x14091, 0x14094,
	0x14099, 0x1409c, 0x140a1, 0x140a4, 0x140a9, 0x140ac, 0x140b1, 0x140b4,
	0x140b9, 0x140bc, 0x14100, 0x14104, 0x14114, 0x14119, 0x14124, 0x14132,
	0x14154, 0x1416b, 0x14340, 0x14341, 0x14344, 0x14344, 0x14346, 0x1437c,
	0x143f0, 0x143f8, 0x143fa, 0x143fe, 0x14400, 0x14404, 0x14406, 0x1440a,
	0x1440c, 0x14410, 0x14412, 0x14416, 0x14418, 0x1441c, 0x1441e, 0x14422,
	0x14424, 0x14424, 0x14498, 0x144a0, 0x144a2, 0x144a6, 0x144a8, 0x144ac,
	0x144ae, 0x144b2, 0x144b4, 0x144b8, 0x144ba, 0x144be, 0x144c0, 0x144c4,
	0x144c6, 0x144ca, 0x144cc, 0x144cc, 0x14540, 0x14548, 0x1454a, 0x1454e,
	0x14550, 0x14554, 0x14556, 0x1455a, 0x1455c, 0x14560, 0x14562, 0x14566,
	0x14568, 0x1456c, 0x1456e, 0x14572, 0x14574, 0x14574, 0x145e8, 0x145f0,
	0x145f2, 0x145f6, 0x145f8, 0x145fc, 0x145fe, 0x14602, 0x14604, 0x14608,
	0x1460a, 0x1460e, 0x14610, 0x14614, 0x14616, 0x1461a, 0x1461c, 0x1461c,
	0x14690, 0x14698, 0x1469a, 0x1469e, 0x146a0, 0x146a4, 0x146a6, 0x146aa,
	0x146ac, 0x146b0, 0x146b2, 0x146b6, 0x146b8, 0x146bc, 0x146be, 0x146c2,
	0x146c4, 0x146c4, 0x14738, 0x14740, 0x14742, 0x14746, 0x14748, 0x1474c,
	0x1474e, 0x14752, 0x14754, 0x14758, 0x1475a, 0x1475e, 0x14760, 0x14764,
	0x14766, 0x1476a, 0x1476c, 0x1476c, 0x147e0, 0x147e8, 0x147ea, 0x147ee,
	0x147f0, 0x147f4, 0x147f6, 0x147fa, 0x147fc, 0x14800, 0x14802, 0x14806,
	0x14808, 0x1480c, 0x1480e, 0x14812, 0x14814, 0x14814, 0x14888, 0x14890,
	0x14892, 0x14896, 0x14898, 0x1489c, 0x1489e, 0x148a2, 0x148a4, 0x148a8,
	0x148aa, 0x148ae, 0x148b0, 0x148b4, 0x148b6, 0x148ba, 0x148bc, 0x148bc,
	0x14930, 0x14938, 0x1493a, 0x1493e, 0x14940, 0x14944, 0x14946, 0x1494a,
	0x1494c, 0x14950, 0x14952, 0x14956, 0x14958, 0x1495c, 0x1495e, 0x14962,
	0x14964, 0x14964,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_rscc_registers), 8));

static const u32 gen7_17_0_cpr_registers[] = {
	0x26800, 0x26805, 0x26808, 0x2680c, 0x26814, 0x26814, 0x2681c, 0x2681c,
	0x26820, 0x26838, 0x26840, 0x26840, 0x26848, 0x26848, 0x26850, 0x26850,
	0x26880, 0x2688e, 0x26980, 0x269b0, 0x269c0, 0x269c2, 0x269c6, 0x269c8,
	0x269e0, 0x269ee, 0x269fb, 0x269ff, 0x26a02, 0x26a07, 0x26a09, 0x26a0b,
	0x26a10, 0x26b0f, 0x27440, 0x27441, 0x27444, 0x27444, 0x27480, 0x274a2,
	0x274ac, 0x274c4, 0x274c8, 0x274da,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_cpr_registers), 8));

static const u32 gen7_17_0_gpucc_registers[] = {
	0x24000, 0x2400f, 0x24400, 0x2440f, 0x24800, 0x24805, 0x24c00, 0x24cff,
	0x25400, 0x25404, 0x25800, 0x25804, 0x25c00, 0x25c04, 0x26000, 0x26004,
	0x26400, 0x26405, 0x26414, 0x2641d, 0x2642a, 0x2642c, 0x2642e, 0x26432,
	0x26434, 0x26434, 0x26443, 0x26457, 0x26459, 0x2645d, 0x2645f, 0x26464,
	0x26477, 0x26479, 0x26489, 0x2648b, 0x2649a, 0x2649b, 0x264ad, 0x264af,
	0x264b1, 0x264b5, 0x264d6, 0x264d8, 0x264e7, 0x264e9, 0x264f9, 0x264fa,
	0x2650a, 0x2650d, 0x2651f, 0x26520, 0x2652d, 0x2652f, 0x2653e, 0x2653e,
	0x26540, 0x2654e, 0x26554, 0x26573, 0x26576, 0x26576, 0x26593, 0x26593,
	0x26600, 0x26616, 0x26620, 0x2662d, 0x26630, 0x26631, 0x26635, 0x26635,
	0x26637, 0x26637, 0x2663a, 0x2663a, 0x26642, 0x26642, 0x26656, 0x26658,
	0x2665b, 0x2665d, 0x2665f, 0x26662,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_gpucc_registers), 8));

static const u32 *gen7_17_0_external_core_regs[] = {
	gen7_17_0_gpucc_registers,
	gen7_17_0_cpr_registers,
};

static const u32 gen7_17_0_debugbus_blocks[] = {
	A7XX_DBGBUS_CP_0_0,
	A7XX_DBGBUS_CP_0_1,
	A7XX_DBGBUS_RBBM,
	A7XX_DBGBUS_HLSQ,
	A7XX_DBGBUS_UCHE_0,
	A7XX_DBGBUS_TESS_BR,
	A7XX_DBGBUS_PC_BR,
	A7XX_DBGBUS_VFDP_BR,
	A7XX_DBGBUS_VPC_BR,
	A7XX_DBGBUS_TSE_BR,
	A7XX_DBGBUS_RAS_BR,
	A7XX_DBGBUS_VSC,
	A7XX_DBGBUS_COM_0,
	A7XX_DBGBUS_LRZ_BR,
	A7XX_DBGBUS_UFC_0,
	A7XX_DBGBUS_UFC_1,
	A7XX_DBGBUS_GMU_GX,
	A7XX_DBGBUS_DBGC,
	A7XX_DBGBUS_GPC_BR,
	A7XX_DBGBUS_LARC,
	A7XX_DBGBUS_HLSQ_SPTP,
	A7XX_DBGBUS_RB_0,
	A7XX_DBGBUS_RB_1,
	A7XX_DBGBUS_UCHE_WRAPPER,
	A7XX_DBGBUS_CCU_0,
	A7XX_DBGBUS_CCU_1,
	A7XX_DBGBUS_VFD_BR_0,
	A7XX_DBGBUS_VFD_BR_1,
	A7XX_DBGBUS_VFD_BR_2,
	A7XX_DBGBUS_VFD_BR_3,
	A7XX_DBGBUS_USP_0,
	A7XX_DBGBUS_USP_1,
	A7XX_DBGBUS_TP_0,
	A7XX_DBGBUS_TP_1,
	A7XX_DBGBUS_TP_2,
	A7XX_DBGBUS_TP_3,
	A7XX_DBGBUS_USPTP_0,
	A7XX_DBGBUS_USPTP_1,
	A7XX_DBGBUS_USPTP_2,
	A7XX_DBGBUS_USPTP_3,
};

static const struct gen7_sel_reg gen7_17_0_rb_rac_sel = {
	.host_reg = REG_A6XX_RB_SUB_BLOCK_SEL_CNTL_HOST,
	.cd_reg = REG_A6XX_RB_SUB_BLOCK_SEL_CNTL_CD,
	.val = 0x0,
};

static const struct gen7_sel_reg gen7_17_0_rb_rbp_sel = {
	.host_reg = REG_A6XX_RB_SUB_BLOCK_SEL_CNTL_HOST,
	.cd_reg = REG_A6XX_RB_SUB_BLOCK_SEL_CNTL_CD,
	.val = 0x9,
};

static const u32 gen7_17_0_post_crashdumper_registers[] = {
	0x00535, 0x00535,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_post_crashdumper_registers), 8));

static const u32 gen7_17_0_gpu_registers[] = {
	0x00000, 0x00000, 0x00002, 0x00002, 0x00011, 0x00012, 0x00016, 0x0001b,
	0x0001f, 0x00032, 0x00038, 0x0003c, 0x00042, 0x00042, 0x00044, 0x00044,
	0x00047, 0x00047, 0x00049, 0x0004a, 0x0004c, 0x0004c, 0x00050, 0x00050,
	0x00056, 0x00056, 0x00073, 0x00075, 0x000ad, 0x000ae, 0x000b0, 0x000b0,
	0x000b4, 0x000b4, 0x000b8, 0x000b8, 0x000bc, 0x000bc, 0x000c0, 0x000c0,
	0x000c4, 0x000c4, 0x000c8, 0x000c8, 0x000cc, 0x000cc, 0x000d0, 0x000d0,
	0x000d4, 0x000d4, 0x000d8, 0x000d8, 0x000dc, 0x000dc, 0x000e0, 0x000e0,
	0x000e4, 0x000e4, 0x000e8, 0x000e8, 0x000ec, 0x000ec, 0x000f0, 0x000f0,
	0x000f4, 0x000f4, 0x000f8, 0x000f8, 0x00100, 0x00100, 0x00104, 0x0010b,
	0x0010f, 0x0011d, 0x0012f, 0x0012f, 0x00200, 0x0020d, 0x00215, 0x00243,
	0x00260, 0x00268, 0x00272, 0x00274, 0x00286, 0x00286, 0x0028a, 0x0028a,
	0x0028c, 0x0028c, 0x00300, 0x00401, 0x00500, 0x00500, 0x00507, 0x0050b,
	0x0050f, 0x0050f, 0x00511, 0x00511, 0x00533, 0x00534, 0x00540, 0x00555,
	0x00564, 0x00567, 0x00800, 0x00808, 0x00810, 0x00813, 0x00820, 0x00821,
	0x00823, 0x00827, 0x00830, 0x00834, 0x00840, 0x00841, 0x00843, 0x00847,
	0x0084f, 0x00886, 0x008a0, 0x008ab, 0x008c0, 0x008c0, 0x008c4, 0x008c5,
	0x008d0, 0x008dd, 0x008f0, 0x008f3, 0x00900, 0x00903, 0x00908, 0x00911,
	0x00928, 0x0093e, 0x00942, 0x0094d, 0x00980, 0x00984, 0x0098d, 0x0098f,
	0x009b0, 0x009b4, 0x009c2, 0x009c9, 0x009ce, 0x009d7, 0x00a00, 0x00a00,
	0x00a02, 0x00a03, 0x00a10, 0x00a4f, 0x00a67, 0x00a6c, 0x00a9c, 0x00a9f,
	0x00c00, 0x00c00, 0x00c02, 0x00c04, 0x00c06, 0x00c06, 0x00c10, 0x00cd9,
	0x00ce0, 0x00d0c, 0x00df0, 0x00df4, 0x00e01, 0x00e02, 0x00e07, 0x00e0e,
	0x00e10, 0x00e12, 0x00e17, 0x00e17, 0x00e19, 0x00e19, 0x00e1b, 0x00e2b,
	0x00e30, 0x00e32, 0x00e38, 0x00e3c,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_gpu_registers), 8));

static const u32 gen7_17_0_dbgc_registers[] = {
	0x00600, 0x0061c, 0x0061e, 0x00634, 0x00640, 0x0065a, 0x00679, 0x0067a,
	0x00699, 0x00699, 0x0069b, 0x0069e, 0x18400, 0x1841c, 0x1841e, 0x18434,
	0x18440, 0x1845c, 0x18479, 0x1847c, 0x18580, 0x18581,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_dbgc_registers), 8));

static const u32 gen7_17_0_noncontext_pipe_br_registers[] = {
	0x00887, 0x0088c, 0x08600, 0x08600, 0x08602, 0x08602, 0x08610, 0x0861b,
	0x08620, 0x08620, 0x08630, 0x08630, 0x08637, 0x08639, 0x08640, 0x08640,
	0x09600, 0x09600, 0x09602, 0x09603, 0x0960a, 0x09616, 0x09624, 0x0963a,
	0x09640, 0x09640, 0x09e00, 0x09e00, 0x09e02, 0x09e07, 0x09e0a, 0x09e16,
	0x09e19, 0x09e19, 0x09e1c, 0x09e1c, 0x09e20, 0x09e25, 0x09e30, 0x09e31,
	0x09e40, 0x09e51, 0x09e64, 0x09e64, 0x09e70, 0x09e72, 0x09e78, 0x09e79,
	0x09e80, 0x09fff, 0x0a600, 0x0a600, 0x0a603, 0x0a603, 0x0a610, 0x0a61f,
	0x0a630, 0x0a631, 0x0a638, 0x0a638,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_noncontext_pipe_br_registers), 8));

static const u32 gen7_17_0_noncontext_rb_rac_pipe_br_registers[] = {
	0x08e10, 0x08e1c, 0x08e20, 0x08e25, 0x08e51, 0x08e54,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_noncontext_rb_rac_pipe_br_registers), 8));

static const u32 gen7_17_0_noncontext_rb_rbp_pipe_br_registers[] = {
	0x08e01, 0x08e01, 0x08e04, 0x08e04, 0x08e06, 0x08e09, 0x08e0c, 0x08e0c,
	0x08e28, 0x08e28, 0x08e2c, 0x08e35, 0x08e3b, 0x08e3f, 0x08e50, 0x08e50,
	0x08e5b, 0x08e5d, 0x08e5f, 0x08e5f, 0x08e61, 0x08e61, 0x08e63, 0x08e65,
	0x08e68, 0x08e68, 0x08e70, 0x08e79, 0x08e80, 0x08e8f,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_noncontext_rb_rbp_pipe_br_registers), 8));

static const u32 gen7_17_0_pc_cluster_fe_pipe_br_registers[] = {
	0x09800, 0x09804, 0x09806, 0x0980a, 0x09810, 0x09811, 0x09884, 0x09886,
	0x09970, 0x09972, 0x09b00, 0x09b08,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_pc_cluster_fe_pipe_br_registers), 8));

static const u32 gen7_17_0_sp_cluster_sp_ps_pipe_lpac_hlsq_state_registers[] = {
	0x0aa40, 0x0aabf,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_sp_cluster_sp_ps_pipe_lpac_hlsq_state_registers), 8));

static const u32 gen7_17_0_sp_cluster_sp_ps_pipe_lpac_usptp_registers[] = {
	0x0aa40, 0x0aabf,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_sp_cluster_sp_ps_pipe_lpac_usptp_registers), 8));

static const u32 gen7_17_0_non_context_tpl1_pipe_none_usptp_registers[] = {
	0x0b602, 0x0b602, 0x0b604, 0x0b604, 0x0b608, 0x0b60c, 0x0b60f, 0x0b621,
	0x0b630, 0x0b633,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_non_context_tpl1_pipe_none_usptp_registers), 8));

static const u32 gen7_17_0_non_context_tpl1_pipe_br_usptp_registers[] = {
	0x0b600, 0x0b600,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_non_context_tpl1_pipe_br_usptp_registers), 8));

static const u32 gen7_17_0_tpl1_cluster_sp_vs_pipe_br_usptp_registers[] = {
	0x0b300, 0x0b307, 0x0b309, 0x0b309, 0x0b310, 0x0b310,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_tpl1_cluster_sp_vs_pipe_br_usptp_registers), 8));

static const u32 gen7_17_0_tpl1_cluster_sp_ps_pipe_br_usptp_registers[] = {
	0x0b180, 0x0b183, 0x0b190, 0x0b195, 0x0b2c0, 0x0b2d5, 0x0b300, 0x0b307,
	0x0b309, 0x0b309, 0x0b310, 0x0b310,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_17_0_tpl1_cluster_sp_ps_pipe_br_usptp_registers), 8));

/* No BV pipe — gen7_0_0_* sub-arrays are shared from adreno_gen7_0_0_snapshot.h */
static struct gen7_cluster_registers gen7_17_0_clusters[] = {
	{ A7XX_CLUSTER_NONE, PIPE_BR, STATE_NON_CONTEXT,
		gen7_17_0_noncontext_pipe_br_registers, },
	{ A7XX_CLUSTER_NONE, PIPE_BR, STATE_NON_CONTEXT,
		gen7_17_0_noncontext_rb_rac_pipe_br_registers, &gen7_17_0_rb_rac_sel, },
	{ A7XX_CLUSTER_NONE, PIPE_BR, STATE_NON_CONTEXT,
		gen7_17_0_noncontext_rb_rbp_pipe_br_registers, &gen7_17_0_rb_rbp_sel, },
	{ A7XX_CLUSTER_PS, PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_rb_rac_cluster_ps_pipe_br_registers, &gen7_17_0_rb_rac_sel, },
	{ A7XX_CLUSTER_PS, PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_rb_rac_cluster_ps_pipe_br_registers, &gen7_17_0_rb_rac_sel, },
	{ A7XX_CLUSTER_PS, PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_rb_rbp_cluster_ps_pipe_br_registers, &gen7_17_0_rb_rbp_sel, },
	{ A7XX_CLUSTER_PS, PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_rb_rbp_cluster_ps_pipe_br_registers, &gen7_17_0_rb_rbp_sel, },
	{ A7XX_CLUSTER_GRAS, PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_gras_cluster_gras_pipe_br_registers, },
	{ A7XX_CLUSTER_GRAS, PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_gras_cluster_gras_pipe_br_registers, },
	{ A7XX_CLUSTER_FE, PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_17_0_pc_cluster_fe_pipe_br_registers, },
	{ A7XX_CLUSTER_FE, PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_17_0_pc_cluster_fe_pipe_br_registers, },
	{ A7XX_CLUSTER_FE, PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_vfd_cluster_fe_pipe_bv_registers, },
	{ A7XX_CLUSTER_FE, PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_vfd_cluster_fe_pipe_bv_registers, },
	{ A7XX_CLUSTER_FE, PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_vpc_cluster_fe_pipe_br_registers, },
	{ A7XX_CLUSTER_FE, PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_vpc_cluster_fe_pipe_br_registers, },
	{ A7XX_CLUSTER_PC_VS, PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_vpc_cluster_pc_vs_pipe_br_registers, },
	{ A7XX_CLUSTER_PC_VS, PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_vpc_cluster_pc_vs_pipe_br_registers, },
	{ A7XX_CLUSTER_VPC_PS, PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_vpc_cluster_vpc_ps_pipe_br_registers, },
	{ A7XX_CLUSTER_VPC_PS, PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_vpc_cluster_vpc_ps_pipe_br_registers, },
};

/* No BV pipe; 2 SPs, 2 USPTPs */
static struct gen7_sptp_cluster_registers gen7_17_0_sptp_clusters[] = {
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, PIPE_BR, 0, A7XX_HLSQ_STATE,
		gen7_0_0_sp_noncontext_pipe_br_hlsq_state_registers, 0xae00 },
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, PIPE_BR, 0, A7XX_SP_TOP,
		gen7_0_0_sp_noncontext_pipe_br_sp_top_registers, 0xae00 },
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, PIPE_BR, 0, A7XX_USPTP,
		gen7_0_0_sp_noncontext_pipe_br_usptp_registers, 0xae00 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, PIPE_BR, 0, A7XX_HLSQ_STATE,
		gen7_0_0_sp_cluster_sp_vs_pipe_br_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, PIPE_BR, 0, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_vs_pipe_br_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, PIPE_BR, 0, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_vs_pipe_br_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, PIPE_BR, 1, A7XX_HLSQ_STATE,
		gen7_0_0_sp_cluster_sp_vs_pipe_br_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, PIPE_BR, 1, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_vs_pipe_br_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, PIPE_BR, 1, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_vs_pipe_br_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, PIPE_BR, 0, A7XX_HLSQ_STATE,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, PIPE_BR, 0, A7XX_HLSQ_DP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_dp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, PIPE_BR, 0, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, PIPE_BR, 0, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, PIPE_LPAC, 0, A7XX_HLSQ_STATE,
		gen7_17_0_sp_cluster_sp_ps_pipe_lpac_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, PIPE_LPAC, 0, A7XX_USPTP,
		gen7_17_0_sp_cluster_sp_ps_pipe_lpac_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, PIPE_BR, 1, A7XX_HLSQ_STATE,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, PIPE_BR, 1, A7XX_HLSQ_DP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_dp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, PIPE_BR, 1, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, PIPE_BR, 1, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX2_3D_CPS_REG, PIPE_BR, 2, A7XX_HLSQ_DP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_dp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX2_3D_CPS_REG, PIPE_BR, 2, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX2_3D_CPS_REG, PIPE_BR, 2, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX3_3D_CPS_REG, PIPE_BR, 3, A7XX_HLSQ_DP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_dp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX3_3D_CPS_REG, PIPE_BR, 3, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX3_3D_CPS_REG, PIPE_BR, 3, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_NONE, A7XX_TP0_NCTX_REG, PIPE_NONE, 0, A7XX_USPTP,
		gen7_17_0_non_context_tpl1_pipe_none_usptp_registers, 0xb600 },
	{ A7XX_CLUSTER_NONE, A7XX_TP0_NCTX_REG, PIPE_BR, 0, A7XX_USPTP,
		gen7_17_0_non_context_tpl1_pipe_br_usptp_registers, 0xb600 },
	{ A7XX_CLUSTER_SP_VS, A7XX_TP0_CTX0_3D_CVS_REG, PIPE_BR, 0, A7XX_USPTP,
		gen7_17_0_tpl1_cluster_sp_vs_pipe_br_usptp_registers, 0xb000 },
	{ A7XX_CLUSTER_SP_VS, A7XX_TP0_CTX1_3D_CVS_REG, PIPE_BR, 1, A7XX_USPTP,
		gen7_17_0_tpl1_cluster_sp_vs_pipe_br_usptp_registers, 0xb000 },
	{ A7XX_CLUSTER_SP_PS, A7XX_TP0_CTX0_3D_CPS_REG, PIPE_BR, 0, A7XX_USPTP,
		gen7_17_0_tpl1_cluster_sp_ps_pipe_br_usptp_registers, 0xb000 },
	{ A7XX_CLUSTER_SP_PS, A7XX_TP0_CTX1_3D_CPS_REG, PIPE_BR, 1, A7XX_USPTP,
		gen7_17_0_tpl1_cluster_sp_ps_pipe_br_usptp_registers, 0xb000 },
	{ A7XX_CLUSTER_SP_PS, A7XX_TP0_CTX2_3D_CPS_REG, PIPE_BR, 2, A7XX_USPTP,
		gen7_17_0_tpl1_cluster_sp_ps_pipe_br_usptp_registers, 0xb000 },
	{ A7XX_CLUSTER_SP_PS, A7XX_TP0_CTX3_3D_CPS_REG, PIPE_BR, 3, A7XX_USPTP,
		gen7_17_0_tpl1_cluster_sp_ps_pipe_br_usptp_registers, 0xb000 },
};

static struct gen7_shader_block gen7_17_0_shader_blocks[] = {
	{ A7XX_TP0_TMO_DATA,                  0x0200, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_TP0_SMO_DATA,                  0x0080, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_TP0_MIPMAP_BASE_DATA,          0x03c0, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_INST_DATA,                  0x0800, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_INST_DATA_1,                0x0800, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_0_DATA,                  0x0800, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_1_DATA,                  0x0800, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_2_DATA,                  0x0800, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_3_DATA,                  0x0800, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_4_DATA,                  0x0800, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_5_DATA,                  0x0800, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_CB_RAM,                     0x0390, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_INST_TAG,                   0x0090, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_TMO_TAG,                    0x0080, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_SMO_TAG,                    0x0080, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_STATE_DATA,                 0x0040, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_HWAVE_RAM,                  0x0100, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_L0_INST_BUF,                0x0050, 2, 2, PIPE_BR, A7XX_USPTP },
	{ A7XX_HLSQ_CVS_BE_CTXT_BUF_RAM_TAG,  0x0010, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CPS_BE_CTXT_BUF_RAM_TAG,  0x0010, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CVS_BE_CTXT_BUF_RAM,  0x0300, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CPS_BE_CTXT_BUF_RAM,  0x0300, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CHUNK_CVS_RAM,            0x01c0, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CHUNK_CPS_RAM,            0x0300, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CHUNK_CVS_RAM_TAG,        0x0040, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CHUNK_CPS_RAM_TAG,        0x0040, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_ICB_CVS_CB_BASE_TAG,      0x0010, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_ICB_CPS_CB_BASE_TAG,      0x0010, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CVS_MISC_RAM,             0x0280, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CPS_MISC_RAM,             0x0800, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CPS_MISC_RAM_1,           0x0200, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_INST_RAM,                 0x0800, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CVS_CONST_RAM,        0x0800, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CPS_CONST_RAM,        0x0800, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CVS_MISC_RAM_TAG,         0x0010, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CPS_MISC_RAM_TAG,         0x0010, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_INST_RAM_TAG,             0x0080, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CVS_CONST_RAM_TAG,    0x0064, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CPS_CONST_RAM_TAG,    0x0064, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_INST_RAM_1,               0x0800, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_STPROC_META,              0x0010, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_BV_BE_META,               0x0010, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_DATAPATH_META,            0x0020, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_FRONTEND_META,            0x0040, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_INDIRECT_META,            0x0010, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_BACKEND_META,             0x0040, 1, 1, PIPE_BR, A7XX_HLSQ_STATE },
};

static struct gen7_reg_list gen7_17_0_reg_list[] = {
	{ gen7_17_0_gpu_registers, NULL },
	{ gen7_17_0_dbgc_registers, NULL },
	{ NULL, NULL },
};

static const struct a6xx_indexed_registers gen7_17_0_cp_indexed_reglist[] = {
	{ "CP_SQE_STAT", REG_A6XX_CP_SQE_STAT_ADDR,
		REG_A6XX_CP_SQE_STAT_DATA, 0x40, NULL },
	{ "CP_DRAW_STATE", REG_A6XX_CP_DRAW_STATE_ADDR,
		REG_A6XX_CP_DRAW_STATE_DATA, 0x100, NULL },
	{ "CP_SQE_UCODE_DBG", REG_A6XX_CP_SQE_UCODE_DBG_ADDR,
		REG_A6XX_CP_SQE_UCODE_DBG_DATA, 0x8000, NULL },
	{ "CP_ROQ_DBG", REG_A6XX_CP_ROQ_DBG_ADDR,
		REG_A6XX_CP_ROQ_DBG_DATA, 0, a7xx_get_cp_roq_size },
};

#endif /* __ADRENO_GEN7_17_0_SNAPSHOT_H */
