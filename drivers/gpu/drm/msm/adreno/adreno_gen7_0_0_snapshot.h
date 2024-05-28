/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __ADRENO_GEN7_0_0_SNAPSHOT_H
#define __ADRENO_GEN7_0_0_SNAPSHOT_H

#include "a6xx_gpu_state.h"

static const u32 gen7_0_0_debugbus_blocks[] = {
	A7XX_DBGBUS_CP_0_0,
	A7XX_DBGBUS_CP_0_1,
	A7XX_DBGBUS_RBBM,
	A7XX_DBGBUS_HLSQ,
	A7XX_DBGBUS_UCHE_0,
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
	A7XX_DBGBUS_UCHE_WRAPPER,
	A7XX_DBGBUS_CCU_0,
	A7XX_DBGBUS_CCU_1,
	A7XX_DBGBUS_CCU_2,
	A7XX_DBGBUS_CCU_3,
	A7XX_DBGBUS_VFD_BR_0,
	A7XX_DBGBUS_VFD_BR_1,
	A7XX_DBGBUS_VFD_BR_2,
	A7XX_DBGBUS_VFD_BR_3,
	A7XX_DBGBUS_VFD_BR_4,
	A7XX_DBGBUS_VFD_BR_5,
	A7XX_DBGBUS_VFD_BR_6,
	A7XX_DBGBUS_VFD_BR_7,
	A7XX_DBGBUS_VFD_BV_0,
	A7XX_DBGBUS_VFD_BV_1,
	A7XX_DBGBUS_VFD_BV_2,
	A7XX_DBGBUS_VFD_BV_3,
	A7XX_DBGBUS_USP_0,
	A7XX_DBGBUS_USP_1,
	A7XX_DBGBUS_USP_2,
	A7XX_DBGBUS_USP_3,
	A7XX_DBGBUS_TP_0,
	A7XX_DBGBUS_TP_1,
	A7XX_DBGBUS_TP_2,
	A7XX_DBGBUS_TP_3,
	A7XX_DBGBUS_TP_4,
	A7XX_DBGBUS_TP_5,
	A7XX_DBGBUS_TP_6,
	A7XX_DBGBUS_TP_7,
	A7XX_DBGBUS_USPTP_0,
	A7XX_DBGBUS_USPTP_1,
	A7XX_DBGBUS_USPTP_2,
	A7XX_DBGBUS_USPTP_3,
	A7XX_DBGBUS_USPTP_4,
	A7XX_DBGBUS_USPTP_5,
	A7XX_DBGBUS_USPTP_6,
	A7XX_DBGBUS_USPTP_7,
};

static struct gen7_shader_block gen7_0_0_shader_blocks[] = {
	{A7XX_TP0_TMO_DATA,                 0x200, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_TP0_SMO_DATA,                  0x80, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_TP0_MIPMAP_BASE_DATA,         0x3c0, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_INST_DATA,                 0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_INST_DATA_1,               0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_LB_0_DATA,                 0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_LB_1_DATA,                 0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_LB_2_DATA,                 0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_LB_3_DATA,                 0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_LB_4_DATA,                 0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_LB_5_DATA,                 0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_LB_6_DATA,                 0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_LB_7_DATA,                 0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_CB_RAM,                    0x390, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_INST_TAG,                   0x90, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_INST_DATA_2,               0x200, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_TMO_TAG,                    0x80, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_SMO_TAG,                    0x80, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_STATE_DATA,                 0x40, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_HWAVE_RAM,                 0x100, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_L0_INST_BUF,                0x50, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_LB_8_DATA,                 0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_LB_9_DATA,                 0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_LB_10_DATA,                0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_LB_11_DATA,                0x800, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_SP_LB_12_DATA,                0x200, 4, 2, A7XX_PIPE_BR, A7XX_USPTP},
	{A7XX_HLSQ_CVS_BE_CTXT_BUF_RAM_TAG,  0x10, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CVS_BE_CTXT_BUF_RAM_TAG,  0x10, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CPS_BE_CTXT_BUF_RAM_TAG,  0x10, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_GFX_CVS_BE_CTXT_BUF_RAM, 0x300, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_GFX_CVS_BE_CTXT_BUF_RAM, 0x300, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_GFX_CPS_BE_CTXT_BUF_RAM, 0x300, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CHUNK_CVS_RAM,           0x1c0, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CHUNK_CVS_RAM,           0x1c0, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CHUNK_CPS_RAM,           0x300, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CHUNK_CPS_RAM,           0x300, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CHUNK_CVS_RAM_TAG,        0x40, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CHUNK_CVS_RAM_TAG,        0x40, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CHUNK_CPS_RAM_TAG,        0x40, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CHUNK_CPS_RAM_TAG,        0x40, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_ICB_CVS_CB_BASE_TAG,      0x10, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_ICB_CVS_CB_BASE_TAG,      0x10, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_ICB_CPS_CB_BASE_TAG,      0x10, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_ICB_CPS_CB_BASE_TAG,      0x10, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CVS_MISC_RAM,            0x280, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CVS_MISC_RAM,            0x280, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CPS_MISC_RAM,            0x800, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CPS_MISC_RAM,            0x800, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CPS_MISC_RAM_1,          0x200, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_INST_RAM,                0x800, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_INST_RAM,                0x800, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_INST_RAM,                0x800, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_GFX_CVS_CONST_RAM,       0x800, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_GFX_CVS_CONST_RAM,       0x800, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_GFX_CPS_CONST_RAM,       0x800, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_GFX_CPS_CONST_RAM,       0x800, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CVS_MISC_RAM_TAG,         0x10, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CVS_MISC_RAM_TAG,         0x10, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CPS_MISC_RAM_TAG,         0x10, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_CPS_MISC_RAM_TAG,         0x10, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_INST_RAM_TAG,             0x80, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_INST_RAM_TAG,             0x80, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_INST_RAM_TAG,             0x80, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_GFX_CVS_CONST_RAM_TAG,    0x64, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_GFX_CVS_CONST_RAM_TAG,    0x64, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_GFX_CPS_CONST_RAM_TAG,    0x64, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_GFX_CPS_CONST_RAM_TAG,    0x64, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_INST_RAM_1,              0x800, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_STPROC_META,              0x10, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_BV_BE_META,               0x10, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_BV_BE_META,               0x10, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_DATAPATH_META,            0x20, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_FRONTEND_META,            0x40, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_FRONTEND_META,            0x40, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_FRONTEND_META,            0x40, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_INDIRECT_META,            0x10, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_BACKEND_META,             0x40, 1, 1, A7XX_PIPE_BR, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_BACKEND_META,             0x40, 1, 1, A7XX_PIPE_BV, A7XX_HLSQ_STATE},
	{A7XX_HLSQ_BACKEND_META,             0x40, 1, 1, A7XX_PIPE_LPAC, A7XX_HLSQ_STATE},
};

static const u32 gen7_0_0_pre_crashdumper_gpu_registers[] = {
	0x00210, 0x00210, 0x00212, 0x00213, 0x03c00, 0x03c0b, 0x03c40, 0x03c42,
	0x03c45, 0x03c47, 0x03c49, 0x03c4a, 0x03cc0, 0x03cd1,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_pre_crashdumper_gpu_registers), 8));

static const u32 gen7_0_0_post_crashdumper_registers[] = {
	0x00535, 0x00535, 0x0f400, 0x0f400, 0x0f800, 0x0f803, 0x0fc00, 0x0fc01,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_post_crashdumper_registers), 8));

