/*
 * drivers/base/power/sysfs.c - sysfs entries for device PM
 */

#include <linux/device.h>
#include "power.h"


/**
 *	state - Control current power state of device
 *
 *	show() returns the current power state of the device. '0' indicates
 *	the device is on. Other values (1-3) indicate the device is in a low
 *	power state.
 *
 *	store() sets the current power state, which is an integer value
 *	between 0-3. If the device is on ('0'), and the value written is
 *	greater than 0, then the device is placed directly into the low-power
 *	state (via its driver's ->suspend() method).
 *	If the device is currently in a low-power state, and the value is 0,
 *	the device is powered back on (via the ->resume() method).
 *	If the device is in a low-power state, and a different low-power state
 *	is requested, the device is first resumed, then suspended into the new
 *	low-power state.
 */

static ssize_t state_show(struct device * dev, struct device_attribute *attr, char * buf)
{
	return sprintf(buf, "%u\n", dev->power.power_state.event);
}

static ssize_t state_store(struct device * dev, struct device_attribute *attr, const char * buf, size_t n)
{
	pm_message_t state;
	char * rest;
	int error = 0;

	state.event = simple_strtoul(buf, &rest, 10);
	if (*rest)
		return -EINVAL;
	if (state.event)
		error = dpm_runtime_suspend(dev, state);
	else
		dpm_runtime_resume(dev);
	return error ? error : n;
}

static DEVICE_ATTR(state, 0644, state_show, state_store);


static struct attribute * power_attrs[] = {
	&dev_attr_state.attr,
	NULL,
};
static struct attribute_group pm_attr_group = {
	.name	= "power",
	.attrs	= power_attrs,
};

int dpm_sysfs_add(struct device * dev)
{
	return sysfs_create_group(&dev->kobj, &pm_attr_group);
}

void dpm_sysfs_remove(struct device * dev)
{
	sysfs_remove_group(&dev->kobj, &pm_attr_group);
}
