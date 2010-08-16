/******************************************************************************
 *
 * Module Name: evgpe - General Purpose Event handling and dispatch
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2010, Intel Corp.
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
ACPI_MODULE_NAME("evgpe")

/* Local prototypes */
static void ACPI_SYSTEM_XFACE acpi_ev_asynch_execute_gpe_method(void *context);

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

	register_bit = acpi_hw_get_gpe_register_bit(gpe_event_info,
						gpe_register_info);

	/* Clear the run bit up front */

	ACPI_CLEAR_BIT(gpe_register_info->enable_for_run, register_bit);

	/* Set the mask bit only if there are references to this GPE */

	if (gpe_event_info->runtime_count) {
		ACPI_SET_BIT(gpe_register_info->enable_for_run, (u8)register_bit);
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_enable_gpe
 *
 * PARAMETERS:  gpe_event_info  - GPE to enable
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear the given GPE from stale events and enable it.
 *
 ******************************************************************************/
acpi_status
acpi_ev_enable_gpe(struct acpi_gpe_event_info *gpe_event_info)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_enable_gpe);

	/*
	 * We will only allow a GPE to be enabled if it has either an
	 * associated method (_Lxx/_Exx) or a handler. Otherwise, the
	 * GPE will be immediately disabled by acpi_ev_gpe_dispatch the
	 * first time it fires.
	 */
	if (!(gpe_event_info->flags & ACPI_GPE_DISPATCH_MASK)) {
		return_ACPI_STATUS(AE_NO_HANDLER);
	}

	/* Clear the GPE (of stale events) */
	status = acpi_hw_clear_gpe(gpe_event_info);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Enable the requested GPE */
	status = acpi_hw_low_set_gpe(gpe_event_info, ACPI_GPE_ENABLE);

	return_ACPI_STATUS(status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_raw_enable_gpe
 *
 * PARAMETERS:  gpe_event_info  - GPE to enable
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Add a reference to a GPE. On the first reference, the GPE is
 *              hardware-enabled.
 *
 ******************************************************************************/

acpi_status acpi_raw_enable_gpe(struct acpi_gpe_event_info *gpe_event_info)
{
	acpi_status status = AE_OK;

	if (gpe_event_info->runtime_count == ACPI_UINT8_MAX) {
		return_ACPI_STATUS(AE_LIMIT);
	}

	gpe_event_info->runtime_count++;
	if (gpe_event_info->runtime_count == 1) {
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
 * FUNCTION:    acpi_raw_disable_gpe
 *
 * PARAMETERS:  gpe_event_info  - GPE to disable
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a reference to a GPE. When the last reference is
 *              removed, the GPE is hardware-disabled.
 *
 ******************************************************************************/

acpi_status acpi_raw_disable_gpe(struct acpi_gpe_event_info *gpe_event_info)
{
	acpi_status status = AE_OK;

	if (!gpe_event_info->runtime_count) {
		return_ACPI_STATUS(AE_LIMIT);
	}

	gpe_event_info->runtime_count--;
	if (!gpe_event_info->runtime_count) {
		status = acpi_ev_update_gpe_enable_mask(gpe_event_info);
		if (ACPI_SUCCESS(status)) {
			status = acpi_hw_low_set_gpe(gpe_event_info,
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

	obj_desc = acpi_ns_get_attached_object((struct acpi_namespace_node *)
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
		/*
		 * Read all of the 8-bit GPE status and enable registers in this GPE
		 * block, saving all of them. Find all currently active GP events.
		 */
		for (i = 0; i < gpe_block->register_count; i++) {

			/* Get the next status/enable pair */

			gpe_register_info = &gpe_block->register_info[i];

			/* Read the Status Register */

			status =
			    acpi_hw_read(&status_reg,
					 &gpe_register_info->status_address);
			if (ACPI_FAILURE(status)) {
				goto unlock_and_exit;
			}

			/* Read the Enable Register */

			status =
			    acpi_hw_read(&enable_reg,
					 &gpe_register_info->enable_address);
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

				if (enabled_status_byte & (1 << j)) {
					/*
					 * Found an active GPE. Dispatch the event to a handler
					 * or method.
					 */
					int_status |=
					    acpi_ev_gpe_dispatch(&gpe_block->
						event_info[((acpi_size) i * ACPI_GPE_REGISTER_WIDTH) + j], j + gpe_register_info->base_gpe_number);
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
static void acpi_ev_asynch_enable_gpe(void *context);

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

	/*
	 * Take a snapshot of the GPE info for this level - we copy the info to
	 * prevent a race condition with remove_handler/remove_block.
	 */
	ACPI_MEMCPY(&local_gpe_event_info, gpe_event_info,
		    sizeof(struct acpi_gpe_event_info));

	status = acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_VOID;
	}

	/*
	 * Must check for control method type dispatch one more time to avoid a
	 * race with ev_gpe_install_handler
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
			info->flags = ACPI_IGNORE_RETURN_VALUE;

			status = acpi_ns_evaluate(info);
			ACPI_FREE(info);
		}

		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"while evaluating GPE method [%4.4s]",
					acpi_ut_get_node_name
					(local_gpe_event_info.dispatch.
					 method_node)));
		}
	}
	/* Defer enabling of GPE until all notify handlers are done */
	acpi_os_execute(OSL_NOTIFY_HANDLER, acpi_ev_asynch_enable_gpe,
				gpe_event_info);
	return_VOID;
}

static void acpi_ev_asynch_enable_gpe(void *context)
{
	struct acpi_gpe_event_info *gpe_event_info = context;
	acpi_status status;
	if ((gpe_event_info->flags & ACPI_GPE_XRUPT_TYPE_MASK) ==
	    ACPI_GPE_LEVEL_TRIGGERED) {
		/*
		 * GPE is level-triggered, we clear the GPE status bit after handling
		 * the event.
		 */
		status = acpi_hw_clear_gpe(gpe_event_info);
		if (ACPI_FAILURE(status)) {
			return_VOID;
		}
	}

	/*
	 * Enable this GPE, conditionally. This means that the GPE will only be
	 * physically enabled if the enable_for_run bit is set in the event_info
	 */
	(void)acpi_hw_low_set_gpe(gpe_event_info, ACPI_GPE_COND_ENABLE);

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

	acpi_os_gpe_count(gpe_number);

	/*
	 * If edge-triggered, clear the GPE status bit now. Note that
	 * level-triggered events are cleared after the GPE is serviced.
	 */
	if ((gpe_event_info->flags & ACPI_GPE_XRUPT_TYPE_MASK) ==
	    ACPI_GPE_EDGE_TRIGGERED) {
		status = acpi_hw_clear_gpe(gpe_event_info);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Unable to clear GPE[0x%2X]",
					gpe_number));
			return_UINT32(ACPI_INTERRUPT_NOT_HANDLED);
		}
	}

	/*
	 * Dispatch the GPE to either an installed handler, or the control method
	 * associated with this GPE (_Lxx or _Exx). If a handler exists, we invoke
	 * it and do not attempt to run the method. If there is neither a handler
	 * nor a method, we disable this GPE to prevent further such pointless
	 * events from firing.
	 */
	switch (gpe_event_info->flags & ACPI_GPE_DISPATCH_MASK) {
	case ACPI_GPE_DISPATCH_HANDLER:

		/*
		 * Invoke the installed handler (at interrupt level)
		 * Ignore return status for now.
		 * TBD: leave GPE disabled on error?
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
					"Unable to clear GPE[0x%2X]",
						gpe_number));
				return_UINT32(ACPI_INTERRUPT_NOT_HANDLED);
			}
		}
		break;

	case ACPI_GPE_DISPATCH_METHOD:

		/*
		 * Disable the GPE, so it doesn't keep firing before the method has a
		 * chance to run (it runs asynchronously with interrupts enabled).
		 */
		status = acpi_hw_low_set_gpe(gpe_event_info, ACPI_GPE_DISABLE);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Unable to disable GPE[0x%2X]",
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
					"Unable to queue handler for GPE[0x%2X] - event disabled",
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
			    "No handler or method for GPE[0x%2X], disabling event",
			    gpe_number));

		/*
		 * Disable the GPE. The GPE will remain disabled a handler
		 * is installed or ACPICA is restarted.
		 */
		status = acpi_hw_low_set_gpe(gpe_event_info, ACPI_GPE_DISABLE);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Unable to disable GPE[0x%2X]",
					gpe_number));
			return_UINT32(ACPI_INTERRUPT_NOT_HANDLED);
		}
		break;
	}

	return_UINT32(ACPI_INTERRUPT_HANDLED);
}
