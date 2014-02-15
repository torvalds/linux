/*
 * Copyright (C) 2010-2013 ARM Limited. All rights reserved.
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
	MALI400_L2_CACHE_REGISTER_COMMAND      = 0x0010, /**< Misc cache commands, e.g. clear */
	MALI400_L2_CACHE_REGISTER_CLEAR_PAGE   = 0x0014,
	MALI400_L2_CACHE_REGISTER_MAX_READS    = 0x0018, /**< Limit of outstanding read requests */
	MALI400_L2_CACHE_REGISTER_ENABLE       = 0x001C, /**< Enable misc cache features */
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
	MALI400_L2_CACHE_COMMAND_CLEAR_ALL = 0x01, /**< Clear the entire cache */
	/* Read HW TRM carefully before adding/using other commands than the clear above */
} mali_l2_cache_command;

/**
 * Mali L2 cache commands
 * These are the commands that can be sent to the Mali L2 cache unit
 */
typedef enum mali_l2_cache_enable {
	MALI400_L2_CACHE_ENABLE_DEFAULT = 0x0, /**< Default state of enable register */
	MALI400_L2_CACHE_ENABLE_ACCESS = 0x01, /**< Permit cacheable accesses */
	MALI400_L2_CACHE_ENABLE_READ_ALLOCATE = 0x02, /**< Permit cache read allocate */
} mali_l2_cache_enable;

/**
 * Mali L2 cache status bits
 */
typedef enum mali_l2_cache_status {
	MALI400_L2_CACHE_STATUS_COMMAND_BUSY = 0x01, /**< Command handler of L2 cache is busy */
	MALI400_L2_CACHE_STATUS_DATA_BUSY    = 0x02, /**< L2 cache is busy handling data requests */
} mali_l2_cache_status;

#define MALI400_L2_MAX_READS_DEFAULT 0x1C

static struct mali_l2_cache_core *mali_global_l2_cache_cores[MALI_MAX_NUMBER_OF_L2_CACHE_CORES] = { NULL, };
static u32 mali_global_num_l2_cache_cores = 0;

int mali_l2_max_reads = MALI400_L2_MAX_READS_DEFAULT;


/* Local helper functions */
static _mali_osk_errcode_t mali_l2_cache_send_command(struct mali_l2_cache_core *cache, u32 reg, u32 val);


static void mali_l2_cache_counter_lock(struct mali_l2_cache_core *cache)
{
#ifdef MALI_UPPER_HALF_SCHEDULING
	_mali_osk_spinlock_irq_lock(cache->counter_lock);
#else
	_mali_osk_spinlock_lock(cache->counter_lock);
#endif
}

static void mali_l2_cache_counter_unlock(struct mali_l2_cache_core *cache)
{
#ifdef MALI_UPPER_HALF_SCHEDULING
	_mali_osk_spinlock_irq_unlock(cache->counter_lock);
#else
	_mali_osk_spinlock_unlock(cache->counter_lock);
#endif
}

static void mali_l2_cache_command_lock(struct mali_l2_cache_core *cache)
{
#ifdef MALI_UPPER_HALF_SCHEDULING
	_mali_osk_spinlock_irq_lock(cache->command_lock);
#else
	_mali_osk_spinlock_lock(cache->command_lock);
#endif
}

static void mali_l2_cache_command_unlock(struct mali_l2_cache_core *cache)
{
#ifdef MALI_UPPER_HALF_SCHEDULING
	_mali_osk_spinlock_irq_unlock(cache->command_lock);
#else
	_mali_osk_spinlock_unlock(cache->command_lock);
#endif
}

struct mali_l2_cache_core *mali_l2_cache_create(_mali_osk_resource_t *resource)
{
	struct mali_l2_cache_core *cache = NULL;

	MALI_DEBUG_PRINT(4, ("Mali L2 cache: Creating Mali L2 cache: %s\n", resource->description));

	if (mali_global_num_l2_cache_cores >= MALI_MAX_NUMBER_OF_L2_CACHE_CORES) {
		MALI_PRINT_ERROR(("Mali L2 cache: Too many L2 cache core objects created\n"));
		return NULL;
	}

