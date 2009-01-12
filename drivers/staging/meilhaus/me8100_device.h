/**
 * @file me8100_device.h
 *
 * @brief ME-8100 device class.
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

#ifndef _ME8100_DEVICE_H
#define _ME8100_DEVICE_H

#include <linux/pci.h>
#include <linux/spinlock.h>

#include "medevice.h"

#ifdef __KERNEL__

/**
 * @brief Structure holding ME-8100 device capabilities.
 */
typedef struct me8100_version {
	uint16_t device_id;
	unsigned int di_subdevices;
	unsigned int do_subdevices;
	unsigned int ctr_subdevices;
} me8100_version_t;

/**
 * @brief Device capabilities.
 */
static me8100_version_t me8100_versions[] = {
	{PCI_DEVICE_ID_MEILHAUS_ME8100_A, 1, 1, 3},
	{PCI_DEVICE_ID_MEILHAUS_ME8100_B, 2, 2, 3},
	{0},
};

#define ME8100_DEVICE_VERSIONS (sizeof(me8100_versions) / sizeof(me8100_version_t) - 1)	/**< Returns the number of entries in #me8100_versions. */

/**
 * @brief Returns the index of the device entry in #me8100_versions.
 *
 * @param device_id The PCI device id of the device to query.
 * @return The index of the device in #me8100_versions.
 */
static inline unsigned int me8100_versions_get_device_index(uint16_t device_id)
{
	unsigned int i;
	for (i = 0; i < ME8100_DEVICE_VERSIONS; i++)
		if (me8100_versions[i].device_id == device_id)
			break;
	return i;
}

/**
 * @brief The ME-8100 device class structure.
 */
typedef struct me8100_device {
	me_device_t base;			/**< The Meilhaus device base class. */

	/* Child class attributes. */
	spinlock_t dio_ctrl_reg_lock;
	spinlock_t ctr_ctrl_reg_lock;
	spinlock_t clk_src_reg_lock;
} me8100_device_t;

/**
 * @brief The ME-8100 device class constructor.
 *
 * @param pci_device The pci device structure given by the PCI subsystem.
 *
 * @return On succes a new ME-8100 device instance. \n
 *         NULL on error.
 */
me_device_t *me8100_pci_constructor(struct pci_dev *pci_device)
    __attribute__ ((weak));

#endif
#endif
