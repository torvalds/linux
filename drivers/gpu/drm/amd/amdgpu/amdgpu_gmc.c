/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#include <linux/io-64-nonatomic-lo-hi.h>

#include "amdgpu.h"
#include "amdgpu_ras.h"
#include "amdgpu_xgmi.h"

/**
 * amdgpu_gmc_get_pde_for_bo - get the PDE for a BO
 *
 * @bo: the BO to get the PDE for
 * @level: the level in the PD hirarchy
 * @addr: resulting addr
 * @flags: resulting flags
 *
 * Get the address and flags to be used for a PDE (Page Directory Entry).
 */
void amdgpu_gmc_get_pde_for_bo(struct amdgpu_bo *bo, int level,
			       uint64_t *addr, uint64_t *flags)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct ttm_dma_tt *ttm;

	switch (bo->tbo.mem.mem_type) {
	case TTM_PL_TT:
		ttm = container_of(bo->tbo.ttm, struct ttm_dma_tt, ttm);
		*addr = ttm->dma_address[0];
		break;
	case TTM_PL_VRAM:
		*addr = amdgpu_bo_gpu_offset(bo);
		break;
	default:
		*addr = 0;
		break;
	}
	*flags = amdgpu_ttm_tt_pde_flags(bo->tbo.ttm, &bo->tbo.mem);
	amdgpu_gmc_get_vm_pde(adev, level, addr, flags);
}

/**
 * amdgpu_gmc_pd_addr - return the address of the root directory
 *
 */
uint64_t amdgpu_gmc_pd_addr(struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	uint64_t pd_addr;

	/* TODO: move that into ASIC specific code */
	if (adev->asic_type >= CHIP_VEGA10) {
		uint64_t flags = AMDGPU_PTE_VALID;

		amdgpu_gmc_get_pde_for_bo(bo, -1, &pd_addr, &flags);
		pd_addr |= flags;
	} else {
		pd_addr = amdgpu_bo_gpu_offset(bo);
	}
	return pd_addr;
}

/**
 * amdgpu_gmc_set_pte_pde - update the page tables using CPU
 *
 * @adev: amdgpu_device pointer
 * @cpu_pt_addr: cpu address of the page table
 * @gpu_page_idx: entry in the page table to update
 * @addr: dst addr to write into pte/pde
 * @flags: access flags
 *
 * Update the page tables using CPU.
 */
int amdgpu_gmc_set_pte_pde(struct amdgpu_device *adev, void *cpu_pt_addr,
				uint32_t gpu_page_idx, uint64_t addr,
				uint64_t flags)
{
	void __iomem *ptr = (void *)cpu_pt_addr;
	uint64_t value;

	/*
	 * The following is for PTE only. GART does not have PDEs.
	*/
	value = addr & 0x0000FFFFFFFFF000ULL;
	value |= flags;
	writeq(value, ptr + (gpu_page_idx * 8));
	return 0;
}

/**
 * amdgpu_gmc_agp_addr - return the address in the AGP address space
 *
 * @tbo: TTM BO which needs the address, must be in GTT domain
 *
 * Tries to figure out how to access the BO through the AGP aperture. Returns
 * AMDGPU_BO_INVALID_OFFSET if that is not possible.
 */
uint64_t amdgpu_gmc_agp_addr(struct ttm_buffer_object *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->bdev);
	struct ttm_dma_tt *ttm;

	if (bo->num_pages != 1 || bo->ttm->caching_state == tt_cached)
		return AMDGPU_BO_INVALID_OFFSET;

	ttm = container_of(bo->ttm, struct ttm_dma_tt, ttm);
	if (ttm->dma_address[0] + PAGE_SIZE >= adev->gmc.agp_size)
		return AMDGPU_BO_INVALID_OFFSET;

	return adev->gmc.agp_start + ttm->dma_address[0];
}

/**
 * amdgpu_gmc_vram_location - try to find VRAM location
 *
 * @adev: amdgpu device structure holding all necessary information
 * @mc: memory controller structure holding memory information
 * @base: base address at which to put VRAM
 *
 * Function will try to place VRAM at base address provided
 * as parameter.
 */
void amdgpu_gmc_vram_location(struct amdgpu_device *adev, struct amdgpu_gmc *mc,
			      u64 base)
{
	uint64_t limit = (uint64_t)amdgpu_vram_limit << 20;

	mc->vram_start = base;
	mc->vram_end = mc->vram_start + mc->mc_vram_size - 1;
	if (limit && limit < mc->real_vram_size)
		mc->real_vram_size = limit;

	if (mc->xgmi.num_physical_nodes == 0) {
		mc->fb_start = mc->vram_start;
		mc->fb_end = mc->vram_end;
	}
	dev_info(adev->dev, "VRAM: %lluM 0x%016llX - 0x%016llX (%lluM used)\n",
			mc->mc_vram_size >> 20, mc->vram_start,
			mc->vram_end, mc->real_vram_size >> 20);
}

