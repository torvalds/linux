/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#include <linux/kconfig.h>

#if IS_REACHABLE(CONFIG_AMD_IOMMU_V2)

#include <linux/printk.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/amd-iommu.h>
#include "kfd_priv.h"
#include "kfd_dbgmgr.h"
#include "kfd_topology.h"
#include "kfd_iommu.h"

static const u32 required_iommu_flags = AMD_IOMMU_DEVICE_FLAG_ATS_SUP |
					AMD_IOMMU_DEVICE_FLAG_PRI_SUP |
					AMD_IOMMU_DEVICE_FLAG_PASID_SUP;

/** kfd_iommu_check_device - Check whether IOMMU is available for device
 */
int kfd_iommu_check_device(struct kfd_dev *kfd)
{
	struct amd_iommu_device_info iommu_info;
	int err;

	if (!kfd->use_iommu_v2)
		return -ENODEV;

	iommu_info.flags = 0;
	err = amd_iommu_device_info(kfd->pdev, &iommu_info);
	if (err)
		return err;

	if ((iommu_info.flags & required_iommu_flags) != required_iommu_flags)
		return -ENODEV;

	return 0;
}

/** kfd_iommu_device_init - Initialize IOMMU for device
 */
int kfd_iommu_device_init(struct kfd_dev *kfd)
{
	struct amd_iommu_device_info iommu_info;
	unsigned int pasid_limit;
	int err;

	if (!kfd->use_iommu_v2)
		return 0;

	iommu_info.flags = 0;
	err = amd_iommu_device_info(kfd->pdev, &iommu_info);
	if (err < 0) {
		dev_err(kfd_device,
			"error getting iommu info. is the iommu enabled?\n");
		return -ENODEV;
	}

	if ((iommu_info.flags & required_iommu_flags) != required_iommu_flags) {
		dev_err(kfd_device,
			"error required iommu flags ats %i, pri %i, pasid %i\n",
		       (iommu_info.flags & AMD_IOMMU_DEVICE_FLAG_ATS_SUP) != 0,
		       (iommu_info.flags & AMD_IOMMU_DEVICE_FLAG_PRI_SUP) != 0,
		       (iommu_info.flags & AMD_IOMMU_DEVICE_FLAG_PASID_SUP)
									!= 0);
		return -ENODEV;
	}

	pasid_limit = min_t(unsigned int,
			(unsigned int)(1 << kfd->device_info->max_pasid_bits),
			iommu_info.max_pasids);

	if (!kfd_set_pasid_limit(pasid_limit)) {
		dev_err(kfd_device, "error setting pasid limit\n");
		return -EBUSY;
	}

	return 0;
}

/** kfd_iommu_bind_process_to_device - Have the IOMMU bind a process
 *
 * Binds the given process to the given device using its PASID. This
 * enables IOMMUv2 address translation for the process on the device.
 *
 * This function assumes that the process mutex is held.
 */
int kfd_iommu_bind_process_to_device(struct kfd_process_device *pdd)
{
	struct kfd_dev *dev = pdd->dev;
	struct kfd_process *p = pdd->process;
	int err;

	if (!dev->use_iommu_v2 || pdd->bound == PDD_BOUND)
		return 0;

	if (unlikely(pdd->bound == PDD_BOUND_SUSPENDED)) {
		pr_err("Binding PDD_BOUND_SUSPENDED pdd is unexpected!\n");
		return -EINVAL;
	}

	err = amd_iommu_bind_pasid(dev->pdev, p->pasid, p->lead_thread);
	if (!err)
		pdd->bound = PDD_BOUND;

	return err;
}

/** kfd_iommu_unbind_process - Unbind process from all devices
 *
 * This removes all IOMMU device bindings of the process. To be used
 * before process termination.
 */
void kfd_iommu_unbind_process(struct kfd_process *p)
{
	struct kfd_process_device *pdd;

	list_for_each_entry(pdd, &p->per_device_data, per_device_list)
		if (pdd->bound == PDD_BOUND)
			amd_iommu_unbind_pasid(pdd->dev->pdev, p->pasid);
}

/* Callback for process shutdown invoked by the IOMMU driver */
static void iommu_pasid_shutdown_callback(struct pci_dev *pdev, u32 pasid)
{
	struct kfd_dev *dev = kfd_device_by_pci_dev(pdev);
	struct kfd_process *p;
	struct kfd_process_device *pdd;

	if (!dev)
		return;

	/*
	 * Look for the process that matches the pasid. If there is no such
	 * process, we either released it in amdkfd's own notifier, or there
	 * is a bug. Unfortunately, there is no way to tell...
	 */
	p = kfd_lookup_process_by_pasid(pasid);
	if (!p)
		return;

	pr_debug("Unbinding process 0x%x from IOMMU\n", pasid);

	mutex_lock(kfd_get_dbgmgr_mutex());

	if (dev->dbgmgr && dev->dbgmgr->pasid == p->pasid) {
		if (!kfd_dbgmgr_unregister(dev->dbgmgr, p)) {
			kfd_dbgmgr_destroy(dev->dbgmgr);
			dev->dbgmgr = NULL;
		}
	}

	mutex_unlock(kfd_get_dbgmgr_mutex());

	mutex_lock(&p->mutex);

	pdd = kfd_get_process_device_data(dev, p);
	if (pdd)
		/* For GPU relying on IOMMU, we need to dequeue here
		 * when PASID is still bound.
		 */
		kfd_process_dequeue_from_device(pdd);

	mutex_unlock(&p->mutex);

	kfd_unref_process(p);
}

