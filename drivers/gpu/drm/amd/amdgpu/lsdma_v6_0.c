/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#include "amdgpu.h"
#include "lsdma_v6_0.h"
#include "amdgpu_lsdma.h"

#include "lsdma/lsdma_6_0_0_offset.h"
#include "lsdma/lsdma_6_0_0_sh_mask.h"

static int lsdma_v6_0_wait_pio_status(struct amdgpu_device *adev)
{
	return amdgpu_lsdma_wait_for(adev, SOC15_REG_OFFSET(LSDMA, 0, regLSDMA_PIO_STATUS),
			LSDMA_PIO_STATUS__PIO_IDLE_MASK | LSDMA_PIO_STATUS__PIO_FIFO_EMPTY_MASK,
			LSDMA_PIO_STATUS__PIO_IDLE_MASK | LSDMA_PIO_STATUS__PIO_FIFO_EMPTY_MASK);
}

static int lsdma_v6_0_copy_mem(struct amdgpu_device *adev,
			       uint64_t src_addr,
			       uint64_t dst_addr,
			       uint64_t size)
{
	int ret;
	uint32_t tmp;

	WREG32_SOC15(LSDMA, 0, regLSDMA_PIO_SRC_ADDR_LO, lower_32_bits(src_addr));
	WREG32_SOC15(LSDMA, 0, regLSDMA_PIO_SRC_ADDR_HI, upper_32_bits(src_addr));

	WREG32_SOC15(LSDMA, 0, regLSDMA_PIO_DST_ADDR_LO, lower_32_bits(dst_addr));
	WREG32_SOC15(LSDMA, 0, regLSDMA_PIO_DST_ADDR_HI, upper_32_bits(dst_addr));

	WREG32_SOC15(LSDMA, 0, regLSDMA_PIO_CONTROL, 0x0);

	tmp = RREG32_SOC15(LSDMA, 0, regLSDMA_PIO_COMMAND);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, BYTE_COUNT, size);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, SRC_LOCATION, 0);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, DST_LOCATION, 0);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, SRC_ADDR_INC, 0);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, DST_ADDR_INC, 0);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, OVERLAP_DISABLE, 0);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, CONSTANT_FILL, 0);
	WREG32_SOC15(LSDMA, 0, regLSDMA_PIO_COMMAND, tmp);

	ret = lsdma_v6_0_wait_pio_status(adev);
	if (ret)
		dev_err(adev->dev, "LSDMA PIO failed to copy memory!\n");

	return ret;
}

static int lsdma_v6_0_fill_mem(struct amdgpu_device *adev,
			       uint64_t dst_addr,
			       uint32_t data,
			       uint64_t size)
{
	int ret;
	uint32_t tmp;

	WREG32_SOC15(LSDMA, 0, regLSDMA_PIO_CONSTFILL_DATA, data);

	WREG32_SOC15(LSDMA, 0, regLSDMA_PIO_DST_ADDR_LO, lower_32_bits(dst_addr));
	WREG32_SOC15(LSDMA, 0, regLSDMA_PIO_DST_ADDR_HI, upper_32_bits(dst_addr));

	WREG32_SOC15(LSDMA, 0, regLSDMA_PIO_CONTROL, 0x0);

	tmp = RREG32_SOC15(LSDMA, 0, regLSDMA_PIO_COMMAND);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, BYTE_COUNT, size);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, SRC_LOCATION, 0);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, DST_LOCATION, 0);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, SRC_ADDR_INC, 0);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, DST_ADDR_INC, 0);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, OVERLAP_DISABLE, 0);
	tmp = REG_SET_FIELD(tmp, LSDMA_PIO_COMMAND, CONSTANT_FILL, 1);
	WREG32_SOC15(LSDMA, 0, regLSDMA_PIO_COMMAND, tmp);

	ret = lsdma_v6_0_wait_pio_status(adev);
	if (ret)
		dev_err(adev->dev, "LSDMA PIO failed to fill memory!\n");

	return ret;
}

static void lsdma_v6_0_update_memory_power_gating(struct amdgpu_device *adev,
						 bool enable)
{
	uint32_t tmp;

	tmp = RREG32_SOC15(LSDMA, 0, regLSDMA_MEM_POWER_CTRL);
	tmp = REG_SET_FIELD(tmp, LSDMA_MEM_POWER_CTRL, MEM_POWER_CTRL_EN, 0);
	WREG32_SOC15(LSDMA, 0, regLSDMA_MEM_POWER_CTRL, tmp);

	tmp = REG_SET_FIELD(tmp, LSDMA_MEM_POWER_CTRL, MEM_POWER_CTRL_EN, enable);
	WREG32_SOC15(LSDMA, 0, regLSDMA_MEM_POWER_CTRL, tmp);
}

const struct amdgpu_lsdma_funcs lsdma_v6_0_funcs = {
	.copy_mem = lsdma_v6_0_copy_mem,
	.fill_mem = lsdma_v6_0_fill_mem,
	.update_memory_power_gating = lsdma_v6_0_update_memory_power_gating
};
