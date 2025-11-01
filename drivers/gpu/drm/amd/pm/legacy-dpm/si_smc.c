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

#include "amdgpu.h"
#include "sid.h"
#include "ppsmc.h"
#include "amdgpu_ucode.h"
#include "sislands_smc.h"

#include "smu/smu_6_0_d.h"
#include "smu/smu_6_0_sh_mask.h"

#include "gca/gfx_6_0_d.h"
#include "gca/gfx_6_0_sh_mask.h"

static int si_set_smc_sram_address(struct amdgpu_device *adev,
				   u32 smc_address, u32 limit)
{
	if (smc_address & 3)
		return -EINVAL;
	if ((smc_address + 3) > limit)
		return -EINVAL;

	WREG32(mmSMC_IND_INDEX_0, smc_address);
	WREG32_P(mmSMC_IND_ACCESS_CNTL, 0, ~SMC_IND_ACCESS_CNTL__AUTO_INCREMENT_IND_0_MASK);

	return 0;
}

int amdgpu_si_copy_bytes_to_smc(struct amdgpu_device *adev,
				u32 smc_start_address,
				const u8 *src, u32 byte_count, u32 limit)
{
	unsigned long flags;
	int ret = 0;
	u32 data, original_data, addr, extra_shift;

	if (smc_start_address & 3)
		return -EINVAL;
	if ((smc_start_address + byte_count) > limit)
		return -EINVAL;

	addr = smc_start_address;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	while (byte_count >= 4) {
		/* SMC address space is BE */
		data = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];

		ret = si_set_smc_sram_address(adev, addr, limit);
		if (ret)
			goto done;

		WREG32(mmSMC_IND_DATA_0, data);

		src += 4;
		byte_count -= 4;
		addr += 4;
	}

	/* RMW for the final bytes */
	if (byte_count > 0) {
		data = 0;

		ret = si_set_smc_sram_address(adev, addr, limit);
		if (ret)
			goto done;

		original_data = RREG32(mmSMC_IND_DATA_0);
		extra_shift = 8 * (4 - byte_count);

		while (byte_count > 0) {
			/* SMC address space is BE */
			data = (data << 8) + *src++;
			byte_count--;
		}

		data <<= extra_shift;
		data |= (original_data & ~((~0UL) << extra_shift));

		ret = si_set_smc_sram_address(adev, addr, limit);
		if (ret)
			goto done;

		WREG32(mmSMC_IND_DATA_0, data);
	}

done:
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);

	return ret;
}

void amdgpu_si_start_smc(struct amdgpu_device *adev)
{
	u32 tmp = RREG32_SMC(SMC_SYSCON_RESET_CNTL);

	tmp &= ~RST_REG;

	WREG32_SMC(SMC_SYSCON_RESET_CNTL, tmp);
}

void amdgpu_si_reset_smc(struct amdgpu_device *adev)
{
	u32 tmp;

	RREG32(mmCB_CGTT_SCLK_CTRL);
	RREG32(mmCB_CGTT_SCLK_CTRL);
	RREG32(mmCB_CGTT_SCLK_CTRL);
	RREG32(mmCB_CGTT_SCLK_CTRL);

	tmp = RREG32_SMC(SMC_SYSCON_RESET_CNTL) |
	      RST_REG;
	WREG32_SMC(SMC_SYSCON_RESET_CNTL, tmp);
}

int amdgpu_si_program_jump_on_start(struct amdgpu_device *adev)
{
	static const u8 data[] = { 0x0E, 0x00, 0x40, 0x40 };

	return amdgpu_si_copy_bytes_to_smc(adev, 0x0, data, 4, sizeof(data)+1);
}

void amdgpu_si_smc_clock(struct amdgpu_device *adev, bool enable)
{
	u32 tmp = RREG32_SMC(SMC_SYSCON_CLOCK_CNTL_0);

	if (enable)
		tmp &= ~CK_DISABLE;
	else
		tmp |= CK_DISABLE;

	WREG32_SMC(SMC_SYSCON_CLOCK_CNTL_0, tmp);
}

bool amdgpu_si_is_smc_running(struct amdgpu_device *adev)
{
	u32 rst = RREG32_SMC(SMC_SYSCON_RESET_CNTL);
	u32 clk = RREG32_SMC(SMC_SYSCON_CLOCK_CNTL_0);

	if (!(rst & RST_REG) && !(clk & CK_DISABLE))
		return true;

	return false;
}

