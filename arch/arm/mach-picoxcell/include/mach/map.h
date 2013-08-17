/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
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
 */
#ifndef __PICOXCELL_MAP_H__
#define __PICOXCELL_MAP_H__

#define PHYS_TO_IO(x)		(((x) & 0x00ffffff) | 0xfe000000)

#ifdef __ASSEMBLY__
#define IO_ADDRESS(x)		PHYS_TO_IO((x))
#else
#define IO_ADDRESS(x)		(void __iomem __force *)(PHYS_TO_IO((x)))
#endif

#endif /* __PICOXCELL_MAP_H__ */
