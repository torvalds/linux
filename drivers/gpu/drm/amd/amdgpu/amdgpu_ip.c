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

#include "amdgpu.h"
#include "amdgpu_ip.h"

static int8_t amdgpu_logical_to_dev_inst(struct amdgpu_device *adev,
					 enum amd_hw_ip_block_type block,
					 int8_t inst)
{
	int8_t dev_inst;

	switch (block) {
	case GC_HWIP:
	case SDMA0_HWIP:
	/* Both JPEG and VCN as JPEG is only alias of VCN */
	case VCN_HWIP:
		dev_inst = adev->ip_map.dev_inst[block][inst];
		break;
	default:
		/* For rest of the IPs, no look up required.
		 * Assume 'logical instance == physical instance' for all configs. */
		dev_inst = inst;
		break;
	}

	return dev_inst;
}

static uint32_t amdgpu_logical_to_dev_mask(struct amdgpu_device *adev,
					   enum amd_hw_ip_block_type block,
					   uint32_t mask)
{
	uint32_t dev_mask = 0;
	int8_t log_inst, dev_inst;

	while (mask) {
		log_inst = ffs(mask) - 1;
		dev_inst = amdgpu_logical_to_dev_inst(adev, block, log_inst);
		dev_mask |= (1 << dev_inst);
		mask &= ~(1 << log_inst);
	}

	return dev_mask;
}

static void amdgpu_populate_ip_map(struct amdgpu_device *adev,
				   enum amd_hw_ip_block_type ip_block,
				   uint32_t inst_mask)
{
	int l = 0, i;

	while (inst_mask) {
		i = ffs(inst_mask) - 1;
		adev->ip_map.dev_inst[ip_block][l++] = i;
		inst_mask &= ~(1 << i);
	}
	for (; l < HWIP_MAX_INSTANCE; l++)
		adev->ip_map.dev_inst[ip_block][l] = -1;
}

void amdgpu_ip_map_init(struct amdgpu_device *adev)
{
	u32 ip_map[][2] = {
		{ GC_HWIP, adev->gfx.xcc_mask },
		{ SDMA0_HWIP, adev->sdma.sdma_mask },
		{ VCN_HWIP, adev->vcn.inst_mask },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(ip_map); ++i)
		amdgpu_populate_ip_map(adev, ip_map[i][0], ip_map[i][1]);

	adev->ip_map.logical_to_dev_inst = amdgpu_logical_to_dev_inst;
	adev->ip_map.logical_to_dev_mask = amdgpu_logical_to_dev_mask;
}

int amdgpu_ip_block_suspend(struct amdgpu_ip_block *ip_block)
{
	int r;

	if (ip_block->version->funcs->suspend) {
		r = ip_block->version->funcs->suspend(ip_block);
		if (r) {
			dev_err(ip_block->adev->dev,
				"suspend of IP block <%s> failed %d\n",
				ip_block->version->funcs->name, r);
			return r;
		}
	}

	ip_block->status.hw = false;
	return 0;
}

int amdgpu_ip_block_resume(struct amdgpu_ip_block *ip_block)
{
	int r;

	if (ip_block->version->funcs->resume) {
		r = ip_block->version->funcs->resume(ip_block);
		if (r) {
			dev_err(ip_block->adev->dev,
				"resume of IP block <%s> failed %d\n",
				ip_block->version->funcs->name, r);
			return r;
		}
	}

	ip_block->status.hw = true;
	return 0;
}

/**
 * amdgpu_device_ip_get_ip_block - get a hw IP pointer
 *
 * @adev: amdgpu_device pointer
 * @type: Type of hardware IP (SMU, GFX, UVD, etc.)
 *
 * Returns a pointer to the hardware IP block structure
 * if it exists for the asic, otherwise NULL.
 */
struct amdgpu_ip_block *
amdgpu_device_ip_get_ip_block(struct amdgpu_device *adev,
			      enum amd_ip_block_type type)
{
	int i;

	for (i = 0; i < adev->num_ip_blocks; i++)
		if (adev->ip_blocks[i].version->type == type)
			return &adev->ip_blocks[i];

	return NULL;
}

/**
 * amdgpu_device_ip_block_version_cmp
 *
 * @adev: amdgpu_device pointer
 * @type: enum amd_ip_block_type
 * @major: major version
 * @minor: minor version
 *
 * return 0 if equal or greater
 * return 1 if smaller or the ip_block doesn't exist
 */
