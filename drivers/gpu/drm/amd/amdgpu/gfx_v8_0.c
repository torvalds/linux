/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include <linux/firmware.h>
#include "drmP.h"
#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "vi.h"
#include "vid.h"
#include "amdgpu_ucode.h"
#include "amdgpu_atombios.h"
#include "clearstate_vi.h"

#include "gmc/gmc_8_2_d.h"
#include "gmc/gmc_8_2_sh_mask.h"

#include "oss/oss_3_0_d.h"
#include "oss/oss_3_0_sh_mask.h"

#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"

#include "gca/gfx_8_0_d.h"
#include "gca/gfx_8_0_enum.h"
#include "gca/gfx_8_0_sh_mask.h"
#include "gca/gfx_8_0_enum.h"

#include "dce/dce_10_0_d.h"
#include "dce/dce_10_0_sh_mask.h"

#define GFX8_NUM_GFX_RINGS     1
#define GFX8_NUM_COMPUTE_RINGS 8

#define TOPAZ_GB_ADDR_CONFIG_GOLDEN 0x22010001
#define CARRIZO_GB_ADDR_CONFIG_GOLDEN 0x22010001
#define POLARIS11_GB_ADDR_CONFIG_GOLDEN 0x22011002
#define TONGA_GB_ADDR_CONFIG_GOLDEN 0x22011003

#define ARRAY_MODE(x)					((x) << GB_TILE_MODE0__ARRAY_MODE__SHIFT)
#define PIPE_CONFIG(x)					((x) << GB_TILE_MODE0__PIPE_CONFIG__SHIFT)
#define TILE_SPLIT(x)					((x) << GB_TILE_MODE0__TILE_SPLIT__SHIFT)
#define MICRO_TILE_MODE_NEW(x)				((x) << GB_TILE_MODE0__MICRO_TILE_MODE_NEW__SHIFT)
#define SAMPLE_SPLIT(x)					((x) << GB_TILE_MODE0__SAMPLE_SPLIT__SHIFT)
#define BANK_WIDTH(x)					((x) << GB_MACROTILE_MODE0__BANK_WIDTH__SHIFT)
#define BANK_HEIGHT(x)					((x) << GB_MACROTILE_MODE0__BANK_HEIGHT__SHIFT)
#define MACRO_TILE_ASPECT(x)				((x) << GB_MACROTILE_MODE0__MACRO_TILE_ASPECT__SHIFT)
#define NUM_BANKS(x)					((x) << GB_MACROTILE_MODE0__NUM_BANKS__SHIFT)

#define RLC_CGTT_MGCG_OVERRIDE__CPF_MASK            0x00000001L
#define RLC_CGTT_MGCG_OVERRIDE__RLC_MASK            0x00000002L
#define RLC_CGTT_MGCG_OVERRIDE__MGCG_MASK           0x00000004L
#define RLC_CGTT_MGCG_OVERRIDE__CGCG_MASK           0x00000008L
#define RLC_CGTT_MGCG_OVERRIDE__CGLS_MASK           0x00000010L
#define RLC_CGTT_MGCG_OVERRIDE__GRBM_MASK           0x00000020L

/* BPM SERDES CMD */
#define SET_BPM_SERDES_CMD    1
#define CLE_BPM_SERDES_CMD    0

/* BPM Register Address*/
enum {
	BPM_REG_CGLS_EN = 0,        /* Enable/Disable CGLS */
	BPM_REG_CGLS_ON,            /* ON/OFF CGLS: shall be controlled by RLC FW */
	BPM_REG_CGCG_OVERRIDE,      /* Set/Clear CGCG Override */
	BPM_REG_MGCG_OVERRIDE,      /* Set/Clear MGCG Override */
	BPM_REG_FGCG_OVERRIDE,      /* Set/Clear FGCG Override */
	BPM_REG_FGCG_MAX
};

#define RLC_FormatDirectRegListLength        14

MODULE_FIRMWARE("amdgpu/carrizo_ce.bin");
MODULE_FIRMWARE("amdgpu/carrizo_pfp.bin");
MODULE_FIRMWARE("amdgpu/carrizo_me.bin");
MODULE_FIRMWARE("amdgpu/carrizo_mec.bin");
MODULE_FIRMWARE("amdgpu/carrizo_mec2.bin");
MODULE_FIRMWARE("amdgpu/carrizo_rlc.bin");

MODULE_FIRMWARE("amdgpu/stoney_ce.bin");
MODULE_FIRMWARE("amdgpu/stoney_pfp.bin");
MODULE_FIRMWARE("amdgpu/stoney_me.bin");
MODULE_FIRMWARE("amdgpu/stoney_mec.bin");
MODULE_FIRMWARE("amdgpu/stoney_rlc.bin");

MODULE_FIRMWARE("amdgpu/tonga_ce.bin");
MODULE_FIRMWARE("amdgpu/tonga_pfp.bin");
MODULE_FIRMWARE("amdgpu/tonga_me.bin");
MODULE_FIRMWARE("amdgpu/tonga_mec.bin");
MODULE_FIRMWARE("amdgpu/tonga_mec2.bin");
MODULE_FIRMWARE("amdgpu/tonga_rlc.bin");

MODULE_FIRMWARE("amdgpu/topaz_ce.bin");
MODULE_FIRMWARE("amdgpu/topaz_pfp.bin");
MODULE_FIRMWARE("amdgpu/topaz_me.bin");
MODULE_FIRMWARE("amdgpu/topaz_mec.bin");
MODULE_FIRMWARE("amdgpu/topaz_rlc.bin");

MODULE_FIRMWARE("amdgpu/fiji_ce.bin");
MODULE_FIRMWARE("amdgpu/fiji_pfp.bin");
MODULE_FIRMWARE("amdgpu/fiji_me.bin");
MODULE_FIRMWARE("amdgpu/fiji_mec.bin");
MODULE_FIRMWARE("amdgpu/fiji_mec2.bin");
MODULE_FIRMWARE("amdgpu/fiji_rlc.bin");

MODULE_FIRMWARE("amdgpu/polaris11_ce.bin");
MODULE_FIRMWARE("amdgpu/polaris11_pfp.bin");
MODULE_FIRMWARE("amdgpu/polaris11_me.bin");
MODULE_FIRMWARE("amdgpu/polaris11_mec.bin");
MODULE_FIRMWARE("amdgpu/polaris11_mec2.bin");
MODULE_FIRMWARE("amdgpu/polaris11_rlc.bin");

MODULE_FIRMWARE("amdgpu/polaris10_ce.bin");
MODULE_FIRMWARE("amdgpu/polaris10_pfp.bin");
MODULE_FIRMWARE("amdgpu/polaris10_me.bin");
MODULE_FIRMWARE("amdgpu/polaris10_mec.bin");
MODULE_FIRMWARE("amdgpu/polaris10_mec2.bin");
MODULE_FIRMWARE("amdgpu/polaris10_rlc.bin");

static const struct amdgpu_gds_reg_offset amdgpu_gds_reg_offset[] =
{
	{mmGDS_VMID0_BASE, mmGDS_VMID0_SIZE, mmGDS_GWS_VMID0, mmGDS_OA_VMID0},
	{mmGDS_VMID1_BASE, mmGDS_VMID1_SIZE, mmGDS_GWS_VMID1, mmGDS_OA_VMID1},
	{mmGDS_VMID2_BASE, mmGDS_VMID2_SIZE, mmGDS_GWS_VMID2, mmGDS_OA_VMID2},
	{mmGDS_VMID3_BASE, mmGDS_VMID3_SIZE, mmGDS_GWS_VMID3, mmGDS_OA_VMID3},
	{mmGDS_VMID4_BASE, mmGDS_VMID4_SIZE, mmGDS_GWS_VMID4, mmGDS_OA_VMID4},
	{mmGDS_VMID5_BASE, mmGDS_VMID5_SIZE, mmGDS_GWS_VMID5, mmGDS_OA_VMID5},
	{mmGDS_VMID6_BASE, mmGDS_VMID6_SIZE, mmGDS_GWS_VMID6, mmGDS_OA_VMID6},
	{mmGDS_VMID7_BASE, mmGDS_VMID7_SIZE, mmGDS_GWS_VMID7, mmGDS_OA_VMID7},
	{mmGDS_VMID8_BASE, mmGDS_VMID8_SIZE, mmGDS_GWS_VMID8, mmGDS_OA_VMID8},
	{mmGDS_VMID9_BASE, mmGDS_VMID9_SIZE, mmGDS_GWS_VMID9, mmGDS_OA_VMID9},
	{mmGDS_VMID10_BASE, mmGDS_VMID10_SIZE, mmGDS_GWS_VMID10, mmGDS_OA_VMID10},
	{mmGDS_VMID11_BASE, mmGDS_VMID11_SIZE, mmGDS_GWS_VMID11, mmGDS_OA_VMID11},
	{mmGDS_VMID12_BASE, mmGDS_VMID12_SIZE, mmGDS_GWS_VMID12, mmGDS_OA_VMID12},
	{mmGDS_VMID13_BASE, mmGDS_VMID13_SIZE, mmGDS_GWS_VMID13, mmGDS_OA_VMID13},
	{mmGDS_VMID14_BASE, mmGDS_VMID14_SIZE, mmGDS_GWS_VMID14, mmGDS_OA_VMID14},
	{mmGDS_VMID15_BASE, mmGDS_VMID15_SIZE, mmGDS_GWS_VMID15, mmGDS_OA_VMID15}
};

static const u32 golden_settings_tonga_a11[] =
{
	mmCB_HW_CONTROL, 0xfffdf3cf, 0x00007208,
	mmCB_HW_CONTROL_3, 0x00000040, 0x00000040,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmGB_GPU_ID, 0x0000000f, 0x00000000,
	mmPA_SC_ENHANCE, 0xffffffff, 0x20000001,
	mmPA_SC_FIFO_DEPTH_CNTL, 0x000003ff, 0x000000fc,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmSQ_RANDOM_WAVE_PRI, 0x001fffff, 0x000006fd,
	mmTA_CNTL_AUX, 0x000f000f, 0x000b0000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCC_EXE_DISABLE, 0x00000002, 0x00000002,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000002fb,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x0000543b,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0xa9210876,
	mmVGT_RESET_DEBUG, 0x00000004, 0x00000004,
};

static const u32 tonga_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmPA_SC_RASTER_CONFIG, 0xffffffff, 0x16000012,
	mmPA_SC_RASTER_CONFIG_1, 0xffffffff, 0x0000002A,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x22011003,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00007FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00007FAF
};

static const u32 tonga_mgcg_cgcg_init[] =
{
	mmRLC_CGTT_MGCG_OVERRIDE, 0xffffffff, 0xffffffff,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCB_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_BCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CPC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CPF_CLK_CTRL, 0xffffffff, 0x40000100,
	mmCGTT_GDS_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_IA_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PA_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_WD_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_RLC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SPI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQ_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQG_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL0, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL1, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL2, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL3, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL4, 0xffffffff, 0x00000100,
	mmCGTT_TCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_TCP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_VGT_CLK_CTRL, 0xffffffff, 0x06000100,
	mmDB_CGTT_CLK_CTRL_0, 0xffffffff, 0x00000100,
	mmTA_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmTCA_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTCC_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTD_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCGTS_CU0_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU0_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU0_TA_SQC_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU0_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU0_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU1_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU1_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU1_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU1_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU1_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU2_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU2_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU2_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU2_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU2_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU3_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU3_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU3_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU3_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU3_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU4_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU4_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU4_TA_SQC_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU4_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU4_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU5_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU5_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU5_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU5_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU5_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU6_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU6_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU6_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU6_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU6_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU7_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU7_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU7_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU7_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU7_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96e00200,
	mmCP_RB_WPTR_POLL_CNTL, 0xffffffff, 0x00900100,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003c,
	mmCP_MEM_SLP_CNTL, 0x00000001, 0x00000001,
};

static const u32 golden_settings_polaris11_a11[] =
{
	mmCB_HW_CONTROL, 0xfffdf3cf, 0x00006208,
	mmCB_HW_CONTROL_3, 0x000001ff, 0x00000040,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmPA_SC_ENHANCE, 0xffffffff, 0x20000001,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmPA_SC_RASTER_CONFIG, 0x3f3fffff, 0x16000012,
	mmPA_SC_RASTER_CONFIG_1, 0x0000003f, 0x00000000,
	mmRLC_CGCG_CGLS_CTRL, 0x00000003, 0x0001003c,
	mmRLC_CGCG_CGLS_CTRL_3D, 0xffffffff, 0x0001003c,
	mmSQ_CONFIG, 0x07f80000, 0x07180000,
	mmTA_CNTL_AUX, 0x000f000f, 0x000b0000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000000f3,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0x00003210,
};

static const u32 polaris11_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x22011002,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00007FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00007FAF,
};

static const u32 golden_settings_polaris10_a11[] =
{
	mmATC_MISC_CG, 0x000c0fc0, 0x000c0200,
	mmCB_HW_CONTROL, 0xfffdf3cf, 0x00007208,
	mmCB_HW_CONTROL_2, 0, 0x0f000000,
	mmCB_HW_CONTROL_3, 0x000001ff, 0x00000040,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmPA_SC_ENHANCE, 0xffffffff, 0x20000001,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmPA_SC_RASTER_CONFIG, 0x3f3fffff, 0x16000012,
	mmPA_SC_RASTER_CONFIG_1, 0x0000003f, 0x0000002a,
	mmRLC_CGCG_CGLS_CTRL, 0x00000003, 0x0001003c,
	mmRLC_CGCG_CGLS_CTRL_3D, 0xffffffff, 0x0001003c,
	mmSQ_CONFIG, 0x07f80000, 0x07180000,
	mmTA_CNTL_AUX, 0x000f000f, 0x000b0000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000000f7,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000,
};

static const u32 polaris10_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmPA_SC_RASTER_CONFIG, 0xffffffff, 0x16000012,
	mmPA_SC_RASTER_CONFIG_1, 0xffffffff, 0x0000002A,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x22011003,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00007FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00007FAF,
};

static const u32 fiji_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmPA_SC_RASTER_CONFIG, 0xffffffff, 0x3a00161a,
	mmPA_SC_RASTER_CONFIG_1, 0xffffffff, 0x0000002e,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x22011003,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00007FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00007FAF,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmSPI_CONFIG_CNTL_1, 0x0000000f, 0x00000009,
};

static const u32 golden_settings_fiji_a10[] =
{
	mmCB_HW_CONTROL_3, 0x000001ff, 0x00000040,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmPA_SC_ENHANCE, 0xffffffff, 0x20000001,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmRLC_CGCG_CGLS_CTRL, 0x00000003, 0x0001003c,
	mmSQ_RANDOM_WAVE_PRI, 0x001fffff, 0x000006fd,
	mmTA_CNTL_AUX, 0x000f000f, 0x000b0000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCC_EXE_DISABLE, 0x00000002, 0x00000002,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000000ff,
	mmVGT_RESET_DEBUG, 0x00000004, 0x00000004,
};

static const u32 fiji_mgcg_cgcg_init[] =
{
	mmRLC_CGTT_MGCG_OVERRIDE, 0xffffffff, 0xffffffff,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCB_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_BCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CPC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CPF_CLK_CTRL, 0xffffffff, 0x40000100,
	mmCGTT_GDS_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_IA_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PA_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_WD_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_RLC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SPI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQ_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQG_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL0, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL1, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL2, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL3, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL4, 0xffffffff, 0x00000100,
	mmCGTT_TCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_TCP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_VGT_CLK_CTRL, 0xffffffff, 0x06000100,
	mmDB_CGTT_CLK_CTRL_0, 0xffffffff, 0x00000100,
	mmTA_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmTCA_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTCC_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTD_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96e00200,
	mmCP_RB_WPTR_POLL_CNTL, 0xffffffff, 0x00900100,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003c,
	mmCP_MEM_SLP_CNTL, 0x00000001, 0x00000001,
};

static const u32 golden_settings_iceland_a11[] =
{
	mmCB_HW_CONTROL_3, 0x00000040, 0x00000040,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmDB_DEBUG3, 0xc0000000, 0xc0000000,
	mmGB_GPU_ID, 0x0000000f, 0x00000000,
	mmPA_SC_ENHANCE, 0xffffffff, 0x20000001,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmPA_SC_RASTER_CONFIG, 0x3f3fffff, 0x00000002,
	mmPA_SC_RASTER_CONFIG_1, 0x0000003f, 0x00000000,
	mmSQ_RANDOM_WAVE_PRI, 0x001fffff, 0x000006fd,
	mmTA_CNTL_AUX, 0x000f000f, 0x000b0000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCC_EXE_DISABLE, 0x00000002, 0x00000002,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000000f1,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0x00000010,
};

static const u32 iceland_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmPA_SC_RASTER_CONFIG, 0xffffffff, 0x00000002,
	mmPA_SC_RASTER_CONFIG_1, 0xffffffff, 0x00000000,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x22010001,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00007FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00007FAF
};

static const u32 iceland_mgcg_cgcg_init[] =
{
	mmRLC_CGTT_MGCG_OVERRIDE, 0xffffffff, 0xffffffff,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCB_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_BCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CP_CLK_CTRL, 0xffffffff, 0xc0000100,
	mmCGTT_CPC_CLK_CTRL, 0xffffffff, 0xc0000100,
	mmCGTT_CPF_CLK_CTRL, 0xffffffff, 0xc0000100,
	mmCGTT_GDS_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_IA_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PA_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_WD_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_RLC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SPI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQ_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQG_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL0, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL1, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL2, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL3, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL4, 0xffffffff, 0x00000100,
	mmCGTT_TCI_CLK_CTRL, 0xffffffff, 0xff000100,
	mmCGTT_TCP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_VGT_CLK_CTRL, 0xffffffff, 0x06000100,
	mmDB_CGTT_CLK_CTRL_0, 0xffffffff, 0x00000100,
	mmTA_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmTCA_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTCC_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTD_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCGTS_CU0_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU0_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU0_TA_SQC_CTRL_REG, 0xffffffff, 0x0f840f87,
	mmCGTS_CU0_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU0_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU1_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU1_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU1_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU1_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU1_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU2_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU2_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU2_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU2_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU2_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU3_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU3_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU3_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU3_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU3_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU4_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU4_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU4_TA_SQC_CTRL_REG, 0xffffffff, 0x0f840f87,
	mmCGTS_CU4_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU4_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU5_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU5_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU5_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU5_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU5_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96e00200,
	mmCP_RB_WPTR_POLL_CNTL, 0xffffffff, 0x00900100,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003c,
};

static const u32 cz_golden_settings_a11[] =
{
	mmCB_HW_CONTROL_3, 0x00000040, 0x00000040,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmGB_GPU_ID, 0x0000000f, 0x00000000,
	mmPA_SC_ENHANCE, 0xffffffff, 0x00000001,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmSQ_RANDOM_WAVE_PRI, 0x001fffff, 0x000006fd,
	mmTA_CNTL_AUX, 0x000f000f, 0x00010000,
	mmTCC_EXE_DISABLE, 0x00000002, 0x00000002,
	mmTCP_ADDR_CONFIG, 0x0000000f, 0x000000f3,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0x00001302
};

static const u32 cz_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmPA_SC_RASTER_CONFIG, 0xffffffff, 0x00000002,
	mmPA_SC_RASTER_CONFIG_1, 0xffffffff, 0x00000000,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x22010001,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00007FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00007FAF
};

static const u32 cz_mgcg_cgcg_init[] =
{
	mmRLC_CGTT_MGCG_OVERRIDE, 0xffffffff, 0xffffffff,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCB_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_BCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CPC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CPF_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_GDS_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_IA_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PA_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_WD_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_RLC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SC_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SPI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQ_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SQG_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL0, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL1, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL2, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL3, 0xffffffff, 0x00000100,
	mmCGTT_SX_CLK_CTRL4, 0xffffffff, 0x00000100,
	mmCGTT_TCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_TCP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_VGT_CLK_CTRL, 0xffffffff, 0x06000100,
	mmDB_CGTT_CLK_CTRL_0, 0xffffffff, 0x00000100,
	mmTA_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmTCA_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTCC_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTD_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCGTS_CU0_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU0_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU0_TA_SQC_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU0_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU0_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU1_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU1_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU1_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU1_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU1_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU2_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU2_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU2_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU2_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU2_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU3_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU3_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU3_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU3_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU3_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU4_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU4_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU4_TA_SQC_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU4_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU4_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU5_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU5_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU5_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU5_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU5_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU6_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU6_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU6_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU6_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU6_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_CU7_SP0_CTRL_REG, 0xffffffff, 0x00010000,
	mmCGTS_CU7_LDS_SQ_CTRL_REG, 0xffffffff, 0x00030002,
	mmCGTS_CU7_TA_CTRL_REG, 0xffffffff, 0x00040007,
	mmCGTS_CU7_SP1_CTRL_REG, 0xffffffff, 0x00060005,
	mmCGTS_CU7_TD_TCP_CTRL_REG, 0xffffffff, 0x00090008,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96e00200,
	mmCP_RB_WPTR_POLL_CNTL, 0xffffffff, 0x00900100,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003f,
	mmCP_MEM_SLP_CNTL, 0x00000001, 0x00000001,
};

static const u32 stoney_golden_settings_a11[] =
{
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmGB_GPU_ID, 0x0000000f, 0x00000000,
	mmPA_SC_ENHANCE, 0xffffffff, 0x20000001,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmRLC_CGCG_CGLS_CTRL, 0x00000003, 0x0001003c,
	mmTA_CNTL_AUX, 0x000f000f, 0x000b0000,
	mmTCC_CTRL, 0x00100000, 0xf31fff7f,
	mmTCC_EXE_DISABLE, 0x00000002, 0x00000002,
	mmTCP_ADDR_CONFIG, 0x0000000f, 0x000000f1,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0x10101010,
};

static const u32 stoney_golden_common_all[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmPA_SC_RASTER_CONFIG, 0xffffffff, 0x00000000,
	mmPA_SC_RASTER_CONFIG_1, 0xffffffff, 0x00000000,
	mmGB_ADDR_CONFIG, 0xffffffff, 0x12010001,
	mmSPI_RESOURCE_RESERVE_CU_0, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_CU_1, 0xffffffff, 0x00000800,
	mmSPI_RESOURCE_RESERVE_EN_CU_0, 0xffffffff, 0x00007FBF,
	mmSPI_RESOURCE_RESERVE_EN_CU_1, 0xffffffff, 0x00007FAF,
};

static const u32 stoney_mgcg_cgcg_init[] =
{
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003f,
	mmCP_MEM_SLP_CNTL, 0xffffffff, 0x00020201,
	mmRLC_MEM_SLP_CNTL, 0xffffffff, 0x00020201,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96940200,
	mmATC_MISC_CG, 0xffffffff, 0x000c0200,
};

static void gfx_v8_0_set_ring_funcs(struct amdgpu_device *adev);
static void gfx_v8_0_set_irq_funcs(struct amdgpu_device *adev);
static void gfx_v8_0_set_gds_init(struct amdgpu_device *adev);
static void gfx_v8_0_set_rlc_funcs(struct amdgpu_device *adev);
static u32 gfx_v8_0_get_csb_size(struct amdgpu_device *adev);
static void gfx_v8_0_get_cu_info(struct amdgpu_device *adev);

static void gfx_v8_0_init_golden_registers(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		amdgpu_program_register_sequence(adev,
						 iceland_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(iceland_mgcg_cgcg_init));
		amdgpu_program_register_sequence(adev,
						 golden_settings_iceland_a11,
						 (const u32)ARRAY_SIZE(golden_settings_iceland_a11));
		amdgpu_program_register_sequence(adev,
						 iceland_golden_common_all,
						 (const u32)ARRAY_SIZE(iceland_golden_common_all));
		break;
	case CHIP_FIJI:
		amdgpu_program_register_sequence(adev,
						 fiji_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(fiji_mgcg_cgcg_init));
		amdgpu_program_register_sequence(adev,
						 golden_settings_fiji_a10,
						 (const u32)ARRAY_SIZE(golden_settings_fiji_a10));
		amdgpu_program_register_sequence(adev,
						 fiji_golden_common_all,
						 (const u32)ARRAY_SIZE(fiji_golden_common_all));
		break;

	case CHIP_TONGA:
		amdgpu_program_register_sequence(adev,
						 tonga_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(tonga_mgcg_cgcg_init));
		amdgpu_program_register_sequence(adev,
						 golden_settings_tonga_a11,
						 (const u32)ARRAY_SIZE(golden_settings_tonga_a11));
		amdgpu_program_register_sequence(adev,
						 tonga_golden_common_all,
						 (const u32)ARRAY_SIZE(tonga_golden_common_all));
		break;
	case CHIP_POLARIS11:
		amdgpu_program_register_sequence(adev,
						 golden_settings_polaris11_a11,
						 (const u32)ARRAY_SIZE(golden_settings_polaris11_a11));
		amdgpu_program_register_sequence(adev,
						 polaris11_golden_common_all,
						 (const u32)ARRAY_SIZE(polaris11_golden_common_all));
		break;
	case CHIP_POLARIS10:
		amdgpu_program_register_sequence(adev,
						 golden_settings_polaris10_a11,
						 (const u32)ARRAY_SIZE(golden_settings_polaris10_a11));
		amdgpu_program_register_sequence(adev,
						 polaris10_golden_common_all,
						 (const u32)ARRAY_SIZE(polaris10_golden_common_all));
		break;
	case CHIP_CARRIZO:
		amdgpu_program_register_sequence(adev,
						 cz_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(cz_mgcg_cgcg_init));
		amdgpu_program_register_sequence(adev,
						 cz_golden_settings_a11,
						 (const u32)ARRAY_SIZE(cz_golden_settings_a11));
		amdgpu_program_register_sequence(adev,
						 cz_golden_common_all,
						 (const u32)ARRAY_SIZE(cz_golden_common_all));
		break;
	case CHIP_STONEY:
		amdgpu_program_register_sequence(adev,
						 stoney_mgcg_cgcg_init,
						 (const u32)ARRAY_SIZE(stoney_mgcg_cgcg_init));
		amdgpu_program_register_sequence(adev,
						 stoney_golden_settings_a11,
						 (const u32)ARRAY_SIZE(stoney_golden_settings_a11));
		amdgpu_program_register_sequence(adev,
						 stoney_golden_common_all,
						 (const u32)ARRAY_SIZE(stoney_golden_common_all));
		break;
	default:
		break;
	}
}

