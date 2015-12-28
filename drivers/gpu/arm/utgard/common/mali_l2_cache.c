/*
 * Copyright (C) 2010-2015 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_l2_cache.h"
#include "mali_hw_core.h"
#include "mali_scheduler.h"
#include "mali_pm.h"
#include "mali_pm_domain.h"

/**
 * Size of the Mali L2 cache registers in bytes
 */
#define MALI400_L2_CACHE_REGISTERS_SIZE 0x30

/**
 * Mali L2 cache register numbers
 * Used in the register read/write routines.
 * See the hardware documentation for more information about each register
 */
typedef enum mali_l2_cache_register {
	MALI400_L2_CACHE_REGISTER_SIZE         = 0x0004,
	MALI400_L2_CACHE_REGISTER_STATUS       = 0x0008,
	/*unused                               = 0x000C */
	MALI400_L2_CACHE_REGISTER_COMMAND      = 0x0010,
	MALI400_L2_CACHE_REGISTER_CLEAR_PAGE   = 0x0014,
	MALI400_L2_CACHE_REGISTER_MAX_READS    = 0x0018,
	MALI400_L2_CACHE_REGISTER_ENABLE       = 0x001C,
	MALI400_L2_CACHE_REGISTER_PERFCNT_SRC0 = 0x0020,
	MALI400_L2_CACHE_REGISTER_PERFCNT_VAL0 = 0x0024,
	MALI400_L2_CACHE_REGISTER_PERFCNT_SRC1 = 0x0028,
	MALI400_L2_CACHE_REGISTER_PERFCNT_VAL1 = 0x002C,
} mali_l2_cache_register;

/**
 * Mali L2 cache commands
 * These are the commands that can be sent to the Mali L2 cache unit
 */
typedef enum mali_l2_cache_command {
	MALI400_L2_CACHE_COMMAND_CLEAR_ALL = 0x01,
} mali_l2_cache_command;

/**
 * Mali L2 cache commands
 * These are the commands that can be sent to the Mali L2 cache unit
 */
typedef enum mali_l2_cache_enable {
	MALI400_L2_CACHE_ENABLE_DEFAULT = 0x0, /* Default */
	MALI400_L2_CACHE_ENABLE_ACCESS = 0x01,
	MALI400_L2_CACHE_ENABLE_READ_ALLOCATE = 0x02,
} mali_l2_cache_enable;

/**
 * Mali L2 cache status bits
 */
typedef enum mali_l2_cache_status {
	MALI400_L2_CACHE_STATUS_COMMAND_BUSY = 0x01,
	MALI400_L2_CACHE_STATUS_DATA_BUSY    = 0x02,
} mali_l2_cache_status;

#define MALI400_L2_MAX_READS_NOT_SET -1

static struct mali_l2_cache_core *
	mali_global_l2s[MALI_MAX_NUMBER_OF_L2_CACHE_CORES] = { NULL, };
static u32 mali_global_num_l2s = 0;

int mali_l2_max_reads = MALI400_L2_MAX_READS_NOT_SET;


/* Local helper functions */

static void mali_l2_cache_reset(struct mali_l2_cache_core *cache);

static _mali_osk_errcode_t mali_l2_cache_send_command(
	struct mali_l2_cache_core *cache, u32 reg, u32 val);

static void mali_l2_cache_lock(struct mali_l2_cache_core *cache)
{
	MALI_DEBUG_ASSERT_POINTER(cache);
	_mali_osk_spinlock_irq_lock(cache->lock);
}

static void mali_l2_cache_unlock(struct mali_l2_cache_core *cache)
{
	MALI_DEBUG_ASSERT_POINTER(cache);
	_mali_osk_spinlock_irq_unlock(cache->lock);
}

/* Implementation of the L2 cache interface */

