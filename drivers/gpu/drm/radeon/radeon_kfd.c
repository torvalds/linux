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

#define CIK_PIPE_PER_MEC	(4)

struct kgd_mem {
	struct radeon_sa_bo *sa_bo;
	uint64_t gpu_addr;
	void *ptr;
};

static int init_sa_manager(struct kgd_dev *kgd, unsigned int size);
static void fini_sa_manager(struct kgd_dev *kgd);

static int allocate_mem(struct kgd_dev *kgd, size_t size, size_t alignment,
		enum kgd_memory_pool pool, struct kgd_mem **mem);

static void free_mem(struct kgd_dev *kgd, struct kgd_mem *mem);

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

static int kgd_init_memory(struct kgd_dev *kgd);

static int kgd_init_pipeline(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t hpd_size, uint64_t hpd_gpu_addr);

static int kgd_hqd_load(struct kgd_dev *kgd, void *mqd, uint32_t pipe_id,
			uint32_t queue_id, uint32_t __user *wptr);

static bool kgd_hqd_is_occupied(struct kgd_dev *kgd, uint64_t queue_address,
				uint32_t pipe_id, uint32_t queue_id);

static int kgd_hqd_destroy(struct kgd_dev *kgd, uint32_t reset_type,
				unsigned int timeout, uint32_t pipe_id,
				uint32_t queue_id);

static const struct kfd2kgd_calls kfd2kgd = {
	.init_sa_manager = init_sa_manager,
	.fini_sa_manager = fini_sa_manager,
	.allocate_mem = allocate_mem,
	.free_mem = free_mem,
	.get_vmem_size = get_vmem_size,
	.get_gpu_clock_counter = get_gpu_clock_counter,
	.get_max_engine_clock_in_mhz = get_max_engine_clock_in_mhz,
	.program_sh_mem_settings = kgd_program_sh_mem_settings,
	.set_pasid_vmid_mapping = kgd_set_pasid_vmid_mapping,
	.init_memory = kgd_init_memory,
	.init_pipeline = kgd_init_pipeline,
	.hqd_load = kgd_hqd_load,
	.hqd_is_occupied = kgd_hqd_is_occupied,
	.hqd_destroy = kgd_hqd_destroy,
	.get_fw_version = get_fw_version
};

static const struct kgd2kfd_calls *kgd2kfd;

bool radeon_kfd_init(void)
{
#if defined(CONFIG_HSA_AMD_MODULE)
	bool (*kgd2kfd_init_p)(unsigned, const struct kfd2kgd_calls*,
				const struct kgd2kfd_calls**);

	kgd2kfd_init_p = symbol_request(kgd2kfd_init);

	if (kgd2kfd_init_p == NULL)
		return false;

	if (!kgd2kfd_init_p(KFD_INTERFACE_VERSION, &kfd2kgd, &kgd2kfd)) {
		symbol_put(kgd2kfd_init);
		kgd2kfd = NULL;

		return false;
	}

	return true;
#elif defined(CONFIG_HSA_AMD)
	if (!kgd2kfd_init(KFD_INTERFACE_VERSION, &kfd2kgd, &kgd2kfd)) {
		kgd2kfd = NULL;

		return false;
	}

	return true;
#else
	return false;
#endif
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
		rdev->kfd = kgd2kfd->probe((struct kgd_dev *)rdev, rdev->pdev);
}

