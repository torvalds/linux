/******************************************************************************
 *
 * Module Name: evmisc - Miscellaneous event manager support functions
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2006, R. Byron Moore
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

#ifdef ACPI_DEBUG_OUTPUT
static const char *acpi_notify_value_names[] = {
	"Bus Check",
	"Device Check",
	"Device Wake",
	"Eject request",
	"Device Check Light",
	"Frequency Mismatch",
	"Bus Mode Mismatch",
	"Power Fault"
};
#endif

/* Local prototypes */

static void ACPI_SYSTEM_XFACE acpi_ev_notify_dispatch(void *context);

static void ACPI_SYSTEM_XFACE acpi_ev_global_lock_thread(void *context);

static u32 acpi_ev_global_lock_handler(void *context);

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
	case ACPI_TYPE_POWER:
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
			  "Dispatching Notify(%X) on node %p\n", notify_value,
			  node));

	if (notify_value <= 7) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Notify value: %s\n",
				  acpi_notify_value_names[notify_value]));
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Notify value: 0x%2.2X **Device Specific**\n",
				  notify_value));
	}

	/* Get the notify object attached to the NS Node */

	obj_desc = acpi_ns_get_attached_object(node);
	if (obj_desc) {

		/* We have the notify object, Get the right handler */

		switch (node->type) {
		case ACPI_TYPE_DEVICE:
		case ACPI_TYPE_THERMAL:
		case ACPI_TYPE_PROCESSOR:
		case ACPI_TYPE_POWER:

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

	/* If there is any handler to run, schedule the dispatcher */

	if ((acpi_gbl_system_notify.handler
	     && (notify_value <= ACPI_MAX_SYS_NOTIFY))
	    || (acpi_gbl_device_notify.handler
		&& (notify_value > ACPI_MAX_SYS_NOTIFY)) || handler_obj) {
		notify_info = acpi_ut_create_generic_state();
		if (!notify_info) {
			return (AE_NO_MEMORY);
		}

		notify_info->common.descriptor_type =
		    ACPI_DESC_TYPE_STATE_NOTIFY;
		notify_info->notify.node = node;
		notify_info->notify.value = (u16) notify_value;
		notify_info->notify.handler_obj = handler_obj;

		status = acpi_os_execute(OSL_NOTIFY_HANDLER,
					 acpi_ev_notify_dispatch, notify_info);
		if (ACPI_FAILURE(status)) {
			acpi_ut_delete_generic_state(notify_info);
		}
	}

	if (!handler_obj) {
		/*
		 * There is no per-device notify handler for this device.
		 * This may or may not be a problem.
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "No notify handler for Notify(%4.4s, %X) node %p\n",
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
 * FUNCTION:    acpi_ev_global_lock_thread
 *
 * PARAMETERS:  Context         - From thread interface, not used
 *
 * RETURN:      None
 *
 * DESCRIPTION: Invoked by SCI interrupt handler upon acquisition of the
 *              Global Lock.  Simply signal all threads that are waiting
 *              for the lock.
 *
 ******************************************************************************/

static void ACPI_SYSTEM_XFACE acpi_ev_global_lock_thread(void *context)
{
	acpi_status status;

	/* Signal threads that are waiting for the lock */

	if (acpi_gbl_global_lock_thread_count) {

		/* Send sufficient units to the semaphore */

		status =
		    acpi_os_signal_semaphore(acpi_gbl_global_lock_semaphore,
					     acpi_gbl_global_lock_thread_count);
		if (ACPI_FAILURE(status)) {
			ACPI_ERROR((AE_INFO,
				    "Could not signal Global Lock semaphore"));
		}
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_global_lock_handler
 *
 * PARAMETERS:  Context         - From thread interface, not used
 *
 * RETURN:      ACPI_INTERRUPT_HANDLED or ACPI_INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Invoked directly from the SCI handler when a global lock
 *              release interrupt occurs.  Grab the global lock and queue
 *              the global lock thread for execution
 *
 ******************************************************************************/

static u32 acpi_ev_global_lock_handler(void *context)
{
	u8 acquired = FALSE;
	acpi_status status;

	/*
	 * Attempt to get the lock
	 * If we don't get it now, it will be marked pending and we will
	 * take another interrupt when it becomes free.
	 */
	ACPI_ACQUIRE_GLOBAL_LOCK(acpi_gbl_common_fACS.global_lock, acquired);
	if (acquired) {

		/* Got the lock, now wake all threads waiting for it */

		acpi_gbl_global_lock_acquired = TRUE;

		/* Run the Global Lock thread which will signal all waiting threads */

		status = acpi_os_execute(OSL_GLOBAL_LOCK_HANDLER,
					 acpi_ev_global_lock_thread, context);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Could not queue Global Lock thread"));

			return (ACPI_INTERRUPT_NOT_HANDLED);
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
 *****************************************************************************/

acpi_status acpi_ev_acquire_global_lock(u16 timeout)
{
	acpi_status status = AE_OK;
	u8 acquired = FALSE;

	ACPI_FUNCTION_TRACE(ev_acquire_global_lock);

#ifndef ACPI_APPLICATION
	/* Make sure that we actually have a global lock */

	if (!acpi_gbl_global_lock_present) {
		return_ACPI_STATUS(AE_NO_GLOBAL_LOCK);
	}
#endif

	/* One more thread wants the global lock */

	acpi_gbl_global_lock_thread_count++;

	/*
	 * If we (OS side vs. BIOS side) have the hardware lock already,
	 * we are done
	 */
	if (acpi_gbl_global_lock_acquired) {
		return_ACPI_STATUS(AE_OK);
	}

	/* We must acquire the actual hardware lock */

	ACPI_ACQUIRE_GLOBAL_LOCK(acpi_gbl_common_fACS.global_lock, acquired);
	if (acquired) {

		/* We got the lock */

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "Acquired the HW Global Lock\n"));

		acpi_gbl_global_lock_acquired = TRUE;
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Did not get the lock.  The pending bit was set above, and we must now
	 * wait until we get the global lock released interrupt.
	 */
	ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Waiting for the HW Global Lock\n"));

	/*
	 * Acquire the global lock semaphore first.
	 * Since this wait will block, we must release the interpreter
	 */
	status = acpi_ex_system_wait_semaphore(acpi_gbl_global_lock_semaphore,
					       timeout);
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

	if (!acpi_gbl_global_lock_thread_count) {
		ACPI_WARNING((AE_INFO,
			      "Cannot release HW Global Lock, it has not been acquired"));
		return_ACPI_STATUS(AE_NOT_ACQUIRED);
	}

	/* One fewer thread has the global lock */

	acpi_gbl_global_lock_thread_count--;
	if (acpi_gbl_global_lock_thread_count) {

		/* There are still some threads holding the lock, cannot release */

		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * No more threads holding lock, we can do the actual hardware
	 * release
	 */
	ACPI_RELEASE_GLOBAL_LOCK(acpi_gbl_common_fACS.global_lock, pending);
	acpi_gbl_global_lock_acquired = FALSE;

	/*
	 * If the pending bit was set, we must write GBL_RLS to the control
	 * register
	 */
	if (pending) {
		status = acpi_set_register(ACPI_BITREG_GLOBAL_LOCK_RELEASE,
					   1, ACPI_MTX_LOCK);
	}

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
