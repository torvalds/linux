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
 *
 */
#include <linux/firmware.h>
#include <linux/pci.h>

#include <drm/drm_cache.h>

#include "amdgpu.h"
#include "amdgpu_atomfirmware.h"
#include "gmc_v10_0.h"
#include "umc_v8_7.h"

#include "athub/athub_2_0_0_sh_mask.h"
#include "athub/athub_2_0_0_offset.h"
#include "dcn/dcn_2_0_0_offset.h"
#include "dcn/dcn_2_0_0_sh_mask.h"
#include "oss/osssys_5_0_0_offset.h"
#include "ivsrcid/vmc/irqsrcs_vmc_1_0.h"
#include "navi10_enum.h"

#include "soc15.h"
#include "soc15d.h"
#include "soc15_common.h"

#include "nbio_v2_3.h"

#include "gfxhub_v2_0.h"
#include "gfxhub_v2_1.h"
#include "mmhub_v2_0.h"
#include "mmhub_v2_3.h"
#include "athub_v2_0.h"
#include "athub_v2_1.h"

#include "amdgpu_reset.h"

#if 0
static const struct soc15_reg_golden golden_settings_navi10_hdp[] =
{
	/* TODO add golden setting for hdp */
};
#endif

static int gmc_v10_0_ecc_interrupt_state(struct amdgpu_device *adev,
					 struct amdgpu_irq_src *src,
					 unsigned type,
					 enum amdgpu_interrupt_state state)
{
	return 0;
}

static int
gmc_v10_0_vm_fault_interrupt_state(struct amdgpu_device *adev,
				   struct amdgpu_irq_src *src, unsigned type,
				   enum amdgpu_interrupt_state state)
{
	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		/* MM HUB */
		amdgpu_gmc_set_vm_fault_masks(adev, AMDGPU_MMHUB0(0), false);
		/* GFX HUB */
		/* This works because this interrupt is only
		 * enabled at init/resume and disabled in
		 * fini/suspend, so the overall state doesn't
		 * change over the course of suspend/resume.
		 */
		if (!adev->in_s0ix)
			amdgpu_gmc_set_vm_fault_masks(adev, AMDGPU_GFXHUB(0), false);
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		/* MM HUB */
		amdgpu_gmc_set_vm_fault_masks(adev, AMDGPU_MMHUB0(0), true);
		/* GFX HUB */
		/* This works because this interrupt is only
		 * enabled at init/resume and disabled in
		 * fini/suspend, so the overall state doesn't
		 * change over the course of suspend/resume.
		 */
		if (!adev->in_s0ix)
			amdgpu_gmc_set_vm_fault_masks(adev, AMDGPU_GFXHUB(0), true);
		break;
	default:
		break;
	}

	return 0;
}

static int gmc_v10_0_process_interrupt(struct amdgpu_device *adev,
				       struct amdgpu_irq_src *source,
				       struct amdgpu_iv_entry *entry)
{
	bool retry_fault = !!(entry->src_data[1] & 0x80);
	bool write_fault = !!(entry->src_data[1] & 0x20);
	struct amdgpu_vmhub *hub = &adev->vmhub[entry->vmid_src];
	struct amdgpu_task_info task_info;
	uint32_t status = 0;
	u64 addr;

	addr = (u64)entry->src_data[0] << 12;
	addr |= ((u64)entry->src_data[1] & 0xf) << 44;

	if (retry_fault) {
		/* Returning 1 here also prevents sending the IV to the KFD */

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
		if (amdgpu_vm_handle_fault(adev, entry->pasid, 0, 0, addr, write_fault))
			return 1;
	}

	if (!amdgpu_sriov_vf(adev)) {
		/*
		 * Issue a dummy read to wait for the status register to
		 * be updated to avoid reading an incorrect value due to
		 * the new fast GRBM interface.
		 */
		if ((entry->vmid_src == AMDGPU_GFXHUB(0)) &&
		    (adev->ip_versions[GC_HWIP][0] < IP_VERSION(10, 3, 0)))
			RREG32(hub->vm_l2_pro_fault_status);

		status = RREG32(hub->vm_l2_pro_fault_status);
		WREG32_P(hub->vm_l2_pro_fault_cntl, 1, ~1);
	}

	if (!printk_ratelimit())
		return 0;

	memset(&task_info, 0, sizeof(struct amdgpu_task_info));
	amdgpu_vm_get_task_info(adev, entry->pasid, &task_info);

	dev_err(adev->dev,
		"[%s] page fault (src_id:%u ring:%u vmid:%u pasid:%u, "
		"for process %s pid %d thread %s pid %d)\n",
		entry->vmid_src ? "mmhub" : "gfxhub",
		entry->src_id, entry->ring_id, entry->vmid,
		entry->pasid, task_info.process_name, task_info.tgid,
		task_info.task_name, task_info.pid);
	dev_err(adev->dev, "  in page starting at address 0x%016llx from client 0x%x (%s)\n",
		addr, entry->client_id,
		soc15_ih_clientid_name[entry->client_id]);

	if (!amdgpu_sriov_vf(adev))
		hub->vmhub_funcs->print_l2_protection_fault_status(adev,
								   status);

	return 0;
}

static const struct amdgpu_irq_src_funcs gmc_v10_0_irq_funcs = {
	.set = gmc_v10_0_vm_fault_interrupt_state,
	.process = gmc_v10_0_process_interrupt,
};

