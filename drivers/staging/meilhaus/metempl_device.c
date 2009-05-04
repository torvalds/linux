/**
 * @file metempl_device.c
 *
 * @brief template device class implementation.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 */

/*
 * Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __KERNEL__
#  define __KERNEL__
#endif

#ifndef MODULE
#  define MODULE
#endif

#include <linux/module.h>

#include <linux/pci.h>
#include <linux/slab.h>

#include <meids.h>
#include "meerror.h"
#include "mecommon.h"
#include "meinternal.h"

#include "medebug.h"
#include "medevice.h"
#include "metempl_device.h"
#include "mesubdevice.h"
#include "metempl_sub.h"

me_device_t *metempl_pci_constructor(struct pci_dev *pci_device)
{
	metempl_device_t *metempl_device;
	me_subdevice_t *subdevice;
	unsigned int version_idx;
	int err;
	int i;

	PDEBUG("executed.\n");

	// Allocate structure for device instance.
	metempl_device = kmalloc(sizeof(metempl_device_t), GFP_KERNEL);

	if (!metempl_device) {
		PERROR("Cannot get memory for device instance.\n");
		return NULL;
	}

	memset(metempl_device, 0, sizeof(metempl_device_t));

	// Initialize base class structure.
	err = me_device_pci_init((me_device_t *) metempl_device, pci_device);

	if (err) {
		kfree(metempl_device);
		PERROR("Cannot initialize device base class.\n");
		return NULL;
	}

	/* Get the index in the device version information table. */
	version_idx =
	    metempl_versions_get_device_index(metempl_device->base.info.pci.
					      device_id);

	// Initialize spin lock .
	spin_lock_init(&metempl_device->ctrl_reg_lock);

	// Create subdevice instances.
	for (i = 0; i < metempl_versions[version_idx].subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) metempl_sub_constructor(metempl_device->
							       base.info.pci.
							       reg_bases[2], i,
							       &metempl_device->
							       ctrl_reg_lock);

		if (!subdevice) {
			me_device_deinit((me_device_t *) metempl_device);
			kfree(metempl_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&metempl_device->base.slist,
					    subdevice);
	}

	/* Overwrite base class methods if applicable. */

	return (me_device_t *) metempl_device;
}
EXPORT_SYMBOL(metempl_pci_constructor);

// Init and exit of module.

static int __init metempl_init(void)
{
	PDEBUG("executed.\n.");
	return 0;
}

static void __exit metempl_exit(void)
{
	PDEBUG("executed.\n.");
}

module_init(metempl_init);

module_exit(metempl_exit);

// Administrative stuff for modinfo.
MODULE_AUTHOR("Guenter Gebhardt <g.gebhardt@meilhaus.de>");
MODULE_DESCRIPTION("Device Driver Module for Template Device");
MODULE_SUPPORTED_DEVICE("Meilhaus Template Devices");
MODULE_LICENSE("GPL");
