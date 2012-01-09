/*
 * omap_device headers
 *
 * Copyright (C) 2009 Nokia Corporation
 * Paul Walmsley
 *
 * Developed in collaboration with (alphabetical order): Benoit
 * Cousson, Kevin Hilman, Tony Lindgren, Rajendra Nayak, Vikram
 * Pandita, Sakari Poussa, Anand Sawant, Santosh Shilimkar, Richard
 * Woodruff
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Eventually this type of functionality should either be
 * a) implemented via arch-specific pointers in platform_device
 * or
 * b) implemented as a proper omap_bus/omap_device in Linux, no more
 *    platform_device
 *
 * omap_device differs from omap_hwmod in that it includes external
 * (e.g., board- and system-level) integration details.  omap_hwmod
 * stores hardware data that is invariant for a given OMAP chip.
 *
 * To do:
 * - GPIO integration
 * - regulator integration
 *
 */
#ifndef __ARCH_ARM_PLAT_OMAP_INCLUDE_MACH_OMAP_DEVICE_H
#define __ARCH_ARM_PLAT_OMAP_INCLUDE_MACH_OMAP_DEVICE_H

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <plat/omap_hwmod.h>

extern struct device omap_device_parent;

/* omap_device._state values */
#define OMAP_DEVICE_STATE_UNKNOWN	0
#define OMAP_DEVICE_STATE_ENABLED	1
#define OMAP_DEVICE_STATE_IDLE		2
#define OMAP_DEVICE_STATE_SHUTDOWN	3

/* omap_device.flags values */
#define OMAP_DEVICE_SUSPENDED BIT(0)
#define OMAP_DEVICE_NO_IDLE_ON_SUSPEND BIT(1)

/**
 * struct omap_device - omap_device wrapper for platform_devices
 * @pdev: platform_device
 * @hwmods: (one .. many per omap_device)
 * @hwmods_cnt: ARRAY_SIZE() of @hwmods
 * @pm_lats: ptr to an omap_device_pm_latency table
 * @pm_lats_cnt: ARRAY_SIZE() of what is passed to @pm_lats
 * @pm_lat_level: array index of the last odpl entry executed - -1 if never
 * @dev_wakeup_lat: dev wakeup latency in nanoseconds
 * @_dev_wakeup_lat_limit: dev wakeup latency limit in nsec - set by OMAP PM
 * @_state: one of OMAP_DEVICE_STATE_* (see above)
 * @flags: device flags
 *
 * Integrates omap_hwmod data into Linux platform_device.
 *
 * Field names beginning with underscores are for the internal use of
 * the omap_device code.
 *
 */
struct omap_device {
	struct platform_device		*pdev;
	struct omap_hwmod		**hwmods;
	struct omap_device_pm_latency	*pm_lats;
	u32				dev_wakeup_lat;
	u32				_dev_wakeup_lat_limit;
	u8				pm_lats_cnt;
	s8				pm_lat_level;
	u8				hwmods_cnt;
	u8				_state;
	u8                              flags;
};

/* Device driver interface (call via platform_data fn ptrs) */

int omap_device_enable(struct platform_device *pdev);
int omap_device_idle(struct platform_device *pdev);
int omap_device_shutdown(struct platform_device *pdev);

/* Core code interface */

struct platform_device *omap_device_build(const char *pdev_name, int pdev_id,
				      struct omap_hwmod *oh, void *pdata,
				      int pdata_len,
				      struct omap_device_pm_latency *pm_lats,
				      int pm_lats_cnt, int is_early_device);

struct platform_device *omap_device_build_ss(const char *pdev_name, int pdev_id,
					 struct omap_hwmod **oh, int oh_cnt,
					 void *pdata, int pdata_len,
					 struct omap_device_pm_latency *pm_lats,
					 int pm_lats_cnt, int is_early_device);

void __iomem *omap_device_get_rt_va(struct omap_device *od);
struct device *omap_device_get_by_hwmod_name(const char *oh_name);

/* OMAP PM interface */
int omap_device_align_pm_lat(struct platform_device *pdev,
			     u32 new_wakeup_lat_limit);
struct powerdomain *omap_device_get_pwrdm(struct omap_device *od);
int omap_device_get_context_loss_count(struct platform_device *pdev);

/* Other */

int omap_device_idle_hwmods(struct omap_device *od);
int omap_device_enable_hwmods(struct omap_device *od);

int omap_device_disable_clocks(struct omap_device *od);
int omap_device_enable_clocks(struct omap_device *od);

/*
 * Entries should be kept in latency order ascending
 *
 * deact_lat is the maximum number of microseconds required to complete
 * deactivate_func() at the device's slowest OPP.
 *
 * act_lat is the maximum number of microseconds required to complete
 * activate_func() at the device's slowest OPP.
 *
 * This will result in some suboptimal power management decisions at fast
 * OPPs, but avoids having to recompute all device power management decisions
 * if the system shifts from a fast OPP to a slow OPP (in order to meet
 * latency requirements).
 *
 * XXX should deactivate_func/activate_func() take platform_device pointers
 * rather than omap_device pointers?
 */
struct omap_device_pm_latency {
	u32 deactivate_lat;
	u32 deactivate_lat_worst;
	int (*deactivate_func)(struct omap_device *od);
	u32 activate_lat;
	u32 activate_lat_worst;
	int (*activate_func)(struct omap_device *od);
	u32 flags;
};

#define OMAP_DEVICE_LATENCY_AUTO_ADJUST BIT(1)

/* Get omap_device pointer from platform_device pointer */
static inline struct omap_device *to_omap_device(struct platform_device *pdev)
{
	return pdev ? pdev->archdata.od : NULL;
}

static inline
void omap_device_disable_idle_on_suspend(struct platform_device *pdev)
{
	struct omap_device *od = to_omap_device(pdev);

	od->flags |= OMAP_DEVICE_NO_IDLE_ON_SUSPEND;
}

#endif