static const struct amdgpu_irq_src_funcs gmc_v10_0_ecc_funcs = {
	.set = gmc_v10_0_ecc_interrupt_state,
	.process = amdgpu_umc_process_ecc_irq,
};

static void gmc_v10_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gmc.vm_fault.num_types = 1;
	adev->gmc.vm_fault.funcs = &gmc_v10_0_irq_funcs;

	if (!amdgpu_sriov_vf(adev)) {
		adev->gmc.ecc_irq.num_types = 1;
		adev->gmc.ecc_irq.funcs = &gmc_v10_0_ecc_funcs;
	}
}

/**
 * gmc_v10_0_use_invalidate_semaphore - judge whether to use semaphore
 *
 * @adev: amdgpu_device pointer
 * @vmhub: vmhub type
 *
 */
static bool gmc_v10_0_use_invalidate_semaphore(struct amdgpu_device *adev,
				       uint32_t vmhub)
{
	return ((vmhub == AMDGPU_MMHUB0(0)) &&
		(!amdgpu_sriov_vf(adev)));
}

static bool gmc_v10_0_get_atc_vmid_pasid_mapping_info(
					struct amdgpu_device *adev,
					uint8_t vmid, uint16_t *p_pasid)
{
	uint32_t value;

	value = RREG32(SOC15_REG_OFFSET(ATHUB, 0, mmATC_VMID0_PASID_MAPPING)
		     + vmid);
	*p_pasid = value & ATC_VMID0_PASID_MAPPING__PASID_MASK;

	return !!(value & ATC_VMID0_PASID_MAPPING__VALID_MASK);
}

/*
 * GART
 * VMID 0 is the physical GPU addresses as used by the kernel.
 * VMIDs 1-15 are used for userspace clients and are handled
 * by the amdgpu vm/hsa code.
 */

static void gmc_v10_0_flush_vm_hub(struct amdgpu_device *adev, uint32_t vmid,
				   unsigned int vmhub, uint32_t flush_type)
{
	bool use_semaphore = gmc_v10_0_use_invalidate_semaphore(adev, vmhub);
	struct amdgpu_vmhub *hub = &adev->vmhub[vmhub];
	u32 inv_req = hub->vmhub_funcs->get_invalidate_req(vmid, flush_type);
	u32 tmp;
	/* Use register 17 for GART */
	const unsigned eng = 17;
	unsigned int i;
	unsigned char hub_ip = 0;

	hub_ip = (vmhub == AMDGPU_GFXHUB(0)) ?
		   GC_HWIP : MMHUB_HWIP;

	spin_lock(&adev->gmc.invalidate_lock);
	/*
	 * It may lose gpuvm invalidate acknowldege state across power-gating
	 * off cycle, add semaphore acquire before invalidation and semaphore
	 * release after invalidation to avoid entering power gated state
	 * to WA the Issue
	 */

	/* TODO: It needs to continue working on debugging with semaphore for GFXHUB as well. */
	if (use_semaphore) {
		for (i = 0; i < adev->usec_timeout; i++) {
			/* a read return value of 1 means semaphore acuqire */
			tmp = RREG32_RLC_NO_KIQ(hub->vm_inv_eng0_sem +
					 hub->eng_distance * eng, hub_ip);

			if (tmp & 0x1)
				break;
			udelay(1);
		}

		if (i >= adev->usec_timeout)
			DRM_ERROR("Timeout waiting for sem acquire in VM flush!\n");
	}

	WREG32_RLC_NO_KIQ(hub->vm_inv_eng0_req +
			  hub->eng_distance * eng,
			  inv_req, hub_ip);

	/*
	 * Issue a dummy read to wait for the ACK register to be cleared
	 * to avoid a false ACK due to the new fast GRBM interface.
	 */
	if ((vmhub == AMDGPU_GFXHUB(0)) &&
	    (adev->ip_versions[GC_HWIP][0] < IP_VERSION(10, 3, 0)))
		RREG32_RLC_NO_KIQ(hub->vm_inv_eng0_req +
				  hub->eng_distance * eng, hub_ip);

	/* Wait for ACK with a delay.*/
	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32_RLC_NO_KIQ(hub->vm_inv_eng0_ack +
				  hub->eng_distance * eng, hub_ip);

		tmp &= 1 << vmid;
		if (tmp)
			break;

		udelay(1);
	}

	/* TODO: It needs to continue working on debugging with semaphore for GFXHUB as well. */
	if (use_semaphore)
		/*
		 * add semaphore release after invalidation,
		 * write with 0 means semaphore release
		 */
		WREG32_RLC_NO_KIQ(hub->vm_inv_eng0_sem +
				  hub->eng_distance * eng, 0, hub_ip);

	spin_unlock(&adev->gmc.invalidate_lock);

	if (i < adev->usec_timeout)
		return;

	DRM_ERROR("Timeout waiting for VM flush hub: %d!\n", vmhub);
}

/**
 * gmc_v10_0_flush_gpu_tlb - gart tlb flush callback
 *
 * @adev: amdgpu_device pointer
 * @vmid: vm instance to flush
 * @vmhub: vmhub type
 * @flush_type: the flush type
 *
 * Flush the TLB for the requested page table.
 */
