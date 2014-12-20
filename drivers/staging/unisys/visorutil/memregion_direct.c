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
#include "uniklog.h"
#include "timskmod.h"
#include "memregion.h"

#define MYDRVNAME "memregion"

struct memregion {
	HOSTADDRESS physaddr;
	ulong nbytes;
	void __iomem *mapped;
	BOOL requested;
	BOOL overlapped;
};

static BOOL mapit(struct memregion *memregion);
static void unmapit(struct memregion *memregion);

struct memregion *
visor_memregion_create(HOSTADDRESS physaddr, ulong nbytes)
{
	struct memregion *rc = NULL;
	struct memregion *memregion = kzalloc(sizeof(*memregion),
					      GFP_KERNEL | __GFP_NORETRY);
	if (memregion == NULL) {
		ERRDRV("visor_memregion_create allocation failed");
		return NULL;
	}
	memregion->physaddr = physaddr;
	memregion->nbytes = nbytes;
	memregion->overlapped = FALSE;
	if (!mapit(memregion)) {
		rc = NULL;
		goto cleanup;
	}
	rc = memregion;
cleanup:
	if (rc == NULL) {
		visor_memregion_destroy(memregion);
		memregion = NULL;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(visor_memregion_create);

struct memregion *
visor_memregion_create_overlapped(struct memregion *parent, ulong offset,
				  ulong nbytes)
{
	struct memregion *memregion = NULL;

	if (parent == NULL) {
		ERRDRV("%s parent is NULL", __func__);
		return NULL;
	}
	if (parent->mapped == NULL) {
		ERRDRV("%s parent is not mapped!", __func__);
		return NULL;
	}
	if ((offset >= parent->nbytes) ||
	    ((offset + nbytes) >= parent->nbytes)) {
		ERRDRV("%s range (%lu,%lu) out of parent range",
		       __func__, offset, nbytes);
		return NULL;
	}
	memregion = kzalloc(sizeof(*memregion), GFP_KERNEL|__GFP_NORETRY);
	if (memregion == NULL) {
		ERRDRV("%s allocation failed", __func__);
		return NULL;
	}

	memregion->physaddr = parent->physaddr + offset;
	memregion->nbytes = nbytes;
	memregion->mapped = ((u8 __iomem *)(parent->mapped)) + offset;
	memregion->requested = FALSE;
	memregion->overlapped = TRUE;
	return memregion;
}
EXPORT_SYMBOL_GPL(visor_memregion_create_overlapped);

static BOOL
mapit(struct memregion *memregion)
{
	ulong physaddr = (ulong)(memregion->physaddr);
	ulong nbytes = memregion->nbytes;

	memregion->requested = FALSE;
	if (!request_mem_region(physaddr, nbytes, MYDRVNAME))
		ERRDRV("cannot reserve channel memory @0x%lx for 0x%lx-- no big deal",
		       physaddr, nbytes);
	else
		memregion->requested = TRUE;
	memregion->mapped = ioremap_cache(physaddr, nbytes);
	if (memregion->mapped == NULL) {
		ERRDRV("cannot ioremap_cache channel memory @0x%lx for 0x%lx",
		       physaddr, nbytes);
		return FALSE;
	}
	return TRUE;
}

static void
unmapit(struct memregion *memregion)
{
	if (memregion->mapped != NULL) {
		iounmap(memregion->mapped);
		memregion->mapped = NULL;
	}
	if (memregion->requested) {
		release_mem_region((ulong)(memregion->physaddr),
				   memregion->nbytes);
		memregion->requested = FALSE;
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
	if (newsize == memregion->nbytes)
		return 0;
	if (memregion->overlapped)
		/* no error check here - we no longer know the
		 * parent's range!
		 */
		memregion->nbytes = newsize;
	else {
		unmapit(memregion);
		memregion->nbytes = newsize;
		if (!mapit(memregion))
			return -1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(visor_memregion_resize);

static int
memregion_readwrite(BOOL is_write,
		    struct memregion *memregion, ulong offset,
		    void *local, ulong nbytes)
{
	if (offset + nbytes > memregion->nbytes) {
		ERRDRV("memregion_readwrite offset out of range!!");
		return -EIO;
	}
	if (is_write)
		memcpy_toio(memregion->mapped + offset, local, nbytes);
	else
		memcpy_fromio(local, memregion->mapped + offset, nbytes);

	return 0;
}

int
visor_memregion_read(struct memregion *memregion, ulong offset, void *dest,
		     ulong nbytes)
{
	return memregion_readwrite(FALSE, memregion, offset, dest, nbytes);
}
EXPORT_SYMBOL_GPL(visor_memregion_read);

int
visor_memregion_write(struct memregion *memregion, ulong offset, void *src,
		      ulong nbytes)
{
	return memregion_readwrite(TRUE, memregion, offset, src, nbytes);
}
EXPORT_SYMBOL_GPL(visor_memregion_write);

void
visor_memregion_destroy(struct memregion *memregion)
{
	if (memregion == NULL)
		return;
	if (!memregion->overlapped)
		unmapit(memregion);
	kfree(memregion);
}
EXPORT_SYMBOL_GPL(visor_memregion_destroy);

