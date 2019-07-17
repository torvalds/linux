/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "soc15.h"
#include "soc15d.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_pm.h"

#include "gc/gc_9_0_offset.h"
#include "gc/gc_9_0_sh_mask.h"
#include "vega10_enum.h"
#include "hdp/hdp_4_0_offset.h"

#include "soc15.h"
#include "soc15_common.h"
#include "clearstate_gfx9.h"
#include "v9_structs.h"

#include "ivsrcid/gfx/irqsrcs_gfx_9_0.h"

#include "amdgpu_ras.h"

#define GFX9_NUM_GFX_RINGS     1
#define GFX9_MEC_HPD_SIZE 4096
#define RLCG_UCODE_LOADING_START_ADDRESS 0x00002000L
#define RLC_SAVE_RESTORE_ADDR_STARTING_OFFSET 0x00000000L

#define mmPWR_MISC_CNTL_STATUS					0x0183
#define mmPWR_MISC_CNTL_STATUS_BASE_IDX				0
#define PWR_MISC_CNTL_STATUS__PWR_GFX_RLC_CGPG_EN__SHIFT	0x0
#define PWR_MISC_CNTL_STATUS__PWR_GFXOFF_STATUS__SHIFT		0x1
#define PWR_MISC_CNTL_STATUS__PWR_GFX_RLC_CGPG_EN_MASK		0x00000001L
#define PWR_MISC_CNTL_STATUS__PWR_GFXOFF_STATUS_MASK		0x00000006L

MODULE_FIRMWARE("amdgpu/vega10_ce.bin");
MODULE_FIRMWARE("amdgpu/vega10_pfp.bin");
MODULE_FIRMWARE("amdgpu/vega10_me.bin");
MODULE_FIRMWARE("amdgpu/vega10_mec.bin");
MODULE_FIRMWARE("amdgpu/vega10_mec2.bin");
MODULE_FIRMWARE("amdgpu/vega10_rlc.bin");

MODULE_FIRMWARE("amdgpu/vega12_ce.bin");
MODULE_FIRMWARE("amdgpu/vega12_pfp.bin");
MODULE_FIRMWARE("amdgpu/vega12_me.bin");
MODULE_FIRMWARE("amdgpu/vega12_mec.bin");
MODULE_FIRMWARE("amdgpu/vega12_mec2.bin");
MODULE_FIRMWARE("amdgpu/vega12_rlc.bin");

MODULE_FIRMWARE("amdgpu/vega20_ce.bin");
MODULE_FIRMWARE("amdgpu/vega20_pfp.bin");
MODULE_FIRMWARE("amdgpu/vega20_me.bin");
MODULE_FIRMWARE("amdgpu/vega20_mec.bin");
MODULE_FIRMWARE("amdgpu/vega20_mec2.bin");
MODULE_FIRMWARE("amdgpu/vega20_rlc.bin");

MODULE_FIRMWARE("amdgpu/raven_ce.bin");
MODULE_FIRMWARE("amdgpu/raven_pfp.bin");
MODULE_FIRMWARE("amdgpu/raven_me.bin");
MODULE_FIRMWARE("amdgpu/raven_mec.bin");
MODULE_FIRMWARE("amdgpu/raven_mec2.bin");
MODULE_FIRMWARE("amdgpu/raven_rlc.bin");

MODULE_FIRMWARE("amdgpu/picasso_ce.bin");
MODULE_FIRMWARE("amdgpu/picasso_pfp.bin");
MODULE_FIRMWARE("amdgpu/picasso_me.bin");
MODULE_FIRMWARE("amdgpu/picasso_mec.bin");
MODULE_FIRMWARE("amdgpu/picasso_mec2.bin");
MODULE_FIRMWARE("amdgpu/picasso_rlc.bin");
MODULE_FIRMWARE("amdgpu/picasso_rlc_am4.bin");

MODULE_FIRMWARE("amdgpu/raven2_ce.bin");
MODULE_FIRMWARE("amdgpu/raven2_pfp.bin");
MODULE_FIRMWARE("amdgpu/raven2_me.bin");
MODULE_FIRMWARE("amdgpu/raven2_mec.bin");
MODULE_FIRMWARE("amdgpu/raven2_mec2.bin");
MODULE_FIRMWARE("amdgpu/raven2_rlc.bin");
MODULE_FIRMWARE("amdgpu/raven_kicker_rlc.bin");

static const struct soc15_reg_golden golden_settings_gc_9_0[] =
{
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG2, 0xf00fffff, 0x00000400),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG3, 0x80000000, 0x80000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGB_GPU_ID, 0x0000000f, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_BINNER_EVENT_CNTL_3, 0x00000003, 0x82400024),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE, 0x3fffffff, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSH_MEM_CONFIG, 0x00001000, 0x00001000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSPI_RESOURCE_RESERVE_CU_0, 0x0007ffff, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSPI_RESOURCE_RESERVE_CU_1, 0x0007ffff, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSPI_RESOURCE_RESERVE_EN_CU_0, 0x01ffffff, 0x0000ff87),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSPI_RESOURCE_RESERVE_EN_CU_1, 0x01ffffff, 0x0000ff8f),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSQC_CONFIG, 0x03000000, 0x020a2000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTA_CNTL_AUX, 0xfffffeef, 0x010b0000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTCP_CHAN_STEER_HI, 0xffffffff, 0x4a2c0e68),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTCP_CHAN_STEER_LO, 0xffffffff, 0xb5d3f197),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmVGT_CACHE_INVALIDATION, 0x3fff3af3, 0x19200000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmVGT_GS_MAX_WAVE_ID, 0x00000fff, 0x000003ff),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCP_MEC1_F32_INT_DIS, 0x00000000, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCP_MEC2_F32_INT_DIS, 0x00000000, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCP_DEBUG, 0x00000000, 0x00008000)
};

static const struct soc15_reg_golden golden_settings_gc_9_0_vg10[] =
{
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_HW_CONTROL, 0x0000f000, 0x00012107),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_HW_CONTROL_3, 0x30000000, 0x10000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCPC_UTCL1_CNTL, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCPF_UTCL1_CNTL, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCPG_UTCL1_CNTL, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGB_ADDR_CONFIG, 0xffff77ff, 0x2a114042),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGB_ADDR_CONFIG_READ, 0xffff77ff, 0x2a114042),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmIA_UTCL1_CNTL, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE_1, 0x00008000, 0x00048000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_GPM_UTCL1_CNTL_0, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_GPM_UTCL1_CNTL_1, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_GPM_UTCL1_CNTL_2, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_PREWALKER_UTCL1_CNTL, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_SPM_UTCL1_CNTL, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRMI_UTCL1_CNTL2, 0x00030000, 0x00020000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSPI_CONFIG_CNTL_1, 0x0000000f, 0x01000107),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTD_CNTL, 0x00001800, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmWD_UTCL1_CNTL, 0x08000000, 0x08000080)
};

static const struct soc15_reg_golden golden_settings_gc_9_0_vg20[] =
{
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_DCC_CONFIG, 0x0f000080, 0x04000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_HW_CONTROL_2, 0x0f000000, 0x0a000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_HW_CONTROL_3, 0x30000000, 0x10000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGB_ADDR_CONFIG, 0xf3e777ff, 0x22014042),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGB_ADDR_CONFIG_READ, 0xf3e777ff, 0x22014042),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG2, 0x00003e00, 0x00000400),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE_1, 0xff840000, 0x04040000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRMI_UTCL1_CNTL2, 0x00030000, 0x00030000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSPI_CONFIG_CNTL_1, 0xffff010f, 0x01000107),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTA_CNTL_AUX, 0x000b0000, 0x000b0000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTD_CNTL, 0x01000000, 0x01000000)
};

static const struct soc15_reg_golden golden_settings_gc_9_1[] =
{
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_HW_CONTROL, 0xfffdf3cf, 0x00014104),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCPC_UTCL1_CNTL, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCPF_UTCL1_CNTL, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCPG_UTCL1_CNTL, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG2, 0xf00fffff, 0x00000420),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGB_GPU_ID, 0x0000000f, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmIA_UTCL1_CNTL, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_BINNER_EVENT_CNTL_3, 0x00000003, 0x82400024),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE, 0x3fffffff, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_GPM_UTCL1_CNTL_0, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_GPM_UTCL1_CNTL_1, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_GPM_UTCL1_CNTL_2, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_PREWALKER_UTCL1_CNTL, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_SPM_UTCL1_CNTL, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTA_CNTL_AUX, 0xfffffeef, 0x010b0000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTCP_CHAN_STEER_LO, 0xffffffff, 0x00003120),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmVGT_CACHE_INVALIDATION, 0x3fff3af3, 0x19200000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmVGT_GS_MAX_WAVE_ID, 0x00000fff, 0x000000ff),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmWD_UTCL1_CNTL, 0x08000000, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCP_MEC1_F32_INT_DIS, 0x00000000, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCP_MEC2_F32_INT_DIS, 0x00000000, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCP_DEBUG, 0x00000000, 0x00008000)
};

static const struct soc15_reg_golden golden_settings_gc_9_1_rv1[] =
{
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_HW_CONTROL_3, 0x30000000, 0x10000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGB_ADDR_CONFIG, 0xffff77ff, 0x24000042),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGB_ADDR_CONFIG_READ, 0xffff77ff, 0x24000042),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE_1, 0xffffffff, 0x04048000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_MODE_CNTL_1, 0x06000000, 0x06000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRMI_UTCL1_CNTL2, 0x00030000, 0x00020000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTD_CNTL, 0x01bd9f33, 0x00000800)
};

static const struct soc15_reg_golden golden_settings_gc_9_1_rv2[] =
{
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_DCC_CONFIG, 0xff7fffff, 0x04000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_HW_CONTROL, 0xfffdf3cf, 0x00014104),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_HW_CONTROL_2, 0xff7fffff, 0x0a000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCPC_UTCL1_CNTL, 0x7f0fffff, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCPF_UTCL1_CNTL, 0xff8fffff, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCPG_UTCL1_CNTL, 0x7f8fffff, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGB_ADDR_CONFIG, 0xffff77ff, 0x26013041),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGB_ADDR_CONFIG_READ, 0xffff77ff, 0x26013041),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmIA_UTCL1_CNTL, 0x3f8fffff, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE_1, 0xffffffff, 0x04040000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_GPM_UTCL1_CNTL_0, 0xff0fffff, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_GPM_UTCL1_CNTL_1, 0xff0fffff, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_GPM_UTCL1_CNTL_2, 0xff0fffff, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_PREWALKER_UTCL1_CNTL, 0xff0fffff, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmRLC_SPM_UTCL1_CNTL, 0xff0fffff, 0x08000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTCP_CHAN_STEER_LO, 0xffffffff, 0x00000010),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTD_CNTL, 0x01bd9f33, 0x01000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmWD_UTCL1_CNTL, 0x3f8fffff, 0x08000080),
};

static const struct soc15_reg_golden golden_settings_gc_9_x_common[] =
{
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCP_SD_CNTL, 0xffffffff, 0x000001ff),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGRBM_CAM_INDEX, 0xffffffff, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGRBM_CAM_DATA, 0xffffffff, 0x2544c382)
};

static const struct soc15_reg_golden golden_settings_gc_9_2_1[] =
{
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmDB_DEBUG2, 0xf00fffff, 0x00000420),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGB_GPU_ID, 0x0000000f, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_BINNER_EVENT_CNTL_3, 0x00000003, 0x82400024),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE, 0x3fffffff, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSH_MEM_CONFIG, 0x00001000, 0x00001000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSPI_RESOURCE_RESERVE_CU_0, 0x0007ffff, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSPI_RESOURCE_RESERVE_CU_1, 0x0007ffff, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSPI_RESOURCE_RESERVE_EN_CU_0, 0x01ffffff, 0x0000ff87),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSPI_RESOURCE_RESERVE_EN_CU_1, 0x01ffffff, 0x0000ff8f),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSQC_CONFIG, 0x03000000, 0x020a2000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTA_CNTL_AUX, 0xfffffeef, 0x010b0000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTCP_CHAN_STEER_HI, 0xffffffff, 0x4a2c0e68),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTCP_CHAN_STEER_LO, 0xffffffff, 0xb5d3f197),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmVGT_CACHE_INVALIDATION, 0x3fff3af3, 0x19200000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmVGT_GS_MAX_WAVE_ID, 0x00000fff, 0x000003ff)
};

static const struct soc15_reg_golden golden_settings_gc_9_2_1_vg12[] =
{
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_DCC_CONFIG, 0x00000080, 0x04000080),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_HW_CONTROL, 0xfffdf3cf, 0x00014104),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCB_HW_CONTROL_2, 0x0f000000, 0x0a000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGB_ADDR_CONFIG, 0xffff77ff, 0x24104041),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmGB_ADDR_CONFIG_READ, 0xffff77ff, 0x24104041),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmPA_SC_ENHANCE_1, 0xffffffff, 0x04040000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSPI_CONFIG_CNTL_1, 0xffff03ff, 0x01000107),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTCP_CHAN_STEER_LO, 0xffffffff, 0x76325410),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmTD_CNTL, 0x01bd9f33, 0x01000000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCP_MEC1_F32_INT_DIS, 0x00000000, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCP_MEC2_F32_INT_DIS, 0x00000000, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmCP_DEBUG, 0x00000000, 0x00008000)
};

static const u32 GFX_RLC_SRM_INDEX_CNTL_ADDR_OFFSETS[] =
{
	mmRLC_SRM_INDEX_CNTL_ADDR_0 - mmRLC_SRM_INDEX_CNTL_ADDR_0,
	mmRLC_SRM_INDEX_CNTL_ADDR_1 - mmRLC_SRM_INDEX_CNTL_ADDR_0,
	mmRLC_SRM_INDEX_CNTL_ADDR_2 - mmRLC_SRM_INDEX_CNTL_ADDR_0,
	mmRLC_SRM_INDEX_CNTL_ADDR_3 - mmRLC_SRM_INDEX_CNTL_ADDR_0,
	mmRLC_SRM_INDEX_CNTL_ADDR_4 - mmRLC_SRM_INDEX_CNTL_ADDR_0,
	mmRLC_SRM_INDEX_CNTL_ADDR_5 - mmRLC_SRM_INDEX_CNTL_ADDR_0,
	mmRLC_SRM_INDEX_CNTL_ADDR_6 - mmRLC_SRM_INDEX_CNTL_ADDR_0,
	mmRLC_SRM_INDEX_CNTL_ADDR_7 - mmRLC_SRM_INDEX_CNTL_ADDR_0,
};

static const u32 GFX_RLC_SRM_INDEX_CNTL_DATA_OFFSETS[] =
{
	mmRLC_SRM_INDEX_CNTL_DATA_0 - mmRLC_SRM_INDEX_CNTL_DATA_0,
	mmRLC_SRM_INDEX_CNTL_DATA_1 - mmRLC_SRM_INDEX_CNTL_DATA_0,
	mmRLC_SRM_INDEX_CNTL_DATA_2 - mmRLC_SRM_INDEX_CNTL_DATA_0,
	mmRLC_SRM_INDEX_CNTL_DATA_3 - mmRLC_SRM_INDEX_CNTL_DATA_0,
	mmRLC_SRM_INDEX_CNTL_DATA_4 - mmRLC_SRM_INDEX_CNTL_DATA_0,
	mmRLC_SRM_INDEX_CNTL_DATA_5 - mmRLC_SRM_INDEX_CNTL_DATA_0,
	mmRLC_SRM_INDEX_CNTL_DATA_6 - mmRLC_SRM_INDEX_CNTL_DATA_0,
	mmRLC_SRM_INDEX_CNTL_DATA_7 - mmRLC_SRM_INDEX_CNTL_DATA_0,
};

#define VEGA10_GB_ADDR_CONFIG_GOLDEN 0x2a114042
#define VEGA12_GB_ADDR_CONFIG_GOLDEN 0x24104041
#define RAVEN_GB_ADDR_CONFIG_GOLDEN 0x24000042
#define RAVEN2_GB_ADDR_CONFIG_GOLDEN 0x26013041

static void gfx_v9_0_set_ring_funcs(struct amdgpu_device *adev);
static void gfx_v9_0_set_irq_funcs(struct amdgpu_device *adev);
static void gfx_v9_0_set_gds_init(struct amdgpu_device *adev);
static void gfx_v9_0_set_rlc_funcs(struct amdgpu_device *adev);
static int gfx_v9_0_get_cu_info(struct amdgpu_device *adev,
                                 struct amdgpu_cu_info *cu_info);
static uint64_t gfx_v9_0_get_gpu_clock_counter(struct amdgpu_device *adev);
static void gfx_v9_0_select_se_sh(struct amdgpu_device *adev, u32 se_num, u32 sh_num, u32 instance);
static void gfx_v9_0_ring_emit_de_meta(struct amdgpu_ring *ring);
static u64 gfx_v9_0_ring_get_rptr_compute(struct amdgpu_ring *ring);

static void gfx_v9_0_init_golden_registers(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_VEGA10:
		if (!amdgpu_virt_support_skip_setting(adev)) {
			soc15_program_register_sequence(adev,
							 golden_settings_gc_9_0,
							 ARRAY_SIZE(golden_settings_gc_9_0));
			soc15_program_register_sequence(adev,
							 golden_settings_gc_9_0_vg10,
							 ARRAY_SIZE(golden_settings_gc_9_0_vg10));
		}
		break;
	case CHIP_VEGA12:
		soc15_program_register_sequence(adev,
						golden_settings_gc_9_2_1,
						ARRAY_SIZE(golden_settings_gc_9_2_1));
		soc15_program_register_sequence(adev,
						golden_settings_gc_9_2_1_vg12,
						ARRAY_SIZE(golden_settings_gc_9_2_1_vg12));
		break;
	case CHIP_VEGA20:
		soc15_program_register_sequence(adev,
						golden_settings_gc_9_0,
						ARRAY_SIZE(golden_settings_gc_9_0));
		soc15_program_register_sequence(adev,
						golden_settings_gc_9_0_vg20,
						ARRAY_SIZE(golden_settings_gc_9_0_vg20));
		break;
	case CHIP_RAVEN:
		soc15_program_register_sequence(adev, golden_settings_gc_9_1,
						ARRAY_SIZE(golden_settings_gc_9_1));
		if (adev->rev_id >= 8)
			soc15_program_register_sequence(adev,
							golden_settings_gc_9_1_rv2,
							ARRAY_SIZE(golden_settings_gc_9_1_rv2));
		else
			soc15_program_register_sequence(adev,
							golden_settings_gc_9_1_rv1,
							ARRAY_SIZE(golden_settings_gc_9_1_rv1));
		break;
	default:
		break;
	}

	soc15_program_register_sequence(adev, golden_settings_gc_9_x_common,
					(const u32)ARRAY_SIZE(golden_settings_gc_9_x_common));
}

static void gfx_v9_0_scratch_init(struct amdgpu_device *adev)
{
	adev->gfx.scratch.num_reg = 8;
	adev->gfx.scratch.reg_base = SOC15_REG_OFFSET(GC, 0, mmSCRATCH_REG0);
	adev->gfx.scratch.free_mask = (1u << adev->gfx.scratch.num_reg) - 1;
}

static void gfx_v9_0_write_data_to_reg(struct amdgpu_ring *ring, int eng_sel,
				       bool wc, uint32_t reg, uint32_t val)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, WRITE_DATA_ENGINE_SEL(eng_sel) |
				WRITE_DATA_DST_SEL(0) |
				(wc ? WR_CONFIRM : 0));
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, val);
}

static void gfx_v9_0_wait_reg_mem(struct amdgpu_ring *ring, int eng_sel,
				  int mem_space, int opt, uint32_t addr0,
				  uint32_t addr1, uint32_t ref, uint32_t mask,
				  uint32_t inv)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_WAIT_REG_MEM, 5));
	amdgpu_ring_write(ring,
				 /* memory (1) or register (0) */
				 (WAIT_REG_MEM_MEM_SPACE(mem_space) |
				 WAIT_REG_MEM_OPERATION(opt) | /* wait */
				 WAIT_REG_MEM_FUNCTION(3) |  /* equal */
				 WAIT_REG_MEM_ENGINE(eng_sel)));

	if (mem_space)
		BUG_ON(addr0 & 0x3); /* Dword align */
	amdgpu_ring_write(ring, addr0);
	amdgpu_ring_write(ring, addr1);
	amdgpu_ring_write(ring, ref);
	amdgpu_ring_write(ring, mask);
	amdgpu_ring_write(ring, inv); /* poll interval */
}

static int gfx_v9_0_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t scratch;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	r = amdgpu_gfx_scratch_get(adev, &scratch);
	if (r)
		return r;

	WREG32(scratch, 0xCAFEDEAD);
	r = amdgpu_ring_alloc(ring, 3);
	if (r)
		goto error_free_scratch;

	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_UCONFIG_REG, 1));
	amdgpu_ring_write(ring, (scratch - PACKET3_SET_UCONFIG_REG_START));
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(scratch);
		if (tmp == 0xDEADBEEF)
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;

error_free_scratch:
	amdgpu_gfx_scratch_free(adev, scratch);
	return r;
}

static int gfx_v9_0_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;

	unsigned index;
	uint64_t gpu_addr;
	uint32_t tmp;
	long r;

	r = amdgpu_device_wb_get(adev, &index);
	if (r)
		return r;

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	adev->wb.wb[index] = cpu_to_le32(0xCAFEDEAD);
	memset(&ib, 0, sizeof(ib));
	r = amdgpu_ib_get(adev, NULL, 16, &ib);
	if (r)
		goto err1;

	ib.ptr[0] = PACKET3(PACKET3_WRITE_DATA, 3);
	ib.ptr[1] = WRITE_DATA_DST_SEL(5) | WR_CONFIRM;
	ib.ptr[2] = lower_32_bits(gpu_addr);
	ib.ptr[3] = upper_32_bits(gpu_addr);
	ib.ptr[4] = 0xDEADBEEF;
	ib.length_dw = 5;

	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, &f);
	if (r)
		goto err2;

	r = dma_fence_wait_timeout(f, false, timeout);
	if (r == 0) {
		r = -ETIMEDOUT;
		goto err2;
	} else if (r < 0) {
		goto err2;
	}

	tmp = adev->wb.wb[index];
	if (tmp == 0xDEADBEEF)
		r = 0;
	else
		r = -EINVAL;

err2:
	amdgpu_ib_free(adev, &ib, NULL);
	dma_fence_put(f);
err1:
	amdgpu_device_wb_free(adev, index);
	return r;
}


static void gfx_v9_0_free_microcode(struct amdgpu_device *adev)
{
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

	kfree(adev->gfx.rlc.register_list_format);
}

static void gfx_v9_0_init_rlc_ext_microcode(struct amdgpu_device *adev)
{
	const struct rlc_firmware_header_v2_1 *rlc_hdr;

	rlc_hdr = (const struct rlc_firmware_header_v2_1 *)adev->gfx.rlc_fw->data;
	adev->gfx.rlc_srlc_fw_version = le32_to_cpu(rlc_hdr->save_restore_list_cntl_ucode_ver);
	adev->gfx.rlc_srlc_feature_version = le32_to_cpu(rlc_hdr->save_restore_list_cntl_feature_ver);
	adev->gfx.rlc.save_restore_list_cntl_size_bytes = le32_to_cpu(rlc_hdr->save_restore_list_cntl_size_bytes);
	adev->gfx.rlc.save_restore_list_cntl = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->save_restore_list_cntl_offset_bytes);
	adev->gfx.rlc_srlg_fw_version = le32_to_cpu(rlc_hdr->save_restore_list_gpm_ucode_ver);
	adev->gfx.rlc_srlg_feature_version = le32_to_cpu(rlc_hdr->save_restore_list_gpm_feature_ver);
	adev->gfx.rlc.save_restore_list_gpm_size_bytes = le32_to_cpu(rlc_hdr->save_restore_list_gpm_size_bytes);
	adev->gfx.rlc.save_restore_list_gpm = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->save_restore_list_gpm_offset_bytes);
	adev->gfx.rlc_srls_fw_version = le32_to_cpu(rlc_hdr->save_restore_list_srm_ucode_ver);
	adev->gfx.rlc_srls_feature_version = le32_to_cpu(rlc_hdr->save_restore_list_srm_feature_ver);
	adev->gfx.rlc.save_restore_list_srm_size_bytes = le32_to_cpu(rlc_hdr->save_restore_list_srm_size_bytes);
	adev->gfx.rlc.save_restore_list_srm = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->save_restore_list_srm_offset_bytes);
	adev->gfx.rlc.reg_list_format_direct_reg_list_length =
			le32_to_cpu(rlc_hdr->reg_list_format_direct_reg_list_length);
}

