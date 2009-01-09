/**
 * @file me8100_do.h
 *
 * @brief ME-8100 digital output subdevice class.
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

#ifndef _ME8100_DO_H_
#define _ME8100_DO_H_

#include "mesubdevice.h"

#ifdef __KERNEL__

/**
 * @brief The template subdevice class.
 */
typedef struct me8100_do_subdevice {
	/* Inheritance */
	me_subdevice_t base;			/**< The subdevice base class. */

	/* Attributes */
	spinlock_t subdevice_lock;		/**< Spin lock to protect the subdevice from concurrent access. */
	spinlock_t *ctrl_reg_lock;		/**< Spin lock to protect the #ctrl_reg. */

	unsigned int do_idx;

	uint16_t port_reg_mirror;		/**< Mirror used to store current port register setting which is write only. */

	unsigned long port_reg;			/**< Register holding the port status. */
	unsigned long ctrl_reg;			/**< Control register. */
#ifdef MEDEBUG_DEBUG_REG
	unsigned long reg_base;
#endif
} me8100_do_subdevice_t;

/**
 * @brief The constructor to generate a ME-8100 digital output subdevice instance.
 *
 * @param reg_base The register base address of the device as returned by the PCI BIOS.
 * @param do_idx The index of the digital output subdevice on this device.
 *
 * @return Pointer to new instance on success.\n
 * NULL on error.
 */
me8100_do_subdevice_t *me8100_do_constructor(uint32_t reg_base,
					     unsigned int do_idx,
					     spinlock_t * ctrl_reg_lock);

#endif
#endif
