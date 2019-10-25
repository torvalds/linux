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
#include "amdgpu.h"
#include "amdgpu_atomfirmware.h"
#include "gmc_v10_0.h"

#include "hdp/hdp_5_0_0_offset.h"
#include "hdp/hdp_5_0_0_sh_mask.h"
#include "gc/gc_10_1_0_sh_mask.h"
#include "mmhub/mmhub_2_0_0_sh_mask.h"
#include "dcn/dcn_2_0_0_offset.h"
#include "dcn/dcn_2_0_0_sh_mask.h"
#include "oss/osssys_5_0_0_offset.h"
#include "ivsrcid/vmc/irqsrcs_vmc_1_0.h"
#include "navi10_enum.h"

#include "soc15.h"
#include "soc15_common.h"

#include "nbio_v2_3.h"

#include "gfxhub_v2_0.h"
#include "mmhub_v2_0.h"
#include "athub_v2_0.h"
/* XXX Move this macro to navi10 header file, which is like vid.h for VI.*/
#define AMDGPU_NUM_OF_VMIDS			8

#if 0
static const struct soc15_reg_golden golden_settings_navi10_hdp[] =
{
	/* TODO add golden setting for hdp */
};
#endif

static int
gmc_v10_0_vm_fault_interrupt_state(struct amdgpu_device *adev,
				   struct amdgpu_irq_src *src, unsigned type,
				   enum amdgpu_interrupt_state state)
{
	struct amdgpu_vmhub *hub;
	u32 tmp, reg, bits[AMDGPU_MAX_VMHUBS], i;

	bits[AMDGPU_GFXHUB_0] = GCVM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		GCVM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		GCVM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		GCVM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		GCVM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		GCVM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		GCVM_CONTEXT1_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK;

	bits[AMDGPU_MMHUB_0] = MMVM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		MMVM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		MMVM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		MMVM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		MMVM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		MMVM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		MMVM_CONTEXT1_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		/* MM HUB */
		hub = &adev->vmhub[AMDGPU_MMHUB_0];
		for (i = 0; i < 16; i++) {
			reg = hub->vm_context0_cntl + i;
			tmp = RREG32(reg);
			tmp &= ~bits[AMDGPU_MMHUB_0];
			WREG32(reg, tmp);
		}

		/* GFX HUB */
		hub = &adev->vmhub[AMDGPU_GFXHUB_0];
		for (i = 0; i < 16; i++) {
			reg = hub->vm_context0_cntl + i;
			tmp = RREG32(reg);
			tmp &= ~bits[AMDGPU_GFXHUB_0];
			WREG32(reg, tmp);
		}
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		/* MM HUB */
		hub = &adev->vmhub[AMDGPU_MMHUB_0];
		for (i = 0; i < 16; i++) {
			reg = hub->vm_context0_cntl + i;
			tmp = RREG32(reg);
			tmp |= bits[AMDGPU_MMHUB_0];
			WREG32(reg, tmp);
		}

		/* GFX HUB */
		hub = &adev->vmhub[AMDGPU_GFXHUB_0];
		for (i = 0; i < 16; i++) {
			reg = hub->vm_context0_cntl + i;
			tmp = RREG32(reg);
			tmp |= bits[AMDGPU_GFXHUB_0];
			WREG32(reg, tmp);
		}
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
	struct amdgpu_vmhub *hub = &adev->vmhub[entry->vmid_src];
	uint32_t status = 0;
	u64 addr;

	addr = (u64)entry->src_data[0] << 12;
	addr |= ((u64)entry->src_data[1] & 0xf) << 44;

	if (!amdgpu_sriov_vf(adev)) {
		/*
		 * Issue a dummy read to wait for the status register to
		 * be updated to avoid reading an incorrect value due to
		 * the new fast GRBM interface.
		 */
		if (entry->vmid_src == AMDGPU_GFXHUB_0)
			RREG32(hub->vm_l2_pro_fault_status);

		status = RREG32(hub->vm_l2_pro_fault_status);
		WREG32_P(hub->vm_l2_pro_fault_cntl, 1, ~1);
	}

