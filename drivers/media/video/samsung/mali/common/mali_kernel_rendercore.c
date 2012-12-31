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
#include "mali_kernel_core.h"
#include "mali_osk.h"
#include "mali_kernel_subsystem.h"
#include "mali_kernel_rendercore.h"
#include "mali_osk_list.h"
#if MALI_GPU_UTILIZATION
#include "mali_kernel_utilization.h"
#endif
#if MALI_TIMELINE_PROFILING_ENABLED
#include "mali_kernel_profiling.h"
#endif
#if USING_MMU
#include "mali_kernel_mem_mmu.h"
#endif /* USING_MMU */
#if defined USING_MALI400_L2_CACHE
#include "mali_kernel_l2_cache.h"
#endif /* USING_MALI400_L2_CACHE */

#define HANG_CHECK_MSECS_MIN 100
#define HANG_CHECK_MSECS_MAX 2000 /* 2 secs */
#define HANG_CHECK_MSECS_DEFAULT 500 /* 500 ms */

#define WATCHDOG_MSECS_MIN (10*HANG_CHECK_MSECS_MIN)
#define WATCHDOG_MSECS_MAX 3600000 /* 1 hour */
#define WATCHDOG_MSECS_DEFAULT 4000 /* 4 secs */

/* max value that will be converted from jiffies to micro seconds and written to job->render_time_usecs */
#define JOB_MAX_JIFFIES 100000

int mali_hang_check_interval = HANG_CHECK_MSECS_DEFAULT;
int mali_max_job_runtime = WATCHDOG_MSECS_DEFAULT;

#if MALI_TIMELINE_PROFILING_ENABLED
int mali_boot_profiling = 0;
#endif

/* Subsystem entrypoints: */
static _mali_osk_errcode_t rendercore_subsystem_startup(mali_kernel_subsystem_identifier id);
static void rendercore_subsystem_terminate(mali_kernel_subsystem_identifier id);
#if USING_MMU
static void rendercore_subsystem_broadcast_notification(mali_core_notification_message message, u32 data);
#endif


static void mali_core_subsystem_cleanup_all_renderunits(struct mali_core_subsystem* subsys);
static void mali_core_subsystem_move_core_set_idle(struct mali_core_renderunit *core);

static mali_core_session * mali_core_subsystem_get_waiting_session(mali_core_subsystem *subsystem);
static mali_core_job * mali_core_subsystem_release_session_get_job(mali_core_subsystem *subsystem, mali_core_session * session);

static void find_and_abort(mali_core_session* session, u32 abort_id);

static void mali_core_job_start_on_core(mali_core_job *job, mali_core_renderunit *core);
#if USING_MMU
static void mali_core_subsystem_callback_schedule_wrapper(void* sub);
#endif
static void mali_core_subsystem_schedule(mali_core_subsystem*subsystem);
static void mali_core_renderunit_detach_job_from_core(mali_core_renderunit* core, mali_subsystem_reschedule_option reschedule, mali_subsystem_job_end_code end_status);

static void  mali_core_renderunit_irq_handler_remove(struct mali_core_renderunit *core);

static _mali_osk_errcode_t  mali_core_irq_handler_upper_half (void * data);
static void mali_core_irq_handler_bottom_half ( void *data );

#if USING_MMU
static void lock_subsystem(struct mali_core_subsystem * subsys);
static void unlock_subsystem(struct mali_core_subsystem * subsys);
#endif


/**
 * This will be one of the subsystems in the array of subsystems:
 * static struct mali_kernel_subsystem * subsystems[];
 * found in file: mali_kernel_core.c
 *
 * This subsystem is necessary for operations common to all rendercore
 * subsystems. For example, mali_subsystem_mali200 and mali_subsystem_gp2 may
 * share a mutex when RENDERCORES_USE_GLOBAL_MUTEX is non-zero.
 */
struct mali_kernel_subsystem mali_subsystem_rendercore=
{
	rendercore_subsystem_startup,                  /* startup */
	NULL, /*rendercore_subsystem_terminate,*/                /* shutdown */
	NULL,                                          /* load_complete */
	NULL,                                          /* system_info_fill */
	NULL,                                          /* session_begin */
	NULL,                                          /* session_end */
#if USING_MMU
	rendercore_subsystem_broadcast_notification,  /* broadcast_notification */
#else
	NULL,
#endif
#if MALI_STATE_TRACKING
	NULL,                                          /* dump_state */
#endif
} ;

static _mali_osk_lock_t *rendercores_global_mutex = NULL;
static u32 rendercores_global_mutex_is_held = 0;
static u32 rendercores_global_mutex_owner = 0;

/** The 'dummy' rendercore subsystem to allow global subsystem mutex to be
 * locked for all subsystems that extend the ''rendercore'' */
static mali_core_subsystem rendercore_dummy_subsystem = {0,};

/*
 * Rendercore Subsystem functions.
 *
 * These are exposed by mali_subsystem_rendercore
 */

/**
 * @brief Initialize the Rendercore subsystem.
 *
 * This must be called before any other subsystem that extends the
 * ''rendercore'' may be initialized. For example, this must be called before
 * the following functions:
 * - mali200_subsystem_startup(), from mali_subsystem_mali200
 * - maligp_subsystem_startup(), from mali_subsystem_gp2
 *
 * @note This function is separate from mali_core_subsystem_init(). They
 * are related, in that mali_core_subsystem_init() may use the structures
 * initialized by rendercore_subsystem_startup()
 */
static _mali_osk_errcode_t rendercore_subsystem_startup(mali_kernel_subsystem_identifier id)
{
	rendercores_global_mutex_is_held = 0;
	rendercores_global_mutex = _mali_osk_lock_init(
					(_mali_osk_lock_flags_t)(_MALI_OSK_LOCKFLAG_NONINTERRUPTABLE | _MALI_OSK_LOCKFLAG_ORDERED),
					0, 129);

    if (NULL == rendercores_global_mutex)
    {
		MALI_PRINT_ERROR(("Failed: _mali_osk_lock_init\n")) ;
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
    }

	rendercore_dummy_subsystem.name = "Rendercore Global Subsystem"; /* On the constant pool, do not free */
	rendercore_dummy_subsystem.magic_nr = SUBSYSTEM_MAGIC_NR; /* To please the Subsystem Mutex code */

#if MALI_GPU_UTILIZATION
	if (mali_utilization_init() != _MALI_OSK_ERR_OK)
	{
		_mali_osk_lock_term(rendercores_global_mutex);
		rendercores_global_mutex = NULL;
		MALI_PRINT_ERROR(("Failed: mali_utilization_init\n")) ;
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}
#endif

#if MALI_TIMELINE_PROFILING_ENABLED
	if (_mali_profiling_init(mali_boot_profiling ? MALI_TRUE : MALI_FALSE) != _MALI_OSK_ERR_OK)
	{
		/* No biggie if we wheren't able to initialize the profiling */
		MALI_PRINT_ERROR(("Rendercore: Failed to initialize profiling, feature will be unavailable\n")) ;
	}
#endif

	MALI_DEBUG_PRINT(2, ("Rendercore: subsystem global mutex initialized\n")) ;
	MALI_SUCCESS;
}

/**
 * @brief Terminate the Rendercore subsystem.
 *
 * This must only be called \b after any other subsystem that extends the
 * ''rendercore'' has been terminated. For example, this must be called \b after
 * the following functions:
 * - mali200_subsystem_terminate(), from mali_subsystem_mali200
 * - maligp_subsystem_terminate(), from mali_subsystem_gp2
 *
 * @note This function is separate from mali_core_subsystem_cleanup(), though,
 * the subsystems that extend ''rendercore'' must still call
 * mali_core_subsystem_cleanup() when they terminate.
 */
static void rendercore_subsystem_terminate(mali_kernel_subsystem_identifier id)
{
	/* Catch double-terminate */
	MALI_DEBUG_ASSERT_POINTER( rendercores_global_mutex );

#if MALI_TIMELINE_PROFILING_ENABLED
	_mali_profiling_term();
#endif

#if MALI_GPU_UTILIZATION
	mali_utilization_term();
#endif

	rendercore_dummy_subsystem.name = NULL; /* The original string was on the constant pool, do not free */
	rendercore_dummy_subsystem.magic_nr = 0;

	/* ASSERT that no-one's holding this */
	MALI_DEBUG_PRINT_ASSERT( 0 == rendercores_global_mutex_is_held,
							 ("Rendercores' Global Mutex was held at termination time. Have the subsystems that extend ''rendercore'' been terminated?\n") );

	_mali_osk_lock_term( rendercores_global_mutex );
	rendercores_global_mutex = NULL;

	MALI_DEBUG_PRINT(2, ("Rendercore: subsystem global mutex terminated\n")) ;
}


#if USING_MMU
/**
 * @brief Handle certain Rendercore subsystem broadcast notifications
 *
 * When RENDERCORES_USE_GLOBAL_MUTEX is non-zero, this handles the following messages:
 * - MMU_KILL_STEP0_LOCK_SUBSYSTEM
 * - MMU_KILL_STEP4_UNLOCK_SUBSYSTEM
 *
 * The purpose is to manage the Rendercode Global Mutex, which cannot be
 * managed by any system that extends the ''rendercore''.
 *
 * All other messages must be handled by mali_core_subsystem_broadcast_notification()
 *
 *
 * When RENDERCORES_USE_GLOBAL_MUTEX is 0, this function does nothing.
 * Instead, the subsystem that extends the ''rendercore' \b must handle its
 * own mutexes - refer to mali_core_subsystem_broadcast_notification().
 *
 * Used currently only for signalling when MMU has a pagefault
 */
