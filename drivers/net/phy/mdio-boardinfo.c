/*
 * mdio-boardinfo - Collect pre-declarations for MDIO devices
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/list.h>

#include "mdio-boardinfo.h"

static LIST_HEAD(mdio_board_list);
static DEFINE_MUTEX(mdio_board_lock);

/**
 * mdiobus_setup_mdiodev_from_board_info - create and setup MDIO devices
 * from pre-collected board specific MDIO information
 * @mdiodev: MDIO device pointer
 * Context: can sleep
 */
void mdiobus_setup_mdiodev_from_board_info(struct mii_bus *bus)
{
	struct mdio_board_entry *be;
	struct mdio_device *mdiodev;
	struct mdio_board_info *bi;
	int ret;

	mutex_lock(&mdio_board_lock);
	list_for_each_entry(be, &mdio_board_list, list) {
		bi = &be->board_info;

		if (strcmp(bus->id, bi->bus_id))
			continue;

		mdiodev = mdio_device_create(bus, bi->mdio_addr);
		if (IS_ERR(mdiodev))
			continue;

		strncpy(mdiodev->modalias, bi->modalias,
			sizeof(mdiodev->modalias));
		mdiodev->bus_match = mdio_device_bus_match;
		mdiodev->dev.platform_data = (void *)bi->platform_data;

		ret = mdio_device_register(mdiodev);
		if (ret) {
			mdio_device_free(mdiodev);
			continue;
		}
	}
	mutex_unlock(&mdio_board_lock);
}

/**
 * mdio_register_board_info - register MDIO devices for a given board
 * @info: array of devices descriptors
 * @n: number of descriptors provided
 * Context: can sleep
 *
 * The board info passed can be marked with __initdata but be pointers
 * such as platform_data etc. are copied as-is
 */
int mdiobus_register_board_info(const struct mdio_board_info *info,
				unsigned int n)
{
	struct mdio_board_entry *be;
	unsigned int i;

	be = kcalloc(n, sizeof(*be), GFP_KERNEL);
	if (!be)
		return -ENOMEM;

	for (i = 0; i < n; i++, be++, info++) {
		memcpy(&be->board_info, info, sizeof(*info));
		mutex_lock(&mdio_board_lock);
		list_add_tail(&be->list, &mdio_board_list);
		mutex_unlock(&mdio_board_lock);
	}

	return 0;
}
