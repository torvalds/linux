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

#include <linux/module.h>
#include <linux/fdtable.h>
#include <linux/uaccess.h>
#include <drm/drmP.h>
#include "radeon.h"
#include "cikd.h"
#include "cik_reg.h"
#include "radeon_kfd.h"
#include "radeon_ucode.h"
#include <linux/firmware.h>
#include "cik_structs.h"

#define CIK_PIPE_PER_MEC	(4)

static const uint32_t watchRegs[MAX_WATCH_ADDRESSES * ADDRESS_WATCH_REG_MAX] = {
	TCP_WATCH0_ADDR_H, TCP_WATCH0_ADDR_L, TCP_WATCH0_CNTL,
	TCP_WATCH1_ADDR_H, TCP_WATCH1_ADDR_L, TCP_WATCH1_CNTL,
	TCP_WATCH2_ADDR_H, TCP_WATCH2_ADDR_L, TCP_WATCH2_CNTL,
	TCP_WATCH3_ADDR_H, TCP_WATCH3_ADDR_L, TCP_WATCH3_CNTL
};

struct kgd_mem {
	struct radeon_bo *bo;
	uint64_t gpu_addr;
	void *cpu_ptr;
};


static int alloc_gtt_mem(struct kgd_dev *kgd, size_t size,
			void **mem_obj, uint64_t *gpu_addr,
			void **cpu_ptr);

static void free_gtt_mem(struct kgd_dev *kgd, void *mem_obj);

static uint64_t get_vmem_size(struct kgd_dev *kgd);
static uint64_t get_gpu_clock_counter(struct kgd_dev *kgd);

static uint32_t get_max_engine_clock_in_mhz(struct kgd_dev *kgd);
static uint16_t get_fw_version(struct kgd_dev *kgd, enum kgd_engine_type type);

/*
 * Register access functions
 */

static void kgd_program_sh_mem_settings(struct kgd_dev *kgd, uint32_t vmid,
		uint32_t sh_mem_config,	uint32_t sh_mem_ape1_base,
		uint32_t sh_mem_ape1_limit, uint32_t sh_mem_bases);

static int kgd_set_pasid_vmid_mapping(struct kgd_dev *kgd, unsigned int pasid,
					unsigned int vmid);

static int kgd_init_pipeline(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t hpd_size, uint64_t hpd_gpu_addr);
static int kgd_init_interrupts(struct kgd_dev *kgd, uint32_t pipe_id);
static int kgd_hqd_load(struct kgd_dev *kgd, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr);
static int kgd_hqd_sdma_load(struct kgd_dev *kgd, void *mqd);
static bool kgd_hqd_is_occupied(struct kgd_dev *kgd, uint64_t queue_address,
				uint32_t pipe_id, uint32_t queue_id);

static int kgd_hqd_destroy(struct kgd_dev *kgd, uint32_t reset_type,
				unsigned int timeout, uint32_t pipe_id,
				uint32_t queue_id);
static bool kgd_hqd_sdma_is_occupied(struct kgd_dev *kgd, void *mqd);
static int kgd_hqd_sdma_destroy(struct kgd_dev *kgd, void *mqd,
				unsigned int timeout);
static int kgd_address_watch_disable(struct kgd_dev *kgd);
static int kgd_address_watch_execute(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					uint32_t cntl_val,
					uint32_t addr_hi,
					uint32_t addr_lo);
static int kgd_wave_control_execute(struct kgd_dev *kgd,
					uint32_t gfx_index_val,
					uint32_t sq_cmd);
static uint32_t kgd_address_watch_get_offset(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					unsigned int reg_offset);

static bool get_atc_vmid_pasid_mapping_valid(struct kgd_dev *kgd, uint8_t vmid);
static uint16_t get_atc_vmid_pasid_mapping_pasid(struct kgd_dev *kgd,
							uint8_t vmid);
