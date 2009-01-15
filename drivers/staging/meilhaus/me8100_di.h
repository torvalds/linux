/**
 * @file me8100_di.h
 *
 * @brief ME-8100 digital input subdevice class.
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

#ifndef _ME8100_DI_H_
#define _ME8100_DI_H_

#include "mesubdevice.h"

#ifdef __KERNEL__

/**
 * @brief The template subdevice class.
 */
typedef struct me8100_di_subdevice {
	// Inheritance
	me_subdevice_t base;			/**< The subdevice base class. */

	/* Attributes */
	spinlock_t subdevice_lock;		/**< Spin lock to protect the subdevice from concurrent access. */
	spinlock_t *ctrl_reg_lock;

	unsigned di_idx;

	int irq;
	volatile int rised;
	unsigned int irq_count;

	uint status_flag;				/**< Default interupt status flag */
	uint status_value;				/**< Interupt status */
	uint status_value_edges;		/**< Extended interupt status */
	uint line_value;

	uint16_t compare_value;
	uint8_t filtering_flag;

	wait_queue_head_t wait_queue;

	unsigned long ctrl_reg;
	unsigned long port_reg;
	unsigned long mask_reg;
	unsigned long pattern_reg;
	unsigned long long din_int_reg;
	unsigned long irq_reset_reg;
	unsigned long irq_status_reg;
#ifdef MEDEBUG_DEBUG_REG
	unsigned long reg_base;
#endif

} me8100_di_subdevice_t;

/**
 * @brief The constructor to generate a ME-8100 digital input subdevice instance.
 *
 * @param reg_base The register base address of the device as returned by the PCI BIOS.
 *
 * @return Pointer to new instance on success.\n
 * NULL on error.
 */
me8100_di_subdevice_t *me8100_di_constructor(uint32_t me8100_reg_base,
					     uint32_t plx_reg_base,
					     unsigned int di_idx,
					     int irq,
					     spinlock_t * ctrl_leg_lock);

#endif
#endif
