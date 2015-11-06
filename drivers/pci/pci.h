#ifndef DRIVERS_PCI_H
#define DRIVERS_PCI_H

#define PCI_CFG_SPACE_SIZE	256
#define PCI_CFG_SPACE_EXP_SIZE	4096

#define PCI_FIND_CAP_TTL	48

extern const unsigned char pcie_link_speed[];

bool pcie_cap_has_lnkctl(const struct pci_dev *dev);

/* Functions internal to the PCI core code */

int pci_create_sysfs_dev_files(struct pci_dev *pdev);
void pci_remove_sysfs_dev_files(struct pci_dev *pdev);
#if !defined(CONFIG_DMI) && !defined(CONFIG_ACPI)
static inline void pci_create_firmware_label_files(struct pci_dev *pdev)
{ return; }
static inline void pci_remove_firmware_label_files(struct pci_dev *pdev)
{ return; }
#else
void pci_create_firmware_label_files(struct pci_dev *pdev);
void pci_remove_firmware_label_files(struct pci_dev *pdev);
#endif
void pci_cleanup_rom(struct pci_dev *dev);
#ifdef HAVE_PCI_MMAP
enum pci_mmap_api {
	PCI_MMAP_SYSFS,	/* mmap on /sys/bus/pci/devices/<BDF>/resource<N> */
	PCI_MMAP_PROCFS	/* mmap on /proc/bus/pci/<BDF> */
};
int pci_mmap_fits(struct pci_dev *pdev, int resno, struct vm_area_struct *vmai,
		  enum pci_mmap_api mmap_api);
#endif
int pci_probe_reset_function(struct pci_dev *dev);

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
 * @sleep_wake: enables/disables the system wake up capability of given device
 *
 * @run_wake: enables/disables the platform to generate run-time wake-up events
 *		for given device (the device's wake-up capability has to be
 *		enabled by @sleep_wake for this feature to work)
 *
 * @need_resume: returns 'true' if the given device (which is currently
 *		suspended) needs to be resumed to be configured for system
 *		wakeup.
 *
 * If given platform is generally capable of power managing PCI devices, all of
 * these callbacks are mandatory.
 */
struct pci_platform_pm_ops {
	bool (*is_manageable)(struct pci_dev *dev);
	int (*set_state)(struct pci_dev *dev, pci_power_t state);
	pci_power_t (*choose_state)(struct pci_dev *dev);
	int (*sleep_wake)(struct pci_dev *dev, bool enable);
	int (*run_wake)(struct pci_dev *dev, bool enable);
	bool (*need_resume)(struct pci_dev *dev);
};

int pci_set_platform_pm(struct pci_platform_pm_ops *ops);
void pci_update_current_state(struct pci_dev *dev, pci_power_t state);
void pci_power_up(struct pci_dev *dev);
void pci_disable_enabled_device(struct pci_dev *dev);
int pci_finish_runtime_suspend(struct pci_dev *dev);
int __pci_pme_wakeup(struct pci_dev *dev, void *ign);
bool pci_dev_keep_suspended(struct pci_dev *dev);
void pci_dev_complete_resume(struct pci_dev *pci_dev);
void pci_config_pm_runtime_get(struct pci_dev *dev);
void pci_config_pm_runtime_put(struct pci_dev *dev);
void pci_pm_init(struct pci_dev *dev);
void pci_ea_init(struct pci_dev *dev);
void pci_allocate_cap_save_buffers(struct pci_dev *dev);
void pci_free_cap_save_buffers(struct pci_dev *dev);

static inline void pci_wakeup_event(struct pci_dev *dev)
{
	/* Wait 100 ms before the system can be put into a sleep state. */
	pm_wakeup_event(&dev->dev, 100);
}

static inline bool pci_has_subordinate(struct pci_dev *pci_dev)
{
	return !!(pci_dev->subordinate);
}

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

int pci_vpd_pci22_init(struct pci_dev *dev);
static inline void pci_vpd_release(struct pci_dev *dev)
{
	if (dev->vpd)
		dev->vpd->ops->release(dev);
}

/* PCI /proc functions */
#ifdef CONFIG_PROC_FS
int pci_proc_attach_device(struct pci_dev *dev);
int pci_proc_detach_device(struct pci_dev *dev);
int pci_proc_detach_bus(struct pci_bus *bus);
#else
static inline int pci_proc_attach_device(struct pci_dev *dev) { return 0; }
static inline int pci_proc_detach_device(struct pci_dev *dev) { return 0; }
static inline int pci_proc_detach_bus(struct pci_bus *bus) { return 0; }
#endif

/* Functions for PCI Hotplug drivers to use */
int pci_hp_add_bridge(struct pci_dev *dev);

#ifdef HAVE_PCI_LEGACY
void pci_create_legacy_files(struct pci_bus *bus);
void pci_remove_legacy_files(struct pci_bus *bus);
#else
static inline void pci_create_legacy_files(struct pci_bus *bus) { return; }
static inline void pci_remove_legacy_files(struct pci_bus *bus) { return; }
#endif

/* Lock for read/write access to pci device and bus lists */
extern struct rw_semaphore pci_bus_sem;

extern raw_spinlock_t pci_lock;

