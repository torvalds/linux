/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_kernel_subsystem.h
 */

#ifndef __MALI_KERNEL_SUBSYSTEM_H__
#define __MALI_KERNEL_SUBSYSTEM_H__

#include "mali_osk.h"
#include "mali_uk_types.h"
#include "mali_kernel_common.h"
#include "mali_kernel_session_manager.h"

/* typedefs of the datatypes used in the hook functions */
typedef void * mali_kernel_subsystem_session_slot;
typedef int mali_kernel_subsystem_identifier;
typedef _mali_osk_errcode_t (*mali_kernel_resource_registrator)(_mali_osk_resource_t *);

/**
 * Broadcast notification messages
 */
typedef enum mali_core_notification_message
{
	MMU_KILL_STEP0_LOCK_SUBSYSTEM,                        /**< Request to lock subsystem */
	MMU_KILL_STEP1_STOP_BUS_FOR_ALL_CORES,                /**< Request to stop all buses */
	MMU_KILL_STEP2_RESET_ALL_CORES_AND_ABORT_THEIR_JOBS,  /**< Request kill all jobs, and not start more jobs */
	MMU_KILL_STEP3_CONTINUE_JOB_HANDLING,                 /**< Request to continue with new jobs on all cores */
	MMU_KILL_STEP4_UNLOCK_SUBSYSTEM                       /**< Request to unlock subsystem */
} mali_core_notification_message;

/**
 * A function pointer can be NULL if the subsystem isn't interested in the event.
 */
typedef struct mali_kernel_subsystem
{
	/* subsystem control */
	_mali_osk_errcode_t (*startup)(mali_kernel_subsystem_identifier id); /**< Called during module load or system startup*/
	void (*shutdown)(mali_kernel_subsystem_identifier id); /**< Called during module unload or system shutdown */

	/**
	 * Called during module load or system startup.
	 * Called when all subsystems have reported startup OK and all resources where successfully initialized
	*/
	_mali_osk_errcode_t (*load_complete)(mali_kernel_subsystem_identifier id);

	/* per subsystem handlers */
	_mali_osk_errcode_t (*system_info_fill)(_mali_system_info* info); /**< Fill info into info struct. MUST allocate memory with kmalloc, since it's kfree'd */

	/* per session handlers */
	/**
	 * Informs about a new session.
	 * slot can be used to track per-session per-subsystem data.
	 * queue can be used to send events to user space.
	 * _mali_osk_errcode_t error return value.
	 */
	_mali_osk_errcode_t (*session_begin)(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot, _mali_osk_notification_queue_t * queue);
	/**
	 * Informs that a session is ending
	 * slot was the same as given during session_begin
	 */
	void (*session_end)(struct mali_session_data * mali_session_data, mali_kernel_subsystem_session_slot * slot);

	/* Used by subsystems to send messages to each other. This is the receiving end */
	void (*broadcast_notification)(mali_core_notification_message message, u32 data);

#if MALI_STATE_TRACKING
	/** Dump the current state of the subsystem */
	u32 (*dump_state)(char *buf, u32 size);
#endif
} mali_kernel_subsystem;

/* functions used by the subsystems to interact with the core */
/**
 * Register a resouce handler
 * @param type The resoruce type to register a handler for
 * @param handler Pointer to the function handling this resource
 * @return _MALI_OSK_ERR_OK on success. Otherwise, a suitable _mali_osk_errcode_t error.
 */
_mali_osk_errcode_t _mali_kernel_core_register_resource_handler(_mali_osk_resource_type_t type, mali_kernel_resource_registrator handler);

/* function used to interact with other subsystems */
/**
 * Broadcast a message
 * Sends a message to all subsystems which have registered a broadcast notification handler
 * @param message The message to send
 * @param data Message specific extra data
 */
void _mali_kernel_core_broadcast_subsystem_message(mali_core_notification_message message, u32 data);

#if MALI_STATE_TRACKING
/**
 * Tell all subsystems to dump their current state
 */
u32 _mali_kernel_core_dump_state(char *buf, u32 size);
#endif


#endif /* __MALI_KERNEL_SUBSYSTEM_H__ */
