/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __ADRENO_GEN7_9_0_SNAPSHOT_H
#define __ADRENO_GEN7_9_0_SNAPSHOT_H

#include "a6xx_gpu_state.h"

static const u32 gen7_9_0_debugbus_blocks[] = {
	A7XX_DBGBUS_CP_0_0,
	A7XX_DBGBUS_CP_0_1,
	A7XX_DBGBUS_RBBM,
	A7XX_DBGBUS_HLSQ,
	A7XX_DBGBUS_UCHE_0,
	A7XX_DBGBUS_UCHE_1,
	A7XX_DBGBUS_TESS_BR,
	A7XX_DBGBUS_TESS_BV,
	A7XX_DBGBUS_PC_BR,
	A7XX_DBGBUS_PC_BV,
	A7XX_DBGBUS_VFDP_BR,
	A7XX_DBGBUS_VFDP_BV,
	A7XX_DBGBUS_VPC_BR,
	A7XX_DBGBUS_VPC_BV,
	A7XX_DBGBUS_TSE_BR,
	A7XX_DBGBUS_TSE_BV,
	A7XX_DBGBUS_RAS_BR,
	A7XX_DBGBUS_RAS_BV,
	A7XX_DBGBUS_VSC,
	A7XX_DBGBUS_COM_0,
	A7XX_DBGBUS_LRZ_BR,
	A7XX_DBGBUS_LRZ_BV,
	A7XX_DBGBUS_UFC_0,
	A7XX_DBGBUS_UFC_1,
	A7XX_DBGBUS_GMU_GX,
	A7XX_DBGBUS_DBGC,
	A7XX_DBGBUS_GPC_BR,
	A7XX_DBGBUS_GPC_BV,
	A7XX_DBGBUS_LARC,
	A7XX_DBGBUS_HLSQ_SPTP,
	A7XX_DBGBUS_RB_0,
	A7XX_DBGBUS_RB_1,
	A7XX_DBGBUS_RB_2,
	A7XX_DBGBUS_RB_3,
	A7XX_DBGBUS_RB_4,
	A7XX_DBGBUS_RB_5,
	A7XX_DBGBUS_UCHE_WRAPPER,
	A7XX_DBGBUS_CCU_0,
	A7XX_DBGBUS_CCU_1,
	A7XX_DBGBUS_CCU_2,
	A7XX_DBGBUS_CCU_3,
	A7XX_DBGBUS_CCU_4,
	A7XX_DBGBUS_CCU_5,
	A7XX_DBGBUS_VFD_BR_0,
	A7XX_DBGBUS_VFD_BR_1,
	A7XX_DBGBUS_VFD_BR_2,
	A7XX_DBGBUS_VFD_BV_0,
	A7XX_DBGBUS_VFD_BV_1,
	A7XX_DBGBUS_VFD_BV_2,
	A7XX_DBGBUS_USP_0,
	A7XX_DBGBUS_USP_1,
	A7XX_DBGBUS_USP_2,
	A7XX_DBGBUS_USP_3,
	A7XX_DBGBUS_USP_4,
	A7XX_DBGBUS_USP_5,
	A7XX_DBGBUS_TP_0,
	A7XX_DBGBUS_TP_1,
	A7XX_DBGBUS_TP_2,
	A7XX_DBGBUS_TP_3,
	A7XX_DBGBUS_TP_4,
	A7XX_DBGBUS_TP_5,
	A7XX_DBGBUS_TP_6,
	A7XX_DBGBUS_TP_7,
	A7XX_DBGBUS_TP_8,
	A7XX_DBGBUS_TP_9,
	A7XX_DBGBUS_TP_10,
	A7XX_DBGBUS_TP_11,
	A7XX_DBGBUS_USPTP_0,
	A7XX_DBGBUS_USPTP_1,
	A7XX_DBGBUS_USPTP_2,
	A7XX_DBGBUS_USPTP_3,
	A7XX_DBGBUS_USPTP_4,
	A7XX_DBGBUS_USPTP_5,
	A7XX_DBGBUS_USPTP_6,
	A7XX_DBGBUS_USPTP_7,
	A7XX_DBGBUS_USPTP_8,
	A7XX_DBGBUS_USPTP_9,
	A7XX_DBGBUS_USPTP_10,
	A7XX_DBGBUS_USPTP_11,
	A7XX_DBGBUS_CCHE_0,
	A7XX_DBGBUS_CCHE_1,
	A7XX_DBGBUS_CCHE_2,
	A7XX_DBGBUS_VPC_DSTR_0,
	A7XX_DBGBUS_VPC_DSTR_1,
	A7XX_DBGBUS_VPC_DSTR_2,
	A7XX_DBGBUS_HLSQ_DP_STR_0,
	A7XX_DBGBUS_HLSQ_DP_STR_1,
	A7XX_DBGBUS_HLSQ_DP_STR_2,
	A7XX_DBGBUS_HLSQ_DP_STR_3,
	A7XX_DBGBUS_HLSQ_DP_STR_4,
	A7XX_DBGBUS_HLSQ_DP_STR_5,
	A7XX_DBGBUS_UFC_DSTR_0,
	A7XX_DBGBUS_UFC_DSTR_1,
	A7XX_DBGBUS_UFC_DSTR_2,
	A7XX_DBGBUS_CGC_SUBCORE,
	A7XX_DBGBUS_CGC_CORE,
};

static const u32 gen7_9_0_gbif_debugbus_blocks[] = {
	A7XX_DBGBUS_GBIF_GX,
};

static const u32 gen7_9_0_cx_debugbus_blocks[] = {
	A7XX_DBGBUS_CX,
	A7XX_DBGBUS_GMU_CX,
	A7XX_DBGBUS_GBIF_CX,
};

static struct gen7_shader_block gen7_9_0_shader_blocks[] = {
	{ A7XX_TP0_TMO_DATA, 0x0200, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_TP0_SMO_DATA, 0x0080, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_TP0_MIPMAP_BASE_DATA, 0x03C0, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_INST_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_INST_DATA_1, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_0_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_1_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_2_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_3_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_4_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_5_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_6_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_7_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_CB_RAM, 0x0390, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_13_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_14_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_INST_TAG, 0x00C0, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_INST_DATA_2, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_TMO_TAG, 0x0080, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_SMO_TAG, 0x0080, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_STATE_DATA, 0x0040, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_HWAVE_RAM, 0x0100, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_L0_INST_BUF, 0x0050, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_8_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_9_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_10_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_11_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_SP_LB_12_DATA, 0x0800, 6, 2, A7XX_PIPE_BR, A7XX_USPTP },
	{ A7XX_HLSQ_DATAPATH_DSTR_META, 0x0010, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_DATAPATH_DSTR_META, 0x0010, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_L2STC_TAG_RAM, 0x0200, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_L2STC_INFO_CMD, 0x0474, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CVS_BE_CTXT_BUF_RAM_TAG, 0x0080, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CVS_BE_CTXT_BUF_RAM_TAG, 0x0080, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CPS_BE_CTXT_BUF_RAM_TAG, 0x0080, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CVS_BE_CTXT_BUF_RAM, 0x0400, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CVS_BE_CTXT_BUF_RAM, 0x0400, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CPS_BE_CTXT_BUF_RAM, 0x0400, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CHUNK_CVS_RAM, 0x01C0, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CHUNK_CVS_RAM, 0x01C0, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CHUNK_CPS_RAM, 0x0300, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CHUNK_CPS_RAM, 0x0180, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CHUNK_CVS_RAM_TAG, 0x0040, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CHUNK_CVS_RAM_TAG, 0x0040, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CHUNK_CPS_RAM_TAG, 0x0040, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CHUNK_CPS_RAM_TAG, 0x0040, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_ICB_CVS_CB_BASE_TAG, 0x0010, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_ICB_CVS_CB_BASE_TAG, 0x0010, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_ICB_CPS_CB_BASE_TAG, 0x0010, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_ICB_CPS_CB_BASE_TAG, 0x0010, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CVS_MISC_RAM, 0x0540, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CVS_MISC_RAM, 0x0540, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CPS_MISC_RAM, 0x0640, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CPS_MISC_RAM, 0x00B0, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CPS_MISC_RAM_1, 0x0800, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_INST_RAM, 0x0800, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_INST_RAM, 0x0800, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_INST_RAM, 0x0200, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CVS_CONST_RAM, 0x0800, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CVS_CONST_RAM, 0x0800, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CPS_CONST_RAM, 0x0800, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CPS_CONST_RAM, 0x0800, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CVS_MISC_RAM_TAG, 0x0050, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CVS_MISC_RAM_TAG, 0x0050, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CPS_MISC_RAM_TAG, 0x0050, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_CPS_MISC_RAM_TAG, 0x0008, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_INST_RAM_TAG, 0x0014, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_INST_RAM_TAG, 0x0010, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_INST_RAM_TAG, 0x0004, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CVS_CONST_RAM_TAG, 0x0040, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CVS_CONST_RAM_TAG, 0x0040, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CPS_CONST_RAM_TAG, 0x0040, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_CPS_CONST_RAM_TAG, 0x0020, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_LOCAL_MISC_RAM, 0x03C0, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_LOCAL_MISC_RAM, 0x0280, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_LOCAL_MISC_RAM, 0x0050, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_LOCAL_MISC_RAM_TAG, 0x0010, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_GFX_LOCAL_MISC_RAM_TAG, 0x0008, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_INST_RAM_1, 0x0800, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_STPROC_META, 0x0010, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_BV_BE_META, 0x0018, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_BV_BE_META, 0x0018, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_INST_RAM_2, 0x0800, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_DATAPATH_META, 0x0020, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_FRONTEND_META, 0x0080, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_FRONTEND_META, 0x0080, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_FRONTEND_META, 0x0080, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_INDIRECT_META, 0x0010, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_BACKEND_META, 0x0040, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_BACKEND_META, 0x0040, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE },
	{ A7XX_HLSQ_BACKEND_META, 0x0040, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE },
};

/*
 * Block   : ['PRE_CRASHDUMPER', 'GBIF']
 * pairs   : 2 (Regs:5), 5 (Regs:38)
 */
static const u32 gen7_9_0_pre_crashdumper_gpu_registers[] = {
	 0x00210, 0x00213, 0x00536, 0x00536, 0x03c00, 0x03c0b, 0x03c40, 0x03c42,
	 0x03c45, 0x03c47, 0x03c49, 0x03c4a, 0x03cc0, 0x03cd1,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_pre_crashdumper_gpu_registers), 8));

/*
 * Block   : ['BROADCAST', 'CP', 'GRAS', 'GXCLKCTL']
 * Block   : ['PC', 'RBBM', 'RDVM', 'UCHE']
 * Block   : ['VFD', 'VPC', 'VSC']
 * Pipeline: A7XX_PIPE_NONE
 * pairs   : 196 (Regs:1778)
 */