static void gmc_v10_0_flush_gpu_tlb(struct amdgpu_device *adev, uint32_t vmid,
					uint32_t vmhub, uint32_t flush_type)
{
	struct amdgpu_ring *ring = adev->mman.buffer_funcs_ring;
	struct dma_fence *fence;
	struct amdgpu_job *job;

	int r;

	/* flush hdp cache */
	adev->hdp.funcs->flush_hdp(adev, NULL);

	/* For SRIOV run time, driver shouldn't access the register through MMIO
	 * Directly use kiq to do the vm invalidation instead
	 */
	if (adev->gfx.kiq[0].ring.sched.ready && !adev->enable_mes &&
	    (amdgpu_sriov_runtime(adev) || !amdgpu_sriov_vf(adev)) &&
	    down_read_trylock(&adev->reset_domain->sem)) {
		struct amdgpu_vmhub *hub = &adev->vmhub[vmhub];
		const unsigned eng = 17;
		u32 inv_req = hub->vmhub_funcs->get_invalidate_req(vmid, flush_type);
		u32 req = hub->vm_inv_eng0_req + hub->eng_distance * eng;
		u32 ack = hub->vm_inv_eng0_ack + hub->eng_distance * eng;

		amdgpu_virt_kiq_reg_write_reg_wait(adev, req, ack, inv_req,
				1 << vmid);

		up_read(&adev->reset_domain->sem);
		return;
	}

	mutex_lock(&adev->mman.gtt_window_lock);

	if (vmhub == AMDGPU_MMHUB0(0)) {
		gmc_v10_0_flush_vm_hub(adev, vmid, AMDGPU_MMHUB0(0), 0);
		mutex_unlock(&adev->mman.gtt_window_lock);
		return;
	}

	BUG_ON(vmhub != AMDGPU_GFXHUB(0));

	if (!adev->mman.buffer_funcs_enabled ||
	    !adev->ib_pool_ready ||
	    amdgpu_in_reset(adev) ||
	    ring->sched.ready == false) {
		gmc_v10_0_flush_vm_hub(adev, vmid, AMDGPU_GFXHUB(0), 0);
		mutex_unlock(&adev->mman.gtt_window_lock);
		return;
	}

	/* The SDMA on Navi has a bug which can theoretically result in memory
	 * corruption if an invalidation happens at the same time as an VA
	 * translation. Avoid this by doing the invalidation from the SDMA
	 * itself.
	 */
	r = amdgpu_job_alloc_with_ib(ring->adev, &adev->mman.high_pr,
				     AMDGPU_FENCE_OWNER_UNDEFINED,
				     16 * 4, AMDGPU_IB_POOL_IMMEDIATE,
				     &job);
	if (r)
		goto error_alloc;

	job->vm_pd_addr = amdgpu_gmc_pd_addr(adev->gart.bo);
	job->vm_needs_flush = true;
	job->ibs->ptr[job->ibs->length_dw++] = ring->funcs->nop;
	amdgpu_ring_pad_ib(ring, &job->ibs[0]);
	fence = amdgpu_job_submit(job);

	mutex_unlock(&adev->mman.gtt_window_lock);

	dma_fence_wait(fence, false);
	dma_fence_put(fence);

	return;

error_alloc:
	mutex_unlock(&adev->mman.gtt_window_lock);
	DRM_ERROR("Error flushing GPU TLB using the SDMA (%d)!\n", r);
}

/**
 * gmc_v10_0_flush_gpu_tlb_pasid - tlb flush via pasid
 *
 * @adev: amdgpu_device pointer
 * @pasid: pasid to be flush
 * @flush_type: the flush type
 * @all_hub: Used with PACKET3_INVALIDATE_TLBS_ALL_HUB()
 * @inst: is used to select which instance of KIQ to use for the invalidation
 *
 * Flush the TLB for the requested pasid.
 */
static int gmc_v10_0_flush_gpu_tlb_pasid(struct amdgpu_device *adev,
					uint16_t pasid, uint32_t flush_type,
					bool all_hub, uint32_t inst)
{
	int vmid, i;
	signed long r;
	uint32_t seq;
	uint16_t queried_pasid;
	bool ret;
	u32 usec_timeout = amdgpu_sriov_vf(adev) ? SRIOV_USEC_TIMEOUT : adev->usec_timeout;
	struct amdgpu_ring *ring = &adev->gfx.kiq[0].ring;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[0];

	if (amdgpu_emu_mode == 0 && ring->sched.ready) {
		spin_lock(&adev->gfx.kiq[0].ring_lock);
		/* 2 dwords flush + 8 dwords fence */
		amdgpu_ring_alloc(ring, kiq->pmf->invalidate_tlbs_size + 8);
		kiq->pmf->kiq_invalidate_tlbs(ring,
					pasid, flush_type, all_hub);
		r = amdgpu_fence_emit_polling(ring, &seq, MAX_KIQ_REG_WAIT);
		if (r) {
			amdgpu_ring_undo(ring);
			spin_unlock(&adev->gfx.kiq[0].ring_lock);
			return -ETIME;
		}

		amdgpu_ring_commit(ring);
		spin_unlock(&adev->gfx.kiq[0].ring_lock);
		r = amdgpu_fence_wait_polling(ring, seq, usec_timeout);
		if (r < 1) {
			dev_err(adev->dev, "wait for kiq fence error: %ld.\n", r);
			return -ETIME;
		}

		return 0;
	}

	for (vmid = 1; vmid < AMDGPU_NUM_VMID; vmid++) {

		ret = gmc_v10_0_get_atc_vmid_pasid_mapping_info(adev, vmid,
				&queried_pasid);
		if (ret	&& queried_pasid == pasid) {
			if (all_hub) {
				for_each_set_bit(i, adev->vmhubs_mask, AMDGPU_MAX_VMHUBS)
					gmc_v10_0_flush_gpu_tlb(adev, vmid,
							i, flush_type);
			} else {
				gmc_v10_0_flush_gpu_tlb(adev, vmid,
						AMDGPU_GFXHUB(0), flush_type);
			}
			if (!adev->enable_mes)
				break;
		}
	}

	return 0;
}