static const u32 gen7_0_0_gpu_registers[] = {
	0x00000, 0x00000, 0x00002, 0x00002, 0x00011, 0x00012, 0x00016, 0x0001b,
	0x0001f, 0x00032, 0x00038, 0x0003c, 0x00042, 0x00042, 0x00044, 0x00044,
	0x00047, 0x00047, 0x00049, 0x0004a, 0x0004c, 0x0004c, 0x00050, 0x00050,
	0x00056, 0x00056, 0x00073, 0x00075, 0x000ad, 0x000ae, 0x000b0, 0x000b0,
	0x000b4, 0x000b4, 0x000b8, 0x000b8, 0x000bc, 0x000bc, 0x000c0, 0x000c0,
	0x000c4, 0x000c4, 0x000c8, 0x000c8, 0x000cc, 0x000cc, 0x000d0, 0x000d0,
	0x000d4, 0x000d4, 0x000d8, 0x000d8, 0x000dc, 0x000dc, 0x000e0, 0x000e0,
	0x000e4, 0x000e4, 0x000e8, 0x000e8, 0x000ec, 0x000ec, 0x000f0, 0x000f0,
	0x000f4, 0x000f4, 0x000f8, 0x000f8, 0x00100, 0x00100, 0x00104, 0x0010b,
	0x0010f, 0x0011d, 0x0012f, 0x0012f, 0x00200, 0x0020d, 0x00211, 0x00211,
	0x00215, 0x00243, 0x00260, 0x00268, 0x00272, 0x00274, 0x00281, 0x0028d,
	0x00300, 0x00401, 0x00410, 0x00451, 0x00460, 0x004a3, 0x004c0, 0x004d1,
	0x00500, 0x00500, 0x00507, 0x0050b, 0x0050f, 0x0050f, 0x00511, 0x00511,
	0x00533, 0x00534, 0x00536, 0x00536, 0x00540, 0x00555, 0x00564, 0x00567,
	0x00574, 0x00577, 0x005fb, 0x005ff, 0x00800, 0x00808, 0x00810, 0x00813,
	0x00820, 0x00821, 0x00823, 0x00827, 0x00830, 0x00834, 0x0083f, 0x00841,
	0x00843, 0x00847, 0x0084f, 0x00886, 0x008a0, 0x008ab, 0x008c0, 0x008c0,
	0x008c4, 0x008c5, 0x008d0, 0x008dd, 0x008e0, 0x008e6, 0x008f0, 0x008f3,
	0x00900, 0x00903, 0x00908, 0x00911, 0x00928, 0x0093e, 0x00942, 0x0094d,
	0x00980, 0x00984, 0x0098d, 0x0098f, 0x009b0, 0x009b4, 0x009c2, 0x009c9,
	0x009ce, 0x009d7, 0x009e0, 0x009e7, 0x00a00, 0x00a00, 0x00a02, 0x00a03,
	0x00a10, 0x00a4f, 0x00a61, 0x00a9f, 0x00ad0, 0x00adb, 0x00b00, 0x00b31,
	0x00b35, 0x00b3c, 0x00b40, 0x00b40, 0x00c00, 0x00c00, 0x00c02, 0x00c04,
	0x00c06, 0x00c06, 0x00c10, 0x00cd9, 0x00ce0, 0x00d0c, 0x00df0, 0x00df4,
	0x00e01, 0x00e02, 0x00e07, 0x00e0e, 0x00e10, 0x00e13, 0x00e17, 0x00e19,
	0x00e1b, 0x00e2b, 0x00e30, 0x00e32, 0x00e38, 0x00e3c,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_gpu_registers), 8));

static const u32 gen7_0_0_gmu_registers[] = {
	0x10001, 0x10001, 0x10003, 0x10003, 0x10401, 0x10401, 0x10403, 0x10403,
	0x10801, 0x10801, 0x10803, 0x10803, 0x10c01, 0x10c01, 0x10c03, 0x10c03,
	0x11001, 0x11001, 0x11003, 0x11003, 0x11401, 0x11401, 0x11403, 0x11403,
	0x11801, 0x11801, 0x11803, 0x11803, 0x11c01, 0x11c01, 0x11c03, 0x11c03,
	0x1f400, 0x1f40d, 0x1f40f, 0x1f411, 0x1f500, 0x1f500, 0x1f507, 0x1f507,
	0x1f509, 0x1f50b, 0x1f800, 0x1f804, 0x1f807, 0x1f808, 0x1f80b, 0x1f80c,
	0x1f80f, 0x1f80f, 0x1f811, 0x1f811, 0x1f813, 0x1f817, 0x1f819, 0x1f81c,
	0x1f824, 0x1f82a, 0x1f82d, 0x1f830, 0x1f840, 0x1f853, 0x1f860, 0x1f860,
	0x1f870, 0x1f879, 0x1f87f, 0x1f87f, 0x1f888, 0x1f889, 0x1f8a0, 0x1f8a2,
	0x1f8a4, 0x1f8af, 0x1f8c0, 0x1f8c1, 0x1f8c3, 0x1f8c4, 0x1f8d0, 0x1f8d0,
	0x1f8ec, 0x1f8ec, 0x1f8f0, 0x1f8f1, 0x1f910, 0x1f914, 0x1f920, 0x1f921,
	0x1f924, 0x1f925, 0x1f928, 0x1f929, 0x1f92c, 0x1f92d, 0x1f940, 0x1f940,
	0x1f942, 0x1f944, 0x1f948, 0x1f94a, 0x1f94f, 0x1f951, 0x1f958, 0x1f95a,
	0x1f95d, 0x1f95d, 0x1f962, 0x1f962, 0x1f964, 0x1f96b, 0x1f970, 0x1f979,
	0x1f980, 0x1f981, 0x1f984, 0x1f986, 0x1f992, 0x1f993, 0x1f996, 0x1f99e,
	0x1f9c0, 0x1f9c0, 0x1f9c5, 0x1f9d4, 0x1f9f0, 0x1f9f1, 0x1f9f8, 0x1f9fa,
	0x1fa00, 0x1fa03, 0x20000, 0x20005, 0x20008, 0x2000c, 0x20010, 0x20012,
	0x20018, 0x20018, 0x20020, 0x20023, 0x20030, 0x20031, 0x23801, 0x23801,
	0x23803, 0x23803, 0x23805, 0x23805, 0x23807, 0x23807, 0x23809, 0x23809,
	0x2380b, 0x2380b, 0x2380d, 0x2380d, 0x2380f, 0x2380f, 0x23811, 0x23811,
	0x23813, 0x23813, 0x23815, 0x23815, 0x23817, 0x23817, 0x23819, 0x23819,
	0x2381b, 0x2381b, 0x2381d, 0x2381d, 0x2381f, 0x23820, 0x23822, 0x23822,
	0x23824, 0x23824, 0x23826, 0x23826, 0x23828, 0x23828, 0x2382a, 0x2382a,
	0x2382c, 0x2382c, 0x2382e, 0x2382e, 0x23830, 0x23830, 0x23832, 0x23832,
	0x23834, 0x23834, 0x23836, 0x23836, 0x23838, 0x23838, 0x2383a, 0x2383a,
	0x2383c, 0x2383c, 0x2383e, 0x2383e, 0x23840, 0x23847, 0x23b00, 0x23b01,
	0x23b03, 0x23b03, 0x23b05, 0x23b0e, 0x23b10, 0x23b13, 0x23b15, 0x23b16,
	0x23b20, 0x23b20, 0x23b28, 0x23b28, 0x23b30, 0x23b30,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_gmu_registers), 8));

