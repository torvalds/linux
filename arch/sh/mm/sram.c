/*
 * SRAM pool for tiny memories not otherwise managed.
 *
 * Copyright (C) 2010  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <asm/sram.h>

/*
 * This provides a standard SRAM pool for tiny memories that can be
 * added either by the CPU or the platform code. Typical SRAM sizes
 * to be inserted in to the pool will generally be less than the page
 * size, with anything more reasonably sized handled as a NUMA memory
 * node.
 */
struct gen_pool *sram_pool;

static int __init sram_pool_init(void)
{
	/*
	 * This is a global pool, we don't care about node locality.
	 */
	sram_pool = gen_pool_create(1, -1);
	if (unlikely(!sram_pool))
		return -ENOMEM;

	return 0;
}
core_initcall(sram_pool_init);
