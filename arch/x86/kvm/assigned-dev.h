#ifndef ARCH_X86_KVM_ASSIGNED_DEV_H
#define ARCH_X86_KVM_ASSIGNED_DEV_H

#include <linux/kvm_host.h>

#ifdef CONFIG_KVM_DEVICE_ASSIGNMENT
int kvm_assign_device(struct kvm *kvm, struct pci_dev *pdev);
int kvm_deassign_device(struct kvm *kvm, struct pci_dev *pdev);

int kvm_iommu_map_guest(struct kvm *kvm);
int kvm_iommu_unmap_guest(struct kvm *kvm);

long kvm_vm_ioctl_assigned_device(struct kvm *kvm, unsigned ioctl,
				  unsigned long arg);

void kvm_free_all_assigned_devices(struct kvm *kvm);
#else
static inline int kvm_iommu_unmap_guest(struct kvm *kvm)
{
	return 0;
}

static inline long kvm_vm_ioctl_assigned_device(struct kvm *kvm, unsigned ioctl,
						unsigned long arg)
{
	return -ENOTTY;
}

static inline void kvm_free_all_assigned_devices(struct kvm *kvm) {}
#endif /* CONFIG_KVM_DEVICE_ASSIGNMENT */

#endif /* ARCH_X86_KVM_ASSIGNED_DEV_H */
