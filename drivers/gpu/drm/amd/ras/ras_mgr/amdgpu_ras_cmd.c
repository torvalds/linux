// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#include <linux/pci.h>
#include "amdgpu.h"
#include "amdgpu_ras.h"
#include "ras_sys.h"
#include "amdgpu_ras_cmd.h"
#include "amdgpu_ras_mgr.h"

/* inject address is 52 bits */
#define	RAS_UMC_INJECT_ADDR_LIMIT	(0x1ULL << 52)

#define AMDGPU_RAS_TYPE_RASCORE  0x1
#define AMDGPU_RAS_TYPE_AMDGPU   0x2
#define AMDGPU_RAS_TYPE_VF       0x3

static int amdgpu_ras_trigger_error_prepare(struct ras_core_context *ras_core,
			struct ras_cmd_inject_error_req *block_info)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	int ret;

	if (block_info->block_id == TA_RAS_BLOCK__XGMI_WAFL) {
		if (amdgpu_dpm_set_df_cstate(adev, DF_CSTATE_DISALLOW))
			RAS_DEV_WARN(adev, "Failed to disallow df cstate");

		ret = amdgpu_dpm_set_pm_policy(adev, PP_PM_POLICY_XGMI_PLPD, XGMI_PLPD_DISALLOW);
		if (ret && (ret != -EOPNOTSUPP))
			RAS_DEV_WARN(adev, "Failed to disallow XGMI power down");
	}

	return 0;
}

static int amdgpu_ras_trigger_error_end(struct ras_core_context *ras_core,
			struct ras_cmd_inject_error_req *block_info)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	int ret;

	if (block_info->block_id == TA_RAS_BLOCK__XGMI_WAFL) {
		if (amdgpu_ras_intr_triggered())
			return 0;

		ret = amdgpu_dpm_set_pm_policy(adev, PP_PM_POLICY_XGMI_PLPD, XGMI_PLPD_DEFAULT);
		if (ret && (ret != -EOPNOTSUPP))
			RAS_DEV_WARN(adev, "Failed to allow XGMI power down");

		if (amdgpu_dpm_set_df_cstate(adev, DF_CSTATE_ALLOW))
			RAS_DEV_WARN(adev, "Failed to allow df cstate");
	}

	return 0;
}

static uint64_t local_addr_to_xgmi_global_addr(struct ras_core_context *ras_core,
					   uint64_t addr)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	struct amdgpu_xgmi *xgmi = &adev->gmc.xgmi;

	return (addr + xgmi->physical_node_id * xgmi->node_segment_size);
}

static int amdgpu_ras_inject_error(struct ras_core_context *ras_core,
			struct ras_cmd_ctx *cmd, void *data)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	struct ras_cmd_inject_error_req *req =
		(struct ras_cmd_inject_error_req *)cmd->input_buff_raw;
	int ret = RAS_CMD__ERROR_GENERIC;

	if (req->block_id == RAS_BLOCK_ID__UMC) {
		if (amdgpu_ras_mgr_check_retired_addr(adev, req->address)) {
			RAS_DEV_WARN(ras_core->dev,
				"RAS WARN: inject: 0x%llx has already been marked as bad!\n",
				req->address);
			return RAS_CMD__ERROR_ACCESS_DENIED;
		}

		if ((req->address >= adev->gmc.mc_vram_size &&
			adev->gmc.mc_vram_size) ||
			(req->address >= RAS_UMC_INJECT_ADDR_LIMIT)) {
			RAS_DEV_WARN(adev, "RAS WARN: input address 0x%llx is invalid.",
					req->address);
			return RAS_CMD__ERROR_INVALID_INPUT_DATA;
		}

		/* Calculate XGMI relative offset */
		if (adev->gmc.xgmi.num_physical_nodes > 1 &&
			req->block_id != RAS_BLOCK_ID__GFX) {
			req->address = local_addr_to_xgmi_global_addr(ras_core, req->address);
		}
	}

	amdgpu_ras_trigger_error_prepare(ras_core, req);
	ret = rascore_handle_cmd(ras_core, cmd, data);
	amdgpu_ras_trigger_error_end(ras_core, req);
	if (ret) {
		RAS_DEV_ERR(adev, "ras inject block %u failed %d\n", req->block_id, ret);
		ret = RAS_CMD__ERROR_ACCESS_DENIED;
	}


	return ret;
}

static int amdgpu_ras_get_ras_safe_fb_addr_ranges(struct ras_core_context *ras_core,
	struct ras_cmd_ctx *cmd, void *data)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	struct ras_cmd_dev_handle *input_data =
			(struct ras_cmd_dev_handle *)cmd->input_buff_raw;
	struct ras_cmd_ras_safe_fb_address_ranges_rsp *ranges =
			(struct ras_cmd_ras_safe_fb_address_ranges_rsp *)cmd->output_buff_raw;
	struct amdgpu_mem_partition_info *mem_ranges;
	uint32_t i = 0;

	if (cmd->input_size != sizeof(*input_data))
		return RAS_CMD__ERROR_INVALID_INPUT_DATA;

	mem_ranges = adev->gmc.mem_partitions;
	for (i = 0; i < adev->gmc.num_mem_partitions; i++) {
		ranges->range[i].start = mem_ranges[i].range.fpfn << AMDGPU_GPU_PAGE_SHIFT;
		ranges->range[i].size = mem_ranges[i].size;
		ranges->range[i].idx = i;
	}

	ranges->num_ranges = adev->gmc.num_mem_partitions;

	ranges->version = 0;
	cmd->output_size = sizeof(struct ras_cmd_ras_safe_fb_address_ranges_rsp);

	return RAS_CMD__SUCCESS;
}

