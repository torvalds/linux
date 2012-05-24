/******************************************************************************
 *
 * Module Name: evmisc - Miscellaneous event manager support functions
 *
 *****************************************************************************/

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
#include "acevents.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evmisc")

/* Local prototypes */
static void ACPI_SYSTEM_XFACE acpi_ev_notify_dispatch(void *context);

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
	 * For value 0x03 (Ejection Request), may need to run a device method.
	 * For value 0x02 (Device Wake), if _PRW exists, may need to run
	 *   the _PS0 method.
	 * For value 0x80 (Status Change) on the power button or sleep button,
	 *   initiate soft-off or sleep operation.
	 *
	 * For all cases, simply dispatch the notify to the handler.
	 */
	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Dispatching Notify on [%4.4s] (%s) Value 0x%2.2X (%s) Node %p\n",
			  acpi_ut_get_node_name(node),
			  acpi_ut_get_type_name(node->type), notify_value,
			  acpi_ut_get_notify_name(notify_value), node));

	/* Get the notify object attached to the NS Node */

	obj_desc = acpi_ns_get_attached_object(node);
	if (obj_desc) {

		/* We have the notify object, Get the correct handler */

		switch (node->type) {

			/* Notify is allowed only on these types */

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
	 * If there is a handler to run, schedule the dispatcher.
	 * Check for:
	 * 1) Global system notify handler
	 * 2) Global device notify handler
	 * 3) Per-device notify handler
	 */
	if ((acpi_gbl_system_notify.handler &&
	     (notify_value <= ACPI_MAX_SYS_NOTIFY)) ||
	    (acpi_gbl_device_notify.handler &&
	     (notify_value > ACPI_MAX_SYS_NOTIFY)) || handler_obj) {
		notify_info = acpi_ut_create_generic_state();
		if (!notify_info) {
			return (AE_NO_MEMORY);
		}

		if (!handler_obj) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
					  "Executing system notify handler for Notify (%4.4s, %X) "
					  "node %p\n",
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
		/* There is no notify handler (per-device or system) for this device */

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
	 * We will invoke a global notify handler if installed. This is done
	 * _before_ we invoke the per-device handler attached to the device.
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
		struct acpi_object_notify_handler *notifier;

		notifier = &handler_obj->notify;
		while (notifier) {
			notifier->handler(notify_info->notify.node,
					  notify_info->notify.value,
					  notifier->context);
			notifier = notifier->next;
		}
	}

	/* All done with the info object */

	acpi_ut_delete_generic_state(notify_info);
}

#if (!ACPI_REDUCED_HARDWARE)
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
	u32 i;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_terminate);

	if (acpi_gbl_events_initialized) {
		/*
		 * Disable all event-related functionality. In all cases, on error,
		 * print a message but obviously we don't abort.
		 */

		/* Disable all fixed events */

		for (i = 0; i < ACPI_NUM_FIXED_EVENTS; i++) {
			status = acpi_disable_event(i, 0);
			if (ACPI_FAILURE(status)) {
				ACPI_ERROR((AE_INFO,
					    "Could not disable fixed event %u",
					    (u32) i));
			}
		}

		/* Disable all GPEs in all GPE blocks */

		status = acpi_ev_walk_gpe_list(acpi_hw_disable_gpe_block, NULL);

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

	status = acpi_ev_walk_gpe_list(acpi_ev_delete_gpe_handlers, NULL);

	/* Return to original mode if necessary */

	if (acpi_gbl_original_mode == ACPI_SYS_MODE_LEGACY) {
		status = acpi_disable();
		if (ACPI_FAILURE(status)) {
			ACPI_WARNING((AE_INFO, "AcpiDisable failed"));
		}
	}
	return_VOID;
}

#endif				/* !ACPI_REDUCED_HARDWARE */