static void gfx_v8_0_scratch_init(struct amdgpu_device *adev)
{
	int i;

	adev->gfx.scratch.num_reg = 7;
	adev->gfx.scratch.reg_base = mmSCRATCH_REG0;
	for (i = 0; i < adev->gfx.scratch.num_reg; i++) {
		adev->gfx.scratch.free[i] = true;
		adev->gfx.scratch.reg[i] = adev->gfx.scratch.reg_base + i;
	}
}

static int gfx_v8_0_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t scratch;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	r = amdgpu_gfx_scratch_get(adev, &scratch);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to get scratch reg (%d).\n", r);
		return r;
	}
	WREG32(scratch, 0xCAFEDEAD);
	r = amdgpu_ring_alloc(ring, 3);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to lock ring %d (%d).\n",
			  ring->idx, r);
		amdgpu_gfx_scratch_free(adev, scratch);
		return r;
	}
	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_UCONFIG_REG, 1));
	amdgpu_ring_write(ring, (scratch - PACKET3_SET_UCONFIG_REG_START));
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(scratch);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}
	if (i < adev->usec_timeout) {
		DRM_INFO("ring test on %d succeeded in %d usecs\n",
			 ring->idx, i);
	} else {
		DRM_ERROR("amdgpu: ring %d test failed (scratch(0x%04X)=0x%08X)\n",
			  ring->idx, scratch, tmp);
		r = -EINVAL;
	}
	amdgpu_gfx_scratch_free(adev, scratch);
	return r;
}

static int gfx_v8_0_ring_test_ib(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct fence *f = NULL;
	uint32_t scratch;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	r = amdgpu_gfx_scratch_get(adev, &scratch);
	if (r) {
		DRM_ERROR("amdgpu: failed to get scratch reg (%d).\n", r);
		return r;
	}
	WREG32(scratch, 0xCAFEDEAD);
	memset(&ib, 0, sizeof(ib));
	r = amdgpu_ib_get(adev, NULL, 256, &ib);
	if (r) {
		DRM_ERROR("amdgpu: failed to get ib (%d).\n", r);
		goto err1;
	}
	ib.ptr[0] = PACKET3(PACKET3_SET_UCONFIG_REG, 1);
	ib.ptr[1] = ((scratch - PACKET3_SET_UCONFIG_REG_START));
	ib.ptr[2] = 0xDEADBEEF;
	ib.length_dw = 3;

	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, NULL, &f);
	if (r)
		goto err2;

	r = fence_wait(f, false);
	if (r) {
		DRM_ERROR("amdgpu: fence wait failed (%d).\n", r);
		goto err2;
	}
	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(scratch);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}
	if (i < adev->usec_timeout) {
		DRM_INFO("ib test on ring %d succeeded in %u usecs\n",
			 ring->idx, i);
		goto err2;
	} else {
		DRM_ERROR("amdgpu: ib test failed (scratch(0x%04X)=0x%08X)\n",
			  scratch, tmp);
		r = -EINVAL;
	}
err2:
	fence_put(f);
	amdgpu_ib_free(adev, &ib, NULL);
	fence_put(f);
err1:
	amdgpu_gfx_scratch_free(adev, scratch);
	return r;
}


static void gfx_v8_0_free_microcode(struct amdgpu_device *adev) {
	release_firmware(adev->gfx.pfp_fw);
	adev->gfx.pfp_fw = NULL;
	release_firmware(adev->gfx.me_fw);
	adev->gfx.me_fw = NULL;
	release_firmware(adev->gfx.ce_fw);
	adev->gfx.ce_fw = NULL;
	release_firmware(adev->gfx.rlc_fw);
	adev->gfx.rlc_fw = NULL;
	release_firmware(adev->gfx.mec_fw);
	adev->gfx.mec_fw = NULL;
	if ((adev->asic_type != CHIP_STONEY) &&
	    (adev->asic_type != CHIP_TOPAZ))
		release_firmware(adev->gfx.mec2_fw);
	adev->gfx.mec2_fw = NULL;

	kfree(adev->gfx.rlc.register_list_format);
}

static int gfx_v8_0_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[30];
	int err;
	struct amdgpu_firmware_info *info = NULL;
	const struct common_firmware_header *header = NULL;
	const struct gfx_firmware_header_v1_0 *cp_hdr;
	const struct rlc_firmware_header_v2_0 *rlc_hdr;
	unsigned int *tmp = NULL, i;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		chip_name = "topaz";
		break;
	case CHIP_TONGA:
		chip_name = "tonga";
		break;
	case CHIP_CARRIZO:
		chip_name = "carrizo";
		break;
	case CHIP_FIJI:
		chip_name = "fiji";
		break;
	case CHIP_POLARIS11:
		chip_name = "polaris11";
		break;
	case CHIP_POLARIS10:
		chip_name = "polaris10";
		break;
	case CHIP_STONEY:
		chip_name = "stoney";
		break;
	default:
		BUG();
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_pfp.bin", chip_name);
	err = request_firmware(&adev->gfx.pfp_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.pfp_fw);
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.pfp_fw->data;
	adev->gfx.pfp_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.pfp_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_me.bin", chip_name);
	err = request_firmware(&adev->gfx.me_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.me_fw);
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.me_fw->data;
	adev->gfx.me_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.me_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_ce.bin", chip_name);
	err = request_firmware(&adev->gfx.ce_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.ce_fw);
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.ce_fw->data;
	adev->gfx.ce_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.ce_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_rlc.bin", chip_name);
	err = request_firmware(&adev->gfx.rlc_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.rlc_fw);
	rlc_hdr = (const struct rlc_firmware_header_v2_0 *)adev->gfx.rlc_fw->data;
	adev->gfx.rlc_fw_version = le32_to_cpu(rlc_hdr->header.ucode_version);
	adev->gfx.rlc_feature_version = le32_to_cpu(rlc_hdr->ucode_feature_version);

	adev->gfx.rlc.save_and_restore_offset =
			le32_to_cpu(rlc_hdr->save_and_restore_offset);
	adev->gfx.rlc.clear_state_descriptor_offset =
			le32_to_cpu(rlc_hdr->clear_state_descriptor_offset);
	adev->gfx.rlc.avail_scratch_ram_locations =
			le32_to_cpu(rlc_hdr->avail_scratch_ram_locations);
	adev->gfx.rlc.reg_restore_list_size =
			le32_to_cpu(rlc_hdr->reg_restore_list_size);
	adev->gfx.rlc.reg_list_format_start =
			le32_to_cpu(rlc_hdr->reg_list_format_start);
	adev->gfx.rlc.reg_list_format_separate_start =
			le32_to_cpu(rlc_hdr->reg_list_format_separate_start);
	adev->gfx.rlc.starting_offsets_start =
			le32_to_cpu(rlc_hdr->starting_offsets_start);
	adev->gfx.rlc.reg_list_format_size_bytes =
			le32_to_cpu(rlc_hdr->reg_list_format_size_bytes);
	adev->gfx.rlc.reg_list_size_bytes =
			le32_to_cpu(rlc_hdr->reg_list_size_bytes);

	adev->gfx.rlc.register_list_format =
			kmalloc(adev->gfx.rlc.reg_list_format_size_bytes +
					adev->gfx.rlc.reg_list_size_bytes, GFP_KERNEL);

	if (!adev->gfx.rlc.register_list_format) {
		err = -ENOMEM;
		goto out;
	}

	tmp = (unsigned int *)((uintptr_t)rlc_hdr +
			le32_to_cpu(rlc_hdr->reg_list_format_array_offset_bytes));
	for (i = 0 ; i < (rlc_hdr->reg_list_format_size_bytes >> 2); i++)
		adev->gfx.rlc.register_list_format[i] =	le32_to_cpu(tmp[i]);

	adev->gfx.rlc.register_restore = adev->gfx.rlc.register_list_format + i;

	tmp = (unsigned int *)((uintptr_t)rlc_hdr +
			le32_to_cpu(rlc_hdr->reg_list_array_offset_bytes));
	for (i = 0 ; i < (rlc_hdr->reg_list_size_bytes >> 2); i++)
		adev->gfx.rlc.register_restore[i] = le32_to_cpu(tmp[i]);

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mec.bin", chip_name);
	err = request_firmware(&adev->gfx.mec_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.mec_fw);
	if (err)
		goto out;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.mec_fw->data;
	adev->gfx.mec_fw_version = le32_to_cpu(cp_hdr->header.ucode_version);
	adev->gfx.mec_feature_version = le32_to_cpu(cp_hdr->ucode_feature_version);

	if ((adev->asic_type != CHIP_STONEY) &&
	    (adev->asic_type != CHIP_TOPAZ)) {
		snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mec2.bin", chip_name);
		err = request_firmware(&adev->gfx.mec2_fw, fw_name, adev->dev);
		if (!err) {
			err = amdgpu_ucode_validate(adev->gfx.mec2_fw);
			if (err)
				goto out;
			cp_hdr = (const struct gfx_firmware_header_v1_0 *)
				adev->gfx.mec2_fw->data;
			adev->gfx.mec2_fw_version =
				le32_to_cpu(cp_hdr->header.ucode_version);
			adev->gfx.mec2_feature_version =
				le32_to_cpu(cp_hdr->ucode_feature_version);
		} else {
			err = 0;
			adev->gfx.mec2_fw = NULL;
		}
	}

	if (adev->firmware.smu_load) {
		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_PFP];
		info->ucode_id = AMDGPU_UCODE_ID_CP_PFP;
		info->fw = adev->gfx.pfp_fw;
		header = (const struct common_firmware_header *)info->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_ME];
		info->ucode_id = AMDGPU_UCODE_ID_CP_ME;
		info->fw = adev->gfx.me_fw;
		header = (const struct common_firmware_header *)info->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_CE];
		info->ucode_id = AMDGPU_UCODE_ID_CP_CE;
		info->fw = adev->gfx.ce_fw;
		header = (const struct common_firmware_header *)info->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_G];
		info->ucode_id = AMDGPU_UCODE_ID_RLC_G;
		info->fw = adev->gfx.rlc_fw;
		header = (const struct common_firmware_header *)info->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_MEC1];
		info->ucode_id = AMDGPU_UCODE_ID_CP_MEC1;
		info->fw = adev->gfx.mec_fw;
		header = (const struct common_firmware_header *)info->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);

		if (adev->gfx.mec2_fw) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_MEC2];
			info->ucode_id = AMDGPU_UCODE_ID_CP_MEC2;
			info->fw = adev->gfx.mec2_fw;
			header = (const struct common_firmware_header *)info->fw->data;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);
		}

	}

out:
	if (err) {
		dev_err(adev->dev,
			"gfx8: Failed to load firmware \"%s\"\n",
			fw_name);
		release_firmware(adev->gfx.pfp_fw);
		adev->gfx.pfp_fw = NULL;
		release_firmware(adev->gfx.me_fw);
		adev->gfx.me_fw = NULL;
		release_firmware(adev->gfx.ce_fw);
		adev->gfx.ce_fw = NULL;
		release_firmware(adev->gfx.rlc_fw);
		adev->gfx.rlc_fw = NULL;
		release_firmware(adev->gfx.mec_fw);
		adev->gfx.mec_fw = NULL;
		release_firmware(adev->gfx.mec2_fw);
		adev->gfx.mec2_fw = NULL;
	}
	return err;
}

static void gfx_v8_0_get_csb_buffer(struct amdgpu_device *adev,
				    volatile u32 *buffer)
{
	u32 count = 0, i;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	if (adev->gfx.rlc.cs_data == NULL)
		return;
	if (buffer == NULL)
		return;

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	buffer[count++] = cpu_to_le32(PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	buffer[count++] = cpu_to_le32(0x80000000);
	buffer[count++] = cpu_to_le32(0x80000000);

	for (sect = adev->gfx.rlc.cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT) {
				buffer[count++] =
					cpu_to_le32(PACKET3(PACKET3_SET_CONTEXT_REG, ext->reg_count));
				buffer[count++] = cpu_to_le32(ext->reg_index -
						PACKET3_SET_CONTEXT_REG_START);
				for (i = 0; i < ext->reg_count; i++)
					buffer[count++] = cpu_to_le32(ext->extent[i]);
			} else {
				return;
			}
		}
	}

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	buffer[count++] = cpu_to_le32(mmPA_SC_RASTER_CONFIG -
			PACKET3_SET_CONTEXT_REG_START);
	switch (adev->asic_type) {
	case CHIP_TONGA:
	case CHIP_POLARIS10:
		buffer[count++] = cpu_to_le32(0x16000012);
		buffer[count++] = cpu_to_le32(0x0000002A);
		break;
	case CHIP_POLARIS11:
		buffer[count++] = cpu_to_le32(0x16000012);
		buffer[count++] = cpu_to_le32(0x00000000);
		break;
	case CHIP_FIJI:
		buffer[count++] = cpu_to_le32(0x3a00161a);
		buffer[count++] = cpu_to_le32(0x0000002e);
		break;
	case CHIP_TOPAZ:
	case CHIP_CARRIZO:
		buffer[count++] = cpu_to_le32(0x00000002);
		buffer[count++] = cpu_to_le32(0x00000000);
		break;
	case CHIP_STONEY:
		buffer[count++] = cpu_to_le32(0x00000000);
		buffer[count++] = cpu_to_le32(0x00000000);
		break;
	default:
		buffer[count++] = cpu_to_le32(0x00000000);
		buffer[count++] = cpu_to_le32(0x00000000);
		break;
	}

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	buffer[count++] = cpu_to_le32(PACKET3_PREAMBLE_END_CLEAR_STATE);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_CLEAR_STATE, 0));
	buffer[count++] = cpu_to_le32(0);
}

static void gfx_v8_0_rlc_fini(struct amdgpu_device *adev)
{
	int r;

	/* clear state block */
	if (adev->gfx.rlc.clear_state_obj) {
		r = amdgpu_bo_reserve(adev->gfx.rlc.clear_state_obj, false);
		if (unlikely(r != 0))
			dev_warn(adev->dev, "(%d) reserve RLC c bo failed\n", r);
		amdgpu_bo_unpin(adev->gfx.rlc.clear_state_obj);
		amdgpu_bo_unreserve(adev->gfx.rlc.clear_state_obj);

		amdgpu_bo_unref(&adev->gfx.rlc.clear_state_obj);
		adev->gfx.rlc.clear_state_obj = NULL;
	}
}

static int gfx_v8_0_rlc_init(struct amdgpu_device *adev)
{
	volatile u32 *dst_ptr;
	u32 dws;
	const struct cs_section_def *cs_data;
	int r;

	adev->gfx.rlc.cs_data = vi_cs_data;

	cs_data = adev->gfx.rlc.cs_data;

	if (cs_data) {
		/* clear state block */
		adev->gfx.rlc.clear_state_size = dws = gfx_v8_0_get_csb_size(adev);

		if (adev->gfx.rlc.clear_state_obj == NULL) {
			r = amdgpu_bo_create(adev, dws * 4, PAGE_SIZE, true,
					     AMDGPU_GEM_DOMAIN_VRAM,
					     AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED,
					     NULL, NULL,
					     &adev->gfx.rlc.clear_state_obj);
			if (r) {
				dev_warn(adev->dev, "(%d) create RLC c bo failed\n", r);
				gfx_v8_0_rlc_fini(adev);
				return r;
			}
		}
		r = amdgpu_bo_reserve(adev->gfx.rlc.clear_state_obj, false);
		if (unlikely(r != 0)) {
			gfx_v8_0_rlc_fini(adev);
			return r;
		}
		r = amdgpu_bo_pin(adev->gfx.rlc.clear_state_obj, AMDGPU_GEM_DOMAIN_VRAM,
				  &adev->gfx.rlc.clear_state_gpu_addr);
		if (r) {
			amdgpu_bo_unreserve(adev->gfx.rlc.clear_state_obj);
			dev_warn(adev->dev, "(%d) pin RLC c bo failed\n", r);
			gfx_v8_0_rlc_fini(adev);
			return r;
		}

		r = amdgpu_bo_kmap(adev->gfx.rlc.clear_state_obj, (void **)&adev->gfx.rlc.cs_ptr);
		if (r) {
			dev_warn(adev->dev, "(%d) map RLC c bo failed\n", r);
			gfx_v8_0_rlc_fini(adev);
			return r;
		}
		/* set up the cs buffer */
		dst_ptr = adev->gfx.rlc.cs_ptr;
		gfx_v8_0_get_csb_buffer(adev, dst_ptr);
		amdgpu_bo_kunmap(adev->gfx.rlc.clear_state_obj);
		amdgpu_bo_unreserve(adev->gfx.rlc.clear_state_obj);
	}

	return 0;
}

static void gfx_v8_0_mec_fini(struct amdgpu_device *adev)
{
	int r;

	if (adev->gfx.mec.hpd_eop_obj) {
		r = amdgpu_bo_reserve(adev->gfx.mec.hpd_eop_obj, false);
		if (unlikely(r != 0))
			dev_warn(adev->dev, "(%d) reserve HPD EOP bo failed\n", r);
		amdgpu_bo_unpin(adev->gfx.mec.hpd_eop_obj);
		amdgpu_bo_unreserve(adev->gfx.mec.hpd_eop_obj);

		amdgpu_bo_unref(&adev->gfx.mec.hpd_eop_obj);
		adev->gfx.mec.hpd_eop_obj = NULL;
	}
}

#define MEC_HPD_SIZE 2048

static int gfx_v8_0_mec_init(struct amdgpu_device *adev)
{
	int r;
	u32 *hpd;

	/*
	 * we assign only 1 pipe because all other pipes will
	 * be handled by KFD
	 */
	adev->gfx.mec.num_mec = 1;
	adev->gfx.mec.num_pipe = 1;
	adev->gfx.mec.num_queue = adev->gfx.mec.num_mec * adev->gfx.mec.num_pipe * 8;

	if (adev->gfx.mec.hpd_eop_obj == NULL) {
		r = amdgpu_bo_create(adev,
				     adev->gfx.mec.num_mec *adev->gfx.mec.num_pipe * MEC_HPD_SIZE * 2,
				     PAGE_SIZE, true,
				     AMDGPU_GEM_DOMAIN_GTT, 0, NULL, NULL,
				     &adev->gfx.mec.hpd_eop_obj);
		if (r) {
			dev_warn(adev->dev, "(%d) create HDP EOP bo failed\n", r);
			return r;
		}
	}

	r = amdgpu_bo_reserve(adev->gfx.mec.hpd_eop_obj, false);
	if (unlikely(r != 0)) {
		gfx_v8_0_mec_fini(adev);
		return r;
	}
	r = amdgpu_bo_pin(adev->gfx.mec.hpd_eop_obj, AMDGPU_GEM_DOMAIN_GTT,
			  &adev->gfx.mec.hpd_eop_gpu_addr);
	if (r) {
		dev_warn(adev->dev, "(%d) pin HDP EOP bo failed\n", r);
		gfx_v8_0_mec_fini(adev);
		return r;
	}
	r = amdgpu_bo_kmap(adev->gfx.mec.hpd_eop_obj, (void **)&hpd);
	if (r) {
		dev_warn(adev->dev, "(%d) map HDP EOP bo failed\n", r);
		gfx_v8_0_mec_fini(adev);
		return r;
	}

	memset(hpd, 0, adev->gfx.mec.num_mec *adev->gfx.mec.num_pipe * MEC_HPD_SIZE * 2);

	amdgpu_bo_kunmap(adev->gfx.mec.hpd_eop_obj);
	amdgpu_bo_unreserve(adev->gfx.mec.hpd_eop_obj);

	return 0;
}

static const u32 vgpr_init_compute_shader[] =
{
	0x7e000209, 0x7e020208,
	0x7e040207, 0x7e060206,
	0x7e080205, 0x7e0a0204,
	0x7e0c0203, 0x7e0e0202,
	0x7e100201, 0x7e120200,
	0x7e140209, 0x7e160208,
	0x7e180207, 0x7e1a0206,
	0x7e1c0205, 0x7e1e0204,
	0x7e200203, 0x7e220202,
	0x7e240201, 0x7e260200,
	0x7e280209, 0x7e2a0208,
	0x7e2c0207, 0x7e2e0206,
	0x7e300205, 0x7e320204,
	0x7e340203, 0x7e360202,
	0x7e380201, 0x7e3a0200,
	0x7e3c0209, 0x7e3e0208,
	0x7e400207, 0x7e420206,
	0x7e440205, 0x7e460204,
	0x7e480203, 0x7e4a0202,
	0x7e4c0201, 0x7e4e0200,
	0x7e500209, 0x7e520208,
	0x7e540207, 0x7e560206,
	0x7e580205, 0x7e5a0204,
	0x7e5c0203, 0x7e5e0202,
	0x7e600201, 0x7e620200,
	0x7e640209, 0x7e660208,
	0x7e680207, 0x7e6a0206,
	0x7e6c0205, 0x7e6e0204,
	0x7e700203, 0x7e720202,
	0x7e740201, 0x7e760200,
	0x7e780209, 0x7e7a0208,
	0x7e7c0207, 0x7e7e0206,
	0xbf8a0000, 0xbf810000,
};

static const u32 sgpr_init_compute_shader[] =
{
	0xbe8a0100, 0xbe8c0102,
	0xbe8e0104, 0xbe900106,
	0xbe920108, 0xbe940100,
	0xbe960102, 0xbe980104,
	0xbe9a0106, 0xbe9c0108,
	0xbe9e0100, 0xbea00102,
	0xbea20104, 0xbea40106,
	0xbea60108, 0xbea80100,
	0xbeaa0102, 0xbeac0104,
	0xbeae0106, 0xbeb00108,
	0xbeb20100, 0xbeb40102,
	0xbeb60104, 0xbeb80106,
	0xbeba0108, 0xbebc0100,
	0xbebe0102, 0xbec00104,
	0xbec20106, 0xbec40108,
	0xbec60100, 0xbec80102,
	0xbee60004, 0xbee70005,
	0xbeea0006, 0xbeeb0007,
	0xbee80008, 0xbee90009,
	0xbefc0000, 0xbf8a0000,
	0xbf810000, 0x00000000,
};

static const u32 vgpr_init_regs[] =
{
	mmCOMPUTE_STATIC_THREAD_MGMT_SE0, 0xffffffff,
	mmCOMPUTE_RESOURCE_LIMITS, 0,
	mmCOMPUTE_NUM_THREAD_X, 256*4,
	mmCOMPUTE_NUM_THREAD_Y, 1,
	mmCOMPUTE_NUM_THREAD_Z, 1,
	mmCOMPUTE_PGM_RSRC2, 20,
	mmCOMPUTE_USER_DATA_0, 0xedcedc00,
	mmCOMPUTE_USER_DATA_1, 0xedcedc01,
	mmCOMPUTE_USER_DATA_2, 0xedcedc02,
	mmCOMPUTE_USER_DATA_3, 0xedcedc03,
	mmCOMPUTE_USER_DATA_4, 0xedcedc04,
	mmCOMPUTE_USER_DATA_5, 0xedcedc05,
	mmCOMPUTE_USER_DATA_6, 0xedcedc06,
	mmCOMPUTE_USER_DATA_7, 0xedcedc07,
	mmCOMPUTE_USER_DATA_8, 0xedcedc08,
	mmCOMPUTE_USER_DATA_9, 0xedcedc09,
};

