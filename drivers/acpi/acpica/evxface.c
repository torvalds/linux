/******************************************************************************
 *
 * Module Name: evxface - External interfaces for ACPI events
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2011, Intel Corp.
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

#include <linux/export.h>
#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"
#include "acevents.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evxface")

/*******************************************************************************
 *
 * FUNCTION:    acpi_install_exception_handler
 *
 * PARAMETERS:  Handler         - Pointer to the handler function for the
 *                                event
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Saves the pointer to the handler function
 *
 ******************************************************************************/
#ifdef ACPI_FUTURE_USAGE
acpi_status acpi_install_exception_handler(acpi_exception_handler handler)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_install_exception_handler);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Don't allow two handlers. */

	if (acpi_gbl_exception_handler) {
		status = AE_ALREADY_EXISTS;
		goto cleanup;
	}

	/* Install the handler */

	acpi_gbl_exception_handler = handler;

      cleanup:
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_install_exception_handler)
#endif				/*  ACPI_FUTURE_USAGE  */

/*******************************************************************************
 *
 * FUNCTION:    acpi_install_global_event_handler
 *
 * PARAMETERS:  Handler         - Pointer to the global event handler function
 *              Context         - Value passed to the handler on each event
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Saves the pointer to the handler function. The global handler
 *              is invoked upon each incoming GPE and Fixed Event. It is
 *              invoked at interrupt level at the time of the event dispatch.
 *              Can be used to update event counters, etc.
 *
 ******************************************************************************/
acpi_status
acpi_install_global_event_handler(ACPI_GBL_EVENT_HANDLER handler, void *context)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_install_global_event_handler);

	/* Parameter validation */

	if (!handler) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Don't allow two handlers. */

	if (acpi_gbl_global_event_handler) {
		status = AE_ALREADY_EXISTS;
		goto cleanup;
	}

	acpi_gbl_global_event_handler = handler;
	acpi_gbl_global_event_handler_context = context;

      cleanup:
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_install_global_event_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_install_fixed_event_handler
 *
 * PARAMETERS:  Event           - Event type to enable.
 *              Handler         - Pointer to the handler function for the
 *                                event
 *              Context         - Value passed to the handler on each GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Saves the pointer to the handler function and then enables the
 *              event.
 *
 ******************************************************************************/
acpi_status
acpi_install_fixed_event_handler(u32 event,
				 acpi_event_handler handler, void *context)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_install_fixed_event_handler);

	/* Parameter validation */

	if (event > ACPI_EVENT_MAX) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Don't allow two handlers. */

	if (NULL != acpi_gbl_fixed_event_handlers[event].handler) {
		status = AE_ALREADY_EXISTS;
		goto cleanup;
	}

	/* Install the handler before enabling the event */

	acpi_gbl_fixed_event_handlers[event].handler = handler;
	acpi_gbl_fixed_event_handlers[event].context = context;

	status = acpi_clear_event(event);
	if (ACPI_SUCCESS(status))
		status = acpi_enable_event(event, 0);
	if (ACPI_FAILURE(status)) {
		ACPI_WARNING((AE_INFO, "Could not enable fixed event 0x%X",
			      event));

		/* Remove the handler */

		acpi_gbl_fixed_event_handlers[event].handler = NULL;
		acpi_gbl_fixed_event_handlers[event].context = NULL;
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Enabled fixed event %X, Handler=%p\n", event,
				  handler));
	}

      cleanup:
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_install_fixed_event_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_remove_fixed_event_handler
 *
 * PARAMETERS:  Event           - Event type to disable.
 *              Handler         - Address of the handler
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disables the event and unregisters the event handler.
 *
 ******************************************************************************/
acpi_status
acpi_remove_fixed_event_handler(u32 event, acpi_event_handler handler)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(acpi_remove_fixed_event_handler);

	/* Parameter validation */

	if (event > ACPI_EVENT_MAX) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Disable the event before removing the handler */

	status = acpi_disable_event(event, 0);

	/* Always Remove the handler */

	acpi_gbl_fixed_event_handlers[event].handler = NULL;
	acpi_gbl_fixed_event_handlers[event].context = NULL;

	if (ACPI_FAILURE(status)) {
		ACPI_WARNING((AE_INFO,
			      "Could not write to fixed event enable register 0x%X",
			      event));
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Disabled fixed event %X\n",
				  event));
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_remove_fixed_event_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_populate_handler_object
 *
 * PARAMETERS:  handler_obj        - Handler object to populate
 *              handler_type       - The type of handler:
 *                                  ACPI_SYSTEM_NOTIFY: system_handler (00-7f)
 *                                  ACPI_DEVICE_NOTIFY: driver_handler (80-ff)
 *                                  ACPI_ALL_NOTIFY:  both system and device
 *              handler            - Address of the handler
 *              context            - Value passed to the handler on each GPE
 *              next               - Address of a handler object to link to
 *
 * RETURN:      None
 *
 * DESCRIPTION: Populate a handler object.
 *
 ******************************************************************************/