static uint64_t gmc_v10_0_emit_flush_gpu_tlb(struct amdgpu_ring *ring,
					     unsigned vmid, uint64_t pd_addr)
{
	bool use_semaphore = gmc_v10_0_use_invalidate_semaphore(ring->adev, ring->vm_hub);
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->vm_hub];
	uint32_t req = hub->vmhub_funcs->get_invalidate_req(vmid, 0);
	unsigned eng = ring->vm_inv_eng;

	/*
	 * It may lose gpuvm invalidate acknowldege state across power-gating
	 * off cycle, add semaphore acquire before invalidation and semaphore
	 * release after invalidation to avoid entering power gated state
	 * to WA the Issue
	 */

	/* TODO: It needs to continue working on debugging with semaphore for GFXHUB as well. */
	if (use_semaphore)
		/* a read return value of 1 means semaphore acuqire */
		amdgpu_ring_emit_reg_wait(ring,
					  hub->vm_inv_eng0_sem +
					  hub->eng_distance * eng, 0x1, 0x1);

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

	/* TODO: It needs to continue working on debugging with semaphore for GFXHUB as well. */
	if (use_semaphore)
		/*
		 * add semaphore release after invalidation,
		 * write with 0 means semaphore release
		 */
		amdgpu_ring_emit_wreg(ring, hub->vm_inv_eng0_sem +
				      hub->eng_distance * eng, 0);

	return pd_addr;
}

static void gmc_v10_0_emit_pasid_mapping(struct amdgpu_ring *ring, unsigned vmid,
					 unsigned pasid)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t reg;

	/* MES fw manages IH_VMID_x_LUT updating */
	if (ring->is_mes_queue)
		return;

	if (ring->vm_hub == AMDGPU_GFXHUB(0))
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_VMID_0_LUT) + vmid;
	else
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_VMID_0_LUT_MM) + vmid;

	amdgpu_ring_emit_wreg(ring, reg, pasid);
}

/*
 * PTE format on NAVI 10:
 * 63:59 reserved
 * 58 reserved and for sienna_cichlid is used for MALL noalloc
 * 57 reserved
 * 56 F
 * 55 L
 * 54 reserved
 * 53:52 SW
 * 51 T
 * 50:48 mtype
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
 * PDE format on NAVI 10:
 * 63:59 block fragment size
 * 58:55 reserved
 * 54 P
 * 53:48 reserved
 * 47:6 physical base address of PD or PTE
 * 5:3 reserved
 * 2 C
 * 1 system
 * 0 valid
 */

static uint64_t gmc_v10_0_map_mtype(struct amdgpu_device *adev, uint32_t flags)
{
	switch (flags) {
	case AMDGPU_VM_MTYPE_DEFAULT:
		return AMDGPU_PTE_MTYPE_NV10(MTYPE_NC);
	case AMDGPU_VM_MTYPE_NC:
		return AMDGPU_PTE_MTYPE_NV10(MTYPE_NC);
	case AMDGPU_VM_MTYPE_WC:
		return AMDGPU_PTE_MTYPE_NV10(MTYPE_WC);
	case AMDGPU_VM_MTYPE_CC:
		return AMDGPU_PTE_MTYPE_NV10(MTYPE_CC);
	case AMDGPU_VM_MTYPE_UC:
		return AMDGPU_PTE_MTYPE_NV10(MTYPE_UC);
	default:
		return AMDGPU_PTE_MTYPE_NV10(MTYPE_NC);
	}
}

static void gmc_v10_0_get_vm_pde(struct amdgpu_device *adev, int level,
				 uint64_t *addr, uint64_t *flags)
{
	if (!(*flags & AMDGPU_PDE_PTE) && !(*flags & AMDGPU_PTE_SYSTEM))
		*addr = amdgpu_gmc_vram_mc2pa(adev, *addr);
	BUG_ON(*addr & 0xFFFF00000000003FULL);

	if (!adev->gmc.translate_further)
		return;

	if (level == AMDGPU_VM_PDB1) {
		/* Set the block fragment size */
		if (!(*flags & AMDGPU_PDE_PTE))
			*flags |= AMDGPU_PDE_BFS(0x9);

	} else if (level == AMDGPU_VM_PDB0) {
		if (*flags & AMDGPU_PDE_PTE)
			*flags &= ~AMDGPU_PDE_PTE;
		else
			*flags |= AMDGPU_PTE_TF;
	}
}