static void write_vmid_invalidate_request(struct kgd_dev *kgd, uint8_t vmid);

static const struct kfd2kgd_calls kfd2kgd = {
	.init_gtt_mem_allocation = alloc_gtt_mem,
	.free_gtt_mem = free_gtt_mem,
	.get_vmem_size = get_vmem_size,
	.get_gpu_clock_counter = get_gpu_clock_counter,
	.get_max_engine_clock_in_mhz = get_max_engine_clock_in_mhz,
	.program_sh_mem_settings = kgd_program_sh_mem_settings,
	.set_pasid_vmid_mapping = kgd_set_pasid_vmid_mapping,
	.init_pipeline = kgd_init_pipeline,
	.init_interrupts = kgd_init_interrupts,
	.hqd_load = kgd_hqd_load,
	.hqd_sdma_load = kgd_hqd_sdma_load,
	.hqd_is_occupied = kgd_hqd_is_occupied,
	.hqd_sdma_is_occupied = kgd_hqd_sdma_is_occupied,
	.hqd_destroy = kgd_hqd_destroy,
	.hqd_sdma_destroy = kgd_hqd_sdma_destroy,
	.address_watch_disable = kgd_address_watch_disable,
	.address_watch_execute = kgd_address_watch_execute,
	.wave_control_execute = kgd_wave_control_execute,
	.address_watch_get_offset = kgd_address_watch_get_offset,
	.get_atc_vmid_pasid_mapping_pasid = get_atc_vmid_pasid_mapping_pasid,
	.get_atc_vmid_pasid_mapping_valid = get_atc_vmid_pasid_mapping_valid,
	.write_vmid_invalidate_request = write_vmid_invalidate_request,
	.get_fw_version = get_fw_version
};

static const struct kgd2kfd_calls *kgd2kfd;

int radeon_kfd_init(void)
{
	int ret;

#if defined(CONFIG_HSA_AMD_MODULE)
	int (*kgd2kfd_init_p)(unsigned, const struct kgd2kfd_calls**);

	kgd2kfd_init_p = symbol_request(kgd2kfd_init);

	if (kgd2kfd_init_p == NULL)
		return -ENOENT;

	ret = kgd2kfd_init_p(KFD_INTERFACE_VERSION, &kgd2kfd);
	if (ret) {
		symbol_put(kgd2kfd_init);
		kgd2kfd = NULL;
	}

#elif defined(CONFIG_HSA_AMD)
	ret = kgd2kfd_init(KFD_INTERFACE_VERSION, &kgd2kfd);
	if (ret)
		kgd2kfd = NULL;

#else
	ret = -ENOENT;
#endif

	return ret;
}

void radeon_kfd_fini(void)
{
	if (kgd2kfd) {
		kgd2kfd->exit();
		symbol_put(kgd2kfd_init);
	}
}

void radeon_kfd_device_probe(struct radeon_device *rdev)
{
	if (kgd2kfd)
		rdev->kfd = kgd2kfd->probe((struct kgd_dev *)rdev,
			rdev->pdev, &kfd2kgd);
}

void radeon_kfd_device_init(struct radeon_device *rdev)
{
	int i, queue, pipe, mec;

	if (rdev->kfd) {
		struct kgd2kfd_shared_resources gpu_resources = {
			.compute_vmid_bitmap = 0xFF00,
			.num_pipe_per_mec = 4,
			.num_queue_per_pipe = 8
		};

		bitmap_zero(gpu_resources.queue_bitmap, KGD_MAX_QUEUES);

		for (i = 0; i < KGD_MAX_QUEUES; ++i) {
			queue = i % gpu_resources.num_queue_per_pipe;
			pipe = (i / gpu_resources.num_queue_per_pipe)
				% gpu_resources.num_pipe_per_mec;
			mec = (i / gpu_resources.num_queue_per_pipe)
				/ gpu_resources.num_pipe_per_mec;

			if (mec == 0 && pipe > 0)
				set_bit(i, gpu_resources.queue_bitmap);
		}

		radeon_doorbell_get_kfd_info(rdev,
				&gpu_resources.doorbell_physical_address,
				&gpu_resources.doorbell_aperture_size,
				&gpu_resources.doorbell_start_offset);

		kgd2kfd->device_init(rdev->kfd, &gpu_resources);
	}
}

