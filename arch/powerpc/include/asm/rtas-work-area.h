/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_POWERPC_RTAS_WORK_AREA_H
#define _ASM_POWERPC_RTAS_WORK_AREA_H

#include <linux/build_bug.h>
#include <linux/sizes.h>
#include <linux/types.h>

#include <asm/page.h>

/**
 * struct rtas_work_area - RTAS work area descriptor.
 *
 * Descriptor for a "work area" in PAPR terminology that satisfies
 * RTAS addressing requirements.
 */
struct rtas_work_area {
	/* private: Use the APIs provided below. */
	char *buf;
	size_t size;
};

enum {
	/* Maximum allocation size, enforced at build time. */
	RTAS_WORK_AREA_MAX_ALLOC_SZ = SZ_128K,
};

/**
 * rtas_work_area_alloc() - Acquire a work area of the requested size.
 * @size_: Allocation size. Must be compile-time constant and not more
 *         than %RTAS_WORK_AREA_MAX_ALLOC_SZ.
 *
 * Allocate a buffer suitable for passing to RTAS functions that have
 * a memory address parameter, often (but not always) referred to as a
 * "work area" in PAPR. Although callers are allowed to block while
 * holding a work area, the amount of memory reserved for this purpose
 * is limited, and allocations should be short-lived. A good guideline
 * is to release any allocated work area before returning from a
 * system call.
 *
 * This function does not fail. It blocks until the allocation
 * succeeds. To prevent deadlocks, callers are discouraged from
 * allocating more than one work area simultaneously in a single task
 * context.
 *
 * Context: This function may sleep.
 * Return: A &struct rtas_work_area descriptor for the allocated work area.
 */
#define rtas_work_area_alloc(size_) ({				\
	static_assert(__builtin_constant_p(size_));		\
	static_assert((size_) > 0);				\
	static_assert((size_) <= RTAS_WORK_AREA_MAX_ALLOC_SZ);	\
	__rtas_work_area_alloc(size_);				\
})

/*
 * Do not call __rtas_work_area_alloc() directly. Use
 * rtas_work_area_alloc().
 */
struct rtas_work_area *__rtas_work_area_alloc(size_t size);

/**
 * rtas_work_area_free() - Release a work area.
 * @area: Work area descriptor as returned from rtas_work_area_alloc().
 *
 * Return a work area buffer to the pool.
 */
void rtas_work_area_free(struct rtas_work_area *area);

static inline char *rtas_work_area_raw_buf(const struct rtas_work_area *area)
{
	return area->buf;
}

static inline size_t rtas_work_area_size(const struct rtas_work_area *area)
{
	return area->size;
}

static inline phys_addr_t rtas_work_area_phys(const struct rtas_work_area *area)
{
	return __pa(area->buf);
}

/*
 * Early setup for the work area allocator. Call from
 * rtas_initialize() only.
 */

#ifdef CONFIG_PPC_PSERIES
void rtas_work_area_reserve_arena(phys_addr_t limit);
#else /* CONFIG_PPC_PSERIES */
static inline void rtas_work_area_reserve_arena(phys_addr_t limit) {}
#endif /* CONFIG_PPC_PSERIES */

#endif /* _ASM_POWERPC_RTAS_WORK_AREA_H */