	cache = _mali_osk_malloc(sizeof(struct mali_l2_cache_core));
	if (NULL != cache) {
		cache->core_id =  mali_global_num_l2_cache_cores;
		cache->counter_src0 = MALI_HW_CORE_NO_COUNTER;
		cache->counter_src1 = MALI_HW_CORE_NO_COUNTER;
		cache->pm_domain = NULL;
		cache->mali_l2_status = MALI_L2_NORMAL;
		if (_MALI_OSK_ERR_OK == mali_hw_core_create(&cache->hw_core, resource, MALI400_L2_CACHE_REGISTERS_SIZE)) {
			MALI_DEBUG_CODE(u32 cache_size = mali_hw_core_register_read(&cache->hw_core, MALI400_L2_CACHE_REGISTER_SIZE));
			MALI_DEBUG_PRINT(2, ("Mali L2 cache: Created %s: % 3uK, %u-way, % 2ubyte cache line, % 3ubit external bus\n",
			                     resource->description,
			                     1 << (((cache_size >> 16) & 0xff) - 10),
			                     1 << ((cache_size >> 8) & 0xff),
			                     1 << (cache_size & 0xff),
			                     1 << ((cache_size >> 24) & 0xff)));

#ifdef MALI_UPPER_HALF_SCHEDULING
			cache->command_lock = _mali_osk_spinlock_irq_init(_MALI_OSK_LOCKFLAG_ORDERED, _MALI_OSK_LOCK_ORDER_L2_COMMAND);
#else
			cache->command_lock = _mali_osk_spinlock_init(_MALI_OSK_LOCKFLAG_ORDERED, _MALI_OSK_LOCK_ORDER_L2_COMMAND);
#endif
			if (NULL != cache->command_lock) {
#ifdef MALI_UPPER_HALF_SCHEDULING
				cache->counter_lock = _mali_osk_spinlock_irq_init(_MALI_OSK_LOCKFLAG_ORDERED, _MALI_OSK_LOCK_ORDER_L2_COMMAND);
#else
				cache->counter_lock = _mali_osk_spinlock_init(_MALI_OSK_LOCKFLAG_ORDERED, _MALI_OSK_LOCK_ORDER_L2_COMMAND);
#endif
				if (NULL != cache->counter_lock) {
					mali_l2_cache_reset(cache);

					cache->last_invalidated_id = 0;

					mali_global_l2_cache_cores[mali_global_num_l2_cache_cores] = cache;
					mali_global_num_l2_cache_cores++;

					return cache;
				} else {
					MALI_PRINT_ERROR(("Mali L2 cache: Failed to create counter lock for L2 cache core %s\n", cache->hw_core.description));
				}
#ifdef MALI_UPPER_HALF_SCHEDULING
				_mali_osk_spinlock_irq_term(cache->command_lock);
#else
				_mali_osk_spinlock_term(cache->command_lock);
#endif
			} else {
				MALI_PRINT_ERROR(("Mali L2 cache: Failed to create command lock for L2 cache core %s\n", cache->hw_core.description));
			}

			mali_hw_core_delete(&cache->hw_core);
		}

		_mali_osk_free(cache);
	} else {
		MALI_PRINT_ERROR(("Mali L2 cache: Failed to allocate memory for L2 cache core\n"));
	}

	return NULL;
}

void mali_l2_cache_delete(struct mali_l2_cache_core *cache)
{
	u32 i;

	/* reset to defaults */
	mali_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_MAX_READS, (u32)MALI400_L2_MAX_READS_DEFAULT);
	mali_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_ENABLE, (u32)MALI400_L2_CACHE_ENABLE_DEFAULT);

#ifdef MALI_UPPER_HALF_SCHEDULING
	_mali_osk_spinlock_irq_term(cache->counter_lock);
	_mali_osk_spinlock_irq_term(cache->command_lock);
#else
	_mali_osk_spinlock_term(cache->command_lock);
	_mali_osk_spinlock_term(cache->counter_lock);
#endif

	mali_hw_core_delete(&cache->hw_core);

	for (i = 0; i < mali_global_num_l2_cache_cores; i++) {
		if (mali_global_l2_cache_cores[i] == cache) {
			mali_global_l2_cache_cores[i] = NULL;
			mali_global_num_l2_cache_cores--;

			if (i != mali_global_num_l2_cache_cores) {
				/* We removed a l2 cache from the middle of the array -- move the last
				 * l2 cache to the current position to close the gap */
				mali_global_l2_cache_cores[i] = mali_global_l2_cache_cores[mali_global_num_l2_cache_cores];
				mali_global_l2_cache_cores[mali_global_num_l2_cache_cores] = NULL;
			}

			break;
		}
	}

	_mali_osk_free(cache);
}