static const u32 gen7_0_0_gmugx_registers[] = {
	0x1a400, 0x1a41f, 0x1a440, 0x1a45f, 0x1a480, 0x1a49f, 0x1a4c0, 0x1a4df,
	0x1a500, 0x1a51f, 0x1a540, 0x1a55f, 0x1a580, 0x1a59f, 0x1a5c0, 0x1a5df,
	0x1a780, 0x1a781, 0x1a783, 0x1a785, 0x1a787, 0x1a789, 0x1a78b, 0x1a78d,
	0x1a78f, 0x1a791, 0x1a793, 0x1a795, 0x1a797, 0x1a799, 0x1a79b, 0x1a79b,
	0x1a7c0, 0x1a7c1, 0x1a7c4, 0x1a7c5, 0x1a7c8, 0x1a7c9, 0x1a7cc, 0x1a7cd,
	0x1a7d0, 0x1a7d1, 0x1a7d4, 0x1a7d5, 0x1a7d8, 0x1a7d9, 0x1a7fc, 0x1a7fd,
	0x1a800, 0x1a802, 0x1a804, 0x1a804, 0x1a816, 0x1a816, 0x1a81e, 0x1a81e,
	0x1a826, 0x1a826, 0x1a82e, 0x1a82e, 0x1a836, 0x1a836, 0x1a83e, 0x1a83e,
	0x1a846, 0x1a846, 0x1a860, 0x1a862, 0x1a864, 0x1a867, 0x1a870, 0x1a870,
	0x1a883, 0x1a884, 0x1a8c0, 0x1a8c2, 0x1a8c4, 0x1a8c7, 0x1a8d0, 0x1a8d3,
	0x1a900, 0x1a92b, 0x1a940, 0x1a940,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_gmugx_registers), 8));

static const u32 gen7_0_0_noncontext_pipe_br_registers[] = {
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
static_assert(IS_ALIGNED(sizeof(gen7_0_0_noncontext_pipe_br_registers), 8));

static const u32 gen7_0_0_noncontext_pipe_bv_registers[] = {
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
static_assert(IS_ALIGNED(sizeof(gen7_0_0_noncontext_pipe_bv_registers), 8));

static const u32 gen7_0_0_noncontext_pipe_lpac_registers[] = {
	0x00887, 0x0088c, 0x00f80, 0x00f80,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_noncontext_pipe_lpac_registers), 8));

static const u32 gen7_0_0_noncontext_rb_rac_pipe_br_registers[] = {
	0x08e10, 0x08e1c, 0x08e20, 0x08e25, 0x08e51, 0x08e5a,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_noncontext_rb_rac_pipe_br_registers), 8));

static const u32 gen7_0_0_noncontext_rb_rbp_pipe_br_registers[] = {
	0x08e01, 0x08e01, 0x08e04, 0x08e04, 0x08e06, 0x08e09, 0x08e0c, 0x08e0c,
	0x08e28, 0x08e28, 0x08e2c, 0x08e35, 0x08e3b, 0x08e3f, 0x08e50, 0x08e50,
	0x08e5b, 0x08e5d, 0x08e5f, 0x08e5f, 0x08e61, 0x08e61, 0x08e63, 0x08e65,
	0x08e68, 0x08e68, 0x08e70, 0x08e79, 0x08e80, 0x08e8f,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_noncontext_rb_rbp_pipe_br_registers), 8));

/* Block: GRAS Cluster: A7XX_CLUSTER_GRAS Pipeline: A7XX_PIPE_BR */
static const u32 gen7_0_0_gras_cluster_gras_pipe_br_registers[] = {
	0x08000, 0x08008, 0x08010, 0x08092, 0x08094, 0x08099, 0x0809b, 0x0809d,
	0x080a0, 0x080a7, 0x080af, 0x080f1, 0x080f4, 0x080f6, 0x080f8, 0x080fa,
	0x08100, 0x08107, 0x08109, 0x0810b, 0x08110, 0x08110, 0x08120, 0x0813f,
	0x08400, 0x08406, 0x0840a, 0x0840b,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_gras_cluster_gras_pipe_br_registers), 8));

/* Block: GRAS Cluster: A7XX_CLUSTER_GRAS Pipeline: A7XX_PIPE_BV */
static const u32 gen7_0_0_gras_cluster_gras_pipe_bv_registers[] = {
	0x08000, 0x08008, 0x08010, 0x08092, 0x08094, 0x08099, 0x0809b, 0x0809d,
	0x080a0, 0x080a7, 0x080af, 0x080f1, 0x080f4, 0x080f6, 0x080f8, 0x080fa,
	0x08100, 0x08107, 0x08109, 0x0810b, 0x08110, 0x08110, 0x08120, 0x0813f,
	0x08400, 0x08406, 0x0840a, 0x0840b,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_gras_cluster_gras_pipe_bv_registers), 8));

/* Block: PC Cluster: A7XX_CLUSTER_FE Pipeline: A7XX_PIPE_BR */
static const u32 gen7_0_0_pc_cluster_fe_pipe_br_registers[] = {
	0x09800, 0x09804, 0x09806, 0x0980a, 0x09810, 0x09811, 0x09884, 0x09886,
	0x09b00, 0x09b08,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_pc_cluster_fe_pipe_br_registers), 8));

/* Block: PC Cluster: A7XX_CLUSTER_FE Pipeline: A7XX_PIPE_BV */
static const u32 gen7_0_0_pc_cluster_fe_pipe_bv_registers[] = {
	0x09800, 0x09804, 0x09806, 0x0980a, 0x09810, 0x09811, 0x09884, 0x09886,
	0x09b00, 0x09b08,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_pc_cluster_fe_pipe_bv_registers), 8));

/* Block: RB_RAC Cluster: A7XX_CLUSTER_PS Pipeline: A7XX_PIPE_BR */
static const u32 gen7_0_0_rb_rac_cluster_ps_pipe_br_registers[] = {
	0x08802, 0x08802, 0x08804, 0x08806, 0x08809, 0x0880a, 0x0880e, 0x08811,
	0x08818, 0x0881e, 0x08821, 0x08821, 0x08823, 0x08826, 0x08829, 0x08829,
	0x0882b, 0x0882e, 0x08831, 0x08831, 0x08833, 0x08836, 0x08839, 0x08839,
	0x0883b, 0x0883e, 0x08841, 0x08841, 0x08843, 0x08846, 0x08849, 0x08849,
	0x0884b, 0x0884e, 0x08851, 0x08851, 0x08853, 0x08856, 0x08859, 0x08859,
	0x0885b, 0x0885e, 0x08860, 0x08864, 0x08870, 0x08870, 0x08873, 0x08876,
	0x08878, 0x08879, 0x08882, 0x08885, 0x08887, 0x08889, 0x08891, 0x08891,
	0x08898, 0x08898, 0x088c0, 0x088c1, 0x088e5, 0x088e5, 0x088f4, 0x088f5,
	0x08a00, 0x08a05, 0x08a10, 0x08a15, 0x08a20, 0x08a25, 0x08a30, 0x08a35,
	0x08c00, 0x08c01, 0x08c18, 0x08c1f, 0x08c26, 0x08c34,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_rb_rac_cluster_ps_pipe_br_registers), 8));

