/**
 * @file me6000_device.c
 *
 * @brief Device class template implementation.
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

#include "mefirmware.h"

#include "mesubdevice.h"
#include "medebug.h"
#include "medevice.h"
#include "me6000_reg.h"
#include "me6000_device.h"
#include "meplx_reg.h"
#include "me6000_dio.h"
#include "me6000_ao.h"

/**
 * @brief Global variable.
 * This is working queue for runing a separate atask that will be responsible for work status (start, stop, timeouts).
 */
static struct workqueue_struct *me6000_workqueue;

me_device_t *me6000_pci_constructor(struct pci_dev *pci_device)
{
	me6000_device_t *me6000_device;
	me_subdevice_t *subdevice;
	unsigned int version_idx;
	int err;
	int i;
	int high_range = 0;
	int fifo;

	PDEBUG("executed.\n");

	// Allocate structure for device instance.
	me6000_device = kmalloc(sizeof(me6000_device_t), GFP_KERNEL);

	if (!me6000_device) {
		PERROR("Cannot get memory for device instance.\n");
		return NULL;
	}

	memset(me6000_device, 0, sizeof(me6000_device_t));

	// Initialize base class structure.
	err = me_device_pci_init((me_device_t *) me6000_device, pci_device);

	if (err) {
		kfree(me6000_device);
		PERROR("Cannot initialize device base class.\n");
		return NULL;
	}

	/* Download the xilinx firmware */
	err = me_xilinx_download(me6000_device->base.info.pci.reg_bases[1],
				 me6000_device->base.info.pci.reg_bases[2],
				 &pci_device->dev, "me6000.bin");

	if (err) {
		me_device_deinit((me_device_t *) me6000_device);
		kfree(me6000_device);
		PERROR("Can't download firmware.\n");
		return NULL;
	}

	/* Get the index in the device version information table. */
	version_idx =
	    me6000_versions_get_device_index(me6000_device->base.info.pci.
					     device_id);

	// Initialize spin lock .
	spin_lock_init(&me6000_device->preload_reg_lock);
	spin_lock_init(&me6000_device->dio_ctrl_reg_lock);

	/* Create digital input/output instances. */
	for (i = 0; i < me6000_versions[version_idx].dio_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me6000_dio_constructor(me6000_device->
							      base.info.pci.
							      reg_bases[3], i,
							      &me6000_device->
							      dio_ctrl_reg_lock);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me6000_device);
			kfree(me6000_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me6000_device->base.slist,
					    subdevice);
	}

	/* Create analog output instances. */
	for (i = 0; i < me6000_versions[version_idx].ao_subdevices; i++) {
		high_range = ((i == 8)
			      &&
			      ((me6000_device->base.info.pci.device_id ==
				PCI_DEVICE_ID_MEILHAUS_ME6359)
			       || (me6000_device->base.info.pci.device_id ==
				   PCI_DEVICE_ID_MEILHAUS_ME6259)
			      )
		    )? 1 : 0;

		fifo =
		    (i <
		     me6000_versions[version_idx].
		     ao_fifo) ? ME6000_AO_HAS_FIFO : 0x0;
		fifo |= (i < 4) ? ME6000_AO_EXTRA_HARDWARE : 0x0;

		subdevice =
		    (me_subdevice_t *) me6000_ao_constructor(me6000_device->
							     base.info.pci.
							     reg_bases[2],
							     &me6000_device->
							     preload_reg_lock,
							     &me6000_device->
							     preload_flags,
							     &me6000_device->
							     triggering_flags,
							     i, fifo,
							     me6000_device->
							     base.irq,
							     high_range,
							     me6000_workqueue);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me6000_device);
			kfree(me6000_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me6000_device->base.slist,
					    subdevice);
	}

	return (me_device_t *) me6000_device;
}

// Init and exit of module.

static int __init me6000_init(void)
{
	PDEBUG("executed.\n");

	me6000_workqueue = create_singlethread_workqueue("me6000");
	return 0;
}

static void __exit me6000_exit(void)
{
	PDEBUG("executed.\n");

	flush_workqueue(me6000_workqueue);
	destroy_workqueue(me6000_workqueue);
}

module_init(me6000_init);
module_exit(me6000_exit);

// Administrative stuff for modinfo.
MODULE_AUTHOR
    ("Guenter Gebhardt <g.gebhardt@meilhaus.de> & Krzysztof Gantzke <k.gantzke@meilhaus.de>");
MODULE_DESCRIPTION("Device Driver Module for ME-6000 Device");
MODULE_SUPPORTED_DEVICE("Meilhaus ME-6000 Devices");
MODULE_LICENSE("GPL");

// Export the constructor.
EXPORT_SYMBOL(me6000_pci_constructor);