static void gfx_v9_0_check_fw_write_wait(struct amdgpu_device *adev)
{
	adev->gfx.me_fw_write_wait = false;
	adev->gfx.mec_fw_write_wait = false;

	switch (adev->asic_type) {
	case CHIP_VEGA10:
		if ((adev->gfx.me_fw_version >= 0x0000009c) &&
		    (adev->gfx.me_feature_version >= 42) &&
		    (adev->gfx.pfp_fw_version >=  0x000000b1) &&
		    (adev->gfx.pfp_feature_version >= 42))
			adev->gfx.me_fw_write_wait = true;

		if ((adev->gfx.mec_fw_version >=  0x00000193) &&
		    (adev->gfx.mec_feature_version >= 42))
			adev->gfx.mec_fw_write_wait = true;
		break;
	case CHIP_VEGA12:
		if ((adev->gfx.me_fw_version >= 0x0000009c) &&
		    (adev->gfx.me_feature_version >= 44) &&
		    (adev->gfx.pfp_fw_version >=  0x000000b2) &&
		    (adev->gfx.pfp_feature_version >= 44))
			adev->gfx.me_fw_write_wait = true;

		if ((adev->gfx.mec_fw_version >=  0x00000196) &&
		    (adev->gfx.mec_feature_version >= 44))
			adev->gfx.mec_fw_write_wait = true;
		break;
	case CHIP_VEGA20:
		if ((adev->gfx.me_fw_version >= 0x0000009c) &&
		    (adev->gfx.me_feature_version >= 44) &&
		    (adev->gfx.pfp_fw_version >=  0x000000b2) &&
		    (adev->gfx.pfp_feature_version >= 44))
			adev->gfx.me_fw_write_wait = true;

		if ((adev->gfx.mec_fw_version >=  0x00000197) &&
		    (adev->gfx.mec_feature_version >= 44))
			adev->gfx.mec_fw_write_wait = true;
		break;
	case CHIP_RAVEN:
		if ((adev->gfx.me_fw_version >= 0x0000009c) &&
		    (adev->gfx.me_feature_version >= 42) &&
		    (adev->gfx.pfp_fw_version >=  0x000000b1) &&
		    (adev->gfx.pfp_feature_version >= 42))
			adev->gfx.me_fw_write_wait = true;

		if ((adev->gfx.mec_fw_version >=  0x00000192) &&
		    (adev->gfx.mec_feature_version >= 42))
			adev->gfx.mec_fw_write_wait = true;
		break;
	default:
		break;
	}
}

static void gfx_v9_0_check_if_need_gfxoff(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
		break;
	case CHIP_RAVEN:
		if (adev->rev_id >= 0x8 || adev->pdev->device == 0x15d8)
			break;
		if ((adev->gfx.rlc_fw_version != 106 &&
		     adev->gfx.rlc_fw_version < 531) ||
		    (adev->gfx.rlc_fw_version == 53815) ||
		    (adev->gfx.rlc_feature_version < 1) ||
		    !adev->gfx.rlc.is_rlc_v2_1)
			adev->pm.pp_feature &= ~PP_GFXOFF_MASK;
		break;
	default:
		break;
	}
}

static int gfx_v9_0_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[30];
	int err;
	struct amdgpu_firmware_info *info = NULL;
	const struct common_firmware_header *header = NULL;
	const struct gfx_firmware_header_v1_0 *cp_hdr;
	const struct rlc_firmware_header_v2_0 *rlc_hdr;
	unsigned int *tmp = NULL;
	unsigned int i = 0;
	uint16_t version_major;
	uint16_t version_minor;
	uint32_t smu_version;

	DRM_DEBUG("\n");

	switch (adev->asic_type) {
	case CHIP_VEGA10:
		chip_name = "vega10";
		break;
	case CHIP_VEGA12:
		chip_name = "vega12";
		break;
	case CHIP_VEGA20:
		chip_name = "vega20";
		break;
	case CHIP_RAVEN:
		if (adev->rev_id >= 8)
			chip_name = "raven2";
		else if (adev->pdev->device == 0x15d8)
			chip_name = "picasso";
		else
			chip_name = "raven";
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

	/*
	 * For Picasso && AM4 SOCKET board, we use picasso_rlc_am4.bin
	 * instead of picasso_rlc.bin.
	 * Judgment method:
	 * PCO AM4: revision >= 0xC8 && revision <= 0xCF
	 *          or revision >= 0xD8 && revision <= 0xDF
	 * otherwise is PCO FP5
	 */
	if (!strcmp(chip_name, "picasso") &&
		(((adev->pdev->revision >= 0xC8) && (adev->pdev->revision <= 0xCF)) ||
		((adev->pdev->revision >= 0xD8) && (adev->pdev->revision <= 0xDF))))
		snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_rlc_am4.bin", chip_name);
	else if (!strcmp(chip_name, "raven") && (amdgpu_pm_load_smu_firmware(adev, &smu_version) == 0) &&
		(smu_version >= 0x41e2b))
		/**
		*SMC is loaded by SBIOS on APU and it's able to get the SMU version directly.
		*/
		snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_kicker_rlc.bin", chip_name);
	else
		snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_rlc.bin", chip_name);
	err = request_firmware(&adev->gfx.rlc_fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->gfx.rlc_fw);
	rlc_hdr = (const struct rlc_firmware_header_v2_0 *)adev->gfx.rlc_fw->data;

	version_major = le16_to_cpu(rlc_hdr->header.header_version_major);
	version_minor = le16_to_cpu(rlc_hdr->header.header_version_minor);
	if (version_major == 2 && version_minor == 1)
		adev->gfx.rlc.is_rlc_v2_1 = true;

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
	for (i = 0 ; i < (adev->gfx.rlc.reg_list_format_size_bytes >> 2); i++)
		adev->gfx.rlc.register_list_format[i] =	le32_to_cpu(tmp[i]);

	adev->gfx.rlc.register_restore = adev->gfx.rlc.register_list_format + i;

	tmp = (unsigned int *)((uintptr_t)rlc_hdr +
			le32_to_cpu(rlc_hdr->reg_list_array_offset_bytes));
	for (i = 0 ; i < (adev->gfx.rlc.reg_list_size_bytes >> 2); i++)
		adev->gfx.rlc.register_restore[i] = le32_to_cpu(tmp[i]);

	if (adev->gfx.rlc.is_rlc_v2_1)
		gfx_v9_0_init_rlc_ext_microcode(adev);

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

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
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

		if (adev->gfx.rlc.is_rlc_v2_1 &&
		    adev->gfx.rlc.save_restore_list_cntl_size_bytes &&
		    adev->gfx.rlc.save_restore_list_gpm_size_bytes &&
		    adev->gfx.rlc.save_restore_list_srm_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL];
			info->ucode_id = AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.save_restore_list_cntl_size_bytes, PAGE_SIZE);

			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM];
			info->ucode_id = AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.save_restore_list_gpm_size_bytes, PAGE_SIZE);

			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM];
			info->ucode_id = AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.save_restore_list_srm_size_bytes, PAGE_SIZE);
		}

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_MEC1];
		info->ucode_id = AMDGPU_UCODE_ID_CP_MEC1;
		info->fw = adev->gfx.mec_fw;
		header = (const struct common_firmware_header *)info->fw->data;
		cp_hdr = (const struct gfx_firmware_header_v1_0 *)info->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes) - le32_to_cpu(cp_hdr->jt_size) * 4, PAGE_SIZE);

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_MEC1_JT];
		info->ucode_id = AMDGPU_UCODE_ID_CP_MEC1_JT;
		info->fw = adev->gfx.mec_fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(cp_hdr->jt_size) * 4, PAGE_SIZE);

		if (adev->gfx.mec2_fw) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_MEC2];
			info->ucode_id = AMDGPU_UCODE_ID_CP_MEC2;
			info->fw = adev->gfx.mec2_fw;
			header = (const struct common_firmware_header *)info->fw->data;
			cp_hdr = (const struct gfx_firmware_header_v1_0 *)info->fw->data;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(header->ucode_size_bytes) - le32_to_cpu(cp_hdr->jt_size) * 4, PAGE_SIZE);
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_CP_MEC2_JT];
			info->ucode_id = AMDGPU_UCODE_ID_CP_MEC2_JT;
			info->fw = adev->gfx.mec2_fw;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(cp_hdr->jt_size) * 4, PAGE_SIZE);
		}

	}

out:
	gfx_v9_0_check_if_need_gfxoff(adev);
	gfx_v9_0_check_fw_write_wait(adev);
	if (err) {
		dev_err(adev->dev,
			"gfx9: Failed to load firmware \"%s\"\n",
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

static u32 gfx_v9_0_get_csb_size(struct amdgpu_device *adev)
{
	u32 count = 0;
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;

	/* begin clear state */
	count += 2;
	/* context control state */
	count += 3;

	for (sect = gfx9_cs_data; sect->section != NULL; ++sect) {
		for (ext = sect->section; ext->extent != NULL; ++ext) {
			if (sect->id == SECT_CONTEXT)
				count += 2 + ext->reg_count;
			else
				return 0;
		}
	}

	/* end clear state */
	count += 2;
	/* clear state */
	count += 2;

	return count;
}

static void gfx_v9_0_get_csb_buffer(struct amdgpu_device *adev,
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

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	buffer[count++] = cpu_to_le32(PACKET3_PREAMBLE_END_CLEAR_STATE);

	buffer[count++] = cpu_to_le32(PACKET3(PACKET3_CLEAR_STATE, 0));
	buffer[count++] = cpu_to_le32(0);
}

static void gfx_v9_0_init_always_on_cu_mask(struct amdgpu_device *adev)
{
	struct amdgpu_cu_info *cu_info = &adev->gfx.cu_info;
	uint32_t pg_always_on_cu_num = 2;
	uint32_t always_on_cu_num;
	uint32_t i, j, k;
	uint32_t mask, cu_bitmap, counter;

	if (adev->flags & AMD_IS_APU)
		always_on_cu_num = 4;
	else if (adev->asic_type == CHIP_VEGA12)
		always_on_cu_num = 8;
	else
		always_on_cu_num = 12;

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			mask = 1;
			cu_bitmap = 0;
			counter = 0;
			gfx_v9_0_select_se_sh(adev, i, j, 0xffffffff);

			for (k = 0; k < adev->gfx.config.max_cu_per_sh; k ++) {
				if (cu_info->bitmap[i][j] & mask) {
					if (counter == pg_always_on_cu_num)
						WREG32_SOC15(GC, 0, mmRLC_PG_ALWAYS_ON_CU_MASK, cu_bitmap);
					if (counter < always_on_cu_num)
						cu_bitmap |= mask;
					else
						break;
					counter++;
				}
				mask <<= 1;
			}

			WREG32_SOC15(GC, 0, mmRLC_LB_ALWAYS_ACTIVE_CU_MASK, cu_bitmap);
			cu_info->ao_cu_bitmap[i][j] = cu_bitmap;
		}
	}
	gfx_v9_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);
}

static void gfx_v9_0_init_lbpw(struct amdgpu_device *adev)
{
	uint32_t data;

	/* set mmRLC_LB_THR_CONFIG_1/2/3/4 */
	WREG32_SOC15(GC, 0, mmRLC_LB_THR_CONFIG_1, 0x0000007F);
	WREG32_SOC15(GC, 0, mmRLC_LB_THR_CONFIG_2, 0x0333A5A7);
	WREG32_SOC15(GC, 0, mmRLC_LB_THR_CONFIG_3, 0x00000077);
	WREG32_SOC15(GC, 0, mmRLC_LB_THR_CONFIG_4, (0x30 | 0x40 << 8 | 0x02FA << 16));

	/* set mmRLC_LB_CNTR_INIT = 0x0000_0000 */
	WREG32_SOC15(GC, 0, mmRLC_LB_CNTR_INIT, 0x00000000);

	/* set mmRLC_LB_CNTR_MAX = 0x0000_0500 */
	WREG32_SOC15(GC, 0, mmRLC_LB_CNTR_MAX, 0x00000500);

	mutex_lock(&adev->grbm_idx_mutex);
	/* set mmRLC_LB_INIT_CU_MASK thru broadcast mode to enable all SE/SH*/
	gfx_v9_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	WREG32_SOC15(GC, 0, mmRLC_LB_INIT_CU_MASK, 0xffffffff);

	/* set mmRLC_LB_PARAMS = 0x003F_1006 */
	data = REG_SET_FIELD(0, RLC_LB_PARAMS, FIFO_SAMPLES, 0x0003);
	data |= REG_SET_FIELD(data, RLC_LB_PARAMS, PG_IDLE_SAMPLES, 0x0010);
	data |= REG_SET_FIELD(data, RLC_LB_PARAMS, PG_IDLE_SAMPLE_INTERVAL, 0x033F);
	WREG32_SOC15(GC, 0, mmRLC_LB_PARAMS, data);

	/* set mmRLC_GPM_GENERAL_7[31-16] = 0x00C0 */
	data = RREG32_SOC15(GC, 0, mmRLC_GPM_GENERAL_7);
	data &= 0x0000FFFF;
	data |= 0x00C00000;
	WREG32_SOC15(GC, 0, mmRLC_GPM_GENERAL_7, data);

	/*
	 * RLC_LB_ALWAYS_ACTIVE_CU_MASK = 0xF (4 CUs AON for Raven),
	 * programmed in gfx_v9_0_init_always_on_cu_mask()
	 */

	/* set RLC_LB_CNTL = 0x8000_0095, 31 bit is reserved,
	 * but used for RLC_LB_CNTL configuration */
	data = RLC_LB_CNTL__LB_CNT_SPIM_ACTIVE_MASK;
	data |= REG_SET_FIELD(data, RLC_LB_CNTL, CU_MASK_USED_OFF_HYST, 0x09);
	data |= REG_SET_FIELD(data, RLC_LB_CNTL, RESERVED, 0x80000);
	WREG32_SOC15(GC, 0, mmRLC_LB_CNTL, data);
	mutex_unlock(&adev->grbm_idx_mutex);

	gfx_v9_0_init_always_on_cu_mask(adev);
}

static void gfx_v9_4_init_lbpw(struct amdgpu_device *adev)
{
	uint32_t data;

	/* set mmRLC_LB_THR_CONFIG_1/2/3/4 */
	WREG32_SOC15(GC, 0, mmRLC_LB_THR_CONFIG_1, 0x0000007F);
	WREG32_SOC15(GC, 0, mmRLC_LB_THR_CONFIG_2, 0x033388F8);
	WREG32_SOC15(GC, 0, mmRLC_LB_THR_CONFIG_3, 0x00000077);
	WREG32_SOC15(GC, 0, mmRLC_LB_THR_CONFIG_4, (0x10 | 0x27 << 8 | 0x02FA << 16));

	/* set mmRLC_LB_CNTR_INIT = 0x0000_0000 */
	WREG32_SOC15(GC, 0, mmRLC_LB_CNTR_INIT, 0x00000000);

	/* set mmRLC_LB_CNTR_MAX = 0x0000_0500 */
	WREG32_SOC15(GC, 0, mmRLC_LB_CNTR_MAX, 0x00000800);

	mutex_lock(&adev->grbm_idx_mutex);
	/* set mmRLC_LB_INIT_CU_MASK thru broadcast mode to enable all SE/SH*/
	gfx_v9_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	WREG32_SOC15(GC, 0, mmRLC_LB_INIT_CU_MASK, 0xffffffff);

	/* set mmRLC_LB_PARAMS = 0x003F_1006 */
	data = REG_SET_FIELD(0, RLC_LB_PARAMS, FIFO_SAMPLES, 0x0003);
	data |= REG_SET_FIELD(data, RLC_LB_PARAMS, PG_IDLE_SAMPLES, 0x0010);
	data |= REG_SET_FIELD(data, RLC_LB_PARAMS, PG_IDLE_SAMPLE_INTERVAL, 0x033F);
	WREG32_SOC15(GC, 0, mmRLC_LB_PARAMS, data);

	/* set mmRLC_GPM_GENERAL_7[31-16] = 0x00C0 */
	data = RREG32_SOC15(GC, 0, mmRLC_GPM_GENERAL_7);
	data &= 0x0000FFFF;
	data |= 0x00C00000;
	WREG32_SOC15(GC, 0, mmRLC_GPM_GENERAL_7, data);

	/*
	 * RLC_LB_ALWAYS_ACTIVE_CU_MASK = 0xFFF (12 CUs AON),
	 * programmed in gfx_v9_0_init_always_on_cu_mask()
	 */

	/* set RLC_LB_CNTL = 0x8000_0095, 31 bit is reserved,
	 * but used for RLC_LB_CNTL configuration */
	data = RLC_LB_CNTL__LB_CNT_SPIM_ACTIVE_MASK;
	data |= REG_SET_FIELD(data, RLC_LB_CNTL, CU_MASK_USED_OFF_HYST, 0x09);
	data |= REG_SET_FIELD(data, RLC_LB_CNTL, RESERVED, 0x80000);
	WREG32_SOC15(GC, 0, mmRLC_LB_CNTL, data);
	mutex_unlock(&adev->grbm_idx_mutex);

	gfx_v9_0_init_always_on_cu_mask(adev);
}

static void gfx_v9_0_enable_lbpw(struct amdgpu_device *adev, bool enable)
{
	WREG32_FIELD15(GC, 0, RLC_LB_CNTL, LOAD_BALANCE_ENABLE, enable ? 1 : 0);
}

static int gfx_v9_0_cp_jump_table_num(struct amdgpu_device *adev)
{
	return 5;
}

static int gfx_v9_0_rlc_init(struct amdgpu_device *adev)
{
	const struct cs_section_def *cs_data;
	int r;

	adev->gfx.rlc.cs_data = gfx9_cs_data;

	cs_data = adev->gfx.rlc.cs_data;

	if (cs_data) {
		/* init clear state block */
		r = amdgpu_gfx_rlc_init_csb(adev);
		if (r)
			return r;
	}

	if (adev->asic_type == CHIP_RAVEN) {
		/* TODO: double check the cp_table_size for RV */
		adev->gfx.rlc.cp_table_size = ALIGN(96 * 5 * 4, 2048) + (64 * 1024); /* JT + GDS */
		r = amdgpu_gfx_rlc_init_cpt(adev);
		if (r)
			return r;
	}

	switch (adev->asic_type) {
	case CHIP_RAVEN:
		gfx_v9_0_init_lbpw(adev);
		break;
	case CHIP_VEGA20:
		gfx_v9_4_init_lbpw(adev);
		break;
	default:
		break;
	}

	return 0;
}

static int gfx_v9_0_csb_vram_pin(struct amdgpu_device *adev)
{
	int r;

	r = amdgpu_bo_reserve(adev->gfx.rlc.clear_state_obj, false);
	if (unlikely(r != 0))
		return r;

	r = amdgpu_bo_pin(adev->gfx.rlc.clear_state_obj,
			AMDGPU_GEM_DOMAIN_VRAM);
	if (!r)
		adev->gfx.rlc.clear_state_gpu_addr =
			amdgpu_bo_gpu_offset(adev->gfx.rlc.clear_state_obj);

	amdgpu_bo_unreserve(adev->gfx.rlc.clear_state_obj);

	return r;
}

static void gfx_v9_0_csb_vram_unpin(struct amdgpu_device *adev)
{
	int r;

	if (!adev->gfx.rlc.clear_state_obj)
		return;

	r = amdgpu_bo_reserve(adev->gfx.rlc.clear_state_obj, true);
	if (likely(r == 0)) {
		amdgpu_bo_unpin(adev->gfx.rlc.clear_state_obj);
		amdgpu_bo_unreserve(adev->gfx.rlc.clear_state_obj);
	}
}

static void gfx_v9_0_mec_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_free_kernel(&adev->gfx.mec.hpd_eop_obj, NULL, NULL);
	amdgpu_bo_free_kernel(&adev->gfx.mec.mec_fw_obj, NULL, NULL);
}

static int gfx_v9_0_mec_init(struct amdgpu_device *adev)
{
	int r;
	u32 *hpd;
	const __le32 *fw_data;
	unsigned fw_size;
	u32 *fw;
	size_t mec_hpd_size;

	const struct gfx_firmware_header_v1_0 *mec_hdr;

	bitmap_zero(adev->gfx.mec.queue_bitmap, AMDGPU_MAX_COMPUTE_QUEUES);

	/* take ownership of the relevant compute queues */
	amdgpu_gfx_compute_queue_acquire(adev);
	mec_hpd_size = adev->gfx.num_compute_rings * GFX9_MEC_HPD_SIZE;

	r = amdgpu_bo_create_reserved(adev, mec_hpd_size, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &adev->gfx.mec.hpd_eop_obj,
				      &adev->gfx.mec.hpd_eop_gpu_addr,
				      (void **)&hpd);
	if (r) {
		dev_warn(adev->dev, "(%d) create HDP EOP bo failed\n", r);
		gfx_v9_0_mec_fini(adev);
		return r;
	}

	memset(hpd, 0, adev->gfx.mec.hpd_eop_obj->tbo.mem.size);

	amdgpu_bo_kunmap(adev->gfx.mec.hpd_eop_obj);
	amdgpu_bo_unreserve(adev->gfx.mec.hpd_eop_obj);

	mec_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.mec_fw->data;

	fw_data = (const __le32 *)
		(adev->gfx.mec_fw->data +
		 le32_to_cpu(mec_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(mec_hdr->header.ucode_size_bytes) / 4;

	r = amdgpu_bo_create_reserved(adev, mec_hdr->header.ucode_size_bytes,
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
				      &adev->gfx.mec.mec_fw_obj,
				      &adev->gfx.mec.mec_fw_gpu_addr,
				      (void **)&fw);
	if (r) {
		dev_warn(adev->dev, "(%d) create mec firmware bo failed\n", r);
		gfx_v9_0_mec_fini(adev);
		return r;
	}

	memcpy(fw, fw_data, fw_size);

	amdgpu_bo_kunmap(adev->gfx.mec.mec_fw_obj);
	amdgpu_bo_unreserve(adev->gfx.mec.mec_fw_obj);

	return 0;
}

static uint32_t wave_read_ind(struct amdgpu_device *adev, uint32_t simd, uint32_t wave, uint32_t address)
{
	WREG32_SOC15(GC, 0, mmSQ_IND_INDEX,
		(wave << SQ_IND_INDEX__WAVE_ID__SHIFT) |
		(simd << SQ_IND_INDEX__SIMD_ID__SHIFT) |
		(address << SQ_IND_INDEX__INDEX__SHIFT) |
		(SQ_IND_INDEX__FORCE_READ_MASK));
	return RREG32_SOC15(GC, 0, mmSQ_IND_DATA);
}

static void wave_read_regs(struct amdgpu_device *adev, uint32_t simd,
			   uint32_t wave, uint32_t thread,
			   uint32_t regno, uint32_t num, uint32_t *out)
{
	WREG32_SOC15(GC, 0, mmSQ_IND_INDEX,
		(wave << SQ_IND_INDEX__WAVE_ID__SHIFT) |
		(simd << SQ_IND_INDEX__SIMD_ID__SHIFT) |
		(regno << SQ_IND_INDEX__INDEX__SHIFT) |
		(thread << SQ_IND_INDEX__THREAD_ID__SHIFT) |
		(SQ_IND_INDEX__FORCE_READ_MASK) |
		(SQ_IND_INDEX__AUTO_INCR_MASK));
	while (num--)
		*(out++) = RREG32_SOC15(GC, 0, mmSQ_IND_DATA);
}

static void gfx_v9_0_read_wave_data(struct amdgpu_device *adev, uint32_t simd, uint32_t wave, uint32_t *dst, int *no_fields)
{
	/* type 1 wave data */
	dst[(*no_fields)++] = 1;
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_STATUS);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_PC_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_PC_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_EXEC_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_EXEC_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_HW_ID);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_INST_DW0);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_INST_DW1);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_GPR_ALLOC);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_LDS_ALLOC);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_TRAPSTS);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_IB_STS);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_IB_DBG0);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_M0);
}

static void gfx_v9_0_read_wave_sgprs(struct amdgpu_device *adev, uint32_t simd,
				     uint32_t wave, uint32_t start,
				     uint32_t size, uint32_t *dst)
{
	wave_read_regs(
		adev, simd, wave, 0,
		start + SQIND_WAVE_SGPRS_OFFSET, size, dst);
}

static void gfx_v9_0_read_wave_vgprs(struct amdgpu_device *adev, uint32_t simd,
				     uint32_t wave, uint32_t thread,
				     uint32_t start, uint32_t size,
				     uint32_t *dst)
{
	wave_read_regs(
		adev, simd, wave, thread,
		start + SQIND_WAVE_VGPRS_OFFSET, size, dst);
}

static void gfx_v9_0_select_me_pipe_q(struct amdgpu_device *adev,
				  u32 me, u32 pipe, u32 q, u32 vm)
{
	soc15_grbm_select(adev, me, pipe, q, vm);
}

static const struct amdgpu_gfx_funcs gfx_v9_0_gfx_funcs = {
	.get_gpu_clock_counter = &gfx_v9_0_get_gpu_clock_counter,
	.select_se_sh = &gfx_v9_0_select_se_sh,
	.read_wave_data = &gfx_v9_0_read_wave_data,
	.read_wave_sgprs = &gfx_v9_0_read_wave_sgprs,
	.read_wave_vgprs = &gfx_v9_0_read_wave_vgprs,
	.select_me_pipe_q = &gfx_v9_0_select_me_pipe_q
};

