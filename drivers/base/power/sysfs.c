/*
 * drivers/base/power/sysfs.c - sysfs entries for device PM
 */

#include <linux/device.h>
#include <linux/string.h>
#include <linux/pm_runtime.h>
#include <asm/atomic.h>
#include <linux/jiffies.h>
#include "power.h"

/*
 *	control - Report/change current runtime PM setting of the device
 *
 *	Runtime power management of a device can be blocked with the help of
 *	this attribute.  All devices have one of the following two values for
 *	the power/control file:
 *
 *	 + "auto\n" to allow the device to be power managed at run time;
 *	 + "on\n" to prevent the device from being power managed at run time;
 *
 *	The default for all devices is "auto", which means that devices may be
 *	subject to automatic power management, depending on their drivers.
 *	Changing this attribute to "on" prevents the driver from power managing
 *	the device at run time.  Doing that while the device is suspended causes
 *	it to be woken up.
 *
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
 *
 *	async - Report/change current async suspend setting for the device
 *
 *	Asynchronous suspend and resume of the device during system-wide power
 *	state transitions can be enabled by writing "enabled" to this file.
 *	Analogously, if "disabled" is written to this file, the device will be
 *	suspended and resumed synchronously.
 *
 *	All devices have one of the following two values for power/async:
 *
 *	 + "enabled\n" to permit the asynchronous suspend/resume of the device;
 *	 + "disabled\n" to forbid it;
 *
 *	NOTE: It generally is unsafe to permit the asynchronous suspend/resume
 *	of a device unless it is certain that all of the PM dependencies of the
 *	device are known to the PM core.  However, for some devices this
 *	attribute is set to "enabled" by bus type code or device drivers and in
 *	that cases it should be safe to leave the default value.
 *
 *	autosuspend_delay_ms - Report/change a device's autosuspend_delay value
 *
 *	Some drivers don't want to carry out a runtime suspend as soon as a
 *	device becomes idle; they want it always to remain idle for some period
 *	of time before suspending it.  This period is the autosuspend_delay
 *	value (expressed in milliseconds) and it can be controlled by the user.
 *	If the value is negative then the device will never be runtime
 *	suspended.
 *
 *	NOTE: The autosuspend_delay_ms attribute and the autosuspend_delay
 *	value are used only if the driver calls pm_runtime_use_autosuspend().
 *
 *	wakeup_count - Report the number of wakeup events related to the device
 */

static const char enabled[] = "enabled";
static const char disabled[] = "disabled";

const char power_group_name[] = "power";
EXPORT_SYMBOL_GPL(power_group_name);

#ifdef CONFIG_PM_RUNTIME
static const char ctrl_auto[] = "auto";
static const char ctrl_on[] = "on";

static ssize_t control_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%s\n",
				dev->power.runtime_auto ? ctrl_auto : ctrl_on);
}

static ssize_t control_store(struct device * dev, struct device_attribute *attr,
			     const char * buf, size_t n)
{
	char *cp;
	int len = n;

	cp = memchr(buf, '\n', n);
	if (cp)
		len = cp - buf;
	if (len == sizeof ctrl_auto - 1 && strncmp(buf, ctrl_auto, len) == 0)
		pm_runtime_allow(dev);
	else if (len == sizeof ctrl_on - 1 && strncmp(buf, ctrl_on, len) == 0)
		pm_runtime_forbid(dev);
	else
		return -EINVAL;
	return n;
}

static DEVICE_ATTR(control, 0644, control_show, control_store);

static ssize_t rtpm_active_time_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;
	spin_lock_irq(&dev->power.lock);
	update_pm_runtime_accounting(dev);
	ret = sprintf(buf, "%i\n", jiffies_to_msecs(dev->power.active_jiffies));
	spin_unlock_irq(&dev->power.lock);
	return ret;
}

static DEVICE_ATTR(runtime_active_time, 0444, rtpm_active_time_show, NULL);

static ssize_t rtpm_suspended_time_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret;
	spin_lock_irq(&dev->power.lock);
	update_pm_runtime_accounting(dev);
	ret = sprintf(buf, "%i\n",
		jiffies_to_msecs(dev->power.suspended_jiffies));
	spin_unlock_irq(&dev->power.lock);
	return ret;
}

static DEVICE_ATTR(runtime_suspended_time, 0444, rtpm_suspended_time_show, NULL);

