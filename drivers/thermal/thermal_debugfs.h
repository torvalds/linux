/* SPDX-License-Identifier: GPL-2.0 */

#ifdef CONFIG_THERMAL_DEBUGFS
void thermal_debug_init(void);
void thermal_debug_cdev_add(struct thermal_cooling_device *cdev, int state);
void thermal_debug_cdev_remove(struct thermal_cooling_device *cdev);
void thermal_debug_cdev_state_update(const struct thermal_cooling_device *cdev, int state);
void thermal_debug_tz_add(struct thermal_zone_device *tz);
void thermal_debug_tz_remove(struct thermal_zone_device *tz);
void thermal_debug_tz_resume(struct thermal_zone_device *tz);
void thermal_debug_tz_trip_up(struct thermal_zone_device *tz,
			      const struct thermal_trip *trip);
void thermal_debug_tz_trip_down(struct thermal_zone_device *tz,
				const struct thermal_trip *trip);
void thermal_debug_update_trip_stats(struct thermal_zone_device *tz);
#else
static inline void thermal_debug_init(void) {}
static inline void thermal_debug_cdev_add(struct thermal_cooling_device *cdev, int state) {}
static inline void thermal_debug_cdev_remove(struct thermal_cooling_device *cdev) {}
static inline void thermal_debug_cdev_state_update(const struct thermal_cooling_device *cdev,
						   int state) {}
static inline void thermal_debug_tz_add(struct thermal_zone_device *tz) {}
static inline void thermal_debug_tz_remove(struct thermal_zone_device *tz) {}
static inline void thermal_debug_tz_resume(struct thermal_zone_device *tz) {}
static inline void thermal_debug_tz_trip_up(struct thermal_zone_device *tz,
					    const struct thermal_trip *trip) {};
static inline void thermal_debug_tz_trip_down(struct thermal_zone_device *tz,
					      const struct thermal_trip *trip) {}
static inline void thermal_debug_update_trip_stats(struct thermal_zone_device *tz) {}
#endif /* CONFIG_THERMAL_DEBUGFS */
