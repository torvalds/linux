/*
 * drivers/pci/ats.c
 *
 * Copyright (C) 2009 Intel Corporation, Yu Zhao <yu.zhao@intel.com>
 *
 * PCI Express I/O Virtualization (IOV) support.
 *   Address Translation Service 1.0
 */

#include <linux/pci-ats.h>
#include <linux/pci.h>

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
