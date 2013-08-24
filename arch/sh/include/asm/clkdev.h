/*
 *  Copyright (C) 2010 Paul Mundt <lethal@linux-sh.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Helper for the clk API to assist looking up a struct clk.
 */

#ifndef __CLKDEV__H_
#define __CLKDEV__H_

#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/clock.h>

static inline struct clk_lookup_alloc *__clkdev_alloc(size_t size)
{
	if (!slab_is_available())
		return alloc_bootmem_low_pages(size);
	else
		return kzalloc(size, GFP_KERNEL);
}

#ifndef CONFIG_COMMON_CLK
#define __clk_put(clk)
#define __clk_get(clk) ({ 1; })
#endif

#endif /* __CLKDEV_H__ */
