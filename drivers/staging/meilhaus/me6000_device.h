/**
 * @file me6000_device.h
 *
 * @brief ME-6000 device class.
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

#ifndef _ME6000_DEVICE_H
#define _ME6000_DEVICE_H

#include <linux/pci.h>
#include <linux/spinlock.h>

#include "medevice.h"

#ifdef __KERNEL__

/**
 * @brief Structure holding ME-6000 device capabilities.
 */
typedef struct me6000_version {
	uint16_t device_id;
	unsigned int dio_subdevices;
	unsigned int ao_subdevices;
	unsigned int ao_fifo;	//How many devices have FIFO
} me6000_version_t;

/**
 * @brief ME-6000 device capabilities.
 */
static me6000_version_t me6000_versions[] = {
	{PCI_DEVICE_ID_MEILHAUS_ME6004, 0, 4, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME6008, 0, 8, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME600F, 0, 16, 0},

	{PCI_DEVICE_ID_MEILHAUS_ME6014, 0, 4, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME6018, 0, 8, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME601F, 0, 16, 0},

	{PCI_DEVICE_ID_MEILHAUS_ME6034, 0, 4, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME6038, 0, 8, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME603F, 0, 16, 0},

	{PCI_DEVICE_ID_MEILHAUS_ME6104, 0, 4, 4},
	{PCI_DEVICE_ID_MEILHAUS_ME6108, 0, 8, 4},
	{PCI_DEVICE_ID_MEILHAUS_ME610F, 0, 16, 4},

	{PCI_DEVICE_ID_MEILHAUS_ME6114, 0, 4, 4},
	{PCI_DEVICE_ID_MEILHAUS_ME6118, 0, 8, 4},
	{PCI_DEVICE_ID_MEILHAUS_ME611F, 0, 16, 4},

	{PCI_DEVICE_ID_MEILHAUS_ME6134, 0, 4, 4},
	{PCI_DEVICE_ID_MEILHAUS_ME6138, 0, 8, 4},
	{PCI_DEVICE_ID_MEILHAUS_ME613F, 0, 16, 4},

	{PCI_DEVICE_ID_MEILHAUS_ME6044, 2, 4, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME6048, 2, 8, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME604F, 2, 16, 0},

	{PCI_DEVICE_ID_MEILHAUS_ME6054, 2, 4, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME6058, 2, 8, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME605F, 2, 16, 0},

	{PCI_DEVICE_ID_MEILHAUS_ME6074, 2, 4, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME6078, 2, 8, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME607F, 2, 16, 0},

	{PCI_DEVICE_ID_MEILHAUS_ME6144, 2, 4, 4},
	{PCI_DEVICE_ID_MEILHAUS_ME6148, 2, 8, 4},
	{PCI_DEVICE_ID_MEILHAUS_ME614F, 2, 16, 4},

	{PCI_DEVICE_ID_MEILHAUS_ME6154, 2, 4, 4},
	{PCI_DEVICE_ID_MEILHAUS_ME6158, 2, 8, 4},
	{PCI_DEVICE_ID_MEILHAUS_ME615F, 2, 16, 4},

	{PCI_DEVICE_ID_MEILHAUS_ME6174, 2, 4, 4},
	{PCI_DEVICE_ID_MEILHAUS_ME6178, 2, 8, 4},
	{PCI_DEVICE_ID_MEILHAUS_ME617F, 2, 16, 4},

	{PCI_DEVICE_ID_MEILHAUS_ME6259, 2, 9, 0},

	{PCI_DEVICE_ID_MEILHAUS_ME6359, 2, 9, 4},

	{0},
};

#define ME6000_DEVICE_VERSIONS (sizeof(me6000_versions) / sizeof(me6000_version_t) - 1)	/**< Returns the number of entries in #me6000_versions. */

/**
 * @brief Returns the index of the device entry in #me6000_versions.
 *
 * @param device_id The PCI device id of the device to query.
 * @return The index of the device in #me6000_versions.
 */
static inline unsigned int me6000_versions_get_device_index(uint16_t device_id)
{
	unsigned int i;
	for (i = 0; i < ME6000_DEVICE_VERSIONS; i++)
		if (me6000_versions[i].device_id == device_id)
			break;
	return i;
}

/**
 * @brief The ME-6000 device class structure.
 */
typedef struct me6000_device {
	me_device_t base;			/**< The Meilhaus device base class. */

	/* Child class attributes. */
	spinlock_t preload_reg_lock;		/**< Guards the preload register. */
	uint32_t preload_flags;
	uint32_t triggering_flags;

	spinlock_t dio_ctrl_reg_lock;
} me6000_device_t;

/**
 * @brief The ME-6000 device class constructor.
 *
 * @param pci_device The pci device structure given by the PCI subsystem.
 *
 * @return On succes a new ME-6000 device instance. \n
 *         NULL on error.
 */
me_device_t *me6000_pci_constructor(struct pci_dev *pci_device)
    __attribute__ ((weak));

#endif
#endif