/* Block: RB_RBP Cluster: A7XX_CLUSTER_PS Pipeline: A7XX_PIPE_BR */
static const u32 gen7_0_0_rb_rbp_cluster_ps_pipe_br_registers[] = {
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
static_assert(IS_ALIGNED(sizeof(gen7_0_0_rb_rbp_cluster_ps_pipe_br_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_BR Location: HLSQ_STATE */
static const u32 gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_state_registers[] = {
	0x0a980, 0x0a980, 0x0a982, 0x0a984, 0x0a99e, 0x0a99e, 0x0a9a7, 0x0a9a7,
	0x0a9aa, 0x0a9aa, 0x0a9ae, 0x0a9b0, 0x0a9b3, 0x0a9b5, 0x0a9ba, 0x0a9ba,
	0x0a9bc, 0x0a9bc, 0x0a9c4, 0x0a9c4, 0x0a9cd, 0x0a9cd, 0x0a9e0, 0x0a9fc,
	0x0aa00, 0x0aa00, 0x0aa30, 0x0aa31, 0x0aa40, 0x0aabf, 0x0ab00, 0x0ab03,
	0x0ab05, 0x0ab05, 0x0ab0a, 0x0ab1b, 0x0ab20, 0x0ab20, 0x0ab40, 0x0abbf,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_state_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_LPAC Location: HLSQ_STATE */
static const u32 gen7_0_0_sp_cluster_sp_ps_pipe_lpac_hlsq_state_registers[] = {
	0x0a9b0, 0x0a9b0, 0x0a9b3, 0x0a9b5, 0x0a9ba, 0x0a9ba, 0x0a9bc, 0x0a9bc,
	0x0a9c4, 0x0a9c4, 0x0a9cd, 0x0a9cd, 0x0a9e2, 0x0a9e3, 0x0a9e6, 0x0a9fc,
	0x0aa00, 0x0aa00, 0x0aa31, 0x0aa31, 0x0ab00, 0x0ab01,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_ps_pipe_lpac_hlsq_state_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_BR Location: HLSQ_DP */
static const u32 gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_dp_registers[] = {
	0x0a9b1, 0x0a9b1, 0x0a9c6, 0x0a9cb, 0x0a9d4, 0x0a9df,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_dp_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_LPAC Location: HLSQ_DP */
static const u32 gen7_0_0_sp_cluster_sp_ps_pipe_lpac_hlsq_dp_registers[] = {
	0x0a9b1, 0x0a9b1, 0x0a9d4, 0x0a9df,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_ps_pipe_lpac_hlsq_dp_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_BR Location: SP_TOP */
static const u32 gen7_0_0_sp_cluster_sp_ps_pipe_br_sp_top_registers[] = {
	0x0a980, 0x0a980, 0x0a982, 0x0a984, 0x0a99e, 0x0a9a2, 0x0a9a7, 0x0a9a8,
	0x0a9aa, 0x0a9aa, 0x0a9ae, 0x0a9ae, 0x0a9b0, 0x0a9b1, 0x0a9b3, 0x0a9b5,
	0x0a9ba, 0x0a9bc, 0x0a9e0, 0x0a9f9, 0x0aa00, 0x0aa00, 0x0ab00, 0x0ab00,
	0x0ab02, 0x0ab02, 0x0ab04, 0x0ab05, 0x0ab0a, 0x0ab1b, 0x0ab20, 0x0ab20,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_ps_pipe_br_sp_top_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_LPAC Location: SP_TOP */
static const u32 gen7_0_0_sp_cluster_sp_ps_pipe_lpac_sp_top_registers[] = {
	0x0a9b0, 0x0a9b1, 0x0a9b3, 0x0a9b5, 0x0a9ba, 0x0a9bc, 0x0a9e2, 0x0a9e3,
	0x0a9e6, 0x0a9f9, 0x0aa00, 0x0aa00, 0x0ab00, 0x0ab00,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_ps_pipe_lpac_sp_top_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_BR Location: uSPTP */
static const u32 gen7_0_0_sp_cluster_sp_ps_pipe_br_usptp_registers[] = {
	0x0a980, 0x0a982, 0x0a985, 0x0a9a6, 0x0a9a8, 0x0a9a9, 0x0a9ab, 0x0a9ae,
	0x0a9b0, 0x0a9b3, 0x0a9b6, 0x0a9b9, 0x0a9bb, 0x0a9bf, 0x0a9c2, 0x0a9c3,
	0x0a9cd, 0x0a9cd, 0x0a9d0, 0x0a9d3, 0x0aa30, 0x0aa31, 0x0aa40, 0x0aabf,
	0x0ab00, 0x0ab05, 0x0ab21, 0x0ab22, 0x0ab40, 0x0abbf,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_ps_pipe_br_usptp_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_LPAC Location: uSPTP */
static const u32 gen7_0_0_sp_cluster_sp_ps_pipe_lpac_usptp_registers[] = {
	0x0a9b0, 0x0a9b3, 0x0a9b6, 0x0a9b9, 0x0a9bb, 0x0a9be, 0x0a9c2, 0x0a9c3,
	0x0a9cd, 0x0a9cd, 0x0a9d0, 0x0a9d3, 0x0aa31, 0x0aa31, 0x0ab00, 0x0ab01,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_ps_pipe_lpac_usptp_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_VS Pipeline: A7XX_PIPE_BR Location: HLSQ_STATE */
static const u32 gen7_0_0_sp_cluster_sp_vs_pipe_br_hlsq_state_registers[] = {
	0x0a800, 0x0a800, 0x0a81b, 0x0a81d, 0x0a822, 0x0a822, 0x0a824, 0x0a824,
	0x0a827, 0x0a82a, 0x0a830, 0x0a830, 0x0a833, 0x0a835, 0x0a83a, 0x0a83a,
	0x0a83c, 0x0a83c, 0x0a83f, 0x0a840, 0x0a85b, 0x0a85d, 0x0a862, 0x0a862,
	0x0a864, 0x0a864, 0x0a867, 0x0a867, 0x0a870, 0x0a870, 0x0a88c, 0x0a88e,
	0x0a893, 0x0a893, 0x0a895, 0x0a895, 0x0a898, 0x0a898, 0x0a89a, 0x0a89d,
	0x0a8a0, 0x0a8af, 0x0a8c0, 0x0a8c3, 0x0ab00, 0x0ab03, 0x0ab05, 0x0ab05,
	0x0ab0a, 0x0ab1b, 0x0ab20, 0x0ab20, 0x0ab40, 0x0abbf,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_vs_pipe_br_hlsq_state_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_VS Pipeline: A7XX_PIPE_BV Location: HLSQ_STATE */
static const u32 gen7_0_0_sp_cluster_sp_vs_pipe_bv_hlsq_state_registers[] = {
	0x0a800, 0x0a800, 0x0a81b, 0x0a81d, 0x0a822, 0x0a822, 0x0a824, 0x0a824,
	0x0a827, 0x0a82a, 0x0a830, 0x0a830, 0x0a833, 0x0a835, 0x0a83a, 0x0a83a,
	0x0a83c, 0x0a83c, 0x0a83f, 0x0a840, 0x0a85b, 0x0a85d, 0x0a862, 0x0a862,
	0x0a864, 0x0a864, 0x0a867, 0x0a867, 0x0a870, 0x0a870, 0x0a88c, 0x0a88e,
	0x0a893, 0x0a893, 0x0a895, 0x0a895, 0x0a898, 0x0a898, 0x0a89a, 0x0a89d,
	0x0a8a0, 0x0a8af, 0x0a8c0, 0x0a8c3, 0x0ab00, 0x0ab02, 0x0ab0a, 0x0ab1b,
	0x0ab20, 0x0ab20, 0x0ab40, 0x0abbf,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_vs_pipe_bv_hlsq_state_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_VS Pipeline: A7XX_PIPE_BR Location: SP_TOP */
static const u32 gen7_0_0_sp_cluster_sp_vs_pipe_br_sp_top_registers[] = {
	0x0a800, 0x0a800, 0x0a81c, 0x0a81d, 0x0a822, 0x0a824, 0x0a830, 0x0a831,
	0x0a834, 0x0a835, 0x0a83a, 0x0a83c, 0x0a840, 0x0a840, 0x0a85c, 0x0a85d,
	0x0a862, 0x0a864, 0x0a870, 0x0a871, 0x0a88d, 0x0a88e, 0x0a893, 0x0a895,
	0x0a8a0, 0x0a8af, 0x0ab00, 0x0ab00, 0x0ab02, 0x0ab02, 0x0ab04, 0x0ab05,
	0x0ab0a, 0x0ab1b, 0x0ab20, 0x0ab20,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_vs_pipe_br_sp_top_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_VS Pipeline: A7XX_PIPE_BV Location: SP_TOP */
static const u32 gen7_0_0_sp_cluster_sp_vs_pipe_bv_sp_top_registers[] = {
	0x0a800, 0x0a800, 0x0a81c, 0x0a81d, 0x0a822, 0x0a824, 0x0a830, 0x0a831,
	0x0a834, 0x0a835, 0x0a83a, 0x0a83c, 0x0a840, 0x0a840, 0x0a85c, 0x0a85d,
	0x0a862, 0x0a864, 0x0a870, 0x0a871, 0x0a88d, 0x0a88e, 0x0a893, 0x0a895,
	0x0a8a0, 0x0a8af, 0x0ab00, 0x0ab00, 0x0ab02, 0x0ab02, 0x0ab0a, 0x0ab1b,
	0x0ab20, 0x0ab20,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_vs_pipe_bv_sp_top_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_VS Pipeline: A7XX_PIPE_BR Location: uSPTP */
static const u32 gen7_0_0_sp_cluster_sp_vs_pipe_br_usptp_registers[] = {
	0x0a800, 0x0a81b, 0x0a81e, 0x0a821, 0x0a823, 0x0a827, 0x0a830, 0x0a833,
	0x0a836, 0x0a839, 0x0a83b, 0x0a85b, 0x0a85e, 0x0a861, 0x0a863, 0x0a867,
	0x0a870, 0x0a88c, 0x0a88f, 0x0a892, 0x0a894, 0x0a898, 0x0a8c0, 0x0a8c3,
	0x0ab00, 0x0ab05, 0x0ab21, 0x0ab22, 0x0ab40, 0x0abbf,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_vs_pipe_br_usptp_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_VS Pipeline: A7XX_PIPE_BV Location: uSPTP */
static const u32 gen7_0_0_sp_cluster_sp_vs_pipe_bv_usptp_registers[] = {
	0x0a800, 0x0a81b, 0x0a81e, 0x0a821, 0x0a823, 0x0a827, 0x0a830, 0x0a833,
	0x0a836, 0x0a839, 0x0a83b, 0x0a85b, 0x0a85e, 0x0a861, 0x0a863, 0x0a867,
	0x0a870, 0x0a88c, 0x0a88f, 0x0a892, 0x0a894, 0x0a898, 0x0a8c0, 0x0a8c3,
	0x0ab00, 0x0ab02, 0x0ab21, 0x0ab22, 0x0ab40, 0x0abbf,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_vs_pipe_bv_usptp_registers), 8));

/* Block: TPL1 Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_BR */
static const u32 gen7_0_0_tpl1_cluster_sp_ps_pipe_br_registers[] = {
	0x0b180, 0x0b183, 0x0b190, 0x0b195, 0x0b2c0, 0x0b2d5, 0x0b300, 0x0b307,
	0x0b309, 0x0b309, 0x0b310, 0x0b310,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_tpl1_cluster_sp_ps_pipe_br_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_BV Location: HLSQ_STATE */
static const u32 gen7_0_0_sp_cluster_sp_ps_pipe_bv_hlsq_state_registers[] = {
	0x0ab00, 0x0ab02, 0x0ab0a, 0x0ab1b, 0x0ab20, 0x0ab20, 0x0ab40, 0x0abbf,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_ps_pipe_bv_hlsq_state_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_BV Location: SP_TOP */
static const u32 gen7_0_0_sp_cluster_sp_ps_pipe_bv_sp_top_registers[] = {
	0x0ab00, 0x0ab00, 0x0ab02, 0x0ab02, 0x0ab0a, 0x0ab1b, 0x0ab20, 0x0ab20,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_ps_pipe_bv_sp_top_registers), 8));

/* Block: SP Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_BV Location: uSPTP */
static const u32 gen7_0_0_sp_cluster_sp_ps_pipe_bv_usptp_registers[] = {
	0x0ab00, 0x0ab02, 0x0ab21, 0x0ab22, 0x0ab40, 0x0abbf,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_cluster_sp_ps_pipe_bv_usptp_registers), 8));

/* Block: TPL1 Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_BV */
static const u32 gen7_0_0_tpl1_cluster_sp_ps_pipe_bv_registers[] = {
	0x0b300, 0x0b307, 0x0b309, 0x0b309, 0x0b310, 0x0b310,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_tpl1_cluster_sp_ps_pipe_bv_registers), 8));

/* Block: TPL1 Cluster: A7XX_CLUSTER_SP_PS Pipeline: A7XX_PIPE_LPAC */
static const u32 gen7_0_0_tpl1_cluster_sp_ps_pipe_lpac_registers[] = {
	0x0b180, 0x0b181, 0x0b300, 0x0b301, 0x0b307, 0x0b307, 0x0b309, 0x0b309,
	0x0b310, 0x0b310,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_tpl1_cluster_sp_ps_pipe_lpac_registers), 8));

/* Block: TPL1 Cluster: A7XX_CLUSTER_SP_VS Pipeline: A7XX_PIPE_BR */
static const u32 gen7_0_0_tpl1_cluster_sp_vs_pipe_br_registers[] = {
	0x0b300, 0x0b307, 0x0b309, 0x0b309, 0x0b310, 0x0b310,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_tpl1_cluster_sp_vs_pipe_br_registers), 8));

/* Block: TPL1 Cluster: A7XX_CLUSTER_SP_VS Pipeline: A7XX_PIPE_BV */
static const u32 gen7_0_0_tpl1_cluster_sp_vs_pipe_bv_registers[] = {
	0x0b300, 0x0b307, 0x0b309, 0x0b309, 0x0b310, 0x0b310,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_tpl1_cluster_sp_vs_pipe_bv_registers), 8));

/* Block: VFD Cluster: A7XX_CLUSTER_FE Pipeline: A7XX_PIPE_BR */
static const u32 gen7_0_0_vfd_cluster_fe_pipe_br_registers[] = {
	0x0a000, 0x0a009, 0x0a00e, 0x0a0ef,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_vfd_cluster_fe_pipe_br_registers), 8));

/* Block: VFD Cluster: A7XX_CLUSTER_FE Pipeline: A7XX_PIPE_BV */
static const u32 gen7_0_0_vfd_cluster_fe_pipe_bv_registers[] = {
	0x0a000, 0x0a009, 0x0a00e, 0x0a0ef,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_vfd_cluster_fe_pipe_bv_registers), 8));

/* Block: VPC Cluster: A7XX_CLUSTER_FE Pipeline: A7XX_PIPE_BR */
static const u32 gen7_0_0_vpc_cluster_fe_pipe_br_registers[] = {
	0x09300, 0x09307,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_vpc_cluster_fe_pipe_br_registers), 8));

