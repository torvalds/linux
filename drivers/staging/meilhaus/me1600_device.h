/**
 * @file me1600_device.h
 *
 * @brief ME-1600 device class.
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

#ifndef _ME1600_H
#define _ME1600_H

#include <linux/pci.h>
#include <linux/spinlock.h>

#include "medevice.h"
#include "me1600_ao.h"
#include "me1600_ao_reg.h"

#ifdef __KERNEL__

/**
 * @brief Structure to store device capabilities.
 */
typedef struct me1600_version {
	uint16_t device_id;				/**< The PCI device id of the device. */
	unsigned int ao_chips;			/**< The number of analog outputs on the device. */
	int curr;						/**< Flag to identify amounts of current output. */
} me1600_version_t;

/**
  * @brief Defines for each ME-1600 device version its capabilities.
 */
static me1600_version_t me1600_versions[] = {
	{PCI_DEVICE_ID_MEILHAUS_ME1600_4U, 4, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME1600_8U, 8, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME1600_12U, 12, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME1600_16U, 16, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME1600_16U_8I, 16, 8},
	{0}
};

/**< Returns the number of entries in #me1600_versions. */
#define ME1600_DEVICE_VERSIONS (sizeof(me1600_versions) / sizeof(me1600_version_t) - 1)

/**
 * @brief Returns the index of the device entry in #me1600_versions.
 *
 * @param device_id The PCI device id of the device to query.
 * @return The index of the device in #me1600_versions.
 */
static inline unsigned int me1600_versions_get_device_index(uint16_t device_id)
{
	unsigned int i;
	for (i = 0; i < ME1600_DEVICE_VERSIONS; i++)
		if (me1600_versions[i].device_id == device_id)
			break;
	return i;
}

/**
 * @brief The ME-1600 device class structure.
 */
typedef struct me1600_device {
	me_device_t base;						/**< The Meilhaus device base class. */
	spinlock_t config_regs_lock;			/**< Protects the configuration registers. */

	me1600_ao_shadow_t ao_regs_shadows;		/**< Addresses and shadows of output's registers. */
	spinlock_t ao_shadows_lock;				/**< Protects the shadow's struct. */
} me1600_device_t;

/**
 * @brief The ME-1600 device class constructor.
 *
 * @param pci_device The pci device structure given by the PCI subsystem.
 *
 * @return On succes a new ME-1600 device instance. \n
 *         NULL on error.
 */
me_device_t *me1600_pci_constructor(struct pci_dev *pci_device)
    __attribute__ ((weak));

#endif
#endif
