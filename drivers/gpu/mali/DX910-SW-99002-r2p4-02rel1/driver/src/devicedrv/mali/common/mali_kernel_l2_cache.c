/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_osk_list.h"

#include "mali_kernel_core.h"
#include "mali_kernel_pp.h"
#include "mali_kernel_subsystem.h"
#include "regs/mali_200_regs.h"
#include "mali_kernel_rendercore.h"
#include "mali_kernel_l2_cache.h"

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
	MALI400_L2_CACHE_REGISTER_STATUS       = 0x0002,
	/*unused                               = 0x0003 */
	MALI400_L2_CACHE_REGISTER_COMMAND      = 0x0004, /**< Misc cache commands, e.g. clear */
	MALI400_L2_CACHE_REGISTER_CLEAR_PAGE   = 0x0005,
	MALI400_L2_CACHE_REGISTER_MAX_READS    = 0x0006, /**< Limit of outstanding read requests */
	MALI400_L2_CACHE_REGISTER_ENABLE       = 0x0007, /**< Enable misc cache features */
	MALI400_L2_CACHE_REGISTER_PERFCNT_SRC0 = 0x0008,
	MALI400_L2_CACHE_REGISTER_PERFCNT_VAL0 = 0x0009,
	MALI400_L2_CACHE_REGISTER_PERFCNT_SRC1 = 0x000A,
	MALI400_L2_CACHE_REGISTER_PERFCNT_VAL1 = 0x000B,
} mali_l2_cache_register;


/**
 * Mali L2 cache commands
 * These are the commands that can be sent to the Mali L2 cache unit
 */
typedef enum mali_l2_cache_command
{
	MALI400_L2_CACHE_COMMAND_CLEAR_ALL = 0x01, /**< Clear the entire cache */
	/* Read HW TRM carefully before adding/using other commands than the clear above */
} mali_l2_cache_command;

/**
 * Mali L2 cache commands
 * These are the commands that can be sent to the Mali L2 cache unit
 */
typedef enum mali_l2_cache_enable
{
	MALI400_L2_CACHE_ENABLE_DEFAULT = 0x0, /**< Default state of enable register */
	MALI400_L2_CACHE_ENABLE_ACCESS = 0x01, /**< Permit cacheable accesses */
	MALI400_L2_CACHE_ENABLE_READ_ALLOCATE = 0x02, /**< Permit cache read allocate */
} mali_l2_cache_enable;

/**
 * Mali L2 cache status bits
 */
typedef enum mali_l2_cache_status
{
	MALI400_L2_CACHE_STATUS_COMMAND_BUSY = 0x01, /**< Command handler of L2 cache is busy */
	MALI400_L2_CACHE_STATUS_DATA_BUSY    = 0x02, /**< L2 cache is busy handling data requests */
} mali_l2_cache_status;


/**
 * Definition of the L2 cache core struct
 * Used to track a L2 cache unit in the system.
 * Contains information about the mapping of the registers
 */
typedef struct mali_kernel_l2_cache_core
{
	unsigned long base; /**< Physical address of the registers */
	mali_io_address mapped_registers; /**< Virtual mapping of the registers */
	u32 mapping_size; /**< Size of registers in bytes */
	_mali_osk_list_t list; /**< Used to link multiple cache cores into a list */
	_mali_osk_lock_t *lock; /**< Serialize all L2 cache commands */
} mali_kernel_l2_cache_core;


#define MALI400_L2_MAX_READS_DEFAULT 0x1C

int mali_l2_max_reads = MALI400_L2_MAX_READS_DEFAULT;


/**
 * Mali L2 cache subsystem startup function
 * Called by the driver core when the driver is loaded.
 *
 * @param id Identifier assigned by the core to the L2 cache subsystem
 * @return 0 on success, negative on error
 */
static _mali_osk_errcode_t mali_l2_cache_initialize(mali_kernel_subsystem_identifier id);

/**
 * Mali L2 cache subsystem shutdown function
 * Called by the driver core when the driver is unloaded.
 * Cleans up
 * @param id Identifier assigned by the core to the L2 cache subsystem
 */
static void mali_l2_cache_terminate(mali_kernel_subsystem_identifier id);

