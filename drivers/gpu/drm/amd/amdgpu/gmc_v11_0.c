/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "gmc_v11_0.h"
#include "umc_v8_10.h"
#include "athub/athub_3_0_0_sh_mask.h"
#include "athub/athub_3_0_0_offset.h"
#include "dcn/dcn_3_2_0_offset.h"
#include "dcn/dcn_3_2_0_sh_mask.h"
#include "oss/osssys_6_0_0_offset.h"
#include "ivsrcid/vmc/irqsrcs_vmc_1_0.h"
#include "navi10_enum.h"
#include "soc15.h"
#include "soc15d.h"
#include "soc15_common.h"
#include "nbio_v4_3.h"
#include "gfxhub_v3_0.h"
#include "gfxhub_v3_0_3.h"
#include "mmhub_v3_0.h"
#include "mmhub_v3_0_1.h"
#include "mmhub_v3_0_2.h"
#include "athub_v3_0.h"


static int gmc_v11_0_ecc_interrupt_state(struct amdgpu_device *adev,
					 struct amdgpu_irq_src *src,
					 unsigned type,
					 enum amdgpu_interrupt_state state)
{
	return 0;
}

static int
gmc_v11_0_vm_fault_interrupt_state(struct amdgpu_device *adev,
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

static int gmc_v11_0_process_interrupt(struct amdgpu_device *adev,
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
		if (entry->vmid_src == AMDGPU_GFXHUB(0))
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
		if (!amdgpu_sriov_vf(adev))
			hub->vmhub_funcs->print_l2_protection_fault_status(adev, status);
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs gmc_v11_0_irq_funcs = {
	.set = gmc_v11_0_vm_fault_interrupt_state,
	.process = gmc_v11_0_process_interrupt,
};

static const struct amdgpu_irq_src_funcs gmc_v11_0_ecc_funcs = {
	.set = gmc_v11_0_ecc_interrupt_state,
	.process = amdgpu_umc_process_ecc_irq,
};

static void gmc_v11_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gmc.vm_fault.num_types = 1;
	adev->gmc.vm_fault.funcs = &gmc_v11_0_irq_funcs;

	if (!amdgpu_sriov_vf(adev)) {
		adev->gmc.ecc_irq.num_types = 1;
		adev->gmc.ecc_irq.funcs = &gmc_v11_0_ecc_funcs;
	}
}

/**
 * gmc_v11_0_use_invalidate_semaphore - judge whether to use semaphore
 *
 * @adev: amdgpu_device pointer
 * @vmhub: vmhub type
 *
 */
static bool gmc_v11_0_use_invalidate_semaphore(struct amdgpu_device *adev,
				       uint32_t vmhub)
{
	return ((vmhub == AMDGPU_MMHUB0(0)) &&
		(!amdgpu_sriov_vf(adev)));
}

static bool gmc_v11_0_get_vmid_pasid_mapping_info(
					struct amdgpu_device *adev,
					uint8_t vmid, uint16_t *p_pasid)
{
	*p_pasid = RREG32(SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_0_LUT) + vmid) & 0xffff;

	return !!(*p_pasid);
}

/*
 * GART
 * VMID 0 is the physical GPU addresses as used by the kernel.
 * VMIDs 1-15 are used for userspace clients and are handled
 * by the amdgpu vm/hsa code.
 */

