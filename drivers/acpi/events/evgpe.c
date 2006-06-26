/******************************************************************************
 *
 * Module Name: evgpe - General Purpose Event handling and dispatch
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

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evgpe")

/* Local prototypes */
static void ACPI_SYSTEM_XFACE acpi_ev_asynch_execute_gpe_method(void *context);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_set_gpe_type
 *
 * PARAMETERS:  gpe_event_info          - GPE to set
 *              Type                    - New type
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Sets the new type for the GPE (wake, run, or wake/run)
 *
 ******************************************************************************/

acpi_status
acpi_ev_set_gpe_type(struct acpi_gpe_event_info *gpe_event_info, u8 type)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_set_gpe_type);

	/* Validate type and update register enable masks */

	switch (type) {
	case ACPI_GPE_TYPE_WAKE:
	case ACPI_GPE_TYPE_RUNTIME:
	case ACPI_GPE_TYPE_WAKE_RUN:
		break;

	default:
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Disable the GPE if currently enabled */

	status = acpi_ev_disable_gpe(gpe_event_info);

	/* Type was validated above */

	gpe_event_info->flags &= ~ACPI_GPE_TYPE_MASK;	/* Clear type bits */
	gpe_event_info->flags |= type;	/* Insert type */
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_update_gpe_enable_masks
 *
 * PARAMETERS:  gpe_event_info          - GPE to update
 *              Type                    - What to do: ACPI_GPE_DISABLE or
 *                                        ACPI_GPE_ENABLE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Updates GPE register enable masks based on the GPE type
 *
 ******************************************************************************/

acpi_status
acpi_ev_update_gpe_enable_masks(struct acpi_gpe_event_info *gpe_event_info,
				u8 type)
{
	struct acpi_gpe_register_info *gpe_register_info;
	u8 register_bit;

	ACPI_FUNCTION_TRACE(ev_update_gpe_enable_masks);

	gpe_register_info = gpe_event_info->register_info;
	if (!gpe_register_info) {
		return_ACPI_STATUS(AE_NOT_EXIST);
	}
	register_bit = gpe_event_info->register_bit;

	/* 1) Disable case.  Simply clear all enable bits */

	if (type == ACPI_GPE_DISABLE) {
		ACPI_CLEAR_BIT(gpe_register_info->enable_for_wake,
			       register_bit);
		ACPI_CLEAR_BIT(gpe_register_info->enable_for_run, register_bit);
		return_ACPI_STATUS(AE_OK);
	}

	/* 2) Enable case.  Set/Clear the appropriate enable bits */

	switch (gpe_event_info->flags & ACPI_GPE_TYPE_MASK) {
	case ACPI_GPE_TYPE_WAKE:
		ACPI_SET_BIT(gpe_register_info->enable_for_wake, register_bit);
		ACPI_CLEAR_BIT(gpe_register_info->enable_for_run, register_bit);
		break;

	case ACPI_GPE_TYPE_RUNTIME:
		ACPI_CLEAR_BIT(gpe_register_info->enable_for_wake,
			       register_bit);
		ACPI_SET_BIT(gpe_register_info->enable_for_run, register_bit);
		break;

	case ACPI_GPE_TYPE_WAKE_RUN:
		ACPI_SET_BIT(gpe_register_info->enable_for_wake, register_bit);
		ACPI_SET_BIT(gpe_register_info->enable_for_run, register_bit);
		break;

	default:
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_enable_gpe
 *
 * PARAMETERS:  gpe_event_info          - GPE to enable
 *              write_to_hardware       - Enable now, or just mark data structs
 *                                        (WAKE GPEs should be deferred)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable a GPE based on the GPE type
 *
 ******************************************************************************/

acpi_status
acpi_ev_enable_gpe(struct acpi_gpe_event_info *gpe_event_info,
		   u8 write_to_hardware)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_enable_gpe);

	/* Make sure HW enable masks are updated */

	status =
	    acpi_ev_update_gpe_enable_masks(gpe_event_info, ACPI_GPE_ENABLE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Mark wake-enabled or HW enable, or both */

	switch (gpe_event_info->flags & ACPI_GPE_TYPE_MASK) {
	case ACPI_GPE_TYPE_WAKE:

		ACPI_SET_BIT(gpe_event_info->flags, ACPI_GPE_WAKE_ENABLED);
		break;

	case ACPI_GPE_TYPE_WAKE_RUN:

		ACPI_SET_BIT(gpe_event_info->flags, ACPI_GPE_WAKE_ENABLED);

		/*lint -fallthrough */

	case ACPI_GPE_TYPE_RUNTIME:

		ACPI_SET_BIT(gpe_event_info->flags, ACPI_GPE_RUN_ENABLED);

		if (write_to_hardware) {

			/* Clear the GPE (of stale events), then enable it */

			status = acpi_hw_clear_gpe(gpe_event_info);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}

			/* Enable the requested runtime GPE */

			status = acpi_hw_write_gpe_enable_reg(gpe_event_info);
		}
		break;

	default:
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_disable_gpe
 *
 * PARAMETERS:  gpe_event_info          - GPE to disable
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disable a GPE based on the GPE type
 *
 ******************************************************************************/

acpi_status acpi_ev_disable_gpe(struct acpi_gpe_event_info *gpe_event_info)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_disable_gpe);

	if (!(gpe_event_info->flags & ACPI_GPE_ENABLE_MASK)) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Make sure HW enable masks are updated */

	status =
	    acpi_ev_update_gpe_enable_masks(gpe_event_info, ACPI_GPE_DISABLE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Mark wake-disabled or HW disable, or both */

	switch (gpe_event_info->flags & ACPI_GPE_TYPE_MASK) {
	case ACPI_GPE_TYPE_WAKE:
		ACPI_CLEAR_BIT(gpe_event_info->flags, ACPI_GPE_WAKE_ENABLED);
		break;

	case ACPI_GPE_TYPE_WAKE_RUN:
		ACPI_CLEAR_BIT(gpe_event_info->flags, ACPI_GPE_WAKE_ENABLED);

		/*lint -fallthrough */

	case ACPI_GPE_TYPE_RUNTIME:

		/* Disable the requested runtime GPE */

		ACPI_CLEAR_BIT(gpe_event_info->flags, ACPI_GPE_RUN_ENABLED);
		status = acpi_hw_write_gpe_enable_reg(gpe_event_info);
		break;

	default:
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_get_gpe_event_info
 *
 * PARAMETERS:  gpe_device          - Device node.  NULL for GPE0/GPE1
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
	struct acpi_gpe_block_info *gpe_block;
	acpi_native_uint i;

	ACPI_FUNCTION_ENTRY();

	/* A NULL gpe_block means use the FADT-defined GPE block(s) */

	if (!gpe_device) {

		/* Examine GPE Block 0 and 1 (These blocks are permanent) */

		for (i = 0; i < ACPI_MAX_GPE_BLOCKS; i++) {
			gpe_block = acpi_gbl_gpe_fadt_blocks[i];
			if (gpe_block) {
				if ((gpe_number >= gpe_block->block_base_number)
				    && (gpe_number <
					gpe_block->block_base_number +
					(gpe_block->register_count * 8))) {
					return (&gpe_block->
						event_info[gpe_number -
							   gpe_block->
							   block_base_number]);
				}
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

	gpe_block = obj_desc->device.gpe_block;

	if ((gpe_number >= gpe_block->block_base_number) &&
	    (gpe_number <
	     gpe_block->block_base_number + (gpe_block->register_count * 8))) {
		return (&gpe_block->
			event_info[gpe_number - gpe_block->block_base_number]);
	}

	return (NULL);
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
 * DESCRIPTION: Detect if any GP events have occurred.  This function is
 *              executed at interrupt level.
 *
 ******************************************************************************/

u32 acpi_ev_gpe_detect(struct acpi_gpe_xrupt_info * gpe_xrupt_list)
{
	acpi_status status;
	struct acpi_gpe_block_info *gpe_block;
	struct acpi_gpe_register_info *gpe_register_info;
	u32 int_status = ACPI_INTERRUPT_NOT_HANDLED;
	u8 enabled_status_byte;
	u32 status_reg;
	u32 enable_reg;
	acpi_cpu_flags flags;
	acpi_cpu_flags hw_flags;
	acpi_native_uint i;
	acpi_native_uint j;

	ACPI_FUNCTION_NAME(ev_gpe_detect);

	/* Check for the case where there are no GPEs */

	if (!gpe_xrupt_list) {
		return (int_status);
	}

	/* We need to hold the GPE lock now, hardware lock in the loop */

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Examine all GPE blocks attached to this interrupt level */

	gpe_block = gpe_xrupt_list->gpe_block_list_head;
	while (gpe_block) {
		/*
		 * Read all of the 8-bit GPE status and enable registers
		 * in this GPE block, saving all of them.
		 * Find all currently active GP events.
		 */
		for (i = 0; i < gpe_block->register_count; i++) {

			/* Get the next status/enable pair */

			gpe_register_info = &gpe_block->register_info[i];

			hw_flags = acpi_os_acquire_lock(acpi_gbl_hardware_lock);

			/* Read the Status Register */

			status =
			    acpi_hw_low_level_read(ACPI_GPE_REGISTER_WIDTH,
						   &status_reg,
						   &gpe_register_info->
						   status_address);
			if (ACPI_FAILURE(status)) {
				acpi_os_release_lock(acpi_gbl_hardware_lock,
						     hw_flags);
				goto unlock_and_exit;
			}

			/* Read the Enable Register */

			status =
			    acpi_hw_low_level_read(ACPI_GPE_REGISTER_WIDTH,
						   &enable_reg,
						   &gpe_register_info->
						   enable_address);
			acpi_os_release_lock(acpi_gbl_hardware_lock, hw_flags);

			if (ACPI_FAILURE(status)) {
				goto unlock_and_exit;
			}

			ACPI_DEBUG_PRINT((ACPI_DB_INTERRUPTS,
					  "Read GPE Register at GPE%X: Status=%02X, Enable=%02X\n",
					  gpe_register_info->base_gpe_number,
					  status_reg, enable_reg));

			/* Check if there is anything active at all in this register */

			enabled_status_byte = (u8) (status_reg & enable_reg);
			if (!enabled_status_byte) {

				/* No active GPEs in this register, move on */

				continue;
			}

			/* Now look at the individual GPEs in this byte register */

			for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++) {

				/* Examine one GPE bit */

				if (enabled_status_byte &
				    acpi_gbl_decode_to8bit[j]) {
					/*
					 * Found an active GPE. Dispatch the event to a handler
					 * or method.
					 */
					int_status |=
					    acpi_ev_gpe_dispatch(&gpe_block->
								 event_info[(i *
									     ACPI_GPE_REGISTER_WIDTH)
									    +
									    j],
								 (u32) j +
								 gpe_register_info->
								 base_gpe_number);
				}
			}
		}

		gpe_block = gpe_block->next;
	}

      unlock_and_exit:

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
	struct acpi_gpe_event_info *gpe_event_info = (void *)context;
	acpi_status status;
	struct acpi_gpe_event_info local_gpe_event_info;
	struct acpi_evaluate_info *info;

	ACPI_FUNCTION_TRACE(ev_asynch_execute_gpe_method);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_VOID;
	}

	/* Must revalidate the gpe_number/gpe_block */

	if (!acpi_ev_valid_gpe_event(gpe_event_info)) {
		status = acpi_ut_release_mutex(ACPI_MTX_EVENTS);
		return_VOID;
	}

	/* Set the GPE flags for return to enabled state */

	(void)acpi_ev_enable_gpe(gpe_event_info, FALSE);

	/*
	 * Take a snapshot of the GPE info for this level - we copy the
	 * info to prevent a race condition with remove_handler/remove_block.
	 */
	ACPI_MEMCPY(&local_gpe_event_info, gpe_event_info,
		    sizeof(struct acpi_gpe_event_info));

	status = acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_VOID;
	}

	/*
	 * Must check for control method type dispatch one more
	 * time to avoid race with ev_gpe_install_handler
	 */
	if ((local_gpe_event_info.flags & ACPI_GPE_DISPATCH_MASK) ==
	    ACPI_GPE_DISPATCH_METHOD) {

		/* Allocate the evaluation information block */

		info = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_evaluate_info));
		if (!info) {
			status = AE_NO_MEMORY;
		} else {
			/*
			 * Invoke the GPE Method (_Lxx, _Exx) i.e., evaluate the _Lxx/_Exx
			 * control method that corresponds to this GPE
			 */
			info->prefix_node =
			    local_gpe_event_info.dispatch.method_node;
			info->parameters =
			    ACPI_CAST_PTR(union acpi_operand_object *,
					  gpe_event_info);
			info->parameter_type = ACPI_PARAM_GPE;
			info->flags = ACPI_IGNORE_RETURN_VALUE;

			status = acpi_ns_evaluate(info);
			ACPI_FREE(info);
		}

		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"While evaluating GPE method [%4.4s]",
					acpi_ut_get_node_name
					(local_gpe_event_info.dispatch.
					 method_node)));
		}
	}

	if ((local_gpe_event_info.flags & ACPI_GPE_XRUPT_TYPE_MASK) ==
	    ACPI_GPE_LEVEL_TRIGGERED) {
		/*
		 * GPE is level-triggered, we clear the GPE status bit after
		 * handling the event.
		 */
		status = acpi_hw_clear_gpe(&local_gpe_event_info);
		if (ACPI_FAILURE(status)) {
			return_VOID;
		}
	}

	/* Enable this GPE */

	(void)acpi_hw_write_gpe_enable_reg(&local_gpe_event_info);
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_gpe_dispatch
 *
 * PARAMETERS:  gpe_event_info  - Info for this GPE
 *              gpe_number      - Number relative to the parent GPE block
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Dispatch a General Purpose Event to either a function (e.g. EC)
 *              or method (e.g. _Lxx/_Exx) handler.
 *
 *              This function executes at interrupt level.
 *
 ******************************************************************************/

