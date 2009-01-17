/**
 * @file me1400_device.c
 *
 * @brief ME-1400 device instance.
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

/*
 * User application could also include the kernel header files. But the
 * real kernel functions are protected by #ifdef __KERNEL__.
 */
#ifndef __KERNEL__
#  define __KERNEL__
#endif

/*
 * This must be defined before module.h is included. Not needed, when
 * it is a built in driver.
 */
#ifndef MODULE
#  define MODULE
#endif

#include <linux/module.h>

#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/version.h>

#include "meids.h"
#include "meerror.h"
#include "mecommon.h"
#include "meinternal.h"

#include "medebug.h"

#include "me1400_device.h"
#include "me8254.h"
#include "me8254_reg.h"
#include "me8255.h"
#include "me1400_ext_irq.h"

me_device_t *me1400_pci_constructor(struct pci_dev *pci_device)
{
	int err;
	me1400_device_t *me1400_device;
	me_subdevice_t *subdevice;
	unsigned int version_idx;
	unsigned int me8255_idx;
	unsigned int dio_idx;
	unsigned int me8254_idx;
	unsigned int ctr_idx;
	unsigned int ext_irq_idx;

	PDEBUG("executed.\n");

	// Allocate structure for device instance.
	me1400_device = kmalloc(sizeof(me1400_device_t), GFP_KERNEL);

	if (!me1400_device) {
		PERROR("Cannot get memory for 1400ate device instance.\n");
		return NULL;
	}

	memset(me1400_device, 0, sizeof(me1400_device_t));

	// Initialize base class structure.
	err = me_device_pci_init((me_device_t *) me1400_device, pci_device);

	if (err) {
		kfree(me1400_device);
		PERROR("Cannot initialize device base class.\n");
		return NULL;
	}

	/* Check for ME1400 extension device. If detected we fake a ME-1400 D device id. */
	if (me1400_device->base.info.pci.device_id ==
	    PCI_DEVICE_ID_MEILHAUS_ME140C) {
		uint8_t ctrl;
		ctrl =
		    inb(me1400_device->base.info.pci.reg_bases[2] +
			ME1400D_CLK_SRC_2_REG);
		PDEBUG_REG("xxx_reg inb(0x%X+0x%X)=0x%x\n",
			   me1400_device->base.info.pci.reg_bases[2],
			   ME1400D_CLK_SRC_2_REG, ctrl);
		outb(ctrl | 0xF0,
		     me1400_device->base.info.pci.reg_bases[2] +
		     ME1400D_CLK_SRC_2_REG);
		PDEBUG_REG("xxx_reg outb(0x%X+0x%X)=0x%x\n",
			   me1400_device->base.info.pci.reg_bases[2],
			   ME1400D_CLK_SRC_2_REG, ctrl | 0xF0);
		ctrl =
		    inb(me1400_device->base.info.pci.reg_bases[2] +
			ME1400D_CLK_SRC_2_REG);
		PDEBUG_REG("xxx_reg inb(0x%X+0x%X)=0x%x\n",
			   me1400_device->base.info.pci.reg_bases[2],
			   ME1400D_CLK_SRC_2_REG, ctrl);

		if ((ctrl & 0xF0) == 0xF0) {
			PINFO("ME1400 D detected.\n");
			me1400_device->base.info.pci.device_id =
			    PCI_DEVICE_ID_MEILHAUS_ME140D;
		}
	}

	/* Initialize global stuff of digital i/o subdevices. */
	for (me8255_idx = 0; me8255_idx < ME1400_MAX_8255; me8255_idx++) {
		me1400_device->dio_current_mode[me8255_idx] = 0;
		spin_lock_init(&me1400_device->dio_ctrl_reg_lock[me8255_idx]);
	}

	/* Initialize global stuff of counter subdevices. */
	spin_lock_init(&me1400_device->clk_src_reg_lock);

	for (me8254_idx = 0; me8254_idx < ME1400_MAX_8254; me8254_idx++)
		spin_lock_init(&me1400_device->ctr_ctrl_reg_lock[me8254_idx]);

	/* Get the index in the device version information table. */
	version_idx =
	    me1400_versions_get_device_index(me1400_device->base.info.pci.
					     device_id);

	/* Generate DIO subdevice instances. */
	for (me8255_idx = 0;
	     me8255_idx < me1400_versions[version_idx].dio_chips;
	     me8255_idx++) {
		for (dio_idx = 0; dio_idx < 3; dio_idx++) {
			subdevice =
			    (me_subdevice_t *)
			    me8255_constructor(me1400_versions[version_idx].
					       device_id,
					       me1400_device->base.info.pci.
					       reg_bases[2], me8255_idx,
					       dio_idx,
					       &me1400_device->
					       dio_current_mode[me8255_idx],
					       &me1400_device->
					       dio_ctrl_reg_lock[me8255_idx]);

			if (!subdevice) {
				me_device_deinit((me_device_t *) me1400_device);
				kfree(me1400_device);
				PERROR("Cannot get memory for subdevice.\n");
				return NULL;
			}

			me_slist_add_subdevice_tail(&me1400_device->base.slist,
						    subdevice);
		}
	}

	/* Generate counter subdevice instances. */
	for (me8254_idx = 0;
	     me8254_idx < me1400_versions[version_idx].ctr_chips;
	     me8254_idx++) {
		for (ctr_idx = 0; ctr_idx < 3; ctr_idx++) {
			subdevice =
			    (me_subdevice_t *)
			    me8254_constructor(me1400_device->base.info.pci.
					       device_id,
					       me1400_device->base.info.pci.
					       reg_bases[2], me8254_idx,
					       ctr_idx,
					       &me1400_device->
					       ctr_ctrl_reg_lock[me8254_idx],
					       &me1400_device->
					       clk_src_reg_lock);

			if (!subdevice) {
				me_device_deinit((me_device_t *) me1400_device);
				kfree(me1400_device);
				PERROR("Cannot get memory for subdevice.\n");
				return NULL;
			}

			me_slist_add_subdevice_tail(&me1400_device->base.slist,
						    subdevice);
		}
	}

	/* Generate external interrupt subdevice instances. */
	for (ext_irq_idx = 0;
	     ext_irq_idx < me1400_versions[version_idx].ext_irq_subdevices;
	     ext_irq_idx++) {
		subdevice =
		    (me_subdevice_t *)
		    me1400_ext_irq_constructor(me1400_device->base.info.pci.
					       device_id,
					       me1400_device->base.info.pci.
					       reg_bases[1],
					       me1400_device->base.info.pci.
					       reg_bases[2],
					       &me1400_device->clk_src_reg_lock,
					       me1400_device->base.irq);

		if (!subdevice) {
			me_device_deinit((me_device_t *) me1400_device);
			kfree(me1400_device);
			PERROR("Cannot get memory for subdevice.\n");
			return NULL;
		}

		me_slist_add_subdevice_tail(&me1400_device->base.slist,
					    subdevice);
	}

	return (me_device_t *) me1400_device;
}

// Init and exit of module.

static int __init me1400_init(void)
{
	PDEBUG("executed.\n");
	return 0;
}

static void __exit me1400_exit(void)
{
	PDEBUG("executed.\n");
}

module_init(me1400_init);
module_exit(me1400_exit);

// Administrative stuff for modinfo.
MODULE_AUTHOR
    ("Guenter Gebhardt <g.gebhardt@meilhaus.de> & Krzysztof Gantzke <k.gantzke@meilhaus.de>");
MODULE_DESCRIPTION("Device Driver Module for Meilhaus ME-14xx devices");
MODULE_SUPPORTED_DEVICE("Meilhaus ME-14xx MIO devices");
MODULE_LICENSE("GPL");

// Export the constructor.
EXPORT_SYMBOL(me1400_pci_constructor);
