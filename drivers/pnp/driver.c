/*
 * driver.c - device id matching, driver model, etc.
 *
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 */

#include <linux/string.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/pnp.h>
#include "base.h"

static int compare_func(const char *ida, const char *idb)
{
	int i;

	/* we only need to compare the last 4 chars */
	for (i = 3; i < 7; i++) {
		if (ida[i] != 'X' &&
		    idb[i] != 'X' && toupper(ida[i]) != toupper(idb[i]))
			return 0;
	}
	return 1;
}

int compare_pnp_id(struct pnp_id *pos, const char *id)
{
	if (!pos || !id || (strlen(id) != 7))
		return 0;
	if (memcmp(id, "ANYDEVS", 7) == 0)
		return 1;
	while (pos) {
		if (memcmp(pos->id, id, 3) == 0)
			if (compare_func(pos->id, id) == 1)
				return 1;
		pos = pos->next;
	}
	return 0;
}

static const struct pnp_device_id *match_device(struct pnp_driver *drv,
						struct pnp_dev *dev)
{
	const struct pnp_device_id *drv_id = drv->id_table;

	if (!drv_id)
		return NULL;

	while (*drv_id->id) {
		if (compare_pnp_id(dev->id, drv_id->id))
			return drv_id;
		drv_id++;
	}
	return NULL;
}

int pnp_device_attach(struct pnp_dev *pnp_dev)
{
	mutex_lock(&pnp_lock);
	if (pnp_dev->status != PNP_READY) {
		mutex_unlock(&pnp_lock);
		return -EBUSY;
	}
	pnp_dev->status = PNP_ATTACHED;
	mutex_unlock(&pnp_lock);
	return 0;
}

void pnp_device_detach(struct pnp_dev *pnp_dev)
{
	mutex_lock(&pnp_lock);
	if (pnp_dev->status == PNP_ATTACHED)
		pnp_dev->status = PNP_READY;
	mutex_unlock(&pnp_lock);
}

static int pnp_device_probe(struct device *dev)
{
	int error;
	struct pnp_driver *pnp_drv;
	struct pnp_dev *pnp_dev;
	const struct pnp_device_id *dev_id = NULL;
	pnp_dev = to_pnp_dev(dev);
	pnp_drv = to_pnp_driver(dev->driver);

	error = pnp_device_attach(pnp_dev);
	if (error < 0)
		return error;

	if (pnp_dev->active == 0) {
		if (!(pnp_drv->flags & PNP_DRIVER_RES_DO_NOT_CHANGE)) {
			error = pnp_activate_dev(pnp_dev);
			if (error < 0)
				return error;
		}
	} else if ((pnp_drv->flags & PNP_DRIVER_RES_DISABLE)
		   == PNP_DRIVER_RES_DISABLE) {
		error = pnp_disable_dev(pnp_dev);
		if (error < 0)
			return error;
	}
	error = 0;
	if (pnp_drv->probe) {
		dev_id = match_device(pnp_drv, pnp_dev);
		if (dev_id != NULL)
			error = pnp_drv->probe(pnp_dev, dev_id);
	}
	if (error >= 0) {
		pnp_dev->driver = pnp_drv;
		error = 0;
	} else
		goto fail;

	return error;

fail:
	pnp_device_detach(pnp_dev);
	return error;
}

static int pnp_device_remove(struct device *dev)
{
	struct pnp_dev *pnp_dev = to_pnp_dev(dev);
	struct pnp_driver *drv = pnp_dev->driver;

	if (drv) {
		if (drv->remove)
			drv->remove(pnp_dev);
		pnp_dev->driver = NULL;
	}

	if (pnp_dev->active &&
	    (!drv || !(drv->flags & PNP_DRIVER_RES_DO_NOT_CHANGE)))
		pnp_disable_dev(pnp_dev);

	pnp_device_detach(pnp_dev);
	return 0;
}

static void pnp_device_shutdown(struct device *dev)
{
	struct pnp_dev *pnp_dev = to_pnp_dev(dev);
	struct pnp_driver *drv = pnp_dev->driver;

	if (drv && drv->shutdown)
		drv->shutdown(pnp_dev);
}

static int pnp_bus_match(struct device *dev, struct device_driver *drv)
{
	struct pnp_dev *pnp_dev = to_pnp_dev(dev);
	struct pnp_driver *pnp_drv = to_pnp_driver(drv);

	if (match_device(pnp_drv, pnp_dev) == NULL)
		return 0;
	return 1;
}

