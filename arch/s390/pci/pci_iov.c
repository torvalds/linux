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

int zpci_iov_setup_virtfn(struct zpci_bus *zbus, struct pci_dev *virtfn, int vfn)
{
	int i, cand_devfn;
	struct zpci_dev *zdev;
	struct pci_dev *pdev;
	int vfid = vfn - 1; /* Linux' vfid's start at 0 vfn at 1*/
	int rc = 0;

	if (!zbus->multifunction)
		return 0;

	/* If the parent PF for the given VF is also configured in the
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
			if (cand_devfn == virtfn->devfn) {
				rc = zpci_iov_link_virtfn(pdev, virtfn, vfid);
				/* balance pci_get_slot() */
				pci_dev_put(pdev);
				break;
			}
			/* balance pci_get_slot() */
			pci_dev_put(pdev);
		}
	}
	return rc;
}
