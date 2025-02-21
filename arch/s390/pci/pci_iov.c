// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2020
 *
 * Author(s):
 *   Niklas Schnelle <schnelle@linux.ibm.com>
 *
 */

#define KMSG_COMPONENT "zpci"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/pci.h>

#include "pci_iov.h"

static struct resource iov_res = {
	.name	= "PCI IOV res",
	.start	= 0,
	.end	= -1,
	.flags	= IORESOURCE_MEM,
};

void zpci_iov_map_resources(struct pci_dev *pdev)
{
	resource_size_t len;
	int i;

	for (i = 0; i < PCI_SRIOV_NUM_BARS; i++) {
		int bar = i + PCI_IOV_RESOURCES;

		len = pci_resource_len(pdev, bar);
		if (!len)
			continue;
		pdev->resource[bar].parent = &iov_res;
	}
}

void zpci_iov_remove_virtfn(struct pci_dev *pdev, int vfn)
{
	pci_lock_rescan_remove();
	/* Linux' vfid's start at 0 vfn at 1 */
	pci_iov_remove_virtfn(pdev->physfn, vfn - 1);
	pci_unlock_rescan_remove();
}

static int zpci_iov_link_virtfn(struct pci_dev *pdev, struct pci_dev *virtfn, int vfid)
{
	int rc;

	rc = pci_iov_sysfs_link(pdev, virtfn, vfid);
	if (rc)
		return rc;

	virtfn->is_virtfn = 1;
	virtfn->multifunction = 0;
	virtfn->physfn = pci_dev_get(pdev);

	return 0;
}

/**
 * zpci_iov_find_parent_pf - Find the parent PF, if any, of the given function
 * @zbus:	The bus that the PCI function is on, or would be added on
 * @zdev:	The PCI function
 *
 * Finds the parent PF, if it exists and is configured, of the given PCI function
 * and increments its refcount. Th PF is searched for on the provided bus so the
 * caller has to ensure that this is the correct bus to search. This function may
 * be used before adding the PCI function to a zbus.
 *
 * Return: Pointer to the struct pci_dev of the parent PF or NULL if it not
 * found. If the function is not a VF or has no RequesterID information,
 * NULL is returned as well.
 */
struct pci_dev *zpci_iov_find_parent_pf(struct zpci_bus *zbus, struct zpci_dev *zdev)
{
	int i, vfid, devfn, cand_devfn;
	struct pci_dev *pdev;

	if (!zbus->multifunction)
		return NULL;
	/* Non-VFs and VFs without RID available don't have a parent */
	if (!zdev->vfn || !zdev->rid_available)
		return NULL;
	/* Linux vfid starts at 0 vfn at 1 */
	vfid = zdev->vfn - 1;
	devfn = zdev->rid & ZPCI_RID_MASK_DEVFN;
	/*
	 * If the parent PF for the given VF is also configured in the
	 * instance, it must be on the same zbus.
	 * We can then identify the parent PF by checking what
	 * devfn the VF would have if it belonged to that PF using the PF's
	 * stride and offset. Only if this candidate devfn matches the
	 * actual devfn will we link both functions.
	 */
	for (i = 0; i < ZPCI_FUNCTIONS_PER_BUS; i++) {
		zdev = zbus->function[i];
		if (zdev && zdev->is_physfn) {
			pdev = pci_get_slot(zbus->bus, zdev->devfn);
			if (!pdev)
				continue;
			cand_devfn = pci_iov_virtfn_devfn(pdev, vfid);
			if (cand_devfn == devfn)
				return pdev;
			/* balance pci_get_slot() */
			pci_dev_put(pdev);
		}
	}
	return NULL;
}

int zpci_iov_setup_virtfn(struct zpci_bus *zbus, struct pci_dev *virtfn, int vfn)
{
	struct zpci_dev *zdev = to_zpci(virtfn);
	struct pci_dev *pdev_pf;
	int rc = 0;

	pdev_pf = zpci_iov_find_parent_pf(zbus, zdev);
	if (pdev_pf) {
		/* Linux' vfids start at 0 while zdev->vfn starts at 1 */
		rc = zpci_iov_link_virtfn(pdev_pf, virtfn, zdev->vfn - 1);
		pci_dev_put(pdev_pf);
	}
	return rc;
}
