/*
 * drivers/pci/ats.c
 *
 * Copyright (C) 2009 Intel Corporation, Yu Zhao <yu.zhao@intel.com>
 * Copyright (C) 2011 Advanced Micro Devices,
 *
 * PCI Express I/O Virtualization (IOV) support.
 *   Address Translation Service 1.0
 *   Page Request Interface added by Joerg Roedel <joerg.roedel@amd.com>
 *   PASID support added by Joerg Roedel <joerg.roedel@amd.com>
 */

#include <linux/export.h>
#include <linux/pci-ats.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "pci.h"

static int ats_alloc_one(struct pci_dev *dev, int ps)
{
	int pos;
	u16 cap;
	struct pci_ats *ats;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ATS);
	if (!pos)
		return -ENODEV;

	ats = kzalloc(sizeof(*ats), GFP_KERNEL);
	if (!ats)
		return -ENOMEM;

	ats->pos = pos;
	ats->stu = ps;
	pci_read_config_word(dev, pos + PCI_ATS_CAP, &cap);
	ats->qdep = PCI_ATS_CAP_QDEP(cap) ? PCI_ATS_CAP_QDEP(cap) :
					    PCI_ATS_MAX_QDEP;
	dev->ats = ats;

	return 0;
}

static void ats_free_one(struct pci_dev *dev)
{
	kfree(dev->ats);
	dev->ats = NULL;
}

/**
 * pci_enable_ats - enable the ATS capability
 * @dev: the PCI device
 * @ps: the IOMMU page shift
 *
 * Returns 0 on success, or negative on failure.
 */
int pci_enable_ats(struct pci_dev *dev, int ps)
{
	int rc;
	u16 ctrl;

	BUG_ON(dev->ats && dev->ats->is_enabled);

	if (ps < PCI_ATS_MIN_STU)
		return -EINVAL;

	if (dev->is_physfn || dev->is_virtfn) {
		struct pci_dev *pdev = dev->is_physfn ? dev : dev->physfn;

		mutex_lock(&pdev->sriov->lock);
		if (pdev->ats)
			rc = pdev->ats->stu == ps ? 0 : -EINVAL;
		else
			rc = ats_alloc_one(pdev, ps);

		if (!rc)
			pdev->ats->ref_cnt++;
		mutex_unlock(&pdev->sriov->lock);
		if (rc)
			return rc;
	}

	if (!dev->is_physfn) {
		rc = ats_alloc_one(dev, ps);
		if (rc)
			return rc;
	}

	ctrl = PCI_ATS_CTRL_ENABLE;
	if (!dev->is_virtfn)
		ctrl |= PCI_ATS_CTRL_STU(ps - PCI_ATS_MIN_STU);
	pci_write_config_word(dev, dev->ats->pos + PCI_ATS_CTRL, ctrl);

	dev->ats->is_enabled = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(pci_enable_ats);

/**
 * pci_disable_ats - disable the ATS capability
 * @dev: the PCI device
 */
void pci_disable_ats(struct pci_dev *dev)
{
	u16 ctrl;

	BUG_ON(!dev->ats || !dev->ats->is_enabled);

	pci_read_config_word(dev, dev->ats->pos + PCI_ATS_CTRL, &ctrl);
	ctrl &= ~PCI_ATS_CTRL_ENABLE;
	pci_write_config_word(dev, dev->ats->pos + PCI_ATS_CTRL, ctrl);

	dev->ats->is_enabled = 0;

	if (dev->is_physfn || dev->is_virtfn) {
		struct pci_dev *pdev = dev->is_physfn ? dev : dev->physfn;

		mutex_lock(&pdev->sriov->lock);
		pdev->ats->ref_cnt--;
		if (!pdev->ats->ref_cnt)
			ats_free_one(pdev);
		mutex_unlock(&pdev->sriov->lock);
	}

	if (!dev->is_physfn)
		ats_free_one(dev);
}
EXPORT_SYMBOL_GPL(pci_disable_ats);

void pci_restore_ats_state(struct pci_dev *dev)
{
	u16 ctrl;

	if (!pci_ats_enabled(dev))
		return;
	if (!pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ATS))
		BUG();

	ctrl = PCI_ATS_CTRL_ENABLE;
	if (!dev->is_virtfn)
		ctrl |= PCI_ATS_CTRL_STU(dev->ats->stu - PCI_ATS_MIN_STU);

	pci_write_config_word(dev, dev->ats->pos + PCI_ATS_CTRL, ctrl);
}
EXPORT_SYMBOL_GPL(pci_restore_ats_state);