struct mali_l2_cache_core *mali_l2_cache_create(
	_mali_osk_resource_t *resource, u32 domain_index)
{
	struct mali_l2_cache_core *cache = NULL;
#if defined(DEBUG)
	u32 cache_size;
#endif

	MALI_DEBUG_PRINT(4, ("Mali L2 cache: Creating Mali L2 cache: %s\n",
			     resource->description));

	if (mali_global_num_l2s >= MALI_MAX_NUMBER_OF_L2_CACHE_CORES) {
		MALI_PRINT_ERROR(("Mali L2 cache: Too many L2 caches\n"));
		return NULL;
	}

	cache = _mali_osk_malloc(sizeof(struct mali_l2_cache_core));
	if (NULL == cache) {
		MALI_PRINT_ERROR(("Mali L2 cache: Failed to allocate memory for L2 cache core\n"));
		return NULL;
	}

	cache->core_id =  mali_global_num_l2s;
	cache->counter_src0 = MALI_HW_CORE_NO_COUNTER;
	cache->counter_src1 = MALI_HW_CORE_NO_COUNTER;
	cache->counter_value0_base = 0;
	cache->counter_value1_base = 0;
	cache->pm_domain = NULL;
	cache->power_is_on = MALI_FALSE;
	cache->last_invalidated_id = 0;

	if (_MALI_OSK_ERR_OK != mali_hw_core_create(&cache->hw_core,
			resource, MALI400_L2_CACHE_REGISTERS_SIZE)) {
		_mali_osk_free(cache);
		return NULL;
	}

#if defined(DEBUG)
	cache_size = mali_hw_core_register_read(&cache->hw_core,
						MALI400_L2_CACHE_REGISTER_SIZE);
	MALI_DEBUG_PRINT(2, ("Mali L2 cache: Created %s: % 3uK, %u-way, % 2ubyte cache line, % 3ubit external bus\n",
			     resource->description,
			     1 << (((cache_size >> 16) & 0xff) - 10),
			     1 << ((cache_size >> 8) & 0xff),
			     1 << (cache_size & 0xff),
			     1 << ((cache_size >> 24) & 0xff)));
#endif

	cache->lock = _mali_osk_spinlock_irq_init(_MALI_OSK_LOCKFLAG_ORDERED,
			_MALI_OSK_LOCK_ORDER_L2);
	if (NULL == cache->lock) {
		MALI_PRINT_ERROR(("Mali L2 cache: Failed to create counter lock for L2 cache core %s\n",
				  cache->hw_core.description));
		mali_hw_core_delete(&cache->hw_core);
		_mali_osk_free(cache);
		return NULL;
	}

	/* register with correct power domain */
	cache->pm_domain = mali_pm_register_l2_cache(
				   domain_index, cache);

	mali_global_l2s[mali_global_num_l2s] = cache;
	mali_global_num_l2s++;

	return cache;
}

void mali_l2_cache_delete(struct mali_l2_cache_core *cache)
{
	u32 i;
	for (i = 0; i < mali_global_num_l2s; i++) {
		if (mali_global_l2s[i] != cache) {
			continue;
		}

		mali_global_l2s[i] = NULL;
		mali_global_num_l2s--;

		if (i == mali_global_num_l2s) {
			/* Removed last element, nothing more to do */
			break;
		}

		/*
		 * We removed a l2 cache from the middle of the array,
		 * so move the last l2 cache to current position
		 */
		mali_global_l2s[i] = mali_global_l2s[mali_global_num_l2s];
		mali_global_l2s[mali_global_num_l2s] = NULL;

		/* All good */
		break;
	}

	_mali_osk_spinlock_irq_term(cache->lock);
	mali_hw_core_delete(&cache->hw_core);
	_mali_osk_free(cache);
}

void mali_l2_cache_power_up(struct mali_l2_cache_core *cache)
{
	MALI_DEBUG_ASSERT_POINTER(cache);

	mali_l2_cache_lock(cache);

	mali_l2_cache_reset(cache);

	if ((1 << MALI_DOMAIN_INDEX_DUMMY) != cache->pm_domain->pmu_mask)
		MALI_DEBUG_ASSERT(MALI_FALSE == cache->power_is_on);
	cache->power_is_on = MALI_TRUE;

	mali_l2_cache_unlock(cache);
}