static ssize_t rtpm_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	const char *p;

	if (dev->power.runtime_error) {
		p = "error\n";
	} else if (dev->power.disable_depth) {
		p = "unsupported\n";
	} else {
		switch (dev->power.runtime_status) {
		case RPM_SUSPENDED:
			p = "suspended\n";
			break;
		case RPM_SUSPENDING:
			p = "suspending\n";
			break;
		case RPM_RESUMING:
			p = "resuming\n";
			break;
		case RPM_ACTIVE:
			p = "active\n";
			break;
		default:
			return -EIO;
		}
	}
	return sprintf(buf, p);
}

static DEVICE_ATTR(runtime_status, 0444, rtpm_status_show, NULL);

static ssize_t autosuspend_delay_ms_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	if (!dev->power.use_autosuspend)
		return -EIO;
	return sprintf(buf, "%d\n", dev->power.autosuspend_delay);
}

static ssize_t autosuspend_delay_ms_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	long delay;

	if (!dev->power.use_autosuspend)
		return -EIO;

	if (strict_strtol(buf, 10, &delay) != 0 || delay != (int) delay)
		return -EINVAL;

	pm_runtime_set_autosuspend_delay(dev, delay);
	return n;
}

static DEVICE_ATTR(autosuspend_delay_ms, 0644, autosuspend_delay_ms_show,
		autosuspend_delay_ms_store);

#endif

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

#ifdef CONFIG_PM_SLEEP
static ssize_t wakeup_count_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned long count = 0;
	bool enabled = false;

	spin_lock_irq(&dev->power.lock);
	if (dev->power.wakeup) {
		count = dev->power.wakeup->event_count;
		enabled = true;
	}
	spin_unlock_irq(&dev->power.lock);
	return enabled ? sprintf(buf, "%lu\n", count) : sprintf(buf, "\n");
}

static DEVICE_ATTR(wakeup_count, 0444, wakeup_count_show, NULL);

static ssize_t wakeup_active_count_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned long count = 0;
	bool enabled = false;

	spin_lock_irq(&dev->power.lock);
	if (dev->power.wakeup) {
		count = dev->power.wakeup->active_count;
		enabled = true;
	}
	spin_unlock_irq(&dev->power.lock);
	return enabled ? sprintf(buf, "%lu\n", count) : sprintf(buf, "\n");
}

static DEVICE_ATTR(wakeup_active_count, 0444, wakeup_active_count_show, NULL);

static ssize_t wakeup_hit_count_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned long count = 0;
	bool enabled = false;

	spin_lock_irq(&dev->power.lock);
	if (dev->power.wakeup) {
		count = dev->power.wakeup->hit_count;
		enabled = true;
	}
	spin_unlock_irq(&dev->power.lock);
	return enabled ? sprintf(buf, "%lu\n", count) : sprintf(buf, "\n");
}

static DEVICE_ATTR(wakeup_hit_count, 0444, wakeup_hit_count_show, NULL);

static ssize_t wakeup_active_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	unsigned int active = 0;
	bool enabled = false;

	spin_lock_irq(&dev->power.lock);
	if (dev->power.wakeup) {
		active = dev->power.wakeup->active;
		enabled = true;
	}
	spin_unlock_irq(&dev->power.lock);
	return enabled ? sprintf(buf, "%u\n", active) : sprintf(buf, "\n");
}

static DEVICE_ATTR(wakeup_active, 0444, wakeup_active_show, NULL);

static ssize_t wakeup_total_time_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	s64 msec = 0;
	bool enabled = false;

	spin_lock_irq(&dev->power.lock);
	if (dev->power.wakeup) {
		msec = ktime_to_ms(dev->power.wakeup->total_time);
		enabled = true;
	}
	spin_unlock_irq(&dev->power.lock);
	return enabled ? sprintf(buf, "%lld\n", msec) : sprintf(buf, "\n");
}

static DEVICE_ATTR(wakeup_total_time_ms, 0444, wakeup_total_time_show, NULL);

static ssize_t wakeup_max_time_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	s64 msec = 0;
	bool enabled = false;

	spin_lock_irq(&dev->power.lock);
	if (dev->power.wakeup) {
		msec = ktime_to_ms(dev->power.wakeup->max_time);
		enabled = true;
	}
	spin_unlock_irq(&dev->power.lock);
	return enabled ? sprintf(buf, "%lld\n", msec) : sprintf(buf, "\n");
}

static DEVICE_ATTR(wakeup_max_time_ms, 0444, wakeup_max_time_show, NULL);

static ssize_t wakeup_last_time_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	s64 msec = 0;
	bool enabled = false;

	spin_lock_irq(&dev->power.lock);
	if (dev->power.wakeup) {
		msec = ktime_to_ms(dev->power.wakeup->last_time);
		enabled = true;
	}
	spin_unlock_irq(&dev->power.lock);
	return enabled ? sprintf(buf, "%lld\n", msec) : sprintf(buf, "\n");
}

