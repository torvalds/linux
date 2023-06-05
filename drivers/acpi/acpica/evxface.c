// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: evxface - External interfaces for ACPI events
 *
 * Copyright (C) 2000 - 2023, Intel Corp.
 *
 *****************************************************************************/

#define EXPORT_ACPI_INTERFACES

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"
#include "acevents.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evxface")
#if (!ACPI_REDUCED_HARDWARE)
/* Local prototypes */
static acpi_status
acpi_ev_install_gpe_handler(acpi_handle gpe_device,
			    u32 gpe_number,
			    u32 type,
			    u8 is_raw_handler,
			    acpi_gpe_handler address, void *context);

#endif


/*******************************************************************************
 *
 * FUNCTION:    acpi_install_notify_handler
 *
 * PARAMETERS:  device          - The device for which notifies will be handled
 *              handler_type    - The type of handler:
 *                                  ACPI_SYSTEM_NOTIFY: System Handler (00-7F)
 *                                  ACPI_DEVICE_NOTIFY: Device Handler (80-FF)
 *                                  ACPI_ALL_NOTIFY:    Both System and Device
 *              handler         - Address of the handler
 *              context         - Value passed to the handler on each GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for notifications on an ACPI Device,
 *              thermal_zone, or Processor object.
 *
 * NOTES:       The Root namespace object may have only one handler for each
 *              type of notify (System/Device). Device/Thermal/Processor objects
 *              may have one device notify handler, and multiple system notify
 *              handlers.
 *
 ******************************************************************************/