/**
 * amdgpu_gmc_gart_location - try to find GART location
 *
 * @adev: amdgpu device structure holding all necessary information
 * @mc: memory controller structure holding memory information
 *
 * Function will place try to place GART before or after VRAM.
 *
 * If GART size is bigger than space left then we ajust GART size.
 * Thus function will never fails.
 */
void amdgpu_gmc_gart_location(struct amdgpu_device *adev, struct amdgpu_gmc *mc)
{
	const uint64_t four_gb = 0x100000000ULL;
	u64 size_af, size_bf;
	/*To avoid the hole, limit the max mc address to AMDGPU_GMC_HOLE_START*/
	u64 max_mc_address = min(adev->gmc.mc_mask, AMDGPU_GMC_HOLE_START - 1);

	mc->gart_size += adev->pm.smu_prv_buffer_size;

	/* VCE doesn't like it when BOs cross a 4GB segment, so align
	 * the GART base on a 4GB boundary as well.
	 */
	size_bf = mc->fb_start;
	size_af = max_mc_address + 1 - ALIGN(mc->fb_end + 1, four_gb);

	if (mc->gart_size > max(size_bf, size_af)) {
		dev_warn(adev->dev, "limiting GART\n");
		mc->gart_size = max(size_bf, size_af);
	}

	if ((size_bf >= mc->gart_size && size_bf < size_af) ||
	    (size_af < mc->gart_size))
		mc->gart_start = 0;
	else
		mc->gart_start = max_mc_address - mc->gart_size + 1;

	mc->gart_start &= ~(four_gb - 1);
	mc->gart_end = mc->gart_start + mc->gart_size - 1;
	dev_info(adev->dev, "GART: %lluM 0x%016llX - 0x%016llX\n",
			mc->gart_size >> 20, mc->gart_start, mc->gart_end);
}

/**
 * amdgpu_gmc_agp_location - try to find AGP location
 * @adev: amdgpu device structure holding all necessary information
 * @mc: memory controller structure holding memory information
 *
 * Function will place try to find a place for the AGP BAR in the MC address
 * space.
 *
 * AGP BAR will be assigned the largest available hole in the address space.
 * Should be called after VRAM and GART locations are setup.
 */
void amdgpu_gmc_agp_location(struct amdgpu_device *adev, struct amdgpu_gmc *mc)
{
	const uint64_t sixteen_gb = 1ULL << 34;
	const uint64_t sixteen_gb_mask = ~(sixteen_gb - 1);
	u64 size_af, size_bf;

	if (amdgpu_sriov_vf(adev)) {
		mc->agp_start = 0xffffffffffff;
		mc->agp_end = 0x0;
		mc->agp_size = 0;

		return;
	}

	if (mc->fb_start > mc->gart_start) {
		size_bf = (mc->fb_start & sixteen_gb_mask) -
			ALIGN(mc->gart_end + 1, sixteen_gb);
		size_af = mc->mc_mask + 1 - ALIGN(mc->fb_end + 1, sixteen_gb);
	} else {
		size_bf = mc->fb_start & sixteen_gb_mask;
		size_af = (mc->gart_start & sixteen_gb_mask) -
			ALIGN(mc->fb_end + 1, sixteen_gb);
	}

	if (size_bf > size_af) {
		mc->agp_start = (mc->fb_start - size_bf) & sixteen_gb_mask;
		mc->agp_size = size_bf;
	} else {
		mc->agp_start = ALIGN(mc->fb_end + 1, sixteen_gb);
		mc->agp_size = size_af;
	}

	mc->agp_end = mc->agp_start + mc->agp_size - 1;
	dev_info(adev->dev, "AGP: %lluM 0x%016llX - 0x%016llX\n",
			mc->agp_size >> 20, mc->agp_start, mc->agp_end);
}

/**
 * amdgpu_gmc_filter_faults - filter VM faults
 *
 * @adev: amdgpu device structure
 * @addr: address of the VM fault
 * @pasid: PASID of the process causing the fault
 * @timestamp: timestamp of the fault
 *
 * Returns:
 * True if the fault was filtered and should not be processed further.
 * False if the fault is a new one and needs to be handled.
 */