static int gfx_v9_0_gpu_early_init(struct amdgpu_device *adev)
{
	u32 gb_addr_config;
	int err;

	adev->gfx.funcs = &gfx_v9_0_gfx_funcs;

	switch (adev->asic_type) {
	case CHIP_VEGA10:
		adev->gfx.config.max_hw_contexts = 8;
		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x4C0;
		gb_addr_config = VEGA10_GB_ADDR_CONFIG_GOLDEN;
		break;
	case CHIP_VEGA12:
		adev->gfx.config.max_hw_contexts = 8;
		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x4C0;
		gb_addr_config = VEGA12_GB_ADDR_CONFIG_GOLDEN;
		DRM_INFO("fix gfx.config for vega12\n");
		break;
	case CHIP_VEGA20:
		adev->gfx.config.max_hw_contexts = 8;
		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x4C0;
		gb_addr_config = RREG32_SOC15(GC, 0, mmGB_ADDR_CONFIG);
		gb_addr_config &= ~0xf3e777ff;
		gb_addr_config |= 0x22014042;
		/* check vbios table if gpu info is not available */
		err = amdgpu_atomfirmware_get_gfx_info(adev);
		if (err)
			return err;
		break;
	case CHIP_RAVEN:
		adev->gfx.config.max_hw_contexts = 8;
		adev->gfx.config.sc_prim_fifo_size_frontend = 0x20;
		adev->gfx.config.sc_prim_fifo_size_backend = 0x100;
		adev->gfx.config.sc_hiz_tile_fifo_size = 0x30;
		adev->gfx.config.sc_earlyz_tile_fifo_size = 0x4C0;
		if (adev->rev_id >= 8)
			gb_addr_config = RAVEN2_GB_ADDR_CONFIG_GOLDEN;
		else
			gb_addr_config = RAVEN_GB_ADDR_CONFIG_GOLDEN;
		break;
	default:
		BUG();
		break;
	}

	adev->gfx.config.gb_addr_config = gb_addr_config;

	adev->gfx.config.gb_addr_config_fields.num_pipes = 1 <<
			REG_GET_FIELD(
					adev->gfx.config.gb_addr_config,
					GB_ADDR_CONFIG,
					NUM_PIPES);

	adev->gfx.config.max_tile_pipes =
		adev->gfx.config.gb_addr_config_fields.num_pipes;

	adev->gfx.config.gb_addr_config_fields.num_banks = 1 <<
			REG_GET_FIELD(
					adev->gfx.config.gb_addr_config,
					GB_ADDR_CONFIG,
					NUM_BANKS);
	adev->gfx.config.gb_addr_config_fields.max_compress_frags = 1 <<
			REG_GET_FIELD(
					adev->gfx.config.gb_addr_config,
					GB_ADDR_CONFIG,
					MAX_COMPRESSED_FRAGS);
	adev->gfx.config.gb_addr_config_fields.num_rb_per_se = 1 <<
			REG_GET_FIELD(
					adev->gfx.config.gb_addr_config,
					GB_ADDR_CONFIG,
					NUM_RB_PER_SE);
	adev->gfx.config.gb_addr_config_fields.num_se = 1 <<
			REG_GET_FIELD(
					adev->gfx.config.gb_addr_config,
					GB_ADDR_CONFIG,
					NUM_SHADER_ENGINES);
	adev->gfx.config.gb_addr_config_fields.pipe_interleave_size = 1 << (8 +
			REG_GET_FIELD(
					adev->gfx.config.gb_addr_config,
					GB_ADDR_CONFIG,
					PIPE_INTERLEAVE_SIZE));

	return 0;
}

static int gfx_v9_0_ngg_create_buf(struct amdgpu_device *adev,
				   struct amdgpu_ngg_buf *ngg_buf,
				   int size_se,
				   int default_size_se)
{
	int r;

	if (size_se < 0) {
		dev_err(adev->dev, "Buffer size is invalid: %d\n", size_se);
		return -EINVAL;
	}
	size_se = size_se ? size_se : default_size_se;

	ngg_buf->size = size_se * adev->gfx.config.max_shader_engines;
	r = amdgpu_bo_create_kernel(adev, ngg_buf->size,
				    PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM,
				    &ngg_buf->bo,
				    &ngg_buf->gpu_addr,
				    NULL);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create NGG buffer\n", r);
		return r;
	}
	ngg_buf->bo_size = amdgpu_bo_size(ngg_buf->bo);

	return r;
}

static int gfx_v9_0_ngg_fini(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < NGG_BUF_MAX; i++)
		amdgpu_bo_free_kernel(&adev->gfx.ngg.buf[i].bo,
				      &adev->gfx.ngg.buf[i].gpu_addr,
				      NULL);

	memset(&adev->gfx.ngg.buf[0], 0,
			sizeof(struct amdgpu_ngg_buf) * NGG_BUF_MAX);

	adev->gfx.ngg.init = false;

	return 0;
}

static int gfx_v9_0_ngg_init(struct amdgpu_device *adev)
{
	int r;

	if (!amdgpu_ngg || adev->gfx.ngg.init == true)
		return 0;

	/* GDS reserve memory: 64 bytes alignment */
	adev->gfx.ngg.gds_reserve_size = ALIGN(5 * 4, 0x40);
	adev->gds.gds_size -= adev->gfx.ngg.gds_reserve_size;
	adev->gfx.ngg.gds_reserve_addr = RREG32_SOC15(GC, 0, mmGDS_VMID0_BASE);
	adev->gfx.ngg.gds_reserve_addr += RREG32_SOC15(GC, 0, mmGDS_VMID0_SIZE);

	/* Primitive Buffer */
	r = gfx_v9_0_ngg_create_buf(adev, &adev->gfx.ngg.buf[NGG_PRIM],
				    amdgpu_prim_buf_per_se,
				    64 * 1024);
	if (r) {
		dev_err(adev->dev, "Failed to create Primitive Buffer\n");
		goto err;
	}

	/* Position Buffer */
	r = gfx_v9_0_ngg_create_buf(adev, &adev->gfx.ngg.buf[NGG_POS],
				    amdgpu_pos_buf_per_se,
				    256 * 1024);
	if (r) {
		dev_err(adev->dev, "Failed to create Position Buffer\n");
		goto err;
	}

	/* Control Sideband */
	r = gfx_v9_0_ngg_create_buf(adev, &adev->gfx.ngg.buf[NGG_CNTL],
				    amdgpu_cntl_sb_buf_per_se,
				    256);
	if (r) {
		dev_err(adev->dev, "Failed to create Control Sideband Buffer\n");
		goto err;
	}

	/* Parameter Cache, not created by default */
	if (amdgpu_param_buf_per_se <= 0)
		goto out;

	r = gfx_v9_0_ngg_create_buf(adev, &adev->gfx.ngg.buf[NGG_PARAM],
				    amdgpu_param_buf_per_se,
				    512 * 1024);
	if (r) {
		dev_err(adev->dev, "Failed to create Parameter Cache\n");
		goto err;
	}

out:
	adev->gfx.ngg.init = true;
	return 0;
err:
	gfx_v9_0_ngg_fini(adev);
	return r;
}

static int gfx_v9_0_ngg_en(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->gfx.gfx_ring[0];
	int r;
	u32 data, base;

	if (!amdgpu_ngg)
		return 0;

	/* Program buffer size */
	data = REG_SET_FIELD(0, WD_BUF_RESOURCE_1, INDEX_BUF_SIZE,
			     adev->gfx.ngg.buf[NGG_PRIM].size >> 8);
	data = REG_SET_FIELD(data, WD_BUF_RESOURCE_1, POS_BUF_SIZE,
			     adev->gfx.ngg.buf[NGG_POS].size >> 8);
	WREG32_SOC15(GC, 0, mmWD_BUF_RESOURCE_1, data);

	data = REG_SET_FIELD(0, WD_BUF_RESOURCE_2, CNTL_SB_BUF_SIZE,
			     adev->gfx.ngg.buf[NGG_CNTL].size >> 8);
	data = REG_SET_FIELD(data, WD_BUF_RESOURCE_2, PARAM_BUF_SIZE,
			     adev->gfx.ngg.buf[NGG_PARAM].size >> 10);
	WREG32_SOC15(GC, 0, mmWD_BUF_RESOURCE_2, data);

	/* Program buffer base address */
	base = lower_32_bits(adev->gfx.ngg.buf[NGG_PRIM].gpu_addr);
	data = REG_SET_FIELD(0, WD_INDEX_BUF_BASE, BASE, base);
	WREG32_SOC15(GC, 0, mmWD_INDEX_BUF_BASE, data);

	base = upper_32_bits(adev->gfx.ngg.buf[NGG_PRIM].gpu_addr);
	data = REG_SET_FIELD(0, WD_INDEX_BUF_BASE_HI, BASE_HI, base);
	WREG32_SOC15(GC, 0, mmWD_INDEX_BUF_BASE_HI, data);

	base = lower_32_bits(adev->gfx.ngg.buf[NGG_POS].gpu_addr);
	data = REG_SET_FIELD(0, WD_POS_BUF_BASE, BASE, base);
	WREG32_SOC15(GC, 0, mmWD_POS_BUF_BASE, data);

	base = upper_32_bits(adev->gfx.ngg.buf[NGG_POS].gpu_addr);
	data = REG_SET_FIELD(0, WD_POS_BUF_BASE_HI, BASE_HI, base);
	WREG32_SOC15(GC, 0, mmWD_POS_BUF_BASE_HI, data);

	base = lower_32_bits(adev->gfx.ngg.buf[NGG_CNTL].gpu_addr);
	data = REG_SET_FIELD(0, WD_CNTL_SB_BUF_BASE, BASE, base);
	WREG32_SOC15(GC, 0, mmWD_CNTL_SB_BUF_BASE, data);

	base = upper_32_bits(adev->gfx.ngg.buf[NGG_CNTL].gpu_addr);
	data = REG_SET_FIELD(0, WD_CNTL_SB_BUF_BASE_HI, BASE_HI, base);
	WREG32_SOC15(GC, 0, mmWD_CNTL_SB_BUF_BASE_HI, data);

	/* Clear GDS reserved memory */
	r = amdgpu_ring_alloc(ring, 17);
	if (r) {
		DRM_ERROR("amdgpu: NGG failed to lock ring %s (%d).\n",
			  ring->name, r);
		return r;
	}

	gfx_v9_0_write_data_to_reg(ring, 0, false,
				   SOC15_REG_OFFSET(GC, 0, mmGDS_VMID0_SIZE),
			           (adev->gds.gds_size +
				    adev->gfx.ngg.gds_reserve_size));

	amdgpu_ring_write(ring, PACKET3(PACKET3_DMA_DATA, 5));
	amdgpu_ring_write(ring, (PACKET3_DMA_DATA_CP_SYNC |
				PACKET3_DMA_DATA_DST_SEL(1) |
				PACKET3_DMA_DATA_SRC_SEL(2)));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, adev->gfx.ngg.gds_reserve_addr);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, PACKET3_DMA_DATA_CMD_RAW_WAIT |
				adev->gfx.ngg.gds_reserve_size);

	gfx_v9_0_write_data_to_reg(ring, 0, false,
				   SOC15_REG_OFFSET(GC, 0, mmGDS_VMID0_SIZE), 0);

	amdgpu_ring_commit(ring);

	return 0;
}

static int gfx_v9_0_compute_ring_init(struct amdgpu_device *adev, int ring_id,
				      int mec, int pipe, int queue)
{
	int r;
	unsigned irq_type;
	struct amdgpu_ring *ring = &adev->gfx.compute_ring[ring_id];

	ring = &adev->gfx.compute_ring[ring_id];

	/* mec0 is me1 */
	ring->me = mec + 1;
	ring->pipe = pipe;
	ring->queue = queue;

	ring->ring_obj = NULL;
	ring->use_doorbell = true;
	ring->doorbell_index = (adev->doorbell_index.mec_ring0 + ring_id) << 1;
	ring->eop_gpu_addr = adev->gfx.mec.hpd_eop_gpu_addr
				+ (ring_id * GFX9_MEC_HPD_SIZE);
	sprintf(ring->name, "comp_%d.%d.%d", ring->me, ring->pipe, ring->queue);

	irq_type = AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP
		+ ((ring->me - 1) * adev->gfx.mec.num_pipe_per_mec)
		+ ring->pipe;

	/* type-2 packets are deprecated on MEC, use type-3 instead */
	r = amdgpu_ring_init(adev, ring, 1024,
			     &adev->gfx.eop_irq, irq_type);
	if (r)
		return r;


	return 0;
}

static int gfx_v9_0_sw_init(void *handle)
{
	int i, j, k, r, ring_id;
	struct amdgpu_ring *ring;
	struct amdgpu_kiq *kiq;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
	case CHIP_RAVEN:
		adev->gfx.mec.num_mec = 2;
		break;
	default:
		adev->gfx.mec.num_mec = 1;
		break;
	}

	adev->gfx.mec.num_pipe_per_mec = 4;
	adev->gfx.mec.num_queue_per_pipe = 8;

	/* EOP Event */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_GRBM_CP, GFX_9_0__SRCID__CP_EOP_INTERRUPT, &adev->gfx.eop_irq);
	if (r)
		return r;

	/* Privileged reg */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_GRBM_CP, GFX_9_0__SRCID__CP_PRIV_REG_FAULT,
			      &adev->gfx.priv_reg_irq);
	if (r)
		return r;

	/* Privileged inst */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_GRBM_CP, GFX_9_0__SRCID__CP_PRIV_INSTR_FAULT,
			      &adev->gfx.priv_inst_irq);
	if (r)
		return r;

	/* ECC error */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_GRBM_CP, GFX_9_0__SRCID__CP_ECC_ERROR,
			      &adev->gfx.cp_ecc_error_irq);
	if (r)
		return r;

	/* FUE error */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_GRBM_CP, GFX_9_0__SRCID__CP_FUE_ERROR,
			      &adev->gfx.cp_ecc_error_irq);
	if (r)
		return r;

	adev->gfx.gfx_current_status = AMDGPU_GFX_NORMAL_MODE;

	gfx_v9_0_scratch_init(adev);

	r = gfx_v9_0_init_microcode(adev);
	if (r) {
		DRM_ERROR("Failed to load gfx firmware!\n");
		return r;
	}

	r = adev->gfx.rlc.funcs->init(adev);
	if (r) {
		DRM_ERROR("Failed to init rlc BOs!\n");
		return r;
	}

	r = gfx_v9_0_mec_init(adev);
	if (r) {
		DRM_ERROR("Failed to init MEC BOs!\n");
		return r;
	}

	/* set up the gfx ring */
	for (i = 0; i < adev->gfx.num_gfx_rings; i++) {
		ring = &adev->gfx.gfx_ring[i];
		ring->ring_obj = NULL;
		if (!i)
			sprintf(ring->name, "gfx");
		else
			sprintf(ring->name, "gfx_%d", i);
		ring->use_doorbell = true;
		ring->doorbell_index = adev->doorbell_index.gfx_ring0 << 1;
		r = amdgpu_ring_init(adev, ring, 1024,
				     &adev->gfx.eop_irq, AMDGPU_CP_IRQ_GFX_ME0_PIPE0_EOP);
		if (r)
			return r;
	}

	/* set up the compute queues - allocate horizontally across pipes */
	ring_id = 0;
	for (i = 0; i < adev->gfx.mec.num_mec; ++i) {
		for (j = 0; j < adev->gfx.mec.num_queue_per_pipe; j++) {
			for (k = 0; k < adev->gfx.mec.num_pipe_per_mec; k++) {
				if (!amdgpu_gfx_is_mec_queue_enabled(adev, i, k, j))
					continue;

				r = gfx_v9_0_compute_ring_init(adev,
							       ring_id,
							       i, k, j);
				if (r)
					return r;

				ring_id++;
			}
		}
	}

	r = amdgpu_gfx_kiq_init(adev, GFX9_MEC_HPD_SIZE);
	if (r) {
		DRM_ERROR("Failed to init KIQ BOs!\n");
		return r;
	}

	kiq = &adev->gfx.kiq;
	r = amdgpu_gfx_kiq_init_ring(adev, &kiq->ring, &kiq->irq);
	if (r)
		return r;

	/* create MQD for all compute queues as wel as KIQ for SRIOV case */
	r = amdgpu_gfx_mqd_sw_init(adev, sizeof(struct v9_mqd_allocation));
	if (r)
		return r;

	adev->gfx.ce_ram_size = 0x8000;

	r = gfx_v9_0_gpu_early_init(adev);
	if (r)
		return r;

	r = gfx_v9_0_ngg_init(adev);
	if (r)
		return r;

	return 0;
}


static int gfx_v9_0_sw_fini(void *handle)
{
	int i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__GFX) &&
			adev->gfx.ras_if) {
		struct ras_common_if *ras_if = adev->gfx.ras_if;
		struct ras_ih_if ih_info = {
			.head = *ras_if,
		};

		amdgpu_ras_debugfs_remove(adev, ras_if);
		amdgpu_ras_sysfs_remove(adev, ras_if);
		amdgpu_ras_interrupt_remove_handler(adev,  &ih_info);
		amdgpu_ras_feature_enable(adev, ras_if, 0);
		kfree(ras_if);
	}

	for (i = 0; i < adev->gfx.num_gfx_rings; i++)
		amdgpu_ring_fini(&adev->gfx.gfx_ring[i]);
	for (i = 0; i < adev->gfx.num_compute_rings; i++)
		amdgpu_ring_fini(&adev->gfx.compute_ring[i]);

	amdgpu_gfx_mqd_sw_fini(adev);
	amdgpu_gfx_kiq_free_ring(&adev->gfx.kiq.ring, &adev->gfx.kiq.irq);
	amdgpu_gfx_kiq_fini(adev);

	gfx_v9_0_mec_fini(adev);
	gfx_v9_0_ngg_fini(adev);
	amdgpu_bo_unref(&adev->gfx.rlc.clear_state_obj);
	if (adev->asic_type == CHIP_RAVEN) {
		amdgpu_bo_free_kernel(&adev->gfx.rlc.cp_table_obj,
				&adev->gfx.rlc.cp_table_gpu_addr,
				(void **)&adev->gfx.rlc.cp_table_ptr);
	}
	gfx_v9_0_free_microcode(adev);

	return 0;
}


static void gfx_v9_0_tiling_mode_table_init(struct amdgpu_device *adev)
{
	/* TODO */
}

static void gfx_v9_0_select_se_sh(struct amdgpu_device *adev, u32 se_num, u32 sh_num, u32 instance)
{
	u32 data;

	if (instance == 0xffffffff)
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX, INSTANCE_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX, INSTANCE_INDEX, instance);

	if (se_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_INDEX, se_num);

	if (sh_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SH_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SH_INDEX, sh_num);

	WREG32_SOC15_RLC_SHADOW(GC, 0, mmGRBM_GFX_INDEX, data);
}

static u32 gfx_v9_0_get_rb_active_bitmap(struct amdgpu_device *adev)
{
	u32 data, mask;

	data = RREG32_SOC15(GC, 0, mmCC_RB_BACKEND_DISABLE);
	data |= RREG32_SOC15(GC, 0, mmGC_USER_RB_BACKEND_DISABLE);

	data &= CC_RB_BACKEND_DISABLE__BACKEND_DISABLE_MASK;
	data >>= GC_USER_RB_BACKEND_DISABLE__BACKEND_DISABLE__SHIFT;

	mask = amdgpu_gfx_create_bitmask(adev->gfx.config.max_backends_per_se /
					 adev->gfx.config.max_sh_per_se);

	return (~data) & mask;
}

static void gfx_v9_0_setup_rb(struct amdgpu_device *adev)
{
	int i, j;
	u32 data;
	u32 active_rbs = 0;
	u32 rb_bitmap_width_per_sh = adev->gfx.config.max_backends_per_se /
					adev->gfx.config.max_sh_per_se;

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			gfx_v9_0_select_se_sh(adev, i, j, 0xffffffff);
			data = gfx_v9_0_get_rb_active_bitmap(adev);
			active_rbs |= data << ((i * adev->gfx.config.max_sh_per_se + j) *
					       rb_bitmap_width_per_sh);
		}
	}
	gfx_v9_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);

	adev->gfx.config.backend_enable_mask = active_rbs;
	adev->gfx.config.num_rbs = hweight32(active_rbs);
}

#define DEFAULT_SH_MEM_BASES	(0x6000)
#define FIRST_COMPUTE_VMID	(8)
#define LAST_COMPUTE_VMID	(16)
static void gfx_v9_0_init_compute_vmid(struct amdgpu_device *adev)
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

	sh_mem_config = SH_MEM_ADDRESS_MODE_64 |
			SH_MEM_ALIGNMENT_MODE_UNALIGNED <<
			SH_MEM_CONFIG__ALIGNMENT_MODE__SHIFT;

	mutex_lock(&adev->srbm_mutex);
	for (i = FIRST_COMPUTE_VMID; i < LAST_COMPUTE_VMID; i++) {
		soc15_grbm_select(adev, 0, 0, 0, i);
		/* CP and shaders */
		WREG32_SOC15_RLC(GC, 0, mmSH_MEM_CONFIG, sh_mem_config);
		WREG32_SOC15_RLC(GC, 0, mmSH_MEM_BASES, sh_mem_bases);
	}
	soc15_grbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);

	/* Initialize all compute VMIDs to have no GDS, GWS, or OA
	   acccess. These should be enabled by FW for target VMIDs. */
	for (i = FIRST_COMPUTE_VMID; i < LAST_COMPUTE_VMID; i++) {
		WREG32_SOC15_OFFSET(GC, 0, mmGDS_VMID0_BASE, 2 * i, 0);
		WREG32_SOC15_OFFSET(GC, 0, mmGDS_VMID0_SIZE, 2 * i, 0);
		WREG32_SOC15_OFFSET(GC, 0, mmGDS_GWS_VMID0, i, 0);
		WREG32_SOC15_OFFSET(GC, 0, mmGDS_OA_VMID0, i, 0);
	}
}

static void gfx_v9_0_constants_init(struct amdgpu_device *adev)
{
	u32 tmp;
	int i;

	WREG32_FIELD15_RLC(GC, 0, GRBM_CNTL, READ_TIMEOUT, 0xff);

	gfx_v9_0_tiling_mode_table_init(adev);

	gfx_v9_0_setup_rb(adev);
	gfx_v9_0_get_cu_info(adev, &adev->gfx.cu_info);
	adev->gfx.config.db_debug2 = RREG32_SOC15(GC, 0, mmDB_DEBUG2);

	/* XXX SH_MEM regs */
	/* where to put LDS, scratch, GPUVM in FSA64 space */
	mutex_lock(&adev->srbm_mutex);
	for (i = 0; i < adev->vm_manager.id_mgr[AMDGPU_GFXHUB].num_ids; i++) {
		soc15_grbm_select(adev, 0, 0, 0, i);
		/* CP and shaders */
		if (i == 0) {
			tmp = REG_SET_FIELD(0, SH_MEM_CONFIG, ALIGNMENT_MODE,
					    SH_MEM_ALIGNMENT_MODE_UNALIGNED);
			tmp = REG_SET_FIELD(tmp, SH_MEM_CONFIG, RETRY_DISABLE,
					    !!amdgpu_noretry);
			WREG32_SOC15_RLC(GC, 0, mmSH_MEM_CONFIG, tmp);
			WREG32_SOC15_RLC(GC, 0, mmSH_MEM_BASES, 0);
		} else {
			tmp = REG_SET_FIELD(0, SH_MEM_CONFIG, ALIGNMENT_MODE,
					    SH_MEM_ALIGNMENT_MODE_UNALIGNED);
			tmp = REG_SET_FIELD(tmp, SH_MEM_CONFIG, RETRY_DISABLE,
					    !!amdgpu_noretry);
			WREG32_SOC15_RLC(GC, 0, mmSH_MEM_CONFIG, tmp);
			tmp = REG_SET_FIELD(0, SH_MEM_BASES, PRIVATE_BASE,
				(adev->gmc.private_aperture_start >> 48));
			tmp = REG_SET_FIELD(tmp, SH_MEM_BASES, SHARED_BASE,
				(adev->gmc.shared_aperture_start >> 48));
			WREG32_SOC15_RLC(GC, 0, mmSH_MEM_BASES, tmp);
		}
	}
	soc15_grbm_select(adev, 0, 0, 0, 0);

	mutex_unlock(&adev->srbm_mutex);

	gfx_v9_0_init_compute_vmid(adev);
}

static void gfx_v9_0_wait_for_rlc_serdes(struct amdgpu_device *adev)
{
	u32 i, j, k;
	u32 mask;

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			gfx_v9_0_select_se_sh(adev, i, j, 0xffffffff);
			for (k = 0; k < adev->usec_timeout; k++) {
				if (RREG32_SOC15(GC, 0, mmRLC_SERDES_CU_MASTER_BUSY) == 0)
					break;
				udelay(1);
			}
			if (k == adev->usec_timeout) {
				gfx_v9_0_select_se_sh(adev, 0xffffffff,
						      0xffffffff, 0xffffffff);
				mutex_unlock(&adev->grbm_idx_mutex);
				DRM_INFO("Timeout wait for RLC serdes %u,%u\n",
					 i, j);
				return;
			}
		}
	}
	gfx_v9_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);

	mask = RLC_SERDES_NONCU_MASTER_BUSY__SE_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__GC_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__TC0_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__TC1_MASTER_BUSY_MASK;
	for (k = 0; k < adev->usec_timeout; k++) {
		if ((RREG32_SOC15(GC, 0, mmRLC_SERDES_NONCU_MASTER_BUSY) & mask) == 0)
			break;
		udelay(1);
	}
}

static void gfx_v9_0_enable_gui_idle_interrupt(struct amdgpu_device *adev,
					       bool enable)
{
	u32 tmp = RREG32_SOC15(GC, 0, mmCP_INT_CNTL_RING0);

	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_BUSY_INT_ENABLE, enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_EMPTY_INT_ENABLE, enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CMP_BUSY_INT_ENABLE, enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, GFX_IDLE_INT_ENABLE, enable ? 1 : 0);

	WREG32_SOC15(GC, 0, mmCP_INT_CNTL_RING0, tmp);
}

static void gfx_v9_0_init_csb(struct amdgpu_device *adev)
{
	/* csib */
	WREG32_RLC(SOC15_REG_OFFSET(GC, 0, mmRLC_CSIB_ADDR_HI),
			adev->gfx.rlc.clear_state_gpu_addr >> 32);
	WREG32_RLC(SOC15_REG_OFFSET(GC, 0, mmRLC_CSIB_ADDR_LO),
			adev->gfx.rlc.clear_state_gpu_addr & 0xfffffffc);
	WREG32_RLC(SOC15_REG_OFFSET(GC, 0, mmRLC_CSIB_LENGTH),
			adev->gfx.rlc.clear_state_size);
}

static void gfx_v9_1_parse_ind_reg_list(int *register_list_format,
				int indirect_offset,
				int list_size,
				int *unique_indirect_regs,
				int unique_indirect_reg_count,
				int *indirect_start_offsets,
				int *indirect_start_offsets_count,
				int max_start_offsets_count)
{
	int idx;

	for (; indirect_offset < list_size; indirect_offset++) {
		WARN_ON(*indirect_start_offsets_count >= max_start_offsets_count);
		indirect_start_offsets[*indirect_start_offsets_count] = indirect_offset;
		*indirect_start_offsets_count = *indirect_start_offsets_count + 1;

		while (register_list_format[indirect_offset] != 0xFFFFFFFF) {
			indirect_offset += 2;

			/* look for the matching indice */
			for (idx = 0; idx < unique_indirect_reg_count; idx++) {
				if (unique_indirect_regs[idx] ==
					register_list_format[indirect_offset] ||
					!unique_indirect_regs[idx])
					break;
			}

			BUG_ON(idx >= unique_indirect_reg_count);

			if (!unique_indirect_regs[idx])
				unique_indirect_regs[idx] = register_list_format[indirect_offset];

			indirect_offset++;
		}
	}
}