static const u32 gen7_9_0_gpu_registers[] = {
	 0x00000, 0x00000, 0x00002, 0x00002, 0x00011, 0x00012, 0x00016, 0x0001b,
	 0x0001f, 0x00032, 0x00038, 0x0003c, 0x00044, 0x00044, 0x00047, 0x00047,
	 0x00049, 0x0004a, 0x0004c, 0x0004c, 0x00056, 0x00056, 0x00073, 0x0007d,
	 0x00090, 0x000a8, 0x000ad, 0x000ad, 0x00117, 0x00117, 0x00120, 0x00122,
	 0x00130, 0x0013f, 0x00142, 0x0015f, 0x00162, 0x00164, 0x00166, 0x00171,
	 0x00173, 0x00174, 0x00176, 0x0017b, 0x0017e, 0x00180, 0x00183, 0x00192,
	 0x00195, 0x00196, 0x00199, 0x0019a, 0x0019d, 0x001a2, 0x001aa, 0x001ae,
	 0x001b9, 0x001b9, 0x001bb, 0x001bb, 0x001be, 0x001be, 0x001c1, 0x001c2,
	 0x001c5, 0x001c5, 0x001c7, 0x001c7, 0x001c9, 0x001c9, 0x001cb, 0x001ce,
	 0x001d1, 0x001df, 0x001e1, 0x001e3, 0x001e5, 0x001e5, 0x001e7, 0x001e9,
	 0x00200, 0x0020d, 0x00215, 0x00253, 0x00260, 0x00260, 0x00264, 0x00270,
	 0x00272, 0x00274, 0x00281, 0x00281, 0x00283, 0x00283, 0x00289, 0x0028d,
	 0x00290, 0x002a2, 0x002c0, 0x002c1, 0x00300, 0x00401, 0x00410, 0x00451,
	 0x00460, 0x004a3, 0x004c0, 0x004d1, 0x00500, 0x00500, 0x00507, 0x0050b,
	 0x0050f, 0x0050f, 0x00511, 0x00511, 0x00533, 0x00535, 0x00540, 0x0055b,
	 0x00564, 0x00567, 0x00574, 0x00577, 0x00584, 0x0059b, 0x005fb, 0x005ff,
	 0x00800, 0x00808, 0x00810, 0x00813, 0x00820, 0x00821, 0x00823, 0x00827,
	 0x00830, 0x00834, 0x0083f, 0x00841, 0x00843, 0x00847, 0x0084f, 0x00886,
	 0x008a0, 0x008ab, 0x008c0, 0x008c0, 0x008c4, 0x008c4, 0x008c6, 0x008c6,
	 0x008d0, 0x008dd, 0x008e0, 0x008e6, 0x008f0, 0x008f3, 0x00900, 0x00903,
	 0x00908, 0x00911, 0x00928, 0x0093e, 0x00942, 0x0094d, 0x00980, 0x00984,
	 0x0098d, 0x0098f, 0x009b0, 0x009b4, 0x009c2, 0x009c9, 0x009ce, 0x009d7,
	 0x009e0, 0x009e7, 0x00a00, 0x00a00, 0x00a02, 0x00a03, 0x00a10, 0x00a4f,
	 0x00a61, 0x00a9f, 0x00ad0, 0x00adb, 0x00b00, 0x00b31, 0x00b35, 0x00b3c,
	 0x00b40, 0x00b40, 0x00b70, 0x00b73, 0x00b78, 0x00b79, 0x00b7c, 0x00b7d,
	 0x00b80, 0x00b81, 0x00b84, 0x00b85, 0x00b88, 0x00b89, 0x00b8c, 0x00b8d,
	 0x00b90, 0x00b93, 0x00b98, 0x00b99, 0x00b9c, 0x00b9d, 0x00ba0, 0x00ba1,
	 0x00ba4, 0x00ba5, 0x00ba8, 0x00ba9, 0x00bac, 0x00bad, 0x00bb0, 0x00bb1,
	 0x00bb4, 0x00bb5, 0x00bb8, 0x00bb9, 0x00bbc, 0x00bbd, 0x00bc0, 0x00bc1,
	 0x00c00, 0x00c00, 0x00c02, 0x00c04, 0x00c06, 0x00c06, 0x00c10, 0x00cd9,
	 0x00ce0, 0x00d0c, 0x00df0, 0x00df4, 0x00e01, 0x00e02, 0x00e07, 0x00e0e,
	 0x00e10, 0x00e13, 0x00e17, 0x00e19, 0x00e1c, 0x00e2b, 0x00e30, 0x00e32,
	 0x00e3a, 0x00e3d, 0x00e50, 0x00e5b, 0x02840, 0x0287f, 0x0ec00, 0x0ec01,
	 0x0ec05, 0x0ec05, 0x0ec07, 0x0ec07, 0x0ec0a, 0x0ec0a, 0x0ec12, 0x0ec12,
	 0x0ec26, 0x0ec28, 0x0ec2b, 0x0ec2d, 0x0ec2f, 0x0ec2f, 0x0ec40, 0x0ec41,
	 0x0ec45, 0x0ec45, 0x0ec47, 0x0ec47, 0x0ec4a, 0x0ec4a, 0x0ec52, 0x0ec52,
	 0x0ec66, 0x0ec68, 0x0ec6b, 0x0ec6d, 0x0ec6f, 0x0ec6f, 0x0ec80, 0x0ec81,
	 0x0ec85, 0x0ec85, 0x0ec87, 0x0ec87, 0x0ec8a, 0x0ec8a, 0x0ec92, 0x0ec92,
	 0x0eca6, 0x0eca8, 0x0ecab, 0x0ecad, 0x0ecaf, 0x0ecaf, 0x0ecc0, 0x0ecc1,
	 0x0ecc5, 0x0ecc5, 0x0ecc7, 0x0ecc7, 0x0ecca, 0x0ecca, 0x0ecd2, 0x0ecd2,
	 0x0ece6, 0x0ece8, 0x0eceb, 0x0eced, 0x0ecef, 0x0ecef, 0x0ed00, 0x0ed01,
	 0x0ed05, 0x0ed05, 0x0ed07, 0x0ed07, 0x0ed0a, 0x0ed0a, 0x0ed12, 0x0ed12,
	 0x0ed26, 0x0ed28, 0x0ed2b, 0x0ed2d, 0x0ed2f, 0x0ed2f, 0x0ed40, 0x0ed41,
	 0x0ed45, 0x0ed45, 0x0ed47, 0x0ed47, 0x0ed4a, 0x0ed4a, 0x0ed52, 0x0ed52,
	 0x0ed66, 0x0ed68, 0x0ed6b, 0x0ed6d, 0x0ed6f, 0x0ed6f, 0x0ed80, 0x0ed81,
	 0x0ed85, 0x0ed85, 0x0ed87, 0x0ed87, 0x0ed8a, 0x0ed8a, 0x0ed92, 0x0ed92,
	 0x0eda6, 0x0eda8, 0x0edab, 0x0edad, 0x0edaf, 0x0edaf,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_gpu_registers), 8));

static const u32 gen7_9_0_gxclkctl_registers[] = {
	 0x18800, 0x18800, 0x18808, 0x1880b, 0x18820, 0x18822, 0x18830, 0x18830,
	 0x18834, 0x1883b,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_gxclkctl_registers), 8));

/*
 * Block   : ['GMUAO', 'GMUCX', 'GMUCX_RAM']
 * Pipeline: A7XX_PIPE_NONE
 * pairs   : 134 (Regs:429)
 */
static const u32 gen7_9_0_gmu_registers[] = {
	 0x10001, 0x10001, 0x10003, 0x10003, 0x10401, 0x10401, 0x10403, 0x10403,
	 0x10801, 0x10801, 0x10803, 0x10803, 0x10c01, 0x10c01, 0x10c03, 0x10c03,
	 0x11001, 0x11001, 0x11003, 0x11003, 0x11401, 0x11401, 0x11403, 0x11403,
	 0x11801, 0x11801, 0x11803, 0x11803, 0x11c01, 0x11c01, 0x11c03, 0x11c03,
	 0x1f400, 0x1f40b, 0x1f40f, 0x1f411, 0x1f500, 0x1f500, 0x1f507, 0x1f507,
	 0x1f509, 0x1f50b, 0x1f700, 0x1f701, 0x1f704, 0x1f706, 0x1f708, 0x1f709,
	 0x1f70c, 0x1f70d, 0x1f710, 0x1f711, 0x1f713, 0x1f716, 0x1f718, 0x1f71d,
	 0x1f720, 0x1f724, 0x1f729, 0x1f729, 0x1f730, 0x1f747, 0x1f750, 0x1f756,
	 0x1f758, 0x1f759, 0x1f75c, 0x1f75c, 0x1f760, 0x1f761, 0x1f764, 0x1f76b,
	 0x1f770, 0x1f775, 0x1f780, 0x1f785, 0x1f790, 0x1f798, 0x1f7a0, 0x1f7a8,
	 0x1f7b0, 0x1f7b3, 0x1f800, 0x1f804, 0x1f807, 0x1f808, 0x1f80b, 0x1f80c,
	 0x1f80f, 0x1f80f, 0x1f811, 0x1f811, 0x1f813, 0x1f817, 0x1f819, 0x1f81c,
	 0x1f824, 0x1f82a, 0x1f82d, 0x1f830, 0x1f840, 0x1f853, 0x1f860, 0x1f860,
	 0x1f862, 0x1f866, 0x1f868, 0x1f869, 0x1f870, 0x1f879, 0x1f87f, 0x1f881,
	 0x1f890, 0x1f896, 0x1f8a0, 0x1f8a2, 0x1f8a4, 0x1f8af, 0x1f8b8, 0x1f8b9,
	 0x1f8c0, 0x1f8c1, 0x1f8c3, 0x1f8c4, 0x1f8d0, 0x1f8d0, 0x1f8ec, 0x1f8ec,
	 0x1f8f0, 0x1f8f1, 0x1f910, 0x1f917, 0x1f920, 0x1f921, 0x1f924, 0x1f925,
	 0x1f928, 0x1f929, 0x1f92c, 0x1f92d, 0x1f942, 0x1f944, 0x1f948, 0x1f94a,
	 0x1f94f, 0x1f951, 0x1f954, 0x1f955, 0x1f95d, 0x1f95d, 0x1f962, 0x1f96b,
	 0x1f970, 0x1f971, 0x1f973, 0x1f977, 0x1f97c, 0x1f97c, 0x1f980, 0x1f981,
	 0x1f984, 0x1f986, 0x1f992, 0x1f993, 0x1f996, 0x1f99e, 0x1f9c5, 0x1f9d4,
	 0x1f9f0, 0x1f9f1, 0x1f9f8, 0x1f9fa, 0x1f9fc, 0x1f9fc, 0x1fa00, 0x1fa03,
	 0x20000, 0x20013, 0x20018, 0x2001a, 0x20020, 0x20021, 0x20024, 0x20025,
	 0x2002a, 0x2002c, 0x20030, 0x20031, 0x20034, 0x20036, 0x23801, 0x23801,
	 0x23803, 0x23803, 0x23805, 0x23805, 0x23807, 0x23807, 0x23809, 0x23809,
	 0x2380b, 0x2380b, 0x2380d, 0x2380d, 0x2380f, 0x2380f, 0x23811, 0x23811,
	 0x23813, 0x23813, 0x23815, 0x23815, 0x23817, 0x23817, 0x23819, 0x23819,
	 0x2381b, 0x2381b, 0x2381d, 0x2381d, 0x2381f, 0x23820, 0x23822, 0x23822,
	 0x23824, 0x23824, 0x23826, 0x23826, 0x23828, 0x23828, 0x2382a, 0x2382a,
	 0x2382c, 0x2382c, 0x2382e, 0x2382e, 0x23830, 0x23830, 0x23832, 0x23832,
	 0x23834, 0x23834, 0x23836, 0x23836, 0x23838, 0x23838, 0x2383a, 0x2383a,
	 0x2383c, 0x2383c, 0x2383e, 0x2383e, 0x23840, 0x23847, 0x23b00, 0x23b01,
	 0x23b03, 0x23b03, 0x23b05, 0x23b0e, 0x23b10, 0x23b13, 0x23b15, 0x23b16,
	 0x23b28, 0x23b28, 0x23b30, 0x23b30,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_gmu_registers), 8));