static const u32 sgpr1_init_regs[] =
{
	mmCOMPUTE_STATIC_THREAD_MGMT_SE0, 0x0f,
	mmCOMPUTE_RESOURCE_LIMITS, 0x1000000,
	mmCOMPUTE_NUM_THREAD_X, 256*5,
	mmCOMPUTE_NUM_THREAD_Y, 1,
	mmCOMPUTE_NUM_THREAD_Z, 1,
	mmCOMPUTE_PGM_RSRC2, 20,
	mmCOMPUTE_USER_DATA_0, 0xedcedc00,
	mmCOMPUTE_USER_DATA_1, 0xedcedc01,
	mmCOMPUTE_USER_DATA_2, 0xedcedc02,
	mmCOMPUTE_USER_DATA_3, 0xedcedc03,
	mmCOMPUTE_USER_DATA_4, 0xedcedc04,
	mmCOMPUTE_USER_DATA_5, 0xedcedc05,
	mmCOMPUTE_USER_DATA_6, 0xedcedc06,
	mmCOMPUTE_USER_DATA_7, 0xedcedc07,
	mmCOMPUTE_USER_DATA_8, 0xedcedc08,
	mmCOMPUTE_USER_DATA_9, 0xedcedc09,
};

static const u32 sgpr2_init_regs[] =
{
	mmCOMPUTE_STATIC_THREAD_MGMT_SE0, 0xf0,
	mmCOMPUTE_RESOURCE_LIMITS, 0x1000000,
	mmCOMPUTE_NUM_THREAD_X, 256*5,
	mmCOMPUTE_NUM_THREAD_Y, 1,
	mmCOMPUTE_NUM_THREAD_Z, 1,
	mmCOMPUTE_PGM_RSRC2, 20,
	mmCOMPUTE_USER_DATA_0, 0xedcedc00,
	mmCOMPUTE_USER_DATA_1, 0xedcedc01,
	mmCOMPUTE_USER_DATA_2, 0xedcedc02,
	mmCOMPUTE_USER_DATA_3, 0xedcedc03,
	mmCOMPUTE_USER_DATA_4, 0xedcedc04,
	mmCOMPUTE_USER_DATA_5, 0xedcedc05,
	mmCOMPUTE_USER_DATA_6, 0xedcedc06,
	mmCOMPUTE_USER_DATA_7, 0xedcedc07,
	mmCOMPUTE_USER_DATA_8, 0xedcedc08,
	mmCOMPUTE_USER_DATA_9, 0xedcedc09,
};

static const u32 sec_ded_counter_registers[] =
{
	mmCPC_EDC_ATC_CNT,
	mmCPC_EDC_SCRATCH_CNT,
	mmCPC_EDC_UCODE_CNT,
	mmCPF_EDC_ATC_CNT,
	mmCPF_EDC_ROQ_CNT,
	mmCPF_EDC_TAG_CNT,
	mmCPG_EDC_ATC_CNT,
	mmCPG_EDC_DMA_CNT,
	mmCPG_EDC_TAG_CNT,
	mmDC_EDC_CSINVOC_CNT,
	mmDC_EDC_RESTORE_CNT,
	mmDC_EDC_STATE_CNT,
	mmGDS_EDC_CNT,
	mmGDS_EDC_GRBM_CNT,
	mmGDS_EDC_OA_DED,
	mmSPI_EDC_CNT,
	mmSQC_ATC_EDC_GATCL1_CNT,
	mmSQC_EDC_CNT,
	mmSQ_EDC_DED_CNT,
	mmSQ_EDC_INFO,
	mmSQ_EDC_SEC_CNT,
	mmTCC_EDC_CNT,
	mmTCP_ATC_EDC_GATCL1_CNT,
	mmTCP_EDC_CNT,
	mmTD_EDC_CNT
};

static int gfx_v8_0_do_edc_gpr_workarounds(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->gfx.compute_ring[0];
	struct amdgpu_ib ib;
	struct fence *f = NULL;
	int r, i;
	u32 tmp;
	unsigned total_size, vgpr_offset, sgpr_offset;
	u64 gpu_addr;

	/* only supported on CZ */
	if (adev->asic_type != CHIP_CARRIZO)
		return 0;

	/* bail if the compute ring is not ready */
	if (!ring->ready)
		return 0;

	tmp = RREG32(mmGB_EDC_MODE);
	WREG32(mmGB_EDC_MODE, 0);

	total_size =
		(((ARRAY_SIZE(vgpr_init_regs) / 2) * 3) + 4 + 5 + 2) * 4;
	total_size +=
		(((ARRAY_SIZE(sgpr1_init_regs) / 2) * 3) + 4 + 5 + 2) * 4;
	total_size +=
		(((ARRAY_SIZE(sgpr2_init_regs) / 2) * 3) + 4 + 5 + 2) * 4;
	total_size = ALIGN(total_size, 256);
	vgpr_offset = total_size;
	total_size += ALIGN(sizeof(vgpr_init_compute_shader), 256);
	sgpr_offset = total_size;
	total_size += sizeof(sgpr_init_compute_shader);

	/* allocate an indirect buffer to put the commands in */
	memset(&ib, 0, sizeof(ib));
	r = amdgpu_ib_get(adev, NULL, total_size, &ib);
	if (r) {
		DRM_ERROR("amdgpu: failed to get ib (%d).\n", r);
		return r;
	}

	/* load the compute shaders */
	for (i = 0; i < ARRAY_SIZE(vgpr_init_compute_shader); i++)
		ib.ptr[i + (vgpr_offset / 4)] = vgpr_init_compute_shader[i];

	for (i = 0; i < ARRAY_SIZE(sgpr_init_compute_shader); i++)
		ib.ptr[i + (sgpr_offset / 4)] = sgpr_init_compute_shader[i];

	/* init the ib length to 0 */
	ib.length_dw = 0;

	/* VGPR */
	/* write the register state for the compute dispatch */
	for (i = 0; i < ARRAY_SIZE(vgpr_init_regs); i += 2) {
		ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 1);
		ib.ptr[ib.length_dw++] = vgpr_init_regs[i] - PACKET3_SET_SH_REG_START;
		ib.ptr[ib.length_dw++] = vgpr_init_regs[i + 1];
	}
	/* write the shader start address: mmCOMPUTE_PGM_LO, mmCOMPUTE_PGM_HI */
	gpu_addr = (ib.gpu_addr + (u64)vgpr_offset) >> 8;
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 2);
	ib.ptr[ib.length_dw++] = mmCOMPUTE_PGM_LO - PACKET3_SET_SH_REG_START;
	ib.ptr[ib.length_dw++] = lower_32_bits(gpu_addr);
	ib.ptr[ib.length_dw++] = upper_32_bits(gpu_addr);

	/* write dispatch packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_DISPATCH_DIRECT, 3);
	ib.ptr[ib.length_dw++] = 8; /* x */
	ib.ptr[ib.length_dw++] = 1; /* y */
	ib.ptr[ib.length_dw++] = 1; /* z */
	ib.ptr[ib.length_dw++] =
		REG_SET_FIELD(0, COMPUTE_DISPATCH_INITIATOR, COMPUTE_SHADER_EN, 1);

	/* write CS partial flush packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_EVENT_WRITE, 0);
	ib.ptr[ib.length_dw++] = EVENT_TYPE(7) | EVENT_INDEX(4);

	/* SGPR1 */
	/* write the register state for the compute dispatch */
	for (i = 0; i < ARRAY_SIZE(sgpr1_init_regs); i += 2) {
		ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 1);
		ib.ptr[ib.length_dw++] = sgpr1_init_regs[i] - PACKET3_SET_SH_REG_START;
		ib.ptr[ib.length_dw++] = sgpr1_init_regs[i + 1];
	}
	/* write the shader start address: mmCOMPUTE_PGM_LO, mmCOMPUTE_PGM_HI */
	gpu_addr = (ib.gpu_addr + (u64)sgpr_offset) >> 8;
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 2);
	ib.ptr[ib.length_dw++] = mmCOMPUTE_PGM_LO - PACKET3_SET_SH_REG_START;
	ib.ptr[ib.length_dw++] = lower_32_bits(gpu_addr);
	ib.ptr[ib.length_dw++] = upper_32_bits(gpu_addr);

	/* write dispatch packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_DISPATCH_DIRECT, 3);
	ib.ptr[ib.length_dw++] = 8; /* x */
	ib.ptr[ib.length_dw++] = 1; /* y */
	ib.ptr[ib.length_dw++] = 1; /* z */
	ib.ptr[ib.length_dw++] =
		REG_SET_FIELD(0, COMPUTE_DISPATCH_INITIATOR, COMPUTE_SHADER_EN, 1);

	/* write CS partial flush packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_EVENT_WRITE, 0);
	ib.ptr[ib.length_dw++] = EVENT_TYPE(7) | EVENT_INDEX(4);

	/* SGPR2 */
	/* write the register state for the compute dispatch */
	for (i = 0; i < ARRAY_SIZE(sgpr2_init_regs); i += 2) {
		ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 1);
		ib.ptr[ib.length_dw++] = sgpr2_init_regs[i] - PACKET3_SET_SH_REG_START;
		ib.ptr[ib.length_dw++] = sgpr2_init_regs[i + 1];
	}
	/* write the shader start address: mmCOMPUTE_PGM_LO, mmCOMPUTE_PGM_HI */
	gpu_addr = (ib.gpu_addr + (u64)sgpr_offset) >> 8;
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 2);
	ib.ptr[ib.length_dw++] = mmCOMPUTE_PGM_LO - PACKET3_SET_SH_REG_START;
	ib.ptr[ib.length_dw++] = lower_32_bits(gpu_addr);
	ib.ptr[ib.length_dw++] = upper_32_bits(gpu_addr);

	/* write dispatch packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_DISPATCH_DIRECT, 3);
	ib.ptr[ib.length_dw++] = 8; /* x */
	ib.ptr[ib.length_dw++] = 1; /* y */
	ib.ptr[ib.length_dw++] = 1; /* z */
	ib.ptr[ib.length_dw++] =
		REG_SET_FIELD(0, COMPUTE_DISPATCH_INITIATOR, COMPUTE_SHADER_EN, 1);

	/* write CS partial flush packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_EVENT_WRITE, 0);
	ib.ptr[ib.length_dw++] = EVENT_TYPE(7) | EVENT_INDEX(4);

	/* shedule the ib on the ring */
	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, NULL, &f);
	if (r) {
		DRM_ERROR("amdgpu: ib submit failed (%d).\n", r);
		goto fail;
	}

	/* wait for the GPU to finish processing the IB */
	r = fence_wait(f, false);
	if (r) {
		DRM_ERROR("amdgpu: fence wait failed (%d).\n", r);
		goto fail;
	}

	tmp = REG_SET_FIELD(tmp, GB_EDC_MODE, DED_MODE, 2);
	tmp = REG_SET_FIELD(tmp, GB_EDC_MODE, PROP_FED, 1);
	WREG32(mmGB_EDC_MODE, tmp);

	tmp = RREG32(mmCC_GC_EDC_CONFIG);
	tmp = REG_SET_FIELD(tmp, CC_GC_EDC_CONFIG, DIS_EDC, 0) | 1;
	WREG32(mmCC_GC_EDC_CONFIG, tmp);


	/* read back registers to clear the counters */
	for (i = 0; i < ARRAY_SIZE(sec_ded_counter_registers); i++)
		RREG32(sec_ded_counter_registers[i]);

fail:
	fence_put(f);
	amdgpu_ib_free(adev, &ib, NULL);
	fence_put(f);

	return r;
}

static int gfx_v8_0_gpu_early_init(struct amdgpu_device *adev)
{
	u32 gb_addr_config;
	u32 mc_shared_chmap, mc_arb_ramcfg;
	u32 dimm00_addr_map, dimm01_addr_map, dimm10_addr_map, dimm11_addr_map;
	u32 tmp;
	int ret;

	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		adev->gfx.config.max_shader_engines = 1;
		adev->gfx.config.max_tile_pipes = 2;
		adev->gfx.config.max_cu_per_sh = 6;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 2;
		adev->gfx.config.max_texture_channel_caches = 2;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TOPAZ_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_FIJI:
		adev->gfx.config.max_shader_engines = 4;
		adev->gfx.config.max_tile_pipes = 16;
		adev->gfx.config.max_cu_per_sh = 16;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 4;
		adev->gfx.config.max_texture_channel_caches = 16;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TONGA_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_POLARIS11:
		ret = amdgpu_atombios_get_gfx_info(adev);
		if (ret)
			return ret;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = POLARIS11_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_POLARIS10:
		ret = amdgpu_atombios_get_gfx_info(adev);
		if (ret)
			return ret;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TONGA_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_TONGA:
		adev->gfx.config.max_shader_engines = 4;
		adev->gfx.config.max_tile_pipes = 8;
		adev->gfx.config.max_cu_per_sh = 8;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 2;
		adev->gfx.config.max_texture_channel_caches = 8;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TONGA_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_CARRIZO:
		adev->gfx.config.max_shader_engines = 1;
		adev->gfx.config.max_tile_pipes = 2;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 2;

		switch (adev->pdev->revision) {
		case 0xc4:
		case 0x84:
		case 0xc8:
		case 0xcc:
		case 0xe1:
		case 0xe3:
			/* B10 */
			adev->gfx.config.max_cu_per_sh = 8;
			break;
		case 0xc5:
		case 0x81:
		case 0x85:
		case 0xc9:
		case 0xcd:
		case 0xe2:
		case 0xe4:
			/* B8 */
			adev->gfx.config.max_cu_per_sh = 6;
			break;
		case 0xc6:
		case 0xca:
		case 0xce:
		case 0x88:
			/* B6 */
			adev->gfx.config.max_cu_per_sh = 6;
			break;
		case 0xc7:
		case 0x87:
		case 0xcb:
		case 0xe5:
		case 0x89:
		default:
			/* B4 */
			adev->gfx.config.max_cu_per_sh = 4;
			break;
		}

		adev->gfx.config.max_texture_channel_caches = 2;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = CARRIZO_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_STONEY:
		adev->gfx.config.max_shader_engines = 1;
		adev->gfx.config.max_tile_pipes = 2;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 1;

		switch (adev->pdev->revision) {
		case 0xc0:
		case 0xc1:
		case 0xc2:
		case 0xc4:
		case 0xc8:
		case 0xc9:
			adev->gfx.config.max_cu_per_sh = 3;
			break;
		case 0xd0:
		case 0xd1:
		case 0xd2:
		default:
			adev->gfx.config.max_cu_per_sh = 2;
			break;
		}

		adev->gfx.config.max_texture_channel_caches = 2;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 16;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = CARRIZO_GB_ADDR_CONFIG_GOLDEN;
		break;
	default:
		adev->gfx.config.max_shader_engines = 2;
		adev->gfx.config.max_tile_pipes = 4;
		adev->gfx.config.max_cu_per_sh = 2;
		adev->gfx.config.max_sh_per_se = 1;
		adev->gfx.config.max_backends_per_se = 2;
		adev->gfx.config.max_texture_channel_caches = 4;
		adev->gfx.config.max_gprs = 256;
		adev->gfx.config.max_gs_threads = 32;
		adev->gfx.config.max_hw_contexts = 8;

		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x130;
		gb_addr_config = TONGA_GB_ADDR_CONFIG_GOLDEN;
		break;
	}

	mc_shared_chmap = RREG32(mmMC_SHARED_CHMAP);
	adev->gfx.config.mc_arb_ramcfg = RREG32(mmMC_ARB_RAMCFG);
	mc_arb_ramcfg = adev->gfx.config.mc_arb_ramcfg;

	adev->gfx.config.num_tile_pipes = adev->gfx.config.max_tile_pipes;
	adev->gfx.config.mem_max_burst_length_bytes = 256;
	if (adev->flags & AMD_IS_APU) {
		/* Get memory bank mapping mode. */
		tmp = RREG32(mmMC_FUS_DRAM0_BANK_ADDR_MAPPING);
		dimm00_addr_map = REG_GET_FIELD(tmp, MC_FUS_DRAM0_BANK_ADDR_MAPPING, DIMM0ADDRMAP);
		dimm01_addr_map = REG_GET_FIELD(tmp, MC_FUS_DRAM0_BANK_ADDR_MAPPING, DIMM1ADDRMAP);

		tmp = RREG32(mmMC_FUS_DRAM1_BANK_ADDR_MAPPING);
		dimm10_addr_map = REG_GET_FIELD(tmp, MC_FUS_DRAM1_BANK_ADDR_MAPPING, DIMM0ADDRMAP);
		dimm11_addr_map = REG_GET_FIELD(tmp, MC_FUS_DRAM1_BANK_ADDR_MAPPING, DIMM1ADDRMAP);

		/* Validate settings in case only one DIMM installed. */
		if ((dimm00_addr_map == 0) || (dimm00_addr_map == 3) || (dimm00_addr_map == 4) || (dimm00_addr_map > 12))
			dimm00_addr_map = 0;
		if ((dimm01_addr_map == 0) || (dimm01_addr_map == 3) || (dimm01_addr_map == 4) || (dimm01_addr_map > 12))
			dimm01_addr_map = 0;
		if ((dimm10_addr_map == 0) || (dimm10_addr_map == 3) || (dimm10_addr_map == 4) || (dimm10_addr_map > 12))
			dimm10_addr_map = 0;
		if ((dimm11_addr_map == 0) || (dimm11_addr_map == 3) || (dimm11_addr_map == 4) || (dimm11_addr_map > 12))
			dimm11_addr_map = 0;

		/* If DIMM Addr map is 8GB, ROW size should be 2KB. Otherwise 1KB. */
		/* If ROW size(DIMM1) != ROW size(DMIMM0), ROW size should be larger one. */
		if ((dimm00_addr_map == 11) || (dimm01_addr_map == 11) || (dimm10_addr_map == 11) || (dimm11_addr_map == 11))
			adev->gfx.config.mem_row_size_in_kb = 2;
		else
			adev->gfx.config.mem_row_size_in_kb = 1;
	} else {
		tmp = REG_GET_FIELD(mc_arb_ramcfg, MC_ARB_RAMCFG, NOOFCOLS);
		adev->gfx.config.mem_row_size_in_kb = (4 * (1 << (8 + tmp))) / 1024;
		if (adev->gfx.config.mem_row_size_in_kb > 4)
			adev->gfx.config.mem_row_size_in_kb = 4;
	}

	adev->gfx.config.shader_engine_tile_size = 32;
	adev->gfx.config.num_gpus = 1;
	adev->gfx.config.multi_gpu_tile_size = 64;

	/* fix up row size */
	switch (adev->gfx.config.mem_row_size_in_kb) {
	case 1:
	default:
		gb_addr_config = REG_SET_FIELD(gb_addr_config, GB_ADDR_CONFIG, ROW_SIZE, 0);
		break;
	case 2:
		gb_addr_config = REG_SET_FIELD(gb_addr_config, GB_ADDR_CONFIG, ROW_SIZE, 1);
		break;
	case 4:
		gb_addr_config = REG_SET_FIELD(gb_addr_config, GB_ADDR_CONFIG, ROW_SIZE, 2);
		break;
	}
	adev->gfx.config.gb_addr_config = gb_addr_config;

	return 0;
}

static int gfx_v8_0_sw_init(void *handle)
{
	int i, r;
	struct amdgpu_ring *ring;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* EOP Event */
	r = amdgpu_irq_add_id(adev, 181, &adev->gfx.eop_irq);
	if (r)
		return r;

	/* Privileged reg */
	r = amdgpu_irq_add_id(adev, 184, &adev->gfx.priv_reg_irq);
	if (r)
		return r;

	/* Privileged inst */
	r = amdgpu_irq_add_id(adev, 185, &adev->gfx.priv_inst_irq);
	if (r)
		return r;

	adev->gfx.gfx_current_status = AMDGPU_GFX_NORMAL_MODE;

	gfx_v8_0_scratch_init(adev);

	r = gfx_v8_0_init_microcode(adev);
	if (r) {
		DRM_ERROR("Failed to load gfx firmware!\n");
		return r;
	}

	r = gfx_v8_0_rlc_init(adev);
	if (r) {
		DRM_ERROR("Failed to init rlc BOs!\n");
		return r;
	}

	r = gfx_v8_0_mec_init(adev);
	if (r) {
		DRM_ERROR("Failed to init MEC BOs!\n");
		return r;
	}

	/* set up the gfx ring */
	for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
		ring = &adev->gfx.gfx_ring[i];
		ring->ring_obj = NULL;
		sprintf(ring->name, "gfx");
		/* no gfx doorbells on iceland */
		if (adev->asic_type != CHIP_TOPAZ) {
			ring->use_doorbell = true;
			ring->doorbell_index = AMDGPU_DOORBELL_GFX_RING0;
		}

		r = amdgpu_ring_init(adev, ring, 1024,
				     PACKET3(PACKET3_NOP, 0x3FFF), 0xf,
				     &adev->gfx.eop_irq, AMDGPU_CP_IRQ_GFX_EOP,
				     AMDGPU_RING_TYPE_GFX);
		if (r)
			return r;
	}

	/* set up the compute queues */
	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		unsigned irq_type;

		/* max 32 queues per MEC */
		if ((i >= 32) || (i >= AMDGPU_MAX_COMPUTE_RINGS)) {
			DRM_ERROR("Too many (%d) compute rings!\n", i);
			break;
		}
		ring = &adev->gfx.compute_ring[i];
		ring->ring_obj = NULL;
		ring->use_doorbell = true;
		ring->doorbell_index = AMDGPU_DOORBELL_MEC_RING0 + i;
		ring->me = 1; /* first MEC */
		ring->pipe = i / 8;
		ring->queue = i % 8;
		sprintf(ring->name, "comp_%d.%d.%d", ring->me, ring->pipe, ring->queue);
		irq_type = AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP + ring->pipe;
		/* type-2 packets are deprecated on MEC, use type-3 instead */
		r = amdgpu_ring_init(adev, ring, 1024,
				     PACKET3(PACKET3_NOP, 0x3FFF), 0xf,
				     &adev->gfx.eop_irq, irq_type,
				     AMDGPU_RING_TYPE_COMPUTE);
		if (r)
			return r;
	}

	/* reserve GDS, GWS and OA resource for gfx */
	r = amdgpu_bo_create(adev, adev->gds.mem.gfx_partition_size,
			PAGE_SIZE, true,
			AMDGPU_GEM_DOMAIN_GDS, 0, NULL,
			NULL, &adev->gds.gds_gfx_bo);
	if (r)
		return r;

	r = amdgpu_bo_create(adev, adev->gds.gws.gfx_partition_size,
		PAGE_SIZE, true,
		AMDGPU_GEM_DOMAIN_GWS, 0, NULL,
		NULL, &adev->gds.gws_gfx_bo);
	if (r)
		return r;

	r = amdgpu_bo_create(adev, adev->gds.oa.gfx_partition_size,
			PAGE_SIZE, true,
			AMDGPU_GEM_DOMAIN_OA, 0, NULL,
			NULL, &adev->gds.oa_gfx_bo);
	if (r)
		return r;

	adev->gfx.ce_ram_size = 0x8000;

	r = gfx_v8_0_gpu_early_init(adev);
	if (r)
		return r;

	return 0;
}

static int gfx_v8_0_sw_fini(void *handle)
{
	int i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_bo_unref(&adev->gds.oa_gfx_bo);
	amdgpu_bo_unref(&adev->gds.gws_gfx_bo);
	amdgpu_bo_unref(&adev->gds.gds_gfx_bo);

	for (i = 0; i < adev->gfx.num_gfx_rings; i++)
		amdgpu_ring_fini(&adev->gfx.gfx_ring[i]);
	for (i = 0; i < adev->gfx.num_compute_rings; i++)
		amdgpu_ring_fini(&adev->gfx.compute_ring[i]);

	gfx_v8_0_mec_fini(adev);

	gfx_v8_0_rlc_fini(adev);

	gfx_v8_0_free_microcode(adev);

	return 0;
}

