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
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_ucode.h"
#include "amdgpu_trace.h"

#include "sdma0/sdma0_4_2_offset.h"
#include "sdma0/sdma0_4_2_sh_mask.h"
#include "sdma1/sdma1_4_2_offset.h"
#include "sdma1/sdma1_4_2_sh_mask.h"
#include "sdma2/sdma2_4_2_2_offset.h"
#include "sdma2/sdma2_4_2_2_sh_mask.h"
#include "sdma3/sdma3_4_2_2_offset.h"
#include "sdma3/sdma3_4_2_2_sh_mask.h"
#include "sdma4/sdma4_4_2_2_offset.h"
#include "sdma4/sdma4_4_2_2_sh_mask.h"
#include "sdma5/sdma5_4_2_2_offset.h"
#include "sdma5/sdma5_4_2_2_sh_mask.h"
#include "sdma6/sdma6_4_2_2_offset.h"
#include "sdma6/sdma6_4_2_2_sh_mask.h"
#include "sdma7/sdma7_4_2_2_offset.h"
#include "sdma7/sdma7_4_2_2_sh_mask.h"
#include "sdma0/sdma0_4_1_default.h"

#include "soc15_common.h"
#include "soc15.h"
#include "vega10_sdma_pkt_open.h"

#include "ivsrcid/sdma0/irqsrcs_sdma0_4_0.h"
#include "ivsrcid/sdma1/irqsrcs_sdma1_4_0.h"

#include "amdgpu_ras.h"
#include "sdma_v4_4.h"

MODULE_FIRMWARE("amdgpu/vega10_sdma.bin");
MODULE_FIRMWARE("amdgpu/vega10_sdma1.bin");
MODULE_FIRMWARE("amdgpu/vega12_sdma.bin");
MODULE_FIRMWARE("amdgpu/vega12_sdma1.bin");
MODULE_FIRMWARE("amdgpu/vega20_sdma.bin");
MODULE_FIRMWARE("amdgpu/vega20_sdma1.bin");
MODULE_FIRMWARE("amdgpu/raven_sdma.bin");
MODULE_FIRMWARE("amdgpu/picasso_sdma.bin");
MODULE_FIRMWARE("amdgpu/raven2_sdma.bin");
MODULE_FIRMWARE("amdgpu/arcturus_sdma.bin");
MODULE_FIRMWARE("amdgpu/renoir_sdma.bin");
MODULE_FIRMWARE("amdgpu/green_sardine_sdma.bin");
MODULE_FIRMWARE("amdgpu/aldebaran_sdma.bin");

#define SDMA0_POWER_CNTL__ON_OFF_CONDITION_HOLD_TIME_MASK  0x000000F8L
#define SDMA0_POWER_CNTL__ON_OFF_STATUS_DURATION_TIME_MASK 0xFC000000L

#define WREG32_SDMA(instance, offset, value) \
	WREG32(sdma_v4_0_get_reg_offset(adev, (instance), (offset)), value)
#define RREG32_SDMA(instance, offset) \
	RREG32(sdma_v4_0_get_reg_offset(adev, (instance), (offset)))

static void sdma_v4_0_set_ring_funcs(struct amdgpu_device *adev);
static void sdma_v4_0_set_buffer_funcs(struct amdgpu_device *adev);
static void sdma_v4_0_set_vm_pte_funcs(struct amdgpu_device *adev);
static void sdma_v4_0_set_irq_funcs(struct amdgpu_device *adev);
static void sdma_v4_0_set_ras_funcs(struct amdgpu_device *adev);

static const struct soc15_reg_golden golden_settings_sdma_4[] = {
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_CHICKEN_BITS, 0xfe931f07, 0x02831d07),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_CLK_CTRL, 0xff000ff0, 0x3f000100),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GFX_IB_CNTL, 0x800f0100, 0x00000100),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GFX_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_PAGE_IB_CNTL, 0x800f0100, 0x00000100),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_PAGE_RB_WPTR_POLL_CNTL, 0x0000fff0, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_POWER_CNTL, 0x003ff006, 0x0003c000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC0_IB_CNTL, 0x800f0100, 0x00000100),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC0_RB_WPTR_POLL_CNTL, 0x0000fff0, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC1_IB_CNTL, 0x800f0100, 0x00000100),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC1_RB_WPTR_POLL_CNTL, 0x0000fff0, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_UTCL1_PAGE, 0x000003ff, 0x000003c0),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_UTCL1_WATERMK, 0xfc000000, 0x00000000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_CLK_CTRL, 0xffffffff, 0x3f000100),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GFX_IB_CNTL, 0x800f0100, 0x00000100),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GFX_RB_WPTR_POLL_CNTL, 0x0000fff0, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_PAGE_IB_CNTL, 0x800f0100, 0x00000100),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_PAGE_RB_WPTR_POLL_CNTL, 0x0000fff0, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_POWER_CNTL, 0x003ff000, 0x0003c000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC0_IB_CNTL, 0x800f0100, 0x00000100),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC0_RB_WPTR_POLL_CNTL, 0x0000fff0, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC1_IB_CNTL, 0x800f0100, 0x00000100),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC1_RB_WPTR_POLL_CNTL, 0x0000fff0, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_UTCL1_PAGE, 0x000003ff, 0x000003c0),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_UTCL1_WATERMK, 0xfc000000, 0x00000000)
};

static const struct soc15_reg_golden golden_settings_sdma_vg10[] = {
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG_READ, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_CHICKEN_BITS, 0xfe931f07, 0x02831d07),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GB_ADDR_CONFIG, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GB_ADDR_CONFIG_READ, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
};

static const struct soc15_reg_golden golden_settings_sdma_vg12[] = {
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG, 0x0018773f, 0x00104001),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG_READ, 0x0018773f, 0x00104001),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_CHICKEN_BITS, 0xfe931f07, 0x02831d07),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GB_ADDR_CONFIG, 0x0018773f, 0x00104001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GB_ADDR_CONFIG_READ, 0x0018773f, 0x00104001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
};

static const struct soc15_reg_golden golden_settings_sdma_4_1[] = {
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_CHICKEN_BITS, 0xfe931f07, 0x02831d07),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_CLK_CTRL, 0xffffffff, 0x3f000100),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GFX_IB_CNTL, 0x800f0111, 0x00000100),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GFX_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_POWER_CNTL, 0xfc3fffff, 0x40000051),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC0_IB_CNTL, 0x800f0111, 0x00000100),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC0_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC1_IB_CNTL, 0x800f0111, 0x00000100),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC1_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_UTCL1_PAGE, 0x000003ff, 0x000003e0),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_UTCL1_WATERMK, 0xfc000000, 0x00000000)
};

static const struct soc15_reg_golden golden_settings_sdma0_4_2_init[] = {
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC0_RB_WPTR_POLL_CNTL, 0xfffffff0, 0x00403000),
};

static const struct soc15_reg_golden golden_settings_sdma0_4_2[] =
{
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_CHICKEN_BITS, 0xfe931f07, 0x02831f07),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_CLK_CTRL, 0xffffffff, 0x3f000100),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG_READ, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GFX_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GFX_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_PAGE_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_PAGE_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RD_BURST_CNTL, 0x0000000f, 0x00000003),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC0_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC0_RB_WPTR_POLL_CNTL, 0xfffffff0, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC1_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC1_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC2_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC2_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC3_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC3_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC4_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC4_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC5_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC5_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC6_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC6_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC7_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC7_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_UTCL1_PAGE, 0x000003ff, 0x000003c0),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
};

static const struct soc15_reg_golden golden_settings_sdma1_4_2[] = {
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_CHICKEN_BITS, 0xfe931f07, 0x02831f07),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_CLK_CTRL, 0xffffffff, 0x3f000100),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GB_ADDR_CONFIG, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GB_ADDR_CONFIG_READ, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GFX_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GFX_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_PAGE_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_PAGE_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RD_BURST_CNTL, 0x0000000f, 0x00000003),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC0_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC0_RB_WPTR_POLL_CNTL, 0xfffffff0, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC1_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC1_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC2_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC2_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC3_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC3_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC4_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC4_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC5_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC5_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC6_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC6_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC7_RB_RPTR_ADDR_LO, 0xfffffffd, 0x00000001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_RLC7_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_UTCL1_PAGE, 0x000003ff, 0x000003c0),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
};

static const struct soc15_reg_golden golden_settings_sdma_rv1[] =
{
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG, 0x0018773f, 0x00000002),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG_READ, 0x0018773f, 0x00000002)
};

static const struct soc15_reg_golden golden_settings_sdma_rv2[] =
{
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG, 0x0018773f, 0x00003001),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG_READ, 0x0018773f, 0x00003001)
};

static const struct soc15_reg_golden golden_settings_sdma_arct[] =
{
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_CHICKEN_BITS, 0xfe931f07, 0x02831f07),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG_READ, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_CHICKEN_BITS, 0xfe931f07, 0x02831f07),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GB_ADDR_CONFIG, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GB_ADDR_CONFIG_READ, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
	SOC15_REG_GOLDEN_VALUE(SDMA2, 0, mmSDMA2_CHICKEN_BITS, 0xfe931f07, 0x02831f07),
	SOC15_REG_GOLDEN_VALUE(SDMA2, 0, mmSDMA2_GB_ADDR_CONFIG, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA2, 0, mmSDMA2_GB_ADDR_CONFIG_READ, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA2, 0, mmSDMA2_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
	SOC15_REG_GOLDEN_VALUE(SDMA3, 0, mmSDMA3_CHICKEN_BITS, 0xfe931f07, 0x02831f07),
	SOC15_REG_GOLDEN_VALUE(SDMA3, 0, mmSDMA3_GB_ADDR_CONFIG, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA3, 0, mmSDMA3_GB_ADDR_CONFIG_READ, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA3, 0, mmSDMA3_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
	SOC15_REG_GOLDEN_VALUE(SDMA4, 0, mmSDMA4_CHICKEN_BITS, 0xfe931f07, 0x02831f07),
	SOC15_REG_GOLDEN_VALUE(SDMA4, 0, mmSDMA4_GB_ADDR_CONFIG, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA4, 0, mmSDMA4_GB_ADDR_CONFIG_READ, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA4, 0, mmSDMA4_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
	SOC15_REG_GOLDEN_VALUE(SDMA5, 0, mmSDMA5_CHICKEN_BITS, 0xfe931f07, 0x02831f07),
	SOC15_REG_GOLDEN_VALUE(SDMA5, 0, mmSDMA5_GB_ADDR_CONFIG, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA5, 0, mmSDMA5_GB_ADDR_CONFIG_READ, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA5, 0, mmSDMA5_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
	SOC15_REG_GOLDEN_VALUE(SDMA6, 0, mmSDMA6_CHICKEN_BITS, 0xfe931f07, 0x02831f07),
	SOC15_REG_GOLDEN_VALUE(SDMA6, 0, mmSDMA6_GB_ADDR_CONFIG, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA6, 0, mmSDMA6_GB_ADDR_CONFIG_READ, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA6, 0, mmSDMA6_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
	SOC15_REG_GOLDEN_VALUE(SDMA7, 0, mmSDMA7_CHICKEN_BITS, 0xfe931f07, 0x02831f07),
	SOC15_REG_GOLDEN_VALUE(SDMA7, 0, mmSDMA7_GB_ADDR_CONFIG, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA7, 0, mmSDMA7_GB_ADDR_CONFIG_READ, 0x0000773f, 0x00004002),
	SOC15_REG_GOLDEN_VALUE(SDMA7, 0, mmSDMA7_UTCL1_TIMEOUT, 0xffffffff, 0x00010001)
};

static const struct soc15_reg_golden golden_settings_sdma_aldebaran[] = {
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG_READ, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GB_ADDR_CONFIG, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_GB_ADDR_CONFIG_READ, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA1, 0, mmSDMA1_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
	SOC15_REG_GOLDEN_VALUE(SDMA2, 0, mmSDMA2_GB_ADDR_CONFIG, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA2, 0, mmSDMA2_GB_ADDR_CONFIG_READ, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA3, 0, mmSDMA2_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
	SOC15_REG_GOLDEN_VALUE(SDMA3, 0, mmSDMA3_GB_ADDR_CONFIG, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA3, 0, mmSDMA3_GB_ADDR_CONFIG_READ, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA3, 0, mmSDMA3_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
	SOC15_REG_GOLDEN_VALUE(SDMA4, 0, mmSDMA4_GB_ADDR_CONFIG, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA4, 0, mmSDMA4_GB_ADDR_CONFIG_READ, 0x0018773f, 0x00104002),
	SOC15_REG_GOLDEN_VALUE(SDMA4, 0, mmSDMA4_UTCL1_TIMEOUT, 0xffffffff, 0x00010001),
};

static const struct soc15_reg_golden golden_settings_sdma_4_3[] = {
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_CHICKEN_BITS, 0xfe931f07, 0x02831f07),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_CLK_CTRL, 0xffffffff, 0x3f000100),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG, 0x0018773f, 0x00000002),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GB_ADDR_CONFIG_READ, 0x0018773f, 0x00000002),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_GFX_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_POWER_CNTL, 0x003fff07, 0x40000051),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC0_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_RLC1_RB_WPTR_POLL_CNTL, 0xfffffff7, 0x00403000),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_UTCL1_PAGE, 0x000003ff, 0x000003e0),
	SOC15_REG_GOLDEN_VALUE(SDMA0, 0, mmSDMA0_UTCL1_WATERMK, 0xfc000000, 0x03fbe1fe)
};