/*
 * Block   : ['GMUGX']
 * Pipeline: A7XX_PIPE_NONE
 * pairs   : 44 (Regs:454)
 */
static const u32 gen7_9_0_gmugx_registers[] = {
	 0x1a400, 0x1a41f, 0x1a440, 0x1a45f, 0x1a480, 0x1a49f, 0x1a4c0, 0x1a4df,
	 0x1a500, 0x1a51f, 0x1a540, 0x1a55f, 0x1a580, 0x1a59f, 0x1a600, 0x1a61f,
	 0x1a640, 0x1a65f, 0x1a780, 0x1a781, 0x1a783, 0x1a785, 0x1a787, 0x1a789,
	 0x1a78b, 0x1a78d, 0x1a78f, 0x1a791, 0x1a793, 0x1a795, 0x1a797, 0x1a799,
	 0x1a79b, 0x1a79d, 0x1a79f, 0x1a7a1, 0x1a7a3, 0x1a7a3, 0x1a7a8, 0x1a7b9,
	 0x1a7c0, 0x1a7c1, 0x1a7c4, 0x1a7c5, 0x1a7c8, 0x1a7c9, 0x1a7cc, 0x1a7cd,
	 0x1a7d0, 0x1a7d1, 0x1a7d4, 0x1a7d5, 0x1a7d8, 0x1a7d9, 0x1a7dc, 0x1a7dd,
	 0x1a7e0, 0x1a7e1, 0x1a7fc, 0x1a7fd, 0x1a800, 0x1a808, 0x1a816, 0x1a816,
	 0x1a81e, 0x1a81e, 0x1a826, 0x1a826, 0x1a82e, 0x1a82e, 0x1a836, 0x1a836,
	 0x1a83e, 0x1a83e, 0x1a846, 0x1a846, 0x1a84e, 0x1a84e, 0x1a856, 0x1a856,
	 0x1a883, 0x1a884, 0x1a890, 0x1a8b3, 0x1a900, 0x1a92b, 0x1a940, 0x1a940,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_gmugx_registers), 8));

/*
 * Block   : ['CX_MISC']
 * Pipeline: A7XX_PIPE_NONE
 * pairs   : 7 (Regs:56)
 */
static const u32 gen7_9_0_cx_misc_registers[] = {
	 0x27800, 0x27800, 0x27810, 0x27814, 0x27820, 0x27824, 0x27828, 0x2782a,
	 0x27832, 0x27857, 0x27880, 0x27881, 0x27c00, 0x27c01,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_cx_misc_registers), 8));

/*
 * Block   : ['DBGC']
 * Pipeline: A7XX_PIPE_NONE
 * pairs   : 19 (Regs:155)
 */
static const u32 gen7_9_0_dbgc_registers[] = {
	 0x00600, 0x0061c, 0x0061e, 0x00634, 0x00640, 0x00643, 0x0064e, 0x00652,
	 0x00654, 0x0065e, 0x00699, 0x00699, 0x0069b, 0x0069e, 0x006c2, 0x006e4,
	 0x006e6, 0x006e6, 0x006e9, 0x006e9, 0x006eb, 0x006eb, 0x006f1, 0x006f4,
	 0x00700, 0x00707, 0x00718, 0x00718, 0x00720, 0x00729, 0x00740, 0x0074a,
	 0x00758, 0x00758, 0x00760, 0x00762,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_dbgc_registers), 8));

/*
 * Block   : ['CX_DBGC']
 * Pipeline: A7XX_PIPE_NONE
 * pairs   : 7 (Regs:75)
 */
static const u32 gen7_9_0_cx_dbgc_registers[] = {
	 0x18400, 0x1841c, 0x1841e, 0x18434, 0x18440, 0x18443, 0x1844e, 0x18452,
	 0x18454, 0x1845e, 0x18520, 0x18520, 0x18580, 0x18581,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_cx_dbgc_registers), 8));

/*
 * Block   : ['BROADCAST', 'CP', 'CX_DBGC', 'CX_MISC', 'DBGC', 'GBIF']
 * Block   : ['GMUAO', 'GMUCX', 'GMUGX', 'GRAS', 'GXCLKCTL', 'PC']
 * Block   : ['RBBM', 'RDVM', 'UCHE', 'VFD', 'VPC', 'VSC']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_NONE
 * pairs   : 29 (Regs:573)
 */
static const u32 gen7_9_0_non_context_pipe_br_registers[] = {
	 0x00887, 0x0088c, 0x08600, 0x08602, 0x08610, 0x0861b, 0x08620, 0x08620,
	 0x08630, 0x08630, 0x08637, 0x08639, 0x08640, 0x08640, 0x09600, 0x09603,
	 0x0960a, 0x09616, 0x09624, 0x0963a, 0x09640, 0x09640, 0x09e00, 0x09e00,
	 0x09e02, 0x09e07, 0x09e0a, 0x09e16, 0x09e18, 0x09e1a, 0x09e1c, 0x09e1c,
	 0x09e20, 0x09e25, 0x09e30, 0x09e31, 0x09e40, 0x09e51, 0x09e64, 0x09e6c,
	 0x09e70, 0x09e72, 0x09e78, 0x09e79, 0x09e80, 0x09fff, 0x0a600, 0x0a600,
	 0x0a603, 0x0a603, 0x0a610, 0x0a61f, 0x0a630, 0x0a631, 0x0a638, 0x0a63c,
	 0x0a640, 0x0a65f,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_pipe_br_registers), 8));

/*
 * Block   : ['BROADCAST', 'CP', 'CX_DBGC', 'CX_MISC', 'DBGC', 'GBIF']
 * Block   : ['GMUAO', 'GMUCX', 'GMUGX', 'GRAS', 'GXCLKCTL', 'PC']
 * Block   : ['RBBM', 'RDVM', 'UCHE', 'VFD', 'VPC', 'VSC']
 * Pipeline: A7XX_PIPE_BV
 * Cluster : A7XX_CLUSTER_NONE
 * pairs   : 29 (Regs:573)
 */
static const u32 gen7_9_0_non_context_pipe_bv_registers[] = {
	 0x00887, 0x0088c, 0x08600, 0x08602, 0x08610, 0x0861b, 0x08620, 0x08620,
	 0x08630, 0x08630, 0x08637, 0x08639, 0x08640, 0x08640, 0x09600, 0x09603,
	 0x0960a, 0x09616, 0x09624, 0x0963a, 0x09640, 0x09640, 0x09e00, 0x09e00,
	 0x09e02, 0x09e07, 0x09e0a, 0x09e16, 0x09e18, 0x09e1a, 0x09e1c, 0x09e1c,
	 0x09e20, 0x09e25, 0x09e30, 0x09e31, 0x09e40, 0x09e51, 0x09e64, 0x09e6c,
	 0x09e70, 0x09e72, 0x09e78, 0x09e79, 0x09e80, 0x09fff, 0x0a600, 0x0a600,
	 0x0a603, 0x0a603, 0x0a610, 0x0a61f, 0x0a630, 0x0a631, 0x0a638, 0x0a63c,
	 0x0a640, 0x0a65f,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_pipe_bv_registers), 8));

/*
 * Block   : ['BROADCAST', 'CP', 'CX_DBGC', 'CX_MISC', 'DBGC', 'GBIF']
 * Block   : ['GMUAO', 'GMUCX', 'GMUGX', 'GRAS', 'GXCLKCTL', 'PC']
 * Block   : ['RBBM', 'RDVM', 'UCHE', 'VFD', 'VPC', 'VSC']
 * Pipeline: A7XX_PIPE_LPAC
 * Cluster : A7XX_CLUSTER_NONE
 * pairs   : 2 (Regs:7)
 */
static const u32 gen7_9_0_non_context_pipe_lpac_registers[] = {
	 0x00887, 0x0088c, 0x00f80, 0x00f80,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_pipe_lpac_registers), 8));

/*
 * Block   : ['RB']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_NONE
 * pairs   : 5 (Regs:37)
 */
static const u32 gen7_9_0_non_context_rb_pipe_br_rac_registers[] = {
	 0x08e10, 0x08e1c, 0x08e20, 0x08e25, 0x08e51, 0x08e5a, 0x08e6a, 0x08e6d,
	 0x08ea0, 0x08ea3,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_rb_pipe_br_rac_registers), 8));

/*
 * Block   : ['RB']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_NONE
 * pairs   : 15 (Regs:66)
 */
