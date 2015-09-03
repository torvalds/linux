/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 */
#include "scif_main.h"
#include "../bus/scif_bus.h"
#include "scif_peer_bus.h"

static inline struct scif_peer_dev *
dev_to_scif_peer(struct device *dev)
{
	return container_of(dev, struct scif_peer_dev, dev);
}

static inline struct scif_peer_driver *
drv_to_scif_peer(struct device_driver *drv)
{
	return container_of(drv, struct scif_peer_driver, driver);
}

static int scif_peer_dev_match(struct device *dv, struct device_driver *dr)
{
	return !strncmp(dev_name(dv), dr->name, 4);
}

static int scif_peer_dev_probe(struct device *d)
{
	struct scif_peer_dev *dev = dev_to_scif_peer(d);
	struct scif_peer_driver *drv = drv_to_scif_peer(dev->dev.driver);

	return drv->probe(dev);
}

static int scif_peer_dev_remove(struct device *d)
{
	struct scif_peer_dev *dev = dev_to_scif_peer(d);
	struct scif_peer_driver *drv = drv_to_scif_peer(dev->dev.driver);

	drv->remove(dev);
	return 0;
}

static struct bus_type scif_peer_bus = {
	.name  = "scif_peer_bus",
	.match = scif_peer_dev_match,
	.probe = scif_peer_dev_probe,
	.remove = scif_peer_dev_remove,
};

int scif_peer_register_driver(struct scif_peer_driver *driver)
{
	driver->driver.bus = &scif_peer_bus;
	return driver_register(&driver->driver);
}

void scif_peer_unregister_driver(struct scif_peer_driver *driver)
{
	driver_unregister(&driver->driver);
}

static void scif_peer_release_dev(struct device *d)
{
	struct scif_peer_dev *sdev = dev_to_scif_peer(d);
	struct scif_dev *scifdev = &scif_dev[sdev->dnode];

	scif_cleanup_scifdev(scifdev);
	kfree(sdev);
}

struct scif_peer_dev *
scif_peer_register_device(struct scif_dev *scifdev)
{
	int ret;
	struct scif_peer_dev *spdev;

	spdev = kzalloc(sizeof(*spdev), GFP_KERNEL);
	if (!spdev)
		return ERR_PTR(-ENOMEM);

	spdev->dev.parent = scifdev->sdev->dev.parent;
	spdev->dev.release = scif_peer_release_dev;
	spdev->dnode = scifdev->node;
	spdev->dev.bus = &scif_peer_bus;

	dev_set_name(&spdev->dev, "scif_peer-dev%u", spdev->dnode);
	/*
	 * device_register() causes the bus infrastructure to look for a
	 * matching driver.
	 */
	ret = device_register(&spdev->dev);
	if (ret)
		goto free_spdev;
	return spdev;
free_spdev:
	kfree(spdev);
	return ERR_PTR(ret);
}

void scif_peer_unregister_device(struct scif_peer_dev *sdev)
{
	device_unregister(&sdev->dev);
}

int scif_peer_bus_init(void)
{
	return bus_register(&scif_peer_bus);
}

void scif_peer_bus_exit(void)
{
	bus_unregister(&scif_peer_bus);
}
