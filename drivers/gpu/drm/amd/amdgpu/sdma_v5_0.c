/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_ucode.h"
#include "amdgpu_trace.h"

#include "gc/gc_10_1_0_offset.h"
#include "gc/gc_10_1_0_sh_mask.h"
#include "ivsrcid/sdma0/irqsrcs_sdma0_5_0.h"
#include "ivsrcid/sdma1/irqsrcs_sdma1_5_0.h"

#include "soc15_common.h"
#include "soc15.h"
#include "navi10_sdma_pkt_open.h"
#include "nbio_v2_3.h"
#include "sdma_common.h"
#include "sdma_v5_0.h"

MODULE_FIRMWARE("amdgpu/navi10_sdma.bin");
MODULE_FIRMWARE("amdgpu/navi10_sdma1.bin");

MODULE_FIRMWARE("amdgpu/navi14_sdma.bin");
MODULE_FIRMWARE("amdgpu/navi14_sdma1.bin");

MODULE_FIRMWARE("amdgpu/navi12_sdma.bin");
MODULE_FIRMWARE("amdgpu/navi12_sdma1.bin");

MODULE_FIRMWARE("amdgpu/cyan_skillfish2_sdma.bin");
MODULE_FIRMWARE("amdgpu/cyan_skillfish2_sdma1.bin");

#define SDMA1_REG_OFFSET 0x600
#define SDMA0_HYP_DEC_REG_START 0x5880
#define SDMA0_HYP_DEC_REG_END 0x5893
#define SDMA1_HYP_DEC_REG_OFFSET 0x20

static const struct amdgpu_hwip_reg_entry sdma_reg_list_5_0[] = {
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_STATUS_REG),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_STATUS1_REG),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_STATUS2_REG),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_STATUS3_REG),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_UCODE_CHECKSUM),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_RB_RPTR_FETCH_HI),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_RB_RPTR_FETCH),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_UTCL1_RD_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_UTCL1_WR_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_UTCL1_RD_XNACK0),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_UTCL1_RD_XNACK1),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_UTCL1_WR_XNACK0),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_UTCL1_WR_XNACK1),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_GFX_RB_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_GFX_RB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_GFX_RB_RPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_GFX_RB_WPTR),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_GFX_RB_WPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_GFX_IB_OFFSET),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_GFX_IB_BASE_LO),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_GFX_IB_BASE_HI),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_GFX_IB_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_GFX_IB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_GFX_IB_SUB_REMAIN),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_GFX_DUMMY_REG),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_PAGE_RB_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_PAGE_RB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_PAGE_RB_RPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_PAGE_RB_WPTR),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_PAGE_RB_WPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_PAGE_IB_OFFSET),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_PAGE_IB_BASE_LO),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_PAGE_IB_BASE_HI),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_PAGE_DUMMY_REG),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_RLC0_RB_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_RLC0_RB_RPTR),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_RLC0_RB_RPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_RLC0_RB_WPTR),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_RLC0_RB_WPTR_HI),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_RLC0_IB_OFFSET),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_RLC0_IB_BASE_LO),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_RLC0_IB_BASE_HI),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_RLC0_DUMMY_REG),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_INT_STATUS),
	SOC15_REG_ENTRY_STR(GC, 0, mmSDMA0_VM_CNTL),
	SOC15_REG_ENTRY_STR(GC, 0, mmGRBM_STATUS2)
};

static void sdma_v5_0_set_ring_funcs(struct amdgpu_device *adev);
static void sdma_v5_0_set_buffer_funcs(struct amdgpu_device *adev);
static void sdma_v5_0_set_vm_pte_funcs(struct amdgpu_device *adev);
static void sdma_v5_0_set_irq_funcs(struct amdgpu_device *adev);

static const struct soc15_reg_golden golden_settings_sdma_5[] = {
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_CHICKEN_BITS, 0xffbf1f0f, 0x03ab0107),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_GFX_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_PAGE_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC0_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC1_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC2_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC3_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC4_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC5_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC6_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC7_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_UTCL1_PAGE, 0x00ffffff, 0x000c5c00),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_CHICKEN_BITS, 0xffbf1f0f, 0x03ab0107),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_GFX_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_PAGE_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC0_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC1_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC2_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC3_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC4_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC5_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC6_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC7_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_UTCL1_PAGE, 0x00ffffff, 0x000c5c00)
};

static const struct soc15_reg_golden golden_settings_sdma_5_sriov[] = {
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_GFX_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_PAGE_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC0_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC1_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC2_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC3_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC4_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC5_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC6_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC7_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_GFX_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_PAGE_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC0_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC1_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC2_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC3_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC4_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC5_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC6_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC7_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
};

static const struct soc15_reg_golden golden_settings_sdma_nv10[] = {
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC3_RB_WPTR_POLL_CNTL, 0x0000fff0, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC3_RB_WPTR_POLL_CNTL, 0x0000fff0, 0x00403000),
};

static const struct soc15_reg_golden golden_settings_sdma_nv14[] = {
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC3_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC3_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
};

static const struct soc15_reg_golden golden_settings_sdma_nv12[] = {
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC3_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_GB_ADDR_CONFIG, 0x001877ff, 0x00000044),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_GB_ADDR_CONFIG_READ, 0x001877ff, 0x00000044),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_GB_ADDR_CONFIG, 0x001877ff, 0x00000044),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_GB_ADDR_CONFIG_READ, 0x001877ff, 0x00000044),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC3_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
};

static const struct soc15_reg_golden golden_settings_sdma_cyan_skillfish[] = {
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_CHICKEN_BITS, 0xffbf1f0f, 0x03ab0107),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_GB_ADDR_CONFIG, 0x001877ff, 0x00000044),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_GB_ADDR_CONFIG_READ, 0x001877ff, 0x00000044),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_GFX_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_PAGE_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC0_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC1_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC2_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC3_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC4_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC5_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC6_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_RLC7_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA0_UTCL1_PAGE, 0x007fffff, 0x004c5c00),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_CHICKEN_BITS, 0xffbf1f0f, 0x03ab0107),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_GB_ADDR_CONFIG, 0x001877ff, 0x00000044),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_GB_ADDR_CONFIG_READ, 0x001877ff, 0x00000044),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_GFX_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_PAGE_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC0_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC1_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC2_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC3_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC4_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC5_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC6_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_RLC7_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, mmSDMA1_UTCL1_PAGE, 0x007fffff, 0x004c5c00)
};

static u32 sdma_v5_0_get_reg_offset(struct amdgpu_device *adev, u32 instance, u32 internal_offset)
{
	u32 base;

	if (internal_offset >= SDMA0_HYP_DEC_REG_START &&
	    internal_offset <= SDMA0_HYP_DEC_REG_END) {
		base = adev->reg_offset[GC_HWIP][0][1];
		if (instance == 1)
			internal_offset += SDMA1_HYP_DEC_REG_OFFSET;
	} else {
		base = adev->reg_offset[GC_HWIP][0][0];
		if (instance == 1)
			internal_offset += SDMA1_REG_OFFSET;
	}

	return base + internal_offset;
}

static void sdma_v5_0_init_golden_registers(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, SDMA0_HWIP, 0)) {
	case IP_VERSION(5, 0, 0):
		soc15_program_register_sequence(adev,
						golden_settings_sdma_5,
						(const u32)ARRAY_SIZE(golden_settings_sdma_5));
		soc15_program_register_sequence(adev,
						golden_settings_sdma_nv10,
						(const u32)ARRAY_SIZE(golden_settings_sdma_nv10));
		break;
	case IP_VERSION(5, 0, 2):
		soc15_program_register_sequence(adev,
						golden_settings_sdma_5,
						(const u32)ARRAY_SIZE(golden_settings_sdma_5));
		soc15_program_register_sequence(adev,
						golden_settings_sdma_nv14,
						(const u32)ARRAY_SIZE(golden_settings_sdma_nv14));
		break;
	case IP_VERSION(5, 0, 5):
		if (amdgpu_sriov_vf(adev))
			soc15_program_register_sequence(adev,
							golden_settings_sdma_5_sriov,
							(const u32)ARRAY_SIZE(golden_settings_sdma_5_sriov));
		else
			soc15_program_register_sequence(adev,
							golden_settings_sdma_5,
							(const u32)ARRAY_SIZE(golden_settings_sdma_5));
		soc15_program_register_sequence(adev,
						golden_settings_sdma_nv12,
						(const u32)ARRAY_SIZE(golden_settings_sdma_nv12));
		break;
	case IP_VERSION(5, 0, 1):
		soc15_program_register_sequence(adev,
						golden_settings_sdma_cyan_skillfish,
						(const u32)ARRAY_SIZE(golden_settings_sdma_cyan_skillfish));
		break;
	default:
		break;
	}
}

/**
 * sdma_v5_0_init_microcode - load ucode images from disk
 *
 * @adev: amdgpu_device pointer
 *
 * Use the firmware interface to load the ucode images into
 * the driver (not loaded into hw).
 * Returns 0 on success, error on failure.
 */

