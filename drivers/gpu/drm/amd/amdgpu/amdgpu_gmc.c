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
#include "amdgpu_gmc.h"
#include "amdgpu_ras.h"
#include "amdgpu_xgmi.h"

#include <drm/drm_drv.h>

/**
 * amdgpu_gmc_pdb0_alloc - allocate vram for pdb0
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate video memory for pdb0 and map it for CPU access
 * Returns 0 for success, error for failure.
 */
int amdgpu_gmc_pdb0_alloc(struct amdgpu_device *adev)
{
	int r;
	struct amdgpu_bo_param bp;
	u64 vram_size = adev->gmc.xgmi.node_segment_size * adev->gmc.xgmi.num_physical_nodes;
	uint32_t pde0_page_shift = adev->gmc.vmid0_page_table_block_size + 21;
	uint32_t npdes = (vram_size + (1ULL << pde0_page_shift) -1) >> pde0_page_shift;

	memset(&bp, 0, sizeof(bp));
	bp.size = PAGE_ALIGN((npdes + 1) * 8);
	bp.byte_align = PAGE_SIZE;
	bp.domain = AMDGPU_GEM_DOMAIN_VRAM;
	bp.flags = AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED |
		AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS;
	bp.type = ttm_bo_type_kernel;
	bp.resv = NULL;
	bp.bo_ptr_size = sizeof(struct amdgpu_bo);

	r = amdgpu_bo_create(adev, &bp, &adev->gmc.pdb0_bo);
	if (r)
		return r;

	r = amdgpu_bo_reserve(adev->gmc.pdb0_bo, false);
	if (unlikely(r != 0))
		goto bo_reserve_failure;

	r = amdgpu_bo_pin(adev->gmc.pdb0_bo, AMDGPU_GEM_DOMAIN_VRAM);
	if (r)
		goto bo_pin_failure;
	r = amdgpu_bo_kmap(adev->gmc.pdb0_bo, &adev->gmc.ptr_pdb0);
	if (r)
		goto bo_kmap_failure;

	amdgpu_bo_unreserve(adev->gmc.pdb0_bo);
	return 0;

bo_kmap_failure:
	amdgpu_bo_unpin(adev->gmc.pdb0_bo);
bo_pin_failure:
	amdgpu_bo_unreserve(adev->gmc.pdb0_bo);
bo_reserve_failure:
	amdgpu_bo_unref(&adev->gmc.pdb0_bo);
	return r;
}

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

	switch (bo->tbo.mem.mem_type) {
	case TTM_PL_TT:
		*addr = bo->tbo.ttm->dma_address[0];
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

/*
 * amdgpu_gmc_pd_addr - return the address of the root directory
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
	int idx;

	if (!drm_dev_enter(&adev->ddev, &idx))
		return 0;

	/*
	 * The following is for PTE only. GART does not have PDEs.
	*/
	value = addr & 0x0000FFFFFFFFF000ULL;
	value |= flags;
	writeq(value, ptr + (gpu_page_idx * 8));

	drm_dev_exit(idx);

	return 0;
}

/**
 * amdgpu_gmc_agp_addr - return the address in the AGP address space
 *
 * @bo: TTM BO which needs the address, must be in GTT domain
 *
 * Tries to figure out how to access the BO through the AGP aperture. Returns
 * AMDGPU_BO_INVALID_OFFSET if that is not possible.
 */
uint64_t amdgpu_gmc_agp_addr(struct ttm_buffer_object *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->bdev);

	if (bo->ttm->num_pages != 1 || bo->ttm->caching == ttm_cached)
		return AMDGPU_BO_INVALID_OFFSET;

	if (bo->ttm->dma_address[0] + PAGE_SIZE >= adev->gmc.agp_size)
		return AMDGPU_BO_INVALID_OFFSET;

	return adev->gmc.agp_start + bo->ttm->dma_address[0];
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

/** amdgpu_gmc_sysvm_location - place vram and gart in sysvm aperture
 *
 * @adev: amdgpu device structure holding all necessary information
 * @mc: memory controller structure holding memory information
 *
 * This function is only used if use GART for FB translation. In such
 * case, we use sysvm aperture (vmid0 page tables) for both vram
 * and gart (aka system memory) access.
 *
 * GPUVM (and our organization of vmid0 page tables) require sysvm
 * aperture to be placed at a location aligned with 8 times of native
 * page size. For example, if vm_context0_cntl.page_table_block_size
 * is 12, then native page size is 8G (2M*2^12), sysvm should start
 * with a 64G aligned address. For simplicity, we just put sysvm at
 * address 0. So vram start at address 0 and gart is right after vram.
 */
