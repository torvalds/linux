/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 */

/*
 * This file defines the private interface between the
 * AMD kernel graphics drivers and the AMD KFD.
 */

#ifndef KGD_KFD_INTERFACE_H_INCLUDED
#define KGD_KFD_INTERFACE_H_INCLUDED

#include <linux/types.h>
#include <linux/bitmap.h>
#include <linux/dma-fence.h>

struct pci_dev;
struct amdgpu_device;

#define KGD_MAX_QUEUES 128

struct kfd_dev;
struct kgd_mem;

enum kfd_preempt_type {
	KFD_PREEMPT_TYPE_WAVEFRONT_DRAIN = 0,
	KFD_PREEMPT_TYPE_WAVEFRONT_RESET,
	KFD_PREEMPT_TYPE_WAVEFRONT_SAVE
};

struct kfd_vm_fault_info {
	uint64_t	page_addr;
	uint32_t	vmid;
	uint32_t	mc_id;
	uint32_t	status;
	bool		prot_valid;
	bool		prot_read;
	bool		prot_write;
	bool		prot_exec;
};

struct kfd_cu_info {
	uint32_t num_shader_engines;
	uint32_t num_shader_arrays_per_engine;
	uint32_t num_cu_per_sh;
	uint32_t cu_active_number;
	uint32_t cu_ao_mask;
	uint32_t simd_per_cu;
	uint32_t max_waves_per_simd;
	uint32_t wave_front_size;
	uint32_t max_scratch_slots_per_cu;
	uint32_t lds_size;
	uint32_t cu_bitmap[4][4];
};

/* For getting GPU local memory information from KGD */
struct kfd_local_mem_info {
	uint64_t local_mem_size_private;
	uint64_t local_mem_size_public;
	uint32_t vram_width;
	uint32_t mem_clk_max;
};

enum kgd_memory_pool {
	KGD_POOL_SYSTEM_CACHEABLE = 1,
	KGD_POOL_SYSTEM_WRITECOMBINE = 2,
	KGD_POOL_FRAMEBUFFER = 3,
};

/**
 * enum kfd_sched_policy
 *
 * @KFD_SCHED_POLICY_HWS: H/W scheduling policy known as command processor (cp)
 * scheduling. In this scheduling mode we're using the firmware code to
 * schedule the user mode queues and kernel queues such as HIQ and DIQ.
 * the HIQ queue is used as a special queue that dispatches the configuration
 * to the cp and the user mode queues list that are currently running.
 * the DIQ queue is a debugging queue that dispatches debugging commands to the
 * firmware.
 * in this scheduling mode user mode queues over subscription feature is
 * enabled.
 *
 * @KFD_SCHED_POLICY_HWS_NO_OVERSUBSCRIPTION: The same as above but the over
 * subscription feature disabled.
 *
 * @KFD_SCHED_POLICY_NO_HWS: no H/W scheduling policy is a mode which directly
 * set the command processor registers and sets the queues "manually". This
 * mode is used *ONLY* for debugging proposes.
 *
 */
enum kfd_sched_policy {
	KFD_SCHED_POLICY_HWS = 0,
	KFD_SCHED_POLICY_HWS_NO_OVERSUBSCRIPTION,
	KFD_SCHED_POLICY_NO_HWS
};

struct kgd2kfd_shared_resources {
	/* Bit n == 1 means VMID n is available for KFD. */
	unsigned int compute_vmid_bitmap;

	/* number of pipes per mec */
	uint32_t num_pipe_per_mec;

	/* number of queues per pipe */
	uint32_t num_queue_per_pipe;

	/* Bit n == 1 means Queue n is available for KFD */
	DECLARE_BITMAP(cp_queue_bitmap, KGD_MAX_QUEUES);

	/* SDMA doorbell assignments (SOC15 and later chips only). Only
	 * specific doorbells are routed to each SDMA engine. Others
	 * are routed to IH and VCN. They are not usable by the CP.
	 */
	uint32_t *sdma_doorbell_idx;

	/* From SOC15 onward, the doorbell index range not usable for CP
	 * queues.
	 */
	uint32_t non_cp_doorbells_start;
	uint32_t non_cp_doorbells_end;

	/* Base address of doorbell aperture. */
	phys_addr_t doorbell_physical_address;

	/* Size in bytes of doorbell aperture. */
	size_t doorbell_aperture_size;

	/* Number of bytes at start of aperture reserved for KGD. */
	size_t doorbell_start_offset;

	/* GPUVM address space size in bytes */
	uint64_t gpuvm_size;

	/* Minor device number of the render node */
	int drm_render_minor;

};

struct tile_config {
	uint32_t *tile_config_ptr;
	uint32_t *macro_tile_config_ptr;
	uint32_t num_tile_configs;
	uint32_t num_macro_tile_configs;

	uint32_t gb_addr_config;
	uint32_t num_banks;
	uint32_t num_ranks;
};

#define KFD_MAX_NUM_OF_QUEUES_PER_DEVICE_DEFAULT 4096

