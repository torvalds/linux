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
#ifndef __AMDGPU_GMC_H__
#define __AMDGPU_GMC_H__

#include <linux/types.h>

#include "amdgpu_irq.h"

/* VA hole for 48bit addresses on Vega10 */
#define AMDGPU_GMC_HOLE_START	0x0000800000000000ULL
#define AMDGPU_GMC_HOLE_END	0xffff800000000000ULL

/*
 * Hardware is programmed as if the hole doesn't exists with start and end
 * address values.
 *
 * This mask is used to remove the upper 16bits of the VA and so come up with
 * the linear addr value.
 */
#define AMDGPU_GMC_HOLE_MASK	0x0000ffffffffffffULL

/*
 * Ring size as power of two for the log of recent faults.
 */
#define AMDGPU_GMC_FAULT_RING_ORDER	8
#define AMDGPU_GMC_FAULT_RING_SIZE	(1 << AMDGPU_GMC_FAULT_RING_ORDER)

/*
 * Hash size as power of two for the log of recent faults
 */
#define AMDGPU_GMC_FAULT_HASH_ORDER	8
#define AMDGPU_GMC_FAULT_HASH_SIZE	(1 << AMDGPU_GMC_FAULT_HASH_ORDER)

/*
 * Number of IH timestamp ticks until a fault is considered handled
 */
#define AMDGPU_GMC_FAULT_TIMEOUT	5000ULL

struct firmware;

/*
 * GMC page fault information
 */
struct amdgpu_gmc_fault {
	uint64_t	timestamp:48;
	uint64_t	next:AMDGPU_GMC_FAULT_RING_ORDER;
	atomic64_t	key;
};

/*
 * VMHUB structures, functions & helpers
 */
struct amdgpu_vmhub_funcs {
	void (*print_l2_protection_fault_status)(struct amdgpu_device *adev,
						 uint32_t status);
	uint32_t (*get_invalidate_req)(unsigned int vmid, uint32_t flush_type);
};

struct amdgpu_vmhub {
	uint32_t	ctx0_ptb_addr_lo32;
	uint32_t	ctx0_ptb_addr_hi32;
	uint32_t	vm_inv_eng0_sem;
	uint32_t	vm_inv_eng0_req;
	uint32_t	vm_inv_eng0_ack;
	uint32_t	vm_context0_cntl;
	uint32_t	vm_l2_pro_fault_status;
	uint32_t	vm_l2_pro_fault_cntl;

	/*
	 * store the register distances between two continuous context domain
	 * and invalidation engine.
	 */
	uint32_t	ctx_distance;
	uint32_t	ctx_addr_distance; /* include LO32/HI32 */
	uint32_t	eng_distance;
	uint32_t	eng_addr_distance; /* include LO32/HI32 */

	uint32_t	vm_cntx_cntl_vm_fault;

	const struct amdgpu_vmhub_funcs *vmhub_funcs;
};

/*
 * GPU MC structures, functions & helpers
 */
struct amdgpu_gmc_funcs {
	/* flush the vm tlb via mmio */
	void (*flush_gpu_tlb)(struct amdgpu_device *adev, uint32_t vmid,
				uint32_t vmhub, uint32_t flush_type);
	/* flush the vm tlb via pasid */
	int (*flush_gpu_tlb_pasid)(struct amdgpu_device *adev, uint16_t pasid,
					uint32_t flush_type, bool all_hub);
	/* flush the vm tlb via ring */
	uint64_t (*emit_flush_gpu_tlb)(struct amdgpu_ring *ring, unsigned vmid,
				       uint64_t pd_addr);
	/* Change the VMID -> PASID mapping */
	void (*emit_pasid_mapping)(struct amdgpu_ring *ring, unsigned vmid,
				   unsigned pasid);
	/* enable/disable PRT support */
	void (*set_prt)(struct amdgpu_device *adev, bool enable);
	/* map mtype to hardware flags */
	uint64_t (*map_mtype)(struct amdgpu_device *adev, uint32_t flags);
	/* get the pde for a given mc addr */
	void (*get_vm_pde)(struct amdgpu_device *adev, int level,
			   u64 *dst, u64 *flags);
	/* get the pte flags to use for a BO VA mapping */
	void (*get_vm_pte)(struct amdgpu_device *adev,
			   struct amdgpu_bo_va_mapping *mapping,
			   uint64_t *flags);
	/* get the amount of memory used by the vbios for pre-OS console */
	unsigned int (*get_vbios_fb_size)(struct amdgpu_device *adev);
};

struct amdgpu_xgmi_ras_funcs {
	int (*ras_late_init)(struct amdgpu_device *adev);
	void (*ras_fini)(struct amdgpu_device *adev);
	int (*query_ras_error_count)(struct amdgpu_device *adev,
				     void *ras_error_status);
	void (*reset_ras_error_count)(struct amdgpu_device *adev);
};

