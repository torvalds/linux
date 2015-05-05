/* memregion_direct.c
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

/*
 *  This is an implementation of memory regions that can be used to read/write
 *  channel memory (in main memory of the host system) from code running in
 *  a virtual partition.
 */
#include "timskmod.h"
#include "memregion.h"

#define MYDRVNAME "memregion"

int
visor_memregion_write(struct memregion *memregion, ulong offset, void *src,
		      ulong nbytes)
{
	if (offset + nbytes > memregion->nbytes)
		return -EIO;

	memcpy_toio(memregion->mapped + offset, src, nbytes);
	return 0;
}
EXPORT_SYMBOL_GPL(visor_memregion_write);