void amdgpu_gmc_sysvm_location(struct amdgpu_device *adev, struct amdgpu_gmc *mc)
{
	u64 hive_vram_start = 0;
	u64 hive_vram_end = mc->xgmi.node_segment_size * mc->xgmi.num_physical_nodes - 1;
	mc->vram_start = mc->xgmi.node_segment_size * mc->xgmi.physical_node_id;
	mc->vram_end = mc->vram_start + mc->xgmi.node_segment_size - 1;
	mc->gart_start = hive_vram_end + 1;
	mc->gart_end = mc->gart_start + mc->gart_size - 1;
	mc->fb_start = hive_vram_start;
	mc->fb_end = hive_vram_end;
	dev_info(adev->dev, "VRAM: %lluM 0x%016llX - 0x%016llX (%lluM used)\n",
			mc->mc_vram_size >> 20, mc->vram_start,
			mc->vram_end, mc->real_vram_size >> 20);
	dev_info(adev->dev, "GART: %lluM 0x%016llX - 0x%016llX\n",
			mc->gart_size >> 20, mc->gart_start, mc->gart_end);
}

/**
 * amdgpu_gmc_gart_location - try to find GART location
 *
 * @adev: amdgpu device structure holding all necessary information
 * @mc: memory controller structure holding memory information
 *
 * Function will place try to place GART before or after VRAM.
 * If GART size is bigger than space left then we ajust GART size.
 * Thus function will never fails.
 */
void amdgpu_gmc_gart_location(struct amdgpu_device *adev, struct amdgpu_gmc *mc)
{
	const uint64_t four_gb = 0x100000000ULL;
	u64 size_af, size_bf;
	/*To avoid the hole, limit the max mc address to AMDGPU_GMC_HOLE_START*/
	u64 max_mc_address = min(adev->gmc.mc_mask, AMDGPU_GMC_HOLE_START - 1);

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
 * amdgpu_gmc_fault_key - get hask key from vm fault address and pasid
 *
 * @addr: 48 bit physical address, page aligned (36 significant bits)
 * @pasid: 16 bit process address space identifier
 */
static inline uint64_t amdgpu_gmc_fault_key(uint64_t addr, uint16_t pasid)
{
	return addr << 4 | pasid;
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
	uint64_t stamp, key = amdgpu_gmc_fault_key(addr, pasid);
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

		if (atomic64_read(&fault->key) == key)
			return true;

		tmp = fault->timestamp;
		fault = &gmc->fault_ring[fault->next];

		/* Check if the entry was reused */
		if (fault->timestamp >= tmp)
			break;
	}

	/* Add the fault to the ring */
	fault = &gmc->fault_ring[gmc->last_fault];
	atomic64_set(&fault->key, key);
	fault->timestamp = timestamp;

	/* And update the hash */
	fault->next = gmc->fault_hash[hash].idx;
	gmc->fault_hash[hash].idx = gmc->last_fault++;
	return false;
}

/**
 * amdgpu_gmc_filter_faults_remove - remove address from VM faults filter
 *
 * @adev: amdgpu device structure
 * @addr: address of the VM fault
 * @pasid: PASID of the process causing the fault
 *
 * Remove the address from fault filter, then future vm fault on this address
 * will pass to retry fault handler to recover.
 */
void amdgpu_gmc_filter_faults_remove(struct amdgpu_device *adev, uint64_t addr,
				     uint16_t pasid)
{
	struct amdgpu_gmc *gmc = &adev->gmc;
	uint64_t key = amdgpu_gmc_fault_key(addr, pasid);
	struct amdgpu_gmc_fault *fault;
	uint32_t hash;
	uint64_t tmp;

	hash = hash_64(key, AMDGPU_GMC_FAULT_HASH_ORDER);
	fault = &gmc->fault_ring[gmc->fault_hash[hash].idx];
	do {
		if (atomic64_cmpxchg(&fault->key, key, 0) == key)
			break;

		tmp = fault->timestamp;
		fault = &gmc->fault_ring[fault->next];
	} while (fault->timestamp < tmp);
}

int amdgpu_gmc_ras_late_init(struct amdgpu_device *adev)
{
	int r;

	if (adev->umc.ras_funcs &&
	    adev->umc.ras_funcs->ras_late_init) {
		r = adev->umc.ras_funcs->ras_late_init(adev);
		if (r)
			return r;
	}

	if (adev->mmhub.ras_funcs &&
	    adev->mmhub.ras_funcs->ras_late_init) {
		r = adev->mmhub.ras_funcs->ras_late_init(adev);
		if (r)
			return r;
	}

	if (!adev->gmc.xgmi.connected_to_cpu)
		adev->gmc.xgmi.ras_funcs = &xgmi_ras_funcs;

	if (adev->gmc.xgmi.ras_funcs &&
	    adev->gmc.xgmi.ras_funcs->ras_late_init) {
		r = adev->gmc.xgmi.ras_funcs->ras_late_init(adev);
		if (r)
			return r;
	}

	if (adev->hdp.ras_funcs &&
	    adev->hdp.ras_funcs->ras_late_init) {
		r = adev->hdp.ras_funcs->ras_late_init(adev);
		if (r)
			return r;
	}

	return 0;
}

