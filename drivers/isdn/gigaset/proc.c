/*
 * Stuff used by all variants of the driver
 *
 * Copyright (c) 2001 by Stefan Eilers <Eilers.Stefan@epost.de>,
 *                       Hansjoerg Lipp <hjlipp@web.de>,
 *                       Tilman Schmidt <tilman@imap.cc>.
 *
 * =====================================================================
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 * =====================================================================
 */

#include "gigaset.h"
#include <linux/ctype.h>

static ssize_t show_cidmode(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct cardstate *cs = usb_get_intfdata(intf);
	return sprintf(buf, "%d\n", atomic_read(&cs->cidmode));
}

static ssize_t set_cidmode(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct cardstate *cs = usb_get_intfdata(intf);
	long int value;
	char *end;

	value = simple_strtol(buf, &end, 0);
	while (*end)
		if (!isspace(*end++))
			return -EINVAL;
	if (value < 0 || value > 1)
			return -EINVAL;

	if (down_interruptible(&cs->sem))
		return -ERESTARTSYS; // FIXME -EINTR?

	cs->waiting = 1;
	if (!gigaset_add_event(cs, &cs->at_state, EV_PROC_CIDMODE,
			       NULL, value, NULL)) {
		cs->waiting = 0;
		up(&cs->sem);
		return -ENOMEM;
	}

	gig_dbg(DEBUG_CMD, "scheduling PROC_CIDMODE");
	gigaset_schedule_event(cs);

	wait_event(cs->waitqueue, !cs->waiting);

	up(&cs->sem);

	return count;
}

static DEVICE_ATTR(cidmode, S_IRUGO|S_IWUSR, show_cidmode, set_cidmode);

/* free sysfs for device */
void gigaset_free_dev_sysfs(struct usb_interface *interface)
{
	gig_dbg(DEBUG_INIT, "removing sysfs entries");
	device_remove_file(&interface->dev, &dev_attr_cidmode);
}
EXPORT_SYMBOL_GPL(gigaset_free_dev_sysfs);

/* initialize sysfs for device */
void gigaset_init_dev_sysfs(struct usb_interface *interface)
{
	gig_dbg(DEBUG_INIT, "setting up sysfs");
	device_create_file(&interface->dev, &dev_attr_cidmode);
}
EXPORT_SYMBOL_GPL(gigaset_init_dev_sysfs);