struct amdgpu_xgmi {
	/* from psp */
	u64 node_id;
	u64 hive_id;
	/* fixed per family */
	u64 node_segment_size;
	/* physical node (0-3) */
	unsigned physical_node_id;
	/* number of nodes (0-4) */
	unsigned num_physical_nodes;
	/* gpu list in the same hive */
	struct list_head head;
	bool supported;
	struct ras_common_if *ras_if;
	bool connected_to_cpu;
	bool pending_reset;
	const struct amdgpu_xgmi_ras_funcs *ras_funcs;
};

struct amdgpu_gmc {
	/* FB's physical address in MMIO space (for CPU to
	 * map FB). This is different compared to the agp/
	 * gart/vram_start/end field as the later is from
	 * GPU's view and aper_base is from CPU's view.
	 */
	resource_size_t		aper_size;
	resource_size_t		aper_base;
	/* for some chips with <= 32MB we need to lie
	 * about vram size near mc fb location */
	u64			mc_vram_size;
	u64			visible_vram_size;
	/* AGP aperture start and end in MC address space
	 * Driver find a hole in the MC address space
	 * to place AGP by setting MC_VM_AGP_BOT/TOP registers
	 * Under VMID0, logical address == MC address. AGP
	 * aperture maps to physical bus or IOVA addressed.
	 * AGP aperture is used to simulate FB in ZFB case.
	 * AGP aperture is also used for page table in system
	 * memory (mainly for APU).
	 *
	 */
	u64			agp_size;
	u64			agp_start;
	u64			agp_end;
	/* GART aperture start and end in MC address space
	 * Driver find a hole in the MC address space
	 * to place GART by setting VM_CONTEXT0_PAGE_TABLE_START/END_ADDR
	 * registers
	 * Under VMID0, logical address inside GART aperture will
	 * be translated through gpuvm gart page table to access
	 * paged system memory
	 */
	u64			gart_size;
	u64			gart_start;
	u64			gart_end;
	/* Frame buffer aperture of this GPU device. Different from
	 * fb_start (see below), this only covers the local GPU device.
	 * If driver uses FB aperture to access FB, driver get fb_start from
	 * MC_VM_FB_LOCATION_BASE (set by vbios) and calculate vram_start
	 * of this local device by adding an offset inside the XGMI hive.
	 * If driver uses GART table for VMID0 FB access, driver finds a hole in
	 * VMID0's virtual address space to place the SYSVM aperture inside
	 * which the first part is vram and the second part is gart (covering
	 * system ram).
	 */
	u64			vram_start;
	u64			vram_end;
	/* FB region , it's same as local vram region in single GPU, in XGMI
	 * configuration, this region covers all GPUs in the same hive ,
	 * each GPU in the hive has the same view of this FB region .
	 * GPU0's vram starts at offset (0 * segment size) ,
	 * GPU1 starts at offset (1 * segment size), etc.
	 */
	u64			fb_start;
	u64			fb_end;
	unsigned		vram_width;
	u64			real_vram_size;
	int			vram_mtrr;
	u64                     mc_mask;
	const struct firmware   *fw;	/* MC firmware */
	uint32_t                fw_version;
	struct amdgpu_irq_src	vm_fault;
	uint32_t		vram_type;
	uint8_t			vram_vendor;
	uint32_t                srbm_soft_reset;
	bool			prt_warning;
	uint32_t		sdpif_register;
	/* apertures */
	u64			shared_aperture_start;
	u64			shared_aperture_end;
	u64			private_aperture_start;
	u64			private_aperture_end;
	/* protects concurrent invalidation */
	spinlock_t		invalidate_lock;
	bool			translate_further;
	struct kfd_vm_fault_info *vm_fault_info;
	atomic_t		vm_fault_info_updated;

	struct amdgpu_gmc_fault	fault_ring[AMDGPU_GMC_FAULT_RING_SIZE];
	struct {
		uint64_t	idx:AMDGPU_GMC_FAULT_RING_ORDER;
	} fault_hash[AMDGPU_GMC_FAULT_HASH_SIZE];
	uint64_t		last_fault:AMDGPU_GMC_FAULT_RING_ORDER;

	bool tmz_enabled;

	const struct amdgpu_gmc_funcs	*gmc_funcs;

	struct amdgpu_xgmi xgmi;
	struct amdgpu_irq_src	ecc_irq;
	int noretry;

	uint32_t	vmid0_page_table_block_size;
	uint32_t	vmid0_page_table_depth;
	struct amdgpu_bo		*pdb0_bo;
	/* CPU kmapped address of pdb0*/
	void				*ptr_pdb0;
};

