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

#define MQD_SIZE_ALIGNED 768

static const struct kfd_device_info kaveri_device_info = {
	.max_pasid_bits = 16,
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
};

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

struct kfd_dev *kgd2kfd_probe(struct kgd_dev *kgd, struct pci_dev *pdev)
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

bool kgd2kfd_device_init(struct kfd_dev *kfd,
			 const struct kgd2kfd_shared_resources *gpu_resources)
{
	unsigned int size;

	kfd->shared_resources = *gpu_resources;

	/* calculate max size of mqds needed for queues */
	size = max_num_of_processes *
		max_num_of_queues_per_process *
		kfd->device_info->mqd_size_aligned;

	/* add another 512KB for all other allocations on gart */
	size += 512 * 1024;

	if (kfd2kgd->init_sa_manager(kfd->kgd, size)) {
		dev_err(kfd_device,
			"Error initializing sa manager for device (%x:%x)\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto out;
	}

	kfd_doorbell_init(kfd);

	if (kfd_topology_add_device(kfd) != 0) {
		dev_err(kfd_device,
			"Error adding device (%x:%x) to topology\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto kfd_topology_add_device_error;
	}

	if (!device_iommu_pasid_init(kfd)) {
		dev_err(kfd_device,
			"Error initializing iommuv2 for device (%x:%x)\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto device_iommu_pasid_error;
	}
	amd_iommu_set_invalidate_ctx_cb(kfd->pdev,
						iommu_pasid_shutdown_callback);

	kfd->dqm = device_queue_manager_init(kfd);
	if (!kfd->dqm) {
		dev_err(kfd_device,
			"Error initializing queue manager for device (%x:%x)\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto device_queue_manager_error;
	}

	if (kfd->dqm->start(kfd->dqm) != 0) {
		dev_err(kfd_device,
			"Error starting queuen manager for device (%x:%x)\n",
			kfd->pdev->vendor, kfd->pdev->device);
		goto dqm_start_error;
	}

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
	kfd_topology_remove_device(kfd);
kfd_topology_add_device_error:
	kfd2kgd->fini_sa_manager(kfd->kgd);
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
		kfd_topology_remove_device(kfd);
	}

	kfree(kfd);
}

void kgd2kfd_suspend(struct kfd_dev *kfd)
{
	BUG_ON(kfd == NULL);

	if (kfd->init_complete) {
		kfd->dqm->stop(kfd->dqm);
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
		kfd->dqm->start(kfd->dqm);
	}

	return 0;
}

void kgd2kfd_interrupt(struct kfd_dev *dev, const void *ih_ring_entry)
{
}