// emulation only, won't work on real chip
// navi10 real chip need to use PSP to load firmware
static int sdma_v5_0_init_microcode(struct amdgpu_device *adev)
{
	int ret, i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ret = amdgpu_sdma_init_microcode(adev, i, false);
		if (ret)
			return ret;
	}

	return ret;
}

static unsigned sdma_v5_0_ring_init_cond_exec(struct amdgpu_ring *ring,
					      uint64_t addr)
{
	unsigned ret;

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_COND_EXE));
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, 1);
	/* this is the offset we need patch later */
	ret = ring->wptr & ring->buf_mask;
	/* insert dummy here and patch it later */
	amdgpu_ring_write(ring, 0);

	return ret;
}

/**
 * sdma_v5_0_ring_get_rptr - get the current read pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current rptr from the hardware (NAVI10+).
 */
static uint64_t sdma_v5_0_ring_get_rptr(struct amdgpu_ring *ring)
{
	u64 *rptr;

	/* XXX check if swapping is necessary on BE */
	rptr = (u64 *)ring->rptr_cpu_addr;

	DRM_DEBUG("rptr before shift == 0x%016llx\n", *rptr);
	return ((*rptr) >> 2);
}

/**
 * sdma_v5_0_ring_get_wptr - get the current write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current wptr from the hardware (NAVI10+).
 */
static uint64_t sdma_v5_0_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u64 wptr;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		wptr = READ_ONCE(*((u64 *)ring->wptr_cpu_addr));
		DRM_DEBUG("wptr/doorbell before shift == 0x%016llx\n", wptr);
	} else {
		wptr = RREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, ring->me, mmSDMA0_GFX_RB_WPTR_HI));
		wptr = wptr << 32;
		wptr |= RREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, ring->me, mmSDMA0_GFX_RB_WPTR));
		DRM_DEBUG("wptr before shift [%i] wptr == 0x%016llx\n", ring->me, wptr);
	}

	return wptr >> 2;
}

/**
 * sdma_v5_0_ring_set_wptr - commit the write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Write the wptr back to the hardware (NAVI10+).
 */
static void sdma_v5_0_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t *wptr_saved;
	uint32_t *is_queue_unmap;
	uint64_t aggregated_db_index;
	uint32_t mqd_size = adev->mqds[AMDGPU_HW_IP_DMA].mqd_size;

	DRM_DEBUG("Setting write pointer\n");
	if (ring->is_mes_queue) {
		wptr_saved = (uint32_t *)(ring->mqd_ptr + mqd_size);
		is_queue_unmap = (uint32_t *)(ring->mqd_ptr + mqd_size +
					      sizeof(uint32_t));
		aggregated_db_index =
			amdgpu_mes_get_aggregated_doorbell_index(adev,
			AMDGPU_MES_PRIORITY_LEVEL_NORMAL);

		atomic64_set((atomic64_t *)ring->wptr_cpu_addr,
			     ring->wptr << 2);
		*wptr_saved = ring->wptr << 2;
		if (*is_queue_unmap) {
			WDOORBELL64(aggregated_db_index, ring->wptr << 2);
			DRM_DEBUG("calling WDOORBELL64(0x%08x, 0x%016llx)\n",
					ring->doorbell_index, ring->wptr << 2);
			WDOORBELL64(ring->doorbell_index, ring->wptr << 2);
		} else {
			DRM_DEBUG("calling WDOORBELL64(0x%08x, 0x%016llx)\n",
					ring->doorbell_index, ring->wptr << 2);
			WDOORBELL64(ring->doorbell_index, ring->wptr << 2);

			if (*is_queue_unmap)
				WDOORBELL64(aggregated_db_index,
					    ring->wptr << 2);
		}
	} else {
		if (ring->use_doorbell) {
			DRM_DEBUG("Using doorbell -- "
				  "wptr_offs == 0x%08x "
				  "lower_32_bits(ring->wptr) << 2 == 0x%08x "
				  "upper_32_bits(ring->wptr) << 2 == 0x%08x\n",
				  ring->wptr_offs,
				  lower_32_bits(ring->wptr << 2),
				  upper_32_bits(ring->wptr << 2));
			/* XXX check if swapping is necessary on BE */
			atomic64_set((atomic64_t *)ring->wptr_cpu_addr,
				     ring->wptr << 2);
			DRM_DEBUG("calling WDOORBELL64(0x%08x, 0x%016llx)\n",
				  ring->doorbell_index, ring->wptr << 2);
			WDOORBELL64(ring->doorbell_index, ring->wptr << 2);
		} else {
			DRM_DEBUG("Not using doorbell -- "
				  "mmSDMA%i_GFX_RB_WPTR == 0x%08x "
				  "mmSDMA%i_GFX_RB_WPTR_HI == 0x%08x\n",
				  ring->me,
				  lower_32_bits(ring->wptr << 2),
				  ring->me,
				  upper_32_bits(ring->wptr << 2));
			WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev,
					     ring->me, mmSDMA0_GFX_RB_WPTR),
					lower_32_bits(ring->wptr << 2));
			WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev,
					     ring->me, mmSDMA0_GFX_RB_WPTR_HI),
					upper_32_bits(ring->wptr << 2));
		}
	}
}

static void sdma_v5_0_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
{
	struct amdgpu_sdma_instance *sdma = amdgpu_sdma_get_instance_from_ring(ring);
	int i;

	for (i = 0; i < count; i++)
		if (sdma && sdma->burst_nop && (i == 0))
			amdgpu_ring_write(ring, ring->funcs->nop |
				SDMA_PKT_NOP_HEADER_COUNT(count - 1));
		else
			amdgpu_ring_write(ring, ring->funcs->nop);
}

/**
 * sdma_v5_0_ring_emit_ib - Schedule an IB on the DMA engine
 *
 * @ring: amdgpu ring pointer
 * @job: job to retrieve vmid from
 * @ib: IB object to schedule
 * @flags: unused
 *
 * Schedule an IB in the DMA ring (NAVI10).
 */
static void sdma_v5_0_ring_emit_ib(struct amdgpu_ring *ring,
				   struct amdgpu_job *job,
				   struct amdgpu_ib *ib,
				   uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);
	uint64_t csa_mc_addr = amdgpu_sdma_get_csa_mc_addr(ring, vmid);

	/* An IB packet must end on a 8 DW boundary--the next dword
	 * must be on a 8-dword boundary. Our IB packet below is 6
	 * dwords long, thus add x number of NOPs, such that, in
	 * modular arithmetic,
	 * wptr + 6 + x = 8k, k >= 0, which in C is,
	 * (wptr + 6 + x) % 8 = 0.
	 * The expression below, is a solution of x.
	 */
	sdma_v5_0_ring_insert_nop(ring, (2 - lower_32_bits(ring->wptr)) & 7);

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_INDIRECT) |
			  SDMA_PKT_INDIRECT_HEADER_VMID(vmid & 0xf));
	/* base must be 32 byte aligned */
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr) & 0xffffffe0);
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, ib->length_dw);
	amdgpu_ring_write(ring, lower_32_bits(csa_mc_addr));
	amdgpu_ring_write(ring, upper_32_bits(csa_mc_addr));
}

/**
 * sdma_v5_0_ring_emit_mem_sync - flush the IB by graphics cache rinse
 *
 * @ring: amdgpu ring pointer
 *
 * flush the IB by graphics cache rinse.
 */
static void sdma_v5_0_ring_emit_mem_sync(struct amdgpu_ring *ring)
{
	uint32_t gcr_cntl = SDMA_GCR_GL2_INV | SDMA_GCR_GL2_WB | SDMA_GCR_GLM_INV |
			    SDMA_GCR_GL1_INV | SDMA_GCR_GLV_INV | SDMA_GCR_GLK_INV |
			    SDMA_GCR_GLI_INV(1);

	/* flush entire cache L0/L1/L2, this can be optimized by performance requirement */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_GCR_REQ));
	amdgpu_ring_write(ring, SDMA_PKT_GCR_REQ_PAYLOAD1_BASE_VA_31_7(0));
	amdgpu_ring_write(ring, SDMA_PKT_GCR_REQ_PAYLOAD2_GCR_CONTROL_15_0(gcr_cntl) |
			  SDMA_PKT_GCR_REQ_PAYLOAD2_BASE_VA_47_32(0));
	amdgpu_ring_write(ring, SDMA_PKT_GCR_REQ_PAYLOAD3_LIMIT_VA_31_7(0) |
			  SDMA_PKT_GCR_REQ_PAYLOAD3_GCR_CONTROL_18_16(gcr_cntl >> 16));
	amdgpu_ring_write(ring, SDMA_PKT_GCR_REQ_PAYLOAD4_LIMIT_VA_47_32(0) |
			  SDMA_PKT_GCR_REQ_PAYLOAD4_VMID(0));
}