static int gfx_v9_1_init_rlc_save_restore_list(struct amdgpu_device *adev)
{
	int unique_indirect_regs[] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
	int unique_indirect_reg_count = 0;

	int indirect_start_offsets[] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
	int indirect_start_offsets_count = 0;

	int list_size = 0;
	int i = 0, j = 0;
	u32 tmp = 0;

	u32 *register_list_format =
		kmemdup(adev->gfx.rlc.register_list_format,
			adev->gfx.rlc.reg_list_format_size_bytes, GFP_KERNEL);
	if (!register_list_format)
		return -ENOMEM;

	/* setup unique_indirect_regs array and indirect_start_offsets array */
	unique_indirect_reg_count = ARRAY_SIZE(unique_indirect_regs);
	gfx_v9_1_parse_ind_reg_list(register_list_format,
				    adev->gfx.rlc.reg_list_format_direct_reg_list_length,
				    adev->gfx.rlc.reg_list_format_size_bytes >> 2,
				    unique_indirect_regs,
				    unique_indirect_reg_count,
				    indirect_start_offsets,
				    &indirect_start_offsets_count,
				    ARRAY_SIZE(indirect_start_offsets));

	/* enable auto inc in case it is disabled */
	tmp = RREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_SRM_CNTL));
	tmp |= RLC_SRM_CNTL__AUTO_INCR_ADDR_MASK;
	WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_SRM_CNTL), tmp);

	/* write register_restore table to offset 0x0 using RLC_SRM_ARAM_ADDR/DATA */
	WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_SRM_ARAM_ADDR),
		RLC_SAVE_RESTORE_ADDR_STARTING_OFFSET);
	for (i = 0; i < adev->gfx.rlc.reg_list_size_bytes >> 2; i++)
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_SRM_ARAM_DATA),
			adev->gfx.rlc.register_restore[i]);

	/* load indirect register */
	WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_GPM_SCRATCH_ADDR),
		adev->gfx.rlc.reg_list_format_start);

	/* direct register portion */
	for (i = 0; i < adev->gfx.rlc.reg_list_format_direct_reg_list_length; i++)
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_GPM_SCRATCH_DATA),
			register_list_format[i]);

	/* indirect register portion */
	while (i < (adev->gfx.rlc.reg_list_format_size_bytes >> 2)) {
		if (register_list_format[i] == 0xFFFFFFFF) {
			WREG32_SOC15(GC, 0, mmRLC_GPM_SCRATCH_DATA, register_list_format[i++]);
			continue;
		}

		WREG32_SOC15(GC, 0, mmRLC_GPM_SCRATCH_DATA, register_list_format[i++]);
		WREG32_SOC15(GC, 0, mmRLC_GPM_SCRATCH_DATA, register_list_format[i++]);

		for (j = 0; j < unique_indirect_reg_count; j++) {
			if (register_list_format[i] == unique_indirect_regs[j]) {
				WREG32_SOC15(GC, 0, mmRLC_GPM_SCRATCH_DATA, j);
				break;
			}
		}

		BUG_ON(j >= unique_indirect_reg_count);

		i++;
	}

	/* set save/restore list size */
	list_size = adev->gfx.rlc.reg_list_size_bytes >> 2;
	list_size = list_size >> 1;
	WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_GPM_SCRATCH_ADDR),
		adev->gfx.rlc.reg_restore_list_size);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_GPM_SCRATCH_DATA), list_size);

	/* write the starting offsets to RLC scratch ram */
	WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_GPM_SCRATCH_ADDR),
		adev->gfx.rlc.starting_offsets_start);
	for (i = 0; i < ARRAY_SIZE(indirect_start_offsets); i++)
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_GPM_SCRATCH_DATA),
		       indirect_start_offsets[i]);

	/* load unique indirect regs*/
	for (i = 0; i < ARRAY_SIZE(unique_indirect_regs); i++) {
		if (unique_indirect_regs[i] != 0) {
			WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_SRM_INDEX_CNTL_ADDR_0)
			       + GFX_RLC_SRM_INDEX_CNTL_ADDR_OFFSETS[i],
			       unique_indirect_regs[i] & 0x3FFFF);

			WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_SRM_INDEX_CNTL_DATA_0)
			       + GFX_RLC_SRM_INDEX_CNTL_DATA_OFFSETS[i],
			       unique_indirect_regs[i] >> 20);
		}
	}

	kfree(register_list_format);
	return 0;
}

static void gfx_v9_0_enable_save_restore_machine(struct amdgpu_device *adev)
{
	WREG32_FIELD15(GC, 0, RLC_SRM_CNTL, SRM_ENABLE, 1);
}

static void pwr_10_0_gfxip_control_over_cgpg(struct amdgpu_device *adev,
					     bool enable)
{
	uint32_t data = 0;
	uint32_t default_data = 0;

	default_data = data = RREG32(SOC15_REG_OFFSET(PWR, 0, mmPWR_MISC_CNTL_STATUS));
	if (enable == true) {
		/* enable GFXIP control over CGPG */
		data |= PWR_MISC_CNTL_STATUS__PWR_GFX_RLC_CGPG_EN_MASK;
		if(default_data != data)
			WREG32(SOC15_REG_OFFSET(PWR, 0, mmPWR_MISC_CNTL_STATUS), data);

		/* update status */
		data &= ~PWR_MISC_CNTL_STATUS__PWR_GFXOFF_STATUS_MASK;
		data |= (2 << PWR_MISC_CNTL_STATUS__PWR_GFXOFF_STATUS__SHIFT);
		if(default_data != data)
			WREG32(SOC15_REG_OFFSET(PWR, 0, mmPWR_MISC_CNTL_STATUS), data);
	} else {
		/* restore GFXIP control over GCPG */
		data &= ~PWR_MISC_CNTL_STATUS__PWR_GFX_RLC_CGPG_EN_MASK;
		if(default_data != data)
			WREG32(SOC15_REG_OFFSET(PWR, 0, mmPWR_MISC_CNTL_STATUS), data);
	}
}

static void gfx_v9_0_init_gfx_power_gating(struct amdgpu_device *adev)
{
	uint32_t data = 0;

	if (adev->pg_flags & (AMD_PG_SUPPORT_GFX_PG |
			      AMD_PG_SUPPORT_GFX_SMG |
			      AMD_PG_SUPPORT_GFX_DMG)) {
		/* init IDLE_POLL_COUNT = 60 */
		data = RREG32(SOC15_REG_OFFSET(GC, 0, mmCP_RB_WPTR_POLL_CNTL));
		data &= ~CP_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT_MASK;
		data |= (0x60 << CP_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT__SHIFT);
		WREG32(SOC15_REG_OFFSET(GC, 0, mmCP_RB_WPTR_POLL_CNTL), data);

		/* init RLC PG Delay */
		data = 0;
		data |= (0x10 << RLC_PG_DELAY__POWER_UP_DELAY__SHIFT);
		data |= (0x10 << RLC_PG_DELAY__POWER_DOWN_DELAY__SHIFT);
		data |= (0x10 << RLC_PG_DELAY__CMD_PROPAGATE_DELAY__SHIFT);
		data |= (0x40 << RLC_PG_DELAY__MEM_SLEEP_DELAY__SHIFT);
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_DELAY), data);

		data = RREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_DELAY_2));
		data &= ~RLC_PG_DELAY_2__SERDES_CMD_DELAY_MASK;
		data |= (0x4 << RLC_PG_DELAY_2__SERDES_CMD_DELAY__SHIFT);
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_DELAY_2), data);

		data = RREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_DELAY_3));
		data &= ~RLC_PG_DELAY_3__CGCG_ACTIVE_BEFORE_CGPG_MASK;
		data |= (0xff << RLC_PG_DELAY_3__CGCG_ACTIVE_BEFORE_CGPG__SHIFT);
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_DELAY_3), data);

		data = RREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_AUTO_PG_CTRL));
		data &= ~RLC_AUTO_PG_CTRL__GRBM_REG_SAVE_GFX_IDLE_THRESHOLD_MASK;

		/* program GRBM_REG_SAVE_GFX_IDLE_THRESHOLD to 0x55f0 */
		data |= (0x55f0 << RLC_AUTO_PG_CTRL__GRBM_REG_SAVE_GFX_IDLE_THRESHOLD__SHIFT);
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_AUTO_PG_CTRL), data);

		pwr_10_0_gfxip_control_over_cgpg(adev, true);
	}
}

static void gfx_v9_0_enable_sck_slow_down_on_power_up(struct amdgpu_device *adev,
						bool enable)
{
	uint32_t data = 0;
	uint32_t default_data = 0;

	default_data = data = RREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL));
	data = REG_SET_FIELD(data, RLC_PG_CNTL,
			     SMU_CLK_SLOWDOWN_ON_PU_ENABLE,
			     enable ? 1 : 0);
	if (default_data != data)
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL), data);
}

static void gfx_v9_0_enable_sck_slow_down_on_power_down(struct amdgpu_device *adev,
						bool enable)
{
	uint32_t data = 0;
	uint32_t default_data = 0;

	default_data = data = RREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL));
	data = REG_SET_FIELD(data, RLC_PG_CNTL,
			     SMU_CLK_SLOWDOWN_ON_PD_ENABLE,
			     enable ? 1 : 0);
	if(default_data != data)
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL), data);
}

static void gfx_v9_0_enable_cp_power_gating(struct amdgpu_device *adev,
					bool enable)
{
	uint32_t data = 0;
	uint32_t default_data = 0;

	default_data = data = RREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL));
	data = REG_SET_FIELD(data, RLC_PG_CNTL,
			     CP_PG_DISABLE,
			     enable ? 0 : 1);
	if(default_data != data)
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL), data);
}

static void gfx_v9_0_enable_gfx_cg_power_gating(struct amdgpu_device *adev,
						bool enable)
{
	uint32_t data, default_data;

	default_data = data = RREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL));
	data = REG_SET_FIELD(data, RLC_PG_CNTL,
			     GFX_POWER_GATING_ENABLE,
			     enable ? 1 : 0);
	if(default_data != data)
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL), data);
}

static void gfx_v9_0_enable_gfx_pipeline_powergating(struct amdgpu_device *adev,
						bool enable)
{
	uint32_t data, default_data;

	default_data = data = RREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL));
	data = REG_SET_FIELD(data, RLC_PG_CNTL,
			     GFX_PIPELINE_PG_ENABLE,
			     enable ? 1 : 0);
	if(default_data != data)
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL), data);

	if (!enable)
		/* read any GFX register to wake up GFX */
		data = RREG32(SOC15_REG_OFFSET(GC, 0, mmDB_RENDER_CONTROL));
}

static void gfx_v9_0_enable_gfx_static_mg_power_gating(struct amdgpu_device *adev,
						       bool enable)
{
	uint32_t data, default_data;

	default_data = data = RREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL));
	data = REG_SET_FIELD(data, RLC_PG_CNTL,
			     STATIC_PER_CU_PG_ENABLE,
			     enable ? 1 : 0);
	if(default_data != data)
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL), data);
}

static void gfx_v9_0_enable_gfx_dynamic_mg_power_gating(struct amdgpu_device *adev,
						bool enable)
{
	uint32_t data, default_data;

	default_data = data = RREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL));
	data = REG_SET_FIELD(data, RLC_PG_CNTL,
			     DYN_PER_CU_PG_ENABLE,
			     enable ? 1 : 0);
	if(default_data != data)
		WREG32(SOC15_REG_OFFSET(GC, 0, mmRLC_PG_CNTL), data);
}

static void gfx_v9_0_init_pg(struct amdgpu_device *adev)
{
	gfx_v9_0_init_csb(adev);

	/*
	 * Rlc save restore list is workable since v2_1.
	 * And it's needed by gfxoff feature.
	 */
	if (adev->gfx.rlc.is_rlc_v2_1) {
		gfx_v9_1_init_rlc_save_restore_list(adev);
		gfx_v9_0_enable_save_restore_machine(adev);
	}

	if (adev->pg_flags & (AMD_PG_SUPPORT_GFX_PG |
			      AMD_PG_SUPPORT_GFX_SMG |
			      AMD_PG_SUPPORT_GFX_DMG |
			      AMD_PG_SUPPORT_CP |
			      AMD_PG_SUPPORT_GDS |
			      AMD_PG_SUPPORT_RLC_SMU_HS)) {
		WREG32(mmRLC_JUMP_TABLE_RESTORE,
		       adev->gfx.rlc.cp_table_gpu_addr >> 8);
		gfx_v9_0_init_gfx_power_gating(adev);
	}
}

void gfx_v9_0_rlc_stop(struct amdgpu_device *adev)
{
	WREG32_FIELD15(GC, 0, RLC_CNTL, RLC_ENABLE_F32, 0);
	gfx_v9_0_enable_gui_idle_interrupt(adev, false);
	gfx_v9_0_wait_for_rlc_serdes(adev);
}

static void gfx_v9_0_rlc_reset(struct amdgpu_device *adev)
{
	WREG32_FIELD15(GC, 0, GRBM_SOFT_RESET, SOFT_RESET_RLC, 1);
	udelay(50);
	WREG32_FIELD15(GC, 0, GRBM_SOFT_RESET, SOFT_RESET_RLC, 0);
	udelay(50);
}

static void gfx_v9_0_rlc_start(struct amdgpu_device *adev)
{
#ifdef AMDGPU_RLC_DEBUG_RETRY
	u32 rlc_ucode_ver;
#endif

	WREG32_FIELD15(GC, 0, RLC_CNTL, RLC_ENABLE_F32, 1);
	udelay(50);

	/* carrizo do enable cp interrupt after cp inited */
	if (!(adev->flags & AMD_IS_APU)) {
		gfx_v9_0_enable_gui_idle_interrupt(adev, true);
		udelay(50);
	}

#ifdef AMDGPU_RLC_DEBUG_RETRY
	/* RLC_GPM_GENERAL_6 : RLC Ucode version */
	rlc_ucode_ver = RREG32_SOC15(GC, 0, mmRLC_GPM_GENERAL_6);
	if(rlc_ucode_ver == 0x108) {
		DRM_INFO("Using rlc debug ucode. mmRLC_GPM_GENERAL_6 ==0x08%x / fw_ver == %i \n",
				rlc_ucode_ver, adev->gfx.rlc_fw_version);
		/* RLC_GPM_TIMER_INT_3 : Timer interval in RefCLK cycles,
		 * default is 0x9C4 to create a 100us interval */
		WREG32_SOC15(GC, 0, mmRLC_GPM_TIMER_INT_3, 0x9C4);
		/* RLC_GPM_GENERAL_12 : Minimum gap between wptr and rptr
		 * to disable the page fault retry interrupts, default is
		 * 0x100 (256) */
		WREG32_SOC15(GC, 0, mmRLC_GPM_GENERAL_12, 0x100);
	}
#endif
}

static int gfx_v9_0_rlc_load_microcode(struct amdgpu_device *adev)
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

	WREG32_SOC15(GC, 0, mmRLC_GPM_UCODE_ADDR,
			RLCG_UCODE_LOADING_START_ADDRESS);
	for (i = 0; i < fw_size; i++)
		WREG32_SOC15(GC, 0, mmRLC_GPM_UCODE_DATA, le32_to_cpup(fw_data++));
	WREG32_SOC15(GC, 0, mmRLC_GPM_UCODE_ADDR, adev->gfx.rlc_fw_version);

	return 0;
}

static int gfx_v9_0_rlc_resume(struct amdgpu_device *adev)
{
	int r;

	if (amdgpu_sriov_vf(adev)) {
		gfx_v9_0_init_csb(adev);
		return 0;
	}

	adev->gfx.rlc.funcs->stop(adev);

	/* disable CG */
	WREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL, 0);

	gfx_v9_0_init_pg(adev);

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		/* legacy rlc firmware loading */
		r = gfx_v9_0_rlc_load_microcode(adev);
		if (r)
			return r;
	}

	switch (adev->asic_type) {
	case CHIP_RAVEN:
		if (amdgpu_lbpw == 0)
			gfx_v9_0_enable_lbpw(adev, false);
		else
			gfx_v9_0_enable_lbpw(adev, true);
		break;
	case CHIP_VEGA20:
		if (amdgpu_lbpw > 0)
			gfx_v9_0_enable_lbpw(adev, true);
		else
			gfx_v9_0_enable_lbpw(adev, false);
		break;
	default:
		break;
	}

	adev->gfx.rlc.funcs->start(adev);

	return 0;
}

static void gfx_v9_0_cp_gfx_enable(struct amdgpu_device *adev, bool enable)
{
	int i;
	u32 tmp = RREG32_SOC15(GC, 0, mmCP_ME_CNTL);

	tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, ME_HALT, enable ? 0 : 1);
	tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, PFP_HALT, enable ? 0 : 1);
	tmp = REG_SET_FIELD(tmp, CP_ME_CNTL, CE_HALT, enable ? 0 : 1);
	if (!enable) {
		for (i = 0; i < adev->gfx.num_gfx_rings; i++)
			adev->gfx.gfx_ring[i].sched.ready = false;
	}
	WREG32_SOC15_RLC(GC, 0, mmCP_ME_CNTL, tmp);
	udelay(50);
}

static int gfx_v9_0_cp_gfx_load_microcode(struct amdgpu_device *adev)
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

	gfx_v9_0_cp_gfx_enable(adev, false);

	/* PFP */
	fw_data = (const __le32 *)
		(adev->gfx.pfp_fw->data +
		 le32_to_cpu(pfp_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(pfp_hdr->header.ucode_size_bytes) / 4;
	WREG32_SOC15(GC, 0, mmCP_PFP_UCODE_ADDR, 0);
	for (i = 0; i < fw_size; i++)
		WREG32_SOC15(GC, 0, mmCP_PFP_UCODE_DATA, le32_to_cpup(fw_data++));
	WREG32_SOC15(GC, 0, mmCP_PFP_UCODE_ADDR, adev->gfx.pfp_fw_version);

	/* CE */
	fw_data = (const __le32 *)
		(adev->gfx.ce_fw->data +
		 le32_to_cpu(ce_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(ce_hdr->header.ucode_size_bytes) / 4;
	WREG32_SOC15(GC, 0, mmCP_CE_UCODE_ADDR, 0);
	for (i = 0; i < fw_size; i++)
		WREG32_SOC15(GC, 0, mmCP_CE_UCODE_DATA, le32_to_cpup(fw_data++));
	WREG32_SOC15(GC, 0, mmCP_CE_UCODE_ADDR, adev->gfx.ce_fw_version);

	/* ME */
	fw_data = (const __le32 *)
		(adev->gfx.me_fw->data +
		 le32_to_cpu(me_hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(me_hdr->header.ucode_size_bytes) / 4;
	WREG32_SOC15(GC, 0, mmCP_ME_RAM_WADDR, 0);
	for (i = 0; i < fw_size; i++)
		WREG32_SOC15(GC, 0, mmCP_ME_RAM_DATA, le32_to_cpup(fw_data++));
	WREG32_SOC15(GC, 0, mmCP_ME_RAM_WADDR, adev->gfx.me_fw_version);

	return 0;
}

static int gfx_v9_0_cp_gfx_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->gfx.gfx_ring[0];
	const struct cs_section_def *sect = NULL;
	const struct cs_extent_def *ext = NULL;
	int r, i, tmp;

	/* init the CP */
	WREG32_SOC15(GC, 0, mmCP_MAX_CONTEXT, adev->gfx.config.max_hw_contexts - 1);
	WREG32_SOC15(GC, 0, mmCP_DEVICE_ID, 1);

	gfx_v9_0_cp_gfx_enable(adev, true);

	r = amdgpu_ring_alloc(ring, gfx_v9_0_get_csb_size(adev) + 4 + 3);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to lock ring (%d).\n", r);
		return r;
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	amdgpu_ring_write(ring, PACKET3_PREAMBLE_BEGIN_CLEAR_STATE);

	amdgpu_ring_write(ring, PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	amdgpu_ring_write(ring, 0x80000000);
	amdgpu_ring_write(ring, 0x80000000);

	for (sect = gfx9_cs_data; sect->section != NULL; ++sect) {
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

	amdgpu_ring_write(ring, PACKET3(PACKET3_PREAMBLE_CNTL, 0));
	amdgpu_ring_write(ring, PACKET3_PREAMBLE_END_CLEAR_STATE);

	amdgpu_ring_write(ring, PACKET3(PACKET3_CLEAR_STATE, 0));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_BASE, 2));
	amdgpu_ring_write(ring, PACKET3_BASE_INDEX(CE_PARTITION_BASE));
	amdgpu_ring_write(ring, 0x8000);
	amdgpu_ring_write(ring, 0x8000);

	amdgpu_ring_write(ring, PACKET3(PACKET3_SET_UCONFIG_REG,1));
	tmp = (PACKET3_SET_UCONFIG_REG_INDEX_TYPE |
		(SOC15_REG_OFFSET(GC, 0, mmVGT_INDEX_TYPE) - PACKET3_SET_UCONFIG_REG_START));
	amdgpu_ring_write(ring, tmp);
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_commit(ring);

	return 0;
}

static int gfx_v9_0_cp_gfx_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	u32 tmp;
	u32 rb_bufsz;
	u64 rb_addr, rptr_addr, wptr_gpu_addr;

	/* Set the write pointer delay */
	WREG32_SOC15(GC, 0, mmCP_RB_WPTR_DELAY, 0);

	/* set the RB to use vmid 0 */
	WREG32_SOC15(GC, 0, mmCP_RB_VMID, 0);

	/* Set ring buffer size */
	ring = &adev->gfx.gfx_ring[0];
	rb_bufsz = order_base_2(ring->ring_size / 8);
	tmp = REG_SET_FIELD(0, CP_RB0_CNTL, RB_BUFSZ, rb_bufsz);
	tmp = REG_SET_FIELD(tmp, CP_RB0_CNTL, RB_BLKSZ, rb_bufsz - 2);
#ifdef __BIG_ENDIAN
	tmp = REG_SET_FIELD(tmp, CP_RB0_CNTL, BUF_SWAP, 1);
#endif
	WREG32_SOC15(GC, 0, mmCP_RB0_CNTL, tmp);

	/* Initialize the ring buffer's write pointers */
	ring->wptr = 0;
	WREG32_SOC15(GC, 0, mmCP_RB0_WPTR, lower_32_bits(ring->wptr));
	WREG32_SOC15(GC, 0, mmCP_RB0_WPTR_HI, upper_32_bits(ring->wptr));

	/* set the wb address wether it's enabled or not */
	rptr_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);
	WREG32_SOC15(GC, 0, mmCP_RB0_RPTR_ADDR, lower_32_bits(rptr_addr));
	WREG32_SOC15(GC, 0, mmCP_RB0_RPTR_ADDR_HI, upper_32_bits(rptr_addr) & CP_RB_RPTR_ADDR_HI__RB_RPTR_ADDR_HI_MASK);

	wptr_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	WREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_ADDR_LO, lower_32_bits(wptr_gpu_addr));
	WREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_ADDR_HI, upper_32_bits(wptr_gpu_addr));

	mdelay(1);
	WREG32_SOC15(GC, 0, mmCP_RB0_CNTL, tmp);

	rb_addr = ring->gpu_addr >> 8;
	WREG32_SOC15(GC, 0, mmCP_RB0_BASE, rb_addr);
	WREG32_SOC15(GC, 0, mmCP_RB0_BASE_HI, upper_32_bits(rb_addr));

	tmp = RREG32_SOC15(GC, 0, mmCP_RB_DOORBELL_CONTROL);
	if (ring->use_doorbell) {
		tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
				    DOORBELL_OFFSET, ring->doorbell_index);
		tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL,
				    DOORBELL_EN, 1);
	} else {
		tmp = REG_SET_FIELD(tmp, CP_RB_DOORBELL_CONTROL, DOORBELL_EN, 0);
	}
	WREG32_SOC15(GC, 0, mmCP_RB_DOORBELL_CONTROL, tmp);

	tmp = REG_SET_FIELD(0, CP_RB_DOORBELL_RANGE_LOWER,
			DOORBELL_RANGE_LOWER, ring->doorbell_index);
	WREG32_SOC15(GC, 0, mmCP_RB_DOORBELL_RANGE_LOWER, tmp);

	WREG32_SOC15(GC, 0, mmCP_RB_DOORBELL_RANGE_UPPER,
		       CP_RB_DOORBELL_RANGE_UPPER__DOORBELL_RANGE_UPPER_MASK);


	/* start the ring */
	gfx_v9_0_cp_gfx_start(adev);
	ring->sched.ready = true;

	return 0;
}

static void gfx_v9_0_cp_compute_enable(struct amdgpu_device *adev, bool enable)
{
	int i;

	if (enable) {
		WREG32_SOC15_RLC(GC, 0, mmCP_MEC_CNTL, 0);
	} else {
		WREG32_SOC15_RLC(GC, 0, mmCP_MEC_CNTL,
			(CP_MEC_CNTL__MEC_ME1_HALT_MASK | CP_MEC_CNTL__MEC_ME2_HALT_MASK));
		for (i = 0; i < adev->gfx.num_compute_rings; i++)
			adev->gfx.compute_ring[i].sched.ready = false;
		adev->gfx.kiq.ring.sched.ready = false;
	}
	udelay(50);
}