void mali_l2_cache_power_down(struct mali_l2_cache_core *cache)
{
	MALI_DEBUG_ASSERT_POINTER(cache);

	mali_l2_cache_lock(cache);

	MALI_DEBUG_ASSERT(MALI_TRUE == cache->power_is_on);

	/*
	 * The HW counters will start from zero again when we resume,
	 * but we should report counters as always increasing.
	 * Take a copy of the HW values now in order to add this to
	 * the values we report after being powered up.
	 *
	 * The physical power off of the L2 cache might be outside our
	 * own control (e.g. runtime PM). That is why we must manually
	 * set set the counter value to zero as well.
	 */

	if (cache->counter_src0 != MALI_HW_CORE_NO_COUNTER) {
		cache->counter_value0_base += mali_hw_core_register_read(
						      &cache->hw_core,
						      MALI400_L2_CACHE_REGISTER_PERFCNT_VAL0);
		mali_hw_core_register_write(&cache->hw_core,
					    MALI400_L2_CACHE_REGISTER_PERFCNT_VAL0, 0);
	}

	if (cache->counter_src1 != MALI_HW_CORE_NO_COUNTER) {
		cache->counter_value1_base += mali_hw_core_register_read(
						      &cache->hw_core,
						      MALI400_L2_CACHE_REGISTER_PERFCNT_VAL1);
		mali_hw_core_register_write(&cache->hw_core,
					    MALI400_L2_CACHE_REGISTER_PERFCNT_VAL1, 0);
	}


	cache->power_is_on = MALI_FALSE;

	mali_l2_cache_unlock(cache);
}

void mali_l2_cache_core_set_counter_src(
	struct mali_l2_cache_core *cache, u32 source_id, u32 counter)
{
	u32 reg_offset_src;
	u32 reg_offset_val;

	MALI_DEBUG_ASSERT_POINTER(cache);
	MALI_DEBUG_ASSERT(source_id >= 0 && source_id <= 1);

	mali_l2_cache_lock(cache);

	if (0 == source_id) {
		/* start counting from 0 */
		cache->counter_value0_base = 0;
		cache->counter_src0 = counter;
		reg_offset_src = MALI400_L2_CACHE_REGISTER_PERFCNT_SRC0;
		reg_offset_val = MALI400_L2_CACHE_REGISTER_PERFCNT_VAL0;
	} else {
		/* start counting from 0 */
		cache->counter_value1_base = 0;
		cache->counter_src1 = counter;
		reg_offset_src = MALI400_L2_CACHE_REGISTER_PERFCNT_SRC1;
		reg_offset_val = MALI400_L2_CACHE_REGISTER_PERFCNT_VAL1;
	}

	if (cache->power_is_on) {
		u32 hw_src;

		if (MALI_HW_CORE_NO_COUNTER != counter) {
			hw_src = counter;
		} else {
			hw_src = 0; /* disable value for HW */
		}

		/* Set counter src */
		mali_hw_core_register_write(&cache->hw_core,
					    reg_offset_src, hw_src);

		/* Make sure the HW starts counting from 0 again */
		mali_hw_core_register_write(&cache->hw_core,
					    reg_offset_val, 0);
	}

	mali_l2_cache_unlock(cache);
}

