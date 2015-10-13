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

#include <linux/amd-iommu.h>
#include <linux/bsearch.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include "kfd_priv.h"
#include "kfd_device_queue_manager.h"
#include "kfd_pm4_headers.h"

#define MQD_SIZE_ALIGNED 768

static const struct kfd_device_info kaveri_device_info = {
	.asic_family = CHIP_KAVERI,
	.max_pasid_bits = 16,
	/* max num of queues for KV.TODO should be a dynamic value */
	.max_no_of_hqd	= 24,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED
};

static const struct kfd_device_info carrizo_device_info = {
	.asic_family = CHIP_CARRIZO,
	.max_pasid_bits = 16,
	/* max num of queues for CZ.TODO should be a dynamic value */
	.max_no_of_hqd	= 24,
	.ih_ring_entry_size = 4 * sizeof(uint32_t),
	.event_interrupt_class = &event_interrupt_class_cik,
	.num_of_watch_points = 4,
	.mqd_size_aligned = MQD_SIZE_ALIGNED
};

struct kfd_deviceid {
	unsigned short did;
	const struct kfd_device_info *device_info;
};

/* Please keep this sorted by increasing device id. */
static const struct kfd_deviceid supported_devices[] = {
	{ 0x1304, &kaveri_device_info },	/* Kaveri */
	{ 0x1305, &kaveri_device_info },	/* Kaveri */
	{ 0x1306, &kaveri_device_info },	/* Kaveri */
	{ 0x1307, &kaveri_device_info },	/* Kaveri */
	{ 0x1309, &kaveri_device_info },	/* Kaveri */
	{ 0x130A, &kaveri_device_info },	/* Kaveri */
	{ 0x130B, &kaveri_device_info },	/* Kaveri */
	{ 0x130C, &kaveri_device_info },	/* Kaveri */
	{ 0x130D, &kaveri_device_info },	/* Kaveri */
	{ 0x130E, &kaveri_device_info },	/* Kaveri */
	{ 0x130F, &kaveri_device_info },	/* Kaveri */
	{ 0x1310, &kaveri_device_info },	/* Kaveri */
	{ 0x1311, &kaveri_device_info },	/* Kaveri */
	{ 0x1312, &kaveri_device_info },	/* Kaveri */
	{ 0x1313, &kaveri_device_info },	/* Kaveri */
	{ 0x1315, &kaveri_device_info },	/* Kaveri */
	{ 0x1316, &kaveri_device_info },	/* Kaveri */
	{ 0x1317, &kaveri_device_info },	/* Kaveri */
	{ 0x1318, &kaveri_device_info },	/* Kaveri */
	{ 0x131B, &kaveri_device_info },	/* Kaveri */
	{ 0x131C, &kaveri_device_info },	/* Kaveri */
	{ 0x131D, &kaveri_device_info },	/* Kaveri */
	{ 0x9870, &carrizo_device_info },	/* Carrizo */
	{ 0x9874, &carrizo_device_info },	/* Carrizo */
	{ 0x9875, &carrizo_device_info },	/* Carrizo */
	{ 0x9876, &carrizo_device_info },	/* Carrizo */
	{ 0x9877, &carrizo_device_info }	/* Carrizo */
};

static int kfd_gtt_sa_init(struct kfd_dev *kfd, unsigned int buf_size,
				unsigned int chunk_size);
static void kfd_gtt_sa_fini(struct kfd_dev *kfd);

static const struct kfd_device_info *lookup_device_info(unsigned short did)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(supported_devices); i++) {
		if (supported_devices[i].did == did) {
			BUG_ON(supported_devices[i].device_info == NULL);
			return supported_devices[i].device_info;
		}
	}

	return NULL;
}

struct kfd_dev *kgd2kfd_probe(struct kgd_dev *kgd,
	struct pci_dev *pdev, const struct kfd2kgd_calls *f2g)
{
	struct kfd_dev *kfd;

	const struct kfd_device_info *device_info =
					lookup_device_info(pdev->device);

	if (!device_info)
		return NULL;

	kfd = kzalloc(sizeof(*kfd), GFP_KERNEL);
	if (!kfd)
		return NULL;

