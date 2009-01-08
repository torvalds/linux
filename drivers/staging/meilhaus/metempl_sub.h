/**
 * @file metempl_sub.h
 *
 * @brief Meilhaus subdevice class.
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

#ifndef _METEMPL_SUB_H_
#define _METEMPL_SUB_H_

#include "mesubdevice.h"

#ifdef __KERNEL__

/**
 * @brief The subdevice class.
 */
typedef struct metempl_sub_subdevice {
	/* Inheritance */
	me_subdevice_t base;			/**< The subdevice base class. */

	/* Attributes */
	spinlock_t subdevice_lock;		/**< Spin lock to protect the subdevice from concurrent access. */
	spinlock_t *ctrl_reg_lock;		/**< Spin lock to protect #ctrl_reg from concurrent access. */
	int sub_idx;				/**< The index of the subdevice on the device. */

	unsigned long ctrl_reg;			/**< Register to configure the modes. */
} metempl_sub_subdevice_t;

/**
 * @brief The constructor to generate a subdevice instance.
 *
 * @param reg_base The register base address of the device as returned by the PCI BIOS.
 * @param sub_idx The index of the subdevice on the device.
 * @param ctrl_reg_lock Pointer to spin lock protecting the control register from concurrent access.
 *
 * @return Pointer to new instance on success.\n
 * NULL on error.
 */
metempl_sub_subdevice_t *metempl_sub_constructor(uint32_t reg_base,
						 unsigned int sub_idx,
						 spinlock_t * ctrl_reg_lock);

#endif
#endif