static void
acpi_populate_handler_object(struct acpi_object_notify_handler *handler_obj,
			     u32 handler_type,
			     acpi_notify_handler handler, void *context,
			     struct acpi_object_notify_handler *next)
{
	handler_obj->handler_type = handler_type;
	handler_obj->handler = handler;
	handler_obj->context = context;
	handler_obj->next = next;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_add_handler_object
 *
 * PARAMETERS:  parent_obj         - Parent of the new object
 *              handler            - Address of the handler
 *              context            - Value passed to the handler on each GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a new handler object and populate it.
 *
 ******************************************************************************/
static acpi_status
acpi_add_handler_object(struct acpi_object_notify_handler *parent_obj,
			acpi_notify_handler handler, void *context)
{
	struct acpi_object_notify_handler *handler_obj;

	/* The parent must not be a defice notify handler object. */
	if (parent_obj->handler_type & ACPI_DEVICE_NOTIFY)
		return AE_BAD_PARAMETER;

	handler_obj = ACPI_ALLOCATE_ZEROED(sizeof(*handler_obj));
	if (!handler_obj)
		return AE_NO_MEMORY;

	acpi_populate_handler_object(handler_obj,
					ACPI_SYSTEM_NOTIFY,
					handler, context,
					parent_obj->next);
	parent_obj->next = handler_obj;

	return AE_OK;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_install_notify_handler
 *
 * PARAMETERS:  Device          - The device for which notifies will be handled
 *              handler_type    - The type of handler:
 *                                  ACPI_SYSTEM_NOTIFY: system_handler (00-7f)
 *                                  ACPI_DEVICE_NOTIFY: driver_handler (80-ff)
 *                                  ACPI_ALL_NOTIFY:  both system and device
 *              Handler         - Address of the handler
 *              Context         - Value passed to the handler on each GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for notifies on an ACPI device
 *
 ******************************************************************************/
acpi_status
acpi_install_notify_handler(acpi_handle device,
			    u32 handler_type,
			    acpi_notify_handler handler, void *context)
{
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *notify_obj;
	struct acpi_namespace_node *node;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_install_notify_handler);

	/* Parameter validation */

	if ((!device) ||
	    (!handler) || (handler_type > ACPI_MAX_NOTIFY_HANDLER_TYPE)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Convert and validate the device handle */

	node = acpi_ns_validate_handle(device);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/*
	 * Root Object:
	 * Registering a notify handler on the root object indicates that the
	 * caller wishes to receive notifications for all objects. Note that
	 * only one <external> global handler can be regsitered (per notify type).
	 */
	if (device == ACPI_ROOT_OBJECT) {

		/* Make sure the handler is not already installed */

		if (((handler_type & ACPI_SYSTEM_NOTIFY) &&
		     acpi_gbl_system_notify.handler) ||
		    ((handler_type & ACPI_DEVICE_NOTIFY) &&
		     acpi_gbl_device_notify.handler)) {
			status = AE_ALREADY_EXISTS;
			goto unlock_and_exit;
		}

		if (handler_type & ACPI_SYSTEM_NOTIFY) {
			acpi_gbl_system_notify.node = node;
			acpi_gbl_system_notify.handler = handler;
			acpi_gbl_system_notify.context = context;
		}

		if (handler_type & ACPI_DEVICE_NOTIFY) {
			acpi_gbl_device_notify.node = node;
			acpi_gbl_device_notify.handler = handler;
			acpi_gbl_device_notify.context = context;
		}

		/* Global notify handler installed */
	}

	/*
	 * All Other Objects:
	 * Caller will only receive notifications specific to the target object.
	 * Note that only certain object types can receive notifications.
	 */
	else {
		/* Notifies allowed on this object? */

		if (!acpi_ev_is_notify_object(node)) {
			status = AE_TYPE;
			goto unlock_and_exit;
		}

		/* Check for an existing internal object */

		obj_desc = acpi_ns_get_attached_object(node);
		if (obj_desc) {

			/* Object exists. */

			/* For a device notify, make sure there's no handler. */
			if ((handler_type & ACPI_DEVICE_NOTIFY) &&
			     obj_desc->common_notify.device_notify) {
				status = AE_ALREADY_EXISTS;
				goto unlock_and_exit;
			}

			/* System notifies may have more handlers installed. */
			notify_obj = obj_desc->common_notify.system_notify;

			if ((handler_type & ACPI_SYSTEM_NOTIFY) && notify_obj) {
				struct acpi_object_notify_handler *parent_obj;

				if (handler_type & ACPI_DEVICE_NOTIFY) {
					status = AE_ALREADY_EXISTS;
					goto unlock_and_exit;
				}

				parent_obj = &notify_obj->notify;
				status = acpi_add_handler_object(parent_obj,
								 handler,
								 context);
				goto unlock_and_exit;
			}
		} else {
			/* Create a new object */

			obj_desc = acpi_ut_create_internal_object(node->type);
			if (!obj_desc) {
				status = AE_NO_MEMORY;
				goto unlock_and_exit;
			}

			/* Attach new object to the Node */

			status =
			    acpi_ns_attach_object(device, obj_desc, node->type);

			/* Remove local reference to the object */

			acpi_ut_remove_reference(obj_desc);
			if (ACPI_FAILURE(status)) {
				goto unlock_and_exit;
			}
		}

		/* Install the handler */

		notify_obj =
		    acpi_ut_create_internal_object(ACPI_TYPE_LOCAL_NOTIFY);
		if (!notify_obj) {
			status = AE_NO_MEMORY;
			goto unlock_and_exit;
		}

		acpi_populate_handler_object(&notify_obj->notify,
						handler_type,
						handler, context,
						NULL);

		if (handler_type & ACPI_SYSTEM_NOTIFY) {
			obj_desc->common_notify.system_notify = notify_obj;
		}

		if (handler_type & ACPI_DEVICE_NOTIFY) {
			obj_desc->common_notify.device_notify = notify_obj;
		}

		if (handler_type == ACPI_ALL_NOTIFY) {

			/* Extra ref if installed in both */

			acpi_ut_add_reference(notify_obj);
		}
	}

      unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_install_notify_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_remove_notify_handler
 *
 * PARAMETERS:  Device          - The device for which notifies will be handled
 *              handler_type    - The type of handler:
 *                                  ACPI_SYSTEM_NOTIFY: system_handler (00-7f)
 *                                  ACPI_DEVICE_NOTIFY: driver_handler (80-ff)
 *                                  ACPI_ALL_NOTIFY:  both system and device
 *              Handler         - Address of the handler
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a handler for notifies on an ACPI device
 *
 ******************************************************************************/
acpi_status
acpi_remove_notify_handler(acpi_handle device,
			   u32 handler_type, acpi_notify_handler handler)
{
	union acpi_operand_object *notify_obj;
	union acpi_operand_object *obj_desc;
	struct acpi_namespace_node *node;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_remove_notify_handler);

	/* Parameter validation */

	if ((!device) ||
	    (!handler) || (handler_type > ACPI_MAX_NOTIFY_HANDLER_TYPE)) {
		status = AE_BAD_PARAMETER;
		goto exit;
	}


	/* Make sure all deferred tasks are completed */
	acpi_os_wait_events_complete(NULL);

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	/* Convert and validate the device handle */

	node = acpi_ns_validate_handle(device);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Root Object */

	if (device == ACPI_ROOT_OBJECT) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Removing notify handler for namespace root object\n"));

		if (((handler_type & ACPI_SYSTEM_NOTIFY) &&
		     !acpi_gbl_system_notify.handler) ||
		    ((handler_type & ACPI_DEVICE_NOTIFY) &&
		     !acpi_gbl_device_notify.handler)) {
			status = AE_NOT_EXIST;
			goto unlock_and_exit;
		}

		if (handler_type & ACPI_SYSTEM_NOTIFY) {
			acpi_gbl_system_notify.node = NULL;
			acpi_gbl_system_notify.handler = NULL;
			acpi_gbl_system_notify.context = NULL;
		}

		if (handler_type & ACPI_DEVICE_NOTIFY) {
			acpi_gbl_device_notify.node = NULL;
			acpi_gbl_device_notify.handler = NULL;
			acpi_gbl_device_notify.context = NULL;
		}
	}

	/* All Other Objects */

	else {
		/* Notifies allowed on this object? */

		if (!acpi_ev_is_notify_object(node)) {
			status = AE_TYPE;
			goto unlock_and_exit;
		}

		/* Check for an existing internal object */

		obj_desc = acpi_ns_get_attached_object(node);
		if (!obj_desc) {
			status = AE_NOT_EXIST;
			goto unlock_and_exit;
		}

		/* Object exists - make sure there's an existing handler */

		if (handler_type & ACPI_SYSTEM_NOTIFY) {
			struct acpi_object_notify_handler *handler_obj;
			struct acpi_object_notify_handler *parent_obj;

			notify_obj = obj_desc->common_notify.system_notify;
			if (!notify_obj) {
				status = AE_NOT_EXIST;
				goto unlock_and_exit;
			}

			handler_obj = &notify_obj->notify;
			parent_obj = NULL;
			while (handler_obj->handler != handler) {
				if (handler_obj->next) {
					parent_obj = handler_obj;
					handler_obj = handler_obj->next;
				} else {
					break;
				}
			}

			if (handler_obj->handler != handler) {
				status = AE_BAD_PARAMETER;
				goto unlock_and_exit;
			}

			/*
			 * Remove the handler.  There are three possible cases.
			 * First, we may need to remove a non-embedded object.
			 * Second, we may need to remove the embedded object's
			 * handler data, while non-embedded objects exist.
			 * Finally, we may need to remove the embedded object
			 * entirely along with its container.
			 */
			if (parent_obj) {
				/* Non-embedded object is being removed. */
				parent_obj->next = handler_obj->next;
				ACPI_FREE(handler_obj);
			} else if (notify_obj->notify.next) {
				/*
				 * The handler matches the embedded object, but
				 * there are more handler objects in the list.
				 * Replace the embedded object's data with the
				 * first next object's data and remove that
				 * object.
				 */
				parent_obj = &notify_obj->notify;
				handler_obj = notify_obj->notify.next;
				*parent_obj = *handler_obj;
				ACPI_FREE(handler_obj);
			} else {
				/* No more handler objects in the list. */
				obj_desc->common_notify.system_notify = NULL;
				acpi_ut_remove_reference(notify_obj);
			}
		}

		if (handler_type & ACPI_DEVICE_NOTIFY) {
			notify_obj = obj_desc->common_notify.device_notify;
			if (!notify_obj) {
				status = AE_NOT_EXIST;
				goto unlock_and_exit;
			}

			if (notify_obj->notify.handler != handler) {
				status = AE_BAD_PARAMETER;
				goto unlock_and_exit;
			}

			/* Remove the handler */
			obj_desc->common_notify.device_notify = NULL;
			acpi_ut_remove_reference(notify_obj);
		}
	}

      unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
      exit:
	if (ACPI_FAILURE(status))
		ACPI_EXCEPTION((AE_INFO, status, "Removing notify handler"));
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_remove_notify_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_install_gpe_handler
 *
 * PARAMETERS:  gpe_device      - Namespace node for the GPE (NULL for FADT
 *                                defined GPEs)
 *              gpe_number      - The GPE number within the GPE block
 *              Type            - Whether this GPE should be treated as an
 *                                edge- or level-triggered interrupt.
 *              Address         - Address of the handler
 *              Context         - Value passed to the handler on each GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for a General Purpose Event.
 *
 ******************************************************************************/
acpi_status
acpi_install_gpe_handler(acpi_handle gpe_device,
			 u32 gpe_number,
			 u32 type, acpi_gpe_handler address, void *context)
{
	struct acpi_gpe_event_info *gpe_event_info;
	struct acpi_gpe_handler_info *handler;
	acpi_status status;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_install_gpe_handler);

	/* Parameter validation */

	if ((!address) || (type & ~ACPI_GPE_XRUPT_TYPE_MASK)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Allocate memory for the handler object */

	handler = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_gpe_handler_info));
	if (!handler) {
		status = AE_NO_MEMORY;
		goto unlock_and_exit;
	}

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto free_and_exit;
	}

	/* Make sure that there isn't a handler there already */

	if ((gpe_event_info->flags & ACPI_GPE_DISPATCH_MASK) ==
	    ACPI_GPE_DISPATCH_HANDLER) {
		status = AE_ALREADY_EXISTS;
		goto free_and_exit;
	}

	/* Allocate and init handler object */

	handler->address = address;
	handler->context = context;
	handler->method_node = gpe_event_info->dispatch.method_node;
	handler->original_flags = gpe_event_info->flags &
			(ACPI_GPE_XRUPT_TYPE_MASK | ACPI_GPE_DISPATCH_MASK);

	/*
	 * If the GPE is associated with a method, it might have been enabled
	 * automatically during initialization, in which case it has to be
	 * disabled now to avoid spurious execution of the handler.
	 */

	if ((handler->original_flags & ACPI_GPE_DISPATCH_METHOD)
	    && gpe_event_info->runtime_count) {
		handler->originally_enabled = 1;
		(void)acpi_ev_remove_gpe_reference(gpe_event_info);
	}

	/* Install the handler */

	gpe_event_info->dispatch.handler = handler;

	/* Setup up dispatch flags to indicate handler (vs. method) */

	gpe_event_info->flags &=
	    ~(ACPI_GPE_XRUPT_TYPE_MASK | ACPI_GPE_DISPATCH_MASK);
	gpe_event_info->flags |= (u8) (type | ACPI_GPE_DISPATCH_HANDLER);

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return_ACPI_STATUS(status);