	if (printk_ratelimit()) {
		struct amdgpu_task_info task_info;

		memset(&task_info, 0, sizeof(struct amdgpu_task_info));
		amdgpu_vm_get_task_info(adev, entry->pasid, &task_info);

		dev_err(adev->dev,
			"[%s] page fault (src_id:%u ring:%u vmid:%u pasid:%u, "
			"for process %s pid %d thread %s pid %d)\n",
			entry->vmid_src ? "mmhub" : "gfxhub",
			entry->src_id, entry->ring_id, entry->vmid,
			entry->pasid, task_info.process_name, task_info.tgid,
			task_info.task_name, task_info.pid);
		dev_err(adev->dev, "  in page starting at address 0x%016llx from client %d\n",
			addr, entry->client_id);
		if (!amdgpu_sriov_vf(adev)) {
			dev_err(adev->dev,
				"GCVM_L2_PROTECTION_FAULT_STATUS:0x%08X\n",
				status);
			dev_err(adev->dev, "\t MORE_FAULTS: 0x%lx\n",
				REG_GET_FIELD(status,
				GCVM_L2_PROTECTION_FAULT_STATUS, MORE_FAULTS));
			dev_err(adev->dev, "\t WALKER_ERROR: 0x%lx\n",
				REG_GET_FIELD(status,
				GCVM_L2_PROTECTION_FAULT_STATUS, WALKER_ERROR));
			dev_err(adev->dev, "\t PERMISSION_FAULTS: 0x%lx\n",
				REG_GET_FIELD(status,
				GCVM_L2_PROTECTION_FAULT_STATUS, PERMISSION_FAULTS));
			dev_err(adev->dev, "\t MAPPING_ERROR: 0x%lx\n",
				REG_GET_FIELD(status,
				GCVM_L2_PROTECTION_FAULT_STATUS, MAPPING_ERROR));
			dev_err(adev->dev, "\t RW: 0x%lx\n",
				REG_GET_FIELD(status,
				GCVM_L2_PROTECTION_FAULT_STATUS, RW));
		}
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs gmc_v10_0_irq_funcs = {
	.set = gmc_v10_0_vm_fault_interrupt_state,
	.process = gmc_v10_0_process_interrupt,
};

static void gmc_v10_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gmc.vm_fault.num_types = 1;
	adev->gmc.vm_fault.funcs = &gmc_v10_0_irq_funcs;
}

static uint32_t gmc_v10_0_get_invalidate_req(unsigned int vmid,
					     uint32_t flush_type)
{
	u32 req = 0;

	/* invalidate using legacy mode on vmid*/
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ,
			    PER_VMID_INVALIDATE_REQ, 1 << vmid);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ, FLUSH_TYPE, flush_type);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PTES, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE0, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE1, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE2, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ, INVALIDATE_L1_PTES, 1);
	req = REG_SET_FIELD(req, GCVM_INVALIDATE_ENG0_REQ,
			    CLEAR_PROTECTION_FAULT_STATUS_ADDR,	0);

	return req;
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
	struct amdgpu_vmhub *hub = &adev->vmhub[vmhub];
	u32 tmp = gmc_v10_0_get_invalidate_req(vmid, flush_type);
	/* Use register 17 for GART */
	const unsigned eng = 17;
	unsigned int i;

	WREG32_NO_KIQ(hub->vm_inv_eng0_req + eng, tmp);

	/*
	 * Issue a dummy read to wait for the ACK register to be cleared
	 * to avoid a false ACK due to the new fast GRBM interface.
	 */
	if (vmhub == AMDGPU_GFXHUB_0)
		RREG32_NO_KIQ(hub->vm_inv_eng0_req + eng);

	/* Wait for ACK with a delay.*/
	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32_NO_KIQ(hub->vm_inv_eng0_ack + eng);
		tmp &= 1 << vmid;
		if (tmp)
			break;

		udelay(1);
	}

	if (i < adev->usec_timeout)
		return;

	DRM_ERROR("Timeout waiting for VM flush ACK!\n");
}