static void gfx_v8_0_tiling_mode_table_init(struct amdgpu_device *adev)
{
	uint32_t *modearray, *mod2array;
	const u32 num_tile_mode_states = ARRAY_SIZE(adev->gfx.config.tile_mode_array);
	const u32 num_secondary_tile_mode_states = ARRAY_SIZE(adev->gfx.config.macrotile_mode_array);
	u32 reg_offset;

	modearray = adev->gfx.config.tile_mode_array;
	mod2array = adev->gfx.config.macrotile_mode_array;

	for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
		modearray[reg_offset] = 0;

	for (reg_offset = 0; reg_offset <  num_secondary_tile_mode_states; reg_offset++)
		mod2array[reg_offset] = 0;

	switch (adev->asic_type) {
	case CHIP_TOPAZ:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P2));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_8_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			if (reg_offset != 7 && reg_offset != 12 && reg_offset != 17 &&
			    reg_offset != 23)
				WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	case CHIP_FIJI:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[7] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[12] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[17] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[23] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P16_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[30] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				 NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				 NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				 NUM_BANKS(ADDR_SURF_4_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	case CHIP_TONGA:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[7] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[12] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[17] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[23] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[30] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				 NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				 NUM_BANKS(ADDR_SURF_4_BANK));
		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				 NUM_BANKS(ADDR_SURF_4_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	case CHIP_POLARIS11:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[7] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[12] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[17] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[23] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[30] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));

		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_4_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	case CHIP_POLARIS10:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[7] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[12] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[17] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[23] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P8_32x32_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[30] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P4_16x16) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_16_BANK));

		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_8_BANK));

		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_4_BANK));

		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_1) |
				NUM_BANKS(ADDR_SURF_4_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	case CHIP_STONEY:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P2));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_8_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			if (reg_offset != 7 && reg_offset != 12 && reg_offset != 17 &&
			    reg_offset != 23)
				WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	default:
		dev_warn(adev->dev,
			 "Unknown chip type (%d) in function gfx_v8_0_tiling_mode_table_init() falling through to CHIP_CARRIZO\n",
			 adev->asic_type);

	case CHIP_CARRIZO:
		modearray[0] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_64B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[1] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_128B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[2] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_256B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[3] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_512B) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[4] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[5] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[6] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				TILE_SPLIT(ADDR_SURF_TILE_SPLIT_2KB) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DEPTH_MICRO_TILING));
		modearray[8] = (ARRAY_MODE(ARRAY_LINEAR_ALIGNED) |
				PIPE_CONFIG(ADDR_SURF_P2));
		modearray[9] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				PIPE_CONFIG(ADDR_SURF_P2) |
				MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[10] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[11] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_DISPLAY_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[13] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[14] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[15] = (ARRAY_MODE(ARRAY_3D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[16] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));
		modearray[18] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[19] = (ARRAY_MODE(ARRAY_1D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[20] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[21] = (ARRAY_MODE(ARRAY_3D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[22] = (ARRAY_MODE(ARRAY_PRT_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[24] = (ARRAY_MODE(ARRAY_2D_TILED_THICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THIN_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[25] = (ARRAY_MODE(ARRAY_2D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[26] = (ARRAY_MODE(ARRAY_3D_TILED_XTHICK) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_THICK_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_1));
		modearray[27] = (ARRAY_MODE(ARRAY_1D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[28] = (ARRAY_MODE(ARRAY_2D_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_2));
		modearray[29] = (ARRAY_MODE(ARRAY_PRT_TILED_THIN1) |
				 PIPE_CONFIG(ADDR_SURF_P2) |
				 MICRO_TILE_MODE_NEW(ADDR_SURF_ROTATED_MICRO_TILING) |
				 SAMPLE_SPLIT(ADDR_SURF_SAMPLE_SPLIT_8));

		mod2array[0] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[1] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[2] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[3] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[4] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[5] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[6] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				NUM_BANKS(ADDR_SURF_8_BANK));
		mod2array[8] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_8) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[9] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_4) |
				BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[10] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_4) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[11] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_2) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[12] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_2) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[13] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_4) |
				 NUM_BANKS(ADDR_SURF_16_BANK));
		mod2array[14] = (BANK_WIDTH(ADDR_SURF_BANK_WIDTH_1) |
				 BANK_HEIGHT(ADDR_SURF_BANK_HEIGHT_1) |
				 MACRO_TILE_ASPECT(ADDR_SURF_MACRO_ASPECT_2) |
				 NUM_BANKS(ADDR_SURF_8_BANK));

		for (reg_offset = 0; reg_offset < num_tile_mode_states; reg_offset++)
			if (reg_offset != 7 && reg_offset != 12 && reg_offset != 17 &&
			    reg_offset != 23)
				WREG32(mmGB_TILE_MODE0 + reg_offset, modearray[reg_offset]);

		for (reg_offset = 0; reg_offset < num_secondary_tile_mode_states; reg_offset++)
			if (reg_offset != 7)
				WREG32(mmGB_MACROTILE_MODE0 + reg_offset, mod2array[reg_offset]);

		break;
	}
}

void gfx_v8_0_select_se_sh(struct amdgpu_device *adev, u32 se_num, u32 sh_num)
{
	u32 data = REG_SET_FIELD(0, GRBM_GFX_INDEX, INSTANCE_BROADCAST_WRITES, 1);

	if ((se_num == 0xffffffff) && (sh_num == 0xffffffff)) {
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SH_BROADCAST_WRITES, 1);
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_BROADCAST_WRITES, 1);
	} else if (se_num == 0xffffffff) {
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SH_INDEX, sh_num);
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_BROADCAST_WRITES, 1);
	} else if (sh_num == 0xffffffff) {
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SH_BROADCAST_WRITES, 1);
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_INDEX, se_num);
	} else {
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SH_INDEX, sh_num);
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_INDEX, se_num);
	}
	WREG32(mmGRBM_GFX_INDEX, data);
}

static u32 gfx_v8_0_create_bitmask(u32 bit_width)
{
	return (u32)((1ULL << bit_width) - 1);
}

static u32 gfx_v8_0_get_rb_active_bitmap(struct amdgpu_device *adev)
{
	u32 data, mask;

	data = RREG32(mmCC_RB_BACKEND_DISABLE);
	data |= RREG32(mmGC_USER_RB_BACKEND_DISABLE);

	data &= CC_RB_BACKEND_DISABLE__BACKEND_DISABLE_MASK;
	data >>= GC_USER_RB_BACKEND_DISABLE__BACKEND_DISABLE__SHIFT;

	mask = gfx_v8_0_create_bitmask(adev->gfx.config.max_backends_per_se /
				       adev->gfx.config.max_sh_per_se);

	return (~data) & mask;
}

static void gfx_v8_0_setup_rb(struct amdgpu_device *adev)
{
	int i, j;
	u32 data;
	u32 active_rbs = 0;
	u32 rb_bitmap_width_per_sh = adev->gfx.config.max_backends_per_se /
					adev->gfx.config.max_sh_per_se;

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			gfx_v8_0_select_se_sh(adev, i, j);
			data = gfx_v8_0_get_rb_active_bitmap(adev);
			active_rbs |= data << ((i * adev->gfx.config.max_sh_per_se + j) *
					       rb_bitmap_width_per_sh);
		}
	}
	gfx_v8_0_select_se_sh(adev, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);

	adev->gfx.config.backend_enable_mask = active_rbs;
	adev->gfx.config.num_rbs = hweight32(active_rbs);
}

/**
 * gfx_v8_0_init_compute_vmid - gart enable
 *
 * @rdev: amdgpu_device pointer
 *
 * Initialize compute vmid sh_mem registers
 *
 */
#define DEFAULT_SH_MEM_BASES	(0x6000)
#define FIRST_COMPUTE_VMID	(8)
#define LAST_COMPUTE_VMID	(16)
static void gfx_v8_0_init_compute_vmid(struct amdgpu_device *adev)
{
	int i;
	uint32_t sh_mem_config;
	uint32_t sh_mem_bases;

	/*
	 * Configure apertures:
	 * LDS:         0x60000000'00000000 - 0x60000001'00000000 (4GB)
	 * Scratch:     0x60000001'00000000 - 0x60000002'00000000 (4GB)
	 * GPUVM:       0x60010000'00000000 - 0x60020000'00000000 (1TB)
	 */
	sh_mem_bases = DEFAULT_SH_MEM_BASES | (DEFAULT_SH_MEM_BASES << 16);

	sh_mem_config = SH_MEM_ADDRESS_MODE_HSA64 <<
			SH_MEM_CONFIG__ADDRESS_MODE__SHIFT |
			SH_MEM_ALIGNMENT_MODE_UNALIGNED <<
			SH_MEM_CONFIG__ALIGNMENT_MODE__SHIFT |
			MTYPE_CC << SH_MEM_CONFIG__DEFAULT_MTYPE__SHIFT |
			SH_MEM_CONFIG__PRIVATE_ATC_MASK;

	mutex_lock(&adev->srbm_mutex);
	for (i = FIRST_COMPUTE_VMID; i < LAST_COMPUTE_VMID; i++) {
		vi_srbm_select(adev, 0, 0, 0, i);
		/* CP and shaders */
		WREG32(mmSH_MEM_CONFIG, sh_mem_config);
		WREG32(mmSH_MEM_APE1_BASE, 1);
		WREG32(mmSH_MEM_APE1_LIMIT, 0);
		WREG32(mmSH_MEM_BASES, sh_mem_bases);
	}
	vi_srbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);
}

static void gfx_v8_0_gpu_init(struct amdgpu_device *adev)
{
	u32 tmp;
	int i;

	tmp = RREG32(mmGRBM_CNTL);
	tmp = REG_SET_FIELD(tmp, GRBM_CNTL, READ_TIMEOUT, 0xff);
	WREG32(mmGRBM_CNTL, tmp);

	WREG32(mmGB_ADDR_CONFIG, adev->gfx.config.gb_addr_config);
	WREG32(mmHDP_ADDR_CONFIG, adev->gfx.config.gb_addr_config);
	WREG32(mmDMIF_ADDR_CALC, adev->gfx.config.gb_addr_config);

	gfx_v8_0_tiling_mode_table_init(adev);

	gfx_v8_0_setup_rb(adev);
	gfx_v8_0_get_cu_info(adev);

	/* XXX SH_MEM regs */
	/* where to put LDS, scratch, GPUVM in FSA64 space */
	mutex_lock(&adev->srbm_mutex);
	for (i = 0; i < 16; i++) {
		vi_srbm_select(adev, 0, 0, 0, i);
		/* CP and shaders */
		if (i == 0) {
			tmp = REG_SET_FIELD(0, SH_MEM_CONFIG, DEFAULT_MTYPE, MTYPE_UC);
			tmp = REG_SET_FIELD(tmp, SH_MEM_CONFIG, APE1_MTYPE, MTYPE_UC);
			tmp = REG_SET_FIELD(tmp, SH_MEM_CONFIG, ALIGNMENT_MODE,
					    SH_MEM_ALIGNMENT_MODE_UNALIGNED);
			WREG32(mmSH_MEM_CONFIG, tmp);
		} else {
			tmp = REG_SET_FIELD(0, SH_MEM_CONFIG, DEFAULT_MTYPE, MTYPE_NC);
			tmp = REG_SET_FIELD(tmp, SH_MEM_CONFIG, APE1_MTYPE, MTYPE_NC);
			tmp = REG_SET_FIELD(tmp, SH_MEM_CONFIG, ALIGNMENT_MODE,
					    SH_MEM_ALIGNMENT_MODE_UNALIGNED);
			WREG32(mmSH_MEM_CONFIG, tmp);
		}

		WREG32(mmSH_MEM_APE1_BASE, 1);
		WREG32(mmSH_MEM_APE1_LIMIT, 0);
		WREG32(mmSH_MEM_BASES, 0);
	}
	vi_srbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);

	gfx_v8_0_init_compute_vmid(adev);

	mutex_lock(&adev->grbm_idx_mutex);
	/*
	 * making sure that the following register writes will be broadcasted
	 * to all the shaders
	 */
	gfx_v8_0_select_se_sh(adev, 0xffffffff, 0xffffffff);

	WREG32(mmPA_SC_FIFO_SIZE,
		   (adev->gfx.config.sc_prim_fifo_size_frontend <<
			PA_SC_FIFO_SIZE__SC_FRONTEND_PRIM_FIFO_SIZE__SHIFT) |
		   (adev->gfx.config.sc_prim_fifo_size_backend <<
			PA_SC_FIFO_SIZE__SC_BACKEND_PRIM_FIFO_SIZE__SHIFT) |
		   (adev->gfx.config.sc_hiz_tile_fifo_size <<
			PA_SC_FIFO_SIZE__SC_HIZ_TILE_FIFO_SIZE__SHIFT) |
		   (adev->gfx.config.sc_earlyz_tile_fifo_size <<
			PA_SC_FIFO_SIZE__SC_EARLYZ_TILE_FIFO_SIZE__SHIFT));
	mutex_unlock(&adev->grbm_idx_mutex);

}

static void gfx_v8_0_wait_for_rlc_serdes(struct amdgpu_device *adev)
{
	u32 i, j, k;
	u32 mask;

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			gfx_v8_0_select_se_sh(adev, i, j);
			for (k = 0; k < adev->usec_timeout; k++) {
				if (RREG32(mmRLC_SERDES_CU_MASTER_BUSY) == 0)
					break;
				udelay(1);
			}
		}
	}
	gfx_v8_0_select_se_sh(adev, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);

	mask = RLC_SERDES_NONCU_MASTER_BUSY__SE_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__GC_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__TC0_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__TC1_MASTER_BUSY_MASK;
	for (k = 0; k < adev->usec_timeout; k++) {
		if ((RREG32(mmRLC_SERDES_NONCU_MASTER_BUSY) & mask) == 0)
			break;
		udelay(1);
	}
}

static void gfx_v8_0_enable_gui_idle_interrupt(struct amdgpu_device *adev,
					       bool enable)
{
	u32 tmp = RREG32(mmCP_INT_CNTL_RING0);

	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_BUSY_INT_ENABLE, enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_EMPTY_INT_ENABLE, enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CMP_BUSY_INT_ENABLE, enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, GFX_IDLE_INT_ENABLE, enable ? 1 : 0);

	WREG32(mmCP_INT_CNTL_RING0, tmp);
}

static void gfx_v8_0_init_csb(struct amdgpu_device *adev)
{
	/* csib */
	WREG32(mmRLC_CSIB_ADDR_HI,
			adev->gfx.rlc.clear_state_gpu_addr >> 32);
	WREG32(mmRLC_CSIB_ADDR_LO,
			adev->gfx.rlc.clear_state_gpu_addr & 0xfffffffc);
	WREG32(mmRLC_CSIB_LENGTH,
			adev->gfx.rlc.clear_state_size);
}

static void gfx_v8_0_parse_ind_reg_list(int *register_list_format,
				int ind_offset,
				int list_size,
				int *unique_indices,
				int *indices_count,
				int max_indices,
				int *ind_start_offsets,
				int *offset_count,
				int max_offset)
{
	int indices;
	bool new_entry = true;

	for (; ind_offset < list_size; ind_offset++) {

		if (new_entry) {
			new_entry = false;
			ind_start_offsets[*offset_count] = ind_offset;
			*offset_count = *offset_count + 1;
			BUG_ON(*offset_count >= max_offset);
		}

		if (register_list_format[ind_offset] == 0xFFFFFFFF) {
			new_entry = true;
			continue;
		}

		ind_offset += 2;

		/* look for the matching indice */
		for (indices = 0;
			indices < *indices_count;
			indices++) {
			if (unique_indices[indices] ==
				register_list_format[ind_offset])
				break;
		}

		if (indices >= *indices_count) {
			unique_indices[*indices_count] =
				register_list_format[ind_offset];
			indices = *indices_count;
			*indices_count = *indices_count + 1;
			BUG_ON(*indices_count >= max_indices);
		}

		register_list_format[ind_offset] = indices;
	}
}

static int gfx_v8_0_init_save_restore_list(struct amdgpu_device *adev)
{
	int i, temp, data;
	int unique_indices[] = {0, 0, 0, 0, 0, 0, 0, 0};
	int indices_count = 0;
	int indirect_start_offsets[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	int offset_count = 0;

	int list_size;
	unsigned int *register_list_format =
		kmalloc(adev->gfx.rlc.reg_list_format_size_bytes, GFP_KERNEL);
	if (register_list_format == NULL)
		return -ENOMEM;
	memcpy(register_list_format, adev->gfx.rlc.register_list_format,
			adev->gfx.rlc.reg_list_format_size_bytes);

	gfx_v8_0_parse_ind_reg_list(register_list_format,
				RLC_FormatDirectRegListLength,
				adev->gfx.rlc.reg_list_format_size_bytes >> 2,
				unique_indices,
				&indices_count,
				sizeof(unique_indices) / sizeof(int),
				indirect_start_offsets,
				&offset_count,
				sizeof(indirect_start_offsets)/sizeof(int));

	/* save and restore list */
	temp = RREG32(mmRLC_SRM_CNTL);
	temp |= RLC_SRM_CNTL__AUTO_INCR_ADDR_MASK;
	WREG32(mmRLC_SRM_CNTL, temp);

	WREG32(mmRLC_SRM_ARAM_ADDR, 0);
	for (i = 0; i < adev->gfx.rlc.reg_list_size_bytes >> 2; i++)
		WREG32(mmRLC_SRM_ARAM_DATA, adev->gfx.rlc.register_restore[i]);

	/* indirect list */
	WREG32(mmRLC_GPM_SCRATCH_ADDR, adev->gfx.rlc.reg_list_format_start);
	for (i = 0; i < adev->gfx.rlc.reg_list_format_size_bytes >> 2; i++)
		WREG32(mmRLC_GPM_SCRATCH_DATA, register_list_format[i]);

	list_size = adev->gfx.rlc.reg_list_size_bytes >> 2;
	list_size = list_size >> 1;
	WREG32(mmRLC_GPM_SCRATCH_ADDR, adev->gfx.rlc.reg_restore_list_size);
	WREG32(mmRLC_GPM_SCRATCH_DATA, list_size);

	/* starting offsets starts */
	WREG32(mmRLC_GPM_SCRATCH_ADDR,
		adev->gfx.rlc.starting_offsets_start);
	for (i = 0; i < sizeof(indirect_start_offsets)/sizeof(int); i++)
		WREG32(mmRLC_GPM_SCRATCH_DATA,
				indirect_start_offsets[i]);

	/* unique indices */
	temp = mmRLC_SRM_INDEX_CNTL_ADDR_0;
	data = mmRLC_SRM_INDEX_CNTL_DATA_0;
	for (i = 0; i < sizeof(unique_indices) / sizeof(int); i++) {
		amdgpu_mm_wreg(adev, temp + i, unique_indices[i] & 0x3FFFF, false);
		amdgpu_mm_wreg(adev, data + i, unique_indices[i] >> 20, false);
	}
	kfree(register_list_format);

	return 0;
}

static void gfx_v8_0_enable_save_restore_machine(struct amdgpu_device *adev)
{
	uint32_t data;

	data = RREG32(mmRLC_SRM_CNTL);
	data |= RLC_SRM_CNTL__SRM_ENABLE_MASK;
	WREG32(mmRLC_SRM_CNTL, data);
}

static void polaris11_init_power_gating(struct amdgpu_device *adev)
{
	uint32_t data;

	if (adev->pg_flags & (AMD_PG_SUPPORT_GFX_PG |
			AMD_PG_SUPPORT_GFX_SMG |
			AMD_PG_SUPPORT_GFX_DMG)) {
		data = RREG32(mmCP_RB_WPTR_POLL_CNTL);
		data &= ~CP_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT_MASK;
		data |= (0x60 << CP_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT__SHIFT);
		WREG32(mmCP_RB_WPTR_POLL_CNTL, data);

		data = 0;
		data |= (0x10 << RLC_PG_DELAY__POWER_UP_DELAY__SHIFT);
		data |= (0x10 << RLC_PG_DELAY__POWER_DOWN_DELAY__SHIFT);
		data |= (0x10 << RLC_PG_DELAY__CMD_PROPAGATE_DELAY__SHIFT);
		data |= (0x10 << RLC_PG_DELAY__MEM_SLEEP_DELAY__SHIFT);
		WREG32(mmRLC_PG_DELAY, data);

		data = RREG32(mmRLC_PG_DELAY_2);
		data &= ~RLC_PG_DELAY_2__SERDES_CMD_DELAY_MASK;
		data |= (0x3 << RLC_PG_DELAY_2__SERDES_CMD_DELAY__SHIFT);
		WREG32(mmRLC_PG_DELAY_2, data);

		data = RREG32(mmRLC_AUTO_PG_CTRL);
		data &= ~RLC_AUTO_PG_CTRL__GRBM_REG_SAVE_GFX_IDLE_THRESHOLD_MASK;
		data |= (0x55f0 << RLC_AUTO_PG_CTRL__GRBM_REG_SAVE_GFX_IDLE_THRESHOLD__SHIFT);
		WREG32(mmRLC_AUTO_PG_CTRL, data);
	}
}

static void gfx_v8_0_init_pg(struct amdgpu_device *adev)
{
	if (adev->pg_flags & (AMD_PG_SUPPORT_GFX_PG |
			      AMD_PG_SUPPORT_GFX_SMG |
			      AMD_PG_SUPPORT_GFX_DMG |
			      AMD_PG_SUPPORT_CP |
			      AMD_PG_SUPPORT_GDS |
			      AMD_PG_SUPPORT_RLC_SMU_HS)) {
		gfx_v8_0_init_csb(adev);
		gfx_v8_0_init_save_restore_list(adev);
		gfx_v8_0_enable_save_restore_machine(adev);

		if (adev->asic_type == CHIP_POLARIS11)
			polaris11_init_power_gating(adev);
	}
}

void gfx_v8_0_rlc_stop(struct amdgpu_device *adev)
{
	u32 tmp = RREG32(mmRLC_CNTL);

	tmp = REG_SET_FIELD(tmp, RLC_CNTL, RLC_ENABLE_F32, 0);
	WREG32(mmRLC_CNTL, tmp);

	gfx_v8_0_enable_gui_idle_interrupt(adev, false);

	gfx_v8_0_wait_for_rlc_serdes(adev);
}

static void gfx_v8_0_rlc_reset(struct amdgpu_device *adev)
{
	u32 tmp = RREG32(mmGRBM_SOFT_RESET);

	tmp = REG_SET_FIELD(tmp, GRBM_SOFT_RESET, SOFT_RESET_RLC, 1);
	WREG32(mmGRBM_SOFT_RESET, tmp);
	udelay(50);
	tmp = REG_SET_FIELD(tmp, GRBM_SOFT_RESET, SOFT_RESET_RLC, 0);
	WREG32(mmGRBM_SOFT_RESET, tmp);
	udelay(50);
}

static void gfx_v8_0_rlc_start(struct amdgpu_device *adev)
{
	u32 tmp = RREG32(mmRLC_CNTL);

	tmp = REG_SET_FIELD(tmp, RLC_CNTL, RLC_ENABLE_F32, 1);
	WREG32(mmRLC_CNTL, tmp);

	/* carrizo do enable cp interrupt after cp inited */
	if (!(adev->flags & AMD_IS_APU))
		gfx_v8_0_enable_gui_idle_interrupt(adev, true);

	udelay(50);
}

static int gfx_v8_0_rlc_load_microcode(struct amdgpu_device *adev)
{
	const struct rlc_firmware_header_v2_0 *hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;

	if (!adev->gfx.rlc_fw)
		return -EINVAL;

	hdr = (const struct rlc_firmware_header_v2_0 *)adev->gfx.rlc_fw->data;
	amdgpu_ucode_print_rlc_hdr(&hdr->header);

	fw_data = (const __le32 *)(adev->gfx.rlc_fw->data +
			   le32_to_cpu(hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;

	WREG32(mmRLC_GPM_UCODE_ADDR, 0);
	for (i = 0; i < fw_size; i++)
		WREG32(mmRLC_GPM_UCODE_DATA, le32_to_cpup(fw_data++));
	WREG32(mmRLC_GPM_UCODE_ADDR, adev->gfx.rlc_fw_version);

	return 0;
}

static int gfx_v8_0_rlc_resume(struct amdgpu_device *adev)
{
	int r;

	gfx_v8_0_rlc_stop(adev);

	/* disable CG */
	WREG32(mmRLC_CGCG_CGLS_CTRL, 0);
	if (adev->asic_type == CHIP_POLARIS11 ||
		adev->asic_type == CHIP_POLARIS10)
		WREG32(mmRLC_CGCG_CGLS_CTRL_3D, 0);

	/* disable PG */
	WREG32(mmRLC_PG_CNTL, 0);

	gfx_v8_0_rlc_reset(adev);

	gfx_v8_0_init_pg(adev);

	if (!adev->pp_enabled) {
		if (!adev->firmware.smu_load) {
			/* legacy rlc firmware loading */
			r = gfx_v8_0_rlc_load_microcode(adev);
			if (r)
				return r;
		} else {
			r = adev->smu.smumgr_funcs->check_fw_load_finish(adev,
							AMDGPU_UCODE_ID_RLC_G);
			if (r)
				return -EINVAL;
		}
	}

	gfx_v8_0_rlc_start(adev);

	return 0;
}

static void gfx_v8_0_cp_gfx_enable(struct amdgpu_device *adev, bool enable)
{
	int i;
	u32 tmp = RREG32(mmCP_ME_CNTL);

	if (enable) {
		tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, ME_HALT, 0);
		tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, PFP_HALT, 0);
		tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, CE_HALT, 0);
	} else {
		tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, ME_HALT, 1);
		tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, PFP_HALT, 1);
		tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, CE_HALT, 1);
		for (i = 0; i < adev->gfx.num_gfx_rings; i++)
			adev->gfx.gfx_ring[i].ready = false;
	}
	WREG32(mmCP_ME_CNTL, tmp);
	udelay(50);
}

