/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#include <linux/slab.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_atombios.h"
#include "amdgpu_ih.h"
#include "amdgpu_uvd.h"
#include "amdgpu_vce.h"
#include "atom.h"
#include "amd_pcie.h"
#include "si_dpm.h"
#include "sid.h"
#include "si_ih.h"
#include "gfx_v6_0.h"
#include "gmc_v6_0.h"
#include "si_dma.h"
#include "dce_v6_0.h"
#include "si.h"
#include "dce_virtual.h"
#include "gca/gfx_6_0_d.h"
#include "oss/oss_1_0_d.h"
#include "gmc/gmc_6_0_d.h"
#include "dce/dce_6_0_d.h"
#include "uvd/uvd_4_0_d.h"
#include "bif/bif_3_0_d.h"

static const u32 tahiti_golden_registers[] =
{
	mmAZALIA_SCLK_CONTROL, 0x00000030, 0x00000011,
	mmCB_HW_CONTROL, 0x00010000, 0x00018208,
	mmDB_DEBUG, 0xffffffff, 0x00000000,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmDB_DEBUG3, 0x0002021c, 0x00020200,
	mmDCI_CLK_CNTL, 0x00000080, 0x00000000,
	0x340c, 0x000000c0, 0x00800040,
	0x360c, 0x000000c0, 0x00800040,
	mmFBC_DEBUG_COMP, 0x000000f0, 0x00000070,
	mmFBC_MISC, 0x00200000, 0x50100000,
	mmDIG0_HDMI_CONTROL, 0x31000311, 0x00000011,
	mmMC_ARB_WTM_CNTL_RD, 0x00000003, 0x000007ff,
	mmMC_XPB_P2P_BAR_CFG, 0x000007ff, 0x00000000,
	mmPA_CL_ENHANCE, 0xf000001f, 0x00000007,
	mmPA_SC_FORCE_EOV_MAX_CNTS, 0xffffffff, 0x00ffffff,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmPA_SC_MODE_CNTL_1, 0x07ffffff, 0x4e000000,
	mmPA_SC_RASTER_CONFIG, 0x3f3f3fff, 0x2a00126a,
	0x000c, 0xffffffff, 0x0040,
	0x000d, 0x00000040, 0x00004040,
	mmSPI_CONFIG_CNTL, 0x07ffffff, 0x03000000,
	mmSQ_DED_CNT, 0x01ff1f3f, 0x00000000,
	mmSQ_SEC_CNT, 0x01ff1f3f, 0x00000000,
	mmSX_DEBUG_1, 0x0000007f, 0x00000020,
	mmTA_CNTL_AUX, 0x00010000, 0x00010000,
	mmTCP_ADDR_CONFIG, 0x00000200, 0x000002fb,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x0000543b,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0xa9210876,
	mmVGT_FIFO_DEPTHS, 0xffffffff, 0x000fff40,
	mmVGT_GS_VERTEX_REUSE, 0x0000001f, 0x00000010,
	mmVM_CONTEXT0_CNTL, 0x20000000, 0x20fffed8,
	mmVM_L2_CG, 0x000c0fc0, 0x000c0400,
	mmVM_PRT_APERTURE0_LOW_ADDR, 0x0fffffff, 0xffffffff,
	mmVM_PRT_APERTURE1_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE2_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE3_LOW_ADDR, 0x0fffffff, 0x0fffffff,
};

static const u32 tahiti_golden_registers2[] =
{
	mmMCIF_MEM_CONTROL, 0x00000001, 0x00000001,
};

static const u32 tahiti_golden_rlc_registers[] =
{
	mmGB_ADDR_CONFIG, 0xffffffff, 0x12011003,
	mmRLC_LB_PARAMS, 0xffffffff, 0x00601005,
	0x311f, 0xffffffff, 0x10104040,
	0x3122, 0xffffffff, 0x0100000a,
	mmRLC_LB_CNTR_MAX, 0xffffffff, 0x00000800,
	mmRLC_LB_CNTL, 0xffffffff, 0x800000f4,
	mmUVD_CGC_GATE, 0x00000008, 0x00000000,
};

static const u32 pitcairn_golden_registers[] =
{
	mmAZALIA_SCLK_CONTROL, 0x00000030, 0x00000011,
	mmCB_HW_CONTROL, 0x00010000, 0x00018208,
	mmDB_DEBUG, 0xffffffff, 0x00000000,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmDB_DEBUG3, 0x0002021c, 0x00020200,
	mmDCI_CLK_CNTL, 0x00000080, 0x00000000,
	0x340c, 0x000300c0, 0x00800040,
	0x360c, 0x000300c0, 0x00800040,
	mmFBC_DEBUG_COMP, 0x000000f0, 0x00000070,
	mmFBC_MISC, 0x00200000, 0x50100000,
	mmDIG0_HDMI_CONTROL, 0x31000311, 0x00000011,
	mmMC_SEQ_PMG_PG_HWCNTL, 0x00073ffe, 0x000022a2,
	mmMC_XPB_P2P_BAR_CFG, 0x000007ff, 0x00000000,
	mmPA_CL_ENHANCE, 0xf000001f, 0x00000007,
	mmPA_SC_FORCE_EOV_MAX_CNTS, 0xffffffff, 0x00ffffff,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmPA_SC_MODE_CNTL_1, 0x07ffffff, 0x4e000000,
	mmPA_SC_RASTER_CONFIG, 0x3f3f3fff, 0x2a00126a,
	0x000c, 0xffffffff, 0x0040,
	0x000d, 0x00000040, 0x00004040,
	mmSPI_CONFIG_CNTL, 0x07ffffff, 0x03000000,
	mmSX_DEBUG_1, 0x0000007f, 0x00000020,
	mmTA_CNTL_AUX, 0x00010000, 0x00010000,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000000f7,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0x32761054,
	mmVGT_GS_VERTEX_REUSE, 0x0000001f, 0x00000010,
	mmVM_L2_CG, 0x000c0fc0, 0x000c0400,
	mmVM_PRT_APERTURE0_LOW_ADDR, 0x0fffffff, 0xffffffff,
	mmVM_PRT_APERTURE1_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE2_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE3_LOW_ADDR, 0x0fffffff, 0x0fffffff,
};

static const u32 pitcairn_golden_rlc_registers[] =
{
	mmGB_ADDR_CONFIG, 0xffffffff, 0x12011003,
	mmRLC_LB_PARAMS, 0xffffffff, 0x00601004,
	0x311f, 0xffffffff, 0x10102020,
	0x3122, 0xffffffff, 0x01000020,
	mmRLC_LB_CNTR_MAX, 0xffffffff, 0x00000800,
	mmRLC_LB_CNTL, 0xffffffff, 0x800000a4,
};

static const u32 verde_pg_init[] =
{
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x40000,
	mmGMCON_PGFSM_CONFIG, 0xffffffff, 0x200010ff,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x7007,
	mmGMCON_PGFSM_CONFIG, 0xffffffff, 0x300010ff,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x400000,
	mmGMCON_PGFSM_CONFIG, 0xffffffff, 0x100010ff,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x120200,
	mmGMCON_PGFSM_CONFIG, 0xffffffff, 0x500010ff,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x1e1e16,
	mmGMCON_PGFSM_CONFIG, 0xffffffff, 0x600010ff,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x171f1e,
	mmGMCON_PGFSM_CONFIG, 0xffffffff, 0x700010ff,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_WRITE, 0xffffffff, 0x0,
	mmGMCON_PGFSM_CONFIG, 0xffffffff, 0x9ff,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x0,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x10000800,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0xf,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0xf,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x4,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x1000051e,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0xffff,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0xffff,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x8,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x80500,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x12,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x9050c,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x1d,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0xb052c,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x2a,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x1053e,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x2d,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x10546,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x30,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0xa054e,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x3c,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x1055f,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x3f,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x10567,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x42,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x1056f,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x45,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x10572,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x48,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x20575,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x4c,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x190801,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x67,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x1082a,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x6a,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x1b082d,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x87,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x310851,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0xba,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x891,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0xbc,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x893,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0xbe,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x20895,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0xc2,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x20899,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0xc6,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x2089d,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0xca,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x8a1,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0xcc,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x8a3,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0xce,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x308a5,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0xd3,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x6d08cd,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x142,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x2000095a,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x1,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x144,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x301f095b,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x165,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0xc094d,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x173,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0xf096d,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x184,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x15097f,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x19b,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0xc0998,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x1a9,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x409a7,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x1af,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0xcdc,
	mmGMCON_RENG_RAM_INDEX, 0xffffffff, 0x1b1,
	mmGMCON_RENG_RAM_DATA, 0xffffffff, 0x800,
	mmGMCON_RENG_EXECUTE, 0xffffffff, 0x6c9b2000,
	mmGMCON_MISC2, 0xfc00, 0x2000,
	mmGMCON_MISC3, 0xffffffff, 0xfc0,
	mmMC_PMG_AUTO_CFG, 0x00000100, 0x100,
};

static const u32 verde_golden_rlc_registers[] =
{
	mmGB_ADDR_CONFIG, 0xffffffff, 0x02010002,
	mmRLC_LB_PARAMS, 0xffffffff, 0x033f1005,
	0x311f, 0xffffffff, 0x10808020,
	0x3122, 0xffffffff, 0x00800008,
	mmRLC_LB_CNTR_MAX, 0xffffffff, 0x00001000,
	mmRLC_LB_CNTL, 0xffffffff, 0x80010014,
};