#define amdgpu_gmc_flush_gpu_tlb(adev, vmid, vmhub, type) ((adev)->gmc.gmc_funcs->flush_gpu_tlb((adev), (vmid), (vmhub), (type)))
#define amdgpu_gmc_flush_gpu_tlb_pasid(adev, pasid, type, allhub) \
	((adev)->gmc.gmc_funcs->flush_gpu_tlb_pasid \
	((adev), (pasid), (type), (allhub)))
#define amdgpu_gmc_emit_flush_gpu_tlb(r, vmid, addr) (r)->adev->gmc.gmc_funcs->emit_flush_gpu_tlb((r), (vmid), (addr))
#define amdgpu_gmc_emit_pasid_mapping(r, vmid, pasid) (r)->adev->gmc.gmc_funcs->emit_pasid_mapping((r), (vmid), (pasid))
#define amdgpu_gmc_map_mtype(adev, flags) (adev)->gmc.gmc_funcs->map_mtype((adev),(flags))
#define amdgpu_gmc_get_vm_pde(adev, level, dst, flags) (adev)->gmc.gmc_funcs->get_vm_pde((adev), (level), (dst), (flags))
#define amdgpu_gmc_get_vm_pte(adev, mapping, flags) (adev)->gmc.gmc_funcs->get_vm_pte((adev), (mapping), (flags))
#define amdgpu_gmc_get_vbios_fb_size(adev) (adev)->gmc.gmc_funcs->get_vbios_fb_size((adev))

/**
 * amdgpu_gmc_vram_full_visible - Check if full VRAM is visible through the BAR
 *
 * @adev: amdgpu_device pointer
 *
 * Returns:
 * True if full VRAM is visible through the BAR
 */
static inline bool amdgpu_gmc_vram_full_visible(struct amdgpu_gmc *gmc)
{
	WARN_ON(gmc->real_vram_size < gmc->visible_vram_size);

	return (gmc->real_vram_size == gmc->visible_vram_size);
}

/**
 * amdgpu_gmc_sign_extend - sign extend the given gmc address
 *
 * @addr: address to extend
 */
static inline uint64_t amdgpu_gmc_sign_extend(uint64_t addr)
{
	if (addr >= AMDGPU_GMC_HOLE_START)
		addr |= AMDGPU_GMC_HOLE_END;

	return addr;
}

int amdgpu_gmc_pdb0_alloc(struct amdgpu_device *adev);
void amdgpu_gmc_get_pde_for_bo(struct amdgpu_bo *bo, int level,
			       uint64_t *addr, uint64_t *flags);
int amdgpu_gmc_set_pte_pde(struct amdgpu_device *adev, void *cpu_pt_addr,
				uint32_t gpu_page_idx, uint64_t addr,
				uint64_t flags);
uint64_t amdgpu_gmc_pd_addr(struct amdgpu_bo *bo);
uint64_t amdgpu_gmc_agp_addr(struct ttm_buffer_object *bo);
void amdgpu_gmc_sysvm_location(struct amdgpu_device *adev, struct amdgpu_gmc *mc);
void amdgpu_gmc_vram_location(struct amdgpu_device *adev, struct amdgpu_gmc *mc,
			      u64 base);
void amdgpu_gmc_gart_location(struct amdgpu_device *adev,
			      struct amdgpu_gmc *mc);
void amdgpu_gmc_agp_location(struct amdgpu_device *adev,
			     struct amdgpu_gmc *mc);
bool amdgpu_gmc_filter_faults(struct amdgpu_device *adev,
			      struct amdgpu_ih_ring *ih, uint64_t addr,
			      uint16_t pasid, uint64_t timestamp);
void amdgpu_gmc_filter_faults_remove(struct amdgpu_device *adev, uint64_t addr,
				     uint16_t pasid);
int amdgpu_gmc_ras_late_init(struct amdgpu_device *adev);
void amdgpu_gmc_ras_fini(struct amdgpu_device *adev);
int amdgpu_gmc_allocate_vm_inv_eng(struct amdgpu_device *adev);

extern void amdgpu_gmc_tmz_set(struct amdgpu_device *adev);
extern void amdgpu_gmc_noretry_set(struct amdgpu_device *adev);

extern void
amdgpu_gmc_set_vm_fault_masks(struct amdgpu_device *adev, int hub_type,
			      bool enable);

void amdgpu_gmc_get_vbios_allocations(struct amdgpu_device *adev);
void amdgpu_gmc_get_reserved_allocation(struct amdgpu_device *adev);

void amdgpu_gmc_init_pdb0(struct amdgpu_device *adev);
uint64_t amdgpu_gmc_vram_mc2pa(struct amdgpu_device *adev, uint64_t mc_addr);
uint64_t amdgpu_gmc_vram_pa(struct amdgpu_device *adev, struct amdgpu_bo *bo);
uint64_t amdgpu_gmc_vram_cpu_pa(struct amdgpu_device *adev, struct amdgpu_bo *bo);
#endif
