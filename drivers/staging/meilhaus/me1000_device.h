/**
 * @file me1000_device.h
 *
 * @brief ME-1000 device class instance.
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

#ifndef _ME1000_H_
#define _ME1000_H_

#include <linux/pci.h>
#include <linux/spinlock.h>

#include "medevice.h"

#ifdef __KERNEL__

#define ME1000_MAGIC_NUMBER	1000

/**
 * @brief The ME-1000 device class structure.
 */
typedef struct me1000_device {
	me_device_t base;		/**< The Meilhaus device base class. */
	spinlock_t ctrl_lock;		/**< Guards the DIO mode register. */
} me1000_device_t;

/**
 * @brief The ME-1000 device class constructor.
 *
 * @param pci_device The pci device structure given by the PCI subsystem.
 *
 * @return On succes a new ME-1000 device instance. \n
 *         NULL on error.
 */
me_device_t *me1000_pci_constructor(struct pci_dev *pci_device)
    __attribute__ ((weak));

#endif
#endif
