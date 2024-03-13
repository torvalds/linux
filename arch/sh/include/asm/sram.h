/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SRAM_H
#define __ASM_SRAM_H

#ifdef CONFIG_HAVE_SRAM_POOL

#include <linux/spinlock.h>
#include <linux/genalloc.h>

/* arch/sh/mm/sram.c */
extern struct gen_pool *sram_pool;

static inline unsigned long sram_alloc(size_t len)
{
	if (!sram_pool)
		return 0UL;

	return gen_pool_alloc(sram_pool, len);
}

static inline void sram_free(unsigned long addr, size_t len)
{
	return gen_pool_free(sram_pool, addr, len);
}

#else

static inline unsigned long sram_alloc(size_t len)
{
	return 0;
}

static inline void sram_free(unsigned long addr, size_t len)
{
}

#endif /* CONFIG_HAVE_SRAM_POOL */

#endif /* __ASM_SRAM_H */