static const u32 verde_golden_registers[] =
{
	mmAZALIA_SCLK_CONTROL, 0x00000030, 0x00000011,
	mmCB_HW_CONTROL, 0x00010000, 0x00018208,
	mmDB_DEBUG, 0xffffffff, 0x00000000,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmDB_DEBUG3, 0x0002021c, 0x00020200,
	mmDCI_CLK_CNTL, 0x00000080, 0x00000000,
	0x340c, 0x000300c0, 0x00800040,
	0x360c, 0x000300c0, 0x00800040,
	mmFBC_DEBUG_COMP, 0x000000f0, 0x00000070,
	mmFBC_MISC, 0x00200000, 0x50100000,
	mmDIG0_HDMI_CONTROL, 0x31000311, 0x00000011,
	mmMC_SEQ_PMG_PG_HWCNTL, 0x00073ffe, 0x000022a2,
	mmMC_XPB_P2P_BAR_CFG, 0x000007ff, 0x00000000,
	mmPA_CL_ENHANCE, 0xf000001f, 0x00000007,
	mmPA_SC_FORCE_EOV_MAX_CNTS, 0xffffffff, 0x00ffffff,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmPA_SC_MODE_CNTL_1, 0x07ffffff, 0x4e000000,
	mmPA_SC_RASTER_CONFIG, 0x3f3f3fff, 0x0000124a,
	0x000c, 0xffffffff, 0x0040,
	0x000d, 0x00000040, 0x00004040,
	mmSPI_CONFIG_CNTL, 0x07ffffff, 0x03000000,
	mmSQ_DED_CNT, 0x01ff1f3f, 0x00000000,
	mmSQ_SEC_CNT, 0x01ff1f3f, 0x00000000,
	mmSX_DEBUG_1, 0x0000007f, 0x00000020,
	mmTA_CNTL_AUX, 0x00010000, 0x00010000,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x00000003,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0x00001032,
	mmVGT_GS_VERTEX_REUSE, 0x0000001f, 0x00000010,
	mmVM_L2_CG, 0x000c0fc0, 0x000c0400,
	mmVM_PRT_APERTURE0_LOW_ADDR, 0x0fffffff, 0xffffffff,
	mmVM_PRT_APERTURE1_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE2_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE3_LOW_ADDR, 0x0fffffff, 0x0fffffff,
};

static const u32 oland_golden_registers[] =
{
	mmAZALIA_SCLK_CONTROL, 0x00000030, 0x00000011,
	mmCB_HW_CONTROL, 0x00010000, 0x00018208,
	mmDB_DEBUG, 0xffffffff, 0x00000000,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmDB_DEBUG3, 0x0002021c, 0x00020200,
	mmDCI_CLK_CNTL, 0x00000080, 0x00000000,
	0x340c, 0x000300c0, 0x00800040,
	0x360c, 0x000300c0, 0x00800040,
	mmFBC_DEBUG_COMP, 0x000000f0, 0x00000070,
	mmFBC_MISC, 0x00200000, 0x50100000,
	mmDIG0_HDMI_CONTROL, 0x31000311, 0x00000011,
	mmMC_SEQ_PMG_PG_HWCNTL, 0x00073ffe, 0x000022a2,
	mmMC_XPB_P2P_BAR_CFG, 0x000007ff, 0x00000000,
	mmPA_CL_ENHANCE, 0xf000001f, 0x00000007,
	mmPA_SC_FORCE_EOV_MAX_CNTS, 0xffffffff, 0x00ffffff,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmPA_SC_MODE_CNTL_1, 0x07ffffff, 0x4e000000,
	mmPA_SC_RASTER_CONFIG, 0x3f3f3fff, 0x00000082,
	0x000c, 0xffffffff, 0x0040,
	0x000d, 0x00000040, 0x00004040,
	mmSPI_CONFIG_CNTL, 0x07ffffff, 0x03000000,
	mmSX_DEBUG_1, 0x0000007f, 0x00000020,
	mmTA_CNTL_AUX, 0x00010000, 0x00010000,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000000f3,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0x00003210,
	mmVGT_GS_VERTEX_REUSE, 0x0000001f, 0x00000010,
	mmVM_L2_CG, 0x000c0fc0, 0x000c0400,
	mmVM_PRT_APERTURE0_LOW_ADDR, 0x0fffffff, 0xffffffff,
	mmVM_PRT_APERTURE1_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE2_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE3_LOW_ADDR, 0x0fffffff, 0x0fffffff,

};

static const u32 oland_golden_rlc_registers[] =
{
	mmGB_ADDR_CONFIG, 0xffffffff, 0x02010002,
	mmRLC_LB_PARAMS, 0xffffffff, 0x00601005,
	0x311f, 0xffffffff, 0x10104040,
	0x3122, 0xffffffff, 0x0100000a,
	mmRLC_LB_CNTR_MAX, 0xffffffff, 0x00000800,
	mmRLC_LB_CNTL, 0xffffffff, 0x800000f4,
};

static const u32 hainan_golden_registers[] =
{
	0x17bc, 0x00000030, 0x00000011,
	mmCB_HW_CONTROL, 0x00010000, 0x00018208,
	mmDB_DEBUG, 0xffffffff, 0x00000000,
	mmDB_DEBUG2, 0xf00fffff, 0x00000400,
	mmDB_DEBUG3, 0x0002021c, 0x00020200,
	0x031e, 0x00000080, 0x00000000,
	0x3430, 0xff000fff, 0x00000100,
	0x340c, 0x000300c0, 0x00800040,
	0x3630, 0xff000fff, 0x00000100,
	0x360c, 0x000300c0, 0x00800040,
	0x16ec, 0x000000f0, 0x00000070,
	0x16f0, 0x00200000, 0x50100000,
	0x1c0c, 0x31000311, 0x00000011,
	mmMC_SEQ_PMG_PG_HWCNTL, 0x00073ffe, 0x000022a2,
	mmMC_XPB_P2P_BAR_CFG, 0x000007ff, 0x00000000,
	mmPA_CL_ENHANCE, 0xf000001f, 0x00000007,
	mmPA_SC_FORCE_EOV_MAX_CNTS, 0xffffffff, 0x00ffffff,
	mmPA_SC_LINE_STIPPLE_STATE, 0x0000ff0f, 0x00000000,
	mmPA_SC_MODE_CNTL_1, 0x07ffffff, 0x4e000000,
	mmPA_SC_RASTER_CONFIG, 0x3f3f3fff, 0x00000000,
	0x000c, 0xffffffff, 0x0040,
	0x000d, 0x00000040, 0x00004040,
	mmSPI_CONFIG_CNTL, 0x03e00000, 0x03600000,
	mmSX_DEBUG_1, 0x0000007f, 0x00000020,
	mmTA_CNTL_AUX, 0x00010000, 0x00010000,
	mmTCP_ADDR_CONFIG, 0x000003ff, 0x000000f1,
	mmTCP_CHAN_STEER_HI, 0xffffffff, 0x00000000,
	mmTCP_CHAN_STEER_LO, 0xffffffff, 0x00003210,
	mmVGT_GS_VERTEX_REUSE, 0x0000001f, 0x00000010,
	mmVM_L2_CG, 0x000c0fc0, 0x000c0400,
	mmVM_PRT_APERTURE0_LOW_ADDR, 0x0fffffff, 0xffffffff,
	mmVM_PRT_APERTURE1_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE2_LOW_ADDR, 0x0fffffff, 0x0fffffff,
	mmVM_PRT_APERTURE3_LOW_ADDR, 0x0fffffff, 0x0fffffff,
};

static const u32 hainan_golden_registers2[] =
{
	mmGB_ADDR_CONFIG, 0xffffffff, 0x2011003,
};

