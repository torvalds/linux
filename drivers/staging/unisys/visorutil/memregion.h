/* memregion.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __MEMREGION_H__
#define __MEMREGION_H__

#include "timskmod.h"

/* struct memregion is an opaque structure to users.
 * Fields are declared only in the implementation .c files.
 */
struct memregion;

struct memregion *visor_memregion_create(HOSTADDRESS physaddr, ulong nbytes);
struct memregion *visor_memregion_create_overlapped(struct memregion *parent,
						    ulong offset, ulong nbytes);
int visor_memregion_resize(struct memregion *memregion, ulong newsize);
int visor_memregion_read(struct memregion *memregion,
			 ulong offset, void *dest, ulong nbytes);
int visor_memregion_write(struct memregion *memregion,
			  ulong offset, void *src, ulong nbytes);
void visor_memregion_destroy(struct memregion *memregion);
HOSTADDRESS visor_memregion_get_physaddr(struct memregion *memregion);
ulong visor_memregion_get_nbytes(struct memregion *memregion);
void memregion_dump(struct memregion *memregion, char *s,
		    ulong off, ulong len, struct seq_file *seq);
void __iomem *visor_memregion_get_pointer(struct memregion *memregion);

#endif
