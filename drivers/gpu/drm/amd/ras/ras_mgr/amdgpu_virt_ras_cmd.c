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
#include "amdgpu_virt_ras_cmd.h"
#include "amdgpu_ras_mgr.h"

static int amdgpu_virt_ras_remote_ioctl_cmd(struct ras_core_context *ras_core,
			struct ras_cmd_ctx *cmd, void *output_data, uint32_t output_size)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	uint32_t mem_len = ALIGN(sizeof(*cmd) + output_size, AMDGPU_GPU_PAGE_SIZE);
	struct ras_cmd_ctx *rcmd;
	struct amdgpu_bo *rcmd_bo = NULL;
	uint64_t mc_addr = 0;
	void *cpu_addr = NULL;
	int ret = 0;

	ret = amdgpu_bo_create_kernel(adev, mem_len, PAGE_SIZE,
			AMDGPU_GEM_DOMAIN_VRAM, &rcmd_bo, &mc_addr, (void **)&cpu_addr);
	if (ret)
		return ret;

	rcmd = (struct ras_cmd_ctx *)cpu_addr;
	memset(rcmd, 0, mem_len);
	memcpy(rcmd, cmd, sizeof(*cmd));

	ret = amdgpu_virt_send_remote_ras_cmd(ras_core->dev,
				mc_addr - adev->gmc.vram_start, mem_len);
	if (!ret) {
		if (rcmd->cmd_res) {
			ret = rcmd->cmd_res;
			goto out;
		}

		cmd->cmd_res = rcmd->cmd_res;
		cmd->output_size = rcmd->output_size;
		if (rcmd->output_size && (rcmd->output_size <= output_size) && output_data)
			memcpy(output_data, rcmd->output_buff_raw, rcmd->output_size);
	}

out:
	amdgpu_bo_free_kernel(&rcmd_bo, &mc_addr, &cpu_addr);

	return ret;
}

static struct ras_cmd_func_map amdgpu_virt_ras_cmd_maps[] = {

};

int amdgpu_virt_ras_handle_cmd(struct ras_core_context *ras_core,
		struct ras_cmd_ctx *cmd)
{
	struct ras_cmd_func_map *ras_cmd = NULL;
	int i, res;

	for (i = 0; i < ARRAY_SIZE(amdgpu_virt_ras_cmd_maps); i++) {
		if (cmd->cmd_id == amdgpu_virt_ras_cmd_maps[i].cmd_id) {
			ras_cmd = &amdgpu_virt_ras_cmd_maps[i];
			break;
		}
	}

	if (ras_cmd)
		res = ras_cmd->func(ras_core, cmd, NULL);
	else
		res = amdgpu_virt_ras_remote_ioctl_cmd(ras_core, cmd,
					cmd->output_buff_raw, cmd->output_buf_size);

	cmd->cmd_res = res;

	if (cmd->output_size > cmd->output_buf_size) {
		RAS_DEV_ERR(ras_core->dev,
			"Output data size 0x%x exceeds buffer size 0x%x!\n",
			cmd->output_size, cmd->output_buf_size);
		return RAS_CMD__SUCCESS_EXEED_BUFFER;
	}

	return RAS_CMD__SUCCESS;
}

int amdgpu_virt_ras_sw_init(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	ras_mgr->virt_ras_cmd = kzalloc(sizeof(struct amdgpu_virt_ras_cmd), GFP_KERNEL);
	if (!ras_mgr->virt_ras_cmd)
		return -ENOMEM;

	return 0;
}

int amdgpu_virt_ras_sw_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	kfree(ras_mgr->virt_ras_cmd);
	ras_mgr->virt_ras_cmd = NULL;

	return 0;
}

int amdgpu_virt_ras_hw_init(struct amdgpu_device *adev)
{
	return 0;
}

int amdgpu_virt_ras_hw_fini(struct amdgpu_device *adev)
{
	return 0;
}
