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
#include "gmc_v12_1.h"
#include "soc15_common.h"
#include "soc_v1_0_enum.h"
#include "oss/osssys_7_1_0_offset.h"
#include "oss/osssys_7_1_0_sh_mask.h"
#include "ivsrcid/vmc/irqsrcs_vmc_1_0.h"

static int gmc_v12_1_vm_fault_interrupt_state(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *src,
					      unsigned int type,
					      enum amdgpu_interrupt_state state)
{
	struct amdgpu_vmhub *hub;
	u32 tmp, reg, i, j;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		for_each_set_bit(j, adev->vmhubs_mask, AMDGPU_MAX_VMHUBS) {
			hub = &adev->vmhub[j];
			for (i = 0; i < 16; i++) {
				reg = hub->vm_context0_cntl + i;

				/* This works because this interrupt is only
				 * enabled at init/resume and disabled in
				 * fini/suspend, so the overall state doesn't
				 * change over the course of suspend/resume.
				 */
				if (adev->in_s0ix && (j == AMDGPU_GFXHUB(0)))
					continue;

				if (j >= AMDGPU_MMHUB0(0))
					tmp = RREG32_SOC15_IP(MMHUB, reg);
				else
					tmp = RREG32_XCC(reg, j);

				tmp &= ~hub->vm_cntx_cntl_vm_fault;

				if (j >= AMDGPU_MMHUB0(0))
					WREG32_SOC15_IP(MMHUB, reg, tmp);
				else
					WREG32_XCC(reg, tmp, j);
			}
		}
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		for_each_set_bit(j, adev->vmhubs_mask, AMDGPU_MAX_VMHUBS) {
			hub = &adev->vmhub[j];
			for (i = 0; i < 16; i++) {
				reg = hub->vm_context0_cntl + i;

				/* This works because this interrupt is only
				 * enabled at init/resume and disabled in
				 * fini/suspend, so the overall state doesn't
				 * change over the course of suspend/resume.
				 */
				if (adev->in_s0ix && (j == AMDGPU_GFXHUB(0)))
					continue;

				if (j >= AMDGPU_MMHUB0(0))
					tmp = RREG32_SOC15_IP(MMHUB, reg);
				else
					tmp = RREG32_XCC(reg, j);

				tmp |= hub->vm_cntx_cntl_vm_fault;

				if (j >= AMDGPU_MMHUB0(0))
					WREG32_SOC15_IP(MMHUB, reg, tmp);
				else
					WREG32_XCC(reg, tmp, j);
			}
		}
		break;
	default:
		break;
	}

	return 0;
}

static int gmc_v12_1_process_interrupt(struct amdgpu_device *adev,
				       struct amdgpu_irq_src *source,
				       struct amdgpu_iv_entry *entry)
{
	struct amdgpu_task_info *task_info;
	bool retry_fault = false, write_fault = false;
	unsigned int vmhub, node_id;
	struct amdgpu_vmhub *hub;
	uint32_t cam_index = 0;
	const char *hub_name;
	int ret, xcc_id = 0;
	uint32_t status = 0;
	u64 addr;

	node_id = entry->node_id;

	addr = (u64)entry->src_data[0] << 12;
	addr |= ((u64)entry->src_data[1] & 0x1fff) << 44;

	if (entry->src_id == UTCL2_1_0__SRCID__RETRY) {
		retry_fault = true;
		write_fault = !!(entry->src_data[1] & 0x200000);
	}

	if (entry->client_id == SOC_V1_0_IH_CLIENTID_VMC) {
		hub_name = "mmhub0";
		vmhub = AMDGPU_MMHUB0(node_id / 4);
	} else {
		hub_name = "gfxhub0";
		if (adev->gfx.funcs->ih_node_to_logical_xcc) {
			xcc_id = adev->gfx.funcs->ih_node_to_logical_xcc(adev,
								node_id);
			if (xcc_id < 0)
				xcc_id = 0;
		}
		vmhub = xcc_id;
	}

	hub = &adev->vmhub[vmhub];

