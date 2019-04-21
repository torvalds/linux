// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: evgpe - General Purpose Event handling and dispatch
 *
 * Copyright (C) 2000 - 2019, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acevents.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evgpe")
#if (!ACPI_REDUCED_HARDWARE)	/* Entire module */
/* Local prototypes */
static void ACPI_SYSTEM_XFACE acpi_ev_asynch_execute_gpe_method(void *context);

static void ACPI_SYSTEM_XFACE acpi_ev_asynch_enable_gpe(void *context);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_update_gpe_enable_mask
 *
 * PARAMETERS:  gpe_event_info          - GPE to update
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Updates GPE register enable mask based upon whether there are
 *              runtime references to this GPE
 *
 ******************************************************************************/

acpi_status
acpi_ev_update_gpe_enable_mask(struct acpi_gpe_event_info *gpe_event_info)
{
	struct acpi_gpe_register_info *gpe_register_info;
	u32 register_bit;

	ACPI_FUNCTION_TRACE(ev_update_gpe_enable_mask);

	gpe_register_info = gpe_event_info->register_info;
	if (!gpe_register_info) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	register_bit = acpi_hw_get_gpe_register_bit(gpe_event_info);

	/* Clear the run bit up front */

	ACPI_CLEAR_BIT(gpe_register_info->enable_for_run, register_bit);

	/* Set the mask bit only if there are references to this GPE */

	if (gpe_event_info->runtime_count) {
		ACPI_SET_BIT(gpe_register_info->enable_for_run,
			     (u8)register_bit);
	}

	gpe_register_info->enable_mask = gpe_register_info->enable_for_run;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_enable_gpe
 *
 * PARAMETERS:  gpe_event_info          - GPE to enable
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable a GPE.
 *
 ******************************************************************************/

acpi_status acpi_ev_enable_gpe(struct acpi_gpe_event_info *gpe_event_info)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_enable_gpe);

	/* Clear the GPE status */
	status = acpi_hw_clear_gpe(gpe_event_info);
	if (ACPI_FAILURE(status))
		return_ACPI_STATUS(status);

	/* Enable the requested GPE */
	status = acpi_hw_low_set_gpe(gpe_event_info, ACPI_GPE_ENABLE);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_mask_gpe
 *
 * PARAMETERS:  gpe_event_info          - GPE to be blocked/unblocked
 *              is_masked               - Whether the GPE is masked or not
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Unconditionally mask/unmask a GPE during runtime.
 *
 ******************************************************************************/