u32 mali_l2_cache_get_id(struct mali_l2_cache_core *cache)
{
	return cache->core_id;
}

static void mali_l2_cache_core_set_counter_internal(struct mali_l2_cache_core *cache, u32 source_id, u32 counter)
{
	u32 value = 0; /* disabled src */
	u32 reg_offset = 0;
	mali_bool core_is_on;

	MALI_DEBUG_ASSERT_POINTER(cache);

	core_is_on = mali_l2_cache_lock_power_state(cache);

	mali_l2_cache_counter_lock(cache);

	switch (source_id) {
	case 0:
		cache->counter_src0 = counter;
		reg_offset = MALI400_L2_CACHE_REGISTER_PERFCNT_SRC0;
		break;

	case 1:
		cache->counter_src1 = counter;
		reg_offset = MALI400_L2_CACHE_REGISTER_PERFCNT_SRC1;
		break;

	default:
		MALI_DEBUG_ASSERT(0);
		break;
	}

	if (MALI_L2_PAUSE == cache->mali_l2_status) {
		mali_l2_cache_counter_unlock(cache);
		mali_l2_cache_unlock_power_state(cache);
		return;
	}

	if (MALI_HW_CORE_NO_COUNTER != counter) {
		value = counter;
	}

	if (MALI_TRUE == core_is_on) {
		mali_hw_core_register_write(&cache->hw_core, reg_offset, value);
	}

	mali_l2_cache_counter_unlock(cache);
	mali_l2_cache_unlock_power_state(cache);
}

void mali_l2_cache_core_set_counter_src0(struct mali_l2_cache_core *cache, u32 counter)
{
	mali_l2_cache_core_set_counter_internal(cache, 0, counter);
}

void mali_l2_cache_core_set_counter_src1(struct mali_l2_cache_core *cache, u32 counter)
{
	mali_l2_cache_core_set_counter_internal(cache, 1, counter);
}

u32 mali_l2_cache_core_get_counter_src0(struct mali_l2_cache_core *cache)
{
	return cache->counter_src0;
}

u32 mali_l2_cache_core_get_counter_src1(struct mali_l2_cache_core *cache)
{
	return cache->counter_src1;
}

void mali_l2_cache_core_get_counter_values(struct mali_l2_cache_core *cache, u32 *src0, u32 *value0, u32 *src1, u32 *value1)
{
	MALI_DEBUG_ASSERT(NULL != src0);
	MALI_DEBUG_ASSERT(NULL != value0);
	MALI_DEBUG_ASSERT(NULL != src1);
	MALI_DEBUG_ASSERT(NULL != value1);

	/* Caller must hold the PM lock and know that we are powered on */

	mali_l2_cache_counter_lock(cache);

	if (MALI_L2_PAUSE == cache->mali_l2_status) {
		mali_l2_cache_counter_unlock(cache);

		return;
	}

	*src0 = cache->counter_src0;
	*src1 = cache->counter_src1;

	if (cache->counter_src0 != MALI_HW_CORE_NO_COUNTER) {
		*value0 = mali_hw_core_register_read(&cache->hw_core, MALI400_L2_CACHE_REGISTER_PERFCNT_VAL0);
	}

	if (cache->counter_src1 != MALI_HW_CORE_NO_COUNTER) {
		*value1 = mali_hw_core_register_read(&cache->hw_core, MALI400_L2_CACHE_REGISTER_PERFCNT_VAL1);
	}

	mali_l2_cache_counter_unlock(cache);
}

static void mali_l2_cache_reset_counters_all(void)
{
	int i;
	u32 value;
	struct mali_l2_cache_core *cache;
	u32 num_cores = mali_l2_cache_core_get_glob_num_l2_cores();

	for (i = 0; i < num_cores; i++) {
		cache = mali_l2_cache_core_get_glob_l2_core(i);
		if (MALI_TRUE == mali_l2_cache_lock_power_state(cache)) {
			mali_l2_cache_counter_lock(cache);

			if (MALI_L2_PAUSE == cache->mali_l2_status) {
				mali_l2_cache_counter_unlock(cache);
				mali_l2_cache_unlock_power_state(cache);
				return;
			}

			/* Reset performance counters */
			if (MALI_HW_CORE_NO_COUNTER == cache->counter_src0) {
				value = 0;
			} else {
				value = cache->counter_src0;
			}
			mali_hw_core_register_write(&cache->hw_core,
			                            MALI400_L2_CACHE_REGISTER_PERFCNT_SRC0, value);

			if (MALI_HW_CORE_NO_COUNTER == cache->counter_src1) {
				value = 0;
			} else {
				value = cache->counter_src1;
			}
			mali_hw_core_register_write(&cache->hw_core,
			                            MALI400_L2_CACHE_REGISTER_PERFCNT_SRC1, value);

			mali_l2_cache_counter_unlock(cache);
		}

		mali_l2_cache_unlock_power_state(cache);
	}
}


