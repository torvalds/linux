/*
 * drivers/base/power/generic_ops.c - Generic PM callbacks for subsystems
 *
 * Copyright (c) 2010 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file is released under the GPLv2.
 */

#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/export.h>

#ifdef CONFIG_PM_RUNTIME
/**
 * pm_generic_runtime_idle - Generic runtime idle callback for subsystems.
 * @dev: Device to handle.
 *
 * If PM operations are defined for the @dev's driver and they include
 * ->runtime_idle(), execute it and return its error code, if nonzero.
 * Otherwise, execute pm_runtime_suspend() for the device and return 0.
 */
int pm_generic_runtime_idle(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (pm && pm->runtime_idle) {
		int ret = pm->runtime_idle(dev);
		if (ret)
			return ret;
	}

	pm_runtime_suspend(dev);
	return 0;
}
EXPORT_SYMBOL_GPL(pm_generic_runtime_idle);

/**
 * pm_generic_runtime_suspend - Generic runtime suspend callback for subsystems.
 * @dev: Device to suspend.
 *
 * If PM operations are defined for the @dev's driver and they include
 * ->runtime_suspend(), execute it and return its error code.  Otherwise,
 * return 0.
 */
int pm_generic_runtime_suspend(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	int ret;

	ret = pm && pm->runtime_suspend ? pm->runtime_suspend(dev) : 0;

	return ret;
}
EXPORT_SYMBOL_GPL(pm_generic_runtime_suspend);

/**
 * pm_generic_runtime_resume - Generic runtime resume callback for subsystems.
 * @dev: Device to resume.
 *
 * If PM operations are defined for the @dev's driver and they include
 * ->runtime_resume(), execute it and return its error code.  Otherwise,
 * return 0.
 */
int pm_generic_runtime_resume(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	int ret;

	ret = pm && pm->runtime_resume ? pm->runtime_resume(dev) : 0;

	return ret;
}
EXPORT_SYMBOL_GPL(pm_generic_runtime_resume);
#endif /* CONFIG_PM_RUNTIME */

#ifdef CONFIG_PM_SLEEP
/**
 * pm_generic_prepare - Generic routine preparing a device for power transition.
 * @dev: Device to prepare.
 *
 * Prepare a device for a system-wide power transition.
 */
int pm_generic_prepare(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (drv && drv->pm && drv->pm->prepare)
		ret = drv->pm->prepare(dev);

	return ret;
}

/**
 * __pm_generic_call - Generic suspend/freeze/poweroff/thaw subsystem callback.
 * @dev: Device to handle.
 * @event: PM transition of the system under way.
 * @bool: Whether or not this is the "noirq" stage.
 *
 * If the device has not been suspended at run time, execute the
 * suspend/freeze/poweroff/thaw callback provided by its driver, if defined, and
 * return its error code.  Otherwise, return zero.
 */
static int __pm_generic_call(struct device *dev, int event, bool noirq)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	int (*callback)(struct device *);

	if (!pm || pm_runtime_suspended(dev))
		return 0;

	switch (event) {
	case PM_EVENT_SUSPEND:
		callback = noirq ? pm->suspend_noirq : pm->suspend;
		break;
	case PM_EVENT_FREEZE:
		callback = noirq ? pm->freeze_noirq : pm->freeze;
		break;
	case PM_EVENT_HIBERNATE:
		callback = noirq ? pm->poweroff_noirq : pm->poweroff;
		break;
	case PM_EVENT_THAW:
		callback = noirq ? pm->thaw_noirq : pm->thaw;
		break;
	default:
		callback = NULL;
		break;
	}

	return callback ? callback(dev) : 0;
}

/**
 * pm_generic_suspend_noirq - Generic suspend_noirq callback for subsystems.
 * @dev: Device to suspend.
 */
int pm_generic_suspend_noirq(struct device *dev)
{
	return __pm_generic_call(dev, PM_EVENT_SUSPEND, true);
}
EXPORT_SYMBOL_GPL(pm_generic_suspend_noirq);

/**
 * pm_generic_suspend - Generic suspend callback for subsystems.
 * @dev: Device to suspend.
 */
int pm_generic_suspend(struct device *dev)
{
	return __pm_generic_call(dev, PM_EVENT_SUSPEND, false);
}
EXPORT_SYMBOL_GPL(pm_generic_suspend);

/**
 * pm_generic_freeze_noirq - Generic freeze_noirq callback for subsystems.
 * @dev: Device to freeze.
 */
int pm_generic_freeze_noirq(struct device *dev)
{
	return __pm_generic_call(dev, PM_EVENT_FREEZE, true);
}
EXPORT_SYMBOL_GPL(pm_generic_freeze_noirq);

/**
 * pm_generic_freeze - Generic freeze callback for subsystems.
 * @dev: Device to freeze.
 */
int pm_generic_freeze(struct device *dev)
{
	return __pm_generic_call(dev, PM_EVENT_FREEZE, false);
}
EXPORT_SYMBOL_GPL(pm_generic_freeze);

/**
 * pm_generic_poweroff_noirq - Generic poweroff_noirq callback for subsystems.
 * @dev: Device to handle.
 */
