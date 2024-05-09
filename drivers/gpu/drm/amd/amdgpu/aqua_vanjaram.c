/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#include "soc15.h"

#include "soc15_common.h"
#include "amdgpu_xcp.h"
#include "gfx_v9_4_3.h"
#include "gfxhub_v1_2.h"
#include "sdma_v4_4_2.h"

#define XCP_INST_MASK(num_inst, xcp_id)                                        \
	(num_inst ? GENMASK(num_inst - 1, 0) << (xcp_id * num_inst) : 0)

#define AMDGPU_XCP_OPS_KFD	(1 << 0)

void aqua_vanjaram_doorbell_index_init(struct amdgpu_device *adev)
{
	int i;

	adev->doorbell_index.kiq = AMDGPU_DOORBELL_LAYOUT1_KIQ_START;

	adev->doorbell_index.mec_ring0 = AMDGPU_DOORBELL_LAYOUT1_MEC_RING_START;

	adev->doorbell_index.userqueue_start = AMDGPU_DOORBELL_LAYOUT1_USERQUEUE_START;
	adev->doorbell_index.userqueue_end = AMDGPU_DOORBELL_LAYOUT1_USERQUEUE_END;
	adev->doorbell_index.xcc_doorbell_range = AMDGPU_DOORBELL_LAYOUT1_XCC_RANGE;

	adev->doorbell_index.sdma_doorbell_range = 20;
	for (i = 0; i < adev->sdma.num_instances; i++)
		adev->doorbell_index.sdma_engine[i] =
			AMDGPU_DOORBELL_LAYOUT1_sDMA_ENGINE_START +
			i * (adev->doorbell_index.sdma_doorbell_range >> 1);

	adev->doorbell_index.ih = AMDGPU_DOORBELL_LAYOUT1_IH;
	adev->doorbell_index.vcn.vcn_ring0_1 = AMDGPU_DOORBELL_LAYOUT1_VCN_START;

	adev->doorbell_index.first_non_cp = AMDGPU_DOORBELL_LAYOUT1_FIRST_NON_CP;
	adev->doorbell_index.last_non_cp = AMDGPU_DOORBELL_LAYOUT1_LAST_NON_CP;

	adev->doorbell_index.max_assignment = AMDGPU_DOORBELL_LAYOUT1_MAX_ASSIGNMENT << 1;
}

static void aqua_vanjaram_set_xcp_id(struct amdgpu_device *adev,
			     uint32_t inst_idx, struct amdgpu_ring *ring)
{
	int xcp_id;
	enum AMDGPU_XCP_IP_BLOCK ip_blk;
	uint32_t inst_mask;

	ring->xcp_id = AMDGPU_XCP_NO_PARTITION;
	if (adev->xcp_mgr->mode == AMDGPU_XCP_MODE_NONE)
		return;

	inst_mask = 1 << inst_idx;

	switch (ring->funcs->type) {
	case AMDGPU_HW_IP_GFX:
	case AMDGPU_RING_TYPE_COMPUTE:
	case AMDGPU_RING_TYPE_KIQ:
		ip_blk = AMDGPU_XCP_GFX;
		break;
	case AMDGPU_RING_TYPE_SDMA:
		ip_blk = AMDGPU_XCP_SDMA;
		break;
	case AMDGPU_RING_TYPE_VCN_ENC:
	case AMDGPU_RING_TYPE_VCN_JPEG:
		ip_blk = AMDGPU_XCP_VCN;
		if (adev->xcp_mgr->mode == AMDGPU_CPX_PARTITION_MODE)
			inst_mask = 1 << (inst_idx * 2);
		break;
	default:
		DRM_ERROR("Not support ring type %d!", ring->funcs->type);
		return;
	}

	for (xcp_id = 0; xcp_id < adev->xcp_mgr->num_xcps; xcp_id++) {
		if (adev->xcp_mgr->xcp[xcp_id].ip[ip_blk].inst_mask & inst_mask) {
			ring->xcp_id = xcp_id;
			break;
		}
	}
}

