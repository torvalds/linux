/* SPDX-License-Identifier: GPL-2.0 */

#ifdef CONFIG_THERMAL_DEBUGFS
void thermal_debug_init(void);
void thermal_debug_cdev_add(struct thermal_cooling_device *cdev);
void thermal_debug_cdev_remove(struct thermal_cooling_device *cdev);
void thermal_debug_cdev_state_update(const struct thermal_cooling_device *cdev, int state);
#else
static inline void thermal_debug_init(void) {}
static inline void thermal_debug_cdev_add(struct thermal_cooling_device *cdev) {}
static inline void thermal_debug_cdev_remove(struct thermal_cooling_device *cdev) {}
static inline void thermal_debug_cdev_state_update(const struct thermal_cooling_device *cdev,
						   int state) {}
#endif /* CONFIG_THERMAL_DEBUGFS */