static const struct soc15_ras_field_entry sdma_v4_0_ras_fields[] = {
	{ "SDMA_UCODE_BUF_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_UCODE_BUF_SED),
	0, 0,
	},
	{ "SDMA_RB_CMD_BUF_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_RB_CMD_BUF_SED),
	0, 0,
	},
	{ "SDMA_IB_CMD_BUF_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_IB_CMD_BUF_SED),
	0, 0,
	},
	{ "SDMA_UTCL1_RD_FIFO_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_UTCL1_RD_FIFO_SED),
	0, 0,
	},
	{ "SDMA_UTCL1_RDBST_FIFO_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_UTCL1_RDBST_FIFO_SED),
	0, 0,
	},
	{ "SDMA_DATA_LUT_FIFO_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_DATA_LUT_FIFO_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF0_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF0_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF1_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF1_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF2_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF2_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF3_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF3_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF4_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF4_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF5_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF5_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF6_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF6_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF7_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF7_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF8_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF8_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF9_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF9_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF10_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF10_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF11_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF11_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF12_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF12_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF13_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF13_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF14_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF14_SED),
	0, 0,
	},
	{ "SDMA_MBANK_DATA_BUF15_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MBANK_DATA_BUF15_SED),
	0, 0,
	},
	{ "SDMA_SPLIT_DAT_BUF_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_SPLIT_DAT_BUF_SED),
	0, 0,
	},
	{ "SDMA_MC_WR_ADDR_FIFO_SED", SOC15_REG_ENTRY(SDMA0, 0, mmSDMA0_EDC_COUNTER),
	SOC15_REG_FIELD(SDMA0_EDC_COUNTER, SDMA_MC_WR_ADDR_FIFO_SED),
	0, 0,
	},
};

static u32 sdma_v4_0_get_reg_offset(struct amdgpu_device *adev,
		u32 instance, u32 offset)
{
	switch (instance) {
	case 0:
		return (adev->reg_offset[SDMA0_HWIP][0][0] + offset);
	case 1:
		return (adev->reg_offset[SDMA1_HWIP][0][0] + offset);
	case 2:
		return (adev->reg_offset[SDMA2_HWIP][0][1] + offset);
	case 3:
		return (adev->reg_offset[SDMA3_HWIP][0][1] + offset);
	case 4:
		return (adev->reg_offset[SDMA4_HWIP][0][1] + offset);
	case 5:
		return (adev->reg_offset[SDMA5_HWIP][0][1] + offset);
	case 6:
		return (adev->reg_offset[SDMA6_HWIP][0][1] + offset);
	case 7:
		return (adev->reg_offset[SDMA7_HWIP][0][1] + offset);
	default:
		break;
	}
	return 0;
}

static unsigned sdma_v4_0_seq_to_irq_id(int seq_num)
{
	switch (seq_num) {
	case 0:
		return SOC15_IH_CLIENTID_SDMA0;
	case 1:
		return SOC15_IH_CLIENTID_SDMA1;
	case 2:
		return SOC15_IH_CLIENTID_SDMA2;
	case 3:
		return SOC15_IH_CLIENTID_SDMA3;
	case 4:
		return SOC15_IH_CLIENTID_SDMA4;
	case 5:
		return SOC15_IH_CLIENTID_SDMA5;
	case 6:
		return SOC15_IH_CLIENTID_SDMA6;
	case 7:
		return SOC15_IH_CLIENTID_SDMA7;
	default:
		break;
	}
	return -EINVAL;
}

static int sdma_v4_0_irq_id_to_seq(unsigned client_id)
{
	switch (client_id) {
	case SOC15_IH_CLIENTID_SDMA0:
		return 0;
	case SOC15_IH_CLIENTID_SDMA1:
		return 1;
	case SOC15_IH_CLIENTID_SDMA2:
		return 2;
	case SOC15_IH_CLIENTID_SDMA3:
		return 3;
	case SOC15_IH_CLIENTID_SDMA4:
		return 4;
	case SOC15_IH_CLIENTID_SDMA5:
		return 5;
	case SOC15_IH_CLIENTID_SDMA6:
		return 6;
	case SOC15_IH_CLIENTID_SDMA7:
		return 7;
	default:
		break;
	}
	return -EINVAL;
}

static void sdma_v4_0_init_golden_registers(struct amdgpu_device *adev)
{
	switch (adev->ip_versions[SDMA0_HWIP][0]) {
	case IP_VERSION(4, 0, 0):
		soc15_program_register_sequence(adev,
						golden_settings_sdma_4,
						ARRAY_SIZE(golden_settings_sdma_4));
		soc15_program_register_sequence(adev,
						golden_settings_sdma_vg10,
						ARRAY_SIZE(golden_settings_sdma_vg10));
		break;
	case IP_VERSION(4, 0, 1):
		soc15_program_register_sequence(adev,
						golden_settings_sdma_4,
						ARRAY_SIZE(golden_settings_sdma_4));
		soc15_program_register_sequence(adev,
						golden_settings_sdma_vg12,
						ARRAY_SIZE(golden_settings_sdma_vg12));
		break;
	case IP_VERSION(4, 2, 0):
		soc15_program_register_sequence(adev,
						golden_settings_sdma0_4_2_init,
						ARRAY_SIZE(golden_settings_sdma0_4_2_init));
		soc15_program_register_sequence(adev,
						golden_settings_sdma0_4_2,
						ARRAY_SIZE(golden_settings_sdma0_4_2));
		soc15_program_register_sequence(adev,
						golden_settings_sdma1_4_2,
						ARRAY_SIZE(golden_settings_sdma1_4_2));
		break;
	case IP_VERSION(4, 2, 2):
		soc15_program_register_sequence(adev,
						golden_settings_sdma_arct,
						ARRAY_SIZE(golden_settings_sdma_arct));
		break;
	case IP_VERSION(4, 4, 0):
		soc15_program_register_sequence(adev,
						golden_settings_sdma_aldebaran,
						ARRAY_SIZE(golden_settings_sdma_aldebaran));
		break;
	case IP_VERSION(4, 1, 0):
	case IP_VERSION(4, 1, 1):
		soc15_program_register_sequence(adev,
						golden_settings_sdma_4_1,
						ARRAY_SIZE(golden_settings_sdma_4_1));
		if (adev->apu_flags & AMD_APU_IS_RAVEN2)
			soc15_program_register_sequence(adev,
							golden_settings_sdma_rv2,
							ARRAY_SIZE(golden_settings_sdma_rv2));
		else
			soc15_program_register_sequence(adev,
							golden_settings_sdma_rv1,
							ARRAY_SIZE(golden_settings_sdma_rv1));
		break;
	case IP_VERSION(4, 1, 2):
		soc15_program_register_sequence(adev,
						golden_settings_sdma_4_3,
						ARRAY_SIZE(golden_settings_sdma_4_3));
		break;
	default:
		break;
	}
}

static void sdma_v4_0_setup_ulv(struct amdgpu_device *adev)
{
	int i;

	/*
	 * The only chips with SDMAv4 and ULV are VG10 and VG20.
	 * Server SKUs take a different hysteresis setting from other SKUs.
	 */
	switch (adev->ip_versions[SDMA0_HWIP][0]) {
	case IP_VERSION(4, 0, 0):
		if (adev->pdev->device == 0x6860)
			break;
		return;
	case IP_VERSION(4, 2, 0):
		if (adev->pdev->device == 0x66a1)
			break;
		return;
	default:
		return;
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		uint32_t temp;

		temp = RREG32_SDMA(i, mmSDMA0_ULV_CNTL);
		temp = REG_SET_FIELD(temp, SDMA0_ULV_CNTL, HYSTERESIS, 0x0);
		WREG32_SDMA(i, mmSDMA0_ULV_CNTL, temp);
	}
}

static int sdma_v4_0_init_inst_ctx(struct amdgpu_sdma_instance *sdma_inst)
{
	int err = 0;
	const struct sdma_firmware_header_v1_0 *hdr;

	err = amdgpu_ucode_validate(sdma_inst->fw);
	if (err)
		return err;

	hdr = (const struct sdma_firmware_header_v1_0 *)sdma_inst->fw->data;
	sdma_inst->fw_version = le32_to_cpu(hdr->header.ucode_version);
	sdma_inst->feature_version = le32_to_cpu(hdr->ucode_feature_version);

	if (sdma_inst->feature_version >= 20)
		sdma_inst->burst_nop = true;

	return 0;
}

static void sdma_v4_0_destroy_inst_ctx(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		release_firmware(adev->sdma.instance[i].fw);
		adev->sdma.instance[i].fw = NULL;

		/* arcturus shares the same FW memory across
		   all SDMA isntances */
		if (adev->ip_versions[SDMA0_HWIP][0] == IP_VERSION(4, 2, 2) ||
		    adev->ip_versions[SDMA0_HWIP][0] == IP_VERSION(4, 4, 0))
			break;
	}

	memset((void *)adev->sdma.instance, 0,
		sizeof(struct amdgpu_sdma_instance) * AMDGPU_MAX_SDMA_INSTANCES);
}

/**
 * sdma_v4_0_init_microcode - load ucode images from disk
 *
 * @adev: amdgpu_device pointer
 *
 * Use the firmware interface to load the ucode images into
 * the driver (not loaded into hw).
 * Returns 0 on success, error on failure.
 */