	if (retry_fault) {
		if (adev->irq.retry_cam_enabled) {
			/* Delegate it to a different ring if the hardware hasn't
			 * already done it.
			 */
			if (entry->ih == &adev->irq.ih) {
				amdgpu_irq_delegate(adev, entry, 8);
				return 1;
			}

			cam_index = entry->src_data[3] & 0x3ff;

			ret = amdgpu_vm_handle_fault(adev, entry->pasid, entry->vmid, node_id,
							addr, entry->timestamp, write_fault);
			WDOORBELL32(adev->irq.retry_cam_doorbell_index, cam_index);
			if (ret)
				return 1;
		} else {
			/* Process it onyl if it's the first fault for this address */
			if (entry->ih != &adev->irq.ih_soft &&
				amdgpu_gmc_filter_faults(adev, entry->ih, addr, entry->pasid,
							 entry->timestamp))
				return 1;

			/* Delegate it to a different ring if the hardware hasn't
			 * already done it.
			 */
			if (entry->ih == &adev->irq.ih) {
				amdgpu_irq_delegate(adev, entry, 8);
				return 1;
			}

			/* Try to handle the recoverable page faults by filling page
			 * tables
			 */
			if (amdgpu_vm_handle_fault(adev, entry->pasid, entry->vmid, node_id,
						   addr, entry->timestamp, write_fault))
				return 1;
		}
	}

	if (kgd2kfd_vmfault_fast_path(adev, entry, retry_fault))
		return 1;

	if (!printk_ratelimit())
		return 0;

	dev_err(adev->dev,
		"[%s] %s page fault (src_id:%u ring:%u vmid:%u pasid:%u)\n", hub_name,
		retry_fault ? "retry" : "no-retry",
		entry->src_id, entry->ring_id, entry->vmid, entry->pasid);

	task_info = amdgpu_vm_get_task_info_pasid(adev, entry->pasid);
	if (task_info) {
		amdgpu_vm_print_task_info(adev, task_info);
		amdgpu_vm_put_task_info(task_info);
	}

	dev_err(adev->dev, "  in page starting at address 0x%016llx from IH client %d (%s)\n",
		addr, entry->client_id, soc_v1_0_ih_clientid_name[entry->client_id]);

	if (amdgpu_sriov_vf(adev))
		return 0;

	/*
	 * Issue a dummy read to wait for the status register to
	 * be updated to avoid reading an incorrect value due to
	 * the new fast GRBM interface.
	 */
	if (entry->vmid_src == AMDGPU_GFXHUB(0))
		RREG32(hub->vm_l2_pro_fault_status);

	status = RREG32(hub->vm_l2_pro_fault_status);

	/* Only print L2 fault status if the status register could be read and
	 * contains useful information
	 */
	if (!status)
		return 0;

	WREG32_P(hub->vm_l2_pro_fault_cntl, 1, ~1);

	amdgpu_vm_update_fault_cache(adev, entry->pasid, addr, status, vmhub);

	hub->vmhub_funcs->print_l2_protection_fault_status(adev, status);

	return 0;
}

static bool gmc_v12_1_get_vmid_pasid_mapping_info(struct amdgpu_device *adev,
						  uint8_t vmid, uint8_t inst,
						  uint16_t *p_pasid)
{
	uint16_t index;

	if (inst/4)
		index = 0xA + inst%4;
	else
		index = 0x2 + inst%4;

	WREG32(SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_LUT_INDEX), index);

	*p_pasid = RREG32(SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_0_LUT) + vmid) & 0xffff;

	return !!(*p_pasid);
}

/*
 * GART
 * VMID 0 is the physical GPU addresses as used by the kernel.
 * VMIDs 1-15 are used for userspace clients and are handled
 * by the amdgpu vm/hsa code.
 */

static void gmc_v12_1_flush_vm_hub(struct amdgpu_device *adev, uint32_t vmid,
				   unsigned int vmhub, uint32_t flush_type)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[vmhub];
	u32 inv_req = hub->vmhub_funcs->get_invalidate_req(vmid, flush_type);
	u32 tmp;
	/* Use register 17 for GART */
	const unsigned eng = 17;
	unsigned int i;
	unsigned char hub_ip = 0;

	hub_ip = (AMDGPU_IS_GFXHUB(vmhub)) ?
		   GC_HWIP : MMHUB_HWIP;

	spin_lock(&adev->gmc.invalidate_lock);

	WREG32_RLC_NO_KIQ(hub->vm_inv_eng0_req + hub->eng_distance * eng, inv_req, hub_ip);

	/* Wait for ACK with a delay.*/
	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32_RLC_NO_KIQ(hub->vm_inv_eng0_ack +
				    hub->eng_distance * eng, hub_ip);
		tmp &= 1 << vmid;
		if (tmp)
			break;

		udelay(1);
	}

	/* Issue additional private vm invalidation to MMHUB */
	if (!AMDGPU_IS_GFXHUB(vmhub) &&
	    (hub->vm_l2_bank_select_reserved_cid2) &&
		!amdgpu_sriov_vf(adev)) {
		inv_req = RREG32_NO_KIQ(hub->vm_l2_bank_select_reserved_cid2);
		/* bit 25: RSERVED_CACHE_PRIVATE_INVALIDATION */
		inv_req |= (1 << 25);
		/* Issue private invalidation */
		WREG32_NO_KIQ(hub->vm_l2_bank_select_reserved_cid2, inv_req);
		/* Read back to ensure invalidation is done*/
		RREG32_NO_KIQ(hub->vm_l2_bank_select_reserved_cid2);
	}

	spin_unlock(&adev->gmc.invalidate_lock);

	if (i < adev->usec_timeout)
		return;

	dev_err(adev->dev, "Timeout waiting for VM flush ACK!\n");
}

