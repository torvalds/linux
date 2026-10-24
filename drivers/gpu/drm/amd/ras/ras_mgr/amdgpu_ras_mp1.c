// SPDX-License-Identifier: MIT
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
#include "amdgpu_smu.h"
#include "amdgpu_reset.h"
#include "ras.h"

static const enum smu_feature_mask ras_smu_feature_masks[RAS_MP1_FEATURE_MAX] = {
	[RAS_MP1_FEATURE_PMFW_EEPROM] = SMU_FEATURE_HROM_EN_BIT,
};

static const enum smu_message_type ras_smu_msg_maps[RAS_MP1_MSG_MAX] = {
	[RAS_MP1_MSG_QueryValidMcaCount] = SMU_MSG_QueryValidMcaCount,
	[RAS_MP1_MSG_McaBankDumpDW] = SMU_MSG_McaBankDumpDW,
	[RAS_MP1_MSG_ClearMcaOnRead] = SMU_MSG_ClearMcaOnRead,
	[RAS_MP1_MSG_QueryValidMcaCeCount] = SMU_MSG_QueryValidMcaCeCount,
	[RAS_MP1_MSG_McaBankCeDumpDW] = SMU_MSG_McaBankCeDumpDW,
	[RAS_MP1_MSG_GetRasTableVersion] = SMU_MSG_GetRASTableVersion,
	[RAS_MP1_MSG_GetRmaStatus] = SMU_MSG_GetRmaStatus,
	[RAS_MP1_MSG_GetBadPageCount] = SMU_MSG_GetBadPageCount,
	[RAS_MP1_MSG_GetBadPageMcaAddr] = SMU_MSG_GetBadPageMcaAddr,
	[RAS_MP1_MSG_SetTimestamp] = SMU_MSG_SetTimestamp,
	[RAS_MP1_MSG_GetTimestamp] = SMU_MSG_GetTimestamp,
	[RAS_MP1_MSG_GetBadPageIpId] = SMU_MSG_GetBadPageIpid,
	[RAS_MP1_MSG_EraseRasTable] = SMU_MSG_EraseRasTable,
};

static enum smu_message_type
	__get_ras_smu_msg(struct ras_core_context *ras_core, u32 msg_id)
{
	if (msg_id >= RAS_MP1_MSG_MAX)
		return 0;

	return ras_smu_msg_maps[msg_id];
}

static int amdgpu_ras_check_ras_feature_status(struct ras_core_context *ras_core,
		enum ras_mp1_feature_id feature_id, u32 *status)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	u32 res = 0;

	if (!status || (feature_id >= RAS_MP1_FEATURE_MAX))
		return -EINVAL;

	if (down_read_trylock(&adev->reset_domain->sem)) {
		if (amdgpu_smu_ras_feature_is_enabled(adev,
				ras_smu_feature_masks[feature_id]))
			res = 1;
		up_read(&adev->reset_domain->sem);
	} else {
		return -RAS_CORE_GPU_IN_MODE1_RESET;
	}

	*status = res;

	return 0;
}

static int amdgpu_ras_send_mp1_msg(struct ras_core_context *ras_core, u32 msg_id,
		u32 *params, u32 num_params, u32 *read_args, u32 num_read_args)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	enum smu_message_type smu_msg;
	int ret = 0;

	smu_msg = __get_ras_smu_msg(ras_core, msg_id);
	if (!smu_msg)
		return -EOPNOTSUPP;

	if (down_read_trylock(&adev->reset_domain->sem)) {
		ret = amdgpu_smu_ras_send_msg(adev, smu_msg,
				params, num_params, read_args, num_read_args);
		up_read(&adev->reset_domain->sem);
	} else if (msg_id == RAS_MP1_MSG_ClearMcaOnRead)
		ret = amdgpu_smu_ras_send_msg(adev, smu_msg,
				params, num_params, read_args, num_read_args);
	else
		ret = -RAS_CORE_GPU_IN_MODE1_RESET;

	return ret;
}

const struct ras_mp1_sys_func amdgpu_ras_mp1_sys_func = {
	.mp1_send_ras_msg = amdgpu_ras_send_mp1_msg,
	.check_ras_feature_status = amdgpu_ras_check_ras_feature_status,
};