u32
acpi_ev_gpe_dispatch(struct acpi_gpe_event_info *gpe_event_info, u32 gpe_number)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_gpe_dispatch);

	/*
	 * If edge-triggered, clear the GPE status bit now.  Note that
	 * level-triggered events are cleared after the GPE is serviced.
	 */
	if ((gpe_event_info->flags & ACPI_GPE_XRUPT_TYPE_MASK) ==
	    ACPI_GPE_EDGE_TRIGGERED) {
		status = acpi_hw_clear_gpe(gpe_event_info);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Unable to clear GPE[%2X]",
					gpe_number));
			return_UINT32(ACPI_INTERRUPT_NOT_HANDLED);
		}
	}

	/* Save current system state */

	if (acpi_gbl_system_awake_and_running) {
		ACPI_SET_BIT(gpe_event_info->flags, ACPI_GPE_SYSTEM_RUNNING);
	} else {
		ACPI_CLEAR_BIT(gpe_event_info->flags, ACPI_GPE_SYSTEM_RUNNING);
	}

	/*
	 * Dispatch the GPE to either an installed handler, or the control
	 * method associated with this GPE (_Lxx or _Exx).
	 * If a handler exists, we invoke it and do not attempt to run the method.
	 * If there is neither a handler nor a method, we disable the level to
	 * prevent further events from coming in here.
	 */
	switch (gpe_event_info->flags & ACPI_GPE_DISPATCH_MASK) {
	case ACPI_GPE_DISPATCH_HANDLER:

		/*
		 * Invoke the installed handler (at interrupt level)
		 * Ignore return status for now.  TBD: leave GPE disabled on error?
		 */
		(void)gpe_event_info->dispatch.handler->address(gpe_event_info->
								dispatch.
								handler->
								context);

		/* It is now safe to clear level-triggered events. */

		if ((gpe_event_info->flags & ACPI_GPE_XRUPT_TYPE_MASK) ==
		    ACPI_GPE_LEVEL_TRIGGERED) {
			status = acpi_hw_clear_gpe(gpe_event_info);
			if (ACPI_FAILURE(status)) {
				ACPI_EXCEPTION((AE_INFO, status,
						"Unable to clear GPE[%2X]",
						gpe_number));
				return_UINT32(ACPI_INTERRUPT_NOT_HANDLED);
			}
		}
		break;

	case ACPI_GPE_DISPATCH_METHOD:

		/*
		 * Disable GPE, so it doesn't keep firing before the method has a
		 * chance to run.
		 */
		status = acpi_ev_disable_gpe(gpe_event_info);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Unable to disable GPE[%2X]",
					gpe_number));
			return_UINT32(ACPI_INTERRUPT_NOT_HANDLED);
		}

		/*
		 * Execute the method associated with the GPE
		 * NOTE: Level-triggered GPEs are cleared after the method completes.
		 */
		status = acpi_os_execute(OSL_GPE_HANDLER,
					 acpi_ev_asynch_execute_gpe_method,
					 gpe_event_info);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Unable to queue handler for GPE[%2X] - event disabled",
					gpe_number));
		}
		break;

	default:

		/* No handler or method to run! */

		ACPI_ERROR((AE_INFO,
			    "No handler or method for GPE[%2X], disabling event",
			    gpe_number));

		/*
		 * Disable the GPE.  The GPE will remain disabled until the ACPI
		 * Core Subsystem is restarted, or a handler is installed.
		 */
		status = acpi_ev_disable_gpe(gpe_event_info);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Unable to disable GPE[%2X]",
					gpe_number));
			return_UINT32(ACPI_INTERRUPT_NOT_HANDLED);
		}
		break;
	}

	return_UINT32(ACPI_INTERRUPT_HANDLED);
}

