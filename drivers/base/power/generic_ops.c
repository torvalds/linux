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

#ifdef CONFIG_PM
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
#endif /* CONFIG_PM */

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
 * pm_generic_suspend_noirq - Generic suspend_noirq callback for subsystems.
 * @dev: Device to suspend.
 */
int pm_generic_suspend_noirq(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->suspend_noirq ? pm->suspend_noirq(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_suspend_noirq);

/**
 * pm_generic_suspend_late - Generic suspend_late callback for subsystems.
 * @dev: Device to suspend.
 */
int pm_generic_suspend_late(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->suspend_late ? pm->suspend_late(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_suspend_late);

/**
 * pm_generic_suspend - Generic suspend callback for subsystems.
 * @dev: Device to suspend.
 */
int pm_generic_suspend(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->suspend ? pm->suspend(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_suspend);

/**
 * pm_generic_freeze_noirq - Generic freeze_noirq callback for subsystems.
 * @dev: Device to freeze.
 */
int pm_generic_freeze_noirq(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->freeze_noirq ? pm->freeze_noirq(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_freeze_noirq);

/**
 * pm_generic_freeze_late - Generic freeze_late callback for subsystems.
 * @dev: Device to freeze.
 */
int pm_generic_freeze_late(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->freeze_late ? pm->freeze_late(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_freeze_late);

/**
 * pm_generic_freeze - Generic freeze callback for subsystems.
 * @dev: Device to freeze.
 */
int pm_generic_freeze(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->freeze ? pm->freeze(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_freeze);

/**
 * pm_generic_poweroff_noirq - Generic poweroff_noirq callback for subsystems.
 * @dev: Device to handle.
 */
int pm_generic_poweroff_noirq(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->poweroff_noirq ? pm->poweroff_noirq(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_poweroff_noirq);

/**
 * pm_generic_poweroff_late - Generic poweroff_late callback for subsystems.
 * @dev: Device to handle.
 */
int pm_generic_poweroff_late(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->poweroff_late ? pm->poweroff_late(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_poweroff_late);

/**
 * pm_generic_poweroff - Generic poweroff callback for subsystems.
 * @dev: Device to handle.
 */
int pm_generic_poweroff(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->poweroff ? pm->poweroff(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_poweroff);

/**
 * pm_generic_thaw_noirq - Generic thaw_noirq callback for subsystems.
 * @dev: Device to thaw.
 */
int pm_generic_thaw_noirq(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->thaw_noirq ? pm->thaw_noirq(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_thaw_noirq);

/**
 * pm_generic_thaw_early - Generic thaw_early callback for subsystems.
 * @dev: Device to thaw.
 */
int pm_generic_thaw_early(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->thaw_early ? pm->thaw_early(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_thaw_early);

/**
 * pm_generic_thaw - Generic thaw callback for subsystems.
 * @dev: Device to thaw.
 */
int pm_generic_thaw(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->thaw ? pm->thaw(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_thaw);

/**
 * pm_generic_resume_noirq - Generic resume_noirq callback for subsystems.
 * @dev: Device to resume.
 */
int pm_generic_resume_noirq(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->resume_noirq ? pm->resume_noirq(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_resume_noirq);

/**
 * pm_generic_resume_early - Generic resume_early callback for subsystems.
 * @dev: Device to resume.
 */
int pm_generic_resume_early(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->resume_early ? pm->resume_early(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_resume_early);

/**
 * pm_generic_resume - Generic resume callback for subsystems.
 * @dev: Device to resume.
 */
int pm_generic_resume(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->resume ? pm->resume(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_resume);

/**
 * pm_generic_restore_noirq - Generic restore_noirq callback for subsystems.
 * @dev: Device to restore.
 */
int pm_generic_restore_noirq(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->restore_noirq ? pm->restore_noirq(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_restore_noirq);

/**
 * pm_generic_restore_early - Generic restore_early callback for subsystems.
 * @dev: Device to resume.
 */
int pm_generic_restore_early(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->restore_early ? pm->restore_early(dev) : 0;
}
EXPORT_SYMBOL_GPL(pm_generic_restore_early);

/**
 * pm_generic_restore - Generic restore callback for subsystems.
 * @dev: Device to restore.
 */
int pm_generic_restore(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	return pm && pm->restore ? pm->restore(dev) : 0;
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
	pm_request_idle(dev);
}
#endif /* CONFIG_PM_SLEEP */