/**
 * sdma_v5_0_ring_emit_hdp_flush - emit an hdp flush on the DMA ring
 *
 * @ring: amdgpu ring pointer
 *
 * Emit an hdp flush packet on the requested DMA ring.
 */
static void sdma_v5_0_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 ref_and_mask = 0;
	const struct nbio_hdp_flush_reg *nbio_hf_reg = adev->nbio.hdp_flush_reg;

	if (ring->me == 0)
		ref_and_mask = nbio_hf_reg->ref_and_mask_sdma0;
	else
		ref_and_mask = nbio_hf_reg->ref_and_mask_sdma1;

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_HDP_FLUSH(1) |
			  SDMA_PKT_POLL_REGMEM_HEADER_FUNC(3)); /* == */
	amdgpu_ring_write(ring, (adev->nbio.funcs->get_hdp_flush_done_offset(adev)) << 2);
	amdgpu_ring_write(ring, (adev->nbio.funcs->get_hdp_flush_req_offset(adev)) << 2);
	amdgpu_ring_write(ring, ref_and_mask); /* reference */
	amdgpu_ring_write(ring, ref_and_mask); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(10)); /* retry count, poll interval */
}

/**
 * sdma_v5_0_ring_emit_fence - emit a fence on the DMA ring
 *
 * @ring: amdgpu ring pointer
 * @addr: address
 * @seq: sequence number
 * @flags: fence related flags
 *
 * Add a DMA fence packet to the ring to write
 * the fence seq number and DMA trap packet to generate
 * an interrupt if needed (NAVI10).
 */
static void sdma_v5_0_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				      unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	/* write the fence */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_FENCE) |
			  SDMA_PKT_FENCE_HEADER_MTYPE(0x3)); /* Ucached(UC) */
	/* zero in first two bits */
	BUG_ON(addr & 0x3);
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));

	/* optionally write high bits as well */
	if (write64bit) {
		addr += 4;
		amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_FENCE) |
				  SDMA_PKT_FENCE_HEADER_MTYPE(0x3));
		/* zero in first two bits */
		BUG_ON(addr & 0x3);
		amdgpu_ring_write(ring, lower_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(seq));
	}

	if (flags & AMDGPU_FENCE_FLAG_INT) {
		uint32_t ctx = ring->is_mes_queue ?
			(ring->hw_queue_id | AMDGPU_FENCE_MES_QUEUE_FLAG) : 0;
		/* generate an interrupt */
		amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_TRAP));
		amdgpu_ring_write(ring, SDMA_PKT_TRAP_INT_CONTEXT_INT_CONTEXT(ctx));
	}
}


/**
 * sdma_v5_0_gfx_stop - stop the gfx async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the gfx async dma ring buffers (NAVI10).
 */
static void sdma_v5_0_gfx_stop(struct amdgpu_device *adev)
{
	u32 rb_cntl, ib_cntl;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		rb_cntl = RREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_CNTL));
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_ENABLE, 0);
		WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_CNTL), rb_cntl);
		ib_cntl = RREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_IB_CNTL));
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_ENABLE, 0);
		WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_IB_CNTL), ib_cntl);
	}
}

/**
 * sdma_v5_0_rlc_stop - stop the compute async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the compute async dma queues (NAVI10).
 */
static void sdma_v5_0_rlc_stop(struct amdgpu_device *adev)
{
	/* XXX todo */
}

/**
 * sdma_v5_0_ctx_switch_enable - stop the async dma engines context switch
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs context switch.
 *
 * Halt or unhalt the async dma engines context switch (NAVI10).
 */
static void sdma_v5_0_ctx_switch_enable(struct amdgpu_device *adev, bool enable)
{
	u32 f32_cntl = 0, phase_quantum = 0;
	int i;

	if (amdgpu_sdma_phase_quantum) {
		unsigned value = amdgpu_sdma_phase_quantum;
		unsigned unit = 0;

		while (value > (SDMA0_PHASE0_QUANTUM__VALUE_MASK >>
				SDMA0_PHASE0_QUANTUM__VALUE__SHIFT)) {
			value = (value + 1) >> 1;
			unit++;
		}
		if (unit > (SDMA0_PHASE0_QUANTUM__UNIT_MASK >>
			    SDMA0_PHASE0_QUANTUM__UNIT__SHIFT)) {
			value = (SDMA0_PHASE0_QUANTUM__VALUE_MASK >>
				 SDMA0_PHASE0_QUANTUM__VALUE__SHIFT);
			unit = (SDMA0_PHASE0_QUANTUM__UNIT_MASK >>
				SDMA0_PHASE0_QUANTUM__UNIT__SHIFT);
			WARN_ONCE(1,
			"clamping sdma_phase_quantum to %uK clock cycles\n",
				  value << unit);
		}
		phase_quantum =
			value << SDMA0_PHASE0_QUANTUM__VALUE__SHIFT |
			unit  << SDMA0_PHASE0_QUANTUM__UNIT__SHIFT;
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (!amdgpu_sriov_vf(adev)) {
			f32_cntl = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_CNTL));
			f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_CNTL,
						 AUTO_CTXSW_ENABLE, enable ? 1 : 0);
		}

		if (enable && amdgpu_sdma_phase_quantum) {
			WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_PHASE0_QUANTUM),
			       phase_quantum);
			WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_PHASE1_QUANTUM),
			       phase_quantum);
			WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_PHASE2_QUANTUM),
			       phase_quantum);
		}
		if (!amdgpu_sriov_vf(adev))
			WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_CNTL), f32_cntl);
	}

}

/**
 * sdma_v5_0_enable - stop the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs.
 *
 * Halt or unhalt the async dma engines (NAVI10).
 */
static void sdma_v5_0_enable(struct amdgpu_device *adev, bool enable)
{
	u32 f32_cntl;
	int i;

	if (!enable) {
		sdma_v5_0_gfx_stop(adev);
		sdma_v5_0_rlc_stop(adev);
	}

	if (amdgpu_sriov_vf(adev))
		return;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		f32_cntl = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_F32_CNTL));
		f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_F32_CNTL, HALT, enable ? 0 : 1);
		WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_F32_CNTL), f32_cntl);
	}
}

/**
 * sdma_v5_0_gfx_resume_instance - start/restart a certain sdma engine
 *
 * @adev: amdgpu_device pointer
 * @i: instance
 * @restore: used to restore wptr when restart
 *
 * Set up the gfx DMA ring buffers and enable them. On restart, we will restore wptr and rptr.
 * Return 0 for success.
 */
static int sdma_v5_0_gfx_resume_instance(struct amdgpu_device *adev, int i, bool restore)
{
	struct amdgpu_ring *ring;
	u32 rb_cntl, ib_cntl;
	u32 rb_bufsz;
	u32 doorbell;
	u32 doorbell_offset;
	u32 temp;
	u32 wptr_poll_cntl;
	u64 wptr_gpu_addr;

	ring = &adev->sdma.instance[i].ring;

	if (!amdgpu_sriov_vf(adev))
		WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_SEM_WAIT_FAIL_TIMER_CNTL), 0);

	/* Set ring buffer size in dwords */
	rb_bufsz = order_base_2(ring->ring_size / 4);
	rb_cntl = RREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_CNTL));
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_SIZE, rb_bufsz);
#ifdef __BIG_ENDIAN
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_SWAP_ENABLE, 1);
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL,
				RPTR_WRITEBACK_SWAP_ENABLE, 1);
