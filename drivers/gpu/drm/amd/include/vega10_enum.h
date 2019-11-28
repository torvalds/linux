/*
 * Copyright (C) 2017  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#if !defined (_vega10_ENUM_HEADER)
#define _vega10_ENUM_HEADER

#ifndef _DRIVER_BUILD
#ifndef GL_ZERO
#define GL__ZERO                      BLEND_ZERO
#define GL__ONE                       BLEND_ONE
#define GL__SRC_COLOR                 BLEND_SRC_COLOR
#define GL__ONE_MINUS_SRC_COLOR       BLEND_ONE_MINUS_SRC_COLOR
#define GL__DST_COLOR                 BLEND_DST_COLOR
#define GL__ONE_MINUS_DST_COLOR       BLEND_ONE_MINUS_DST_COLOR
#define GL__SRC_ALPHA                 BLEND_SRC_ALPHA
#define GL__ONE_MINUS_SRC_ALPHA       BLEND_ONE_MINUS_SRC_ALPHA
#define GL__DST_ALPHA                 BLEND_DST_ALPHA
#define GL__ONE_MINUS_DST_ALPHA       BLEND_ONE_MINUS_DST_ALPHA
#define GL__SRC_ALPHA_SATURATE        BLEND_SRC_ALPHA_SATURATE
#define GL__CONSTANT_COLOR            BLEND_CONSTANT_COLOR
#define GL__ONE_MINUS_CONSTANT_COLOR  BLEND_ONE_MINUS_CONSTANT_COLOR
#define GL__CONSTANT_ALPHA            BLEND_CONSTANT_ALPHA
#define GL__ONE_MINUS_CONSTANT_ALPHA  BLEND_ONE_MINUS_CONSTANT_ALPHA
#endif
#endif

/*******************************************************
 * GDS DATA_TYPE Enums
 *******************************************************/

#ifndef ENUMS_GDS_PERFCOUNT_SELECT_H
#define ENUMS_GDS_PERFCOUNT_SELECT_H
typedef enum GDS_PERFCOUNT_SELECT {
 GDS_PERF_SEL_DS_ADDR_CONFL = 0,
 GDS_PERF_SEL_DS_BANK_CONFL = 1,
 GDS_PERF_SEL_WBUF_FLUSH = 2,
 GDS_PERF_SEL_WR_COMP = 3,
 GDS_PERF_SEL_WBUF_WR = 4,
 GDS_PERF_SEL_RBUF_HIT = 5,
 GDS_PERF_SEL_RBUF_MISS = 6,
 GDS_PERF_SEL_SE0_SH0_NORET = 7,
 GDS_PERF_SEL_SE0_SH0_RET = 8,
 GDS_PERF_SEL_SE0_SH0_ORD_CNT = 9,
 GDS_PERF_SEL_SE0_SH0_2COMP_REQ = 10,
 GDS_PERF_SEL_SE0_SH0_ORD_WAVE_VALID = 11,
 GDS_PERF_SEL_SE0_SH0_GDS_DATA_VALID = 12,
 GDS_PERF_SEL_SE0_SH0_GDS_STALL_BY_ORD = 13,
 GDS_PERF_SEL_SE0_SH0_GDS_WR_OP = 14,
 GDS_PERF_SEL_SE0_SH0_GDS_RD_OP = 15,
 GDS_PERF_SEL_SE0_SH0_GDS_ATOM_OP = 16,
 GDS_PERF_SEL_SE0_SH0_GDS_REL_OP = 17,
 GDS_PERF_SEL_SE0_SH0_GDS_CMPXCH_OP = 18,
 GDS_PERF_SEL_SE0_SH0_GDS_BYTE_OP = 19,
 GDS_PERF_SEL_SE0_SH0_GDS_SHORT_OP = 20,
 GDS_PERF_SEL_SE0_SH1_NORET = 21,
 GDS_PERF_SEL_SE0_SH1_RET = 22,
 GDS_PERF_SEL_SE0_SH1_ORD_CNT = 23,
 GDS_PERF_SEL_SE0_SH1_2COMP_REQ = 24,
 GDS_PERF_SEL_SE0_SH1_ORD_WAVE_VALID = 25,
 GDS_PERF_SEL_SE0_SH1_GDS_DATA_VALID = 26,
 GDS_PERF_SEL_SE0_SH1_GDS_STALL_BY_ORD = 27,
 GDS_PERF_SEL_SE0_SH1_GDS_WR_OP = 28,
 GDS_PERF_SEL_SE0_SH1_GDS_RD_OP = 29,
 GDS_PERF_SEL_SE0_SH1_GDS_ATOM_OP = 30,
 GDS_PERF_SEL_SE0_SH1_GDS_REL_OP = 31,
 GDS_PERF_SEL_SE0_SH1_GDS_CMPXCH_OP = 32,
 GDS_PERF_SEL_SE0_SH1_GDS_BYTE_OP = 33,
 GDS_PERF_SEL_SE0_SH1_GDS_SHORT_OP = 34,
 GDS_PERF_SEL_SE1_SH0_NORET = 35,
 GDS_PERF_SEL_SE1_SH0_RET = 36,
 GDS_PERF_SEL_SE1_SH0_ORD_CNT = 37,
 GDS_PERF_SEL_SE1_SH0_2COMP_REQ = 38,
 GDS_PERF_SEL_SE1_SH0_ORD_WAVE_VALID = 39,
 GDS_PERF_SEL_SE1_SH0_GDS_DATA_VALID = 40,
 GDS_PERF_SEL_SE1_SH0_GDS_STALL_BY_ORD = 41,
 GDS_PERF_SEL_SE1_SH0_GDS_WR_OP = 42,
 GDS_PERF_SEL_SE1_SH0_GDS_RD_OP = 43,
 GDS_PERF_SEL_SE1_SH0_GDS_ATOM_OP = 44,
 GDS_PERF_SEL_SE1_SH0_GDS_REL_OP = 45,
 GDS_PERF_SEL_SE1_SH0_GDS_CMPXCH_OP = 46,
 GDS_PERF_SEL_SE1_SH0_GDS_BYTE_OP = 47,
 GDS_PERF_SEL_SE1_SH0_GDS_SHORT_OP = 48,
 GDS_PERF_SEL_SE1_SH1_NORET = 49,
 GDS_PERF_SEL_SE1_SH1_RET = 50,
 GDS_PERF_SEL_SE1_SH1_ORD_CNT = 51,
 GDS_PERF_SEL_SE1_SH1_2COMP_REQ = 52,
 GDS_PERF_SEL_SE1_SH1_ORD_WAVE_VALID = 53,
 GDS_PERF_SEL_SE1_SH1_GDS_DATA_VALID = 54,
 GDS_PERF_SEL_SE1_SH1_GDS_STALL_BY_ORD = 55,
 GDS_PERF_SEL_SE1_SH1_GDS_WR_OP = 56,
 GDS_PERF_SEL_SE1_SH1_GDS_RD_OP = 57,
 GDS_PERF_SEL_SE1_SH1_GDS_ATOM_OP = 58,
 GDS_PERF_SEL_SE1_SH1_GDS_REL_OP = 59,
 GDS_PERF_SEL_SE1_SH1_GDS_CMPXCH_OP = 60,
 GDS_PERF_SEL_SE1_SH1_GDS_BYTE_OP = 61,
 GDS_PERF_SEL_SE1_SH1_GDS_SHORT_OP = 62,
 GDS_PERF_SEL_SE2_SH0_NORET = 63,
 GDS_PERF_SEL_SE2_SH0_RET = 64,
 GDS_PERF_SEL_SE2_SH0_ORD_CNT = 65,
 GDS_PERF_SEL_SE2_SH0_2COMP_REQ = 66,
 GDS_PERF_SEL_SE2_SH0_ORD_WAVE_VALID = 67,
 GDS_PERF_SEL_SE2_SH0_GDS_DATA_VALID = 68,
 GDS_PERF_SEL_SE2_SH0_GDS_STALL_BY_ORD = 69,
 GDS_PERF_SEL_SE2_SH0_GDS_WR_OP = 70,
 GDS_PERF_SEL_SE2_SH0_GDS_RD_OP = 71,
 GDS_PERF_SEL_SE2_SH0_GDS_ATOM_OP = 72,
 GDS_PERF_SEL_SE2_SH0_GDS_REL_OP = 73,
 GDS_PERF_SEL_SE2_SH0_GDS_CMPXCH_OP = 74,
 GDS_PERF_SEL_SE2_SH0_GDS_BYTE_OP = 75,
 GDS_PERF_SEL_SE2_SH0_GDS_SHORT_OP = 76,
 GDS_PERF_SEL_SE2_SH1_NORET = 77,
 GDS_PERF_SEL_SE2_SH1_RET = 78,
 GDS_PERF_SEL_SE2_SH1_ORD_CNT = 79,
 GDS_PERF_SEL_SE2_SH1_2COMP_REQ = 80,
 GDS_PERF_SEL_SE2_SH1_ORD_WAVE_VALID = 81,
 GDS_PERF_SEL_SE2_SH1_GDS_DATA_VALID = 82,
 GDS_PERF_SEL_SE2_SH1_GDS_STALL_BY_ORD = 83,
 GDS_PERF_SEL_SE2_SH1_GDS_WR_OP = 84,
 GDS_PERF_SEL_SE2_SH1_GDS_RD_OP = 85,
 GDS_PERF_SEL_SE2_SH1_GDS_ATOM_OP = 86,
 GDS_PERF_SEL_SE2_SH1_GDS_REL_OP = 87,
 GDS_PERF_SEL_SE2_SH1_GDS_CMPXCH_OP = 88,
 GDS_PERF_SEL_SE2_SH1_GDS_BYTE_OP = 89,
 GDS_PERF_SEL_SE2_SH1_GDS_SHORT_OP = 90,
 GDS_PERF_SEL_SE3_SH0_NORET = 91,
 GDS_PERF_SEL_SE3_SH0_RET = 92,
 GDS_PERF_SEL_SE3_SH0_ORD_CNT = 93,
 GDS_PERF_SEL_SE3_SH0_2COMP_REQ = 94,
 GDS_PERF_SEL_SE3_SH0_ORD_WAVE_VALID = 95,
 GDS_PERF_SEL_SE3_SH0_GDS_DATA_VALID = 96,
 GDS_PERF_SEL_SE3_SH0_GDS_STALL_BY_ORD = 97,
 GDS_PERF_SEL_SE3_SH0_GDS_WR_OP = 98,
 GDS_PERF_SEL_SE3_SH0_GDS_RD_OP = 99,
 GDS_PERF_SEL_SE3_SH0_GDS_ATOM_OP = 100,
 GDS_PERF_SEL_SE3_SH0_GDS_REL_OP = 101,
 GDS_PERF_SEL_SE3_SH0_GDS_CMPXCH_OP = 102,
 GDS_PERF_SEL_SE3_SH0_GDS_BYTE_OP = 103,
 GDS_PERF_SEL_SE3_SH0_GDS_SHORT_OP = 104,
 GDS_PERF_SEL_SE3_SH1_NORET = 105,
 GDS_PERF_SEL_SE3_SH1_RET = 106,
 GDS_PERF_SEL_SE3_SH1_ORD_CNT = 107,
 GDS_PERF_SEL_SE3_SH1_2COMP_REQ = 108,
 GDS_PERF_SEL_SE3_SH1_ORD_WAVE_VALID = 109,
 GDS_PERF_SEL_SE3_SH1_GDS_DATA_VALID = 110,
 GDS_PERF_SEL_SE3_SH1_GDS_STALL_BY_ORD = 111,
 GDS_PERF_SEL_SE3_SH1_GDS_WR_OP = 112,
 GDS_PERF_SEL_SE3_SH1_GDS_RD_OP = 113,
 GDS_PERF_SEL_SE3_SH1_GDS_ATOM_OP = 114,
 GDS_PERF_SEL_SE3_SH1_GDS_REL_OP = 115,
 GDS_PERF_SEL_SE3_SH1_GDS_CMPXCH_OP = 116,
 GDS_PERF_SEL_SE3_SH1_GDS_BYTE_OP = 117,
 GDS_PERF_SEL_SE3_SH1_GDS_SHORT_OP = 118,
 GDS_PERF_SEL_GWS_RELEASED = 119,
 GDS_PERF_SEL_GWS_BYPASS = 120,
} GDS_PERFCOUNT_SELECT;
#endif /*ENUMS_GDS_PERFCOUNT_SELECT_H*/

/*******************************************************
 * Chip Enums
 *******************************************************/

/*
 * MEM_PWR_FORCE_CTRL enum
 */

typedef enum MEM_PWR_FORCE_CTRL {
NO_FORCE_REQUEST                         = 0x00000000,
FORCE_LIGHT_SLEEP_REQUEST                = 0x00000001,
FORCE_DEEP_SLEEP_REQUEST                 = 0x00000002,
FORCE_SHUT_DOWN_REQUEST                  = 0x00000003,
} MEM_PWR_FORCE_CTRL;

/*
 * MEM_PWR_FORCE_CTRL2 enum
 */

typedef enum MEM_PWR_FORCE_CTRL2 {
NO_FORCE_REQ                             = 0x00000000,
FORCE_LIGHT_SLEEP_REQ                    = 0x00000001,
} MEM_PWR_FORCE_CTRL2;

/*
 * MEM_PWR_DIS_CTRL enum
 */

typedef enum MEM_PWR_DIS_CTRL {
ENABLE_MEM_PWR_CTRL                      = 0x00000000,
DISABLE_MEM_PWR_CTRL                     = 0x00000001,
} MEM_PWR_DIS_CTRL;

/*
 * MEM_PWR_SEL_CTRL enum
 */

typedef enum MEM_PWR_SEL_CTRL {
DYNAMIC_SHUT_DOWN_ENABLE                 = 0x00000000,
DYNAMIC_DEEP_SLEEP_ENABLE                = 0x00000001,
DYNAMIC_LIGHT_SLEEP_ENABLE               = 0x00000002,
} MEM_PWR_SEL_CTRL;

/*
 * MEM_PWR_SEL_CTRL2 enum
 */

typedef enum MEM_PWR_SEL_CTRL2 {
DYNAMIC_DEEP_SLEEP_EN                    = 0x00000000,
DYNAMIC_LIGHT_SLEEP_EN                   = 0x00000001,
} MEM_PWR_SEL_CTRL2;

/*
 * RowSize enum
 */

typedef enum RowSize {
ADDR_CONFIG_1KB_ROW                      = 0x00000000,
ADDR_CONFIG_2KB_ROW                      = 0x00000001,
ADDR_CONFIG_4KB_ROW                      = 0x00000002,
} RowSize;

/*
 * SurfaceEndian enum
 */

typedef enum SurfaceEndian {
ENDIAN_NONE                              = 0x00000000,
ENDIAN_8IN16                             = 0x00000001,
ENDIAN_8IN32                             = 0x00000002,
ENDIAN_8IN64                             = 0x00000003,
} SurfaceEndian;

/*
 * ArrayMode enum
 */

typedef enum ArrayMode {
ARRAY_LINEAR_GENERAL                     = 0x00000000,
ARRAY_LINEAR_ALIGNED                     = 0x00000001,
ARRAY_1D_TILED_THIN1                     = 0x00000002,
ARRAY_1D_TILED_THICK                     = 0x00000003,
ARRAY_2D_TILED_THIN1                     = 0x00000004,
ARRAY_PRT_TILED_THIN1                    = 0x00000005,
ARRAY_PRT_2D_TILED_THIN1                 = 0x00000006,
ARRAY_2D_TILED_THICK                     = 0x00000007,
ARRAY_2D_TILED_XTHICK                    = 0x00000008,
ARRAY_PRT_TILED_THICK                    = 0x00000009,
ARRAY_PRT_2D_TILED_THICK                 = 0x0000000a,
ARRAY_PRT_3D_TILED_THIN1                 = 0x0000000b,
ARRAY_3D_TILED_THIN1                     = 0x0000000c,
ARRAY_3D_TILED_THICK                     = 0x0000000d,
ARRAY_3D_TILED_XTHICK                    = 0x0000000e,
ARRAY_PRT_3D_TILED_THICK                 = 0x0000000f,
} ArrayMode;

/*
 * NumPipes enum
 */

typedef enum NumPipes {
ADDR_CONFIG_1_PIPE                       = 0x00000000,
ADDR_CONFIG_2_PIPE                       = 0x00000001,
ADDR_CONFIG_4_PIPE                       = 0x00000002,
ADDR_CONFIG_8_PIPE                       = 0x00000003,
ADDR_CONFIG_16_PIPE                      = 0x00000004,
ADDR_CONFIG_32_PIPE                      = 0x00000005,
} NumPipes;

/*
 * NumBanksConfig enum
 */

typedef enum NumBanksConfig {
ADDR_CONFIG_1_BANK                       = 0x00000000,
ADDR_CONFIG_2_BANK                       = 0x00000001,
ADDR_CONFIG_4_BANK                       = 0x00000002,
ADDR_CONFIG_8_BANK                       = 0x00000003,
ADDR_CONFIG_16_BANK                      = 0x00000004,
} NumBanksConfig;

/*
 * PipeInterleaveSize enum
 */

typedef enum PipeInterleaveSize {
ADDR_CONFIG_PIPE_INTERLEAVE_256B         = 0x00000000,
ADDR_CONFIG_PIPE_INTERLEAVE_512B         = 0x00000001,
ADDR_CONFIG_PIPE_INTERLEAVE_1KB          = 0x00000002,
ADDR_CONFIG_PIPE_INTERLEAVE_2KB          = 0x00000003,
} PipeInterleaveSize;

/*
 * BankInterleaveSize enum
 */

typedef enum BankInterleaveSize {
ADDR_CONFIG_BANK_INTERLEAVE_1            = 0x00000000,
ADDR_CONFIG_BANK_INTERLEAVE_2            = 0x00000001,
ADDR_CONFIG_BANK_INTERLEAVE_4            = 0x00000002,
ADDR_CONFIG_BANK_INTERLEAVE_8            = 0x00000003,
} BankInterleaveSize;

/*
 * NumShaderEngines enum
 */

typedef enum NumShaderEngines {
ADDR_CONFIG_1_SHADER_ENGINE              = 0x00000000,
ADDR_CONFIG_2_SHADER_ENGINE              = 0x00000001,
ADDR_CONFIG_4_SHADER_ENGINE              = 0x00000002,
ADDR_CONFIG_8_SHADER_ENGINE              = 0x00000003,
} NumShaderEngines;

/*
 * NumRbPerShaderEngine enum
 */

typedef enum NumRbPerShaderEngine {
ADDR_CONFIG_1_RB_PER_SHADER_ENGINE       = 0x00000000,
ADDR_CONFIG_2_RB_PER_SHADER_ENGINE       = 0x00000001,
ADDR_CONFIG_4_RB_PER_SHADER_ENGINE       = 0x00000002,
} NumRbPerShaderEngine;

/*
 * NumGPUs enum
 */

typedef enum NumGPUs {
ADDR_CONFIG_1_GPU                        = 0x00000000,
ADDR_CONFIG_2_GPU                        = 0x00000001,
ADDR_CONFIG_4_GPU                        = 0x00000002,
ADDR_CONFIG_8_GPU                        = 0x00000003,
} NumGPUs;

/*
 * NumMaxCompressedFragments enum
 */

typedef enum NumMaxCompressedFragments {
ADDR_CONFIG_1_MAX_COMPRESSED_FRAGMENTS   = 0x00000000,
ADDR_CONFIG_2_MAX_COMPRESSED_FRAGMENTS   = 0x00000001,
ADDR_CONFIG_4_MAX_COMPRESSED_FRAGMENTS   = 0x00000002,
ADDR_CONFIG_8_MAX_COMPRESSED_FRAGMENTS   = 0x00000003,
} NumMaxCompressedFragments;

/*
 * ShaderEngineTileSize enum
 */

typedef enum ShaderEngineTileSize {
ADDR_CONFIG_SE_TILE_16                   = 0x00000000,
ADDR_CONFIG_SE_TILE_32                   = 0x00000001,
} ShaderEngineTileSize;

/*
 * MultiGPUTileSize enum
 */

typedef enum MultiGPUTileSize {
ADDR_CONFIG_GPU_TILE_16                  = 0x00000000,
ADDR_CONFIG_GPU_TILE_32                  = 0x00000001,
ADDR_CONFIG_GPU_TILE_64                  = 0x00000002,
ADDR_CONFIG_GPU_TILE_128                 = 0x00000003,
} MultiGPUTileSize;

/*
 * NumLowerPipes enum
 */

typedef enum NumLowerPipes {
ADDR_CONFIG_1_LOWER_PIPES                = 0x00000000,
ADDR_CONFIG_2_LOWER_PIPES                = 0x00000001,
} NumLowerPipes;

/*
 * ColorTransform enum
 */

typedef enum ColorTransform {
DCC_CT_AUTO                              = 0x00000000,
DCC_CT_NONE                              = 0x00000001,
ABGR_TO_A_BG_G_RB                        = 0x00000002,
BGRA_TO_BG_G_RB_A                        = 0x00000003,
} ColorTransform;

/*
 * CompareRef enum
 */

typedef enum CompareRef {
REF_NEVER                                = 0x00000000,
REF_LESS                                 = 0x00000001,
REF_EQUAL                                = 0x00000002,
REF_LEQUAL                               = 0x00000003,
REF_GREATER                              = 0x00000004,
REF_NOTEQUAL                             = 0x00000005,
REF_GEQUAL                               = 0x00000006,
REF_ALWAYS                               = 0x00000007,
} CompareRef;

/*
 * ReadSize enum
 */

typedef enum ReadSize {
READ_256_BITS                            = 0x00000000,
READ_512_BITS                            = 0x00000001,
} ReadSize;

/*
 * DepthFormat enum
 */

typedef enum DepthFormat {
DEPTH_INVALID                            = 0x00000000,
DEPTH_16                                 = 0x00000001,
DEPTH_X8_24                              = 0x00000002,
DEPTH_8_24                               = 0x00000003,
DEPTH_X8_24_FLOAT                        = 0x00000004,
DEPTH_8_24_FLOAT                         = 0x00000005,
DEPTH_32_FLOAT                           = 0x00000006,
DEPTH_X24_8_32_FLOAT                     = 0x00000007,
} DepthFormat;

/*
 * ZFormat enum
 */

typedef enum ZFormat {
Z_INVALID                                = 0x00000000,
Z_16                                     = 0x00000001,
Z_24                                     = 0x00000002,
Z_32_FLOAT                               = 0x00000003,
} ZFormat;

/*
 * StencilFormat enum
 */

typedef enum StencilFormat {
STENCIL_INVALID                          = 0x00000000,
STENCIL_8                                = 0x00000001,
} StencilFormat;

/*
 * CmaskMode enum
 */

typedef enum CmaskMode {
CMASK_CLEAR_NONE                         = 0x00000000,
CMASK_CLEAR_ONE                          = 0x00000001,
CMASK_CLEAR_ALL                          = 0x00000002,
CMASK_ANY_EXPANDED                       = 0x00000003,
CMASK_ALPHA0_FRAG1                       = 0x00000004,
CMASK_ALPHA0_FRAG2                       = 0x00000005,
CMASK_ALPHA0_FRAG4                       = 0x00000006,
CMASK_ALPHA0_FRAGS                       = 0x00000007,
CMASK_ALPHA1_FRAG1                       = 0x00000008,
CMASK_ALPHA1_FRAG2                       = 0x00000009,
CMASK_ALPHA1_FRAG4                       = 0x0000000a,
CMASK_ALPHA1_FRAGS                       = 0x0000000b,
CMASK_ALPHAX_FRAG1                       = 0x0000000c,
CMASK_ALPHAX_FRAG2                       = 0x0000000d,
CMASK_ALPHAX_FRAG4                       = 0x0000000e,
CMASK_ALPHAX_FRAGS                       = 0x0000000f,
} CmaskMode;

/*
 * QuadExportFormat enum
 */

typedef enum QuadExportFormat {
EXPORT_UNUSED                            = 0x00000000,
EXPORT_32_R                              = 0x00000001,
EXPORT_32_GR                             = 0x00000002,
EXPORT_32_AR                             = 0x00000003,
EXPORT_FP16_ABGR                         = 0x00000004,
EXPORT_UNSIGNED16_ABGR                   = 0x00000005,
EXPORT_SIGNED16_ABGR                     = 0x00000006,
EXPORT_32_ABGR                           = 0x00000007,
EXPORT_32BPP_8PIX                        = 0x00000008,
EXPORT_16_16_UNSIGNED_8PIX               = 0x00000009,
EXPORT_16_16_SIGNED_8PIX                 = 0x0000000a,
EXPORT_16_16_FLOAT_8PIX                  = 0x0000000b,
} QuadExportFormat;

/*
 * QuadExportFormatOld enum
 */

typedef enum QuadExportFormatOld {
EXPORT_4P_32BPC_ABGR                     = 0x00000000,
EXPORT_4P_16BPC_ABGR                     = 0x00000001,
EXPORT_4P_32BPC_GR                       = 0x00000002,
EXPORT_4P_32BPC_AR                       = 0x00000003,
EXPORT_2P_32BPC_ABGR                     = 0x00000004,
EXPORT_8P_32BPC_R                        = 0x00000005,
} QuadExportFormatOld;

/*
 * ColorFormat enum
 */

typedef enum ColorFormat {
COLOR_INVALID                            = 0x00000000,
COLOR_8                                  = 0x00000001,
COLOR_16                                 = 0x00000002,
COLOR_8_8                                = 0x00000003,
COLOR_32                                 = 0x00000004,
COLOR_16_16                              = 0x00000005,
COLOR_10_11_11                           = 0x00000006,
COLOR_11_11_10                           = 0x00000007,
COLOR_10_10_10_2                         = 0x00000008,
COLOR_2_10_10_10                         = 0x00000009,
COLOR_8_8_8_8                            = 0x0000000a,
COLOR_32_32                              = 0x0000000b,
COLOR_16_16_16_16                        = 0x0000000c,
COLOR_RESERVED_13                        = 0x0000000d,
COLOR_32_32_32_32                        = 0x0000000e,
COLOR_RESERVED_15                        = 0x0000000f,
COLOR_5_6_5                              = 0x00000010,
COLOR_1_5_5_5                            = 0x00000011,
COLOR_5_5_5_1                            = 0x00000012,
COLOR_4_4_4_4                            = 0x00000013,
COLOR_8_24                               = 0x00000014,
COLOR_24_8                               = 0x00000015,
COLOR_X24_8_32_FLOAT                     = 0x00000016,
COLOR_RESERVED_23                        = 0x00000017,
COLOR_RESERVED_24                        = 0x00000018,
COLOR_RESERVED_25                        = 0x00000019,
COLOR_RESERVED_26                        = 0x0000001a,
COLOR_RESERVED_27                        = 0x0000001b,
COLOR_RESERVED_28                        = 0x0000001c,
COLOR_RESERVED_29                        = 0x0000001d,
COLOR_RESERVED_30                        = 0x0000001e,
COLOR_2_10_10_10_6E4                     = 0x0000001f,
} ColorFormat;

/*
 * SurfaceFormat enum
 */

typedef enum SurfaceFormat {
FMT_INVALID                              = 0x00000000,
FMT_8                                    = 0x00000001,
FMT_16                                   = 0x00000002,
FMT_8_8                                  = 0x00000003,
FMT_32                                   = 0x00000004,
FMT_16_16                                = 0x00000005,
FMT_10_11_11                             = 0x00000006,
FMT_11_11_10                             = 0x00000007,
FMT_10_10_10_2                           = 0x00000008,
FMT_2_10_10_10                           = 0x00000009,
FMT_8_8_8_8                              = 0x0000000a,
FMT_32_32                                = 0x0000000b,
FMT_16_16_16_16                          = 0x0000000c,
FMT_32_32_32                             = 0x0000000d,
FMT_32_32_32_32                          = 0x0000000e,
FMT_RESERVED_4                           = 0x0000000f,
FMT_5_6_5                                = 0x00000010,
FMT_1_5_5_5                              = 0x00000011,
FMT_5_5_5_1                              = 0x00000012,
FMT_4_4_4_4                              = 0x00000013,
FMT_8_24                                 = 0x00000014,
FMT_24_8                                 = 0x00000015,
FMT_X24_8_32_FLOAT                       = 0x00000016,
FMT_RESERVED_33                          = 0x00000017,
FMT_11_11_10_FLOAT                       = 0x00000018,
FMT_16_FLOAT                             = 0x00000019,
FMT_32_FLOAT                             = 0x0000001a,
FMT_16_16_FLOAT                          = 0x0000001b,
FMT_8_24_FLOAT                           = 0x0000001c,
FMT_24_8_FLOAT                           = 0x0000001d,
FMT_32_32_FLOAT                          = 0x0000001e,
FMT_10_11_11_FLOAT                       = 0x0000001f,
FMT_16_16_16_16_FLOAT                    = 0x00000020,
FMT_3_3_2                                = 0x00000021,
FMT_6_5_5                                = 0x00000022,
FMT_32_32_32_32_FLOAT                    = 0x00000023,
FMT_RESERVED_36                          = 0x00000024,
FMT_1                                    = 0x00000025,
FMT_1_REVERSED                           = 0x00000026,
FMT_GB_GR                                = 0x00000027,
FMT_BG_RG                                = 0x00000028,
FMT_32_AS_8                              = 0x00000029,
FMT_32_AS_8_8                            = 0x0000002a,
FMT_5_9_9_9_SHAREDEXP                    = 0x0000002b,
FMT_8_8_8                                = 0x0000002c,
FMT_16_16_16                             = 0x0000002d,
FMT_16_16_16_FLOAT                       = 0x0000002e,
FMT_4_4                                  = 0x0000002f,
FMT_32_32_32_FLOAT                       = 0x00000030,
FMT_BC1                                  = 0x00000031,
FMT_BC2                                  = 0x00000032,
FMT_BC3                                  = 0x00000033,
FMT_BC4                                  = 0x00000034,
FMT_BC5                                  = 0x00000035,
FMT_BC6                                  = 0x00000036,
FMT_BC7                                  = 0x00000037,
FMT_32_AS_32_32_32_32                    = 0x00000038,
FMT_APC3                                 = 0x00000039,
FMT_APC4                                 = 0x0000003a,
FMT_APC5                                 = 0x0000003b,
FMT_APC6                                 = 0x0000003c,
FMT_APC7                                 = 0x0000003d,
FMT_CTX1                                 = 0x0000003e,
FMT_RESERVED_63                          = 0x0000003f,
} SurfaceFormat;

/*
 * BUF_DATA_FORMAT enum
 */

typedef enum BUF_DATA_FORMAT {
BUF_DATA_FORMAT_INVALID                  = 0x00000000,
BUF_DATA_FORMAT_8                        = 0x00000001,
BUF_DATA_FORMAT_16                       = 0x00000002,
BUF_DATA_FORMAT_8_8                      = 0x00000003,
BUF_DATA_FORMAT_32                       = 0x00000004,
BUF_DATA_FORMAT_16_16                    = 0x00000005,
BUF_DATA_FORMAT_10_11_11                 = 0x00000006,
BUF_DATA_FORMAT_11_11_10                 = 0x00000007,
BUF_DATA_FORMAT_10_10_10_2               = 0x00000008,
BUF_DATA_FORMAT_2_10_10_10               = 0x00000009,
BUF_DATA_FORMAT_8_8_8_8                  = 0x0000000a,
BUF_DATA_FORMAT_32_32                    = 0x0000000b,
BUF_DATA_FORMAT_16_16_16_16              = 0x0000000c,
BUF_DATA_FORMAT_32_32_32                 = 0x0000000d,
BUF_DATA_FORMAT_32_32_32_32              = 0x0000000e,
BUF_DATA_FORMAT_RESERVED_15              = 0x0000000f,
} BUF_DATA_FORMAT;

/*
 * IMG_DATA_FORMAT enum
 */

typedef enum IMG_DATA_FORMAT {
IMG_DATA_FORMAT_INVALID                  = 0x00000000,
IMG_DATA_FORMAT_8                        = 0x00000001,
IMG_DATA_FORMAT_16                       = 0x00000002,
IMG_DATA_FORMAT_8_8                      = 0x00000003,
IMG_DATA_FORMAT_32                       = 0x00000004,
IMG_DATA_FORMAT_16_16                    = 0x00000005,
IMG_DATA_FORMAT_10_11_11                 = 0x00000006,
IMG_DATA_FORMAT_11_11_10                 = 0x00000007,
IMG_DATA_FORMAT_10_10_10_2               = 0x00000008,
IMG_DATA_FORMAT_2_10_10_10               = 0x00000009,
IMG_DATA_FORMAT_8_8_8_8                  = 0x0000000a,
IMG_DATA_FORMAT_32_32                    = 0x0000000b,
IMG_DATA_FORMAT_16_16_16_16              = 0x0000000c,
IMG_DATA_FORMAT_32_32_32                 = 0x0000000d,
IMG_DATA_FORMAT_32_32_32_32              = 0x0000000e,
IMG_DATA_FORMAT_RESERVED_15              = 0x0000000f,
IMG_DATA_FORMAT_5_6_5                    = 0x00000010,
IMG_DATA_FORMAT_1_5_5_5                  = 0x00000011,
IMG_DATA_FORMAT_5_5_5_1                  = 0x00000012,
IMG_DATA_FORMAT_4_4_4_4                  = 0x00000013,
IMG_DATA_FORMAT_8_24                     = 0x00000014,
IMG_DATA_FORMAT_24_8                     = 0x00000015,
IMG_DATA_FORMAT_X24_8_32                 = 0x00000016,
IMG_DATA_FORMAT_8_AS_8_8_8_8             = 0x00000017,
IMG_DATA_FORMAT_ETC2_RGB                 = 0x00000018,
IMG_DATA_FORMAT_ETC2_RGBA                = 0x00000019,
IMG_DATA_FORMAT_ETC2_R                   = 0x0000001a,
IMG_DATA_FORMAT_ETC2_RG                  = 0x0000001b,
IMG_DATA_FORMAT_ETC2_RGBA1               = 0x0000001c,
IMG_DATA_FORMAT_RESERVED_29              = 0x0000001d,
IMG_DATA_FORMAT_RESERVED_30              = 0x0000001e,
IMG_DATA_FORMAT_6E4                      = 0x0000001f,
IMG_DATA_FORMAT_GB_GR                    = 0x00000020,
IMG_DATA_FORMAT_BG_RG                    = 0x00000021,
IMG_DATA_FORMAT_5_9_9_9                  = 0x00000022,
IMG_DATA_FORMAT_BC1                      = 0x00000023,
IMG_DATA_FORMAT_BC2                      = 0x00000024,
IMG_DATA_FORMAT_BC3                      = 0x00000025,
IMG_DATA_FORMAT_BC4                      = 0x00000026,
IMG_DATA_FORMAT_BC5                      = 0x00000027,
IMG_DATA_FORMAT_BC6                      = 0x00000028,
IMG_DATA_FORMAT_BC7                      = 0x00000029,
IMG_DATA_FORMAT_16_AS_32_32              = 0x0000002a,
IMG_DATA_FORMAT_16_AS_16_16_16_16        = 0x0000002b,
IMG_DATA_FORMAT_16_AS_32_32_32_32        = 0x0000002c,
IMG_DATA_FORMAT_FMASK                    = 0x0000002d,
IMG_DATA_FORMAT_ASTC_2D_LDR              = 0x0000002e,
IMG_DATA_FORMAT_ASTC_2D_HDR              = 0x0000002f,
IMG_DATA_FORMAT_ASTC_2D_LDR_SRGB         = 0x00000030,
IMG_DATA_FORMAT_ASTC_3D_LDR              = 0x00000031,
IMG_DATA_FORMAT_ASTC_3D_HDR              = 0x00000032,
IMG_DATA_FORMAT_ASTC_3D_LDR_SRGB         = 0x00000033,
IMG_DATA_FORMAT_N_IN_16                  = 0x00000034,
IMG_DATA_FORMAT_N_IN_16_16               = 0x00000035,
IMG_DATA_FORMAT_N_IN_16_16_16_16         = 0x00000036,
IMG_DATA_FORMAT_N_IN_16_AS_16_16_16_16   = 0x00000037,
IMG_DATA_FORMAT_RESERVED_56              = 0x00000038,
IMG_DATA_FORMAT_4_4                      = 0x00000039,
IMG_DATA_FORMAT_6_5_5                    = 0x0000003a,
IMG_DATA_FORMAT_RESERVED_59              = 0x0000003b,
IMG_DATA_FORMAT_RESERVED_60              = 0x0000003c,
IMG_DATA_FORMAT_8_AS_32                  = 0x0000003d,
IMG_DATA_FORMAT_8_AS_32_32               = 0x0000003e,
IMG_DATA_FORMAT_32_AS_32_32_32_32        = 0x0000003f,
} IMG_DATA_FORMAT;

/*
 * BUF_NUM_FORMAT enum
 */

typedef enum BUF_NUM_FORMAT {
BUF_NUM_FORMAT_UNORM                     = 0x00000000,
BUF_NUM_FORMAT_SNORM                     = 0x00000001,
BUF_NUM_FORMAT_USCALED                   = 0x00000002,
BUF_NUM_FORMAT_SSCALED                   = 0x00000003,
BUF_NUM_FORMAT_UINT                      = 0x00000004,
BUF_NUM_FORMAT_SINT                      = 0x00000005,
BUF_NUM_FORMAT_UNORM_UINT                = 0x00000006,
BUF_NUM_FORMAT_FLOAT                     = 0x00000007,
} BUF_NUM_FORMAT;

/*
 * IMG_NUM_FORMAT enum
 */

typedef enum IMG_NUM_FORMAT {
IMG_NUM_FORMAT_UNORM                     = 0x00000000,
IMG_NUM_FORMAT_SNORM                     = 0x00000001,
IMG_NUM_FORMAT_USCALED                   = 0x00000002,
IMG_NUM_FORMAT_SSCALED                   = 0x00000003,
IMG_NUM_FORMAT_UINT                      = 0x00000004,
IMG_NUM_FORMAT_SINT                      = 0x00000005,
IMG_NUM_FORMAT_UNORM_UINT                = 0x00000006,
IMG_NUM_FORMAT_FLOAT                     = 0x00000007,
IMG_NUM_FORMAT_RESERVED_8                = 0x00000008,
IMG_NUM_FORMAT_SRGB                      = 0x00000009,
IMG_NUM_FORMAT_RESERVED_10               = 0x0000000a,
IMG_NUM_FORMAT_RESERVED_11               = 0x0000000b,
IMG_NUM_FORMAT_RESERVED_12               = 0x0000000c,
IMG_NUM_FORMAT_RESERVED_13               = 0x0000000d,
IMG_NUM_FORMAT_RESERVED_14               = 0x0000000e,
IMG_NUM_FORMAT_RESERVED_15               = 0x0000000f,
} IMG_NUM_FORMAT;

/*
 * IMG_NUM_FORMAT_FMASK enum
 */

typedef enum IMG_NUM_FORMAT_FMASK {
IMG_NUM_FORMAT_FMASK_8_2_1               = 0x00000000,
IMG_NUM_FORMAT_FMASK_8_4_1               = 0x00000001,
IMG_NUM_FORMAT_FMASK_8_8_1               = 0x00000002,
IMG_NUM_FORMAT_FMASK_8_2_2               = 0x00000003,
IMG_NUM_FORMAT_FMASK_8_4_2               = 0x00000004,
IMG_NUM_FORMAT_FMASK_8_4_4               = 0x00000005,
IMG_NUM_FORMAT_FMASK_16_16_1             = 0x00000006,
IMG_NUM_FORMAT_FMASK_16_8_2              = 0x00000007,
IMG_NUM_FORMAT_FMASK_32_16_2             = 0x00000008,
IMG_NUM_FORMAT_FMASK_32_8_4              = 0x00000009,
IMG_NUM_FORMAT_FMASK_32_8_8              = 0x0000000a,
IMG_NUM_FORMAT_FMASK_64_16_4             = 0x0000000b,
IMG_NUM_FORMAT_FMASK_64_16_8             = 0x0000000c,
IMG_NUM_FORMAT_FMASK_RESERVED_13         = 0x0000000d,
IMG_NUM_FORMAT_FMASK_RESERVED_14         = 0x0000000e,
IMG_NUM_FORMAT_FMASK_RESERVED_15         = 0x0000000f,
} IMG_NUM_FORMAT_FMASK;

/*
 * IMG_NUM_FORMAT_N_IN_16 enum
 */

typedef enum IMG_NUM_FORMAT_N_IN_16 {
IMG_NUM_FORMAT_N_IN_16_RESERVED_0        = 0x00000000,
IMG_NUM_FORMAT_N_IN_16_UNORM_10          = 0x00000001,
IMG_NUM_FORMAT_N_IN_16_UNORM_9           = 0x00000002,
IMG_NUM_FORMAT_N_IN_16_RESERVED_3        = 0x00000003,
IMG_NUM_FORMAT_N_IN_16_UINT_10           = 0x00000004,
IMG_NUM_FORMAT_N_IN_16_UINT_9            = 0x00000005,
IMG_NUM_FORMAT_N_IN_16_RESERVED_6        = 0x00000006,
IMG_NUM_FORMAT_N_IN_16_UNORM_UINT_10     = 0x00000007,
IMG_NUM_FORMAT_N_IN_16_UNORM_UINT_9      = 0x00000008,
IMG_NUM_FORMAT_N_IN_16_RESERVED_9        = 0x00000009,
IMG_NUM_FORMAT_N_IN_16_RESERVED_10       = 0x0000000a,
IMG_NUM_FORMAT_N_IN_16_RESERVED_11       = 0x0000000b,
IMG_NUM_FORMAT_N_IN_16_RESERVED_12       = 0x0000000c,
IMG_NUM_FORMAT_N_IN_16_RESERVED_13       = 0x0000000d,
IMG_NUM_FORMAT_N_IN_16_RESERVED_14       = 0x0000000e,
IMG_NUM_FORMAT_N_IN_16_RESERVED_15       = 0x0000000f,
} IMG_NUM_FORMAT_N_IN_16;

/*
 * IMG_NUM_FORMAT_ASTC_2D enum
 */

typedef enum IMG_NUM_FORMAT_ASTC_2D {
IMG_NUM_FORMAT_ASTC_2D_4x4               = 0x00000000,
IMG_NUM_FORMAT_ASTC_2D_5x4               = 0x00000001,
IMG_NUM_FORMAT_ASTC_2D_5x5               = 0x00000002,
IMG_NUM_FORMAT_ASTC_2D_6x5               = 0x00000003,
IMG_NUM_FORMAT_ASTC_2D_6x6               = 0x00000004,
IMG_NUM_FORMAT_ASTC_2D_8x5               = 0x00000005,
IMG_NUM_FORMAT_ASTC_2D_8x6               = 0x00000006,
IMG_NUM_FORMAT_ASTC_2D_8x8               = 0x00000007,
IMG_NUM_FORMAT_ASTC_2D_10x5              = 0x00000008,
IMG_NUM_FORMAT_ASTC_2D_10x6              = 0x00000009,
IMG_NUM_FORMAT_ASTC_2D_10x8              = 0x0000000a,
IMG_NUM_FORMAT_ASTC_2D_10x10             = 0x0000000b,
IMG_NUM_FORMAT_ASTC_2D_12x10             = 0x0000000c,
IMG_NUM_FORMAT_ASTC_2D_12x12             = 0x0000000d,
IMG_NUM_FORMAT_ASTC_2D_RESERVED_14       = 0x0000000e,
IMG_NUM_FORMAT_ASTC_2D_RESERVED_15       = 0x0000000f,
} IMG_NUM_FORMAT_ASTC_2D;

/*
 * IMG_NUM_FORMAT_ASTC_3D enum
 */

typedef enum IMG_NUM_FORMAT_ASTC_3D {
IMG_NUM_FORMAT_ASTC_3D_3x3x3             = 0x00000000,
IMG_NUM_FORMAT_ASTC_3D_4x3x3             = 0x00000001,
IMG_NUM_FORMAT_ASTC_3D_4x4x3             = 0x00000002,
IMG_NUM_FORMAT_ASTC_3D_4x4x4             = 0x00000003,
IMG_NUM_FORMAT_ASTC_3D_5x4x4             = 0x00000004,
IMG_NUM_FORMAT_ASTC_3D_5x5x4             = 0x00000005,
IMG_NUM_FORMAT_ASTC_3D_5x5x5             = 0x00000006,
IMG_NUM_FORMAT_ASTC_3D_6x5x5             = 0x00000007,
IMG_NUM_FORMAT_ASTC_3D_6x6x5             = 0x00000008,
IMG_NUM_FORMAT_ASTC_3D_6x6x6             = 0x00000009,
IMG_NUM_FORMAT_ASTC_3D_RESERVED_10       = 0x0000000a,
IMG_NUM_FORMAT_ASTC_3D_RESERVED_11       = 0x0000000b,
IMG_NUM_FORMAT_ASTC_3D_RESERVED_12       = 0x0000000c,
IMG_NUM_FORMAT_ASTC_3D_RESERVED_13       = 0x0000000d,
IMG_NUM_FORMAT_ASTC_3D_RESERVED_14       = 0x0000000e,
IMG_NUM_FORMAT_ASTC_3D_RESERVED_15       = 0x0000000f,
} IMG_NUM_FORMAT_ASTC_3D;

/*
 * TileType enum
 */

typedef enum TileType {
ARRAY_COLOR_TILE                         = 0x00000000,
ARRAY_DEPTH_TILE                         = 0x00000001,
} TileType;

/*
 * NonDispTilingOrder enum
 */

typedef enum NonDispTilingOrder {
ADDR_SURF_MICRO_TILING_DISPLAY           = 0x00000000,
ADDR_SURF_MICRO_TILING_NON_DISPLAY       = 0x00000001,
} NonDispTilingOrder;

/*
 * MicroTileMode enum
 */

typedef enum MicroTileMode {
ADDR_SURF_DISPLAY_MICRO_TILING           = 0x00000000,
ADDR_SURF_THIN_MICRO_TILING              = 0x00000001,
ADDR_SURF_DEPTH_MICRO_TILING             = 0x00000002,
ADDR_SURF_ROTATED_MICRO_TILING           = 0x00000003,
ADDR_SURF_THICK_MICRO_TILING             = 0x00000004,
} MicroTileMode;

/*
 * TileSplit enum
 */

typedef enum TileSplit {
ADDR_SURF_TILE_SPLIT_64B                 = 0x00000000,
ADDR_SURF_TILE_SPLIT_128B                = 0x00000001,
ADDR_SURF_TILE_SPLIT_256B                = 0x00000002,
ADDR_SURF_TILE_SPLIT_512B                = 0x00000003,
ADDR_SURF_TILE_SPLIT_1KB                 = 0x00000004,
ADDR_SURF_TILE_SPLIT_2KB                 = 0x00000005,
ADDR_SURF_TILE_SPLIT_4KB                 = 0x00000006,
} TileSplit;

/*
 * SampleSplit enum
 */

typedef enum SampleSplit {
ADDR_SURF_SAMPLE_SPLIT_1                 = 0x00000000,
ADDR_SURF_SAMPLE_SPLIT_2                 = 0x00000001,
ADDR_SURF_SAMPLE_SPLIT_4                 = 0x00000002,
ADDR_SURF_SAMPLE_SPLIT_8                 = 0x00000003,
} SampleSplit;

/*
 * PipeConfig enum
 */

typedef enum PipeConfig {
ADDR_SURF_P2                             = 0x00000000,
ADDR_SURF_P2_RESERVED0                   = 0x00000001,
ADDR_SURF_P2_RESERVED1                   = 0x00000002,
ADDR_SURF_P2_RESERVED2                   = 0x00000003,
ADDR_SURF_P4_8x16                        = 0x00000004,
ADDR_SURF_P4_16x16                       = 0x00000005,
ADDR_SURF_P4_16x32                       = 0x00000006,
ADDR_SURF_P4_32x32                       = 0x00000007,
ADDR_SURF_P8_16x16_8x16                  = 0x00000008,
ADDR_SURF_P8_16x32_8x16                  = 0x00000009,
ADDR_SURF_P8_32x32_8x16                  = 0x0000000a,
ADDR_SURF_P8_16x32_16x16                 = 0x0000000b,
ADDR_SURF_P8_32x32_16x16                 = 0x0000000c,
ADDR_SURF_P8_32x32_16x32                 = 0x0000000d,
ADDR_SURF_P8_32x64_32x32                 = 0x0000000e,
ADDR_SURF_P8_RESERVED0                   = 0x0000000f,
ADDR_SURF_P16_32x32_8x16                 = 0x00000010,
ADDR_SURF_P16_32x32_16x16                = 0x00000011,
} PipeConfig;

/*
 * SeEnable enum
 */

typedef enum SeEnable {
ADDR_CONFIG_DISABLE_SE                   = 0x00000000,
ADDR_CONFIG_ENABLE_SE                    = 0x00000001,
} SeEnable;

/*
 * NumBanks enum
 */

typedef enum NumBanks {
ADDR_SURF_2_BANK                         = 0x00000000,
ADDR_SURF_4_BANK                         = 0x00000001,
ADDR_SURF_8_BANK                         = 0x00000002,
ADDR_SURF_16_BANK                        = 0x00000003,
} NumBanks;

/*
 * BankWidth enum
 */

typedef enum BankWidth {
ADDR_SURF_BANK_WIDTH_1                   = 0x00000000,
ADDR_SURF_BANK_WIDTH_2                   = 0x00000001,
ADDR_SURF_BANK_WIDTH_4                   = 0x00000002,
ADDR_SURF_BANK_WIDTH_8                   = 0x00000003,
} BankWidth;

/*
 * BankHeight enum
 */

typedef enum BankHeight {
ADDR_SURF_BANK_HEIGHT_1                  = 0x00000000,
ADDR_SURF_BANK_HEIGHT_2                  = 0x00000001,
ADDR_SURF_BANK_HEIGHT_4                  = 0x00000002,
ADDR_SURF_BANK_HEIGHT_8                  = 0x00000003,
} BankHeight;

/*
 * BankWidthHeight enum
 */

typedef enum BankWidthHeight {
ADDR_SURF_BANK_WH_1                      = 0x00000000,
ADDR_SURF_BANK_WH_2                      = 0x00000001,
ADDR_SURF_BANK_WH_4                      = 0x00000002,
ADDR_SURF_BANK_WH_8                      = 0x00000003,
} BankWidthHeight;

/*
 * MacroTileAspect enum
 */

typedef enum MacroTileAspect {
ADDR_SURF_MACRO_ASPECT_1                 = 0x00000000,
ADDR_SURF_MACRO_ASPECT_2                 = 0x00000001,
ADDR_SURF_MACRO_ASPECT_4                 = 0x00000002,
ADDR_SURF_MACRO_ASPECT_8                 = 0x00000003,
} MacroTileAspect;

/*
 * GATCL1RequestType enum
 */

typedef enum GATCL1RequestType {
GATCL1_TYPE_NORMAL                       = 0x00000000,
GATCL1_TYPE_SHOOTDOWN                    = 0x00000001,
GATCL1_TYPE_BYPASS                       = 0x00000002,
} GATCL1RequestType;

/*
 * UTCL1RequestType enum
 */

typedef enum UTCL1RequestType {
UTCL1_TYPE_NORMAL                        = 0x00000000,
UTCL1_TYPE_SHOOTDOWN                     = 0x00000001,
UTCL1_TYPE_BYPASS                        = 0x00000002,
} UTCL1RequestType;

/*
 * UTCL1FaultType enum
 */

typedef enum UTCL1FaultType {
UTCL1_XNACK_SUCCESS                      = 0x00000000,
UTCL1_XNACK_RETRY                        = 0x00000001,
UTCL1_XNACK_PRT                          = 0x00000002,
UTCL1_XNACK_NO_RETRY                     = 0x00000003,
} UTCL1FaultType;

/*
 * TCC_CACHE_POLICIES enum
 */

typedef enum TCC_CACHE_POLICIES {
TCC_CACHE_POLICY_LRU                     = 0x00000000,
TCC_CACHE_POLICY_STREAM                  = 0x00000001,
} TCC_CACHE_POLICIES;

/*
 * MTYPE enum
 */

typedef enum MTYPE {
MTYPE_NC                                 = 0x00000000,
MTYPE_WC                                 = 0x00000001,
MTYPE_RW                                 = 0x00000001,
MTYPE_CC                                 = 0x00000002,
MTYPE_UC                                 = 0x00000003,
} MTYPE;

/*
 * RMI_CID enum
 */

typedef enum RMI_CID {
RMI_CID_CC                               = 0x00000000,
RMI_CID_FC                               = 0x00000001,
RMI_CID_CM                               = 0x00000002,
RMI_CID_DC                               = 0x00000003,
RMI_CID_Z                                = 0x00000004,
RMI_CID_S                                = 0x00000005,
RMI_CID_TILE                             = 0x00000006,
RMI_CID_ZPCPSD                           = 0x00000007,
} RMI_CID;

/*
 * PERFMON_COUNTER_MODE enum
 */

typedef enum PERFMON_COUNTER_MODE {
PERFMON_COUNTER_MODE_ACCUM               = 0x00000000,
PERFMON_COUNTER_MODE_ACTIVE_CYCLES       = 0x00000001,
PERFMON_COUNTER_MODE_MAX                 = 0x00000002,
PERFMON_COUNTER_MODE_DIRTY               = 0x00000003,
PERFMON_COUNTER_MODE_SAMPLE              = 0x00000004,
PERFMON_COUNTER_MODE_CYCLES_SINCE_FIRST_EVENT  = 0x00000005,
PERFMON_COUNTER_MODE_CYCLES_SINCE_LAST_EVENT  = 0x00000006,
PERFMON_COUNTER_MODE_CYCLES_GE_HI        = 0x00000007,
PERFMON_COUNTER_MODE_CYCLES_EQ_HI        = 0x00000008,
PERFMON_COUNTER_MODE_INACTIVE_CYCLES     = 0x00000009,
PERFMON_COUNTER_MODE_RESERVED            = 0x0000000f,
} PERFMON_COUNTER_MODE;

/*
 * PERFMON_SPM_MODE enum
 */

typedef enum PERFMON_SPM_MODE {
PERFMON_SPM_MODE_OFF                     = 0x00000000,
PERFMON_SPM_MODE_16BIT_CLAMP             = 0x00000001,
PERFMON_SPM_MODE_16BIT_NO_CLAMP          = 0x00000002,
PERFMON_SPM_MODE_32BIT_CLAMP             = 0x00000003,
PERFMON_SPM_MODE_32BIT_NO_CLAMP          = 0x00000004,
PERFMON_SPM_MODE_RESERVED_5              = 0x00000005,
PERFMON_SPM_MODE_RESERVED_6              = 0x00000006,
PERFMON_SPM_MODE_RESERVED_7              = 0x00000007,
PERFMON_SPM_MODE_TEST_MODE_0             = 0x00000008,
PERFMON_SPM_MODE_TEST_MODE_1             = 0x00000009,
PERFMON_SPM_MODE_TEST_MODE_2             = 0x0000000a,
} PERFMON_SPM_MODE;

/*
 * SurfaceTiling enum
 */

typedef enum SurfaceTiling {
ARRAY_LINEAR                             = 0x00000000,
ARRAY_TILED                              = 0x00000001,
} SurfaceTiling;

/*
 * SurfaceArray enum
 */

typedef enum SurfaceArray {
ARRAY_1D                                 = 0x00000000,
ARRAY_2D                                 = 0x00000001,
ARRAY_3D                                 = 0x00000002,
ARRAY_3D_SLICE                           = 0x00000003,
} SurfaceArray;

/*
 * ColorArray enum
 */

typedef enum ColorArray {
ARRAY_2D_ALT_COLOR                       = 0x00000000,
ARRAY_2D_COLOR                           = 0x00000001,
ARRAY_3D_SLICE_COLOR                     = 0x00000003,
} ColorArray;

/*
 * DepthArray enum
 */

typedef enum DepthArray {
ARRAY_2D_ALT_DEPTH                       = 0x00000000,
ARRAY_2D_DEPTH                           = 0x00000001,
} DepthArray;

/*
 * ENUM_NUM_SIMD_PER_CU enum
 */

typedef enum ENUM_NUM_SIMD_PER_CU {
NUM_SIMD_PER_CU                          = 0x00000004,
} ENUM_NUM_SIMD_PER_CU;

/*
 * DSM_ENABLE_ERROR_INJECT enum
 */

typedef enum DSM_ENABLE_ERROR_INJECT {
DSM_ENABLE_ERROR_INJECT_FED_IN           = 0x00000000,
DSM_ENABLE_ERROR_INJECT_SINGLE           = 0x00000001,
DSM_ENABLE_ERROR_INJECT_DOUBLE           = 0x00000002,
DSM_ENABLE_ERROR_INJECT_DOUBLE_LIMITED   = 0x00000003,
} DSM_ENABLE_ERROR_INJECT;

/*
 * DSM_SELECT_INJECT_DELAY enum
 */

typedef enum DSM_SELECT_INJECT_DELAY {
DSM_SELECT_INJECT_DELAY_NO_DELAY         = 0x00000000,
DSM_SELECT_INJECT_DELAY_DELAY_ERROR      = 0x00000001,
} DSM_SELECT_INJECT_DELAY;

/*
 * SWIZZLE_TYPE_ENUM enum
 */

typedef enum SWIZZLE_TYPE_ENUM {
SW_Z                                     = 0x00000000,
SW_S                                     = 0x00000001,
SW_D                                     = 0x00000002,
SW_R                                     = 0x00000003,
SW_L                                     = 0x00000004,
} SWIZZLE_TYPE_ENUM;

/*
 * TC_MICRO_TILE_MODE enum
 */

typedef enum TC_MICRO_TILE_MODE {
MICRO_TILE_MODE_LINEAR                   = 0x00000000,
MICRO_TILE_MODE_ROTATED                  = 0x00000001,
MICRO_TILE_MODE_STD_2D                   = 0x00000002,
MICRO_TILE_MODE_STD_3D                   = 0x00000003,
MICRO_TILE_MODE_DISPLAY_2D               = 0x00000004,
MICRO_TILE_MODE_DISPLAY_3D               = 0x00000005,
MICRO_TILE_MODE_Z_2D                     = 0x00000006,
MICRO_TILE_MODE_Z_3D                     = 0x00000007,
} TC_MICRO_TILE_MODE;

/*
 * SWIZZLE_MODE_ENUM enum
 */

typedef enum SWIZZLE_MODE_ENUM {
SW_LINEAR                                = 0x00000000,
SW_256B_S                                = 0x00000001,
SW_256B_D                                = 0x00000002,
SW_256B_R                                = 0x00000003,
SW_4KB_Z                                 = 0x00000004,
SW_4KB_S                                 = 0x00000005,
SW_4KB_D                                 = 0x00000006,
SW_4KB_R                                 = 0x00000007,
SW_64KB_Z                                = 0x00000008,
SW_64KB_S                                = 0x00000009,
SW_64KB_D                                = 0x0000000a,
SW_64KB_R                                = 0x0000000b,
SW_VAR_Z                                 = 0x0000000c,
SW_VAR_S                                 = 0x0000000d,
SW_VAR_D                                 = 0x0000000e,
SW_VAR_R                                 = 0x0000000f,
SW_RESERVED_16                           = 0x00000010,
SW_RESERVED_17                           = 0x00000011,
SW_RESERVED_18                           = 0x00000012,
SW_RESERVED_19                           = 0x00000013,
SW_4KB_Z_X                               = 0x00000014,
SW_4KB_S_X                               = 0x00000015,
SW_4KB_D_X                               = 0x00000016,
SW_4KB_R_X                               = 0x00000017,
SW_64KB_Z_X                              = 0x00000018,
SW_64KB_S_X                              = 0x00000019,
SW_64KB_D_X                              = 0x0000001a,
SW_64KB_R_X                              = 0x0000001b,
SW_VAR_Z_X                               = 0x0000001c,
SW_VAR_S_X                               = 0x0000001d,
SW_VAR_D_X                               = 0x0000001e,
SW_VAR_R_X                               = 0x0000001f,
SW_RESERVED_12                           = 0x00000020,
SW_RESERVED_13                           = 0x00000021,
SW_RESERVED_14                           = 0x00000022,
SW_RESERVED_15                           = 0x00000023,
} SWIZZLE_MODE_ENUM;

/*
 * PipeTiling enum
 */

typedef enum PipeTiling {
CONFIG_1_PIPE                            = 0x00000000,
CONFIG_2_PIPE                            = 0x00000001,
CONFIG_4_PIPE                            = 0x00000002,
CONFIG_8_PIPE                            = 0x00000003,
} PipeTiling;

/*
 * BankTiling enum
 */

typedef enum BankTiling {
CONFIG_4_BANK                            = 0x00000000,
CONFIG_8_BANK                            = 0x00000001,
} BankTiling;

/*
 * GroupInterleave enum
 */

typedef enum GroupInterleave {
CONFIG_256B_GROUP                        = 0x00000000,
CONFIG_512B_GROUP                        = 0x00000001,
} GroupInterleave;

/*
 * RowTiling enum
 */

typedef enum RowTiling {
CONFIG_1KB_ROW                           = 0x00000000,
CONFIG_2KB_ROW                           = 0x00000001,
CONFIG_4KB_ROW                           = 0x00000002,
CONFIG_8KB_ROW                           = 0x00000003,
CONFIG_1KB_ROW_OPT                       = 0x00000004,
CONFIG_2KB_ROW_OPT                       = 0x00000005,
CONFIG_4KB_ROW_OPT                       = 0x00000006,
CONFIG_8KB_ROW_OPT                       = 0x00000007,
} RowTiling;

/*
 * BankSwapBytes enum
 */

typedef enum BankSwapBytes {
CONFIG_128B_SWAPS                        = 0x00000000,
CONFIG_256B_SWAPS                        = 0x00000001,
CONFIG_512B_SWAPS                        = 0x00000002,
CONFIG_1KB_SWAPS                         = 0x00000003,
} BankSwapBytes;

/*
 * SampleSplitBytes enum
 */

typedef enum SampleSplitBytes {
CONFIG_1KB_SPLIT                         = 0x00000000,
CONFIG_2KB_SPLIT                         = 0x00000001,
CONFIG_4KB_SPLIT                         = 0x00000002,
CONFIG_8KB_SPLIT                         = 0x00000003,
} SampleSplitBytes;

/*******************************************************
 * AZSTREAM Enums
 *******************************************************/

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_STATUS_NOT_SET  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_STATUS_SET  = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_STATUS_NOT_SET  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_STATUS_SET  = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_BUFFER_COMPLETION_INTERRUPT_STATUS enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_BUFFER_COMPLETION_INTERRUPT_STATUS {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_BUFFER_COMPLETION_INTERRUPT_STATUS_NOT_SET  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_BUFFER_COMPLETION_INTERRUPT_STATUS_SET  = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_BUFFER_COMPLETION_INTERRUPT_STATUS;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_TRAFFIC_PRIORITY enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_TRAFFIC_PRIORITY {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_NO_TRAFFIC_PRIORITY  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_YES_TRAFFIC_PRIORITY  = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_TRAFFIC_PRIORITY;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_INTERRUPT_ENABLE enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_INTERRUPT_ENABLE {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_INTERRUPT_DISABLED  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_INTERRUPT_ENABLED  = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_INTERRUPT_ENABLE;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_INTERRUPT_ENABLE enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_INTERRUPT_ENABLE {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_INTERRUPT_DISABLED  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_INTERRUPT_ENABLED  = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_INTERRUPT_ENABLE;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_INTERRUPT_ON_COMPLETION_ENABLE enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_INTERRUPT_ON_COMPLETION_ENABLE {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_INTERRUPT_ON_COMPLETION_ENABLE_INTERRUPT_DISABLED  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_INTERRUPT_ON_COMPLETION_ENABLE_INTERRUPT_ENABLED  = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_INTERRUPT_ON_COMPLETION_ENABLE;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_RUN enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_RUN {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_NOT_RUN  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_DO_RUN  = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_RUN;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_RESET enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_RESET {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_NOT_RESET  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_IS_RESET  = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_RESET;

/*
 * OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_RATE enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_RATE {
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_RATE_48KHZ  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_RATE_44P1KHZ  = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_RATE;

/*
 * OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE {
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE_BY1  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE_BY2  = 0x00000001,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE_BY3_RESERVED  = 0x00000002,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE_BY4  = 0x00000003,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE_RESERVED  = 0x00000004,
} OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE;

/*
 * OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR {
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY1  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY2_RESERVED  = 0x00000001,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY3  = 0x00000002,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY4_RESERVED  = 0x00000003,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY5_RESERVED  = 0x00000004,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY6_RESERVED  = 0x00000005,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY7_RESERVED  = 0x00000006,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY8_RESERVED  = 0x00000007,
} OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR;

/*
 * OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE {
OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE_8_RESERVED  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE_16  = 0x00000001,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE_20  = 0x00000002,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE_24  = 0x00000003,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE_32_RESERVED  = 0x00000004,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE_RESERVED  = 0x00000005,
} OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE;

/*
 * OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS {
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_1  = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_2  = 0x00000001,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_3  = 0x00000002,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_4  = 0x00000003,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_5  = 0x00000004,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_6  = 0x00000005,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_7  = 0x00000006,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_8  = 0x00000007,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_9_RESERVED  = 0x00000008,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_10_RESERVED  = 0x00000009,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_11_RESERVED  = 0x0000000a,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_12_RESERVED  = 0x0000000b,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_13_RESERVED  = 0x0000000c,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_14_RESERVED  = 0x0000000d,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_15_RESERVED  = 0x0000000e,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_16_RESERVED  = 0x0000000f,
} OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS;

/*******************************************************
 * BLNDV Enums
 *******************************************************/

/*
 * BLNDV_CONTROL_BLND_MODE enum
 */

typedef enum BLNDV_CONTROL_BLND_MODE {
BLNDV_CONTROL_BLND_MODE_CURRENT_PIPE_ONLY = 0x00000000,
BLNDV_CONTROL_BLND_MODE_OTHER_PIPE_ONLY  = 0x00000001,
BLNDV_CONTROL_BLND_MODE_ALPHA_BLENDING_MODE = 0x00000002,
BLNDV_CONTROL_BLND_MODE_OTHER_STEREO_TYPE = 0x00000003,
} BLNDV_CONTROL_BLND_MODE;

/*
 * BLNDV_CONTROL_BLND_STEREO_TYPE enum
 */

typedef enum BLNDV_CONTROL_BLND_STEREO_TYPE {
BLNDV_CONTROL_BLND_STEREO_TYPE_NON_SINGLE_PIPE_STEREO = 0x00000000,
BLNDV_CONTROL_BLND_STEREO_TYPE_SIDE_BY_SIDE_SINGLE_PIPE_STEREO = 0x00000001,
BLNDV_CONTROL_BLND_STEREO_TYPE_TOP_BOTTOM_SINGLE_PIPE_STEREO = 0x00000002,
BLNDV_CONTROL_BLND_STEREO_TYPE_UNUSED    = 0x00000003,
} BLNDV_CONTROL_BLND_STEREO_TYPE;

/*
 * BLNDV_CONTROL_BLND_STEREO_POLARITY enum
 */

typedef enum BLNDV_CONTROL_BLND_STEREO_POLARITY {
BLNDV_CONTROL_BLND_STEREO_POLARITY_LOW   = 0x00000000,
BLNDV_CONTROL_BLND_STEREO_POLARITY_HIGH  = 0x00000001,
} BLNDV_CONTROL_BLND_STEREO_POLARITY;

/*
 * BLNDV_CONTROL_BLND_FEEDTHROUGH_EN enum
 */

typedef enum BLNDV_CONTROL_BLND_FEEDTHROUGH_EN {
BLNDV_CONTROL_BLND_FEEDTHROUGH_EN_FALSE  = 0x00000000,
BLNDV_CONTROL_BLND_FEEDTHROUGH_EN_TRUE   = 0x00000001,
} BLNDV_CONTROL_BLND_FEEDTHROUGH_EN;

/*
 * BLNDV_CONTROL_BLND_ALPHA_MODE enum
 */

typedef enum BLNDV_CONTROL_BLND_ALPHA_MODE {
BLNDV_CONTROL_BLND_ALPHA_MODE_CURRENT_PIXEL_ALPHA = 0x00000000,
BLNDV_CONTROL_BLND_ALPHA_MODE_PIXEL_ALPHA_COMBINED_GLOBAL_GAIN = 0x00000001,
BLNDV_CONTROL_BLND_ALPHA_MODE_GLOBAL_ALPHA_ONLY = 0x00000002,
BLNDV_CONTROL_BLND_ALPHA_MODE_UNUSED     = 0x00000003,
} BLNDV_CONTROL_BLND_ALPHA_MODE;

/*
 * BLNDV_CONTROL_BLND_ACTIVE_OVERLAP_ONLY enum
 */

typedef enum BLNDV_CONTROL_BLND_ACTIVE_OVERLAP_ONLY {
BLNDV_CONTROL_BLND_ACTIVE_OVERLAP_ONLY_FALSE  = 0x00000000,
BLNDV_CONTROL_BLND_ACTIVE_OVERLAP_ONLY_TRUE  = 0x00000001,
} BLNDV_CONTROL_BLND_ACTIVE_OVERLAP_ONLY;

/*
 * BLNDV_CONTROL_BLND_MULTIPLIED_MODE enum
 */

typedef enum BLNDV_CONTROL_BLND_MULTIPLIED_MODE {
BLNDV_CONTROL_BLND_MULTIPLIED_MODE_FALSE = 0x00000000,
BLNDV_CONTROL_BLND_MULTIPLIED_MODE_TRUE  = 0x00000001,
} BLNDV_CONTROL_BLND_MULTIPLIED_MODE;

/*
 * BLNDV_SM_CONTROL2_SM_MODE enum
 */

typedef enum BLNDV_SM_CONTROL2_SM_MODE {
BLNDV_SM_CONTROL2_SM_MODE_SINGLE_PLANE   = 0x00000000,
BLNDV_SM_CONTROL2_SM_MODE_ROW_SUBSAMPLING = 0x00000002,
BLNDV_SM_CONTROL2_SM_MODE_COLUMN_SUBSAMPLING = 0x00000004,
BLNDV_SM_CONTROL2_SM_MODE_CHECKERBOARD_SUBSAMPLING = 0x00000006,
} BLNDV_SM_CONTROL2_SM_MODE;

/*
 * BLNDV_SM_CONTROL2_SM_FRAME_ALTERNATE enum
 */

typedef enum BLNDV_SM_CONTROL2_SM_FRAME_ALTERNATE {
BLNDV_SM_CONTROL2_SM_FRAME_ALTERNATE_FALSE = 0x00000000,
BLNDV_SM_CONTROL2_SM_FRAME_ALTERNATE_TRUE = 0x00000001,
} BLNDV_SM_CONTROL2_SM_FRAME_ALTERNATE;

/*
 * BLNDV_SM_CONTROL2_SM_FIELD_ALTERNATE enum
 */

typedef enum BLNDV_SM_CONTROL2_SM_FIELD_ALTERNATE {
BLNDV_SM_CONTROL2_SM_FIELD_ALTERNATE_FALSE = 0x00000000,
BLNDV_SM_CONTROL2_SM_FIELD_ALTERNATE_TRUE = 0x00000001,
} BLNDV_SM_CONTROL2_SM_FIELD_ALTERNATE;

/*
 * BLNDV_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL enum
 */

typedef enum BLNDV_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL {
BLNDV_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL_NO_FORCE = 0x00000000,
BLNDV_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL_RESERVED = 0x00000001,
BLNDV_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL_FORCE_LOW = 0x00000002,
BLNDV_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL_FORCE_HIGH = 0x00000003,
} BLNDV_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL;

/*
 * BLNDV_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL enum
 */

typedef enum BLNDV_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL {
BLNDV_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL_NO_FORCE = 0x00000000,
BLNDV_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL_RESERVED = 0x00000001,
BLNDV_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL_FORCE_LOW = 0x00000002,
BLNDV_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL_FORCE_HIGH = 0x00000003,
} BLNDV_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL;

/*
 * BLNDV_CONTROL2_PTI_ENABLE enum
 */

typedef enum BLNDV_CONTROL2_PTI_ENABLE {
BLNDV_CONTROL2_PTI_ENABLE_FALSE          = 0x00000000,
BLNDV_CONTROL2_PTI_ENABLE_TRUE           = 0x00000001,
} BLNDV_CONTROL2_PTI_ENABLE;

/*
 * BLNDV_CONTROL2_BLND_SUPERAA_DEGAMMA_EN enum
 */

typedef enum BLNDV_CONTROL2_BLND_SUPERAA_DEGAMMA_EN {
BLNDV_CONTROL2_BLND_SUPERAA_DEGAMMA_EN_FALSE = 0x00000000,
BLNDV_CONTROL2_BLND_SUPERAA_DEGAMMA_EN_TRUE = 0x00000001,
} BLNDV_CONTROL2_BLND_SUPERAA_DEGAMMA_EN;

/*
 * BLNDV_CONTROL2_BLND_SUPERAA_REGAMMA_EN enum
 */

typedef enum BLNDV_CONTROL2_BLND_SUPERAA_REGAMMA_EN {
BLNDV_CONTROL2_BLND_SUPERAA_REGAMMA_EN_FALSE = 0x00000000,
BLNDV_CONTROL2_BLND_SUPERAA_REGAMMA_EN_TRUE = 0x00000001,
} BLNDV_CONTROL2_BLND_SUPERAA_REGAMMA_EN;

/*
 * BLNDV_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_ACK enum
 */

typedef enum BLNDV_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_ACK {
BLNDV_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_ACK_FALSE = 0x00000000,
BLNDV_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_ACK_TRUE = 0x00000001,
} BLNDV_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_ACK;

/*
 * BLNDV_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_MASK enum
 */

typedef enum BLNDV_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_MASK {
BLNDV_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_MASK_FALSE = 0x00000000,
BLNDV_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_MASK_TRUE = 0x00000001,
} BLNDV_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_MASK;

/*
 * BLNDV_V_UPDATE_LOCK_BLND_DCP_GRPH_V_UPDATE_LOCK enum
 */

typedef enum BLNDV_V_UPDATE_LOCK_BLND_DCP_GRPH_V_UPDATE_LOCK {
BLNDV_V_UPDATE_LOCK_BLND_DCP_GRPH_V_UPDATE_LOCK_FALSE = 0x00000000,
BLNDV_V_UPDATE_LOCK_BLND_DCP_GRPH_V_UPDATE_LOCK_TRUE = 0x00000001,
} BLNDV_V_UPDATE_LOCK_BLND_DCP_GRPH_V_UPDATE_LOCK;

/*
 * BLNDV_V_UPDATE_LOCK_BLND_DCP_GRPH_SURF_V_UPDATE_LOCK enum
 */

typedef enum BLNDV_V_UPDATE_LOCK_BLND_DCP_GRPH_SURF_V_UPDATE_LOCK {
BLNDV_V_UPDATE_LOCK_BLND_DCP_GRPH_SURF_V_UPDATE_LOCK_FALSE = 0x00000000,
BLNDV_V_UPDATE_LOCK_BLND_DCP_GRPH_SURF_V_UPDATE_LOCK_TRUE = 0x00000001,
} BLNDV_V_UPDATE_LOCK_BLND_DCP_GRPH_SURF_V_UPDATE_LOCK;

/*
 * BLNDV_V_UPDATE_LOCK_BLND_DCP_CUR_V_UPDATE_LOCK enum
 */

typedef enum BLNDV_V_UPDATE_LOCK_BLND_DCP_CUR_V_UPDATE_LOCK {
BLNDV_V_UPDATE_LOCK_BLND_DCP_CUR_V_UPDATE_LOCK_FALSE = 0x00000000,
BLNDV_V_UPDATE_LOCK_BLND_DCP_CUR_V_UPDATE_LOCK_TRUE = 0x00000001,
} BLNDV_V_UPDATE_LOCK_BLND_DCP_CUR_V_UPDATE_LOCK;

/*
 * BLNDV_V_UPDATE_LOCK_BLND_DCP_CUR2_V_UPDATE_LOCK enum
 */

typedef enum BLNDV_V_UPDATE_LOCK_BLND_DCP_CUR2_V_UPDATE_LOCK {
BLNDV_V_UPDATE_LOCK_BLND_DCP_CUR2_V_UPDATE_LOCK_FALSE = 0x00000000,
BLNDV_V_UPDATE_LOCK_BLND_DCP_CUR2_V_UPDATE_LOCK_TRUE = 0x00000001,
} BLNDV_V_UPDATE_LOCK_BLND_DCP_CUR2_V_UPDATE_LOCK;

/*
 * BLNDV_V_UPDATE_LOCK_BLND_SCL_V_UPDATE_LOCK enum
 */

typedef enum BLNDV_V_UPDATE_LOCK_BLND_SCL_V_UPDATE_LOCK {
BLNDV_V_UPDATE_LOCK_BLND_SCL_V_UPDATE_LOCK_FALSE = 0x00000000,
BLNDV_V_UPDATE_LOCK_BLND_SCL_V_UPDATE_LOCK_TRUE = 0x00000001,
} BLNDV_V_UPDATE_LOCK_BLND_SCL_V_UPDATE_LOCK;

/*
 * BLNDV_V_UPDATE_LOCK_BLND_BLND_V_UPDATE_LOCK enum
 */

typedef enum BLNDV_V_UPDATE_LOCK_BLND_BLND_V_UPDATE_LOCK {
BLNDV_V_UPDATE_LOCK_BLND_BLND_V_UPDATE_LOCK_FALSE = 0x00000000,
BLNDV_V_UPDATE_LOCK_BLND_BLND_V_UPDATE_LOCK_TRUE = 0x00000001,
} BLNDV_V_UPDATE_LOCK_BLND_BLND_V_UPDATE_LOCK;

/*
 * BLNDV_V_UPDATE_LOCK_BLND_V_UPDATE_LOCK_MODE enum
 */

typedef enum BLNDV_V_UPDATE_LOCK_BLND_V_UPDATE_LOCK_MODE {
BLNDV_V_UPDATE_LOCK_BLND_V_UPDATE_LOCK_MODE_FALSE = 0x00000000,
BLNDV_V_UPDATE_LOCK_BLND_V_UPDATE_LOCK_MODE_TRUE = 0x00000001,
} BLNDV_V_UPDATE_LOCK_BLND_V_UPDATE_LOCK_MODE;

/*
 * BLNDV_DEBUG_BLND_CNV_MUX_SELECT enum
 */

typedef enum BLNDV_DEBUG_BLND_CNV_MUX_SELECT {
BLNDV_DEBUG_BLND_CNV_MUX_SELECT_LOW      = 0x00000000,
BLNDV_DEBUG_BLND_CNV_MUX_SELECT_HIGH     = 0x00000001,
} BLNDV_DEBUG_BLND_CNV_MUX_SELECT;

/*
 * BLNDV_TEST_DEBUG_INDEX_BLND_TEST_DEBUG_WRITE_EN enum
 */

typedef enum BLNDV_TEST_DEBUG_INDEX_BLND_TEST_DEBUG_WRITE_EN {
BLNDV_TEST_DEBUG_INDEX_BLND_TEST_DEBUG_WRITE_EN_FALSE = 0x00000000,
BLNDV_TEST_DEBUG_INDEX_BLND_TEST_DEBUG_WRITE_EN_TRUE = 0x00000001,
} BLNDV_TEST_DEBUG_INDEX_BLND_TEST_DEBUG_WRITE_EN;

/*******************************************************
 * LBV Enums
 *******************************************************/

/*
 * LBV_PIXEL_DEPTH enum
 */

typedef enum LBV_PIXEL_DEPTH {
PIXEL_DEPTH_30BPP                        = 0x00000000,
PIXEL_DEPTH_24BPP                        = 0x00000001,
PIXEL_DEPTH_18BPP                        = 0x00000002,
PIXEL_DEPTH_38BPP                        = 0x00000003,
} LBV_PIXEL_DEPTH;

/*
 * LBV_PIXEL_EXPAN_MODE enum
 */

typedef enum LBV_PIXEL_EXPAN_MODE {
PIXEL_EXPAN_MODE_ZERO_EXP                = 0x00000000,
PIXEL_EXPAN_MODE_DYN_EXP                 = 0x00000001,
} LBV_PIXEL_EXPAN_MODE;

/*
 * LBV_INTERLEAVE_EN enum
 */

typedef enum LBV_INTERLEAVE_EN {
INTERLEAVE_DIS                           = 0x00000000,
INTERLEAVE_EN                            = 0x00000001,
} LBV_INTERLEAVE_EN;

/*
 * LBV_PIXEL_REDUCE_MODE enum
 */

typedef enum LBV_PIXEL_REDUCE_MODE {
PIXEL_REDUCE_MODE_TRUNCATION             = 0x00000000,
PIXEL_REDUCE_MODE_ROUNDING               = 0x00000001,
} LBV_PIXEL_REDUCE_MODE;

/*
 * LBV_DYNAMIC_PIXEL_DEPTH enum
 */

typedef enum LBV_DYNAMIC_PIXEL_DEPTH {
DYNAMIC_PIXEL_DEPTH_36BPP                = 0x00000000,
DYNAMIC_PIXEL_DEPTH_30BPP                = 0x00000001,
} LBV_DYNAMIC_PIXEL_DEPTH;

/*
 * LBV_DITHER_EN enum
 */

typedef enum LBV_DITHER_EN {
DITHER_DIS                               = 0x00000000,
DITHER_EN                                = 0x00000001,
} LBV_DITHER_EN;

/*
 * LBV_DOWNSCALE_PREFETCH_EN enum
 */

typedef enum LBV_DOWNSCALE_PREFETCH_EN {
DOWNSCALE_PREFETCH_DIS                   = 0x00000000,
DOWNSCALE_PREFETCH_EN                    = 0x00000001,
} LBV_DOWNSCALE_PREFETCH_EN;

/*
 * LBV_MEMORY_CONFIG enum
 */

typedef enum LBV_MEMORY_CONFIG {
MEMORY_CONFIG_0                          = 0x00000000,
MEMORY_CONFIG_1                          = 0x00000001,
MEMORY_CONFIG_2                          = 0x00000002,
MEMORY_CONFIG_3                          = 0x00000003,
} LBV_MEMORY_CONFIG;

/*
 * LBV_SYNC_RESET_SEL2 enum
 */

typedef enum LBV_SYNC_RESET_SEL2 {
SYNC_RESET_SEL2_VBLANK                   = 0x00000000,
SYNC_RESET_SEL2_VSYNC                    = 0x00000001,
} LBV_SYNC_RESET_SEL2;

/*
 * LBV_SYNC_DURATION enum
 */

typedef enum LBV_SYNC_DURATION {
SYNC_DURATION_16                         = 0x00000000,
SYNC_DURATION_32                         = 0x00000001,
SYNC_DURATION_64                         = 0x00000002,
SYNC_DURATION_128                        = 0x00000003,
} LBV_SYNC_DURATION;

/*******************************************************
 * CRTC Enums
 *******************************************************/

/*
 * CRTC_CONTROL_CRTC_START_POINT_CNTL enum
 */

typedef enum CRTC_CONTROL_CRTC_START_POINT_CNTL {
CRTC_CONTROL_CRTC_START_POINT_CNTL_NORMAL = 0x00000000,
CRTC_CONTROL_CRTC_START_POINT_CNTL_DP    = 0x00000001,
} CRTC_CONTROL_CRTC_START_POINT_CNTL;

/*
 * CRTC_CONTROL_CRTC_FIELD_NUMBER_CNTL enum
 */

typedef enum CRTC_CONTROL_CRTC_FIELD_NUMBER_CNTL {
CRTC_CONTROL_CRTC_FIELD_NUMBER_CNTL_NORMAL = 0x00000000,
CRTC_CONTROL_CRTC_FIELD_NUMBER_CNTL_DP   = 0x00000001,
} CRTC_CONTROL_CRTC_FIELD_NUMBER_CNTL;

/*
 * CRTC_CONTROL_CRTC_DISABLE_POINT_CNTL enum
 */

typedef enum CRTC_CONTROL_CRTC_DISABLE_POINT_CNTL {
CRTC_CONTROL_CRTC_DISABLE_POINT_CNTL_DISABLE  = 0x00000000,
CRTC_CONTROL_CRTC_DISABLE_POINT_CNTL_DISABLE_CURRENT  = 0x00000001,
CRTC_CONTROL_CRTC_DISABLE_POINT_CNTL_RESERVED  = 0x00000002,
CRTC_CONTROL_CRTC_DISABLE_POINT_CNTL_DISABLE_FIRST  = 0x00000003,
} CRTC_CONTROL_CRTC_DISABLE_POINT_CNTL;

/*
 * CRTC_CONTROL_CRTC_FIELD_NUMBER_POLARITY enum
 */

typedef enum CRTC_CONTROL_CRTC_FIELD_NUMBER_POLARITY {
CRTC_CONTROL_CRTC_FIELD_NUMBER_POLARITY_FALSE  = 0x00000000,
CRTC_CONTROL_CRTC_FIELD_NUMBER_POLARITY_TRUE  = 0x00000001,
} CRTC_CONTROL_CRTC_FIELD_NUMBER_POLARITY;

/*
 * CRTC_CONTROL_CRTC_DISP_READ_REQUEST_DISABLE enum
 */

typedef enum CRTC_CONTROL_CRTC_DISP_READ_REQUEST_DISABLE {
CRTC_CONTROL_CRTC_DISP_READ_REQUEST_DISABLE_FALSE  = 0x00000000,
CRTC_CONTROL_CRTC_DISP_READ_REQUEST_DISABLE_TRUE  = 0x00000001,
} CRTC_CONTROL_CRTC_DISP_READ_REQUEST_DISABLE;

/*
 * CRTC_CONTROL_CRTC_SOF_PULL_EN enum
 */

typedef enum CRTC_CONTROL_CRTC_SOF_PULL_EN {
CRTC_CONTROL_CRTC_SOF_PULL_EN_FALSE      = 0x00000000,
CRTC_CONTROL_CRTC_SOF_PULL_EN_TRUE       = 0x00000001,
} CRTC_CONTROL_CRTC_SOF_PULL_EN;

/*
 * CRTC_H_SYNC_B_CNTL_CRTC_H_SYNC_B_POL enum
 */

typedef enum CRTC_H_SYNC_B_CNTL_CRTC_H_SYNC_B_POL {
CRTC_H_SYNC_B_CNTL_CRTC_H_SYNC_B_POL_FALSE  = 0x00000000,
CRTC_H_SYNC_B_CNTL_CRTC_H_SYNC_B_POL_TRUE  = 0x00000001,
} CRTC_H_SYNC_B_CNTL_CRTC_H_SYNC_B_POL;

/*
 * CRTC_V_TOTAL_CONTROL_CRTC_V_TOTAL_MAX_SEL enum
 */

typedef enum CRTC_V_TOTAL_CONTROL_CRTC_V_TOTAL_MAX_SEL {
CRTC_V_TOTAL_CONTROL_CRTC_V_TOTAL_MAX_SEL_FALSE  = 0x00000000,
CRTC_V_TOTAL_CONTROL_CRTC_V_TOTAL_MAX_SEL_TRUE  = 0x00000001,
} CRTC_V_TOTAL_CONTROL_CRTC_V_TOTAL_MAX_SEL;

/*
 * CRTC_V_TOTAL_CONTROL_CRTC_V_TOTAL_MIN_SEL enum
 */

typedef enum CRTC_V_TOTAL_CONTROL_CRTC_V_TOTAL_MIN_SEL {
CRTC_V_TOTAL_CONTROL_CRTC_V_TOTAL_MIN_SEL_FALSE  = 0x00000000,
CRTC_V_TOTAL_CONTROL_CRTC_V_TOTAL_MIN_SEL_TRUE  = 0x00000001,
} CRTC_V_TOTAL_CONTROL_CRTC_V_TOTAL_MIN_SEL;

/*
 * CRTC_V_TOTAL_CONTROL_CRTC_SET_V_TOTAL_MIN_MASK_EN enum
 */

typedef enum CRTC_V_TOTAL_CONTROL_CRTC_SET_V_TOTAL_MIN_MASK_EN {
CRTC_V_TOTAL_CONTROL_CRTC_SET_V_TOTAL_MIN_MASK_EN_FALSE  = 0x00000000,
CRTC_V_TOTAL_CONTROL_CRTC_SET_V_TOTAL_MIN_MASK_EN_TRUE  = 0x00000001,
} CRTC_V_TOTAL_CONTROL_CRTC_SET_V_TOTAL_MIN_MASK_EN;

/*
 * CRTC_V_TOTAL_CONTROL_CRTC_FORCE_LOCK_TO_MASTER_VSYNC enum
 */

typedef enum CRTC_V_TOTAL_CONTROL_CRTC_FORCE_LOCK_TO_MASTER_VSYNC {
CRTC_V_TOTAL_CONTROL_CRTC_FORCE_LOCK_TO_MASTER_VSYNC_DISABLE = 0x00000000,
CRTC_V_TOTAL_CONTROL_CRTC_FORCE_LOCK_TO_MASTER_VSYNC_ENABLE  = 0x00000001,
} CRTC_V_TOTAL_CONTROL_CRTC_FORCE_LOCK_TO_MASTER_VSYNC;

/*
 * CRTC_V_TOTAL_CONTROL_CRTC_FORCE_LOCK_ON_EVENT enum
 */

typedef enum CRTC_V_TOTAL_CONTROL_CRTC_FORCE_LOCK_ON_EVENT {
CRTC_V_TOTAL_CONTROL_CRTC_FORCE_LOCK_ON_EVENT_DISABLE = 0x00000000,
CRTC_V_TOTAL_CONTROL_CRTC_FORCE_LOCK_ON_EVENT_ENABLE  = 0x00000001,
} CRTC_V_TOTAL_CONTROL_CRTC_FORCE_LOCK_ON_EVENT;

/*
 * CRTC_V_TOTAL_INT_STATUS_CRTC_SET_V_TOTAL_MIN_EVENT_OCCURED_ACK enum
 */

typedef enum CRTC_V_TOTAL_INT_STATUS_CRTC_SET_V_TOTAL_MIN_EVENT_OCCURED_ACK {
CRTC_V_TOTAL_INT_STATUS_CRTC_SET_V_TOTAL_MIN_EVENT_OCCURED_ACK_FALSE = 0x00000000,
CRTC_V_TOTAL_INT_STATUS_CRTC_SET_V_TOTAL_MIN_EVENT_OCCURED_ACK_TRUE  = 0x00000001,
} CRTC_V_TOTAL_INT_STATUS_CRTC_SET_V_TOTAL_MIN_EVENT_OCCURED_ACK;

/*
 * CRTC_VSYNC_NOM_INT_STATUS_CRTC_VSYNC_NOM_INT_CLEAR enum
 */

typedef enum CRTC_VSYNC_NOM_INT_STATUS_CRTC_VSYNC_NOM_INT_CLEAR {
CRTC_VSYNC_NOM_INT_STATUS_CRTC_VSYNC_NOM_INT_CLEAR_FALSE = 0x00000000,
CRTC_VSYNC_NOM_INT_STATUS_CRTC_VSYNC_NOM_INT_CLEAR_TRUE  = 0x00000001,
} CRTC_VSYNC_NOM_INT_STATUS_CRTC_VSYNC_NOM_INT_CLEAR;

/*
 * CRTC_V_SYNC_B_CNTL_CRTC_V_SYNC_B_POL enum
 */

typedef enum CRTC_V_SYNC_B_CNTL_CRTC_V_SYNC_B_POL {
CRTC_V_SYNC_B_CNTL_CRTC_V_SYNC_B_POL_FALSE  = 0x00000000,
CRTC_V_SYNC_B_CNTL_CRTC_V_SYNC_B_POL_TRUE  = 0x00000001,
} CRTC_V_SYNC_B_CNTL_CRTC_V_SYNC_B_POL;

/*
 * CRTC_DTMTEST_CNTL_CRTC_DTMTEST_CRTC_EN enum
 */

typedef enum CRTC_DTMTEST_CNTL_CRTC_DTMTEST_CRTC_EN {
CRTC_DTMTEST_CNTL_CRTC_DTMTEST_CRTC_EN_FALSE  = 0x00000000,
CRTC_DTMTEST_CNTL_CRTC_DTMTEST_CRTC_EN_TRUE  = 0x00000001,
} CRTC_DTMTEST_CNTL_CRTC_DTMTEST_CRTC_EN;

/*
 * CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT enum
 */

typedef enum CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT {
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_VSYNCA_OTHER  = 0x00000001,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_HSYNCA_OTHER  = 0x00000002,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_GENERICF  = 0x00000005,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_GENERICE  = 0x00000006,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_VSYNCA  = 0x00000007,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_HSYNCA  = 0x00000008,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_VSYNCB  = 0x00000009,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_HSYNCB  = 0x0000000a,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_HPD1  = 0x0000000b,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_HPD2  = 0x0000000c,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_GENERICD  = 0x0000000d,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_GENERICC  = 0x0000000e,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_IGSL0  = 0x00000010,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_IGSL1  = 0x00000011,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_IGSL2  = 0x00000012,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_IBLON  = 0x00000013,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_GENERICA  = 0x00000014,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_GENERICB  = 0x00000015,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_IGSL_ALLOW  = 0x00000016,
CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT_MANUAL_FLOW  = 0x00000017,
} CRTC_TRIGA_CNTL_CRTC_TRIGA_SOURCE_SELECT;

/*
 * CRTC_TRIGA_CNTL_CRTC_TRIGA_POLARITY_SELECT enum
 */

typedef enum CRTC_TRIGA_CNTL_CRTC_TRIGA_POLARITY_SELECT {
CRTC_TRIGA_CNTL_CRTC_TRIGA_POLARITY_SELECT_INTERLACE  = 0x00000001,
CRTC_TRIGA_CNTL_CRTC_TRIGA_POLARITY_SELECT_GENERICA  = 0x00000002,
CRTC_TRIGA_CNTL_CRTC_TRIGA_POLARITY_SELECT_GENERICB  = 0x00000003,
CRTC_TRIGA_CNTL_CRTC_TRIGA_POLARITY_SELECT_HSYNCA  = 0x00000004,
CRTC_TRIGA_CNTL_CRTC_TRIGA_POLARITY_SELECT_HSYNCB  = 0x00000005,
CRTC_TRIGA_CNTL_CRTC_TRIGA_POLARITY_SELECT_VIDEO  = 0x00000006,
CRTC_TRIGA_CNTL_CRTC_TRIGA_POLARITY_SELECT_GENERICC  = 0x00000007,
} CRTC_TRIGA_CNTL_CRTC_TRIGA_POLARITY_SELECT;

/*
 * CRTC_TRIGA_CNTL_CRTC_TRIGA_RESYNC_BYPASS_EN enum
 */

typedef enum CRTC_TRIGA_CNTL_CRTC_TRIGA_RESYNC_BYPASS_EN {
CRTC_TRIGA_CNTL_CRTC_TRIGA_RESYNC_BYPASS_EN_FALSE  = 0x00000000,
CRTC_TRIGA_CNTL_CRTC_TRIGA_RESYNC_BYPASS_EN_TRUE  = 0x00000001,
} CRTC_TRIGA_CNTL_CRTC_TRIGA_RESYNC_BYPASS_EN;

/*
 * CRTC_TRIGA_CNTL_CRTC_TRIGA_CLEAR enum
 */

typedef enum CRTC_TRIGA_CNTL_CRTC_TRIGA_CLEAR {
CRTC_TRIGA_CNTL_CRTC_TRIGA_CLEAR_FALSE   = 0x00000000,
CRTC_TRIGA_CNTL_CRTC_TRIGA_CLEAR_TRUE    = 0x00000001,
} CRTC_TRIGA_CNTL_CRTC_TRIGA_CLEAR;

/*
 * CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT enum
 */

typedef enum CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT {
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_VSYNCA_OTHER  = 0x00000001,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_HSYNCA_OTHER  = 0x00000002,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_GENERICF  = 0x00000005,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_GENERICE  = 0x00000006,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_VSYNCA  = 0x00000007,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_HSYNCA  = 0x00000008,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_VSYNCB  = 0x00000009,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_HSYNCB  = 0x0000000a,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_HPD1  = 0x0000000b,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_HPD2  = 0x0000000c,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_GENERICD  = 0x0000000d,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_GENERICC  = 0x0000000e,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_IGSL0  = 0x00000010,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_IGSL1  = 0x00000011,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_IGSL2  = 0x00000012,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_IBLON  = 0x00000013,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_GENERICA  = 0x00000014,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_GENERICB  = 0x00000015,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_IGSL_ALLOW  = 0x00000016,
CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT_MANUAL_FLOW  = 0x00000017,
} CRTC_TRIGB_CNTL_CRTC_TRIGB_SOURCE_SELECT;

/*
 * CRTC_TRIGB_CNTL_CRTC_TRIGB_POLARITY_SELECT enum
 */

typedef enum CRTC_TRIGB_CNTL_CRTC_TRIGB_POLARITY_SELECT {
CRTC_TRIGB_CNTL_CRTC_TRIGB_POLARITY_SELECT_INTERLACE  = 0x00000001,
CRTC_TRIGB_CNTL_CRTC_TRIGB_POLARITY_SELECT_GENERICA  = 0x00000002,
CRTC_TRIGB_CNTL_CRTC_TRIGB_POLARITY_SELECT_GENERICB  = 0x00000003,
CRTC_TRIGB_CNTL_CRTC_TRIGB_POLARITY_SELECT_HSYNCA  = 0x00000004,
CRTC_TRIGB_CNTL_CRTC_TRIGB_POLARITY_SELECT_HSYNCB  = 0x00000005,
CRTC_TRIGB_CNTL_CRTC_TRIGB_POLARITY_SELECT_VIDEO  = 0x00000006,
CRTC_TRIGB_CNTL_CRTC_TRIGB_POLARITY_SELECT_GENERICC  = 0x00000007,
} CRTC_TRIGB_CNTL_CRTC_TRIGB_POLARITY_SELECT;

/*
 * CRTC_TRIGB_CNTL_CRTC_TRIGB_RESYNC_BYPASS_EN enum
 */

typedef enum CRTC_TRIGB_CNTL_CRTC_TRIGB_RESYNC_BYPASS_EN {
CRTC_TRIGB_CNTL_CRTC_TRIGB_RESYNC_BYPASS_EN_FALSE  = 0x00000000,
CRTC_TRIGB_CNTL_CRTC_TRIGB_RESYNC_BYPASS_EN_TRUE  = 0x00000001,
} CRTC_TRIGB_CNTL_CRTC_TRIGB_RESYNC_BYPASS_EN;

/*
 * CRTC_TRIGB_CNTL_CRTC_TRIGB_CLEAR enum
 */

typedef enum CRTC_TRIGB_CNTL_CRTC_TRIGB_CLEAR {
CRTC_TRIGB_CNTL_CRTC_TRIGB_CLEAR_FALSE   = 0x00000000,
CRTC_TRIGB_CNTL_CRTC_TRIGB_CLEAR_TRUE    = 0x00000001,
} CRTC_TRIGB_CNTL_CRTC_TRIGB_CLEAR;

/*
 * CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_MODE enum
 */

typedef enum CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_MODE {
CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_MODE_DISABLE  = 0x00000000,
CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_MODE_HCOUNT  = 0x00000001,
CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_MODE_HCOUNT_VCOUNT  = 0x00000002,
CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_MODE_RESERVED  = 0x00000003,
} CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_MODE;

/*
 * CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_CHECK enum
 */

typedef enum CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_CHECK {
CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_CHECK_FALSE  = 0x00000000,
CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_CHECK_TRUE  = 0x00000001,
} CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_CHECK;

/*
 * CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_TRIG_SEL enum
 */

typedef enum CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_TRIG_SEL {
CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_TRIG_SEL_FALSE  = 0x00000000,
CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_TRIG_SEL_TRUE  = 0x00000001,
} CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_TRIG_SEL;

/*
 * CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_CLEAR enum
 */

typedef enum CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_CLEAR {
CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_CLEAR_FALSE = 0x00000000,
CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_CLEAR_TRUE  = 0x00000001,
} CRTC_FORCE_COUNT_NOW_CNTL_CRTC_FORCE_COUNT_NOW_CLEAR;

/*
 * CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT enum
 */

typedef enum CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT {
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_LOGIC0  = 0x00000000,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_GENERICF  = 0x00000001,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_GENERICE  = 0x00000002,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_HPD1  = 0x00000003,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_HPD2  = 0x00000004,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_DDC1DATA  = 0x00000005,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_DDC1CLK  = 0x00000006,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_DDC2DATA  = 0x00000007,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_DDC2CLK  = 0x00000008,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_DVOCLK  = 0x00000009,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_MANUAL  = 0x0000000a,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_LOGIC1  = 0x0000000b,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_GENERICB  = 0x0000000c,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_GENERICA  = 0x0000000d,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_GENERICD  = 0x0000000e,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT_GENERICC  = 0x0000000f,
} CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_SOURCE_SELECT;

/*
 * CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_POLARITY enum
 */

typedef enum CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_POLARITY {
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_POLARITY_FALSE  = 0x00000000,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_POLARITY_TRUE  = 0x00000001,
} CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_POLARITY;

/*
 * CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_GRANULARITY enum
 */

typedef enum CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_GRANULARITY {
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_GRANULARITY_FALSE  = 0x00000000,
CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_GRANULARITY_TRUE  = 0x00000001,
} CRTC_FLOW_CONTROL_CRTC_FLOW_CONTROL_GRANULARITY;

/*
 * CRTC_STEREO_FORCE_NEXT_EYE_CRTC_STEREO_FORCE_NEXT_EYE enum
 */

typedef enum CRTC_STEREO_FORCE_NEXT_EYE_CRTC_STEREO_FORCE_NEXT_EYE {
CRTC_STEREO_FORCE_NEXT_EYE_CRTC_STEREO_FORCE_NEXT_EYE_NO  = 0x00000000,
CRTC_STEREO_FORCE_NEXT_EYE_CRTC_STEREO_FORCE_NEXT_EYE_RIGHT  = 0x00000001,
CRTC_STEREO_FORCE_NEXT_EYE_CRTC_STEREO_FORCE_NEXT_EYE_LEFT  = 0x00000002,
CRTC_STEREO_FORCE_NEXT_EYE_CRTC_STEREO_FORCE_NEXT_EYE_RESERVED  = 0x00000003,
} CRTC_STEREO_FORCE_NEXT_EYE_CRTC_STEREO_FORCE_NEXT_EYE;

/*
 * CRTC_CONTROL_CRTC_MASTER_EN enum
 */

typedef enum CRTC_CONTROL_CRTC_MASTER_EN {
CRTC_CONTROL_CRTC_MASTER_EN_FALSE        = 0x00000000,
CRTC_CONTROL_CRTC_MASTER_EN_TRUE         = 0x00000001,
} CRTC_CONTROL_CRTC_MASTER_EN;

/*
 * CRTC_BLANK_CONTROL_CRTC_BLANK_DATA_EN enum
 */

typedef enum CRTC_BLANK_CONTROL_CRTC_BLANK_DATA_EN {
CRTC_BLANK_CONTROL_CRTC_BLANK_DATA_EN_FALSE  = 0x00000000,
CRTC_BLANK_CONTROL_CRTC_BLANK_DATA_EN_TRUE  = 0x00000001,
} CRTC_BLANK_CONTROL_CRTC_BLANK_DATA_EN;

/*
 * CRTC_BLANK_CONTROL_CRTC_BLANK_DE_MODE enum
 */

typedef enum CRTC_BLANK_CONTROL_CRTC_BLANK_DE_MODE {
CRTC_BLANK_CONTROL_CRTC_BLANK_DE_MODE_FALSE  = 0x00000000,
CRTC_BLANK_CONTROL_CRTC_BLANK_DE_MODE_TRUE  = 0x00000001,
} CRTC_BLANK_CONTROL_CRTC_BLANK_DE_MODE;

/*
 * CRTC_INTERLACE_CONTROL_CRTC_INTERLACE_ENABLE enum
 */

typedef enum CRTC_INTERLACE_CONTROL_CRTC_INTERLACE_ENABLE {
CRTC_INTERLACE_CONTROL_CRTC_INTERLACE_ENABLE_FALSE  = 0x00000000,
CRTC_INTERLACE_CONTROL_CRTC_INTERLACE_ENABLE_TRUE  = 0x00000001,
} CRTC_INTERLACE_CONTROL_CRTC_INTERLACE_ENABLE;

/*
 * CRTC_INTERLACE_CONTROL_CRTC_INTERLACE_FORCE_NEXT_FIELD enum
 */

typedef enum CRTC_INTERLACE_CONTROL_CRTC_INTERLACE_FORCE_NEXT_FIELD {
CRTC_INTERLACE_CONTROL_CRTC_INTERLACE_FORCE_NEXT_FIELD_NOT  = 0x00000000,
CRTC_INTERLACE_CONTROL_CRTC_INTERLACE_FORCE_NEXT_FIELD_ODD  = 0x00000001,
CRTC_INTERLACE_CONTROL_CRTC_INTERLACE_FORCE_NEXT_FIELD_EVEN  = 0x00000002,
CRTC_INTERLACE_CONTROL_CRTC_INTERLACE_FORCE_NEXT_FIELD_NOT2  = 0x00000003,
} CRTC_INTERLACE_CONTROL_CRTC_INTERLACE_FORCE_NEXT_FIELD;

/*
 * CRTC_FIELD_INDICATION_CONTROL_CRTC_FIELD_INDICATION_OUTPUT_POLARITY enum
 */

typedef enum CRTC_FIELD_INDICATION_CONTROL_CRTC_FIELD_INDICATION_OUTPUT_POLARITY {
CRTC_FIELD_INDICATION_CONTROL_CRTC_FIELD_INDICATION_OUTPUT_POLARITY_FALSE  = 0x00000000,
CRTC_FIELD_INDICATION_CONTROL_CRTC_FIELD_INDICATION_OUTPUT_POLARITY_TRUE  = 0x00000001,
} CRTC_FIELD_INDICATION_CONTROL_CRTC_FIELD_INDICATION_OUTPUT_POLARITY;

/*
 * CRTC_FIELD_INDICATION_CONTROL_CRTC_FIELD_ALIGNMENT enum
 */

typedef enum CRTC_FIELD_INDICATION_CONTROL_CRTC_FIELD_ALIGNMENT {
CRTC_FIELD_INDICATION_CONTROL_CRTC_FIELD_ALIGNMENT_FALSE  = 0x00000000,
CRTC_FIELD_INDICATION_CONTROL_CRTC_FIELD_ALIGNMENT_TRUE  = 0x00000001,
} CRTC_FIELD_INDICATION_CONTROL_CRTC_FIELD_ALIGNMENT;

/*
 * CRTC_COUNT_CONTROL_CRTC_HORZ_COUNT_BY2_EN enum
 */

typedef enum CRTC_COUNT_CONTROL_CRTC_HORZ_COUNT_BY2_EN {
CRTC_COUNT_CONTROL_CRTC_HORZ_COUNT_BY2_EN_FALSE  = 0x00000000,
CRTC_COUNT_CONTROL_CRTC_HORZ_COUNT_BY2_EN_TRUE  = 0x00000001,
} CRTC_COUNT_CONTROL_CRTC_HORZ_COUNT_BY2_EN;

/*
 * CRTC_MANUAL_FORCE_VSYNC_NEXT_LINE_CRTC_MANUAL_FORCE_VSYNC_NEXT_LINE enum
 */

typedef enum CRTC_MANUAL_FORCE_VSYNC_NEXT_LINE_CRTC_MANUAL_FORCE_VSYNC_NEXT_LINE {
CRTC_MANUAL_FORCE_VSYNC_NEXT_LINE_CRTC_MANUAL_FORCE_VSYNC_NEXT_LINE_FALSE = 0x00000000,
CRTC_MANUAL_FORCE_VSYNC_NEXT_LINE_CRTC_MANUAL_FORCE_VSYNC_NEXT_LINE_TRUE  = 0x00000001,
} CRTC_MANUAL_FORCE_VSYNC_NEXT_LINE_CRTC_MANUAL_FORCE_VSYNC_NEXT_LINE;

/*
 * CRTC_VERT_SYNC_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_CLEAR enum
 */

typedef enum CRTC_VERT_SYNC_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_CLEAR {
CRTC_VERT_SYNC_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_CLEAR_FALSE = 0x00000000,
CRTC_VERT_SYNC_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_CLEAR_TRUE  = 0x00000001,
} CRTC_VERT_SYNC_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_CLEAR;

/*
 * CRTC_VERT_SYNC_CONTROL_CRTC_AUTO_FORCE_VSYNC_MODE enum
 */

typedef enum CRTC_VERT_SYNC_CONTROL_CRTC_AUTO_FORCE_VSYNC_MODE {
CRTC_VERT_SYNC_CONTROL_CRTC_AUTO_FORCE_VSYNC_MODE_DISABLE  = 0x00000000,
CRTC_VERT_SYNC_CONTROL_CRTC_AUTO_FORCE_VSYNC_MODE_TRIGGERA  = 0x00000001,
CRTC_VERT_SYNC_CONTROL_CRTC_AUTO_FORCE_VSYNC_MODE_TRIGGERB  = 0x00000002,
CRTC_VERT_SYNC_CONTROL_CRTC_AUTO_FORCE_VSYNC_MODE_RESERVED  = 0x00000003,
} CRTC_VERT_SYNC_CONTROL_CRTC_AUTO_FORCE_VSYNC_MODE;

/*
 * CRTC_STEREO_CONTROL_CRTC_STEREO_SYNC_OUTPUT_POLARITY enum
 */

typedef enum CRTC_STEREO_CONTROL_CRTC_STEREO_SYNC_OUTPUT_POLARITY {
CRTC_STEREO_CONTROL_CRTC_STEREO_SYNC_OUTPUT_POLARITY_FALSE  = 0x00000000,
CRTC_STEREO_CONTROL_CRTC_STEREO_SYNC_OUTPUT_POLARITY_TRUE  = 0x00000001,
} CRTC_STEREO_CONTROL_CRTC_STEREO_SYNC_OUTPUT_POLARITY;

/*
 * CRTC_STEREO_CONTROL_CRTC_STEREO_SYNC_SELECT_POLARITY enum
 */

typedef enum CRTC_STEREO_CONTROL_CRTC_STEREO_SYNC_SELECT_POLARITY {
CRTC_STEREO_CONTROL_CRTC_STEREO_SYNC_SELECT_POLARITY_FALSE  = 0x00000000,
CRTC_STEREO_CONTROL_CRTC_STEREO_SYNC_SELECT_POLARITY_TRUE  = 0x00000001,
} CRTC_STEREO_CONTROL_CRTC_STEREO_SYNC_SELECT_POLARITY;

/*
 * CRTC_STEREO_CONTROL_CRTC_STEREO_EYE_FLAG_POLARITY enum
 */

typedef enum CRTC_STEREO_CONTROL_CRTC_STEREO_EYE_FLAG_POLARITY {
CRTC_STEREO_CONTROL_CRTC_STEREO_EYE_FLAG_POLARITY_FALSE  = 0x00000000,
CRTC_STEREO_CONTROL_CRTC_STEREO_EYE_FLAG_POLARITY_TRUE  = 0x00000001,
} CRTC_STEREO_CONTROL_CRTC_STEREO_EYE_FLAG_POLARITY;

/*
 * CRTC_STEREO_CONTROL_CRTC_STEREO_EN enum
 */

typedef enum CRTC_STEREO_CONTROL_CRTC_STEREO_EN {
CRTC_STEREO_CONTROL_CRTC_STEREO_EN_FALSE  = 0x00000000,
CRTC_STEREO_CONTROL_CRTC_STEREO_EN_TRUE  = 0x00000001,
} CRTC_STEREO_CONTROL_CRTC_STEREO_EN;

/*
 * CRTC_SNAPSHOT_STATUS_CRTC_SNAPSHOT_CLEAR enum
 */

typedef enum CRTC_SNAPSHOT_STATUS_CRTC_SNAPSHOT_CLEAR {
CRTC_SNAPSHOT_STATUS_CRTC_SNAPSHOT_CLEAR_FALSE = 0x00000000,
CRTC_SNAPSHOT_STATUS_CRTC_SNAPSHOT_CLEAR_TRUE  = 0x00000001,
} CRTC_SNAPSHOT_STATUS_CRTC_SNAPSHOT_CLEAR;

/*
 * CRTC_SNAPSHOT_CONTROL_CRTC_AUTO_SNAPSHOT_TRIG_SEL enum
 */

typedef enum CRTC_SNAPSHOT_CONTROL_CRTC_AUTO_SNAPSHOT_TRIG_SEL {
CRTC_SNAPSHOT_CONTROL_CRTC_AUTO_SNAPSHOT_TRIG_SEL_DISABLE  = 0x00000000,
CRTC_SNAPSHOT_CONTROL_CRTC_AUTO_SNAPSHOT_TRIG_SEL_TRIGGERA  = 0x00000001,
CRTC_SNAPSHOT_CONTROL_CRTC_AUTO_SNAPSHOT_TRIG_SEL_TRIGGERB  = 0x00000002,
CRTC_SNAPSHOT_CONTROL_CRTC_AUTO_SNAPSHOT_TRIG_SEL_RESERVED  = 0x00000003,
} CRTC_SNAPSHOT_CONTROL_CRTC_AUTO_SNAPSHOT_TRIG_SEL;

/*
 * CRTC_START_LINE_CONTROL_CRTC_PROGRESSIVE_START_LINE_EARLY enum
 */

typedef enum CRTC_START_LINE_CONTROL_CRTC_PROGRESSIVE_START_LINE_EARLY {
CRTC_START_LINE_CONTROL_CRTC_PROGRESSIVE_START_LINE_EARLY_FALSE = 0x00000000,
CRTC_START_LINE_CONTROL_CRTC_PROGRESSIVE_START_LINE_EARLY_TRUE  = 0x00000001,
} CRTC_START_LINE_CONTROL_CRTC_PROGRESSIVE_START_LINE_EARLY;

/*
 * CRTC_START_LINE_CONTROL_CRTC_INTERLACE_START_LINE_EARLY enum
 */

typedef enum CRTC_START_LINE_CONTROL_CRTC_INTERLACE_START_LINE_EARLY {
CRTC_START_LINE_CONTROL_CRTC_INTERLACE_START_LINE_EARLY_FALSE = 0x00000000,
CRTC_START_LINE_CONTROL_CRTC_INTERLACE_START_LINE_EARLY_TRUE  = 0x00000001,
} CRTC_START_LINE_CONTROL_CRTC_INTERLACE_START_LINE_EARLY;

/*
 * CRTC_START_LINE_CONTROL_CRTC_LEGACY_REQUESTOR_EN enum
 */

typedef enum CRTC_START_LINE_CONTROL_CRTC_LEGACY_REQUESTOR_EN {
CRTC_START_LINE_CONTROL_CRTC_LEGACY_REQUESTOR_EN_FALSE  = 0x00000000,
CRTC_START_LINE_CONTROL_CRTC_LEGACY_REQUESTOR_EN_TRUE  = 0x00000001,
} CRTC_START_LINE_CONTROL_CRTC_LEGACY_REQUESTOR_EN;

/*
 * CRTC_START_LINE_CONTROL_CRTC_PREFETCH_EN enum
 */

typedef enum CRTC_START_LINE_CONTROL_CRTC_PREFETCH_EN {
CRTC_START_LINE_CONTROL_CRTC_PREFETCH_EN_FALSE  = 0x00000000,
CRTC_START_LINE_CONTROL_CRTC_PREFETCH_EN_TRUE  = 0x00000001,
} CRTC_START_LINE_CONTROL_CRTC_PREFETCH_EN;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_SNAPSHOT_INT_MSK enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_SNAPSHOT_INT_MSK {
CRTC_INTERRUPT_CONTROL_CRTC_SNAPSHOT_INT_MSK_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_SNAPSHOT_INT_MSK_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_SNAPSHOT_INT_MSK;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_SNAPSHOT_INT_TYPE enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_SNAPSHOT_INT_TYPE {
CRTC_INTERRUPT_CONTROL_CRTC_SNAPSHOT_INT_TYPE_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_SNAPSHOT_INT_TYPE_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_SNAPSHOT_INT_TYPE;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_V_UPDATE_INT_MSK enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_V_UPDATE_INT_MSK {
CRTC_INTERRUPT_CONTROL_CRTC_V_UPDATE_INT_MSK_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_V_UPDATE_INT_MSK_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_V_UPDATE_INT_MSK;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_V_UPDATE_INT_TYPE enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_V_UPDATE_INT_TYPE {
CRTC_INTERRUPT_CONTROL_CRTC_V_UPDATE_INT_TYPE_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_V_UPDATE_INT_TYPE_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_V_UPDATE_INT_TYPE;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_FORCE_COUNT_NOW_INT_MSK enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_FORCE_COUNT_NOW_INT_MSK {
CRTC_INTERRUPT_CONTROL_CRTC_FORCE_COUNT_NOW_INT_MSK_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_FORCE_COUNT_NOW_INT_MSK_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_FORCE_COUNT_NOW_INT_MSK;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_FORCE_COUNT_NOW_INT_TYPE enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_FORCE_COUNT_NOW_INT_TYPE {
CRTC_INTERRUPT_CONTROL_CRTC_FORCE_COUNT_NOW_INT_TYPE_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_FORCE_COUNT_NOW_INT_TYPE_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_FORCE_COUNT_NOW_INT_TYPE;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_INT_MSK enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_INT_MSK {
CRTC_INTERRUPT_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_INT_MSK_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_INT_MSK_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_INT_MSK;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_INT_TYPE enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_INT_TYPE {
CRTC_INTERRUPT_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_INT_TYPE_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_INT_TYPE_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_FORCE_VSYNC_NEXT_LINE_INT_TYPE;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_TRIGA_INT_MSK enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_TRIGA_INT_MSK {
CRTC_INTERRUPT_CONTROL_CRTC_TRIGA_INT_MSK_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_TRIGA_INT_MSK_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_TRIGA_INT_MSK;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_TRIGA_INT_TYPE enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_TRIGA_INT_TYPE {
CRTC_INTERRUPT_CONTROL_CRTC_TRIGA_INT_TYPE_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_TRIGA_INT_TYPE_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_TRIGA_INT_TYPE;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_TRIGB_INT_MSK enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_TRIGB_INT_MSK {
CRTC_INTERRUPT_CONTROL_CRTC_TRIGB_INT_MSK_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_TRIGB_INT_MSK_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_TRIGB_INT_MSK;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_TRIGB_INT_TYPE enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_TRIGB_INT_TYPE {
CRTC_INTERRUPT_CONTROL_CRTC_TRIGB_INT_TYPE_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_TRIGB_INT_TYPE_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_TRIGB_INT_TYPE;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_VSYNC_NOM_INT_MSK enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_VSYNC_NOM_INT_MSK {
CRTC_INTERRUPT_CONTROL_CRTC_VSYNC_NOM_INT_MSK_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_VSYNC_NOM_INT_MSK_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_VSYNC_NOM_INT_MSK;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_VSYNC_NOM_INT_TYPE enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_VSYNC_NOM_INT_TYPE {
CRTC_INTERRUPT_CONTROL_CRTC_VSYNC_NOM_INT_TYPE_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_VSYNC_NOM_INT_TYPE_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_VSYNC_NOM_INT_TYPE;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_GSL_VSYNC_GAP_INT_MSK enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_GSL_VSYNC_GAP_INT_MSK {
CRTC_INTERRUPT_CONTROL_CRTC_GSL_VSYNC_GAP_INT_MSK_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_GSL_VSYNC_GAP_INT_MSK_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_GSL_VSYNC_GAP_INT_MSK;

/*
 * CRTC_INTERRUPT_CONTROL_CRTC_GSL_VSYNC_GAP_INT_TYPE enum
 */

typedef enum CRTC_INTERRUPT_CONTROL_CRTC_GSL_VSYNC_GAP_INT_TYPE {
CRTC_INTERRUPT_CONTROL_CRTC_GSL_VSYNC_GAP_INT_TYPE_FALSE  = 0x00000000,
CRTC_INTERRUPT_CONTROL_CRTC_GSL_VSYNC_GAP_INT_TYPE_TRUE  = 0x00000001,
} CRTC_INTERRUPT_CONTROL_CRTC_GSL_VSYNC_GAP_INT_TYPE;

/*
 * CRTC_UPDATE_LOCK_CRTC_UPDATE_LOCK enum
 */

typedef enum CRTC_UPDATE_LOCK_CRTC_UPDATE_LOCK {
CRTC_UPDATE_LOCK_CRTC_UPDATE_LOCK_FALSE  = 0x00000000,
CRTC_UPDATE_LOCK_CRTC_UPDATE_LOCK_TRUE   = 0x00000001,
} CRTC_UPDATE_LOCK_CRTC_UPDATE_LOCK;

/*
 * CRTC_DOUBLE_BUFFER_CONTROL_CRTC_UPDATE_INSTANTLY enum
 */

typedef enum CRTC_DOUBLE_BUFFER_CONTROL_CRTC_UPDATE_INSTANTLY {
CRTC_DOUBLE_BUFFER_CONTROL_CRTC_UPDATE_INSTANTLY_FALSE  = 0x00000000,
CRTC_DOUBLE_BUFFER_CONTROL_CRTC_UPDATE_INSTANTLY_TRUE  = 0x00000001,
} CRTC_DOUBLE_BUFFER_CONTROL_CRTC_UPDATE_INSTANTLY;

/*
 * CRTC_DOUBLE_BUFFER_CONTROL_CRTC_BLANK_DATA_DOUBLE_BUFFER_EN enum
 */

typedef enum CRTC_DOUBLE_BUFFER_CONTROL_CRTC_BLANK_DATA_DOUBLE_BUFFER_EN {
CRTC_DOUBLE_BUFFER_CONTROL_CRTC_BLANK_DATA_DOUBLE_BUFFER_EN_FALSE  = 0x00000000,
CRTC_DOUBLE_BUFFER_CONTROL_CRTC_BLANK_DATA_DOUBLE_BUFFER_EN_TRUE  = 0x00000001,
} CRTC_DOUBLE_BUFFER_CONTROL_CRTC_BLANK_DATA_DOUBLE_BUFFER_EN;

/*
 * CRTC_DOUBLE_BUFFER_CONTROL_CRTC_RANGE_TIMING_DBUF_UPDATE_MODE enum
 */

typedef enum CRTC_DOUBLE_BUFFER_CONTROL_CRTC_RANGE_TIMING_DBUF_UPDATE_MODE {
CRTC_DOUBLE_BUFFER_CONTROL_CRTC_RANGE_TIMING_DBUF_UPDATE_MODE_0  = 0x00000000,
CRTC_DOUBLE_BUFFER_CONTROL_CRTC_RANGE_TIMING_DBUF_UPDATE_MODE_1  = 0x00000001,
} CRTC_DOUBLE_BUFFER_CONTROL_CRTC_RANGE_TIMING_DBUF_UPDATE_MODE;

/*
 * CRTC_VGA_PARAMETER_CAPTURE_MODE_CRTC_VGA_PARAMETER_CAPTURE_MODE enum
 */

typedef enum CRTC_VGA_PARAMETER_CAPTURE_MODE_CRTC_VGA_PARAMETER_CAPTURE_MODE {
CRTC_VGA_PARAMETER_CAPTURE_MODE_CRTC_VGA_PARAMETER_CAPTURE_MODE_FALSE  = 0x00000000,
CRTC_VGA_PARAMETER_CAPTURE_MODE_CRTC_VGA_PARAMETER_CAPTURE_MODE_TRUE  = 0x00000001,
} CRTC_VGA_PARAMETER_CAPTURE_MODE_CRTC_VGA_PARAMETER_CAPTURE_MODE;

/*
 * CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_EN enum
 */

typedef enum CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_EN {
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_EN_FALSE  = 0x00000000,
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_EN_TRUE  = 0x00000001,
} CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_EN;

/*
 * CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_MODE enum
 */

typedef enum CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_MODE {
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_MODE_RGB  = 0x00000000,
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_MODE_YCBCR601  = 0x00000001,
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_MODE_YCBCR709  = 0x00000002,
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_MODE_VBARS  = 0x00000003,
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_MODE_HBARS  = 0x00000004,
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_MODE_SRRGB  = 0x00000005,
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_MODE_DRRGB  = 0x00000006,
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_MODE_XRBIAS  = 0x00000007,
} CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_MODE;

/*
 * CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_DYNAMIC_RANGE enum
 */

typedef enum CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_DYNAMIC_RANGE {
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_DYNAMIC_RANGE_FALSE  = 0x00000000,
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_DYNAMIC_RANGE_TRUE  = 0x00000001,
} CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_DYNAMIC_RANGE;

/*
 * CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_COLOR_FORMAT enum
 */

typedef enum CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_COLOR_FORMAT {
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_COLOR_FORMAT_6BPC  = 0x00000000,
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_COLOR_FORMAT_8BPC  = 0x00000001,
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_COLOR_FORMAT_10BPC  = 0x00000002,
CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_COLOR_FORMAT_RESERVED  = 0x00000003,
} CRTC_TEST_PATTERN_CONTROL_CRTC_TEST_PATTERN_COLOR_FORMAT;

/*
 * MASTER_UPDATE_LOCK_MASTER_UPDATE_LOCK enum
 */

typedef enum MASTER_UPDATE_LOCK_MASTER_UPDATE_LOCK {
MASTER_UPDATE_LOCK_MASTER_UPDATE_LOCK_FALSE  = 0x00000000,
MASTER_UPDATE_LOCK_MASTER_UPDATE_LOCK_TRUE  = 0x00000001,
} MASTER_UPDATE_LOCK_MASTER_UPDATE_LOCK;

/*
 * MASTER_UPDATE_LOCK_GSL_CONTROL_MASTER_UPDATE_LOCK enum
 */

typedef enum MASTER_UPDATE_LOCK_GSL_CONTROL_MASTER_UPDATE_LOCK {
MASTER_UPDATE_LOCK_GSL_CONTROL_MASTER_UPDATE_LOCK_FALSE  = 0x00000000,
MASTER_UPDATE_LOCK_GSL_CONTROL_MASTER_UPDATE_LOCK_TRUE  = 0x00000001,
} MASTER_UPDATE_LOCK_GSL_CONTROL_MASTER_UPDATE_LOCK;

/*
 * MASTER_UPDATE_LOCK_UNDERFLOW_UPDATE_LOCK enum
 */

typedef enum MASTER_UPDATE_LOCK_UNDERFLOW_UPDATE_LOCK {
MASTER_UPDATE_LOCK_UNDERFLOW_UPDATE_LOCK_FALSE  = 0x00000000,
MASTER_UPDATE_LOCK_UNDERFLOW_UPDATE_LOCK_TRUE  = 0x00000001,
} MASTER_UPDATE_LOCK_UNDERFLOW_UPDATE_LOCK;

/*
 * MASTER_UPDATE_MODE_MASTER_UPDATE_MODE enum
 */

typedef enum MASTER_UPDATE_MODE_MASTER_UPDATE_MODE {
MASTER_UPDATE_MODE_MASTER_UPDATE_MODE_BETWEEN  = 0x00000000,
MASTER_UPDATE_MODE_MASTER_UPDATE_MODE_HSYNCA  = 0x00000001,
MASTER_UPDATE_MODE_MASTER_UPDATE_MODE_VSYNCA  = 0x00000002,
MASTER_UPDATE_MODE_MASTER_UPDATE_MODE_BEFORE  = 0x00000003,
} MASTER_UPDATE_MODE_MASTER_UPDATE_MODE;

/*
 * MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE enum
 */

typedef enum MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE {
MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE_BOTH  = 0x00000000,
MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE_EVEN  = 0x00000001,
MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE_ODD  = 0x00000002,
MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE_RESERVED  = 0x00000003,
} MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE;

/*
 * CRTC_MVP_INBAND_CNTL_INSERT_CRTC_MVP_INBAND_OUT_MODE enum
 */

typedef enum CRTC_MVP_INBAND_CNTL_INSERT_CRTC_MVP_INBAND_OUT_MODE {
CRTC_MVP_INBAND_CNTL_INSERT_CRTC_MVP_INBAND_OUT_MODE_DISABLE  = 0x00000000,
CRTC_MVP_INBAND_CNTL_INSERT_CRTC_MVP_INBAND_OUT_MODE_DEBUG  = 0x00000001,
CRTC_MVP_INBAND_CNTL_INSERT_CRTC_MVP_INBAND_OUT_MODE_NORMAL  = 0x00000002,
} CRTC_MVP_INBAND_CNTL_INSERT_CRTC_MVP_INBAND_OUT_MODE;

/*
 * CRTC_MVP_STATUS_CRTC_FLIP_NOW_CLEAR enum
 */

typedef enum CRTC_MVP_STATUS_CRTC_FLIP_NOW_CLEAR {
CRTC_MVP_STATUS_CRTC_FLIP_NOW_CLEAR_FALSE = 0x00000000,
CRTC_MVP_STATUS_CRTC_FLIP_NOW_CLEAR_TRUE  = 0x00000001,
} CRTC_MVP_STATUS_CRTC_FLIP_NOW_CLEAR;

/*
 * CRTC_MVP_STATUS_CRTC_AFR_HSYNC_SWITCH_DONE_CLEAR enum
 */

typedef enum CRTC_MVP_STATUS_CRTC_AFR_HSYNC_SWITCH_DONE_CLEAR {
CRTC_MVP_STATUS_CRTC_AFR_HSYNC_SWITCH_DONE_CLEAR_FALSE = 0x00000000,
CRTC_MVP_STATUS_CRTC_AFR_HSYNC_SWITCH_DONE_CLEAR_TRUE  = 0x00000001,
} CRTC_MVP_STATUS_CRTC_AFR_HSYNC_SWITCH_DONE_CLEAR;

/*
 * CRTC_V_UPDATE_INT_STATUS_CRTC_V_UPDATE_INT_CLEAR enum
 */

typedef enum CRTC_V_UPDATE_INT_STATUS_CRTC_V_UPDATE_INT_CLEAR {
CRTC_V_UPDATE_INT_STATUS_CRTC_V_UPDATE_INT_CLEAR_FALSE = 0x00000000,
CRTC_V_UPDATE_INT_STATUS_CRTC_V_UPDATE_INT_CLEAR_TRUE  = 0x00000001,
} CRTC_V_UPDATE_INT_STATUS_CRTC_V_UPDATE_INT_CLEAR;

/*
 * CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_OUTPUT_POLARITY enum
 */

typedef enum CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_OUTPUT_POLARITY {
CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_OUTPUT_POLARITY_FALSE  = 0x00000000,
CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_OUTPUT_POLARITY_TRUE  = 0x00000001,
} CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_OUTPUT_POLARITY;

/*
 * CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_INT_ENABLE enum
 */

typedef enum CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_INT_ENABLE {
CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_INT_ENABLE_FALSE = 0x00000000,
CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_INT_ENABLE_TRUE  = 0x00000001,
} CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_INT_ENABLE;

/*
 * CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_CLEAR enum
 */

typedef enum CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_CLEAR {
CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_CLEAR_FALSE = 0x00000000,
CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_CLEAR_TRUE  = 0x00000001,
} CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_CLEAR;

/*
 * CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_INT_TYPE enum
 */

typedef enum CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_INT_TYPE {
CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_INT_TYPE_FALSE  = 0x00000000,
CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_INT_TYPE_TRUE  = 0x00000001,
} CRTC_VERTICAL_INTERRUPT0_CONTROL_CRTC_VERTICAL_INTERRUPT0_INT_TYPE;

/*
 * CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_CLEAR enum
 */

typedef enum CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_CLEAR {
CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_CLEAR_CLEAR_FALSE = 0x00000000,
CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_CLEAR_TRUE  = 0x00000001,
} CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_CLEAR;

/*
 * CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_INT_ENABLE enum
 */

typedef enum CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_INT_ENABLE {
CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_INT_ENABLE_FALSE = 0x00000000,
CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_INT_ENABLE_TRUE  = 0x00000001,
} CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_INT_ENABLE;

/*
 * CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_INT_TYPE enum
 */

typedef enum CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_INT_TYPE {
CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_INT_TYPE_FALSE  = 0x00000000,
CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_INT_TYPE_TRUE  = 0x00000001,
} CRTC_VERTICAL_INTERRUPT1_CONTROL_CRTC_VERTICAL_INTERRUPT1_INT_TYPE;

/*
 * CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_CLEAR enum
 */

typedef enum CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_CLEAR {
CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_CLEAR_CLEAR_FALSE = 0x00000000,
CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_CLEAR_TRUE  = 0x00000001,
} CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_CLEAR;

/*
 * CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_INT_ENABLE enum
 */

typedef enum CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_INT_ENABLE {
CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_INT_ENABLE_FALSE = 0x00000000,
CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_INT_ENABLE_TRUE  = 0x00000001,
} CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_INT_ENABLE;

/*
 * CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_INT_TYPE enum
 */

typedef enum CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_INT_TYPE {
CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_INT_TYPE_FALSE  = 0x00000000,
CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_INT_TYPE_TRUE  = 0x00000001,
} CRTC_VERTICAL_INTERRUPT2_CONTROL_CRTC_VERTICAL_INTERRUPT2_INT_TYPE;

/*
 * CRTC_CRC_CNTL_CRTC_CRC_EN enum
 */

typedef enum CRTC_CRC_CNTL_CRTC_CRC_EN {
CRTC_CRC_CNTL_CRTC_CRC_EN_FALSE          = 0x00000000,
CRTC_CRC_CNTL_CRTC_CRC_EN_TRUE           = 0x00000001,
} CRTC_CRC_CNTL_CRTC_CRC_EN;

/*
 * CRTC_CRC_CNTL_CRTC_CRC_CONT_EN enum
 */

typedef enum CRTC_CRC_CNTL_CRTC_CRC_CONT_EN {
CRTC_CRC_CNTL_CRTC_CRC_CONT_EN_FALSE     = 0x00000000,
CRTC_CRC_CNTL_CRTC_CRC_CONT_EN_TRUE      = 0x00000001,
} CRTC_CRC_CNTL_CRTC_CRC_CONT_EN;

/*
 * CRTC_CRC_CNTL_CRTC_CRC_STEREO_MODE enum
 */

typedef enum CRTC_CRC_CNTL_CRTC_CRC_STEREO_MODE {
CRTC_CRC_CNTL_CRTC_CRC_STEREO_MODE_LEFT  = 0x00000000,
CRTC_CRC_CNTL_CRTC_CRC_STEREO_MODE_RIGHT  = 0x00000001,
CRTC_CRC_CNTL_CRTC_CRC_STEREO_MODE_BOTH_EYES  = 0x00000002,
CRTC_CRC_CNTL_CRTC_CRC_STEREO_MODE_BOTH_FIELDS  = 0x00000003,
} CRTC_CRC_CNTL_CRTC_CRC_STEREO_MODE;

/*
 * CRTC_CRC_CNTL_CRTC_CRC_INTERLACE_MODE enum
 */

typedef enum CRTC_CRC_CNTL_CRTC_CRC_INTERLACE_MODE {
CRTC_CRC_CNTL_CRTC_CRC_INTERLACE_MODE_TOP  = 0x00000000,
CRTC_CRC_CNTL_CRTC_CRC_INTERLACE_MODE_BOTTOM  = 0x00000001,
CRTC_CRC_CNTL_CRTC_CRC_INTERLACE_MODE_BOTH_BOTTOM  = 0x00000002,
CRTC_CRC_CNTL_CRTC_CRC_INTERLACE_MODE_BOTH_FIELD  = 0x00000003,
} CRTC_CRC_CNTL_CRTC_CRC_INTERLACE_MODE;

/*
 * CRTC_CRC_CNTL_CRTC_CRC_USE_NEW_AND_REPEATED_PIXELS enum
 */

typedef enum CRTC_CRC_CNTL_CRTC_CRC_USE_NEW_AND_REPEATED_PIXELS {
CRTC_CRC_CNTL_CRTC_CRC_USE_NEW_AND_REPEATED_PIXELS_FALSE = 0x00000000,
CRTC_CRC_CNTL_CRTC_CRC_USE_NEW_AND_REPEATED_PIXELS_TRUE  = 0x00000001,
} CRTC_CRC_CNTL_CRTC_CRC_USE_NEW_AND_REPEATED_PIXELS;

/*
 * CRTC_CRC_CNTL_CRTC_CRTC_CRC0_SELECT enum
 */

typedef enum CRTC_CRC_CNTL_CRTC_CRTC_CRC0_SELECT {
CRTC_CRC_CNTL_CRTC_CRTC_CRC0_SELECT_UAB  = 0x00000000,
CRTC_CRC_CNTL_CRTC_CRTC_CRC0_SELECT_UA_B  = 0x00000001,
CRTC_CRC_CNTL_CRTC_CRTC_CRC0_SELECT_U_AB  = 0x00000002,
CRTC_CRC_CNTL_CRTC_CRTC_CRC0_SELECT_U_A_B  = 0x00000003,
CRTC_CRC_CNTL_CRTC_CRTC_CRC0_SELECT_IAB  = 0x00000004,
CRTC_CRC_CNTL_CRTC_CRTC_CRC0_SELECT_IA_B  = 0x00000005,
CRTC_CRC_CNTL_CRTC_CRTC_CRC0_SELECT_I_AB  = 0x00000006,
CRTC_CRC_CNTL_CRTC_CRTC_CRC0_SELECT_I_A_B  = 0x00000007,
} CRTC_CRC_CNTL_CRTC_CRTC_CRC0_SELECT;

/*
 * CRTC_CRC_CNTL_CRTC_CRTC_CRC1_SELECT enum
 */

typedef enum CRTC_CRC_CNTL_CRTC_CRTC_CRC1_SELECT {
CRTC_CRC_CNTL_CRTC_CRTC_CRC1_SELECT_UAB  = 0x00000000,
CRTC_CRC_CNTL_CRTC_CRTC_CRC1_SELECT_UA_B  = 0x00000001,
CRTC_CRC_CNTL_CRTC_CRTC_CRC1_SELECT_U_AB  = 0x00000002,
CRTC_CRC_CNTL_CRTC_CRTC_CRC1_SELECT_U_A_B  = 0x00000003,
CRTC_CRC_CNTL_CRTC_CRTC_CRC1_SELECT_IAB  = 0x00000004,
CRTC_CRC_CNTL_CRTC_CRTC_CRC1_SELECT_IA_B  = 0x00000005,
CRTC_CRC_CNTL_CRTC_CRTC_CRC1_SELECT_I_AB  = 0x00000006,
CRTC_CRC_CNTL_CRTC_CRTC_CRC1_SELECT_I_A_B  = 0x00000007,
} CRTC_CRC_CNTL_CRTC_CRTC_CRC1_SELECT;

/*
 * CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_ENABLE enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_ENABLE {
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_ENABLE_DISABLE  = 0x00000000,
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_ENABLE_ONESHOT  = 0x00000001,
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_ENABLE_CONTINUOUS  = 0x00000002,
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_ENABLE_RESERVED  = 0x00000003,
} CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_ENABLE;

/*
 * CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_HCOUNT_MODE_ENABLE enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_HCOUNT_MODE_ENABLE {
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_HCOUNT_MODE_ENABLE_FALSE  = 0x00000000,
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_HCOUNT_MODE_ENABLE_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_HCOUNT_MODE_ENABLE;

/*
 * CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_JITTER_FILTERING_ENABLE enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_JITTER_FILTERING_ENABLE {
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_JITTER_FILTERING_ENABLE_FALSE  = 0x00000000,
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_JITTER_FILTERING_ENABLE_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_JITTER_FILTERING_ENABLE;

/*
 * CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_JITTER_FILTERING_WINDOW enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_JITTER_FILTERING_WINDOW {
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_JITTER_FILTERING_WINDOW_1pixel  = 0x00000000,
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_JITTER_FILTERING_WINDOW_2pixel  = 0x00000001,
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_JITTER_FILTERING_WINDOW_3pixel  = 0x00000002,
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_JITTER_FILTERING_WINDOW_4pixel  = 0x00000003,
} CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_JITTER_FILTERING_WINDOW;

/*
 * CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_WINDOW_ENABLE enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_WINDOW_ENABLE {
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_WINDOW_ENABLE_FALSE  = 0x00000000,
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_WINDOW_ENABLE_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_WINDOW_ENABLE;

/*
 * CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_WINDOW_UPDATE enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_WINDOW_UPDATE {
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_WINDOW_UPDATE_FALSE = 0x00000000,
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_WINDOW_UPDATE_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_WINDOW_UPDATE;

/*
 * CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_VSYNC_POLARITY enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_VSYNC_POLARITY {
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_VSYNC_POLARITY_FALSE  = 0x00000000,
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_VSYNC_POLARITY_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_VSYNC_POLARITY;

/*
 * CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_HSYNC_POLARITY enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_HSYNC_POLARITY {
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_HSYNC_POLARITY_FALSE  = 0x00000000,
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_HSYNC_POLARITY_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_HSYNC_POLARITY;

/*
 * CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_INTERLACE_MODE enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_INTERLACE_MODE {
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_INTERLACE_MODE_FALSE  = 0x00000000,
CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_INTERLACE_MODE_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_CONTROL_CRTC_EXT_TIMING_SYNC_INTERLACE_MODE;

/*
 * CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_INT_ENABLE enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_INT_ENABLE {
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_INT_ENABLE_FALSE  = 0x00000000,
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_INT_ENABLE_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_INT_ENABLE;

/*
 * CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_CLEAR enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_CLEAR {
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_CLEAR_FALSE = 0x00000000,
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_CLEAR_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_CLEAR;

/*
 * CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_INT_TYPE enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_INT_TYPE {
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_INT_TYPE_FALSE  = 0x00000000,
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_INT_TYPE_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_INT_TYPE;

/*
 * CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_FRAME_COUNT enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_FRAME_COUNT {
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_FRAME_COUNT_1FRAME  = 0x00000000,
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_FRAME_COUNT_2FRAME  = 0x00000001,
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_FRAME_COUNT_4FRAME  = 0x00000002,
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_FRAME_COUNT_8FRAME  = 0x00000003,
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_FRAME_COUNT_16FRAME  = 0x00000004,
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_FRAME_COUNT_32FRAME  = 0x00000005,
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_FRAME_COUNT_64FRAME  = 0x00000006,
CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_FRAME_COUNT_128FRAME  = 0x00000007,
} CRTC_EXT_TIMING_SYNC_LOSS_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_LOSS_FRAME_COUNT;

/*
 * CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_INT_ENABLE enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_INT_ENABLE {
CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_INT_ENABLE_FALSE  = 0x00000000,
CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_INT_ENABLE_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_INT_ENABLE;

/*
 * CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_CLEAR enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_CLEAR {
CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_CLEAR_FALSE = 0x00000000,
CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_CLEAR_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_CLEAR;

/*
 * CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_INT_TYPE enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_INT_TYPE {
CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_INT_TYPE_FALSE  = 0x00000000,
CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_INT_TYPE_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_INT_TYPE;

/*
 * CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_INT_ENABLE enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_INT_ENABLE {
CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_INT_ENABLE_FALSE  = 0x00000000,
CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_INT_ENABLE_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_INT_ENABLE;

/*
 * CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_CLEAR enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_CLEAR {
CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_CLEAR_FALSE = 0x00000000,
CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_CLEAR_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_CLEAR;

/*
 * CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_INT_TYPE enum
 */

typedef enum CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_INT_TYPE {
CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_INT_TYPE_FALSE  = 0x00000000,
CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_INT_TYPE_TRUE  = 0x00000001,
} CRTC_EXT_TIMING_SYNC_SIGNAL_INTERRUPT_CONTROL_CRTC_EXT_TIMING_SYNC_SIGNAL_INT_TYPE;

/*
 * CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_ENABLE enum
 */

typedef enum CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_ENABLE {
CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_ENABLE_FALSE  = 0x00000000,
CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_ENABLE_TRUE  = 0x00000001,
} CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_ENABLE;

/*
 * CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_CLEAR enum
 */

typedef enum CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_CLEAR {
CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_CLEAR_FALSE = 0x00000000,
CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_CLEAR_TRUE  = 0x00000001,
} CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_CLEAR;

/*
 * CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_TYPE enum
 */

typedef enum CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_TYPE {
CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_TYPE_FALSE  = 0x00000000,
CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_TYPE_TRUE  = 0x00000001,
} CRTC_STATIC_SCREEN_CONTROL_CRTC_CPU_SS_INT_TYPE;

/*
 * CRTC_STATIC_SCREEN_CONTROL_CRTC_STATIC_SCREEN_OVERRIDE enum
 */

typedef enum CRTC_STATIC_SCREEN_CONTROL_CRTC_STATIC_SCREEN_OVERRIDE {
CRTC_STATIC_SCREEN_CONTROL_CRTC_STATIC_SCREEN_OVERRIDE_FALSE  = 0x00000000,
CRTC_STATIC_SCREEN_CONTROL_CRTC_STATIC_SCREEN_OVERRIDE_TRUE  = 0x00000001,
} CRTC_STATIC_SCREEN_CONTROL_CRTC_STATIC_SCREEN_OVERRIDE;

/*
 * CRTC_STATIC_SCREEN_CONTROL_CRTC_STATIC_SCREEN_OVERRIDE_VALUE enum
 */

typedef enum CRTC_STATIC_SCREEN_CONTROL_CRTC_STATIC_SCREEN_OVERRIDE_VALUE {
CRTC_STATIC_SCREEN_CONTROL_CRTC_STATIC_SCREEN_OVERRIDE_VALUE_OFF  = 0x00000000,
CRTC_STATIC_SCREEN_CONTROL_CRTC_STATIC_SCREEN_OVERRIDE_VALUE_ON  = 0x00000001,
} CRTC_STATIC_SCREEN_CONTROL_CRTC_STATIC_SCREEN_OVERRIDE_VALUE;

/*
 * CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_EN enum
 */

typedef enum CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_EN {
CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_EN_FALSE  = 0x00000000,
CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_EN_TRUE  = 0x00000001,
} CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_EN;

/*
 * CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_EN_DB enum
 */

typedef enum CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_EN_DB {
CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_EN_DB_FALSE  = 0x00000000,
CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_EN_DB_TRUE  = 0x00000001,
} CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_EN_DB;

/*
 * CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_V_UPDATE_MODE enum
 */

typedef enum CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_V_UPDATE_MODE {
CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_V_UPDATE_MODE_BLOCK_BOTH  = 0x00000000,
CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_V_UPDATE_MODE_BLOCK_INTERLACE  = 0x00000001,
CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_V_UPDATE_MODE_BLOCK_PROGRASSIVE  = 0x00000002,
CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_V_UPDATE_MODE_RESERVED  = 0x00000003,
} CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_V_UPDATE_MODE;

/*
 * CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_STEREO_SEL_OVR enum
 */

typedef enum CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_STEREO_SEL_OVR {
CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_STEREO_SEL_OVR_FALSE  = 0x00000000,
CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_STEREO_SEL_OVR_TRUE  = 0x00000001,
} CRTC_3D_STRUCTURE_CONTROL_CRTC_3D_STRUCTURE_STEREO_SEL_OVR;

/*
 * CRTC_V_SYNC_A_POL enum
 */

typedef enum CRTC_V_SYNC_A_POL {
CRTC_V_SYNC_A_POL_HIGH                   = 0x00000000,
CRTC_V_SYNC_A_POL_LOW                    = 0x00000001,
} CRTC_V_SYNC_A_POL;

/*
 * CRTC_H_SYNC_A_POL enum
 */

typedef enum CRTC_H_SYNC_A_POL {
CRTC_H_SYNC_A_POL_HIGH                   = 0x00000000,
CRTC_H_SYNC_A_POL_LOW                    = 0x00000001,
} CRTC_H_SYNC_A_POL;

/*
 * CRTC_HORZ_REPETITION_COUNT enum
 */

typedef enum CRTC_HORZ_REPETITION_COUNT {
CRTC_HORZ_REPETITION_COUNT_0             = 0x00000000,
CRTC_HORZ_REPETITION_COUNT_1             = 0x00000001,
CRTC_HORZ_REPETITION_COUNT_2             = 0x00000002,
CRTC_HORZ_REPETITION_COUNT_3             = 0x00000003,
CRTC_HORZ_REPETITION_COUNT_4             = 0x00000004,
CRTC_HORZ_REPETITION_COUNT_5             = 0x00000005,
CRTC_HORZ_REPETITION_COUNT_6             = 0x00000006,
CRTC_HORZ_REPETITION_COUNT_7             = 0x00000007,
CRTC_HORZ_REPETITION_COUNT_8             = 0x00000008,
CRTC_HORZ_REPETITION_COUNT_9             = 0x00000009,
CRTC_HORZ_REPETITION_COUNT_10            = 0x0000000a,
CRTC_HORZ_REPETITION_COUNT_11            = 0x0000000b,
CRTC_HORZ_REPETITION_COUNT_12            = 0x0000000c,
CRTC_HORZ_REPETITION_COUNT_13            = 0x0000000d,
CRTC_HORZ_REPETITION_COUNT_14            = 0x0000000e,
CRTC_HORZ_REPETITION_COUNT_15            = 0x0000000f,
} CRTC_HORZ_REPETITION_COUNT;

/*
 * CRTC_DRR_MODE_DBUF_UPDATE_MODE enum
 */

typedef enum CRTC_DRR_MODE_DBUF_UPDATE_MODE {
CRTC_DRR_MODE_DBUF_UPDATE_MODE_00_IMMEDIATE  = 0x00000000,
CRTC_DRR_MODE_DBUF_UPDATE_MODE_01_MANUAL  = 0x00000001,
CRTC_DRR_MODE_DBUF_UPDATE_MODE_10_DBUF   = 0x00000002,
CRTC_DRR_MODE_DBUF_UPDATE_MODE_11_SYNCED_DBUF  = 0x00000003,
} CRTC_DRR_MODE_DBUF_UPDATE_MODE;

/*******************************************************
 * FMT Enums
 *******************************************************/

/*
 * FMT_CONTROL_PIXEL_ENCODING enum
 */

typedef enum FMT_CONTROL_PIXEL_ENCODING {
FMT_CONTROL_PIXEL_ENCODING_RGB444_OR_YCBCR444  = 0x00000000,
FMT_CONTROL_PIXEL_ENCODING_YCBCR422      = 0x00000001,
FMT_CONTROL_PIXEL_ENCODING_YCBCR420      = 0x00000002,
FMT_CONTROL_PIXEL_ENCODING_RESERVED      = 0x00000003,
} FMT_CONTROL_PIXEL_ENCODING;

/*
 * FMT_CONTROL_SUBSAMPLING_MODE enum
 */

typedef enum FMT_CONTROL_SUBSAMPLING_MODE {
FMT_CONTROL_SUBSAMPLING_MODE_DROP        = 0x00000000,
FMT_CONTROL_SUBSAMPLING_MODE_AVERAGE     = 0x00000001,
FMT_CONTROL_SUBSAMPLING_MOME_3_TAP       = 0x00000002,
FMT_CONTROL_SUBSAMPLING_MOME_RESERVED    = 0x00000003,
} FMT_CONTROL_SUBSAMPLING_MODE;

/*
 * FMT_CONTROL_SUBSAMPLING_ORDER enum
 */

typedef enum FMT_CONTROL_SUBSAMPLING_ORDER {
FMT_CONTROL_SUBSAMPLING_ORDER_CB_BEFORE_CR  = 0x00000000,
FMT_CONTROL_SUBSAMPLING_ORDER_CR_BEFORE_CB  = 0x00000001,
} FMT_CONTROL_SUBSAMPLING_ORDER;

/*
 * FMT_CONTROL_CBCR_BIT_REDUCTION_BYPASS enum
 */

typedef enum FMT_CONTROL_CBCR_BIT_REDUCTION_BYPASS {
FMT_CONTROL_CBCR_BIT_REDUCTION_BYPASS_DISABLE  = 0x00000000,
FMT_CONTROL_CBCR_BIT_REDUCTION_BYPASS_ENABLE  = 0x00000001,
} FMT_CONTROL_CBCR_BIT_REDUCTION_BYPASS;

/*
 * FMT_BIT_DEPTH_CONTROL_TRUNCATE_MODE enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_TRUNCATE_MODE {
FMT_BIT_DEPTH_CONTROL_TRUNCATE_MODE_TRUNCATION  = 0x00000000,
FMT_BIT_DEPTH_CONTROL_TRUNCATE_MODE_ROUNDING  = 0x00000001,
} FMT_BIT_DEPTH_CONTROL_TRUNCATE_MODE;

/*
 * FMT_BIT_DEPTH_CONTROL_TRUNCATE_DEPTH enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_TRUNCATE_DEPTH {
FMT_BIT_DEPTH_CONTROL_TRUNCATE_DEPTH_18BPP  = 0x00000000,
FMT_BIT_DEPTH_CONTROL_TRUNCATE_DEPTH_24BPP  = 0x00000001,
FMT_BIT_DEPTH_CONTROL_TRUNCATE_DEPTH_30BPP  = 0x00000002,
} FMT_BIT_DEPTH_CONTROL_TRUNCATE_DEPTH;

/*
 * FMT_BIT_DEPTH_CONTROL_SPATIAL_DITHER_DEPTH enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_SPATIAL_DITHER_DEPTH {
FMT_BIT_DEPTH_CONTROL_SPATIAL_DITHER_DEPTH_18BPP  = 0x00000000,
FMT_BIT_DEPTH_CONTROL_SPATIAL_DITHER_DEPTH_24BPP  = 0x00000001,
FMT_BIT_DEPTH_CONTROL_SPATIAL_DITHER_DEPTH_30BPP  = 0x00000002,
} FMT_BIT_DEPTH_CONTROL_SPATIAL_DITHER_DEPTH;

/*
 * FMT_BIT_DEPTH_CONTROL_TEMPORAL_DITHER_DEPTH enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_TEMPORAL_DITHER_DEPTH {
FMT_BIT_DEPTH_CONTROL_TEMPORAL_DITHER_DEPTH_18BPP  = 0x00000000,
FMT_BIT_DEPTH_CONTROL_TEMPORAL_DITHER_DEPTH_24BPP  = 0x00000001,
FMT_BIT_DEPTH_CONTROL_TEMPORAL_DITHER_DEPTH_30BPP  = 0x00000002,
} FMT_BIT_DEPTH_CONTROL_TEMPORAL_DITHER_DEPTH;

/*
 * FMT_BIT_DEPTH_CONTROL_TEMPORAL_LEVEL enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_TEMPORAL_LEVEL {
FMT_BIT_DEPTH_CONTROL_TEMPORAL_LEVEL_GREY_LEVEL2  = 0x00000000,
FMT_BIT_DEPTH_CONTROL_TEMPORAL_LEVEL_GREY_LEVEL4  = 0x00000001,
} FMT_BIT_DEPTH_CONTROL_TEMPORAL_LEVEL;

/*
 * FMT_BIT_DEPTH_CONTROL_25FRC_SEL enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_25FRC_SEL {
FMT_BIT_DEPTH_CONTROL_25FRC_SEL_Ei       = 0x00000000,
FMT_BIT_DEPTH_CONTROL_25FRC_SEL_Fi       = 0x00000001,
FMT_BIT_DEPTH_CONTROL_25FRC_SEL_Gi       = 0x00000002,
FMT_BIT_DEPTH_CONTROL_25FRC_SEL_RESERVED  = 0x00000003,
} FMT_BIT_DEPTH_CONTROL_25FRC_SEL;

/*
 * FMT_BIT_DEPTH_CONTROL_50FRC_SEL enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_50FRC_SEL {
FMT_BIT_DEPTH_CONTROL_50FRC_SEL_A        = 0x00000000,
FMT_BIT_DEPTH_CONTROL_50FRC_SEL_B        = 0x00000001,
FMT_BIT_DEPTH_CONTROL_50FRC_SEL_C        = 0x00000002,
FMT_BIT_DEPTH_CONTROL_50FRC_SEL_D        = 0x00000003,
} FMT_BIT_DEPTH_CONTROL_50FRC_SEL;

/*
 * FMT_BIT_DEPTH_CONTROL_75FRC_SEL enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_75FRC_SEL {
FMT_BIT_DEPTH_CONTROL_75FRC_SEL_E        = 0x00000000,
FMT_BIT_DEPTH_CONTROL_75FRC_SEL_F        = 0x00000001,
FMT_BIT_DEPTH_CONTROL_75FRC_SEL_G        = 0x00000002,
FMT_BIT_DEPTH_CONTROL_75FRC_SEL_RESERVED  = 0x00000003,
} FMT_BIT_DEPTH_CONTROL_75FRC_SEL;

/*
 * FMT_TEMPORAL_DITHER_PATTERN_CONTROL_SELECT enum
 */

typedef enum FMT_TEMPORAL_DITHER_PATTERN_CONTROL_SELECT {
FMT_TEMPORAL_DITHER_PATTERN_CONTROL_SELECT_LEGACY_HARDCODED_PATTERN  = 0x00000000,
FMT_TEMPORAL_DITHER_PATTERN_CONTROL_SELECT_PROGRAMMABLE_PATTERN  = 0x00000001,
} FMT_TEMPORAL_DITHER_PATTERN_CONTROL_SELECT;

/*
 * FMT_TEMPORAL_DITHER_PATTERN_CONTROL_RGB1_BGR0 enum
 */

typedef enum FMT_TEMPORAL_DITHER_PATTERN_CONTROL_RGB1_BGR0 {
FMT_TEMPORAL_DITHER_PATTERN_CONTROL_RGB1_BGR0_BGR  = 0x00000000,
FMT_TEMPORAL_DITHER_PATTERN_CONTROL_RGB1_BGR0_RGB  = 0x00000001,
} FMT_TEMPORAL_DITHER_PATTERN_CONTROL_RGB1_BGR0;

/*
 * FMT_CLAMP_CNTL_COLOR_FORMAT enum
 */

typedef enum FMT_CLAMP_CNTL_COLOR_FORMAT {
FMT_CLAMP_CNTL_COLOR_FORMAT_6BPC         = 0x00000000,
FMT_CLAMP_CNTL_COLOR_FORMAT_8BPC         = 0x00000001,
FMT_CLAMP_CNTL_COLOR_FORMAT_10BPC        = 0x00000002,
FMT_CLAMP_CNTL_COLOR_FORMAT_12BPC        = 0x00000003,
FMT_CLAMP_CNTL_COLOR_FORMAT_RESERVED1    = 0x00000004,
FMT_CLAMP_CNTL_COLOR_FORMAT_RESERVED2    = 0x00000005,
FMT_CLAMP_CNTL_COLOR_FORMAT_RESERVED3    = 0x00000006,
FMT_CLAMP_CNTL_COLOR_FORMAT_PROGRAMMABLE  = 0x00000007,
} FMT_CLAMP_CNTL_COLOR_FORMAT;

/*
 * FMT_CRC_CNTL_CONT_EN enum
 */

typedef enum FMT_CRC_CNTL_CONT_EN {
FMT_CRC_CNTL_CONT_EN_ONE_SHOT            = 0x00000000,
FMT_CRC_CNTL_CONT_EN_CONT                = 0x00000001,
} FMT_CRC_CNTL_CONT_EN;

/*
 * FMT_CRC_CNTL_INCLUDE_OVERSCAN enum
 */

typedef enum FMT_CRC_CNTL_INCLUDE_OVERSCAN {
FMT_CRC_CNTL_INCLUDE_OVERSCAN_NOT_INCLUDE  = 0x00000000,
FMT_CRC_CNTL_INCLUDE_OVERSCAN_INCLUDE    = 0x00000001,
} FMT_CRC_CNTL_INCLUDE_OVERSCAN;

/*
 * FMT_CRC_CNTL_ONLY_BLANKB enum
 */

typedef enum FMT_CRC_CNTL_ONLY_BLANKB {
FMT_CRC_CNTL_ONLY_BLANKB_ENTIRE_FIELD    = 0x00000000,
FMT_CRC_CNTL_ONLY_BLANKB_NON_BLANK       = 0x00000001,
} FMT_CRC_CNTL_ONLY_BLANKB;

/*
 * FMT_CRC_CNTL_PSR_MODE_ENABLE enum
 */

typedef enum FMT_CRC_CNTL_PSR_MODE_ENABLE {
FMT_CRC_CNTL_PSR_MODE_ENABLE_NORMAL      = 0x00000000,
FMT_CRC_CNTL_PSR_MODE_ENABLE_EDP_PSR_CRC  = 0x00000001,
} FMT_CRC_CNTL_PSR_MODE_ENABLE;

/*
 * FMT_CRC_CNTL_INTERLACE_MODE enum
 */

typedef enum FMT_CRC_CNTL_INTERLACE_MODE {
FMT_CRC_CNTL_INTERLACE_MODE_TOP          = 0x00000000,
FMT_CRC_CNTL_INTERLACE_MODE_BOTTOM       = 0x00000001,
FMT_CRC_CNTL_INTERLACE_MODE_BOTH_BOTTOM  = 0x00000002,
FMT_CRC_CNTL_INTERLACE_MODE_BOTH_EACH    = 0x00000003,
} FMT_CRC_CNTL_INTERLACE_MODE;

/*
 * FMT_CRC_CNTL_EVEN_ODD_PIX_ENABLE enum
 */

typedef enum FMT_CRC_CNTL_EVEN_ODD_PIX_ENABLE {
FMT_CRC_CNTL_EVEN_ODD_PIX_ENABLE_ALL     = 0x00000000,
FMT_CRC_CNTL_EVEN_ODD_PIX_ENABLE_ODD_EVEN  = 0x00000001,
} FMT_CRC_CNTL_EVEN_ODD_PIX_ENABLE;

/*
 * FMT_CRC_CNTL_EVEN_ODD_PIX_SELECT enum
 */

typedef enum FMT_CRC_CNTL_EVEN_ODD_PIX_SELECT {
FMT_CRC_CNTL_EVEN_ODD_PIX_SELECT_EVEN    = 0x00000000,
FMT_CRC_CNTL_EVEN_ODD_PIX_SELECT_ODD     = 0x00000001,
} FMT_CRC_CNTL_EVEN_ODD_PIX_SELECT;

/*
 * FMT_DEBUG_CNTL_COLOR_SELECT enum
 */

typedef enum FMT_DEBUG_CNTL_COLOR_SELECT {
FMT_DEBUG_CNTL_COLOR_SELECT_BLUE         = 0x00000000,
FMT_DEBUG_CNTL_COLOR_SELECT_GREEN        = 0x00000001,
FMT_DEBUG_CNTL_COLOR_SELECT_RED1         = 0x00000002,
FMT_DEBUG_CNTL_COLOR_SELECT_RED2         = 0x00000003,
} FMT_DEBUG_CNTL_COLOR_SELECT;

/*
 * FMT_SPATIAL_DITHER_MODE enum
 */

typedef enum FMT_SPATIAL_DITHER_MODE {
FMT_SPATIAL_DITHER_MODE_0                = 0x00000000,
FMT_SPATIAL_DITHER_MODE_1                = 0x00000001,
FMT_SPATIAL_DITHER_MODE_2                = 0x00000002,
FMT_SPATIAL_DITHER_MODE_3                = 0x00000003,
} FMT_SPATIAL_DITHER_MODE;

/*
 * FMT_STEREOSYNC_OVR_POL enum
 */

typedef enum FMT_STEREOSYNC_OVR_POL {
FMT_STEREOSYNC_OVR_POL_INVERTED          = 0x00000000,
FMT_STEREOSYNC_OVR_POL_NOT_INVERTED      = 0x00000001,
} FMT_STEREOSYNC_OVR_POL;

/*
 * FMT_DYNAMIC_EXP_MODE enum
 */

typedef enum FMT_DYNAMIC_EXP_MODE {
FMT_DYNAMIC_EXP_MODE_10to12              = 0x00000000,
FMT_DYNAMIC_EXP_MODE_8to12               = 0x00000001,
} FMT_DYNAMIC_EXP_MODE;

/*******************************************************
 * HPD Enums
 *******************************************************/

/*
 * HPD_INT_CONTROL_ACK enum
 */

typedef enum HPD_INT_CONTROL_ACK {
HPD_INT_CONTROL_ACK_0                    = 0x00000000,
HPD_INT_CONTROL_ACK_1                    = 0x00000001,
} HPD_INT_CONTROL_ACK;

/*
 * HPD_INT_CONTROL_POLARITY enum
 */

typedef enum HPD_INT_CONTROL_POLARITY {
HPD_INT_CONTROL_GEN_INT_ON_DISCON        = 0x00000000,
HPD_INT_CONTROL_GEN_INT_ON_CON           = 0x00000001,
} HPD_INT_CONTROL_POLARITY;

/*
 * HPD_INT_CONTROL_RX_INT_ACK enum
 */

typedef enum HPD_INT_CONTROL_RX_INT_ACK {
HPD_INT_CONTROL_RX_INT_ACK_0             = 0x00000000,
HPD_INT_CONTROL_RX_INT_ACK_1             = 0x00000001,
} HPD_INT_CONTROL_RX_INT_ACK;

/*******************************************************
 * LB Enums
 *******************************************************/

/*
 * LB_DATA_FORMAT_PIXEL_DEPTH enum
 */

typedef enum LB_DATA_FORMAT_PIXEL_DEPTH {
LB_DATA_FORMAT_PIXEL_DEPTH_30BPP         = 0x00000000,
LB_DATA_FORMAT_PIXEL_DEPTH_24BPP         = 0x00000001,
LB_DATA_FORMAT_PIXEL_DEPTH_18BPP         = 0x00000002,
LB_DATA_FORMAT_PIXEL_DEPTH_36BPP         = 0x00000003,
} LB_DATA_FORMAT_PIXEL_DEPTH;

/*
 * LB_DATA_FORMAT_PIXEL_EXPAN_MODE enum
 */

typedef enum LB_DATA_FORMAT_PIXEL_EXPAN_MODE {
LB_DATA_FORMAT_PIXEL_EXPAN_MODE_ZERO_PIXEL_EXPANSION = 0x00000000,
LB_DATA_FORMAT_PIXEL_EXPAN_MODE_DYNAMIC_PIXEL_EXPANSION = 0x00000001,
} LB_DATA_FORMAT_PIXEL_EXPAN_MODE;

/*
 * LB_DATA_FORMAT_PIXEL_REDUCE_MODE enum
 */

typedef enum LB_DATA_FORMAT_PIXEL_REDUCE_MODE {
LB_DATA_FORMAT_PIXEL_REDUCE_MODE_TRUNCATION = 0x00000000,
LB_DATA_FORMAT_PIXEL_REDUCE_MODE_ROUNDING = 0x00000001,
} LB_DATA_FORMAT_PIXEL_REDUCE_MODE;

/*
 * LB_DATA_FORMAT_DYNAMIC_PIXEL_DEPTH enum
 */

typedef enum LB_DATA_FORMAT_DYNAMIC_PIXEL_DEPTH {
LB_DATA_FORMAT_DYNAMIC_PIXEL_DEPTH_36BPP = 0x00000000,
LB_DATA_FORMAT_DYNAMIC_PIXEL_DEPTH_30BPP = 0x00000001,
} LB_DATA_FORMAT_DYNAMIC_PIXEL_DEPTH;

/*
 * LB_DATA_FORMAT_INTERLEAVE_EN enum
 */

typedef enum LB_DATA_FORMAT_INTERLEAVE_EN {
LB_DATA_FORMAT_INTERLEAVE_DISABLE        = 0x00000000,
LB_DATA_FORMAT_INTERLEAVE_ENABLE         = 0x00000001,
} LB_DATA_FORMAT_INTERLEAVE_EN;

/*
 * LB_DATA_FORMAT_REQUEST_MODE enum
 */

typedef enum LB_DATA_FORMAT_REQUEST_MODE {
LB_DATA_FORMAT_REQUEST_MODE_NORMAL       = 0x00000000,
LB_DATA_FORMAT_REQUEST_MODE_START_OF_LINE  = 0x00000001,
} LB_DATA_FORMAT_REQUEST_MODE;

/*
 * LB_DATA_FORMAT_ALPHA_EN enum
 */

typedef enum LB_DATA_FORMAT_ALPHA_EN {
LB_DATA_FORMAT_ALPHA_DISABLE             = 0x00000000,
LB_DATA_FORMAT_ALPHA_ENABLE              = 0x00000001,
} LB_DATA_FORMAT_ALPHA_EN;

/*
 * LB_VLINE_START_END_VLINE_INV enum
 */

typedef enum LB_VLINE_START_END_VLINE_INV {
LB_VLINE_START_END_VLINE_NORMAL          = 0x00000000,
LB_VLINE_START_END_VLINE_INVERSE         = 0x00000001,
} LB_VLINE_START_END_VLINE_INV;

/*
 * LB_VLINE2_START_END_VLINE2_INV enum
 */

typedef enum LB_VLINE2_START_END_VLINE2_INV {
LB_VLINE2_START_END_VLINE2_NORMAL        = 0x00000000,
LB_VLINE2_START_END_VLINE2_INVERSE       = 0x00000001,
} LB_VLINE2_START_END_VLINE2_INV;

/*
 * LB_INTERRUPT_MASK_VBLANK_INTERRUPT_MASK enum
 */

typedef enum LB_INTERRUPT_MASK_VBLANK_INTERRUPT_MASK {
LB_INTERRUPT_MASK_VBLANK_INTERRUPT_DISABLE = 0x00000000,
LB_INTERRUPT_MASK_VBLANK_INTERRUPT_ENABLE = 0x00000001,
} LB_INTERRUPT_MASK_VBLANK_INTERRUPT_MASK;

/*
 * LB_INTERRUPT_MASK_VLINE_INTERRUPT_MASK enum
 */

typedef enum LB_INTERRUPT_MASK_VLINE_INTERRUPT_MASK {
LB_INTERRUPT_MASK_VLINE_INTERRUPT_DISABLE = 0x00000000,
LB_INTERRUPT_MASK_VLINE_INTERRUPT_ENABLE = 0x00000001,
} LB_INTERRUPT_MASK_VLINE_INTERRUPT_MASK;

/*
 * LB_INTERRUPT_MASK_VLINE2_INTERRUPT_MASK enum
 */

typedef enum LB_INTERRUPT_MASK_VLINE2_INTERRUPT_MASK {
LB_INTERRUPT_MASK_VLINE2_INTERRUPT_DISABLE = 0x00000000,
LB_INTERRUPT_MASK_VLINE2_INTERRUPT_ENABLE = 0x00000001,
} LB_INTERRUPT_MASK_VLINE2_INTERRUPT_MASK;

/*
 * LB_VLINE_STATUS_VLINE_ACK enum
 */

typedef enum LB_VLINE_STATUS_VLINE_ACK {
LB_VLINE_STATUS_VLINE_NORMAL             = 0x00000000,
LB_VLINE_STATUS_VLINE_CLEAR              = 0x00000001,
} LB_VLINE_STATUS_VLINE_ACK;

/*
 * LB_VLINE_STATUS_VLINE_INTERRUPT_TYPE enum
 */

typedef enum LB_VLINE_STATUS_VLINE_INTERRUPT_TYPE {
LB_VLINE_STATUS_VLINE_INTERRUPT_TYPE_LEVEL_BASED  = 0x00000000,
LB_VLINE_STATUS_VLINE_INTERRUPT_TYPE_PULSE_BASED  = 0x00000001,
} LB_VLINE_STATUS_VLINE_INTERRUPT_TYPE;

/*
 * LB_VLINE2_STATUS_VLINE2_ACK enum
 */

typedef enum LB_VLINE2_STATUS_VLINE2_ACK {
LB_VLINE2_STATUS_VLINE2_NORMAL           = 0x00000000,
LB_VLINE2_STATUS_VLINE2_CLEAR            = 0x00000001,
} LB_VLINE2_STATUS_VLINE2_ACK;

/*
 * LB_VLINE2_STATUS_VLINE2_INTERRUPT_TYPE enum
 */

typedef enum LB_VLINE2_STATUS_VLINE2_INTERRUPT_TYPE {
LB_VLINE2_STATUS_VLINE2_INTERRUPT_TYPE_LEVEL_BASED  = 0x00000000,
LB_VLINE2_STATUS_VLINE2_INTERRUPT_TYPE_PULSE_BASED  = 0x00000001,
} LB_VLINE2_STATUS_VLINE2_INTERRUPT_TYPE;

/*
 * LB_VBLANK_STATUS_VBLANK_ACK enum
 */

typedef enum LB_VBLANK_STATUS_VBLANK_ACK {
LB_VBLANK_STATUS_VBLANK_NORMAL           = 0x00000000,
LB_VBLANK_STATUS_VBLANK_CLEAR            = 0x00000001,
} LB_VBLANK_STATUS_VBLANK_ACK;

/*
 * LB_VBLANK_STATUS_VBLANK_INTERRUPT_TYPE enum
 */

typedef enum LB_VBLANK_STATUS_VBLANK_INTERRUPT_TYPE {
LB_VBLANK_STATUS_VBLANK_INTERRUPT_TYPE_LEVEL_BASED  = 0x00000000,
LB_VBLANK_STATUS_VBLANK_INTERRUPT_TYPE_PULSE_BASED  = 0x00000001,
} LB_VBLANK_STATUS_VBLANK_INTERRUPT_TYPE;

/*
 * LB_SYNC_RESET_SEL_LB_SYNC_RESET_SEL enum
 */

typedef enum LB_SYNC_RESET_SEL_LB_SYNC_RESET_SEL {
LB_SYNC_RESET_SEL_LB_SYNC_RESET_SEL_DISABLE  = 0x00000000,
LB_SYNC_RESET_SEL_LB_SYNC_RESET_SEL_FROM_VSYNC_VBLANK  = 0x00000001,
LB_SYNC_RESET_SEL_LB_SYNC_RESET_SEL_FROM_POWERDOWN_RESET  = 0x00000002,
LB_SYNC_RESET_SEL_LB_SYNC_RESET_SEL_FROM_VSYNC_VBLANK_POWERDOWN_RESET  = 0x00000003,
} LB_SYNC_RESET_SEL_LB_SYNC_RESET_SEL;

/*
 * LB_SYNC_RESET_SEL_LB_SYNC_RESET_SEL2 enum
 */

typedef enum LB_SYNC_RESET_SEL_LB_SYNC_RESET_SEL2 {
LB_SYNC_RESET_SEL_LB_SYNC_RESET_SEL2_USE_VBLANK  = 0x00000000,
LB_SYNC_RESET_SEL_LB_SYNC_RESET_SEL2_USE_VSYNC  = 0x00000001,
} LB_SYNC_RESET_SEL_LB_SYNC_RESET_SEL2;

/*
 * LB_SYNC_RESET_SEL_LB_SYNC_DURATION enum
 */

typedef enum LB_SYNC_RESET_SEL_LB_SYNC_DURATION {
LB_SYNC_RESET_SEL_LB_SYNC_DURATION_16_CLOCKS = 0x00000000,
LB_SYNC_RESET_SEL_LB_SYNC_DURATION_32_CLOCKS = 0x00000001,
LB_SYNC_RESET_SEL_LB_SYNC_DURATION_64_CLOCKS = 0x00000002,
LB_SYNC_RESET_SEL_LB_SYNC_DURATION_128_CLOCKS = 0x00000003,
} LB_SYNC_RESET_SEL_LB_SYNC_DURATION;

/*
 * LB_KEYER_COLOR_CTRL_LB_KEYER_COLOR_EN enum
 */

typedef enum LB_KEYER_COLOR_CTRL_LB_KEYER_COLOR_EN {
LB_KEYER_COLOR_CTRL_LB_KEYER_COLOR_DISABLE = 0x00000000,
LB_KEYER_COLOR_CTRL_LB_KEYER_COLOR_ENABLE = 0x00000001,
} LB_KEYER_COLOR_CTRL_LB_KEYER_COLOR_EN;

/*
 * LB_KEYER_COLOR_CTRL_LB_KEYER_COLOR_REP_EN enum
 */

typedef enum LB_KEYER_COLOR_CTRL_LB_KEYER_COLOR_REP_EN {
LB_KEYER_COLOR_CTRL_LB_KEYER_COLOR_REPLACEMENT_DISABLE = 0x00000000,
LB_KEYER_COLOR_CTRL_LB_KEYER_COLOR_REPLACEMENT_ENABLE = 0x00000001,
} LB_KEYER_COLOR_CTRL_LB_KEYER_COLOR_REP_EN;

/*
 * LB_BUFFER_STATUS_LB_BUFFER_EMPTY_ACK enum
 */

typedef enum LB_BUFFER_STATUS_LB_BUFFER_EMPTY_ACK {
LB_BUFFER_STATUS_LB_BUFFER_EMPTY_NORMAL  = 0x00000000,
LB_BUFFER_STATUS_LB_BUFFER_EMPTY_RESET   = 0x00000001,
} LB_BUFFER_STATUS_LB_BUFFER_EMPTY_ACK;

/*
 * LB_BUFFER_STATUS_LB_BUFFER_FULL_ACK enum
 */

typedef enum LB_BUFFER_STATUS_LB_BUFFER_FULL_ACK {
LB_BUFFER_STATUS_LB_BUFFER_FULL_NORMAL   = 0x00000000,
LB_BUFFER_STATUS_LB_BUFFER_FULL_RESET    = 0x00000001,
} LB_BUFFER_STATUS_LB_BUFFER_FULL_ACK;

/*
 * LB_MVP_AFR_FLIP_MODE_MVP_AFR_FLIP_MODE enum
 */

typedef enum LB_MVP_AFR_FLIP_MODE_MVP_AFR_FLIP_MODE {
LB_MVP_AFR_FLIP_MODE_MVP_AFR_FLIP_MODE_REAL_FLIP  = 0x00000002,
LB_MVP_AFR_FLIP_MODE_MVP_AFR_FLIP_MODE_DUMMY_FLIP  = 0x00000003,
} LB_MVP_AFR_FLIP_MODE_MVP_AFR_FLIP_MODE;

/*
 * LB_MVP_AFR_FLIP_FIFO_CNTL_MVP_AFR_FLIP_FIFO_RESET enum
 */

typedef enum LB_MVP_AFR_FLIP_FIFO_CNTL_MVP_AFR_FLIP_FIFO_RESET {
LB_MVP_AFR_FLIP_FIFO_CNTL_MVP_AFR_FLIP_FIFO_NORMAL = 0x00000000,
LB_MVP_AFR_FLIP_FIFO_CNTL_MVP_AFR_FLIP_FIFO_RESET_ACTIVE  = 0x00000001,
} LB_MVP_AFR_FLIP_FIFO_CNTL_MVP_AFR_FLIP_FIFO_RESET;

/*
 * LB_MVP_AFR_FLIP_FIFO_CNTL_MVP_AFR_FLIP_FIFO_RESET_ACK enum
 */

typedef enum LB_MVP_AFR_FLIP_FIFO_CNTL_MVP_AFR_FLIP_FIFO_RESET_ACK {
LB_MVP_AFR_FLIP_FIFO_CNTL_MVP_AFR_FLIP_FIFO_RESET_ACK_NOT_USED0 = 0x00000000,
LB_MVP_AFR_FLIP_FIFO_CNTL_MVP_AFR_FLIP_FIFO_RESET_ACK_NOT_USED1 = 0x00000001,
} LB_MVP_AFR_FLIP_FIFO_CNTL_MVP_AFR_FLIP_FIFO_RESET_ACK;

/*
 * LB_MVP_FLIP_LINE_NUM_INSERT_MVP_FLIP_LINE_NUM_INSERT_MODE enum
 */

typedef enum LB_MVP_FLIP_LINE_NUM_INSERT_MVP_FLIP_LINE_NUM_INSERT_MODE {
LB_MVP_FLIP_LINE_NUM_INSERT_MVP_FLIP_LINE_NUM_INSERT_MODE_NO_INSERT  = 0x00000000,
LB_MVP_FLIP_LINE_NUM_INSERT_MVP_FLIP_LINE_NUM_INSERT_MODE_DEBUG  = 0x00000001,
LB_MVP_FLIP_LINE_NUM_INSERT_MVP_FLIP_LINE_NUM_INSERT_MODE_HSYNC_MODE  = 0x00000002,
} LB_MVP_FLIP_LINE_NUM_INSERT_MVP_FLIP_LINE_NUM_INSERT_MODE;

/*
 * LB_MVP_FLIP_LINE_NUM_INSERT_MVP_FLIP_AUTO_ENABLE enum
 */

typedef enum LB_MVP_FLIP_LINE_NUM_INSERT_MVP_FLIP_AUTO_ENABLE {
LB_MVP_FLIP_LINE_NUM_INSERT_MVP_FLIP_AUTO_DISABLE  = 0x00000000,
LB_MVP_FLIP_LINE_NUM_INSERT_MVP_FLIP_AUTO_EN  = 0x00000001,
} LB_MVP_FLIP_LINE_NUM_INSERT_MVP_FLIP_AUTO_ENABLE;

/*
 * LB_DC_MVP_LB_CONTROL_MVP_SWAP_LOCK_IN_MODE enum
 */

typedef enum LB_DC_MVP_LB_CONTROL_MVP_SWAP_LOCK_IN_MODE {
ALPHA_LB_DC_MVP_LB_CONTROL_MVP_SWAP_LOCK_IN_MODE_MASTER  = 0x00000001,
ALPHA_LB_DC_MVP_LB_CONTROL_MVP_SWAP_LOCK_IN_MODE_SLAVE  = 0x00000002,
} LB_DC_MVP_LB_CONTROL_MVP_SWAP_LOCK_IN_MODE;

/*
 * LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_SEL enum
 */

typedef enum LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_SEL {
LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_SEL_NOT_USED0 = 0x00000000,
LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_SEL_NOT_USED1 = 0x00000001,
} LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_SEL;

/*
 * LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_FORCE_ONE enum
 */

typedef enum LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_FORCE_ONE {
LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_NO_FORCE_ONE = 0x00000000,
LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_FORCE_TO_ONE  = 0x00000001,
} LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_FORCE_ONE;

/*
 * LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_FORCE_ZERO enum
 */

typedef enum LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_FORCE_ZERO {
LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_NO_FORCE_ZERO = 0x00000000,
LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_FORCE_TO_ZERO  = 0x00000001,
} LB_DC_MVP_LB_CONTROL_DC_MVP_SWAP_LOCK_OUT_FORCE_ZERO;

/*
 * LB_TEST_DEBUG_INDEX_LB_TEST_DEBUG_WRITE_EN enum
 */

typedef enum LB_TEST_DEBUG_INDEX_LB_TEST_DEBUG_WRITE_EN {
LB_TEST_DEBUG_INDEX_LB_TEST_DEBUG_WRITE_EN_NOT_USED0 = 0x00000000,
LB_TEST_DEBUG_INDEX_LB_TEST_DEBUG_WRITE_EN_NOT_USED1 = 0x00000001,
} LB_TEST_DEBUG_INDEX_LB_TEST_DEBUG_WRITE_EN;

/*******************************************************
 * DIG Enums
 *******************************************************/

/*
 * HDMI_KEEPOUT_MODE enum
 */

typedef enum HDMI_KEEPOUT_MODE {
HDMI_KEEPOUT_0_650PIX_AFTER_VSYNC        = 0x00000000,
HDMI_KEEPOUT_509_650PIX_AFTER_VSYNC      = 0x00000001,
} HDMI_KEEPOUT_MODE;

/*
 * HDMI_DATA_SCRAMBLE_EN enum
 */

typedef enum HDMI_DATA_SCRAMBLE_EN {
HDMI_DATA_SCRAMBLE_DISABLE               = 0x00000000,
HDMI_DATA_SCRAMBLE_ENABLE                = 0x00000001,
} HDMI_DATA_SCRAMBLE_EN;

/*
 * HDMI_CLOCK_CHANNEL_RATE enum
 */

typedef enum HDMI_CLOCK_CHANNEL_RATE {
HDMI_CLOCK_CHANNEL_FREQ_EQUAL_TO_CHAR_RATE  = 0x00000000,
HDMI_CLOCK_CHANNEL_FREQ_QUARTER_TO_CHAR_RATE  = 0x00000001,
} HDMI_CLOCK_CHANNEL_RATE;

/*
 * HDMI_NO_EXTRA_NULL_PACKET_FILLED enum
 */

typedef enum HDMI_NO_EXTRA_NULL_PACKET_FILLED {
HDMI_EXTRA_NULL_PACKET_FILLED_ENABLE     = 0x00000000,
HDMI_EXTRA_NULL_PACKET_FILLED_DISABLE    = 0x00000001,
} HDMI_NO_EXTRA_NULL_PACKET_FILLED;

/*
 * HDMI_PACKET_GEN_VERSION enum
 */

typedef enum HDMI_PACKET_GEN_VERSION {
HDMI_PACKET_GEN_VERSION_OLD              = 0x00000000,
HDMI_PACKET_GEN_VERSION_NEW              = 0x00000001,
} HDMI_PACKET_GEN_VERSION;

/*
 * HDMI_ERROR_ACK enum
 */

typedef enum HDMI_ERROR_ACK {
HDMI_ERROR_ACK_INT                       = 0x00000000,
HDMI_ERROR_NOT_ACK                       = 0x00000001,
} HDMI_ERROR_ACK;

/*
 * HDMI_ERROR_MASK enum
 */

typedef enum HDMI_ERROR_MASK {
HDMI_ERROR_MASK_INT                      = 0x00000000,
HDMI_ERROR_NOT_MASK                      = 0x00000001,
} HDMI_ERROR_MASK;

/*
 * HDMI_DEEP_COLOR_DEPTH enum
 */

typedef enum HDMI_DEEP_COLOR_DEPTH {
HDMI_DEEP_COLOR_DEPTH_24BPP              = 0x00000000,
HDMI_DEEP_COLOR_DEPTH_30BPP              = 0x00000001,
HDMI_DEEP_COLOR_DEPTH_36BPP              = 0x00000002,
HDMI_DEEP_COLOR_DEPTH_RESERVED           = 0x00000003,
} HDMI_DEEP_COLOR_DEPTH;

/*
 * HDMI_AUDIO_DELAY_EN enum
 */

typedef enum HDMI_AUDIO_DELAY_EN {
HDMI_AUDIO_DELAY_DISABLE                 = 0x00000000,
HDMI_AUDIO_DELAY_58CLK                   = 0x00000001,
HDMI_AUDIO_DELAY_56CLK                   = 0x00000002,
HDMI_AUDIO_DELAY_RESERVED                = 0x00000003,
} HDMI_AUDIO_DELAY_EN;

/*
 * HDMI_AUDIO_SEND_MAX_PACKETS enum
 */

typedef enum HDMI_AUDIO_SEND_MAX_PACKETS {
HDMI_NOT_SEND_MAX_AUDIO_PACKETS          = 0x00000000,
HDMI_SEND_MAX_AUDIO_PACKETS              = 0x00000001,
} HDMI_AUDIO_SEND_MAX_PACKETS;

/*
 * HDMI_ACR_SEND enum
 */

typedef enum HDMI_ACR_SEND {
HDMI_ACR_NOT_SEND                        = 0x00000000,
HDMI_ACR_PKT_SEND                        = 0x00000001,
} HDMI_ACR_SEND;

/*
 * HDMI_ACR_CONT enum
 */

typedef enum HDMI_ACR_CONT {
HDMI_ACR_CONT_DISABLE                    = 0x00000000,
HDMI_ACR_CONT_ENABLE                     = 0x00000001,
} HDMI_ACR_CONT;

/*
 * HDMI_ACR_SELECT enum
 */

typedef enum HDMI_ACR_SELECT {
HDMI_ACR_SELECT_HW                       = 0x00000000,
HDMI_ACR_SELECT_32K                      = 0x00000001,
HDMI_ACR_SELECT_44K                      = 0x00000002,
HDMI_ACR_SELECT_48K                      = 0x00000003,
} HDMI_ACR_SELECT;

/*
 * HDMI_ACR_SOURCE enum
 */

typedef enum HDMI_ACR_SOURCE {
HDMI_ACR_SOURCE_HW                       = 0x00000000,
HDMI_ACR_SOURCE_SW                       = 0x00000001,
} HDMI_ACR_SOURCE;

/*
 * HDMI_ACR_N_MULTIPLE enum
 */

typedef enum HDMI_ACR_N_MULTIPLE {
HDMI_ACR_0_MULTIPLE_RESERVED             = 0x00000000,
HDMI_ACR_1_MULTIPLE                      = 0x00000001,
HDMI_ACR_2_MULTIPLE                      = 0x00000002,
HDMI_ACR_3_MULTIPLE_RESERVED             = 0x00000003,
HDMI_ACR_4_MULTIPLE                      = 0x00000004,
HDMI_ACR_5_MULTIPLE_RESERVED             = 0x00000005,
HDMI_ACR_6_MULTIPLE_RESERVED             = 0x00000006,
HDMI_ACR_7_MULTIPLE_RESERVED             = 0x00000007,
} HDMI_ACR_N_MULTIPLE;

/*
 * HDMI_ACR_AUDIO_PRIORITY enum
 */

typedef enum HDMI_ACR_AUDIO_PRIORITY {
HDMI_ACR_PKT_HIGH_PRIORITY_THAN_AUDIO_SAMPLE  = 0x00000000,
HDMI_AUDIO_SAMPLE_HIGH_PRIORITY_THAN_ACR_PKT  = 0x00000001,
} HDMI_ACR_AUDIO_PRIORITY;

/*
 * HDMI_NULL_SEND enum
 */

typedef enum HDMI_NULL_SEND {
HDMI_NULL_NOT_SEND                       = 0x00000000,
HDMI_NULL_PKT_SEND                       = 0x00000001,
} HDMI_NULL_SEND;

/*
 * HDMI_GC_SEND enum
 */

typedef enum HDMI_GC_SEND {
HDMI_GC_NOT_SEND                         = 0x00000000,
HDMI_GC_PKT_SEND                         = 0x00000001,
} HDMI_GC_SEND;

/*
 * HDMI_GC_CONT enum
 */

typedef enum HDMI_GC_CONT {
HDMI_GC_CONT_DISABLE                     = 0x00000000,
HDMI_GC_CONT_ENABLE                      = 0x00000001,
} HDMI_GC_CONT;

/*
 * HDMI_ISRC_SEND enum
 */

typedef enum HDMI_ISRC_SEND {
HDMI_ISRC_NOT_SEND                       = 0x00000000,
HDMI_ISRC_PKT_SEND                       = 0x00000001,
} HDMI_ISRC_SEND;

/*
 * HDMI_ISRC_CONT enum
 */

typedef enum HDMI_ISRC_CONT {
HDMI_ISRC_CONT_DISABLE                   = 0x00000000,
HDMI_ISRC_CONT_ENABLE                    = 0x00000001,
} HDMI_ISRC_CONT;

/*
 * HDMI_AVI_INFO_SEND enum
 */

typedef enum HDMI_AVI_INFO_SEND {
HDMI_AVI_INFO_NOT_SEND                   = 0x00000000,
HDMI_AVI_INFO_PKT_SEND                   = 0x00000001,
} HDMI_AVI_INFO_SEND;

/*
 * HDMI_AVI_INFO_CONT enum
 */

typedef enum HDMI_AVI_INFO_CONT {
HDMI_AVI_INFO_CONT_DISABLE               = 0x00000000,
HDMI_AVI_INFO_CONT_ENABLE                = 0x00000001,
} HDMI_AVI_INFO_CONT;

/*
 * HDMI_AUDIO_INFO_SEND enum
 */

typedef enum HDMI_AUDIO_INFO_SEND {
HDMI_AUDIO_INFO_NOT_SEND                 = 0x00000000,
HDMI_AUDIO_INFO_PKT_SEND                 = 0x00000001,
} HDMI_AUDIO_INFO_SEND;

/*
 * HDMI_AUDIO_INFO_CONT enum
 */

typedef enum HDMI_AUDIO_INFO_CONT {
HDMI_AUDIO_INFO_CONT_DISABLE             = 0x00000000,
HDMI_AUDIO_INFO_CONT_ENABLE              = 0x00000001,
} HDMI_AUDIO_INFO_CONT;

/*
 * HDMI_MPEG_INFO_SEND enum
 */

typedef enum HDMI_MPEG_INFO_SEND {
HDMI_MPEG_INFO_NOT_SEND                  = 0x00000000,
HDMI_MPEG_INFO_PKT_SEND                  = 0x00000001,
} HDMI_MPEG_INFO_SEND;

/*
 * HDMI_MPEG_INFO_CONT enum
 */

typedef enum HDMI_MPEG_INFO_CONT {
HDMI_MPEG_INFO_CONT_DISABLE              = 0x00000000,
HDMI_MPEG_INFO_CONT_ENABLE               = 0x00000001,
} HDMI_MPEG_INFO_CONT;

/*
 * HDMI_GENERIC0_SEND enum
 */

typedef enum HDMI_GENERIC0_SEND {
HDMI_GENERIC0_NOT_SEND                   = 0x00000000,
HDMI_GENERIC0_PKT_SEND                   = 0x00000001,
} HDMI_GENERIC0_SEND;

/*
 * HDMI_GENERIC0_CONT enum
 */

typedef enum HDMI_GENERIC0_CONT {
HDMI_GENERIC0_CONT_DISABLE               = 0x00000000,
HDMI_GENERIC0_CONT_ENABLE                = 0x00000001,
} HDMI_GENERIC0_CONT;

/*
 * HDMI_GENERIC1_SEND enum
 */

typedef enum HDMI_GENERIC1_SEND {
HDMI_GENERIC1_NOT_SEND                   = 0x00000000,
HDMI_GENERIC1_PKT_SEND                   = 0x00000001,
} HDMI_GENERIC1_SEND;

/*
 * HDMI_GENERIC1_CONT enum
 */

typedef enum HDMI_GENERIC1_CONT {
HDMI_GENERIC1_CONT_DISABLE               = 0x00000000,
HDMI_GENERIC1_CONT_ENABLE                = 0x00000001,
} HDMI_GENERIC1_CONT;

/*
 * HDMI_GC_AVMUTE_CONT enum
 */

typedef enum HDMI_GC_AVMUTE_CONT {
HDMI_GC_AVMUTE_CONT_DISABLE              = 0x00000000,
HDMI_GC_AVMUTE_CONT_ENABLE               = 0x00000001,
} HDMI_GC_AVMUTE_CONT;

/*
 * HDMI_PACKING_PHASE_OVERRIDE enum
 */

typedef enum HDMI_PACKING_PHASE_OVERRIDE {
HDMI_PACKING_PHASE_SET_BY_HW             = 0x00000000,
HDMI_PACKING_PHASE_SET_BY_SW             = 0x00000001,
} HDMI_PACKING_PHASE_OVERRIDE;

/*
 * HDMI_GENERIC2_SEND enum
 */

typedef enum HDMI_GENERIC2_SEND {
HDMI_GENERIC2_NOT_SEND                   = 0x00000000,
HDMI_GENERIC2_PKT_SEND                   = 0x00000001,
} HDMI_GENERIC2_SEND;

/*
 * HDMI_GENERIC2_CONT enum
 */

typedef enum HDMI_GENERIC2_CONT {
HDMI_GENERIC2_CONT_DISABLE               = 0x00000000,
HDMI_GENERIC2_CONT_ENABLE                = 0x00000001,
} HDMI_GENERIC2_CONT;

/*
 * HDMI_GENERIC3_SEND enum
 */

typedef enum HDMI_GENERIC3_SEND {
HDMI_GENERIC3_NOT_SEND                   = 0x00000000,
HDMI_GENERIC3_PKT_SEND                   = 0x00000001,
} HDMI_GENERIC3_SEND;

/*
 * HDMI_GENERIC3_CONT enum
 */

typedef enum HDMI_GENERIC3_CONT {
HDMI_GENERIC3_CONT_DISABLE               = 0x00000000,
HDMI_GENERIC3_CONT_ENABLE                = 0x00000001,
} HDMI_GENERIC3_CONT;

/*
 * TMDS_PIXEL_ENCODING enum
 */

typedef enum TMDS_PIXEL_ENCODING {
TMDS_PIXEL_ENCODING_444_OR_420           = 0x00000000,
TMDS_PIXEL_ENCODING_422                  = 0x00000001,
} TMDS_PIXEL_ENCODING;

/*
 * TMDS_COLOR_FORMAT enum
 */

typedef enum TMDS_COLOR_FORMAT {
TMDS_COLOR_FORMAT__24BPP__TWIN30BPP_MSB__DUAL48BPP  = 0x00000000,
TMDS_COLOR_FORMAT_TWIN30BPP_LSB          = 0x00000001,
TMDS_COLOR_FORMAT_DUAL30BPP              = 0x00000002,
TMDS_COLOR_FORMAT_RESERVED               = 0x00000003,
} TMDS_COLOR_FORMAT;

/*
 * TMDS_STEREOSYNC_CTL_SEL_REG enum
 */

typedef enum TMDS_STEREOSYNC_CTL_SEL_REG {
TMDS_STEREOSYNC_CTL0                     = 0x00000000,
TMDS_STEREOSYNC_CTL1                     = 0x00000001,
TMDS_STEREOSYNC_CTL2                     = 0x00000002,
TMDS_STEREOSYNC_CTL3                     = 0x00000003,
} TMDS_STEREOSYNC_CTL_SEL_REG;

/*
 * TMDS_CTL0_DATA_SEL enum
 */

typedef enum TMDS_CTL0_DATA_SEL {
TMDS_CTL0_DATA_SEL0_RESERVED             = 0x00000000,
TMDS_CTL0_DATA_SEL1_DISPLAY_ENABLE       = 0x00000001,
TMDS_CTL0_DATA_SEL2_VSYNC                = 0x00000002,
TMDS_CTL0_DATA_SEL3_RESERVED             = 0x00000003,
TMDS_CTL0_DATA_SEL4_HSYNC                = 0x00000004,
TMDS_CTL0_DATA_SEL5_SEL7_RESERVED        = 0x00000005,
TMDS_CTL0_DATA_SEL8_RANDOM_DATA          = 0x00000006,
TMDS_CTL0_DATA_SEL9_SEL15_RANDOM_DATA    = 0x00000007,
} TMDS_CTL0_DATA_SEL;

/*
 * TMDS_CTL0_DATA_INVERT enum
 */

typedef enum TMDS_CTL0_DATA_INVERT {
TMDS_CTL0_DATA_NORMAL                    = 0x00000000,
TMDS_CTL0_DATA_INVERT_EN                 = 0x00000001,
} TMDS_CTL0_DATA_INVERT;

/*
 * TMDS_CTL0_DATA_MODULATION enum
 */

typedef enum TMDS_CTL0_DATA_MODULATION {
TMDS_CTL0_DATA_MODULATION_DISABLE        = 0x00000000,
TMDS_CTL0_DATA_MODULATION_BIT0           = 0x00000001,
TMDS_CTL0_DATA_MODULATION_BIT1           = 0x00000002,
TMDS_CTL0_DATA_MODULATION_BIT2           = 0x00000003,
} TMDS_CTL0_DATA_MODULATION;

/*
 * TMDS_CTL0_PATTERN_OUT_EN enum
 */

typedef enum TMDS_CTL0_PATTERN_OUT_EN {
TMDS_CTL0_PATTERN_OUT_DISABLE            = 0x00000000,
TMDS_CTL0_PATTERN_OUT_ENABLE             = 0x00000001,
} TMDS_CTL0_PATTERN_OUT_EN;

/*
 * TMDS_CTL1_DATA_SEL enum
 */

typedef enum TMDS_CTL1_DATA_SEL {
TMDS_CTL1_DATA_SEL0_RESERVED             = 0x00000000,
TMDS_CTL1_DATA_SEL1_DISPLAY_ENABLE       = 0x00000001,
TMDS_CTL1_DATA_SEL2_VSYNC                = 0x00000002,
TMDS_CTL1_DATA_SEL3_RESERVED             = 0x00000003,
TMDS_CTL1_DATA_SEL4_HSYNC                = 0x00000004,
TMDS_CTL1_DATA_SEL5_SEL7_RESERVED        = 0x00000005,
TMDS_CTL1_DATA_SEL8_BLANK_TIME           = 0x00000006,
TMDS_CTL1_DATA_SEL9_SEL15_RESERVED       = 0x00000007,
} TMDS_CTL1_DATA_SEL;

/*
 * TMDS_CTL1_DATA_INVERT enum
 */

typedef enum TMDS_CTL1_DATA_INVERT {
TMDS_CTL1_DATA_NORMAL                    = 0x00000000,
TMDS_CTL1_DATA_INVERT_EN                 = 0x00000001,
} TMDS_CTL1_DATA_INVERT;

/*
 * TMDS_CTL1_DATA_MODULATION enum
 */

typedef enum TMDS_CTL1_DATA_MODULATION {
TMDS_CTL1_DATA_MODULATION_DISABLE        = 0x00000000,
TMDS_CTL1_DATA_MODULATION_BIT0           = 0x00000001,
TMDS_CTL1_DATA_MODULATION_BIT1           = 0x00000002,
TMDS_CTL1_DATA_MODULATION_BIT2           = 0x00000003,
} TMDS_CTL1_DATA_MODULATION;

/*
 * TMDS_CTL1_PATTERN_OUT_EN enum
 */

typedef enum TMDS_CTL1_PATTERN_OUT_EN {
TMDS_CTL1_PATTERN_OUT_DISABLE            = 0x00000000,
TMDS_CTL1_PATTERN_OUT_ENABLE             = 0x00000001,
} TMDS_CTL1_PATTERN_OUT_EN;

/*
 * TMDS_CTL2_DATA_SEL enum
 */

typedef enum TMDS_CTL2_DATA_SEL {
TMDS_CTL2_DATA_SEL0_RESERVED             = 0x00000000,
TMDS_CTL2_DATA_SEL1_DISPLAY_ENABLE       = 0x00000001,
TMDS_CTL2_DATA_SEL2_VSYNC                = 0x00000002,
TMDS_CTL2_DATA_SEL3_RESERVED             = 0x00000003,
TMDS_CTL2_DATA_SEL4_HSYNC                = 0x00000004,
TMDS_CTL2_DATA_SEL5_SEL7_RESERVED        = 0x00000005,
TMDS_CTL2_DATA_SEL8_BLANK_TIME           = 0x00000006,
TMDS_CTL2_DATA_SEL9_SEL15_RESERVED       = 0x00000007,
} TMDS_CTL2_DATA_SEL;

/*
 * TMDS_CTL2_DATA_INVERT enum
 */

typedef enum TMDS_CTL2_DATA_INVERT {
TMDS_CTL2_DATA_NORMAL                    = 0x00000000,
TMDS_CTL2_DATA_INVERT_EN                 = 0x00000001,
} TMDS_CTL2_DATA_INVERT;

/*
 * TMDS_CTL2_DATA_MODULATION enum
 */

typedef enum TMDS_CTL2_DATA_MODULATION {
TMDS_CTL2_DATA_MODULATION_DISABLE        = 0x00000000,
TMDS_CTL2_DATA_MODULATION_BIT0           = 0x00000001,
TMDS_CTL2_DATA_MODULATION_BIT1           = 0x00000002,
TMDS_CTL2_DATA_MODULATION_BIT2           = 0x00000003,
} TMDS_CTL2_DATA_MODULATION;

/*
 * TMDS_CTL2_PATTERN_OUT_EN enum
 */

typedef enum TMDS_CTL2_PATTERN_OUT_EN {
TMDS_CTL2_PATTERN_OUT_DISABLE            = 0x00000000,
TMDS_CTL2_PATTERN_OUT_ENABLE             = 0x00000001,
} TMDS_CTL2_PATTERN_OUT_EN;

/*
 * TMDS_CTL3_DATA_INVERT enum
 */

typedef enum TMDS_CTL3_DATA_INVERT {
TMDS_CTL3_DATA_NORMAL                    = 0x00000000,
TMDS_CTL3_DATA_INVERT_EN                 = 0x00000001,
} TMDS_CTL3_DATA_INVERT;

/*
 * TMDS_CTL3_DATA_MODULATION enum
 */

typedef enum TMDS_CTL3_DATA_MODULATION {
TMDS_CTL3_DATA_MODULATION_DISABLE        = 0x00000000,
TMDS_CTL3_DATA_MODULATION_BIT0           = 0x00000001,
TMDS_CTL3_DATA_MODULATION_BIT1           = 0x00000002,
TMDS_CTL3_DATA_MODULATION_BIT2           = 0x00000003,
} TMDS_CTL3_DATA_MODULATION;

/*
 * TMDS_CTL3_PATTERN_OUT_EN enum
 */

typedef enum TMDS_CTL3_PATTERN_OUT_EN {
TMDS_CTL3_PATTERN_OUT_DISABLE            = 0x00000000,
TMDS_CTL3_PATTERN_OUT_ENABLE             = 0x00000001,
} TMDS_CTL3_PATTERN_OUT_EN;

/*
 * TMDS_CTL3_DATA_SEL enum
 */

typedef enum TMDS_CTL3_DATA_SEL {
TMDS_CTL3_DATA_SEL0_RESERVED             = 0x00000000,
TMDS_CTL3_DATA_SEL1_DISPLAY_ENABLE       = 0x00000001,
TMDS_CTL3_DATA_SEL2_VSYNC                = 0x00000002,
TMDS_CTL3_DATA_SEL3_RESERVED             = 0x00000003,
TMDS_CTL3_DATA_SEL4_HSYNC                = 0x00000004,
TMDS_CTL3_DATA_SEL5_SEL7_RESERVED        = 0x00000005,
TMDS_CTL3_DATA_SEL8_BLANK_TIME           = 0x00000006,
TMDS_CTL3_DATA_SEL9_SEL15_RESERVED       = 0x00000007,
} TMDS_CTL3_DATA_SEL;

/*
 * DIG_FE_CNTL_SOURCE_SELECT enum
 */

typedef enum DIG_FE_CNTL_SOURCE_SELECT {
DIG_FE_SOURCE_FROM_FMT0                  = 0x00000000,
DIG_FE_SOURCE_FROM_FMT1                  = 0x00000001,
DIG_FE_SOURCE_FROM_FMT2                  = 0x00000002,
DIG_FE_SOURCE_FROM_FMT3                  = 0x00000003,
DIG_FE_SOURCE_FROM_FMT4                  = 0x00000004,
DIG_FE_SOURCE_FROM_FMT5                  = 0x00000005,
} DIG_FE_CNTL_SOURCE_SELECT;

/*
 * DIG_FE_CNTL_STEREOSYNC_SELECT enum
 */

typedef enum DIG_FE_CNTL_STEREOSYNC_SELECT {
DIG_FE_STEREOSYNC_FROM_FMT0              = 0x00000000,
DIG_FE_STEREOSYNC_FROM_FMT1              = 0x00000001,
DIG_FE_STEREOSYNC_FROM_FMT2              = 0x00000002,
DIG_FE_STEREOSYNC_FROM_FMT3              = 0x00000003,
DIG_FE_STEREOSYNC_FROM_FMT4              = 0x00000004,
DIG_FE_STEREOSYNC_FROM_FMT5              = 0x00000005,
} DIG_FE_CNTL_STEREOSYNC_SELECT;

/*
 * DIG_FIFO_READ_CLOCK_SRC enum
 */

typedef enum DIG_FIFO_READ_CLOCK_SRC {
DIG_FIFO_READ_CLOCK_SRC_FROM_DCCG        = 0x00000000,
DIG_FIFO_READ_CLOCK_SRC_FROM_DISPLAY_PIPE  = 0x00000001,
} DIG_FIFO_READ_CLOCK_SRC;

/*
 * DIG_OUTPUT_CRC_CNTL_LINK_SEL enum
 */

typedef enum DIG_OUTPUT_CRC_CNTL_LINK_SEL {
DIG_OUTPUT_CRC_ON_LINK0                  = 0x00000000,
DIG_OUTPUT_CRC_ON_LINK1                  = 0x00000001,
} DIG_OUTPUT_CRC_CNTL_LINK_SEL;

/*
 * DIG_OUTPUT_CRC_DATA_SEL enum
 */

typedef enum DIG_OUTPUT_CRC_DATA_SEL {
DIG_OUTPUT_CRC_FOR_FULLFRAME             = 0x00000000,
DIG_OUTPUT_CRC_FOR_ACTIVEONLY            = 0x00000001,
DIG_OUTPUT_CRC_FOR_VBI                   = 0x00000002,
DIG_OUTPUT_CRC_FOR_AUDIO                 = 0x00000003,
} DIG_OUTPUT_CRC_DATA_SEL;

/*
 * DIG_TEST_PATTERN_TEST_PATTERN_OUT_EN enum
 */

typedef enum DIG_TEST_PATTERN_TEST_PATTERN_OUT_EN {
DIG_IN_NORMAL_OPERATION                  = 0x00000000,
DIG_IN_DEBUG_MODE                        = 0x00000001,
} DIG_TEST_PATTERN_TEST_PATTERN_OUT_EN;

/*
 * DIG_TEST_PATTERN_HALF_CLOCK_PATTERN_SEL enum
 */

typedef enum DIG_TEST_PATTERN_HALF_CLOCK_PATTERN_SEL {
DIG_10BIT_TEST_PATTERN                   = 0x00000000,
DIG_ALTERNATING_TEST_PATTERN             = 0x00000001,
} DIG_TEST_PATTERN_HALF_CLOCK_PATTERN_SEL;

/*
 * DIG_TEST_PATTERN_RANDOM_PATTERN_OUT_EN enum
 */

typedef enum DIG_TEST_PATTERN_RANDOM_PATTERN_OUT_EN {
DIG_TEST_PATTERN_NORMAL                  = 0x00000000,
DIG_TEST_PATTERN_RANDOM                  = 0x00000001,
} DIG_TEST_PATTERN_RANDOM_PATTERN_OUT_EN;

/*
 * DIG_TEST_PATTERN_RANDOM_PATTERN_RESET enum
 */

typedef enum DIG_TEST_PATTERN_RANDOM_PATTERN_RESET {
DIG_RANDOM_PATTERN_ENABLED               = 0x00000000,
DIG_RANDOM_PATTERN_RESETED               = 0x00000001,
} DIG_TEST_PATTERN_RANDOM_PATTERN_RESET;

/*
 * DIG_TEST_PATTERN_EXTERNAL_RESET_EN enum
 */

typedef enum DIG_TEST_PATTERN_EXTERNAL_RESET_EN {
DIG_TEST_PATTERN_EXTERNAL_RESET_ENABLE   = 0x00000000,
DIG_TEST_PATTERN_EXTERNAL_RESET_BY_EXT_SIG  = 0x00000001,
} DIG_TEST_PATTERN_EXTERNAL_RESET_EN;

/*
 * DIG_RANDOM_PATTERN_SEED_RAN_PAT enum
 */

typedef enum DIG_RANDOM_PATTERN_SEED_RAN_PAT {
DIG_RANDOM_PATTERN_SEED_RAN_PAT_ALL_PIXELS  = 0x00000000,
DIG_RANDOM_PATTERN_SEED_RAN_PAT_DE_HIGH  = 0x00000001,
} DIG_RANDOM_PATTERN_SEED_RAN_PAT;

/*
 * DIG_FIFO_STATUS_USE_OVERWRITE_LEVEL enum
 */

typedef enum DIG_FIFO_STATUS_USE_OVERWRITE_LEVEL {
DIG_FIFO_USE_OVERWRITE_LEVEL             = 0x00000000,
DIG_FIFO_USE_CAL_AVERAGE_LEVEL           = 0x00000001,
} DIG_FIFO_STATUS_USE_OVERWRITE_LEVEL;

/*
 * DIG_FIFO_ERROR_ACK enum
 */

typedef enum DIG_FIFO_ERROR_ACK {
DIG_FIFO_ERROR_ACK_INT                   = 0x00000000,
DIG_FIFO_ERROR_NOT_ACK                   = 0x00000001,
} DIG_FIFO_ERROR_ACK;

/*
 * DIG_FIFO_STATUS_FORCE_RECAL_AVERAGE enum
 */

typedef enum DIG_FIFO_STATUS_FORCE_RECAL_AVERAGE {
DIG_FIFO_NOT_FORCE_RECAL_AVERAGE         = 0x00000000,
DIG_FIFO_FORCE_RECAL_AVERAGE_LEVEL       = 0x00000001,
} DIG_FIFO_STATUS_FORCE_RECAL_AVERAGE;

/*
 * DIG_FIFO_STATUS_FORCE_RECOMP_MINMAX enum
 */

typedef enum DIG_FIFO_STATUS_FORCE_RECOMP_MINMAX {
DIG_FIFO_NOT_FORCE_RECOMP_MINMAX         = 0x00000000,
DIG_FIFO_FORCE_RECOMP_MINMAX             = 0x00000001,
} DIG_FIFO_STATUS_FORCE_RECOMP_MINMAX;

/*
 * AFMT_INTERRUPT_STATUS_CHG_MASK enum
 */

typedef enum AFMT_INTERRUPT_STATUS_CHG_MASK {
AFMT_INTERRUPT_DISABLE                   = 0x00000000,
AFMT_INTERRUPT_ENABLE                    = 0x00000001,
} AFMT_INTERRUPT_STATUS_CHG_MASK;

/*
 * HDMI_GC_AVMUTE enum
 */

typedef enum HDMI_GC_AVMUTE {
HDMI_GC_AVMUTE_SET                       = 0x00000000,
HDMI_GC_AVMUTE_UNSET                     = 0x00000001,
} HDMI_GC_AVMUTE;

/*
 * HDMI_DEFAULT_PAHSE enum
 */

typedef enum HDMI_DEFAULT_PAHSE {
HDMI_DEFAULT_PHASE_IS_0                  = 0x00000000,
HDMI_DEFAULT_PHASE_IS_1                  = 0x00000001,
} HDMI_DEFAULT_PAHSE;

/*
 * AFMT_AUDIO_PACKET_CONTROL2_AUDIO_LAYOUT_OVRD enum
 */

typedef enum AFMT_AUDIO_PACKET_CONTROL2_AUDIO_LAYOUT_OVRD {
AFMT_AUDIO_LAYOUT_DETERMINED_BY_AZ_AUDIO_CHANNEL_STATUS  = 0x00000000,
AFMT_AUDIO_LAYOUT_OVRD_BY_REGISTER       = 0x00000001,
} AFMT_AUDIO_PACKET_CONTROL2_AUDIO_LAYOUT_OVRD;

/*
 * AUDIO_LAYOUT_SELECT enum
 */

typedef enum AUDIO_LAYOUT_SELECT {
AUDIO_LAYOUT_0                           = 0x00000000,
AUDIO_LAYOUT_1                           = 0x00000001,
} AUDIO_LAYOUT_SELECT;

/*
 * AFMT_AUDIO_CRC_CONTROL_CONT enum
 */

typedef enum AFMT_AUDIO_CRC_CONTROL_CONT {
AFMT_AUDIO_CRC_ONESHOT                   = 0x00000000,
AFMT_AUDIO_CRC_AUTO_RESTART              = 0x00000001,
} AFMT_AUDIO_CRC_CONTROL_CONT;

/*
 * AFMT_AUDIO_CRC_CONTROL_SOURCE enum
 */

typedef enum AFMT_AUDIO_CRC_CONTROL_SOURCE {
AFMT_AUDIO_CRC_SOURCE_FROM_FIFO_INPUT    = 0x00000000,
AFMT_AUDIO_CRC_SOURCE_FROM_FIFO_OUTPUT   = 0x00000001,
} AFMT_AUDIO_CRC_CONTROL_SOURCE;

/*
 * AFMT_AUDIO_CRC_CONTROL_CH_SEL enum
 */

typedef enum AFMT_AUDIO_CRC_CONTROL_CH_SEL {
AFMT_AUDIO_CRC_CH0_SIG                   = 0x00000000,
AFMT_AUDIO_CRC_CH1_SIG                   = 0x00000001,
AFMT_AUDIO_CRC_CH2_SIG                   = 0x00000002,
AFMT_AUDIO_CRC_CH3_SIG                   = 0x00000003,
AFMT_AUDIO_CRC_CH4_SIG                   = 0x00000004,
AFMT_AUDIO_CRC_CH5_SIG                   = 0x00000005,
AFMT_AUDIO_CRC_CH6_SIG                   = 0x00000006,
AFMT_AUDIO_CRC_CH7_SIG                   = 0x00000007,
AFMT_AUDIO_CRC_RESERVED_8                = 0x00000008,
AFMT_AUDIO_CRC_RESERVED_9                = 0x00000009,
AFMT_AUDIO_CRC_RESERVED_10               = 0x0000000a,
AFMT_AUDIO_CRC_RESERVED_11               = 0x0000000b,
AFMT_AUDIO_CRC_RESERVED_12               = 0x0000000c,
AFMT_AUDIO_CRC_RESERVED_13               = 0x0000000d,
AFMT_AUDIO_CRC_RESERVED_14               = 0x0000000e,
AFMT_AUDIO_CRC_AUDIO_SAMPLE_COUNT        = 0x0000000f,
} AFMT_AUDIO_CRC_CONTROL_CH_SEL;

/*
 * AFMT_RAMP_CONTROL0_SIGN enum
 */

typedef enum AFMT_RAMP_CONTROL0_SIGN {
AFMT_RAMP_SIGNED                         = 0x00000000,
AFMT_RAMP_UNSIGNED                       = 0x00000001,
} AFMT_RAMP_CONTROL0_SIGN;

/*
 * AFMT_AUDIO_PACKET_CONTROL_AUDIO_SAMPLE_SEND enum
 */

typedef enum AFMT_AUDIO_PACKET_CONTROL_AUDIO_SAMPLE_SEND {
AFMT_AUDIO_PACKET_SENT_DISABLED          = 0x00000000,
AFMT_AUDIO_PACKET_SENT_ENABLED           = 0x00000001,
} AFMT_AUDIO_PACKET_CONTROL_AUDIO_SAMPLE_SEND;

/*
 * AFMT_AUDIO_PACKET_CONTROL_RESET_FIFO_WHEN_AUDIO_DIS enum
 */

typedef enum AFMT_AUDIO_PACKET_CONTROL_RESET_FIFO_WHEN_AUDIO_DIS {
AFMT_NOT_RESET_AUDIO_FIFO_WHEN_AUDIO_DISABLED_RESERVED  = 0x00000000,
AFMT_RESET_AUDIO_FIFO_WHEN_AUDIO_DISABLED  = 0x00000001,
} AFMT_AUDIO_PACKET_CONTROL_RESET_FIFO_WHEN_AUDIO_DIS;

/*
 * AFMT_INFOFRAME_CONTROL0_AUDIO_INFO_SOURCE enum
 */

typedef enum AFMT_INFOFRAME_CONTROL0_AUDIO_INFO_SOURCE {
AFMT_INFOFRAME_SOURCE_FROM_AZALIA_BLOCK  = 0x00000000,
AFMT_INFOFRAME_SOURCE_FROM_AFMT_REGISTERS  = 0x00000001,
} AFMT_INFOFRAME_CONTROL0_AUDIO_INFO_SOURCE;

/*
 * AFMT_AUDIO_SRC_CONTROL_SELECT enum
 */

typedef enum AFMT_AUDIO_SRC_CONTROL_SELECT {
AFMT_AUDIO_SRC_FROM_AZ_STREAM0           = 0x00000000,
AFMT_AUDIO_SRC_FROM_AZ_STREAM1           = 0x00000001,
AFMT_AUDIO_SRC_FROM_AZ_STREAM2           = 0x00000002,
AFMT_AUDIO_SRC_FROM_AZ_STREAM3           = 0x00000003,
AFMT_AUDIO_SRC_FROM_AZ_STREAM4           = 0x00000004,
AFMT_AUDIO_SRC_FROM_AZ_STREAM5           = 0x00000005,
AFMT_AUDIO_SRC_RESERVED                  = 0x00000006,
} AFMT_AUDIO_SRC_CONTROL_SELECT;

/*
 * DIG_BE_CNTL_MODE enum
 */

typedef enum DIG_BE_CNTL_MODE {
DIG_BE_DP_SST_MODE                       = 0x00000000,
DIG_BE_RESERVED1                         = 0x00000001,
DIG_BE_TMDS_DVI_MODE                     = 0x00000002,
DIG_BE_TMDS_HDMI_MODE                    = 0x00000003,
DIG_BE_SDVO_RESERVED                     = 0x00000004,
DIG_BE_DP_MST_MODE                       = 0x00000005,
DIG_BE_RESERVED2                         = 0x00000006,
DIG_BE_RESERVED3                         = 0x00000007,
} DIG_BE_CNTL_MODE;

/*
 * DIG_BE_CNTL_HPD_SELECT enum
 */

typedef enum DIG_BE_CNTL_HPD_SELECT {
DIG_BE_CNTL_HPD1                         = 0x00000000,
DIG_BE_CNTL_HPD2                         = 0x00000001,
DIG_BE_CNTL_HPD3                         = 0x00000002,
DIG_BE_CNTL_HPD4                         = 0x00000003,
DIG_BE_CNTL_HPD5                         = 0x00000004,
DIG_BE_CNTL_HPD6                         = 0x00000005,
} DIG_BE_CNTL_HPD_SELECT;

/*
 * LVTMA_RANDOM_PATTERN_SEED_RAN_PAT enum
 */

typedef enum LVTMA_RANDOM_PATTERN_SEED_RAN_PAT {
LVTMA_RANDOM_PATTERN_SEED_ALL_PIXELS     = 0x00000000,
LVTMA_RANDOM_PATTERN_SEED_ONLY_DE_HIGH   = 0x00000001,
} LVTMA_RANDOM_PATTERN_SEED_RAN_PAT;

/*
 * TMDS_SYNC_PHASE enum
 */

typedef enum TMDS_SYNC_PHASE {
TMDS_NOT_SYNC_PHASE_ON_FRAME_START       = 0x00000000,
TMDS_SYNC_PHASE_ON_FRAME_START           = 0x00000001,
} TMDS_SYNC_PHASE;

/*
 * TMDS_DATA_SYNCHRONIZATION_DSINTSEL enum
 */

typedef enum TMDS_DATA_SYNCHRONIZATION_DSINTSEL {
TMDS_DATA_SYNCHRONIZATION_DSINTSEL_PCLK_TMDS  = 0x00000000,
TMDS_DATA_SYNCHRONIZATION_DSINTSEL_TMDS_PLL  = 0x00000001,
} TMDS_DATA_SYNCHRONIZATION_DSINTSEL;

/*
 * TMDS_TRANSMITTER_ENABLE_HPD_MASK enum
 */

typedef enum TMDS_TRANSMITTER_ENABLE_HPD_MASK {
TMDS_TRANSMITTER_HPD_MASK_NOT_OVERRIDE   = 0x00000000,
TMDS_TRANSMITTER_HPD_MASK_OVERRIDE       = 0x00000001,
} TMDS_TRANSMITTER_ENABLE_HPD_MASK;

/*
 * TMDS_TRANSMITTER_ENABLE_LNKCEN_HPD_MASK enum
 */

typedef enum TMDS_TRANSMITTER_ENABLE_LNKCEN_HPD_MASK {
TMDS_TRANSMITTER_LNKCEN_HPD_MASK_NOT_OVERRIDE  = 0x00000000,
TMDS_TRANSMITTER_LNKCEN_HPD_MASK_OVERRIDE  = 0x00000001,
} TMDS_TRANSMITTER_ENABLE_LNKCEN_HPD_MASK;

/*
 * TMDS_TRANSMITTER_ENABLE_LNKDEN_HPD_MASK enum
 */

typedef enum TMDS_TRANSMITTER_ENABLE_LNKDEN_HPD_MASK {
TMDS_TRANSMITTER_LNKDEN_HPD_MASK_NOT_OVERRIDE  = 0x00000000,
TMDS_TRANSMITTER_LNKDEN_HPD_MASK_OVERRIDE  = 0x00000001,
} TMDS_TRANSMITTER_ENABLE_LNKDEN_HPD_MASK;

/*
 * TMDS_TRANSMITTER_CONTROL_PLL_ENABLE_HPD_MASK enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_PLL_ENABLE_HPD_MASK {
TMDS_TRANSMITTER_HPD_NOT_OVERRIDE_PLL_ENABLE  = 0x00000000,
TMDS_TRANSMITTER_HPD_OVERRIDE_PLL_ENABLE_ON_DISCON  = 0x00000001,
TMDS_TRANSMITTER_HPD_OVERRIDE_PLL_ENABLE_ON_CON  = 0x00000002,
TMDS_TRANSMITTER_HPD_OVERRIDE_PLL_ENABLE  = 0x00000003,
} TMDS_TRANSMITTER_CONTROL_PLL_ENABLE_HPD_MASK;

/*
 * TMDS_TRANSMITTER_CONTROL_IDSCKSELA enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_IDSCKSELA {
TMDS_TRANSMITTER_IDSCKSELA_USE_IPIXCLK   = 0x00000000,
TMDS_TRANSMITTER_IDSCKSELA_USE_IDCLK     = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_IDSCKSELA;

/*
 * TMDS_TRANSMITTER_CONTROL_IDSCKSELB enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_IDSCKSELB {
TMDS_TRANSMITTER_IDSCKSELB_USE_IPIXCLK   = 0x00000000,
TMDS_TRANSMITTER_IDSCKSELB_USE_IDCLK     = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_IDSCKSELB;

/*
 * TMDS_TRANSMITTER_CONTROL_PLL_PWRUP_SEQ_EN enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_PLL_PWRUP_SEQ_EN {
TMDS_TRANSMITTER_PLL_PWRUP_SEQ_DISABLE   = 0x00000000,
TMDS_TRANSMITTER_PLL_PWRUP_SEQ_ENABLE    = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_PLL_PWRUP_SEQ_EN;

/*
 * TMDS_TRANSMITTER_CONTROL_PLL_RESET_HPD_MASK enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_PLL_RESET_HPD_MASK {
TMDS_TRANSMITTER_PLL_NOT_RST_ON_HPD      = 0x00000000,
TMDS_TRANSMITTER_PLL_RST_ON_HPD          = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_PLL_RESET_HPD_MASK;

/*
 * TMDS_TRANSMITTER_CONTROL_TMCLK_FROM_PADS enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_TMCLK_FROM_PADS {
TMDS_TRANSMITTER_TMCLK_FROM_TMDS_TMCLK   = 0x00000000,
TMDS_TRANSMITTER_TMCLK_FROM_PADS         = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_TMCLK_FROM_PADS;

/*
 * TMDS_TRANSMITTER_CONTROL_TDCLK_FROM_PADS enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_TDCLK_FROM_PADS {
TMDS_TRANSMITTER_TDCLK_FROM_TMDS_TDCLK   = 0x00000000,
TMDS_TRANSMITTER_TDCLK_FROM_PADS         = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_TDCLK_FROM_PADS;

/*
 * TMDS_TRANSMITTER_CONTROL_PLLSEL_OVERWRITE_EN enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_PLLSEL_OVERWRITE_EN {
TMDS_TRANSMITTER_PLLSEL_BY_HW            = 0x00000000,
TMDS_TRANSMITTER_PLLSEL_OVERWRITE_BY_SW  = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_PLLSEL_OVERWRITE_EN;

/*
 * TMDS_TRANSMITTER_CONTROL_BYPASS_PLLA enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_BYPASS_PLLA {
TMDS_TRANSMITTER_BYPASS_PLLA_COHERENT    = 0x00000000,
TMDS_TRANSMITTER_BYPASS_PLLA_INCOHERENT  = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_BYPASS_PLLA;

/*
 * TMDS_TRANSMITTER_CONTROL_BYPASS_PLLB enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_BYPASS_PLLB {
TMDS_TRANSMITTER_BYPASS_PLLB_COHERENT    = 0x00000000,
TMDS_TRANSMITTER_BYPASS_PLLB_INCOHERENT  = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_BYPASS_PLLB;

/*
 * TMDS_REG_TEST_OUTPUTA_CNTLA enum
 */

typedef enum TMDS_REG_TEST_OUTPUTA_CNTLA {
TMDS_REG_TEST_OUTPUTA_CNTLA_OTDATA0      = 0x00000000,
TMDS_REG_TEST_OUTPUTA_CNTLA_OTDATA1      = 0x00000001,
TMDS_REG_TEST_OUTPUTA_CNTLA_OTDATA2      = 0x00000002,
TMDS_REG_TEST_OUTPUTA_CNTLA_NA           = 0x00000003,
} TMDS_REG_TEST_OUTPUTA_CNTLA;

/*
 * TMDS_REG_TEST_OUTPUTB_CNTLB enum
 */

typedef enum TMDS_REG_TEST_OUTPUTB_CNTLB {
TMDS_REG_TEST_OUTPUTB_CNTLB_OTDATB0      = 0x00000000,
TMDS_REG_TEST_OUTPUTB_CNTLB_OTDATB1      = 0x00000001,
TMDS_REG_TEST_OUTPUTB_CNTLB_OTDATB2      = 0x00000002,
TMDS_REG_TEST_OUTPUTB_CNTLB_NA           = 0x00000003,
} TMDS_REG_TEST_OUTPUTB_CNTLB;

/*******************************************************
 * DCP Enums
 *******************************************************/

/*
 * DCP_GRPH_ENABLE enum
 */

typedef enum DCP_GRPH_ENABLE {
DCP_GRPH_ENABLE_FALSE                    = 0x00000000,
DCP_GRPH_ENABLE_TRUE                     = 0x00000001,
} DCP_GRPH_ENABLE;

/*
 * DCP_GRPH_KEYER_ALPHA_SEL enum
 */

typedef enum DCP_GRPH_KEYER_ALPHA_SEL {
DCP_GRPH_KEYER_ALPHA_SEL_FALSE           = 0x00000000,
DCP_GRPH_KEYER_ALPHA_SEL_TRUE            = 0x00000001,
} DCP_GRPH_KEYER_ALPHA_SEL;

/*
 * DCP_GRPH_DEPTH enum
 */

typedef enum DCP_GRPH_DEPTH {
DCP_GRPH_DEPTH_8BPP                      = 0x00000000,
DCP_GRPH_DEPTH_16BPP                     = 0x00000001,
DCP_GRPH_DEPTH_32BPP                     = 0x00000002,
DCP_GRPH_DEPTH_64BPP                     = 0x00000003,
} DCP_GRPH_DEPTH;

/*
 * DCP_GRPH_NUM_BANKS enum
 */

typedef enum DCP_GRPH_NUM_BANKS {
DCP_GRPH_NUM_BANKS_1BANK                 = 0x00000000,
DCP_GRPH_NUM_BANKS_2BANK                 = 0x00000001,
DCP_GRPH_NUM_BANKS_4BANK                 = 0x00000002,
DCP_GRPH_NUM_BANKS_8BANK                 = 0x00000003,
DCP_GRPH_NUM_BANKS_16BANK                = 0x00000004,
} DCP_GRPH_NUM_BANKS;

/*
 * DCP_GRPH_NUM_PIPES enum
 */

typedef enum DCP_GRPH_NUM_PIPES {
DCP_GRPH_NUM_PIPES_1PIPE                 = 0x00000000,
DCP_GRPH_NUM_PIPES_2PIPE                 = 0x00000001,
DCP_GRPH_NUM_PIPES_4PIPE                 = 0x00000002,
DCP_GRPH_NUM_PIPES_8PIPE                 = 0x00000003,
} DCP_GRPH_NUM_PIPES;

/*
 * DCP_GRPH_FORMAT enum
 */

typedef enum DCP_GRPH_FORMAT {
DCP_GRPH_FORMAT_8BPP                     = 0x00000000,
DCP_GRPH_FORMAT_16BPP                    = 0x00000001,
DCP_GRPH_FORMAT_32BPP                    = 0x00000002,
DCP_GRPH_FORMAT_64BPP                    = 0x00000003,
} DCP_GRPH_FORMAT;

/*
 * DCP_GRPH_ADDRESS_TRANSLATION_ENABLE enum
 */

typedef enum DCP_GRPH_ADDRESS_TRANSLATION_ENABLE {
DCP_GRPH_ADDRESS_TRANSLATION_ENABLE_FALSE  = 0x00000000,
DCP_GRPH_ADDRESS_TRANSLATION_ENABLE_TRUE  = 0x00000001,
} DCP_GRPH_ADDRESS_TRANSLATION_ENABLE;

/*
 * DCP_GRPH_SW_MODE enum
 */

typedef enum DCP_GRPH_SW_MODE {
DCP_GRPH_SW_MODE_0                       = 0x00000000,
DCP_GRPH_SW_MODE_2                       = 0x00000002,
DCP_GRPH_SW_MODE_3                       = 0x00000003,
DCP_GRPH_SW_MODE_22                      = 0x00000016,
DCP_GRPH_SW_MODE_23                      = 0x00000017,
DCP_GRPH_SW_MODE_26                      = 0x0000001a,
DCP_GRPH_SW_MODE_27                      = 0x0000001b,
DCP_GRPH_SW_MODE_30                      = 0x0000001e,
DCP_GRPH_SW_MODE_31                      = 0x0000001f,
} DCP_GRPH_SW_MODE;

/*
 * DCP_GRPH_COLOR_EXPANSION_MODE enum
 */

typedef enum DCP_GRPH_COLOR_EXPANSION_MODE {
DCP_GRPH_COLOR_EXPANSION_MODE_DEXP       = 0x00000000,
DCP_GRPH_COLOR_EXPANSION_MODE_ZEXP       = 0x00000001,
} DCP_GRPH_COLOR_EXPANSION_MODE;

/*
 * DCP_GRPH_LUT_10BIT_BYPASS_EN enum
 */

typedef enum DCP_GRPH_LUT_10BIT_BYPASS_EN {
DCP_GRPH_LUT_10BIT_BYPASS_EN_FALSE       = 0x00000000,
DCP_GRPH_LUT_10BIT_BYPASS_EN_TRUE        = 0x00000001,
} DCP_GRPH_LUT_10BIT_BYPASS_EN;

/*
 * DCP_GRPH_LUT_10BIT_BYPASS_DBL_BUF_EN enum
 */

typedef enum DCP_GRPH_LUT_10BIT_BYPASS_DBL_BUF_EN {
DCP_GRPH_LUT_10BIT_BYPASS_DBL_BUF_EN_FALSE  = 0x00000000,
DCP_GRPH_LUT_10BIT_BYPASS_DBL_BUF_EN_TRUE  = 0x00000001,
} DCP_GRPH_LUT_10BIT_BYPASS_DBL_BUF_EN;

/*
 * DCP_GRPH_ENDIAN_SWAP enum
 */

typedef enum DCP_GRPH_ENDIAN_SWAP {
DCP_GRPH_ENDIAN_SWAP_NONE                = 0x00000000,
DCP_GRPH_ENDIAN_SWAP_8IN16               = 0x00000001,
DCP_GRPH_ENDIAN_SWAP_8IN32               = 0x00000002,
DCP_GRPH_ENDIAN_SWAP_8IN64               = 0x00000003,
} DCP_GRPH_ENDIAN_SWAP;

/*
 * DCP_GRPH_RED_CROSSBAR enum
 */

typedef enum DCP_GRPH_RED_CROSSBAR {
DCP_GRPH_RED_CROSSBAR_FROM_R             = 0x00000000,
DCP_GRPH_RED_CROSSBAR_FROM_G             = 0x00000001,
DCP_GRPH_RED_CROSSBAR_FROM_B             = 0x00000002,
DCP_GRPH_RED_CROSSBAR_FROM_A             = 0x00000003,
} DCP_GRPH_RED_CROSSBAR;

/*
 * DCP_GRPH_GREEN_CROSSBAR enum
 */

typedef enum DCP_GRPH_GREEN_CROSSBAR {
DCP_GRPH_GREEN_CROSSBAR_FROM_G           = 0x00000000,
DCP_GRPH_GREEN_CROSSBAR_FROM_B           = 0x00000001,
DCP_GRPH_GREEN_CROSSBAR_FROM_A           = 0x00000002,
DCP_GRPH_GREEN_CROSSBAR_FROM_R           = 0x00000003,
} DCP_GRPH_GREEN_CROSSBAR;

/*
 * DCP_GRPH_BLUE_CROSSBAR enum
 */

typedef enum DCP_GRPH_BLUE_CROSSBAR {
DCP_GRPH_BLUE_CROSSBAR_FROM_B            = 0x00000000,
DCP_GRPH_BLUE_CROSSBAR_FROM_A            = 0x00000001,
DCP_GRPH_BLUE_CROSSBAR_FROM_R            = 0x00000002,
DCP_GRPH_BLUE_CROSSBAR_FROM_G            = 0x00000003,
} DCP_GRPH_BLUE_CROSSBAR;

/*
 * DCP_GRPH_ALPHA_CROSSBAR enum
 */

typedef enum DCP_GRPH_ALPHA_CROSSBAR {
DCP_GRPH_ALPHA_CROSSBAR_FROM_A           = 0x00000000,
DCP_GRPH_ALPHA_CROSSBAR_FROM_R           = 0x00000001,
DCP_GRPH_ALPHA_CROSSBAR_FROM_G           = 0x00000002,
DCP_GRPH_ALPHA_CROSSBAR_FROM_B           = 0x00000003,
} DCP_GRPH_ALPHA_CROSSBAR;

/*
 * DCP_GRPH_PRIMARY_DFQ_ENABLE enum
 */

typedef enum DCP_GRPH_PRIMARY_DFQ_ENABLE {
DCP_GRPH_PRIMARY_DFQ_ENABLE_FALSE        = 0x00000000,
DCP_GRPH_PRIMARY_DFQ_ENABLE_TRUE         = 0x00000001,
} DCP_GRPH_PRIMARY_DFQ_ENABLE;

/*
 * DCP_GRPH_SECONDARY_DFQ_ENABLE enum
 */

typedef enum DCP_GRPH_SECONDARY_DFQ_ENABLE {
DCP_GRPH_SECONDARY_DFQ_ENABLE_FALSE      = 0x00000000,
DCP_GRPH_SECONDARY_DFQ_ENABLE_TRUE       = 0x00000001,
} DCP_GRPH_SECONDARY_DFQ_ENABLE;

/*
 * DCP_GRPH_INPUT_GAMMA_MODE enum
 */

typedef enum DCP_GRPH_INPUT_GAMMA_MODE {
DCP_GRPH_INPUT_GAMMA_MODE_LUT            = 0x00000000,
DCP_GRPH_INPUT_GAMMA_MODE_BYPASS         = 0x00000001,
} DCP_GRPH_INPUT_GAMMA_MODE;

/*
 * DCP_GRPH_MODE_UPDATE_PENDING enum
 */

typedef enum DCP_GRPH_MODE_UPDATE_PENDING {
DCP_GRPH_MODE_UPDATE_PENDING_FALSE       = 0x00000000,
DCP_GRPH_MODE_UPDATE_PENDING_TRUE        = 0x00000001,
} DCP_GRPH_MODE_UPDATE_PENDING;

/*
 * DCP_GRPH_MODE_UPDATE_TAKEN enum
 */

typedef enum DCP_GRPH_MODE_UPDATE_TAKEN {
DCP_GRPH_MODE_UPDATE_TAKEN_FALSE         = 0x00000000,
DCP_GRPH_MODE_UPDATE_TAKEN_TRUE          = 0x00000001,
} DCP_GRPH_MODE_UPDATE_TAKEN;

/*
 * DCP_GRPH_SURFACE_UPDATE_PENDING enum
 */

typedef enum DCP_GRPH_SURFACE_UPDATE_PENDING {
DCP_GRPH_SURFACE_UPDATE_PENDING_FALSE    = 0x00000000,
DCP_GRPH_SURFACE_UPDATE_PENDING_TRUE     = 0x00000001,
} DCP_GRPH_SURFACE_UPDATE_PENDING;

/*
 * DCP_GRPH_SURFACE_UPDATE_TAKEN enum
 */

typedef enum DCP_GRPH_SURFACE_UPDATE_TAKEN {
DCP_GRPH_SURFACE_UPDATE_TAKEN_FALSE      = 0x00000000,
DCP_GRPH_SURFACE_UPDATE_TAKEN_TRUE       = 0x00000001,
} DCP_GRPH_SURFACE_UPDATE_TAKEN;

/*
 * DCP_GRPH_SURFACE_XDMA_PENDING_ENABLE enum
 */

typedef enum DCP_GRPH_SURFACE_XDMA_PENDING_ENABLE {
DCP_GRPH_SURFACE_XDMA_PENDING_ENABLE_FALSE = 0x00000000,
DCP_GRPH_SURFACE_XDMA_PENDING_ENABLE_TRUE = 0x00000001,
} DCP_GRPH_SURFACE_XDMA_PENDING_ENABLE;

/*
 * DCP_GRPH_UPDATE_LOCK enum
 */

typedef enum DCP_GRPH_UPDATE_LOCK {
DCP_GRPH_UPDATE_LOCK_FALSE               = 0x00000000,
DCP_GRPH_UPDATE_LOCK_TRUE                = 0x00000001,
} DCP_GRPH_UPDATE_LOCK;

/*
 * DCP_GRPH_SURFACE_IGNORE_UPDATE_LOCK enum
 */

typedef enum DCP_GRPH_SURFACE_IGNORE_UPDATE_LOCK {
DCP_GRPH_SURFACE_IGNORE_UPDATE_LOCK_FALSE  = 0x00000000,
DCP_GRPH_SURFACE_IGNORE_UPDATE_LOCK_TRUE  = 0x00000001,
} DCP_GRPH_SURFACE_IGNORE_UPDATE_LOCK;

/*
 * DCP_GRPH_MODE_DISABLE_MULTIPLE_UPDATE enum
 */

typedef enum DCP_GRPH_MODE_DISABLE_MULTIPLE_UPDATE {
DCP_GRPH_MODE_DISABLE_MULTIPLE_UPDATE_FALSE  = 0x00000000,
DCP_GRPH_MODE_DISABLE_MULTIPLE_UPDATE_TRUE  = 0x00000001,
} DCP_GRPH_MODE_DISABLE_MULTIPLE_UPDATE;

/*
 * DCP_GRPH_SURFACE_DISABLE_MULTIPLE_UPDATE enum
 */

typedef enum DCP_GRPH_SURFACE_DISABLE_MULTIPLE_UPDATE {
DCP_GRPH_SURFACE_DISABLE_MULTIPLE_UPDATE_FALSE  = 0x00000000,
DCP_GRPH_SURFACE_DISABLE_MULTIPLE_UPDATE_TRUE  = 0x00000001,
} DCP_GRPH_SURFACE_DISABLE_MULTIPLE_UPDATE;

/*
 * DCP_GRPH_SURFACE_UPDATE_H_RETRACE_EN enum
 */

typedef enum DCP_GRPH_SURFACE_UPDATE_H_RETRACE_EN {
DCP_GRPH_SURFACE_UPDATE_H_RETRACE_EN_FALSE  = 0x00000000,
DCP_GRPH_SURFACE_UPDATE_H_RETRACE_EN_TRUE  = 0x00000001,
} DCP_GRPH_SURFACE_UPDATE_H_RETRACE_EN;

/*
 * DCP_GRPH_XDMA_SUPER_AA_EN enum
 */

typedef enum DCP_GRPH_XDMA_SUPER_AA_EN {
DCP_GRPH_XDMA_SUPER_AA_EN_FALSE          = 0x00000000,
DCP_GRPH_XDMA_SUPER_AA_EN_TRUE           = 0x00000001,
} DCP_GRPH_XDMA_SUPER_AA_EN;

/*
 * DCP_GRPH_DFQ_RESET enum
 */

typedef enum DCP_GRPH_DFQ_RESET {
DCP_GRPH_DFQ_RESET_FALSE                 = 0x00000000,
DCP_GRPH_DFQ_RESET_TRUE                  = 0x00000001,
} DCP_GRPH_DFQ_RESET;

/*
 * DCP_GRPH_DFQ_SIZE enum
 */

typedef enum DCP_GRPH_DFQ_SIZE {
DCP_GRPH_DFQ_SIZE_DEEP1                  = 0x00000000,
DCP_GRPH_DFQ_SIZE_DEEP2                  = 0x00000001,
DCP_GRPH_DFQ_SIZE_DEEP3                  = 0x00000002,
DCP_GRPH_DFQ_SIZE_DEEP4                  = 0x00000003,
DCP_GRPH_DFQ_SIZE_DEEP5                  = 0x00000004,
DCP_GRPH_DFQ_SIZE_DEEP6                  = 0x00000005,
DCP_GRPH_DFQ_SIZE_DEEP7                  = 0x00000006,
DCP_GRPH_DFQ_SIZE_DEEP8                  = 0x00000007,
} DCP_GRPH_DFQ_SIZE;

/*
 * DCP_GRPH_DFQ_MIN_FREE_ENTRIES enum
 */

typedef enum DCP_GRPH_DFQ_MIN_FREE_ENTRIES {
DCP_GRPH_DFQ_MIN_FREE_ENTRIES_1          = 0x00000000,
DCP_GRPH_DFQ_MIN_FREE_ENTRIES_2          = 0x00000001,
DCP_GRPH_DFQ_MIN_FREE_ENTRIES_3          = 0x00000002,
DCP_GRPH_DFQ_MIN_FREE_ENTRIES_4          = 0x00000003,
DCP_GRPH_DFQ_MIN_FREE_ENTRIES_5          = 0x00000004,
DCP_GRPH_DFQ_MIN_FREE_ENTRIES_6          = 0x00000005,
DCP_GRPH_DFQ_MIN_FREE_ENTRIES_7          = 0x00000006,
DCP_GRPH_DFQ_MIN_FREE_ENTRIES_8          = 0x00000007,
} DCP_GRPH_DFQ_MIN_FREE_ENTRIES;

/*
 * DCP_GRPH_DFQ_RESET_ACK enum
 */

typedef enum DCP_GRPH_DFQ_RESET_ACK {
DCP_GRPH_DFQ_RESET_ACK_FALSE             = 0x00000000,
DCP_GRPH_DFQ_RESET_ACK_TRUE              = 0x00000001,
} DCP_GRPH_DFQ_RESET_ACK;

/*
 * DCP_GRPH_PFLIP_INT_CLEAR enum
 */

typedef enum DCP_GRPH_PFLIP_INT_CLEAR {
DCP_GRPH_PFLIP_INT_CLEAR_FALSE           = 0x00000000,
DCP_GRPH_PFLIP_INT_CLEAR_TRUE            = 0x00000001,
} DCP_GRPH_PFLIP_INT_CLEAR;

/*
 * DCP_GRPH_PFLIP_INT_MASK enum
 */

typedef enum DCP_GRPH_PFLIP_INT_MASK {
DCP_GRPH_PFLIP_INT_MASK_FALSE            = 0x00000000,
DCP_GRPH_PFLIP_INT_MASK_TRUE             = 0x00000001,
} DCP_GRPH_PFLIP_INT_MASK;

/*
 * DCP_GRPH_PFLIP_INT_TYPE enum
 */

typedef enum DCP_GRPH_PFLIP_INT_TYPE {
DCP_GRPH_PFLIP_INT_TYPE_LEGACY_LEVEL     = 0x00000000,
DCP_GRPH_PFLIP_INT_TYPE_PULSE            = 0x00000001,
} DCP_GRPH_PFLIP_INT_TYPE;

/*
 * DCP_GRPH_PRESCALE_SELECT enum
 */

typedef enum DCP_GRPH_PRESCALE_SELECT {
DCP_GRPH_PRESCALE_SELECT_FIXED           = 0x00000000,
DCP_GRPH_PRESCALE_SELECT_FLOATING        = 0x00000001,
} DCP_GRPH_PRESCALE_SELECT;

/*
 * DCP_GRPH_PRESCALE_R_SIGN enum
 */

typedef enum DCP_GRPH_PRESCALE_R_SIGN {
DCP_GRPH_PRESCALE_R_SIGN_UNSIGNED        = 0x00000000,
DCP_GRPH_PRESCALE_R_SIGN_SIGNED          = 0x00000001,
} DCP_GRPH_PRESCALE_R_SIGN;

/*
 * DCP_GRPH_PRESCALE_G_SIGN enum
 */

typedef enum DCP_GRPH_PRESCALE_G_SIGN {
DCP_GRPH_PRESCALE_G_SIGN_UNSIGNED        = 0x00000000,
DCP_GRPH_PRESCALE_G_SIGN_SIGNED          = 0x00000001,
} DCP_GRPH_PRESCALE_G_SIGN;

/*
 * DCP_GRPH_PRESCALE_B_SIGN enum
 */

typedef enum DCP_GRPH_PRESCALE_B_SIGN {
DCP_GRPH_PRESCALE_B_SIGN_UNSIGNED        = 0x00000000,
DCP_GRPH_PRESCALE_B_SIGN_SIGNED          = 0x00000001,
} DCP_GRPH_PRESCALE_B_SIGN;

/*
 * DCP_GRPH_PRESCALE_BYPASS enum
 */

typedef enum DCP_GRPH_PRESCALE_BYPASS {
DCP_GRPH_PRESCALE_BYPASS_FALSE           = 0x00000000,
DCP_GRPH_PRESCALE_BYPASS_TRUE            = 0x00000001,
} DCP_GRPH_PRESCALE_BYPASS;

/*
 * DCP_INPUT_CSC_GRPH_MODE enum
 */

typedef enum DCP_INPUT_CSC_GRPH_MODE {
DCP_INPUT_CSC_GRPH_MODE_BYPASS           = 0x00000000,
DCP_INPUT_CSC_GRPH_MODE_INPUT_CSC_COEF   = 0x00000001,
DCP_INPUT_CSC_GRPH_MODE_SHARED_COEF      = 0x00000002,
DCP_INPUT_CSC_GRPH_MODE_RESERVED         = 0x00000003,
} DCP_INPUT_CSC_GRPH_MODE;

/*
 * DCP_OUTPUT_CSC_GRPH_MODE enum
 */

typedef enum DCP_OUTPUT_CSC_GRPH_MODE {
DCP_OUTPUT_CSC_GRPH_MODE_BYPASS          = 0x00000000,
DCP_OUTPUT_CSC_GRPH_MODE_RGB             = 0x00000001,
DCP_OUTPUT_CSC_GRPH_MODE_YCBCR601        = 0x00000002,
DCP_OUTPUT_CSC_GRPH_MODE_YCBCR709        = 0x00000003,
DCP_OUTPUT_CSC_GRPH_MODE_OUTPUT_CSC_COEF  = 0x00000004,
DCP_OUTPUT_CSC_GRPH_MODE_SHARED_COEF     = 0x00000005,
DCP_OUTPUT_CSC_GRPH_MODE_RESERVED0       = 0x00000006,
DCP_OUTPUT_CSC_GRPH_MODE_RESERVED1       = 0x00000007,
} DCP_OUTPUT_CSC_GRPH_MODE;

/*
 * DCP_DENORM_MODE enum
 */

typedef enum DCP_DENORM_MODE {
DCP_DENORM_MODE_UNITY                    = 0x00000000,
DCP_DENORM_MODE_6BIT                     = 0x00000001,
DCP_DENORM_MODE_8BIT                     = 0x00000002,
DCP_DENORM_MODE_10BIT                    = 0x00000003,
DCP_DENORM_MODE_11BIT                    = 0x00000004,
DCP_DENORM_MODE_12BIT                    = 0x00000005,
DCP_DENORM_MODE_RESERVED0                = 0x00000006,
DCP_DENORM_MODE_RESERVED1                = 0x00000007,
} DCP_DENORM_MODE;

/*
 * DCP_DENORM_14BIT_OUT enum
 */

typedef enum DCP_DENORM_14BIT_OUT {
DCP_DENORM_14BIT_OUT_FALSE               = 0x00000000,
DCP_DENORM_14BIT_OUT_TRUE                = 0x00000001,
} DCP_DENORM_14BIT_OUT;

/*
 * DCP_OUT_ROUND_TRUNC_MODE enum
 */

typedef enum DCP_OUT_ROUND_TRUNC_MODE {
DCP_OUT_ROUND_TRUNC_MODE_TRUNCATE_12     = 0x00000000,
DCP_OUT_ROUND_TRUNC_MODE_TRUNCATE_11     = 0x00000001,
DCP_OUT_ROUND_TRUNC_MODE_TRUNCATE_10     = 0x00000002,
DCP_OUT_ROUND_TRUNC_MODE_TRUNCATE_9      = 0x00000003,
DCP_OUT_ROUND_TRUNC_MODE_TRUNCATE_8      = 0x00000004,
DCP_OUT_ROUND_TRUNC_MODE_TRUNCATE_RESERVED  = 0x00000005,
DCP_OUT_ROUND_TRUNC_MODE_TRUNCATE_14     = 0x00000006,
DCP_OUT_ROUND_TRUNC_MODE_TRUNCATE_13     = 0x00000007,
DCP_OUT_ROUND_TRUNC_MODE_ROUND_12        = 0x00000008,
DCP_OUT_ROUND_TRUNC_MODE_ROUND_11        = 0x00000009,
DCP_OUT_ROUND_TRUNC_MODE_ROUND_10        = 0x0000000a,
DCP_OUT_ROUND_TRUNC_MODE_ROUND_9         = 0x0000000b,
DCP_OUT_ROUND_TRUNC_MODE_ROUND_8         = 0x0000000c,
DCP_OUT_ROUND_TRUNC_MODE_ROUND_RESERVED  = 0x0000000d,
DCP_OUT_ROUND_TRUNC_MODE_ROUND_14        = 0x0000000e,
DCP_OUT_ROUND_TRUNC_MODE_ROUND_13        = 0x0000000f,
} DCP_OUT_ROUND_TRUNC_MODE;

/*
 * DCP_KEY_MODE enum
 */

typedef enum DCP_KEY_MODE {
DCP_KEY_MODE_ALPHA0                      = 0x00000000,
DCP_KEY_MODE_ALPHA1                      = 0x00000001,
DCP_KEY_MODE_IN_RANGE_ALPHA1             = 0x00000002,
DCP_KEY_MODE_IN_RANGE_ALPHA0             = 0x00000003,
} DCP_KEY_MODE;

/*
 * DCP_GRPH_DEGAMMA_MODE enum
 */

typedef enum DCP_GRPH_DEGAMMA_MODE {
DCP_GRPH_DEGAMMA_MODE_BYPASS             = 0x00000000,
DCP_GRPH_DEGAMMA_MODE_ROMA               = 0x00000001,
DCP_GRPH_DEGAMMA_MODE_ROMB               = 0x00000002,
DCP_GRPH_DEGAMMA_MODE_RESERVED           = 0x00000003,
} DCP_GRPH_DEGAMMA_MODE;

/*
 * DCP_CURSOR_DEGAMMA_MODE enum
 */

typedef enum DCP_CURSOR_DEGAMMA_MODE {
DCP_CURSOR_DEGAMMA_MODE_BYPASS           = 0x00000000,
DCP_CURSOR_DEGAMMA_MODE_ROMA             = 0x00000001,
DCP_CURSOR_DEGAMMA_MODE_ROMB             = 0x00000002,
DCP_CURSOR_DEGAMMA_MODE_RESERVED         = 0x00000003,
} DCP_CURSOR_DEGAMMA_MODE;

/*
 * DCP_GRPH_GAMUT_REMAP_MODE enum
 */

typedef enum DCP_GRPH_GAMUT_REMAP_MODE {
DCP_GRPH_GAMUT_REMAP_MODE_BYPASS         = 0x00000000,
DCP_GRPH_GAMUT_REMAP_MODE_ROMA           = 0x00000001,
DCP_GRPH_GAMUT_REMAP_MODE_ROMB           = 0x00000002,
DCP_GRPH_GAMUT_REMAP_MODE_RESERVED       = 0x00000003,
} DCP_GRPH_GAMUT_REMAP_MODE;

/*
 * DCP_SPATIAL_DITHER_EN enum
 */

typedef enum DCP_SPATIAL_DITHER_EN {
DCP_SPATIAL_DITHER_EN_FALSE              = 0x00000000,
DCP_SPATIAL_DITHER_EN_TRUE               = 0x00000001,
} DCP_SPATIAL_DITHER_EN;

/*
 * DCP_SPATIAL_DITHER_MODE enum
 */

typedef enum DCP_SPATIAL_DITHER_MODE {
DCP_SPATIAL_DITHER_MODE_BYPASS           = 0x00000000,
DCP_SPATIAL_DITHER_MODE_ROMA             = 0x00000001,
DCP_SPATIAL_DITHER_MODE_ROMB             = 0x00000002,
DCP_SPATIAL_DITHER_MODE_RESERVED         = 0x00000003,
} DCP_SPATIAL_DITHER_MODE;

/*
 * DCP_SPATIAL_DITHER_DEPTH enum
 */

typedef enum DCP_SPATIAL_DITHER_DEPTH {
DCP_SPATIAL_DITHER_DEPTH_30BPP           = 0x00000000,
DCP_SPATIAL_DITHER_DEPTH_24BPP           = 0x00000001,
DCP_SPATIAL_DITHER_DEPTH_36BPP           = 0x00000002,
DCP_SPATIAL_DITHER_DEPTH_UNDEFINED       = 0x00000003,
} DCP_SPATIAL_DITHER_DEPTH;

/*
 * DCP_FRAME_RANDOM_ENABLE enum
 */

typedef enum DCP_FRAME_RANDOM_ENABLE {
DCP_FRAME_RANDOM_ENABLE_FALSE            = 0x00000000,
DCP_FRAME_RANDOM_ENABLE_TRUE             = 0x00000001,
} DCP_FRAME_RANDOM_ENABLE;

/*
 * DCP_RGB_RANDOM_ENABLE enum
 */

typedef enum DCP_RGB_RANDOM_ENABLE {
DCP_RGB_RANDOM_ENABLE_FALSE              = 0x00000000,
DCP_RGB_RANDOM_ENABLE_TRUE               = 0x00000001,
} DCP_RGB_RANDOM_ENABLE;

/*
 * DCP_HIGHPASS_RANDOM_ENABLE enum
 */

typedef enum DCP_HIGHPASS_RANDOM_ENABLE {
DCP_HIGHPASS_RANDOM_ENABLE_FALSE         = 0x00000000,
DCP_HIGHPASS_RANDOM_ENABLE_TRUE          = 0x00000001,
} DCP_HIGHPASS_RANDOM_ENABLE;

/*
 * DCP_CURSOR_EN enum
 */

typedef enum DCP_CURSOR_EN {
DCP_CURSOR_EN_FALSE                      = 0x00000000,
DCP_CURSOR_EN_TRUE                       = 0x00000001,
} DCP_CURSOR_EN;

/*
 * DCP_CUR_INV_TRANS_CLAMP enum
 */

typedef enum DCP_CUR_INV_TRANS_CLAMP {
DCP_CUR_INV_TRANS_CLAMP_FALSE            = 0x00000000,
DCP_CUR_INV_TRANS_CLAMP_TRUE             = 0x00000001,
} DCP_CUR_INV_TRANS_CLAMP;

/*
 * DCP_CURSOR_MODE enum
 */

typedef enum DCP_CURSOR_MODE {
DCP_CURSOR_MODE_MONO_2BPP                = 0x00000000,
DCP_CURSOR_MODE_24BPP_1BIT               = 0x00000001,
DCP_CURSOR_MODE_24BPP_8BIT_PREMULTI      = 0x00000002,
DCP_CURSOR_MODE_24BPP_8BIT_UNPREMULTI    = 0x00000003,
} DCP_CURSOR_MODE;

/*
 * DCP_CURSOR_MAX_OUTSTANDING_GROUP_NUM enum
 */

typedef enum DCP_CURSOR_MAX_OUTSTANDING_GROUP_NUM {
DCP_CURSOR_MAX_OUTSTANDING_GROUP_NUM_ONE  = 0x00000000,
DCP_CURSOR_MAX_OUTSTANDING_GROUP_NUM_TWO  = 0x00000001,
} DCP_CURSOR_MAX_OUTSTANDING_GROUP_NUM;

/*
 * DCP_CURSOR_2X_MAGNIFY enum
 */

typedef enum DCP_CURSOR_2X_MAGNIFY {
DCP_CURSOR_2X_MAGNIFY_FALSE              = 0x00000000,
DCP_CURSOR_2X_MAGNIFY_TRUE               = 0x00000001,
} DCP_CURSOR_2X_MAGNIFY;

/*
 * DCP_CURSOR_FORCE_MC_ON enum
 */

typedef enum DCP_CURSOR_FORCE_MC_ON {
DCP_CURSOR_FORCE_MC_ON_FALSE             = 0x00000000,
DCP_CURSOR_FORCE_MC_ON_TRUE              = 0x00000001,
} DCP_CURSOR_FORCE_MC_ON;

/*
 * DCP_CURSOR_URGENT_CONTROL enum
 */

typedef enum DCP_CURSOR_URGENT_CONTROL {
DCP_CURSOR_URGENT_CONTROL_MODE_0         = 0x00000000,
DCP_CURSOR_URGENT_CONTROL_MODE_1         = 0x00000001,
DCP_CURSOR_URGENT_CONTROL_MODE_2         = 0x00000002,
DCP_CURSOR_URGENT_CONTROL_MODE_3         = 0x00000003,
DCP_CURSOR_URGENT_CONTROL_MODE_4         = 0x00000004,
} DCP_CURSOR_URGENT_CONTROL;

/*
 * DCP_CURSOR_UPDATE_PENDING enum
 */

typedef enum DCP_CURSOR_UPDATE_PENDING {
DCP_CURSOR_UPDATE_PENDING_FALSE          = 0x00000000,
DCP_CURSOR_UPDATE_PENDING_TRUE           = 0x00000001,
} DCP_CURSOR_UPDATE_PENDING;

/*
 * DCP_CURSOR_UPDATE_TAKEN enum
 */

typedef enum DCP_CURSOR_UPDATE_TAKEN {
DCP_CURSOR_UPDATE_TAKEN_FALSE            = 0x00000000,
DCP_CURSOR_UPDATE_TAKEN_TRUE             = 0x00000001,
} DCP_CURSOR_UPDATE_TAKEN;

/*
 * DCP_CURSOR_UPDATE_LOCK enum
 */

typedef enum DCP_CURSOR_UPDATE_LOCK {
DCP_CURSOR_UPDATE_LOCK_FALSE             = 0x00000000,
DCP_CURSOR_UPDATE_LOCK_TRUE              = 0x00000001,
} DCP_CURSOR_UPDATE_LOCK;

/*
 * DCP_CURSOR_DISABLE_MULTIPLE_UPDATE enum
 */

typedef enum DCP_CURSOR_DISABLE_MULTIPLE_UPDATE {
DCP_CURSOR_DISABLE_MULTIPLE_UPDATE_FALSE  = 0x00000000,
DCP_CURSOR_DISABLE_MULTIPLE_UPDATE_TRUE  = 0x00000001,
} DCP_CURSOR_DISABLE_MULTIPLE_UPDATE;

/*
 * DCP_CURSOR_UPDATE_STEREO_MODE enum
 */

typedef enum DCP_CURSOR_UPDATE_STEREO_MODE {
DCP_CURSOR_UPDATE_STEREO_MODE_BOTH       = 0x00000000,
DCP_CURSOR_UPDATE_STEREO_MODE_SECONDARY_ONLY  = 0x00000001,
DCP_CURSOR_UPDATE_STEREO_MODE_UNDEFINED  = 0x00000002,
DCP_CURSOR_UPDATE_STEREO_MODE_PRIMARY_ONLY  = 0x00000003,
} DCP_CURSOR_UPDATE_STEREO_MODE;

/*
 * DCP_CUR2_INV_TRANS_CLAMP enum
 */

typedef enum DCP_CUR2_INV_TRANS_CLAMP {
DCP_CUR2_INV_TRANS_CLAMP_FALSE           = 0x00000000,
DCP_CUR2_INV_TRANS_CLAMP_TRUE            = 0x00000001,
} DCP_CUR2_INV_TRANS_CLAMP;

/*
 * DCP_CUR_REQUEST_FILTER_DIS enum
 */

typedef enum DCP_CUR_REQUEST_FILTER_DIS {
DCP_CUR_REQUEST_FILTER_DIS_FALSE         = 0x00000000,
DCP_CUR_REQUEST_FILTER_DIS_TRUE          = 0x00000001,
} DCP_CUR_REQUEST_FILTER_DIS;

/*
 * DCP_CURSOR_STEREO_EN enum
 */

typedef enum DCP_CURSOR_STEREO_EN {
DCP_CURSOR_STEREO_EN_FALSE               = 0x00000000,
DCP_CURSOR_STEREO_EN_TRUE                = 0x00000001,
} DCP_CURSOR_STEREO_EN;

/*
 * DCP_CURSOR_STEREO_OFFSET_YNX enum
 */

typedef enum DCP_CURSOR_STEREO_OFFSET_YNX {
DCP_CURSOR_STEREO_OFFSET_YNX_X_POSITION  = 0x00000000,
DCP_CURSOR_STEREO_OFFSET_YNX_Y_POSITION  = 0x00000001,
} DCP_CURSOR_STEREO_OFFSET_YNX;

/*
 * DCP_DC_LUT_RW_MODE enum
 */

typedef enum DCP_DC_LUT_RW_MODE {
DCP_DC_LUT_RW_MODE_256_ENTRY             = 0x00000000,
DCP_DC_LUT_RW_MODE_PWL                   = 0x00000001,
} DCP_DC_LUT_RW_MODE;

/*
 * DCP_DC_LUT_VGA_ACCESS_ENABLE enum
 */

typedef enum DCP_DC_LUT_VGA_ACCESS_ENABLE {
DCP_DC_LUT_VGA_ACCESS_ENABLE_FALSE       = 0x00000000,
DCP_DC_LUT_VGA_ACCESS_ENABLE_TRUE        = 0x00000001,
} DCP_DC_LUT_VGA_ACCESS_ENABLE;

/*
 * DCP_DC_LUT_AUTOFILL enum
 */

typedef enum DCP_DC_LUT_AUTOFILL {
DCP_DC_LUT_AUTOFILL_FALSE                = 0x00000000,
DCP_DC_LUT_AUTOFILL_TRUE                 = 0x00000001,
} DCP_DC_LUT_AUTOFILL;

/*
 * DCP_DC_LUT_AUTOFILL_DONE enum
 */

typedef enum DCP_DC_LUT_AUTOFILL_DONE {
DCP_DC_LUT_AUTOFILL_DONE_FALSE           = 0x00000000,
DCP_DC_LUT_AUTOFILL_DONE_TRUE            = 0x00000001,
} DCP_DC_LUT_AUTOFILL_DONE;

/*
 * DCP_DC_LUT_INC_B enum
 */

typedef enum DCP_DC_LUT_INC_B {
DCP_DC_LUT_INC_B_NA                      = 0x00000000,
DCP_DC_LUT_INC_B_2                       = 0x00000001,
DCP_DC_LUT_INC_B_4                       = 0x00000002,
DCP_DC_LUT_INC_B_8                       = 0x00000003,
DCP_DC_LUT_INC_B_16                      = 0x00000004,
DCP_DC_LUT_INC_B_32                      = 0x00000005,
DCP_DC_LUT_INC_B_64                      = 0x00000006,
DCP_DC_LUT_INC_B_128                     = 0x00000007,
DCP_DC_LUT_INC_B_256                     = 0x00000008,
DCP_DC_LUT_INC_B_512                     = 0x00000009,
} DCP_DC_LUT_INC_B;

/*
 * DCP_DC_LUT_DATA_B_SIGNED_EN enum
 */

typedef enum DCP_DC_LUT_DATA_B_SIGNED_EN {
DCP_DC_LUT_DATA_B_SIGNED_EN_FALSE        = 0x00000000,
DCP_DC_LUT_DATA_B_SIGNED_EN_TRUE         = 0x00000001,
} DCP_DC_LUT_DATA_B_SIGNED_EN;

/*
 * DCP_DC_LUT_DATA_B_FLOAT_POINT_EN enum
 */

typedef enum DCP_DC_LUT_DATA_B_FLOAT_POINT_EN {
DCP_DC_LUT_DATA_B_FLOAT_POINT_EN_FALSE   = 0x00000000,
DCP_DC_LUT_DATA_B_FLOAT_POINT_EN_TRUE    = 0x00000001,
} DCP_DC_LUT_DATA_B_FLOAT_POINT_EN;

/*
 * DCP_DC_LUT_DATA_B_FORMAT enum
 */

typedef enum DCP_DC_LUT_DATA_B_FORMAT {
DCP_DC_LUT_DATA_B_FORMAT_U0P10           = 0x00000000,
DCP_DC_LUT_DATA_B_FORMAT_S1P10           = 0x00000001,
DCP_DC_LUT_DATA_B_FORMAT_U1P11           = 0x00000002,
DCP_DC_LUT_DATA_B_FORMAT_U0P12           = 0x00000003,
} DCP_DC_LUT_DATA_B_FORMAT;

/*
 * DCP_DC_LUT_INC_G enum
 */

typedef enum DCP_DC_LUT_INC_G {
DCP_DC_LUT_INC_G_NA                      = 0x00000000,
DCP_DC_LUT_INC_G_2                       = 0x00000001,
DCP_DC_LUT_INC_G_4                       = 0x00000002,
DCP_DC_LUT_INC_G_8                       = 0x00000003,
DCP_DC_LUT_INC_G_16                      = 0x00000004,
DCP_DC_LUT_INC_G_32                      = 0x00000005,
DCP_DC_LUT_INC_G_64                      = 0x00000006,
DCP_DC_LUT_INC_G_128                     = 0x00000007,
DCP_DC_LUT_INC_G_256                     = 0x00000008,
DCP_DC_LUT_INC_G_512                     = 0x00000009,
} DCP_DC_LUT_INC_G;

/*
 * DCP_DC_LUT_DATA_G_SIGNED_EN enum
 */

typedef enum DCP_DC_LUT_DATA_G_SIGNED_EN {
DCP_DC_LUT_DATA_G_SIGNED_EN_FALSE        = 0x00000000,
DCP_DC_LUT_DATA_G_SIGNED_EN_TRUE         = 0x00000001,
} DCP_DC_LUT_DATA_G_SIGNED_EN;

/*
 * DCP_DC_LUT_DATA_G_FLOAT_POINT_EN enum
 */

typedef enum DCP_DC_LUT_DATA_G_FLOAT_POINT_EN {
DCP_DC_LUT_DATA_G_FLOAT_POINT_EN_FALSE   = 0x00000000,
DCP_DC_LUT_DATA_G_FLOAT_POINT_EN_TRUE    = 0x00000001,
} DCP_DC_LUT_DATA_G_FLOAT_POINT_EN;

/*
 * DCP_DC_LUT_DATA_G_FORMAT enum
 */

typedef enum DCP_DC_LUT_DATA_G_FORMAT {
DCP_DC_LUT_DATA_G_FORMAT_U0P10           = 0x00000000,
DCP_DC_LUT_DATA_G_FORMAT_S1P10           = 0x00000001,
DCP_DC_LUT_DATA_G_FORMAT_U1P11           = 0x00000002,
DCP_DC_LUT_DATA_G_FORMAT_U0P12           = 0x00000003,
} DCP_DC_LUT_DATA_G_FORMAT;

/*
 * DCP_DC_LUT_INC_R enum
 */

typedef enum DCP_DC_LUT_INC_R {
DCP_DC_LUT_INC_R_NA                      = 0x00000000,
DCP_DC_LUT_INC_R_2                       = 0x00000001,
DCP_DC_LUT_INC_R_4                       = 0x00000002,
DCP_DC_LUT_INC_R_8                       = 0x00000003,
DCP_DC_LUT_INC_R_16                      = 0x00000004,
DCP_DC_LUT_INC_R_32                      = 0x00000005,
DCP_DC_LUT_INC_R_64                      = 0x00000006,
DCP_DC_LUT_INC_R_128                     = 0x00000007,
DCP_DC_LUT_INC_R_256                     = 0x00000008,
DCP_DC_LUT_INC_R_512                     = 0x00000009,
} DCP_DC_LUT_INC_R;

/*
 * DCP_DC_LUT_DATA_R_SIGNED_EN enum
 */

typedef enum DCP_DC_LUT_DATA_R_SIGNED_EN {
DCP_DC_LUT_DATA_R_SIGNED_EN_FALSE        = 0x00000000,
DCP_DC_LUT_DATA_R_SIGNED_EN_TRUE         = 0x00000001,
} DCP_DC_LUT_DATA_R_SIGNED_EN;

/*
 * DCP_DC_LUT_DATA_R_FLOAT_POINT_EN enum
 */

typedef enum DCP_DC_LUT_DATA_R_FLOAT_POINT_EN {
DCP_DC_LUT_DATA_R_FLOAT_POINT_EN_FALSE   = 0x00000000,
DCP_DC_LUT_DATA_R_FLOAT_POINT_EN_TRUE    = 0x00000001,
} DCP_DC_LUT_DATA_R_FLOAT_POINT_EN;

/*
 * DCP_DC_LUT_DATA_R_FORMAT enum
 */

typedef enum DCP_DC_LUT_DATA_R_FORMAT {
DCP_DC_LUT_DATA_R_FORMAT_U0P10           = 0x00000000,
DCP_DC_LUT_DATA_R_FORMAT_S1P10           = 0x00000001,
DCP_DC_LUT_DATA_R_FORMAT_U1P11           = 0x00000002,
DCP_DC_LUT_DATA_R_FORMAT_U0P12           = 0x00000003,
} DCP_DC_LUT_DATA_R_FORMAT;

/*
 * DCP_CRC_ENABLE enum
 */

typedef enum DCP_CRC_ENABLE {
DCP_CRC_ENABLE_FALSE                     = 0x00000000,
DCP_CRC_ENABLE_TRUE                      = 0x00000001,
} DCP_CRC_ENABLE;

/*
 * DCP_CRC_SOURCE_SEL enum
 */

typedef enum DCP_CRC_SOURCE_SEL {
DCP_CRC_SOURCE_SEL_OUTPUT_PIX            = 0x00000000,
DCP_CRC_SOURCE_SEL_INPUT_L32             = 0x00000001,
DCP_CRC_SOURCE_SEL_INPUT_H32             = 0x00000002,
DCP_CRC_SOURCE_SEL_OUTPUT_CNTL           = 0x00000004,
} DCP_CRC_SOURCE_SEL;

/*
 * DCP_CRC_LINE_SEL enum
 */

typedef enum DCP_CRC_LINE_SEL {
DCP_CRC_LINE_SEL_RESERVED                = 0x00000000,
DCP_CRC_LINE_SEL_EVEN                    = 0x00000001,
DCP_CRC_LINE_SEL_ODD                     = 0x00000002,
DCP_CRC_LINE_SEL_BOTH                    = 0x00000003,
} DCP_CRC_LINE_SEL;

/*
 * DCP_GRPH_FLIP_RATE enum
 */

typedef enum DCP_GRPH_FLIP_RATE {
DCP_GRPH_FLIP_RATE_1FRAME                = 0x00000000,
DCP_GRPH_FLIP_RATE_2FRAME                = 0x00000001,
DCP_GRPH_FLIP_RATE_3FRAME                = 0x00000002,
DCP_GRPH_FLIP_RATE_4FRAME                = 0x00000003,
DCP_GRPH_FLIP_RATE_5FRAME                = 0x00000004,
DCP_GRPH_FLIP_RATE_6FRAME                = 0x00000005,
DCP_GRPH_FLIP_RATE_7FRAME                = 0x00000006,
DCP_GRPH_FLIP_RATE_8FRAME                = 0x00000007,
} DCP_GRPH_FLIP_RATE;

/*
 * DCP_GRPH_FLIP_RATE_ENABLE enum
 */

typedef enum DCP_GRPH_FLIP_RATE_ENABLE {
DCP_GRPH_FLIP_RATE_ENABLE_FALSE          = 0x00000000,
DCP_GRPH_FLIP_RATE_ENABLE_TRUE           = 0x00000001,
} DCP_GRPH_FLIP_RATE_ENABLE;

/*
 * DCP_GSL0_EN enum
 */

typedef enum DCP_GSL0_EN {
DCP_GSL0_EN_FALSE                        = 0x00000000,
DCP_GSL0_EN_TRUE                         = 0x00000001,
} DCP_GSL0_EN;

/*
 * DCP_GSL1_EN enum
 */

typedef enum DCP_GSL1_EN {
DCP_GSL1_EN_FALSE                        = 0x00000000,
DCP_GSL1_EN_TRUE                         = 0x00000001,
} DCP_GSL1_EN;

/*
 * DCP_GSL2_EN enum
 */

typedef enum DCP_GSL2_EN {
DCP_GSL2_EN_FALSE                        = 0x00000000,
DCP_GSL2_EN_TRUE                         = 0x00000001,
} DCP_GSL2_EN;

/*
 * DCP_GSL_MASTER_EN enum
 */

typedef enum DCP_GSL_MASTER_EN {
DCP_GSL_MASTER_EN_FALSE                  = 0x00000000,
DCP_GSL_MASTER_EN_TRUE                   = 0x00000001,
} DCP_GSL_MASTER_EN;

/*
 * DCP_GSL_XDMA_GROUP enum
 */

typedef enum DCP_GSL_XDMA_GROUP {
DCP_GSL_XDMA_GROUP_VSYNC                 = 0x00000000,
DCP_GSL_XDMA_GROUP_HSYNC0                = 0x00000001,
DCP_GSL_XDMA_GROUP_HSYNC1                = 0x00000002,
DCP_GSL_XDMA_GROUP_HSYNC2                = 0x00000003,
} DCP_GSL_XDMA_GROUP;

/*
 * DCP_GSL_XDMA_GROUP_UNDERFLOW_EN enum
 */

typedef enum DCP_GSL_XDMA_GROUP_UNDERFLOW_EN {
DCP_GSL_XDMA_GROUP_UNDERFLOW_EN_FALSE    = 0x00000000,
DCP_GSL_XDMA_GROUP_UNDERFLOW_EN_TRUE     = 0x00000001,
} DCP_GSL_XDMA_GROUP_UNDERFLOW_EN;

/*
 * DCP_GSL_SYNC_SOURCE enum
 */

typedef enum DCP_GSL_SYNC_SOURCE {
DCP_GSL_SYNC_SOURCE_FLIP                 = 0x00000000,
DCP_GSL_SYNC_SOURCE_PHASE0               = 0x00000001,
DCP_GSL_SYNC_SOURCE_RESET                = 0x00000002,
DCP_GSL_SYNC_SOURCE_PHASE1               = 0x00000003,
} DCP_GSL_SYNC_SOURCE;

/*
 * DCP_GSL_USE_CHECKPOINT_WINDOW_IN_VSYNC enum
 */

typedef enum DCP_GSL_USE_CHECKPOINT_WINDOW_IN_VSYNC {
DCP_GSL_USE_CHECKPOINT_WINDOW_IN_VSYNC_DIS  = 0x00000000,
DCP_GSL_USE_CHECKPOINT_WINDOW_IN_VSYNC_EN  = 0x00000001,
} DCP_GSL_USE_CHECKPOINT_WINDOW_IN_VSYNC;

/*
 * DCP_GSL_DELAY_SURFACE_UPDATE_PENDING enum
 */

typedef enum DCP_GSL_DELAY_SURFACE_UPDATE_PENDING {
DCP_GSL_DELAY_SURFACE_UPDATE_PENDING_FALSE  = 0x00000000,
DCP_GSL_DELAY_SURFACE_UPDATE_PENDING_TRUE  = 0x00000001,
} DCP_GSL_DELAY_SURFACE_UPDATE_PENDING;

/*
 * DCP_TEST_DEBUG_WRITE_EN enum
 */

typedef enum DCP_TEST_DEBUG_WRITE_EN {
DCP_TEST_DEBUG_WRITE_EN_FALSE            = 0x00000000,
DCP_TEST_DEBUG_WRITE_EN_TRUE             = 0x00000001,
} DCP_TEST_DEBUG_WRITE_EN;

/*
 * DCP_GRPH_STEREOSYNC_FLIP_EN enum
 */

typedef enum DCP_GRPH_STEREOSYNC_FLIP_EN {
DCP_GRPH_STEREOSYNC_FLIP_EN_FALSE        = 0x00000000,
DCP_GRPH_STEREOSYNC_FLIP_EN_TRUE         = 0x00000001,
} DCP_GRPH_STEREOSYNC_FLIP_EN;

/*
 * DCP_GRPH_STEREOSYNC_FLIP_MODE enum
 */

typedef enum DCP_GRPH_STEREOSYNC_FLIP_MODE {
DCP_GRPH_STEREOSYNC_FLIP_MODE_FLIP       = 0x00000000,
DCP_GRPH_STEREOSYNC_FLIP_MODE_PHASE0     = 0x00000001,
DCP_GRPH_STEREOSYNC_FLIP_MODE_RESET      = 0x00000002,
DCP_GRPH_STEREOSYNC_FLIP_MODE_PHASE1     = 0x00000003,
} DCP_GRPH_STEREOSYNC_FLIP_MODE;

/*
 * DCP_GRPH_STEREOSYNC_SELECT_DISABLE enum
 */

typedef enum DCP_GRPH_STEREOSYNC_SELECT_DISABLE {
DCP_GRPH_STEREOSYNC_SELECT_DISABLE_FALSE  = 0x00000000,
DCP_GRPH_STEREOSYNC_SELECT_DISABLE_TRUE  = 0x00000001,
} DCP_GRPH_STEREOSYNC_SELECT_DISABLE;

/*
 * DCP_GRPH_ROTATION_ANGLE enum
 */

typedef enum DCP_GRPH_ROTATION_ANGLE {
DCP_GRPH_ROTATION_ANGLE_0                = 0x00000000,
DCP_GRPH_ROTATION_ANGLE_90               = 0x00000001,
DCP_GRPH_ROTATION_ANGLE_180              = 0x00000002,
DCP_GRPH_ROTATION_ANGLE_270              = 0x00000003,
} DCP_GRPH_ROTATION_ANGLE;

/*
 * DCP_GRPH_XDMA_CACHE_UNDERFLOW_CNT_EN enum
 */

typedef enum DCP_GRPH_XDMA_CACHE_UNDERFLOW_CNT_EN {
DCP_GRPH_XDMA_CACHE_UNDERFLOW_CNT_EN_FALSE  = 0x00000000,
DCP_GRPH_XDMA_CACHE_UNDERFLOW_CNT_EN_TRUE  = 0x00000001,
} DCP_GRPH_XDMA_CACHE_UNDERFLOW_CNT_EN;

/*
 * DCP_GRPH_XDMA_CACHE_UNDERFLOW_CNT_MODE enum
 */

typedef enum DCP_GRPH_XDMA_CACHE_UNDERFLOW_CNT_MODE {
DCP_GRPH_XDMA_CACHE_UNDERFLOW_CNT_MODE_RELY_NUM  = 0x00000000,
DCP_GRPH_XDMA_CACHE_UNDERFLOW_CNT_MODE_RELY_ENABLE  = 0x00000001,
} DCP_GRPH_XDMA_CACHE_UNDERFLOW_CNT_MODE;

/*
 * DCP_GRPH_REGAMMA_MODE enum
 */

typedef enum DCP_GRPH_REGAMMA_MODE {
DCP_GRPH_REGAMMA_MODE_BYPASS             = 0x00000000,
DCP_GRPH_REGAMMA_MODE_SRGB               = 0x00000001,
DCP_GRPH_REGAMMA_MODE_XVYCC              = 0x00000002,
DCP_GRPH_REGAMMA_MODE_PROGA              = 0x00000003,
DCP_GRPH_REGAMMA_MODE_PROGB              = 0x00000004,
} DCP_GRPH_REGAMMA_MODE;

/*
 * DCP_ALPHA_ROUND_TRUNC_MODE enum
 */

typedef enum DCP_ALPHA_ROUND_TRUNC_MODE {
DCP_ALPHA_ROUND_TRUNC_MODE_ROUND         = 0x00000000,
DCP_ALPHA_ROUND_TRUNC_MODE_TRUNC         = 0x00000001,
} DCP_ALPHA_ROUND_TRUNC_MODE;

/*
 * DCP_CURSOR_ALPHA_BLND_ENA enum
 */

typedef enum DCP_CURSOR_ALPHA_BLND_ENA {
DCP_CURSOR_ALPHA_BLND_ENA_FALSE          = 0x00000000,
DCP_CURSOR_ALPHA_BLND_ENA_TRUE           = 0x00000001,
} DCP_CURSOR_ALPHA_BLND_ENA;

/*
 * DCP_GRPH_XDMA_CACHE_UNDERFLOW_FRAME_MASK enum
 */

typedef enum DCP_GRPH_XDMA_CACHE_UNDERFLOW_FRAME_MASK {
DCP_GRPH_XDMA_CACHE_UNDERFLOW_FRAME_MASK_FALSE  = 0x00000000,
DCP_GRPH_XDMA_CACHE_UNDERFLOW_FRAME_MASK_TRUE  = 0x00000001,
} DCP_GRPH_XDMA_CACHE_UNDERFLOW_FRAME_MASK;

/*
 * DCP_GRPH_XDMA_CACHE_UNDERFLOW_FRAME_ACK enum
 */

typedef enum DCP_GRPH_XDMA_CACHE_UNDERFLOW_FRAME_ACK {
DCP_GRPH_XDMA_CACHE_UNDERFLOW_FRAME_ACK_FALSE = 0x00000000,
DCP_GRPH_XDMA_CACHE_UNDERFLOW_FRAME_ACK_TRUE  = 0x00000001,
} DCP_GRPH_XDMA_CACHE_UNDERFLOW_FRAME_ACK;

/*
 * DCP_GRPH_XDMA_CACHE_UNDERFLOW_INT_MASK enum
 */

typedef enum DCP_GRPH_XDMA_CACHE_UNDERFLOW_INT_MASK {
DCP_GRPH_XDMA_CACHE_UNDERFLOW_INT_MASK_FALSE  = 0x00000000,
DCP_GRPH_XDMA_CACHE_UNDERFLOW_INT_MASK_TRUE  = 0x00000001,
} DCP_GRPH_XDMA_CACHE_UNDERFLOW_INT_MASK;

/*
 * DCP_GRPH_XDMA_CACHE_UNDERFLOW_INT_ACK enum
 */

typedef enum DCP_GRPH_XDMA_CACHE_UNDERFLOW_INT_ACK {
DCP_GRPH_XDMA_CACHE_UNDERFLOW_INT_ACK_FALSE = 0x00000000,
DCP_GRPH_XDMA_CACHE_UNDERFLOW_INT_ACK_TRUE  = 0x00000001,
} DCP_GRPH_XDMA_CACHE_UNDERFLOW_INT_ACK;

/*
 * DCP_GRPH_SURFACE_COUNTER_EN enum
 */

typedef enum DCP_GRPH_SURFACE_COUNTER_EN {
DCP_GRPH_SURFACE_COUNTER_EN_DISABLE      = 0x00000000,
DCP_GRPH_SURFACE_COUNTER_EN_ENABLE       = 0x00000001,
} DCP_GRPH_SURFACE_COUNTER_EN;

/*
 * DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT enum
 */

typedef enum DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT {
DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT_0  = 0x00000000,
DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT_1  = 0x00000001,
DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT_2  = 0x00000002,
DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT_3  = 0x00000003,
DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT_4  = 0x00000004,
DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT_5  = 0x00000005,
DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT_6  = 0x00000006,
DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT_7  = 0x00000007,
DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT_8  = 0x00000008,
DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT_9  = 0x00000009,
DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT_10  = 0x0000000a,
DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT_11  = 0x0000000b,
} DCP_GRPH_SURFACE_COUNTER_EVENT_SELECT;

/*
 * DCP_GRPH_SURFACE_COUNTER_ERR_WRAP_OCCURED enum
 */

typedef enum DCP_GRPH_SURFACE_COUNTER_ERR_WRAP_OCCURED {
DCP_GRPH_SURFACE_COUNTER_ERR_WRAP_OCCURED_NO  = 0x00000000,
DCP_GRPH_SURFACE_COUNTER_ERR_WRAP_OCCURED_YES  = 0x00000001,
} DCP_GRPH_SURFACE_COUNTER_ERR_WRAP_OCCURED;

/*
 * DCP_GRPH_XDMA_FLIP_TYPE_CLEAR enum
 */

typedef enum DCP_GRPH_XDMA_FLIP_TYPE_CLEAR {
DCP_GRPH_XDMA_FLIP_TYPE_CLEAR_DISABLE    = 0x00000000,
DCP_GRPH_XDMA_FLIP_TYPE_CLEAR_ENABLE     = 0x00000001,
} DCP_GRPH_XDMA_FLIP_TYPE_CLEAR;

/*
 * DCP_GRPH_XDMA_DRR_MODE_ENABLE enum
 */

typedef enum DCP_GRPH_XDMA_DRR_MODE_ENABLE {
DCP_GRPH_XDMA_DRR_MODE_ENABLE_DISABLE    = 0x00000000,
DCP_GRPH_XDMA_DRR_MODE_ENABLE_ENABLE     = 0x00000001,
} DCP_GRPH_XDMA_DRR_MODE_ENABLE;

/*
 * DCP_GRPH_XDMA_MULTIFLIP_ENABLE enum
 */

typedef enum DCP_GRPH_XDMA_MULTIFLIP_ENABLE {
DCP_GRPH_XDMA_MULTIFLIP_ENABLE_DISABLE   = 0x00000000,
DCP_GRPH_XDMA_MULTIFLIP_ENABLE_ENABLE    = 0x00000001,
} DCP_GRPH_XDMA_MULTIFLIP_ENABLE;

/*
 * DCP_GRPH_XDMA_FLIP_TIMEOUT_MASK enum
 */

typedef enum DCP_GRPH_XDMA_FLIP_TIMEOUT_MASK {
DCP_GRPH_XDMA_FLIP_TIMEOUT_MASK_FALSE    = 0x00000000,
DCP_GRPH_XDMA_FLIP_TIMEOUT_MASK_TRUE     = 0x00000001,
} DCP_GRPH_XDMA_FLIP_TIMEOUT_MASK;

/*
 * DCP_GRPH_XDMA_FLIP_TIMEOUT_ACK enum
 */

typedef enum DCP_GRPH_XDMA_FLIP_TIMEOUT_ACK {
DCP_GRPH_XDMA_FLIP_TIMEOUT_ACK_FALSE     = 0x00000000,
DCP_GRPH_XDMA_FLIP_TIMEOUT_ACK_TRUE      = 0x00000001,
} DCP_GRPH_XDMA_FLIP_TIMEOUT_ACK;

/*******************************************************
 * DC_PERFMON Enums
 *******************************************************/

/*
 * PERFCOUNTER_CVALUE_SEL enum
 */

typedef enum PERFCOUNTER_CVALUE_SEL {
PERFCOUNTER_CVALUE_SEL_47_0              = 0x00000000,
PERFCOUNTER_CVALUE_SEL_15_0              = 0x00000001,
PERFCOUNTER_CVALUE_SEL_31_16             = 0x00000002,
PERFCOUNTER_CVALUE_SEL_47_32             = 0x00000003,
PERFCOUNTER_CVALUE_SEL_11_0              = 0x00000004,
PERFCOUNTER_CVALUE_SEL_23_12             = 0x00000005,
PERFCOUNTER_CVALUE_SEL_35_24             = 0x00000006,
PERFCOUNTER_CVALUE_SEL_47_36             = 0x00000007,
} PERFCOUNTER_CVALUE_SEL;

/*
 * PERFCOUNTER_INC_MODE enum
 */

typedef enum PERFCOUNTER_INC_MODE {
PERFCOUNTER_INC_MODE_MULTI_BIT           = 0x00000000,
PERFCOUNTER_INC_MODE_BOTH_EDGE           = 0x00000001,
PERFCOUNTER_INC_MODE_LSB                 = 0x00000002,
PERFCOUNTER_INC_MODE_POS_EDGE            = 0x00000003,
PERFCOUNTER_INC_MODE_NEG_EDGE            = 0x00000004,
} PERFCOUNTER_INC_MODE;

/*
 * PERFCOUNTER_HW_CNTL_SEL enum
 */

typedef enum PERFCOUNTER_HW_CNTL_SEL {
PERFCOUNTER_HW_CNTL_SEL_RUNEN            = 0x00000000,
PERFCOUNTER_HW_CNTL_SEL_CNTOFF           = 0x00000001,
} PERFCOUNTER_HW_CNTL_SEL;

/*
 * PERFCOUNTER_RUNEN_MODE enum
 */

typedef enum PERFCOUNTER_RUNEN_MODE {
PERFCOUNTER_RUNEN_MODE_LEVEL             = 0x00000000,
PERFCOUNTER_RUNEN_MODE_EDGE              = 0x00000001,
} PERFCOUNTER_RUNEN_MODE;

/*
 * PERFCOUNTER_CNTOFF_START_DIS enum
 */

typedef enum PERFCOUNTER_CNTOFF_START_DIS {
PERFCOUNTER_CNTOFF_START_ENABLE          = 0x00000000,
PERFCOUNTER_CNTOFF_START_DISABLE         = 0x00000001,
} PERFCOUNTER_CNTOFF_START_DIS;

/*
 * PERFCOUNTER_RESTART_EN enum
 */

typedef enum PERFCOUNTER_RESTART_EN {
PERFCOUNTER_RESTART_DISABLE              = 0x00000000,
PERFCOUNTER_RESTART_ENABLE               = 0x00000001,
} PERFCOUNTER_RESTART_EN;

/*
 * PERFCOUNTER_INT_EN enum
 */

typedef enum PERFCOUNTER_INT_EN {
PERFCOUNTER_INT_DISABLE                  = 0x00000000,
PERFCOUNTER_INT_ENABLE                   = 0x00000001,
} PERFCOUNTER_INT_EN;

/*
 * PERFCOUNTER_OFF_MASK enum
 */

typedef enum PERFCOUNTER_OFF_MASK {
PERFCOUNTER_OFF_MASK_DISABLE             = 0x00000000,
PERFCOUNTER_OFF_MASK_ENABLE              = 0x00000001,
} PERFCOUNTER_OFF_MASK;

/*
 * PERFCOUNTER_ACTIVE enum
 */

typedef enum PERFCOUNTER_ACTIVE {
PERFCOUNTER_IS_IDLE                      = 0x00000000,
PERFCOUNTER_IS_ACTIVE                    = 0x00000001,
} PERFCOUNTER_ACTIVE;

/*
 * PERFCOUNTER_INT_TYPE enum
 */

typedef enum PERFCOUNTER_INT_TYPE {
PERFCOUNTER_INT_TYPE_LEVEL               = 0x00000000,
PERFCOUNTER_INT_TYPE_PULSE               = 0x00000001,
} PERFCOUNTER_INT_TYPE;

/*
 * PERFCOUNTER_COUNTED_VALUE_TYPE enum
 */

typedef enum PERFCOUNTER_COUNTED_VALUE_TYPE {
PERFCOUNTER_COUNTED_VALUE_TYPE_ACC       = 0x00000000,
PERFCOUNTER_COUNTED_VALUE_TYPE_MAX       = 0x00000001,
PERFCOUNTER_COUNTED_VALUE_TYPE_MIN       = 0x00000002,
} PERFCOUNTER_COUNTED_VALUE_TYPE;

/*
 * PERFCOUNTER_CNTL_SEL enum
 */

typedef enum PERFCOUNTER_CNTL_SEL {
PERFCOUNTER_CNTL_SEL_0                   = 0x00000000,
PERFCOUNTER_CNTL_SEL_1                   = 0x00000001,
PERFCOUNTER_CNTL_SEL_2                   = 0x00000002,
PERFCOUNTER_CNTL_SEL_3                   = 0x00000003,
PERFCOUNTER_CNTL_SEL_4                   = 0x00000004,
PERFCOUNTER_CNTL_SEL_5                   = 0x00000005,
PERFCOUNTER_CNTL_SEL_6                   = 0x00000006,
PERFCOUNTER_CNTL_SEL_7                   = 0x00000007,
} PERFCOUNTER_CNTL_SEL;

/*
 * PERFCOUNTER_CNT0_STATE enum
 */

typedef enum PERFCOUNTER_CNT0_STATE {
PERFCOUNTER_CNT0_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT0_STATE_START             = 0x00000001,
PERFCOUNTER_CNT0_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT0_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT0_STATE;

/*
 * PERFCOUNTER_STATE_SEL0 enum
 */

typedef enum PERFCOUNTER_STATE_SEL0 {
PERFCOUNTER_STATE_SEL0_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL0_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL0;

/*
 * PERFCOUNTER_CNT1_STATE enum
 */

typedef enum PERFCOUNTER_CNT1_STATE {
PERFCOUNTER_CNT1_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT1_STATE_START             = 0x00000001,
PERFCOUNTER_CNT1_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT1_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT1_STATE;

/*
 * PERFCOUNTER_STATE_SEL1 enum
 */

typedef enum PERFCOUNTER_STATE_SEL1 {
PERFCOUNTER_STATE_SEL1_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL1_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL1;

/*
 * PERFCOUNTER_CNT2_STATE enum
 */

typedef enum PERFCOUNTER_CNT2_STATE {
PERFCOUNTER_CNT2_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT2_STATE_START             = 0x00000001,
PERFCOUNTER_CNT2_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT2_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT2_STATE;

/*
 * PERFCOUNTER_STATE_SEL2 enum
 */

typedef enum PERFCOUNTER_STATE_SEL2 {
PERFCOUNTER_STATE_SEL2_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL2_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL2;

/*
 * PERFCOUNTER_CNT3_STATE enum
 */

typedef enum PERFCOUNTER_CNT3_STATE {
PERFCOUNTER_CNT3_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT3_STATE_START             = 0x00000001,
PERFCOUNTER_CNT3_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT3_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT3_STATE;

/*
 * PERFCOUNTER_STATE_SEL3 enum
 */

typedef enum PERFCOUNTER_STATE_SEL3 {
PERFCOUNTER_STATE_SEL3_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL3_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL3;

/*
 * PERFCOUNTER_CNT4_STATE enum
 */

typedef enum PERFCOUNTER_CNT4_STATE {
PERFCOUNTER_CNT4_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT4_STATE_START             = 0x00000001,
PERFCOUNTER_CNT4_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT4_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT4_STATE;

/*
 * PERFCOUNTER_STATE_SEL4 enum
 */

typedef enum PERFCOUNTER_STATE_SEL4 {
PERFCOUNTER_STATE_SEL4_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL4_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL4;

/*
 * PERFCOUNTER_CNT5_STATE enum
 */

typedef enum PERFCOUNTER_CNT5_STATE {
PERFCOUNTER_CNT5_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT5_STATE_START             = 0x00000001,
PERFCOUNTER_CNT5_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT5_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT5_STATE;

/*
 * PERFCOUNTER_STATE_SEL5 enum
 */

typedef enum PERFCOUNTER_STATE_SEL5 {
PERFCOUNTER_STATE_SEL5_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL5_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL5;

/*
 * PERFCOUNTER_CNT6_STATE enum
 */

typedef enum PERFCOUNTER_CNT6_STATE {
PERFCOUNTER_CNT6_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT6_STATE_START             = 0x00000001,
PERFCOUNTER_CNT6_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT6_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT6_STATE;

/*
 * PERFCOUNTER_STATE_SEL6 enum
 */

typedef enum PERFCOUNTER_STATE_SEL6 {
PERFCOUNTER_STATE_SEL6_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL6_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL6;

/*
 * PERFCOUNTER_CNT7_STATE enum
 */

typedef enum PERFCOUNTER_CNT7_STATE {
PERFCOUNTER_CNT7_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT7_STATE_START             = 0x00000001,
PERFCOUNTER_CNT7_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT7_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT7_STATE;

/*
 * PERFCOUNTER_STATE_SEL7 enum
 */

typedef enum PERFCOUNTER_STATE_SEL7 {
PERFCOUNTER_STATE_SEL7_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL7_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL7;

/*
 * PERFMON_STATE enum
 */

typedef enum PERFMON_STATE {
PERFMON_STATE_RESET                      = 0x00000000,
PERFMON_STATE_START                      = 0x00000001,
PERFMON_STATE_FREEZE                     = 0x00000002,
PERFMON_STATE_HW                         = 0x00000003,
} PERFMON_STATE;

/*
 * PERFMON_CNTOFF_AND_OR enum
 */

typedef enum PERFMON_CNTOFF_AND_OR {
PERFMON_CNTOFF_OR                        = 0x00000000,
PERFMON_CNTOFF_AND                       = 0x00000001,
} PERFMON_CNTOFF_AND_OR;

/*
 * PERFMON_CNTOFF_INT_EN enum
 */

typedef enum PERFMON_CNTOFF_INT_EN {
PERFMON_CNTOFF_INT_DISABLE               = 0x00000000,
PERFMON_CNTOFF_INT_ENABLE                = 0x00000001,
} PERFMON_CNTOFF_INT_EN;

/*
 * PERFMON_CNTOFF_INT_TYPE enum
 */

typedef enum PERFMON_CNTOFF_INT_TYPE {
PERFMON_CNTOFF_INT_TYPE_LEVEL            = 0x00000000,
PERFMON_CNTOFF_INT_TYPE_PULSE            = 0x00000001,
} PERFMON_CNTOFF_INT_TYPE;

/*******************************************************
 * SCL Enums
 *******************************************************/

/*
 * SCL_C_RAM_TAP_PAIR_IDX enum
 */

typedef enum SCL_C_RAM_TAP_PAIR_IDX {
SCL_C_RAM_TAP_PAIR_ID0                   = 0x00000000,
SCL_C_RAM_TAP_PAIR_ID1                   = 0x00000001,
SCL_C_RAM_TAP_PAIR_ID2                   = 0x00000002,
SCL_C_RAM_TAP_PAIR_ID3                   = 0x00000003,
SCL_C_RAM_TAP_PAIR_ID4                   = 0x00000004,
} SCL_C_RAM_TAP_PAIR_IDX;

/*
 * SCL_C_RAM_PHASE enum
 */

typedef enum SCL_C_RAM_PHASE {
SCL_C_RAM_PHASE_0                        = 0x00000000,
SCL_C_RAM_PHASE_1                        = 0x00000001,
SCL_C_RAM_PHASE_2                        = 0x00000002,
SCL_C_RAM_PHASE_3                        = 0x00000003,
SCL_C_RAM_PHASE_4                        = 0x00000004,
SCL_C_RAM_PHASE_5                        = 0x00000005,
SCL_C_RAM_PHASE_6                        = 0x00000006,
SCL_C_RAM_PHASE_7                        = 0x00000007,
SCL_C_RAM_PHASE_8                        = 0x00000008,
} SCL_C_RAM_PHASE;

/*
 * SCL_C_RAM_FILTER_TYPE enum
 */

typedef enum SCL_C_RAM_FILTER_TYPE {
SCL_C_RAM_FILTER_TYPE_VERT_LUMA_RGB_LUT  = 0x00000000,
SCL_C_RAM_FILTER_TYPE_VERT_CHROMA_LUT    = 0x00000001,
SCL_C_RAM_FILTER_TYPE_HORI_LUMA_RGB_LUT  = 0x00000002,
SCL_C_RAM_FILTER_TYPE_HORI_CHROMA_LUT    = 0x00000003,
} SCL_C_RAM_FILTER_TYPE;

/*
 * SCL_MODE_SEL enum
 */

typedef enum SCL_MODE_SEL {
SCL_MODE_RGB_BYPASS                      = 0x00000000,
SCL_MODE_RGB_SCALING                     = 0x00000001,
SCL_MODE_YCBCR_SCALING                   = 0x00000002,
SCL_MODE_YCBCR_BYPASS                    = 0x00000003,
} SCL_MODE_SEL;

/*
 * SCL_PSCL_EN enum
 */

typedef enum SCL_PSCL_EN {
SCL_PSCL_DISABLE                         = 0x00000000,
SCL_PSCL_ENANBLE                         = 0x00000001,
} SCL_PSCL_EN;

/*
 * SCL_V_NUM_OF_TAPS enum
 */

typedef enum SCL_V_NUM_OF_TAPS {
SCL_V_NUM_OF_TAPS_1                      = 0x00000000,
SCL_V_NUM_OF_TAPS_2                      = 0x00000001,
SCL_V_NUM_OF_TAPS_3                      = 0x00000002,
SCL_V_NUM_OF_TAPS_4                      = 0x00000003,
SCL_V_NUM_OF_TAPS_5                      = 0x00000004,
SCL_V_NUM_OF_TAPS_6                      = 0x00000005,
} SCL_V_NUM_OF_TAPS;

/*
 * SCL_H_NUM_OF_TAPS enum
 */

typedef enum SCL_H_NUM_OF_TAPS {
SCL_H_NUM_OF_TAPS_1                      = 0x00000000,
SCL_H_NUM_OF_TAPS_2                      = 0x00000001,
SCL_H_NUM_OF_TAPS_4                      = 0x00000003,
SCL_H_NUM_OF_TAPS_6                      = 0x00000005,
SCL_H_NUM_OF_TAPS_8                      = 0x00000007,
SCL_H_NUM_OF_TAPS_10                     = 0x00000009,
} SCL_H_NUM_OF_TAPS;

/*
 * SCL_BOUNDARY_MODE enum
 */

typedef enum SCL_BOUNDARY_MODE {
SCL_BOUNDARY_MODE_BLACK                  = 0x00000000,
SCL_BOUNDARY_MODE_EDGE                   = 0x00000001,
} SCL_BOUNDARY_MODE;

/*
 * SCL_EARLY_EOL_MOD enum
 */

typedef enum SCL_EARLY_EOL_MOD {
SCL_EARLY_EOL_MODE_CRTC                  = 0x00000000,
SCL_EARLY_EOL_MODE_INTERNAL              = 0x00000001,
} SCL_EARLY_EOL_MOD;

/*
 * SCL_BYPASS_MODE enum
 */

typedef enum SCL_BYPASS_MODE {
SCL_BYPASS_MODE_MC_MR                    = 0x00000000,
SCL_BYPASS_MODE_AC_NR                    = 0x00000001,
SCL_BYPASS_MODE_AC_AR                    = 0x00000002,
SCL_BYPASS_MODE_RESERVED                 = 0x00000003,
} SCL_BYPASS_MODE;

/*
 * SCL_V_MANUAL_REPLICATE_FACTOR enum
 */

typedef enum SCL_V_MANUAL_REPLICATE_FACTOR {
SCL_V_MANUAL_REPLICATE_FACTOR_1          = 0x00000000,
SCL_V_MANUAL_REPLICATE_FACTOR_2          = 0x00000001,
SCL_V_MANUAL_REPLICATE_FACTOR_3          = 0x00000002,
SCL_V_MANUAL_REPLICATE_FACTOR_4          = 0x00000003,
SCL_V_MANUAL_REPLICATE_FACTOR_5          = 0x00000004,
SCL_V_MANUAL_REPLICATE_FACTOR_6          = 0x00000005,
SCL_V_MANUAL_REPLICATE_FACTOR_7          = 0x00000006,
SCL_V_MANUAL_REPLICATE_FACTOR_8          = 0x00000007,
SCL_V_MANUAL_REPLICATE_FACTOR_9          = 0x00000008,
SCL_V_MANUAL_REPLICATE_FACTOR_10         = 0x00000009,
SCL_V_MANUAL_REPLICATE_FACTOR_11         = 0x0000000a,
SCL_V_MANUAL_REPLICATE_FACTOR_12         = 0x0000000b,
SCL_V_MANUAL_REPLICATE_FACTOR_13         = 0x0000000c,
SCL_V_MANUAL_REPLICATE_FACTOR_14         = 0x0000000d,
SCL_V_MANUAL_REPLICATE_FACTOR_15         = 0x0000000e,
SCL_V_MANUAL_REPLICATE_FACTOR_16         = 0x0000000f,
} SCL_V_MANUAL_REPLICATE_FACTOR;

/*
 * SCL_H_MANUAL_REPLICATE_FACTOR enum
 */

typedef enum SCL_H_MANUAL_REPLICATE_FACTOR {
SCL_H_MANUAL_REPLICATE_FACTOR_1          = 0x00000000,
SCL_H_MANUAL_REPLICATE_FACTOR_2          = 0x00000001,
SCL_H_MANUAL_REPLICATE_FACTOR_3          = 0x00000002,
SCL_H_MANUAL_REPLICATE_FACTOR_4          = 0x00000003,
SCL_H_MANUAL_REPLICATE_FACTOR_5          = 0x00000004,
SCL_H_MANUAL_REPLICATE_FACTOR_6          = 0x00000005,
SCL_H_MANUAL_REPLICATE_FACTOR_7          = 0x00000006,
SCL_H_MANUAL_REPLICATE_FACTOR_8          = 0x00000007,
SCL_H_MANUAL_REPLICATE_FACTOR_9          = 0x00000008,
SCL_H_MANUAL_REPLICATE_FACTOR_10         = 0x00000009,
SCL_H_MANUAL_REPLICATE_FACTOR_11         = 0x0000000a,
SCL_H_MANUAL_REPLICATE_FACTOR_12         = 0x0000000b,
SCL_H_MANUAL_REPLICATE_FACTOR_13         = 0x0000000c,
SCL_H_MANUAL_REPLICATE_FACTOR_14         = 0x0000000d,
SCL_H_MANUAL_REPLICATE_FACTOR_15         = 0x0000000e,
SCL_H_MANUAL_REPLICATE_FACTOR_16         = 0x0000000f,
} SCL_H_MANUAL_REPLICATE_FACTOR;

/*
 * SCL_V_CALC_AUTO_RATIO_EN enum
 */

typedef enum SCL_V_CALC_AUTO_RATIO_EN {
SCL_V_CALC_AUTO_RATIO_DISABLE            = 0x00000000,
SCL_V_CALC_AUTO_RATIO_ENABLE             = 0x00000001,
} SCL_V_CALC_AUTO_RATIO_EN;

/*
 * SCL_H_CALC_AUTO_RATIO_EN enum
 */

typedef enum SCL_H_CALC_AUTO_RATIO_EN {
SCL_H_CALC_AUTO_RATIO_DISABLE            = 0x00000000,
SCL_H_CALC_AUTO_RATIO_ENABLE             = 0x00000001,
} SCL_H_CALC_AUTO_RATIO_EN;

/*
 * SCL_H_FILTER_PICK_NEAREST enum
 */

typedef enum SCL_H_FILTER_PICK_NEAREST {
SCL_H_FILTER_PICK_NEAREST_DISABLE        = 0x00000000,
SCL_H_FILTER_PICK_NEAREST_ENABLE         = 0x00000001,
} SCL_H_FILTER_PICK_NEAREST;

/*
 * SCL_H_2TAP_HARDCODE_COEF_EN enum
 */

typedef enum SCL_H_2TAP_HARDCODE_COEF_EN {
SCL_H_2TAP_HARDCODE_COEF_DISABLE         = 0x00000000,
SCL_H_2TAP_HARDCODE_COEF_ENABLE          = 0x00000001,
} SCL_H_2TAP_HARDCODE_COEF_EN;

/*
 * SCL_V_FILTER_PICK_NEAREST enum
 */

typedef enum SCL_V_FILTER_PICK_NEAREST {
SCL_V_FILTER_PICK_NEAREST_DISABLE        = 0x00000000,
SCL_V_FILTER_PICK_NEAREST_ENABLE         = 0x00000001,
} SCL_V_FILTER_PICK_NEAREST;

/*
 * SCL_V_2TAP_HARDCODE_COEF_EN enum
 */

typedef enum SCL_V_2TAP_HARDCODE_COEF_EN {
SCL_V_2TAP_HARDCODE_COEF_DISABLE         = 0x00000000,
SCL_V_2TAP_HARDCODE_COEF_ENABLE          = 0x00000001,
} SCL_V_2TAP_HARDCODE_COEF_EN;

/*
 * SCL_UPDATE_TAKEN enum
 */

typedef enum SCL_UPDATE_TAKEN {
SCL_UPDATE_TAKEN_NO                      = 0x00000000,
SCL_UPDATE_TAKEN_YES                     = 0x00000001,
} SCL_UPDATE_TAKEN;

/*
 * SCL_UPDATE_LOCK enum
 */

typedef enum SCL_UPDATE_LOCK {
SCL_UPDATE_UNLOCKED                      = 0x00000000,
SCL_UPDATE_LOCKED                        = 0x00000001,
} SCL_UPDATE_LOCK;

/*
 * SCL_COEF_UPDATE_COMPLETE enum
 */

typedef enum SCL_COEF_UPDATE_COMPLETE {
SCL_COEF_UPDATE_NOT_COMPLETED            = 0x00000000,
SCL_COEF_UPDATE_COMPLETED                = 0x00000001,
} SCL_COEF_UPDATE_COMPLETE;

/*
 * SCL_HF_SHARP_SCALE_FACTOR enum
 */

typedef enum SCL_HF_SHARP_SCALE_FACTOR {
SCL_HF_SHARP_SCALE_FACTOR_0              = 0x00000000,
SCL_HF_SHARP_SCALE_FACTOR_1              = 0x00000001,
SCL_HF_SHARP_SCALE_FACTOR_2              = 0x00000002,
SCL_HF_SHARP_SCALE_FACTOR_3              = 0x00000003,
SCL_HF_SHARP_SCALE_FACTOR_4              = 0x00000004,
SCL_HF_SHARP_SCALE_FACTOR_5              = 0x00000005,
SCL_HF_SHARP_SCALE_FACTOR_6              = 0x00000006,
SCL_HF_SHARP_SCALE_FACTOR_7              = 0x00000007,
} SCL_HF_SHARP_SCALE_FACTOR;

/*
 * SCL_HF_SHARP_EN enum
 */

typedef enum SCL_HF_SHARP_EN {
SCL_HF_SHARP_DISABLE                     = 0x00000000,
SCL_HF_SHARP_ENABLE                      = 0x00000001,
} SCL_HF_SHARP_EN;

/*
 * SCL_VF_SHARP_SCALE_FACTOR enum
 */

typedef enum SCL_VF_SHARP_SCALE_FACTOR {
SCL_VF_SHARP_SCALE_FACTOR_0              = 0x00000000,
SCL_VF_SHARP_SCALE_FACTOR_1              = 0x00000001,
SCL_VF_SHARP_SCALE_FACTOR_2              = 0x00000002,
SCL_VF_SHARP_SCALE_FACTOR_3              = 0x00000003,
SCL_VF_SHARP_SCALE_FACTOR_4              = 0x00000004,
SCL_VF_SHARP_SCALE_FACTOR_5              = 0x00000005,
SCL_VF_SHARP_SCALE_FACTOR_6              = 0x00000006,
SCL_VF_SHARP_SCALE_FACTOR_7              = 0x00000007,
} SCL_VF_SHARP_SCALE_FACTOR;

/*
 * SCL_VF_SHARP_EN enum
 */

typedef enum SCL_VF_SHARP_EN {
SCL_VF_SHARP_DISABLE                     = 0x00000000,
SCL_VF_SHARP_ENABLE                      = 0x00000001,
} SCL_VF_SHARP_EN;

/*
 * SCL_ALU_DISABLE enum
 */

typedef enum SCL_ALU_DISABLE {
SCL_ALU_ENABLED                          = 0x00000000,
SCL_ALU_DISABLED                         = 0x00000001,
} SCL_ALU_DISABLE;

/*
 * SCL_HOST_CONFLICT_MASK enum
 */

typedef enum SCL_HOST_CONFLICT_MASK {
SCL_HOST_CONFLICT_DISABLE_INTERRUPT      = 0x00000000,
SCL_HOST_CONFLICT_ENABLE_INTERRUPT       = 0x00000001,
} SCL_HOST_CONFLICT_MASK;

/*
 * SCL_SCL_MODE_CHANGE_MASK enum
 */

typedef enum SCL_SCL_MODE_CHANGE_MASK {
SCL_MODE_CHANGE_DISABLE_INTERRUPT        = 0x00000000,
SCL_MODE_CHANGE_ENABLE_INTERRUPT         = 0x00000001,
} SCL_SCL_MODE_CHANGE_MASK;

/*******************************************************
 * SCLV Enums
 *******************************************************/

/*
 * SCLV_MODE_SEL enum
 */

typedef enum SCLV_MODE_SEL {
SCLV_MODE_RGB_BYPASS                     = 0x00000000,
SCLV_MODE_RGB_SCALING                    = 0x00000001,
SCLV_MODE_YCBCR_SCALING                  = 0x00000002,
SCLV_MODE_YCBCR_BYPASS                   = 0x00000003,
} SCLV_MODE_SEL;

/*
 * SCLV_INTERLACE_SOURCE enum
 */

typedef enum SCLV_INTERLACE_SOURCE {
INTERLACE_SOURCE_PROGRESSIVE             = 0x00000000,
INTERLACE_SOURCE_INTERLEAVE              = 0x00000001,
INTERLACE_SOURCE_STACK                   = 0x00000002,
} SCLV_INTERLACE_SOURCE;

/*
 * SCLV_UPDATE_LOCK enum
 */

typedef enum SCLV_UPDATE_LOCK {
UPDATE_UNLOCKED                          = 0x00000000,
UPDATE_LOCKED                            = 0x00000001,
} SCLV_UPDATE_LOCK;

/*
 * SCLV_COEF_UPDATE_COMPLETE enum
 */

typedef enum SCLV_COEF_UPDATE_COMPLETE {
COEF_UPDATE_NOT_COMPLETE                 = 0x00000000,
COEF_UPDATE_COMPLETE                     = 0x00000001,
} SCLV_COEF_UPDATE_COMPLETE;

/*******************************************************
 * DPRX_SD Enums
 *******************************************************/

/*
 * DPRX_SD_PIXEL_ENCODING enum
 */

typedef enum DPRX_SD_PIXEL_ENCODING {
PIXEL_FORMAT_RGB_444                     = 0x00000000,
PIXEL_FORMAT_YCBCR_444                   = 0x00000001,
PIXEL_FORMAT_YCBCR_422                   = 0x00000002,
PIXEL_FORMAT_Y_ONLY                      = 0x00000003,
} DPRX_SD_PIXEL_ENCODING;

/*
 * DPRX_SD_COMPONENT_DEPTH enum
 */

typedef enum DPRX_SD_COMPONENT_DEPTH {
COMPONENT_DEPTH_6BPC                     = 0x00000000,
COMPONENT_DEPTH_8BPC                     = 0x00000001,
COMPONENT_DEPTH_10BPC                    = 0x00000002,
COMPONENT_DEPTH_12BPC                    = 0x00000003,
COMPONENT_DEPTH_16BPC                    = 0x00000004,
} DPRX_SD_COMPONENT_DEPTH;

/*******************************************************
 * AZF0STREAM Enums
 *******************************************************/

/*
 * AZ_LATENCY_COUNTER_CONTROL enum
 */

typedef enum AZ_LATENCY_COUNTER_CONTROL {
AZ_LATENCY_COUNTER_NO_RESET              = 0x00000000,
AZ_LATENCY_COUNTER_RESET_DONE            = 0x00000001,
} AZ_LATENCY_COUNTER_CONTROL;

/*******************************************************
 * BLND Enums
 *******************************************************/

/*
 * BLND_CONTROL_BLND_MODE enum
 */

typedef enum BLND_CONTROL_BLND_MODE {
BLND_CONTROL_BLND_MODE_CURRENT_PIPE_ONLY = 0x00000000,
BLND_CONTROL_BLND_MODE_OTHER_PIPE_ONLY   = 0x00000001,
BLND_CONTROL_BLND_MODE_ALPHA_BLENDING_MODE = 0x00000002,
BLND_CONTROL_BLND_MODE_OTHER_STEREO_TYPE = 0x00000003,
} BLND_CONTROL_BLND_MODE;

/*
 * BLND_CONTROL_BLND_STEREO_TYPE enum
 */

typedef enum BLND_CONTROL_BLND_STEREO_TYPE {
BLND_CONTROL_BLND_STEREO_TYPE_NON_SINGLE_PIPE_STEREO = 0x00000000,
BLND_CONTROL_BLND_STEREO_TYPE_SIDE_BY_SIDE_SINGLE_PIPE_STEREO = 0x00000001,
BLND_CONTROL_BLND_STEREO_TYPE_TOP_BOTTOM_SINGLE_PIPE_STEREO = 0x00000002,
BLND_CONTROL_BLND_STEREO_TYPE_UNUSED     = 0x00000003,
} BLND_CONTROL_BLND_STEREO_TYPE;

/*
 * BLND_CONTROL_BLND_STEREO_POLARITY enum
 */

typedef enum BLND_CONTROL_BLND_STEREO_POLARITY {
BLND_CONTROL_BLND_STEREO_POLARITY_LOW    = 0x00000000,
BLND_CONTROL_BLND_STEREO_POLARITY_HIGH   = 0x00000001,
} BLND_CONTROL_BLND_STEREO_POLARITY;

/*
 * BLND_CONTROL_BLND_FEEDTHROUGH_EN enum
 */

typedef enum BLND_CONTROL_BLND_FEEDTHROUGH_EN {
BLND_CONTROL_BLND_FEEDTHROUGH_EN_FALSE   = 0x00000000,
BLND_CONTROL_BLND_FEEDTHROUGH_EN_TRUE    = 0x00000001,
} BLND_CONTROL_BLND_FEEDTHROUGH_EN;

/*
 * BLND_CONTROL_BLND_ALPHA_MODE enum
 */

typedef enum BLND_CONTROL_BLND_ALPHA_MODE {
BLND_CONTROL_BLND_ALPHA_MODE_CURRENT_PIXEL_ALPHA = 0x00000000,
BLND_CONTROL_BLND_ALPHA_MODE_PIXEL_ALPHA_COMBINED_GLOBAL_GAIN = 0x00000001,
BLND_CONTROL_BLND_ALPHA_MODE_GLOBAL_ALPHA_ONLY = 0x00000002,
BLND_CONTROL_BLND_ALPHA_MODE_UNUSED      = 0x00000003,
} BLND_CONTROL_BLND_ALPHA_MODE;

/*
 * BLND_CONTROL_BLND_ACTIVE_OVERLAP_ONLY enum
 */

typedef enum BLND_CONTROL_BLND_ACTIVE_OVERLAP_ONLY {
BLND_CONTROL_BLND_ACTIVE_OVERLAY_ONLY_FALSE  = 0x00000000,
BLND_CONTROL_BLND_ACTIVE_OVERLAY_ONLY_TRUE  = 0x00000001,
} BLND_CONTROL_BLND_ACTIVE_OVERLAP_ONLY;

/*
 * BLND_CONTROL_BLND_MULTIPLIED_MODE enum
 */

typedef enum BLND_CONTROL_BLND_MULTIPLIED_MODE {
BLND_CONTROL_BLND_MULTIPLIED_MODE_FALSE  = 0x00000000,
BLND_CONTROL_BLND_MULTIPLIED_MODE_TRUE   = 0x00000001,
} BLND_CONTROL_BLND_MULTIPLIED_MODE;

/*
 * BLND_SM_CONTROL2_SM_MODE enum
 */

typedef enum BLND_SM_CONTROL2_SM_MODE {
BLND_SM_CONTROL2_SM_MODE_SINGLE_PLANE    = 0x00000000,
BLND_SM_CONTROL2_SM_MODE_ROW_SUBSAMPLING = 0x00000002,
BLND_SM_CONTROL2_SM_MODE_COLUMN_SUBSAMPLING = 0x00000004,
BLND_SM_CONTROL2_SM_MODE_CHECKERBOARD_SUBSAMPLING = 0x00000006,
} BLND_SM_CONTROL2_SM_MODE;

/*
 * BLND_SM_CONTROL2_SM_FRAME_ALTERNATE enum
 */

typedef enum BLND_SM_CONTROL2_SM_FRAME_ALTERNATE {
BLND_SM_CONTROL2_SM_FRAME_ALTERNATE_FALSE = 0x00000000,
BLND_SM_CONTROL2_SM_FRAME_ALTERNATE_TRUE = 0x00000001,
} BLND_SM_CONTROL2_SM_FRAME_ALTERNATE;

/*
 * BLND_SM_CONTROL2_SM_FIELD_ALTERNATE enum
 */

typedef enum BLND_SM_CONTROL2_SM_FIELD_ALTERNATE {
BLND_SM_CONTROL2_SM_FIELD_ALTERNATE_FALSE = 0x00000000,
BLND_SM_CONTROL2_SM_FIELD_ALTERNATE_TRUE = 0x00000001,
} BLND_SM_CONTROL2_SM_FIELD_ALTERNATE;

/*
 * BLND_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL enum
 */

typedef enum BLND_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL {
BLND_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL_NO_FORCE = 0x00000000,
BLND_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL_RESERVED = 0x00000001,
BLND_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL_FORCE_LOW = 0x00000002,
BLND_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL_FORCE_HIGH = 0x00000003,
} BLND_SM_CONTROL2_SM_FORCE_NEXT_FRAME_POL;

/*
 * BLND_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL enum
 */

typedef enum BLND_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL {
BLND_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL_NO_FORCE = 0x00000000,
BLND_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL_RESERVED = 0x00000001,
BLND_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL_FORCE_LOW = 0x00000002,
BLND_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL_FORCE_HIGH = 0x00000003,
} BLND_SM_CONTROL2_SM_FORCE_NEXT_TOP_POL;

/*
 * BLND_CONTROL2_PTI_ENABLE enum
 */

typedef enum BLND_CONTROL2_PTI_ENABLE {
BLND_CONTROL2_PTI_ENABLE_FALSE           = 0x00000000,
BLND_CONTROL2_PTI_ENABLE_TRUE            = 0x00000001,
} BLND_CONTROL2_PTI_ENABLE;

/*
 * BLND_CONTROL2_BLND_SUPERAA_DEGAMMA_EN enum
 */

typedef enum BLND_CONTROL2_BLND_SUPERAA_DEGAMMA_EN {
BLND_CONTROL2_BLND_SUPERAA_DEGAMMA_EN_FALSE = 0x00000000,
BLND_CONTROL2_BLND_SUPERAA_DEGAMMA_EN_TRUE = 0x00000001,
} BLND_CONTROL2_BLND_SUPERAA_DEGAMMA_EN;

/*
 * BLND_CONTROL2_BLND_SUPERAA_REGAMMA_EN enum
 */

typedef enum BLND_CONTROL2_BLND_SUPERAA_REGAMMA_EN {
BLND_CONTROL2_BLND_SUPERAA_REGAMMA_EN_FALSE = 0x00000000,
BLND_CONTROL2_BLND_SUPERAA_REGAMMA_EN_TRUE = 0x00000001,
} BLND_CONTROL2_BLND_SUPERAA_REGAMMA_EN;

/*
 * BLND_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_ACK enum
 */

typedef enum BLND_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_ACK {
BLND_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_ACK_FALSE = 0x00000000,
BLND_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_ACK_TRUE = 0x00000001,
} BLND_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_ACK;

/*
 * BLND_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_MASK enum
 */

typedef enum BLND_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_MASK {
BLND_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_MASK_FALSE = 0x00000000,
BLND_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_MASK_TRUE = 0x00000001,
} BLND_UNDERFLOW_INTERRUPT_BLND_UNDERFLOW_INT_MASK;

/*
 * BLND_V_UPDATE_LOCK_BLND_DCP_GRPH_V_UPDATE_LOCK enum
 */

typedef enum BLND_V_UPDATE_LOCK_BLND_DCP_GRPH_V_UPDATE_LOCK {
BLND_V_UPDATE_LOCK_BLND_DCP_GRPH_V_UPDATE_LOCK_FALSE = 0x00000000,
BLND_V_UPDATE_LOCK_BLND_DCP_GRPH_V_UPDATE_LOCK_TRUE = 0x00000001,
} BLND_V_UPDATE_LOCK_BLND_DCP_GRPH_V_UPDATE_LOCK;

/*
 * BLND_V_UPDATE_LOCK_BLND_DCP_GRPH_SURF_V_UPDATE_LOCK enum
 */

typedef enum BLND_V_UPDATE_LOCK_BLND_DCP_GRPH_SURF_V_UPDATE_LOCK {
BLND_V_UPDATE_LOCK_BLND_DCP_GRPH_SURF_V_UPDATE_LOCK_FALSE = 0x00000000,
BLND_V_UPDATE_LOCK_BLND_DCP_GRPH_SURF_V_UPDATE_LOCK_TRUE = 0x00000001,
} BLND_V_UPDATE_LOCK_BLND_DCP_GRPH_SURF_V_UPDATE_LOCK;

/*
 * BLND_V_UPDATE_LOCK_BLND_DCP_CUR_V_UPDATE_LOCK enum
 */

typedef enum BLND_V_UPDATE_LOCK_BLND_DCP_CUR_V_UPDATE_LOCK {
BLND_V_UPDATE_LOCK_BLND_DCP_CUR_V_UPDATE_LOCK_FALSE = 0x00000000,
BLND_V_UPDATE_LOCK_BLND_DCP_CUR_V_UPDATE_LOCK_TRUE = 0x00000001,
} BLND_V_UPDATE_LOCK_BLND_DCP_CUR_V_UPDATE_LOCK;

/*
 * BLND_V_UPDATE_LOCK_BLND_DCP_CUR2_V_UPDATE_LOCK enum
 */

typedef enum BLND_V_UPDATE_LOCK_BLND_DCP_CUR2_V_UPDATE_LOCK {
BLND_V_UPDATE_LOCK_BLND_DCP_CUR2_V_UPDATE_LOCK_FALSE = 0x00000000,
BLND_V_UPDATE_LOCK_BLND_DCP_CUR2_V_UPDATE_LOCK_TRUE = 0x00000001,
} BLND_V_UPDATE_LOCK_BLND_DCP_CUR2_V_UPDATE_LOCK;

/*
 * BLND_V_UPDATE_LOCK_BLND_SCL_V_UPDATE_LOCK enum
 */

typedef enum BLND_V_UPDATE_LOCK_BLND_SCL_V_UPDATE_LOCK {
BLND_V_UPDATE_LOCK_BLND_SCL_V_UPDATE_LOCK_FALSE = 0x00000000,
BLND_V_UPDATE_LOCK_BLND_SCL_V_UPDATE_LOCK_TRUE = 0x00000001,
} BLND_V_UPDATE_LOCK_BLND_SCL_V_UPDATE_LOCK;

/*
 * BLND_V_UPDATE_LOCK_BLND_BLND_V_UPDATE_LOCK enum
 */

typedef enum BLND_V_UPDATE_LOCK_BLND_BLND_V_UPDATE_LOCK {
BLND_V_UPDATE_LOCK_BLND_BLND_V_UPDATE_LOCK_FALSE = 0x00000000,
BLND_V_UPDATE_LOCK_BLND_BLND_V_UPDATE_LOCK_TRUE = 0x00000001,
} BLND_V_UPDATE_LOCK_BLND_BLND_V_UPDATE_LOCK;

/*
 * BLND_V_UPDATE_LOCK_BLND_V_UPDATE_LOCK_MODE enum
 */

typedef enum BLND_V_UPDATE_LOCK_BLND_V_UPDATE_LOCK_MODE {
BLND_V_UPDATE_LOCK_BLND_V_UPDATE_LOCK_MODE_FALSE = 0x00000000,
BLND_V_UPDATE_LOCK_BLND_V_UPDATE_LOCK_MODE_TRUE = 0x00000001,
} BLND_V_UPDATE_LOCK_BLND_V_UPDATE_LOCK_MODE;

/*
 * BLND_DEBUG_BLND_CNV_MUX_SELECT enum
 */

typedef enum BLND_DEBUG_BLND_CNV_MUX_SELECT {
BLND_DEBUG_BLND_CNV_MUX_SELECT_LOW       = 0x00000000,
BLND_DEBUG_BLND_CNV_MUX_SELECT_HIGH      = 0x00000001,
} BLND_DEBUG_BLND_CNV_MUX_SELECT;

/*
 * BLND_TEST_DEBUG_INDEX_BLND_TEST_DEBUG_WRITE_EN enum
 */

typedef enum BLND_TEST_DEBUG_INDEX_BLND_TEST_DEBUG_WRITE_EN {
BLND_TEST_DEBUG_INDEX_BLND_TEST_DEBUG_WRITE_EN_FALSE = 0x00000000,
BLND_TEST_DEBUG_INDEX_BLND_TEST_DEBUG_WRITE_EN_TRUE = 0x00000001,
} BLND_TEST_DEBUG_INDEX_BLND_TEST_DEBUG_WRITE_EN;

/*******************************************************
 * AZF0ENDPOINT Enums
 *******************************************************/

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_OUTPUT_CONVERTER_RESERVED  = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_INPUT_CONVERTER_RESERVED  = 0x00000001,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_MIXER_RESERVED  = 0x00000002,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_SELECTOR_RESERVED  = 0x00000003,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_PIN_RESERVED  = 0x00000004,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_POWER_WIDGET_RESERVED  = 0x00000005,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VOLUME_KNOB_RESERVED  = 0x00000006,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_BEEP_GENERATOR_RESERVED  = 0x00000007,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_RESERVED_RESERVED  = 0x00000008,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VENDOR_DEFINED_RESERVED  = 0x00000009,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_LR_SWAP_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_LR_SWAP_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_POWER_CONTROL_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_POWER_CONTROL_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_IS_ANALOG  = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_IS_DIGITAL  = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_CONNECTION_LIST  = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_CONNECTION_LIST  = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_UNSOLICITED_RESPONSE_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_UNSOLICITED_RESPONSE_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_NO_PROCESSING_CAPABILITIES  = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_HAVE_PROCESSING_CAPABILITIES  = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_SUPPORT_STRIPING  = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_SUPPORT_STRIPING  = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_FORMAT_OVERRIDE enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_FORMAT_OVERRIDE {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_FORMAT_OVERRIDE  = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_SUPPORT_FORMAT_OVERRIDE  = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_FORMAT_OVERRIDE;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_AMPLIFIER_PARAMETER  = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_AMPLIFIER_PARAMETER_OVERRIDE  = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_OUTPUT_AMPLIFIER  = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_OUTPUT_AMPLIFIER  = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_INPUT_AMPLIFIER  = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_INPUT_AMPLIFIER  = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES_MONOPHONIC  = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES_STEREO  = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_OUTPUT_CONVERTER_RESERVED  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_INPUT_CONVERTER_RESERVED  = 0x00000001,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_MIXER_RESERVED  = 0x00000002,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_SELECTOR_RESERVED  = 0x00000003,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_PIN_RESERVED  = 0x00000004,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_POWER_WIDGET_RESERVED  = 0x00000005,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VOLUME_KNOB_RESERVED  = 0x00000006,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_BEEP_GENERATOR_RESERVED  = 0x00000007,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_RESERVED_RESERVED  = 0x00000008,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VENDOR_DEFINED_RESERVED  = 0x00000009,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_LR_SWAP_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_LR_SWAP_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_POWER_CONTROL_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_POWER_CONTROL_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_IS_ANALOG  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_IS_DIGITAL  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_CONNECTION_LIST  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_CONNECTION_LIST  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_UNSOLICITED_RESPONSE_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_UNSOLICITED_RESPONSE_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_NO_PROCESSING_CAPABILITIES  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_HAVE_PROCESSING_CAPABILITIES  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_SUPPORT_STRIPING  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_SUPPORT_STRIPING  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_AMPLIFIER_PARAMETER  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_AMPLIFIER_PARAMETER_OVERRIDE  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_OUTPUT_AMPLIFIER  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_OUTPUT_AMPLIFIER  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_INPUT_AMPLIFIER_PRESENT  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_INPUT_AMPLIFIER  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_EAPD_PIN  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HAVE_EAPD_PIN  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_BALANCED_I_O_PINS enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_BALANCED_I_O_PINS {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_I_O_PINS_ARE_NOT_BALANCED  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_I_O_PINS_ARE_BALANCED  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_BALANCED_I_O_PINS;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_INPUT_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_INPUT_CAPABLE {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_INPUT_PIN  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HAVE_INPUT_PIN  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_INPUT_CAPABLE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_OUTPUT_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_OUTPUT_CAPABLE {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_OUTPUT_PIN  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HAVE_OUTPUT_PIN  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_OUTPUT_CAPABLE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HEADPHONE_DRIVE_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HEADPHONE_DRIVE_CAPABLE {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_HEADPHONE_DRIVE_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HAVE_HEADPHONE_DRIVE_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HEADPHONE_DRIVE_CAPABLE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_JACK_DETECTION_CAPABILITY enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_JACK_DETECTION_CAPABILITY {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_JACK_DETECTION_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HAVE_JACK_DETECTION_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_JACK_DETECTION_CAPABILITY;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_TRIGGER_REQUIRED_FOR_IMPEDANCE_MEASUREMENT  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED_FOR_IMPEDANCE_MEASUREMENT  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_IMPEDANCE_SENSE_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_IMPEDANCE_SENSE_CAPABLE {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_IMPEDANCE_SENSE_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HAVE_IMPEDANCE_SENSE_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_IMPEDANCE_SENSE_CAPABLE;

/*
 * AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_MODE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_MODE {
AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_PAIR_MODE  = 0x00000000,
AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_SINGLE_MODE  = 0x00000001,
} AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_MODE;

/*
 * AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR_HBR_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR_HBR_CAPABLE {
AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR_NO_HBR_CAPABLILITY  = 0x00000000,
AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR_HAVE_HBR_CAPABLILITY  = 0x00000001,
} AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR_HBR_CAPABLE;

/*******************************************************
 * AZF0INPUTENDPOINT Enums
 *******************************************************/

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_OUTPUT_CONVERTER_RESERVED  = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_INPUT_CONVERTER_RESERVED  = 0x00000001,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_MIXER_RESERVED  = 0x00000002,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_SELECTOR_RESERVED  = 0x00000003,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_PIN_RESERVED  = 0x00000004,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_POWER_WIDGET_RESERVED  = 0x00000005,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VOLUME_KNOB_RESERVED  = 0x00000006,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_BEEP_GENERATOR_RESERVED  = 0x00000007,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_RESERVED  = 0x00000008,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VENDOR_DEFINED_RESERVED  = 0x00000009,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_LR_SWAP_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_LR_SWAP_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_POWER_CONTROL_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_POWER_CONTROL_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CODEC_CONVERTER0_IS_ANALOG  = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CODEC_CONVERTER0_IS_DIGITAL  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_CONNECTION_LIST  = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_CONNECTION_LIST  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_UNSOLICITED_RESPONSE_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_UNSOLICITED_RESPONSE_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_CODEC_CONVERTER0_HAVE_NO_PROCESSING_CAPABILITIES  = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_CODEC_CONVERTER0_HAVE_PROCESSING_CAPABILITIES  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NOT_SUPPORT_STRIPING  = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_SUPPORT_STRIPING  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_FORMAT_OVERRIDE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_FORMAT_OVERRIDE {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_FORMAT_OVERRIDE  = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_FORMAT_OVERRIDE  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_FORMAT_OVERRIDE;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_AMPLIFIER_PARAMETER  = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_AMPLIFIER_PARAMETER  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_OUTPUT_AMPLIFIER  = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_OUTPUT_AMPLIFIER  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_INPUT_AMPLIFIER  = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_INPUT_AMPLIFIER  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES_MONOPHONIC  = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES_STEREO  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_OUTPUT_CONVERTER_RESERVED  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_INPUT_CONVERTER_RESERVED  = 0x00000001,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_MIXER_RESERVED  = 0x00000002,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_SELECTOR_RESERVED  = 0x00000003,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_PIN_RESERVED  = 0x00000004,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_POWER_WIDGET_RESERVED  = 0x00000005,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VOLUME_KNOB_RESERVED  = 0x00000006,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_BEEP_GENERATOR_RESERVED  = 0x00000007,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_RESERVED  = 0x00000008,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VENDOR_DEFINED_RESERVED  = 0x00000009,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_LR_SWAP  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_LR_SWAP  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_POWER_CONTROL_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_POWER_CONTROL_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_IS_ANALOG  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_IS_DIGITAL  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_CONNECTION_LIST  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_CONNECTION_LIST  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_UNSOLICITED_RESPONSE_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_UNSOLICITED_RESPONSE_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_NO_PROCESING_CAPABILITIES  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_HAVE_PROCESING_CAPABILITIES  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_SUPPORT_STRIPING  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_SUPPORT_STRIPING  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_AMPLIFIER_PARAMETER  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_AMPLIFIER_PARAMETER_OVERRIDE  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_OUTPUT_AMPLIFIER  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_OUTPUT_AMPLIFIER  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_INPUT_AMPLIFIER  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_INPUT_AMPLIFIER  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_DP enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_DP {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_DP_NOT_ENABLED  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_DP_ENABLED  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_DP;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE_NO_EAPD_PIN  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE_HAVE_EAPD_PIN  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HDMI enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HDMI {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HDMI_NOT_ENABLED  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HDMI_ENABLED  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HDMI;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_BALANCED_I_O_PINS enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_BALANCED_I_O_PINS {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_I_O_PINS_NOT_BALANCED  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_I_O_PINS_ARE_BALANCED  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_BALANCED_I_O_PINS;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_INPUT_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_INPUT_CAPABLE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_NO_INPUT_PIN  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HAVE_INPUT_PIN  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_INPUT_CAPABLE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_OUTPUT_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_OUTPUT_CAPABLE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_NO_OUTPUT_PIN  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HAVE_OUTPUT_PIN  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_OUTPUT_CAPABLE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HEADPHONE_DRIVE_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HEADPHONE_DRIVE_CAPABLE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_NO_HEADPHONE_DRIVE_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HAVE_HEADPHONE_DRIVE_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HEADPHONE_DRIVE_CAPABLE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_JACK_DETECTION_CAPABILITY enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_JACK_DETECTION_CAPABILITY {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_NO_JACK_PRESENCE_DETECTION_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HAVE_JACK_PRESENCE_DETECTION_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_JACK_DETECTION_CAPABILITY;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_NO_TRIGGER_REQUIRED_FOR_IMPEDANCE_MEASUREMENT  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED_FOR_IMPEDANCE_MEASUREMENT  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_IMPEDANCE_SENSE_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_IMPEDANCE_SENSE_CAPABLE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_NO_IMPEDANCE_SENSE_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HAVE_IMPEDANCE_SENSE_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_IMPEDANCE_SENSE_CAPABLE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_CONTROL_RESPONSE_HBR_HBR_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_CONTROL_RESPONSE_HBR_HBR_CAPABLE {
AZALIA_F0_CODEC_INPUT_PIN_CONTROL_RESPONSE_HBR_NO_HBR_CAPABILITY  = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_CONTROL_RESPONSE_HBR_HAVE_HBR_CAPABILITY  = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_CONTROL_RESPONSE_HBR_HBR_CAPABLE;

/*******************************************************
 * UNP Enums
 *******************************************************/

/*
 * UNP_GRPH_EN enum
 */

typedef enum UNP_GRPH_EN {
UNP_GRPH_DISABLED                        = 0x00000000,
UNP_GRPH_ENABLED                         = 0x00000001,
} UNP_GRPH_EN;

/*
 * UNP_GRPH_DEPTH enum
 */

typedef enum UNP_GRPH_DEPTH {
UNP_GRPH_8BPP                            = 0x00000000,
UNP_GRPH_16BPP                           = 0x00000001,
UNP_GRPH_32BPP                           = 0x00000002,
} UNP_GRPH_DEPTH;

/*
 * UNP_GRPH_NUM_BANKS enum
 */

typedef enum UNP_GRPH_NUM_BANKS {
UNP_GRPH_ADDR_SURF_2_BANK                = 0x00000000,
UNP_GRPH_ADDR_SURF_4_BANK                = 0x00000001,
UNP_GRPH_ADDR_SURF_8_BANK                = 0x00000002,
UNP_GRPH_ADDR_SURF_16_BANK               = 0x00000003,
} UNP_GRPH_NUM_BANKS;

/*
 * UNP_GRPH_BANK_WIDTH enum
 */

typedef enum UNP_GRPH_BANK_WIDTH {
UNP_GRPH_ADDR_SURF_BANK_WIDTH_1          = 0x00000000,
UNP_GRPH_ADDR_SURF_BANK_WIDTH_2          = 0x00000001,
UNP_GRPH_ADDR_SURF_BANK_WIDTH_4          = 0x00000002,
UNP_GRPH_ADDR_SURF_BANK_WIDTH_8          = 0x00000003,
} UNP_GRPH_BANK_WIDTH;

/*
 * UNP_GRPH_BANK_HEIGHT enum
 */

typedef enum UNP_GRPH_BANK_HEIGHT {
UNP_GRPH_ADDR_SURF_BANK_HEIGHT_1         = 0x00000000,
UNP_GRPH_ADDR_SURF_BANK_HEIGHT_2         = 0x00000001,
UNP_GRPH_ADDR_SURF_BANK_HEIGHT_4         = 0x00000002,
UNP_GRPH_ADDR_SURF_BANK_HEIGHT_8         = 0x00000003,
} UNP_GRPH_BANK_HEIGHT;

/*
 * UNP_GRPH_TILE_SPLIT enum
 */

typedef enum UNP_GRPH_TILE_SPLIT {
UNP_ADDR_SURF_TILE_SPLIT_64B             = 0x00000000,
UNP_ADDR_SURF_TILE_SPLIT_128B            = 0x00000001,
UNP_ADDR_SURF_TILE_SPLIT_256B            = 0x00000002,
UNP_ADDR_SURF_TILE_SPLIT_512B            = 0x00000003,
UNP_ADDR_SURF_TILE_SPLIT_1KB             = 0x00000004,
UNP_ADDR_SURF_TILE_SPLIT_2KB             = 0x00000005,
UNP_ADDR_SURF_TILE_SPLIT_4KB             = 0x00000006,
} UNP_GRPH_TILE_SPLIT;

/*
 * UNP_GRPH_ADDRESS_TRANSLATION_ENABLE enum
 */

typedef enum UNP_GRPH_ADDRESS_TRANSLATION_ENABLE {
UNP_GRPH_ADDRESS_TRANSLATION_ENABLE0     = 0x00000000,
UNP_GRPH_ADDRESS_TRANSLATION_ENABLE1     = 0x00000001,
} UNP_GRPH_ADDRESS_TRANSLATION_ENABLE;

/*
 * UNP_GRPH_MACRO_TILE_ASPECT enum
 */

typedef enum UNP_GRPH_MACRO_TILE_ASPECT {
UNP_ADDR_SURF_MACRO_ASPECT_1             = 0x00000000,
UNP_ADDR_SURF_MACRO_ASPECT_2             = 0x00000001,
UNP_ADDR_SURF_MACRO_ASPECT_4             = 0x00000002,
UNP_ADDR_SURF_MACRO_ASPECT_8             = 0x00000003,
} UNP_GRPH_MACRO_TILE_ASPECT;

/*
 * UNP_GRPH_COLOR_EXPANSION_MODE enum
 */

typedef enum UNP_GRPH_COLOR_EXPANSION_MODE {
UNP_GRPH_DYNAMIC_EXPANSION               = 0x00000000,
UNP_GRPH_ZERO_EXPANSION                  = 0x00000001,
} UNP_GRPH_COLOR_EXPANSION_MODE;

/*
 * UNP_VIDEO_FORMAT enum
 */

typedef enum UNP_VIDEO_FORMAT {
UNP_VIDEO_FORMAT0                        = 0x00000000,
UNP_VIDEO_FORMAT1                        = 0x00000001,
UNP_VIDEO_FORMAT_YUV420_YCbCr            = 0x00000002,
UNP_VIDEO_FORMAT_YUV420_YCrCb            = 0x00000003,
UNP_VIDEO_FORMAT_YUV422_YCb              = 0x00000004,
UNP_VIDEO_FORMAT_YUV422_YCr              = 0x00000005,
UNP_VIDEO_FORMAT_YUV422_CbY              = 0x00000006,
UNP_VIDEO_FORMAT_YUV422_CrY              = 0x00000007,
} UNP_VIDEO_FORMAT;

/*
 * UNP_GRPH_ENDIAN_SWAP enum
 */

typedef enum UNP_GRPH_ENDIAN_SWAP {
UNP_GRPH_ENDIAN_SWAP_NONE                = 0x00000000,
UNP_GRPH_ENDIAN_SWAP_8IN16               = 0x00000001,
UNP_GRPH_ENDIAN_SWAP_8IN32               = 0x00000002,
UNP_GRPH_ENDIAN_SWAP_8IN43               = 0x00000003,
} UNP_GRPH_ENDIAN_SWAP;

/*
 * UNP_GRPH_RED_CROSSBAR enum
 */

typedef enum UNP_GRPH_RED_CROSSBAR {
UNP_GRPH_RED_CROSSBAR_R_Cr               = 0x00000000,
UNP_GRPH_RED_CROSSBAR_G_Y                = 0x00000001,
UNP_GRPH_RED_CROSSBAR_B_Cb               = 0x00000002,
UNP_GRPH_RED_CROSSBAR_A                  = 0x00000003,
} UNP_GRPH_RED_CROSSBAR;

/*
 * UNP_GRPH_GREEN_CROSSBAR enum
 */

typedef enum UNP_GRPH_GREEN_CROSSBAR {
UNP_UNP_GRPH_GREEN_CROSSBAR_GY_AND_Y     = 0x00000000,
UNP_UNP_GRPH_GREEN_CROSSBAR_B_Cb_AND_C   = 0x00000001,
UNP_UNP_GRPH_GREEN_CROSSBAR_A            = 0x00000002,
UNP_UNP_GRPH_GREEN_CROSSBAR_R_Cr         = 0x00000003,
} UNP_GRPH_GREEN_CROSSBAR;

/*
 * UNP_GRPH_BLUE_CROSSBAR enum
 */

typedef enum UNP_GRPH_BLUE_CROSSBAR {
UNP_GRPH_BLUE_CROSSBAR_B_Cb_AND_C        = 0x00000000,
UNP_GRPH_BLUE_CROSSBAR_A                 = 0x00000001,
UNP_GRPH_BLUE_CROSSBAR_R_Cr              = 0x00000002,
UNP_GRPH_BLUE_CROSSBAR_GY_AND_Y          = 0x00000003,
} UNP_GRPH_BLUE_CROSSBAR;

/*
 * UNP_GRPH_MODE_UPDATE_LOCKG enum
 */

typedef enum UNP_GRPH_MODE_UPDATE_LOCKG {
UNP_GRPH_UPDATE_LOCK_0                   = 0x00000000,
UNP_GRPH_UPDATE_LOCK_1                   = 0x00000001,
} UNP_GRPH_MODE_UPDATE_LOCKG;

/*
 * UNP_GRPH_SURFACE_IGNORE_UPDATE_LOCK enum
 */

typedef enum UNP_GRPH_SURFACE_IGNORE_UPDATE_LOCK {
UNP_GRPH_SURFACE_IGNORE_UPDATE_LOCK_0    = 0x00000000,
UNP_GRPH_SURFACE_IGNORE_UPDATE_LOCK_1    = 0x00000001,
} UNP_GRPH_SURFACE_IGNORE_UPDATE_LOCK;

/*
 * UNP_GRPH_MODE_DISABLE_MULTIPLE_UPDATE enum
 */

typedef enum UNP_GRPH_MODE_DISABLE_MULTIPLE_UPDATE {
UNP_GRPH_MODE_DISABLE_MULTIPLE_UPDATE_0  = 0x00000000,
UNP_GRPH_MODE_DISABLE_MULTIPLE_UPDATE_1  = 0x00000001,
} UNP_GRPH_MODE_DISABLE_MULTIPLE_UPDATE;

/*
 * UNP_GRPH_SURFACE_DISABLE_MULTIPLE_UPDATE enum
 */

typedef enum UNP_GRPH_SURFACE_DISABLE_MULTIPLE_UPDATE {
UNP_GRPH_SURFACE_DISABLE_MULTIPLE_UPDATE_0  = 0x00000000,
UNP_GRPH_SURFACE_DISABLE_MULTIPLE_UPDATE_1  = 0x00000001,
} UNP_GRPH_SURFACE_DISABLE_MULTIPLE_UPDATE;

/*
 * UNP_GRPH_STEREOSYNC_FLIP_EN enum
 */

typedef enum UNP_GRPH_STEREOSYNC_FLIP_EN {
UNP_GRPH_STEREOSYNC_FLIP_DISABLE         = 0x00000000,
UNP_GRPH_STEREOSYNC_FLIP_ENABLE          = 0x00000001,
} UNP_GRPH_STEREOSYNC_FLIP_EN;

/*
 * UNP_GRPH_STEREOSYNC_FLIP_MODE enum
 */

typedef enum UNP_GRPH_STEREOSYNC_FLIP_MODE {
UNP_GRPH_STEREOSYNC_FLIP_MODE_0          = 0x00000000,
UNP_GRPH_STEREOSYNC_FLIP_MODE_1          = 0x00000001,
UNP_GRPH_STEREOSYNC_FLIP_MODE_2          = 0x00000002,
UNP_GRPH_STEREOSYNC_FLIP_MODE_3          = 0x00000003,
} UNP_GRPH_STEREOSYNC_FLIP_MODE;

/*
 * UNP_GRPH_STACK_INTERLACE_FLIP_EN enum
 */

typedef enum UNP_GRPH_STACK_INTERLACE_FLIP_EN {
UNP_GRPH_STACK_INTERLACE_FLIP_DISABLE    = 0x00000000,
UNP_GRPH_STACK_INTERLACE_FLIP_ENABLE     = 0x00000001,
} UNP_GRPH_STACK_INTERLACE_FLIP_EN;

/*
 * UNP_GRPH_STACK_INTERLACE_FLIP_MODE enum
 */

typedef enum UNP_GRPH_STACK_INTERLACE_FLIP_MODE {
UNP_GRPH_STACK_INTERLACE_FLIP_MODE_0     = 0x00000000,
UNP_GRPH_STACK_INTERLACE_FLIP_MODE_1     = 0x00000001,
UNP_GRPH_STACK_INTERLACE_FLIP_MODE_2     = 0x00000002,
UNP_GRPH_STACK_INTERLACE_FLIP_MODE_3     = 0x00000003,
} UNP_GRPH_STACK_INTERLACE_FLIP_MODE;

/*
 * UNP_GRPH_STEREOSYNC_SELECT_DISABLE enum
 */

typedef enum UNP_GRPH_STEREOSYNC_SELECT_DISABLE {
UNP_GRPH_STEREOSYNC_SELECT_EN            = 0x00000000,
UNP_GRPH_STEREOSYNC_SELECT_DIS           = 0x00000001,
} UNP_GRPH_STEREOSYNC_SELECT_DISABLE;

/*
 * UNP_CRC_SOURCE_SEL enum
 */

typedef enum UNP_CRC_SOURCE_SEL {
UNP_CRC_SOURCE_SEL_NP_TO_LBV             = 0x00000000,
UNP_CRC_SOURCE_SEL_LOWER32               = 0x00000001,
UNP_CRC_SOURCE_SEL_RESERVED              = 0x00000002,
UNP_CRC_SOURCE_SEL_LOWER16               = 0x00000003,
UNP_CRC_SOURCE_SEL_UNP_TO_LBV            = 0x00000004,
} UNP_CRC_SOURCE_SEL;

/*
 * UNP_CRC_LINE_SEL enum
 */

typedef enum UNP_CRC_LINE_SEL {
UNP_CRC_LINE_SEL_RESERVED                = 0x00000000,
UNP_CRC_LINE_SEL_EVEN_ONLY               = 0x00000001,
UNP_CRC_LINE_SEL_ODD_ONLY                = 0x00000002,
UNP_CRC_LINE_SEL_ODD_EVEN                = 0x00000003,
} UNP_CRC_LINE_SEL;

/*
 * UNP_ROTATION_ANGLE enum
 */

typedef enum UNP_ROTATION_ANGLE {
UNP_ROTATION_ANGLE_0                     = 0x00000000,
UNP_ROTATION_ANGLE_90                    = 0x00000001,
UNP_ROTATION_ANGLE_180                   = 0x00000002,
UNP_ROTATION_ANGLE_270                   = 0x00000003,
UNP_ROTATION_ANGLE_0m                    = 0x00000004,
UNP_ROTATION_ANGLE_90m                   = 0x00000005,
UNP_ROTATION_ANGLE_180m                  = 0x00000006,
UNP_ROTATION_ANGLE_270m                  = 0x00000007,
} UNP_ROTATION_ANGLE;

/*
 * UNP_PIXEL_DROP enum
 */

typedef enum UNP_PIXEL_DROP {
UNP_PIXEL_NO_DROP                        = 0x00000000,
UNP_PIXEL_DROPPING                       = 0x00000001,
} UNP_PIXEL_DROP;

/*
 * UNP_BUFFER_MODE enum
 */

typedef enum UNP_BUFFER_MODE {
UNP_BUFFER_MODE_LUMA                     = 0x00000000,
UNP_BUFFER_MODE_LUMA_CHROMA              = 0x00000001,
} UNP_BUFFER_MODE;

/*******************************************************
 * DP Enums
 *******************************************************/

/*
 * DP_LINK_TRAINING_COMPLETE enum
 */

typedef enum DP_LINK_TRAINING_COMPLETE {
DP_LINK_TRAINING_NOT_COMPLETE            = 0x00000000,
DP_LINK_TRAINING_ALREADY_COMPLETE        = 0x00000001,
} DP_LINK_TRAINING_COMPLETE;

/*
 * DP_EMBEDDED_PANEL_MODE enum
 */

typedef enum DP_EMBEDDED_PANEL_MODE {
DP_EXTERNAL_PANEL                        = 0x00000000,
DP_EMBEDDED_PANEL                        = 0x00000001,
} DP_EMBEDDED_PANEL_MODE;

/*
 * DP_PIXEL_ENCODING enum
 */

typedef enum DP_PIXEL_ENCODING {
DP_PIXEL_ENCODING_RGB444                 = 0x00000000,
DP_PIXEL_ENCODING_YCBCR422               = 0x00000001,
DP_PIXEL_ENCODING_YCBCR444               = 0x00000002,
DP_PIXEL_ENCODING_RGB_WIDE_GAMUT         = 0x00000003,
DP_PIXEL_ENCODING_Y_ONLY                 = 0x00000004,
DP_PIXEL_ENCODING_YCBCR420               = 0x00000005,
DP_PIXEL_ENCODING_RESERVED               = 0x00000006,
} DP_PIXEL_ENCODING;

/*
 * DP_DYN_RANGE enum
 */

typedef enum DP_DYN_RANGE {
DP_DYN_VESA_RANGE                        = 0x00000000,
DP_DYN_CEA_RANGE                         = 0x00000001,
} DP_DYN_RANGE;

/*
 * DP_YCBCR_RANGE enum
 */

typedef enum DP_YCBCR_RANGE {
DP_YCBCR_RANGE_BT601_5                   = 0x00000000,
DP_YCBCR_RANGE_BT709_5                   = 0x00000001,
} DP_YCBCR_RANGE;

/*
 * DP_COMPONENT_DEPTH enum
 */

typedef enum DP_COMPONENT_DEPTH {
DP_COMPONENT_DEPTH_6BPC                  = 0x00000000,
DP_COMPONENT_DEPTH_8BPC                  = 0x00000001,
DP_COMPONENT_DEPTH_10BPC                 = 0x00000002,
DP_COMPONENT_DEPTH_12BPC                 = 0x00000003,
DP_COMPONENT_DEPTH_16BPC_RESERVED        = 0x00000004,
DP_COMPONENT_DEPTH_RESERVED              = 0x00000005,
} DP_COMPONENT_DEPTH;

/*
 * DP_MSA_MISC0_OVERRIDE_ENABLE enum
 */

typedef enum DP_MSA_MISC0_OVERRIDE_ENABLE {
MSA_MISC0_OVERRIDE_DISABLE               = 0x00000000,
MSA_MISC0_OVERRIDE_ENABLE                = 0x00000001,
} DP_MSA_MISC0_OVERRIDE_ENABLE;

/*
 * DP_MSA_MISC1_BIT7_OVERRIDE_ENABLE enum
 */

typedef enum DP_MSA_MISC1_BIT7_OVERRIDE_ENABLE {
MSA_MISC1_BIT7_OVERRIDE_DISABLE          = 0x00000000,
MSA_MISC1_BIT7_OVERRIDE_ENABLE           = 0x00000001,
} DP_MSA_MISC1_BIT7_OVERRIDE_ENABLE;

/*
 * DP_UDI_LANES enum
 */

typedef enum DP_UDI_LANES {
DP_UDI_1_LANE                            = 0x00000000,
DP_UDI_2_LANES                           = 0x00000001,
DP_UDI_LANES_RESERVED                    = 0x00000002,
DP_UDI_4_LANES                           = 0x00000003,
} DP_UDI_LANES;

/*
 * DP_VID_STREAM_DIS_DEFER enum
 */

typedef enum DP_VID_STREAM_DIS_DEFER {
DP_VID_STREAM_DIS_NO_DEFER               = 0x00000000,
DP_VID_STREAM_DIS_DEFER_TO_HBLANK        = 0x00000001,
DP_VID_STREAM_DIS_DEFER_TO_VBLANK        = 0x00000002,
} DP_VID_STREAM_DIS_DEFER;

/*
 * DP_STEER_OVERFLOW_ACK enum
 */

typedef enum DP_STEER_OVERFLOW_ACK {
DP_STEER_OVERFLOW_ACK_NO_EFFECT          = 0x00000000,
DP_STEER_OVERFLOW_ACK_CLR_INTERRUPT      = 0x00000001,
} DP_STEER_OVERFLOW_ACK;

/*
 * DP_STEER_OVERFLOW_MASK enum
 */

typedef enum DP_STEER_OVERFLOW_MASK {
DP_STEER_OVERFLOW_MASKED                 = 0x00000000,
DP_STEER_OVERFLOW_UNMASK                 = 0x00000001,
} DP_STEER_OVERFLOW_MASK;

/*
 * DP_TU_OVERFLOW_ACK enum
 */

typedef enum DP_TU_OVERFLOW_ACK {
DP_TU_OVERFLOW_ACK_NO_EFFECT             = 0x00000000,
DP_TU_OVERFLOW_ACK_CLR_INTERRUPT         = 0x00000001,
} DP_TU_OVERFLOW_ACK;

/*
 * DPHY_ALT_SCRAMBLER_RESET_EN enum
 */

typedef enum DPHY_ALT_SCRAMBLER_RESET_EN {
DPHY_ALT_SCRAMBLER_REGULAR_RESET_VALUE   = 0x00000000,
DPHY_ALT_SCRAMBLER_INTERNAL_RESET_SOLUTION  = 0x00000001,
} DPHY_ALT_SCRAMBLER_RESET_EN;

/*
 * DPHY_ALT_SCRAMBLER_RESET_SEL enum
 */

typedef enum DPHY_ALT_SCRAMBLER_RESET_SEL {
DPHY_ALT_SCRAMBLER_RESET_SEL_EDP_RESET_VALUE  = 0x00000000,
DPHY_ALT_SCRAMBLER_RESET_SEL_CUSTOM_RESET_VALUE  = 0x00000001,
} DPHY_ALT_SCRAMBLER_RESET_SEL;

/*
 * DP_VID_TIMING_MODE enum
 */

typedef enum DP_VID_TIMING_MODE {
DP_VID_TIMING_MODE_ASYNC                 = 0x00000000,
DP_VID_TIMING_MODE_SYNC                  = 0x00000001,
} DP_VID_TIMING_MODE;

/*
 * DP_VID_M_N_DOUBLE_BUFFER_MODE enum
 */

typedef enum DP_VID_M_N_DOUBLE_BUFFER_MODE {
DP_VID_M_N_DOUBLE_BUFFER_AFTER_VID_M_UPDATE  = 0x00000000,
DP_VID_M_N_DOUBLE_BUFFER_AT_FRAME_START  = 0x00000001,
} DP_VID_M_N_DOUBLE_BUFFER_MODE;

/*
 * DP_VID_M_N_GEN_EN enum
 */

typedef enum DP_VID_M_N_GEN_EN {
DP_VID_M_N_PROGRAMMED_VIA_REG            = 0x00000000,
DP_VID_M_N_CALC_AUTO                     = 0x00000001,
} DP_VID_M_N_GEN_EN;

/*
 * DP_VID_M_DOUBLE_VALUE_EN enum
 */

typedef enum DP_VID_M_DOUBLE_VALUE_EN {
DP_VID_M_INPUT_PIXEL_RATE                = 0x00000000,
DP_VID_M_DOUBLE_INPUT_PIXEL_RATE         = 0x00000001,
} DP_VID_M_DOUBLE_VALUE_EN;

/*
 * DP_VID_ENHANCED_FRAME_MODE enum
 */

typedef enum DP_VID_ENHANCED_FRAME_MODE {
VID_NORMAL_FRAME_MODE                    = 0x00000000,
VID_ENHANCED_MODE                        = 0x00000001,
} DP_VID_ENHANCED_FRAME_MODE;

/*
 * DP_VID_MSA_TOP_FIELD_MODE enum
 */

typedef enum DP_VID_MSA_TOP_FIELD_MODE {
DP_TOP_FIELD_ONLY                        = 0x00000000,
DP_TOP_PLUS_BOTTOM_FIELD                 = 0x00000001,
} DP_VID_MSA_TOP_FIELD_MODE;

/*
 * DP_VID_VBID_FIELD_POL enum
 */

typedef enum DP_VID_VBID_FIELD_POL {
DP_VID_VBID_FIELD_POL_NORMAL             = 0x00000000,
DP_VID_VBID_FIELD_POL_INV                = 0x00000001,
} DP_VID_VBID_FIELD_POL;

/*
 * DP_VID_STREAM_DISABLE_ACK enum
 */

typedef enum DP_VID_STREAM_DISABLE_ACK {
ID_STREAM_DISABLE_NO_ACK                 = 0x00000000,
ID_STREAM_DISABLE_ACKED                  = 0x00000001,
} DP_VID_STREAM_DISABLE_ACK;

/*
 * DP_VID_STREAM_DISABLE_MASK enum
 */

typedef enum DP_VID_STREAM_DISABLE_MASK {
VID_STREAM_DISABLE_MASKED                = 0x00000000,
VID_STREAM_DISABLE_UNMASK                = 0x00000001,
} DP_VID_STREAM_DISABLE_MASK;

/*
 * DPHY_ATEST_SEL_LANE0 enum
 */

typedef enum DPHY_ATEST_SEL_LANE0 {
DPHY_ATEST_LANE0_PRBS_PATTERN            = 0x00000000,
DPHY_ATEST_LANE0_REG_PATTERN             = 0x00000001,
} DPHY_ATEST_SEL_LANE0;

/*
 * DPHY_ATEST_SEL_LANE1 enum
 */

typedef enum DPHY_ATEST_SEL_LANE1 {
DPHY_ATEST_LANE1_PRBS_PATTERN            = 0x00000000,
DPHY_ATEST_LANE1_REG_PATTERN             = 0x00000001,
} DPHY_ATEST_SEL_LANE1;

/*
 * DPHY_ATEST_SEL_LANE2 enum
 */

typedef enum DPHY_ATEST_SEL_LANE2 {
DPHY_ATEST_LANE2_PRBS_PATTERN            = 0x00000000,
DPHY_ATEST_LANE2_REG_PATTERN             = 0x00000001,
} DPHY_ATEST_SEL_LANE2;

/*
 * DPHY_ATEST_SEL_LANE3 enum
 */

typedef enum DPHY_ATEST_SEL_LANE3 {
DPHY_ATEST_LANE3_PRBS_PATTERN            = 0x00000000,
DPHY_ATEST_LANE3_REG_PATTERN             = 0x00000001,
} DPHY_ATEST_SEL_LANE3;

/*
 * DPHY_SCRAMBLER_SEL enum
 */

typedef enum DPHY_SCRAMBLER_SEL {
DPHY_SCRAMBLER_SEL_LANE_DATA             = 0x00000000,
DPHY_SCRAMBLER_SEL_DBG_DATA              = 0x00000001,
} DPHY_SCRAMBLER_SEL;

/*
 * DPHY_BYPASS enum
 */

typedef enum DPHY_BYPASS {
DPHY_8B10B_OUTPUT                        = 0x00000000,
DPHY_DBG_OUTPUT                          = 0x00000001,
} DPHY_BYPASS;

/*
 * DPHY_SKEW_BYPASS enum
 */

typedef enum DPHY_SKEW_BYPASS {
DPHY_WITH_SKEW                           = 0x00000000,
DPHY_NO_SKEW                             = 0x00000001,
} DPHY_SKEW_BYPASS;

/*
 * DPHY_TRAINING_PATTERN_SEL enum
 */

typedef enum DPHY_TRAINING_PATTERN_SEL {
DPHY_TRAINING_PATTERN_1                  = 0x00000000,
DPHY_TRAINING_PATTERN_2                  = 0x00000001,
DPHY_TRAINING_PATTERN_3                  = 0x00000002,
DPHY_TRAINING_PATTERN_4                  = 0x00000003,
} DPHY_TRAINING_PATTERN_SEL;

/*
 * DPHY_8B10B_RESET enum
 */

typedef enum DPHY_8B10B_RESET {
DPHY_8B10B_NOT_RESET                     = 0x00000000,
DPHY_8B10B_RESETET                       = 0x00000001,
} DPHY_8B10B_RESET;

/*
 * DP_DPHY_8B10B_EXT_DISP enum
 */

typedef enum DP_DPHY_8B10B_EXT_DISP {
DP_DPHY_8B10B_EXT_DISP_ZERO              = 0x00000000,
DP_DPHY_8B10B_EXT_DISP_ONE               = 0x00000001,
} DP_DPHY_8B10B_EXT_DISP;

/*
 * DPHY_8B10B_CUR_DISP enum
 */

typedef enum DPHY_8B10B_CUR_DISP {
DPHY_8B10B_CUR_DISP_ZERO                 = 0x00000000,
DPHY_8B10B_CUR_DISP_ONE                  = 0x00000001,
} DPHY_8B10B_CUR_DISP;

/*
 * DPHY_PRBS_EN enum
 */

typedef enum DPHY_PRBS_EN {
DPHY_PRBS_DISABLE                        = 0x00000000,
DPHY_PRBS_ENABLE                         = 0x00000001,
} DPHY_PRBS_EN;

/*
 * DPHY_PRBS_SEL enum
 */

typedef enum DPHY_PRBS_SEL {
DPHY_PRBS7_SELECTED                      = 0x00000000,
DPHY_PRBS23_SELECTED                     = 0x00000001,
DPHY_PRBS11_SELECTED                     = 0x00000002,
} DPHY_PRBS_SEL;

/*
 * DPHY_SCRAMBLER_DIS enum
 */

typedef enum DPHY_SCRAMBLER_DIS {
DPHY_SCR_ENABLED                         = 0x00000000,
DPHY_SCR_DISABLED                        = 0x00000001,
} DPHY_SCRAMBLER_DIS;

/*
 * DPHY_SCRAMBLER_ADVANCE enum
 */

typedef enum DPHY_SCRAMBLER_ADVANCE {
DPHY_DPHY_SCRAMBLER_ADVANCE_ON_DATA_SYMBOL_ONLY  = 0x00000000,
DPHY_SCRAMBLER_ADVANCE_ON_BOTH_DATA_AND_CTRL  = 0x00000001,
} DPHY_SCRAMBLER_ADVANCE;

/*
 * DPHY_SCRAMBLER_KCODE enum
 */

typedef enum DPHY_SCRAMBLER_KCODE {
DPHY_SCRAMBLER_KCODE_DISABLED            = 0x00000000,
DPHY_SCRAMBLER_KCODE_ENABLED             = 0x00000001,
} DPHY_SCRAMBLER_KCODE;

/*
 * DPHY_LOAD_BS_COUNT_START enum
 */

typedef enum DPHY_LOAD_BS_COUNT_START {
DPHY_LOAD_BS_COUNT_STARTED               = 0x00000000,
DPHY_LOAD_BS_COUNT_NOT_STARTED           = 0x00000001,
} DPHY_LOAD_BS_COUNT_START;

/*
 * DPHY_CRC_EN enum
 */

typedef enum DPHY_CRC_EN {
DPHY_CRC_DISABLED                        = 0x00000000,
DPHY_CRC_ENABLED                         = 0x00000001,
} DPHY_CRC_EN;

/*
 * DPHY_CRC_CONT_EN enum
 */

typedef enum DPHY_CRC_CONT_EN {
DPHY_CRC_ONE_SHOT                        = 0x00000000,
DPHY_CRC_CONTINUOUS                      = 0x00000001,
} DPHY_CRC_CONT_EN;

/*
 * DPHY_CRC_FIELD enum
 */

typedef enum DPHY_CRC_FIELD {
DPHY_CRC_START_FROM_TOP_FIELD            = 0x00000000,
DPHY_CRC_START_FROM_BOTTOM_FIELD         = 0x00000001,
} DPHY_CRC_FIELD;

/*
 * DPHY_CRC_SEL enum
 */

typedef enum DPHY_CRC_SEL {
DPHY_CRC_LANE0_SELECTED                  = 0x00000000,
DPHY_CRC_LANE1_SELECTED                  = 0x00000001,
DPHY_CRC_LANE2_SELECTED                  = 0x00000002,
DPHY_CRC_LANE3_SELECTED                  = 0x00000003,
} DPHY_CRC_SEL;

/*
 * DPHY_RX_FAST_TRAINING_CAPABLE enum
 */

typedef enum DPHY_RX_FAST_TRAINING_CAPABLE {
DPHY_FAST_TRAINING_NOT_CAPABLE_0         = 0x00000000,
DPHY_FAST_TRAINING_CAPABLE               = 0x00000001,
} DPHY_RX_FAST_TRAINING_CAPABLE;

/*
 * DP_SEC_COLLISION_ACK enum
 */

typedef enum DP_SEC_COLLISION_ACK {
DP_SEC_COLLISION_ACK_NO_EFFECT           = 0x00000000,
DP_SEC_COLLISION_ACK_CLR_FLAG            = 0x00000001,
} DP_SEC_COLLISION_ACK;

/*
 * DP_SEC_AUDIO_MUTE enum
 */

typedef enum DP_SEC_AUDIO_MUTE {
DP_SEC_AUDIO_MUTE_HW_CTRL                = 0x00000000,
DP_SEC_AUDIO_MUTE_SW_CTRL                = 0x00000001,
} DP_SEC_AUDIO_MUTE;

/*
 * DP_SEC_TIMESTAMP_MODE enum
 */

typedef enum DP_SEC_TIMESTAMP_MODE {
DP_SEC_TIMESTAMP_PROGRAMMABLE_MODE       = 0x00000000,
DP_SEC_TIMESTAMP_AUTO_CALC_MODE          = 0x00000001,
} DP_SEC_TIMESTAMP_MODE;

/*
 * DP_SEC_ASP_PRIORITY enum
 */

typedef enum DP_SEC_ASP_PRIORITY {
DP_SEC_ASP_LOW_PRIORITY                  = 0x00000000,
DP_SEC_ASP_HIGH_PRIORITY                 = 0x00000001,
} DP_SEC_ASP_PRIORITY;

/*
 * DP_SEC_ASP_CHANNEL_COUNT_OVERRIDE enum
 */

typedef enum DP_SEC_ASP_CHANNEL_COUNT_OVERRIDE {
DP_SEC_ASP_CHANNEL_COUNT_FROM_AZ         = 0x00000000,
DP_SEC_ASP_CHANNEL_COUNT_OVERRIDE_ENABLED  = 0x00000001,
} DP_SEC_ASP_CHANNEL_COUNT_OVERRIDE;

/*
 * DP_MSE_SAT_UPDATE_ACT enum
 */

typedef enum DP_MSE_SAT_UPDATE_ACT {
DP_MSE_SAT_UPDATE_NO_ACTION              = 0x00000000,
DP_MSE_SAT_UPDATE_WITH_TRIGGER           = 0x00000001,
DP_MSE_SAT_UPDATE_WITHOUT_TRIGGER        = 0x00000002,
} DP_MSE_SAT_UPDATE_ACT;

/*
 * DP_MSE_LINK_LINE enum
 */

typedef enum DP_MSE_LINK_LINE {
DP_MSE_LINK_LINE_32_MTP_LONG             = 0x00000000,
DP_MSE_LINK_LINE_64_MTP_LONG             = 0x00000001,
DP_MSE_LINK_LINE_128_MTP_LONG            = 0x00000002,
DP_MSE_LINK_LINE_256_MTP_LONG            = 0x00000003,
} DP_MSE_LINK_LINE;

/*
 * DP_MSE_BLANK_CODE enum
 */

typedef enum DP_MSE_BLANK_CODE {
DP_MSE_BLANK_CODE_SF_FILLED              = 0x00000000,
DP_MSE_BLANK_CODE_ZERO_FILLED            = 0x00000001,
} DP_MSE_BLANK_CODE;

/*
 * DP_MSE_TIMESTAMP_MODE enum
 */

typedef enum DP_MSE_TIMESTAMP_MODE {
DP_MSE_TIMESTAMP_CALC_BASED_ON_LINK_RATE  = 0x00000000,
DP_MSE_TIMESTAMP_CALC_BASED_ON_VC_RATE   = 0x00000001,
} DP_MSE_TIMESTAMP_MODE;

/*
 * DP_MSE_ZERO_ENCODER enum
 */

typedef enum DP_MSE_ZERO_ENCODER {
DP_MSE_NOT_ZERO_FE_ENCODER               = 0x00000000,
DP_MSE_ZERO_FE_ENCODER                   = 0x00000001,
} DP_MSE_ZERO_ENCODER;

/*
 * DP_MSE_OUTPUT_DPDBG_DATA enum
 */

typedef enum DP_MSE_OUTPUT_DPDBG_DATA {
DP_MSE_OUTPUT_DPDBG_DATA_DIS             = 0x00000000,
DP_MSE_OUTPUT_DPDBG_DATA_EN              = 0x00000001,
} DP_MSE_OUTPUT_DPDBG_DATA;

/*
 * DP_DPHY_HBR2_PATTERN_CONTROL_MODE enum
 */

typedef enum DP_DPHY_HBR2_PATTERN_CONTROL_MODE {
DP_DPHY_HBR2_PASS_THROUGH                = 0x00000000,
DP_DPHY_HBR2_PATTERN_1                   = 0x00000001,
DP_DPHY_HBR2_PATTERN_2_NEG               = 0x00000002,
DP_DPHY_HBR2_PATTERN_3                   = 0x00000003,
DP_DPHY_HBR2_PATTERN_2_POS               = 0x00000006,
} DP_DPHY_HBR2_PATTERN_CONTROL_MODE;

/*
 * DPHY_CRC_MST_PHASE_ERROR_ACK enum
 */

typedef enum DPHY_CRC_MST_PHASE_ERROR_ACK {
DPHY_CRC_MST_PHASE_ERROR_NO_ACK          = 0x00000000,
DPHY_CRC_MST_PHASE_ERROR_ACKED           = 0x00000001,
} DPHY_CRC_MST_PHASE_ERROR_ACK;

/*
 * DPHY_SW_FAST_TRAINING_START enum
 */

typedef enum DPHY_SW_FAST_TRAINING_START {
DPHY_SW_FAST_TRAINING_NOT_STARTED        = 0x00000000,
DPHY_SW_FAST_TRAINING_STARTED            = 0x00000001,
} DPHY_SW_FAST_TRAINING_START;

/*
 * DP_DPHY_FAST_TRAINING_VBLANK_EDGE_DETECT_EN enum
 */

typedef enum DP_DPHY_FAST_TRAINING_VBLANK_EDGE_DETECT_EN {
DP_DPHY_FAST_TRAINING_VBLANK_EDGE_DETECT_DISABLED  = 0x00000000,
DP_DPHY_FAST_TRAINING_VBLANK_EDGE_DETECT_ENABLED  = 0x00000001,
} DP_DPHY_FAST_TRAINING_VBLANK_EDGE_DETECT_EN;

/*
 * DP_DPHY_FAST_TRAINING_COMPLETE_MASK enum
 */

typedef enum DP_DPHY_FAST_TRAINING_COMPLETE_MASK {
DP_DPHY_FAST_TRAINING_COMPLETE_MASKED    = 0x00000000,
DP_DPHY_FAST_TRAINING_COMPLETE_NOT_MASKED  = 0x00000001,
} DP_DPHY_FAST_TRAINING_COMPLETE_MASK;

/*
 * DP_DPHY_FAST_TRAINING_COMPLETE_ACK enum
 */

typedef enum DP_DPHY_FAST_TRAINING_COMPLETE_ACK {
DP_DPHY_FAST_TRAINING_COMPLETE_NOT_ACKED  = 0x00000000,
DP_DPHY_FAST_TRAINING_COMPLETE_ACKED     = 0x00000001,
} DP_DPHY_FAST_TRAINING_COMPLETE_ACK;

/*
 * DP_MSA_V_TIMING_OVERRIDE_EN enum
 */

typedef enum DP_MSA_V_TIMING_OVERRIDE_EN {
MSA_V_TIMING_OVERRIDE_DISABLED           = 0x00000000,
MSA_V_TIMING_OVERRIDE_ENABLED            = 0x00000001,
} DP_MSA_V_TIMING_OVERRIDE_EN;

/*
 * DP_SEC_GSP0_PRIORITY enum
 */

typedef enum DP_SEC_GSP0_PRIORITY {
SEC_GSP0_PRIORITY_LOW                    = 0x00000000,
SEC_GSP0_PRIORITY_HIGH                   = 0x00000001,
} DP_SEC_GSP0_PRIORITY;

/*
 * DP_SEC_GSP0_SEND enum
 */

typedef enum DP_SEC_GSP0_SEND {
NOT_SENT                                 = 0x00000000,
FORCE_SENT                               = 0x00000001,
} DP_SEC_GSP0_SEND;

/*******************************************************
 * COL_MAN Enums
 *******************************************************/

/*
 * COL_MAN_UPDATE_LOCK enum
 */

typedef enum COL_MAN_UPDATE_LOCK {
COL_MAN_UPDATE_UNLOCKED                  = 0x00000000,
COL_MAN_UPDATE_LOCKED                    = 0x00000001,
} COL_MAN_UPDATE_LOCK;

/*
 * COL_MAN_DISABLE_MULTIPLE_UPDATE enum
 */

typedef enum COL_MAN_DISABLE_MULTIPLE_UPDATE {
COL_MAN_MULTIPLE_UPDATE                  = 0x00000000,
COL_MAN_MULTIPLE_UPDAT_EDISABLE          = 0x00000001,
} COL_MAN_DISABLE_MULTIPLE_UPDATE;

/*
 * COL_MAN_INPUTCSC_MODE enum
 */

typedef enum COL_MAN_INPUTCSC_MODE {
INPUTCSC_MODE_BYPASS                     = 0x00000000,
INPUTCSC_MODE_A                          = 0x00000001,
INPUTCSC_MODE_B                          = 0x00000002,
INPUTCSC_MODE_UNITY                      = 0x00000003,
} COL_MAN_INPUTCSC_MODE;

/*
 * COL_MAN_INPUTCSC_TYPE enum
 */

typedef enum COL_MAN_INPUTCSC_TYPE {
INPUTCSC_TYPE_12_0                       = 0x00000000,
INPUTCSC_TYPE_10_2                       = 0x00000001,
INPUTCSC_TYPE_8_4                        = 0x00000002,
} COL_MAN_INPUTCSC_TYPE;

/*
 * COL_MAN_INPUTCSC_CONVERT enum
 */

typedef enum COL_MAN_INPUTCSC_CONVERT {
INPUTCSC_ROUND                           = 0x00000000,
INPUTCSC_TRUNCATE                        = 0x00000001,
} COL_MAN_INPUTCSC_CONVERT;

/*
 * COL_MAN_PRESCALE_MODE enum
 */

typedef enum COL_MAN_PRESCALE_MODE {
PRESCALE_MODE_BYPASS                     = 0x00000000,
PRESCALE_MODE_PROGRAM                    = 0x00000001,
PRESCALE_MODE_UNITY                      = 0x00000002,
} COL_MAN_PRESCALE_MODE;

/*
 * COL_MAN_INPUT_GAMMA_MODE enum
 */

typedef enum COL_MAN_INPUT_GAMMA_MODE {
INGAMMA_MODE_BYPASS                      = 0x00000000,
INGAMMA_MODE_FIX                         = 0x00000001,
INGAMMA_MODE_FLOAT                       = 0x00000002,
} COL_MAN_INPUT_GAMMA_MODE;

/*
 * COL_MAN_OUTPUT_CSC_MODE enum
 */

typedef enum COL_MAN_OUTPUT_CSC_MODE {
COL_MAN_OUTPUT_CSC_BYPASS                = 0x00000000,
COL_MAN_OUTPUT_CSC_RGB                   = 0x00000001,
COL_MAN_OUTPUT_CSC_YCrCb601              = 0x00000002,
COL_MAN_OUTPUT_CSC_YCrCb709              = 0x00000003,
COL_MAN_OUTPUT_CSC_A                     = 0x00000004,
COL_MAN_OUTPUT_CSC_B                     = 0x00000005,
COL_MAN_OUTPUT_CSC_UNITY                 = 0x00000006,
} COL_MAN_OUTPUT_CSC_MODE;

/*
 * COL_MAN_DENORM_CLAMP_CONTROL enum
 */

typedef enum COL_MAN_DENORM_CLAMP_CONTROL {
DENORM_CLAMP_MODE_UNITY                  = 0x00000000,
DENORM_CLAMP_MODE_8                      = 0x00000001,
DENORM_CLAMP_MODE_10                     = 0x00000002,
DENORM_CLAMP_MODE_12                     = 0x00000003,
} COL_MAN_DENORM_CLAMP_CONTROL;

/*
 * COL_MAN_REGAMMA_MODE_CONTROL enum
 */

typedef enum COL_MAN_REGAMMA_MODE_CONTROL {
COL_MAN_REGAMMA_MODE_BYPASS              = 0x00000000,
COL_MAN_REGAMMA_MODE_ROM_A               = 0x00000001,
COL_MAN_REGAMMA_MODE_ROM_B               = 0x00000002,
COL_MAN_REGAMMA_MODE_A                   = 0x00000003,
COL_MAN_REGAMMA_MODE_B                   = 0x00000004,
} COL_MAN_REGAMMA_MODE_CONTROL;

/*
 * COL_MAN_GLOBAL_PASSTHROUGH_ENABLE enum
 */

typedef enum COL_MAN_GLOBAL_PASSTHROUGH_ENABLE {
CM_GLOBAL_PASSTHROUGH_DISBALE            = 0x00000000,
CM_GLOBAL_PASSTHROUGH_ENABLE             = 0x00000001,
} COL_MAN_GLOBAL_PASSTHROUGH_ENABLE;

/*
 * COL_MAN_DEGAMMA_MODE enum
 */

typedef enum COL_MAN_DEGAMMA_MODE {
DEGAMMA_MODE_BYPASS                      = 0x00000000,
DEGAMMA_MODE_A                           = 0x00000001,
DEGAMMA_MODE_B                           = 0x00000002,
} COL_MAN_DEGAMMA_MODE;

/*
 * COL_MAN_GAMUT_REMAP_MODE enum
 */

typedef enum COL_MAN_GAMUT_REMAP_MODE {
GAMUT_REMAP_MODE_BYPASS                  = 0x00000000,
GAMUT_REMAP_MODE_1                       = 0x00000001,
GAMUT_REMAP_MODE_2                       = 0x00000002,
GAMUT_REMAP_MODE_3                       = 0x00000003,
} COL_MAN_GAMUT_REMAP_MODE;

/*******************************************************
 * MCIF_WB Enums
 *******************************************************/

/*******************************************************
 * DP_AUX Enums
 *******************************************************/

/*
 * DP_AUX_CONTROL_HPD_SEL enum
 */

typedef enum DP_AUX_CONTROL_HPD_SEL {
DP_AUX_CONTROL_HPD1_SELECTED             = 0x00000000,
DP_AUX_CONTROL_HPD2_SELECTED             = 0x00000001,
DP_AUX_CONTROL_HPD3_SELECTED             = 0x00000002,
DP_AUX_CONTROL_HPD4_SELECTED             = 0x00000003,
DP_AUX_CONTROL_HPD5_SELECTED             = 0x00000004,
DP_AUX_CONTROL_HPD6_SELECTED             = 0x00000005,
} DP_AUX_CONTROL_HPD_SEL;

/*
 * DP_AUX_CONTROL_TEST_MODE enum
 */

typedef enum DP_AUX_CONTROL_TEST_MODE {
DP_AUX_CONTROL_TEST_MODE_DISABLE         = 0x00000000,
DP_AUX_CONTROL_TEST_MODE_ENABLE          = 0x00000001,
} DP_AUX_CONTROL_TEST_MODE;

/*
 * DP_AUX_SW_CONTROL_SW_GO enum
 */

typedef enum DP_AUX_SW_CONTROL_SW_GO {
DP_AUX_SW_CONTROL_SW__NOT_GO             = 0x00000000,
DP_AUX_SW_CONTROL_SW__GO                 = 0x00000001,
} DP_AUX_SW_CONTROL_SW_GO;

/*
 * DP_AUX_SW_CONTROL_LS_READ_TRIG enum
 */

typedef enum DP_AUX_SW_CONTROL_LS_READ_TRIG {
DP_AUX_SW_CONTROL_LS_READ__NOT_TRIG      = 0x00000000,
DP_AUX_SW_CONTROL_LS_READ__TRIG          = 0x00000001,
} DP_AUX_SW_CONTROL_LS_READ_TRIG;

/*
 * DP_AUX_ARB_CONTROL_ARB_PRIORITY enum
 */

typedef enum DP_AUX_ARB_CONTROL_ARB_PRIORITY {
DP_AUX_ARB_CONTROL_ARB_PRIORITY__GTC_LS_SW  = 0x00000000,
DP_AUX_ARB_CONTROL_ARB_PRIORITY__LS_GTC_SW  = 0x00000001,
DP_AUX_ARB_CONTROL_ARB_PRIORITY__SW_LS_GTC  = 0x00000002,
DP_AUX_ARB_CONTROL_ARB_PRIORITY__SW_GTC_LS  = 0x00000003,
} DP_AUX_ARB_CONTROL_ARB_PRIORITY;

/*
 * DP_AUX_ARB_CONTROL_USE_AUX_REG_REQ enum
 */

typedef enum DP_AUX_ARB_CONTROL_USE_AUX_REG_REQ {
DP_AUX_ARB_CONTROL__NOT_USE_AUX_REG_REQ  = 0x00000000,
DP_AUX_ARB_CONTROL__USE_AUX_REG_REQ      = 0x00000001,
} DP_AUX_ARB_CONTROL_USE_AUX_REG_REQ;

/*
 * DP_AUX_ARB_CONTROL_DONE_USING_AUX_REG enum
 */

typedef enum DP_AUX_ARB_CONTROL_DONE_USING_AUX_REG {
DP_AUX_ARB_CONTROL__DONE_NOT_USING_AUX_REG = 0x00000000,
DP_AUX_ARB_CONTROL__DONE_USING_AUX_REG   = 0x00000001,
} DP_AUX_ARB_CONTROL_DONE_USING_AUX_REG;

/*
 * DP_AUX_INT_ACK enum
 */

typedef enum DP_AUX_INT_ACK {
DP_AUX_INT__NOT_ACK                      = 0x00000000,
DP_AUX_INT__ACK                          = 0x00000001,
} DP_AUX_INT_ACK;

/*
 * DP_AUX_LS_UPDATE_ACK enum
 */

typedef enum DP_AUX_LS_UPDATE_ACK {
DP_AUX_INT_LS_UPDATE_NOT_ACK             = 0x00000000,
DP_AUX_INT_LS_UPDATE_ACK                 = 0x00000001,
} DP_AUX_LS_UPDATE_ACK;

/*
 * DP_AUX_DPHY_TX_REF_CONTROL_TX_REF_SEL enum
 */

typedef enum DP_AUX_DPHY_TX_REF_CONTROL_TX_REF_SEL {
DP_AUX_DPHY_TX_REF_CONTROL_TX_REF_SEL__DIVIDED_SYM_CLK  = 0x00000000,
DP_AUX_DPHY_TX_REF_CONTROL_TX_REF_SEL__FROM_DCCG_MICROSECOND_REF  = 0x00000001,
} DP_AUX_DPHY_TX_REF_CONTROL_TX_REF_SEL;

/*
 * DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE enum
 */

typedef enum DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE {
DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE__1MHZ = 0x00000000,
DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE__2MHZ = 0x00000001,
DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE__4MHZ = 0x00000002,
DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE__8MHZ = 0x00000003,
} DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE;

/*
 * DP_AUX_DPHY_TX_CONTROL_PRECHARGE_LEN enum
 */

typedef enum DP_AUX_DPHY_TX_CONTROL_PRECHARGE_LEN {
DP_AUX_DPHY_TX_CONTROL_PRECHARGE_LEN__0US = 0x00000000,
DP_AUX_DPHY_TX_CONTROL_PRECHARGE_LEN__8US = 0x00000001,
DP_AUX_DPHY_TX_CONTROL_PRECHARGE_LEN__16US = 0x00000002,
DP_AUX_DPHY_TX_CONTROL_PRECHARGE_LEN__24US = 0x00000003,
DP_AUX_DPHY_TX_CONTROL_PRECHARGE_LEN__32US = 0x00000004,
DP_AUX_DPHY_TX_CONTROL_PRECHARGE_LEN__40US = 0x00000005,
DP_AUX_DPHY_TX_CONTROL_PRECHARGE_LEN__48US = 0x00000006,
DP_AUX_DPHY_TX_CONTROL_PRECHARGE_LEN__56US = 0x00000007,
} DP_AUX_DPHY_TX_CONTROL_PRECHARGE_LEN;

/*
 * DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY enum
 */

typedef enum DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY {
DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY__0 = 0x00000000,
DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY__16US = 0x00000001,
DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY__32US = 0x00000002,
DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY__64US = 0x00000003,
DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY__128US = 0x00000004,
DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY__256US = 0x00000005,
} DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY;

/*
 * DP_AUX_DPHY_RX_CONTROL_START_WINDOW enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_START_WINDOW {
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO2_PERIOD  = 0x00000000,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO4_PERIOD  = 0x00000001,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO8_PERIOD  = 0x00000002,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO16_PERIOD  = 0x00000003,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO32_PERIOD  = 0x00000004,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO64_PERIOD  = 0x00000005,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO128_PERIOD  = 0x00000006,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO256_PERIOD  = 0x00000007,
} DP_AUX_DPHY_RX_CONTROL_START_WINDOW;

/*
 * DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW {
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO2_PERIOD  = 0x00000000,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO4_PERIOD  = 0x00000001,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO8_PERIOD  = 0x00000002,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO16_PERIOD  = 0x00000003,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO32_PERIOD  = 0x00000004,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO64_PERIOD  = 0x00000005,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO128_PERIOD  = 0x00000006,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO256_PERIOD  = 0x00000007,
} DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW;

/*
 * DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN {
DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN__6_EDGES = 0x00000000,
DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN__10_EDGES = 0x00000001,
DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN__18_EDGES = 0x00000002,
DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN__RESERVED = 0x00000003,
} DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN;

/*
 * DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_PHASE_DETECT enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_PHASE_DETECT {
DP_AUX_DPHY_RX_CONTROL__NOT_ALLOW_BELOW_THRESHOLD_PHASE_DETECT = 0x00000000,
DP_AUX_DPHY_RX_CONTROL__ALLOW_BELOW_THRESHOLD_PHASE_DETECT = 0x00000001,
} DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_PHASE_DETECT;

/*
 * DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_START enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_START {
DP_AUX_DPHY_RX_CONTROL__NOT_ALLOW_BELOW_THRESHOLD_START = 0x00000000,
DP_AUX_DPHY_RX_CONTROL__ALLOW_BELOW_THRESHOLD_START = 0x00000001,
} DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_START;

/*
 * DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_STOP enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_STOP {
DP_AUX_DPHY_RX_CONTROL__NOT_ALLOW_BELOW_THRESHOLD_STOP = 0x00000000,
DP_AUX_DPHY_RX_CONTROL__ALLOW_BELOW_THRESHOLD_STOP = 0x00000001,
} DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_STOP;

/*
 * DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN {
DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN__2_HALF_SYMBOLS = 0x00000000,
DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN__4_HALF_SYMBOLS = 0x00000001,
DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN__6_HALF_SYMBOLS = 0x00000002,
DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN__8_HALF_SYMBOLS = 0x00000003,
} DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN;

/*
 * DP_AUX_DPHY_RX_CONTROL_TIMEOUT_LEN enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_TIMEOUT_LEN {
DP_AUX_DPHY_RX_CONTROL_TIMEOUT_LEN_450US = 0x00000000,
DP_AUX_DPHY_RX_CONTROL_TIMEOUT_LEN_500US = 0x00000001,
DP_AUX_DPHY_RX_CONTROL_TIMEOUT_LEN_550US = 0x00000002,
DP_AUX_DPHY_RX_CONTROL_TIMEOUT_LEN_600US = 0x00000003,
DP_AUX_DPHY_RX_CONTROL_TIMEOUT_LEN_650US = 0x00000004,
DP_AUX_DPHY_RX_CONTROL_TIMEOUT_LEN_700US = 0x00000005,
DP_AUX_DPHY_RX_CONTROL_TIMEOUT_LEN_750US = 0x00000006,
DP_AUX_DPHY_RX_CONTROL_TIMEOUT_LEN_800US = 0x00000007,
} DP_AUX_DPHY_RX_CONTROL_TIMEOUT_LEN;

/*
 * DP_AUX_DPHY_RX_DETECTION_THRESHOLD enum
 */

typedef enum DP_AUX_DPHY_RX_DETECTION_THRESHOLD {
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__1to2  = 0x00000000,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__3to4  = 0x00000001,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__7to8  = 0x00000002,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__15to16  = 0x00000003,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__31to32  = 0x00000004,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__63to64  = 0x00000005,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__127to128  = 0x00000006,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__255to256  = 0x00000007,
} DP_AUX_DPHY_RX_DETECTION_THRESHOLD;

/*
 * DP_AUX_GTC_SYNC_CONTROL_GTC_SYNC_BLOCK_REQ enum
 */

typedef enum DP_AUX_GTC_SYNC_CONTROL_GTC_SYNC_BLOCK_REQ {
DP_AUX_GTC_SYNC_CONTROL_GTC_SYNC_ALLOW_REQ_FROM_OTHER_AUX  = 0x00000000,
DP_AUX_GTC_SYNC_CONTROL_GTC_SYNC_BLOCK_REQ_FROM_OTHER_AUX  = 0x00000001,
} DP_AUX_GTC_SYNC_CONTROL_GTC_SYNC_BLOCK_REQ;

/*
 * DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW enum
 */

typedef enum DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW {
DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW__300US = 0x00000000,
DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW__400US = 0x00000001,
DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW__500US = 0x00000002,
DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW__600US = 0x00000003,
} DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW;

/*
 * DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT enum
 */

typedef enum DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT {
DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT__4_ATTAMPS = 0x00000000,
DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT__8_ATTAMPS = 0x00000001,
DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT__16_ATTAMPS = 0x00000002,
DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT__RESERVED = 0x00000003,
} DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT;

/*
 * DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN enum
 */

typedef enum DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN {
DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN__0  = 0x00000000,
DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN__64  = 0x00000001,
DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN__128  = 0x00000002,
DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN__256  = 0x00000003,
} DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN;

/*
 * DP_AUX_ERR_OCCURRED_ACK enum
 */

typedef enum DP_AUX_ERR_OCCURRED_ACK {
DP_AUX_ERR_OCCURRED__NOT_ACK             = 0x00000000,
DP_AUX_ERR_OCCURRED__ACK                 = 0x00000001,
} DP_AUX_ERR_OCCURRED_ACK;

/*
 * DP_AUX_POTENTIAL_ERR_REACHED_ACK enum
 */

typedef enum DP_AUX_POTENTIAL_ERR_REACHED_ACK {
DP_AUX_POTENTIAL_ERR_REACHED__NOT_ACK    = 0x00000000,
DP_AUX_POTENTIAL_ERR_REACHED__ACK        = 0x00000001,
} DP_AUX_POTENTIAL_ERR_REACHED_ACK;

/*
 * DP_AUX_DEFINITE_ERR_REACHED_ACK enum
 */

typedef enum DP_AUX_DEFINITE_ERR_REACHED_ACK {
ALPHA_DP_AUX_DEFINITE_ERR_REACHED_NOT_ACK = 0x00000000,
ALPHA_DP_AUX_DEFINITE_ERR_REACHED_ACK    = 0x00000001,
} DP_AUX_DEFINITE_ERR_REACHED_ACK;

/*
 * DP_AUX_RESET enum
 */

typedef enum DP_AUX_RESET {
DP_AUX_RESET_DEASSERTED                  = 0x00000000,
DP_AUX_RESET_ASSERTED                    = 0x00000001,
} DP_AUX_RESET;

/*
 * DP_AUX_RESET_DONE enum
 */

typedef enum DP_AUX_RESET_DONE {
DP_AUX_RESET_SEQUENCE_NOT_DONE           = 0x00000000,
DP_AUX_RESET_SEQUENCE_DONE               = 0x00000001,
} DP_AUX_RESET_DONE;

/*******************************************************
 * DSI Enums
 *******************************************************/

/*
 * DSI_COMMAND_MODE_SRC_FORMAT enum
 */

typedef enum DSI_COMMAND_MODE_SRC_FORMAT {
DSI_COMMAND_SRC_FORMAT_RGB8BIT           = 0x00000002,
DSI_COMMAND_SRC_FORMAT_RGB332            = 0x00000003,
DSI_COMMAND_SRC_FORMAT_RGB444            = 0x00000004,
DSI_COMMAND_SRC_FORMAT_RGB555            = 0x00000005,
DSI_COMMAND_SRC_FORMAT_RGB565            = 0x00000006,
DSI_COMMAND_SRC_FORMAT_RGB888            = 0x00000008,
} DSI_COMMAND_MODE_SRC_FORMAT;

/*
 * DSI_COMMAND_MODE_DST_FORMAT enum
 */

typedef enum DSI_COMMAND_MODE_DST_FORMAT {
DSI_COMMAND_DST_FORMAT_RGB111            = 0x00000000,
DSI_COMMAND_DST_FORMAT_RGB332            = 0x00000003,
DSI_COMMAND_DST_FORMAT_RGB444            = 0x00000004,
DSI_COMMAND_DST_FORMAT_RGB565            = 0x00000006,
DSI_COMMAND_DST_FORMAT_RGB666            = 0x00000007,
DSI_COMMAND_DST_FORMAT_RGB888            = 0x00000008,
} DSI_COMMAND_MODE_DST_FORMAT;

/*
 * DSI_FLAG_CLR enum
 */

typedef enum DSI_FLAG_CLR {
DSI_FLAG_NO_CLEAR                        = 0x00000000,
DSI_FLAG_CLEAR                           = 0x00000001,
} DSI_FLAG_CLR;

/*
 * DSI_BIT_SWAP enum
 */

typedef enum DSI_BIT_SWAP {
DSI_BIT_SWAP_DISABLE                     = 0x00000000,
DSI_BIT_SWAP_ENABLE                      = 0x00000001,
} DSI_BIT_SWAP;

/*
 * DSI_CLK_GATING enum
 */

typedef enum DSI_CLK_GATING {
DSI_CLK_GATING_ENABLE                    = 0x00000000,
DSI_CLK_GATING_DISABLE                   = 0x00000001,
} DSI_CLK_GATING;

/*
 * DSI_LANE_ULPS_REQUEST enum
 */

typedef enum DSI_LANE_ULPS_REQUEST {
DSI_LANE_ULPS_REQUEST_DEASSERT           = 0x00000000,
DSI_LANE_ULPS_REQUEST_ASSERT             = 0x00000001,
} DSI_LANE_ULPS_REQUEST;

/*
 * DSI_LANE_ULPS_EXIT enum
 */

typedef enum DSI_LANE_ULPS_EXIT {
DSI_LANE_ULPS_EXIT_DEASSERT              = 0x00000000,
DSI_LANE_ULPS_EXIT_ASSERT                = 0x00000001,
} DSI_LANE_ULPS_EXIT;

/*
 * DSI_LANE_FORCE_TX_STOP enum
 */

typedef enum DSI_LANE_FORCE_TX_STOP {
DSI_LANE_FORCE_TX_STOP_DEASSERT          = 0x00000000,
DSI_LANE_FORCE_TX_STOP_ASSERT            = 0x00000001,
} DSI_LANE_FORCE_TX_STOP;

/*
 * DSI_CLOCK_LANE_HS_FORCE_REQUEST enum
 */

typedef enum DSI_CLOCK_LANE_HS_FORCE_REQUEST {
DSI_CLOCK_LANE_HS_FORCE_REQUEST_DEASSERT  = 0x00000000,
DSI_CLOCK_LANE_HS_FORCE_REQUEST_ASSERT   = 0x00000001,
} DSI_CLOCK_LANE_HS_FORCE_REQUEST;

/*
 * DSI_CONTROLLER_EN enum
 */

typedef enum DSI_CONTROLLER_EN {
DSI_CONTROLLER_DISABLE                   = 0x00000000,
DSI_CONTROLLER_ENABLE                    = 0x00000001,
} DSI_CONTROLLER_EN;

/*
 * DSI_VIDEO_MODE_EN enum
 */

typedef enum DSI_VIDEO_MODE_EN {
DSI_VIDEO_MODE_DISABLE                   = 0x00000000,
DSI_VIDEO_MODE_ENABLE                    = 0x00000001,
} DSI_VIDEO_MODE_EN;

/*
 * DSI_CMD_MODE_EN enum
 */

typedef enum DSI_CMD_MODE_EN {
DSI_CMD_MODE_DISABLE                     = 0x00000000,
DSI_CMD_MODE_ENABLE                      = 0x00000001,
} DSI_CMD_MODE_EN;

/*
 * DSI_DATA_LANE0_EN enum
 */

typedef enum DSI_DATA_LANE0_EN {
DSI_DATA_LANE0_DISABLE                   = 0x00000000,
DSI_DATA_LANE0_ENABLE                    = 0x00000001,
} DSI_DATA_LANE0_EN;

/*
 * DSI_DATA_LANE1_EN enum
 */

typedef enum DSI_DATA_LANE1_EN {
DSI_DATA_LANE1_DISABLE                   = 0x00000000,
DSI_DATA_LANE1_ENABLE                    = 0x00000001,
} DSI_DATA_LANE1_EN;

/*
 * DSI_DATA_LANE2_EN enum
 */

typedef enum DSI_DATA_LANE2_EN {
DSI_DATA_LANE2_DISABLE                   = 0x00000000,
DSI_DATA_LANE2_ENABLE                    = 0x00000001,
} DSI_DATA_LANE2_EN;

/*
 * DSI_DATA_LANE3_EN enum
 */

typedef enum DSI_DATA_LANE3_EN {
DSI_DATA_LANE3_DISABLE                   = 0x00000000,
DSI_DATA_LANE3_ENABLE                    = 0x00000001,
} DSI_DATA_LANE3_EN;

/*
 * DSI_CLOCK_LANE_EN enum
 */

typedef enum DSI_CLOCK_LANE_EN {
DSI_CLOCK_LANE_DISABLE                   = 0x00000000,
DSI_CLOCK_LANE_ENABLE                    = 0x00000001,
} DSI_CLOCK_LANE_EN;

/*
 * DSI_PHY_DATA_LANE0_EN enum
 */

typedef enum DSI_PHY_DATA_LANE0_EN {
DSI_PHY_DATA_LANE0_DISABLE               = 0x00000000,
DSI_PHY_DATA_LANE0_ENABLE                = 0x00000001,
} DSI_PHY_DATA_LANE0_EN;

/*
 * DSI_PHY_DATA_LANE1_EN enum
 */

typedef enum DSI_PHY_DATA_LANE1_EN {
DSI_PHY_DATA_LANE1_DISABLE               = 0x00000000,
DSI_PHY_DATA_LANE1_ENABLE                = 0x00000001,
} DSI_PHY_DATA_LANE1_EN;

/*
 * DSI_PHY_DATA_LANE2_EN enum
 */

typedef enum DSI_PHY_DATA_LANE2_EN {
DSI_PHY_DATA_LANE2_DISABLE               = 0x00000000,
DSI_PHY_DATA_LANE2_ENABLE                = 0x00000001,
} DSI_PHY_DATA_LANE2_EN;

/*
 * DSI_PHY_DATA_LANE3_EN enum
 */

typedef enum DSI_PHY_DATA_LANE3_EN {
DSI_PHY_DATA_LANE3_DISABLE               = 0x00000000,
DSI_PHY_DATA_LANE3_ENABLE                = 0x00000001,
} DSI_PHY_DATA_LANE3_EN;

/*
 * DSI_RESET_DISPCLK enum
 */

typedef enum DSI_RESET_DISPCLK {
DSI_NO_RESET_ON_DISPCLK_DOMAIN_LOGIC     = 0x00000000,
DSI_RESET_ON_DISPCLK_DOMAIN_LOGIC        = 0x00000001,
} DSI_RESET_DISPCLK;

/*
 * DSI_RESET_DSICLK enum
 */

typedef enum DSI_RESET_DSICLK {
DSI_NO_RESET_ON_DSICLK_DOMAIN_LOGIC      = 0x00000000,
DSI_RESET_ON_DSICLK_DOMAIN_LOGIC         = 0x00000001,
} DSI_RESET_DSICLK;

/*
 * DSI_RESET_BYTECLK enum
 */

typedef enum DSI_RESET_BYTECLK {
DSI_NO_RESET_ON_BYTECLK_DOMAIN_LOGIC     = 0x00000000,
DSI_RESET_ON_BYTECLK_DOMAIN_LOGIC        = 0x00000001,
} DSI_RESET_BYTECLK;

/*
 * DSI_RESET_ESCCLK enum
 */

typedef enum DSI_RESET_ESCCLK {
DSI_NO_RESET_ON_ESCCLK_DOMAIN_LOGIC      = 0x00000000,
DSI_RESET_ON_ESCCLK_DOMAIN_LOGIC         = 0x00000001,
} DSI_RESET_ESCCLK;

/*
 * DSI_CRTC_SEL enum
 */

typedef enum DSI_CRTC_SEL {
DSI_GET_PIXEL_STREAM_FROM_FMT0           = 0x00000000,
DSI_GET_PIXEL_STREAM_FROM_FMT1           = 0x00000001,
DSI_GET_PIXEL_STREAM_FROM_FMT2           = 0x00000002,
DSI_GET_PIXEL_STREAM_FROM_FMT3           = 0x00000003,
DSI_GET_PIXEL_STREAM_FROM_FMT4           = 0x00000004,
DSI_GET_PIXEL_STREAM_FROM_FMT5           = 0x00000005,
} DSI_CRTC_SEL;

/*
 * DSI_PACKET_BYTE_MSB_LSB_FLIP enum
 */

typedef enum DSI_PACKET_BYTE_MSB_LSB_FLIP {
DSI_PACKET_BYTE_MSB_LSB_FLIP_NO_SWAP     = 0x00000000,
DSI_PACKET_BYTE_MSB_LSB_FLIP_SWAP        = 0x00000001,
} DSI_PACKET_BYTE_MSB_LSB_FLIP;

/*
 * DSI_VIDEO_MODE_DST_FORMAT enum
 */

typedef enum DSI_VIDEO_MODE_DST_FORMAT {
DSI_VIDEO_DST_FORMAT_RGB565              = 0x00000000,
DSI_VIDEO_DST_FORMAT_RGB666_PACKED       = 0x00000001,
DSI_VIDEO_DST_FORMAT_RGB666_LOOSELY_PACKED = 0x00000002,
DSI_VIDEO_DST_FORMAT_RGB888              = 0x00000003,
} DSI_VIDEO_MODE_DST_FORMAT;

/*
 * DSI_VIDEO_TRAFFIC_MODE enum
 */

typedef enum DSI_VIDEO_TRAFFIC_MODE {
DSI_TRAFFIC_MODE_SYNC_PULSES             = 0x00000000,
DSI_TRAFFIC_MODE_SYNC_EVENTS             = 0x00000001,
DSI_TRAFFIC_MODE_BURST                   = 0x00000002,
DSI_TRAFFIC_MODE_RESERVED                = 0x00000003,
} DSI_VIDEO_TRAFFIC_MODE;

/*
 * DSI_VIDEO_BLLP_PWR_MODE enum
 */

typedef enum DSI_VIDEO_BLLP_PWR_MODE {
DSI_VIDEO_BLLP_PWR_MODE_HS               = 0x00000000,
DSI_VIDEO_BLLP_PWR_MODE_LP               = 0x00000001,
} DSI_VIDEO_BLLP_PWR_MODE;

/*
 * DSI_VIDEO_EOF_BLLP_PWR_MODE enum
 */

typedef enum DSI_VIDEO_EOF_BLLP_PWR_MODE {
DSI_VIDEO_EOF_BLLP_PWR_MODE_HS           = 0x00000000,
DSI_VIDEO_EOF_BLLP_PWR_MODE_LP           = 0x00000001,
} DSI_VIDEO_EOF_BLLP_PWR_MODE;

/*
 * DSI_VIDEO_PWR_MODE enum
 */

typedef enum DSI_VIDEO_PWR_MODE {
DSI_VIDEO_PWR_MODE_HS                    = 0x00000000,
DSI_VIDEO_PWR_MODE_LP                    = 0x00000001,
} DSI_VIDEO_PWR_MODE;

/*
 * DSI_VIDEO_PULSE_MODE_OPT enum
 */

typedef enum DSI_VIDEO_PULSE_MODE_OPT {
PULSE_MODE_OPT_NO_HSA                    = 0x00000000,
PULSE_MODE_OPT_SEND                      = 0x00000001,
} DSI_VIDEO_PULSE_MODE_OPT;

/*
 * DSI_RGB_SWAP enum
 */

typedef enum DSI_RGB_SWAP {
DSI_SWAP_RGB                             = 0x00000000,
DSI_SWAP_RBG                             = 0x00000001,
DSI_SWAP_BGR                             = 0x00000002,
DSI_SWAP_BRG                             = 0x00000003,
DSI_SWAP_GRB                             = 0x00000004,
DSI_SWAP_GBR                             = 0x00000005,
} DSI_RGB_SWAP;

/*
 * DSI_CMD_PACKET_TYPE enum
 */

typedef enum DSI_CMD_PACKET_TYPE {
DSI_CMD_PACKET_TYPE_SHORT                = 0x00000000,
DSI_CMD_PACKET_TYPE_LONG                 = 0x00000001,
} DSI_CMD_PACKET_TYPE;

/*
 * DSI_CMD_PWR_MODE enum
 */

typedef enum DSI_CMD_PWR_MODE {
DSI_CMD_PWR_MODE_HS                      = 0x00000000,
DSI_CMD_PWR_MODE_LP                      = 0x00000001,
} DSI_CMD_PWR_MODE;

/*
 * DSI_CMD_EMBEDDED_MODE enum
 */

typedef enum DSI_CMD_EMBEDDED_MODE {
CMD_EMBEDDED_MODE_DISABLE                = 0x00000000,
CMD_EMBEDDED_MODE_ENABLE                 = 0x00000001,
} DSI_CMD_EMBEDDED_MODE;

/*
 * DSI_CMD_ORDER enum
 */

typedef enum DSI_CMD_ORDER {
DSI_CMD_ORDER_COMMAND_FIRST              = 0x00000000,
DSI_CMD_ORDER_DATA_FIRST                 = 0x00000001,
} DSI_CMD_ORDER;

/*
 * DSI_DATA_BUFFER_ID enum
 */

typedef enum DSI_DATA_BUFFER_ID {
DSI_DATA_BUFFER_OFFSET0                  = 0x00000000,
DSI_DATA_BUFFER_OFFSET1                  = 0x00000001,
} DSI_DATA_BUFFER_ID;

/*
 * DSI_DWORD_BYTE_SWAP enum
 */

typedef enum DSI_DWORD_BYTE_SWAP {
DWORD_BYTE_SWAP_NO_SWAP                  = 0x00000000,
DWORD_BYTE_SWAP_BYTE_SWAP                = 0x00000001,
DWORD_BYTE_SWAP_WORD_SWAP                = 0x00000002,
DWORD_BYTE_SWAP_BOTH_SWAP                = 0x00000003,
} DSI_DWORD_BYTE_SWAP;

/*
 * DSI_INSERT_DCS_COMMAND enum
 */

typedef enum DSI_INSERT_DCS_COMMAND {
DSI_INSERT_DCS_COMMAND_DISABLE           = 0x00000000,
DSI_INSERT_DCS_COMMAND_ENABLE            = 0x00000001,
} DSI_INSERT_DCS_COMMAND;

/*
 * DSI_DMAFIFO_WRITE_WATERMARK enum
 */

typedef enum DSI_DMAFIFO_WRITE_WATERMARK {
DSI_DMAFIFO_WRITE_WATERMARK_HALF         = 0x00000000,
DSI_DMAFIFO_WRITE_WATERMARK_FOURTH       = 0x00000001,
DSI_DMAFIFO_WRITE_WATERMARK_EIGHTH       = 0x00000002,
DSI_DMAFIFO_WRITE_WATERMARK_SIXTEENTH    = 0x00000003,
} DSI_DMAFIFO_WRITE_WATERMARK;

/*
 * DSI_DMAFIFO_READ_WATERMARK enum
 */

typedef enum DSI_DMAFIFO_READ_WATERMARK {
DSI_DMAFIFO_READ_WATERMARK_HALF          = 0x00000000,
DSI_DMAFIFO_READ_WATERMARK_FOURTH        = 0x00000001,
DSI_DMAFIFO_READ_WATERMARK_EIGHTH        = 0x00000002,
DSI_DMAFIFO_READ_WATERMARK_SIXTEENTH     = 0x00000003,
} DSI_DMAFIFO_READ_WATERMARK;

/*
 * DSI_USE_DENG_LENGTH enum
 */

typedef enum DSI_USE_DENG_LENGTH {
DSI_USE_DENG_LENGTH_DISABLE              = 0x00000000,
DSI_USE_DENG_LENGTH_ENABLE               = 0x00000001,
} DSI_USE_DENG_LENGTH;

/*
 * DSI_COMMAND_TRIGGER_MODE enum
 */

typedef enum DSI_COMMAND_TRIGGER_MODE {
DSI_COMMAND_TRIGGER_MODE_AUTO            = 0x00000000,
DSI_COMMAND_TRIGGER_MODE_MANUAL          = 0x00000001,
} DSI_COMMAND_TRIGGER_MODE;

/*
 * DSI_COMMAND_TRIGGER_SEL enum
 */

typedef enum DSI_COMMAND_TRIGGER_SEL {
DSI_COMMAND_TRIGGER_SEL_NONE             = 0x00000000,
DSI_COMMAND_TRIGGER_SEL_CRTC             = 0x00000001,
DSI_COMMAND_TRIGGER_SEL_TE               = 0x00000002,
DSI_COMMAND_TRIGGER_SEL_HW               = 0x00000003,
} DSI_COMMAND_TRIGGER_SEL;

/*
 * DSI_HW_SOURCE_SEL enum
 */

typedef enum DSI_HW_SOURCE_SEL {
HW_SOURCE_SEL_NONE                       = 0x00000000,
HW_SOURCE_SEL_DSC_VUP                    = 0x00000001,
HW_SOURCE_SEL_DSC_VLP                    = 0x00000002,
HW_SOURCE_SEL_DSC_JPEG                   = 0x00000003,
} DSI_HW_SOURCE_SEL;

/*
 * DSI_COMMAND_TRIGGER_ORDER enum
 */

typedef enum DSI_COMMAND_TRIGGER_ORDER {
DSI_COMMAND_TRIGGER_ORDER_DMA            = 0x00000000,
DSI_COMMAND_TRIGGER_ORDER_DENG           = 0x00000001,
} DSI_COMMAND_TRIGGER_ORDER;

/*
 * DSI_TE_SRC_SEL enum
 */

typedef enum DSI_TE_SRC_SEL {
DSI_TE_SEL_LINK                          = 0x00000000,
DSI_TE_SEL_PIN                           = 0x00000001,
} DSI_TE_SRC_SEL;

/*
 * DSI_EXT_TE_MUX enum
 */

typedef enum DSI_EXT_TE_MUX {
DSI_XT_TE_MUX_LCDD17                     = 0x00000000,
DSI_XT_TE_MUX_DCLK                       = 0x00000001,
DSI_XT_TE_MUX_SS                         = 0x00000002,
DSI_XT_TE_MUX_GCLK                       = 0x00000003,
DSI_XT_TE_MUX_GOE                        = 0x00000004,
DSI_XT_TE_MUX_DINV                       = 0x00000005,
DSI_XT_TE_MUX_FRAME                      = 0x00000006,
DSI_XT_TE_MUX_GPIO4                      = 0x00000007,
DSI_XT_TE_MUX_GPIO5                      = 0x00000008,
} DSI_EXT_TE_MUX;

/*
 * DSI_EXT_TE_MODE enum
 */

typedef enum DSI_EXT_TE_MODE {
DSI_EXT_TE_MODE_VSYNC_EDGE               = 0x00000000,
DSI_EXT_TE_MODE_VSYNC_WIDTH              = 0x00000001,
DSI_EXT_TE_MODE_HVSYNC_EDGE              = 0x00000002,
DSI_EXT_TE_MODE_HVSYNC_WIDTH             = 0x00000003,
} DSI_EXT_TE_MODE;

/*
 * DSI_EXT_RESET_POL enum
 */

typedef enum DSI_EXT_RESET_POL {
DSI_EXT_RESET_POL_HIGH                   = 0x00000000,
DSI_EXT_RESET_POL_LOW                    = 0x00000001,
} DSI_EXT_RESET_POL;

/*
 * DSI_EXT_TE_POL enum
 */

typedef enum DSI_EXT_TE_POL {
DSI_EXT_TE_POL_RISING                    = 0x00000000,
DSI_EXT_TE_POL_FALLING                   = 0x00000001,
} DSI_EXT_TE_POL;

/*
 * DSI_RESET_PANEL enum
 */

typedef enum DSI_RESET_PANEL {
DSI_RESET_PANEL_DEASSERT                 = 0x00000000,
DSI_RESET_PANEL_ASSERT                   = 0x00000001,
} DSI_RESET_PANEL;

/*
 * DSI_CRC_ENABLE enum
 */

typedef enum DSI_CRC_ENABLE {
DSI_CRC_CAL_DISABLE                      = 0x00000000,
DSI_CRC_CAL_ENABLE                       = 0x00000001,
} DSI_CRC_ENABLE;

/*
 * DSI_TX_EOT_APPEND enum
 */

typedef enum DSI_TX_EOT_APPEND {
DSI_TX_EOT_APPEND_DISABLE                = 0x00000000,
DSI_TX_EOT_APPEND_ENABLE                 = 0x00000001,
} DSI_TX_EOT_APPEND;

/*
 * DSI_RX_EOT_IGNORE enum
 */

typedef enum DSI_RX_EOT_IGNORE {
DSI_RX_EOT_IGNORE_DISABLE                = 0x00000000,
DSI_RX_EOT_IGNORE_ENABLE                 = 0x00000001,
} DSI_RX_EOT_IGNORE;

/*
 * DSI_MIPI_BIST_RESET enum
 */

typedef enum DSI_MIPI_BIST_RESET {
DSI_MIPI_BIST_RESET_DEASSERT             = 0x00000000,
DSI_MIPI_BIST_RESET_ASSERT               = 0x00000001,
} DSI_MIPI_BIST_RESET;

/*
 * DSI_MIPI_BIST_VIDEO_FRMT enum
 */

typedef enum DSI_MIPI_BIST_VIDEO_FRMT {
DSI_MIPI_BIST_VIDEO_FRMT_YUV422          = 0x00000000,
DSI_MIPI_BIST_VIDEO_FRMT_RAW8            = 0x00000001,
} DSI_MIPI_BIST_VIDEO_FRMT;

/*
 * DSI_MIPI_BIST_START enum
 */

typedef enum DSI_MIPI_BIST_START {
DSI_MIPI_BIST_START_DEASSERT             = 0x00000000,
DSI_MIPI_BIST_START_ASSERT               = 0x00000001,
} DSI_MIPI_BIST_START;

/*
 * DSI_DBG_CLK_SEL enum
 */

typedef enum DSI_DBG_CLK_SEL {
DSI_TEST_CLK_SEL_DISPCLK_P               = 0x00000000,
DSI_TEST_CLK_SEL_DISPCLK_G               = 0x00000001,
DSI_TEST_CLK_SEL_DISPCLK_R               = 0x00000002,
DSI_TEST_CLK_SEL_ESCCLK_G                = 0x00000003,
DSI_TEST_CLK_SEL_BYTECLK_G               = 0x00000004,
DSI_TEST_CLK_SEL_DSICLK_P                = 0x00000005,
DSI_TEST_CLK_SEL_DSICLK_R                = 0x00000006,
DSI_TEST_CLK_SEL_DSICLK_G                = 0x00000007,
DSI_TEST_CLK_SEL_DSICLK_TRN              = 0x00000008,
} DSI_DBG_CLK_SEL;

/*
 * DSI_DENG_FIFO_USE_OVERWRITE_LEVEL enum
 */

typedef enum DSI_DENG_FIFO_USE_OVERWRITE_LEVEL {
DSI_DENG_FIFO_LEVEL_OVERWRITE            = 0x00000000,
DSI_DENG_FIFO_LEVEL_CAL_AVERAGE          = 0x00000001,
} DSI_DENG_FIFO_USE_OVERWRITE_LEVEL;

/*
 * DSI_DENG_FIFO_FORCE_RECAL_AVERAGE enum
 */

typedef enum DSI_DENG_FIFO_FORCE_RECAL_AVERAGE {
DSI_DENG_FIFO_FORCE_RECAL_AVERAGE_DEASSERT  = 0x00000000,
DSI_DENG_FIFO_FORCE_RECAL_AVERAGE_ASSERT  = 0x00000001,
} DSI_DENG_FIFO_FORCE_RECAL_AVERAGE;

/*
 * DSI_DENG_FIFO_FORCE_RECOMP_MINMAX enum
 */

typedef enum DSI_DENG_FIFO_FORCE_RECOMP_MINMAX {
DSI_DENG_FIFO_FORCE_RECOMP_MINMAX_DEASSERT  = 0x00000000,
DSI_DENG_FIFO_FORCE_RECOMP_MINMAX_ASSERT  = 0x00000001,
} DSI_DENG_FIFO_FORCE_RECOMP_MINMAX;

/*
 * DSI_DENG_FIFO_START enum
 */

typedef enum DSI_DENG_FIFO_START {
DSI_DENG_FIFO_START_DEASSERT             = 0x00000000,
DSI_DENG_FIFO_START_ASSERT               = 0x00000001,
} DSI_DENG_FIFO_START;

/*
 * DSI_USE_CMDFIFO enum
 */

typedef enum DSI_USE_CMDFIFO {
DSI_CMD_USE_DMAFIFO                      = 0x00000000,
DSI_CMD_USE_CMDFIFO                      = 0x00000001,
} DSI_USE_CMDFIFO;

/*
 * DSI_CRTC_FREEZE_TRIG enum
 */

typedef enum DSI_CRTC_FREEZE_TRIG {
DSI_CRTC_FREEZE_TRIG_DEASSERT            = 0x00000000,
DSI_CRTC_FREEZE_TRIG_ASSERT              = 0x00000001,
} DSI_CRTC_FREEZE_TRIG;

/*
 * DSI_PERF_LATENCY_SEL enum
 */

typedef enum DSI_PERF_LATENCY_SEL {
DSI_PERF_LATENCY_SEL_DATA_LANE0          = 0x00000000,
DSI_PERF_LATENCY_SEL_DATA_LANE1          = 0x00000001,
DSI_PERF_LATENCY_SEL_DATA_LANE2          = 0x00000002,
DSI_PERF_LATENCY_SEL_DATA_LANE3          = 0x00000003,
} DSI_PERF_LATENCY_SEL;

/*
 * DSI_DEBUG_DSICLK_SEL enum
 */

typedef enum DSI_DEBUG_DSICLK_SEL {
DSI_DEBUG_DSICLK_SEL_VIDEO_ENGINE        = 0x00000000,
DSI_DEBUG_DSICLK_SEL_CMD_ENGINE          = 0x00000001,
DSI_DEBUG_DSICLK_SEL_RESYNC_FIFO         = 0x00000002,
DSI_DEBUG_DSICLK_SEL_CMDFIFO             = 0x00000003,
DSI_DEBUG_DSICLK_SEL_CMDBUFFER           = 0x00000004,
DSI_DEBUG_DSICLK_SEL_AFIFO               = 0x00000005,
DSI_DEBUG_DSICLK_SEL_LANECTRL            = 0x00000006,
} DSI_DEBUG_DSICLK_SEL;

/*
 * DSI_DEBUG_BYTECLK_SEL enum
 */

typedef enum DSI_DEBUG_BYTECLK_SEL {
DSI_DEBUG_BYTECLK_SEL_AFIFO              = 0x00000000,
DSI_DEBUG_BYTECLK_SEL_LANEFIFO0          = 0x00000001,
DSI_DEBUG_BYTECLK_SEL_LANEFIFO1          = 0x00000002,
DSI_DEBUG_BYTECLK_SEL_LANEFIFO2          = 0x00000003,
DSI_DEBUG_BYTECLK_SEL_LANEFIFO3          = 0x00000004,
DSI_DEBUG_BYTECLK_SEL_LANEBUF0           = 0x00000005,
DSI_DEBUG_BYTECLK_SEL_LANEBUF1           = 0x00000006,
DSI_DEBUG_BYTECLK_SEL_LANEBUF2           = 0x00000007,
DSI_DEBUG_BYTECLK_SEL_LANEBUF3           = 0x00000008,
DSI_DEBUG_BYTECLK_SEL_PINGPONG0          = 0x00000009,
DSI_DEBUG_BYTECLK_SEL_PINGPONG1          = 0x0000000a,
DSI_DEBUG_BYTECLK_SEL_PINGPING2          = 0x0000000b,
DSI_DEBUG_BYTECLK_SEL_PINGPING3          = 0x0000000c,
DSI_DEBUG_BYTECLK_SEL_EOT                = 0x0000000d,
DSI_DEBUG_BYTECLK_SEL_LANECTRL           = 0x0000000e,
} DSI_DEBUG_BYTECLK_SEL;

/*******************************************************
 * DCIO_CHIP Enums
 *******************************************************/

/*
 * DCIOCHIP_HPD_SEL enum
 */

typedef enum DCIOCHIP_HPD_SEL {
DCIOCHIP_HPD_SEL_ASYNC                   = 0x00000000,
DCIOCHIP_HPD_SEL_CLOCKED                 = 0x00000001,
} DCIOCHIP_HPD_SEL;

/*
 * DCIOCHIP_PAD_MODE enum
 */

typedef enum DCIOCHIP_PAD_MODE {
DCIOCHIP_PAD_MODE_DDC                    = 0x00000000,
DCIOCHIP_PAD_MODE_DP                     = 0x00000001,
} DCIOCHIP_PAD_MODE;

/*
 * DCIOCHIP_AUXSLAVE_PAD_MODE enum
 */

typedef enum DCIOCHIP_AUXSLAVE_PAD_MODE {
DCIOCHIP_AUXSLAVE_PAD_MODE_I2C           = 0x00000000,
DCIOCHIP_AUXSLAVE_PAD_MODE_AUX           = 0x00000001,
} DCIOCHIP_AUXSLAVE_PAD_MODE;

/*
 * DCIOCHIP_INVERT enum
 */

typedef enum DCIOCHIP_INVERT {
DCIOCHIP_POL_NON_INVERT                  = 0x00000000,
DCIOCHIP_POL_INVERT                      = 0x00000001,
} DCIOCHIP_INVERT;

/*
 * DCIOCHIP_PD_EN enum
 */

typedef enum DCIOCHIP_PD_EN {
DCIOCHIP_PD_EN_NOTALLOW                  = 0x00000000,
DCIOCHIP_PD_EN_ALLOW                     = 0x00000001,
} DCIOCHIP_PD_EN;

/*
 * DCIOCHIP_GPIO_MASK_EN enum
 */

typedef enum DCIOCHIP_GPIO_MASK_EN {
DCIOCHIP_GPIO_MASK_EN_HARDWARE           = 0x00000000,
DCIOCHIP_GPIO_MASK_EN_SOFTWARE           = 0x00000001,
} DCIOCHIP_GPIO_MASK_EN;

/*
 * DCIOCHIP_MASK enum
 */

typedef enum DCIOCHIP_MASK {
DCIOCHIP_MASK_DISABLE                    = 0x00000000,
DCIOCHIP_MASK_ENABLE                     = 0x00000001,
} DCIOCHIP_MASK;

/*
 * DCIOCHIP_GPIO_I2C_MASK enum
 */

typedef enum DCIOCHIP_GPIO_I2C_MASK {
DCIOCHIP_GPIO_I2C_MASK_DISABLE           = 0x00000000,
DCIOCHIP_GPIO_I2C_MASK_ENABLE            = 0x00000001,
} DCIOCHIP_GPIO_I2C_MASK;

/*
 * DCIOCHIP_GPIO_I2C_DRIVE enum
 */

typedef enum DCIOCHIP_GPIO_I2C_DRIVE {
DCIOCHIP_GPIO_I2C_DRIVE_LOW              = 0x00000000,
DCIOCHIP_GPIO_I2C_DRIVE_HIGH             = 0x00000001,
} DCIOCHIP_GPIO_I2C_DRIVE;

/*
 * DCIOCHIP_GPIO_I2C_EN enum
 */

typedef enum DCIOCHIP_GPIO_I2C_EN {
DCIOCHIP_GPIO_I2C_DISABLE                = 0x00000000,
DCIOCHIP_GPIO_I2C_ENABLE                 = 0x00000001,
} DCIOCHIP_GPIO_I2C_EN;

/*
 * DCIOCHIP_MASK_4BIT enum
 */

typedef enum DCIOCHIP_MASK_4BIT {
DCIOCHIP_MASK_4BIT_DISABLE               = 0x00000000,
DCIOCHIP_MASK_4BIT_ENABLE                = 0x0000000f,
} DCIOCHIP_MASK_4BIT;

/*
 * DCIOCHIP_ENABLE_4BIT enum
 */

typedef enum DCIOCHIP_ENABLE_4BIT {
DCIOCHIP_4BIT_DISABLE                    = 0x00000000,
DCIOCHIP_4BIT_ENABLE                     = 0x0000000f,
} DCIOCHIP_ENABLE_4BIT;

/*
 * DCIOCHIP_MASK_5BIT enum
 */

typedef enum DCIOCHIP_MASK_5BIT {
DCIOCHIP_MASIK_5BIT_DISABLE              = 0x00000000,
DCIOCHIP_MASIK_5BIT_ENABLE               = 0x0000001f,
} DCIOCHIP_MASK_5BIT;

/*
 * DCIOCHIP_ENABLE_5BIT enum
 */

typedef enum DCIOCHIP_ENABLE_5BIT {
DCIOCHIP_5BIT_DISABLE                    = 0x00000000,
DCIOCHIP_5BIT_ENABLE                     = 0x0000001f,
} DCIOCHIP_ENABLE_5BIT;

/*
 * DCIOCHIP_MASK_2BIT enum
 */

typedef enum DCIOCHIP_MASK_2BIT {
DCIOCHIP_MASK_2BIT_DISABLE               = 0x00000000,
DCIOCHIP_MASK_2BIT_ENABLE                = 0x00000003,
} DCIOCHIP_MASK_2BIT;

/*
 * DCIOCHIP_ENABLE_2BIT enum
 */

typedef enum DCIOCHIP_ENABLE_2BIT {
DCIOCHIP_2BIT_DISABLE                    = 0x00000000,
DCIOCHIP_2BIT_ENABLE                     = 0x00000003,
} DCIOCHIP_ENABLE_2BIT;

/*
 * DCIOCHIP_REF_27_SRC_SEL enum
 */

typedef enum DCIOCHIP_REF_27_SRC_SEL {
DCIOCHIP_REF_27_SRC_SEL_XTAL_DIVIDER     = 0x00000000,
DCIOCHIP_REF_27_SRC_SEL_DISP_CLKIN2_DIVIDER  = 0x00000001,
DCIOCHIP_REF_27_SRC_SEL_XTAL_BYPASS      = 0x00000002,
DCIOCHIP_REF_27_SRC_SEL_DISP_CLKIN2_BYPASS  = 0x00000003,
} DCIOCHIP_REF_27_SRC_SEL;

/*
 * DCIOCHIP_DVO_VREFPON enum
 */

typedef enum DCIOCHIP_DVO_VREFPON {
DCIOCHIP_DVO_VREFPON_DISABLE             = 0x00000000,
DCIOCHIP_DVO_VREFPON_ENABLE              = 0x00000001,
} DCIOCHIP_DVO_VREFPON;

/*
 * DCIOCHIP_DVO_VREFSEL enum
 */

typedef enum DCIOCHIP_DVO_VREFSEL {
DCIOCHIP_DVO_VREFSEL_ONCHIP              = 0x00000000,
DCIOCHIP_DVO_VREFSEL_EXTERNAL            = 0x00000001,
} DCIOCHIP_DVO_VREFSEL;

/*
 * DCIOCHIP_SPDIF1_IMODE enum
 */

typedef enum DCIOCHIP_SPDIF1_IMODE {
DCIOCHIP_SPDIF1_IMODE_OE_A               = 0x00000000,
DCIOCHIP_SPDIF1_IMODE_TSTE_TSTO          = 0x00000001,
} DCIOCHIP_SPDIF1_IMODE;

/*
 * DCIOCHIP_AUX_FALLSLEWSEL enum
 */

typedef enum DCIOCHIP_AUX_FALLSLEWSEL {
DCIOCHIP_AUX_FALLSLEWSEL_LOW             = 0x00000000,
DCIOCHIP_AUX_FALLSLEWSEL_HIGH0           = 0x00000001,
DCIOCHIP_AUX_FALLSLEWSEL_HIGH1           = 0x00000002,
DCIOCHIP_AUX_FALLSLEWSEL_ULTRAHIGH       = 0x00000003,
} DCIOCHIP_AUX_FALLSLEWSEL;

/*
 * DCIOCHIP_AUX_SPIKESEL enum
 */

typedef enum DCIOCHIP_AUX_SPIKESEL {
DCIOCHIP_AUX_SPIKESEL_50NS               = 0x00000000,
DCIOCHIP_AUX_SPIKESEL_10NS               = 0x00000001,
} DCIOCHIP_AUX_SPIKESEL;

/*
 * DCIOCHIP_AUX_CSEL0P9 enum
 */

typedef enum DCIOCHIP_AUX_CSEL0P9 {
DCIOCHIP_AUX_CSEL_DEC1P0                 = 0x00000000,
DCIOCHIP_AUX_CSEL_DEC0P9                 = 0x00000001,
} DCIOCHIP_AUX_CSEL0P9;

/*
 * DCIOCHIP_AUX_CSEL1P1 enum
 */

typedef enum DCIOCHIP_AUX_CSEL1P1 {
DCIOCHIP_AUX_CSEL_INC1P0                 = 0x00000000,
DCIOCHIP_AUX_CSEL_INC1P1                 = 0x00000001,
} DCIOCHIP_AUX_CSEL1P1;

/*
 * DCIOCHIP_AUX_RSEL0P9 enum
 */

typedef enum DCIOCHIP_AUX_RSEL0P9 {
DCIOCHIP_AUX_RSEL_DEC1P0                 = 0x00000000,
DCIOCHIP_AUX_RSEL_DEC0P9                 = 0x00000001,
} DCIOCHIP_AUX_RSEL0P9;

/*
 * DCIOCHIP_AUX_RSEL1P1 enum
 */

typedef enum DCIOCHIP_AUX_RSEL1P1 {
DCIOCHIP_AUX_RSEL_INC1P0                 = 0x00000000,
DCIOCHIP_AUX_RSEL_INC1P1                 = 0x00000001,
} DCIOCHIP_AUX_RSEL1P1;

/*******************************************************
 * AZCONTROLLER Enums
 *******************************************************/

/*
 * GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_CONTROL enum
 */

typedef enum GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_CONTROL {
GENERIC_AZ_CONTROLLER_REGISTER_DISABLE   = 0x00000000,
GENERIC_AZ_CONTROLLER_REGISTER_ENABLE    = 0x00000001,
} GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_CONTROL;

/*
 * GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_CONTROL_RESERVED enum
 */

typedef enum GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_CONTROL_RESERVED {
GENERIC_AZ_CONTROLLER_REGISTER_DISABLE_RESERVED  = 0x00000000,
GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_RESERVED  = 0x00000001,
} GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_CONTROL_RESERVED;

/*
 * GENERIC_AZ_CONTROLLER_REGISTER_STATUS enum
 */

typedef enum GENERIC_AZ_CONTROLLER_REGISTER_STATUS {
GENERIC_AZ_CONTROLLER_REGISTER_STATUS_NOT_SET  = 0x00000000,
GENERIC_AZ_CONTROLLER_REGISTER_STATUS_SET  = 0x00000001,
} GENERIC_AZ_CONTROLLER_REGISTER_STATUS;

/*
 * GENERIC_AZ_CONTROLLER_REGISTER_STATUS_RESERVED enum
 */

typedef enum GENERIC_AZ_CONTROLLER_REGISTER_STATUS_RESERVED {
GENERIC_AZ_CONTROLLER_REGISTER_STATUS_NOT_SET_RESERVED  = 0x00000000,
GENERIC_AZ_CONTROLLER_REGISTER_STATUS_SET_RESERVED  = 0x00000001,
} GENERIC_AZ_CONTROLLER_REGISTER_STATUS_RESERVED;

/*
 * AZ_GLOBAL_CAPABILITIES enum
 */

typedef enum AZ_GLOBAL_CAPABILITIES {
AZ_GLOBAL_CAPABILITIES_SIXTY_FOUR_BIT_ADDRESS_NOT_SUPPORTED  = 0x00000000,
AZ_GLOBAL_CAPABILITIES_SIXTY_FOUR_BIT_ADDRESS_SUPPORTED  = 0x00000001,
} AZ_GLOBAL_CAPABILITIES;

/*
 * GLOBAL_CONTROL_ACCEPT_UNSOLICITED_RESPONSE enum
 */

typedef enum GLOBAL_CONTROL_ACCEPT_UNSOLICITED_RESPONSE {
ACCEPT_UNSOLICITED_RESPONSE_NOT_ENABLE   = 0x00000000,
ACCEPT_UNSOLICITED_RESPONSE_ENABLE       = 0x00000001,
} GLOBAL_CONTROL_ACCEPT_UNSOLICITED_RESPONSE;

/*
 * GLOBAL_CONTROL_FLUSH_CONTROL enum
 */

typedef enum GLOBAL_CONTROL_FLUSH_CONTROL {
FLUSH_CONTROL_FLUSH_NOT_STARTED          = 0x00000000,
FLUSH_CONTROL_FLUSH_STARTED              = 0x00000001,
} GLOBAL_CONTROL_FLUSH_CONTROL;

/*
 * GLOBAL_CONTROL_CONTROLLER_RESET enum
 */

typedef enum GLOBAL_CONTROL_CONTROLLER_RESET {
CONTROLLER_RESET_AZ_CONTROLLER_IN_RESET  = 0x00000000,
CONTROLLER_RESET_AZ_CONTROLLER_NOT_IN_RESET  = 0x00000001,
} GLOBAL_CONTROL_CONTROLLER_RESET;

/*
 * AZ_STATE_CHANGE_STATUS enum
 */

typedef enum AZ_STATE_CHANGE_STATUS {
AZ_STATE_CHANGE_STATUS_CODEC_NOT_PRESENT  = 0x00000000,
AZ_STATE_CHANGE_STATUS_CODEC_PRESENT     = 0x00000001,
} AZ_STATE_CHANGE_STATUS;

/*
 * GLOBAL_STATUS_FLUSH_STATUS enum
 */

typedef enum GLOBAL_STATUS_FLUSH_STATUS {
GLOBAL_STATUS_FLUSH_STATUS_FLUSH_NOT_ENDED  = 0x00000000,
GLOBAL_STATUS_FLUSH_STATUS_FLUSH_ENDED   = 0x00000001,
} GLOBAL_STATUS_FLUSH_STATUS;

/*
 * STREAM_0_SYNCHRONIZATION enum
 */

typedef enum STREAM_0_SYNCHRONIZATION {
STREAM_0_SYNCHRONIZATION_STEAM_NOT_STOPPED  = 0x00000000,
STREAM_0_SYNCHRONIZATION_STEAM_STOPPED   = 0x00000001,
} STREAM_0_SYNCHRONIZATION;

/*
 * STREAM_1_SYNCHRONIZATION enum
 */

typedef enum STREAM_1_SYNCHRONIZATION {
STREAM_1_SYNCHRONIZATION_STEAM_NOT_STOPPED  = 0x00000000,
STREAM_1_SYNCHRONIZATION_STEAM_STOPPED   = 0x00000001,
} STREAM_1_SYNCHRONIZATION;

/*
 * STREAM_2_SYNCHRONIZATION enum
 */

typedef enum STREAM_2_SYNCHRONIZATION {
STREAM_2_SYNCHRONIZATION_STEAM_NOT_STOPPED  = 0x00000000,
STREAM_2_SYNCHRONIZATION_STEAM_STOPPED   = 0x00000001,
} STREAM_2_SYNCHRONIZATION;

/*
 * STREAM_3_SYNCHRONIZATION enum
 */

typedef enum STREAM_3_SYNCHRONIZATION {
STREAM_3_SYNCHRONIZATION_STEAM_NOT_STOPPED  = 0x00000000,
STREAM_3_SYNCHRONIZATION_STEAM_STOPPED   = 0x00000001,
} STREAM_3_SYNCHRONIZATION;

/*
 * STREAM_4_SYNCHRONIZATION enum
 */

typedef enum STREAM_4_SYNCHRONIZATION {
STREAM_4_SYNCHRONIZATION_STEAM_NOT_STOPPED  = 0x00000000,
STREAM_4_SYNCHRONIZATION_STEAM_STOPPED   = 0x00000001,
} STREAM_4_SYNCHRONIZATION;

/*
 * STREAM_5_SYNCHRONIZATION enum
 */

typedef enum STREAM_5_SYNCHRONIZATION {
STREAM_5_SYNCHRONIZATION_STEAM_NOT_STOPPED  = 0x00000000,
STREAM_5_SYNCHRONIZATION_STEAM_STOPPED   = 0x00000001,
} STREAM_5_SYNCHRONIZATION;

/*
 * STREAM_6_SYNCHRONIZATION enum
 */

typedef enum STREAM_6_SYNCHRONIZATION {
STREAM_6_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED  = 0x00000000,
STREAM_6_SYNCHRONIZATION_STEAM_STOPPED_RESERVED  = 0x00000001,
} STREAM_6_SYNCHRONIZATION;

/*
 * STREAM_7_SYNCHRONIZATION enum
 */

typedef enum STREAM_7_SYNCHRONIZATION {
STREAM_7_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED  = 0x00000000,
STREAM_7_SYNCHRONIZATION_STEAM_STOPPED_RESERVED  = 0x00000001,
} STREAM_7_SYNCHRONIZATION;

/*
 * STREAM_8_SYNCHRONIZATION enum
 */

typedef enum STREAM_8_SYNCHRONIZATION {
STREAM_8_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED  = 0x00000000,
STREAM_8_SYNCHRONIZATION_STEAM_STOPPED_RESERVED  = 0x00000001,
} STREAM_8_SYNCHRONIZATION;

/*
 * STREAM_9_SYNCHRONIZATION enum
 */

typedef enum STREAM_9_SYNCHRONIZATION {
STREAM_9_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED  = 0x00000000,
STREAM_9_SYNCHRONIZATION_STEAM_STOPPED_RESERVED  = 0x00000001,
} STREAM_9_SYNCHRONIZATION;

/*
 * STREAM_10_SYNCHRONIZATION enum
 */

typedef enum STREAM_10_SYNCHRONIZATION {
STREAM_10_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED  = 0x00000000,
STREAM_10_SYNCHRONIZATION_STEAM_STOPPED_RESERVED  = 0x00000001,
} STREAM_10_SYNCHRONIZATION;

/*
 * STREAM_11_SYNCHRONIZATION enum
 */

typedef enum STREAM_11_SYNCHRONIZATION {
STREAM_11_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED  = 0x00000000,
STREAM_11_SYNCHRONIZATION_STEAM_STOPPED_RESERVED  = 0x00000001,
} STREAM_11_SYNCHRONIZATION;

/*
 * STREAM_12_SYNCHRONIZATION enum
 */

typedef enum STREAM_12_SYNCHRONIZATION {
STREAM_12_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED  = 0x00000000,
STREAM_12_SYNCHRONIZATION_STEAM_STOPPED_RESERVED  = 0x00000001,
} STREAM_12_SYNCHRONIZATION;

/*
 * STREAM_13_SYNCHRONIZATION enum
 */

typedef enum STREAM_13_SYNCHRONIZATION {
STREAM_13_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED  = 0x00000000,
STREAM_13_SYNCHRONIZATION_STEAM_STOPPED_RESERVED  = 0x00000001,
} STREAM_13_SYNCHRONIZATION;

/*
 * STREAM_14_SYNCHRONIZATION enum
 */

typedef enum STREAM_14_SYNCHRONIZATION {
STREAM_14_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED  = 0x00000000,
STREAM_14_SYNCHRONIZATION_STEAM_STOPPED_RESERVED  = 0x00000001,
} STREAM_14_SYNCHRONIZATION;

/*
 * STREAM_15_SYNCHRONIZATION enum
 */

typedef enum STREAM_15_SYNCHRONIZATION {
STREAM_15_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED  = 0x00000000,
STREAM_15_SYNCHRONIZATION_STEAM_STOPPED_RESERVED  = 0x00000001,
} STREAM_15_SYNCHRONIZATION;

/*
 * CORB_READ_POINTER_RESET enum
 */

typedef enum CORB_READ_POINTER_RESET {
CORB_READ_POINTER_RESET_CORB_DMA_IS_NOT_RESET  = 0x00000000,
CORB_READ_POINTER_RESET_CORB_DMA_IS_RESET  = 0x00000001,
} CORB_READ_POINTER_RESET;

/*
 * AZ_CORB_SIZE enum
 */

typedef enum AZ_CORB_SIZE {
AZ_CORB_SIZE_2ENTRIES_RESERVED           = 0x00000000,
AZ_CORB_SIZE_16ENTRIES_RESERVED          = 0x00000001,
AZ_CORB_SIZE_256ENTRIES                  = 0x00000002,
AZ_CORB_SIZE_RESERVED                    = 0x00000003,
} AZ_CORB_SIZE;

/*
 * AZ_RIRB_WRITE_POINTER_RESET enum
 */

typedef enum AZ_RIRB_WRITE_POINTER_RESET {
AZ_RIRB_WRITE_POINTER_NOT_RESET          = 0x00000000,
AZ_RIRB_WRITE_POINTER_DO_RESET           = 0x00000001,
} AZ_RIRB_WRITE_POINTER_RESET;

/*
 * RIRB_CONTROL_RESPONSE_OVERRUN_INTERRUPT_CONTROL enum
 */

typedef enum RIRB_CONTROL_RESPONSE_OVERRUN_INTERRUPT_CONTROL {
RIRB_CONTROL_RESPONSE_OVERRUN_INTERRUPT_CONTROL_INTERRUPT_DISABLED  = 0x00000000,
RIRB_CONTROL_RESPONSE_OVERRUN_INTERRUPT_CONTROL_INTERRUPT_ENABLED  = 0x00000001,
} RIRB_CONTROL_RESPONSE_OVERRUN_INTERRUPT_CONTROL;

/*
 * RIRB_CONTROL_RESPONSE_INTERRUPT_CONTROL enum
 */

typedef enum RIRB_CONTROL_RESPONSE_INTERRUPT_CONTROL {
RIRB_CONTROL_RESPONSE_INTERRUPT_CONTROL_INTERRUPT_DISABLED  = 0x00000000,
RIRB_CONTROL_RESPONSE_INTERRUPT_CONTROL_INTERRUPT_ENABLED  = 0x00000001,
} RIRB_CONTROL_RESPONSE_INTERRUPT_CONTROL;

/*
 * AZ_RIRB_SIZE enum
 */

typedef enum AZ_RIRB_SIZE {
AZ_RIRB_SIZE_2ENTRIES_RESERVED           = 0x00000000,
AZ_RIRB_SIZE_16ENTRIES_RESERVED          = 0x00000001,
AZ_RIRB_SIZE_256ENTRIES                  = 0x00000002,
AZ_RIRB_SIZE_UNDEFINED                   = 0x00000003,
} AZ_RIRB_SIZE;

/*
 * IMMEDIATE_COMMAND_STATUS_IMMEDIATE_RESULT_VALID enum
 */

typedef enum IMMEDIATE_COMMAND_STATUS_IMMEDIATE_RESULT_VALID {
IMMEDIATE_COMMAND_STATUS_IMMEDIATE_RESULT_VALID_NO_IMMEDIATE_RESPONSE_VALID  = 0x00000000,
IMMEDIATE_COMMAND_STATUS_IMMEDIATE_RESULT_VALID_IMMEDIATE_RESPONSE_VALID  = 0x00000001,
} IMMEDIATE_COMMAND_STATUS_IMMEDIATE_RESULT_VALID;

/*
 * IMMEDIATE_COMMAND_STATUS_IMMEDIATE_COMMAND_BUSY enum
 */

typedef enum IMMEDIATE_COMMAND_STATUS_IMMEDIATE_COMMAND_BUSY {
IMMEDIATE_COMMAND_STATUS_IMMEDIATE_COMMAND_NOT_BUSY  = 0x00000000,
IMMEDIATE_COMMAND_STATUS_IMMEDIATE_COMMAND_IS_BUSY  = 0x00000001,
} IMMEDIATE_COMMAND_STATUS_IMMEDIATE_COMMAND_BUSY;

/*
 * DMA_POSITION_LOWER_BASE_ADDRESS_BUFFER_ENABLE enum
 */

typedef enum DMA_POSITION_LOWER_BASE_ADDRESS_BUFFER_ENABLE {
DMA_POSITION_LOWER_BASE_ADDRESS_BUFFER_ENABLE_DMA_DISABLE  = 0x00000000,
DMA_POSITION_LOWER_BASE_ADDRESS_BUFFER_ENABLE_DMA_ENABLE  = 0x00000001,
} DMA_POSITION_LOWER_BASE_ADDRESS_BUFFER_ENABLE;

/*******************************************************
 * AZENDPOINT Enums
 *******************************************************/

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE {
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE_PCM  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE_NOT_PCM  = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE {
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE_48KHZ  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE_44P1KHZ  = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE {
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY1  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY2  = 0x00000001,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY3_RESERVED  = 0x00000002,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY4  = 0x00000003,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_RESERVED  = 0x00000004,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR {
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY1  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY2_RESERVED  = 0x00000001,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY3  = 0x00000002,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY4_RESERVED  = 0x00000003,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY5_RESERVED  = 0x00000004,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY6_RESERVED  = 0x00000005,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY7_RESERVED  = 0x00000006,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY8_RESERVED  = 0x00000007,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE {
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_8_RESERVED  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_16  = 0x00000001,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_20  = 0x00000002,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_24  = 0x00000003,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_32_RESERVED  = 0x00000004,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_RESERVED  = 0x00000005,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS {
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_1  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_2  = 0x00000001,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_3  = 0x00000002,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_4  = 0x00000003,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_5  = 0x00000004,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_6  = 0x00000005,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_7  = 0x00000006,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_8  = 0x00000007,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_RESERVED  = 0x00000008,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_L enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_L {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_L_BIT7_NOT_SET  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_L_BIT7_IS_SET  = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_L;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRO enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRO {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRO_BIT_A_NOT_SET  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRO_BIT_A_IS_SET  = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRO;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_NON_AUDIO enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_NON_AUDIO {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_NON_AUDIO_BIT_B_NOT_SET  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_NON_AUDIO_BIT_B_IS_SET  = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_NON_AUDIO;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_COPY enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_COPY {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_COPY_BIT_C_IS_SET  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_COPY_BIT_C_NOT_SET  = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_COPY;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRE enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRE {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRE_LSB_OF_D_NOT_SET  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRE_LSB_OF_D_IS_SET  = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRE;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_VCFG enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_VCFG {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_VALIDITY_CFG_NOT_ON  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_VALIDITY_CFG_ON  = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_VCFG;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_V enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_V {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_V_BIT28_IS_ZERO  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_V_BIT28_IS_ONE  = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_V;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN_DIGITAL_TRANSMISSION_DISABLED  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN_DIGITAL_TRANSMISSION_ENABLED  = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_3_KEEPALIVE enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_3_KEEPALIVE {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_3_KEEPALIVE_SILENT_STREAM_NOT_ENABLE  = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_3_KEEPALIVE_SILENT_STREAM_ENABLE  = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_3_KEEPALIVE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_WIDGET_CONTROL_OUT_ENABLE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_WIDGET_CONTROL_OUT_ENABLE {
AZALIA_F2_CODEC_PIN_CONTROL_WIDGET_CONTROL_OUT_ENABLE_PIN_SHUT_OFF  = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_WIDGET_CONTROL_OUT_ENABLE_PIN_DRIVEN  = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_WIDGET_CONTROL_OUT_ENABLE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLE {
AZALIA_F2_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_DISABLED  = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLED  = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_DOWN_MIX_INFO_DOWN_MIX_INHIBIT enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_DOWN_MIX_INFO_DOWN_MIX_INHIBIT {
AZALIA_F2_CODEC_PIN_CONTROL_DOWN_MIX_NO_INFO_OR_PERMITTED  = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_DOWN_MIX_FORBIDDEN  = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_DOWN_MIX_INFO_DOWN_MIX_INHIBIT;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL01_ENABLE_MULTICHANNEL01_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL01_ENABLE_MULTICHANNEL01_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL01_ENABLE_MULTICHANNEL01_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL01_ENABLE_MULTICHANNEL01_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL01_ENABLE_MULTICHANNEL01_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL23_ENABLE_MULTICHANNEL23_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL23_ENABLE_MULTICHANNEL23_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL23_ENABLE_MULTICHANNEL23_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL23_ENABLE_MULTICHANNEL23_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL23_ENABLE_MULTICHANNEL23_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL45_ENABLE_MULTICHANNEL45_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL45_ENABLE_MULTICHANNEL45_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL45_ENABLE_MULTICHANNEL45_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL45_ENABLE_MULTICHANNEL45_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL45_ENABLE_MULTICHANNEL45_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL67_ENABLE_MULTICHANNEL67_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL67_ENABLE_MULTICHANNEL67_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL67_ENABLE_MULTICHANNEL67_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL67_ENABLE_MULTICHANNEL67_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL67_ENABLE_MULTICHANNEL67_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_MODE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_MODE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_PAIR_MODE  = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_SINGLE_MODE  = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_MODE;

/*******************************************************
 * AZF0CONTROLLER Enums
 *******************************************************/

/*
 * AZALIA_SOFT_RESET_REFCLK_SOFT_RESET enum
 */

typedef enum AZALIA_SOFT_RESET_REFCLK_SOFT_RESET {
AZALIA_SOFT_RESET_REFCLK_SOFT_RESET_NOT_RESET  = 0x00000000,
AZALIA_SOFT_RESET_REFCLK_SOFT_RESET_RESET_REFCLK_LOGIC  = 0x00000001,
} AZALIA_SOFT_RESET_REFCLK_SOFT_RESET;

/*******************************************************
 * AZF0ROOT Enums
 *******************************************************/

/*
 * CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY enum
 */

typedef enum CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY {
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_ALL  = 0x00000000,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_6  = 0x00000001,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_5  = 0x00000002,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_4  = 0x00000003,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_3  = 0x00000004,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_2  = 0x00000005,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_1  = 0x00000006,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_0  = 0x00000007,
} CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY;

/*
 * CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY enum
 */

typedef enum CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY {
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_ALL  = 0x00000000,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_6  = 0x00000001,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_5  = 0x00000002,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_4  = 0x00000003,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_3  = 0x00000004,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_2  = 0x00000005,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_1  = 0x00000006,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_0  = 0x00000007,
} CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY;

/*******************************************************
 * AZINPUTENDPOINT Enums
 *******************************************************/

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE_PCM  = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE_NOT_PCM  = 0x00000001,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE;

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE_48KHZ  = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE_44P1KHZ  = 0x00000001,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE;

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY1  = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY2  = 0x00000001,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY3_RESERVED  = 0x00000002,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY4  = 0x00000003,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_RESERVED  = 0x00000004,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE;

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY1  = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY2_RESERVED  = 0x00000001,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY3  = 0x00000002,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY4_RESERVED  = 0x00000003,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY5_RESERVED  = 0x00000004,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY6_RESERVED  = 0x00000005,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY7_RESERVED  = 0x00000006,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY8_RESERVED  = 0x00000007,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR;

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_8_RESERVED  = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_16  = 0x00000001,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_20  = 0x00000002,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_24  = 0x00000003,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_32_RESERVED  = 0x00000004,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_RESERVED  = 0x00000005,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE;

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_1  = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_2  = 0x00000001,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_3  = 0x00000002,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_4  = 0x00000003,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_5  = 0x00000004,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_6  = 0x00000005,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_7  = 0x00000006,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_8  = 0x00000007,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_RESERVED  = 0x00000008,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS;

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN_DIGITAL_TRANSMISSION_DISABLED  = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN_DIGITAL_TRANSMISSION_ENABLED  = 0x00000001,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_WIDGET_CONTROL_IN_ENABLE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_WIDGET_CONTROL_IN_ENABLE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_WIDGET_CONTROL_IN_ENABLE_PIN_SHUT_OFF  = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_WIDGET_CONTROL_IN_ENABLE_PIN_DRIVEN  = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_WIDGET_CONTROL_IN_ENABLE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_UNSOLICITED_RESPONSE_DISABLED  = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLED  = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL0_ENABLE_MULTICHANNEL0_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL0_ENABLE_MULTICHANNEL0_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL0_ENABLE_MULTICHANNEL0_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL0_ENABLE_MULTICHANNEL0_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL0_ENABLE_MULTICHANNEL0_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL2_ENABLE_MULTICHANNEL2_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL2_ENABLE_MULTICHANNEL2_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL2_ENABLE_MULTICHANNEL2_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL2_ENABLE_MULTICHANNEL2_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL2_ENABLE_MULTICHANNEL2_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL4_ENABLE_MULTICHANNEL4_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL4_ENABLE_MULTICHANNEL4_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL4_ENABLE_MULTICHANNEL4_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL4_ENABLE_MULTICHANNEL4_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL4_ENABLE_MULTICHANNEL4_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL6_ENABLE_MULTICHANNEL6_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL6_ENABLE_MULTICHANNEL6_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL6_ENABLE_MULTICHANNEL6_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL6_ENABLE_MULTICHANNEL6_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL6_ENABLE_MULTICHANNEL6_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_NOT_MUTED  = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTED  = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTE;

/*******************************************************
 * AZROOT Enums
 *******************************************************/

/*
 * AZALIA_F2_CODEC_FUNCTION_CONTROL_RESET_CODEC_RESET enum
 */

typedef enum AZALIA_F2_CODEC_FUNCTION_CONTROL_RESET_CODEC_RESET {
AZALIA_F2_CODEC_FUNCTION_CONTROL_RESET_CODEC_NOT_RESET  = 0x00000000,
AZALIA_F2_CODEC_FUNCTION_CONTROL_RESET_CODEC_DO_RESET  = 0x00000001,
} AZALIA_F2_CODEC_FUNCTION_CONTROL_RESET_CODEC_RESET;

/*******************************************************
 * DCCG Enums
 *******************************************************/

/*
 * ENABLE enum
 */

typedef enum ENABLE {
DISABLE_THE_FEATURE                      = 0x00000000,
ENABLE_THE_FEATURE                       = 0x00000001,
} ENABLE;

/*
 * ENABLE_CLOCK enum
 */

typedef enum ENABLE_CLOCK {
DISABLE_THE_CLOCK                        = 0x00000000,
ENABLE_THE_CLOCK                         = 0x00000001,
} ENABLE_CLOCK;

/*
 * FORCE_VBI enum
 */

typedef enum FORCE_VBI {
FORCE_VBI_LOW                            = 0x00000000,
FORCE_VBI_HIGH                           = 0x00000001,
} FORCE_VBI;

/*
 * OVERRIDE_CGTT_SCLK enum
 */

typedef enum OVERRIDE_CGTT_SCLK {
OVERRIDE_CGTT_SCLK_NOOP                  = 0x00000000,
SET_OVERRIDE_CGTT_SCLK                   = 0x00000001,
} OVERRIDE_CGTT_SCLK;

/*
 * CLEAR_SMU_INTR enum
 */

typedef enum CLEAR_SMU_INTR {
SMU_INTR_STATUS_NOOP                     = 0x00000000,
SMU_INTR_STATUS_CLEAR                    = 0x00000001,
} CLEAR_SMU_INTR;

/*
 * STATIC_SCREEN_SMU_INTR enum
 */

typedef enum STATIC_SCREEN_SMU_INTR {
STATIC_SCREEN_SMU_INTR_NOOP              = 0x00000000,
SET_STATIC_SCREEN_SMU_INTR               = 0x00000001,
} STATIC_SCREEN_SMU_INTR;

/*
 * JITTER_REMOVE_DISABLE enum
 */

typedef enum JITTER_REMOVE_DISABLE {
ENABLE_JITTER_REMOVAL                    = 0x00000000,
DISABLE_JITTER_REMOVAL                   = 0x00000001,
} JITTER_REMOVE_DISABLE;

/*
 * DS_REF_SRC enum
 */

typedef enum DS_REF_SRC {
DS_REF_IS_XTALIN                         = 0x00000000,
DS_REF_IS_EXT_GENLOCK                    = 0x00000001,
DS_REF_IS_PCIE                           = 0x00000002,
} DS_REF_SRC;

/*
 * DISABLE_CLOCK_GATING enum
 */

typedef enum DISABLE_CLOCK_GATING {
CLOCK_GATING_ENABLED                     = 0x00000000,
CLOCK_GATING_DISABLED                    = 0x00000001,
} DISABLE_CLOCK_GATING;

/*
 * DISABLE_CLOCK_GATING_IN_DCO enum
 */

typedef enum DISABLE_CLOCK_GATING_IN_DCO {
CLOCK_GATING_ENABLED_IN_DCO              = 0x00000000,
CLOCK_GATING_DISABLED_IN_DCO             = 0x00000001,
} DISABLE_CLOCK_GATING_IN_DCO;

/*
 * DCCG_DEEP_COLOR_CNTL enum
 */

typedef enum DCCG_DEEP_COLOR_CNTL {
DCCG_DEEP_COLOR_DTO_DISABLE              = 0x00000000,
DCCG_DEEP_COLOR_DTO_5_4_RATIO            = 0x00000001,
DCCG_DEEP_COLOR_DTO_3_2_RATIO            = 0x00000002,
DCCG_DEEP_COLOR_DTO_2_1_RATIO            = 0x00000003,
} DCCG_DEEP_COLOR_CNTL;

/*
 * REFCLK_CLOCK_EN enum
 */

typedef enum REFCLK_CLOCK_EN {
REFCLK_CLOCK_EN_XTALIN_CLK               = 0x00000000,
REFCLK_CLOCK_EN_ALLOW_SRC_SEL            = 0x00000001,
} REFCLK_CLOCK_EN;

/*
 * REFCLK_SRC_SEL enum
 */

typedef enum REFCLK_SRC_SEL {
REFCLK_SRC_SEL_PCIE_REFCLK               = 0x00000000,
REFCLK_SRC_SEL_CPL_REFCLK                = 0x00000001,
} REFCLK_SRC_SEL;

/*
 * DPREFCLK_SRC_SEL enum
 */

typedef enum DPREFCLK_SRC_SEL {
DPREFCLK_SRC_SEL_CK                      = 0x00000000,
DPREFCLK_SRC_SEL_P0PLL                   = 0x00000001,
DPREFCLK_SRC_SEL_P1PLL                   = 0x00000002,
DPREFCLK_SRC_SEL_P2PLL                   = 0x00000003,
DPREFCLK_SRC_SEL_P3PLL                   = 0x00000004,
} DPREFCLK_SRC_SEL;

/*
 * XTAL_REF_SEL enum
 */

typedef enum XTAL_REF_SEL {
XTAL_REF_SEL_1X                          = 0x00000000,
XTAL_REF_SEL_2X                          = 0x00000001,
} XTAL_REF_SEL;

/*
 * XTAL_REF_CLOCK_SOURCE_SEL enum
 */

typedef enum XTAL_REF_CLOCK_SOURCE_SEL {
XTAL_REF_CLOCK_SOURCE_SEL_XTALIN         = 0x00000000,
XTAL_REF_CLOCK_SOURCE_SEL_PPLL           = 0x00000001,
} XTAL_REF_CLOCK_SOURCE_SEL;

/*
 * MICROSECOND_TIME_BASE_CLOCK_SOURCE_SEL enum
 */

typedef enum MICROSECOND_TIME_BASE_CLOCK_SOURCE_SEL {
MICROSECOND_TIME_BASE_CLOCK_IS_XTALIN    = 0x00000000,
MICROSECOND_TIME_BASE_CLOCK_IS_PPLL_REFCLK  = 0x00000001,
} MICROSECOND_TIME_BASE_CLOCK_SOURCE_SEL;

/*
 * ALLOW_SR_ON_TRANS_REQ enum
 */

typedef enum ALLOW_SR_ON_TRANS_REQ {
ALLOW_SR_ON_TRANS_REQ_ENABLE             = 0x00000000,
ALLOW_SR_ON_TRANS_REQ_DISABLE            = 0x00000001,
} ALLOW_SR_ON_TRANS_REQ;

/*
 * MILLISECOND_TIME_BASE_CLOCK_SOURCE_SEL enum
 */

typedef enum MILLISECOND_TIME_BASE_CLOCK_SOURCE_SEL {
MILLISECOND_TIME_BASE_CLOCK_IS_XTALIN    = 0x00000000,
MILLISECOND_TIME_BASE_CLOCK_IS_PPLL_REFCLK  = 0x00000001,
} MILLISECOND_TIME_BASE_CLOCK_SOURCE_SEL;

/*
 * PIPE_PIXEL_RATE_SOURCE enum
 */

typedef enum PIPE_PIXEL_RATE_SOURCE {
PIPE_PIXEL_RATE_SOURCE_P0PLL             = 0x00000000,
PIPE_PIXEL_RATE_SOURCE_P1PLL             = 0x00000001,
PIPE_PIXEL_RATE_SOURCE_P2PLL             = 0x00000002,
} PIPE_PIXEL_RATE_SOURCE;

/*
 * PIPE_PHYPLL_PIXEL_RATE_SOURCE enum
 */

typedef enum PIPE_PHYPLL_PIXEL_RATE_SOURCE {
PIPE_PHYPLL_PIXEL_RATE_SOURCE_UNIPHYA    = 0x00000000,
PIPE_PHYPLL_PIXEL_RATE_SOURCE_UNIPHYB    = 0x00000001,
PIPE_PHYPLL_PIXEL_RATE_SOURCE_UNIPHYC    = 0x00000002,
PIPE_PHYPLL_PIXEL_RATE_SOURCE_UNIPHYD    = 0x00000003,
PIPE_PHYPLL_PIXEL_RATE_SOURCE_UNIPHYE    = 0x00000004,
PIPE_PHYPLL_PIXEL_RATE_SOURCE_UNIPHYF    = 0x00000005,
PIPE_PHYPLL_PIXEL_RATE_SOURCE_UNIPHYG    = 0x00000006,
} PIPE_PHYPLL_PIXEL_RATE_SOURCE;

/*
 * PIPE_PIXEL_RATE_PLL_SOURCE enum
 */

typedef enum PIPE_PIXEL_RATE_PLL_SOURCE {
PIPE_PIXEL_RATE_PLL_SOURCE_PHYPLL        = 0x00000000,
PIPE_PIXEL_RATE_PLL_SOURCE_DISPPLL       = 0x00000001,
} PIPE_PIXEL_RATE_PLL_SOURCE;

/*
 * DP_DTO_DS_DISABLE enum
 */

typedef enum DP_DTO_DS_DISABLE {
DP_DTO_DESPREAD_DISABLE                  = 0x00000000,
DP_DTO_DESPREAD_ENABLE                   = 0x00000001,
} DP_DTO_DS_DISABLE;

/*
 * CRTC_ADD_PIXEL enum
 */

typedef enum CRTC_ADD_PIXEL {
CRTC_ADD_PIXEL_NOOP                      = 0x00000000,
CRTC_ADD_PIXEL_FORCE                     = 0x00000001,
} CRTC_ADD_PIXEL;

/*
 * CRTC_DROP_PIXEL enum
 */

typedef enum CRTC_DROP_PIXEL {
CRTC_DROP_PIXEL_NOOP                     = 0x00000000,
CRTC_DROP_PIXEL_FORCE                    = 0x00000001,
} CRTC_DROP_PIXEL;

/*
 * SYMCLK_FE_FORCE_EN enum
 */

typedef enum SYMCLK_FE_FORCE_EN {
SYMCLK_FE_FORCE_EN_DISABLE               = 0x00000000,
SYMCLK_FE_FORCE_EN_ENABLE                = 0x00000001,
} SYMCLK_FE_FORCE_EN;

/*
 * SYMCLK_FE_FORCE_SRC enum
 */

typedef enum SYMCLK_FE_FORCE_SRC {
SYMCLK_FE_FORCE_SRC_UNIPHYA              = 0x00000000,
SYMCLK_FE_FORCE_SRC_UNIPHYB              = 0x00000001,
SYMCLK_FE_FORCE_SRC_UNIPHYC              = 0x00000002,
SYMCLK_FE_FORCE_SRC_UNIPHYD              = 0x00000003,
SYMCLK_FE_FORCE_SRC_UNIPHYE              = 0x00000004,
SYMCLK_FE_FORCE_SRC_UNIPHYF              = 0x00000005,
SYMCLK_FE_FORCE_SRC_UNIPHYG              = 0x00000006,
} SYMCLK_FE_FORCE_SRC;

/*
 * DPDBG_CLK_FORCE_EN enum
 */

typedef enum DPDBG_CLK_FORCE_EN {
DPDBG_CLK_FORCE_EN_DISABLE               = 0x00000000,
DPDBG_CLK_FORCE_EN_ENABLE                = 0x00000001,
} DPDBG_CLK_FORCE_EN;

/*
 * DVOACLK_COARSE_SKEW_CNTL enum
 */

typedef enum DVOACLK_COARSE_SKEW_CNTL {
DVOACLK_COARSE_SKEW_CNTL_NO_ADJUSTMENT   = 0x00000000,
DVOACLK_COARSE_SKEW_CNTL_DELAY_1_STEP    = 0x00000001,
DVOACLK_COARSE_SKEW_CNTL_DELAY_2_STEPS   = 0x00000002,
DVOACLK_COARSE_SKEW_CNTL_DELAY_3_STEPS   = 0x00000003,
DVOACLK_COARSE_SKEW_CNTL_DELAY_4_STEPS   = 0x00000004,
DVOACLK_COARSE_SKEW_CNTL_DELAY_5_STEPS   = 0x00000005,
DVOACLK_COARSE_SKEW_CNTL_DELAY_6_STEPS   = 0x00000006,
DVOACLK_COARSE_SKEW_CNTL_DELAY_7_STEPS   = 0x00000007,
DVOACLK_COARSE_SKEW_CNTL_DELAY_8_STEPS   = 0x00000008,
DVOACLK_COARSE_SKEW_CNTL_DELAY_9_STEPS   = 0x00000009,
DVOACLK_COARSE_SKEW_CNTL_DELAY_10_STEPS  = 0x0000000a,
DVOACLK_COARSE_SKEW_CNTL_DELAY_11_STEPS  = 0x0000000b,
DVOACLK_COARSE_SKEW_CNTL_DELAY_12_STEPS  = 0x0000000c,
DVOACLK_COARSE_SKEW_CNTL_DELAY_13_STEPS  = 0x0000000d,
DVOACLK_COARSE_SKEW_CNTL_DELAY_14_STEPS  = 0x0000000e,
DVOACLK_COARSE_SKEW_CNTL_DELAY_15_STEPS  = 0x0000000f,
DVOACLK_COARSE_SKEW_CNTL_EARLY_1_STEP    = 0x00000010,
DVOACLK_COARSE_SKEW_CNTL_EARLY_2_STEPS   = 0x00000011,
DVOACLK_COARSE_SKEW_CNTL_EARLY_3_STEPS   = 0x00000012,
DVOACLK_COARSE_SKEW_CNTL_EARLY_4_STEPS   = 0x00000013,
DVOACLK_COARSE_SKEW_CNTL_EARLY_5_STEPS   = 0x00000014,
DVOACLK_COARSE_SKEW_CNTL_EARLY_6_STEPS   = 0x00000015,
DVOACLK_COARSE_SKEW_CNTL_EARLY_7_STEPS   = 0x00000016,
DVOACLK_COARSE_SKEW_CNTL_EARLY_8_STEPS   = 0x00000017,
DVOACLK_COARSE_SKEW_CNTL_EARLY_9_STEPS   = 0x00000018,
DVOACLK_COARSE_SKEW_CNTL_EARLY_10_STEPS  = 0x00000019,
DVOACLK_COARSE_SKEW_CNTL_EARLY_11_STEPS  = 0x0000001a,
DVOACLK_COARSE_SKEW_CNTL_EARLY_12_STEPS  = 0x0000001b,
DVOACLK_COARSE_SKEW_CNTL_EARLY_13_STEPS  = 0x0000001c,
DVOACLK_COARSE_SKEW_CNTL_EARLY_14_STEPS  = 0x0000001d,
DVOACLK_COARSE_SKEW_CNTL_EARLY_15_STEPS  = 0x0000001e,
} DVOACLK_COARSE_SKEW_CNTL;

/*
 * DVOACLK_FINE_SKEW_CNTL enum
 */

typedef enum DVOACLK_FINE_SKEW_CNTL {
DVOACLK_FINE_SKEW_CNTL_NO_ADJUSTMENT     = 0x00000000,
DVOACLK_FINE_SKEW_CNTL_DELAY_1_STEP      = 0x00000001,
DVOACLK_FINE_SKEW_CNTL_DELAY_2_STEPS     = 0x00000002,
DVOACLK_FINE_SKEW_CNTL_DELAY_3_STEPS     = 0x00000003,
DVOACLK_FINE_SKEW_CNTL_EARLY_1_STEP      = 0x00000004,
DVOACLK_FINE_SKEW_CNTL_EARLY_2_STEPS     = 0x00000005,
DVOACLK_FINE_SKEW_CNTL_EARLY_3_STEPS     = 0x00000006,
DVOACLK_FINE_SKEW_CNTL_EARLY_4_STEPS     = 0x00000007,
} DVOACLK_FINE_SKEW_CNTL;

/*
 * DVOACLKD_IN_PHASE enum
 */

typedef enum DVOACLKD_IN_PHASE {
DVOACLKD_IN_OPPOSITE_PHASE_WITH_PCLK_DVO  = 0x00000000,
DVOACLKD_IN_PHASE_WITH_PCLK_DVO          = 0x00000001,
} DVOACLKD_IN_PHASE;

/*
 * DVOACLKC_IN_PHASE enum
 */

typedef enum DVOACLKC_IN_PHASE {
DVOACLKC_IN_OPPOSITE_PHASE_WITH_PCLK_DVO  = 0x00000000,
DVOACLKC_IN_PHASE_WITH_PCLK_DVO          = 0x00000001,
} DVOACLKC_IN_PHASE;

/*
 * DVOACLKC_MVP_IN_PHASE enum
 */

typedef enum DVOACLKC_MVP_IN_PHASE {
DVOACLKC_MVP_IN_OPPOSITE_PHASE_WITH_PCLK_DVO  = 0x00000000,
DVOACLKC_MVP_IN_PHASE_WITH_PCLK_DVO      = 0x00000001,
} DVOACLKC_MVP_IN_PHASE;

/*
 * DVOACLKC_MVP_SKEW_PHASE_OVERRIDE enum
 */

typedef enum DVOACLKC_MVP_SKEW_PHASE_OVERRIDE {
DVOACLKC_MVP_SKEW_PHASE_OVERRIDE_DISABLE  = 0x00000000,
DVOACLKC_MVP_SKEW_PHASE_OVERRIDE_ENABLE  = 0x00000001,
} DVOACLKC_MVP_SKEW_PHASE_OVERRIDE;

/*
 * MVP_CLK_SRC_SEL enum
 */

typedef enum MVP_CLK_SRC_SEL {
MVP_CLK_SRC_SEL_RSRV                     = 0x00000000,
MVP_CLK_SRC_SEL_IO_1                     = 0x00000001,
MVP_CLK_SRC_SEL_IO_2                     = 0x00000002,
MVP_CLK_SRC_SEL_REFCLK                   = 0x00000003,
} MVP_CLK_SRC_SEL;

/*
 * DCCG_AUDIO_DTO0_SOURCE_SEL enum
 */

typedef enum DCCG_AUDIO_DTO0_SOURCE_SEL {
DCCG_AUDIO_DTO0_SOURCE_SEL_CRTC0         = 0x00000000,
DCCG_AUDIO_DTO0_SOURCE_SEL_CRTC1         = 0x00000001,
DCCG_AUDIO_DTO0_SOURCE_SEL_CRTC2         = 0x00000002,
DCCG_AUDIO_DTO0_SOURCE_SEL_CRTC3         = 0x00000003,
DCCG_AUDIO_DTO0_SOURCE_SEL_CRTC4         = 0x00000004,
DCCG_AUDIO_DTO0_SOURCE_SEL_CRTC5         = 0x00000005,
DCCG_AUDIO_DTO0_SOURCE_SEL_RESERVED      = 0x00000006,
} DCCG_AUDIO_DTO0_SOURCE_SEL;

/*
 * DCCG_AUDIO_DTO_SEL enum
 */

typedef enum DCCG_AUDIO_DTO_SEL {
DCCG_AUDIO_DTO_SEL_AUDIO_DTO0            = 0x00000000,
DCCG_AUDIO_DTO_SEL_AUDIO_DTO1            = 0x00000001,
DCCG_AUDIO_DTO_SEL_NO_AUDIO_DTO          = 0x00000002,
} DCCG_AUDIO_DTO_SEL;

/*
 * DCCG_AUDIO_DTO2_SOURCE_SEL enum
 */

typedef enum DCCG_AUDIO_DTO2_SOURCE_SEL {
DCCG_AUDIO_DTO2_SOURCE_SEL_AMCLK0        = 0x00000000,
DCCG_AUDIO_DTO2_SOURCE_SEL_AMCLK1        = 0x00000001,
} DCCG_AUDIO_DTO2_SOURCE_SEL;

/*
 * DCCG_AUDIO_DTO_USE_512FBR_DTO enum
 */

typedef enum DCCG_AUDIO_DTO_USE_512FBR_DTO {
DCCG_AUDIO_DTO_USE_128FBR_FOR_DP         = 0x00000000,
DCCG_AUDIO_DTO_USE_512FBR_FOR_DP         = 0x00000001,
} DCCG_AUDIO_DTO_USE_512FBR_DTO;

/*
 * DCCG_DBG_EN enum
 */

typedef enum DCCG_DBG_EN {
DCCG_DBG_EN_DISABLE                      = 0x00000000,
DCCG_DBG_EN_ENABLE                       = 0x00000001,
} DCCG_DBG_EN;

/*
 * DCCG_DBG_BLOCK_SEL enum
 */

typedef enum DCCG_DBG_BLOCK_SEL {
DCCG_DBG_BLOCK_SEL_DCCG                  = 0x00000000,
DCCG_DBG_BLOCK_SEL_PMON                  = 0x00000001,
DCCG_DBG_BLOCK_SEL_PMON2                 = 0x00000002,
} DCCG_DBG_BLOCK_SEL;

/*
 * DISPCLK_FREQ_RAMP_DONE enum
 */

typedef enum DISPCLK_FREQ_RAMP_DONE {
DISPCLK_FREQ_RAMP_IN_PROGRESS            = 0x00000000,
DISPCLK_FREQ_RAMP_COMPLETED              = 0x00000001,
} DISPCLK_FREQ_RAMP_DONE;

/*
 * DCCG_FIFO_ERRDET_RESET enum
 */

typedef enum DCCG_FIFO_ERRDET_RESET {
DCCG_FIFO_ERRDET_RESET_NOOP              = 0x00000000,
DCCG_FIFO_ERRDET_RESET_FORCE             = 0x00000001,
} DCCG_FIFO_ERRDET_RESET;

/*
 * DCCG_FIFO_ERRDET_STATE enum
 */

typedef enum DCCG_FIFO_ERRDET_STATE {
DCCG_FIFO_ERRDET_STATE_DETECTION         = 0x00000000,
DCCG_FIFO_ERRDET_STATE_CALIBRATION       = 0x00000001,
} DCCG_FIFO_ERRDET_STATE;

/*
 * DCCG_FIFO_ERRDET_OVR_EN enum
 */

typedef enum DCCG_FIFO_ERRDET_OVR_EN {
DCCG_FIFO_ERRDET_OVR_DISABLE             = 0x00000000,
DCCG_FIFO_ERRDET_OVR_ENABLE              = 0x00000001,
} DCCG_FIFO_ERRDET_OVR_EN;

/*
 * DISPCLK_CHG_FWD_CORR_DISABLE enum
 */

typedef enum DISPCLK_CHG_FWD_CORR_DISABLE {
DISPCLK_CHG_FWD_CORR_ENABLE_AT_BEGINNING  = 0x00000000,
DISPCLK_CHG_FWD_CORR_DISABLE_AT_BEGINNING  = 0x00000001,
} DISPCLK_CHG_FWD_CORR_DISABLE;

/*
 * DC_MEM_GLOBAL_PWR_REQ_DIS enum
 */

typedef enum DC_MEM_GLOBAL_PWR_REQ_DIS {
DC_MEM_GLOBAL_PWR_REQ_ENABLE             = 0x00000000,
DC_MEM_GLOBAL_PWR_REQ_DISABLE            = 0x00000001,
} DC_MEM_GLOBAL_PWR_REQ_DIS;

/*
 * DCCG_PERF_RUN enum
 */

typedef enum DCCG_PERF_RUN {
DCCG_PERF_RUN_NOOP                       = 0x00000000,
DCCG_PERF_RUN_START                      = 0x00000001,
} DCCG_PERF_RUN;

/*
 * DCCG_PERF_MODE_VSYNC enum
 */

typedef enum DCCG_PERF_MODE_VSYNC {
DCCG_PERF_MODE_VSYNC_NOOP                = 0x00000000,
DCCG_PERF_MODE_VSYNC_START               = 0x00000001,
} DCCG_PERF_MODE_VSYNC;

/*
 * DCCG_PERF_MODE_HSYNC enum
 */

typedef enum DCCG_PERF_MODE_HSYNC {
DCCG_PERF_MODE_HSYNC_NOOP                = 0x00000000,
DCCG_PERF_MODE_HSYNC_START               = 0x00000001,
} DCCG_PERF_MODE_HSYNC;

/*
 * DCCG_PERF_CRTC_SELECT enum
 */

typedef enum DCCG_PERF_CRTC_SELECT {
DCCG_PERF_SEL_CRTC0                      = 0x00000000,
DCCG_PERF_SEL_CRTC1                      = 0x00000001,
DCCG_PERF_SEL_CRTC2                      = 0x00000002,
DCCG_PERF_SEL_CRTC3                      = 0x00000003,
DCCG_PERF_SEL_CRTC4                      = 0x00000004,
DCCG_PERF_SEL_CRTC5                      = 0x00000005,
} DCCG_PERF_CRTC_SELECT;

/*
 * CLOCK_BRANCH_SOFT_RESET enum
 */

typedef enum CLOCK_BRANCH_SOFT_RESET {
CLOCK_BRANCH_SOFT_RESET_NOOP             = 0x00000000,
CLOCK_BRANCH_SOFT_RESET_FORCE            = 0x00000001,
} CLOCK_BRANCH_SOFT_RESET;

/*
 * PLL_CFG_IF_SOFT_RESET enum
 */

typedef enum PLL_CFG_IF_SOFT_RESET {
PLL_CFG_IF_SOFT_RESET_NOOP               = 0x00000000,
PLL_CFG_IF_SOFT_RESET_FORCE              = 0x00000001,
} PLL_CFG_IF_SOFT_RESET;

/*
 * DVO_ENABLE_RST enum
 */

typedef enum DVO_ENABLE_RST {
DVO_ENABLE_RST_DISABLE                   = 0x00000000,
DVO_ENABLE_RST_ENABLE                    = 0x00000001,
} DVO_ENABLE_RST;

/*******************************************************
 * DCI Enums
 *******************************************************/

/*
 * LptNumPipes enum
 */

typedef enum LptNumPipes {
LPT_NUM_PIPES_1CH                        = 0x00000000,
LPT_NUM_PIPES_2CH                        = 0x00000001,
LPT_NUM_PIPES_4CH                        = 0x00000002,
LPT_NUM_PIPES_8CH                        = 0x00000003,
} LptNumPipes;

/*
 * LptNumBanks enum
 */

typedef enum LptNumBanks {
LPT_NUM_BANKS_2BANK                      = 0x00000000,
LPT_NUM_BANKS_4BANK                      = 0x00000001,
LPT_NUM_BANKS_8BANK                      = 0x00000002,
LPT_NUM_BANKS_16BANK                     = 0x00000003,
LPT_NUM_BANKS_32BANK                     = 0x00000004,
} LptNumBanks;

/*
 * OVERRIDE_CGTT_DCEFCLK enum
 */

typedef enum OVERRIDE_CGTT_DCEFCLK {
OVERRIDE_CGTT_DCEFCLK_NOOP               = 0x00000000,
SET_OVERRIDE_CGTT_DCEFCLK                = 0x00000001,
} OVERRIDE_CGTT_DCEFCLK;

/*******************************************************
 * DCIO Enums
 *******************************************************/

/*
 * DCIO_DC_GENERICA_SEL enum
 */

typedef enum DCIO_DC_GENERICA_SEL {
DCIO_GENERICA_SEL_DACA_STEREOSYNC        = 0x00000000,
DCIO_GENERICA_SEL_STEREOSYNC             = 0x00000001,
DCIO_GENERICA_SEL_DACA_PIXCLK            = 0x00000002,
DCIO_GENERICA_SEL_DACB_PIXCLK            = 0x00000003,
DCIO_GENERICA_SEL_DVOA_CTL3              = 0x00000004,
DCIO_GENERICA_SEL_P1_PLLCLK              = 0x00000005,
DCIO_GENERICA_SEL_P2_PLLCLK              = 0x00000006,
DCIO_GENERICA_SEL_DVOA_STEREOSYNC        = 0x00000007,
DCIO_GENERICA_SEL_DACA_FIELD_NUMBER      = 0x00000008,
DCIO_GENERICA_SEL_DACB_FIELD_NUMBER      = 0x00000009,
DCIO_GENERICA_SEL_GENERICA_DCCG          = 0x0000000a,
DCIO_GENERICA_SEL_SYNCEN                 = 0x0000000b,
DCIO_GENERICA_SEL_UNIPHY_REFDIV_CLK      = 0x0000000c,
DCIO_GENERICA_SEL_UNIPHY_FBDIV_CLK       = 0x0000000d,
DCIO_GENERICA_SEL_UNIPHY_FBDIV_SSC_CLK   = 0x0000000e,
DCIO_GENERICA_SEL_UNIPHY_FBDIV_CLK_DIV2  = 0x0000000f,
DCIO_GENERICA_SEL_GENERICA_DPRX          = 0x00000010,
DCIO_GENERICA_SEL_GENERICB_DPRX          = 0x00000011,
} DCIO_DC_GENERICA_SEL;

/*
 * DCIO_DC_GENERIC_UNIPHY_REFDIV_CLK_SEL enum
 */

typedef enum DCIO_DC_GENERIC_UNIPHY_REFDIV_CLK_SEL {
DCIO_UNIPHYA_TEST_REFDIV_CLK             = 0x00000000,
DCIO_UNIPHYB_TEST_REFDIV_CLK             = 0x00000001,
DCIO_UNIPHYC_TEST_REFDIV_CLK             = 0x00000002,
DCIO_UNIPHYD_TEST_REFDIV_CLK             = 0x00000003,
DCIO_UNIPHYE_TEST_REFDIV_CLK             = 0x00000004,
DCIO_UNIPHYF_TEST_REFDIV_CLK             = 0x00000005,
DCIO_UNIPHYG_TEST_REFDIV_CLK             = 0x00000006,
DCIO_UNIPHYLPA_TEST_REFDIV_CLK           = 0x00000007,
DCIO_UNIPHYLPB_TEST_REFDIV_CLK           = 0x00000008,
} DCIO_DC_GENERIC_UNIPHY_REFDIV_CLK_SEL;

/*
 * DCIO_DC_GENERIC_UNIPHY_FBDIV_CLK_SEL enum
 */

typedef enum DCIO_DC_GENERIC_UNIPHY_FBDIV_CLK_SEL {
DCIO_UNIPHYA_FBDIV_CLK                   = 0x00000000,
DCIO_UNIPHYB_FBDIV_CLK                   = 0x00000001,
DCIO_UNIPHYC_FBDIV_CLK                   = 0x00000002,
DCIO_UNIPHYD_FBDIV_CLK                   = 0x00000003,
DCIO_UNIPHYE_FBDIV_CLK                   = 0x00000004,
DCIO_UNIPHYF_FBDIV_CLK                   = 0x00000005,
DCIO_UNIPHYG_FBDIV_CLK                   = 0x00000006,
DCIO_UNIPHYLPA_FBDIV_CLK                 = 0x00000007,
DCIO_UNIPHYLPB_FBDIV_CLK                 = 0x00000008,
} DCIO_DC_GENERIC_UNIPHY_FBDIV_CLK_SEL;

/*
 * DCIO_DC_GENERIC_UNIPHY_FBDIV_SSC_CLK_SEL enum
 */

typedef enum DCIO_DC_GENERIC_UNIPHY_FBDIV_SSC_CLK_SEL {
DCIO_UNIPHYA_FBDIV_SSC_CLK               = 0x00000000,
DCIO_UNIPHYB_FBDIV_SSC_CLK               = 0x00000001,
DCIO_UNIPHYC_FBDIV_SSC_CLK               = 0x00000002,
DCIO_UNIPHYD_FBDIV_SSC_CLK               = 0x00000003,
DCIO_UNIPHYE_FBDIV_SSC_CLK               = 0x00000004,
DCIO_UNIPHYF_FBDIV_SSC_CLK               = 0x00000005,
DCIO_UNIPHYG_FBDIV_SSC_CLK               = 0x00000006,
DCIO_UNIPHYLPA_FBDIV_SSC_CLK             = 0x00000007,
DCIO_UNIPHYLPB_FBDIV_SSC_CLK             = 0x00000008,
} DCIO_DC_GENERIC_UNIPHY_FBDIV_SSC_CLK_SEL;

/*
 * DCIO_DC_GENERIC_UNIPHY_FBDIV_CLK_DIV2_SEL enum
 */

typedef enum DCIO_DC_GENERIC_UNIPHY_FBDIV_CLK_DIV2_SEL {
DCIO_UNIPHYA_TEST_FBDIV_CLK_DIV2         = 0x00000000,
DCIO_UNIPHYB_TEST_FBDIV_CLK_DIV2         = 0x00000001,
DCIO_UNIPHYC_TEST_FBDIV_CLK_DIV2         = 0x00000002,
DCIO_UNIPHYD_TEST_FBDIV_CLK_DIV2         = 0x00000003,
DCIO_UNIPHYE_TEST_FBDIV_CLK_DIV2         = 0x00000004,
DCIO_UNIPHYF_TEST_FBDIV_CLK_DIV2         = 0x00000005,
DCIO_UNIPHYG_TEST_FBDIV_CLK_DIV2         = 0x00000006,
DCIO_UNIPHYLPA_TEST_FBDIV_CLK_DIV2       = 0x00000007,
DCIO_UNIPHYLPB_TEST_FBDIV_CLK_DIV2       = 0x00000008,
} DCIO_DC_GENERIC_UNIPHY_FBDIV_CLK_DIV2_SEL;

/*
 * DCIO_DC_GENERICB_SEL enum
 */

typedef enum DCIO_DC_GENERICB_SEL {
DCIO_GENERICB_SEL_DACA_STEREOSYNC        = 0x00000000,
DCIO_GENERICB_SEL_STEREOSYNC             = 0x00000001,
DCIO_GENERICB_SEL_DACA_PIXCLK            = 0x00000002,
DCIO_GENERICB_SEL_DACB_PIXCLK            = 0x00000003,
DCIO_GENERICB_SEL_DVOA_CTL3              = 0x00000004,
DCIO_GENERICB_SEL_P1_PLLCLK              = 0x00000005,
DCIO_GENERICB_SEL_P2_PLLCLK              = 0x00000006,
DCIO_GENERICB_SEL_DVOA_STEREOSYNC        = 0x00000007,
DCIO_GENERICB_SEL_DACA_FIELD_NUMBER      = 0x00000008,
DCIO_GENERICB_SEL_DACB_FIELD_NUMBER      = 0x00000009,
DCIO_GENERICB_SEL_GENERICB_DCCG          = 0x0000000a,
DCIO_GENERICB_SEL_SYNCEN                 = 0x0000000b,
DCIO_GENERICB_SEL_UNIPHY_REFDIV_CLK      = 0x0000000c,
DCIO_GENERICB_SEL_UNIPHY_FBDIV_CLK       = 0x0000000d,
DCIO_GENERICB_SEL_UNIPHY_FBDIV_SSC_CLK   = 0x0000000e,
DCIO_GENERICB_SEL_UNIPHY_FBDIV_CLK_DIV2  = 0x0000000f,
} DCIO_DC_GENERICB_SEL;

/*
 * DCIO_DC_PAD_EXTERN_SIG_SEL enum
 */

typedef enum DCIO_DC_PAD_EXTERN_SIG_SEL {
DCIO_DC_PAD_EXTERN_SIG_SEL_MVP           = 0x00000000,
DCIO_DC_PAD_EXTERN_SIG_SEL_VSYNCA        = 0x00000001,
DCIO_DC_PAD_EXTERN_SIG_SEL_GENLK_CLK     = 0x00000002,
DCIO_DC_PAD_EXTERN_SIG_SEL_GENLK_VSYNC   = 0x00000003,
DCIO_DC_PAD_EXTERN_SIG_SEL_GENERICA      = 0x00000004,
DCIO_DC_PAD_EXTERN_SIG_SEL_GENERICB      = 0x00000005,
DCIO_DC_PAD_EXTERN_SIG_SEL_GENERICC      = 0x00000006,
DCIO_DC_PAD_EXTERN_SIG_SEL_HPD1          = 0x00000007,
DCIO_DC_PAD_EXTERN_SIG_SEL_HPD2          = 0x00000008,
DCIO_DC_PAD_EXTERN_SIG_SEL_DDC1CLK       = 0x00000009,
DCIO_DC_PAD_EXTERN_SIG_SEL_DDC1DATA      = 0x0000000a,
DCIO_DC_PAD_EXTERN_SIG_SEL_DDC2CLK       = 0x0000000b,
DCIO_DC_PAD_EXTERN_SIG_SEL_DDC2DATA      = 0x0000000c,
DCIO_DC_PAD_EXTERN_SIG_SEL_VHAD1         = 0x0000000d,
DCIO_DC_PAD_EXTERN_SIG_SEL_VHAD0         = 0x0000000e,
DCIO_DC_PAD_EXTERN_SIG_SEL_VPHCTL        = 0x0000000f,
} DCIO_DC_PAD_EXTERN_SIG_SEL;

/*
 * DCIO_DC_PAD_EXTERN_SIG_MVP_PIXEL_SRC_STATUS enum
 */

typedef enum DCIO_DC_PAD_EXTERN_SIG_MVP_PIXEL_SRC_STATUS {
DCIO_MVP_PIXEL_SRC_STATUS_HSYNCA         = 0x00000000,
DCIO_MVP_PIXEL_SRC_STATUS_HSYNCA_DUPLICATE  = 0x00000001,
DCIO_MVP_PIXEL_SRC_STATUS_CRTC           = 0x00000002,
DCIO_MVP_PIXEL_SRC_STATUS_LB             = 0x00000003,
} DCIO_DC_PAD_EXTERN_SIG_MVP_PIXEL_SRC_STATUS;

/*
 * DCIO_DC_REF_CLK_CNTL_HSYNCA_OUTPUT_SEL enum
 */

typedef enum DCIO_DC_REF_CLK_CNTL_HSYNCA_OUTPUT_SEL {
DCIO_HSYNCA_OUTPUT_SEL_DISABLE           = 0x00000000,
DCIO_HSYNCA_OUTPUT_SEL_PPLL1             = 0x00000001,
DCIO_HSYNCA_OUTPUT_SEL_PPLL2             = 0x00000002,
DCIO_HSYNCA_OUTPUT_SEL_RESERVED          = 0x00000003,
} DCIO_DC_REF_CLK_CNTL_HSYNCA_OUTPUT_SEL;

/*
 * DCIO_DC_REF_CLK_CNTL_GENLK_CLK_OUTPUT_SEL enum
 */

typedef enum DCIO_DC_REF_CLK_CNTL_GENLK_CLK_OUTPUT_SEL {
DCIO_GENLK_CLK_OUTPUT_SEL_DISABLE        = 0x00000000,
DCIO_GENLK_CLK_OUTPUT_SEL_PPLL1          = 0x00000001,
DCIO_GENLK_CLK_OUTPUT_SEL_PPLL2          = 0x00000002,
DCIO_GENLK_CLK_OUTPUT_SEL_RESERVED_VALUE3  = 0x00000003,
} DCIO_DC_REF_CLK_CNTL_GENLK_CLK_OUTPUT_SEL;

/*
 * DCIO_DC_GPIO_VIP_DEBUG enum
 */

typedef enum DCIO_DC_GPIO_VIP_DEBUG {
DCIO_DC_GPIO_VIP_DEBUG_NORMAL            = 0x00000000,
DCIO_DC_GPIO_VIP_DEBUG_CG_BIG            = 0x00000001,
} DCIO_DC_GPIO_VIP_DEBUG;

/*
 * DCIO_DC_GPIO_MACRO_DEBUG enum
 */

typedef enum DCIO_DC_GPIO_MACRO_DEBUG {
DCIO_DC_GPIO_MACRO_DEBUG_NORMAL          = 0x00000000,
DCIO_DC_GPIO_MACRO_DEBUG_CHIP_BIF        = 0x00000001,
DCIO_DC_GPIO_MACRO_DEBUG_RESERVED_VALUE2  = 0x00000002,
DCIO_DC_GPIO_MACRO_DEBUG_RESERVED_VALUE3  = 0x00000003,
} DCIO_DC_GPIO_MACRO_DEBUG;

/*
 * DCIO_DC_GPIO_CHIP_DEBUG_OUT_PIN_SEL enum
 */

typedef enum DCIO_DC_GPIO_CHIP_DEBUG_OUT_PIN_SEL {
DCIO_DC_GPIO_CHIP_DEBUG_OUT_PIN_SEL_NORMAL  = 0x00000000,
DCIO_DC_GPIO_CHIP_DEBUG_OUT_PIN_SEL_SWAP  = 0x00000001,
} DCIO_DC_GPIO_CHIP_DEBUG_OUT_PIN_SEL;

/*
 * DCIO_DC_GPIO_DEBUG_BUS_FLOP_EN enum
 */

typedef enum DCIO_DC_GPIO_DEBUG_BUS_FLOP_EN {
DCIO_DC_GPIO_DEBUG_BUS_FLOP_EN_BYPASS    = 0x00000000,
DCIO_DC_GPIO_DEBUG_BUS_FLOP_EN_ENABLE    = 0x00000001,
} DCIO_DC_GPIO_DEBUG_BUS_FLOP_EN;

/*
 * DCIO_DC_GPIO_DEBUG_DPRX_LOOPBACK_ENABLE enum
 */

typedef enum DCIO_DC_GPIO_DEBUG_DPRX_LOOPBACK_ENABLE {
DCIO_DPRX_LOOPBACK_ENABLE_NORMAL         = 0x00000000,
DCIO_DPRX_LOOPBACK_ENABLE_LOOP           = 0x00000001,
} DCIO_DC_GPIO_DEBUG_DPRX_LOOPBACK_ENABLE;

/*
 * DCIO_UNIPHY_LINK_CNTL_MINIMUM_PIXVLD_LOW_DURATION enum
 */

typedef enum DCIO_UNIPHY_LINK_CNTL_MINIMUM_PIXVLD_LOW_DURATION {
DCIO_UNIPHY_MINIMUM_PIXVLD_LOW_DURATION_3_CLOCKS = 0x00000000,
DCIO_UNIPHY_MINIMUM_PIXVLD_LOW_DURATION_7_CLOCKS = 0x00000001,
DCIO_UNIPHY_MINIMUM_PIXVLD_LOW_DURATION_11_CLOCKS = 0x00000002,
DCIO_UNIPHY_MINIMUM_PIXVLD_LOW_DURATION_15_CLOCKS = 0x00000003,
DCIO_UNIPHY_MINIMUM_PIXVLD_LOW_DURATION_19_CLOCKS = 0x00000004,
DCIO_UNIPHY_MINIMUM_PIXVLD_LOW_DURATION_23_CLOCKS = 0x00000005,
DCIO_UNIPHY_MINIMUM_PIXVLD_LOW_DURATION_27_CLOCKS = 0x00000006,
DCIO_UNIPHY_MINIMUM_PIXVLD_LOW_DURATION_31_CLOCKS = 0x00000007,
} DCIO_UNIPHY_LINK_CNTL_MINIMUM_PIXVLD_LOW_DURATION;

/*
 * DCIO_UNIPHY_LINK_CNTL_CHANNEL_INVERT enum
 */

typedef enum DCIO_UNIPHY_LINK_CNTL_CHANNEL_INVERT {
DCIO_UNIPHY_CHANNEL_NO_INVERSION         = 0x00000000,
DCIO_UNIPHY_CHANNEL_INVERTED             = 0x00000001,
} DCIO_UNIPHY_LINK_CNTL_CHANNEL_INVERT;

/*
 * DCIO_UNIPHY_LINK_CNTL_ENABLE_HPD_MASK enum
 */

typedef enum DCIO_UNIPHY_LINK_CNTL_ENABLE_HPD_MASK {
DCIO_UNIPHY_LINK_ENABLE_HPD_MASK_DISALLOW  = 0x00000000,
DCIO_UNIPHY_LINK_ENABLE_HPD_MASK_ALLOW   = 0x00000001,
DCIO_UNIPHY_LINK_ENABLE_HPD_MASK_ALLOW_DEBOUNCED  = 0x00000002,
DCIO_UNIPHY_LINK_ENABLE_HPD_MASK_ALLOW_TOGGLE_FILTERED  = 0x00000003,
} DCIO_UNIPHY_LINK_CNTL_ENABLE_HPD_MASK;

/*
 * DCIO_UNIPHY_CHANNEL_XBAR_SOURCE enum
 */

typedef enum DCIO_UNIPHY_CHANNEL_XBAR_SOURCE {
DCIO_UNIPHY_CHANNEL_XBAR_SOURCE_CH0      = 0x00000000,
DCIO_UNIPHY_CHANNEL_XBAR_SOURCE_CH1      = 0x00000001,
DCIO_UNIPHY_CHANNEL_XBAR_SOURCE_CH2      = 0x00000002,
DCIO_UNIPHY_CHANNEL_XBAR_SOURCE_CH3      = 0x00000003,
} DCIO_UNIPHY_CHANNEL_XBAR_SOURCE;

/*
 * DCIO_DC_DVODATA_CONFIG_VIP_MUX_EN enum
 */

typedef enum DCIO_DC_DVODATA_CONFIG_VIP_MUX_EN {
DCIO_VIP_MUX_EN_DVO                      = 0x00000000,
DCIO_VIP_MUX_EN_VIP                      = 0x00000001,
} DCIO_DC_DVODATA_CONFIG_VIP_MUX_EN;

/*
 * DCIO_DC_DVODATA_CONFIG_VIP_ALTER_MAPPING_EN enum
 */

typedef enum DCIO_DC_DVODATA_CONFIG_VIP_ALTER_MAPPING_EN {
DCIO_VIP_ALTER_MAPPING_EN_DEFAULT        = 0x00000000,
DCIO_VIP_ALTER_MAPPING_EN_ALTERNATIVE    = 0x00000001,
} DCIO_DC_DVODATA_CONFIG_VIP_ALTER_MAPPING_EN;

/*
 * DCIO_DC_DVODATA_CONFIG_DVO_ALTER_MAPPING_EN enum
 */

typedef enum DCIO_DC_DVODATA_CONFIG_DVO_ALTER_MAPPING_EN {
DCIO_DVO_ALTER_MAPPING_EN_DEFAULT        = 0x00000000,
DCIO_DVO_ALTER_MAPPING_EN_ALTERNATIVE    = 0x00000001,
} DCIO_DC_DVODATA_CONFIG_DVO_ALTER_MAPPING_EN;

/*
 * DCIO_LVTMA_PWRSEQ_CNTL_DISABLE_SYNCEN_CONTROL_OF_TX_EN enum
 */

typedef enum DCIO_LVTMA_PWRSEQ_CNTL_DISABLE_SYNCEN_CONTROL_OF_TX_EN {
DCIO_LVTMA_PWRSEQ_DISABLE_SYNCEN_CONTROL_OF_TX_ENABLE  = 0x00000000,
DCIO_LVTMA_PWRSEQ_DISABLE_SYNCEN_CONTROL_OF_TX_DISABLE  = 0x00000001,
} DCIO_LVTMA_PWRSEQ_CNTL_DISABLE_SYNCEN_CONTROL_OF_TX_EN;

/*
 * DCIO_LVTMA_PWRSEQ_CNTL_TARGET_STATE enum
 */

typedef enum DCIO_LVTMA_PWRSEQ_CNTL_TARGET_STATE {
DCIO_LVTMA_PWRSEQ_TARGET_STATE_LCD_OFF   = 0x00000000,
DCIO_LVTMA_PWRSEQ_TARGET_STATE_LCD_ON    = 0x00000001,
} DCIO_LVTMA_PWRSEQ_CNTL_TARGET_STATE;

/*
 * DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_SYNCEN_POL enum
 */

typedef enum DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_SYNCEN_POL {
DCIO_LVTMA_SYNCEN_POL_NON_INVERT         = 0x00000000,
DCIO_LVTMA_SYNCEN_POL_INVERT             = 0x00000001,
} DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_SYNCEN_POL;

/*
 * DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_DIGON enum
 */

typedef enum DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_DIGON {
DCIO_LVTMA_DIGON_OFF                     = 0x00000000,
DCIO_LVTMA_DIGON_ON                      = 0x00000001,
} DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_DIGON;

/*
 * DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_DIGON_POL enum
 */

typedef enum DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_DIGON_POL {
DCIO_LVTMA_DIGON_POL_NON_INVERT          = 0x00000000,
DCIO_LVTMA_DIGON_POL_INVERT              = 0x00000001,
} DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_DIGON_POL;

/*
 * DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_BLON enum
 */

typedef enum DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_BLON {
DCIO_LVTMA_BLON_OFF                      = 0x00000000,
DCIO_LVTMA_BLON_ON                       = 0x00000001,
} DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_BLON;

/*
 * DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_BLON_POL enum
 */

typedef enum DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_BLON_POL {
DCIO_LVTMA_BLON_POL_NON_INVERT           = 0x00000000,
DCIO_LVTMA_BLON_POL_INVERT               = 0x00000001,
} DCIO_LVTMA_PWRSEQ_CNTL_LVTMA_BLON_POL;

/*
 * DCIO_LVTMA_PWRSEQ_DELAY2_LVTMA_VARY_BL_OVERRIDE_EN enum
 */

typedef enum DCIO_LVTMA_PWRSEQ_DELAY2_LVTMA_VARY_BL_OVERRIDE_EN {
DCIO_LVTMA_VARY_BL_OVERRIDE_EN_BLON      = 0x00000000,
DCIO_LVTMA_VARY_BL_OVERRIDE_EN_SEPARATE  = 0x00000001,
} DCIO_LVTMA_PWRSEQ_DELAY2_LVTMA_VARY_BL_OVERRIDE_EN;

/*
 * DCIO_BL_PWM_CNTL_BL_PWM_FRACTIONAL_EN enum
 */

typedef enum DCIO_BL_PWM_CNTL_BL_PWM_FRACTIONAL_EN {
DCIO_BL_PWM_FRACTIONAL_DISABLE           = 0x00000000,
DCIO_BL_PWM_FRACTIONAL_ENABLE            = 0x00000001,
} DCIO_BL_PWM_CNTL_BL_PWM_FRACTIONAL_EN;

/*
 * DCIO_BL_PWM_CNTL_BL_PWM_EN enum
 */

typedef enum DCIO_BL_PWM_CNTL_BL_PWM_EN {
DCIO_BL_PWM_DISABLE                      = 0x00000000,
DCIO_BL_PWM_ENABLE                       = 0x00000001,
} DCIO_BL_PWM_CNTL_BL_PWM_EN;

/*
 * DCIO_BL_PWM_CNTL2_DBG_BL_PWM_INPUT_REFCLK_SELECT enum
 */

typedef enum DCIO_BL_PWM_CNTL2_DBG_BL_PWM_INPUT_REFCLK_SELECT {
DCIO_DBG_BL_PWM_INPUT_REFCLK_SELECT_NORMAL  = 0x00000000,
DCIO_DBG_BL_PWM_INPUT_REFCLK_SELECT_DEBUG1  = 0x00000001,
DCIO_DBG_BL_PWM_INPUT_REFCLK_SELECT_DEBUG2  = 0x00000002,
DCIO_DBG_BL_PWM_INPUT_REFCLK_SELECT_DEBUG3  = 0x00000003,
} DCIO_BL_PWM_CNTL2_DBG_BL_PWM_INPUT_REFCLK_SELECT;

/*
 * DCIO_BL_PWM_CNTL2_BL_PWM_OVERRIDE_BL_OUT_ENABLE enum
 */

typedef enum DCIO_BL_PWM_CNTL2_BL_PWM_OVERRIDE_BL_OUT_ENABLE {
DCIO_BL_PWM_OVERRIDE_BL_OUT_DISABLE      = 0x00000000,
DCIO_BL_PWM_OVERRIDE_BL_OUT_ENABLE       = 0x00000001,
} DCIO_BL_PWM_CNTL2_BL_PWM_OVERRIDE_BL_OUT_ENABLE;

/*
 * DCIO_BL_PWM_CNTL2_BL_PWM_OVERRIDE_LVTMA_PWRSEQ_EN enum
 */

typedef enum DCIO_BL_PWM_CNTL2_BL_PWM_OVERRIDE_LVTMA_PWRSEQ_EN {
DCIO_BL_PWM_OVERRIDE_LVTMA_PWRSEQ_EN_NORMAL  = 0x00000000,
DCIO_BL_PWM_OVERRIDE_LVTMA_PWRSEQ_EN_PWM  = 0x00000001,
} DCIO_BL_PWM_CNTL2_BL_PWM_OVERRIDE_LVTMA_PWRSEQ_EN;

/*
 * DCIO_BL_PWM_GRP1_REG_LOCK enum
 */

typedef enum DCIO_BL_PWM_GRP1_REG_LOCK {
DCIO_BL_PWM_GRP1_REG_LOCK_DISABLE        = 0x00000000,
DCIO_BL_PWM_GRP1_REG_LOCK_ENABLE         = 0x00000001,
} DCIO_BL_PWM_GRP1_REG_LOCK;

/*
 * DCIO_BL_PWM_GRP1_UPDATE_AT_FRAME_START enum
 */

typedef enum DCIO_BL_PWM_GRP1_UPDATE_AT_FRAME_START {
DCIO_BL_PWM_GRP1_UPDATE_AT_FRAME_START_DISABLE  = 0x00000000,
DCIO_BL_PWM_GRP1_UPDATE_AT_FRAME_START_ENABLE  = 0x00000001,
} DCIO_BL_PWM_GRP1_UPDATE_AT_FRAME_START;

/*
 * DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL enum
 */

typedef enum DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL {
DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL_CONTROLLER1  = 0x00000000,
DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL_CONTROLLER2  = 0x00000001,
DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL_CONTROLLER3  = 0x00000002,
DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL_CONTROLLER4  = 0x00000003,
DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL_CONTROLLER5  = 0x00000004,
DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL_CONTROLLER6  = 0x00000005,
} DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL;

/*
 * DCIO_BL_PWM_GRP1_READBACK_DB_REG_VALUE_EN enum
 */

typedef enum DCIO_BL_PWM_GRP1_READBACK_DB_REG_VALUE_EN {
DCIO_BL_PWM_GRP1_READBACK_DB_REG_VALUE_EN_BL_PWM  = 0x00000000,
DCIO_BL_PWM_GRP1_READBACK_DB_REG_VALUE_EN_BL1_PWM  = 0x00000001,
} DCIO_BL_PWM_GRP1_READBACK_DB_REG_VALUE_EN;

/*
 * DCIO_BL_PWM_GRP1_IGNORE_MASTER_LOCK_EN enum
 */

typedef enum DCIO_BL_PWM_GRP1_IGNORE_MASTER_LOCK_EN {
DCIO_BL_PWM_GRP1_IGNORE_MASTER_LOCK_ENABLE  = 0x00000000,
DCIO_BL_PWM_GRP1_IGNORE_MASTER_LOCK_DISABLE  = 0x00000001,
} DCIO_BL_PWM_GRP1_IGNORE_MASTER_LOCK_EN;

/*
 * DCIO_GSL_SEL enum
 */

typedef enum DCIO_GSL_SEL {
DCIO_GSL_SEL_GROUP_0                     = 0x00000000,
DCIO_GSL_SEL_GROUP_1                     = 0x00000001,
DCIO_GSL_SEL_GROUP_2                     = 0x00000002,
} DCIO_GSL_SEL;

/*
 * DCIO_GENLK_CLK_GSL_MASK enum
 */

typedef enum DCIO_GENLK_CLK_GSL_MASK {
DCIO_GENLK_CLK_GSL_MASK_NO               = 0x00000000,
DCIO_GENLK_CLK_GSL_MASK_TIMING           = 0x00000001,
DCIO_GENLK_CLK_GSL_MASK_STEREO           = 0x00000002,
} DCIO_GENLK_CLK_GSL_MASK;

/*
 * DCIO_GENLK_VSYNC_GSL_MASK enum
 */

typedef enum DCIO_GENLK_VSYNC_GSL_MASK {
DCIO_GENLK_VSYNC_GSL_MASK_NO             = 0x00000000,
DCIO_GENLK_VSYNC_GSL_MASK_TIMING         = 0x00000001,
DCIO_GENLK_VSYNC_GSL_MASK_STEREO         = 0x00000002,
} DCIO_GENLK_VSYNC_GSL_MASK;

/*
 * DCIO_SWAPLOCK_A_GSL_MASK enum
 */

typedef enum DCIO_SWAPLOCK_A_GSL_MASK {
DCIO_SWAPLOCK_A_GSL_MASK_NO              = 0x00000000,
DCIO_SWAPLOCK_A_GSL_MASK_TIMING          = 0x00000001,
DCIO_SWAPLOCK_A_GSL_MASK_STEREO          = 0x00000002,
} DCIO_SWAPLOCK_A_GSL_MASK;

/*
 * DCIO_SWAPLOCK_B_GSL_MASK enum
 */

typedef enum DCIO_SWAPLOCK_B_GSL_MASK {
DCIO_SWAPLOCK_B_GSL_MASK_NO              = 0x00000000,
DCIO_SWAPLOCK_B_GSL_MASK_TIMING          = 0x00000001,
DCIO_SWAPLOCK_B_GSL_MASK_STEREO          = 0x00000002,
} DCIO_SWAPLOCK_B_GSL_MASK;

/*
 * DCIO_GSL_VSYNC_SEL enum
 */

typedef enum DCIO_GSL_VSYNC_SEL {
DCIO_GSL_VSYNC_SEL_PIPE0                 = 0x00000000,
DCIO_GSL_VSYNC_SEL_PIPE1                 = 0x00000001,
DCIO_GSL_VSYNC_SEL_PIPE2                 = 0x00000002,
DCIO_GSL_VSYNC_SEL_PIPE3                 = 0x00000003,
DCIO_GSL_VSYNC_SEL_PIPE4                 = 0x00000004,
DCIO_GSL_VSYNC_SEL_PIPE5                 = 0x00000005,
} DCIO_GSL_VSYNC_SEL;

/*
 * DCIO_GSL0_TIMING_SYNC_SEL enum
 */

typedef enum DCIO_GSL0_TIMING_SYNC_SEL {
DCIO_GSL0_TIMING_SYNC_SEL_PIPE           = 0x00000000,
DCIO_GSL0_TIMING_SYNC_SEL_GENCLK_VSYNC   = 0x00000001,
DCIO_GSL0_TIMING_SYNC_SEL_GENCLK_CLK     = 0x00000002,
DCIO_GSL0_TIMING_SYNC_SEL_SWAPLOCK_A     = 0x00000003,
DCIO_GSL0_TIMING_SYNC_SEL_SWAPLOCK_B     = 0x00000004,
} DCIO_GSL0_TIMING_SYNC_SEL;

/*
 * DCIO_GSL0_GLOBAL_UNLOCK_SEL enum
 */

typedef enum DCIO_GSL0_GLOBAL_UNLOCK_SEL {
DCIO_GSL0_GLOBAL_UNLOCK_SEL_INVERSION    = 0x00000000,
DCIO_GSL0_GLOBAL_UNLOCK_SEL_GENCLK_VSYNC  = 0x00000001,
DCIO_GSL0_GLOBAL_UNLOCK_SEL_GENLK_CLK    = 0x00000002,
DCIO_GSL0_GLOBAL_UNLOCK_SEL_SWAPLOCK_A   = 0x00000003,
DCIO_GSL0_GLOBAL_UNLOCK_SEL_SWAPLOCK_B   = 0x00000004,
} DCIO_GSL0_GLOBAL_UNLOCK_SEL;

/*
 * DCIO_GSL1_TIMING_SYNC_SEL enum
 */

typedef enum DCIO_GSL1_TIMING_SYNC_SEL {
DCIO_GSL1_TIMING_SYNC_SEL_PIPE           = 0x00000000,
DCIO_GSL1_TIMING_SYNC_SEL_GENCLK_VSYNC   = 0x00000001,
DCIO_GSL1_TIMING_SYNC_SEL_GENCLK_CLK     = 0x00000002,
DCIO_GSL1_TIMING_SYNC_SEL_SWAPLOCK_A     = 0x00000003,
DCIO_GSL1_TIMING_SYNC_SEL_SWAPLOCK_B     = 0x00000004,
} DCIO_GSL1_TIMING_SYNC_SEL;

/*
 * DCIO_GSL1_GLOBAL_UNLOCK_SEL enum
 */

typedef enum DCIO_GSL1_GLOBAL_UNLOCK_SEL {
DCIO_GSL1_GLOBAL_UNLOCK_SEL_INVERSION    = 0x00000000,
DCIO_GSL1_GLOBAL_UNLOCK_SEL_GENCLK_VSYNC  = 0x00000001,
DCIO_GSL1_GLOBAL_UNLOCK_SEL_GENLK_CLK    = 0x00000002,
DCIO_GSL1_GLOBAL_UNLOCK_SEL_SWAPLOCK_A   = 0x00000003,
DCIO_GSL1_GLOBAL_UNLOCK_SEL_SWAPLOCK_B   = 0x00000004,
} DCIO_GSL1_GLOBAL_UNLOCK_SEL;

/*
 * DCIO_GSL2_TIMING_SYNC_SEL enum
 */

typedef enum DCIO_GSL2_TIMING_SYNC_SEL {
DCIO_GSL2_TIMING_SYNC_SEL_PIPE           = 0x00000000,
DCIO_GSL2_TIMING_SYNC_SEL_GENCLK_VSYNC   = 0x00000001,
DCIO_GSL2_TIMING_SYNC_SEL_GENCLK_CLK     = 0x00000002,
DCIO_GSL2_TIMING_SYNC_SEL_SWAPLOCK_A     = 0x00000003,
DCIO_GSL2_TIMING_SYNC_SEL_SWAPLOCK_B     = 0x00000004,
} DCIO_GSL2_TIMING_SYNC_SEL;

/*
 * DCIO_GSL2_GLOBAL_UNLOCK_SEL enum
 */

typedef enum DCIO_GSL2_GLOBAL_UNLOCK_SEL {
DCIO_GSL2_GLOBAL_UNLOCK_SEL_INVERSION    = 0x00000000,
DCIO_GSL2_GLOBAL_UNLOCK_SEL_GENCLK_VSYNC  = 0x00000001,
DCIO_GSL2_GLOBAL_UNLOCK_SEL_GENLK_CLK    = 0x00000002,
DCIO_GSL2_GLOBAL_UNLOCK_SEL_SWAPLOCK_A   = 0x00000003,
DCIO_GSL2_GLOBAL_UNLOCK_SEL_SWAPLOCK_B   = 0x00000004,
} DCIO_GSL2_GLOBAL_UNLOCK_SEL;

/*
 * DCIO_DC_GPU_TIMER_START_POSITION enum
 */

typedef enum DCIO_DC_GPU_TIMER_START_POSITION {
DCIO_GPU_TIMER_START_0_END_27            = 0x00000000,
DCIO_GPU_TIMER_START_1_END_28            = 0x00000001,
DCIO_GPU_TIMER_START_2_END_29            = 0x00000002,
DCIO_GPU_TIMER_START_3_END_30            = 0x00000003,
DCIO_GPU_TIMER_START_4_END_31            = 0x00000004,
DCIO_GPU_TIMER_START_6_END_33            = 0x00000005,
DCIO_GPU_TIMER_START_8_END_35            = 0x00000006,
DCIO_GPU_TIMER_START_10_END_37           = 0x00000007,
} DCIO_DC_GPU_TIMER_START_POSITION;

/*
 * DCIO_CLOCK_CNTL_DCIO_TEST_CLK_SEL enum
 */

typedef enum DCIO_CLOCK_CNTL_DCIO_TEST_CLK_SEL {
DCIO_TEST_CLK_SEL_DISPCLK                = 0x00000000,
DCIO_TEST_CLK_SEL_GATED_DISPCLK          = 0x00000001,
DCIO_TEST_CLK_SEL_SCLK                   = 0x00000002,
} DCIO_CLOCK_CNTL_DCIO_TEST_CLK_SEL;

/*
 * DCIO_CLOCK_CNTL_DISPCLK_R_DCIO_GATE_DIS enum
 */

typedef enum DCIO_CLOCK_CNTL_DISPCLK_R_DCIO_GATE_DIS {
DCIO_DISPCLK_R_DCIO_GATE_DISABLE         = 0x00000000,
DCIO_DISPCLK_R_DCIO_GATE_ENABLE          = 0x00000001,
} DCIO_CLOCK_CNTL_DISPCLK_R_DCIO_GATE_DIS;

/*
 * DCIO_DCO_DCFE_EXT_VSYNC_MUX enum
 */

typedef enum DCIO_DCO_DCFE_EXT_VSYNC_MUX {
DCIO_EXT_VSYNC_MUX_SWAPLOCKB             = 0x00000000,
DCIO_EXT_VSYNC_MUX_CRTC0                 = 0x00000001,
DCIO_EXT_VSYNC_MUX_CRTC1                 = 0x00000002,
DCIO_EXT_VSYNC_MUX_CRTC2                 = 0x00000003,
DCIO_EXT_VSYNC_MUX_CRTC3                 = 0x00000004,
DCIO_EXT_VSYNC_MUX_CRTC4                 = 0x00000005,
DCIO_EXT_VSYNC_MUX_CRTC5                 = 0x00000006,
DCIO_EXT_VSYNC_MUX_GENERICB              = 0x00000007,
} DCIO_DCO_DCFE_EXT_VSYNC_MUX;

/*
 * DCIO_DCO_EXT_VSYNC_MASK enum
 */

typedef enum DCIO_DCO_EXT_VSYNC_MASK {
DCIO_EXT_VSYNC_MASK_NONE                 = 0x00000000,
DCIO_EXT_VSYNC_MASK_PIPE0                = 0x00000001,
DCIO_EXT_VSYNC_MASK_PIPE1                = 0x00000002,
DCIO_EXT_VSYNC_MASK_PIPE2                = 0x00000003,
DCIO_EXT_VSYNC_MASK_PIPE3                = 0x00000004,
DCIO_EXT_VSYNC_MASK_PIPE4                = 0x00000005,
DCIO_EXT_VSYNC_MASK_PIPE5                = 0x00000006,
DCIO_EXT_VSYNC_MASK_NONE_DUPLICATE       = 0x00000007,
} DCIO_DCO_EXT_VSYNC_MASK;

/*
 * DCIO_DSYNC_SOFT_RESET enum
 */

typedef enum DCIO_DSYNC_SOFT_RESET {
DCIO_DSYNC_SOFT_RESET_DEASSERT           = 0x00000000,
DCIO_DSYNC_SOFT_RESET_ASSERT             = 0x00000001,
} DCIO_DSYNC_SOFT_RESET;

/*
 * DCIO_DACA_SOFT_RESET enum
 */

typedef enum DCIO_DACA_SOFT_RESET {
DCIO_DACA_SOFT_RESET_DEASSERT            = 0x00000000,
DCIO_DACA_SOFT_RESET_ASSERT              = 0x00000001,
} DCIO_DACA_SOFT_RESET;

/*
 * DCIO_DCRXPHY_SOFT_RESET enum
 */

typedef enum DCIO_DCRXPHY_SOFT_RESET {
DCIO_DCRXPHY_SOFT_RESET_DEASSERT         = 0x00000000,
DCIO_DCRXPHY_SOFT_RESET_ASSERT           = 0x00000001,
} DCIO_DCRXPHY_SOFT_RESET;

/*
 * DCIO_DPHY_LANE_SEL enum
 */

typedef enum DCIO_DPHY_LANE_SEL {
DCIO_DPHY_LANE_SEL_LANE0                 = 0x00000000,
DCIO_DPHY_LANE_SEL_LANE1                 = 0x00000001,
DCIO_DPHY_LANE_SEL_LANE2                 = 0x00000002,
DCIO_DPHY_LANE_SEL_LANE3                 = 0x00000003,
} DCIO_DPHY_LANE_SEL;

/*
 * DCIO_DPCS_INTERRUPT_TYPE enum
 */

typedef enum DCIO_DPCS_INTERRUPT_TYPE {
DCIO_DPCS_INTERRUPT_TYPE_LEVEL_BASED     = 0x00000000,
DCIO_DPCS_INTERRUPT_TYPE_PULSE_BASED     = 0x00000001,
} DCIO_DPCS_INTERRUPT_TYPE;

/*
 * DCIO_DPCS_INTERRUPT_MASK enum
 */

typedef enum DCIO_DPCS_INTERRUPT_MASK {
DCIO_DPCS_INTERRUPT_DISABLE              = 0x00000000,
DCIO_DPCS_INTERRUPT_ENABLE               = 0x00000001,
} DCIO_DPCS_INTERRUPT_MASK;

/*
 * DCIO_DC_GPU_TIMER_READ_SELECT enum
 */

typedef enum DCIO_DC_GPU_TIMER_READ_SELECT {
DCIO_GPU_TIMER_READ_SELECT_LOWER_D1_V_UPDATE  = 0x00000000,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D1_V_UPDATE  = 0x00000001,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D2_V_UPDATE  = 0x00000002,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D2_V_UPDATE  = 0x00000003,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D3_V_UPDATE  = 0x00000004,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D3_V_UPDATE  = 0x00000005,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D4_V_UPDATE  = 0x00000006,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D4_V_UPDATE  = 0x00000007,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D5_V_UPDATE  = 0x00000008,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D5_V_UPDATE  = 0x00000009,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D6_V_UPDATE  = 0x0000000a,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D6_V_UPDATE  = 0x0000000b,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D1_P_FLIP  = 0x0000000c,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D1_P_FLIP  = 0x0000000d,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D2_P_FLIP  = 0x0000000e,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D2_P_FLIP  = 0x0000000f,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D3_P_FLIP  = 0x00000010,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D3_P_FLIP  = 0x00000011,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D4_P_FLIP  = 0x00000012,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D4_P_FLIP  = 0x00000013,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D5_P_FLIP  = 0x00000014,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D5_P_FLIP  = 0x00000015,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D6_P_FLIP  = 0x00000016,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D6_P_FLIP  = 0x00000017,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D1_VSYNC_NOM  = 0x00000018,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D1_VSYNC_NOM  = 0x00000019,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D2_VSYNC_NOM  = 0x0000001a,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D2_VSYNC_NOM  = 0x0000001b,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D3_VSYNC_NOM  = 0x0000001c,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D3_VSYNC_NOM  = 0x0000001d,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D4_VSYNC_NOM  = 0x0000001e,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D4_VSYNC_NOM  = 0x0000001f,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D5_VSYNC_NOM  = 0x00000020,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D5_VSYNC_NOM  = 0x00000021,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D6_VSYNC_NOM  = 0x00000022,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D6_VSYNC_NOM  = 0x00000023,
} DCIO_DC_GPU_TIMER_READ_SELECT;

/*
 * DCIO_IMPCAL_STEP_DELAY enum
 */

typedef enum DCIO_IMPCAL_STEP_DELAY {
DCIO_IMPCAL_STEP_DELAY_1us               = 0x00000000,
DCIO_IMPCAL_STEP_DELAY_2us               = 0x00000001,
DCIO_IMPCAL_STEP_DELAY_3us               = 0x00000002,
DCIO_IMPCAL_STEP_DELAY_4us               = 0x00000003,
DCIO_IMPCAL_STEP_DELAY_5us               = 0x00000004,
DCIO_IMPCAL_STEP_DELAY_6us               = 0x00000005,
DCIO_IMPCAL_STEP_DELAY_7us               = 0x00000006,
DCIO_IMPCAL_STEP_DELAY_8us               = 0x00000007,
DCIO_IMPCAL_STEP_DELAY_9us               = 0x00000008,
DCIO_IMPCAL_STEP_DELAY_10us              = 0x00000009,
DCIO_IMPCAL_STEP_DELAY_11us              = 0x0000000a,
DCIO_IMPCAL_STEP_DELAY_12us              = 0x0000000b,
DCIO_IMPCAL_STEP_DELAY_13us              = 0x0000000c,
DCIO_IMPCAL_STEP_DELAY_14us              = 0x0000000d,
DCIO_IMPCAL_STEP_DELAY_15us              = 0x0000000e,
DCIO_IMPCAL_STEP_DELAY_16us              = 0x0000000f,
} DCIO_IMPCAL_STEP_DELAY;

/*
 * DCIO_UNIPHY_IMPCAL_SEL enum
 */

typedef enum DCIO_UNIPHY_IMPCAL_SEL {
DCIO_UNIPHY_IMPCAL_SEL_TEMPERATURE       = 0x00000000,
DCIO_UNIPHY_IMPCAL_SEL_BINARY            = 0x00000001,
} DCIO_UNIPHY_IMPCAL_SEL;

/*
 * DCIO_DBG_ASYNC_BLOCK_SEL enum
 */

typedef enum DCIO_DBG_ASYNC_BLOCK_SEL {
DCIO_DBG_ASYNC_BLOCK_SEL_OVERRIDE        = 0x00000000,
DCIO_DBG_ASYNC_BLOCK_SEL_DCCG            = 0x00000001,
DCIO_DBG_ASYNC_BLOCK_SEL_DCIO            = 0x00000002,
DCIO_DBG_ASYNC_BLOCK_SEL_DCO             = 0x00000003,
} DCIO_DBG_ASYNC_BLOCK_SEL;

/*
 * DCIO_DBG_ASYNC_4BIT_SEL enum
 */

typedef enum DCIO_DBG_ASYNC_4BIT_SEL {
DCIO_DBG_ASYNC_4BIT_SEL_3TO0             = 0x00000000,
DCIO_DBG_ASYNC_4BIT_SEL_7TO4             = 0x00000001,
DCIO_DBG_ASYNC_4BIT_SEL_11TO8            = 0x00000002,
DCIO_DBG_ASYNC_4BIT_SEL_15TO12           = 0x00000003,
DCIO_DBG_ASYNC_4BIT_SEL_19TO16           = 0x00000004,
DCIO_DBG_ASYNC_4BIT_SEL_23TO20           = 0x00000005,
DCIO_DBG_ASYNC_4BIT_SEL_27TO24           = 0x00000006,
DCIO_DBG_ASYNC_4BIT_SEL_31TO28           = 0x00000007,
} DCIO_DBG_ASYNC_4BIT_SEL;

/*******************************************************
 * AOUT Enums
 *******************************************************/

/*
 * AOUT_EN enum
 */

typedef enum AOUT_EN {
AOUT_DISABLE                             = 0x00000000,
AOUT_ENABLE                              = 0x00000001,
} AOUT_EN;

/*
 * AOUT_FIFO_START_ADDR enum
 */

typedef enum AOUT_FIFO_START_ADDR {
AOUT_FIFO_START_ADDR_2                   = 0x00000000,
AOUT_FIFO_START_ADDR_3                   = 0x00000001,
} AOUT_FIFO_START_ADDR;

/*
 * AOUT_CRC_TEST_EN enum
 */

typedef enum AOUT_CRC_TEST_EN {
AOUT_CRC_DISABLE                         = 0x00000000,
AOUT_CRC_ENABLE                          = 0x00000001,
} AOUT_CRC_TEST_EN;

/*
 * AOUT_CRC_SOFT_RESET enum
 */

typedef enum AOUT_CRC_SOFT_RESET {
AOUT_CRC_NO_RESET                        = 0x00000000,
AOUT_CRC_RESET                           = 0x00000001,
} AOUT_CRC_SOFT_RESET;

/*
 * AOUT_CRC_CONT_EN enum
 */

typedef enum AOUT_CRC_CONT_EN {
AOUT_CRC_ONE_SHOT                        = 0x00000000,
AOUT_CRC_CONT                            = 0x00000001,
} AOUT_CRC_CONT_EN;

/*
 * I2S_WORD_SIZE enum
 */

typedef enum I2S_WORD_SIZE {
I2S_WORD_SIZE_32                         = 0x00000000,
I2S_WORD_SIZE_16                         = 0x00000001,
} I2S_WORD_SIZE;

/*
 * I2S_SAMPLE_ALIGNMENT enum
 */

typedef enum I2S_SAMPLE_ALIGNMENT {
I2S_SAMPLE_LEFT_ALIGNED                  = 0x00000000,
I2S_SAMPLE_RIGHT_ALIGNED                 = 0x00000001,
} I2S_SAMPLE_ALIGNMENT;

/*
 * I2S_SAMPLE_BIT_ORDER enum
 */

typedef enum I2S_SAMPLE_BIT_ORDER {
I2S_SAMPLE_BIT_ORDER_MSB                 = 0x00000000,
I2S_SAMPLE_BIT_ORDER_LSB                 = 0x00000001,
} I2S_SAMPLE_BIT_ORDER;

/*
 * I2S_LRCLK_POLARITY enum
 */

typedef enum I2S_LRCLK_POLARITY {
I2S_LRCLK_LOW_LEFT                       = 0x00000000,
I2S_LRCLK_HIGH_LEFT                      = 0x00000001,
} I2S_LRCLK_POLARITY;

/*
 * I2S_WORD_ALIGNMENT enum
 */

typedef enum I2S_WORD_ALIGNMENT {
I2S_WORD_ALTERNATE_ALIGNMENT             = 0x00000000,
I2S_WORD_I2S_ALIGNMENT                   = 0x00000001,
} I2S_WORD_ALIGNMENT;

/*
 * SPDIF_INVERT_EN enum
 */

typedef enum SPDIF_INVERT_EN {
SPDIF_INVERT_DISABLE                     = 0x00000000,
SPDIF_INVERT_ENABLE                      = 0x00000001,
} SPDIF_INVERT_EN;

/*******************************************************
 * DCO Enums
 *******************************************************/

/*
 * DPDBG_EN enum
 */

typedef enum DPDBG_EN {
DPDBG_DISABLE                            = 0x00000000,
DPDBG_ENABLE                             = 0x00000001,
} DPDBG_EN;

/*
 * DPDBG_INPUT_EN enum
 */

typedef enum DPDBG_INPUT_EN {
DPDBG_INPUT_DISABLE                      = 0x00000000,
DPDBG_INPUT_ENABLE                       = 0x00000001,
} DPDBG_INPUT_EN;

/*
 * DPDBG_ERROR_DETECTION_MODE enum
 */

typedef enum DPDBG_ERROR_DETECTION_MODE {
DPDBG_ERROR_DETECTION_MODE_CSC           = 0x00000000,
DPDBG_ERROR_DETECTION_MODE_RS_ENCODING   = 0x00000001,
} DPDBG_ERROR_DETECTION_MODE;

/*
 * DPDBG_FIFO_OVERFLOW_INTERRUPT_MASK enum
 */

typedef enum DPDBG_FIFO_OVERFLOW_INTERRUPT_MASK {
DPDBG_FIFO_OVERFLOW_INT_DISABLE          = 0x00000000,
DPDBG_FIFO_OVERFLOW_INT_ENABLE           = 0x00000001,
} DPDBG_FIFO_OVERFLOW_INTERRUPT_MASK;

/*
 * DPDBG_FIFO_OVERFLOW_INTERRUPT_TYPE enum
 */

typedef enum DPDBG_FIFO_OVERFLOW_INTERRUPT_TYPE {
DPDBG_FIFO_OVERFLOW_INT_LEVEL_BASED      = 0x00000000,
DPDBG_FIFO_OVERFLOW_INT_PULSE_BASED      = 0x00000001,
} DPDBG_FIFO_OVERFLOW_INTERRUPT_TYPE;

/*
 * DPDBG_FIFO_OVERFLOW_INTERRUPT_ACK enum
 */

typedef enum DPDBG_FIFO_OVERFLOW_INTERRUPT_ACK {
DPDBG_FIFO_OVERFLOW_INT_NO_ACK           = 0x00000000,
DPDBG_FIFO_OVERFLOW_INT_CLEAR            = 0x00000001,
} DPDBG_FIFO_OVERFLOW_INTERRUPT_ACK;

/*
 * PM_ASSERT_RESET enum
 */

typedef enum PM_ASSERT_RESET {
PM_ASSERT_RESET_0                        = 0x00000000,
PM_ASSERT_RESET_1                        = 0x00000001,
} PM_ASSERT_RESET;

/*
 * DAC_MUX_SELECT enum
 */

typedef enum DAC_MUX_SELECT {
DAC_MUX_SELECT_DACA                      = 0x00000000,
DAC_MUX_SELECT_DACB                      = 0x00000001,
} DAC_MUX_SELECT;

/*
 * TMDS_DVO_MUX_SELECT enum
 */

typedef enum TMDS_DVO_MUX_SELECT {
TMDS_DVO_MUX_SELECT_B                    = 0x00000000,
TMDS_DVO_MUX_SELECT_G                    = 0x00000001,
TMDS_DVO_MUX_SELECT_R                    = 0x00000002,
TMDS_DVO_MUX_SELECT_RESERVED             = 0x00000003,
} TMDS_DVO_MUX_SELECT;

/*
 * DACA_SOFT_RESET enum
 */

typedef enum DACA_SOFT_RESET {
DACA_SOFT_RESET_0                        = 0x00000000,
DACA_SOFT_RESET_1                        = 0x00000001,
} DACA_SOFT_RESET;

/*
 * I2S0_SPDIF0_SOFT_RESET enum
 */

typedef enum I2S0_SPDIF0_SOFT_RESET {
I2S0_SPDIF0_SOFT_RESET_0                 = 0x00000000,
I2S0_SPDIF0_SOFT_RESET_1                 = 0x00000001,
} I2S0_SPDIF0_SOFT_RESET;

/*
 * I2S1_SOFT_RESET enum
 */

typedef enum I2S1_SOFT_RESET {
I2S1_SOFT_RESET_0                        = 0x00000000,
I2S1_SOFT_RESET_1                        = 0x00000001,
} I2S1_SOFT_RESET;

/*
 * SPDIF1_SOFT_RESET enum
 */

typedef enum SPDIF1_SOFT_RESET {
SPDIF1_SOFT_RESET_0                      = 0x00000000,
SPDIF1_SOFT_RESET_1                      = 0x00000001,
} SPDIF1_SOFT_RESET;

/*
 * DB_CLK_SOFT_RESET enum
 */

typedef enum DB_CLK_SOFT_RESET {
DB_CLK_SOFT_RESET_0                      = 0x00000000,
DB_CLK_SOFT_RESET_1                      = 0x00000001,
} DB_CLK_SOFT_RESET;

/*
 * FMT0_SOFT_RESET enum
 */

typedef enum FMT0_SOFT_RESET {
FMT0_SOFT_RESET_0                        = 0x00000000,
FMT0_SOFT_RESET_1                        = 0x00000001,
} FMT0_SOFT_RESET;

/*
 * FMT1_SOFT_RESET enum
 */

typedef enum FMT1_SOFT_RESET {
FMT1_SOFT_RESET_0                        = 0x00000000,
FMT1_SOFT_RESET_1                        = 0x00000001,
} FMT1_SOFT_RESET;

/*
 * FMT2_SOFT_RESET enum
 */

typedef enum FMT2_SOFT_RESET {
FMT2_SOFT_RESET_0                        = 0x00000000,
FMT2_SOFT_RESET_1                        = 0x00000001,
} FMT2_SOFT_RESET;

/*
 * FMT3_SOFT_RESET enum
 */

typedef enum FMT3_SOFT_RESET {
FMT3_SOFT_RESET_0                        = 0x00000000,
FMT3_SOFT_RESET_1                        = 0x00000001,
} FMT3_SOFT_RESET;

/*
 * FMT4_SOFT_RESET enum
 */

typedef enum FMT4_SOFT_RESET {
FMT4_SOFT_RESET_0                        = 0x00000000,
FMT4_SOFT_RESET_1                        = 0x00000001,
} FMT4_SOFT_RESET;

/*
 * FMT5_SOFT_RESET enum
 */

typedef enum FMT5_SOFT_RESET {
FMT5_SOFT_RESET_0                        = 0x00000000,
FMT5_SOFT_RESET_1                        = 0x00000001,
} FMT5_SOFT_RESET;

/*
 * MVP_SOFT_RESET enum
 */

typedef enum MVP_SOFT_RESET {
MVP_SOFT_RESET_0                         = 0x00000000,
MVP_SOFT_RESET_1                         = 0x00000001,
} MVP_SOFT_RESET;

/*
 * ABM_SOFT_RESET enum
 */

typedef enum ABM_SOFT_RESET {
ABM_SOFT_RESET_0                         = 0x00000000,
ABM_SOFT_RESET_1                         = 0x00000001,
} ABM_SOFT_RESET;

/*
 * DVO_SOFT_RESET enum
 */

typedef enum DVO_SOFT_RESET {
DVO_SOFT_RESET_0                         = 0x00000000,
DVO_SOFT_RESET_1                         = 0x00000001,
} DVO_SOFT_RESET;

/*
 * DIGA_FE_SOFT_RESET enum
 */

typedef enum DIGA_FE_SOFT_RESET {
DIGA_FE_SOFT_RESET_0                     = 0x00000000,
DIGA_FE_SOFT_RESET_1                     = 0x00000001,
} DIGA_FE_SOFT_RESET;

/*
 * DIGA_BE_SOFT_RESET enum
 */

typedef enum DIGA_BE_SOFT_RESET {
DIGA_BE_SOFT_RESET_0                     = 0x00000000,
DIGA_BE_SOFT_RESET_1                     = 0x00000001,
} DIGA_BE_SOFT_RESET;

/*
 * DIGB_FE_SOFT_RESET enum
 */

typedef enum DIGB_FE_SOFT_RESET {
DIGB_FE_SOFT_RESET_0                     = 0x00000000,
DIGB_FE_SOFT_RESET_1                     = 0x00000001,
} DIGB_FE_SOFT_RESET;

/*
 * DIGB_BE_SOFT_RESET enum
 */

typedef enum DIGB_BE_SOFT_RESET {
DIGB_BE_SOFT_RESET_0                     = 0x00000000,
DIGB_BE_SOFT_RESET_1                     = 0x00000001,
} DIGB_BE_SOFT_RESET;

/*
 * DIGC_FE_SOFT_RESET enum
 */

typedef enum DIGC_FE_SOFT_RESET {
DIGC_FE_SOFT_RESET_0                     = 0x00000000,
DIGC_FE_SOFT_RESET_1                     = 0x00000001,
} DIGC_FE_SOFT_RESET;

/*
 * DIGC_BE_SOFT_RESET enum
 */

typedef enum DIGC_BE_SOFT_RESET {
DIGC_BE_SOFT_RESET_0                     = 0x00000000,
DIGC_BE_SOFT_RESET_1                     = 0x00000001,
} DIGC_BE_SOFT_RESET;

/*
 * DIGD_FE_SOFT_RESET enum
 */

typedef enum DIGD_FE_SOFT_RESET {
DIGD_FE_SOFT_RESET_0                     = 0x00000000,
DIGD_FE_SOFT_RESET_1                     = 0x00000001,
} DIGD_FE_SOFT_RESET;

/*
 * DIGD_BE_SOFT_RESET enum
 */

typedef enum DIGD_BE_SOFT_RESET {
DIGD_BE_SOFT_RESET_0                     = 0x00000000,
DIGD_BE_SOFT_RESET_1                     = 0x00000001,
} DIGD_BE_SOFT_RESET;

/*
 * DIGE_FE_SOFT_RESET enum
 */

typedef enum DIGE_FE_SOFT_RESET {
DIGE_FE_SOFT_RESET_0                     = 0x00000000,
DIGE_FE_SOFT_RESET_1                     = 0x00000001,
} DIGE_FE_SOFT_RESET;

/*
 * DIGE_BE_SOFT_RESET enum
 */

typedef enum DIGE_BE_SOFT_RESET {
DIGE_BE_SOFT_RESET_0                     = 0x00000000,
DIGE_BE_SOFT_RESET_1                     = 0x00000001,
} DIGE_BE_SOFT_RESET;

/*
 * DIGF_FE_SOFT_RESET enum
 */

typedef enum DIGF_FE_SOFT_RESET {
DIGF_FE_SOFT_RESET_0                     = 0x00000000,
DIGF_FE_SOFT_RESET_1                     = 0x00000001,
} DIGF_FE_SOFT_RESET;

/*
 * DIGF_BE_SOFT_RESET enum
 */

typedef enum DIGF_BE_SOFT_RESET {
DIGF_BE_SOFT_RESET_0                     = 0x00000000,
DIGF_BE_SOFT_RESET_1                     = 0x00000001,
} DIGF_BE_SOFT_RESET;

/*
 * DIGG_FE_SOFT_RESET enum
 */

typedef enum DIGG_FE_SOFT_RESET {
DIGG_FE_SOFT_RESET_0                     = 0x00000000,
DIGG_FE_SOFT_RESET_1                     = 0x00000001,
} DIGG_FE_SOFT_RESET;

/*
 * DIGG_BE_SOFT_RESET enum
 */

typedef enum DIGG_BE_SOFT_RESET {
DIGG_BE_SOFT_RESET_0                     = 0x00000000,
DIGG_BE_SOFT_RESET_1                     = 0x00000001,
} DIGG_BE_SOFT_RESET;

/*
 * DPDBG_SOFT_RESET enum
 */

typedef enum DPDBG_SOFT_RESET {
DPDBG_SOFT_RESET_0                       = 0x00000000,
DPDBG_SOFT_RESET_1                       = 0x00000001,
} DPDBG_SOFT_RESET;

/*
 * DIGLPA_FE_SOFT_RESET enum
 */

typedef enum DIGLPA_FE_SOFT_RESET {
DIGLPA_FE_SOFT_RESET_0                   = 0x00000000,
DIGLPA_FE_SOFT_RESET_1                   = 0x00000001,
} DIGLPA_FE_SOFT_RESET;

/*
 * DIGLPA_BE_SOFT_RESET enum
 */

typedef enum DIGLPA_BE_SOFT_RESET {
DIGLPA_BE_SOFT_RESET_0                   = 0x00000000,
DIGLPA_BE_SOFT_RESET_1                   = 0x00000001,
} DIGLPA_BE_SOFT_RESET;

/*
 * DIGLPB_FE_SOFT_RESET enum
 */

typedef enum DIGLPB_FE_SOFT_RESET {
DIGLPB_FE_SOFT_RESET_0                   = 0x00000000,
DIGLPB_FE_SOFT_RESET_1                   = 0x00000001,
} DIGLPB_FE_SOFT_RESET;

/*
 * DIGLPB_BE_SOFT_RESET enum
 */

typedef enum DIGLPB_BE_SOFT_RESET {
DIGLPB_BE_SOFT_RESET_0                   = 0x00000000,
DIGLPB_BE_SOFT_RESET_1                   = 0x00000001,
} DIGLPB_BE_SOFT_RESET;

/*
 * GENERICA_STEREOSYNC_SEL enum
 */

typedef enum GENERICA_STEREOSYNC_SEL {
GENERICA_STEREOSYNC_SEL_D1               = 0x00000000,
GENERICA_STEREOSYNC_SEL_D2               = 0x00000001,
GENERICA_STEREOSYNC_SEL_D3               = 0x00000002,
GENERICA_STEREOSYNC_SEL_D4               = 0x00000003,
GENERICA_STEREOSYNC_SEL_D5               = 0x00000004,
GENERICA_STEREOSYNC_SEL_D6               = 0x00000005,
GENERICA_STEREOSYNC_SEL_RESERVED         = 0x00000006,
} GENERICA_STEREOSYNC_SEL;

/*
 * GENERICB_STEREOSYNC_SEL enum
 */

typedef enum GENERICB_STEREOSYNC_SEL {
GENERICB_STEREOSYNC_SEL_D1               = 0x00000000,
GENERICB_STEREOSYNC_SEL_D2               = 0x00000001,
GENERICB_STEREOSYNC_SEL_D3               = 0x00000002,
GENERICB_STEREOSYNC_SEL_D4               = 0x00000003,
GENERICB_STEREOSYNC_SEL_D5               = 0x00000004,
GENERICB_STEREOSYNC_SEL_D6               = 0x00000005,
GENERICB_STEREOSYNC_SEL_RESERVED         = 0x00000006,
} GENERICB_STEREOSYNC_SEL;

/*
 * DCO_DBG_BLOCK_SEL enum
 */

typedef enum DCO_DBG_BLOCK_SEL {
DCO_DBG_BLOCK_SEL_DCO                    = 0x00000000,
DCO_DBG_BLOCK_SEL_ABM                    = 0x00000001,
DCO_DBG_BLOCK_SEL_DVO                    = 0x00000002,
DCO_DBG_BLOCK_SEL_DAC                    = 0x00000003,
DCO_DBG_BLOCK_SEL_MVP                    = 0x00000004,
DCO_DBG_BLOCK_SEL_FMT0                   = 0x00000005,
DCO_DBG_BLOCK_SEL_FMT1                   = 0x00000006,
DCO_DBG_BLOCK_SEL_FMT2                   = 0x00000007,
DCO_DBG_BLOCK_SEL_FMT3                   = 0x00000008,
DCO_DBG_BLOCK_SEL_FMT4                   = 0x00000009,
DCO_DBG_BLOCK_SEL_FMT5                   = 0x0000000a,
DCO_DBG_BLOCK_SEL_DIGFE_A                = 0x0000000b,
DCO_DBG_BLOCK_SEL_DIGFE_B                = 0x0000000c,
DCO_DBG_BLOCK_SEL_DIGFE_C                = 0x0000000d,
DCO_DBG_BLOCK_SEL_DIGFE_D                = 0x0000000e,
DCO_DBG_BLOCK_SEL_DIGFE_E                = 0x0000000f,
DCO_DBG_BLOCK_SEL_DIGFE_F                = 0x00000010,
DCO_DBG_BLOCK_SEL_DIGFE_G                = 0x00000011,
DCO_DBG_BLOCK_SEL_DIGA                   = 0x00000012,
DCO_DBG_BLOCK_SEL_DIGB                   = 0x00000013,
DCO_DBG_BLOCK_SEL_DIGC                   = 0x00000014,
DCO_DBG_BLOCK_SEL_DIGD                   = 0x00000015,
DCO_DBG_BLOCK_SEL_DIGE                   = 0x00000016,
DCO_DBG_BLOCK_SEL_DIGF                   = 0x00000017,
DCO_DBG_BLOCK_SEL_DIGG                   = 0x00000018,
DCO_DBG_BLOCK_SEL_DPFE_A                 = 0x00000019,
DCO_DBG_BLOCK_SEL_DPFE_B                 = 0x0000001a,
DCO_DBG_BLOCK_SEL_DPFE_C                 = 0x0000001b,
DCO_DBG_BLOCK_SEL_DPFE_D                 = 0x0000001c,
DCO_DBG_BLOCK_SEL_DPFE_E                 = 0x0000001d,
DCO_DBG_BLOCK_SEL_DPFE_F                 = 0x0000001e,
DCO_DBG_BLOCK_SEL_DPFE_G                 = 0x0000001f,
DCO_DBG_BLOCK_SEL_DPA                    = 0x00000020,
DCO_DBG_BLOCK_SEL_DPB                    = 0x00000021,
DCO_DBG_BLOCK_SEL_DPC                    = 0x00000022,
DCO_DBG_BLOCK_SEL_DPD                    = 0x00000023,
DCO_DBG_BLOCK_SEL_DPE                    = 0x00000024,
DCO_DBG_BLOCK_SEL_DPF                    = 0x00000025,
DCO_DBG_BLOCK_SEL_DPG                    = 0x00000026,
DCO_DBG_BLOCK_SEL_AUX0                   = 0x00000027,
DCO_DBG_BLOCK_SEL_AUX1                   = 0x00000028,
DCO_DBG_BLOCK_SEL_AUX2                   = 0x00000029,
DCO_DBG_BLOCK_SEL_AUX3                   = 0x0000002a,
DCO_DBG_BLOCK_SEL_AUX4                   = 0x0000002b,
DCO_DBG_BLOCK_SEL_AUX5                   = 0x0000002c,
DCO_DBG_BLOCK_SEL_PERFMON_DCO            = 0x0000002d,
DCO_DBG_BLOCK_SEL_AUDIO_OUT              = 0x0000002e,
DCO_DBG_BLOCK_SEL_DIGLPFEA               = 0x0000002f,
DCO_DBG_BLOCK_SEL_DIGLPFEB               = 0x00000030,
DCO_DBG_BLOCK_SEL_DIGLPA                 = 0x00000031,
DCO_DBG_BLOCK_SEL_DIGLPB                 = 0x00000032,
DCO_DBG_BLOCK_SEL_DPLPFEA                = 0x00000033,
DCO_DBG_BLOCK_SEL_DPLPFEB                = 0x00000034,
DCO_DBG_BLOCK_SEL_DPLPA                  = 0x00000035,
DCO_DBG_BLOCK_SEL_DPLPB                  = 0x00000036,
} DCO_DBG_BLOCK_SEL;

/*
 * DCO_HDMI_RXSTATUS_TIMER_CONTROL_DCO_HDMI_RXSTATUS_TIMER_TYPE enum
 */

typedef enum DCO_HDMI_RXSTATUS_TIMER_CONTROL_DCO_HDMI_RXSTATUS_TIMER_TYPE {
DCO_HDMI_RXSTATUS_TIMER_TYPE_LEVEL       = 0x00000000,
DCO_HDMI_RXSTATUS_TIMER_TYPE_PULSE       = 0x00000001,
} DCO_HDMI_RXSTATUS_TIMER_CONTROL_DCO_HDMI_RXSTATUS_TIMER_TYPE;

/*
 * FMT420_MEMORY_SOURCE_SEL enum
 */

typedef enum FMT420_MEMORY_SOURCE_SEL {
FMT420_MEMORY_SOURCE_SEL_FMT0            = 0x00000000,
FMT420_MEMORY_SOURCE_SEL_FMT1            = 0x00000001,
FMT420_MEMORY_SOURCE_SEL_FMT2            = 0x00000002,
FMT420_MEMORY_SOURCE_SEL_FMT3            = 0x00000003,
FMT420_MEMORY_SOURCE_SEL_FMT4            = 0x00000004,
FMT420_MEMORY_SOURCE_SEL_FMT5            = 0x00000005,
FMT420_MEMORY_SOURCE_SEL_FMT_RESERVED    = 0x00000006,
} FMT420_MEMORY_SOURCE_SEL;

/*******************************************************
 * DOUT_I2C Enums
 *******************************************************/

/*
 * DOUT_I2C_CONTROL_GO enum
 */

typedef enum DOUT_I2C_CONTROL_GO {
DOUT_I2C_CONTROL_STOP_TRANSFER           = 0x00000000,
DOUT_I2C_CONTROL_START_TRANSFER          = 0x00000001,
} DOUT_I2C_CONTROL_GO;

/*
 * DOUT_I2C_CONTROL_SOFT_RESET enum
 */

typedef enum DOUT_I2C_CONTROL_SOFT_RESET {
DOUT_I2C_CONTROL_NOT_RESET_I2C_CONTROLLER = 0x00000000,
DOUT_I2C_CONTROL_RESET_I2C_CONTROLLER    = 0x00000001,
} DOUT_I2C_CONTROL_SOFT_RESET;

/*
 * DOUT_I2C_CONTROL_SEND_RESET enum
 */

typedef enum DOUT_I2C_CONTROL_SEND_RESET {
DOUT_I2C_CONTROL__NOT_SEND_RESET         = 0x00000000,
DOUT_I2C_CONTROL__SEND_RESET             = 0x00000001,
} DOUT_I2C_CONTROL_SEND_RESET;

/*
 * DOUT_I2C_CONTROL_SW_STATUS_RESET enum
 */

typedef enum DOUT_I2C_CONTROL_SW_STATUS_RESET {
DOUT_I2C_CONTROL_NOT_RESET_SW_STATUS     = 0x00000000,
DOUT_I2C_CONTROL_RESET_SW_STATUS         = 0x00000001,
} DOUT_I2C_CONTROL_SW_STATUS_RESET;

/*
 * DOUT_I2C_CONTROL_DDC_SELECT enum
 */

typedef enum DOUT_I2C_CONTROL_DDC_SELECT {
DOUT_I2C_CONTROL_SELECT_DDC1             = 0x00000000,
DOUT_I2C_CONTROL_SELECT_DDC2             = 0x00000001,
DOUT_I2C_CONTROL_SELECT_DDC3             = 0x00000002,
DOUT_I2C_CONTROL_SELECT_DDC4             = 0x00000003,
DOUT_I2C_CONTROL_SELECT_DDC5             = 0x00000004,
DOUT_I2C_CONTROL_SELECT_DDC6             = 0x00000005,
DOUT_I2C_CONTROL_SELECT_DDCVGA           = 0x00000006,
} DOUT_I2C_CONTROL_DDC_SELECT;

/*
 * DOUT_I2C_CONTROL_TRANSACTION_COUNT enum
 */

typedef enum DOUT_I2C_CONTROL_TRANSACTION_COUNT {
DOUT_I2C_CONTROL_TRANS0                  = 0x00000000,
DOUT_I2C_CONTROL_TRANS0_TRANS1           = 0x00000001,
DOUT_I2C_CONTROL_TRANS0_TRANS1_TRANS2    = 0x00000002,
DOUT_I2C_CONTROL_TRANS0_TRANS1_TRANS2_TRANS3  = 0x00000003,
} DOUT_I2C_CONTROL_TRANSACTION_COUNT;

/*
 * DOUT_I2C_CONTROL_DBG_REF_SEL enum
 */

typedef enum DOUT_I2C_CONTROL_DBG_REF_SEL {
DOUT_I2C_CONTROL_NORMAL_DEBUG            = 0x00000000,
DOUT_I2C_CONTROL_FAST_REFERENCE_DEBUG    = 0x00000001,
} DOUT_I2C_CONTROL_DBG_REF_SEL;

/*
 * DOUT_I2C_ARBITRATION_SW_PRIORITY enum
 */

typedef enum DOUT_I2C_ARBITRATION_SW_PRIORITY {
DOUT_I2C_ARBITRATION_SW_PRIORITY_NORMAL  = 0x00000000,
DOUT_I2C_ARBITRATION_SW_PRIORITY_HIGH    = 0x00000001,
DOUT_I2C_ARBITRATION_SW_PRIORITY_0_RESERVED = 0x00000002,
DOUT_I2C_ARBITRATION_SW_PRIORITY_1_RESERVED = 0x00000003,
} DOUT_I2C_ARBITRATION_SW_PRIORITY;

/*
 * DOUT_I2C_ARBITRATION_NO_QUEUED_SW_GO enum
 */

typedef enum DOUT_I2C_ARBITRATION_NO_QUEUED_SW_GO {
DOUT_I2C_ARBITRATION_SW_QUEUE_ENABLED    = 0x00000000,
DOUT_I2C_ARBITRATION_SW_QUEUE_DISABLED   = 0x00000001,
} DOUT_I2C_ARBITRATION_NO_QUEUED_SW_GO;

/*
 * DOUT_I2C_ARBITRATION_ABORT_XFER enum
 */

typedef enum DOUT_I2C_ARBITRATION_ABORT_XFER {
DOUT_I2C_ARBITRATION_NOT_ABORT_CURRENT_TRANSFER = 0x00000000,
DOUT_I2C_ARBITRATION_ABORT_CURRENT_TRANSFER  = 0x00000001,
} DOUT_I2C_ARBITRATION_ABORT_XFER;

/*
 * DOUT_I2C_ARBITRATION_USE_I2C_REG_REQ enum
 */

typedef enum DOUT_I2C_ARBITRATION_USE_I2C_REG_REQ {
DOUT_I2C_ARBITRATION__NOT_USE_I2C_REG_REQ = 0x00000000,
DOUT_I2C_ARBITRATION__USE_I2C_REG_REQ    = 0x00000001,
} DOUT_I2C_ARBITRATION_USE_I2C_REG_REQ;

/*
 * DOUT_I2C_ARBITRATION_DONE_USING_I2C_REG enum
 */

typedef enum DOUT_I2C_ARBITRATION_DONE_USING_I2C_REG {
DOUT_I2C_ARBITRATION_DONE__NOT_USING_I2C_REG = 0x00000000,
DOUT_I2C_ARBITRATION_DONE__USING_I2C_REG  = 0x00000001,
} DOUT_I2C_ARBITRATION_DONE_USING_I2C_REG;

/*
 * DOUT_I2C_ACK enum
 */

typedef enum DOUT_I2C_ACK {
DOUT_I2C_NO_ACK                          = 0x00000000,
DOUT_I2C_ACK_TO_CLEAN                    = 0x00000001,
} DOUT_I2C_ACK;

/*
 * DOUT_I2C_DDC_SPEED_THRESHOLD enum
 */

typedef enum DOUT_I2C_DDC_SPEED_THRESHOLD {
DOUT_I2C_DDC_SPEED_THRESHOLD_BIG_THAN_ZERO  = 0x00000000,
DOUT_I2C_DDC_SPEED_THRESHOLD_QUATER_OF_TOTAL_SAMPLE  = 0x00000001,
DOUT_I2C_DDC_SPEED_THRESHOLD_HALF_OF_TOTAL_SAMPLE  = 0x00000002,
DOUT_I2C_DDC_SPEED_THRESHOLD_THREE_QUATERS_OF_TOTAL_SAMPLE  = 0x00000003,
} DOUT_I2C_DDC_SPEED_THRESHOLD;

/*
 * DOUT_I2C_DDC_SETUP_DATA_DRIVE_EN enum
 */

typedef enum DOUT_I2C_DDC_SETUP_DATA_DRIVE_EN {
DOUT_I2C_DDC_SETUP_DATA_DRIVE_BY_EXTERNAL_RESISTOR  = 0x00000000,
DOUT_I2C_DDC_SETUP_I2C_PAD_DRIVE_SDA     = 0x00000001,
} DOUT_I2C_DDC_SETUP_DATA_DRIVE_EN;

/*
 * DOUT_I2C_DDC_SETUP_DATA_DRIVE_SEL enum
 */

typedef enum DOUT_I2C_DDC_SETUP_DATA_DRIVE_SEL {
DOUT_I2C_DDC_SETUP_DATA_DRIVE_FOR_10MCLKS  = 0x00000000,
DOUT_I2C_DDC_SETUP_DATA_DRIVE_FOR_20MCLKS  = 0x00000001,
} DOUT_I2C_DDC_SETUP_DATA_DRIVE_SEL;

/*
 * DOUT_I2C_DDC_SETUP_EDID_DETECT_MODE enum
 */

typedef enum DOUT_I2C_DDC_SETUP_EDID_DETECT_MODE {
DOUT_I2C_DDC_SETUP_EDID_DETECT_CONNECT   = 0x00000000,
DOUT_I2C_DDC_SETUP_EDID_DETECT_DISCONNECT  = 0x00000001,
} DOUT_I2C_DDC_SETUP_EDID_DETECT_MODE;

/*
 * DOUT_I2C_DDC_SETUP_CLK_DRIVE_EN enum
 */

typedef enum DOUT_I2C_DDC_SETUP_CLK_DRIVE_EN {
DOUT_I2C_DDC_SETUP_CLK_DRIVE_BY_EXTERNAL_RESISTOR  = 0x00000000,
DOUT_I2C_DDC_SETUP_I2C_PAD_DRIVE_SCL     = 0x00000001,
} DOUT_I2C_DDC_SETUP_CLK_DRIVE_EN;

/*
 * DOUT_I2C_TRANSACTION_STOP_ON_NACK enum
 */

typedef enum DOUT_I2C_TRANSACTION_STOP_ON_NACK {
DOUT_I2C_TRANSACTION_STOP_CURRENT_TRANS  = 0x00000000,
DOUT_I2C_TRANSACTION_STOP_ALL_TRANS      = 0x00000001,
} DOUT_I2C_TRANSACTION_STOP_ON_NACK;

/*
 * DOUT_I2C_DATA_INDEX_WRITE enum
 */

typedef enum DOUT_I2C_DATA_INDEX_WRITE {
DOUT_I2C_DATA__NOT_INDEX_WRITE           = 0x00000000,
DOUT_I2C_DATA__INDEX_WRITE               = 0x00000001,
} DOUT_I2C_DATA_INDEX_WRITE;

/*
 * DOUT_I2C_EDID_DETECT_CTRL_SEND_RESET enum
 */

typedef enum DOUT_I2C_EDID_DETECT_CTRL_SEND_RESET {
DOUT_I2C_EDID_NOT_SEND_RESET_BEFORE_EDID_READ_TRACTION = 0x00000000,
DOUT_I2C_EDID_SEND_RESET_BEFORE_EDID_READ_TRACTION  = 0x00000001,
} DOUT_I2C_EDID_DETECT_CTRL_SEND_RESET;

/*
 * DOUT_I2C_READ_REQUEST_INTERRUPT_TYPE enum
 */

typedef enum DOUT_I2C_READ_REQUEST_INTERRUPT_TYPE {
DOUT_I2C_READ_REQUEST_INTERRUPT_TYPE__LEVEL  = 0x00000000,
DOUT_I2C_READ_REQUEST_INTERRUPT_TYPE__PULSE  = 0x00000001,
} DOUT_I2C_READ_REQUEST_INTERRUPT_TYPE;

/*******************************************************
 * FBC Enums
 *******************************************************/

/*
 * FBC_IDLE_MASK_MASK_BITS enum
 */

typedef enum FBC_IDLE_MASK_MASK_BITS {
FBC_IDLE_MASK_DISP_REG_UPDATE            = 0x00000000,
FBC_IDLE_MASK_RESERVED1                  = 0x00000001,
FBC_IDLE_MASK_FBC_GRPH_COMP_EN           = 0x00000002,
FBC_IDLE_MASK_FBC_MIN_COMPRESSION        = 0x00000003,
FBC_IDLE_MASK_FBC_ALPHA_COMP_EN          = 0x00000004,
FBC_IDLE_MASK_FBC_ZERO_ALPHA_CHUNK_SKIP_EN  = 0x00000005,
FBC_IDLE_MASK_FBC_FORCE_COPY_TO_COMP_BUF  = 0x00000006,
FBC_IDLE_MASK_RESERVED7                  = 0x00000007,
FBC_IDLE_MASK_RESERVED8                  = 0x00000008,
FBC_IDLE_MASK_RESERVED9                  = 0x00000009,
FBC_IDLE_MASK_RESERVED10                 = 0x0000000a,
FBC_IDLE_MASK_RESERVED11                 = 0x0000000b,
FBC_IDLE_MASK_RESERVED12                 = 0x0000000c,
FBC_IDLE_MASK_RESERVED13                 = 0x0000000d,
FBC_IDLE_MASK_RESERVED14                 = 0x0000000e,
FBC_IDLE_MASK_RESERVED15                 = 0x0000000f,
FBC_IDLE_MASK_RESERVED16                 = 0x00000010,
FBC_IDLE_MASK_RESERVED17                 = 0x00000011,
FBC_IDLE_MASK_RESERVED18                 = 0x00000012,
FBC_IDLE_MASK_RESERVED19                 = 0x00000013,
FBC_IDLE_MASK_RESERVED20                 = 0x00000014,
FBC_IDLE_MASK_RESERVED21                 = 0x00000015,
FBC_IDLE_MASK_RESERVED22                 = 0x00000016,
FBC_IDLE_MASK_RESERVED23                 = 0x00000017,
FBC_IDLE_MASK_MC_HIT_REGION_0            = 0x00000018,
FBC_IDLE_MASK_MC_HIT_REGION_1            = 0x00000019,
FBC_IDLE_MASK_MC_HIT_REGION_2            = 0x0000001a,
FBC_IDLE_MASK_MC_HIT_REGION_3            = 0x0000001b,
FBC_IDLE_MASK_MC_WRITE                   = 0x0000001c,
FBC_IDLE_MASK_RESERVED29                 = 0x0000001d,
FBC_IDLE_MASK_RESERVED30                 = 0x0000001e,
FBC_IDLE_MASK_RESERVED31                 = 0x0000001f,
} FBC_IDLE_MASK_MASK_BITS;

/*******************************************************
 * DPCSRX Enums
 *******************************************************/

/*
 * DPCSRX_RX_CLOCK_CNTL_DPCS_SYMCLK_RX_SEL enum
 */

typedef enum DPCSRX_RX_CLOCK_CNTL_DPCS_SYMCLK_RX_SEL {
DPCSRX_BPHY_PCS_RX0_CLK                  = 0x00000000,
DPCSRX_BPHY_PCS_RX1_CLK                  = 0x00000001,
DPCSRX_BPHY_PCS_RX2_CLK                  = 0x00000002,
DPCSRX_BPHY_PCS_RX3_CLK                  = 0x00000003,
} DPCSRX_RX_CLOCK_CNTL_DPCS_SYMCLK_RX_SEL;

/*
 * DPCSRX_DBG_CFGCLK_SEL enum
 */

typedef enum DPCSRX_DBG_CFGCLK_SEL {
DPCSRX_DBG_CFGCLK_SEL_DC_DPCS_INF        = 0x00000000,
DPCSRX_DBG_CFGCLK_SEL_DPCS_BPHY_INF      = 0x00000001,
DPCSRX_DBG_CFGCLK_SEL_CBUS_SLAVE         = 0x00000002,
DPCSRX_DBG_CFGCLK_SEL_CBUS_MASTER        = 0x00000003,
} DPCSRX_DBG_CFGCLK_SEL;

/*
 * DPCSRX_RX_SYMCLK_SEL enum
 */

typedef enum DPCSRX_RX_SYMCLK_SEL {
DPCSRX_DBG_RX_SYMCLK_SEL_OUT0            = 0x00000000,
DPCSRX_DBG_RX_SYMCLK_SEL_OUT1            = 0x00000001,
DPCSRX_DBG_RX_SYMCLK_SEL_INT             = 0x00000002,
} DPCSRX_RX_SYMCLK_SEL;

/*******************************************************
 * DPCSTX Enums
 *******************************************************/

/*
 * DPCSTX_DBG_CFGCLK_SEL enum
 */

typedef enum DPCSTX_DBG_CFGCLK_SEL {
DPCSTX_DBG_CFGCLK_SEL_DC_DPCS_INF        = 0x00000000,
DPCSTX_DBG_CFGCLK_SEL_DPCS_BPHY_INF      = 0x00000001,
DPCSTX_DBG_CFGCLK_SEL_CBUS_SLAVE         = 0x00000002,
DPCSTX_DBG_CFGCLK_SEL_CBUS_MASTER        = 0x00000003,
} DPCSTX_DBG_CFGCLK_SEL;

/*
 * DPCSTX_TX_SYMCLK_SEL enum
 */

typedef enum DPCSTX_TX_SYMCLK_SEL {
DPCSTX_DBG_TX_SYMCLK_SEL_IN0             = 0x00000000,
DPCSTX_DBG_TX_SYMCLK_SEL_IN1             = 0x00000001,
DPCSTX_DBG_TX_SYMCLK_SEL_FIFO_WR         = 0x00000002,
} DPCSTX_TX_SYMCLK_SEL;

/*
 * DPCSTX_TX_SYMCLK_DIV2_SEL enum
 */

typedef enum DPCSTX_TX_SYMCLK_DIV2_SEL {
DPCSTX_DBG_TX_SYMCLK_DIV2_SEL_OUT0       = 0x00000000,
DPCSTX_DBG_TX_SYMCLK_DIV2_SEL_OUT1       = 0x00000001,
DPCSTX_DBG_TX_SYMCLK_DIV2_SEL_OUT2       = 0x00000002,
DPCSTX_DBG_TX_SYMCLK_DIV2_SEL_OUT3       = 0x00000003,
DPCSTX_DBG_TX_SYMCLK_DIV2_SEL_FIFO_RD    = 0x00000004,
DPCSTX_DBG_TX_SYMCLK_DIV2_SEL_INT        = 0x00000005,
} DPCSTX_TX_SYMCLK_DIV2_SEL;

/*******************************************************
 * CB Enums
 *******************************************************/

/*
 * SurfaceNumber enum
 */

typedef enum SurfaceNumber {
NUMBER_UNORM                             = 0x00000000,
NUMBER_SNORM                             = 0x00000001,
NUMBER_USCALED                           = 0x00000002,
NUMBER_SSCALED                           = 0x00000003,
NUMBER_UINT                              = 0x00000004,
NUMBER_SINT                              = 0x00000005,
NUMBER_SRGB                              = 0x00000006,
NUMBER_FLOAT                             = 0x00000007,
} SurfaceNumber;

/*
 * SurfaceSwap enum
 */

typedef enum SurfaceSwap {
SWAP_STD                                 = 0x00000000,
SWAP_ALT                                 = 0x00000001,
SWAP_STD_REV                             = 0x00000002,
SWAP_ALT_REV                             = 0x00000003,
} SurfaceSwap;

/*
 * CBMode enum
 */

typedef enum CBMode {
CB_DISABLE                               = 0x00000000,
CB_NORMAL                                = 0x00000001,
CB_ELIMINATE_FAST_CLEAR                  = 0x00000002,
CB_RESOLVE                               = 0x00000003,
CB_DECOMPRESS                            = 0x00000004,
CB_FMASK_DECOMPRESS                      = 0x00000005,
CB_DCC_DECOMPRESS                        = 0x00000006,
} CBMode;

/*
 * RoundMode enum
 */

typedef enum RoundMode {
ROUND_BY_HALF                            = 0x00000000,
ROUND_TRUNCATE                           = 0x00000001,
} RoundMode;

/*
 * SourceFormat enum
 */

typedef enum SourceFormat {
EXPORT_4C_32BPC                          = 0x00000000,
EXPORT_4C_16BPC                          = 0x00000001,
EXPORT_2C_32BPC_GR                       = 0x00000002,
EXPORT_2C_32BPC_AR                       = 0x00000003,
} SourceFormat;

/*
 * BlendOp enum
 */

typedef enum BlendOp {
BLEND_ZERO                               = 0x00000000,
BLEND_ONE                                = 0x00000001,
BLEND_SRC_COLOR                          = 0x00000002,
BLEND_ONE_MINUS_SRC_COLOR                = 0x00000003,
BLEND_SRC_ALPHA                          = 0x00000004,
BLEND_ONE_MINUS_SRC_ALPHA                = 0x00000005,
BLEND_DST_ALPHA                          = 0x00000006,
BLEND_ONE_MINUS_DST_ALPHA                = 0x00000007,
BLEND_DST_COLOR                          = 0x00000008,
BLEND_ONE_MINUS_DST_COLOR                = 0x00000009,
BLEND_SRC_ALPHA_SATURATE                 = 0x0000000a,
BLEND_BOTH_SRC_ALPHA                     = 0x0000000b,
BLEND_BOTH_INV_SRC_ALPHA                 = 0x0000000c,
BLEND_CONSTANT_COLOR                     = 0x0000000d,
BLEND_ONE_MINUS_CONSTANT_COLOR           = 0x0000000e,
BLEND_SRC1_COLOR                         = 0x0000000f,
BLEND_INV_SRC1_COLOR                     = 0x00000010,
BLEND_SRC1_ALPHA                         = 0x00000011,
BLEND_INV_SRC1_ALPHA                     = 0x00000012,
BLEND_CONSTANT_ALPHA                     = 0x00000013,
BLEND_ONE_MINUS_CONSTANT_ALPHA           = 0x00000014,
} BlendOp;

/*
 * CombFunc enum
 */

typedef enum CombFunc {
COMB_DST_PLUS_SRC                        = 0x00000000,
COMB_SRC_MINUS_DST                       = 0x00000001,
COMB_MIN_DST_SRC                         = 0x00000002,
COMB_MAX_DST_SRC                         = 0x00000003,
COMB_DST_MINUS_SRC                       = 0x00000004,
} CombFunc;

/*
 * BlendOpt enum
 */

typedef enum BlendOpt {
FORCE_OPT_AUTO                           = 0x00000000,
FORCE_OPT_DISABLE                        = 0x00000001,
FORCE_OPT_ENABLE_IF_SRC_A_0              = 0x00000002,
FORCE_OPT_ENABLE_IF_SRC_RGB_0            = 0x00000003,
FORCE_OPT_ENABLE_IF_SRC_ARGB_0           = 0x00000004,
FORCE_OPT_ENABLE_IF_SRC_A_1              = 0x00000005,
FORCE_OPT_ENABLE_IF_SRC_RGB_1            = 0x00000006,
FORCE_OPT_ENABLE_IF_SRC_ARGB_1           = 0x00000007,
} BlendOpt;

/*
 * CmaskCode enum
 */

typedef enum CmaskCode {
CMASK_CLR00_F0                           = 0x00000000,
CMASK_CLR00_F1                           = 0x00000001,
CMASK_CLR00_F2                           = 0x00000002,
CMASK_CLR00_FX                           = 0x00000003,
CMASK_CLR01_F0                           = 0x00000004,
CMASK_CLR01_F1                           = 0x00000005,
CMASK_CLR01_F2                           = 0x00000006,
CMASK_CLR01_FX                           = 0x00000007,
CMASK_CLR10_F0                           = 0x00000008,
CMASK_CLR10_F1                           = 0x00000009,
CMASK_CLR10_F2                           = 0x0000000a,
CMASK_CLR10_FX                           = 0x0000000b,
CMASK_CLR11_F0                           = 0x0000000c,
CMASK_CLR11_F1                           = 0x0000000d,
CMASK_CLR11_F2                           = 0x0000000e,
CMASK_CLR11_FX                           = 0x0000000f,
} CmaskCode;

/*
 * CmaskAddr enum
 */

typedef enum CmaskAddr {
CMASK_ADDR_TILED                         = 0x00000000,
CMASK_ADDR_LINEAR                        = 0x00000001,
CMASK_ADDR_COMPATIBLE                    = 0x00000002,
} CmaskAddr;

/*
 * MemArbMode enum
 */

typedef enum MemArbMode {
MEM_ARB_MODE_FIXED                       = 0x00000000,
MEM_ARB_MODE_AGE                         = 0x00000001,
MEM_ARB_MODE_WEIGHT                      = 0x00000002,
MEM_ARB_MODE_BOTH                        = 0x00000003,
} MemArbMode;

/*
 * CBPerfSel enum
 */

typedef enum CBPerfSel {
CB_PERF_SEL_NONE                         = 0x00000000,
CB_PERF_SEL_BUSY                         = 0x00000001,
CB_PERF_SEL_CORE_SCLK_VLD                = 0x00000002,
CB_PERF_SEL_REG_SCLK0_VLD                = 0x00000003,
CB_PERF_SEL_REG_SCLK1_VLD                = 0x00000004,
CB_PERF_SEL_DRAWN_QUAD                   = 0x00000005,
CB_PERF_SEL_DRAWN_PIXEL                  = 0x00000006,
CB_PERF_SEL_DRAWN_QUAD_FRAGMENT          = 0x00000007,
CB_PERF_SEL_DRAWN_TILE                   = 0x00000008,
CB_PERF_SEL_DB_CB_TILE_VALID_READY       = 0x00000009,
CB_PERF_SEL_DB_CB_TILE_VALID_READYB      = 0x0000000a,
CB_PERF_SEL_DB_CB_TILE_VALIDB_READY      = 0x0000000b,
CB_PERF_SEL_DB_CB_TILE_VALIDB_READYB     = 0x0000000c,
CB_PERF_SEL_CM_FC_TILE_VALID_READY       = 0x0000000d,
CB_PERF_SEL_CM_FC_TILE_VALID_READYB      = 0x0000000e,
CB_PERF_SEL_CM_FC_TILE_VALIDB_READY      = 0x0000000f,
CB_PERF_SEL_CM_FC_TILE_VALIDB_READYB     = 0x00000010,
CB_PERF_SEL_MERGE_TILE_ONLY_VALID_READY  = 0x00000011,
CB_PERF_SEL_MERGE_TILE_ONLY_VALID_READYB  = 0x00000012,
CB_PERF_SEL_DB_CB_LQUAD_VALID_READY      = 0x00000013,
CB_PERF_SEL_DB_CB_LQUAD_VALID_READYB     = 0x00000014,
CB_PERF_SEL_DB_CB_LQUAD_VALIDB_READY     = 0x00000015,
CB_PERF_SEL_DB_CB_LQUAD_VALIDB_READYB    = 0x00000016,
CB_PERF_SEL_LQUAD_NO_TILE                = 0x00000017,
CB_PERF_SEL_LQUAD_FORMAT_IS_EXPORT_32_R  = 0x00000018,
CB_PERF_SEL_LQUAD_FORMAT_IS_EXPORT_32_AR  = 0x00000019,
CB_PERF_SEL_LQUAD_FORMAT_IS_EXPORT_32_GR  = 0x0000001a,
CB_PERF_SEL_LQUAD_FORMAT_IS_EXPORT_32_ABGR  = 0x0000001b,
CB_PERF_SEL_LQUAD_FORMAT_IS_EXPORT_FP16_ABGR  = 0x0000001c,
CB_PERF_SEL_LQUAD_FORMAT_IS_EXPORT_SIGNED16_ABGR  = 0x0000001d,
CB_PERF_SEL_LQUAD_FORMAT_IS_EXPORT_UNSIGNED16_ABGR  = 0x0000001e,
CB_PERF_SEL_QUAD_KILLED_BY_EXTRA_PIXEL_EXPORT  = 0x0000001f,
CB_PERF_SEL_QUAD_KILLED_BY_COLOR_INVALID  = 0x00000020,
CB_PERF_SEL_QUAD_KILLED_BY_NULL_TARGET_SHADER_MASK  = 0x00000021,
CB_PERF_SEL_QUAD_KILLED_BY_NULL_SAMPLE_MASK  = 0x00000022,
CB_PERF_SEL_QUAD_KILLED_BY_DISCARD_PIXEL  = 0x00000023,
CB_PERF_SEL_FC_CLEAR_QUAD_VALID_READY    = 0x00000024,
CB_PERF_SEL_FC_CLEAR_QUAD_VALID_READYB   = 0x00000025,
CB_PERF_SEL_FC_CLEAR_QUAD_VALIDB_READY   = 0x00000026,
CB_PERF_SEL_FC_CLEAR_QUAD_VALIDB_READYB  = 0x00000027,
CB_PERF_SEL_FOP_IN_VALID_READY           = 0x00000028,
CB_PERF_SEL_FOP_IN_VALID_READYB          = 0x00000029,
CB_PERF_SEL_FOP_IN_VALIDB_READY          = 0x0000002a,
CB_PERF_SEL_FOP_IN_VALIDB_READYB         = 0x0000002b,
CB_PERF_SEL_FC_CC_QUADFRAG_VALID_READY   = 0x0000002c,
CB_PERF_SEL_FC_CC_QUADFRAG_VALID_READYB  = 0x0000002d,
CB_PERF_SEL_FC_CC_QUADFRAG_VALIDB_READY  = 0x0000002e,
CB_PERF_SEL_FC_CC_QUADFRAG_VALIDB_READYB  = 0x0000002f,
CB_PERF_SEL_CC_IB_SR_FRAG_VALID_READY    = 0x00000030,
CB_PERF_SEL_CC_IB_SR_FRAG_VALID_READYB   = 0x00000031,
CB_PERF_SEL_CC_IB_SR_FRAG_VALIDB_READY   = 0x00000032,
CB_PERF_SEL_CC_IB_SR_FRAG_VALIDB_READYB  = 0x00000033,
CB_PERF_SEL_CC_IB_TB_FRAG_VALID_READY    = 0x00000034,
CB_PERF_SEL_CC_IB_TB_FRAG_VALID_READYB   = 0x00000035,
CB_PERF_SEL_CC_IB_TB_FRAG_VALIDB_READY   = 0x00000036,
CB_PERF_SEL_CC_IB_TB_FRAG_VALIDB_READYB  = 0x00000037,
CB_PERF_SEL_CC_RB_BC_EVENFRAG_VALID_READY  = 0x00000038,
CB_PERF_SEL_CC_RB_BC_EVENFRAG_VALID_READYB  = 0x00000039,
CB_PERF_SEL_CC_RB_BC_EVENFRAG_VALIDB_READY  = 0x0000003a,
CB_PERF_SEL_CC_RB_BC_EVENFRAG_VALIDB_READYB  = 0x0000003b,
CB_PERF_SEL_CC_RB_BC_ODDFRAG_VALID_READY  = 0x0000003c,
CB_PERF_SEL_CC_RB_BC_ODDFRAG_VALID_READYB  = 0x0000003d,
CB_PERF_SEL_CC_RB_BC_ODDFRAG_VALIDB_READY  = 0x0000003e,
CB_PERF_SEL_CC_RB_BC_ODDFRAG_VALIDB_READYB  = 0x0000003f,
CB_PERF_SEL_CC_BC_CS_FRAG_VALID          = 0x00000040,
CB_PERF_SEL_CM_CACHE_HIT                 = 0x00000041,
CB_PERF_SEL_CM_CACHE_TAG_MISS            = 0x00000042,
CB_PERF_SEL_CM_CACHE_SECTOR_MISS         = 0x00000043,
CB_PERF_SEL_CM_CACHE_REEVICTION_STALL    = 0x00000044,
CB_PERF_SEL_CM_CACHE_EVICT_NONZERO_INFLIGHT_STALL  = 0x00000045,
CB_PERF_SEL_CM_CACHE_REPLACE_PENDING_EVICT_STALL  = 0x00000046,
CB_PERF_SEL_CM_CACHE_INFLIGHT_COUNTER_MAXIMUM_STALL  = 0x00000047,
CB_PERF_SEL_CM_CACHE_READ_OUTPUT_STALL   = 0x00000048,
CB_PERF_SEL_CM_CACHE_WRITE_OUTPUT_STALL  = 0x00000049,
CB_PERF_SEL_CM_CACHE_ACK_OUTPUT_STALL    = 0x0000004a,
CB_PERF_SEL_CM_CACHE_STALL               = 0x0000004b,
CB_PERF_SEL_CM_CACHE_FLUSH               = 0x0000004c,
CB_PERF_SEL_CM_CACHE_TAGS_FLUSHED        = 0x0000004d,
CB_PERF_SEL_CM_CACHE_SECTORS_FLUSHED     = 0x0000004e,
CB_PERF_SEL_CM_CACHE_DIRTY_SECTORS_FLUSHED  = 0x0000004f,
CB_PERF_SEL_FC_CACHE_HIT                 = 0x00000050,
CB_PERF_SEL_FC_CACHE_TAG_MISS            = 0x00000051,
CB_PERF_SEL_FC_CACHE_SECTOR_MISS         = 0x00000052,
CB_PERF_SEL_FC_CACHE_REEVICTION_STALL    = 0x00000053,
CB_PERF_SEL_FC_CACHE_EVICT_NONZERO_INFLIGHT_STALL  = 0x00000054,
CB_PERF_SEL_FC_CACHE_REPLACE_PENDING_EVICT_STALL  = 0x00000055,
CB_PERF_SEL_FC_CACHE_INFLIGHT_COUNTER_MAXIMUM_STALL  = 0x00000056,
CB_PERF_SEL_FC_CACHE_READ_OUTPUT_STALL   = 0x00000057,
CB_PERF_SEL_FC_CACHE_WRITE_OUTPUT_STALL  = 0x00000058,
CB_PERF_SEL_FC_CACHE_ACK_OUTPUT_STALL    = 0x00000059,
CB_PERF_SEL_FC_CACHE_STALL               = 0x0000005a,
CB_PERF_SEL_FC_CACHE_FLUSH               = 0x0000005b,
CB_PERF_SEL_FC_CACHE_TAGS_FLUSHED        = 0x0000005c,
CB_PERF_SEL_FC_CACHE_SECTORS_FLUSHED     = 0x0000005d,
CB_PERF_SEL_FC_CACHE_DIRTY_SECTORS_FLUSHED  = 0x0000005e,
CB_PERF_SEL_CC_CACHE_HIT                 = 0x0000005f,
CB_PERF_SEL_CC_CACHE_TAG_MISS            = 0x00000060,
CB_PERF_SEL_CC_CACHE_SECTOR_MISS         = 0x00000061,
CB_PERF_SEL_CC_CACHE_REEVICTION_STALL    = 0x00000062,
CB_PERF_SEL_CC_CACHE_EVICT_NONZERO_INFLIGHT_STALL  = 0x00000063,
CB_PERF_SEL_CC_CACHE_REPLACE_PENDING_EVICT_STALL  = 0x00000064,
CB_PERF_SEL_CC_CACHE_INFLIGHT_COUNTER_MAXIMUM_STALL  = 0x00000065,
CB_PERF_SEL_CC_CACHE_READ_OUTPUT_STALL   = 0x00000066,
CB_PERF_SEL_CC_CACHE_WRITE_OUTPUT_STALL  = 0x00000067,
CB_PERF_SEL_CC_CACHE_ACK_OUTPUT_STALL    = 0x00000068,
CB_PERF_SEL_CC_CACHE_STALL               = 0x00000069,
CB_PERF_SEL_CC_CACHE_FLUSH               = 0x0000006a,
CB_PERF_SEL_CC_CACHE_TAGS_FLUSHED        = 0x0000006b,
CB_PERF_SEL_CC_CACHE_SECTORS_FLUSHED     = 0x0000006c,
CB_PERF_SEL_CC_CACHE_DIRTY_SECTORS_FLUSHED  = 0x0000006d,
CB_PERF_SEL_CC_CACHE_WA_TO_RMW_CONVERSION  = 0x0000006e,
CB_PERF_SEL_CC_CACHE_READS_SAVED_DUE_TO_DCC  = 0x0000006f,
CB_PERF_SEL_CB_TAP_WRREQ_VALID_READY     = 0x00000070,
CB_PERF_SEL_CB_TAP_WRREQ_VALID_READYB    = 0x00000071,
CB_PERF_SEL_CB_TAP_WRREQ_VALIDB_READY    = 0x00000072,
CB_PERF_SEL_CB_TAP_WRREQ_VALIDB_READYB   = 0x00000073,
CB_PERF_SEL_CM_MC_WRITE_REQUEST          = 0x00000074,
CB_PERF_SEL_FC_MC_WRITE_REQUEST          = 0x00000075,
CB_PERF_SEL_CC_MC_WRITE_REQUEST          = 0x00000076,
CB_PERF_SEL_CM_MC_WRITE_REQUESTS_IN_FLIGHT  = 0x00000077,
CB_PERF_SEL_FC_MC_WRITE_REQUESTS_IN_FLIGHT  = 0x00000078,
CB_PERF_SEL_CC_MC_WRITE_REQUESTS_IN_FLIGHT  = 0x00000079,
CB_PERF_SEL_CB_TAP_RDREQ_VALID_READY     = 0x0000007a,
CB_PERF_SEL_CB_TAP_RDREQ_VALID_READYB    = 0x0000007b,
CB_PERF_SEL_CB_TAP_RDREQ_VALIDB_READY    = 0x0000007c,
CB_PERF_SEL_CB_TAP_RDREQ_VALIDB_READYB   = 0x0000007d,
CB_PERF_SEL_CM_MC_READ_REQUEST           = 0x0000007e,
CB_PERF_SEL_FC_MC_READ_REQUEST           = 0x0000007f,
CB_PERF_SEL_CC_MC_READ_REQUEST           = 0x00000080,
CB_PERF_SEL_CM_MC_READ_REQUESTS_IN_FLIGHT  = 0x00000081,
CB_PERF_SEL_FC_MC_READ_REQUESTS_IN_FLIGHT  = 0x00000082,
CB_PERF_SEL_CC_MC_READ_REQUESTS_IN_FLIGHT  = 0x00000083,
CB_PERF_SEL_CM_TQ_FULL                   = 0x00000084,
CB_PERF_SEL_CM_TQ_FIFO_TILE_RESIDENCY_STALL  = 0x00000085,
CB_PERF_SEL_FC_QUAD_RDLAT_FIFO_FULL      = 0x00000086,
CB_PERF_SEL_FC_TILE_RDLAT_FIFO_FULL      = 0x00000087,
CB_PERF_SEL_FC_RDLAT_FIFO_QUAD_RESIDENCY_STALL  = 0x00000088,
CB_PERF_SEL_FOP_FMASK_RAW_STALL          = 0x00000089,
CB_PERF_SEL_FOP_FMASK_BYPASS_STALL       = 0x0000008a,
CB_PERF_SEL_CC_SF_FULL                   = 0x0000008b,
CB_PERF_SEL_CC_RB_FULL                   = 0x0000008c,
CB_PERF_SEL_CC_EVENFIFO_QUAD_RESIDENCY_STALL  = 0x0000008d,
CB_PERF_SEL_CC_ODDFIFO_QUAD_RESIDENCY_STALL  = 0x0000008e,
CB_PERF_SEL_BLENDER_RAW_HAZARD_STALL     = 0x0000008f,
CB_PERF_SEL_EVENT                        = 0x00000090,
CB_PERF_SEL_EVENT_CACHE_FLUSH_TS         = 0x00000091,
CB_PERF_SEL_EVENT_CONTEXT_DONE           = 0x00000092,
CB_PERF_SEL_EVENT_CACHE_FLUSH            = 0x00000093,
CB_PERF_SEL_EVENT_CACHE_FLUSH_AND_INV_TS_EVENT  = 0x00000094,
CB_PERF_SEL_EVENT_CACHE_FLUSH_AND_INV_EVENT  = 0x00000095,
CB_PERF_SEL_EVENT_FLUSH_AND_INV_CB_DATA_TS  = 0x00000096,
CB_PERF_SEL_EVENT_FLUSH_AND_INV_CB_META  = 0x00000097,
CB_PERF_SEL_CC_SURFACE_SYNC              = 0x00000098,
CB_PERF_SEL_CMASK_READ_DATA_0xC          = 0x00000099,
CB_PERF_SEL_CMASK_READ_DATA_0xD          = 0x0000009a,
CB_PERF_SEL_CMASK_READ_DATA_0xE          = 0x0000009b,
CB_PERF_SEL_CMASK_READ_DATA_0xF          = 0x0000009c,
CB_PERF_SEL_CMASK_WRITE_DATA_0xC         = 0x0000009d,
CB_PERF_SEL_CMASK_WRITE_DATA_0xD         = 0x0000009e,
CB_PERF_SEL_CMASK_WRITE_DATA_0xE         = 0x0000009f,
CB_PERF_SEL_CMASK_WRITE_DATA_0xF         = 0x000000a0,
CB_PERF_SEL_TWO_PROBE_QUAD_FRAGMENT      = 0x000000a1,
CB_PERF_SEL_EXPORT_32_ABGR_QUAD_FRAGMENT  = 0x000000a2,
CB_PERF_SEL_DUAL_SOURCE_COLOR_QUAD_FRAGMENT  = 0x000000a3,
CB_PERF_SEL_QUAD_HAS_1_FRAGMENT_BEFORE_UPDATE  = 0x000000a4,
CB_PERF_SEL_QUAD_HAS_2_FRAGMENTS_BEFORE_UPDATE  = 0x000000a5,
CB_PERF_SEL_QUAD_HAS_3_FRAGMENTS_BEFORE_UPDATE  = 0x000000a6,
CB_PERF_SEL_QUAD_HAS_4_FRAGMENTS_BEFORE_UPDATE  = 0x000000a7,
CB_PERF_SEL_QUAD_HAS_5_FRAGMENTS_BEFORE_UPDATE  = 0x000000a8,
CB_PERF_SEL_QUAD_HAS_6_FRAGMENTS_BEFORE_UPDATE  = 0x000000a9,
CB_PERF_SEL_QUAD_HAS_7_FRAGMENTS_BEFORE_UPDATE  = 0x000000aa,
CB_PERF_SEL_QUAD_HAS_8_FRAGMENTS_BEFORE_UPDATE  = 0x000000ab,
CB_PERF_SEL_QUAD_HAS_1_FRAGMENT_AFTER_UPDATE  = 0x000000ac,
CB_PERF_SEL_QUAD_HAS_2_FRAGMENTS_AFTER_UPDATE  = 0x000000ad,
CB_PERF_SEL_QUAD_HAS_3_FRAGMENTS_AFTER_UPDATE  = 0x000000ae,
CB_PERF_SEL_QUAD_HAS_4_FRAGMENTS_AFTER_UPDATE  = 0x000000af,
CB_PERF_SEL_QUAD_HAS_5_FRAGMENTS_AFTER_UPDATE  = 0x000000b0,
CB_PERF_SEL_QUAD_HAS_6_FRAGMENTS_AFTER_UPDATE  = 0x000000b1,
CB_PERF_SEL_QUAD_HAS_7_FRAGMENTS_AFTER_UPDATE  = 0x000000b2,
CB_PERF_SEL_QUAD_HAS_8_FRAGMENTS_AFTER_UPDATE  = 0x000000b3,
CB_PERF_SEL_QUAD_ADDED_1_FRAGMENT        = 0x000000b4,
CB_PERF_SEL_QUAD_ADDED_2_FRAGMENTS       = 0x000000b5,
CB_PERF_SEL_QUAD_ADDED_3_FRAGMENTS       = 0x000000b6,
CB_PERF_SEL_QUAD_ADDED_4_FRAGMENTS       = 0x000000b7,
CB_PERF_SEL_QUAD_ADDED_5_FRAGMENTS       = 0x000000b8,
CB_PERF_SEL_QUAD_ADDED_6_FRAGMENTS       = 0x000000b9,
CB_PERF_SEL_QUAD_ADDED_7_FRAGMENTS       = 0x000000ba,
CB_PERF_SEL_QUAD_REMOVED_1_FRAGMENT      = 0x000000bb,
CB_PERF_SEL_QUAD_REMOVED_2_FRAGMENTS     = 0x000000bc,
CB_PERF_SEL_QUAD_REMOVED_3_FRAGMENTS     = 0x000000bd,
CB_PERF_SEL_QUAD_REMOVED_4_FRAGMENTS     = 0x000000be,
CB_PERF_SEL_QUAD_REMOVED_5_FRAGMENTS     = 0x000000bf,
CB_PERF_SEL_QUAD_REMOVED_6_FRAGMENTS     = 0x000000c0,
CB_PERF_SEL_QUAD_REMOVED_7_FRAGMENTS     = 0x000000c1,
CB_PERF_SEL_QUAD_READS_FRAGMENT_0        = 0x000000c2,
CB_PERF_SEL_QUAD_READS_FRAGMENT_1        = 0x000000c3,
CB_PERF_SEL_QUAD_READS_FRAGMENT_2        = 0x000000c4,
CB_PERF_SEL_QUAD_READS_FRAGMENT_3        = 0x000000c5,
CB_PERF_SEL_QUAD_READS_FRAGMENT_4        = 0x000000c6,
CB_PERF_SEL_QUAD_READS_FRAGMENT_5        = 0x000000c7,
CB_PERF_SEL_QUAD_READS_FRAGMENT_6        = 0x000000c8,
CB_PERF_SEL_QUAD_READS_FRAGMENT_7        = 0x000000c9,
CB_PERF_SEL_QUAD_WRITES_FRAGMENT_0       = 0x000000ca,
CB_PERF_SEL_QUAD_WRITES_FRAGMENT_1       = 0x000000cb,
CB_PERF_SEL_QUAD_WRITES_FRAGMENT_2       = 0x000000cc,
CB_PERF_SEL_QUAD_WRITES_FRAGMENT_3       = 0x000000cd,
CB_PERF_SEL_QUAD_WRITES_FRAGMENT_4       = 0x000000ce,
CB_PERF_SEL_QUAD_WRITES_FRAGMENT_5       = 0x000000cf,
CB_PERF_SEL_QUAD_WRITES_FRAGMENT_6       = 0x000000d0,
CB_PERF_SEL_QUAD_WRITES_FRAGMENT_7       = 0x000000d1,
CB_PERF_SEL_QUAD_BLEND_OPT_DONT_READ_DST  = 0x000000d2,
CB_PERF_SEL_QUAD_BLEND_OPT_BLEND_BYPASS  = 0x000000d3,
CB_PERF_SEL_QUAD_BLEND_OPT_DISCARD_PIXELS  = 0x000000d4,
CB_PERF_SEL_QUAD_DST_READ_COULD_HAVE_BEEN_OPTIMIZED  = 0x000000d5,
CB_PERF_SEL_QUAD_BLENDING_COULD_HAVE_BEEN_BYPASSED  = 0x000000d6,
CB_PERF_SEL_QUAD_COULD_HAVE_BEEN_DISCARDED  = 0x000000d7,
CB_PERF_SEL_BLEND_OPT_PIXELS_RESULT_EQ_DEST  = 0x000000d8,
CB_PERF_SEL_DRAWN_BUSY                   = 0x000000d9,
CB_PERF_SEL_TILE_TO_CMR_REGION_BUSY      = 0x000000da,
CB_PERF_SEL_CMR_TO_FCR_REGION_BUSY       = 0x000000db,
CB_PERF_SEL_FCR_TO_CCR_REGION_BUSY       = 0x000000dc,
CB_PERF_SEL_CCR_TO_CCW_REGION_BUSY       = 0x000000dd,
CB_PERF_SEL_FC_PF_SLOW_MODE_QUAD_EMPTY_HALF_DROPPED  = 0x000000de,
CB_PERF_SEL_FC_SEQUENCER_CLEAR           = 0x000000df,
CB_PERF_SEL_FC_SEQUENCER_ELIMINATE_FAST_CLEAR  = 0x000000e0,
CB_PERF_SEL_FC_SEQUENCER_FMASK_DECOMPRESS  = 0x000000e1,
CB_PERF_SEL_FC_SEQUENCER_FMASK_COMPRESSION_DISABLE  = 0x000000e2,
CB_PERF_SEL_FC_KEYID_RDLAT_FIFO_FULL     = 0x000000e3,
CB_PERF_SEL_FC_DOC_IS_STALLED            = 0x000000e4,
CB_PERF_SEL_FC_DOC_MRTS_NOT_COMBINED     = 0x000000e5,
CB_PERF_SEL_FC_DOC_MRTS_COMBINED         = 0x000000e6,
CB_PERF_SEL_FC_DOC_QTILE_CAM_MISS        = 0x000000e7,
CB_PERF_SEL_FC_DOC_QTILE_CAM_HIT         = 0x000000e8,
CB_PERF_SEL_FC_DOC_CLINE_CAM_MISS        = 0x000000e9,
CB_PERF_SEL_FC_DOC_CLINE_CAM_HIT         = 0x000000ea,
CB_PERF_SEL_FC_DOC_QUAD_PTR_FIFO_IS_FULL  = 0x000000eb,
CB_PERF_SEL_FC_DOC_OVERWROTE_1_SECTOR    = 0x000000ec,
CB_PERF_SEL_FC_DOC_OVERWROTE_2_SECTORS   = 0x000000ed,
CB_PERF_SEL_FC_DOC_OVERWROTE_3_SECTORS   = 0x000000ee,
CB_PERF_SEL_FC_DOC_OVERWROTE_4_SECTORS   = 0x000000ef,
CB_PERF_SEL_FC_DOC_TOTAL_OVERWRITTEN_SECTORS  = 0x000000f0,
CB_PERF_SEL_FC_DCC_CACHE_HIT             = 0x000000f1,
CB_PERF_SEL_FC_DCC_CACHE_TAG_MISS        = 0x000000f2,
CB_PERF_SEL_FC_DCC_CACHE_SECTOR_MISS     = 0x000000f3,
CB_PERF_SEL_FC_DCC_CACHE_REEVICTION_STALL  = 0x000000f4,
CB_PERF_SEL_FC_DCC_CACHE_EVICT_NONZERO_INFLIGHT_STALL  = 0x000000f5,
CB_PERF_SEL_FC_DCC_CACHE_REPLACE_PENDING_EVICT_STALL  = 0x000000f6,
CB_PERF_SEL_FC_DCC_CACHE_INFLIGHT_COUNTER_MAXIMUM_STALL  = 0x000000f7,
CB_PERF_SEL_FC_DCC_CACHE_READ_OUTPUT_STALL  = 0x000000f8,
CB_PERF_SEL_FC_DCC_CACHE_WRITE_OUTPUT_STALL  = 0x000000f9,
CB_PERF_SEL_FC_DCC_CACHE_ACK_OUTPUT_STALL  = 0x000000fa,
CB_PERF_SEL_FC_DCC_CACHE_STALL           = 0x000000fb,
CB_PERF_SEL_FC_DCC_CACHE_FLUSH           = 0x000000fc,
CB_PERF_SEL_FC_DCC_CACHE_TAGS_FLUSHED    = 0x000000fd,
CB_PERF_SEL_FC_DCC_CACHE_SECTORS_FLUSHED  = 0x000000fe,
CB_PERF_SEL_FC_DCC_CACHE_DIRTY_SECTORS_FLUSHED  = 0x000000ff,
CB_PERF_SEL_CC_DCC_BEYOND_TILE_SPLIT     = 0x00000100,
CB_PERF_SEL_FC_MC_DCC_WRITE_REQUEST      = 0x00000101,
CB_PERF_SEL_FC_MC_DCC_WRITE_REQUESTS_IN_FLIGHT  = 0x00000102,
CB_PERF_SEL_FC_MC_DCC_READ_REQUEST       = 0x00000103,
CB_PERF_SEL_FC_MC_DCC_READ_REQUESTS_IN_FLIGHT  = 0x00000104,
CB_PERF_SEL_CC_DCC_RDREQ_STALL           = 0x00000105,
CB_PERF_SEL_CC_DCC_DECOMPRESS_TIDS_IN    = 0x00000106,
CB_PERF_SEL_CC_DCC_DECOMPRESS_TIDS_OUT   = 0x00000107,
CB_PERF_SEL_CC_DCC_COMPRESS_TIDS_IN      = 0x00000108,
CB_PERF_SEL_CC_DCC_COMPRESS_TIDS_OUT     = 0x00000109,
CB_PERF_SEL_FC_DCC_KEY_VALUE__CLEAR      = 0x0000010a,
CB_PERF_SEL_CC_DCC_KEY_VALUE__4_BLOCKS__2TO1  = 0x0000010b,
CB_PERF_SEL_CC_DCC_KEY_VALUE__3BLOCKS_2TO1__1BLOCK_2TO2  = 0x0000010c,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_2TO1__1BLOCK_2TO2__1BLOCK_2TO1  = 0x0000010d,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_2TO2__2BLOCKS_2TO1  = 0x0000010e,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__3BLOCKS_2TO1  = 0x0000010f,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_2TO1__2BLOCKS_2TO2  = 0x00000110,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__2BLOCKS_2TO2__1BLOCK_2TO1  = 0x00000111,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_2TO2__1BLOCK_2TO1__1BLOCK_2TO2  = 0x00000112,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_2TO1__1BLOCK_2TO2__1BLOCK_2TO1  = 0x00000113,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_2TO2__2BLOCKS_2TO1  = 0x00000114,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__2BLOCKS_2TO1__1BLOCK_2TO2  = 0x00000115,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__3BLOCKS_2TO2  = 0x00000116,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_2TO1__2BLOCKS_2TO2  = 0x00000117,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_2TO2__1BLOCK_2TO1__1BLOCK_2TO2  = 0x00000118,
CB_PERF_SEL_CC_DCC_KEY_VALUE__3BLOCKS_2TO2__1BLOCK_2TO1  = 0x00000119,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_4TO1  = 0x0000011a,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO1__1BLOCK_4TO2  = 0x0000011b,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO1__1BLOCK_4TO3  = 0x0000011c,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO1__1BLOCK_4TO4  = 0x0000011d,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO2__1BLOCK_4TO1  = 0x0000011e,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_4TO2  = 0x0000011f,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO2__1BLOCK_4TO3  = 0x00000120,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO2__1BLOCK_4TO4  = 0x00000121,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO3__1BLOCK_4TO1  = 0x00000122,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO3__1BLOCK_4TO2  = 0x00000123,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_4TO3  = 0x00000124,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO3__1BLOCK_4TO4  = 0x00000125,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO4__1BLOCK_4TO1  = 0x00000126,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO4__1BLOCK_4TO2  = 0x00000127,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO4__1BLOCK_4TO3  = 0x00000128,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_2TO1__1BLOCK_4TO1  = 0x00000129,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_2TO1__1BLOCK_4TO2  = 0x0000012a,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_2TO1__1BLOCK_4TO3  = 0x0000012b,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_2TO1__1BLOCK_4TO4  = 0x0000012c,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_2TO2__1BLOCK_4TO1  = 0x0000012d,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_2TO2__1BLOCK_4TO2  = 0x0000012e,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_2TO2__1BLOCK_4TO3  = 0x0000012f,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_2TO2__1BLOCK_4TO4  = 0x00000130,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_2TO1__1BLOCK_4TO1  = 0x00000131,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_2TO1__1BLOCK_4TO2  = 0x00000132,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_2TO1__1BLOCK_4TO3  = 0x00000133,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_2TO1__1BLOCK_4TO4  = 0x00000134,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_2TO2__1BLOCK_4TO1  = 0x00000135,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_2TO2__1BLOCK_4TO2  = 0x00000136,
CB_PERF_SEL_CC_DCC_KEY_VALUE__2BLOCKS_2TO2__1BLOCK_4TO3  = 0x00000137,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_4TO1__1BLOCK_2TO1  = 0x00000138,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_4TO2__1BLOCK_2TO1  = 0x00000139,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_4TO3__1BLOCK_2TO1  = 0x0000013a,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_4TO4__1BLOCK_2TO1  = 0x0000013b,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_4TO1__1BLOCK_2TO1  = 0x0000013c,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_4TO2__1BLOCK_2TO1  = 0x0000013d,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_4TO3__1BLOCK_2TO1  = 0x0000013e,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_4TO4__1BLOCK_2TO1  = 0x0000013f,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_4TO1__1BLOCK_2TO2  = 0x00000140,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_4TO2__1BLOCK_2TO2  = 0x00000141,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_4TO3__1BLOCK_2TO2  = 0x00000142,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_4TO4__1BLOCK_2TO2  = 0x00000143,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_4TO1__1BLOCK_2TO2  = 0x00000144,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_4TO2__1BLOCK_2TO2  = 0x00000145,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_4TO3__1BLOCK_2TO2  = 0x00000146,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO1__2BLOCKS_2TO1  = 0x00000147,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO2__2BLOCKS_2TO1  = 0x00000148,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO3__2BLOCKS_2TO1  = 0x00000149,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO4__2BLOCKS_2TO1  = 0x0000014a,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO1__2BLOCKS_2TO2  = 0x0000014b,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO2__2BLOCKS_2TO2  = 0x0000014c,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO3__2BLOCKS_2TO2  = 0x0000014d,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO1__1BLOCK_2TO1__1BLOCK_2TO2  = 0x0000014e,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO2__1BLOCK_2TO1__1BLOCK_2TO2  = 0x0000014f,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO3__1BLOCK_2TO1__1BLOCK_2TO2  = 0x00000150,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO4__1BLOCK_2TO1__1BLOCK_2TO2  = 0x00000151,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO1__1BLOCK_2TO2__1BLOCK_2TO1  = 0x00000152,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO2__1BLOCK_2TO2__1BLOCK_2TO1  = 0x00000153,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO3__1BLOCK_2TO2__1BLOCK_2TO1  = 0x00000154,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_4TO4__1BLOCK_2TO2__1BLOCK_2TO1  = 0x00000155,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_6TO1  = 0x00000156,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_6TO2  = 0x00000157,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_6TO3  = 0x00000158,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_6TO4  = 0x00000159,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_6TO5  = 0x0000015a,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__1BLOCK_6TO6  = 0x0000015b,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__INV0  = 0x0000015c,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO1__INV1  = 0x0000015d,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_6TO1  = 0x0000015e,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_6TO2  = 0x0000015f,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_6TO3  = 0x00000160,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_6TO4  = 0x00000161,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__1BLOCK_6TO5  = 0x00000162,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__INV0  = 0x00000163,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_2TO2__INV1  = 0x00000164,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_6TO1__1BLOCK_2TO1  = 0x00000165,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_6TO2__1BLOCK_2TO1  = 0x00000166,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_6TO3__1BLOCK_2TO1  = 0x00000167,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_6TO4__1BLOCK_2TO1  = 0x00000168,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_6TO5__1BLOCK_2TO1  = 0x00000169,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_6TO6__1BLOCK_2TO1  = 0x0000016a,
CB_PERF_SEL_CC_DCC_KEY_VALUE__INV0__1BLOCK_2TO1  = 0x0000016b,
CB_PERF_SEL_CC_DCC_KEY_VALUE__INV1__1BLOCK_2TO1  = 0x0000016c,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_6TO1__1BLOCK_2TO2  = 0x0000016d,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_6TO2__1BLOCK_2TO2  = 0x0000016e,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_6TO3__1BLOCK_2TO2  = 0x0000016f,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_6TO4__1BLOCK_2TO2  = 0x00000170,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_6TO5__1BLOCK_2TO2  = 0x00000171,
CB_PERF_SEL_CC_DCC_KEY_VALUE__INV0__1BLOCK_2TO2  = 0x00000172,
CB_PERF_SEL_CC_DCC_KEY_VALUE__INV1__1BLOCK_2TO2  = 0x00000173,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_8TO1  = 0x00000174,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_8TO2  = 0x00000175,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_8TO3  = 0x00000176,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_8TO4  = 0x00000177,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_8TO5  = 0x00000178,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_8TO6  = 0x00000179,
CB_PERF_SEL_CC_DCC_KEY_VALUE__1BLOCK_8TO7  = 0x0000017a,
CB_PERF_SEL_CC_DCC_KEY_VALUE__UNCOMPRESSED  = 0x0000017b,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_2TO1   = 0x0000017c,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_4TO1   = 0x0000017d,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_4TO2   = 0x0000017e,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_4TO3   = 0x0000017f,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_6TO1   = 0x00000180,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_6TO2   = 0x00000181,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_6TO3   = 0x00000182,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_6TO4   = 0x00000183,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_6TO5   = 0x00000184,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_8TO1   = 0x00000185,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_8TO2   = 0x00000186,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_8TO3   = 0x00000187,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_8TO4   = 0x00000188,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_8TO5   = 0x00000189,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_8TO6   = 0x0000018a,
CB_PERF_SEL_CC_DCC_COMPRESS_RATIO_8TO7   = 0x0000018b,
CB_PERF_SEL_RBP_EXPORT_8PIX_LIT_BOTH     = 0x0000018c,
CB_PERF_SEL_RBP_EXPORT_8PIX_LIT_LEFT     = 0x0000018d,
CB_PERF_SEL_RBP_EXPORT_8PIX_LIT_RIGHT    = 0x0000018e,
CB_PERF_SEL_RBP_SPLIT_MICROTILE          = 0x0000018f,
CB_PERF_SEL_RBP_SPLIT_AA_SAMPLE_MASK     = 0x00000190,
CB_PERF_SEL_RBP_SPLIT_PARTIAL_TARGET_MASK  = 0x00000191,
CB_PERF_SEL_RBP_SPLIT_LINEAR_ADDRESSING  = 0x00000192,
CB_PERF_SEL_RBP_SPLIT_AA_NO_FMASK_COMPRESS  = 0x00000193,
CB_PERF_SEL_RBP_INSERT_MISSING_LAST_QUAD  = 0x00000194,
} CBPerfSel;

/*
 * CBPerfOpFilterSel enum
 */

typedef enum CBPerfOpFilterSel {
CB_PERF_OP_FILTER_SEL_WRITE_ONLY         = 0x00000000,
CB_PERF_OP_FILTER_SEL_NEEDS_DESTINATION  = 0x00000001,
CB_PERF_OP_FILTER_SEL_RESOLVE            = 0x00000002,
CB_PERF_OP_FILTER_SEL_DECOMPRESS         = 0x00000003,
CB_PERF_OP_FILTER_SEL_FMASK_DECOMPRESS   = 0x00000004,
CB_PERF_OP_FILTER_SEL_ELIMINATE_FAST_CLEAR  = 0x00000005,
} CBPerfOpFilterSel;

/*
 * CBPerfClearFilterSel enum
 */

typedef enum CBPerfClearFilterSel {
CB_PERF_CLEAR_FILTER_SEL_NONCLEAR        = 0x00000000,
CB_PERF_CLEAR_FILTER_SEL_CLEAR           = 0x00000001,
} CBPerfClearFilterSel;

/*******************************************************
 * TC Enums
 *******************************************************/

/*
 * TC_OP_MASKS enum
 */

typedef enum TC_OP_MASKS {
TC_OP_MASK_FLUSH_DENROM                  = 0x00000008,
TC_OP_MASK_64                            = 0x00000020,
TC_OP_MASK_NO_RTN                        = 0x00000040,
} TC_OP_MASKS;

/*
 * TC_OP enum
 */

typedef enum TC_OP {
TC_OP_READ                               = 0x00000000,
TC_OP_ATOMIC_FCMPSWAP_RTN_32             = 0x00000001,
TC_OP_ATOMIC_FMIN_RTN_32                 = 0x00000002,
TC_OP_ATOMIC_FMAX_RTN_32                 = 0x00000003,
TC_OP_RESERVED_FOP_RTN_32_0              = 0x00000004,
TC_OP_RESERVED_FOP_RTN_32_1              = 0x00000005,
TC_OP_RESERVED_FOP_RTN_32_2              = 0x00000006,
TC_OP_ATOMIC_SWAP_RTN_32                 = 0x00000007,
TC_OP_ATOMIC_CMPSWAP_RTN_32              = 0x00000008,
TC_OP_ATOMIC_FCMPSWAP_FLUSH_DENORM_RTN_32  = 0x00000009,
TC_OP_ATOMIC_FMIN_FLUSH_DENORM_RTN_32    = 0x0000000a,
TC_OP_ATOMIC_FMAX_FLUSH_DENORM_RTN_32    = 0x0000000b,
TC_OP_PROBE_FILTER                       = 0x0000000c,
TC_OP_RESERVED_FOP_FLUSH_DENORM_RTN_32_1  = 0x0000000d,
TC_OP_RESERVED_FOP_FLUSH_DENORM_RTN_32_2  = 0x0000000e,
TC_OP_ATOMIC_ADD_RTN_32                  = 0x0000000f,
TC_OP_ATOMIC_SUB_RTN_32                  = 0x00000010,
TC_OP_ATOMIC_SMIN_RTN_32                 = 0x00000011,
TC_OP_ATOMIC_UMIN_RTN_32                 = 0x00000012,
TC_OP_ATOMIC_SMAX_RTN_32                 = 0x00000013,
TC_OP_ATOMIC_UMAX_RTN_32                 = 0x00000014,
TC_OP_ATOMIC_AND_RTN_32                  = 0x00000015,
TC_OP_ATOMIC_OR_RTN_32                   = 0x00000016,
TC_OP_ATOMIC_XOR_RTN_32                  = 0x00000017,
TC_OP_ATOMIC_INC_RTN_32                  = 0x00000018,
TC_OP_ATOMIC_DEC_RTN_32                  = 0x00000019,
TC_OP_WBINVL1_VOL                        = 0x0000001a,
TC_OP_WBINVL1_SD                         = 0x0000001b,
TC_OP_RESERVED_NON_FLOAT_RTN_32_0        = 0x0000001c,
TC_OP_RESERVED_NON_FLOAT_RTN_32_1        = 0x0000001d,
TC_OP_RESERVED_NON_FLOAT_RTN_32_2        = 0x0000001e,
TC_OP_RESERVED_NON_FLOAT_RTN_32_3        = 0x0000001f,
TC_OP_WRITE                              = 0x00000020,
TC_OP_ATOMIC_FCMPSWAP_RTN_64             = 0x00000021,
TC_OP_ATOMIC_FMIN_RTN_64                 = 0x00000022,
TC_OP_ATOMIC_FMAX_RTN_64                 = 0x00000023,
TC_OP_RESERVED_FOP_RTN_64_0              = 0x00000024,
TC_OP_RESERVED_FOP_RTN_64_1              = 0x00000025,
TC_OP_RESERVED_FOP_RTN_64_2              = 0x00000026,
TC_OP_ATOMIC_SWAP_RTN_64                 = 0x00000027,
TC_OP_ATOMIC_CMPSWAP_RTN_64              = 0x00000028,
TC_OP_ATOMIC_FCMPSWAP_FLUSH_DENORM_RTN_64  = 0x00000029,
TC_OP_ATOMIC_FMIN_FLUSH_DENORM_RTN_64    = 0x0000002a,
TC_OP_ATOMIC_FMAX_FLUSH_DENORM_RTN_64    = 0x0000002b,
TC_OP_WBINVL2_SD                         = 0x0000002c,
TC_OP_RESERVED_FOP_FLUSH_DENORM_RTN_64_0  = 0x0000002d,
TC_OP_RESERVED_FOP_FLUSH_DENORM_RTN_64_1  = 0x0000002e,
TC_OP_ATOMIC_ADD_RTN_64                  = 0x0000002f,
TC_OP_ATOMIC_SUB_RTN_64                  = 0x00000030,
TC_OP_ATOMIC_SMIN_RTN_64                 = 0x00000031,
TC_OP_ATOMIC_UMIN_RTN_64                 = 0x00000032,
TC_OP_ATOMIC_SMAX_RTN_64                 = 0x00000033,
TC_OP_ATOMIC_UMAX_RTN_64                 = 0x00000034,
TC_OP_ATOMIC_AND_RTN_64                  = 0x00000035,
TC_OP_ATOMIC_OR_RTN_64                   = 0x00000036,
TC_OP_ATOMIC_XOR_RTN_64                  = 0x00000037,
TC_OP_ATOMIC_INC_RTN_64                  = 0x00000038,
TC_OP_ATOMIC_DEC_RTN_64                  = 0x00000039,
TC_OP_WBL2_NC                            = 0x0000003a,
TC_OP_WBL2_WC                            = 0x0000003b,
TC_OP_RESERVED_NON_FLOAT_RTN_64_1        = 0x0000003c,
TC_OP_RESERVED_NON_FLOAT_RTN_64_2        = 0x0000003d,
TC_OP_RESERVED_NON_FLOAT_RTN_64_3        = 0x0000003e,
TC_OP_RESERVED_NON_FLOAT_RTN_64_4        = 0x0000003f,
TC_OP_WBINVL1                            = 0x00000040,
TC_OP_ATOMIC_FCMPSWAP_32                 = 0x00000041,
TC_OP_ATOMIC_FMIN_32                     = 0x00000042,
TC_OP_ATOMIC_FMAX_32                     = 0x00000043,
TC_OP_RESERVED_FOP_32_0                  = 0x00000044,
TC_OP_RESERVED_FOP_32_1                  = 0x00000045,
TC_OP_RESERVED_FOP_32_2                  = 0x00000046,
TC_OP_ATOMIC_SWAP_32                     = 0x00000047,
TC_OP_ATOMIC_CMPSWAP_32                  = 0x00000048,
TC_OP_ATOMIC_FCMPSWAP_FLUSH_DENORM_32    = 0x00000049,
TC_OP_ATOMIC_FMIN_FLUSH_DENORM_32        = 0x0000004a,
TC_OP_ATOMIC_FMAX_FLUSH_DENORM_32        = 0x0000004b,
TC_OP_INV_METADATA                       = 0x0000004c,
TC_OP_RESERVED_FOP_FLUSH_DENORM_32_1     = 0x0000004d,
TC_OP_RESERVED_FOP_FLUSH_DENORM_32_2     = 0x0000004e,
TC_OP_ATOMIC_ADD_32                      = 0x0000004f,
TC_OP_ATOMIC_SUB_32                      = 0x00000050,
TC_OP_ATOMIC_SMIN_32                     = 0x00000051,
TC_OP_ATOMIC_UMIN_32                     = 0x00000052,
TC_OP_ATOMIC_SMAX_32                     = 0x00000053,
TC_OP_ATOMIC_UMAX_32                     = 0x00000054,
TC_OP_ATOMIC_AND_32                      = 0x00000055,
TC_OP_ATOMIC_OR_32                       = 0x00000056,
TC_OP_ATOMIC_XOR_32                      = 0x00000057,
TC_OP_ATOMIC_INC_32                      = 0x00000058,
TC_OP_ATOMIC_DEC_32                      = 0x00000059,
TC_OP_INVL2_NC                           = 0x0000005a,
TC_OP_NOP_RTN0                           = 0x0000005b,
TC_OP_RESERVED_NON_FLOAT_32_1            = 0x0000005c,
TC_OP_RESERVED_NON_FLOAT_32_2            = 0x0000005d,
TC_OP_RESERVED_NON_FLOAT_32_3            = 0x0000005e,
TC_OP_RESERVED_NON_FLOAT_32_4            = 0x0000005f,
TC_OP_WBINVL2                            = 0x00000060,
TC_OP_ATOMIC_FCMPSWAP_64                 = 0x00000061,
TC_OP_ATOMIC_FMIN_64                     = 0x00000062,
TC_OP_ATOMIC_FMAX_64                     = 0x00000063,
TC_OP_RESERVED_FOP_64_0                  = 0x00000064,
TC_OP_RESERVED_FOP_64_1                  = 0x00000065,
TC_OP_RESERVED_FOP_64_2                  = 0x00000066,
TC_OP_ATOMIC_SWAP_64                     = 0x00000067,
TC_OP_ATOMIC_CMPSWAP_64                  = 0x00000068,
TC_OP_ATOMIC_FCMPSWAP_FLUSH_DENORM_64    = 0x00000069,
TC_OP_ATOMIC_FMIN_FLUSH_DENORM_64        = 0x0000006a,
TC_OP_ATOMIC_FMAX_FLUSH_DENORM_64        = 0x0000006b,
TC_OP_RESERVED_FOP_FLUSH_DENORM_64_0     = 0x0000006c,
TC_OP_RESERVED_FOP_FLUSH_DENORM_64_1     = 0x0000006d,
TC_OP_RESERVED_FOP_FLUSH_DENORM_64_2     = 0x0000006e,
TC_OP_ATOMIC_ADD_64                      = 0x0000006f,
TC_OP_ATOMIC_SUB_64                      = 0x00000070,
TC_OP_ATOMIC_SMIN_64                     = 0x00000071,
TC_OP_ATOMIC_UMIN_64                     = 0x00000072,
TC_OP_ATOMIC_SMAX_64                     = 0x00000073,
TC_OP_ATOMIC_UMAX_64                     = 0x00000074,
TC_OP_ATOMIC_AND_64                      = 0x00000075,
TC_OP_ATOMIC_OR_64                       = 0x00000076,
TC_OP_ATOMIC_XOR_64                      = 0x00000077,
TC_OP_ATOMIC_INC_64                      = 0x00000078,
TC_OP_ATOMIC_DEC_64                      = 0x00000079,
TC_OP_WBINVL2_NC                         = 0x0000007a,
TC_OP_NOP_ACK                            = 0x0000007b,
TC_OP_RESERVED_NON_FLOAT_64_1            = 0x0000007c,
TC_OP_RESERVED_NON_FLOAT_64_2            = 0x0000007d,
TC_OP_RESERVED_NON_FLOAT_64_3            = 0x0000007e,
TC_OP_RESERVED_NON_FLOAT_64_4            = 0x0000007f,
} TC_OP;

/*
 * TC_CHUB_REQ_CREDITS_ENUM enum
 */

typedef enum TC_CHUB_REQ_CREDITS_ENUM {
TC_CHUB_REQ_CREDITS                      = 0x00000010,
} TC_CHUB_REQ_CREDITS_ENUM;

/*
 * CHUB_TC_RET_CREDITS_ENUM enum
 */

typedef enum CHUB_TC_RET_CREDITS_ENUM {
CHUB_TC_RET_CREDITS                      = 0x00000020,
} CHUB_TC_RET_CREDITS_ENUM;

/*
 * TC_NACKS enum
 */

typedef enum TC_NACKS {
TC_NACK_NO_FAULT                         = 0x00000000,
TC_NACK_PAGE_FAULT                       = 0x00000001,
TC_NACK_PROTECTION_FAULT                 = 0x00000002,
TC_NACK_DATA_ERROR                       = 0x00000003,
} TC_NACKS;

/*
 * TC_EA_CID enum
 */

typedef enum TC_EA_CID {
TC_EA_CID_RT                             = 0x00000000,
TC_EA_CID_FMASK                          = 0x00000001,
TC_EA_CID_DCC                            = 0x00000002,
TC_EA_CID_TCPMETA                        = 0x00000003,
TC_EA_CID_Z                              = 0x00000004,
TC_EA_CID_STENCIL                        = 0x00000005,
TC_EA_CID_HTILE                          = 0x00000006,
TC_EA_CID_MISC                           = 0x00000007,
TC_EA_CID_TCP                            = 0x00000008,
TC_EA_CID_SQC                            = 0x00000009,
TC_EA_CID_CPF                            = 0x0000000a,
TC_EA_CID_CPG                            = 0x0000000b,
TC_EA_CID_IA                             = 0x0000000c,
TC_EA_CID_WD                             = 0x0000000d,
TC_EA_CID_PA                             = 0x0000000e,
TC_EA_CID_UTCL2_TPI                      = 0x0000000f,
} TC_EA_CID;

/*******************************************************
 * SPI Enums
 *******************************************************/

/*
 * SPI_SAMPLE_CNTL enum
 */

typedef enum SPI_SAMPLE_CNTL {
CENTROIDS_ONLY                           = 0x00000000,
CENTERS_ONLY                             = 0x00000001,
CENTROIDS_AND_CENTERS                    = 0x00000002,
UNDEF                                    = 0x00000003,
} SPI_SAMPLE_CNTL;

/*
 * SPI_FOG_MODE enum
 */

typedef enum SPI_FOG_MODE {
SPI_FOG_NONE                             = 0x00000000,
SPI_FOG_EXP                              = 0x00000001,
SPI_FOG_EXP2                             = 0x00000002,
SPI_FOG_LINEAR                           = 0x00000003,
} SPI_FOG_MODE;

/*
 * SPI_PNT_SPRITE_OVERRIDE enum
 */

typedef enum SPI_PNT_SPRITE_OVERRIDE {
SPI_PNT_SPRITE_SEL_0                     = 0x00000000,
SPI_PNT_SPRITE_SEL_1                     = 0x00000001,
SPI_PNT_SPRITE_SEL_S                     = 0x00000002,
SPI_PNT_SPRITE_SEL_T                     = 0x00000003,
SPI_PNT_SPRITE_SEL_NONE                  = 0x00000004,
} SPI_PNT_SPRITE_OVERRIDE;

/*
 * SPI_PERFCNT_SEL enum
 */

typedef enum SPI_PERFCNT_SEL {
SPI_PERF_VS_WINDOW_VALID                 = 0x00000000,
SPI_PERF_VS_BUSY                         = 0x00000001,
SPI_PERF_VS_FIRST_WAVE                   = 0x00000002,
SPI_PERF_VS_LAST_WAVE                    = 0x00000003,
SPI_PERF_VS_LSHS_DEALLOC                 = 0x00000004,
SPI_PERF_VS_PC_STALL                     = 0x00000005,
SPI_PERF_VS_POS0_STALL                   = 0x00000006,
SPI_PERF_VS_POS1_STALL                   = 0x00000007,
SPI_PERF_VS_CRAWLER_STALL                = 0x00000008,
SPI_PERF_VS_EVENT_WAVE                   = 0x00000009,
SPI_PERF_VS_WAVE                         = 0x0000000a,
SPI_PERF_VS_PERS_UPD_FULL0               = 0x0000000b,
SPI_PERF_VS_PERS_UPD_FULL1               = 0x0000000c,
SPI_PERF_VS_LATE_ALLOC_FULL              = 0x0000000d,
SPI_PERF_VS_FIRST_SUBGRP                 = 0x0000000e,
SPI_PERF_VS_LAST_SUBGRP                  = 0x0000000f,
SPI_PERF_GS_WINDOW_VALID                 = 0x00000010,
SPI_PERF_GS_BUSY                         = 0x00000011,
SPI_PERF_GS_CRAWLER_STALL                = 0x00000012,
SPI_PERF_GS_EVENT_WAVE                   = 0x00000013,
SPI_PERF_GS_WAVE                         = 0x00000014,
SPI_PERF_GS_PERS_UPD_FULL0               = 0x00000015,
SPI_PERF_GS_PERS_UPD_FULL1               = 0x00000016,
SPI_PERF_GS_FIRST_SUBGRP                 = 0x00000017,
SPI_PERF_GS_LAST_SUBGRP                  = 0x00000018,
SPI_PERF_ES_WINDOW_VALID                 = 0x00000019,
SPI_PERF_ES_BUSY                         = 0x0000001a,
SPI_PERF_ES_CRAWLER_STALL                = 0x0000001b,
SPI_PERF_ES_FIRST_WAVE                   = 0x0000001c,
SPI_PERF_ES_LAST_WAVE                    = 0x0000001d,
SPI_PERF_ES_LSHS_DEALLOC                 = 0x0000001e,
SPI_PERF_ES_EVENT_WAVE                   = 0x0000001f,
SPI_PERF_ES_WAVE                         = 0x00000020,
SPI_PERF_ES_PERS_UPD_FULL0               = 0x00000021,
SPI_PERF_ES_PERS_UPD_FULL1               = 0x00000022,
SPI_PERF_ES_FIRST_SUBGRP                 = 0x00000023,
SPI_PERF_ES_LAST_SUBGRP                  = 0x00000024,
SPI_PERF_HS_WINDOW_VALID                 = 0x00000025,
SPI_PERF_HS_BUSY                         = 0x00000026,
SPI_PERF_HS_CRAWLER_STALL                = 0x00000027,
SPI_PERF_HS_FIRST_WAVE                   = 0x00000028,
SPI_PERF_HS_LAST_WAVE                    = 0x00000029,
SPI_PERF_HS_LSHS_DEALLOC                 = 0x0000002a,
SPI_PERF_HS_EVENT_WAVE                   = 0x0000002b,
SPI_PERF_HS_WAVE                         = 0x0000002c,
SPI_PERF_HS_PERS_UPD_FULL0               = 0x0000002d,
SPI_PERF_HS_PERS_UPD_FULL1               = 0x0000002e,
SPI_PERF_LS_WINDOW_VALID                 = 0x0000002f,
SPI_PERF_LS_BUSY                         = 0x00000030,
SPI_PERF_LS_CRAWLER_STALL                = 0x00000031,
SPI_PERF_LS_FIRST_WAVE                   = 0x00000032,
SPI_PERF_LS_LAST_WAVE                    = 0x00000033,
SPI_PERF_OFFCHIP_LDS_STALL_LS            = 0x00000034,
SPI_PERF_LS_EVENT_WAVE                   = 0x00000035,
SPI_PERF_LS_WAVE                         = 0x00000036,
SPI_PERF_LS_PERS_UPD_FULL0               = 0x00000037,
SPI_PERF_LS_PERS_UPD_FULL1               = 0x00000038,
SPI_PERF_CSG_WINDOW_VALID                = 0x00000039,
SPI_PERF_CSG_BUSY                        = 0x0000003a,
SPI_PERF_CSG_NUM_THREADGROUPS            = 0x0000003b,
SPI_PERF_CSG_CRAWLER_STALL               = 0x0000003c,
SPI_PERF_CSG_EVENT_WAVE                  = 0x0000003d,
SPI_PERF_CSG_WAVE                        = 0x0000003e,
SPI_PERF_CSN_WINDOW_VALID                = 0x0000003f,
SPI_PERF_CSN_BUSY                        = 0x00000040,
SPI_PERF_CSN_NUM_THREADGROUPS            = 0x00000041,
SPI_PERF_CSN_CRAWLER_STALL               = 0x00000042,
SPI_PERF_CSN_EVENT_WAVE                  = 0x00000043,
SPI_PERF_CSN_WAVE                        = 0x00000044,
SPI_PERF_PS_CTL_WINDOW_VALID             = 0x00000045,
SPI_PERF_PS_CTL_BUSY                     = 0x00000046,
SPI_PERF_PS_CTL_ACTIVE                   = 0x00000047,
SPI_PERF_PS_CTL_DEALLOC_BIN0             = 0x00000048,
SPI_PERF_PS_CTL_FPOS_BIN1_STALL          = 0x00000049,
SPI_PERF_PS_CTL_EVENT_WAVE               = 0x0000004a,
SPI_PERF_PS_CTL_WAVE                     = 0x0000004b,
SPI_PERF_PS_CTL_OPT_WAVE                 = 0x0000004c,
SPI_PERF_PS_CTL_PASS_BIN0                = 0x0000004d,
SPI_PERF_PS_CTL_PASS_BIN1                = 0x0000004e,
SPI_PERF_PS_CTL_FPOS_BIN2                = 0x0000004f,
SPI_PERF_PS_CTL_PRIM_BIN0                = 0x00000050,
SPI_PERF_PS_CTL_PRIM_BIN1                = 0x00000051,
SPI_PERF_PS_CTL_CNF_BIN2                 = 0x00000052,
SPI_PERF_PS_CTL_CNF_BIN3                 = 0x00000053,
SPI_PERF_PS_CTL_CRAWLER_STALL            = 0x00000054,
SPI_PERF_PS_CTL_LDS_RES_FULL             = 0x00000055,
SPI_PERF_PS_PERS_UPD_FULL0               = 0x00000056,
SPI_PERF_PS_PERS_UPD_FULL1               = 0x00000057,
SPI_PERF_PIX_ALLOC_PEND_CNT              = 0x00000058,
SPI_PERF_PIX_ALLOC_SCB_STALL             = 0x00000059,
SPI_PERF_PIX_ALLOC_DB0_STALL             = 0x0000005a,
SPI_PERF_PIX_ALLOC_DB1_STALL             = 0x0000005b,
SPI_PERF_PIX_ALLOC_DB2_STALL             = 0x0000005c,
SPI_PERF_PIX_ALLOC_DB3_STALL             = 0x0000005d,
SPI_PERF_LDS0_PC_VALID                   = 0x0000005e,
SPI_PERF_LDS1_PC_VALID                   = 0x0000005f,
SPI_PERF_RA_PIPE_REQ_BIN2                = 0x00000060,
SPI_PERF_RA_TASK_REQ_BIN3                = 0x00000061,
SPI_PERF_RA_WR_CTL_FULL                  = 0x00000062,
SPI_PERF_RA_REQ_NO_ALLOC                 = 0x00000063,
SPI_PERF_RA_REQ_NO_ALLOC_PS              = 0x00000064,
SPI_PERF_RA_REQ_NO_ALLOC_VS              = 0x00000065,
SPI_PERF_RA_REQ_NO_ALLOC_GS              = 0x00000066,
SPI_PERF_RA_REQ_NO_ALLOC_ES              = 0x00000067,
SPI_PERF_RA_REQ_NO_ALLOC_HS              = 0x00000068,
SPI_PERF_RA_REQ_NO_ALLOC_LS              = 0x00000069,
SPI_PERF_RA_REQ_NO_ALLOC_CSG             = 0x0000006a,
SPI_PERF_RA_REQ_NO_ALLOC_CSN             = 0x0000006b,
SPI_PERF_RA_RES_STALL_PS                 = 0x0000006c,
SPI_PERF_RA_RES_STALL_VS                 = 0x0000006d,
SPI_PERF_RA_RES_STALL_GS                 = 0x0000006e,
SPI_PERF_RA_RES_STALL_ES                 = 0x0000006f,
SPI_PERF_RA_RES_STALL_HS                 = 0x00000070,
SPI_PERF_RA_RES_STALL_LS                 = 0x00000071,
SPI_PERF_RA_RES_STALL_CSG                = 0x00000072,
SPI_PERF_RA_RES_STALL_CSN                = 0x00000073,
SPI_PERF_RA_TMP_STALL_PS                 = 0x00000074,
SPI_PERF_RA_TMP_STALL_VS                 = 0x00000075,
SPI_PERF_RA_TMP_STALL_GS                 = 0x00000076,
SPI_PERF_RA_TMP_STALL_ES                 = 0x00000077,
SPI_PERF_RA_TMP_STALL_HS                 = 0x00000078,
SPI_PERF_RA_TMP_STALL_LS                 = 0x00000079,
SPI_PERF_RA_TMP_STALL_CSG                = 0x0000007a,
SPI_PERF_RA_TMP_STALL_CSN                = 0x0000007b,
SPI_PERF_RA_WAVE_SIMD_FULL_PS            = 0x0000007c,
SPI_PERF_RA_WAVE_SIMD_FULL_VS            = 0x0000007d,
SPI_PERF_RA_WAVE_SIMD_FULL_GS            = 0x0000007e,
SPI_PERF_RA_WAVE_SIMD_FULL_ES            = 0x0000007f,
SPI_PERF_RA_WAVE_SIMD_FULL_HS            = 0x00000080,
SPI_PERF_RA_WAVE_SIMD_FULL_LS            = 0x00000081,
SPI_PERF_RA_WAVE_SIMD_FULL_CSG           = 0x00000082,
SPI_PERF_RA_WAVE_SIMD_FULL_CSN           = 0x00000083,
SPI_PERF_RA_VGPR_SIMD_FULL_PS            = 0x00000084,
SPI_PERF_RA_VGPR_SIMD_FULL_VS            = 0x00000085,
SPI_PERF_RA_VGPR_SIMD_FULL_GS            = 0x00000086,
SPI_PERF_RA_VGPR_SIMD_FULL_ES            = 0x00000087,
SPI_PERF_RA_VGPR_SIMD_FULL_HS            = 0x00000088,
SPI_PERF_RA_VGPR_SIMD_FULL_LS            = 0x00000089,
SPI_PERF_RA_VGPR_SIMD_FULL_CSG           = 0x0000008a,
SPI_PERF_RA_VGPR_SIMD_FULL_CSN           = 0x0000008b,
SPI_PERF_RA_SGPR_SIMD_FULL_PS            = 0x0000008c,
SPI_PERF_RA_SGPR_SIMD_FULL_VS            = 0x0000008d,
SPI_PERF_RA_SGPR_SIMD_FULL_GS            = 0x0000008e,
SPI_PERF_RA_SGPR_SIMD_FULL_ES            = 0x0000008f,
SPI_PERF_RA_SGPR_SIMD_FULL_HS            = 0x00000090,
SPI_PERF_RA_SGPR_SIMD_FULL_LS            = 0x00000091,
SPI_PERF_RA_SGPR_SIMD_FULL_CSG           = 0x00000092,
SPI_PERF_RA_SGPR_SIMD_FULL_CSN           = 0x00000093,
SPI_PERF_RA_LDS_CU_FULL_PS               = 0x00000094,
SPI_PERF_RA_LDS_CU_FULL_LS               = 0x00000095,
SPI_PERF_RA_LDS_CU_FULL_ES               = 0x00000096,
SPI_PERF_RA_LDS_CU_FULL_CSG              = 0x00000097,
SPI_PERF_RA_LDS_CU_FULL_CSN              = 0x00000098,
SPI_PERF_RA_BAR_CU_FULL_HS               = 0x00000099,
SPI_PERF_RA_BAR_CU_FULL_CSG              = 0x0000009a,
SPI_PERF_RA_BAR_CU_FULL_CSN              = 0x0000009b,
SPI_PERF_RA_BULKY_CU_FULL_CSG            = 0x0000009c,
SPI_PERF_RA_BULKY_CU_FULL_CSN            = 0x0000009d,
SPI_PERF_RA_TGLIM_CU_FULL_CSG            = 0x0000009e,
SPI_PERF_RA_TGLIM_CU_FULL_CSN            = 0x0000009f,
SPI_PERF_RA_WVLIM_STALL_PS               = 0x000000a0,
SPI_PERF_RA_WVLIM_STALL_VS               = 0x000000a1,
SPI_PERF_RA_WVLIM_STALL_GS               = 0x000000a2,
SPI_PERF_RA_WVLIM_STALL_ES               = 0x000000a3,
SPI_PERF_RA_WVLIM_STALL_HS               = 0x000000a4,
SPI_PERF_RA_WVLIM_STALL_LS               = 0x000000a5,
SPI_PERF_RA_WVLIM_STALL_CSG              = 0x000000a6,
SPI_PERF_RA_WVLIM_STALL_CSN              = 0x000000a7,
SPI_PERF_RA_PS_LOCK_NA                   = 0x000000a8,
SPI_PERF_RA_VS_LOCK                      = 0x000000a9,
SPI_PERF_RA_GS_LOCK                      = 0x000000aa,
SPI_PERF_RA_ES_LOCK                      = 0x000000ab,
SPI_PERF_RA_HS_LOCK                      = 0x000000ac,
SPI_PERF_RA_LS_LOCK                      = 0x000000ad,
SPI_PERF_RA_CSG_LOCK                     = 0x000000ae,
SPI_PERF_RA_CSN_LOCK                     = 0x000000af,
SPI_PERF_RA_RSV_UPD                      = 0x000000b0,
SPI_PERF_EXP_ARB_COL_CNT                 = 0x000000b1,
SPI_PERF_EXP_ARB_PAR_CNT                 = 0x000000b2,
SPI_PERF_EXP_ARB_POS_CNT                 = 0x000000b3,
SPI_PERF_EXP_ARB_GDS_CNT                 = 0x000000b4,
SPI_PERF_CLKGATE_BUSY_STALL              = 0x000000b5,
SPI_PERF_CLKGATE_ACTIVE_STALL            = 0x000000b6,
SPI_PERF_CLKGATE_ALL_CLOCKS_ON           = 0x000000b7,
SPI_PERF_CLKGATE_CGTT_DYN_ON             = 0x000000b8,
SPI_PERF_CLKGATE_CGTT_REG_ON             = 0x000000b9,
SPI_PERF_NUM_VS_POS_EXPORTS              = 0x000000ba,
SPI_PERF_NUM_VS_PARAM_EXPORTS            = 0x000000bb,
SPI_PERF_NUM_PS_COL_EXPORTS              = 0x000000bc,
SPI_PERF_ES_GRP_FIFO_FULL                = 0x000000bd,
SPI_PERF_GS_GRP_FIFO_FULL                = 0x000000be,
SPI_PERF_HS_GRP_FIFO_FULL                = 0x000000bf,
SPI_PERF_LS_GRP_FIFO_FULL                = 0x000000c0,
SPI_PERF_VS_ALLOC_CNT                    = 0x000000c1,
SPI_PERF_VS_LATE_ALLOC_ACCUM             = 0x000000c2,
SPI_PERF_PC_ALLOC_CNT                    = 0x000000c3,
SPI_PERF_PC_ALLOC_ACCUM                  = 0x000000c4,
} SPI_PERFCNT_SEL;

/*
 * SPI_SHADER_FORMAT enum
 */

typedef enum SPI_SHADER_FORMAT {
SPI_SHADER_NONE                          = 0x00000000,
SPI_SHADER_1COMP                         = 0x00000001,
SPI_SHADER_2COMP                         = 0x00000002,
SPI_SHADER_4COMPRESS                     = 0x00000003,
SPI_SHADER_4COMP                         = 0x00000004,
} SPI_SHADER_FORMAT;

/*
 * SPI_SHADER_EX_FORMAT enum
 */

typedef enum SPI_SHADER_EX_FORMAT {
SPI_SHADER_ZERO                          = 0x00000000,
SPI_SHADER_32_R                          = 0x00000001,
SPI_SHADER_32_GR                         = 0x00000002,
SPI_SHADER_32_AR                         = 0x00000003,
SPI_SHADER_FP16_ABGR                     = 0x00000004,
SPI_SHADER_UNORM16_ABGR                  = 0x00000005,
SPI_SHADER_SNORM16_ABGR                  = 0x00000006,
SPI_SHADER_UINT16_ABGR                   = 0x00000007,
SPI_SHADER_SINT16_ABGR                   = 0x00000008,
SPI_SHADER_32_ABGR                       = 0x00000009,
} SPI_SHADER_EX_FORMAT;

/*
 * CLKGATE_SM_MODE enum
 */

typedef enum CLKGATE_SM_MODE {
ON_SEQ                                   = 0x00000000,
OFF_SEQ                                  = 0x00000001,
PROG_SEQ                                 = 0x00000002,
READ_SEQ                                 = 0x00000003,
SM_MODE_RESERVED                         = 0x00000004,
} CLKGATE_SM_MODE;

/*
 * CLKGATE_BASE_MODE enum
 */

typedef enum CLKGATE_BASE_MODE {
MULT_8                                   = 0x00000000,
MULT_16                                  = 0x00000001,
} CLKGATE_BASE_MODE;

/*******************************************************
 * SQ Enums
 *******************************************************/

/*
 * SQ_TEX_CLAMP enum
 */

typedef enum SQ_TEX_CLAMP {
SQ_TEX_WRAP                              = 0x00000000,
SQ_TEX_MIRROR                            = 0x00000001,
SQ_TEX_CLAMP_LAST_TEXEL                  = 0x00000002,
SQ_TEX_MIRROR_ONCE_LAST_TEXEL            = 0x00000003,
SQ_TEX_CLAMP_HALF_BORDER                 = 0x00000004,
SQ_TEX_MIRROR_ONCE_HALF_BORDER           = 0x00000005,
SQ_TEX_CLAMP_BORDER                      = 0x00000006,
SQ_TEX_MIRROR_ONCE_BORDER                = 0x00000007,
} SQ_TEX_CLAMP;

/*
 * SQ_TEX_XY_FILTER enum
 */

typedef enum SQ_TEX_XY_FILTER {
SQ_TEX_XY_FILTER_POINT                   = 0x00000000,
SQ_TEX_XY_FILTER_BILINEAR                = 0x00000001,
SQ_TEX_XY_FILTER_ANISO_POINT             = 0x00000002,
SQ_TEX_XY_FILTER_ANISO_BILINEAR          = 0x00000003,
} SQ_TEX_XY_FILTER;

/*
 * SQ_TEX_Z_FILTER enum
 */

typedef enum SQ_TEX_Z_FILTER {
SQ_TEX_Z_FILTER_NONE                     = 0x00000000,
SQ_TEX_Z_FILTER_POINT                    = 0x00000001,
SQ_TEX_Z_FILTER_LINEAR                   = 0x00000002,
} SQ_TEX_Z_FILTER;

/*
 * SQ_TEX_MIP_FILTER enum
 */

typedef enum SQ_TEX_MIP_FILTER {
SQ_TEX_MIP_FILTER_NONE                   = 0x00000000,
SQ_TEX_MIP_FILTER_POINT                  = 0x00000001,
SQ_TEX_MIP_FILTER_LINEAR                 = 0x00000002,
SQ_TEX_MIP_FILTER_POINT_ANISO_ADJ        = 0x00000003,
} SQ_TEX_MIP_FILTER;

/*
 * SQ_TEX_ANISO_RATIO enum
 */

typedef enum SQ_TEX_ANISO_RATIO {
SQ_TEX_ANISO_RATIO_1                     = 0x00000000,
SQ_TEX_ANISO_RATIO_2                     = 0x00000001,
SQ_TEX_ANISO_RATIO_4                     = 0x00000002,
SQ_TEX_ANISO_RATIO_8                     = 0x00000003,
SQ_TEX_ANISO_RATIO_16                    = 0x00000004,
} SQ_TEX_ANISO_RATIO;

/*
 * SQ_TEX_DEPTH_COMPARE enum
 */

typedef enum SQ_TEX_DEPTH_COMPARE {
SQ_TEX_DEPTH_COMPARE_NEVER               = 0x00000000,
SQ_TEX_DEPTH_COMPARE_LESS                = 0x00000001,
SQ_TEX_DEPTH_COMPARE_EQUAL               = 0x00000002,
SQ_TEX_DEPTH_COMPARE_LESSEQUAL           = 0x00000003,
SQ_TEX_DEPTH_COMPARE_GREATER             = 0x00000004,
SQ_TEX_DEPTH_COMPARE_NOTEQUAL            = 0x00000005,
SQ_TEX_DEPTH_COMPARE_GREATEREQUAL        = 0x00000006,
SQ_TEX_DEPTH_COMPARE_ALWAYS              = 0x00000007,
} SQ_TEX_DEPTH_COMPARE;

/*
 * SQ_TEX_BORDER_COLOR enum
 */

typedef enum SQ_TEX_BORDER_COLOR {
SQ_TEX_BORDER_COLOR_TRANS_BLACK          = 0x00000000,
SQ_TEX_BORDER_COLOR_OPAQUE_BLACK         = 0x00000001,
SQ_TEX_BORDER_COLOR_OPAQUE_WHITE         = 0x00000002,
SQ_TEX_BORDER_COLOR_REGISTER             = 0x00000003,
} SQ_TEX_BORDER_COLOR;

/*
 * SQ_RSRC_BUF_TYPE enum
 */

typedef enum SQ_RSRC_BUF_TYPE {
SQ_RSRC_BUF                              = 0x00000000,
SQ_RSRC_BUF_RSVD_1                       = 0x00000001,
SQ_RSRC_BUF_RSVD_2                       = 0x00000002,
SQ_RSRC_BUF_RSVD_3                       = 0x00000003,
} SQ_RSRC_BUF_TYPE;

/*
 * SQ_RSRC_IMG_TYPE enum
 */

typedef enum SQ_RSRC_IMG_TYPE {
SQ_RSRC_IMG_RSVD_0                       = 0x00000000,
SQ_RSRC_IMG_RSVD_1                       = 0x00000001,
SQ_RSRC_IMG_RSVD_2                       = 0x00000002,
SQ_RSRC_IMG_RSVD_3                       = 0x00000003,
SQ_RSRC_IMG_RSVD_4                       = 0x00000004,
SQ_RSRC_IMG_RSVD_5                       = 0x00000005,
SQ_RSRC_IMG_RSVD_6                       = 0x00000006,
SQ_RSRC_IMG_RSVD_7                       = 0x00000007,
SQ_RSRC_IMG_1D                           = 0x00000008,
SQ_RSRC_IMG_2D                           = 0x00000009,
SQ_RSRC_IMG_3D                           = 0x0000000a,
SQ_RSRC_IMG_CUBE                         = 0x0000000b,
SQ_RSRC_IMG_1D_ARRAY                     = 0x0000000c,
SQ_RSRC_IMG_2D_ARRAY                     = 0x0000000d,
SQ_RSRC_IMG_2D_MSAA                      = 0x0000000e,
SQ_RSRC_IMG_2D_MSAA_ARRAY                = 0x0000000f,
} SQ_RSRC_IMG_TYPE;

/*
 * SQ_RSRC_FLAT_TYPE enum
 */

typedef enum SQ_RSRC_FLAT_TYPE {
SQ_RSRC_FLAT_RSVD_0                      = 0x00000000,
SQ_RSRC_FLAT                             = 0x00000001,
SQ_RSRC_FLAT_RSVD_2                      = 0x00000002,
SQ_RSRC_FLAT_RSVD_3                      = 0x00000003,
} SQ_RSRC_FLAT_TYPE;

/*
 * SQ_IMG_FILTER_TYPE enum
 */

typedef enum SQ_IMG_FILTER_TYPE {
SQ_IMG_FILTER_MODE_BLEND                 = 0x00000000,
SQ_IMG_FILTER_MODE_MIN                   = 0x00000001,
SQ_IMG_FILTER_MODE_MAX                   = 0x00000002,
} SQ_IMG_FILTER_TYPE;

/*
 * SQ_SEL_XYZW01 enum
 */

typedef enum SQ_SEL_XYZW01 {
SQ_SEL_0                                 = 0x00000000,
SQ_SEL_1                                 = 0x00000001,
SQ_SEL_RESERVED_0                        = 0x00000002,
SQ_SEL_RESERVED_1                        = 0x00000003,
SQ_SEL_X                                 = 0x00000004,
SQ_SEL_Y                                 = 0x00000005,
SQ_SEL_Z                                 = 0x00000006,
SQ_SEL_W                                 = 0x00000007,
} SQ_SEL_XYZW01;

/*
 * SQ_WAVE_TYPE enum
 */

typedef enum SQ_WAVE_TYPE {
SQ_WAVE_TYPE_PS                          = 0x00000000,
SQ_WAVE_TYPE_VS                          = 0x00000001,
SQ_WAVE_TYPE_GS                          = 0x00000002,
SQ_WAVE_TYPE_ES                          = 0x00000003,
SQ_WAVE_TYPE_HS                          = 0x00000004,
SQ_WAVE_TYPE_LS                          = 0x00000005,
SQ_WAVE_TYPE_CS                          = 0x00000006,
SQ_WAVE_TYPE_PS1                         = 0x00000007,
} SQ_WAVE_TYPE;

/*
 * SQ_THREAD_TRACE_TOKEN_TYPE enum
 */

typedef enum SQ_THREAD_TRACE_TOKEN_TYPE {
SQ_THREAD_TRACE_TOKEN_MISC               = 0x00000000,
SQ_THREAD_TRACE_TOKEN_TIMESTAMP          = 0x00000001,
SQ_THREAD_TRACE_TOKEN_REG                = 0x00000002,
SQ_THREAD_TRACE_TOKEN_WAVE_START         = 0x00000003,
SQ_THREAD_TRACE_TOKEN_WAVE_ALLOC         = 0x00000004,
SQ_THREAD_TRACE_TOKEN_REG_CSPRIV         = 0x00000005,
SQ_THREAD_TRACE_TOKEN_WAVE_END           = 0x00000006,
SQ_THREAD_TRACE_TOKEN_EVENT              = 0x00000007,
SQ_THREAD_TRACE_TOKEN_EVENT_CS           = 0x00000008,
SQ_THREAD_TRACE_TOKEN_EVENT_GFX1         = 0x00000009,
SQ_THREAD_TRACE_TOKEN_INST               = 0x0000000a,
SQ_THREAD_TRACE_TOKEN_INST_PC            = 0x0000000b,
SQ_THREAD_TRACE_TOKEN_INST_USERDATA      = 0x0000000c,
SQ_THREAD_TRACE_TOKEN_ISSUE              = 0x0000000d,
SQ_THREAD_TRACE_TOKEN_PERF               = 0x0000000e,
SQ_THREAD_TRACE_TOKEN_REG_CS             = 0x0000000f,
} SQ_THREAD_TRACE_TOKEN_TYPE;

/*
 * SQ_THREAD_TRACE_MISC_TOKEN_TYPE enum
 */

typedef enum SQ_THREAD_TRACE_MISC_TOKEN_TYPE {
SQ_THREAD_TRACE_MISC_TOKEN_TIME          = 0x00000000,
SQ_THREAD_TRACE_MISC_TOKEN_TIME_RESET    = 0x00000001,
SQ_THREAD_TRACE_MISC_TOKEN_PACKET_LOST   = 0x00000002,
SQ_THREAD_TRACE_MISC_TOKEN_SURF_SYNC     = 0x00000003,
SQ_THREAD_TRACE_MISC_TOKEN_TTRACE_STALL_BEGIN  = 0x00000004,
SQ_THREAD_TRACE_MISC_TOKEN_TTRACE_STALL_END  = 0x00000005,
SQ_THREAD_TRACE_MISC_TOKEN_SAVECTX       = 0x00000006,
SQ_THREAD_TRACE_MISC_TOKEN_SHOOT_DOWN    = 0x00000007,
} SQ_THREAD_TRACE_MISC_TOKEN_TYPE;

/*
 * SQ_THREAD_TRACE_INST_TYPE enum
 */

typedef enum SQ_THREAD_TRACE_INST_TYPE {
SQ_THREAD_TRACE_INST_TYPE_SMEM_RD        = 0x00000000,
SQ_THREAD_TRACE_INST_TYPE_SALU_32        = 0x00000001,
SQ_THREAD_TRACE_INST_TYPE_VMEM_RD        = 0x00000002,
SQ_THREAD_TRACE_INST_TYPE_VMEM_WR        = 0x00000003,
SQ_THREAD_TRACE_INST_TYPE_FLAT_WR        = 0x00000004,
SQ_THREAD_TRACE_INST_TYPE_VALU_32        = 0x00000005,
SQ_THREAD_TRACE_INST_TYPE_LDS            = 0x00000006,
SQ_THREAD_TRACE_INST_TYPE_PC             = 0x00000007,
SQ_THREAD_TRACE_INST_TYPE_EXPREQ_GDS     = 0x00000008,
SQ_THREAD_TRACE_INST_TYPE_EXPREQ_GFX     = 0x00000009,
SQ_THREAD_TRACE_INST_TYPE_EXPGNT_PAR_COL  = 0x0000000a,
SQ_THREAD_TRACE_INST_TYPE_EXPGNT_POS_GDS  = 0x0000000b,
SQ_THREAD_TRACE_INST_TYPE_JUMP           = 0x0000000c,
SQ_THREAD_TRACE_INST_TYPE_NEXT           = 0x0000000d,
SQ_THREAD_TRACE_INST_TYPE_FLAT_RD        = 0x0000000e,
SQ_THREAD_TRACE_INST_TYPE_OTHER_MSG      = 0x0000000f,
SQ_THREAD_TRACE_INST_TYPE_SMEM_WR        = 0x00000010,
SQ_THREAD_TRACE_INST_TYPE_SALU_64        = 0x00000011,
SQ_THREAD_TRACE_INST_TYPE_VALU_64        = 0x00000012,
SQ_THREAD_TRACE_INST_TYPE_SMEM_RD_REPLAY  = 0x00000013,
SQ_THREAD_TRACE_INST_TYPE_SMEM_WR_REPLAY  = 0x00000014,
SQ_THREAD_TRACE_INST_TYPE_VMEM_RD_REPLAY  = 0x00000015,
SQ_THREAD_TRACE_INST_TYPE_VMEM_WR_REPLAY  = 0x00000016,
SQ_THREAD_TRACE_INST_TYPE_FLAT_RD_REPLAY  = 0x00000017,
SQ_THREAD_TRACE_INST_TYPE_FLAT_WR_REPLAY  = 0x00000018,
SQ_THREAD_TRACE_INST_TYPE_FATAL_HALT     = 0x00000019,
} SQ_THREAD_TRACE_INST_TYPE;

/*
 * SQ_THREAD_TRACE_REG_TYPE enum
 */

typedef enum SQ_THREAD_TRACE_REG_TYPE {
SQ_THREAD_TRACE_REG_TYPE_EVENT           = 0x00000000,
SQ_THREAD_TRACE_REG_TYPE_DRAW            = 0x00000001,
SQ_THREAD_TRACE_REG_TYPE_DISPATCH        = 0x00000002,
SQ_THREAD_TRACE_REG_TYPE_USERDATA        = 0x00000003,
SQ_THREAD_TRACE_REG_TYPE_MARKER          = 0x00000004,
SQ_THREAD_TRACE_REG_TYPE_GFXDEC          = 0x00000005,
SQ_THREAD_TRACE_REG_TYPE_SHDEC           = 0x00000006,
SQ_THREAD_TRACE_REG_TYPE_OTHER           = 0x00000007,
} SQ_THREAD_TRACE_REG_TYPE;

/*
 * SQ_THREAD_TRACE_REG_OP enum
 */

typedef enum SQ_THREAD_TRACE_REG_OP {
SQ_THREAD_TRACE_REG_OP_READ              = 0x00000000,
SQ_THREAD_TRACE_REG_OP_WRITE             = 0x00000001,
} SQ_THREAD_TRACE_REG_OP;

/*
 * SQ_THREAD_TRACE_MODE_SEL enum
 */

typedef enum SQ_THREAD_TRACE_MODE_SEL {
SQ_THREAD_TRACE_MODE_OFF                 = 0x00000000,
SQ_THREAD_TRACE_MODE_ON                  = 0x00000001,
} SQ_THREAD_TRACE_MODE_SEL;

/*
 * SQ_THREAD_TRACE_CAPTURE_MODE enum
 */

typedef enum SQ_THREAD_TRACE_CAPTURE_MODE {
SQ_THREAD_TRACE_CAPTURE_MODE_ALL         = 0x00000000,
SQ_THREAD_TRACE_CAPTURE_MODE_SELECT      = 0x00000001,
SQ_THREAD_TRACE_CAPTURE_MODE_SELECT_DETAIL  = 0x00000002,
} SQ_THREAD_TRACE_CAPTURE_MODE;

/*
 * SQ_THREAD_TRACE_VM_ID_MASK enum
 */

typedef enum SQ_THREAD_TRACE_VM_ID_MASK {
SQ_THREAD_TRACE_VM_ID_MASK_SINGLE        = 0x00000000,
SQ_THREAD_TRACE_VM_ID_MASK_ALL           = 0x00000001,
SQ_THREAD_TRACE_VM_ID_MASK_SINGLE_DETAIL  = 0x00000002,
} SQ_THREAD_TRACE_VM_ID_MASK;

/*
 * SQ_THREAD_TRACE_WAVE_MASK enum
 */

typedef enum SQ_THREAD_TRACE_WAVE_MASK {
SQ_THREAD_TRACE_WAVE_MASK_NONE           = 0x00000000,
SQ_THREAD_TRACE_WAVE_MASK_ALL            = 0x00000001,
} SQ_THREAD_TRACE_WAVE_MASK;

/*
 * SQ_THREAD_TRACE_ISSUE enum
 */

typedef enum SQ_THREAD_TRACE_ISSUE {
SQ_THREAD_TRACE_ISSUE_NULL               = 0x00000000,
SQ_THREAD_TRACE_ISSUE_STALL              = 0x00000001,
SQ_THREAD_TRACE_ISSUE_INST               = 0x00000002,
SQ_THREAD_TRACE_ISSUE_IMMED              = 0x00000003,
} SQ_THREAD_TRACE_ISSUE;

/*
 * SQ_THREAD_TRACE_ISSUE_MASK enum
 */

typedef enum SQ_THREAD_TRACE_ISSUE_MASK {
SQ_THREAD_TRACE_ISSUE_MASK_ALL           = 0x00000000,
SQ_THREAD_TRACE_ISSUE_MASK_STALLED       = 0x00000001,
SQ_THREAD_TRACE_ISSUE_MASK_STALLED_AND_IMMED  = 0x00000002,
SQ_THREAD_TRACE_ISSUE_MASK_IMMED         = 0x00000003,
} SQ_THREAD_TRACE_ISSUE_MASK;

/*
 * SQ_PERF_SEL enum
 */

typedef enum SQ_PERF_SEL {
SQ_PERF_SEL_NONE                         = 0x00000000,
SQ_PERF_SEL_ACCUM_PREV                   = 0x00000001,
SQ_PERF_SEL_CYCLES                       = 0x00000002,
SQ_PERF_SEL_BUSY_CYCLES                  = 0x00000003,
SQ_PERF_SEL_WAVES                        = 0x00000004,
SQ_PERF_SEL_LEVEL_WAVES                  = 0x00000005,
SQ_PERF_SEL_WAVES_EQ_64                  = 0x00000006,
SQ_PERF_SEL_WAVES_LT_64                  = 0x00000007,
SQ_PERF_SEL_WAVES_LT_48                  = 0x00000008,
SQ_PERF_SEL_WAVES_LT_32                  = 0x00000009,
SQ_PERF_SEL_WAVES_LT_16                  = 0x0000000a,
SQ_PERF_SEL_WAVES_CU                     = 0x0000000b,
SQ_PERF_SEL_LEVEL_WAVES_CU               = 0x0000000c,
SQ_PERF_SEL_BUSY_CU_CYCLES               = 0x0000000d,
SQ_PERF_SEL_ITEMS                        = 0x0000000e,
SQ_PERF_SEL_QUADS                        = 0x0000000f,
SQ_PERF_SEL_EVENTS                       = 0x00000010,
SQ_PERF_SEL_SURF_SYNCS                   = 0x00000011,
SQ_PERF_SEL_TTRACE_REQS                  = 0x00000012,
SQ_PERF_SEL_TTRACE_INFLIGHT_REQS         = 0x00000013,
SQ_PERF_SEL_TTRACE_STALL                 = 0x00000014,
SQ_PERF_SEL_MSG_CNTR                     = 0x00000015,
SQ_PERF_SEL_MSG_PERF                     = 0x00000016,
SQ_PERF_SEL_MSG_GSCNT                    = 0x00000017,
SQ_PERF_SEL_MSG_INTERRUPT                = 0x00000018,
SQ_PERF_SEL_INSTS                        = 0x00000019,
SQ_PERF_SEL_INSTS_VALU                   = 0x0000001a,
SQ_PERF_SEL_INSTS_VMEM_WR                = 0x0000001b,
SQ_PERF_SEL_INSTS_VMEM_RD                = 0x0000001c,
SQ_PERF_SEL_INSTS_VMEM                   = 0x0000001d,
SQ_PERF_SEL_INSTS_SALU                   = 0x0000001e,
SQ_PERF_SEL_INSTS_SMEM                   = 0x0000001f,
SQ_PERF_SEL_INSTS_FLAT                   = 0x00000020,
SQ_PERF_SEL_INSTS_FLAT_LDS_ONLY          = 0x00000021,
SQ_PERF_SEL_INSTS_LDS                    = 0x00000022,
SQ_PERF_SEL_INSTS_GDS                    = 0x00000023,
SQ_PERF_SEL_INSTS_EXP                    = 0x00000024,
SQ_PERF_SEL_INSTS_EXP_GDS                = 0x00000025,
SQ_PERF_SEL_INSTS_BRANCH                 = 0x00000026,
SQ_PERF_SEL_INSTS_SENDMSG                = 0x00000027,
SQ_PERF_SEL_INSTS_VSKIPPED               = 0x00000028,
SQ_PERF_SEL_INST_LEVEL_VMEM              = 0x00000029,
SQ_PERF_SEL_INST_LEVEL_SMEM              = 0x0000002a,
SQ_PERF_SEL_INST_LEVEL_LDS               = 0x0000002b,
SQ_PERF_SEL_INST_LEVEL_GDS               = 0x0000002c,
SQ_PERF_SEL_INST_LEVEL_EXP               = 0x0000002d,
SQ_PERF_SEL_WAVE_CYCLES                  = 0x0000002e,
SQ_PERF_SEL_WAVE_READY                   = 0x0000002f,
SQ_PERF_SEL_WAIT_CNT_VM                  = 0x00000030,
SQ_PERF_SEL_WAIT_CNT_LGKM                = 0x00000031,
SQ_PERF_SEL_WAIT_CNT_EXP                 = 0x00000032,
SQ_PERF_SEL_WAIT_CNT_ANY                 = 0x00000033,
SQ_PERF_SEL_WAIT_BARRIER                 = 0x00000034,
SQ_PERF_SEL_WAIT_EXP_ALLOC               = 0x00000035,
SQ_PERF_SEL_WAIT_SLEEP                   = 0x00000036,
SQ_PERF_SEL_WAIT_SLEEP_XNACK             = 0x00000037,
SQ_PERF_SEL_WAIT_OTHER                   = 0x00000038,
SQ_PERF_SEL_WAIT_ANY                     = 0x00000039,
SQ_PERF_SEL_WAIT_TTRACE                  = 0x0000003a,
SQ_PERF_SEL_WAIT_IFETCH                  = 0x0000003b,
SQ_PERF_SEL_WAIT_INST_ANY                = 0x0000003c,
SQ_PERF_SEL_WAIT_INST_VMEM               = 0x0000003d,
SQ_PERF_SEL_WAIT_INST_SCA                = 0x0000003e,
SQ_PERF_SEL_WAIT_INST_LDS                = 0x0000003f,
SQ_PERF_SEL_WAIT_INST_VALU               = 0x00000040,
SQ_PERF_SEL_WAIT_INST_EXP_GDS            = 0x00000041,
SQ_PERF_SEL_WAIT_INST_MISC               = 0x00000042,
SQ_PERF_SEL_WAIT_INST_FLAT               = 0x00000043,
SQ_PERF_SEL_ACTIVE_INST_ANY              = 0x00000044,
SQ_PERF_SEL_ACTIVE_INST_VMEM             = 0x00000045,
SQ_PERF_SEL_ACTIVE_INST_LDS              = 0x00000046,
SQ_PERF_SEL_ACTIVE_INST_VALU             = 0x00000047,
SQ_PERF_SEL_ACTIVE_INST_SCA              = 0x00000048,
SQ_PERF_SEL_ACTIVE_INST_EXP_GDS          = 0x00000049,
SQ_PERF_SEL_ACTIVE_INST_MISC             = 0x0000004a,
SQ_PERF_SEL_ACTIVE_INST_FLAT             = 0x0000004b,
SQ_PERF_SEL_INST_CYCLES_VMEM_WR          = 0x0000004c,
SQ_PERF_SEL_INST_CYCLES_VMEM_RD          = 0x0000004d,
SQ_PERF_SEL_INST_CYCLES_VMEM_ADDR        = 0x0000004e,
SQ_PERF_SEL_INST_CYCLES_VMEM_DATA        = 0x0000004f,
SQ_PERF_SEL_INST_CYCLES_VMEM_CMD         = 0x00000050,
SQ_PERF_SEL_INST_CYCLES_EXP              = 0x00000051,
SQ_PERF_SEL_INST_CYCLES_GDS              = 0x00000052,
SQ_PERF_SEL_INST_CYCLES_SMEM             = 0x00000053,
SQ_PERF_SEL_INST_CYCLES_SALU             = 0x00000054,
SQ_PERF_SEL_THREAD_CYCLES_VALU           = 0x00000055,
SQ_PERF_SEL_THREAD_CYCLES_VALU_MAX       = 0x00000056,
SQ_PERF_SEL_IFETCH                       = 0x00000057,
SQ_PERF_SEL_IFETCH_LEVEL                 = 0x00000058,
SQ_PERF_SEL_CBRANCH_FORK                 = 0x00000059,
SQ_PERF_SEL_CBRANCH_FORK_SPLIT           = 0x0000005a,
SQ_PERF_SEL_VALU_LDS_DIRECT_RD           = 0x0000005b,
SQ_PERF_SEL_VALU_LDS_INTERP_OP           = 0x0000005c,
SQ_PERF_SEL_LDS_BANK_CONFLICT            = 0x0000005d,
SQ_PERF_SEL_LDS_ADDR_CONFLICT            = 0x0000005e,
SQ_PERF_SEL_LDS_UNALIGNED_STALL          = 0x0000005f,
SQ_PERF_SEL_LDS_MEM_VIOLATIONS           = 0x00000060,
SQ_PERF_SEL_LDS_ATOMIC_RETURN            = 0x00000061,
SQ_PERF_SEL_LDS_IDX_ACTIVE               = 0x00000062,
SQ_PERF_SEL_VALU_DEP_STALL               = 0x00000063,
SQ_PERF_SEL_VALU_STARVE                  = 0x00000064,
SQ_PERF_SEL_EXP_REQ_FIFO_FULL            = 0x00000065,
SQ_PERF_SEL_LDS_DATA_FIFO_FULL           = 0x00000066,
SQ_PERF_SEL_LDS_CMD_FIFO_FULL            = 0x00000067,
SQ_PERF_SEL_VMEM_TA_ADDR_FIFO_FULL       = 0x00000068,
SQ_PERF_SEL_VMEM_TA_CMD_FIFO_FULL        = 0x00000069,
SQ_PERF_SEL_VMEM_EX_DATA_REG_BUSY        = 0x0000006a,
SQ_PERF_SEL_VMEM_WR_TA_DATA_FIFO_FULL    = 0x0000006b,
SQ_PERF_SEL_VALU_SRC_C_CONFLICT          = 0x0000006c,
SQ_PERF_SEL_VMEM_RD_SRC_CD_CONFLICT      = 0x0000006d,
SQ_PERF_SEL_VMEM_WR_SRC_CD_CONFLICT      = 0x0000006e,
SQ_PERF_SEL_FLAT_SRC_CD_CONFLICT         = 0x0000006f,
SQ_PERF_SEL_LDS_SRC_CD_CONFLICT          = 0x00000070,
SQ_PERF_SEL_SRC_CD_BUSY                  = 0x00000071,
SQ_PERF_SEL_PT_POWER_STALL               = 0x00000072,
SQ_PERF_SEL_USER0                        = 0x00000073,
SQ_PERF_SEL_USER1                        = 0x00000074,
SQ_PERF_SEL_USER2                        = 0x00000075,
SQ_PERF_SEL_USER3                        = 0x00000076,
SQ_PERF_SEL_USER4                        = 0x00000077,
SQ_PERF_SEL_USER5                        = 0x00000078,
SQ_PERF_SEL_USER6                        = 0x00000079,
SQ_PERF_SEL_USER7                        = 0x0000007a,
SQ_PERF_SEL_USER8                        = 0x0000007b,
SQ_PERF_SEL_USER9                        = 0x0000007c,
SQ_PERF_SEL_USER10                       = 0x0000007d,
SQ_PERF_SEL_USER11                       = 0x0000007e,
SQ_PERF_SEL_USER12                       = 0x0000007f,
SQ_PERF_SEL_USER13                       = 0x00000080,
SQ_PERF_SEL_USER14                       = 0x00000081,
SQ_PERF_SEL_USER15                       = 0x00000082,
SQ_PERF_SEL_USER_LEVEL0                  = 0x00000083,
SQ_PERF_SEL_USER_LEVEL1                  = 0x00000084,
SQ_PERF_SEL_USER_LEVEL2                  = 0x00000085,
SQ_PERF_SEL_USER_LEVEL3                  = 0x00000086,
SQ_PERF_SEL_USER_LEVEL4                  = 0x00000087,
SQ_PERF_SEL_USER_LEVEL5                  = 0x00000088,
SQ_PERF_SEL_USER_LEVEL6                  = 0x00000089,
SQ_PERF_SEL_USER_LEVEL7                  = 0x0000008a,
SQ_PERF_SEL_USER_LEVEL8                  = 0x0000008b,
SQ_PERF_SEL_USER_LEVEL9                  = 0x0000008c,
SQ_PERF_SEL_USER_LEVEL10                 = 0x0000008d,
SQ_PERF_SEL_USER_LEVEL11                 = 0x0000008e,
SQ_PERF_SEL_USER_LEVEL12                 = 0x0000008f,
SQ_PERF_SEL_USER_LEVEL13                 = 0x00000090,
SQ_PERF_SEL_USER_LEVEL14                 = 0x00000091,
SQ_PERF_SEL_USER_LEVEL15                 = 0x00000092,
SQ_PERF_SEL_POWER_VALU                   = 0x00000093,
SQ_PERF_SEL_POWER_VALU0                  = 0x00000094,
SQ_PERF_SEL_POWER_VALU1                  = 0x00000095,
SQ_PERF_SEL_POWER_VALU2                  = 0x00000096,
SQ_PERF_SEL_POWER_GPR_RD                 = 0x00000097,
SQ_PERF_SEL_POWER_GPR_WR                 = 0x00000098,
SQ_PERF_SEL_POWER_LDS_BUSY               = 0x00000099,
SQ_PERF_SEL_POWER_ALU_BUSY               = 0x0000009a,
SQ_PERF_SEL_POWER_TEX_BUSY               = 0x0000009b,
SQ_PERF_SEL_ACCUM_PREV_HIRES             = 0x0000009c,
SQ_PERF_SEL_WAVES_RESTORED               = 0x0000009d,
SQ_PERF_SEL_WAVES_SAVED                  = 0x0000009e,
SQ_PERF_SEL_INSTS_SMEM_NORM              = 0x0000009f,
SQ_PERF_SEL_ATC_INSTS_VMEM               = 0x000000a0,
SQ_PERF_SEL_ATC_INST_LEVEL_VMEM          = 0x000000a1,
SQ_PERF_SEL_ATC_XNACK_FIRST              = 0x000000a2,
SQ_PERF_SEL_ATC_XNACK_ALL                = 0x000000a3,
SQ_PERF_SEL_ATC_XNACK_FIFO_FULL          = 0x000000a4,
SQ_PERF_SEL_ATC_INSTS_SMEM               = 0x000000a5,
SQ_PERF_SEL_ATC_INST_LEVEL_SMEM          = 0x000000a6,
SQ_PERF_SEL_IFETCH_XNACK                 = 0x000000a7,
SQ_PERF_SEL_TLB_SHOOTDOWN                = 0x000000a8,
SQ_PERF_SEL_TLB_SHOOTDOWN_CYCLES         = 0x000000a9,
SQ_PERF_SEL_INSTS_VMEM_WR_REPLAY         = 0x000000aa,
SQ_PERF_SEL_INSTS_VMEM_RD_REPLAY         = 0x000000ab,
SQ_PERF_SEL_INSTS_VMEM_REPLAY            = 0x000000ac,
SQ_PERF_SEL_INSTS_SMEM_REPLAY            = 0x000000ad,
SQ_PERF_SEL_INSTS_SMEM_NORM_REPLAY       = 0x000000ae,
SQ_PERF_SEL_INSTS_FLAT_REPLAY            = 0x000000af,
SQ_PERF_SEL_ATC_INSTS_VMEM_REPLAY        = 0x000000b0,
SQ_PERF_SEL_ATC_INSTS_SMEM_REPLAY        = 0x000000b1,
SQ_PERF_SEL_UTCL1_TRANSLATION_MISS       = 0x000000b2,
SQ_PERF_SEL_UTCL1_PERMISSION_MISS        = 0x000000b3,
SQ_PERF_SEL_UTCL1_REQUEST                = 0x000000b4,
SQ_PERF_SEL_UTCL1_STALL_MISSFIFO_FULL    = 0x000000b5,
SQ_PERF_SEL_UTCL1_STALL_INFLIGHT_MAX     = 0x000000b6,
SQ_PERF_SEL_UTCL1_STALL_LRU_INFLIGHT     = 0x000000b7,
SQ_PERF_SEL_UTCL1_LFIFO_FULL             = 0x000000b8,
SQ_PERF_SEL_UTCL1_STALL_LFIFO_NOT_RES    = 0x000000b9,
SQ_PERF_SEL_UTCL1_STALL_UTCL2_REQ_OUT_OF_CREDITS  = 0x000000ba,
SQ_PERF_SEL_DUMMY_END                    = 0x000000bb,
SQ_PERF_SEL_DUMMY_LAST                   = 0x000000ff,
SQC_PERF_SEL_ICACHE_INPUT_VALID_READY    = 0x00000100,
SQC_PERF_SEL_ICACHE_INPUT_VALID_READYB   = 0x00000101,
SQC_PERF_SEL_ICACHE_INPUT_VALIDB         = 0x00000102,
SQC_PERF_SEL_DCACHE_INPUT_VALID_READY    = 0x00000103,
SQC_PERF_SEL_DCACHE_INPUT_VALID_READYB   = 0x00000104,
SQC_PERF_SEL_DCACHE_INPUT_VALIDB         = 0x00000105,
SQC_PERF_SEL_TC_REQ                      = 0x00000106,
SQC_PERF_SEL_TC_INST_REQ                 = 0x00000107,
SQC_PERF_SEL_TC_DATA_READ_REQ            = 0x00000108,
SQC_PERF_SEL_TC_DATA_WRITE_REQ           = 0x00000109,
SQC_PERF_SEL_TC_DATA_ATOMIC_REQ          = 0x0000010a,
SQC_PERF_SEL_TC_STALL                    = 0x0000010b,
SQC_PERF_SEL_TC_STARVE                   = 0x0000010c,
SQC_PERF_SEL_ICACHE_BUSY_CYCLES          = 0x0000010d,
SQC_PERF_SEL_ICACHE_REQ                  = 0x0000010e,
SQC_PERF_SEL_ICACHE_HITS                 = 0x0000010f,
SQC_PERF_SEL_ICACHE_MISSES               = 0x00000110,
SQC_PERF_SEL_ICACHE_MISSES_DUPLICATE     = 0x00000111,
SQC_PERF_SEL_ICACHE_INVAL_INST           = 0x00000112,
SQC_PERF_SEL_ICACHE_INVAL_ASYNC          = 0x00000113,
SQC_PERF_SEL_ICACHE_INPUT_STALL_ARB_NO_GRANT  = 0x00000114,
SQC_PERF_SEL_ICACHE_INPUT_STALL_BANK_READYB  = 0x00000115,
SQC_PERF_SEL_ICACHE_CACHE_STALLED        = 0x00000116,
SQC_PERF_SEL_ICACHE_CACHE_STALL_INFLIGHT_NONZERO  = 0x00000117,
SQC_PERF_SEL_ICACHE_CACHE_STALL_INFLIGHT_MAX  = 0x00000118,
SQC_PERF_SEL_ICACHE_CACHE_STALL_OUTPUT   = 0x00000119,
SQC_PERF_SEL_ICACHE_CACHE_STALL_OUTPUT_MISS_FIFO  = 0x0000011a,
SQC_PERF_SEL_ICACHE_CACHE_STALL_OUTPUT_HIT_FIFO  = 0x0000011b,
SQC_PERF_SEL_ICACHE_CACHE_STALL_OUTPUT_TC_IF  = 0x0000011c,
SQC_PERF_SEL_ICACHE_STALL_OUTXBAR_ARB_NO_GRANT  = 0x0000011d,
SQC_PERF_SEL_ICACHE_PREFETCH_1           = 0x0000011e,
SQC_PERF_SEL_ICACHE_PREFETCH_2           = 0x0000011f,
SQC_PERF_SEL_ICACHE_PREFETCH_FILTERED    = 0x00000120,
SQC_PERF_SEL_DCACHE_BUSY_CYCLES          = 0x00000121,
SQC_PERF_SEL_DCACHE_REQ                  = 0x00000122,
SQC_PERF_SEL_DCACHE_HITS                 = 0x00000123,
SQC_PERF_SEL_DCACHE_MISSES               = 0x00000124,
SQC_PERF_SEL_DCACHE_MISSES_DUPLICATE     = 0x00000125,
SQC_PERF_SEL_DCACHE_HIT_LRU_READ         = 0x00000126,
SQC_PERF_SEL_DCACHE_MISS_EVICT_READ      = 0x00000127,
SQC_PERF_SEL_DCACHE_WC_LRU_WRITE         = 0x00000128,
SQC_PERF_SEL_DCACHE_WT_EVICT_WRITE       = 0x00000129,
SQC_PERF_SEL_DCACHE_ATOMIC               = 0x0000012a,
SQC_PERF_SEL_DCACHE_VOLATILE             = 0x0000012b,
SQC_PERF_SEL_DCACHE_INVAL_INST           = 0x0000012c,
SQC_PERF_SEL_DCACHE_INVAL_ASYNC          = 0x0000012d,
SQC_PERF_SEL_DCACHE_INVAL_VOLATILE_INST  = 0x0000012e,
SQC_PERF_SEL_DCACHE_INVAL_VOLATILE_ASYNC  = 0x0000012f,
SQC_PERF_SEL_DCACHE_WB_INST              = 0x00000130,
SQC_PERF_SEL_DCACHE_WB_ASYNC             = 0x00000131,
SQC_PERF_SEL_DCACHE_WB_VOLATILE_INST     = 0x00000132,
SQC_PERF_SEL_DCACHE_WB_VOLATILE_ASYNC    = 0x00000133,
SQC_PERF_SEL_DCACHE_INPUT_STALL_ARB_NO_GRANT  = 0x00000134,
SQC_PERF_SEL_DCACHE_INPUT_STALL_BANK_READYB  = 0x00000135,
SQC_PERF_SEL_DCACHE_CACHE_STALLED        = 0x00000136,
SQC_PERF_SEL_DCACHE_CACHE_STALL_INFLIGHT_MAX  = 0x00000137,
SQC_PERF_SEL_DCACHE_CACHE_STALL_OUTPUT   = 0x00000138,
SQC_PERF_SEL_DCACHE_CACHE_STALL_EVICT    = 0x00000139,
SQC_PERF_SEL_DCACHE_CACHE_STALL_UNORDERED  = 0x0000013a,
SQC_PERF_SEL_DCACHE_CACHE_STALL_ALLOC_UNAVAILABLE  = 0x0000013b,
SQC_PERF_SEL_DCACHE_CACHE_STALL_FORCE_EVICT  = 0x0000013c,
SQC_PERF_SEL_DCACHE_CACHE_STALL_MULTI_FLUSH  = 0x0000013d,
SQC_PERF_SEL_DCACHE_CACHE_STALL_FLUSH_DONE  = 0x0000013e,
SQC_PERF_SEL_DCACHE_CACHE_STALL_OUTPUT_MISS_FIFO  = 0x0000013f,
SQC_PERF_SEL_DCACHE_CACHE_STALL_OUTPUT_HIT_FIFO  = 0x00000140,
SQC_PERF_SEL_DCACHE_CACHE_STALL_OUTPUT_TC_IF  = 0x00000141,
SQC_PERF_SEL_DCACHE_STALL_OUTXBAR_ARB_NO_GRANT  = 0x00000142,
SQC_PERF_SEL_DCACHE_REQ_READ_1           = 0x00000143,
SQC_PERF_SEL_DCACHE_REQ_READ_2           = 0x00000144,
SQC_PERF_SEL_DCACHE_REQ_READ_4           = 0x00000145,
SQC_PERF_SEL_DCACHE_REQ_READ_8           = 0x00000146,
SQC_PERF_SEL_DCACHE_REQ_READ_16          = 0x00000147,
SQC_PERF_SEL_DCACHE_REQ_TIME             = 0x00000148,
SQC_PERF_SEL_DCACHE_REQ_WRITE_1          = 0x00000149,
SQC_PERF_SEL_DCACHE_REQ_WRITE_2          = 0x0000014a,
SQC_PERF_SEL_DCACHE_REQ_WRITE_4          = 0x0000014b,
SQC_PERF_SEL_DCACHE_REQ_ATC_PROBE        = 0x0000014c,
SQC_PERF_SEL_SQ_DCACHE_REQS              = 0x0000014d,
SQC_PERF_SEL_DCACHE_FLAT_REQ             = 0x0000014e,
SQC_PERF_SEL_DCACHE_NONFLAT_REQ          = 0x0000014f,
SQC_PERF_SEL_ICACHE_INFLIGHT_LEVEL       = 0x00000150,
SQC_PERF_SEL_DCACHE_INFLIGHT_LEVEL       = 0x00000151,
SQC_PERF_SEL_TC_INFLIGHT_LEVEL           = 0x00000152,
SQC_PERF_SEL_ICACHE_TC_INFLIGHT_LEVEL    = 0x00000153,
SQC_PERF_SEL_DCACHE_TC_INFLIGHT_LEVEL    = 0x00000154,
SQC_PERF_SEL_ICACHE_GATCL1_TRANSLATION_MISS  = 0x00000155,
SQC_PERF_SEL_ICACHE_GATCL1_PERMISSION_MISS  = 0x00000156,
SQC_PERF_SEL_ICACHE_GATCL1_REQUEST       = 0x00000157,
SQC_PERF_SEL_ICACHE_GATCL1_STALL_INFLIGHT_MAX  = 0x00000158,
SQC_PERF_SEL_ICACHE_GATCL1_STALL_LRU_INFLIGHT  = 0x00000159,
SQC_PERF_SEL_ICACHE_GATCL1_LFIFO_FULL    = 0x0000015a,
SQC_PERF_SEL_ICACHE_GATCL1_STALL_LFIFO_NOT_RES  = 0x0000015b,
SQC_PERF_SEL_ICACHE_GATCL1_STALL_ATCL2_REQ_OUT_OF_CREDITS  = 0x0000015c,
SQC_PERF_SEL_ICACHE_GATCL1_ATCL2_INFLIGHT  = 0x0000015d,
SQC_PERF_SEL_ICACHE_GATCL1_STALL_MISSFIFO_FULL  = 0x0000015e,
SQC_PERF_SEL_DCACHE_GATCL1_TRANSLATION_MISS  = 0x0000015f,
SQC_PERF_SEL_DCACHE_GATCL1_PERMISSION_MISS  = 0x00000160,
SQC_PERF_SEL_DCACHE_GATCL1_REQUEST       = 0x00000161,
SQC_PERF_SEL_DCACHE_GATCL1_STALL_INFLIGHT_MAX  = 0x00000162,
SQC_PERF_SEL_DCACHE_GATCL1_STALL_LRU_INFLIGHT  = 0x00000163,
SQC_PERF_SEL_DCACHE_GATCL1_LFIFO_FULL    = 0x00000164,
SQC_PERF_SEL_DCACHE_GATCL1_STALL_LFIFO_NOT_RES  = 0x00000165,
SQC_PERF_SEL_DCACHE_GATCL1_STALL_ATCL2_REQ_OUT_OF_CREDITS  = 0x00000166,
SQC_PERF_SEL_DCACHE_GATCL1_ATCL2_INFLIGHT  = 0x00000167,
SQC_PERF_SEL_DCACHE_GATCL1_STALL_MISSFIFO_FULL  = 0x00000168,
SQC_PERF_SEL_DCACHE_GATCL1_STALL_MULTI_MISS  = 0x00000169,
SQC_PERF_SEL_DCACHE_GATCL1_HIT_FIFO_FULL  = 0x0000016a,
SQC_PERF_SEL_DUMMY_LAST                  = 0x0000016b,
} SQ_PERF_SEL;

/*
 * SQ_CAC_POWER_SEL enum
 */

typedef enum SQ_CAC_POWER_SEL {
SQ_CAC_POWER_VALU                        = 0x00000000,
SQ_CAC_POWER_VALU0                       = 0x00000001,
SQ_CAC_POWER_VALU1                       = 0x00000002,
SQ_CAC_POWER_VALU2                       = 0x00000003,
SQ_CAC_POWER_GPR_RD                      = 0x00000004,
SQ_CAC_POWER_GPR_WR                      = 0x00000005,
SQ_CAC_POWER_LDS_BUSY                    = 0x00000006,
SQ_CAC_POWER_ALU_BUSY                    = 0x00000007,
SQ_CAC_POWER_TEX_BUSY                    = 0x00000008,
} SQ_CAC_POWER_SEL;

/*
 * SQ_IND_CMD_CMD enum
 */

typedef enum SQ_IND_CMD_CMD {
SQ_IND_CMD_CMD_NULL                      = 0x00000000,
SQ_IND_CMD_CMD_SETHALT                   = 0x00000001,
SQ_IND_CMD_CMD_SAVECTX                   = 0x00000002,
SQ_IND_CMD_CMD_KILL                      = 0x00000003,
SQ_IND_CMD_CMD_DEBUG                     = 0x00000004,
SQ_IND_CMD_CMD_TRAP                      = 0x00000005,
SQ_IND_CMD_CMD_SET_SPI_PRIO              = 0x00000006,
SQ_IND_CMD_CMD_SETFATALHALT              = 0x00000007,
} SQ_IND_CMD_CMD;

/*
 * SQ_IND_CMD_MODE enum
 */

typedef enum SQ_IND_CMD_MODE {
SQ_IND_CMD_MODE_SINGLE                   = 0x00000000,
SQ_IND_CMD_MODE_BROADCAST                = 0x00000001,
SQ_IND_CMD_MODE_BROADCAST_QUEUE          = 0x00000002,
SQ_IND_CMD_MODE_BROADCAST_PIPE           = 0x00000003,
SQ_IND_CMD_MODE_BROADCAST_ME             = 0x00000004,
} SQ_IND_CMD_MODE;

/*
 * SQ_EDC_INFO_SOURCE enum
 */

typedef enum SQ_EDC_INFO_SOURCE {
SQ_EDC_INFO_SOURCE_INVALID               = 0x00000000,
SQ_EDC_INFO_SOURCE_INST                  = 0x00000001,
SQ_EDC_INFO_SOURCE_SGPR                  = 0x00000002,
SQ_EDC_INFO_SOURCE_VGPR                  = 0x00000003,
SQ_EDC_INFO_SOURCE_LDS                   = 0x00000004,
SQ_EDC_INFO_SOURCE_GDS                   = 0x00000005,
SQ_EDC_INFO_SOURCE_TA                    = 0x00000006,
} SQ_EDC_INFO_SOURCE;

/*
 * SQ_ROUND_MODE enum
 */

typedef enum SQ_ROUND_MODE {
SQ_ROUND_NEAREST_EVEN                    = 0x00000000,
SQ_ROUND_PLUS_INFINITY                   = 0x00000001,
SQ_ROUND_MINUS_INFINITY                  = 0x00000002,
SQ_ROUND_TO_ZERO                         = 0x00000003,
} SQ_ROUND_MODE;

/*
 * SQ_INTERRUPT_WORD_ENCODING enum
 */

typedef enum SQ_INTERRUPT_WORD_ENCODING {
SQ_INTERRUPT_WORD_ENCODING_AUTO          = 0x00000000,
SQ_INTERRUPT_WORD_ENCODING_INST          = 0x00000001,
SQ_INTERRUPT_WORD_ENCODING_ERROR         = 0x00000002,
} SQ_INTERRUPT_WORD_ENCODING;

/*
 * ENUM_SQ_EXPORT_RAT_INST enum
 */

typedef enum ENUM_SQ_EXPORT_RAT_INST {
SQ_EXPORT_RAT_INST_NOP                   = 0x00000000,
SQ_EXPORT_RAT_INST_STORE_TYPED           = 0x00000001,
SQ_EXPORT_RAT_INST_STORE_RAW             = 0x00000002,
SQ_EXPORT_RAT_INST_STORE_RAW_FDENORM     = 0x00000003,
SQ_EXPORT_RAT_INST_CMPXCHG_INT           = 0x00000004,
SQ_EXPORT_RAT_INST_CMPXCHG_FLT           = 0x00000005,
SQ_EXPORT_RAT_INST_CMPXCHG_FDENORM       = 0x00000006,
SQ_EXPORT_RAT_INST_ADD                   = 0x00000007,
SQ_EXPORT_RAT_INST_SUB                   = 0x00000008,
SQ_EXPORT_RAT_INST_RSUB                  = 0x00000009,
SQ_EXPORT_RAT_INST_MIN_INT               = 0x0000000a,
SQ_EXPORT_RAT_INST_MIN_UINT              = 0x0000000b,
SQ_EXPORT_RAT_INST_MAX_INT               = 0x0000000c,
SQ_EXPORT_RAT_INST_MAX_UINT              = 0x0000000d,
SQ_EXPORT_RAT_INST_AND                   = 0x0000000e,
SQ_EXPORT_RAT_INST_OR                    = 0x0000000f,
SQ_EXPORT_RAT_INST_XOR                   = 0x00000010,
SQ_EXPORT_RAT_INST_MSKOR                 = 0x00000011,
SQ_EXPORT_RAT_INST_INC_UINT              = 0x00000012,
SQ_EXPORT_RAT_INST_DEC_UINT              = 0x00000013,
SQ_EXPORT_RAT_INST_STORE_DWORD           = 0x00000014,
SQ_EXPORT_RAT_INST_STORE_SHORT           = 0x00000015,
SQ_EXPORT_RAT_INST_STORE_BYTE            = 0x00000016,
SQ_EXPORT_RAT_INST_NOP_RTN               = 0x00000020,
SQ_EXPORT_RAT_INST_XCHG_RTN              = 0x00000022,
SQ_EXPORT_RAT_INST_XCHG_FDENORM_RTN      = 0x00000023,
SQ_EXPORT_RAT_INST_CMPXCHG_INT_RTN       = 0x00000024,
SQ_EXPORT_RAT_INST_CMPXCHG_FLT_RTN       = 0x00000025,
SQ_EXPORT_RAT_INST_CMPXCHG_FDENORM_RTN   = 0x00000026,
SQ_EXPORT_RAT_INST_ADD_RTN               = 0x00000027,
SQ_EXPORT_RAT_INST_SUB_RTN               = 0x00000028,
SQ_EXPORT_RAT_INST_RSUB_RTN              = 0x00000029,
SQ_EXPORT_RAT_INST_MIN_INT_RTN           = 0x0000002a,
SQ_EXPORT_RAT_INST_MIN_UINT_RTN          = 0x0000002b,
SQ_EXPORT_RAT_INST_MAX_INT_RTN           = 0x0000002c,
SQ_EXPORT_RAT_INST_MAX_UINT_RTN          = 0x0000002d,
SQ_EXPORT_RAT_INST_AND_RTN               = 0x0000002e,
SQ_EXPORT_RAT_INST_OR_RTN                = 0x0000002f,
SQ_EXPORT_RAT_INST_XOR_RTN               = 0x00000030,
SQ_EXPORT_RAT_INST_MSKOR_RTN             = 0x00000031,
SQ_EXPORT_RAT_INST_INC_UINT_RTN          = 0x00000032,
SQ_EXPORT_RAT_INST_DEC_UINT_RTN          = 0x00000033,
} ENUM_SQ_EXPORT_RAT_INST;

/*
 * SQ_IBUF_ST enum
 */

typedef enum SQ_IBUF_ST {
SQ_IBUF_IB_IDLE                          = 0x00000000,
SQ_IBUF_IB_INI_WAIT_GNT                  = 0x00000001,
SQ_IBUF_IB_INI_WAIT_DRET                 = 0x00000002,
SQ_IBUF_IB_LE_4DW                        = 0x00000003,
SQ_IBUF_IB_WAIT_DRET                     = 0x00000004,
SQ_IBUF_IB_EMPTY_WAIT_DRET               = 0x00000005,
SQ_IBUF_IB_DRET                          = 0x00000006,
SQ_IBUF_IB_EMPTY_WAIT_GNT                = 0x00000007,
} SQ_IBUF_ST;

/*
 * SQ_INST_STR_ST enum
 */

typedef enum SQ_INST_STR_ST {
SQ_INST_STR_IB_WAVE_NORML                = 0x00000000,
SQ_INST_STR_IB_WAVE2ID_NORMAL_INST_AV    = 0x00000001,
SQ_INST_STR_IB_WAVE_INTERNAL_INST_AV     = 0x00000002,
SQ_INST_STR_IB_WAVE_INST_SKIP_AV         = 0x00000003,
SQ_INST_STR_IB_WAVE_SETVSKIP_ST0         = 0x00000004,
SQ_INST_STR_IB_WAVE_SETVSKIP_ST1         = 0x00000005,
SQ_INST_STR_IB_WAVE_NOP_SLEEP_WAIT       = 0x00000006,
SQ_INST_STR_IB_WAVE_PC_FROM_SGPR_MSG_WAIT  = 0x00000007,
} SQ_INST_STR_ST;

/*
 * SQ_WAVE_IB_ECC_ST enum
 */

typedef enum SQ_WAVE_IB_ECC_ST {
SQ_WAVE_IB_ECC_CLEAN                     = 0x00000000,
SQ_WAVE_IB_ECC_ERR_CONTINUE              = 0x00000001,
SQ_WAVE_IB_ECC_ERR_HALT                  = 0x00000002,
SQ_WAVE_IB_ECC_WITH_ERR_MSG              = 0x00000003,
} SQ_WAVE_IB_ECC_ST;

/*
 * SH_MEM_ADDRESS_MODE enum
 */

typedef enum SH_MEM_ADDRESS_MODE {
SH_MEM_ADDRESS_MODE_64                   = 0x00000000,
SH_MEM_ADDRESS_MODE_32                   = 0x00000001,
} SH_MEM_ADDRESS_MODE;

/*
 * SH_MEM_ALIGNMENT_MODE enum
 */

typedef enum SH_MEM_ALIGNMENT_MODE {
SH_MEM_ALIGNMENT_MODE_DWORD              = 0x00000000,
SH_MEM_ALIGNMENT_MODE_DWORD_STRICT       = 0x00000001,
SH_MEM_ALIGNMENT_MODE_STRICT             = 0x00000002,
SH_MEM_ALIGNMENT_MODE_UNALIGNED          = 0x00000003,
} SH_MEM_ALIGNMENT_MODE;

/*
 * SQ_THREAD_TRACE_WAVE_START_COUNT_PREFIX enum
 */

typedef enum SQ_THREAD_TRACE_WAVE_START_COUNT_PREFIX {
SQ_THREAD_TRACE_WAVE_START_COUNT_PREFIX_WREXEC  = 0x00000018,
SQ_THREAD_TRACE_WAVE_START_COUNT_PREFIX_RESTORE  = 0x00000019,
} SQ_THREAD_TRACE_WAVE_START_COUNT_PREFIX;

/*
 * SQ_LB_CTR_SEL_VALUES enum
 */

typedef enum SQ_LB_CTR_SEL_VALUES {
SQ_LB_CTR_SEL_ALU_CYCLES                 = 0x00000000,
SQ_LB_CTR_SEL_ALU_STALLS                 = 0x00000001,
SQ_LB_CTR_SEL_TEX_CYCLES                 = 0x00000002,
SQ_LB_CTR_SEL_TEX_STALLS                 = 0x00000003,
SQ_LB_CTR_SEL_SALU_CYCLES                = 0x00000004,
SQ_LB_CTR_SEL_SCALAR_STALLS              = 0x00000005,
SQ_LB_CTR_SEL_SMEM_CYCLES                = 0x00000006,
SQ_LB_CTR_SEL_ICACHE_STALLS              = 0x00000007,
SQ_LB_CTR_SEL_DCACHE_STALLS              = 0x00000008,
SQ_LB_CTR_SEL_RESERVED0                  = 0x00000009,
SQ_LB_CTR_SEL_RESERVED1                  = 0x0000000a,
SQ_LB_CTR_SEL_RESERVED2                  = 0x0000000b,
SQ_LB_CTR_SEL_RESERVED3                  = 0x0000000c,
SQ_LB_CTR_SEL_RESERVED4                  = 0x0000000d,
SQ_LB_CTR_SEL_RESERVED5                  = 0x0000000e,
SQ_LB_CTR_SEL_RESERVED6                  = 0x0000000f,
} SQ_LB_CTR_SEL_VALUES;

/*
 * SQ_WAVE_TYPE value
 */

#define SQ_WAVE_TYPE_PS0               0x00000000

/*
 * SQIND_PARTITIONS value
 */

#define SQIND_GLOBAL_REGS_OFFSET       0x00000000
#define SQIND_GLOBAL_REGS_SIZE         0x00000008
#define SQIND_LOCAL_REGS_OFFSET        0x00000008
#define SQIND_LOCAL_REGS_SIZE          0x00000008
#define SQIND_WAVE_HWREGS_OFFSET       0x00000010
#define SQIND_WAVE_HWREGS_SIZE         0x000001f0
#define SQIND_WAVE_SGPRS_OFFSET        0x00000200
#define SQIND_WAVE_SGPRS_SIZE          0x00000200
#define SQIND_WAVE_VGPRS_OFFSET        0x00000400
#define SQIND_WAVE_VGPRS_SIZE          0x00000100

/*
 * SQ_GFXDEC value
 */

#define SQ_GFXDEC_BEGIN                0x0000a000
#define SQ_GFXDEC_END                  0x0000c000
#define SQ_GFXDEC_STATE_ID_SHIFT       0x0000000a

/*
 * SQDEC value
 */

#define SQDEC_BEGIN                    0x00002300
#define SQDEC_END                      0x000023ff

/*
 * SQPERFSDEC value
 */

#define SQPERFSDEC_BEGIN               0x0000d9c0
#define SQPERFSDEC_END                 0x0000da40

/*
 * SQPERFDDEC value
 */

#define SQPERFDDEC_BEGIN               0x0000d1c0
#define SQPERFDDEC_END                 0x0000d240

/*
 * SQGFXUDEC value
 */

#define SQGFXUDEC_BEGIN                0x0000c330
#define SQGFXUDEC_END                  0x0000c380

/*
 * SQPWRDEC value
 */

#define SQPWRDEC_BEGIN                 0x0000f08c
#define SQPWRDEC_END                   0x0000f094

/*
 * SQ_DISPATCHER value
 */

#define SQ_DISPATCHER_GFX_MIN          0x00000010
#define SQ_DISPATCHER_GFX_CNT_PER_RING 0x00000008

/*
 * SQ_MAX value
 */

#define SQ_MAX_PGM_SGPRS               0x00000068
#define SQ_MAX_PGM_VGPRS               0x00000100

/*
 * SQ_THREAD_TRACE_TIME_UNIT value
 */

#define SQ_THREAD_TRACE_TIME_UNIT      0x00000004

/*
 * SQ_EXCP_BITS value
 */

#define SQ_EX_MODE_EXCP_VALU_BASE      0x00000000
#define SQ_EX_MODE_EXCP_VALU_SIZE      0x00000007
#define SQ_EX_MODE_EXCP_INVALID        0x00000000
#define SQ_EX_MODE_EXCP_INPUT_DENORM   0x00000001
#define SQ_EX_MODE_EXCP_DIV0           0x00000002
#define SQ_EX_MODE_EXCP_OVERFLOW       0x00000003
#define SQ_EX_MODE_EXCP_UNDERFLOW      0x00000004
#define SQ_EX_MODE_EXCP_INEXACT        0x00000005
#define SQ_EX_MODE_EXCP_INT_DIV0       0x00000006
#define SQ_EX_MODE_EXCP_ADDR_WATCH0    0x00000007
#define SQ_EX_MODE_EXCP_MEM_VIOL       0x00000008

/*
 * SQ_EXCP_HI_BITS value
 */

#define SQ_EX_MODE_EXCP_HI_ADDR_WATCH1 0x00000000
#define SQ_EX_MODE_EXCP_HI_ADDR_WATCH2 0x00000001
#define SQ_EX_MODE_EXCP_HI_ADDR_WATCH3 0x00000002

/*
 * HW_INSERTED_INST_ID value
 */

#define INST_ID_PRIV_START             0x80000000
#define INST_ID_ECC_INTERRUPT_MSG      0xfffffff0
#define INST_ID_TTRACE_NEW_PC_MSG      0xfffffff1
#define INST_ID_HW_TRAP                0xfffffff2
#define INST_ID_KILL_SEQ               0xfffffff3
#define INST_ID_SPI_WREXEC             0xfffffff4
#define INST_ID_HOST_REG_TRAP_MSG      0xfffffffe

/*
 * SIMM16_WAITCNT_PARTITIONS value
 */

#define SIMM16_WAITCNT_VM_CNT_START    0x00000000
#define SIMM16_WAITCNT_VM_CNT_SIZE     0x00000004
#define SIMM16_WAITCNT_EXP_CNT_START   0x00000004
#define SIMM16_WAITCNT_EXP_CNT_SIZE    0x00000003
#define SIMM16_WAITCNT_LGKM_CNT_START  0x00000008
#define SIMM16_WAITCNT_LGKM_CNT_SIZE   0x00000004
#define SIMM16_WAITCNT_VM_CNT_HI_START 0x0000000e
#define SIMM16_WAITCNT_VM_CNT_HI_SIZE  0x00000002

/*
 * SQ_EDC_FUE_CNTL_BITS value
 */

#define SQ_EDC_FUE_CNTL_SQ             0x00000000
#define SQ_EDC_FUE_CNTL_LDS            0x00000001
#define SQ_EDC_FUE_CNTL_SIMD0          0x00000002
#define SQ_EDC_FUE_CNTL_SIMD1          0x00000003
#define SQ_EDC_FUE_CNTL_SIMD2          0x00000004
#define SQ_EDC_FUE_CNTL_SIMD3          0x00000005
#define SQ_EDC_FUE_CNTL_TA             0x00000006
#define SQ_EDC_FUE_CNTL_TD             0x00000007
#define SQ_EDC_FUE_CNTL_TCP            0x00000008

/*******************************************************
 * COMP Enums
 *******************************************************/

/*
 * CSDATA_TYPE enum
 */

typedef enum CSDATA_TYPE {
CSDATA_TYPE_TG                           = 0x00000000,
CSDATA_TYPE_STATE                        = 0x00000001,
CSDATA_TYPE_EVENT                        = 0x00000002,
CSDATA_TYPE_PRIVATE                      = 0x00000003,
} CSDATA_TYPE;

/*
 * CSDATA_TYPE_WIDTH value
 */

#define CSDATA_TYPE_WIDTH              0x00000002

/*
 * CSDATA_ADDR_WIDTH value
 */

#define CSDATA_ADDR_WIDTH              0x00000007

/*
 * CSDATA_DATA_WIDTH value
 */

#define CSDATA_DATA_WIDTH              0x00000020

/*******************************************************
 * VGT Enums
 *******************************************************/

/*
 * VGT_OUT_PRIM_TYPE enum
 */

typedef enum VGT_OUT_PRIM_TYPE {
VGT_OUT_POINT                            = 0x00000000,
VGT_OUT_LINE                             = 0x00000001,
VGT_OUT_TRI                              = 0x00000002,
VGT_OUT_RECT_V0                          = 0x00000003,
VGT_OUT_RECT_V1                          = 0x00000004,
VGT_OUT_RECT_V2                          = 0x00000005,
VGT_OUT_RECT_V3                          = 0x00000006,
VGT_OUT_2D_RECT                          = 0x00000007,
VGT_TE_QUAD                              = 0x00000008,
VGT_TE_PRIM_INDEX_LINE                   = 0x00000009,
VGT_TE_PRIM_INDEX_TRI                    = 0x0000000a,
VGT_TE_PRIM_INDEX_QUAD                   = 0x0000000b,
VGT_OUT_LINE_ADJ                         = 0x0000000c,
VGT_OUT_TRI_ADJ                          = 0x0000000d,
VGT_OUT_PATCH                            = 0x0000000e,
} VGT_OUT_PRIM_TYPE;

/*
 * VGT_DI_PRIM_TYPE enum
 */

typedef enum VGT_DI_PRIM_TYPE {
DI_PT_NONE                               = 0x00000000,
DI_PT_POINTLIST                          = 0x00000001,
DI_PT_LINELIST                           = 0x00000002,
DI_PT_LINESTRIP                          = 0x00000003,
DI_PT_TRILIST                            = 0x00000004,
DI_PT_TRIFAN                             = 0x00000005,
DI_PT_TRISTRIP                           = 0x00000006,
DI_PT_2D_RECTANGLE                       = 0x00000007,
DI_PT_UNUSED_1                           = 0x00000008,
DI_PT_PATCH                              = 0x00000009,
DI_PT_LINELIST_ADJ                       = 0x0000000a,
DI_PT_LINESTRIP_ADJ                      = 0x0000000b,
DI_PT_TRILIST_ADJ                        = 0x0000000c,
DI_PT_TRISTRIP_ADJ                       = 0x0000000d,
DI_PT_UNUSED_3                           = 0x0000000e,
DI_PT_UNUSED_4                           = 0x0000000f,
DI_PT_TRI_WITH_WFLAGS                    = 0x00000010,
DI_PT_RECTLIST                           = 0x00000011,
DI_PT_LINELOOP                           = 0x00000012,
DI_PT_QUADLIST                           = 0x00000013,
DI_PT_QUADSTRIP                          = 0x00000014,
DI_PT_POLYGON                            = 0x00000015,
} VGT_DI_PRIM_TYPE;

/*
 * VGT_DI_SOURCE_SELECT enum
 */

typedef enum VGT_DI_SOURCE_SELECT {
DI_SRC_SEL_DMA                           = 0x00000000,
DI_SRC_SEL_IMMEDIATE                     = 0x00000001,
DI_SRC_SEL_AUTO_INDEX                    = 0x00000002,
DI_SRC_SEL_RESERVED                      = 0x00000003,
} VGT_DI_SOURCE_SELECT;

/*
 * VGT_DI_MAJOR_MODE_SELECT enum
 */

typedef enum VGT_DI_MAJOR_MODE_SELECT {
DI_MAJOR_MODE_0                          = 0x00000000,
DI_MAJOR_MODE_1                          = 0x00000001,
} VGT_DI_MAJOR_MODE_SELECT;

/*
 * VGT_DI_INDEX_SIZE enum
 */

typedef enum VGT_DI_INDEX_SIZE {
DI_INDEX_SIZE_16_BIT                     = 0x00000000,
DI_INDEX_SIZE_32_BIT                     = 0x00000001,
DI_INDEX_SIZE_8_BIT                      = 0x00000002,
} VGT_DI_INDEX_SIZE;

/*
 * VGT_EVENT_TYPE enum
 */

typedef enum VGT_EVENT_TYPE {
Reserved_0x00                            = 0x00000000,
SAMPLE_STREAMOUTSTATS1                   = 0x00000001,
SAMPLE_STREAMOUTSTATS2                   = 0x00000002,
SAMPLE_STREAMOUTSTATS3                   = 0x00000003,
CACHE_FLUSH_TS                           = 0x00000004,
CONTEXT_DONE                             = 0x00000005,
CACHE_FLUSH                              = 0x00000006,
CS_PARTIAL_FLUSH                         = 0x00000007,
VGT_STREAMOUT_SYNC                       = 0x00000008,
Reserved_0x09                            = 0x00000009,
VGT_STREAMOUT_RESET                      = 0x0000000a,
END_OF_PIPE_INCR_DE                      = 0x0000000b,
END_OF_PIPE_IB_END                       = 0x0000000c,
RST_PIX_CNT                              = 0x0000000d,
BREAK_BATCH                              = 0x0000000e,
VS_PARTIAL_FLUSH                         = 0x0000000f,
PS_PARTIAL_FLUSH                         = 0x00000010,
FLUSH_HS_OUTPUT                          = 0x00000011,
FLUSH_DFSM                               = 0x00000012,
RESET_TO_LOWEST_VGT                      = 0x00000013,
CACHE_FLUSH_AND_INV_TS_EVENT             = 0x00000014,
ZPASS_DONE                               = 0x00000015,
CACHE_FLUSH_AND_INV_EVENT                = 0x00000016,
PERFCOUNTER_START                        = 0x00000017,
PERFCOUNTER_STOP                         = 0x00000018,
PIPELINESTAT_START                       = 0x00000019,
PIPELINESTAT_STOP                        = 0x0000001a,
PERFCOUNTER_SAMPLE                       = 0x0000001b,
Available_0x1c                           = 0x0000001c,
Available_0x1d                           = 0x0000001d,
SAMPLE_PIPELINESTAT                      = 0x0000001e,
SO_VGTSTREAMOUT_FLUSH                    = 0x0000001f,
SAMPLE_STREAMOUTSTATS                    = 0x00000020,
RESET_VTX_CNT                            = 0x00000021,
BLOCK_CONTEXT_DONE                       = 0x00000022,
CS_CONTEXT_DONE                          = 0x00000023,
VGT_FLUSH                                = 0x00000024,
TGID_ROLLOVER                            = 0x00000025,
SQ_NON_EVENT                             = 0x00000026,
SC_SEND_DB_VPZ                           = 0x00000027,
BOTTOM_OF_PIPE_TS                        = 0x00000028,
FLUSH_SX_TS                              = 0x00000029,
DB_CACHE_FLUSH_AND_INV                   = 0x0000002a,
FLUSH_AND_INV_DB_DATA_TS                 = 0x0000002b,
FLUSH_AND_INV_DB_META                    = 0x0000002c,
FLUSH_AND_INV_CB_DATA_TS                 = 0x0000002d,
FLUSH_AND_INV_CB_META                    = 0x0000002e,
CS_DONE                                  = 0x0000002f,
PS_DONE                                  = 0x00000030,
FLUSH_AND_INV_CB_PIXEL_DATA              = 0x00000031,
SX_CB_RAT_ACK_REQUEST                    = 0x00000032,
THREAD_TRACE_START                       = 0x00000033,
THREAD_TRACE_STOP                        = 0x00000034,
THREAD_TRACE_MARKER                      = 0x00000035,
THREAD_TRACE_FLUSH                       = 0x00000036,
THREAD_TRACE_FINISH                      = 0x00000037,
PIXEL_PIPE_STAT_CONTROL                  = 0x00000038,
PIXEL_PIPE_STAT_DUMP                     = 0x00000039,
PIXEL_PIPE_STAT_RESET                    = 0x0000003a,
CONTEXT_SUSPEND                          = 0x0000003b,
OFFCHIP_HS_DEALLOC                       = 0x0000003c,
ENABLE_NGG_PIPELINE                      = 0x0000003d,
ENABLE_LEGACY_PIPELINE                   = 0x0000003e,
Reserved_0x3f                            = 0x0000003f,
} VGT_EVENT_TYPE;

/*
 * VGT_DMA_SWAP_MODE enum
 */

typedef enum VGT_DMA_SWAP_MODE {
VGT_DMA_SWAP_NONE                        = 0x00000000,
VGT_DMA_SWAP_16_BIT                      = 0x00000001,
VGT_DMA_SWAP_32_BIT                      = 0x00000002,
VGT_DMA_SWAP_WORD                        = 0x00000003,
} VGT_DMA_SWAP_MODE;

/*
 * VGT_INDEX_TYPE_MODE enum
 */

typedef enum VGT_INDEX_TYPE_MODE {
VGT_INDEX_16                             = 0x00000000,
VGT_INDEX_32                             = 0x00000001,
VGT_INDEX_8                              = 0x00000002,
} VGT_INDEX_TYPE_MODE;

/*
 * VGT_DMA_BUF_TYPE enum
 */

typedef enum VGT_DMA_BUF_TYPE {
VGT_DMA_BUF_MEM                          = 0x00000000,
VGT_DMA_BUF_RING                         = 0x00000001,
VGT_DMA_BUF_SETUP                        = 0x00000002,
VGT_DMA_PTR_UPDATE                       = 0x00000003,
} VGT_DMA_BUF_TYPE;

/*
 * VGT_OUTPATH_SELECT enum
 */

typedef enum VGT_OUTPATH_SELECT {
VGT_OUTPATH_VTX_REUSE                    = 0x00000000,
VGT_OUTPATH_TESS_EN                      = 0x00000001,
VGT_OUTPATH_PASSTHRU                     = 0x00000002,
VGT_OUTPATH_GS_BLOCK                     = 0x00000003,
VGT_OUTPATH_HS_BLOCK                     = 0x00000004,
VGT_OUTPATH_PRIM_GEN                     = 0x00000005,
} VGT_OUTPATH_SELECT;

/*
 * VGT_GRP_PRIM_TYPE enum
 */

typedef enum VGT_GRP_PRIM_TYPE {
VGT_GRP_3D_POINT                         = 0x00000000,
VGT_GRP_3D_LINE                          = 0x00000001,
VGT_GRP_3D_TRI                           = 0x00000002,
VGT_GRP_3D_RECT                          = 0x00000003,
VGT_GRP_3D_QUAD                          = 0x00000004,
VGT_GRP_2D_COPY_RECT_V0                  = 0x00000005,
VGT_GRP_2D_COPY_RECT_V1                  = 0x00000006,
VGT_GRP_2D_COPY_RECT_V2                  = 0x00000007,
VGT_GRP_2D_COPY_RECT_V3                  = 0x00000008,
VGT_GRP_2D_FILL_RECT                     = 0x00000009,
VGT_GRP_2D_LINE                          = 0x0000000a,
VGT_GRP_2D_TRI                           = 0x0000000b,
VGT_GRP_PRIM_INDEX_LINE                  = 0x0000000c,
VGT_GRP_PRIM_INDEX_TRI                   = 0x0000000d,
VGT_GRP_PRIM_INDEX_QUAD                  = 0x0000000e,
VGT_GRP_3D_LINE_ADJ                      = 0x0000000f,
VGT_GRP_3D_TRI_ADJ                       = 0x00000010,
VGT_GRP_3D_PATCH                         = 0x00000011,
VGT_GRP_2D_RECT                          = 0x00000012,
} VGT_GRP_PRIM_TYPE;

/*
 * VGT_GRP_PRIM_ORDER enum
 */

typedef enum VGT_GRP_PRIM_ORDER {
VGT_GRP_LIST                             = 0x00000000,
VGT_GRP_STRIP                            = 0x00000001,
VGT_GRP_FAN                              = 0x00000002,
VGT_GRP_LOOP                             = 0x00000003,
VGT_GRP_POLYGON                          = 0x00000004,
} VGT_GRP_PRIM_ORDER;

/*
 * VGT_GROUP_CONV_SEL enum
 */

typedef enum VGT_GROUP_CONV_SEL {
VGT_GRP_INDEX_16                         = 0x00000000,
VGT_GRP_INDEX_32                         = 0x00000001,
VGT_GRP_UINT_16                          = 0x00000002,
VGT_GRP_UINT_32                          = 0x00000003,
VGT_GRP_SINT_16                          = 0x00000004,
VGT_GRP_SINT_32                          = 0x00000005,
VGT_GRP_FLOAT_32                         = 0x00000006,
VGT_GRP_AUTO_PRIM                        = 0x00000007,
VGT_GRP_FIX_1_23_TO_FLOAT                = 0x00000008,
} VGT_GROUP_CONV_SEL;

/*
 * VGT_GS_MODE_TYPE enum
 */

typedef enum VGT_GS_MODE_TYPE {
GS_OFF                                   = 0x00000000,
GS_SCENARIO_A                            = 0x00000001,
GS_SCENARIO_B                            = 0x00000002,
GS_SCENARIO_G                            = 0x00000003,
GS_SCENARIO_C                            = 0x00000004,
SPRITE_EN                                = 0x00000005,
} VGT_GS_MODE_TYPE;

/*
 * VGT_GS_CUT_MODE enum
 */

typedef enum VGT_GS_CUT_MODE {
GS_CUT_1024                              = 0x00000000,
GS_CUT_512                               = 0x00000001,
GS_CUT_256                               = 0x00000002,
GS_CUT_128                               = 0x00000003,
} VGT_GS_CUT_MODE;

/*
 * VGT_GS_OUTPRIM_TYPE enum
 */

typedef enum VGT_GS_OUTPRIM_TYPE {
POINTLIST                                = 0x00000000,
LINESTRIP                                = 0x00000001,
TRISTRIP                                 = 0x00000002,
RECTLIST                                 = 0x00000003,
} VGT_GS_OUTPRIM_TYPE;

/*
 * VGT_CACHE_INVALID_MODE enum
 */

typedef enum VGT_CACHE_INVALID_MODE {
VC_ONLY                                  = 0x00000000,
TC_ONLY                                  = 0x00000001,
VC_AND_TC                                = 0x00000002,
} VGT_CACHE_INVALID_MODE;

/*
 * VGT_TESS_TYPE enum
 */

typedef enum VGT_TESS_TYPE {
TESS_ISOLINE                             = 0x00000000,
TESS_TRIANGLE                            = 0x00000001,
TESS_QUAD                                = 0x00000002,
} VGT_TESS_TYPE;

/*
 * VGT_TESS_PARTITION enum
 */

typedef enum VGT_TESS_PARTITION {
PART_INTEGER                             = 0x00000000,
PART_POW2                                = 0x00000001,
PART_FRAC_ODD                            = 0x00000002,
PART_FRAC_EVEN                           = 0x00000003,
} VGT_TESS_PARTITION;

/*
 * VGT_TESS_TOPOLOGY enum
 */

typedef enum VGT_TESS_TOPOLOGY {
OUTPUT_POINT                             = 0x00000000,
OUTPUT_LINE                              = 0x00000001,
OUTPUT_TRIANGLE_CW                       = 0x00000002,
OUTPUT_TRIANGLE_CCW                      = 0x00000003,
} VGT_TESS_TOPOLOGY;

/*
 * VGT_RDREQ_POLICY enum
 */

typedef enum VGT_RDREQ_POLICY {
VGT_POLICY_LRU                           = 0x00000000,
VGT_POLICY_STREAM                        = 0x00000001,
} VGT_RDREQ_POLICY;

/*
 * VGT_DIST_MODE enum
 */

typedef enum VGT_DIST_MODE {
NO_DIST                                  = 0x00000000,
PATCHES                                  = 0x00000001,
DONUTS                                   = 0x00000002,
TRAPEZOIDS                               = 0x00000003,
} VGT_DIST_MODE;

/*
 * VGT_STAGES_LS_EN enum
 */

typedef enum VGT_STAGES_LS_EN {
LS_STAGE_OFF                             = 0x00000000,
LS_STAGE_ON                              = 0x00000001,
CS_STAGE_ON                              = 0x00000002,
RESERVED_LS                              = 0x00000003,
} VGT_STAGES_LS_EN;

/*
 * VGT_STAGES_HS_EN enum
 */

typedef enum VGT_STAGES_HS_EN {
HS_STAGE_OFF                             = 0x00000000,
HS_STAGE_ON                              = 0x00000001,
} VGT_STAGES_HS_EN;

/*
 * VGT_STAGES_ES_EN enum
 */

typedef enum VGT_STAGES_ES_EN {
ES_STAGE_OFF                             = 0x00000000,
ES_STAGE_DS                              = 0x00000001,
ES_STAGE_REAL                            = 0x00000002,
RESERVED_ES                              = 0x00000003,
} VGT_STAGES_ES_EN;

/*
 * VGT_STAGES_GS_EN enum
 */

typedef enum VGT_STAGES_GS_EN {
GS_STAGE_OFF                             = 0x00000000,
GS_STAGE_ON                              = 0x00000001,
} VGT_STAGES_GS_EN;

/*
 * VGT_STAGES_VS_EN enum
 */

typedef enum VGT_STAGES_VS_EN {
VS_STAGE_REAL                            = 0x00000000,
VS_STAGE_DS                              = 0x00000001,
VS_STAGE_COPY_SHADER                     = 0x00000002,
RESERVED_VS                              = 0x00000003,
} VGT_STAGES_VS_EN;

/*
 * VGT_PERFCOUNT_SELECT enum
 */

typedef enum VGT_PERFCOUNT_SELECT {
vgt_perf_VGT_SPI_ESTHREAD_EVENT_WINDOW_ACTIVE  = 0x00000000,
vgt_perf_VGT_SPI_ESVERT_VALID            = 0x00000001,
vgt_perf_VGT_SPI_ESVERT_EOV              = 0x00000002,
vgt_perf_VGT_SPI_ESVERT_STALLED          = 0x00000003,
vgt_perf_VGT_SPI_ESVERT_STARVED_BUSY     = 0x00000004,
vgt_perf_VGT_SPI_ESVERT_STARVED_IDLE     = 0x00000005,
vgt_perf_VGT_SPI_ESVERT_STATIC           = 0x00000006,
vgt_perf_VGT_SPI_ESTHREAD_IS_EVENT       = 0x00000007,
vgt_perf_VGT_SPI_ESTHREAD_SEND           = 0x00000008,
vgt_perf_VGT_SPI_GSPRIM_VALID            = 0x00000009,
vgt_perf_VGT_SPI_GSPRIM_EOV              = 0x0000000a,
vgt_perf_VGT_SPI_GSPRIM_CONT             = 0x0000000b,
vgt_perf_VGT_SPI_GSPRIM_STALLED          = 0x0000000c,
vgt_perf_VGT_SPI_GSPRIM_STARVED_BUSY     = 0x0000000d,
vgt_perf_VGT_SPI_GSPRIM_STARVED_IDLE     = 0x0000000e,
vgt_perf_VGT_SPI_GSPRIM_STATIC           = 0x0000000f,
vgt_perf_VGT_SPI_GSTHREAD_EVENT_WINDOW_ACTIVE  = 0x00000010,
vgt_perf_VGT_SPI_GSTHREAD_IS_EVENT       = 0x00000011,
vgt_perf_VGT_SPI_GSTHREAD_SEND           = 0x00000012,
vgt_perf_VGT_SPI_VSTHREAD_EVENT_WINDOW_ACTIVE  = 0x00000013,
vgt_perf_VGT_SPI_VSVERT_SEND             = 0x00000014,
vgt_perf_VGT_SPI_VSVERT_EOV              = 0x00000015,
vgt_perf_VGT_SPI_VSVERT_STALLED          = 0x00000016,
vgt_perf_VGT_SPI_VSVERT_STARVED_BUSY     = 0x00000017,
vgt_perf_VGT_SPI_VSVERT_STARVED_IDLE     = 0x00000018,
vgt_perf_VGT_SPI_VSVERT_STATIC           = 0x00000019,
vgt_perf_VGT_SPI_VSTHREAD_IS_EVENT       = 0x0000001a,
vgt_perf_VGT_SPI_VSTHREAD_SEND           = 0x0000001b,
vgt_perf_VGT_PA_EVENT_WINDOW_ACTIVE      = 0x0000001c,
vgt_perf_VGT_PA_CLIPV_SEND               = 0x0000001d,
vgt_perf_VGT_PA_CLIPV_FIRSTVERT          = 0x0000001e,
vgt_perf_VGT_PA_CLIPV_STALLED            = 0x0000001f,
vgt_perf_VGT_PA_CLIPV_STARVED_BUSY       = 0x00000020,
vgt_perf_VGT_PA_CLIPV_STARVED_IDLE       = 0x00000021,
vgt_perf_VGT_PA_CLIPV_STATIC             = 0x00000022,
vgt_perf_VGT_PA_CLIPP_SEND               = 0x00000023,
vgt_perf_VGT_PA_CLIPP_EOP                = 0x00000024,
vgt_perf_VGT_PA_CLIPP_IS_EVENT           = 0x00000025,
vgt_perf_VGT_PA_CLIPP_NULL_PRIM          = 0x00000026,
vgt_perf_VGT_PA_CLIPP_NEW_VTX_VECT       = 0x00000027,
vgt_perf_VGT_PA_CLIPP_STALLED            = 0x00000028,
vgt_perf_VGT_PA_CLIPP_STARVED_BUSY       = 0x00000029,
vgt_perf_VGT_PA_CLIPP_STARVED_IDLE       = 0x0000002a,
vgt_perf_VGT_PA_CLIPP_STATIC             = 0x0000002b,
vgt_perf_VGT_PA_CLIPS_SEND               = 0x0000002c,
vgt_perf_VGT_PA_CLIPS_STALLED            = 0x0000002d,
vgt_perf_VGT_PA_CLIPS_STARVED_BUSY       = 0x0000002e,
vgt_perf_VGT_PA_CLIPS_STARVED_IDLE       = 0x0000002f,
vgt_perf_VGT_PA_CLIPS_STATIC             = 0x00000030,
vgt_perf_vsvert_ds_send                  = 0x00000031,
vgt_perf_vsvert_api_send                 = 0x00000032,
vgt_perf_hs_tif_stall                    = 0x00000033,
vgt_perf_hs_input_stall                  = 0x00000034,
vgt_perf_hs_interface_stall              = 0x00000035,
vgt_perf_hs_tfm_stall                    = 0x00000036,
vgt_perf_te11_starved                    = 0x00000037,
vgt_perf_gs_event_stall                  = 0x00000038,
vgt_perf_vgt_pa_clipp_send_not_event     = 0x00000039,
vgt_perf_vgt_pa_clipp_valid_prim         = 0x0000003a,
vgt_perf_reused_es_indices               = 0x0000003b,
vgt_perf_vs_cache_hits                   = 0x0000003c,
vgt_perf_gs_cache_hits                   = 0x0000003d,
vgt_perf_ds_cache_hits                   = 0x0000003e,
vgt_perf_total_cache_hits                = 0x0000003f,
vgt_perf_vgt_busy                        = 0x00000040,
vgt_perf_vgt_gs_busy                     = 0x00000041,
vgt_perf_esvert_stalled_es_tbl           = 0x00000042,
vgt_perf_esvert_stalled_gs_tbl           = 0x00000043,
vgt_perf_esvert_stalled_gs_event         = 0x00000044,
vgt_perf_esvert_stalled_gsprim           = 0x00000045,
vgt_perf_gsprim_stalled_es_tbl           = 0x00000046,
vgt_perf_gsprim_stalled_gs_tbl           = 0x00000047,
vgt_perf_gsprim_stalled_gs_event         = 0x00000048,
vgt_perf_gsprim_stalled_esvert           = 0x00000049,
vgt_perf_esthread_stalled_es_rb_full     = 0x0000004a,
vgt_perf_esthread_stalled_spi_bp         = 0x0000004b,
vgt_perf_counters_avail_stalled          = 0x0000004c,
vgt_perf_gs_rb_space_avail_stalled       = 0x0000004d,
vgt_perf_gs_issue_rtr_stalled            = 0x0000004e,
vgt_perf_gsthread_stalled                = 0x0000004f,
vgt_perf_strmout_stalled                 = 0x00000050,
vgt_perf_wait_for_es_done_stalled        = 0x00000051,
vgt_perf_cm_stalled_by_gog               = 0x00000052,
vgt_perf_cm_reading_stalled              = 0x00000053,
vgt_perf_cm_stalled_by_gsfetch_done      = 0x00000054,
vgt_perf_gog_vs_tbl_stalled              = 0x00000055,
vgt_perf_gog_out_indx_stalled            = 0x00000056,
vgt_perf_gog_out_prim_stalled            = 0x00000057,
vgt_perf_waveid_stalled                  = 0x00000058,
vgt_perf_gog_busy                        = 0x00000059,
vgt_perf_reused_vs_indices               = 0x0000005a,
vgt_perf_sclk_reg_vld_event              = 0x0000005b,
vgt_perf_vs_conflicting_indices          = 0x0000005c,
vgt_perf_sclk_core_vld_event             = 0x0000005d,
vgt_perf_hswave_stalled                  = 0x0000005e,
vgt_perf_sclk_gs_vld_event               = 0x0000005f,
vgt_perf_VGT_SPI_LSVERT_VALID            = 0x00000060,
vgt_perf_VGT_SPI_LSVERT_EOV              = 0x00000061,
vgt_perf_VGT_SPI_LSVERT_STALLED          = 0x00000062,
vgt_perf_VGT_SPI_LSVERT_STARVED_BUSY     = 0x00000063,
vgt_perf_VGT_SPI_LSVERT_STARVED_IDLE     = 0x00000064,
vgt_perf_VGT_SPI_LSVERT_STATIC           = 0x00000065,
vgt_perf_VGT_SPI_LSWAVE_EVENT_WINDOW_ACTIVE  = 0x00000066,
vgt_perf_VGT_SPI_LSWAVE_IS_EVENT         = 0x00000067,
vgt_perf_VGT_SPI_LSWAVE_SEND             = 0x00000068,
vgt_perf_VGT_SPI_HSVERT_VALID            = 0x00000069,
vgt_perf_VGT_SPI_HSVERT_EOV              = 0x0000006a,
vgt_perf_VGT_SPI_HSVERT_STALLED          = 0x0000006b,
vgt_perf_VGT_SPI_HSVERT_STARVED_BUSY     = 0x0000006c,
vgt_perf_VGT_SPI_HSVERT_STARVED_IDLE     = 0x0000006d,
vgt_perf_VGT_SPI_HSVERT_STATIC           = 0x0000006e,
vgt_perf_VGT_SPI_HSWAVE_EVENT_WINDOW_ACTIVE  = 0x0000006f,
vgt_perf_VGT_SPI_HSWAVE_IS_EVENT         = 0x00000070,
vgt_perf_VGT_SPI_HSWAVE_SEND             = 0x00000071,
vgt_perf_ds_prims                        = 0x00000072,
vgt_perf_ds_RESERVED                     = 0x00000073,
vgt_perf_ls_thread_groups                = 0x00000074,
vgt_perf_hs_thread_groups                = 0x00000075,
vgt_perf_es_thread_groups                = 0x00000076,
vgt_perf_vs_thread_groups                = 0x00000077,
vgt_perf_ls_done_latency                 = 0x00000078,
vgt_perf_hs_done_latency                 = 0x00000079,
vgt_perf_es_done_latency                 = 0x0000007a,
vgt_perf_gs_done_latency                 = 0x0000007b,
vgt_perf_vgt_hs_busy                     = 0x0000007c,
vgt_perf_vgt_te11_busy                   = 0x0000007d,
vgt_perf_ls_flush                        = 0x0000007e,
vgt_perf_hs_flush                        = 0x0000007f,
vgt_perf_es_flush                        = 0x00000080,
vgt_perf_vgt_pa_clipp_eopg               = 0x00000081,
vgt_perf_ls_done                         = 0x00000082,
vgt_perf_hs_done                         = 0x00000083,
vgt_perf_es_done                         = 0x00000084,
vgt_perf_gs_done                         = 0x00000085,
vgt_perf_vsfetch_done                    = 0x00000086,
vgt_perf_gs_done_received                = 0x00000087,
vgt_perf_es_ring_high_water_mark         = 0x00000088,
vgt_perf_gs_ring_high_water_mark         = 0x00000089,
vgt_perf_vs_table_high_water_mark        = 0x0000008a,
vgt_perf_hs_tgs_active_high_water_mark   = 0x0000008b,
vgt_perf_pa_clipp_dealloc                = 0x0000008c,
vgt_perf_cut_mem_flush_stalled           = 0x0000008d,
vgt_perf_vsvert_work_received            = 0x0000008e,
vgt_perf_vgt_pa_clipp_starved_after_work  = 0x0000008f,
vgt_perf_te11_con_starved_after_work     = 0x00000090,
vgt_perf_hs_waiting_on_ls_done_stall     = 0x00000091,
vgt_spi_vsvert_valid                     = 0x00000092,
} VGT_PERFCOUNT_SELECT;

/*
 * IA_PERFCOUNT_SELECT enum
 */

typedef enum IA_PERFCOUNT_SELECT {
ia_perf_GRP_INPUT_EVENT_WINDOW_ACTIVE    = 0x00000000,
ia_perf_dma_data_fifo_full               = 0x00000001,
ia_perf_RESERVED1                        = 0x00000002,
ia_perf_RESERVED2                        = 0x00000003,
ia_perf_RESERVED3                        = 0x00000004,
ia_perf_RESERVED4                        = 0x00000005,
ia_perf_RESERVED5                        = 0x00000006,
ia_perf_MC_LAT_BIN_0                     = 0x00000007,
ia_perf_MC_LAT_BIN_1                     = 0x00000008,
ia_perf_MC_LAT_BIN_2                     = 0x00000009,
ia_perf_MC_LAT_BIN_3                     = 0x0000000a,
ia_perf_MC_LAT_BIN_4                     = 0x0000000b,
ia_perf_MC_LAT_BIN_5                     = 0x0000000c,
ia_perf_MC_LAT_BIN_6                     = 0x0000000d,
ia_perf_MC_LAT_BIN_7                     = 0x0000000e,
ia_perf_ia_busy                          = 0x0000000f,
ia_perf_ia_sclk_reg_vld_event            = 0x00000010,
ia_perf_RESERVED6                        = 0x00000011,
ia_perf_ia_sclk_core_vld_event           = 0x00000012,
ia_perf_RESERVED7                        = 0x00000013,
ia_perf_ia_dma_return                    = 0x00000014,
ia_perf_ia_stalled                       = 0x00000015,
ia_perf_shift_starved_pipe0_event        = 0x00000016,
ia_perf_shift_starved_pipe1_event        = 0x00000017,
} IA_PERFCOUNT_SELECT;

/*
 * WD_PERFCOUNT_SELECT enum
 */

typedef enum WD_PERFCOUNT_SELECT {
wd_perf_RBIU_FIFOS_EVENT_WINDOW_ACTIVE   = 0x00000000,
wd_perf_RBIU_DR_FIFO_STARVED             = 0x00000001,
wd_perf_RBIU_DR_FIFO_STALLED             = 0x00000002,
wd_perf_RBIU_DI_FIFO_STARVED             = 0x00000003,
wd_perf_RBIU_DI_FIFO_STALLED             = 0x00000004,
wd_perf_wd_busy                          = 0x00000005,
wd_perf_wd_sclk_reg_vld_event            = 0x00000006,
wd_perf_wd_sclk_input_vld_event          = 0x00000007,
wd_perf_wd_sclk_core_vld_event           = 0x00000008,
wd_perf_wd_stalled                       = 0x00000009,
wd_perf_inside_tf_bin_0                  = 0x0000000a,
wd_perf_inside_tf_bin_1                  = 0x0000000b,
wd_perf_inside_tf_bin_2                  = 0x0000000c,
wd_perf_inside_tf_bin_3                  = 0x0000000d,
wd_perf_inside_tf_bin_4                  = 0x0000000e,
wd_perf_inside_tf_bin_5                  = 0x0000000f,
wd_perf_inside_tf_bin_6                  = 0x00000010,
wd_perf_inside_tf_bin_7                  = 0x00000011,
wd_perf_inside_tf_bin_8                  = 0x00000012,
wd_perf_tfreq_lat_bin_0                  = 0x00000013,
wd_perf_tfreq_lat_bin_1                  = 0x00000014,
wd_perf_tfreq_lat_bin_2                  = 0x00000015,
wd_perf_tfreq_lat_bin_3                  = 0x00000016,
wd_perf_tfreq_lat_bin_4                  = 0x00000017,
wd_perf_tfreq_lat_bin_5                  = 0x00000018,
wd_perf_tfreq_lat_bin_6                  = 0x00000019,
wd_perf_tfreq_lat_bin_7                  = 0x0000001a,
wd_starved_on_hs_done                    = 0x0000001b,
wd_perf_se0_hs_done_latency              = 0x0000001c,
wd_perf_se1_hs_done_latency              = 0x0000001d,
wd_perf_se2_hs_done_latency              = 0x0000001e,
wd_perf_se3_hs_done_latency              = 0x0000001f,
wd_perf_hs_done_se0                      = 0x00000020,
wd_perf_hs_done_se1                      = 0x00000021,
wd_perf_hs_done_se2                      = 0x00000022,
wd_perf_hs_done_se3                      = 0x00000023,
wd_perf_null_patches                     = 0x00000024,
} WD_PERFCOUNT_SELECT;

/*
 * WD_IA_DRAW_TYPE enum
 */

typedef enum WD_IA_DRAW_TYPE {
WD_IA_DRAW_TYPE_DI_MM0                   = 0x00000000,
WD_IA_DRAW_TYPE_REG_XFER                 = 0x00000001,
WD_IA_DRAW_TYPE_EVENT_INIT               = 0x00000002,
WD_IA_DRAW_TYPE_EVENT_ADDR               = 0x00000003,
WD_IA_DRAW_TYPE_MIN_INDX                 = 0x00000004,
WD_IA_DRAW_TYPE_MAX_INDX                 = 0x00000005,
WD_IA_DRAW_TYPE_INDX_OFF                 = 0x00000006,
WD_IA_DRAW_TYPE_IMM_DATA                 = 0x00000007,
} WD_IA_DRAW_TYPE;

/*
 * WD_IA_DRAW_REG_XFER enum
 */

typedef enum WD_IA_DRAW_REG_XFER {
WD_IA_DRAW_REG_XFER_IA_MULTI_VGT_PARAM   = 0x00000000,
WD_IA_DRAW_REG_XFER_VGT_MULTI_PRIM_IB_RESET_EN = 0x00000001,
} WD_IA_DRAW_REG_XFER;

/*
 * WD_IA_DRAW_SOURCE enum
 */

typedef enum WD_IA_DRAW_SOURCE {
WD_IA_DRAW_SOURCE_DMA                    = 0x00000000,
WD_IA_DRAW_SOURCE_IMMD                   = 0x00000001,
WD_IA_DRAW_SOURCE_AUTO                   = 0x00000002,
WD_IA_DRAW_SOURCE_OPAQ                   = 0x00000003,
} WD_IA_DRAW_SOURCE;

/*
 * GS_THREADID_SIZE value
 */

#define GSTHREADID_SIZE                0x00000002

/*******************************************************
 * GB Enums
 *******************************************************/

/*
 * GB_EDC_DED_MODE enum
 */

typedef enum GB_EDC_DED_MODE {
GB_EDC_DED_MODE_LOG                      = 0x00000000,
GB_EDC_DED_MODE_HALT                     = 0x00000001,
GB_EDC_DED_MODE_INT_HALT                 = 0x00000002,
} GB_EDC_DED_MODE;

/*
 * VALUE_GB_TILING_CONFIG_TABLE_SIZE value
 */

#define GB_TILING_CONFIG_TABLE_SIZE    0x00000020

/*
 * VALUE_GB_TILING_CONFIG_MACROTABLE_SIZE value
 */

#define GB_TILING_CONFIG_MACROTABLE_SIZE 0x00000010

/*******************************************************
 * TP Enums
 *******************************************************/

/*
 * TA_TC_ADDR_MODES enum
 */

typedef enum TA_TC_ADDR_MODES {
TA_TC_ADDR_MODE_DEFAULT                  = 0x00000000,
TA_TC_ADDR_MODE_COMP0                    = 0x00000001,
TA_TC_ADDR_MODE_COMP1                    = 0x00000002,
TA_TC_ADDR_MODE_COMP2                    = 0x00000003,
TA_TC_ADDR_MODE_COMP3                    = 0x00000004,
TA_TC_ADDR_MODE_UNALIGNED                = 0x00000005,
TA_TC_ADDR_MODE_BORDER_COLOR             = 0x00000006,
} TA_TC_ADDR_MODES;

/*
 * TA_PERFCOUNT_SEL enum
 */

typedef enum TA_PERFCOUNT_SEL {
TA_PERF_SEL_NULL                         = 0x00000000,
TA_PERF_SEL_sh_fifo_busy                 = 0x00000001,
TA_PERF_SEL_sh_fifo_cmd_busy             = 0x00000002,
TA_PERF_SEL_sh_fifo_addr_busy            = 0x00000003,
TA_PERF_SEL_sh_fifo_data_busy            = 0x00000004,
TA_PERF_SEL_sh_fifo_data_sfifo_busy      = 0x00000005,
TA_PERF_SEL_sh_fifo_data_tfifo_busy      = 0x00000006,
TA_PERF_SEL_gradient_busy                = 0x00000007,
TA_PERF_SEL_gradient_fifo_busy           = 0x00000008,
TA_PERF_SEL_lod_busy                     = 0x00000009,
TA_PERF_SEL_lod_fifo_busy                = 0x0000000a,
TA_PERF_SEL_addresser_busy               = 0x0000000b,
TA_PERF_SEL_addresser_fifo_busy          = 0x0000000c,
TA_PERF_SEL_aligner_busy                 = 0x0000000d,
TA_PERF_SEL_write_path_busy              = 0x0000000e,
TA_PERF_SEL_ta_busy                      = 0x0000000f,
TA_PERF_SEL_sq_ta_cmd_cycles             = 0x00000010,
TA_PERF_SEL_sp_ta_addr_cycles            = 0x00000011,
TA_PERF_SEL_sp_ta_data_cycles            = 0x00000012,
TA_PERF_SEL_ta_fa_data_state_cycles      = 0x00000013,
TA_PERF_SEL_sh_fifo_addr_waiting_on_cmd_cycles  = 0x00000014,
TA_PERF_SEL_sh_fifo_cmd_waiting_on_addr_cycles  = 0x00000015,
TA_PERF_SEL_sh_fifo_addr_starved_while_busy_cycles  = 0x00000016,
TA_PERF_SEL_sh_fifo_cmd_starved_while_busy_cycles  = 0x00000017,
TA_PERF_SEL_sh_fifo_data_waiting_on_data_state_cycles  = 0x00000018,
TA_PERF_SEL_sh_fifo_data_state_waiting_on_data_cycles  = 0x00000019,
TA_PERF_SEL_sh_fifo_data_starved_while_busy_cycles  = 0x0000001a,
TA_PERF_SEL_sh_fifo_data_state_starved_while_busy_cycles  = 0x0000001b,
TA_PERF_SEL_RESERVED_28                  = 0x0000001c,
TA_PERF_SEL_RESERVED_29                  = 0x0000001d,
TA_PERF_SEL_sh_fifo_addr_cycles          = 0x0000001e,
TA_PERF_SEL_sh_fifo_data_cycles          = 0x0000001f,
TA_PERF_SEL_total_wavefronts             = 0x00000020,
TA_PERF_SEL_gradient_cycles              = 0x00000021,
TA_PERF_SEL_walker_cycles                = 0x00000022,
TA_PERF_SEL_aligner_cycles               = 0x00000023,
TA_PERF_SEL_image_wavefronts             = 0x00000024,
TA_PERF_SEL_image_read_wavefronts        = 0x00000025,
TA_PERF_SEL_image_write_wavefronts       = 0x00000026,
TA_PERF_SEL_image_atomic_wavefronts      = 0x00000027,
TA_PERF_SEL_image_total_cycles           = 0x00000028,
TA_PERF_SEL_RESERVED_41                  = 0x00000029,
TA_PERF_SEL_RESERVED_42                  = 0x0000002a,
TA_PERF_SEL_RESERVED_43                  = 0x0000002b,
TA_PERF_SEL_buffer_wavefronts            = 0x0000002c,
TA_PERF_SEL_buffer_read_wavefronts       = 0x0000002d,
TA_PERF_SEL_buffer_write_wavefronts      = 0x0000002e,
TA_PERF_SEL_buffer_atomic_wavefronts     = 0x0000002f,
TA_PERF_SEL_buffer_coalescable_wavefronts  = 0x00000030,
TA_PERF_SEL_buffer_total_cycles          = 0x00000031,
TA_PERF_SEL_buffer_coalescable_addr_multicycled_cycles  = 0x00000032,
TA_PERF_SEL_buffer_coalescable_clamp_16kdword_multicycled_cycles  = 0x00000033,
TA_PERF_SEL_buffer_coalesced_read_cycles  = 0x00000034,
TA_PERF_SEL_buffer_coalesced_write_cycles  = 0x00000035,
TA_PERF_SEL_addr_stalled_by_tc_cycles    = 0x00000036,
TA_PERF_SEL_addr_stalled_by_td_cycles    = 0x00000037,
TA_PERF_SEL_data_stalled_by_tc_cycles    = 0x00000038,
TA_PERF_SEL_addresser_stalled_by_aligner_only_cycles  = 0x00000039,
TA_PERF_SEL_addresser_stalled_cycles     = 0x0000003a,
TA_PERF_SEL_aniso_stalled_by_addresser_only_cycles  = 0x0000003b,
TA_PERF_SEL_aniso_stalled_cycles         = 0x0000003c,
TA_PERF_SEL_deriv_stalled_by_aniso_only_cycles  = 0x0000003d,
TA_PERF_SEL_deriv_stalled_cycles         = 0x0000003e,
TA_PERF_SEL_aniso_gt1_cycle_quads        = 0x0000003f,
TA_PERF_SEL_color_1_cycle_pixels         = 0x00000040,
TA_PERF_SEL_color_2_cycle_pixels         = 0x00000041,
TA_PERF_SEL_color_3_cycle_pixels         = 0x00000042,
TA_PERF_SEL_color_4_cycle_pixels         = 0x00000043,
TA_PERF_SEL_mip_1_cycle_pixels           = 0x00000044,
TA_PERF_SEL_mip_2_cycle_pixels           = 0x00000045,
TA_PERF_SEL_vol_1_cycle_pixels           = 0x00000046,
TA_PERF_SEL_vol_2_cycle_pixels           = 0x00000047,
TA_PERF_SEL_bilin_point_1_cycle_pixels   = 0x00000048,
TA_PERF_SEL_mipmap_lod_0_samples         = 0x00000049,
TA_PERF_SEL_mipmap_lod_1_samples         = 0x0000004a,
TA_PERF_SEL_mipmap_lod_2_samples         = 0x0000004b,
TA_PERF_SEL_mipmap_lod_3_samples         = 0x0000004c,
TA_PERF_SEL_mipmap_lod_4_samples         = 0x0000004d,
TA_PERF_SEL_mipmap_lod_5_samples         = 0x0000004e,
TA_PERF_SEL_mipmap_lod_6_samples         = 0x0000004f,
TA_PERF_SEL_mipmap_lod_7_samples         = 0x00000050,
TA_PERF_SEL_mipmap_lod_8_samples         = 0x00000051,
TA_PERF_SEL_mipmap_lod_9_samples         = 0x00000052,
TA_PERF_SEL_mipmap_lod_10_samples        = 0x00000053,
TA_PERF_SEL_mipmap_lod_11_samples        = 0x00000054,
TA_PERF_SEL_mipmap_lod_12_samples        = 0x00000055,
TA_PERF_SEL_mipmap_lod_13_samples        = 0x00000056,
TA_PERF_SEL_mipmap_lod_14_samples        = 0x00000057,
TA_PERF_SEL_mipmap_invalid_samples       = 0x00000058,
TA_PERF_SEL_aniso_1_cycle_quads          = 0x00000059,
TA_PERF_SEL_aniso_2_cycle_quads          = 0x0000005a,
TA_PERF_SEL_aniso_4_cycle_quads          = 0x0000005b,
TA_PERF_SEL_aniso_6_cycle_quads          = 0x0000005c,
TA_PERF_SEL_aniso_8_cycle_quads          = 0x0000005d,
TA_PERF_SEL_aniso_10_cycle_quads         = 0x0000005e,
TA_PERF_SEL_aniso_12_cycle_quads         = 0x0000005f,
TA_PERF_SEL_aniso_14_cycle_quads         = 0x00000060,
TA_PERF_SEL_aniso_16_cycle_quads         = 0x00000061,
TA_PERF_SEL_write_path_input_cycles      = 0x00000062,
TA_PERF_SEL_write_path_output_cycles     = 0x00000063,
TA_PERF_SEL_flat_wavefronts              = 0x00000064,
TA_PERF_SEL_flat_read_wavefronts         = 0x00000065,
TA_PERF_SEL_flat_write_wavefronts        = 0x00000066,
TA_PERF_SEL_flat_atomic_wavefronts       = 0x00000067,
TA_PERF_SEL_flat_coalesceable_wavefronts  = 0x00000068,
TA_PERF_SEL_reg_sclk_vld                 = 0x00000069,
TA_PERF_SEL_local_cg_dyn_sclk_grp0_en    = 0x0000006a,
TA_PERF_SEL_local_cg_dyn_sclk_grp1_en    = 0x0000006b,
TA_PERF_SEL_local_cg_dyn_sclk_grp1_mems_en  = 0x0000006c,
TA_PERF_SEL_local_cg_dyn_sclk_grp4_en    = 0x0000006d,
TA_PERF_SEL_local_cg_dyn_sclk_grp5_en    = 0x0000006e,
TA_PERF_SEL_xnack_on_phase0              = 0x0000006f,
TA_PERF_SEL_xnack_on_phase1              = 0x00000070,
TA_PERF_SEL_xnack_on_phase2              = 0x00000071,
TA_PERF_SEL_xnack_on_phase3              = 0x00000072,
TA_PERF_SEL_first_xnack_on_phase0        = 0x00000073,
TA_PERF_SEL_first_xnack_on_phase1        = 0x00000074,
TA_PERF_SEL_first_xnack_on_phase2        = 0x00000075,
TA_PERF_SEL_first_xnack_on_phase3        = 0x00000076,
} TA_PERFCOUNT_SEL;

/*
 * TD_PERFCOUNT_SEL enum
 */

typedef enum TD_PERFCOUNT_SEL {
TD_PERF_SEL_none                         = 0x00000000,
TD_PERF_SEL_td_busy                      = 0x00000001,
TD_PERF_SEL_input_busy                   = 0x00000002,
TD_PERF_SEL_output_busy                  = 0x00000003,
TD_PERF_SEL_lerp_busy                    = 0x00000004,
TD_PERF_SEL_reg_sclk_vld                 = 0x00000005,
TD_PERF_SEL_local_cg_dyn_sclk_grp0_en    = 0x00000006,
TD_PERF_SEL_local_cg_dyn_sclk_grp1_en    = 0x00000007,
TD_PERF_SEL_local_cg_dyn_sclk_grp4_en    = 0x00000008,
TD_PERF_SEL_local_cg_dyn_sclk_grp5_en    = 0x00000009,
TD_PERF_SEL_tc_td_fifo_full              = 0x0000000a,
TD_PERF_SEL_constant_state_full          = 0x0000000b,
TD_PERF_SEL_sample_state_full            = 0x0000000c,
TD_PERF_SEL_output_fifo_full             = 0x0000000d,
TD_PERF_SEL_RESERVED_14                  = 0x0000000e,
TD_PERF_SEL_tc_stall                     = 0x0000000f,
TD_PERF_SEL_pc_stall                     = 0x00000010,
TD_PERF_SEL_gds_stall                    = 0x00000011,
TD_PERF_SEL_RESERVED_18                  = 0x00000012,
TD_PERF_SEL_RESERVED_19                  = 0x00000013,
TD_PERF_SEL_gather4_wavefront            = 0x00000014,
TD_PERF_SEL_gather4h_wavefront           = 0x00000015,
TD_PERF_SEL_gather4h_packed_wavefront    = 0x00000016,
TD_PERF_SEL_gather8h_packed_wavefront    = 0x00000017,
TD_PERF_SEL_sample_c_wavefront           = 0x00000018,
TD_PERF_SEL_load_wavefront               = 0x00000019,
TD_PERF_SEL_atomic_wavefront             = 0x0000001a,
TD_PERF_SEL_store_wavefront              = 0x0000001b,
TD_PERF_SEL_ldfptr_wavefront             = 0x0000001c,
TD_PERF_SEL_d16_en_wavefront             = 0x0000001d,
TD_PERF_SEL_bypass_filter_wavefront      = 0x0000001e,
TD_PERF_SEL_min_max_filter_wavefront     = 0x0000001f,
TD_PERF_SEL_coalescable_wavefront        = 0x00000020,
TD_PERF_SEL_coalesced_phase              = 0x00000021,
TD_PERF_SEL_four_phase_wavefront         = 0x00000022,
TD_PERF_SEL_eight_phase_wavefront        = 0x00000023,
TD_PERF_SEL_sixteen_phase_wavefront      = 0x00000024,
TD_PERF_SEL_four_phase_forward_wavefront  = 0x00000025,
TD_PERF_SEL_write_ack_wavefront          = 0x00000026,
TD_PERF_SEL_RESERVED_39                  = 0x00000027,
TD_PERF_SEL_user_defined_border          = 0x00000028,
TD_PERF_SEL_white_border                 = 0x00000029,
TD_PERF_SEL_opaque_black_border          = 0x0000002a,
TD_PERF_SEL_RESERVED_43                  = 0x0000002b,
TD_PERF_SEL_RESERVED_44                  = 0x0000002c,
TD_PERF_SEL_nack                         = 0x0000002d,
TD_PERF_SEL_td_sp_traffic                = 0x0000002e,
TD_PERF_SEL_consume_gds_traffic          = 0x0000002f,
TD_PERF_SEL_addresscmd_poison            = 0x00000030,
TD_PERF_SEL_data_poison                  = 0x00000031,
TD_PERF_SEL_start_cycle_0                = 0x00000032,
TD_PERF_SEL_start_cycle_1                = 0x00000033,
TD_PERF_SEL_start_cycle_2                = 0x00000034,
TD_PERF_SEL_start_cycle_3                = 0x00000035,
TD_PERF_SEL_null_cycle_output            = 0x00000036,
TD_PERF_SEL_d16_data_packed              = 0x00000037,
TD_PERF_SEL_texels_zeroed_out_by_blend_zero_prt  = 0x00000038,
} TD_PERFCOUNT_SEL;

/*
 * TCP_PERFCOUNT_SELECT enum
 */

typedef enum TCP_PERFCOUNT_SELECT {
TCP_PERF_SEL_TA_TCP_ADDR_STARVE_CYCLES   = 0x00000000,
TCP_PERF_SEL_TA_TCP_DATA_STARVE_CYCLES   = 0x00000001,
TCP_PERF_SEL_TCP_TA_ADDR_STALL_CYCLES    = 0x00000002,
TCP_PERF_SEL_TCP_TA_DATA_STALL_CYCLES    = 0x00000003,
TCP_PERF_SEL_TD_TCP_STALL_CYCLES         = 0x00000004,
TCP_PERF_SEL_TCR_TCP_STALL_CYCLES        = 0x00000005,
TCP_PERF_SEL_LOD_STALL_CYCLES            = 0x00000006,
TCP_PERF_SEL_READ_TAGCONFLICT_STALL_CYCLES  = 0x00000007,
TCP_PERF_SEL_WRITE_TAGCONFLICT_STALL_CYCLES  = 0x00000008,
TCP_PERF_SEL_ATOMIC_TAGCONFLICT_STALL_CYCLES  = 0x00000009,
TCP_PERF_SEL_ALLOC_STALL_CYCLES          = 0x0000000a,
TCP_PERF_SEL_LFIFO_STALL_CYCLES          = 0x0000000b,
TCP_PERF_SEL_RFIFO_STALL_CYCLES          = 0x0000000c,
TCP_PERF_SEL_TCR_RDRET_STALL             = 0x0000000d,
TCP_PERF_SEL_WRITE_CONFLICT_STALL        = 0x0000000e,
TCP_PERF_SEL_HOLE_READ_STALL             = 0x0000000f,
TCP_PERF_SEL_READCONFLICT_STALL_CYCLES   = 0x00000010,
TCP_PERF_SEL_PENDING_STALL_CYCLES        = 0x00000011,
TCP_PERF_SEL_READFIFO_STALL_CYCLES       = 0x00000012,
TCP_PERF_SEL_TCP_LATENCY                 = 0x00000013,
TCP_PERF_SEL_TCC_READ_REQ_LATENCY        = 0x00000014,
TCP_PERF_SEL_TCC_WRITE_REQ_LATENCY       = 0x00000015,
TCP_PERF_SEL_TCC_WRITE_REQ_HOLE_LATENCY  = 0x00000016,
TCP_PERF_SEL_TCC_READ_REQ                = 0x00000017,
TCP_PERF_SEL_TCC_WRITE_REQ               = 0x00000018,
TCP_PERF_SEL_TCC_ATOMIC_WITH_RET_REQ     = 0x00000019,
TCP_PERF_SEL_TCC_ATOMIC_WITHOUT_RET_REQ  = 0x0000001a,
TCP_PERF_SEL_TOTAL_LOCAL_READ            = 0x0000001b,
TCP_PERF_SEL_TOTAL_GLOBAL_READ           = 0x0000001c,
TCP_PERF_SEL_TOTAL_LOCAL_WRITE           = 0x0000001d,
TCP_PERF_SEL_TOTAL_GLOBAL_WRITE          = 0x0000001e,
TCP_PERF_SEL_TOTAL_ATOMIC_WITH_RET       = 0x0000001f,
TCP_PERF_SEL_TOTAL_ATOMIC_WITHOUT_RET    = 0x00000020,
TCP_PERF_SEL_TOTAL_WBINVL1               = 0x00000021,
TCP_PERF_SEL_IMG_READ_FMT_1              = 0x00000022,
TCP_PERF_SEL_IMG_READ_FMT_8              = 0x00000023,
TCP_PERF_SEL_IMG_READ_FMT_16             = 0x00000024,
TCP_PERF_SEL_IMG_READ_FMT_32             = 0x00000025,
TCP_PERF_SEL_IMG_READ_FMT_32_AS_8        = 0x00000026,
TCP_PERF_SEL_IMG_READ_FMT_32_AS_16       = 0x00000027,
TCP_PERF_SEL_IMG_READ_FMT_32_AS_128      = 0x00000028,
TCP_PERF_SEL_IMG_READ_FMT_64_2_CYCLE     = 0x00000029,
TCP_PERF_SEL_IMG_READ_FMT_64_1_CYCLE     = 0x0000002a,
TCP_PERF_SEL_IMG_READ_FMT_96             = 0x0000002b,
TCP_PERF_SEL_IMG_READ_FMT_128_4_CYCLE    = 0x0000002c,
TCP_PERF_SEL_IMG_READ_FMT_128_1_CYCLE    = 0x0000002d,
TCP_PERF_SEL_IMG_READ_FMT_BC1            = 0x0000002e,
TCP_PERF_SEL_IMG_READ_FMT_BC2            = 0x0000002f,
TCP_PERF_SEL_IMG_READ_FMT_BC3            = 0x00000030,
TCP_PERF_SEL_IMG_READ_FMT_BC4            = 0x00000031,
TCP_PERF_SEL_IMG_READ_FMT_BC5            = 0x00000032,
TCP_PERF_SEL_IMG_READ_FMT_BC6            = 0x00000033,
TCP_PERF_SEL_IMG_READ_FMT_BC7            = 0x00000034,
TCP_PERF_SEL_IMG_READ_FMT_I8             = 0x00000035,
TCP_PERF_SEL_IMG_READ_FMT_I16            = 0x00000036,
TCP_PERF_SEL_IMG_READ_FMT_I32            = 0x00000037,
TCP_PERF_SEL_IMG_READ_FMT_I32_AS_8       = 0x00000038,
TCP_PERF_SEL_IMG_READ_FMT_I32_AS_16      = 0x00000039,
TCP_PERF_SEL_IMG_READ_FMT_D8             = 0x0000003a,
TCP_PERF_SEL_IMG_READ_FMT_D16            = 0x0000003b,
TCP_PERF_SEL_IMG_READ_FMT_D32            = 0x0000003c,
TCP_PERF_SEL_IMG_WRITE_FMT_8             = 0x0000003d,
TCP_PERF_SEL_IMG_WRITE_FMT_16            = 0x0000003e,
TCP_PERF_SEL_IMG_WRITE_FMT_32            = 0x0000003f,
TCP_PERF_SEL_IMG_WRITE_FMT_64            = 0x00000040,
TCP_PERF_SEL_IMG_WRITE_FMT_128           = 0x00000041,
TCP_PERF_SEL_IMG_WRITE_FMT_D8            = 0x00000042,
TCP_PERF_SEL_IMG_WRITE_FMT_D16           = 0x00000043,
TCP_PERF_SEL_IMG_WRITE_FMT_D32           = 0x00000044,
TCP_PERF_SEL_IMG_ATOMIC_WITH_RET_FMT_32  = 0x00000045,
TCP_PERF_SEL_IMG_ATOMIC_WITHOUT_RET_FMT_32  = 0x00000046,
TCP_PERF_SEL_IMG_ATOMIC_WITH_RET_FMT_64  = 0x00000047,
TCP_PERF_SEL_IMG_ATOMIC_WITHOUT_RET_FMT_64  = 0x00000048,
TCP_PERF_SEL_BUF_READ_FMT_8              = 0x00000049,
TCP_PERF_SEL_BUF_READ_FMT_16             = 0x0000004a,
TCP_PERF_SEL_BUF_READ_FMT_32             = 0x0000004b,
TCP_PERF_SEL_BUF_WRITE_FMT_8             = 0x0000004c,
TCP_PERF_SEL_BUF_WRITE_FMT_16            = 0x0000004d,
TCP_PERF_SEL_BUF_WRITE_FMT_32            = 0x0000004e,
TCP_PERF_SEL_BUF_ATOMIC_WITH_RET_FMT_32  = 0x0000004f,
TCP_PERF_SEL_BUF_ATOMIC_WITHOUT_RET_FMT_32  = 0x00000050,
TCP_PERF_SEL_BUF_ATOMIC_WITH_RET_FMT_64  = 0x00000051,
TCP_PERF_SEL_BUF_ATOMIC_WITHOUT_RET_FMT_64  = 0x00000052,
TCP_PERF_SEL_ARR_LINEAR_GENERAL          = 0x00000053,
TCP_PERF_SEL_ARR_LINEAR_ALIGNED          = 0x00000054,
TCP_PERF_SEL_ARR_1D_THIN1                = 0x00000055,
TCP_PERF_SEL_ARR_1D_THICK                = 0x00000056,
TCP_PERF_SEL_ARR_2D_THIN1                = 0x00000057,
TCP_PERF_SEL_ARR_2D_THICK                = 0x00000058,
TCP_PERF_SEL_ARR_2D_XTHICK               = 0x00000059,
TCP_PERF_SEL_ARR_3D_THIN1                = 0x0000005a,
TCP_PERF_SEL_ARR_3D_THICK                = 0x0000005b,
TCP_PERF_SEL_ARR_3D_XTHICK               = 0x0000005c,
TCP_PERF_SEL_DIM_1D                      = 0x0000005d,
TCP_PERF_SEL_DIM_2D                      = 0x0000005e,
TCP_PERF_SEL_DIM_3D                      = 0x0000005f,
TCP_PERF_SEL_DIM_1D_ARRAY                = 0x00000060,
TCP_PERF_SEL_DIM_2D_ARRAY                = 0x00000061,
TCP_PERF_SEL_DIM_2D_MSAA                 = 0x00000062,
TCP_PERF_SEL_DIM_2D_ARRAY_MSAA           = 0x00000063,
TCP_PERF_SEL_DIM_CUBE_ARRAY              = 0x00000064,
TCP_PERF_SEL_CP_TCP_INVALIDATE           = 0x00000065,
TCP_PERF_SEL_TA_TCP_STATE_READ           = 0x00000066,
TCP_PERF_SEL_TAGRAM0_REQ                 = 0x00000067,
TCP_PERF_SEL_TAGRAM1_REQ                 = 0x00000068,
TCP_PERF_SEL_TAGRAM2_REQ                 = 0x00000069,
TCP_PERF_SEL_TAGRAM3_REQ                 = 0x0000006a,
TCP_PERF_SEL_GATE_EN1                    = 0x0000006b,
TCP_PERF_SEL_GATE_EN2                    = 0x0000006c,
TCP_PERF_SEL_CORE_REG_SCLK_VLD           = 0x0000006d,
TCP_PERF_SEL_TCC_REQ                     = 0x0000006e,
TCP_PERF_SEL_TCC_NON_READ_REQ            = 0x0000006f,
TCP_PERF_SEL_TCC_BYPASS_READ_REQ         = 0x00000070,
TCP_PERF_SEL_TCC_MISS_EVICT_READ_REQ     = 0x00000071,
TCP_PERF_SEL_TCC_VOLATILE_READ_REQ       = 0x00000072,
TCP_PERF_SEL_TCC_VOLATILE_BYPASS_READ_REQ  = 0x00000073,
TCP_PERF_SEL_TCC_VOLATILE_MISS_EVICT_READ_REQ  = 0x00000074,
TCP_PERF_SEL_TCC_BYPASS_WRITE_REQ        = 0x00000075,
TCP_PERF_SEL_TCC_MISS_EVICT_WRITE_REQ    = 0x00000076,
TCP_PERF_SEL_TCC_VOLATILE_BYPASS_WRITE_REQ  = 0x00000077,
TCP_PERF_SEL_TCC_VOLATILE_WRITE_REQ      = 0x00000078,
TCP_PERF_SEL_TCC_VOLATILE_MISS_EVICT_WRITE_REQ  = 0x00000079,
TCP_PERF_SEL_TCC_BYPASS_ATOMIC_REQ       = 0x0000007a,
TCP_PERF_SEL_TCC_ATOMIC_REQ              = 0x0000007b,
TCP_PERF_SEL_TCC_VOLATILE_ATOMIC_REQ     = 0x0000007c,
TCP_PERF_SEL_TCC_DATA_BUS_BUSY           = 0x0000007d,
TCP_PERF_SEL_TOTAL_ACCESSES              = 0x0000007e,
TCP_PERF_SEL_TOTAL_READ                  = 0x0000007f,
TCP_PERF_SEL_TOTAL_HIT_LRU_READ          = 0x00000080,
TCP_PERF_SEL_TOTAL_HIT_EVICT_READ        = 0x00000081,
TCP_PERF_SEL_TOTAL_MISS_LRU_READ         = 0x00000082,
TCP_PERF_SEL_TOTAL_MISS_EVICT_READ       = 0x00000083,
TCP_PERF_SEL_TOTAL_NON_READ              = 0x00000084,
TCP_PERF_SEL_TOTAL_WRITE                 = 0x00000085,
TCP_PERF_SEL_TOTAL_MISS_LRU_WRITE        = 0x00000086,
TCP_PERF_SEL_TOTAL_MISS_EVICT_WRITE      = 0x00000087,
TCP_PERF_SEL_TOTAL_WBINVL1_VOL           = 0x00000088,
TCP_PERF_SEL_TOTAL_WRITEBACK_INVALIDATES  = 0x00000089,
TCP_PERF_SEL_DISPLAY_MICROTILING         = 0x0000008a,
TCP_PERF_SEL_THIN_MICROTILING            = 0x0000008b,
TCP_PERF_SEL_DEPTH_MICROTILING           = 0x0000008c,
TCP_PERF_SEL_ARR_PRT_THIN1               = 0x0000008d,
TCP_PERF_SEL_ARR_PRT_2D_THIN1            = 0x0000008e,
TCP_PERF_SEL_ARR_PRT_3D_THIN1            = 0x0000008f,
TCP_PERF_SEL_ARR_PRT_THICK               = 0x00000090,
TCP_PERF_SEL_ARR_PRT_2D_THICK            = 0x00000091,
TCP_PERF_SEL_ARR_PRT_3D_THICK            = 0x00000092,
TCP_PERF_SEL_CP_TCP_INVALIDATE_VOL       = 0x00000093,
TCP_PERF_SEL_SQ_TCP_INVALIDATE_VOL       = 0x00000094,
TCP_PERF_SEL_UNALIGNED                   = 0x00000095,
TCP_PERF_SEL_ROTATED_MICROTILING         = 0x00000096,
TCP_PERF_SEL_THICK_MICROTILING           = 0x00000097,
TCP_PERF_SEL_ATC                         = 0x00000098,
TCP_PERF_SEL_POWER_STALL                 = 0x00000099,
TCP_PERF_SEL_RESERVED_154                = 0x0000009a,
TCP_PERF_SEL_TCC_LRU_REQ                 = 0x0000009b,
TCP_PERF_SEL_TCC_STREAM_REQ              = 0x0000009c,
TCP_PERF_SEL_TCC_NC_READ_REQ             = 0x0000009d,
TCP_PERF_SEL_TCC_NC_WRITE_REQ            = 0x0000009e,
TCP_PERF_SEL_TCC_NC_ATOMIC_REQ           = 0x0000009f,
TCP_PERF_SEL_TCC_UC_READ_REQ             = 0x000000a0,
TCP_PERF_SEL_TCC_UC_WRITE_REQ            = 0x000000a1,
TCP_PERF_SEL_TCC_UC_ATOMIC_REQ           = 0x000000a2,
TCP_PERF_SEL_TCC_CC_READ_REQ             = 0x000000a3,
TCP_PERF_SEL_TCC_CC_WRITE_REQ            = 0x000000a4,
TCP_PERF_SEL_TCC_CC_ATOMIC_REQ           = 0x000000a5,
TCP_PERF_SEL_TCC_DCC_REQ                 = 0x000000a6,
TCP_PERF_SEL_TCC_PHYSICAL_REQ            = 0x000000a7,
TCP_PERF_SEL_UNORDERED_MTYPE_STALL       = 0x000000a8,
TCP_PERF_SEL_VOLATILE                    = 0x000000a9,
TCP_PERF_SEL_TC_TA_XNACK_STALL           = 0x000000aa,
TCP_PERF_SEL_UTCL1_SERIALIZATION_STALL   = 0x000000ab,
TCP_PERF_SEL_SHOOTDOWN                   = 0x000000ac,
TCP_PERF_SEL_UTCL1_TRANSLATION_MISS      = 0x000000ad,
TCP_PERF_SEL_UTCL1_PERMISSION_MISS       = 0x000000ae,
TCP_PERF_SEL_UTCL1_REQUEST               = 0x000000af,
TCP_PERF_SEL_UTCL1_STALL_INFLIGHT_MAX    = 0x000000b0,
TCP_PERF_SEL_UTCL1_STALL_LRU_INFLIGHT    = 0x000000b1,
TCP_PERF_SEL_UTCL1_LFIFO_FULL            = 0x000000b2,
TCP_PERF_SEL_UTCL1_STALL_LFIFO_NOT_RES   = 0x000000b3,
TCP_PERF_SEL_UTCL1_STALL_UTCL2_REQ_OUT_OF_CREDITS  = 0x000000b4,
TCP_PERF_SEL_UTCL1_UTCL2_INFLIGHT        = 0x000000b5,
TCP_PERF_SEL_UTCL1_STALL_MISSFIFO_FULL   = 0x000000b6,
TCP_PERF_SEL_IMG_READ_FMT_ETC2_RGB       = 0x000000b7,
TCP_PERF_SEL_IMG_READ_FMT_ETC2_RGBA      = 0x000000b8,
TCP_PERF_SEL_IMG_READ_FMT_ETC2_RGBA1     = 0x000000b9,
TCP_PERF_SEL_IMG_READ_FMT_ETC2_R         = 0x000000ba,
TCP_PERF_SEL_IMG_READ_FMT_ETC2_RG        = 0x000000bb,
TCP_PERF_SEL_IMG_READ_FMT_8_AS_32        = 0x000000bc,
TCP_PERF_SEL_IMG_READ_FMT_8_AS_64        = 0x000000bd,
TCP_PERF_SEL_IMG_READ_FMT_16_AS_64       = 0x000000be,
TCP_PERF_SEL_IMG_READ_FMT_16_AS_128      = 0x000000bf,
TCP_PERF_SEL_IMG_WRITE_FMT_8_AS_32       = 0x000000c0,
TCP_PERF_SEL_IMG_WRITE_FMT_8_AS_64       = 0x000000c1,
TCP_PERF_SEL_IMG_WRITE_FMT_16_AS_64      = 0x000000c2,
TCP_PERF_SEL_IMG_WRITE_FMT_16_AS_128     = 0x000000c3,
} TCP_PERFCOUNT_SELECT;

/*
 * TCP_CACHE_POLICIES enum
 */

typedef enum TCP_CACHE_POLICIES {
TCP_CACHE_POLICY_MISS_LRU                = 0x00000000,
TCP_CACHE_POLICY_MISS_EVICT              = 0x00000001,
TCP_CACHE_POLICY_HIT_LRU                 = 0x00000002,
TCP_CACHE_POLICY_HIT_EVICT               = 0x00000003,
} TCP_CACHE_POLICIES;

/*
 * TCP_CACHE_STORE_POLICIES enum
 */

typedef enum TCP_CACHE_STORE_POLICIES {
TCP_CACHE_STORE_POLICY_WT_LRU            = 0x00000000,
TCP_CACHE_STORE_POLICY_WT_EVICT          = 0x00000001,
} TCP_CACHE_STORE_POLICIES;

/*
 * TCP_WATCH_MODES enum
 */

typedef enum TCP_WATCH_MODES {
TCP_WATCH_MODE_READ                      = 0x00000000,
TCP_WATCH_MODE_NONREAD                   = 0x00000001,
TCP_WATCH_MODE_ATOMIC                    = 0x00000002,
TCP_WATCH_MODE_ALL                       = 0x00000003,
} TCP_WATCH_MODES;

/*
 * TCP_DSM_DATA_SEL enum
 */

typedef enum TCP_DSM_DATA_SEL {
TCP_DSM_DISABLE                          = 0x00000000,
TCP_DSM_SEL0                             = 0x00000001,
TCP_DSM_SEL1                             = 0x00000002,
TCP_DSM_SEL_BOTH                         = 0x00000003,
} TCP_DSM_DATA_SEL;

/*
 * TCP_DSM_SINGLE_WRITE enum
 */

typedef enum TCP_DSM_SINGLE_WRITE {
TCP_DSM_SINGLE_WRITE_DIS                 = 0x00000000,
TCP_DSM_SINGLE_WRITE_EN                  = 0x00000001,
} TCP_DSM_SINGLE_WRITE;

/*
 * TCP_DSM_INJECT_SEL enum
 */

typedef enum TCP_DSM_INJECT_SEL {
TCP_DSM_INJECT_SEL0                      = 0x00000000,
TCP_DSM_INJECT_SEL1                      = 0x00000001,
TCP_DSM_INJECT_SEL2                      = 0x00000002,
TCP_DSM_INJECT_SEL3                      = 0x00000003,
} TCP_DSM_INJECT_SEL;

/*******************************************************
 * TCC Enums
 *******************************************************/

/*
 * TCC_PERF_SEL enum
 */

typedef enum TCC_PERF_SEL {
TCC_PERF_SEL_NONE                        = 0x00000000,
TCC_PERF_SEL_CYCLE                       = 0x00000001,
TCC_PERF_SEL_BUSY                        = 0x00000002,
TCC_PERF_SEL_REQ                         = 0x00000003,
TCC_PERF_SEL_STREAMING_REQ               = 0x00000004,
TCC_PERF_SEL_EXE_REQ                     = 0x00000005,
TCC_PERF_SEL_COMPRESSED_REQ              = 0x00000006,
TCC_PERF_SEL_COMPRESSED_0_REQ            = 0x00000007,
TCC_PERF_SEL_METADATA_REQ                = 0x00000008,
TCC_PERF_SEL_NC_VIRTUAL_REQ              = 0x00000009,
TCC_PERF_SEL_UC_VIRTUAL_REQ              = 0x0000000a,
TCC_PERF_SEL_CC_PHYSICAL_REQ             = 0x0000000b,
TCC_PERF_SEL_PROBE                       = 0x0000000c,
TCC_PERF_SEL_PROBE_ALL                   = 0x0000000d,
TCC_PERF_SEL_READ                        = 0x0000000e,
TCC_PERF_SEL_WRITE                       = 0x0000000f,
TCC_PERF_SEL_ATOMIC                      = 0x00000010,
TCC_PERF_SEL_HIT                         = 0x00000011,
TCC_PERF_SEL_SECTOR_HIT                  = 0x00000012,
TCC_PERF_SEL_MISS                        = 0x00000013,
TCC_PERF_SEL_DEWRITE_ALLOCATE_HIT        = 0x00000014,
TCC_PERF_SEL_FULLY_WRITTEN_HIT           = 0x00000015,
TCC_PERF_SEL_WRITEBACK                   = 0x00000016,
TCC_PERF_SEL_LATENCY_FIFO_FULL           = 0x00000017,
TCC_PERF_SEL_SRC_FIFO_FULL               = 0x00000018,
TCC_PERF_SEL_HOLE_FIFO_FULL              = 0x00000019,
TCC_PERF_SEL_EA_WRREQ                    = 0x0000001a,
TCC_PERF_SEL_EA_WRREQ_64B                = 0x0000001b,
TCC_PERF_SEL_EA_WRREQ_PROBE_COMMAND      = 0x0000001c,
TCC_PERF_SEL_EA_WR_UNCACHED_32B          = 0x0000001d,
TCC_PERF_SEL_EA_WRREQ_STALL              = 0x0000001e,
TCC_PERF_SEL_EA_WRREQ_CREDIT_STALL       = 0x0000001f,
TCC_PERF_SEL_TOO_MANY_EA_WRREQS_STALL    = 0x00000020,
TCC_PERF_SEL_EA_WRREQ_LEVEL              = 0x00000021,
TCC_PERF_SEL_EA_ATOMIC                   = 0x00000022,
TCC_PERF_SEL_EA_ATOMIC_LEVEL             = 0x00000023,
TCC_PERF_SEL_EA_RDREQ                    = 0x00000024,
TCC_PERF_SEL_EA_RDREQ_32B                = 0x00000025,
TCC_PERF_SEL_EA_RD_UNCACHED_32B          = 0x00000026,
TCC_PERF_SEL_EA_RD_MDC_32B               = 0x00000027,
TCC_PERF_SEL_EA_RD_COMPRESSED_32B        = 0x00000028,
TCC_PERF_SEL_EA_RDREQ_CREDIT_STALL       = 0x00000029,
TCC_PERF_SEL_EA_RDREQ_LEVEL              = 0x0000002a,
TCC_PERF_SEL_TAG_STALL                   = 0x0000002b,
TCC_PERF_SEL_TAG_WRITEBACK_FIFO_FULL_STALL  = 0x0000002c,
TCC_PERF_SEL_TAG_MISS_NOTHING_REPLACEABLE_STALL  = 0x0000002d,
TCC_PERF_SEL_TAG_UNCACHED_WRITE_ATOMIC_FIFO_FULL_STALL  = 0x0000002e,
TCC_PERF_SEL_TAG_NO_UNCACHED_WRITE_ATOMIC_ENTRIES_STALL  = 0x0000002f,
TCC_PERF_SEL_TAG_PROBE_STALL             = 0x00000030,
TCC_PERF_SEL_TAG_PROBE_FILTER_STALL      = 0x00000031,
TCC_PERF_SEL_READ_RETURN_TIMEOUT         = 0x00000032,
TCC_PERF_SEL_WRITEBACK_READ_TIMEOUT      = 0x00000033,
TCC_PERF_SEL_READ_RETURN_FULL_BUBBLE     = 0x00000034,
TCC_PERF_SEL_BUBBLE                      = 0x00000035,
TCC_PERF_SEL_RETURN_ACK                  = 0x00000036,
TCC_PERF_SEL_RETURN_DATA                 = 0x00000037,
TCC_PERF_SEL_RETURN_HOLE                 = 0x00000038,
TCC_PERF_SEL_RETURN_ACK_HOLE             = 0x00000039,
TCC_PERF_SEL_IB_REQ                      = 0x0000003a,
TCC_PERF_SEL_IB_STALL                    = 0x0000003b,
TCC_PERF_SEL_IB_TAG_STALL                = 0x0000003c,
TCC_PERF_SEL_IB_MDC_STALL                = 0x0000003d,
TCC_PERF_SEL_TCA_LEVEL                   = 0x0000003e,
TCC_PERF_SEL_HOLE_LEVEL                  = 0x0000003f,
TCC_PERF_SEL_NORMAL_WRITEBACK            = 0x00000040,
TCC_PERF_SEL_TC_OP_WBL2_NC_WRITEBACK     = 0x00000041,
TCC_PERF_SEL_TC_OP_WBL2_WC_WRITEBACK     = 0x00000042,
TCC_PERF_SEL_TC_OP_WBINVL2_WRITEBACK     = 0x00000043,
TCC_PERF_SEL_TC_OP_WBINVL2_NC_WRITEBACK  = 0x00000044,
TCC_PERF_SEL_TC_OP_WBINVL2_SD_WRITEBACK  = 0x00000045,
TCC_PERF_SEL_ALL_TC_OP_WB_WRITEBACK      = 0x00000046,
TCC_PERF_SEL_NORMAL_EVICT                = 0x00000047,
TCC_PERF_SEL_TC_OP_WBL2_NC_EVICT         = 0x00000048,
TCC_PERF_SEL_TC_OP_WBL2_WC_EVICT         = 0x00000049,
TCC_PERF_SEL_TC_OP_INVL2_NC_EVICT        = 0x0000004a,
TCC_PERF_SEL_TC_OP_WBINVL2_EVICT         = 0x0000004b,
TCC_PERF_SEL_TC_OP_WBINVL2_NC_EVICT      = 0x0000004c,
TCC_PERF_SEL_TC_OP_WBINVL2_SD_EVICT      = 0x0000004d,
TCC_PERF_SEL_ALL_TC_OP_INV_EVICT         = 0x0000004e,
TCC_PERF_SEL_PROBE_EVICT                 = 0x0000004f,
TCC_PERF_SEL_TC_OP_WBL2_NC_CYCLE         = 0x00000050,
TCC_PERF_SEL_TC_OP_WBL2_WC_CYCLE         = 0x00000051,
TCC_PERF_SEL_TC_OP_INVL2_NC_CYCLE        = 0x00000052,
TCC_PERF_SEL_TC_OP_WBINVL2_CYCLE         = 0x00000053,
TCC_PERF_SEL_TC_OP_WBINVL2_NC_CYCLE      = 0x00000054,
TCC_PERF_SEL_TC_OP_WBINVL2_SD_CYCLE      = 0x00000055,
TCC_PERF_SEL_ALL_TC_OP_WB_OR_INV_CYCLE   = 0x00000056,
TCC_PERF_SEL_TC_OP_WBL2_NC_START         = 0x00000057,
TCC_PERF_SEL_TC_OP_WBL2_WC_START         = 0x00000058,
TCC_PERF_SEL_TC_OP_INVL2_NC_START        = 0x00000059,
TCC_PERF_SEL_TC_OP_WBINVL2_START         = 0x0000005a,
TCC_PERF_SEL_TC_OP_WBINVL2_NC_START      = 0x0000005b,
TCC_PERF_SEL_TC_OP_WBINVL2_SD_START      = 0x0000005c,
TCC_PERF_SEL_ALL_TC_OP_WB_OR_INV_START   = 0x0000005d,
TCC_PERF_SEL_TC_OP_WBL2_NC_FINISH        = 0x0000005e,
TCC_PERF_SEL_TC_OP_WBL2_WC_FINISH        = 0x0000005f,
TCC_PERF_SEL_TC_OP_INVL2_NC_FINISH       = 0x00000060,
TCC_PERF_SEL_TC_OP_WBINVL2_FINISH        = 0x00000061,
TCC_PERF_SEL_TC_OP_WBINVL2_NC_FINISH     = 0x00000062,
TCC_PERF_SEL_TC_OP_WBINVL2_SD_FINISH     = 0x00000063,
TCC_PERF_SEL_ALL_TC_OP_WB_OR_INV_FINISH  = 0x00000064,
TCC_PERF_SEL_MDC_REQ                     = 0x00000065,
TCC_PERF_SEL_MDC_LEVEL                   = 0x00000066,
TCC_PERF_SEL_MDC_TAG_HIT                 = 0x00000067,
TCC_PERF_SEL_MDC_SECTOR_HIT              = 0x00000068,
TCC_PERF_SEL_MDC_SECTOR_MISS             = 0x00000069,
TCC_PERF_SEL_MDC_TAG_STALL               = 0x0000006a,
TCC_PERF_SEL_MDC_TAG_REPLACEMENT_LINE_IN_USE_STALL  = 0x0000006b,
TCC_PERF_SEL_MDC_TAG_DESECTORIZATION_FIFO_FULL_STALL  = 0x0000006c,
TCC_PERF_SEL_MDC_TAG_WAITING_FOR_INVALIDATE_COMPLETION_STALL  = 0x0000006d,
TCC_PERF_SEL_PROBE_FILTER_DISABLE_TRANSITION  = 0x0000006e,
TCC_PERF_SEL_PROBE_FILTER_DISABLED       = 0x0000006f,
TCC_PERF_SEL_CLIENT0_REQ                 = 0x00000080,
TCC_PERF_SEL_CLIENT1_REQ                 = 0x00000081,
TCC_PERF_SEL_CLIENT2_REQ                 = 0x00000082,
TCC_PERF_SEL_CLIENT3_REQ                 = 0x00000083,
TCC_PERF_SEL_CLIENT4_REQ                 = 0x00000084,
TCC_PERF_SEL_CLIENT5_REQ                 = 0x00000085,
TCC_PERF_SEL_CLIENT6_REQ                 = 0x00000086,
TCC_PERF_SEL_CLIENT7_REQ                 = 0x00000087,
TCC_PERF_SEL_CLIENT8_REQ                 = 0x00000088,
TCC_PERF_SEL_CLIENT9_REQ                 = 0x00000089,
TCC_PERF_SEL_CLIENT10_REQ                = 0x0000008a,
TCC_PERF_SEL_CLIENT11_REQ                = 0x0000008b,
TCC_PERF_SEL_CLIENT12_REQ                = 0x0000008c,
TCC_PERF_SEL_CLIENT13_REQ                = 0x0000008d,
TCC_PERF_SEL_CLIENT14_REQ                = 0x0000008e,
TCC_PERF_SEL_CLIENT15_REQ                = 0x0000008f,
TCC_PERF_SEL_CLIENT16_REQ                = 0x00000090,
TCC_PERF_SEL_CLIENT17_REQ                = 0x00000091,
TCC_PERF_SEL_CLIENT18_REQ                = 0x00000092,
TCC_PERF_SEL_CLIENT19_REQ                = 0x00000093,
TCC_PERF_SEL_CLIENT20_REQ                = 0x00000094,
TCC_PERF_SEL_CLIENT21_REQ                = 0x00000095,
TCC_PERF_SEL_CLIENT22_REQ                = 0x00000096,
TCC_PERF_SEL_CLIENT23_REQ                = 0x00000097,
TCC_PERF_SEL_CLIENT24_REQ                = 0x00000098,
TCC_PERF_SEL_CLIENT25_REQ                = 0x00000099,
TCC_PERF_SEL_CLIENT26_REQ                = 0x0000009a,
TCC_PERF_SEL_CLIENT27_REQ                = 0x0000009b,
TCC_PERF_SEL_CLIENT28_REQ                = 0x0000009c,
TCC_PERF_SEL_CLIENT29_REQ                = 0x0000009d,
TCC_PERF_SEL_CLIENT30_REQ                = 0x0000009e,
TCC_PERF_SEL_CLIENT31_REQ                = 0x0000009f,
TCC_PERF_SEL_CLIENT32_REQ                = 0x000000a0,
TCC_PERF_SEL_CLIENT33_REQ                = 0x000000a1,
TCC_PERF_SEL_CLIENT34_REQ                = 0x000000a2,
TCC_PERF_SEL_CLIENT35_REQ                = 0x000000a3,
TCC_PERF_SEL_CLIENT36_REQ                = 0x000000a4,
TCC_PERF_SEL_CLIENT37_REQ                = 0x000000a5,
TCC_PERF_SEL_CLIENT38_REQ                = 0x000000a6,
TCC_PERF_SEL_CLIENT39_REQ                = 0x000000a7,
TCC_PERF_SEL_CLIENT40_REQ                = 0x000000a8,
TCC_PERF_SEL_CLIENT41_REQ                = 0x000000a9,
TCC_PERF_SEL_CLIENT42_REQ                = 0x000000aa,
TCC_PERF_SEL_CLIENT43_REQ                = 0x000000ab,
TCC_PERF_SEL_CLIENT44_REQ                = 0x000000ac,
TCC_PERF_SEL_CLIENT45_REQ                = 0x000000ad,
TCC_PERF_SEL_CLIENT46_REQ                = 0x000000ae,
TCC_PERF_SEL_CLIENT47_REQ                = 0x000000af,
TCC_PERF_SEL_CLIENT48_REQ                = 0x000000b0,
TCC_PERF_SEL_CLIENT49_REQ                = 0x000000b1,
TCC_PERF_SEL_CLIENT50_REQ                = 0x000000b2,
TCC_PERF_SEL_CLIENT51_REQ                = 0x000000b3,
TCC_PERF_SEL_CLIENT52_REQ                = 0x000000b4,
TCC_PERF_SEL_CLIENT53_REQ                = 0x000000b5,
TCC_PERF_SEL_CLIENT54_REQ                = 0x000000b6,
TCC_PERF_SEL_CLIENT55_REQ                = 0x000000b7,
TCC_PERF_SEL_CLIENT56_REQ                = 0x000000b8,
TCC_PERF_SEL_CLIENT57_REQ                = 0x000000b9,
TCC_PERF_SEL_CLIENT58_REQ                = 0x000000ba,
TCC_PERF_SEL_CLIENT59_REQ                = 0x000000bb,
TCC_PERF_SEL_CLIENT60_REQ                = 0x000000bc,
TCC_PERF_SEL_CLIENT61_REQ                = 0x000000bd,
TCC_PERF_SEL_CLIENT62_REQ                = 0x000000be,
TCC_PERF_SEL_CLIENT63_REQ                = 0x000000bf,
TCC_PERF_SEL_CLIENT64_REQ                = 0x000000c0,
TCC_PERF_SEL_CLIENT65_REQ                = 0x000000c1,
TCC_PERF_SEL_CLIENT66_REQ                = 0x000000c2,
TCC_PERF_SEL_CLIENT67_REQ                = 0x000000c3,
TCC_PERF_SEL_CLIENT68_REQ                = 0x000000c4,
TCC_PERF_SEL_CLIENT69_REQ                = 0x000000c5,
TCC_PERF_SEL_CLIENT70_REQ                = 0x000000c6,
TCC_PERF_SEL_CLIENT71_REQ                = 0x000000c7,
TCC_PERF_SEL_CLIENT72_REQ                = 0x000000c8,
TCC_PERF_SEL_CLIENT73_REQ                = 0x000000c9,
TCC_PERF_SEL_CLIENT74_REQ                = 0x000000ca,
TCC_PERF_SEL_CLIENT75_REQ                = 0x000000cb,
TCC_PERF_SEL_CLIENT76_REQ                = 0x000000cc,
TCC_PERF_SEL_CLIENT77_REQ                = 0x000000cd,
TCC_PERF_SEL_CLIENT78_REQ                = 0x000000ce,
TCC_PERF_SEL_CLIENT79_REQ                = 0x000000cf,
TCC_PERF_SEL_CLIENT80_REQ                = 0x000000d0,
TCC_PERF_SEL_CLIENT81_REQ                = 0x000000d1,
TCC_PERF_SEL_CLIENT82_REQ                = 0x000000d2,
TCC_PERF_SEL_CLIENT83_REQ                = 0x000000d3,
TCC_PERF_SEL_CLIENT84_REQ                = 0x000000d4,
TCC_PERF_SEL_CLIENT85_REQ                = 0x000000d5,
TCC_PERF_SEL_CLIENT86_REQ                = 0x000000d6,
TCC_PERF_SEL_CLIENT87_REQ                = 0x000000d7,
TCC_PERF_SEL_CLIENT88_REQ                = 0x000000d8,
TCC_PERF_SEL_CLIENT89_REQ                = 0x000000d9,
TCC_PERF_SEL_CLIENT90_REQ                = 0x000000da,
TCC_PERF_SEL_CLIENT91_REQ                = 0x000000db,
TCC_PERF_SEL_CLIENT92_REQ                = 0x000000dc,
TCC_PERF_SEL_CLIENT93_REQ                = 0x000000dd,
TCC_PERF_SEL_CLIENT94_REQ                = 0x000000de,
TCC_PERF_SEL_CLIENT95_REQ                = 0x000000df,
TCC_PERF_SEL_CLIENT96_REQ                = 0x000000e0,
TCC_PERF_SEL_CLIENT97_REQ                = 0x000000e1,
TCC_PERF_SEL_CLIENT98_REQ                = 0x000000e2,
TCC_PERF_SEL_CLIENT99_REQ                = 0x000000e3,
TCC_PERF_SEL_CLIENT100_REQ               = 0x000000e4,
TCC_PERF_SEL_CLIENT101_REQ               = 0x000000e5,
TCC_PERF_SEL_CLIENT102_REQ               = 0x000000e6,
TCC_PERF_SEL_CLIENT103_REQ               = 0x000000e7,
TCC_PERF_SEL_CLIENT104_REQ               = 0x000000e8,
TCC_PERF_SEL_CLIENT105_REQ               = 0x000000e9,
TCC_PERF_SEL_CLIENT106_REQ               = 0x000000ea,
TCC_PERF_SEL_CLIENT107_REQ               = 0x000000eb,
TCC_PERF_SEL_CLIENT108_REQ               = 0x000000ec,
TCC_PERF_SEL_CLIENT109_REQ               = 0x000000ed,
TCC_PERF_SEL_CLIENT110_REQ               = 0x000000ee,
TCC_PERF_SEL_CLIENT111_REQ               = 0x000000ef,
TCC_PERF_SEL_CLIENT112_REQ               = 0x000000f0,
TCC_PERF_SEL_CLIENT113_REQ               = 0x000000f1,
TCC_PERF_SEL_CLIENT114_REQ               = 0x000000f2,
TCC_PERF_SEL_CLIENT115_REQ               = 0x000000f3,
TCC_PERF_SEL_CLIENT116_REQ               = 0x000000f4,
TCC_PERF_SEL_CLIENT117_REQ               = 0x000000f5,
TCC_PERF_SEL_CLIENT118_REQ               = 0x000000f6,
TCC_PERF_SEL_CLIENT119_REQ               = 0x000000f7,
TCC_PERF_SEL_CLIENT120_REQ               = 0x000000f8,
TCC_PERF_SEL_CLIENT121_REQ               = 0x000000f9,
TCC_PERF_SEL_CLIENT122_REQ               = 0x000000fa,
TCC_PERF_SEL_CLIENT123_REQ               = 0x000000fb,
TCC_PERF_SEL_CLIENT124_REQ               = 0x000000fc,
TCC_PERF_SEL_CLIENT125_REQ               = 0x000000fd,
TCC_PERF_SEL_CLIENT126_REQ               = 0x000000fe,
TCC_PERF_SEL_CLIENT127_REQ               = 0x000000ff,
} TCC_PERF_SEL;

/*
 * TCA_PERF_SEL enum
 */

typedef enum TCA_PERF_SEL {
TCA_PERF_SEL_NONE                        = 0x00000000,
TCA_PERF_SEL_CYCLE                       = 0x00000001,
TCA_PERF_SEL_BUSY                        = 0x00000002,
TCA_PERF_SEL_FORCED_HOLE_TCC0            = 0x00000003,
TCA_PERF_SEL_FORCED_HOLE_TCC1            = 0x00000004,
TCA_PERF_SEL_FORCED_HOLE_TCC2            = 0x00000005,
TCA_PERF_SEL_FORCED_HOLE_TCC3            = 0x00000006,
TCA_PERF_SEL_FORCED_HOLE_TCC4            = 0x00000007,
TCA_PERF_SEL_FORCED_HOLE_TCC5            = 0x00000008,
TCA_PERF_SEL_FORCED_HOLE_TCC6            = 0x00000009,
TCA_PERF_SEL_FORCED_HOLE_TCC7            = 0x0000000a,
TCA_PERF_SEL_REQ_TCC0                    = 0x0000000b,
TCA_PERF_SEL_REQ_TCC1                    = 0x0000000c,
TCA_PERF_SEL_REQ_TCC2                    = 0x0000000d,
TCA_PERF_SEL_REQ_TCC3                    = 0x0000000e,
TCA_PERF_SEL_REQ_TCC4                    = 0x0000000f,
TCA_PERF_SEL_REQ_TCC5                    = 0x00000010,
TCA_PERF_SEL_REQ_TCC6                    = 0x00000011,
TCA_PERF_SEL_REQ_TCC7                    = 0x00000012,
TCA_PERF_SEL_CROSSBAR_DOUBLE_ARB_TCC0    = 0x00000013,
TCA_PERF_SEL_CROSSBAR_DOUBLE_ARB_TCC1    = 0x00000014,
TCA_PERF_SEL_CROSSBAR_DOUBLE_ARB_TCC2    = 0x00000015,
TCA_PERF_SEL_CROSSBAR_DOUBLE_ARB_TCC3    = 0x00000016,
TCA_PERF_SEL_CROSSBAR_DOUBLE_ARB_TCC4    = 0x00000017,
TCA_PERF_SEL_CROSSBAR_DOUBLE_ARB_TCC5    = 0x00000018,
TCA_PERF_SEL_CROSSBAR_DOUBLE_ARB_TCC6    = 0x00000019,
TCA_PERF_SEL_CROSSBAR_DOUBLE_ARB_TCC7    = 0x0000001a,
TCA_PERF_SEL_CROSSBAR_STALL_TCC0         = 0x0000001b,
TCA_PERF_SEL_CROSSBAR_STALL_TCC1         = 0x0000001c,
TCA_PERF_SEL_CROSSBAR_STALL_TCC2         = 0x0000001d,
TCA_PERF_SEL_CROSSBAR_STALL_TCC3         = 0x0000001e,
TCA_PERF_SEL_CROSSBAR_STALL_TCC4         = 0x0000001f,
TCA_PERF_SEL_CROSSBAR_STALL_TCC5         = 0x00000020,
TCA_PERF_SEL_CROSSBAR_STALL_TCC6         = 0x00000021,
TCA_PERF_SEL_CROSSBAR_STALL_TCC7         = 0x00000022,
} TCA_PERF_SEL;

/*******************************************************
 * GRBM Enums
 *******************************************************/

/*
 * GRBM_PERF_SEL enum
 */

typedef enum GRBM_PERF_SEL {
GRBM_PERF_SEL_COUNT                      = 0x00000000,
GRBM_PERF_SEL_USER_DEFINED               = 0x00000001,
GRBM_PERF_SEL_GUI_ACTIVE                 = 0x00000002,
GRBM_PERF_SEL_CP_BUSY                    = 0x00000003,
GRBM_PERF_SEL_CP_COHER_BUSY              = 0x00000004,
GRBM_PERF_SEL_CP_DMA_BUSY                = 0x00000005,
GRBM_PERF_SEL_CB_BUSY                    = 0x00000006,
GRBM_PERF_SEL_DB_BUSY                    = 0x00000007,
GRBM_PERF_SEL_PA_BUSY                    = 0x00000008,
GRBM_PERF_SEL_SC_BUSY                    = 0x00000009,
GRBM_PERF_SEL_RESERVED_6                 = 0x0000000a,
GRBM_PERF_SEL_SPI_BUSY                   = 0x0000000b,
GRBM_PERF_SEL_SX_BUSY                    = 0x0000000c,
GRBM_PERF_SEL_TA_BUSY                    = 0x0000000d,
GRBM_PERF_SEL_CB_CLEAN                   = 0x0000000e,
GRBM_PERF_SEL_DB_CLEAN                   = 0x0000000f,
GRBM_PERF_SEL_RESERVED_5                 = 0x00000010,
GRBM_PERF_SEL_VGT_BUSY                   = 0x00000011,
GRBM_PERF_SEL_RESERVED_4                 = 0x00000012,
GRBM_PERF_SEL_RESERVED_3                 = 0x00000013,
GRBM_PERF_SEL_RESERVED_2                 = 0x00000014,
GRBM_PERF_SEL_RESERVED_1                 = 0x00000015,
GRBM_PERF_SEL_RESERVED_0                 = 0x00000016,
GRBM_PERF_SEL_IA_BUSY                    = 0x00000017,
GRBM_PERF_SEL_IA_NO_DMA_BUSY             = 0x00000018,
GRBM_PERF_SEL_GDS_BUSY                   = 0x00000019,
GRBM_PERF_SEL_BCI_BUSY                   = 0x0000001a,
GRBM_PERF_SEL_RLC_BUSY                   = 0x0000001b,
GRBM_PERF_SEL_TC_BUSY                    = 0x0000001c,
GRBM_PERF_SEL_CPG_BUSY                   = 0x0000001d,
GRBM_PERF_SEL_CPC_BUSY                   = 0x0000001e,
GRBM_PERF_SEL_CPF_BUSY                   = 0x0000001f,
GRBM_PERF_SEL_WD_BUSY                    = 0x00000020,
GRBM_PERF_SEL_WD_NO_DMA_BUSY             = 0x00000021,
GRBM_PERF_SEL_UTCL2_BUSY                 = 0x00000022,
GRBM_PERF_SEL_EA_BUSY                    = 0x00000023,
GRBM_PERF_SEL_RMI_BUSY                   = 0x00000024,
GRBM_PERF_SEL_CPAXI_BUSY                 = 0x00000025,
} GRBM_PERF_SEL;

/*
 * GRBM_SE0_PERF_SEL enum
 */

typedef enum GRBM_SE0_PERF_SEL {
GRBM_SE0_PERF_SEL_COUNT                  = 0x00000000,
GRBM_SE0_PERF_SEL_USER_DEFINED           = 0x00000001,
GRBM_SE0_PERF_SEL_CB_BUSY                = 0x00000002,
GRBM_SE0_PERF_SEL_DB_BUSY                = 0x00000003,
GRBM_SE0_PERF_SEL_SC_BUSY                = 0x00000004,
GRBM_SE0_PERF_SEL_RESERVED_1             = 0x00000005,
GRBM_SE0_PERF_SEL_SPI_BUSY               = 0x00000006,
GRBM_SE0_PERF_SEL_SX_BUSY                = 0x00000007,
GRBM_SE0_PERF_SEL_TA_BUSY                = 0x00000008,
GRBM_SE0_PERF_SEL_CB_CLEAN               = 0x00000009,
GRBM_SE0_PERF_SEL_DB_CLEAN               = 0x0000000a,
GRBM_SE0_PERF_SEL_RESERVED_0             = 0x0000000b,
GRBM_SE0_PERF_SEL_PA_BUSY                = 0x0000000c,
GRBM_SE0_PERF_SEL_VGT_BUSY               = 0x0000000d,
GRBM_SE0_PERF_SEL_BCI_BUSY               = 0x0000000e,
GRBM_SE0_PERF_SEL_RMI_BUSY               = 0x0000000f,
} GRBM_SE0_PERF_SEL;

/*
 * GRBM_SE1_PERF_SEL enum
 */

typedef enum GRBM_SE1_PERF_SEL {
GRBM_SE1_PERF_SEL_COUNT                  = 0x00000000,
GRBM_SE1_PERF_SEL_USER_DEFINED           = 0x00000001,
GRBM_SE1_PERF_SEL_CB_BUSY                = 0x00000002,
GRBM_SE1_PERF_SEL_DB_BUSY                = 0x00000003,
GRBM_SE1_PERF_SEL_SC_BUSY                = 0x00000004,
GRBM_SE1_PERF_SEL_RESERVED_1             = 0x00000005,
GRBM_SE1_PERF_SEL_SPI_BUSY               = 0x00000006,
GRBM_SE1_PERF_SEL_SX_BUSY                = 0x00000007,
GRBM_SE1_PERF_SEL_TA_BUSY                = 0x00000008,
GRBM_SE1_PERF_SEL_CB_CLEAN               = 0x00000009,
GRBM_SE1_PERF_SEL_DB_CLEAN               = 0x0000000a,
GRBM_SE1_PERF_SEL_RESERVED_0             = 0x0000000b,
GRBM_SE1_PERF_SEL_PA_BUSY                = 0x0000000c,
GRBM_SE1_PERF_SEL_VGT_BUSY               = 0x0000000d,
GRBM_SE1_PERF_SEL_BCI_BUSY               = 0x0000000e,
GRBM_SE1_PERF_SEL_RMI_BUSY               = 0x0000000f,
} GRBM_SE1_PERF_SEL;

/*
 * GRBM_SE2_PERF_SEL enum
 */

typedef enum GRBM_SE2_PERF_SEL {
GRBM_SE2_PERF_SEL_COUNT                  = 0x00000000,
GRBM_SE2_PERF_SEL_USER_DEFINED           = 0x00000001,
GRBM_SE2_PERF_SEL_CB_BUSY                = 0x00000002,
GRBM_SE2_PERF_SEL_DB_BUSY                = 0x00000003,
GRBM_SE2_PERF_SEL_SC_BUSY                = 0x00000004,
GRBM_SE2_PERF_SEL_RESERVED_1             = 0x00000005,
GRBM_SE2_PERF_SEL_SPI_BUSY               = 0x00000006,
GRBM_SE2_PERF_SEL_SX_BUSY                = 0x00000007,
GRBM_SE2_PERF_SEL_TA_BUSY                = 0x00000008,
GRBM_SE2_PERF_SEL_CB_CLEAN               = 0x00000009,
GRBM_SE2_PERF_SEL_DB_CLEAN               = 0x0000000a,
GRBM_SE2_PERF_SEL_RESERVED_0             = 0x0000000b,
GRBM_SE2_PERF_SEL_PA_BUSY                = 0x0000000c,
GRBM_SE2_PERF_SEL_VGT_BUSY               = 0x0000000d,
GRBM_SE2_PERF_SEL_BCI_BUSY               = 0x0000000e,
GRBM_SE2_PERF_SEL_RMI_BUSY               = 0x0000000f,
} GRBM_SE2_PERF_SEL;

/*
 * GRBM_SE3_PERF_SEL enum
 */

typedef enum GRBM_SE3_PERF_SEL {
GRBM_SE3_PERF_SEL_COUNT                  = 0x00000000,
GRBM_SE3_PERF_SEL_USER_DEFINED           = 0x00000001,
GRBM_SE3_PERF_SEL_CB_BUSY                = 0x00000002,
GRBM_SE3_PERF_SEL_DB_BUSY                = 0x00000003,
GRBM_SE3_PERF_SEL_SC_BUSY                = 0x00000004,
GRBM_SE3_PERF_SEL_RESERVED_1             = 0x00000005,
GRBM_SE3_PERF_SEL_SPI_BUSY               = 0x00000006,
GRBM_SE3_PERF_SEL_SX_BUSY                = 0x00000007,
GRBM_SE3_PERF_SEL_TA_BUSY                = 0x00000008,
GRBM_SE3_PERF_SEL_CB_CLEAN               = 0x00000009,
GRBM_SE3_PERF_SEL_DB_CLEAN               = 0x0000000a,
GRBM_SE3_PERF_SEL_RESERVED_0             = 0x0000000b,
GRBM_SE3_PERF_SEL_PA_BUSY                = 0x0000000c,
GRBM_SE3_PERF_SEL_VGT_BUSY               = 0x0000000d,
GRBM_SE3_PERF_SEL_BCI_BUSY               = 0x0000000e,
GRBM_SE3_PERF_SEL_RMI_BUSY               = 0x0000000f,
} GRBM_SE3_PERF_SEL;

/*******************************************************
 * CP Enums
 *******************************************************/

/*
 * CP_RING_ID enum
 */

typedef enum CP_RING_ID {
RINGID0                                  = 0x00000000,
RINGID1                                  = 0x00000001,
RINGID2                                  = 0x00000002,
RINGID3                                  = 0x00000003,
} CP_RING_ID;

/*
 * CP_PIPE_ID enum
 */

typedef enum CP_PIPE_ID {
PIPE_ID0                                 = 0x00000000,
PIPE_ID1                                 = 0x00000001,
PIPE_ID2                                 = 0x00000002,
PIPE_ID3                                 = 0x00000003,
} CP_PIPE_ID;

/*
 * CP_ME_ID enum
 */

typedef enum CP_ME_ID {
ME_ID0                                   = 0x00000000,
ME_ID1                                   = 0x00000001,
ME_ID2                                   = 0x00000002,
ME_ID3                                   = 0x00000003,
} CP_ME_ID;

/*
 * SPM_PERFMON_STATE enum
 */

typedef enum SPM_PERFMON_STATE {
STRM_PERFMON_STATE_DISABLE_AND_RESET     = 0x00000000,
STRM_PERFMON_STATE_START_COUNTING        = 0x00000001,
STRM_PERFMON_STATE_STOP_COUNTING         = 0x00000002,
STRM_PERFMON_STATE_RESERVED_3            = 0x00000003,
STRM_PERFMON_STATE_DISABLE_AND_RESET_PHANTOM  = 0x00000004,
STRM_PERFMON_STATE_COUNT_AND_DUMP_PHANTOM  = 0x00000005,
} SPM_PERFMON_STATE;

/*
 * CP_PERFMON_STATE enum
 */

typedef enum CP_PERFMON_STATE {
CP_PERFMON_STATE_DISABLE_AND_RESET       = 0x00000000,
CP_PERFMON_STATE_START_COUNTING          = 0x00000001,
CP_PERFMON_STATE_STOP_COUNTING           = 0x00000002,
CP_PERFMON_STATE_RESERVED_3              = 0x00000003,
CP_PERFMON_STATE_DISABLE_AND_RESET_PHANTOM  = 0x00000004,
CP_PERFMON_STATE_COUNT_AND_DUMP_PHANTOM  = 0x00000005,
} CP_PERFMON_STATE;

/*
 * CP_PERFMON_ENABLE_MODE enum
 */

typedef enum CP_PERFMON_ENABLE_MODE {
CP_PERFMON_ENABLE_MODE_ALWAYS_COUNT      = 0x00000000,
CP_PERFMON_ENABLE_MODE_RESERVED_1        = 0x00000001,
CP_PERFMON_ENABLE_MODE_COUNT_CONTEXT_TRUE  = 0x00000002,
CP_PERFMON_ENABLE_MODE_COUNT_CONTEXT_FALSE  = 0x00000003,
} CP_PERFMON_ENABLE_MODE;

/*
 * CPG_PERFCOUNT_SEL enum
 */

typedef enum CPG_PERFCOUNT_SEL {
CPG_PERF_SEL_ALWAYS_COUNT                = 0x00000000,
CPG_PERF_SEL_RBIU_FIFO_FULL              = 0x00000001,
CPG_PERF_SEL_CSF_RTS_BUT_MIU_NOT_RTR     = 0x00000002,
CPG_PERF_SEL_CSF_ST_BASE_SIZE_FIFO_FULL  = 0x00000003,
CPG_PERF_SEL_CP_GRBM_DWORDS_SENT         = 0x00000004,
CPG_PERF_SEL_ME_PARSER_BUSY              = 0x00000005,
CPG_PERF_SEL_COUNT_TYPE0_PACKETS         = 0x00000006,
CPG_PERF_SEL_COUNT_TYPE3_PACKETS         = 0x00000007,
CPG_PERF_SEL_CSF_FETCHING_CMD_BUFFERS    = 0x00000008,
CPG_PERF_SEL_CP_GRBM_OUT_OF_CREDITS      = 0x00000009,
CPG_PERF_SEL_CP_PFP_GRBM_OUT_OF_CREDITS  = 0x0000000a,
CPG_PERF_SEL_CP_GDS_GRBM_OUT_OF_CREDITS  = 0x0000000b,
CPG_PERF_SEL_RCIU_STALLED_ON_ME_READ     = 0x0000000c,
CPG_PERF_SEL_RCIU_STALLED_ON_DMA_READ    = 0x0000000d,
CPG_PERF_SEL_SSU_STALLED_ON_ACTIVE_CNTX  = 0x0000000e,
CPG_PERF_SEL_SSU_STALLED_ON_CLEAN_SIGNALS  = 0x0000000f,
CPG_PERF_SEL_QU_STALLED_ON_EOP_DONE_PULSE  = 0x00000010,
CPG_PERF_SEL_QU_STALLED_ON_EOP_DONE_WR_CONFIRM  = 0x00000011,
CPG_PERF_SEL_PFP_STALLED_ON_CSF_READY    = 0x00000012,
CPG_PERF_SEL_PFP_STALLED_ON_MEQ_READY    = 0x00000013,
CPG_PERF_SEL_PFP_STALLED_ON_RCIU_READY   = 0x00000014,
CPG_PERF_SEL_PFP_STALLED_FOR_DATA_FROM_ROQ  = 0x00000015,
CPG_PERF_SEL_ME_STALLED_FOR_DATA_FROM_PFP  = 0x00000016,
CPG_PERF_SEL_ME_STALLED_FOR_DATA_FROM_STQ  = 0x00000017,
CPG_PERF_SEL_ME_STALLED_ON_NO_AVAIL_GFX_CNTX  = 0x00000018,
CPG_PERF_SEL_ME_STALLED_WRITING_TO_RCIU  = 0x00000019,
CPG_PERF_SEL_ME_STALLED_WRITING_CONSTANTS  = 0x0000001a,
CPG_PERF_SEL_ME_STALLED_ON_PARTIAL_FLUSH  = 0x0000001b,
CPG_PERF_SEL_ME_WAIT_ON_CE_COUNTER       = 0x0000001c,
CPG_PERF_SEL_ME_WAIT_ON_AVAIL_BUFFER     = 0x0000001d,
CPG_PERF_SEL_SEMAPHORE_BUSY_POLLING_FOR_PASS  = 0x0000001e,
CPG_PERF_SEL_LOAD_STALLED_ON_SET_COHERENCY  = 0x0000001f,
CPG_PERF_SEL_DYNAMIC_CLK_VALID           = 0x00000020,
CPG_PERF_SEL_REGISTER_CLK_VALID          = 0x00000021,
CPG_PERF_SEL_MIU_WRITE_REQUEST_SENT      = 0x00000022,
CPG_PERF_SEL_MIU_READ_REQUEST_SENT       = 0x00000023,
CPG_PERF_SEL_CE_STALL_RAM_DUMP           = 0x00000024,
CPG_PERF_SEL_CE_STALL_RAM_WRITE          = 0x00000025,
CPG_PERF_SEL_CE_STALL_ON_INC_FIFO        = 0x00000026,
CPG_PERF_SEL_CE_STALL_ON_WR_RAM_FIFO     = 0x00000027,
CPG_PERF_SEL_CE_STALL_ON_DATA_FROM_MIU   = 0x00000028,
CPG_PERF_SEL_CE_STALL_ON_DATA_FROM_ROQ   = 0x00000029,
CPG_PERF_SEL_CE_STALL_ON_CE_BUFFER_FLAG  = 0x0000002a,
CPG_PERF_SEL_CE_STALL_ON_DE_COUNTER      = 0x0000002b,
CPG_PERF_SEL_TCIU_STALL_WAIT_ON_FREE     = 0x0000002c,
CPG_PERF_SEL_TCIU_STALL_WAIT_ON_TAGS     = 0x0000002d,
CPG_PERF_SEL_UTCL2IU_STALL_WAIT_ON_FREE  = 0x0000002e,
CPG_PERF_SEL_UTCL2IU_STALL_WAIT_ON_TAGS  = 0x0000002f,
CPG_PERF_SEL_UTCL1_STALL_ON_TRANSLATION  = 0x00000030,
} CPG_PERFCOUNT_SEL;

/*
 * CPF_PERFCOUNT_SEL enum
 */

typedef enum CPF_PERFCOUNT_SEL {
CPF_PERF_SEL_ALWAYS_COUNT                = 0x00000000,
CPF_PERF_SEL_MIU_STALLED_WAITING_RDREQ_FREE  = 0x00000001,
CPF_PERF_SEL_TCIU_STALLED_WAITING_ON_FREE  = 0x00000002,
CPF_PERF_SEL_TCIU_STALLED_WAITING_ON_TAGS  = 0x00000003,
CPF_PERF_SEL_CSF_BUSY_FOR_FETCHING_RING  = 0x00000004,
CPF_PERF_SEL_CSF_BUSY_FOR_FETCHING_IB1   = 0x00000005,
CPF_PERF_SEL_CSF_BUSY_FOR_FETCHING_IB2   = 0x00000006,
CPF_PERF_SEL_CSF_BUSY_FOR_FECTHINC_STATE  = 0x00000007,
CPF_PERF_SEL_MIU_BUSY_FOR_OUTSTANDING_TAGS  = 0x00000008,
CPF_PERF_SEL_CSF_RTS_MIU_NOT_RTR         = 0x00000009,
CPF_PERF_SEL_CSF_STATE_FIFO_NOT_RTR      = 0x0000000a,
CPF_PERF_SEL_CSF_FETCHING_CMD_BUFFERS    = 0x0000000b,
CPF_PERF_SEL_GRBM_DWORDS_SENT            = 0x0000000c,
CPF_PERF_SEL_DYNAMIC_CLOCK_VALID         = 0x0000000d,
CPF_PERF_SEL_REGISTER_CLOCK_VALID        = 0x0000000e,
CPF_PERF_SEL_MIU_WRITE_REQUEST_SEND      = 0x0000000f,
CPF_PERF_SEL_MIU_READ_REQUEST_SEND       = 0x00000010,
CPF_PERF_SEL_UTCL2IU_STALL_WAIT_ON_FREE  = 0x00000011,
CPF_PERF_SEL_UTCL2IU_STALL_WAIT_ON_TAGS  = 0x00000012,
CPF_PERF_SEL_UTCL1_STALL_ON_TRANSLATION  = 0x00000013,
CPF_PERF_SEL_RCIU_STALL_WAIT_ON_FREE     = 0x00000014,
} CPF_PERFCOUNT_SEL;

/*
 * CPC_PERFCOUNT_SEL enum
 */

typedef enum CPC_PERFCOUNT_SEL {
CPC_PERF_SEL_ALWAYS_COUNT                = 0x00000000,
CPC_PERF_SEL_RCIU_STALL_WAIT_ON_FREE     = 0x00000001,
CPC_PERF_SEL_RCIU_STALL_PRIV_VIOLATION   = 0x00000002,
CPC_PERF_SEL_MIU_STALL_ON_RDREQ_FREE     = 0x00000003,
CPC_PERF_SEL_MIU_STALL_ON_WRREQ_FREE     = 0x00000004,
CPC_PERF_SEL_TCIU_STALL_WAIT_ON_FREE     = 0x00000005,
CPC_PERF_SEL_ME1_STALL_WAIT_ON_RCIU_READY  = 0x00000006,
CPC_PERF_SEL_ME1_STALL_WAIT_ON_RCIU_READY_PERF  = 0x00000007,
CPC_PERF_SEL_ME1_STALL_WAIT_ON_RCIU_READ  = 0x00000008,
CPC_PERF_SEL_ME1_STALL_WAIT_ON_MIU_READ  = 0x00000009,
CPC_PERF_SEL_ME1_STALL_WAIT_ON_MIU_WRITE  = 0x0000000a,
CPC_PERF_SEL_ME1_STALL_ON_DATA_FROM_ROQ  = 0x0000000b,
CPC_PERF_SEL_ME1_STALL_ON_DATA_FROM_ROQ_PERF  = 0x0000000c,
CPC_PERF_SEL_ME1_BUSY_FOR_PACKET_DECODE  = 0x0000000d,
CPC_PERF_SEL_ME2_STALL_WAIT_ON_RCIU_READY  = 0x0000000e,
CPC_PERF_SEL_ME2_STALL_WAIT_ON_RCIU_READY_PERF  = 0x0000000f,
CPC_PERF_SEL_ME2_STALL_WAIT_ON_RCIU_READ  = 0x00000010,
CPC_PERF_SEL_ME2_STALL_WAIT_ON_MIU_READ  = 0x00000011,
CPC_PERF_SEL_ME2_STALL_WAIT_ON_MIU_WRITE  = 0x00000012,
CPC_PERF_SEL_ME2_STALL_ON_DATA_FROM_ROQ  = 0x00000013,
CPC_PERF_SEL_ME2_STALL_ON_DATA_FROM_ROQ_PERF  = 0x00000014,
CPC_PERF_SEL_ME2_BUSY_FOR_PACKET_DECODE  = 0x00000015,
CPC_PERF_SEL_UTCL2IU_STALL_WAIT_ON_FREE  = 0x00000016,
CPC_PERF_SEL_UTCL2IU_STALL_WAIT_ON_TAGS  = 0x00000017,
CPC_PERF_SEL_UTCL1_STALL_ON_TRANSLATION  = 0x00000018,
} CPC_PERFCOUNT_SEL;

/*
 * CP_ALPHA_TAG_RAM_SEL enum
 */

typedef enum CP_ALPHA_TAG_RAM_SEL {
CPG_TAG_RAM                              = 0x00000000,
CPC_TAG_RAM                              = 0x00000001,
CPF_TAG_RAM                              = 0x00000002,
RSV_TAG_RAM                              = 0x00000003,
} CP_ALPHA_TAG_RAM_SEL;

/*
 * SEM_RESPONSE value
 */

#define SEM_ECC_ERROR                  0x00000000
#define SEM_TRANS_ERROR                0x00000001
#define SEM_FAILED                     0x00000002
#define SEM_PASSED                     0x00000003

/*
 * IQ_RETRY_TYPE value
 */

#define IQ_QUEUE_SLEEP                 0x00000000
#define IQ_OFFLOAD_RETRY               0x00000001
#define IQ_SCH_WAVE_MSG                0x00000002
#define IQ_SEM_REARM                   0x00000003
#define IQ_DEQUEUE_RETRY               0x00000004

/*
 * IQ_INTR_TYPE value
 */

#define IQ_INTR_TYPE_PQ                0x00000000
#define IQ_INTR_TYPE_IB                0x00000001
#define IQ_INTR_TYPE_MQD               0x00000002

/*
 * VMID_SIZE value
 */

#define VMID_SZ                        0x00000004

/*
 * CONFIG_SPACE value
 */

#define CONFIG_SPACE_START             0x00002000
#define CONFIG_SPACE_END               0x00009fff

/*
 * CONFIG_SPACE1 value
 */

#define CONFIG_SPACE1_START            0x00002000
#define CONFIG_SPACE1_END              0x00002bff

/*
 * CONFIG_SPACE2 value
 */

#define CONFIG_SPACE2_START            0x00003000
#define CONFIG_SPACE2_END              0x00009fff

/*
 * UCONFIG_SPACE value
 */

#define UCONFIG_SPACE_START            0x0000c000
#define UCONFIG_SPACE_END              0x0000ffff

/*
 * PERSISTENT_SPACE value
 */

#define PERSISTENT_SPACE_START         0x00002c00
#define PERSISTENT_SPACE_END           0x00002fff

/*
 * CONTEXT_SPACE value
 */

#define CONTEXT_SPACE_START            0x0000a000
#define CONTEXT_SPACE_END              0x0000bfff

/*******************************************************
 * SQ_UC Enums
 *******************************************************/

/*
 * VALUE_SQ_ENC_SOP1 value
 */

#define SQ_ENC_SOP1_BITS               0xbe800000
#define SQ_ENC_SOP1_MASK               0xff800000
#define SQ_ENC_SOP1_FIELD              0x0000017d

/*
 * VALUE_SQ_ENC_SOPC value
 */

#define SQ_ENC_SOPC_BITS               0xbf000000
#define SQ_ENC_SOPC_MASK               0xff800000
#define SQ_ENC_SOPC_FIELD              0x0000017e

/*
 * VALUE_SQ_ENC_SOPP value
 */

#define SQ_ENC_SOPP_BITS               0xbf800000
#define SQ_ENC_SOPP_MASK               0xff800000
#define SQ_ENC_SOPP_FIELD              0x0000017f

/*
 * VALUE_SQ_ENC_SOPK value
 */

#define SQ_ENC_SOPK_BITS               0xb0000000
#define SQ_ENC_SOPK_MASK               0xf0000000
#define SQ_ENC_SOPK_FIELD              0x0000000b

/*
 * VALUE_SQ_ENC_SOP2 value
 */

#define SQ_ENC_SOP2_BITS               0x80000000
#define SQ_ENC_SOP2_MASK               0xc0000000
#define SQ_ENC_SOP2_FIELD              0x00000002

/*
 * VALUE_SQ_ENC_SMEM value
 */

#define SQ_ENC_SMEM_BITS               0xc0000000
#define SQ_ENC_SMEM_MASK               0xfc000000
#define SQ_ENC_SMEM_FIELD              0x00000030

/*
 * VALUE_SQ_ENC_VOP1 value
 */

#define SQ_ENC_VOP1_BITS               0x7e000000
#define SQ_ENC_VOP1_MASK               0xfe000000
#define SQ_ENC_VOP1_FIELD              0x0000003f

/*
 * VALUE_SQ_ENC_VOPC value
 */

#define SQ_ENC_VOPC_BITS               0x7c000000
#define SQ_ENC_VOPC_MASK               0xfe000000
#define SQ_ENC_VOPC_FIELD              0x0000003e

/*
 * VALUE_SQ_ENC_VOP2 value
 */

#define SQ_ENC_VOP2_BITS               0x00000000
#define SQ_ENC_VOP2_MASK               0x80000000
#define SQ_ENC_VOP2_FIELD              0x00000000

/*
 * VALUE_SQ_ENC_VINTRP value
 */

#define SQ_ENC_VINTRP_BITS             0xd4000000
#define SQ_ENC_VINTRP_MASK             0xfc000000
#define SQ_ENC_VINTRP_FIELD            0x00000035

/*
 * VALUE_SQ_ENC_VOP3P value
 */

#define SQ_ENC_VOP3P_BITS              0xd3800000
#define SQ_ENC_VOP3P_MASK              0xff800000
#define SQ_ENC_VOP3P_FIELD             0x000001a7

/*
 * VALUE_SQ_ENC_VOP3 value
 */

#define SQ_ENC_VOP3_BITS               0xd0000000
#define SQ_ENC_VOP3_MASK               0xfc000000
#define SQ_ENC_VOP3_FIELD              0x00000034

/*
 * VALUE_SQ_ENC_DS value
 */

#define SQ_ENC_DS_BITS                 0xd8000000
#define SQ_ENC_DS_MASK                 0xfc000000
#define SQ_ENC_DS_FIELD                0x00000036

/*
 * VALUE_SQ_ENC_MUBUF value
 */

#define SQ_ENC_MUBUF_BITS              0xe0000000
#define SQ_ENC_MUBUF_MASK              0xfc000000
#define SQ_ENC_MUBUF_FIELD             0x00000038

/*
 * VALUE_SQ_ENC_MTBUF value
 */

#define SQ_ENC_MTBUF_BITS              0xe8000000
#define SQ_ENC_MTBUF_MASK              0xfc000000
#define SQ_ENC_MTBUF_FIELD             0x0000003a

/*
 * VALUE_SQ_ENC_MIMG value
 */

#define SQ_ENC_MIMG_BITS               0xf0000000
#define SQ_ENC_MIMG_MASK               0xfc000000
#define SQ_ENC_MIMG_FIELD              0x0000003c

/*
 * VALUE_SQ_ENC_EXP value
 */

#define SQ_ENC_EXP_BITS                0xc4000000
#define SQ_ENC_EXP_MASK                0xfc000000
#define SQ_ENC_EXP_FIELD               0x00000031

/*
 * VALUE_SQ_ENC_FLAT value
 */

#define SQ_ENC_FLAT_BITS               0xdc000000
#define SQ_ENC_FLAT_MASK               0xfc000000
#define SQ_ENC_FLAT_FIELD              0x00000037

/*
 * VALUE_SQ_V_OP3_INTRP_COUNT value
 */

#define SQ_V_OP3_INTRP_COUNT           0x0000000c

/*
 * VALUE_SQ_SENDMSG_SYSTEM_SIZE value
 */

#define SQ_SENDMSG_SYSTEM_SIZE         0x00000003

/*
 * VALUE_SQ_HWREG_ID_SIZE value
 */

#define SQ_HWREG_ID_SIZE               0x00000006

/*
 * VALUE_SQ_V_OPC_COUNT value
 */

#define SQ_V_OPC_COUNT                 0x00000100

/*
 * VALUE_SQ_NUM_VGPR value
 */

#define SQ_NUM_VGPR                    0x00000100

/*
 * VALUE_SQ_WAITCNT_LGKM_SHIFT value
 */

#define SQ_WAITCNT_LGKM_SHIFT          0x00000008

/*
 * VALUE_SQ_HWREG_ID_SHIFT value
 */

#define SQ_HWREG_ID_SHIFT              0x00000000

/*
 * VALUE_SQ_EXP_NUM_POS value
 */

#define SQ_EXP_NUM_POS                 0x00000004

/*
 * VALUE_SQ_XLATE_VOP3_TO_VOPC_OFFSET value
 */

#define SQ_XLATE_VOP3_TO_VOPC_OFFSET   0x00000000

/*
 * VALUE_SQ_V_OP3_2IN_OFFSET value
 */

#define SQ_V_OP3_2IN_OFFSET            0x00000280

/*
 * VALUE_SQ_XLATE_VOP3_TO_VOP2_OFFSET value
 */

#define SQ_XLATE_VOP3_TO_VOP2_OFFSET   0x00000100

/*
 * VALUE_SQ_EXP_NUM_MRT value
 */

#define SQ_EXP_NUM_MRT                 0x00000008

/*
 * VALUE_SQ_NUM_TTMP value
 */

#define SQ_NUM_TTMP                    0x00000010

/*
 * VALUE_SQ_SENDMSG_STREAMID_SHIFT value
 */

#define SQ_SENDMSG_STREAMID_SHIFT      0x00000008

/*
 * VALUE_SQ_V_OP1_COUNT value
 */

#define SQ_V_OP1_COUNT                 0x00000080

/*
 * VALUE_SQ_WAITCNT_LGKM_SIZE value
 */

#define SQ_WAITCNT_LGKM_SIZE           0x00000004

/*
 * VALUE_SQ_XLATE_VOP3_TO_VOPC_COUNT value
 */

#define SQ_XLATE_VOP3_TO_VOPC_COUNT    0x00000100

/*
 * VALUE_SQ_SENDMSG_MSG_SHIFT value
 */

#define SQ_SENDMSG_MSG_SHIFT           0x00000000

/*
 * VALUE_SQ_V_OP3_3IN_OFFSET value
 */

#define SQ_V_OP3_3IN_OFFSET            0x000001c0

/*
 * VALUE_SQ_HWREG_OFFSET_SHIFT value
 */

#define SQ_HWREG_OFFSET_SHIFT          0x00000006

/*
 * VALUE_SQ_HWREG_SIZE_SHIFT value
 */

#define SQ_HWREG_SIZE_SHIFT            0x0000000b

/*
 * VALUE_SQ_HWREG_OFFSET_SIZE value
 */

#define SQ_HWREG_OFFSET_SIZE           0x00000005

/*
 * VALUE_SQ_V_OP3_3IN_COUNT value
 */

#define SQ_V_OP3_3IN_COUNT             0x000000b0

/*
 * VALUE_SQ_SENDMSG_MSG_SIZE value
 */

#define SQ_SENDMSG_MSG_SIZE            0x00000004

/*
 * VALUE_SQ_XLATE_VOP3_TO_VOP1_COUNT value
 */

#define SQ_XLATE_VOP3_TO_VOP1_COUNT    0x00000080

/*
 * VALUE_SQ_EXP_NUM_GDS value
 */

#define SQ_EXP_NUM_GDS                 0x00000005

/*
 * VALUE_SQ_V_OP2_COUNT value
 */

#define SQ_V_OP2_COUNT                 0x00000040

/*
 * VALUE_SQ_SENDMSG_GSOP_SIZE value
 */

#define SQ_SENDMSG_GSOP_SIZE           0x00000002

/*
 * VALUE_SQ_WAITCNT_VM_SHIFT value
 */

#define SQ_WAITCNT_VM_SHIFT            0x00000000

/*
 * VALUE_SQ_XLATE_VOP3_TO_VOP3P_COUNT value
 */

#define SQ_XLATE_VOP3_TO_VOP3P_COUNT   0x00000080

/*
 * VALUE_SQ_V_OP3_2IN_COUNT value
 */

#define SQ_V_OP3_2IN_COUNT             0x00000080

/*
 * VALUE_SQ_SENDMSG_SYSTEM_SHIFT value
 */

#define SQ_SENDMSG_SYSTEM_SHIFT        0x00000004

/*
 * VALUE_SQ_WAITCNT_VM_SIZE value
 */

#define SQ_WAITCNT_VM_SIZE             0x00000004

/*
 * VALUE_SQ_XLATE_VOP3_TO_VOP3P_OFFSET value
 */

#define SQ_XLATE_VOP3_TO_VOP3P_OFFSET  0x00000380

/*
 * VALUE_SQ_WAITCNT_EXP_SHIFT value
 */

#define SQ_WAITCNT_EXP_SHIFT           0x00000004

/*
 * VALUE_SQ_XLATE_VOP3_TO_VOP2_COUNT value
 */

#define SQ_XLATE_VOP3_TO_VOP2_COUNT    0x00000040

/*
 * VALUE_SQ_EXP_NUM_PARAM value
 */

#define SQ_EXP_NUM_PARAM               0x00000020

/*
 * VALUE_SQ_HWREG_SIZE_SIZE value
 */

#define SQ_HWREG_SIZE_SIZE             0x00000005

/*
 * VALUE_SQ_WAITCNT_EXP_SIZE value
 */

#define SQ_WAITCNT_EXP_SIZE            0x00000003

/*
 * VALUE_SQ_V_OP3_INTRP_OFFSET value
 */

#define SQ_V_OP3_INTRP_OFFSET          0x00000274

/*
 * VALUE_SQ_SENDMSG_GSOP_SHIFT value
 */

#define SQ_SENDMSG_GSOP_SHIFT          0x00000004

/*
 * VALUE_SQ_XLATE_VOP3_TO_VINTRP_OFFSET value
 */

#define SQ_XLATE_VOP3_TO_VINTRP_OFFSET 0x00000270

/*
 * VALUE_SQ_NUM_ATTR value
 */

#define SQ_NUM_ATTR                    0x00000021

/*
 * VALUE_SQ_NUM_SGPR value
 */

#define SQ_NUM_SGPR                    0x00000066

/*
 * VALUE_SQ_SRC_VGPR_BIT value
 */

#define SQ_SRC_VGPR_BIT                0x00000100

/*
 * VALUE_SQ_V_INTRP_COUNT value
 */

#define SQ_V_INTRP_COUNT               0x00000004

/*
 * VALUE_SQ_SENDMSG_STREAMID_SIZE value
 */

#define SQ_SENDMSG_STREAMID_SIZE       0x00000002

/*
 * VALUE_SQ_V_OP3P_COUNT value
 */

#define SQ_V_OP3P_COUNT                0x00000080

/*
 * VALUE_SQ_XLATE_VOP3_TO_VOP1_OFFSET value
 */

#define SQ_XLATE_VOP3_TO_VOP1_OFFSET   0x00000140

/*
 * VALUE_SQ_XLATE_VOP3_TO_VINTRP_COUNT value
 */

#define SQ_XLATE_VOP3_TO_VINTRP_COUNT  0x00000004

/*
 * VALUE_SQ_SSRC_SPECIAL_DPP value
 */

#define SQ_SRC_DPP                     0x000000fa

/*
 * VALUE_SQ_OP_MTBUF value
 */

#define SQ_TBUFFER_LOAD_FORMAT_X       0x00000000
#define SQ_TBUFFER_LOAD_FORMAT_XY      0x00000001
#define SQ_TBUFFER_LOAD_FORMAT_XYZ     0x00000002
#define SQ_TBUFFER_LOAD_FORMAT_XYZW    0x00000003
#define SQ_TBUFFER_STORE_FORMAT_X      0x00000004
#define SQ_TBUFFER_STORE_FORMAT_XY     0x00000005
#define SQ_TBUFFER_STORE_FORMAT_XYZ    0x00000006
#define SQ_TBUFFER_STORE_FORMAT_XYZW   0x00000007
#define SQ_TBUFFER_LOAD_FORMAT_D16_X   0x00000008
#define SQ_TBUFFER_LOAD_FORMAT_D16_XY  0x00000009
#define SQ_TBUFFER_LOAD_FORMAT_D16_XYZ 0x0000000a
#define SQ_TBUFFER_LOAD_FORMAT_D16_XYZW 0x0000000b
#define SQ_TBUFFER_STORE_FORMAT_D16_X  0x0000000c
#define SQ_TBUFFER_STORE_FORMAT_D16_XY 0x0000000d
#define SQ_TBUFFER_STORE_FORMAT_D16_XYZ 0x0000000e
#define SQ_TBUFFER_STORE_FORMAT_D16_XYZW 0x0000000f

/*
 * VALUE_SQ_OP_FLAT_GLBL value
 */

#define SQ_GLOBAL_LOAD_UBYTE           0x00000010
#define SQ_GLOBAL_LOAD_SBYTE           0x00000011
#define SQ_GLOBAL_LOAD_USHORT          0x00000012
#define SQ_GLOBAL_LOAD_SSHORT          0x00000013
#define SQ_GLOBAL_LOAD_DWORD           0x00000014
#define SQ_GLOBAL_LOAD_DWORDX2         0x00000015
#define SQ_GLOBAL_LOAD_DWORDX3         0x00000016
#define SQ_GLOBAL_LOAD_DWORDX4         0x00000017
#define SQ_GLOBAL_STORE_BYTE           0x00000018
#define SQ_GLOBAL_STORE_SHORT          0x0000001a
#define SQ_GLOBAL_STORE_DWORD          0x0000001c
#define SQ_GLOBAL_STORE_DWORDX2        0x0000001d
#define SQ_GLOBAL_STORE_DWORDX3        0x0000001e
#define SQ_GLOBAL_STORE_DWORDX4        0x0000001f
#define SQ_GLOBAL_ATOMIC_SWAP          0x00000040
#define SQ_GLOBAL_ATOMIC_CMPSWAP       0x00000041
#define SQ_GLOBAL_ATOMIC_ADD           0x00000042
#define SQ_GLOBAL_ATOMIC_SUB           0x00000043
#define SQ_GLOBAL_ATOMIC_SMIN          0x00000044
#define SQ_GLOBAL_ATOMIC_UMIN          0x00000045
#define SQ_GLOBAL_ATOMIC_SMAX          0x00000046
#define SQ_GLOBAL_ATOMIC_UMAX          0x00000047
#define SQ_GLOBAL_ATOMIC_AND           0x00000048
#define SQ_GLOBAL_ATOMIC_OR            0x00000049
#define SQ_GLOBAL_ATOMIC_XOR           0x0000004a
#define SQ_GLOBAL_ATOMIC_INC           0x0000004b
#define SQ_GLOBAL_ATOMIC_DEC           0x0000004c
#define SQ_GLOBAL_ATOMIC_SWAP_X2       0x00000060
#define SQ_GLOBAL_ATOMIC_CMPSWAP_X2    0x00000061
#define SQ_GLOBAL_ATOMIC_ADD_X2        0x00000062
#define SQ_GLOBAL_ATOMIC_SUB_X2        0x00000063
#define SQ_GLOBAL_ATOMIC_SMIN_X2       0x00000064
#define SQ_GLOBAL_ATOMIC_UMIN_X2       0x00000065
#define SQ_GLOBAL_ATOMIC_SMAX_X2       0x00000066
#define SQ_GLOBAL_ATOMIC_UMAX_X2       0x00000067
#define SQ_GLOBAL_ATOMIC_AND_X2        0x00000068
#define SQ_GLOBAL_ATOMIC_OR_X2         0x00000069
#define SQ_GLOBAL_ATOMIC_XOR_X2        0x0000006a
#define SQ_GLOBAL_ATOMIC_INC_X2        0x0000006b
#define SQ_GLOBAL_ATOMIC_DEC_X2        0x0000006c

/*
 * VALUE_SQ_VGPR value
 */

#define SQ_VGPR0                       0x00000000

/*
 * VALUE_SQ_OP_FLAT_SCRATCH value
 */

#define SQ_SCRATCH_LOAD_UBYTE          0x00000010
#define SQ_SCRATCH_LOAD_SBYTE          0x00000011
#define SQ_SCRATCH_LOAD_USHORT         0x00000012
#define SQ_SCRATCH_LOAD_SSHORT         0x00000013
#define SQ_SCRATCH_LOAD_DWORD          0x00000014
#define SQ_SCRATCH_LOAD_DWORDX2        0x00000015
#define SQ_SCRATCH_LOAD_DWORDX3        0x00000016
#define SQ_SCRATCH_LOAD_DWORDX4        0x00000017
#define SQ_SCRATCH_STORE_BYTE          0x00000018
#define SQ_SCRATCH_STORE_SHORT         0x0000001a
#define SQ_SCRATCH_STORE_DWORD         0x0000001c
#define SQ_SCRATCH_STORE_DWORDX2       0x0000001d
#define SQ_SCRATCH_STORE_DWORDX3       0x0000001e
#define SQ_SCRATCH_STORE_DWORDX4       0x0000001f

/*
 * VALUE_SQ_VCC value
 */

#define SQ_VCC_ALL                     0x00000000

/*
 * VALUE_SQ_SSRC_0_63_INLINES value
 */

#define SQ_SRC_0                       0x00000080
#define SQ_SRC_1_INT                   0x00000081
#define SQ_SRC_2_INT                   0x00000082
#define SQ_SRC_3_INT                   0x00000083
#define SQ_SRC_4_INT                   0x00000084
#define SQ_SRC_5_INT                   0x00000085
#define SQ_SRC_6_INT                   0x00000086
#define SQ_SRC_7_INT                   0x00000087
#define SQ_SRC_8_INT                   0x00000088
#define SQ_SRC_9_INT                   0x00000089
#define SQ_SRC_10_INT                  0x0000008a
#define SQ_SRC_11_INT                  0x0000008b
#define SQ_SRC_12_INT                  0x0000008c
#define SQ_SRC_13_INT                  0x0000008d
#define SQ_SRC_14_INT                  0x0000008e
#define SQ_SRC_15_INT                  0x0000008f
#define SQ_SRC_16_INT                  0x00000090
#define SQ_SRC_17_INT                  0x00000091
#define SQ_SRC_18_INT                  0x00000092
#define SQ_SRC_19_INT                  0x00000093
#define SQ_SRC_20_INT                  0x00000094
#define SQ_SRC_21_INT                  0x00000095
#define SQ_SRC_22_INT                  0x00000096
#define SQ_SRC_23_INT                  0x00000097
#define SQ_SRC_24_INT                  0x00000098
#define SQ_SRC_25_INT                  0x00000099
#define SQ_SRC_26_INT                  0x0000009a
#define SQ_SRC_27_INT                  0x0000009b
#define SQ_SRC_28_INT                  0x0000009c
#define SQ_SRC_29_INT                  0x0000009d
#define SQ_SRC_30_INT                  0x0000009e
#define SQ_SRC_31_INT                  0x0000009f
#define SQ_SRC_32_INT                  0x000000a0
#define SQ_SRC_33_INT                  0x000000a1
#define SQ_SRC_34_INT                  0x000000a2
#define SQ_SRC_35_INT                  0x000000a3
#define SQ_SRC_36_INT                  0x000000a4
#define SQ_SRC_37_INT                  0x000000a5
#define SQ_SRC_38_INT                  0x000000a6
#define SQ_SRC_39_INT                  0x000000a7
#define SQ_SRC_40_INT                  0x000000a8
#define SQ_SRC_41_INT                  0x000000a9
#define SQ_SRC_42_INT                  0x000000aa
#define SQ_SRC_43_INT                  0x000000ab
#define SQ_SRC_44_INT                  0x000000ac
#define SQ_SRC_45_INT                  0x000000ad
#define SQ_SRC_46_INT                  0x000000ae
#define SQ_SRC_47_INT                  0x000000af
#define SQ_SRC_48_INT                  0x000000b0
#define SQ_SRC_49_INT                  0x000000b1
#define SQ_SRC_50_INT                  0x000000b2
#define SQ_SRC_51_INT                  0x000000b3
#define SQ_SRC_52_INT                  0x000000b4
#define SQ_SRC_53_INT                  0x000000b5
#define SQ_SRC_54_INT                  0x000000b6
#define SQ_SRC_55_INT                  0x000000b7
#define SQ_SRC_56_INT                  0x000000b8
#define SQ_SRC_57_INT                  0x000000b9
#define SQ_SRC_58_INT                  0x000000ba
#define SQ_SRC_59_INT                  0x000000bb
#define SQ_SRC_60_INT                  0x000000bc
#define SQ_SRC_61_INT                  0x000000bd
#define SQ_SRC_62_INT                  0x000000be
#define SQ_SRC_63_INT                  0x000000bf

/*
 * VALUE_SQ_OP_MIMG value
 */

#define SQ_IMAGE_LOAD                  0x00000000
#define SQ_IMAGE_LOAD_MIP              0x00000001
#define SQ_IMAGE_LOAD_PCK              0x00000002
#define SQ_IMAGE_LOAD_PCK_SGN          0x00000003
#define SQ_IMAGE_LOAD_MIP_PCK          0x00000004
#define SQ_IMAGE_LOAD_MIP_PCK_SGN      0x00000005
#define SQ_IMAGE_STORE                 0x00000008
#define SQ_IMAGE_STORE_MIP             0x00000009
#define SQ_IMAGE_STORE_PCK             0x0000000a
#define SQ_IMAGE_STORE_MIP_PCK         0x0000000b
#define SQ_IMAGE_GET_RESINFO           0x0000000e
#define SQ_IMAGE_ATOMIC_SWAP           0x00000010
#define SQ_IMAGE_ATOMIC_CMPSWAP        0x00000011
#define SQ_IMAGE_ATOMIC_ADD            0x00000012
#define SQ_IMAGE_ATOMIC_SUB            0x00000013
#define SQ_IMAGE_ATOMIC_SMIN           0x00000014
#define SQ_IMAGE_ATOMIC_UMIN           0x00000015
#define SQ_IMAGE_ATOMIC_SMAX           0x00000016
#define SQ_IMAGE_ATOMIC_UMAX           0x00000017
#define SQ_IMAGE_ATOMIC_AND            0x00000018
#define SQ_IMAGE_ATOMIC_OR             0x00000019
#define SQ_IMAGE_ATOMIC_XOR            0x0000001a
#define SQ_IMAGE_ATOMIC_INC            0x0000001b
#define SQ_IMAGE_ATOMIC_DEC            0x0000001c
#define SQ_IMAGE_SAMPLE                0x00000020
#define SQ_IMAGE_SAMPLE_CL             0x00000021
#define SQ_IMAGE_SAMPLE_D              0x00000022
#define SQ_IMAGE_SAMPLE_D_CL           0x00000023
#define SQ_IMAGE_SAMPLE_L              0x00000024
#define SQ_IMAGE_SAMPLE_B              0x00000025
#define SQ_IMAGE_SAMPLE_B_CL           0x00000026
#define SQ_IMAGE_SAMPLE_LZ             0x00000027
#define SQ_IMAGE_SAMPLE_C              0x00000028
#define SQ_IMAGE_SAMPLE_C_CL           0x00000029
#define SQ_IMAGE_SAMPLE_C_D            0x0000002a
#define SQ_IMAGE_SAMPLE_C_D_CL         0x0000002b
#define SQ_IMAGE_SAMPLE_C_L            0x0000002c
#define SQ_IMAGE_SAMPLE_C_B            0x0000002d
#define SQ_IMAGE_SAMPLE_C_B_CL         0x0000002e
#define SQ_IMAGE_SAMPLE_C_LZ           0x0000002f
#define SQ_IMAGE_SAMPLE_O              0x00000030
#define SQ_IMAGE_SAMPLE_CL_O           0x00000031
#define SQ_IMAGE_SAMPLE_D_O            0x00000032
#define SQ_IMAGE_SAMPLE_D_CL_O         0x00000033
#define SQ_IMAGE_SAMPLE_L_O            0x00000034
#define SQ_IMAGE_SAMPLE_B_O            0x00000035
#define SQ_IMAGE_SAMPLE_B_CL_O         0x00000036
#define SQ_IMAGE_SAMPLE_LZ_O           0x00000037
#define SQ_IMAGE_SAMPLE_C_O            0x00000038
#define SQ_IMAGE_SAMPLE_C_CL_O         0x00000039
#define SQ_IMAGE_SAMPLE_C_D_O          0x0000003a
#define SQ_IMAGE_SAMPLE_C_D_CL_O       0x0000003b
#define SQ_IMAGE_SAMPLE_C_L_O          0x0000003c
#define SQ_IMAGE_SAMPLE_C_B_O          0x0000003d
#define SQ_IMAGE_SAMPLE_C_B_CL_O       0x0000003e
#define SQ_IMAGE_SAMPLE_C_LZ_O         0x0000003f
#define SQ_IMAGE_GATHER4               0x00000040
#define SQ_IMAGE_GATHER4_CL            0x00000041
#define SQ_IMAGE_GATHER4H              0x00000042
#define SQ_IMAGE_GATHER4_L             0x00000044
#define SQ_IMAGE_GATHER4_B             0x00000045
#define SQ_IMAGE_GATHER4_B_CL          0x00000046
#define SQ_IMAGE_GATHER4_LZ            0x00000047
#define SQ_IMAGE_GATHER4_C             0x00000048
#define SQ_IMAGE_GATHER4_C_CL          0x00000049
#define SQ_IMAGE_GATHER4H_PCK          0x0000004a
#define SQ_IMAGE_GATHER8H_PCK          0x0000004b
#define SQ_IMAGE_GATHER4_C_L           0x0000004c
#define SQ_IMAGE_GATHER4_C_B           0x0000004d
#define SQ_IMAGE_GATHER4_C_B_CL        0x0000004e
#define SQ_IMAGE_GATHER4_C_LZ          0x0000004f
#define SQ_IMAGE_GATHER4_O             0x00000050
#define SQ_IMAGE_GATHER4_CL_O          0x00000051
#define SQ_IMAGE_GATHER4_L_O           0x00000054
#define SQ_IMAGE_GATHER4_B_O           0x00000055
#define SQ_IMAGE_GATHER4_B_CL_O        0x00000056
#define SQ_IMAGE_GATHER4_LZ_O          0x00000057
#define SQ_IMAGE_GATHER4_C_O           0x00000058
#define SQ_IMAGE_GATHER4_C_CL_O        0x00000059
#define SQ_IMAGE_GATHER4_C_L_O         0x0000005c
#define SQ_IMAGE_GATHER4_C_B_O         0x0000005d
#define SQ_IMAGE_GATHER4_C_B_CL_O      0x0000005e
#define SQ_IMAGE_GATHER4_C_LZ_O        0x0000005f
#define SQ_IMAGE_GET_LOD               0x00000060
#define SQ_IMAGE_SAMPLE_CD             0x00000068
#define SQ_IMAGE_SAMPLE_CD_CL          0x00000069
#define SQ_IMAGE_SAMPLE_C_CD           0x0000006a
#define SQ_IMAGE_SAMPLE_C_CD_CL        0x0000006b
#define SQ_IMAGE_SAMPLE_CD_O           0x0000006c
#define SQ_IMAGE_SAMPLE_CD_CL_O        0x0000006d
#define SQ_IMAGE_SAMPLE_C_CD_O         0x0000006e
#define SQ_IMAGE_SAMPLE_C_CD_CL_O      0x0000006f
#define SQ_IMAGE_RSRC256               0x0000007e
#define SQ_IMAGE_SAMPLER               0x0000007f

/*
 * VALUE_SQ_HW_REG value
 */

#define SQ_HW_REG_MODE                 0x00000001
#define SQ_HW_REG_STATUS               0x00000002
#define SQ_HW_REG_TRAPSTS              0x00000003
#define SQ_HW_REG_HW_ID                0x00000004
#define SQ_HW_REG_GPR_ALLOC            0x00000005
#define SQ_HW_REG_LDS_ALLOC            0x00000006
#define SQ_HW_REG_IB_STS               0x00000007
#define SQ_HW_REG_PC_LO                0x00000008
#define SQ_HW_REG_PC_HI                0x00000009
#define SQ_HW_REG_INST_DW0             0x0000000a
#define SQ_HW_REG_INST_DW1             0x0000000b
#define SQ_HW_REG_IB_DBG0              0x0000000c
#define SQ_HW_REG_IB_DBG1              0x0000000d
#define SQ_HW_REG_FLUSH_IB             0x0000000e
#define SQ_HW_REG_SH_MEM_BASES         0x0000000f
#define SQ_HW_REG_SQ_SHADER_TBA_LO     0x00000010
#define SQ_HW_REG_SQ_SHADER_TBA_HI     0x00000011
#define SQ_HW_REG_SQ_SHADER_TMA_LO     0x00000012
#define SQ_HW_REG_SQ_SHADER_TMA_HI     0x00000013

/*
 * VALUE_SQ_OP_SOP1 value
 */

#define SQ_S_MOV_B32                   0x00000000
#define SQ_S_MOV_B64                   0x00000001
#define SQ_S_CMOV_B32                  0x00000002
#define SQ_S_CMOV_B64                  0x00000003
#define SQ_S_NOT_B32                   0x00000004
#define SQ_S_NOT_B64                   0x00000005
#define SQ_S_WQM_B32                   0x00000006
#define SQ_S_WQM_B64                   0x00000007
#define SQ_S_BREV_B32                  0x00000008
#define SQ_S_BREV_B64                  0x00000009
#define SQ_S_BCNT0_I32_B32             0x0000000a
#define SQ_S_BCNT0_I32_B64             0x0000000b
#define SQ_S_BCNT1_I32_B32             0x0000000c
#define SQ_S_BCNT1_I32_B64             0x0000000d
#define SQ_S_FF0_I32_B32               0x0000000e
#define SQ_S_FF0_I32_B64               0x0000000f
#define SQ_S_FF1_I32_B32               0x00000010
#define SQ_S_FF1_I32_B64               0x00000011
#define SQ_S_FLBIT_I32_B32             0x00000012
#define SQ_S_FLBIT_I32_B64             0x00000013
#define SQ_S_FLBIT_I32                 0x00000014
#define SQ_S_FLBIT_I32_I64             0x00000015
#define SQ_S_SEXT_I32_I8               0x00000016
#define SQ_S_SEXT_I32_I16              0x00000017
#define SQ_S_BITSET0_B32               0x00000018
#define SQ_S_BITSET0_B64               0x00000019
#define SQ_S_BITSET1_B32               0x0000001a
#define SQ_S_BITSET1_B64               0x0000001b
#define SQ_S_GETPC_B64                 0x0000001c
#define SQ_S_SETPC_B64                 0x0000001d
#define SQ_S_SWAPPC_B64                0x0000001e
#define SQ_S_RFE_B64                   0x0000001f
#define SQ_S_AND_SAVEEXEC_B64          0x00000020
#define SQ_S_OR_SAVEEXEC_B64           0x00000021
#define SQ_S_XOR_SAVEEXEC_B64          0x00000022
#define SQ_S_ANDN2_SAVEEXEC_B64        0x00000023
#define SQ_S_ORN2_SAVEEXEC_B64         0x00000024
#define SQ_S_NAND_SAVEEXEC_B64         0x00000025
#define SQ_S_NOR_SAVEEXEC_B64          0x00000026
#define SQ_S_XNOR_SAVEEXEC_B64         0x00000027
#define SQ_S_QUADMASK_B32              0x00000028
#define SQ_S_QUADMASK_B64              0x00000029
#define SQ_S_MOVRELS_B32               0x0000002a
#define SQ_S_MOVRELS_B64               0x0000002b
#define SQ_S_MOVRELD_B32               0x0000002c
#define SQ_S_MOVRELD_B64               0x0000002d
#define SQ_S_CBRANCH_JOIN              0x0000002e
#define SQ_S_MOV_REGRD_B32             0x0000002f
#define SQ_S_ABS_I32                   0x00000030
#define SQ_S_MOV_FED_B32               0x00000031
#define SQ_S_SET_GPR_IDX_IDX           0x00000032
#define SQ_S_ANDN1_SAVEEXEC_B64        0x00000033
#define SQ_S_ORN1_SAVEEXEC_B64         0x00000034
#define SQ_S_ANDN1_WREXEC_B64          0x00000035
#define SQ_S_ANDN2_WREXEC_B64          0x00000036
#define SQ_S_BITREPLICATE_B64_B32      0x00000037

/*
 * VALUE_SQ_CNT value
 */

#define SQ_CNT1                        0x00000000
#define SQ_CNT2                        0x00000001
#define SQ_CNT3                        0x00000002
#define SQ_CNT4                        0x00000003

/*
 * VALUE_SQ_OP_VOP3 value
 */

#define SQ_V_MAD_LEGACY_F32            0x000001c0
#define SQ_V_MAD_F32                   0x000001c1
#define SQ_V_MAD_I32_I24               0x000001c2
#define SQ_V_MAD_U32_U24               0x000001c3
#define SQ_V_CUBEID_F32                0x000001c4
#define SQ_V_CUBESC_F32                0x000001c5
#define SQ_V_CUBETC_F32                0x000001c6
#define SQ_V_CUBEMA_F32                0x000001c7
#define SQ_V_BFE_U32                   0x000001c8
#define SQ_V_BFE_I32                   0x000001c9
#define SQ_V_BFI_B32                   0x000001ca
#define SQ_V_FMA_F32                   0x000001cb
#define SQ_V_FMA_F64                   0x000001cc
#define SQ_V_LERP_U8                   0x000001cd
#define SQ_V_ALIGNBIT_B32              0x000001ce
#define SQ_V_ALIGNBYTE_B32             0x000001cf
#define SQ_V_MIN3_F32                  0x000001d0
#define SQ_V_MIN3_I32                  0x000001d1
#define SQ_V_MIN3_U32                  0x000001d2
#define SQ_V_MAX3_F32                  0x000001d3
#define SQ_V_MAX3_I32                  0x000001d4
#define SQ_V_MAX3_U32                  0x000001d5
#define SQ_V_MED3_F32                  0x000001d6
#define SQ_V_MED3_I32                  0x000001d7
#define SQ_V_MED3_U32                  0x000001d8
#define SQ_V_SAD_U8                    0x000001d9
#define SQ_V_SAD_HI_U8                 0x000001da
#define SQ_V_SAD_U16                   0x000001db
#define SQ_V_SAD_U32                   0x000001dc
#define SQ_V_CVT_PK_U8_F32             0x000001dd
#define SQ_V_DIV_FIXUP_F32             0x000001de
#define SQ_V_DIV_FIXUP_F64             0x000001df
#define SQ_V_DIV_SCALE_F32             0x000001e0
#define SQ_V_DIV_SCALE_F64             0x000001e1
#define SQ_V_DIV_FMAS_F32              0x000001e2
#define SQ_V_DIV_FMAS_F64              0x000001e3
#define SQ_V_MSAD_U8                   0x000001e4
#define SQ_V_QSAD_PK_U16_U8            0x000001e5
#define SQ_V_MQSAD_PK_U16_U8           0x000001e6
#define SQ_V_MQSAD_U32_U8              0x000001e7
#define SQ_V_MAD_U64_U32               0x000001e8
#define SQ_V_MAD_I64_I32               0x000001e9
#define SQ_V_MAD_LEGACY_F16            0x000001ea
#define SQ_V_MAD_LEGACY_U16            0x000001eb
#define SQ_V_MAD_LEGACY_I16            0x000001ec
#define SQ_V_PERM_B32                  0x000001ed
#define SQ_V_FMA_LEGACY_F16            0x000001ee
#define SQ_V_DIV_FIXUP_LEGACY_F16      0x000001ef
#define SQ_V_CVT_PKACCUM_U8_F32        0x000001f0
#define SQ_V_MAD_U32_U16               0x000001f1
#define SQ_V_MAD_I32_I16               0x000001f2
#define SQ_V_XAD_U32                   0x000001f3
#define SQ_V_MIN3_F16                  0x000001f4
#define SQ_V_MIN3_I16                  0x000001f5
#define SQ_V_MIN3_U16                  0x000001f6
#define SQ_V_MAX3_F16                  0x000001f7
#define SQ_V_MAX3_I16                  0x000001f8
#define SQ_V_MAX3_U16                  0x000001f9
#define SQ_V_MED3_F16                  0x000001fa
#define SQ_V_MED3_I16                  0x000001fb
#define SQ_V_MED3_U16                  0x000001fc
#define SQ_V_LSHL_ADD_U32              0x000001fd
#define SQ_V_ADD_LSHL_U32              0x000001fe
#define SQ_V_ADD3_U32                  0x000001ff
#define SQ_V_LSHL_OR_B32               0x00000200
#define SQ_V_AND_OR_B32                0x00000201
#define SQ_V_OR3_B32                   0x00000202
#define SQ_V_MAD_F16                   0x00000203
#define SQ_V_MAD_U16                   0x00000204
#define SQ_V_MAD_I16                   0x00000205
#define SQ_V_FMA_F16                   0x00000206
#define SQ_V_DIV_FIXUP_F16             0x00000207
#define SQ_V_INTERP_P1LL_F16           0x00000274
#define SQ_V_INTERP_P1LV_F16           0x00000275
#define SQ_V_INTERP_P2_LEGACY_F16      0x00000276
#define SQ_V_INTERP_P2_F16             0x00000277
#define SQ_V_ADD_F64                   0x00000280
#define SQ_V_MUL_F64                   0x00000281
#define SQ_V_MIN_F64                   0x00000282
#define SQ_V_MAX_F64                   0x00000283
#define SQ_V_LDEXP_F64                 0x00000284
#define SQ_V_MUL_LO_U32                0x00000285
#define SQ_V_MUL_HI_U32                0x00000286
#define SQ_V_MUL_HI_I32                0x00000287
#define SQ_V_LDEXP_F32                 0x00000288
#define SQ_V_READLANE_B32              0x00000289
#define SQ_V_WRITELANE_B32             0x0000028a
#define SQ_V_BCNT_U32_B32              0x0000028b
#define SQ_V_MBCNT_LO_U32_B32          0x0000028c
#define SQ_V_MBCNT_HI_U32_B32          0x0000028d
#define SQ_V_MAC_LEGACY_F32            0x0000028e
#define SQ_V_LSHLREV_B64               0x0000028f
#define SQ_V_LSHRREV_B64               0x00000290
#define SQ_V_ASHRREV_I64               0x00000291
#define SQ_V_TRIG_PREOP_F64            0x00000292
#define SQ_V_BFM_B32                   0x00000293
#define SQ_V_CVT_PKNORM_I16_F32        0x00000294
#define SQ_V_CVT_PKNORM_U16_F32        0x00000295
#define SQ_V_CVT_PKRTZ_F16_F32         0x00000296
#define SQ_V_CVT_PK_U16_U32            0x00000297
#define SQ_V_CVT_PK_I16_I32            0x00000298
#define SQ_V_CVT_PKNORM_I16_F16        0x00000299
#define SQ_V_CVT_PKNORM_U16_F16        0x0000029a
#define SQ_V_READLANE_REGRD_B32        0x0000029b
#define SQ_V_ADD_I32                   0x0000029c
#define SQ_V_SUB_I32                   0x0000029d
#define SQ_V_ADD_I16                   0x0000029e
#define SQ_V_SUB_I16                   0x0000029f
#define SQ_V_PACK_B32_F16              0x000002a0

/*
 * VALUE_SQ_SSRC_SPECIAL_LIT value
 */

#define SQ_SRC_LITERAL                 0x000000ff

/*
 * VALUE_SQ_DPP_CTRL value
 */

#define SQ_DPP_QUAD_PERM               0x00000000
#define SQ_DPP_ROW_SL1                 0x00000101
#define SQ_DPP_ROW_SL2                 0x00000102
#define SQ_DPP_ROW_SL3                 0x00000103
#define SQ_DPP_ROW_SL4                 0x00000104
#define SQ_DPP_ROW_SL5                 0x00000105
#define SQ_DPP_ROW_SL6                 0x00000106
#define SQ_DPP_ROW_SL7                 0x00000107
#define SQ_DPP_ROW_SL8                 0x00000108
#define SQ_DPP_ROW_SL9                 0x00000109
#define SQ_DPP_ROW_SL10                0x0000010a
#define SQ_DPP_ROW_SL11                0x0000010b
#define SQ_DPP_ROW_SL12                0x0000010c
#define SQ_DPP_ROW_SL13                0x0000010d
#define SQ_DPP_ROW_SL14                0x0000010e
#define SQ_DPP_ROW_SL15                0x0000010f
#define SQ_DPP_ROW_SR1                 0x00000111
#define SQ_DPP_ROW_SR2                 0x00000112
#define SQ_DPP_ROW_SR3                 0x00000113
#define SQ_DPP_ROW_SR4                 0x00000114
#define SQ_DPP_ROW_SR5                 0x00000115
#define SQ_DPP_ROW_SR6                 0x00000116
#define SQ_DPP_ROW_SR7                 0x00000117
#define SQ_DPP_ROW_SR8                 0x00000118
#define SQ_DPP_ROW_SR9                 0x00000119
#define SQ_DPP_ROW_SR10                0x0000011a
#define SQ_DPP_ROW_SR11                0x0000011b
#define SQ_DPP_ROW_SR12                0x0000011c
#define SQ_DPP_ROW_SR13                0x0000011d
#define SQ_DPP_ROW_SR14                0x0000011e
#define SQ_DPP_ROW_SR15                0x0000011f
#define SQ_DPP_ROW_RR1                 0x00000121
#define SQ_DPP_ROW_RR2                 0x00000122
#define SQ_DPP_ROW_RR3                 0x00000123
#define SQ_DPP_ROW_RR4                 0x00000124
#define SQ_DPP_ROW_RR5                 0x00000125
#define SQ_DPP_ROW_RR6                 0x00000126
#define SQ_DPP_ROW_RR7                 0x00000127
#define SQ_DPP_ROW_RR8                 0x00000128
#define SQ_DPP_ROW_RR9                 0x00000129
#define SQ_DPP_ROW_RR10                0x0000012a
#define SQ_DPP_ROW_RR11                0x0000012b
#define SQ_DPP_ROW_RR12                0x0000012c
#define SQ_DPP_ROW_RR13                0x0000012d
#define SQ_DPP_ROW_RR14                0x0000012e
#define SQ_DPP_ROW_RR15                0x0000012f
#define SQ_DPP_WF_SL1                  0x00000130
#define SQ_DPP_WF_RL1                  0x00000134
#define SQ_DPP_WF_SR1                  0x00000138
#define SQ_DPP_WF_RR1                  0x0000013c
#define SQ_DPP_ROW_MIRROR              0x00000140
#define SQ_DPP_ROW_HALF_MIRROR         0x00000141
#define SQ_DPP_ROW_BCAST15             0x00000142
#define SQ_DPP_ROW_BCAST31             0x00000143

/*
 * VALUE_SQ_FLAT_SCRATCH_LOHI value
 */

#define SQ_FLAT_SCRATCH_LO             0x00000066
#define SQ_FLAT_SCRATCH_HI             0x00000067

/*
 * VALUE_SQ_OP_VOP1 value
 */

#define SQ_V_NOP                       0x00000000
#define SQ_V_MOV_B32                   0x00000001
#define SQ_V_READFIRSTLANE_B32         0x00000002
#define SQ_V_CVT_I32_F64               0x00000003
#define SQ_V_CVT_F64_I32               0x00000004
#define SQ_V_CVT_F32_I32               0x00000005
#define SQ_V_CVT_F32_U32               0x00000006
#define SQ_V_CVT_U32_F32               0x00000007
#define SQ_V_CVT_I32_F32               0x00000008
#define SQ_V_MOV_FED_B32               0x00000009
#define SQ_V_CVT_F16_F32               0x0000000a
#define SQ_V_CVT_F32_F16               0x0000000b
#define SQ_V_CVT_RPI_I32_F32           0x0000000c
#define SQ_V_CVT_FLR_I32_F32           0x0000000d
#define SQ_V_CVT_OFF_F32_I4            0x0000000e
#define SQ_V_CVT_F32_F64               0x0000000f
#define SQ_V_CVT_F64_F32               0x00000010
#define SQ_V_CVT_F32_UBYTE0            0x00000011
#define SQ_V_CVT_F32_UBYTE1            0x00000012
#define SQ_V_CVT_F32_UBYTE2            0x00000013
#define SQ_V_CVT_F32_UBYTE3            0x00000014
#define SQ_V_CVT_U32_F64               0x00000015
#define SQ_V_CVT_F64_U32               0x00000016
#define SQ_V_TRUNC_F64                 0x00000017
#define SQ_V_CEIL_F64                  0x00000018
#define SQ_V_RNDNE_F64                 0x00000019
#define SQ_V_FLOOR_F64                 0x0000001a
#define SQ_V_FRACT_F32                 0x0000001b
#define SQ_V_TRUNC_F32                 0x0000001c
#define SQ_V_CEIL_F32                  0x0000001d
#define SQ_V_RNDNE_F32                 0x0000001e
#define SQ_V_FLOOR_F32                 0x0000001f
#define SQ_V_EXP_F32                   0x00000020
#define SQ_V_LOG_F32                   0x00000021
#define SQ_V_RCP_F32                   0x00000022
#define SQ_V_RCP_IFLAG_F32             0x00000023
#define SQ_V_RSQ_F32                   0x00000024
#define SQ_V_RCP_F64                   0x00000025
#define SQ_V_RSQ_F64                   0x00000026
#define SQ_V_SQRT_F32                  0x00000027
#define SQ_V_SQRT_F64                  0x00000028
#define SQ_V_SIN_F32                   0x00000029
#define SQ_V_COS_F32                   0x0000002a
#define SQ_V_NOT_B32                   0x0000002b
#define SQ_V_BFREV_B32                 0x0000002c
#define SQ_V_FFBH_U32                  0x0000002d
#define SQ_V_FFBL_B32                  0x0000002e
#define SQ_V_FFBH_I32                  0x0000002f
#define SQ_V_FREXP_EXP_I32_F64         0x00000030
#define SQ_V_FREXP_MANT_F64            0x00000031
#define SQ_V_FRACT_F64                 0x00000032
#define SQ_V_FREXP_EXP_I32_F32         0x00000033
#define SQ_V_FREXP_MANT_F32            0x00000034
#define SQ_V_CLREXCP                   0x00000035
#define SQ_V_MOV_PRSV_B32              0x00000036
#define SQ_V_CVT_F16_U16               0x00000039
#define SQ_V_CVT_F16_I16               0x0000003a
#define SQ_V_CVT_U16_F16               0x0000003b
#define SQ_V_CVT_I16_F16               0x0000003c
#define SQ_V_RCP_F16                   0x0000003d
#define SQ_V_SQRT_F16                  0x0000003e
#define SQ_V_RSQ_F16                   0x0000003f
#define SQ_V_LOG_F16                   0x00000040
#define SQ_V_EXP_F16                   0x00000041
#define SQ_V_FREXP_MANT_F16            0x00000042
#define SQ_V_FREXP_EXP_I16_F16         0x00000043
#define SQ_V_FLOOR_F16                 0x00000044
#define SQ_V_CEIL_F16                  0x00000045
#define SQ_V_TRUNC_F16                 0x00000046
#define SQ_V_RNDNE_F16                 0x00000047
#define SQ_V_FRACT_F16                 0x00000048
#define SQ_V_SIN_F16                   0x00000049
#define SQ_V_COS_F16                   0x0000004a
#define SQ_V_EXP_LEGACY_F32            0x0000004b
#define SQ_V_LOG_LEGACY_F32            0x0000004c
#define SQ_V_CVT_NORM_I16_F16          0x0000004d
#define SQ_V_CVT_NORM_U16_F16          0x0000004e
#define SQ_V_SAT_PK_U8_I16             0x0000004f
#define SQ_V_WRITELANE_IMM32           0x00000050
#define SQ_V_SWAP_B32                  0x00000051

/*
 * VALUE_SQ_OP_FLAT value
 */

#define SQ_FLAT_LOAD_UBYTE             0x00000010
#define SQ_FLAT_LOAD_SBYTE             0x00000011
#define SQ_FLAT_LOAD_USHORT            0x00000012
#define SQ_FLAT_LOAD_SSHORT            0x00000013
#define SQ_FLAT_LOAD_DWORD             0x00000014
#define SQ_FLAT_LOAD_DWORDX2           0x00000015
#define SQ_FLAT_LOAD_DWORDX3           0x00000016
#define SQ_FLAT_LOAD_DWORDX4           0x00000017
#define SQ_FLAT_STORE_BYTE             0x00000018
#define SQ_FLAT_STORE_SHORT            0x0000001a
#define SQ_FLAT_STORE_DWORD            0x0000001c
#define SQ_FLAT_STORE_DWORDX2          0x0000001d
#define SQ_FLAT_STORE_DWORDX3          0x0000001e
#define SQ_FLAT_STORE_DWORDX4          0x0000001f
#define SQ_FLAT_ATOMIC_SWAP            0x00000040
#define SQ_FLAT_ATOMIC_CMPSWAP         0x00000041
#define SQ_FLAT_ATOMIC_ADD             0x00000042
#define SQ_FLAT_ATOMIC_SUB             0x00000043
#define SQ_FLAT_ATOMIC_SMIN            0x00000044
#define SQ_FLAT_ATOMIC_UMIN            0x00000045
#define SQ_FLAT_ATOMIC_SMAX            0x00000046
#define SQ_FLAT_ATOMIC_UMAX            0x00000047
#define SQ_FLAT_ATOMIC_AND             0x00000048
#define SQ_FLAT_ATOMIC_OR              0x00000049
#define SQ_FLAT_ATOMIC_XOR             0x0000004a
#define SQ_FLAT_ATOMIC_INC             0x0000004b
#define SQ_FLAT_ATOMIC_DEC             0x0000004c
#define SQ_FLAT_ATOMIC_SWAP_X2         0x00000060
#define SQ_FLAT_ATOMIC_CMPSWAP_X2      0x00000061
#define SQ_FLAT_ATOMIC_ADD_X2          0x00000062
#define SQ_FLAT_ATOMIC_SUB_X2          0x00000063
#define SQ_FLAT_ATOMIC_SMIN_X2         0x00000064
#define SQ_FLAT_ATOMIC_UMIN_X2         0x00000065
#define SQ_FLAT_ATOMIC_SMAX_X2         0x00000066
#define SQ_FLAT_ATOMIC_UMAX_X2         0x00000067
#define SQ_FLAT_ATOMIC_AND_X2          0x00000068
#define SQ_FLAT_ATOMIC_OR_X2           0x00000069
#define SQ_FLAT_ATOMIC_XOR_X2          0x0000006a
#define SQ_FLAT_ATOMIC_INC_X2          0x0000006b
#define SQ_FLAT_ATOMIC_DEC_X2          0x0000006c

/*
 * VALUE_SQ_OP_DS value
 */

#define SQ_DS_ADD_U32                  0x00000000
#define SQ_DS_SUB_U32                  0x00000001
#define SQ_DS_RSUB_U32                 0x00000002
#define SQ_DS_INC_U32                  0x00000003
#define SQ_DS_DEC_U32                  0x00000004
#define SQ_DS_MIN_I32                  0x00000005
#define SQ_DS_MAX_I32                  0x00000006
#define SQ_DS_MIN_U32                  0x00000007
#define SQ_DS_MAX_U32                  0x00000008
#define SQ_DS_AND_B32                  0x00000009
#define SQ_DS_OR_B32                   0x0000000a
#define SQ_DS_XOR_B32                  0x0000000b
#define SQ_DS_MSKOR_B32                0x0000000c
#define SQ_DS_WRITE_B32                0x0000000d
#define SQ_DS_WRITE2_B32               0x0000000e
#define SQ_DS_WRITE2ST64_B32           0x0000000f
#define SQ_DS_CMPST_B32                0x00000010
#define SQ_DS_CMPST_F32                0x00000011
#define SQ_DS_MIN_F32                  0x00000012
#define SQ_DS_MAX_F32                  0x00000013
#define SQ_DS_NOP                      0x00000014
#define SQ_DS_ADD_F32                  0x00000015
#define SQ_DS_WRITE_ADDTID_B32         0x0000001d
#define SQ_DS_WRITE_B8                 0x0000001e
#define SQ_DS_WRITE_B16                0x0000001f
#define SQ_DS_ADD_RTN_U32              0x00000020
#define SQ_DS_SUB_RTN_U32              0x00000021
#define SQ_DS_RSUB_RTN_U32             0x00000022
#define SQ_DS_INC_RTN_U32              0x00000023
#define SQ_DS_DEC_RTN_U32              0x00000024
#define SQ_DS_MIN_RTN_I32              0x00000025
#define SQ_DS_MAX_RTN_I32              0x00000026
#define SQ_DS_MIN_RTN_U32              0x00000027
#define SQ_DS_MAX_RTN_U32              0x00000028
#define SQ_DS_AND_RTN_B32              0x00000029
#define SQ_DS_OR_RTN_B32               0x0000002a
#define SQ_DS_XOR_RTN_B32              0x0000002b
#define SQ_DS_MSKOR_RTN_B32            0x0000002c
#define SQ_DS_WRXCHG_RTN_B32           0x0000002d
#define SQ_DS_WRXCHG2_RTN_B32          0x0000002e
#define SQ_DS_WRXCHG2ST64_RTN_B32      0x0000002f
#define SQ_DS_CMPST_RTN_B32            0x00000030
#define SQ_DS_CMPST_RTN_F32            0x00000031
#define SQ_DS_MIN_RTN_F32              0x00000032
#define SQ_DS_MAX_RTN_F32              0x00000033
#define SQ_DS_WRAP_RTN_B32             0x00000034
#define SQ_DS_ADD_RTN_F32              0x00000035
#define SQ_DS_READ_B32                 0x00000036
#define SQ_DS_READ2_B32                0x00000037
#define SQ_DS_READ2ST64_B32            0x00000038
#define SQ_DS_READ_I8                  0x00000039
#define SQ_DS_READ_U8                  0x0000003a
#define SQ_DS_READ_I16                 0x0000003b
#define SQ_DS_READ_U16                 0x0000003c
#define SQ_DS_SWIZZLE_B32              0x0000003d
#define SQ_DS_PERMUTE_B32              0x0000003e
#define SQ_DS_BPERMUTE_B32             0x0000003f
#define SQ_DS_ADD_U64                  0x00000040
#define SQ_DS_SUB_U64                  0x00000041
#define SQ_DS_RSUB_U64                 0x00000042
#define SQ_DS_INC_U64                  0x00000043
#define SQ_DS_DEC_U64                  0x00000044
#define SQ_DS_MIN_I64                  0x00000045
#define SQ_DS_MAX_I64                  0x00000046
#define SQ_DS_MIN_U64                  0x00000047
#define SQ_DS_MAX_U64                  0x00000048
#define SQ_DS_AND_B64                  0x00000049
#define SQ_DS_OR_B64                   0x0000004a
#define SQ_DS_XOR_B64                  0x0000004b
#define SQ_DS_MSKOR_B64                0x0000004c
#define SQ_DS_WRITE_B64                0x0000004d
#define SQ_DS_WRITE2_B64               0x0000004e
#define SQ_DS_WRITE2ST64_B64           0x0000004f
#define SQ_DS_CMPST_B64                0x00000050
#define SQ_DS_CMPST_F64                0x00000051
#define SQ_DS_MIN_F64                  0x00000052
#define SQ_DS_MAX_F64                  0x00000053
#define SQ_DS_ADD_RTN_U64              0x00000060
#define SQ_DS_SUB_RTN_U64              0x00000061
#define SQ_DS_RSUB_RTN_U64             0x00000062
#define SQ_DS_INC_RTN_U64              0x00000063
#define SQ_DS_DEC_RTN_U64              0x00000064
#define SQ_DS_MIN_RTN_I64              0x00000065
#define SQ_DS_MAX_RTN_I64              0x00000066
#define SQ_DS_MIN_RTN_U64              0x00000067
#define SQ_DS_MAX_RTN_U64              0x00000068
#define SQ_DS_AND_RTN_B64              0x00000069
#define SQ_DS_OR_RTN_B64               0x0000006a
#define SQ_DS_XOR_RTN_B64              0x0000006b
#define SQ_DS_MSKOR_RTN_B64            0x0000006c
#define SQ_DS_WRXCHG_RTN_B64           0x0000006d
#define SQ_DS_WRXCHG2_RTN_B64          0x0000006e
#define SQ_DS_WRXCHG2ST64_RTN_B64      0x0000006f
#define SQ_DS_CMPST_RTN_B64            0x00000070
#define SQ_DS_CMPST_RTN_F64            0x00000071
#define SQ_DS_MIN_RTN_F64              0x00000072
#define SQ_DS_MAX_RTN_F64              0x00000073
#define SQ_DS_READ_B64                 0x00000076
#define SQ_DS_READ2_B64                0x00000077
#define SQ_DS_READ2ST64_B64            0x00000078
#define SQ_DS_CONDXCHG32_RTN_B64       0x0000007e
#define SQ_DS_ADD_SRC2_U32             0x00000080
#define SQ_DS_SUB_SRC2_U32             0x00000081
#define SQ_DS_RSUB_SRC2_U32            0x00000082
#define SQ_DS_INC_SRC2_U32             0x00000083
#define SQ_DS_DEC_SRC2_U32             0x00000084
#define SQ_DS_MIN_SRC2_I32             0x00000085
#define SQ_DS_MAX_SRC2_I32             0x00000086
#define SQ_DS_MIN_SRC2_U32             0x00000087
#define SQ_DS_MAX_SRC2_U32             0x00000088
#define SQ_DS_AND_SRC2_B32             0x00000089
#define SQ_DS_OR_SRC2_B32              0x0000008a
#define SQ_DS_XOR_SRC2_B32             0x0000008b
#define SQ_DS_WRITE_SRC2_B32           0x0000008d
#define SQ_DS_MIN_SRC2_F32             0x00000092
#define SQ_DS_MAX_SRC2_F32             0x00000093
#define SQ_DS_ADD_SRC2_F32             0x00000095
#define SQ_DS_GWS_SEMA_RELEASE_ALL     0x00000098
#define SQ_DS_GWS_INIT                 0x00000099
#define SQ_DS_GWS_SEMA_V               0x0000009a
#define SQ_DS_GWS_SEMA_BR              0x0000009b
#define SQ_DS_GWS_SEMA_P               0x0000009c
#define SQ_DS_GWS_BARRIER              0x0000009d
#define SQ_DS_READ_ADDTID_B32          0x000000b6
#define SQ_DS_CONSUME                  0x000000bd
#define SQ_DS_APPEND                   0x000000be
#define SQ_DS_ORDERED_COUNT            0x000000bf
#define SQ_DS_ADD_SRC2_U64             0x000000c0
#define SQ_DS_SUB_SRC2_U64             0x000000c1
#define SQ_DS_RSUB_SRC2_U64            0x000000c2
#define SQ_DS_INC_SRC2_U64             0x000000c3
#define SQ_DS_DEC_SRC2_U64             0x000000c4
#define SQ_DS_MIN_SRC2_I64             0x000000c5
#define SQ_DS_MAX_SRC2_I64             0x000000c6
#define SQ_DS_MIN_SRC2_U64             0x000000c7
#define SQ_DS_MAX_SRC2_U64             0x000000c8
#define SQ_DS_AND_SRC2_B64             0x000000c9
#define SQ_DS_OR_SRC2_B64              0x000000ca
#define SQ_DS_XOR_SRC2_B64             0x000000cb
#define SQ_DS_WRITE_SRC2_B64           0x000000cd
#define SQ_DS_MIN_SRC2_F64             0x000000d2
#define SQ_DS_MAX_SRC2_F64             0x000000d3
#define SQ_DS_WRITE_B96                0x000000de
#define SQ_DS_WRITE_B128               0x000000df
#define SQ_DS_CONDXCHG32_RTN_B128      0x000000fd
#define SQ_DS_READ_B96                 0x000000fe
#define SQ_DS_READ_B128                0x000000ff

/*
 * VALUE_SQ_OP_SMEM value
 */

#define SQ_S_LOAD_DWORD                0x00000000
#define SQ_S_LOAD_DWORDX2              0x00000001
#define SQ_S_LOAD_DWORDX4              0x00000002
#define SQ_S_LOAD_DWORDX8              0x00000003
#define SQ_S_LOAD_DWORDX16             0x00000004
#define SQ_S_SCRATCH_LOAD_DWORD        0x00000005
#define SQ_S_SCRATCH_LOAD_DWORDX2      0x00000006
#define SQ_S_SCRATCH_LOAD_DWORDX4      0x00000007
#define SQ_S_BUFFER_LOAD_DWORD         0x00000008
#define SQ_S_BUFFER_LOAD_DWORDX2       0x00000009
#define SQ_S_BUFFER_LOAD_DWORDX4       0x0000000a
#define SQ_S_BUFFER_LOAD_DWORDX8       0x0000000b
#define SQ_S_BUFFER_LOAD_DWORDX16      0x0000000c
#define SQ_S_STORE_DWORD               0x00000010
#define SQ_S_STORE_DWORDX2             0x00000011
#define SQ_S_STORE_DWORDX4             0x00000012
#define SQ_S_SCRATCH_STORE_DWORD       0x00000015
#define SQ_S_SCRATCH_STORE_DWORDX2     0x00000016
#define SQ_S_SCRATCH_STORE_DWORDX4     0x00000017
#define SQ_S_BUFFER_STORE_DWORD        0x00000018
#define SQ_S_BUFFER_STORE_DWORDX2      0x00000019
#define SQ_S_BUFFER_STORE_DWORDX4      0x0000001a
#define SQ_S_DCACHE_INV                0x00000020
#define SQ_S_DCACHE_WB                 0x00000021
#define SQ_S_DCACHE_INV_VOL            0x00000022
#define SQ_S_DCACHE_WB_VOL             0x00000023
#define SQ_S_MEMTIME                   0x00000024
#define SQ_S_MEMREALTIME               0x00000025
#define SQ_S_ATC_PROBE                 0x00000026
#define SQ_S_ATC_PROBE_BUFFER          0x00000027
#define SQ_S_BUFFER_ATOMIC_SWAP        0x00000040
#define SQ_S_BUFFER_ATOMIC_CMPSWAP     0x00000041
#define SQ_S_BUFFER_ATOMIC_ADD         0x00000042
#define SQ_S_BUFFER_ATOMIC_SUB         0x00000043
#define SQ_S_BUFFER_ATOMIC_SMIN        0x00000044
#define SQ_S_BUFFER_ATOMIC_UMIN        0x00000045
#define SQ_S_BUFFER_ATOMIC_SMAX        0x00000046
#define SQ_S_BUFFER_ATOMIC_UMAX        0x00000047
#define SQ_S_BUFFER_ATOMIC_AND         0x00000048
#define SQ_S_BUFFER_ATOMIC_OR          0x00000049
#define SQ_S_BUFFER_ATOMIC_XOR         0x0000004a
#define SQ_S_BUFFER_ATOMIC_INC         0x0000004b
#define SQ_S_BUFFER_ATOMIC_DEC         0x0000004c
#define SQ_S_BUFFER_ATOMIC_SWAP_X2     0x00000060
#define SQ_S_BUFFER_ATOMIC_CMPSWAP_X2  0x00000061
#define SQ_S_BUFFER_ATOMIC_ADD_X2      0x00000062
#define SQ_S_BUFFER_ATOMIC_SUB_X2      0x00000063
#define SQ_S_BUFFER_ATOMIC_SMIN_X2     0x00000064
#define SQ_S_BUFFER_ATOMIC_UMIN_X2     0x00000065
#define SQ_S_BUFFER_ATOMIC_SMAX_X2     0x00000066
#define SQ_S_BUFFER_ATOMIC_UMAX_X2     0x00000067
#define SQ_S_BUFFER_ATOMIC_AND_X2      0x00000068
#define SQ_S_BUFFER_ATOMIC_OR_X2       0x00000069
#define SQ_S_BUFFER_ATOMIC_XOR_X2      0x0000006a
#define SQ_S_BUFFER_ATOMIC_INC_X2      0x0000006b
#define SQ_S_BUFFER_ATOMIC_DEC_X2      0x0000006c
#define SQ_S_ATOMIC_SWAP               0x00000080
#define SQ_S_ATOMIC_CMPSWAP            0x00000081
#define SQ_S_ATOMIC_ADD                0x00000082
#define SQ_S_ATOMIC_SUB                0x00000083
#define SQ_S_ATOMIC_SMIN               0x00000084
#define SQ_S_ATOMIC_UMIN               0x00000085
#define SQ_S_ATOMIC_SMAX               0x00000086
#define SQ_S_ATOMIC_UMAX               0x00000087
#define SQ_S_ATOMIC_AND                0x00000088
#define SQ_S_ATOMIC_OR                 0x00000089
#define SQ_S_ATOMIC_XOR                0x0000008a
#define SQ_S_ATOMIC_INC                0x0000008b
#define SQ_S_ATOMIC_DEC                0x0000008c
#define SQ_S_ATOMIC_SWAP_X2            0x000000a0
#define SQ_S_ATOMIC_CMPSWAP_X2         0x000000a1
#define SQ_S_ATOMIC_ADD_X2             0x000000a2
#define SQ_S_ATOMIC_SUB_X2             0x000000a3
#define SQ_S_ATOMIC_SMIN_X2            0x000000a4
#define SQ_S_ATOMIC_UMIN_X2            0x000000a5
#define SQ_S_ATOMIC_SMAX_X2            0x000000a6
#define SQ_S_ATOMIC_UMAX_X2            0x000000a7
#define SQ_S_ATOMIC_AND_X2             0x000000a8
#define SQ_S_ATOMIC_OR_X2              0x000000a9
#define SQ_S_ATOMIC_XOR_X2             0x000000aa
#define SQ_S_ATOMIC_INC_X2             0x000000ab
#define SQ_S_ATOMIC_DEC_X2             0x000000ac

/*
 * VALUE_SQ_OP_VOP2 value
 */

#define SQ_V_CNDMASK_B32               0x00000000
#define SQ_V_ADD_F32                   0x00000001
#define SQ_V_SUB_F32                   0x00000002
#define SQ_V_SUBREV_F32                0x00000003
#define SQ_V_MUL_LEGACY_F32            0x00000004
#define SQ_V_MUL_F32                   0x00000005
#define SQ_V_MUL_I32_I24               0x00000006
#define SQ_V_MUL_HI_I32_I24            0x00000007
#define SQ_V_MUL_U32_U24               0x00000008
#define SQ_V_MUL_HI_U32_U24            0x00000009
#define SQ_V_MIN_F32                   0x0000000a
#define SQ_V_MAX_F32                   0x0000000b
#define SQ_V_MIN_I32                   0x0000000c
#define SQ_V_MAX_I32                   0x0000000d
#define SQ_V_MIN_U32                   0x0000000e
#define SQ_V_MAX_U32                   0x0000000f
#define SQ_V_LSHRREV_B32               0x00000010
#define SQ_V_ASHRREV_I32               0x00000011
#define SQ_V_LSHLREV_B32               0x00000012
#define SQ_V_AND_B32                   0x00000013
#define SQ_V_OR_B32                    0x00000014
#define SQ_V_XOR_B32                   0x00000015
#define SQ_V_MAC_F32                   0x00000016
#define SQ_V_MADMK_F32                 0x00000017
#define SQ_V_MADAK_F32                 0x00000018
#define SQ_V_ADD_CO_U32                0x00000019
#define SQ_V_SUB_CO_U32                0x0000001a
#define SQ_V_SUBREV_CO_U32             0x0000001b
#define SQ_V_ADDC_CO_U32               0x0000001c
#define SQ_V_SUBB_CO_U32               0x0000001d
#define SQ_V_SUBBREV_CO_U32            0x0000001e
#define SQ_V_ADD_F16                   0x0000001f
#define SQ_V_SUB_F16                   0x00000020
#define SQ_V_SUBREV_F16                0x00000021
#define SQ_V_MUL_F16                   0x00000022
#define SQ_V_MAC_F16                   0x00000023
#define SQ_V_MADMK_F16                 0x00000024
#define SQ_V_MADAK_F16                 0x00000025
#define SQ_V_ADD_U16                   0x00000026
#define SQ_V_SUB_U16                   0x00000027
#define SQ_V_SUBREV_U16                0x00000028
#define SQ_V_MUL_LO_U16                0x00000029
#define SQ_V_LSHLREV_B16               0x0000002a
#define SQ_V_LSHRREV_B16               0x0000002b
#define SQ_V_ASHRREV_I16               0x0000002c
#define SQ_V_MAX_F16                   0x0000002d
#define SQ_V_MIN_F16                   0x0000002e
#define SQ_V_MAX_U16                   0x0000002f
#define SQ_V_MAX_I16                   0x00000030
#define SQ_V_MIN_U16                   0x00000031
#define SQ_V_MIN_I16                   0x00000032
#define SQ_V_LDEXP_F16                 0x00000033
#define SQ_V_ADD_U32                   0x00000034
#define SQ_V_SUB_U32                   0x00000035
#define SQ_V_SUBREV_U32                0x00000036

/*
 * VALUE_SQ_SYSMSG_OP value
 */

#define SQ_SYSMSG_OP_ECC_ERR_INTERRUPT 0x00000001
#define SQ_SYSMSG_OP_REG_RD            0x00000002
#define SQ_SYSMSG_OP_HOST_TRAP_ACK     0x00000003
#define SQ_SYSMSG_OP_TTRACE_PC         0x00000004
#define SQ_SYSMSG_OP_ILLEGAL_INST_INTERRUPT 0x00000005
#define SQ_SYSMSG_OP_MEMVIOL_INTERRUPT 0x00000006

/*
 * VALUE_SQ_SSRC_SPECIAL_VCCZ value
 */

#define SQ_SRC_VCCZ                    0x000000fb

/*
 * VALUE_SQ_CHAN value
 */

#define SQ_CHAN_X                      0x00000000
#define SQ_CHAN_Y                      0x00000001
#define SQ_CHAN_Z                      0x00000002
#define SQ_CHAN_W                      0x00000003

/*
 * VALUE_SQ_OP_SOPK value
 */

#define SQ_S_MOVK_I32                  0x00000000
#define SQ_S_CMOVK_I32                 0x00000001
#define SQ_S_CMPK_EQ_I32               0x00000002
#define SQ_S_CMPK_LG_I32               0x00000003
#define SQ_S_CMPK_GT_I32               0x00000004
#define SQ_S_CMPK_GE_I32               0x00000005
#define SQ_S_CMPK_LT_I32               0x00000006
#define SQ_S_CMPK_LE_I32               0x00000007
#define SQ_S_CMPK_EQ_U32               0x00000008
#define SQ_S_CMPK_LG_U32               0x00000009
#define SQ_S_CMPK_GT_U32               0x0000000a
#define SQ_S_CMPK_GE_U32               0x0000000b
#define SQ_S_CMPK_LT_U32               0x0000000c
#define SQ_S_CMPK_LE_U32               0x0000000d
#define SQ_S_ADDK_I32                  0x0000000e
#define SQ_S_MULK_I32                  0x0000000f
#define SQ_S_CBRANCH_I_FORK            0x00000010
#define SQ_S_GETREG_B32                0x00000011
#define SQ_S_SETREG_B32                0x00000012
#define SQ_S_GETREG_REGRD_B32          0x00000013
#define SQ_S_SETREG_IMM32_B32          0x00000014
#define SQ_S_CALL_B64                  0x00000015

/*
 * VALUE_SQ_DPP_CTRL_L_1_15 value
 */

#define SQ_L1                          0x00000001
#define SQ_L2                          0x00000002
#define SQ_L3                          0x00000003
#define SQ_L4                          0x00000004
#define SQ_L5                          0x00000005
#define SQ_L6                          0x00000006
#define SQ_L7                          0x00000007
#define SQ_L8                          0x00000008
#define SQ_L9                          0x00000009
#define SQ_L10                         0x0000000a
#define SQ_L11                         0x0000000b
#define SQ_L12                         0x0000000c
#define SQ_L13                         0x0000000d
#define SQ_L14                         0x0000000e
#define SQ_L15                         0x0000000f

/*
 * VALUE_SQ_SGPR value
 */

#define SQ_SGPR0                       0x00000000

/*
 * VALUE_SQ_OP_VOP3P value
 */

#define SQ_V_PK_MAD_I16                0x00000000
#define SQ_V_PK_MUL_LO_U16             0x00000001
#define SQ_V_PK_ADD_I16                0x00000002
#define SQ_V_PK_SUB_I16                0x00000003
#define SQ_V_PK_LSHLREV_B16            0x00000004
#define SQ_V_PK_LSHRREV_B16            0x00000005
#define SQ_V_PK_ASHRREV_I16            0x00000006
#define SQ_V_PK_MAX_I16                0x00000007
#define SQ_V_PK_MIN_I16                0x00000008
#define SQ_V_PK_MAD_U16                0x00000009
#define SQ_V_PK_ADD_U16                0x0000000a
#define SQ_V_PK_SUB_U16                0x0000000b
#define SQ_V_PK_MAX_U16                0x0000000c
#define SQ_V_PK_MIN_U16                0x0000000d
#define SQ_V_PK_MAD_F16                0x0000000e
#define SQ_V_PK_ADD_F16                0x0000000f
#define SQ_V_PK_MUL_F16                0x00000010
#define SQ_V_PK_MIN_F16                0x00000011
#define SQ_V_PK_MAX_F16                0x00000012
#define SQ_V_MAD_MIX_F32               0x00000020
#define SQ_V_MAD_MIXLO_F16             0x00000021
#define SQ_V_MAD_MIXHI_F16             0x00000022

/*
 * VALUE_SQ_OP_VINTRP value
 */

#define SQ_V_INTERP_P1_F32             0x00000000
#define SQ_V_INTERP_P2_F32             0x00000001
#define SQ_V_INTERP_MOV_F32            0x00000002

/*
 * VALUE_SQ_DPP_CTRL_R_1_15 value
 */

#define SQ_R1                          0x00000001
#define SQ_R2                          0x00000002
#define SQ_R3                          0x00000003
#define SQ_R4                          0x00000004
#define SQ_R5                          0x00000005
#define SQ_R6                          0x00000006
#define SQ_R7                          0x00000007
#define SQ_R8                          0x00000008
#define SQ_R9                          0x00000009
#define SQ_R10                         0x0000000a
#define SQ_R11                         0x0000000b
#define SQ_R12                         0x0000000c
#define SQ_R13                         0x0000000d
#define SQ_R14                         0x0000000e
#define SQ_R15                         0x0000000f

/*
 * VALUE_SQ_OP_SOP2 value
 */

#define SQ_S_ADD_U32                   0x00000000
#define SQ_S_SUB_U32                   0x00000001
#define SQ_S_ADD_I32                   0x00000002
#define SQ_S_SUB_I32                   0x00000003
#define SQ_S_ADDC_U32                  0x00000004
#define SQ_S_SUBB_U32                  0x00000005
#define SQ_S_MIN_I32                   0x00000006
#define SQ_S_MIN_U32                   0x00000007
#define SQ_S_MAX_I32                   0x00000008
#define SQ_S_MAX_U32                   0x00000009
#define SQ_S_CSELECT_B32               0x0000000a
#define SQ_S_CSELECT_B64               0x0000000b
#define SQ_S_AND_B32                   0x0000000c
#define SQ_S_AND_B64                   0x0000000d
#define SQ_S_OR_B32                    0x0000000e
#define SQ_S_OR_B64                    0x0000000f
#define SQ_S_XOR_B32                   0x00000010
#define SQ_S_XOR_B64                   0x00000011
#define SQ_S_ANDN2_B32                 0x00000012
#define SQ_S_ANDN2_B64                 0x00000013
#define SQ_S_ORN2_B32                  0x00000014
#define SQ_S_ORN2_B64                  0x00000015
#define SQ_S_NAND_B32                  0x00000016
#define SQ_S_NAND_B64                  0x00000017
#define SQ_S_NOR_B32                   0x00000018
#define SQ_S_NOR_B64                   0x00000019
#define SQ_S_XNOR_B32                  0x0000001a
#define SQ_S_XNOR_B64                  0x0000001b
#define SQ_S_LSHL_B32                  0x0000001c
#define SQ_S_LSHL_B64                  0x0000001d
#define SQ_S_LSHR_B32                  0x0000001e
#define SQ_S_LSHR_B64                  0x0000001f
#define SQ_S_ASHR_I32                  0x00000020
#define SQ_S_ASHR_I64                  0x00000021
#define SQ_S_BFM_B32                   0x00000022
#define SQ_S_BFM_B64                   0x00000023
#define SQ_S_MUL_I32                   0x00000024
#define SQ_S_BFE_U32                   0x00000025
#define SQ_S_BFE_I32                   0x00000026
#define SQ_S_BFE_U64                   0x00000027
#define SQ_S_BFE_I64                   0x00000028
#define SQ_S_CBRANCH_G_FORK            0x00000029
#define SQ_S_ABSDIFF_I32               0x0000002a
#define SQ_S_RFE_RESTORE_B64           0x0000002b
#define SQ_S_MUL_HI_U32                0x0000002c
#define SQ_S_MUL_HI_I32                0x0000002d
#define SQ_S_LSHL1_ADD_U32             0x0000002e
#define SQ_S_LSHL2_ADD_U32             0x0000002f
#define SQ_S_LSHL3_ADD_U32             0x00000030
#define SQ_S_LSHL4_ADD_U32             0x00000031
#define SQ_S_PACK_LL_B32_B16           0x00000032
#define SQ_S_PACK_LH_B32_B16           0x00000033
#define SQ_S_PACK_HH_B32_B16           0x00000034

/*
 * VALUE_SQ_SEG value
 */

#define SQ_FLAT                        0x00000000
#define SQ_SCRATCH                     0x00000001
#define SQ_GLOBAL                      0x00000002

/*
 * VALUE_SQ_SDST_EXEC value
 */

#define SQ_EXEC_LO                     0x0000007e
#define SQ_EXEC_HI                     0x0000007f

/*
 * VALUE_SQ_SSRC_SPECIAL_NOLIT value
 */

#define SQ_SRC_64_INT                  0x000000c0
#define SQ_SRC_M_1_INT                 0x000000c1
#define SQ_SRC_M_2_INT                 0x000000c2
#define SQ_SRC_M_3_INT                 0x000000c3
#define SQ_SRC_M_4_INT                 0x000000c4
#define SQ_SRC_M_5_INT                 0x000000c5
#define SQ_SRC_M_6_INT                 0x000000c6
#define SQ_SRC_M_7_INT                 0x000000c7
#define SQ_SRC_M_8_INT                 0x000000c8
#define SQ_SRC_M_9_INT                 0x000000c9
#define SQ_SRC_M_10_INT                0x000000ca
#define SQ_SRC_M_11_INT                0x000000cb
#define SQ_SRC_M_12_INT                0x000000cc
#define SQ_SRC_M_13_INT                0x000000cd
#define SQ_SRC_M_14_INT                0x000000ce
#define SQ_SRC_M_15_INT                0x000000cf
#define SQ_SRC_M_16_INT                0x000000d0
#define SQ_SRC_0_5                     0x000000f0
#define SQ_SRC_M_0_5                   0x000000f1
#define SQ_SRC_1                       0x000000f2
#define SQ_SRC_M_1                     0x000000f3
#define SQ_SRC_2                       0x000000f4
#define SQ_SRC_M_2                     0x000000f5
#define SQ_SRC_4                       0x000000f6
#define SQ_SRC_M_4                     0x000000f7
#define SQ_SRC_INV_2PI                 0x000000f8

/*
 * VALUE_SQ_VCC_LOHI value
 */

#define SQ_VCC_LO                      0x0000006a
#define SQ_VCC_HI                      0x0000006b

/*
 * VALUE_SQ_TGT value
 */

#define SQ_EXP_MRT0                    0x00000000
#define SQ_EXP_MRTZ                    0x00000008
#define SQ_EXP_NULL                    0x00000009
#define SQ_EXP_POS0                    0x0000000c
#define SQ_EXP_PARAM0                  0x00000020

/*
 * VALUE_SQ_OP_SOPP value
 */

#define SQ_S_NOP                       0x00000000
#define SQ_S_ENDPGM                    0x00000001
#define SQ_S_BRANCH                    0x00000002
#define SQ_S_WAKEUP                    0x00000003
#define SQ_S_CBRANCH_SCC0              0x00000004
#define SQ_S_CBRANCH_SCC1              0x00000005
#define SQ_S_CBRANCH_VCCZ              0x00000006
#define SQ_S_CBRANCH_VCCNZ             0x00000007
#define SQ_S_CBRANCH_EXECZ             0x00000008
#define SQ_S_CBRANCH_EXECNZ            0x00000009
#define SQ_S_BARRIER                   0x0000000a
#define SQ_S_SETKILL                   0x0000000b
#define SQ_S_WAITCNT                   0x0000000c
#define SQ_S_SETHALT                   0x0000000d
#define SQ_S_SLEEP                     0x0000000e
#define SQ_S_SETPRIO                   0x0000000f
#define SQ_S_SENDMSG                   0x00000010
#define SQ_S_SENDMSGHALT               0x00000011
#define SQ_S_TRAP                      0x00000012
#define SQ_S_ICACHE_INV                0x00000013
#define SQ_S_INCPERFLEVEL              0x00000014
#define SQ_S_DECPERFLEVEL              0x00000015
#define SQ_S_TTRACEDATA                0x00000016
#define SQ_S_CBRANCH_CDBGSYS           0x00000017
#define SQ_S_CBRANCH_CDBGUSER          0x00000018
#define SQ_S_CBRANCH_CDBGSYS_OR_USER   0x00000019
#define SQ_S_CBRANCH_CDBGSYS_AND_USER  0x0000001a
#define SQ_S_ENDPGM_SAVED              0x0000001b
#define SQ_S_SET_GPR_IDX_OFF           0x0000001c
#define SQ_S_SET_GPR_IDX_MODE          0x0000001d
#define SQ_S_ENDPGM_ORDERED_PS_DONE    0x0000001e

/*
 * VALUE_SQ_OP_EXP value
 */

#define SQ_EXP                         0x00000000

/*
 * VALUE_SQ_SSRC_SPECIAL_POPS_EXITING_WAVE_ID value
 */

#define SQ_SRC_POPS_EXITING_WAVE_ID    0x000000ef

/*
 * VALUE_SQ_XNACK_MASK_LOHI value
 */

#define SQ_XNACK_MASK_LO               0x00000068
#define SQ_XNACK_MASK_HI               0x00000069

/*
 * VALUE_SQ_OMOD value
 */

#define SQ_OMOD_OFF                    0x00000000
#define SQ_OMOD_M2                     0x00000001
#define SQ_OMOD_M4                     0x00000002
#define SQ_OMOD_D2                     0x00000003

/*
 * VALUE_SQ_SSRC_SPECIAL_EXECZ value
 */

#define SQ_SRC_EXECZ                   0x000000fc

/*
 * VALUE_SQ_COMPI value
 */

#define SQ_F                           0x00000000
#define SQ_LT                          0x00000001
#define SQ_EQ                          0x00000002
#define SQ_LE                          0x00000003
#define SQ_GT                          0x00000004
#define SQ_NE                          0x00000005
#define SQ_GE                          0x00000006
#define SQ_T                           0x00000007

/*
 * VALUE_SQ_DPP_BOUND_CTRL value
 */

#define SQ_DPP_BOUND_OFF               0x00000000
#define SQ_DPP_BOUND_ZERO              0x00000001

/*
 * VALUE_SQ_SDST_M0 value
 */

#define SQ_M0                          0x0000007c

/*
 * VALUE_SQ_MSG value
 */

#define SQ_MSG_INTERRUPT               0x00000001
#define SQ_MSG_GS                      0x00000002
#define SQ_MSG_GS_DONE                 0x00000003
#define SQ_MSG_SAVEWAVE                0x00000004
#define SQ_MSG_STALL_WAVE_GEN          0x00000005
#define SQ_MSG_HALT_WAVES              0x00000006
#define SQ_MSG_ORDERED_PS_DONE         0x00000007
#define SQ_MSG_EARLY_PRIM_DEALLOC      0x00000008
#define SQ_MSG_GS_ALLOC_REQ            0x00000009
#define SQ_MSG_SYSMSG                  0x0000000f

/*
 * VALUE_SQ_PARAM value
 */

#define SQ_PARAM_P10                   0x00000000
#define SQ_PARAM_P20                   0x00000001
#define SQ_PARAM_P0                    0x00000002

/*
 * VALUE_SQ_OPU_VOP3 value
 */

#define SQ_V_OPC_OFFSET                0x00000000
#define SQ_V_OP2_OFFSET                0x00000100
#define SQ_V_OP1_OFFSET                0x00000140
#define SQ_V_INTRP_OFFSET              0x00000270
#define SQ_V_OP3P_OFFSET               0x00000380

/*
 * VALUE_SQ_SSRC_SPECIAL_SDWA value
 */

#define SQ_SRC_SDWA                    0x000000f9

/*
 * VALUE_SQ_SSRC_SPECIAL_APERTURE value
 */

#define SQ_SRC_SHARED_BASE             0x000000eb
#define SQ_SRC_SHARED_LIMIT            0x000000ec
#define SQ_SRC_PRIVATE_BASE            0x000000ed
#define SQ_SRC_PRIVATE_LIMIT           0x000000ee

/*
 * VALUE_SQ_COMPF value
 */

#define SQ_F                           0x00000000
#define SQ_LT                          0x00000001
#define SQ_EQ                          0x00000002
#define SQ_LE                          0x00000003
#define SQ_GT                          0x00000004
#define SQ_LG                          0x00000005
#define SQ_GE                          0x00000006
#define SQ_O                           0x00000007
#define SQ_U                           0x00000008
#define SQ_NGE                         0x00000009
#define SQ_NLG                         0x0000000a
#define SQ_NGT                         0x0000000b
#define SQ_NLE                         0x0000000c
#define SQ_NEQ                         0x0000000d
#define SQ_NLT                         0x0000000e
#define SQ_TRU                         0x0000000f

/*
 * VALUE_SQ_SDWA_UNUSED value
 */

#define SQ_SDWA_UNUSED_PAD             0x00000000
#define SQ_SDWA_UNUSED_SEXT            0x00000001
#define SQ_SDWA_UNUSED_PRESERVE        0x00000002

/*
 * VALUE_SQ_SSRC_SPECIAL_SCC value
 */

#define SQ_SRC_SCC                     0x000000fd

/*
 * VALUE_SQ_OP_VOPC value
 */

#define SQ_V_CMP_CLASS_F32             0x00000010
#define SQ_V_CMPX_CLASS_F32            0x00000011
#define SQ_V_CMP_CLASS_F64             0x00000012
#define SQ_V_CMPX_CLASS_F64            0x00000013
#define SQ_V_CMP_CLASS_F16             0x00000014
#define SQ_V_CMPX_CLASS_F16            0x00000015
#define SQ_V_CMP_F_F16                 0x00000020
#define SQ_V_CMP_LT_F16                0x00000021
#define SQ_V_CMP_EQ_F16                0x00000022
#define SQ_V_CMP_LE_F16                0x00000023
#define SQ_V_CMP_GT_F16                0x00000024
#define SQ_V_CMP_LG_F16                0x00000025
#define SQ_V_CMP_GE_F16                0x00000026
#define SQ_V_CMP_O_F16                 0x00000027
#define SQ_V_CMP_U_F16                 0x00000028
#define SQ_V_CMP_NGE_F16               0x00000029
#define SQ_V_CMP_NLG_F16               0x0000002a
#define SQ_V_CMP_NGT_F16               0x0000002b
#define SQ_V_CMP_NLE_F16               0x0000002c
#define SQ_V_CMP_NEQ_F16               0x0000002d
#define SQ_V_CMP_NLT_F16               0x0000002e
#define SQ_V_CMP_TRU_F16               0x0000002f
#define SQ_V_CMPX_F_F16                0x00000030
#define SQ_V_CMPX_LT_F16               0x00000031
#define SQ_V_CMPX_EQ_F16               0x00000032
#define SQ_V_CMPX_LE_F16               0x00000033
#define SQ_V_CMPX_GT_F16               0x00000034
#define SQ_V_CMPX_LG_F16               0x00000035
#define SQ_V_CMPX_GE_F16               0x00000036
#define SQ_V_CMPX_O_F16                0x00000037
#define SQ_V_CMPX_U_F16                0x00000038
#define SQ_V_CMPX_NGE_F16              0x00000039
#define SQ_V_CMPX_NLG_F16              0x0000003a
#define SQ_V_CMPX_NGT_F16              0x0000003b
#define SQ_V_CMPX_NLE_F16              0x0000003c
#define SQ_V_CMPX_NEQ_F16              0x0000003d
#define SQ_V_CMPX_NLT_F16              0x0000003e
#define SQ_V_CMPX_TRU_F16              0x0000003f
#define SQ_V_CMP_F_F32                 0x00000040
#define SQ_V_CMP_LT_F32                0x00000041
#define SQ_V_CMP_EQ_F32                0x00000042
#define SQ_V_CMP_LE_F32                0x00000043
#define SQ_V_CMP_GT_F32                0x00000044
#define SQ_V_CMP_LG_F32                0x00000045
#define SQ_V_CMP_GE_F32                0x00000046
#define SQ_V_CMP_O_F32                 0x00000047
#define SQ_V_CMP_U_F32                 0x00000048
#define SQ_V_CMP_NGE_F32               0x00000049
#define SQ_V_CMP_NLG_F32               0x0000004a
#define SQ_V_CMP_NGT_F32               0x0000004b
#define SQ_V_CMP_NLE_F32               0x0000004c
#define SQ_V_CMP_NEQ_F32               0x0000004d
#define SQ_V_CMP_NLT_F32               0x0000004e
#define SQ_V_CMP_TRU_F32               0x0000004f
#define SQ_V_CMPX_F_F32                0x00000050
#define SQ_V_CMPX_LT_F32               0x00000051
#define SQ_V_CMPX_EQ_F32               0x00000052
#define SQ_V_CMPX_LE_F32               0x00000053
#define SQ_V_CMPX_GT_F32               0x00000054
#define SQ_V_CMPX_LG_F32               0x00000055
#define SQ_V_CMPX_GE_F32               0x00000056
#define SQ_V_CMPX_O_F32                0x00000057
#define SQ_V_CMPX_U_F32                0x00000058
#define SQ_V_CMPX_NGE_F32              0x00000059
#define SQ_V_CMPX_NLG_F32              0x0000005a
#define SQ_V_CMPX_NGT_F32              0x0000005b
#define SQ_V_CMPX_NLE_F32              0x0000005c
#define SQ_V_CMPX_NEQ_F32              0x0000005d
#define SQ_V_CMPX_NLT_F32              0x0000005e
#define SQ_V_CMPX_TRU_F32              0x0000005f
#define SQ_V_CMP_F_F64                 0x00000060
#define SQ_V_CMP_LT_F64                0x00000061
#define SQ_V_CMP_EQ_F64                0x00000062
#define SQ_V_CMP_LE_F64                0x00000063
#define SQ_V_CMP_GT_F64                0x00000064
#define SQ_V_CMP_LG_F64                0x00000065
#define SQ_V_CMP_GE_F64                0x00000066
#define SQ_V_CMP_O_F64                 0x00000067
#define SQ_V_CMP_U_F64                 0x00000068
#define SQ_V_CMP_NGE_F64               0x00000069
#define SQ_V_CMP_NLG_F64               0x0000006a
#define SQ_V_CMP_NGT_F64               0x0000006b
#define SQ_V_CMP_NLE_F64               0x0000006c
#define SQ_V_CMP_NEQ_F64               0x0000006d
#define SQ_V_CMP_NLT_F64               0x0000006e
#define SQ_V_CMP_TRU_F64               0x0000006f
#define SQ_V_CMPX_F_F64                0x00000070
#define SQ_V_CMPX_LT_F64               0x00000071
#define SQ_V_CMPX_EQ_F64               0x00000072
#define SQ_V_CMPX_LE_F64               0x00000073
#define SQ_V_CMPX_GT_F64               0x00000074
#define SQ_V_CMPX_LG_F64               0x00000075
#define SQ_V_CMPX_GE_F64               0x00000076
#define SQ_V_CMPX_O_F64                0x00000077
#define SQ_V_CMPX_U_F64                0x00000078
#define SQ_V_CMPX_NGE_F64              0x00000079
#define SQ_V_CMPX_NLG_F64              0x0000007a
#define SQ_V_CMPX_NGT_F64              0x0000007b
#define SQ_V_CMPX_NLE_F64              0x0000007c
#define SQ_V_CMPX_NEQ_F64              0x0000007d
#define SQ_V_CMPX_NLT_F64              0x0000007e
#define SQ_V_CMPX_TRU_F64              0x0000007f
#define SQ_V_CMP_F_I16                 0x000000a0
#define SQ_V_CMP_LT_I16                0x000000a1
#define SQ_V_CMP_EQ_I16                0x000000a2
#define SQ_V_CMP_LE_I16                0x000000a3
#define SQ_V_CMP_GT_I16                0x000000a4
#define SQ_V_CMP_NE_I16                0x000000a5
#define SQ_V_CMP_GE_I16                0x000000a6
#define SQ_V_CMP_T_I16                 0x000000a7
#define SQ_V_CMP_F_U16                 0x000000a8
#define SQ_V_CMP_LT_U16                0x000000a9
#define SQ_V_CMP_EQ_U16                0x000000aa
#define SQ_V_CMP_LE_U16                0x000000ab
#define SQ_V_CMP_GT_U16                0x000000ac
#define SQ_V_CMP_NE_U16                0x000000ad
#define SQ_V_CMP_GE_U16                0x000000ae
#define SQ_V_CMP_T_U16                 0x000000af
#define SQ_V_CMPX_F_I16                0x000000b0
#define SQ_V_CMPX_LT_I16               0x000000b1
#define SQ_V_CMPX_EQ_I16               0x000000b2
#define SQ_V_CMPX_LE_I16               0x000000b3
#define SQ_V_CMPX_GT_I16               0x000000b4
#define SQ_V_CMPX_NE_I16               0x000000b5
#define SQ_V_CMPX_GE_I16               0x000000b6
#define SQ_V_CMPX_T_I16                0x000000b7
#define SQ_V_CMPX_F_U16                0x000000b8
#define SQ_V_CMPX_LT_U16               0x000000b9
#define SQ_V_CMPX_EQ_U16               0x000000ba
#define SQ_V_CMPX_LE_U16               0x000000bb
#define SQ_V_CMPX_GT_U16               0x000000bc
#define SQ_V_CMPX_NE_U16               0x000000bd
#define SQ_V_CMPX_GE_U16               0x000000be
#define SQ_V_CMPX_T_U16                0x000000bf
#define SQ_V_CMP_F_I32                 0x000000c0
#define SQ_V_CMP_LT_I32                0x000000c1
#define SQ_V_CMP_EQ_I32                0x000000c2
#define SQ_V_CMP_LE_I32                0x000000c3
#define SQ_V_CMP_GT_I32                0x000000c4
#define SQ_V_CMP_NE_I32                0x000000c5
#define SQ_V_CMP_GE_I32                0x000000c6
#define SQ_V_CMP_T_I32                 0x000000c7
#define SQ_V_CMP_F_U32                 0x000000c8
#define SQ_V_CMP_LT_U32                0x000000c9
#define SQ_V_CMP_EQ_U32                0x000000ca
#define SQ_V_CMP_LE_U32                0x000000cb
#define SQ_V_CMP_GT_U32                0x000000cc
#define SQ_V_CMP_NE_U32                0x000000cd
#define SQ_V_CMP_GE_U32                0x000000ce
#define SQ_V_CMP_T_U32                 0x000000cf
#define SQ_V_CMPX_F_I32                0x000000d0
#define SQ_V_CMPX_LT_I32               0x000000d1
#define SQ_V_CMPX_EQ_I32               0x000000d2
#define SQ_V_CMPX_LE_I32               0x000000d3
#define SQ_V_CMPX_GT_I32               0x000000d4
#define SQ_V_CMPX_NE_I32               0x000000d5
#define SQ_V_CMPX_GE_I32               0x000000d6
#define SQ_V_CMPX_T_I32                0x000000d7
#define SQ_V_CMPX_F_U32                0x000000d8
#define SQ_V_CMPX_LT_U32               0x000000d9
#define SQ_V_CMPX_EQ_U32               0x000000da
#define SQ_V_CMPX_LE_U32               0x000000db
#define SQ_V_CMPX_GT_U32               0x000000dc
#define SQ_V_CMPX_NE_U32               0x000000dd
#define SQ_V_CMPX_GE_U32               0x000000de
#define SQ_V_CMPX_T_U32                0x000000df
#define SQ_V_CMP_F_I64                 0x000000e0
#define SQ_V_CMP_LT_I64                0x000000e1
#define SQ_V_CMP_EQ_I64                0x000000e2
#define SQ_V_CMP_LE_I64                0x000000e3
#define SQ_V_CMP_GT_I64                0x000000e4
#define SQ_V_CMP_NE_I64                0x000000e5
#define SQ_V_CMP_GE_I64                0x000000e6
#define SQ_V_CMP_T_I64                 0x000000e7
#define SQ_V_CMP_F_U64                 0x000000e8
#define SQ_V_CMP_LT_U64                0x000000e9
#define SQ_V_CMP_EQ_U64                0x000000ea
#define SQ_V_CMP_LE_U64                0x000000eb
#define SQ_V_CMP_GT_U64                0x000000ec
#define SQ_V_CMP_NE_U64                0x000000ed
#define SQ_V_CMP_GE_U64                0x000000ee
#define SQ_V_CMP_T_U64                 0x000000ef
#define SQ_V_CMPX_F_I64                0x000000f0
#define SQ_V_CMPX_LT_I64               0x000000f1
#define SQ_V_CMPX_EQ_I64               0x000000f2
#define SQ_V_CMPX_LE_I64               0x000000f3
#define SQ_V_CMPX_GT_I64               0x000000f4
#define SQ_V_CMPX_NE_I64               0x000000f5
#define SQ_V_CMPX_GE_I64               0x000000f6
#define SQ_V_CMPX_T_I64                0x000000f7
#define SQ_V_CMPX_F_U64                0x000000f8
#define SQ_V_CMPX_LT_U64               0x000000f9
#define SQ_V_CMPX_EQ_U64               0x000000fa
#define SQ_V_CMPX_LE_U64               0x000000fb
#define SQ_V_CMPX_GT_U64               0x000000fc
#define SQ_V_CMPX_NE_U64               0x000000fd
#define SQ_V_CMPX_GE_U64               0x000000fe
#define SQ_V_CMPX_T_U64                0x000000ff

/*
 * VALUE_SQ_GS_OP value
 */

#define SQ_GS_OP_NOP                   0x00000000
#define SQ_GS_OP_CUT                   0x00000001
#define SQ_GS_OP_EMIT                  0x00000002
#define SQ_GS_OP_EMIT_CUT              0x00000003

/*
 * VALUE_SQ_SSRC_SPECIAL_LDS value
 */

#define SQ_SRC_LDS_DIRECT              0x000000fe

/*
 * VALUE_SQ_ATTR value
 */

#define SQ_ATTR0                       0x00000000

/*
 * VALUE_SQ_TGT_INTERNAL value
 */

#define SQ_EXP_GDS0                    0x00000018

/*
 * VALUE_SQ_OP_SOPC value
 */

#define SQ_S_CMP_EQ_I32                0x00000000
#define SQ_S_CMP_LG_I32                0x00000001
#define SQ_S_CMP_GT_I32                0x00000002
#define SQ_S_CMP_GE_I32                0x00000003
#define SQ_S_CMP_LT_I32                0x00000004
#define SQ_S_CMP_LE_I32                0x00000005
#define SQ_S_CMP_EQ_U32                0x00000006
#define SQ_S_CMP_LG_U32                0x00000007
#define SQ_S_CMP_GT_U32                0x00000008
#define SQ_S_CMP_GE_U32                0x00000009
#define SQ_S_CMP_LT_U32                0x0000000a
#define SQ_S_CMP_LE_U32                0x0000000b
#define SQ_S_BITCMP0_B32               0x0000000c
#define SQ_S_BITCMP1_B32               0x0000000d
#define SQ_S_BITCMP0_B64               0x0000000e
#define SQ_S_BITCMP1_B64               0x0000000f
#define SQ_S_SETVSKIP                  0x00000010
#define SQ_S_SET_GPR_IDX_ON            0x00000011
#define SQ_S_CMP_EQ_U64                0x00000012
#define SQ_S_CMP_LG_U64                0x00000013

/*
 * VALUE_SQ_TRAP value
 */

#define SQ_TTMP0                       0x0000006c
#define SQ_TTMP1                       0x0000006d
#define SQ_TTMP2                       0x0000006e
#define SQ_TTMP3                       0x0000006f
#define SQ_TTMP4                       0x00000070
#define SQ_TTMP5                       0x00000071
#define SQ_TTMP6                       0x00000072
#define SQ_TTMP7                       0x00000073
#define SQ_TTMP8                       0x00000074
#define SQ_TTMP9                       0x00000075
#define SQ_TTMP10                      0x00000076
#define SQ_TTMP11                      0x00000077
#define SQ_TTMP12                      0x00000078
#define SQ_TTMP13                      0x00000079
#define SQ_TTMP14                      0x0000007a
#define SQ_TTMP15                      0x0000007b

/*
 * VALUE_SQ_SRC_VGPR value
 */

#define SQ_SRC_VGPR0                   0x00000100

/*
 * VALUE_SQ_OP_MUBUF value
 */

#define SQ_BUFFER_LOAD_FORMAT_X        0x00000000
#define SQ_BUFFER_LOAD_FORMAT_XY       0x00000001
#define SQ_BUFFER_LOAD_FORMAT_XYZ      0x00000002
#define SQ_BUFFER_LOAD_FORMAT_XYZW     0x00000003
#define SQ_BUFFER_STORE_FORMAT_X       0x00000004
#define SQ_BUFFER_STORE_FORMAT_XY      0x00000005
#define SQ_BUFFER_STORE_FORMAT_XYZ     0x00000006
#define SQ_BUFFER_STORE_FORMAT_XYZW    0x00000007
#define SQ_BUFFER_LOAD_FORMAT_D16_X    0x00000008
#define SQ_BUFFER_LOAD_FORMAT_D16_XY   0x00000009
#define SQ_BUFFER_LOAD_FORMAT_D16_XYZ  0x0000000a
#define SQ_BUFFER_LOAD_FORMAT_D16_XYZW 0x0000000b
#define SQ_BUFFER_STORE_FORMAT_D16_X   0x0000000c
#define SQ_BUFFER_STORE_FORMAT_D16_XY  0x0000000d
#define SQ_BUFFER_STORE_FORMAT_D16_XYZ 0x0000000e
#define SQ_BUFFER_STORE_FORMAT_D16_XYZW 0x0000000f
#define SQ_BUFFER_LOAD_UBYTE           0x00000010
#define SQ_BUFFER_LOAD_SBYTE           0x00000011
#define SQ_BUFFER_LOAD_USHORT          0x00000012
#define SQ_BUFFER_LOAD_SSHORT          0x00000013
#define SQ_BUFFER_LOAD_DWORD           0x00000014
#define SQ_BUFFER_LOAD_DWORDX2         0x00000015
#define SQ_BUFFER_LOAD_DWORDX3         0x00000016
#define SQ_BUFFER_LOAD_DWORDX4         0x00000017
#define SQ_BUFFER_STORE_BYTE           0x00000018
#define SQ_BUFFER_STORE_SHORT          0x0000001a
#define SQ_BUFFER_STORE_DWORD          0x0000001c
#define SQ_BUFFER_STORE_DWORDX2        0x0000001d
#define SQ_BUFFER_STORE_DWORDX3        0x0000001e
#define SQ_BUFFER_STORE_DWORDX4        0x0000001f
#define SQ_BUFFER_STORE_LDS_DWORD      0x0000003d
#define SQ_BUFFER_WBINVL1              0x0000003e
#define SQ_BUFFER_WBINVL1_VOL          0x0000003f
#define SQ_BUFFER_ATOMIC_SWAP          0x00000040
#define SQ_BUFFER_ATOMIC_CMPSWAP       0x00000041
#define SQ_BUFFER_ATOMIC_ADD           0x00000042
#define SQ_BUFFER_ATOMIC_SUB           0x00000043
#define SQ_BUFFER_ATOMIC_SMIN          0x00000044
#define SQ_BUFFER_ATOMIC_UMIN          0x00000045
#define SQ_BUFFER_ATOMIC_SMAX          0x00000046
#define SQ_BUFFER_ATOMIC_UMAX          0x00000047
#define SQ_BUFFER_ATOMIC_AND           0x00000048
#define SQ_BUFFER_ATOMIC_OR            0x00000049
#define SQ_BUFFER_ATOMIC_XOR           0x0000004a
#define SQ_BUFFER_ATOMIC_INC           0x0000004b
#define SQ_BUFFER_ATOMIC_DEC           0x0000004c
#define SQ_BUFFER_ATOMIC_SWAP_X2       0x00000060
#define SQ_BUFFER_ATOMIC_CMPSWAP_X2    0x00000061
#define SQ_BUFFER_ATOMIC_ADD_X2        0x00000062
#define SQ_BUFFER_ATOMIC_SUB_X2        0x00000063
#define SQ_BUFFER_ATOMIC_SMIN_X2       0x00000064
#define SQ_BUFFER_ATOMIC_UMIN_X2       0x00000065
#define SQ_BUFFER_ATOMIC_SMAX_X2       0x00000066
#define SQ_BUFFER_ATOMIC_UMAX_X2       0x00000067
#define SQ_BUFFER_ATOMIC_AND_X2        0x00000068
#define SQ_BUFFER_ATOMIC_OR_X2         0x00000069
#define SQ_BUFFER_ATOMIC_XOR_X2        0x0000006a
#define SQ_BUFFER_ATOMIC_INC_X2        0x0000006b
#define SQ_BUFFER_ATOMIC_DEC_X2        0x0000006c

/*
 * VALUE_SQ_SDWA_SEL value
 */

#define SQ_SDWA_BYTE_0                 0x00000000
#define SQ_SDWA_BYTE_1                 0x00000001
#define SQ_SDWA_BYTE_2                 0x00000002
#define SQ_SDWA_BYTE_3                 0x00000003
#define SQ_SDWA_WORD_0                 0x00000004
#define SQ_SDWA_WORD_1                 0x00000005
#define SQ_SDWA_DWORD                  0x00000006

/*******************************************************
 * SX Enums
 *******************************************************/

/*
 * SX_BLEND_OPT enum
 */

typedef enum SX_BLEND_OPT {
BLEND_OPT_PRESERVE_NONE_IGNORE_ALL       = 0x00000000,
BLEND_OPT_PRESERVE_ALL_IGNORE_NONE       = 0x00000001,
BLEND_OPT_PRESERVE_C1_IGNORE_C0          = 0x00000002,
BLEND_OPT_PRESERVE_C0_IGNORE_C1          = 0x00000003,
BLEND_OPT_PRESERVE_A1_IGNORE_A0          = 0x00000004,
BLEND_OPT_PRESERVE_A0_IGNORE_A1          = 0x00000005,
BLEND_OPT_PRESERVE_NONE_IGNORE_A0        = 0x00000006,
BLEND_OPT_PRESERVE_NONE_IGNORE_NONE      = 0x00000007,
} SX_BLEND_OPT;

/*
 * SX_OPT_COMB_FCN enum
 */

typedef enum SX_OPT_COMB_FCN {
OPT_COMB_NONE                            = 0x00000000,
OPT_COMB_ADD                             = 0x00000001,
OPT_COMB_SUBTRACT                        = 0x00000002,
OPT_COMB_MIN                             = 0x00000003,
OPT_COMB_MAX                             = 0x00000004,
OPT_COMB_REVSUBTRACT                     = 0x00000005,
OPT_COMB_BLEND_DISABLED                  = 0x00000006,
OPT_COMB_SAFE_ADD                        = 0x00000007,
} SX_OPT_COMB_FCN;

/*
 * SX_DOWNCONVERT_FORMAT enum
 */

typedef enum SX_DOWNCONVERT_FORMAT {
SX_RT_EXPORT_NO_CONVERSION               = 0x00000000,
SX_RT_EXPORT_32_R                        = 0x00000001,
SX_RT_EXPORT_32_A                        = 0x00000002,
SX_RT_EXPORT_10_11_11                    = 0x00000003,
SX_RT_EXPORT_2_10_10_10                  = 0x00000004,
SX_RT_EXPORT_8_8_8_8                     = 0x00000005,
SX_RT_EXPORT_5_6_5                       = 0x00000006,
SX_RT_EXPORT_1_5_5_5                     = 0x00000007,
SX_RT_EXPORT_4_4_4_4                     = 0x00000008,
SX_RT_EXPORT_16_16_GR                    = 0x00000009,
SX_RT_EXPORT_16_16_AR                    = 0x0000000a,
} SX_DOWNCONVERT_FORMAT;

/*
 * SX_PERFCOUNTER_VALS enum
 */

typedef enum SX_PERFCOUNTER_VALS {
SX_PERF_SEL_PA_IDLE_CYCLES               = 0x00000000,
SX_PERF_SEL_PA_REQ                       = 0x00000001,
SX_PERF_SEL_PA_POS                       = 0x00000002,
SX_PERF_SEL_CLOCK                        = 0x00000003,
SX_PERF_SEL_GATE_EN1                     = 0x00000004,
SX_PERF_SEL_GATE_EN2                     = 0x00000005,
SX_PERF_SEL_GATE_EN3                     = 0x00000006,
SX_PERF_SEL_GATE_EN4                     = 0x00000007,
SX_PERF_SEL_SH_POS_STARVE                = 0x00000008,
SX_PERF_SEL_SH_COLOR_STARVE              = 0x00000009,
SX_PERF_SEL_SH_POS_STALL                 = 0x0000000a,
SX_PERF_SEL_SH_COLOR_STALL               = 0x0000000b,
SX_PERF_SEL_DB0_PIXELS                   = 0x0000000c,
SX_PERF_SEL_DB0_HALF_QUADS               = 0x0000000d,
SX_PERF_SEL_DB0_PIXEL_STALL              = 0x0000000e,
SX_PERF_SEL_DB0_PIXEL_IDLE               = 0x0000000f,
SX_PERF_SEL_DB0_PRED_PIXELS              = 0x00000010,
SX_PERF_SEL_DB1_PIXELS                   = 0x00000011,
SX_PERF_SEL_DB1_HALF_QUADS               = 0x00000012,
SX_PERF_SEL_DB1_PIXEL_STALL              = 0x00000013,
SX_PERF_SEL_DB1_PIXEL_IDLE               = 0x00000014,
SX_PERF_SEL_DB1_PRED_PIXELS              = 0x00000015,
SX_PERF_SEL_DB2_PIXELS                   = 0x00000016,
SX_PERF_SEL_DB2_HALF_QUADS               = 0x00000017,
SX_PERF_SEL_DB2_PIXEL_STALL              = 0x00000018,
SX_PERF_SEL_DB2_PIXEL_IDLE               = 0x00000019,
SX_PERF_SEL_DB2_PRED_PIXELS              = 0x0000001a,
SX_PERF_SEL_DB3_PIXELS                   = 0x0000001b,
SX_PERF_SEL_DB3_HALF_QUADS               = 0x0000001c,
SX_PERF_SEL_DB3_PIXEL_STALL              = 0x0000001d,
SX_PERF_SEL_DB3_PIXEL_IDLE               = 0x0000001e,
SX_PERF_SEL_DB3_PRED_PIXELS              = 0x0000001f,
SX_PERF_SEL_COL_BUSY                     = 0x00000020,
SX_PERF_SEL_POS_BUSY                     = 0x00000021,
SX_PERF_SEL_DB0_A2M_DISCARD_QUADS        = 0x00000022,
SX_PERF_SEL_DB0_MRT0_BLEND_BYPASS        = 0x00000023,
SX_PERF_SEL_DB0_MRT0_DONT_RD_DEST        = 0x00000024,
SX_PERF_SEL_DB0_MRT0_DISCARD_SRC         = 0x00000025,
SX_PERF_SEL_DB0_MRT0_SINGLE_QUADS        = 0x00000026,
SX_PERF_SEL_DB0_MRT0_DOUBLE_QUADS        = 0x00000027,
SX_PERF_SEL_DB0_MRT1_BLEND_BYPASS        = 0x00000028,
SX_PERF_SEL_DB0_MRT1_DONT_RD_DEST        = 0x00000029,
SX_PERF_SEL_DB0_MRT1_DISCARD_SRC         = 0x0000002a,
SX_PERF_SEL_DB0_MRT1_SINGLE_QUADS        = 0x0000002b,
SX_PERF_SEL_DB0_MRT1_DOUBLE_QUADS        = 0x0000002c,
SX_PERF_SEL_DB0_MRT2_BLEND_BYPASS        = 0x0000002d,
SX_PERF_SEL_DB0_MRT2_DONT_RD_DEST        = 0x0000002e,
SX_PERF_SEL_DB0_MRT2_DISCARD_SRC         = 0x0000002f,
SX_PERF_SEL_DB0_MRT2_SINGLE_QUADS        = 0x00000030,
SX_PERF_SEL_DB0_MRT2_DOUBLE_QUADS        = 0x00000031,
SX_PERF_SEL_DB0_MRT3_BLEND_BYPASS        = 0x00000032,
SX_PERF_SEL_DB0_MRT3_DONT_RD_DEST        = 0x00000033,
SX_PERF_SEL_DB0_MRT3_DISCARD_SRC         = 0x00000034,
SX_PERF_SEL_DB0_MRT3_SINGLE_QUADS        = 0x00000035,
SX_PERF_SEL_DB0_MRT3_DOUBLE_QUADS        = 0x00000036,
SX_PERF_SEL_DB0_MRT4_BLEND_BYPASS        = 0x00000037,
SX_PERF_SEL_DB0_MRT4_DONT_RD_DEST        = 0x00000038,
SX_PERF_SEL_DB0_MRT4_DISCARD_SRC         = 0x00000039,
SX_PERF_SEL_DB0_MRT4_SINGLE_QUADS        = 0x0000003a,
SX_PERF_SEL_DB0_MRT4_DOUBLE_QUADS        = 0x0000003b,
SX_PERF_SEL_DB0_MRT5_BLEND_BYPASS        = 0x0000003c,
SX_PERF_SEL_DB0_MRT5_DONT_RD_DEST        = 0x0000003d,
SX_PERF_SEL_DB0_MRT5_DISCARD_SRC         = 0x0000003e,
SX_PERF_SEL_DB0_MRT5_SINGLE_QUADS        = 0x0000003f,
SX_PERF_SEL_DB0_MRT5_DOUBLE_QUADS        = 0x00000040,
SX_PERF_SEL_DB0_MRT6_BLEND_BYPASS        = 0x00000041,
SX_PERF_SEL_DB0_MRT6_DONT_RD_DEST        = 0x00000042,
SX_PERF_SEL_DB0_MRT6_DISCARD_SRC         = 0x00000043,
SX_PERF_SEL_DB0_MRT6_SINGLE_QUADS        = 0x00000044,
SX_PERF_SEL_DB0_MRT6_DOUBLE_QUADS        = 0x00000045,
SX_PERF_SEL_DB0_MRT7_BLEND_BYPASS        = 0x00000046,
SX_PERF_SEL_DB0_MRT7_DONT_RD_DEST        = 0x00000047,
SX_PERF_SEL_DB0_MRT7_DISCARD_SRC         = 0x00000048,
SX_PERF_SEL_DB0_MRT7_SINGLE_QUADS        = 0x00000049,
SX_PERF_SEL_DB0_MRT7_DOUBLE_QUADS        = 0x0000004a,
SX_PERF_SEL_DB1_A2M_DISCARD_QUADS        = 0x0000004b,
SX_PERF_SEL_DB1_MRT0_BLEND_BYPASS        = 0x0000004c,
SX_PERF_SEL_DB1_MRT0_DONT_RD_DEST        = 0x0000004d,
SX_PERF_SEL_DB1_MRT0_DISCARD_SRC         = 0x0000004e,
SX_PERF_SEL_DB1_MRT0_SINGLE_QUADS        = 0x0000004f,
SX_PERF_SEL_DB1_MRT0_DOUBLE_QUADS        = 0x00000050,
SX_PERF_SEL_DB1_MRT1_BLEND_BYPASS        = 0x00000051,
SX_PERF_SEL_DB1_MRT1_DONT_RD_DEST        = 0x00000052,
SX_PERF_SEL_DB1_MRT1_DISCARD_SRC         = 0x00000053,
SX_PERF_SEL_DB1_MRT1_SINGLE_QUADS        = 0x00000054,
SX_PERF_SEL_DB1_MRT1_DOUBLE_QUADS        = 0x00000055,
SX_PERF_SEL_DB1_MRT2_BLEND_BYPASS        = 0x00000056,
SX_PERF_SEL_DB1_MRT2_DONT_RD_DEST        = 0x00000057,
SX_PERF_SEL_DB1_MRT2_DISCARD_SRC         = 0x00000058,
SX_PERF_SEL_DB1_MRT2_SINGLE_QUADS        = 0x00000059,
SX_PERF_SEL_DB1_MRT2_DOUBLE_QUADS        = 0x0000005a,
SX_PERF_SEL_DB1_MRT3_BLEND_BYPASS        = 0x0000005b,
SX_PERF_SEL_DB1_MRT3_DONT_RD_DEST        = 0x0000005c,
SX_PERF_SEL_DB1_MRT3_DISCARD_SRC         = 0x0000005d,
SX_PERF_SEL_DB1_MRT3_SINGLE_QUADS        = 0x0000005e,
SX_PERF_SEL_DB1_MRT3_DOUBLE_QUADS        = 0x0000005f,
SX_PERF_SEL_DB1_MRT4_BLEND_BYPASS        = 0x00000060,
SX_PERF_SEL_DB1_MRT4_DONT_RD_DEST        = 0x00000061,
SX_PERF_SEL_DB1_MRT4_DISCARD_SRC         = 0x00000062,
SX_PERF_SEL_DB1_MRT4_SINGLE_QUADS        = 0x00000063,
SX_PERF_SEL_DB1_MRT4_DOUBLE_QUADS        = 0x00000064,
SX_PERF_SEL_DB1_MRT5_BLEND_BYPASS        = 0x00000065,
SX_PERF_SEL_DB1_MRT5_DONT_RD_DEST        = 0x00000066,
SX_PERF_SEL_DB1_MRT5_DISCARD_SRC         = 0x00000067,
SX_PERF_SEL_DB1_MRT5_SINGLE_QUADS        = 0x00000068,
SX_PERF_SEL_DB1_MRT5_DOUBLE_QUADS        = 0x00000069,
SX_PERF_SEL_DB1_MRT6_BLEND_BYPASS        = 0x0000006a,
SX_PERF_SEL_DB1_MRT6_DONT_RD_DEST        = 0x0000006b,
SX_PERF_SEL_DB1_MRT6_DISCARD_SRC         = 0x0000006c,
SX_PERF_SEL_DB1_MRT6_SINGLE_QUADS        = 0x0000006d,
SX_PERF_SEL_DB1_MRT6_DOUBLE_QUADS        = 0x0000006e,
SX_PERF_SEL_DB1_MRT7_BLEND_BYPASS        = 0x0000006f,
SX_PERF_SEL_DB1_MRT7_DONT_RD_DEST        = 0x00000070,
SX_PERF_SEL_DB1_MRT7_DISCARD_SRC         = 0x00000071,
SX_PERF_SEL_DB1_MRT7_SINGLE_QUADS        = 0x00000072,
SX_PERF_SEL_DB1_MRT7_DOUBLE_QUADS        = 0x00000073,
SX_PERF_SEL_DB2_A2M_DISCARD_QUADS        = 0x00000074,
SX_PERF_SEL_DB2_MRT0_BLEND_BYPASS        = 0x00000075,
SX_PERF_SEL_DB2_MRT0_DONT_RD_DEST        = 0x00000076,
SX_PERF_SEL_DB2_MRT0_DISCARD_SRC         = 0x00000077,
SX_PERF_SEL_DB2_MRT0_SINGLE_QUADS        = 0x00000078,
SX_PERF_SEL_DB2_MRT0_DOUBLE_QUADS        = 0x00000079,
SX_PERF_SEL_DB2_MRT1_BLEND_BYPASS        = 0x0000007a,
SX_PERF_SEL_DB2_MRT1_DONT_RD_DEST        = 0x0000007b,
SX_PERF_SEL_DB2_MRT1_DISCARD_SRC         = 0x0000007c,
SX_PERF_SEL_DB2_MRT1_SINGLE_QUADS        = 0x0000007d,
SX_PERF_SEL_DB2_MRT1_DOUBLE_QUADS        = 0x0000007e,
SX_PERF_SEL_DB2_MRT2_BLEND_BYPASS        = 0x0000007f,
SX_PERF_SEL_DB2_MRT2_DONT_RD_DEST        = 0x00000080,
SX_PERF_SEL_DB2_MRT2_DISCARD_SRC         = 0x00000081,
SX_PERF_SEL_DB2_MRT2_SINGLE_QUADS        = 0x00000082,
SX_PERF_SEL_DB2_MRT2_DOUBLE_QUADS        = 0x00000083,
SX_PERF_SEL_DB2_MRT3_BLEND_BYPASS        = 0x00000084,
SX_PERF_SEL_DB2_MRT3_DONT_RD_DEST        = 0x00000085,
SX_PERF_SEL_DB2_MRT3_DISCARD_SRC         = 0x00000086,
SX_PERF_SEL_DB2_MRT3_SINGLE_QUADS        = 0x00000087,
SX_PERF_SEL_DB2_MRT3_DOUBLE_QUADS        = 0x00000088,
SX_PERF_SEL_DB2_MRT4_BLEND_BYPASS        = 0x00000089,
SX_PERF_SEL_DB2_MRT4_DONT_RD_DEST        = 0x0000008a,
SX_PERF_SEL_DB2_MRT4_DISCARD_SRC         = 0x0000008b,
SX_PERF_SEL_DB2_MRT4_SINGLE_QUADS        = 0x0000008c,
SX_PERF_SEL_DB2_MRT4_DOUBLE_QUADS        = 0x0000008d,
SX_PERF_SEL_DB2_MRT5_BLEND_BYPASS        = 0x0000008e,
SX_PERF_SEL_DB2_MRT5_DONT_RD_DEST        = 0x0000008f,
SX_PERF_SEL_DB2_MRT5_DISCARD_SRC         = 0x00000090,
SX_PERF_SEL_DB2_MRT5_SINGLE_QUADS        = 0x00000091,
SX_PERF_SEL_DB2_MRT5_DOUBLE_QUADS        = 0x00000092,
SX_PERF_SEL_DB2_MRT6_BLEND_BYPASS        = 0x00000093,
SX_PERF_SEL_DB2_MRT6_DONT_RD_DEST        = 0x00000094,
SX_PERF_SEL_DB2_MRT6_DISCARD_SRC         = 0x00000095,
SX_PERF_SEL_DB2_MRT6_SINGLE_QUADS        = 0x00000096,
SX_PERF_SEL_DB2_MRT6_DOUBLE_QUADS        = 0x00000097,
SX_PERF_SEL_DB2_MRT7_BLEND_BYPASS        = 0x00000098,
SX_PERF_SEL_DB2_MRT7_DONT_RD_DEST        = 0x00000099,
SX_PERF_SEL_DB2_MRT7_DISCARD_SRC         = 0x0000009a,
SX_PERF_SEL_DB2_MRT7_SINGLE_QUADS        = 0x0000009b,
SX_PERF_SEL_DB2_MRT7_DOUBLE_QUADS        = 0x0000009c,
SX_PERF_SEL_DB3_A2M_DISCARD_QUADS        = 0x0000009d,
SX_PERF_SEL_DB3_MRT0_BLEND_BYPASS        = 0x0000009e,
SX_PERF_SEL_DB3_MRT0_DONT_RD_DEST        = 0x0000009f,
SX_PERF_SEL_DB3_MRT0_DISCARD_SRC         = 0x000000a0,
SX_PERF_SEL_DB3_MRT0_SINGLE_QUADS        = 0x000000a1,
SX_PERF_SEL_DB3_MRT0_DOUBLE_QUADS        = 0x000000a2,
SX_PERF_SEL_DB3_MRT1_BLEND_BYPASS        = 0x000000a3,
SX_PERF_SEL_DB3_MRT1_DONT_RD_DEST        = 0x000000a4,
SX_PERF_SEL_DB3_MRT1_DISCARD_SRC         = 0x000000a5,
SX_PERF_SEL_DB3_MRT1_SINGLE_QUADS        = 0x000000a6,
SX_PERF_SEL_DB3_MRT1_DOUBLE_QUADS        = 0x000000a7,
SX_PERF_SEL_DB3_MRT2_BLEND_BYPASS        = 0x000000a8,
SX_PERF_SEL_DB3_MRT2_DONT_RD_DEST        = 0x000000a9,
SX_PERF_SEL_DB3_MRT2_DISCARD_SRC         = 0x000000aa,
SX_PERF_SEL_DB3_MRT2_SINGLE_QUADS        = 0x000000ab,
SX_PERF_SEL_DB3_MRT2_DOUBLE_QUADS        = 0x000000ac,
SX_PERF_SEL_DB3_MRT3_BLEND_BYPASS        = 0x000000ad,
SX_PERF_SEL_DB3_MRT3_DONT_RD_DEST        = 0x000000ae,
SX_PERF_SEL_DB3_MRT3_DISCARD_SRC         = 0x000000af,
SX_PERF_SEL_DB3_MRT3_SINGLE_QUADS        = 0x000000b0,
SX_PERF_SEL_DB3_MRT3_DOUBLE_QUADS        = 0x000000b1,
SX_PERF_SEL_DB3_MRT4_BLEND_BYPASS        = 0x000000b2,
SX_PERF_SEL_DB3_MRT4_DONT_RD_DEST        = 0x000000b3,
SX_PERF_SEL_DB3_MRT4_DISCARD_SRC         = 0x000000b4,
SX_PERF_SEL_DB3_MRT4_SINGLE_QUADS        = 0x000000b5,
SX_PERF_SEL_DB3_MRT4_DOUBLE_QUADS        = 0x000000b6,
SX_PERF_SEL_DB3_MRT5_BLEND_BYPASS        = 0x000000b7,
SX_PERF_SEL_DB3_MRT5_DONT_RD_DEST        = 0x000000b8,
SX_PERF_SEL_DB3_MRT5_DISCARD_SRC         = 0x000000b9,
SX_PERF_SEL_DB3_MRT5_SINGLE_QUADS        = 0x000000ba,
SX_PERF_SEL_DB3_MRT5_DOUBLE_QUADS        = 0x000000bb,
SX_PERF_SEL_DB3_MRT6_BLEND_BYPASS        = 0x000000bc,
SX_PERF_SEL_DB3_MRT6_DONT_RD_DEST        = 0x000000bd,
SX_PERF_SEL_DB3_MRT6_DISCARD_SRC         = 0x000000be,
SX_PERF_SEL_DB3_MRT6_SINGLE_QUADS        = 0x000000bf,
SX_PERF_SEL_DB3_MRT6_DOUBLE_QUADS        = 0x000000c0,
SX_PERF_SEL_DB3_MRT7_BLEND_BYPASS        = 0x000000c1,
SX_PERF_SEL_DB3_MRT7_DONT_RD_DEST        = 0x000000c2,
SX_PERF_SEL_DB3_MRT7_DISCARD_SRC         = 0x000000c3,
SX_PERF_SEL_DB3_MRT7_SINGLE_QUADS        = 0x000000c4,
SX_PERF_SEL_DB3_MRT7_DOUBLE_QUADS        = 0x000000c5,
} SX_PERFCOUNTER_VALS;

/*******************************************************
 * DB Enums
 *******************************************************/

/*
 * ForceControl enum
 */

typedef enum ForceControl {
FORCE_OFF                                = 0x00000000,
FORCE_ENABLE                             = 0x00000001,
FORCE_DISABLE                            = 0x00000002,
FORCE_RESERVED                           = 0x00000003,
} ForceControl;

/*
 * ZSamplePosition enum
 */

typedef enum ZSamplePosition {
Z_SAMPLE_CENTER                          = 0x00000000,
Z_SAMPLE_CENTROID                        = 0x00000001,
} ZSamplePosition;

/*
 * ZOrder enum
 */

typedef enum ZOrder {
LATE_Z                                   = 0x00000000,
EARLY_Z_THEN_LATE_Z                      = 0x00000001,
RE_Z                                     = 0x00000002,
EARLY_Z_THEN_RE_Z                        = 0x00000003,
} ZOrder;

/*
 * ZpassControl enum
 */

typedef enum ZpassControl {
ZPASS_DISABLE                            = 0x00000000,
ZPASS_SAMPLES                            = 0x00000001,
ZPASS_PIXELS                             = 0x00000002,
} ZpassControl;

/*
 * ZModeForce enum
 */

typedef enum ZModeForce {
NO_FORCE                                 = 0x00000000,
FORCE_EARLY_Z                            = 0x00000001,
FORCE_LATE_Z                             = 0x00000002,
FORCE_RE_Z                               = 0x00000003,
} ZModeForce;

/*
 * ZLimitSumm enum
 */

typedef enum ZLimitSumm {
FORCE_SUMM_OFF                           = 0x00000000,
FORCE_SUMM_MINZ                          = 0x00000001,
FORCE_SUMM_MAXZ                          = 0x00000002,
FORCE_SUMM_BOTH                          = 0x00000003,
} ZLimitSumm;

/*
 * CompareFrag enum
 */

typedef enum CompareFrag {
FRAG_NEVER                               = 0x00000000,
FRAG_LESS                                = 0x00000001,
FRAG_EQUAL                               = 0x00000002,
FRAG_LEQUAL                              = 0x00000003,
FRAG_GREATER                             = 0x00000004,
FRAG_NOTEQUAL                            = 0x00000005,
FRAG_GEQUAL                              = 0x00000006,
FRAG_ALWAYS                              = 0x00000007,
} CompareFrag;

/*
 * StencilOp enum
 */

typedef enum StencilOp {
STENCIL_KEEP                             = 0x00000000,
STENCIL_ZERO                             = 0x00000001,
STENCIL_ONES                             = 0x00000002,
STENCIL_REPLACE_TEST                     = 0x00000003,
STENCIL_REPLACE_OP                       = 0x00000004,
STENCIL_ADD_CLAMP                        = 0x00000005,
STENCIL_SUB_CLAMP                        = 0x00000006,
STENCIL_INVERT                           = 0x00000007,
STENCIL_ADD_WRAP                         = 0x00000008,
STENCIL_SUB_WRAP                         = 0x00000009,
STENCIL_AND                              = 0x0000000a,
STENCIL_OR                               = 0x0000000b,
STENCIL_XOR                              = 0x0000000c,
STENCIL_NAND                             = 0x0000000d,
STENCIL_NOR                              = 0x0000000e,
STENCIL_XNOR                             = 0x0000000f,
} StencilOp;

/*
 * ConservativeZExport enum
 */

typedef enum ConservativeZExport {
EXPORT_ANY_Z                             = 0x00000000,
EXPORT_LESS_THAN_Z                       = 0x00000001,
EXPORT_GREATER_THAN_Z                    = 0x00000002,
EXPORT_RESERVED                          = 0x00000003,
} ConservativeZExport;

/*
 * DbPSLControl enum
 */

typedef enum DbPSLControl {
PSLC_AUTO                                = 0x00000000,
PSLC_ON_HANG_ONLY                        = 0x00000001,
PSLC_ASAP                                = 0x00000002,
PSLC_COUNTDOWN                           = 0x00000003,
} DbPSLControl;

/*
 * DbPRTFaultBehavior enum
 */

typedef enum DbPRTFaultBehavior {
FAULT_ZERO                               = 0x00000000,
FAULT_ONE                                = 0x00000001,
FAULT_FAIL                               = 0x00000002,
FAULT_PASS                               = 0x00000003,
} DbPRTFaultBehavior;

/*
 * PerfCounter_Vals enum
 */

typedef enum PerfCounter_Vals {
DB_PERF_SEL_SC_DB_tile_sends             = 0x00000000,
DB_PERF_SEL_SC_DB_tile_busy              = 0x00000001,
DB_PERF_SEL_SC_DB_tile_stalls            = 0x00000002,
DB_PERF_SEL_SC_DB_tile_events            = 0x00000003,
DB_PERF_SEL_SC_DB_tile_tiles             = 0x00000004,
DB_PERF_SEL_SC_DB_tile_covered           = 0x00000005,
DB_PERF_SEL_hiz_tc_read_starved          = 0x00000006,
DB_PERF_SEL_hiz_tc_write_stall           = 0x00000007,
DB_PERF_SEL_hiz_qtiles_culled            = 0x00000008,
DB_PERF_SEL_his_qtiles_culled            = 0x00000009,
DB_PERF_SEL_DB_SC_tile_sends             = 0x0000000a,
DB_PERF_SEL_DB_SC_tile_busy              = 0x0000000b,
DB_PERF_SEL_DB_SC_tile_stalls            = 0x0000000c,
DB_PERF_SEL_DB_SC_tile_df_stalls         = 0x0000000d,
DB_PERF_SEL_DB_SC_tile_tiles             = 0x0000000e,
DB_PERF_SEL_DB_SC_tile_culled            = 0x0000000f,
DB_PERF_SEL_DB_SC_tile_hier_kill         = 0x00000010,
DB_PERF_SEL_DB_SC_tile_fast_ops          = 0x00000011,
DB_PERF_SEL_DB_SC_tile_no_ops            = 0x00000012,
DB_PERF_SEL_DB_SC_tile_tile_rate         = 0x00000013,
DB_PERF_SEL_DB_SC_tile_ssaa_kill         = 0x00000014,
DB_PERF_SEL_DB_SC_tile_fast_z_ops        = 0x00000015,
DB_PERF_SEL_DB_SC_tile_fast_stencil_ops  = 0x00000016,
DB_PERF_SEL_SC_DB_quad_sends             = 0x00000017,
DB_PERF_SEL_SC_DB_quad_busy              = 0x00000018,
DB_PERF_SEL_SC_DB_quad_squads            = 0x00000019,
DB_PERF_SEL_SC_DB_quad_tiles             = 0x0000001a,
DB_PERF_SEL_SC_DB_quad_pixels            = 0x0000001b,
DB_PERF_SEL_SC_DB_quad_killed_tiles      = 0x0000001c,
DB_PERF_SEL_DB_SC_quad_sends             = 0x0000001d,
DB_PERF_SEL_DB_SC_quad_busy              = 0x0000001e,
DB_PERF_SEL_DB_SC_quad_stalls            = 0x0000001f,
DB_PERF_SEL_DB_SC_quad_tiles             = 0x00000020,
DB_PERF_SEL_DB_SC_quad_lit_quad          = 0x00000021,
DB_PERF_SEL_DB_CB_tile_sends             = 0x00000022,
DB_PERF_SEL_DB_CB_tile_busy              = 0x00000023,
DB_PERF_SEL_DB_CB_tile_stalls            = 0x00000024,
DB_PERF_SEL_SX_DB_quad_sends             = 0x00000025,
DB_PERF_SEL_SX_DB_quad_busy              = 0x00000026,
DB_PERF_SEL_SX_DB_quad_stalls            = 0x00000027,
DB_PERF_SEL_SX_DB_quad_quads             = 0x00000028,
DB_PERF_SEL_SX_DB_quad_pixels            = 0x00000029,
DB_PERF_SEL_SX_DB_quad_exports           = 0x0000002a,
DB_PERF_SEL_SH_quads_outstanding_sum     = 0x0000002b,
DB_PERF_SEL_DB_CB_lquad_sends            = 0x0000002c,
DB_PERF_SEL_DB_CB_lquad_busy             = 0x0000002d,
DB_PERF_SEL_DB_CB_lquad_stalls           = 0x0000002e,
DB_PERF_SEL_DB_CB_lquad_quads            = 0x0000002f,
DB_PERF_SEL_tile_rd_sends                = 0x00000030,
DB_PERF_SEL_mi_tile_rd_outstanding_sum   = 0x00000031,
DB_PERF_SEL_quad_rd_sends                = 0x00000032,
DB_PERF_SEL_quad_rd_busy                 = 0x00000033,
DB_PERF_SEL_quad_rd_mi_stall             = 0x00000034,
DB_PERF_SEL_quad_rd_rw_collision         = 0x00000035,
DB_PERF_SEL_quad_rd_tag_stall            = 0x00000036,
DB_PERF_SEL_quad_rd_32byte_reqs          = 0x00000037,
DB_PERF_SEL_quad_rd_panic                = 0x00000038,
DB_PERF_SEL_mi_quad_rd_outstanding_sum   = 0x00000039,
DB_PERF_SEL_quad_rdret_sends             = 0x0000003a,
DB_PERF_SEL_quad_rdret_busy              = 0x0000003b,
DB_PERF_SEL_tile_wr_sends                = 0x0000003c,
DB_PERF_SEL_tile_wr_acks                 = 0x0000003d,
DB_PERF_SEL_mi_tile_wr_outstanding_sum   = 0x0000003e,
DB_PERF_SEL_quad_wr_sends                = 0x0000003f,
DB_PERF_SEL_quad_wr_busy                 = 0x00000040,
DB_PERF_SEL_quad_wr_mi_stall             = 0x00000041,
DB_PERF_SEL_quad_wr_coherency_stall      = 0x00000042,
DB_PERF_SEL_quad_wr_acks                 = 0x00000043,
DB_PERF_SEL_mi_quad_wr_outstanding_sum   = 0x00000044,
DB_PERF_SEL_Tile_Cache_misses            = 0x00000045,
DB_PERF_SEL_Tile_Cache_hits              = 0x00000046,
DB_PERF_SEL_Tile_Cache_flushes           = 0x00000047,
DB_PERF_SEL_Tile_Cache_surface_stall     = 0x00000048,
DB_PERF_SEL_Tile_Cache_starves           = 0x00000049,
DB_PERF_SEL_Tile_Cache_mem_return_starve  = 0x0000004a,
DB_PERF_SEL_tcp_dispatcher_reads         = 0x0000004b,
DB_PERF_SEL_tcp_prefetcher_reads         = 0x0000004c,
DB_PERF_SEL_tcp_preloader_reads          = 0x0000004d,
DB_PERF_SEL_tcp_dispatcher_flushes       = 0x0000004e,
DB_PERF_SEL_tcp_prefetcher_flushes       = 0x0000004f,
DB_PERF_SEL_tcp_preloader_flushes        = 0x00000050,
DB_PERF_SEL_Depth_Tile_Cache_sends       = 0x00000051,
DB_PERF_SEL_Depth_Tile_Cache_busy        = 0x00000052,
DB_PERF_SEL_Depth_Tile_Cache_starves     = 0x00000053,
DB_PERF_SEL_Depth_Tile_Cache_dtile_locked  = 0x00000054,
DB_PERF_SEL_Depth_Tile_Cache_alloc_stall  = 0x00000055,
DB_PERF_SEL_Depth_Tile_Cache_misses      = 0x00000056,
DB_PERF_SEL_Depth_Tile_Cache_hits        = 0x00000057,
DB_PERF_SEL_Depth_Tile_Cache_flushes     = 0x00000058,
DB_PERF_SEL_Depth_Tile_Cache_noop_tile   = 0x00000059,
DB_PERF_SEL_Depth_Tile_Cache_detailed_noop  = 0x0000005a,
DB_PERF_SEL_Depth_Tile_Cache_event       = 0x0000005b,
DB_PERF_SEL_Depth_Tile_Cache_tile_frees  = 0x0000005c,
DB_PERF_SEL_Depth_Tile_Cache_data_frees  = 0x0000005d,
DB_PERF_SEL_Depth_Tile_Cache_mem_return_starve  = 0x0000005e,
DB_PERF_SEL_Stencil_Cache_misses         = 0x0000005f,
DB_PERF_SEL_Stencil_Cache_hits           = 0x00000060,
DB_PERF_SEL_Stencil_Cache_flushes        = 0x00000061,
DB_PERF_SEL_Stencil_Cache_starves        = 0x00000062,
DB_PERF_SEL_Stencil_Cache_frees          = 0x00000063,
DB_PERF_SEL_Z_Cache_separate_Z_misses    = 0x00000064,
DB_PERF_SEL_Z_Cache_separate_Z_hits      = 0x00000065,
DB_PERF_SEL_Z_Cache_separate_Z_flushes   = 0x00000066,
DB_PERF_SEL_Z_Cache_separate_Z_starves   = 0x00000067,
DB_PERF_SEL_Z_Cache_pmask_misses         = 0x00000068,
DB_PERF_SEL_Z_Cache_pmask_hits           = 0x00000069,
DB_PERF_SEL_Z_Cache_pmask_flushes        = 0x0000006a,
DB_PERF_SEL_Z_Cache_pmask_starves        = 0x0000006b,
DB_PERF_SEL_Z_Cache_frees                = 0x0000006c,
DB_PERF_SEL_Plane_Cache_misses           = 0x0000006d,
DB_PERF_SEL_Plane_Cache_hits             = 0x0000006e,
DB_PERF_SEL_Plane_Cache_flushes          = 0x0000006f,
DB_PERF_SEL_Plane_Cache_starves          = 0x00000070,
DB_PERF_SEL_Plane_Cache_frees            = 0x00000071,
DB_PERF_SEL_flush_expanded_stencil       = 0x00000072,
DB_PERF_SEL_flush_compressed_stencil     = 0x00000073,
DB_PERF_SEL_flush_single_stencil         = 0x00000074,
DB_PERF_SEL_planes_flushed               = 0x00000075,
DB_PERF_SEL_flush_1plane                 = 0x00000076,
DB_PERF_SEL_flush_2plane                 = 0x00000077,
DB_PERF_SEL_flush_3plane                 = 0x00000078,
DB_PERF_SEL_flush_4plane                 = 0x00000079,
DB_PERF_SEL_flush_5plane                 = 0x0000007a,
DB_PERF_SEL_flush_6plane                 = 0x0000007b,
DB_PERF_SEL_flush_7plane                 = 0x0000007c,
DB_PERF_SEL_flush_8plane                 = 0x0000007d,
DB_PERF_SEL_flush_9plane                 = 0x0000007e,
DB_PERF_SEL_flush_10plane                = 0x0000007f,
DB_PERF_SEL_flush_11plane                = 0x00000080,
DB_PERF_SEL_flush_12plane                = 0x00000081,
DB_PERF_SEL_flush_13plane                = 0x00000082,
DB_PERF_SEL_flush_14plane                = 0x00000083,
DB_PERF_SEL_flush_15plane                = 0x00000084,
DB_PERF_SEL_flush_16plane                = 0x00000085,
DB_PERF_SEL_flush_expanded_z             = 0x00000086,
DB_PERF_SEL_earlyZ_waiting_for_postZ_done  = 0x00000087,
DB_PERF_SEL_reZ_waiting_for_postZ_done   = 0x00000088,
DB_PERF_SEL_dk_tile_sends                = 0x00000089,
DB_PERF_SEL_dk_tile_busy                 = 0x0000008a,
DB_PERF_SEL_dk_tile_quad_starves         = 0x0000008b,
DB_PERF_SEL_dk_tile_stalls               = 0x0000008c,
DB_PERF_SEL_dk_squad_sends               = 0x0000008d,
DB_PERF_SEL_dk_squad_busy                = 0x0000008e,
DB_PERF_SEL_dk_squad_stalls              = 0x0000008f,
DB_PERF_SEL_Op_Pipe_Busy                 = 0x00000090,
DB_PERF_SEL_Op_Pipe_MC_Read_stall        = 0x00000091,
DB_PERF_SEL_qc_busy                      = 0x00000092,
DB_PERF_SEL_qc_xfc                       = 0x00000093,
DB_PERF_SEL_qc_conflicts                 = 0x00000094,
DB_PERF_SEL_qc_full_stall                = 0x00000095,
DB_PERF_SEL_qc_in_preZ_tile_stalls_postZ  = 0x00000096,
DB_PERF_SEL_qc_in_postZ_tile_stalls_preZ  = 0x00000097,
DB_PERF_SEL_tsc_insert_summarize_stall   = 0x00000098,
DB_PERF_SEL_tl_busy                      = 0x00000099,
DB_PERF_SEL_tl_dtc_read_starved          = 0x0000009a,
DB_PERF_SEL_tl_z_fetch_stall             = 0x0000009b,
DB_PERF_SEL_tl_stencil_stall             = 0x0000009c,
DB_PERF_SEL_tl_z_decompress_stall        = 0x0000009d,
DB_PERF_SEL_tl_stencil_locked_stall      = 0x0000009e,
DB_PERF_SEL_tl_events                    = 0x0000009f,
DB_PERF_SEL_tl_summarize_squads          = 0x000000a0,
DB_PERF_SEL_tl_flush_expand_squads       = 0x000000a1,
DB_PERF_SEL_tl_expand_squads             = 0x000000a2,
DB_PERF_SEL_tl_preZ_squads               = 0x000000a3,
DB_PERF_SEL_tl_postZ_squads              = 0x000000a4,
DB_PERF_SEL_tl_preZ_noop_squads          = 0x000000a5,
DB_PERF_SEL_tl_postZ_noop_squads         = 0x000000a6,
DB_PERF_SEL_tl_tile_ops                  = 0x000000a7,
DB_PERF_SEL_tl_in_xfc                    = 0x000000a8,
DB_PERF_SEL_tl_in_single_stencil_expand_stall  = 0x000000a9,
DB_PERF_SEL_tl_in_fast_z_stall           = 0x000000aa,
DB_PERF_SEL_tl_out_xfc                   = 0x000000ab,
DB_PERF_SEL_tl_out_squads                = 0x000000ac,
DB_PERF_SEL_zf_plane_multicycle          = 0x000000ad,
DB_PERF_SEL_PostZ_Samples_passing_Z      = 0x000000ae,
DB_PERF_SEL_PostZ_Samples_failing_Z      = 0x000000af,
DB_PERF_SEL_PostZ_Samples_failing_S      = 0x000000b0,
DB_PERF_SEL_PreZ_Samples_passing_Z       = 0x000000b1,
DB_PERF_SEL_PreZ_Samples_failing_Z       = 0x000000b2,
DB_PERF_SEL_PreZ_Samples_failing_S       = 0x000000b3,
DB_PERF_SEL_ts_tc_update_stall           = 0x000000b4,
DB_PERF_SEL_sc_kick_start                = 0x000000b5,
DB_PERF_SEL_sc_kick_end                  = 0x000000b6,
DB_PERF_SEL_clock_reg_active             = 0x000000b7,
DB_PERF_SEL_clock_main_active            = 0x000000b8,
DB_PERF_SEL_clock_mem_export_active      = 0x000000b9,
DB_PERF_SEL_esr_ps_out_busy              = 0x000000ba,
DB_PERF_SEL_esr_ps_lqf_busy              = 0x000000bb,
DB_PERF_SEL_esr_ps_lqf_stall             = 0x000000bc,
DB_PERF_SEL_etr_out_send                 = 0x000000bd,
DB_PERF_SEL_etr_out_busy                 = 0x000000be,
DB_PERF_SEL_etr_out_ltile_probe_fifo_full_stall  = 0x000000bf,
DB_PERF_SEL_etr_out_cb_tile_stall        = 0x000000c0,
DB_PERF_SEL_etr_out_esr_stall            = 0x000000c1,
DB_PERF_SEL_esr_ps_sqq_busy              = 0x000000c2,
DB_PERF_SEL_esr_ps_sqq_stall             = 0x000000c3,
DB_PERF_SEL_esr_eot_fwd_busy             = 0x000000c4,
DB_PERF_SEL_esr_eot_fwd_holding_squad    = 0x000000c5,
DB_PERF_SEL_esr_eot_fwd_forward          = 0x000000c6,
DB_PERF_SEL_esr_sqq_zi_busy              = 0x000000c7,
DB_PERF_SEL_esr_sqq_zi_stall             = 0x000000c8,
DB_PERF_SEL_postzl_sq_pt_busy            = 0x000000c9,
DB_PERF_SEL_postzl_sq_pt_stall           = 0x000000ca,
DB_PERF_SEL_postzl_se_busy               = 0x000000cb,
DB_PERF_SEL_postzl_se_stall              = 0x000000cc,
DB_PERF_SEL_postzl_partial_launch        = 0x000000cd,
DB_PERF_SEL_postzl_full_launch           = 0x000000ce,
DB_PERF_SEL_postzl_partial_waiting       = 0x000000cf,
DB_PERF_SEL_postzl_tile_mem_stall        = 0x000000d0,
DB_PERF_SEL_postzl_tile_init_stall       = 0x000000d1,
DB_PEFF_SEL_prezl_tile_mem_stall         = 0x000000d2,
DB_PERF_SEL_prezl_tile_init_stall        = 0x000000d3,
DB_PERF_SEL_dtt_sm_clash_stall           = 0x000000d4,
DB_PERF_SEL_dtt_sm_slot_stall            = 0x000000d5,
DB_PERF_SEL_dtt_sm_miss_stall            = 0x000000d6,
DB_PERF_SEL_mi_rdreq_busy                = 0x000000d7,
DB_PERF_SEL_mi_rdreq_stall               = 0x000000d8,
DB_PERF_SEL_mi_wrreq_busy                = 0x000000d9,
DB_PERF_SEL_mi_wrreq_stall               = 0x000000da,
DB_PERF_SEL_recomp_tile_to_1zplane_no_fastop  = 0x000000db,
DB_PERF_SEL_dkg_tile_rate_tile           = 0x000000dc,
DB_PERF_SEL_prezl_src_in_sends           = 0x000000dd,
DB_PERF_SEL_prezl_src_in_stall           = 0x000000de,
DB_PERF_SEL_prezl_src_in_squads          = 0x000000df,
DB_PERF_SEL_prezl_src_in_squads_unrolled  = 0x000000e0,
DB_PERF_SEL_prezl_src_in_tile_rate       = 0x000000e1,
DB_PERF_SEL_prezl_src_in_tile_rate_unrolled  = 0x000000e2,
DB_PERF_SEL_prezl_src_out_stall          = 0x000000e3,
DB_PERF_SEL_postzl_src_in_sends          = 0x000000e4,
DB_PERF_SEL_postzl_src_in_stall          = 0x000000e5,
DB_PERF_SEL_postzl_src_in_squads         = 0x000000e6,
DB_PERF_SEL_postzl_src_in_squads_unrolled  = 0x000000e7,
DB_PERF_SEL_postzl_src_in_tile_rate      = 0x000000e8,
DB_PERF_SEL_postzl_src_in_tile_rate_unrolled  = 0x000000e9,
DB_PERF_SEL_postzl_src_out_stall         = 0x000000ea,
DB_PERF_SEL_esr_ps_src_in_sends          = 0x000000eb,
DB_PERF_SEL_esr_ps_src_in_stall          = 0x000000ec,
DB_PERF_SEL_esr_ps_src_in_squads         = 0x000000ed,
DB_PERF_SEL_esr_ps_src_in_squads_unrolled  = 0x000000ee,
DB_PERF_SEL_esr_ps_src_in_tile_rate      = 0x000000ef,
DB_PERF_SEL_esr_ps_src_in_tile_rate_unrolled  = 0x000000f0,
DB_PERF_SEL_esr_ps_src_in_tile_rate_unrolled_to_pixel_rate  = 0x000000f1,
DB_PERF_SEL_esr_ps_src_out_stall         = 0x000000f2,
DB_PERF_SEL_depth_bounds_qtiles_culled   = 0x000000f3,
DB_PERF_SEL_PreZ_Samples_failing_DB      = 0x000000f4,
DB_PERF_SEL_PostZ_Samples_failing_DB     = 0x000000f5,
DB_PERF_SEL_flush_compressed             = 0x000000f6,
DB_PERF_SEL_flush_plane_le4              = 0x000000f7,
DB_PERF_SEL_tiles_z_fully_summarized     = 0x000000f8,
DB_PERF_SEL_tiles_stencil_fully_summarized  = 0x000000f9,
DB_PERF_SEL_tiles_z_clear_on_expclear    = 0x000000fa,
DB_PERF_SEL_tiles_s_clear_on_expclear    = 0x000000fb,
DB_PERF_SEL_tiles_decomp_on_expclear     = 0x000000fc,
DB_PERF_SEL_tiles_compressed_to_decompressed  = 0x000000fd,
DB_PERF_SEL_Op_Pipe_Prez_Busy            = 0x000000fe,
DB_PERF_SEL_Op_Pipe_Postz_Busy           = 0x000000ff,
DB_PERF_SEL_di_dt_stall                  = 0x00000100,
DB_PERF_SEL_DB_SC_quad_double_quad       = 0x00000101,
DB_PERF_SEL_SX_DB_quad_export_quads      = 0x00000102,
DB_PERF_SEL_SX_DB_quad_double_format     = 0x00000103,
DB_PERF_SEL_SX_DB_quad_fast_format       = 0x00000104,
DB_PERF_SEL_SX_DB_quad_slow_format       = 0x00000105,
DB_PERF_SEL_DB_CB_lquad_export_quads     = 0x00000106,
DB_PERF_SEL_DB_CB_lquad_double_format    = 0x00000107,
DB_PERF_SEL_DB_CB_lquad_fast_format      = 0x00000108,
DB_PERF_SEL_DB_CB_lquad_slow_format      = 0x00000109,
DB_PERF_SEL_CB_DB_rdreq_sends            = 0x0000010a,
DB_PERF_SEL_CB_DB_rdreq_prt_sends        = 0x0000010b,
DB_PERF_SEL_CB_DB_wrreq_sends            = 0x0000010c,
DB_PERF_SEL_CB_DB_wrreq_prt_sends        = 0x0000010d,
DB_PERF_SEL_DB_CB_rdret_ack              = 0x0000010e,
DB_PERF_SEL_DB_CB_rdret_nack             = 0x0000010f,
DB_PERF_SEL_DB_CB_wrret_ack              = 0x00000110,
DB_PERF_SEL_DB_CB_wrret_nack             = 0x00000111,
DB_PERF_SEL_DFSM_squads_in               = 0x00000112,
DB_PERF_SEL_DFSM_full_cleared_squads_out  = 0x00000113,
DB_PERF_SEL_DFSM_quads_in                = 0x00000114,
DB_PERF_SEL_DFSM_fully_cleared_quads_out  = 0x00000115,
DB_PERF_SEL_DFSM_lit_pixels_in           = 0x00000116,
DB_PERF_SEL_DFSM_fully_cleared_pixels_out  = 0x00000117,
DB_PERF_SEL_DFSM_lit_samples_in          = 0x00000118,
DB_PERF_SEL_DFSM_lit_samples_out         = 0x00000119,
DB_PERF_SEL_DFSM_cycles_above_watermark  = 0x0000011a,
DB_PERF_SEL_DFSM_cant_accept_squads_but_not_stalled_by_downstream  = 0x0000011b,
DB_PERF_SEL_DFSM_stalled_by_downstream   = 0x0000011c,
DB_PERF_SEL_DFSM_evicted_squads_above_watermark  = 0x0000011d,
DB_PERF_SEL_DFSM_collisions_due_to_POPS_overflow  = 0x0000011e,
DB_PERF_SEL_DFSM_collisions_detected_within_POPS_FIFO  = 0x0000011f,
DB_PERF_SEL_DFSM_evicted_squads_due_to_prim_watermark  = 0x00000120,
} PerfCounter_Vals;

/*
 * RingCounterControl enum
 */

typedef enum RingCounterControl {
COUNTER_RING_SPLIT                       = 0x00000000,
COUNTER_RING_0                           = 0x00000001,
COUNTER_RING_1                           = 0x00000002,
} RingCounterControl;

/*
 * DbMemArbWatermarks enum
 */

typedef enum DbMemArbWatermarks {
TRANSFERRED_64_BYTES                     = 0x00000000,
TRANSFERRED_128_BYTES                    = 0x00000001,
TRANSFERRED_256_BYTES                    = 0x00000002,
TRANSFERRED_512_BYTES                    = 0x00000003,
TRANSFERRED_1024_BYTES                   = 0x00000004,
TRANSFERRED_2048_BYTES                   = 0x00000005,
TRANSFERRED_4096_BYTES                   = 0x00000006,
TRANSFERRED_8192_BYTES                   = 0x00000007,
} DbMemArbWatermarks;

/*
 * DFSMFlushEvents enum
 */

typedef enum DFSMFlushEvents {
DB_FLUSH_AND_INV_DB_DATA_TS              = 0x00000000,
DB_FLUSH_AND_INV_DB_META                 = 0x00000001,
DB_CACHE_FLUSH                           = 0x00000002,
DB_CACHE_FLUSH_TS                        = 0x00000003,
DB_CACHE_FLUSH_AND_INV_EVENT             = 0x00000004,
DB_CACHE_FLUSH_AND_INV_TS_EVENT          = 0x00000005,
} DFSMFlushEvents;

/*
 * PixelPipeCounterId enum
 */

typedef enum PixelPipeCounterId {
PIXEL_PIPE_OCCLUSION_COUNT_0             = 0x00000000,
PIXEL_PIPE_OCCLUSION_COUNT_1             = 0x00000001,
PIXEL_PIPE_OCCLUSION_COUNT_2             = 0x00000002,
PIXEL_PIPE_OCCLUSION_COUNT_3             = 0x00000003,
PIXEL_PIPE_SCREEN_MIN_EXTENTS_0          = 0x00000004,
PIXEL_PIPE_SCREEN_MAX_EXTENTS_0          = 0x00000005,
PIXEL_PIPE_SCREEN_MIN_EXTENTS_1          = 0x00000006,
PIXEL_PIPE_SCREEN_MAX_EXTENTS_1          = 0x00000007,
} PixelPipeCounterId;

/*
 * PixelPipeStride enum
 */

typedef enum PixelPipeStride {
PIXEL_PIPE_STRIDE_32_BITS                = 0x00000000,
PIXEL_PIPE_STRIDE_64_BITS                = 0x00000001,
PIXEL_PIPE_STRIDE_128_BITS               = 0x00000002,
PIXEL_PIPE_STRIDE_256_BITS               = 0x00000003,
} PixelPipeStride;

/*******************************************************
 * TA Enums
 *******************************************************/

/*
 * TEX_BORDER_COLOR_TYPE enum
 */

typedef enum TEX_BORDER_COLOR_TYPE {
TEX_BorderColor_TransparentBlack         = 0x00000000,
TEX_BorderColor_OpaqueBlack              = 0x00000001,
TEX_BorderColor_OpaqueWhite              = 0x00000002,
TEX_BorderColor_Register                 = 0x00000003,
} TEX_BORDER_COLOR_TYPE;

/*
 * TEX_CHROMA_KEY enum
 */

typedef enum TEX_CHROMA_KEY {
TEX_ChromaKey_Disabled                   = 0x00000000,
TEX_ChromaKey_Kill                       = 0x00000001,
TEX_ChromaKey_Blend                      = 0x00000002,
TEX_ChromaKey_RESERVED_3                 = 0x00000003,
} TEX_CHROMA_KEY;

/*
 * TEX_CLAMP enum
 */

typedef enum TEX_CLAMP {
TEX_Clamp_Repeat                         = 0x00000000,
TEX_Clamp_Mirror                         = 0x00000001,
TEX_Clamp_ClampToLast                    = 0x00000002,
TEX_Clamp_MirrorOnceToLast               = 0x00000003,
TEX_Clamp_ClampHalfToBorder              = 0x00000004,
TEX_Clamp_MirrorOnceHalfToBorder         = 0x00000005,
TEX_Clamp_ClampToBorder                  = 0x00000006,
TEX_Clamp_MirrorOnceToBorder             = 0x00000007,
} TEX_CLAMP;

/*
 * TEX_COORD_TYPE enum
 */

typedef enum TEX_COORD_TYPE {
TEX_CoordType_Unnormalized               = 0x00000000,
TEX_CoordType_Normalized                 = 0x00000001,
} TEX_COORD_TYPE;

/*
 * TEX_DEPTH_COMPARE_FUNCTION enum
 */

typedef enum TEX_DEPTH_COMPARE_FUNCTION {
TEX_DepthCompareFunction_Never           = 0x00000000,
TEX_DepthCompareFunction_Less            = 0x00000001,
TEX_DepthCompareFunction_Equal           = 0x00000002,
TEX_DepthCompareFunction_LessEqual       = 0x00000003,
TEX_DepthCompareFunction_Greater         = 0x00000004,
TEX_DepthCompareFunction_NotEqual        = 0x00000005,
TEX_DepthCompareFunction_GreaterEqual    = 0x00000006,
TEX_DepthCompareFunction_Always          = 0x00000007,
} TEX_DEPTH_COMPARE_FUNCTION;

/*
 * TEX_DIM enum
 */

typedef enum TEX_DIM {
TEX_Dim_1D                               = 0x00000000,
TEX_Dim_2D                               = 0x00000001,
TEX_Dim_3D                               = 0x00000002,
TEX_Dim_CubeMap                          = 0x00000003,
TEX_Dim_1DArray                          = 0x00000004,
TEX_Dim_2DArray                          = 0x00000005,
TEX_Dim_2D_MSAA                          = 0x00000006,
TEX_Dim_2DArray_MSAA                     = 0x00000007,
} TEX_DIM;

/*
 * TEX_FORMAT_COMP enum
 */

typedef enum TEX_FORMAT_COMP {
TEX_FormatComp_Unsigned                  = 0x00000000,
TEX_FormatComp_Signed                    = 0x00000001,
TEX_FormatComp_UnsignedBiased            = 0x00000002,
TEX_FormatComp_RESERVED_3                = 0x00000003,
} TEX_FORMAT_COMP;

/*
 * TEX_MAX_ANISO_RATIO enum
 */

typedef enum TEX_MAX_ANISO_RATIO {
TEX_MaxAnisoRatio_1to1                   = 0x00000000,
TEX_MaxAnisoRatio_2to1                   = 0x00000001,
TEX_MaxAnisoRatio_4to1                   = 0x00000002,
TEX_MaxAnisoRatio_8to1                   = 0x00000003,
TEX_MaxAnisoRatio_16to1                  = 0x00000004,
TEX_MaxAnisoRatio_RESERVED_5             = 0x00000005,
TEX_MaxAnisoRatio_RESERVED_6             = 0x00000006,
TEX_MaxAnisoRatio_RESERVED_7             = 0x00000007,
} TEX_MAX_ANISO_RATIO;

/*
 * TEX_MIP_FILTER enum
 */

typedef enum TEX_MIP_FILTER {
TEX_MipFilter_None                       = 0x00000000,
TEX_MipFilter_Point                      = 0x00000001,
TEX_MipFilter_Linear                     = 0x00000002,
TEX_MipFilter_Point_Aniso_Adj            = 0x00000003,
} TEX_MIP_FILTER;

/*
 * TEX_REQUEST_SIZE enum
 */

typedef enum TEX_REQUEST_SIZE {
TEX_RequestSize_32B                      = 0x00000000,
TEX_RequestSize_64B                      = 0x00000001,
TEX_RequestSize_128B                     = 0x00000002,
TEX_RequestSize_2X64B                    = 0x00000003,
} TEX_REQUEST_SIZE;

/*
 * TEX_SAMPLER_TYPE enum
 */

typedef enum TEX_SAMPLER_TYPE {
TEX_SamplerType_Invalid                  = 0x00000000,
TEX_SamplerType_Valid                    = 0x00000001,
} TEX_SAMPLER_TYPE;

/*
 * TEX_XY_FILTER enum
 */

typedef enum TEX_XY_FILTER {
TEX_XYFilter_Point                       = 0x00000000,
TEX_XYFilter_Linear                      = 0x00000001,
TEX_XYFilter_AnisoPoint                  = 0x00000002,
TEX_XYFilter_AnisoLinear                 = 0x00000003,
} TEX_XY_FILTER;

/*
 * TEX_Z_FILTER enum
 */

typedef enum TEX_Z_FILTER {
TEX_ZFilter_None                         = 0x00000000,
TEX_ZFilter_Point                        = 0x00000001,
TEX_ZFilter_Linear                       = 0x00000002,
TEX_ZFilter_RESERVED_3                   = 0x00000003,
} TEX_Z_FILTER;

/*
 * VTX_CLAMP enum
 */

typedef enum VTX_CLAMP {
VTX_Clamp_ClampToZero                    = 0x00000000,
VTX_Clamp_ClampToNAN                     = 0x00000001,
} VTX_CLAMP;

/*
 * VTX_FETCH_TYPE enum
 */

typedef enum VTX_FETCH_TYPE {
VTX_FetchType_VertexData                 = 0x00000000,
VTX_FetchType_InstanceData               = 0x00000001,
VTX_FetchType_NoIndexOffset              = 0x00000002,
VTX_FetchType_RESERVED_3                 = 0x00000003,
} VTX_FETCH_TYPE;

/*
 * VTX_FORMAT_COMP_ALL enum
 */

typedef enum VTX_FORMAT_COMP_ALL {
VTX_FormatCompAll_Unsigned               = 0x00000000,
VTX_FormatCompAll_Signed                 = 0x00000001,
} VTX_FORMAT_COMP_ALL;

/*
 * VTX_MEM_REQUEST_SIZE enum
 */

typedef enum VTX_MEM_REQUEST_SIZE {
VTX_MemRequestSize_32B                   = 0x00000000,
VTX_MemRequestSize_64B                   = 0x00000001,
} VTX_MEM_REQUEST_SIZE;

/*
 * TVX_DATA_FORMAT enum
 */

typedef enum TVX_DATA_FORMAT {
TVX_FMT_INVALID                          = 0x00000000,
TVX_FMT_8                                = 0x00000001,
TVX_FMT_4_4                              = 0x00000002,
TVX_FMT_3_3_2                            = 0x00000003,
TVX_FMT_RESERVED_4                       = 0x00000004,
TVX_FMT_16                               = 0x00000005,
TVX_FMT_16_FLOAT                         = 0x00000006,
TVX_FMT_8_8                              = 0x00000007,
TVX_FMT_5_6_5                            = 0x00000008,
TVX_FMT_6_5_5                            = 0x00000009,
TVX_FMT_1_5_5_5                          = 0x0000000a,
TVX_FMT_4_4_4_4                          = 0x0000000b,
TVX_FMT_5_5_5_1                          = 0x0000000c,
TVX_FMT_32                               = 0x0000000d,
TVX_FMT_32_FLOAT                         = 0x0000000e,
TVX_FMT_16_16                            = 0x0000000f,
TVX_FMT_16_16_FLOAT                      = 0x00000010,
TVX_FMT_8_24                             = 0x00000011,
TVX_FMT_8_24_FLOAT                       = 0x00000012,
TVX_FMT_24_8                             = 0x00000013,
TVX_FMT_24_8_FLOAT                       = 0x00000014,
TVX_FMT_10_11_11                         = 0x00000015,
TVX_FMT_10_11_11_FLOAT                   = 0x00000016,
TVX_FMT_11_11_10                         = 0x00000017,
TVX_FMT_11_11_10_FLOAT                   = 0x00000018,
TVX_FMT_2_10_10_10                       = 0x00000019,
TVX_FMT_8_8_8_8                          = 0x0000001a,
TVX_FMT_10_10_10_2                       = 0x0000001b,
TVX_FMT_X24_8_32_FLOAT                   = 0x0000001c,
TVX_FMT_32_32                            = 0x0000001d,
TVX_FMT_32_32_FLOAT                      = 0x0000001e,
TVX_FMT_16_16_16_16                      = 0x0000001f,
TVX_FMT_16_16_16_16_FLOAT                = 0x00000020,
TVX_FMT_RESERVED_33                      = 0x00000021,
TVX_FMT_32_32_32_32                      = 0x00000022,
TVX_FMT_32_32_32_32_FLOAT                = 0x00000023,
TVX_FMT_RESERVED_36                      = 0x00000024,
TVX_FMT_1                                = 0x00000025,
TVX_FMT_1_REVERSED                       = 0x00000026,
TVX_FMT_GB_GR                            = 0x00000027,
TVX_FMT_BG_RG                            = 0x00000028,
TVX_FMT_32_AS_8                          = 0x00000029,
TVX_FMT_32_AS_8_8                        = 0x0000002a,
TVX_FMT_5_9_9_9_SHAREDEXP                = 0x0000002b,
TVX_FMT_8_8_8                            = 0x0000002c,
TVX_FMT_16_16_16                         = 0x0000002d,
TVX_FMT_16_16_16_FLOAT                   = 0x0000002e,
TVX_FMT_32_32_32                         = 0x0000002f,
TVX_FMT_32_32_32_FLOAT                   = 0x00000030,
TVX_FMT_BC1                              = 0x00000031,
TVX_FMT_BC2                              = 0x00000032,
TVX_FMT_BC3                              = 0x00000033,
TVX_FMT_BC4                              = 0x00000034,
TVX_FMT_BC5                              = 0x00000035,
TVX_FMT_APC0                             = 0x00000036,
TVX_FMT_APC1                             = 0x00000037,
TVX_FMT_APC2                             = 0x00000038,
TVX_FMT_APC3                             = 0x00000039,
TVX_FMT_APC4                             = 0x0000003a,
TVX_FMT_APC5                             = 0x0000003b,
TVX_FMT_APC6                             = 0x0000003c,
TVX_FMT_APC7                             = 0x0000003d,
TVX_FMT_CTX1                             = 0x0000003e,
TVX_FMT_RESERVED_63                      = 0x0000003f,
} TVX_DATA_FORMAT;

/*
 * TVX_DST_SEL enum
 */

typedef enum TVX_DST_SEL {
TVX_DstSel_X                             = 0x00000000,
TVX_DstSel_Y                             = 0x00000001,
TVX_DstSel_Z                             = 0x00000002,
TVX_DstSel_W                             = 0x00000003,
TVX_DstSel_0f                            = 0x00000004,
TVX_DstSel_1f                            = 0x00000005,
TVX_DstSel_RESERVED_6                    = 0x00000006,
TVX_DstSel_Mask                          = 0x00000007,
} TVX_DST_SEL;

/*
 * TVX_ENDIAN_SWAP enum
 */

typedef enum TVX_ENDIAN_SWAP {
TVX_EndianSwap_None                      = 0x00000000,
TVX_EndianSwap_8in16                     = 0x00000001,
TVX_EndianSwap_8in32                     = 0x00000002,
TVX_EndianSwap_8in64                     = 0x00000003,
} TVX_ENDIAN_SWAP;

/*
 * TVX_INST enum
 */

typedef enum TVX_INST {
TVX_Inst_NormalVertexFetch               = 0x00000000,
TVX_Inst_SemanticVertexFetch             = 0x00000001,
TVX_Inst_RESERVED_2                      = 0x00000002,
TVX_Inst_LD                              = 0x00000003,
TVX_Inst_GetTextureResInfo               = 0x00000004,
TVX_Inst_GetNumberOfSamples              = 0x00000005,
TVX_Inst_GetLOD                          = 0x00000006,
TVX_Inst_GetGradientsH                   = 0x00000007,
TVX_Inst_GetGradientsV                   = 0x00000008,
TVX_Inst_SetTextureOffsets               = 0x00000009,
TVX_Inst_KeepGradients                   = 0x0000000a,
TVX_Inst_SetGradientsH                   = 0x0000000b,
TVX_Inst_SetGradientsV                   = 0x0000000c,
TVX_Inst_Pass                            = 0x0000000d,
TVX_Inst_GetBufferResInfo                = 0x0000000e,
TVX_Inst_RESERVED_15                     = 0x0000000f,
TVX_Inst_Sample                          = 0x00000010,
TVX_Inst_Sample_L                        = 0x00000011,
TVX_Inst_Sample_LB                       = 0x00000012,
TVX_Inst_Sample_LZ                       = 0x00000013,
TVX_Inst_Sample_G                        = 0x00000014,
TVX_Inst_Gather4                         = 0x00000015,
TVX_Inst_Sample_G_LB                     = 0x00000016,
TVX_Inst_Gather4_O                       = 0x00000017,
TVX_Inst_Sample_C                        = 0x00000018,
TVX_Inst_Sample_C_L                      = 0x00000019,
TVX_Inst_Sample_C_LB                     = 0x0000001a,
TVX_Inst_Sample_C_LZ                     = 0x0000001b,
TVX_Inst_Sample_C_G                      = 0x0000001c,
TVX_Inst_Gather4_C                       = 0x0000001d,
TVX_Inst_Sample_C_G_LB                   = 0x0000001e,
TVX_Inst_Gather4_C_O                     = 0x0000001f,
} TVX_INST;

/*
 * TVX_NUM_FORMAT_ALL enum
 */

typedef enum TVX_NUM_FORMAT_ALL {
TVX_NumFormatAll_Norm                    = 0x00000000,
TVX_NumFormatAll_Int                     = 0x00000001,
TVX_NumFormatAll_Scaled                  = 0x00000002,
TVX_NumFormatAll_RESERVED_3              = 0x00000003,
} TVX_NUM_FORMAT_ALL;

/*
 * TVX_SRC_SEL enum
 */

typedef enum TVX_SRC_SEL {
TVX_SrcSel_X                             = 0x00000000,
TVX_SrcSel_Y                             = 0x00000001,
TVX_SrcSel_Z                             = 0x00000002,
TVX_SrcSel_W                             = 0x00000003,
TVX_SrcSel_0f                            = 0x00000004,
TVX_SrcSel_1f                            = 0x00000005,
} TVX_SRC_SEL;

/*
 * TVX_SRF_MODE_ALL enum
 */

typedef enum TVX_SRF_MODE_ALL {
TVX_SRFModeAll_ZCMO                      = 0x00000000,
TVX_SRFModeAll_NZ                        = 0x00000001,
} TVX_SRF_MODE_ALL;

/*
 * TVX_TYPE enum
 */

typedef enum TVX_TYPE {
TVX_Type_InvalidTextureResource          = 0x00000000,
TVX_Type_InvalidVertexBuffer             = 0x00000001,
TVX_Type_ValidTextureResource            = 0x00000002,
TVX_Type_ValidVertexBuffer               = 0x00000003,
} TVX_TYPE;

/*******************************************************
 * PA Enums
 *******************************************************/

/*
 * SU_PERFCNT_SEL enum
 */

typedef enum SU_PERFCNT_SEL {
PERF_PAPC_PASX_REQ                       = 0x00000000,
PERF_PAPC_PASX_DISABLE_PIPE              = 0x00000001,
PERF_PAPC_PASX_FIRST_VECTOR              = 0x00000002,
PERF_PAPC_PASX_SECOND_VECTOR             = 0x00000003,
PERF_PAPC_PASX_FIRST_DEAD                = 0x00000004,
PERF_PAPC_PASX_SECOND_DEAD               = 0x00000005,
PERF_PAPC_PASX_VTX_KILL_DISCARD          = 0x00000006,
PERF_PAPC_PASX_VTX_NAN_DISCARD           = 0x00000007,
PERF_PAPC_PA_INPUT_PRIM                  = 0x00000008,
PERF_PAPC_PA_INPUT_NULL_PRIM             = 0x00000009,
PERF_PAPC_PA_INPUT_EVENT_FLAG            = 0x0000000a,
PERF_PAPC_PA_INPUT_FIRST_PRIM_SLOT       = 0x0000000b,
PERF_PAPC_PA_INPUT_END_OF_PACKET         = 0x0000000c,
PERF_PAPC_PA_INPUT_EXTENDED_EVENT        = 0x0000000d,
PERF_PAPC_CLPR_CULL_PRIM                 = 0x0000000e,
PERF_PAPC_CLPR_VVUCP_CULL_PRIM           = 0x0000000f,
PERF_PAPC_CLPR_VV_CULL_PRIM              = 0x00000010,
PERF_PAPC_CLPR_UCP_CULL_PRIM             = 0x00000011,
PERF_PAPC_CLPR_VTX_KILL_CULL_PRIM        = 0x00000012,
PERF_PAPC_CLPR_VTX_NAN_CULL_PRIM         = 0x00000013,
PERF_PAPC_CLPR_CULL_TO_NULL_PRIM         = 0x00000014,
PERF_PAPC_CLPR_VVUCP_CLIP_PRIM           = 0x00000015,
PERF_PAPC_CLPR_VV_CLIP_PRIM              = 0x00000016,
PERF_PAPC_CLPR_UCP_CLIP_PRIM             = 0x00000017,
PERF_PAPC_CLPR_POINT_CLIP_CANDIDATE      = 0x00000018,
PERF_PAPC_CLPR_CLIP_PLANE_CNT_1          = 0x00000019,
PERF_PAPC_CLPR_CLIP_PLANE_CNT_2          = 0x0000001a,
PERF_PAPC_CLPR_CLIP_PLANE_CNT_3          = 0x0000001b,
PERF_PAPC_CLPR_CLIP_PLANE_CNT_4          = 0x0000001c,
PERF_PAPC_CLPR_CLIP_PLANE_CNT_5_8        = 0x0000001d,
PERF_PAPC_CLPR_CLIP_PLANE_CNT_9_12       = 0x0000001e,
PERF_PAPC_CLPR_CLIP_PLANE_NEAR           = 0x0000001f,
PERF_PAPC_CLPR_CLIP_PLANE_FAR            = 0x00000020,
PERF_PAPC_CLPR_CLIP_PLANE_LEFT           = 0x00000021,
PERF_PAPC_CLPR_CLIP_PLANE_RIGHT          = 0x00000022,
PERF_PAPC_CLPR_CLIP_PLANE_TOP            = 0x00000023,
PERF_PAPC_CLPR_CLIP_PLANE_BOTTOM         = 0x00000024,
PERF_PAPC_CLPR_GSC_KILL_CULL_PRIM        = 0x00000025,
PERF_PAPC_CLPR_RASTER_KILL_CULL_PRIM     = 0x00000026,
PERF_PAPC_CLSM_NULL_PRIM                 = 0x00000027,
PERF_PAPC_CLSM_TOTALLY_VISIBLE_PRIM      = 0x00000028,
PERF_PAPC_CLSM_CULL_TO_NULL_PRIM         = 0x00000029,
PERF_PAPC_CLSM_OUT_PRIM_CNT_1            = 0x0000002a,
PERF_PAPC_CLSM_OUT_PRIM_CNT_2            = 0x0000002b,
PERF_PAPC_CLSM_OUT_PRIM_CNT_3            = 0x0000002c,
PERF_PAPC_CLSM_OUT_PRIM_CNT_4            = 0x0000002d,
PERF_PAPC_CLSM_OUT_PRIM_CNT_5_8          = 0x0000002e,
PERF_PAPC_CLSM_OUT_PRIM_CNT_9_13         = 0x0000002f,
PERF_PAPC_CLIPGA_VTE_KILL_PRIM           = 0x00000030,
PERF_PAPC_SU_INPUT_PRIM                  = 0x00000031,
PERF_PAPC_SU_INPUT_CLIP_PRIM             = 0x00000032,
PERF_PAPC_SU_INPUT_NULL_PRIM             = 0x00000033,
PERF_PAPC_SU_INPUT_PRIM_DUAL             = 0x00000034,
PERF_PAPC_SU_INPUT_CLIP_PRIM_DUAL        = 0x00000035,
PERF_PAPC_SU_ZERO_AREA_CULL_PRIM         = 0x00000036,
PERF_PAPC_SU_BACK_FACE_CULL_PRIM         = 0x00000037,
PERF_PAPC_SU_FRONT_FACE_CULL_PRIM        = 0x00000038,
PERF_PAPC_SU_POLYMODE_FACE_CULL          = 0x00000039,
PERF_PAPC_SU_POLYMODE_BACK_CULL          = 0x0000003a,
PERF_PAPC_SU_POLYMODE_FRONT_CULL         = 0x0000003b,
PERF_PAPC_SU_POLYMODE_INVALID_FILL       = 0x0000003c,
PERF_PAPC_SU_OUTPUT_PRIM                 = 0x0000003d,
PERF_PAPC_SU_OUTPUT_CLIP_PRIM            = 0x0000003e,
PERF_PAPC_SU_OUTPUT_NULL_PRIM            = 0x0000003f,
PERF_PAPC_SU_OUTPUT_EVENT_FLAG           = 0x00000040,
PERF_PAPC_SU_OUTPUT_FIRST_PRIM_SLOT      = 0x00000041,
PERF_PAPC_SU_OUTPUT_END_OF_PACKET        = 0x00000042,
PERF_PAPC_SU_OUTPUT_POLYMODE_FACE        = 0x00000043,
PERF_PAPC_SU_OUTPUT_POLYMODE_BACK        = 0x00000044,
PERF_PAPC_SU_OUTPUT_POLYMODE_FRONT       = 0x00000045,
PERF_PAPC_SU_OUT_CLIP_POLYMODE_FACE      = 0x00000046,
PERF_PAPC_SU_OUT_CLIP_POLYMODE_BACK      = 0x00000047,
PERF_PAPC_SU_OUT_CLIP_POLYMODE_FRONT     = 0x00000048,
PERF_PAPC_SU_OUTPUT_PRIM_DUAL            = 0x00000049,
PERF_PAPC_SU_OUTPUT_CLIP_PRIM_DUAL       = 0x0000004a,
PERF_PAPC_SU_OUTPUT_POLYMODE_DUAL        = 0x0000004b,
PERF_PAPC_SU_OUTPUT_CLIP_POLYMODE_DUAL   = 0x0000004c,
PERF_PAPC_PASX_REQ_IDLE                  = 0x0000004d,
PERF_PAPC_PASX_REQ_BUSY                  = 0x0000004e,
PERF_PAPC_PASX_REQ_STALLED               = 0x0000004f,
PERF_PAPC_PASX_REC_IDLE                  = 0x00000050,
PERF_PAPC_PASX_REC_BUSY                  = 0x00000051,
PERF_PAPC_PASX_REC_STARVED_SX            = 0x00000052,
PERF_PAPC_PASX_REC_STALLED               = 0x00000053,
PERF_PAPC_PASX_REC_STALLED_POS_MEM       = 0x00000054,
PERF_PAPC_PASX_REC_STALLED_CCGSM_IN      = 0x00000055,
PERF_PAPC_CCGSM_IDLE                     = 0x00000056,
PERF_PAPC_CCGSM_BUSY                     = 0x00000057,
PERF_PAPC_CCGSM_STALLED                  = 0x00000058,
PERF_PAPC_CLPRIM_IDLE                    = 0x00000059,
PERF_PAPC_CLPRIM_BUSY                    = 0x0000005a,
PERF_PAPC_CLPRIM_STALLED                 = 0x0000005b,
PERF_PAPC_CLPRIM_STARVED_CCGSM           = 0x0000005c,
PERF_PAPC_CLIPSM_IDLE                    = 0x0000005d,
PERF_PAPC_CLIPSM_BUSY                    = 0x0000005e,
PERF_PAPC_CLIPSM_WAIT_CLIP_VERT_ENGH     = 0x0000005f,
PERF_PAPC_CLIPSM_WAIT_HIGH_PRI_SEQ       = 0x00000060,
PERF_PAPC_CLIPSM_WAIT_CLIPGA             = 0x00000061,
PERF_PAPC_CLIPSM_WAIT_AVAIL_VTE_CLIP     = 0x00000062,
PERF_PAPC_CLIPSM_WAIT_CLIP_OUTSM         = 0x00000063,
PERF_PAPC_CLIPGA_IDLE                    = 0x00000064,
PERF_PAPC_CLIPGA_BUSY                    = 0x00000065,
PERF_PAPC_CLIPGA_STARVED_VTE_CLIP        = 0x00000066,
PERF_PAPC_CLIPGA_STALLED                 = 0x00000067,
PERF_PAPC_CLIP_IDLE                      = 0x00000068,
PERF_PAPC_CLIP_BUSY                      = 0x00000069,
PERF_PAPC_SU_IDLE                        = 0x0000006a,
PERF_PAPC_SU_BUSY                        = 0x0000006b,
PERF_PAPC_SU_STARVED_CLIP                = 0x0000006c,
PERF_PAPC_SU_STALLED_SC                  = 0x0000006d,
PERF_PAPC_CL_DYN_SCLK_VLD                = 0x0000006e,
PERF_PAPC_SU_DYN_SCLK_VLD                = 0x0000006f,
PERF_PAPC_PA_REG_SCLK_VLD                = 0x00000070,
PERF_PAPC_SU_MULTI_GPU_PRIM_FILTER_CULL  = 0x00000071,
PERF_PAPC_PASX_SE0_REQ                   = 0x00000072,
PERF_PAPC_PASX_SE1_REQ                   = 0x00000073,
PERF_PAPC_PASX_SE0_FIRST_VECTOR          = 0x00000074,
PERF_PAPC_PASX_SE0_SECOND_VECTOR         = 0x00000075,
PERF_PAPC_PASX_SE1_FIRST_VECTOR          = 0x00000076,
PERF_PAPC_PASX_SE1_SECOND_VECTOR         = 0x00000077,
PERF_PAPC_SU_SE0_PRIM_FILTER_CULL        = 0x00000078,
PERF_PAPC_SU_SE1_PRIM_FILTER_CULL        = 0x00000079,
PERF_PAPC_SU_SE01_PRIM_FILTER_CULL       = 0x0000007a,
PERF_PAPC_SU_SE0_OUTPUT_PRIM             = 0x0000007b,
PERF_PAPC_SU_SE1_OUTPUT_PRIM             = 0x0000007c,
PERF_PAPC_SU_SE01_OUTPUT_PRIM            = 0x0000007d,
PERF_PAPC_SU_SE0_OUTPUT_NULL_PRIM        = 0x0000007e,
PERF_PAPC_SU_SE1_OUTPUT_NULL_PRIM        = 0x0000007f,
PERF_PAPC_SU_SE01_OUTPUT_NULL_PRIM       = 0x00000080,
PERF_PAPC_SU_SE0_OUTPUT_FIRST_PRIM_SLOT  = 0x00000081,
PERF_PAPC_SU_SE1_OUTPUT_FIRST_PRIM_SLOT  = 0x00000082,
PERF_PAPC_SU_SE0_STALLED_SC              = 0x00000083,
PERF_PAPC_SU_SE1_STALLED_SC              = 0x00000084,
PERF_PAPC_SU_SE01_STALLED_SC             = 0x00000085,
PERF_PAPC_CLSM_CLIPPING_PRIM             = 0x00000086,
PERF_PAPC_SU_CULLED_PRIM                 = 0x00000087,
PERF_PAPC_SU_OUTPUT_EOPG                 = 0x00000088,
PERF_PAPC_SU_SE2_PRIM_FILTER_CULL        = 0x00000089,
PERF_PAPC_SU_SE3_PRIM_FILTER_CULL        = 0x0000008a,
PERF_PAPC_SU_SE2_OUTPUT_PRIM             = 0x0000008b,
PERF_PAPC_SU_SE3_OUTPUT_PRIM             = 0x0000008c,
PERF_PAPC_SU_SE2_OUTPUT_NULL_PRIM        = 0x0000008d,
PERF_PAPC_SU_SE3_OUTPUT_NULL_PRIM        = 0x0000008e,
PERF_PAPC_SU_SE0_OUTPUT_END_OF_PACKET    = 0x0000008f,
PERF_PAPC_SU_SE1_OUTPUT_END_OF_PACKET    = 0x00000090,
PERF_PAPC_SU_SE2_OUTPUT_END_OF_PACKET    = 0x00000091,
PERF_PAPC_SU_SE3_OUTPUT_END_OF_PACKET    = 0x00000092,
PERF_PAPC_SU_SE0_OUTPUT_EOPG             = 0x00000093,
PERF_PAPC_SU_SE1_OUTPUT_EOPG             = 0x00000094,
PERF_PAPC_SU_SE2_OUTPUT_EOPG             = 0x00000095,
PERF_PAPC_SU_SE3_OUTPUT_EOPG             = 0x00000096,
PERF_PAPC_SU_SE2_STALLED_SC              = 0x00000097,
PERF_PAPC_SU_SE3_STALLED_SC              = 0x00000098,
} SU_PERFCNT_SEL;

/*
 * SC_PERFCNT_SEL enum
 */

typedef enum SC_PERFCNT_SEL {
SC_SRPS_WINDOW_VALID                     = 0x00000000,
SC_PSSW_WINDOW_VALID                     = 0x00000001,
SC_TPQZ_WINDOW_VALID                     = 0x00000002,
SC_QZQP_WINDOW_VALID                     = 0x00000003,
SC_TRPK_WINDOW_VALID                     = 0x00000004,
SC_SRPS_WINDOW_VALID_BUSY                = 0x00000005,
SC_PSSW_WINDOW_VALID_BUSY                = 0x00000006,
SC_TPQZ_WINDOW_VALID_BUSY                = 0x00000007,
SC_QZQP_WINDOW_VALID_BUSY                = 0x00000008,
SC_TRPK_WINDOW_VALID_BUSY                = 0x00000009,
SC_STARVED_BY_PA                         = 0x0000000a,
SC_STALLED_BY_PRIMFIFO                   = 0x0000000b,
SC_STALLED_BY_DB_TILE                    = 0x0000000c,
SC_STARVED_BY_DB_TILE                    = 0x0000000d,
SC_STALLED_BY_TILEORDERFIFO              = 0x0000000e,
SC_STALLED_BY_TILEFIFO                   = 0x0000000f,
SC_STALLED_BY_DB_QUAD                    = 0x00000010,
SC_STARVED_BY_DB_QUAD                    = 0x00000011,
SC_STALLED_BY_QUADFIFO                   = 0x00000012,
SC_STALLED_BY_BCI                        = 0x00000013,
SC_STALLED_BY_SPI                        = 0x00000014,
SC_SCISSOR_DISCARD                       = 0x00000015,
SC_BB_DISCARD                            = 0x00000016,
SC_SUPERTILE_COUNT                       = 0x00000017,
SC_SUPERTILE_PER_PRIM_H0                 = 0x00000018,
SC_SUPERTILE_PER_PRIM_H1                 = 0x00000019,
SC_SUPERTILE_PER_PRIM_H2                 = 0x0000001a,
SC_SUPERTILE_PER_PRIM_H3                 = 0x0000001b,
SC_SUPERTILE_PER_PRIM_H4                 = 0x0000001c,
SC_SUPERTILE_PER_PRIM_H5                 = 0x0000001d,
SC_SUPERTILE_PER_PRIM_H6                 = 0x0000001e,
SC_SUPERTILE_PER_PRIM_H7                 = 0x0000001f,
SC_SUPERTILE_PER_PRIM_H8                 = 0x00000020,
SC_SUPERTILE_PER_PRIM_H9                 = 0x00000021,
SC_SUPERTILE_PER_PRIM_H10                = 0x00000022,
SC_SUPERTILE_PER_PRIM_H11                = 0x00000023,
SC_SUPERTILE_PER_PRIM_H12                = 0x00000024,
SC_SUPERTILE_PER_PRIM_H13                = 0x00000025,
SC_SUPERTILE_PER_PRIM_H14                = 0x00000026,
SC_SUPERTILE_PER_PRIM_H15                = 0x00000027,
SC_SUPERTILE_PER_PRIM_H16                = 0x00000028,
SC_TILE_PER_PRIM_H0                      = 0x00000029,
SC_TILE_PER_PRIM_H1                      = 0x0000002a,
SC_TILE_PER_PRIM_H2                      = 0x0000002b,
SC_TILE_PER_PRIM_H3                      = 0x0000002c,
SC_TILE_PER_PRIM_H4                      = 0x0000002d,
SC_TILE_PER_PRIM_H5                      = 0x0000002e,
SC_TILE_PER_PRIM_H6                      = 0x0000002f,
SC_TILE_PER_PRIM_H7                      = 0x00000030,
SC_TILE_PER_PRIM_H8                      = 0x00000031,
SC_TILE_PER_PRIM_H9                      = 0x00000032,
SC_TILE_PER_PRIM_H10                     = 0x00000033,
SC_TILE_PER_PRIM_H11                     = 0x00000034,
SC_TILE_PER_PRIM_H12                     = 0x00000035,
SC_TILE_PER_PRIM_H13                     = 0x00000036,
SC_TILE_PER_PRIM_H14                     = 0x00000037,
SC_TILE_PER_PRIM_H15                     = 0x00000038,
SC_TILE_PER_PRIM_H16                     = 0x00000039,
SC_TILE_PER_SUPERTILE_H0                 = 0x0000003a,
SC_TILE_PER_SUPERTILE_H1                 = 0x0000003b,
SC_TILE_PER_SUPERTILE_H2                 = 0x0000003c,
SC_TILE_PER_SUPERTILE_H3                 = 0x0000003d,
SC_TILE_PER_SUPERTILE_H4                 = 0x0000003e,
SC_TILE_PER_SUPERTILE_H5                 = 0x0000003f,
SC_TILE_PER_SUPERTILE_H6                 = 0x00000040,
SC_TILE_PER_SUPERTILE_H7                 = 0x00000041,
SC_TILE_PER_SUPERTILE_H8                 = 0x00000042,
SC_TILE_PER_SUPERTILE_H9                 = 0x00000043,
SC_TILE_PER_SUPERTILE_H10                = 0x00000044,
SC_TILE_PER_SUPERTILE_H11                = 0x00000045,
SC_TILE_PER_SUPERTILE_H12                = 0x00000046,
SC_TILE_PER_SUPERTILE_H13                = 0x00000047,
SC_TILE_PER_SUPERTILE_H14                = 0x00000048,
SC_TILE_PER_SUPERTILE_H15                = 0x00000049,
SC_TILE_PER_SUPERTILE_H16                = 0x0000004a,
SC_TILE_PICKED_H1                        = 0x0000004b,
SC_TILE_PICKED_H2                        = 0x0000004c,
SC_TILE_PICKED_H3                        = 0x0000004d,
SC_TILE_PICKED_H4                        = 0x0000004e,
SC_QZ0_MULTI_GPU_TILE_DISCARD            = 0x0000004f,
SC_QZ1_MULTI_GPU_TILE_DISCARD            = 0x00000050,
SC_QZ2_MULTI_GPU_TILE_DISCARD            = 0x00000051,
SC_QZ3_MULTI_GPU_TILE_DISCARD            = 0x00000052,
SC_QZ0_TILE_COUNT                        = 0x00000053,
SC_QZ1_TILE_COUNT                        = 0x00000054,
SC_QZ2_TILE_COUNT                        = 0x00000055,
SC_QZ3_TILE_COUNT                        = 0x00000056,
SC_QZ0_TILE_COVERED_COUNT                = 0x00000057,
SC_QZ1_TILE_COVERED_COUNT                = 0x00000058,
SC_QZ2_TILE_COVERED_COUNT                = 0x00000059,
SC_QZ3_TILE_COVERED_COUNT                = 0x0000005a,
SC_QZ0_TILE_NOT_COVERED_COUNT            = 0x0000005b,
SC_QZ1_TILE_NOT_COVERED_COUNT            = 0x0000005c,
SC_QZ2_TILE_NOT_COVERED_COUNT            = 0x0000005d,
SC_QZ3_TILE_NOT_COVERED_COUNT            = 0x0000005e,
SC_QZ0_QUAD_PER_TILE_H0                  = 0x0000005f,
SC_QZ0_QUAD_PER_TILE_H1                  = 0x00000060,
SC_QZ0_QUAD_PER_TILE_H2                  = 0x00000061,
SC_QZ0_QUAD_PER_TILE_H3                  = 0x00000062,
SC_QZ0_QUAD_PER_TILE_H4                  = 0x00000063,
SC_QZ0_QUAD_PER_TILE_H5                  = 0x00000064,
SC_QZ0_QUAD_PER_TILE_H6                  = 0x00000065,
SC_QZ0_QUAD_PER_TILE_H7                  = 0x00000066,
SC_QZ0_QUAD_PER_TILE_H8                  = 0x00000067,
SC_QZ0_QUAD_PER_TILE_H9                  = 0x00000068,
SC_QZ0_QUAD_PER_TILE_H10                 = 0x00000069,
SC_QZ0_QUAD_PER_TILE_H11                 = 0x0000006a,
SC_QZ0_QUAD_PER_TILE_H12                 = 0x0000006b,
SC_QZ0_QUAD_PER_TILE_H13                 = 0x0000006c,
SC_QZ0_QUAD_PER_TILE_H14                 = 0x0000006d,
SC_QZ0_QUAD_PER_TILE_H15                 = 0x0000006e,
SC_QZ0_QUAD_PER_TILE_H16                 = 0x0000006f,
SC_QZ1_QUAD_PER_TILE_H0                  = 0x00000070,
SC_QZ1_QUAD_PER_TILE_H1                  = 0x00000071,
SC_QZ1_QUAD_PER_TILE_H2                  = 0x00000072,
SC_QZ1_QUAD_PER_TILE_H3                  = 0x00000073,
SC_QZ1_QUAD_PER_TILE_H4                  = 0x00000074,
SC_QZ1_QUAD_PER_TILE_H5                  = 0x00000075,
SC_QZ1_QUAD_PER_TILE_H6                  = 0x00000076,
SC_QZ1_QUAD_PER_TILE_H7                  = 0x00000077,
SC_QZ1_QUAD_PER_TILE_H8                  = 0x00000078,
SC_QZ1_QUAD_PER_TILE_H9                  = 0x00000079,
SC_QZ1_QUAD_PER_TILE_H10                 = 0x0000007a,
SC_QZ1_QUAD_PER_TILE_H11                 = 0x0000007b,
SC_QZ1_QUAD_PER_TILE_H12                 = 0x0000007c,
SC_QZ1_QUAD_PER_TILE_H13                 = 0x0000007d,
SC_QZ1_QUAD_PER_TILE_H14                 = 0x0000007e,
SC_QZ1_QUAD_PER_TILE_H15                 = 0x0000007f,
SC_QZ1_QUAD_PER_TILE_H16                 = 0x00000080,
SC_QZ2_QUAD_PER_TILE_H0                  = 0x00000081,
SC_QZ2_QUAD_PER_TILE_H1                  = 0x00000082,
SC_QZ2_QUAD_PER_TILE_H2                  = 0x00000083,
SC_QZ2_QUAD_PER_TILE_H3                  = 0x00000084,
SC_QZ2_QUAD_PER_TILE_H4                  = 0x00000085,
SC_QZ2_QUAD_PER_TILE_H5                  = 0x00000086,
SC_QZ2_QUAD_PER_TILE_H6                  = 0x00000087,
SC_QZ2_QUAD_PER_TILE_H7                  = 0x00000088,
SC_QZ2_QUAD_PER_TILE_H8                  = 0x00000089,
SC_QZ2_QUAD_PER_TILE_H9                  = 0x0000008a,
SC_QZ2_QUAD_PER_TILE_H10                 = 0x0000008b,
SC_QZ2_QUAD_PER_TILE_H11                 = 0x0000008c,
SC_QZ2_QUAD_PER_TILE_H12                 = 0x0000008d,
SC_QZ2_QUAD_PER_TILE_H13                 = 0x0000008e,
SC_QZ2_QUAD_PER_TILE_H14                 = 0x0000008f,
SC_QZ2_QUAD_PER_TILE_H15                 = 0x00000090,
SC_QZ2_QUAD_PER_TILE_H16                 = 0x00000091,
SC_QZ3_QUAD_PER_TILE_H0                  = 0x00000092,
SC_QZ3_QUAD_PER_TILE_H1                  = 0x00000093,
SC_QZ3_QUAD_PER_TILE_H2                  = 0x00000094,
SC_QZ3_QUAD_PER_TILE_H3                  = 0x00000095,
SC_QZ3_QUAD_PER_TILE_H4                  = 0x00000096,
SC_QZ3_QUAD_PER_TILE_H5                  = 0x00000097,
SC_QZ3_QUAD_PER_TILE_H6                  = 0x00000098,
SC_QZ3_QUAD_PER_TILE_H7                  = 0x00000099,
SC_QZ3_QUAD_PER_TILE_H8                  = 0x0000009a,
SC_QZ3_QUAD_PER_TILE_H9                  = 0x0000009b,
SC_QZ3_QUAD_PER_TILE_H10                 = 0x0000009c,
SC_QZ3_QUAD_PER_TILE_H11                 = 0x0000009d,
SC_QZ3_QUAD_PER_TILE_H12                 = 0x0000009e,
SC_QZ3_QUAD_PER_TILE_H13                 = 0x0000009f,
SC_QZ3_QUAD_PER_TILE_H14                 = 0x000000a0,
SC_QZ3_QUAD_PER_TILE_H15                 = 0x000000a1,
SC_QZ3_QUAD_PER_TILE_H16                 = 0x000000a2,
SC_QZ0_QUAD_COUNT                        = 0x000000a3,
SC_QZ1_QUAD_COUNT                        = 0x000000a4,
SC_QZ2_QUAD_COUNT                        = 0x000000a5,
SC_QZ3_QUAD_COUNT                        = 0x000000a6,
SC_P0_HIZ_TILE_COUNT                     = 0x000000a7,
SC_P1_HIZ_TILE_COUNT                     = 0x000000a8,
SC_P2_HIZ_TILE_COUNT                     = 0x000000a9,
SC_P3_HIZ_TILE_COUNT                     = 0x000000aa,
SC_P0_HIZ_QUAD_PER_TILE_H0               = 0x000000ab,
SC_P0_HIZ_QUAD_PER_TILE_H1               = 0x000000ac,
SC_P0_HIZ_QUAD_PER_TILE_H2               = 0x000000ad,
SC_P0_HIZ_QUAD_PER_TILE_H3               = 0x000000ae,
SC_P0_HIZ_QUAD_PER_TILE_H4               = 0x000000af,
SC_P0_HIZ_QUAD_PER_TILE_H5               = 0x000000b0,
SC_P0_HIZ_QUAD_PER_TILE_H6               = 0x000000b1,
SC_P0_HIZ_QUAD_PER_TILE_H7               = 0x000000b2,
SC_P0_HIZ_QUAD_PER_TILE_H8               = 0x000000b3,
SC_P0_HIZ_QUAD_PER_TILE_H9               = 0x000000b4,
SC_P0_HIZ_QUAD_PER_TILE_H10              = 0x000000b5,
SC_P0_HIZ_QUAD_PER_TILE_H11              = 0x000000b6,
SC_P0_HIZ_QUAD_PER_TILE_H12              = 0x000000b7,
SC_P0_HIZ_QUAD_PER_TILE_H13              = 0x000000b8,
SC_P0_HIZ_QUAD_PER_TILE_H14              = 0x000000b9,
SC_P0_HIZ_QUAD_PER_TILE_H15              = 0x000000ba,
SC_P0_HIZ_QUAD_PER_TILE_H16              = 0x000000bb,
SC_P1_HIZ_QUAD_PER_TILE_H0               = 0x000000bc,
SC_P1_HIZ_QUAD_PER_TILE_H1               = 0x000000bd,
SC_P1_HIZ_QUAD_PER_TILE_H2               = 0x000000be,
SC_P1_HIZ_QUAD_PER_TILE_H3               = 0x000000bf,
SC_P1_HIZ_QUAD_PER_TILE_H4               = 0x000000c0,
SC_P1_HIZ_QUAD_PER_TILE_H5               = 0x000000c1,
SC_P1_HIZ_QUAD_PER_TILE_H6               = 0x000000c2,
SC_P1_HIZ_QUAD_PER_TILE_H7               = 0x000000c3,
SC_P1_HIZ_QUAD_PER_TILE_H8               = 0x000000c4,
SC_P1_HIZ_QUAD_PER_TILE_H9               = 0x000000c5,
SC_P1_HIZ_QUAD_PER_TILE_H10              = 0x000000c6,
SC_P1_HIZ_QUAD_PER_TILE_H11              = 0x000000c7,
SC_P1_HIZ_QUAD_PER_TILE_H12              = 0x000000c8,
SC_P1_HIZ_QUAD_PER_TILE_H13              = 0x000000c9,
SC_P1_HIZ_QUAD_PER_TILE_H14              = 0x000000ca,
SC_P1_HIZ_QUAD_PER_TILE_H15              = 0x000000cb,
SC_P1_HIZ_QUAD_PER_TILE_H16              = 0x000000cc,
SC_P2_HIZ_QUAD_PER_TILE_H0               = 0x000000cd,
SC_P2_HIZ_QUAD_PER_TILE_H1               = 0x000000ce,
SC_P2_HIZ_QUAD_PER_TILE_H2               = 0x000000cf,
SC_P2_HIZ_QUAD_PER_TILE_H3               = 0x000000d0,
SC_P2_HIZ_QUAD_PER_TILE_H4               = 0x000000d1,
SC_P2_HIZ_QUAD_PER_TILE_H5               = 0x000000d2,
SC_P2_HIZ_QUAD_PER_TILE_H6               = 0x000000d3,
SC_P2_HIZ_QUAD_PER_TILE_H7               = 0x000000d4,
SC_P2_HIZ_QUAD_PER_TILE_H8               = 0x000000d5,
SC_P2_HIZ_QUAD_PER_TILE_H9               = 0x000000d6,
SC_P2_HIZ_QUAD_PER_TILE_H10              = 0x000000d7,
SC_P2_HIZ_QUAD_PER_TILE_H11              = 0x000000d8,
SC_P2_HIZ_QUAD_PER_TILE_H12              = 0x000000d9,
SC_P2_HIZ_QUAD_PER_TILE_H13              = 0x000000da,
SC_P2_HIZ_QUAD_PER_TILE_H14              = 0x000000db,
SC_P2_HIZ_QUAD_PER_TILE_H15              = 0x000000dc,
SC_P2_HIZ_QUAD_PER_TILE_H16              = 0x000000dd,
SC_P3_HIZ_QUAD_PER_TILE_H0               = 0x000000de,
SC_P3_HIZ_QUAD_PER_TILE_H1               = 0x000000df,
SC_P3_HIZ_QUAD_PER_TILE_H2               = 0x000000e0,
SC_P3_HIZ_QUAD_PER_TILE_H3               = 0x000000e1,
SC_P3_HIZ_QUAD_PER_TILE_H4               = 0x000000e2,
SC_P3_HIZ_QUAD_PER_TILE_H5               = 0x000000e3,
SC_P3_HIZ_QUAD_PER_TILE_H6               = 0x000000e4,
SC_P3_HIZ_QUAD_PER_TILE_H7               = 0x000000e5,
SC_P3_HIZ_QUAD_PER_TILE_H8               = 0x000000e6,
SC_P3_HIZ_QUAD_PER_TILE_H9               = 0x000000e7,
SC_P3_HIZ_QUAD_PER_TILE_H10              = 0x000000e8,
SC_P3_HIZ_QUAD_PER_TILE_H11              = 0x000000e9,
SC_P3_HIZ_QUAD_PER_TILE_H12              = 0x000000ea,
SC_P3_HIZ_QUAD_PER_TILE_H13              = 0x000000eb,
SC_P3_HIZ_QUAD_PER_TILE_H14              = 0x000000ec,
SC_P3_HIZ_QUAD_PER_TILE_H15              = 0x000000ed,
SC_P3_HIZ_QUAD_PER_TILE_H16              = 0x000000ee,
SC_P0_HIZ_QUAD_COUNT                     = 0x000000ef,
SC_P1_HIZ_QUAD_COUNT                     = 0x000000f0,
SC_P2_HIZ_QUAD_COUNT                     = 0x000000f1,
SC_P3_HIZ_QUAD_COUNT                     = 0x000000f2,
SC_P0_DETAIL_QUAD_COUNT                  = 0x000000f3,
SC_P1_DETAIL_QUAD_COUNT                  = 0x000000f4,
SC_P2_DETAIL_QUAD_COUNT                  = 0x000000f5,
SC_P3_DETAIL_QUAD_COUNT                  = 0x000000f6,
SC_P0_DETAIL_QUAD_WITH_1_PIX             = 0x000000f7,
SC_P0_DETAIL_QUAD_WITH_2_PIX             = 0x000000f8,
SC_P0_DETAIL_QUAD_WITH_3_PIX             = 0x000000f9,
SC_P0_DETAIL_QUAD_WITH_4_PIX             = 0x000000fa,
SC_P1_DETAIL_QUAD_WITH_1_PIX             = 0x000000fb,
SC_P1_DETAIL_QUAD_WITH_2_PIX             = 0x000000fc,
SC_P1_DETAIL_QUAD_WITH_3_PIX             = 0x000000fd,
SC_P1_DETAIL_QUAD_WITH_4_PIX             = 0x000000fe,
SC_P2_DETAIL_QUAD_WITH_1_PIX             = 0x000000ff,
SC_P2_DETAIL_QUAD_WITH_2_PIX             = 0x00000100,
SC_P2_DETAIL_QUAD_WITH_3_PIX             = 0x00000101,
SC_P2_DETAIL_QUAD_WITH_4_PIX             = 0x00000102,
SC_P3_DETAIL_QUAD_WITH_1_PIX             = 0x00000103,
SC_P3_DETAIL_QUAD_WITH_2_PIX             = 0x00000104,
SC_P3_DETAIL_QUAD_WITH_3_PIX             = 0x00000105,
SC_P3_DETAIL_QUAD_WITH_4_PIX             = 0x00000106,
SC_EARLYZ_QUAD_COUNT                     = 0x00000107,
SC_EARLYZ_QUAD_WITH_1_PIX                = 0x00000108,
SC_EARLYZ_QUAD_WITH_2_PIX                = 0x00000109,
SC_EARLYZ_QUAD_WITH_3_PIX                = 0x0000010a,
SC_EARLYZ_QUAD_WITH_4_PIX                = 0x0000010b,
SC_PKR_QUAD_PER_ROW_H1                   = 0x0000010c,
SC_PKR_QUAD_PER_ROW_H2                   = 0x0000010d,
SC_PKR_4X2_QUAD_SPLIT                    = 0x0000010e,
SC_PKR_4X2_FILL_QUAD                     = 0x0000010f,
SC_PKR_END_OF_VECTOR                     = 0x00000110,
SC_PKR_CONTROL_XFER                      = 0x00000111,
SC_PKR_DBHANG_FORCE_EOV                  = 0x00000112,
SC_REG_SCLK_BUSY                         = 0x00000113,
SC_GRP0_DYN_SCLK_BUSY                    = 0x00000114,
SC_GRP1_DYN_SCLK_BUSY                    = 0x00000115,
SC_GRP2_DYN_SCLK_BUSY                    = 0x00000116,
SC_GRP3_DYN_SCLK_BUSY                    = 0x00000117,
SC_GRP4_DYN_SCLK_BUSY                    = 0x00000118,
SC_PA0_SC_DATA_FIFO_RD                   = 0x00000119,
SC_PA0_SC_DATA_FIFO_WE                   = 0x0000011a,
SC_PA1_SC_DATA_FIFO_RD                   = 0x0000011b,
SC_PA1_SC_DATA_FIFO_WE                   = 0x0000011c,
SC_PS_ARB_XFC_ALL_EVENT_OR_PRIM_CYCLES   = 0x0000011d,
SC_PS_ARB_XFC_ONLY_PRIM_CYCLES           = 0x0000011e,
SC_PS_ARB_XFC_ONLY_ONE_INC_PER_PRIM      = 0x0000011f,
SC_PS_ARB_STALLED_FROM_BELOW             = 0x00000120,
SC_PS_ARB_STARVED_FROM_ABOVE             = 0x00000121,
SC_PS_ARB_SC_BUSY                        = 0x00000122,
SC_PS_ARB_PA_SC_BUSY                     = 0x00000123,
SC_PA2_SC_DATA_FIFO_RD                   = 0x00000124,
SC_PA2_SC_DATA_FIFO_WE                   = 0x00000125,
SC_PA3_SC_DATA_FIFO_RD                   = 0x00000126,
SC_PA3_SC_DATA_FIFO_WE                   = 0x00000127,
SC_PA_SC_DEALLOC_0_0_WE                  = 0x00000128,
SC_PA_SC_DEALLOC_0_1_WE                  = 0x00000129,
SC_PA_SC_DEALLOC_1_0_WE                  = 0x0000012a,
SC_PA_SC_DEALLOC_1_1_WE                  = 0x0000012b,
SC_PA_SC_DEALLOC_2_0_WE                  = 0x0000012c,
SC_PA_SC_DEALLOC_2_1_WE                  = 0x0000012d,
SC_PA_SC_DEALLOC_3_0_WE                  = 0x0000012e,
SC_PA_SC_DEALLOC_3_1_WE                  = 0x0000012f,
SC_PA0_SC_EOP_WE                         = 0x00000130,
SC_PA0_SC_EOPG_WE                        = 0x00000131,
SC_PA0_SC_EVENT_WE                       = 0x00000132,
SC_PA1_SC_EOP_WE                         = 0x00000133,
SC_PA1_SC_EOPG_WE                        = 0x00000134,
SC_PA1_SC_EVENT_WE                       = 0x00000135,
SC_PA2_SC_EOP_WE                         = 0x00000136,
SC_PA2_SC_EOPG_WE                        = 0x00000137,
SC_PA2_SC_EVENT_WE                       = 0x00000138,
SC_PA3_SC_EOP_WE                         = 0x00000139,
SC_PA3_SC_EOPG_WE                        = 0x0000013a,
SC_PA3_SC_EVENT_WE                       = 0x0000013b,
SC_PS_ARB_OOO_THRESHOLD_SWITCH_TO_DESIRED_FIFO  = 0x0000013c,
SC_PS_ARB_OOO_FIFO_EMPTY_SWITCH          = 0x0000013d,
SC_PS_ARB_NULL_PRIM_BUBBLE_POP           = 0x0000013e,
SC_PS_ARB_EOP_POP_SYNC_POP               = 0x0000013f,
SC_PS_ARB_EVENT_SYNC_POP                 = 0x00000140,
SC_SC_PS_ENG_MULTICYCLE_BUBBLE           = 0x00000141,
SC_PA0_SC_FPOV_WE                        = 0x00000142,
SC_PA1_SC_FPOV_WE                        = 0x00000143,
SC_PA2_SC_FPOV_WE                        = 0x00000144,
SC_PA3_SC_FPOV_WE                        = 0x00000145,
SC_PA0_SC_LPOV_WE                        = 0x00000146,
SC_PA1_SC_LPOV_WE                        = 0x00000147,
SC_PA2_SC_LPOV_WE                        = 0x00000148,
SC_PA3_SC_LPOV_WE                        = 0x00000149,
SC_SC_SPI_DEALLOC_0_0                    = 0x0000014a,
SC_SC_SPI_DEALLOC_0_1                    = 0x0000014b,
SC_SC_SPI_DEALLOC_0_2                    = 0x0000014c,
SC_SC_SPI_DEALLOC_1_0                    = 0x0000014d,
SC_SC_SPI_DEALLOC_1_1                    = 0x0000014e,
SC_SC_SPI_DEALLOC_1_2                    = 0x0000014f,
SC_SC_SPI_DEALLOC_2_0                    = 0x00000150,
SC_SC_SPI_DEALLOC_2_1                    = 0x00000151,
SC_SC_SPI_DEALLOC_2_2                    = 0x00000152,
SC_SC_SPI_DEALLOC_3_0                    = 0x00000153,
SC_SC_SPI_DEALLOC_3_1                    = 0x00000154,
SC_SC_SPI_DEALLOC_3_2                    = 0x00000155,
SC_SC_SPI_FPOV_0                         = 0x00000156,
SC_SC_SPI_FPOV_1                         = 0x00000157,
SC_SC_SPI_FPOV_2                         = 0x00000158,
SC_SC_SPI_FPOV_3                         = 0x00000159,
SC_SC_SPI_EVENT                          = 0x0000015a,
SC_PS_TS_EVENT_FIFO_PUSH                 = 0x0000015b,
SC_PS_TS_EVENT_FIFO_POP                  = 0x0000015c,
SC_PS_CTX_DONE_FIFO_PUSH                 = 0x0000015d,
SC_PS_CTX_DONE_FIFO_POP                  = 0x0000015e,
SC_MULTICYCLE_BUBBLE_FREEZE              = 0x0000015f,
SC_EOP_SYNC_WINDOW                       = 0x00000160,
SC_PA0_SC_NULL_WE                        = 0x00000161,
SC_PA0_SC_NULL_DEALLOC_WE                = 0x00000162,
SC_PA0_SC_DATA_FIFO_EOPG_RD              = 0x00000163,
SC_PA0_SC_DATA_FIFO_EOP_RD               = 0x00000164,
SC_PA0_SC_DEALLOC_0_RD                   = 0x00000165,
SC_PA0_SC_DEALLOC_1_RD                   = 0x00000166,
SC_PA1_SC_DATA_FIFO_EOPG_RD              = 0x00000167,
SC_PA1_SC_DATA_FIFO_EOP_RD               = 0x00000168,
SC_PA1_SC_DEALLOC_0_RD                   = 0x00000169,
SC_PA1_SC_DEALLOC_1_RD                   = 0x0000016a,
SC_PA1_SC_NULL_WE                        = 0x0000016b,
SC_PA1_SC_NULL_DEALLOC_WE                = 0x0000016c,
SC_PA2_SC_DATA_FIFO_EOPG_RD              = 0x0000016d,
SC_PA2_SC_DATA_FIFO_EOP_RD               = 0x0000016e,
SC_PA2_SC_DEALLOC_0_RD                   = 0x0000016f,
SC_PA2_SC_DEALLOC_1_RD                   = 0x00000170,
SC_PA2_SC_NULL_WE                        = 0x00000171,
SC_PA2_SC_NULL_DEALLOC_WE                = 0x00000172,
SC_PA3_SC_DATA_FIFO_EOPG_RD              = 0x00000173,
SC_PA3_SC_DATA_FIFO_EOP_RD               = 0x00000174,
SC_PA3_SC_DEALLOC_0_RD                   = 0x00000175,
SC_PA3_SC_DEALLOC_1_RD                   = 0x00000176,
SC_PA3_SC_NULL_WE                        = 0x00000177,
SC_PA3_SC_NULL_DEALLOC_WE                = 0x00000178,
SC_PS_PA0_SC_FIFO_EMPTY                  = 0x00000179,
SC_PS_PA0_SC_FIFO_FULL                   = 0x0000017a,
SC_PA0_PS_DATA_SEND                      = 0x0000017b,
SC_PS_PA1_SC_FIFO_EMPTY                  = 0x0000017c,
SC_PS_PA1_SC_FIFO_FULL                   = 0x0000017d,
SC_PA1_PS_DATA_SEND                      = 0x0000017e,
SC_PS_PA2_SC_FIFO_EMPTY                  = 0x0000017f,
SC_PS_PA2_SC_FIFO_FULL                   = 0x00000180,
SC_PA2_PS_DATA_SEND                      = 0x00000181,
SC_PS_PA3_SC_FIFO_EMPTY                  = 0x00000182,
SC_PS_PA3_SC_FIFO_FULL                   = 0x00000183,
SC_PA3_PS_DATA_SEND                      = 0x00000184,
SC_BUSY_PROCESSING_MULTICYCLE_PRIM       = 0x00000185,
SC_BUSY_CNT_NOT_ZERO                     = 0x00000186,
SC_BM_BUSY                               = 0x00000187,
SC_BACKEND_BUSY                          = 0x00000188,
SC_SCF_SCB_INTERFACE_BUSY                = 0x00000189,
SC_SCB_BUSY                              = 0x0000018a,
SC_STARVED_BY_PA_WITH_UNSELECTED_PA_NOT_EMPTY  = 0x0000018b,
SC_STARVED_BY_PA_WITH_UNSELECTED_PA_FULL  = 0x0000018c,
SC_PBB_BIN_HIST_NUM_PRIMS                = 0x0000018d,
SC_PBB_BATCH_HIST_NUM_PRIMS              = 0x0000018e,
SC_PBB_BIN_HIST_NUM_CONTEXTS             = 0x0000018f,
SC_PBB_BATCH_HIST_NUM_CONTEXTS           = 0x00000190,
SC_PBB_BIN_HIST_NUM_PERSISTENT_STATES    = 0x00000191,
SC_PBB_BATCH_HIST_NUM_PERSISTENT_STATES  = 0x00000192,
SC_PBB_BATCH_HIST_NUM_PS_WAVE_BREAKS     = 0x00000193,
SC_PBB_BATCH_HIST_NUM_TRIV_REJECTED_PRIMS  = 0x00000194,
SC_PBB_BATCH_HIST_NUM_ROWS_PER_PRIM      = 0x00000195,
SC_PBB_BATCH_HIST_NUM_COLUMNS_PER_ROW    = 0x00000196,
SC_PBB_BUSY                              = 0x00000197,
SC_PBB_BUSY_AND_RTR                      = 0x00000198,
SC_PBB_STALLS_PA_DUE_TO_NO_TILES         = 0x00000199,
SC_PBB_NUM_BINS                          = 0x0000019a,
SC_PBB_END_OF_BIN                        = 0x0000019b,
SC_PBB_END_OF_BATCH                      = 0x0000019c,
SC_PBB_PRIMBIN_PROCESSED                 = 0x0000019d,
SC_PBB_PRIM_ADDED_TO_BATCH               = 0x0000019e,
SC_PBB_NONBINNED_PRIM                    = 0x0000019f,
SC_PBB_TOTAL_REAL_PRIMS_OUT_OF_PBB       = 0x000001a0,
SC_PBB_TOTAL_NULL_PRIMS_OUT_OF_PBB       = 0x000001a1,
SC_PBB_IDLE_CLK_DUE_TO_ROW_TO_COLUMN_TRANSITION  = 0x000001a2,
SC_PBB_IDLE_CLK_DUE_TO_FALSE_POSITIVE_ON_ROW  = 0x000001a3,
SC_PBB_IDLE_CLK_DUE_TO_FALSE_POSITIVE_ON_COLUMN  = 0x000001a4,
SC_PBB_BATCH_BREAK_DUE_TO_PERSISTENT_STATE  = 0x000001a5,
SC_PBB_BATCH_BREAK_DUE_TO_CONTEXT_STATE  = 0x000001a6,
SC_PBB_BATCH_BREAK_DUE_TO_PRIM           = 0x000001a7,
SC_PBB_BATCH_BREAK_DUE_TO_PC_STORAGE     = 0x000001a8,
SC_PBB_BATCH_BREAK_DUE_TO_EVENT          = 0x000001a9,
SC_PBB_BATCH_BREAK_DUE_TO_FPOV_LIMIT     = 0x000001aa,
SC_POPS_INTRA_WAVE_OVERLAPS              = 0x000001ab,
SC_POPS_FORCE_EOV                        = 0x000001ac,
SC_PKR_QUAD_OVERLAP_NOT_FOUND_IN_WAVE_TABLE  = 0x000001ad,
SC_PKR_QUAD_OVERLAP_FOUND_IN_WAVE_TABLE  = 0x000001ae,
} SC_PERFCNT_SEL;

/*
 * SePairXsel enum
 */

typedef enum SePairXsel {
RASTER_CONFIG_SE_PAIR_XSEL_8_WIDE_TILE   = 0x00000000,
RASTER_CONFIG_SE_PAIR_XSEL_16_WIDE_TILE  = 0x00000001,
RASTER_CONFIG_SE_PAIR_XSEL_32_WIDE_TILE  = 0x00000002,
RASTER_CONFIG_SE_PAIR_XSEL_64_WIDE_TILE  = 0x00000003,
RASTER_CONFIG_SE_PAIR_XSEL_128_WIDE_TILE  = 0x00000004,
} SePairXsel;

/*
 * SePairYsel enum
 */

typedef enum SePairYsel {
RASTER_CONFIG_SE_PAIR_YSEL_8_WIDE_TILE   = 0x00000000,
RASTER_CONFIG_SE_PAIR_YSEL_16_WIDE_TILE  = 0x00000001,
RASTER_CONFIG_SE_PAIR_YSEL_32_WIDE_TILE  = 0x00000002,
RASTER_CONFIG_SE_PAIR_YSEL_64_WIDE_TILE  = 0x00000003,
RASTER_CONFIG_SE_PAIR_YSEL_128_WIDE_TILE  = 0x00000004,
} SePairYsel;

/*
 * SePairMap enum
 */

typedef enum SePairMap {
RASTER_CONFIG_SE_PAIR_MAP_0              = 0x00000000,
RASTER_CONFIG_SE_PAIR_MAP_1              = 0x00000001,
RASTER_CONFIG_SE_PAIR_MAP_2              = 0x00000002,
RASTER_CONFIG_SE_PAIR_MAP_3              = 0x00000003,
} SePairMap;

/*
 * SeXsel enum
 */

typedef enum SeXsel {
RASTER_CONFIG_SE_XSEL_8_WIDE_TILE        = 0x00000000,
RASTER_CONFIG_SE_XSEL_16_WIDE_TILE       = 0x00000001,
RASTER_CONFIG_SE_XSEL_32_WIDE_TILE       = 0x00000002,
RASTER_CONFIG_SE_XSEL_64_WIDE_TILE       = 0x00000003,
RASTER_CONFIG_SE_XSEL_128_WIDE_TILE      = 0x00000004,
} SeXsel;

/*
 * SeYsel enum
 */

typedef enum SeYsel {
RASTER_CONFIG_SE_YSEL_8_WIDE_TILE        = 0x00000000,
RASTER_CONFIG_SE_YSEL_16_WIDE_TILE       = 0x00000001,
RASTER_CONFIG_SE_YSEL_32_WIDE_TILE       = 0x00000002,
RASTER_CONFIG_SE_YSEL_64_WIDE_TILE       = 0x00000003,
RASTER_CONFIG_SE_YSEL_128_WIDE_TILE      = 0x00000004,
} SeYsel;

/*
 * SeMap enum
 */

typedef enum SeMap {
RASTER_CONFIG_SE_MAP_0                   = 0x00000000,
RASTER_CONFIG_SE_MAP_1                   = 0x00000001,
RASTER_CONFIG_SE_MAP_2                   = 0x00000002,
RASTER_CONFIG_SE_MAP_3                   = 0x00000003,
} SeMap;

/*
 * ScXsel enum
 */

typedef enum ScXsel {
RASTER_CONFIG_SC_XSEL_8_WIDE_TILE        = 0x00000000,
RASTER_CONFIG_SC_XSEL_16_WIDE_TILE       = 0x00000001,
RASTER_CONFIG_SC_XSEL_32_WIDE_TILE       = 0x00000002,
RASTER_CONFIG_SC_XSEL_64_WIDE_TILE       = 0x00000003,
} ScXsel;

/*
 * ScYsel enum
 */

typedef enum ScYsel {
RASTER_CONFIG_SC_YSEL_8_WIDE_TILE        = 0x00000000,
RASTER_CONFIG_SC_YSEL_16_WIDE_TILE       = 0x00000001,
RASTER_CONFIG_SC_YSEL_32_WIDE_TILE       = 0x00000002,
RASTER_CONFIG_SC_YSEL_64_WIDE_TILE       = 0x00000003,
} ScYsel;

/*
 * ScMap enum
 */

typedef enum ScMap {
RASTER_CONFIG_SC_MAP_0                   = 0x00000000,
RASTER_CONFIG_SC_MAP_1                   = 0x00000001,
RASTER_CONFIG_SC_MAP_2                   = 0x00000002,
RASTER_CONFIG_SC_MAP_3                   = 0x00000003,
} ScMap;

/*
 * PkrXsel2 enum
 */

typedef enum PkrXsel2 {
RASTER_CONFIG_PKR_XSEL2_0                = 0x00000000,
RASTER_CONFIG_PKR_XSEL2_1                = 0x00000001,
RASTER_CONFIG_PKR_XSEL2_2                = 0x00000002,
RASTER_CONFIG_PKR_XSEL2_3                = 0x00000003,
} PkrXsel2;

/*
 * PkrXsel enum
 */

typedef enum PkrXsel {
RASTER_CONFIG_PKR_XSEL_0                 = 0x00000000,
RASTER_CONFIG_PKR_XSEL_1                 = 0x00000001,
RASTER_CONFIG_PKR_XSEL_2                 = 0x00000002,
RASTER_CONFIG_PKR_XSEL_3                 = 0x00000003,
} PkrXsel;

/*
 * PkrYsel enum
 */

typedef enum PkrYsel {
RASTER_CONFIG_PKR_YSEL_0                 = 0x00000000,
RASTER_CONFIG_PKR_YSEL_1                 = 0x00000001,
RASTER_CONFIG_PKR_YSEL_2                 = 0x00000002,
RASTER_CONFIG_PKR_YSEL_3                 = 0x00000003,
} PkrYsel;

/*
 * PkrMap enum
 */

typedef enum PkrMap {
RASTER_CONFIG_PKR_MAP_0                  = 0x00000000,
RASTER_CONFIG_PKR_MAP_1                  = 0x00000001,
RASTER_CONFIG_PKR_MAP_2                  = 0x00000002,
RASTER_CONFIG_PKR_MAP_3                  = 0x00000003,
} PkrMap;

/*
 * RbXsel enum
 */

typedef enum RbXsel {
RASTER_CONFIG_RB_XSEL_0                  = 0x00000000,
RASTER_CONFIG_RB_XSEL_1                  = 0x00000001,
} RbXsel;

/*
 * RbYsel enum
 */

typedef enum RbYsel {
RASTER_CONFIG_RB_YSEL_0                  = 0x00000000,
RASTER_CONFIG_RB_YSEL_1                  = 0x00000001,
} RbYsel;

/*
 * RbXsel2 enum
 */

typedef enum RbXsel2 {
RASTER_CONFIG_RB_XSEL2_0                 = 0x00000000,
RASTER_CONFIG_RB_XSEL2_1                 = 0x00000001,
RASTER_CONFIG_RB_XSEL2_2                 = 0x00000002,
RASTER_CONFIG_RB_XSEL2_3                 = 0x00000003,
} RbXsel2;

/*
 * RbMap enum
 */

typedef enum RbMap {
RASTER_CONFIG_RB_MAP_0                   = 0x00000000,
RASTER_CONFIG_RB_MAP_1                   = 0x00000001,
RASTER_CONFIG_RB_MAP_2                   = 0x00000002,
RASTER_CONFIG_RB_MAP_3                   = 0x00000003,
} RbMap;

/*
 * BinningMode enum
 */

typedef enum BinningMode {
BINNING_ALLOWED                          = 0x00000000,
FORCE_BINNING_ON                         = 0x00000001,
DISABLE_BINNING_USE_NEW_SC               = 0x00000002,
DISABLE_BINNING_USE_LEGACY_SC            = 0x00000003,
} BinningMode;

/*
 * BinEventCntl enum
 */

typedef enum BinEventCntl {
BINNER_BREAK_BATCH                       = 0x00000000,
BINNER_PIPELINE                          = 0x00000001,
BINNER_DROP_ASSERT                       = 0x00000002,
} BinEventCntl;

/*
 * CovToShaderSel enum
 */

typedef enum CovToShaderSel {
INPUT_COVERAGE                           = 0x00000000,
INPUT_INNER_COVERAGE                     = 0x00000001,
INPUT_DEPTH_COVERAGE                     = 0x00000002,
RAW                                      = 0x00000003,
} CovToShaderSel;

/*******************************************************
 * RMI Enums
 *******************************************************/

/*
 * RMIPerfSel enum
 */

typedef enum RMIPerfSel {
RMI_PERF_SEL_NONE                        = 0x00000000,
RMI_PERF_SEL_BUSY                        = 0x00000001,
RMI_PERF_SEL_REG_CLK_VLD                 = 0x00000002,
RMI_PERF_SEL_DYN_CLK_CMN_VLD             = 0x00000003,
RMI_PERF_SEL_DYN_CLK_RB_VLD              = 0x00000004,
RMI_PERF_SEL_DYN_CLK_PERF_VLD            = 0x00000005,
RMI_PERF_SEL_PERF_WINDOW                 = 0x00000006,
RMI_PERF_SEL_EVENT_SEND                  = 0x00000007,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID0 = 0x00000008,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID1 = 0x00000009,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID2 = 0x0000000a,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID3 = 0x0000000b,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID4 = 0x0000000c,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID5 = 0x0000000d,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID6 = 0x0000000e,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID7 = 0x0000000f,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID8 = 0x00000010,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID9 = 0x00000011,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID10 = 0x00000012,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID11 = 0x00000013,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID12 = 0x00000014,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID13 = 0x00000015,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID14 = 0x00000016,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID15 = 0x00000017,
RMI_PERF_SEL_RMI_INVALIDATION_ATC_REQ_VMID_ALL = 0x00000018,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID0 = 0x00000019,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID1 = 0x0000001a,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID2 = 0x0000001b,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID3 = 0x0000001c,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID4 = 0x0000001d,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID5 = 0x0000001e,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID6 = 0x0000001f,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID7 = 0x00000020,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID8 = 0x00000021,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID9 = 0x00000022,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID10 = 0x00000023,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID11 = 0x00000024,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID12 = 0x00000025,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID13 = 0x00000026,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID14 = 0x00000027,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID15 = 0x00000028,
RMI_PERF_SEL_RMI_INVALIDATION_REQ_START_FINISH_VMID_ALL = 0x00000029,
RMI_PERF_SEL_UTCL1_TRANSLATION_MISS      = 0x0000002a,
RMI_PERF_SEL_UTCL1_PERMISSION_MISS       = 0x0000002b,
RMI_PERF_SEL_UTCL1_REQUEST               = 0x0000002c,
RMI_PERF_SEL_UTCL1_STALL_INFLIGHT_MAX    = 0x0000002d,
RMI_PERF_SEL_UTCL1_STALL_LRU_INFLIGHT    = 0x0000002e,
RMI_PERF_SEL_UTCL1_LFIFO_FULL            = 0x0000002f,
RMI_PERF_SEL_UTCL1_STALL_LFIFO_NOT_RES   = 0x00000030,
RMI_PERF_SEL_UTCL1_STALL_UTCL2_REQ_OUT_OF_CREDITS  = 0x00000031,
RMI_PERF_SEL_UTCL1_STALL_MISSFIFO_FULL   = 0x00000032,
RMI_PERF_SEL_UTCL1_HIT_FIFO_FULL         = 0x00000033,
RMI_PERF_SEL_UTCL1_STALL_MULTI_MISS      = 0x00000034,
RMI_PERF_SEL_RB_RMI_WRREQ_ALL_CID        = 0x00000035,
RMI_PERF_SEL_RB_RMI_WRREQ_BUSY           = 0x00000036,
RMI_PERF_SEL_RB_RMI_WRREQ_CID0           = 0x00000037,
RMI_PERF_SEL_RB_RMI_WRREQ_CID1           = 0x00000038,
RMI_PERF_SEL_RB_RMI_WRREQ_CID2           = 0x00000039,
RMI_PERF_SEL_RB_RMI_WRREQ_CID3           = 0x0000003a,
RMI_PERF_SEL_RB_RMI_WRREQ_CID4           = 0x0000003b,
RMI_PERF_SEL_RB_RMI_WRREQ_CID5           = 0x0000003c,
RMI_PERF_SEL_RB_RMI_WRREQ_CID6           = 0x0000003d,
RMI_PERF_SEL_RB_RMI_WRREQ_CID7           = 0x0000003e,
RMI_PERF_SEL_RB_RMI_WRREQ_INFLIGHT_ALL_ORONE_CID = 0x0000003f,
RMI_PERF_SEL_RB_RMI_WRREQ_BURST_LENGTH_ALL_ORONE_CID = 0x00000040,
RMI_PERF_SEL_RB_RMI_WRREQ_BURST_ALL_ORONE_CID = 0x00000041,
RMI_PERF_SEL_RB_RMI_WRREQ_RESIDENCY      = 0x00000042,
RMI_PERF_SEL_RMI_RB_WRRET_VALID_ALL_CID  = 0x00000043,
RMI_PERF_SEL_RMI_RB_WRRET_VALID_CID0     = 0x00000044,
RMI_PERF_SEL_RMI_RB_WRRET_VALID_CID1     = 0x00000045,
RMI_PERF_SEL_RMI_RB_WRRET_VALID_CID2     = 0x00000046,
RMI_PERF_SEL_RMI_RB_WRRET_VALID_CID3     = 0x00000047,
RMI_PERF_SEL_RMI_RB_WRRET_VALID_CID4     = 0x00000048,
RMI_PERF_SEL_RMI_RB_WRRET_VALID_CID5     = 0x00000049,
RMI_PERF_SEL_RMI_RB_WRRET_VALID_CID6     = 0x0000004a,
RMI_PERF_SEL_RMI_RB_WRRET_VALID_CID7     = 0x0000004b,
RMI_PERF_SEL_RMI_RB_WRRET_VALID_NACK0    = 0x0000004c,
RMI_PERF_SEL_RMI_RB_WRRET_VALID_NACK1    = 0x0000004d,
RMI_PERF_SEL_RMI_RB_WRRET_VALID_NACK2    = 0x0000004e,
RMI_PERF_SEL_RMI_RB_WRRET_VALID_NACK3    = 0x0000004f,
RMI_PERF_SEL_RB_RMI_32BRDREQ_ALL_CID     = 0x00000050,
RMI_PERF_SEL_RB_RMI_RDREQ_ALL_CID        = 0x00000051,
RMI_PERF_SEL_RB_RMI_RDREQ_BUSY           = 0x00000052,
RMI_PERF_SEL_RB_RMI_32BRDREQ_CID0        = 0x00000053,
RMI_PERF_SEL_RB_RMI_32BRDREQ_CID1        = 0x00000054,
RMI_PERF_SEL_RB_RMI_32BRDREQ_CID2        = 0x00000055,
RMI_PERF_SEL_RB_RMI_32BRDREQ_CID3        = 0x00000056,
RMI_PERF_SEL_RB_RMI_32BRDREQ_CID4        = 0x00000057,
RMI_PERF_SEL_RB_RMI_32BRDREQ_CID5        = 0x00000058,
RMI_PERF_SEL_RB_RMI_32BRDREQ_CID6        = 0x00000059,
RMI_PERF_SEL_RB_RMI_32BRDREQ_CID7        = 0x0000005a,
RMI_PERF_SEL_RB_RMI_RDREQ_CID0           = 0x0000005b,
RMI_PERF_SEL_RB_RMI_RDREQ_CID1           = 0x0000005c,
RMI_PERF_SEL_RB_RMI_RDREQ_CID2           = 0x0000005d,
RMI_PERF_SEL_RB_RMI_RDREQ_CID3           = 0x0000005e,
RMI_PERF_SEL_RB_RMI_RDREQ_CID4           = 0x0000005f,
RMI_PERF_SEL_RB_RMI_RDREQ_CID5           = 0x00000060,
RMI_PERF_SEL_RB_RMI_RDREQ_CID6           = 0x00000061,
RMI_PERF_SEL_RB_RMI_RDREQ_CID7           = 0x00000062,
RMI_PERF_SEL_RB_RMI_32BRDREQ_INFLIGHT_ALL_ORONE_CID = 0x00000063,
RMI_PERF_SEL_RB_RMI_RDREQ_BURST_LENGTH_ALL_ORONE_CID = 0x00000064,
RMI_PERF_SEL_RB_RMI_RDREQ_BURST_ALL_ORONE_CID = 0x00000065,
RMI_PERF_SEL_RB_RMI_RDREQ_RESIDENCY      = 0x00000066,
RMI_PERF_SEL_RMI_RB_32BRDRET_VALID_ALL_CID = 0x00000067,
RMI_PERF_SEL_RMI_RB_32BRDRET_VALID_CID0  = 0x00000068,
RMI_PERF_SEL_RMI_RB_32BRDRET_VALID_CID1  = 0x00000069,
RMI_PERF_SEL_RMI_RB_32BRDRET_VALID_CID2  = 0x0000006a,
RMI_PERF_SEL_RMI_RB_32BRDRET_VALID_CID3  = 0x0000006b,
RMI_PERF_SEL_RMI_RB_32BRDRET_VALID_CID4  = 0x0000006c,
RMI_PERF_SEL_RMI_RB_32BRDRET_VALID_CID5  = 0x0000006d,
RMI_PERF_SEL_RMI_RB_32BRDRET_VALID_CID6  = 0x0000006e,
RMI_PERF_SEL_RMI_RB_32BRDRET_VALID_CID7  = 0x0000006f,
RMI_PERF_SEL_RMI_RB_32BRDRET_VALID_NACK0 = 0x00000070,
RMI_PERF_SEL_RMI_RB_32BRDRET_VALID_NACK1 = 0x00000071,
RMI_PERF_SEL_RMI_RB_32BRDRET_VALID_NACK2 = 0x00000072,
RMI_PERF_SEL_RMI_RB_32BRDRET_VALID_NACK3 = 0x00000073,
RMI_PERF_SEL_RMI_TC_WRREQ_ALL_CID        = 0x00000074,
RMI_PERF_SEL_RMI_TC_REQ_BUSY             = 0x00000075,
RMI_PERF_SEL_RMI_TC_WRREQ_CID0           = 0x00000076,
RMI_PERF_SEL_RMI_TC_WRREQ_CID1           = 0x00000077,
RMI_PERF_SEL_RMI_TC_WRREQ_CID2           = 0x00000078,
RMI_PERF_SEL_RMI_TC_WRREQ_CID3           = 0x00000079,
RMI_PERF_SEL_RMI_TC_WRREQ_CID4           = 0x0000007a,
RMI_PERF_SEL_RMI_TC_WRREQ_CID5           = 0x0000007b,
RMI_PERF_SEL_RMI_TC_WRREQ_CID6           = 0x0000007c,
RMI_PERF_SEL_RMI_TC_WRREQ_CID7           = 0x0000007d,
RMI_PERF_SEL_RMI_TC_WRREQ_INFLIGHT_ALL_CID = 0x0000007e,
RMI_PERF_SEL_TC_RMI_WRRET_VALID_ALL_CID  = 0x0000007f,
RMI_PERF_SEL_RMI_TC_RDREQ_ALL_CID        = 0x00000080,
RMI_PERF_SEL_RMI_TC_RDREQ_CID0           = 0x00000081,
RMI_PERF_SEL_RMI_TC_RDREQ_CID1           = 0x00000082,
RMI_PERF_SEL_RMI_TC_RDREQ_CID2           = 0x00000083,
RMI_PERF_SEL_RMI_TC_RDREQ_CID3           = 0x00000084,
RMI_PERF_SEL_RMI_TC_RDREQ_CID4           = 0x00000085,
RMI_PERF_SEL_RMI_TC_RDREQ_CID5           = 0x00000086,
RMI_PERF_SEL_RMI_TC_RDREQ_CID6           = 0x00000087,
RMI_PERF_SEL_RMI_TC_RDREQ_CID7           = 0x00000088,
RMI_PERF_SEL_RMI_TC_RDREQ_INFLIGHT_ALL_CID = 0x00000089,
RMI_PERF_SEL_TC_RMI_RDRET_VALID_ALL_CID  = 0x0000008a,
RMI_PERF_SEL_UTCL1_BUSY                  = 0x0000008b,
RMI_PERF_SEL_RMI_UTC_REQ                 = 0x0000008c,
RMI_PERF_SEL_RMI_UTC_BUSY                = 0x0000008d,
RMI_PERF_SEL_UTCL1_UTCL2_REQ             = 0x0000008e,
RMI_PERF_SEL_PROBE_UTCL1_XNACK_RETRY     = 0x0000008f,
RMI_PERF_SEL_PROBE_UTCL1_ALL_FAULT       = 0x00000090,
RMI_PERF_SEL_PROBE_UTCL1_PRT_FAULT       = 0x00000091,
RMI_PERF_SEL_PROBE_UTCL1_XNACK_NORETRY_FAULT = 0x00000092,
RMI_PERF_SEL_XNACK_FIFO_NUM_USED         = 0x00000093,
RMI_PERF_SEL_LAT_FIFO_NUM_USED           = 0x00000094,
RMI_PERF_SEL_LAT_FIFO_BLOCKING_REQ       = 0x00000095,
RMI_PERF_SEL_LAT_FIFO_NONBLOCKING_REQ    = 0x00000096,
RMI_PERF_SEL_XNACK_FIFO_FULL             = 0x00000097,
RMI_PERF_SEL_XNACK_FIFO_BUSY             = 0x00000098,
RMI_PERF_SEL_LAT_FIFO_FULL               = 0x00000099,
RMI_PERF_SEL_SKID_FIFO_DEPTH             = 0x0000009a,
RMI_PERF_SEL_TCIW_INFLIGHT_COUNT         = 0x0000009b,
RMI_PERF_SEL_PRT_FIFO_NUM_USED           = 0x0000009c,
RMI_PERF_SEL_PRT_FIFO_REQ                = 0x0000009d,
RMI_PERF_SEL_PRT_FIFO_BUSY               = 0x0000009e,
RMI_PERF_SEL_TCIW_REQ                    = 0x0000009f,
RMI_PERF_SEL_TCIW_BUSY                   = 0x000000a0,
RMI_PERF_SEL_SKID_FIFO_REQ               = 0x000000a1,
RMI_PERF_SEL_SKID_FIFO_BUSY              = 0x000000a2,
RMI_PERF_SEL_DEMUX_TCIW_RESIDENCY_NACK0  = 0x000000a3,
RMI_PERF_SEL_DEMUX_TCIW_RESIDENCY_NACK1  = 0x000000a4,
RMI_PERF_SEL_DEMUX_TCIW_RESIDENCY_NACK2  = 0x000000a5,
RMI_PERF_SEL_DEMUX_TCIW_RESIDENCY_NACK3  = 0x000000a6,
RMI_PERF_SEL_XBAR_PROBEGEN_RTS_RTR       = 0x000000a7,
RMI_PERF_SEL_XBAR_PROBEGEN_RTSB_RTR      = 0x000000a8,
RMI_PERF_SEL_XBAR_PROBEGEN_RTS_RTRB      = 0x000000a9,
RMI_PERF_SEL_XBAR_PROBEGEN_RTSB_RTRB     = 0x000000aa,
RMI_PERF_SEL_DEMUX_TCIW_FORMATTER_RTS_RTR = 0x000000ab,
RMI_PERF_SEL_DEMUX_TCIW_FORMATTER_RTSB_RTR = 0x000000ac,
RMI_PERF_SEL_DEMUX_TCIW_FORMATTER_RTS_RTRB = 0x000000ad,
RMI_PERF_SEL_DEMUX_TCIW_FORMATTER_RTSB_RTRB = 0x000000ae,
RMI_PERF_SEL_WRREQCONSUMER_XBAR_WRREQ_RTS_RTR = 0x000000af,
RMI_PERF_SEL_WRREQCONSUMER_XBAR_WRREQ_RTSB_RTR = 0x000000b0,
RMI_PERF_SEL_WRREQCONSUMER_XBAR_WRREQ_RTS_RTRB = 0x000000b1,
RMI_PERF_SEL_WRREQCONSUMER_XBAR_WRREQ_RTSB_RTRB = 0x000000b2,
RMI_PERF_SEL_RDREQCONSUMER_XBAR_RDREQ_RTS_RTR = 0x000000b3,
RMI_PERF_SEL_RDREQCONSUMER_XBAR_RDREQ_RTSB_RTR = 0x000000b4,
RMI_PERF_SEL_RDREQCONSUMER_XBAR_RDREQ_RTS_RTRB = 0x000000b5,
RMI_PERF_SEL_RDREQCONSUMER_XBAR_RDREQ_RTSB_RTRB = 0x000000b6,
RMI_PERF_SEL_POP_DEMUX_RTS_RTR           = 0x000000b7,
RMI_PERF_SEL_POP_DEMUX_RTSB_RTR          = 0x000000b8,
RMI_PERF_SEL_POP_DEMUX_RTS_RTRB          = 0x000000b9,
RMI_PERF_SEL_POP_DEMUX_RTSB_RTRB         = 0x000000ba,
RMI_PERF_SEL_PROBEGEN_UTC_RTS_RTR        = 0x000000bb,
RMI_PERF_SEL_PROBEGEN_UTC_RTSB_RTR       = 0x000000bc,
RMI_PERF_SEL_PROBEGEN_UTC_RTS_RTRB       = 0x000000bd,
RMI_PERF_SEL_PROBEGEN_UTC_RTSB_RTRB      = 0x000000be,
RMI_PERF_SEL_UTC_POP_RTS_RTR             = 0x000000bf,
RMI_PERF_SEL_UTC_POP_RTSB_RTR            = 0x000000c0,
RMI_PERF_SEL_UTC_POP_RTS_RTRB            = 0x000000c1,
RMI_PERF_SEL_UTC_POP_RTSB_RTRB           = 0x000000c2,
RMI_PERF_SEL_POP_XNACK_RTS_RTR           = 0x000000c3,
RMI_PERF_SEL_POP_XNACK_RTSB_RTR          = 0x000000c4,
RMI_PERF_SEL_POP_XNACK_RTS_RTRB          = 0x000000c5,
RMI_PERF_SEL_POP_XNACK_RTSB_RTRB         = 0x000000c6,
RMI_PERF_SEL_XNACK_PROBEGEN_RTS_RTR      = 0x000000c7,
RMI_PERF_SEL_XNACK_PROBEGEN_RTSB_RTR     = 0x000000c8,
RMI_PERF_SEL_XNACK_PROBEGEN_RTS_RTRB     = 0x000000c9,
RMI_PERF_SEL_XNACK_PROBEGEN_RTSB_RTRB    = 0x000000ca,
RMI_PERF_SEL_PRTFIFO_RTNFORMATTER_RTS_RTR = 0x000000cb,
RMI_PERF_SEL_PRTFIFO_RTNFORMATTER_RTSB_RTR = 0x000000cc,
RMI_PERF_SEL_PRTFIFO_RTNFORMATTER_RTS_RTRB = 0x000000cd,
RMI_PERF_SEL_PRTFIFO_RTNFORMATTER_RTSB_RTRB = 0x000000ce,
RMI_PERF_SEL_SKID_FIFO_IN_RTS            = 0x000000cf,
RMI_PERF_SEL_SKID_FIFO_IN_RTSB           = 0x000000d0,
RMI_PERF_SEL_SKID_FIFO_OUT_RTS           = 0x000000d1,
RMI_PERF_SEL_SKID_FIFO_OUT_RTSB          = 0x000000d2,
RMI_PERF_SEL_XBAR_PROBEGEN_READ_RTS_RTR  = 0x000000d3,
RMI_PERF_SEL_XBAR_PROBEGEN_WRITE_RTS_RTR = 0x000000d4,
RMI_PERF_SEL_XBAR_PROBEGEN_IN0_RTS_RTR   = 0x000000d5,
RMI_PERF_SEL_XBAR_PROBEGEN_IN1_RTS_RTR   = 0x000000d6,
RMI_PERF_SEL_XBAR_PROBEGEN_CB_RTS_RTR    = 0x000000d7,
RMI_PERF_SEL_XBAR_PROBEGEN_DB_RTS_RTR    = 0x000000d8,
RMI_PERF_SEL_REORDER_FIFO_REQ            = 0x000000d9,
RMI_PERF_SEL_REORDER_FIFO_BUSY           = 0x000000da,
RMI_PERF_SEL_RMI_RB_EARLY_WRACK_ALL_CID  = 0x000000db,
RMI_PERF_SEL_RMI_RB_EARLY_WRACK_CID0     = 0x000000dc,
RMI_PERF_SEL_RMI_RB_EARLY_WRACK_CID1     = 0x000000dd,
RMI_PERF_SEL_RMI_RB_EARLY_WRACK_CID2     = 0x000000de,
RMI_PERF_SEL_RMI_RB_EARLY_WRACK_CID3     = 0x000000df,
RMI_PERF_SEL_RMI_RB_EARLY_WRACK_CID4     = 0x000000e0,
RMI_PERF_SEL_RMI_RB_EARLY_WRACK_CID5     = 0x000000e1,
RMI_PERF_SEL_RMI_RB_EARLY_WRACK_CID6     = 0x000000e2,
RMI_PERF_SEL_RMI_RB_EARLY_WRACK_CID7     = 0x000000e3,
RMI_PERF_SEL_RMI_RB_EARLY_WRACK_NACK0    = 0x000000e4,
RMI_PERF_SEL_RMI_RB_EARLY_WRACK_NACK1    = 0x000000e5,
RMI_PERF_SEL_RMI_RB_EARLY_WRACK_NACK2    = 0x000000e6,
RMI_PERF_SEL_RMI_RB_EARLY_WRACK_NACK3    = 0x000000e7,
} RMIPerfSel;

/*******************************************************
 * IH Enums
 *******************************************************/

/*
 * IH_PERF_SEL enum
 */

typedef enum IH_PERF_SEL {
IH_PERF_SEL_CYCLE                        = 0x00000000,
IH_PERF_SEL_IDLE                         = 0x00000001,
IH_PERF_SEL_INPUT_IDLE                   = 0x00000002,
IH_PERF_SEL_BUFFER_IDLE                  = 0x00000003,
IH_PERF_SEL_RB0_FULL                     = 0x00000004,
IH_PERF_SEL_RB0_OVERFLOW                 = 0x00000005,
IH_PERF_SEL_RB0_WPTR_WRITEBACK           = 0x00000006,
IH_PERF_SEL_RB0_WPTR_WRAP                = 0x00000007,
IH_PERF_SEL_RB0_RPTR_WRAP                = 0x00000008,
IH_PERF_SEL_MC_WR_IDLE                   = 0x00000009,
IH_PERF_SEL_MC_WR_COUNT                  = 0x0000000a,
IH_PERF_SEL_MC_WR_STALL                  = 0x0000000b,
IH_PERF_SEL_MC_WR_CLEAN_PENDING          = 0x0000000c,
IH_PERF_SEL_MC_WR_CLEAN_STALL            = 0x0000000d,
IH_PERF_SEL_BIF_LINE0_RISING             = 0x0000000e,
IH_PERF_SEL_BIF_LINE0_FALLING            = 0x0000000f,
IH_PERF_SEL_RB1_FULL                     = 0x00000010,
IH_PERF_SEL_RB1_OVERFLOW                 = 0x00000011,
Reserved18                               = 0x00000012,
IH_PERF_SEL_RB1_WPTR_WRAP                = 0x00000013,
IH_PERF_SEL_RB1_RPTR_WRAP                = 0x00000014,
IH_PERF_SEL_RB2_FULL                     = 0x00000015,
IH_PERF_SEL_RB2_OVERFLOW                 = 0x00000016,
Reserved23                               = 0x00000017,
IH_PERF_SEL_RB2_WPTR_WRAP                = 0x00000018,
IH_PERF_SEL_RB2_RPTR_WRAP                = 0x00000019,
Reserved26                               = 0x0000001a,
Reserved27                               = 0x0000001b,
Reserved28                               = 0x0000001c,
Reserved29                               = 0x0000001d,
IH_PERF_SEL_RB0_FULL_VF0                 = 0x0000001e,
IH_PERF_SEL_RB0_FULL_VF1                 = 0x0000001f,
IH_PERF_SEL_RB0_FULL_VF2                 = 0x00000020,
IH_PERF_SEL_RB0_FULL_VF3                 = 0x00000021,
IH_PERF_SEL_RB0_FULL_VF4                 = 0x00000022,
IH_PERF_SEL_RB0_FULL_VF5                 = 0x00000023,
IH_PERF_SEL_RB0_FULL_VF6                 = 0x00000024,
IH_PERF_SEL_RB0_FULL_VF7                 = 0x00000025,
IH_PERF_SEL_RB0_FULL_VF8                 = 0x00000026,
IH_PERF_SEL_RB0_FULL_VF9                 = 0x00000027,
IH_PERF_SEL_RB0_FULL_VF10                = 0x00000028,
IH_PERF_SEL_RB0_FULL_VF11                = 0x00000029,
IH_PERF_SEL_RB0_FULL_VF12                = 0x0000002a,
IH_PERF_SEL_RB0_FULL_VF13                = 0x0000002b,
IH_PERF_SEL_RB0_FULL_VF14                = 0x0000002c,
IH_PERF_SEL_RB0_FULL_VF15                = 0x0000002d,
IH_PERF_SEL_RB0_OVERFLOW_VF0             = 0x0000002e,
IH_PERF_SEL_RB0_OVERFLOW_VF1             = 0x0000002f,
IH_PERF_SEL_RB0_OVERFLOW_VF2             = 0x00000030,
IH_PERF_SEL_RB0_OVERFLOW_VF3             = 0x00000031,
IH_PERF_SEL_RB0_OVERFLOW_VF4             = 0x00000032,
IH_PERF_SEL_RB0_OVERFLOW_VF5             = 0x00000033,
IH_PERF_SEL_RB0_OVERFLOW_VF6             = 0x00000034,
IH_PERF_SEL_RB0_OVERFLOW_VF7             = 0x00000035,
IH_PERF_SEL_RB0_OVERFLOW_VF8             = 0x00000036,
IH_PERF_SEL_RB0_OVERFLOW_VF9             = 0x00000037,
IH_PERF_SEL_RB0_OVERFLOW_VF10            = 0x00000038,
IH_PERF_SEL_RB0_OVERFLOW_VF11            = 0x00000039,
IH_PERF_SEL_RB0_OVERFLOW_VF12            = 0x0000003a,
IH_PERF_SEL_RB0_OVERFLOW_VF13            = 0x0000003b,
IH_PERF_SEL_RB0_OVERFLOW_VF14            = 0x0000003c,
IH_PERF_SEL_RB0_OVERFLOW_VF15            = 0x0000003d,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF0       = 0x0000003e,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF1       = 0x0000003f,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF2       = 0x00000040,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF3       = 0x00000041,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF4       = 0x00000042,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF5       = 0x00000043,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF6       = 0x00000044,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF7       = 0x00000045,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF8       = 0x00000046,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF9       = 0x00000047,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF10      = 0x00000048,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF11      = 0x00000049,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF12      = 0x0000004a,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF13      = 0x0000004b,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF14      = 0x0000004c,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF15      = 0x0000004d,
IH_PERF_SEL_RB0_WPTR_WRAP_VF0            = 0x0000004e,
IH_PERF_SEL_RB0_WPTR_WRAP_VF1            = 0x0000004f,
IH_PERF_SEL_RB0_WPTR_WRAP_VF2            = 0x00000050,
IH_PERF_SEL_RB0_WPTR_WRAP_VF3            = 0x00000051,
IH_PERF_SEL_RB0_WPTR_WRAP_VF4            = 0x00000052,
IH_PERF_SEL_RB0_WPTR_WRAP_VF5            = 0x00000053,
IH_PERF_SEL_RB0_WPTR_WRAP_VF6            = 0x00000054,
IH_PERF_SEL_RB0_WPTR_WRAP_VF7            = 0x00000055,
IH_PERF_SEL_RB0_WPTR_WRAP_VF8            = 0x00000056,
IH_PERF_SEL_RB0_WPTR_WRAP_VF9            = 0x00000057,
IH_PERF_SEL_RB0_WPTR_WRAP_VF10           = 0x00000058,
IH_PERF_SEL_RB0_WPTR_WRAP_VF11           = 0x00000059,
IH_PERF_SEL_RB0_WPTR_WRAP_VF12           = 0x0000005a,
IH_PERF_SEL_RB0_WPTR_WRAP_VF13           = 0x0000005b,
IH_PERF_SEL_RB0_WPTR_WRAP_VF14           = 0x0000005c,
IH_PERF_SEL_RB0_WPTR_WRAP_VF15           = 0x0000005d,
IH_PERF_SEL_RB0_RPTR_WRAP_VF0            = 0x0000005e,
IH_PERF_SEL_RB0_RPTR_WRAP_VF1            = 0x0000005f,
IH_PERF_SEL_RB0_RPTR_WRAP_VF2            = 0x00000060,
IH_PERF_SEL_RB0_RPTR_WRAP_VF3            = 0x00000061,
IH_PERF_SEL_RB0_RPTR_WRAP_VF4            = 0x00000062,
IH_PERF_SEL_RB0_RPTR_WRAP_VF5            = 0x00000063,
IH_PERF_SEL_RB0_RPTR_WRAP_VF6            = 0x00000064,
IH_PERF_SEL_RB0_RPTR_WRAP_VF7            = 0x00000065,
IH_PERF_SEL_RB0_RPTR_WRAP_VF8            = 0x00000066,
IH_PERF_SEL_RB0_RPTR_WRAP_VF9            = 0x00000067,
IH_PERF_SEL_RB0_RPTR_WRAP_VF10           = 0x00000068,
IH_PERF_SEL_RB0_RPTR_WRAP_VF11           = 0x00000069,
IH_PERF_SEL_RB0_RPTR_WRAP_VF12           = 0x0000006a,
IH_PERF_SEL_RB0_RPTR_WRAP_VF13           = 0x0000006b,
IH_PERF_SEL_RB0_RPTR_WRAP_VF14           = 0x0000006c,
IH_PERF_SEL_RB0_RPTR_WRAP_VF15           = 0x0000006d,
IH_PERF_SEL_BIF_LINE0_RISING_VF0         = 0x0000006e,
IH_PERF_SEL_BIF_LINE0_RISING_VF1         = 0x0000006f,
IH_PERF_SEL_BIF_LINE0_RISING_VF2         = 0x00000070,
IH_PERF_SEL_BIF_LINE0_RISING_VF3         = 0x00000071,
IH_PERF_SEL_BIF_LINE0_RISING_VF4         = 0x00000072,
IH_PERF_SEL_BIF_LINE0_RISING_VF5         = 0x00000073,
IH_PERF_SEL_BIF_LINE0_RISING_VF6         = 0x00000074,
IH_PERF_SEL_BIF_LINE0_RISING_VF7         = 0x00000075,
IH_PERF_SEL_BIF_LINE0_RISING_VF8         = 0x00000076,
IH_PERF_SEL_BIF_LINE0_RISING_VF9         = 0x00000077,
IH_PERF_SEL_BIF_LINE0_RISING_VF10        = 0x00000078,
IH_PERF_SEL_BIF_LINE0_RISING_VF11        = 0x00000079,
IH_PERF_SEL_BIF_LINE0_RISING_VF12        = 0x0000007a,
IH_PERF_SEL_BIF_LINE0_RISING_VF13        = 0x0000007b,
IH_PERF_SEL_BIF_LINE0_RISING_VF14        = 0x0000007c,
IH_PERF_SEL_BIF_LINE0_RISING_VF15        = 0x0000007d,
IH_PERF_SEL_BIF_LINE0_FALLING_VF0        = 0x0000007e,
IH_PERF_SEL_BIF_LINE0_FALLING_VF1        = 0x0000007f,
IH_PERF_SEL_BIF_LINE0_FALLING_VF2        = 0x00000080,
IH_PERF_SEL_BIF_LINE0_FALLING_VF3        = 0x00000081,
IH_PERF_SEL_BIF_LINE0_FALLING_VF4        = 0x00000082,
IH_PERF_SEL_BIF_LINE0_FALLING_VF5        = 0x00000083,
IH_PERF_SEL_BIF_LINE0_FALLING_VF6        = 0x00000084,
IH_PERF_SEL_BIF_LINE0_FALLING_VF7        = 0x00000085,
IH_PERF_SEL_BIF_LINE0_FALLING_VF8        = 0x00000086,
IH_PERF_SEL_BIF_LINE0_FALLING_VF9        = 0x00000087,
IH_PERF_SEL_BIF_LINE0_FALLING_VF10       = 0x00000088,
IH_PERF_SEL_BIF_LINE0_FALLING_VF11       = 0x00000089,
IH_PERF_SEL_BIF_LINE0_FALLING_VF12       = 0x0000008a,
IH_PERF_SEL_BIF_LINE0_FALLING_VF13       = 0x0000008b,
IH_PERF_SEL_BIF_LINE0_FALLING_VF14       = 0x0000008c,
IH_PERF_SEL_BIF_LINE0_FALLING_VF15       = 0x0000008d,
Reserved142                              = 0x0000008e,
Reserved143                              = 0x0000008f,
Reserved144                              = 0x00000090,
Reserved145                              = 0x00000091,
Reserved146                              = 0x00000092,
Reserved147                              = 0x00000093,
Reserved148                              = 0x00000094,
Reserved149                              = 0x00000095,
IH_PERF_SEL_CLIENT0_INT                  = 0x00000096,
IH_PERF_SEL_CLIENT1_INT                  = 0x00000097,
IH_PERF_SEL_CLIENT2_INT                  = 0x00000098,
IH_PERF_SEL_CLIENT3_INT                  = 0x00000099,
IH_PERF_SEL_CLIENT4_INT                  = 0x0000009a,
IH_PERF_SEL_CLIENT5_INT                  = 0x0000009b,
IH_PERF_SEL_CLIENT6_INT                  = 0x0000009c,
IH_PERF_SEL_CLIENT7_INT                  = 0x0000009d,
IH_PERF_SEL_CLIENT8_INT                  = 0x0000009e,
IH_PERF_SEL_CLIENT9_INT                  = 0x0000009f,
IH_PERF_SEL_CLIENT10_INT                 = 0x000000a0,
IH_PERF_SEL_CLIENT11_INT                 = 0x000000a1,
IH_PERF_SEL_CLIENT12_INT                 = 0x000000a2,
IH_PERF_SEL_CLIENT13_INT                 = 0x000000a3,
IH_PERF_SEL_CLIENT14_INT                 = 0x000000a4,
IH_PERF_SEL_CLIENT15_INT                 = 0x000000a5,
IH_PERF_SEL_CLIENT16_INT                 = 0x000000a6,
IH_PERF_SEL_CLIENT17_INT                 = 0x000000a7,
IH_PERF_SEL_CLIENT18_INT                 = 0x000000a8,
IH_PERF_SEL_CLIENT19_INT                 = 0x000000a9,
IH_PERF_SEL_CLIENT20_INT                 = 0x000000aa,
IH_PERF_SEL_CLIENT21_INT                 = 0x000000ab,
IH_PERF_SEL_CLIENT22_INT                 = 0x000000ac,
IH_PERF_SEL_CLIENT23_INT                 = 0x000000ad,
IH_PERF_SEL_CLIENT24_INT                 = 0x000000ae,
IH_PERF_SEL_CLIENT25_INT                 = 0x000000af,
IH_PERF_SEL_CLIENT26_INT                 = 0x000000b0,
IH_PERF_SEL_CLIENT27_INT                 = 0x000000b1,
IH_PERF_SEL_CLIENT28_INT                 = 0x000000b2,
IH_PERF_SEL_CLIENT29_INT                 = 0x000000b3,
IH_PERF_SEL_CLIENT30_INT                 = 0x000000b4,
IH_PERF_SEL_CLIENT31_INT                 = 0x000000b5,
Reserved182                              = 0x000000b6,
Reserved183                              = 0x000000b7,
Reserved184                              = 0x000000b8,
Reserved185                              = 0x000000b9,
Reserved186                              = 0x000000ba,
Reserved187                              = 0x000000bb,
Reserved188                              = 0x000000bc,
Reserved189                              = 0x000000bd,
Reserved190                              = 0x000000be,
Reserved191                              = 0x000000bf,
Reserved192                              = 0x000000c0,
Reserved193                              = 0x000000c1,
Reserved194                              = 0x000000c2,
Reserved195                              = 0x000000c3,
Reserved196                              = 0x000000c4,
Reserved197                              = 0x000000c5,
Reserved198                              = 0x000000c6,
Reserved199                              = 0x000000c7,
Reserved200                              = 0x000000c8,
Reserved201                              = 0x000000c9,
Reserved202                              = 0x000000ca,
Reserved203                              = 0x000000cb,
Reserved204                              = 0x000000cc,
Reserved205                              = 0x000000cd,
Reserved206                              = 0x000000ce,
Reserved207                              = 0x000000cf,
Reserved208                              = 0x000000d0,
Reserved209                              = 0x000000d1,
Reserved210                              = 0x000000d2,
Reserved211                              = 0x000000d3,
Reserved212                              = 0x000000d4,
Reserved213                              = 0x000000d5,
Reserved214                              = 0x000000d6,
Reserved215                              = 0x000000d7,
Reserved216                              = 0x000000d8,
Reserved217                              = 0x000000d9,
Reserved218                              = 0x000000da,
Reserved219                              = 0x000000db,
IH_PERF_SEL_RB1_FULL_VF0                 = 0x000000dc,
IH_PERF_SEL_RB1_FULL_VF1                 = 0x000000dd,
IH_PERF_SEL_RB1_FULL_VF2                 = 0x000000de,
IH_PERF_SEL_RB1_FULL_VF3                 = 0x000000df,
IH_PERF_SEL_RB1_FULL_VF4                 = 0x000000e0,
IH_PERF_SEL_RB1_FULL_VF5                 = 0x000000e1,
IH_PERF_SEL_RB1_FULL_VF6                 = 0x000000e2,
IH_PERF_SEL_RB1_FULL_VF7                 = 0x000000e3,
IH_PERF_SEL_RB1_FULL_VF8                 = 0x000000e4,
IH_PERF_SEL_RB1_FULL_VF9                 = 0x000000e5,
IH_PERF_SEL_RB1_FULL_VF10                = 0x000000e6,
IH_PERF_SEL_RB1_FULL_VF11                = 0x000000e7,
IH_PERF_SEL_RB1_FULL_VF12                = 0x000000e8,
IH_PERF_SEL_RB1_FULL_VF13                = 0x000000e9,
IH_PERF_SEL_RB1_FULL_VF14                = 0x000000ea,
IH_PERF_SEL_RB1_FULL_VF15                = 0x000000eb,
IH_PERF_SEL_RB1_OVERFLOW_VF0             = 0x000000ec,
IH_PERF_SEL_RB1_OVERFLOW_VF1             = 0x000000ed,
IH_PERF_SEL_RB1_OVERFLOW_VF2             = 0x000000ee,
IH_PERF_SEL_RB1_OVERFLOW_VF3             = 0x000000ef,
IH_PERF_SEL_RB1_OVERFLOW_VF4             = 0x000000f0,
IH_PERF_SEL_RB1_OVERFLOW_VF5             = 0x000000f1,
IH_PERF_SEL_RB1_OVERFLOW_VF6             = 0x000000f2,
IH_PERF_SEL_RB1_OVERFLOW_VF7             = 0x000000f3,
IH_PERF_SEL_RB1_OVERFLOW_VF8             = 0x000000f4,
IH_PERF_SEL_RB1_OVERFLOW_VF9             = 0x000000f5,
IH_PERF_SEL_RB1_OVERFLOW_VF10            = 0x000000f6,
IH_PERF_SEL_RB1_OVERFLOW_VF11            = 0x000000f7,
IH_PERF_SEL_RB1_OVERFLOW_VF12            = 0x000000f8,
IH_PERF_SEL_RB1_OVERFLOW_VF13            = 0x000000f9,
IH_PERF_SEL_RB1_OVERFLOW_VF14            = 0x000000fa,
IH_PERF_SEL_RB1_OVERFLOW_VF15            = 0x000000fb,
Reserved252                              = 0x000000fc,
Reserved253                              = 0x000000fd,
Reserved254                              = 0x000000fe,
Reserved255                              = 0x000000ff,
Reserved256                              = 0x00000100,
Reserved257                              = 0x00000101,
Reserved258                              = 0x00000102,
Reserved259                              = 0x00000103,
Reserved260                              = 0x00000104,
Reserved261                              = 0x00000105,
Reserved262                              = 0x00000106,
Reserved263                              = 0x00000107,
Reserved264                              = 0x00000108,
Reserved265                              = 0x00000109,
Reserved266                              = 0x0000010a,
Reserved267                              = 0x0000010b,
IH_PERF_SEL_RB1_WPTR_WRAP_VF0            = 0x0000010c,
IH_PERF_SEL_RB1_WPTR_WRAP_VF1            = 0x0000010d,
IH_PERF_SEL_RB1_WPTR_WRAP_VF2            = 0x0000010e,
IH_PERF_SEL_RB1_WPTR_WRAP_VF3            = 0x0000010f,
IH_PERF_SEL_RB1_WPTR_WRAP_VF4            = 0x00000110,
IH_PERF_SEL_RB1_WPTR_WRAP_VF5            = 0x00000111,
IH_PERF_SEL_RB1_WPTR_WRAP_VF6            = 0x00000112,
IH_PERF_SEL_RB1_WPTR_WRAP_VF7            = 0x00000113,
IH_PERF_SEL_RB1_WPTR_WRAP_VF8            = 0x00000114,
IH_PERF_SEL_RB1_WPTR_WRAP_VF9            = 0x00000115,
IH_PERF_SEL_RB1_WPTR_WRAP_VF10           = 0x00000116,
IH_PERF_SEL_RB1_WPTR_WRAP_VF11           = 0x00000117,
IH_PERF_SEL_RB1_WPTR_WRAP_VF12           = 0x00000118,
IH_PERF_SEL_RB1_WPTR_WRAP_VF13           = 0x00000119,
IH_PERF_SEL_RB1_WPTR_WRAP_VF14           = 0x0000011a,
IH_PERF_SEL_RB1_WPTR_WRAP_VF15           = 0x0000011b,
IH_PERF_SEL_RB1_RPTR_WRAP_VF0            = 0x0000011c,
IH_PERF_SEL_RB1_RPTR_WRAP_VF1            = 0x0000011d,
IH_PERF_SEL_RB1_RPTR_WRAP_VF2            = 0x0000011e,
IH_PERF_SEL_RB1_RPTR_WRAP_VF3            = 0x0000011f,
IH_PERF_SEL_RB1_RPTR_WRAP_VF4            = 0x00000120,
IH_PERF_SEL_RB1_RPTR_WRAP_VF5            = 0x00000121,
IH_PERF_SEL_RB1_RPTR_WRAP_VF6            = 0x00000122,
IH_PERF_SEL_RB1_RPTR_WRAP_VF7            = 0x00000123,
IH_PERF_SEL_RB1_RPTR_WRAP_VF8            = 0x00000124,
IH_PERF_SEL_RB1_RPTR_WRAP_VF9            = 0x00000125,
IH_PERF_SEL_RB1_RPTR_WRAP_VF10           = 0x00000126,
IH_PERF_SEL_RB1_RPTR_WRAP_VF11           = 0x00000127,
IH_PERF_SEL_RB1_RPTR_WRAP_VF12           = 0x00000128,
IH_PERF_SEL_RB1_RPTR_WRAP_VF13           = 0x00000129,
IH_PERF_SEL_RB1_RPTR_WRAP_VF14           = 0x0000012a,
IH_PERF_SEL_RB1_RPTR_WRAP_VF15           = 0x0000012b,
Reserved300                              = 0x0000012c,
Reserved301                              = 0x0000012d,
Reserved302                              = 0x0000012e,
Reserved303                              = 0x0000012f,
Reserved304                              = 0x00000130,
Reserved305                              = 0x00000131,
Reserved306                              = 0x00000132,
Reserved307                              = 0x00000133,
Reserved308                              = 0x00000134,
Reserved309                              = 0x00000135,
Reserved310                              = 0x00000136,
Reserved311                              = 0x00000137,
Reserved312                              = 0x00000138,
Reserved313                              = 0x00000139,
Reserved314                              = 0x0000013a,
Reserved315                              = 0x0000013b,
Reserved316                              = 0x0000013c,
Reserved317                              = 0x0000013d,
Reserved318                              = 0x0000013e,
Reserved319                              = 0x0000013f,
Reserved320                              = 0x00000140,
Reserved321                              = 0x00000141,
Reserved322                              = 0x00000142,
Reserved323                              = 0x00000143,
Reserved324                              = 0x00000144,
Reserved325                              = 0x00000145,
Reserved326                              = 0x00000146,
Reserved327                              = 0x00000147,
Reserved328                              = 0x00000148,
Reserved329                              = 0x00000149,
Reserved330                              = 0x0000014a,
Reserved331                              = 0x0000014b,
IH_PERF_SEL_RB2_FULL_VF0                 = 0x0000014c,
IH_PERF_SEL_RB2_FULL_VF1                 = 0x0000014d,
IH_PERF_SEL_RB2_FULL_VF2                 = 0x0000014e,
IH_PERF_SEL_RB2_FULL_VF3                 = 0x0000014f,
IH_PERF_SEL_RB2_FULL_VF4                 = 0x00000150,
IH_PERF_SEL_RB2_FULL_VF5                 = 0x00000151,
IH_PERF_SEL_RB2_FULL_VF6                 = 0x00000152,
IH_PERF_SEL_RB2_FULL_VF7                 = 0x00000153,
IH_PERF_SEL_RB2_FULL_VF8                 = 0x00000154,
IH_PERF_SEL_RB2_FULL_VF9                 = 0x00000155,
IH_PERF_SEL_RB2_FULL_VF10                = 0x00000156,
IH_PERF_SEL_RB2_FULL_VF11                = 0x00000157,
IH_PERF_SEL_RB2_FULL_VF12                = 0x00000158,
IH_PERF_SEL_RB2_FULL_VF13                = 0x00000159,
IH_PERF_SEL_RB2_FULL_VF14                = 0x0000015a,
IH_PERF_SEL_RB2_FULL_VF15                = 0x0000015b,
IH_PERF_SEL_RB2_OVERFLOW_VF0             = 0x0000015c,
IH_PERF_SEL_RB2_OVERFLOW_VF1             = 0x0000015d,
IH_PERF_SEL_RB2_OVERFLOW_VF2             = 0x0000015e,
IH_PERF_SEL_RB2_OVERFLOW_VF3             = 0x0000015f,
IH_PERF_SEL_RB2_OVERFLOW_VF4             = 0x00000160,
IH_PERF_SEL_RB2_OVERFLOW_VF5             = 0x00000161,
IH_PERF_SEL_RB2_OVERFLOW_VF6             = 0x00000162,
IH_PERF_SEL_RB2_OVERFLOW_VF7             = 0x00000163,
IH_PERF_SEL_RB2_OVERFLOW_VF8             = 0x00000164,
IH_PERF_SEL_RB2_OVERFLOW_VF9             = 0x00000165,
IH_PERF_SEL_RB2_OVERFLOW_VF10            = 0x00000166,
IH_PERF_SEL_RB2_OVERFLOW_VF11            = 0x00000167,
IH_PERF_SEL_RB2_OVERFLOW_VF12            = 0x00000168,
IH_PERF_SEL_RB2_OVERFLOW_VF13            = 0x00000169,
IH_PERF_SEL_RB2_OVERFLOW_VF14            = 0x0000016a,
IH_PERF_SEL_RB2_OVERFLOW_VF15            = 0x0000016b,
Reserved364                              = 0x0000016c,
Reserved365                              = 0x0000016d,
Reserved366                              = 0x0000016e,
Reserved367                              = 0x0000016f,
Reserved368                              = 0x00000170,
Reserved369                              = 0x00000171,
Reserved370                              = 0x00000172,
Reserved371                              = 0x00000173,
Reserved372                              = 0x00000174,
Reserved373                              = 0x00000175,
Reserved374                              = 0x00000176,
Reserved375                              = 0x00000177,
Reserved376                              = 0x00000178,
Reserved377                              = 0x00000179,
Reserved378                              = 0x0000017a,
Reserved379                              = 0x0000017b,
IH_PERF_SEL_RB2_WPTR_WRAP_VF0            = 0x0000017c,
IH_PERF_SEL_RB2_WPTR_WRAP_VF1            = 0x0000017d,
IH_PERF_SEL_RB2_WPTR_WRAP_VF2            = 0x0000017e,
IH_PERF_SEL_RB2_WPTR_WRAP_VF3            = 0x0000017f,
IH_PERF_SEL_RB2_WPTR_WRAP_VF4            = 0x00000180,
IH_PERF_SEL_RB2_WPTR_WRAP_VF5            = 0x00000181,
IH_PERF_SEL_RB2_WPTR_WRAP_VF6            = 0x00000182,
IH_PERF_SEL_RB2_WPTR_WRAP_VF7            = 0x00000183,
IH_PERF_SEL_RB2_WPTR_WRAP_VF8            = 0x00000184,
IH_PERF_SEL_RB2_WPTR_WRAP_VF9            = 0x00000185,
IH_PERF_SEL_RB2_WPTR_WRAP_VF10           = 0x00000186,
IH_PERF_SEL_RB2_WPTR_WRAP_VF11           = 0x00000187,
IH_PERF_SEL_RB2_WPTR_WRAP_VF12           = 0x00000188,
IH_PERF_SEL_RB2_WPTR_WRAP_VF13           = 0x00000189,
IH_PERF_SEL_RB2_WPTR_WRAP_VF14           = 0x0000018a,
IH_PERF_SEL_RB2_WPTR_WRAP_VF15           = 0x0000018b,
IH_PERF_SEL_RB2_RPTR_WRAP_VF0            = 0x0000018c,
IH_PERF_SEL_RB2_RPTR_WRAP_VF1            = 0x0000018d,
IH_PERF_SEL_RB2_RPTR_WRAP_VF2            = 0x0000018e,
IH_PERF_SEL_RB2_RPTR_WRAP_VF3            = 0x0000018f,
IH_PERF_SEL_RB2_RPTR_WRAP_VF4            = 0x00000190,
IH_PERF_SEL_RB2_RPTR_WRAP_VF5            = 0x00000191,
IH_PERF_SEL_RB2_RPTR_WRAP_VF6            = 0x00000192,
IH_PERF_SEL_RB2_RPTR_WRAP_VF7            = 0x00000193,
IH_PERF_SEL_RB2_RPTR_WRAP_VF8            = 0x00000194,
IH_PERF_SEL_RB2_RPTR_WRAP_VF9            = 0x00000195,
IH_PERF_SEL_RB2_RPTR_WRAP_VF10           = 0x00000196,
IH_PERF_SEL_RB2_RPTR_WRAP_VF11           = 0x00000197,
IH_PERF_SEL_RB2_RPTR_WRAP_VF12           = 0x00000198,
IH_PERF_SEL_RB2_RPTR_WRAP_VF13           = 0x00000199,
IH_PERF_SEL_RB2_RPTR_WRAP_VF14           = 0x0000019a,
IH_PERF_SEL_RB2_RPTR_WRAP_VF15           = 0x0000019b,
Reserved412                              = 0x0000019c,
Reserved413                              = 0x0000019d,
Reserved414                              = 0x0000019e,
Reserved415                              = 0x0000019f,
Reserved416                              = 0x000001a0,
Reserved417                              = 0x000001a1,
Reserved418                              = 0x000001a2,
Reserved419                              = 0x000001a3,
Reserved420                              = 0x000001a4,
Reserved421                              = 0x000001a5,
Reserved422                              = 0x000001a6,
Reserved423                              = 0x000001a7,
Reserved424                              = 0x000001a8,
Reserved425                              = 0x000001a9,
Reserved426                              = 0x000001aa,
Reserved427                              = 0x000001ab,
Reserved428                              = 0x000001ac,
Reserved429                              = 0x000001ad,
Reserved430                              = 0x000001ae,
Reserved431                              = 0x000001af,
Reserved432                              = 0x000001b0,
Reserved433                              = 0x000001b1,
Reserved434                              = 0x000001b2,
Reserved435                              = 0x000001b3,
Reserved436                              = 0x000001b4,
Reserved437                              = 0x000001b5,
Reserved438                              = 0x000001b6,
Reserved439                              = 0x000001b7,
Reserved440                              = 0x000001b8,
Reserved441                              = 0x000001b9,
Reserved442                              = 0x000001ba,
Reserved443                              = 0x000001bb,
Reserved444                              = 0x000001bc,
Reserved445                              = 0x000001bd,
Reserved446                              = 0x000001be,
Reserved447                              = 0x000001bf,
Reserved448                              = 0x000001c0,
Reserved449                              = 0x000001c1,
Reserved450                              = 0x000001c2,
Reserved451                              = 0x000001c3,
Reserved452                              = 0x000001c4,
Reserved453                              = 0x000001c5,
Reserved454                              = 0x000001c6,
Reserved455                              = 0x000001c7,
Reserved456                              = 0x000001c8,
Reserved457                              = 0x000001c9,
Reserved458                              = 0x000001ca,
Reserved459                              = 0x000001cb,
Reserved460                              = 0x000001cc,
Reserved461                              = 0x000001cd,
Reserved462                              = 0x000001ce,
Reserved463                              = 0x000001cf,
Reserved464                              = 0x000001d0,
Reserved465                              = 0x000001d1,
Reserved466                              = 0x000001d2,
Reserved467                              = 0x000001d3,
Reserved468                              = 0x000001d4,
Reserved469                              = 0x000001d5,
Reserved470                              = 0x000001d6,
Reserved471                              = 0x000001d7,
Reserved472                              = 0x000001d8,
Reserved473                              = 0x000001d9,
Reserved474                              = 0x000001da,
Reserved475                              = 0x000001db,
Reserved476                              = 0x000001dc,
Reserved477                              = 0x000001dd,
Reserved478                              = 0x000001de,
Reserved479                              = 0x000001df,
Reserved480                              = 0x000001e0,
Reserved481                              = 0x000001e1,
Reserved482                              = 0x000001e2,
Reserved483                              = 0x000001e3,
Reserved484                              = 0x000001e4,
Reserved485                              = 0x000001e5,
Reserved486                              = 0x000001e6,
Reserved487                              = 0x000001e7,
Reserved488                              = 0x000001e8,
Reserved489                              = 0x000001e9,
Reserved490                              = 0x000001ea,
Reserved491                              = 0x000001eb,
Reserved492                              = 0x000001ec,
Reserved493                              = 0x000001ed,
Reserved494                              = 0x000001ee,
Reserved495                              = 0x000001ef,
Reserved496                              = 0x000001f0,
Reserved497                              = 0x000001f1,
Reserved498                              = 0x000001f2,
Reserved499                              = 0x000001f3,
Reserved500                              = 0x000001f4,
Reserved501                              = 0x000001f5,
Reserved502                              = 0x000001f6,
Reserved503                              = 0x000001f7,
Reserved504                              = 0x000001f8,
Reserved505                              = 0x000001f9,
Reserved506                              = 0x000001fa,
Reserved507                              = 0x000001fb,
Reserved508                              = 0x000001fc,
Reserved509                              = 0x000001fd,
Reserved510                              = 0x000001fe,
Reserved511                              = 0x000001ff,
} IH_PERF_SEL;

/*******************************************************
 * SEM Enums
 *******************************************************/

/*
 * SEM_PERF_SEL enum
 */

typedef enum SEM_PERF_SEL {
SEM_PERF_SEL_CYCLE                       = 0x00000000,
SEM_PERF_SEL_IDLE                        = 0x00000001,
SEM_PERF_SEL_SDMA0_REQ_SIGNAL            = 0x00000002,
SEM_PERF_SEL_SDMA1_REQ_SIGNAL            = 0x00000003,
SEM_PERF_SEL_UVD_REQ_SIGNAL              = 0x00000004,
SEM_PERF_SEL_VCE0_REQ_SIGNAL             = 0x00000005,
SEM_PERF_SEL_ACP_REQ_SIGNAL              = 0x00000006,
SEM_PERF_SEL_ISP_REQ_SIGNAL              = 0x00000007,
SEM_PERF_SEL_VCE1_REQ_SIGNAL             = 0x00000008,
SEM_PERF_SEL_VP8_REQ_SIGNAL              = 0x00000009,
SEM_PERF_SEL_CPG_E0_REQ_SIGNAL           = 0x0000000a,
SEM_PERF_SEL_CPG_E1_REQ_SIGNAL           = 0x0000000b,
SEM_PERF_SEL_CPC1_IMME_E0_REQ_SIGNAL     = 0x0000000c,
SEM_PERF_SEL_CPC1_IMME_E1_REQ_SIGNAL     = 0x0000000d,
SEM_PERF_SEL_CPC1_IMME_E2_REQ_SIGNAL     = 0x0000000e,
SEM_PERF_SEL_CPC1_IMME_E3_REQ_SIGNAL     = 0x0000000f,
SEM_PERF_SEL_CPC2_IMME_E0_REQ_SIGNAL     = 0x00000010,
SEM_PERF_SEL_CPC2_IMME_E1_REQ_SIGNAL     = 0x00000011,
SEM_PERF_SEL_CPC2_IMME_E2_REQ_SIGNAL     = 0x00000012,
SEM_PERF_SEL_CPC2_IMME_E3_REQ_SIGNAL     = 0x00000013,
SEM_PERF_SEL_SDMA0_REQ_WAIT              = 0x00000014,
SEM_PERF_SEL_SDMA1_REQ_WAIT              = 0x00000015,
SEM_PERF_SEL_UVD_REQ_WAIT                = 0x00000016,
SEM_PERF_SEL_VCE0_REQ_WAIT               = 0x00000017,
SEM_PERF_SEL_ACP_REQ_WAIT                = 0x00000018,
SEM_PERF_SEL_ISP_REQ_WAIT                = 0x00000019,
SEM_PERF_SEL_VCE1_REQ_WAIT               = 0x0000001a,
SEM_PERF_SEL_VP8_REQ_WAIT                = 0x0000001b,
SEM_PERF_SEL_CPG_E0_REQ_WAIT             = 0x0000001c,
SEM_PERF_SEL_CPG_E1_REQ_WAIT             = 0x0000001d,
SEM_PERF_SEL_CPC1_IMME_E0_REQ_WAIT       = 0x0000001e,
SEM_PERF_SEL_CPC1_IMME_E1_REQ_WAIT       = 0x0000001f,
SEM_PERF_SEL_CPC1_IMME_E2_REQ_WAIT       = 0x00000020,
SEM_PERF_SEL_CPC1_IMME_E3_REQ_WAIT       = 0x00000021,
SEM_PERF_SEL_CPC2_IMME_E0_REQ_WAIT       = 0x00000022,
SEM_PERF_SEL_CPC2_IMME_E1_REQ_WAIT       = 0x00000023,
SEM_PERF_SEL_CPC2_IMME_E2_REQ_WAIT       = 0x00000024,
SEM_PERF_SEL_CPC2_IMME_E3_REQ_WAIT       = 0x00000025,
SEM_PERF_SEL_CPC1_OFFL_E0_REQ_WAIT       = 0x00000026,
SEM_PERF_SEL_CPC1_OFFL_E1_REQ_WAIT       = 0x00000027,
SEM_PERF_SEL_CPC1_OFFL_E2_REQ_WAIT       = 0x00000028,
SEM_PERF_SEL_CPC1_OFFL_E3_REQ_WAIT       = 0x00000029,
SEM_PERF_SEL_CPC1_OFFL_E4_REQ_WAIT       = 0x0000002a,
SEM_PERF_SEL_CPC1_OFFL_E5_REQ_WAIT       = 0x0000002b,
SEM_PERF_SEL_CPC1_OFFL_E6_REQ_WAIT       = 0x0000002c,
SEM_PERF_SEL_CPC1_OFFL_E7_REQ_WAIT       = 0x0000002d,
SEM_PERF_SEL_CPC1_OFFL_E8_REQ_WAIT       = 0x0000002e,
SEM_PERF_SEL_CPC1_OFFL_E9_REQ_WAIT       = 0x0000002f,
SEM_PERF_SEL_CPC1_OFFL_E10_REQ_WAIT      = 0x00000030,
SEM_PERF_SEL_CPC1_OFFL_E11_REQ_WAIT      = 0x00000031,
SEM_PERF_SEL_CPC1_OFFL_E12_REQ_WAIT      = 0x00000032,
SEM_PERF_SEL_CPC1_OFFL_E13_REQ_WAIT      = 0x00000033,
SEM_PERF_SEL_CPC1_OFFL_E14_REQ_WAIT      = 0x00000034,
SEM_PERF_SEL_CPC1_OFFL_E15_REQ_WAIT      = 0x00000035,
SEM_PERF_SEL_CPC1_OFFL_E16_REQ_WAIT      = 0x00000036,
SEM_PERF_SEL_CPC1_OFFL_E17_REQ_WAIT      = 0x00000037,
SEM_PERF_SEL_CPC1_OFFL_E18_REQ_WAIT      = 0x00000038,
SEM_PERF_SEL_CPC1_OFFL_E19_REQ_WAIT      = 0x00000039,
SEM_PERF_SEL_CPC1_OFFL_E20_REQ_WAIT      = 0x0000003a,
SEM_PERF_SEL_CPC1_OFFL_E21_REQ_WAIT      = 0x0000003b,
SEM_PERF_SEL_CPC1_OFFL_E22_REQ_WAIT      = 0x0000003c,
SEM_PERF_SEL_CPC1_OFFL_E23_REQ_WAIT      = 0x0000003d,
SEM_PERF_SEL_CPC1_OFFL_E24_REQ_WAIT      = 0x0000003e,
SEM_PERF_SEL_CPC1_OFFL_E25_REQ_WAIT      = 0x0000003f,
SEM_PERF_SEL_CPC1_OFFL_E26_REQ_WAIT      = 0x00000040,
SEM_PERF_SEL_CPC1_OFFL_E27_REQ_WAIT      = 0x00000041,
SEM_PERF_SEL_CPC1_OFFL_E28_REQ_WAIT      = 0x00000042,
SEM_PERF_SEL_CPC1_OFFL_E29_REQ_WAIT      = 0x00000043,
SEM_PERF_SEL_CPC1_OFFL_E30_REQ_WAIT      = 0x00000044,
SEM_PERF_SEL_CPC1_OFFL_E31_REQ_WAIT      = 0x00000045,
SEM_PERF_SEL_CPC2_OFFL_E0_REQ_WAIT       = 0x00000046,
SEM_PERF_SEL_CPC2_OFFL_E1_REQ_WAIT       = 0x00000047,
SEM_PERF_SEL_CPC2_OFFL_E2_REQ_WAIT       = 0x00000048,
SEM_PERF_SEL_CPC2_OFFL_E3_REQ_WAIT       = 0x00000049,
SEM_PERF_SEL_CPC2_OFFL_E4_REQ_WAIT       = 0x0000004a,
SEM_PERF_SEL_CPC2_OFFL_E5_REQ_WAIT       = 0x0000004b,
SEM_PERF_SEL_CPC2_OFFL_E6_REQ_WAIT       = 0x0000004c,
SEM_PERF_SEL_CPC2_OFFL_E7_REQ_WAIT       = 0x0000004d,
SEM_PERF_SEL_CPC2_OFFL_E8_REQ_WAIT       = 0x0000004e,
SEM_PERF_SEL_CPC2_OFFL_E9_REQ_WAIT       = 0x0000004f,
SEM_PERF_SEL_CPC2_OFFL_E10_REQ_WAIT      = 0x00000050,
SEM_PERF_SEL_CPC2_OFFL_E11_REQ_WAIT      = 0x00000051,
SEM_PERF_SEL_CPC2_OFFL_E12_REQ_WAIT      = 0x00000052,
SEM_PERF_SEL_CPC2_OFFL_E13_REQ_WAIT      = 0x00000053,
SEM_PERF_SEL_CPC2_OFFL_E14_REQ_WAIT      = 0x00000054,
SEM_PERF_SEL_CPC2_OFFL_E15_REQ_WAIT      = 0x00000055,
SEM_PERF_SEL_CPC2_OFFL_E16_REQ_WAIT      = 0x00000056,
SEM_PERF_SEL_CPC2_OFFL_E17_REQ_WAIT      = 0x00000057,
SEM_PERF_SEL_CPC2_OFFL_E18_REQ_WAIT      = 0x00000058,
SEM_PERF_SEL_CPC2_OFFL_E19_REQ_WAIT      = 0x00000059,
SEM_PERF_SEL_CPC2_OFFL_E20_REQ_WAIT      = 0x0000005a,
SEM_PERF_SEL_CPC2_OFFL_E21_REQ_WAIT      = 0x0000005b,
SEM_PERF_SEL_CPC2_OFFL_E22_REQ_WAIT      = 0x0000005c,
SEM_PERF_SEL_CPC2_OFFL_E23_REQ_WAIT      = 0x0000005d,
SEM_PERF_SEL_CPC2_OFFL_E24_REQ_WAIT      = 0x0000005e,
SEM_PERF_SEL_CPC2_OFFL_E25_REQ_WAIT      = 0x0000005f,
SEM_PERF_SEL_CPC2_OFFL_E26_REQ_WAIT      = 0x00000060,
SEM_PERF_SEL_CPC2_OFFL_E27_REQ_WAIT      = 0x00000061,
SEM_PERF_SEL_CPC2_OFFL_E28_REQ_WAIT      = 0x00000062,
SEM_PERF_SEL_CPC2_OFFL_E29_REQ_WAIT      = 0x00000063,
SEM_PERF_SEL_CPC2_OFFL_E30_REQ_WAIT      = 0x00000064,
SEM_PERF_SEL_CPC2_OFFL_E31_REQ_WAIT      = 0x00000065,
SEM_PERF_SEL_CPC1_OFFL_E0_POLL_WAIT      = 0x00000066,
SEM_PERF_SEL_CPC1_OFFL_E1_POLL_WAIT      = 0x00000067,
SEM_PERF_SEL_CPC1_OFFL_E2_POLL_WAIT      = 0x00000068,
SEM_PERF_SEL_CPC1_OFFL_E3_POLL_WAIT      = 0x00000069,
SEM_PERF_SEL_CPC1_OFFL_E4_POLL_WAIT      = 0x0000006a,
SEM_PERF_SEL_CPC1_OFFL_E5_POLL_WAIT      = 0x0000006b,
SEM_PERF_SEL_CPC1_OFFL_E6_POLL_WAIT      = 0x0000006c,
SEM_PERF_SEL_CPC1_OFFL_E7_POLL_WAIT      = 0x0000006d,
SEM_PERF_SEL_CPC1_OFFL_E8_POLL_WAIT      = 0x0000006e,
SEM_PERF_SEL_CPC1_OFFL_E9_POLL_WAIT      = 0x0000006f,
SEM_PERF_SEL_CPC1_OFFL_E10_POLL_WAIT     = 0x00000070,
SEM_PERF_SEL_CPC1_OFFL_E11_POLL_WAIT     = 0x00000071,
SEM_PERF_SEL_CPC1_OFFL_E12_POLL_WAIT     = 0x00000072,
SEM_PERF_SEL_CPC1_OFFL_E13_POLL_WAIT     = 0x00000073,
SEM_PERF_SEL_CPC1_OFFL_E14_POLL_WAIT     = 0x00000074,
SEM_PERF_SEL_CPC1_OFFL_E15_POLL_WAIT     = 0x00000075,
SEM_PERF_SEL_CPC1_OFFL_E16_POLL_WAIT     = 0x00000076,
SEM_PERF_SEL_CPC1_OFFL_E17_POLL_WAIT     = 0x00000077,
SEM_PERF_SEL_CPC1_OFFL_E18_POLL_WAIT     = 0x00000078,
SEM_PERF_SEL_CPC1_OFFL_E19_POLL_WAIT     = 0x00000079,
SEM_PERF_SEL_CPC1_OFFL_E20_POLL_WAIT     = 0x0000007a,
SEM_PERF_SEL_CPC1_OFFL_E21_POLL_WAIT     = 0x0000007b,
SEM_PERF_SEL_CPC1_OFFL_E22_POLL_WAIT     = 0x0000007c,
SEM_PERF_SEL_CPC1_OFFL_E23_POLL_WAIT     = 0x0000007d,
SEM_PERF_SEL_CPC1_OFFL_E24_POLL_WAIT     = 0x0000007e,
SEM_PERF_SEL_CPC1_OFFL_E25_POLL_WAIT     = 0x0000007f,
SEM_PERF_SEL_CPC1_OFFL_E26_POLL_WAIT     = 0x00000080,
SEM_PERF_SEL_CPC1_OFFL_E27_POLL_WAIT     = 0x00000081,
SEM_PERF_SEL_CPC1_OFFL_E28_POLL_WAIT     = 0x00000082,
SEM_PERF_SEL_CPC1_OFFL_E29_POLL_WAIT     = 0x00000083,
SEM_PERF_SEL_CPC1_OFFL_E30_POLL_WAIT     = 0x00000084,
SEM_PERF_SEL_CPC1_OFFL_E31_POLL_WAIT     = 0x00000085,
SEM_PERF_SEL_CPC2_OFFL_E0_POLL_WAIT      = 0x00000086,
SEM_PERF_SEL_CPC2_OFFL_E1_POLL_WAIT      = 0x00000087,
SEM_PERF_SEL_CPC2_OFFL_E2_POLL_WAIT      = 0x00000088,
SEM_PERF_SEL_CPC2_OFFL_E3_POLL_WAIT      = 0x00000089,
SEM_PERF_SEL_CPC2_OFFL_E4_POLL_WAIT      = 0x0000008a,
SEM_PERF_SEL_CPC2_OFFL_E5_POLL_WAIT      = 0x0000008b,
SEM_PERF_SEL_CPC2_OFFL_E6_POLL_WAIT      = 0x0000008c,
SEM_PERF_SEL_CPC2_OFFL_E7_POLL_WAIT      = 0x0000008d,
SEM_PERF_SEL_CPC2_OFFL_E8_POLL_WAIT      = 0x0000008e,
SEM_PERF_SEL_CPC2_OFFL_E9_POLL_WAIT      = 0x0000008f,
SEM_PERF_SEL_CPC2_OFFL_E10_POLL_WAIT     = 0x00000090,
SEM_PERF_SEL_CPC2_OFFL_E11_POLL_WAIT     = 0x00000091,
SEM_PERF_SEL_CPC2_OFFL_E12_POLL_WAIT     = 0x00000092,
SEM_PERF_SEL_CPC2_OFFL_E13_POLL_WAIT     = 0x00000093,
SEM_PERF_SEL_CPC2_OFFL_E14_POLL_WAIT     = 0x00000094,
SEM_PERF_SEL_CPC2_OFFL_E15_POLL_WAIT     = 0x00000095,
SEM_PERF_SEL_CPC2_OFFL_E16_POLL_WAIT     = 0x00000096,
SEM_PERF_SEL_CPC2_OFFL_E17_POLL_WAIT     = 0x00000097,
SEM_PERF_SEL_CPC2_OFFL_E18_POLL_WAIT     = 0x00000098,
SEM_PERF_SEL_CPC2_OFFL_E19_POLL_WAIT     = 0x00000099,
SEM_PERF_SEL_CPC2_OFFL_E20_POLL_WAIT     = 0x0000009a,
SEM_PERF_SEL_CPC2_OFFL_E21_POLL_WAIT     = 0x0000009b,
SEM_PERF_SEL_CPC2_OFFL_E22_POLL_WAIT     = 0x0000009c,
SEM_PERF_SEL_CPC2_OFFL_E23_POLL_WAIT     = 0x0000009d,
SEM_PERF_SEL_CPC2_OFFL_E24_POLL_WAIT     = 0x0000009e,
SEM_PERF_SEL_CPC2_OFFL_E25_POLL_WAIT     = 0x0000009f,
SEM_PERF_SEL_CPC2_OFFL_E26_POLL_WAIT     = 0x000000a0,
SEM_PERF_SEL_CPC2_OFFL_E27_POLL_WAIT     = 0x000000a1,
SEM_PERF_SEL_CPC2_OFFL_E28_POLL_WAIT     = 0x000000a2,
SEM_PERF_SEL_CPC2_OFFL_E29_POLL_WAIT     = 0x000000a3,
SEM_PERF_SEL_CPC2_OFFL_E30_POLL_WAIT     = 0x000000a4,
SEM_PERF_SEL_CPC2_OFFL_E31_POLL_WAIT     = 0x000000a5,
SEM_PERF_SEL_MC_RD_REQ                   = 0x000000a6,
SEM_PERF_SEL_MC_RD_RET                   = 0x000000a7,
SEM_PERF_SEL_MC_WR_REQ                   = 0x000000a8,
SEM_PERF_SEL_MC_WR_RET                   = 0x000000a9,
SEM_PERF_SEL_ATC_REQ                     = 0x000000aa,
SEM_PERF_SEL_ATC_RET                     = 0x000000ab,
SEM_PERF_SEL_ATC_XNACK                   = 0x000000ac,
SEM_PERF_SEL_ATC_INVALIDATION            = 0x000000ad,
} SEM_PERF_SEL;

/*******************************************************
 * SDMA Enums
 *******************************************************/

/*
 * SDMA_PERF_SEL enum
 */

typedef enum SDMA_PERF_SEL {
SDMA_PERF_SEL_CYCLE                      = 0x00000000,
SDMA_PERF_SEL_IDLE                       = 0x00000001,
SDMA_PERF_SEL_REG_IDLE                   = 0x00000002,
SDMA_PERF_SEL_RB_EMPTY                   = 0x00000003,
SDMA_PERF_SEL_RB_FULL                    = 0x00000004,
SDMA_PERF_SEL_RB_WPTR_WRAP               = 0x00000005,
SDMA_PERF_SEL_RB_RPTR_WRAP               = 0x00000006,
SDMA_PERF_SEL_RB_WPTR_POLL_READ          = 0x00000007,
SDMA_PERF_SEL_RB_RPTR_WB                 = 0x00000008,
SDMA_PERF_SEL_RB_CMD_IDLE                = 0x00000009,
SDMA_PERF_SEL_RB_CMD_FULL                = 0x0000000a,
SDMA_PERF_SEL_IB_CMD_IDLE                = 0x0000000b,
SDMA_PERF_SEL_IB_CMD_FULL                = 0x0000000c,
SDMA_PERF_SEL_EX_IDLE                    = 0x0000000d,
SDMA_PERF_SEL_SRBM_REG_SEND              = 0x0000000e,
SDMA_PERF_SEL_EX_IDLE_POLL_TIMER_EXPIRE  = 0x0000000f,
SDMA_PERF_SEL_MC_WR_IDLE                 = 0x00000010,
SDMA_PERF_SEL_MC_WR_COUNT                = 0x00000011,
SDMA_PERF_SEL_MC_RD_IDLE                 = 0x00000012,
SDMA_PERF_SEL_MC_RD_COUNT                = 0x00000013,
SDMA_PERF_SEL_MC_RD_RET_STALL            = 0x00000014,
SDMA_PERF_SEL_MC_RD_NO_POLL_IDLE         = 0x00000015,
SDMA_PERF_SEL_SEM_IDLE                   = 0x00000018,
SDMA_PERF_SEL_SEM_REQ_STALL              = 0x00000019,
SDMA_PERF_SEL_SEM_REQ_COUNT              = 0x0000001a,
SDMA_PERF_SEL_SEM_RESP_INCOMPLETE        = 0x0000001b,
SDMA_PERF_SEL_SEM_RESP_FAIL              = 0x0000001c,
SDMA_PERF_SEL_SEM_RESP_PASS              = 0x0000001d,
SDMA_PERF_SEL_INT_IDLE                   = 0x0000001e,
SDMA_PERF_SEL_INT_REQ_STALL              = 0x0000001f,
SDMA_PERF_SEL_INT_REQ_COUNT              = 0x00000020,
SDMA_PERF_SEL_INT_RESP_ACCEPTED          = 0x00000021,
SDMA_PERF_SEL_INT_RESP_RETRY             = 0x00000022,
SDMA_PERF_SEL_NUM_PACKET                 = 0x00000023,
SDMA_PERF_SEL_CE_WREQ_IDLE               = 0x00000025,
SDMA_PERF_SEL_CE_WR_IDLE                 = 0x00000026,
SDMA_PERF_SEL_CE_SPLIT_IDLE              = 0x00000027,
SDMA_PERF_SEL_CE_RREQ_IDLE               = 0x00000028,
SDMA_PERF_SEL_CE_OUT_IDLE                = 0x00000029,
SDMA_PERF_SEL_CE_IN_IDLE                 = 0x0000002a,
SDMA_PERF_SEL_CE_DST_IDLE                = 0x0000002b,
SDMA_PERF_SEL_CE_AFIFO_FULL              = 0x0000002e,
SDMA_PERF_SEL_CE_INFO_FULL               = 0x00000031,
SDMA_PERF_SEL_CE_INFO1_FULL              = 0x00000032,
SDMA_PERF_SEL_CE_RD_STALL                = 0x00000033,
SDMA_PERF_SEL_CE_WR_STALL                = 0x00000034,
SDMA_PERF_SEL_GFX_SELECT                 = 0x00000035,
SDMA_PERF_SEL_RLC0_SELECT                = 0x00000036,
SDMA_PERF_SEL_RLC1_SELECT                = 0x00000037,
SDMA_PERF_SEL_PAGE_SELECT                = 0x00000038,
SDMA_PERF_SEL_CTX_CHANGE                 = 0x00000039,
SDMA_PERF_SEL_CTX_CHANGE_EXPIRED         = 0x0000003a,
SDMA_PERF_SEL_CTX_CHANGE_EXCEPTION       = 0x0000003b,
SDMA_PERF_SEL_DOORBELL                   = 0x0000003c,
SDMA_PERF_SEL_RD_BA_RTR                  = 0x0000003d,
SDMA_PERF_SEL_WR_BA_RTR                  = 0x0000003e,
SDMA_PERF_SEL_F32_L1_WR_VLD              = 0x0000003f,
SDMA_PERF_SEL_CE_L1_WR_VLD               = 0x00000040,
SDMA_PERF_SEL_CE_L1_STALL                = 0x00000041,
SDMA_PERF_SEL_SDMA_INVACK_NFLUSH         = 0x00000042,
SDMA_PERF_SEL_SDMA_INVACK_FLUSH          = 0x00000043,
SDMA_PERF_SEL_ATCL2_INVREQ_NFLUSH        = 0x00000044,
SDMA_PERF_SEL_ATCL2_INVREQ_FLUSH         = 0x00000045,
SDMA_PERF_SEL_ATCL2_RET_XNACK            = 0x00000046,
SDMA_PERF_SEL_ATCL2_RET_ACK              = 0x00000047,
SDMA_PERF_SEL_ATCL2_FREE                 = 0x00000048,
SDMA_PERF_SEL_SDMA_ATCL2_SEND            = 0x00000049,
SDMA_PERF_SEL_DMA_L1_WR_SEND             = 0x0000004a,
SDMA_PERF_SEL_DMA_L1_RD_SEND             = 0x0000004b,
SDMA_PERF_SEL_DMA_MC_WR_SEND             = 0x0000004c,
SDMA_PERF_SEL_DMA_MC_RD_SEND             = 0x0000004d,
SDMA_PERF_SEL_L1_WR_FIFO_IDLE            = 0x0000004e,
SDMA_PERF_SEL_L1_RD_FIFO_IDLE            = 0x0000004f,
SDMA_PERF_SEL_L1_WRL2_IDLE               = 0x00000050,
SDMA_PERF_SEL_L1_RDL2_IDLE               = 0x00000051,
SDMA_PERF_SEL_L1_WRMC_IDLE               = 0x00000052,
SDMA_PERF_SEL_L1_RDMC_IDLE               = 0x00000053,
SDMA_PERF_SEL_L1_WR_INV_IDLE             = 0x00000054,
SDMA_PERF_SEL_L1_RD_INV_IDLE             = 0x00000055,
SDMA_PERF_SEL_L1_WR_INV_EN               = 0x00000056,
SDMA_PERF_SEL_L1_RD_INV_EN               = 0x00000057,
SDMA_PERF_SEL_L1_WR_WAIT_INVADR          = 0x00000058,
SDMA_PERF_SEL_L1_RD_WAIT_INVADR          = 0x00000059,
SDMA_PERF_SEL_IS_INVREQ_ADDR_WR          = 0x0000005a,
SDMA_PERF_SEL_IS_INVREQ_ADDR_RD          = 0x0000005b,
SDMA_PERF_SEL_L1_WR_XNACK_TIMEOUT        = 0x0000005c,
SDMA_PERF_SEL_L1_RD_XNACK_TIMEOUT        = 0x0000005d,
SDMA_PERF_SEL_L1_INV_MIDDLE              = 0x0000005e,
SDMA_PERF_SEL_UTCL1_TAG_DELAY_COUNTER    = 0x000000fe,
SDMA_PERF_SEL_MMHUB_TAG_DELAY_COUNTER    = 0x000000ff,
} SDMA_PERF_SEL;

/*******************************************************
 * SMUIO Enums
 *******************************************************/

/*
 * ROM_SIGNATURE value
 */

#define ROM_SIGNATURE                  0x0000aa55

/*******************************************************
 * XDMA_CMN Enums
 *******************************************************/

/*
 * ENUM_XDMA_LOCAL_SW_MODE enum
 */

typedef enum ENUM_XDMA_LOCAL_SW_MODE {
XDMA_LOCAL_SW_MODE_SW_256B_D             = 0x00000002,
XDMA_LOCAL_SW_MODE_SW_64KB_D             = 0x0000000a,
XDMA_LOCAL_SW_MODE_SW_64KB_D_X           = 0x0000001a,
} ENUM_XDMA_LOCAL_SW_MODE;

/*******************************************************
 * XDMA_SLV Enums
 *******************************************************/

/*
 * ENUM_XDMA_SLV_ALPHA_POSITION enum
 */

typedef enum ENUM_XDMA_SLV_ALPHA_POSITION {
XDMA_SLV_ALPHA_POSITION_7_0              = 0x00000000,
XDMA_SLV_ALPHA_POSITION_15_8             = 0x00000001,
XDMA_SLV_ALPHA_POSITION_23_16            = 0x00000002,
XDMA_SLV_ALPHA_POSITION_31_24            = 0x00000003,
} ENUM_XDMA_SLV_ALPHA_POSITION;

/*******************************************************
 * XDMA_MSTR Enums
 *******************************************************/

/*
 * ENUM_XDMA_MSTR_ALPHA_POSITION enum
 */

typedef enum ENUM_XDMA_MSTR_ALPHA_POSITION {
XDMA_MSTR_ALPHA_POSITION_7_0             = 0x00000000,
XDMA_MSTR_ALPHA_POSITION_15_8            = 0x00000001,
XDMA_MSTR_ALPHA_POSITION_23_16           = 0x00000002,
XDMA_MSTR_ALPHA_POSITION_31_24           = 0x00000003,
} ENUM_XDMA_MSTR_ALPHA_POSITION;

/*
 * ENUM_XDMA_MSTR_VSYNC_GSL_CHECK_SEL enum
 */

typedef enum ENUM_XDMA_MSTR_VSYNC_GSL_CHECK_SEL {
XDMA_MSTR_VSYNC_GSL_CHECK_SEL_PIPE0      = 0x00000000,
XDMA_MSTR_VSYNC_GSL_CHECK_SEL_PIPE1      = 0x00000001,
XDMA_MSTR_VSYNC_GSL_CHECK_SEL_PIPE2      = 0x00000002,
XDMA_MSTR_VSYNC_GSL_CHECK_SEL_PIPE3      = 0x00000003,
XDMA_MSTR_VSYNC_GSL_CHECK_SEL_PIPE4      = 0x00000004,
XDMA_MSTR_VSYNC_GSL_CHECK_SEL_PIPE5      = 0x00000005,
} ENUM_XDMA_MSTR_VSYNC_GSL_CHECK_SEL;


#endif /*_vega10_ENUM_HEADER*/

