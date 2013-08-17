/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Access to VGA videoram.
 */

#ifndef _ASM_TILE_VGA_H
#define _ASM_TILE_VGA_H

#include <asm/io.h>

#define VT_BUF_HAVE_RW

static inline void scr_writew(u16 val, volatile u16 *addr)
{
	__raw_writew(val, (volatile u16 __iomem *) addr);
}

static inline u16 scr_readw(volatile const u16 *addr)
{
	return __raw_readw((volatile const u16 __iomem *) addr);
}

#define vga_readb(a)	readb((u8 __iomem *)(a))
#define vga_writeb(v,a)	writeb(v, (u8 __iomem *)(a))

#define VGA_MAP_MEM(x,s)	((unsigned long) ioremap(x, s))

#endif
