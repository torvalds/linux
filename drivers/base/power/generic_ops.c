/*
 * drivers/base/power/generic_ops.c - Generic PM callbacks for subsystems
 *
 * Copyright (c) 2010 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file is released under the GPLv2.
 */

#include <linux/pm.h>
#include <linux/pm_runtime.h>

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
 * return -EINVAL.
 */
int pm_generic_runtime_suspend(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	int ret;

	ret = pm && pm->runtime_suspend ? pm->runtime_suspend(dev) : -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(pm_generic_runtime_suspend);

/**
 * pm_generic_runtime_resume - Generic runtime resume callback for subsystems.
 * @dev: Device to resume.
 *
 * If PM operations are defined for the @dev's driver and they include
 * ->runtime_resume(), execute it and return its error code.  Otherwise,
 * return -EINVAL.
 */
int pm_generic_runtime_resume(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	int ret;

	ret = pm && pm->runtime_resume ? pm->runtime_resume(dev) : -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(pm_generic_runtime_resume);
#endif /* CONFIG_PM_RUNTIME */

#ifdef CONFIG_PM_SLEEP
/**
 * __pm_generic_call - Generic suspend/freeze/poweroff/thaw subsystem callback.
 * @dev: Device to handle.
 * @event: PM transition of the system under way.
 *
 * If the device has not been suspended at run time, execute the
 * suspend/freeze/poweroff/thaw callback provided by its driver, if defined, and
 * return its error code.  Otherwise, return zero.
 */
static int __pm_generic_call(struct device *dev, int event)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	int (*callback)(struct device *);

	if (!pm || pm_runtime_suspended(dev))
		return 0;

	switch (event) {
	case PM_EVENT_SUSPEND:
		callback = pm->suspend;
		break;
	case PM_EVENT_FREEZE:
		callback = pm->freeze;
		break;
	case PM_EVENT_HIBERNATE:
		callback = pm->poweroff;
		break;
	case PM_EVENT_THAW:
		callback = pm->thaw;
		break;
	default:
		callback = NULL;
		break;
	}

	return callback ? callback(dev) : 0;
}

/**
 * pm_generic_suspend - Generic suspend callback for subsystems.
 * @dev: Device to suspend.
 */
int pm_generic_suspend(struct device *dev)
{
	return __pm_generic_call(dev, PM_EVENT_SUSPEND);
}
EXPORT_SYMBOL_GPL(pm_generic_suspend);

/**
 * pm_generic_freeze - Generic freeze callback for subsystems.
 * @dev: Device to freeze.
 */
int pm_generic_freeze(struct device *dev)
{
	return __pm_generic_call(dev, PM_EVENT_FREEZE);
}
EXPORT_SYMBOL_GPL(pm_generic_freeze);

/**
 * pm_generic_poweroff - Generic poweroff callback for subsystems.
 * @dev: Device to handle.
 */
int pm_generic_poweroff(struct device *dev)
{
	return __pm_generic_call(dev, PM_EVENT_HIBERNATE);
}
EXPORT_SYMBOL_GPL(pm_generic_poweroff);

/**
 * pm_generic_thaw - Generic thaw callback for subsystems.
 * @dev: Device to thaw.
 */
int pm_generic_thaw(struct device *dev)
{
	return __pm_generic_call(dev, PM_EVENT_THAW);
}
EXPORT_SYMBOL_GPL(pm_generic_thaw);

/**
 * __pm_generic_resume - Generic resume/restore callback for subsystems.
 * @dev: Device to handle.
 * @event: PM transition of the system under way.
 *
 * Execute the resume/resotre callback provided by the @dev's driver, if
 * defined.  If it returns 0, change the device's runtime PM status to 'active'.
 * Return the callback's error code.
 */
static int __pm_generic_resume(struct device *dev, int event)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	int (*callback)(struct device *);
	int ret;

	if (!pm)
		return 0;

	switch (event) {
	case PM_EVENT_RESUME:
		callback = pm->resume;
		break;
	case PM_EVENT_RESTORE:
		callback = pm->restore;
		break;
	default:
		callback = NULL;
		break;
	}

	if (!callback)
		return 0;

	ret = callback(dev);
	if (!ret) {
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	return ret;
}

/**
 * pm_generic_resume - Generic resume callback for subsystems.
 * @dev: Device to resume.
 */
int pm_generic_resume(struct device *dev)
{
	return __pm_generic_resume(dev, PM_EVENT_RESUME);
}
EXPORT_SYMBOL_GPL(pm_generic_resume);

/**
 * pm_generic_restore - Generic restore callback for subsystems.
 * @dev: Device to restore.
 */
int pm_generic_restore(struct device *dev)
{
	return __pm_generic_resume(dev, PM_EVENT_RESTORE);
}
EXPORT_SYMBOL_GPL(pm_generic_restore);
#endif /* CONFIG_PM_SLEEP */

struct dev_pm_ops generic_subsys_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.suspend = pm_generic_suspend,
	.resume = pm_generic_resume,
	.freeze = pm_generic_freeze,
	.thaw = pm_generic_thaw,
	.poweroff = pm_generic_poweroff,
	.restore = pm_generic_restore,
#endif
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend = pm_generic_runtime_suspend,
	.runtime_resume = pm_generic_runtime_resume,
	.runtime_idle = pm_generic_runtime_idle,
#endif
};
EXPORT_SYMBOL_GPL(generic_subsys_pm_ops);
