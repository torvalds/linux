// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2015 - 2021 Intel Corporation */
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/device.h>
#include "adf_common_drv.h"
#include "adf_cfg.h"
#include "adf_pfvf_pf_msg.h"

#define ADF_VF2PF_RATELIMIT_INTERVAL	8
#define ADF_VF2PF_RATELIMIT_BURST	130

static struct workqueue_struct *pf2vf_resp_wq;

struct adf_pf2vf_resp {
	struct work_struct pf2vf_resp_work;
	struct adf_accel_vf_info *vf_info;
};

static void adf_iov_send_resp(struct work_struct *work)
{
	struct adf_pf2vf_resp *pf2vf_resp =
		container_of(work, struct adf_pf2vf_resp, pf2vf_resp_work);
	struct adf_accel_vf_info *vf_info = pf2vf_resp->vf_info;
	struct adf_accel_dev *accel_dev = vf_info->accel_dev;
	u32 vf_nr = vf_info->vf_nr;
	bool ret;

	ret = adf_recv_and_handle_vf2pf_msg(accel_dev, vf_nr);
	if (ret)
		/* re-enable interrupt on PF from this VF */
		adf_enable_vf2pf_interrupts(accel_dev, 1 << vf_nr);

	kfree(pf2vf_resp);
}

void adf_schedule_vf2pf_handler(struct adf_accel_vf_info *vf_info)
{
	struct adf_pf2vf_resp *pf2vf_resp;

	pf2vf_resp = kzalloc(sizeof(*pf2vf_resp), GFP_ATOMIC);
	if (!pf2vf_resp)
		return;

	pf2vf_resp->vf_info = vf_info;
	INIT_WORK(&pf2vf_resp->pf2vf_resp_work, adf_iov_send_resp);
	queue_work(pf2vf_resp_wq, &pf2vf_resp->pf2vf_resp_work);
}

static int adf_enable_sriov(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);
	int totalvfs = pci_sriov_get_totalvfs(pdev);
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_accel_vf_info *vf_info;
	int i;

	for (i = 0, vf_info = accel_dev->pf.vf_info; i < totalvfs;
	     i++, vf_info++) {
		/* This ptr will be populated when VFs will be created */
		vf_info->accel_dev = accel_dev;
		vf_info->vf_nr = i;
		vf_info->vf_compat_ver = 0;

		mutex_init(&vf_info->pf2vf_lock);
		ratelimit_state_init(&vf_info->vf2pf_ratelimit,
				     ADF_VF2PF_RATELIMIT_INTERVAL,
				     ADF_VF2PF_RATELIMIT_BURST);
	}

	/* Set Valid bits in AE Thread to PCIe Function Mapping */
	if (hw_data->configure_iov_threads)
		hw_data->configure_iov_threads(accel_dev, true);

	/* Enable VF to PF interrupts for all VFs */
	adf_enable_vf2pf_interrupts(accel_dev, BIT_ULL(totalvfs) - 1);

	/*
	 * Due to the hardware design, when SR-IOV and the ring arbiter
	 * are enabled all the VFs supported in hardware must be enabled in
	 * order for all the hardware resources (i.e. bundles) to be usable.
	 * When SR-IOV is enabled, each of the VFs will own one bundle.
	 */
	return pci_enable_sriov(pdev, totalvfs);
}

/**
 * adf_disable_sriov() - Disable SRIOV for the device
 * @accel_dev:  Pointer to accel device.
 *
 * Function disables SRIOV for the accel device.
 *
 * Return: 0 on success, error code otherwise.
 */
void adf_disable_sriov(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	int totalvfs = pci_sriov_get_totalvfs(accel_to_pci_dev(accel_dev));
	struct adf_accel_vf_info *vf;
	int i;

	if (!accel_dev->pf.vf_info)
		return;

	adf_pf2vf_notify_restarting(accel_dev);
	pci_disable_sriov(accel_to_pci_dev(accel_dev));

	/* Disable VF to PF interrupts */
	adf_disable_all_vf2pf_interrupts(accel_dev);

	/* Clear Valid bits in AE Thread to PCIe Function Mapping */
	if (hw_data->configure_iov_threads)
		hw_data->configure_iov_threads(accel_dev, false);

	for (i = 0, vf = accel_dev->pf.vf_info; i < totalvfs; i++, vf++)
		mutex_destroy(&vf->pf2vf_lock);

	kfree(accel_dev->pf.vf_info);
	accel_dev->pf.vf_info = NULL;
}
EXPORT_SYMBOL_GPL(adf_disable_sriov);

/**
 * adf_sriov_configure() - Enable SRIOV for the device
 * @pdev:  Pointer to PCI device.
 * @numvfs: Number of virtual functions (VFs) to enable.
 *
 * Note that the @numvfs parameter is ignored and all VFs supported by the
 * device are enabled due to the design of the hardware.
 *
 * Function enables SRIOV for the PCI device.
 *
 * Return: number of VFs enabled on success, error code otherwise.
 */
int adf_sriov_configure(struct pci_dev *pdev, int numvfs)
{
	struct adf_accel_dev *accel_dev = adf_devmgr_pci_to_accel_dev(pdev);
	int totalvfs = pci_sriov_get_totalvfs(pdev);
	unsigned long val;
	int ret;

	if (!accel_dev) {
		dev_err(&pdev->dev, "Failed to find accel_dev\n");
		return -EFAULT;
	}

	if (!device_iommu_mapped(&pdev->dev))
		dev_warn(&pdev->dev, "IOMMU should be enabled for SR-IOV to work correctly\n");

	if (accel_dev->pf.vf_info) {
		dev_info(&pdev->dev, "Already enabled for this device\n");
		return -EINVAL;
	}

	if (adf_dev_started(accel_dev)) {
		if (adf_devmgr_in_reset(accel_dev) ||
		    adf_dev_in_use(accel_dev)) {
			dev_err(&GET_DEV(accel_dev), "Device busy\n");
			return -EBUSY;
		}

		ret = adf_dev_down(accel_dev, true);
		if (ret)
			return ret;
	}

	if (adf_cfg_section_add(accel_dev, ADF_KERNEL_SEC))
		return -EFAULT;
	val = 0;
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					ADF_NUM_CY, (void *)&val, ADF_DEC))
		return -EFAULT;
	ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, ADF_NUM_DC,
					  &val, ADF_DEC);
	if (ret)
		return ret;

	set_bit(ADF_STATUS_CONFIGURED, &accel_dev->status);

	/* Allocate memory for VF info structs */
	accel_dev->pf.vf_info = kcalloc(totalvfs,
					sizeof(struct adf_accel_vf_info),
					GFP_KERNEL);
	if (!accel_dev->pf.vf_info)
		return -ENOMEM;

	if (adf_dev_up(accel_dev, false)) {
		dev_err(&GET_DEV(accel_dev), "Failed to start qat_dev%d\n",
			accel_dev->accel_id);
		return -EFAULT;
	}

	ret = adf_enable_sriov(accel_dev);
	if (ret)
		return ret;

	return numvfs;
}
EXPORT_SYMBOL_GPL(adf_sriov_configure);

int __init adf_init_pf_wq(void)
{
	/* Workqueue for PF2VF responses */
	pf2vf_resp_wq = alloc_workqueue("qat_pf2vf_resp_wq", WQ_MEM_RECLAIM, 0);

	return !pf2vf_resp_wq ? -ENOMEM : 0;
}

void adf_exit_pf_wq(void)
{
	if (pf2vf_resp_wq) {
		destroy_workqueue(pf2vf_resp_wq);
		pf2vf_resp_wq = NULL;
	}
}
