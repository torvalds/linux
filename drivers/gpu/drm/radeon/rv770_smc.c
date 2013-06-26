/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */

#include <linux/firmware.h>
#include "drmP.h"
#include "radeon.h"
#include "rv770d.h"
#include "rv770_dpm.h"
#include "rv770_smc.h"
#include "atom.h"
#include "radeon_ucode.h"

#define FIRST_SMC_INT_VECT_REG 0xFFD8
#define FIRST_INT_VECT_S19     0xFFC0

static const u8 rv770_smc_int_vectors[] =
{
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x0C, 0xD7,
	0x08, 0x2B, 0x08, 0x10,
	0x03, 0x51, 0x03, 0x51,
	0x03, 0x51, 0x03, 0x51
};

static const u8 rv730_smc_int_vectors[] =
{
	0x08, 0x15, 0x08, 0x15,
	0x08, 0x15, 0x08, 0x15,
	0x08, 0x15, 0x08, 0x15,
	0x08, 0x15, 0x08, 0x15,
	0x08, 0x15, 0x08, 0x15,
	0x08, 0x15, 0x08, 0x15,
	0x08, 0x15, 0x08, 0x15,
	0x08, 0x15, 0x08, 0x15,
	0x08, 0x15, 0x08, 0x15,
	0x08, 0x15, 0x08, 0x15,
	0x08, 0x15, 0x08, 0x15,
	0x08, 0x15, 0x08, 0x15,
	0x08, 0x15, 0x0C, 0xBB,
	0x08, 0x30, 0x08, 0x15,
	0x03, 0x56, 0x03, 0x56,
	0x03, 0x56, 0x03, 0x56
};

static const u8 rv710_smc_int_vectors[] =
{
	0x08, 0x04, 0x08, 0x04,
	0x08, 0x04, 0x08, 0x04,
	0x08, 0x04, 0x08, 0x04,
	0x08, 0x04, 0x08, 0x04,
	0x08, 0x04, 0x08, 0x04,
	0x08, 0x04, 0x08, 0x04,
	0x08, 0x04, 0x08, 0x04,
	0x08, 0x04, 0x08, 0x04,
	0x08, 0x04, 0x08, 0x04,
	0x08, 0x04, 0x08, 0x04,
	0x08, 0x04, 0x08, 0x04,
	0x08, 0x04, 0x08, 0x04,
	0x08, 0x04, 0x0C, 0xCB,
	0x08, 0x1F, 0x08, 0x04,
	0x03, 0x51, 0x03, 0x51,
	0x03, 0x51, 0x03, 0x51
};

static const u8 rv740_smc_int_vectors[] =
{
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x08, 0x10,
	0x08, 0x10, 0x0C, 0xD7,
	0x08, 0x2B, 0x08, 0x10,
	0x03, 0x51, 0x03, 0x51,
	0x03, 0x51, 0x03, 0x51
};

int rv770_set_smc_sram_address(struct radeon_device *rdev,
			       u16 smc_address, u16 limit)
{
	u32 addr;

	if (smc_address & 3)
		return -EINVAL;
	if ((smc_address + 3) > limit)
		return -EINVAL;

	addr = smc_address;
	addr |= SMC_SRAM_AUTO_INC_DIS;

	WREG32(SMC_SRAM_ADDR, addr);

	return 0;
}

int rv770_copy_bytes_to_smc(struct radeon_device *rdev,
			    u16 smc_start_address, const u8 *src,
			    u16 byte_count, u16 limit)
{
	u32 data, original_data, extra_shift;
	u16 addr;
	int ret;

	if (smc_start_address & 3)
		return -EINVAL;
	if ((smc_start_address + byte_count) > limit)
		return -EINVAL;

	addr = smc_start_address;

	while (byte_count >= 4) {
		/* SMC address space is BE */
		data = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];

		ret = rv770_set_smc_sram_address(rdev, addr, limit);
		if (ret)
			return ret;

		WREG32(SMC_SRAM_DATA, data);

		src += 4;
		byte_count -= 4;
		addr += 4;
	}

	/* RMW for final bytes */
	if (byte_count > 0) {
		data = 0;

		ret = rv770_set_smc_sram_address(rdev, addr, limit);
		if (ret)
			return ret;

		original_data = RREG32(SMC_SRAM_DATA);

		extra_shift = 8 * (4 - byte_count);

		while (byte_count > 0) {
			/* SMC address space is BE */
			data = (data << 8) + *src++;
			byte_count--;
		}

		data <<= extra_shift;

		data |= (original_data & ~((~0UL) << extra_shift));

		ret = rv770_set_smc_sram_address(rdev, addr, limit);
		if (ret)
			return ret;

		WREG32(SMC_SRAM_DATA, data);
	}

	return 0;
}

static int rv770_program_interrupt_vectors(struct radeon_device *rdev,
					   u32 smc_first_vector, const u8 *src,
					   u32 byte_count)
{
	u32 tmp, i;

	if (byte_count % 4)
		return -EINVAL;

	if (smc_first_vector < FIRST_SMC_INT_VECT_REG) {
		tmp = FIRST_SMC_INT_VECT_REG - smc_first_vector;

		if (tmp > byte_count)
			return 0;

		byte_count -= tmp;
		src += tmp;
		smc_first_vector = FIRST_SMC_INT_VECT_REG;
	}

	for (i = 0; i < byte_count; i += 4) {
		/* SMC address space is BE */
		tmp = (src[i] << 24) | (src[i + 1] << 16) | (src[i + 2] << 8) | src[i + 3];

		WREG32(SMC_ISR_FFD8_FFDB + i, tmp);
	}

	return 0;
}

