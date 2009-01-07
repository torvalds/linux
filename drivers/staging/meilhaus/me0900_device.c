/**
 * @file me0900_device.c
 *
 * @brief ME-9x device class implementation.
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
#include "me0900_device.h"
#include "me0900_reg.h"
#include "mesubdevice.h"
#include "me0900_do.h"
#include "me0900_di.h"

me_device_t *me0900_pci_constructor(struct pci_dev *pci_device)
{
	me0900_device_t *me0900_device;
	me_subdevice_t *subdevice;
	unsigned int version_idx;
	int err;
	int i;
	int port_shift;

	PDEBUG("executed.\n");

	// Allocate structure for device instance.
	me0900_device = kmalloc(sizeof(me0900_device_t), GFP_KERNEL);

	if (!me0900_device) {
		PERROR("Cannot get memory for device instance.\n");
		return NULL;
	}

	memset(me0900_device, 0, sizeof(me0900_device_t));

	// Initialize base class structure.
	err = me_device_pci_init((me_device_t *) me0900_device, pci_device);

	if (err) {
		kfree(me0900_device);
		PERROR("Cannot initialize device base class.\n");
		return NULL;
	}

	/* Get the index in the device version information table. */
	version_idx =
	    me0900_versions_get_device_index(me0900_device->base.info.pci.
					     device_id);

	/* Initialize 8255 chip to desired mode */
	if (me0900_device->base.info.pci.device_id ==
	    PCI_DEVICE_ID_MEILHAUS_ME0940) {
		outb(0x9B,
		     me0900_device->base.info.pci.reg_bases[2] +
		     ME0900_CTRL_REG);
	} else if (me0900_device->base.info.pci.device_id ==
		   PCI_DEVICE_ID_MEILHAUS_ME0950) {
		outb(0x89,
		     me0900_device->base.info.pci.reg_bases[2] +
		     ME0900_CTRL_REG);
		outb(0x00,
		     me0900_device->base.info.pci.reg_bases[2] +
		     ME0900_WRITE_ENABLE_REG);
	} else if (me0900_device->base.info.pci.device_id ==
		   PCI_DEVICE_ID_MEILHAUS_ME0960) {
		outb(0x8B,
		     me0900_device->base.info.pci.reg_bases[2] +
		     ME0900_CTRL_REG);
		outb(0x00,
		     me0900_device->base.info.pci.reg_bases[2] +
		     ME0900_WRITE_ENABLE_REG);
	}

	port_shift =
	    (me0900_device->base.info.pci.device_id ==
	     PCI_DEVICE_ID_MEILHAUS_ME0960) ? 1 : 0;
	// Create subdevice instances.

	for (i = 0; i < me0900_versions[version_idx].di_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me0900_di_constructor(me0900_device->
							     base.info.pci.
							     reg_bases[2],
							     i + port_shift);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me0900_device);
			kfree(me0900_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me0900_device->base.slist,
					    subdevice);
	}

	for (i = 0; i < me0900_versions[version_idx].do_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me0900_do_constructor(me0900_device->
							     base.info.pci.
							     reg_bases[2], i);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me0900_device);
			kfree(me0900_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me0900_device->base.slist,
					    subdevice);
	}

	return (me_device_t *) me0900_device;
}

// Init and exit of module.

static int __init me0900_init(void)
{
	PDEBUG("executed.\n.");
	return 0;
}

static void __exit me0900_exit(void)
{
	PDEBUG("executed.\n.");
}

module_init(me0900_init);
module_exit(me0900_exit);

// Administrative stuff for modinfo.
MODULE_AUTHOR
    ("Guenter Gebhardt <g.gebhardt@meilhaus.de> & Krzysztof Gantzke <k.gantzke@meilhaus.de>");
MODULE_DESCRIPTION("Device Driver Module for ME-9x Device");
MODULE_SUPPORTED_DEVICE("Meilhaus ME-9x Devices");
MODULE_LICENSE("GPL");

// Export the constructor.
EXPORT_SYMBOL(me0900_pci_constructor);