static int gfx_v8_0_cp_gfx_load_microcode(struct amdgpu_device *adev)
{
	const struct gfx_firmware_header_v1_0 *pfp_hdr;
	const struct gfx_firmware_header_v1_0 *ce_hdr;
	const struct gfx_firmware_header_v1_0 *me_hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;

	if (!adev->gfx.me_fw || !adev->gfx.pfp_fw || !adev->gfx.ce_fw)
		return -EINVAL;

	pfp_hdr = (const struct gfx_firmware_header_v1_0 *)
		adev->gfx.pfp_fw->data;
	ce_hdr = (const struct gfx_firmware_header_v1_0 *)
		adev->gfx.ce_fw->data;
	me_hdr = (const struct gfx_firmware_header_v1_0 *)
		adev->gfx.me_fw->data;

	amdgpu_ucode_print_gfx_hdr(&pfp_hdr->header);
	amdgpu_ucode_print_gfx_hdr(&ce_hdr->header);
	amdgpu_ucode_print_gfx_hdr(&me_hdr->header);

	gfx_v8_0_cp_gfx_enable(adev, false);

	/* PFP */
	fw_data = (const __le32 *)
		(adev->gfx.pfp_fw->data +
		 le32_to_cpu(pfp_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(pfp_hdr->header.ucode_size_bytes) / 4;
	WREG32(mmCP_PFP_UCODE_ADDR, 0);
	for (i = 0; i < fw_size; i++)
		WREG32(mmCP_PFP_UCODE_DATA, le32_to_cpup(fw_data++));
	WREG32(mmCP_PFP_UCODE_ADDR, adev->gfx.pfp_fw_version);

	/* CE */
	fw_data = (const __le32 *)
		(adev->gfx.ce_fw->data +
		 le32_to_cpu(ce_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(ce_hdr->header.ucode_size_bytes) / 4;
	WREG32(mmCP_CE_UCODE_ADDR, 0);
	for (i = 0; i < fw_size; i++)
		WREG32(mmCP_CE_UCODE_DATA, le32_to_cpup(fw_data++));
	WREG32(mmCP_CE_UCODE_ADDR, adev->gfx.ce_fw_version);

	/* ME */
	fw_data = (const __le32 *)
		(adev->gfx.me_fw->data +
		 le32_to_cpu(me_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(me_hdr->header.ucode_size_bytes) / 4;
	WREG32(mmCP_ME_RAM_WADDR, 0);
	for (i = 0; i < fw_size; i++)
		WREG32(mmCP_ME_RAM_DATA, le32_to_cpup(fw_data++));
	WREG32(mmCP_ME_RAM_WADDR, adev->gfx.me_fw_version);

	return 0;
}

static u32 gfx_v8_0_get_csb_size(struct amdgpu_device *adev)
{
	u32 count = 0;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	/* begin clear state */
	count += 2;
	/* context control state */
	count += 3;

	for (sect = vi_cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT)
				count += 2 + ext->reg_count;
			else
				return 0;
		}
	}
	/* pa_sc_raster_config/pa_sc_raster_config1 */
	count += 4;
	/* end clear state */
	count += 2;
	/* clear state */
	count += 2;

	return count;
}

static int gfx_v8_0_cp_gfx_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->gfx.gfx_ring[0];
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;
	int r, i;

	/* init the CP */
	WREG32(mmCP_MAX_CONTEXT, adev->gfx.config.max_hw_contexts - 1);
	WREG32(mmCP_ENDIAN_SWAP, 0);
	WREG32(mmCP_DEVICE_ID, 1);

	gfx_v8_0_cp_gfx_enable(adev, true);

	r = amdgpu_ring_alloc(ring, gfx_v8_0_get_csb_size(adev) + 4);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to lock ring (%d).\n", r);
		return r;
	}

	/* clear state buffer */
	amdgpu_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	amdgpu_ring_write(ring, PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	amdgpu_ring_write(ring, PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	amdgpu_ring_write(ring, 0x80000000);
	amdgpu_ring_write(ring, 0x80000000);

	for (sect = vi_cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT) {
				amdgpu_ring_write(ring,
				       PACKET3(PACKET3_SET_CONTEXT_REG,
					       ext->reg_count));
				amdgpu_ring_write(ring,
				       ext->reg_index - PACKET3_SET_CONTEXT_REG_START);
				for (i = 0; i < ext->reg_count; i++)
					amdgpu_ring_write(ring, ext->extent[i]);
			}
		}
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_CONTEXT_REG, 2));
	amdgpu_ring_write(ring, mmPA_SC_RASTER_CONFIG - PACKET3_SET_CONTEXT_REG_START);
	switch (adev->asic_type) {
	case CHIP_TONGA:
	case CHIP_POLARIS10:
		amdgpu_ring_write(ring, 0x16000012);
		amdgpu_ring_write(ring, 0x0000002A);
		break;
	case CHIP_POLARIS11:
		amdgpu_ring_write(ring, 0x16000012);
		amdgpu_ring_write(ring, 0x00000000);
		break;
	case CHIP_FIJI:
		amdgpu_ring_write(ring, 0x3a00161a);
		amdgpu_ring_write(ring, 0x0000002e);
		break;
	case CHIP_CARRIZO:
		amdgpu_ring_write(ring, 0x00000002);
		amdgpu_ring_write(ring, 0x00000000);
		break;
	case CHIP_TOPAZ:
		amdgpu_ring_write(ring, adev->gfx.config.num_rbs == 1 ?
				0x00000000 : 0x00000002);
		amdgpu_ring_write(ring, 0x00000000);
		break;
	case CHIP_STONEY:
		amdgpu_ring_write(ring, 0x00000000);
		amdgpu_ring_write(ring, 0x00000000);
		break;
	default:
		BUG();
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	amdgpu_ring_write(ring, PACKET3_PREAMBLE_END_CLEAR_STATE);

	amdgpu_ring_write(ring, PACKET3(PACKET3_CLEAR_STATE, 0));
	amdgpu_ring_write(ring, 0);

	/* init the CE partitions */
	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_BASE, 2));
	amdgpu_ring_write(ring, PACKET3_BASE_INDEX(CE_PARTITION_BASE));
	amdgpu_ring_write(ring, 0x8000);
	amdgpu_ring_write(ring, 0x8000);

	amdgpu_ring_commit(ring);

	return 0;
}

static int gfx_v8_0_cp_gfx_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	u32 tmp;
	u32 rb_bufsz;
	u64 rb_addr, rptr_addr;
	int r;

	/* Set the write pointer delay */
	WREG32(mmCP_RB_WPTR_DELAY, 0);

	/* set the RB to use vmid 0 */
	WREG32(mmCP_RB_VMID, 0);

	/* Set ring buffer size */
	ring = &adev->gfx.gfx_ring[0];
	rb_bufsz = order_base_2(ring->ring_size / 8);
	tmp = REG_SET_FIELD(0, CP_RB0_CNTL, RB_BUFSZ, rb_bufsz);
	tmp = REG_SET_FIELD(tmp, CP_RB0_CNTL, RB_BLKSZ, rb_bufsz - 2);
	tmp = REG_SET_FIELD(tmp, CP_RB0_CNTL, MTYPE, 3);
	tmp = REG_SET_FIELD(tmp, CP_RB0_CNTL, MIN_IB_AVAILSZ, 1);
#ifdef __BIG_ENDIAN
	tmp = REG_SET_FIELD(tmp, CP_RB0_CNTL, BUF_SWAP, 1);
#endif
	WREG32(mmCP_RB0_CNTL, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32(mmCP_RB0_CNTL, tmp | CP_RB0_CNTL__RB_RPTR_WR_ENA_MASK);
	ring->wptr = 0;
	WREG32(mmCP_RB0_WPTR, ring->wptr);

	/* set the wb address wether it's enabled or not */
	rptr_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);
	WREG32(mmCP_RB0_RPTR_ADDR, lower_32_bits(rptr_addr));
	WREG32(mmCP_RB0_RPTR_ADDR_HI, upper_32_bits(rptr_addr) & 0xFF);

	mdelay(1);
	WREG32(mmCP_RB0_CNTL, tmp);

	rb_addr = ring->gpu_addr >> 8;
	WREG32(mmCP_RB0_BASE, rb_addr);
	WREG32(mmCP_RB0_BASE_HI, upper_32_bits(rb_addr));

	/* no gfx doorbells on iceland */
	if (adev->asic_type != CHIP_TOPAZ) {
		tmp = RREG32(mmCP_RB_DOORBELL_CONTROL);
		if (ring->use_doorbell) {
			tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
					    DOORBELL_OFFSET, ring->doorbell_index);
			tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
					    DOORBELL_HIT, 0);
			tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
					    DOORBELL_EN, 1);
		} else {
			tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
					    DOORBELL_EN, 0);
		}
		WREG32(mmCP_RB_DOORBELL_CONTROL, tmp);

		if (adev->asic_type == CHIP_TONGA) {
			tmp = REG_SET_FIELD(0, CP_RB_DOORBELL_RANGE_LOWER,
					    DOORBELL_RANGE_LOWER,
					    AMDGPU_DOORBELL_GFX_RING0);
			WREG32(mmCP_RB_DOORBELL_RANGE_LOWER, tmp);

			WREG32(mmCP_RB_DOORBELL_RANGE_UPPER,
			       CP_RB_DOORBELL_RANGE_UPPER__DOORBELL_RANGE_UPPER_MASK);
		}

	}

	/* start the ring */
	gfx_v8_0_cp_gfx_start(adev);
	ring->ready = true;
	r = amdgpu_ring_test_ring(ring);
	if (r) {
		ring->ready = false;
		return r;
	}

	return 0;
}

static void gfx_v8_0_cp_compute_enable(struct amdgpu_device *adev, bool enable)
{
	int i;

	if (enable) {
		WREG32(mmCP_MEC_CNTL, 0);
	} else {
		WREG32(mmCP_MEC_CNTL, (CP_MEC_CNTL__MEC_ME1_HALT_MASK | CP_MEC_CNTL__MEC_ME2_HALT_MASK));
		for (i = 0; i < adev->gfx.num_compute_rings; i++)
			adev->gfx.compute_ring[i].ready = false;
	}
	udelay(50);
}

static int gfx_v8_0_cp_compute_load_microcode(struct amdgpu_device *adev)
{
	const struct gfx_firmware_header_v1_0 *mec_hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;

	if (!adev->gfx.mec_fw)
		return -EINVAL;

	gfx_v8_0_cp_compute_enable(adev, false);

	mec_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.mec_fw->data;
	amdgpu_ucode_print_gfx_hdr(&mec_hdr->header);

	fw_data = (const __le32 *)
		(adev->gfx.mec_fw->data +
		 le32_to_cpu(mec_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(mec_hdr->header.ucode_size_bytes) / 4;

	/* MEC1 */
	WREG32(mmCP_MEC_ME1_UCODE_ADDR, 0);
	for (i = 0; i < fw_size; i++)
		WREG32(mmCP_MEC_ME1_UCODE_DATA, le32_to_cpup(fw_data+i));
	WREG32(mmCP_MEC_ME1_UCODE_ADDR, adev->gfx.mec_fw_version);

	/* Loading MEC2 firmware is only necessary if MEC2 should run different microcode than MEC1. */
	if (adev->gfx.mec2_fw) {
		const struct gfx_firmware_header_v1_0 *mec2_hdr;

		mec2_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.mec2_fw->data;
		amdgpu_ucode_print_gfx_hdr(&mec2_hdr->header);

		fw_data = (const __le32 *)
			(adev->gfx.mec2_fw->data +
			 le32_to_cpu(mec2_hdr->header.ucode_array_offset_bytes));
		fw_size = le32_to_cpu(mec2_hdr->header.ucode_size_bytes) / 4;

		WREG32(mmCP_MEC_ME2_UCODE_ADDR, 0);
		for (i = 0; i < fw_size; i++)
			WREG32(mmCP_MEC_ME2_UCODE_DATA, le32_to_cpup(fw_data+i));
		WREG32(mmCP_MEC_ME2_UCODE_ADDR, adev->gfx.mec2_fw_version);
	}

	return 0;
}

struct vi_mqd {
	uint32_t header;  /* ordinal0 */
	uint32_t compute_dispatch_initiator;  /* ordinal1 */
	uint32_t compute_dim_x;  /* ordinal2 */
	uint32_t compute_dim_y;  /* ordinal3 */
	uint32_t compute_dim_z;  /* ordinal4 */
	uint32_t compute_start_x;  /* ordinal5 */
	uint32_t compute_start_y;  /* ordinal6 */
	uint32_t compute_start_z;  /* ordinal7 */
	uint32_t compute_num_thread_x;  /* ordinal8 */
	uint32_t compute_num_thread_y;  /* ordinal9 */
	uint32_t compute_num_thread_z;  /* ordinal10 */
	uint32_t compute_pipelinestat_enable;  /* ordinal11 */
	uint32_t compute_perfcount_enable;  /* ordinal12 */
	uint32_t compute_pgm_lo;  /* ordinal13 */
	uint32_t compute_pgm_hi;  /* ordinal14 */
	uint32_t compute_tba_lo;  /* ordinal15 */
	uint32_t compute_tba_hi;  /* ordinal16 */
	uint32_t compute_tma_lo;  /* ordinal17 */
	uint32_t compute_tma_hi;  /* ordinal18 */
	uint32_t compute_pgm_rsrc1;  /* ordinal19 */
	uint32_t compute_pgm_rsrc2;  /* ordinal20 */
	uint32_t compute_vmid;  /* ordinal21 */
	uint32_t compute_resource_limits;  /* ordinal22 */
	uint32_t compute_static_thread_mgmt_se0;  /* ordinal23 */
	uint32_t compute_static_thread_mgmt_se1;  /* ordinal24 */
	uint32_t compute_tmpring_size;  /* ordinal25 */
	uint32_t compute_static_thread_mgmt_se2;  /* ordinal26 */
	uint32_t compute_static_thread_mgmt_se3;  /* ordinal27 */
	uint32_t compute_restart_x;  /* ordinal28 */
	uint32_t compute_restart_y;  /* ordinal29 */
	uint32_t compute_restart_z;  /* ordinal30 */
	uint32_t compute_thread_trace_enable;  /* ordinal31 */
	uint32_t compute_misc_reserved;  /* ordinal32 */
	uint32_t compute_dispatch_id;  /* ordinal33 */
	uint32_t compute_threadgroup_id;  /* ordinal34 */
	uint32_t compute_relaunch;  /* ordinal35 */
	uint32_t compute_wave_restore_addr_lo;  /* ordinal36 */
	uint32_t compute_wave_restore_addr_hi;  /* ordinal37 */
	uint32_t compute_wave_restore_control;  /* ordinal38 */
	uint32_t reserved9;  /* ordinal39 */
	uint32_t reserved10;  /* ordinal40 */
	uint32_t reserved11;  /* ordinal41 */
	uint32_t reserved12;  /* ordinal42 */
	uint32_t reserved13;  /* ordinal43 */
	uint32_t reserved14;  /* ordinal44 */
	uint32_t reserved15;  /* ordinal45 */
	uint32_t reserved16;  /* ordinal46 */
	uint32_t reserved17;  /* ordinal47 */
	uint32_t reserved18;  /* ordinal48 */
	uint32_t reserved19;  /* ordinal49 */
	uint32_t reserved20;  /* ordinal50 */
	uint32_t reserved21;  /* ordinal51 */
	uint32_t reserved22;  /* ordinal52 */
	uint32_t reserved23;  /* ordinal53 */
	uint32_t reserved24;  /* ordinal54 */
	uint32_t reserved25;  /* ordinal55 */
	uint32_t reserved26;  /* ordinal56 */
	uint32_t reserved27;  /* ordinal57 */
	uint32_t reserved28;  /* ordinal58 */
	uint32_t reserved29;  /* ordinal59 */
	uint32_t reserved30;  /* ordinal60 */
	uint32_t reserved31;  /* ordinal61 */
	uint32_t reserved32;  /* ordinal62 */
	uint32_t reserved33;  /* ordinal63 */
	uint32_t reserved34;  /* ordinal64 */
	uint32_t compute_user_data_0;  /* ordinal65 */
	uint32_t compute_user_data_1;  /* ordinal66 */
	uint32_t compute_user_data_2;  /* ordinal67 */
	uint32_t compute_user_data_3;  /* ordinal68 */
	uint32_t compute_user_data_4;  /* ordinal69 */
	uint32_t compute_user_data_5;  /* ordinal70 */
	uint32_t compute_user_data_6;  /* ordinal71 */
	uint32_t compute_user_data_7;  /* ordinal72 */
	uint32_t compute_user_data_8;  /* ordinal73 */
	uint32_t compute_user_data_9;  /* ordinal74 */
	uint32_t compute_user_data_10;  /* ordinal75 */
	uint32_t compute_user_data_11;  /* ordinal76 */
	uint32_t compute_user_data_12;  /* ordinal77 */
	uint32_t compute_user_data_13;  /* ordinal78 */
	uint32_t compute_user_data_14;  /* ordinal79 */
	uint32_t compute_user_data_15;  /* ordinal80 */
	uint32_t cp_compute_csinvoc_count_lo;  /* ordinal81 */
	uint32_t cp_compute_csinvoc_count_hi;  /* ordinal82 */
	uint32_t reserved35;  /* ordinal83 */
	uint32_t reserved36;  /* ordinal84 */
	uint32_t reserved37;  /* ordinal85 */
	uint32_t cp_mqd_query_time_lo;  /* ordinal86 */
	uint32_t cp_mqd_query_time_hi;  /* ordinal87 */
	uint32_t cp_mqd_connect_start_time_lo;  /* ordinal88 */
	uint32_t cp_mqd_connect_start_time_hi;  /* ordinal89 */
	uint32_t cp_mqd_connect_end_time_lo;  /* ordinal90 */
	uint32_t cp_mqd_connect_end_time_hi;  /* ordinal91 */
	uint32_t cp_mqd_connect_end_wf_count;  /* ordinal92 */
	uint32_t cp_mqd_connect_end_pq_rptr;  /* ordinal93 */
	uint32_t cp_mqd_connect_end_pq_wptr;  /* ordinal94 */
	uint32_t cp_mqd_connect_end_ib_rptr;  /* ordinal95 */
	uint32_t reserved38;  /* ordinal96 */
	uint32_t reserved39;  /* ordinal97 */
	uint32_t cp_mqd_save_start_time_lo;  /* ordinal98 */
	uint32_t cp_mqd_save_start_time_hi;  /* ordinal99 */
	uint32_t cp_mqd_save_end_time_lo;  /* ordinal100 */
	uint32_t cp_mqd_save_end_time_hi;  /* ordinal101 */
	uint32_t cp_mqd_restore_start_time_lo;  /* ordinal102 */
	uint32_t cp_mqd_restore_start_time_hi;  /* ordinal103 */
	uint32_t cp_mqd_restore_end_time_lo;  /* ordinal104 */
	uint32_t cp_mqd_restore_end_time_hi;  /* ordinal105 */
	uint32_t reserved40;  /* ordinal106 */
	uint32_t reserved41;  /* ordinal107 */
	uint32_t gds_cs_ctxsw_cnt0;  /* ordinal108 */
	uint32_t gds_cs_ctxsw_cnt1;  /* ordinal109 */
	uint32_t gds_cs_ctxsw_cnt2;  /* ordinal110 */
	uint32_t gds_cs_ctxsw_cnt3;  /* ordinal111 */
	uint32_t reserved42;  /* ordinal112 */
	uint32_t reserved43;  /* ordinal113 */
	uint32_t cp_pq_exe_status_lo;  /* ordinal114 */
	uint32_t cp_pq_exe_status_hi;  /* ordinal115 */
	uint32_t cp_packet_id_lo;  /* ordinal116 */
	uint32_t cp_packet_id_hi;  /* ordinal117 */
	uint32_t cp_packet_exe_status_lo;  /* ordinal118 */
	uint32_t cp_packet_exe_status_hi;  /* ordinal119 */
	uint32_t gds_save_base_addr_lo;  /* ordinal120 */
	uint32_t gds_save_base_addr_hi;  /* ordinal121 */
	uint32_t gds_save_mask_lo;  /* ordinal122 */
	uint32_t gds_save_mask_hi;  /* ordinal123 */
	uint32_t ctx_save_base_addr_lo;  /* ordinal124 */
	uint32_t ctx_save_base_addr_hi;  /* ordinal125 */
	uint32_t reserved44;  /* ordinal126 */
	uint32_t reserved45;  /* ordinal127 */
	uint32_t cp_mqd_base_addr_lo;  /* ordinal128 */
	uint32_t cp_mqd_base_addr_hi;  /* ordinal129 */
	uint32_t cp_hqd_active;  /* ordinal130 */
	uint32_t cp_hqd_vmid;  /* ordinal131 */
	uint32_t cp_hqd_persistent_state;  /* ordinal132 */
	uint32_t cp_hqd_pipe_priority;  /* ordinal133 */
	uint32_t cp_hqd_queue_priority;  /* ordinal134 */
	uint32_t cp_hqd_quantum;  /* ordinal135 */
	uint32_t cp_hqd_pq_base_lo;  /* ordinal136 */
	uint32_t cp_hqd_pq_base_hi;  /* ordinal137 */
	uint32_t cp_hqd_pq_rptr;  /* ordinal138 */
	uint32_t cp_hqd_pq_rptr_report_addr_lo;  /* ordinal139 */
	uint32_t cp_hqd_pq_rptr_report_addr_hi;  /* ordinal140 */
	uint32_t cp_hqd_pq_wptr_poll_addr;  /* ordinal141 */
	uint32_t cp_hqd_pq_wptr_poll_addr_hi;  /* ordinal142 */
	uint32_t cp_hqd_pq_doorbell_control;  /* ordinal143 */
	uint32_t cp_hqd_pq_wptr;  /* ordinal144 */
	uint32_t cp_hqd_pq_control;  /* ordinal145 */
	uint32_t cp_hqd_ib_base_addr_lo;  /* ordinal146 */
	uint32_t cp_hqd_ib_base_addr_hi;  /* ordinal147 */
	uint32_t cp_hqd_ib_rptr;  /* ordinal148 */
	uint32_t cp_hqd_ib_control;  /* ordinal149 */
	uint32_t cp_hqd_iq_timer;  /* ordinal150 */
	uint32_t cp_hqd_iq_rptr;  /* ordinal151 */
	uint32_t cp_hqd_dequeue_request;  /* ordinal152 */
	uint32_t cp_hqd_dma_offload;  /* ordinal153 */
	uint32_t cp_hqd_sema_cmd;  /* ordinal154 */
	uint32_t cp_hqd_msg_type;  /* ordinal155 */
	uint32_t cp_hqd_atomic0_preop_lo;  /* ordinal156 */
	uint32_t cp_hqd_atomic0_preop_hi;  /* ordinal157 */
	uint32_t cp_hqd_atomic1_preop_lo;  /* ordinal158 */
	uint32_t cp_hqd_atomic1_preop_hi;  /* ordinal159 */
	uint32_t cp_hqd_hq_status0;  /* ordinal160 */
	uint32_t cp_hqd_hq_control0;  /* ordinal161 */
	uint32_t cp_mqd_control;  /* ordinal162 */
	uint32_t cp_hqd_hq_status1;  /* ordinal163 */
	uint32_t cp_hqd_hq_control1;  /* ordinal164 */
	uint32_t cp_hqd_eop_base_addr_lo;  /* ordinal165 */
	uint32_t cp_hqd_eop_base_addr_hi;  /* ordinal166 */
	uint32_t cp_hqd_eop_control;  /* ordinal167 */
	uint32_t cp_hqd_eop_rptr;  /* ordinal168 */
	uint32_t cp_hqd_eop_wptr;  /* ordinal169 */
	uint32_t cp_hqd_eop_done_events;  /* ordinal170 */
	uint32_t cp_hqd_ctx_save_base_addr_lo;  /* ordinal171 */
	uint32_t cp_hqd_ctx_save_base_addr_hi;  /* ordinal172 */
	uint32_t cp_hqd_ctx_save_control;  /* ordinal173 */
	uint32_t cp_hqd_cntl_stack_offset;  /* ordinal174 */
	uint32_t cp_hqd_cntl_stack_size;  /* ordinal175 */
	uint32_t cp_hqd_wg_state_offset;  /* ordinal176 */
	uint32_t cp_hqd_ctx_save_size;  /* ordinal177 */
	uint32_t cp_hqd_gds_resource_state;  /* ordinal178 */
	uint32_t cp_hqd_error;  /* ordinal179 */
	uint32_t cp_hqd_eop_wptr_mem;  /* ordinal180 */
	uint32_t cp_hqd_eop_dones;  /* ordinal181 */
	uint32_t reserved46;  /* ordinal182 */
	uint32_t reserved47;  /* ordinal183 */
	uint32_t reserved48;  /* ordinal184 */
	uint32_t reserved49;  /* ordinal185 */
	uint32_t reserved50;  /* ordinal186 */
	uint32_t reserved51;  /* ordinal187 */
	uint32_t reserved52;  /* ordinal188 */
	uint32_t reserved53;  /* ordinal189 */
	uint32_t reserved54;  /* ordinal190 */
	uint32_t reserved55;  /* ordinal191 */
	uint32_t iqtimer_pkt_header;  /* ordinal192 */
	uint32_t iqtimer_pkt_dw0;  /* ordinal193 */
	uint32_t iqtimer_pkt_dw1;  /* ordinal194 */
	uint32_t iqtimer_pkt_dw2;  /* ordinal195 */
	uint32_t iqtimer_pkt_dw3;  /* ordinal196 */
	uint32_t iqtimer_pkt_dw4;  /* ordinal197 */
	uint32_t iqtimer_pkt_dw5;  /* ordinal198 */
	uint32_t iqtimer_pkt_dw6;  /* ordinal199 */
	uint32_t iqtimer_pkt_dw7;  /* ordinal200 */
	uint32_t iqtimer_pkt_dw8;  /* ordinal201 */
	uint32_t iqtimer_pkt_dw9;  /* ordinal202 */
	uint32_t iqtimer_pkt_dw10;  /* ordinal203 */
	uint32_t iqtimer_pkt_dw11;  /* ordinal204 */
	uint32_t iqtimer_pkt_dw12;  /* ordinal205 */
	uint32_t iqtimer_pkt_dw13;  /* ordinal206 */
	uint32_t iqtimer_pkt_dw14;  /* ordinal207 */
	uint32_t iqtimer_pkt_dw15;  /* ordinal208 */
	uint32_t iqtimer_pkt_dw16;  /* ordinal209 */
	uint32_t iqtimer_pkt_dw17;  /* ordinal210 */
	uint32_t iqtimer_pkt_dw18;  /* ordinal211 */
	uint32_t iqtimer_pkt_dw19;  /* ordinal212 */
	uint32_t iqtimer_pkt_dw20;  /* ordinal213 */
	uint32_t iqtimer_pkt_dw21;  /* ordinal214 */
	uint32_t iqtimer_pkt_dw22;  /* ordinal215 */
	uint32_t iqtimer_pkt_dw23;  /* ordinal216 */
	uint32_t iqtimer_pkt_dw24;  /* ordinal217 */
	uint32_t iqtimer_pkt_dw25;  /* ordinal218 */
	uint32_t iqtimer_pkt_dw26;  /* ordinal219 */
	uint32_t iqtimer_pkt_dw27;  /* ordinal220 */
	uint32_t iqtimer_pkt_dw28;  /* ordinal221 */
	uint32_t iqtimer_pkt_dw29;  /* ordinal222 */
	uint32_t iqtimer_pkt_dw30;  /* ordinal223 */
	uint32_t iqtimer_pkt_dw31;  /* ordinal224 */
	uint32_t reserved56;  /* ordinal225 */
	uint32_t reserved57;  /* ordinal226 */
	uint32_t reserved58;  /* ordinal227 */
	uint32_t set_resources_header;  /* ordinal228 */
	uint32_t set_resources_dw1;  /* ordinal229 */
	uint32_t set_resources_dw2;  /* ordinal230 */
	uint32_t set_resources_dw3;  /* ordinal231 */
	uint32_t set_resources_dw4;  /* ordinal232 */
	uint32_t set_resources_dw5;  /* ordinal233 */
	uint32_t set_resources_dw6;  /* ordinal234 */
	uint32_t set_resources_dw7;  /* ordinal235 */
	uint32_t reserved59;  /* ordinal236 */
	uint32_t reserved60;  /* ordinal237 */
	uint32_t reserved61;  /* ordinal238 */
	uint32_t reserved62;  /* ordinal239 */
	uint32_t reserved63;  /* ordinal240 */
	uint32_t reserved64;  /* ordinal241 */
	uint32_t reserved65;  /* ordinal242 */
	uint32_t reserved66;  /* ordinal243 */
	uint32_t reserved67;  /* ordinal244 */
	uint32_t reserved68;  /* ordinal245 */
	uint32_t reserved69;  /* ordinal246 */
	uint32_t reserved70;  /* ordinal247 */
	uint32_t reserved71;  /* ordinal248 */
	uint32_t reserved72;  /* ordinal249 */
	uint32_t reserved73;  /* ordinal250 */
	uint32_t reserved74;  /* ordinal251 */
	uint32_t reserved75;  /* ordinal252 */
	uint32_t reserved76;  /* ordinal253 */
	uint32_t reserved77;  /* ordinal254 */
	uint32_t reserved78;  /* ordinal255 */

	uint32_t reserved_t[256]; /* Reserve 256 dword buffer used by ucode */
};

static void gfx_v8_0_cp_compute_fini(struct amdgpu_device *adev)
{
	int i, r;

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		struct amdgpu_ring *ring = &adev->gfx.compute_ring[i];

		if (ring->mqd_obj) {
			r = amdgpu_bo_reserve(ring->mqd_obj, false);
			if (unlikely(r != 0))
				dev_warn(adev->dev, "(%d) reserve MQD bo failed\n", r);

			amdgpu_bo_unpin(ring->mqd_obj);
			amdgpu_bo_unreserve(ring->mqd_obj);

			amdgpu_bo_unref(&ring->mqd_obj);
			ring->mqd_obj = NULL;
		}
	}
}

static int gfx_v8_0_cp_compute_resume(struct amdgpu_device *adev)
{
	int r, i, j;
	u32 tmp;
	bool use_doorbell = true;
	u64 hqd_gpu_addr;
	u64 mqd_gpu_addr;
	u64 eop_gpu_addr;
	u64 wb_gpu_addr;
	u32 *buf;
	struct vi_mqd *mqd;

	/* init the pipes */
	mutex_lock(&adev->srbm_mutex);
	for (i = 0; i < (adev->gfx.mec.num_pipe * adev->gfx.mec.num_mec); i++) {
		int me = (i < 4) ? 1 : 2;
		int pipe = (i < 4) ? i : (i - 4);

		eop_gpu_addr = adev->gfx.mec.hpd_eop_gpu_addr + (i * MEC_HPD_SIZE);
		eop_gpu_addr >>= 8;

		vi_srbm_select(adev, me, pipe, 0, 0);

		/* write the EOP addr */
		WREG32(mmCP_HQD_EOP_BASE_ADDR, eop_gpu_addr);
		WREG32(mmCP_HQD_EOP_BASE_ADDR_HI, upper_32_bits(eop_gpu_addr));

		/* set the VMID assigned */
		WREG32(mmCP_HQD_VMID, 0);

		/* set the EOP size, register value is 2^(EOP_SIZE+1) dwords */
		tmp = RREG32(mmCP_HQD_EOP_CONTROL);
		tmp = REG_SET_FIELD(tmp, CP_HQD_EOP_CONTROL, EOP_SIZE,
				    (order_base_2(MEC_HPD_SIZE / 4) - 1));
		WREG32(mmCP_HQD_EOP_CONTROL, tmp);
	}
	vi_srbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);

	/* init the queues.  Just two for now. */
	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		struct amdgpu_ring *ring = &adev->gfx.compute_ring[i];

		if (ring->mqd_obj == NULL) {
			r = amdgpu_bo_create(adev,
					     sizeof(struct vi_mqd),
					     PAGE_SIZE, true,
					     AMDGPU_GEM_DOMAIN_GTT, 0, NULL,
					     NULL, &ring->mqd_obj);
			if (r) {
				dev_warn(adev->dev, "(%d) create MQD bo failed\n", r);
				return r;
			}
		}

		r = amdgpu_bo_reserve(ring->mqd_obj, false);
		if (unlikely(r != 0)) {
			gfx_v8_0_cp_compute_fini(adev);
			return r;
		}
		r = amdgpu_bo_pin(ring->mqd_obj, AMDGPU_GEM_DOMAIN_GTT,
				  &mqd_gpu_addr);
		if (r) {
			dev_warn(adev->dev, "(%d) pin MQD bo failed\n", r);
			gfx_v8_0_cp_compute_fini(adev);
			return r;
		}
		r = amdgpu_bo_kmap(ring->mqd_obj, (void **)&buf);
		if (r) {
			dev_warn(adev->dev, "(%d) map MQD bo failed\n", r);
			gfx_v8_0_cp_compute_fini(adev);
			return r;
		}

		/* init the mqd struct */
		memset(buf, 0, sizeof(struct vi_mqd));

		mqd = (struct vi_mqd *)buf;
		mqd->header = 0xC0310800;
		mqd->compute_pipelinestat_enable = 0x00000001;
		mqd->compute_static_thread_mgmt_se0 = 0xffffffff;
		mqd->compute_static_thread_mgmt_se1 = 0xffffffff;
		mqd->compute_static_thread_mgmt_se2 = 0xffffffff;
		mqd->compute_static_thread_mgmt_se3 = 0xffffffff;
		mqd->compute_misc_reserved = 0x00000003;

		mutex_lock(&adev->srbm_mutex);
		vi_srbm_select(adev, ring->me,
			       ring->pipe,
			       ring->queue, 0);

		/* disable wptr polling */
		tmp = RREG32(mmCP_PQ_WPTR_POLL_CNTL);
		tmp = REG_SET_FIELD(tmp, CP_PQ_WPTR_POLL_CNTL, EN, 0);
		WREG32(mmCP_PQ_WPTR_POLL_CNTL, tmp);

		mqd->cp_hqd_eop_base_addr_lo =
			RREG32(mmCP_HQD_EOP_BASE_ADDR);
		mqd->cp_hqd_eop_base_addr_hi =
			RREG32(mmCP_HQD_EOP_BASE_ADDR_HI);

		/* enable doorbell? */
		tmp = RREG32(mmCP_HQD_PQ_DOORBELL_CONTROL);
		if (use_doorbell) {
			tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL, DOORBELL_EN, 1);
		} else {
			tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL, DOORBELL_EN, 0);
		}
		WREG32(mmCP_HQD_PQ_DOORBELL_CONTROL, tmp);
		mqd->cp_hqd_pq_doorbell_control = tmp;

		/* disable the queue if it's active */
		mqd->cp_hqd_dequeue_request = 0;
		mqd->cp_hqd_pq_rptr = 0;
		mqd->cp_hqd_pq_wptr= 0;
		if (RREG32(mmCP_HQD_ACTIVE) & 1) {
			WREG32(mmCP_HQD_DEQUEUE_REQUEST, 1);
			for (j = 0; j < adev->usec_timeout; j++) {
				if (!(RREG32(mmCP_HQD_ACTIVE) & 1))
					break;
				udelay(1);
			}
			WREG32(mmCP_HQD_DEQUEUE_REQUEST, mqd->cp_hqd_dequeue_request);
			WREG32(mmCP_HQD_PQ_RPTR, mqd->cp_hqd_pq_rptr);
			WREG32(mmCP_HQD_PQ_WPTR, mqd->cp_hqd_pq_wptr);
		}

		/* set the pointer to the MQD */
		mqd->cp_mqd_base_addr_lo = mqd_gpu_addr & 0xfffffffc;
		mqd->cp_mqd_base_addr_hi = upper_32_bits(mqd_gpu_addr);
		WREG32(mmCP_MQD_BASE_ADDR, mqd->cp_mqd_base_addr_lo);
		WREG32(mmCP_MQD_BASE_ADDR_HI, mqd->cp_mqd_base_addr_hi);

		/* set MQD vmid to 0 */
		tmp = RREG32(mmCP_MQD_CONTROL);
		tmp = REG_SET_FIELD(tmp, CP_MQD_CONTROL, VMID, 0);
		WREG32(mmCP_MQD_CONTROL, tmp);
		mqd->cp_mqd_control = tmp;

		/* set the pointer to the HQD, this is similar CP_RB0_BASE/_HI */
		hqd_gpu_addr = ring->gpu_addr >> 8;
		mqd->cp_hqd_pq_base_lo = hqd_gpu_addr;
		mqd->cp_hqd_pq_base_hi = upper_32_bits(hqd_gpu_addr);
		WREG32(mmCP_HQD_PQ_BASE, mqd->cp_hqd_pq_base_lo);
		WREG32(mmCP_HQD_PQ_BASE_HI, mqd->cp_hqd_pq_base_hi);

		/* set up the HQD, this is similar to CP_RB0_CNTL */
		tmp = RREG32(mmCP_HQD_PQ_CONTROL);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, QUEUE_SIZE,
				    (order_base_2(ring->ring_size / 4) - 1));
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, RPTR_BLOCK_SIZE,
			       ((order_base_2(AMDGPU_GPU_PAGE_SIZE / 4) - 1) << 8));
