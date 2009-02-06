/**
 * @file me0600_ttli.h
 *
 * @brief ME-630 TTL input subdevice class.
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

#ifndef _ME0600_TTLI_H_
#define _ME0600_TTLI_H_

#include "mesubdevice.h"

#ifdef __KERNEL__

/**
 * @brief The template subdevice class.
 */
typedef struct me0600_ttli_subdevice {
	/* Inheritance */
	me_subdevice_t base;			/**< The subdevice base class. */

	/* Attributes */
	spinlock_t subdevice_lock;		/**< Spin lock to protect the subdevice from concurrent access. */

	uint32_t port_reg;			/**< Register holding the port status. */
} me0600_ttli_subdevice_t;

/**
 * @brief The constructor to generate a ME-630 TTL input subdevice instance.
 *
 * @param reg_base The register base address of the device as returned by the PCI BIOS.
 *
 * @return Pointer to new instance on success.\n
 * NULL on error.
 */
me0600_ttli_subdevice_t *me0600_ttli_constructor(uint32_t reg_base);

#endif
#endif
