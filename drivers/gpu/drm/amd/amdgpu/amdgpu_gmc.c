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
#ifdef CONFIG_X86
#include <asm/hypervisor.h>
#endif

#include "amdgpu.h"
#include "amdgpu_gmc.h"
#include "amdgpu_ras.h"
#include "amdgpu_reset.h"
#include "amdgpu_xgmi.h"

#include <drm/drm_drv.h>
#include <drm/ttm/ttm_tt.h>

static const u64 four_gb = 0x100000000ULL;

bool amdgpu_gmc_is_pdb0_enabled(struct amdgpu_device *adev)
{
	return adev->gmc.xgmi.connected_to_cpu || amdgpu_virt_xgmi_migrate_enabled(adev);
}

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
	uint32_t npdes = (vram_size + (1ULL << pde0_page_shift) - 1) >> pde0_page_shift;

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

	switch (bo->tbo.resource->mem_type) {
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
	*flags = amdgpu_ttm_tt_pde_flags(bo->tbo.ttm, bo->tbo.resource);
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
 * @bo: TTM BO which needs the address, must be in GTT domain
 *
 * Tries to figure out how to access the BO through the AGP aperture. Returns
 * AMDGPU_BO_INVALID_OFFSET if that is not possible.
 */
uint64_t amdgpu_gmc_agp_addr(struct ttm_buffer_object *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->bdev);

	if (!bo->ttm)
		return AMDGPU_BO_INVALID_OFFSET;

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
	uint64_t vis_limit = (uint64_t)amdgpu_vis_vram_limit << 20;
	uint64_t limit = (uint64_t)amdgpu_vram_limit << 20;

	mc->vram_start = base;
	mc->vram_end = mc->vram_start + mc->mc_vram_size - 1;
	if (limit < mc->real_vram_size)
		mc->real_vram_size = limit;

	if (vis_limit && vis_limit < mc->visible_vram_size)
		mc->visible_vram_size = vis_limit;

	if (mc->real_vram_size < mc->visible_vram_size)
		mc->visible_vram_size = mc->real_vram_size;

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
	/* node_segment_size may not 4GB aligned on SRIOV, align up is needed. */
	mc->gart_start = ALIGN(hive_vram_end + 1, four_gb);
	mc->gart_end = mc->gart_start + mc->gart_size - 1;
	if (amdgpu_virt_xgmi_migrate_enabled(adev)) {
		/* set mc->vram_start to 0 to switch the returned GPU address of
		 * amdgpu_bo_create_reserved() from FB aperture to GART aperture.
		 */
		mc->vram_start = 0;
		mc->vram_end = mc->vram_start + mc->mc_vram_size - 1;
		mc->visible_vram_size = min(mc->visible_vram_size, mc->real_vram_size);
	} else {
		mc->fb_start = hive_vram_start;
		mc->fb_end = hive_vram_end;
	}
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
 * @gart_placement: GART placement policy with respect to VRAM
 *
 * Function will try to place GART before or after VRAM.
 * If GART size is bigger than space left then we ajust GART size.
 * Thus function will never fails.
 */
void amdgpu_gmc_gart_location(struct amdgpu_device *adev, struct amdgpu_gmc *mc,
			      enum amdgpu_gart_placement gart_placement)
{
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

	switch (gart_placement) {
	case AMDGPU_GART_PLACEMENT_HIGH:
		mc->gart_start = max_mc_address - mc->gart_size + 1;
		break;
	case AMDGPU_GART_PLACEMENT_LOW:
		mc->gart_start = 0;
		break;
	case AMDGPU_GART_PLACEMENT_BEST_FIT:
	default:
		if ((size_bf >= mc->gart_size && size_bf < size_af) ||
		    (size_af < mc->gart_size))
			mc->gart_start = 0;
		else
			mc->gart_start = max_mc_address - mc->gart_size + 1;
		break;
	}

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
 * amdgpu_gmc_set_agp_default - Set the default AGP aperture value.
 * @adev: amdgpu device structure holding all necessary information
 * @mc: memory controller structure holding memory information
 *
 * To disable the AGP aperture, you need to set the start to a larger
 * value than the end.  This function sets the default value which
 * can then be overridden using amdgpu_gmc_agp_location() if you want
 * to enable the AGP aperture on a specific chip.
 *
 */
void amdgpu_gmc_set_agp_default(struct amdgpu_device *adev,
				struct amdgpu_gmc *mc)
{
	mc->agp_start = 0xffffffffffff;
	mc->agp_end = 0;
	mc->agp_size = 0;
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
 * @ih: interrupt ring that the fault received from
 * @addr: address of the VM fault
 * @pasid: PASID of the process causing the fault
 * @timestamp: timestamp of the fault
 *
 * Returns:
 * True if the fault was filtered and should not be processed further.
 * False if the fault is a new one and needs to be handled.
 */
bool amdgpu_gmc_filter_faults(struct amdgpu_device *adev,
			      struct amdgpu_ih_ring *ih, uint64_t addr,
			      uint16_t pasid, uint64_t timestamp)
{
	struct amdgpu_gmc *gmc = &adev->gmc;
	uint64_t stamp, key = amdgpu_gmc_fault_key(addr, pasid);
	struct amdgpu_gmc_fault *fault;
	uint32_t hash;

	/* Stale retry fault if timestamp goes backward */
	if (amdgpu_ih_ts_after(timestamp, ih->processed_timestamp))
		return true;

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

		if (atomic64_read(&fault->key) == key) {
			/*
			 * if we get a fault which is already present in
			 * the fault_ring and the timestamp of
			 * the fault is after the expired timestamp,
			 * then this is a new fault that needs to be added
			 * into the fault ring.
			 */
			if (fault->timestamp_expiry != 0 &&
			    amdgpu_ih_ts_after(fault->timestamp_expiry,
					       timestamp))
				break;
			else
				return true;
		}

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
	struct amdgpu_ih_ring *ih;
	struct amdgpu_gmc_fault *fault;
	uint32_t last_wptr;
	uint64_t last_ts;
	uint32_t hash;
	uint64_t tmp;

	if (adev->irq.retry_cam_enabled)
		return;

	ih = &adev->irq.ih1;
	/* Get the WPTR of the last entry in IH ring */
	last_wptr = amdgpu_ih_get_wptr(adev, ih);
	/* Order wptr with ring data. */
	rmb();
	/* Get the timetamp of the last entry in IH ring */
	last_ts = amdgpu_ih_decode_iv_ts(adev, ih, last_wptr, -1);

	hash = hash_64(key, AMDGPU_GMC_FAULT_HASH_ORDER);
	fault = &gmc->fault_ring[gmc->fault_hash[hash].idx];
	do {
		if (atomic64_read(&fault->key) == key) {
			/*
			 * Update the timestamp when this fault
			 * expired.
			 */
			fault->timestamp_expiry = last_ts;
			break;
		}

		tmp = fault->timestamp;
		fault = &gmc->fault_ring[fault->next];
	} while (fault->timestamp < tmp);
}

int amdgpu_gmc_ras_sw_init(struct amdgpu_device *adev)
{
	int r;

	/* umc ras block */
	r = amdgpu_umc_ras_sw_init(adev);
	if (r)
		return r;

	/* mmhub ras block */
	r = amdgpu_mmhub_ras_sw_init(adev);
	if (r)
		return r;

	/* hdp ras block */
	r = amdgpu_hdp_ras_sw_init(adev);
	if (r)
		return r;

	/* mca.x ras block */
	r = amdgpu_mca_mp0_ras_sw_init(adev);
	if (r)
		return r;

	r = amdgpu_mca_mp1_ras_sw_init(adev);
	if (r)
		return r;

	r = amdgpu_mca_mpio_ras_sw_init(adev);
	if (r)
		return r;

	/* xgmi ras block */
	r = amdgpu_xgmi_ras_sw_init(adev);
	if (r)
		return r;

	return 0;
}

int amdgpu_gmc_ras_late_init(struct amdgpu_device *adev)
{
	return 0;
}

void amdgpu_gmc_ras_fini(struct amdgpu_device *adev)
{

}

	/*
	 * The latest engine allocation on gfx9/10 is:
	 * Engine 2, 3: firmware
	 * Engine 0, 1, 4~16: amdgpu ring,
	 *                    subject to change when ring number changes
	 * Engine 17: Gart flushes
	 */
#define AMDGPU_VMHUB_INV_ENG_BITMAP		0x1FFF3

int amdgpu_gmc_allocate_vm_inv_eng(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	unsigned vm_inv_engs[AMDGPU_MAX_VMHUBS] = {0};
	unsigned i;
	unsigned vmhub, inv_eng;
	struct amdgpu_ring *shared_ring;

	/* init the vm inv eng for all vmhubs */
	for_each_set_bit(i, adev->vmhubs_mask, AMDGPU_MAX_VMHUBS) {
		vm_inv_engs[i] = AMDGPU_VMHUB_INV_ENG_BITMAP;
		/* reserve engine 5 for firmware */
		if (adev->enable_mes)
			vm_inv_engs[i] &= ~(1 << 5);
		/* reserve engine 6 for uni mes */
		if (adev->enable_uni_mes)
			vm_inv_engs[i] &= ~(1 << 6);
		/* reserve mmhub engine 3 for firmware */
		if (adev->enable_umsch_mm)
			vm_inv_engs[i] &= ~(1 << 3);
	}

	for (i = 0; i < adev->num_rings; ++i) {
		ring = adev->rings[i];
		vmhub = ring->vm_hub;

		if (ring == &adev->mes.ring[0] ||
		    ring == &adev->mes.ring[1] ||
		    ring == &adev->umsch_mm.ring ||
		    ring == &adev->cper.ring_buf)
			continue;

		/* Skip if the ring is a shared ring */
		if (amdgpu_sdma_is_shared_inv_eng(adev, ring))
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
			 ring->name, ring->vm_inv_eng, ring->vm_hub);
		/* SDMA has a special packet which allows it to use the same
		 * invalidation engine for all the rings in one instance.
		 * Therefore, we do not allocate a separate VM invalidation engine
		 * for SDMA page rings. Instead, they share the VM invalidation
		 * engine with the SDMA gfx ring. This change ensures efficient
		 * resource management and avoids the issue of insufficient VM
		 * invalidation engines.
		 */
		shared_ring = amdgpu_sdma_get_shared_ring(adev, ring);
		if (shared_ring) {
			shared_ring->vm_inv_eng = ring->vm_inv_eng;
			dev_info(adev->dev, "ring %s shares VM invalidation engine %u with ring %s on hub %u\n",
					ring->name, ring->vm_inv_eng, shared_ring->name, ring->vm_hub);
			continue;
		}
	}

	return 0;
}

void amdgpu_gmc_flush_gpu_tlb(struct amdgpu_device *adev, uint32_t vmid,
			      uint32_t vmhub, uint32_t flush_type)
{
	struct amdgpu_ring *ring = adev->mman.buffer_funcs_ring;
	struct amdgpu_vmhub *hub = &adev->vmhub[vmhub];
	struct dma_fence *fence;
	struct amdgpu_job *job;
	int r;

	if (!hub->sdma_invalidation_workaround || vmid ||
	    !adev->mman.buffer_funcs_enabled || !adev->ib_pool_ready ||
	    !ring->sched.ready) {
		/*
		 * A GPU reset should flush all TLBs anyway, so no need to do
		 * this while one is ongoing.
		 */
		if (!down_read_trylock(&adev->reset_domain->sem))
			return;

		if (adev->gmc.flush_tlb_needs_extra_type_2)
			adev->gmc.gmc_funcs->flush_gpu_tlb(adev, vmid,
							   vmhub, 2);

		if (adev->gmc.flush_tlb_needs_extra_type_0 && flush_type == 2)
			adev->gmc.gmc_funcs->flush_gpu_tlb(adev, vmid,
							   vmhub, 0);

		adev->gmc.gmc_funcs->flush_gpu_tlb(adev, vmid, vmhub,
						   flush_type);
		up_read(&adev->reset_domain->sem);
		return;
	}

	/* The SDMA on Navi 1x has a bug which can theoretically result in memory
	 * corruption if an invalidation happens at the same time as an VA
	 * translation. Avoid this by doing the invalidation from the SDMA
	 * itself at least for GART.
	 */
	mutex_lock(&adev->mman.gtt_window_lock);
	r = amdgpu_job_alloc_with_ib(ring->adev, &adev->mman.high_pr,
				     AMDGPU_FENCE_OWNER_UNDEFINED,
				     16 * 4, AMDGPU_IB_POOL_IMMEDIATE,
				     &job, AMDGPU_KERNEL_JOB_ID_FLUSH_GPU_TLB);
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
	dev_err(adev->dev, "Error flushing GPU TLB using the SDMA (%d)!\n", r);
}

int amdgpu_gmc_flush_gpu_tlb_pasid(struct amdgpu_device *adev, uint16_t pasid,
				   uint32_t flush_type, bool all_hub,
				   uint32_t inst)
{
	struct amdgpu_ring *ring = &adev->gfx.kiq[inst].ring;
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[inst];
	unsigned int ndw;
	int r, cnt = 0;
	uint32_t seq;

	/*
	 * A GPU reset should flush all TLBs anyway, so no need to do
	 * this while one is ongoing.
	 */
	if (!down_read_trylock(&adev->reset_domain->sem))
		return 0;

	if (!adev->gmc.flush_pasid_uses_kiq || !ring->sched.ready) {
		if (adev->gmc.flush_tlb_needs_extra_type_2)
			adev->gmc.gmc_funcs->flush_gpu_tlb_pasid(adev, pasid,
								 2, all_hub,
								 inst);

		if (adev->gmc.flush_tlb_needs_extra_type_0 && flush_type == 2)
			adev->gmc.gmc_funcs->flush_gpu_tlb_pasid(adev, pasid,
								 0, all_hub,
								 inst);

		adev->gmc.gmc_funcs->flush_gpu_tlb_pasid(adev, pasid,
							 flush_type, all_hub,
							 inst);
		r = 0;
	} else {
		/* 2 dwords flush + 8 dwords fence */
		ndw = kiq->pmf->invalidate_tlbs_size + 8;

		if (adev->gmc.flush_tlb_needs_extra_type_2)
			ndw += kiq->pmf->invalidate_tlbs_size;

		if (adev->gmc.flush_tlb_needs_extra_type_0)
			ndw += kiq->pmf->invalidate_tlbs_size;

		spin_lock(&adev->gfx.kiq[inst].ring_lock);
		r = amdgpu_ring_alloc(ring, ndw);
		if (r) {
			spin_unlock(&adev->gfx.kiq[inst].ring_lock);
			goto error_unlock_reset;
		}
		if (adev->gmc.flush_tlb_needs_extra_type_2)
			kiq->pmf->kiq_invalidate_tlbs(ring, pasid, 2, all_hub);

		if (flush_type == 2 && adev->gmc.flush_tlb_needs_extra_type_0)
			kiq->pmf->kiq_invalidate_tlbs(ring, pasid, 0, all_hub);

		kiq->pmf->kiq_invalidate_tlbs(ring, pasid, flush_type, all_hub);
		r = amdgpu_fence_emit_polling(ring, &seq, MAX_KIQ_REG_WAIT);
		if (r) {
			amdgpu_ring_undo(ring);
			spin_unlock(&adev->gfx.kiq[inst].ring_lock);
			goto error_unlock_reset;
		}

		amdgpu_ring_commit(ring);
		spin_unlock(&adev->gfx.kiq[inst].ring_lock);

		r = amdgpu_fence_wait_polling(ring, seq, MAX_KIQ_REG_WAIT);

		might_sleep();
		while (r < 1 && cnt++ < MAX_KIQ_REG_TRY &&
		       !amdgpu_reset_pending(adev->reset_domain)) {
			msleep(MAX_KIQ_REG_BAILOUT_INTERVAL);
			r = amdgpu_fence_wait_polling(ring, seq, MAX_KIQ_REG_WAIT);
		}

		if (cnt > MAX_KIQ_REG_TRY) {
			dev_err(adev->dev, "timeout waiting for kiq fence\n");
			r = -ETIME;
		} else
			r = 0;
	}

error_unlock_reset:
	up_read(&adev->reset_domain->sem);
	return r;
}

void amdgpu_gmc_fw_reg_write_reg_wait(struct amdgpu_device *adev,
				      uint32_t reg0, uint32_t reg1,
				      uint32_t ref, uint32_t mask,
				      uint32_t xcc_inst)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq[xcc_inst];
	struct amdgpu_ring *ring = &kiq->ring;
	signed long r, cnt = 0;
	unsigned long flags;
	uint32_t seq;

	if (adev->mes.ring[0].sched.ready) {
		amdgpu_mes_reg_write_reg_wait(adev, reg0, reg1,
					      ref, mask);
		return;
	}

	spin_lock_irqsave(&kiq->ring_lock, flags);
	amdgpu_ring_alloc(ring, 32);
	amdgpu_ring_emit_reg_write_reg_wait(ring, reg0, reg1,
					    ref, mask);
	r = amdgpu_fence_emit_polling(ring, &seq, MAX_KIQ_REG_WAIT);
	if (r)
		goto failed_undo;

	amdgpu_ring_commit(ring);
	spin_unlock_irqrestore(&kiq->ring_lock, flags);

	r = amdgpu_fence_wait_polling(ring, seq, MAX_KIQ_REG_WAIT);

	/* don't wait anymore for IRQ context */
	if (r < 1 && in_interrupt())
		goto failed_kiq;

	might_sleep();
	while (r < 1 && cnt++ < MAX_KIQ_REG_TRY &&
	       !amdgpu_reset_pending(adev->reset_domain)) {

		msleep(MAX_KIQ_REG_BAILOUT_INTERVAL);
		r = amdgpu_fence_wait_polling(ring, seq, MAX_KIQ_REG_WAIT);
	}

	if (cnt > MAX_KIQ_REG_TRY)
		goto failed_kiq;

	return;

failed_undo:
	amdgpu_ring_undo(ring);
	spin_unlock_irqrestore(&kiq->ring_lock, flags);
failed_kiq:
	dev_err(adev->dev, "failed to write reg %x wait reg %x\n", reg0, reg1);
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
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	/* RAVEN */
	case IP_VERSION(9, 2, 2):
	case IP_VERSION(9, 1, 0):
	/* RENOIR looks like RAVEN */
	case IP_VERSION(9, 3, 0):
	/* GC 10.3.7 */
	case IP_VERSION(10, 3, 7):
	/* GC 11.0.1 */
	case IP_VERSION(11, 0, 1):
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
	case IP_VERSION(10, 1, 10):
	case IP_VERSION(10, 1, 1):
	case IP_VERSION(10, 1, 2):
	case IP_VERSION(10, 1, 3):
	case IP_VERSION(10, 3, 0):
	case IP_VERSION(10, 3, 2):
	case IP_VERSION(10, 3, 4):
	case IP_VERSION(10, 3, 5):
	case IP_VERSION(10, 3, 6):
	/* VANGOGH */
	case IP_VERSION(10, 3, 1):
	/* YELLOW_CARP*/
	case IP_VERSION(10, 3, 3):
	case IP_VERSION(11, 0, 4):
	case IP_VERSION(11, 5, 0):
	case IP_VERSION(11, 5, 1):
	case IP_VERSION(11, 5, 2):
	case IP_VERSION(11, 5, 3):
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
		dev_info(adev->dev,
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
	uint32_t gc_ver = amdgpu_ip_version(adev, GC_HWIP, 0);
	bool noretry_default = (gc_ver == IP_VERSION(9, 0, 1) ||
				gc_ver == IP_VERSION(9, 4, 0) ||
				gc_ver == IP_VERSION(9, 4, 1) ||
				gc_ver == IP_VERSION(9, 4, 2) ||
				gc_ver == IP_VERSION(9, 4, 3) ||
				gc_ver == IP_VERSION(9, 4, 4) ||
				gc_ver == IP_VERSION(9, 5, 0) ||
				gc_ver >= IP_VERSION(10, 3, 0));

	if (!amdgpu_sriov_xnack_support(adev))
		gmc->noretry = 1;
	else
		gmc->noretry = (amdgpu_noretry == -1) ? noretry_default : amdgpu_noretry;
}

void amdgpu_gmc_set_vm_fault_masks(struct amdgpu_device *adev, int hub_type,
				   bool enable)
{
	struct amdgpu_vmhub *hub;
	u32 tmp, reg, i;

	hub = &adev->vmhub[hub_type];
	for (i = 0; i < 16; i++) {
		reg = hub->vm_context0_cntl + hub->ctx_distance * i;

		tmp = (hub_type == AMDGPU_GFXHUB(0)) ?
			RREG32_SOC15_IP(GC, reg) :
			RREG32_SOC15_IP(MMHUB, reg);

		if (enable)
			tmp |= hub->vm_cntx_cntl_vm_fault;
		else
			tmp &= ~hub->vm_cntx_cntl_vm_fault;

		(hub_type == AMDGPU_GFXHUB(0)) ?
			WREG32_SOC15_IP(GC, reg, tmp) :
			WREG32_SOC15_IP(MMHUB, reg, tmp);
	}
}

void amdgpu_gmc_get_vbios_allocations(struct amdgpu_device *adev)
{
	unsigned size;

	/*
	 * Some ASICs need to reserve a region of video memory to avoid access
	 * from driver
	 */
	adev->mman.stolen_reserved_offset = 0;
	adev->mman.stolen_reserved_size = 0;

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
		adev->mman.keep_stolen_vga_memory = true;
		/*
		 * VEGA10 SRIOV VF with MS_HYPERV host needs some firmware reserved area.
		 */
#ifdef CONFIG_X86
		if (amdgpu_sriov_vf(adev) && hypervisor_is_type(X86_HYPER_MS_HYPERV)) {
			adev->mman.stolen_reserved_offset = 0x500000;
			adev->mman.stolen_reserved_size = 0x200000;
		}
#endif
		break;
	case CHIP_RAVEN:
	case CHIP_RENOIR:
		adev->mman.keep_stolen_vga_memory = true;
		break;
	default:
		adev->mman.keep_stolen_vga_memory = false;
		break;
	}

	if (amdgpu_sriov_vf(adev) ||
	    !amdgpu_device_has_display_hardware(adev)) {
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
	u64 vram_addr, vram_end;
	u64 gart_ptb_gpu_pa = amdgpu_gmc_vram_pa(adev, adev->gart.bo);
	int idx;

	if (!drm_dev_enter(adev_to_drm(adev), &idx))
		return;

	flags |= AMDGPU_PTE_VALID | AMDGPU_PTE_READABLE;
	flags |= AMDGPU_PTE_WRITEABLE;
	flags |= AMDGPU_PTE_SNOOPED;
	flags |= AMDGPU_PTE_FRAG((adev->gmc.vmid0_page_table_block_size + 9*1));
	flags |= AMDGPU_PDE_PTE_FLAG(adev);

	vram_addr = adev->vm_manager.vram_base_offset;
	if (!amdgpu_virt_xgmi_migrate_enabled(adev))
		vram_addr -= adev->gmc.xgmi.physical_node_id * adev->gmc.xgmi.node_segment_size;
	vram_end = vram_addr + vram_size;

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
	flags |= AMDGPU_PTE_SNOOPED | AMDGPU_PDE_BFS_FLAG(adev, 0);
	/* Requires gart_ptb_gpu_pa to be 4K aligned */
	amdgpu_gmc_set_pte_pde(adev, adev->gmc.ptr_pdb0, i, gart_ptb_gpu_pa, flags);
	drm_dev_exit(idx);
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

int amdgpu_gmc_vram_checking(struct amdgpu_device *adev)
{
	struct amdgpu_bo *vram_bo = NULL;
	uint64_t vram_gpu = 0;
	void *vram_ptr = NULL;

	int ret, size = 0x100000;
	uint8_t cptr[10];

	ret = amdgpu_bo_create_kernel(adev, size, PAGE_SIZE,
				AMDGPU_GEM_DOMAIN_VRAM,
				&vram_bo,
				&vram_gpu,
				&vram_ptr);
	if (ret)
		return ret;

	memset(vram_ptr, 0x86, size);
	memset(cptr, 0x86, 10);

	/**
	 * Check the start, the mid, and the end of the memory if the content of
	 * each byte is the pattern "0x86". If yes, we suppose the vram bo is
	 * workable.
	 *
	 * Note: If check the each byte of whole 1M bo, it will cost too many
	 * seconds, so here, we just pick up three parts for emulation.
	 */
	ret = memcmp(vram_ptr, cptr, 10);
	if (ret) {
		ret = -EIO;
		goto release_buffer;
	}

	ret = memcmp(vram_ptr + (size / 2), cptr, 10);
	if (ret) {
		ret = -EIO;
		goto release_buffer;
	}

	ret = memcmp(vram_ptr + size - 10, cptr, 10);
	if (ret) {
		ret = -EIO;
		goto release_buffer;
	}

release_buffer:
	amdgpu_bo_free_kernel(&vram_bo, &vram_gpu,
			&vram_ptr);

	return ret;
}

static const char *nps_desc[] = {
	[AMDGPU_NPS1_PARTITION_MODE] = "NPS1",
	[AMDGPU_NPS2_PARTITION_MODE] = "NPS2",
	[AMDGPU_NPS3_PARTITION_MODE] = "NPS3",
	[AMDGPU_NPS4_PARTITION_MODE] = "NPS4",
	[AMDGPU_NPS6_PARTITION_MODE] = "NPS6",
	[AMDGPU_NPS8_PARTITION_MODE] = "NPS8",
};

static ssize_t available_memory_partition_show(struct device *dev,
					       struct device_attribute *addr,
					       char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	int size = 0, mode;
	char *sep = "";

	for_each_inst(mode, adev->gmc.supported_nps_modes) {
		size += sysfs_emit_at(buf, size, "%s%s", sep, nps_desc[mode]);
		sep = ", ";
	}
	size += sysfs_emit_at(buf, size, "\n");

	return size;
}

static ssize_t current_memory_partition_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	enum amdgpu_memory_partition mode;
	struct amdgpu_hive_info *hive;
	int i;

	mode = UNKNOWN_MEMORY_PARTITION_MODE;
	for_each_inst(i, adev->gmc.supported_nps_modes) {
		if (!strncasecmp(nps_desc[i], buf, strlen(nps_desc[i]))) {
			mode = i;
			break;
		}
	}

	if (mode == UNKNOWN_MEMORY_PARTITION_MODE)
		return -EINVAL;

	if (mode == adev->gmc.gmc_funcs->query_mem_partition_mode(adev)) {
		dev_info(
			adev->dev,
			"requested NPS mode is same as current NPS mode, skipping\n");
		return count;
	}

	/* If device is part of hive, all devices in the hive should request the
	 * same mode. Hence store the requested mode in hive.
	 */
	hive = amdgpu_get_xgmi_hive(adev);
	if (hive) {
		atomic_set(&hive->requested_nps_mode, mode);
		amdgpu_put_xgmi_hive(hive);
	} else {
		adev->gmc.requested_nps_mode = mode;
	}

	dev_info(
		adev->dev,
		"NPS mode change requested, please remove and reload the driver\n");

	return count;
}

static ssize_t current_memory_partition_show(
	struct device *dev, struct device_attribute *addr, char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	enum amdgpu_memory_partition mode;

	/* Only minimal precaution taken to reject requests while in reset */
	if (amdgpu_in_reset(adev))
		return -EPERM;

	mode = adev->gmc.gmc_funcs->query_mem_partition_mode(adev);
	if ((mode >= ARRAY_SIZE(nps_desc)) ||
	    (BIT(mode) & AMDGPU_ALL_NPS_MASK) != BIT(mode))
		return sysfs_emit(buf, "UNKNOWN\n");

	return sysfs_emit(buf, "%s\n", nps_desc[mode]);
}

static DEVICE_ATTR_RW(current_memory_partition);
static DEVICE_ATTR_RO(available_memory_partition);

int amdgpu_gmc_sysfs_init(struct amdgpu_device *adev)
{
	bool nps_switch_support;
	int r = 0;

	if (!adev->gmc.gmc_funcs->query_mem_partition_mode)
		return 0;

	nps_switch_support = (hweight32(adev->gmc.supported_nps_modes &
					AMDGPU_ALL_NPS_MASK) > 1);
	if (!nps_switch_support)
		dev_attr_current_memory_partition.attr.mode &=
			~(S_IWUSR | S_IWGRP | S_IWOTH);
	else
		r = device_create_file(adev->dev,
				       &dev_attr_available_memory_partition);

	if (r)
		return r;

	return device_create_file(adev->dev,
				  &dev_attr_current_memory_partition);
}

void amdgpu_gmc_sysfs_fini(struct amdgpu_device *adev)
{
	if (!adev->gmc.gmc_funcs->query_mem_partition_mode)
		return;

	device_remove_file(adev->dev, &dev_attr_current_memory_partition);
	device_remove_file(adev->dev, &dev_attr_available_memory_partition);
}

int amdgpu_gmc_get_nps_memranges(struct amdgpu_device *adev,
				 struct amdgpu_mem_partition_info *mem_ranges,
				 uint8_t *exp_ranges)
{
	struct amdgpu_gmc_memrange *ranges;
	int range_cnt, ret, i, j;
	uint32_t nps_type;
	bool refresh;

	if (!mem_ranges || !exp_ranges)
		return -EINVAL;

	refresh = (adev->init_lvl->level != AMDGPU_INIT_LEVEL_MINIMAL_XGMI) &&
		  (adev->gmc.reset_flags & AMDGPU_GMC_INIT_RESET_NPS);
	ret = amdgpu_discovery_get_nps_info(adev, &nps_type, &ranges,
					    &range_cnt, refresh);

	if (ret)
		return ret;

	/* TODO: For now, expect ranges and partition count to be the same.
	 * Adjust if there are holes expected in any NPS domain.
	 */
	if (*exp_ranges && (range_cnt != *exp_ranges)) {
		dev_warn(
			adev->dev,
			"NPS config mismatch - expected ranges: %d discovery - nps mode: %d, nps ranges: %d",
			*exp_ranges, nps_type, range_cnt);
		ret = -EINVAL;
		goto err;
	}

	for (i = 0; i < range_cnt; ++i) {
		if (ranges[i].base_address >= ranges[i].limit_address) {
			dev_warn(
				adev->dev,
				"Invalid NPS range - nps mode: %d, range[%d]: base: %llx limit: %llx",
				nps_type, i, ranges[i].base_address,
				ranges[i].limit_address);
			ret = -EINVAL;
			goto err;
		}

		/* Check for overlaps, not expecting any now */
		for (j = i - 1; j >= 0; j--) {
			if (max(ranges[j].base_address,
				ranges[i].base_address) <=
			    min(ranges[j].limit_address,
				ranges[i].limit_address)) {
				dev_warn(
					adev->dev,
					"overlapping ranges detected [ %llx - %llx ] | [%llx - %llx]",
					ranges[j].base_address,
					ranges[j].limit_address,
					ranges[i].base_address,
					ranges[i].limit_address);
				ret = -EINVAL;
				goto err;
			}
		}

		mem_ranges[i].range.fpfn =
			(ranges[i].base_address -
			 adev->vm_manager.vram_base_offset) >>
			AMDGPU_GPU_PAGE_SHIFT;
		mem_ranges[i].range.lpfn =
			(ranges[i].limit_address -
			 adev->vm_manager.vram_base_offset) >>
			AMDGPU_GPU_PAGE_SHIFT;
		mem_ranges[i].size =
			ranges[i].limit_address - ranges[i].base_address + 1;
	}

	if (!*exp_ranges)
		*exp_ranges = range_cnt;
err:
	kfree(ranges);

	return ret;
}

int amdgpu_gmc_request_memory_partition(struct amdgpu_device *adev,
					int nps_mode)
{
	/* Not supported on VF devices and APUs */
	if (amdgpu_sriov_vf(adev) || (adev->flags & AMD_IS_APU))
		return -EOPNOTSUPP;

	if (!adev->psp.funcs) {
		dev_err(adev->dev,
			"PSP interface not available for nps mode change request");
		return -EINVAL;
	}

	return psp_memory_partition(&adev->psp, nps_mode);
}

static inline bool amdgpu_gmc_need_nps_switch_req(struct amdgpu_device *adev,
						  int req_nps_mode,
						  int cur_nps_mode)
{
	return (((BIT(req_nps_mode) & adev->gmc.supported_nps_modes) ==
			BIT(req_nps_mode)) &&
		req_nps_mode != cur_nps_mode);
}

void amdgpu_gmc_prepare_nps_mode_change(struct amdgpu_device *adev)
{
	int req_nps_mode, cur_nps_mode, r;
	struct amdgpu_hive_info *hive;

	if (amdgpu_sriov_vf(adev) || !adev->gmc.supported_nps_modes ||
	    !adev->gmc.gmc_funcs->request_mem_partition_mode)
		return;

	cur_nps_mode = adev->gmc.gmc_funcs->query_mem_partition_mode(adev);
	hive = amdgpu_get_xgmi_hive(adev);
	if (hive) {
		req_nps_mode = atomic_read(&hive->requested_nps_mode);
		if (!amdgpu_gmc_need_nps_switch_req(adev, req_nps_mode,
						    cur_nps_mode)) {
			amdgpu_put_xgmi_hive(hive);
			return;
		}
		r = amdgpu_xgmi_request_nps_change(adev, hive, req_nps_mode);
		amdgpu_put_xgmi_hive(hive);
		goto out;
	}

	req_nps_mode = adev->gmc.requested_nps_mode;
	if (!amdgpu_gmc_need_nps_switch_req(adev, req_nps_mode, cur_nps_mode))
		return;

	/* even if this fails, we should let driver unload w/o blocking */
	r = adev->gmc.gmc_funcs->request_mem_partition_mode(adev, req_nps_mode);
out:
	if (r)
		dev_err(adev->dev, "NPS mode change request failed\n");
	else
		dev_info(
			adev->dev,
			"NPS mode change request done, reload driver to complete the change\n");
}

bool amdgpu_gmc_need_reset_on_init(struct amdgpu_device *adev)
{
	if (adev->gmc.gmc_funcs->need_reset_on_init)
		return adev->gmc.gmc_funcs->need_reset_on_init(adev);

	return false;
}

enum amdgpu_memory_partition
amdgpu_gmc_get_vf_memory_partition(struct amdgpu_device *adev)
{
	switch (adev->gmc.num_mem_partitions) {
	case 0:
		return UNKNOWN_MEMORY_PARTITION_MODE;
	case 1:
		return AMDGPU_NPS1_PARTITION_MODE;
	case 2:
		return AMDGPU_NPS2_PARTITION_MODE;
	case 4:
		return AMDGPU_NPS4_PARTITION_MODE;
	case 8:
		return AMDGPU_NPS8_PARTITION_MODE;
	default:
		return AMDGPU_NPS1_PARTITION_MODE;
	}
}

enum amdgpu_memory_partition
amdgpu_gmc_get_memory_partition(struct amdgpu_device *adev, u32 *supp_modes)
{
	enum amdgpu_memory_partition mode = UNKNOWN_MEMORY_PARTITION_MODE;

	if (adev->nbio.funcs &&
	    adev->nbio.funcs->get_memory_partition_mode)
		mode = adev->nbio.funcs->get_memory_partition_mode(adev,
								   supp_modes);
	else
		dev_warn(adev->dev, "memory partition mode query is not supported\n");

	return mode;
}

enum amdgpu_memory_partition
amdgpu_gmc_query_memory_partition(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev))
		return amdgpu_gmc_get_vf_memory_partition(adev);
	else
		return amdgpu_gmc_get_memory_partition(adev, NULL);
}

