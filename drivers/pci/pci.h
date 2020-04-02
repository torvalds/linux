/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DRIVERS_PCI_H
#define DRIVERS_PCI_H

#include <linux/pci.h>

/* Number of possible devfns: 0.0 to 1f.7 inclusive */
#define MAX_NR_DEVFNS 256

#define PCI_FIND_CAP_TTL	48

#define PCI_VSEC_ID_INTEL_TBT	0x1234	/* Thunderbolt */

extern const unsigned char pcie_link_speed[];
extern bool pci_early_dump;

bool pcie_cap_has_lnkctl(const struct pci_dev *dev);
bool pcie_cap_has_rtctl(const struct pci_dev *dev);

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

enum pci_mmap_api {
	PCI_MMAP_SYSFS,	/* mmap on /sys/bus/pci/devices/<BDF>/resource<N> */
	PCI_MMAP_PROCFS	/* mmap on /proc/bus/pci/<BDF> */
};
int pci_mmap_fits(struct pci_dev *pdev, int resno, struct vm_area_struct *vmai,
		  enum pci_mmap_api mmap_api);

int pci_probe_reset_function(struct pci_dev *dev);
int pci_bridge_secondary_bus_reset(struct pci_dev *dev);
int pci_bus_error_reset(struct pci_dev *dev);

#define PCI_PM_D2_DELAY         200
#define PCI_PM_D3_WAIT          10
#define PCI_PM_D3COLD_WAIT      100
#define PCI_PM_BUS_WAIT         50

/**
 * struct pci_platform_pm_ops - Firmware PM callbacks
 *
 * @bridge_d3: Does the bridge allow entering into D3
 *
 * @is_manageable: returns 'true' if given device is power manageable by the
 *		   platform firmware
 *
 * @set_state: invokes the platform firmware to set the device's power state
 *
 * @get_state: queries the platform firmware for a device's current power state
 *
 * @refresh_state: asks the platform to refresh the device's power state data
 *
 * @choose_state: returns PCI power state of given device preferred by the
 *		  platform; to be used during system-wide transitions from a
 *		  sleeping state to the working state and vice versa
 *
 * @set_wakeup: enables/disables wakeup capability for the device
 *
 * @need_resume: returns 'true' if the given device (which is currently
 *		 suspended) needs to be resumed to be configured for system
 *		 wakeup.
 *
 * If given platform is generally capable of power managing PCI devices, all of
 * these callbacks are mandatory.
 */
struct pci_platform_pm_ops {
	bool (*bridge_d3)(struct pci_dev *dev);
	bool (*is_manageable)(struct pci_dev *dev);
	int (*set_state)(struct pci_dev *dev, pci_power_t state);
	pci_power_t (*get_state)(struct pci_dev *dev);
	void (*refresh_state)(struct pci_dev *dev);
	pci_power_t (*choose_state)(struct pci_dev *dev);
	int (*set_wakeup)(struct pci_dev *dev, bool enable);
	bool (*need_resume)(struct pci_dev *dev);
};

int pci_set_platform_pm(const struct pci_platform_pm_ops *ops);
void pci_update_current_state(struct pci_dev *dev, pci_power_t state);
void pci_refresh_power_state(struct pci_dev *dev);
int pci_power_up(struct pci_dev *dev);
void pci_disable_enabled_device(struct pci_dev *dev);
int pci_finish_runtime_suspend(struct pci_dev *dev);
void pcie_clear_root_pme_status(struct pci_dev *dev);
bool pci_check_pme_status(struct pci_dev *dev);
void pci_pme_wakeup_bus(struct pci_bus *bus);
int __pci_pme_wakeup(struct pci_dev *dev, void *ign);
void pci_pme_restore(struct pci_dev *dev);
bool pci_dev_need_resume(struct pci_dev *dev);
void pci_dev_adjust_pme(struct pci_dev *dev);
void pci_dev_complete_resume(struct pci_dev *pci_dev);
void pci_config_pm_runtime_get(struct pci_dev *dev);
void pci_config_pm_runtime_put(struct pci_dev *dev);
void pci_pm_init(struct pci_dev *dev);
void pci_ea_init(struct pci_dev *dev);
void pci_allocate_cap_save_buffers(struct pci_dev *dev);
void pci_free_cap_save_buffers(struct pci_dev *dev);
bool pci_bridge_d3_possible(struct pci_dev *dev);
void pci_bridge_d3_update(struct pci_dev *dev);
void pci_bridge_wait_for_secondary_bus(struct pci_dev *dev);