static void rendercore_subsystem_broadcast_notification(mali_core_notification_message message, u32 data)
{
	switch(message)
	{
		case MMU_KILL_STEP0_LOCK_SUBSYSTEM:
			lock_subsystem( &rendercore_dummy_subsystem );
			break;
		case MMU_KILL_STEP4_UNLOCK_SUBSYSTEM:
			unlock_subsystem( &rendercore_dummy_subsystem );
			break;

		case MMU_KILL_STEP1_STOP_BUS_FOR_ALL_CORES:
			/** FALLTHROUGH */
		case MMU_KILL_STEP2_RESET_ALL_CORES_AND_ABORT_THEIR_JOBS:
			/** FALLTHROUGH */
		case MMU_KILL_STEP3_CONTINUE_JOB_HANDLING:
			break;

		default:
			MALI_PRINT_ERROR(("Illegal message: 0x%x, data: 0x%x\n", (u32)message, data));
			break;
	}

}
#endif

/*
 * Functions inherited by the subsystems that extend the ''rendercore''.
 */

void mali_core_renderunit_timeout_function_hang_detection(void *arg)
{
	mali_bool action = MALI_FALSE;
	mali_core_renderunit * core;

	core = (mali_core_renderunit *) arg;
	if( !core ) return;

	/* if NOT idle OR NOT powered off OR has TIMED_OUT */
	if ( !((CORE_WATCHDOG_TIMEOUT == core->state ) || (CORE_IDLE== core->state) || (CORE_OFF == core->state)) )
	{
		core->state = CORE_HANG_CHECK_TIMEOUT;
		action = MALI_TRUE;
	}

	if(action) _mali_osk_irq_schedulework(core->irq);
}


void mali_core_renderunit_timeout_function(void *arg)
{
	mali_core_renderunit * core;
	mali_bool is_watchdog;

	core = (mali_core_renderunit *)arg;
	if( !core ) return;

	is_watchdog = MALI_TRUE;
	if (mali_benchmark)
	{
		/* poll based core */
		mali_core_job *job;
		job = core->current_job;
		if ( (NULL != job) &&
		     (0 != _mali_osk_time_after(job->watchdog_jiffies,_mali_osk_time_tickcount()))
		   )
		{
			core->state = CORE_POLL;
			is_watchdog = MALI_FALSE;
		}
	}

	if (is_watchdog)
	{
		MALI_DEBUG_PRINT(3, ("SW-Watchdog timeout: Core:%s\n", core->description));
		core->state = CORE_WATCHDOG_TIMEOUT;
	}

	_mali_osk_irq_schedulework(core->irq);
}

/* Used by external renderunit_create<> function */
_mali_osk_errcode_t mali_core_renderunit_init(mali_core_renderunit * core)
{
	MALI_DEBUG_PRINT(5, ("Core: renderunit_init: Core:%s\n", core->description));

	_MALI_OSK_INIT_LIST_HEAD(&core->list) ;
	core->timer = _mali_osk_timer_init();
    if (NULL == core->timer)
    {
	    MALI_PRINT_ERROR(("Core: renderunit_init: Core:%s -- cannot init timer\n", core->description));
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
    }

    _mali_osk_timer_setcallback(core->timer, mali_core_renderunit_timeout_function, (void *)core);

	core->timer_hang_detection = _mali_osk_timer_init();
    if (NULL == core->timer_hang_detection)
    {
        _mali_osk_timer_term(core->timer);
	    MALI_PRINT_ERROR(("Core: renderunit_init: Core:%s -- cannot init hang detection timer\n", core->description));
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
    }

    _mali_osk_timer_setcallback(core->timer_hang_detection, mali_core_renderunit_timeout_function_hang_detection, (void *)core);

#if USING_MALI_PMM
	/* Init no pending power downs */
	core->pend_power_down = MALI_FALSE;
	
	/* Register the core with the PMM - which powers it up */
	if (_MALI_OSK_ERR_OK != malipmm_core_register( core->pmm_id ))
	{
		_mali_osk_timer_term(core->timer);
		_mali_osk_timer_term(core->timer_hang_detection);
	    MALI_PRINT_ERROR(("Core: renderunit_init: Core:%s -- cannot register with PMM\n", core->description));        
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}
#endif /* USING_MALI_PMM */

	core->error_recovery = MALI_FALSE;
	core->in_detach_function = MALI_FALSE;
	core->state = CORE_IDLE;
	core->current_job = NULL;
	core->magic_nr = CORE_MAGIC_NR;
#if USING_MMU
	core->mmu = NULL;
#endif /* USING_MMU */

    MALI_SUCCESS;
}

void mali_core_renderunit_term(mali_core_renderunit * core)
{
	MALI_DEBUG_PRINT(5, ("Core: renderunit_term: Core:%s\n", core->description));

    if (NULL != core->timer)
    {
        _mali_osk_timer_term(core->timer);
        core->timer = NULL;
    }
    if (NULL != core->timer_hang_detection)
    {
        _mali_osk_timer_term(core->timer_hang_detection);
        core->timer_hang_detection = NULL;
    }

#if USING_MALI_PMM
	/* Unregister the core with the PMM */
	malipmm_core_unregister( core->pmm_id );
#endif
}

/* Used by external renderunit_create<> function */
_mali_osk_errcode_t mali_core_renderunit_map_registers(mali_core_renderunit *core)
{
	MALI_DEBUG_PRINT(3, ("Core: renderunit_map_registers: Core:%s\n", core->description)) ;
	if( (0 == core->registers_base_addr) ||
	    (0 == core->size) ||
	    (NULL == core->description)
	  )
	{
		MALI_PRINT_ERROR(("Missing fields in the core structure %u %u 0x%x;\n", core->registers_base_addr, core->size, core->description));
        MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	}

	if (_MALI_OSK_ERR_OK != _mali_osk_mem_reqregion(core->registers_base_addr, core->size, core->description))
	{
		MALI_PRINT_ERROR(("Could not request register region (0x%08X - 0x%08X) to core: %s\n",
		         core->registers_base_addr, core->registers_base_addr + core->size - 1, core->description));
        MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	}
	else
	{
		MALI_DEBUG_PRINT(6, ("Success: request_mem_region: (0x%08X - 0x%08X) Core:%s\n",
		        core->registers_base_addr, core->registers_base_addr + core->size - 1, core->description));
	}

	core->registers_mapped = _mali_osk_mem_mapioregion( core->registers_base_addr, core->size, core->description );

	if ( 0 == core->registers_mapped )
	{
		MALI_PRINT_ERROR(("Could not ioremap registers for %s .\n", core->description));
		_mali_osk_mem_unreqregion(core->registers_base_addr, core->size);
        MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}
	else
	{
		MALI_DEBUG_PRINT(6, ("Success: ioremap_nocache: Internal ptr: (0x%08X - 0x%08X) Core:%s\n",
		        (u32) core->registers_mapped,
		        ((u32)core->registers_mapped)+ core->size - 1,
		        core->description));
	}

	MALI_DEBUG_PRINT(4, ("Success: Mapping registers to core: %s\n",core->description));

	MALI_SUCCESS;
}

/* Used by external renderunit_create<> function + other places */
void mali_core_renderunit_unmap_registers(mali_core_renderunit *core)
{
	MALI_DEBUG_PRINT(3, ("Core: renderunit_unmap_registers: Core:%s\n", core->description));
	if (0 == core->registers_mapped)
	{
		MALI_PRINT_ERROR(("Trying to unmap register-mapping with NULL from core: %s\n", core->description));
		return;
	}
	_mali_osk_mem_unmapioregion(core->registers_base_addr, core->size, core->registers_mapped);
	core->registers_mapped = 0;
	_mali_osk_mem_unreqregion(core->registers_base_addr, core->size);
}

static void mali_core_renderunit_irq_handler_remove(mali_core_renderunit *core)
{
	MALI_DEBUG_PRINT(3, ("Core: renderunit_irq_handler_remove: Core:%s\n", core->description));
    _mali_osk_irq_term(core->irq);
}

mali_core_renderunit * mali_core_renderunit_get_mali_core_nr(mali_core_subsystem *subsys, u32 mali_core_nr)
{
	mali_core_renderunit * core;
	MALI_ASSERT_MUTEX_IS_GRABBED(subsys);
	if (subsys->number_of_cores <= mali_core_nr)
	{
		MALI_PRINT_ERROR(("Trying to get illegal mali_core_nr: 0x%x for %s", mali_core_nr, subsys->name));
		return NULL;
	}
	core = (subsys->mali_core_array)[mali_core_nr];
	MALI_DEBUG_PRINT(6, ("Core: renderunit_get_mali_core_nr: Core:%s\n", core->description));
	MALI_CHECK_CORE(core);
	return core;
}

/* Is used by external function:
	subsystem_startup<> */
_mali_osk_errcode_t mali_core_subsystem_init(mali_core_subsystem* new_subsys)
{
	int i;

	/* These function pointers must have been set on before calling this function */
	if (
	       ( NULL == new_subsys->name ) ||
	       ( NULL == new_subsys->start_job ) ||
	       ( NULL == new_subsys->irq_handler_upper_half ) ||
	       ( NULL == new_subsys->irq_handler_bottom_half ) ||
	       ( NULL == new_subsys->get_new_job_from_user ) ||
	       ( NULL == new_subsys->return_job_to_user )
	   )
	{
		MALI_PRINT_ERROR(("Missing functions in subsystem."));
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	MALI_DEBUG_PRINT(2, ("Core: subsystem_init: %s\n", new_subsys->name)) ;

	/* Catch use-before-initialize/use-after-terminate */
	MALI_DEBUG_ASSERT_POINTER( rendercores_global_mutex );

	new_subsys->magic_nr = SUBSYSTEM_MAGIC_NR;

	_MALI_OSK_INIT_LIST_HEAD(&new_subsys->renderunit_idle_head); /* Idle cores of this type */
	_MALI_OSK_INIT_LIST_HEAD(&new_subsys->renderunit_off_head);  /* Powered off cores of this type */

	/* Linked list for each priority of sessions with a job ready for scheduleing */
	for(i=0; i<PRIORITY_LEVELS; ++i)
	{
		_MALI_OSK_INIT_LIST_HEAD(&new_subsys->awaiting_sessions_head[i]);
	}

	/* Linked list of all sessions connected to this coretype */
	_MALI_OSK_INIT_LIST_HEAD(&new_subsys->all_sessions_head);

	MALI_SUCCESS;
}

#if USING_MMU
void mali_core_subsystem_attach_mmu(mali_core_subsystem* subsys)
{
	u32 i;
	mali_core_renderunit * core;

	MALI_CORE_SUBSYSTEM_MUTEX_GRAB(subsys);

	for(i=0 ; i < subsys->number_of_cores ; ++i)
	{
		core = mali_core_renderunit_get_mali_core_nr(subsys,i);
		if ( NULL==core ) break;
		core->mmu = mali_memory_core_mmu_lookup(core->mmu_id);
		mali_memory_core_mmu_owner(core,core->mmu);
		MALI_DEBUG_PRINT(2, ("Attach mmu: 0x%x to core: %s in subsystem: %s\n", core->mmu, core->description, subsys->name));
	}

	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys);
}
#endif

