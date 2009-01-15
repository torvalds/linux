/**
 * @file me1400_device.c
 *
 * @brief ME-1400 device family instance.
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

#ifndef _ME1400_DEVICE_H_
#define _ME1400_DEVICE_H_

#include "metypes.h"
#include "medefines.h"
#include "meinternal.h"

#include "medevice.h"

#ifdef __KERNEL__

/**
 * @brief Structure to store device capabilities.
 */
typedef struct me1400_version {
	uint16_t device_id;					/**< The PCI device id of the device. */
	unsigned int dio_chips;				/**< The number of 8255 chips on the device. */
	unsigned int ctr_chips;				/**< The number of 8254 chips on the device. */
	unsigned int ext_irq_subdevices;	/**< The number of external interrupt inputs on the device. */
} me1400_version_t;

/**
  * @brief Defines for each ME-1400 device version its capabilities.
 */
static me1400_version_t me1400_versions[] = {
	{PCI_DEVICE_ID_MEILHAUS_ME1400, 1, 0, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME140A, 1, 1, 1},
	{PCI_DEVICE_ID_MEILHAUS_ME140B, 2, 2, 1},
	{PCI_DEVICE_ID_MEILHAUS_ME14E0, 1, 0, 0},
	{PCI_DEVICE_ID_MEILHAUS_ME14EA, 1, 1, 1},
	{PCI_DEVICE_ID_MEILHAUS_ME14EB, 2, 2, 1},
	{PCI_DEVICE_ID_MEILHAUS_ME140C, 1, 5, 1},
	{PCI_DEVICE_ID_MEILHAUS_ME140D, 2, 10, 1},
	{0}
};

#define ME1400_DEVICE_VERSIONS (sizeof(me1400_versions) / sizeof(me1400_version_t) - 1)	/**< Returns the number of entries in #me1400_versions. */

/**
 * @brief Returns the index of the device entry in #me1400_versions.
 *
 * @param device_id The PCI device id of the device to query.
 * @return The index of the device in #me1400_versions.
 */
static inline unsigned int me1400_versions_get_device_index(uint16_t device_id)
{
	unsigned int i;
	for (i = 0; i < ME1400_DEVICE_VERSIONS; i++)
		if (me1400_versions[i].device_id == device_id)
			break;
	return i;
}

#define ME1400_MAX_8254		10	/**< The maximum number of 8254 counter subdevices available on any ME-1400 device. */
#define ME1400_MAX_8255		2	/**< The maximum number of 8255 digital i/o subdevices available on any ME-1400 device. */

/**
 * @brief The ME-1400 device class.
 */
typedef struct me1400_device {
	me_device_t base;									/**< The Meilhaus device base class. */

	spinlock_t clk_src_reg_lock;						/**< Guards the 8254 clock source registers. */
	spinlock_t ctr_ctrl_reg_lock[ME1400_MAX_8254];		/**< Guards the 8254 ctrl registers. */

	int dio_current_mode[ME1400_MAX_8255];				/**< Saves the current mode setting of a single 8255 DIO chip. */
	spinlock_t dio_ctrl_reg_lock[ME1400_MAX_8255];		/**< Guards the 8255 ctrl register and #dio_current_mode. */
} me1400_device_t;

/**
 * @brief The ME-1400 device class constructor.
 *
 * @param pci_device The pci device structure given by the PCI subsystem.
 *
 * @return On succes a new ME-1400 device instance. \n
 *         NULL on error.
 */
me_device_t *me1400_pci_constructor(struct pci_dev *pci_device)
    __attribute__ ((weak));

#endif
#endif