#endif
	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_CNTL), rb_cntl);

	/* Initialize the ring buffer's read and write pointers */
	if (restore) {
		WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_RPTR), lower_32_bits(ring->wptr << 2));
		WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_RPTR_HI), upper_32_bits(ring->wptr << 2));
		WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR), lower_32_bits(ring->wptr << 2));
		WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR_HI), upper_32_bits(ring->wptr << 2));
	} else {
		WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_RPTR), 0);
		WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_RPTR_HI), 0);
		WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR), 0);
		WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR_HI), 0);
	}
	/* setup the wptr shadow polling */
	wptr_gpu_addr = ring->wptr_gpu_addr;
	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR_POLL_ADDR_LO),
	       lower_32_bits(wptr_gpu_addr));
	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR_POLL_ADDR_HI),
	       upper_32_bits(wptr_gpu_addr));
	wptr_poll_cntl = RREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i,
						 mmSDMA0_GFX_RB_WPTR_POLL_CNTL));
	wptr_poll_cntl = REG_SET_FIELD(wptr_poll_cntl,
				       SDMA0_GFX_RB_WPTR_POLL_CNTL,
				       F32_POLL_ENABLE, 1);
	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR_POLL_CNTL),
	       wptr_poll_cntl);

	/* set the wb address whether it's enabled or not */
	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_RPTR_ADDR_HI),
	       upper_32_bits(ring->rptr_gpu_addr) & 0xFFFFFFFF);
	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_RPTR_ADDR_LO),
	       lower_32_bits(ring->rptr_gpu_addr) & 0xFFFFFFFC);

	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RPTR_WRITEBACK_ENABLE, 1);

	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_BASE),
	       ring->gpu_addr >> 8);
	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_BASE_HI),
	       ring->gpu_addr >> 40);

	if (!restore)
		ring->wptr = 0;

	/* before programing wptr to a less value, need set minor_ptr_update first */
	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_MINOR_PTR_UPDATE), 1);

	if (!amdgpu_sriov_vf(adev)) { /* only bare-metal use register write for wptr */
		WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR),
		       lower_32_bits(ring->wptr << 2));
		WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_WPTR_HI),
		       upper_32_bits(ring->wptr << 2));
	}

	doorbell = RREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_DOORBELL));
	doorbell_offset = RREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i,
					mmSDMA0_GFX_DOORBELL_OFFSET));

	if (ring->use_doorbell) {
		doorbell = REG_SET_FIELD(doorbell, SDMA0_GFX_DOORBELL, ENABLE, 1);
		doorbell_offset = REG_SET_FIELD(doorbell_offset, SDMA0_GFX_DOORBELL_OFFSET,
				OFFSET, ring->doorbell_index);
	} else {
		doorbell = REG_SET_FIELD(doorbell, SDMA0_GFX_DOORBELL, ENABLE, 0);
	}
	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_DOORBELL), doorbell);
	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_DOORBELL_OFFSET),
	       doorbell_offset);

	adev->nbio.funcs->sdma_doorbell_range(adev, i, ring->use_doorbell,
					      ring->doorbell_index, 20);

	if (amdgpu_sriov_vf(adev))
		sdma_v5_0_ring_set_wptr(ring);

	/* set minor_ptr_update to 0 after wptr programed */
	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_MINOR_PTR_UPDATE), 0);

	if (!amdgpu_sriov_vf(adev)) {
		/* set utc l1 enable flag always to 1 */
		temp = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_CNTL));
		temp = REG_SET_FIELD(temp, SDMA0_CNTL, UTC_L1_ENABLE, 1);

		/* enable MCBP */
		temp = REG_SET_FIELD(temp, SDMA0_CNTL, MIDCMD_PREEMPT_ENABLE, 1);
		WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_CNTL), temp);

		/* Set up RESP_MODE to non-copy addresses */
		temp = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_UTCL1_CNTL));
		temp = REG_SET_FIELD(temp, SDMA0_UTCL1_CNTL, RESP_MODE, 3);
		temp = REG_SET_FIELD(temp, SDMA0_UTCL1_CNTL, REDO_DELAY, 9);
		WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_UTCL1_CNTL), temp);

		/* program default cache read and write policy */
		temp = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_UTCL1_PAGE));
		/* clean read policy and write policy bits */
		temp &= 0xFF0FFF;
		temp |= ((CACHE_READ_POLICY_L2__DEFAULT << 12) | (CACHE_WRITE_POLICY_L2__DEFAULT << 14));
		WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_UTCL1_PAGE), temp);
	}

	if (!amdgpu_sriov_vf(adev)) {
		/* unhalt engine */
		temp = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_F32_CNTL));
		temp = REG_SET_FIELD(temp, SDMA0_F32_CNTL, HALT, 0);
		WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_F32_CNTL), temp);
	}

	/* enable DMA RB */
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_ENABLE, 1);
	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_CNTL), rb_cntl);

	ib_cntl = RREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_IB_CNTL));
	ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_ENABLE, 1);
#ifdef __BIG_ENDIAN
	ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_SWAP_ENABLE, 1);
#endif
	/* enable DMA IBs */
	WREG32_SOC15_IP(GC, sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_IB_CNTL), ib_cntl);

	if (amdgpu_sriov_vf(adev)) { /* bare-metal sequence doesn't need below to lines */
		sdma_v5_0_ctx_switch_enable(adev, true);
		sdma_v5_0_enable(adev, true);
	}

	return amdgpu_ring_test_helper(ring);
}

/**
 * sdma_v5_0_gfx_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the gfx DMA ring buffers and enable them (NAVI10).
 * Returns 0 for success, error for failure.
 */
static int sdma_v5_0_gfx_resume(struct amdgpu_device *adev)
{
	int i, r;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		r = sdma_v5_0_gfx_resume_instance(adev, i, false);
		if (r)
			return r;
	}

	return 0;
}

/**
 * sdma_v5_0_rlc_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the compute DMA queues and enable them (NAVI10).
 * Returns 0 for success, error for failure.
 */
static int sdma_v5_0_rlc_resume(struct amdgpu_device *adev)
{
	return 0;
}

/**
 * sdma_v5_0_load_microcode - load the sDMA ME ucode
 *
 * @adev: amdgpu_device pointer
 *
 * Loads the sDMA0/1 ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int sdma_v5_0_load_microcode(struct amdgpu_device *adev)
{
	const struct sdma_firmware_header_v1_0 *hdr;
	const __le32 *fw_data;
	u32 fw_size;
	int i, j;

	/* halt the MEs */
	sdma_v5_0_enable(adev, false);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (!adev->sdma.instance[i].fw)
			return -EINVAL;

		hdr = (const struct sdma_firmware_header_v1_0 *)adev->sdma.instance[i].fw->data;
		amdgpu_ucode_print_sdma_hdr(&hdr->header);
		fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;

		fw_data = (const __le32 *)
			(adev->sdma.instance[i].fw->data +
				le32_to_cpu(hdr->header.ucode_array_offset_bytes));

		WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_UCODE_ADDR), 0);

		for (j = 0; j < fw_size; j++) {
			if (amdgpu_emu_mode == 1 && j % 500 == 0)
				msleep(1);
			WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_UCODE_DATA), le32_to_cpup(fw_data++));
		}

		WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_UCODE_ADDR), adev->sdma.instance[i].fw_version);
	}

	return 0;
}

/**
 * sdma_v5_0_start - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the DMA engines and enable them (NAVI10).
 * Returns 0 for success, error for failure.
 */
static int sdma_v5_0_start(struct amdgpu_device *adev)
{
	int r = 0;

	if (amdgpu_sriov_vf(adev)) {
		sdma_v5_0_ctx_switch_enable(adev, false);
		sdma_v5_0_enable(adev, false);

		/* set RB registers */
		r = sdma_v5_0_gfx_resume(adev);
		return r;
	}

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_DIRECT) {
		r = sdma_v5_0_load_microcode(adev);
		if (r)
			return r;
	}

	/* unhalt the MEs */
	sdma_v5_0_enable(adev, true);
	/* enable sdma ring preemption */
	sdma_v5_0_ctx_switch_enable(adev, true);

	/* start the gfx rings and rlc compute queues */
	r = sdma_v5_0_gfx_resume(adev);
	if (r)
		return r;
	r = sdma_v5_0_rlc_resume(adev);

	return r;
}

static int sdma_v5_0_mqd_init(struct amdgpu_device *adev, void *mqd,
			      struct amdgpu_mqd_prop *prop)
{
	struct v10_sdma_mqd *m = mqd;
	uint64_t wb_gpu_addr;

	m->sdmax_rlcx_rb_cntl =
		order_base_2(prop->queue_size / 4) << SDMA0_RLC0_RB_CNTL__RB_SIZE__SHIFT |
		1 << SDMA0_RLC0_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT |
		6 << SDMA0_RLC0_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT |
		1 << SDMA0_RLC0_RB_CNTL__RB_PRIV__SHIFT;

	m->sdmax_rlcx_rb_base = lower_32_bits(prop->hqd_base_gpu_addr >> 8);
	m->sdmax_rlcx_rb_base_hi = upper_32_bits(prop->hqd_base_gpu_addr >> 8);

	m->sdmax_rlcx_rb_wptr_poll_cntl = RREG32(sdma_v5_0_get_reg_offset(adev, 0,
						  mmSDMA0_GFX_RB_WPTR_POLL_CNTL));

	wb_gpu_addr = prop->wptr_gpu_addr;
	m->sdmax_rlcx_rb_wptr_poll_addr_lo = lower_32_bits(wb_gpu_addr);
	m->sdmax_rlcx_rb_wptr_poll_addr_hi = upper_32_bits(wb_gpu_addr);

	wb_gpu_addr = prop->rptr_gpu_addr;
	m->sdmax_rlcx_rb_rptr_addr_lo = lower_32_bits(wb_gpu_addr);
	m->sdmax_rlcx_rb_rptr_addr_hi = upper_32_bits(wb_gpu_addr);

	m->sdmax_rlcx_ib_cntl = RREG32(sdma_v5_0_get_reg_offset(adev, 0,
							mmSDMA0_GFX_IB_CNTL));

	m->sdmax_rlcx_doorbell_offset =
		prop->doorbell_index << SDMA0_RLC0_DOORBELL_OFFSET__OFFSET__SHIFT;

	m->sdmax_rlcx_doorbell = REG_SET_FIELD(0, SDMA0_RLC0_DOORBELL, ENABLE, 1);

	return 0;
}

