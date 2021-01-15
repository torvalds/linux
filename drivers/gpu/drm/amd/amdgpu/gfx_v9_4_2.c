/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#include "amdgpu.h"
#include "soc15.h"

#include "gc/gc_9_4_2_offset.h"
#include "gc/gc_9_4_2_sh_mask.h"
#include "gfx_v9_0.h"

static const struct soc15_reg_golden golden_settings_gc_9_4_2_alde_die_0[] = {
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_0, 0x3fffffff, 0x141dc920),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_1, 0x3fffffff, 0x3b458b93),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_2, 0x3fffffff, 0x1a4f5583),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_3, 0x3fffffff, 0x317717f6),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_4, 0x3fffffff, 0x107cc1e6),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_5, 0x3ff, 0x351),
};

static const struct soc15_reg_golden golden_settings_gc_9_4_2_alde_die_1[] = {
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_0, 0x3fffffff, 0x2591aa38),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_1, 0x3fffffff, 0xac9e88b),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_2, 0x3fffffff, 0x2bc3369b),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_3, 0x3fffffff, 0xfb74ee),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_4, 0x3fffffff, 0x21f0a2fe),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_5, 0x3ff, 0x49),
};

static const struct soc15_reg_golden golden_settings_gc_9_4_2_alde[] = {
	SOC15_REG_GOLDEN_VALUE(GC, 0, regGB_ADDR_CONFIG, 0xffff77ff, 0x2a114042),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTA_CNTL_AUX, 0xfffffeef, 0x10b0000),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_UTCL1_CNTL1, 0xffffffff, 0x30800400),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCI_CNTL_3, 0xff, 0x20),
};

void gfx_v9_4_2_init_golden_registers(struct amdgpu_device *adev,
				      uint32_t die_id)
{
	soc15_program_register_sequence(adev,
					golden_settings_gc_9_4_2_alde,
					ARRAY_SIZE(golden_settings_gc_9_4_2_alde));

	/* apply golden settings per die */
	switch (die_id) {
	case 0:
		soc15_program_register_sequence(adev,
				golden_settings_gc_9_4_2_alde_die_0,
				ARRAY_SIZE(golden_settings_gc_9_4_2_alde_die_0));
		break;
	case 1:
		soc15_program_register_sequence(adev,
				golden_settings_gc_9_4_2_alde_die_1,
				ARRAY_SIZE(golden_settings_gc_9_4_2_alde_die_1));
		break;
	default:
		dev_warn(adev->dev,
			 "invalid die id %d, ignore channel fabricid remap settings\n",
			 die_id);
		break;
	}

	return;
}

void gfx_v9_4_2_debug_trap_config_init(struct amdgpu_device *adev,
				uint32_t first_vmid,
				uint32_t last_vmid)
{
	uint32_t data;
	int i;

	mutex_lock(&adev->srbm_mutex);

	for (i = first_vmid; i < last_vmid; i++) {
		data = 0;
		soc15_grbm_select(adev, 0, 0, 0, i);
		data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, TRAP_EN, 1);
		data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_EN, 0);
		data = REG_SET_FIELD(data, SPI_GDBG_PER_VMID_CNTL, EXCP_REPLACE,
					0);
		WREG32(SOC15_REG_OFFSET(GC, 0, regSPI_GDBG_PER_VMID_CNTL), data);
	}

	soc15_grbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);
}

void gfx_v9_4_2_set_power_brake_sequence(struct amdgpu_device *adev)
{
	u32 tmp;

	gfx_v9_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);

	tmp = 0;
	tmp = REG_SET_FIELD(tmp, GC_THROTTLE_CTRL, PATTERN_MODE, 1);
	WREG32_SOC15(GC, 0, regGC_THROTTLE_CTRL, tmp);

	tmp = 0;
	tmp = REG_SET_FIELD(tmp, GC_THROTTLE_CTRL1, PWRBRK_STALL_EN, 1);
	WREG32_SOC15(GC, 0, regGC_THROTTLE_CTRL1, tmp);

	WREG32_SOC15(GC, 0, regDIDT_IND_INDEX, ixDIDT_SQ_THROTTLE_CTRL);
	tmp = 0;
	tmp = REG_SET_FIELD(tmp, DIDT_SQ_THROTTLE_CTRL, PWRBRK_STALL_EN, 1);
	WREG32_SOC15(GC, 0, regDIDT_IND_DATA, tmp);

	WREG32_SOC15(GC, 0, regGC_CAC_IND_INDEX, ixPWRBRK_STALL_PATTERN_CTRL);
	tmp = 0;
	tmp = REG_SET_FIELD(tmp, PWRBRK_STALL_PATTERN_CTRL, PWRBRK_END_STEP, 0x12);
	WREG32_SOC15(GC, 0, regGC_CAC_IND_DATA, tmp);
}
