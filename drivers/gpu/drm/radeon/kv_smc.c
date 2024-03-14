/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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

#include "radeon.h"
#include "cikd.h"
#include "kv_dpm.h"

int kv_notify_message_to_smu(struct radeon_device *rdev, u32 id)
{
	u32 i;
	u32 tmp = 0;

	WREG32(SMC_MESSAGE_0, id & SMC_MSG_MASK);

	for (i = 0; i < rdev->usec_timeout; i++) {
		if ((RREG32(SMC_RESP_0) & SMC_RESP_MASK) != 0)
			break;
		udelay(1);
	}
	tmp = RREG32(SMC_RESP_0) & SMC_RESP_MASK;

	if (tmp != 1) {
		if (tmp == 0xFF)
			return -EINVAL;
		else if (tmp == 0xFE)
			return -EINVAL;
	}

	return 0;
}

int kv_dpm_get_enable_mask(struct radeon_device *rdev, u32 *enable_mask)
{
	int ret;

	ret = kv_notify_message_to_smu(rdev, PPSMC_MSG_SCLKDPM_GetEnabledMask);

	if (ret == 0)
		*enable_mask = RREG32_SMC(SMC_SYSCON_MSG_ARG_0);

	return ret;
}

int kv_send_msg_to_smc_with_parameter(struct radeon_device *rdev,
				      PPSMC_Msg msg, u32 parameter)
{

	WREG32(SMC_MSG_ARG_0, parameter);

	return kv_notify_message_to_smu(rdev, msg);
}

static int kv_set_smc_sram_address(struct radeon_device *rdev,
				   u32 smc_address, u32 limit)
{
	if (smc_address & 3)
		return -EINVAL;
	if ((smc_address + 3) > limit)
		return -EINVAL;

	WREG32(SMC_IND_INDEX_0, smc_address);
	WREG32_P(SMC_IND_ACCESS_CNTL, 0, ~AUTO_INCREMENT_IND_0);

	return 0;
}

int kv_read_smc_sram_dword(struct radeon_device *rdev, u32 smc_address,
			   u32 *value, u32 limit)
{
	int ret;

	ret = kv_set_smc_sram_address(rdev, smc_address, limit);
	if (ret)
		return ret;

	*value = RREG32(SMC_IND_DATA_0);
	return 0;
}

int kv_smc_dpm_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		return kv_notify_message_to_smu(rdev, PPSMC_MSG_DPM_Enable);
	else
		return kv_notify_message_to_smu(rdev, PPSMC_MSG_DPM_Disable);
}

int kv_smc_bapm_enable(struct radeon_device *rdev, bool enable)
{
	if (enable)
		return kv_notify_message_to_smu(rdev, PPSMC_MSG_EnableBAPM);
	else
		return kv_notify_message_to_smu(rdev, PPSMC_MSG_DisableBAPM);
}

int kv_copy_bytes_to_smc(struct radeon_device *rdev,
			 u32 smc_start_address,
			 const u8 *src, u32 byte_count, u32 limit)
{
	int ret;
	u32 data, original_data, addr, extra_shift, t_byte, count, mask;

	if ((smc_start_address + byte_count) > limit)
		return -EINVAL;

	addr = smc_start_address;
	t_byte = addr & 3;

	/* RMW for the initial bytes */
	if  (t_byte != 0) {
		addr -= t_byte;

		ret = kv_set_smc_sram_address(rdev, addr, limit);
		if (ret)
			return ret;

		original_data = RREG32(SMC_IND_DATA_0);

		data = 0;
		mask = 0;
		count = 4;
		while (count > 0) {
			if (t_byte > 0) {
				mask = (mask << 8) | 0xff;
				t_byte--;
			} else if (byte_count > 0) {
				data = (data << 8) + *src++;
				byte_count--;
				mask <<= 8;
			} else {
				data <<= 8;
				mask = (mask << 8) | 0xff;
			}
			count--;
		}

		data |= original_data & mask;

		ret = kv_set_smc_sram_address(rdev, addr, limit);
		if (ret)
			return ret;

		WREG32(SMC_IND_DATA_0, data);

		addr += 4;
	}

	while (byte_count >= 4) {
		/* SMC address space is BE */
		data = (src[0] << 24) + (src[1] << 16) + (src[2] << 8) + src[3];

		ret = kv_set_smc_sram_address(rdev, addr, limit);
		if (ret)
			return ret;

		WREG32(SMC_IND_DATA_0, data);

		src += 4;
		byte_count -= 4;
		addr += 4;
	}

	/* RMW for the final bytes */
	if (byte_count > 0) {
		data = 0;

		ret = kv_set_smc_sram_address(rdev, addr, limit);
		if (ret)
			return ret;

		original_data = RREG32(SMC_IND_DATA_0);

		extra_shift = 8 * (4 - byte_count);

		while (byte_count > 0) {
			/* SMC address space is BE */
			data = (data << 8) + *src++;
			byte_count--;
		}

		data <<= extra_shift;

		data |= (original_data & ~((~0UL) << extra_shift));

		ret = kv_set_smc_sram_address(rdev, addr, limit);
		if (ret)
			return ret;

		WREG32(SMC_IND_DATA_0, data);
	}
	return 0;
}