#ifdef __BIG_ENDIAN
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, ENDIAN_SWAP, 1);
#endif
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, UNORD_DISPATCH, 0);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, ROQ_PQ_IB_FLIP, 0);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, PRIV_STATE, 1);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_CONTROL, KMD_QUEUE, 1);
		WREG32(mmCP_HQD_PQ_CONTROL, tmp);
		mqd->cp_hqd_pq_control = tmp;

		/* set the wb address wether it's enabled or not */
		wb_gpu_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);
		mqd->cp_hqd_pq_rptr_report_addr_lo = wb_gpu_addr & 0xfffffffc;
		mqd->cp_hqd_pq_rptr_report_addr_hi =
			upper_32_bits(wb_gpu_addr) & 0xffff;
		WREG32(mmCP_HQD_PQ_RPTR_REPORT_ADDR,
		       mqd->cp_hqd_pq_rptr_report_addr_lo);
		WREG32(mmCP_HQD_PQ_RPTR_REPORT_ADDR_HI,
		       mqd->cp_hqd_pq_rptr_report_addr_hi);

		/* only used if CP_PQ_WPTR_POLL_CNTL.CP_PQ_WPTR_POLL_CNTL__EN_MASK=1 */
		wb_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
		mqd->cp_hqd_pq_wptr_poll_addr = wb_gpu_addr & 0xfffffffc;
		mqd->cp_hqd_pq_wptr_poll_addr_hi = upper_32_bits(wb_gpu_addr) & 0xffff;
		WREG32(mmCP_HQD_PQ_WPTR_POLL_ADDR, mqd->cp_hqd_pq_wptr_poll_addr);
		WREG32(mmCP_HQD_PQ_WPTR_POLL_ADDR_HI,
		       mqd->cp_hqd_pq_wptr_poll_addr_hi);

		/* enable the doorbell if requested */
		if (use_doorbell) {
			if ((adev->asic_type == CHIP_CARRIZO) ||
			    (adev->asic_type == CHIP_FIJI) ||
			    (adev->asic_type == CHIP_STONEY) ||
			    (adev->asic_type == CHIP_POLARIS11) ||
			    (adev->asic_type == CHIP_POLARIS10)) {
				WREG32(mmCP_MEC_DOORBELL_RANGE_LOWER,
				       AMDGPU_DOORBELL_KIQ << 2);
				WREG32(mmCP_MEC_DOORBELL_RANGE_UPPER,
				       AMDGPU_DOORBELL_MEC_RING7 << 2);
			}
			tmp = RREG32(mmCP_HQD_PQ_DOORBELL_CONTROL);
			tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
					    DOORBELL_OFFSET, ring->doorbell_index);
			tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL, DOORBELL_EN, 1);
			tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL, DOORBELL_SOURCE, 0);
			tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL, DOORBELL_HIT, 0);
			mqd->cp_hqd_pq_doorbell_control = tmp;

		} else {
			mqd->cp_hqd_pq_doorbell_control = 0;
		}
		WREG32(mmCP_HQD_PQ_DOORBELL_CONTROL,
		       mqd->cp_hqd_pq_doorbell_control);

		/* reset read and write pointers, similar to CP_RB0_WPTR/_RPTR */
		ring->wptr = 0;
		mqd->cp_hqd_pq_wptr = ring->wptr;
		WREG32(mmCP_HQD_PQ_WPTR, mqd->cp_hqd_pq_wptr);
		mqd->cp_hqd_pq_rptr = RREG32(mmCP_HQD_PQ_RPTR);

		/* set the vmid for the queue */
		mqd->cp_hqd_vmid = 0;
		WREG32(mmCP_HQD_VMID, mqd->cp_hqd_vmid);

		tmp = RREG32(mmCP_HQD_PERSISTENT_STATE);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PERSISTENT_STATE, PRELOAD_SIZE, 0x53);
		WREG32(mmCP_HQD_PERSISTENT_STATE, tmp);
		mqd->cp_hqd_persistent_state = tmp;
		if (adev->asic_type == CHIP_STONEY ||
			adev->asic_type == CHIP_POLARIS11 ||
			adev->asic_type == CHIP_POLARIS10) {
			tmp = RREG32(mmCP_ME1_PIPE3_INT_CNTL);
			tmp = REG_SET_FIELD(tmp, CP_ME1_PIPE3_INT_CNTL, GENERIC2_INT_ENABLE, 1);
			WREG32(mmCP_ME1_PIPE3_INT_CNTL, tmp);
		}

		/* activate the queue */
		mqd->cp_hqd_active = 1;
		WREG32(mmCP_HQD_ACTIVE, mqd->cp_hqd_active);

		vi_srbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);

		amdgpu_bo_kunmap(ring->mqd_obj);
		amdgpu_bo_unreserve(ring->mqd_obj);
	}

	if (use_doorbell) {
		tmp = RREG32(mmCP_PQ_STATUS);
		tmp = REG_SET_FIELD(tmp, CP_PQ_STATUS, DOORBELL_ENABLE, 1);
		WREG32(mmCP_PQ_STATUS, tmp);
	}

	gfx_v8_0_cp_compute_enable(adev, true);

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		struct amdgpu_ring *ring = &adev->gfx.compute_ring[i];

		ring->ready = true;
		r = amdgpu_ring_test_ring(ring);
		if (r)
			ring->ready = false;
	}

	return 0;
}

static int gfx_v8_0_cp_resume(struct amdgpu_device *adev)
{
	int r;

	if (!(adev->flags & AMD_IS_APU))
		gfx_v8_0_enable_gui_idle_interrupt(adev, false);

	if (!adev->pp_enabled) {
		if (!adev->firmware.smu_load) {
			/* legacy firmware loading */
			r = gfx_v8_0_cp_gfx_load_microcode(adev);
			if (r)
				return r;

			r = gfx_v8_0_cp_compute_load_microcode(adev);
			if (r)
				return r;
		} else {
			r = adev->smu.smumgr_funcs->check_fw_load_finish(adev,
							AMDGPU_UCODE_ID_CP_CE);
			if (r)
				return -EINVAL;

			r = adev->smu.smumgr_funcs->check_fw_load_finish(adev,
							AMDGPU_UCODE_ID_CP_PFP);
			if (r)
				return -EINVAL;

			r = adev->smu.smumgr_funcs->check_fw_load_finish(adev,
							AMDGPU_UCODE_ID_CP_ME);
			if (r)
				return -EINVAL;

			if (adev->asic_type == CHIP_TOPAZ) {
				r = gfx_v8_0_cp_compute_load_microcode(adev);
				if (r)
					return r;
			} else {
				r = adev->smu.smumgr_funcs->check_fw_load_finish(adev,
										 AMDGPU_UCODE_ID_CP_MEC1);
				if (r)
					return -EINVAL;
			}
		}
	}

	r = gfx_v8_0_cp_gfx_resume(adev);
	if (r)
		return r;

	r = gfx_v8_0_cp_compute_resume(adev);
	if (r)
		return r;

	gfx_v8_0_enable_gui_idle_interrupt(adev, true);

	return 0;
}

static void gfx_v8_0_cp_enable(struct amdgpu_device *adev, bool enable)
{
	gfx_v8_0_cp_gfx_enable(adev, enable);
	gfx_v8_0_cp_compute_enable(adev, enable);
}

static int gfx_v8_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gfx_v8_0_init_golden_registers(adev);

	gfx_v8_0_gpu_init(adev);

	r = gfx_v8_0_rlc_resume(adev);
	if (r)
		return r;

	r = gfx_v8_0_cp_resume(adev);
	if (r)
		return r;

	return r;
}

static int gfx_v8_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_irq_put(adev, &adev->gfx.priv_reg_irq, 0);
	amdgpu_irq_put(adev, &adev->gfx.priv_inst_irq, 0);
	gfx_v8_0_cp_enable(adev, false);
	gfx_v8_0_rlc_stop(adev);
	gfx_v8_0_cp_compute_fini(adev);

	amdgpu_set_powergating_state(adev,
			AMD_IP_BLOCK_TYPE_GFX, AMD_PG_STATE_UNGATE);

	return 0;
}

static int gfx_v8_0_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return gfx_v8_0_hw_fini(adev);
}

static int gfx_v8_0_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return gfx_v8_0_hw_init(adev);
}

static bool gfx_v8_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (REG_GET_FIELD(RREG32(mmGRBM_STATUS), GRBM_STATUS, GUI_ACTIVE))
		return false;
	else
		return true;
}

static int gfx_v8_0_wait_for_idle(void *handle)
{
	unsigned i;
	u32 tmp;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		/* read MC_STATUS */
		tmp = RREG32(mmGRBM_STATUS) & GRBM_STATUS__GUI_ACTIVE_MASK;

		if (!REG_GET_FIELD(tmp, GRBM_STATUS, GUI_ACTIVE))
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int gfx_v8_0_soft_reset(void *handle)
{
	u32 grbm_soft_reset = 0, srbm_soft_reset = 0;
	u32 tmp;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* GRBM_STATUS */
	tmp = RREG32(mmGRBM_STATUS);
	if (tmp & (GRBM_STATUS__PA_BUSY_MASK | GRBM_STATUS__SC_BUSY_MASK |
		   GRBM_STATUS__BCI_BUSY_MASK | GRBM_STATUS__SX_BUSY_MASK |
		   GRBM_STATUS__TA_BUSY_MASK | GRBM_STATUS__VGT_BUSY_MASK |
		   GRBM_STATUS__DB_BUSY_MASK | GRBM_STATUS__CB_BUSY_MASK |
		   GRBM_STATUS__GDS_BUSY_MASK | GRBM_STATUS__SPI_BUSY_MASK |
		   GRBM_STATUS__IA_BUSY_MASK | GRBM_STATUS__IA_BUSY_NO_DMA_MASK)) {
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_CP, 1);
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_GFX, 1);
	}

	if (tmp & (GRBM_STATUS__CP_BUSY_MASK | GRBM_STATUS__CP_COHERENCY_BUSY_MASK)) {
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_CP, 1);
		srbm_soft_reset = REG_SET_FIELD(srbm_soft_reset,
						SRBM_SOFT_RESET, SOFT_RESET_GRBM, 1);
	}

	/* GRBM_STATUS2 */
	tmp = RREG32(mmGRBM_STATUS2);
	if (REG_GET_FIELD(tmp, GRBM_STATUS2, RLC_BUSY))
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_RLC, 1);

	/* SRBM_STATUS */
	tmp = RREG32(mmSRBM_STATUS);
	if (REG_GET_FIELD(tmp, SRBM_STATUS, GRBM_RQ_PENDING))
		srbm_soft_reset = REG_SET_FIELD(srbm_soft_reset,
						SRBM_SOFT_RESET, SOFT_RESET_GRBM, 1);

	if (grbm_soft_reset || srbm_soft_reset) {
		/* stop the rlc */
		gfx_v8_0_rlc_stop(adev);

		/* Disable GFX parsing/prefetching */
		gfx_v8_0_cp_gfx_enable(adev, false);

		/* Disable MEC parsing/prefetching */
		gfx_v8_0_cp_compute_enable(adev, false);

		if (grbm_soft_reset || srbm_soft_reset) {
			tmp = RREG32(mmGMCON_DEBUG);
			tmp = REG_SET_FIELD(tmp,
					    GMCON_DEBUG, GFX_STALL, 1);
			tmp = REG_SET_FIELD(tmp,
					    GMCON_DEBUG, GFX_CLEAR, 1);
			WREG32(mmGMCON_DEBUG, tmp);

			udelay(50);
		}

		if (grbm_soft_reset) {
			tmp = RREG32(mmGRBM_SOFT_RESET);
			tmp |= grbm_soft_reset;
			dev_info(adev->dev, "GRBM_SOFT_RESET=0x%08X\n", tmp);
			WREG32(mmGRBM_SOFT_RESET, tmp);
			tmp = RREG32(mmGRBM_SOFT_RESET);

			udelay(50);

			tmp &= ~grbm_soft_reset;
			WREG32(mmGRBM_SOFT_RESET, tmp);
			tmp = RREG32(mmGRBM_SOFT_RESET);
		}

		if (srbm_soft_reset) {
			tmp = RREG32(mmSRBM_SOFT_RESET);
			tmp |= srbm_soft_reset;
			dev_info(adev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
			WREG32(mmSRBM_SOFT_RESET, tmp);
			tmp = RREG32(mmSRBM_SOFT_RESET);

			udelay(50);

			tmp &= ~srbm_soft_reset;
			WREG32(mmSRBM_SOFT_RESET, tmp);
			tmp = RREG32(mmSRBM_SOFT_RESET);
		}

		if (grbm_soft_reset || srbm_soft_reset) {
			tmp = RREG32(mmGMCON_DEBUG);
			tmp = REG_SET_FIELD(tmp,
					    GMCON_DEBUG, GFX_STALL, 0);
			tmp = REG_SET_FIELD(tmp,
					    GMCON_DEBUG, GFX_CLEAR, 0);
			WREG32(mmGMCON_DEBUG, tmp);
		}

		/* Wait a little for things to settle down */
		udelay(50);
	}
	return 0;
}

