/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#include "smumgr.h"
#include "vega10_inc.h"
#include "soc15_common.h"
#include "pp_debug.h"


/* MP Apertures */
#define MP0_Public                  0x03800000
#define MP0_SRAM                    0x03900000
#define MP1_Public                  0x03b00000
#define MP1_SRAM                    0x03c00004

#define smnMP1_FIRMWARE_FLAGS                                                                           0x3010028

bool smu9_is_smc_ram_running(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t mp1_fw_flags;

	mp1_fw_flags = RREG32_PCIE(MP1_Public |
				   (smnMP1_FIRMWARE_FLAGS & 0xffffffff));

	if (mp1_fw_flags & MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED_MASK)
		return true;

	return false;
}

/*
 * Check if SMC has responded to previous message.
 *
 * @param    smumgr  the address of the powerplay hardware manager.
 * @return   TRUE    SMC has responded, FALSE otherwise.
 */
static uint32_t smu9_wait_for_response(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t reg;
	uint32_t ret;

	if (hwmgr->pp_one_vf) {
		reg = SOC15_REG_OFFSET(MP1, 0, mmMP1_SMN_C2PMSG_103);

		ret = phm_wait_for_register_unequal(hwmgr, reg,
				0, MP1_C2PMSG_103__CONTENT_MASK);

		if (ret)
			pr_err("No response from smu\n");

		return RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_103);
	} else {
		reg = SOC15_REG_OFFSET(MP1, 0, mmMP1_SMN_C2PMSG_90);

		ret = phm_wait_for_register_unequal(hwmgr, reg,
				0, MP1_C2PMSG_90__CONTENT_MASK);

		if (ret)
			pr_err("No response from smu\n");
		return RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90);
	}
}

/*
 * Send a message to the SMC, and do not wait for its response.
 * @param    smumgr  the address of the powerplay hardware manager.
 * @param    msg the message to send.
 * @return   Always return 0.
 */
static int smu9_send_msg_to_smc_without_waiting(struct pp_hwmgr *hwmgr,
						uint16_t msg)
{
	struct amdgpu_device *adev = hwmgr->adev;

	if (hwmgr->pp_one_vf) {
		WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_101, msg);
	} else {
		WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_66, msg);
	}

	return 0;
}

/*
 * Send a message to the SMC, and wait for its response.
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @param    msg the message to send.
 * @return   Always return 0.
 */
int smu9_send_msg_to_smc(struct pp_hwmgr *hwmgr, uint16_t msg)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t ret;

	smu9_wait_for_response(hwmgr);

	if (hwmgr->pp_one_vf)
		WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_103, 0);
	else
		WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	smu9_send_msg_to_smc_without_waiting(hwmgr, msg);

	ret = smu9_wait_for_response(hwmgr);
	if (ret != 1)
		pr_err("Failed to send message: 0x%x, ret value: 0x%x\n", msg, ret);

	return 0;
}

/*
 * Send a message to the SMC with parameter
 * @param    hwmgr:  the address of the powerplay hardware manager.
 * @param    msg: the message to send.
 * @param    parameter: the parameter to send
 * @return   Always return 0.
 */
int smu9_send_msg_to_smc_with_parameter(struct pp_hwmgr *hwmgr,
					uint16_t msg, uint32_t parameter)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t ret;

	smu9_wait_for_response(hwmgr);

	if (hwmgr->pp_one_vf) {
		WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_103, 0);
		WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_102, parameter);
	} else {
		WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);
		WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82, parameter);
	}

	smu9_send_msg_to_smc_without_waiting(hwmgr, msg);

	ret = smu9_wait_for_response(hwmgr);
	if (ret != 1)
		pr_err("Failed message: 0x%x, input parameter: 0x%x, error code: 0x%x\n", msg, parameter, ret);

	return 0;
}

uint32_t smu9_get_argument(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	if (hwmgr->pp_one_vf)
		return RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_102);
	else
		return RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82);
}