void amdgpu_gmc_ras_fini(struct amdgpu_device *adev)
{
	if (adev->umc.ras_funcs &&
	    adev->umc.ras_funcs->ras_fini)
		adev->umc.ras_funcs->ras_fini(adev);

	if (adev->mmhub.ras_funcs &&
	    adev->mmhub.ras_funcs->ras_fini)
		adev->mmhub.ras_funcs->ras_fini(adev);

	if (adev->gmc.xgmi.ras_funcs &&
	    adev->gmc.xgmi.ras_funcs->ras_fini)
		adev->gmc.xgmi.ras_funcs->ras_fini(adev);

	if (adev->hdp.ras_funcs &&
	    adev->hdp.ras_funcs->ras_fini)
		adev->hdp.ras_funcs->ras_fini(adev);
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

		if (ring == &adev->mes.ring)
			continue;

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
 * amdgpu_gmc_tmz_set -- check and set if a device supports TMZ
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
		if (amdgpu_tmz == 0) {
			adev->gmc.tmz_enabled = false;
			dev_info(adev->dev,
				 "Trusted Memory Zone (TMZ) feature disabled (cmd line)\n");
		} else {
			adev->gmc.tmz_enabled = true;
			dev_info(adev->dev,
				 "Trusted Memory Zone (TMZ) feature enabled\n");
		}
		break;
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
	case CHIP_VANGOGH:
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

/**
 * amdgpu_gmc_noretry_set -- set per asic noretry defaults
 * @adev: amdgpu_device pointer
 *
 * Set a per asic default for the no-retry parameter.
 *
 */
void amdgpu_gmc_noretry_set(struct amdgpu_device *adev)
{
	struct amdgpu_gmc *gmc = &adev->gmc;

	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_VEGA20:
	case CHIP_ARCTURUS:
	case CHIP_ALDEBARAN:
		/*
		 * noretry = 0 will cause kfd page fault tests fail
		 * for some ASICs, so set default to 1 for these ASICs.
		 */
		if (amdgpu_noretry == -1)
			gmc->noretry = 1;
		else
			gmc->noretry = amdgpu_noretry;
		break;
	case CHIP_RAVEN:
	default:
		/* Raven currently has issues with noretry
		 * regardless of what we decide for other
		 * asics, we should leave raven with
		 * noretry = 0 until we root cause the
		 * issues.
		 *
		 * default this to 0 for now, but we may want
		 * to change this in the future for certain
		 * GPUs as it can increase performance in
		 * certain cases.
		 */
		if (amdgpu_noretry == -1)
			gmc->noretry = 0;
		else
			gmc->noretry = amdgpu_noretry;
		break;
	}
}

void amdgpu_gmc_set_vm_fault_masks(struct amdgpu_device *adev, int hub_type,
				   bool enable)
{
	struct amdgpu_vmhub *hub;
	u32 tmp, reg, i;

	hub = &adev->vmhub[hub_type];
	for (i = 0; i < 16; i++) {
		reg = hub->vm_context0_cntl + hub->ctx_distance * i;

		tmp = (hub_type == AMDGPU_GFXHUB_0) ?
			RREG32_SOC15_IP(GC, reg) :
			RREG32_SOC15_IP(MMHUB, reg);

		if (enable)
			tmp |= hub->vm_cntx_cntl_vm_fault;
		else
			tmp &= ~hub->vm_cntx_cntl_vm_fault;

		(hub_type == AMDGPU_GFXHUB_0) ?
			WREG32_SOC15_IP(GC, reg, tmp) :
			WREG32_SOC15_IP(MMHUB, reg, tmp);
	}
}

void amdgpu_gmc_get_vbios_allocations(struct amdgpu_device *adev)
{
	unsigned size;

	/*
	 * TODO:
	 * Currently there is a bug where some memory client outside
	 * of the driver writes to first 8M of VRAM on S3 resume,
	 * this overrides GART which by default gets placed in first 8M and
	 * causes VM_FAULTS once GTT is accessed.
	 * Keep the stolen memory reservation until the while this is not solved.
	 */
	switch (adev->asic_type) {
	case CHIP_VEGA10:
	case CHIP_RAVEN:
	case CHIP_RENOIR:
		adev->mman.keep_stolen_vga_memory = true;
		break;
	default:
		adev->mman.keep_stolen_vga_memory = false;
		break;
	}

	if (amdgpu_sriov_vf(adev) ||
	    !amdgpu_device_ip_get_ip_block(adev, AMD_IP_BLOCK_TYPE_DCE)) {
		size = 0;
	} else {
		size = amdgpu_gmc_get_vbios_fb_size(adev);

		if (adev->mman.keep_stolen_vga_memory)
			size = max(size, (unsigned)AMDGPU_VBIOS_VGA_ALLOCATION);
	}

	/* set to 0 if the pre-OS buffer uses up most of vram */
	if ((adev->gmc.real_vram_size - size) < (8 * 1024 * 1024))
		size = 0;

	if (size > AMDGPU_VBIOS_VGA_ALLOCATION) {
		adev->mman.stolen_vga_size = AMDGPU_VBIOS_VGA_ALLOCATION;
		adev->mman.stolen_extended_size = size - adev->mman.stolen_vga_size;
	} else {
		adev->mman.stolen_vga_size = size;
		adev->mman.stolen_extended_size = 0;
	}
}

