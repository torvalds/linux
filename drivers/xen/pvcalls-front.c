/*
 * (c) 2017 Stefano Stabellini <stefano@aporeto.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>

#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/interface/io/pvcalls.h>

static const struct xenbus_device_id pvcalls_front_ids[] = {
	{ "pvcalls" },
	{ "" }
};

static int pvcalls_front_remove(struct xenbus_device *dev)
{
	return 0;
}

static int pvcalls_front_probe(struct xenbus_device *dev,
			  const struct xenbus_device_id *id)
{
	return 0;
}

static void pvcalls_front_changed(struct xenbus_device *dev,
			    enum xenbus_state backend_state)
{
}

static struct xenbus_driver pvcalls_front_driver = {
	.ids = pvcalls_front_ids,
	.probe = pvcalls_front_probe,
	.remove = pvcalls_front_remove,
	.otherend_changed = pvcalls_front_changed,
};

static int __init pvcalls_frontend_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	pr_info("Initialising Xen pvcalls frontend driver\n");

	return xenbus_register_frontend(&pvcalls_front_driver);
}

module_init(pvcalls_frontend_init);
