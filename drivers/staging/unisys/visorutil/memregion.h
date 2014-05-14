/* memregion.h
 *
 * Copyright © 2010 - 2013 UNISYS CORPORATION
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

/* MEMREGION is an opaque structure to users.
 * Fields are declared only in the implementation .c files.
 */
typedef struct MEMREGION_Tag MEMREGION;

MEMREGION *visor_memregion_create(HOSTADDRESS physaddr, ulong nbytes);
MEMREGION *visor_memregion_create_overlapped(MEMREGION *parent,
					     ulong offset, ulong nbytes);
int visor_memregion_resize(MEMREGION *memregion, ulong newsize);
int visor_memregion_read(MEMREGION *memregion,
		   ulong offset, void *dest, ulong nbytes);
int visor_memregion_write(MEMREGION *memregion,
			  ulong offset, void *src, ulong nbytes);
void visor_memregion_destroy(MEMREGION *memregion);
HOSTADDRESS visor_memregion_get_physaddr(MEMREGION *memregion);
ulong visor_memregion_get_nbytes(MEMREGION *memregion);
void memregion_dump(MEMREGION *memregion, char *s,
		    ulong off, ulong len, struct seq_file *seq);
void *visor_memregion_get_pointer(MEMREGION *memregion);

#endif