/**
 * L2 cache subsystem complete notification function.
 * Called by the driver core when all drivers have loaded and all resources has been registered
 * @param id Identifier assigned by the core to the L2 cache subsystem
 * @return 0 on success, negative on error
 */
static _mali_osk_errcode_t mali_l2_cache_load_complete(mali_kernel_subsystem_identifier id);

/**
 * Mali L2 cache subsystem's notification handler for a Mali L2 cache resource instances.
 * Registered with the core during startup.
 * Called by the core for each Mali L2 cache described in the active architecture's config.h file.
 * @param resource The resource to handle (type MALI400L2)
 * @return 0 if the Mali L2 cache was found and initialized, negative on error
 */
static _mali_osk_errcode_t mali_l2_cache_core_create(_mali_osk_resource_t * resource);

/**
 * Write to a L2 cache register
 * Writes the given value to the specified register
 * @param unit The L2 cache to write to
 * @param reg The register to write to
 * @param val The value to write to the register
 */
static void mali_l2_cache_register_write(mali_kernel_l2_cache_core * unit, mali_l2_cache_register reg, u32 val);



/**
 * Invalidate specified L2 cache
 * @param cache The L2 cache to invalidate
 * @return 0 if Mali L2 cache was successfully invalidated, otherwise error
 */
static _mali_osk_errcode_t mali_kernel_l2_cache_invalidate_all_cache(mali_kernel_l2_cache_core *cache);


/*
	The fixed Mali L2 cache system's mali subsystem interface implementation.
	We currently handle module and session life-time management.
*/
struct mali_kernel_subsystem mali_subsystem_l2_cache =
{
	mali_l2_cache_initialize,    /**< startup */
	mali_l2_cache_terminate,     /**< shutdown */
	mali_l2_cache_load_complete, /**< load_complete */
	NULL,                        /**< system_info_fill */
	NULL,                        /**< session_begin */
	NULL,                        /**< session_end */
	NULL,                        /**< broadcast_notification */
#if MALI_STATE_TRACKING
	NULL,                        /**< dump_state */
#endif
};



static _MALI_OSK_LIST_HEAD(caches_head);




/* called during module init */
static _mali_osk_errcode_t mali_l2_cache_initialize(mali_kernel_subsystem_identifier id)
{
	_mali_osk_errcode_t err;

	MALI_IGNORE( id );

	MALI_DEBUG_PRINT(2, ( "Mali L2 cache system initializing\n"));

	_MALI_OSK_INIT_LIST_HEAD(&caches_head);

	/* This will register the function for adding Mali L2 cache cores to the subsystem */
	err = _mali_kernel_core_register_resource_handler(MALI400L2, mali_l2_cache_core_create);

	MALI_ERROR(err);
}



/* called if/when our module is unloaded */
static void mali_l2_cache_terminate(mali_kernel_subsystem_identifier id)
{
	mali_kernel_l2_cache_core * cache, *temp_cache;

	MALI_DEBUG_PRINT(2, ( "Mali L2 cache system terminating\n"));

	/* loop over all L2 cache units and shut them down */
	_MALI_OSK_LIST_FOREACHENTRY( cache, temp_cache, &caches_head, mali_kernel_l2_cache_core, list )
	{
		/* reset to defaults */
		mali_l2_cache_register_write(cache, MALI400_L2_CACHE_REGISTER_MAX_READS, (u32)MALI400_L2_MAX_READS_DEFAULT);
		mali_l2_cache_register_write(cache, MALI400_L2_CACHE_REGISTER_ENABLE, (u32)MALI400_L2_CACHE_ENABLE_DEFAULT);

		/* remove from the list of cacges on the system */
		_mali_osk_list_del( &cache->list );

		/* release resources */
		_mali_osk_mem_unmapioregion( cache->base, cache->mapping_size, cache->mapped_registers );
		_mali_osk_mem_unreqregion( cache->base, cache->mapping_size );
		_mali_osk_lock_term( cache->lock );
		_mali_osk_free( cache );

		#if USING_MALI_PMM
			/* Unregister the L2 cache with the PMM */
			malipmm_core_unregister( MALI_PMM_CORE_L2 );
		#endif
	}
}