static bool amdgpu_gmc_validate_partition_info(struct amdgpu_device *adev)
{
	enum amdgpu_memory_partition mode;
	u32 supp_modes;
	bool valid;

	mode = amdgpu_gmc_get_memory_partition(adev, &supp_modes);

	/* Mode detected by hardware not present in supported modes */
	if ((mode != UNKNOWN_MEMORY_PARTITION_MODE) &&
	    !(BIT(mode - 1) & supp_modes))
		return false;

	switch (mode) {
	case UNKNOWN_MEMORY_PARTITION_MODE:
	case AMDGPU_NPS1_PARTITION_MODE:
		valid = (adev->gmc.num_mem_partitions == 1);
		break;
	case AMDGPU_NPS2_PARTITION_MODE:
		valid = (adev->gmc.num_mem_partitions == 2);
		break;
	case AMDGPU_NPS4_PARTITION_MODE:
		valid = (adev->gmc.num_mem_partitions == 3 ||
			 adev->gmc.num_mem_partitions == 4);
		break;
	case AMDGPU_NPS8_PARTITION_MODE:
		valid = (adev->gmc.num_mem_partitions == 8);
		break;
	default:
		valid = false;
	}

	return valid;
}

static bool amdgpu_gmc_is_node_present(int *node_ids, int num_ids, int nid)
{
	int i;

	/* Check if node with id 'nid' is present in 'node_ids' array */
	for (i = 0; i < num_ids; ++i)
		if (node_ids[i] == nid)
			return true;

	return false;
}