/* This will register an IRQ handler, and add the core to the list of available cores for this subsystem. */
_mali_osk_errcode_t mali_core_subsystem_register_renderunit(mali_core_subsystem* subsys, mali_core_renderunit * core)
{
	mali_core_renderunit ** mali_core_array;
	u32 previous_nr;
	u32 previous_size;
	u32 new_nr;
	u32 new_size;
	_mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT;

	/* If any of these are 0 there is an error */
	if(0 == core->subsystem ||
	   0 == core->registers_base_addr ||
	   0 == core->size ||
	   0 == core->description)
	{
		MALI_PRINT_ERROR(("Missing fields in the core structure 0x%x 0x%x 0x%x;\n",
		        core->registers_base_addr, core->size, core->description));
        MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	}

	MALI_DEBUG_PRINT(3, ("Core: subsystem_register_renderunit: %s\n", core->description));

	MALI_CHECK_NON_NULL(
        core->irq = _mali_osk_irq_init(
                            core->irq_nr,
                            mali_core_irq_handler_upper_half,
                            mali_core_irq_handler_bottom_half,
                            (_mali_osk_irq_trigger_t)subsys->probe_core_irq_trigger,
                            (_mali_osk_irq_ack_t)subsys->probe_core_irq_acknowledge,
                            core,
                            "mali_core_irq_handlers"
                            ),
        _MALI_OSK_ERR_FAULT
        );

	MALI_CORE_SUBSYSTEM_MUTEX_GRAB(subsys);

	/* Update which core number this is */
	core->core_number = subsys->number_of_cores;

	/* Update the array of cores in the subsystem. */
	previous_nr   = subsys->number_of_cores;
	previous_size = sizeof(mali_core_renderunit*)*previous_nr;
	new_nr        = previous_nr + 1;
	new_size      = sizeof(mali_core_renderunit*)*new_nr;

	if (0 != previous_nr)
	{
		if (NULL == subsys->mali_core_array)
		{
			MALI_PRINT_ERROR(("Internal error"));
			goto exit_function;
		}

		mali_core_array = (mali_core_renderunit **) _mali_osk_malloc( new_size );
		if (NULL == mali_core_array )
		{
			MALI_PRINT_ERROR(("Out of mem"));
            err = _MALI_OSK_ERR_NOMEM;
			goto exit_function;
		}
		_mali_osk_memcpy(mali_core_array, subsys->mali_core_array, previous_size);
		_mali_osk_free( subsys->mali_core_array);
		MALI_DEBUG_PRINT(5, ("Success: adding a new core to subsystem array %s\n", core->description) ) ;
	}
	else
	{
		mali_core_array = (mali_core_renderunit **) _mali_osk_malloc( new_size );
		if (NULL == mali_core_array )
		{
			MALI_PRINT_ERROR(("Out of mem"));
            err = _MALI_OSK_ERR_NOMEM;
			goto exit_function;
		}
		MALI_DEBUG_PRINT(6, ("Success: adding first core to subsystem array %s\n", core->description) ) ;
	}
	subsys->mali_core_array = mali_core_array;
	mali_core_array[previous_nr] = core;

	/* Add the core to the list of available cores on the system */
	_mali_osk_list_add(&(core->list), &(subsys->renderunit_idle_head));

	/* Update total number of cores */
	subsys->number_of_cores = new_nr;
	MALI_DEBUG_PRINT(6, ("Success: mali_core_subsystem_register_renderunit %s\n", core->description));
	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys);
    MALI_SUCCESS;

exit_function:
	mali_core_renderunit_irq_handler_remove(core);
	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys);
    MALI_ERROR(err);
}


/**
 * Called by the core when a system info update is needed
 * We fill in info about all the core types available
 * @param subsys Pointer to the core's @a mali_core_subsystem data structure
 * @param info Pointer to system info struct to update
 * @return _MALI_OSK_ERR_OK on success, or another _mali_osk_errcode_t error code on failure
 */
_mali_osk_errcode_t mali_core_subsystem_system_info_fill(mali_core_subsystem* subsys, _mali_system_info* info)
{
 	u32 i;
	_mali_osk_errcode_t err = _MALI_OSK_ERR_OK;  /* OK if no cores to update info for */
	mali_core_renderunit * core;
	_mali_core_info **core_info_nextp;
	_mali_core_info * cinfo;

	MALI_DEBUG_PRINT(4, ("mali_core_subsystem_system_info_fill: %s\n", subsys->name) ) ;

	/* check input */
    MALI_CHECK_NON_NULL(info, _MALI_OSK_ERR_INVALID_ARGS);

	core_info_nextp = &(info->core_info);
	cinfo = info->core_info;

	while(NULL!=cinfo)
	{
		core_info_nextp = &(cinfo->next);
		cinfo = cinfo->next;
	}

	MALI_CORE_SUBSYSTEM_MUTEX_GRAB(subsys);
	for(i=0 ; i < subsys->number_of_cores ; ++i)
	{
		core = mali_core_renderunit_get_mali_core_nr(subsys,i);
		if ( NULL==core )
		{
			err = _MALI_OSK_ERR_FAULT;
			goto early_exit;
		}
		cinfo = (_mali_core_info *)_mali_osk_calloc(1, sizeof(_mali_core_info));
		if ( NULL==cinfo )
		{
			err = _MALI_OSK_ERR_NOMEM;
			goto early_exit;
		}
		cinfo->version = core->core_version;
		cinfo->type =subsys->core_type;
		cinfo->reg_address = core->registers_base_addr;
		cinfo->core_nr     = i;
		cinfo->next = NULL;
		/* Writing this address to the previous' *(&next) ptr */
		*core_info_nextp = cinfo;
		/* Setting the next_ptr to point to &this->next_ptr */
		core_info_nextp = &(cinfo->next);
	}
early_exit:
	if ( _MALI_OSK_ERR_OK != err) MALI_PRINT_ERROR(("Error: In mali_core_subsystem_system_info_fill %d\n", err));
	MALI_DEBUG_CODE(
		cinfo = info->core_info;

		MALI_DEBUG_PRINT(3, ("Current list of cores\n"));
		while( NULL != cinfo )
		{
			MALI_DEBUG_PRINT(3, ("Type:     0x%x\n", cinfo->type));
			MALI_DEBUG_PRINT(3, ("Version:  0x%x\n", cinfo->version));
			MALI_DEBUG_PRINT(3, ("Reg_addr: 0x%x\n", cinfo->reg_address));
			MALI_DEBUG_PRINT(3, ("Core_nr:  0x%x\n", cinfo->core_nr));
			MALI_DEBUG_PRINT(3, ("Flags:    0x%x\n", cinfo->flags));
			MALI_DEBUG_PRINT(3, ("*****\n"));
			cinfo  = cinfo->next;
		}
	);

	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys);
    MALI_ERROR(err);
}


/* Is used by external function:
	subsystem_terminate<> */
void mali_core_subsystem_cleanup(mali_core_subsystem* subsys)
{
	u32 i;
	mali_core_renderunit * core;

	MALI_CORE_SUBSYSTEM_MUTEX_GRAB(subsys);
	MALI_DEBUG_PRINT(2, ("Core: subsystem_cleanup: %s\n", subsys->name )) ;

	for(i=0 ; i < subsys->number_of_cores ; ++i)
	{
		core = mali_core_renderunit_get_mali_core_nr(subsys,i);

#if USING_MMU
			if (NULL != core->mmu)
			{
				/* the MMU is attached in the load_complete callback, which will never be called if the module fails to load, handle that case */
				mali_memory_core_mmu_unregister_callback(core->mmu, mali_core_subsystem_callback_schedule_wrapper);
			}
#endif

		MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys);

		mali_core_renderunit_irq_handler_remove(core);

		/* When a process terminates, all cores running jobs from that process is reset and put to idle.
			That means that when the module is unloading (this code) we are guaranteed that all cores are idle.
			However: if something (we can't think of) is really wrong, a core may give an interrupt during this
			unloading, and we may now in the code have a bottom-half-processing pending from the interrupts
			we deregistered above. To be sure that the bottom halves do not access the structures after they
			are deallocated we flush the bottom-halves processing here, before the deallocation. */

		MALI_CORE_SUBSYSTEM_MUTEX_GRAB(subsys);

#if USING_MALI_PMM
			/* Only reset when we are using PMM and the core is not off */
#if MALI_PMM_NO_PMU
				/* We need to reset when there is no PMU - but this will
				 * cause the register read/write functions to report an
				 * error (hence the if to check for CORE_OFF below) we 
				 * change state to allow the reset to happen.
				 */
				core->state = CORE_IDLE;
#endif
			if( core->state != CORE_OFF )
			{
				subsys->reset_core( core, MALI_CORE_RESET_STYLE_DISABLE );
			}
#else
			/* Always reset the core */
			subsys->reset_core( core, MALI_CORE_RESET_STYLE_DISABLE );
#endif

		mali_core_renderunit_unmap_registers(core);

		_mali_osk_list_delinit(&core->list);

		mali_core_renderunit_term(core);

		subsys->renderunit_delete(core);
	}

	mali_core_subsystem_cleanup_all_renderunits(subsys);
	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys);
	MALI_DEBUG_PRINT(6, ("SUCCESS: mali_core_subsystem_cleanup: %s\n", subsys->name )) ;
}