static _mali_osk_errcode_t mali_l2_cache_core_create(_mali_osk_resource_t * resource)
{
	_mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT ;
	mali_kernel_l2_cache_core * cache = NULL;

	MALI_DEBUG_PRINT(2, ( "Creating Mali L2 cache: %s\n", resource->description));

#if USING_MALI_PMM
	/* Register the L2 cache with the PMM */
	err = malipmm_core_register( MALI_PMM_CORE_L2 );
	if( _MALI_OSK_ERR_OK != err )
	{
		MALI_DEBUG_PRINT(1, ( "Failed to register L2 cache unit with PMM"));
		return err;
	}
#endif

	err = _mali_osk_mem_reqregion( resource->base, MALI400_L2_CACHE_REGISTERS_SIZE, resource->description);

	MALI_CHECK_GOTO( _MALI_OSK_ERR_OK == err, err_cleanup_requestmem_failed);

	/* Reset error that might be passed out */
	err = _MALI_OSK_ERR_FAULT;

	cache = _mali_osk_malloc(sizeof(mali_kernel_l2_cache_core));

	MALI_CHECK_GOTO( NULL != cache, err_cleanup);

	cache->lock = _mali_osk_lock_init( _MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_SPINLOCK | _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE, 0, 104 );

	MALI_CHECK_GOTO( NULL != cache->lock, err_cleanup);

	/* basic setup */
	_MALI_OSK_INIT_LIST_HEAD(&cache->list);

	cache->base = resource->base;
	cache->mapping_size = MALI400_L2_CACHE_REGISTERS_SIZE;

	/* map the registers */
	cache->mapped_registers = _mali_osk_mem_mapioregion( cache->base, cache->mapping_size, resource->description );

	MALI_CHECK_GOTO( NULL != cache->mapped_registers, err_cleanup);

	/* Invalidate cache (just to keep it in a known state at startup) */
	err = mali_kernel_l2_cache_invalidate_all_cache(cache);

	MALI_CHECK_GOTO( _MALI_OSK_ERR_OK == err, err_cleanup);

	/* add to our list of L2 caches */
	_mali_osk_list_add( &cache->list, &caches_head );

	MALI_SUCCESS;

err_cleanup:
	/* This cleanup used when resources have been requested successfully */

	if ( NULL != cache )
	{
		if (NULL != cache->mapped_registers)
		{
			_mali_osk_mem_unmapioregion( cache->base, cache->mapping_size, cache->mapped_registers);
		}
		else
		{
			MALI_DEBUG_PRINT(1, ( "Failed to map Mali L2 cache registers at 0x%08lX\n", cache->base));
		}

		if( NULL != cache->lock )
		{
			_mali_osk_lock_term( cache->lock );
		}
		else
		{
			MALI_DEBUG_PRINT(1, ( "Failed to allocate a lock for handling a L2 cache unit"));
		}

		_mali_osk_free( cache );
	}
	else
	{
		MALI_DEBUG_PRINT(1, ( "Failed to allocate memory for handling a L2 cache unit"));
	}

	/* A call is to request region, so this must always be reversed */
	_mali_osk_mem_unreqregion( resource->base, MALI400_L2_CACHE_REGISTERS_SIZE);
#if USING_MALI_PMM
	malipmm_core_unregister( MALI_PMM_CORE_L2 );
#endif
	return err;

err_cleanup_requestmem_failed:
	MALI_DEBUG_PRINT(1, ("Failed to request Mali L2 cache '%s' register address space at (0x%08X - 0x%08X)\n",
						 resource->description, resource->base, resource->base + MALI400_L2_CACHE_REGISTERS_SIZE - 1) );
#if USING_MALI_PMM
	malipmm_core_unregister( MALI_PMM_CORE_L2 );
#endif
	return err;

}


static void mali_l2_cache_register_write(mali_kernel_l2_cache_core * unit, mali_l2_cache_register reg, u32 val)
{
	_mali_osk_mem_iowrite32(unit->mapped_registers, (u32)reg * sizeof(u32), val);
}


