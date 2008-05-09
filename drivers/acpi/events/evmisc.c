/******************************************************************************
 *
 * Module Name: evmisc - Miscellaneous event manager support functions
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2008, Intel Corp.
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
#include <acpi/acevents.h>
#include <acpi/acnamesp.h>
#include <acpi/acinterp.h>

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evmisc")

/* Pointer to FACS needed for the Global Lock */
static struct acpi_table_facs *facs = NULL;

/* Local prototypes */

static void ACPI_SYSTEM_XFACE acpi_ev_notify_dispatch(void *context);

static u32 acpi_ev_global_lock_handler(void *context);

static acpi_status acpi_ev_remove_global_lock_handler(void);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_is_notify_object
 *
 * PARAMETERS:  Node            - Node to check
 *
 * RETURN:      TRUE if notifies allowed on this object
 *
 * DESCRIPTION: Check type of node for a object that supports notifies.
 *
 *              TBD: This could be replaced by a flag bit in the node.
 *
 ******************************************************************************/

u8 acpi_ev_is_notify_object(struct acpi_namespace_node *node)
{
	switch (node->type) {
	case ACPI_TYPE_DEVICE:
	case ACPI_TYPE_PROCESSOR:
	case ACPI_TYPE_THERMAL:
		/*
		 * These are the ONLY objects that can receive ACPI notifications
		 */
		return (TRUE);

	default:
		return (FALSE);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_queue_notify_request
 *
 * PARAMETERS:  Node            - NS node for the notified object
 *              notify_value    - Value from the Notify() request
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dispatch a device notification event to a previously
 *              installed handler.
 *
 ******************************************************************************/

acpi_status
acpi_ev_queue_notify_request(struct acpi_namespace_node * node,
			     u32 notify_value)
{
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *handler_obj = NULL;
	union acpi_generic_state *notify_info;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_NAME(ev_queue_notify_request);

	/*
	 * For value 3 (Ejection Request), some device method may need to be run.
	 * For value 2 (Device Wake) if _PRW exists, the _PS0 method may need
	 *   to be run.
	 * For value 0x80 (Status Change) on the power button or sleep button,
	 *   initiate soft-off or sleep operation?
	 */
	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Dispatching Notify on [%4.4s] Node %p Value 0x%2.2X (%s)\n",
			  acpi_ut_get_node_name(node), node, notify_value,
			  acpi_ut_get_notify_name(notify_value)));

	/* Get the notify object attached to the NS Node */

	obj_desc = acpi_ns_get_attached_object(node);
	if (obj_desc) {

		/* We have the notify object, Get the right handler */

		switch (node->type) {

			/* Notify allowed only on these types */

		case ACPI_TYPE_DEVICE:
		case ACPI_TYPE_THERMAL:
		case ACPI_TYPE_PROCESSOR:

			if (notify_value <= ACPI_MAX_SYS_NOTIFY) {
				handler_obj =
				    obj_desc->common_notify.system_notify;
			} else {
				handler_obj =
				    obj_desc->common_notify.device_notify;
			}
			break;

		default:
			/* All other types are not supported */
			return (AE_TYPE);
		}
	}

	/*
	 * If there is any handler to run, schedule the dispatcher.
	 * Check for:
	 * 1) Global system notify handler
	 * 2) Global device notify handler
	 * 3) Per-device notify handler
	 */
	if ((acpi_gbl_system_notify.handler
	     && (notify_value <= ACPI_MAX_SYS_NOTIFY))
	    || (acpi_gbl_device_notify.handler
		&& (notify_value > ACPI_MAX_SYS_NOTIFY)) || handler_obj) {
		notify_info = acpi_ut_create_generic_state();
		if (!notify_info) {
			return (AE_NO_MEMORY);
		}

		if (!handler_obj) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "Executing system notify handler for Notify (%4.4s, %X) node %p\n",
					  acpi_ut_get_node_name(node),
					  notify_value, node));
		}

		notify_info->common.descriptor_type =
		    ACPI_DESC_TYPE_STATE_NOTIFY;
		notify_info->notify.node = node;
		notify_info->notify.value = (u16) notify_value;
		notify_info->notify.handler_obj = handler_obj;

		status =
		    acpi_os_execute(OSL_NOTIFY_HANDLER, acpi_ev_notify_dispatch,
				    notify_info);
		if (ACPI_FAILURE(status)) {
			acpi_ut_delete_generic_state(notify_info);
		}
	} else {
		/*
		 * There is no notify handler (per-device or system) for this device.
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "No notify handler for Notify (%4.4s, %X) node %p\n",
				  acpi_ut_get_node_name(node), notify_value,
				  node));
	}

	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_notify_dispatch
 *
 * PARAMETERS:  Context         - To be passed to the notify handler
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Dispatch a device notification event to a previously
 *              installed handler.
 *
 ******************************************************************************/

static void ACPI_SYSTEM_XFACE acpi_ev_notify_dispatch(void *context)
{
	union acpi_generic_state *notify_info =
	    (union acpi_generic_state *)context;
	acpi_notify_handler global_handler = NULL;
	void *global_context = NULL;
	union acpi_operand_object *handler_obj;

	ACPI_FUNCTION_ENTRY();

	/*
	 * We will invoke a global notify handler if installed.
	 * This is done _before_ we invoke the per-device handler attached
	 * to the device.
	 */
	if (notify_info->notify.value <= ACPI_MAX_SYS_NOTIFY) {

		/* Global system notification handler */

		if (acpi_gbl_system_notify.handler) {
			global_handler = acpi_gbl_system_notify.handler;
			global_context = acpi_gbl_system_notify.context;
		}
	} else {
		/* Global driver notification handler */

		if (acpi_gbl_device_notify.handler) {
			global_handler = acpi_gbl_device_notify.handler;
			global_context = acpi_gbl_device_notify.context;
		}
	}

	/* Invoke the system handler first, if present */

	if (global_handler) {
		global_handler(notify_info->notify.node,
			       notify_info->notify.value, global_context);
	}

	/* Now invoke the per-device handler, if present */

	handler_obj = notify_info->notify.handler_obj;
	if (handler_obj) {
		handler_obj->notify.handler(notify_info->notify.node,
					    notify_info->notify.value,
					    handler_obj->notify.context);
	}

	/* All done with the info object */

	acpi_ut_delete_generic_state(notify_info);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_global_lock_handler
 *
 * PARAMETERS:  Context         - From thread interface, not used
 *
 * RETURN:      ACPI_INTERRUPT_HANDLED
 *
 * DESCRIPTION: Invoked directly from the SCI handler when a global lock
 *              release interrupt occurs. Attempt to acquire the global lock,
 *              if successful, signal the thread waiting for the lock.
 *
 * NOTE: Assumes that the semaphore can be signaled from interrupt level. If
 * this is not possible for some reason, a separate thread will have to be
 * scheduled to do this.
 *
 ******************************************************************************/

static u32 acpi_ev_global_lock_handler(void *context)
{
	u8 acquired = FALSE;

	/*
	 * Attempt to get the lock.
	 *
	 * If we don't get it now, it will be marked pending and we will
	 * take another interrupt when it becomes free.
	 */
	ACPI_ACQUIRE_GLOBAL_LOCK(facs, acquired);
	if (acquired) {

		/* Got the lock, now wake all threads waiting for it */

		acpi_gbl_global_lock_acquired = TRUE;
		/* Send a unit to the semaphore */

		if (ACPI_FAILURE
		    (acpi_os_signal_semaphore
		     (acpi_gbl_global_lock_semaphore, 1))) {
			ACPI_ERROR((AE_INFO,
				    "Could not signal Global Lock semaphore"));
		}
	}

	return (ACPI_INTERRUPT_HANDLED);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_init_global_lock_handler
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for the global lock release event
 *
 ******************************************************************************/

acpi_status acpi_ev_init_global_lock_handler(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_init_global_lock_handler);

	status = acpi_get_table_by_index(ACPI_TABLE_INDEX_FACS,
					 ACPI_CAST_INDIRECT_PTR(struct
								acpi_table_header,
								&facs));
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	acpi_gbl_global_lock_present = TRUE;
	status = acpi_install_fixed_event_handler(ACPI_EVENT_GLOBAL,
						  acpi_ev_global_lock_handler,
						  NULL);

	/*
	 * If the global lock does not exist on this platform, the attempt
	 * to enable GBL_STATUS will fail (the GBL_ENABLE bit will not stick)
	 * Map to AE_OK, but mark global lock as not present.
	 * Any attempt to actually use the global lock will be flagged
	 * with an error.
	 */
	if (status == AE_NO_HARDWARE_RESPONSE) {
		ACPI_ERROR((AE_INFO,
			    "No response from Global Lock hardware, disabling lock"));

		acpi_gbl_global_lock_present = FALSE;
		status = AE_OK;
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_remove_global_lock_handler
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove the handler for the Global Lock
 *
 ******************************************************************************/

static acpi_status acpi_ev_remove_global_lock_handler(void)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_remove_global_lock_handler);

	acpi_gbl_global_lock_present = FALSE;
	status = acpi_remove_fixed_event_handler(ACPI_EVENT_GLOBAL,
						 acpi_ev_global_lock_handler);

	return_ACPI_STATUS(status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ev_acquire_global_lock
 *
 * PARAMETERS:  Timeout         - Max time to wait for the lock, in millisec.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Attempt to gain ownership of the Global Lock.
 *
 * MUTEX:       Interpreter must be locked
 *
 * Note: The original implementation allowed multiple threads to "acquire" the
 * Global Lock, and the OS would hold the lock until the last thread had
 * released it. However, this could potentially starve the BIOS out of the
 * lock, especially in the case where there is a tight handshake between the
 * Embedded Controller driver and the BIOS. Therefore, this implementation
 * allows only one thread to acquire the HW Global Lock at a time, and makes
 * the global lock appear as a standard mutex on the OS side.
 *
 *****************************************************************************/
static acpi_thread_id acpi_ev_global_lock_thread_id;
static int acpi_ev_global_lock_acquired;

acpi_status acpi_ev_acquire_global_lock(u16 timeout)
{
	acpi_status status = AE_OK;
	u8 acquired = FALSE;

	ACPI_FUNCTION_TRACE(ev_acquire_global_lock);

	/*
	 * Only one thread can acquire the GL at a time, the global_lock_mutex
	 * enforces this. This interface releases the interpreter if we must wait.
	 */
	status = acpi_ex_system_wait_mutex(
			acpi_gbl_global_lock_mutex->mutex.os_mutex, 0);
	if (status == AE_TIME) {
		if (acpi_ev_global_lock_thread_id == acpi_os_get_thread_id()) {
			acpi_ev_global_lock_acquired++;
			return AE_OK;
		}
	}

	if (ACPI_FAILURE(status)) {
		status = acpi_ex_system_wait_mutex(
				acpi_gbl_global_lock_mutex->mutex.os_mutex,
				timeout);
	}
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	acpi_ev_global_lock_thread_id = acpi_os_get_thread_id();
	acpi_ev_global_lock_acquired++;

	/*
	 * Update the global lock handle and check for wraparound. The handle is
	 * only used for the external global lock interfaces, but it is updated
	 * here to properly handle the case where a single thread may acquire the
	 * lock via both the AML and the acpi_acquire_global_lock interfaces. The
	 * handle is therefore updated on the first acquire from a given thread
	 * regardless of where the acquisition request originated.
	 */
	acpi_gbl_global_lock_handle++;
	if (acpi_gbl_global_lock_handle == 0) {
		acpi_gbl_global_lock_handle = 1;
	}

	/*
	 * Make sure that a global lock actually exists. If not, just treat
	 * the lock as a standard mutex.
	 */
	if (!acpi_gbl_global_lock_present) {
		acpi_gbl_global_lock_acquired = TRUE;
		return_ACPI_STATUS(AE_OK);
	}

	/* Attempt to acquire the actual hardware lock */

	ACPI_ACQUIRE_GLOBAL_LOCK(facs, acquired);
	if (acquired) {

		/* We got the lock */

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "Acquired hardware Global Lock\n"));

		acpi_gbl_global_lock_acquired = TRUE;
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Did not get the lock. The pending bit was set above, and we must now
	 * wait until we get the global lock released interrupt.
	 */
	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Waiting for hardware Global Lock\n"));

	/*
	 * Wait for handshake with the global lock interrupt handler.
	 * This interface releases the interpreter if we must wait.
	 */
	status = acpi_ex_system_wait_semaphore(acpi_gbl_global_lock_semaphore,
					       ACPI_WAIT_FOREVER);

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_release_global_lock
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Releases ownership of the Global Lock.
 *
 ******************************************************************************/

acpi_status acpi_ev_release_global_lock(void)
{
	u8 pending = FALSE;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(ev_release_global_lock);

	/* Lock must be already acquired */

	if (!acpi_gbl_global_lock_acquired) {
		ACPI_WARNING((AE_INFO,
			      "Cannot release the ACPI Global Lock, it has not been acquired"));
		return_ACPI_STATUS(AE_NOT_ACQUIRED);
	}

	acpi_ev_global_lock_acquired--;
	if (acpi_ev_global_lock_acquired > 0) {
		return AE_OK;
	}

	if (acpi_gbl_global_lock_present) {

		/* Allow any thread to release the lock */

		ACPI_RELEASE_GLOBAL_LOCK(facs, pending);

		/*
		 * If the pending bit was set, we must write GBL_RLS to the control
		 * register
		 */
		if (pending) {
			status =
			    acpi_set_register(ACPI_BITREG_GLOBAL_LOCK_RELEASE,
					      1);
		}

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "Released hardware Global Lock\n"));
	}

	acpi_gbl_global_lock_acquired = FALSE;

	/* Release the local GL mutex */
	acpi_ev_global_lock_thread_id = NULL;
	acpi_ev_global_lock_acquired = 0;
	acpi_os_release_mutex(acpi_gbl_global_lock_mutex->mutex.os_mutex);
	return_ACPI_STATUS(status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_ev_terminate
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Disable events and free memory allocated for table storage.
 *
 ******************************************************************************/

void acpi_ev_terminate(void)
{
	acpi_native_uint i;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_terminate);

	if (acpi_gbl_events_initialized) {
		/*
		 * Disable all event-related functionality.
		 * In all cases, on error, print a message but obviously we don't abort.
		 */

		/* Disable all fixed events */

		for (i = 0; i < ACPI_NUM_FIXED_EVENTS; i++) {
			status = acpi_disable_event((u32) i, 0);
			if (ACPI_FAILURE(status)) {
				ACPI_ERROR((AE_INFO,
					    "Could not disable fixed event %d",
					    (u32) i));
			}
		}

		/* Disable all GPEs in all GPE blocks */

		status = acpi_ev_walk_gpe_list(acpi_hw_disable_gpe_block);

		/* Remove SCI handler */

		status = acpi_ev_remove_sci_handler();
		if (ACPI_FAILURE(status)) {
			ACPI_ERROR((AE_INFO, "Could not remove SCI handler"));
		}

		status = acpi_ev_remove_global_lock_handler();
		if (ACPI_FAILURE(status)) {
			ACPI_ERROR((AE_INFO,
				    "Could not remove Global Lock handler"));
		}
	}

	/* Deallocate all handler objects installed within GPE info structs */

	status = acpi_ev_walk_gpe_list(acpi_ev_delete_gpe_handlers);

	/* Return to original mode if necessary */

	if (acpi_gbl_original_mode == ACPI_SYS_MODE_LEGACY) {
		status = acpi_disable();
		if (ACPI_FAILURE(status)) {
			ACPI_WARNING((AE_INFO, "AcpiDisable failed"));
		}
	}
	return_VOID;
}