static int __pnp_bus_suspend(struct device *dev, pm_message_t state)
{
	struct pnp_dev *pnp_dev = to_pnp_dev(dev);
	struct pnp_driver *pnp_drv = pnp_dev->driver;
	int error;

	if (!pnp_drv)
		return 0;

	if (pnp_drv->driver.pm && pnp_drv->driver.pm->suspend) {
		error = pnp_drv->driver.pm->suspend(dev);
		suspend_report_result(pnp_drv->driver.pm->suspend, error);
		if (error)
			return error;
	}

	if (pnp_drv->suspend) {
		error = pnp_drv->suspend(pnp_dev, state);
		if (error)
			return error;
	}

	if (pnp_can_disable(pnp_dev)) {
		error = pnp_stop_dev(pnp_dev);
		if (error)
			return error;
	}

	if (pnp_can_suspend(pnp_dev))
		pnp_dev->protocol->suspend(pnp_dev, state);
	return 0;
}

static int pnp_bus_suspend(struct device *dev)
{
	return __pnp_bus_suspend(dev, PMSG_SUSPEND);
}

static int pnp_bus_freeze(struct device *dev)
{
	return __pnp_bus_suspend(dev, PMSG_FREEZE);
}

static int pnp_bus_poweroff(struct device *dev)
{
	return __pnp_bus_suspend(dev, PMSG_HIBERNATE);
}

static int pnp_bus_resume(struct device *dev)
{
	struct pnp_dev *pnp_dev = to_pnp_dev(dev);
	struct pnp_driver *pnp_drv = pnp_dev->driver;
	int error;

	if (!pnp_drv)
		return 0;

	if (pnp_dev->protocol->resume) {
		error = pnp_dev->protocol->resume(pnp_dev);
		if (error)
			return error;
	}

	if (pnp_can_write(pnp_dev)) {
		error = pnp_start_dev(pnp_dev);
		if (error)
			return error;
	}

	if (pnp_drv->driver.pm && pnp_drv->driver.pm->resume) {
		error = pnp_drv->driver.pm->resume(dev);
		if (error)
			return error;
	}

	if (pnp_drv->resume) {
		error = pnp_drv->resume(pnp_dev);
		if (error)
			return error;
	}

	return 0;
}

static const struct dev_pm_ops pnp_bus_dev_pm_ops = {
	/* Suspend callbacks */
	.suspend = pnp_bus_suspend,
	.resume = pnp_bus_resume,
	/* Hibernate callbacks */
	.freeze = pnp_bus_freeze,
	.thaw = pnp_bus_resume,
	.poweroff = pnp_bus_poweroff,
	.restore = pnp_bus_resume,
};

struct bus_type pnp_bus_type = {
	.name    = "pnp",
	.match   = pnp_bus_match,
	.probe   = pnp_device_probe,
	.remove  = pnp_device_remove,
	.shutdown = pnp_device_shutdown,
	.pm	 = &pnp_bus_dev_pm_ops,
	.dev_groups = pnp_dev_groups,
};

int pnp_register_driver(struct pnp_driver *drv)
{
	drv->driver.name = drv->name;
	drv->driver.bus = &pnp_bus_type;

	return driver_register(&drv->driver);
}

void pnp_unregister_driver(struct pnp_driver *drv)
{
	driver_unregister(&drv->driver);
}

/**
 * pnp_add_id - adds an EISA id to the specified device
 * @dev: pointer to the desired device
 * @id: pointer to an EISA id string
 */
struct pnp_id *pnp_add_id(struct pnp_dev *dev, const char *id)
{
	struct pnp_id *dev_id, *ptr;

	dev_id = kzalloc(sizeof(struct pnp_id), GFP_KERNEL);
	if (!dev_id)
		return NULL;

	dev_id->id[0] = id[0];
	dev_id->id[1] = id[1];
	dev_id->id[2] = id[2];
	dev_id->id[3] = tolower(id[3]);
	dev_id->id[4] = tolower(id[4]);
	dev_id->id[5] = tolower(id[5]);
	dev_id->id[6] = tolower(id[6]);
	dev_id->id[7] = '\0';

	dev_id->next = NULL;
	ptr = dev->id;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = dev_id;
	else
		dev->id = dev_id;

	return dev_id;
}

EXPORT_SYMBOL(pnp_register_driver);
EXPORT_SYMBOL(pnp_unregister_driver);
EXPORT_SYMBOL(pnp_device_attach);
EXPORT_SYMBOL(pnp_device_detach);
