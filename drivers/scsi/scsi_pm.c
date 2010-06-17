/*
 *	scsi_pm.c	Copyright (C) 2010 Alan Stern
 *
 *	SCSI dynamic Power Management
 *		Initial version: Alan Stern <stern@rowland.harvard.edu>
 */

#include <linux/pm_runtime.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_host.h>

#include "scsi_priv.h"

static int scsi_dev_type_suspend(struct device *dev, pm_message_t msg)
{
	struct device_driver *drv;
	int err;

	err = scsi_device_quiesce(to_scsi_device(dev));
	if (err == 0) {
		drv = dev->driver;
		if (drv && drv->suspend)
			err = drv->suspend(dev, msg);
	}
	dev_dbg(dev, "scsi suspend: %d\n", err);
	return err;
}

static int scsi_dev_type_resume(struct device *dev)
{
	struct device_driver *drv;
	int err = 0;

	drv = dev->driver;
	if (drv && drv->resume)
		err = drv->resume(dev);
	scsi_device_resume(to_scsi_device(dev));
	dev_dbg(dev, "scsi resume: %d\n", err);
	return err;
}

#ifdef CONFIG_PM_SLEEP

static int scsi_bus_suspend_common(struct device *dev, pm_message_t msg)
{
	int err = 0;

	if (scsi_is_sdev_device(dev))
		err = scsi_dev_type_suspend(dev, msg);
	return err;
}

static int scsi_bus_resume_common(struct device *dev)
{
	int err = 0;

	if (scsi_is_sdev_device(dev))
		err = scsi_dev_type_resume(dev);
	return err;
}

static int scsi_bus_suspend(struct device *dev)
{
	return scsi_bus_suspend_common(dev, PMSG_SUSPEND);
}

static int scsi_bus_freeze(struct device *dev)
{
	return scsi_bus_suspend_common(dev, PMSG_FREEZE);
}

static int scsi_bus_poweroff(struct device *dev)
{
	return scsi_bus_suspend_common(dev, PMSG_HIBERNATE);
}

#else /* CONFIG_PM_SLEEP */

#define scsi_bus_resume_common		NULL
#define scsi_bus_suspend		NULL
#define scsi_bus_freeze			NULL
#define scsi_bus_poweroff		NULL

#endif /* CONFIG_PM_SLEEP */

const struct dev_pm_ops scsi_bus_pm_ops = {
	.suspend =		scsi_bus_suspend,
	.resume =		scsi_bus_resume_common,
	.freeze =		scsi_bus_freeze,
	.thaw =			scsi_bus_resume_common,
	.poweroff =		scsi_bus_poweroff,
	.restore =		scsi_bus_resume_common,
};