_mali_osk_errcode_t mali_core_subsystem_ioctl_number_of_cores_get(mali_core_session * session, u32 *number_of_cores)
{
	mali_core_subsystem * subsystem;

	subsystem = session->subsystem;
	if ( NULL != number_of_cores )
	{
		*number_of_cores = subsystem->number_of_cores;

		MALI_DEBUG_PRINT(4, ("Core: ioctl_number_of_cores_get: %s: %u\n", subsystem->name, *number_of_cores) ) ;
	}

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_core_subsystem_ioctl_start_job(mali_core_session * session, void *job_data)
{
    mali_core_subsystem * subsystem;
    _mali_osk_errcode_t err;

    /* need the subsystem to run callback function */
    subsystem = session->subsystem;
    MALI_CHECK_NON_NULL(subsystem, _MALI_OSK_ERR_FAULT);

    MALI_CORE_SUBSYSTEM_MUTEX_GRAB(subsystem);
    err = subsystem->get_new_job_from_user(session, job_data);
    MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsystem);

    MALI_ERROR(err);
}


/* We return the version number to the first core in this subsystem */
_mali_osk_errcode_t mali_core_subsystem_ioctl_core_version_get(mali_core_session * session, _mali_core_version *version)
{
	mali_core_subsystem * subsystem;
	mali_core_renderunit * core0;
	u32 nr_return;

	subsystem = session->subsystem;
	MALI_CORE_SUBSYSTEM_MUTEX_GRAB(subsystem);

	core0 = mali_core_renderunit_get_mali_core_nr(subsystem, 0);

	if( NULL == core0 )
	{
		MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsystem);
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	nr_return = core0->core_version;
	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsystem);

	MALI_DEBUG_PRINT(4, ("Core: ioctl_core_version_get: %s: %u\n", subsystem->name, nr_return )) ;

	*version = nr_return;

	MALI_SUCCESS;
}

void mali_core_subsystem_ioctl_abort_job(mali_core_session * session, u32 id)
{
	find_and_abort(session, id);
}

static mali_bool job_should_be_aborted(mali_core_job *job, u32 abort_id)
{
	if ( job->abort_id == abort_id ) return MALI_TRUE;
	else return MALI_FALSE;
}

static void find_and_abort(mali_core_session* session, u32 abort_id)
{
	mali_core_subsystem * subsystem;
	mali_core_renderunit *core;
	mali_core_renderunit *tmp;
	mali_core_job *job;

	subsystem = session->subsystem;

	MALI_CORE_SUBSYSTEM_MUTEX_GRAB( subsystem );

	job = mali_job_queue_abort_job(session, abort_id);
	if (NULL != job)
	{
		MALI_DEBUG_PRINT(3, ("Core: Aborting %s job, with id nr: %u, from the waiting_to_run slot.\n", subsystem->name, abort_id ));
		if (mali_job_queue_empty(session))
		{
			_mali_osk_list_delinit(&(session->awaiting_sessions_list));
		}
		subsystem->awaiting_sessions_sum_all_priorities--;
		subsystem->return_job_to_user(job , JOB_STATUS_END_ABORT);
	}

	_MALI_OSK_LIST_FOREACHENTRY( core, tmp, &session->renderunits_working_head, mali_core_renderunit, list )
	{
		job = core->current_job;
		if ( (job!=NULL) && (job_should_be_aborted (job, abort_id) ) )
		{
			MALI_DEBUG_PRINT(3, ("Core: Aborting %s job, with id nr: %u, which is currently running on mali.\n", subsystem->name, abort_id ));
			if ( core->state==CORE_IDLE )
			{
				MALI_PRINT_ERROR(("Aborting core with running job which is idle. Must be something very wrong."));
				goto end_bug;
			}
			mali_core_renderunit_detach_job_from_core(core, SUBSYSTEM_RESCHEDULE, JOB_STATUS_END_ABORT);
		}
	}
end_bug:

	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE( subsystem );
}


_mali_osk_errcode_t mali_core_subsystem_ioctl_suspend_response(mali_core_session * session, void *argument)
{
    mali_core_subsystem * subsystem;
    _mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT;

    /* need the subsystem to run callback function */
    subsystem = session->subsystem;
    MALI_CHECK_NON_NULL(subsystem, _MALI_OSK_ERR_FAULT);

    MALI_CORE_SUBSYSTEM_MUTEX_GRAB(subsystem);
	if ( NULL != subsystem->suspend_response)
	{
		MALI_DEBUG_PRINT(4, ("MALI_IOC_CORE_CMD_SUSPEND_RESPONSE start\n"));
		err = subsystem->suspend_response(session, argument);
		MALI_DEBUG_PRINT(4, ("MALI_IOC_CORE_CMD_SUSPEND_RESPONSE end\n"));
	}

    MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsystem);

    return err;
}


/* Is used by internal function:
	mali_core_subsystem_cleanup<>s */
/* All cores should be removed before calling this function
Must hold subsystem_mutex before entering this function */
static void mali_core_subsystem_cleanup_all_renderunits(mali_core_subsystem* subsys)
{
	int i;
	_mali_osk_free(subsys->mali_core_array);
	subsys->number_of_cores = 0;

	MALI_DEBUG_PRINT(5, ("Core: subsystem_cleanup_all_renderunits: %s\n", subsys->name) ) ;
	MALI_ASSERT_MUTEX_IS_GRABBED(subsys);

	if ( ! _mali_osk_list_empty(&(subsys->renderunit_idle_head)))
	{
		MALI_PRINT_ERROR(("List renderunit_list_idle should be empty."));
		_MALI_OSK_INIT_LIST_HEAD(&(subsys->renderunit_idle_head)) ;
	}

	if ( ! _mali_osk_list_empty(&(subsys->renderunit_off_head)))
	{
		MALI_PRINT_ERROR(("List renderunit_list_off should be empty."));
		_MALI_OSK_INIT_LIST_HEAD(&(subsys->renderunit_off_head)) ;
	}

	for(i=0; i<PRIORITY_LEVELS; ++i)
	{
		if ( ! _mali_osk_list_empty(&(subsys->awaiting_sessions_head[i])))
		{
			MALI_PRINT_ERROR(("List awaiting_sessions_linkedlist should be empty."));
			_MALI_OSK_INIT_LIST_HEAD(&(subsys->awaiting_sessions_head[i])) ;
			subsys->awaiting_sessions_sum_all_priorities = 0;
		}
	}

	if ( ! _mali_osk_list_empty(&(subsys->all_sessions_head)))
	{
		MALI_PRINT_ERROR(("List all_sessions_linkedlist should be empty."));
		_MALI_OSK_INIT_LIST_HEAD(&(subsys->all_sessions_head)) ;
	}
}

/* Is used by internal functions:
	mali_core_irq_handler_bottom_half<>;
	mali_core_subsystem_schedule<>;	*/
/* Will release the core.*/
/* Must hold subsystem_mutex before entering this function */
static void mali_core_subsystem_move_core_set_idle(mali_core_renderunit *core)
{
	mali_core_subsystem *subsystem;
#if USING_MALI_PMM
	mali_core_status oldstatus;
#endif
	subsystem = core->subsystem;
	MALI_ASSERT_MUTEX_IS_GRABBED(subsystem);
	MALI_CHECK_CORE(core);
	MALI_CHECK_SUBSYSTEM(subsystem);

    _mali_osk_timer_del(core->timer);
    _mali_osk_timer_del(core->timer_hang_detection);

    MALI_DEBUG_PRINT(5, ("Core: subsystem_move_core_set_idle: %s\n", core->description) ) ;

	core->current_job = NULL ;

#if USING_MALI_PMM

	oldstatus = core->state;

	if ( !core->pend_power_down )
	{
		core->state = CORE_IDLE ;
		_mali_osk_list_move( &core->list, &subsystem->renderunit_idle_head );
	}

	if( CORE_OFF != oldstatus )
	{
		/* Message that this core is now idle or in fact off */
		_mali_uk_pmm_message_s event = {
			NULL,
			MALI_PMM_EVENT_JOB_FINISHED,
			0 };
		event.data = core->pmm_id;
		_mali_ukk_pmm_event_message( &event );
#if USING_MMU
		/* Only free the reference when entering idle state from
		 * anything other than power off
		 */
		mali_memory_core_mmu_release_address_space_reference(core->mmu);
#endif /* USING_MMU */
	}

	if( core->pend_power_down )
	{
		core->state = CORE_OFF ;
		_mali_osk_list_move( &core->list, &subsystem->renderunit_off_head );

		/* Done the move from the active queues, so the pending power down can be done */
		core->pend_power_down = MALI_FALSE;
		malipmm_core_power_down_okay( core->pmm_id );
	}

#else /* !USING_MALI_PMM */

	core->state = CORE_IDLE ;
	_mali_osk_list_move( &core->list, &subsystem->renderunit_idle_head );

#if USING_MMU
		mali_memory_core_mmu_release_address_space_reference(core->mmu);
#endif

#endif /* USING_MALI_PMM */
}