static void aqua_vanjaram_xcp_gpu_sched_update(
		struct amdgpu_device *adev,
		struct amdgpu_ring *ring,
		unsigned int sel_xcp_id)
{
	unsigned int *num_gpu_sched;

	num_gpu_sched = &adev->xcp_mgr->xcp[sel_xcp_id]
			.gpu_sched[ring->funcs->type][ring->hw_prio].num_scheds;
	adev->xcp_mgr->xcp[sel_xcp_id].gpu_sched[ring->funcs->type][ring->hw_prio]
			.sched[(*num_gpu_sched)++] = &ring->sched;
	DRM_DEBUG("%s :[%d] gpu_sched[%d][%d] = %d", ring->name,
			sel_xcp_id, ring->funcs->type,
			ring->hw_prio, *num_gpu_sched);
}

static int aqua_vanjaram_xcp_sched_list_update(
		struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	int i;

	for (i = 0; i < MAX_XCP; i++) {
		atomic_set(&adev->xcp_mgr->xcp[i].ref_cnt, 0);
		memset(adev->xcp_mgr->xcp[i].gpu_sched, 0, sizeof(adev->xcp_mgr->xcp->gpu_sched));
	}

	if (adev->xcp_mgr->mode == AMDGPU_XCP_MODE_NONE)
		return 0;

	for (i = 0; i < AMDGPU_MAX_RINGS; i++) {
		ring = adev->rings[i];
		if (!ring || !ring->sched.ready || ring->no_scheduler)
			continue;

		aqua_vanjaram_xcp_gpu_sched_update(adev, ring, ring->xcp_id);

		/* VCN is shared by two partitions under CPX MODE */
		if ((ring->funcs->type == AMDGPU_RING_TYPE_VCN_ENC ||
			ring->funcs->type == AMDGPU_RING_TYPE_VCN_JPEG) &&
			adev->xcp_mgr->mode == AMDGPU_CPX_PARTITION_MODE)
			aqua_vanjaram_xcp_gpu_sched_update(adev, ring, ring->xcp_id + 1);
	}

	return 0;
}

static int aqua_vanjaram_update_partition_sched_list(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->num_rings; i++) {
		struct amdgpu_ring *ring = adev->rings[i];

		if (ring->funcs->type == AMDGPU_RING_TYPE_COMPUTE ||
			ring->funcs->type == AMDGPU_RING_TYPE_KIQ)
			aqua_vanjaram_set_xcp_id(adev, ring->xcc_id, ring);
		else
			aqua_vanjaram_set_xcp_id(adev, ring->me, ring);
	}

	return aqua_vanjaram_xcp_sched_list_update(adev);
}

static int aqua_vanjaram_select_scheds(
		struct amdgpu_device *adev,
		u32 hw_ip,
		u32 hw_prio,
		struct amdgpu_fpriv *fpriv,
		unsigned int *num_scheds,
		struct drm_gpu_scheduler ***scheds)
{
	u32 sel_xcp_id;
	int i;

	if (fpriv->xcp_id == AMDGPU_XCP_NO_PARTITION) {
		u32 least_ref_cnt = ~0;

		fpriv->xcp_id = 0;
		for (i = 0; i < adev->xcp_mgr->num_xcps; i++) {
			u32 total_ref_cnt;

			total_ref_cnt = atomic_read(&adev->xcp_mgr->xcp[i].ref_cnt);
			if (total_ref_cnt < least_ref_cnt) {
				fpriv->xcp_id = i;
				least_ref_cnt = total_ref_cnt;
			}
		}
	}
	sel_xcp_id = fpriv->xcp_id;

	if (adev->xcp_mgr->xcp[sel_xcp_id].gpu_sched[hw_ip][hw_prio].num_scheds) {
		*num_scheds = adev->xcp_mgr->xcp[fpriv->xcp_id].gpu_sched[hw_ip][hw_prio].num_scheds;
		*scheds = adev->xcp_mgr->xcp[fpriv->xcp_id].gpu_sched[hw_ip][hw_prio].sched;
		atomic_inc(&adev->xcp_mgr->xcp[sel_xcp_id].ref_cnt);
		DRM_DEBUG("Selected partition #%d", sel_xcp_id);
	} else {
		DRM_ERROR("Failed to schedule partition #%d.", sel_xcp_id);
		return -ENOENT;
	}