struct mali_l2_cache_core *mali_l2_cache_core_get_glob_l2_core(u32 index)
{
	if (mali_global_num_l2_cache_cores > index) {
		return mali_global_l2_cache_cores[index];
	}

	return NULL;
}

u32 mali_l2_cache_core_get_glob_num_l2_cores(void)
{
	return mali_global_num_l2_cache_cores;
}

void mali_l2_cache_reset(struct mali_l2_cache_core *cache)
{
	/* Invalidate cache (just to keep it in a known state at startup) */
	mali_l2_cache_send_command(cache, MALI400_L2_CACHE_REGISTER_COMMAND, MALI400_L2_CACHE_COMMAND_CLEAR_ALL);

	mali_l2_cache_counter_lock(cache);

	if (MALI_L2_PAUSE == cache->mali_l2_status) {
		mali_l2_cache_counter_unlock(cache);

		return;
	}

	/* Enable cache */
	mali_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_ENABLE, (u32)MALI400_L2_CACHE_ENABLE_ACCESS | (u32)MALI400_L2_CACHE_ENABLE_READ_ALLOCATE);
	mali_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_MAX_READS, (u32)mali_l2_max_reads);

	/* Restart any performance counters (if enabled) */
	if (cache->counter_src0 != MALI_HW_CORE_NO_COUNTER) {
		mali_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_PERFCNT_SRC0, cache->counter_src0);
	}

	if (cache->counter_src1 != MALI_HW_CORE_NO_COUNTER) {
		mali_hw_core_register_write(&cache->hw_core, MALI400_L2_CACHE_REGISTER_PERFCNT_SRC1, cache->counter_src1);
	}

	mali_l2_cache_counter_unlock(cache);
}

void mali_l2_cache_reset_all(void)
{
	int i;
	u32 num_cores = mali_l2_cache_core_get_glob_num_l2_cores();

	for (i = 0; i < num_cores; i++) {
		mali_l2_cache_reset(mali_l2_cache_core_get_glob_l2_core(i));
	}
}

void mali_l2_cache_invalidate(struct mali_l2_cache_core *cache)
{
	MALI_DEBUG_ASSERT_POINTER(cache);

	if (NULL != cache) {
		cache->last_invalidated_id = mali_scheduler_get_new_cache_order();
		mali_l2_cache_send_command(cache, MALI400_L2_CACHE_REGISTER_COMMAND, MALI400_L2_CACHE_COMMAND_CLEAR_ALL);
	}
}

mali_bool mali_l2_cache_invalidate_conditional(struct mali_l2_cache_core *cache, u32 id)
{
	MALI_DEBUG_ASSERT_POINTER(cache);

	if (NULL != cache) {
		/* If the last cache invalidation was done by a job with a higher id we
		 * don't have to flush. Since user space will store jobs w/ their
		 * corresponding memory in sequence (first job #0, then job #1, ...),
		 * we don't have to flush for job n-1 if job n has already invalidated
		 * the cache since we know for sure that job n-1's memory was already
		 * written when job n was started. */
		if (((s32)id) <= ((s32)cache->last_invalidated_id)) {
			return MALI_FALSE;
		} else {
			cache->last_invalidated_id = mali_scheduler_get_new_cache_order();
		}

		mali_l2_cache_send_command(cache, MALI400_L2_CACHE_REGISTER_COMMAND, MALI400_L2_CACHE_COMMAND_CLEAR_ALL);
	}
	return MALI_TRUE;
}

void mali_l2_cache_invalidate_all(void)
{
	u32 i;
	for (i = 0; i < mali_global_num_l2_cache_cores; i++) {
		/*additional check*/
		if (MALI_TRUE == mali_l2_cache_lock_power_state(mali_global_l2_cache_cores[i])) {
			_mali_osk_errcode_t ret;
			mali_global_l2_cache_cores[i]->last_invalidated_id = mali_scheduler_get_new_cache_order();
			ret = mali_l2_cache_send_command(mali_global_l2_cache_cores[i], MALI400_L2_CACHE_REGISTER_COMMAND, MALI400_L2_CACHE_COMMAND_CLEAR_ALL);
			if (_MALI_OSK_ERR_OK != ret) {
				MALI_PRINT_ERROR(("Failed to invalidate cache\n"));
			}
		}
		mali_l2_cache_unlock_power_state(mali_global_l2_cache_cores[i]);
	}
}

