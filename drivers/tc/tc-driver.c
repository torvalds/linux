/*
 *	TURBOchannel driver services.
 *
 *	Copyright (c) 2005  James Simmons
 *	Copyright (c) 2006  Maciej W. Rozycki
 *
 *	Loosely based on drivers/dio/dio-driver.c and
 *	drivers/pci/pci-driver.c.
 *
 *	This file is subject to the terms and conditions of the GNU
 *	General Public License.  See the file "COPYING" in the main
 *	directory of this archive for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/tc.h>

/**
 * tc_register_driver - register a new TC driver
 * @drv: the driver structure to register
 *
 * Adds the driver structure to the list of registered drivers
 * Returns a negative value on error, otherwise 0.
 * If no error occurred, the driver remains registered even if
 * no device was claimed during registration.
 */
int tc_register_driver(struct tc_driver *tdrv)
{
	return driver_register(&tdrv->driver);
}
EXPORT_SYMBOL(tc_register_driver);

/**
 * tc_unregister_driver - unregister a TC driver
 * @drv: the driver structure to unregister
 *
 * Deletes the driver structure from the list of registered TC drivers,
 * gives it a chance to clean up by calling its remove() function for
 * each device it was responsible for, and marks those devices as
 * driverless.
 */
void tc_unregister_driver(struct tc_driver *tdrv)
{
	driver_unregister(&tdrv->driver);
}
EXPORT_SYMBOL(tc_unregister_driver);

/**
 * tc_match_device - tell if a TC device structure has a matching
 *                   TC device ID structure
 * @tdrv: the TC driver to earch for matching TC device ID strings
 * @tdev: the TC device structure to match against
 *
 * Used by a driver to check whether a TC device present in the
 * system is in its list of supported devices.  Returns the matching
 * tc_device_id structure or %NULL if there is no match.
 */
static const struct tc_device_id *tc_match_device(struct tc_driver *tdrv,
						  struct tc_dev *tdev)
{
	const struct tc_device_id *id = tdrv->id_table;

	if (id) {
		while (id->name[0] || id->vendor[0]) {
			if (strcmp(tdev->name, id->name) == 0 &&
			    strcmp(tdev->vendor, id->vendor) == 0)
				return id;
			id++;
		}
	}
	return NULL;
}

/**
 * tc_bus_match - Tell if a device structure has a matching
 *                TC device ID structure
 * @dev: the device structure to match against
 * @drv: the device driver to search for matching TC device ID strings
 *
 * Used by a driver to check whether a TC device present in the
 * system is in its list of supported devices.  Returns 1 if there
 * is a match or 0 otherwise.
 */
static int tc_bus_match(struct device *dev, struct device_driver *drv)
{
	struct tc_dev *tdev = to_tc_dev(dev);
	struct tc_driver *tdrv = to_tc_driver(drv);
	const struct tc_device_id *id;

	id = tc_match_device(tdrv, tdev);
	if (id)
		return 1;

	return 0;
}

const struct bus_type tc_bus_type = {
	.name	= "tc",
	.match	= tc_bus_match,
};
EXPORT_SYMBOL(tc_bus_type);

static int __init tc_driver_init(void)
{
	return bus_register(&tc_bus_type);
}

postcore_initcall(tc_driver_init);