static const u32 tahiti_mgcg_cgcg_init[] =
{
	mmRLC_CGTT_MGCG_OVERRIDE, 0xffffffff, 0xfffffffc,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCB_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_BCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_GDS_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_IA_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PA_CLK_CTRL, 0xffffffff, 0x00000100,
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
	mmCGTT_TCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_TCP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_VGT_CLK_CTRL, 0xffffffff, 0x06000100,
	mmDB_CGTT_CLK_CTRL_0, 0xffffffff, 0x00000100,
	mmTA_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmTCA_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTCC_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTD_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	0x2458, 0xffffffff, 0x00010000,
	0x2459, 0xffffffff, 0x00030002,
	0x245a, 0xffffffff, 0x00040007,
	0x245b, 0xffffffff, 0x00060005,
	0x245c, 0xffffffff, 0x00090008,
	0x245d, 0xffffffff, 0x00020001,
	0x245e, 0xffffffff, 0x00040003,
	0x245f, 0xffffffff, 0x00000007,
	0x2460, 0xffffffff, 0x00060005,
	0x2461, 0xffffffff, 0x00090008,
	0x2462, 0xffffffff, 0x00030002,
	0x2463, 0xffffffff, 0x00050004,
	0x2464, 0xffffffff, 0x00000008,
	0x2465, 0xffffffff, 0x00070006,
	0x2466, 0xffffffff, 0x000a0009,
	0x2467, 0xffffffff, 0x00040003,
	0x2468, 0xffffffff, 0x00060005,
	0x2469, 0xffffffff, 0x00000009,
	0x246a, 0xffffffff, 0x00080007,
	0x246b, 0xffffffff, 0x000b000a,
	0x246c, 0xffffffff, 0x00050004,
	0x246d, 0xffffffff, 0x00070006,
	0x246e, 0xffffffff, 0x0008000b,
	0x246f, 0xffffffff, 0x000a0009,
	0x2470, 0xffffffff, 0x000d000c,
	0x2471, 0xffffffff, 0x00060005,
	0x2472, 0xffffffff, 0x00080007,
	0x2473, 0xffffffff, 0x0000000b,
	0x2474, 0xffffffff, 0x000a0009,
	0x2475, 0xffffffff, 0x000d000c,
	0x2476, 0xffffffff, 0x00070006,
	0x2477, 0xffffffff, 0x00090008,
	0x2478, 0xffffffff, 0x0000000c,
	0x2479, 0xffffffff, 0x000b000a,
	0x247a, 0xffffffff, 0x000e000d,
	0x247b, 0xffffffff, 0x00080007,
	0x247c, 0xffffffff, 0x000a0009,
	0x247d, 0xffffffff, 0x0000000d,
	0x247e, 0xffffffff, 0x000c000b,
	0x247f, 0xffffffff, 0x000f000e,
	0x2480, 0xffffffff, 0x00090008,
	0x2481, 0xffffffff, 0x000b000a,
	0x2482, 0xffffffff, 0x000c000f,
	0x2483, 0xffffffff, 0x000e000d,
	0x2484, 0xffffffff, 0x00110010,
	0x2485, 0xffffffff, 0x000a0009,
	0x2486, 0xffffffff, 0x000c000b,
	0x2487, 0xffffffff, 0x0000000f,
	0x2488, 0xffffffff, 0x000e000d,
	0x2489, 0xffffffff, 0x00110010,
	0x248a, 0xffffffff, 0x000b000a,
	0x248b, 0xffffffff, 0x000d000c,
	0x248c, 0xffffffff, 0x00000010,
	0x248d, 0xffffffff, 0x000f000e,
	0x248e, 0xffffffff, 0x00120011,
	0x248f, 0xffffffff, 0x000c000b,
	0x2490, 0xffffffff, 0x000e000d,
	0x2491, 0xffffffff, 0x00000011,
	0x2492, 0xffffffff, 0x0010000f,
	0x2493, 0xffffffff, 0x00130012,
	0x2494, 0xffffffff, 0x000d000c,
	0x2495, 0xffffffff, 0x000f000e,
	0x2496, 0xffffffff, 0x00100013,
	0x2497, 0xffffffff, 0x00120011,
	0x2498, 0xffffffff, 0x00150014,
	0x2499, 0xffffffff, 0x000e000d,
	0x249a, 0xffffffff, 0x0010000f,
	0x249b, 0xffffffff, 0x00000013,
	0x249c, 0xffffffff, 0x00120011,
	0x249d, 0xffffffff, 0x00150014,
	0x249e, 0xffffffff, 0x000f000e,
	0x249f, 0xffffffff, 0x00110010,
	0x24a0, 0xffffffff, 0x00000014,
	0x24a1, 0xffffffff, 0x00130012,
	0x24a2, 0xffffffff, 0x00160015,
	0x24a3, 0xffffffff, 0x0010000f,
	0x24a4, 0xffffffff, 0x00120011,
	0x24a5, 0xffffffff, 0x00000015,
	0x24a6, 0xffffffff, 0x00140013,
	0x24a7, 0xffffffff, 0x00170016,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96940200,
	mmCP_RB_WPTR_POLL_CNTL, 0xffffffff, 0x00900100,
	mmRLC_GCPM_GENERAL_3, 0xffffffff, 0x00000080,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003f,
	0x000c, 0xffffffff, 0x0000001c,
	0x000d, 0x000f0000, 0x000f0000,
	0x0583, 0xffffffff, 0x00000100,
	mmXDMA_CLOCK_GATING_CNTL, 0xffffffff, 0x00000100,
	mmXDMA_MEM_POWER_CNTL, 0x00000101, 0x00000000,
	mmMC_MEM_POWER_LS, 0xffffffff, 0x00000104,
	mmMC_CITF_MISC_WR_CG, 0x000c0000, 0x000c0000,
	mmMC_CITF_MISC_RD_CG, 0x000c0000, 0x000c0000,
	mmCGTT_DRM_CLK_CTRL0, 0xff000fff, 0x00000100,
	0x157a, 0x00000001, 0x00000001,
	mmHDP_MEM_POWER_LS, 0x00000001, 0x00000001,
	mmHDP_XDP_CGTT_BLK_CTRL, 0xc0000fff, 0x00000104,
	mmCP_MEM_SLP_CNTL, 0x00000001, 0x00000001,
	0x3430, 0xfffffff0, 0x00000100,
	0x3630, 0xfffffff0, 0x00000100,
};
static const u32 pitcairn_mgcg_cgcg_init[] =
{
	mmRLC_CGTT_MGCG_OVERRIDE, 0xffffffff, 0xfffffffc,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCB_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_BCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_GDS_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_IA_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PA_CLK_CTRL, 0xffffffff, 0x00000100,
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
	mmCGTT_TCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_TCP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_VGT_CLK_CTRL, 0xffffffff, 0x06000100,
	mmDB_CGTT_CLK_CTRL_0, 0xffffffff, 0x00000100,
	mmTA_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmTCA_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTCC_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTD_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	0x2458, 0xffffffff, 0x00010000,
	0x2459, 0xffffffff, 0x00030002,
	0x245a, 0xffffffff, 0x00040007,
	0x245b, 0xffffffff, 0x00060005,
	0x245c, 0xffffffff, 0x00090008,
	0x245d, 0xffffffff, 0x00020001,
	0x245e, 0xffffffff, 0x00040003,
	0x245f, 0xffffffff, 0x00000007,
	0x2460, 0xffffffff, 0x00060005,
	0x2461, 0xffffffff, 0x00090008,
	0x2462, 0xffffffff, 0x00030002,
	0x2463, 0xffffffff, 0x00050004,
	0x2464, 0xffffffff, 0x00000008,
	0x2465, 0xffffffff, 0x00070006,
	0x2466, 0xffffffff, 0x000a0009,
	0x2467, 0xffffffff, 0x00040003,
	0x2468, 0xffffffff, 0x00060005,
	0x2469, 0xffffffff, 0x00000009,
	0x246a, 0xffffffff, 0x00080007,
	0x246b, 0xffffffff, 0x000b000a,
	0x246c, 0xffffffff, 0x00050004,
	0x246d, 0xffffffff, 0x00070006,
	0x246e, 0xffffffff, 0x0008000b,
	0x246f, 0xffffffff, 0x000a0009,
	0x2470, 0xffffffff, 0x000d000c,
	0x2480, 0xffffffff, 0x00090008,
	0x2481, 0xffffffff, 0x000b000a,
	0x2482, 0xffffffff, 0x000c000f,
	0x2483, 0xffffffff, 0x000e000d,
	0x2484, 0xffffffff, 0x00110010,
	0x2485, 0xffffffff, 0x000a0009,
	0x2486, 0xffffffff, 0x000c000b,
	0x2487, 0xffffffff, 0x0000000f,
	0x2488, 0xffffffff, 0x000e000d,
	0x2489, 0xffffffff, 0x00110010,
	0x248a, 0xffffffff, 0x000b000a,
	0x248b, 0xffffffff, 0x000d000c,
	0x248c, 0xffffffff, 0x00000010,
	0x248d, 0xffffffff, 0x000f000e,
	0x248e, 0xffffffff, 0x00120011,
	0x248f, 0xffffffff, 0x000c000b,
	0x2490, 0xffffffff, 0x000e000d,
	0x2491, 0xffffffff, 0x00000011,
	0x2492, 0xffffffff, 0x0010000f,
	0x2493, 0xffffffff, 0x00130012,
	0x2494, 0xffffffff, 0x000d000c,
	0x2495, 0xffffffff, 0x000f000e,
	0x2496, 0xffffffff, 0x00100013,
	0x2497, 0xffffffff, 0x00120011,
	0x2498, 0xffffffff, 0x00150014,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96940200,
	mmCP_RB_WPTR_POLL_CNTL, 0xffffffff, 0x00900100,
	mmRLC_GCPM_GENERAL_3, 0xffffffff, 0x00000080,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003f,
	0x000c, 0xffffffff, 0x0000001c,
	0x000d, 0x000f0000, 0x000f0000,
	0x0583, 0xffffffff, 0x00000100,
	mmXDMA_CLOCK_GATING_CNTL, 0xffffffff, 0x00000100,
	mmXDMA_MEM_POWER_CNTL, 0x00000101, 0x00000000,
	mmMC_MEM_POWER_LS, 0xffffffff, 0x00000104,
	mmCGTT_DRM_CLK_CTRL0, 0xff000fff, 0x00000100,
	0x157a, 0x00000001, 0x00000001,
	mmHDP_MEM_POWER_LS, 0x00000001, 0x00000001,
	mmHDP_XDP_CGTT_BLK_CTRL, 0xc0000fff, 0x00000104,
	mmCP_MEM_SLP_CNTL, 0x00000001, 0x00000001,
	0x3430, 0xfffffff0, 0x00000100,
	0x3630, 0xfffffff0, 0x00000100,
};

static const u32 verde_mgcg_cgcg_init[] =
{
	mmRLC_CGTT_MGCG_OVERRIDE, 0xffffffff, 0xfffffffc,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCB_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_BCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_GDS_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_IA_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PA_CLK_CTRL, 0xffffffff, 0x00000100,
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
	mmCGTT_TCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_TCP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_VGT_CLK_CTRL, 0xffffffff, 0x06000100,
	mmDB_CGTT_CLK_CTRL_0, 0xffffffff, 0x00000100,
	mmTA_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmTCA_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTCC_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTD_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	0x2458, 0xffffffff, 0x00010000,
	0x2459, 0xffffffff, 0x00030002,
	0x245a, 0xffffffff, 0x00040007,
	0x245b, 0xffffffff, 0x00060005,
	0x245c, 0xffffffff, 0x00090008,
	0x245d, 0xffffffff, 0x00020001,
	0x245e, 0xffffffff, 0x00040003,
	0x245f, 0xffffffff, 0x00000007,
	0x2460, 0xffffffff, 0x00060005,
	0x2461, 0xffffffff, 0x00090008,
	0x2462, 0xffffffff, 0x00030002,
	0x2463, 0xffffffff, 0x00050004,
	0x2464, 0xffffffff, 0x00000008,
	0x2465, 0xffffffff, 0x00070006,
	0x2466, 0xffffffff, 0x000a0009,
	0x2467, 0xffffffff, 0x00040003,
	0x2468, 0xffffffff, 0x00060005,
	0x2469, 0xffffffff, 0x00000009,
	0x246a, 0xffffffff, 0x00080007,
	0x246b, 0xffffffff, 0x000b000a,
	0x246c, 0xffffffff, 0x00050004,
	0x246d, 0xffffffff, 0x00070006,
	0x246e, 0xffffffff, 0x0008000b,
	0x246f, 0xffffffff, 0x000a0009,
	0x2470, 0xffffffff, 0x000d000c,
	0x2480, 0xffffffff, 0x00090008,
	0x2481, 0xffffffff, 0x000b000a,
	0x2482, 0xffffffff, 0x000c000f,
	0x2483, 0xffffffff, 0x000e000d,
	0x2484, 0xffffffff, 0x00110010,
	0x2485, 0xffffffff, 0x000a0009,
	0x2486, 0xffffffff, 0x000c000b,
	0x2487, 0xffffffff, 0x0000000f,
	0x2488, 0xffffffff, 0x000e000d,
	0x2489, 0xffffffff, 0x00110010,
	0x248a, 0xffffffff, 0x000b000a,
	0x248b, 0xffffffff, 0x000d000c,
	0x248c, 0xffffffff, 0x00000010,
	0x248d, 0xffffffff, 0x000f000e,
	0x248e, 0xffffffff, 0x00120011,
	0x248f, 0xffffffff, 0x000c000b,
	0x2490, 0xffffffff, 0x000e000d,
	0x2491, 0xffffffff, 0x00000011,
	0x2492, 0xffffffff, 0x0010000f,
	0x2493, 0xffffffff, 0x00130012,
	0x2494, 0xffffffff, 0x000d000c,
	0x2495, 0xffffffff, 0x000f000e,
	0x2496, 0xffffffff, 0x00100013,
	0x2497, 0xffffffff, 0x00120011,
	0x2498, 0xffffffff, 0x00150014,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96940200,
	mmCP_RB_WPTR_POLL_CNTL, 0xffffffff, 0x00900100,
	mmRLC_GCPM_GENERAL_3, 0xffffffff, 0x00000080,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003f,
	0x000c, 0xffffffff, 0x0000001c,
	0x000d, 0x000f0000, 0x000f0000,
	0x0583, 0xffffffff, 0x00000100,
	mmXDMA_CLOCK_GATING_CNTL, 0xffffffff, 0x00000100,
	mmXDMA_MEM_POWER_CNTL, 0x00000101, 0x00000000,
	mmMC_MEM_POWER_LS, 0xffffffff, 0x00000104,
	mmMC_CITF_MISC_WR_CG, 0x000c0000, 0x000c0000,
	mmMC_CITF_MISC_RD_CG, 0x000c0000, 0x000c0000,
	mmCGTT_DRM_CLK_CTRL0, 0xff000fff, 0x00000100,
	0x157a, 0x00000001, 0x00000001,
	mmHDP_MEM_POWER_LS, 0x00000001, 0x00000001,
	mmHDP_XDP_CGTT_BLK_CTRL, 0xc0000fff, 0x00000104,
	mmCP_MEM_SLP_CNTL, 0x00000001, 0x00000001,
	0x3430, 0xfffffff0, 0x00000100,
	0x3630, 0xfffffff0, 0x00000100,
};