static inline void pci_wakeup_event(struct pci_dev *dev)
{
	/* Wait 100 ms before the system can be put into a sleep state. */
	pm_wakeup_event(&dev->dev, 100);
}

static inline bool pci_has_subordinate(struct pci_dev *pci_dev)
{
	return !!(pci_dev->subordinate);
}

static inline bool pci_power_manageable(struct pci_dev *pci_dev)
{
	/*
	 * Currently we allow normal PCI devices and PCI bridges transition
	 * into D3 if their bridge_d3 is set.
	 */
	return !pci_has_subordinate(pci_dev) || pci_dev->bridge_d3;
}

static inline bool pcie_downstream_port(const struct pci_dev *dev)
{
	int type = pci_pcie_type(dev);

	return type == PCI_EXP_TYPE_ROOT_PORT ||
	       type == PCI_EXP_TYPE_DOWNSTREAM ||
	       type == PCI_EXP_TYPE_PCIE_BRIDGE;
}

int pci_vpd_init(struct pci_dev *dev);
void pci_vpd_release(struct pci_dev *dev);
void pcie_vpd_create_sysfs_dev_files(struct pci_dev *dev);
void pcie_vpd_remove_sysfs_dev_files(struct pci_dev *dev);

/* PCI Virtual Channel */
int pci_save_vc_state(struct pci_dev *dev);
void pci_restore_vc_state(struct pci_dev *dev);
void pci_allocate_vc_save_buffers(struct pci_dev *dev);

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
extern struct mutex pci_slot_mutex;

extern raw_spinlock_t pci_lock;

extern unsigned int pci_pm_d3_delay;

#ifdef CONFIG_PCI_MSI
void pci_no_msi(void);
#else
static inline void pci_no_msi(void) { }
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
extern const struct device_type pci_dev_type;
extern const struct attribute_group *pci_bus_groups[];

extern unsigned long pci_hotplug_io_size;
extern unsigned long pci_hotplug_mmio_size;
extern unsigned long pci_hotplug_mmio_pref_size;
extern unsigned long pci_hotplug_bus_size;

/**
 * pci_match_one_device - Tell if a PCI device structure has a matching
 *			  PCI device id structure
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
	pci_bar_io,		/* An I/O port BAR */
	pci_bar_mem32,		/* A 32-bit memory BAR */
	pci_bar_mem64,		/* A 64-bit memory BAR */
};

struct device *pci_get_host_bridge_device(struct pci_dev *dev);
void pci_put_host_bridge_device(struct device *dev);

int pci_configure_extended_tags(struct pci_dev *dev, void *ign);
bool pci_bus_read_dev_vendor_id(struct pci_bus *bus, int devfn, u32 *pl,
				int crs_timeout);
bool pci_bus_generic_read_dev_vendor_id(struct pci_bus *bus, int devfn, u32 *pl,
					int crs_timeout);
int pci_idt_bus_quirk(struct pci_bus *bus, int devfn, u32 *pl, int crs_timeout);

int pci_setup_device(struct pci_dev *dev);
int __pci_read_base(struct pci_dev *dev, enum pci_bar_type type,
		    struct resource *res, unsigned int reg);
void pci_configure_ari(struct pci_dev *dev);
void __pci_bus_size_bridges(struct pci_bus *bus,
			struct list_head *realloc_head);
void __pci_bus_assign_resources(const struct pci_bus *bus,
				struct list_head *realloc_head,
				struct list_head *fail_head);
bool pci_bus_clip_resource(struct pci_dev *dev, int idx);

void pci_reassigndev_resource_alignment(struct pci_dev *dev);
void pci_disable_bridge_window(struct pci_dev *dev);
struct pci_bus *pci_bus_get(struct pci_bus *bus);
void pci_bus_put(struct pci_bus *bus);

/* PCIe link information from Link Capabilities 2 */
#define PCIE_LNKCAP2_SLS2SPEED(lnkcap2) \
	((lnkcap2) & PCI_EXP_LNKCAP2_SLS_32_0GB ? PCIE_SPEED_32_0GT : \
	 (lnkcap2) & PCI_EXP_LNKCAP2_SLS_16_0GB ? PCIE_SPEED_16_0GT : \
	 (lnkcap2) & PCI_EXP_LNKCAP2_SLS_8_0GB ? PCIE_SPEED_8_0GT : \
	 (lnkcap2) & PCI_EXP_LNKCAP2_SLS_5_0GB ? PCIE_SPEED_5_0GT : \
	 (lnkcap2) & PCI_EXP_LNKCAP2_SLS_2_5GB ? PCIE_SPEED_2_5GT : \
	 PCI_SPEED_UNKNOWN)

