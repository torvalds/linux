/* zd_types.h
 *
 * This program is free software; you can redistribute it and/or modify
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _ZD_TYPES_H
#define _ZD_TYPES_H

#include <linux/types.h>

/* We have three register spaces mapped into the overall USB address space of
 * 64K words (16-bit values). There is the control register space of
 * double-word registers, the eeprom register space and the firmware register
 * space. The control register space is byte mapped, the others are word
 * mapped.
 *
 * For that reason, we are using byte offsets for control registers and word
 * offsets for everything else.
 */

typedef u32 __nocast zd_addr_t;

enum {
	ADDR_BASE_MASK		= 0xff000000,
	ADDR_OFFSET_MASK	= 0x0000ffff,
	ADDR_ZERO_MASK		= 0x00ff0000,
	NULL_BASE		= 0x00000000,
	USB_BASE		= 0x01000000,
	CR_BASE			= 0x02000000,
	CR_MAX_OFFSET		= 0x0b30,
	E2P_BASE		= 0x03000000,
	E2P_MAX_OFFSET		= 0x007e,
	FW_BASE			= 0x04000000,
	FW_MAX_OFFSET		= 0x0005,
};

#define ZD_ADDR_BASE(addr) ((u32)(addr) & ADDR_BASE_MASK)
#define ZD_OFFSET(addr) ((u32)(addr) & ADDR_OFFSET_MASK)

#define ZD_ADDR(base, offset) \
	((zd_addr_t)(((base) & ADDR_BASE_MASK) | ((offset) & ADDR_OFFSET_MASK)))

#define ZD_NULL_ADDR    ((zd_addr_t)0)
#define USB_REG(offset)  ZD_ADDR(USB_BASE, offset)	/* word addressing */
#define CTL_REG(offset)  ZD_ADDR(CR_BASE, offset)	/* byte addressing */
#define E2P_REG(offset)  ZD_ADDR(E2P_BASE, offset)	/* word addressing */
#define FW_REG(offset)   ZD_ADDR(FW_BASE, offset)	/* word addressing */

static inline zd_addr_t zd_inc_word(zd_addr_t addr)
{
	u32 base = ZD_ADDR_BASE(addr);
	u32 offset = ZD_OFFSET(addr);

	offset += base == CR_BASE ? 2 : 1;

	return base | offset;
}

#endif /* _ZD_TYPES_H */
