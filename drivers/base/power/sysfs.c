/*
 * drivers/base/power/sysfs.c - sysfs entries for device PM
 */

#include <linux/device.h>
#include <linux/string.h>
#include "power.h"


#ifdef	CONFIG_PM_SYSFS_DEPRECATED

/**
 *	state - Control current power state of device
 *
 *	show() returns the current power state of the device. '0' indicates
 *	the device is on. Other values (2) indicate the device is in some low
 *	power state.
 *
 *	store() sets the current power state, which is an integer valued
 *	0, 2, or 3.  Devices with bus.suspend_late(), or bus.resume_early()
 *	methods fail this operation; those methods couldn't be called.
 *	Otherwise,
 *
 *	- If the recorded dev->power.power_state.event matches the
 *	  target value, nothing is done.
 *	- If the recorded event code is nonzero, the device is reactivated
 *	  by calling bus.resume() and/or class.resume().
 *	- If the target value is nonzero, the device is suspended by
 *	  calling class.suspend() and/or bus.suspend() with event code
 *	  PM_EVENT_SUSPEND.
 *
 *	This mechanism is DEPRECATED and should only be used for testing.
 */

static ssize_t state_show(struct device * dev, struct device_attribute *attr, char * buf)
{
	if (dev->power.power_state.event)
		return sprintf(buf, "2\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t state_store(struct device * dev, struct device_attribute *attr, const char * buf, size_t n)
{
	pm_message_t state;
	int error = -EINVAL;

	/* disallow incomplete suspend sequences */
	if (dev->bus && (dev->bus->suspend_late || dev->bus->resume_early))
		return error;

	state.event = PM_EVENT_SUSPEND;
	/* Older apps expected to write "3" here - confused with PCI D3 */
	if ((n == 1) && !strcmp(buf, "3"))
		error = dpm_runtime_suspend(dev, state);

	if ((n == 1) && !strcmp(buf, "2"))
		error = dpm_runtime_suspend(dev, state);

	if ((n == 1) && !strcmp(buf, "0")) {
		dpm_runtime_resume(dev);
		error = 0;
	}

	return error ? error : n;
}

static DEVICE_ATTR(state, 0644, state_show, state_store);


#endif	/* CONFIG_PM_SYSFS_DEPRECATED */

/*
 *	wakeup - Report/change current wakeup option for device
 *
 *	Some devices support "wakeup" events, which are hardware signals
 *	used to activate devices from suspended or low power states.  Such
 *	devices have one of three values for the sysfs power/wakeup file:
 *
 *	 + "enabled\n" to issue the events;
 *	 + "disabled\n" not to do so; or
 *	 + "\n" for temporary or permanent inability to issue wakeup.
 *
 *	(For example, unconfigured USB devices can't issue wakeups.)
 *
 *	Familiar examples of devices that can issue wakeup events include
 *	keyboards and mice (both PS2 and USB styles), power buttons, modems,
 *	"Wake-On-LAN" Ethernet links, GPIO lines, and more.  Some events
 *	will wake the entire system from a suspend state; others may just
 *	wake up the device (if the system as a whole is already active).
 *	Some wakeup events use normal IRQ lines; other use special out
 *	of band signaling.
 *
 *	It is the responsibility of device drivers to enable (or disable)
 *	wakeup signaling as part of changing device power states, respecting
 *	the policy choices provided through the driver model.
 *
 *	Devices may not be able to generate wakeup events from all power
 *	states.  Also, the events may be ignored in some configurations;
 *	for example, they might need help from other devices that aren't
 *	active, or which may have wakeup disabled.  Some drivers rely on
 *	wakeup events internally (unless they are disabled), keeping
 *	their hardware in low power modes whenever they're unused.  This
 *	saves runtime power, without requiring system-wide sleep states.
 */

static const char enabled[] = "enabled";
static const char disabled[] = "disabled";

static ssize_t
wake_show(struct device * dev, struct device_attribute *attr, char * buf)
{
	return sprintf(buf, "%s\n", device_can_wakeup(dev)
		? (device_may_wakeup(dev) ? enabled : disabled)
		: "");
}

static ssize_t
wake_store(struct device * dev, struct device_attribute *attr,
	const char * buf, size_t n)
{
	char *cp;
	int len = n;

	if (!device_can_wakeup(dev))
		return -EINVAL;

	cp = memchr(buf, '\n', n);
	if (cp)
		len = cp - buf;
	if (len == sizeof enabled - 1
			&& strncmp(buf, enabled, sizeof enabled - 1) == 0)
		device_set_wakeup_enable(dev, 1);
	else if (len == sizeof disabled - 1
			&& strncmp(buf, disabled, sizeof disabled - 1) == 0)
		device_set_wakeup_enable(dev, 0);
	else
		return -EINVAL;
	return n;
}

static DEVICE_ATTR(wakeup, 0644, wake_show, wake_store);


static struct attribute * power_attrs[] = {
#ifdef	CONFIG_PM_SYSFS_DEPRECATED
	&dev_attr_state.attr,
#endif
	&dev_attr_wakeup.attr,
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