acpi_status
acpi_install_notify_handler(acpi_handle device,
			    u32 handler_type,
			    acpi_notify_handler handler, void *context)
{
	struct acpi_namespace_node *node =
	    ACPI_CAST_PTR(struct acpi_namespace_node, device);
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *handler_obj;
	acpi_status status;
	u32 i;

	ACPI_FUNCTION_TRACE(acpi_install_notify_handler);

	/* Parameter validation */

	if ((!device) || (!handler) || (!handler_type) ||
	    (handler_type > ACPI_MAX_NOTIFY_HANDLER_TYPE)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Root Object:
	 * Registering a notify handler on the root object indicates that the
	 * caller wishes to receive notifications for all objects. Note that
	 * only one global handler can be registered per notify type.
	 * Ensure that a handler is not already installed.
	 */
	if (device == ACPI_ROOT_OBJECT) {
		for (i = 0; i < ACPI_NUM_NOTIFY_TYPES; i++) {
			if (handler_type & (i + 1)) {
				if (acpi_gbl_global_notify[i].handler) {
					status = AE_ALREADY_EXISTS;
					goto unlock_and_exit;
				}

				acpi_gbl_global_notify[i].handler = handler;
				acpi_gbl_global_notify[i].context = context;
			}
		}

		goto unlock_and_exit;	/* Global notify handler installed, all done */
	}

	/*
	 * All Other Objects:
	 * Caller will only receive notifications specific to the target
	 * object. Note that only certain object types are allowed to
	 * receive notifications.
	 */

	/* Are Notifies allowed on this object? */

	if (!acpi_ev_is_notify_object(node)) {
		status = AE_TYPE;
		goto unlock_and_exit;
	}

	/* Check for an existing internal object, might not exist */

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc) {

		/* Create a new object */

		obj_desc = acpi_ut_create_internal_object(node->type);
		if (!obj_desc) {
			status = AE_NO_MEMORY;
			goto unlock_and_exit;
		}

		/* Attach new object to the Node, remove local reference */

		status = acpi_ns_attach_object(device, obj_desc, node->type);
		acpi_ut_remove_reference(obj_desc);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}
	}

	/* Ensure that the handler is not already installed in the lists */

	for (i = 0; i < ACPI_NUM_NOTIFY_TYPES; i++) {
		if (handler_type & (i + 1)) {
			handler_obj = obj_desc->common_notify.notify_list[i];
			while (handler_obj) {
				if (handler_obj->notify.handler == handler) {
					status = AE_ALREADY_EXISTS;
					goto unlock_and_exit;
				}

				handler_obj = handler_obj->notify.next[i];
			}
		}
	}

	/* Create and populate a new notify handler object */

	handler_obj = acpi_ut_create_internal_object(ACPI_TYPE_LOCAL_NOTIFY);
	if (!handler_obj) {
		status = AE_NO_MEMORY;
		goto unlock_and_exit;
	}

	handler_obj->notify.node = node;
	handler_obj->notify.handler_type = handler_type;
	handler_obj->notify.handler = handler;
	handler_obj->notify.context = context;

	/* Install the handler at the list head(s) */

	for (i = 0; i < ACPI_NUM_NOTIFY_TYPES; i++) {
		if (handler_type & (i + 1)) {
			handler_obj->notify.next[i] =
			    obj_desc->common_notify.notify_list[i];

			obj_desc->common_notify.notify_list[i] = handler_obj;
		}
	}

	/* Add an extra reference if handler was installed in both lists */

	if (handler_type == ACPI_ALL_NOTIFY) {
		acpi_ut_add_reference(handler_obj);
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
 * PARAMETERS:  device          - The device for which the handler is installed
 *              handler_type    - The type of handler:
 *                                  ACPI_SYSTEM_NOTIFY: System Handler (00-7F)
 *                                  ACPI_DEVICE_NOTIFY: Device Handler (80-FF)
 *                                  ACPI_ALL_NOTIFY:    Both System and Device
 *              handler         - Address of the handler
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
	struct acpi_namespace_node *node =
	    ACPI_CAST_PTR(struct acpi_namespace_node, device);
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *handler_obj;
	union acpi_operand_object *previous_handler_obj;
	acpi_status status = AE_OK;
	u32 i;

	ACPI_FUNCTION_TRACE(acpi_remove_notify_handler);

	/* Parameter validation */

	if ((!device) || (!handler) || (!handler_type) ||
	    (handler_type > ACPI_MAX_NOTIFY_HANDLER_TYPE)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Root Object. Global handlers are removed here */

	if (device == ACPI_ROOT_OBJECT) {
		for (i = 0; i < ACPI_NUM_NOTIFY_TYPES; i++) {
			if (handler_type & (i + 1)) {
				status =
				    acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
				if (ACPI_FAILURE(status)) {
					return_ACPI_STATUS(status);
				}

				if (!acpi_gbl_global_notify[i].handler ||
				    (acpi_gbl_global_notify[i].handler !=
				     handler)) {
					status = AE_NOT_EXIST;
					goto unlock_and_exit;
				}

				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
						  "Removing global notify handler\n"));

				acpi_gbl_global_notify[i].handler = NULL;
				acpi_gbl_global_notify[i].context = NULL;

				(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);

				/* Make sure all deferred notify tasks are completed */

				acpi_os_wait_events_complete();
			}
		}

		return_ACPI_STATUS(AE_OK);
	}

	/* All other objects: Are Notifies allowed on this object? */

	if (!acpi_ev_is_notify_object(node)) {
		return_ACPI_STATUS(AE_TYPE);
	}

	/* Must have an existing internal object */

	obj_desc = acpi_ns_get_attached_object(node);
	if (!obj_desc) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	/* Internal object exists. Find the handler and remove it */

	for (i = 0; i < ACPI_NUM_NOTIFY_TYPES; i++) {
		if (handler_type & (i + 1)) {
			status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}

			handler_obj = obj_desc->common_notify.notify_list[i];
			previous_handler_obj = NULL;

			/* Attempt to find the handler in the handler list */

			while (handler_obj &&
			       (handler_obj->notify.handler != handler)) {
				previous_handler_obj = handler_obj;
				handler_obj = handler_obj->notify.next[i];
			}

			if (!handler_obj) {
				status = AE_NOT_EXIST;
				goto unlock_and_exit;
			}

			/* Remove the handler object from the list */

			if (previous_handler_obj) {	/* Handler is not at the list head */
				previous_handler_obj->notify.next[i] =
				    handler_obj->notify.next[i];
			} else {	/* Handler is at the list head */

				obj_desc->common_notify.notify_list[i] =
				    handler_obj->notify.next[i];
			}

			(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);

			/* Make sure all deferred notify tasks are completed */

			acpi_os_wait_events_complete();
			acpi_ut_remove_reference(handler_obj);
		}
	}

	return_ACPI_STATUS(status);

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_remove_notify_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_install_exception_handler
 *
 * PARAMETERS:  handler         - Pointer to the handler function for the
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
#endif

#if (!ACPI_REDUCED_HARDWARE)
/*******************************************************************************
 *
 * FUNCTION:    acpi_install_sci_handler
 *
 * PARAMETERS:  address             - Address of the handler
 *              context             - Value passed to the handler on each SCI
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for a System Control Interrupt.
 *
 ******************************************************************************/
acpi_status acpi_install_sci_handler(acpi_sci_handler address, void *context)
{
	struct acpi_sci_handler_info *new_sci_handler;
	struct acpi_sci_handler_info *sci_handler;
	acpi_cpu_flags flags;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_install_sci_handler);

	if (!address) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Allocate and init a handler object */

	new_sci_handler = ACPI_ALLOCATE(sizeof(struct acpi_sci_handler_info));
	if (!new_sci_handler) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	new_sci_handler->address = address;
	new_sci_handler->context = context;

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	/* Lock list during installation */

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);
	sci_handler = acpi_gbl_sci_handler_list;

	/* Ensure handler does not already exist */

	while (sci_handler) {
		if (address == sci_handler->address) {
			status = AE_ALREADY_EXISTS;
			goto unlock_and_exit;
		}

		sci_handler = sci_handler->next;
	}

	/* Install the new handler into the global list (at head) */

	new_sci_handler->next = acpi_gbl_sci_handler_list;
	acpi_gbl_sci_handler_list = new_sci_handler;