static const u32 gen7_9_0_non_context_rb_pipe_br_rbp_registers[] = {
	 0x08e01, 0x08e01, 0x08e04, 0x08e04, 0x08e06, 0x08e09, 0x08e0c, 0x08e0c,
	 0x08e28, 0x08e28, 0x08e2c, 0x08e35, 0x08e3b, 0x08e40, 0x08e50, 0x08e50,
	 0x08e5b, 0x08e5d, 0x08e5f, 0x08e5f, 0x08e61, 0x08e61, 0x08e63, 0x08e66,
	 0x08e68, 0x08e69, 0x08e70, 0x08e7d, 0x08e80, 0x08e8f,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_rb_pipe_br_rbp_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_NONE
 * Location: A7XX_HLSQ_STATE
 * pairs   : 4 (Regs:28)
 */
static const u32 gen7_9_0_non_context_sp_pipe_br_hlsq_state_registers[] = {
	 0x0ae52, 0x0ae52, 0x0ae60, 0x0ae67, 0x0ae69, 0x0ae75, 0x0aec0, 0x0aec5,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_sp_pipe_br_hlsq_state_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_NONE
 * Location: A7XX_SP_TOP
 * pairs   : 10 (Regs:61)
 */
static const u32 gen7_9_0_non_context_sp_pipe_br_sp_top_registers[] = {
	 0x0ae00, 0x0ae00, 0x0ae02, 0x0ae04, 0x0ae06, 0x0ae0a, 0x0ae0c, 0x0ae0c,
	 0x0ae0f, 0x0ae0f, 0x0ae28, 0x0ae2b, 0x0ae35, 0x0ae35, 0x0ae3a, 0x0ae3f,
	 0x0ae50, 0x0ae52, 0x0ae80, 0x0aea3,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_sp_pipe_br_sp_top_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_NONE
 * Location: A7XX_USPTP
 * pairs   : 12 (Regs:62)
 */
static const u32 gen7_9_0_non_context_sp_pipe_br_usptp_registers[] = {
	 0x0ae00, 0x0ae00, 0x0ae02, 0x0ae04, 0x0ae06, 0x0ae0a, 0x0ae0c, 0x0ae0c,
	 0x0ae0f, 0x0ae0f, 0x0ae28, 0x0ae2b, 0x0ae30, 0x0ae32, 0x0ae35, 0x0ae35,
	 0x0ae3a, 0x0ae3b, 0x0ae3e, 0x0ae3f, 0x0ae50, 0x0ae52, 0x0ae80, 0x0aea3,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_sp_pipe_br_usptp_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_NONE
 * Location: A7XX_HLSQ_DP_STR
 * pairs   : 2 (Regs:5)
 */
static const u32 gen7_9_0_non_context_sp_pipe_br_hlsq_dp_str_registers[] = {
	 0x0ae6b, 0x0ae6c, 0x0ae73, 0x0ae75,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_sp_pipe_br_hlsq_dp_str_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_LPAC
 * Cluster : A7XX_CLUSTER_NONE
 * Location: A7XX_HLSQ_STATE
 * pairs   : 1 (Regs:5)
 */
static const u32 gen7_9_0_non_context_sp_pipe_lpac_hlsq_state_registers[] = {
	 0x0af88, 0x0af8c,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_sp_pipe_lpac_hlsq_state_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_LPAC
 * Cluster : A7XX_CLUSTER_NONE
 * Location: A7XX_SP_TOP
 * pairs   : 1 (Regs:6)
 */
static const u32 gen7_9_0_non_context_sp_pipe_lpac_sp_top_registers[] = {
	 0x0af80, 0x0af85,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_sp_pipe_lpac_sp_top_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_LPAC
 * Cluster : A7XX_CLUSTER_NONE
 * Location: A7XX_USPTP
 * pairs   : 2 (Regs:9)
 */
static const u32 gen7_9_0_non_context_sp_pipe_lpac_usptp_registers[] = {
	 0x0af80, 0x0af85, 0x0af90, 0x0af92,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_sp_pipe_lpac_usptp_registers), 8));

/*
 * Block   : ['TPL1']
 * Pipeline: A7XX_PIPE_NONE
 * Cluster : A7XX_CLUSTER_NONE
 * Location: A7XX_USPTP
 * pairs   : 5 (Regs:29)
 */
static const u32 gen7_9_0_non_context_tpl1_pipe_none_usptp_registers[] = {
	 0x0b602, 0x0b602, 0x0b604, 0x0b604, 0x0b608, 0x0b60c, 0x0b610, 0x0b621,
	 0x0b630, 0x0b633,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_tpl1_pipe_none_usptp_registers), 8));

/*
 * Block   : ['TPL1']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_NONE
 * Location: A7XX_USPTP
 * pairs   : 1 (Regs:1)
 */
static const u32 gen7_9_0_non_context_tpl1_pipe_br_usptp_registers[] = {
	 0x0b600, 0x0b600,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_tpl1_pipe_br_usptp_registers), 8));

/*
 * Block   : ['TPL1']
 * Pipeline: A7XX_PIPE_LPAC
 * Cluster : A7XX_CLUSTER_NONE
 * Location: A7XX_USPTP
 * pairs   : 1 (Regs:1)
 */
static const u32 gen7_9_0_non_context_tpl1_pipe_lpac_usptp_registers[] = {
	 0x0b780, 0x0b780,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_non_context_tpl1_pipe_lpac_usptp_registers), 8));

/*
 * Block   : ['GRAS']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_GRAS
 * pairs   : 14 (Regs:293)
 */
static const u32 gen7_9_0_gras_pipe_br_cluster_gras_registers[] = {
	 0x08000, 0x0800c, 0x08010, 0x08092, 0x08094, 0x08099, 0x0809b, 0x0809d,
	 0x080a0, 0x080a7, 0x080af, 0x080f1, 0x080f4, 0x080f6, 0x080f8, 0x080fa,
	 0x08100, 0x08107, 0x08109, 0x0810b, 0x08110, 0x08116, 0x08120, 0x0813f,
	 0x08400, 0x08406, 0x0840a, 0x0840b,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_gras_pipe_br_cluster_gras_registers), 8));

/*
 * Block   : ['GRAS']
 * Pipeline: A7XX_PIPE_BV
 * Cluster : A7XX_CLUSTER_GRAS
 * pairs   : 14 (Regs:293)
 */
static const u32 gen7_9_0_gras_pipe_bv_cluster_gras_registers[] = {
	 0x08000, 0x0800c, 0x08010, 0x08092, 0x08094, 0x08099, 0x0809b, 0x0809d,
	 0x080a0, 0x080a7, 0x080af, 0x080f1, 0x080f4, 0x080f6, 0x080f8, 0x080fa,
	 0x08100, 0x08107, 0x08109, 0x0810b, 0x08110, 0x08116, 0x08120, 0x0813f,
	 0x08400, 0x08406, 0x0840a, 0x0840b,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_gras_pipe_bv_cluster_gras_registers), 8));

/*
 * Block   : ['PC']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_FE
 * pairs   : 6 (Regs:31)
 */
static const u32 gen7_9_0_pc_pipe_br_cluster_fe_registers[] = {
	 0x09800, 0x09804, 0x09806, 0x0980a, 0x09810, 0x09811, 0x09884, 0x09886,
	 0x09970, 0x09972, 0x09b00, 0x09b0c,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_pc_pipe_br_cluster_fe_registers), 8));

/*
 * Block   : ['PC']
 * Pipeline: A7XX_PIPE_BV
 * Cluster : A7XX_CLUSTER_FE
 * pairs   : 6 (Regs:31)
 */
static const u32 gen7_9_0_pc_pipe_bv_cluster_fe_registers[] = {
	 0x09800, 0x09804, 0x09806, 0x0980a, 0x09810, 0x09811, 0x09884, 0x09886,
	 0x09970, 0x09972, 0x09b00, 0x09b0c,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_pc_pipe_bv_cluster_fe_registers), 8));

/*
 * Block   : ['VFD']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_FE
 * pairs   : 2 (Regs:236)
 */
static const u32 gen7_9_0_vfd_pipe_br_cluster_fe_registers[] = {
	 0x0a000, 0x0a009, 0x0a00e, 0x0a0ef,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_vfd_pipe_br_cluster_fe_registers), 8));

/*
 * Block   : ['VFD']
 * Pipeline: A7XX_PIPE_BV
 * Cluster : A7XX_CLUSTER_FE
 * pairs   : 2 (Regs:236)
 */
static const u32 gen7_9_0_vfd_pipe_bv_cluster_fe_registers[] = {
	 0x0a000, 0x0a009, 0x0a00e, 0x0a0ef,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_vfd_pipe_bv_cluster_fe_registers), 8));

/*
 * Block   : ['VPC']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_FE
 * pairs   : 2 (Regs:18)
 */
static const u32 gen7_9_0_vpc_pipe_br_cluster_fe_registers[] = {
	 0x09300, 0x0930a, 0x09311, 0x09317,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_vpc_pipe_br_cluster_fe_registers), 8));

/*
 * Block   : ['VPC']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_PC_VS
 * pairs   : 3 (Regs:30)
 */
static const u32 gen7_9_0_vpc_pipe_br_cluster_pc_vs_registers[] = {
	 0x09101, 0x0910c, 0x09300, 0x0930a, 0x09311, 0x09317,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_vpc_pipe_br_cluster_pc_vs_registers), 8));

/*
 * Block   : ['VPC']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_VPC_PS
 * pairs   : 5 (Regs:76)
 */
static const u32 gen7_9_0_vpc_pipe_br_cluster_vpc_ps_registers[] = {
	 0x09200, 0x0920f, 0x09212, 0x09216, 0x09218, 0x0923c, 0x09300, 0x0930a,
	 0x09311, 0x09317,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_vpc_pipe_br_cluster_vpc_ps_registers), 8));

/*
 * Block   : ['VPC']
 * Pipeline: A7XX_PIPE_BV
 * Cluster : A7XX_CLUSTER_FE
 * pairs   : 2 (Regs:18)
 */
static const u32 gen7_9_0_vpc_pipe_bv_cluster_fe_registers[] = {
	 0x09300, 0x0930a, 0x09311, 0x09317,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_vpc_pipe_bv_cluster_fe_registers), 8));

/*
 * Block   : ['VPC']
 * Pipeline: A7XX_PIPE_BV
 * Cluster : A7XX_CLUSTER_PC_VS
 * pairs   : 3 (Regs:30)
 */
static const u32 gen7_9_0_vpc_pipe_bv_cluster_pc_vs_registers[] = {
	 0x09101, 0x0910c, 0x09300, 0x0930a, 0x09311, 0x09317,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_vpc_pipe_bv_cluster_pc_vs_registers), 8));

/*
 * Block   : ['VPC']
 * Pipeline: A7XX_PIPE_BV
 * Cluster : A7XX_CLUSTER_VPC_PS
 * pairs   : 5 (Regs:76)
 */
static const u32 gen7_9_0_vpc_pipe_bv_cluster_vpc_ps_registers[] = {
	 0x09200, 0x0920f, 0x09212, 0x09216, 0x09218, 0x0923c, 0x09300, 0x0930a,
	 0x09311, 0x09317,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_vpc_pipe_bv_cluster_vpc_ps_registers), 8));

/*
 * Block   : ['RB']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_PS
 * pairs   : 39 (Regs:133)
 */
static const u32 gen7_9_0_rb_pipe_br_cluster_ps_rac_registers[] = {
	 0x08802, 0x08802, 0x08804, 0x08806, 0x08809, 0x0880a, 0x0880e, 0x08811,
	 0x08818, 0x0881e, 0x08821, 0x08821, 0x08823, 0x08826, 0x08829, 0x08829,
	 0x0882b, 0x0882e, 0x08831, 0x08831, 0x08833, 0x08836, 0x08839, 0x08839,
	 0x0883b, 0x0883e, 0x08841, 0x08841, 0x08843, 0x08846, 0x08849, 0x08849,
	 0x0884b, 0x0884e, 0x08851, 0x08851, 0x08853, 0x08856, 0x08859, 0x08859,
	 0x0885b, 0x0885e, 0x08860, 0x08864, 0x08870, 0x08870, 0x08873, 0x08876,
	 0x08878, 0x08879, 0x08882, 0x08885, 0x08887, 0x08889, 0x08891, 0x08891,
	 0x08898, 0x08899, 0x088c0, 0x088c1, 0x088e5, 0x088e5, 0x088f4, 0x088f5,
	 0x08a00, 0x08a05, 0x08a10, 0x08a15, 0x08a20, 0x08a25, 0x08a30, 0x08a35,
	 0x08c00, 0x08c01, 0x08c18, 0x08c1f, 0x08c26, 0x08c34,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_rb_pipe_br_cluster_ps_rac_registers), 8));

