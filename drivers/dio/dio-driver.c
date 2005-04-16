/*
 *  DIO Driver Services
 *
 *  Copyright (C) 2004 Jochen Friedrich
 *
 *  Loosely based on drivers/pci/pci-driver.c and drivers/zorro/zorro-driver.c
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/dio.h>


	/**
	 *  dio_match_device - Tell if a DIO device structure has a matching
	 *                     DIO device id structure
	 *  @ids: array of DIO device id structures to search in
	 *  @dev: the DIO device structure to match against
	 *
	 *  Used by a driver to check whether a DIO device present in the
	 *  system is in its list of supported devices. Returns the matching
	 *  dio_device_id structure or %NULL if there is no match.
	 */

const struct dio_device_id *
dio_match_device(const struct dio_device_id *ids,
		   const struct dio_dev *d)
{
	while (ids->id) {
		if (ids->id == DIO_WILDCARD)
			return ids;
		if (DIO_NEEDSSECID(ids->id & 0xff)) {
			if (ids->id == d->id)
				return ids;
		} else {
			if ((ids->id & 0xff) == (d->id & 0xff))
				return ids;
		}
		ids++;
	}
	return NULL;
}

static int dio_device_probe(struct device *dev)
{
	int error = 0;
	struct dio_driver *drv = to_dio_driver(dev->driver);
	struct dio_dev *d = to_dio_dev(dev);

	if (!d->driver && drv->probe) {
		const struct dio_device_id *id;

		id = dio_match_device(drv->id_table, d);
		if (id)
			error = drv->probe(d, id);
		if (error >= 0) {
			d->driver = drv;
			error = 0;
		}
	}
	return error;
}


	/**
	 *  dio_register_driver - register a new DIO driver
	 *  @drv: the driver structure to register
	 *
	 *  Adds the driver structure to the list of registered drivers
	 *  Returns the number of DIO devices which were claimed by the driver
	 *  during registration.  The driver remains registered even if the
	 *  return value is zero.
	 */

int dio_register_driver(struct dio_driver *drv)
{
	int count = 0;

	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &dio_bus_type;
	drv->driver.probe = dio_device_probe;

	/* register with core */
	count = driver_register(&drv->driver);
	return count ? count : 1;
}


	/**
	 *  dio_unregister_driver - unregister a DIO driver
	 *  @drv: the driver structure to unregister
	 *
	 *  Deletes the driver structure from the list of registered DIO drivers,
	 *  gives it a chance to clean up by calling its remove() function for
	 *  each device it was responsible for, and marks those devices as
	 *  driverless.
	 */

void dio_unregister_driver(struct dio_driver *drv)
{
	driver_unregister(&drv->driver);
}


	/**
	 *  dio_bus_match - Tell if a DIO device structure has a matching DIO
	 *                  device id structure
	 *  @ids: array of DIO device id structures to search in
	 *  @dev: the DIO device structure to match against
	 *
	 *  Used by a driver to check whether a DIO device present in the
	 *  system is in its list of supported devices. Returns the matching
	 *  dio_device_id structure or %NULL if there is no match.
	 */

static int dio_bus_match(struct device *dev, struct device_driver *drv)
{
	struct dio_dev *d = to_dio_dev(dev);
	struct dio_driver *dio_drv = to_dio_driver(drv);
	const struct dio_device_id *ids = dio_drv->id_table;

	if (!ids)
		return 0;

	while (ids->id) {
		if (ids->id == DIO_WILDCARD)
			return 1;
		if (DIO_NEEDSSECID(ids->id & 0xff)) {
			if (ids->id == d->id)
				return 1;
		} else {
			if ((ids->id & 0xff) == (d->id & 0xff))
				return 1;
		}
		ids++;
	}
	return 0;
}


struct bus_type dio_bus_type = {
	.name	= "dio",
	.match	= dio_bus_match
};


static int __init dio_driver_init(void)
{
	return bus_register(&dio_bus_type);
}

postcore_initcall(dio_driver_init);

EXPORT_SYMBOL(dio_match_device);
EXPORT_SYMBOL(dio_register_driver);
EXPORT_SYMBOL(dio_unregister_driver);
EXPORT_SYMBOL(dio_dev_driver);
EXPORT_SYMBOL(dio_bus_type);
