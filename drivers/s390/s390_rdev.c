/*
 *  drivers/s390/s390_rdev.c
 *  s390 root device
 *   $Revision: 1.4 $
 *
 *    Copyright (C) 2002, 2005 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Cornelia Huck (cornelia.huck@de.ibm.com)
 *		  Carsten Otte  (cotte@de.ibm.com)
 */

#include <linux/slab.h>
#include <linux/err.h>
#include <linux/device.h>
#include <asm/s390_rdev.h>

static void
s390_root_dev_release(struct device *dev)
{
	kfree(dev);
}

struct device *
s390_root_dev_register(const char *name)
{
	struct device *dev;
	int ret;

	if (!strlen(name))
		return ERR_PTR(-EINVAL);
	dev = kmalloc(sizeof(struct device), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);
	memset(dev, 0, sizeof(struct device));
	strncpy(dev->bus_id, name, min(strlen(name), (size_t)BUS_ID_SIZE));
	dev->release = s390_root_dev_release;
	ret = device_register(dev);
	if (ret) {
		kfree(dev);
		return ERR_PTR(ret);
	}
	return dev;
}

void
s390_root_dev_unregister(struct device *dev)
{
	if (dev)
		device_unregister(dev);
}

EXPORT_SYMBOL(s390_root_dev_register);
EXPORT_SYMBOL(s390_root_dev_unregister);