unlock_and_exit:

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);

exit:
	if (ACPI_FAILURE(status)) {
		ACPI_FREE(new_sci_handler);
	}
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_install_sci_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_remove_sci_handler
 *
 * PARAMETERS:  address             - Address of the handler
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a handler for a System Control Interrupt.
 *
 ******************************************************************************/
acpi_status acpi_remove_sci_handler(acpi_sci_handler address)
{
	struct acpi_sci_handler_info *prev_sci_handler;
	struct acpi_sci_handler_info *next_sci_handler;
	acpi_cpu_flags flags;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_remove_sci_handler);

	if (!address) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Remove the SCI handler with lock */

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	prev_sci_handler = NULL;
	next_sci_handler = acpi_gbl_sci_handler_list;
	while (next_sci_handler) {
		if (next_sci_handler->address == address) {

			/* Unlink and free the SCI handler info block */

			if (prev_sci_handler) {
				prev_sci_handler->next = next_sci_handler->next;
			} else {
				acpi_gbl_sci_handler_list =
				    next_sci_handler->next;
			}

			acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
			ACPI_FREE(next_sci_handler);
			goto unlock_and_exit;
		}

		prev_sci_handler = next_sci_handler;
		next_sci_handler = next_sci_handler->next;
	}

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	status = AE_NOT_EXIST;

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_remove_sci_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_install_global_event_handler
 *
 * PARAMETERS:  handler         - Pointer to the global event handler function
 *              context         - Value passed to the handler on each event
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
acpi_install_global_event_handler(acpi_gbl_event_handler handler, void *context)
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
 * PARAMETERS:  event           - Event type to enable.
 *              handler         - Pointer to the handler function for the
 *                                event
 *              context         - Value passed to the handler on each GPE
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

	/* Do not allow multiple handlers */

	if (acpi_gbl_fixed_event_handlers[event].handler) {
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
		ACPI_WARNING((AE_INFO,
			      "Could not enable fixed event - %s (%u)",
			      acpi_ut_get_event_name(event), event));

		/* Remove the handler */

		acpi_gbl_fixed_event_handlers[event].handler = NULL;
		acpi_gbl_fixed_event_handlers[event].context = NULL;
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Enabled fixed event %s (%X), Handler=%p\n",
				  acpi_ut_get_event_name(event), event,
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
 * PARAMETERS:  event           - Event type to disable.
 *              handler         - Address of the handler
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
			      "Could not disable fixed event - %s (%u)",
			      acpi_ut_get_event_name(event), event));
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Disabled fixed event - %s (%X)\n",
				  acpi_ut_get_event_name(event), event));
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_remove_fixed_event_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_install_gpe_handler
 *
 * PARAMETERS:  gpe_device      - Namespace node for the GPE (NULL for FADT
 *                                defined GPEs)
 *              gpe_number      - The GPE number within the GPE block
 *              type            - Whether this GPE should be treated as an
 *                                edge- or level-triggered interrupt.
 *              is_raw_handler  - Whether this GPE should be handled using
 *                                the special GPE handler mode.
 *              address         - Address of the handler
 *              context         - Value passed to the handler on each GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Internal function to install a handler for a General Purpose
 *              Event.
 *
 ******************************************************************************/