// emulation only, won't work on real chip
// vega10 real chip need to use PSP to load firmware
static int sdma_v4_0_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[30];
	int err = 0, i;
	struct amdgpu_firmware_info *info = NULL;
	const struct common_firmware_header *header = NULL;

	DRM_DEBUG("\n");

	switch (adev->ip_versions[SDMA0_HWIP][0]) {
	case IP_VERSION(4, 0, 0):
		chip_name = "vega10";
		break;
	case IP_VERSION(4, 0, 1):
		chip_name = "vega12";
		break;
	case IP_VERSION(4, 2, 0):
		chip_name = "vega20";
		break;
	case IP_VERSION(4, 1, 0):
	case IP_VERSION(4, 1, 1):
		if (adev->apu_flags & AMD_APU_IS_RAVEN2)
			chip_name = "raven2";
		else if (adev->apu_flags & AMD_APU_IS_PICASSO)
			chip_name = "picasso";
		else
			chip_name = "raven";
		break;
	case IP_VERSION(4, 2, 2):
		chip_name = "arcturus";
		break;
	case IP_VERSION(4, 1, 2):
		if (adev->apu_flags & AMD_APU_IS_RENOIR)
			chip_name = "renoir";
		else
			chip_name = "green_sardine";
		break;
	case IP_VERSION(4, 4, 0):
		chip_name = "aldebaran";
		break;
	default:
		BUG();
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_sdma.bin", chip_name);

	err = request_firmware(&adev->sdma.instance[0].fw, fw_name, adev->dev);
	if (err)
		goto out;

	err = sdma_v4_0_init_inst_ctx(&adev->sdma.instance[0]);
	if (err)
		goto out;

	for (i = 1; i < adev->sdma.num_instances; i++) {
		if (adev->ip_versions[SDMA0_HWIP][0] == IP_VERSION(4, 2, 2) ||
                    adev->ip_versions[SDMA0_HWIP][0] == IP_VERSION(4, 4, 0)) {
			/* Acturus & Aldebaran will leverage the same FW memory
			   for every SDMA instance */
			memcpy((void *)&adev->sdma.instance[i],
			       (void *)&adev->sdma.instance[0],
			       sizeof(struct amdgpu_sdma_instance));
		}
		else {
			snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_sdma%d.bin", chip_name, i);

			err = request_firmware(&adev->sdma.instance[i].fw, fw_name, adev->dev);
			if (err)
				goto out;

			err = sdma_v4_0_init_inst_ctx(&adev->sdma.instance[i]);
			if (err)
				goto out;
		}
	}

	DRM_DEBUG("psp_load == '%s'\n",
		adev->firmware.load_type == AMDGPU_FW_LOAD_PSP ? "true" : "false");

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_SDMA0 + i];
			info->ucode_id = AMDGPU_UCODE_ID_SDMA0 + i;
			info->fw = adev->sdma.instance[i].fw;
			header = (const struct common_firmware_header *)info->fw->data;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);
		}
	}

out:
	if (err) {
		DRM_ERROR("sdma_v4_0: Failed to load firmware \"%s\"\n", fw_name);
		sdma_v4_0_destroy_inst_ctx(adev);
	}
	return err;
}

/**
 * sdma_v4_0_ring_get_rptr - get the current read pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current rptr from the hardware (VEGA10+).
 */
static uint64_t sdma_v4_0_ring_get_rptr(struct amdgpu_ring *ring)
{
	u64 *rptr;

	/* XXX check if swapping is necessary on BE */
	rptr = ((u64 *)&ring->adev->wb.wb[ring->rptr_offs]);

	DRM_DEBUG("rptr before shift == 0x%016llx\n", *rptr);
	return ((*rptr) >> 2);
}

/**
 * sdma_v4_0_ring_get_wptr - get the current write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current wptr from the hardware (VEGA10+).
 */
static uint64_t sdma_v4_0_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u64 wptr;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		wptr = READ_ONCE(*((u64 *)&adev->wb.wb[ring->wptr_offs]));
		DRM_DEBUG("wptr/doorbell before shift == 0x%016llx\n", wptr);
	} else {
		wptr = RREG32_SDMA(ring->me, mmSDMA0_GFX_RB_WPTR_HI);
		wptr = wptr << 32;
		wptr |= RREG32_SDMA(ring->me, mmSDMA0_GFX_RB_WPTR);
		DRM_DEBUG("wptr before shift [%i] wptr == 0x%016llx\n",
				ring->me, wptr);
	}

	return wptr >> 2;
}

/**
 * sdma_v4_0_ring_set_wptr - commit the write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Write the wptr back to the hardware (VEGA10+).
 */
static void sdma_v4_0_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	DRM_DEBUG("Setting write pointer\n");
	if (ring->use_doorbell) {
		u64 *wb = (u64 *)&adev->wb.wb[ring->wptr_offs];

		DRM_DEBUG("Using doorbell -- "
				"wptr_offs == 0x%08x "
				"lower_32_bits(ring->wptr) << 2 == 0x%08x "
				"upper_32_bits(ring->wptr) << 2 == 0x%08x\n",
				ring->wptr_offs,
				lower_32_bits(ring->wptr << 2),
				upper_32_bits(ring->wptr << 2));
		/* XXX check if swapping is necessary on BE */
		WRITE_ONCE(*wb, (ring->wptr << 2));
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
		WREG32_SDMA(ring->me, mmSDMA0_GFX_RB_WPTR,
			    lower_32_bits(ring->wptr << 2));
		WREG32_SDMA(ring->me, mmSDMA0_GFX_RB_WPTR_HI,
			    upper_32_bits(ring->wptr << 2));
	}
}

/**
 * sdma_v4_0_page_ring_get_wptr - get the current write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Get the current wptr from the hardware (VEGA10+).
 */
static uint64_t sdma_v4_0_page_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u64 wptr;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		wptr = READ_ONCE(*((u64 *)&adev->wb.wb[ring->wptr_offs]));
	} else {
		wptr = RREG32_SDMA(ring->me, mmSDMA0_PAGE_RB_WPTR_HI);
		wptr = wptr << 32;
		wptr |= RREG32_SDMA(ring->me, mmSDMA0_PAGE_RB_WPTR);
	}

	return wptr >> 2;
}

/**
 * sdma_v4_0_page_ring_set_wptr - commit the write pointer
 *
 * @ring: amdgpu ring pointer
 *
 * Write the wptr back to the hardware (VEGA10+).
 */
static void sdma_v4_0_page_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		u64 *wb = (u64 *)&adev->wb.wb[ring->wptr_offs];

		/* XXX check if swapping is necessary on BE */
		WRITE_ONCE(*wb, (ring->wptr << 2));
		WDOORBELL64(ring->doorbell_index, ring->wptr << 2);
	} else {
		uint64_t wptr = ring->wptr << 2;

		WREG32_SDMA(ring->me, mmSDMA0_PAGE_RB_WPTR,
			    lower_32_bits(wptr));
		WREG32_SDMA(ring->me, mmSDMA0_PAGE_RB_WPTR_HI,
			    upper_32_bits(wptr));
	}
}

static void sdma_v4_0_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
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
 * sdma_v4_0_ring_emit_ib - Schedule an IB on the DMA engine
 *
 * @ring: amdgpu ring pointer
 * @job: job to retrieve vmid from
 * @ib: IB object to schedule
 * @flags: unused
 *
 * Schedule an IB in the DMA ring (VEGA10).
 */
static void sdma_v4_0_ring_emit_ib(struct amdgpu_ring *ring,
				   struct amdgpu_job *job,
				   struct amdgpu_ib *ib,
				   uint32_t flags)
{
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);

	/* IB packet must end on a 8 DW boundary */
	sdma_v4_0_ring_insert_nop(ring, (2 - lower_32_bits(ring->wptr)) & 7);

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_INDIRECT) |
			  SDMA_PKT_INDIRECT_HEADER_VMID(vmid & 0xf));
	/* base must be 32 byte aligned */
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr) & 0xffffffe0);
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, ib->length_dw);
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, 0);

}

static void sdma_v4_0_wait_reg_mem(struct amdgpu_ring *ring,
				   int mem_space, int hdp,
				   uint32_t addr0, uint32_t addr1,
				   uint32_t ref, uint32_t mask,
				   uint32_t inv)
{
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_POLL_REGMEM) |
			  SDMA_PKT_POLL_REGMEM_HEADER_HDP_FLUSH(hdp) |
			  SDMA_PKT_POLL_REGMEM_HEADER_MEM_POLL(mem_space) |
			  SDMA_PKT_POLL_REGMEM_HEADER_FUNC(3)); /* == */
	if (mem_space) {
		/* memory */
		amdgpu_ring_write(ring, addr0);
		amdgpu_ring_write(ring, addr1);
	} else {
		/* registers */
		amdgpu_ring_write(ring, addr0 << 2);
		amdgpu_ring_write(ring, addr1 << 2);
	}
	amdgpu_ring_write(ring, ref); /* reference */
	amdgpu_ring_write(ring, mask); /* mask */
	amdgpu_ring_write(ring, SDMA_PKT_POLL_REGMEM_DW5_RETRY_COUNT(0xfff) |
			  SDMA_PKT_POLL_REGMEM_DW5_INTERVAL(inv)); /* retry count, poll interval */
}

/**
 * sdma_v4_0_ring_emit_hdp_flush - emit an hdp flush on the DMA ring
 *
 * @ring: amdgpu ring pointer
 *
 * Emit an hdp flush packet on the requested DMA ring.
 */
static void sdma_v4_0_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 ref_and_mask = 0;
	const struct nbio_hdp_flush_reg *nbio_hf_reg = adev->nbio.hdp_flush_reg;

	ref_and_mask = nbio_hf_reg->ref_and_mask_sdma0 << ring->me;

	sdma_v4_0_wait_reg_mem(ring, 0, 1,
			       adev->nbio.funcs->get_hdp_flush_done_offset(adev),
			       adev->nbio.funcs->get_hdp_flush_req_offset(adev),
			       ref_and_mask, ref_and_mask, 10);
}

/**
 * sdma_v4_0_ring_emit_fence - emit a fence on the DMA ring
 *
 * @ring: amdgpu ring pointer
 * @addr: address
 * @seq: sequence number
 * @flags: fence related flags
 *
 * Add a DMA fence packet to the ring to write
 * the fence seq number and DMA trap packet to generate
 * an interrupt if needed (VEGA10).
 */
static void sdma_v4_0_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				      unsigned flags)
{
	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	/* write the fence */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_FENCE));
	/* zero in first two bits */
	BUG_ON(addr & 0x3);
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, lower_32_bits(seq));

	/* optionally write high bits as well */
	if (write64bit) {
		addr += 4;
		amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_FENCE));
		/* zero in first two bits */
		BUG_ON(addr & 0x3);
		amdgpu_ring_write(ring, lower_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(addr));
		amdgpu_ring_write(ring, upper_32_bits(seq));
	}

	/* generate an interrupt */
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_TRAP));
	amdgpu_ring_write(ring, SDMA_PKT_TRAP_INT_CONTEXT_INT_CONTEXT(0));
}


/**
 * sdma_v4_0_gfx_stop - stop the gfx async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the gfx async dma ring buffers (VEGA10).
 */
static void sdma_v4_0_gfx_stop(struct amdgpu_device *adev)
{
	struct amdgpu_ring *sdma[AMDGPU_MAX_SDMA_INSTANCES];
	u32 rb_cntl, ib_cntl;
	int i, unset = 0;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		sdma[i] = &adev->sdma.instance[i].ring;

		if ((adev->mman.buffer_funcs_ring == sdma[i]) && unset != 1) {
			amdgpu_ttm_set_buffer_funcs_status(adev, false);
			unset = 1;
		}

		rb_cntl = RREG32_SDMA(i, mmSDMA0_GFX_RB_CNTL);
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_ENABLE, 0);
		WREG32_SDMA(i, mmSDMA0_GFX_RB_CNTL, rb_cntl);
		ib_cntl = RREG32_SDMA(i, mmSDMA0_GFX_IB_CNTL);
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_ENABLE, 0);
		WREG32_SDMA(i, mmSDMA0_GFX_IB_CNTL, ib_cntl);
	}
}

/**
 * sdma_v4_0_rlc_stop - stop the compute async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the compute async dma queues (VEGA10).
 */
static void sdma_v4_0_rlc_stop(struct amdgpu_device *adev)
{
	/* XXX todo */
}

/**
 * sdma_v4_0_page_stop - stop the page async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the page async dma ring buffers (VEGA10).
 */
static void sdma_v4_0_page_stop(struct amdgpu_device *adev)
{
	struct amdgpu_ring *sdma[AMDGPU_MAX_SDMA_INSTANCES];
	u32 rb_cntl, ib_cntl;
	int i;
	bool unset = false;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		sdma[i] = &adev->sdma.instance[i].page;

		if ((adev->mman.buffer_funcs_ring == sdma[i]) &&
			(!unset)) {
			amdgpu_ttm_set_buffer_funcs_status(adev, false);
			unset = true;
		}

		rb_cntl = RREG32_SDMA(i, mmSDMA0_PAGE_RB_CNTL);
		rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_PAGE_RB_CNTL,
					RB_ENABLE, 0);
		WREG32_SDMA(i, mmSDMA0_PAGE_RB_CNTL, rb_cntl);
		ib_cntl = RREG32_SDMA(i, mmSDMA0_PAGE_IB_CNTL);
		ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_PAGE_IB_CNTL,
					IB_ENABLE, 0);
		WREG32_SDMA(i, mmSDMA0_PAGE_IB_CNTL, ib_cntl);
	}
}