/* Must hold subsystem_mutex before entering this function */
static void mali_core_subsystem_move_set_working(mali_core_renderunit *core, mali_core_job *job)
{
	mali_core_subsystem *subsystem;
	mali_core_session *session;
	u64 time_now;

	session   = job->session;
	subsystem = core->subsystem;

	MALI_CHECK_CORE(core);
	MALI_CHECK_JOB(job);
	MALI_CHECK_SUBSYSTEM(subsystem);

	MALI_ASSERT_MUTEX_IS_GRABBED(subsystem);

	MALI_DEBUG_PRINT(5, ("Core: subsystem_move_set_working: %s\n", core->description) ) ;

	time_now = _mali_osk_time_get_ns();
	job->start_time = time_now;
#if MALI_GPU_UTILIZATION
	mali_utilization_core_start(time_now);
#endif

	core->current_job = job ;
	core->state = CORE_WORKING ;
	_mali_osk_list_move( &core->list, &session->renderunits_working_head );

}

#if USING_MALI_PMM

/* Must hold subsystem_mutex before entering this function */
static void mali_core_subsystem_move_core_set_off(mali_core_renderunit *core)
{
	mali_core_subsystem *subsystem;
	subsystem = core->subsystem;
	MALI_ASSERT_MUTEX_IS_GRABBED(subsystem);
	MALI_CHECK_CORE(core);
	MALI_CHECK_SUBSYSTEM(subsystem);

	/* Cores must be idle before powering off */
	MALI_DEBUG_ASSERT(core->state == CORE_IDLE);

	MALI_DEBUG_PRINT(5, ("Core: subsystem_move_core_set_off: %s\n", core->description) ) ;

	core->current_job = NULL ;
	core->state = CORE_OFF ;
	_mali_osk_list_move( &core->list, &subsystem->renderunit_off_head );
}

#endif /* USING_MALI_PMM */

/* Is used by internal function:
	mali_core_subsystem_schedule<>;	*/
/* Returns the job with the highest priority for the subsystem. NULL if none*/
/* Must hold subsystem_mutex before entering this function */
static mali_core_session * mali_core_subsystem_get_waiting_session(mali_core_subsystem *subsystem)
{
	int i;

	MALI_CHECK_SUBSYSTEM(subsystem);
	MALI_ASSERT_MUTEX_IS_GRABBED(subsystem);

	if ( 0 == subsystem->awaiting_sessions_sum_all_priorities )
	{
		MALI_DEBUG_PRINT(5, ("Core: subsystem_get_waiting_job: No awaiting session found\n"));
		return NULL;
	}

	for( i=0; i<PRIORITY_LEVELS ; ++i)
	{
		if (!_mali_osk_list_empty(&subsystem->awaiting_sessions_head[i]))
		{
			return _MALI_OSK_LIST_ENTRY(subsystem->awaiting_sessions_head[i].next, mali_core_session, awaiting_sessions_list);
		}
	}

	return NULL;
}

static mali_core_job * mali_core_subsystem_release_session_get_job(mali_core_subsystem *subsystem, mali_core_session * session)
{
	mali_core_job *job;
	MALI_CHECK_SUBSYSTEM(subsystem);
	MALI_ASSERT_MUTEX_IS_GRABBED(subsystem);

	job = mali_job_queue_get_job(session);
	subsystem->awaiting_sessions_sum_all_priorities--;

	if(mali_job_queue_empty(session))
	{
		/* This is the last job, so remove it from the list */
		_mali_osk_list_delinit(&session->awaiting_sessions_list);
	}
	else
	{
		if (0 == (job->flags & MALI_UK_START_JOB_FLAG_MORE_JOBS_FOLLOW))
		{
			/* There are more jobs, but the follow flag is not set, so let other sessions run their jobs first */
			_mali_osk_list_del(&(session->awaiting_sessions_list));
			_mali_osk_list_addtail(&(session->awaiting_sessions_list), &(subsystem->awaiting_sessions_head[
			                       session->queue[session->queue_head]->priority]));
		}
		/* else; keep on list, follow flag is set and there are more jobs in queue for this session */
	}

	MALI_CHECK_JOB(job);
	return job;
}

/* Is used by internal functions:
	mali_core_subsystem_schedule<> */
/* This will start the job on the core. It will also release the core if it did not start.*/
/* Must hold subsystem_mutex before entering this function */
static void mali_core_job_start_on_core(mali_core_job *job, mali_core_renderunit *core)
{
	mali_core_session *session;
	mali_core_subsystem *subsystem;
	_mali_osk_errcode_t err;
	session   = job->session;
	subsystem = core->subsystem;

	MALI_CHECK_CORE(core);
	MALI_CHECK_JOB(job);
	MALI_CHECK_SUBSYSTEM(subsystem);
	MALI_CHECK_SESSION(session);
	MALI_ASSERT_MUTEX_IS_GRABBED(subsystem);

	MALI_DEBUG_PRINT(4, ("Core: job_start_on_core: job=0x%x, session=0x%x, core=%s\n", job, session, core->description));

	MALI_DEBUG_ASSERT(NULL == core->current_job) ;
	MALI_DEBUG_ASSERT(CORE_IDLE == core->state );

	mali_core_subsystem_move_set_working(core, job);

#if defined USING_MALI400_L2_CACHE
	if (0 == (job->flags & MALI_UK_START_JOB_FLAG_NO_FLUSH))
	{
		/* Invalidate the L2 cache */
		if (_MALI_OSK_ERR_OK != mali_kernel_l2_cache_invalidate_all() )
		{
			MALI_DEBUG_PRINT(4, ("Core: Clear of L2 failed, return job. System may not be usable for some reason.\n"));
			mali_core_subsystem_move_core_set_idle(core);
			subsystem->return_job_to_user(job,JOB_STATUS_END_SYSTEM_UNUSABLE );
			return;
		}
	}
#endif

	/* Tries to start job on the core. Returns MALI_FALSE if the job could not be started */
	err = subsystem->start_job(job, core);

	if ( _MALI_OSK_ERR_OK != err )
	{
		/* This will happen only if there is something in the job object
		which make it inpossible to start. Like if it require illegal memory.*/
		MALI_DEBUG_PRINT(4, ("Core: start_job failed, return job and putting core back into idle list\n"));
		mali_core_subsystem_move_core_set_idle(core);
		subsystem->return_job_to_user(job,JOB_STATUS_END_ILLEGAL_JOB );
	}
	else
	{
		u32 delay = _mali_osk_time_mstoticks(job->watchdog_msecs)+1;
		job->watchdog_jiffies = _mali_osk_time_tickcount() + delay;
		if (mali_benchmark)
		{
			_mali_osk_timer_add(core->timer, 1);
		}
		else
		{
			_mali_osk_timer_add(core->timer, delay);
		}
	}
}

#if USING_MMU
static void mali_core_subsystem_callback_schedule_wrapper(void* sub)
{
	mali_core_subsystem * subsystem;
	subsystem = (mali_core_subsystem *)sub;
	MALI_DEBUG_PRINT(3, ("MMU: Is schedulling subsystem: %s\n", subsystem->name));
	mali_core_subsystem_schedule(subsystem);
}
#endif

/* Is used by internal function:
	mali_core_irq_handler_bottom_half
	mali_core_session_add_job
*/
/* Must hold subsystem_mutex before entering this function */
static void mali_core_subsystem_schedule(mali_core_subsystem * subsystem)
{
	mali_core_renderunit *core, *tmp;
	mali_core_session *session;
	mali_core_job *job;

	MALI_DEBUG_PRINT(5, ("Core: subsystem_schedule: %s\n", subsystem->name )) ;

	MALI_ASSERT_MUTEX_IS_GRABBED(subsystem);

	/* First check that there are sessions with jobs waiting to run */
	if ( 0 == subsystem->awaiting_sessions_sum_all_priorities)
	{
		MALI_DEBUG_PRINT(6, ("Core: No jobs available for %s\n", subsystem->name) ) ;
		return;
	}

	/* Returns the session with the highest priority job for the subsystem. NULL if none*/
	session = mali_core_subsystem_get_waiting_session(subsystem);

	if (NULL == session)
	{
		MALI_DEBUG_PRINT(6, ("Core: Schedule: No runnable job found\n"));
		return;
	}

	_MALI_OSK_LIST_FOREACHENTRY(core, tmp, &subsystem->renderunit_idle_head, mali_core_renderunit, list)
	{
#if USING_MMU
		int err = mali_memory_core_mmu_activate_page_table(core->mmu, session->mmu_session, mali_core_subsystem_callback_schedule_wrapper, subsystem);
		if (0 == err)
		{
			/* core points to a core where the MMU page table activation succeeded */
#endif
			/* This will remove the job from queue system */
			job = mali_core_subsystem_release_session_get_job(subsystem, session);
			MALI_DEBUG_ASSERT_POINTER(job);

			MALI_DEBUG_PRINT(6, ("Core: Schedule: Got a job 0x%x\n", job));

#if USING_MALI_PMM
			{
				/* Message that there is a job scheduled to run 
				 * NOTE: mali_core_job_start_on_core() can fail to start
				 * the job for several reasons, but it will move the core
				 * back to idle which will create the FINISHED message
				 * so we can still say that the job is SCHEDULED
				 */
				_mali_uk_pmm_message_s event = {
					NULL,
					MALI_PMM_EVENT_JOB_SCHEDULED,
					0 };
				event.data = core->pmm_id;
				_mali_ukk_pmm_event_message( &event );
			}
#endif
			/* This will {remove core from freelist AND start the job on the core}*/
			mali_core_job_start_on_core(job, core);

			MALI_DEBUG_PRINT(6, ("Core: Schedule: Job started, done\n"));
			return;
#if USING_MMU
		}
#endif
	}
	MALI_DEBUG_PRINT(6, ("Core: Schedule: Could not activate MMU. Scheduelling postponed to MMU, checking next.\n"));

#if USING_MALI_PMM
	{
		/* Message that there are jobs to run */
		_mali_uk_pmm_message_s event = {
			NULL,
			MALI_PMM_EVENT_JOB_QUEUED,
			0 };
		if( subsystem->core_type == _MALI_GP2 || subsystem->core_type == _MALI_400_GP )
		{
			event.data = MALI_PMM_CORE_GP;
		}
		else
		{
			/* Check the PP is supported by the PMM */
			MALI_DEBUG_ASSERT( subsystem->core_type == _MALI_200 || subsystem->core_type == _MALI_400_PP );
			/* We state that all PP cores are scheduled to inform the PMM
			 * that it may need to power something up!
			 */
			event.data = MALI_PMM_CORE_PP_ALL;
		}
		_mali_ukk_pmm_event_message( &event );
	}
#endif /* USING_MALI_PMM */

}