static void gmc_v10_0_get_vm_pte(struct amdgpu_device *adev,
				 struct amdgpu_bo_va_mapping *mapping,
				 uint64_t *flags)
{
	struct amdgpu_bo *bo = mapping->bo_va->base.bo;

	*flags &= ~AMDGPU_PTE_EXECUTABLE;
	*flags |= mapping->flags & AMDGPU_PTE_EXECUTABLE;

	*flags &= ~AMDGPU_PTE_MTYPE_NV10_MASK;
	*flags |= (mapping->flags & AMDGPU_PTE_MTYPE_NV10_MASK);

	*flags &= ~AMDGPU_PTE_NOALLOC;
	*flags |= (mapping->flags & AMDGPU_PTE_NOALLOC);

	if (mapping->flags & AMDGPU_PTE_PRT) {
		*flags |= AMDGPU_PTE_PRT;
		*flags |= AMDGPU_PTE_SNOOPED;
		*flags |= AMDGPU_PTE_LOG;
		*flags |= AMDGPU_PTE_SYSTEM;
		*flags &= ~AMDGPU_PTE_VALID;
	}

	if (bo && bo->flags & (AMDGPU_GEM_CREATE_COHERENT |
			       AMDGPU_GEM_CREATE_UNCACHED))
		*flags = (*flags & ~AMDGPU_PTE_MTYPE_NV10_MASK) |
			 AMDGPU_PTE_MTYPE_NV10(MTYPE_UC);
}

static unsigned gmc_v10_0_get_vbios_fb_size(struct amdgpu_device *adev)
{
	u32 d1vga_control = RREG32_SOC15(DCE, 0, mmD1VGA_CONTROL);
	unsigned size;

	if (REG_GET_FIELD(d1vga_control, D1VGA_CONTROL, D1VGA_MODE_ENABLE)) {
		size = AMDGPU_VBIOS_VGA_ALLOCATION;
	} else {
		u32 viewport;
		u32 pitch;

		viewport = RREG32_SOC15(DCE, 0, mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION);
		pitch = RREG32_SOC15(DCE, 0, mmHUBPREQ0_DCSURF_SURFACE_PITCH);
		size = (REG_GET_FIELD(viewport,
					HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION, PRI_VIEWPORT_HEIGHT) *
				REG_GET_FIELD(pitch, HUBPREQ0_DCSURF_SURFACE_PITCH, PITCH) *
				4);
	}

	return size;
}

static const struct amdgpu_gmc_funcs gmc_v10_0_gmc_funcs = {
	.flush_gpu_tlb = gmc_v10_0_flush_gpu_tlb,
	.flush_gpu_tlb_pasid = gmc_v10_0_flush_gpu_tlb_pasid,
	.emit_flush_gpu_tlb = gmc_v10_0_emit_flush_gpu_tlb,
	.emit_pasid_mapping = gmc_v10_0_emit_pasid_mapping,
	.map_mtype = gmc_v10_0_map_mtype,
	.get_vm_pde = gmc_v10_0_get_vm_pde,
	.get_vm_pte = gmc_v10_0_get_vm_pte,
	.get_vbios_fb_size = gmc_v10_0_get_vbios_fb_size,
};

static void gmc_v10_0_set_gmc_funcs(struct amdgpu_device *adev)
{
	if (adev->gmc.gmc_funcs == NULL)
		adev->gmc.gmc_funcs = &gmc_v10_0_gmc_funcs;
}

static void gmc_v10_0_set_umc_funcs(struct amdgpu_device *adev)
{
	switch (adev->ip_versions[UMC_HWIP][0]) {
	case IP_VERSION(8, 7, 0):
		adev->umc.max_ras_err_cnt_per_query = UMC_V8_7_TOTAL_CHANNEL_NUM;
		adev->umc.channel_inst_num = UMC_V8_7_CHANNEL_INSTANCE_NUM;
		adev->umc.umc_inst_num = UMC_V8_7_UMC_INSTANCE_NUM;
		adev->umc.channel_offs = UMC_V8_7_PER_CHANNEL_OFFSET_SIENNA;
		adev->umc.retire_unit = 1;
		adev->umc.channel_idx_tbl = &umc_v8_7_channel_idx_tbl[0][0];
		adev->umc.ras = &umc_v8_7_ras;
		break;
	default:
		break;
	}
}

static void gmc_v10_0_set_mmhub_funcs(struct amdgpu_device *adev)
{
	switch (adev->ip_versions[MMHUB_HWIP][0]) {
	case IP_VERSION(2, 3, 0):
	case IP_VERSION(2, 4, 0):
	case IP_VERSION(2, 4, 1):
		adev->mmhub.funcs = &mmhub_v2_3_funcs;
		break;
	default:
		adev->mmhub.funcs = &mmhub_v2_0_funcs;
		break;
	}
}

static void gmc_v10_0_set_gfxhub_funcs(struct amdgpu_device *adev)
{
	switch (adev->ip_versions[GC_HWIP][0]) {
	case IP_VERSION(10, 3, 0):
	case IP_VERSION(10, 3, 2):
	case IP_VERSION(10, 3, 1):
	case IP_VERSION(10, 3, 4):
	case IP_VERSION(10, 3, 5):
	case IP_VERSION(10, 3, 6):
	case IP_VERSION(10, 3, 3):
	case IP_VERSION(10, 3, 7):
		adev->gfxhub.funcs = &gfxhub_v2_1_funcs;
		break;
	default:
		adev->gfxhub.funcs = &gfxhub_v2_0_funcs;
		break;
	}
}


