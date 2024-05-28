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
#include "amdgpu_reg_state.h"
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

static bool aqua_vanjaram_xcp_vcn_shared(struct amdgpu_device *adev)
{
	return (adev->xcp_mgr->num_xcps > adev->vcn.num_vcn_inst);
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
		if (aqua_vanjaram_xcp_vcn_shared(adev))
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

		/* VCN may be shared by two partitions under CPX MODE in certain
		 * configs.
		 */
		if ((ring->funcs->type == AMDGPU_RING_TYPE_VCN_ENC ||
		     ring->funcs->type == AMDGPU_RING_TYPE_VCN_JPEG) &&
		    aqua_vanjaram_xcp_vcn_shared(adev))
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

static void aqua_read_smn(struct amdgpu_device *adev,
			  struct amdgpu_smn_reg_data *regdata,
			  uint64_t smn_addr)
{
	regdata->addr = smn_addr;
	regdata->value = RREG32_PCIE(smn_addr);
}

struct aqua_reg_list {
	uint64_t start_addr;
	uint32_t num_regs;
	uint32_t incrx;
};

#define DW_ADDR_INCR	4

static void aqua_read_smn_ext(struct amdgpu_device *adev,
			      struct amdgpu_smn_reg_data *regdata,
			      uint64_t smn_addr, int i)
{
	regdata->addr =
		smn_addr + adev->asic_funcs->encode_ext_smn_addressing(i);
	regdata->value = RREG32_PCIE_EXT(regdata->addr);
}

#define smnreg_0x1A340218	0x1A340218
#define smnreg_0x1A3402E4	0x1A3402E4
#define smnreg_0x1A340294	0x1A340294
#define smreg_0x1A380088	0x1A380088

#define NUM_PCIE_SMN_REGS	14

static struct aqua_reg_list pcie_reg_addrs[] = {
	{ smnreg_0x1A340218, 1, 0 },
	{ smnreg_0x1A3402E4, 1, 0 },
	{ smnreg_0x1A340294, 6, DW_ADDR_INCR },
	{ smreg_0x1A380088, 6, DW_ADDR_INCR },
};

static ssize_t aqua_vanjaram_read_pcie_state(struct amdgpu_device *adev,
					     void *buf, size_t max_size)
{
	struct amdgpu_reg_state_pcie_v1_0 *pcie_reg_state;
	uint32_t start_addr, incrx, num_regs, szbuf;
	struct amdgpu_regs_pcie_v1_0 *pcie_regs;
	struct amdgpu_smn_reg_data *reg_data;
	struct pci_dev *us_pdev, *ds_pdev;
	int aer_cap, r, n;

	if (!buf || !max_size)
		return -EINVAL;

	pcie_reg_state = (struct amdgpu_reg_state_pcie_v1_0 *)buf;

	szbuf = sizeof(*pcie_reg_state) +
		amdgpu_reginst_size(1, sizeof(*pcie_regs), NUM_PCIE_SMN_REGS);
	/* Only one instance of pcie regs */
	if (max_size < szbuf)
		return -EOVERFLOW;

	pcie_regs = (struct amdgpu_regs_pcie_v1_0 *)((uint8_t *)buf +
						     sizeof(*pcie_reg_state));
	pcie_regs->inst_header.instance = 0;
	pcie_regs->inst_header.state = AMDGPU_INST_S_OK;
	pcie_regs->inst_header.num_smn_regs = NUM_PCIE_SMN_REGS;

	reg_data = pcie_regs->smn_reg_values;

	for (r = 0; r < ARRAY_SIZE(pcie_reg_addrs); r++) {
		start_addr = pcie_reg_addrs[r].start_addr;
		incrx = pcie_reg_addrs[r].incrx;
		num_regs = pcie_reg_addrs[r].num_regs;
		for (n = 0; n < num_regs; n++) {
			aqua_read_smn(adev, reg_data, start_addr + n * incrx);
			++reg_data;
		}
	}

	ds_pdev = pci_upstream_bridge(adev->pdev);
	us_pdev = pci_upstream_bridge(ds_pdev);

	pcie_capability_read_word(us_pdev, PCI_EXP_DEVSTA,
				  &pcie_regs->device_status);
	pcie_capability_read_word(us_pdev, PCI_EXP_LNKSTA,
				  &pcie_regs->link_status);

	aer_cap = pci_find_ext_capability(us_pdev, PCI_EXT_CAP_ID_ERR);
	if (aer_cap) {
		pci_read_config_dword(us_pdev, aer_cap + PCI_ERR_COR_STATUS,
				      &pcie_regs->pcie_corr_err_status);
		pci_read_config_dword(us_pdev, aer_cap + PCI_ERR_UNCOR_STATUS,
				      &pcie_regs->pcie_uncorr_err_status);
	}

	pci_read_config_dword(us_pdev, PCI_PRIMARY_BUS,
			      &pcie_regs->sub_bus_number_latency);

	pcie_reg_state->common_header.structure_size = szbuf;
	pcie_reg_state->common_header.format_revision = 1;
	pcie_reg_state->common_header.content_revision = 0;
	pcie_reg_state->common_header.state_type = AMDGPU_REG_STATE_TYPE_PCIE;
	pcie_reg_state->common_header.num_instances = 1;

	return pcie_reg_state->common_header.structure_size;
}

#define smnreg_0x11A00050	0x11A00050
#define smnreg_0x11A00180	0x11A00180
#define smnreg_0x11A00070	0x11A00070
#define smnreg_0x11A00200	0x11A00200
#define smnreg_0x11A0020C	0x11A0020C
#define smnreg_0x11A00210	0x11A00210
#define smnreg_0x11A00108	0x11A00108

#define XGMI_LINK_REG(smnreg, l) ((smnreg) | (l << 20))

#define NUM_XGMI_SMN_REGS 25

static struct aqua_reg_list xgmi_reg_addrs[] = {
	{ smnreg_0x11A00050, 1, 0 },
	{ smnreg_0x11A00180, 16, DW_ADDR_INCR },
	{ smnreg_0x11A00070, 4, DW_ADDR_INCR },
	{ smnreg_0x11A00200, 1, 0 },
	{ smnreg_0x11A0020C, 1, 0 },
	{ smnreg_0x11A00210, 1, 0 },
	{ smnreg_0x11A00108, 1, 0 },
};

static ssize_t aqua_vanjaram_read_xgmi_state(struct amdgpu_device *adev,
					     void *buf, size_t max_size)
{
	struct amdgpu_reg_state_xgmi_v1_0 *xgmi_reg_state;
	uint32_t start_addr, incrx, num_regs, szbuf;
	struct amdgpu_regs_xgmi_v1_0 *xgmi_regs;
	struct amdgpu_smn_reg_data *reg_data;
	const int max_xgmi_instances = 8;
	int inst = 0, i, j, r, n;
	const int xgmi_inst = 2;
	void *p;

	if (!buf || !max_size)
		return -EINVAL;

	xgmi_reg_state = (struct amdgpu_reg_state_xgmi_v1_0 *)buf;

	szbuf = sizeof(*xgmi_reg_state) +
		amdgpu_reginst_size(max_xgmi_instances, sizeof(*xgmi_regs),
				    NUM_XGMI_SMN_REGS);
	/* Only one instance of pcie regs */
	if (max_size < szbuf)
		return -EOVERFLOW;

	p = &xgmi_reg_state->xgmi_state_regs[0];
	for_each_inst(i, adev->aid_mask) {
		for (j = 0; j < xgmi_inst; ++j) {
			xgmi_regs = (struct amdgpu_regs_xgmi_v1_0 *)p;
			xgmi_regs->inst_header.instance = inst++;

			xgmi_regs->inst_header.state = AMDGPU_INST_S_OK;
			xgmi_regs->inst_header.num_smn_regs = NUM_XGMI_SMN_REGS;

			reg_data = xgmi_regs->smn_reg_values;

			for (r = 0; r < ARRAY_SIZE(xgmi_reg_addrs); r++) {
				start_addr = xgmi_reg_addrs[r].start_addr;
				incrx = xgmi_reg_addrs[r].incrx;
				num_regs = xgmi_reg_addrs[r].num_regs;

				for (n = 0; n < num_regs; n++) {
					aqua_read_smn_ext(
						adev, reg_data,
						XGMI_LINK_REG(start_addr, j) +
							n * incrx,
						i);
					++reg_data;
				}
			}
			p = reg_data;
		}
	}

	xgmi_reg_state->common_header.structure_size = szbuf;
	xgmi_reg_state->common_header.format_revision = 1;
	xgmi_reg_state->common_header.content_revision = 0;
	xgmi_reg_state->common_header.state_type = AMDGPU_REG_STATE_TYPE_XGMI;
	xgmi_reg_state->common_header.num_instances = max_xgmi_instances;

	return xgmi_reg_state->common_header.structure_size;
}

#define smnreg_0x11C00070	0x11C00070
#define smnreg_0x11C00210	0x11C00210

static struct aqua_reg_list wafl_reg_addrs[] = {
	{ smnreg_0x11C00070, 4, DW_ADDR_INCR },
	{ smnreg_0x11C00210, 1, 0 },
};

#define WAFL_LINK_REG(smnreg, l) ((smnreg) | (l << 20))

#define NUM_WAFL_SMN_REGS 5

static ssize_t aqua_vanjaram_read_wafl_state(struct amdgpu_device *adev,
					     void *buf, size_t max_size)
{
	struct amdgpu_reg_state_wafl_v1_0 *wafl_reg_state;
	uint32_t start_addr, incrx, num_regs, szbuf;
	struct amdgpu_regs_wafl_v1_0 *wafl_regs;
	struct amdgpu_smn_reg_data *reg_data;
	const int max_wafl_instances = 8;
	int inst = 0, i, j, r, n;
	const int wafl_inst = 2;
	void *p;

	if (!buf || !max_size)
		return -EINVAL;

	wafl_reg_state = (struct amdgpu_reg_state_wafl_v1_0 *)buf;

	szbuf = sizeof(*wafl_reg_state) +
		amdgpu_reginst_size(max_wafl_instances, sizeof(*wafl_regs),
				    NUM_WAFL_SMN_REGS);

	if (max_size < szbuf)
		return -EOVERFLOW;

	p = &wafl_reg_state->wafl_state_regs[0];
	for_each_inst(i, adev->aid_mask) {
		for (j = 0; j < wafl_inst; ++j) {
			wafl_regs = (struct amdgpu_regs_wafl_v1_0 *)p;
			wafl_regs->inst_header.instance = inst++;

			wafl_regs->inst_header.state = AMDGPU_INST_S_OK;
			wafl_regs->inst_header.num_smn_regs = NUM_WAFL_SMN_REGS;

			reg_data = wafl_regs->smn_reg_values;

			for (r = 0; r < ARRAY_SIZE(wafl_reg_addrs); r++) {
				start_addr = wafl_reg_addrs[r].start_addr;
				incrx = wafl_reg_addrs[r].incrx;
				num_regs = wafl_reg_addrs[r].num_regs;
				for (n = 0; n < num_regs; n++) {
					aqua_read_smn_ext(
						adev, reg_data,
						WAFL_LINK_REG(start_addr, j) +
							n * incrx,
						i);
					++reg_data;
				}
			}
			p = reg_data;
		}
	}

	wafl_reg_state->common_header.structure_size = szbuf;
	wafl_reg_state->common_header.format_revision = 1;
	wafl_reg_state->common_header.content_revision = 0;
	wafl_reg_state->common_header.state_type = AMDGPU_REG_STATE_TYPE_WAFL;
	wafl_reg_state->common_header.num_instances = max_wafl_instances;

	return wafl_reg_state->common_header.structure_size;
}

#define smnreg_0x1B311060 0x1B311060
#define smnreg_0x1B411060 0x1B411060
#define smnreg_0x1B511060 0x1B511060
#define smnreg_0x1B611060 0x1B611060

#define smnreg_0x1C307120 0x1C307120
#define smnreg_0x1C317120 0x1C317120

#define smnreg_0x1C320830 0x1C320830
#define smnreg_0x1C380830 0x1C380830
#define smnreg_0x1C3D0830 0x1C3D0830
#define smnreg_0x1C420830 0x1C420830

#define smnreg_0x1C320100 0x1C320100
#define smnreg_0x1C380100 0x1C380100
#define smnreg_0x1C3D0100 0x1C3D0100
#define smnreg_0x1C420100 0x1C420100

#define smnreg_0x1B310500 0x1B310500
#define smnreg_0x1C300400 0x1C300400

#define USR_CAKE_INCR 0x11000
#define USR_LINK_INCR 0x100000
#define USR_CP_INCR 0x10000

#define NUM_USR_SMN_REGS	20

struct aqua_reg_list usr_reg_addrs[] = {
	{ smnreg_0x1B311060, 4, DW_ADDR_INCR },
	{ smnreg_0x1B411060, 4, DW_ADDR_INCR },
	{ smnreg_0x1B511060, 4, DW_ADDR_INCR },
	{ smnreg_0x1B611060, 4, DW_ADDR_INCR },
	{ smnreg_0x1C307120, 2, DW_ADDR_INCR },
	{ smnreg_0x1C317120, 2, DW_ADDR_INCR },
};

#define NUM_USR1_SMN_REGS	46
struct aqua_reg_list usr1_reg_addrs[] = {
	{ smnreg_0x1C320830, 6, USR_CAKE_INCR },
	{ smnreg_0x1C380830, 5, USR_CAKE_INCR },
	{ smnreg_0x1C3D0830, 5, USR_CAKE_INCR },
	{ smnreg_0x1C420830, 4, USR_CAKE_INCR },
	{ smnreg_0x1C320100, 6, USR_CAKE_INCR },
	{ smnreg_0x1C380100, 5, USR_CAKE_INCR },
	{ smnreg_0x1C3D0100, 5, USR_CAKE_INCR },
	{ smnreg_0x1C420100, 4, USR_CAKE_INCR },
	{ smnreg_0x1B310500, 4, USR_LINK_INCR },
	{ smnreg_0x1C300400, 2, USR_CP_INCR },
};

static ssize_t aqua_vanjaram_read_usr_state(struct amdgpu_device *adev,
					    void *buf, size_t max_size,
					    int reg_state)
{
	uint32_t start_addr, incrx, num_regs, szbuf, num_smn;
	struct amdgpu_reg_state_usr_v1_0 *usr_reg_state;
	struct amdgpu_regs_usr_v1_0 *usr_regs;
	struct amdgpu_smn_reg_data *reg_data;
	const int max_usr_instances = 4;
	struct aqua_reg_list *reg_addrs;
	int inst = 0, i, n, r, arr_size;
	void *p;

	if (!buf || !max_size)
		return -EINVAL;

	switch (reg_state) {
	case AMDGPU_REG_STATE_TYPE_USR:
		arr_size = ARRAY_SIZE(usr_reg_addrs);
		reg_addrs = usr_reg_addrs;
		num_smn = NUM_USR_SMN_REGS;
		break;
	case AMDGPU_REG_STATE_TYPE_USR_1:
		arr_size = ARRAY_SIZE(usr1_reg_addrs);
		reg_addrs = usr1_reg_addrs;
		num_smn = NUM_USR1_SMN_REGS;
		break;
	default:
		return -EINVAL;
	}

	usr_reg_state = (struct amdgpu_reg_state_usr_v1_0 *)buf;

	szbuf = sizeof(*usr_reg_state) + amdgpu_reginst_size(max_usr_instances,
							     sizeof(*usr_regs),
							     num_smn);
	if (max_size < szbuf)
		return -EOVERFLOW;

	p = &usr_reg_state->usr_state_regs[0];
	for_each_inst(i, adev->aid_mask) {
		usr_regs = (struct amdgpu_regs_usr_v1_0 *)p;
		usr_regs->inst_header.instance = inst++;
		usr_regs->inst_header.state = AMDGPU_INST_S_OK;
		usr_regs->inst_header.num_smn_regs = num_smn;
		reg_data = usr_regs->smn_reg_values;

		for (r = 0; r < arr_size; r++) {
			start_addr = reg_addrs[r].start_addr;
			incrx = reg_addrs[r].incrx;
			num_regs = reg_addrs[r].num_regs;
			for (n = 0; n < num_regs; n++) {
				aqua_read_smn_ext(adev, reg_data,
						  start_addr + n * incrx, i);
				reg_data++;
			}
		}
		p = reg_data;
	}

	usr_reg_state->common_header.structure_size = szbuf;
	usr_reg_state->common_header.format_revision = 1;
	usr_reg_state->common_header.content_revision = 0;
	usr_reg_state->common_header.state_type = AMDGPU_REG_STATE_TYPE_USR;
	usr_reg_state->common_header.num_instances = max_usr_instances;

	return usr_reg_state->common_header.structure_size;
}

ssize_t aqua_vanjaram_get_reg_state(struct amdgpu_device *adev,
				    enum amdgpu_reg_state reg_state, void *buf,
				    size_t max_size)
{
	ssize_t size;

	switch (reg_state) {
	case AMDGPU_REG_STATE_TYPE_PCIE:
		size = aqua_vanjaram_read_pcie_state(adev, buf, max_size);
		break;
	case AMDGPU_REG_STATE_TYPE_XGMI:
		size = aqua_vanjaram_read_xgmi_state(adev, buf, max_size);
		break;
	case AMDGPU_REG_STATE_TYPE_WAFL:
		size = aqua_vanjaram_read_wafl_state(adev, buf, max_size);
		break;
	case AMDGPU_REG_STATE_TYPE_USR:
		size = aqua_vanjaram_read_usr_state(adev, buf, max_size,
						    AMDGPU_REG_STATE_TYPE_USR);
		break;
	case AMDGPU_REG_STATE_TYPE_USR_1:
		size = aqua_vanjaram_read_usr_state(
			adev, buf, max_size, AMDGPU_REG_STATE_TYPE_USR_1);
		break;
	default:
		return -EINVAL;
	}

	return size;
}