PPSMC_Result amdgpu_si_send_msg_to_smc(struct amdgpu_device *adev,
				       PPSMC_Msg msg)
{
	u32 tmp;
	int i;
	int usec_timeout;

	/* SMC seems to process some messages exceptionally slowly. */
	switch (msg) {
	case PPSMC_MSG_NoForcedLevel:
	case PPSMC_MSG_SetEnabledLevels:
	case PPSMC_MSG_SetForcedLevels:
	case PPSMC_MSG_DisableULV:
	case PPSMC_MSG_SwitchToSwState:
		usec_timeout = 1000000; /* 1 sec */
		break;
	default:
		usec_timeout = 200000; /* 200 ms */
		break;
	}

	if (!amdgpu_si_is_smc_running(adev))
		return PPSMC_Result_Failed;

	WREG32(mmSMC_MESSAGE_0, msg);

	for (i = 0; i < usec_timeout; i++) {
		tmp = RREG32(mmSMC_RESP_0);
		if (tmp != 0)
			break;
		udelay(1);
	}

	tmp = RREG32(mmSMC_RESP_0);
	if (tmp == 0) {
		drm_warn(adev_to_drm(adev),
			"%s timeout on message: %x (SMC_SCRATCH0: %x)\n",
			__func__, msg, RREG32(mmSMC_SCRATCH0));
	}

	return (PPSMC_Result)tmp;
}

PPSMC_Result amdgpu_si_wait_for_smc_inactive(struct amdgpu_device *adev)
{
	u32 tmp;
	int i;

	if (!amdgpu_si_is_smc_running(adev))
		return PPSMC_Result_OK;

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32_SMC(SMC_SYSCON_CLOCK_CNTL_0);
		if ((tmp & CKEN) == 0)
			break;
		udelay(1);
	}

	return PPSMC_Result_OK;
}

int amdgpu_si_load_smc_ucode(struct amdgpu_device *adev, u32 limit)
{
	const struct smc_firmware_header_v1_0 *hdr;
	unsigned long flags;
	u32 ucode_start_address;
	u32 ucode_size;
	const u8 *src;
	u32 data;

	if (!adev->pm.fw)
		return -EINVAL;

	hdr = (const struct smc_firmware_header_v1_0 *)adev->pm.fw->data;

	amdgpu_ucode_print_smc_hdr(&hdr->header);

	adev->pm.fw_version = le32_to_cpu(hdr->header.ucode_version);
	ucode_start_address = le32_to_cpu(hdr->ucode_start_addr);
	ucode_size = le32_to_cpu(hdr->header.ucode_size_bytes);
	src = (const u8 *)
		(adev->pm.fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));
	if (ucode_size & 3)
		return -EINVAL;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	WREG32(mmSMC_IND_INDEX_0, ucode_start_address);
	WREG32_P(mmSMC_IND_ACCESS_CNTL, SMC_IND_ACCESS_CNTL__AUTO_INCREMENT_IND_0_MASK, ~SMC_IND_ACCESS_CNTL__AUTO_INCREMENT_IND_0_MASK);
	while (ucode_size >= 4) {
		/* SMC address space is BE */
		data = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];

		WREG32(mmSMC_IND_DATA_0, data);

		src += 4;
		ucode_size -= 4;
	}
	WREG32_P(mmSMC_IND_ACCESS_CNTL, 0, ~SMC_IND_ACCESS_CNTL__AUTO_INCREMENT_IND_0_MASK);
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);

	return 0;
}

int amdgpu_si_read_smc_sram_dword(struct amdgpu_device *adev, u32 smc_address,
				  u32 *value, u32 limit)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	ret = si_set_smc_sram_address(adev, smc_address, limit);
	if (ret == 0)
		*value = RREG32(mmSMC_IND_DATA_0);
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);

	return ret;
}

int amdgpu_si_write_smc_sram_dword(struct amdgpu_device *adev, u32 smc_address,
				   u32 value, u32 limit)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	ret = si_set_smc_sram_address(adev, smc_address, limit);
	if (ret == 0)
		WREG32(mmSMC_IND_DATA_0, value);
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);

	return ret;
}