void radeon_kfd_device_fini(struct radeon_device *rdev)
{
	if (rdev->kfd) {
		kgd2kfd->device_exit(rdev->kfd);
		rdev->kfd = NULL;
	}
}

void radeon_kfd_interrupt(struct radeon_device *rdev, const void *ih_ring_entry)
{
	if (rdev->kfd)
		kgd2kfd->interrupt(rdev->kfd, ih_ring_entry);
}

void radeon_kfd_suspend(struct radeon_device *rdev)
{
	if (rdev->kfd)
		kgd2kfd->suspend(rdev->kfd);
}

int radeon_kfd_resume(struct radeon_device *rdev)
{
	int r = 0;

	if (rdev->kfd)
		r = kgd2kfd->resume(rdev->kfd);

	return r;
}

static int alloc_gtt_mem(struct kgd_dev *kgd, size_t size,
			void **mem_obj, uint64_t *gpu_addr,
			void **cpu_ptr)
{
	struct radeon_device *rdev = (struct radeon_device *)kgd;
	struct kgd_mem **mem = (struct kgd_mem **) mem_obj;
	int r;

	BUG_ON(kgd == NULL);
	BUG_ON(gpu_addr == NULL);
	BUG_ON(cpu_ptr == NULL);

	*mem = kmalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if ((*mem) == NULL)
		return -ENOMEM;

	r = radeon_bo_create(rdev, size, PAGE_SIZE, true, RADEON_GEM_DOMAIN_GTT,
				RADEON_GEM_GTT_WC, NULL, NULL, &(*mem)->bo);
	if (r) {
		dev_err(rdev->dev,
			"failed to allocate BO for amdkfd (%d)\n", r);
		return r;
	}

	/* map the buffer */
	r = radeon_bo_reserve((*mem)->bo, true);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to reserve bo for amdkfd\n", r);
		goto allocate_mem_reserve_bo_failed;
	}

	r = radeon_bo_pin((*mem)->bo, RADEON_GEM_DOMAIN_GTT,
				&(*mem)->gpu_addr);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to pin bo for amdkfd\n", r);
		goto allocate_mem_pin_bo_failed;
	}
	*gpu_addr = (*mem)->gpu_addr;

	r = radeon_bo_kmap((*mem)->bo, &(*mem)->cpu_ptr);
	if (r) {
		dev_err(rdev->dev,
			"(%d) failed to map bo to kernel for amdkfd\n", r);
		goto allocate_mem_kmap_bo_failed;
	}
	*cpu_ptr = (*mem)->cpu_ptr;

	radeon_bo_unreserve((*mem)->bo);

	return 0;

allocate_mem_kmap_bo_failed:
	radeon_bo_unpin((*mem)->bo);
allocate_mem_pin_bo_failed:
	radeon_bo_unreserve((*mem)->bo);
allocate_mem_reserve_bo_failed:
	radeon_bo_unref(&(*mem)->bo);

	return r;
}

static void free_gtt_mem(struct kgd_dev *kgd, void *mem_obj)
{
	struct kgd_mem *mem = (struct kgd_mem *) mem_obj;

	BUG_ON(mem == NULL);

	radeon_bo_reserve(mem->bo, true);
	radeon_bo_kunmap(mem->bo);
	radeon_bo_unpin(mem->bo);
	radeon_bo_unreserve(mem->bo);
	radeon_bo_unref(&(mem->bo));
	kfree(mem);
}

