/**
 * @file me0900_di.h
 *
 * @brief ME-9x digital input subdevice class.
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

#ifndef _ME0900_DI_H_
#define _ME0900_DI_H_

#include "mesubdevice.h"

#ifdef __KERNEL__

/**
 * @brief The template subdevice class.
 */
typedef struct me0900_di_subdevice {
	/* Inheritance */
	me_subdevice_t base;			/**< The subdevice base class. */

	/* Attributes */
	spinlock_t subdevice_lock;		/**< Spin lock to protect the subdevice from concurrent access. */

	unsigned int di_idx;

	unsigned long ctrl_reg;
	unsigned long port_reg;
#ifdef MEDEBUG_DEBUG_REG
	unsigned long reg_base;
#endif
} me0900_di_subdevice_t;

/**
 * @brief The constructor to generate a ME-9x digital input subdevice instance.
 *
 * @param reg_base The register base address of the device as returned by the PCI BIOS.
 *
 * @return Pointer to new instance on success.\n
 * NULL on error.
 */
me0900_di_subdevice_t *me0900_di_constructor(uint32_t me0900_reg_base,
					     unsigned int di_idx);

#endif
#endif