/*
 * Block   : ['RB']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_PS
 * pairs   : 34 (Regs:100)
 */
static const u32 gen7_9_0_rb_pipe_br_cluster_ps_rbp_registers[] = {
	 0x08800, 0x08801, 0x08803, 0x08803, 0x0880b, 0x0880d, 0x08812, 0x08812,
	 0x08820, 0x08820, 0x08822, 0x08822, 0x08827, 0x08828, 0x0882a, 0x0882a,
	 0x0882f, 0x08830, 0x08832, 0x08832, 0x08837, 0x08838, 0x0883a, 0x0883a,
	 0x0883f, 0x08840, 0x08842, 0x08842, 0x08847, 0x08848, 0x0884a, 0x0884a,
	 0x0884f, 0x08850, 0x08852, 0x08852, 0x08857, 0x08858, 0x0885a, 0x0885a,
	 0x0885f, 0x0885f, 0x08865, 0x08865, 0x08871, 0x08872, 0x08877, 0x08877,
	 0x08880, 0x08881, 0x08886, 0x08886, 0x08890, 0x08890, 0x088d0, 0x088e4,
	 0x088e8, 0x088ea, 0x088f0, 0x088f0, 0x08900, 0x0891a, 0x08927, 0x08928,
	 0x08c17, 0x08c17, 0x08c20, 0x08c25,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_rb_pipe_br_cluster_ps_rbp_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_SP_VS
 * Location: A7XX_HLSQ_STATE
 * pairs   : 29 (Regs:215)
 */
static const u32 gen7_9_0_sp_pipe_br_cluster_sp_vs_hlsq_state_registers[] = {
	 0x0a800, 0x0a801, 0x0a81b, 0x0a81d, 0x0a822, 0x0a822, 0x0a824, 0x0a824,
	 0x0a827, 0x0a82a, 0x0a830, 0x0a830, 0x0a832, 0x0a835, 0x0a83a, 0x0a83a,
	 0x0a83c, 0x0a83c, 0x0a83f, 0x0a841, 0x0a85b, 0x0a85d, 0x0a862, 0x0a862,
	 0x0a864, 0x0a864, 0x0a867, 0x0a867, 0x0a870, 0x0a870, 0x0a872, 0x0a872,
	 0x0a88c, 0x0a88e, 0x0a893, 0x0a893, 0x0a895, 0x0a895, 0x0a898, 0x0a898,
	 0x0a89a, 0x0a89d, 0x0a8a0, 0x0a8af, 0x0a8c0, 0x0a8c3, 0x0a974, 0x0a977,
	 0x0ab00, 0x0ab03, 0x0ab05, 0x0ab05, 0x0ab0a, 0x0ab1b, 0x0ab20, 0x0ab20,
	 0x0ab40, 0x0abbf,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_br_cluster_sp_vs_hlsq_state_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_SP_VS
 * Location: A7XX_SP_TOP
 * pairs   : 22 (Regs:73)
 */
static const u32 gen7_9_0_sp_pipe_br_cluster_sp_vs_sp_top_registers[] = {
	 0x0a800, 0x0a800, 0x0a81c, 0x0a81d, 0x0a822, 0x0a824, 0x0a82d, 0x0a82d,
	 0x0a82f, 0x0a831, 0x0a834, 0x0a835, 0x0a83a, 0x0a83c, 0x0a840, 0x0a840,
	 0x0a85c, 0x0a85d, 0x0a862, 0x0a864, 0x0a868, 0x0a868, 0x0a870, 0x0a871,
	 0x0a88d, 0x0a88e, 0x0a893, 0x0a895, 0x0a899, 0x0a899, 0x0a8a0, 0x0a8af,
	 0x0a974, 0x0a977, 0x0ab00, 0x0ab00, 0x0ab02, 0x0ab02, 0x0ab04, 0x0ab05,
	 0x0ab0a, 0x0ab1b, 0x0ab20, 0x0ab20,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_br_cluster_sp_vs_sp_top_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_SP_VS
 * Location: A7XX_USPTP
 * pairs   : 16 (Regs:269)
 */
static const u32 gen7_9_0_sp_pipe_br_cluster_sp_vs_usptp_registers[] = {
	 0x0a800, 0x0a81b, 0x0a81e, 0x0a821, 0x0a823, 0x0a827, 0x0a82d, 0x0a82d,
	 0x0a82f, 0x0a833, 0x0a836, 0x0a839, 0x0a83b, 0x0a85b, 0x0a85e, 0x0a861,
	 0x0a863, 0x0a868, 0x0a870, 0x0a88c, 0x0a88f, 0x0a892, 0x0a894, 0x0a899,
	 0x0a8c0, 0x0a8c3, 0x0ab00, 0x0ab05, 0x0ab21, 0x0ab22, 0x0ab40, 0x0abbf,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_br_cluster_sp_vs_usptp_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_SP_PS
 * Location: A7XX_HLSQ_STATE
 * pairs   : 21 (Regs:334)
 */
static const u32 gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_state_registers[] = {
	 0x0a980, 0x0a984, 0x0a99e, 0x0a99e, 0x0a9a7, 0x0a9a7, 0x0a9aa, 0x0a9aa,
	 0x0a9ae, 0x0a9b0, 0x0a9b2, 0x0a9b5, 0x0a9ba, 0x0a9ba, 0x0a9bc, 0x0a9bc,
	 0x0a9c4, 0x0a9c4, 0x0a9c6, 0x0a9c6, 0x0a9cd, 0x0a9cd, 0x0a9e0, 0x0a9fc,
	 0x0aa00, 0x0aa00, 0x0aa30, 0x0aa31, 0x0aa40, 0x0aabf, 0x0aaf2, 0x0aaf3,
	 0x0ab00, 0x0ab03, 0x0ab05, 0x0ab05, 0x0ab0a, 0x0ab1b, 0x0ab20, 0x0ab20,
	 0x0ab40, 0x0abbf,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_state_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_SP_PS
 * Location: A7XX_HLSQ_DP
 * pairs   : 3 (Regs:19)
 */
static const u32 gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_dp_registers[] = {
	 0x0a9b1, 0x0a9b1, 0x0a9c6, 0x0a9cb, 0x0a9d4, 0x0a9df,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_dp_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_SP_PS
 * Location: A7XX_SP_TOP
 * pairs   : 18 (Regs:77)
 */
static const u32 gen7_9_0_sp_pipe_br_cluster_sp_ps_sp_top_registers[] = {
	 0x0a980, 0x0a980, 0x0a982, 0x0a984, 0x0a99e, 0x0a9a2, 0x0a9a7, 0x0a9a8,
	 0x0a9aa, 0x0a9aa, 0x0a9ae, 0x0a9ae, 0x0a9b0, 0x0a9b1, 0x0a9b3, 0x0a9b5,
	 0x0a9ba, 0x0a9bc, 0x0a9c5, 0x0a9c5, 0x0a9e0, 0x0a9f9, 0x0aa00, 0x0aa03,
	 0x0aaf2, 0x0aaf3, 0x0ab00, 0x0ab00, 0x0ab02, 0x0ab02, 0x0ab04, 0x0ab05,
	 0x0ab0a, 0x0ab1b, 0x0ab20, 0x0ab20,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_br_cluster_sp_ps_sp_top_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_SP_PS
 * Location: A7XX_USPTP
 * pairs   : 17 (Regs:333)
 */
static const u32 gen7_9_0_sp_pipe_br_cluster_sp_ps_usptp_registers[] = {
	 0x0a980, 0x0a982, 0x0a985, 0x0a9a6, 0x0a9a8, 0x0a9a9, 0x0a9ab, 0x0a9ae,
	 0x0a9b0, 0x0a9b3, 0x0a9b6, 0x0a9b9, 0x0a9bb, 0x0a9bf, 0x0a9c2, 0x0a9c3,
	 0x0a9c5, 0x0a9c5, 0x0a9cd, 0x0a9cd, 0x0a9d0, 0x0a9d3, 0x0aa01, 0x0aa03,
	 0x0aa30, 0x0aa31, 0x0aa40, 0x0aabf, 0x0ab00, 0x0ab05, 0x0ab21, 0x0ab22,
	 0x0ab40, 0x0abbf,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_br_cluster_sp_ps_usptp_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_SP_PS
 * Location: A7XX_HLSQ_DP_STR
 * pairs   : 1 (Regs:6)
 */
static const u32 gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_dp_str_registers[] = {
	 0x0a9c6, 0x0a9cb,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_dp_str_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BV
 * Cluster : A7XX_CLUSTER_SP_VS
 * Location: A7XX_HLSQ_STATE
 * pairs   : 28 (Regs:213)
 */
static const u32 gen7_9_0_sp_pipe_bv_cluster_sp_vs_hlsq_state_registers[] = {
	 0x0a800, 0x0a801, 0x0a81b, 0x0a81d, 0x0a822, 0x0a822, 0x0a824, 0x0a824,
	 0x0a827, 0x0a82a, 0x0a830, 0x0a830, 0x0a832, 0x0a835, 0x0a83a, 0x0a83a,
	 0x0a83c, 0x0a83c, 0x0a83f, 0x0a841, 0x0a85b, 0x0a85d, 0x0a862, 0x0a862,
	 0x0a864, 0x0a864, 0x0a867, 0x0a867, 0x0a870, 0x0a870, 0x0a872, 0x0a872,
	 0x0a88c, 0x0a88e, 0x0a893, 0x0a893, 0x0a895, 0x0a895, 0x0a898, 0x0a898,
	 0x0a89a, 0x0a89d, 0x0a8a0, 0x0a8af, 0x0a8c0, 0x0a8c3, 0x0a974, 0x0a977,
	 0x0ab00, 0x0ab02, 0x0ab0a, 0x0ab1b, 0x0ab20, 0x0ab20, 0x0ab40, 0x0abbf,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_bv_cluster_sp_vs_hlsq_state_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BV
 * Cluster : A7XX_CLUSTER_SP_VS
 * Location: A7XX_SP_TOP
 * pairs   : 21 (Regs:71)
 */
static const u32 gen7_9_0_sp_pipe_bv_cluster_sp_vs_sp_top_registers[] = {
	 0x0a800, 0x0a800, 0x0a81c, 0x0a81d, 0x0a822, 0x0a824, 0x0a82d, 0x0a82d,
	 0x0a82f, 0x0a831, 0x0a834, 0x0a835, 0x0a83a, 0x0a83c, 0x0a840, 0x0a840,
	 0x0a85c, 0x0a85d, 0x0a862, 0x0a864, 0x0a868, 0x0a868, 0x0a870, 0x0a871,
	 0x0a88d, 0x0a88e, 0x0a893, 0x0a895, 0x0a899, 0x0a899, 0x0a8a0, 0x0a8af,
	 0x0a974, 0x0a977, 0x0ab00, 0x0ab00, 0x0ab02, 0x0ab02, 0x0ab0a, 0x0ab1b,
	 0x0ab20, 0x0ab20,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_bv_cluster_sp_vs_sp_top_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_BV
 * Cluster : A7XX_CLUSTER_SP_VS
 * Location: A7XX_USPTP
 * pairs   : 16 (Regs:266)
 */
static const u32 gen7_9_0_sp_pipe_bv_cluster_sp_vs_usptp_registers[] = {
	 0x0a800, 0x0a81b, 0x0a81e, 0x0a821, 0x0a823, 0x0a827, 0x0a82d, 0x0a82d,
	 0x0a82f, 0x0a833, 0x0a836, 0x0a839, 0x0a83b, 0x0a85b, 0x0a85e, 0x0a861,
	 0x0a863, 0x0a868, 0x0a870, 0x0a88c, 0x0a88f, 0x0a892, 0x0a894, 0x0a899,
	 0x0a8c0, 0x0a8c3, 0x0ab00, 0x0ab02, 0x0ab21, 0x0ab22, 0x0ab40, 0x0abbf,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_bv_cluster_sp_vs_usptp_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_LPAC
 * Cluster : A7XX_CLUSTER_SP_PS
 * Location: A7XX_HLSQ_STATE
 * pairs   : 14 (Regs:299)
 */
static const u32 gen7_9_0_sp_pipe_lpac_cluster_sp_ps_hlsq_state_registers[] = {
	 0x0a9b0, 0x0a9b0, 0x0a9b2, 0x0a9b5, 0x0a9ba, 0x0a9ba, 0x0a9bc, 0x0a9bc,
	 0x0a9c4, 0x0a9c4, 0x0a9cd, 0x0a9cd, 0x0a9e2, 0x0a9e3, 0x0a9e6, 0x0a9fc,
	 0x0aa00, 0x0aa00, 0x0aa31, 0x0aa35, 0x0aa40, 0x0aabf, 0x0aaf3, 0x0aaf3,
	 0x0ab00, 0x0ab01, 0x0ab40, 0x0abbf,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_lpac_cluster_sp_ps_hlsq_state_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_LPAC
 * Cluster : A7XX_CLUSTER_SP_PS
 * Location: A7XX_HLSQ_DP
 * pairs   : 2 (Regs:13)
 */
static const u32 gen7_9_0_sp_pipe_lpac_cluster_sp_ps_hlsq_dp_registers[] = {
	 0x0a9b1, 0x0a9b1, 0x0a9d4, 0x0a9df,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_lpac_cluster_sp_ps_hlsq_dp_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_LPAC
 * Cluster : A7XX_CLUSTER_SP_PS
 * Location: A7XX_SP_TOP
 * pairs   : 9 (Regs:34)
 */
static const u32 gen7_9_0_sp_pipe_lpac_cluster_sp_ps_sp_top_registers[] = {
	 0x0a9b0, 0x0a9b1, 0x0a9b3, 0x0a9b5, 0x0a9ba, 0x0a9bc, 0x0a9c5, 0x0a9c5,
	 0x0a9e2, 0x0a9e3, 0x0a9e6, 0x0a9f9, 0x0aa00, 0x0aa00, 0x0aaf3, 0x0aaf3,
	 0x0ab00, 0x0ab00,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_lpac_cluster_sp_ps_sp_top_registers), 8));

/*
 * Block   : ['SP']
 * Pipeline: A7XX_PIPE_LPAC
 * Cluster : A7XX_CLUSTER_SP_PS
 * Location: A7XX_USPTP
 * pairs   : 11 (Regs:279)
 */
static const u32 gen7_9_0_sp_pipe_lpac_cluster_sp_ps_usptp_registers[] = {
	 0x0a9b0, 0x0a9b3, 0x0a9b6, 0x0a9b9, 0x0a9bb, 0x0a9be, 0x0a9c2, 0x0a9c3,
	 0x0a9c5, 0x0a9c5, 0x0a9cd, 0x0a9cd, 0x0a9d0, 0x0a9d3, 0x0aa31, 0x0aa31,
	 0x0aa40, 0x0aabf, 0x0ab00, 0x0ab01, 0x0ab40, 0x0abbf,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_sp_pipe_lpac_cluster_sp_ps_usptp_registers), 8));

/*
 * Block   : ['TPL1']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_SP_VS
 * Location: A7XX_USPTP
 * pairs   : 3 (Regs:10)
 */
static const u32 gen7_9_0_tpl1_pipe_br_cluster_sp_vs_usptp_registers[] = {
	 0x0b300, 0x0b307, 0x0b309, 0x0b309, 0x0b310, 0x0b310,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_tpl1_pipe_br_cluster_sp_vs_usptp_registers), 8));

/*
 * Block   : ['TPL1']
 * Pipeline: A7XX_PIPE_BR
 * Cluster : A7XX_CLUSTER_SP_PS
 * Location: A7XX_USPTP
 * pairs   : 6 (Regs:42)
 */
static const u32 gen7_9_0_tpl1_pipe_br_cluster_sp_ps_usptp_registers[] = {
	 0x0b180, 0x0b183, 0x0b190, 0x0b195, 0x0b2c0, 0x0b2d5, 0x0b300, 0x0b307,
	 0x0b309, 0x0b309, 0x0b310, 0x0b310,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_tpl1_pipe_br_cluster_sp_ps_usptp_registers), 8));

/*
 * Block   : ['TPL1']
 * Pipeline: A7XX_PIPE_BV
 * Cluster : A7XX_CLUSTER_SP_VS
 * Location: A7XX_USPTP
 * pairs   : 3 (Regs:10)
 */
static const u32 gen7_9_0_tpl1_pipe_bv_cluster_sp_vs_usptp_registers[] = {
	 0x0b300, 0x0b307, 0x0b309, 0x0b309, 0x0b310, 0x0b310,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_tpl1_pipe_bv_cluster_sp_vs_usptp_registers), 8));

/*
 * Block   : ['TPL1']
 * Pipeline: A7XX_PIPE_LPAC
 * Cluster : A7XX_CLUSTER_SP_PS
 * Location: A7XX_USPTP
 * pairs   : 5 (Regs:7)
 */
static const u32 gen7_9_0_tpl1_pipe_lpac_cluster_sp_ps_usptp_registers[] = {
	 0x0b180, 0x0b181, 0x0b300, 0x0b301, 0x0b307, 0x0b307, 0x0b309, 0x0b309,
	 0x0b310, 0x0b310,
	 UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_tpl1_pipe_lpac_cluster_sp_ps_usptp_registers), 8));

static const struct gen7_sel_reg gen7_9_0_rb_rac_sel = {
	.host_reg = REG_A6XX_RB_RB_SUB_BLOCK_SEL_CNTL_HOST,
	.cd_reg = REG_A6XX_RB_RB_SUB_BLOCK_SEL_CNTL_CD,
	.val = 0,
};

static const struct gen7_sel_reg gen7_9_0_rb_rbp_sel = {
	.host_reg = REG_A6XX_RB_RB_SUB_BLOCK_SEL_CNTL_HOST,
	.cd_reg = REG_A6XX_RB_RB_SUB_BLOCK_SEL_CNTL_CD,
	.val = 0x9,
};

static struct gen7_cluster_registers gen7_9_0_clusters[] = {
	{ A7XX_CLUSTER_NONE, A7XX_PIPE_BR, STATE_NON_CONTEXT,
		gen7_9_0_non_context_pipe_br_registers,  },
	{ A7XX_CLUSTER_NONE, A7XX_PIPE_BV, STATE_NON_CONTEXT,
		gen7_9_0_non_context_pipe_bv_registers,  },
	{ A7XX_CLUSTER_NONE, A7XX_PIPE_LPAC, STATE_NON_CONTEXT,
		gen7_9_0_non_context_pipe_lpac_registers,  },
	{ A7XX_CLUSTER_NONE, A7XX_PIPE_BR, STATE_NON_CONTEXT,
		gen7_9_0_non_context_rb_pipe_br_rac_registers, &gen7_9_0_rb_rac_sel, },
	{ A7XX_CLUSTER_NONE, A7XX_PIPE_BR, STATE_NON_CONTEXT,
		gen7_9_0_non_context_rb_pipe_br_rbp_registers, &gen7_9_0_rb_rbp_sel, },
	{ A7XX_CLUSTER_PS, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_9_0_rb_pipe_br_cluster_ps_rac_registers, &gen7_9_0_rb_rac_sel, },
	{ A7XX_CLUSTER_PS, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_9_0_rb_pipe_br_cluster_ps_rac_registers, &gen7_9_0_rb_rac_sel, },
	{ A7XX_CLUSTER_PS, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_9_0_rb_pipe_br_cluster_ps_rbp_registers, &gen7_9_0_rb_rbp_sel, },
	{ A7XX_CLUSTER_PS, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_9_0_rb_pipe_br_cluster_ps_rbp_registers, &gen7_9_0_rb_rbp_sel, },
	{ A7XX_CLUSTER_GRAS, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_9_0_gras_pipe_br_cluster_gras_registers,  },
	{ A7XX_CLUSTER_GRAS, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_9_0_gras_pipe_br_cluster_gras_registers,  },
	{ A7XX_CLUSTER_GRAS, A7XX_PIPE_BV, STATE_FORCE_CTXT_0,
		gen7_9_0_gras_pipe_bv_cluster_gras_registers,  },
	{ A7XX_CLUSTER_GRAS, A7XX_PIPE_BV, STATE_FORCE_CTXT_1,
		gen7_9_0_gras_pipe_bv_cluster_gras_registers,  },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_9_0_pc_pipe_br_cluster_fe_registers,  },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_9_0_pc_pipe_br_cluster_fe_registers,  },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BV, STATE_FORCE_CTXT_0,
		gen7_9_0_pc_pipe_bv_cluster_fe_registers,  },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BV, STATE_FORCE_CTXT_1,
		gen7_9_0_pc_pipe_bv_cluster_fe_registers,  },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_9_0_vfd_pipe_br_cluster_fe_registers,  },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_9_0_vfd_pipe_br_cluster_fe_registers,  },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BV, STATE_FORCE_CTXT_0,
		gen7_9_0_vfd_pipe_bv_cluster_fe_registers,  },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BV, STATE_FORCE_CTXT_1,
		gen7_9_0_vfd_pipe_bv_cluster_fe_registers,  },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_9_0_vpc_pipe_br_cluster_fe_registers,  },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_9_0_vpc_pipe_br_cluster_fe_registers,  },
	{ A7XX_CLUSTER_PC_VS, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_9_0_vpc_pipe_br_cluster_pc_vs_registers,  },
	{ A7XX_CLUSTER_PC_VS, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_9_0_vpc_pipe_br_cluster_pc_vs_registers,  },
	{ A7XX_CLUSTER_VPC_PS, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_9_0_vpc_pipe_br_cluster_vpc_ps_registers,  },
	{ A7XX_CLUSTER_VPC_PS, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_9_0_vpc_pipe_br_cluster_vpc_ps_registers,  },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BV, STATE_FORCE_CTXT_0,
		gen7_9_0_vpc_pipe_bv_cluster_fe_registers,  },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BV, STATE_FORCE_CTXT_1,
		gen7_9_0_vpc_pipe_bv_cluster_fe_registers,  },
	{ A7XX_CLUSTER_PC_VS, A7XX_PIPE_BV, STATE_FORCE_CTXT_0,
		gen7_9_0_vpc_pipe_bv_cluster_pc_vs_registers,  },
	{ A7XX_CLUSTER_PC_VS, A7XX_PIPE_BV, STATE_FORCE_CTXT_1,
		gen7_9_0_vpc_pipe_bv_cluster_pc_vs_registers,  },
	{ A7XX_CLUSTER_VPC_PS, A7XX_PIPE_BV, STATE_FORCE_CTXT_0,
		gen7_9_0_vpc_pipe_bv_cluster_vpc_ps_registers,  },
	{ A7XX_CLUSTER_VPC_PS, A7XX_PIPE_BV, STATE_FORCE_CTXT_1,
		gen7_9_0_vpc_pipe_bv_cluster_vpc_ps_registers,  },
};