static int gfx_v9_0_cp_compute_load_microcode(struct amdgpu_device *adev)
{
	const struct gfx_firmware_header_v1_0 *mec_hdr;
	const __le32 *fw_data;
	unsigned i;
	u32 tmp;

	if (!adev->gfx.mec_fw)
		return -EINVAL;

	gfx_v9_0_cp_compute_enable(adev, false);

	mec_hdr = (const struct gfx_firmware_header_v1_0 *)adev->gfx.mec_fw->data;
	amdgpu_ucode_print_gfx_hdr(&mec_hdr->header);

	fw_data = (const __le32 *)
		(adev->gfx.mec_fw->data +
		 le32_to_cpu(mec_hdr->header.ucode_array_offset_bytes));
	tmp = 0;
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, VMID, 0);
	tmp = REG_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, CACHE_POLICY, 0);
	WREG32_SOC15(GC, 0, mmCP_CPC_IC_BASE_CNTL, tmp);

	WREG32_SOC15(GC, 0, mmCP_CPC_IC_BASE_LO,
		adev->gfx.mec.mec_fw_gpu_addr & 0xFFFFF000);
	WREG32_SOC15(GC, 0, mmCP_CPC_IC_BASE_HI,
		upper_32_bits(adev->gfx.mec.mec_fw_gpu_addr));

	/* MEC1 */
	WREG32_SOC15(GC, 0, mmCP_MEC_ME1_UCODE_ADDR,
			 mec_hdr->jt_offset);
	for (i = 0; i < mec_hdr->jt_size; i++)
		WREG32_SOC15(GC, 0, mmCP_MEC_ME1_UCODE_DATA,
			le32_to_cpup(fw_data + mec_hdr->jt_offset + i));

	WREG32_SOC15(GC, 0, mmCP_MEC_ME1_UCODE_ADDR,
			adev->gfx.mec_fw_version);
	/* Todo : Loading MEC2 firmware is only necessary if MEC2 should run different microcode than MEC1. */

	return 0;
}

/* KIQ functions */
static void gfx_v9_0_kiq_setting(struct amdgpu_ring *ring)
{
	uint32_t tmp;
	struct amdgpu_device *adev = ring->adev;

	/* tell RLC which is KIQ queue */
	tmp = RREG32_SOC15(GC, 0, mmRLC_CP_SCHEDULERS);
	tmp &= 0xffffff00;
	tmp |= (ring->me << 5) | (ring->pipe << 3) | (ring->queue);
	WREG32_SOC15_RLC(GC, 0, mmRLC_CP_SCHEDULERS, tmp);
	tmp |= 0x80;
	WREG32_SOC15_RLC(GC, 0, mmRLC_CP_SCHEDULERS, tmp);
}

static int gfx_v9_0_kiq_kcq_enable(struct amdgpu_device *adev)
{
	struct amdgpu_ring *kiq_ring = &adev->gfx.kiq.ring;
	uint64_t queue_mask = 0;
	int r, i;

	for (i = 0; i < AMDGPU_MAX_COMPUTE_QUEUES; ++i) {
		if (!test_bit(i, adev->gfx.mec.queue_bitmap))
			continue;

		/* This situation may be hit in the future if a new HW
		 * generation exposes more than 64 queues. If so, the
		 * definition of queue_mask needs updating */
		if (WARN_ON(i >= (sizeof(queue_mask)*8))) {
			DRM_ERROR("Invalid KCQ enabled: %d\n", i);
			break;
		}

		queue_mask |= (1ull << i);
	}

	r = amdgpu_ring_alloc(kiq_ring, (7 * adev->gfx.num_compute_rings) + 8);
	if (r) {
		DRM_ERROR("Failed to lock KIQ (%d).\n", r);
		return r;
	}

	/* set resources */
	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_SET_RESOURCES, 6));
	amdgpu_ring_write(kiq_ring, PACKET3_SET_RESOURCES_VMID_MASK(0) |
			  PACKET3_SET_RESOURCES_QUEUE_TYPE(0));	/* vmid_mask:0 queue_type:0 (KIQ) */
	amdgpu_ring_write(kiq_ring, lower_32_bits(queue_mask));	/* queue mask lo */
	amdgpu_ring_write(kiq_ring, upper_32_bits(queue_mask));	/* queue mask hi */
	amdgpu_ring_write(kiq_ring, 0);	/* gws mask lo */
	amdgpu_ring_write(kiq_ring, 0);	/* gws mask hi */
	amdgpu_ring_write(kiq_ring, 0);	/* oac mask */
	amdgpu_ring_write(kiq_ring, 0);	/* gds heap base:0, gds heap size:0 */
	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		struct amdgpu_ring *ring = &adev->gfx.compute_ring[i];
		uint64_t mqd_addr = amdgpu_bo_gpu_offset(ring->mqd_obj);
		uint64_t wptr_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);

		amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_MAP_QUEUES, 5));
		/* Q_sel:0, vmid:0, vidmem: 1, engine:0, num_Q:1*/
		amdgpu_ring_write(kiq_ring, /* Q_sel: 0, vmid: 0, engine: 0, num_Q: 1 */
				  PACKET3_MAP_QUEUES_QUEUE_SEL(0) | /* Queue_Sel */
				  PACKET3_MAP_QUEUES_VMID(0) | /* VMID */
				  PACKET3_MAP_QUEUES_QUEUE(ring->queue) |
				  PACKET3_MAP_QUEUES_PIPE(ring->pipe) |
				  PACKET3_MAP_QUEUES_ME((ring->me == 1 ? 0 : 1)) |
				  PACKET3_MAP_QUEUES_QUEUE_TYPE(0) | /*queue_type: normal compute queue */
				  PACKET3_MAP_QUEUES_ALLOC_FORMAT(0) | /* alloc format: all_on_one_pipe */
				  PACKET3_MAP_QUEUES_ENGINE_SEL(0) | /* engine_sel: compute */
				  PACKET3_MAP_QUEUES_NUM_QUEUES(1)); /* num_queues: must be 1 */
		amdgpu_ring_write(kiq_ring, PACKET3_MAP_QUEUES_DOORBELL_OFFSET(ring->doorbell_index));
		amdgpu_ring_write(kiq_ring, lower_32_bits(mqd_addr));
		amdgpu_ring_write(kiq_ring, upper_32_bits(mqd_addr));
		amdgpu_ring_write(kiq_ring, lower_32_bits(wptr_addr));
		amdgpu_ring_write(kiq_ring, upper_32_bits(wptr_addr));
	}

	r = amdgpu_ring_test_helper(kiq_ring);
	if (r)
		DRM_ERROR("KCQ enable failed\n");

	return r;
}

static int gfx_v9_0_mqd_init(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct v9_mqd *mqd = ring->mqd_ptr;
	uint64_t hqd_gpu_addr, wb_gpu_addr, eop_base_addr;
	uint32_t tmp;

	mqd->header = 0xC0310800;
	mqd->compute_pipelinestat_enable = 0x00000001;
	mqd->compute_static_thread_mgmt_se0 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se1 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se2 = 0xffffffff;
	mqd->compute_static_thread_mgmt_se3 = 0xffffffff;
	mqd->compute_misc_reserved = 0x00000003;

	mqd->dynamic_cu_mask_addr_lo =
		lower_32_bits(ring->mqd_gpu_addr
			      + offsetof(struct v9_mqd_allocation, dynamic_cu_mask));
	mqd->dynamic_cu_mask_addr_hi =
		upper_32_bits(ring->mqd_gpu_addr
			      + offsetof(struct v9_mqd_allocation, dynamic_cu_mask));

	eop_base_addr = ring->eop_gpu_addr >> 8;
	mqd->cp_hqd_eop_base_addr_lo = eop_base_addr;
	mqd->cp_hqd_eop_base_addr_hi = upper_32_bits(eop_base_addr);

	/* set the EOP size, register value is 2^(EOP_SIZE+1) dwords */
	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_EOP_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_EOP_CONTROL, EOP_SIZE,
			(order_base_2(GFX9_MEC_HPD_SIZE / 4) - 1));

	mqd->cp_hqd_eop_control = tmp;

	/* enable doorbell? */
	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL);

	if (ring->use_doorbell) {
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_OFFSET, ring->doorbell_index);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_EN, 1);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_SOURCE, 0);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				    DOORBELL_HIT, 0);
	} else {
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
					 DOORBELL_EN, 0);
	}

	mqd->cp_hqd_pq_doorbell_control = tmp;

	/* disable the queue if it's active */
	ring->wptr = 0;
	mqd->cp_hqd_dequeue_request = 0;
	mqd->cp_hqd_pq_rptr = 0;
	mqd->cp_hqd_pq_wptr_lo = 0;
	mqd->cp_hqd_pq_wptr_hi = 0;

	/* set the pointer to the MQD */
	mqd->cp_mqd_base_addr_lo = ring->mqd_gpu_addr & 0xfffffffc;
	mqd->cp_mqd_base_addr_hi = upper_32_bits(ring->mqd_gpu_addr);

	/* set MQD vmid to 0 */
	tmp = RREG32_SOC15(GC, 0, mmCP_MQD_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_MQD_CONTROL, VMID, 0);
	mqd->cp_mqd_control = tmp;

	/* set the pointer to the HQD, this is similar CP_RB0_BASE/_HI */
	hqd_gpu_addr = ring->gpu_addr >> 8;
	mqd->cp_hqd_pq_base_lo = hqd_gpu_addr;
	mqd->cp_hqd_pq_base_hi = upper_32_bits(hqd_gpu_addr);

	/* set up the HQD, this is similar to CP_RB0_CNTL */
	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_PQ_CONTROL);
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
	mqd->cp_hqd_pq_control = tmp;

	/* set the wb address whether it's enabled or not */
	wb_gpu_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);
	mqd->cp_hqd_pq_rptr_report_addr_lo = wb_gpu_addr & 0xfffffffc;
	mqd->cp_hqd_pq_rptr_report_addr_hi =
		upper_32_bits(wb_gpu_addr) & 0xffff;

	/* only used if CP_PQ_WPTR_POLL_CNTL.CP_PQ_WPTR_POLL_CNTL__EN_MASK=1 */
	wb_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	mqd->cp_hqd_pq_wptr_poll_addr_lo = wb_gpu_addr & 0xfffffffc;
	mqd->cp_hqd_pq_wptr_poll_addr_hi = upper_32_bits(wb_gpu_addr) & 0xffff;

	tmp = 0;
	/* enable the doorbell if requested */
	if (ring->use_doorbell) {
		tmp = RREG32_SOC15(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
				DOORBELL_OFFSET, ring->doorbell_index);

		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
					 DOORBELL_EN, 1);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
					 DOORBELL_SOURCE, 0);
		tmp = REG_SET_FIELD(tmp, CP_HQD_PQ_DOORBELL_CONTROL,
					 DOORBELL_HIT, 0);
	}

	mqd->cp_hqd_pq_doorbell_control = tmp;

	/* reset read and write pointers, similar to CP_RB0_WPTR/_RPTR */
	ring->wptr = 0;
	mqd->cp_hqd_pq_rptr = RREG32_SOC15(GC, 0, mmCP_HQD_PQ_RPTR);

	/* set the vmid for the queue */
	mqd->cp_hqd_vmid = 0;

	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_PERSISTENT_STATE);
	tmp = REG_SET_FIELD(tmp, CP_HQD_PERSISTENT_STATE, PRELOAD_SIZE, 0x53);
	mqd->cp_hqd_persistent_state = tmp;

	/* set MIN_IB_AVAIL_SIZE */
	tmp = RREG32_SOC15(GC, 0, mmCP_HQD_IB_CONTROL);
	tmp = REG_SET_FIELD(tmp, CP_HQD_IB_CONTROL, MIN_IB_AVAIL_SIZE, 3);
	mqd->cp_hqd_ib_control = tmp;

	/* activate the queue */
	mqd->cp_hqd_active = 1;

	return 0;
}

static int gfx_v9_0_kiq_init_register(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct v9_mqd *mqd = ring->mqd_ptr;
	int j;

	/* disable wptr polling */
	WREG32_FIELD15(GC, 0, CP_PQ_WPTR_POLL_CNTL, EN, 0);

	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_EOP_BASE_ADDR,
	       mqd->cp_hqd_eop_base_addr_lo);
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_EOP_BASE_ADDR_HI,
	       mqd->cp_hqd_eop_base_addr_hi);

	/* set the EOP size, register value is 2^(EOP_SIZE+1) dwords */
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_EOP_CONTROL,
	       mqd->cp_hqd_eop_control);

	/* enable doorbell? */
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL,
	       mqd->cp_hqd_pq_doorbell_control);

	/* disable the queue if it's active */
	if (RREG32_SOC15(GC, 0, mmCP_HQD_ACTIVE) & 1) {
		WREG32_SOC15_RLC(GC, 0, mmCP_HQD_DEQUEUE_REQUEST, 1);
		for (j = 0; j < adev->usec_timeout; j++) {
			if (!(RREG32_SOC15(GC, 0, mmCP_HQD_ACTIVE) & 1))
				break;
			udelay(1);
		}
		WREG32_SOC15_RLC(GC, 0, mmCP_HQD_DEQUEUE_REQUEST,
		       mqd->cp_hqd_dequeue_request);
		WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_RPTR,
		       mqd->cp_hqd_pq_rptr);
		WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_WPTR_LO,
		       mqd->cp_hqd_pq_wptr_lo);
		WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_WPTR_HI,
		       mqd->cp_hqd_pq_wptr_hi);
	}

	/* set the pointer to the MQD */
	WREG32_SOC15_RLC(GC, 0, mmCP_MQD_BASE_ADDR,
	       mqd->cp_mqd_base_addr_lo);
	WREG32_SOC15_RLC(GC, 0, mmCP_MQD_BASE_ADDR_HI,
	       mqd->cp_mqd_base_addr_hi);

	/* set MQD vmid to 0 */
	WREG32_SOC15_RLC(GC, 0, mmCP_MQD_CONTROL,
	       mqd->cp_mqd_control);

	/* set the pointer to the HQD, this is similar CP_RB0_BASE/_HI */
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_BASE,
	       mqd->cp_hqd_pq_base_lo);
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_BASE_HI,
	       mqd->cp_hqd_pq_base_hi);

	/* set up the HQD, this is similar to CP_RB0_CNTL */
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_CONTROL,
	       mqd->cp_hqd_pq_control);

	/* set the wb address whether it's enabled or not */
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_RPTR_REPORT_ADDR,
				mqd->cp_hqd_pq_rptr_report_addr_lo);
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_RPTR_REPORT_ADDR_HI,
				mqd->cp_hqd_pq_rptr_report_addr_hi);

	/* only used if CP_PQ_WPTR_POLL_CNTL.CP_PQ_WPTR_POLL_CNTL__EN_MASK=1 */
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_WPTR_POLL_ADDR,
	       mqd->cp_hqd_pq_wptr_poll_addr_lo);
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_WPTR_POLL_ADDR_HI,
	       mqd->cp_hqd_pq_wptr_poll_addr_hi);

	/* enable the doorbell if requested */
	if (ring->use_doorbell) {
		WREG32_SOC15(GC, 0, mmCP_MEC_DOORBELL_RANGE_LOWER,
					(adev->doorbell_index.kiq * 2) << 2);
		WREG32_SOC15(GC, 0, mmCP_MEC_DOORBELL_RANGE_UPPER,
					(adev->doorbell_index.userqueue_end * 2) << 2);
	}

	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL,
	       mqd->cp_hqd_pq_doorbell_control);

	/* reset read and write pointers, similar to CP_RB0_WPTR/_RPTR */
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_WPTR_LO,
	       mqd->cp_hqd_pq_wptr_lo);
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_WPTR_HI,
	       mqd->cp_hqd_pq_wptr_hi);

	/* set the vmid for the queue */
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_VMID, mqd->cp_hqd_vmid);

	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PERSISTENT_STATE,
	       mqd->cp_hqd_persistent_state);

	/* activate the queue */
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_ACTIVE,
	       mqd->cp_hqd_active);

	if (ring->use_doorbell)
		WREG32_FIELD15(GC, 0, CP_PQ_STATUS, DOORBELL_ENABLE, 1);

	return 0;
}

static int gfx_v9_0_kiq_fini_register(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	int j;

	/* disable the queue if it's active */
	if (RREG32_SOC15(GC, 0, mmCP_HQD_ACTIVE) & 1) {

		WREG32_SOC15_RLC(GC, 0, mmCP_HQD_DEQUEUE_REQUEST, 1);

		for (j = 0; j < adev->usec_timeout; j++) {
			if (!(RREG32_SOC15(GC, 0, mmCP_HQD_ACTIVE) & 1))
				break;
			udelay(1);
		}

		if (j == AMDGPU_MAX_USEC_TIMEOUT) {
			DRM_DEBUG("KIQ dequeue request failed.\n");

			/* Manual disable if dequeue request times out */
			WREG32_SOC15_RLC(GC, 0, mmCP_HQD_ACTIVE, 0);
		}

		WREG32_SOC15_RLC(GC, 0, mmCP_HQD_DEQUEUE_REQUEST,
		      0);
	}

	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_IQ_TIMER, 0);
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_IB_CONTROL, 0);
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PERSISTENT_STATE, 0);
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL, 0x40000000);
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL, 0);
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_RPTR, 0);
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_WPTR_HI, 0);
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PQ_WPTR_LO, 0);

	return 0;
}

static int gfx_v9_0_kiq_init_queue(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct v9_mqd *mqd = ring->mqd_ptr;
	int mqd_idx = AMDGPU_MAX_COMPUTE_RINGS;

	gfx_v9_0_kiq_setting(ring);

	if (adev->in_gpu_reset) { /* for GPU_RESET case */
		/* reset MQD to a clean status */
		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy(mqd, adev->gfx.mec.mqd_backup[mqd_idx], sizeof(struct v9_mqd_allocation));

		/* reset ring buffer */
		ring->wptr = 0;
		amdgpu_ring_clear_ring(ring);

		mutex_lock(&adev->srbm_mutex);
		soc15_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0);
		gfx_v9_0_kiq_init_register(ring);
		soc15_grbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);
	} else {
		memset((void *)mqd, 0, sizeof(struct v9_mqd_allocation));
		((struct v9_mqd_allocation *)mqd)->dynamic_cu_mask = 0xFFFFFFFF;
		((struct v9_mqd_allocation *)mqd)->dynamic_rb_mask = 0xFFFFFFFF;
		mutex_lock(&adev->srbm_mutex);
		soc15_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0);
		gfx_v9_0_mqd_init(ring);
		gfx_v9_0_kiq_init_register(ring);
		soc15_grbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);

		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy(adev->gfx.mec.mqd_backup[mqd_idx], mqd, sizeof(struct v9_mqd_allocation));
	}

	return 0;
}

static int gfx_v9_0_kcq_init_queue(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	struct v9_mqd *mqd = ring->mqd_ptr;
	int mqd_idx = ring - &adev->gfx.compute_ring[0];

	if (!adev->in_gpu_reset && !adev->in_suspend) {
		memset((void *)mqd, 0, sizeof(struct v9_mqd_allocation));
		((struct v9_mqd_allocation *)mqd)->dynamic_cu_mask = 0xFFFFFFFF;
		((struct v9_mqd_allocation *)mqd)->dynamic_rb_mask = 0xFFFFFFFF;
		mutex_lock(&adev->srbm_mutex);
		soc15_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0);
		gfx_v9_0_mqd_init(ring);
		soc15_grbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);

		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy(adev->gfx.mec.mqd_backup[mqd_idx], mqd, sizeof(struct v9_mqd_allocation));
	} else if (adev->in_gpu_reset) { /* for GPU_RESET case */
		/* reset MQD to a clean status */
		if (adev->gfx.mec.mqd_backup[mqd_idx])
			memcpy(mqd, adev->gfx.mec.mqd_backup[mqd_idx], sizeof(struct v9_mqd_allocation));

		/* reset ring buffer */
		ring->wptr = 0;
		amdgpu_ring_clear_ring(ring);
	} else {
		amdgpu_ring_clear_ring(ring);
	}

	return 0;
}

static int gfx_v9_0_kiq_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	int r;

	ring = &adev->gfx.kiq.ring;

	r = amdgpu_bo_reserve(ring->mqd_obj, false);
	if (unlikely(r != 0))
		return r;

	r = amdgpu_bo_kmap(ring->mqd_obj, (void **)&ring->mqd_ptr);
	if (unlikely(r != 0))
		return r;

	gfx_v9_0_kiq_init_queue(ring);
	amdgpu_bo_kunmap(ring->mqd_obj);
	ring->mqd_ptr = NULL;
	amdgpu_bo_unreserve(ring->mqd_obj);
	ring->sched.ready = true;
	return 0;
}

static int gfx_v9_0_kcq_resume(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = NULL;
	int r = 0, i;

	gfx_v9_0_cp_compute_enable(adev, true);

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		ring = &adev->gfx.compute_ring[i];

		r = amdgpu_bo_reserve(ring->mqd_obj, false);
		if (unlikely(r != 0))
			goto done;
		r = amdgpu_bo_kmap(ring->mqd_obj, (void **)&ring->mqd_ptr);
		if (!r) {
			r = gfx_v9_0_kcq_init_queue(ring);
			amdgpu_bo_kunmap(ring->mqd_obj);
			ring->mqd_ptr = NULL;
		}
		amdgpu_bo_unreserve(ring->mqd_obj);
		if (r)
			goto done;
	}

	r = gfx_v9_0_kiq_kcq_enable(adev);
done:
	return r;
}

static int gfx_v9_0_cp_resume(struct amdgpu_device *adev)
{
	int r, i;
	struct amdgpu_ring *ring;

	if (!(adev->flags & AMD_IS_APU))
		gfx_v9_0_enable_gui_idle_interrupt(adev, false);

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		/* legacy firmware loading */
		r = gfx_v9_0_cp_gfx_load_microcode(adev);
		if (r)
			return r;

		r = gfx_v9_0_cp_compute_load_microcode(adev);
		if (r)
			return r;
	}

	r = gfx_v9_0_kiq_resume(adev);
	if (r)
		return r;

	r = gfx_v9_0_cp_gfx_resume(adev);
	if (r)
		return r;

	r = gfx_v9_0_kcq_resume(adev);
	if (r)
		return r;

	ring = &adev->gfx.gfx_ring[0];
	r = amdgpu_ring_test_helper(ring);
	if (r)
		return r;

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		ring = &adev->gfx.compute_ring[i];
		amdgpu_ring_test_helper(ring);
	}

	gfx_v9_0_enable_gui_idle_interrupt(adev, true);

	return 0;
}

static void gfx_v9_0_cp_enable(struct amdgpu_device *adev, bool enable)
{
	gfx_v9_0_cp_gfx_enable(adev, enable);
	gfx_v9_0_cp_compute_enable(adev, enable);
}

static int gfx_v9_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gfx_v9_0_init_golden_registers(adev);

	gfx_v9_0_constants_init(adev);

	r = gfx_v9_0_csb_vram_pin(adev);
	if (r)
		return r;

	r = adev->gfx.rlc.funcs->resume(adev);
	if (r)
		return r;

	r = gfx_v9_0_cp_resume(adev);
	if (r)
		return r;

	r = gfx_v9_0_ngg_en(adev);
	if (r)
		return r;

	return r;
}

static int gfx_v9_0_kcq_disable(struct amdgpu_device *adev)
{
	int r, i;
	struct amdgpu_ring *kiq_ring = &adev->gfx.kiq.ring;

	r = amdgpu_ring_alloc(kiq_ring, 6 * adev->gfx.num_compute_rings);
	if (r)
		DRM_ERROR("Failed to lock KIQ (%d).\n", r);

	for (i = 0; i < adev->gfx.num_compute_rings; i++) {
		struct amdgpu_ring *ring = &adev->gfx.compute_ring[i];

		amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_UNMAP_QUEUES, 4));
		amdgpu_ring_write(kiq_ring, /* Q_sel: 0, vmid: 0, engine: 0, num_Q: 1 */
						PACKET3_UNMAP_QUEUES_ACTION(1) | /* RESET_QUEUES */
						PACKET3_UNMAP_QUEUES_QUEUE_SEL(0) |
						PACKET3_UNMAP_QUEUES_ENGINE_SEL(0) |
						PACKET3_UNMAP_QUEUES_NUM_QUEUES(1));
		amdgpu_ring_write(kiq_ring, PACKET3_UNMAP_QUEUES_DOORBELL_OFFSET0(ring->doorbell_index));
		amdgpu_ring_write(kiq_ring, 0);
		amdgpu_ring_write(kiq_ring, 0);
		amdgpu_ring_write(kiq_ring, 0);
	}
	r = amdgpu_ring_test_helper(kiq_ring);
	if (r)
		DRM_ERROR("KCQ disable failed\n");

	return r;
}

static int gfx_v9_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_irq_put(adev, &adev->gfx.cp_ecc_error_irq, 0);
	amdgpu_irq_put(adev, &adev->gfx.priv_reg_irq, 0);
	amdgpu_irq_put(adev, &adev->gfx.priv_inst_irq, 0);

	/* disable KCQ to avoid CPC touch memory not valid anymore */
	gfx_v9_0_kcq_disable(adev);

	if (amdgpu_sriov_vf(adev)) {
		gfx_v9_0_cp_gfx_enable(adev, false);
		/* must disable polling for SRIOV when hw finished, otherwise
		 * CPC engine may still keep fetching WB address which is already
		 * invalid after sw finished and trigger DMAR reading error in
		 * hypervisor side.
		 */
		WREG32_FIELD15(GC, 0, CP_PQ_WPTR_POLL_CNTL, EN, 0);
		return 0;
	}

	/* Use deinitialize sequence from CAIL when unbinding device from driver,
	 * otherwise KIQ is hanging when binding back
	 */
	if (!adev->in_gpu_reset && !adev->in_suspend) {
		mutex_lock(&adev->srbm_mutex);
		soc15_grbm_select(adev, adev->gfx.kiq.ring.me,
				adev->gfx.kiq.ring.pipe,
				adev->gfx.kiq.ring.queue, 0);
		gfx_v9_0_kiq_fini_register(&adev->gfx.kiq.ring);
		soc15_grbm_select(adev, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);
	}

	gfx_v9_0_cp_enable(adev, false);
	adev->gfx.rlc.funcs->stop(adev);

	gfx_v9_0_csb_vram_unpin(adev);

	return 0;
}

