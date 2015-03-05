/*
 * Copyright (C) 2010-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_L2_CACHE_H__
#define __MALI_KERNEL_L2_CACHE_H__

#include "mali_osk.h"
#include "mali_hw_core.h"

#define MALI_MAX_NUMBER_OF_L2_CACHE_CORES  3
/* Maximum 1 GP and 4 PP for an L2 cache core (Mali-400 MP4) */
#define MALI_MAX_NUMBER_OF_GROUPS_PER_L2_CACHE 5

/**
 * Definition of the L2 cache core struct
 * Used to track a L2 cache unit in the system.
 * Contains information about the mapping of the registers
 */
struct mali_l2_cache_core {
	/* Common HW core functionality */
	struct mali_hw_core hw_core;

	/* Synchronize L2 cache access */
	_mali_osk_spinlock_irq_t *lock;

	/* Unique core ID */
	u32 core_id;

	/* The power domain this L2 cache belongs to */
	struct mali_pm_domain *pm_domain;

	/* MALI_TRUE if power is on for this L2 cache */
	mali_bool power_is_on;

	/* A "timestamp" to avoid unnecessary flushes */
	u32 last_invalidated_id;

	/* Performance counter 0, MALI_HW_CORE_NO_COUNTER for disabled */
	u32 counter_src0;

	/* Performance counter 1, MALI_HW_CORE_NO_COUNTER for disabled */
	u32 counter_src1;

	/*
	 * Performance counter 0 value base/offset
	 * (allows accumulative reporting even after power off)
	 */
	u32 counter_value0_base;

	/*
	 * Performance counter 0 value base/offset
	 * (allows accumulative reporting even after power off)
	 */
	u32 counter_value1_base;

	/* Used by PM domains to link L2 caches of same domain */
	_mali_osk_list_t pm_domain_list;
};

_mali_osk_errcode_t mali_l2_cache_initialize(void);
void mali_l2_cache_terminate(void);

struct mali_l2_cache_core *mali_l2_cache_create(
	_mali_osk_resource_t *resource, u32 domain_index);
void mali_l2_cache_delete(struct mali_l2_cache_core *cache);

MALI_STATIC_INLINE u32 mali_l2_cache_get_id(struct mali_l2_cache_core *cache)
{
	MALI_DEBUG_ASSERT_POINTER(cache);
	return cache->core_id;
}

MALI_STATIC_INLINE struct mali_pm_domain *mali_l2_cache_get_pm_domain(
	struct mali_l2_cache_core *cache)
{
	MALI_DEBUG_ASSERT_POINTER(cache);
	return cache->pm_domain;
}

void mali_l2_cache_power_up(struct mali_l2_cache_core *cache);
void mali_l2_cache_power_down(struct mali_l2_cache_core *cache);

void mali_l2_cache_core_set_counter_src(
	struct mali_l2_cache_core *cache, u32 source_id, u32 counter);

MALI_STATIC_INLINE u32 mali_l2_cache_core_get_counter_src0(
	struct mali_l2_cache_core *cache)
{
	MALI_DEBUG_ASSERT_POINTER(cache);
	return cache->counter_src0;
}

MALI_STATIC_INLINE u32 mali_l2_cache_core_get_counter_src1(
	struct mali_l2_cache_core *cache)
{
	MALI_DEBUG_ASSERT_POINTER(cache);
	return cache->counter_src1;
}

void mali_l2_cache_core_get_counter_values(
	struct mali_l2_cache_core *cache,
	u32 *src0, u32 *value0, u32 *src1, u32 *value1);

struct mali_l2_cache_core *mali_l2_cache_core_get_glob_l2_core(u32 index);
u32 mali_l2_cache_core_get_glob_num_l2_cores(void);

struct mali_group *mali_l2_cache_get_group(
	struct mali_l2_cache_core *cache, u32 index);

void mali_l2_cache_invalidate(struct mali_l2_cache_core *cache);
void mali_l2_cache_invalidate_conditional(
	struct mali_l2_cache_core *cache, u32 id);

void mali_l2_cache_invalidate_all(void);
void mali_l2_cache_invalidate_all_pages(u32 *pages, u32 num_pages);

#endif /* __MALI_KERNEL_L2_CACHE_H__ */