static struct gen7_sptp_cluster_registers gen7_9_0_sptp_clusters[] = {
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, A7XX_PIPE_BR, 0, A7XX_HLSQ_STATE,
		gen7_9_0_non_context_sp_pipe_br_hlsq_state_registers, 0xae00},
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, A7XX_PIPE_BR, 0, A7XX_SP_TOP,
		gen7_9_0_non_context_sp_pipe_br_sp_top_registers, 0xae00},
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, A7XX_PIPE_BR, 0, A7XX_USPTP,
		gen7_9_0_non_context_sp_pipe_br_usptp_registers, 0xae00},
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, A7XX_PIPE_BR, 0, A7XX_HLSQ_DP_STR,
		gen7_9_0_non_context_sp_pipe_br_hlsq_dp_str_registers, 0xae00},
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, A7XX_PIPE_LPAC, 0, A7XX_HLSQ_STATE,
		gen7_9_0_non_context_sp_pipe_lpac_hlsq_state_registers, 0xaf80},
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, A7XX_PIPE_LPAC, 0, A7XX_SP_TOP,
		gen7_9_0_non_context_sp_pipe_lpac_sp_top_registers, 0xaf80},
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, A7XX_PIPE_LPAC, 0, A7XX_USPTP,
		gen7_9_0_non_context_sp_pipe_lpac_usptp_registers, 0xaf80},
	{ A7XX_CLUSTER_NONE, A7XX_TP0_NCTX_REG, A7XX_PIPE_NONE, 0, A7XX_USPTP,
		gen7_9_0_non_context_tpl1_pipe_none_usptp_registers, 0xb600},
	{ A7XX_CLUSTER_NONE, A7XX_TP0_NCTX_REG, A7XX_PIPE_BR, 0, A7XX_USPTP,
		gen7_9_0_non_context_tpl1_pipe_br_usptp_registers, 0xb600},
	{ A7XX_CLUSTER_NONE, A7XX_TP0_NCTX_REG, A7XX_PIPE_LPAC, 0, A7XX_USPTP,
		gen7_9_0_non_context_tpl1_pipe_lpac_usptp_registers, 0xb780},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BR, 0, A7XX_HLSQ_STATE,
		gen7_9_0_sp_pipe_br_cluster_sp_vs_hlsq_state_registers, 0xa800},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BR, 0, A7XX_SP_TOP,
		gen7_9_0_sp_pipe_br_cluster_sp_vs_sp_top_registers, 0xa800},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BR, 0, A7XX_USPTP,
		gen7_9_0_sp_pipe_br_cluster_sp_vs_usptp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BV, 0, A7XX_HLSQ_STATE,
		gen7_9_0_sp_pipe_bv_cluster_sp_vs_hlsq_state_registers, 0xa800},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BV, 0, A7XX_SP_TOP,
		gen7_9_0_sp_pipe_bv_cluster_sp_vs_sp_top_registers, 0xa800},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BV, 0, A7XX_USPTP,
		gen7_9_0_sp_pipe_bv_cluster_sp_vs_usptp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BR, 1, A7XX_HLSQ_STATE,
		gen7_9_0_sp_pipe_br_cluster_sp_vs_hlsq_state_registers, 0xa800},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BR, 1, A7XX_SP_TOP,
		gen7_9_0_sp_pipe_br_cluster_sp_vs_sp_top_registers, 0xa800},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BR, 1, A7XX_USPTP,
		gen7_9_0_sp_pipe_br_cluster_sp_vs_usptp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BV, 1, A7XX_HLSQ_STATE,
		gen7_9_0_sp_pipe_bv_cluster_sp_vs_hlsq_state_registers, 0xa800},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BV, 1, A7XX_SP_TOP,
		gen7_9_0_sp_pipe_bv_cluster_sp_vs_sp_top_registers, 0xa800},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BV, 1, A7XX_USPTP,
		gen7_9_0_sp_pipe_bv_cluster_sp_vs_usptp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_BR, 0, A7XX_HLSQ_STATE,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_state_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_BR, 0, A7XX_HLSQ_DP,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_dp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_BR, 0, A7XX_SP_TOP,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_sp_top_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_BR, 0, A7XX_USPTP,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_usptp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_BR, 0, A7XX_HLSQ_DP_STR,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_dp_str_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_LPAC, 0, A7XX_HLSQ_STATE,
		gen7_9_0_sp_pipe_lpac_cluster_sp_ps_hlsq_state_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_LPAC, 0, A7XX_HLSQ_DP,
		gen7_9_0_sp_pipe_lpac_cluster_sp_ps_hlsq_dp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_LPAC, 0, A7XX_SP_TOP,
		gen7_9_0_sp_pipe_lpac_cluster_sp_ps_sp_top_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_LPAC, 0, A7XX_USPTP,
		gen7_9_0_sp_pipe_lpac_cluster_sp_ps_usptp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, A7XX_PIPE_BR, 1, A7XX_HLSQ_STATE,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_state_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, A7XX_PIPE_BR, 1, A7XX_HLSQ_DP,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_dp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, A7XX_PIPE_BR, 1, A7XX_SP_TOP,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_sp_top_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, A7XX_PIPE_BR, 1, A7XX_USPTP,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_usptp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, A7XX_PIPE_BR, 1, A7XX_HLSQ_DP_STR,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_dp_str_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX2_3D_CPS_REG, A7XX_PIPE_BR, 2, A7XX_HLSQ_DP,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_dp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX2_3D_CPS_REG, A7XX_PIPE_BR, 2, A7XX_SP_TOP,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_sp_top_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX2_3D_CPS_REG, A7XX_PIPE_BR, 2, A7XX_USPTP,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_usptp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX2_3D_CPS_REG, A7XX_PIPE_BR, 2, A7XX_HLSQ_DP_STR,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_dp_str_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX3_3D_CPS_REG, A7XX_PIPE_BR, 3, A7XX_HLSQ_DP,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_dp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX3_3D_CPS_REG, A7XX_PIPE_BR, 3, A7XX_SP_TOP,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_sp_top_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX3_3D_CPS_REG, A7XX_PIPE_BR, 3, A7XX_USPTP,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_usptp_registers, 0xa800},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX3_3D_CPS_REG, A7XX_PIPE_BR, 3, A7XX_HLSQ_DP_STR,
		gen7_9_0_sp_pipe_br_cluster_sp_ps_hlsq_dp_str_registers, 0xa800},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BR, 0, A7XX_USPTP,
		gen7_9_0_tpl1_pipe_br_cluster_sp_vs_usptp_registers, 0xb000},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BV, 0, A7XX_USPTP,
		gen7_9_0_tpl1_pipe_bv_cluster_sp_vs_usptp_registers, 0xb000},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BR, 1, A7XX_USPTP,
		gen7_9_0_tpl1_pipe_br_cluster_sp_vs_usptp_registers, 0xb000},
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BV, 1, A7XX_USPTP,
		gen7_9_0_tpl1_pipe_bv_cluster_sp_vs_usptp_registers, 0xb000},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_BR, 0, A7XX_USPTP,
		gen7_9_0_tpl1_pipe_br_cluster_sp_ps_usptp_registers, 0xb000},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_LPAC, 0, A7XX_USPTP,
		gen7_9_0_tpl1_pipe_lpac_cluster_sp_ps_usptp_registers, 0xb000},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, A7XX_PIPE_BR, 1, A7XX_USPTP,
		gen7_9_0_tpl1_pipe_br_cluster_sp_ps_usptp_registers, 0xb000},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX2_3D_CPS_REG, A7XX_PIPE_BR, 2, A7XX_USPTP,
		gen7_9_0_tpl1_pipe_br_cluster_sp_ps_usptp_registers, 0xb000},
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX3_3D_CPS_REG, A7XX_PIPE_BR, 3, A7XX_USPTP,
		gen7_9_0_tpl1_pipe_br_cluster_sp_ps_usptp_registers, 0xb000},
};