static int gfx_v9_0_suspend(void *handle)
{
	return gfx_v9_0_hw_fini(handle);
}

static int gfx_v9_0_resume(void *handle)
{
	return gfx_v9_0_hw_init(handle);
}

static bool gfx_v9_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (REG_GET_FIELD(RREG32_SOC15(GC, 0, mmGRBM_STATUS),
				GRBM_STATUS, GUI_ACTIVE))
		return false;
	else
		return true;
}

static int gfx_v9_0_wait_for_idle(void *handle)
{
	unsigned i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (gfx_v9_0_is_idle(handle))
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int gfx_v9_0_soft_reset(void *handle)
{
	u32 grbm_soft_reset = 0;
	u32 tmp;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* GRBM_STATUS */
	tmp = RREG32_SOC15(GC, 0, mmGRBM_STATUS);
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
	}

	/* GRBM_STATUS2 */
	tmp = RREG32_SOC15(GC, 0, mmGRBM_STATUS2);
	if (REG_GET_FIELD(tmp, GRBM_STATUS2, RLC_BUSY))
		grbm_soft_reset = REG_SET_FIELD(grbm_soft_reset,
						GRBM_SOFT_RESET, SOFT_RESET_RLC, 1);


	if (grbm_soft_reset) {
		/* stop the rlc */
		adev->gfx.rlc.funcs->stop(adev);

		/* Disable GFX parsing/prefetching */
		gfx_v9_0_cp_gfx_enable(adev, false);

		/* Disable MEC parsing/prefetching */
		gfx_v9_0_cp_compute_enable(adev, false);

		if (grbm_soft_reset) {
			tmp = RREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET);
			tmp |= grbm_soft_reset;
			dev_info(adev->dev, "GRBM_SOFT_RESET=0x%08X\n", tmp);
			WREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET, tmp);
			tmp = RREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET);

			udelay(50);

			tmp &= ~grbm_soft_reset;
			WREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET, tmp);
			tmp = RREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET);
		}

		/* Wait a little for things to settle down */
		udelay(50);
	}
	return 0;
}

static uint64_t gfx_v9_0_get_gpu_clock_counter(struct amdgpu_device *adev)
{
	uint64_t clock;

	mutex_lock(&adev->gfx.gpu_clock_mutex);
	WREG32_SOC15(GC, 0, mmRLC_CAPTURE_GPU_CLOCK_COUNT, 1);
	clock = (uint64_t)RREG32_SOC15(GC, 0, mmRLC_GPU_CLOCK_COUNT_LSB) |
		((uint64_t)RREG32_SOC15(GC, 0, mmRLC_GPU_CLOCK_COUNT_MSB) << 32ULL);
	mutex_unlock(&adev->gfx.gpu_clock_mutex);
	return clock;
}

static void gfx_v9_0_ring_emit_gds_switch(struct amdgpu_ring *ring,
					  uint32_t vmid,
					  uint32_t gds_base, uint32_t gds_size,
					  uint32_t gws_base, uint32_t gws_size,
					  uint32_t oa_base, uint32_t oa_size)
{
	struct amdgpu_device *adev = ring->adev;

	/* GDS Base */
	gfx_v9_0_write_data_to_reg(ring, 0, false,
				   SOC15_REG_OFFSET(GC, 0, mmGDS_VMID0_BASE) + 2 * vmid,
				   gds_base);

	/* GDS Size */
	gfx_v9_0_write_data_to_reg(ring, 0, false,
				   SOC15_REG_OFFSET(GC, 0, mmGDS_VMID0_SIZE) + 2 * vmid,
				   gds_size);

	/* GWS */
	gfx_v9_0_write_data_to_reg(ring, 0, false,
				   SOC15_REG_OFFSET(GC, 0, mmGDS_GWS_VMID0) + vmid,
				   gws_size << GDS_GWS_VMID0__SIZE__SHIFT | gws_base);

	/* OA */
	gfx_v9_0_write_data_to_reg(ring, 0, false,
				   SOC15_REG_OFFSET(GC, 0, mmGDS_OA_VMID0) + vmid,
				   (1 << (oa_size + oa_base)) - (1 << oa_base));
}

static const u32 vgpr_init_compute_shader[] =
{
	0xb07c0000, 0xbe8000ff,
	0x000000f8, 0xbf110800,
	0x7e000280, 0x7e020280,
	0x7e040280, 0x7e060280,
	0x7e080280, 0x7e0a0280,
	0x7e0c0280, 0x7e0e0280,
	0x80808800, 0xbe803200,
	0xbf84fff5, 0xbf9c0000,
	0xd28c0001, 0x0001007f,
	0xd28d0001, 0x0002027e,
	0x10020288, 0xb8810904,
	0xb7814000, 0xd1196a01,
	0x00000301, 0xbe800087,
	0xbefc00c1, 0xd89c4000,
	0x00020201, 0xd89cc080,
	0x00040401, 0x320202ff,
	0x00000800, 0x80808100,
	0xbf84fff8, 0x7e020280,
	0xbf810000, 0x00000000,
};

static const u32 sgpr_init_compute_shader[] =
{
	0xb07c0000, 0xbe8000ff,
	0x0000005f, 0xbee50080,
	0xbe812c65, 0xbe822c65,
	0xbe832c65, 0xbe842c65,
	0xbe852c65, 0xb77c0005,
	0x80808500, 0xbf84fff8,
	0xbe800080, 0xbf810000,
};

static const struct soc15_reg_entry vgpr_init_regs[] = {
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_STATIC_THREAD_MGMT_SE0), 0xffffffff },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_STATIC_THREAD_MGMT_SE1), 0xffffffff },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_STATIC_THREAD_MGMT_SE2), 0xffffffff },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_STATIC_THREAD_MGMT_SE3), 0xffffffff },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_RESOURCE_LIMITS), 0x1000000 }, /* CU_GROUP_COUNT=1 */
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_NUM_THREAD_X), 256*2 },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_NUM_THREAD_Y), 1 },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_NUM_THREAD_Z), 1 },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_PGM_RSRC1), 0x100007f }, /* VGPRS=15 (256 logical VGPRs, SGPRS=1 (16 SGPRs, BULKY=1 */
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_PGM_RSRC2), 0x400000 },  /* 64KB LDS */
};

static const struct soc15_reg_entry sgpr_init_regs[] = {
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_STATIC_THREAD_MGMT_SE0), 0xffffffff },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_STATIC_THREAD_MGMT_SE1), 0xffffffff },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_STATIC_THREAD_MGMT_SE2), 0xffffffff },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_STATIC_THREAD_MGMT_SE3), 0xffffffff },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_RESOURCE_LIMITS), 0x1000000 }, /* CU_GROUP_COUNT=1 */
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_NUM_THREAD_X), 256*2 },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_NUM_THREAD_Y), 1 },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_NUM_THREAD_Z), 1 },
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_PGM_RSRC1), 0x340 }, /* SGPRS=13 (112 GPRS) */
   { SOC15_REG_ENTRY(GC, 0, mmCOMPUTE_PGM_RSRC2), 0x0 },
};

static const struct soc15_reg_entry sec_ded_counter_registers[] = {
   { SOC15_REG_ENTRY(GC, 0, mmCPC_EDC_SCRATCH_CNT), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmCPC_EDC_UCODE_CNT), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmCPF_EDC_ROQ_CNT), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmCPF_EDC_TAG_CNT), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmCPG_EDC_DMA_CNT), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmCPG_EDC_TAG_CNT), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmDC_EDC_CSINVOC_CNT), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmDC_EDC_RESTORE_CNT), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmDC_EDC_STATE_CNT), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmGDS_EDC_CNT), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmGDS_EDC_GRBM_CNT), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmGDS_EDC_OA_DED), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmSPI_EDC_CNT), 0, 4, 1},
   { SOC15_REG_ENTRY(GC, 0, mmSQC_EDC_CNT), 0, 4, 6},
   { SOC15_REG_ENTRY(GC, 0, mmSQ_EDC_DED_CNT), 0, 4, 16},
   { SOC15_REG_ENTRY(GC, 0, mmSQ_EDC_INFO), 0, 4, 16},
   { SOC15_REG_ENTRY(GC, 0, mmSQ_EDC_SEC_CNT), 0, 4, 16},
   { SOC15_REG_ENTRY(GC, 0, mmTCC_EDC_CNT), 0, 1, 16},
   { SOC15_REG_ENTRY(GC, 0, mmTCP_ATC_EDC_GATCL1_CNT), 0, 4, 16},
   { SOC15_REG_ENTRY(GC, 0, mmTCP_EDC_CNT), 0, 4, 16},
   { SOC15_REG_ENTRY(GC, 0, mmTD_EDC_CNT), 0, 4, 16},
   { SOC15_REG_ENTRY(GC, 0, mmSQC_EDC_CNT2), 0, 4, 6},
   { SOC15_REG_ENTRY(GC, 0, mmSQ_EDC_CNT), 0, 4, 16},
   { SOC15_REG_ENTRY(GC, 0, mmTA_EDC_CNT), 0, 4, 16},
   { SOC15_REG_ENTRY(GC, 0, mmGDS_EDC_OA_PHY_CNT), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmGDS_EDC_OA_PIPE_CNT), 0, 1, 1},
   { SOC15_REG_ENTRY(GC, 0, mmGCEA_EDC_CNT), 0, 1, 32},
   { SOC15_REG_ENTRY(GC, 0, mmGCEA_EDC_CNT2), 0, 1, 32},
   { SOC15_REG_ENTRY(GC, 0, mmTCI_EDC_CNT), 0, 1, 72},
   { SOC15_REG_ENTRY(GC, 0, mmTCC_EDC_CNT2), 0, 1, 16},
   { SOC15_REG_ENTRY(GC, 0, mmTCA_EDC_CNT), 0, 1, 2},
   { SOC15_REG_ENTRY(GC, 0, mmSQC_EDC_CNT3), 0, 4, 6},
};

static int gfx_v9_0_do_edc_gds_workarounds(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->gfx.compute_ring[0];
	int i, r;

	r = amdgpu_ring_alloc(ring, 7);
	if (r) {
		DRM_ERROR("amdgpu: GDS workarounds failed to lock ring %s (%d).\n",
			ring->name, r);
		return r;
	}

	WREG32_SOC15(GC, 0, mmGDS_VMID0_BASE, 0x00000000);
	WREG32_SOC15(GC, 0, mmGDS_VMID0_SIZE, adev->gds.gds_size);

	amdgpu_ring_write(ring, PACKET3(PACKET3_DMA_DATA, 5));
	amdgpu_ring_write(ring, (PACKET3_DMA_DATA_CP_SYNC |
				PACKET3_DMA_DATA_DST_SEL(1) |
				PACKET3_DMA_DATA_SRC_SEL(2) |
				PACKET3_DMA_DATA_ENGINE(0)));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, PACKET3_DMA_DATA_CMD_RAW_WAIT |
				adev->gds.gds_size);

	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		if (ring->wptr == gfx_v9_0_ring_get_rptr_compute(ring))
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;

	WREG32_SOC15(GC, 0, mmGDS_VMID0_SIZE, 0x00000000);

	return r;
}

static int gfx_v9_0_do_edc_gpr_workarounds(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->gfx.compute_ring[0];
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;
	int r, i, j, k;
	unsigned total_size, vgpr_offset, sgpr_offset;
	u64 gpu_addr;

	/* only support when RAS is enabled */
	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__GFX))
		return 0;

	/* bail if the compute ring is not ready */
	if (!ring->sched.ready)
		return 0;

	total_size =
		((ARRAY_SIZE(vgpr_init_regs) * 3) + 4 + 5 + 2) * 4;
	total_size +=
		((ARRAY_SIZE(sgpr_init_regs) * 3) + 4 + 5 + 2) * 4;
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
	for (i = 0; i < ARRAY_SIZE(vgpr_init_regs); i++) {
		ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 1);
		ib.ptr[ib.length_dw++] = SOC15_REG_ENTRY_OFFSET(vgpr_init_regs[i])
								- PACKET3_SET_SH_REG_START;
		ib.ptr[ib.length_dw++] = vgpr_init_regs[i].reg_value;
	}
	/* write the shader start address: mmCOMPUTE_PGM_LO, mmCOMPUTE_PGM_HI */
	gpu_addr = (ib.gpu_addr + (u64)vgpr_offset) >> 8;
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 2);
	ib.ptr[ib.length_dw++] = SOC15_REG_OFFSET(GC, 0, mmCOMPUTE_PGM_LO)
							- PACKET3_SET_SH_REG_START;
	ib.ptr[ib.length_dw++] = lower_32_bits(gpu_addr);
	ib.ptr[ib.length_dw++] = upper_32_bits(gpu_addr);

	/* write dispatch packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_DISPATCH_DIRECT, 3);
	ib.ptr[ib.length_dw++] = 128; /* x */
	ib.ptr[ib.length_dw++] = 1; /* y */
	ib.ptr[ib.length_dw++] = 1; /* z */
	ib.ptr[ib.length_dw++] =
		REG_SET_FIELD(0, COMPUTE_DISPATCH_INITIATOR, COMPUTE_SHADER_EN, 1);

	/* write CS partial flush packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_EVENT_WRITE, 0);
	ib.ptr[ib.length_dw++] = EVENT_TYPE(7) | EVENT_INDEX(4);

	/* SGPR */
	/* write the register state for the compute dispatch */
	for (i = 0; i < ARRAY_SIZE(sgpr_init_regs); i++) {
		ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 1);
		ib.ptr[ib.length_dw++] = SOC15_REG_ENTRY_OFFSET(sgpr_init_regs[i])
								- PACKET3_SET_SH_REG_START;
		ib.ptr[ib.length_dw++] = sgpr_init_regs[i].reg_value;
	}
	/* write the shader start address: mmCOMPUTE_PGM_LO, mmCOMPUTE_PGM_HI */
	gpu_addr = (ib.gpu_addr + (u64)sgpr_offset) >> 8;
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_SET_SH_REG, 2);
	ib.ptr[ib.length_dw++] = SOC15_REG_OFFSET(GC, 0, mmCOMPUTE_PGM_LO)
							- PACKET3_SET_SH_REG_START;
	ib.ptr[ib.length_dw++] = lower_32_bits(gpu_addr);
	ib.ptr[ib.length_dw++] = upper_32_bits(gpu_addr);

	/* write dispatch packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_DISPATCH_DIRECT, 3);
	ib.ptr[ib.length_dw++] = 128; /* x */
	ib.ptr[ib.length_dw++] = 1; /* y */
	ib.ptr[ib.length_dw++] = 1; /* z */
	ib.ptr[ib.length_dw++] =
		REG_SET_FIELD(0, COMPUTE_DISPATCH_INITIATOR, COMPUTE_SHADER_EN, 1);

	/* write CS partial flush packet */
	ib.ptr[ib.length_dw++] = PACKET3(PACKET3_EVENT_WRITE, 0);
	ib.ptr[ib.length_dw++] = EVENT_TYPE(7) | EVENT_INDEX(4);

	/* shedule the ib on the ring */
	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, &f);
	if (r) {
		DRM_ERROR("amdgpu: ib submit failed (%d).\n", r);
		goto fail;
	}

	/* wait for the GPU to finish processing the IB */
	r = dma_fence_wait(f, false);
	if (r) {
		DRM_ERROR("amdgpu: fence wait failed (%d).\n", r);
		goto fail;
	}

	/* read back registers to clear the counters */
	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < ARRAY_SIZE(sec_ded_counter_registers); i++) {
		for (j = 0; j < sec_ded_counter_registers[i].se_num; j++) {
			for (k = 0; k < sec_ded_counter_registers[i].instance; k++) {
				gfx_v9_0_select_se_sh(adev, j, 0x0, k);
				RREG32(SOC15_REG_ENTRY_OFFSET(sec_ded_counter_registers[i]));
			}
		}
	}
	WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, 0xe0000000);
	mutex_unlock(&adev->grbm_idx_mutex);

fail:
	amdgpu_ib_free(adev, &ib, NULL);
	dma_fence_put(f);

	return r;
}

static int gfx_v9_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->gfx.num_gfx_rings = GFX9_NUM_GFX_RINGS;
	adev->gfx.num_compute_rings = AMDGPU_MAX_COMPUTE_RINGS;
	gfx_v9_0_set_ring_funcs(adev);
	gfx_v9_0_set_irq_funcs(adev);
	gfx_v9_0_set_gds_init(adev);
	gfx_v9_0_set_rlc_funcs(adev);

	return 0;
}

static int gfx_v9_0_process_ras_data_cb(struct amdgpu_device *adev,
		struct amdgpu_iv_entry *entry);

static int gfx_v9_0_ecc_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ras_common_if **ras_if = &adev->gfx.ras_if;
	struct ras_ih_if ih_info = {
		.cb = gfx_v9_0_process_ras_data_cb,
	};
	struct ras_fs_if fs_info = {
		.sysfs_name = "gfx_err_count",
		.debugfs_name = "gfx_err_inject",
	};
	struct ras_common_if ras_block = {
		.block = AMDGPU_RAS_BLOCK__GFX,
		.type = AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE,
		.sub_block_index = 0,
		.name = "gfx",
	};
	int r;

	if (!amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__GFX)) {
		amdgpu_ras_feature_enable_on_boot(adev, &ras_block, 0);
		return 0;
	}

	r = gfx_v9_0_do_edc_gds_workarounds(adev);
	if (r)
		return r;

	/* requires IBs so do in late init after IB pool is initialized */
	r = gfx_v9_0_do_edc_gpr_workarounds(adev);
	if (r)
		return r;

	/* handle resume path. */
	if (*ras_if) {
		/* resend ras TA enable cmd during resume.
		 * prepare to handle failure.
		 */
		ih_info.head = **ras_if;
		r = amdgpu_ras_feature_enable_on_boot(adev, *ras_if, 1);
		if (r) {
			if (r == -EAGAIN) {
				/* request a gpu reset. will run again. */
				amdgpu_ras_request_reset_on_boot(adev,
						AMDGPU_RAS_BLOCK__GFX);
				return 0;
			}
			/* fail to enable ras, cleanup all. */
			goto irq;
		}
		/* enable successfully. continue. */
		goto resume;
	}

	*ras_if = kmalloc(sizeof(**ras_if), GFP_KERNEL);
	if (!*ras_if)
		return -ENOMEM;

	**ras_if = ras_block;

	r = amdgpu_ras_feature_enable_on_boot(adev, *ras_if, 1);
	if (r) {
		if (r == -EAGAIN) {
			amdgpu_ras_request_reset_on_boot(adev,
					AMDGPU_RAS_BLOCK__GFX);
			r = 0;
		}
		goto feature;
	}

	ih_info.head = **ras_if;
	fs_info.head = **ras_if;

	r = amdgpu_ras_interrupt_add_handler(adev, &ih_info);
	if (r)
		goto interrupt;

	amdgpu_ras_debugfs_create(adev, &fs_info);

	r = amdgpu_ras_sysfs_create(adev, &fs_info);
	if (r)
		goto sysfs;
resume:
	r = amdgpu_irq_get(adev, &adev->gfx.cp_ecc_error_irq, 0);
	if (r)
		goto irq;

	return 0;
irq:
	amdgpu_ras_sysfs_remove(adev, *ras_if);
sysfs:
	amdgpu_ras_debugfs_remove(adev, *ras_if);
	amdgpu_ras_interrupt_remove_handler(adev, &ih_info);
interrupt:
	amdgpu_ras_feature_enable(adev, *ras_if, 0);
feature:
	kfree(*ras_if);
	*ras_if = NULL;
	return r;
}

static int gfx_v9_0_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = amdgpu_irq_get(adev, &adev->gfx.priv_reg_irq, 0);
	if (r)
		return r;

	r = amdgpu_irq_get(adev, &adev->gfx.priv_inst_irq, 0);
	if (r)
		return r;

	r = gfx_v9_0_ecc_late_init(handle);
	if (r)
		return r;

	return 0;
}

static bool gfx_v9_0_is_rlc_enabled(struct amdgpu_device *adev)
{
	uint32_t rlc_setting;

	/* if RLC is not enabled, do nothing */
	rlc_setting = RREG32_SOC15(GC, 0, mmRLC_CNTL);
	if (!(rlc_setting & RLC_CNTL__RLC_ENABLE_F32_MASK))
		return false;

	return true;
}

static void gfx_v9_0_set_safe_mode(struct amdgpu_device *adev)
{
	uint32_t data;
	unsigned i;

	data = RLC_SAFE_MODE__CMD_MASK;
	data |= (1 << RLC_SAFE_MODE__MESSAGE__SHIFT);
	WREG32_SOC15(GC, 0, mmRLC_SAFE_MODE, data);

	/* wait for RLC_SAFE_MODE */
	for (i = 0; i < adev->usec_timeout; i++) {
		if (!REG_GET_FIELD(RREG32_SOC15(GC, 0, mmRLC_SAFE_MODE), RLC_SAFE_MODE, CMD))
			break;
		udelay(1);
	}
}

static void gfx_v9_0_unset_safe_mode(struct amdgpu_device *adev)
{
	uint32_t data;

	data = RLC_SAFE_MODE__CMD_MASK;
	WREG32_SOC15(GC, 0, mmRLC_SAFE_MODE, data);
}

static void gfx_v9_0_update_gfx_cg_power_gating(struct amdgpu_device *adev,
						bool enable)
{
	amdgpu_gfx_rlc_enter_safe_mode(adev);

	if ((adev->pg_flags & AMD_PG_SUPPORT_GFX_PG) && enable) {
		gfx_v9_0_enable_gfx_cg_power_gating(adev, true);
		if (adev->pg_flags & AMD_PG_SUPPORT_GFX_PIPELINE)
			gfx_v9_0_enable_gfx_pipeline_powergating(adev, true);
	} else {
		gfx_v9_0_enable_gfx_cg_power_gating(adev, false);
		gfx_v9_0_enable_gfx_pipeline_powergating(adev, false);
	}

	amdgpu_gfx_rlc_exit_safe_mode(adev);
}

static void gfx_v9_0_update_gfx_mg_power_gating(struct amdgpu_device *adev,
						bool enable)
{
	/* TODO: double check if we need to perform under safe mode */
	/* gfx_v9_0_enter_rlc_safe_mode(adev); */

	if ((adev->pg_flags & AMD_PG_SUPPORT_GFX_SMG) && enable)
		gfx_v9_0_enable_gfx_static_mg_power_gating(adev, true);
	else
		gfx_v9_0_enable_gfx_static_mg_power_gating(adev, false);

	if ((adev->pg_flags & AMD_PG_SUPPORT_GFX_DMG) && enable)
		gfx_v9_0_enable_gfx_dynamic_mg_power_gating(adev, true);
	else
		gfx_v9_0_enable_gfx_dynamic_mg_power_gating(adev, false);

	/* gfx_v9_0_exit_rlc_safe_mode(adev); */
}

static void gfx_v9_0_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t data, def;

	amdgpu_gfx_rlc_enter_safe_mode(adev);

	/* It is disabled by HW by default */
	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGCG)) {
		/* 1 - RLC_CGTT_MGCG_OVERRIDE */
		def = data = RREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE);

		if (adev->asic_type != CHIP_VEGA12)
			data &= ~RLC_CGTT_MGCG_OVERRIDE__CPF_CGTT_SCLK_OVERRIDE_MASK;

		data &= ~(RLC_CGTT_MGCG_OVERRIDE__GRBM_CGTT_SCLK_OVERRIDE_MASK |
			  RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGCG_OVERRIDE_MASK |
			  RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGLS_OVERRIDE_MASK);

		/* only for Vega10 & Raven1 */
		data |= RLC_CGTT_MGCG_OVERRIDE__RLC_CGTT_SCLK_OVERRIDE_MASK;

		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE, data);

		/* MGLS is a global flag to control all MGLS in GFX */
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_MGLS) {
			/* 2 - RLC memory Light sleep */
			if (adev->cg_flags & AMD_CG_SUPPORT_GFX_RLC_LS) {
				def = data = RREG32_SOC15(GC, 0, mmRLC_MEM_SLP_CNTL);
				data |= RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK;
				if (def != data)
					WREG32_SOC15(GC, 0, mmRLC_MEM_SLP_CNTL, data);
			}
			/* 3 - CP memory Light sleep */
			if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CP_LS) {
				def = data = RREG32_SOC15(GC, 0, mmCP_MEM_SLP_CNTL);
				data |= CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK;
				if (def != data)
					WREG32_SOC15(GC, 0, mmCP_MEM_SLP_CNTL, data);
			}
		}
	} else {
		/* 1 - MGCG_OVERRIDE */
		def = data = RREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE);

		if (adev->asic_type != CHIP_VEGA12)
			data |= RLC_CGTT_MGCG_OVERRIDE__CPF_CGTT_SCLK_OVERRIDE_MASK;

		data |= (RLC_CGTT_MGCG_OVERRIDE__RLC_CGTT_SCLK_OVERRIDE_MASK |
			 RLC_CGTT_MGCG_OVERRIDE__GRBM_CGTT_SCLK_OVERRIDE_MASK |
			 RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGCG_OVERRIDE_MASK |
			 RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGLS_OVERRIDE_MASK);

		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE, data);

		/* 2 - disable MGLS in RLC */
		data = RREG32_SOC15(GC, 0, mmRLC_MEM_SLP_CNTL);
		if (data & RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK) {
			data &= ~RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK;
			WREG32_SOC15(GC, 0, mmRLC_MEM_SLP_CNTL, data);
		}

		/* 3 - disable MGLS in CP */
		data = RREG32_SOC15(GC, 0, mmCP_MEM_SLP_CNTL);
		if (data & CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK) {
			data &= ~CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK;
			WREG32_SOC15(GC, 0, mmCP_MEM_SLP_CNTL, data);
		}
	}

	amdgpu_gfx_rlc_exit_safe_mode(adev);
}

