/*
 * SPU local store allocation routines
 *
 * Copyright 2007 Benjamin Herrenschmidt, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
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

#undef DEBUG

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <asm/spu.h>
#include <asm/spu_csa.h>
#include <asm/mmu.h>

#include "spufs.h"

int spu_alloc_lscsa(struct spu_state *csa)
{
	struct spu_lscsa *lscsa;
	unsigned char *p;

	lscsa = vzalloc(sizeof(*lscsa));
	if (!lscsa)
		return -ENOMEM;
	csa->lscsa = lscsa;

	/* Set LS pages reserved to allow for user-space mapping. */
	for (p = lscsa->ls; p < lscsa->ls + LS_SIZE; p += PAGE_SIZE)
		SetPageReserved(vmalloc_to_page(p));

	return 0;
}

void spu_free_lscsa(struct spu_state *csa)
{
	/* Clear reserved bit before vfree. */
	unsigned char *p;

	if (csa->lscsa == NULL)
		return;

	for (p = csa->lscsa->ls; p < csa->lscsa->ls + LS_SIZE; p += PAGE_SIZE)
		ClearPageReserved(vmalloc_to_page(p));

	vfree(csa->lscsa);
}