/* This function called by IOMMU driver on PPR failure */
static int iommu_invalid_ppr_cb(struct pci_dev *pdev, u32 pasid,
				unsigned long address, u16 flags)
{
	struct kfd_dev *dev;

	dev_warn_ratelimited(kfd_device,
			"Invalid PPR device %x:%x.%x pasid 0x%x address 0x%lX flags 0x%X",
			pdev->bus->number,
			PCI_SLOT(pdev->devfn),
			PCI_FUNC(pdev->devfn),
			pasid,
			address,
			flags);

	dev = kfd_device_by_pci_dev(pdev);
	if (!WARN_ON(!dev))
		kfd_signal_iommu_event(dev, pasid, address,
			flags & PPR_FAULT_WRITE, flags & PPR_FAULT_EXEC);

	return AMD_IOMMU_INV_PRI_RSP_INVALID;
}

/*
 * Bind processes do the device that have been temporarily unbound
 * (PDD_BOUND_SUSPENDED) in kfd_unbind_processes_from_device.
 */
static int kfd_bind_processes_to_device(struct kfd_dev *kfd)
{
	struct kfd_process_device *pdd;
	struct kfd_process *p;
	unsigned int temp;
	int err = 0;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		mutex_lock(&p->mutex);
		pdd = kfd_get_process_device_data(kfd, p);

		if (WARN_ON(!pdd) || pdd->bound != PDD_BOUND_SUSPENDED) {
			mutex_unlock(&p->mutex);
			continue;
		}

		err = amd_iommu_bind_pasid(kfd->pdev, p->pasid,
				p->lead_thread);
		if (err < 0) {
			pr_err("Unexpected pasid 0x%x binding failure\n",
					p->pasid);
			mutex_unlock(&p->mutex);
			break;
		}

		pdd->bound = PDD_BOUND;
		mutex_unlock(&p->mutex);
	}

	srcu_read_unlock(&kfd_processes_srcu, idx);

	return err;
}

/*
 * Mark currently bound processes as PDD_BOUND_SUSPENDED. These
 * processes will be restored to PDD_BOUND state in
 * kfd_bind_processes_to_device.
 */
static void kfd_unbind_processes_from_device(struct kfd_dev *kfd)
{
	struct kfd_process_device *pdd;
	struct kfd_process *p;
	unsigned int temp;

	int idx = srcu_read_lock(&kfd_processes_srcu);

	hash_for_each_rcu(kfd_processes_table, temp, p, kfd_processes) {
		mutex_lock(&p->mutex);
		pdd = kfd_get_process_device_data(kfd, p);

		if (WARN_ON(!pdd)) {
			mutex_unlock(&p->mutex);
			continue;
		}

		if (pdd->bound == PDD_BOUND)
			pdd->bound = PDD_BOUND_SUSPENDED;
		mutex_unlock(&p->mutex);
	}

	srcu_read_unlock(&kfd_processes_srcu, idx);
}

/** kfd_iommu_suspend - Prepare IOMMU for suspend
 *
 * This unbinds processes from the device and disables the IOMMU for
 * the device.
 */
void kfd_iommu_suspend(struct kfd_dev *kfd)
{
	if (!kfd->use_iommu_v2)
		return;

	kfd_unbind_processes_from_device(kfd);

	amd_iommu_set_invalidate_ctx_cb(kfd->pdev, NULL);
	amd_iommu_set_invalid_ppr_cb(kfd->pdev, NULL);
	amd_iommu_free_device(kfd->pdev);
}

/** kfd_iommu_resume - Restore IOMMU after resume
 *
 * This reinitializes the IOMMU for the device and re-binds previously
 * suspended processes to the device.
 */
int kfd_iommu_resume(struct kfd_dev *kfd)
{
	unsigned int pasid_limit;
	int err;

	if (!kfd->use_iommu_v2)
		return 0;

	pasid_limit = kfd_get_pasid_limit();

	err = amd_iommu_init_device(kfd->pdev, pasid_limit);
	if (err)
		return -ENXIO;

	amd_iommu_set_invalidate_ctx_cb(kfd->pdev,
					iommu_pasid_shutdown_callback);
	amd_iommu_set_invalid_ppr_cb(kfd->pdev,
				     iommu_invalid_ppr_cb);

	err = kfd_bind_processes_to_device(kfd);
	if (err) {
		amd_iommu_set_invalidate_ctx_cb(kfd->pdev, NULL);
		amd_iommu_set_invalid_ppr_cb(kfd->pdev, NULL);
		amd_iommu_free_device(kfd->pdev);
		return err;
	}

	return 0;
}

extern bool amd_iommu_pc_supported(void);
extern u8 amd_iommu_pc_get_max_banks(u16 devid);
extern u8 amd_iommu_pc_get_max_counters(u16 devid);

/** kfd_iommu_add_perf_counters - Add IOMMU performance counters to topology
 */
int kfd_iommu_add_perf_counters(struct kfd_topology_device *kdev)
{
	struct kfd_perf_properties *props;

	if (!(kdev->node_props.capability & HSA_CAP_ATS_PRESENT))
		return 0;

	if (!amd_iommu_pc_supported())
		return 0;

	props = kfd_alloc_struct(props);
	if (!props)
		return -ENOMEM;
	strcpy(props->block_name, "iommu");
	props->max_concurrent = amd_iommu_pc_get_max_banks(0) *
		amd_iommu_pc_get_max_counters(0); /* assume one iommu */
	list_add_tail(&props->list, &kdev->perf_props);

	return 0;
}

#endif