static const u32 oland_mgcg_cgcg_init[] =
{
	mmRLC_CGTT_MGCG_OVERRIDE, 0xffffffff, 0xfffffffc,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCB_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_BCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_GDS_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_IA_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PA_CLK_CTRL, 0xffffffff, 0x00000100,
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
	mmCGTT_TCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_TCP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_VGT_CLK_CTRL, 0xffffffff, 0x06000100,
	mmDB_CGTT_CLK_CTRL_0, 0xffffffff, 0x00000100,
	mmTA_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmTCA_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTCC_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTD_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	0x2458, 0xffffffff, 0x00010000,
	0x2459, 0xffffffff, 0x00030002,
	0x245a, 0xffffffff, 0x00040007,
	0x245b, 0xffffffff, 0x00060005,
	0x245c, 0xffffffff, 0x00090008,
	0x245d, 0xffffffff, 0x00020001,
	0x245e, 0xffffffff, 0x00040003,
	0x245f, 0xffffffff, 0x00000007,
	0x2460, 0xffffffff, 0x00060005,
	0x2461, 0xffffffff, 0x00090008,
	0x2462, 0xffffffff, 0x00030002,
	0x2463, 0xffffffff, 0x00050004,
	0x2464, 0xffffffff, 0x00000008,
	0x2465, 0xffffffff, 0x00070006,
	0x2466, 0xffffffff, 0x000a0009,
	0x2467, 0xffffffff, 0x00040003,
	0x2468, 0xffffffff, 0x00060005,
	0x2469, 0xffffffff, 0x00000009,
	0x246a, 0xffffffff, 0x00080007,
	0x246b, 0xffffffff, 0x000b000a,
	0x246c, 0xffffffff, 0x00050004,
	0x246d, 0xffffffff, 0x00070006,
	0x246e, 0xffffffff, 0x0008000b,
	0x246f, 0xffffffff, 0x000a0009,
	0x2470, 0xffffffff, 0x000d000c,
	0x2471, 0xffffffff, 0x00060005,
	0x2472, 0xffffffff, 0x00080007,
	0x2473, 0xffffffff, 0x0000000b,
	0x2474, 0xffffffff, 0x000a0009,
	0x2475, 0xffffffff, 0x000d000c,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96940200,
	mmCP_RB_WPTR_POLL_CNTL, 0xffffffff, 0x00900100,
	mmRLC_GCPM_GENERAL_3, 0xffffffff, 0x00000080,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003f,
	0x000c, 0xffffffff, 0x0000001c,
	0x000d, 0x000f0000, 0x000f0000,
	0x0583, 0xffffffff, 0x00000100,
	mmXDMA_CLOCK_GATING_CNTL, 0xffffffff, 0x00000100,
	mmXDMA_MEM_POWER_CNTL, 0x00000101, 0x00000000,
	mmMC_MEM_POWER_LS, 0xffffffff, 0x00000104,
	mmMC_CITF_MISC_WR_CG, 0x000c0000, 0x000c0000,
	mmMC_CITF_MISC_RD_CG, 0x000c0000, 0x000c0000,
	mmCGTT_DRM_CLK_CTRL0, 0xff000fff, 0x00000100,
	0x157a, 0x00000001, 0x00000001,
	mmHDP_MEM_POWER_LS, 0x00000001, 0x00000001,
	mmHDP_XDP_CGTT_BLK_CTRL, 0xc0000fff, 0x00000104,
	mmCP_MEM_SLP_CNTL, 0x00000001, 0x00000001,
	0x3430, 0xfffffff0, 0x00000100,
	0x3630, 0xfffffff0, 0x00000100,
};

static const u32 hainan_mgcg_cgcg_init[] =
{
	mmRLC_CGTT_MGCG_OVERRIDE, 0xffffffff, 0xfffffffc,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	mmCB_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_BCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_CP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_GDS_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_IA_CLK_CTRL, 0xffffffff, 0x06000100,
	mmCGTT_PA_CLK_CTRL, 0xffffffff, 0x00000100,
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
	mmCGTT_TCI_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_TCP_CLK_CTRL, 0xffffffff, 0x00000100,
	mmCGTT_VGT_CLK_CTRL, 0xffffffff, 0x06000100,
	mmDB_CGTT_CLK_CTRL_0, 0xffffffff, 0x00000100,
	mmTA_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmTCA_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTCC_CGTT_SCLK_CTRL, 0xffffffff, 0x00000100,
	mmTD_CGTT_CTRL, 0xffffffff, 0x00000100,
	mmGRBM_GFX_INDEX, 0xffffffff, 0xe0000000,
	0x2458, 0xffffffff, 0x00010000,
	0x2459, 0xffffffff, 0x00030002,
	0x245a, 0xffffffff, 0x00040007,
	0x245b, 0xffffffff, 0x00060005,
	0x245c, 0xffffffff, 0x00090008,
	0x245d, 0xffffffff, 0x00020001,
	0x245e, 0xffffffff, 0x00040003,
	0x245f, 0xffffffff, 0x00000007,
	0x2460, 0xffffffff, 0x00060005,
	0x2461, 0xffffffff, 0x00090008,
	0x2462, 0xffffffff, 0x00030002,
	0x2463, 0xffffffff, 0x00050004,
	0x2464, 0xffffffff, 0x00000008,
	0x2465, 0xffffffff, 0x00070006,
	0x2466, 0xffffffff, 0x000a0009,
	0x2467, 0xffffffff, 0x00040003,
	0x2468, 0xffffffff, 0x00060005,
	0x2469, 0xffffffff, 0x00000009,
	0x246a, 0xffffffff, 0x00080007,
	0x246b, 0xffffffff, 0x000b000a,
	0x246c, 0xffffffff, 0x00050004,
	0x246d, 0xffffffff, 0x00070006,
	0x246e, 0xffffffff, 0x0008000b,
	0x246f, 0xffffffff, 0x000a0009,
	0x2470, 0xffffffff, 0x000d000c,
	0x2471, 0xffffffff, 0x00060005,
	0x2472, 0xffffffff, 0x00080007,
	0x2473, 0xffffffff, 0x0000000b,
	0x2474, 0xffffffff, 0x000a0009,
	0x2475, 0xffffffff, 0x000d000c,
	mmCGTS_SM_CTRL_REG, 0xffffffff, 0x96940200,
	mmCP_RB_WPTR_POLL_CNTL, 0xffffffff, 0x00900100,
	mmRLC_GCPM_GENERAL_3, 0xffffffff, 0x00000080,
	mmRLC_CGCG_CGLS_CTRL, 0xffffffff, 0x0020003f,
	0x000c, 0xffffffff, 0x0000001c,
	0x000d, 0x000f0000, 0x000f0000,
	0x0583, 0xffffffff, 0x00000100,
	0x0409, 0xffffffff, 0x00000100,
	mmMC_MEM_POWER_LS, 0xffffffff, 0x00000104,
	mmMC_CITF_MISC_WR_CG, 0x000c0000, 0x000c0000,
	mmMC_CITF_MISC_RD_CG, 0x000c0000, 0x000c0000,
	mmHDP_MEM_POWER_LS, 0x00000001, 0x00000001,
	mmHDP_XDP_CGTT_BLK_CTRL, 0xc0000fff, 0x00000104,
	mmCP_MEM_SLP_CNTL, 0x00000001, 0x00000001,
	0x3430, 0xfffffff0, 0x00000100,
	0x3630, 0xfffffff0, 0x00000100,
};

static u32 si_pcie_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(AMDGPU_PCIE_INDEX, reg);
	(void)RREG32(AMDGPU_PCIE_INDEX);
	r = RREG32(AMDGPU_PCIE_DATA);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
	return r;
}

static void si_pcie_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(AMDGPU_PCIE_INDEX, reg);
	(void)RREG32(AMDGPU_PCIE_INDEX);
	WREG32(AMDGPU_PCIE_DATA, v);
	(void)RREG32(AMDGPU_PCIE_DATA);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

static u32 si_pciep_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(PCIE_PORT_INDEX, ((reg) & 0xff));
	(void)RREG32(PCIE_PORT_INDEX);
	r = RREG32(PCIE_PORT_DATA);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
	return r;
}

static void si_pciep_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(PCIE_PORT_INDEX, ((reg) & 0xff));
	(void)RREG32(PCIE_PORT_INDEX);
	WREG32(PCIE_PORT_DATA, (v));
	(void)RREG32(PCIE_PORT_DATA);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

static u32 si_smc_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	WREG32(SMC_IND_INDEX_0, (reg));
	r = RREG32(SMC_IND_DATA_0);
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);
	return r;
}

static void si_smc_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	WREG32(SMC_IND_INDEX_0, (reg));
	WREG32(SMC_IND_DATA_0, (v));
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);
}

