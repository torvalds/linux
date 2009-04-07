/**
 * @file me4600_device.c
 *
 * @brief ME-4600 device class implementation.
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
#include "me4600_device.h"
#include "meplx_reg.h"

#include "mefirmware.h"

#include "mesubdevice.h"
#include "me4600_do.h"
#include "me4600_di.h"
#include "me4600_dio.h"
#include "me8254.h"
#include "me4600_ai.h"
#include "me4600_ao.h"
#include "me4600_ext_irq.h"

/**
 * @brief Global variable.
 * This is working queue for runing a separate atask that will be responsible for work status (start, stop, timeouts).
 */
static struct workqueue_struct *me4600_workqueue;

#ifdef BOSCH
me_device_t *me4600_pci_constructor(struct pci_dev *pci_device, int me_bosch_fw)
#else //~BOSCH
me_device_t *me4600_pci_constructor(struct pci_dev *pci_device)
#endif				//BOSCH
{
	me4600_device_t *me4600_device;
	me_subdevice_t *subdevice;
	unsigned int version_idx;
	int err;
	int i;

	PDEBUG("executed.\n");

	// Allocate structure for device instance.
	me4600_device = kmalloc(sizeof(me4600_device_t), GFP_KERNEL);

	if (!me4600_device) {
		PERROR("Cannot get memory for ME-4600 device instance.\n");
		return NULL;
	}

	memset(me4600_device, 0, sizeof(me4600_device_t));

	// Initialize base class structure.
	err = me_device_pci_init((me_device_t *) me4600_device, pci_device);

	if (err) {
		kfree(me4600_device);
		PERROR("Cannot initialize device base class.\n");
		return NULL;
	}
	// Download the xilinx firmware.
	if (me4600_device->base.info.pci.device_id == PCI_DEVICE_ID_MEILHAUS_ME4610) {	//Jekyll <=> me4610
		err =
		    me_xilinx_download(me4600_device->base.info.pci.
				       reg_bases[1],
				       me4600_device->base.info.pci.
				       reg_bases[5], &pci_device->dev,
				       "me4610.bin");
	} else {		// General me4600 firmware
#ifdef BOSCH
		err =
		    me_xilinx_download(me4600_device->base.info.pci.
				       reg_bases[1],
				       me4600_device->base.info.pci.
				       reg_bases[5], &pci_device->dev,
				       (me_bosch_fw) ? "me4600_bosch.bin" :
				       "me4600.bin");
#else //~BOSCH
		err =
		    me_xilinx_download(me4600_device->base.info.pci.
				       reg_bases[1],
				       me4600_device->base.info.pci.
				       reg_bases[5], &pci_device->dev,
				       "me4600.bin");
#endif
	}

	if (err) {
		me_device_deinit((me_device_t *) me4600_device);
		kfree(me4600_device);
		PERROR("Cannot download firmware.\n");
		return NULL;
	}
	// Get the index in the device version information table.
	version_idx =
	    me4600_versions_get_device_index(me4600_device->base.info.pci.
					     device_id);

	// Initialize spin locks.
	spin_lock_init(&me4600_device->preload_reg_lock);

	me4600_device->preload_flags = 0;

	spin_lock_init(&me4600_device->dio_lock);
	spin_lock_init(&me4600_device->ai_ctrl_lock);
	spin_lock_init(&me4600_device->ctr_ctrl_reg_lock);
	spin_lock_init(&me4600_device->ctr_clk_src_reg_lock);

	// Create digital input instances.
	for (i = 0; i < me4600_versions[version_idx].di_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me4600_di_constructor(me4600_device->
							     base.info.pci.
							     reg_bases[2],
							     &me4600_device->
							     dio_lock);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me4600_device);
			kfree(me4600_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me4600_device->base.slist,
					    subdevice);
	}

	// Create digital output instances.
	for (i = 0; i < me4600_versions[version_idx].do_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me4600_do_constructor(me4600_device->
							     base.info.pci.
							     reg_bases[2],
							     &me4600_device->
							     dio_lock);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me4600_device);
			kfree(me4600_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me4600_device->base.slist,
					    subdevice);
	}

	// Create digital input/output instances.
	for (i = 0; i < me4600_versions[version_idx].dio_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me4600_dio_constructor(me4600_device->
							      base.info.pci.
							      reg_bases[2],
							      me4600_versions
							      [version_idx].
							      do_subdevices +
							      me4600_versions
							      [version_idx].
							      di_subdevices + i,
							      &me4600_device->
							      dio_lock);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me4600_device);
			kfree(me4600_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me4600_device->base.slist,
					    subdevice);
	}

	// Create analog input instances.
	for (i = 0; i < me4600_versions[version_idx].ai_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me4600_ai_constructor(me4600_device->
							     base.info.pci.
							     reg_bases[2],
							     me4600_versions
							     [version_idx].
							     ai_channels,
							     me4600_versions
							     [version_idx].
							     ai_ranges,
							     me4600_versions
							     [version_idx].
							     ai_isolated,
							     me4600_versions
							     [version_idx].
							     ai_sh,
							     me4600_device->
							     base.irq,
							     &me4600_device->
							     ai_ctrl_lock,
							     me4600_workqueue);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me4600_device);
			kfree(me4600_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me4600_device->base.slist,
					    subdevice);
	}

	// Create analog output instances.
	for (i = 0; i < me4600_versions[version_idx].ao_subdevices; i++) {
#ifdef BOSCH
		subdevice =
		    (me_subdevice_t *) me4600_ao_constructor(me4600_device->
							     base.info.pci.
							     reg_bases[2],
							     &me4600_device->
							     preload_reg_lock,
							     &me4600_device->
							     preload_flags, i,
							     me4600_versions
							     [version_idx].
							     ao_fifo,
							     me4600_device->
							     base.irq);
#else //~BOSCH
		subdevice =
		    (me_subdevice_t *) me4600_ao_constructor(me4600_device->
							     base.info.pci.
							     reg_bases[2],
							     &me4600_device->
							     preload_reg_lock,
							     &me4600_device->
							     preload_flags, i,
							     me4600_versions
							     [version_idx].
							     ao_fifo,
							     me4600_device->
							     base.irq,
							     me4600_workqueue);
#endif

		if (!subdevice) {
			me_device_deinit((me_device_t *) me4600_device);
			kfree(me4600_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me4600_device->base.slist,
					    subdevice);
	}

	// Create counter instances.
	for (i = 0; i < me4600_versions[version_idx].ctr_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *) me8254_constructor(me4600_device->base.
							  info.pci.device_id,
							  me4600_device->base.
							  info.pci.reg_bases[3],
							  0, i,
							  &me4600_device->
							  ctr_ctrl_reg_lock,
							  &me4600_device->
							  ctr_clk_src_reg_lock);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me4600_device);
			kfree(me4600_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me4600_device->base.slist,
					    subdevice);
	}

	// Create external interrupt instances.
	for (i = 0; i < me4600_versions[version_idx].ext_irq_subdevices; i++) {
		subdevice =
		    (me_subdevice_t *)
		    me4600_ext_irq_constructor(me4600_device->base.info.pci.
					       reg_bases[2],
					       me4600_device->base.irq,
					       &me4600_device->ai_ctrl_lock);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me4600_device);
			kfree(me4600_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me4600_device->base.slist,
					    subdevice);
	}

	return (me_device_t *) me4600_device;
}
EXPORT_SYMBOL(me4600_pci_constructor);

// Init and exit of module.

static int __init me4600_init(void)
{
	PDEBUG("executed.\n");

#ifndef BOSCH
	me4600_workqueue = create_singlethread_workqueue("me4600");
#endif
	return 0;
}

static void __exit me4600_exit(void)
{
	PDEBUG("executed.\n");

#ifndef BOSCH
	flush_workqueue(me4600_workqueue);
	destroy_workqueue(me4600_workqueue);
#endif
}

module_init(me4600_init);
module_exit(me4600_exit);

// Administrative stuff for modinfo.
MODULE_AUTHOR
    ("Guenter Gebhardt <g.gebhardt@meilhaus.de> & Krzysztof Gantzke <k.gantzke@meilhaus.de>");
MODULE_DESCRIPTION("Device Driver Module for ME-46xx Devices");
MODULE_SUPPORTED_DEVICE("Meilhaus ME-46xx Devices");
MODULE_LICENSE("GPL");
