/*
 * arch/arm/include/asm/outercache.h
 *
 * Copyright (C) 2010 ARM Ltd.
 * Written by Catalin Marinas <catalin.marinas@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __ASM_OUTERCACHE_H
#define __ASM_OUTERCACHE_H

struct outer_cache_fns {
	void (*inv_range)(unsigned long, unsigned long);
	void (*clean_range)(unsigned long, unsigned long);
	void (*flush_range)(unsigned long, unsigned long);
	void (*flush_all)(void);
	void (*inv_all)(void);
	void (*disable)(void);
#ifdef CONFIG_OUTER_CACHE_SYNC
	void (*sync)(void);
#endif
	void (*set_debug)(unsigned long);
};

#ifdef CONFIG_OUTER_CACHE

extern struct outer_cache_fns outer_cache;

static inline void outer_inv_range(unsigned long start, unsigned long end)
{
	if (outer_cache.inv_range)
		outer_cache.inv_range(start, end);
}
static inline void outer_clean_range(unsigned long start, unsigned long end)
{
	if (outer_cache.clean_range)
		outer_cache.clean_range(start, end);
}
static inline void outer_flush_range(unsigned long start, unsigned long end)
{
	if (outer_cache.flush_range)
		outer_cache.flush_range(start, end);
}

static inline void outer_flush_all(void)
{
	if (outer_cache.flush_all)
		outer_cache.flush_all();
}

static inline void outer_inv_all(void)
{
	if (outer_cache.inv_all)
		outer_cache.inv_all();
}

static inline void outer_disable(void)
{
	if (outer_cache.disable)
		outer_cache.disable();
}

#else

static inline void outer_inv_range(unsigned long start, unsigned long end)
{ }
static inline void outer_clean_range(unsigned long start, unsigned long end)
{ }
static inline void outer_flush_range(unsigned long start, unsigned long end)
{ }
static inline void outer_flush_all(void) { }
static inline void outer_inv_all(void) { }
static inline void outer_disable(void) { }

#endif

#ifdef CONFIG_OUTER_CACHE_SYNC
static inline void outer_sync(void)
{
	if (outer_cache.sync)
		outer_cache.sync();
}
#else
static inline void outer_sync(void)
{ }
#endif

#endif	/* __ASM_OUTERCACHE_H */