static struct amdgpu_allowed_register_entry si_allowed_read_registers[] = {
	{GRBM_STATUS},
	{GB_ADDR_CONFIG},
	{MC_ARB_RAMCFG},
	{GB_TILE_MODE0},
	{GB_TILE_MODE1},
	{GB_TILE_MODE2},
	{GB_TILE_MODE3},
	{GB_TILE_MODE4},
	{GB_TILE_MODE5},
	{GB_TILE_MODE6},
	{GB_TILE_MODE7},
	{GB_TILE_MODE8},
	{GB_TILE_MODE9},
	{GB_TILE_MODE10},
	{GB_TILE_MODE11},
	{GB_TILE_MODE12},
	{GB_TILE_MODE13},
	{GB_TILE_MODE14},
	{GB_TILE_MODE15},
	{GB_TILE_MODE16},
	{GB_TILE_MODE17},
	{GB_TILE_MODE18},
	{GB_TILE_MODE19},
	{GB_TILE_MODE20},
	{GB_TILE_MODE21},
	{GB_TILE_MODE22},
	{GB_TILE_MODE23},
	{GB_TILE_MODE24},
	{GB_TILE_MODE25},
	{GB_TILE_MODE26},
	{GB_TILE_MODE27},
	{GB_TILE_MODE28},
	{GB_TILE_MODE29},
	{GB_TILE_MODE30},
	{GB_TILE_MODE31},
	{CC_RB_BACKEND_DISABLE, true},
	{GC_USER_RB_BACKEND_DISABLE, true},
	{PA_SC_RASTER_CONFIG, true},
};

static uint32_t si_get_register_value(struct amdgpu_device *adev,
				      bool indexed, u32 se_num,
				      u32 sh_num, u32 reg_offset)
{
	if (indexed) {
		uint32_t val;
		unsigned se_idx = (se_num == 0xffffffff) ? 0 : se_num;
		unsigned sh_idx = (sh_num == 0xffffffff) ? 0 : sh_num;

		switch (reg_offset) {
		case mmCC_RB_BACKEND_DISABLE:
			return adev->gfx.config.rb_config[se_idx][sh_idx].rb_backend_disable;
		case mmGC_USER_RB_BACKEND_DISABLE:
			return adev->gfx.config.rb_config[se_idx][sh_idx].user_rb_backend_disable;
		case mmPA_SC_RASTER_CONFIG:
			return adev->gfx.config.rb_config[se_idx][sh_idx].raster_config;
		}

		mutex_lock(&adev->grbm_idx_mutex);
		if (se_num != 0xffffffff || sh_num != 0xffffffff)
			amdgpu_gfx_select_se_sh(adev, se_num, sh_num, 0xffffffff);

		val = RREG32(reg_offset);

		if (se_num != 0xffffffff || sh_num != 0xffffffff)
			amdgpu_gfx_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
		mutex_unlock(&adev->grbm_idx_mutex);
		return val;
	} else {
		unsigned idx;

		switch (reg_offset) {
		case mmGB_ADDR_CONFIG:
			return adev->gfx.config.gb_addr_config;
		case mmMC_ARB_RAMCFG:
			return adev->gfx.config.mc_arb_ramcfg;
		case mmGB_TILE_MODE0:
		case mmGB_TILE_MODE1:
		case mmGB_TILE_MODE2:
		case mmGB_TILE_MODE3:
		case mmGB_TILE_MODE4:
		case mmGB_TILE_MODE5:
		case mmGB_TILE_MODE6:
		case mmGB_TILE_MODE7:
		case mmGB_TILE_MODE8:
		case mmGB_TILE_MODE9:
		case mmGB_TILE_MODE10:
		case mmGB_TILE_MODE11:
		case mmGB_TILE_MODE12:
		case mmGB_TILE_MODE13:
		case mmGB_TILE_MODE14:
		case mmGB_TILE_MODE15:
		case mmGB_TILE_MODE16:
		case mmGB_TILE_MODE17:
		case mmGB_TILE_MODE18:
		case mmGB_TILE_MODE19:
		case mmGB_TILE_MODE20:
		case mmGB_TILE_MODE21:
		case mmGB_TILE_MODE22:
		case mmGB_TILE_MODE23:
		case mmGB_TILE_MODE24:
		case mmGB_TILE_MODE25:
		case mmGB_TILE_MODE26:
		case mmGB_TILE_MODE27:
		case mmGB_TILE_MODE28:
		case mmGB_TILE_MODE29:
		case mmGB_TILE_MODE30:
		case mmGB_TILE_MODE31:
			idx = (reg_offset - mmGB_TILE_MODE0);
			return adev->gfx.config.tile_mode_array[idx];
		default:
			return RREG32(reg_offset);
		}
	}
}
static int si_read_register(struct amdgpu_device *adev, u32 se_num,
			     u32 sh_num, u32 reg_offset, u32 *value)
{
	uint32_t i;

	*value = 0;
	for (i = 0; i < ARRAY_SIZE(si_allowed_read_registers); i++) {
		bool indexed = si_allowed_read_registers[i].grbm_indexed;

		if (reg_offset != si_allowed_read_registers[i].reg_offset)
			continue;

		*value = si_get_register_value(adev, indexed, se_num, sh_num,
					       reg_offset);
		return 0;
	}
	return -EINVAL;
}

static bool si_read_disabled_bios(struct amdgpu_device *adev)
{
	u32 bus_cntl;
	u32 d1vga_control = 0;
	u32 d2vga_control = 0;
	u32 vga_render_control = 0;
	u32 rom_cntl;
	bool r;

	bus_cntl = RREG32(R600_BUS_CNTL);
	if (adev->mode_info.num_crtc) {
		d1vga_control = RREG32(AVIVO_D1VGA_CONTROL);
		d2vga_control = RREG32(AVIVO_D2VGA_CONTROL);
		vga_render_control = RREG32(VGA_RENDER_CONTROL);
	}
	rom_cntl = RREG32(R600_ROM_CNTL);

	/* enable the rom */
	WREG32(R600_BUS_CNTL, (bus_cntl & ~R600_BIOS_ROM_DIS));
	if (adev->mode_info.num_crtc) {
		/* Disable VGA mode */
		WREG32(AVIVO_D1VGA_CONTROL,
		       (d1vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
					  AVIVO_DVGA_CONTROL_TIMING_SELECT)));
		WREG32(AVIVO_D2VGA_CONTROL,
		       (d2vga_control & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE |
					  AVIVO_DVGA_CONTROL_TIMING_SELECT)));
		WREG32(VGA_RENDER_CONTROL,
		       (vga_render_control & C_000300_VGA_VSTATUS_CNTL));
	}
	WREG32(R600_ROM_CNTL, rom_cntl | R600_SCK_OVERWRITE);

	r = amdgpu_read_bios(adev);

	/* restore regs */
	WREG32(R600_BUS_CNTL, bus_cntl);
	if (adev->mode_info.num_crtc) {
		WREG32(AVIVO_D1VGA_CONTROL, d1vga_control);
		WREG32(AVIVO_D2VGA_CONTROL, d2vga_control);
		WREG32(VGA_RENDER_CONTROL, vga_render_control);
	}
	WREG32(R600_ROM_CNTL, rom_cntl);
	return r;
}

#define mmROM_INDEX 0x2A
#define mmROM_DATA  0x2B

static bool si_read_bios_from_rom(struct amdgpu_device *adev,
				  u8 *bios, u32 length_bytes)
{
	u32 *dw_ptr;
	u32 i, length_dw;

	if (bios == NULL)
		return false;
	if (length_bytes == 0)
		return false;
	/* APU vbios image is part of sbios image */
	if (adev->flags & AMD_IS_APU)
		return false;

	dw_ptr = (u32 *)bios;
	length_dw = ALIGN(length_bytes, 4) / 4;
	/* set rom index to 0 */
	WREG32(mmROM_INDEX, 0);
	for (i = 0; i < length_dw; i++)
		dw_ptr[i] = RREG32(mmROM_DATA);

	return true;
}

//xxx: not implemented
static int si_asic_reset(struct amdgpu_device *adev)
{
	return 0;
}

static u32 si_get_config_memsize(struct amdgpu_device *adev)
{
	return RREG32(mmCONFIG_MEMSIZE);
}

static void si_vga_set_state(struct amdgpu_device *adev, bool state)
{
	uint32_t temp;

	temp = RREG32(CONFIG_CNTL);
	if (state == false) {
		temp &= ~(1<<0);
		temp |= (1<<1);
	} else {
		temp &= ~(1<<1);
	}
	WREG32(CONFIG_CNTL, temp);
}

static u32 si_get_xclk(struct amdgpu_device *adev)
{
        u32 reference_clock = adev->clock.spll.reference_freq;
	u32 tmp;

	tmp = RREG32(CG_CLKPIN_CNTL_2);
	if (tmp & MUX_TCLK_TO_XCLK)
		return TCLK;

	tmp = RREG32(CG_CLKPIN_CNTL);
	if (tmp & XTALIN_DIVIDE)
		return reference_clock / 4;

	return reference_clock;
}

//xxx:not implemented
static int si_set_uvd_clocks(struct amdgpu_device *adev, u32 vclk, u32 dclk)
{
	return 0;
}

static void si_detect_hw_virtualization(struct amdgpu_device *adev)
{
	if (is_virtual_machine()) /* passthrough mode */
		adev->virt.caps |= AMDGPU_PASSTHROUGH_MODE;
}

static void si_flush_hdp(struct amdgpu_device *adev, struct amdgpu_ring *ring)
{
	if (!ring || !ring->funcs->emit_wreg) {
		WREG32(mmHDP_MEM_COHERENCY_FLUSH_CNTL, 1);
		RREG32(mmHDP_MEM_COHERENCY_FLUSH_CNTL);
	} else {
		amdgpu_ring_emit_wreg(ring, mmHDP_MEM_COHERENCY_FLUSH_CNTL, 1);
	}
}

static void si_invalidate_hdp(struct amdgpu_device *adev,
			      struct amdgpu_ring *ring)
{
	if (!ring || !ring->funcs->emit_wreg) {
		WREG32(mmHDP_DEBUG0, 1);
		RREG32(mmHDP_DEBUG0);
	} else {
		amdgpu_ring_emit_wreg(ring, mmHDP_DEBUG0, 1);
	}
}

static bool si_need_full_reset(struct amdgpu_device *adev)
{
	/* change this when we support soft reset */
	return true;
}

static int si_get_pcie_lanes(struct amdgpu_device *adev)
{
	u32 link_width_cntl;

	if (adev->flags & AMD_IS_APU)
		return 0;

	link_width_cntl = RREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL);

	switch ((link_width_cntl & LC_LINK_WIDTH_RD_MASK) >> LC_LINK_WIDTH_RD_SHIFT) {
	case LC_LINK_WIDTH_X1:
		return 1;
	case LC_LINK_WIDTH_X2:
		return 2;
	case LC_LINK_WIDTH_X4:
		return 4;
	case LC_LINK_WIDTH_X8:
		return 8;
	case LC_LINK_WIDTH_X0:
	case LC_LINK_WIDTH_X16:
	default:
		return 16;
	}
}

