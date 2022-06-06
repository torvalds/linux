/* SPDX-License-Identifier: GPL-2.0 */
/*
 * s390 kvm PCI passthrough support
 *
 * Copyright IBM Corp. 2022
 *
 *    Author(s): Matthew Rosato <mjrosato@linux.ibm.com>
 */

#ifndef __KVM_S390_PCI_H
#define __KVM_S390_PCI_H

#include <linux/kvm_host.h>
#include <linux/pci.h>

struct kvm_zdev {
	struct zpci_dev *zdev;
	struct kvm *kvm;
};

#endif /* __KVM_S390_PCI_H */