/**
 * pci_ats_queue_depth - query the ATS Invalidate Queue Depth
 * @dev: the PCI device
 *
 * Returns the queue depth on success, or negative on failure.
 *
 * The ATS spec uses 0 in the Invalidate Queue Depth field to
 * indicate that the function can accept 32 Invalidate Request.
 * But here we use the `real' values (i.e. 1~32) for the Queue
 * Depth; and 0 indicates the function shares the Queue with
 * other functions (doesn't exclusively own a Queue).
 */
int pci_ats_queue_depth(struct pci_dev *dev)
{
	int pos;
	u16 cap;

	if (dev->is_virtfn)
		return 0;

	if (dev->ats)
		return dev->ats->qdep;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ATS);
	if (!pos)
		return -ENODEV;

	pci_read_config_word(dev, pos + PCI_ATS_CAP, &cap);

	return PCI_ATS_CAP_QDEP(cap) ? PCI_ATS_CAP_QDEP(cap) :
				       PCI_ATS_MAX_QDEP;
}
EXPORT_SYMBOL_GPL(pci_ats_queue_depth);

#ifdef CONFIG_PCI_PRI
/**
 * pci_enable_pri - Enable PRI capability
 * @ pdev: PCI device structure
 *
 * Returns 0 on success, negative value on error
 */
int pci_enable_pri(struct pci_dev *pdev, u32 reqs)
{
	u16 control, status;
	u32 max_requests;
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PRI);
	if (!pos)
		return -EINVAL;

	pci_read_config_word(pdev, pos + PCI_PRI_CTRL, &control);
	pci_read_config_word(pdev, pos + PCI_PRI_STATUS, &status);
	if ((control & PCI_PRI_CTRL_ENABLE) ||
	    !(status & PCI_PRI_STATUS_STOPPED))
		return -EBUSY;

	pci_read_config_dword(pdev, pos + PCI_PRI_MAX_REQ, &max_requests);
	reqs = min(max_requests, reqs);
	pci_write_config_dword(pdev, pos + PCI_PRI_ALLOC_REQ, reqs);

	control |= PCI_PRI_CTRL_ENABLE;
	pci_write_config_word(pdev, pos + PCI_PRI_CTRL, control);

	return 0;
}
EXPORT_SYMBOL_GPL(pci_enable_pri);

/**
 * pci_disable_pri - Disable PRI capability
 * @pdev: PCI device structure
 *
 * Only clears the enabled-bit, regardless of its former value
 */
void pci_disable_pri(struct pci_dev *pdev)
{
	u16 control;
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PRI);
	if (!pos)
		return;

	pci_read_config_word(pdev, pos + PCI_PRI_CTRL, &control);
	control &= ~PCI_PRI_CTRL_ENABLE;
	pci_write_config_word(pdev, pos + PCI_PRI_CTRL, control);
}
EXPORT_SYMBOL_GPL(pci_disable_pri);

/**
 * pci_pri_enabled - Checks if PRI capability is enabled
 * @pdev: PCI device structure
 *
 * Returns true if PRI is enabled on the device, false otherwise
 */
bool pci_pri_enabled(struct pci_dev *pdev)
{
	u16 control;
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PRI);
	if (!pos)
		return false;

	pci_read_config_word(pdev, pos + PCI_PRI_CTRL, &control);

	return (control & PCI_PRI_CTRL_ENABLE) ? true : false;
}
EXPORT_SYMBOL_GPL(pci_pri_enabled);

/**
 * pci_reset_pri - Resets device's PRI state
 * @pdev: PCI device structure
 *
 * The PRI capability must be disabled before this function is called.
 * Returns 0 on success, negative value on error.
 */
int pci_reset_pri(struct pci_dev *pdev)
{
	u16 control;
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PRI);
	if (!pos)
		return -EINVAL;

	pci_read_config_word(pdev, pos + PCI_PRI_CTRL, &control);
	if (control & PCI_PRI_CTRL_ENABLE)
		return -EBUSY;

	control |= PCI_PRI_CTRL_RESET;

	pci_write_config_word(pdev, pos + PCI_PRI_CTRL, control);

	return 0;
}
EXPORT_SYMBOL_GPL(pci_reset_pri);

/**
 * pci_pri_stopped - Checks whether the PRI capability is stopped
 * @pdev: PCI device structure
 *
 * Returns true if the PRI capability on the device is disabled and the
 * device has no outstanding PRI requests, false otherwise. The device
 * indicates this via the STOPPED bit in the status register of the
 * capability.
 * The device internal state can be cleared by resetting the PRI state
 * with pci_reset_pri(). This can force the capability into the STOPPED
 * state.
 */
