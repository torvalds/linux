/*
 * cpuidle.h - The internal header file
 */

#ifndef __DRIVER_CPUIDLE_H
#define __DRIVER_CPUIDLE_H

#include <linux/device.h>

/* For internal use only */
extern struct cpuidle_governor *cpuidle_curr_governor;
extern struct list_head cpuidle_governors;
extern struct list_head cpuidle_detected_devices;
extern struct mutex cpuidle_lock;
extern spinlock_t cpuidle_driver_lock;
extern int cpuidle_disabled(void);

/* idle loop */
extern void cpuidle_install_idle_handler(void);
extern void cpuidle_uninstall_idle_handler(void);

/* governors */
extern int cpuidle_switch_governor(struct cpuidle_governor *gov);

/* sysfs */
extern int cpuidle_add_interface(struct device *dev);
extern void cpuidle_remove_interface(struct device *dev);
extern int cpuidle_add_state_sysfs(struct cpuidle_device *device);
extern void cpuidle_remove_state_sysfs(struct cpuidle_device *device);
extern int cpuidle_add_sysfs(struct device *dev);
extern void cpuidle_remove_sysfs(struct device *dev);

#endif /* __DRIVER_CPUIDLE_H */