static void sdma_v5_0_set_mqd_funcs(struct amdgpu_device *adev)
{
	adev->mqds[AMDGPU_HW_IP_DMA].mqd_size = sizeof(struct v10_sdma_mqd);
	adev->mqds[AMDGPU_HW_IP_DMA].init_mqd = sdma_v5_0_mqd_init;
}

/**
 * sdma_v5_0_ring_test_ring - simple async dma engine test
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test the DMA engine by writing using it to write an
 * value to memory. (NAVI10).
 * Returns 0 for success, error for failure.
 */
static int sdma_v5_0_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned i;
	unsigned index;
	int r;
	u32 tmp;
	u64 gpu_addr;
	volatile uint32_t *cpu_ptr = NULL;

	tmp = 0xCAFEDEAD;

	if (ring->is_mes_queue) {
		uint32_t offset = 0;
		offset = amdgpu_mes_ctx_get_offs(ring,
					 AMDGPU_MES_CTX_PADDING_OFFS);
		gpu_addr = amdgpu_mes_ctx_get_offs_gpu_addr(ring, offset);
		cpu_ptr = amdgpu_mes_ctx_get_offs_cpu_addr(ring, offset);
		*cpu_ptr = tmp;
	} else {
		r = amdgpu_device_wb_get(adev, &index);
		if (r) {
			dev_err(adev->dev, "(%d) failed to allocate wb slot\n", r);
			return r;
		}

		gpu_addr = adev->wb.gpu_addr + (index * 4);
		adev->wb.wb[index] = cpu_to_le32(tmp);
	}

	r = amdgpu_ring_alloc(ring, 20);
	if (r) {
		DRM_ERROR("amdgpu: dma failed to lock ring %d (%d).\n", ring->idx, r);
		if (!ring->is_mes_queue)
			amdgpu_device_wb_free(adev, index);
		return r;
	}

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_WRITE) |
			  SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_WRITE_LINEAR));
	amdgpu_ring_write(ring, lower_32_bits(gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(gpu_addr));
	amdgpu_ring_write(ring, SDMA_PKT_WRITE_UNTILED_DW_3_COUNT(0));
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		if (ring->is_mes_queue)
			tmp = le32_to_cpu(*cpu_ptr);
		else
			tmp = le32_to_cpu(adev->wb.wb[index]);
		if (tmp == 0xDEADBEEF)
			break;
		if (amdgpu_emu_mode == 1)
			msleep(1);
		else
			udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;

	if (!ring->is_mes_queue)
		amdgpu_device_wb_free(adev, index);

	return r;
}

/**
 * sdma_v5_0_ring_test_ib - test an IB on the DMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @timeout: timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 *
 * Test a simple IB in the DMA ring (NAVI10).
 * Returns 0 on success, error on failure.
 */
static int sdma_v5_0_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;
	unsigned index;
	long r;
	u32 tmp = 0;
	u64 gpu_addr;
	volatile uint32_t *cpu_ptr = NULL;

	tmp = 0xCAFEDEAD;
	memset(&ib, 0, sizeof(ib));

	if (ring->is_mes_queue) {
		uint32_t offset = 0;
		offset = amdgpu_mes_ctx_get_offs(ring, AMDGPU_MES_CTX_IB_OFFS);
		ib.gpu_addr = amdgpu_mes_ctx_get_offs_gpu_addr(ring, offset);
		ib.ptr = (void *)amdgpu_mes_ctx_get_offs_cpu_addr(ring, offset);

		offset = amdgpu_mes_ctx_get_offs(ring,
					 AMDGPU_MES_CTX_PADDING_OFFS);
		gpu_addr = amdgpu_mes_ctx_get_offs_gpu_addr(ring, offset);
		cpu_ptr = amdgpu_mes_ctx_get_offs_cpu_addr(ring, offset);
		*cpu_ptr = tmp;
	} else {
		r = amdgpu_device_wb_get(adev, &index);
		if (r) {
			dev_err(adev->dev, "(%ld) failed to allocate wb slot\n", r);
			return r;
		}

		gpu_addr = adev->wb.gpu_addr + (index * 4);
		adev->wb.wb[index] = cpu_to_le32(tmp);

		r = amdgpu_ib_get(adev, NULL, 256,
					AMDGPU_IB_POOL_DIRECT, &ib);
		if (r) {
			DRM_ERROR("amdgpu: failed to get ib (%ld).\n", r);
			goto err0;
		}
	}

	ib.ptr[0] = SDMA_PKT_HEADER_OP(SDMA_OP_WRITE) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_WRITE_LINEAR);
	ib.ptr[1] = lower_32_bits(gpu_addr);
	ib.ptr[2] = upper_32_bits(gpu_addr);
	ib.ptr[3] = SDMA_PKT_WRITE_UNTILED_DW_3_COUNT(0);
	ib.ptr[4] = 0xDEADBEEF;
	ib.ptr[5] = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP);
	ib.ptr[6] = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP);
	ib.ptr[7] = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP);
	ib.length_dw = 8;

	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, &f);
	if (r)
		goto err1;

	r = dma_fence_wait_timeout(f, false, timeout);
	if (r == 0) {
		DRM_ERROR("amdgpu: IB test timed out\n");
		r = -ETIMEDOUT;
		goto err1;
	} else if (r < 0) {
		DRM_ERROR("amdgpu: fence wait failed (%ld).\n", r);
		goto err1;
	}

	if (ring->is_mes_queue)
		tmp = le32_to_cpu(*cpu_ptr);
	else
		tmp = le32_to_cpu(adev->wb.wb[index]);

	if (tmp == 0xDEADBEEF)
		r = 0;
	else
		r = -EINVAL;

err1:
	amdgpu_ib_free(&ib, NULL);
	dma_fence_put(f);
err0:
	if (!ring->is_mes_queue)
		amdgpu_device_wb_free(adev, index);
	return r;
}


/**
 * sdma_v5_0_vm_copy_pte - update PTEs by copying them from the GART
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @src: src addr to copy from
 * @count: number of page entries to update
 *
 * Update PTEs by copying them from the GART using sDMA (NAVI10).
 */
static void sdma_v5_0_vm_copy_pte(struct amdgpu_ib *ib,
				  uint64_t pe, uint64_t src,
				  unsigned count)
{
	unsigned bytes = count * 8;

	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_COPY) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_COPY_LINEAR);
	ib->ptr[ib->length_dw++] = bytes - 1;
	ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
	ib->ptr[ib->length_dw++] = lower_32_bits(src);
	ib->ptr[ib->length_dw++] = upper_32_bits(src);
	ib->ptr[ib->length_dw++] = lower_32_bits(pe);
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);

}

/**
 * sdma_v5_0_vm_write_pte - update PTEs by writing them manually
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @value: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 *
 * Update PTEs by writing them manually using sDMA (NAVI10).
 */
static void sdma_v5_0_vm_write_pte(struct amdgpu_ib *ib, uint64_t pe,
				   uint64_t value, unsigned count,
				   uint32_t incr)
{
	unsigned ndw = count * 2;

	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_WRITE) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_WRITE_LINEAR);
	ib->ptr[ib->length_dw++] = lower_32_bits(pe);
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);
	ib->ptr[ib->length_dw++] = ndw - 1;
	for (; ndw > 0; ndw -= 2) {
		ib->ptr[ib->length_dw++] = lower_32_bits(value);
		ib->ptr[ib->length_dw++] = upper_32_bits(value);
		value += incr;
	}
}

/**
 * sdma_v5_0_vm_set_pte_pde - update the page tables using sDMA
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: access flags
 *
 * Update the page tables using sDMA (NAVI10).
 */
static void sdma_v5_0_vm_set_pte_pde(struct amdgpu_ib *ib,
				     uint64_t pe,
				     uint64_t addr, unsigned count,
				     uint32_t incr, uint64_t flags)
{
	/* for physically contiguous pages (vram) */
	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_PTEPDE);
	ib->ptr[ib->length_dw++] = lower_32_bits(pe); /* dst addr */
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);
	ib->ptr[ib->length_dw++] = lower_32_bits(flags); /* mask */
	ib->ptr[ib->length_dw++] = upper_32_bits(flags);
	ib->ptr[ib->length_dw++] = lower_32_bits(addr); /* value */
	ib->ptr[ib->length_dw++] = upper_32_bits(addr);
	ib->ptr[ib->length_dw++] = incr; /* increment size */
	ib->ptr[ib->length_dw++] = 0;
	ib->ptr[ib->length_dw++] = count - 1; /* number of entries */
}

/**
 * sdma_v5_0_ring_pad_ib - pad the IB
 * @ring: amdgpu_ring structure holding ring information
 * @ib: indirect buffer to fill with padding
 *
 * Pad the IB with NOPs to a boundary multiple of 8.
 */