bool pci_pri_stopped(struct pci_dev *pdev)
{
	u16 control, status;
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PRI);
	if (!pos)
		return true;

	pci_read_config_word(pdev, pos + PCI_PRI_CTRL, &control);
	pci_read_config_word(pdev, pos + PCI_PRI_STATUS, &status);

	if (control & PCI_PRI_CTRL_ENABLE)
		return false;

	return (status & PCI_PRI_STATUS_STOPPED) ? true : false;
}
EXPORT_SYMBOL_GPL(pci_pri_stopped);

/**
 * pci_pri_status - Request PRI status of a device
 * @pdev: PCI device structure
 *
 * Returns negative value on failure, status on success. The status can
 * be checked against status-bits. Supported bits are currently:
 * PCI_PRI_STATUS_RF:      Response failure
 * PCI_PRI_STATUS_UPRGI:   Unexpected Page Request Group Index
 * PCI_PRI_STATUS_STOPPED: PRI has stopped
 */
int pci_pri_status(struct pci_dev *pdev)
{
	u16 status, control;
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PRI);
	if (!pos)
		return -EINVAL;

	pci_read_config_word(pdev, pos + PCI_PRI_CTRL, &control);
	pci_read_config_word(pdev, pos + PCI_PRI_STATUS, &status);

	/* Stopped bit is undefined when enable == 1, so clear it */
	if (control & PCI_PRI_CTRL_ENABLE)
		status &= ~PCI_PRI_STATUS_STOPPED;

	return status;
}
EXPORT_SYMBOL_GPL(pci_pri_status);
#endif /* CONFIG_PCI_PRI */

#ifdef CONFIG_PCI_PASID
/**
 * pci_enable_pasid - Enable the PASID capability
 * @pdev: PCI device structure
 * @features: Features to enable
 *
 * Returns 0 on success, negative value on error. This function checks
 * whether the features are actually supported by the device and returns
 * an error if not.
 */
int pci_enable_pasid(struct pci_dev *pdev, int features)
{
	u16 control, supported;
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PASID);
	if (!pos)
		return -EINVAL;

	pci_read_config_word(pdev, pos + PCI_PASID_CTRL, &control);
	pci_read_config_word(pdev, pos + PCI_PASID_CAP, &supported);

	if (control & PCI_PASID_CTRL_ENABLE)
		return -EINVAL;

	supported &= PCI_PASID_CAP_EXEC | PCI_PASID_CAP_PRIV;

	/* User wants to enable anything unsupported? */
	if ((supported & features) != features)
		return -EINVAL;

	control = PCI_PASID_CTRL_ENABLE | features;

	pci_write_config_word(pdev, pos + PCI_PASID_CTRL, control);

	return 0;
}
EXPORT_SYMBOL_GPL(pci_enable_pasid);

/**
 * pci_disable_pasid - Disable the PASID capability
 * @pdev: PCI device structure
 *
 */
void pci_disable_pasid(struct pci_dev *pdev)
{
	u16 control = 0;
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PASID);
	if (!pos)
		return;

	pci_write_config_word(pdev, pos + PCI_PASID_CTRL, control);
}
EXPORT_SYMBOL_GPL(pci_disable_pasid);

/**
 * pci_pasid_features - Check which PASID features are supported
 * @pdev: PCI device structure
 *
 * Returns a negative value when no PASI capability is present.
 * Otherwise is returns a bitmask with supported features. Current
 * features reported are:
 * PCI_PASID_CAP_EXEC - Execute permission supported
 * PCI_PASID_CAP_PRIV - Priviledged mode supported
 */
int pci_pasid_features(struct pci_dev *pdev)
{
	u16 supported;
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PASID);
	if (!pos)
		return -EINVAL;

	pci_read_config_word(pdev, pos + PCI_PASID_CAP, &supported);

	supported &= PCI_PASID_CAP_EXEC | PCI_PASID_CAP_PRIV;

	return supported;
}
EXPORT_SYMBOL_GPL(pci_pasid_features);

#define PASID_NUMBER_SHIFT	8
#define PASID_NUMBER_MASK	(0x1f << PASID_NUMBER_SHIFT)
/**
 * pci_max_pasid - Get maximum number of PASIDs supported by device
 * @pdev: PCI device structure
 *
 * Returns negative value when PASID capability is not present.
 * Otherwise it returns the numer of supported PASIDs.
 */
int pci_max_pasids(struct pci_dev *pdev)
{
	u16 supported;
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PASID);
	if (!pos)
		return -EINVAL;

	pci_read_config_word(pdev, pos + PCI_PASID_CAP, &supported);

	supported = (supported & PASID_NUMBER_MASK) >> PASID_NUMBER_SHIFT;

	return (1 << supported);
}
EXPORT_SYMBOL_GPL(pci_max_pasids);
#endif /* CONFIG_PCI_PASID */