void mali_l2_cache_core_get_counter_values(
	struct mali_l2_cache_core *cache,
	u32 *src0, u32 *value0, u32 *src1, u32 *value1)
{
	MALI_DEBUG_ASSERT_POINTER(cache);
	MALI_DEBUG_ASSERT(NULL != src0);
	MALI_DEBUG_ASSERT(NULL != value0);
	MALI_DEBUG_ASSERT(NULL != src1);
	MALI_DEBUG_ASSERT(NULL != value1);

	mali_l2_cache_lock(cache);

	*src0 = cache->counter_src0;
	*src1 = cache->counter_src1;

	if (cache->counter_src0 != MALI_HW_CORE_NO_COUNTER) {
		if (MALI_TRUE == cache->power_is_on) {
			*value0 = mali_hw_core_register_read(&cache->hw_core,
							     MALI400_L2_CACHE_REGISTER_PERFCNT_VAL0);
		} else {
			*value0 = 0;
		}

		/* Add base offset value (in case we have been power off) */
		*value0 += cache->counter_value0_base;
	}

	if (cache->counter_src1 != MALI_HW_CORE_NO_COUNTER) {
		if (MALI_TRUE == cache->power_is_on) {
			*value1 = mali_hw_core_register_read(&cache->hw_core,
							     MALI400_L2_CACHE_REGISTER_PERFCNT_VAL1);
		} else {
			*value1 = 0;
		}

		/* Add base offset value (in case we have been power off) */
		*value1 += cache->counter_value1_base;
	}

	mali_l2_cache_unlock(cache);
}

struct mali_l2_cache_core *mali_l2_cache_core_get_glob_l2_core(u32 index)
{
	if (mali_global_num_l2s > index) {
		return mali_global_l2s[index];
	}

	return NULL;
}

u32 mali_l2_cache_core_get_glob_num_l2_cores(void)
{
	return mali_global_num_l2s;
}

void mali_l2_cache_invalidate(struct mali_l2_cache_core *cache)
{
	MALI_DEBUG_ASSERT_POINTER(cache);

	if (NULL == cache) {
		return;
	}

	mali_l2_cache_lock(cache);

	cache->last_invalidated_id = mali_scheduler_get_new_cache_order();
	mali_l2_cache_send_command(cache, MALI400_L2_CACHE_REGISTER_COMMAND,
				   MALI400_L2_CACHE_COMMAND_CLEAR_ALL);

	mali_l2_cache_unlock(cache);
}

void mali_l2_cache_invalidate_conditional(
	struct mali_l2_cache_core *cache, u32 id)
{
	MALI_DEBUG_ASSERT_POINTER(cache);

	if (NULL == cache) {
		return;
	}

	/*
	 * If the last cache invalidation was done by a job with a higher id we
	 * don't have to flush. Since user space will store jobs w/ their
	 * corresponding memory in sequence (first job #0, then job #1, ...),
	 * we don't have to flush for job n-1 if job n has already invalidated
	 * the cache since we know for sure that job n-1's memory was already
	 * written when job n was started.
	 */

	mali_l2_cache_lock(cache);

	if (((s32)id) > ((s32)cache->last_invalidated_id)) {
		/* Set latest invalidated id to current "point in time" */
		cache->last_invalidated_id =
			mali_scheduler_get_new_cache_order();
		mali_l2_cache_send_command(cache,
					   MALI400_L2_CACHE_REGISTER_COMMAND,
					   MALI400_L2_CACHE_COMMAND_CLEAR_ALL);
	}

	mali_l2_cache_unlock(cache);
}

void mali_l2_cache_invalidate_all(void)
{
	u32 i;
	for (i = 0; i < mali_global_num_l2s; i++) {
		struct mali_l2_cache_core *cache = mali_global_l2s[i];
		_mali_osk_errcode_t ret;

		MALI_DEBUG_ASSERT_POINTER(cache);

		mali_l2_cache_lock(cache);

		if (MALI_TRUE != cache->power_is_on) {
			mali_l2_cache_unlock(cache);
			continue;
		}

		cache->last_invalidated_id =
			mali_scheduler_get_new_cache_order();

		ret = mali_l2_cache_send_command(cache,
						 MALI400_L2_CACHE_REGISTER_COMMAND,
						 MALI400_L2_CACHE_COMMAND_CLEAR_ALL);
		if (_MALI_OSK_ERR_OK != ret) {
			MALI_PRINT_ERROR(("Failed to invalidate cache\n"));
		}

		mali_l2_cache_unlock(cache);
	}
}

