/******************************************************************************
 *
 * Module Name: evgpeutil - GPE utilities
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2017, Intel Corp.
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

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evgpeutil")

#if (!ACPI_REDUCED_HARDWARE)	/* Entire module */
/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_walk_gpe_list
 *
 * PARAMETERS:  gpe_walk_callback   - Routine called for each GPE block
 *              context             - Value passed to callback
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk the GPE lists.
 *
 ******************************************************************************/
acpi_status
acpi_ev_walk_gpe_list(acpi_gpe_callback gpe_walk_callback, void *context)
{
	struct acpi_gpe_block_info *gpe_block;
	struct acpi_gpe_xrupt_info *gpe_xrupt_info;
	acpi_status status = AE_OK;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(ev_walk_gpe_list);

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);

	/* Walk the interrupt level descriptor list */

	gpe_xrupt_info = acpi_gbl_gpe_xrupt_list_head;
	while (gpe_xrupt_info) {

		/* Walk all Gpe Blocks attached to this interrupt level */

		gpe_block = gpe_xrupt_info->gpe_block_list_head;
		while (gpe_block) {

			/* One callback per GPE block */

			status =
			    gpe_walk_callback(gpe_xrupt_info, gpe_block,
					      context);
			if (ACPI_FAILURE(status)) {
				if (status == AE_CTRL_END) {	/* Callback abort */
					status = AE_OK;
				}
				goto unlock_and_exit;
			}

			gpe_block = gpe_block->next;
		}

		gpe_xrupt_info = gpe_xrupt_info->next;
	}

unlock_and_exit:
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_get_gpe_device
 *
 * PARAMETERS:  GPE_WALK_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Matches the input GPE index (0-current_gpe_count) with a GPE
 *              block device. NULL if the GPE is one of the FADT-defined GPEs.
 *
 ******************************************************************************/

acpi_status
acpi_ev_get_gpe_device(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
		       struct acpi_gpe_block_info *gpe_block, void *context)
{
	struct acpi_gpe_device_info *info = context;

	/* Increment Index by the number of GPEs in this block */

	info->next_block_base_index += gpe_block->gpe_count;

	if (info->index < info->next_block_base_index) {
		/*
		 * The GPE index is within this block, get the node. Leave the node
		 * NULL for the FADT-defined GPEs
		 */
		if ((gpe_block->node)->type == ACPI_TYPE_DEVICE) {
			info->gpe_device = gpe_block->node;
		}

		info->status = AE_OK;
		return (AE_CTRL_END);
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_get_gpe_xrupt_block
 *
 * PARAMETERS:  interrupt_number            - Interrupt for a GPE block
 *              gpe_xrupt_block             - Where the block is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get or Create a GPE interrupt block. There is one interrupt
 *              block per unique interrupt level used for GPEs. Should be
 *              called only when the GPE lists are semaphore locked and not
 *              subject to change.
 *
 ******************************************************************************/

acpi_status
acpi_ev_get_gpe_xrupt_block(u32 interrupt_number,
			    struct acpi_gpe_xrupt_info **gpe_xrupt_block)
{
	struct acpi_gpe_xrupt_info *next_gpe_xrupt;
	struct acpi_gpe_xrupt_info *gpe_xrupt;
	acpi_status status;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(ev_get_gpe_xrupt_block);

	/* No need for lock since we are not changing any list elements here */

	next_gpe_xrupt = acpi_gbl_gpe_xrupt_list_head;
	while (next_gpe_xrupt) {
		if (next_gpe_xrupt->interrupt_number == interrupt_number) {
			*gpe_xrupt_block = next_gpe_xrupt;
			return_ACPI_STATUS(AE_OK);
		}

		next_gpe_xrupt = next_gpe_xrupt->next;
	}

	/* Not found, must allocate a new xrupt descriptor */

	gpe_xrupt = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_gpe_xrupt_info));
	if (!gpe_xrupt) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	gpe_xrupt->interrupt_number = interrupt_number;

