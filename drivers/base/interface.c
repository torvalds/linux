/*
 * drivers/base/interface.c - common driverfs interface that's exported to
 * 	the world for all devices.
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/stat.h>
#include <linux/string.h>

/**
 *	detach_state - control the default power state for the device.
 *
 *	This is the state the device enters when it's driver module is
 *	unloaded. The value is an unsigned integer, in the range of 0-4.
 *	'0' indicates 'On', so no action will be taken when the driver is
 *	unloaded. This is the default behavior.
 *	'4' indicates 'Off', meaning the driver core will call the driver's
 *	shutdown method to quiesce the device.
 *	1-3 indicate a low-power state for the device to enter via the
 *	driver's suspend method.
 */

static ssize_t detach_show(struct device * dev, char * buf)
{
	return sprintf(buf, "%u\n", dev->detach_state);
}

static ssize_t detach_store(struct device * dev, const char * buf, size_t n)
{
	u32 state;
	state = simple_strtoul(buf, NULL, 10);
	if (state > 4)
		return -EINVAL;
	dev->detach_state = state;
	return n;
}

static DEVICE_ATTR(detach_state, 0644, detach_show, detach_store);


struct attribute * dev_default_attrs[] = {
	&dev_attr_detach_state.attr,
	NULL,
};