static void sdma_v5_0_ring_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib)
{
	struct amdgpu_sdma_instance *sdma = amdgpu_sdma_get_instance_from_ring(ring);
	u32 pad_count;
	int i;

	pad_count = (-ib->length_dw) & 0x7;
	for (i = 0; i < pad_count; i++)
		if (sdma && sdma->burst_nop && (i == 0))
			ib->ptr[ib->length_dw++] =
				SDMA_PKT_HEADER_OP(SDMA_OP_NOP) |
				SDMA_PKT_NOP_HEADER_COUNT(pad_count - 1);
		else
			ib->ptr[ib->length_dw++] =
				SDMA_PKT_HEADER_OP(SDMA_OP_NOP);
}


/**
 * sdma_v5_0_ring_emit_pipeline_sync - sync the pipeline
 *
 * @ring: amdgpu_ring pointer
 *
 * Make sure all previous operations are completed (CIK).
 */
static void sdma_v5_0_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	/* wait for idle */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_HDP_FLUSH(0) |
			  SDMA_PKT_POLL_REGMEM_HEADER_FUNC(3) | /* equal */
			  SDMA_PKT_POLL_REGMEM_HEADER_MEM_POLL(1));
	amdgpu_ring_write(ring, addr & 0xfffffffc);
	amdgpu_ring_write(ring, upper_32_bits(addr) & 0xffffffff);
	amdgpu_ring_write(ring, seq); /* reference */
	amdgpu_ring_write(ring, 0xffffffff); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(4)); /* retry count, poll interval */
}


/**
 * sdma_v5_0_ring_emit_vm_flush - vm flush using sDMA
 *
 * @ring: amdgpu_ring pointer
 * @vmid: vmid number to use
 * @pd_addr: address
 *
 * Update the page table base and flush the VM TLB
 * using sDMA (NAVI10).
 */
static void sdma_v5_0_ring_emit_vm_flush(struct amdgpu_ring *ring,
					 unsigned vmid, uint64_t pd_addr)
{
	amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);
}

static void sdma_v5_0_ring_emit_wreg(struct amdgpu_ring *ring,
				     uint32_t reg, uint32_t val)
{
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_SRBM_WRITE) |
			  SDMA_PKT_SRBM_WRITE_HEADER_BYTE_EN(0xf));
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, val);
}

static void sdma_v5_0_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
					 uint32_t val, uint32_t mask)
{
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_HDP_FLUSH(0) |
			  SDMA_PKT_POLL_REGMEM_HEADER_FUNC(3)); /* equal */
	amdgpu_ring_write(ring, reg << 2);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, val); /* reference */
	amdgpu_ring_write(ring, mask); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(10));
}

static void sdma_v5_0_ring_emit_reg_write_reg_wait(struct amdgpu_ring *ring,
						   uint32_t reg0, uint32_t reg1,
						   uint32_t ref, uint32_t mask)
{
	amdgpu_ring_emit_wreg(ring, reg0, ref);
	/* wait for a cycle to reset vm_inv_eng*_ack */
	amdgpu_ring_emit_reg_wait(ring, reg0, 0, 0);
	amdgpu_ring_emit_reg_wait(ring, reg1, mask, mask);
}

static int sdma_v5_0_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = sdma_v5_0_init_microcode(adev);
	if (r)
		return r;

	sdma_v5_0_set_ring_funcs(adev);
	sdma_v5_0_set_buffer_funcs(adev);
	sdma_v5_0_set_vm_pte_funcs(adev);
	sdma_v5_0_set_irq_funcs(adev);
	sdma_v5_0_set_mqd_funcs(adev);

	return 0;
}


static int sdma_v5_0_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_ring *ring;
	int r, i;
	struct amdgpu_device *adev = ip_block->adev;
	uint32_t reg_count = ARRAY_SIZE(sdma_reg_list_5_0);
	uint32_t *ptr;

	/* SDMA trap event */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_SDMA0,
			      SDMA0_5_0__SRCID__SDMA_TRAP,
			      &adev->sdma.trap_irq);
	if (r)
		return r;

	/* SDMA trap event */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_SDMA1,
			      SDMA1_5_0__SRCID__SDMA_TRAP,
			      &adev->sdma.trap_irq);
	if (r)
		return r;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		ring->ring_obj = NULL;
		ring->use_doorbell = true;

		DRM_DEBUG("SDMA %d use_doorbell being set to: [%s]\n", i,
				ring->use_doorbell?"true":"false");

		ring->doorbell_index = (i == 0) ?
			(adev->doorbell_index.sdma_engine[0] << 1) //get DWORD offset
			: (adev->doorbell_index.sdma_engine[1] << 1); // get DWORD offset

		ring->vm_hub = AMDGPU_GFXHUB(0);
		sprintf(ring->name, "sdma%d", i);
		r = amdgpu_ring_init(adev, ring, 1024, &adev->sdma.trap_irq,
				     (i == 0) ? AMDGPU_SDMA_IRQ_INSTANCE0 :
				     AMDGPU_SDMA_IRQ_INSTANCE1,
				     AMDGPU_RING_PRIO_DEFAULT, NULL);
		if (r)
			return r;
	}

	adev->sdma.supported_reset =
		amdgpu_get_soft_full_reset_mask(&adev->sdma.instance[0].ring);
	switch (amdgpu_ip_version(adev, SDMA0_HWIP, 0)) {
	case IP_VERSION(5, 0, 0):
	case IP_VERSION(5, 0, 2):
	case IP_VERSION(5, 0, 5):
		if (adev->sdma.instance[0].fw_version >= 35)
			adev->sdma.supported_reset |= AMDGPU_RESET_TYPE_PER_QUEUE;
		break;
	default:
		break;
	}

	/* Allocate memory for SDMA IP Dump buffer */
	ptr = kcalloc(adev->sdma.num_instances * reg_count, sizeof(uint32_t), GFP_KERNEL);
	if (ptr)
		adev->sdma.ip_dump = ptr;
	else
		DRM_ERROR("Failed to allocated memory for SDMA IP Dump\n");

	r = amdgpu_sdma_sysfs_reset_mask_init(adev);
	if (r)
		return r;

	return r;
}

static int sdma_v5_0_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		amdgpu_ring_fini(&adev->sdma.instance[i].ring);

	amdgpu_sdma_sysfs_reset_mask_fini(adev);
	amdgpu_sdma_destroy_inst_ctx(adev, false);

	kfree(adev->sdma.ip_dump);

	return 0;
}

static int sdma_v5_0_hw_init(struct amdgpu_ip_block *ip_block)
{
	int r;
	struct amdgpu_device *adev = ip_block->adev;

	sdma_v5_0_init_golden_registers(adev);

	r = sdma_v5_0_start(adev);

	return r;
}

static int sdma_v5_0_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	if (amdgpu_sriov_vf(adev))
		return 0;

	sdma_v5_0_ctx_switch_enable(adev, false);
	sdma_v5_0_enable(adev, false);

	return 0;
}

static int sdma_v5_0_suspend(struct amdgpu_ip_block *ip_block)
{
	return sdma_v5_0_hw_fini(ip_block);
}

static int sdma_v5_0_resume(struct amdgpu_ip_block *ip_block)
{
	return sdma_v5_0_hw_init(ip_block);
}

static bool sdma_v5_0_is_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	u32 i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		u32 tmp = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_STATUS_REG));

		if (!(tmp & SDMA0_STATUS_REG__IDLE_MASK))
			return false;
	}

	return true;
}