static void
amdgpu_gmc_init_acpi_mem_ranges(struct amdgpu_device *adev,
				struct amdgpu_mem_partition_info *mem_ranges)
{
	struct amdgpu_numa_info numa_info;
	int node_ids[AMDGPU_MAX_MEM_RANGES];
	int num_ranges = 0, ret;
	int num_xcc, xcc_id;
	uint32_t xcc_mask;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	xcc_mask = (1U << num_xcc) - 1;

	for_each_inst(xcc_id, xcc_mask)	{
		ret = amdgpu_acpi_get_mem_info(adev, xcc_id, &numa_info);
		if (ret)
			continue;

		if (numa_info.nid == NUMA_NO_NODE) {
			mem_ranges[0].size = numa_info.size;
			mem_ranges[0].numa.node = numa_info.nid;
			num_ranges = 1;
			break;
		}

		if (amdgpu_gmc_is_node_present(node_ids, num_ranges,
					     numa_info.nid))
			continue;

		node_ids[num_ranges] = numa_info.nid;
		mem_ranges[num_ranges].numa.node = numa_info.nid;
		mem_ranges[num_ranges].size = numa_info.size;
		++num_ranges;
	}

	adev->gmc.num_mem_partitions = num_ranges;
}

void amdgpu_gmc_init_sw_mem_ranges(struct amdgpu_device *adev,
				   struct amdgpu_mem_partition_info *mem_ranges)
{
	enum amdgpu_memory_partition mode;
	u32 start_addr = 0, size;
	int i, r, l;

