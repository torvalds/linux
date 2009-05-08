#ifndef DRIVERS_PCI_H
#define DRIVERS_PCI_H

#include <linux/workqueue.h>

#define PCI_CFG_SPACE_SIZE	256
#define PCI_CFG_SPACE_EXP_SIZE	4096

/* Functions internal to the PCI core code */

extern int pci_uevent(struct device *dev, struct kobj_uevent_env *env);
extern int pci_create_sysfs_dev_files(struct pci_dev *pdev);
extern void pci_remove_sysfs_dev_files(struct pci_dev *pdev);
extern void pci_cleanup_rom(struct pci_dev *dev);
#ifdef HAVE_PCI_MMAP
extern int pci_mmap_fits(struct pci_dev *pdev, int resno,
			 struct vm_area_struct *vma);
#endif

/**
 * struct pci_platform_pm_ops - Firmware PM callbacks
 *
 * @is_manageable: returns 'true' if given device is power manageable by the
 *                 platform firmware
 *
 * @set_state: invokes the platform firmware to set the device's power state
 *
 * @choose_state: returns PCI power state of given device preferred by the
 *                platform; to be used during system-wide transitions from a
 *                sleeping state to the working state and vice versa
 *
 * @can_wakeup: returns 'true' if given device is capable of waking up the
 *              system from a sleeping state
 *
 * @sleep_wake: enables/disables the system wake up capability of given device
 *
 * If given platform is generally capable of power managing PCI devices, all of
 * these callbacks are mandatory.
 */
struct pci_platform_pm_ops {
	bool (*is_manageable)(struct pci_dev *dev);
	int (*set_state)(struct pci_dev *dev, pci_power_t state);
	pci_power_t (*choose_state)(struct pci_dev *dev);
	bool (*can_wakeup)(struct pci_dev *dev);
	int (*sleep_wake)(struct pci_dev *dev, bool enable);
};

extern int pci_set_platform_pm(struct pci_platform_pm_ops *ops);
extern void pci_update_current_state(struct pci_dev *dev, pci_power_t state);
extern void pci_disable_enabled_device(struct pci_dev *dev);
extern void pci_pm_init(struct pci_dev *dev);
extern void platform_pci_wakeup_init(struct pci_dev *dev);
extern void pci_allocate_cap_save_buffers(struct pci_dev *dev);

static inline bool pci_is_bridge(struct pci_dev *pci_dev)
{
	return !!(pci_dev->subordinate);
}

extern int pci_user_read_config_byte(struct pci_dev *dev, int where, u8 *val);
extern int pci_user_read_config_word(struct pci_dev *dev, int where, u16 *val);
extern int pci_user_read_config_dword(struct pci_dev *dev, int where, u32 *val);
extern int pci_user_write_config_byte(struct pci_dev *dev, int where, u8 val);
extern int pci_user_write_config_word(struct pci_dev *dev, int where, u16 val);
extern int pci_user_write_config_dword(struct pci_dev *dev, int where, u32 val);

struct pci_vpd_ops {
	ssize_t (*read)(struct pci_dev *dev, loff_t pos, size_t count, void *buf);
	ssize_t (*write)(struct pci_dev *dev, loff_t pos, size_t count, const void *buf);
	void (*release)(struct pci_dev *dev);
};

struct pci_vpd {
	unsigned int len;
	const struct pci_vpd_ops *ops;
	struct bin_attribute *attr; /* descriptor for sysfs VPD entry */
};

extern int pci_vpd_pci22_init(struct pci_dev *dev);
static inline void pci_vpd_release(struct pci_dev *dev)
{
	if (dev->vpd)
		dev->vpd->ops->release(dev);
}

/* PCI /proc functions */
#ifdef CONFIG_PROC_FS
extern int pci_proc_attach_device(struct pci_dev *dev);
extern int pci_proc_detach_device(struct pci_dev *dev);
extern int pci_proc_detach_bus(struct pci_bus *bus);
#else
static inline int pci_proc_attach_device(struct pci_dev *dev) { return 0; }
static inline int pci_proc_detach_device(struct pci_dev *dev) { return 0; }
static inline int pci_proc_detach_bus(struct pci_bus *bus) { return 0; }
#endif

/* Functions for PCI Hotplug drivers to use */
extern unsigned int pci_do_scan_bus(struct pci_bus *bus);

#ifdef HAVE_PCI_LEGACY
extern void pci_create_legacy_files(struct pci_bus *bus);
extern void pci_remove_legacy_files(struct pci_bus *bus);
#else
static inline void pci_create_legacy_files(struct pci_bus *bus) { return; }
static inline void pci_remove_legacy_files(struct pci_bus *bus) { return; }
#endif

/* Lock for read/write access to pci device and bus lists */
extern struct rw_semaphore pci_bus_sem;

extern unsigned int pci_pm_d3_delay;

#ifdef CONFIG_PCI_MSI
void pci_no_msi(void);
extern void pci_msi_init_pci_dev(struct pci_dev *dev);
#else
static inline void pci_no_msi(void) { }
static inline void pci_msi_init_pci_dev(struct pci_dev *dev) { }
#endif

#ifdef CONFIG_PCIEAER
void pci_no_aer(void);
#else
static inline void pci_no_aer(void) { }
#endif