static acpi_status
acpi_ev_install_gpe_handler(acpi_handle gpe_device,
			    u32 gpe_number,
			    u32 type,
			    u8 is_raw_handler,
			    acpi_gpe_handler address, void *context)
{
	struct acpi_gpe_event_info *gpe_event_info;
	struct acpi_gpe_handler_info *handler;
	acpi_status status;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(ev_install_gpe_handler);

	/* Parameter validation */

	if ((!address) || (type & ~ACPI_GPE_XRUPT_TYPE_MASK)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Allocate and init handler object (before lock) */

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

	if ((ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) ==
	     ACPI_GPE_DISPATCH_HANDLER) ||
	    (ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) ==
	     ACPI_GPE_DISPATCH_RAW_HANDLER)) {
		status = AE_ALREADY_EXISTS;
		goto free_and_exit;
	}

	handler->address = address;
	handler->context = context;
	handler->method_node = gpe_event_info->dispatch.method_node;
	handler->original_flags = (u8)(gpe_event_info->flags &
				       (ACPI_GPE_XRUPT_TYPE_MASK |
					ACPI_GPE_DISPATCH_MASK));

	/*
	 * If the GPE is associated with a method, it may have been enabled
	 * automatically during initialization, in which case it has to be
	 * disabled now to avoid spurious execution of the handler.
	 */
	if (((ACPI_GPE_DISPATCH_TYPE(handler->original_flags) ==
	      ACPI_GPE_DISPATCH_METHOD) ||
	     (ACPI_GPE_DISPATCH_TYPE(handler->original_flags) ==
	      ACPI_GPE_DISPATCH_NOTIFY)) && gpe_event_info->runtime_count) {
		handler->originally_enabled = TRUE;
		(void)acpi_ev_remove_gpe_reference(gpe_event_info);

		/* Sanity check of original type against new type */

		if (type !=
		    (u32)(gpe_event_info->flags & ACPI_GPE_XRUPT_TYPE_MASK)) {
			ACPI_WARNING((AE_INFO,
				      "GPE type mismatch (level/edge)"));
		}
	}

	/* Install the handler */

	gpe_event_info->dispatch.handler = handler;

	/* Setup up dispatch flags to indicate handler (vs. method/notify) */

	gpe_event_info->flags &=
	    ~(ACPI_GPE_XRUPT_TYPE_MASK | ACPI_GPE_DISPATCH_MASK);
	gpe_event_info->flags |=
	    (u8)(type |
		 (is_raw_handler ? ACPI_GPE_DISPATCH_RAW_HANDLER :
		  ACPI_GPE_DISPATCH_HANDLER));

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return_ACPI_STATUS(status);

free_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	ACPI_FREE(handler);
	goto unlock_and_exit;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_install_gpe_handler
 *
 * PARAMETERS:  gpe_device      - Namespace node for the GPE (NULL for FADT
 *                                defined GPEs)
 *              gpe_number      - The GPE number within the GPE block
 *              type            - Whether this GPE should be treated as an
 *                                edge- or level-triggered interrupt.
 *              address         - Address of the handler
 *              context         - Value passed to the handler on each GPE
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
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_install_gpe_handler);

	status = acpi_ev_install_gpe_handler(gpe_device, gpe_number, type,
					     FALSE, address, context);

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_install_gpe_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_install_gpe_raw_handler
 *
 * PARAMETERS:  gpe_device      - Namespace node for the GPE (NULL for FADT
 *                                defined GPEs)
 *              gpe_number      - The GPE number within the GPE block
 *              type            - Whether this GPE should be treated as an
 *                                edge- or level-triggered interrupt.
 *              address         - Address of the handler
 *              context         - Value passed to the handler on each GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for a General Purpose Event.
 *
 ******************************************************************************/
acpi_status
acpi_install_gpe_raw_handler(acpi_handle gpe_device,
			     u32 gpe_number,
			     u32 type, acpi_gpe_handler address, void *context)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_install_gpe_raw_handler);

	status = acpi_ev_install_gpe_handler(gpe_device, gpe_number, type,
					     TRUE, address, context);

	return_ACPI_STATUS(status);
}

ACPI_EXPORT_SYMBOL(acpi_install_gpe_raw_handler)

/*******************************************************************************
 *
 * FUNCTION:    acpi_remove_gpe_handler
 *
 * PARAMETERS:  gpe_device      - Namespace node for the GPE (NULL for FADT
 *                                defined GPEs)
 *              gpe_number      - The event to remove a handler
 *              address         - Address of the handler
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

	if ((ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) !=
	     ACPI_GPE_DISPATCH_HANDLER) &&
	    (ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) !=
	     ACPI_GPE_DISPATCH_RAW_HANDLER)) {
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
	gpe_event_info->dispatch.handler = NULL;

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
	if (((ACPI_GPE_DISPATCH_TYPE(handler->original_flags) ==
	      ACPI_GPE_DISPATCH_METHOD) ||
	     (ACPI_GPE_DISPATCH_TYPE(handler->original_flags) ==
	      ACPI_GPE_DISPATCH_NOTIFY)) && handler->originally_enabled) {
		(void)acpi_ev_add_gpe_reference(gpe_event_info, FALSE);
		if (ACPI_GPE_IS_POLLING_NEEDED(gpe_event_info)) {

			/* Poll edge triggered GPEs to handle existing events */

			acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
			(void)acpi_ev_detect_gpe(gpe_device, gpe_event_info,
						 gpe_number);
			flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);
		}
	}

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);

	/* Make sure all deferred GPE tasks are completed */

	acpi_os_wait_events_complete();

	/* Now we can free the handler object */

	ACPI_FREE(handler);
	return_ACPI_STATUS(status);

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
 * PARAMETERS:  timeout         - How long the caller is willing to wait
 *              handle          - Where the handle to the lock is returned
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
acpi_status acpi_acquire_global_lock(u16 timeout, u32 *handle)
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
 * PARAMETERS:  handle      - Returned from acpi_acquire_global_lock
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
#endif				/* !ACPI_REDUCED_HARDWARE */
