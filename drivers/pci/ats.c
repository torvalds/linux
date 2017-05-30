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

void pci_ats_init(struct pci_dev *dev)
{
	int pos;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ATS);
	if (!pos)
		return;

	dev->ats_cap = pos;
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
	u16 ctrl;
	struct pci_dev *pdev;

	if (!dev->ats_cap)
		return -EINVAL;

	if (WARN_ON(dev->ats_enabled))
		return -EBUSY;

	if (ps < PCI_ATS_MIN_STU)
		return -EINVAL;

	/*
	 * Note that enabling ATS on a VF fails unless it's already enabled
	 * with the same STU on the PF.
	 */
	ctrl = PCI_ATS_CTRL_ENABLE;
	if (dev->is_virtfn) {
		pdev = pci_physfn(dev);
		if (pdev->ats_stu != ps)
			return -EINVAL;

		atomic_inc(&pdev->ats_ref_cnt);  /* count enabled VFs */
	} else {
		dev->ats_stu = ps;
		ctrl |= PCI_ATS_CTRL_STU(dev->ats_stu - PCI_ATS_MIN_STU);
	}
	pci_write_config_word(dev, dev->ats_cap + PCI_ATS_CTRL, ctrl);

	dev->ats_enabled = 1;
	return 0;
}
EXPORT_SYMBOL_GPL(pci_enable_ats);

/**
 * pci_disable_ats - disable the ATS capability
 * @dev: the PCI device
 */
void pci_disable_ats(struct pci_dev *dev)
{
	struct pci_dev *pdev;
	u16 ctrl;

	if (WARN_ON(!dev->ats_enabled))
		return;

	if (atomic_read(&dev->ats_ref_cnt))
		return;		/* VFs still enabled */

	if (dev->is_virtfn) {
		pdev = pci_physfn(dev);
		atomic_dec(&pdev->ats_ref_cnt);
	}

	pci_read_config_word(dev, dev->ats_cap + PCI_ATS_CTRL, &ctrl);
	ctrl &= ~PCI_ATS_CTRL_ENABLE;
	pci_write_config_word(dev, dev->ats_cap + PCI_ATS_CTRL, ctrl);

	dev->ats_enabled = 0;
}
EXPORT_SYMBOL_GPL(pci_disable_ats);

void pci_restore_ats_state(struct pci_dev *dev)
{
	u16 ctrl;

	if (!dev->ats_enabled)
		return;

	ctrl = PCI_ATS_CTRL_ENABLE;
	if (!dev->is_virtfn)
		ctrl |= PCI_ATS_CTRL_STU(dev->ats_stu - PCI_ATS_MIN_STU);
	pci_write_config_word(dev, dev->ats_cap + PCI_ATS_CTRL, ctrl);
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
	u16 cap;

	if (!dev->ats_cap)
		return -EINVAL;

	if (dev->is_virtfn)
		return 0;

	pci_read_config_word(dev, dev->ats_cap + PCI_ATS_CAP, &cap);
	return PCI_ATS_CAP_QDEP(cap) ? PCI_ATS_CAP_QDEP(cap) : PCI_ATS_MAX_QDEP;
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

	if (WARN_ON(pdev->pri_enabled))
		return -EBUSY;

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

	pdev->pri_enabled = 1;

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

	if (WARN_ON(!pdev->pri_enabled))
		return;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PRI);
	if (!pos)
		return;

	pci_read_config_word(pdev, pos + PCI_PRI_CTRL, &control);
	control &= ~PCI_PRI_CTRL_ENABLE;
	pci_write_config_word(pdev, pos + PCI_PRI_CTRL, control);

	pdev->pri_enabled = 0;
}
EXPORT_SYMBOL_GPL(pci_disable_pri);

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

	if (WARN_ON(pdev->pri_enabled))
		return -EBUSY;

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

	if (WARN_ON(pdev->pasid_enabled))
		return -EBUSY;

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

	pdev->pasid_enabled = 1;

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

	if (WARN_ON(!pdev->pasid_enabled))
		return;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_PASID);
	if (!pos)
		return;

	pci_write_config_word(pdev, pos + PCI_PASID_CTRL, control);

	pdev->pasid_enabled = 0;
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
 * PCI_PASID_CAP_PRIV - Privileged mode supported
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