/* Block: VPC Cluster: A7XX_CLUSTER_FE Pipeline: A7XX_PIPE_BV */
static const u32 gen7_0_0_vpc_cluster_fe_pipe_bv_registers[] = {
	0x09300, 0x09307,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_vpc_cluster_fe_pipe_bv_registers), 8));

/* Block: VPC Cluster: A7XX_CLUSTER_PC_VS Pipeline: A7XX_PIPE_BR */
static const u32 gen7_0_0_vpc_cluster_pc_vs_pipe_br_registers[] = {
	0x09101, 0x0910c, 0x09300, 0x09307,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_vpc_cluster_pc_vs_pipe_br_registers), 8));

/* Block: VPC Cluster: A7XX_CLUSTER_PC_VS Pipeline: A7XX_PIPE_BV */
static const u32 gen7_0_0_vpc_cluster_pc_vs_pipe_bv_registers[] = {
	0x09101, 0x0910c, 0x09300, 0x09307,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_vpc_cluster_pc_vs_pipe_bv_registers), 8));

/* Block: VPC Cluster: A7XX_CLUSTER_VPC_PS Pipeline: A7XX_PIPE_BR */
static const u32 gen7_0_0_vpc_cluster_vpc_ps_pipe_br_registers[] = {
	0x09200, 0x0920f, 0x09212, 0x09216, 0x09218, 0x09236, 0x09300, 0x09307,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_vpc_cluster_vpc_ps_pipe_br_registers), 8));

