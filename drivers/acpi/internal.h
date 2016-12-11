/*
 * acpi/internal.h
 * For use by Linux/ACPI infrastructure, not drivers
 *
 * Copyright (c) 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef _ACPI_INTERNAL_H_
#define _ACPI_INTERNAL_H_

#define PREFIX "ACPI: "

acpi_status acpi_os_initialize1(void);
void init_acpi_device_notify(void);
int acpi_scan_init(void);
void acpi_pci_root_init(void);
void acpi_pci_link_init(void);
void acpi_processor_init(void);
void acpi_platform_init(void);
void acpi_pnp_init(void);
void acpi_int340x_thermal_init(void);
int acpi_sysfs_init(void);
void acpi_container_init(void);
void acpi_memory_hotplug_init(void);
#ifdef	CONFIG_ACPI_HOTPLUG_IOAPIC
int acpi_ioapic_add(struct acpi_pci_root *root);
int acpi_ioapic_remove(struct acpi_pci_root *root);
#else
static inline int acpi_ioapic_add(struct acpi_pci_root *root) { return 0; }
static inline int acpi_ioapic_remove(struct acpi_pci_root *root) { return 0; }
#endif
#ifdef CONFIG_ACPI_DOCK
void register_dock_dependent_device(struct acpi_device *adev,
				    acpi_handle dshandle);
int dock_notify(struct acpi_device *adev, u32 event);
void acpi_dock_add(struct acpi_device *adev);
#else
static inline void register_dock_dependent_device(struct acpi_device *adev,
						  acpi_handle dshandle) {}
static inline int dock_notify(struct acpi_device *adev, u32 event) { return -ENODEV; }
static inline void acpi_dock_add(struct acpi_device *adev) {}
#endif
#ifdef CONFIG_X86
void acpi_cmos_rtc_init(void);
#else
static inline void acpi_cmos_rtc_init(void) {}
#endif
int acpi_rev_override_setup(char *str);

extern bool acpi_force_hot_remove;

void acpi_sysfs_add_hotplug_profile(struct acpi_hotplug_profile *hotplug,
				    const char *name);
int acpi_scan_add_handler_with_hotplug(struct acpi_scan_handler *handler,
				       const char *hotplug_profile_name);
void acpi_scan_hotplug_enabled(struct acpi_hotplug_profile *hotplug, bool val);

#ifdef CONFIG_DEBUG_FS
extern struct dentry *acpi_debugfs_dir;
void acpi_debugfs_init(void);
#else
static inline void acpi_debugfs_init(void) { return; }
#endif
void acpi_lpss_init(void);

void acpi_apd_init(void);

acpi_status acpi_hotplug_schedule(struct acpi_device *adev, u32 src);
bool acpi_queue_hotplug_work(struct work_struct *work);
void acpi_device_hotplug(struct acpi_device *adev, u32 src);
bool acpi_scan_is_offline(struct acpi_device *adev, bool uevent);

/* --------------------------------------------------------------------------
                     Device Node Initialization / Removal
   -------------------------------------------------------------------------- */
#define ACPI_STA_DEFAULT (ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED | \
			  ACPI_STA_DEVICE_UI | ACPI_STA_DEVICE_FUNCTIONING)

int acpi_device_add(struct acpi_device *device,
		    void (*release)(struct device *));
void acpi_init_device_object(struct acpi_device *device, acpi_handle handle,
			     int type, unsigned long long sta);
int acpi_device_setup_files(struct acpi_device *dev);
void acpi_device_remove_files(struct acpi_device *dev);
void acpi_device_add_finalize(struct acpi_device *device);
void acpi_free_pnp_ids(struct acpi_device_pnp *pnp);
bool acpi_device_is_present(struct acpi_device *adev);
bool acpi_device_is_battery(struct acpi_device *adev);
bool acpi_device_is_first_physical_node(struct acpi_device *adev,
					const struct device *dev);

/* --------------------------------------------------------------------------
                     Device Matching and Notification
   -------------------------------------------------------------------------- */