/**
 * sdma_v4_0_ctx_switch_enable - stop the async dma engines context switch
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs context switch.
 *
 * Halt or unhalt the async dma engines context switch (VEGA10).
 */
static void sdma_v4_0_ctx_switch_enable(struct amdgpu_device *adev, bool enable)
{
	u32 f32_cntl, phase_quantum = 0;
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
		f32_cntl = RREG32_SDMA(i, mmSDMA0_CNTL);
		f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_CNTL,
				AUTO_CTXSW_ENABLE, enable ? 1 : 0);
		if (enable && amdgpu_sdma_phase_quantum) {
			WREG32_SDMA(i, mmSDMA0_PHASE0_QUANTUM, phase_quantum);
			WREG32_SDMA(i, mmSDMA0_PHASE1_QUANTUM, phase_quantum);
			WREG32_SDMA(i, mmSDMA0_PHASE2_QUANTUM, phase_quantum);
		}
		WREG32_SDMA(i, mmSDMA0_CNTL, f32_cntl);

		/*
		 * Enable SDMA utilization. Its only supported on
		 * Arcturus for the moment and firmware version 14
		 * and above.
		 */
		if (adev->ip_versions[SDMA0_HWIP][0] == IP_VERSION(4, 2, 2) &&
		    adev->sdma.instance[i].fw_version >= 14)
			WREG32_SDMA(i, mmSDMA0_PUB_DUMMY_REG2, enable);
		/* Extend page fault timeout to avoid interrupt storm */
		WREG32_SDMA(i, mmSDMA0_UTCL1_TIMEOUT, 0x00800080);
	}

}

/**
 * sdma_v4_0_enable - stop the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @enable: enable/disable the DMA MEs.
 *
 * Halt or unhalt the async dma engines (VEGA10).
 */
static void sdma_v4_0_enable(struct amdgpu_device *adev, bool enable)
{
	u32 f32_cntl;
	int i;

	if (!enable) {
		sdma_v4_0_gfx_stop(adev);
		sdma_v4_0_rlc_stop(adev);
		if (adev->sdma.has_page_queue)
			sdma_v4_0_page_stop(adev);
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		f32_cntl = RREG32_SDMA(i, mmSDMA0_F32_CNTL);
		f32_cntl = REG_SET_FIELD(f32_cntl, SDMA0_F32_CNTL, HALT, enable ? 0 : 1);
		WREG32_SDMA(i, mmSDMA0_F32_CNTL, f32_cntl);
	}
}

/*
 * sdma_v4_0_rb_cntl - get parameters for rb_cntl
 */
static uint32_t sdma_v4_0_rb_cntl(struct amdgpu_ring *ring, uint32_t rb_cntl)
{
	/* Set ring buffer size in dwords */
	uint32_t rb_bufsz = order_base_2(ring->ring_size / 4);

	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_SIZE, rb_bufsz);
#ifdef __BIG_ENDIAN
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_SWAP_ENABLE, 1);
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL,
				RPTR_WRITEBACK_SWAP_ENABLE, 1);
#endif
	return rb_cntl;
}

/**
 * sdma_v4_0_gfx_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @i: instance to resume
 *
 * Set up the gfx DMA ring buffers and enable them (VEGA10).
 * Returns 0 for success, error for failure.
 */
static void sdma_v4_0_gfx_resume(struct amdgpu_device *adev, unsigned int i)
{
	struct amdgpu_ring *ring = &adev->sdma.instance[i].ring;
	u32 rb_cntl, ib_cntl, wptr_poll_cntl;
	u32 wb_offset;
	u32 doorbell;
	u32 doorbell_offset;
	u64 wptr_gpu_addr;

	wb_offset = (ring->rptr_offs * 4);

	rb_cntl = RREG32_SDMA(i, mmSDMA0_GFX_RB_CNTL);
	rb_cntl = sdma_v4_0_rb_cntl(ring, rb_cntl);
	WREG32_SDMA(i, mmSDMA0_GFX_RB_CNTL, rb_cntl);

	/* Initialize the ring buffer's read and write pointers */
	WREG32_SDMA(i, mmSDMA0_GFX_RB_RPTR, 0);
	WREG32_SDMA(i, mmSDMA0_GFX_RB_RPTR_HI, 0);
	WREG32_SDMA(i, mmSDMA0_GFX_RB_WPTR, 0);
	WREG32_SDMA(i, mmSDMA0_GFX_RB_WPTR_HI, 0);

	/* set the wb address whether it's enabled or not */
	WREG32_SDMA(i, mmSDMA0_GFX_RB_RPTR_ADDR_HI,
	       upper_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFF);
	WREG32_SDMA(i, mmSDMA0_GFX_RB_RPTR_ADDR_LO,
	       lower_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFC);

	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL,
				RPTR_WRITEBACK_ENABLE, 1);

	WREG32_SDMA(i, mmSDMA0_GFX_RB_BASE, ring->gpu_addr >> 8);
	WREG32_SDMA(i, mmSDMA0_GFX_RB_BASE_HI, ring->gpu_addr >> 40);

	ring->wptr = 0;

	/* before programing wptr to a less value, need set minor_ptr_update first */
	WREG32_SDMA(i, mmSDMA0_GFX_MINOR_PTR_UPDATE, 1);

	doorbell = RREG32_SDMA(i, mmSDMA0_GFX_DOORBELL);
	doorbell_offset = RREG32_SDMA(i, mmSDMA0_GFX_DOORBELL_OFFSET);

	doorbell = REG_SET_FIELD(doorbell, SDMA0_GFX_DOORBELL, ENABLE,
				 ring->use_doorbell);
	doorbell_offset = REG_SET_FIELD(doorbell_offset,
					SDMA0_GFX_DOORBELL_OFFSET,
					OFFSET, ring->doorbell_index);
	WREG32_SDMA(i, mmSDMA0_GFX_DOORBELL, doorbell);
	WREG32_SDMA(i, mmSDMA0_GFX_DOORBELL_OFFSET, doorbell_offset);

	sdma_v4_0_ring_set_wptr(ring);

	/* set minor_ptr_update to 0 after wptr programed */
	WREG32_SDMA(i, mmSDMA0_GFX_MINOR_PTR_UPDATE, 0);

	/* setup the wptr shadow polling */
	wptr_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	WREG32_SDMA(i, mmSDMA0_GFX_RB_WPTR_POLL_ADDR_LO,
		    lower_32_bits(wptr_gpu_addr));
	WREG32_SDMA(i, mmSDMA0_GFX_RB_WPTR_POLL_ADDR_HI,
		    upper_32_bits(wptr_gpu_addr));
	wptr_poll_cntl = RREG32_SDMA(i, mmSDMA0_GFX_RB_WPTR_POLL_CNTL);
	wptr_poll_cntl = REG_SET_FIELD(wptr_poll_cntl,
				       SDMA0_GFX_RB_WPTR_POLL_CNTL,
				       F32_POLL_ENABLE, amdgpu_sriov_vf(adev)? 1 : 0);
	WREG32_SDMA(i, mmSDMA0_GFX_RB_WPTR_POLL_CNTL, wptr_poll_cntl);

	/* enable DMA RB */
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_GFX_RB_CNTL, RB_ENABLE, 1);
	WREG32_SDMA(i, mmSDMA0_GFX_RB_CNTL, rb_cntl);

	ib_cntl = RREG32_SDMA(i, mmSDMA0_GFX_IB_CNTL);
	ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_ENABLE, 1);
#ifdef __BIG_ENDIAN
	ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_GFX_IB_CNTL, IB_SWAP_ENABLE, 1);
#endif
	/* enable DMA IBs */
	WREG32_SDMA(i, mmSDMA0_GFX_IB_CNTL, ib_cntl);

	ring->sched.ready = true;
}

/**
 * sdma_v4_0_page_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 * @i: instance to resume
 *
 * Set up the page DMA ring buffers and enable them (VEGA10).
 * Returns 0 for success, error for failure.
 */
static void sdma_v4_0_page_resume(struct amdgpu_device *adev, unsigned int i)
{
	struct amdgpu_ring *ring = &adev->sdma.instance[i].page;
	u32 rb_cntl, ib_cntl, wptr_poll_cntl;
	u32 wb_offset;
	u32 doorbell;
	u32 doorbell_offset;
	u64 wptr_gpu_addr;

	wb_offset = (ring->rptr_offs * 4);

	rb_cntl = RREG32_SDMA(i, mmSDMA0_PAGE_RB_CNTL);
	rb_cntl = sdma_v4_0_rb_cntl(ring, rb_cntl);
	WREG32_SDMA(i, mmSDMA0_PAGE_RB_CNTL, rb_cntl);

	/* Initialize the ring buffer's read and write pointers */
	WREG32_SDMA(i, mmSDMA0_PAGE_RB_RPTR, 0);
	WREG32_SDMA(i, mmSDMA0_PAGE_RB_RPTR_HI, 0);
	WREG32_SDMA(i, mmSDMA0_PAGE_RB_WPTR, 0);
	WREG32_SDMA(i, mmSDMA0_PAGE_RB_WPTR_HI, 0);

	/* set the wb address whether it's enabled or not */
	WREG32_SDMA(i, mmSDMA0_PAGE_RB_RPTR_ADDR_HI,
	       upper_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFF);
	WREG32_SDMA(i, mmSDMA0_PAGE_RB_RPTR_ADDR_LO,
	       lower_32_bits(adev->wb.gpu_addr + wb_offset) & 0xFFFFFFFC);

	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_PAGE_RB_CNTL,
				RPTR_WRITEBACK_ENABLE, 1);

	WREG32_SDMA(i, mmSDMA0_PAGE_RB_BASE, ring->gpu_addr >> 8);
	WREG32_SDMA(i, mmSDMA0_PAGE_RB_BASE_HI, ring->gpu_addr >> 40);

	ring->wptr = 0;

	/* before programing wptr to a less value, need set minor_ptr_update first */
	WREG32_SDMA(i, mmSDMA0_PAGE_MINOR_PTR_UPDATE, 1);

	doorbell = RREG32_SDMA(i, mmSDMA0_PAGE_DOORBELL);
	doorbell_offset = RREG32_SDMA(i, mmSDMA0_PAGE_DOORBELL_OFFSET);

	doorbell = REG_SET_FIELD(doorbell, SDMA0_PAGE_DOORBELL, ENABLE,
				 ring->use_doorbell);
	doorbell_offset = REG_SET_FIELD(doorbell_offset,
					SDMA0_PAGE_DOORBELL_OFFSET,
					OFFSET, ring->doorbell_index);
	WREG32_SDMA(i, mmSDMA0_PAGE_DOORBELL, doorbell);
	WREG32_SDMA(i, mmSDMA0_PAGE_DOORBELL_OFFSET, doorbell_offset);

	/* paging queue doorbell range is setup at sdma_v4_0_gfx_resume */
	sdma_v4_0_page_ring_set_wptr(ring);

	/* set minor_ptr_update to 0 after wptr programed */
	WREG32_SDMA(i, mmSDMA0_PAGE_MINOR_PTR_UPDATE, 0);

	/* setup the wptr shadow polling */
	wptr_gpu_addr = adev->wb.gpu_addr + (ring->wptr_offs * 4);
	WREG32_SDMA(i, mmSDMA0_PAGE_RB_WPTR_POLL_ADDR_LO,
		    lower_32_bits(wptr_gpu_addr));
	WREG32_SDMA(i, mmSDMA0_PAGE_RB_WPTR_POLL_ADDR_HI,
		    upper_32_bits(wptr_gpu_addr));
	wptr_poll_cntl = RREG32_SDMA(i, mmSDMA0_PAGE_RB_WPTR_POLL_CNTL);
	wptr_poll_cntl = REG_SET_FIELD(wptr_poll_cntl,
				       SDMA0_PAGE_RB_WPTR_POLL_CNTL,
				       F32_POLL_ENABLE, amdgpu_sriov_vf(adev)? 1 : 0);
	WREG32_SDMA(i, mmSDMA0_PAGE_RB_WPTR_POLL_CNTL, wptr_poll_cntl);

	/* enable DMA RB */
	rb_cntl = REG_SET_FIELD(rb_cntl, SDMA0_PAGE_RB_CNTL, RB_ENABLE, 1);
	WREG32_SDMA(i, mmSDMA0_PAGE_RB_CNTL, rb_cntl);

	ib_cntl = RREG32_SDMA(i, mmSDMA0_PAGE_IB_CNTL);
	ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_PAGE_IB_CNTL, IB_ENABLE, 1);