/* Block: VPC Cluster: A7XX_CLUSTER_VPC_PS Pipeline: A7XX_PIPE_BV */
static const u32 gen7_0_0_vpc_cluster_vpc_ps_pipe_bv_registers[] = {
	0x09200, 0x0920f, 0x09212, 0x09216, 0x09218, 0x09236, 0x09300, 0x09307,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_vpc_cluster_vpc_ps_pipe_bv_registers), 8));

/* Block: SP Cluster: noncontext Pipeline: A7XX_PIPE_BR Location: HLSQ_STATE */
static const u32 gen7_0_0_sp_noncontext_pipe_br_hlsq_state_registers[] = {
	0x0ae52, 0x0ae52, 0x0ae60, 0x0ae67, 0x0ae69, 0x0ae73,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_noncontext_pipe_br_hlsq_state_registers), 8));

/* Block: SP Cluster: noncontext Pipeline: A7XX_PIPE_BR Location: SP_TOP */
static const u32 gen7_0_0_sp_noncontext_pipe_br_sp_top_registers[] = {
	0x0ae00, 0x0ae00, 0x0ae02, 0x0ae04, 0x0ae06, 0x0ae09, 0x0ae0c, 0x0ae0c,
	0x0ae0f, 0x0ae0f, 0x0ae28, 0x0ae2b, 0x0ae35, 0x0ae35, 0x0ae3a, 0x0ae3f,
	0x0ae50, 0x0ae52, 0x0ae80, 0x0aea3,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_noncontext_pipe_br_sp_top_registers), 8));

/* Block: SP Cluster: noncontext Pipeline: A7XX_PIPE_BR Location: uSPTP */
static const u32 gen7_0_0_sp_noncontext_pipe_br_usptp_registers[] = {
	0x0ae00, 0x0ae00, 0x0ae02, 0x0ae04, 0x0ae06, 0x0ae09, 0x0ae0c, 0x0ae0c,
	0x0ae0f, 0x0ae0f, 0x0ae30, 0x0ae32, 0x0ae35, 0x0ae35, 0x0ae3a, 0x0ae3b,
	0x0ae3e, 0x0ae3f, 0x0ae50, 0x0ae52,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_noncontext_pipe_br_usptp_registers), 8));

/* Block: SP Cluster: noncontext Pipeline: A7XX_PIPE_LPAC Location: HLSQ_STATE */
static const u32 gen7_0_0_sp_noncontext_pipe_lpac_hlsq_state_registers[] = {
	0x0af88, 0x0af8a,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_noncontext_pipe_lpac_hlsq_state_registers), 8));

/* Block: SP Cluster: noncontext Pipeline: A7XX_PIPE_LPAC Location: SP_TOP */
static const u32 gen7_0_0_sp_noncontext_pipe_lpac_sp_top_registers[] = {
	0x0af80, 0x0af84,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_noncontext_pipe_lpac_sp_top_registers), 8));

/* Block: SP Cluster: noncontext Pipeline: A7XX_PIPE_LPAC Location: uSPTP */
static const u32 gen7_0_0_sp_noncontext_pipe_lpac_usptp_registers[] = {
	0x0af80, 0x0af84, 0x0af90, 0x0af92,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_sp_noncontext_pipe_lpac_usptp_registers), 8));

/* Block: TPl1 Cluster: noncontext Pipeline: A7XX_PIPE_BR */
static const u32 gen7_0_0_tpl1_noncontext_pipe_br_registers[] = {
	0x0b600, 0x0b600, 0x0b602, 0x0b602, 0x0b604, 0x0b604, 0x0b608, 0x0b60c,
	0x0b60f, 0x0b621, 0x0b630, 0x0b633,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_tpl1_noncontext_pipe_br_registers), 8));

/* Block: TPl1 Cluster: noncontext Pipeline: A7XX_PIPE_LPAC */
static const u32 gen7_0_0_tpl1_noncontext_pipe_lpac_registers[] = {
	0x0b780, 0x0b780,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_tpl1_noncontext_pipe_lpac_registers), 8));

static const struct gen7_sel_reg  gen7_0_0_rb_rac_sel = {
	.host_reg = REG_A6XX_RB_RB_SUB_BLOCK_SEL_CNTL_HOST,
	.cd_reg = REG_A6XX_RB_RB_SUB_BLOCK_SEL_CNTL_CD,
	.val = 0x0,
};

static const struct gen7_sel_reg gen7_0_0_rb_rbp_sel = {
	.host_reg = REG_A6XX_RB_RB_SUB_BLOCK_SEL_CNTL_HOST,
	.cd_reg = REG_A6XX_RB_RB_SUB_BLOCK_SEL_CNTL_CD,
	.val = 0x9,
};