static void gmc_v11_0_flush_vm_hub(struct amdgpu_device *adev, uint32_t vmid,
				   unsigned int vmhub, uint32_t flush_type)
{
	bool use_semaphore = gmc_v11_0_use_invalidate_semaphore(adev, vmhub);
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

	/* TODO: It needs to continue working on debugging with semaphore for GFXHUB as well. */
	if (use_semaphore)
		/*
		 * add semaphore release after invalidation,
		 * write with 0 means semaphore release
		 */
		WREG32_RLC_NO_KIQ(hub->vm_inv_eng0_sem +
			      hub->eng_distance * eng, 0, hub_ip);

	/* Issue additional private vm invalidation to MMHUB */
	if ((vmhub != AMDGPU_GFXHUB(0)) &&
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

	DRM_ERROR("Timeout waiting for VM flush ACK!\n");
}

/**
 * gmc_v11_0_flush_gpu_tlb - gart tlb flush callback
 *
 * @adev: amdgpu_device pointer
 * @vmid: vm instance to flush
 * @vmhub: which hub to flush
 * @flush_type: the flush type
 *
 * Flush the TLB for the requested page table.
 */
static void gmc_v11_0_flush_gpu_tlb(struct amdgpu_device *adev, uint32_t vmid,
					uint32_t vmhub, uint32_t flush_type)
{
	if ((vmhub == AMDGPU_GFXHUB(0)) && !adev->gfx.is_poweron)
		return;

	/* flush hdp cache */
	adev->hdp.funcs->flush_hdp(adev, NULL);

	/* For SRIOV run time, driver shouldn't access the register through MMIO
	 * Directly use kiq to do the vm invalidation instead
	 */
	if ((adev->gfx.kiq[0].ring.sched.ready || adev->mes.ring.sched.ready) &&
	    (amdgpu_sriov_runtime(adev) || !amdgpu_sriov_vf(adev))) {
		struct amdgpu_vmhub *hub = &adev->vmhub[vmhub];
		const unsigned eng = 17;
		u32 inv_req = hub->vmhub_funcs->get_invalidate_req(vmid, flush_type);
		u32 req = hub->vm_inv_eng0_req + hub->eng_distance * eng;
		u32 ack = hub->vm_inv_eng0_ack + hub->eng_distance * eng;

		amdgpu_virt_kiq_reg_write_reg_wait(adev, req, ack, inv_req,
				1 << vmid);
		return;
	}

	mutex_lock(&adev->mman.gtt_window_lock);
	gmc_v11_0_flush_vm_hub(adev, vmid, vmhub, 0);
	mutex_unlock(&adev->mman.gtt_window_lock);
	return;
}

/**
 * gmc_v11_0_flush_gpu_tlb_pasid - tlb flush via pasid
 *
 * @adev: amdgpu_device pointer
 * @pasid: pasid to be flush
 * @flush_type: the flush type
 * @all_hub: flush all hubs
 * @inst: is used to select which instance of KIQ to use for the invalidation
 *
 * Flush the TLB for the requested pasid.
 */
static int gmc_v11_0_flush_gpu_tlb_pasid(struct amdgpu_device *adev,
					uint16_t pasid, uint32_t flush_type,
					bool all_hub, uint32_t inst)
{
	int vmid, i;
	signed long r;
	uint32_t seq;
	uint16_t queried_pasid;
	bool ret;
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
		r = amdgpu_fence_wait_polling(ring, seq, adev->usec_timeout);
		if (r < 1) {
			dev_err(adev->dev, "wait for kiq fence error: %ld.\n", r);
			return -ETIME;
		}

		return 0;
	}

	for (vmid = 1; vmid < 16; vmid++) {

		ret = gmc_v11_0_get_vmid_pasid_mapping_info(adev, vmid,
				&queried_pasid);
		if (ret	&& queried_pasid == pasid) {
			if (all_hub) {
				for_each_set_bit(i, adev->vmhubs_mask, AMDGPU_MAX_VMHUBS)
					gmc_v11_0_flush_gpu_tlb(adev, vmid,
							i, flush_type);
			} else {
				gmc_v11_0_flush_gpu_tlb(adev, vmid,
						AMDGPU_GFXHUB(0), flush_type);
			}
		}
	}

	return 0;
}

