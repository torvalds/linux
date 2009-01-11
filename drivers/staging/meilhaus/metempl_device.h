/**
 * @file metempl_device.h
 *
 * @brief template device class.
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

#ifndef _METEMPL_DEVICE_H
#define _METEMPL_DEVICE_H

#include <linux/pci.h>
#include <linux/spinlock.h>

#include "medevice.h"

#ifdef __KERNEL__

/**
 * @brief Structure holding template device capabilities.
 */
typedef struct metempl_version {
	uint16_t device_id;
	unsigned int subdevices;
} metempl_version_t;

/**
 * @brief Device capabilities.
 */
static metempl_version_t metempl_versions[] = {
	{0xDEAD, 1},
	{0},
};

#define METEMPL_DEVICE_VERSIONS (sizeof(metempl_versions) / sizeof(metempl_version_t) - 1) /**< Returns the number of entries in #metempl_versions. */

/**
 * @brief Returns the index of the device entry in #metempl_versions.
 *
 * @param device_id The PCI device id of the device to query.
 * @return The index of the device in #metempl_versions.
 */
static inline unsigned int metempl_versions_get_device_index(uint16_t device_id)
{
	unsigned int i;
	for (i = 0; i < METEMPL_DEVICE_VERSIONS; i++)
		if (metempl_versions[i].device_id == device_id)
			break;
	return i;
}

/**
 * @brief The template device class structure.
 */
typedef struct metempl_device {
	me_device_t base;			/**< The Meilhaus device base class. */

	/* Child class attributes. */
	spinlock_t ctrl_reg_lock;
} metempl_device_t;

/**
 * @brief The template device class constructor.
 *
 * @param pci_device The pci device structure given by the PCI subsystem.
 *
 * @return On succes a new template device instance. \n
 *         NULL on error.
 */
me_device_t *metempl_pci_constructor(struct pci_dev *pci_device)
    __attribute__ ((weak));

#endif
#endif