#ifdef __BIG_ENDIAN
	ib_cntl = REG_SET_FIELD(ib_cntl, SDMA0_PAGE_IB_CNTL, IB_SWAP_ENABLE, 1);
#endif
	/* enable DMA IBs */
	WREG32_SDMA(i, mmSDMA0_PAGE_IB_CNTL, ib_cntl);

	ring->sched.ready = true;
}

static void
sdma_v4_1_update_power_gating(struct amdgpu_device *adev, bool enable)
{
	uint32_t def, data;

	if (enable && (adev->pg_flags & AMD_PG_SUPPORT_SDMA)) {
		/* enable idle interrupt */
		def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CNTL));
		data |= SDMA0_CNTL__CTXEMPTY_INT_ENABLE_MASK;

		if (data != def)
			WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CNTL), data);
	} else {
		/* disable idle interrupt */
		def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CNTL));
		data &= ~SDMA0_CNTL__CTXEMPTY_INT_ENABLE_MASK;
		if (data != def)
			WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CNTL), data);
	}
}

static void sdma_v4_1_init_power_gating(struct amdgpu_device *adev)
{
	uint32_t def, data;

	/* Enable HW based PG. */
	def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL));
	data |= SDMA0_POWER_CNTL__PG_CNTL_ENABLE_MASK;
	if (data != def)
		WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL), data);

	/* enable interrupt */
	def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CNTL));
	data |= SDMA0_CNTL__CTXEMPTY_INT_ENABLE_MASK;
	if (data != def)
		WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CNTL), data);

	/* Configure hold time to filter in-valid power on/off request. Use default right now */
	def = data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL));
	data &= ~SDMA0_POWER_CNTL__ON_OFF_CONDITION_HOLD_TIME_MASK;
	data |= (mmSDMA0_POWER_CNTL_DEFAULT & SDMA0_POWER_CNTL__ON_OFF_CONDITION_HOLD_TIME_MASK);
	/* Configure switch time for hysteresis purpose. Use default right now */
	data &= ~SDMA0_POWER_CNTL__ON_OFF_STATUS_DURATION_TIME_MASK;
	data |= (mmSDMA0_POWER_CNTL_DEFAULT & SDMA0_POWER_CNTL__ON_OFF_STATUS_DURATION_TIME_MASK);
	if(data != def)
		WREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL), data);
}

static void sdma_v4_0_init_pg(struct amdgpu_device *adev)
{
	if (!(adev->pg_flags & AMD_PG_SUPPORT_SDMA))
		return;

	switch (adev->ip_versions[SDMA0_HWIP][0]) {
	case IP_VERSION(4, 1, 0):
        case IP_VERSION(4, 1, 1):
	case IP_VERSION(4, 1, 2):
		sdma_v4_1_init_power_gating(adev);
		sdma_v4_1_update_power_gating(adev, true);
		break;
	default:
		break;
	}
}

/**
 * sdma_v4_0_rlc_resume - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the compute DMA queues and enable them (VEGA10).
 * Returns 0 for success, error for failure.
 */
static int sdma_v4_0_rlc_resume(struct amdgpu_device *adev)
{
	sdma_v4_0_init_pg(adev);

	return 0;
}

/**
 * sdma_v4_0_load_microcode - load the sDMA ME ucode
 *
 * @adev: amdgpu_device pointer
 *
 * Loads the sDMA0/1 ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int sdma_v4_0_load_microcode(struct amdgpu_device *adev)
{
	const struct sdma_firmware_header_v1_0 *hdr;
	const __le32 *fw_data;
	u32 fw_size;
	int i, j;

	/* halt the MEs */
	sdma_v4_0_enable(adev, false);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (!adev->sdma.instance[i].fw)
			return -EINVAL;

		hdr = (const struct sdma_firmware_header_v1_0 *)adev->sdma.instance[i].fw->data;
		amdgpu_ucode_print_sdma_hdr(&hdr->header);
		fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;

		fw_data = (const __le32 *)
			(adev->sdma.instance[i].fw->data +
				le32_to_cpu(hdr->header.ucode_array_offset_bytes));

		WREG32_SDMA(i, mmSDMA0_UCODE_ADDR, 0);

		for (j = 0; j < fw_size; j++)
			WREG32_SDMA(i, mmSDMA0_UCODE_DATA,
				    le32_to_cpup(fw_data++));

		WREG32_SDMA(i, mmSDMA0_UCODE_ADDR,
			    adev->sdma.instance[i].fw_version);
	}

	return 0;
}

/**
 * sdma_v4_0_start - setup and start the async dma engines
 *
 * @adev: amdgpu_device pointer
 *
 * Set up the DMA engines and enable them (VEGA10).
 * Returns 0 for success, error for failure.
 */
static int sdma_v4_0_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	int i, r = 0;

	if (amdgpu_sriov_vf(adev)) {
		sdma_v4_0_ctx_switch_enable(adev, false);
		sdma_v4_0_enable(adev, false);
	} else {

		if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
			r = sdma_v4_0_load_microcode(adev);
			if (r)
				return r;
		}

		/* unhalt the MEs */
		sdma_v4_0_enable(adev, true);
		/* enable sdma ring preemption */
		sdma_v4_0_ctx_switch_enable(adev, true);
	}

	/* start the gfx rings and rlc compute queues */
	for (i = 0; i < adev->sdma.num_instances; i++) {
		uint32_t temp;

		WREG32_SDMA(i, mmSDMA0_SEM_WAIT_FAIL_TIMER_CNTL, 0);
		sdma_v4_0_gfx_resume(adev, i);
		if (adev->sdma.has_page_queue)
			sdma_v4_0_page_resume(adev, i);

		/* set utc l1 enable flag always to 1 */
		temp = RREG32_SDMA(i, mmSDMA0_CNTL);
		temp = REG_SET_FIELD(temp, SDMA0_CNTL, UTC_L1_ENABLE, 1);
		WREG32_SDMA(i, mmSDMA0_CNTL, temp);

		if (!amdgpu_sriov_vf(adev)) {
			/* unhalt engine */
			temp = RREG32_SDMA(i, mmSDMA0_F32_CNTL);
			temp = REG_SET_FIELD(temp, SDMA0_F32_CNTL, HALT, 0);
			WREG32_SDMA(i, mmSDMA0_F32_CNTL, temp);
		}
	}

	if (amdgpu_sriov_vf(adev)) {
		sdma_v4_0_ctx_switch_enable(adev, true);
		sdma_v4_0_enable(adev, true);
	} else {
		r = sdma_v4_0_rlc_resume(adev);
		if (r)
			return r;
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;

		r = amdgpu_ring_test_helper(ring);
		if (r)
			return r;

		if (adev->sdma.has_page_queue) {
			struct amdgpu_ring *page = &adev->sdma.instance[i].page;

			r = amdgpu_ring_test_helper(page);
			if (r)
				return r;

			if (adev->mman.buffer_funcs_ring == page)
				amdgpu_ttm_set_buffer_funcs_status(adev, true);
		}

		if (adev->mman.buffer_funcs_ring == ring)
			amdgpu_ttm_set_buffer_funcs_status(adev, true);
	}

	return r;
}

/**
 * sdma_v4_0_ring_test_ring - simple async dma engine test
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test the DMA engine by writing using it to write an
 * value to memory. (VEGA10).
 * Returns 0 for success, error for failure.
 */
static int sdma_v4_0_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned i;
	unsigned index;
	int r;
	u32 tmp;
	u64 gpu_addr;

	r = amdgpu_device_wb_get(adev, &index);
	if (r)
		return r;

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	tmp = 0xCAFEDEAD;
	adev->wb.wb[index] = cpu_to_le32(tmp);

	r = amdgpu_ring_alloc(ring, 5);
	if (r)
		goto error_free_wb;

	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_WRITE) |
			  SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_WRITE_LINEAR));
	amdgpu_ring_write(ring, lower_32_bits(gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(gpu_addr));
	amdgpu_ring_write(ring, SDMA_PKT_WRITE_UNTILED_DW_3_COUNT(0));
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = le32_to_cpu(adev->wb.wb[index]);
		if (tmp == 0xDEADBEEF)
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;

error_free_wb:
	amdgpu_device_wb_free(adev, index);
	return r;
}

/**
 * sdma_v4_0_ring_test_ib - test an IB on the DMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @timeout: timeout value in jiffies, or MAX_SCHEDULE_TIMEOUT
 *
 * Test a simple IB in the DMA ring (VEGA10).
 * Returns 0 on success, error on failure.
 */
static int sdma_v4_0_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct dma_fence *f = NULL;
	unsigned index;
	long r;
	u32 tmp = 0;
	u64 gpu_addr;

	r = amdgpu_device_wb_get(adev, &index);
	if (r)
		return r;

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	tmp = 0xCAFEDEAD;
	adev->wb.wb[index] = cpu_to_le32(tmp);
	memset(&ib, 0, sizeof(ib));
	r = amdgpu_ib_get(adev, NULL, 256,
					AMDGPU_IB_POOL_DIRECT, &ib);
	if (r)
		goto err0;

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
		r = -ETIMEDOUT;
		goto err1;
	} else if (r < 0) {
		goto err1;
	}
	tmp = le32_to_cpu(adev->wb.wb[index]);
	if (tmp == 0xDEADBEEF)
		r = 0;
	else
		r = -EINVAL;

err1:
	amdgpu_ib_free(adev, &ib, NULL);
	dma_fence_put(f);
err0:
	amdgpu_device_wb_free(adev, index);
	return r;
}


/**
 * sdma_v4_0_vm_copy_pte - update PTEs by copying them from the GART
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @src: src addr to copy from
 * @count: number of page entries to update
 *
 * Update PTEs by copying them from the GART using sDMA (VEGA10).
 */
