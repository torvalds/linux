/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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
#include "ivsrcid/mpnht/irqsrcs_mpnht_15_0.h"
#include "amdgpu_ualink.h"
#include "ualink_v1_0.h"

/* nHT firmware mailbox registers via SMN, copy of MP1 */
#define mmMPNHT_SMN_C2PMSG_CMD		0xAE10958
#define mmMPNHT_SMN_C2PMSG_DATA		0xAE1095C
#define mmMPNHT_SMN_C2PMSG_ADDR_LO	0xAE10960
#define mmMPNHT_SMN_C2PMSG_ADDR_HI	0xAE10964
#define mmMPNHT_SMN_C2PMSG_STATUS	0xAE10968

/* nHT firmware commands */
#define UALINK_V1_0_FW_CMD_LOAD_METADATA	0x1
#define UALINK_V1_0_FW_CMD_HALT_OPERATION	0x2

#define UALINK_V1_0_FW_POLL_COUNT	2000

static u32 ualink_v1_0_check_status(struct amdgpu_device *adev)
{
	return RREG32_PCIE(mmMPNHT_SMN_C2PMSG_STATUS);
}

static int ualink_v1_0_send_metadata(struct amdgpu_device *adev,
				     u64 metadata_gpu_addr, u32 accel_data)
{
	u32 status;
	int i;

	dev_dbg(adev->dev, "nht load metadata addr 0x%llx accel_data 0x%x\n",
		metadata_gpu_addr, accel_data);

	WREG32_PCIE(mmMPNHT_SMN_C2PMSG_ADDR_HI, upper_32_bits(metadata_gpu_addr));
	WREG32_PCIE(mmMPNHT_SMN_C2PMSG_ADDR_LO, lower_32_bits(metadata_gpu_addr));
	WREG32_PCIE(mmMPNHT_SMN_C2PMSG_DATA, accel_data);
	WREG32_PCIE(mmMPNHT_SMN_C2PMSG_CMD, UALINK_V1_0_FW_CMD_LOAD_METADATA);

	for (i = 0; i < UALINK_V1_0_FW_POLL_COUNT; i++) {
		status = RREG32_PCIE(mmMPNHT_SMN_C2PMSG_STATUS);
		if (status == AMDGPU_NHT_FW_ST_READY)
			return 0;
		mdelay(1);
	}

	dev_dbg(adev->dev, "f/w load metadata failed 0x%x\n", status);
	return -ETIME;
}

static int ualink_v1_0_send_halt(struct amdgpu_device *adev)
{
	u32 status;
	int i;

	dev_dbg(adev->dev, "nht halt cmd 0x%x\n", UALINK_V1_0_FW_CMD_HALT_OPERATION);

	WREG32_PCIE(mmMPNHT_SMN_C2PMSG_CMD, UALINK_V1_0_FW_CMD_HALT_OPERATION);

	for (i = 0; i < UALINK_V1_0_FW_POLL_COUNT; i++) {
		status = RREG32_PCIE(mmMPNHT_SMN_C2PMSG_STATUS);
		if (status == AMDGPU_NHT_FW_ST_HALT)
			return 0;
		mdelay(1);
	}

	dev_warn(adev->dev, "f/w halt failed status 0x%x\n", status);
	return -ETIME;
}

const struct amdgpu_ualink_msg_ctl ualink_v1_0_msg_ctl = {
	.check_status = ualink_v1_0_check_status,
	.send_metadata = ualink_v1_0_send_metadata,
	.send_halt = ualink_v1_0_send_halt,
};

static int ualink_v1_0_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	adev->ualink.msg_ctl = &ualink_v1_0_msg_ctl;

	return 0;
}

static int ualink_v1_0_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = ualink_ip_sw_init(ip_block);
	if (r)
		return r;

	r = amdgpu_ualink_init_interrupt(adev, SOC_V1_0_IH_CLIENTID_nHT,
					 MPNHT_15_0__SRCID__REMOTE_INTERRUPT);
	if (r) {
		dev_err(adev->dev, "Failed to add UALink irq: %d\n", r);
		return r;
	}

	return 0;
}

static const struct amd_ip_funcs ualink_v1_0_ip_funcs = {
	.name = "ualink",
	.early_init = ualink_v1_0_early_init,
	.late_init = ualink_ip_late_init,
	.sw_init = ualink_v1_0_sw_init,
	.sw_fini = ualink_ip_sw_fini,
	.hw_init = ualink_ip_hw_init,
	.hw_fini = ualink_ip_hw_fini,
};

const struct amdgpu_ip_block_version ualink_v1_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_UALINK,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &ualink_v1_0_ip_funcs,
};