/**
 * gmc_v12_1_flush_gpu_tlb - gart tlb flush callback
 *
 * @adev: amdgpu_device pointer
 * @vmid: vm instance to flush
 * @vmhub: which hub to flush
 * @flush_type: the flush type
 *
 * Flush the TLB for the requested page table.
 */
static void gmc_v12_1_flush_gpu_tlb(struct amdgpu_device *adev, uint32_t vmid,
				    uint32_t vmhub, uint32_t flush_type)
{
	u32 inst;

	if (AMDGPU_IS_GFXHUB(vmhub) &&
	    !adev->gfx.is_poweron)
		return;

	if (vmhub >= AMDGPU_MMHUB0(0))
		inst = 0;
	else
		inst = vmhub;

	/* This is necessary for SRIOV as well as for GFXOFF to function
	 * properly under bare metal
	 */
	if (((adev->gfx.kiq[inst].ring.sched.ready ||
	      adev->mes.ring[MES_PIPE_INST(inst, 0)].sched.ready) &&
	    (amdgpu_sriov_runtime(adev) || !amdgpu_sriov_vf(adev)))) {
		struct amdgpu_vmhub *hub = &adev->vmhub[vmhub];
		const unsigned eng = 17;
		u32 inv_req = hub->vmhub_funcs->get_invalidate_req(vmid, flush_type);
		u32 req = hub->vm_inv_eng0_req + hub->eng_distance * eng;
		u32 ack = hub->vm_inv_eng0_ack + hub->eng_distance * eng;

		amdgpu_gmc_fw_reg_write_reg_wait(adev, req, ack, inv_req,
				1 << vmid, inst);
		return;
	}

	mutex_lock(&adev->mman.gtt_window_lock);
	gmc_v12_1_flush_vm_hub(adev, vmid, vmhub, 0);
	mutex_unlock(&adev->mman.gtt_window_lock);
	return;
}

/**
 * gmc_v12_1_flush_gpu_tlb_pasid - tlb flush via pasid
 *
 * @adev: amdgpu_device pointer
 * @pasid: pasid to be flush
 * @flush_type: the flush type
 * @all_hub: flush all hubs
 * @inst: is used to select which instance of KIQ to use for the invalidation
 *
 * Flush the TLB for the requested pasid.
 */
static void gmc_v12_1_flush_gpu_tlb_pasid(struct amdgpu_device *adev,
					  uint16_t pasid, uint32_t flush_type,
					  bool all_hub, uint32_t inst)
{
	uint16_t queried;
	int vmid, i;

	if (adev->enable_uni_mes && adev->mes.ring[0].sched.ready &&
	    (adev->mes.sched_version & AMDGPU_MES_VERSION_MASK) >= 0x6f) {
		struct mes_inv_tlbs_pasid_input input = {0};
		input.xcc_id = inst;
		input.pasid = pasid;
		input.flush_type = flush_type;

		/* MES will invalidate hubs for the device(including slave xcc) from master, ignore request from slave */
		if (!amdgpu_gfx_is_master_xcc(adev, inst))
			return;

		input.hub_id = AMDGPU_GFXHUB(0);
		adev->mes.funcs->invalidate_tlbs_pasid(&adev->mes, &input);

		if (all_hub) {
			/* invalidate mm_hub */
			if (test_bit(AMDGPU_MMHUB1(0), adev->vmhubs_mask)) {
				input.hub_id = AMDGPU_MMHUB0(0);
				adev->mes.funcs->invalidate_tlbs_pasid(&adev->mes, &input);
			}
			if (test_bit(AMDGPU_MMHUB1(0), adev->vmhubs_mask)) {
				input.hub_id = AMDGPU_MMHUB1(0);
				adev->mes.funcs->invalidate_tlbs_pasid(&adev->mes, &input);
			}
		}
		return;
	}

	for (vmid = 1; vmid < 16; vmid++) {
		bool valid;

		valid = gmc_v12_1_get_vmid_pasid_mapping_info(adev, vmid, inst,
							      &queried);
		if (!valid || queried != pasid)
			continue;

		if (all_hub) {
			for_each_set_bit(i, adev->vmhubs_mask,
					 AMDGPU_MAX_VMHUBS)
				gmc_v12_1_flush_gpu_tlb(adev, vmid, i,
							flush_type);
		} else {
			gmc_v12_1_flush_gpu_tlb(adev, vmid, AMDGPU_GFXHUB(inst),
						flush_type);
		}
	}
}

