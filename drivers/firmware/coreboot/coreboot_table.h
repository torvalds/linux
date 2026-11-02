/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * coreboot_table.h
 *
 * Internal header for coreboot table access.
 *
 * Copyright 2014 Gerd Hoffmann <kraxel@redhat.com>
 * Copyright 2017 Google Inc.
 * Copyright 2017 Samuel Holland <samuel@sholland.org>
 */

#ifndef __COREBOOT_TABLE_H
#define __COREBOOT_TABLE_H

#include <linux/coreboot.h>
#include <linux/device.h>

/* A device, additionally with information from coreboot. */
struct coreboot_device {
	struct device dev;
	union {
		struct coreboot_table_entry entry;
		struct lb_cbmem_ref cbmem_ref;
		struct lb_cbmem_entry cbmem_entry;
		struct lb_framebuffer framebuffer;
		DECLARE_FLEX_ARRAY(u8, raw);
	};
};

static inline struct coreboot_device *dev_to_coreboot_device(struct device *dev)
{
	return container_of(dev, struct coreboot_device, dev);
}

/* A driver for handling devices described in coreboot tables. */
struct coreboot_driver {
	int (*probe)(struct coreboot_device *);
	void (*remove)(struct coreboot_device *);
	struct device_driver drv;
	const struct coreboot_device_id *id_table;
};

/* use a macro to avoid include chaining to get THIS_MODULE */
#define coreboot_driver_register(driver) \
	__coreboot_driver_register(driver, THIS_MODULE)
/* Register a driver that uses the data from a coreboot table. */
int __coreboot_driver_register(struct coreboot_driver *driver,
			       struct module *owner);

/* Unregister a driver that uses the data from a coreboot table. */
void coreboot_driver_unregister(struct coreboot_driver *driver);

/* module_coreboot_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#define module_coreboot_driver(__coreboot_driver) \
	module_driver(__coreboot_driver, coreboot_driver_register, \
			coreboot_driver_unregister)

#endif /* __COREBOOT_TABLE_H */