/**
 * gfx_v8_0_get_gpu_clock_counter - return GPU clock counter snapshot
 *
 * @adev: amdgpu_device pointer
 *
 * Fetches a GPU clock counter snapshot.
 * Returns the 64 bit clock counter snapshot.
 */
uint64_t gfx_v8_0_get_gpu_clock_counter(struct amdgpu_device *adev)
{
	uint64_t clock;

	mutex_lock(&adev->gfx.gpu_clock_mutex);
	WREG32(mmRLC_CAPTURE_GPU_CLOCK_COUNT, 1);
	clock = (uint64_t)RREG32(mmRLC_GPU_CLOCK_COUNT_LSB) |
		((uint64_t)RREG32(mmRLC_GPU_CLOCK_COUNT_MSB) << 32ULL);
	mutex_unlock(&adev->gfx.gpu_clock_mutex);
	return clock;
}

static void gfx_v8_0_ring_emit_gds_switch(struct amdgpu_ring *ring,
					  uint32_t vmid,
					  uint32_t gds_base, uint32_t gds_size,
					  uint32_t gws_base, uint32_t gws_size,
					  uint32_t oa_base, uint32_t oa_size)
{
	gds_base = gds_base >> AMDGPU_GDS_SHIFT;
	gds_size = gds_size >> AMDGPU_GDS_SHIFT;

	gws_base = gws_base >> AMDGPU_GWS_SHIFT;
	gws_size = gws_size >> AMDGPU_GWS_SHIFT;

	oa_base = oa_base >> AMDGPU_OA_SHIFT;
	oa_size = oa_size >> AMDGPU_OA_SHIFT;

	/* GDS Base */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				WRITE_DATA_DST_SEL(0)));
	amdgpu_ring_write(ring, amdgpu_gds_reg_offset[vmid].mem_base);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, gds_base);

	/* GDS Size */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				WRITE_DATA_DST_SEL(0)));
	amdgpu_ring_write(ring, amdgpu_gds_reg_offset[vmid].mem_size);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, gds_size);

	/* GWS */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				WRITE_DATA_DST_SEL(0)));
	amdgpu_ring_write(ring, amdgpu_gds_reg_offset[vmid].gws);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, gws_size << GDS_GWS_VMID0__SIZE__SHIFT | gws_base);

	/* OA */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				WRITE_DATA_DST_SEL(0)));
	amdgpu_ring_write(ring, amdgpu_gds_reg_offset[vmid].oa);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, (1 << (oa_size + oa_base)) - (1 << oa_base));
}

static int gfx_v8_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->gfx.num_gfx_rings = GFX8_NUM_GFX_RINGS;
	adev->gfx.num_compute_rings = GFX8_NUM_COMPUTE_RINGS;
	gfx_v8_0_set_ring_funcs(adev);
	gfx_v8_0_set_irq_funcs(adev);
	gfx_v8_0_set_gds_init(adev);
	gfx_v8_0_set_rlc_funcs(adev);

	return 0;
}

static int gfx_v8_0_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = amdgpu_irq_get(adev, &adev->gfx.priv_reg_irq, 0);
	if (r)
		return r;

	r = amdgpu_irq_get(adev, &adev->gfx.priv_inst_irq, 0);
	if (r)
		return r;

	/* requires IBs so do in late init after IB pool is initialized */
	r = gfx_v8_0_do_edc_gpr_workarounds(adev);
	if (r)
		return r;

	amdgpu_set_powergating_state(adev,
			AMD_IP_BLOCK_TYPE_GFX, AMD_PG_STATE_GATE);

	return 0;
}

static void polaris11_enable_gfx_static_mg_power_gating(struct amdgpu_device *adev,
		bool enable)
{
	uint32_t data, temp;

	/* Send msg to SMU via Powerplay */
	amdgpu_set_powergating_state(adev,
			AMD_IP_BLOCK_TYPE_SMC,
			enable ? AMD_PG_STATE_GATE : AMD_PG_STATE_UNGATE);

	if (enable) {
		/* Enable static MGPG */
		temp = data = RREG32(mmRLC_PG_CNTL);
		data |= RLC_PG_CNTL__STATIC_PER_CU_PG_ENABLE_MASK;

		if (temp != data)
			WREG32(mmRLC_PG_CNTL, data);
	} else {
		temp = data = RREG32(mmRLC_PG_CNTL);
		data &= ~RLC_PG_CNTL__STATIC_PER_CU_PG_ENABLE_MASK;

		if (temp != data)
			WREG32(mmRLC_PG_CNTL, data);
	}
}

static void polaris11_enable_gfx_dynamic_mg_power_gating(struct amdgpu_device *adev,
		bool enable)
{
	uint32_t data, temp;

	if (enable) {
		/* Enable dynamic MGPG */
		temp = data = RREG32(mmRLC_PG_CNTL);
		data |= RLC_PG_CNTL__DYN_PER_CU_PG_ENABLE_MASK;

		if (temp != data)
			WREG32(mmRLC_PG_CNTL, data);
	} else {
		temp = data = RREG32(mmRLC_PG_CNTL);
		data &= ~RLC_PG_CNTL__DYN_PER_CU_PG_ENABLE_MASK;

		if (temp != data)
			WREG32(mmRLC_PG_CNTL, data);
	}
}

static void polaris11_enable_gfx_quick_mg_power_gating(struct amdgpu_device *adev,
		bool enable)
{
	uint32_t data, temp;

	if (enable) {
		/* Enable quick PG */
		temp = data = RREG32(mmRLC_PG_CNTL);
		data |= 0x100000;

		if (temp != data)
			WREG32(mmRLC_PG_CNTL, data);
	} else {
		temp = data = RREG32(mmRLC_PG_CNTL);
		data &= ~0x100000;

		if (temp != data)
			WREG32(mmRLC_PG_CNTL, data);
	}
}

static int gfx_v8_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!(adev->pg_flags & AMD_PG_SUPPORT_GFX_PG))
		return 0;

	switch (adev->asic_type) {
	case CHIP_POLARIS11:
		if (adev->pg_flags & AMD_PG_SUPPORT_GFX_SMG)
			polaris11_enable_gfx_static_mg_power_gating(adev,
					state == AMD_PG_STATE_GATE ? true : false);
		else if (adev->pg_flags & AMD_PG_SUPPORT_GFX_DMG)
			polaris11_enable_gfx_dynamic_mg_power_gating(adev,
					state == AMD_PG_STATE_GATE ? true : false);
		else
			polaris11_enable_gfx_quick_mg_power_gating(adev,
					state == AMD_PG_STATE_GATE ? true : false);
		break;
	default:
		break;
	}

	return 0;
}

static void gfx_v8_0_send_serdes_cmd(struct amdgpu_device *adev,
				     uint32_t reg_addr, uint32_t cmd)
{
	uint32_t data;

	gfx_v8_0_select_se_sh(adev, 0xffffffff, 0xffffffff);

	WREG32(mmRLC_SERDES_WR_CU_MASTER_MASK, 0xffffffff);
	WREG32(mmRLC_SERDES_WR_NONCU_MASTER_MASK, 0xffffffff);

	data = RREG32(mmRLC_SERDES_WR_CTRL);
	if (adev->asic_type == CHIP_STONEY)
			data &= ~(RLC_SERDES_WR_CTRL__WRITE_COMMAND_MASK |
			RLC_SERDES_WR_CTRL__READ_COMMAND_MASK |
			RLC_SERDES_WR_CTRL__P1_SELECT_MASK |
			RLC_SERDES_WR_CTRL__P2_SELECT_MASK |
			RLC_SERDES_WR_CTRL__RDDATA_RESET_MASK |
			RLC_SERDES_WR_CTRL__POWER_DOWN_MASK |
			RLC_SERDES_WR_CTRL__POWER_UP_MASK |
			RLC_SERDES_WR_CTRL__SHORT_FORMAT_MASK |
			RLC_SERDES_WR_CTRL__SRBM_OVERRIDE_MASK);
	else
		data &= ~(RLC_SERDES_WR_CTRL__WRITE_COMMAND_MASK |
			  RLC_SERDES_WR_CTRL__READ_COMMAND_MASK |
			  RLC_SERDES_WR_CTRL__P1_SELECT_MASK |
			  RLC_SERDES_WR_CTRL__P2_SELECT_MASK |
			  RLC_SERDES_WR_CTRL__RDDATA_RESET_MASK |
			  RLC_SERDES_WR_CTRL__POWER_DOWN_MASK |
			  RLC_SERDES_WR_CTRL__POWER_UP_MASK |
			  RLC_SERDES_WR_CTRL__SHORT_FORMAT_MASK |
			  RLC_SERDES_WR_CTRL__BPM_DATA_MASK |
			  RLC_SERDES_WR_CTRL__REG_ADDR_MASK |
			  RLC_SERDES_WR_CTRL__SRBM_OVERRIDE_MASK);
	data |= (RLC_SERDES_WR_CTRL__RSVD_BPM_ADDR_MASK |
		 (cmd << RLC_SERDES_WR_CTRL__BPM_DATA__SHIFT) |
		 (reg_addr << RLC_SERDES_WR_CTRL__REG_ADDR__SHIFT) |
		 (0xff << RLC_SERDES_WR_CTRL__BPM_ADDR__SHIFT));

	WREG32(mmRLC_SERDES_WR_CTRL, data);
}

#define MSG_ENTER_RLC_SAFE_MODE     1
#define MSG_EXIT_RLC_SAFE_MODE      0

#define RLC_GPR_REG2__REQ_MASK           0x00000001
#define RLC_GPR_REG2__MESSAGE__SHIFT     0x00000001
#define RLC_GPR_REG2__MESSAGE_MASK       0x0000001e

static void cz_enter_rlc_safe_mode(struct amdgpu_device *adev)
{
	u32 data = 0;
	unsigned i;

	data = RREG32(mmRLC_CNTL);
	if ((data & RLC_CNTL__RLC_ENABLE_F32_MASK) == 0)
		return;

	if ((adev->cg_flags & (AMD_CG_SUPPORT_GFX_CGCG | AMD_CG_SUPPORT_GFX_MGCG)) ||
	    (adev->pg_flags & (AMD_PG_SUPPORT_GFX_PG | AMD_PG_SUPPORT_GFX_SMG |
			       AMD_PG_SUPPORT_GFX_DMG))) {
		data |= RLC_GPR_REG2__REQ_MASK;
		data &= ~RLC_GPR_REG2__MESSAGE_MASK;
		data |= (MSG_ENTER_RLC_SAFE_MODE << RLC_GPR_REG2__MESSAGE__SHIFT);
		WREG32(mmRLC_GPR_REG2, data);

		for (i = 0; i < adev->usec_timeout; i++) {
			if ((RREG32(mmRLC_GPM_STAT) &
			     (RLC_GPM_STAT__GFX_CLOCK_STATUS_MASK |
			      RLC_GPM_STAT__GFX_POWER_STATUS_MASK)) ==
			    (RLC_GPM_STAT__GFX_CLOCK_STATUS_MASK |
			     RLC_GPM_STAT__GFX_POWER_STATUS_MASK))
				break;
			udelay(1);
		}

		for (i = 0; i < adev->usec_timeout; i++) {
			if ((RREG32(mmRLC_GPR_REG2) & RLC_GPR_REG2__REQ_MASK) == 0)
				break;
			udelay(1);
		}
		adev->gfx.rlc.in_safe_mode = true;
	}
}

static void cz_exit_rlc_safe_mode(struct amdgpu_device *adev)
{
	u32 data;
	unsigned i;

	data = RREG32(mmRLC_CNTL);
	if ((data & RLC_CNTL__RLC_ENABLE_F32_MASK) == 0)
		return;

	if ((adev->cg_flags & (AMD_CG_SUPPORT_GFX_CGCG | AMD_CG_SUPPORT_GFX_MGCG)) ||
	    (adev->pg_flags & (AMD_PG_SUPPORT_GFX_PG | AMD_PG_SUPPORT_GFX_SMG |
			       AMD_PG_SUPPORT_GFX_DMG))) {
		data |= RLC_GPR_REG2__REQ_MASK;
		data &= ~RLC_GPR_REG2__MESSAGE_MASK;
		data |= (MSG_EXIT_RLC_SAFE_MODE << RLC_GPR_REG2__MESSAGE__SHIFT);
		WREG32(mmRLC_GPR_REG2, data);
		adev->gfx.rlc.in_safe_mode = false;
	}

	for (i = 0; i < adev->usec_timeout; i++) {
		if ((RREG32(mmRLC_GPR_REG2) & RLC_GPR_REG2__REQ_MASK) == 0)
			break;
		udelay(1);
	}
}

static void iceland_enter_rlc_safe_mode(struct amdgpu_device *adev)
{
	u32 data;
	unsigned i;

	data = RREG32(mmRLC_CNTL);
	if (!(data & RLC_CNTL__RLC_ENABLE_F32_MASK))
		return;

	if (adev->cg_flags & (AMD_CG_SUPPORT_GFX_CGCG | AMD_CG_SUPPORT_GFX_MGCG)) {
		data |= RLC_SAFE_MODE__CMD_MASK;
		data &= ~RLC_SAFE_MODE__MESSAGE_MASK;
		data |= (1 << RLC_SAFE_MODE__MESSAGE__SHIFT);
		WREG32(mmRLC_SAFE_MODE, data);

		for (i = 0; i < adev->usec_timeout; i++) {
			if ((RREG32(mmRLC_GPM_STAT) &
			     (RLC_GPM_STAT__GFX_CLOCK_STATUS_MASK |
			      RLC_GPM_STAT__GFX_POWER_STATUS_MASK)) ==
			    (RLC_GPM_STAT__GFX_CLOCK_STATUS_MASK |
			     RLC_GPM_STAT__GFX_POWER_STATUS_MASK))
				break;
			udelay(1);
		}

		for (i = 0; i < adev->usec_timeout; i++) {
			if ((RREG32(mmRLC_SAFE_MODE) & RLC_SAFE_MODE__CMD_MASK) == 0)
				break;
			udelay(1);
		}
		adev->gfx.rlc.in_safe_mode = true;
	}
}

static void iceland_exit_rlc_safe_mode(struct amdgpu_device *adev)
{
	u32 data = 0;
	unsigned i;

	data = RREG32(mmRLC_CNTL);
	if (!(data & RLC_CNTL__RLC_ENABLE_F32_MASK))
		return;

	if (adev->cg_flags & (AMD_CG_SUPPORT_GFX_CGCG | AMD_CG_SUPPORT_GFX_MGCG)) {
		if (adev->gfx.rlc.in_safe_mode) {
			data |= RLC_SAFE_MODE__CMD_MASK;
			data &= ~RLC_SAFE_MODE__MESSAGE_MASK;
			WREG32(mmRLC_SAFE_MODE, data);
			adev->gfx.rlc.in_safe_mode = false;
		}
	}

	for (i = 0; i < adev->usec_timeout; i++) {
		if ((RREG32(mmRLC_SAFE_MODE) & RLC_SAFE_MODE__CMD_MASK) == 0)
			break;
		udelay(1);
	}
}

static void gfx_v8_0_nop_enter_rlc_safe_mode(struct amdgpu_device *adev)
{
	adev->gfx.rlc.in_safe_mode = true;
}

static void gfx_v8_0_nop_exit_rlc_safe_mode(struct amdgpu_device *adev)
{
	adev->gfx.rlc.in_safe_mode = false;
}

static const struct amdgpu_rlc_funcs cz_rlc_funcs = {
	.enter_safe_mode = cz_enter_rlc_safe_mode,
	.exit_safe_mode = cz_exit_rlc_safe_mode
};

static const struct amdgpu_rlc_funcs iceland_rlc_funcs = {
	.enter_safe_mode = iceland_enter_rlc_safe_mode,
	.exit_safe_mode = iceland_exit_rlc_safe_mode
};

static const struct amdgpu_rlc_funcs gfx_v8_0_nop_rlc_funcs = {
	.enter_safe_mode = gfx_v8_0_nop_enter_rlc_safe_mode,
	.exit_safe_mode = gfx_v8_0_nop_exit_rlc_safe_mode
};

static void gfx_v8_0_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t temp, data;

	adev->gfx.rlc.funcs->enter_safe_mode(adev);

	/* It is disabled by HW by default */
	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGCG)) {
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGLS) {
			if (adev->cg_flags & AMD_CG_SUPPORT_GFX_RLC_LS) {
				/* 1 - RLC memory Light sleep */
				temp = data = RREG32(mmRLC_MEM_SLP_CNTL);
				data |= RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK;
				if (temp != data)
					WREG32(mmRLC_MEM_SLP_CNTL, data);
			}

			if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CP_LS) {
				/* 2 - CP memory Light sleep */
				temp = data = RREG32(mmCP_MEM_SLP_CNTL);
				data |= CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK;
				if (temp != data)
					WREG32(mmCP_MEM_SLP_CNTL, data);
			}
		}

		/* 3 - RLC_CGTT_MGCG_OVERRIDE */
		temp = data = RREG32(mmRLC_CGTT_MGCG_OVERRIDE);
		if (adev->flags & AMD_IS_APU)
			data &= ~(RLC_CGTT_MGCG_OVERRIDE__CPF_MASK |
				  RLC_CGTT_MGCG_OVERRIDE__RLC_MASK |
				  RLC_CGTT_MGCG_OVERRIDE__MGCG_MASK);
		else
			data &= ~(RLC_CGTT_MGCG_OVERRIDE__CPF_MASK |
				  RLC_CGTT_MGCG_OVERRIDE__RLC_MASK |
				  RLC_CGTT_MGCG_OVERRIDE__MGCG_MASK |
				  RLC_CGTT_MGCG_OVERRIDE__GRBM_MASK);

		if (temp != data)
			WREG32(mmRLC_CGTT_MGCG_OVERRIDE, data);

		/* 4 - wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);

		/* 5 - clear mgcg override */
		gfx_v8_0_send_serdes_cmd(adev, BPM_REG_MGCG_OVERRIDE, CLE_BPM_SERDES_CMD);

		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGTS) {
			/* 6 - Enable CGTS(Tree Shade) MGCG /MGLS */
			temp = data = RREG32(mmCGTS_SM_CTRL_REG);
			data &= ~(CGTS_SM_CTRL_REG__SM_MODE_MASK);
			data |= (0x2 << CGTS_SM_CTRL_REG__SM_MODE__SHIFT);
			data |= CGTS_SM_CTRL_REG__SM_MODE_ENABLE_MASK;
			data &= ~CGTS_SM_CTRL_REG__OVERRIDE_MASK;
			if ((adev->cg_flags & AMD_CG_SUPPORT_GFX_MGLS) &&
			    (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGTS_LS))
				data &= ~CGTS_SM_CTRL_REG__LS_OVERRIDE_MASK;
			data |= CGTS_SM_CTRL_REG__ON_MONITOR_ADD_EN_MASK;
			data |= (0x96 << CGTS_SM_CTRL_REG__ON_MONITOR_ADD__SHIFT);
			if (temp != data)
				WREG32(mmCGTS_SM_CTRL_REG, data);
		}
		udelay(50);

		/* 7 - wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);
	} else {
		/* 1 - MGCG_OVERRIDE[0] for CP and MGCG_OVERRIDE[1] for RLC */
		temp = data = RREG32(mmRLC_CGTT_MGCG_OVERRIDE);
		data |= (RLC_CGTT_MGCG_OVERRIDE__CPF_MASK |
				RLC_CGTT_MGCG_OVERRIDE__RLC_MASK |
				RLC_CGTT_MGCG_OVERRIDE__MGCG_MASK |
				RLC_CGTT_MGCG_OVERRIDE__GRBM_MASK);
		if (temp != data)
			WREG32(mmRLC_CGTT_MGCG_OVERRIDE, data);

		/* 2 - disable MGLS in RLC */
		data = RREG32(mmRLC_MEM_SLP_CNTL);
		if (data & RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK) {
			data &= ~RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK;
			WREG32(mmRLC_MEM_SLP_CNTL, data);
		}

		/* 3 - disable MGLS in CP */
		data = RREG32(mmCP_MEM_SLP_CNTL);
		if (data & CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK) {
			data &= ~CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK;
			WREG32(mmCP_MEM_SLP_CNTL, data);
		}

		/* 4 - Disable CGTS(Tree Shade) MGCG and MGLS */
		temp = data = RREG32(mmCGTS_SM_CTRL_REG);
		data |= (CGTS_SM_CTRL_REG__OVERRIDE_MASK |
				CGTS_SM_CTRL_REG__LS_OVERRIDE_MASK);
		if (temp != data)
			WREG32(mmCGTS_SM_CTRL_REG, data);

		/* 5 - wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);

		/* 6 - set mgcg override */
		gfx_v8_0_send_serdes_cmd(adev, BPM_REG_MGCG_OVERRIDE, SET_BPM_SERDES_CMD);

		udelay(50);

		/* 7- wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);
	}

	adev->gfx.rlc.funcs->exit_safe_mode(adev);
}

static void gfx_v8_0_update_coarse_grain_clock_gating(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t temp, temp1, data, data1;

	temp = data = RREG32(mmRLC_CGCG_CGLS_CTRL);

	adev->gfx.rlc.funcs->enter_safe_mode(adev);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGCG)) {
		/* 1 enable cntx_empty_int_enable/cntx_busy_int_enable/
		 * Cmp_busy/GFX_Idle interrupts
		 */
		gfx_v8_0_enable_gui_idle_interrupt(adev, true);

		temp1 = data1 =	RREG32(mmRLC_CGTT_MGCG_OVERRIDE);
		data1 &= ~RLC_CGTT_MGCG_OVERRIDE__CGCG_MASK;
		if (temp1 != data1)
			WREG32(mmRLC_CGTT_MGCG_OVERRIDE, data1);

		/* 2 wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);

		/* 3 - clear cgcg override */
		gfx_v8_0_send_serdes_cmd(adev, BPM_REG_CGCG_OVERRIDE, CLE_BPM_SERDES_CMD);

		/* wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);

		/* 4 - write cmd to set CGLS */
		gfx_v8_0_send_serdes_cmd(adev, BPM_REG_CGLS_EN, SET_BPM_SERDES_CMD);

		/* 5 - enable cgcg */
		data |= RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK;

		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGLS) {
			/* enable cgls*/
			data |= RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK;

			temp1 = data1 =	RREG32(mmRLC_CGTT_MGCG_OVERRIDE);
			data1 &= ~RLC_CGTT_MGCG_OVERRIDE__CGLS_MASK;

			if (temp1 != data1)
				WREG32(mmRLC_CGTT_MGCG_OVERRIDE, data1);
		} else {
			data &= ~RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK;
		}

		if (temp != data)
			WREG32(mmRLC_CGCG_CGLS_CTRL, data);
	} else {
		/* disable cntx_empty_int_enable & GFX Idle interrupt */
		gfx_v8_0_enable_gui_idle_interrupt(adev, false);

		/* TEST CGCG */
		temp1 = data1 =	RREG32(mmRLC_CGTT_MGCG_OVERRIDE);
		data1 |= (RLC_CGTT_MGCG_OVERRIDE__CGCG_MASK |
				RLC_CGTT_MGCG_OVERRIDE__CGLS_MASK);
		if (temp1 != data1)
			WREG32(mmRLC_CGTT_MGCG_OVERRIDE, data1);

		/* read gfx register to wake up cgcg */
		RREG32(mmCB_CGTT_SCLK_CTRL);
		RREG32(mmCB_CGTT_SCLK_CTRL);
		RREG32(mmCB_CGTT_SCLK_CTRL);
		RREG32(mmCB_CGTT_SCLK_CTRL);

		/* wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);

		/* write cmd to Set CGCG Overrride */
		gfx_v8_0_send_serdes_cmd(adev, BPM_REG_CGCG_OVERRIDE, SET_BPM_SERDES_CMD);

		/* wait for RLC_SERDES_CU_MASTER & RLC_SERDES_NONCU_MASTER idle */
		gfx_v8_0_wait_for_rlc_serdes(adev);

		/* write cmd to Clear CGLS */
		gfx_v8_0_send_serdes_cmd(adev, BPM_REG_CGLS_EN, CLE_BPM_SERDES_CMD);

		/* disable cgcg, cgls should be disabled too. */
		data &= ~(RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK |
			  RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK);
		if (temp != data)
			WREG32(mmRLC_CGCG_CGLS_CTRL, data);
	}

	adev->gfx.rlc.funcs->exit_safe_mode(adev);
}
static int gfx_v8_0_update_gfx_clock_gating(struct amdgpu_device *adev,
					    bool enable)
{
	if (enable) {
		/* CGCG/CGLS should be enabled after MGCG/MGLS/TS(CG/LS)
		 * ===  MGCG + MGLS + TS(CG/LS) ===
		 */
		gfx_v8_0_update_medium_grain_clock_gating(adev, enable);
		gfx_v8_0_update_coarse_grain_clock_gating(adev, enable);
	} else {
		/* CGCG/CGLS should be disabled before MGCG/MGLS/TS(CG/LS)
		 * ===  CGCG + CGLS ===
		 */
		gfx_v8_0_update_coarse_grain_clock_gating(adev, enable);
		gfx_v8_0_update_medium_grain_clock_gating(adev, enable);
	}
	return 0;
}

