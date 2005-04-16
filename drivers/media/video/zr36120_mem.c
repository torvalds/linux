/*
    zr36120_mem.c - Zoran 36120/36125 based framegrabbers

    Copyright (C) 1998-1999 Pauline Middelink <middelin@polyware.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <asm/io.h>
#ifdef CONFIG_BIGPHYS_AREA
#include <linux/bigphysarea.h>
#endif

#include "zr36120.h"
#include "zr36120_mem.h"

/*******************************/
/* Memory management functions */
/*******************************/

void* bmalloc(unsigned long size)
{
	void* mem;
#ifdef CONFIG_BIGPHYS_AREA
	mem = bigphysarea_alloc_pages(size/PAGE_SIZE, 1, GFP_KERNEL);
#else
	/*
	 * The following function got a lot of memory at boottime,
	 * so we know its always there...
	 */
	mem = (void*)__get_free_pages(GFP_USER|GFP_DMA,get_order(size));
#endif
	if (mem) {
		unsigned long adr = (unsigned long)mem;
		while (size > 0) {
			SetPageReserved(virt_to_page(phys_to_virt(adr)));
			adr += PAGE_SIZE;
			size -= PAGE_SIZE;
		}
	}
	return mem;
}

void bfree(void* mem, unsigned long size)
{
	if (mem) {
		unsigned long adr = (unsigned long)mem;
		unsigned long siz = size;
		while (siz > 0) {
			ClearPageReserved(virt_to_page(phys_to_virt(adr)));
			adr += PAGE_SIZE;
			siz -= PAGE_SIZE;
		}
#ifdef CONFIG_BIGPHYS_AREA
		bigphysarea_free_pages(mem);
#else
		free_pages((unsigned long)mem,get_order(size));
#endif
	}
}

MODULE_LICENSE("GPL");
