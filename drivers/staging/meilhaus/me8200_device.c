/**
 * @file me8200_device.c
 *
 * @brief ME-8200 device class implementation.
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
#include "meplx_reg.h"
#include "medevice.h"
#include "me8200_device.h"
#include "mesubdevice.h"
#include "me8200_di.h"
#include "me8200_do.h"
#include "me8200_dio.h"

me_device_t *me8200_pci_constructor(struct pci_dev *pci_device)
{
	me8200_device_t *me8200_device;
	me_subdevice_t *subdevice;
	unsigned int version_idx;
	int err;
	int i;

	PDEBUG("executed.\n");

	// Allocate structure for device instance.
	me8200_device = kmalloc(sizeof(me8200_device_t), GFP_KERNEL);

	if (!me8200_device) {
		PERROR("Cannot get memory for device instance.\n");
		return NULL;
	}

	memset(me8200_device, 0, sizeof(me8200_device_t));

	// Initialize base class structure.
	err = me_device_pci_init((me_device_t *) me8200_device, pci_device);

	if (err) {
		kfree(me8200_device);
		PERROR("Cannot initialize device base class.\n");
		return NULL;
	}

	/* Get the index in the device version information table. */
	version_idx =
	    me8200_versions_get_device_index(me8200_device->base.info.pci.
					     device_id);

	// Initialize spin lock .
	spin_lock_init(&me8200_device->irq_ctrl_lock);
	spin_lock_init(&me8200_device->irq_mode_lock);
	spin_lock_init(&me8200_device->dio_ctrl_lock);

	/* Setup the PLX interrupt configuration */
	outl(PLX_INTCSR_LOCAL_INT1_EN |
	     PLX_INTCSR_LOCAL_INT1_POL |
	     PLX_INTCSR_LOCAL_INT2_EN |
	     PLX_INTCSR_LOCAL_INT2_POL |
	     PLX_INTCSR_PCI_INT_EN,
	     me8200_device->base.info.pci.reg_bases[1] + PLX_INTCSR);

	// Create subdevice instances.

	for (i = 0; i < me8200_versions[version_idx].di_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me8200_di_constructor(me8200_device->
							     base.info.pci.
							     reg_bases[2], i,
							     me8200_device->
							     base.irq,
							     &me8200_device->
							     irq_ctrl_lock,
							     &me8200_device->
							     irq_mode_lock);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me8200_device);
			kfree(me8200_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me8200_device->base.slist,
					    subdevice);
	}

	for (i = 0; i < me8200_versions[version_idx].do_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me8200_do_constructor(me8200_device->
							     base.info.pci.
							     reg_bases[2], i,
							     me8200_device->
							     base.irq,
							     &me8200_device->
							     irq_mode_lock);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me8200_device);
			kfree(me8200_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me8200_device->base.slist,
					    subdevice);
	}

	for (i = 0; i < me8200_versions[version_idx].dio_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me8200_dio_constructor(me8200_device->
							      base.info.pci.
							      reg_bases[2], i,
							      &me8200_device->
							      dio_ctrl_lock);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me8200_device);
			kfree(me8200_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me8200_device->base.slist,
					    subdevice);
	}

	return (me_device_t *) me8200_device;
}

// Init and exit of module.

static int __init me8200_init(void)
{
	PDEBUG("executed.\n.");
	return 0;
}

static void __exit me8200_exit(void)
{
	PDEBUG("executed.\n.");
}

module_init(me8200_init);

module_exit(me8200_exit);

// Administrative stuff for modinfo.
MODULE_AUTHOR("Guenter Gebhardt <g.gebhardt@meilhaus.de>");
MODULE_DESCRIPTION("Device Driver Module for Template Device");
MODULE_SUPPORTED_DEVICE("Meilhaus Template Devices");
MODULE_LICENSE("GPL");

// Export the constructor.
EXPORT_SYMBOL(me8200_pci_constructor);