static int gfx_v8_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (adev->asic_type) {
	case CHIP_FIJI:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		gfx_v8_0_update_gfx_clock_gating(adev,
						 state == AMD_CG_STATE_GATE ? true : false);
		break;
	default:
		break;
	}
	return 0;
}

static u32 gfx_v8_0_ring_get_rptr_gfx(struct amdgpu_ring *ring)
{
	u32 rptr;

	rptr = ring->adev->wb.wb[ring->rptr_offs];

	return rptr;
}

static u32 gfx_v8_0_ring_get_wptr_gfx(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 wptr;

	if (ring->use_doorbell)
		/* XXX check if swapping is necessary on BE */
		wptr = ring->adev->wb.wb[ring->wptr_offs];
	else
		wptr = RREG32(mmCP_RB0_WPTR);

	return wptr;
}

static void gfx_v8_0_ring_set_wptr_gfx(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		adev->wb.wb[ring->wptr_offs] = ring->wptr;
		WDOORBELL32(ring->doorbell_index, ring->wptr);
	} else {
		WREG32(mmCP_RB0_WPTR, ring->wptr);
		(void)RREG32(mmCP_RB0_WPTR);
	}
}

static void gfx_v8_0_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	u32 ref_and_mask, reg_mem_engine;

	if (ring->type == AMDGPU_RING_TYPE_COMPUTE) {
		switch (ring->me) {
		case 1:
			ref_and_mask = GPU_HDP_FLUSH_DONE__CP2_MASK << ring->pipe;
			break;
		case 2:
			ref_and_mask = GPU_HDP_FLUSH_DONE__CP6_MASK << ring->pipe;
			break;
		default:
			return;
		}
		reg_mem_engine = 0;
	} else {
		ref_and_mask = GPU_HDP_FLUSH_DONE__CP0_MASK;
		reg_mem_engine = WAIT_REG_MEM_ENGINE(1); /* pfp */
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	amdgpu_ring_write(ring, (WAIT_REG_MEM_OPERATION(1) | /* write, wait, write */
				 WAIT_REG_MEM_FUNCTION(3) |  /* == */
				 reg_mem_engine));
	amdgpu_ring_write(ring, mmGPU_HDP_FLUSH_REQ);
	amdgpu_ring_write(ring, mmGPU_HDP_FLUSH_DONE);
	amdgpu_ring_write(ring, ref_and_mask);
	amdgpu_ring_write(ring, ref_and_mask);
	amdgpu_ring_write(ring, 0x20); /* poll interval */
}

static void gfx_v8_0_ring_emit_hdp_invalidate(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(0) |
				 WR_CONFIRM));
	amdgpu_ring_write(ring, mmHDP_DEBUG0);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 1);

}

static void gfx_v8_0_ring_emit_ib_gfx(struct amdgpu_ring *ring,
				      struct amdgpu_ib *ib,
				      unsigned vm_id, bool ctx_switch)
{
	u32 header, control = 0;
	u32 next_rptr = ring->wptr + 5;

	if (ctx_switch)
		next_rptr += 2;

	next_rptr += 4;
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, WRITE_DATA_DST_SEL(5) | WR_CONFIRM);
	amdgpu_ring_write(ring, ring->next_rptr_gpu_addr & 0xfffffffc);
	amdgpu_ring_write(ring, upper_32_bits(ring->next_rptr_gpu_addr) & 0xffffffff);
	amdgpu_ring_write(ring, next_rptr);

	/* insert SWITCH_BUFFER packet before first IB in the ring frame */
	if (ctx_switch) {
		amdgpu_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		amdgpu_ring_write(ring, 0);
	}

	if (ib->flags & AMDGPU_IB_FLAG_CE)
		header = PACKET3(PACKET3_INDIRECT_BUFFER_CONST, 2);
	else
		header = PACKET3(PACKET3_INDIRECT_BUFFER, 2);

	control |= ib->length_dw | (vm_id << 24);

	amdgpu_ring_write(ring, header);
	amdgpu_ring_write(ring,
#ifdef __BIG_ENDIAN
			  (2 << 0) |
#endif
			  (ib->gpu_addr & 0xFFFFFFFC));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr) & 0xFFFF);
	amdgpu_ring_write(ring, control);
}

static void gfx_v8_0_ring_emit_ib_compute(struct amdgpu_ring *ring,
					  struct amdgpu_ib *ib,
					  unsigned vm_id, bool ctx_switch)
{
	u32 header, control = 0;
	u32 next_rptr = ring->wptr + 5;

	control |= INDIRECT_BUFFER_VALID;

	next_rptr += 4;
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, WRITE_DATA_DST_SEL(5) | WR_CONFIRM);
	amdgpu_ring_write(ring, ring->next_rptr_gpu_addr & 0xfffffffc);
	amdgpu_ring_write(ring, upper_32_bits(ring->next_rptr_gpu_addr) & 0xffffffff);
	amdgpu_ring_write(ring, next_rptr);

	header = PACKET3(PACKET3_INDIRECT_BUFFER, 2);

	control |= ib->length_dw | (vm_id << 24);

	amdgpu_ring_write(ring, header);
	amdgpu_ring_write(ring,
#ifdef __BIG_ENDIAN
					  (2 << 0) |
#endif
					  (ib->gpu_addr & 0xFFFFFFFC));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr) & 0xFFFF);
	amdgpu_ring_write(ring, control);
}

static void gfx_v8_0_ring_emit_fence_gfx(struct amdgpu_ring *ring, u64 addr,
					 u64 seq, unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	bool int_sel = flags & AMDGPU_FENCE_FLAG_INT;

	/* EVENT_WRITE_EOP - flush caches, send int */
	amdgpu_ring_write(ring, PACKET3(PACKET3_EVENT_WRITE_EOP, 4));
	amdgpu_ring_write(ring, (EOP_TCL1_ACTION_EN |
				 EOP_TC_ACTION_EN |
				 EOP_TC_WB_ACTION_EN |
				 EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) |
				 EVENT_INDEX(5)));
	amdgpu_ring_write(ring, addr & 0xfffffffc);
	amdgpu_ring_write(ring, (upper_32_bits(addr) & 0xffff) |
			  DATA_SEL(write64bit ? 2 : 1) | INT_SEL(int_sel ? 2 : 0));
	amdgpu_ring_write(ring, lower_32_bits(seq));
	amdgpu_ring_write(ring, upper_32_bits(seq));

}

static void gfx_v8_0_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	int usepfp = (ring->type == AMDGPU_RING_TYPE_GFX);
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	amdgpu_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	amdgpu_ring_write(ring, (WAIT_REG_MEM_MEM_SPACE(1) | /* memory */
				 WAIT_REG_MEM_FUNCTION(3) | /* equal */
				 WAIT_REG_MEM_ENGINE(usepfp))); /* pfp or me */
	amdgpu_ring_write(ring, addr & 0xfffffffc);
	amdgpu_ring_write(ring, upper_32_bits(addr) & 0xffffffff);
	amdgpu_ring_write(ring, seq);
	amdgpu_ring_write(ring, 0xffffffff);
	amdgpu_ring_write(ring, 4); /* poll interval */

	if (usepfp) {
		/* synce CE with ME to prevent CE fetch CEIB before context switch done */
		amdgpu_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		amdgpu_ring_write(ring, 0);
	}
}

static void gfx_v8_0_ring_emit_vm_flush(struct amdgpu_ring *ring,
					unsigned vm_id, uint64_t pd_addr)
{
	int usepfp = (ring->type == AMDGPU_RING_TYPE_GFX);

	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(usepfp) |
				 WRITE_DATA_DST_SEL(0)) |
				 WR_CONFIRM);
	if (vm_id < 8) {
		amdgpu_ring_write(ring,
				  (mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR + vm_id));
	} else {
		amdgpu_ring_write(ring,
				  (mmVM_CONTEXT8_PAGE_TABLE_BASE_ADDR + vm_id - 8));
	}
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, pd_addr >> 12);

	/* bits 0-15 are the VM contexts0-15 */
	/* invalidate the cache */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(0)));
	amdgpu_ring_write(ring, mmVM_INVALIDATE_REQUEST);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 1 << vm_id);

	/* wait for the invalidate to complete */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	amdgpu_ring_write(ring, (WAIT_REG_MEM_OPERATION(0) | /* wait */
				 WAIT_REG_MEM_FUNCTION(0) |  /* always */
				 WAIT_REG_MEM_ENGINE(0))); /* me */
	amdgpu_ring_write(ring, mmVM_INVALIDATE_REQUEST);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0); /* ref */
	amdgpu_ring_write(ring, 0); /* mask */
	amdgpu_ring_write(ring, 0x20); /* poll interval */

	/* compute doesn't have PFP */
	if (usepfp) {
		/* sync PFP to ME, otherwise we might get invalid PFP reads */
		amdgpu_ring_write(ring, PACKET3(PACKET3_PFP_SYNC_ME, 0));
		amdgpu_ring_write(ring, 0x0);
		amdgpu_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
		amdgpu_ring_write(ring, 0);
	}
}

static u32 gfx_v8_0_ring_get_rptr_compute(struct amdgpu_ring *ring)
{
	return ring->adev->wb.wb[ring->rptr_offs];
}

static u32 gfx_v8_0_ring_get_wptr_compute(struct amdgpu_ring *ring)
{
	return ring->adev->wb.wb[ring->wptr_offs];
}

static void gfx_v8_0_ring_set_wptr_compute(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	/* XXX check if swapping is necessary on BE */
	adev->wb.wb[ring->wptr_offs] = ring->wptr;
	WDOORBELL32(ring->doorbell_index, ring->wptr);
}

static void gfx_v8_0_ring_emit_fence_compute(struct amdgpu_ring *ring,
					     u64 addr, u64 seq,
					     unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	bool int_sel = flags & AMDGPU_FENCE_FLAG_INT;

	/* RELEASE_MEM - flush caches, send int */
	amdgpu_ring_write(ring, PACKET3(PACKET3_RELEASE_MEM, 5));
	amdgpu_ring_write(ring, (EOP_TCL1_ACTION_EN |
				 EOP_TC_ACTION_EN |
				 EOP_TC_WB_ACTION_EN |
				 EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) |
				 EVENT_INDEX(5)));
	amdgpu_ring_write(ring, DATA_SEL(write64bit ? 2 : 1) | INT_SEL(int_sel ? 2 : 0));
	amdgpu_ring_write(ring, addr & 0xfffffffc);
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));
	amdgpu_ring_write(ring, upper_32_bits(seq));
}

static void gfx_v8_0_set_gfx_eop_interrupt_state(struct amdgpu_device *adev,
						 enum amdgpu_interrupt_state state)
{
	u32 cp_int_cntl;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		cp_int_cntl = RREG32(mmCP_INT_CNTL_RING0);
		cp_int_cntl = REG_SET_FIELD(cp_int_cntl, CP_INT_CNTL_RING0,
					    TIME_STAMP_INT_ENABLE, 0);
		WREG32(mmCP_INT_CNTL_RING0, cp_int_cntl);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		cp_int_cntl = RREG32(mmCP_INT_CNTL_RING0);
		cp_int_cntl =
			REG_SET_FIELD(cp_int_cntl, CP_INT_CNTL_RING0,
				      TIME_STAMP_INT_ENABLE, 1);
		WREG32(mmCP_INT_CNTL_RING0, cp_int_cntl);
		break;
	default:
		break;
	}
}

static void gfx_v8_0_set_compute_eop_interrupt_state(struct amdgpu_device *adev,
						     int me, int pipe,
						     enum amdgpu_interrupt_state state)
{
	u32 mec_int_cntl, mec_int_cntl_reg;

	/*
	 * amdgpu controls only pipe 0 of MEC1. That's why this function only
	 * handles the setting of interrupts for this specific pipe. All other
	 * pipes' interrupts are set by amdkfd.
	 */

	if (me == 1) {
		switch (pipe) {
		case 0:
			mec_int_cntl_reg = mmCP_ME1_PIPE0_INT_CNTL;
			break;
		default:
			DRM_DEBUG("invalid pipe %d\n", pipe);
			return;
		}
	} else {
		DRM_DEBUG("invalid me %d\n", me);
		return;
	}

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		mec_int_cntl = RREG32(mec_int_cntl_reg);
		mec_int_cntl = REG_SET_FIELD(mec_int_cntl, CP_ME1_PIPE0_INT_CNTL,
					     TIME_STAMP_INT_ENABLE, 0);
		WREG32(mec_int_cntl_reg, mec_int_cntl);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		mec_int_cntl = RREG32(mec_int_cntl_reg);
		mec_int_cntl = REG_SET_FIELD(mec_int_cntl, CP_ME1_PIPE0_INT_CNTL,
					     TIME_STAMP_INT_ENABLE, 1);
		WREG32(mec_int_cntl_reg, mec_int_cntl);
		break;
	default:
		break;
	}
}

static int gfx_v8_0_set_priv_reg_fault_state(struct amdgpu_device *adev,
					     struct amdgpu_irq_src *source,
					     unsigned type,
					     enum amdgpu_interrupt_state state)
{
	u32 cp_int_cntl;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		cp_int_cntl = RREG32(mmCP_INT_CNTL_RING0);
		cp_int_cntl = REG_SET_FIELD(cp_int_cntl, CP_INT_CNTL_RING0,
					    PRIV_REG_INT_ENABLE, 0);
		WREG32(mmCP_INT_CNTL_RING0, cp_int_cntl);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		cp_int_cntl = RREG32(mmCP_INT_CNTL_RING0);
		cp_int_cntl = REG_SET_FIELD(cp_int_cntl, CP_INT_CNTL_RING0,
					    PRIV_REG_INT_ENABLE, 1);
		WREG32(mmCP_INT_CNTL_RING0, cp_int_cntl);
		break;
	default:
		break;
	}

	return 0;
}

static int gfx_v8_0_set_priv_inst_fault_state(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      unsigned type,
					      enum amdgpu_interrupt_state state)
{
	u32 cp_int_cntl;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		cp_int_cntl = RREG32(mmCP_INT_CNTL_RING0);
		cp_int_cntl = REG_SET_FIELD(cp_int_cntl, CP_INT_CNTL_RING0,
					    PRIV_INSTR_INT_ENABLE, 0);
		WREG32(mmCP_INT_CNTL_RING0, cp_int_cntl);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		cp_int_cntl = RREG32(mmCP_INT_CNTL_RING0);
		cp_int_cntl = REG_SET_FIELD(cp_int_cntl, CP_INT_CNTL_RING0,
					    PRIV_INSTR_INT_ENABLE, 1);
		WREG32(mmCP_INT_CNTL_RING0, cp_int_cntl);
		break;
	default:
		break;
	}

	return 0;
}

static int gfx_v8_0_set_eop_interrupt_state(struct amdgpu_device *adev,
					    struct amdgpu_irq_src *src,
					    unsigned type,
					    enum amdgpu_interrupt_state state)
{
	switch (type) {
	case AMDGPU_CP_IRQ_GFX_EOP:
		gfx_v8_0_set_gfx_eop_interrupt_state(adev, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 1, 0, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE1_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 1, 1, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE2_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 1, 2, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE3_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 1, 3, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE0_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 2, 0, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE1_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 2, 1, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE2_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 2, 2, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE3_EOP:
		gfx_v8_0_set_compute_eop_interrupt_state(adev, 2, 3, state);
		break;
	default:
		break;
	}
	return 0;
}

static int gfx_v8_0_eop_irq(struct amdgpu_device *adev,
			    struct amdgpu_irq_src *source,
			    struct amdgpu_iv_entry *entry)
{
	int i;
	u8 me_id, pipe_id, queue_id;
	struct amdgpu_ring *ring;

	DRM_DEBUG("IH: CP EOP\n");
	me_id = (entry->ring_id & 0x0c) >> 2;
	pipe_id = (entry->ring_id & 0x03) >> 0;
	queue_id = (entry->ring_id & 0x70) >> 4;

	switch (me_id) {
	case 0:
		amdgpu_fence_process(&adev->gfx.gfx_ring[0]);
		break;
	case 1:
	case 2:
		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			ring = &adev->gfx.compute_ring[i];
			/* Per-queue interrupt is supported for MEC starting from VI.
			  * The interrupt can only be enabled/disabled per pipe instead of per queue.
			  */
			if ((ring->me == me_id) && (ring->pipe == pipe_id) && (ring->queue == queue_id))
				amdgpu_fence_process(ring);
		}
		break;
	}
	return 0;
}

static int gfx_v8_0_priv_reg_irq(struct amdgpu_device *adev,
				 struct amdgpu_irq_src *source,
				 struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal register access in command stream\n");
	schedule_work(&adev->reset_work);
	return 0;
}

static int gfx_v8_0_priv_inst_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal instruction in command stream\n");
	schedule_work(&adev->reset_work);
	return 0;
}

const struct amd_ip_funcs gfx_v8_0_ip_funcs = {
	.name = "gfx_v8_0",
	.early_init = gfx_v8_0_early_init,
	.late_init = gfx_v8_0_late_init,
	.sw_init = gfx_v8_0_sw_init,
	.sw_fini = gfx_v8_0_sw_fini,
	.hw_init = gfx_v8_0_hw_init,
	.hw_fini = gfx_v8_0_hw_fini,
	.suspend = gfx_v8_0_suspend,
	.resume = gfx_v8_0_resume,
	.is_idle = gfx_v8_0_is_idle,
	.wait_for_idle = gfx_v8_0_wait_for_idle,
	.soft_reset = gfx_v8_0_soft_reset,
	.set_clockgating_state = gfx_v8_0_set_clockgating_state,
	.set_powergating_state = gfx_v8_0_set_powergating_state,
};

static const struct amdgpu_ring_funcs gfx_v8_0_ring_funcs_gfx = {
	.get_rptr = gfx_v8_0_ring_get_rptr_gfx,
	.get_wptr = gfx_v8_0_ring_get_wptr_gfx,
	.set_wptr = gfx_v8_0_ring_set_wptr_gfx,
	.parse_cs = NULL,
	.emit_ib = gfx_v8_0_ring_emit_ib_gfx,
	.emit_fence = gfx_v8_0_ring_emit_fence_gfx,
	.emit_pipeline_sync = gfx_v8_0_ring_emit_pipeline_sync,
	.emit_vm_flush = gfx_v8_0_ring_emit_vm_flush,
	.emit_gds_switch = gfx_v8_0_ring_emit_gds_switch,
	.emit_hdp_flush = gfx_v8_0_ring_emit_hdp_flush,
	.emit_hdp_invalidate = gfx_v8_0_ring_emit_hdp_invalidate,
	.test_ring = gfx_v8_0_ring_test_ring,
	.test_ib = gfx_v8_0_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
};

static const struct amdgpu_ring_funcs gfx_v8_0_ring_funcs_compute = {
	.get_rptr = gfx_v8_0_ring_get_rptr_compute,
	.get_wptr = gfx_v8_0_ring_get_wptr_compute,
	.set_wptr = gfx_v8_0_ring_set_wptr_compute,
	.parse_cs = NULL,
	.emit_ib = gfx_v8_0_ring_emit_ib_compute,
	.emit_fence = gfx_v8_0_ring_emit_fence_compute,
	.emit_pipeline_sync = gfx_v8_0_ring_emit_pipeline_sync,
	.emit_vm_flush = gfx_v8_0_ring_emit_vm_flush,
	.emit_gds_switch = gfx_v8_0_ring_emit_gds_switch,
	.emit_hdp_flush = gfx_v8_0_ring_emit_hdp_flush,
	.emit_hdp_invalidate = gfx_v8_0_ring_emit_hdp_invalidate,
	.test_ring = gfx_v8_0_ring_test_ring,
	.test_ib = gfx_v8_0_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
};

static void gfx_v8_0_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->gfx.num_gfx_rings; i++)
		adev->gfx.gfx_ring[i].funcs = &gfx_v8_0_ring_funcs_gfx;

	for (i = 0; i < adev->gfx.num_compute_rings; i++)
		adev->gfx.compute_ring[i].funcs = &gfx_v8_0_ring_funcs_compute;
}

static const struct amdgpu_irq_src_funcs gfx_v8_0_eop_irq_funcs = {
	.set = gfx_v8_0_set_eop_interrupt_state,
	.process = gfx_v8_0_eop_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v8_0_priv_reg_irq_funcs = {
	.set = gfx_v8_0_set_priv_reg_fault_state,
	.process = gfx_v8_0_priv_reg_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v8_0_priv_inst_irq_funcs = {
	.set = gfx_v8_0_set_priv_inst_fault_state,
	.process = gfx_v8_0_priv_inst_irq,
};

static void gfx_v8_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gfx.eop_irq.num_types = AMDGPU_CP_IRQ_LAST;
	adev->gfx.eop_irq.funcs = &gfx_v8_0_eop_irq_funcs;

	adev->gfx.priv_reg_irq.num_types = 1;
	adev->gfx.priv_reg_irq.funcs = &gfx_v8_0_priv_reg_irq_funcs;

	adev->gfx.priv_inst_irq.num_types = 1;
	adev->gfx.priv_inst_irq.funcs = &gfx_v8_0_priv_inst_irq_funcs;
}

static void gfx_v8_0_set_rlc_funcs(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_TOPAZ:
	case CHIP_STONEY:
		adev->gfx.rlc.funcs = &iceland_rlc_funcs;
		break;
	case CHIP_CARRIZO:
		adev->gfx.rlc.funcs = &cz_rlc_funcs;
		break;
	default:
		adev->gfx.rlc.funcs = &gfx_v8_0_nop_rlc_funcs;
		break;
	}
}

static void gfx_v8_0_set_gds_init(struct amdgpu_device *adev)
{
	/* init asci gds info */
	adev->gds.mem.total_size = RREG32(mmGDS_VMID0_SIZE);
	adev->gds.gws.total_size = 64;
	adev->gds.oa.total_size = 16;

	if (adev->gds.mem.total_size == 64 * 1024) {
		adev->gds.mem.gfx_partition_size = 4096;
		adev->gds.mem.cs_partition_size = 4096;

		adev->gds.gws.gfx_partition_size = 4;
		adev->gds.gws.cs_partition_size = 4;

		adev->gds.oa.gfx_partition_size = 4;
		adev->gds.oa.cs_partition_size = 1;
	} else {
		adev->gds.mem.gfx_partition_size = 1024;
		adev->gds.mem.cs_partition_size = 1024;

		adev->gds.gws.gfx_partition_size = 16;
		adev->gds.gws.cs_partition_size = 16;

		adev->gds.oa.gfx_partition_size = 4;
		adev->gds.oa.cs_partition_size = 4;
	}
}

static u32 gfx_v8_0_get_cu_active_bitmap(struct amdgpu_device *adev)
{
	u32 data, mask;

	data = RREG32(mmCC_GC_SHADER_ARRAY_CONFIG);
	data |= RREG32(mmGC_USER_SHADER_ARRAY_CONFIG);

	data &= CC_GC_SHADER_ARRAY_CONFIG__INACTIVE_CUS_MASK;
	data >>= CC_GC_SHADER_ARRAY_CONFIG__INACTIVE_CUS__SHIFT;

	mask = gfx_v8_0_create_bitmask(adev->gfx.config.max_cu_per_sh);

	return (~data) & mask;
}

static void gfx_v8_0_get_cu_info(struct amdgpu_device *adev)
{
	int i, j, k, counter, active_cu_number = 0;
	u32 mask, bitmap, ao_bitmap, ao_cu_mask = 0;
	struct amdgpu_cu_info *cu_info = &adev->gfx.cu_info;

	memset(cu_info, 0, sizeof(*cu_info));

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			mask = 1;
			ao_bitmap = 0;
			counter = 0;
			gfx_v8_0_select_se_sh(adev, i, j);
			bitmap = gfx_v8_0_get_cu_active_bitmap(adev);
			cu_info->bitmap[i][j] = bitmap;

			for (k = 0; k < 16; k ++) {
				if (bitmap & mask) {
					if (counter < 2)
						ao_bitmap |= mask;
					counter ++;
				}
				mask <<= 1;
			}
			active_cu_number += counter;
			ao_cu_mask |= (ao_bitmap << (i * 16 + j * 8));
		}
	}
	gfx_v8_0_select_se_sh(adev, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);

	cu_info->number = active_cu_number;
	cu_info->ao_cu_mask = ao_cu_mask;
}