static u32 mali_l2_cache_register_read(mali_kernel_l2_cache_core * unit, mali_l2_cache_register reg)
{
	return _mali_osk_mem_ioread32(unit->mapped_registers, (u32)reg * sizeof(u32));
}

void mali_kernel_l2_cache_do_enable(void)
{
	mali_kernel_l2_cache_core * cache, *temp_cache;

	/* loop over all L2 cache units and enable them*/
	_MALI_OSK_LIST_FOREACHENTRY( cache, temp_cache, &caches_head, mali_kernel_l2_cache_core, list)
	{
		mali_l2_cache_register_write(cache, MALI400_L2_CACHE_REGISTER_ENABLE, (u32)MALI400_L2_CACHE_ENABLE_ACCESS | (u32)MALI400_L2_CACHE_ENABLE_READ_ALLOCATE);
		mali_l2_cache_register_write(cache, MALI400_L2_CACHE_REGISTER_MAX_READS, (u32)mali_l2_max_reads);
	}
}


static _mali_osk_errcode_t mali_l2_cache_load_complete(mali_kernel_subsystem_identifier id)
{
	mali_kernel_l2_cache_do_enable();
	MALI_DEBUG_PRINT(2, ( "Mali L2 cache system load complete\n"));

	MALI_SUCCESS;
}

static _mali_osk_errcode_t mali_kernel_l2_cache_send_command(mali_kernel_l2_cache_core *cache, u32 reg, u32 val)
{
	int i = 0;
	const int loop_count = 100000;

	/*
	 * Grab lock in order to send commands to the L2 cache in a serialized fashion.
	 * The L2 cache will ignore commands if it is busy.
	 */
	_mali_osk_lock_wait(cache->lock, _MALI_OSK_LOCKMODE_RW);

	/* First, wait for L2 cache command handler to go idle */

	for (i = 0; i < loop_count; i++)
	{
		if (!(_mali_osk_mem_ioread32(cache->mapped_registers , (u32)MALI400_L2_CACHE_REGISTER_STATUS * sizeof(u32)) & (u32)MALI400_L2_CACHE_STATUS_COMMAND_BUSY))
		{
			break;
		}
	}

	if (i == loop_count)
	{
		_mali_osk_lock_signal(cache->lock, _MALI_OSK_LOCKMODE_RW);
		MALI_DEBUG_PRINT(1, ( "Mali L2 cache: aborting wait for command interface to go idle\n"));
		MALI_ERROR( _MALI_OSK_ERR_FAULT );
	}

	/* then issue the command */
	mali_l2_cache_register_write(cache, reg, val);

	_mali_osk_lock_signal(cache->lock, _MALI_OSK_LOCKMODE_RW);
	MALI_SUCCESS;
}


static _mali_osk_errcode_t mali_kernel_l2_cache_invalidate_all_cache(mali_kernel_l2_cache_core *cache)
{
	return mali_kernel_l2_cache_send_command(cache, MALI400_L2_CACHE_REGISTER_COMMAND, MALI400_L2_CACHE_COMMAND_CLEAR_ALL);
}

_mali_osk_errcode_t mali_kernel_l2_cache_invalidate_all(void)
{
	mali_kernel_l2_cache_core * cache, *temp_cache;

	/* loop over all L2 cache units and invalidate them */

	_MALI_OSK_LIST_FOREACHENTRY( cache, temp_cache, &caches_head, mali_kernel_l2_cache_core, list)
	{
		MALI_CHECK_NO_ERROR( mali_kernel_l2_cache_invalidate_all_cache(cache) );
	}

	MALI_SUCCESS;
}


static _mali_osk_errcode_t mali_kernel_l2_cache_invalidate_page_cache(mali_kernel_l2_cache_core *cache, u32 page)
{
	return mali_kernel_l2_cache_send_command(cache, MALI400_L2_CACHE_REGISTER_CLEAR_PAGE, page);
}

_mali_osk_errcode_t mali_kernel_l2_cache_invalidate_page(u32 page)
{
	mali_kernel_l2_cache_core * cache, *temp_cache;

	/* loop over all L2 cache units and invalidate them */

	_MALI_OSK_LIST_FOREACHENTRY( cache, temp_cache, &caches_head, mali_kernel_l2_cache_core, list)
	{
		MALI_CHECK_NO_ERROR( mali_kernel_l2_cache_invalidate_page_cache(cache, page) );
	}

	MALI_SUCCESS;
}


