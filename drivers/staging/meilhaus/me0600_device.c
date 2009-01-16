/**
 * @file me0600_device.c
 *
 * @brief ME-630 device class implementation.
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
#include "me0600_device.h"
#include "mesubdevice.h"
#include "me0600_relay.h"
#include "me0600_ttli.h"
#include "me0600_optoi.h"
#include "me0600_dio.h"
#include "me0600_ext_irq.h"

me_device_t *me0600_pci_constructor(struct pci_dev *pci_device)
{
	me0600_device_t *me0600_device;
	me_subdevice_t *subdevice;
	unsigned int version_idx;
	int err;
	int i;

	PDEBUG("executed.\n");

	// Allocate structure for device instance.
	me0600_device = kmalloc(sizeof(me0600_device_t), GFP_KERNEL);

	if (!me0600_device) {
		PERROR("Cannot get memory for device instance.\n");
		return NULL;
	}

	memset(me0600_device, 0, sizeof(me0600_device_t));

	// Initialize base class structure.
	err = me_device_pci_init((me_device_t *) me0600_device, pci_device);

	if (err) {
		kfree(me0600_device);
		PERROR("Cannot initialize device base class.\n");
		return NULL;
	}

	/* Get the index in the device version information table. */
	version_idx =
	    me0600_versions_get_device_index(me0600_device->base.info.pci.
					     device_id);

	// Initialize spin lock .
	spin_lock_init(&me0600_device->dio_ctrl_reg_lock);
	spin_lock_init(&me0600_device->intcsr_lock);

	// Create subdevice instances.

	for (i = 0; i < me0600_versions[version_idx].optoi_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me0600_optoi_constructor(me0600_device->
								base.info.pci.
								reg_bases[2]);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me0600_device);
			kfree(me0600_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me0600_device->base.slist,
					    subdevice);
	}

	for (i = 0; i < me0600_versions[version_idx].relay_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me0600_relay_constructor(me0600_device->
								base.info.pci.
								reg_bases[2]);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me0600_device);
			kfree(me0600_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me0600_device->base.slist,
					    subdevice);
	}

	for (i = 0; i < me0600_versions[version_idx].ttli_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me0600_ttli_constructor(me0600_device->
							       base.info.pci.
							       reg_bases[2]);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me0600_device);
			kfree(me0600_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me0600_device->base.slist,
					    subdevice);
	}

	for (i = 0; i < me0600_versions[version_idx].dio_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me0600_dio_constructor(me0600_device->
							      base.info.pci.
							      reg_bases[2], i,
							      &me0600_device->
							      dio_ctrl_reg_lock);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me0600_device);
			kfree(me0600_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me0600_device->base.slist,
					    subdevice);
	}

	for (i = 0; i < me0600_versions[version_idx].ext_irq_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *)
		    me0600_ext_irq_constructor(me0600_device->base.info.pci.
					       reg_bases[1],
					       me0600_device->base.info.pci.
					       reg_bases[2],
					       &me0600_device->intcsr_lock, i,
					       me0600_device->base.irq);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me0600_device);
			kfree(me0600_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me0600_device->base.slist,
					    subdevice);
	}

	return (me_device_t *) me0600_device;
}

// Init and exit of module.

static int __init me0600_init(void)
{
	PDEBUG("executed.\n");
	return 0;
}

static void __exit me0600_exit(void)
{
	PDEBUG("executed.\n");
}

module_init(me0600_init);

module_exit(me0600_exit);

// Administrative stuff for modinfo.
MODULE_AUTHOR
    ("Guenter Gebhardt <g.gebhardt@meilhaus.de> & Krzysztof Gantzke <k.gantzke@meilhaus.de>");
MODULE_DESCRIPTION("Device Driver Module for ME-6xx Device");
MODULE_SUPPORTED_DEVICE("Meilhaus ME-6xx Devices");
MODULE_LICENSE("GPL");

// Export the constructor.
EXPORT_SYMBOL(me0600_pci_constructor);
