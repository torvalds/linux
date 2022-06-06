// SPDX-License-Identifier: GPL-2.0
/*
 * s390 kvm PCI passthrough support
 *
 * Copyright IBM Corp. 2022
 *
 *    Author(s): Matthew Rosato <mjrosato@linux.ibm.com>
 */

#include <linux/kvm_host.h>
#include <linux/pci.h>
#include "pci.h"

static int kvm_s390_pci_dev_open(struct zpci_dev *zdev)
{
	struct kvm_zdev *kzdev;

	kzdev = kzalloc(sizeof(struct kvm_zdev), GFP_KERNEL);
	if (!kzdev)
		return -ENOMEM;

	kzdev->zdev = zdev;
	zdev->kzdev = kzdev;

	return 0;
}

static void kvm_s390_pci_dev_release(struct zpci_dev *zdev)
{
	struct kvm_zdev *kzdev;

	kzdev = zdev->kzdev;
	WARN_ON(kzdev->zdev != zdev);
	zdev->kzdev = NULL;
	kfree(kzdev);
}