int amdgpu_device_ip_block_version_cmp(struct amdgpu_device *adev,
				       enum amd_ip_block_type type, u32 major,
				       u32 minor)
{
	struct amdgpu_ip_block *ip_block =
		amdgpu_device_ip_get_ip_block(adev, type);

	if (ip_block && ((ip_block->version->major > major) ||
			 ((ip_block->version->major == major) &&
			  (ip_block->version->minor >= minor))))
		return 0;

	return 1;
}

static const char *const ip_block_names[] = {
	[AMD_IP_BLOCK_TYPE_COMMON] = "common",
	[AMD_IP_BLOCK_TYPE_GMC] = "gmc",
	[AMD_IP_BLOCK_TYPE_IH] = "ih",
	[AMD_IP_BLOCK_TYPE_SMC] = "smu",
	[AMD_IP_BLOCK_TYPE_PSP] = "psp",
	[AMD_IP_BLOCK_TYPE_DCE] = "dce",
	[AMD_IP_BLOCK_TYPE_GFX] = "gfx",
	[AMD_IP_BLOCK_TYPE_SDMA] = "sdma",
	[AMD_IP_BLOCK_TYPE_UVD] = "uvd",
	[AMD_IP_BLOCK_TYPE_VCE] = "vce",
	[AMD_IP_BLOCK_TYPE_ACP] = "acp",
	[AMD_IP_BLOCK_TYPE_VCN] = "vcn",
	[AMD_IP_BLOCK_TYPE_MES] = "mes",
	[AMD_IP_BLOCK_TYPE_JPEG] = "jpeg",
	[AMD_IP_BLOCK_TYPE_VPE] = "vpe",
	[AMD_IP_BLOCK_TYPE_UMSCH_MM] = "umsch_mm",
	[AMD_IP_BLOCK_TYPE_ISP] = "isp",
	[AMD_IP_BLOCK_TYPE_RAS] = "ras",
};

static const char *ip_block_name(struct amdgpu_device *adev,
				 enum amd_ip_block_type type)
{
	int idx = (int)type;

	return idx < ARRAY_SIZE(ip_block_names) ? ip_block_names[idx] :
						  "unknown";
}

/**
 * amdgpu_device_ip_block_add
 *
 * @adev: amdgpu_device pointer
 * @ip_block_version: pointer to the IP to add
 *
 * Adds the IP block driver information to the collection of IPs
 * on the asic.
 */
int amdgpu_device_ip_block_add(
	struct amdgpu_device *adev,
	const struct amdgpu_ip_block_version *ip_block_version)
{
	if (!ip_block_version)
		return -EINVAL;

	switch (ip_block_version->type) {
	case AMD_IP_BLOCK_TYPE_VCN:
		if (adev->harvest_ip_mask & AMD_HARVEST_IP_VCN_MASK)
			return 0;
		break;
	case AMD_IP_BLOCK_TYPE_JPEG:
		if (adev->harvest_ip_mask & AMD_HARVEST_IP_JPEG_MASK)
			return 0;
		break;
	default:
		break;
	}

	dev_info(adev->dev, "detected ip block number %d <%s_v%d_%d_%d> (%s)\n",
		 adev->num_ip_blocks,
		 ip_block_name(adev, ip_block_version->type),
		 ip_block_version->major, ip_block_version->minor,
		 ip_block_version->rev, ip_block_version->funcs->name);

	adev->ip_blocks[adev->num_ip_blocks].adev = adev;

	adev->ip_blocks[adev->num_ip_blocks++].version = ip_block_version;

	return 0;
}

/**
 * amdgpu_device_ip_set_clockgating_state - set the CG state
 *
 * @adev: amdgpu_device pointer
 * @block_type: Type of hardware IP (SMU, GFX, UVD, etc.)
 * @state: clockgating state (gate or ungate)
 *
 * Sets the requested clockgating state for all instances of
 * the hardware IP specified.
 * Returns the error code from the last instance.
 */