extern unsigned int pci_pm_d3_delay;

#ifdef CONFIG_PCI_MSI
void pci_no_msi(void);
void pci_msi_init_pci_dev(struct pci_dev *dev);
#else
static inline void pci_no_msi(void) { }
static inline void pci_msi_init_pci_dev(struct pci_dev *dev) { }
#endif

static inline void pci_msi_set_enable(struct pci_dev *dev, int enable)
{
	u16 control;

	pci_read_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, &control);
	control &= ~PCI_MSI_FLAGS_ENABLE;
	if (enable)
		control |= PCI_MSI_FLAGS_ENABLE;
	pci_write_config_word(dev, dev->msi_cap + PCI_MSI_FLAGS, control);
}

static inline void pci_msix_clear_and_set_ctrl(struct pci_dev *dev, u16 clear, u16 set)
{
	u16 ctrl;

	pci_read_config_word(dev, dev->msix_cap + PCI_MSIX_FLAGS, &ctrl);
	ctrl &= ~clear;
	ctrl |= set;
	pci_write_config_word(dev, dev->msix_cap + PCI_MSIX_FLAGS, ctrl);
}

void pci_realloc_get_opt(char *);

static inline int pci_no_d1d2(struct pci_dev *dev)
{
	unsigned int parent_dstates = 0;

	if (dev->bus->self)
		parent_dstates = dev->bus->self->no_d1d2;
	return (dev->no_d1d2 || parent_dstates);

}
extern const struct attribute_group *pci_dev_groups[];
extern const struct attribute_group *pcibus_groups[];
extern struct device_type pci_dev_type;
extern const struct attribute_group *pci_bus_groups[];


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

bool pci_bus_read_dev_vendor_id(struct pci_bus *bus, int devfn, u32 *pl,
				int crs_timeout);
int pci_setup_device(struct pci_dev *dev);
int __pci_read_base(struct pci_dev *dev, enum pci_bar_type type,
		    struct resource *res, unsigned int reg);
int pci_resource_bar(struct pci_dev *dev, int resno, enum pci_bar_type *type);
void pci_configure_ari(struct pci_dev *dev);
void __pci_bus_size_bridges(struct pci_bus *bus,
			struct list_head *realloc_head);
void __pci_bus_assign_resources(const struct pci_bus *bus,
				struct list_head *realloc_head,
				struct list_head *fail_head);
bool pci_bus_clip_resource(struct pci_dev *dev, int idx);

void pci_reassigndev_resource_alignment(struct pci_dev *dev);
void pci_disable_bridge_window(struct pci_dev *dev);

/* Single Root I/O Virtualization */
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
	u8 max_VF_buses;	/* max buses consumed by VFs */
	u16 driver_max_VFs;	/* max num VFs driver supports */
	struct pci_dev *dev;	/* lowest numbered PF */
	struct pci_dev *self;	/* this PF */
	struct mutex lock;	/* lock for VF bus */
	resource_size_t barsz[PCI_SRIOV_NUM_BARS];	/* VF BAR size */
};

#ifdef CONFIG_PCI_ATS
void pci_restore_ats_state(struct pci_dev *dev);
#else
static inline void pci_restore_ats_state(struct pci_dev *dev)
{
}
#endif /* CONFIG_PCI_ATS */

#ifdef CONFIG_PCI_IOV
int pci_iov_init(struct pci_dev *dev);
void pci_iov_release(struct pci_dev *dev);
int pci_iov_resource_bar(struct pci_dev *dev, int resno);
resource_size_t pci_sriov_resource_alignment(struct pci_dev *dev, int resno);
void pci_restore_iov_state(struct pci_dev *dev);
int pci_iov_bus_range(struct pci_bus *bus);

#else
static inline int pci_iov_init(struct pci_dev *dev)
{
	return -ENODEV;
}
static inline void pci_iov_release(struct pci_dev *dev)

{
}
static inline int pci_iov_resource_bar(struct pci_dev *dev, int resno)
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

unsigned long pci_cardbus_resource_alignment(struct resource *);

static inline resource_size_t pci_resource_alignment(struct pci_dev *dev,
						     struct resource *res)
{
#ifdef CONFIG_PCI_IOV
	int resno = res - dev->resource;

	if (resno >= PCI_IOV_RESOURCES && resno <= PCI_IOV_RESOURCE_END)
		return pci_sriov_resource_alignment(dev, resno);
#endif
	if (dev->class >> 8  == PCI_CLASS_BRIDGE_CARDBUS)
		return pci_cardbus_resource_alignment(res);
	return resource_alignment(res);
}

void pci_enable_acs(struct pci_dev *dev);

struct pci_dev_reset_methods {
	u16 vendor;
	u16 device;
	int (*reset)(struct pci_dev *dev, int probe);
};

#ifdef CONFIG_PCI_QUIRKS
int pci_dev_specific_reset(struct pci_dev *dev, int probe);
#else
static inline int pci_dev_specific_reset(struct pci_dev *dev, int probe)
{
	return -ENOTTY;
}
#endif

struct pci_host_bridge *pci_find_host_bridge(struct pci_bus *bus);

#endif /* DRIVERS_PCI_H */