/* Is used by external function:
	session_begin<> */
void mali_core_session_begin(mali_core_session * session)
{
	mali_core_subsystem * subsystem;
	int i;

	subsystem = session->subsystem;
	if ( NULL == subsystem )
	{
		MALI_PRINT_ERROR(("Missing data in struct\n"));
		return;
	}
	MALI_DEBUG_PRINT(2, ("Core: session_begin: for %s\n", session->subsystem->name )) ;

	session->magic_nr = SESSION_MAGIC_NR;

	_MALI_OSK_INIT_LIST_HEAD(&session->renderunits_working_head);

	for (i = 0; i < MALI_JOB_QUEUE_SIZE; i++)
	{
		session->queue[i] = NULL;
	}
	session->queue_head = 0;
	session->queue_tail = 0;
	_MALI_OSK_INIT_LIST_HEAD(&session->awaiting_sessions_list);
	_MALI_OSK_INIT_LIST_HEAD(&session->all_sessions_list);

	MALI_CORE_SUBSYSTEM_MUTEX_GRAB(subsystem);
	_mali_osk_list_add(&session->all_sessions_list, &session->subsystem->all_sessions_head);

#if MALI_STATE_TRACKING
	_mali_osk_atomic_init(&session->jobs_received, 0);
	_mali_osk_atomic_init(&session->jobs_returned, 0);
	session->pid = _mali_osk_get_pid();
#endif

	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsystem);

	MALI_DEBUG_PRINT(5, ("Core: session_begin: for %s DONE\n", session->subsystem->name) ) ;
}

#if USING_MMU
static void mali_core_renderunit_stop_bus(mali_core_renderunit* core)
{
	core->subsystem->stop_bus(core);
}
#endif

void mali_core_session_close(mali_core_session * session)
{
	mali_core_subsystem * subsystem;
	mali_core_renderunit *core;

	subsystem = session->subsystem;
	MALI_DEBUG_ASSERT_POINTER(subsystem);

	MALI_DEBUG_PRINT(2, ("Core: session_close: for %s\n", session->subsystem->name) ) ;

	/* We must grab subsystem mutex since the list this session belongs to
	is owned by the subsystem */
	MALI_CORE_SUBSYSTEM_MUTEX_GRAB( subsystem );

	/* Remove this session from the global sessionlist */
	_mali_osk_list_delinit(&session->all_sessions_list);

	_mali_osk_list_delinit(&(session->awaiting_sessions_list));

	/* Return the potensial waiting job to user */
	while ( !mali_job_queue_empty(session) )
	{
		/* Queue not empty */
		mali_core_job *job = mali_job_queue_get_job(session);
		subsystem->return_job_to_user( job, JOB_STATUS_END_SHUTDOWN );
		subsystem->awaiting_sessions_sum_all_priorities--;
	}

	/* Kill active cores working for this session - freeing their jobs
	   Since the handling of one core also could stop jobs from another core, there is a while loop */
	while ( ! _mali_osk_list_empty(&session->renderunits_working_head) )
	{
		core = _MALI_OSK_LIST_ENTRY(session->renderunits_working_head.next, mali_core_renderunit, list);
		MALI_DEBUG_PRINT(3, ("Core: session_close: Core was working: %s\n", core->description )) ;
		mali_core_renderunit_detach_job_from_core(core, SUBSYSTEM_RESCHEDULE, JOB_STATUS_END_SHUTDOWN );
	}
	_MALI_OSK_INIT_LIST_HEAD(&session->renderunits_working_head); /* Not necessary - we will _mali_osk_free session*/

	MALI_DEBUG_PRINT(5, ("Core: session_close: for %s FINISHED\n", session->subsystem->name )) ;
	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE( subsystem );
}

/* Must hold subsystem_mutex before entering this function */
_mali_osk_errcode_t mali_core_session_add_job(mali_core_session * session, mali_core_job *job, mali_core_job **job_return)
{
	mali_core_subsystem * subsystem;

	job->magic_nr = JOB_MAGIC_NR;
	MALI_CHECK_SESSION(session);

	subsystem = session->subsystem;
	MALI_CHECK_SUBSYSTEM(subsystem);
	MALI_ASSERT_MUTEX_IS_GRABBED(subsystem);

	MALI_DEBUG_PRINT(5, ("Core: session_add_job: for %s\n", subsystem->name )) ;

	/* Setting the default value; No job to return */
	MALI_DEBUG_ASSERT_POINTER(job_return);
	*job_return = NULL;

	if (mali_job_queue_empty(session))
	{
		/* Add session to the wait list only if it didn't already have a job waiting. */
		_mali_osk_list_addtail( &(session->awaiting_sessions_list), &(subsystem->awaiting_sessions_head[job->priority]));
	}


	if (_MALI_OSK_ERR_OK != mali_job_queue_add_job(session, job))
	{
		if (mali_job_queue_empty(session))
		{
			_mali_osk_list_delinit(&(session->awaiting_sessions_list));
		}
		MALI_DEBUG_PRINT(4, ("Core: session_add_job: %s queue is full\n", subsystem->name));
		MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}

	/* Continue to add the new job as the next job from this session */
	MALI_DEBUG_PRINT(6, ("Core: session_add_job job=0x%x\n", job));

	subsystem->awaiting_sessions_sum_all_priorities++;

	mali_core_subsystem_schedule(subsystem);

	MALI_DEBUG_PRINT(6, ("Core: session_add_job: for %s FINISHED\n", session->subsystem->name )) ;

	MALI_SUCCESS;
}

static void mali_core_job_set_run_time(mali_core_job * job, u64 end_time)
{
	u32 time_used_nano_seconds;

	time_used_nano_seconds = end_time - job->start_time;
	job->render_time_usecs = time_used_nano_seconds / 1000;
}

static void mali_core_renderunit_detach_job_from_core(mali_core_renderunit* core, mali_subsystem_reschedule_option reschedule, mali_subsystem_job_end_code end_status)
{
	mali_core_job * job;
	mali_core_subsystem * subsystem;
	mali_bool already_in_detach_function;
	u64 time_now;

	MALI_DEBUG_ASSERT(CORE_IDLE != core->state);
	time_now = _mali_osk_time_get_ns();
	job = core->current_job;
	subsystem = core->subsystem;

	/* The reset_core() called some lines below might call this detach
	 * funtion again. To protect the core object from being modified by 
	 * recursive calls, the in_detach_function would track if it is an recursive call
	 */
	already_in_detach_function = core->in_detach_function;
	

	if ( MALI_FALSE == already_in_detach_function )
	{
		core->in_detach_function = MALI_TRUE;
		if ( NULL != job )
		{
			mali_core_job_set_run_time(job, time_now);
			core->current_job = NULL;
		}
	}

	if (JOB_STATUS_END_SEG_FAULT == end_status)
	{
		subsystem->reset_core( core, MALI_CORE_RESET_STYLE_HARD );
	}
	else
	{
		subsystem->reset_core( core, MALI_CORE_RESET_STYLE_RUNABLE );
	}

	if ( MALI_FALSE == already_in_detach_function )
	{
		if ( CORE_IDLE != core->state )
		{
			#if MALI_GPU_UTILIZATION
			mali_utilization_core_end(time_now);
			#endif
			mali_core_subsystem_move_core_set_idle(core);
		}

		core->in_detach_function = MALI_FALSE;

		if ( SUBSYSTEM_RESCHEDULE == reschedule )
		{
			mali_core_subsystem_schedule(subsystem);
		}
		if ( NULL != job )
		{
			core->subsystem->return_job_to_user(job, end_status);
		}
	}
}

#if USING_MMU
/* This function intentionally does not release the semaphore. You must run
   stop_bus_for_all_cores(), reset_all_cores_on_mmu() and continue_job_handling()
   after calling this function, and then call unlock_subsystem() to release the
   semaphore. */

static void lock_subsystem(struct mali_core_subsystem * subsys)
{
	MALI_CORE_SUBSYSTEM_MUTEX_GRAB(subsys);
	MALI_ASSERT_MUTEX_IS_GRABBED(subsys);
}

/* You must run lock_subsystem() before entering this function, to ensure that
   the subsystem mutex is held.
   Later, unlock_subsystem() can be called to release the mutex.

   This function only stops cores behind the given MMU, unless "mmu" is NULL, in
   which case all cores are stopped.
*/
static void stop_bus_for_all_cores_on_mmu(struct mali_core_subsystem * subsys, void* mmu)
{
	u32 i;

	MALI_ASSERT_MUTEX_IS_GRABBED(subsys);
	MALI_DEBUG_PRINT(2,("Handling: bus stop %s\n", subsys->name ));
	for(i=0 ; i < subsys->number_of_cores ; ++i)
	{
		mali_core_renderunit * core;
		core = mali_core_renderunit_get_mali_core_nr(subsys,i);

		/* We stop only cores behind the given MMU, unless MMU is NULL */
		if ( (NULL!=mmu) && (core->mmu != mmu) ) continue;

		if ( CORE_IDLE != core->state )
		{
			MALI_DEBUG_PRINT(4, ("Stopping bus on core %s\n", core->description));
			mali_core_renderunit_stop_bus(core);
			core->error_recovery = MALI_TRUE;
		}
		else
		{
			MALI_DEBUG_PRINT(4,("Core: not active %s\n", core->description ));
		}
	}
	/* Mutex is still being held, to prevent things to happen while we do cleanup */
	MALI_ASSERT_MUTEX_IS_GRABBED(subsys);
}