static int sdma_v5_0_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	unsigned i;
	u32 sdma0, sdma1;
	struct amdgpu_device *adev = ip_block->adev;

	for (i = 0; i < adev->usec_timeout; i++) {
		sdma0 = RREG32(sdma_v5_0_get_reg_offset(adev, 0, mmSDMA0_STATUS_REG));
		sdma1 = RREG32(sdma_v5_0_get_reg_offset(adev, 1, mmSDMA0_STATUS_REG));

		if (sdma0 & sdma1 & SDMA0_STATUS_REG__IDLE_MASK)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int sdma_v5_0_soft_reset(struct amdgpu_ip_block *ip_block)
{
	/* todo */

	return 0;
}

static int sdma_v5_0_reset_queue(struct amdgpu_ring *ring, unsigned int vmid)
{
	struct amdgpu_device *adev = ring->adev;
	int i, j, r;
	u32 rb_cntl, ib_cntl, f32_cntl, freeze, cntl, preempt, soft_reset, stat1_reg;

	if (amdgpu_sriov_vf(adev))
		return -EINVAL;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (ring == &adev->sdma.instance[i].ring)
			break;
	}

	if (i == adev->sdma.num_instances) {
		DRM_ERROR("sdma instance not found\n");
		return -EINVAL;
	}

	amdgpu_gfx_rlc_enter_safe_mode(adev, 0);

	/* stop queue */
	ib_cntl = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_IB_CNTL));
	ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_ENABLE, 0);
	WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_IB_CNTL), ib_cntl);

	rb_cntl = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_CNTL));
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_ENABLE, 0);
	WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_RB_CNTL), rb_cntl);

	/* engine stop SDMA1_F32_CNTL.HALT to 1 and SDMAx_FREEZE freeze bit to 1 */
	freeze = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_FREEZE));
	freeze = REG_SET_FIELD(freeze, SDMA0_FREEZE, FREEZE, 1);
	WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_FREEZE), freeze);

	for (j = 0; j < adev->usec_timeout; j++) {
		freeze = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_FREEZE));
		if (REG_GET_FIELD(freeze, SDMA0_FREEZE, FROZEN) & 1)
			break;
		udelay(1);
	}

	/* check sdma copy engine all idle if frozen not received*/
	if (j == adev->usec_timeout) {
		stat1_reg = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_STATUS1_REG));
		if ((stat1_reg & 0x3FF) != 0x3FF) {
			DRM_ERROR("cannot soft reset as sdma not idle\n");
			r = -ETIMEDOUT;
			goto err0;
		}
	}

	f32_cntl = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_F32_CNTL));
	f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_F32_CNTL, HALT, 1);
	WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_F32_CNTL), f32_cntl);

	cntl = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_CNTL));
	cntl = REG_SET_FIELD(cntl, SDMA0_CNTL, UTC_L1_ENABLE, 0);
	WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_CNTL), cntl);

	/* soft reset SDMA_GFX_PREEMPT.IB_PREEMPT = 0 mmGRBM_SOFT_RESET.SOFT_RESET_SDMA0/1 = 1 */
	preempt = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_PREEMPT));
	preempt = REG_SET_FIELD(preempt, SDMA0_GFX_PREEMPT, IB_PREEMPT, 0);
	WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_GFX_PREEMPT), preempt);

	soft_reset = RREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET);
	soft_reset |= 1 << GRBM_SOFT_RESET__SOFT_RESET_SDMA0__SHIFT << i;

	WREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET, soft_reset);

	udelay(50);

	soft_reset &= ~(1 << GRBM_SOFT_RESET__SOFT_RESET_SDMA0__SHIFT << i);
	WREG32_SOC15(GC, 0, mmGRBM_SOFT_RESET, soft_reset);

	/* unfreeze*/
	freeze = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_FREEZE));
	freeze = REG_SET_FIELD(freeze, SDMA0_FREEZE, FREEZE, 0);
	WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_FREEZE), freeze);

	r = sdma_v5_0_gfx_resume_instance(adev, i, true);

err0:
	amdgpu_gfx_rlc_exit_safe_mode(adev, 0);
	return r;
}

static int sdma_v5_0_ring_preempt_ib(struct amdgpu_ring *ring)
{
	int i, r = 0;
	struct amdgpu_device *adev = ring->adev;
	u32 index = 0;
	u64 sdma_gfx_preempt;

	amdgpu_sdma_get_index_from_ring(ring, &index);
	if (index == 0)
		sdma_gfx_preempt = mmSDMA0_GFX_PREEMPT;
	else
		sdma_gfx_preempt = mmSDMA1_GFX_PREEMPT;

	/* assert preemption condition */
	amdgpu_ring_set_preempt_cond_exec(ring, false);

	/* emit the trailing fence */
	ring->trail_seq += 1;
	amdgpu_ring_alloc(ring, 10);
	sdma_v5_0_ring_emit_fence(ring, ring->trail_fence_gpu_addr,
				  ring->trail_seq, 0);
	amdgpu_ring_commit(ring);

	/* assert IB preemption */
	WREG32(sdma_gfx_preempt, 1);

	/* poll the trailing fence */
	for (i = 0; i < adev->usec_timeout; i++) {
		if (ring->trail_seq ==
		    le32_to_cpu(*(ring->trail_fence_cpu_addr)))
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout) {
		r = -EINVAL;
		DRM_ERROR("ring %d failed to be preempted\n", ring->idx);
	}

	/* deassert IB preemption */
	WREG32(sdma_gfx_preempt, 0);

	/* deassert the preemption condition */
	amdgpu_ring_set_preempt_cond_exec(ring, true);
	return r;
}

static int sdma_v5_0_set_trap_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	u32 sdma_cntl;

	if (!amdgpu_sriov_vf(adev)) {
		u32 reg_offset = (type == AMDGPU_SDMA_IRQ_INSTANCE0) ?
			sdma_v5_0_get_reg_offset(adev, 0, mmSDMA0_CNTL) :
			sdma_v5_0_get_reg_offset(adev, 1, mmSDMA0_CNTL);

		sdma_cntl = RREG32(reg_offset);
		sdma_cntl = REG_SET_FIELD(sdma_cntl, SDMA0_CNTL, TRAP_ENABLE,
					  state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
		WREG32(reg_offset, sdma_cntl);
	}

	return 0;
}

static int sdma_v5_0_process_trap_irq(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	uint32_t mes_queue_id = entry->src_data[0];

	DRM_DEBUG("IH: SDMA trap\n");

	if (adev->enable_mes && (mes_queue_id & AMDGPU_FENCE_MES_QUEUE_FLAG)) {
		struct amdgpu_mes_queue *queue;

		mes_queue_id &= AMDGPU_FENCE_MES_QUEUE_ID_MASK;

		spin_lock(&adev->mes.queue_id_lock);
		queue = idr_find(&adev->mes.queue_id_idr, mes_queue_id);
		if (queue) {
			DRM_DEBUG("process smda queue id = %d\n", mes_queue_id);
			amdgpu_fence_process(queue->ring);
		}
		spin_unlock(&adev->mes.queue_id_lock);
		return 0;
	}

	switch (entry->client_id) {
	case SOC15_IH_CLIENTID_SDMA0:
		switch (entry->ring_id) {
		case 0:
			amdgpu_fence_process(&adev->sdma.instance[0].ring);
			break;
		case 1:
			/* XXX compute */
			break;
		case 2:
			/* XXX compute */
			break;
		case 3:
			/* XXX page queue*/
			break;
		}
		break;
	case SOC15_IH_CLIENTID_SDMA1:
		switch (entry->ring_id) {
		case 0:
			amdgpu_fence_process(&adev->sdma.instance[1].ring);
			break;
		case 1:
			/* XXX compute */
			break;
		case 2:
			/* XXX compute */
			break;
		case 3:
			/* XXX page queue*/
			break;
		}
		break;
	}
	return 0;
}

static int sdma_v5_0_process_illegal_inst_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	return 0;
}

static void sdma_v5_0_update_medium_grain_clock_gating(struct amdgpu_device *adev,
						       bool enable)
{
	uint32_t data, def;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (enable && (adev->cg_flags & AMD_CG_SUPPORT_SDMA_MGCG)) {
			/* Enable sdma clock gating */
			def = data = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_CLK_CTRL));
			data &= ~(SDMA0_CLK_CTRL__SOFT_OVERRIDE7_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE6_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE5_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE4_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE3_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE2_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE1_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE0_MASK);
			if (def != data)
				WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_CLK_CTRL), data);
		} else {
			/* Disable sdma clock gating */
			def = data = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_CLK_CTRL));
			data |= (SDMA0_CLK_CTRL__SOFT_OVERRIDE7_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE6_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE5_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE4_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE3_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE2_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE1_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE0_MASK);
			if (def != data)
				WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_CLK_CTRL), data);
		}
	}
}

static void sdma_v5_0_update_medium_grain_light_sleep(struct amdgpu_device *adev,
						      bool enable)
{
	uint32_t data, def;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (enable && (adev->cg_flags & AMD_CG_SUPPORT_SDMA_LS)) {
			/* Enable sdma mem light sleep */
			def = data = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_POWER_CNTL));
			data |= SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;
			if (def != data)
				WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_POWER_CNTL), data);

		} else {
			/* Disable sdma mem light sleep */
			def = data = RREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_POWER_CNTL));
			data &= ~SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;
			if (def != data)
				WREG32(sdma_v5_0_get_reg_offset(adev, i, mmSDMA0_POWER_CNTL), data);

		}
	}
}

static int sdma_v5_0_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					   enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;

	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (amdgpu_ip_version(adev, SDMA0_HWIP, 0)) {
	case IP_VERSION(5, 0, 0):
	case IP_VERSION(5, 0, 2):
	case IP_VERSION(5, 0, 5):
		sdma_v5_0_update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		sdma_v5_0_update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		break;
	default:
		break;
	}

	return 0;
}

static int sdma_v5_0_set_powergating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_powergating_state state)
{
	return 0;
}

static void sdma_v5_0_get_clockgating_state(struct amdgpu_ip_block *ip_block, u64 *flags)
{
	struct amdgpu_device *adev = ip_block->adev;
	int data;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	/* AMD_CG_SUPPORT_SDMA_MGCG */
	data = RREG32(sdma_v5_0_get_reg_offset(adev, 0, mmSDMA0_CLK_CTRL));
	if (!(data & SDMA0_CLK_CTRL__SOFT_OVERRIDE7_MASK))
		*flags |= AMD_CG_SUPPORT_SDMA_MGCG;

	/* AMD_CG_SUPPORT_SDMA_LS */
	data = RREG32(sdma_v5_0_get_reg_offset(adev, 0, mmSDMA0_POWER_CNTL));
	if (data & SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK)
		*flags |= AMD_CG_SUPPORT_SDMA_LS;
}