	mode = amdgpu_gmc_query_memory_partition(adev);

	switch (mode) {
	case UNKNOWN_MEMORY_PARTITION_MODE:
		adev->gmc.num_mem_partitions = 0;
		break;
	case AMDGPU_NPS1_PARTITION_MODE:
		adev->gmc.num_mem_partitions = 1;
		break;
	case AMDGPU_NPS2_PARTITION_MODE:
		adev->gmc.num_mem_partitions = 2;
		break;
	case AMDGPU_NPS4_PARTITION_MODE:
		if (adev->flags & AMD_IS_APU)
			adev->gmc.num_mem_partitions = 3;
		else
			adev->gmc.num_mem_partitions = 4;
		break;
	case AMDGPU_NPS8_PARTITION_MODE:
		adev->gmc.num_mem_partitions = 8;
		break;
	default:
		adev->gmc.num_mem_partitions = 1;
		break;
	}

	/* Use NPS range info, if populated */
	r = amdgpu_gmc_get_nps_memranges(adev, mem_ranges,
					 &adev->gmc.num_mem_partitions);
	if (!r) {
		l = 0;
		for (i = 1; i < adev->gmc.num_mem_partitions; ++i) {
			if (mem_ranges[i].range.lpfn >
			    mem_ranges[i - 1].range.lpfn)
				l = i;
		}

	} else {
		if (!adev->gmc.num_mem_partitions) {
			dev_warn(adev->dev,
				 "Not able to detect NPS mode, fall back to NPS1\n");
			adev->gmc.num_mem_partitions = 1;
		}
		/* Fallback to sw based calculation */
		size = (adev->gmc.real_vram_size + SZ_16M) >> AMDGPU_GPU_PAGE_SHIFT;
		size /= adev->gmc.num_mem_partitions;

		for (i = 0; i < adev->gmc.num_mem_partitions; ++i) {
			mem_ranges[i].range.fpfn = start_addr;
			mem_ranges[i].size =
				((u64)size << AMDGPU_GPU_PAGE_SHIFT);
			mem_ranges[i].range.lpfn = start_addr + size - 1;
			start_addr += size;
		}

		l = adev->gmc.num_mem_partitions - 1;
	}