static struct gen7_cluster_registers gen7_0_0_clusters[] = {
	{ A7XX_CLUSTER_NONE, A7XX_PIPE_BR, STATE_NON_CONTEXT,
		gen7_0_0_noncontext_pipe_br_registers, },
	{ A7XX_CLUSTER_NONE, A7XX_PIPE_BV, STATE_NON_CONTEXT,
		gen7_0_0_noncontext_pipe_bv_registers, },
	{ A7XX_CLUSTER_NONE, A7XX_PIPE_LPAC, STATE_NON_CONTEXT,
		gen7_0_0_noncontext_pipe_lpac_registers, },
	{ A7XX_CLUSTER_NONE, A7XX_PIPE_BR, STATE_NON_CONTEXT,
		gen7_0_0_noncontext_rb_rac_pipe_br_registers, &gen7_0_0_rb_rac_sel, },
	{ A7XX_CLUSTER_NONE, A7XX_PIPE_BR, STATE_NON_CONTEXT,
		gen7_0_0_noncontext_rb_rbp_pipe_br_registers, &gen7_0_0_rb_rbp_sel, },
	{ A7XX_CLUSTER_GRAS, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_gras_cluster_gras_pipe_br_registers, },
	{ A7XX_CLUSTER_GRAS, A7XX_PIPE_BV, STATE_FORCE_CTXT_0,
		gen7_0_0_gras_cluster_gras_pipe_bv_registers, },
	{ A7XX_CLUSTER_GRAS, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_gras_cluster_gras_pipe_br_registers, },
	{ A7XX_CLUSTER_GRAS, A7XX_PIPE_BV, STATE_FORCE_CTXT_1,
		gen7_0_0_gras_cluster_gras_pipe_bv_registers, },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_pc_cluster_fe_pipe_br_registers, },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BV, STATE_FORCE_CTXT_0,
		gen7_0_0_pc_cluster_fe_pipe_bv_registers, },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_pc_cluster_fe_pipe_br_registers, },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BV, STATE_FORCE_CTXT_1,
		gen7_0_0_pc_cluster_fe_pipe_bv_registers, },
	{ A7XX_CLUSTER_PS, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_rb_rac_cluster_ps_pipe_br_registers, &gen7_0_0_rb_rac_sel, },
	{ A7XX_CLUSTER_PS, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_rb_rac_cluster_ps_pipe_br_registers, &gen7_0_0_rb_rac_sel, },
	{ A7XX_CLUSTER_PS, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_rb_rbp_cluster_ps_pipe_br_registers, &gen7_0_0_rb_rbp_sel, },
	{ A7XX_CLUSTER_PS, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_rb_rbp_cluster_ps_pipe_br_registers, &gen7_0_0_rb_rbp_sel, },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_vfd_cluster_fe_pipe_br_registers, },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BV, STATE_FORCE_CTXT_0,
		gen7_0_0_vfd_cluster_fe_pipe_bv_registers, },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_vfd_cluster_fe_pipe_br_registers, },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BV, STATE_FORCE_CTXT_1,
		gen7_0_0_vfd_cluster_fe_pipe_bv_registers, },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_vpc_cluster_fe_pipe_br_registers, },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BV, STATE_FORCE_CTXT_0,
		gen7_0_0_vpc_cluster_fe_pipe_bv_registers, },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_vpc_cluster_fe_pipe_br_registers, },
	{ A7XX_CLUSTER_FE, A7XX_PIPE_BV, STATE_FORCE_CTXT_1,
		gen7_0_0_vpc_cluster_fe_pipe_bv_registers, },
	{ A7XX_CLUSTER_PC_VS, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_vpc_cluster_pc_vs_pipe_br_registers, },
	{ A7XX_CLUSTER_PC_VS, A7XX_PIPE_BV, STATE_FORCE_CTXT_0,
		gen7_0_0_vpc_cluster_pc_vs_pipe_bv_registers, },
	{ A7XX_CLUSTER_PC_VS, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_vpc_cluster_pc_vs_pipe_br_registers, },
	{ A7XX_CLUSTER_PC_VS, A7XX_PIPE_BV, STATE_FORCE_CTXT_1,
		gen7_0_0_vpc_cluster_pc_vs_pipe_bv_registers, },
	{ A7XX_CLUSTER_VPC_PS, A7XX_PIPE_BR, STATE_FORCE_CTXT_0,
		gen7_0_0_vpc_cluster_vpc_ps_pipe_br_registers, },
	{ A7XX_CLUSTER_VPC_PS, A7XX_PIPE_BV, STATE_FORCE_CTXT_0,
		gen7_0_0_vpc_cluster_vpc_ps_pipe_bv_registers, },
	{ A7XX_CLUSTER_VPC_PS, A7XX_PIPE_BR, STATE_FORCE_CTXT_1,
		gen7_0_0_vpc_cluster_vpc_ps_pipe_br_registers, },
	{ A7XX_CLUSTER_VPC_PS, A7XX_PIPE_BV, STATE_FORCE_CTXT_1,
		gen7_0_0_vpc_cluster_vpc_ps_pipe_bv_registers, },
};

static struct gen7_sptp_cluster_registers gen7_0_0_sptp_clusters[] = {
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, A7XX_PIPE_BR, 0, A7XX_HLSQ_STATE,
		gen7_0_0_sp_noncontext_pipe_br_hlsq_state_registers, 0xae00 },
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, A7XX_PIPE_BR, 0, A7XX_SP_TOP,
		gen7_0_0_sp_noncontext_pipe_br_sp_top_registers, 0xae00 },
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, A7XX_PIPE_BR, 0, A7XX_USPTP,
		gen7_0_0_sp_noncontext_pipe_br_usptp_registers, 0xae00 },
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, A7XX_PIPE_LPAC, 0, A7XX_HLSQ_STATE,
		gen7_0_0_sp_noncontext_pipe_lpac_hlsq_state_registers, 0xaf80 },
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, A7XX_PIPE_LPAC, 0, A7XX_SP_TOP,
		gen7_0_0_sp_noncontext_pipe_lpac_sp_top_registers, 0xaf80 },
	{ A7XX_CLUSTER_NONE, A7XX_SP_NCTX_REG, A7XX_PIPE_LPAC, 0, A7XX_USPTP,
		gen7_0_0_sp_noncontext_pipe_lpac_usptp_registers, 0xaf80 },
	{ A7XX_CLUSTER_NONE, A7XX_TP0_NCTX_REG, A7XX_PIPE_BR, 0, A7XX_USPTP,
		gen7_0_0_tpl1_noncontext_pipe_br_registers, 0xb600 },
	{ A7XX_CLUSTER_NONE, A7XX_TP0_NCTX_REG, A7XX_PIPE_LPAC, 0, A7XX_USPTP,
		gen7_0_0_tpl1_noncontext_pipe_lpac_registers, 0xb780 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_BR, 0, A7XX_HLSQ_STATE,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_BR, 0, A7XX_HLSQ_DP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_dp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_BR, 0, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_BR, 0, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, A7XX_PIPE_BR, 1, A7XX_HLSQ_STATE,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, A7XX_PIPE_BR, 1, A7XX_HLSQ_DP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_dp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, A7XX_PIPE_BR, 1, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX1_3D_CPS_REG, A7XX_PIPE_BR, 1, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX2_3D_CPS_REG, A7XX_PIPE_BR, 2, A7XX_HLSQ_STATE,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX2_3D_CPS_REG, A7XX_PIPE_BR, 2, A7XX_HLSQ_DP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_dp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX2_3D_CPS_REG, A7XX_PIPE_BR, 2, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX2_3D_CPS_REG, A7XX_PIPE_BR, 2, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX3_3D_CPS_REG, A7XX_PIPE_BR, 3, A7XX_HLSQ_STATE,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX3_3D_CPS_REG, A7XX_PIPE_BR, 3, A7XX_HLSQ_DP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_hlsq_dp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX3_3D_CPS_REG, A7XX_PIPE_BR, 3, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX3_3D_CPS_REG, A7XX_PIPE_BR, 3, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_ps_pipe_br_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_LPAC, 0, A7XX_HLSQ_STATE,
		gen7_0_0_sp_cluster_sp_ps_pipe_lpac_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_LPAC, 0, A7XX_HLSQ_DP,
		gen7_0_0_sp_cluster_sp_ps_pipe_lpac_hlsq_dp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_LPAC, 0, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_ps_pipe_lpac_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_SP_CTX0_3D_CPS_REG, A7XX_PIPE_LPAC, 0, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_ps_pipe_lpac_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BR, 0, A7XX_HLSQ_STATE,
		gen7_0_0_sp_cluster_sp_vs_pipe_br_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BV, 0, A7XX_HLSQ_STATE,
		gen7_0_0_sp_cluster_sp_vs_pipe_bv_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BR, 0, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_vs_pipe_br_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BV, 0, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_vs_pipe_bv_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BR, 0, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_vs_pipe_br_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX0_3D_CVS_REG, A7XX_PIPE_BV, 0, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_vs_pipe_bv_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BR, 1, A7XX_HLSQ_STATE,
		gen7_0_0_sp_cluster_sp_vs_pipe_br_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BV, 1, A7XX_HLSQ_STATE,
		gen7_0_0_sp_cluster_sp_vs_pipe_bv_hlsq_state_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BR, 1, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_vs_pipe_br_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BV, 1, A7XX_SP_TOP,
		gen7_0_0_sp_cluster_sp_vs_pipe_bv_sp_top_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BR, 1, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_vs_pipe_br_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_VS, A7XX_SP_CTX1_3D_CVS_REG, A7XX_PIPE_BV, 1, A7XX_USPTP,
		gen7_0_0_sp_cluster_sp_vs_pipe_bv_usptp_registers, 0xa800 },
	{ A7XX_CLUSTER_SP_PS, A7XX_TP0_CTX0_3D_CPS_REG, A7XX_PIPE_BR, 0, A7XX_USPTP,
		gen7_0_0_tpl1_cluster_sp_ps_pipe_br_registers, 0xb000 },
	{ A7XX_CLUSTER_SP_PS, A7XX_TP0_CTX1_3D_CPS_REG, A7XX_PIPE_BR, 1, A7XX_USPTP,
		gen7_0_0_tpl1_cluster_sp_ps_pipe_br_registers, 0xb000 },
	{ A7XX_CLUSTER_SP_PS, A7XX_TP0_CTX2_3D_CPS_REG, A7XX_PIPE_BR, 2, A7XX_USPTP,
		gen7_0_0_tpl1_cluster_sp_ps_pipe_br_registers, 0xb000 },
	{ A7XX_CLUSTER_SP_PS, A7XX_TP0_CTX3_3D_CPS_REG, A7XX_PIPE_BR, 3, A7XX_USPTP,
		gen7_0_0_tpl1_cluster_sp_ps_pipe_br_registers, 0xb000 },
	{ A7XX_CLUSTER_SP_PS, A7XX_TP0_CTX0_3D_CPS_REG, A7XX_PIPE_LPAC, 0, A7XX_USPTP,
		gen7_0_0_tpl1_cluster_sp_ps_pipe_lpac_registers, 0xb000 },
	{ A7XX_CLUSTER_SP_VS, A7XX_TP0_CTX0_3D_CVS_REG, A7XX_PIPE_BR, 0, A7XX_USPTP,
		gen7_0_0_tpl1_cluster_sp_vs_pipe_br_registers, 0xb000 },
	{ A7XX_CLUSTER_SP_VS, A7XX_TP0_CTX0_3D_CVS_REG, A7XX_PIPE_BV, 0, A7XX_USPTP,
		gen7_0_0_tpl1_cluster_sp_vs_pipe_bv_registers, 0xb000 },
	{ A7XX_CLUSTER_SP_VS, A7XX_TP0_CTX1_3D_CVS_REG, A7XX_PIPE_BR, 1, A7XX_USPTP,
		gen7_0_0_tpl1_cluster_sp_vs_pipe_br_registers, 0xb000 },
	{ A7XX_CLUSTER_SP_VS, A7XX_TP0_CTX1_3D_CVS_REG, A7XX_PIPE_BV, 1, A7XX_USPTP,
		gen7_0_0_tpl1_cluster_sp_vs_pipe_bv_registers, 0xb000 },
};

