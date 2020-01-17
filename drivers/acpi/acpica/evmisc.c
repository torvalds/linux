// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: evmisc - Miscellaneous event manager support functions
 *
 * Copyright (C) 2000 - 2019, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acevents.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evmisc")

/* Local prototypes */
static void ACPI_SYSTEM_XFACE acpi_ev_yestify_dispatch(void *context);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_is_yestify_object
 *
 * PARAMETERS:  yesde            - Node to check
 *
 * RETURN:      TRUE if yestifies allowed on this object
 *
 * DESCRIPTION: Check type of yesde for a object that supports yestifies.
 *
 *              TBD: This could be replaced by a flag bit in the yesde.
 *
 ******************************************************************************/

u8 acpi_ev_is_yestify_object(struct acpi_namespace_yesde *yesde)
{

	switch (yesde->type) {
	case ACPI_TYPE_DEVICE:
	case ACPI_TYPE_PROCESSOR:
	case ACPI_TYPE_THERMAL:
		/*
		 * These are the ONLY objects that can receive ACPI yestifications
		 */
		return (TRUE);

	default:

		return (FALSE);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_queue_yestify_request
 *
 * PARAMETERS:  yesde            - NS yesde for the yestified object
 *              yestify_value    - Value from the Notify() request
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Dispatch a device yestification event to a previously
 *              installed handler.
 *
 ******************************************************************************/

acpi_status
acpi_ev_queue_yestify_request(struct acpi_namespace_yesde *yesde, u32 yestify_value)
{
	union acpi_operand_object *obj_desc;
	union acpi_operand_object *handler_list_head = NULL;
	union acpi_generic_state *info;
	u8 handler_list_id = 0;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_NAME(ev_queue_yestify_request);

	/* Are Notifies allowed on this object? */

	if (!acpi_ev_is_yestify_object(yesde)) {
		return (AE_TYPE);
	}

	/* Get the correct yestify list type (System or Device) */

	if (yestify_value <= ACPI_MAX_SYS_NOTIFY) {
		handler_list_id = ACPI_SYSTEM_HANDLER_LIST;
	} else {
		handler_list_id = ACPI_DEVICE_HANDLER_LIST;
	}

	/* Get the yestify object attached to the namespace Node */

	obj_desc = acpi_ns_get_attached_object(yesde);
	if (obj_desc) {

		/* We have an attached object, Get the correct handler list */

		handler_list_head =
		    obj_desc->common_yestify.yestify_list[handler_list_id];
	}

	/*
	 * If there is yes yestify handler (Global or Local)
	 * for this object, just igyesre the yestify
	 */
	if (!acpi_gbl_global_yestify[handler_list_id].handler
	    && !handler_list_head) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "No yestify handler for Notify, igyesring (%4.4s, %X) yesde %p\n",
				  acpi_ut_get_yesde_name(yesde), yestify_value,
				  yesde));

		return (AE_OK);
	}

	/* Setup yestify info and schedule the yestify dispatcher */

	info = acpi_ut_create_generic_state();
	if (!info) {
		return (AE_NO_MEMORY);
	}

	info->common.descriptor_type = ACPI_DESC_TYPE_STATE_NOTIFY;

	info->yestify.yesde = yesde;
	info->yestify.value = (u16)yestify_value;
	info->yestify.handler_list_id = handler_list_id;
	info->yestify.handler_list_head = handler_list_head;
	info->yestify.global = &acpi_gbl_global_yestify[handler_list_id];

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Dispatching Notify on [%4.4s] (%s) Value 0x%2.2X (%s) Node %p\n",
			  acpi_ut_get_yesde_name(yesde),
			  acpi_ut_get_type_name(yesde->type), yestify_value,
			  acpi_ut_get_yestify_name(yestify_value, ACPI_TYPE_ANY),
			  yesde));

	status = acpi_os_execute(OSL_NOTIFY_HANDLER,
				 acpi_ev_yestify_dispatch, info);
	if (ACPI_FAILURE(status)) {
		acpi_ut_delete_generic_state(info);
	}

	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_yestify_dispatch
 *
 * PARAMETERS:  context         - To be passed to the yestify handler
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Dispatch a device yestification event to a previously
 *              installed handler.
 *
 ******************************************************************************/

static void ACPI_SYSTEM_XFACE acpi_ev_yestify_dispatch(void *context)
{
	union acpi_generic_state *info = (union acpi_generic_state *)context;
	union acpi_operand_object *handler_obj;

	ACPI_FUNCTION_ENTRY();

	/* Invoke a global yestify handler if installed */

	if (info->yestify.global->handler) {
		info->yestify.global->handler(info->yestify.yesde,
					     info->yestify.value,
					     info->yestify.global->context);
	}

	/* Now invoke the local yestify handler(s) if any are installed */

	handler_obj = info->yestify.handler_list_head;
	while (handler_obj) {
		handler_obj->yestify.handler(info->yestify.yesde,
					    info->yestify.value,
					    handler_obj->yestify.context);

		handler_obj =
		    handler_obj->yestify.next[info->yestify.handler_list_id];
	}

	/* All done with the info object */

	acpi_ut_delete_generic_state(info);
}

#if (!ACPI_REDUCED_HARDWARE)
/******************************************************************************
 *
 * FUNCTION:    acpi_ev_terminate
 *
 * PARAMETERS:  yesne
 *
 * RETURN:      yesne
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
					    "Could yest disable fixed event %u",
					    (u32) i));
			}
		}

		/* Disable all GPEs in all GPE blocks */

		status = acpi_ev_walk_gpe_list(acpi_hw_disable_gpe_block, NULL);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Could yest disable GPEs in GPE block"));
		}

		status = acpi_ev_remove_global_lock_handler();
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Could yest remove Global Lock handler"));
		}

		acpi_gbl_events_initialized = FALSE;
	}

	/* Remove SCI handlers */

	status = acpi_ev_remove_all_sci_handlers();
	if (ACPI_FAILURE(status)) {
		ACPI_ERROR((AE_INFO, "Could yest remove SCI handler"));
	}

	/* Deallocate all handler objects installed within GPE info structs */

	status = acpi_ev_walk_gpe_list(acpi_ev_delete_gpe_handlers, NULL);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Could yest delete GPE handlers"));
	}

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