	/* Install new interrupt descriptor with spin lock */

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);
	if (acpi_gbl_gpe_xrupt_list_head) {
		next_gpe_xrupt = acpi_gbl_gpe_xrupt_list_head;
		while (next_gpe_xrupt->next) {
			next_gpe_xrupt = next_gpe_xrupt->next;
		}

		next_gpe_xrupt->next = gpe_xrupt;
		gpe_xrupt->previous = next_gpe_xrupt;
	} else {
		acpi_gbl_gpe_xrupt_list_head = gpe_xrupt;
	}

	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);

	/* Install new interrupt handler if not SCI_INT */

	if (interrupt_number != acpi_gbl_FADT.sci_interrupt) {
		status = acpi_os_install_interrupt_handler(interrupt_number,
							   acpi_ev_gpe_xrupt_handler,
							   gpe_xrupt);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Could not install GPE interrupt handler at level 0x%X",
					interrupt_number));
			return_ACPI_STATUS(status);
		}
	}

	*gpe_xrupt_block = gpe_xrupt;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_delete_gpe_xrupt
 *
 * PARAMETERS:  gpe_xrupt       - A GPE interrupt info block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove and free a gpe_xrupt block. Remove an associated
 *              interrupt handler if not the SCI interrupt.
 *
 ******************************************************************************/

acpi_status acpi_ev_delete_gpe_xrupt(struct acpi_gpe_xrupt_info *gpe_xrupt)
{
	acpi_status status;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(ev_delete_gpe_xrupt);

	/* We never want to remove the SCI interrupt handler */

	if (gpe_xrupt->interrupt_number == acpi_gbl_FADT.sci_interrupt) {
		gpe_xrupt->gpe_block_list_head = NULL;
		return_ACPI_STATUS(AE_OK);
	}

	/* Disable this interrupt */

	status =
	    acpi_os_remove_interrupt_handler(gpe_xrupt->interrupt_number,
					     acpi_ev_gpe_xrupt_handler);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Unlink the interrupt block with lock */

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);
	if (gpe_xrupt->previous) {
		gpe_xrupt->previous->next = gpe_xrupt->next;
	} else {
		/* No previous, update list head */

		acpi_gbl_gpe_xrupt_list_head = gpe_xrupt->next;
	}

	if (gpe_xrupt->next) {
		gpe_xrupt->next->previous = gpe_xrupt->previous;
	}
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);

	/* Free the block */

	ACPI_FREE(gpe_xrupt);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_delete_gpe_handlers
 *
 * PARAMETERS:  gpe_xrupt_info      - GPE Interrupt info
 *              gpe_block           - Gpe Block info
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete all Handler objects found in the GPE data structs.
 *              Used only prior to termination.
 *
 ******************************************************************************/

acpi_status
acpi_ev_delete_gpe_handlers(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
			    struct acpi_gpe_block_info *gpe_block,
			    void *context)
{
	struct acpi_gpe_event_info *gpe_event_info;
	struct acpi_gpe_notify_info *notify;
	struct acpi_gpe_notify_info *next;
	u32 i;
	u32 j;

	ACPI_FUNCTION_TRACE(ev_delete_gpe_handlers);

	/* Examine each GPE Register within the block */

	for (i = 0; i < gpe_block->register_count; i++) {

		/* Now look at the individual GPEs in this byte register */

		for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++) {
			gpe_event_info = &gpe_block->event_info[((acpi_size)i *
								 ACPI_GPE_REGISTER_WIDTH)
								+ j];

			if ((ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) ==
			     ACPI_GPE_DISPATCH_HANDLER) ||
			    (ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) ==
			     ACPI_GPE_DISPATCH_RAW_HANDLER)) {

				/* Delete an installed handler block */

				ACPI_FREE(gpe_event_info->dispatch.handler);
				gpe_event_info->dispatch.handler = NULL;
				gpe_event_info->flags &=
				    ~ACPI_GPE_DISPATCH_MASK;
			} else if (ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags)
				   == ACPI_GPE_DISPATCH_NOTIFY) {

				/* Delete the implicit notification device list */

				notify = gpe_event_info->dispatch.notify_list;
				while (notify) {
					next = notify->next;
					ACPI_FREE(notify);
					notify = next;
				}

				gpe_event_info->dispatch.notify_list = NULL;
				gpe_event_info->flags &=
				    ~ACPI_GPE_DISPATCH_MASK;
			}
		}
	}

	return_ACPI_STATUS(AE_OK);
}

#endif				/* !ACPI_REDUCED_HARDWARE */
