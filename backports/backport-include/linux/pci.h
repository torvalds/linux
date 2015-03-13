#ifndef _BACKPORT_LINUX_PCI_H
#define _BACKPORT_LINUX_PCI_H
#include_next <linux/pci.h>
#include <linux/version.h>

#ifndef module_pci_driver
/**
 * module_pci_driver() - Helper macro for registering a PCI driver
 * @__pci_driver: pci_driver struct
 *
 * Helper macro for PCI drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_pci_driver(__pci_driver) \
	module_driver(__pci_driver, pci_register_driver, \
		       pci_unregister_driver)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
#define pcie_capability_read_word LINUX_BACKPORT(pcie_capability_read_word)
int pcie_capability_read_word(struct pci_dev *dev, int pos, u16 *val);
#define pcie_capability_read_dword LINUX_BACKPORT(pcie_capability_read_dword)
int pcie_capability_read_dword(struct pci_dev *dev, int pos, u32 *val);
#define pcie_capability_write_word LINUX_BACKPORT(pcie_capability_write_word)
int pcie_capability_write_word(struct pci_dev *dev, int pos, u16 val);
#define pcie_capability_write_dword LINUX_BACKPORT(pcie_capability_write_dword)
int pcie_capability_write_dword(struct pci_dev *dev, int pos, u32 val);
#define pcie_capability_clear_and_set_word LINUX_BACKPORT(pcie_capability_clear_and_set_word)
int pcie_capability_clear_and_set_word(struct pci_dev *dev, int pos,
				       u16 clear, u16 set);
#define pcie_capability_clear_and_set_dword LINUX_BACKPORT(pcie_capability_clear_and_set_dword)
int pcie_capability_clear_and_set_dword(struct pci_dev *dev, int pos,
					u32 clear, u32 set);

#define pcie_capability_set_word LINUX_BACKPORT(pcie_capability_set_word)
static inline int pcie_capability_set_word(struct pci_dev *dev, int pos,
					   u16 set)
{
	return pcie_capability_clear_and_set_word(dev, pos, 0, set);
}

#define pcie_capability_set_dword LINUX_BACKPORT(pcie_capability_set_dword)
static inline int pcie_capability_set_dword(struct pci_dev *dev, int pos,
					    u32 set)
{
	return pcie_capability_clear_and_set_dword(dev, pos, 0, set);
}

#define pcie_capability_clear_word LINUX_BACKPORT(pcie_capability_clear_word)
static inline int pcie_capability_clear_word(struct pci_dev *dev, int pos,
					     u16 clear)
{
	return pcie_capability_clear_and_set_word(dev, pos, clear, 0);
}

#define pcie_capability_clear_dword LINUX_BACKPORT(pcie_capability_clear_dword)
static inline int pcie_capability_clear_dword(struct pci_dev *dev, int pos,
					      u32 clear)
{
	return pcie_capability_clear_and_set_dword(dev, pos, clear, 0);
}
#endif

#ifndef PCI_DEVICE_SUB
/**
 * PCI_DEVICE_SUB - macro used to describe a specific pci device with subsystem
 * @vend: the 16 bit PCI Vendor ID
 * @dev: the 16 bit PCI Device ID
 * @subvend: the 16 bit PCI Subvendor ID
 * @subdev: the 16 bit PCI Subdevice ID
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific device with subsystem information.
 */
#define PCI_DEVICE_SUB(vend, dev, subvend, subdev) \
	.vendor = (vend), .device = (dev), \
	.subvendor = (subvend), .subdevice = (subdev)
#endif /* PCI_DEVICE_SUB */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
#define pci_dev_flags LINUX_BACKPORT(pci_dev_flags)
#define PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG LINUX_BACKPORT(PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG)
#define PCI_DEV_FLAGS_NO_D3 LINUX_BACKPORT(PCI_DEV_FLAGS_NO_D3)
#define PCI_DEV_FLAGS_ASSIGNED LINUX_BACKPORT(PCI_DEV_FLAGS_ASSIGNED)
enum pci_dev_flags {
	/* INTX_DISABLE in PCI_COMMAND register disables MSI
	 * generation too.
	 */
	PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG = (__force pci_dev_flags_t) 1,
	/* Device configuration is irrevocably lost if disabled into D3 */
	PCI_DEV_FLAGS_NO_D3 = (__force pci_dev_flags_t) 2,
	/* Provide indication device is assigned by a Virtual Machine Manager */
	PCI_DEV_FLAGS_ASSIGNED = (__force pci_dev_flags_t) 4,
};
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)
#define pci_sriov_set_totalvfs LINUX_BACKPORT(pci_sriov_set_totalvfs)
int pci_sriov_set_totalvfs(struct pci_dev *dev, u16 numvfs);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
/* Taken from drivers/pci/pci.h */
struct pci_sriov {
	int pos;		/* capability position */
	int nres;		/* number of resources */
	u32 cap;		/* SR-IOV Capabilities */
	u16 ctrl;		/* SR-IOV Control */
	u16 total_VFs;		/* total VFs associated with the PF */
	u16 initial_VFs;	/* initial VFs associated with the PF */
	u16 num_VFs;		/* number of VFs available */
	u16 offset;		/* first VF Routing ID offset */
	u16 stride;		/* following VF stride */
	u32 pgsz;		/* page size for BAR alignment */
	u8 link;		/* Function Dependency Link */
	u16 driver_max_VFs;	/* max num VFs driver supports */
	struct pci_dev *dev;	/* lowest numbered PF */
	struct pci_dev *self;	/* this PF */
	struct mutex lock;	/* lock for VF bus */
	struct work_struct mtask; /* VF Migration task */
	u8 __iomem *mstate;	/* VF Migration State Array */
};

#define pci_vfs_assigned LINUX_BACKPORT(pci_vfs_assigned)
#ifdef CONFIG_PCI_IOV
int pci_vfs_assigned(struct pci_dev *dev);
#else
static inline int pci_vfs_assigned(struct pci_dev *dev)
{
	return 0;
}
#endif

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0))
#define pci_enable_msi_range LINUX_BACKPORT(pci_enable_msi_range)
#ifdef CONFIG_PCI_MSI
int pci_enable_msi_range(struct pci_dev *dev, int minvec, int maxvec);
#else
static inline int pci_enable_msi_range(struct pci_dev *dev, int minvec,
				       int maxvec)
{ return -ENOSYS; }
#endif
#endif

#ifdef CONFIG_PCI
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
#define pci_enable_msix_range LINUX_BACKPORT(pci_enable_msix_range)
#ifdef CONFIG_PCI_MSI
int pci_enable_msix_range(struct pci_dev *dev, struct msix_entry *entries,
			  int minvec, int maxvec);
#else
static inline int pci_enable_msix_range(struct pci_dev *dev,
		      struct msix_entry *entries, int minvec, int maxvec)
{ return -ENOSYS; }
#endif
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
#define pci_device_is_present LINUX_BACKPORT(pci_device_is_present)
bool pci_device_is_present(struct pci_dev *pdev);
#endif

#endif /* _BACKPORT_LINUX_PCI_H */