static void sdma_v4_0_vm_copy_pte(struct amdgpu_ib *ib,
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
 * sdma_v4_0_vm_write_pte - update PTEs by writing them manually
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @value: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 *
 * Update PTEs by writing them manually using sDMA (VEGA10).
 */
static void sdma_v4_0_vm_write_pte(struct amdgpu_ib *ib, uint64_t pe,
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
 * sdma_v4_0_vm_set_pte_pde - update the page tables using sDMA
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: access flags
 *
 * Update the page tables using sDMA (VEGA10).
 */
static void sdma_v4_0_vm_set_pte_pde(struct amdgpu_ib *ib,
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
 * sdma_v4_0_ring_pad_ib - pad the IB to the required number of dw
 *
 * @ring: amdgpu_ring structure holding ring information
 * @ib: indirect buffer to fill with padding
 */
static void sdma_v4_0_ring_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib)
{
	struct amdgpu_sdma_instance *sdma = amdgpu_sdma_get_instance_from_ring(ring);
	u32 pad_count;
	int i;

	pad_count = (-ib->length_dw) & 7;
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
 * sdma_v4_0_ring_emit_pipeline_sync - sync the pipeline
 *
 * @ring: amdgpu_ring pointer
 *
 * Make sure all previous operations are completed (CIK).
 */
static void sdma_v4_0_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	/* wait for idle */
	sdma_v4_0_wait_reg_mem(ring, 1, 0,
			       addr & 0xfffffffc,
			       upper_32_bits(addr) & 0xffffffff,
			       seq, 0xffffffff, 4);
}


/**
 * sdma_v4_0_ring_emit_vm_flush - vm flush using sDMA
 *
 * @ring: amdgpu_ring pointer
 * @vmid: vmid number to use
 * @pd_addr: address
 *
 * Update the page table base and flush the VM TLB
 * using sDMA (VEGA10).
 */
static void sdma_v4_0_ring_emit_vm_flush(struct amdgpu_ring *ring,
					 unsigned vmid, uint64_t pd_addr)
{
	amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);
}

static void sdma_v4_0_ring_emit_wreg(struct amdgpu_ring *ring,
				     uint32_t reg, uint32_t val)
{
	amdgpu_ring_write(ring, SDMA_PKT_HEADER_OP(SDMA_OP_SRBM_WRITE) |
			  SDMA_PKT_SRBM_WRITE_HEADER_BYTE_EN(0xf));
	amdgpu_ring_write(ring, reg);
	amdgpu_ring_write(ring, val);
}

static void sdma_v4_0_ring_emit_reg_wait(struct amdgpu_ring *ring, uint32_t reg,
					 uint32_t val, uint32_t mask)
{
	sdma_v4_0_wait_reg_mem(ring, 0, 0, reg, 0, val, mask, 10);
}

static bool sdma_v4_0_fw_support_paging_queue(struct amdgpu_device *adev)
{
	uint fw_version = adev->sdma.instance[0].fw_version;

	switch (adev->ip_versions[SDMA0_HWIP][0]) {
	case IP_VERSION(4, 0, 0):
		return fw_version >= 430;
	case IP_VERSION(4, 0, 1):
		/*return fw_version >= 31;*/
		return false;
	case IP_VERSION(4, 2, 0):
		return fw_version >= 123;
	default:
		return false;
	}
}

static int sdma_v4_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = sdma_v4_0_init_microcode(adev);
	if (r) {
		DRM_ERROR("Failed to load sdma firmware!\n");
		return r;
	}

	/* TODO: Page queue breaks driver reload under SRIOV */
	if ((adev->ip_versions[SDMA0_HWIP][0] == IP_VERSION(4, 0, 0)) &&
	    amdgpu_sriov_vf((adev)))
		adev->sdma.has_page_queue = false;
	else if (sdma_v4_0_fw_support_paging_queue(adev))
		adev->sdma.has_page_queue = true;

	sdma_v4_0_set_ring_funcs(adev);
	sdma_v4_0_set_buffer_funcs(adev);
	sdma_v4_0_set_vm_pte_funcs(adev);
	sdma_v4_0_set_irq_funcs(adev);
	sdma_v4_0_set_ras_funcs(adev);

	return 0;
}

static int sdma_v4_0_process_ras_data_cb(struct amdgpu_device *adev,
		void *err_data,
		struct amdgpu_iv_entry *entry);

static int sdma_v4_0_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct ras_ih_if ih_info = {
		.cb = sdma_v4_0_process_ras_data_cb,
	};

	sdma_v4_0_setup_ulv(adev);

	if (!amdgpu_persistent_edc_harvesting_supported(adev)) {
		if (adev->sdma.ras && adev->sdma.ras->ras_block.hw_ops &&
		    adev->sdma.ras->ras_block.hw_ops->reset_ras_error_count)
			adev->sdma.ras->ras_block.hw_ops->reset_ras_error_count(adev);
	}

	if (adev->sdma.ras && adev->sdma.ras->ras_block.ras_late_init)
		return adev->sdma.ras->ras_block.ras_late_init(adev, &ih_info);
	else
		return 0;
}

static int sdma_v4_0_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	int r, i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* SDMA trap event */
	for (i = 0; i < adev->sdma.num_instances; i++) {
		r = amdgpu_irq_add_id(adev, sdma_v4_0_seq_to_irq_id(i),
				      SDMA0_4_0__SRCID__SDMA_TRAP,
				      &adev->sdma.trap_irq);
		if (r)
			return r;
	}

	/* SDMA SRAM ECC event */
	for (i = 0; i < adev->sdma.num_instances; i++) {
		r = amdgpu_irq_add_id(adev, sdma_v4_0_seq_to_irq_id(i),
				      SDMA0_4_0__SRCID__SDMA_SRAM_ECC,
				      &adev->sdma.ecc_irq);
		if (r)
			return r;
	}

	/* SDMA VM_HOLE/DOORBELL_INV/POLL_TIMEOUT/SRBM_WRITE_PROTECTION event*/
	for (i = 0; i < adev->sdma.num_instances; i++) {
		r = amdgpu_irq_add_id(adev, sdma_v4_0_seq_to_irq_id(i),
				      SDMA0_4_0__SRCID__SDMA_VM_HOLE,
				      &adev->sdma.vm_hole_irq);
		if (r)
			return r;

		r = amdgpu_irq_add_id(adev, sdma_v4_0_seq_to_irq_id(i),
				      SDMA0_4_0__SRCID__SDMA_DOORBELL_INVALID,
				      &adev->sdma.doorbell_invalid_irq);
		if (r)
			return r;

		r = amdgpu_irq_add_id(adev, sdma_v4_0_seq_to_irq_id(i),
				      SDMA0_4_0__SRCID__SDMA_POLL_TIMEOUT,
				      &adev->sdma.pool_timeout_irq);
		if (r)
			return r;

		r = amdgpu_irq_add_id(adev, sdma_v4_0_seq_to_irq_id(i),
				      SDMA0_4_0__SRCID__SDMA_SRBMWRITE,
				      &adev->sdma.srbm_write_irq);
		if (r)
			return r;
	}

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		ring->ring_obj = NULL;
		ring->use_doorbell = true;

		DRM_DEBUG("SDMA %d use_doorbell being set to: [%s]\n", i,
				ring->use_doorbell?"true":"false");

		/* doorbell size is 2 dwords, get DWORD offset */
		ring->doorbell_index = adev->doorbell_index.sdma_engine[i] << 1;

		sprintf(ring->name, "sdma%d", i);
		r = amdgpu_ring_init(adev, ring, 1024, &adev->sdma.trap_irq,
				     AMDGPU_SDMA_IRQ_INSTANCE0 + i,
				     AMDGPU_RING_PRIO_DEFAULT, NULL);
		if (r)
			return r;

		if (adev->sdma.has_page_queue) {
			ring = &adev->sdma.instance[i].page;
			ring->ring_obj = NULL;
			ring->use_doorbell = true;

			/* paging queue use same doorbell index/routing as gfx queue
			 * with 0x400 (4096 dwords) offset on second doorbell page
			 */
			ring->doorbell_index = adev->doorbell_index.sdma_engine[i] << 1;
			ring->doorbell_index += 0x400;

			sprintf(ring->name, "page%d", i);
			r = amdgpu_ring_init(adev, ring, 1024,
					     &adev->sdma.trap_irq,
					     AMDGPU_SDMA_IRQ_INSTANCE0 + i,
					     AMDGPU_RING_PRIO_DEFAULT, NULL);
			if (r)
				return r;
		}
	}

	return r;
}

static int sdma_v4_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i;

	if (adev->sdma.ras && adev->sdma.ras->ras_block.hw_ops &&
		adev->sdma.ras->ras_block.ras_fini)
		adev->sdma.ras->ras_block.ras_fini(adev);

	for (i = 0; i < adev->sdma.num_instances; i++) {
		amdgpu_ring_fini(&adev->sdma.instance[i].ring);
		if (adev->sdma.has_page_queue)
			amdgpu_ring_fini(&adev->sdma.instance[i].page);
	}

	sdma_v4_0_destroy_inst_ctx(adev);

	return 0;
}

static int sdma_v4_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->flags & AMD_IS_APU)
		amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_SDMA, false);

	if (!amdgpu_sriov_vf(adev))
		sdma_v4_0_init_golden_registers(adev);

	r = sdma_v4_0_start(adev);

	return r;
}

static int sdma_v4_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i;

	if (amdgpu_sriov_vf(adev))
		return 0;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		amdgpu_irq_put(adev, &adev->sdma.ecc_irq,
			       AMDGPU_SDMA_IRQ_INSTANCE0 + i);
	}

	sdma_v4_0_ctx_switch_enable(adev, false);
	sdma_v4_0_enable(adev, false);

	if (adev->flags & AMD_IS_APU)
		amdgpu_dpm_set_powergating_by_smu(adev, AMD_IP_BLOCK_TYPE_SDMA, true);

	return 0;
}

static int sdma_v4_0_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* SMU saves SDMA state for us */
	if (adev->in_s0ix)
		return 0;

	return sdma_v4_0_hw_fini(adev);
}

static int sdma_v4_0_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* SMU restores SDMA state for us */
	if (adev->in_s0ix)
		return 0;

	return sdma_v4_0_hw_init(adev);
}

static bool sdma_v4_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		u32 tmp = RREG32_SDMA(i, mmSDMA0_STATUS_REG);

		if (!(tmp & SDMA0_STATUS_REG__IDLE_MASK))
			return false;
	}

	return true;
}

static int sdma_v4_0_wait_for_idle(void *handle)
{
	unsigned i, j;
	u32 sdma[AMDGPU_MAX_SDMA_INSTANCES];
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		for (j = 0; j < adev->sdma.num_instances; j++) {
			sdma[j] = RREG32_SDMA(j, mmSDMA0_STATUS_REG);
			if (!(sdma[j] & SDMA0_STATUS_REG__IDLE_MASK))
				break;
		}
		if (j == adev->sdma.num_instances)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int sdma_v4_0_soft_reset(void *handle)
{
	/* todo */

	return 0;
}

static int sdma_v4_0_set_trap_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	u32 sdma_cntl;

	sdma_cntl = RREG32_SDMA(type, mmSDMA0_CNTL);
	sdma_cntl = REG_SET_FIELD(sdma_cntl, SDMA0_CNTL, TRAP_ENABLE,
		       state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
	WREG32_SDMA(type, mmSDMA0_CNTL, sdma_cntl);

	return 0;
}

static int sdma_v4_0_process_trap_irq(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	uint32_t instance;

	DRM_DEBUG("IH: SDMA trap\n");
	instance = sdma_v4_0_irq_id_to_seq(entry->client_id);
	switch (entry->ring_id) {
	case 0:
		amdgpu_fence_process(&adev->sdma.instance[instance].ring);
		break;
	case 1:
		if (adev->ip_versions[SDMA0_HWIP][0] == IP_VERSION(4, 2, 0))
			amdgpu_fence_process(&adev->sdma.instance[instance].page);
		break;
	case 2:
		/* XXX compute */
		break;
	case 3:
		if (adev->ip_versions[SDMA0_HWIP][0] != IP_VERSION(4, 2, 0))
			amdgpu_fence_process(&adev->sdma.instance[instance].page);
		break;
	}
	return 0;
}

static int sdma_v4_0_process_ras_data_cb(struct amdgpu_device *adev,
		void *err_data,
		struct amdgpu_iv_entry *entry)
{
	int instance;

	/* When “Full RAS” is enabled, the per-IP interrupt sources should
	 * be disabled and the driver should only look for the aggregated
	 * interrupt via sync flood
	 */
	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__GFX))
		goto out;

	instance = sdma_v4_0_irq_id_to_seq(entry->client_id);
	if (instance < 0)
		goto out;

	amdgpu_sdma_process_ras_data_cb(adev, err_data, entry);

out:
	return AMDGPU_RAS_SUCCESS;
}

static int sdma_v4_0_process_illegal_inst_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	int instance;

	DRM_ERROR("Illegal instruction in SDMA command stream\n");

	instance = sdma_v4_0_irq_id_to_seq(entry->client_id);
	if (instance < 0)
		return 0;

	switch (entry->ring_id) {
	case 0:
		drm_sched_fault(&adev->sdma.instance[instance].ring.sched);
		break;
	}
	return 0;
}

static int sdma_v4_0_set_ecc_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	u32 sdma_edc_config;

	sdma_edc_config = RREG32_SDMA(type, mmSDMA0_EDC_CONFIG);
	sdma_edc_config = REG_SET_FIELD(sdma_edc_config, SDMA0_EDC_CONFIG, ECC_INT_ENABLE,
		       state == AMDGPU_IRQ_STATE_ENABLE ? 1 : 0);
	WREG32_SDMA(type, mmSDMA0_EDC_CONFIG, sdma_edc_config);

	return 0;
}

