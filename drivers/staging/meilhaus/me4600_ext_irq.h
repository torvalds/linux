/**
 * @file me4600_ext_irq.h
 *
 * @brief Meilhaus ME-4000 external interrupt subdevice class.
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

#ifndef _ME4600_EXT_IRQ_H_
#define _ME4600_EXT_IRQ_H_

#include "mesubdevice.h"

#ifdef __KERNEL__

/**
 * @brief The subdevice class.
 */
typedef struct me4600_ext_irq_subdevice {
	/* Inheritance */
	me_subdevice_t base;			/**< The subdevice base class. */

	/* Attributes */
	spinlock_t subdevice_lock;		/**< Spin lock to protect the subdevice from concurrent access. */
	spinlock_t *ctrl_reg_lock;		/**< Spin lock to protect #ctrl_reg from concurrent access. */

	wait_queue_head_t wait_queue;

	int irq;

	int rised;
	int value;
	int count;

	unsigned long ctrl_reg;
	unsigned long irq_status_reg;
	unsigned long ext_irq_config_reg;
	unsigned long ext_irq_value_reg;
#ifdef MEDEBUG_DEBUG_REG
	unsigned long reg_base;
#endif
} me4600_ext_irq_subdevice_t;

/**
 * @brief The constructor to generate a external interrupt subdevice instance.
 *
 * @param reg_base The register base address of the device as returned by the PCI BIOS.
 * @param irq The interrupt number assigned by the PCI BIOS.
 * @param ctrl_reg_lock Pointer to spin lock protecting the control register from concurrent access.
 *
 * @return Pointer to new instance on success.\n
 * NULL on error.
 */
me4600_ext_irq_subdevice_t *me4600_ext_irq_constructor(uint32_t reg_base,
						       int irq,
						       spinlock_t *
						       ctrl_reg_lock);

#endif
#endif