static int gmc_v10_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v10_0_set_mmhub_funcs(adev);
	gmc_v10_0_set_gfxhub_funcs(adev);
	gmc_v10_0_set_gmc_funcs(adev);
	gmc_v10_0_set_irq_funcs(adev);
	gmc_v10_0_set_umc_funcs(adev);

	adev->gmc.shared_aperture_start = 0x2000000000000000ULL;
	adev->gmc.shared_aperture_end =
		adev->gmc.shared_aperture_start + (4ULL << 30) - 1;
	adev->gmc.private_aperture_start = 0x1000000000000000ULL;
	adev->gmc.private_aperture_end =
		adev->gmc.private_aperture_start + (4ULL << 30) - 1;

	return 0;
}

static int gmc_v10_0_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = amdgpu_gmc_allocate_vm_inv_eng(adev);
	if (r)
		return r;

	r = amdgpu_gmc_ras_late_init(adev);
	if (r)
		return r;

	return amdgpu_irq_get(adev, &adev->gmc.vm_fault, 0);
}

static void gmc_v10_0_vram_gtt_location(struct amdgpu_device *adev,
					struct amdgpu_gmc *mc)
{
	u64 base = 0;

	base = adev->gfxhub.funcs->get_fb_location(adev);

	/* add the xgmi offset of the physical node */
	base += adev->gmc.xgmi.physical_node_id * adev->gmc.xgmi.node_segment_size;

	amdgpu_gmc_vram_location(adev, &adev->gmc, base);
	amdgpu_gmc_gart_location(adev, mc);
	amdgpu_gmc_agp_location(adev, mc);

	/* base offset of vram pages */
	adev->vm_manager.vram_base_offset = adev->gfxhub.funcs->get_mc_fb_offset(adev);

	/* add the xgmi offset of the physical node */
	adev->vm_manager.vram_base_offset +=
		adev->gmc.xgmi.physical_node_id * adev->gmc.xgmi.node_segment_size;
}

/**
 * gmc_v10_0_mc_init - initialize the memory controller driver params
 *
 * @adev: amdgpu_device pointer
 *
 * Look up the amount of vram, vram width, and decide how to place
 * vram and gart within the GPU's physical address space.
 * Returns 0 for success.
 */
static int gmc_v10_0_mc_init(struct amdgpu_device *adev)
{
	int r;

	/* size in MB on si */
	adev->gmc.mc_vram_size =
		adev->nbio.funcs->get_memsize(adev) * 1024ULL * 1024ULL;
	adev->gmc.real_vram_size = adev->gmc.mc_vram_size;

	if (!(adev->flags & AMD_IS_APU)) {
		r = amdgpu_device_resize_fb_bar(adev);
		if (r)
			return r;
	}
	adev->gmc.aper_base = pci_resource_start(adev->pdev, 0);
	adev->gmc.aper_size = pci_resource_len(adev->pdev, 0);

#ifdef CONFIG_X86_64
	if ((adev->flags & AMD_IS_APU) && !amdgpu_passthrough(adev)) {
		adev->gmc.aper_base = adev->gfxhub.funcs->get_mc_fb_offset(adev);
		adev->gmc.aper_size = adev->gmc.real_vram_size;
	}
#endif

	adev->gmc.visible_vram_size = adev->gmc.aper_size;

	/* set the gart size */
	if (amdgpu_gart_size == -1) {
		switch (adev->ip_versions[GC_HWIP][0]) {
		default:
			adev->gmc.gart_size = 512ULL << 20;
			break;
		case IP_VERSION(10, 3, 1):   /* DCE SG support */
		case IP_VERSION(10, 3, 3):   /* DCE SG support */
		case IP_VERSION(10, 3, 6):   /* DCE SG support */
		case IP_VERSION(10, 3, 7):   /* DCE SG support */
			adev->gmc.gart_size = 1024ULL << 20;
			break;
		}
	} else {
		adev->gmc.gart_size = (u64)amdgpu_gart_size << 20;
	}

	gmc_v10_0_vram_gtt_location(adev, &adev->gmc);

	return 0;
}

static int gmc_v10_0_gart_init(struct amdgpu_device *adev)
{
	int r;

	if (adev->gart.bo) {
		WARN(1, "NAVI10 PCIE GART already initialized\n");
		return 0;
	}

	/* Initialize common gart structure */
	r = amdgpu_gart_init(adev);
	if (r)
		return r;

	adev->gart.table_size = adev->gart.num_gpu_pages * 8;
	adev->gart.gart_pte_flags = AMDGPU_PTE_MTYPE_NV10(MTYPE_UC) |
				 AMDGPU_PTE_EXECUTABLE;

	return amdgpu_gart_table_vram_alloc(adev);
}