void mali_l2_cache_invalidate_all_pages(u32 *pages, u32 num_pages)
{
	u32 i;
	for (i = 0; i < mali_global_num_l2_cache_cores; i++) {
		/*additional check*/
		if (MALI_TRUE == mali_l2_cache_lock_power_state(mali_global_l2_cache_cores[i])) {
			u32 j;
			for (j = 0; j < num_pages; j++) {
				_mali_osk_errcode_t ret;
				ret = mali_l2_cache_send_command(mali_global_l2_cache_cores[i], MALI400_L2_CACHE_REGISTER_CLEAR_PAGE, pages[j]);
				if (_MALI_OSK_ERR_OK != ret) {
					MALI_PRINT_ERROR(("Failed to invalidate page cache\n"));
				}
			}
		}
		mali_l2_cache_unlock_power_state(mali_global_l2_cache_cores[i]);
	}
}

mali_bool mali_l2_cache_lock_power_state(struct mali_l2_cache_core *cache)
{
	return mali_pm_domain_lock_state(cache->pm_domain);
}

void mali_l2_cache_unlock_power_state(struct mali_l2_cache_core *cache)
{
	return mali_pm_domain_unlock_state(cache->pm_domain);
}

/* -------- local helper functions below -------- */


static _mali_osk_errcode_t mali_l2_cache_send_command(struct mali_l2_cache_core *cache, u32 reg, u32 val)
{
	int i = 0;
	const int loop_count = 100000;

	/*
	 * Grab lock in order to send commands to the L2 cache in a serialized fashion.
	 * The L2 cache will ignore commands if it is busy.
	 */
	mali_l2_cache_command_lock(cache);

	if (MALI_L2_PAUSE == cache->mali_l2_status) {
		mali_l2_cache_command_unlock(cache);
		MALI_DEBUG_PRINT(1, ( "Mali L2 cache: aborting wait for L2 come back\n"));

		MALI_ERROR( _MALI_OSK_ERR_BUSY );
	}

	/* First, wait for L2 cache command handler to go idle */

	for (i = 0; i < loop_count; i++) {
		if (!(mali_hw_core_register_read(&cache->hw_core, MALI400_L2_CACHE_REGISTER_STATUS) & (u32)MALI400_L2_CACHE_STATUS_COMMAND_BUSY)) {
			break;
		}
	}

	if (i == loop_count) {
		mali_l2_cache_command_unlock(cache);
		MALI_DEBUG_PRINT(1, ( "Mali L2 cache: aborting wait for command interface to go idle\n"));
		MALI_ERROR( _MALI_OSK_ERR_FAULT );
	}

	/* then issue the command */
	mali_hw_core_register_write(&cache->hw_core, reg, val);

	mali_l2_cache_command_unlock(cache);

	MALI_SUCCESS;
}

void mali_l2_cache_pause_all(mali_bool pause)
{
	int i;
	struct mali_l2_cache_core * cache;
	u32 num_cores = mali_l2_cache_core_get_glob_num_l2_cores();
	mali_l2_power_status status = MALI_L2_NORMAL;

	if (pause) {
		status = MALI_L2_PAUSE;
	}

	for (i = 0; i < num_cores; i++) {
		cache = mali_l2_cache_core_get_glob_l2_core(i);
		if (NULL != cache) {
			cache->mali_l2_status = status;

			/* Take and release the counter and command locks to
			 * ensure there are no active threads that didn't get
			 * the status flag update.
			 *
			 * The locks will also ensure the necessary memory
			 * barriers are done on SMP systems.
			 */
			mali_l2_cache_counter_lock(cache);
			mali_l2_cache_counter_unlock(cache);

			mali_l2_cache_command_lock(cache);
			mali_l2_cache_command_unlock(cache);
		}
	}

	/* Resume from pause: do the cache invalidation here to prevent any
	 * loss of cache operation during the pause period to make sure the SW
	 * status is consistent with L2 cache status.
	 */
	if(!pause) {
		mali_l2_cache_invalidate_all();
		mali_l2_cache_reset_counters_all();
	}
}