int pm_generic_poweroff_noirq(struct device *dev)
{
	return __pm_generic_call(dev, PM_EVENT_HIBERNATE, true);
}
EXPORT_SYMBOL_GPL(pm_generic_poweroff_noirq);

/**
 * pm_generic_poweroff - Generic poweroff callback for subsystems.
 * @dev: Device to handle.
 */
int pm_generic_poweroff(struct device *dev)
{
	return __pm_generic_call(dev, PM_EVENT_HIBERNATE, false);
}
EXPORT_SYMBOL_GPL(pm_generic_poweroff);

/**
 * pm_generic_thaw_noirq - Generic thaw_noirq callback for subsystems.
 * @dev: Device to thaw.
 */
int pm_generic_thaw_noirq(struct device *dev)
{
	return __pm_generic_call(dev, PM_EVENT_THAW, true);
}
EXPORT_SYMBOL_GPL(pm_generic_thaw_noirq);

/**
 * pm_generic_thaw - Generic thaw callback for subsystems.
 * @dev: Device to thaw.
 */
int pm_generic_thaw(struct device *dev)
{
	return __pm_generic_call(dev, PM_EVENT_THAW, false);
}
EXPORT_SYMBOL_GPL(pm_generic_thaw);

/**
 * __pm_generic_resume - Generic resume/restore callback for subsystems.
 * @dev: Device to handle.
 * @event: PM transition of the system under way.
 * @bool: Whether or not this is the "noirq" stage.
 *
 * Execute the resume/resotre callback provided by the @dev's driver, if
 * defined.  If it returns 0, change the device's runtime PM status to 'active'.
 * Return the callback's error code.
 */
static int __pm_generic_resume(struct device *dev, int event, bool noirq)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	int (*callback)(struct device *);
	int ret;

	if (!pm)
		return 0;

	switch (event) {
	case PM_EVENT_RESUME:
		callback = noirq ? pm->resume_noirq : pm->resume;
		break;
	case PM_EVENT_RESTORE:
		callback = noirq ? pm->restore_noirq : pm->restore;
		break;
	default:
		callback = NULL;
		break;
	}

	if (!callback)
		return 0;

	ret = callback(dev);
	if (!ret && !noirq && pm_runtime_enabled(dev)) {
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	return ret;
}

/**
 * pm_generic_resume_noirq - Generic resume_noirq callback for subsystems.
 * @dev: Device to resume.
 */
int pm_generic_resume_noirq(struct device *dev)
{
	return __pm_generic_resume(dev, PM_EVENT_RESUME, true);
}
EXPORT_SYMBOL_GPL(pm_generic_resume_noirq);

/**
 * pm_generic_resume - Generic resume callback for subsystems.
 * @dev: Device to resume.
 */
int pm_generic_resume(struct device *dev)
{
	return __pm_generic_resume(dev, PM_EVENT_RESUME, false);
}
EXPORT_SYMBOL_GPL(pm_generic_resume);

/**
 * pm_generic_restore_noirq - Generic restore_noirq callback for subsystems.
 * @dev: Device to restore.
 */
int pm_generic_restore_noirq(struct device *dev)
{
	return __pm_generic_resume(dev, PM_EVENT_RESTORE, true);
}
EXPORT_SYMBOL_GPL(pm_generic_restore_noirq);

/**
 * pm_generic_restore - Generic restore callback for subsystems.
 * @dev: Device to restore.
 */
int pm_generic_restore(struct device *dev)
{
	return __pm_generic_resume(dev, PM_EVENT_RESTORE, false);
}
EXPORT_SYMBOL_GPL(pm_generic_restore);

/**
 * pm_generic_complete - Generic routine competing a device power transition.
 * @dev: Device to handle.
 *
 * Complete a device power transition during a system-wide power transition.
 */
void pm_generic_complete(struct device *dev)
{
	struct device_driver *drv = dev->driver;

	if (drv && drv->pm && drv->pm->complete)
		drv->pm->complete(dev);

	/*
	 * Let runtime PM try to suspend devices that haven't been in use before
	 * going into the system-wide sleep state we're resuming from.
	 */
	pm_runtime_idle(dev);
}
#endif /* CONFIG_PM_SLEEP */

struct dev_pm_ops generic_subsys_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.prepare = pm_generic_prepare,
	.suspend = pm_generic_suspend,
	.suspend_noirq = pm_generic_suspend_noirq,
	.resume = pm_generic_resume,
	.resume_noirq = pm_generic_resume_noirq,
	.freeze = pm_generic_freeze,
	.freeze_noirq = pm_generic_freeze_noirq,
	.thaw = pm_generic_thaw,
	.thaw_noirq = pm_generic_thaw_noirq,
	.poweroff = pm_generic_poweroff,
	.poweroff_noirq = pm_generic_poweroff_noirq,
	.restore = pm_generic_restore,
	.restore_noirq = pm_generic_restore_noirq,
	.complete = pm_generic_complete,
#endif
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend = pm_generic_runtime_suspend,
	.runtime_resume = pm_generic_runtime_resume,
	.runtime_idle = pm_generic_runtime_idle,
#endif
};
EXPORT_SYMBOL_GPL(generic_subsys_pm_ops);
