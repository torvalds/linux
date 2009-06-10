/**
 * @file me1600_device.c
 *
 * @brief ME-1600 device class implementation.
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
#include "mesubdevice.h"
#include "me1600_device.h"

static void me1600_set_registry(me1600_device_t *subdevice, uint32_t reg_base);
static void me1600_destructor(struct me_device *device);

/**
 * @brief Global variable.
 * This is working queue for runing a separate atask that will be responsible for work status (start, stop, timeouts).
 */
static struct workqueue_struct *me1600_workqueue;

me_device_t *me1600_pci_constructor(struct pci_dev *pci_device)
{
	int err;
	me1600_device_t *me1600_device;
	me_subdevice_t *subdevice;
	unsigned int chip_idx;
	int i;

	PDEBUG("executed.\n");

	// Allocate structure for device instance.
	me1600_device = kmalloc(sizeof(me1600_device_t), GFP_KERNEL);

	if (!me1600_device) {
		PERROR("Cannot get memory for device instance.\n");
		return NULL;
	}

	memset(me1600_device, 0, sizeof(me1600_device_t));

	// Initialize base class structure.
	err = me_device_pci_init((me_device_t *) me1600_device, pci_device);

	if (err) {
		kfree(me1600_device);
		PERROR("Cannot initialize device base class.\n");
		return NULL;
	}
	// Initialize spin lock .
	spin_lock_init(&me1600_device->config_regs_lock);
	spin_lock_init(&me1600_device->ao_shadows_lock);

	// Get the number of analog output subdevices.
	chip_idx =
	    me1600_versions_get_device_index(me1600_device->base.info.pci.
					     device_id);

	// Create shadow instance.
	me1600_device->ao_regs_shadows.count =
	    me1600_versions[chip_idx].ao_chips;
	me1600_device->ao_regs_shadows.registry =
	    kmalloc(me1600_versions[chip_idx].ao_chips * sizeof(unsigned long),
		    GFP_KERNEL);
	me1600_set_registry(me1600_device,
			    me1600_device->base.info.pci.reg_bases[2]);
	me1600_device->ao_regs_shadows.shadow =
	    kmalloc(me1600_versions[chip_idx].ao_chips * sizeof(uint16_t),
		    GFP_KERNEL);
	me1600_device->ao_regs_shadows.mirror =
	    kmalloc(me1600_versions[chip_idx].ao_chips * sizeof(uint16_t),
		    GFP_KERNEL);

	// Create subdevice instances.
	for (i = 0; i < me1600_versions[chip_idx].ao_chips; i++) {
		subdevice =
		    (me_subdevice_t *) me1600_ao_constructor(me1600_device->
							     base.info.pci.
							     reg_bases[2], i,
							     ((me1600_versions
							       [chip_idx].curr >
							       i) ? 1 : 0),
							     &me1600_device->
							     config_regs_lock,
							     &me1600_device->
							     ao_shadows_lock,
							     &me1600_device->
							     ao_regs_shadows,
							     me1600_workqueue);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me1600_device);
			kfree(me1600_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me1600_device->base.slist,
					    subdevice);
	}

	// Overwrite base class methods.
	me1600_device->base.me_device_destructor = me1600_destructor;

	return (me_device_t *) me1600_device;
}
EXPORT_SYMBOL(me1600_pci_constructor);

static void me1600_destructor(struct me_device *device)
{
	me1600_device_t *me1600_device = (me1600_device_t *) device;
	PDEBUG("executed.\n");

	// Destroy shadow instance.
	kfree(me1600_device->ao_regs_shadows.registry);
	kfree(me1600_device->ao_regs_shadows.shadow);
	kfree(me1600_device->ao_regs_shadows.mirror);

	me_device_deinit((me_device_t *) me1600_device);
	kfree(me1600_device);
}

static void me1600_set_registry(me1600_device_t *subdevice, uint32_t reg_base)
{				// Create shadow structure.
	if (subdevice->ao_regs_shadows.count >= 1) {
		subdevice->ao_regs_shadows.registry[0] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_0_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 2) {
		subdevice->ao_regs_shadows.registry[1] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_1_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 3) {
		subdevice->ao_regs_shadows.registry[2] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_2_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 4) {
		subdevice->ao_regs_shadows.registry[3] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_3_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 5) {
		subdevice->ao_regs_shadows.registry[4] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_4_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 6) {
		subdevice->ao_regs_shadows.registry[5] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_5_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 7) {
		subdevice->ao_regs_shadows.registry[6] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_6_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 8) {
		subdevice->ao_regs_shadows.registry[7] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_7_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 9) {
		subdevice->ao_regs_shadows.registry[8] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_8_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 10) {
		subdevice->ao_regs_shadows.registry[9] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_9_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 11) {
		subdevice->ao_regs_shadows.registry[10] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_10_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 12) {
		subdevice->ao_regs_shadows.registry[11] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_11_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 13) {
		subdevice->ao_regs_shadows.registry[12] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_12_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 14) {
		subdevice->ao_regs_shadows.registry[13] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_13_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 15) {
		subdevice->ao_regs_shadows.registry[14] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_14_REG);
	}
	if (subdevice->ao_regs_shadows.count >= 16) {
		subdevice->ao_regs_shadows.registry[15] =
		    (unsigned long)(reg_base + ME1600_CHANNEL_15_REG);
	}
	if (subdevice->ao_regs_shadows.count > 16) {
		PERROR("More than 16 outputs! (%d)\n",
		       subdevice->ao_regs_shadows.count);
	}
}

// Init and exit of module.

static int __init me1600_init(void)
{
	PDEBUG("executed\n.");

	me1600_workqueue = create_singlethread_workqueue("me1600");
	return 0;
}

static void __exit me1600_exit(void)
{
	PDEBUG("executed\n.");

	flush_workqueue(me1600_workqueue);
	destroy_workqueue(me1600_workqueue);
}

module_init(me1600_init);
module_exit(me1600_exit);

// Administrative stuff for modinfo.
MODULE_AUTHOR
    ("Guenter Gebhardt <g.gebhardt@meilhaus.de> & Krzysztof Gantzke <k.gantzke@meilhaus.de>");
MODULE_DESCRIPTION("Device Driver Module for ME-1600 Device");
MODULE_SUPPORTED_DEVICE("Meilhaus ME-1600 Devices");
MODULE_LICENSE("GPL");