/* You must run lock_subsystem() before entering this function, to ensure that
   the subsystem mutex is held.
   Later, unlock_subsystem() can be called to release the mutex.

   This function only resets cores behind the given MMU, unless "mmu" is NULL, in
   which case all cores are reset.
*/
static void reset_all_cores_on_mmu(struct mali_core_subsystem * subsys, void* mmu)
{
	u32 i;

	MALI_ASSERT_MUTEX_IS_GRABBED(subsys);
	MALI_DEBUG_PRINT(3, ("Handling: reset cores from mmu: 0x%x on %s\n", mmu, subsys->name ));
	for(i=0 ; i < subsys->number_of_cores ; ++i)
	{
		mali_core_renderunit * core;
		core = mali_core_renderunit_get_mali_core_nr(subsys,i);

		/* We reset only cores behind the given MMU, unless MMU is NULL */
		if ( (NULL!=mmu) && (core->mmu != mmu) ) continue;

		if ( CORE_IDLE != core->state )
		{
			MALI_DEBUG_PRINT(4, ("Abort and reset core: %s\n", core->description ));
			mali_core_renderunit_detach_job_from_core(core, SUBSYSTEM_WAIT, JOB_STATUS_END_SEG_FAULT);
		}
		else
		{
			MALI_DEBUG_PRINT(4, ("Core: not active %s\n", core->description ));
		}
	}
	MALI_DEBUG_PRINT(4, ("Handling: done %s\n", subsys->name ));
	MALI_ASSERT_MUTEX_IS_GRABBED(subsys);
}

/* You must run lock_subsystem() before entering this function, to ensure that
   the subsystem mutex is held.
   Later, unlock_subsystem() can be called to release the mutex. */
static void continue_job_handling(struct mali_core_subsystem * subsys)
{
	u32 i, j;

	MALI_DEBUG_PRINT(3, ("Handling: Continue: %s\n", subsys->name ));
	MALI_ASSERT_MUTEX_IS_GRABBED(subsys);


	for(i=0 ; i < subsys->number_of_cores ; ++i)
	{
		mali_core_renderunit * core;
		core = mali_core_renderunit_get_mali_core_nr(subsys,i);
		core->error_recovery = MALI_FALSE;
	}

	i = subsys->number_of_cores;
	j = subsys->awaiting_sessions_sum_all_priorities;
	
	/* Schedule MIN(nr_waiting_jobs , number of cores) times */
	while( i-- && j--)
	{
		mali_core_subsystem_schedule(subsys);
	}
 	MALI_DEBUG_PRINT(4, ("Handling: done %s\n", subsys->name ));
	MALI_ASSERT_MUTEX_IS_GRABBED(subsys);
}

/* Unlock the subsystem. */
static void unlock_subsystem(struct mali_core_subsystem * subsys)
{
	MALI_ASSERT_MUTEX_IS_GRABBED(subsys);
 	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys);
}

void mali_core_subsystem_broadcast_notification(struct mali_core_subsystem * subsys, mali_core_notification_message message, u32 data)
{
	void * mmu;
	mmu = (void*) data;

	switch(message)
	{
		case MMU_KILL_STEP0_LOCK_SUBSYSTEM:
			break;
		case MMU_KILL_STEP1_STOP_BUS_FOR_ALL_CORES:
			stop_bus_for_all_cores_on_mmu(subsys, mmu);
			break;
		case MMU_KILL_STEP2_RESET_ALL_CORES_AND_ABORT_THEIR_JOBS:
			reset_all_cores_on_mmu(subsys, mmu );
			break;
		case MMU_KILL_STEP3_CONTINUE_JOB_HANDLING:
			continue_job_handling(subsys);
			break;
		case MMU_KILL_STEP4_UNLOCK_SUBSYSTEM:
			break;

		default:
			MALI_PRINT_ERROR(("Illegal message: 0x%x, data: 0x%x\n", (u32)message, data));
			break;
	}
}
#endif /* USING_MMU */

void job_watchdog_set(mali_core_job * job, u32 watchdog_msecs)
{
	if (watchdog_msecs == 0) job->watchdog_msecs = mali_max_job_runtime; /* use the default */
	else if (watchdog_msecs > WATCHDOG_MSECS_MAX) job->watchdog_msecs = WATCHDOG_MSECS_MAX; /* no larger than max */
	else if (watchdog_msecs < WATCHDOG_MSECS_MIN) job->watchdog_msecs = WATCHDOG_MSECS_MIN; /* not below min */
	else job->watchdog_msecs = watchdog_msecs;
}

u32 mali_core_hang_check_timeout_get(void)
{
	/* check the value. The user might have set the value outside the allowed range */
	if (mali_hang_check_interval > HANG_CHECK_MSECS_MAX) mali_hang_check_interval = HANG_CHECK_MSECS_MAX; /* cap to max */
	else if (mali_hang_check_interval < HANG_CHECK_MSECS_MIN) mali_hang_check_interval = HANG_CHECK_MSECS_MIN; /* cap to min */

	/* return the active value */
	return mali_hang_check_interval;
}

static _mali_osk_errcode_t  mali_core_irq_handler_upper_half (void * data)
{
	mali_core_renderunit *core;
	u32 has_pending_irq;

    core  = (mali_core_renderunit * )data;

	if(core && (CORE_OFF == core->state))
	{
		MALI_SUCCESS;
	}

	if ( (NULL == core) ||
		 (NULL == core->subsystem) ||
		 (NULL == core->subsystem->irq_handler_upper_half) )
	{
		MALI_ERROR(_MALI_OSK_ERR_INVALID_ARGS);
	}
	MALI_CHECK_CORE(core);
	MALI_CHECK_SUBSYSTEM(core->subsystem);

	has_pending_irq = core->subsystem->irq_handler_upper_half(core);

	if ( has_pending_irq )
	{
		_mali_osk_irq_schedulework( core->irq ) ;
		MALI_SUCCESS;
	}

	if (mali_benchmark) MALI_SUCCESS;

	MALI_ERROR(_MALI_OSK_ERR_FAULT);
}

static void mali_core_irq_handler_bottom_half ( void *data )
{
	mali_core_renderunit *core;
	mali_core_subsystem* subsystem;

	mali_subsystem_job_end_code job_status;

    core  = (mali_core_renderunit * )data;

	MALI_CHECK_CORE(core);
	subsystem = core->subsystem;
	MALI_CHECK_SUBSYSTEM(subsystem);

	MALI_CORE_SUBSYSTEM_MUTEX_GRAB( subsystem );
	if ( CORE_IDLE == core->state || CORE_OFF == core->state ) goto end_function;

	MALI_DEBUG_PRINT(5, ("IRQ: handling irq from core %s\n", core->description )) ;

	_mali_osk_cache_flushall();

	/* This function must also update the job status flag */
	job_status = subsystem->irq_handler_bottom_half( core );

	/* Retval is nonzero if the job is finished. */
	if ( JOB_STATUS_CONTINUE_RUN != job_status )
	{
		mali_core_renderunit_detach_job_from_core(core, SUBSYSTEM_RESCHEDULE, job_status);
	}
	else
	{
		switch ( core->state )
		{
			case CORE_WATCHDOG_TIMEOUT:
				MALI_DEBUG_PRINT(2, ("Watchdog SW Timeout of job from core: %s\n", core->description ));
				mali_core_renderunit_detach_job_from_core(core, SUBSYSTEM_RESCHEDULE, JOB_STATUS_END_TIMEOUT_SW );
				break;

			case CORE_POLL:
				MALI_DEBUG_PRINT(5, ("Poll core: %s\n", core->description )) ;
				core->state = CORE_WORKING;
				_mali_osk_timer_add( core->timer, 1);
				break;

			default:
				MALI_DEBUG_PRINT(4, ("IRQ: The job on the core continue to run: %s\n", core->description )) ;
				break;
		}
	}
end_function:
	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsystem);
}

void subsystem_flush_mapped_mem_cache(void)
{
	_mali_osk_cache_flushall();
	_mali_osk_mem_barrier();
}

#if USING_MALI_PMM

_mali_osk_errcode_t mali_core_subsystem_signal_power_down(mali_core_subsystem *subsys, u32 mali_core_nr, mali_bool immediate_only)
{
	mali_core_renderunit * core = NULL;

	MALI_CHECK_SUBSYSTEM(subsys);
	MALI_CORE_SUBSYSTEM_MUTEX_GRAB(subsys);

	/* It is possible that this signal funciton can be called during a driver exit, 
	 * and so the requested core may now be destroyed. (This is due to us not having
	 * the subsys lock before signalling power down).
	 * mali_core_renderunit_get_mali_core_nr() will report a Mali ERR because
	 * the core number is out of range (which is a valid error in other cases). 
	 * So instead we check here (now that we have the subsys lock) and let the 
	 * caller cope with the core get failure and check that the core has 
	 * been unregistered in the PMM as part of its destruction.
	 */
	if ( subsys->number_of_cores > mali_core_nr )
	{
		core = mali_core_renderunit_get_mali_core_nr(subsys, mali_core_nr);
	}

	if ( NULL == core )
	{
		/* Couldn't find the core */
		MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys);
		MALI_DEBUG_PRINT( 1, ("Core: Failed to find core to power down\n") );
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}
	else if ( core->state != CORE_IDLE )
	{
		/* When powering down we either set a pending power down flag here so we
		 * can power down cleanly after the job completes or we don't set the 
		 * flag if we have been asked to only do a power down right now
		 * In either case, return that the core is busy
		 */
		if ( !immediate_only ) core->pend_power_down = MALI_TRUE;
		MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys);
		MALI_DEBUG_PRINT( 5, ("Core: No idle core to power down\n") );
        MALI_ERROR(_MALI_OSK_ERR_BUSY);
	}

	/* Shouldn't have a pending power down flag set */
	MALI_DEBUG_ASSERT( !core->pend_power_down );

	/* Move core to off queue */
	mali_core_subsystem_move_core_set_off(core);
	
	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys);

    MALI_SUCCESS;
}
	
