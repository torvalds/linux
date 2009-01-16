/**
 * @file meplx_reg.h
 *
 * @brief PLX 9052 PCI bridge register definitions.
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

#ifndef _MEPLX_REG_H_
#define _MEPLX_REG_H_

#ifdef __KERNEL__

#define PLX_INTCSR				0x4C		/**< Interrupt control and status register. */
#define PLX_INTCSR_LOCAL_INT1_EN		0x01		/**< If set, local interrupt 1 is enabled (r/w). */
#define PLX_INTCSR_LOCAL_INT1_POL		0x02		/**< If set, local interrupt 1 polarity is active high (r/w). */
#define PLX_INTCSR_LOCAL_INT1_STATE		0x04		/**< If set, local interrupt 1 is active (r/_). */
#define PLX_INTCSR_LOCAL_INT2_EN		0x08		/**< If set, local interrupt 2 is enabled (r/w). */
#define PLX_INTCSR_LOCAL_INT2_POL		0x10		/**< If set, local interrupt 2 polarity is active high (r/w). */
#define PLX_INTCSR_LOCAL_INT2_STATE		0x20		/**< If set, local interrupt 2 is active  (r/_). */
#define PLX_INTCSR_PCI_INT_EN			0x40		/**< If set, PCI interrupt is enabled (r/w). */
#define PLX_INTCSR_SOFT_INT			0x80		/**< If set, a software interrupt is generated (r/w). */

#define PLX_ICR					0x50		/**< Initialization control register. */
#define PLX_ICR_BIT_EEPROM_CLOCK_SET		0x01000000
#define PLX_ICR_BIT_EEPROM_CHIP_SELECT		0x02000000
#define PLX_ICR_BIT_EEPROM_WRITE		0x04000000
#define PLX_ICR_BIT_EEPROM_READ			0x08000000
#define PLX_ICR_BIT_EEPROM_VALID		0x10000000

#define PLX_ICR_MASK_EEPROM			0x1F000000
#define EEPROM_DELAY				1

#endif
#endif