void rv770_start_smc(struct radeon_device *rdev)
{
	WREG32_P(SMC_IO, SMC_RST_N, ~SMC_RST_N);
}

void rv770_reset_smc(struct radeon_device *rdev)
{
	WREG32_P(SMC_IO, 0, ~SMC_RST_N);
}

void rv770_stop_smc_clock(struct radeon_device *rdev)
{
	WREG32_P(SMC_IO, 0, ~SMC_CLK_EN);
}

void rv770_start_smc_clock(struct radeon_device *rdev)
{
	WREG32_P(SMC_IO, SMC_CLK_EN, ~SMC_CLK_EN);
}

bool rv770_is_smc_running(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32(SMC_IO);

	if ((tmp & SMC_RST_N) && (tmp & SMC_CLK_EN))
		return true;
	else
		return false;
}

PPSMC_Result rv770_send_msg_to_smc(struct radeon_device *rdev, PPSMC_Msg msg)
{
	u32 tmp;
	int i;
	PPSMC_Result result;

	if (!rv770_is_smc_running(rdev))
		return PPSMC_Result_Failed;

	WREG32_P(SMC_MSG, HOST_SMC_MSG(msg), ~HOST_SMC_MSG_MASK);

	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(SMC_MSG) & HOST_SMC_RESP_MASK;
		tmp >>= HOST_SMC_RESP_SHIFT;
		if (tmp != 0)
			break;
		udelay(1);
	}

	tmp = RREG32(SMC_MSG) & HOST_SMC_RESP_MASK;
	tmp >>= HOST_SMC_RESP_SHIFT;

	result = (PPSMC_Result)tmp;
	return result;
}

PPSMC_Result rv770_wait_for_smc_inactive(struct radeon_device *rdev)
{
	int i;
	PPSMC_Result result = PPSMC_Result_OK;

	if (!rv770_is_smc_running(rdev))
		return result;

	for (i = 0; i < rdev->usec_timeout; i++) {
		if (RREG32(SMC_IO) & SMC_STOP_MODE)
			break;
		udelay(1);
	}

	return result;
}

static void rv770_clear_smc_sram(struct radeon_device *rdev, u16 limit)
{
	u16 i;

	for (i = 0;  i < limit; i += 4) {
		rv770_set_smc_sram_address(rdev, i, limit);
		WREG32(SMC_SRAM_DATA, 0);
	}
}

int rv770_load_smc_ucode(struct radeon_device *rdev,
			 u16 limit)
{
	int ret;
	const u8 *int_vect;
	u16 int_vect_start_address;
	u16 int_vect_size;
	const u8 *ucode_data;
	u16 ucode_start_address;
	u16 ucode_size;

	if (!rdev->smc_fw)
		return -EINVAL;

	rv770_clear_smc_sram(rdev, limit);

	switch (rdev->family) {
	case CHIP_RV770:
		ucode_start_address = RV770_SMC_UCODE_START;
		ucode_size = RV770_SMC_UCODE_SIZE;
		int_vect = (const u8 *)&rv770_smc_int_vectors;
		int_vect_start_address = RV770_SMC_INT_VECTOR_START;
		int_vect_size = RV770_SMC_INT_VECTOR_SIZE;
		break;
	case CHIP_RV730:
		ucode_start_address = RV730_SMC_UCODE_START;
		ucode_size = RV730_SMC_UCODE_SIZE;
		int_vect = (const u8 *)&rv730_smc_int_vectors;
		int_vect_start_address = RV730_SMC_INT_VECTOR_START;
		int_vect_size = RV730_SMC_INT_VECTOR_SIZE;
		break;
	case CHIP_RV710:
		ucode_start_address = RV710_SMC_UCODE_START;
		ucode_size = RV710_SMC_UCODE_SIZE;
		int_vect = (const u8 *)&rv710_smc_int_vectors;
		int_vect_start_address = RV710_SMC_INT_VECTOR_START;
		int_vect_size = RV710_SMC_INT_VECTOR_SIZE;
		break;
	case CHIP_RV740:
		ucode_start_address = RV740_SMC_UCODE_START;
		ucode_size = RV740_SMC_UCODE_SIZE;
		int_vect = (const u8 *)&rv740_smc_int_vectors;
		int_vect_start_address = RV740_SMC_INT_VECTOR_START;
		int_vect_size = RV740_SMC_INT_VECTOR_SIZE;
		break;
	default:
		DRM_ERROR("unknown asic in smc ucode loader\n");
		BUG();
	}

	/* load the ucode */
	ucode_data = (const u8 *)rdev->smc_fw->data;
	ret = rv770_copy_bytes_to_smc(rdev, ucode_start_address,
				      ucode_data, ucode_size, limit);
	if (ret)
		return ret;

	/* set up the int vectors */
	ret = rv770_program_interrupt_vectors(rdev, int_vect_start_address,
					      int_vect, int_vect_size);
	if (ret)
		return ret;

	return 0;
}

int rv770_read_smc_sram_dword(struct radeon_device *rdev,
			      u16 smc_address, u32 *value, u16 limit)
{
	int ret;

	ret = rv770_set_smc_sram_address(rdev, smc_address, limit);
	if (ret)
		return ret;

	*value = RREG32(SMC_SRAM_DATA);

	return 0;
}

int rv770_write_smc_sram_dword(struct radeon_device *rdev,
			       u16 smc_address, u32 value, u16 limit)
{
	int ret;

	ret = rv770_set_smc_sram_address(rdev, smc_address, limit);
	if (ret)
		return ret;

	WREG32(SMC_SRAM_DATA, value);

	return 0;
}