free_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	ACPI_FREE(handler);
	goto unlock_and_exit;
}

ACPI_EXPORT_SYMBOL(acpi_install_gpe_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_remove_gpe_handler
 *
 * PARAMETERS:  gpe_device      - Namespace node for the GPE (NULL for FADT
 *                                defined GPEs)
 *              gpe_number      - The event to remove a handler
 *              Address         - Address of the handler
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a handler for a General Purpose acpi_event.
 *
 ******************************************************************************/
acpi_status
acpi_remove_gpe_handler(acpi_handle gpe_device,
			u32 gpe_number, acpi_gpe_handler address)
{
	struct acpi_gpe_event_info *gpe_event_info;
	struct acpi_gpe_handler_info *handler;
	acpi_status status;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(acpi_remove_gpe_handler);

	/* Parameter validation */

	if (!address) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Make sure all deferred tasks are completed */

	acpi_os_wait_events_complete(NULL);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Ensure that we have a valid GPE number */

	gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
	if (!gpe_event_info) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Make sure that a handler is indeed installed */

	if ((gpe_event_info->flags & ACPI_GPE_DISPATCH_MASK) !=
	    ACPI_GPE_DISPATCH_HANDLER) {
		status = AE_NOT_EXIST;
		goto unlock_and_exit;
	}

	/* Make sure that the installed handler is the same */

	if (gpe_event_info->dispatch.handler->address != address) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Remove the handler */

	handler = gpe_event_info->dispatch.handler;

	/* Restore Method node (if any), set dispatch flags */

	gpe_event_info->dispatch.method_node = handler->method_node;
	gpe_event_info->flags &=
		~(ACPI_GPE_XRUPT_TYPE_MASK | ACPI_GPE_DISPATCH_MASK);
	gpe_event_info->flags |= handler->original_flags;

	/*
	 * If the GPE was previously associated with a method and it was
	 * enabled, it should be enabled at this point to restore the
	 * post-initialization configuration.
	 */

	if ((handler->original_flags & ACPI_GPE_DISPATCH_METHOD)
	    && handler->originally_enabled)
		(void)acpi_ev_add_gpe_reference(gpe_event_info);

	/* Now we can free the handler object */

	ACPI_FREE(handler);

unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);

	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_remove_gpe_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_acquire_global_lock
 *
 * PARAMETERS:  Timeout         - How long the caller is willing to wait
 *              Handle          - Where the handle to the lock is returned
 *                                (if acquired)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Acquire the ACPI Global Lock
 *
 * Note: Allows callers with the same thread ID to acquire the global lock
 * multiple times. In other words, externally, the behavior of the global lock
 * is identical to an AML mutex. On the first acquire, a new handle is
 * returned. On any subsequent calls to acquire by the same thread, the same
 * handle is returned.
 *
 ******************************************************************************/
acpi_status acpi_acquire_global_lock(u16 timeout, u32 * handle)
{
	acpi_status status;

	if (!handle) {
		return (AE_BAD_PARAMETER);
	}

	/* Must lock interpreter to prevent race conditions */

	acpi_ex_enter_interpreter();

	status = acpi_ex_acquire_mutex_object(timeout,
					      acpi_gbl_global_lock_mutex,
					      acpi_os_get_thread_id());

	if (ACPI_SUCCESS(status)) {

		/* Return the global lock handle (updated in acpi_ev_acquire_global_lock) */

		*handle = acpi_gbl_global_lock_handle;
	}

	acpi_ex_exit_interpreter();
	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_acquire_global_lock)

/*******************************************************************************
 *
 * FUNCTION:    acpi_release_global_lock
 *
 * PARAMETERS:  Handle      - Returned from acpi_acquire_global_lock
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release the ACPI Global Lock. The handle must be valid.
 *
 ******************************************************************************/
acpi_status acpi_release_global_lock(u32 handle)
{
	acpi_status status;

	if (!handle || (handle != acpi_gbl_global_lock_handle)) {
		return (AE_NOT_ACQUIRED);
	}

	status = acpi_ex_release_mutex_object(acpi_gbl_global_lock_mutex);
	return (status);
}

ACPI_EXPORT_SYMBOL(acpi_release_global_lock)