#ifdef ACPI_GPE_NOTIFY_CHECK
/*******************************************************************************
 * TBD: NOT USED, PROTOTYPE ONLY AND WILL PROBABLY BE REMOVED
 *
 * FUNCTION:    acpi_ev_check_for_wake_only_gpe
 *
 * PARAMETERS:  gpe_event_info  - info for this GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Determine if a a GPE is "wake-only".
 *
 *              Called from Notify() code in interpreter when a "DeviceWake"
 *              Notify comes in.
 *
 ******************************************************************************/

acpi_status
acpi_ev_check_for_wake_only_gpe(struct acpi_gpe_event_info *gpe_event_info)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_check_for_wake_only_gpe);

	if ((gpe_event_info) &&	/* Only >0 for _Lxx/_Exx */
	    ((gpe_event_info->flags & ACPI_GPE_SYSTEM_MASK) == ACPI_GPE_SYSTEM_RUNNING)) {	/* System state at GPE time */
		/* This must be a wake-only GPE, disable it */

		status = acpi_ev_disable_gpe(gpe_event_info);

		/* Set GPE to wake-only.  Do not change wake disabled/enabled status */

		acpi_ev_set_gpe_type(gpe_event_info, ACPI_GPE_TYPE_WAKE);

		ACPI_INFO((AE_INFO,
			   "GPE %p was updated from wake/run to wake-only",
			   gpe_event_info));

		/* This was a wake-only GPE */

		return_ACPI_STATUS(AE_WAKE_ONLY_GPE);
	}

	return_ACPI_STATUS(AE_OK);
}
#endif
