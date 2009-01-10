/**
 * @file me8200_do.h
 *
 * @brief ME-8200 digital output subdevice class.
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

#ifndef _ME8200_DO_H_
#define _ME8200_DO_H_

#include "mesubdevice.h"

#ifdef __KERNEL__

/**
 * @brief The template subdevice class.
 */
typedef struct me8200_do_subdevice {
	/* Inheritance */
	me_subdevice_t base;			/**< The subdevice base class. */

	/* Attributes */
	spinlock_t subdevice_lock;		/**< Spin lock to protect the subdevice from concurrent access. */
	spinlock_t *irq_mode_lock;

	int irq;				/**< The number of the interrupt request */
	int rised;				/**< Flag to indicate if an interrupt occured */
	int count;				/**< Counts the number of interrupts occured */
	wait_queue_head_t wait_queue;		/**< To wait on interrupts */

	unsigned int do_idx;			/**< The number of the digital output */

	unsigned long port_reg;			/**< The digital output port */
	unsigned long irq_ctrl_reg;		/**< The interrupt control register */
	unsigned long irq_status_reg;		/**< The interrupt status register */
#ifdef MEDEBUG_DEBUG_REG
	unsigned long reg_base;
#endif
} me8200_do_subdevice_t;

/**
 * @brief The constructor to generate a ME-8200 digital output subdevice instance.
 *
 * @param reg_base The register base address of the device as returned by the PCI BIOS.
 * @param do_idx The index of the digital output subdevice on this device.
 *
 * @return Pointer to new instance on success.\n
 * NULL on error.
 */
me8200_do_subdevice_t *me8200_do_constructor(uint32_t reg_base,
					     unsigned int do_idx,
					     int irq,
					     spinlock_t * irq_mode_lock);

#endif
#endif