static int gmc_v10_0_sw_init(void *handle)
{
	int r, vram_width = 0, vram_type = 0, vram_vendor = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->gfxhub.funcs->init(adev);

	adev->mmhub.funcs->init(adev);

	spin_lock_init(&adev->gmc.invalidate_lock);

	if ((adev->flags & AMD_IS_APU) && amdgpu_emu_mode == 1) {
		adev->gmc.vram_type = AMDGPU_VRAM_TYPE_DDR4;
		adev->gmc.vram_width = 64;
	} else if (amdgpu_emu_mode == 1) {
		adev->gmc.vram_type = AMDGPU_VRAM_TYPE_GDDR6;
		adev->gmc.vram_width = 1 * 128; /* numchan * chansize */
	} else {
		r = amdgpu_atomfirmware_get_vram_info(adev,
				&vram_width, &vram_type, &vram_vendor);
		adev->gmc.vram_width = vram_width;

		adev->gmc.vram_type = vram_type;
		adev->gmc.vram_vendor = vram_vendor;
	}

	switch (adev->ip_versions[GC_HWIP][0]) {
	case IP_VERSION(10, 3, 0):
		adev->gmc.mall_size = 128 * 1024 * 1024;
		break;
	case IP_VERSION(10, 3, 2):
		adev->gmc.mall_size = 96 * 1024 * 1024;
		break;
	case IP_VERSION(10, 3, 4):
		adev->gmc.mall_size = 32 * 1024 * 1024;
		break;
	case IP_VERSION(10, 3, 5):
		adev->gmc.mall_size = 16 * 1024 * 1024;
		break;
	default:
		adev->gmc.mall_size = 0;
		break;
	}

	switch (adev->ip_versions[GC_HWIP][0]) {
	case IP_VERSION(10, 1, 10):
	case IP_VERSION(10, 1, 1):
	case IP_VERSION(10, 1, 2):
	case IP_VERSION(10, 1, 3):
	case IP_VERSION(10, 1, 4):
	case IP_VERSION(10, 3, 0):
	case IP_VERSION(10, 3, 2):
	case IP_VERSION(10, 3, 1):
	case IP_VERSION(10, 3, 4):
	case IP_VERSION(10, 3, 5):
	case IP_VERSION(10, 3, 6):
	case IP_VERSION(10, 3, 3):
	case IP_VERSION(10, 3, 7):
		set_bit(AMDGPU_GFXHUB(0), adev->vmhubs_mask);
		set_bit(AMDGPU_MMHUB0(0), adev->vmhubs_mask);
		/*
		 * To fulfill 4-level page support,
		 * vm size is 256TB (48bit), maximum size of Navi10/Navi14/Navi12,
		 * block size 512 (9bit)
		 */
		amdgpu_vm_adjust_size(adev, 256 * 1024, 9, 3, 48);
		break;
	default:
		break;
	}

	/* This interrupt is VMC page fault.*/
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VMC,
			      VMC_1_0__SRCID__VM_FAULT,
			      &adev->gmc.vm_fault);

	if (r)
		return r;

	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_UTCL2,
			      UTCL2_1_0__SRCID__FAULT,
			      &adev->gmc.vm_fault);
	if (r)
		return r;

	if (!amdgpu_sriov_vf(adev)) {
		/* interrupt sent to DF. */
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DF, 0,
				      &adev->gmc.ecc_irq);
		if (r)
			return r;
	}

	/*
	 * Set the internal MC address mask This is the max address of the GPU's
	 * internal address space.
	 */
	adev->gmc.mc_mask = 0xffffffffffffULL; /* 48 bit MC */

	r = dma_set_mask_and_coherent(adev->dev, DMA_BIT_MASK(44));
	if (r) {
		printk(KERN_WARNING "amdgpu: No suitable DMA available.\n");
		return r;
	}

	adev->need_swiotlb = drm_need_swiotlb(44);

	r = gmc_v10_0_mc_init(adev);
	if (r)
		return r;

	amdgpu_gmc_get_vbios_allocations(adev);

	/* Memory manager */
	r = amdgpu_bo_init(adev);
	if (r)
		return r;

	r = gmc_v10_0_gart_init(adev);
	if (r)
		return r;

	/*
	 * number of VMs
	 * VMID 0 is reserved for System
	 * amdgpu graphics/compute will use VMIDs 1-7
	 * amdkfd will use VMIDs 8-15
	 */
	adev->vm_manager.first_kfd_vmid = 8;

	amdgpu_vm_manager_init(adev);

	r = amdgpu_gmc_ras_sw_init(adev);
	if (r)
		return r;

	return 0;
}

/**
 * gmc_v10_0_gart_fini - vm fini callback
 *
 * @adev: amdgpu_device pointer
 *
 * Tears down the driver GART/VM setup (CIK).
 */
static void gmc_v10_0_gart_fini(struct amdgpu_device *adev)
{
	amdgpu_gart_table_vram_free(adev);
}

static int gmc_v10_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_vm_manager_fini(adev);
	gmc_v10_0_gart_fini(adev);
	amdgpu_gem_force_release(adev);
	amdgpu_bo_fini(adev);

	return 0;
}

static void gmc_v10_0_init_golden_registers(struct amdgpu_device *adev)
{
}

/**
 * gmc_v10_0_gart_enable - gart enable
 *
 * @adev: amdgpu_device pointer
 */
static int gmc_v10_0_gart_enable(struct amdgpu_device *adev)
{
	int r;
	bool value;

	if (adev->gart.bo == NULL) {
		dev_err(adev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}

	amdgpu_gtt_mgr_recover(&adev->mman.gtt_mgr);

	if (!adev->in_s0ix) {
		r = adev->gfxhub.funcs->gart_enable(adev);
		if (r)
			return r;
	}

	r = adev->mmhub.funcs->gart_enable(adev);
	if (r)
		return r;

	adev->hdp.funcs->init_registers(adev);

	/* Flush HDP after it is initialized */
	adev->hdp.funcs->flush_hdp(adev, NULL);

	value = (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_ALWAYS) ?
		false : true;

	if (!adev->in_s0ix)
		adev->gfxhub.funcs->set_fault_enable_default(adev, value);
	adev->mmhub.funcs->set_fault_enable_default(adev, value);
	gmc_v10_0_flush_gpu_tlb(adev, 0, AMDGPU_MMHUB0(0), 0);
	if (!adev->in_s0ix)
		gmc_v10_0_flush_gpu_tlb(adev, 0, AMDGPU_GFXHUB(0), 0);

	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(adev->gmc.gart_size >> 20),
		 (unsigned long long)amdgpu_bo_gpu_offset(adev->gart.bo));

	return 0;
}

