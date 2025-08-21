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

static bool gmc_v12_1_get_vmid_pasid_mapping_info(struct amdgpu_device *adev,
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

	hub_ip = (vmhub == AMDGPU_GFXHUB(0)) ?
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
	if ((vmhub == AMDGPU_GFXHUB(0)) && !adev->gfx.is_poweron)
		return;

	/* This is necessary for SRIOV as well as for GFXOFF to function
	 * properly under bare metal
	 */
	if (((adev->gfx.kiq[0].ring.sched.ready || adev->mes.ring[0].sched.ready) &&
	    (amdgpu_sriov_runtime(adev) || !amdgpu_sriov_vf(adev)))) {
		struct amdgpu_vmhub *hub = &adev->vmhub[vmhub];
		const unsigned eng = 17;
		u32 inv_req = hub->vmhub_funcs->get_invalidate_req(vmid, flush_type);
		u32 req = hub->vm_inv_eng0_req + hub->eng_distance * eng;
		u32 ack = hub->vm_inv_eng0_ack + hub->eng_distance * eng;

		amdgpu_gmc_fw_reg_write_reg_wait(adev, req, ack, inv_req,
				1 << vmid, GET_INST(GC, 0));
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

	for (vmid = 1; vmid < 16; vmid++) {
		bool valid;

		valid = gmc_v12_1_get_vmid_pasid_mapping_info(adev, vmid,
							      &queried);
		if (!valid || queried != pasid)
			continue;

		if (all_hub) {
			for_each_set_bit(i, adev->vmhubs_mask,
					 AMDGPU_MAX_VMHUBS)
				gmc_v12_1_flush_gpu_tlb(adev, vmid, i,
							flush_type);
		} else {
			gmc_v12_1_flush_gpu_tlb(adev, vmid, AMDGPU_GFXHUB(0),
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
	case AMDGPU_VM_MTYPE_UC:
		*flags = AMDGPU_PTE_MTYPE_GFX12(*flags, MTYPE_UC);
		break;
	}

	if (vm_flags & AMDGPU_VM_PAGE_NOALLOC)
		*flags |= AMDGPU_PTE_NOALLOC;
	else
		*flags &= ~AMDGPU_PTE_NOALLOC;

	if (vm_flags & AMDGPU_VM_PAGE_PRT) {
		*flags |= AMDGPU_PTE_SNOOPED;
		*flags |= AMDGPU_PTE_SYSTEM;
		*flags |= AMDGPU_PTE_IS_PTE;
		*flags &= ~AMDGPU_PTE_VALID;
	}

	if (bo && bo->flags & (AMDGPU_GEM_CREATE_COHERENT |
			       AMDGPU_GEM_CREATE_EXT_COHERENT |
			       AMDGPU_GEM_CREATE_UNCACHED))
		*flags = AMDGPU_PTE_MTYPE_NV10(*flags, MTYPE_UC);

	if (bo && bo->flags & AMDGPU_GEM_CREATE_UNCACHED)
		*flags = AMDGPU_PTE_MTYPE_GFX12(*flags, MTYPE_UC);
}

static const struct amdgpu_gmc_funcs gmc_v12_1_gmc_funcs = {
	.flush_gpu_tlb = gmc_v12_1_flush_gpu_tlb,
	.flush_gpu_tlb_pasid = gmc_v12_1_flush_gpu_tlb_pasid,
	.emit_flush_gpu_tlb = gmc_v12_1_emit_flush_gpu_tlb,
	.emit_pasid_mapping = gmc_v12_1_emit_pasid_mapping,
	.get_vm_pde = gmc_v12_1_get_vm_pde,
	.get_vm_pte = gmc_v12_1_get_vm_pte,
};

void gmc_v12_1_set_gmc_funcs(struct amdgpu_device *adev)
{
	adev->gmc.gmc_funcs = &gmc_v12_1_gmc_funcs;
}