	/* Adjust the last one */
	mem_ranges[l].range.lpfn =
		(adev->gmc.real_vram_size >> AMDGPU_GPU_PAGE_SHIFT) - 1;
	mem_ranges[l].size =
		adev->gmc.real_vram_size -
		((u64)mem_ranges[l].range.fpfn << AMDGPU_GPU_PAGE_SHIFT);
}

int amdgpu_gmc_init_mem_ranges(struct amdgpu_device *adev)
{
	bool valid;

	adev->gmc.mem_partitions = kcalloc(AMDGPU_MAX_MEM_RANGES,
					   sizeof(struct amdgpu_mem_partition_info),
					   GFP_KERNEL);
	if (!adev->gmc.mem_partitions)
		return -ENOMEM;

	if (adev->gmc.is_app_apu)
		amdgpu_gmc_init_acpi_mem_ranges(adev, adev->gmc.mem_partitions);
	else
		amdgpu_gmc_init_sw_mem_ranges(adev, adev->gmc.mem_partitions);

	if (amdgpu_sriov_vf(adev))
		valid = true;
	else
		valid = amdgpu_gmc_validate_partition_info(adev);
	if (!valid) {
		/* TODO: handle invalid case */
		dev_warn(adev->dev,
			 "Mem ranges not matching with hardware config\n");
	}

	return 0;
}