static uint64_t get_vmem_size(struct kgd_dev *kgd)
{
	struct radeon_device *rdev = (struct radeon_device *)kgd;

	BUG_ON(kgd == NULL);

	return rdev->mc.real_vram_size;
}

static uint64_t get_gpu_clock_counter(struct kgd_dev *kgd)
{
	struct radeon_device *rdev = (struct radeon_device *)kgd;

	return rdev->asic->get_gpu_clock_counter(rdev);
}

static uint32_t get_max_engine_clock_in_mhz(struct kgd_dev *kgd)
{
	struct radeon_device *rdev = (struct radeon_device *)kgd;

	/* The sclk is in quantas of 10kHz */
	return rdev->pm.dpm.dyn_state.max_clock_voltage_on_ac.sclk / 100;
}

static inline struct radeon_device *get_radeon_device(struct kgd_dev *kgd)
{
	return (struct radeon_device *)kgd;
}

static void write_register(struct kgd_dev *kgd, uint32_t offset, uint32_t value)
{
	struct radeon_device *rdev = get_radeon_device(kgd);

	writel(value, (void __iomem *)(rdev->rmmio + offset));
}

static uint32_t read_register(struct kgd_dev *kgd, uint32_t offset)
{
	struct radeon_device *rdev = get_radeon_device(kgd);

	return readl((void __iomem *)(rdev->rmmio + offset));
}

static void lock_srbm(struct kgd_dev *kgd, uint32_t mec, uint32_t pipe,
			uint32_t queue, uint32_t vmid)
{
	struct radeon_device *rdev = get_radeon_device(kgd);
	uint32_t value = PIPEID(pipe) | MEID(mec) | VMID(vmid) | QUEUEID(queue);

	mutex_lock(&rdev->srbm_mutex);
	write_register(kgd, SRBM_GFX_CNTL, value);
}

static void unlock_srbm(struct kgd_dev *kgd)
{
	struct radeon_device *rdev = get_radeon_device(kgd);

	write_register(kgd, SRBM_GFX_CNTL, 0);
	mutex_unlock(&rdev->srbm_mutex);
}

static void acquire_queue(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t queue_id)
{
	uint32_t mec = (++pipe_id / CIK_PIPE_PER_MEC) + 1;
	uint32_t pipe = (pipe_id % CIK_PIPE_PER_MEC);

	lock_srbm(kgd, mec, pipe, queue_id, 0);
}

static void release_queue(struct kgd_dev *kgd)
{
	unlock_srbm(kgd);
}

static void kgd_program_sh_mem_settings(struct kgd_dev *kgd, uint32_t vmid,
					uint32_t sh_mem_config,
					uint32_t sh_mem_ape1_base,
					uint32_t sh_mem_ape1_limit,
					uint32_t sh_mem_bases)
{
	lock_srbm(kgd, 0, 0, 0, vmid);

	write_register(kgd, SH_MEM_CONFIG, sh_mem_config);
	write_register(kgd, SH_MEM_APE1_BASE, sh_mem_ape1_base);
	write_register(kgd, SH_MEM_APE1_LIMIT, sh_mem_ape1_limit);
	write_register(kgd, SH_MEM_BASES, sh_mem_bases);

	unlock_srbm(kgd);
}

static int kgd_set_pasid_vmid_mapping(struct kgd_dev *kgd, unsigned int pasid,
					unsigned int vmid)
{
	/*
	 * We have to assume that there is no outstanding mapping.
	 * The ATC_VMID_PASID_MAPPING_UPDATE_STATUS bit could be 0
	 * because a mapping is in progress or because a mapping finished and
	 * the SW cleared it.
	 * So the protocol is to always wait & clear.
	 */
	uint32_t pasid_mapping = (pasid == 0) ? 0 : (uint32_t)pasid |
					ATC_VMID_PASID_MAPPING_VALID_MASK;

	write_register(kgd, ATC_VMID0_PASID_MAPPING + vmid*sizeof(uint32_t),
			pasid_mapping);

	while (!(read_register(kgd, ATC_VMID_PASID_MAPPING_UPDATE_STATUS) &
								(1U << vmid)))
		cpu_relax();
	write_register(kgd, ATC_VMID_PASID_MAPPING_UPDATE_STATUS, 1U << vmid);

	/* Mapping vmid to pasid also for IH block */
	write_register(kgd, IH_VMID_0_LUT + vmid * sizeof(uint32_t),
			pasid_mapping);

	return 0;
}