/**
 * struct kfd2kgd_calls
 *
 * @program_sh_mem_settings: A function that should initiate the memory
 * properties such as main aperture memory type (cache / non cached) and
 * secondary aperture base address, size and memory type.
 * This function is used only for no cp scheduling mode.
 *
 * @set_pasid_vmid_mapping: Exposes pasid/vmid pair to the H/W for no cp
 * scheduling mode. Only used for no cp scheduling mode.
 *
 * @hqd_load: Loads the mqd structure to a H/W hqd slot. used only for no cp
 * sceduling mode.
 *
 * @hqd_sdma_load: Loads the SDMA mqd structure to a H/W SDMA hqd slot.
 * used only for no HWS mode.
 *
 * @hqd_dump: Dumps CPC HQD registers to an array of address-value pairs.
 * Array is allocated with kmalloc, needs to be freed with kfree by caller.
 *
 * @hqd_sdma_dump: Dumps SDMA HQD registers to an array of address-value pairs.
 * Array is allocated with kmalloc, needs to be freed with kfree by caller.
 *
 * @hqd_is_occupies: Checks if a hqd slot is occupied.
 *
 * @hqd_destroy: Destructs and preempts the queue assigned to that hqd slot.
 *
 * @hqd_sdma_is_occupied: Checks if an SDMA hqd slot is occupied.
 *
 * @hqd_sdma_destroy: Destructs and preempts the SDMA queue assigned to that
 * SDMA hqd slot.
 *
 * @set_scratch_backing_va: Sets VA for scratch backing memory of a VMID.
 * Only used for no cp scheduling mode
 *
 * @set_vm_context_page_table_base: Program page table base for a VMID
 *
 * @invalidate_tlbs: Invalidate TLBs for a specific PASID
 *
 * @invalidate_tlbs_vmid: Invalidate TLBs for a specific VMID
 *
 * @read_vmid_from_vmfault_reg: On Hawaii the VMID is not set in the
 * IH ring entry. This function allows the KFD ISR to get the VMID
 * from the fault status register as early as possible.
 *
 * @get_cu_occupancy: Function pointer that returns to caller the number
 * of wave fronts that are in flight for all of the queues of a process
 * as identified by its pasid. It is important to note that the value
 * returned by this function is a snapshot of current moment and cannot
 * guarantee any minimum for the number of waves in-flight. This function
 * is defined for devices that belong to GFX9 and later GFX families. Care
 * must be taken in calling this function as it is not defined for devices
 * that belong to GFX8 and below GFX families.
 *
 * This structure contains function pointers to services that the kgd driver
 * provides to amdkfd driver.
 *
 */
struct kfd2kgd_calls {
	/* Register access functions */
	void (*program_sh_mem_settings)(struct amdgpu_device *adev, uint32_t vmid,
			uint32_t sh_mem_config,	uint32_t sh_mem_ape1_base,
			uint32_t sh_mem_ape1_limit, uint32_t sh_mem_bases);

	int (*set_pasid_vmid_mapping)(struct amdgpu_device *adev, u32 pasid,
					unsigned int vmid);

	int (*init_interrupts)(struct amdgpu_device *adev, uint32_t pipe_id);

	int (*hqd_load)(struct amdgpu_device *adev, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr,
			uint32_t wptr_shift, uint32_t wptr_mask,
			struct mm_struct *mm);

	int (*hiq_mqd_load)(struct amdgpu_device *adev, void *mqd,
			    uint32_t pipe_id, uint32_t queue_id,
			    uint32_t doorbell_off);

	int (*hqd_sdma_load)(struct amdgpu_device *adev, void *mqd,
			     uint32_t __user *wptr, struct mm_struct *mm);

	int (*hqd_dump)(struct amdgpu_device *adev,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t (**dump)[2], uint32_t *n_regs);

	int (*hqd_sdma_dump)(struct amdgpu_device *adev,
			     uint32_t engine_id, uint32_t queue_id,
			     uint32_t (**dump)[2], uint32_t *n_regs);

	bool (*hqd_is_occupied)(struct amdgpu_device *adev,
				uint64_t queue_address, uint32_t pipe_id,
				uint32_t queue_id);

	int (*hqd_destroy)(struct amdgpu_device *adev, void *mqd,
				uint32_t reset_type, unsigned int timeout,
				uint32_t pipe_id, uint32_t queue_id);

	bool (*hqd_sdma_is_occupied)(struct amdgpu_device *adev, void *mqd);

	int (*hqd_sdma_destroy)(struct amdgpu_device *adev, void *mqd,
				unsigned int timeout);

	int (*wave_control_execute)(struct amdgpu_device *adev,
					uint32_t gfx_index_val,
					uint32_t sq_cmd);
	bool (*get_atc_vmid_pasid_mapping_info)(struct amdgpu_device *adev,
					uint8_t vmid,
					uint16_t *p_pasid);

	/* No longer needed from GFXv9 onward. The scratch base address is
	 * passed to the shader by the CP. It's the user mode driver's
	 * responsibility.
	 */
	void (*set_scratch_backing_va)(struct amdgpu_device *adev,
				uint64_t va, uint32_t vmid);

	void (*set_vm_context_page_table_base)(struct amdgpu_device *adev,
			uint32_t vmid, uint64_t page_table_base);
	uint32_t (*read_vmid_from_vmfault_reg)(struct amdgpu_device *adev);

	void (*get_cu_occupancy)(struct amdgpu_device *adev, int pasid,
			int *wave_cnt, int *max_waves_per_cu);
	void (*program_trap_handler_settings)(struct amdgpu_device *adev,
			uint32_t vmid, uint64_t tba_addr, uint64_t tma_addr);
};

#endif	/* KGD_KFD_INTERFACE_H_INCLUDED */