void mali_l2_cache_invalidate_all_pages(u32 *pages, u32 num_pages)
{
	u32 i;
	for (i = 0; i < mali_global_num_l2s; i++) {
		struct mali_l2_cache_core *cache = mali_global_l2s[i];
		u32 j;

		MALI_DEBUG_ASSERT_POINTER(cache);

		mali_l2_cache_lock(cache);

		if (MALI_TRUE != cache->power_is_on) {
			mali_l2_cache_unlock(cache);
			continue;
		}

		for (j = 0; j < num_pages; j++) {
			_mali_osk_errcode_t ret;

			ret = mali_l2_cache_send_command(cache,
							 MALI400_L2_CACHE_REGISTER_CLEAR_PAGE,
							 pages[j]);
			if (_MALI_OSK_ERR_OK != ret) {
				MALI_PRINT_ERROR(("Failed to invalidate cache (page)\n"));
			}
		}

		mali_l2_cache_unlock(cache);
	}
}

/* -------- local helper functions below -------- */

static void mali_l2_cache_reset(struct mali_l2_cache_core *cache)
{
	MALI_DEBUG_ASSERT_POINTER(cache);
	MALI_DEBUG_ASSERT_LOCK_HELD(cache->lock);

	/* Invalidate cache (just to keep it in a known state at startup) */
	mali_l2_cache_send_command(cache, MALI400_L2_CACHE_REGISTER_COMMAND,
				   MALI400_L2_CACHE_COMMAND_CLEAR_ALL);

	/* Enable cache */
	mali_hw_core_register_write(&cache->hw_core,
				    MALI400_L2_CACHE_REGISTER_ENABLE,
				    (u32)MALI400_L2_CACHE_ENABLE_ACCESS |
				    (u32)MALI400_L2_CACHE_ENABLE_READ_ALLOCATE);

	if (MALI400_L2_MAX_READS_NOT_SET != mali_l2_max_reads) {
		mali_hw_core_register_write(&cache->hw_core,
					    MALI400_L2_CACHE_REGISTER_MAX_READS,
					    (u32)mali_l2_max_reads);
	}

	/* Restart any performance counters (if enabled) */
	if (cache->counter_src0 != MALI_HW_CORE_NO_COUNTER) {

		mali_hw_core_register_write(&cache->hw_core,
					    MALI400_L2_CACHE_REGISTER_PERFCNT_SRC0,
					    cache->counter_src0);
	}

	if (cache->counter_src1 != MALI_HW_CORE_NO_COUNTER) {
		mali_hw_core_register_write(&cache->hw_core,
					    MALI400_L2_CACHE_REGISTER_PERFCNT_SRC1,
					    cache->counter_src1);
	}
}

static _mali_osk_errcode_t mali_l2_cache_send_command(
	struct mali_l2_cache_core *cache, u32 reg, u32 val)
{
	int i = 0;
	const int loop_count = 100000;

	MALI_DEBUG_ASSERT_POINTER(cache);
	MALI_DEBUG_ASSERT_LOCK_HELD(cache->lock);

	/*
	 * First, wait for L2 cache command handler to go idle.
	 * (Commands received while processing another command will be ignored)
	 */
	for (i = 0; i < loop_count; i++) {
		if (!(mali_hw_core_register_read(&cache->hw_core,
						 MALI400_L2_CACHE_REGISTER_STATUS) &
		      (u32)MALI400_L2_CACHE_STATUS_COMMAND_BUSY)) {
			break;
		}
	}

	if (i == loop_count) {
		MALI_DEBUG_PRINT(1, ("Mali L2 cache: aborting wait for command interface to go idle\n"));
		return _MALI_OSK_ERR_FAULT;
	}

	/* then issue the command */
	mali_hw_core_register_write(&cache->hw_core, reg, val);

	return _MALI_OSK_ERR_OK;
}
