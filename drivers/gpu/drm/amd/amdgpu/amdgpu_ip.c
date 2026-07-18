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

/**
 * amdgpu_ip_from_ring() - Find IP block type corresponding to ring type.
 *
 * @ring_type: The ring type whose IP block you are looking for.
 */
static enum amd_ip_block_type amdgpu_ip_from_ring(const enum amdgpu_ring_type ring_type)
{
	switch (ring_type) {
	case AMDGPU_RING_TYPE_GFX:
	case AMDGPU_RING_TYPE_COMPUTE:
		return AMD_IP_BLOCK_TYPE_GFX;

	case AMDGPU_RING_TYPE_SDMA:
		return AMD_IP_BLOCK_TYPE_SDMA;

	case AMDGPU_RING_TYPE_UVD:
	case AMDGPU_RING_TYPE_UVD_ENC:
		return AMD_IP_BLOCK_TYPE_UVD;

	case AMDGPU_RING_TYPE_VCE:
		return AMD_IP_BLOCK_TYPE_VCE;

	case AMDGPU_RING_TYPE_VCN_DEC:
	case AMDGPU_RING_TYPE_VCN_ENC:
		return AMD_IP_BLOCK_TYPE_VCN;

	case AMDGPU_RING_TYPE_VCN_JPEG:
		return AMD_IP_BLOCK_TYPE_JPEG;

	case AMDGPU_RING_TYPE_VPE:
		return AMD_IP_BLOCK_TYPE_VPE;

	default:
		return AMD_IP_BLOCK_TYPE_NUM;
	}
}

/**
 * amdgpu_ring_mask_from_ip() - Find mask of ring types corresponding to an IP block type.
 *
 * @ip_type: The IP block type whose rings you are looking for.
 */
static u32 amdgpu_ring_mask_from_ip(const enum amd_ip_block_type ip_type)
{
	switch (ip_type) {
	case AMD_IP_BLOCK_TYPE_GFX:
		return BIT(AMDGPU_RING_TYPE_GFX) | BIT(AMDGPU_RING_TYPE_COMPUTE);

	case AMD_IP_BLOCK_TYPE_SDMA:
		return BIT(AMDGPU_RING_TYPE_SDMA);

	case AMD_IP_BLOCK_TYPE_UVD:
		return BIT(AMDGPU_RING_TYPE_UVD) | BIT(AMDGPU_RING_TYPE_UVD_ENC);

	case AMD_IP_BLOCK_TYPE_VCE:
		return BIT(AMD_IP_BLOCK_TYPE_VCE);

	case AMD_IP_BLOCK_TYPE_VCN:
		return BIT(AMDGPU_RING_TYPE_VCN_DEC) | BIT(AMDGPU_RING_TYPE_VCN_ENC);

	case AMD_IP_BLOCK_TYPE_JPEG:
		return BIT(AMDGPU_RING_TYPE_VCN_JPEG);

	case AMD_IP_BLOCK_TYPE_VPE:
		return BIT(AMDGPU_RING_TYPE_VPE);

	default:
		return 0;
	}
}

/**
 * amdgpu_device_ip_soft_reset() - Perform a graceful soft reset on an IP block.
 *
 * @guilty_ring: The ring which is guilty of causing a reset.
 * @guilty_fence: The fence which didn't signal.
 *
 * IP block soft reset is used when attempting to recover
 * from a GPU hang in a situation where a more fine grained
 * reset type isn't available or didn't work. This effectively
 * resets all rings that belong to the same device IP block
 * and re-initializes the device IP block.
 *
 * The reset is handled gracefully, meaning that we try to
 * minimize collateral damage (ie. avoid rejecting non-guilty jobs)
 * as well as back up and restore the contents of all rings
 * so that the system can move on from the hang.
 */
int amdgpu_device_ip_soft_reset(struct amdgpu_ring *guilty_ring,
				struct amdgpu_fence *guilty_fence)
{
	struct amdgpu_device *adev = guilty_ring->adev;
	struct amdgpu_ip_block *ip_block;
	enum amd_ip_block_type ip_type;
	u32 ring_type_mask;
	int r;

	ip_type = amdgpu_ip_from_ring(guilty_ring->funcs->type);
	ip_block = amdgpu_device_ip_get_ip_block(adev, ip_type);

	if (unlikely(!ip_block)) {
		dev_warn(adev->dev, "IP block not found for ring %s\n",
			 guilty_ring->name);
		return -EOPNOTSUPP;
	}

	if (!ip_block->version->funcs->soft_reset) {
		dev_warn(adev->dev, "IP block soft reset not supported on %s\n",
			 ip_block->version->funcs->name);
		return -EOPNOTSUPP;
	}

	dev_err(adev->dev, "Starting %s IP block soft reset\n",
		ip_block->version->funcs->name);

	ring_type_mask = amdgpu_ring_mask_from_ip(ip_type);

	amdgpu_device_lock_reset_domain(adev->reset_domain);
	amdgpu_multi_ring_reset_helper_begin(ring_type_mask, guilty_ring, guilty_fence);

	r = ip_block->version->funcs->soft_reset(ip_block);

	r = amdgpu_multi_ring_reset_helper_end(ring_type_mask, guilty_ring, r);
	amdgpu_device_unlock_reset_domain(adev->reset_domain);

	if (r) {
		dev_err(adev->dev, "Failed %s IP block soft reset: %d\n",
			ip_block->version->funcs->name, r);
		return r;
	}

	dev_err(adev->dev, "Successful %s IP block soft reset\n",
		ip_block->version->funcs->name);
	return 0;
}