static int sdma_v4_0_print_iv_entry(struct amdgpu_device *adev,
					      struct amdgpu_iv_entry *entry)
{
	int instance;
	struct amdgpu_task_info task_info;
	u64 addr;

	instance = sdma_v4_0_irq_id_to_seq(entry->client_id);
	if (instance < 0 || instance >= adev->sdma.num_instances) {
		dev_err(adev->dev, "sdma instance invalid %d\n", instance);
		return -EINVAL;
	}

	addr = (u64)entry->src_data[0] << 12;
	addr |= ((u64)entry->src_data[1] & 0xf) << 44;

	memset(&task_info, 0, sizeof(struct amdgpu_task_info));
	amdgpu_vm_get_task_info(adev, entry->pasid, &task_info);

	dev_dbg_ratelimited(adev->dev,
		   "[sdma%d] address:0x%016llx src_id:%u ring:%u vmid:%u "
		   "pasid:%u, for process %s pid %d thread %s pid %d\n",
		   instance, addr, entry->src_id, entry->ring_id, entry->vmid,
		   entry->pasid, task_info.process_name, task_info.tgid,
		   task_info.task_name, task_info.pid);
	return 0;
}

static int sdma_v4_0_process_vm_hole_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	dev_dbg_ratelimited(adev->dev, "MC or SEM address in VM hole\n");
	sdma_v4_0_print_iv_entry(adev, entry);
	return 0;
}

static int sdma_v4_0_process_doorbell_invalid_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	dev_dbg_ratelimited(adev->dev, "SDMA received a doorbell from BIF with byte_enable !=0xff\n");
	sdma_v4_0_print_iv_entry(adev, entry);
	return 0;
}

static int sdma_v4_0_process_pool_timeout_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	dev_dbg_ratelimited(adev->dev,
		"Polling register/memory timeout executing POLL_REG/MEM with finite timer\n");
	sdma_v4_0_print_iv_entry(adev, entry);
	return 0;
}

static int sdma_v4_0_process_srbm_write_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	dev_dbg_ratelimited(adev->dev,
		"SDMA gets an Register Write SRBM_WRITE command in non-privilege command buffer\n");
	sdma_v4_0_print_iv_entry(adev, entry);
	return 0;
}

static void sdma_v4_0_update_medium_grain_clock_gating(
		struct amdgpu_device *adev,
		bool enable)
{
	uint32_t data, def;
	int i;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_SDMA_MGCG)) {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			def = data = RREG32_SDMA(i, mmSDMA0_CLK_CTRL);
			data &= ~(SDMA0_CLK_CTRL__SOFT_OVERRIDE7_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE6_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE5_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE4_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE3_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE2_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE1_MASK |
				  SDMA0_CLK_CTRL__SOFT_OVERRIDE0_MASK);
			if (def != data)
				WREG32_SDMA(i, mmSDMA0_CLK_CTRL, data);
		}
	} else {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			def = data = RREG32_SDMA(i, mmSDMA0_CLK_CTRL);
			data |= (SDMA0_CLK_CTRL__SOFT_OVERRIDE7_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE6_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE5_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE4_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE3_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE2_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE1_MASK |
				 SDMA0_CLK_CTRL__SOFT_OVERRIDE0_MASK);
			if (def != data)
				WREG32_SDMA(i, mmSDMA0_CLK_CTRL, data);
		}
	}
}


static void sdma_v4_0_update_medium_grain_light_sleep(
		struct amdgpu_device *adev,
		bool enable)
{
	uint32_t data, def;
	int i;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_SDMA_LS)) {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			/* 1-not override: enable sdma mem light sleep */
			def = data = RREG32_SDMA(0, mmSDMA0_POWER_CNTL);
			data |= SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;
			if (def != data)
				WREG32_SDMA(0, mmSDMA0_POWER_CNTL, data);
		}
	} else {
		for (i = 0; i < adev->sdma.num_instances; i++) {
		/* 0-override:disable sdma mem light sleep */
			def = data = RREG32_SDMA(0, mmSDMA0_POWER_CNTL);
			data &= ~SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK;
			if (def != data)
				WREG32_SDMA(0, mmSDMA0_POWER_CNTL, data);
		}
	}
}

static int sdma_v4_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		return 0;

	sdma_v4_0_update_medium_grain_clock_gating(adev,
			state == AMD_CG_STATE_GATE);
	sdma_v4_0_update_medium_grain_light_sleep(adev,
			state == AMD_CG_STATE_GATE);
	return 0;
}

static int sdma_v4_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (adev->ip_versions[SDMA0_HWIP][0]) {
	case IP_VERSION(4, 1, 0):
	case IP_VERSION(4, 1, 1):
	case IP_VERSION(4, 1, 2):
		sdma_v4_1_update_power_gating(adev,
				state == AMD_PG_STATE_GATE);
		break;
	default:
		break;
	}

	return 0;
}

static void sdma_v4_0_get_clockgating_state(void *handle, u32 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int data;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	/* AMD_CG_SUPPORT_SDMA_MGCG */
	data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_CLK_CTRL));
	if (!(data & SDMA0_CLK_CTRL__SOFT_OVERRIDE7_MASK))
		*flags |= AMD_CG_SUPPORT_SDMA_MGCG;

	/* AMD_CG_SUPPORT_SDMA_LS */
	data = RREG32(SOC15_REG_OFFSET(SDMA0, 0, mmSDMA0_POWER_CNTL));
	if (data & SDMA0_POWER_CNTL__MEM_POWER_OVERRIDE_MASK)
		*flags |= AMD_CG_SUPPORT_SDMA_LS;
}

const struct amd_ip_funcs sdma_v4_0_ip_funcs = {
	.name = "sdma_v4_0",
	.early_init = sdma_v4_0_early_init,
	.late_init = sdma_v4_0_late_init,
	.sw_init = sdma_v4_0_sw_init,
	.sw_fini = sdma_v4_0_sw_fini,
	.hw_init = sdma_v4_0_hw_init,
	.hw_fini = sdma_v4_0_hw_fini,
	.suspend = sdma_v4_0_suspend,
	.resume = sdma_v4_0_resume,
	.is_idle = sdma_v4_0_is_idle,
	.wait_for_idle = sdma_v4_0_wait_for_idle,
	.soft_reset = sdma_v4_0_soft_reset,
	.set_clockgating_state = sdma_v4_0_set_clockgating_state,
	.set_powergating_state = sdma_v4_0_set_powergating_state,
	.get_clockgating_state = sdma_v4_0_get_clockgating_state,
};

static const struct amdgpu_ring_funcs sdma_v4_0_ring_funcs = {
	.type = AMDGPU_RING_TYPE_SDMA,
	.align_mask = 0xf,
	.nop = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP),
	.support_64bit_ptrs = true,
	.vmhub = AMDGPU_MMHUB_0,
	.get_rptr = sdma_v4_0_ring_get_rptr,
	.get_wptr = sdma_v4_0_ring_get_wptr,
	.set_wptr = sdma_v4_0_ring_set_wptr,
	.emit_frame_size =
		6 + /* sdma_v4_0_ring_emit_hdp_flush */
		3 + /* hdp invalidate */
		6 + /* sdma_v4_0_ring_emit_pipeline_sync */
		/* sdma_v4_0_ring_emit_vm_flush */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 6 +
		10 + 10 + 10, /* sdma_v4_0_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 7 + 6, /* sdma_v4_0_ring_emit_ib */
	.emit_ib = sdma_v4_0_ring_emit_ib,
	.emit_fence = sdma_v4_0_ring_emit_fence,
	.emit_pipeline_sync = sdma_v4_0_ring_emit_pipeline_sync,
	.emit_vm_flush = sdma_v4_0_ring_emit_vm_flush,
	.emit_hdp_flush = sdma_v4_0_ring_emit_hdp_flush,
	.test_ring = sdma_v4_0_ring_test_ring,
	.test_ib = sdma_v4_0_ring_test_ib,
	.insert_nop = sdma_v4_0_ring_insert_nop,
	.pad_ib = sdma_v4_0_ring_pad_ib,
	.emit_wreg = sdma_v4_0_ring_emit_wreg,
	.emit_reg_wait = sdma_v4_0_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

/*
 * On Arcturus, SDMA instance 5~7 has a different vmhub type(AMDGPU_MMHUB_1).
 * So create a individual constant ring_funcs for those instances.
 */
static const struct amdgpu_ring_funcs sdma_v4_0_ring_funcs_2nd_mmhub = {
	.type = AMDGPU_RING_TYPE_SDMA,
	.align_mask = 0xf,
	.nop = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP),
	.support_64bit_ptrs = true,
	.vmhub = AMDGPU_MMHUB_1,
	.get_rptr = sdma_v4_0_ring_get_rptr,
	.get_wptr = sdma_v4_0_ring_get_wptr,
	.set_wptr = sdma_v4_0_ring_set_wptr,
	.emit_frame_size =
		6 + /* sdma_v4_0_ring_emit_hdp_flush */
		3 + /* hdp invalidate */
		6 + /* sdma_v4_0_ring_emit_pipeline_sync */
		/* sdma_v4_0_ring_emit_vm_flush */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 6 +
		10 + 10 + 10, /* sdma_v4_0_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 7 + 6, /* sdma_v4_0_ring_emit_ib */
	.emit_ib = sdma_v4_0_ring_emit_ib,
	.emit_fence = sdma_v4_0_ring_emit_fence,
	.emit_pipeline_sync = sdma_v4_0_ring_emit_pipeline_sync,
	.emit_vm_flush = sdma_v4_0_ring_emit_vm_flush,
	.emit_hdp_flush = sdma_v4_0_ring_emit_hdp_flush,
	.test_ring = sdma_v4_0_ring_test_ring,
	.test_ib = sdma_v4_0_ring_test_ib,
	.insert_nop = sdma_v4_0_ring_insert_nop,
	.pad_ib = sdma_v4_0_ring_pad_ib,
	.emit_wreg = sdma_v4_0_ring_emit_wreg,
	.emit_reg_wait = sdma_v4_0_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static const struct amdgpu_ring_funcs sdma_v4_0_page_ring_funcs = {
	.type = AMDGPU_RING_TYPE_SDMA,
	.align_mask = 0xf,
	.nop = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP),
	.support_64bit_ptrs = true,
	.vmhub = AMDGPU_MMHUB_0,
	.get_rptr = sdma_v4_0_ring_get_rptr,
	.get_wptr = sdma_v4_0_page_ring_get_wptr,
	.set_wptr = sdma_v4_0_page_ring_set_wptr,
	.emit_frame_size =
		6 + /* sdma_v4_0_ring_emit_hdp_flush */
		3 + /* hdp invalidate */
		6 + /* sdma_v4_0_ring_emit_pipeline_sync */
		/* sdma_v4_0_ring_emit_vm_flush */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 6 +
		10 + 10 + 10, /* sdma_v4_0_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 7 + 6, /* sdma_v4_0_ring_emit_ib */
	.emit_ib = sdma_v4_0_ring_emit_ib,
	.emit_fence = sdma_v4_0_ring_emit_fence,
	.emit_pipeline_sync = sdma_v4_0_ring_emit_pipeline_sync,
	.emit_vm_flush = sdma_v4_0_ring_emit_vm_flush,
	.emit_hdp_flush = sdma_v4_0_ring_emit_hdp_flush,
	.test_ring = sdma_v4_0_ring_test_ring,
	.test_ib = sdma_v4_0_ring_test_ib,
	.insert_nop = sdma_v4_0_ring_insert_nop,
	.pad_ib = sdma_v4_0_ring_pad_ib,
	.emit_wreg = sdma_v4_0_ring_emit_wreg,
	.emit_reg_wait = sdma_v4_0_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static const struct amdgpu_ring_funcs sdma_v4_0_page_ring_funcs_2nd_mmhub = {
	.type = AMDGPU_RING_TYPE_SDMA,
	.align_mask = 0xf,
	.nop = SDMA_PKT_NOP_HEADER_OP(SDMA_OP_NOP),
	.support_64bit_ptrs = true,
	.vmhub = AMDGPU_MMHUB_1,
	.get_rptr = sdma_v4_0_ring_get_rptr,
	.get_wptr = sdma_v4_0_page_ring_get_wptr,
	.set_wptr = sdma_v4_0_page_ring_set_wptr,
	.emit_frame_size =
		6 + /* sdma_v4_0_ring_emit_hdp_flush */
		3 + /* hdp invalidate */
		6 + /* sdma_v4_0_ring_emit_pipeline_sync */
		/* sdma_v4_0_ring_emit_vm_flush */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 6 +
		10 + 10 + 10, /* sdma_v4_0_ring_emit_fence x3 for user fence, vm fence */
	.emit_ib_size = 7 + 6, /* sdma_v4_0_ring_emit_ib */
	.emit_ib = sdma_v4_0_ring_emit_ib,
	.emit_fence = sdma_v4_0_ring_emit_fence,
	.emit_pipeline_sync = sdma_v4_0_ring_emit_pipeline_sync,
	.emit_vm_flush = sdma_v4_0_ring_emit_vm_flush,
	.emit_hdp_flush = sdma_v4_0_ring_emit_hdp_flush,
	.test_ring = sdma_v4_0_ring_test_ring,
	.test_ib = sdma_v4_0_ring_test_ib,
	.insert_nop = sdma_v4_0_ring_insert_nop,
	.pad_ib = sdma_v4_0_ring_pad_ib,
	.emit_wreg = sdma_v4_0_ring_emit_wreg,
	.emit_reg_wait = sdma_v4_0_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static void sdma_v4_0_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (adev->ip_versions[SDMA0_HWIP][0] == IP_VERSION(4, 2, 2) && i >= 5)
			adev->sdma.instance[i].ring.funcs =
					&sdma_v4_0_ring_funcs_2nd_mmhub;
		else
			adev->sdma.instance[i].ring.funcs =
					&sdma_v4_0_ring_funcs;
		adev->sdma.instance[i].ring.me = i;
		if (adev->sdma.has_page_queue) {
			if (adev->ip_versions[SDMA0_HWIP][0] == IP_VERSION(4, 2, 2) && i >= 5)
				adev->sdma.instance[i].page.funcs =
					&sdma_v4_0_page_ring_funcs_2nd_mmhub;
			else
				adev->sdma.instance[i].page.funcs =
					&sdma_v4_0_page_ring_funcs;
			adev->sdma.instance[i].page.me = i;
		}
	}
}

