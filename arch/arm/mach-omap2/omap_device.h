/* SPDX-License-Identifier: GPL-2.0-only */
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
 * This type of functionality should be implemented as a proper
 * omap_bus/omap_device in Linux.
 *
 * omap_device differs from omap_hwmod in that it includes external
 * (e.g., board- and system-level) integration details.  omap_hwmod
 * stores hardware data that is invariant for a given OMAP chip.
 */
#ifndef __ARCH_ARM_PLAT_OMAP_INCLUDE_MACH_OMAP_DEVICE_H
#define __ARCH_ARM_PLAT_OMAP_INCLUDE_MACH_OMAP_DEVICE_H

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include "omap_hwmod.h"

extern struct dev_pm_domain omap_device_pm_domain;
extern struct dev_pm_domain omap_device_fail_pm_domain;

/* omap_device._state values */
#define OMAP_DEVICE_STATE_UNKNOWN	0
#define OMAP_DEVICE_STATE_ENABLED	1
#define OMAP_DEVICE_STATE_IDLE		2
#define OMAP_DEVICE_STATE_SHUTDOWN	3

/* omap_device.flags values */
#define OMAP_DEVICE_SUSPENDED		BIT(0)

/**
 * struct omap_device - omap_device wrapper for platform_devices
 * @pdev: platform_device
 * @hwmods: (one .. many per omap_device)
 * @hwmods_cnt: ARRAY_SIZE() of @hwmods
 * @_state: one of OMAP_DEVICE_STATE_* (see above)
 * @flags: device flags
 * @_driver_status: one of BUS_NOTIFY_*_DRIVER from <linux/device.h>
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
	unsigned long			_driver_status;
	u8				hwmods_cnt;
	u8				_state;
	u8                              flags;
};

/* Device driver interface (call via platform_data fn ptrs) */

int omap_device_enable(struct platform_device *pdev);
int omap_device_idle(struct platform_device *pdev);

/* Core code interface */

struct omap_device *omap_device_alloc(struct platform_device *pdev,
				      struct omap_hwmod **ohs, int oh_cnt);
void omap_device_delete(struct omap_device *od);

struct device *omap_device_get_by_hwmod_name(const char *oh_name);

/* OMAP PM interface */
int omap_device_get_context_loss_count(struct platform_device *pdev);

/* Other */

int omap_device_assert_hardreset(struct platform_device *pdev,
				 const char *name);
int omap_device_deassert_hardreset(struct platform_device *pdev,
				 const char *name);

/* Get omap_device pointer from platform_device pointer */
static inline struct omap_device *to_omap_device(struct platform_device *pdev)
{
	return pdev ? pdev->archdata.od : NULL;
}
#endif