static uint64_t gmc_v11_0_emit_flush_gpu_tlb(struct amdgpu_ring *ring,
					     unsigned vmid, uint64_t pd_addr)
{
	bool use_semaphore = gmc_v11_0_use_invalidate_semaphore(ring->adev, ring->vm_hub);
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

static void gmc_v11_0_emit_pasid_mapping(struct amdgpu_ring *ring, unsigned vmid,
					 unsigned pasid)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t reg;

	/* MES fw manages IH_VMID_x_LUT updating */
	if (ring->is_mes_queue)
		return;

	if (ring->vm_hub == AMDGPU_GFXHUB(0))
		reg = SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_0_LUT) + vmid;
	else
		reg = SOC15_REG_OFFSET(OSSSYS, 0, regIH_VMID_0_LUT_MM) + vmid;

	amdgpu_ring_emit_wreg(ring, reg, pasid);
}

/*
 * PTE format:
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
 * PDE format:
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

static uint64_t gmc_v11_0_map_mtype(struct amdgpu_device *adev, uint32_t flags)
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

static void gmc_v11_0_get_vm_pde(struct amdgpu_device *adev, int level,
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

static void gmc_v11_0_get_vm_pte(struct amdgpu_device *adev,
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

static unsigned gmc_v11_0_get_vbios_fb_size(struct amdgpu_device *adev)
{
	u32 d1vga_control = RREG32_SOC15(DCE, 0, regD1VGA_CONTROL);
	unsigned size;

	if (REG_GET_FIELD(d1vga_control, D1VGA_CONTROL, D1VGA_MODE_ENABLE)) {
		size = AMDGPU_VBIOS_VGA_ALLOCATION;
	} else {
		u32 viewport;
		u32 pitch;

		viewport = RREG32_SOC15(DCE, 0, regHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION);
		pitch = RREG32_SOC15(DCE, 0, regHUBPREQ0_DCSURF_SURFACE_PITCH);
		size = (REG_GET_FIELD(viewport,
					HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION, PRI_VIEWPORT_HEIGHT) *
				REG_GET_FIELD(pitch, HUBPREQ0_DCSURF_SURFACE_PITCH, PITCH) *
				4);
	}

	return size;
}

static const struct amdgpu_gmc_funcs gmc_v11_0_gmc_funcs = {
	.flush_gpu_tlb = gmc_v11_0_flush_gpu_tlb,
	.flush_gpu_tlb_pasid = gmc_v11_0_flush_gpu_tlb_pasid,
	.emit_flush_gpu_tlb = gmc_v11_0_emit_flush_gpu_tlb,
	.emit_pasid_mapping = gmc_v11_0_emit_pasid_mapping,
	.map_mtype = gmc_v11_0_map_mtype,
	.get_vm_pde = gmc_v11_0_get_vm_pde,
	.get_vm_pte = gmc_v11_0_get_vm_pte,
	.get_vbios_fb_size = gmc_v11_0_get_vbios_fb_size,
};

static void gmc_v11_0_set_gmc_funcs(struct amdgpu_device *adev)
{
	adev->gmc.gmc_funcs = &gmc_v11_0_gmc_funcs;
}

static void gmc_v11_0_set_umc_funcs(struct amdgpu_device *adev)
{
	switch (adev->ip_versions[UMC_HWIP][0]) {
	case IP_VERSION(8, 10, 0):
		adev->umc.channel_inst_num = UMC_V8_10_CHANNEL_INSTANCE_NUM;
		adev->umc.umc_inst_num = UMC_V8_10_UMC_INSTANCE_NUM;
		adev->umc.max_ras_err_cnt_per_query = UMC_V8_10_TOTAL_CHANNEL_NUM(adev);
		adev->umc.channel_offs = UMC_V8_10_PER_CHANNEL_OFFSET;
		adev->umc.retire_unit = UMC_V8_10_NA_COL_2BITS_POWER_OF_2_NUM;
		if (adev->umc.node_inst_num == 4)
			adev->umc.channel_idx_tbl = &umc_v8_10_channel_idx_tbl_ext0[0][0][0];
		else
			adev->umc.channel_idx_tbl = &umc_v8_10_channel_idx_tbl[0][0][0];
		adev->umc.ras = &umc_v8_10_ras;
		break;
	case IP_VERSION(8, 11, 0):
		break;
	default:
		break;
	}
}


static void gmc_v11_0_set_mmhub_funcs(struct amdgpu_device *adev)
{
	switch (adev->ip_versions[MMHUB_HWIP][0]) {
	case IP_VERSION(3, 0, 1):
		adev->mmhub.funcs = &mmhub_v3_0_1_funcs;
		break;
	case IP_VERSION(3, 0, 2):
		adev->mmhub.funcs = &mmhub_v3_0_2_funcs;
		break;
	default:
		adev->mmhub.funcs = &mmhub_v3_0_funcs;
		break;
	}
}

static void gmc_v11_0_set_gfxhub_funcs(struct amdgpu_device *adev)
{
	switch (adev->ip_versions[GC_HWIP][0]) {
	case IP_VERSION(11, 0, 3):
		adev->gfxhub.funcs = &gfxhub_v3_0_3_funcs;
		break;
	default:
		adev->gfxhub.funcs = &gfxhub_v3_0_funcs;
		break;
	}
}

static int gmc_v11_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v11_0_set_gfxhub_funcs(adev);
	gmc_v11_0_set_mmhub_funcs(adev);
	gmc_v11_0_set_gmc_funcs(adev);
	gmc_v11_0_set_irq_funcs(adev);
	gmc_v11_0_set_umc_funcs(adev);

	adev->gmc.shared_aperture_start = 0x2000000000000000ULL;
	adev->gmc.shared_aperture_end =
		adev->gmc.shared_aperture_start + (4ULL << 30) - 1;
	adev->gmc.private_aperture_start = 0x1000000000000000ULL;
	adev->gmc.private_aperture_end =
		adev->gmc.private_aperture_start + (4ULL << 30) - 1;
	adev->gmc.noretry_flags = AMDGPU_VM_NORETRY_FLAGS_TF;

	return 0;
}

static int gmc_v11_0_late_init(void *handle)
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

static void gmc_v11_0_vram_gtt_location(struct amdgpu_device *adev,
					struct amdgpu_gmc *mc)
{
	u64 base = 0;

	base = adev->mmhub.funcs->get_fb_location(adev);

	amdgpu_gmc_vram_location(adev, &adev->gmc, base);
	amdgpu_gmc_gart_location(adev, mc);
	amdgpu_gmc_agp_location(adev, mc);

	/* base offset of vram pages */
	if (amdgpu_sriov_vf(adev))
		adev->vm_manager.vram_base_offset = 0;
	else
		adev->vm_manager.vram_base_offset = adev->mmhub.funcs->get_mc_fb_offset(adev);
}