void mali_kernel_l2_cache_set_perf_counters(u32 src0, u32 src1, int force_reset)
{
	mali_kernel_l2_cache_core * cache, *temp_cache;
	int reset0 = force_reset;
	int reset1 = force_reset;
	MALI_DEBUG_CODE(
		int changed0 = 0;
		int changed1 = 0;
	)

	/* loop over all L2 cache units and activate the counters on them */
	_MALI_OSK_LIST_FOREACHENTRY(cache, temp_cache, &caches_head, mali_kernel_l2_cache_core, list)
	{
		u32 cur_src0 = mali_l2_cache_register_read(cache, MALI400_L2_CACHE_REGISTER_PERFCNT_SRC0);
		u32 cur_src1 = mali_l2_cache_register_read(cache, MALI400_L2_CACHE_REGISTER_PERFCNT_SRC1);

		if (src0 != cur_src0)
		{
			mali_l2_cache_register_write(cache, MALI400_L2_CACHE_REGISTER_PERFCNT_SRC0, src0);
			MALI_DEBUG_CODE(changed0 = 1;)
			reset0 = 1;
		}

		if (src1 != cur_src1)
		{
			mali_l2_cache_register_write(cache, MALI400_L2_CACHE_REGISTER_PERFCNT_SRC1, src1);
			MALI_DEBUG_CODE(changed1 = 1;)
			reset1 = 1;
		}

		if (reset0)
		{
			mali_l2_cache_register_write(cache, MALI400_L2_CACHE_REGISTER_PERFCNT_VAL0, 0);
		}

		if (reset1)
		{
			mali_l2_cache_register_write(cache, MALI400_L2_CACHE_REGISTER_PERFCNT_VAL1, 0);
		}

		MALI_DEBUG_PRINT(5, ("L2 cache counters set: SRC0=%u, CHANGED0=%d, RESET0=%d, SRC1=%u, CHANGED1=%d, RESET1=%d\n",
			src0, changed0, reset0,
			src1, changed1, reset1));
	}
}


void mali_kernel_l2_cache_get_perf_counters(u32 *src0, u32 *val0, u32 *src1, u32 *val1)
{
	mali_kernel_l2_cache_core * cache, *temp_cache;
	int first_time = 1;
	*src0 = 0;
	*src1 = 0;
	*val0 = 0;
	*val1 = 0;

	/* loop over all L2 cache units and read the counters */
	_MALI_OSK_LIST_FOREACHENTRY(cache, temp_cache, &caches_head, mali_kernel_l2_cache_core, list)
	{
		u32 cur_src0 = mali_l2_cache_register_read(cache, MALI400_L2_CACHE_REGISTER_PERFCNT_SRC0);
		u32 cur_src1 = mali_l2_cache_register_read(cache, MALI400_L2_CACHE_REGISTER_PERFCNT_SRC1);
		u32 cur_val0 = mali_l2_cache_register_read(cache, MALI400_L2_CACHE_REGISTER_PERFCNT_VAL0);
		u32 cur_val1 = mali_l2_cache_register_read(cache, MALI400_L2_CACHE_REGISTER_PERFCNT_VAL1);

		MALI_DEBUG_PRINT(5, ("L2 cache counters get: SRC0=%u, VAL0=%u, SRC1=%u, VAL1=%u\n", cur_src0, cur_val0, cur_src1, cur_val1));

		/* Only update the counter source once, with the value from the first L2 cache unit. */
		if (first_time)
		{
			*src0 = cur_src0;
			*src1 = cur_src1;
			first_time = 0;
		}

		/* Bail out if the L2 cache units have different counters set. */
		if (*src0 == cur_src0 && *src1 == cur_src1)
		{
			*val0 += cur_val0;
			*val1 += cur_val1;
		}
		else
		{
			MALI_DEBUG_PRINT(1, ("Warning: Mali L2 caches has different performance counters set, not retrieving data\n"));
		}
	}
}