/**
 * gmc_v10_0_flush_gpu_tlb - gart tlb flush callback
 *
 * @adev: amdgpu_device pointer
 * @vmid: vm instance to flush
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
	adev->nbio.funcs->hdp_flush(adev, NULL);

	mutex_lock(&adev->mman.gtt_window_lock);

	if (vmhub == AMDGPU_MMHUB_0) {
		gmc_v10_0_flush_vm_hub(adev, vmid, AMDGPU_MMHUB_0, 0);
		mutex_unlock(&adev->mman.gtt_window_lock);
		return;
	}

	BUG_ON(vmhub != AMDGPU_GFXHUB_0);

	if (!adev->mman.buffer_funcs_enabled ||
	    !adev->ib_pool_ready ||
	    adev->in_gpu_reset) {
		gmc_v10_0_flush_vm_hub(adev, vmid, AMDGPU_GFXHUB_0, 0);
		mutex_unlock(&adev->mman.gtt_window_lock);
		return;
	}

	/* The SDMA on Navi has a bug which can theoretically result in memory
	 * corruption if an invalidation happens at the same time as an VA
	 * translation. Avoid this by doing the invalidation from the SDMA
	 * itself.
	 */
	r = amdgpu_job_alloc_with_ib(adev, 16 * 4, &job);
	if (r)
		goto error_alloc;

	job->vm_pd_addr = amdgpu_gmc_pd_addr(adev->gart.bo);
	job->vm_needs_flush = true;
	amdgpu_ring_pad_ib(ring, &job->ibs[0]);
	r = amdgpu_job_submit(job, &adev->mman.entity,
			      AMDGPU_FENCE_OWNER_UNDEFINED, &fence);
	if (r)
		goto error_submit;

	mutex_unlock(&adev->mman.gtt_window_lock);

	dma_fence_wait(fence, false);
	dma_fence_put(fence);

	return;

error_submit:
	amdgpu_job_free(job);

error_alloc:
	mutex_unlock(&adev->mman.gtt_window_lock);
	DRM_ERROR("Error flushing GPU TLB using the SDMA (%d)!\n", r);
}

static uint64_t gmc_v10_0_emit_flush_gpu_tlb(struct amdgpu_ring *ring,
					     unsigned vmid, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->funcs->vmhub];
	uint32_t req = gmc_v10_0_get_invalidate_req(vmid, 0);
	unsigned eng = ring->vm_inv_eng;

	amdgpu_ring_emit_wreg(ring, hub->ctx0_ptb_addr_lo32 + (2 * vmid),
			      lower_32_bits(pd_addr));

	amdgpu_ring_emit_wreg(ring, hub->ctx0_ptb_addr_hi32 + (2 * vmid),
			      upper_32_bits(pd_addr));

	amdgpu_ring_emit_wreg(ring, hub->vm_inv_eng0_req + eng, req);

	/* wait for the invalidate to complete */
	amdgpu_ring_emit_reg_wait(ring, hub->vm_inv_eng0_ack + eng,
				  1 << vmid, 1 << vmid);

	return pd_addr;
}

static void gmc_v10_0_emit_pasid_mapping(struct amdgpu_ring *ring, unsigned vmid,
					 unsigned pasid)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t reg;

	if (ring->funcs->vmhub == AMDGPU_GFXHUB_0)
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_VMID_0_LUT) + vmid;
	else
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_VMID_0_LUT_MM) + vmid;

	amdgpu_ring_emit_wreg(ring, reg, pasid);
}

/*
 * PTE format on NAVI 10:
 * 63:59 reserved
 * 58:57 reserved
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
		*addr = adev->vm_manager.vram_base_offset + *addr -
			adev->gmc.vram_start;
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
	*flags &= ~AMDGPU_PTE_EXECUTABLE;
	*flags |= mapping->flags & AMDGPU_PTE_EXECUTABLE;

	*flags &= ~AMDGPU_PTE_MTYPE_NV10_MASK;
	*flags |= (mapping->flags & AMDGPU_PTE_MTYPE_NV10_MASK);

	if (mapping->flags & AMDGPU_PTE_PRT) {
		*flags |= AMDGPU_PTE_PRT;
		*flags |= AMDGPU_PTE_SNOOPED;
		*flags |= AMDGPU_PTE_LOG;
		*flags |= AMDGPU_PTE_SYSTEM;
		*flags &= ~AMDGPU_PTE_VALID;
	}
}

static const struct amdgpu_gmc_funcs gmc_v10_0_gmc_funcs = {
	.flush_gpu_tlb = gmc_v10_0_flush_gpu_tlb,
	.emit_flush_gpu_tlb = gmc_v10_0_emit_flush_gpu_tlb,
	.emit_pasid_mapping = gmc_v10_0_emit_pasid_mapping,
	.map_mtype = gmc_v10_0_map_mtype,
	.get_vm_pde = gmc_v10_0_get_vm_pde,
	.get_vm_pte = gmc_v10_0_get_vm_pte
};

static void gmc_v10_0_set_gmc_funcs(struct amdgpu_device *adev)
{
	if (adev->gmc.gmc_funcs == NULL)
		adev->gmc.gmc_funcs = &gmc_v10_0_gmc_funcs;
}

static int gmc_v10_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v10_0_set_gmc_funcs(adev);
	gmc_v10_0_set_irq_funcs(adev);

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
	unsigned vm_inv_eng[AMDGPU_MAX_VMHUBS] = { 4, 4 };
	unsigned i;

	for(i = 0; i < adev->num_rings; ++i) {
		struct amdgpu_ring *ring = adev->rings[i];
		unsigned vmhub = ring->funcs->vmhub;

		ring->vm_inv_eng = vm_inv_eng[vmhub]++;
		dev_info(adev->dev, "ring %u(%s) uses VM inv eng %u on hub %u\n",
			 ring->idx, ring->name, ring->vm_inv_eng,
			 ring->funcs->vmhub);
	}

	/* Engine 17 is used for GART flushes */
	for(i = 0; i < AMDGPU_MAX_VMHUBS; ++i)
		BUG_ON(vm_inv_eng[i] > 17);

	return amdgpu_irq_get(adev, &adev->gmc.vm_fault, 0);
}

