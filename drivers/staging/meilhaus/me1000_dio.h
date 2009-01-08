/**
 * @file me1000_dio.h
 *
 * @brief Meilhaus ME-1000 digital i/o implementation.
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

#ifndef _ME1000_DIO_H_
#define _ME1000_DIO_H_

#include "mesubdevice.h"
#include "meslock.h"

#ifdef __KERNEL__

/**
 * @brief The ME-1000 DIO subdevice class.
 */
typedef struct me1000_dio_subdevice {
	/* Inheritance */
	me_subdevice_t base;			/**< The subdevice base class. */

	/* Attributes */
//      uint32_t magic;                                 /**< The magic number unique for this structure. */

	spinlock_t subdevice_lock;		/**< Spin lock to protect the subdevice from concurrent access. */
	spinlock_t *ctrl_reg_lock;		/**< Spin lock to protect #ctrl_reg and #ctrl_reg_mirror from concurrent access. */
	int dio_idx;					/**< The index of the DIO port on the device. */

	unsigned long port_reg;			/**< Register to read or write a value from or to the port respectively. */
	unsigned long ctrl_reg;			/**< Register to configure the DIO modes. */
#ifdef MEDEBUG_DEBUG_REG
	unsigned long reg_base;
#endif
} me1000_dio_subdevice_t;

/**
 * @brief The constructor to generate a ME-1000 DIO instance.
 *
 * @param reg_base The register base address of the device as returned by the PCI BIOS.
 * @param dio_idx The index of the DIO on the device.
 * @param ctrl_reg_lock Pointer to spin lock protecting the control register and from concurrent access.
 *
 * @return Pointer to new instance on success.\n
 * NULL on error.
 */
me1000_dio_subdevice_t *me1000_dio_constructor(uint32_t reg_base,
					       unsigned int dio_idx,
					       spinlock_t * ctrl_reg_lock);

#endif
#endif