static void si_set_pcie_lanes(struct amdgpu_device *adev, int lanes)
{
	u32 link_width_cntl, mask;

	if (adev->flags & AMD_IS_APU)
		return;

	switch (lanes) {
	case 0:
		mask = LC_LINK_WIDTH_X0;
		break;
	case 1:
		mask = LC_LINK_WIDTH_X1;
		break;
	case 2:
		mask = LC_LINK_WIDTH_X2;
		break;
	case 4:
		mask = LC_LINK_WIDTH_X4;
		break;
	case 8:
		mask = LC_LINK_WIDTH_X8;
		break;
	case 16:
		mask = LC_LINK_WIDTH_X16;
		break;
	default:
		DRM_ERROR("invalid pcie lane request: %d\n", lanes);
		return;
	}

	link_width_cntl = RREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL);
	link_width_cntl &= ~LC_LINK_WIDTH_MASK;
	link_width_cntl |= mask << LC_LINK_WIDTH_SHIFT;
	link_width_cntl |= (LC_RECONFIG_NOW |
			    LC_RECONFIG_ARC_MISSING_ESCAPE);

	WREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL, link_width_cntl);
}

static const struct amdgpu_asic_funcs si_asic_funcs =
{
	.read_disabled_bios = &si_read_disabled_bios,
	.read_bios_from_rom = &si_read_bios_from_rom,
	.read_register = &si_read_register,
	.reset = &si_asic_reset,
	.set_vga_state = &si_vga_set_state,
	.get_xclk = &si_get_xclk,
	.set_uvd_clocks = &si_set_uvd_clocks,
	.set_vce_clocks = NULL,
	.get_pcie_lanes = &si_get_pcie_lanes,
	.set_pcie_lanes = &si_set_pcie_lanes,
	.get_config_memsize = &si_get_config_memsize,
	.flush_hdp = &si_flush_hdp,
	.invalidate_hdp = &si_invalidate_hdp,
	.need_full_reset = &si_need_full_reset,
};

static uint32_t si_get_rev_id(struct amdgpu_device *adev)
{
	return (RREG32(CC_DRM_ID_STRAPS) & CC_DRM_ID_STRAPS__ATI_REV_ID_MASK)
		>> CC_DRM_ID_STRAPS__ATI_REV_ID__SHIFT;
}

static int si_common_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->smc_rreg = &si_smc_rreg;
	adev->smc_wreg = &si_smc_wreg;
	adev->pcie_rreg = &si_pcie_rreg;
	adev->pcie_wreg = &si_pcie_wreg;
	adev->pciep_rreg = &si_pciep_rreg;
	adev->pciep_wreg = &si_pciep_wreg;
	adev->uvd_ctx_rreg = NULL;
	adev->uvd_ctx_wreg = NULL;
	adev->didt_rreg = NULL;
	adev->didt_wreg = NULL;

	adev->asic_funcs = &si_asic_funcs;

	adev->rev_id = si_get_rev_id(adev);
	adev->external_rev_id = 0xFF;
	switch (adev->asic_type) {
	case CHIP_TAHITI:
		adev->cg_flags =
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			/*AMD_CG_SUPPORT_GFX_CGCG |*/
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_CGTS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_VCE_MGCG |
			AMD_CG_SUPPORT_UVD_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_HDP_MGCG;
			adev->pg_flags = 0;
		adev->external_rev_id = (adev->rev_id == 0) ? 1 :
					(adev->rev_id == 1) ? 5 : 6;
		break;
	case CHIP_PITCAIRN:
		adev->cg_flags =
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			/*AMD_CG_SUPPORT_GFX_CGCG |*/
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_CGTS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_VCE_MGCG |
			AMD_CG_SUPPORT_UVD_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_HDP_MGCG;
		adev->pg_flags = 0;
		adev->external_rev_id = adev->rev_id + 20;
		break;

	case CHIP_VERDE:
		adev->cg_flags =
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_CGTS |
			AMD_CG_SUPPORT_GFX_CGTS_LS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_SDMA_LS |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_VCE_MGCG |
			AMD_CG_SUPPORT_UVD_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_HDP_MGCG;
		adev->pg_flags = 0;
		//???
		adev->external_rev_id = adev->rev_id + 40;
		break;
	case CHIP_OLAND:
		adev->cg_flags =
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			/*AMD_CG_SUPPORT_GFX_CGCG |*/
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_CGTS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_UVD_MGCG |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_HDP_MGCG;
		adev->pg_flags = 0;
		adev->external_rev_id = 60;
		break;
	case CHIP_HAINAN:
		adev->cg_flags =
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_MGLS |
			/*AMD_CG_SUPPORT_GFX_CGCG |*/
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_CGTS |
			AMD_CG_SUPPORT_GFX_CP_LS |
			AMD_CG_SUPPORT_GFX_RLC_LS |
			AMD_CG_SUPPORT_MC_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_SDMA_MGCG |
			AMD_CG_SUPPORT_BIF_LS |
			AMD_CG_SUPPORT_HDP_LS |
			AMD_CG_SUPPORT_HDP_MGCG;
		adev->pg_flags = 0;
		adev->external_rev_id = 70;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int si_common_sw_init(void *handle)
{
	return 0;
}

static int si_common_sw_fini(void *handle)
{
	return 0;
}


static void si_init_golden_registers(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_TAHITI:
		amdgpu_device_program_register_sequence(adev,
							tahiti_golden_registers,
							ARRAY_SIZE(tahiti_golden_registers));
		amdgpu_device_program_register_sequence(adev,
							tahiti_golden_rlc_registers,
							ARRAY_SIZE(tahiti_golden_rlc_registers));
		amdgpu_device_program_register_sequence(adev,
							tahiti_mgcg_cgcg_init,
							ARRAY_SIZE(tahiti_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							tahiti_golden_registers2,
							ARRAY_SIZE(tahiti_golden_registers2));
		break;
	case CHIP_PITCAIRN:
		amdgpu_device_program_register_sequence(adev,
							pitcairn_golden_registers,
							ARRAY_SIZE(pitcairn_golden_registers));
		amdgpu_device_program_register_sequence(adev,
							pitcairn_golden_rlc_registers,
							ARRAY_SIZE(pitcairn_golden_rlc_registers));
		amdgpu_device_program_register_sequence(adev,
							pitcairn_mgcg_cgcg_init,
							ARRAY_SIZE(pitcairn_mgcg_cgcg_init));
		break;
	case CHIP_VERDE:
		amdgpu_device_program_register_sequence(adev,
							verde_golden_registers,
							ARRAY_SIZE(verde_golden_registers));
		amdgpu_device_program_register_sequence(adev,
							verde_golden_rlc_registers,
							ARRAY_SIZE(verde_golden_rlc_registers));
		amdgpu_device_program_register_sequence(adev,
							verde_mgcg_cgcg_init,
							ARRAY_SIZE(verde_mgcg_cgcg_init));
		amdgpu_device_program_register_sequence(adev,
							verde_pg_init,
							ARRAY_SIZE(verde_pg_init));
		break;
	case CHIP_OLAND:
		amdgpu_device_program_register_sequence(adev,
							oland_golden_registers,
							ARRAY_SIZE(oland_golden_registers));
		amdgpu_device_program_register_sequence(adev,
							oland_golden_rlc_registers,
							ARRAY_SIZE(oland_golden_rlc_registers));
		amdgpu_device_program_register_sequence(adev,
							oland_mgcg_cgcg_init,
							ARRAY_SIZE(oland_mgcg_cgcg_init));
		break;
	case CHIP_HAINAN:
		amdgpu_device_program_register_sequence(adev,
							hainan_golden_registers,
							ARRAY_SIZE(hainan_golden_registers));
		amdgpu_device_program_register_sequence(adev,
							hainan_golden_registers2,
							ARRAY_SIZE(hainan_golden_registers2));
		amdgpu_device_program_register_sequence(adev,
							hainan_mgcg_cgcg_init,
							ARRAY_SIZE(hainan_mgcg_cgcg_init));
		break;


	default:
		BUG();
	}
}

static void si_pcie_gen3_enable(struct amdgpu_device *adev)
{
	struct pci_dev *root = adev->pdev->bus->self;
	int bridge_pos, gpu_pos;
	u32 speed_cntl, current_data_rate;
	int i;
	u16 tmp16;

	if (pci_is_root_bus(adev->pdev->bus))
		return;

	if (amdgpu_pcie_gen2 == 0)
		return;

	if (adev->flags & AMD_IS_APU)
		return;

	if (!(adev->pm.pcie_gen_mask & (CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2 |
					CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3)))
		return;

	speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	current_data_rate = (speed_cntl & LC_CURRENT_DATA_RATE_MASK) >>
		LC_CURRENT_DATA_RATE_SHIFT;
	if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3) {
		if (current_data_rate == 2) {
			DRM_INFO("PCIE gen 3 link speeds already enabled\n");
			return;
		}
		DRM_INFO("enabling PCIE gen 3 link speeds, disable with amdgpu.pcie_gen2=0\n");
	} else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2) {
		if (current_data_rate == 1) {
			DRM_INFO("PCIE gen 2 link speeds already enabled\n");
			return;
		}
		DRM_INFO("enabling PCIE gen 2 link speeds, disable with amdgpu.pcie_gen2=0\n");
	}

	bridge_pos = pci_pcie_cap(root);
	if (!bridge_pos)
		return;

	gpu_pos = pci_pcie_cap(adev->pdev);
	if (!gpu_pos)
		return;

	if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3) {
		if (current_data_rate != 2) {
			u16 bridge_cfg, gpu_cfg;
			u16 bridge_cfg2, gpu_cfg2;
			u32 max_lw, current_lw, tmp;

			pci_read_config_word(root, bridge_pos + PCI_EXP_LNKCTL, &bridge_cfg);
			pci_read_config_word(adev->pdev, gpu_pos + PCI_EXP_LNKCTL, &gpu_cfg);

			tmp16 = bridge_cfg | PCI_EXP_LNKCTL_HAWD;
			pci_write_config_word(root, bridge_pos + PCI_EXP_LNKCTL, tmp16);

			tmp16 = gpu_cfg | PCI_EXP_LNKCTL_HAWD;
			pci_write_config_word(adev->pdev, gpu_pos + PCI_EXP_LNKCTL, tmp16);

			tmp = RREG32_PCIE(PCIE_LC_STATUS1);
			max_lw = (tmp & LC_DETECTED_LINK_WIDTH_MASK) >> LC_DETECTED_LINK_WIDTH_SHIFT;
			current_lw = (tmp & LC_OPERATING_LINK_WIDTH_MASK) >> LC_OPERATING_LINK_WIDTH_SHIFT;

			if (current_lw < max_lw) {
				tmp = RREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL);
				if (tmp & LC_RENEGOTIATION_SUPPORT) {
					tmp &= ~(LC_LINK_WIDTH_MASK | LC_UPCONFIGURE_DIS);
					tmp |= (max_lw << LC_LINK_WIDTH_SHIFT);
					tmp |= LC_UPCONFIGURE_SUPPORT | LC_RENEGOTIATE_EN | LC_RECONFIG_NOW;
					WREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL, tmp);
				}
			}

			for (i = 0; i < 10; i++) {
				pci_read_config_word(adev->pdev, gpu_pos + PCI_EXP_DEVSTA, &tmp16);
				if (tmp16 & PCI_EXP_DEVSTA_TRPND)
					break;

				pci_read_config_word(root, bridge_pos + PCI_EXP_LNKCTL, &bridge_cfg);
				pci_read_config_word(adev->pdev, gpu_pos + PCI_EXP_LNKCTL, &gpu_cfg);

				pci_read_config_word(root, bridge_pos + PCI_EXP_LNKCTL2, &bridge_cfg2);
				pci_read_config_word(adev->pdev, gpu_pos + PCI_EXP_LNKCTL2, &gpu_cfg2);

				tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL4);
				tmp |= LC_SET_QUIESCE;
				WREG32_PCIE_PORT(PCIE_LC_CNTL4, tmp);

				tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL4);
				tmp |= LC_REDO_EQ;
				WREG32_PCIE_PORT(PCIE_LC_CNTL4, tmp);

				mdelay(100);

				pci_read_config_word(root, bridge_pos + PCI_EXP_LNKCTL, &tmp16);
				tmp16 &= ~PCI_EXP_LNKCTL_HAWD;
				tmp16 |= (bridge_cfg & PCI_EXP_LNKCTL_HAWD);
				pci_write_config_word(root, bridge_pos + PCI_EXP_LNKCTL, tmp16);

				pci_read_config_word(adev->pdev, gpu_pos + PCI_EXP_LNKCTL, &tmp16);
				tmp16 &= ~PCI_EXP_LNKCTL_HAWD;
				tmp16 |= (gpu_cfg & PCI_EXP_LNKCTL_HAWD);
				pci_write_config_word(adev->pdev, gpu_pos + PCI_EXP_LNKCTL, tmp16);

				pci_read_config_word(root, bridge_pos + PCI_EXP_LNKCTL2, &tmp16);
				tmp16 &= ~((1 << 4) | (7 << 9));
				tmp16 |= (bridge_cfg2 & ((1 << 4) | (7 << 9)));
				pci_write_config_word(root, bridge_pos + PCI_EXP_LNKCTL2, tmp16);

				pci_read_config_word(adev->pdev, gpu_pos + PCI_EXP_LNKCTL2, &tmp16);
				tmp16 &= ~((1 << 4) | (7 << 9));
				tmp16 |= (gpu_cfg2 & ((1 << 4) | (7 << 9)));
				pci_write_config_word(adev->pdev, gpu_pos + PCI_EXP_LNKCTL2, tmp16);

				tmp = RREG32_PCIE_PORT(PCIE_LC_CNTL4);
				tmp &= ~LC_SET_QUIESCE;
				WREG32_PCIE_PORT(PCIE_LC_CNTL4, tmp);
			}
		}
	}

	speed_cntl |= LC_FORCE_EN_SW_SPEED_CHANGE | LC_FORCE_DIS_HW_SPEED_CHANGE;
	speed_cntl &= ~LC_FORCE_DIS_SW_SPEED_CHANGE;
	WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, speed_cntl);

	pci_read_config_word(adev->pdev, gpu_pos + PCI_EXP_LNKCTL2, &tmp16);
	tmp16 &= ~0xf;
	if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3)
		tmp16 |= 3;
	else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2)
		tmp16 |= 2;
	else
		tmp16 |= 1;
	pci_write_config_word(adev->pdev, gpu_pos + PCI_EXP_LNKCTL2, tmp16);

	speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
	speed_cntl |= LC_INITIATE_LINK_SPEED_CHANGE;
	WREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL, speed_cntl);

	for (i = 0; i < adev->usec_timeout; i++) {
		speed_cntl = RREG32_PCIE_PORT(PCIE_LC_SPEED_CNTL);
		if ((speed_cntl & LC_INITIATE_LINK_SPEED_CHANGE) == 0)
			break;
		udelay(1);
	}
}