struct acpi_device *acpi_companion_match(const struct device *dev);
int __acpi_device_uevent_modalias(struct acpi_device *adev,
				  struct kobj_uevent_env *env);

/* --------------------------------------------------------------------------
                                  Power Resource
   -------------------------------------------------------------------------- */
int acpi_power_init(void);
void acpi_power_resources_list_free(struct list_head *list);
int acpi_extract_power_resources(union acpi_object *package, unsigned int start,
				 struct list_head *list);
int acpi_add_power_resource(acpi_handle handle);
void acpi_power_add_remove_device(struct acpi_device *adev, bool add);
int acpi_power_wakeup_list_init(struct list_head *list, int *system_level);
int acpi_device_sleep_wake(struct acpi_device *dev,
                           int enable, int sleep_state, int dev_state);
int acpi_power_get_inferred_state(struct acpi_device *device, int *state);
int acpi_power_on_resources(struct acpi_device *device, int state);
int acpi_power_transition(struct acpi_device *device, int state);

int acpi_wakeup_device_init(void);

#ifdef CONFIG_ARCH_MIGHT_HAVE_ACPI_PDC
void acpi_early_processor_set_pdc(void);
#else
static inline void acpi_early_processor_set_pdc(void) {}
#endif

#ifdef CONFIG_X86
void acpi_early_processor_osc(void);
#else
static inline void acpi_early_processor_osc(void) {}
#endif

/* --------------------------------------------------------------------------
                                  Embedded Controller
   -------------------------------------------------------------------------- */
struct acpi_ec {
	acpi_handle handle;
	unsigned long gpe;
	unsigned long command_addr;
	unsigned long data_addr;
	bool global_lock;
	unsigned long flags;
	unsigned long reference_count;
	struct mutex mutex;
	wait_queue_head_t wait;
	struct list_head list;
	struct transaction *curr;
	spinlock_t lock;
	struct work_struct work;
	unsigned long timestamp;
	unsigned long nr_pending_queries;
};

extern struct acpi_ec *first_ec;

/* If we find an EC via the ECDT, we need to keep a ptr to its context */
/* External interfaces use first EC only, so remember */
typedef int (*acpi_ec_query_func) (void *data);

int acpi_ec_init(void);
int acpi_ec_ecdt_probe(void);
int acpi_boot_ec_enable(void);
void acpi_ec_block_transactions(void);
void acpi_ec_unblock_transactions(void);
void acpi_ec_unblock_transactions_early(void);
int acpi_ec_add_query_handler(struct acpi_ec *ec, u8 query_bit,
			      acpi_handle handle, acpi_ec_query_func func,
			      void *data);
void acpi_ec_remove_query_handler(struct acpi_ec *ec, u8 query_bit);


/*--------------------------------------------------------------------------
                                  Suspend/Resume
  -------------------------------------------------------------------------- */
#ifdef CONFIG_ACPI_SYSTEM_POWER_STATES_SUPPORT
extern int acpi_sleep_init(void);
#else
static inline int acpi_sleep_init(void) { return -ENXIO; }
#endif

#ifdef CONFIG_ACPI_SLEEP
void acpi_sleep_proc_init(void);
int suspend_nvs_alloc(void);
void suspend_nvs_free(void);
int suspend_nvs_save(void);
void suspend_nvs_restore(void);
#else
static inline void acpi_sleep_proc_init(void) {}
static inline int suspend_nvs_alloc(void) { return 0; }
static inline void suspend_nvs_free(void) {}
static inline int suspend_nvs_save(void) { return 0; }
static inline void suspend_nvs_restore(void) {}
#endif

/*--------------------------------------------------------------------------
				Device properties
  -------------------------------------------------------------------------- */
#define ACPI_DT_NAMESPACE_HID	"PRP0001"

void acpi_init_properties(struct acpi_device *adev);
void acpi_free_properties(struct acpi_device *adev);

#endif /* _ACPI_INTERNAL_H_ */
