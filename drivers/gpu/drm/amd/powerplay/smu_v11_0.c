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
 */

#include "pp_debug.h"
#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_v11_0.h"
#include "smu_v11_0_ppsmc.h"
#include "smu11_driver_if.h"
#include "soc15_common.h"

#include "asic_reg/thm/thm_11_0_2_offset.h"
#include "asic_reg/thm/thm_11_0_2_sh_mask.h"
#include "asic_reg/mp/mp_9_0_offset.h"
#include "asic_reg/mp/mp_9_0_sh_mask.h"
#include "asic_reg/nbio/nbio_7_4_offset.h"

MODULE_FIRMWARE("amdgpu/vega20_smc.bin");

static int smu_v11_0_send_msg_without_waiting(struct smu_context *smu,
					      uint16_t msg)
{
	struct amdgpu_device *adev = smu->adev;
	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_66, msg);
	return 0;
}

static int smu_v11_0_read_arg(struct smu_context *smu, uint32_t *arg)
{
	struct amdgpu_device *adev = smu->adev;

	*arg = RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82);
	return 0;
}

static int smu_v11_0_wait_for_response(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t cur_value, i;

	for (i = 0; i < adev->usec_timeout; i++) {
		cur_value = RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90);
		if ((cur_value & MP1_C2PMSG_90__CONTENT_MASK) != 0)
			break;
		udelay(1);
	}

	/* timeout means wrong logic */
	if (i == adev->usec_timeout)
		return -ETIME;

	return RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90) ==  PPSMC_Result_OK ? 0:-EIO;
}

static int smu_v11_0_send_msg(struct smu_context *smu, uint16_t msg)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	smu_v11_0_wait_for_response(smu);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	smu_v11_0_send_msg_without_waiting(smu, msg);

	ret = smu_v11_0_wait_for_response(smu);

	if (ret)
		pr_err("Failed to send message 0x%x, response 0x%x\n", msg,
		       ret);

	return ret;

}

static int
smu_v11_0_send_msg_with_param(struct smu_context *smu, uint16_t msg,
			      uint32_t param)
{

	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	ret = smu_v11_0_wait_for_response(smu);
	if (ret)
		pr_err("Failed to send message 0x%x, response 0x%x\n", msg,
		       ret);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82, param);

	smu_v11_0_send_msg_without_waiting(smu, msg);

	ret = smu_v11_0_wait_for_response(smu);
	if (ret)
		pr_err("Failed to send message 0x%x, response 0x%x\n", msg,
		       ret);

	return ret;
}

static int smu_v11_0_init_microcode(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	const char *chip_name;
	char fw_name[30];
	int err = 0;
	const struct smc_firmware_header_v1_0 *hdr;
	const struct common_firmware_header *header;
	struct amdgpu_firmware_info *ucode = NULL;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
		chip_name = "vega20";
		break;
	default:
		BUG();
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_smc.bin", chip_name);

	err = request_firmware(&adev->pm.fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->pm.fw);
	if (err)
		goto out;

	hdr = (const struct smc_firmware_header_v1_0 *) adev->pm.fw->data;
	amdgpu_ucode_print_smc_hdr(&hdr->header);
	adev->pm.fw_version = le32_to_cpu(hdr->header.ucode_version);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		ucode = &adev->firmware.ucode[AMDGPU_UCODE_ID_SMC];
		ucode->ucode_id = AMDGPU_UCODE_ID_SMC;
		ucode->fw = adev->pm.fw;
		header = (const struct common_firmware_header *)ucode->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);
	}

out:
	if (err) {
		DRM_ERROR("smu_v11_0: Failed to load firmware \"%s\"\n",
			  fw_name);
		release_firmware(adev->pm.fw);
		adev->pm.fw = NULL;
	}
	return err;
}

static int smu_v11_0_load_microcode(struct smu_context *smu)
{
	return 0;
}

static int smu_v11_0_check_fw_status(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t mp1_fw_flags;

	WREG32_SOC15(NBIF, 0, mmPCIE_INDEX2,
		     (MP1_Public | (smnMP1_FIRMWARE_FLAGS & 0xffffffff)));

	mp1_fw_flags = RREG32_SOC15(NBIF, 0, mmPCIE_DATA2);

	if ((mp1_fw_flags & MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED_MASK) >>
	    MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED__SHIFT)
		return 0;
	return -EIO;
}

static int smu_v11_0_check_fw_version(struct smu_context *smu)
{
	uint32_t smu_version = 0xff;
	int ret = 0;

	ret = smu_send_smc_msg(smu, PPSMC_MSG_GetDriverIfVersion);
	if (ret)
		goto err;

	ret = smu_v11_0_read_arg(smu, &smu_version);
	if (ret)
		goto err;

	if (smu_version == SMU11_DRIVER_IF_VERSION)
		return 0;
err:
	return ret;
}

static const struct smu_funcs smu_v11_0_funcs = {
	.init_microcode = smu_v11_0_init_microcode,
	.load_microcode = smu_v11_0_load_microcode,
	.check_fw_status = smu_v11_0_check_fw_status,
	.check_fw_version = smu_v11_0_check_fw_version,
	.send_smc_msg = smu_v11_0_send_msg,
	.send_smc_msg_with_param = smu_v11_0_send_msg_with_param,
};

void smu_v11_0_set_smu_funcs(struct smu_context *smu)
{
	smu->funcs = &smu_v11_0_funcs;
}