static DEVICE_ATTR(wakeup_last_time_ms, 0444, wakeup_last_time_show, NULL);
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM_ADVANCED_DEBUG
#ifdef CONFIG_PM_RUNTIME

static ssize_t rtpm_usagecount_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", atomic_read(&dev->power.usage_count));
}

static ssize_t rtpm_children_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", dev->power.ignore_children ?
		0 : atomic_read(&dev->power.child_count));
}

static ssize_t rtpm_enabled_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	if ((dev->power.disable_depth) && (dev->power.runtime_auto == false))
		return sprintf(buf, "disabled & forbidden\n");
	else if (dev->power.disable_depth)
		return sprintf(buf, "disabled\n");
	else if (dev->power.runtime_auto == false)
		return sprintf(buf, "forbidden\n");
	return sprintf(buf, "enabled\n");
}

static DEVICE_ATTR(runtime_usage, 0444, rtpm_usagecount_show, NULL);
static DEVICE_ATTR(runtime_active_kids, 0444, rtpm_children_show, NULL);
static DEVICE_ATTR(runtime_enabled, 0444, rtpm_enabled_show, NULL);

#endif

static ssize_t async_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	return sprintf(buf, "%s\n",
			device_async_suspend_enabled(dev) ? enabled : disabled);
}

static ssize_t async_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t n)
{
	char *cp;
	int len = n;

	cp = memchr(buf, '\n', n);
	if (cp)
		len = cp - buf;
	if (len == sizeof enabled - 1 && strncmp(buf, enabled, len) == 0)
		device_enable_async_suspend(dev);
	else if (len == sizeof disabled - 1 && strncmp(buf, disabled, len) == 0)
		device_disable_async_suspend(dev);
	else
		return -EINVAL;
	return n;
}

static DEVICE_ATTR(async, 0644, async_show, async_store);
#endif /* CONFIG_PM_ADVANCED_DEBUG */

static struct attribute * power_attrs[] = {
	&dev_attr_wakeup.attr,
#ifdef CONFIG_PM_SLEEP
	&dev_attr_wakeup_count.attr,
	&dev_attr_wakeup_active_count.attr,
	&dev_attr_wakeup_hit_count.attr,
	&dev_attr_wakeup_active.attr,
	&dev_attr_wakeup_total_time_ms.attr,
	&dev_attr_wakeup_max_time_ms.attr,
	&dev_attr_wakeup_last_time_ms.attr,
#endif
#ifdef CONFIG_PM_ADVANCED_DEBUG
	&dev_attr_async.attr,
#ifdef CONFIG_PM_RUNTIME
	&dev_attr_runtime_status.attr,
	&dev_attr_runtime_usage.attr,
	&dev_attr_runtime_active_kids.attr,
	&dev_attr_runtime_enabled.attr,
#endif
#endif
	NULL,
};
static struct attribute_group pm_attr_group = {
	.name	= power_group_name,
	.attrs	= power_attrs,
};

#ifdef CONFIG_PM_RUNTIME

static struct attribute *runtime_attrs[] = {
#ifndef CONFIG_PM_ADVANCED_DEBUG
	&dev_attr_runtime_status.attr,
#endif
	&dev_attr_control.attr,
	&dev_attr_runtime_suspended_time.attr,
	&dev_attr_runtime_active_time.attr,
	&dev_attr_autosuspend_delay_ms.attr,
	NULL,
};
static struct attribute_group pm_runtime_attr_group = {
	.name	= power_group_name,
	.attrs	= runtime_attrs,
};

int dpm_sysfs_add(struct device *dev)
{
	int rc;

	rc = sysfs_create_group(&dev->kobj, &pm_attr_group);
	if (rc == 0 && !dev->power.no_callbacks) {
		rc = sysfs_merge_group(&dev->kobj, &pm_runtime_attr_group);
		if (rc)
			sysfs_remove_group(&dev->kobj, &pm_attr_group);
	}
	return rc;
}

void rpm_sysfs_remove(struct device *dev)
{
	sysfs_unmerge_group(&dev->kobj, &pm_runtime_attr_group);
}

void dpm_sysfs_remove(struct device *dev)
{
	rpm_sysfs_remove(dev);
	sysfs_remove_group(&dev->kobj, &pm_attr_group);
}

#else /* CONFIG_PM_RUNTIME */

int dpm_sysfs_add(struct device * dev)
{
	return sysfs_create_group(&dev->kobj, &pm_attr_group);
}

void dpm_sysfs_remove(struct device * dev)
{
	sysfs_remove_group(&dev->kobj, &pm_attr_group);
}

#endif