	kfd->kgd = kgd;
	kfd->device_info = device_info;
	kfd->pdev = pdev;
	kfd->init_complete = false;
	kfd->kfd2kgd = f2g;

	mutex_init(&kfd->doorbell_mutex);
	memset(&kfd->doorbell_available_index, 0,
		sizeof(kfd->doorbell_available_index));

	return kfd;
}

static bool device_iommu_pasid_init(struct kfd_dev *kfd)
{
	const u32 required_iommu_flags = AMD_IOMMU_DEVICE_FLAG_ATS_SUP |
					AMD_IOMMU_DEVICE_FLAG_PRI_SUP |
					AMD_IOMMU_DEVICE_FLAG_PASID_SUP;

	struct amd_iommu_device_info iommu_info;
	unsigned int pasid_limit;
	int err;

	err = amd_iommu_device_info(kfd->pdev, &iommu_info);
	if (err < 0) {
		dev_err(kfd_device,
			"error getting iommu info. is the iommu enabled?\n");
		return false;
	}

	if ((iommu_info.flags & required_iommu_flags) != required_iommu_flags) {
		dev_err(kfd_device, "error required iommu flags ats(%i), pri(%i), pasid(%i)\n",
		       (iommu_info.flags & AMD_IOMMU_DEVICE_FLAG_ATS_SUP) != 0,
		       (iommu_info.flags & AMD_IOMMU_DEVICE_FLAG_PRI_SUP) != 0,
		       (iommu_info.flags & AMD_IOMMU_DEVICE_FLAG_PASID_SUP) != 0);
		return false;
	}

	pasid_limit = min_t(unsigned int,
			(unsigned int)1 << kfd->device_info->max_pasid_bits,
			iommu_info.max_pasids);
	/*
	 * last pasid is used for kernel queues doorbells
	 * in the future the last pasid might be used for a kernel thread.
	 */
	pasid_limit = min_t(unsigned int,
				pasid_limit,
				kfd->doorbell_process_limit - 1);

	err = amd_iommu_init_device(kfd->pdev, pasid_limit);
	if (err < 0) {
		dev_err(kfd_device, "error initializing iommu device\n");
		return false;
	}

	if (!kfd_set_pasid_limit(pasid_limit)) {
		dev_err(kfd_device, "error setting pasid limit\n");
		amd_iommu_free_device(kfd->pdev);
		return false;
	}

	return true;
}

static void iommu_pasid_shutdown_callback(struct pci_dev *pdev, int pasid)
{
	struct kfd_dev *dev = kfd_device_by_pci_dev(pdev);

	if (dev)
		kfd_unbind_process_from_device(dev, pasid);
}

/*
 * This function called by IOMMU driver on PPR failure
 */
static int iommu_invalid_ppr_cb(struct pci_dev *pdev, int pasid,
		unsigned long address, u16 flags)
{
	struct kfd_dev *dev;

	dev_warn(kfd_device,
			"Invalid PPR device %x:%x.%x pasid %d address 0x%lX flags 0x%X",
			PCI_BUS_NUM(pdev->devfn),
			PCI_SLOT(pdev->devfn),
			PCI_FUNC(pdev->devfn),
			pasid,
			address,
			flags);

	dev = kfd_device_by_pci_dev(pdev);
	BUG_ON(dev == NULL);

	kfd_signal_iommu_event(dev, pasid, address,
			flags & PPR_FAULT_WRITE, flags & PPR_FAULT_EXEC);

	return AMD_IOMMU_INV_PRI_RSP_INVALID;
}