static inline u32 si_pif_phy0_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(EVERGREEN_PIF_PHY0_INDEX, ((reg) & 0xffff));
	r = RREG32(EVERGREEN_PIF_PHY0_DATA);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
	return r;
}

static inline void si_pif_phy0_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(EVERGREEN_PIF_PHY0_INDEX, ((reg) & 0xffff));
	WREG32(EVERGREEN_PIF_PHY0_DATA, (v));
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

static inline u32 si_pif_phy1_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags;
	u32 r;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(EVERGREEN_PIF_PHY1_INDEX, ((reg) & 0xffff));
	r = RREG32(EVERGREEN_PIF_PHY1_DATA);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
	return r;
}

static inline void si_pif_phy1_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(EVERGREEN_PIF_PHY1_INDEX, ((reg) & 0xffff));
	WREG32(EVERGREEN_PIF_PHY1_DATA, (v));
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}
static void si_program_aspm(struct amdgpu_device *adev)
{
	u32 data, orig;
	bool disable_l0s = false, disable_l1 = false, disable_plloff_in_l1 = false;
	bool disable_clkreq = false;

	if (amdgpu_aspm == 0)
		return;

	if (adev->flags & AMD_IS_APU)
		return;
	orig = data = RREG32_PCIE_PORT(PCIE_LC_N_FTS_CNTL);
	data &= ~LC_XMIT_N_FTS_MASK;
	data |= LC_XMIT_N_FTS(0x24) | LC_XMIT_N_FTS_OVERRIDE_EN;
	if (orig != data)
		WREG32_PCIE_PORT(PCIE_LC_N_FTS_CNTL, data);

	orig = data = RREG32_PCIE_PORT(PCIE_LC_CNTL3);
	data |= LC_GO_TO_RECOVERY;
	if (orig != data)
		WREG32_PCIE_PORT(PCIE_LC_CNTL3, data);

	orig = data = RREG32_PCIE(PCIE_P_CNTL);
	data |= P_IGNORE_EDB_ERR;
	if (orig != data)
		WREG32_PCIE(PCIE_P_CNTL, data);

	orig = data = RREG32_PCIE_PORT(PCIE_LC_CNTL);
	data &= ~(LC_L0S_INACTIVITY_MASK | LC_L1_INACTIVITY_MASK);
	data |= LC_PMI_TO_L1_DIS;
	if (!disable_l0s)
		data |= LC_L0S_INACTIVITY(7);

	if (!disable_l1) {
		data |= LC_L1_INACTIVITY(7);
		data &= ~LC_PMI_TO_L1_DIS;
		if (orig != data)
			WREG32_PCIE_PORT(PCIE_LC_CNTL, data);

		if (!disable_plloff_in_l1) {
			bool clk_req_support;

			orig = data = si_pif_phy0_rreg(adev,PB0_PIF_PWRDOWN_0);
			data &= ~(PLL_POWER_STATE_IN_OFF_0_MASK | PLL_POWER_STATE_IN_TXS2_0_MASK);
			data |= PLL_POWER_STATE_IN_OFF_0(7) | PLL_POWER_STATE_IN_TXS2_0(7);
			if (orig != data)
				si_pif_phy0_wreg(adev,PB0_PIF_PWRDOWN_0, data);

			orig = data = si_pif_phy0_rreg(adev,PB0_PIF_PWRDOWN_1);
			data &= ~(PLL_POWER_STATE_IN_OFF_1_MASK | PLL_POWER_STATE_IN_TXS2_1_MASK);
			data |= PLL_POWER_STATE_IN_OFF_1(7) | PLL_POWER_STATE_IN_TXS2_1(7);
			if (orig != data)
				si_pif_phy0_wreg(adev,PB0_PIF_PWRDOWN_1, data);

			orig = data = si_pif_phy1_rreg(adev,PB1_PIF_PWRDOWN_0);
			data &= ~(PLL_POWER_STATE_IN_OFF_0_MASK | PLL_POWER_STATE_IN_TXS2_0_MASK);
			data |= PLL_POWER_STATE_IN_OFF_0(7) | PLL_POWER_STATE_IN_TXS2_0(7);
			if (orig != data)
				si_pif_phy1_wreg(adev,PB1_PIF_PWRDOWN_0, data);

			orig = data = si_pif_phy1_rreg(adev,PB1_PIF_PWRDOWN_1);
			data &= ~(PLL_POWER_STATE_IN_OFF_1_MASK | PLL_POWER_STATE_IN_TXS2_1_MASK);
			data |= PLL_POWER_STATE_IN_OFF_1(7) | PLL_POWER_STATE_IN_TXS2_1(7);
			if (orig != data)
				si_pif_phy1_wreg(adev,PB1_PIF_PWRDOWN_1, data);

			if ((adev->asic_type != CHIP_OLAND) && (adev->asic_type != CHIP_HAINAN)) {
				orig = data = si_pif_phy0_rreg(adev,PB0_PIF_PWRDOWN_0);
				data &= ~PLL_RAMP_UP_TIME_0_MASK;
				if (orig != data)
					si_pif_phy0_wreg(adev,PB0_PIF_PWRDOWN_0, data);

				orig = data = si_pif_phy0_rreg(adev,PB0_PIF_PWRDOWN_1);
				data &= ~PLL_RAMP_UP_TIME_1_MASK;
				if (orig != data)
					si_pif_phy0_wreg(adev,PB0_PIF_PWRDOWN_1, data);

				orig = data = si_pif_phy0_rreg(adev,PB0_PIF_PWRDOWN_2);
				data &= ~PLL_RAMP_UP_TIME_2_MASK;
				if (orig != data)
					si_pif_phy0_wreg(adev,PB0_PIF_PWRDOWN_2, data);

				orig = data = si_pif_phy0_rreg(adev,PB0_PIF_PWRDOWN_3);
				data &= ~PLL_RAMP_UP_TIME_3_MASK;
				if (orig != data)
					si_pif_phy0_wreg(adev,PB0_PIF_PWRDOWN_3, data);

				orig = data = si_pif_phy1_rreg(adev,PB1_PIF_PWRDOWN_0);
				data &= ~PLL_RAMP_UP_TIME_0_MASK;
				if (orig != data)
					si_pif_phy1_wreg(adev,PB1_PIF_PWRDOWN_0, data);

				orig = data = si_pif_phy1_rreg(adev,PB1_PIF_PWRDOWN_1);
				data &= ~PLL_RAMP_UP_TIME_1_MASK;
				if (orig != data)
					si_pif_phy1_wreg(adev,PB1_PIF_PWRDOWN_1, data);

				orig = data = si_pif_phy1_rreg(adev,PB1_PIF_PWRDOWN_2);
				data &= ~PLL_RAMP_UP_TIME_2_MASK;
				if (orig != data)
					si_pif_phy1_wreg(adev,PB1_PIF_PWRDOWN_2, data);

				orig = data = si_pif_phy1_rreg(adev,PB1_PIF_PWRDOWN_3);
				data &= ~PLL_RAMP_UP_TIME_3_MASK;
				if (orig != data)
					si_pif_phy1_wreg(adev,PB1_PIF_PWRDOWN_3, data);
			}
			orig = data = RREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL);
			data &= ~LC_DYN_LANES_PWR_STATE_MASK;
			data |= LC_DYN_LANES_PWR_STATE(3);
			if (orig != data)
				WREG32_PCIE_PORT(PCIE_LC_LINK_WIDTH_CNTL, data);

			orig = data = si_pif_phy0_rreg(adev,PB0_PIF_CNTL);
			data &= ~LS2_EXIT_TIME_MASK;
			if ((adev->asic_type == CHIP_OLAND) || (adev->asic_type == CHIP_HAINAN))
				data |= LS2_EXIT_TIME(5);
			if (orig != data)
				si_pif_phy0_wreg(adev,PB0_PIF_CNTL, data);

			orig = data = si_pif_phy1_rreg(adev,PB1_PIF_CNTL);
			data &= ~LS2_EXIT_TIME_MASK;
			if ((adev->asic_type == CHIP_OLAND) || (adev->asic_type == CHIP_HAINAN))
				data |= LS2_EXIT_TIME(5);
			if (orig != data)
				si_pif_phy1_wreg(adev,PB1_PIF_CNTL, data);

			if (!disable_clkreq &&
			    !pci_is_root_bus(adev->pdev->bus)) {
				struct pci_dev *root = adev->pdev->bus->self;
				u32 lnkcap;

				clk_req_support = false;
				pcie_capability_read_dword(root, PCI_EXP_LNKCAP, &lnkcap);
				if (lnkcap & PCI_EXP_LNKCAP_CLKPM)
					clk_req_support = true;
			} else {
				clk_req_support = false;
			}

			if (clk_req_support) {
				orig = data = RREG32_PCIE_PORT(PCIE_LC_CNTL2);
				data |= LC_ALLOW_PDWN_IN_L1 | LC_ALLOW_PDWN_IN_L23;
				if (orig != data)
					WREG32_PCIE_PORT(PCIE_LC_CNTL2, data);

				orig = data = RREG32(THM_CLK_CNTL);
				data &= ~(CMON_CLK_SEL_MASK | TMON_CLK_SEL_MASK);
				data |= CMON_CLK_SEL(1) | TMON_CLK_SEL(1);
				if (orig != data)
					WREG32(THM_CLK_CNTL, data);

				orig = data = RREG32(MISC_CLK_CNTL);
				data &= ~(DEEP_SLEEP_CLK_SEL_MASK | ZCLK_SEL_MASK);
				data |= DEEP_SLEEP_CLK_SEL(1) | ZCLK_SEL(1);
				if (orig != data)
					WREG32(MISC_CLK_CNTL, data);

				orig = data = RREG32(CG_CLKPIN_CNTL);
				data &= ~BCLK_AS_XCLK;
				if (orig != data)
					WREG32(CG_CLKPIN_CNTL, data);

				orig = data = RREG32(CG_CLKPIN_CNTL_2);
				data &= ~FORCE_BIF_REFCLK_EN;
				if (orig != data)
					WREG32(CG_CLKPIN_CNTL_2, data);

				orig = data = RREG32(MPLL_BYPASSCLK_SEL);
				data &= ~MPLL_CLKOUT_SEL_MASK;
				data |= MPLL_CLKOUT_SEL(4);
				if (orig != data)
					WREG32(MPLL_BYPASSCLK_SEL, data);

				orig = data = RREG32(SPLL_CNTL_MODE);
				data &= ~SPLL_REFCLK_SEL_MASK;
				if (orig != data)
					WREG32(SPLL_CNTL_MODE, data);
			}
		}
	} else {
		if (orig != data)
			WREG32_PCIE_PORT(PCIE_LC_CNTL, data);
	}

	orig = data = RREG32_PCIE(PCIE_CNTL2);
	data |= SLV_MEM_LS_EN | MST_MEM_LS_EN | REPLAY_MEM_LS_EN;
	if (orig != data)
		WREG32_PCIE(PCIE_CNTL2, data);

	if (!disable_l0s) {
		data = RREG32_PCIE_PORT(PCIE_LC_N_FTS_CNTL);
		if((data & LC_N_FTS_MASK) == LC_N_FTS_MASK) {
			data = RREG32_PCIE(PCIE_LC_STATUS1);
			if ((data & LC_REVERSE_XMIT) && (data & LC_REVERSE_RCVR)) {
				orig = data = RREG32_PCIE_PORT(PCIE_LC_CNTL);
				data &= ~LC_L0S_INACTIVITY_MASK;
				if (orig != data)
					WREG32_PCIE_PORT(PCIE_LC_CNTL, data);
			}
		}
	}
}