	return 0;
}

static int8_t aqua_vanjaram_logical_to_dev_inst(struct amdgpu_device *adev,
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

static uint32_t aqua_vanjaram_logical_to_dev_mask(struct amdgpu_device *adev,
					 enum amd_hw_ip_block_type block,
					 uint32_t mask)
{
	uint32_t dev_mask = 0;
	int8_t log_inst, dev_inst;

	while (mask) {
		log_inst = ffs(mask) - 1;
		dev_inst = aqua_vanjaram_logical_to_dev_inst(adev, block, log_inst);
		dev_mask |= (1 << dev_inst);
		mask &= ~(1 << log_inst);
	}

	return dev_mask;
}

static void aqua_vanjaram_populate_ip_map(struct amdgpu_device *adev,
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

void aqua_vanjaram_ip_map_init(struct amdgpu_device *adev)
{
	u32 ip_map[][2] = {
		{ GC_HWIP, adev->gfx.xcc_mask },
		{ SDMA0_HWIP, adev->sdma.sdma_mask },
		{ VCN_HWIP, adev->vcn.inst_mask },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(ip_map); ++i)
		aqua_vanjaram_populate_ip_map(adev, ip_map[i][0], ip_map[i][1]);

	adev->ip_map.logical_to_dev_inst = aqua_vanjaram_logical_to_dev_inst;
	adev->ip_map.logical_to_dev_mask = aqua_vanjaram_logical_to_dev_mask;
}

/* Fixed pattern for smn addressing on different AIDs:
 *   bit[34]: indicate cross AID access
 *   bit[33:32]: indicate target AID id
 * AID id range is 0 ~ 3 as maximum AID number is 4.
 */
u64 aqua_vanjaram_encode_ext_smn_addressing(int ext_id)
{
	u64 ext_offset;

	/* local routing and bit[34:32] will be zeros */
	if (ext_id == 0)
		return 0;

	/* Initiated from host, accessing to all non-zero aids are cross traffic */
	ext_offset = ((u64)(ext_id & 0x3) << 32) | (1ULL << 34);

	return ext_offset;
}

static int aqua_vanjaram_query_partition_mode(struct amdgpu_xcp_mgr *xcp_mgr)
{
	enum amdgpu_gfx_partition mode = AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE;
	struct amdgpu_device *adev = xcp_mgr->adev;

	if (adev->nbio.funcs->get_compute_partition_mode)
		mode = adev->nbio.funcs->get_compute_partition_mode(adev);

	return mode;
}

static int __aqua_vanjaram_get_xcc_per_xcp(struct amdgpu_xcp_mgr *xcp_mgr, int mode)
{
	int num_xcc, num_xcc_per_xcp = 0;

	num_xcc = NUM_XCC(xcp_mgr->adev->gfx.xcc_mask);

	switch (mode) {
	case AMDGPU_SPX_PARTITION_MODE:
		num_xcc_per_xcp = num_xcc;
		break;
	case AMDGPU_DPX_PARTITION_MODE:
		num_xcc_per_xcp = num_xcc / 2;
		break;
	case AMDGPU_TPX_PARTITION_MODE:
		num_xcc_per_xcp = num_xcc / 3;
		break;
	case AMDGPU_QPX_PARTITION_MODE:
		num_xcc_per_xcp = num_xcc / 4;
		break;
	case AMDGPU_CPX_PARTITION_MODE:
		num_xcc_per_xcp = 1;
		break;
	}

	return num_xcc_per_xcp;
}

static int __aqua_vanjaram_get_xcp_ip_info(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id,
				    enum AMDGPU_XCP_IP_BLOCK ip_id,
				    struct amdgpu_xcp_ip *ip)
{
	struct amdgpu_device *adev = xcp_mgr->adev;
	int num_xcc_xcp, num_sdma_xcp, num_vcn_xcp;
	int num_sdma, num_vcn;

	num_sdma = adev->sdma.num_instances;
	num_vcn = adev->vcn.num_vcn_inst;

	switch (xcp_mgr->mode) {
	case AMDGPU_SPX_PARTITION_MODE:
		num_sdma_xcp = num_sdma;
		num_vcn_xcp = num_vcn;
		break;
	case AMDGPU_DPX_PARTITION_MODE:
		num_sdma_xcp = num_sdma / 2;
		num_vcn_xcp = num_vcn / 2;
		break;
	case AMDGPU_TPX_PARTITION_MODE:
		num_sdma_xcp = num_sdma / 3;
		num_vcn_xcp = num_vcn / 3;
		break;
	case AMDGPU_QPX_PARTITION_MODE:
		num_sdma_xcp = num_sdma / 4;
		num_vcn_xcp = num_vcn / 4;
		break;
	case AMDGPU_CPX_PARTITION_MODE:
		num_sdma_xcp = 2;
		num_vcn_xcp = num_vcn ? 1 : 0;
		break;
	default:
		return -EINVAL;
	}

	num_xcc_xcp = adev->gfx.num_xcc_per_xcp;

	switch (ip_id) {
	case AMDGPU_XCP_GFXHUB:
		ip->inst_mask = XCP_INST_MASK(num_xcc_xcp, xcp_id);
		ip->ip_funcs = &gfxhub_v1_2_xcp_funcs;
		break;
	case AMDGPU_XCP_GFX:
		ip->inst_mask = XCP_INST_MASK(num_xcc_xcp, xcp_id);
		ip->ip_funcs = &gfx_v9_4_3_xcp_funcs;
		break;
	case AMDGPU_XCP_SDMA:
		ip->inst_mask = XCP_INST_MASK(num_sdma_xcp, xcp_id);
		ip->ip_funcs = &sdma_v4_4_2_xcp_funcs;
		break;
	case AMDGPU_XCP_VCN:
		ip->inst_mask = XCP_INST_MASK(num_vcn_xcp, xcp_id);
		/* TODO : Assign IP funcs */
		break;
	default:
		return -EINVAL;
	}

	ip->ip_id = ip_id;

	return 0;
}

static enum amdgpu_gfx_partition
__aqua_vanjaram_get_auto_mode(struct amdgpu_xcp_mgr *xcp_mgr)
{
	struct amdgpu_device *adev = xcp_mgr->adev;
	int num_xcc;

	num_xcc = NUM_XCC(xcp_mgr->adev->gfx.xcc_mask);

	if (adev->gmc.num_mem_partitions == 1)
		return AMDGPU_SPX_PARTITION_MODE;

	if (adev->gmc.num_mem_partitions == num_xcc)
		return AMDGPU_CPX_PARTITION_MODE;

	if (adev->gmc.num_mem_partitions == num_xcc / 2)
		return (adev->flags & AMD_IS_APU) ? AMDGPU_TPX_PARTITION_MODE :
						    AMDGPU_QPX_PARTITION_MODE;

	if (adev->gmc.num_mem_partitions == 2 && !(adev->flags & AMD_IS_APU))
		return AMDGPU_DPX_PARTITION_MODE;

	return AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE;
}

static bool __aqua_vanjaram_is_valid_mode(struct amdgpu_xcp_mgr *xcp_mgr,
					  enum amdgpu_gfx_partition mode)
{
	struct amdgpu_device *adev = xcp_mgr->adev;
	int num_xcc, num_xccs_per_xcp;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	switch (mode) {
	case AMDGPU_SPX_PARTITION_MODE:
		return adev->gmc.num_mem_partitions == 1 && num_xcc > 0;
	case AMDGPU_DPX_PARTITION_MODE:
		return adev->gmc.num_mem_partitions != 8 && (num_xcc % 4) == 0;
	case AMDGPU_TPX_PARTITION_MODE:
		return (adev->gmc.num_mem_partitions == 1 ||
			adev->gmc.num_mem_partitions == 3) &&
		       ((num_xcc % 3) == 0);
	case AMDGPU_QPX_PARTITION_MODE:
		num_xccs_per_xcp = num_xcc / 4;
		return (adev->gmc.num_mem_partitions == 1 ||
			adev->gmc.num_mem_partitions == 4) &&
		       (num_xccs_per_xcp >= 2);
	case AMDGPU_CPX_PARTITION_MODE:
		return ((num_xcc > 1) &&
		       (adev->gmc.num_mem_partitions == 1 || adev->gmc.num_mem_partitions == 4) &&
		       (num_xcc % adev->gmc.num_mem_partitions) == 0);
	default:
		return false;
	}

	return false;
}

static int __aqua_vanjaram_pre_partition_switch(struct amdgpu_xcp_mgr *xcp_mgr, u32 flags)
{
	/* TODO:
	 * Stop user queues and threads, and make sure GPU is empty of work.
	 */

	if (flags & AMDGPU_XCP_OPS_KFD)
		amdgpu_amdkfd_device_fini_sw(xcp_mgr->adev);

	return 0;
}

static int __aqua_vanjaram_post_partition_switch(struct amdgpu_xcp_mgr *xcp_mgr, u32 flags)
{
	int ret = 0;

	if (flags & AMDGPU_XCP_OPS_KFD) {
		amdgpu_amdkfd_device_probe(xcp_mgr->adev);
		amdgpu_amdkfd_device_init(xcp_mgr->adev);
		/* If KFD init failed, return failure */
		if (!xcp_mgr->adev->kfd.init_complete)
			ret = -EIO;
	}

	return ret;
}

static int aqua_vanjaram_switch_partition_mode(struct amdgpu_xcp_mgr *xcp_mgr,
					       int mode, int *num_xcps)
{
	int num_xcc_per_xcp, num_xcc, ret;
	struct amdgpu_device *adev;
	u32 flags = 0;

	adev = xcp_mgr->adev;
	num_xcc = NUM_XCC(adev->gfx.xcc_mask);

	if (mode == AMDGPU_AUTO_COMPUTE_PARTITION_MODE) {
		mode = __aqua_vanjaram_get_auto_mode(xcp_mgr);
	} else if (!__aqua_vanjaram_is_valid_mode(xcp_mgr, mode)) {
		dev_err(adev->dev,
			"Invalid compute partition mode requested, requested: %s, available memory partitions: %d",
			amdgpu_gfx_compute_mode_desc(mode), adev->gmc.num_mem_partitions);
		return -EINVAL;
	}

	if (adev->kfd.init_complete && !amdgpu_in_reset(adev))
		flags |= AMDGPU_XCP_OPS_KFD;

	if (flags & AMDGPU_XCP_OPS_KFD) {
		ret = amdgpu_amdkfd_check_and_lock_kfd(adev);
		if (ret)
			goto out;
	}

	ret = __aqua_vanjaram_pre_partition_switch(xcp_mgr, flags);
	if (ret)
		goto unlock;

	num_xcc_per_xcp = __aqua_vanjaram_get_xcc_per_xcp(xcp_mgr, mode);
	if (adev->gfx.funcs->switch_partition_mode)
		adev->gfx.funcs->switch_partition_mode(xcp_mgr->adev,
						       num_xcc_per_xcp);

	/* Init info about new xcps */
	*num_xcps = num_xcc / num_xcc_per_xcp;
	amdgpu_xcp_init(xcp_mgr, *num_xcps, mode);

	ret = __aqua_vanjaram_post_partition_switch(xcp_mgr, flags);
unlock:
	if (flags & AMDGPU_XCP_OPS_KFD)
		amdgpu_amdkfd_unlock_kfd(adev);
out:
	return ret;
}

static int __aqua_vanjaram_get_xcp_mem_id(struct amdgpu_device *adev,
					  int xcc_id, uint8_t *mem_id)
{
	/* memory/spatial modes validation check is already done */
	*mem_id = xcc_id / adev->gfx.num_xcc_per_xcp;
	*mem_id /= adev->xcp_mgr->num_xcp_per_mem_partition;

	return 0;
}

static int aqua_vanjaram_get_xcp_mem_id(struct amdgpu_xcp_mgr *xcp_mgr,
					struct amdgpu_xcp *xcp, uint8_t *mem_id)
{
	struct amdgpu_numa_info numa_info;
	struct amdgpu_device *adev;
	uint32_t xcc_mask;
	int r, i, xcc_id;

	adev = xcp_mgr->adev;
	/* TODO: BIOS is not returning the right info now
	 * Check on this later
	 */
	/*
	if (adev->gmc.gmc_funcs->query_mem_partition_mode)
		mode = adev->gmc.gmc_funcs->query_mem_partition_mode(adev);
	*/
	if (adev->gmc.num_mem_partitions == 1) {
		/* Only one range */
		*mem_id = 0;
		return 0;
	}

	r = amdgpu_xcp_get_inst_details(xcp, AMDGPU_XCP_GFX, &xcc_mask);
	if (r || !xcc_mask)
		return -EINVAL;

	xcc_id = ffs(xcc_mask) - 1;
	if (!adev->gmc.is_app_apu)
		return __aqua_vanjaram_get_xcp_mem_id(adev, xcc_id, mem_id);

	r = amdgpu_acpi_get_mem_info(adev, xcc_id, &numa_info);

	if (r)
		return r;

	r = -EINVAL;
	for (i = 0; i < adev->gmc.num_mem_partitions; ++i) {
		if (adev->gmc.mem_partitions[i].numa.node == numa_info.nid) {
			*mem_id = i;
			r = 0;
			break;
		}
	}

	return r;
}

static int aqua_vanjaram_get_xcp_ip_details(struct amdgpu_xcp_mgr *xcp_mgr, int xcp_id,
				     enum AMDGPU_XCP_IP_BLOCK ip_id,
				     struct amdgpu_xcp_ip *ip)
{
	if (!ip)
		return -EINVAL;

	return __aqua_vanjaram_get_xcp_ip_info(xcp_mgr, xcp_id, ip_id, ip);
}

struct amdgpu_xcp_mgr_funcs aqua_vanjaram_xcp_funcs = {
	.switch_partition_mode = &aqua_vanjaram_switch_partition_mode,
	.query_partition_mode = &aqua_vanjaram_query_partition_mode,
	.get_ip_details = &aqua_vanjaram_get_xcp_ip_details,
	.get_xcp_mem_id = &aqua_vanjaram_get_xcp_mem_id,
	.select_scheds = &aqua_vanjaram_select_scheds,
	.update_partition_sched_list = &aqua_vanjaram_update_partition_sched_list
};

static int aqua_vanjaram_xcp_mgr_init(struct amdgpu_device *adev)
{
	int ret;

	ret = amdgpu_xcp_mgr_init(adev, AMDGPU_UNKNOWN_COMPUTE_PARTITION_MODE, 1,
				  &aqua_vanjaram_xcp_funcs);
	if (ret)
		return ret;

	/* TODO: Default memory node affinity init */

	return ret;
}

int aqua_vanjaram_init_soc_config(struct amdgpu_device *adev)
{
	u32 mask, inst_mask = adev->sdma.sdma_mask;
	int ret, i;

	/* generally 1 AID supports 4 instances */
	adev->sdma.num_inst_per_aid = 4;
	adev->sdma.num_instances = NUM_SDMA(adev->sdma.sdma_mask);

	adev->aid_mask = i = 1;
	inst_mask >>= adev->sdma.num_inst_per_aid;

	for (mask = (1 << adev->sdma.num_inst_per_aid) - 1; inst_mask;
	     inst_mask >>= adev->sdma.num_inst_per_aid, ++i) {
		if ((inst_mask & mask) == mask)
			adev->aid_mask |= (1 << i);
	}

	/* Harvest config is not used for aqua vanjaram. VCN and JPEGs will be
	 * addressed based on logical instance ids.
	 */
	adev->vcn.harvest_config = 0;
	adev->vcn.num_inst_per_aid = 1;
	adev->vcn.num_vcn_inst = hweight32(adev->vcn.inst_mask);
	adev->jpeg.harvest_config = 0;
	adev->jpeg.num_inst_per_aid = 1;
	adev->jpeg.num_jpeg_inst = hweight32(adev->jpeg.inst_mask);

	ret = aqua_vanjaram_xcp_mgr_init(adev);
	if (ret)
		return ret;

	aqua_vanjaram_ip_map_init(adev);

	return 0;
}