static void gmc_v10_0_vram_gtt_location(struct amdgpu_device *adev,
					struct amdgpu_gmc *mc)
{
	u64 base = 0;

	base = gfxhub_v2_0_get_fb_location(adev);

	amdgpu_gmc_vram_location(adev, &adev->gmc, base);
	amdgpu_gmc_gart_location(adev, mc);

	/* base offset of vram pages */
	adev->vm_manager.vram_base_offset = gfxhub_v2_0_get_mc_fb_offset(adev);
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
	/* Could aper size report 0 ? */
	adev->gmc.aper_base = pci_resource_start(adev->pdev, 0);
	adev->gmc.aper_size = pci_resource_len(adev->pdev, 0);

	/* size in MB on si */
	adev->gmc.mc_vram_size =
		adev->nbio.funcs->get_memsize(adev) * 1024ULL * 1024ULL;
	adev->gmc.real_vram_size = adev->gmc.mc_vram_size;
	adev->gmc.visible_vram_size = adev->gmc.aper_size;

	/* In case the PCI BAR is larger than the actual amount of vram */
	if (adev->gmc.visible_vram_size > adev->gmc.real_vram_size)
		adev->gmc.visible_vram_size = adev->gmc.real_vram_size;

	/* set the gart size */
	if (amdgpu_gart_size == -1) {
		switch (adev->asic_type) {
		case CHIP_NAVI10:
		case CHIP_NAVI14:
		case CHIP_NAVI12:
		default:
			adev->gmc.gart_size = 512ULL << 20;
			break;
		}
	} else
		adev->gmc.gart_size = (u64)amdgpu_gart_size << 20;

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

static unsigned gmc_v10_0_get_vbios_fb_size(struct amdgpu_device *adev)
{
	u32 d1vga_control = RREG32_SOC15(DCE, 0, mmD1VGA_CONTROL);
	unsigned size;

	if (REG_GET_FIELD(d1vga_control, D1VGA_CONTROL, D1VGA_MODE_ENABLE)) {
		size = 9 * 1024 * 1024; /* reserve 8MB for vga emulator and 1 MB for FB */
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
	/* return 0 if the pre-OS buffer uses up most of vram */
	if ((adev->gmc.real_vram_size - size) < (8 * 1024 * 1024)) {
		DRM_ERROR("Warning: pre-OS buffer uses most of vram, \
				be aware of gart table overwrite\n");
		return 0;
	}

	return size;
}



static int gmc_v10_0_sw_init(void *handle)
{
	int r, vram_width = 0, vram_type = 0, vram_vendor = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gfxhub_v2_0_init(adev);
	mmhub_v2_0_init(adev);

	spin_lock_init(&adev->gmc.invalidate_lock);

	r = amdgpu_atomfirmware_get_vram_info(adev,
		&vram_width, &vram_type, &vram_vendor);
	if (!amdgpu_emu_mode)
		adev->gmc.vram_width = vram_width;
	else
		adev->gmc.vram_width = 1 * 128; /* numchan * chansize */

	adev->gmc.vram_type = vram_type;
	adev->gmc.vram_vendor = vram_vendor;
	switch (adev->asic_type) {
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
		adev->num_vmhubs = 2;
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
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_UTCL2,
			      UTCL2_1_0__SRCID__FAULT,
			      &adev->gmc.vm_fault);
	if (r)
		return r;

	/*
	 * Set the internal MC address mask This is the max address of the GPU's
	 * internal address space.
	 */
	adev->gmc.mc_mask = 0xffffffffffffULL; /* 48 bit MC */

	/*
	 * Reserve 8M stolen memory for navi10 like vega10
	 * TODO: will check if it's really needed on asic.
	 */
	if (amdgpu_emu_mode == 1)
		adev->gmc.stolen_size = 0;
	else
		adev->gmc.stolen_size = 9 * 1024 *1024;

	r = dma_set_mask_and_coherent(adev->dev, DMA_BIT_MASK(44));
	if (r) {
		printk(KERN_WARNING "amdgpu: No suitable DMA available.\n");
		return r;
	}

	r = gmc_v10_0_mc_init(adev);
	if (r)
		return r;

	adev->gmc.stolen_size = gmc_v10_0_get_vbios_fb_size(adev);

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
	adev->vm_manager.id_mgr[AMDGPU_GFXHUB_0].num_ids = AMDGPU_NUM_OF_VMIDS;
	adev->vm_manager.id_mgr[AMDGPU_MMHUB_0].num_ids = AMDGPU_NUM_OF_VMIDS;

	amdgpu_vm_manager_init(adev);

	return 0;
}

/**
 * gmc_v8_0_gart_fini - vm fini callback
 *
 * @adev: amdgpu_device pointer
 *
 * Tears down the driver GART/VM setup (CIK).
 */
static void gmc_v10_0_gart_fini(struct amdgpu_device *adev)
{
	amdgpu_gart_table_vram_free(adev);
	amdgpu_gart_fini(adev);
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
	switch (adev->asic_type) {
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
		break;
	default:
		break;
	}
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
	u32 tmp;

	if (adev->gart.bo == NULL) {
		dev_err(adev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}

	r = amdgpu_gart_table_vram_pin(adev);
	if (r)
		return r;

	r = gfxhub_v2_0_gart_enable(adev);
	if (r)
		return r;

	r = mmhub_v2_0_gart_enable(adev);
	if (r)
		return r;

	tmp = RREG32_SOC15(HDP, 0, mmHDP_MISC_CNTL);
	tmp |= HDP_MISC_CNTL__FLUSH_INVALIDATE_CACHE_MASK;
	WREG32_SOC15(HDP, 0, mmHDP_MISC_CNTL, tmp);

	tmp = RREG32_SOC15(HDP, 0, mmHDP_HOST_PATH_CNTL);
	WREG32_SOC15(HDP, 0, mmHDP_HOST_PATH_CNTL, tmp);

	/* Flush HDP after it is initialized */
	adev->nbio.funcs->hdp_flush(adev, NULL);

	value = (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_ALWAYS) ?
		false : true;

	gfxhub_v2_0_set_fault_enable_default(adev, value);
	mmhub_v2_0_set_fault_enable_default(adev, value);
	gmc_v10_0_flush_gpu_tlb(adev, 0, AMDGPU_MMHUB_0, 0);
	gmc_v10_0_flush_gpu_tlb(adev, 0, AMDGPU_GFXHUB_0, 0);

	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(adev->gmc.gart_size >> 20),
		 (unsigned long long)amdgpu_bo_gpu_offset(adev->gart.bo));

	adev->gart.ready = true;

	return 0;
}

static int gmc_v10_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* The sequence of these two function calls matters.*/
	gmc_v10_0_init_golden_registers(adev);

	r = gmc_v10_0_gart_enable(adev);
	if (r)
		return r;

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
	gfxhub_v2_0_gart_disable(adev);
	mmhub_v2_0_gart_disable(adev);
	amdgpu_gart_table_vram_unpin(adev);
}

static int gmc_v10_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev)) {
		/* full access mode, so don't touch any GMC register */
		DRM_DEBUG("For SRIOV client, shouldn't do anything.\n");
		return 0;
	}

	amdgpu_irq_put(adev, &adev->gmc.vm_fault, 0);
	gmc_v10_0_gart_disable(adev);

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

	r = mmhub_v2_0_set_clockgating(adev, state);
	if (r)
		return r;

	return athub_v2_0_set_clockgating(adev, state);
}

static void gmc_v10_0_get_clockgating_state(void *handle, u32 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	mmhub_v2_0_get_clockgating(adev, flags);

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