int amdgpu_device_ip_set_clockgating_state(struct amdgpu_device *adev,
					   enum amd_ip_block_type block_type,
					   enum amd_clockgating_state state)
{
	int i, r = 0;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].version->type != block_type)
			continue;
		if (!adev->ip_blocks[i].version->funcs->set_clockgating_state)
			continue;
		r = adev->ip_blocks[i].version->funcs->set_clockgating_state(
			&adev->ip_blocks[i], state);
		if (r)
			dev_err(adev->dev,
				"set_clockgating_state of IP block <%s> failed %d\n",
				adev->ip_blocks[i].version->funcs->name, r);
	}
	return r;
}

/**
 * amdgpu_device_ip_set_powergating_state - set the PG state
 *
 * @adev: amdgpu_device pointer
 * @block_type: Type of hardware IP (SMU, GFX, UVD, etc.)
 * @state: powergating state (gate or ungate)
 *
 * Sets the requested powergating state for all instances of
 * the hardware IP specified.
 * Returns the error code from the last instance.
 */
int amdgpu_device_ip_set_powergating_state(struct amdgpu_device *adev,
					   enum amd_ip_block_type block_type,
					   enum amd_powergating_state state)
{
	int i, r = 0;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].version->type != block_type)
			continue;
		if (!adev->ip_blocks[i].version->funcs->set_powergating_state)
			continue;
		r = adev->ip_blocks[i].version->funcs->set_powergating_state(
			&adev->ip_blocks[i], state);
		if (r)
			dev_err(adev->dev,
				"set_powergating_state of IP block <%s> failed %d\n",
				adev->ip_blocks[i].version->funcs->name, r);
	}
	return r;
}

/**
 * amdgpu_device_ip_get_clockgating_state - get the CG state
 *
 * @adev: amdgpu_device pointer
 * @flags: clockgating feature flags
 *
 * Walks the list of IPs on the device and updates the clockgating
 * flags for each IP.
 * Updates @flags with the feature flags for each hardware IP where
 * clockgating is enabled.
 */
void amdgpu_device_ip_get_clockgating_state(struct amdgpu_device *adev,
					    u64 *flags)
{
	int i;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (adev->ip_blocks[i].version->funcs->get_clockgating_state)
			adev->ip_blocks[i].version->funcs->get_clockgating_state(
				&adev->ip_blocks[i], flags);
	}
}

/**
 * amdgpu_device_ip_wait_for_idle - wait for idle
 *
 * @adev: amdgpu_device pointer
 * @block_type: Type of hardware IP (SMU, GFX, UVD, etc.)
 *
 * Waits for the request hardware IP to be idle.
 * Returns 0 for success or a negative error code on failure.
 */
int amdgpu_device_ip_wait_for_idle(struct amdgpu_device *adev,
				   enum amd_ip_block_type block_type)
{
	struct amdgpu_ip_block *ip_block;

	ip_block = amdgpu_device_ip_get_ip_block(adev, block_type);
	if (!ip_block || !ip_block->status.valid)
		return 0;

	if (ip_block->version->funcs->wait_for_idle)
		return ip_block->version->funcs->wait_for_idle(ip_block);

	return 0;
}

/**
 * amdgpu_device_ip_is_hw - is the hardware IP enabled
 *
 * @adev: amdgpu_device pointer
 * @block_type: Type of hardware IP (SMU, GFX, UVD, etc.)
 *
 * Check if the hardware IP is enable or not.
 * Returns true if it the IP is enable, false if not.
 */
bool amdgpu_device_ip_is_hw(struct amdgpu_device *adev,
			    enum amd_ip_block_type block_type)
{
	struct amdgpu_ip_block *ip_block;

	ip_block = amdgpu_device_ip_get_ip_block(adev, block_type);
	if (ip_block)
		return ip_block->status.hw;

	return false;
}

/**
 * amdgpu_device_ip_is_valid - is the hardware IP valid
 *
 * @adev: amdgpu_device pointer
 * @block_type: Type of hardware IP (SMU, GFX, UVD, etc.)
 *
 * Check if the hardware IP is valid or not.
 * Returns true if it the IP is valid, false if not.
 */
bool amdgpu_device_ip_is_valid(struct amdgpu_device *adev,
			       enum amd_ip_block_type block_type)
{
	struct amdgpu_ip_block *ip_block;

	ip_block = amdgpu_device_ip_get_ip_block(adev, block_type);
	if (ip_block)
		return ip_block->status.valid;

	return false;
}