static struct a6xx_indexed_registers gen7_9_0_cp_indexed_reg_list[] = {
	{ "CP_SQE_STAT", REG_A6XX_CP_SQE_STAT_ADDR,
		REG_A6XX_CP_SQE_STAT_DATA, 0x00040},
	{ "CP_DRAW_STATE", REG_A6XX_CP_DRAW_STATE_ADDR,
		REG_A6XX_CP_DRAW_STATE_DATA, 0x00200},
	{ "CP_ROQ", REG_A6XX_CP_ROQ_DBG_ADDR,
		REG_A6XX_CP_ROQ_DBG_DATA, 0x00800},
	{ "CP_UCODE_DBG_DATA", REG_A6XX_CP_SQE_UCODE_DBG_ADDR,
		REG_A6XX_CP_SQE_UCODE_DBG_DATA, 0x08000},
	{ "CP_BV_SQE_STAT_ADDR", REG_A7XX_CP_BV_DRAW_STATE_ADDR,
		REG_A7XX_CP_BV_DRAW_STATE_DATA, 0x00200},
	{ "CP_BV_ROQ_DBG_ADDR", REG_A7XX_CP_BV_ROQ_DBG_ADDR,
		REG_A7XX_CP_BV_ROQ_DBG_DATA, 0x00800},
	{ "CP_BV_SQE_UCODE_DBG_ADDR", REG_A7XX_CP_BV_SQE_UCODE_DBG_ADDR,
		REG_A7XX_CP_BV_SQE_UCODE_DBG_DATA, 0x08000},
	{ "CP_BV_SQE_STAT_ADDR", REG_A7XX_CP_BV_SQE_STAT_ADDR,
		REG_A7XX_CP_BV_SQE_STAT_DATA, 0x00040},
	{ "CP_RESOURCE_TBL", REG_A7XX_CP_RESOURCE_TBL_DBG_ADDR,
		REG_A7XX_CP_RESOURCE_TBL_DBG_DATA, 0x04100},
	{ "CP_LPAC_DRAW_STATE_ADDR", REG_A7XX_CP_LPAC_DRAW_STATE_ADDR,
		REG_A7XX_CP_LPAC_DRAW_STATE_DATA, 0x00200},
	{ "CP_LPAC_ROQ", REG_A7XX_CP_LPAC_ROQ_DBG_ADDR,
		REG_A7XX_CP_LPAC_ROQ_DBG_DATA, 0x00200},
	{ "CP_SQE_AC_UCODE_DBG_ADDR", REG_A7XX_CP_SQE_AC_UCODE_DBG_ADDR,
		REG_A7XX_CP_SQE_AC_UCODE_DBG_DATA, 0x08000},
	{ "CP_SQE_AC_STAT_ADDR", REG_A7XX_CP_SQE_AC_STAT_ADDR,
		REG_A7XX_CP_SQE_AC_STAT_DATA, 0x00040},
	{ "CP_LPAC_FIFO_DBG_ADDR", REG_A7XX_CP_LPAC_FIFO_DBG_ADDR,
		REG_A7XX_CP_LPAC_FIFO_DBG_DATA, 0x00040},
	{ "CP_AQE_ROQ_0", REG_A7XX_CP_AQE_ROQ_DBG_ADDR_0,
		REG_A7XX_CP_AQE_ROQ_DBG_DATA_0, 0x00100},
	{ "CP_AQE_ROQ_1", REG_A7XX_CP_AQE_ROQ_DBG_ADDR_1,
		REG_A7XX_CP_AQE_ROQ_DBG_DATA_1, 0x00100},
	{ "CP_AQE_UCODE_DBG_0", REG_A7XX_CP_AQE_UCODE_DBG_ADDR_0,
		REG_A7XX_CP_AQE_UCODE_DBG_DATA_0, 0x08000},
	{ "CP_AQE_UCODE_DBG_1", REG_A7XX_CP_AQE_UCODE_DBG_ADDR_1,
		REG_A7XX_CP_AQE_UCODE_DBG_DATA_1, 0x08000},
	{ "CP_AQE_STAT_0", REG_A7XX_CP_AQE_STAT_ADDR_0,
		REG_A7XX_CP_AQE_STAT_DATA_0, 0x00040},
	{ "CP_AQE_STAT_1", REG_A7XX_CP_AQE_STAT_ADDR_1,
		REG_A7XX_CP_AQE_STAT_DATA_1, 0x00040},
};