static uint64_t gmc_v12_1_emit_flush_gpu_tlb(struct amdgpu_ring *ring,
					     unsigned vmid, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->vm_hub];
	uint32_t req = hub->vmhub_funcs->get_invalidate_req(vmid, 0);
	unsigned eng = ring->vm_inv_eng;

	amdgpu_ring_emit_wreg(ring, hub->ctx0_ptb_addr_lo32 +
			      (hub->ctx_addr_distance * vmid),
			      lower_32_bits(pd_addr));

	amdgpu_ring_emit_wreg(ring, hub->ctx0_ptb_addr_hi32 +
			      (hub->ctx_addr_distance * vmid),
			      upper_32_bits(pd_addr));

	amdgpu_ring_emit_reg_write_reg_wait(ring, hub->vm_inv_eng0_req +
					    hub->eng_distance * eng,
					    hub->vm_inv_eng0_ack +
					    hub->eng_distance * eng,
					    req, 1 << vmid);

	return pd_addr;
}

static void gmc_v12_1_emit_pasid_mapping(struct amdgpu_ring *ring,
					 unsigned vmid, unsigned pasid)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t reg;

	if (ring->vm_hub == AMDGPU_GFXHUB(0))
		reg = SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_0_LUT) + vmid;
	else
		reg = SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_0_LUT_MM) + vmid;

	amdgpu_ring_emit_wreg(ring, reg, pasid);
}

/*
 * PTE format:
 * 63 P
 * 62:59 reserved
 * 58 D
 * 57 G
 * 56 T
 * 55:54 M
 * 53:52 SW
 * 51:48 reserved for future
 * 47:12 4k physical page base address
 * 11:7 fragment
 * 6 write
 * 5 read
 * 4 exe
 * 3 Z
 * 2 snooped
 * 1 system
 * 0 valid
 *
 * PDE format:
 * 63 P
 * 62:58 block fragment size
 * 57 reserved
 * 56 A
 * 55:54 M
 * 53:52 reserved
 * 51:48 reserved for future
 * 47:6 physical base address of PD or PTE
 * 5:3 reserved
 * 2 C
 * 1 system
 * 0 valid
 */

static void gmc_v12_1_get_vm_pde(struct amdgpu_device *adev, int level,
				 uint64_t *addr, uint64_t *flags)
{
	if (!(*flags & AMDGPU_PDE_PTE_GFX12) && !(*flags & AMDGPU_PTE_SYSTEM))
		*addr = adev->vm_manager.vram_base_offset + *addr -
			adev->gmc.vram_start;
	BUG_ON(*addr & 0xFFFF00000000003FULL);

	*flags |= AMDGPU_PTE_SNOOPED;

	if (!adev->gmc.translate_further)
		return;

	if (level == AMDGPU_VM_PDB1) {
		/* Set the block fragment size */
		if (!(*flags & AMDGPU_PDE_PTE_GFX12))
			*flags |= AMDGPU_PDE_BFS_GFX12(0x9);

	} else if (level == AMDGPU_VM_PDB0) {
		if (*flags & AMDGPU_PDE_PTE_GFX12)
			*flags &= ~AMDGPU_PDE_PTE_GFX12;
	}
}