/**
 * amdgpu_gmc_init_pdb0 - initialize PDB0
 *
 * @adev: amdgpu_device pointer
 *
 * This function is only used when GART page table is used
 * for FB address translatioin. In such a case, we construct
 * a 2-level system VM page table: PDB0->PTB, to cover both
 * VRAM of the hive and system memory.
 *
 * PDB0 is static, initialized once on driver initialization.
 * The first n entries of PDB0 are used as PTE by setting
 * P bit to 1, pointing to VRAM. The n+1'th entry points
 * to a big PTB covering system memory.
 *
 */
void amdgpu_gmc_init_pdb0(struct amdgpu_device *adev)
{
	int i;
	uint64_t flags = adev->gart.gart_pte_flags; //TODO it is UC. explore NC/RW?
	/* Each PDE0 (used as PTE) covers (2^vmid0_page_table_block_size)*2M
	 */
	u64 vram_size = adev->gmc.xgmi.node_segment_size * adev->gmc.xgmi.num_physical_nodes;
	u64 pde0_page_size = (1ULL<<adev->gmc.vmid0_page_table_block_size)<<21;
	u64 vram_addr = adev->vm_manager.vram_base_offset -
		adev->gmc.xgmi.physical_node_id * adev->gmc.xgmi.node_segment_size;
	u64 vram_end = vram_addr + vram_size;
	u64 gart_ptb_gpu_pa = amdgpu_gmc_vram_pa(adev, adev->gart.bo);

	flags |= AMDGPU_PTE_VALID | AMDGPU_PTE_READABLE;
	flags |= AMDGPU_PTE_WRITEABLE;
	flags |= AMDGPU_PTE_SNOOPED;
	flags |= AMDGPU_PTE_FRAG((adev->gmc.vmid0_page_table_block_size + 9*1));
	flags |= AMDGPU_PDE_PTE;

	/* The first n PDE0 entries are used as PTE,
	 * pointing to vram
	 */
	for (i = 0; vram_addr < vram_end; i++, vram_addr += pde0_page_size)
		amdgpu_gmc_set_pte_pde(adev, adev->gmc.ptr_pdb0, i, vram_addr, flags);

	/* The n+1'th PDE0 entry points to a huge
	 * PTB who has more than 512 entries each
	 * pointing to a 4K system page
	 */
	flags = AMDGPU_PTE_VALID;
	flags |= AMDGPU_PDE_BFS(0) | AMDGPU_PTE_SNOOPED;
	/* Requires gart_ptb_gpu_pa to be 4K aligned */
	amdgpu_gmc_set_pte_pde(adev, adev->gmc.ptr_pdb0, i, gart_ptb_gpu_pa, flags);
}

/**
 * amdgpu_gmc_vram_mc2pa - calculate vram buffer's physical address from MC
 * address
 *
 * @adev: amdgpu_device pointer
 * @mc_addr: MC address of buffer
 */
uint64_t amdgpu_gmc_vram_mc2pa(struct amdgpu_device *adev, uint64_t mc_addr)
{
	return mc_addr - adev->gmc.vram_start + adev->vm_manager.vram_base_offset;
}

/**
 * amdgpu_gmc_vram_pa - calculate vram buffer object's physical address from
 * GPU's view
 *
 * @adev: amdgpu_device pointer
 * @bo: amdgpu buffer object
 */
uint64_t amdgpu_gmc_vram_pa(struct amdgpu_device *adev, struct amdgpu_bo *bo)
{
	return amdgpu_gmc_vram_mc2pa(adev, amdgpu_bo_gpu_offset(bo));
}

/**
 * amdgpu_gmc_vram_cpu_pa - calculate vram buffer object's physical address
 * from CPU's view
 *
 * @adev: amdgpu_device pointer
 * @bo: amdgpu buffer object
 */
uint64_t amdgpu_gmc_vram_cpu_pa(struct amdgpu_device *adev, struct amdgpu_bo *bo)
{
	return amdgpu_bo_gpu_offset(bo) - adev->gmc.vram_start + adev->gmc.aper_base;
}