static int kgd_init_pipeline(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t hpd_size, uint64_t hpd_gpu_addr)
{
	/* nothing to do here */
	return 0;
}

static int kgd_init_interrupts(struct kgd_dev *kgd, uint32_t pipe_id)
{
	uint32_t mec;
	uint32_t pipe;

	mec = (pipe_id / CIK_PIPE_PER_MEC) + 1;
	pipe = (pipe_id % CIK_PIPE_PER_MEC);

	lock_srbm(kgd, mec, pipe, 0, 0);

	write_register(kgd, CPC_INT_CNTL,
			TIME_STAMP_INT_ENABLE | OPCODE_ERROR_INT_ENABLE);

	unlock_srbm(kgd);

	return 0;
}

static inline uint32_t get_sdma_base_addr(struct cik_sdma_rlc_registers *m)
{
	uint32_t retval;

	retval = m->sdma_engine_id * SDMA1_REGISTER_OFFSET +
			m->sdma_queue_id * KFD_CIK_SDMA_QUEUE_OFFSET;

	pr_debug("kfd: sdma base address: 0x%x\n", retval);

	return retval;
}

static inline struct cik_mqd *get_mqd(void *mqd)
{
	return (struct cik_mqd *)mqd;
}

static inline struct cik_sdma_rlc_registers *get_sdma_mqd(void *mqd)
{
	return (struct cik_sdma_rlc_registers *)mqd;
}