/**
 * gmc_v11_0_mc_init - initialize the memory controller driver params
 *
 * @adev: amdgpu_device pointer
 *
 * Look up the amount of vram, vram width, and decide how to place
 * vram and gart within the GPU's physical address space.
 * Returns 0 for success.
 */
static int gmc_v11_0_mc_init(struct amdgpu_device *adev)
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
		adev->gmc.aper_base = adev->mmhub.funcs->get_mc_fb_offset(adev);
		adev->gmc.aper_size = adev->gmc.real_vram_size;
	}
#endif
	/* In case the PCI BAR is larger than the actual amount of vram */
	adev->gmc.visible_vram_size = adev->gmc.aper_size;
	if (adev->gmc.visible_vram_size > adev->gmc.real_vram_size)
		adev->gmc.visible_vram_size = adev->gmc.real_vram_size;

	/* set the gart size */
	if (amdgpu_gart_size == -1) {
		adev->gmc.gart_size = 512ULL << 20;
	} else
		adev->gmc.gart_size = (u64)amdgpu_gart_size << 20;

	gmc_v11_0_vram_gtt_location(adev, &adev->gmc);

	return 0;
}

static int gmc_v11_0_gart_init(struct amdgpu_device *adev)
{
	int r;

	if (adev->gart.bo) {
		WARN(1, "PCIE GART already initialized\n");
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

static int gmc_v11_0_sw_init(void *handle)
{
	int r, vram_width = 0, vram_type = 0, vram_vendor = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->mmhub.funcs->init(adev);

	spin_lock_init(&adev->gmc.invalidate_lock);

	r = amdgpu_atomfirmware_get_vram_info(adev,
					      &vram_width, &vram_type, &vram_vendor);
	adev->gmc.vram_width = vram_width;

	adev->gmc.vram_type = vram_type;
	adev->gmc.vram_vendor = vram_vendor;

	switch (adev->ip_versions[GC_HWIP][0]) {
	case IP_VERSION(11, 0, 0):
	case IP_VERSION(11, 0, 1):
	case IP_VERSION(11, 0, 2):
	case IP_VERSION(11, 0, 3):
	case IP_VERSION(11, 0, 4):
		set_bit(AMDGPU_GFXHUB(0), adev->vmhubs_mask);
		set_bit(AMDGPU_MMHUB0(0), adev->vmhubs_mask);
		/*
		 * To fulfill 4-level page support,
		 * vm size is 256TB (48bit), maximum size,
		 * block size 512 (9bit)
		 */
		amdgpu_vm_adjust_size(adev, 256 * 1024, 9, 3, 48);
		break;
	default:
		break;
	}

	/* This interrupt is VMC page fault.*/
	r = amdgpu_irq_add_id(adev, SOC21_IH_CLIENTID_VMC,
			      VMC_1_0__SRCID__VM_FAULT,
			      &adev->gmc.vm_fault);

	if (r)
		return r;

	r = amdgpu_irq_add_id(adev, SOC21_IH_CLIENTID_GFX,
			      UTCL2_1_0__SRCID__FAULT,
			      &adev->gmc.vm_fault);
	if (r)
		return r;

	if (!amdgpu_sriov_vf(adev)) {
		/* interrupt sent to DF. */
		r = amdgpu_irq_add_id(adev, SOC21_IH_CLIENTID_DF, 0,
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

	r = gmc_v11_0_mc_init(adev);
	if (r)
		return r;

	amdgpu_gmc_get_vbios_allocations(adev);

	/* Memory manager */
	r = amdgpu_bo_init(adev);
	if (r)
		return r;

	r = gmc_v11_0_gart_init(adev);
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
 * gmc_v11_0_gart_fini - vm fini callback
 *
 * @adev: amdgpu_device pointer
 *
 * Tears down the driver GART/VM setup (CIK).
 */
static void gmc_v11_0_gart_fini(struct amdgpu_device *adev)
{
	amdgpu_gart_table_vram_free(adev);
}

static int gmc_v11_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_vm_manager_fini(adev);
	gmc_v11_0_gart_fini(adev);
	amdgpu_gem_force_release(adev);
	amdgpu_bo_fini(adev);

	return 0;
}

static void gmc_v11_0_init_golden_registers(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev)) {
		struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_MMHUB0(0)];

		WREG32(hub->vm_contexts_disable, 0);
		return;
	}
}

/**
 * gmc_v11_0_gart_enable - gart enable
 *
 * @adev: amdgpu_device pointer
 */
static int gmc_v11_0_gart_enable(struct amdgpu_device *adev)
{
	int r;
	bool value;

	if (adev->gart.bo == NULL) {
		dev_err(adev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}

	amdgpu_gtt_mgr_recover(&adev->mman.gtt_mgr);

	r = adev->mmhub.funcs->gart_enable(adev);
	if (r)
		return r;

	/* Flush HDP after it is initialized */
	adev->hdp.funcs->flush_hdp(adev, NULL);

	value = (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_ALWAYS) ?
		false : true;

	adev->mmhub.funcs->set_fault_enable_default(adev, value);
	gmc_v11_0_flush_gpu_tlb(adev, 0, AMDGPU_MMHUB0(0), 0);

	DRM_INFO("PCIE GART of %uM enabled (table at 0x%016llX).\n",
		 (unsigned)(adev->gmc.gart_size >> 20),
		 (unsigned long long)amdgpu_bo_gpu_offset(adev->gart.bo));

	return 0;
}

static int gmc_v11_0_hw_init(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* The sequence of these two function calls matters.*/
	gmc_v11_0_init_golden_registers(adev);

	r = gmc_v11_0_gart_enable(adev);
	if (r)
		return r;

	if (adev->umc.funcs && adev->umc.funcs->init_registers)
		adev->umc.funcs->init_registers(adev);

	return 0;
}

/**
 * gmc_v11_0_gart_disable - gart disable
 *
 * @adev: amdgpu_device pointer
 *
 * This disables all VM page table.
 */
static void gmc_v11_0_gart_disable(struct amdgpu_device *adev)
{
	adev->mmhub.funcs->gart_disable(adev);
}

static int gmc_v11_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev)) {
		/* full access mode, so don't touch any GMC register */
		DRM_DEBUG("For SRIOV client, shouldn't do anything.\n");
		return 0;
	}

	amdgpu_irq_put(adev, &adev->gmc.vm_fault, 0);
	gmc_v11_0_gart_disable(adev);

	return 0;
}

