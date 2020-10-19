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
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_1, 0x3fffffff, 0xac9688B),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_2, 0x3fffffff, 0x2bc3369B),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_3, 0x3fffffff, 0xfb74ee),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_4, 0x3fffffff, 0x21f0a2fe),
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_CHAN_STEER_5, 0x3ff, 0x49),
};

static const struct soc15_reg_golden golden_settings_gc_9_4_2_alde[] = {
	SOC15_REG_GOLDEN_VALUE(GC, 0, regTCP_UTCL1_CNTL1, 0x30000000, 0x30000000),
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