static int kgd_hqd_load(struct kgd_dev *kgd, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr)
{
	uint32_t wptr_shadow, is_wptr_shadow_valid;
	struct cik_mqd *m;

	m = get_mqd(mqd);

	is_wptr_shadow_valid = !get_user(wptr_shadow, wptr);

	acquire_queue(kgd, pipe_id, queue_id);
	write_register(kgd, CP_MQD_BASE_ADDR, m->cp_mqd_base_addr_lo);
	write_register(kgd, CP_MQD_BASE_ADDR_HI, m->cp_mqd_base_addr_hi);
	write_register(kgd, CP_MQD_CONTROL, m->cp_mqd_control);

	write_register(kgd, CP_HQD_PQ_BASE, m->cp_hqd_pq_base_lo);
	write_register(kgd, CP_HQD_PQ_BASE_HI, m->cp_hqd_pq_base_hi);
	write_register(kgd, CP_HQD_PQ_CONTROL, m->cp_hqd_pq_control);

	write_register(kgd, CP_HQD_IB_CONTROL, m->cp_hqd_ib_control);
	write_register(kgd, CP_HQD_IB_BASE_ADDR, m->cp_hqd_ib_base_addr_lo);
	write_register(kgd, CP_HQD_IB_BASE_ADDR_HI, m->cp_hqd_ib_base_addr_hi);

	write_register(kgd, CP_HQD_IB_RPTR, m->cp_hqd_ib_rptr);

	write_register(kgd, CP_HQD_PERSISTENT_STATE,
			m->cp_hqd_persistent_state);
	write_register(kgd, CP_HQD_SEMA_CMD, m->cp_hqd_sema_cmd);
	write_register(kgd, CP_HQD_MSG_TYPE, m->cp_hqd_msg_type);

	write_register(kgd, CP_HQD_ATOMIC0_PREOP_LO,
			m->cp_hqd_atomic0_preop_lo);

	write_register(kgd, CP_HQD_ATOMIC0_PREOP_HI,
			m->cp_hqd_atomic0_preop_hi);

	write_register(kgd, CP_HQD_ATOMIC1_PREOP_LO,
			m->cp_hqd_atomic1_preop_lo);

	write_register(kgd, CP_HQD_ATOMIC1_PREOP_HI,
			m->cp_hqd_atomic1_preop_hi);

	write_register(kgd, CP_HQD_PQ_RPTR_REPORT_ADDR,
			m->cp_hqd_pq_rptr_report_addr_lo);

	write_register(kgd, CP_HQD_PQ_RPTR_REPORT_ADDR_HI,
			m->cp_hqd_pq_rptr_report_addr_hi);

	write_register(kgd, CP_HQD_PQ_RPTR, m->cp_hqd_pq_rptr);

	write_register(kgd, CP_HQD_PQ_WPTR_POLL_ADDR,
			m->cp_hqd_pq_wptr_poll_addr_lo);

	write_register(kgd, CP_HQD_PQ_WPTR_POLL_ADDR_HI,
			m->cp_hqd_pq_wptr_poll_addr_hi);

	write_register(kgd, CP_HQD_PQ_DOORBELL_CONTROL,
			m->cp_hqd_pq_doorbell_control);

	write_register(kgd, CP_HQD_VMID, m->cp_hqd_vmid);

	write_register(kgd, CP_HQD_QUANTUM, m->cp_hqd_quantum);

	write_register(kgd, CP_HQD_PIPE_PRIORITY, m->cp_hqd_pipe_priority);
	write_register(kgd, CP_HQD_QUEUE_PRIORITY, m->cp_hqd_queue_priority);

	write_register(kgd, CP_HQD_IQ_RPTR, m->cp_hqd_iq_rptr);

	if (is_wptr_shadow_valid)
		write_register(kgd, CP_HQD_PQ_WPTR, wptr_shadow);

	write_register(kgd, CP_HQD_ACTIVE, m->cp_hqd_active);
	release_queue(kgd);

	return 0;
}

static int kgd_hqd_sdma_load(struct kgd_dev *kgd, void *mqd)
{
	struct cik_sdma_rlc_registers *m;
	uint32_t sdma_base_addr;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_VIRTUAL_ADDR,
			m->sdma_rlc_virtual_addr);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_RB_BASE,
			m->sdma_rlc_rb_base);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_RB_BASE_HI,
			m->sdma_rlc_rb_base_hi);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_RB_RPTR_ADDR_LO,
			m->sdma_rlc_rb_rptr_addr_lo);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_RB_RPTR_ADDR_HI,
			m->sdma_rlc_rb_rptr_addr_hi);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_DOORBELL,
			m->sdma_rlc_doorbell);

	write_register(kgd,
			sdma_base_addr + SDMA0_RLC0_RB_CNTL,
			m->sdma_rlc_rb_cntl);

	return 0;
}

static bool kgd_hqd_is_occupied(struct kgd_dev *kgd, uint64_t queue_address,
				uint32_t pipe_id, uint32_t queue_id)
{
	uint32_t act;
	bool retval = false;
	uint32_t low, high;

	acquire_queue(kgd, pipe_id, queue_id);
	act = read_register(kgd, CP_HQD_ACTIVE);
	if (act) {
		low = lower_32_bits(queue_address >> 8);
		high = upper_32_bits(queue_address >> 8);

		if (low == read_register(kgd, CP_HQD_PQ_BASE) &&
				high == read_register(kgd, CP_HQD_PQ_BASE_HI))
			retval = true;
	}
	release_queue(kgd);
	return retval;
}

