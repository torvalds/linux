/* Copyright 2008 - 2016 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DPAA_SYS_H
#define __DPAA_SYS_H

#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/prefetch.h>
#include <linux/genalloc.h>
#include <asm/cacheflush.h>

/* For 2-element tables related to cache-inhibited and cache-enabled mappings */
#define DPAA_PORTAL_CE 0
#define DPAA_PORTAL_CI 1

#if (L1_CACHE_BYTES != 32) && (L1_CACHE_BYTES != 64)
#error "Unsupported Cacheline Size"
#endif

static inline void dpaa_flush(void *p)
{
#ifdef CONFIG_PPC
	flush_dcache_range((unsigned long)p, (unsigned long)p+64);
#elif defined(CONFIG_ARM32)
	__cpuc_flush_dcache_area(p, 64);
#elif defined(CONFIG_ARM64)
	__flush_dcache_area(p, 64);
#endif
}

#define dpaa_invalidate(p) dpaa_flush(p)

#define dpaa_zero(p) memset(p, 0, 64)

static inline void dpaa_touch_ro(void *p)
{
#if (L1_CACHE_BYTES == 32)
	prefetch(p+32);
#endif
	prefetch(p);
}

/* Commonly used combo */
static inline void dpaa_invalidate_touch_ro(void *p)
{
	dpaa_invalidate(p);
	dpaa_touch_ro(p);
}


#ifdef CONFIG_FSL_DPAA_CHECKING
#define DPAA_ASSERT(x) WARN_ON(!(x))
#else
#define DPAA_ASSERT(x)
#endif

/* cyclic helper for rings */
static inline u8 dpaa_cyc_diff(u8 ringsize, u8 first, u8 last)
{
	/* 'first' is included, 'last' is excluded */
	if (first <= last)
		return last - first;
	return ringsize + last - first;
}

/* Offset applied to genalloc pools due to zero being an error return */
#define DPAA_GENALLOC_OFF	0x80000000

#endif	/* __DPAA_SYS_H */