static void gfx_v9_0_update_3d_clock_gating(struct amdgpu_device *adev,
					   bool enable)
{
	uint32_t data, def;

	amdgpu_gfx_rlc_enter_safe_mode(adev);

	/* Enable 3D CGCG/CGLS */
	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_3D_CGCG)) {
		/* write cmd to clear cgcg/cgls ov */
		def = data = RREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE);
		/* unset CGCG override */
		data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_GFX3D_CG_OVERRIDE_MASK;
		/* update CGCG and CGLS override bits */
		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE, data);

		/* enable 3Dcgcg FSM(0x0000363f) */
		def = RREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL_3D);

		data = (0x36 << RLC_CGCG_CGLS_CTRL_3D__CGCG_GFX_IDLE_THRESHOLD__SHIFT) |
			RLC_CGCG_CGLS_CTRL_3D__CGCG_EN_MASK;
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_3D_CGLS)
			data |= (0x000F << RLC_CGCG_CGLS_CTRL_3D__CGLS_REP_COMPANSAT_DELAY__SHIFT) |
				RLC_CGCG_CGLS_CTRL_3D__CGLS_EN_MASK;
		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL_3D, data);

		/* set IDLE_POLL_COUNT(0x00900100) */
		def = RREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_CNTL);
		data = (0x0100 << CP_RB_WPTR_POLL_CNTL__POLL_FREQUENCY__SHIFT) |
			(0x0090 << CP_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT__SHIFT);
		if (def != data)
			WREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_CNTL, data);
	} else {
		/* Disable CGCG/CGLS */
		def = data = RREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL_3D);
		/* disable cgcg, cgls should be disabled */
		data &= ~(RLC_CGCG_CGLS_CTRL_3D__CGCG_EN_MASK |
			  RLC_CGCG_CGLS_CTRL_3D__CGLS_EN_MASK);
		/* disable cgcg and cgls in FSM */
		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL_3D, data);
	}

	amdgpu_gfx_rlc_exit_safe_mode(adev);
}

static void gfx_v9_0_update_coarse_grain_clock_gating(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t def, data;

	amdgpu_gfx_rlc_enter_safe_mode(adev);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGCG)) {
		def = data = RREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE);
		/* unset CGCG override */
		data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_CGCG_OVERRIDE_MASK;
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGLS)
			data &= ~RLC_CGTT_MGCG_OVERRIDE__GFXIP_CGLS_OVERRIDE_MASK;
		else
			data |= RLC_CGTT_MGCG_OVERRIDE__GFXIP_CGLS_OVERRIDE_MASK;
		/* update CGCG and CGLS override bits */
		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE, data);

		/* enable cgcg FSM(0x0000363F) */
		def = RREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL);

		data = (0x36 << RLC_CGCG_CGLS_CTRL__CGCG_GFX_IDLE_THRESHOLD__SHIFT) |
			RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK;
		if (adev->cg_flags & AMD_CG_SUPPORT_GFX_CGLS)
			data |= (0x000F << RLC_CGCG_CGLS_CTRL__CGLS_REP_COMPANSAT_DELAY__SHIFT) |
				RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK;
		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL, data);

		/* set IDLE_POLL_COUNT(0x00900100) */
		def = RREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_CNTL);
		data = (0x0100 << CP_RB_WPTR_POLL_CNTL__POLL_FREQUENCY__SHIFT) |
			(0x0090 << CP_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT__SHIFT);
		if (def != data)
			WREG32_SOC15(GC, 0, mmCP_RB_WPTR_POLL_CNTL, data);
	} else {
		def = data = RREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL);
		/* reset CGCG/CGLS bits */
		data &= ~(RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK | RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK);
		/* disable cgcg and cgls in FSM */
		if (def != data)
			WREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL, data);
	}

	amdgpu_gfx_rlc_exit_safe_mode(adev);
}

static int gfx_v9_0_update_gfx_clock_gating(struct amdgpu_device *adev,
					    bool enable)
{
	if (enable) {
		/* CGCG/CGLS should be enabled after MGCG/MGLS
		 * ===  MGCG + MGLS ===
		 */
		gfx_v9_0_update_medium_grain_clock_gating(adev, enable);
		/* ===  CGCG /CGLS for GFX 3D Only === */
		gfx_v9_0_update_3d_clock_gating(adev, enable);
		/* ===  CGCG + CGLS === */
		gfx_v9_0_update_coarse_grain_clock_gating(adev, enable);
	} else {
		/* CGCG/CGLS should be disabled before MGCG/MGLS
		 * ===  CGCG + CGLS ===
		 */
		gfx_v9_0_update_coarse_grain_clock_gating(adev, enable);
		/* ===  CGCG /CGLS for GFX 3D Only === */
		gfx_v9_0_update_3d_clock_gating(adev, enable);
		/* ===  MGCG + MGLS === */
		gfx_v9_0_update_medium_grain_clock_gating(adev, enable);
	}
	return 0;
}

static const struct amdgpu_rlc_funcs gfx_v9_0_rlc_funcs = {
	.is_rlc_enabled = gfx_v9_0_is_rlc_enabled,
	.set_safe_mode = gfx_v9_0_set_safe_mode,
	.unset_safe_mode = gfx_v9_0_unset_safe_mode,
	.init = gfx_v9_0_rlc_init,
	.get_csb_size = gfx_v9_0_get_csb_size,
	.get_csb_buffer = gfx_v9_0_get_csb_buffer,
	.get_cp_table_num = gfx_v9_0_cp_jump_table_num,
	.resume = gfx_v9_0_rlc_resume,
	.stop = gfx_v9_0_rlc_stop,
	.reset = gfx_v9_0_rlc_reset,
	.start = gfx_v9_0_rlc_start
};

static int gfx_v9_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_PG_STATE_GATE) ? true : false;

	switch (adev->asic_type) {
	case CHIP_RAVEN:
		if (!enable) {
			amdgpu_gfx_off_ctrl(adev, false);
			cancel_delayed_work_sync(&adev->gfx.gfx_off_delay_work);
		}
		if (adev->pg_flags & AMD_PG_SUPPORT_RLC_SMU_HS) {
			gfx_v9_0_enable_sck_slow_down_on_power_up(adev, true);
			gfx_v9_0_enable_sck_slow_down_on_power_down(adev, true);
		} else {
			gfx_v9_0_enable_sck_slow_down_on_power_up(adev, false);
			gfx_v9_0_enable_sck_slow_down_on_power_down(adev, false);
		}

		if (adev->pg_flags & AMD_PG_SUPPORT_CP)
			gfx_v9_0_enable_cp_power_gating(adev, true);
		else
			gfx_v9_0_enable_cp_power_gating(adev, false);

		/* update gfx cgpg state */
		gfx_v9_0_update_gfx_cg_power_gating(adev, enable);

		/* update mgcg state */
		gfx_v9_0_update_gfx_mg_power_gating(adev, enable);

		if (enable)
			amdgpu_gfx_off_ctrl(adev, true);
		break;
	case CHIP_VEGA12:
		if (!enable) {
			amdgpu_gfx_off_ctrl(adev, false);
			cancel_delayed_work_sync(&adev->gfx.gfx_off_delay_work);
		} else {
			amdgpu_gfx_off_ctrl(adev, true);
		}
		break;
	default:
		break;
	}

	return 0;
}

static int gfx_v9_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
	case CHIP_RAVEN:
		gfx_v9_0_update_gfx_clock_gating(adev,
						 state == AMD_CG_STATE_GATE ? true : false);
		break;
	default:
		break;
	}
	return 0;
}

static void gfx_v9_0_get_clockgating_state(void *handle, u32 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int data;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	/* AMD_CG_SUPPORT_GFX_MGCG */
	data = RREG32_SOC15(GC, 0, mmRLC_CGTT_MGCG_OVERRIDE);
	if (!(data & RLC_CGTT_MGCG_OVERRIDE__GFXIP_MGCG_OVERRIDE_MASK))
		*flags |= AMD_CG_SUPPORT_GFX_MGCG;

	/* AMD_CG_SUPPORT_GFX_CGCG */
	data = RREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL);
	if (data & RLC_CGCG_CGLS_CTRL__CGCG_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CGCG;

	/* AMD_CG_SUPPORT_GFX_CGLS */
	if (data & RLC_CGCG_CGLS_CTRL__CGLS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CGLS;

	/* AMD_CG_SUPPORT_GFX_RLC_LS */
	data = RREG32_SOC15(GC, 0, mmRLC_MEM_SLP_CNTL);
	if (data & RLC_MEM_SLP_CNTL__RLC_MEM_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_RLC_LS | AMD_CG_SUPPORT_GFX_MGLS;

	/* AMD_CG_SUPPORT_GFX_CP_LS */
	data = RREG32_SOC15(GC, 0, mmCP_MEM_SLP_CNTL);
	if (data & CP_MEM_SLP_CNTL__CP_MEM_LS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_CP_LS | AMD_CG_SUPPORT_GFX_MGLS;

	/* AMD_CG_SUPPORT_GFX_3D_CGCG */
	data = RREG32_SOC15(GC, 0, mmRLC_CGCG_CGLS_CTRL_3D);
	if (data & RLC_CGCG_CGLS_CTRL_3D__CGCG_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_3D_CGCG;

	/* AMD_CG_SUPPORT_GFX_3D_CGLS */
	if (data & RLC_CGCG_CGLS_CTRL_3D__CGLS_EN_MASK)
		*flags |= AMD_CG_SUPPORT_GFX_3D_CGLS;
}

static u64 gfx_v9_0_ring_get_rptr_gfx(struct amdgpu_ring *ring)
{
	return ring->adev->wb.wb[ring->rptr_offs]; /* gfx9 is 32bit rptr*/
}

static u64 gfx_v9_0_ring_get_wptr_gfx(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u64 wptr;

	/* XXX check if swapping is necessary on BE */
	if (ring->use_doorbell) {
		wptr = atomic64_read((atomic64_t *)&adev->wb.wb[ring->wptr_offs]);
	} else {
		wptr = RREG32_SOC15(GC, 0, mmCP_RB0_WPTR);
		wptr += (u64)RREG32_SOC15(GC, 0, mmCP_RB0_WPTR_HI) << 32;
	}

	return wptr;
}

static void gfx_v9_0_ring_set_wptr_gfx(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		atomic64_set((atomic64_t*)&adev->wb.wb[ring->wptr_offs], ring->wptr);
		WDOORBELL64(ring->doorbell_index, ring->wptr);
	} else {
		WREG32_SOC15(GC, 0, mmCP_RB0_WPTR, lower_32_bits(ring->wptr));
		WREG32_SOC15(GC, 0, mmCP_RB0_WPTR_HI, upper_32_bits(ring->wptr));
	}
}

static void gfx_v9_0_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 ref_and_mask, reg_mem_engine;
	const struct nbio_hdp_flush_reg *nbio_hf_reg = adev->nbio_funcs->hdp_flush_reg;

	if (ring->funcs->type == AMDGPU_RING_TYPE_COMPUTE) {
		switch (ring->me) {
		case 1:
			ref_and_mask = nbio_hf_reg->ref_and_mask_cp2 << ring->pipe;
			break;
		case 2:
			ref_and_mask = nbio_hf_reg->ref_and_mask_cp6 << ring->pipe;
			break;
		default:
			return;
		}
		reg_mem_engine = 0;
	} else {
		ref_and_mask = nbio_hf_reg->ref_and_mask_cp0;
		reg_mem_engine = 1; /* pfp */
	}

	gfx_v9_0_wait_reg_mem(ring, reg_mem_engine, 0, 1,
			      adev->nbio_funcs->get_hdp_flush_req_offset(adev),
			      adev->nbio_funcs->get_hdp_flush_done_offset(adev),
			      ref_and_mask, ref_and_mask, 0x20);
}

static void gfx_v9_0_ring_emit_ib_gfx(struct amdgpu_ring *ring,
					struct amdgpu_job *job,
					struct amdgpu_ib *ib,
					uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);
	u32 header, control = 0;

	if (ib->flags & AMDGPU_IB_FLAG_CE)
		header = PACKET3(PACKET3_INDIRECT_BUFFER_CONST, 2);
	else
		header = PACKET3(PACKET3_INDIRECT_BUFFER, 2);

	control |= ib->length_dw | (vmid << 24);

	if (amdgpu_sriov_vf(ring->adev) && (ib->flags & AMDGPU_IB_FLAG_PREEMPT)) {
		control |= INDIRECT_BUFFER_PRE_ENB(1);

		if (!(ib->flags & AMDGPU_IB_FLAG_CE))
			gfx_v9_0_ring_emit_de_meta(ring);
	}

	amdgpu_ring_write(ring, header);
	BUG_ON(ib->gpu_addr & 0x3); /* Dword align */
	amdgpu_ring_write(ring,
#ifdef __BIG_ENDIAN
		(2 << 0) |
#endif
		lower_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, control);
}

static void gfx_v9_0_ring_emit_ib_compute(struct amdgpu_ring *ring,
					  struct amdgpu_job *job,
					  struct amdgpu_ib *ib,
					  uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);
	u32 control = INDIRECT_BUFFER_VALID | ib->length_dw | (vmid << 24);

	/* Currently, there is a high possibility to get wave ID mismatch
	 * between ME and GDS, leading to a hw deadlock, because ME generates
	 * different wave IDs than the GDS expects. This situation happens
	 * randomly when at least 5 compute pipes use GDS ordered append.
	 * The wave IDs generated by ME are also wrong after suspend/resume.
	 * Those are probably bugs somewhere else in the kernel driver.
	 *
	 * Writing GDS_COMPUTE_MAX_WAVE_ID resets wave ID counters in ME and
	 * GDS to 0 for this ring (me/pipe).
	 */
	if (ib->flags & AMDGPU_IB_FLAG_RESET_GDS_MAX_WAVE_ID) {
		amdgpu_ring_write(ring, PACKET3(PACKET3_SET_CONFIG_REG, 1));
		amdgpu_ring_write(ring, mmGDS_COMPUTE_MAX_WAVE_ID);
		amdgpu_ring_write(ring, ring->adev->gds.gds_compute_max_wave_id);
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_INDIRECT_BUFFER, 2));
	BUG_ON(ib->gpu_addr & 0x3); /* Dword align */
	amdgpu_ring_write(ring,
#ifdef __BIG_ENDIAN
				(2 << 0) |
#endif
				lower_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, control);
}

static void gfx_v9_0_ring_emit_fence(struct amdgpu_ring *ring, u64 addr,
				     u64 seq, unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	bool int_sel = flags & AMDGPU_FENCE_FLAG_INT;
	bool writeback = flags & AMDGPU_FENCE_FLAG_TC_WB_ONLY;

	/* RELEASE_MEM - flush caches, send int */
	amdgpu_ring_write(ring, PACKET3(PACKET3_RELEASE_MEM, 6));
	amdgpu_ring_write(ring, ((writeback ? (EOP_TC_WB_ACTION_EN |
					       EOP_TC_NC_ACTION_EN) :
					      (EOP_TCL1_ACTION_EN |
					       EOP_TC_ACTION_EN |
					       EOP_TC_WB_ACTION_EN |
					       EOP_TC_MD_ACTION_EN)) |
				 EVENT_TYPE(CACHE_FLUSH_AND_INV_TS_EVENT) |
				 EVENT_INDEX(5)));
	amdgpu_ring_write(ring, DATA_SEL(write64bit ? 2 : 1) | INT_SEL(int_sel ? 2 : 0));

	/*
	 * the address should be Qword aligned if 64bit write, Dword
	 * aligned if only send 32bit data low (discard data high)
	 */
	if (write64bit)
		BUG_ON(addr & 0x7);
	else
		BUG_ON(addr & 0x3);
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));
	amdgpu_ring_write(ring, upper_32_bits(seq));
	amdgpu_ring_write(ring, 0);
}

static void gfx_v9_0_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	int usepfp = (ring->funcs->type == AMDGPU_RING_TYPE_GFX);
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	gfx_v9_0_wait_reg_mem(ring, usepfp, 1, 0,
			      lower_32_bits(addr), upper_32_bits(addr),
			      seq, 0xffffffff, 4);
}

static void gfx_v9_0_ring_emit_vm_flush(struct amdgpu_ring *ring,
					unsigned vmid, uint64_t pd_addr)
{
	amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	/* compute doesn't have PFP */
	if (ring->funcs->type == AMDGPU_RING_TYPE_GFX) {
		/* sync PFP to ME, otherwise we might get invalid PFP reads */
		amdgpu_ring_write(ring, PACKET3(PACKET3_PFP_SYNC_ME, 0));
		amdgpu_ring_write(ring, 0x0);
	}
}

static u64 gfx_v9_0_ring_get_rptr_compute(struct amdgpu_ring *ring)
{
	return ring->adev->wb.wb[ring->rptr_offs]; /* gfx9 hardware is 32bit rptr */
}

static u64 gfx_v9_0_ring_get_wptr_compute(struct amdgpu_ring *ring)
{
	u64 wptr;

	/* XXX check if swapping is necessary on BE */
	if (ring->use_doorbell)
		wptr = atomic64_read((atomic64_t *)&ring->adev->wb.wb[ring->wptr_offs]);
	else
		BUG();
	return wptr;
}

static void gfx_v9_0_ring_set_pipe_percent(struct amdgpu_ring *ring,
					   bool acquire)
{
	struct amdgpu_device *adev = ring->adev;
	int pipe_num, tmp, reg;
	int pipe_percent = acquire ? SPI_WCL_PIPE_PERCENT_GFX__VALUE_MASK : 0x1;

	pipe_num = ring->me * adev->gfx.mec.num_pipe_per_mec + ring->pipe;

	/* first me only has 2 entries, GFX and HP3D */
	if (ring->me > 0)
		pipe_num -= 2;

	reg = SOC15_REG_OFFSET(GC, 0, mmSPI_WCL_PIPE_PERCENT_GFX) + pipe_num;
	tmp = RREG32(reg);
	tmp = REG_SET_FIELD(tmp, SPI_WCL_PIPE_PERCENT_GFX, VALUE, pipe_percent);
	WREG32(reg, tmp);
}

static void gfx_v9_0_pipe_reserve_resources(struct amdgpu_device *adev,
					    struct amdgpu_ring *ring,
					    bool acquire)
{
	int i, pipe;
	bool reserve;
	struct amdgpu_ring *iring;

	mutex_lock(&adev->gfx.pipe_reserve_mutex);
	pipe = amdgpu_gfx_mec_queue_to_bit(adev, ring->me, ring->pipe, 0);
	if (acquire)
		set_bit(pipe, adev->gfx.pipe_reserve_bitmap);
	else
		clear_bit(pipe, adev->gfx.pipe_reserve_bitmap);

	if (!bitmap_weight(adev->gfx.pipe_reserve_bitmap, AMDGPU_MAX_COMPUTE_QUEUES)) {
		/* Clear all reservations - everyone reacquires all resources */
		for (i = 0; i < adev->gfx.num_gfx_rings; ++i)
			gfx_v9_0_ring_set_pipe_percent(&adev->gfx.gfx_ring[i],
						       true);

		for (i = 0; i < adev->gfx.num_compute_rings; ++i)
			gfx_v9_0_ring_set_pipe_percent(&adev->gfx.compute_ring[i],
						       true);
	} else {
		/* Lower all pipes without a current reservation */
		for (i = 0; i < adev->gfx.num_gfx_rings; ++i) {
			iring = &adev->gfx.gfx_ring[i];
			pipe = amdgpu_gfx_mec_queue_to_bit(adev,
							   iring->me,
							   iring->pipe,
							   0);
			reserve = test_bit(pipe, adev->gfx.pipe_reserve_bitmap);
			gfx_v9_0_ring_set_pipe_percent(iring, reserve);
		}

		for (i = 0; i < adev->gfx.num_compute_rings; ++i) {
			iring = &adev->gfx.compute_ring[i];
			pipe = amdgpu_gfx_mec_queue_to_bit(adev,
							   iring->me,
							   iring->pipe,
							   0);
			reserve = test_bit(pipe, adev->gfx.pipe_reserve_bitmap);
			gfx_v9_0_ring_set_pipe_percent(iring, reserve);
		}
	}

	mutex_unlock(&adev->gfx.pipe_reserve_mutex);
}

static void gfx_v9_0_hqd_set_priority(struct amdgpu_device *adev,
				      struct amdgpu_ring *ring,
				      bool acquire)
{
	uint32_t pipe_priority = acquire ? 0x2 : 0x0;
	uint32_t queue_priority = acquire ? 0xf : 0x0;

	mutex_lock(&adev->srbm_mutex);
	soc15_grbm_select(adev, ring->me, ring->pipe, ring->queue, 0);

	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_PIPE_PRIORITY, pipe_priority);
	WREG32_SOC15_RLC(GC, 0, mmCP_HQD_QUEUE_PRIORITY, queue_priority);

	soc15_grbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);
}

static void gfx_v9_0_ring_set_priority_compute(struct amdgpu_ring *ring,
					       enum drm_sched_priority priority)
{
	struct amdgpu_device *adev = ring->adev;
	bool acquire = priority == DRM_SCHED_PRIORITY_HIGH_HW;

	if (ring->funcs->type != AMDGPU_RING_TYPE_COMPUTE)
		return;

	gfx_v9_0_hqd_set_priority(adev, ring, acquire);
	gfx_v9_0_pipe_reserve_resources(adev, ring, acquire);
}

static void gfx_v9_0_ring_set_wptr_compute(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	/* XXX check if swapping is necessary on BE */
	if (ring->use_doorbell) {
		atomic64_set((atomic64_t*)&adev->wb.wb[ring->wptr_offs], ring->wptr);
		WDOORBELL64(ring->doorbell_index, ring->wptr);
	} else{
		BUG(); /* only DOORBELL method supported on gfx9 now */
	}
}

static void gfx_v9_0_ring_emit_fence_kiq(struct amdgpu_ring *ring, u64 addr,
					 u64 seq, unsigned int flags)
{
	struct amdgpu_device *adev = ring->adev;

	/* we only allocate 32bit for each seq wb address */
	BUG_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	/* write fence seq to the "addr" */
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
				 WRITE_DATA_DST_SEL(5) | WR_CONFIRM));
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));

	if (flags & AMDGPU_FENCE_FLAG_INT) {
		/* set register to trigger INT */
		amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
		amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(0) |
					 WRITE_DATA_DST_SEL(0) | WR_CONFIRM));
		amdgpu_ring_write(ring, SOC15_REG_OFFSET(GC, 0, mmCPC_INT_STATUS));
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring, 0x20000000); /* src_id is 178 */
	}
}

static void gfx_v9_ring_emit_sb(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_SWITCH_BUFFER, 0));
	amdgpu_ring_write(ring, 0);
}

static void gfx_v9_0_ring_emit_ce_meta(struct amdgpu_ring *ring)
{
	struct v9_ce_ib_state ce_payload = {0};
	uint64_t csa_addr;
	int cnt;

	cnt = (sizeof(ce_payload) >> 2) + 4 - 2;
	csa_addr = amdgpu_csa_vaddr(ring->adev);

	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, cnt));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(2) |
				 WRITE_DATA_DST_SEL(8) |
				 WR_CONFIRM) |
				 WRITE_DATA_CACHE_POLICY(0));
	amdgpu_ring_write(ring, lower_32_bits(csa_addr + offsetof(struct v9_gfx_meta_data, ce_payload)));
	amdgpu_ring_write(ring, upper_32_bits(csa_addr + offsetof(struct v9_gfx_meta_data, ce_payload)));
	amdgpu_ring_write_multiple(ring, (void *)&ce_payload, sizeof(ce_payload) >> 2);
}

static void gfx_v9_0_ring_emit_de_meta(struct amdgpu_ring *ring)
{
	struct v9_de_ib_state de_payload = {0};
	uint64_t csa_addr, gds_addr;
	int cnt;

	csa_addr = amdgpu_csa_vaddr(ring->adev);
	gds_addr = csa_addr + 4096;
	de_payload.gds_backup_addrlo = lower_32_bits(gds_addr);
	de_payload.gds_backup_addrhi = upper_32_bits(gds_addr);

	cnt = (sizeof(de_payload) >> 2) + 4 - 2;
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, cnt));
	amdgpu_ring_write(ring, (WRITE_DATA_ENGINE_SEL(1) |
				 WRITE_DATA_DST_SEL(8) |
				 WR_CONFIRM) |
				 WRITE_DATA_CACHE_POLICY(0));
	amdgpu_ring_write(ring, lower_32_bits(csa_addr + offsetof(struct v9_gfx_meta_data, de_payload)));
	amdgpu_ring_write(ring, upper_32_bits(csa_addr + offsetof(struct v9_gfx_meta_data, de_payload)));
	amdgpu_ring_write_multiple(ring, (void *)&de_payload, sizeof(de_payload) >> 2);
}

static void gfx_v9_0_ring_emit_tmz(struct amdgpu_ring *ring, bool start)
{
	amdgpu_ring_write(ring, PACKET3(PACKET3_FRAME_CONTROL, 0));
	amdgpu_ring_write(ring, FRAME_CMD(start ? 0 : 1)); /* frame_end */
}