/* PCIe speed to Mb/s reduced by encoding overhead */
#define PCIE_SPEED2MBS_ENC(speed) \
	((speed) == PCIE_SPEED_32_0GT ? 32000*128/130 : \
	 (speed) == PCIE_SPEED_16_0GT ? 16000*128/130 : \
	 (speed) == PCIE_SPEED_8_0GT  ?  8000*128/130 : \
	 (speed) == PCIE_SPEED_5_0GT  ?  5000*8/10 : \
	 (speed) == PCIE_SPEED_2_5GT  ?  2500*8/10 : \
	 0)

const char *pci_speed_string(enum pci_bus_speed speed);
enum pci_bus_speed pcie_get_speed_cap(struct pci_dev *dev);
enum pcie_link_width pcie_get_width_cap(struct pci_dev *dev);
u32 pcie_bandwidth_capable(struct pci_dev *dev, enum pci_bus_speed *speed,
			   enum pcie_link_width *width);
void __pcie_print_link_status(struct pci_dev *dev, bool verbose);
void pcie_report_downtraining(struct pci_dev *dev);
void pcie_update_link_speed(struct pci_bus *bus, u16 link_status);

/* Single Root I/O Virtualization */
struct pci_sriov {
	int		pos;		/* Capability position */
	int		nres;		/* Number of resources */
	u32		cap;		/* SR-IOV Capabilities */
	u16		ctrl;		/* SR-IOV Control */
	u16		total_VFs;	/* Total VFs associated with the PF */
	u16		initial_VFs;	/* Initial VFs associated with the PF */
	u16		num_VFs;	/* Number of VFs available */
	u16		offset;		/* First VF Routing ID offset */
	u16		stride;		/* Following VF stride */
	u16		vf_device;	/* VF device ID */
	u32		pgsz;		/* Page size for BAR alignment */
	u8		link;		/* Function Dependency Link */
	u8		max_VF_buses;	/* Max buses consumed by VFs */
	u16		driver_max_VFs;	/* Max num VFs driver supports */
	struct pci_dev	*dev;		/* Lowest numbered PF */
	struct pci_dev	*self;		/* This PF */
	u32		class;		/* VF device */
	u8		hdr_type;	/* VF header type */
	u16		subsystem_vendor; /* VF subsystem vendor */
	u16		subsystem_device; /* VF subsystem device */
	resource_size_t	barsz[PCI_SRIOV_NUM_BARS];	/* VF BAR size */
	bool		drivers_autoprobe; /* Auto probing of VFs by driver */
};

/**
 * pci_dev_set_io_state - Set the new error state if possible.
 *
 * @dev - pci device to set new error_state
 * @new - the state we want dev to be in
 *
 * Must be called with device_lock held.
 *
 * Returns true if state has been changed to the requested state.
 */
static inline bool pci_dev_set_io_state(struct pci_dev *dev,
					pci_channel_state_t new)
{
	bool changed = false;

	device_lock_assert(&dev->dev);
	switch (new) {
	case pci_channel_io_perm_failure:
		switch (dev->error_state) {
		case pci_channel_io_frozen:
		case pci_channel_io_normal:
		case pci_channel_io_perm_failure:
			changed = true;
			break;
		}
		break;
	case pci_channel_io_frozen:
		switch (dev->error_state) {
		case pci_channel_io_frozen:
		case pci_channel_io_normal:
			changed = true;
			break;
		}
		break;
	case pci_channel_io_normal:
		switch (dev->error_state) {
		case pci_channel_io_frozen:
		case pci_channel_io_normal:
			changed = true;
			break;
		}
		break;
	}
	if (changed)
		dev->error_state = new;
	return changed;
}

static inline int pci_dev_set_disconnected(struct pci_dev *dev, void *unused)
{
	device_lock(&dev->dev);
	pci_dev_set_io_state(dev, pci_channel_io_perm_failure);
	device_unlock(&dev->dev);

	return 0;
}

static inline bool pci_dev_is_disconnected(const struct pci_dev *dev)
{
	return dev->error_state == pci_channel_io_perm_failure;
}

/* pci_dev priv_flags */
#define PCI_DEV_ADDED 0

static inline void pci_dev_assign_added(struct pci_dev *dev, bool added)
{
	assign_bit(PCI_DEV_ADDED, &dev->priv_flags, added);
}

static inline bool pci_dev_is_added(const struct pci_dev *dev)
{
	return test_bit(PCI_DEV_ADDED, &dev->priv_flags);
}

#ifdef CONFIG_PCIEAER
#include <linux/aer.h>

