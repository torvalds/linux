/*******************************************************************************
 *
 * Module Name: utmutex - local mutex support
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2012, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#include <acpi/acpi.h>
#include "accommon.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utmutex")

/* Local prototypes */
static acpi_status acpi_ut_create_mutex(acpi_mutex_handle mutex_id);

static void acpi_ut_delete_mutex(acpi_mutex_handle mutex_id);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_mutex_initialize
 *
 * PARAMETERS:  None.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create the system mutex objects. This includes mutexes,
 *              spin locks, and reader/writer locks.
 *
 ******************************************************************************/

acpi_status acpi_ut_mutex_initialize(void)
{
	u32 i;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ut_mutex_initialize);

	/* Create each of the predefined mutex objects */

	for (i = 0; i < ACPI_NUM_MUTEX; i++) {
		status = acpi_ut_create_mutex(i);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/* Create the spinlocks for use at interrupt level */

	status = acpi_os_create_lock (&acpi_gbl_gpe_lock);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_os_create_lock (&acpi_gbl_hardware_lock);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Mutex for _OSI support */
	status = acpi_os_create_mutex(&acpi_gbl_osi_mutex);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Create the reader/writer lock for namespace access */

	status = acpi_ut_create_rw_lock(&acpi_gbl_namespace_rw_lock);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_mutex_terminate
 *
 * PARAMETERS:  None.
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete all of the system mutex objects. This includes mutexes,
 *              spin locks, and reader/writer locks.
 *
 ******************************************************************************/

void acpi_ut_mutex_terminate(void)
{
	u32 i;

	ACPI_FUNCTION_TRACE(ut_mutex_terminate);

	/* Delete each predefined mutex object */

	for (i = 0; i < ACPI_NUM_MUTEX; i++) {
		acpi_ut_delete_mutex(i);
	}

	acpi_os_delete_mutex(acpi_gbl_osi_mutex);

	/* Delete the spinlocks */

	acpi_os_delete_lock(acpi_gbl_gpe_lock);
	acpi_os_delete_lock(acpi_gbl_hardware_lock);

	/* Delete the reader/writer lock */

	acpi_ut_delete_rw_lock(&acpi_gbl_namespace_rw_lock);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_mutex
 *
 * PARAMETERS:  mutex_ID        - ID of the mutex to be created
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a mutex object.
 *
 ******************************************************************************/

static acpi_status acpi_ut_create_mutex(acpi_mutex_handle mutex_id)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE_U32(ut_create_mutex, mutex_id);

	if (!acpi_gbl_mutex_info[mutex_id].mutex) {
		status =
		    acpi_os_create_mutex(&acpi_gbl_mutex_info[mutex_id].mutex);
		acpi_gbl_mutex_info[mutex_id].thread_id =
		    ACPI_MUTEX_NOT_ACQUIRED;
		acpi_gbl_mutex_info[mutex_id].use_count = 0;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_delete_mutex
 *
 * PARAMETERS:  mutex_ID        - ID of the mutex to be deleted
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete a mutex object.
 *
 ******************************************************************************/

static void acpi_ut_delete_mutex(acpi_mutex_handle mutex_id)
{

	ACPI_FUNCTION_TRACE_U32(ut_delete_mutex, mutex_id);

	acpi_os_delete_mutex(acpi_gbl_mutex_info[mutex_id].mutex);

	acpi_gbl_mutex_info[mutex_id].mutex = NULL;
	acpi_gbl_mutex_info[mutex_id].thread_id = ACPI_MUTEX_NOT_ACQUIRED;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_acquire_mutex
 *
 * PARAMETERS:  mutex_ID        - ID of the mutex to be acquired
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Acquire a mutex object.
 *
 ******************************************************************************/

acpi_status acpi_ut_acquire_mutex(acpi_mutex_handle mutex_id)
{
	acpi_status status;
	acpi_thread_id this_thread_id;

	ACPI_FUNCTION_NAME(ut_acquire_mutex);

	if (mutex_id > ACPI_MAX_MUTEX) {
		return (AE_BAD_PARAMETER);
	}

	this_thread_id = acpi_os_get_thread_id();

#ifdef ACPI_MUTEX_DEBUG
	{
		u32 i;
		/*
		 * Mutex debug code, for internal debugging only.
		 *
		 * Deadlock prevention.  Check if this thread owns any mutexes of value
		 * greater than or equal to this one.  If so, the thread has violated
		 * the mutex ordering rule.  This indicates a coding error somewhere in
		 * the ACPI subsystem code.
		 */
		for (i = mutex_id; i < ACPI_NUM_MUTEX; i++) {
			if (acpi_gbl_mutex_info[i].thread_id == this_thread_id) {
				if (i == mutex_id) {
					ACPI_ERROR((AE_INFO,
						    "Mutex [%s] already acquired by this thread [%u]",
						    acpi_ut_get_mutex_name
						    (mutex_id),
						    (u32)this_thread_id));

					return (AE_ALREADY_ACQUIRED);
				}

				ACPI_ERROR((AE_INFO,
					    "Invalid acquire order: Thread %u owns [%s], wants [%s]",
					    (u32)this_thread_id,
					    acpi_ut_get_mutex_name(i),
					    acpi_ut_get_mutex_name(mutex_id)));

				return (AE_ACQUIRE_DEADLOCK);
			}
		}
	}
#endif

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
			  "Thread %u attempting to acquire Mutex [%s]\n",
			  (u32)this_thread_id,
			  acpi_ut_get_mutex_name(mutex_id)));

	status = acpi_os_acquire_mutex(acpi_gbl_mutex_info[mutex_id].mutex,
				       ACPI_WAIT_FOREVER);
	if (ACPI_SUCCESS(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_MUTEX,
				  "Thread %u acquired Mutex [%s]\n",
				  (u32)this_thread_id,
				  acpi_ut_get_mutex_name(mutex_id)));

		acpi_gbl_mutex_info[mutex_id].use_count++;
		acpi_gbl_mutex_info[mutex_id].thread_id = this_thread_id;
	} else {
		ACPI_EXCEPTION((AE_INFO, status,
				"Thread %u could not acquire Mutex [0x%X]",
				(u32)this_thread_id, mutex_id));
	}

	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_release_mutex
 *
 * PARAMETERS:  mutex_ID        - ID of the mutex to be released
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release a mutex object.
 *
 ******************************************************************************/

acpi_status acpi_ut_release_mutex(acpi_mutex_handle mutex_id)
{
	ACPI_FUNCTION_NAME(ut_release_mutex);

	ACPI_DEBUG_PRINT((ACPI_DB_MUTEX, "Thread %u releasing Mutex [%s]\n",
			  (u32)acpi_os_get_thread_id(),
			  acpi_ut_get_mutex_name(mutex_id)));

	if (mutex_id > ACPI_MAX_MUTEX) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Mutex must be acquired in order to release it!
	 */
	if (acpi_gbl_mutex_info[mutex_id].thread_id == ACPI_MUTEX_NOT_ACQUIRED) {
		ACPI_ERROR((AE_INFO,
			    "Mutex [0x%X] is not acquired, cannot release",
			    mutex_id));

		return (AE_NOT_ACQUIRED);
	}
#ifdef ACPI_MUTEX_DEBUG
	{
		u32 i;
		/*
		 * Mutex debug code, for internal debugging only.
		 *
		 * Deadlock prevention.  Check if this thread owns any mutexes of value
		 * greater than this one.  If so, the thread has violated the mutex
		 * ordering rule.  This indicates a coding error somewhere in
		 * the ACPI subsystem code.
		 */
		for (i = mutex_id; i < ACPI_NUM_MUTEX; i++) {
			if (acpi_gbl_mutex_info[i].thread_id ==
			    acpi_os_get_thread_id()) {
				if (i == mutex_id) {
					continue;
				}

				ACPI_ERROR((AE_INFO,
					    "Invalid release order: owns [%s], releasing [%s]",
					    acpi_ut_get_mutex_name(i),
					    acpi_ut_get_mutex_name(mutex_id)));

				return (AE_RELEASE_DEADLOCK);
			}
		}
	}
#endif

	/* Mark unlocked FIRST */

	acpi_gbl_mutex_info[mutex_id].thread_id = ACPI_MUTEX_NOT_ACQUIRED;

	acpi_os_release_mutex(acpi_gbl_mutex_info[mutex_id].mutex);
	return (AE_OK);
}