static inline int pci_no_d1d2(struct pci_dev *dev)
{
	unsigned int parent_dstates = 0;

	if (dev->bus->self)
		parent_dstates = dev->bus->self->no_d1d2;
	return (dev->no_d1d2 || parent_dstates);

}
extern int pcie_mch_quirk;
extern struct device_attribute pci_dev_attrs[];
extern struct device_attribute dev_attr_cpuaffinity;
extern struct device_attribute dev_attr_cpulistaffinity;
#ifdef CONFIG_HOTPLUG
extern struct bus_attribute pci_bus_attrs[];
#else
#define pci_bus_attrs	NULL
#endif


/**
 * pci_match_one_device - Tell if a PCI device structure has a matching
 *                        PCI device id structure
 * @id: single PCI device id structure to match
 * @dev: the PCI device structure to match against
 *
 * Returns the matching pci_device_id structure or %NULL if there is no match.
 */
static inline const struct pci_device_id *
pci_match_one_device(const struct pci_device_id *id, const struct pci_dev *dev)
{
	if ((id->vendor == PCI_ANY_ID || id->vendor == dev->vendor) &&
	    (id->device == PCI_ANY_ID || id->device == dev->device) &&
	    (id->subvendor == PCI_ANY_ID || id->subvendor == dev->subsystem_vendor) &&
	    (id->subdevice == PCI_ANY_ID || id->subdevice == dev->subsystem_device) &&
	    !((id->class ^ dev->class) & id->class_mask))
		return id;
	return NULL;
}

struct pci_dev *pci_find_upstream_pcie_bridge(struct pci_dev *pdev);

/* PCI slot sysfs helper code */
#define to_pci_slot(s) container_of(s, struct pci_slot, kobj)

extern struct kset *pci_slots_kset;

struct pci_slot_attribute {
	struct attribute attr;
	ssize_t (*show)(struct pci_slot *, char *);
	ssize_t (*store)(struct pci_slot *, const char *, size_t);
};
#define to_pci_slot_attr(s) container_of(s, struct pci_slot_attribute, attr)

enum pci_bar_type {
	pci_bar_unknown,	/* Standard PCI BAR probe */
	pci_bar_io,		/* An io port BAR */
	pci_bar_mem32,		/* A 32-bit memory BAR */
	pci_bar_mem64,		/* A 64-bit memory BAR */
};

extern int pci_setup_device(struct pci_dev *dev);
extern int __pci_read_base(struct pci_dev *dev, enum pci_bar_type type,
				struct resource *res, unsigned int reg);
extern int pci_resource_bar(struct pci_dev *dev, int resno,
			    enum pci_bar_type *type);
extern int pci_bus_add_child(struct pci_bus *bus);
extern void pci_enable_ari(struct pci_dev *dev);
/**
 * pci_ari_enabled - query ARI forwarding status
 * @bus: the PCI bus
 *
 * Returns 1 if ARI forwarding is enabled, or 0 if not enabled;
 */
static inline int pci_ari_enabled(struct pci_bus *bus)
{
	return bus->self && bus->self->ari_enabled;
}

#ifdef CONFIG_PCI_QUIRKS
extern int pci_is_reassigndev(struct pci_dev *dev);
resource_size_t pci_specified_resource_alignment(struct pci_dev *dev);
extern void pci_disable_bridge_window(struct pci_dev *dev);
#endif

/* Single Root I/O Virtualization */
struct pci_sriov {
	int pos;		/* capability position */
	int nres;		/* number of resources */
	u32 cap;		/* SR-IOV Capabilities */
	u16 ctrl;		/* SR-IOV Control */
	u16 total;		/* total VFs associated with the PF */
	u16 initial;		/* initial VFs associated with the PF */
	u16 nr_virtfn;		/* number of VFs available */
	u16 offset;		/* first VF Routing ID offset */
	u16 stride;		/* following VF stride */
	u32 pgsz;		/* page size for BAR alignment */
	u8 link;		/* Function Dependency Link */
	struct pci_dev *dev;	/* lowest numbered PF */
	struct pci_dev *self;	/* this PF */
	struct mutex lock;	/* lock for VF bus */
	struct work_struct mtask; /* VF Migration task */
	u8 __iomem *mstate;	/* VF Migration State Array */
};

#ifdef CONFIG_PCI_IOV
extern int pci_iov_init(struct pci_dev *dev);
extern void pci_iov_release(struct pci_dev *dev);
extern int pci_iov_resource_bar(struct pci_dev *dev, int resno,
				enum pci_bar_type *type);
extern void pci_restore_iov_state(struct pci_dev *dev);
extern int pci_iov_bus_range(struct pci_bus *bus);
#else
static inline int pci_iov_init(struct pci_dev *dev)
{
	return -ENODEV;
}
static inline void pci_iov_release(struct pci_dev *dev)

{
}
static inline int pci_iov_resource_bar(struct pci_dev *dev, int resno,
				       enum pci_bar_type *type)
{
	return 0;
}
static inline void pci_restore_iov_state(struct pci_dev *dev)
{
}
static inline int pci_iov_bus_range(struct pci_bus *bus)
{
	return 0;
}
#endif /* CONFIG_PCI_IOV */

#endif /* DRIVERS_PCI_H */