static bool kgd_hqd_sdma_is_occupied(struct kgd_dev *kgd, void *mqd)
{
	struct cik_sdma_rlc_registers *m;
	uint32_t sdma_base_addr;
	uint32_t sdma_rlc_rb_cntl;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

	sdma_rlc_rb_cntl = read_register(kgd,
					sdma_base_addr + SDMA0_RLC0_RB_CNTL);

	if (sdma_rlc_rb_cntl & SDMA_RB_ENABLE)
		return true;

	return false;
}

static int kgd_hqd_destroy(struct kgd_dev *kgd, uint32_t reset_type,
				unsigned int timeout, uint32_t pipe_id,
				uint32_t queue_id)
{
	uint32_t temp;

	acquire_queue(kgd, pipe_id, queue_id);
	write_register(kgd, CP_HQD_PQ_DOORBELL_CONTROL, 0);

	write_register(kgd, CP_HQD_DEQUEUE_REQUEST, reset_type);

	while (true) {
		temp = read_register(kgd, CP_HQD_ACTIVE);
		if (temp & 0x1)
			break;
		if (timeout == 0) {
			pr_err("kfd: cp queue preemption time out (%dms)\n",
				temp);
			release_queue(kgd);
			return -ETIME;
		}
		msleep(20);
		timeout -= 20;
	}

	release_queue(kgd);
	return 0;
}

static int kgd_hqd_sdma_destroy(struct kgd_dev *kgd, void *mqd,
				unsigned int timeout)
{
	struct cik_sdma_rlc_registers *m;
	uint32_t sdma_base_addr;
	uint32_t temp;

	m = get_sdma_mqd(mqd);
	sdma_base_addr = get_sdma_base_addr(m);

	temp = read_register(kgd, sdma_base_addr + SDMA0_RLC0_RB_CNTL);
	temp = temp & ~SDMA_RB_ENABLE;
	write_register(kgd, sdma_base_addr + SDMA0_RLC0_RB_CNTL, temp);

	while (true) {
		temp = read_register(kgd, sdma_base_addr +
						SDMA0_RLC0_CONTEXT_STATUS);
		if (temp & SDMA_RLC_IDLE)
			break;
		if (timeout == 0)
			return -ETIME;
		msleep(20);
		timeout -= 20;
	}

	write_register(kgd, sdma_base_addr + SDMA0_RLC0_DOORBELL, 0);
	write_register(kgd, sdma_base_addr + SDMA0_RLC0_RB_RPTR, 0);
	write_register(kgd, sdma_base_addr + SDMA0_RLC0_RB_WPTR, 0);
	write_register(kgd, sdma_base_addr + SDMA0_RLC0_RB_BASE, 0);

	return 0;
}

static int kgd_address_watch_disable(struct kgd_dev *kgd)
{
	union TCP_WATCH_CNTL_BITS cntl;
	unsigned int i;

	cntl.u32All = 0;

	cntl.bitfields.valid = 0;
	cntl.bitfields.mask = ADDRESS_WATCH_REG_CNTL_DEFAULT_MASK;
	cntl.bitfields.atc = 1;

	/* Turning off this address until we set all the registers */
	for (i = 0; i < MAX_WATCH_ADDRESSES; i++)
		write_register(kgd,
				watchRegs[i * ADDRESS_WATCH_REG_MAX +
					ADDRESS_WATCH_REG_CNTL],
				cntl.u32All);

	return 0;
}

static int kgd_address_watch_execute(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					uint32_t cntl_val,
					uint32_t addr_hi,
					uint32_t addr_lo)
{
	union TCP_WATCH_CNTL_BITS cntl;

	cntl.u32All = cntl_val;

	/* Turning off this watch point until we set all the registers */
	cntl.bitfields.valid = 0;
	write_register(kgd,
			watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX +
				ADDRESS_WATCH_REG_CNTL],
			cntl.u32All);

	write_register(kgd,
			watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX +
				ADDRESS_WATCH_REG_ADDR_HI],
			addr_hi);

	write_register(kgd,
			watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX +
				ADDRESS_WATCH_REG_ADDR_LO],
			addr_lo);

	/* Enable the watch point */
	cntl.bitfields.valid = 1;

	write_register(kgd,
			watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX +
				ADDRESS_WATCH_REG_CNTL],
			cntl.u32All);

	return 0;
}