_mali_osk_errcode_t mali_core_subsystem_signal_power_up(mali_core_subsystem *subsys, u32 mali_core_nr, mali_bool queue_only)
{
	mali_core_renderunit * core;

	MALI_CHECK_SUBSYSTEM(subsys);
	MALI_CORE_SUBSYSTEM_MUTEX_GRAB(subsys);

	core = mali_core_renderunit_get_mali_core_nr(subsys, mali_core_nr);

	if( core == NULL )
	{
		/* Couldn't find the core */
		MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys);
		MALI_DEBUG_PRINT( 1, ("Core: Failed to find core to power up\n") );
        MALI_ERROR(_MALI_OSK_ERR_FAULT);
	}
	else if( core->state != CORE_OFF )
	{
		/* This will usually happen because we are trying to cancel a pending power down */
		core->pend_power_down = MALI_FALSE;
		MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys);
		MALI_DEBUG_PRINT( 1, ("Core: No powered off core to power up (cancelled power down?)\n") );
        MALI_ERROR(_MALI_OSK_ERR_BUSY);
	}

	/* Shouldn't have a pending power down set */
	MALI_DEBUG_ASSERT( !core->pend_power_down );

	/* Move core to idle queue */
	mali_core_subsystem_move_core_set_idle(core);

	if( !queue_only )
	{
		/* Reset MMU & core - core must be idle to allow this */
#if USING_MMU
		if ( NULL!=core->mmu )
		{
#if defined(USING_MALI200)
			if (core->pmm_id != MALI_PMM_CORE_PP0)
			{
#endif
				mali_kernel_mmu_reset(core->mmu);
#if defined(USING_MALI200)
			}
#endif

		}
#endif /* USING_MMU */
		subsys->reset_core( core, MALI_CORE_RESET_STYLE_RUNABLE );
	}

	/* Need to schedule work to start on this core */
	mali_core_subsystem_schedule(subsys);
	
	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE(subsys);

    MALI_SUCCESS;
}

#endif /* USING_MALI_PMM */

#if MALI_STATE_TRACKING
u32 mali_core_renderunit_dump_state(mali_core_subsystem* subsystem, char *buf, u32 size)
{
	u32 i, len = 0;
	mali_core_renderunit *core;
	mali_core_renderunit *tmp_core;
	
	mali_core_session* session;
	mali_core_session* tmp_session;

	if (0 >= size)
	{
		return 0;
	}

	MALI_CORE_SUBSYSTEM_MUTEX_GRAB( subsystem );

	len += _mali_osk_snprintf(buf + len, size - len, "Subsystem:\n");
	len += _mali_osk_snprintf(buf + len, size - len, "  Name: %s\n", subsystem->name);

	for (i = 0; i < subsystem->number_of_cores; i++)
	{
		len += _mali_osk_snprintf(buf + len, size - len, "  Core: #%u\n",
		                          subsystem->mali_core_array[i]->core_number);
		len += _mali_osk_snprintf(buf + len, size - len, "    Description: %s\n",
		                          subsystem->mali_core_array[i]->description);
		switch(subsystem->mali_core_array[i]->state)
		{
			case CORE_IDLE:
				len += _mali_osk_snprintf(buf + len, size - len, "    State: CORE_IDLE\n");
				break;
			case CORE_WORKING:
				len += _mali_osk_snprintf(buf + len, size - len, "    State: CORE_WORKING\n");
					break;
			case CORE_WATCHDOG_TIMEOUT:
				len += _mali_osk_snprintf(buf + len, size - len, "    State: CORE_WATCHDOG_TIMEOUT\n");
				break;
			case CORE_POLL:
				len += _mali_osk_snprintf(buf + len, size - len, "    State: CORE_POLL\n");
				break;
			case CORE_HANG_CHECK_TIMEOUT:
				len += _mali_osk_snprintf(buf + len, size - len, "    State: CORE_HANG_CHECK_TIMEOUT\n");
				break;
			case CORE_OFF:
				len += _mali_osk_snprintf(buf + len, size - len, "    State: CORE_OFF\n");
				break;
			default:
				len += _mali_osk_snprintf(buf + len, size - len, "    State: Unknown (0x%X)\n",
				                          subsystem->mali_core_array[i]->state);
				break;
		}
		len += _mali_osk_snprintf(buf + len, size - len, "    Current job: 0x%X\n",
		                          (u32)(subsystem->mali_core_array[i]->current_job));
		if (subsystem->mali_core_array[i]->current_job)
		{
			u64 time_used_nano_seconds;
			u32 time_used_micro_seconds;
			u64 time_now = _mali_osk_time_get_ns();

			time_used_nano_seconds = time_now - subsystem->mali_core_array[i]->current_job->start_time;
			time_used_micro_seconds = ((u32)(time_used_nano_seconds)) / 1000;

			len += _mali_osk_snprintf(buf + len, size - len, "      Current job session: 0x%X\n",
			                          subsystem->mali_core_array[i]->current_job->session);
			len += _mali_osk_snprintf(buf + len, size - len, "      Current job number: %d\n",
			                          subsystem->mali_core_array[i]->current_job->job_nr);
			len += _mali_osk_snprintf(buf + len, size - len, "      Current job render_time micro seconds: %d\n",
			                          time_used_micro_seconds  );
			len += _mali_osk_snprintf(buf + len, size - len, "      Current job start time micro seconds: %d\n",
			                          (u32) (subsystem->mali_core_array[i]->current_job->start_time >>10)  );
		}
		len += _mali_osk_snprintf(buf + len, size - len, "    Core version: 0x%X\n",
		                          subsystem->mali_core_array[i]->core_version);
#if USING_MALI_PMM
		len += _mali_osk_snprintf(buf + len, size - len, "    PMM id: 0x%X\n",
		                          subsystem->mali_core_array[i]->pmm_id);
		len += _mali_osk_snprintf(buf + len, size - len, "    Power down requested: %s\n",
		                          subsystem->mali_core_array[i]->pend_power_down ? "TRUE" : "FALSE");
#endif
	}

	len += _mali_osk_snprintf(buf + len, size - len, "  Cores on idle list:\n");
	_MALI_OSK_LIST_FOREACHENTRY(core, tmp_core, &subsystem->renderunit_idle_head, mali_core_renderunit, list)
	{
		len += _mali_osk_snprintf(buf + len, size - len, "    Core #%u\n", core->core_number);
	}

	len += _mali_osk_snprintf(buf + len, size - len, "  Cores on off list:\n");
	_MALI_OSK_LIST_FOREACHENTRY(core, tmp_core, &subsystem->renderunit_off_head, mali_core_renderunit, list)
	{
		len += _mali_osk_snprintf(buf + len, size - len, "    Core #%u\n", core->core_number);
	}

	len += _mali_osk_snprintf(buf + len, size - len, "  Connected sessions:\n");
	_MALI_OSK_LIST_FOREACHENTRY(session, tmp_session, &subsystem->all_sessions_head, mali_core_session, all_sessions_list)
	{
		len += _mali_osk_snprintf(buf + len, size - len,
				"    Session 0x%X:\n", (u32)session);
		len += _mali_osk_snprintf(buf + len, size - len,
				"      Queue depth: %u\n", mali_job_queue_size(session));
		len += _mali_osk_snprintf(buf + len, size - len,
				"      First waiting job: 0x%p\n", session->queue[session->queue_head]);
		len += _mali_osk_snprintf(buf + len, size - len, "      Notification queue: %s\n",
				_mali_osk_notification_queue_is_empty(session->notification_queue) ? "EMPTY" : "NON-EMPTY");
		len += _mali_osk_snprintf(buf + len, size - len,
				"      Jobs received:%4d\n", _mali_osk_atomic_read(&session->jobs_received));
		len += _mali_osk_snprintf(buf + len, size - len,
				"      Jobs started :%4d\n", _mali_osk_atomic_read(&session->jobs_started));
		len += _mali_osk_snprintf(buf + len, size - len,
				"      Jobs ended   :%4d\n", _mali_osk_atomic_read(&session->jobs_ended));
		len += _mali_osk_snprintf(buf + len, size - len,
				"      Jobs returned:%4d\n", _mali_osk_atomic_read(&session->jobs_returned));
		len += _mali_osk_snprintf(buf + len, size - len, "      PID:  %d\n", session->pid);
	}

	len += _mali_osk_snprintf(buf + len, size - len, "  Waiting sessions sum all priorities: %u\n",
			subsystem->awaiting_sessions_sum_all_priorities);
	for (i = 0; i < PRIORITY_LEVELS; i++)
	{
		len += _mali_osk_snprintf(buf + len, size - len, "    Waiting sessions with priority %u:\n", i);
		_MALI_OSK_LIST_FOREACHENTRY(session, tmp_session, &subsystem->awaiting_sessions_head[i],
				mali_core_session, awaiting_sessions_list)
		{
			len += _mali_osk_snprintf(buf + len, size - len, "      Session 0x%X:\n", (u32)session);
			len += _mali_osk_snprintf(buf + len, size - len, "        Waiting job: 0x%X\n",
					(u32)session->queue[session->queue_head]);
			len += _mali_osk_snprintf(buf + len, size - len, "        Notification queue: %s\n",
					_mali_osk_notification_queue_is_empty(session->notification_queue) ? "EMPTY" : "NON-EMPTY");
		}
	}

	MALI_CORE_SUBSYSTEM_MUTEX_RELEASE( subsystem );
	return len;
}
#endif