acpi_status
acpi_ev_mask_gpe(struct acpi_gpe_event_info *gpe_event_info, u8 is_masked)
{
	struct acpi_gpe_register_info *gpe_register_info;
	u32 register_bit;

	ACPI_FUNCTION_TRACE(ev_mask_gpe);

	gpe_register_info = gpe_event_info->register_info;
	if (!gpe_register_info) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	register_bit = acpi_hw_get_gpe_register_bit(gpe_event_info);

	/* Perform the action */

	if (is_masked) {
		if (register_bit & gpe_register_info->mask_for_run) {
			return_ACPI_STATUS(AE_BAD_PARAMETER);
		}

		(void)acpi_hw_low_set_gpe(gpe_event_info, ACPI_GPE_DISABLE);
		ACPI_SET_BIT(gpe_register_info->mask_for_run, (u8)register_bit);
	} else {
		if (!(register_bit & gpe_register_info->mask_for_run)) {
			return_ACPI_STATUS(AE_BAD_PARAMETER);
		}

		ACPI_CLEAR_BIT(gpe_register_info->mask_for_run,
			       (u8)register_bit);
		if (gpe_event_info->runtime_count
		    && !gpe_event_info->disable_for_dispatch) {
			(void)acpi_hw_low_set_gpe(gpe_event_info,
						  ACPI_GPE_ENABLE);
		}
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_add_gpe_reference
 *
 * PARAMETERS:  gpe_event_info          - Add a reference to this GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Add a reference to a GPE. On the first reference, the GPE is
 *              hardware-enabled.
 *
 ******************************************************************************/

acpi_status
acpi_ev_add_gpe_reference(struct acpi_gpe_event_info *gpe_event_info)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(ev_add_gpe_reference);

	if (gpe_event_info->runtime_count == ACPI_UINT8_MAX) {
		return_ACPI_STATUS(AE_LIMIT);
	}

	gpe_event_info->runtime_count++;
	if (gpe_event_info->runtime_count == 1) {

		/* Enable on first reference */

		status = acpi_ev_update_gpe_enable_mask(gpe_event_info);
		if (ACPI_SUCCESS(status)) {
			status = acpi_ev_enable_gpe(gpe_event_info);
		}

		if (ACPI_FAILURE(status)) {
			gpe_event_info->runtime_count--;
		}
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_remove_gpe_reference
 *
 * PARAMETERS:  gpe_event_info          - Remove a reference to this GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a reference to a GPE. When the last reference is
 *              removed, the GPE is hardware-disabled.
 *
 ******************************************************************************/

acpi_status
acpi_ev_remove_gpe_reference(struct acpi_gpe_event_info *gpe_event_info)
{
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE(ev_remove_gpe_reference);

	if (!gpe_event_info->runtime_count) {
		return_ACPI_STATUS(AE_LIMIT);
	}

	gpe_event_info->runtime_count--;
	if (!gpe_event_info->runtime_count) {

		/* Disable on last reference */

		status = acpi_ev_update_gpe_enable_mask(gpe_event_info);
		if (ACPI_SUCCESS(status)) {
			status =
			    acpi_hw_low_set_gpe(gpe_event_info,
						ACPI_GPE_DISABLE);
		}

		if (ACPI_FAILURE(status)) {
			gpe_event_info->runtime_count++;
		}
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_low_get_gpe_info
 *
 * PARAMETERS:  gpe_number          - Raw GPE number
 *              gpe_block           - A GPE info block
 *
 * RETURN:      A GPE event_info struct. NULL if not a valid GPE (The gpe_number
 *              is not within the specified GPE block)
 *
 * DESCRIPTION: Returns the event_info struct associated with this GPE. This is
 *              the low-level implementation of ev_get_gpe_event_info.
 *
 ******************************************************************************/

struct acpi_gpe_event_info *acpi_ev_low_get_gpe_info(u32 gpe_number,
						     struct acpi_gpe_block_info
						     *gpe_block)
{
	u32 gpe_index;

	/*
	 * Validate that the gpe_number is within the specified gpe_block.
	 * (Two steps)
	 */
	if (!gpe_block || (gpe_number < gpe_block->block_base_number)) {
		return (NULL);
	}

	gpe_index = gpe_number - gpe_block->block_base_number;
	if (gpe_index >= gpe_block->gpe_count) {
		return (NULL);
	}

	return (&gpe_block->event_info[gpe_index]);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_get_gpe_event_info
 *
 * PARAMETERS:  gpe_device          - Device node. NULL for GPE0/GPE1
 *              gpe_number          - Raw GPE number
 *
 * RETURN:      A GPE event_info struct. NULL if not a valid GPE
 *
 * DESCRIPTION: Returns the event_info struct associated with this GPE.
 *              Validates the gpe_block and the gpe_number
 *
 *              Should be called only when the GPE lists are semaphore locked
 *              and not subject to change.
 *
 ******************************************************************************/

struct acpi_gpe_event_info *acpi_ev_get_gpe_event_info(acpi_handle gpe_device,
						       u32 gpe_number)
{
	union acpi_operand_object *obj_desc;
	struct acpi_gpe_event_info *gpe_info;
	u32 i;

	ACPI_FUNCTION_ENTRY();

	/* A NULL gpe_device means use the FADT-defined GPE block(s) */

	if (!gpe_device) {

		/* Examine GPE Block 0 and 1 (These blocks are permanent) */

		for (i = 0; i < ACPI_MAX_GPE_BLOCKS; i++) {
			gpe_info = acpi_ev_low_get_gpe_info(gpe_number,
							    acpi_gbl_gpe_fadt_blocks
							    [i]);
			if (gpe_info) {
				return (gpe_info);
			}
		}

		/* The gpe_number was not in the range of either FADT GPE block */

		return (NULL);
	}

	/* A Non-NULL gpe_device means this is a GPE Block Device */

	obj_desc =
	    acpi_ns_get_attached_object((struct acpi_namespace_node *)
					       gpe_device);
	if (!obj_desc || !obj_desc->device.gpe_block) {
		return (NULL);
	}

	return (acpi_ev_low_get_gpe_info
		(gpe_number, obj_desc->device.gpe_block));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_gpe_detect
 *
 * PARAMETERS:  gpe_xrupt_list      - Interrupt block for this interrupt.
 *                                    Can have multiple GPE blocks attached.
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Detect if any GP events have occurred. This function is
 *              executed at interrupt level.
 *
 ******************************************************************************/

u32 acpi_ev_gpe_detect(struct acpi_gpe_xrupt_info *gpe_xrupt_list)
{
	struct acpi_gpe_block_info *gpe_block;
	struct acpi_namespace_node *gpe_device;
	struct acpi_gpe_register_info *gpe_register_info;
	struct acpi_gpe_event_info *gpe_event_info;
	u32 gpe_number;
	u32 int_status = ACPI_INTERRUPT_NOT_HANDLED;
	acpi_cpu_flags flags;
	u32 i;
	u32 j;

	ACPI_FUNCTION_NAME(ev_gpe_detect);

	/* Check for the case where there are no GPEs */

	if (!gpe_xrupt_list) {
		return (int_status);
	}

	/*
	 * We need to obtain the GPE lock for both the data structs and registers
	 * Note: Not necessary to obtain the hardware lock, since the GPE
	 * registers are owned by the gpe_lock.
	 */
	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Examine all GPE blocks attached to this interrupt level */

	gpe_block = gpe_xrupt_list->gpe_block_list_head;
	while (gpe_block) {
		gpe_device = gpe_block->node;

		/*
		 * Read all of the 8-bit GPE status and enable registers in this GPE
		 * block, saving all of them. Find all currently active GP events.
		 */
		for (i = 0; i < gpe_block->register_count; i++) {

			/* Get the next status/enable pair */

			gpe_register_info = &gpe_block->register_info[i];

			/*
			 * Optimization: If there are no GPEs enabled within this
			 * register, we can safely ignore the entire register.
			 */
			if (!(gpe_register_info->enable_for_run |
			      gpe_register_info->enable_for_wake)) {
				ACPI_DEBUG_PRINT((ACPI_DB_INTERRUPTS,
						  "Ignore disabled registers for GPE %02X-%02X: "
						  "RunEnable=%02X, WakeEnable=%02X\n",
						  gpe_register_info->
						  base_gpe_number,
						  gpe_register_info->
						  base_gpe_number +
						  (ACPI_GPE_REGISTER_WIDTH - 1),
						  gpe_register_info->
						  enable_for_run,
						  gpe_register_info->
						  enable_for_wake));
				continue;
			}

			/* Now look at the individual GPEs in this byte register */

			for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++) {

				/* Detect and dispatch one GPE bit */

				gpe_event_info =
				    &gpe_block->
				    event_info[((acpi_size)i *
						ACPI_GPE_REGISTER_WIDTH) + j];
				gpe_number =
				    j + gpe_register_info->base_gpe_number;
				acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
				int_status |=
				    acpi_ev_detect_gpe(gpe_device,
						       gpe_event_info,
						       gpe_number);
				flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);
			}
		}

		gpe_block = gpe_block->next;
	}

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return (int_status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_asynch_execute_gpe_method
 *
 * PARAMETERS:  Context (gpe_event_info) - Info for this GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Perform the actual execution of a GPE control method. This
 *              function is called from an invocation of acpi_os_execute and
 *              therefore does NOT execute at interrupt level - so that
 *              the control method itself is not executed in the context of
 *              an interrupt handler.
 *
 ******************************************************************************/

static void ACPI_SYSTEM_XFACE acpi_ev_asynch_execute_gpe_method(void *context)
{
	struct acpi_gpe_event_info *gpe_event_info = context;
	acpi_status status = AE_OK;
	struct acpi_evaluate_info *info;
	struct acpi_gpe_notify_info *notify;

	ACPI_FUNCTION_TRACE(ev_asynch_execute_gpe_method);

	/* Do the correct dispatch - normal method or implicit notify */

	switch (ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags)) {
	case ACPI_GPE_DISPATCH_NOTIFY:
		/*
		 * Implicit notify.
		 * Dispatch a DEVICE_WAKE notify to the appropriate handler.
		 * NOTE: the request is queued for execution after this method
		 * completes. The notify handlers are NOT invoked synchronously
		 * from this thread -- because handlers may in turn run other
		 * control methods.
		 *
		 * June 2012: Expand implicit notify mechanism to support
		 * notifies on multiple device objects.
		 */
		notify = gpe_event_info->dispatch.notify_list;
		while (ACPI_SUCCESS(status) && notify) {
			status =
			    acpi_ev_queue_notify_request(notify->device_node,
							 ACPI_NOTIFY_DEVICE_WAKE);

			notify = notify->next;
		}

		break;

	case ACPI_GPE_DISPATCH_METHOD:

		/* Allocate the evaluation information block */

		info = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_evaluate_info));
		if (!info) {
			status = AE_NO_MEMORY;
		} else {
			/*
			 * Invoke the GPE Method (_Lxx, _Exx) i.e., evaluate the
			 * _Lxx/_Exx control method that corresponds to this GPE
			 */
			info->prefix_node =
			    gpe_event_info->dispatch.method_node;
			info->flags = ACPI_IGNORE_RETURN_VALUE;

			status = acpi_ns_evaluate(info);
			ACPI_FREE(info);
		}

		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"while evaluating GPE method [%4.4s]",
					acpi_ut_get_node_name(gpe_event_info->
							      dispatch.
							      method_node)));
		}
		break;

	default:

		goto error_exit;	/* Should never happen */
	}

	/* Defer enabling of GPE until all notify handlers are done */

	status = acpi_os_execute(OSL_NOTIFY_HANDLER,
				 acpi_ev_asynch_enable_gpe, gpe_event_info);
	if (ACPI_SUCCESS(status)) {
		return_VOID;
	}

error_exit:
	acpi_ev_asynch_enable_gpe(gpe_event_info);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_asynch_enable_gpe
 *
 * PARAMETERS:  Context (gpe_event_info) - Info for this GPE
 *              Callback from acpi_os_execute
 *
 * RETURN:      None
 *
 * DESCRIPTION: Asynchronous clear/enable for GPE. This allows the GPE to
 *              complete (i.e., finish execution of Notify)
 *
 ******************************************************************************/

static void ACPI_SYSTEM_XFACE acpi_ev_asynch_enable_gpe(void *context)
{
	struct acpi_gpe_event_info *gpe_event_info = context;
	acpi_cpu_flags flags;

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);
	(void)acpi_ev_finish_gpe(gpe_event_info);
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_finish_gpe
 *
 * PARAMETERS:  gpe_event_info      - Info for this GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear/Enable a GPE. Common code that is used after execution
 *              of a GPE method or a synchronous or asynchronous GPE handler.
 *
 ******************************************************************************/

acpi_status acpi_ev_finish_gpe(struct acpi_gpe_event_info *gpe_event_info)
{
	acpi_status status;

	if ((gpe_event_info->flags & ACPI_GPE_XRUPT_TYPE_MASK) ==
	    ACPI_GPE_LEVEL_TRIGGERED) {
		/*
		 * GPE is level-triggered, we clear the GPE status bit after
		 * handling the event.
		 */
		status = acpi_hw_clear_gpe(gpe_event_info);
		if (ACPI_FAILURE(status)) {
			return (status);
		}
	}

	/*
	 * Enable this GPE, conditionally. This means that the GPE will
	 * only be physically enabled if the enable_mask bit is set
	 * in the event_info.
	 */
	(void)acpi_hw_low_set_gpe(gpe_event_info, ACPI_GPE_CONDITIONAL_ENABLE);
	gpe_event_info->disable_for_dispatch = FALSE;
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_detect_gpe
 *
 * PARAMETERS:  gpe_device          - Device node. NULL for GPE0/GPE1
 *              gpe_event_info      - Info for this GPE
 *              gpe_number          - Number relative to the parent GPE block
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Detect and dispatch a General Purpose Event to either a function
 *              (e.g. EC) or method (e.g. _Lxx/_Exx) handler.
 * NOTE:        GPE is W1C, so it is possible to handle a single GPE from both
 *              task and irq context in parallel as long as the process to
 *              detect and mask the GPE is atomic.
 *              However the atomicity of ACPI_GPE_DISPATCH_RAW_HANDLER is
 *              dependent on the raw handler itself.
 *
 ******************************************************************************/

u32
acpi_ev_detect_gpe(struct acpi_namespace_node *gpe_device,
		   struct acpi_gpe_event_info *gpe_event_info, u32 gpe_number)
{
	u32 int_status = ACPI_INTERRUPT_NOT_HANDLED;
	u8 enabled_status_byte;
	u64 status_reg;
	u64 enable_reg;
	u32 register_bit;
	struct acpi_gpe_register_info *gpe_register_info;
	struct acpi_gpe_handler_info *gpe_handler_info;
	acpi_cpu_flags flags;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_gpe_detect);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	if (!gpe_event_info) {
		gpe_event_info = acpi_ev_get_gpe_event_info(gpe_device, gpe_number);
		if (!gpe_event_info)
			goto error_exit;
	}

	/* Get the info block for the entire GPE register */

	gpe_register_info = gpe_event_info->register_info;

	/* Get the register bitmask for this GPE */

	register_bit = acpi_hw_get_gpe_register_bit(gpe_event_info);

	/* GPE currently enabled (enable bit == 1)? */

	status = acpi_hw_read(&enable_reg, &gpe_register_info->enable_address);
	if (ACPI_FAILURE(status)) {
		goto error_exit;
	}

	/* GPE currently active (status bit == 1)? */

	status = acpi_hw_read(&status_reg, &gpe_register_info->status_address);
	if (ACPI_FAILURE(status)) {
		goto error_exit;
	}

	/* Check if there is anything active at all in this GPE */

	ACPI_DEBUG_PRINT((ACPI_DB_INTERRUPTS,
			  "Read registers for GPE %02X: Status=%02X, Enable=%02X, "
			  "RunEnable=%02X, WakeEnable=%02X\n",
			  gpe_number,
			  (u32)(status_reg & register_bit),
			  (u32)(enable_reg & register_bit),
			  gpe_register_info->enable_for_run,
			  gpe_register_info->enable_for_wake));

	enabled_status_byte = (u8)(status_reg & enable_reg);
	if (!(enabled_status_byte & register_bit)) {
		goto error_exit;
	}

	/* Invoke global event handler if present */

	acpi_gpe_count++;
	if (acpi_gbl_global_event_handler) {
		acpi_gbl_global_event_handler(ACPI_EVENT_TYPE_GPE,
					      gpe_device, gpe_number,
					      acpi_gbl_global_event_handler_context);
	}

	/* Found an active GPE */

	if (ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) ==
	    ACPI_GPE_DISPATCH_RAW_HANDLER) {

		/* Dispatch the event to a raw handler */

		gpe_handler_info = gpe_event_info->dispatch.handler;

		/*
		 * There is no protection around the namespace node
		 * and the GPE handler to ensure a safe destruction
		 * because:
		 * 1. The namespace node is expected to always
		 *    exist after loading a table.
		 * 2. The GPE handler is expected to be flushed by
		 *    acpi_os_wait_events_complete() before the
		 *    destruction.
		 */
		acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
		int_status |=
		    gpe_handler_info->address(gpe_device, gpe_number,
					      gpe_handler_info->context);
		flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);
	} else {
		/* Dispatch the event to a standard handler or method. */

		int_status |= acpi_ev_gpe_dispatch(gpe_device,
						   gpe_event_info, gpe_number);
	}

error_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return (int_status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_gpe_dispatch
 *
 * PARAMETERS:  gpe_device          - Device node. NULL for GPE0/GPE1
 *              gpe_event_info      - Info for this GPE
 *              gpe_number          - Number relative to the parent GPE block
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Dispatch a General Purpose Event to either a function (e.g. EC)
 *              or method (e.g. _Lxx/_Exx) handler.
 *
 ******************************************************************************/

u32
acpi_ev_gpe_dispatch(struct acpi_namespace_node *gpe_device,
		     struct acpi_gpe_event_info *gpe_event_info, u32 gpe_number)
{
	acpi_status status;
	u32 return_value;

	ACPI_FUNCTION_TRACE(ev_gpe_dispatch);

	/*
	 * Always disable the GPE so that it does not keep firing before
	 * any asynchronous activity completes (either from the execution
	 * of a GPE method or an asynchronous GPE handler.)
	 *
	 * If there is no handler or method to run, just disable the
	 * GPE and leave it disabled permanently to prevent further such
	 * pointless events from firing.
	 */
	status = acpi_hw_low_set_gpe(gpe_event_info, ACPI_GPE_DISABLE);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status,
				"Unable to disable GPE %02X", gpe_number));
		return_UINT32(ACPI_INTERRUPT_NOT_HANDLED);
	}

	/*
	 * If edge-triggered, clear the GPE status bit now. Note that
	 * level-triggered events are cleared after the GPE is serviced.
	 */
	if ((gpe_event_info->flags & ACPI_GPE_XRUPT_TYPE_MASK) ==
	    ACPI_GPE_EDGE_TRIGGERED) {
		status = acpi_hw_clear_gpe(gpe_event_info);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Unable to clear GPE %02X",
					gpe_number));
			(void)acpi_hw_low_set_gpe(gpe_event_info,
						  ACPI_GPE_CONDITIONAL_ENABLE);
			return_UINT32(ACPI_INTERRUPT_NOT_HANDLED);
		}
	}

	gpe_event_info->disable_for_dispatch = TRUE;

	/*
	 * Dispatch the GPE to either an installed handler or the control
	 * method associated with this GPE (_Lxx or _Exx). If a handler
	 * exists, we invoke it and do not attempt to run the method.
	 * If there is neither a handler nor a method, leave the GPE
	 * disabled.
	 */
	switch (ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags)) {
	case ACPI_GPE_DISPATCH_HANDLER:

		/* Invoke the installed handler (at interrupt level) */

		return_value =
		    gpe_event_info->dispatch.handler->address(gpe_device,
							      gpe_number,
							      gpe_event_info->
							      dispatch.handler->
							      context);

		/* If requested, clear (if level-triggered) and re-enable the GPE */

		if (return_value & ACPI_REENABLE_GPE) {
			(void)acpi_ev_finish_gpe(gpe_event_info);
		}
		break;

	case ACPI_GPE_DISPATCH_METHOD:
	case ACPI_GPE_DISPATCH_NOTIFY:
		/*
		 * Execute the method associated with the GPE
		 * NOTE: Level-triggered GPEs are cleared after the method completes.
		 */
		status = acpi_os_execute(OSL_GPE_HANDLER,
					 acpi_ev_asynch_execute_gpe_method,
					 gpe_event_info);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Unable to queue handler for GPE %02X - event disabled",
					gpe_number));
		}
		break;

	default:
		/*
		 * No handler or method to run!
		 * 03/2010: This case should no longer be possible. We will not allow
		 * a GPE to be enabled if it has no handler or method.
		 */
		ACPI_ERROR((AE_INFO,
			    "No handler or method for GPE %02X, disabling event",
			    gpe_number));

		break;
	}

	return_UINT32(ACPI_INTERRUPT_HANDLED);
}

#endif				/* !ACPI_REDUCED_HARDWARE */
