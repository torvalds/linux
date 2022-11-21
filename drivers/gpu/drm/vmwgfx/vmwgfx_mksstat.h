/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright 2021 VMware, Inc., Palo Alto, CA., USA
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef _VMWGFX_MKSSTAT_H_
#define _VMWGFX_MKSSTAT_H_

#include <asm/page.h>

/* Reservation marker for mksstat pid's */
#define MKSSTAT_PID_RESERVED -1

#if IS_ENABLED(CONFIG_DRM_VMWGFX_MKSSTATS)
/*
 * Kernel-internal mksGuestStat counters. The order of this enum dictates the
 * order of instantiation of these counters in the mksGuestStat pages.
 */

typedef enum {
	MKSSTAT_KERN_EXECBUF, /* vmw_execbuf_ioctl */

	MKSSTAT_KERN_COUNT /* Reserved entry; always last */
} mksstat_kern_stats_t;

/**
 * vmw_mksstat_get_kern_pstat: Computes the address of the MKSGuestStatCounterTime
 * array from the address of the base page.
 *
 * @page_addr: Pointer to the base page.
 * Return: Pointer to the MKSGuestStatCounterTime array.
 */

static inline void *vmw_mksstat_get_kern_pstat(void *page_addr)
{
	return page_addr + PAGE_SIZE * 1;
}

/**
 * vmw_mksstat_get_kern_pinfo: Computes the address of the MKSGuestStatInfoEntry
 * array from the address of the base page.
 *
 * @page_addr: Pointer to the base page.
 * Return: Pointer to the MKSGuestStatInfoEntry array.
 */

static inline void *vmw_mksstat_get_kern_pinfo(void *page_addr)
{
	return page_addr + PAGE_SIZE * 2;
}

/**
 * vmw_mksstat_get_kern_pstrs: Computes the address of the mksGuestStat strings
 * sequence from the address of the base page.
 *
 * @page_addr: Pointer to the base page.
 * Return: Pointer to the mksGuestStat strings sequence.
 */

static inline void *vmw_mksstat_get_kern_pstrs(void *page_addr)
{
	return page_addr + PAGE_SIZE * 3;
}

/*
 * MKS_STAT_TIME_DECL/PUSH/POP macros to be used in timer-counted routines.
 */

struct mksstat_timer_t {
/* mutable */ mksstat_kern_stats_t old_top;
	const u64 t0;
	const int slot;
};

#define MKS_STAT_TIME_DECL(kern_cntr)                                     \
	struct mksstat_timer_t _##kern_cntr = {                           \
		.t0 = rdtsc(),                                            \
		.slot = vmw_mksstat_get_kern_slot(current->pid, dev_priv) \
	}

#define MKS_STAT_TIME_PUSH(kern_cntr)                                                               \
	do {                                                                                        \
		if (_##kern_cntr.slot >= 0) {                                                       \
			_##kern_cntr.old_top = dev_priv->mksstat_kern_top_timer[_##kern_cntr.slot]; \
			dev_priv->mksstat_kern_top_timer[_##kern_cntr.slot] = kern_cntr;            \
		}                                                                                   \
	} while (0)

#define MKS_STAT_TIME_POP(kern_cntr)                                                                                                           \
	do {                                                                                                                                   \
		if (_##kern_cntr.slot >= 0) {                                                                                                  \
			const pid_t pid = atomic_cmpxchg(&dev_priv->mksstat_kern_pids[_##kern_cntr.slot], current->pid, MKSSTAT_PID_RESERVED); \
			dev_priv->mksstat_kern_top_timer[_##kern_cntr.slot] = _##kern_cntr.old_top;                                            \
			                                                                                                                       \
			if (pid == current->pid) {                                                                                             \
				const u64 dt = rdtsc() - _##kern_cntr.t0;                                                                      \
				MKSGuestStatCounterTime *pstat;                                                                                \
				                                                                                                               \
				BUG_ON(!dev_priv->mksstat_kern_pages[_##kern_cntr.slot]);                                                      \
				                                                                                                               \
				pstat = vmw_mksstat_get_kern_pstat(page_address(dev_priv->mksstat_kern_pages[_##kern_cntr.slot]));             \
				                                                                                                               \
				atomic64_inc(&pstat[kern_cntr].counter.count);                                                                 \
				atomic64_add(dt, &pstat[kern_cntr].selfCycles);                                                                \
				atomic64_add(dt, &pstat[kern_cntr].totalCycles);                                                               \
				                                                                                                               \
				if (_##kern_cntr.old_top != MKSSTAT_KERN_COUNT)                                                                \
					atomic64_sub(dt, &pstat[_##kern_cntr.old_top].selfCycles);                                             \
					                                                                                                       \
				atomic_set(&dev_priv->mksstat_kern_pids[_##kern_cntr.slot], current->pid);                                     \
			}                                                                                                                      \
		}                                                                                                                              \
	} while (0)

#else
#define MKS_STAT_TIME_DECL(kern_cntr)
#define MKS_STAT_TIME_PUSH(kern_cntr)
#define MKS_STAT_TIME_POP(kern_cntr)

#endif /* IS_ENABLED(CONFIG_DRM_VMWGFX_MKSSTATS */

#endif
