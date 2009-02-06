/**
 * @file me8254.h
 *
 * @brief 8254 counter implementation.
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

#ifndef _ME8254_H_
#define _ME8254_H_

#include "mesubdevice.h"
#include "meslock.h"

#ifdef __KERNEL__

/**
 * @brief The 8254 subdevice class.
 */
typedef struct me8254_subdevice {
	/* Inheritance */
	me_subdevice_t base;			/**< The subdevice base class. */

	/* Attributes */
	spinlock_t subdevice_lock;		/**< Spin lock to protect the subdevice from concurrent access. */

	spinlock_t *ctrl_reg_lock;		/**< Spin lock to protect the control register from concurrent access. */
	spinlock_t *clk_src_reg_lock;		/**< Spin lock to protect the clock source register from concurrent access. */

	uint32_t device_id;			/**< The Meilhaus device type carrying the 8254 chip. */
	int me8254_idx;				/**< The index of the 8254 chip on the device. */
	int ctr_idx;				/**< The index of the counter on the 8254 chip. */

	int caps;				/**< Holds the device capabilities. */

	unsigned long val_reg;			/**< Holds the actual counter value. */
	unsigned long ctrl_reg;			/**< Register to configure the 8254 modes. */
	unsigned long clk_src_reg;		/**< Register to configure the counter connections. */
} me8254_subdevice_t;

/**
 * @brief The constructor to generate a 8254 instance.
 *
 * @param device_id The kind of Meilhaus device holding the 8254.
 * @param reg_base The register base address of the device as returned by the PCI BIOS.
 * @param me8254_idx The index of the 8254 chip on the Meilhaus device.
 * @param ctr_idx The index of the counter inside a 8254 chip.
 * @param ctrl_reg_lock Pointer to spin lock protecting the 8254 control register from concurrent access.
 * @param clk_src_reg_lock Pointer to spin lock protecting the clock source register from concurrent access.
 *
 * @return Pointer to new instance on success.\n
 * NULL on error.
 */
me8254_subdevice_t *me8254_constructor(uint32_t device_id,
				       uint32_t reg_base,
				       unsigned int me8254_idx,
				       unsigned int ctr_idx,
				       spinlock_t * ctrl_reg_lock,
				       spinlock_t * clk_src_reg_lock);

#endif
#endif