static void sdma_v5_0_print_ip_state(struct amdgpu_ip_block *ip_block, struct drm_printer *p)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, j;
	uint32_t reg_count = ARRAY_SIZE(sdma_reg_list_5_0);
	uint32_t instance_offset;

	if (!adev->sdma.ip_dump)
		return;

	drm_printf(p, "num_instances:%d\n", adev->sdma.num_instances);
	for (i = 0; i < adev->sdma.num_instances; i++) {
		instance_offset = i * reg_count;
		drm_printf(p, "\nInstance:%d\n", i);

		for (j = 0; j < reg_count; j++)
			drm_printf(p, "%-50s \t 0x%08x\n", sdma_reg_list_5_0[j].reg_name,
				   adev->sdma.ip_dump[instance_offset + j]);
	}
}

static void sdma_v5_0_dump_ip_state(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, j;
	uint32_t instance_offset;
	uint32_t reg_count = ARRAY_SIZE(sdma_reg_list_5_0);

	if (!adev->sdma.ip_dump)
		return;

	amdgpu_gfx_off_ctrl(adev, false);
	for (i = 0; i < adev->sdma.num_instances; i++) {
		instance_offset = i * reg_count;
		for (j = 0; j < reg_count; j++)
			adev->sdma.ip_dump[instance_offset + j] =
				RREG32(sdma_v5_0_get_reg_offset(adev, i,
				       sdma_reg_list_5_0[j].reg_offset));
	}
	amdgpu_gfx_off_ctrl(adev, true);
}

static const struct amd_ip_funcs sdma_v5_0_ip_funcs = {
	.name = "sdma_v5_0",
	.early_init = sdma_v5_0_early_init,
	.sw_init = sdma_v5_0_sw_init,
	.sw_fini = sdma_v5_0_sw_fini,
	.hw_init = sdma_v5_0_hw_init,
	.hw_fini = sdma_v5_0_hw_fini,
	.suspend = sdma_v5_0_suspend,
	.resume = sdma_v5_0_resume,
	.is_idle = sdma_v5_0_is_idle,
	.wait_for_idle = sdma_v5_0_wait_for_idle,
	.soft_reset = sdma_v5_0_soft_reset,
	.set_clockgating_state = sdma_v5_0_set_clockgating_state,
	.set_powergating_state = sdma_v5_0_set_powergating_state,
	.get_clockgating_state = sdma_v5_0_get_clockgating_state,
	.dump_ip_state = sdma_v5_0_dump_ip_state,
	.print_ip_state = sdma_v5_0_print_ip_state,
};

static const struct amdgpu_ring_funcs sdma_v5_0_ring_funcs = {
	.type = AMDGPU_RING_TYPE_SDMA,
	.align_mask = 0xf,
	.nop = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP),
	.support_64bit_ptrs = true,
	.secure_submission_supported = true,
	.get_rptr = sdma_v5_0_ring_get_rptr,
	.get_wptr = sdma_v5_0_ring_get_wptr,
	.set_wptr = sdma_v5_0_ring_set_wptr,
	.emit_frame_size =
		5 + /* sdma_v5_0_ring_init_cond_exec */
		6 + /* sdma_v5_0_ring_emit_hdp_flush */
		3 + /* hdp_invalidate */
		6 + /* sdma_v5_0_ring_emit_pipeline_sync */
		/* sdma_v5_0_ring_emit_vm_flush */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 6 * 2 +
		10 + 10 + 10, /* sdma_v5_0_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 5 + 7 + 6, /* sdma_v5_0_ring_emit_ib */
	.emit_ib = sdma_v5_0_ring_emit_ib,
	.emit_mem_sync = sdma_v5_0_ring_emit_mem_sync,
	.emit_fence = sdma_v5_0_ring_emit_fence,
	.emit_pipeline_sync = sdma_v5_0_ring_emit_pipeline_sync,
	.emit_vm_flush = sdma_v5_0_ring_emit_vm_flush,
	.emit_hdp_flush = sdma_v5_0_ring_emit_hdp_flush,
	.test_ring = sdma_v5_0_ring_test_ring,
	.test_ib = sdma_v5_0_ring_test_ib,
	.insert_nop = sdma_v5_0_ring_insert_nop,
	.pad_ib = sdma_v5_0_ring_pad_ib,
	.emit_wreg = sdma_v5_0_ring_emit_wreg,
	.emit_reg_wait = sdma_v5_0_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = sdma_v5_0_ring_emit_reg_write_reg_wait,
	.init_cond_exec = sdma_v5_0_ring_init_cond_exec,
	.preempt_ib = sdma_v5_0_ring_preempt_ib,
	.reset = sdma_v5_0_reset_queue,
};

static void sdma_v5_0_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		adev->sdma.instance[i].ring.funcs = &sdma_v5_0_ring_funcs;
		adev->sdma.instance[i].ring.me = i;
	}
}

static const struct amdgpu_irq_src_funcs sdma_v5_0_trap_irq_funcs = {
	.set = sdma_v5_0_set_trap_irq_state,
	.process = sdma_v5_0_process_trap_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v5_0_illegal_inst_irq_funcs = {
	.process = sdma_v5_0_process_illegal_inst_irq,
};

static void sdma_v5_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->sdma.trap_irq.num_types = AMDGPU_SDMA_IRQ_INSTANCE0 +
					adev->sdma.num_instances;
	adev->sdma.trap_irq.funcs = &sdma_v5_0_trap_irq_funcs;
	adev->sdma.illegal_inst_irq.funcs = &sdma_v5_0_illegal_inst_irq_funcs;
}

/**
 * sdma_v5_0_emit_copy_buffer - copy buffer using the sDMA engine
 *
 * @ib: indirect buffer to copy to
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 * @copy_flags: copy flags for the buffers
 *
 * Copy GPU buffers using the DMA engine (NAVI10).
 * Used by the amdgpu ttm implementation to move pages if
 * registered as the asic copy callback.
 */
static void sdma_v5_0_emit_copy_buffer(struct amdgpu_ib *ib,
				       uint64_t src_offset,
				       uint64_t dst_offset,
				       uint32_t byte_count,
				       uint32_t copy_flags)
{
	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_COPY) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_COPY_LINEAR) |
		SDMA_PKT_COPY_LINEAR_HEADER_TMZ((copy_flags & AMDGPU_COPY_FLAGS_TMZ) ? 1 : 0);
	ib->ptr[ib->length_dw++] = byte_count - 1;
	ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
	ib->ptr[ib->length_dw++] = lower_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset);
}

/**
 * sdma_v5_0_emit_fill_buffer - fill buffer using the sDMA engine
 *
 * @ib: indirect buffer to fill
 * @src_data: value to write to buffer
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Fill GPU buffers using the DMA engine (NAVI10).
 */
static void sdma_v5_0_emit_fill_buffer(struct amdgpu_ib *ib,
				       uint32_t src_data,
				       uint64_t dst_offset,
				       uint32_t byte_count)
{
	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_CONST_FILL);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = src_data;
	ib->ptr[ib->length_dw++] = byte_count - 1;
}

static const struct amdgpu_buffer_funcs sdma_v5_0_buffer_funcs = {
	.copy_max_bytes = 0x400000,
	.copy_num_dw = 7,
	.emit_copy_buffer = sdma_v5_0_emit_copy_buffer,

	.fill_max_bytes = 0x400000,
	.fill_num_dw = 5,
	.emit_fill_buffer = sdma_v5_0_emit_fill_buffer,
};

static void sdma_v5_0_set_buffer_funcs(struct amdgpu_device *adev)
{
	if (adev->mman.buffer_funcs == NULL) {
		adev->mman.buffer_funcs = &sdma_v5_0_buffer_funcs;
		adev->mman.buffer_funcs_ring = &adev->sdma.instance[0].ring;
	}
}

static const struct amdgpu_vm_pte_funcs sdma_v5_0_vm_pte_funcs = {
	.copy_pte_num_dw = 7,
	.copy_pte = sdma_v5_0_vm_copy_pte,
	.write_pte = sdma_v5_0_vm_write_pte,
	.set_pte_pde = sdma_v5_0_vm_set_pte_pde,
};

static void sdma_v5_0_set_vm_pte_funcs(struct amdgpu_device *adev)
{
	unsigned i;

	if (adev->vm_manager.vm_pte_funcs == NULL) {
		adev->vm_manager.vm_pte_funcs = &sdma_v5_0_vm_pte_funcs;
		for (i = 0; i < adev->sdma.num_instances; i++) {
			adev->vm_manager.vm_pte_scheds[i] =
				&adev->sdma.instance[i].ring.sched;
		}
		adev->vm_manager.vm_pte_num_scheds = adev->sdma.num_instances;
	}
}

const struct amdgpu_ip_block_version sdma_v5_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_SDMA,
	.major = 5,
	.minor = 0,
	.rev = 0,
	.funcs = &sdma_v5_0_ip_funcs,
};
