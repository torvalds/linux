/*
 * Stuff used by all variants of the driver
 *
 * Copyright (c) 2001 by Stefan Eilers,
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

static ssize_t show_cidmode(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	int ret;
	unsigned long flags;
	struct cardstate *cs = dev_get_drvdata(dev);

	spin_lock_irqsave(&cs->lock, flags);
	ret = sprintf(buf, "%u\n", cs->cidmode);
	spin_unlock_irqrestore(&cs->lock, flags);

	return ret;
}

static ssize_t set_cidmode(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct cardstate *cs = dev_get_drvdata(dev);
	long int value;
	char *end;

	value = simple_strtol(buf, &end, 0);
	while (*end)
		if (!isspace(*end++))
			return -EINVAL;
	if (value < 0 || value > 1)
			return -EINVAL;

	if (mutex_lock_interruptible(&cs->mutex))
		return -ERESTARTSYS; // FIXME -EINTR?

	cs->waiting = 1;
	if (!gigaset_add_event(cs, &cs->at_state, EV_PROC_CIDMODE,
			       NULL, value, NULL)) {
		cs->waiting = 0;
		mutex_unlock(&cs->mutex);
		return -ENOMEM;
	}

	gig_dbg(DEBUG_CMD, "scheduling PROC_CIDMODE");
	gigaset_schedule_event(cs);

	wait_event(cs->waitqueue, !cs->waiting);

	mutex_unlock(&cs->mutex);

	return count;
}

static DEVICE_ATTR(cidmode, S_IRUGO|S_IWUSR, show_cidmode, set_cidmode);

/* free sysfs for device */
void gigaset_free_dev_sysfs(struct cardstate *cs)
{
	if (!cs->tty_dev)
		return;

	gig_dbg(DEBUG_INIT, "removing sysfs entries");
	device_remove_file(cs->tty_dev, &dev_attr_cidmode);
}

/* initialize sysfs for device */
void gigaset_init_dev_sysfs(struct cardstate *cs)
{
	if (!cs->tty_dev)
		return;

	gig_dbg(DEBUG_INIT, "setting up sysfs");
	if (device_create_file(cs->tty_dev, &dev_attr_cidmode))
		dev_err(cs->dev, "could not create sysfs attribute\n");
}