static int kgd_wave_control_execute(struct kgd_dev *kgd,
					uint32_t gfx_index_val,
					uint32_t sq_cmd)
{
	struct radeon_device *rdev = get_radeon_device(kgd);
	uint32_t data;

	mutex_lock(&rdev->grbm_idx_mutex);

	write_register(kgd, GRBM_GFX_INDEX, gfx_index_val);
	write_register(kgd, SQ_CMD, sq_cmd);

	/*  Restore the GRBM_GFX_INDEX register  */

	data = INSTANCE_BROADCAST_WRITES | SH_BROADCAST_WRITES |
		SE_BROADCAST_WRITES;

	write_register(kgd, GRBM_GFX_INDEX, data);

	mutex_unlock(&rdev->grbm_idx_mutex);

	return 0;
}

static uint32_t kgd_address_watch_get_offset(struct kgd_dev *kgd,
					unsigned int watch_point_id,
					unsigned int reg_offset)
{
	return watchRegs[watch_point_id * ADDRESS_WATCH_REG_MAX + reg_offset];
}

static bool get_atc_vmid_pasid_mapping_valid(struct kgd_dev *kgd, uint8_t vmid)
{
	uint32_t reg;
	struct radeon_device *rdev = (struct radeon_device *) kgd;

	reg = RREG32(ATC_VMID0_PASID_MAPPING + vmid*4);
	return reg & ATC_VMID_PASID_MAPPING_VALID_MASK;
}

static uint16_t get_atc_vmid_pasid_mapping_pasid(struct kgd_dev *kgd,
							uint8_t vmid)
{
	uint32_t reg;
	struct radeon_device *rdev = (struct radeon_device *) kgd;

	reg = RREG32(ATC_VMID0_PASID_MAPPING + vmid*4);
	return reg & ATC_VMID_PASID_MAPPING_PASID_MASK;
}

static void write_vmid_invalidate_request(struct kgd_dev *kgd, uint8_t vmid)
{
	struct radeon_device *rdev = (struct radeon_device *) kgd;

	return WREG32(VM_INVALIDATE_REQUEST, 1 << vmid);
}

static uint16_t get_fw_version(struct kgd_dev *kgd, enum kgd_engine_type type)
{
	struct radeon_device *rdev = (struct radeon_device *) kgd;
	const union radeon_firmware_header *hdr;

	BUG_ON(kgd == NULL || rdev->mec_fw == NULL);

	switch (type) {
	case KGD_ENGINE_PFP:
		hdr = (const union radeon_firmware_header *) rdev->pfp_fw->data;
		break;

	case KGD_ENGINE_ME:
		hdr = (const union radeon_firmware_header *) rdev->me_fw->data;
		break;

	case KGD_ENGINE_CE:
		hdr = (const union radeon_firmware_header *) rdev->ce_fw->data;
		break;

	case KGD_ENGINE_MEC1:
		hdr = (const union radeon_firmware_header *) rdev->mec_fw->data;
		break;

	case KGD_ENGINE_MEC2:
		hdr = (const union radeon_firmware_header *)
							rdev->mec2_fw->data;
		break;

	case KGD_ENGINE_RLC:
		hdr = (const union radeon_firmware_header *) rdev->rlc_fw->data;
		break;

	case KGD_ENGINE_SDMA1:
	case KGD_ENGINE_SDMA2:
		hdr = (const union radeon_firmware_header *)
							rdev->sdma_fw->data;
		break;

	default:
		return 0;
	}

	if (hdr == NULL)
		return 0;

	/* Only 12 bit in use*/
	return hdr->common.ucode_version;
}