static void gmc_v12_1_get_coherence_flags(struct amdgpu_device *adev,
					  struct amdgpu_bo *bo,
					  uint64_t *flags)
{
	struct amdgpu_device *bo_adev = amdgpu_ttm_adev(bo->tbo.bdev);
	bool is_vram = bo->tbo.resource &&
		       bo->tbo.resource->mem_type == TTM_PL_VRAM;
	bool coherent = bo->flags & (AMDGPU_GEM_CREATE_COHERENT |
				     AMDGPU_GEM_CREATE_EXT_COHERENT);
	bool ext_coherent = bo->flags & AMDGPU_GEM_CREATE_EXT_COHERENT;
	uint32_t gc_ip_version = amdgpu_ip_version(adev, GC_HWIP, 0);
	bool uncached = bo->flags & AMDGPU_GEM_CREATE_UNCACHED;
	unsigned int mtype, mtype_local;
	bool snoop = false;
	bool is_local = false;

	switch (gc_ip_version) {
	case IP_VERSION(12, 1, 0):
		mtype_local = MTYPE_RW;
		if (amdgpu_mtype_local == 1) {
			DRM_INFO_ONCE("Using MTYPE_NC for local memory\n");
			mtype_local = MTYPE_NC;
		} else if (amdgpu_mtype_local == 2) {
			DRM_INFO_ONCE("MTYPE_CC not supported, using MTYPE_RW instead for local memory\n");
		} else {
			DRM_INFO_ONCE("Using MTYPE_RW for local memory\n");
		}

		is_local = (is_vram && adev == bo_adev);
		snoop = true;
		if (uncached) {
			mtype = MTYPE_UC;
		} else if (ext_coherent) {
			mtype = is_local ? mtype_local : MTYPE_UC;
		} else {
			if (is_local)
				mtype = mtype_local;
			else
				mtype = MTYPE_NC;
		}
		break;
	default:
		if (uncached || coherent)
			mtype = MTYPE_UC;
		else
			mtype = MTYPE_NC;
	}

	if (mtype != MTYPE_NC)
		*flags = AMDGPU_PTE_MTYPE_GFX12(*flags, mtype);

	if (is_local || adev->have_atomics_support)
		*flags |= AMDGPU_PTE_BUS_ATOMICS;

	*flags |= snoop ? AMDGPU_PTE_SNOOPED : 0;
}

static void gmc_v12_1_get_vm_pte(struct amdgpu_device *adev,
				 struct amdgpu_vm *vm,
				 struct amdgpu_bo *bo,
				 uint32_t vm_flags,
				 uint64_t *flags)
{
	if (vm_flags & AMDGPU_VM_PAGE_EXECUTABLE)
		*flags |= AMDGPU_PTE_EXECUTABLE;
	else
		*flags &= ~AMDGPU_PTE_EXECUTABLE;

	switch (vm_flags & AMDGPU_VM_MTYPE_MASK) {
	case AMDGPU_VM_MTYPE_DEFAULT:
		*flags = AMDGPU_PTE_MTYPE_GFX12(*flags, MTYPE_NC);
		break;
	case AMDGPU_VM_MTYPE_NC:
	default:
		*flags = AMDGPU_PTE_MTYPE_GFX12(*flags, MTYPE_NC);
		break;
	case AMDGPU_VM_MTYPE_RW:
		*flags = AMDGPU_PTE_MTYPE_GFX12(*flags, MTYPE_RW);
		break;
	case AMDGPU_VM_MTYPE_UC:
		*flags = AMDGPU_PTE_MTYPE_GFX12(*flags, MTYPE_UC);
		break;
	}

	if ((*flags & AMDGPU_PTE_VALID) && bo)
		gmc_v12_1_get_coherence_flags(adev, bo, flags);
}

static const struct amdgpu_gmc_funcs gmc_v12_1_gmc_funcs = {
	.flush_gpu_tlb = gmc_v12_1_flush_gpu_tlb,
	.flush_gpu_tlb_pasid = gmc_v12_1_flush_gpu_tlb_pasid,
	.emit_flush_gpu_tlb = gmc_v12_1_emit_flush_gpu_tlb,
	.emit_pasid_mapping = gmc_v12_1_emit_pasid_mapping,
	.get_vm_pde = gmc_v12_1_get_vm_pde,
	.get_vm_pte = gmc_v12_1_get_vm_pte,
	.query_mem_partition_mode = &amdgpu_gmc_query_memory_partition,
	.request_mem_partition_mode = &amdgpu_gmc_request_memory_partition,
};

void gmc_v12_1_set_gmc_funcs(struct amdgpu_device *adev)
{
	adev->gmc.gmc_funcs = &gmc_v12_1_gmc_funcs;
}

static const struct amdgpu_irq_src_funcs gmc_v12_1_irq_funcs = {
	.set = gmc_v12_1_vm_fault_interrupt_state,
	.process = gmc_v12_1_process_interrupt,
};

void gmc_v12_1_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gmc.vm_fault.num_types = 1;
	adev->gmc.vm_fault.funcs = &gmc_v12_1_irq_funcs;
}

void gmc_v12_1_init_vram_info(struct amdgpu_device *adev)
{
	/* TODO: query vram_info from ip discovery binary */
	adev->gmc.vram_type = AMDGPU_VRAM_TYPE_HBM4;
	adev->gmc.vram_width = 384 * 64;
}
