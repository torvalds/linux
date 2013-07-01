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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _ACPI_INTERNAL_H_
#define _ACPI_INTERNAL_H_

#define PREFIX "ACPI: "

int init_acpi_device_notify(void);
int acpi_scan_init(void);
#ifdef	CONFIG_ACPI_PCI_SLOT
void acpi_pci_slot_init(void);
#else
static inline void acpi_pci_slot_init(void) { }
#endif
void acpi_pci_root_init(void);
void acpi_pci_link_init(void);
void acpi_pci_root_hp_init(void);
void acpi_platform_init(void);
int acpi_sysfs_init(void);
#ifdef CONFIG_ACPI_CONTAINER
void acpi_container_init(void);
#else
static inline void acpi_container_init(void) {}
#endif
#ifdef CONFIG_ACPI_DOCK
void acpi_dock_init(void);
#else
static inline void acpi_dock_init(void) {}
#endif
#ifdef CONFIG_ACPI_HOTPLUG_MEMORY
void acpi_memory_hotplug_init(void);
#else
static inline void acpi_memory_hotplug_init(void) {}
#endif

void acpi_sysfs_add_hotplug_profile(struct acpi_hotplug_profile *hotplug,
				    const char *name);
int acpi_scan_add_handler_with_hotplug(struct acpi_scan_handler *handler,
				       const char *hotplug_profile_name);
void acpi_scan_hotplug_enabled(struct acpi_hotplug_profile *hotplug, bool val);

#ifdef CONFIG_DEBUG_FS
extern struct dentry *acpi_debugfs_dir;
int acpi_debugfs_init(void);
#else
static inline void acpi_debugfs_init(void) { return; }
#endif
#ifdef CONFIG_X86_INTEL_LPSS
void acpi_lpss_init(void);
#else
static inline void acpi_lpss_init(void) {}
#endif

/* --------------------------------------------------------------------------
                     Device Node Initialization / Removal
   -------------------------------------------------------------------------- */
#define ACPI_STA_DEFAULT (ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED | \
			  ACPI_STA_DEVICE_UI | ACPI_STA_DEVICE_FUNCTIONING)

int acpi_device_add(struct acpi_device *device,
		    void (*release)(struct device *));
void acpi_init_device_object(struct acpi_device *device, acpi_handle handle,
			     int type, unsigned long long sta);
void acpi_device_add_finalize(struct acpi_device *device);
void acpi_free_pnp_ids(struct acpi_device_pnp *pnp);

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
void acpi_early_processor_set_pdc(void);

/* --------------------------------------------------------------------------
                                  Embedded Controller
   -------------------------------------------------------------------------- */
struct acpi_ec {
	acpi_handle handle;
	unsigned long gpe;
	unsigned long command_addr;
	unsigned long data_addr;
	unsigned long global_lock;
	unsigned long flags;
	struct mutex mutex;
	wait_queue_head_t wait;
	struct list_head list;
	struct transaction *curr;
	spinlock_t lock;
};

extern struct acpi_ec *first_ec;

int acpi_ec_init(void);
int acpi_ec_ecdt_probe(void);
int acpi_boot_ec_enable(void);
void acpi_ec_block_transactions(void);
void acpi_ec_unblock_transactions(void);
void acpi_ec_unblock_transactions_early(void);

/*--------------------------------------------------------------------------
                                  Suspend/Resume
  -------------------------------------------------------------------------- */
extern int acpi_sleep_init(void);

#ifdef CONFIG_ACPI_SLEEP
int acpi_sleep_proc_init(void);
int suspend_nvs_alloc(void);
void suspend_nvs_free(void);
int suspend_nvs_save(void);
void suspend_nvs_restore(void);
#else
static inline int acpi_sleep_proc_init(void) { return 0; }
static inline int suspend_nvs_alloc(void) { return 0; }
static inline void suspend_nvs_free(void) {}
static inline int suspend_nvs_save(void) { return 0; }
static inline void suspend_nvs_restore(void) {}
#endif

/*--------------------------------------------------------------------------
				Platform bus support
  -------------------------------------------------------------------------- */
struct platform_device;

int acpi_create_platform_device(struct acpi_device *adev,
				const struct acpi_device_id *id);

#endif /* _ACPI_INTERNAL_H_ */