static int gmc_v10_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* The sequence of these two function calls matters.*/
	gmc_v10_0_init_golden_registers(adev);

	/*
	 * harvestable groups in gc_utcl2 need to be programmed before any GFX block
	 * register setup within GMC, or else system hang when harvesting SA.
	 */
	if (!adev->in_s0ix && adev->gfxhub.funcs && adev->gfxhub.funcs->utcl2_harvest)
		adev->gfxhub.funcs->utcl2_harvest(adev);

	r = gmc_v10_0_gart_enable(adev);
	if (r)
		return r;

	if (amdgpu_emu_mode == 1) {
		r = amdgpu_gmc_vram_checking(adev);
		if (r)
			return r;
	}

	if (adev->umc.funcs && adev->umc.funcs->init_registers)
		adev->umc.funcs->init_registers(adev);

	return 0;
}

/**
 * gmc_v10_0_gart_disable - gart disable
 *
 * @adev: amdgpu_device pointer
 *
 * This disables all VM page table.
 */
static void gmc_v10_0_gart_disable(struct amdgpu_device *adev)
{
	if (!adev->in_s0ix)
		adev->gfxhub.funcs->gart_disable(adev);
	adev->mmhub.funcs->gart_disable(adev);
}

static int gmc_v10_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v10_0_gart_disable(adev);

	if (amdgpu_sriov_vf(adev)) {
		/* full access mode, so don't touch any GMC register */
		DRM_DEBUG("For SRIOV client, shouldn't do anything.\n");
		return 0;
	}

	amdgpu_irq_put(adev, &adev->gmc.vm_fault, 0);

	return 0;
}

static int gmc_v10_0_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v10_0_hw_fini(adev);

	return 0;
}

static int gmc_v10_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = gmc_v10_0_hw_init(adev);
	if (r)
		return r;

	amdgpu_vmid_reset_all(adev);

	return 0;
}

static bool gmc_v10_0_is_idle(void *handle)
{
	/* MC is always ready in GMC v10.*/
	return true;
}

static int gmc_v10_0_wait_for_idle(void *handle)
{
	/* There is no need to wait for MC idle in GMC v10.*/
	return 0;
}

static int gmc_v10_0_soft_reset(void *handle)
{
	return 0;
}

static int gmc_v10_0_set_clockgating_state(void *handle,
					   enum amd_clockgating_state state)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/*
	 * The issue mmhub can't disconnect from DF with MMHUB clock gating being disabled
	 * is a new problem observed at DF 3.0.3, however with the same suspend sequence not
	 * seen any issue on the DF 3.0.2 series platform.
	 */
	if (adev->in_s0ix && adev->ip_versions[DF_HWIP][0] > IP_VERSION(3, 0, 2)) {
		dev_dbg(adev->dev, "keep mmhub clock gating being enabled for s0ix\n");
		return 0;
	}

	r = adev->mmhub.funcs->set_clockgating(adev, state);
	if (r)
		return r;

	if (adev->ip_versions[ATHUB_HWIP][0] >= IP_VERSION(2, 1, 0))
		return athub_v2_1_set_clockgating(adev, state);
	else
		return athub_v2_0_set_clockgating(adev, state);
}

static void gmc_v10_0_get_clockgating_state(void *handle, u64 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->ip_versions[GC_HWIP][0] == IP_VERSION(10, 1, 3) ||
	    adev->ip_versions[GC_HWIP][0] == IP_VERSION(10, 1, 4))
		return;

	adev->mmhub.funcs->get_clockgating(adev, flags);

	if (adev->ip_versions[ATHUB_HWIP][0] >= IP_VERSION(2, 1, 0))
		athub_v2_1_get_clockgating(adev, flags);
	else
		athub_v2_0_get_clockgating(adev, flags);
}

static int gmc_v10_0_set_powergating_state(void *handle,
					   enum amd_powergating_state state)
{
	return 0;
}

const struct amd_ip_funcs gmc_v10_0_ip_funcs = {
	.name = "gmc_v10_0",
	.early_init = gmc_v10_0_early_init,
	.late_init = gmc_v10_0_late_init,
	.sw_init = gmc_v10_0_sw_init,
	.sw_fini = gmc_v10_0_sw_fini,
	.hw_init = gmc_v10_0_hw_init,
	.hw_fini = gmc_v10_0_hw_fini,
	.suspend = gmc_v10_0_suspend,
	.resume = gmc_v10_0_resume,
	.is_idle = gmc_v10_0_is_idle,
	.wait_for_idle = gmc_v10_0_wait_for_idle,
	.soft_reset = gmc_v10_0_soft_reset,
	.set_clockgating_state = gmc_v10_0_set_clockgating_state,
	.set_powergating_state = gmc_v10_0_set_powergating_state,
	.get_clockgating_state = gmc_v10_0_get_clockgating_state,
};

const struct amdgpu_ip_block_version gmc_v10_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_GMC,
	.major = 10,
	.minor = 0,
	.rev = 0,
	.funcs = &gmc_v10_0_ip_funcs,
};