void radeon_kfd_device_init(struct radeon_device *rdev)
{
	if (rdev->kfd) {
		struct kgd2kfd_shared_resources gpu_resources = {
			.compute_vmid_bitmap = 0xFF00,

			.first_compute_pipe = 1,
			.compute_pipe_count = 8 - 1,
		};

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

static u32 pool_to_domain(enum kgd_memory_pool p)
{
	switch (p) {
	case KGD_POOL_FRAMEBUFFER: return RADEON_GEM_DOMAIN_VRAM;
	default: return RADEON_GEM_DOMAIN_GTT;
	}
}

static int init_sa_manager(struct kgd_dev *kgd, unsigned int size)
{
	struct radeon_device *rdev = (struct radeon_device *)kgd;
	int r;

	BUG_ON(kgd == NULL);

	r = radeon_sa_bo_manager_init(rdev, &rdev->kfd_bo,
				      size,
				      RADEON_GPU_PAGE_SIZE,
				      RADEON_GEM_DOMAIN_GTT,
				      RADEON_GEM_GTT_WC);

	if (r)
		return r;

	r = radeon_sa_bo_manager_start(rdev, &rdev->kfd_bo);
	if (r)
		radeon_sa_bo_manager_fini(rdev, &rdev->kfd_bo);

	return r;
}

static void fini_sa_manager(struct kgd_dev *kgd)
{
	struct radeon_device *rdev = (struct radeon_device *)kgd;

	BUG_ON(kgd == NULL);

	radeon_sa_bo_manager_suspend(rdev, &rdev->kfd_bo);
	radeon_sa_bo_manager_fini(rdev, &rdev->kfd_bo);
}

static int allocate_mem(struct kgd_dev *kgd, size_t size, size_t alignment,
		enum kgd_memory_pool pool, struct kgd_mem **mem)
{
	struct radeon_device *rdev = (struct radeon_device *)kgd;
	u32 domain;
	int r;

	BUG_ON(kgd == NULL);

	domain = pool_to_domain(pool);
	if (domain != RADEON_GEM_DOMAIN_GTT) {
		dev_err(rdev->dev,
			"Only allowed to allocate gart memory for kfd\n");
		return -EINVAL;
	}

	*mem = kmalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if ((*mem) == NULL)
		return -ENOMEM;

	r = radeon_sa_bo_new(rdev, &rdev->kfd_bo, &(*mem)->sa_bo, size,
				alignment);
	if (r) {
		dev_err(rdev->dev, "failed to get memory for kfd (%d)\n", r);
		return r;
	}

	(*mem)->ptr = radeon_sa_bo_cpu_addr((*mem)->sa_bo);
	(*mem)->gpu_addr = radeon_sa_bo_gpu_addr((*mem)->sa_bo);

	return 0;
}

static void free_mem(struct kgd_dev *kgd, struct kgd_mem *mem)
{
	struct radeon_device *rdev = (struct radeon_device *)kgd;

	BUG_ON(kgd == NULL);

	radeon_sa_bo_free(rdev, &mem->sa_bo, NULL);
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
	uint32_t pasid_mapping = (pasid == 0) ? 0 :
				(uint32_t)pasid | ATC_VMID_PASID_MAPPING_VALID;

	write_register(kgd, ATC_VMID0_PASID_MAPPING + vmid*sizeof(uint32_t),
			pasid_mapping);

	while (!(read_register(kgd, ATC_VMID_PASID_MAPPING_UPDATE_STATUS) &
								(1U << vmid)))
		cpu_relax();
	write_register(kgd, ATC_VMID_PASID_MAPPING_UPDATE_STATUS, 1U << vmid);

	return 0;
}

static int kgd_init_memory(struct kgd_dev *kgd)
{
	/*
	 * Configure apertures:
	 * LDS:         0x60000000'00000000 - 0x60000001'00000000 (4GB)
	 * Scratch:     0x60000001'00000000 - 0x60000002'00000000 (4GB)
	 * GPUVM:       0x60010000'00000000 - 0x60020000'00000000 (1TB)
	 */
	int i;
	uint32_t sh_mem_bases = PRIVATE_BASE(0x6000) | SHARED_BASE(0x6000);

	for (i = 8; i < 16; i++) {
		uint32_t sh_mem_config;

		lock_srbm(kgd, 0, 0, 0, i);

		sh_mem_config = ALIGNMENT_MODE(SH_MEM_ALIGNMENT_MODE_UNALIGNED);
		sh_mem_config |= DEFAULT_MTYPE(MTYPE_NONCACHED);

		write_register(kgd, SH_MEM_CONFIG, sh_mem_config);

		write_register(kgd, SH_MEM_BASES, sh_mem_bases);

		/* Scratch aperture is not supported for now. */
		write_register(kgd, SH_STATIC_MEM_CONFIG, 0);

		/* APE1 disabled for now. */
		write_register(kgd, SH_MEM_APE1_BASE, 1);
		write_register(kgd, SH_MEM_APE1_LIMIT, 0);

		unlock_srbm(kgd);
	}

	return 0;
}

static int kgd_init_pipeline(struct kgd_dev *kgd, uint32_t pipe_id,
				uint32_t hpd_size, uint64_t hpd_gpu_addr)
{
	uint32_t mec = (++pipe_id / CIK_PIPE_PER_MEC) + 1;
	uint32_t pipe = (pipe_id % CIK_PIPE_PER_MEC);

	lock_srbm(kgd, mec, pipe, 0, 0);
	write_register(kgd, CP_HPD_EOP_BASE_ADDR,
			lower_32_bits(hpd_gpu_addr >> 8));
	write_register(kgd, CP_HPD_EOP_BASE_ADDR_HI,
			upper_32_bits(hpd_gpu_addr >> 8));
	write_register(kgd, CP_HPD_EOP_VMID, 0);
	write_register(kgd, CP_HPD_EOP_CONTROL, hpd_size);
	unlock_srbm(kgd);

	return 0;
}

static inline struct cik_mqd *get_mqd(void *mqd)
{
	return (struct cik_mqd *)mqd;
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

	case KGD_ENGINE_SDMA:
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