static void si_fix_pci_max_read_req_size(struct amdgpu_device *adev)
{
	int readrq;
	u16 v;

	readrq = pcie_get_readrq(adev->pdev);
	v = ffs(readrq) - 8;
	if ((v == 0) || (v == 6) || (v == 7))
		pcie_set_readrq(adev->pdev, 512);
}

static int si_common_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	si_fix_pci_max_read_req_size(adev);
	si_init_golden_registers(adev);
	si_pcie_gen3_enable(adev);
	si_program_aspm(adev);

	return 0;
}

static int si_common_hw_fini(void *handle)
{
	return 0;
}

static int si_common_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return si_common_hw_fini(adev);
}

static int si_common_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return si_common_hw_init(adev);
}

static bool si_common_is_idle(void *handle)
{
	return true;
}

static int si_common_wait_for_idle(void *handle)
{
	return 0;
}

static int si_common_soft_reset(void *handle)
{
	return 0;
}

static int si_common_set_clockgating_state(void *handle,
					    enum amd_clockgating_state state)
{
	return 0;
}

static int si_common_set_powergating_state(void *handle,
					    enum amd_powergating_state state)
{
	return 0;
}

static const struct amd_ip_funcs si_common_ip_funcs = {
	.name = "si_common",
	.early_init = si_common_early_init,
	.late_init = NULL,
	.sw_init = si_common_sw_init,
	.sw_fini = si_common_sw_fini,
	.hw_init = si_common_hw_init,
	.hw_fini = si_common_hw_fini,
	.suspend = si_common_suspend,
	.resume = si_common_resume,
	.is_idle = si_common_is_idle,
	.wait_for_idle = si_common_wait_for_idle,
	.soft_reset = si_common_soft_reset,
	.set_clockgating_state = si_common_set_clockgating_state,
	.set_powergating_state = si_common_set_powergating_state,
};

static const struct amdgpu_ip_block_version si_common_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_COMMON,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &si_common_ip_funcs,
};

int si_set_ip_blocks(struct amdgpu_device *adev)
{
	si_detect_hw_virtualization(adev);

	switch (adev->asic_type) {
	case CHIP_VERDE:
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
		amdgpu_device_ip_block_add(adev, &si_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v6_0_ip_block);
		amdgpu_device_ip_block_add(adev, &si_ih_ip_block);
		amdgpu_device_ip_block_add(adev, &si_smu_ip_block);
		if (adev->enable_virtual_display)
			amdgpu_device_ip_block_add(adev, &dce_virtual_ip_block);
		else
			amdgpu_device_ip_block_add(adev, &dce_v6_0_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v6_0_ip_block);
		amdgpu_device_ip_block_add(adev, &si_dma_ip_block);
		/* amdgpu_device_ip_block_add(adev, &uvd_v3_1_ip_block); */
		/* amdgpu_device_ip_block_add(adev, &vce_v1_0_ip_block); */
		break;
	case CHIP_OLAND:
		amdgpu_device_ip_block_add(adev, &si_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v6_0_ip_block);
		amdgpu_device_ip_block_add(adev, &si_ih_ip_block);
		amdgpu_device_ip_block_add(adev, &si_smu_ip_block);
		if (adev->enable_virtual_display)
			amdgpu_device_ip_block_add(adev, &dce_virtual_ip_block);
		else
			amdgpu_device_ip_block_add(adev, &dce_v6_4_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v6_0_ip_block);
		amdgpu_device_ip_block_add(adev, &si_dma_ip_block);
		/* amdgpu_device_ip_block_add(adev, &uvd_v3_1_ip_block); */
		/* amdgpu_device_ip_block_add(adev, &vce_v1_0_ip_block); */
		break;
	case CHIP_HAINAN:
		amdgpu_device_ip_block_add(adev, &si_common_ip_block);
		amdgpu_device_ip_block_add(adev, &gmc_v6_0_ip_block);
		amdgpu_device_ip_block_add(adev, &si_ih_ip_block);
		amdgpu_device_ip_block_add(adev, &si_smu_ip_block);
		if (adev->enable_virtual_display)
			amdgpu_device_ip_block_add(adev, &dce_virtual_ip_block);
		amdgpu_device_ip_block_add(adev, &gfx_v6_0_ip_block);
		amdgpu_device_ip_block_add(adev, &si_dma_ip_block);
		break;
	default:
		BUG();
	}
	return 0;
}

