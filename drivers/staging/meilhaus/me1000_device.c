/**
 * @file me1000_device.c
 *
 * @brief ME-1000 device class implementation.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 * @author Krzysztof Gantzke	(k.gantzke@meilhaus.de)
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

#include "meids.h"
#include "meerror.h"
#include "mecommon.h"
#include "meinternal.h"

#include "medebug.h"
#include "medevice.h"
#include "me1000_device.h"
#include "mesubdevice.h"
#include "me1000_dio.h"

static int me1000_config_load(me_device_t * me_device, struct file *filep,
			      me_cfg_device_entry_t * config)
{
	me1000_device_t *me1000_device;
	me1000_dio_subdevice_t *dio;

	PDEBUG("executed.\n");

	me1000_device = (me1000_device_t *) me_device;

	if (config->count == 2) {
		if (me_slist_get_number_subdevices(&me1000_device->base.slist)
		    == 2) {
			// Nothing to do.
		} else {
			// Remove 2 extra subdevices
			dio =
			    (me1000_dio_subdevice_t *)
			    me_slist_del_subdevice_tail(&me1000_device->base.
							slist);
			if (dio)
				dio->base.
				    me_subdevice_destructor((me_subdevice_t *)
							    dio);

			dio =
			    (me1000_dio_subdevice_t *)
			    me_slist_del_subdevice_tail(&me1000_device->base.
							slist);
			if (dio)
				dio->base.
				    me_subdevice_destructor((me_subdevice_t *)
							    dio);
		}
	} else if (config->count == 4) {
		//Add 2 subdevices
		if (me_slist_get_number_subdevices(&me1000_device->base.slist)
		    == 2) {
			dio =
			    me1000_dio_constructor(me1000_device->base.info.pci.
						   reg_bases[2], 2,
						   &me1000_device->ctrl_lock);
			if (!dio) {
				PERROR("Cannot create dio subdevice.\n");
				return ME_ERRNO_INTERNAL;
			}
			me_slist_add_subdevice_tail(&me1000_device->base.slist,
						    (me_subdevice_t *) dio);

			dio =
			    me1000_dio_constructor(me1000_device->base.info.pci.
						   reg_bases[2], 3,
						   &me1000_device->ctrl_lock);
			if (!dio) {
				dio =
				    (me1000_dio_subdevice_t *)
				    me_slist_del_subdevice_tail(&me1000_device->
								base.slist);
				if (dio)
					dio->base.
					    me_subdevice_destructor((me_subdevice_t *) dio);

				PERROR("Cannot create dio subdevice.\n");
				return ME_ERRNO_INTERNAL;
			}
			me_slist_add_subdevice_tail(&me1000_device->base.slist,
						    (me_subdevice_t *) dio);
		} else {
			// Nothing to do.
		}
	} else {
		PERROR("Invalid configuration.\n");
		return ME_ERRNO_INTERNAL;
	}

	return ME_ERRNO_SUCCESS;
}

me_device_t *me1000_pci_constructor(struct pci_dev * pci_device)
{
	me1000_device_t *me1000_device;
	me_subdevice_t *subdevice;
	int err;
	int i;

	PDEBUG("executed.\n");

	// Allocate structure for device instance.
	me1000_device = kmalloc(sizeof(me1000_device_t), GFP_KERNEL);

	if (!me1000_device) {
		PERROR("Cannot get memory for ME-1000 device instance.\n");
		return NULL;
	}

	memset(me1000_device, 0, sizeof(me1000_device_t));

	// Initialize base class structure.
	err = me_device_pci_init((me_device_t *) me1000_device, pci_device);

	if (err) {
		kfree(me1000_device);
		PERROR("Cannot initialize device base class.\n");
		return NULL;
	}
	// Initialize spin lock .
	spin_lock_init(&me1000_device->ctrl_lock);

	for (i = 0; i < 4; i++) {
		subdevice =
		    (me_subdevice_t *) me1000_dio_constructor(me1000_device->
							      base.info.pci.
							      reg_bases[2], i,
							      &me1000_device->
							      ctrl_lock);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me1000_device);
			kfree(me1000_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me1000_device->base.slist,
					    subdevice);
	}

	// Overwrite base class methods.
	me1000_device->base.me_device_config_load = me1000_config_load;

	return (me_device_t *) me1000_device;
}

// Init and exit of module.
static int __init me1000_init(void)
{
	PDEBUG("executed.\n");
	return 0;
}

static void __exit me1000_exit(void)
{
	PDEBUG("executed.\n");
}

module_init(me1000_init);
module_exit(me1000_exit);

// Administrative stuff for modinfo.
MODULE_AUTHOR
    ("Guenter Gebhardt <g.gebhardt@meilhaus.de> & Krzysztof Gantzke <k.gantzke@meilhaus.de>");
MODULE_DESCRIPTION("Device Driver Module for Meilhaus ME-1000 Devices");
MODULE_SUPPORTED_DEVICE("Meilhaus ME-1000 Digital I/O Devices");
MODULE_LICENSE("GPL");

// Export the constructor.
EXPORT_SYMBOL(me1000_pci_constructor);
