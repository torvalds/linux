/*
 * SRAM protect-exec region helper functions
 *
 * Copyright (C) 2017 Texas Instruments Incorporated - http://www.ti.com/
 *	Dave Gerlach
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/genalloc.h>
#include <linux/mm.h>
#include <linux/sram.h>

#include <asm/fncpy.h>
#include <asm/set_memory.h>

#include "sram.h"

static DEFINE_MUTEX(exec_pool_list_mutex);
static LIST_HEAD(exec_pool_list);

int sram_check_protect_exec(struct sram_dev *sram, struct sram_reserve *block,
			    struct sram_partition *part)
{
	unsigned long base = (unsigned long)part->base;
	unsigned long end = base + block->size;

	if (!PAGE_ALIGNED(base) || !PAGE_ALIGNED(end)) {
		dev_err(sram->dev,
			"SRAM pool marked with 'protect-exec' is not page aligned and will not be created.\n");
		return -ENOMEM;
	}

	return 0;
}

int sram_add_protect_exec(struct sram_partition *part)
{
	mutex_lock(&exec_pool_list_mutex);
	list_add_tail(&part->list, &exec_pool_list);
	mutex_unlock(&exec_pool_list_mutex);

	return 0;
}

/**
 * sram_exec_copy - copy data to a protected executable region of sram
 *
 * @pool: struct gen_pool retrieved that is part of this sram
 * @dst: Destination address for the copy, that must be inside pool
 * @src: Source address for the data to copy
 * @size: Size of copy to perform, which starting from dst, must reside in pool
 *
 * Return: Address for copied data that can safely be called through function
 *	   pointer, or NULL if problem.
 *
 * This helper function allows sram driver to act as central control location
 * of 'protect-exec' pools which are normal sram pools but are always set
 * read-only and executable except when copying data to them, at which point
 * they are set to read-write non-executable, to make sure no memory is
 * writeable and executable at the same time. This region must be page-aligned
 * and is checked during probe, otherwise page attribute manipulation would
 * not be possible. Care must be taken to only call the returned address as
 * dst address is not guaranteed to be safely callable.
 *
 * NOTE: This function uses the fncpy macro to move code to the executable
 * region. Some architectures have strict requirements for relocating
 * executable code, so fncpy is a macro that must be defined by any arch
 * making use of this functionality that guarantees a safe copy of exec
 * data and returns a safe address that can be called as a C function
 * pointer.
 */
void *sram_exec_copy(struct gen_pool *pool, void *dst, void *src,
		     size_t size)
{
	struct sram_partition *part = NULL, *p;
	unsigned long base;
	int pages;
	void *dst_cpy;

	mutex_lock(&exec_pool_list_mutex);
	list_for_each_entry(p, &exec_pool_list, list) {
		if (p->pool == pool)
			part = p;
	}
	mutex_unlock(&exec_pool_list_mutex);

	if (!part)
		return NULL;

	if (!addr_in_gen_pool(pool, (unsigned long)dst, size))
		return NULL;

	base = (unsigned long)part->base;
	pages = PAGE_ALIGN(size) / PAGE_SIZE;

	mutex_lock(&part->lock);

	set_memory_nx((unsigned long)base, pages);
	set_memory_rw((unsigned long)base, pages);

	dst_cpy = fncpy(dst, src, size);

	set_memory_ro((unsigned long)base, pages);
	set_memory_x((unsigned long)base, pages);

	mutex_unlock(&part->lock);

	return dst_cpy;
}
EXPORT_SYMBOL_GPL(sram_exec_copy);