static struct gen7_reg_list gen7_9_0_reg_list[] = {
	{ gen7_9_0_gpu_registers, NULL},
	{ gen7_9_0_cx_misc_registers, NULL},
	{ gen7_9_0_cx_dbgc_registers, NULL},
	{ gen7_9_0_dbgc_registers, NULL},
	{ NULL, NULL},
};

static const u32 gen7_9_0_cpr_registers[] = {
	0x26800, 0x26805, 0x26808, 0x2680d, 0x26814, 0x26815, 0x2681c, 0x2681c,
	0x26820, 0x26839, 0x26840, 0x26841, 0x26848, 0x26849, 0x26850, 0x26851,
	0x26880, 0x268a1, 0x26980, 0x269b0, 0x269c0, 0x269c8, 0x269e0, 0x269ee,
	0x269fb, 0x269ff, 0x26a02, 0x26a07, 0x26a09, 0x26a0b, 0x26a10, 0x26b0f,
	0x27440, 0x27441, 0x27444, 0x27444, 0x27480, 0x274a2, 0x274ac, 0x274c4,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_cpr_registers), 8));

static const u32 gen7_9_0_dpm_registers[] = {
	0x1aa00, 0x1aa06, 0x1aa09, 0x1aa0a, 0x1aa0c, 0x1aa0d, 0x1aa0f, 0x1aa12,
	0x1aa14, 0x1aa47, 0x1aa50, 0x1aa51,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_dpm_registers), 8));

static const u32 gen7_9_0_dpm_leakage_registers[] = {
	0x21c00, 0x21c00, 0x21c08, 0x21c09, 0x21c0e, 0x21c0f, 0x21c4f, 0x21c50,
	0x21c52, 0x21c52, 0x21c54, 0x21c56, 0x21c58, 0x21c5a, 0x21c5c, 0x21c60,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_dpm_leakage_registers), 8));

static const u32 gen7_9_0_gfx_gpu_acd_registers[] = {
	0x18c00, 0x18c16, 0x18c20, 0x18c2d, 0x18c30, 0x18c31, 0x18c35, 0x18c35,
	0x18c37, 0x18c37, 0x18c3a, 0x18c3a, 0x18c42, 0x18c42, 0x18c56, 0x18c58,
	0x18c5b, 0x18c5d, 0x18c5f, 0x18c62,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_gfx_gpu_acd_registers), 8));

static const u32 gen7_9_0_gpucc_registers[] = {
	0x24000, 0x2400f, 0x24400, 0x2440f, 0x24800, 0x24805, 0x24c00, 0x24cff,
	0x25400, 0x25404, 0x25800, 0x25804, 0x25c00, 0x25c04, 0x26000, 0x26004,
	0x26400, 0x26405, 0x26414, 0x2641d, 0x2642a, 0x26430, 0x26432, 0x26434,
	0x26441, 0x2644b, 0x2644d, 0x26463, 0x26466, 0x26468, 0x26478, 0x2647a,
	0x26489, 0x2648a, 0x2649c, 0x2649e, 0x264a0, 0x264a6, 0x264c5, 0x264c7,
	0x264d6, 0x264d8, 0x264e8, 0x264e9, 0x264f9, 0x264fc, 0x2650b, 0x2650b,
	0x2651c, 0x2651e, 0x26540, 0x2654e, 0x26554, 0x26573, 0x26576, 0x2657a,
	UINT_MAX, UINT_MAX,

};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_gpucc_registers), 8));

static const u32 gen7_9_0_isense_registers[] = {
	0x22c3a, 0x22c3c, 0x22c41, 0x22c41, 0x22c46, 0x22c47, 0x22c4c, 0x22c4c,
	0x22c51, 0x22c51, 0x22c56, 0x22c56, 0x22c5b, 0x22c5b, 0x22c60, 0x22c60,
	0x22c65, 0x22c65, 0x22c6a, 0x22c70, 0x22c75, 0x22c75, 0x22c7a, 0x22c7a,
	0x22c7f, 0x22c7f, 0x22c84, 0x22c85, 0x22c8a, 0x22c8a, 0x22c8f, 0x22c8f,
	0x23000, 0x23009, 0x2300e, 0x2300e, 0x23013, 0x23013, 0x23018, 0x23018,
	0x2301d, 0x2301d, 0x23022, 0x23022, 0x23027, 0x23032, 0x23037, 0x23037,
	0x2303c, 0x2303c, 0x23041, 0x23041, 0x23046, 0x23046, 0x2304b, 0x2304b,
	0x23050, 0x23050, 0x23055, 0x23055, 0x2305a, 0x2305a, 0x2305f, 0x2305f,
	0x23064, 0x23064, 0x23069, 0x2306a, 0x2306f, 0x2306f, 0x23074, 0x23075,
	0x2307a, 0x2307e, 0x23083, 0x23083, 0x23088, 0x23088, 0x2308d, 0x2308d,
	0x23092, 0x23092, 0x230e2, 0x230e2,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_isense_registers), 8));

static const u32 gen7_9_0_rscc_registers[] = {
	0x14000, 0x14036, 0x14040, 0x14047, 0x14080, 0x14084, 0x14089, 0x1408c,
	0x14091, 0x14094, 0x14099, 0x1409c, 0x140a1, 0x140a4, 0x140a9, 0x140ac,
	0x14100, 0x14104, 0x14114, 0x14119, 0x14124, 0x14132, 0x14154, 0x1416b,
	0x14340, 0x14342, 0x14344, 0x1437c, 0x143f0, 0x143f8, 0x143fa, 0x143fe,
	0x14400, 0x14404, 0x14406, 0x1440a, 0x1440c, 0x14410, 0x14412, 0x14416,
	0x14418, 0x1441c, 0x1441e, 0x14422, 0x14424, 0x14424, 0x14498, 0x144a0,
	0x144a2, 0x144a6, 0x144a8, 0x144ac, 0x144ae, 0x144b2, 0x144b4, 0x144b8,
	0x144ba, 0x144be, 0x144c0, 0x144c4, 0x144c6, 0x144ca, 0x144cc, 0x144cc,
	0x14540, 0x14548, 0x1454a, 0x1454e, 0x14550, 0x14554, 0x14556, 0x1455a,
	0x1455c, 0x14560, 0x14562, 0x14566, 0x14568, 0x1456c, 0x1456e, 0x14572,
	0x14574, 0x14574, 0x145e8, 0x145f0, 0x145f2, 0x145f6, 0x145f8, 0x145fc,
	0x145fe, 0x14602, 0x14604, 0x14608, 0x1460a, 0x1460e, 0x14610, 0x14614,
	0x14616, 0x1461a, 0x1461c, 0x1461c, 0x14690, 0x14698, 0x1469a, 0x1469e,
	0x146a0, 0x146a4, 0x146a6, 0x146aa, 0x146ac, 0x146b0, 0x146b2, 0x146b6,
	0x146b8, 0x146bc, 0x146be, 0x146c2, 0x146c4, 0x146c4, 0x14738, 0x14740,
	0x14742, 0x14746, 0x14748, 0x1474c, 0x1474e, 0x14752, 0x14754, 0x14758,
	0x1475a, 0x1475e, 0x14760, 0x14764, 0x14766, 0x1476a, 0x1476c, 0x1476c,
	0x147e0, 0x147e8, 0x147ea, 0x147ee, 0x147f0, 0x147f4, 0x147f6, 0x147fa,
	0x147fc, 0x14800, 0x14802, 0x14806, 0x14808, 0x1480c, 0x1480e, 0x14812,
	0x14814, 0x14814, 0x14888, 0x14890, 0x14892, 0x14896, 0x14898, 0x1489c,
	0x1489e, 0x148a2, 0x148a4, 0x148a8, 0x148aa, 0x148ae, 0x148b0, 0x148b4,
	0x148b6, 0x148ba, 0x148bc, 0x148bc, 0x14930, 0x14938, 0x1493a, 0x1493e,
	0x14940, 0x14944, 0x14946, 0x1494a, 0x1494c, 0x14950, 0x14952, 0x14956,
	0x14958, 0x1495c, 0x1495e, 0x14962, 0x14964, 0x14964,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_9_0_rscc_registers), 8));

static const u32 *gen7_9_0_external_core_regs[] = {
	gen7_9_0_gpucc_registers,
	gen7_9_0_gxclkctl_registers,
	gen7_9_0_cpr_registers,
	gen7_9_0_dpm_registers,
	gen7_9_0_dpm_leakage_registers,
	gen7_9_0_gfx_gpu_acd_registers,
};
#endif /*_ADRENO_GEN7_9_0_SNAPSHOT_H */