static void gfx_v9_ring_emit_cntxcntl(struct amdgpu_ring *ring, uint32_t flags)
{
	uint32_t dw2 = 0;

	if (amdgpu_sriov_vf(ring->adev))
		gfx_v9_0_ring_emit_ce_meta(ring);

	gfx_v9_0_ring_emit_tmz(ring, true);

	dw2 |= 0x80000000; /* set load_enable otherwise this package is just NOPs */
	if (flags & AMDGPU_HAVE_CTX_SWITCH) {
		/* set load_global_config & load_global_uconfig */
		dw2 |= 0x8001;
		/* set load_cs_sh_regs */
		dw2 |= 0x01000000;
		/* set load_per_context_state & load_gfx_sh_regs for GFX */
		dw2 |= 0x10002;

		/* set load_ce_ram if preamble presented */
		if (AMDGPU_PREAMBLE_IB_PRESENT & flags)
			dw2 |= 0x10000000;
	} else {
		/* still load_ce_ram if this is the first time preamble presented
		 * although there is no context switch happens.
		 */
		if (AMDGPU_PREAMBLE_IB_PRESENT_FIRST & flags)
			dw2 |= 0x10000000;
	}

	amdgpu_ring_write(ring, PACKET3(PACKET3_CONTEXT_CONTROL, 1));
	amdgpu_ring_write(ring, dw2);
	amdgpu_ring_write(ring, 0);
}

static unsigned gfx_v9_0_ring_emit_init_cond_exec(struct amdgpu_ring *ring)
{
	unsigned ret;
	amdgpu_ring_write(ring, PACKET3(PACKET3_COND_EXEC, 3));
	amdgpu_ring_write(ring, lower_32_bits(ring->cond_exe_gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(ring->cond_exe_gpu_addr));
	amdgpu_ring_write(ring, 0); /* discard following DWs if *cond_exec_gpu_addr==0 */
	ret = ring->wptr & ring->buf_mask;
	amdgpu_ring_write(ring, 0x55aa55aa); /* patch dummy value later */
	return ret;
}

static void gfx_v9_0_ring_emit_patch_cond_exec(struct amdgpu_ring *ring, unsigned offset)
{
	unsigned cur;
	BUG_ON(offset > ring->buf_mask);
	BUG_ON(ring->ring[offset] != 0x55aa55aa);

	cur = (ring->wptr & ring->buf_mask) - 1;
	if (likely(cur > offset))
		ring->ring[offset] = cur - offset;
	else
		ring->ring[offset] = (ring->ring_size>>2) - offset + cur;
}

static void gfx_v9_0_ring_emit_rreg(struct amdgpu_ring *ring, uint32_t reg)
{
	struct amdgpu_device *adev = ring->adev;

	amdgpu_ring_write(ring, PACKET3(PACKET3_COPY_DATA, 4));
	amdgpu_ring_write(ring, 0 |	/* src: register*/
				(5 << 8) |	/* dst: memory */
				(1 << 20));	/* write confirm */
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, lower_32_bits(adev->wb.gpu_addr +
				adev->virt.reg_val_offs * 4));
	amdgpu_ring_write(ring, upper_32_bits(adev->wb.gpu_addr +
				adev->virt.reg_val_offs * 4));
}

static void gfx_v9_0_ring_emit_wreg(struct amdgpu_ring *ring, uint32_t reg,
				    uint32_t val)
{
	uint32_t cmd = 0;

	switch (ring->funcs->type) {
	case AMDGPU_RING_TYPE_GFX:
		cmd = WRITE_DATA_ENGINE_SEL(1) | WR_CONFIRM;
		break;
	case AMDGPU_RING_TYPE_KIQ:
		cmd = (1 << 16); /* no inc addr */
		break;
	default:
		cmd = WR_CONFIRM;
		break;
	}
	amdgpu_ring_write(ring, PACKET3(PACKET3_WRITE_DATA, 3));
	amdgpu_ring_write(ring, cmd);
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, val);
}

static void gfx_v9_0_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
					uint32_t val, uint32_t mask)
{
	gfx_v9_0_wait_reg_mem(ring, 0, 0, 0, reg, 0, val, mask, 0x20);
}

static void gfx_v9_0_ring_emit_reg_write_reg_wait(struct amdgpu_ring *ring,
						  uint32_t reg0, uint32_t reg1,
						  uint32_t ref, uint32_t mask)
{
	int usepfp = (ring->funcs->type == AMDGPU_RING_TYPE_GFX);
	struct amdgpu_device *adev = ring->adev;
	bool fw_version_ok = (ring->funcs->type == AMDGPU_RING_TYPE_GFX) ?
		adev->gfx.me_fw_write_wait : adev->gfx.mec_fw_write_wait;

	if (fw_version_ok)
		gfx_v9_0_wait_reg_mem(ring, usepfp, 0, 1, reg0, reg1,
				      ref, mask, 0x20);
	else
		amdgpu_ring_emit_reg_write_reg_wait_helper(ring, reg0, reg1,
							   ref, mask);
}

static void gfx_v9_0_ring_soft_recovery(struct amdgpu_ring *ring, unsigned vmid)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t value = 0;

	value = REG_SET_FIELD(value, SQ_CMD, CMD, 0x03);
	value = REG_SET_FIELD(value, SQ_CMD, MODE, 0x01);
	value = REG_SET_FIELD(value, SQ_CMD, CHECK_VMID, 1);
	value = REG_SET_FIELD(value, SQ_CMD, VM_ID, vmid);
	WREG32(mmSQ_CMD, value);
}

static void gfx_v9_0_set_gfx_eop_interrupt_state(struct amdgpu_device *adev,
						 enum amdgpu_interrupt_state state)
{
	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
	case AMDGPU_IRQ_STATE_ENABLE:
		WREG32_FIELD15(GC, 0, CP_INT_CNTL_RING0,
			       TIME_STAMP_INT_ENABLE,
			       state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
		break;
	default:
		break;
	}
}

static void gfx_v9_0_set_compute_eop_interrupt_state(struct amdgpu_device *adev,
						     int me, int pipe,
						     enum amdgpu_interrupt_state state)
{
	u32 mec_int_cntl, mec_int_cntl_reg;

	/*
	 * amdgpu controls only the first MEC. That's why this function only
	 * handles the setting of interrupts for this specific MEC. All other
	 * pipes' interrupts are set by amdkfd.
	 */

	if (me == 1) {
		switch (pipe) {
		case 0:
			mec_int_cntl_reg = SOC15_REG_OFFSET(GC, 0, mmCP_ME1_PIPE0_INT_CNTL);
			break;
		case 1:
			mec_int_cntl_reg = SOC15_REG_OFFSET(GC, 0, mmCP_ME1_PIPE1_INT_CNTL);
			break;
		case 2:
			mec_int_cntl_reg = SOC15_REG_OFFSET(GC, 0, mmCP_ME1_PIPE2_INT_CNTL);
			break;
		case 3:
			mec_int_cntl_reg = SOC15_REG_OFFSET(GC, 0, mmCP_ME1_PIPE3_INT_CNTL);
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

static int gfx_v9_0_set_priv_reg_fault_state(struct amdgpu_device *adev,
					     struct amdgpu_irq_src *source,
					     unsigned type,
					     enum amdgpu_interrupt_state state)
{
	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
	case AMDGPU_IRQ_STATE_ENABLE:
		WREG32_FIELD15(GC, 0, CP_INT_CNTL_RING0,
			       PRIV_REG_INT_ENABLE,
			       state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
		break;
	default:
		break;
	}

	return 0;
}

static int gfx_v9_0_set_priv_inst_fault_state(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      unsigned type,
					      enum amdgpu_interrupt_state state)
{
	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
	case AMDGPU_IRQ_STATE_ENABLE:
		WREG32_FIELD15(GC, 0, CP_INT_CNTL_RING0,
			       PRIV_INSTR_INT_ENABLE,
			       state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
	default:
		break;
	}

	return 0;
}

#define ENABLE_ECC_ON_ME_PIPE(me, pipe)				\
	WREG32_FIELD15(GC, 0, CP_ME##me##_PIPE##pipe##_INT_CNTL,\
			CP_ECC_ERROR_INT_ENABLE, 1)

#define DISABLE_ECC_ON_ME_PIPE(me, pipe)			\
	WREG32_FIELD15(GC, 0, CP_ME##me##_PIPE##pipe##_INT_CNTL,\
			CP_ECC_ERROR_INT_ENABLE, 0)

static int gfx_v9_0_set_cp_ecc_error_state(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      unsigned type,
					      enum amdgpu_interrupt_state state)
{
	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		WREG32_FIELD15(GC, 0, CP_INT_CNTL_RING0,
				CP_ECC_ERROR_INT_ENABLE, 0);
		DISABLE_ECC_ON_ME_PIPE(1, 0);
		DISABLE_ECC_ON_ME_PIPE(1, 1);
		DISABLE_ECC_ON_ME_PIPE(1, 2);
		DISABLE_ECC_ON_ME_PIPE(1, 3);
		break;

	case AMDGPU_IRQ_STATE_ENABLE:
		WREG32_FIELD15(GC, 0, CP_INT_CNTL_RING0,
				CP_ECC_ERROR_INT_ENABLE, 1);
		ENABLE_ECC_ON_ME_PIPE(1, 0);
		ENABLE_ECC_ON_ME_PIPE(1, 1);
		ENABLE_ECC_ON_ME_PIPE(1, 2);
		ENABLE_ECC_ON_ME_PIPE(1, 3);
		break;
	default:
		break;
	}

	return 0;
}


static int gfx_v9_0_set_eop_interrupt_state(struct amdgpu_device *adev,
					    struct amdgpu_irq_src *src,
					    unsigned type,
					    enum amdgpu_interrupt_state state)
{
	switch (type) {
	case AMDGPU_CP_IRQ_GFX_ME0_PIPE0_EOP:
		gfx_v9_0_set_gfx_eop_interrupt_state(adev, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE0_EOP:
		gfx_v9_0_set_compute_eop_interrupt_state(adev, 1, 0, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE1_EOP:
		gfx_v9_0_set_compute_eop_interrupt_state(adev, 1, 1, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE2_EOP:
		gfx_v9_0_set_compute_eop_interrupt_state(adev, 1, 2, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC1_PIPE3_EOP:
		gfx_v9_0_set_compute_eop_interrupt_state(adev, 1, 3, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE0_EOP:
		gfx_v9_0_set_compute_eop_interrupt_state(adev, 2, 0, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE1_EOP:
		gfx_v9_0_set_compute_eop_interrupt_state(adev, 2, 1, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE2_EOP:
		gfx_v9_0_set_compute_eop_interrupt_state(adev, 2, 2, state);
		break;
	case AMDGPU_CP_IRQ_COMPUTE_MEC2_PIPE3_EOP:
		gfx_v9_0_set_compute_eop_interrupt_state(adev, 2, 3, state);
		break;
	default:
		break;
	}
	return 0;
}

static int gfx_v9_0_eop_irq(struct amdgpu_device *adev,
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

static void gfx_v9_0_fault(struct amdgpu_device *adev,
			   struct amdgpu_iv_entry *entry)
{
	u8 me_id, pipe_id, queue_id;
	struct amdgpu_ring *ring;
	int i;

	me_id = (entry->ring_id & 0x0c) >> 2;
	pipe_id = (entry->ring_id & 0x03) >> 0;
	queue_id = (entry->ring_id & 0x70) >> 4;

	switch (me_id) {
	case 0:
		drm_sched_fault(&adev->gfx.gfx_ring[0].sched);
		break;
	case 1:
	case 2:
		for (i = 0; i < adev->gfx.num_compute_rings; i++) {
			ring = &adev->gfx.compute_ring[i];
			if (ring->me == me_id && ring->pipe == pipe_id &&
			    ring->queue == queue_id)
				drm_sched_fault(&ring->sched);
		}
		break;
	}
}

static int gfx_v9_0_priv_reg_irq(struct amdgpu_device *adev,
				 struct amdgpu_irq_src *source,
				 struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal register access in command stream\n");
	gfx_v9_0_fault(adev, entry);
	return 0;
}

static int gfx_v9_0_priv_inst_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal instruction in command stream\n");
	gfx_v9_0_fault(adev, entry);
	return 0;
}

static int gfx_v9_0_process_ras_data_cb(struct amdgpu_device *adev,
		struct amdgpu_iv_entry *entry)
{
	/* TODO ue will trigger an interrupt. */
	kgd2kfd_set_sram_ecc_flag(adev->kfd.dev);
	amdgpu_ras_reset_gpu(adev, 0);
	return AMDGPU_RAS_UE;
}

static int gfx_v9_0_cp_ecc_error_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry)
{
	struct ras_common_if *ras_if = adev->gfx.ras_if;
	struct ras_dispatch_if ih_data = {
		.entry = entry,
	};

	if (!ras_if)
		return 0;

	ih_data.head = *ras_if;

	DRM_ERROR("CP ECC ERROR IRQ\n");
	amdgpu_ras_interrupt_dispatch(adev, &ih_data);
	return 0;
}

static const struct amd_ip_funcs gfx_v9_0_ip_funcs = {
	.name = "gfx_v9_0",
	.early_init = gfx_v9_0_early_init,
	.late_init = gfx_v9_0_late_init,
	.sw_init = gfx_v9_0_sw_init,
	.sw_fini = gfx_v9_0_sw_fini,
	.hw_init = gfx_v9_0_hw_init,
	.hw_fini = gfx_v9_0_hw_fini,
	.suspend = gfx_v9_0_suspend,
	.resume = gfx_v9_0_resume,
	.is_idle = gfx_v9_0_is_idle,
	.wait_for_idle = gfx_v9_0_wait_for_idle,
	.soft_reset = gfx_v9_0_soft_reset,
	.set_clockgating_state = gfx_v9_0_set_clockgating_state,
	.set_powergating_state = gfx_v9_0_set_powergating_state,
	.get_clockgating_state = gfx_v9_0_get_clockgating_state,
};

static const struct amdgpu_ring_funcs gfx_v9_0_ring_funcs_gfx = {
	.type = AMDGPU_RING_TYPE_GFX,
	.align_mask = 0xff,
	.nop = PACKET3(PACKET3_NOP, 0x3FFF),
	.support_64bit_ptrs = true,
	.vmhub = AMDGPU_GFXHUB,
	.get_rptr = gfx_v9_0_ring_get_rptr_gfx,
	.get_wptr = gfx_v9_0_ring_get_wptr_gfx,
	.set_wptr = gfx_v9_0_ring_set_wptr_gfx,
	.emit_frame_size = /* totally 242 maximum if 16 IBs */
		5 +  /* COND_EXEC */
		7 +  /* PIPELINE_SYNC */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 5 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 7 +
		2 + /* VM_FLUSH */
		8 +  /* FENCE for VM_FLUSH */
		20 + /* GDS switch */
		4 + /* double SWITCH_BUFFER,
		       the first COND_EXEC jump to the place just
			   prior to this double SWITCH_BUFFER  */
		5 + /* COND_EXEC */
		7 +	 /*	HDP_flush */
		4 +	 /*	VGT_flush */
		14 + /*	CE_META */
		31 + /*	DE_META */
		3 + /* CNTX_CTRL */
		5 + /* HDP_INVL */
		8 + 8 + /* FENCE x2 */
		2, /* SWITCH_BUFFER */
	.emit_ib_size =	4, /* gfx_v9_0_ring_emit_ib_gfx */
	.emit_ib = gfx_v9_0_ring_emit_ib_gfx,
	.emit_fence = gfx_v9_0_ring_emit_fence,
	.emit_pipeline_sync = gfx_v9_0_ring_emit_pipeline_sync,
	.emit_vm_flush = gfx_v9_0_ring_emit_vm_flush,
	.emit_gds_switch = gfx_v9_0_ring_emit_gds_switch,
	.emit_hdp_flush = gfx_v9_0_ring_emit_hdp_flush,
	.test_ring = gfx_v9_0_ring_test_ring,
	.test_ib = gfx_v9_0_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.emit_switch_buffer = gfx_v9_ring_emit_sb,
	.emit_cntxcntl = gfx_v9_ring_emit_cntxcntl,
	.init_cond_exec = gfx_v9_0_ring_emit_init_cond_exec,
	.patch_cond_exec = gfx_v9_0_ring_emit_patch_cond_exec,
	.emit_tmz = gfx_v9_0_ring_emit_tmz,
	.emit_wreg = gfx_v9_0_ring_emit_wreg,
	.emit_reg_wait = gfx_v9_0_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = gfx_v9_0_ring_emit_reg_write_reg_wait,
	.soft_recovery = gfx_v9_0_ring_soft_recovery,
};

static const struct amdgpu_ring_funcs gfx_v9_0_ring_funcs_compute = {
	.type = AMDGPU_RING_TYPE_COMPUTE,
	.align_mask = 0xff,
	.nop = PACKET3(PACKET3_NOP, 0x3FFF),
	.support_64bit_ptrs = true,
	.vmhub = AMDGPU_GFXHUB,
	.get_rptr = gfx_v9_0_ring_get_rptr_compute,
	.get_wptr = gfx_v9_0_ring_get_wptr_compute,
	.set_wptr = gfx_v9_0_ring_set_wptr_compute,
	.emit_frame_size =
		20 + /* gfx_v9_0_ring_emit_gds_switch */
		7 + /* gfx_v9_0_ring_emit_hdp_flush */
		5 + /* hdp invalidate */
		7 + /* gfx_v9_0_ring_emit_pipeline_sync */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 5 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 7 +
		2 + /* gfx_v9_0_ring_emit_vm_flush */
		8 + 8 + 8, /* gfx_v9_0_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size =	7, /* gfx_v9_0_ring_emit_ib_compute */
	.emit_ib = gfx_v9_0_ring_emit_ib_compute,
	.emit_fence = gfx_v9_0_ring_emit_fence,
	.emit_pipeline_sync = gfx_v9_0_ring_emit_pipeline_sync,
	.emit_vm_flush = gfx_v9_0_ring_emit_vm_flush,
	.emit_gds_switch = gfx_v9_0_ring_emit_gds_switch,
	.emit_hdp_flush = gfx_v9_0_ring_emit_hdp_flush,
	.test_ring = gfx_v9_0_ring_test_ring,
	.test_ib = gfx_v9_0_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.set_priority = gfx_v9_0_ring_set_priority_compute,
	.emit_wreg = gfx_v9_0_ring_emit_wreg,
	.emit_reg_wait = gfx_v9_0_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = gfx_v9_0_ring_emit_reg_write_reg_wait,
};

static const struct amdgpu_ring_funcs gfx_v9_0_ring_funcs_kiq = {
	.type = AMDGPU_RING_TYPE_KIQ,
	.align_mask = 0xff,
	.nop = PACKET3(PACKET3_NOP, 0x3FFF),
	.support_64bit_ptrs = true,
	.vmhub = AMDGPU_GFXHUB,
	.get_rptr = gfx_v9_0_ring_get_rptr_compute,
	.get_wptr = gfx_v9_0_ring_get_wptr_compute,
	.set_wptr = gfx_v9_0_ring_set_wptr_compute,
	.emit_frame_size =
		20 + /* gfx_v9_0_ring_emit_gds_switch */
		7 + /* gfx_v9_0_ring_emit_hdp_flush */
		5 + /* hdp invalidate */
		7 + /* gfx_v9_0_ring_emit_pipeline_sync */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 5 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 7 +
		2 + /* gfx_v9_0_ring_emit_vm_flush */
		8 + 8 + 8, /* gfx_v9_0_ring_emit_fence_kiq x3 for user fence, vm fence */
	.emit_ib_size =	7, /* gfx_v9_0_ring_emit_ib_compute */
	.emit_fence = gfx_v9_0_ring_emit_fence_kiq,
	.test_ring = gfx_v9_0_ring_test_ring,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.emit_rreg = gfx_v9_0_ring_emit_rreg,
	.emit_wreg = gfx_v9_0_ring_emit_wreg,
	.emit_reg_wait = gfx_v9_0_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = gfx_v9_0_ring_emit_reg_write_reg_wait,
};

static void gfx_v9_0_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	adev->gfx.kiq.ring.funcs = &gfx_v9_0_ring_funcs_kiq;

	for (i = 0; i < adev->gfx.num_gfx_rings; i++)
		adev->gfx.gfx_ring[i].funcs = &gfx_v9_0_ring_funcs_gfx;

	for (i = 0; i < adev->gfx.num_compute_rings; i++)
		adev->gfx.compute_ring[i].funcs = &gfx_v9_0_ring_funcs_compute;
}

static const struct amdgpu_irq_src_funcs gfx_v9_0_eop_irq_funcs = {
	.set = gfx_v9_0_set_eop_interrupt_state,
	.process = gfx_v9_0_eop_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v9_0_priv_reg_irq_funcs = {
	.set = gfx_v9_0_set_priv_reg_fault_state,
	.process = gfx_v9_0_priv_reg_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v9_0_priv_inst_irq_funcs = {
	.set = gfx_v9_0_set_priv_inst_fault_state,
	.process = gfx_v9_0_priv_inst_irq,
};

static const struct amdgpu_irq_src_funcs gfx_v9_0_cp_ecc_error_irq_funcs = {
	.set = gfx_v9_0_set_cp_ecc_error_state,
	.process = gfx_v9_0_cp_ecc_error_irq,
};


static void gfx_v9_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gfx.eop_irq.num_types = AMDGPU_CP_IRQ_LAST;
	adev->gfx.eop_irq.funcs = &gfx_v9_0_eop_irq_funcs;

	adev->gfx.priv_reg_irq.num_types = 1;
	adev->gfx.priv_reg_irq.funcs = &gfx_v9_0_priv_reg_irq_funcs;

	adev->gfx.priv_inst_irq.num_types = 1;
	adev->gfx.priv_inst_irq.funcs = &gfx_v9_0_priv_inst_irq_funcs;

	adev->gfx.cp_ecc_error_irq.num_types = 2; /*C5 ECC error and C9 FUE error*/
	adev->gfx.cp_ecc_error_irq.funcs = &gfx_v9_0_cp_ecc_error_irq_funcs;
}

static void gfx_v9_0_set_rlc_funcs(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
	case CHIP_RAVEN:
		adev->gfx.rlc.funcs = &gfx_v9_0_rlc_funcs;
		break;
	default:
		break;
	}
}

static void gfx_v9_0_set_gds_init(struct amdgpu_device *adev)
{
	/* init asci gds info */
	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
		adev->gds.gds_size = 0x10000;
		break;
	case CHIP_RAVEN:
		adev->gds.gds_size = 0x1000;
		break;
	default:
		adev->gds.gds_size = 0x10000;
		break;
	}

	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_VEGA20:
		adev->gds.gds_compute_max_wave_id = 0x7ff;
		break;
	case CHIP_VEGA12:
		adev->gds.gds_compute_max_wave_id = 0x27f;
		break;
	case CHIP_RAVEN:
		if (adev->rev_id >= 0x8)
			adev->gds.gds_compute_max_wave_id = 0x77; /* raven2 */
		else
			adev->gds.gds_compute_max_wave_id = 0x15f; /* raven1 */
		break;
	default:
		/* this really depends on the chip */
		adev->gds.gds_compute_max_wave_id = 0x7ff;
		break;
	}

	adev->gds.gws_size = 64;
	adev->gds.oa_size = 16;
}

static void gfx_v9_0_set_user_cu_inactive_bitmap(struct amdgpu_device *adev,
						 u32 bitmap)
{
	u32 data;

	if (!bitmap)
		return;

	data = bitmap << GC_USER_SHADER_ARRAY_CONFIG__INACTIVE_CUS__SHIFT;
	data &= GC_USER_SHADER_ARRAY_CONFIG__INACTIVE_CUS_MASK;

	WREG32_SOC15(GC, 0, mmGC_USER_SHADER_ARRAY_CONFIG, data);
}

static u32 gfx_v9_0_get_cu_active_bitmap(struct amdgpu_device *adev)
{
	u32 data, mask;

	data = RREG32_SOC15(GC, 0, mmCC_GC_SHADER_ARRAY_CONFIG);
	data |= RREG32_SOC15(GC, 0, mmGC_USER_SHADER_ARRAY_CONFIG);

	data &= CC_GC_SHADER_ARRAY_CONFIG__INACTIVE_CUS_MASK;
	data >>= CC_GC_SHADER_ARRAY_CONFIG__INACTIVE_CUS__SHIFT;

	mask = amdgpu_gfx_create_bitmask(adev->gfx.config.max_cu_per_sh);

	return (~data) & mask;
}

static int gfx_v9_0_get_cu_info(struct amdgpu_device *adev,
				 struct amdgpu_cu_info *cu_info)
{
	int i, j, k, counter, active_cu_number = 0;
	u32 mask, bitmap, ao_bitmap, ao_cu_mask = 0;
	unsigned disable_masks[4 * 2];

	if (!adev || !cu_info)
		return -EINVAL;

	amdgpu_gfx_parse_disable_cu(disable_masks, 4, 2);

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			mask = 1;
			ao_bitmap = 0;
			counter = 0;
			gfx_v9_0_select_se_sh(adev, i, j, 0xffffffff);
			if (i < 4 && j < 2)
				gfx_v9_0_set_user_cu_inactive_bitmap(
					adev, disable_masks[i * 2 + j]);
			bitmap = gfx_v9_0_get_cu_active_bitmap(adev);
			cu_info->bitmap[i][j] = bitmap;

			for (k = 0; k < adev->gfx.config.max_cu_per_sh; k ++) {
				if (bitmap & mask) {
					if (counter < adev->gfx.config.max_cu_per_sh)
						ao_bitmap |= mask;
					counter ++;
				}
				mask <<= 1;
			}
			active_cu_number += counter;
			if (i < 2 && j < 2)
				ao_cu_mask |= (ao_bitmap << (i * 16 + j * 8));
			cu_info->ao_cu_bitmap[i][j] = ao_bitmap;
		}
	}
	gfx_v9_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);

	cu_info->number = active_cu_number;
	cu_info->ao_cu_mask = ao_cu_mask;
	cu_info->simd_per_cu = NUM_SIMD_PER_CU;

	return 0;
}

const struct amdgpu_ip_block_version gfx_v9_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_GFX,
	.major = 9,
	.minor = 0,
	.rev = 0,
	.funcs = &gfx_v9_0_ip_funcs,
};
