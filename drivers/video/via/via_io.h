/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2010 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/*
 * basic io functions
 */

#ifndef __VIA_IO_H__
#define __VIA_IO_H__

#include <linux/types.h>
#include <linux/io.h>

/*
 * Indexed port operations.  Note that these are all multi-op
 * functions; every invocation will be racy if you're not holding
 * reg_lock.
 */
static inline u8 via_read_reg(u16 port, u8 index)
{
	outb(index, port);
	return inb(port + 1);
}

static inline void via_write_reg(u16 port, u8 index, u8 data)
{
	outb(index, port);
	outb(data, port + 1);
}

static inline void via_write_reg_mask(u16 port, u8 index, u8 data, u8 mask)
{
	u8 old;

	outb(index, port);
	old = inb(port + 1);
	outb((data & mask) | (old & ~mask), port + 1);
}

#endif /* __VIA_IO_H__ */