bool amdgpu_gmc_filter_faults(struct amdgpu_device *adev, uint64_t addr,
			      uint16_t pasid, uint64_t timestamp)
{
	struct amdgpu_gmc *gmc = &adev->gmc;

	uint64_t stamp, key = addr << 4 | pasid;
	struct amdgpu_gmc_fault *fault;
	uint32_t hash;

	/* If we don't have space left in the ring buffer return immediately */
	stamp = max(timestamp, AMDGPU_GMC_FAULT_TIMEOUT + 1) -
		AMDGPU_GMC_FAULT_TIMEOUT;
	if (gmc->fault_ring[gmc->last_fault].timestamp >= stamp)
		return true;

	/* Try to find the fault in the hash */
	hash = hash_64(key, AMDGPU_GMC_FAULT_HASH_ORDER);
	fault = &gmc->fault_ring[gmc->fault_hash[hash].idx];
	while (fault->timestamp >= stamp) {
		uint64_t tmp;

		if (fault->key == key)
			return true;

		tmp = fault->timestamp;
		fault = &gmc->fault_ring[fault->next];

		/* Check if the entry was reused */
		if (fault->timestamp >= tmp)
			break;
	}

	/* Add the fault to the ring */
	fault = &gmc->fault_ring[gmc->last_fault];
	fault->key = key;
	fault->timestamp = timestamp;

	/* And update the hash */
	fault->next = gmc->fault_hash[hash].idx;
	gmc->fault_hash[hash].idx = gmc->last_fault++;
	return false;
}

int amdgpu_gmc_ras_late_init(struct amdgpu_device *adev)
{
	int r;

	if (adev->umc.funcs && adev->umc.funcs->ras_late_init) {
		r = adev->umc.funcs->ras_late_init(adev);
		if (r)
			return r;
	}

	if (adev->mmhub.funcs && adev->mmhub.funcs->ras_late_init) {
		r = adev->mmhub.funcs->ras_late_init(adev);
		if (r)
			return r;
	}

	return amdgpu_xgmi_ras_late_init(adev);
}

void amdgpu_gmc_ras_fini(struct amdgpu_device *adev)
{
	amdgpu_umc_ras_fini(adev);
	amdgpu_mmhub_ras_fini(adev);
	amdgpu_xgmi_ras_fini(adev);
}

	/*
	 * The latest engine allocation on gfx9/10 is:
	 * Engine 2, 3: firmware
	 * Engine 0, 1, 4~16: amdgpu ring,
	 *                    subject to change when ring number changes
	 * Engine 17: Gart flushes
	 */
#define GFXHUB_FREE_VM_INV_ENGS_BITMAP		0x1FFF3
#define MMHUB_FREE_VM_INV_ENGS_BITMAP		0x1FFF3

int amdgpu_gmc_allocate_vm_inv_eng(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	unsigned vm_inv_engs[AMDGPU_MAX_VMHUBS] =
		{GFXHUB_FREE_VM_INV_ENGS_BITMAP, MMHUB_FREE_VM_INV_ENGS_BITMAP,
		GFXHUB_FREE_VM_INV_ENGS_BITMAP};
	unsigned i;
	unsigned vmhub, inv_eng;

	for (i = 0; i < adev->num_rings; ++i) {
		ring = adev->rings[i];
		vmhub = ring->funcs->vmhub;

		inv_eng = ffs(vm_inv_engs[vmhub]);
		if (!inv_eng) {
			dev_err(adev->dev, "no VM inv eng for ring %s\n",
				ring->name);
			return -EINVAL;
		}

		ring->vm_inv_eng = inv_eng - 1;
		vm_inv_engs[vmhub] &= ~(1 << ring->vm_inv_eng);

		dev_info(adev->dev, "ring %s uses VM inv eng %u on hub %u\n",
			 ring->name, ring->vm_inv_eng, ring->funcs->vmhub);
	}

	return 0;
}

/**
 * amdgpu_tmz_set -- check and set if a device supports TMZ
 * @adev: amdgpu_device pointer
 *
 * Check and set if an the device @adev supports Trusted Memory
 * Zones (TMZ).
 */
void amdgpu_gmc_tmz_set(struct amdgpu_device *adev)
{
	switch (adev->asic_type) {
	case CHIP_RAVEN:
	case CHIP_RENOIR:
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
		/* Don't enable it by default yet.
		 */
		if (amdgpu_tmz < 1) {
			adev->gmc.tmz_enabled = false;
			dev_info(adev->dev,
				 "Trusted Memory Zone (TMZ) feature disabled as experimental (default)\n");
		} else {
			adev->gmc.tmz_enabled = true;
			dev_info(adev->dev,
				 "Trusted Memory Zone (TMZ) feature enabled as experimental (cmd line)\n");
		}
		break;
	default:
		adev->gmc.tmz_enabled = false;
		dev_warn(adev->dev,
			 "Trusted Memory Zone (TMZ) feature not supported\n");
		break;
	}
}