static const struct amdgpu_irq_src_funcs sdma_v4_0_trap_irq_funcs = {
	.set = sdma_v4_0_set_trap_irq_state,
	.process = sdma_v4_0_process_trap_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_0_illegal_inst_irq_funcs = {
	.process = sdma_v4_0_process_illegal_inst_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_0_ecc_irq_funcs = {
	.set = sdma_v4_0_set_ecc_irq_state,
	.process = amdgpu_sdma_process_ecc_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_0_vm_hole_irq_funcs = {
	.process = sdma_v4_0_process_vm_hole_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_0_doorbell_invalid_irq_funcs = {
	.process = sdma_v4_0_process_doorbell_invalid_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_0_pool_timeout_irq_funcs = {
	.process = sdma_v4_0_process_pool_timeout_irq,
};

static const struct amdgpu_irq_src_funcs sdma_v4_0_srbm_write_irq_funcs = {
	.process = sdma_v4_0_process_srbm_write_irq,
};

static void sdma_v4_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->sdma.trap_irq.num_types = adev->sdma.num_instances;
	adev->sdma.ecc_irq.num_types = adev->sdma.num_instances;
	/*For Arcturus and Aldebaran, add another 4 irq handler*/
	switch (adev->sdma.num_instances) {
	case 5:
	case 8:
		adev->sdma.vm_hole_irq.num_types = adev->sdma.num_instances;
		adev->sdma.doorbell_invalid_irq.num_types = adev->sdma.num_instances;
		adev->sdma.pool_timeout_irq.num_types = adev->sdma.num_instances;
		adev->sdma.srbm_write_irq.num_types = adev->sdma.num_instances;
		break;
	default:
		break;
	}
	adev->sdma.trap_irq.funcs = &sdma_v4_0_trap_irq_funcs;
	adev->sdma.illegal_inst_irq.funcs = &sdma_v4_0_illegal_inst_irq_funcs;
	adev->sdma.ecc_irq.funcs = &sdma_v4_0_ecc_irq_funcs;
	adev->sdma.vm_hole_irq.funcs = &sdma_v4_0_vm_hole_irq_funcs;
	adev->sdma.doorbell_invalid_irq.funcs = &sdma_v4_0_doorbell_invalid_irq_funcs;
	adev->sdma.pool_timeout_irq.funcs = &sdma_v4_0_pool_timeout_irq_funcs;
	adev->sdma.srbm_write_irq.funcs = &sdma_v4_0_srbm_write_irq_funcs;
}

/**
 * sdma_v4_0_emit_copy_buffer - copy buffer using the sDMA engine
 *
 * @ib: indirect buffer to copy to
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 * @tmz: if a secure copy should be used
 *
 * Copy GPU buffers using the DMA engine (VEGA10/12).
 * Used by the amdgpu ttm implementation to move pages if
 * registered as the asic copy callback.
 */
static void sdma_v4_0_emit_copy_buffer(struct amdgpu_ib *ib,
				       uint64_t src_offset,
				       uint64_t dst_offset,
				       uint32_t byte_count,
				       bool tmz)
{
	ib->ptr[ib->length_dw++] = SDMA_PKT_HEADER_OP(SDMA_OP_COPY) |
		SDMA_PKT_HEADER_SUB_OP(SDMA_SUBOP_COPY_LINEAR) |
		SDMA_PKT_COPY_LINEAR_HEADER_TMZ(tmz ? 1 : 0);
	ib->ptr[ib->length_dw++] = byte_count - 1;
	ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
	ib->ptr[ib->length_dw++] = lower_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset);
}

/**
 * sdma_v4_0_emit_fill_buffer - fill buffer using the sDMA engine
 *
 * @ib: indirect buffer to copy to
 * @src_data: value to write to buffer
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Fill GPU buffers using the DMA engine (VEGA10/12).
 */
static void sdma_v4_0_emit_fill_buffer(struct amdgpu_ib *ib,
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

static const struct amdgpu_buffer_funcs sdma_v4_0_buffer_funcs = {
	.copy_max_bytes = 0x400000,
	.copy_num_dw = 7,
	.emit_copy_buffer = sdma_v4_0_emit_copy_buffer,

	.fill_max_bytes = 0x400000,
	.fill_num_dw = 5,
	.emit_fill_buffer = sdma_v4_0_emit_fill_buffer,
};

static void sdma_v4_0_set_buffer_funcs(struct amdgpu_device *adev)
{
	adev->mman.buffer_funcs = &sdma_v4_0_buffer_funcs;
	if (adev->sdma.has_page_queue)
		adev->mman.buffer_funcs_ring = &adev->sdma.instance[0].page;
	else
		adev->mman.buffer_funcs_ring = &adev->sdma.instance[0].ring;
}

static const struct amdgpu_vm_pte_funcs sdma_v4_0_vm_pte_funcs = {
	.copy_pte_num_dw = 7,
	.copy_pte = sdma_v4_0_vm_copy_pte,

	.write_pte = sdma_v4_0_vm_write_pte,
	.set_pte_pde = sdma_v4_0_vm_set_pte_pde,
};

static void sdma_v4_0_set_vm_pte_funcs(struct amdgpu_device *adev)
{
	struct drm_gpu_scheduler *sched;
	unsigned i;

	adev->vm_manager.vm_pte_funcs = &sdma_v4_0_vm_pte_funcs;
	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (adev->sdma.has_page_queue)
			sched = &adev->sdma.instance[i].page.sched;
		else
			sched = &adev->sdma.instance[i].ring.sched;
		adev->vm_manager.vm_pte_scheds[i] = sched;
	}
	adev->vm_manager.vm_pte_num_scheds = adev->sdma.num_instances;
}

static void sdma_v4_0_get_ras_error_count(uint32_t value,
					uint32_t instance,
					uint32_t *sec_count)
{
	uint32_t i;
	uint32_t sec_cnt;

	/* double bits error (multiple bits) error detection is not supported */
	for (i = 0; i < ARRAY_SIZE(sdma_v4_0_ras_fields); i++) {
		/* the SDMA_EDC_COUNTER register in each sdma instance
		 * shares the same sed shift_mask
		 * */
		sec_cnt = (value &
			sdma_v4_0_ras_fields[i].sec_count_mask) >>
			sdma_v4_0_ras_fields[i].sec_count_shift;
		if (sec_cnt) {
			DRM_INFO("Detected %s in SDMA%d, SED %d\n",
				sdma_v4_0_ras_fields[i].name,
				instance, sec_cnt);
			*sec_count += sec_cnt;
		}
	}
}

static int sdma_v4_0_query_ras_error_count_by_instance(struct amdgpu_device *adev,
			uint32_t instance, void *ras_error_status)
{
	struct ras_err_data *err_data = (struct ras_err_data *)ras_error_status;
	uint32_t sec_count = 0;
	uint32_t reg_value = 0;

	reg_value = RREG32_SDMA(instance, mmSDMA0_EDC_COUNTER);
	/* double bit error is not supported */
	if (reg_value)
		sdma_v4_0_get_ras_error_count(reg_value,
				instance, &sec_count);
	/* err_data->ce_count should be initialized to 0
	 * before calling into this function */
	err_data->ce_count += sec_count;
	/* double bit error is not supported
	 * set ue count to 0 */
	err_data->ue_count = 0;

	return 0;
};

static void sdma_v4_0_query_ras_error_count(struct amdgpu_device *adev,  void *ras_error_status)
{
	int i = 0;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		if (sdma_v4_0_query_ras_error_count_by_instance(adev, i, ras_error_status)) {
			dev_err(adev->dev, "Query ras error count failed in SDMA%d\n", i);
			return;
		}
	}
}

static void sdma_v4_0_reset_ras_error_count(struct amdgpu_device *adev)
{
	int i;

	/* read back edc counter registers to clear the counters */
	if (amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__SDMA)) {
		for (i = 0; i < adev->sdma.num_instances; i++)
			RREG32_SDMA(i, mmSDMA0_EDC_COUNTER);
	}
}

const struct amdgpu_ras_block_hw_ops sdma_v4_0_ras_hw_ops = {
	.query_ras_error_count = sdma_v4_0_query_ras_error_count,
	.reset_ras_error_count = sdma_v4_0_reset_ras_error_count,
};

static struct amdgpu_sdma_ras sdma_v4_0_ras = {
	.ras_block = {
		.hw_ops = &sdma_v4_0_ras_hw_ops,
	},
};

static void sdma_v4_0_set_ras_funcs(struct amdgpu_device *adev)
{
	switch (adev->ip_versions[SDMA0_HWIP][0]) {
	case IP_VERSION(4, 2, 0):
	case IP_VERSION(4, 2, 2):
		adev->sdma.ras = &sdma_v4_0_ras;
		break;
	case IP_VERSION(4, 4, 0):
		adev->sdma.ras = &sdma_v4_4_ras;
		break;
	default:
		break;
	}

	if (adev->sdma.ras) {
		amdgpu_ras_register_ras_block(adev, &adev->sdma.ras->ras_block);

		strcpy(adev->sdma.ras->ras_block.name, "sdma");
		adev->sdma.ras->ras_block.block = AMDGPU_RAS_BLOCK__SDMA;

		/* If don't define special ras_late_init function, use default ras_late_init */
		if (!adev->sdma.ras->ras_block.ras_late_init)
			adev->sdma.ras->ras_block.ras_late_init = amdgpu_sdma_ras_late_init;

		/* If don't define special ras_fini function, use default ras_fini */
		if (!adev->sdma.ras->ras_block.ras_fini)
			adev->sdma.ras->ras_block.ras_fini = amdgpu_sdma_ras_fini;
	}
}

const struct amdgpu_ip_block_version sdma_v4_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_SDMA,
	.major = 4,
	.minor = 0,
	.rev = 0,
	.funcs = &sdma_v4_0_ip_funcs,
};
