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

#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <asm/airq.h>
#include <asm/cpu.h>

struct kvm_zdev {
	struct zpci_dev *zdev;
	struct kvm *kvm;
	struct zpci_fib fib;
	struct list_head entry;
};

struct zpci_gaite {
	u32 gisa;
	u8 gisc;
	u8 count;
	u8 reserved;
	u8 aisbo;
	u64 aisb;
};

struct zpci_aift {
	struct zpci_gaite *gait;
	struct airq_iv *sbv;
	struct kvm_zdev **kzdev;
	spinlock_t gait_lock; /* Protects the gait, used during AEN forward */
	struct mutex aift_lock; /* Protects the other structures in aift */
};

extern struct zpci_aift *aift;

static inline struct kvm *kvm_s390_pci_si_to_kvm(struct zpci_aift *aift,
						 unsigned long si)
{
	if (!IS_ENABLED(CONFIG_VFIO_PCI_ZDEV_KVM) || !aift->kzdev ||
	    !aift->kzdev[si])
		return NULL;
	return aift->kzdev[si]->kvm;
};

int kvm_s390_pci_aen_init(u8 nisc);
void kvm_s390_pci_aen_exit(void);

void kvm_s390_pci_init_list(struct kvm *kvm);
void kvm_s390_pci_clear_list(struct kvm *kvm);

int kvm_s390_pci_zpci_op(struct kvm *kvm, struct kvm_s390_zpci_op *args);

int kvm_s390_pci_init(void);
void kvm_s390_pci_exit(void);

static inline bool kvm_s390_pci_interp_allowed(void)
{
	struct cpuid cpu_id;

	get_cpu_id(&cpu_id);
	switch (cpu_id.machine) {
	case 0x2817:
	case 0x2818:
	case 0x2827:
	case 0x2828:
	case 0x2964:
	case 0x2965:
		/* No SHM on certain machines */
		return false;
	default:
		return (IS_ENABLED(CONFIG_VFIO_PCI_ZDEV_KVM) &&
			sclp.has_zpci_lsi && sclp.has_aeni && sclp.has_aisi &&
			sclp.has_aisii);
	}
}

#endif /* __KVM_S390_PCI_H */
