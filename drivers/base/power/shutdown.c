/*
 * shutdown.c - power management functions for the device tree.
 *
 * Copyright (c) 2002-3 Patrick Mochel
 *		 2002-3 Open Source Development Lab
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include <asm/semaphore.h>

#include "../base.h"
#include "power.h"

#define to_dev(node) container_of(node, struct device, kobj.entry)

extern struct subsystem devices_subsys;


/**
 * We handle system devices differently - we suspend and shut them
 * down last and resume them first. That way, we don't do anything stupid like
 * shutting down the interrupt controller before any devices..
 *
 * Note that there are not different stages for power management calls -
 * they only get one called once when interrupts are disabled.
 */


/**
 * device_shutdown - call ->shutdown() on each device to shutdown.
 */
void device_shutdown(void)
{
	struct device * dev, *devn;

	list_for_each_entry_safe_reverse(dev, devn, &devices_subsys.kset.list,
				kobj.entry) {
		if (dev->bus && dev->bus->shutdown) {
			dev_dbg(dev, "shutdown\n");
			dev->bus->shutdown(dev);
		} else if (dev->driver && dev->driver->shutdown) {
			dev_dbg(dev, "shutdown\n");
			dev->driver->shutdown(dev);
		}
	}

	sysdev_shutdown();
}