static int ras_translate_fb_address(struct ras_core_context *ras_core,
		enum ras_fb_addr_type src_type,
		enum ras_fb_addr_type dest_type,
		union ras_translate_fb_address *src_addr,
		union ras_translate_fb_address *dest_addr)
{
	uint64_t soc_phy_addr;
	int ret = RAS_CMD__SUCCESS;

	/* Does not need to be queued as event as this is a SW translation */
	switch (src_type) {
	case RAS_FB_ADDR_SOC_PHY:
		soc_phy_addr = src_addr->soc_phy_addr;
		break;
	case RAS_FB_ADDR_BANK:
		ret = ras_cmd_translate_bank_to_soc_pa(ras_core,
					src_addr->bank_addr, &soc_phy_addr);
		if (ret)
			return RAS_CMD__ERROR_GENERIC;
		break;
	default:
		return RAS_CMD__ERROR_INVALID_CMD;
	}

	switch (dest_type) {
	case RAS_FB_ADDR_SOC_PHY:
		dest_addr->soc_phy_addr = soc_phy_addr;
		break;
	case RAS_FB_ADDR_BANK:
		ret = ras_cmd_translate_soc_pa_to_bank(ras_core,
				soc_phy_addr, &dest_addr->bank_addr);
		if (ret)
			return RAS_CMD__ERROR_GENERIC;
		break;
	default:
		return RAS_CMD__ERROR_INVALID_CMD;
	}

	return ret;
}

static int amdgpu_ras_translate_fb_address(struct ras_core_context *ras_core,
				struct ras_cmd_ctx *cmd, void *data)
{
	struct ras_cmd_translate_fb_address_req *req_buff =
			(struct ras_cmd_translate_fb_address_req *)cmd->input_buff_raw;
	struct ras_cmd_translate_fb_address_rsp *rsp_buff =
			(struct ras_cmd_translate_fb_address_rsp *)cmd->output_buff_raw;
	int ret = RAS_CMD__ERROR_GENERIC;

	if (cmd->input_size != sizeof(struct ras_cmd_translate_fb_address_req))
		return RAS_CMD__ERROR_INVALID_INPUT_SIZE;

	if ((req_buff->src_addr_type >= RAS_FB_ADDR_UNKNOWN) ||
	    (req_buff->dest_addr_type >= RAS_FB_ADDR_UNKNOWN))
		return RAS_CMD__ERROR_INVALID_INPUT_DATA;

	ret = ras_translate_fb_address(ras_core, req_buff->src_addr_type,
			req_buff->dest_addr_type, &req_buff->trans_addr, &rsp_buff->trans_addr);
	if (ret)
		return RAS_CMD__ERROR_GENERIC;

	rsp_buff->version = 0;
	cmd->output_size = sizeof(struct ras_cmd_translate_fb_address_rsp);

	return RAS_CMD__SUCCESS;
}

static struct ras_cmd_func_map amdgpu_ras_cmd_maps[] = {
	{RAS_CMD__INJECT_ERROR, amdgpu_ras_inject_error},
	{RAS_CMD__GET_SAFE_FB_ADDRESS_RANGES, amdgpu_ras_get_ras_safe_fb_addr_ranges},
	{RAS_CMD__TRANSLATE_FB_ADDRESS, amdgpu_ras_translate_fb_address},
};

int amdgpu_ras_handle_cmd(struct ras_core_context *ras_core, struct ras_cmd_ctx *cmd, void *data)
{
	struct ras_cmd_func_map *ras_cmd = NULL;
	int i, res;

	for (i = 0; i < ARRAY_SIZE(amdgpu_ras_cmd_maps); i++) {
		if (cmd->cmd_id == amdgpu_ras_cmd_maps[i].cmd_id) {
			ras_cmd = &amdgpu_ras_cmd_maps[i];
			break;
		}
	}

	if (ras_cmd)
		res = ras_cmd->func(ras_core, cmd, NULL);
	else
		res = RAS_CMD__ERROR_UKNOWN_CMD;

	return res;
}

int amdgpu_ras_submit_cmd(struct ras_core_context *ras_core, struct ras_cmd_ctx *cmd)
{
	struct ras_core_context *cmd_core = ras_core;
	int timeout = 60;
	int res;

	cmd->cmd_res = RAS_CMD__ERROR_INVALID_CMD;
	cmd->output_size = 0;

	if (!ras_core_is_enabled(cmd_core))
		return RAS_CMD__ERROR_ACCESS_DENIED;

	while (ras_core_gpu_in_reset(cmd_core)) {
		msleep(1000);
		if (!timeout--)
			return RAS_CMD__ERROR_TIMEOUT;
	}

	res = amdgpu_ras_handle_cmd(cmd_core, cmd, NULL);
	if (res == RAS_CMD__ERROR_UKNOWN_CMD)
		res = rascore_handle_cmd(cmd_core, cmd, NULL);

	cmd->cmd_res = res;

	if (cmd->output_size > cmd->output_buf_size) {
		RAS_DEV_ERR(cmd_core->dev,
			"Output size 0x%x exceeds output buffer size 0x%x!\n",
			cmd->output_size, cmd->output_buf_size);
		return RAS_CMD__SUCCESS_EXEED_BUFFER;
	}

	return RAS_CMD__SUCCESS;
}