static const u32 gen7_0_0_rscc_registers[] = {
	0x14000, 0x14036, 0x14040, 0x14042, 0x14080, 0x14084, 0x14089, 0x1408c,
	0x14091, 0x14094, 0x14099, 0x1409c, 0x140a1, 0x140a4, 0x140a9, 0x140ac,
	0x14100, 0x14102, 0x14114, 0x14119, 0x14124, 0x1412e, 0x14140, 0x14143,
	0x14180, 0x14197, 0x14340, 0x14342, 0x14344, 0x14347, 0x1434c, 0x14373,
	0x143ec, 0x143ef, 0x143f4, 0x1441b, 0x14494, 0x14497, 0x1449c, 0x144c3,
	0x1453c, 0x1453f, 0x14544, 0x1456b, 0x145e4, 0x145e7, 0x145ec, 0x14613,
	0x1468c, 0x1468f, 0x14694, 0x146bb, 0x14734, 0x14737, 0x1473c, 0x14763,
	0x147dc, 0x147df, 0x147e4, 0x1480b, 0x14884, 0x14887, 0x1488c, 0x148b3,
	0x1492c, 0x1492f, 0x14934, 0x1495b, 0x14f51, 0x14f54,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_rscc_registers), 8));

static const u32 gen7_0_0_cpr_registers[] = {
	0x26800, 0x26805, 0x26808, 0x2680c, 0x26814, 0x26814, 0x2681c, 0x2681c,
	0x26820, 0x26838, 0x26840, 0x26840, 0x26848, 0x26848, 0x26850, 0x26850,
	0x26880, 0x26898, 0x26980, 0x269b0, 0x269c0, 0x269c8, 0x269e0, 0x269ee,
	0x269fb, 0x269ff, 0x26a02, 0x26a07, 0x26a09, 0x26a0b, 0x26a10, 0x26b0f,
	0x27440, 0x27441, 0x27444, 0x27444, 0x27480, 0x274a2, 0x274ac, 0x274ac,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_cpr_registers), 8));

static const u32 gen7_0_0_gpucc_registers[] = {
	0x24000, 0x2400e, 0x24400, 0x2440e, 0x24800, 0x24805, 0x24c00, 0x24cff,
	0x25800, 0x25804, 0x25c00, 0x25c04, 0x26000, 0x26004, 0x26400, 0x26405,
	0x26414, 0x2641d, 0x2642a, 0x26430, 0x26432, 0x26432, 0x26441, 0x26455,
	0x26466, 0x26468, 0x26478, 0x2647a, 0x26489, 0x2648a, 0x2649c, 0x2649e,
	0x264a0, 0x264a3, 0x264b3, 0x264b5, 0x264c5, 0x264c7, 0x264d6, 0x264d8,
	0x264e8, 0x264e9, 0x264f9, 0x264fc, 0x2650b, 0x2650c, 0x2651c, 0x2651e,
	0x26540, 0x26570, 0x26600, 0x26616, 0x26620, 0x2662d,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_gpucc_registers), 8));

static const u32 gen7_0_0_cx_misc_registers[] = {
	0x27800, 0x27800, 0x27810, 0x27814, 0x27820, 0x27824, 0x27832, 0x27857,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_cx_misc_registers), 8));

static const u32 gen7_0_0_dpm_registers[] = {
	0x1aa00, 0x1aa06, 0x1aa09, 0x1aa0a, 0x1aa0c, 0x1aa0d, 0x1aa0f, 0x1aa12,
	0x1aa14, 0x1aa47, 0x1aa50, 0x1aa51,
	UINT_MAX, UINT_MAX,
};
static_assert(IS_ALIGNED(sizeof(gen7_0_0_dpm_registers), 8));

static struct gen7_reg_list gen7_0_0_reg_list[] = {
	{ gen7_0_0_gpu_registers, NULL },
	{ gen7_0_0_cx_misc_registers, NULL },
	{ gen7_0_0_dpm_registers, NULL },
	{ NULL, NULL },
};

static const u32 *gen7_0_0_external_core_regs[] = {
	gen7_0_0_gpucc_registers,
	gen7_0_0_cpr_registers,
};
#endif /*_ADRENO_GEN7_0_0_SNAPSHOT_H */
