/*********************************************************************
 *
 *	sir_dongle.c:	manager for serial dongle protocol drivers
 *
 *	Copyright (c) 2002 Martin Diehl
 *
 *	This program is free software; you can redistribute it and/or 
 *	modify it under the terms of the GNU General Public License as 
 *	published by the Free Software Foundation; either version 2 of 
 *	the License, or (at your option) any later version.
 *
 ********************************************************************/    

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/kmod.h>

#include <net/irda/irda.h>

#include "sir-dev.h"

/**************************************************************************
 *
 * dongle registration and attachment
 *
 */

static LIST_HEAD(dongle_list);			/* list of registered dongle drivers */
static DECLARE_MUTEX(dongle_list_lock);		/* protects the list */

int irda_register_dongle(struct dongle_driver *new)
{
	struct list_head *entry;
	struct dongle_driver *drv;

	IRDA_DEBUG(0, "%s : registering dongle \"%s\" (%d).\n",
		   __FUNCTION__, new->driver_name, new->type);

	down(&dongle_list_lock);
	list_for_each(entry, &dongle_list) {
		drv = list_entry(entry, struct dongle_driver, dongle_list);
		if (new->type == drv->type) {
			up(&dongle_list_lock);
			return -EEXIST;
		}
	}
	list_add(&new->dongle_list, &dongle_list);
	up(&dongle_list_lock);
	return 0;
}
EXPORT_SYMBOL(irda_register_dongle);

int irda_unregister_dongle(struct dongle_driver *drv)
{
	down(&dongle_list_lock);
	list_del(&drv->dongle_list);
	up(&dongle_list_lock);
	return 0;
}
EXPORT_SYMBOL(irda_unregister_dongle);

int sirdev_get_dongle(struct sir_dev *dev, IRDA_DONGLE type)
{
	struct list_head *entry;
	const struct dongle_driver *drv = NULL;
	int err = -EINVAL;

#ifdef CONFIG_KMOD
	request_module("irda-dongle-%d", type);
#endif

	if (dev->dongle_drv != NULL)
		return -EBUSY;
	
	/* serialize access to the list of registered dongles */
	down(&dongle_list_lock);

	list_for_each(entry, &dongle_list) {
		drv = list_entry(entry, struct dongle_driver, dongle_list);
		if (drv->type == type)
			break;
		else
			drv = NULL;
	}

	if (!drv) {
		err = -ENODEV;
		goto out_unlock;	/* no such dongle */
	}

	/* handling of SMP races with dongle module removal - three cases:
	 * 1) dongle driver was already unregistered - then we haven't found the
	 *	requested dongle above and are already out here
	 * 2) the module is already marked deleted but the driver is still
	 *	registered - then the try_module_get() below will fail
	 * 3) the try_module_get() below succeeds before the module is marked
	 *	deleted - then sys_delete_module() fails and prevents the removal
	 *	because the module is in use.
	 */

	if (!try_module_get(drv->owner)) {
		err = -ESTALE;
		goto out_unlock;	/* rmmod already pending */
	}
	dev->dongle_drv = drv;

	if (!drv->open  ||  (err=drv->open(dev))!=0)
		goto out_reject;		/* failed to open driver */

	up(&dongle_list_lock);
	return 0;

out_reject:
	dev->dongle_drv = NULL;
	module_put(drv->owner);
out_unlock:
	up(&dongle_list_lock);
	return err;
}

int sirdev_put_dongle(struct sir_dev *dev)
{
	const struct dongle_driver *drv = dev->dongle_drv;

	if (drv) {
		if (drv->close)
			drv->close(dev);		/* close this dongle instance */

		dev->dongle_drv = NULL;			/* unlink the dongle driver */
		module_put(drv->owner);/* decrement driver's module refcount */
	}

	return 0;
}