#define AER_MAX_MULTI_ERR_DEVICES	5	/* Not likely to have more */

struct aer_err_info {
	struct pci_dev *dev[AER_MAX_MULTI_ERR_DEVICES];
	int error_dev_num;

	unsigned int id:16;

	unsigned int severity:2;	/* 0:NONFATAL | 1:FATAL | 2:COR */
	unsigned int __pad1:5;
	unsigned int multi_error_valid:1;

	unsigned int first_error:5;
	unsigned int __pad2:2;
	unsigned int tlp_header_valid:1;

	unsigned int status;		/* COR/UNCOR Error Status */
	unsigned int mask;		/* COR/UNCOR Error Mask */
	struct aer_header_log_regs tlp;	/* TLP Header */
};

int aer_get_device_error_info(struct pci_dev *dev, struct aer_err_info *info);
void aer_print_error(struct pci_dev *dev, struct aer_err_info *info);
#endif	/* CONFIG_PCIEAER */

#ifdef CONFIG_PCIE_DPC
void pci_save_dpc_state(struct pci_dev *dev);
void pci_restore_dpc_state(struct pci_dev *dev);
void pci_dpc_init(struct pci_dev *pdev);
void dpc_process_error(struct pci_dev *pdev);
pci_ers_result_t dpc_reset_link(struct pci_dev *pdev);
#else
static inline void pci_save_dpc_state(struct pci_dev *dev) {}
static inline void pci_restore_dpc_state(struct pci_dev *dev) {}
static inline void pci_dpc_init(struct pci_dev *pdev) {}
#endif

#ifdef CONFIG_PCI_ATS
/* Address Translation Service */
void pci_ats_init(struct pci_dev *dev);
void pci_restore_ats_state(struct pci_dev *dev);
#else
static inline void pci_ats_init(struct pci_dev *d) { }
static inline void pci_restore_ats_state(struct pci_dev *dev) { }
#endif /* CONFIG_PCI_ATS */

#ifdef CONFIG_PCI_PRI
void pci_pri_init(struct pci_dev *dev);
void pci_restore_pri_state(struct pci_dev *pdev);
#else
static inline void pci_pri_init(struct pci_dev *dev) { }
static inline void pci_restore_pri_state(struct pci_dev *pdev) { }
#endif

#ifdef CONFIG_PCI_PASID
void pci_pasid_init(struct pci_dev *dev);
void pci_restore_pasid_state(struct pci_dev *pdev);
#else
static inline void pci_pasid_init(struct pci_dev *dev) { }
static inline void pci_restore_pasid_state(struct pci_dev *pdev) { }
#endif

#ifdef CONFIG_PCI_IOV
int pci_iov_init(struct pci_dev *dev);
void pci_iov_release(struct pci_dev *dev);
void pci_iov_remove(struct pci_dev *dev);
void pci_iov_update_resource(struct pci_dev *dev, int resno);
resource_size_t pci_sriov_resource_alignment(struct pci_dev *dev, int resno);
void pci_restore_iov_state(struct pci_dev *dev);
int pci_iov_bus_range(struct pci_bus *bus);
extern const struct attribute_group sriov_dev_attr_group;
#else
static inline int pci_iov_init(struct pci_dev *dev)
{
	return -ENODEV;
}
static inline void pci_iov_release(struct pci_dev *dev)

{
}
static inline void pci_iov_remove(struct pci_dev *dev)
{
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
	if (dev->class >> 8 == PCI_CLASS_BRIDGE_CARDBUS)
		return pci_cardbus_resource_alignment(res);
	return resource_alignment(res);
}

void pci_enable_acs(struct pci_dev *dev);
#ifdef CONFIG_PCI_QUIRKS
int pci_dev_specific_acs_enabled(struct pci_dev *dev, u16 acs_flags);
int pci_dev_specific_enable_acs(struct pci_dev *dev);
int pci_dev_specific_disable_acs_redir(struct pci_dev *dev);
#else
static inline int pci_dev_specific_acs_enabled(struct pci_dev *dev,
					       u16 acs_flags)
{
	return -ENOTTY;
}
static inline int pci_dev_specific_enable_acs(struct pci_dev *dev)
{
	return -ENOTTY;
}
static inline int pci_dev_specific_disable_acs_redir(struct pci_dev *dev)
{
	return -ENOTTY;
}
#endif

/* PCI error reporting and recovery */
pci_ers_result_t pcie_do_recovery(struct pci_dev *dev,
			enum pci_channel_state state,
			pci_ers_result_t (*reset_link)(struct pci_dev *pdev));

