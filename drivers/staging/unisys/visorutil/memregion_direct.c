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

static int mapit(struct memregion *memregion);
static void unmapit(struct memregion *memregion);

static int
mapit(struct memregion *memregion)
{
	ulong physaddr = (ulong)(memregion->physaddr);
	ulong nbytes = memregion->nbytes;

	if (!request_mem_region(physaddr, nbytes, MYDRVNAME))
		return -EBUSY;

	memregion->mapped = ioremap_cache(physaddr, nbytes);
	if (!memregion->mapped)
		return -EFAULT;

	return 0;
}

static void
unmapit(struct memregion *memregion)
{
	if (memregion->mapped) {
		iounmap(memregion->mapped);
		memregion->mapped = NULL;
		release_mem_region((unsigned long)memregion->physaddr,
				   memregion->nbytes);
	}
}

HOSTADDRESS
visor_memregion_get_physaddr(struct memregion *memregion)
{
	return memregion->physaddr;
}
EXPORT_SYMBOL_GPL(visor_memregion_get_physaddr);

ulong
visor_memregion_get_nbytes(struct memregion *memregion)
{
	return memregion->nbytes;
}
EXPORT_SYMBOL_GPL(visor_memregion_get_nbytes);

void __iomem *
visor_memregion_get_pointer(struct memregion *memregion)
{
	return memregion->mapped;
}
EXPORT_SYMBOL_GPL(visor_memregion_get_pointer);

int
visor_memregion_resize(struct memregion *memregion, ulong newsize)
{
	int rc;

	if (newsize == memregion->nbytes)
		return 0;

	unmapit(memregion);
	memregion->nbytes = newsize;
	rc = mapit(memregion);

	return rc;
}
EXPORT_SYMBOL_GPL(visor_memregion_resize);

int
visor_memregion_read(struct memregion *memregion, ulong offset, void *dest,
		     ulong nbytes)
{
	if (offset + nbytes > memregion->nbytes)
		return -EIO;

	memcpy_fromio(dest, memregion->mapped + offset, nbytes);
	return 0;
}
EXPORT_SYMBOL_GPL(visor_memregion_read);

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

void
visor_memregion_destroy(struct memregion *memregion)
{
	if (!memregion)
		return;
	unmapit(memregion);
}
EXPORT_SYMBOL_GPL(visor_memregion_destroy);