static int gmc_v11_0_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v11_0_hw_fini(adev);

	return 0;
}

static int gmc_v11_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = gmc_v11_0_hw_init(adev);
	if (r)
		return r;

	amdgpu_vmid_reset_all(adev);

	return 0;
}

static bool gmc_v11_0_is_idle(void *handle)
{
	/* MC is always ready in GMC v11.*/
	return true;
}

static int gmc_v11_0_wait_for_idle(void *handle)
{
	/* There is no need to wait for MC idle in GMC v11.*/
	return 0;
}

static int gmc_v11_0_soft_reset(void *handle)
{
	return 0;
}

static int gmc_v11_0_set_clockgating_state(void *handle,
					   enum amd_clockgating_state state)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = adev->mmhub.funcs->set_clockgating(adev, state);
	if (r)
		return r;

	return athub_v3_0_set_clockgating(adev, state);
}

static void gmc_v11_0_get_clockgating_state(void *handle, u64 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->mmhub.funcs->get_clockgating(adev, flags);

	athub_v3_0_get_clockgating(adev, flags);
}

static int gmc_v11_0_set_powergating_state(void *handle,
					   enum amd_powergating_state state)
{
	return 0;
}

const struct amd_ip_funcs gmc_v11_0_ip_funcs = {
	.name = "gmc_v11_0",
	.early_init = gmc_v11_0_early_init,
	.sw_init = gmc_v11_0_sw_init,
	.hw_init = gmc_v11_0_hw_init,
	.late_init = gmc_v11_0_late_init,
	.sw_fini = gmc_v11_0_sw_fini,
	.hw_fini = gmc_v11_0_hw_fini,
	.suspend = gmc_v11_0_suspend,
	.resume = gmc_v11_0_resume,
	.is_idle = gmc_v11_0_is_idle,
	.wait_for_idle = gmc_v11_0_wait_for_idle,
	.soft_reset = gmc_v11_0_soft_reset,
	.set_clockgating_state = gmc_v11_0_set_clockgating_state,
	.set_powergating_state = gmc_v11_0_set_powergating_state,
	.get_clockgating_state = gmc_v11_0_get_clockgating_state,
};

const struct amdgpu_ip_block_version gmc_v11_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_GMC,
	.major = 11,
	.minor = 0,
	.rev = 0,
	.funcs = &gmc_v11_0_ip_funcs,
};