bool pcie_wait_for_link(struct pci_dev *pdev, bool active);
#ifdef CONFIG_PCIEASPM
void pcie_aspm_init_link_state(struct pci_dev *pdev);
void pcie_aspm_exit_link_state(struct pci_dev *pdev);
void pcie_aspm_pm_state_change(struct pci_dev *pdev);
void pcie_aspm_powersave_config_link(struct pci_dev *pdev);
#else
static inline void pcie_aspm_init_link_state(struct pci_dev *pdev) { }
static inline void pcie_aspm_exit_link_state(struct pci_dev *pdev) { }
static inline void pcie_aspm_pm_state_change(struct pci_dev *pdev) { }
static inline void pcie_aspm_powersave_config_link(struct pci_dev *pdev) { }
#endif

#ifdef CONFIG_PCIE_ECRC
void pcie_set_ecrc_checking(struct pci_dev *dev);
void pcie_ecrc_get_policy(char *str);
#else
static inline void pcie_set_ecrc_checking(struct pci_dev *dev) { }
static inline void pcie_ecrc_get_policy(char *str) { }
#endif

#ifdef CONFIG_PCIE_PTM
void pci_ptm_init(struct pci_dev *dev);
int pci_enable_ptm(struct pci_dev *dev, u8 *granularity);
#else
static inline void pci_ptm_init(struct pci_dev *dev) { }
static inline int pci_enable_ptm(struct pci_dev *dev, u8 *granularity)
{ return -EINVAL; }
#endif

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

#if defined(CONFIG_PCI_QUIRKS) && defined(CONFIG_ARM64)
int acpi_get_rc_resources(struct device *dev, const char *hid, u16 segment,
			  struct resource *res);
#endif

u32 pci_rebar_get_possible_sizes(struct pci_dev *pdev, int bar);
int pci_rebar_get_current_size(struct pci_dev *pdev, int bar);
int pci_rebar_set_size(struct pci_dev *pdev, int bar, int size);
static inline u64 pci_rebar_size_to_bytes(int size)
{
	return 1ULL << (size + 20);
}

struct device_node;

#ifdef CONFIG_OF
int of_pci_parse_bus_range(struct device_node *node, struct resource *res);
int of_get_pci_domain_nr(struct device_node *node);
int of_pci_get_max_link_speed(struct device_node *node);
void pci_set_of_node(struct pci_dev *dev);
void pci_release_of_node(struct pci_dev *dev);
void pci_set_bus_of_node(struct pci_bus *bus);
void pci_release_bus_of_node(struct pci_bus *bus);

#else
static inline int
of_pci_parse_bus_range(struct device_node *node, struct resource *res)
{
	return -EINVAL;
}

static inline int
of_get_pci_domain_nr(struct device_node *node)
{
	return -1;
}

static inline int
of_pci_get_max_link_speed(struct device_node *node)
{
	return -EINVAL;
}

static inline void pci_set_of_node(struct pci_dev *dev) { }
static inline void pci_release_of_node(struct pci_dev *dev) { }
static inline void pci_set_bus_of_node(struct pci_bus *bus) { }
static inline void pci_release_bus_of_node(struct pci_bus *bus) { }
#endif /* CONFIG_OF */

#ifdef CONFIG_PCIEAER
void pci_no_aer(void);
void pci_aer_init(struct pci_dev *dev);
void pci_aer_exit(struct pci_dev *dev);
extern const struct attribute_group aer_stats_attr_group;
void pci_aer_clear_fatal_status(struct pci_dev *dev);
void pci_aer_clear_device_status(struct pci_dev *dev);
int pci_aer_clear_status(struct pci_dev *dev);
int pci_aer_raw_clear_status(struct pci_dev *dev);
#else
static inline void pci_no_aer(void) { }
static inline void pci_aer_init(struct pci_dev *d) { }
static inline void pci_aer_exit(struct pci_dev *d) { }
static inline void pci_aer_clear_fatal_status(struct pci_dev *dev) { }
static inline void pci_aer_clear_device_status(struct pci_dev *dev) { }
static inline int pci_aer_clear_status(struct pci_dev *dev) { return -EINVAL; }
static inline int pci_aer_raw_clear_status(struct pci_dev *dev) { return -EINVAL; }
#endif

#ifdef CONFIG_ACPI
int pci_acpi_program_hp_params(struct pci_dev *dev);
#else
static inline int pci_acpi_program_hp_params(struct pci_dev *dev)
{
	return -ENODEV;
}
#endif

#ifdef CONFIG_PCIEASPM
extern const struct attribute_group aspm_ctrl_attr_group;
#endif

#endif /* DRIVERS_PCI_H */