bool kgd2kfd_device_init(struct kfd_dev *kfd,
			 const struct kgd2kfd_shared_resources *gpu_resources)
{
	unsigned int size;

	kfd->shared_resources = *gpu_resources;

	/* calculate max size of mqds needed for queues */
	size = max_num_of_queues_per_device *
			kfd->device_info->mqd_size_aligned;

	/*
	 * calculate max size of runlist packet.
	 * There can be only 2 packets at once
	 */
	size += (KFD_MAX_NUM_OF_PROCESSES * sizeof(struct pm4_map_process) +
		max_num_of_queues_per_device *
		sizeof(struct pm4_map_queues) + sizeof(struct pm4_runlist)) * 2;

	/* Add size of HIQ & DIQ */
	size += KFD_KERNEL_QUEUE_SIZE * 2;

	/* add another 512KB for all other allocations on gart (HPD, fences) */
	size += 512 * 1024;

	if (kfd->kfd2kgd->init_gtt_mem_allocation(
			kfd->kgd, size, &kfd->gtt_mem,
			&kfd->gtt_start_gpu_addr, &kfd->gtt_start_cpu_ptr)){
		dev_err(kfd_device,
			"Could not allocate %d bytes for device (%x:%x)\n",
			size, kfd->pdev->vendor, kfd->pdev->device);
		goto out;
	}

	dev_info(kfd_device,
		"Allocated %d bytes on gart for device(%x:%x)\n",
		size, kfd->pdev->vendor, kfd->pdev->device);

	/* Initialize GTT sa with 512 byte chunk size */
	if (kfd_gtt_sa_init(kfd, size, 512) != 0) {
		dev_err(kfd_device,
			"Error initializing gtt sub-allocator\n");
		goto kfd_gtt_sa_init_error;
	}

	kfd_doorbell_init(kfd);

	if (kfd_topology_add_device(kfd) != 0) {
		dev_err(kfd_device,
			"Error adding device (%x:%x) to topology\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto kfd_topology_add_device_error;
	}

	if (kfd_interrupt_init(kfd)) {
		dev_err(kfd_device,
			"Error initializing interrupts for device (%x:%x)\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto kfd_interrupt_error;
	}

	if (!device_iommu_pasid_init(kfd)) {
		dev_err(kfd_device,
			"Error initializing iommuv2 for device (%x:%x)\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto device_iommu_pasid_error;
	}
	amd_iommu_set_invalidate_ctx_cb(kfd->pdev,
						iommu_pasid_shutdown_callback);
	amd_iommu_set_invalid_ppr_cb(kfd->pdev, iommu_invalid_ppr_cb);

	kfd->dqm = device_queue_manager_init(kfd);
	if (!kfd->dqm) {
		dev_err(kfd_device,
			"Error initializing queue manager for device (%x:%x)\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto device_queue_manager_error;
	}

	if (kfd->dqm->ops.start(kfd->dqm) != 0) {
		dev_err(kfd_device,
			"Error starting queuen manager for device (%x:%x)\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto dqm_start_error;
	}

	kfd->dbgmgr = NULL;

	kfd->init_complete = true;
	dev_info(kfd_device, "added device (%x:%x)\n", kfd->pdev->vendor,
		 kfd->pdev->device);

	pr_debug("kfd: Starting kfd with the following scheduling policy %d\n",
		sched_policy);

	goto out;

dqm_start_error:
	device_queue_manager_uninit(kfd->dqm);
device_queue_manager_error:
	amd_iommu_free_device(kfd->pdev);
device_iommu_pasid_error:
	kfd_interrupt_exit(kfd);
kfd_interrupt_error:
	kfd_topology_remove_device(kfd);
kfd_topology_add_device_error:
	kfd_gtt_sa_fini(kfd);
kfd_gtt_sa_init_error:
	kfd->kfd2kgd->free_gtt_mem(kfd->kgd, kfd->gtt_mem);
	dev_err(kfd_device,
		"device (%x:%x) NOT added due to errors\n",
		kfd->pdev->vendor, kfd->pdev->device);
out:
	return kfd->init_complete;
}

void kgd2kfd_device_exit(struct kfd_dev *kfd)
{
	if (kfd->init_complete) {
		device_queue_manager_uninit(kfd->dqm);
		amd_iommu_free_device(kfd->pdev);
		kfd_interrupt_exit(kfd);
		kfd_topology_remove_device(kfd);
		kfd_gtt_sa_fini(kfd);
		kfd->kfd2kgd->free_gtt_mem(kfd->kgd, kfd->gtt_mem);
	}

	kfree(kfd);
}

void kgd2kfd_suspend(struct kfd_dev *kfd)
{
	BUG_ON(kfd == NULL);

	if (kfd->init_complete) {
		kfd->dqm->ops.stop(kfd->dqm);
		amd_iommu_set_invalidate_ctx_cb(kfd->pdev, NULL);
		amd_iommu_set_invalid_ppr_cb(kfd->pdev, NULL);
		amd_iommu_free_device(kfd->pdev);
	}
}

int kgd2kfd_resume(struct kfd_dev *kfd)
{
	unsigned int pasid_limit;
	int err;

	BUG_ON(kfd == NULL);

	pasid_limit = kfd_get_pasid_limit();

	if (kfd->init_complete) {
		err = amd_iommu_init_device(kfd->pdev, pasid_limit);
		if (err < 0)
			return -ENXIO;
		amd_iommu_set_invalidate_ctx_cb(kfd->pdev,
						iommu_pasid_shutdown_callback);
		amd_iommu_set_invalid_ppr_cb(kfd->pdev, iommu_invalid_ppr_cb);
		kfd->dqm->ops.start(kfd->dqm);
	}

	return 0;
}

/* This is called directly from KGD at ISR. */
void kgd2kfd_interrupt(struct kfd_dev *kfd, const void *ih_ring_entry)
{
	if (!kfd->init_complete)
		return;

	spin_lock(&kfd->interrupt_lock);

	if (kfd->interrupts_active
	    && interrupt_is_wanted(kfd, ih_ring_entry)
	    && enqueue_ih_ring_entry(kfd, ih_ring_entry))
		schedule_work(&kfd->interrupt_work);

	spin_unlock(&kfd->interrupt_lock);
}

static int kfd_gtt_sa_init(struct kfd_dev *kfd, unsigned int buf_size,
				unsigned int chunk_size)
{
	unsigned int num_of_bits;

	BUG_ON(!kfd);
	BUG_ON(!kfd->gtt_mem);
	BUG_ON(buf_size < chunk_size);
	BUG_ON(buf_size == 0);
	BUG_ON(chunk_size == 0);

	kfd->gtt_sa_chunk_size = chunk_size;
	kfd->gtt_sa_num_of_chunks = buf_size / chunk_size;

	num_of_bits = kfd->gtt_sa_num_of_chunks / BITS_PER_BYTE;
	BUG_ON(num_of_bits == 0);

	kfd->gtt_sa_bitmap = kzalloc(num_of_bits, GFP_KERNEL);

	if (!kfd->gtt_sa_bitmap)
		return -ENOMEM;

	pr_debug("kfd: gtt_sa_num_of_chunks = %d, gtt_sa_bitmap = %p\n",
			kfd->gtt_sa_num_of_chunks, kfd->gtt_sa_bitmap);

	mutex_init(&kfd->gtt_sa_lock);

	return 0;

}

static void kfd_gtt_sa_fini(struct kfd_dev *kfd)
{
	mutex_destroy(&kfd->gtt_sa_lock);
	kfree(kfd->gtt_sa_bitmap);
}

static inline uint64_t kfd_gtt_sa_calc_gpu_addr(uint64_t start_addr,
						unsigned int bit_num,
						unsigned int chunk_size)
{
	return start_addr + bit_num * chunk_size;
}

static inline uint32_t *kfd_gtt_sa_calc_cpu_addr(void *start_addr,
						unsigned int bit_num,
						unsigned int chunk_size)
{
	return (uint32_t *) ((uint64_t) start_addr + bit_num * chunk_size);
}

int kfd_gtt_sa_allocate(struct kfd_dev *kfd, unsigned int size,
			struct kfd_mem_obj **mem_obj)
{
	unsigned int found, start_search, cur_size;

	BUG_ON(!kfd);

	if (size == 0)
		return -EINVAL;

	if (size > kfd->gtt_sa_num_of_chunks * kfd->gtt_sa_chunk_size)
		return -ENOMEM;

	*mem_obj = kmalloc(sizeof(struct kfd_mem_obj), GFP_KERNEL);
	if ((*mem_obj) == NULL)
		return -ENOMEM;

	pr_debug("kfd: allocated mem_obj = %p for size = %d\n", *mem_obj, size);

	start_search = 0;

	mutex_lock(&kfd->gtt_sa_lock);

kfd_gtt_restart_search:
	/* Find the first chunk that is free */
	found = find_next_zero_bit(kfd->gtt_sa_bitmap,
					kfd->gtt_sa_num_of_chunks,
					start_search);

	pr_debug("kfd: found = %d\n", found);

	/* If there wasn't any free chunk, bail out */
	if (found == kfd->gtt_sa_num_of_chunks)
		goto kfd_gtt_no_free_chunk;

	/* Update fields of mem_obj */
	(*mem_obj)->range_start = found;
	(*mem_obj)->range_end = found;
	(*mem_obj)->gpu_addr = kfd_gtt_sa_calc_gpu_addr(
					kfd->gtt_start_gpu_addr,
					found,
					kfd->gtt_sa_chunk_size);
	(*mem_obj)->cpu_ptr = kfd_gtt_sa_calc_cpu_addr(
					kfd->gtt_start_cpu_ptr,
					found,
					kfd->gtt_sa_chunk_size);

	pr_debug("kfd: gpu_addr = %p, cpu_addr = %p\n",
			(uint64_t *) (*mem_obj)->gpu_addr, (*mem_obj)->cpu_ptr);

	/* If we need only one chunk, mark it as allocated and get out */
	if (size <= kfd->gtt_sa_chunk_size) {
		pr_debug("kfd: single bit\n");
		set_bit(found, kfd->gtt_sa_bitmap);
		goto kfd_gtt_out;
	}

	/* Otherwise, try to see if we have enough contiguous chunks */
	cur_size = size - kfd->gtt_sa_chunk_size;
	do {
		(*mem_obj)->range_end =
			find_next_zero_bit(kfd->gtt_sa_bitmap,
					kfd->gtt_sa_num_of_chunks, ++found);
		/*
		 * If next free chunk is not contiguous than we need to
		 * restart our search from the last free chunk we found (which
		 * wasn't contiguous to the previous ones
		 */
		if ((*mem_obj)->range_end != found) {
			start_search = found;
			goto kfd_gtt_restart_search;
		}

		/*
		 * If we reached end of buffer, bail out with error
		 */
		if (found == kfd->gtt_sa_num_of_chunks)
			goto kfd_gtt_no_free_chunk;

		/* Check if we don't need another chunk */
		if (cur_size <= kfd->gtt_sa_chunk_size)
			cur_size = 0;
		else
			cur_size -= kfd->gtt_sa_chunk_size;

	} while (cur_size > 0);

	pr_debug("kfd: range_start = %d, range_end = %d\n",
		(*mem_obj)->range_start, (*mem_obj)->range_end);

	/* Mark the chunks as allocated */
	for (found = (*mem_obj)->range_start;
		found <= (*mem_obj)->range_end;
		found++)
		set_bit(found, kfd->gtt_sa_bitmap);

kfd_gtt_out:
	mutex_unlock(&kfd->gtt_sa_lock);
	return 0;

kfd_gtt_no_free_chunk:
	pr_debug("kfd: allocation failed with mem_obj = %p\n", mem_obj);
	mutex_unlock(&kfd->gtt_sa_lock);
	kfree(mem_obj);
	return -ENOMEM;
}

int kfd_gtt_sa_free(struct kfd_dev *kfd, struct kfd_mem_obj *mem_obj)
{
	unsigned int bit;

	BUG_ON(!kfd);

	/* Act like kfree when trying to free a NULL object */
	if (!mem_obj)
		return 0;

	pr_debug("kfd: free mem_obj = %p, range_start = %d, range_end = %d\n",
			mem_obj, mem_obj->range_start, mem_obj->range_end);

	mutex_lock(&kfd->gtt_sa_lock);

	/* Mark the chunks as free */
	for (bit = mem_obj->range_start;
		bit <= mem_obj->range_end;
		bit++)
		clear_bit(bit, kfd->gtt_sa_bitmap);

	mutex_unlock(&kfd->gtt_sa_lock);

	kfree(mem_obj);
	return 0;
}
