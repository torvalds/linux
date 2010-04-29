/******************************************************************************
 *
 * Module Name: evgpeblk - GPE block creation and initialization.
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
ACPI_MODULE_NAME("evgpeblk")

/* Local prototypes */
static acpi_status
acpi_ev_save_method_info(acpi_handle obj_handle,
			 u32 level, void *obj_desc, void **return_value);

static acpi_status
acpi_ev_match_prw_and_gpe(acpi_handle obj_handle,
			  u32 level, void *info, void **return_value);

static struct acpi_gpe_xrupt_info *acpi_ev_get_gpe_xrupt_block(u32
							       interrupt_number);

static acpi_status
acpi_ev_delete_gpe_xrupt(struct acpi_gpe_xrupt_info *gpe_xrupt);

static acpi_status
acpi_ev_install_gpe_block(struct acpi_gpe_block_info *gpe_block,
			  u32 interrupt_number);

static acpi_status
acpi_ev_create_gpe_info_blocks(struct acpi_gpe_block_info *gpe_block);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_valid_gpe_event
 *
 * PARAMETERS:  gpe_event_info              - Info for this GPE
 *
 * RETURN:      TRUE if the gpe_event is valid
 *
 * DESCRIPTION: Validate a GPE event. DO NOT CALL FROM INTERRUPT LEVEL.
 *              Should be called only when the GPE lists are semaphore locked
 *              and not subject to change.
 *
 ******************************************************************************/

u8 acpi_ev_valid_gpe_event(struct acpi_gpe_event_info *gpe_event_info)
{
	struct acpi_gpe_xrupt_info *gpe_xrupt_block;
	struct acpi_gpe_block_info *gpe_block;

	ACPI_FUNCTION_ENTRY();

	/* No need for spin lock since we are not changing any list elements */

	/* Walk the GPE interrupt levels */

	gpe_xrupt_block = acpi_gbl_gpe_xrupt_list_head;
	while (gpe_xrupt_block) {
		gpe_block = gpe_xrupt_block->gpe_block_list_head;

		/* Walk the GPE blocks on this interrupt level */

		while (gpe_block) {
			if ((&gpe_block->event_info[0] <= gpe_event_info) &&
			    (&gpe_block->event_info[((acpi_size)
						     gpe_block->
						     register_count) * 8] >
			     gpe_event_info)) {
				return (TRUE);
			}

			gpe_block = gpe_block->next;
		}

		gpe_xrupt_block = gpe_xrupt_block->next;
	}

	return (FALSE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_walk_gpe_list
 *
 * PARAMETERS:  gpe_walk_callback   - Routine called for each GPE block
 *              Context             - Value passed to callback
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
	u32 i;
	u32 j;

	ACPI_FUNCTION_TRACE(ev_delete_gpe_handlers);

	/* Examine each GPE Register within the block */

	for (i = 0; i < gpe_block->register_count; i++) {

		/* Now look at the individual GPEs in this byte register */

		for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++) {
			gpe_event_info = &gpe_block->event_info[((acpi_size) i *
								 ACPI_GPE_REGISTER_WIDTH)
								+ j];

			if ((gpe_event_info->flags & ACPI_GPE_DISPATCH_MASK) ==
			    ACPI_GPE_DISPATCH_HANDLER) {
				ACPI_FREE(gpe_event_info->dispatch.handler);
				gpe_event_info->dispatch.handler = NULL;
				gpe_event_info->flags &=
				    ~ACPI_GPE_DISPATCH_MASK;
			}
		}
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_save_method_info
 *
 * PARAMETERS:  Callback from walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Called from acpi_walk_namespace. Expects each object to be a
 *              control method under the _GPE portion of the namespace.
 *              Extract the name and GPE type from the object, saving this
 *              information for quick lookup during GPE dispatch
 *
 *              The name of each GPE control method is of the form:
 *              "_Lxx" or "_Exx"
 *              Where:
 *                  L      - means that the GPE is level triggered
 *                  E      - means that the GPE is edge triggered
 *                  xx     - is the GPE number [in HEX]
 *
 ******************************************************************************/

static acpi_status
acpi_ev_save_method_info(acpi_handle obj_handle,
			 u32 level, void *obj_desc, void **return_value)
{
	struct acpi_gpe_block_info *gpe_block = (void *)obj_desc;
	struct acpi_gpe_event_info *gpe_event_info;
	u32 gpe_number;
	char name[ACPI_NAME_SIZE + 1];
	u8 type;

	ACPI_FUNCTION_TRACE(ev_save_method_info);

	/*
	 * _Lxx and _Exx GPE method support
	 *
	 * 1) Extract the name from the object and convert to a string
	 */
	ACPI_MOVE_32_TO_32(name,
			   &((struct acpi_namespace_node *)obj_handle)->name.
			   integer);
	name[ACPI_NAME_SIZE] = 0;

	/*
	 * 2) Edge/Level determination is based on the 2nd character
	 *    of the method name
	 *
	 * NOTE: Default GPE type is RUNTIME. May be changed later to WAKE
	 * if a _PRW object is found that points to this GPE.
	 */
	switch (name[1]) {
	case 'L':
		type = ACPI_GPE_LEVEL_TRIGGERED;
		break;

	case 'E':
		type = ACPI_GPE_EDGE_TRIGGERED;
		break;

	default:
		/* Unknown method type, just ignore it! */

		ACPI_DEBUG_PRINT((ACPI_DB_LOAD,
				  "Ignoring unknown GPE method type: %s "
				  "(name not of form _Lxx or _Exx)", name));
		return_ACPI_STATUS(AE_OK);
	}

	/* Convert the last two characters of the name to the GPE Number */

	gpe_number = ACPI_STRTOUL(&name[2], NULL, 16);
	if (gpe_number == ACPI_UINT32_MAX) {

		/* Conversion failed; invalid method, just ignore it */

		ACPI_DEBUG_PRINT((ACPI_DB_LOAD,
				  "Could not extract GPE number from name: %s "
				  "(name is not of form _Lxx or _Exx)", name));
		return_ACPI_STATUS(AE_OK);
	}

	/* Ensure that we have a valid GPE number for this GPE block */

	if ((gpe_number < gpe_block->block_base_number) ||
	    (gpe_number >= (gpe_block->block_base_number +
			    (gpe_block->register_count * 8)))) {
		/*
		 * Not valid for this GPE block, just ignore it. However, it may be
		 * valid for a different GPE block, since GPE0 and GPE1 methods both
		 * appear under \_GPE.
		 */
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Now we can add this information to the gpe_event_info block for use
	 * during dispatch of this GPE.
	 */
	gpe_event_info =
	    &gpe_block->event_info[gpe_number - gpe_block->block_base_number];

	gpe_event_info->flags = (u8) (type | ACPI_GPE_DISPATCH_METHOD);

	gpe_event_info->dispatch.method_node =
	    (struct acpi_namespace_node *)obj_handle;

	ACPI_DEBUG_PRINT((ACPI_DB_LOAD,
			  "Registered GPE method %s as GPE number 0x%.2X\n",
			  name, gpe_number));
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_match_prw_and_gpe
 *
 * PARAMETERS:  Callback from walk_namespace
 *
 * RETURN:      Status. NOTE: We ignore errors so that the _PRW walk is
 *              not aborted on a single _PRW failure.
 *
 * DESCRIPTION: Called from acpi_walk_namespace. Expects each object to be a
 *              Device. Run the _PRW method. If present, extract the GPE
 *              number and mark the GPE as a WAKE GPE.
 *
 ******************************************************************************/

static acpi_status
acpi_ev_match_prw_and_gpe(acpi_handle obj_handle,
			  u32 level, void *info, void **return_value)
{
	struct acpi_gpe_walk_info *gpe_info = (void *)info;
	struct acpi_namespace_node *gpe_device;
	struct acpi_gpe_block_info *gpe_block;
	struct acpi_namespace_node *target_gpe_device;
	struct acpi_gpe_event_info *gpe_event_info;
	union acpi_operand_object *pkg_desc;
	union acpi_operand_object *obj_desc;
	u32 gpe_number;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_match_prw_and_gpe);

	/* Check for a _PRW method under this device */

	status = acpi_ut_evaluate_object(obj_handle, METHOD_NAME__PRW,
					 ACPI_BTYPE_PACKAGE, &pkg_desc);
	if (ACPI_FAILURE(status)) {

		/* Ignore all errors from _PRW, we don't want to abort the subsystem */

		return_ACPI_STATUS(AE_OK);
	}

	/* The returned _PRW package must have at least two elements */

	if (pkg_desc->package.count < 2) {
		goto cleanup;
	}

	/* Extract pointers from the input context */

	gpe_device = gpe_info->gpe_device;
	gpe_block = gpe_info->gpe_block;

	/*
	 * The _PRW object must return a package, we are only interested in the
	 * first element
	 */
	obj_desc = pkg_desc->package.elements[0];

	if (obj_desc->common.type == ACPI_TYPE_INTEGER) {

		/* Use FADT-defined GPE device (from definition of _PRW) */

		target_gpe_device = acpi_gbl_fadt_gpe_device;

		/* Integer is the GPE number in the FADT described GPE blocks */

		gpe_number = (u32) obj_desc->integer.value;
	} else if (obj_desc->common.type == ACPI_TYPE_PACKAGE) {

		/* Package contains a GPE reference and GPE number within a GPE block */

		if ((obj_desc->package.count < 2) ||
		    ((obj_desc->package.elements[0])->common.type !=
		     ACPI_TYPE_LOCAL_REFERENCE) ||
		    ((obj_desc->package.elements[1])->common.type !=
		     ACPI_TYPE_INTEGER)) {
			goto cleanup;
		}

		/* Get GPE block reference and decode */

		target_gpe_device =
		    obj_desc->package.elements[0]->reference.node;
		gpe_number = (u32) obj_desc->package.elements[1]->integer.value;
	} else {
		/* Unknown type, just ignore it */

		goto cleanup;
	}

	/*
	 * Is this GPE within this block?
	 *
	 * TRUE if and only if these conditions are true:
	 *     1) The GPE devices match.
	 *     2) The GPE index(number) is within the range of the Gpe Block
	 *          associated with the GPE device.
	 */
	if ((gpe_device == target_gpe_device) &&
	    (gpe_number >= gpe_block->block_base_number) &&
	    (gpe_number < gpe_block->block_base_number +
	     (gpe_block->register_count * 8))) {
		gpe_event_info = &gpe_block->event_info[gpe_number -
							gpe_block->
							block_base_number];

		gpe_event_info->flags |= ACPI_GPE_CAN_WAKE;
	}

      cleanup:
	acpi_ut_remove_reference(pkg_desc);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_get_gpe_xrupt_block
 *
 * PARAMETERS:  interrupt_number     - Interrupt for a GPE block
 *
 * RETURN:      A GPE interrupt block
 *
 * DESCRIPTION: Get or Create a GPE interrupt block. There is one interrupt
 *              block per unique interrupt level used for GPEs. Should be
 *              called only when the GPE lists are semaphore locked and not
 *              subject to change.
 *
 ******************************************************************************/

static struct acpi_gpe_xrupt_info *acpi_ev_get_gpe_xrupt_block(u32
							       interrupt_number)
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
			return_PTR(next_gpe_xrupt);
		}

		next_gpe_xrupt = next_gpe_xrupt->next;
	}

	/* Not found, must allocate a new xrupt descriptor */

	gpe_xrupt = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_gpe_xrupt_info));
	if (!gpe_xrupt) {
		return_PTR(NULL);
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
			ACPI_ERROR((AE_INFO,
				    "Could not install GPE interrupt handler at level 0x%X",
				    interrupt_number));
			return_PTR(NULL);
		}
	}

	return_PTR(gpe_xrupt);
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

static acpi_status
acpi_ev_delete_gpe_xrupt(struct acpi_gpe_xrupt_info *gpe_xrupt)
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
 * FUNCTION:    acpi_ev_install_gpe_block
 *
 * PARAMETERS:  gpe_block               - New GPE block
 *              interrupt_number        - Xrupt to be associated with this
 *                                        GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install new GPE block with mutex support
 *
 ******************************************************************************/

static acpi_status
acpi_ev_install_gpe_block(struct acpi_gpe_block_info *gpe_block,
			  u32 interrupt_number)
{
	struct acpi_gpe_block_info *next_gpe_block;
	struct acpi_gpe_xrupt_info *gpe_xrupt_block;
	acpi_status status;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(ev_install_gpe_block);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	gpe_xrupt_block = acpi_ev_get_gpe_xrupt_block(interrupt_number);
	if (!gpe_xrupt_block) {
		status = AE_NO_MEMORY;
		goto unlock_and_exit;
	}

	/* Install the new block at the end of the list with lock */

	flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);
	if (gpe_xrupt_block->gpe_block_list_head) {
		next_gpe_block = gpe_xrupt_block->gpe_block_list_head;
		while (next_gpe_block->next) {
			next_gpe_block = next_gpe_block->next;
		}

		next_gpe_block->next = gpe_block;
		gpe_block->previous = next_gpe_block;
	} else {
		gpe_xrupt_block->gpe_block_list_head = gpe_block;
	}

	gpe_block->xrupt_block = gpe_xrupt_block;
	acpi_os_release_lock(acpi_gbl_gpe_lock, flags);

      unlock_and_exit:
	status = acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_delete_gpe_block
 *
 * PARAMETERS:  gpe_block           - Existing GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a GPE block
 *
 ******************************************************************************/

acpi_status acpi_ev_delete_gpe_block(struct acpi_gpe_block_info *gpe_block)
{
	acpi_status status;
	acpi_cpu_flags flags;

	ACPI_FUNCTION_TRACE(ev_install_gpe_block);

	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Disable all GPEs in this block */

	status =
	    acpi_hw_disable_gpe_block(gpe_block->xrupt_block, gpe_block, NULL);

	if (!gpe_block->previous && !gpe_block->next) {

		/* This is the last gpe_block on this interrupt */

		status = acpi_ev_delete_gpe_xrupt(gpe_block->xrupt_block);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}
	} else {
		/* Remove the block on this interrupt with lock */

		flags = acpi_os_acquire_lock(acpi_gbl_gpe_lock);
		if (gpe_block->previous) {
			gpe_block->previous->next = gpe_block->next;
		} else {
			gpe_block->xrupt_block->gpe_block_list_head =
			    gpe_block->next;
		}

		if (gpe_block->next) {
			gpe_block->next->previous = gpe_block->previous;
		}
		acpi_os_release_lock(acpi_gbl_gpe_lock, flags);
	}

	acpi_current_gpe_count -=
	    gpe_block->register_count * ACPI_GPE_REGISTER_WIDTH;

	/* Free the gpe_block */

	ACPI_FREE(gpe_block->register_info);
	ACPI_FREE(gpe_block->event_info);
	ACPI_FREE(gpe_block);

      unlock_and_exit:
	status = acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_create_gpe_info_blocks
 *
 * PARAMETERS:  gpe_block   - New GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create the register_info and event_info blocks for this GPE block
 *
 ******************************************************************************/

static acpi_status
acpi_ev_create_gpe_info_blocks(struct acpi_gpe_block_info *gpe_block)
{
	struct acpi_gpe_register_info *gpe_register_info = NULL;
	struct acpi_gpe_event_info *gpe_event_info = NULL;
	struct acpi_gpe_event_info *this_event;
	struct acpi_gpe_register_info *this_register;
	u32 i;
	u32 j;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_create_gpe_info_blocks);

	/* Allocate the GPE register information block */

	gpe_register_info = ACPI_ALLOCATE_ZEROED((acpi_size) gpe_block->
						 register_count *
						 sizeof(struct
							acpi_gpe_register_info));
	if (!gpe_register_info) {
		ACPI_ERROR((AE_INFO,
			    "Could not allocate the GpeRegisterInfo table"));
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/*
	 * Allocate the GPE event_info block. There are eight distinct GPEs
	 * per register. Initialization to zeros is sufficient.
	 */
	gpe_event_info = ACPI_ALLOCATE_ZEROED(((acpi_size) gpe_block->
					       register_count *
					       ACPI_GPE_REGISTER_WIDTH) *
					      sizeof(struct
						     acpi_gpe_event_info));
	if (!gpe_event_info) {
		ACPI_ERROR((AE_INFO,
			    "Could not allocate the GpeEventInfo table"));
		status = AE_NO_MEMORY;
		goto error_exit;
	}

	/* Save the new Info arrays in the GPE block */

	gpe_block->register_info = gpe_register_info;
	gpe_block->event_info = gpe_event_info;

	/*
	 * Initialize the GPE Register and Event structures. A goal of these
	 * tables is to hide the fact that there are two separate GPE register
	 * sets in a given GPE hardware block, the status registers occupy the
	 * first half, and the enable registers occupy the second half.
	 */
	this_register = gpe_register_info;
	this_event = gpe_event_info;

	for (i = 0; i < gpe_block->register_count; i++) {

		/* Init the register_info for this GPE register (8 GPEs) */

		this_register->base_gpe_number =
		    (u8) (gpe_block->block_base_number +
			  (i * ACPI_GPE_REGISTER_WIDTH));

		this_register->status_address.address =
		    gpe_block->block_address.address + i;

		this_register->enable_address.address =
		    gpe_block->block_address.address + i +
		    gpe_block->register_count;

		this_register->status_address.space_id =
		    gpe_block->block_address.space_id;
		this_register->enable_address.space_id =
		    gpe_block->block_address.space_id;
		this_register->status_address.bit_width =
		    ACPI_GPE_REGISTER_WIDTH;
		this_register->enable_address.bit_width =
		    ACPI_GPE_REGISTER_WIDTH;
		this_register->status_address.bit_offset = 0;
		this_register->enable_address.bit_offset = 0;

		/* Init the event_info for each GPE within this register */

		for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++) {
			this_event->gpe_number =
			    (u8) (this_register->base_gpe_number + j);
			this_event->register_info = this_register;
			this_event++;
		}

		/* Disable all GPEs within this register */

		status = acpi_hw_write(0x00, &this_register->enable_address);
		if (ACPI_FAILURE(status)) {
			goto error_exit;
		}

		/* Clear any pending GPE events within this register */

		status = acpi_hw_write(0xFF, &this_register->status_address);
		if (ACPI_FAILURE(status)) {
			goto error_exit;
		}

		this_register++;
	}

	return_ACPI_STATUS(AE_OK);

      error_exit:
	if (gpe_register_info) {
		ACPI_FREE(gpe_register_info);
	}
	if (gpe_event_info) {
		ACPI_FREE(gpe_event_info);
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_create_gpe_block
 *
 * PARAMETERS:  gpe_device          - Handle to the parent GPE block
 *              gpe_block_address   - Address and space_iD
 *              register_count      - Number of GPE register pairs in the block
 *              gpe_block_base_number - Starting GPE number for the block
 *              interrupt_number    - H/W interrupt for the block
 *              return_gpe_block    - Where the new block descriptor is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create and Install a block of GPE registers. All GPEs within
 *              the block are disabled at exit.
 *              Note: Assumes namespace is locked.
 *
 ******************************************************************************/

acpi_status
acpi_ev_create_gpe_block(struct acpi_namespace_node *gpe_device,
			 struct acpi_generic_address *gpe_block_address,
			 u32 register_count,
			 u8 gpe_block_base_number,
			 u32 interrupt_number,
			 struct acpi_gpe_block_info **return_gpe_block)
{
	acpi_status status;
	struct acpi_gpe_block_info *gpe_block;

	ACPI_FUNCTION_TRACE(ev_create_gpe_block);

	if (!register_count) {
		return_ACPI_STATUS(AE_OK);
	}

	/* Allocate a new GPE block */

	gpe_block = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_gpe_block_info));
	if (!gpe_block) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Initialize the new GPE block */

	gpe_block->node = gpe_device;
	gpe_block->register_count = register_count;
	gpe_block->block_base_number = gpe_block_base_number;

	ACPI_MEMCPY(&gpe_block->block_address, gpe_block_address,
		    sizeof(struct acpi_generic_address));

	/*
	 * Create the register_info and event_info sub-structures
	 * Note: disables and clears all GPEs in the block
	 */
	status = acpi_ev_create_gpe_info_blocks(gpe_block);
	if (ACPI_FAILURE(status)) {
		ACPI_FREE(gpe_block);
		return_ACPI_STATUS(status);
	}

	/* Install the new block in the global lists */

	status = acpi_ev_install_gpe_block(gpe_block, interrupt_number);
	if (ACPI_FAILURE(status)) {
		ACPI_FREE(gpe_block);
		return_ACPI_STATUS(status);
	}

	/* Find all GPE methods (_Lxx, _Exx) for this block */

	status = acpi_ns_walk_namespace(ACPI_TYPE_METHOD, gpe_device,
					ACPI_UINT32_MAX, ACPI_NS_WALK_NO_UNLOCK,
					acpi_ev_save_method_info, NULL,
					gpe_block, NULL);

	/* Return the new block */

	if (return_gpe_block) {
		(*return_gpe_block) = gpe_block;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INIT,
			  "GPE %02X to %02X [%4.4s] %u regs on int 0x%X\n",
			  (u32) gpe_block->block_base_number,
			  (u32) (gpe_block->block_base_number +
				 ((gpe_block->register_count *
				   ACPI_GPE_REGISTER_WIDTH) - 1)),
			  gpe_device->name.ascii, gpe_block->register_count,
			  interrupt_number));

	/* Update global count of currently available GPEs */

	acpi_current_gpe_count += register_count * ACPI_GPE_REGISTER_WIDTH;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_initialize_gpe_block
 *
 * PARAMETERS:  gpe_device          - Handle to the parent GPE block
 *              gpe_block           - Gpe Block info
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize and enable a GPE block. First find and run any
 *              _PRT methods associated with the block, then enable the
 *              appropriate GPEs.
 *              Note: Assumes namespace is locked.
 *
 ******************************************************************************/

acpi_status
acpi_ev_initialize_gpe_block(struct acpi_namespace_node *gpe_device,
			     struct acpi_gpe_block_info *gpe_block)
{
	struct acpi_gpe_event_info *gpe_event_info;
	struct acpi_gpe_walk_info gpe_info;
	u32 wake_gpe_count;
	u32 gpe_enabled_count;
	u32 i;
	u32 j;

	ACPI_FUNCTION_TRACE(ev_initialize_gpe_block);

	/* Ignore a null GPE block (e.g., if no GPE block 1 exists) */

	if (!gpe_block) {
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Runtime option: Should wake GPEs be enabled at runtime?  The default
	 * is no, they should only be enabled just as the machine goes to sleep.
	 */
	if (acpi_gbl_leave_wake_gpes_disabled) {
		/*
		 * Differentiate runtime vs wake GPEs, via the _PRW control methods.
		 * Each GPE that has one or more _PRWs that reference it is by
		 * definition a wake GPE and will not be enabled while the machine
		 * is running.
		 */
		gpe_info.gpe_block = gpe_block;
		gpe_info.gpe_device = gpe_device;

		acpi_ns_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
					   ACPI_UINT32_MAX, ACPI_NS_WALK_UNLOCK,
					   acpi_ev_match_prw_and_gpe, NULL,
					   &gpe_info, NULL);
	}

	/*
	 * Enable all GPEs that have a corresponding method and aren't
	 * capable of generating wakeups. Any other GPEs within this block
	 * must be enabled via the acpi_enable_gpe() interface.
	 */
	wake_gpe_count = 0;
	gpe_enabled_count = 0;
	if (gpe_device == acpi_gbl_fadt_gpe_device)
		gpe_device = NULL;

	for (i = 0; i < gpe_block->register_count; i++) {
		for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++) {
			acpi_status status;
			acpi_size gpe_index;
			int gpe_number;

			/* Get the info block for this particular GPE */
			gpe_index = (acpi_size)i * ACPI_GPE_REGISTER_WIDTH + j;
			gpe_event_info = &gpe_block->event_info[gpe_index];

			if (gpe_event_info->flags & ACPI_GPE_CAN_WAKE) {
				wake_gpe_count++;
				if (acpi_gbl_leave_wake_gpes_disabled)
					continue;
			}

			if (!(gpe_event_info->flags & ACPI_GPE_DISPATCH_METHOD))
				continue;

			gpe_number = gpe_index + gpe_block->block_base_number;
			status = acpi_enable_gpe(gpe_device, gpe_number,
						ACPI_GPE_TYPE_RUNTIME);
			if (ACPI_FAILURE(status))
				ACPI_ERROR((AE_INFO,
						"Failed to enable GPE %02X\n",
						gpe_number));
			else
				gpe_enabled_count++;
		}
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INIT,
			  "Found %u Wake, Enabled %u Runtime GPEs in this block\n",
			  wake_gpe_count, gpe_enabled_count));

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_gpe_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize the GPE data structures
 *
 ******************************************************************************/

acpi_status acpi_ev_gpe_initialize(void)
{
	u32 register_count0 = 0;
	u32 register_count1 = 0;
	u32 gpe_number_max = 0;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_gpe_initialize);

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Initialize the GPE Block(s) defined in the FADT
	 *
	 * Why the GPE register block lengths are divided by 2:  From the ACPI
	 * Spec, section "General-Purpose Event Registers", we have:
	 *
	 * "Each register block contains two registers of equal length
	 *  GPEx_STS and GPEx_EN (where x is 0 or 1). The length of the
	 *  GPE0_STS and GPE0_EN registers is equal to half the GPE0_LEN
	 *  The length of the GPE1_STS and GPE1_EN registers is equal to
	 *  half the GPE1_LEN. If a generic register block is not supported
	 *  then its respective block pointer and block length values in the
	 *  FADT table contain zeros. The GPE0_LEN and GPE1_LEN do not need
	 *  to be the same size."
	 */

	/*
	 * Determine the maximum GPE number for this machine.
	 *
	 * Note: both GPE0 and GPE1 are optional, and either can exist without
	 * the other.
	 *
	 * If EITHER the register length OR the block address are zero, then that
	 * particular block is not supported.
	 */
	if (acpi_gbl_FADT.gpe0_block_length &&
	    acpi_gbl_FADT.xgpe0_block.address) {

		/* GPE block 0 exists (has both length and address > 0) */

		register_count0 = (u16) (acpi_gbl_FADT.gpe0_block_length / 2);

		gpe_number_max =
		    (register_count0 * ACPI_GPE_REGISTER_WIDTH) - 1;

		/* Install GPE Block 0 */

		status = acpi_ev_create_gpe_block(acpi_gbl_fadt_gpe_device,
						  &acpi_gbl_FADT.xgpe0_block,
						  register_count0, 0,
						  acpi_gbl_FADT.sci_interrupt,
						  &acpi_gbl_gpe_fadt_blocks[0]);

		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status,
					"Could not create GPE Block 0"));
		}
	}

	if (acpi_gbl_FADT.gpe1_block_length &&
	    acpi_gbl_FADT.xgpe1_block.address) {

		/* GPE block 1 exists (has both length and address > 0) */

		register_count1 = (u16) (acpi_gbl_FADT.gpe1_block_length / 2);

		/* Check for GPE0/GPE1 overlap (if both banks exist) */

		if ((register_count0) &&
		    (gpe_number_max >= acpi_gbl_FADT.gpe1_base)) {
			ACPI_ERROR((AE_INFO,
				    "GPE0 block (GPE 0 to %d) overlaps the GPE1 block "
				    "(GPE %d to %d) - Ignoring GPE1",
				    gpe_number_max, acpi_gbl_FADT.gpe1_base,
				    acpi_gbl_FADT.gpe1_base +
				    ((register_count1 *
				      ACPI_GPE_REGISTER_WIDTH) - 1)));

			/* Ignore GPE1 block by setting the register count to zero */

			register_count1 = 0;
		} else {
			/* Install GPE Block 1 */

			status =
			    acpi_ev_create_gpe_block(acpi_gbl_fadt_gpe_device,
						     &acpi_gbl_FADT.xgpe1_block,
						     register_count1,
						     acpi_gbl_FADT.gpe1_base,
						     acpi_gbl_FADT.
						     sci_interrupt,
						     &acpi_gbl_gpe_fadt_blocks
						     [1]);

			if (ACPI_FAILURE(status)) {
				ACPI_EXCEPTION((AE_INFO, status,
						"Could not create GPE Block 1"));
			}

			/*
			 * GPE0 and GPE1 do not have to be contiguous in the GPE number
			 * space. However, GPE0 always starts at GPE number zero.
			 */
			gpe_number_max = acpi_gbl_FADT.gpe1_base +
			    ((register_count1 * ACPI_GPE_REGISTER_WIDTH) - 1);
		}
	}

	/* Exit if there are no GPE registers */

	if ((register_count0 + register_count1) == 0) {

		/* GPEs are not required by ACPI, this is OK */

		ACPI_DEBUG_PRINT((ACPI_DB_INIT,
				  "There are no GPE blocks defined in the FADT\n"));
		status = AE_OK;
		goto cleanup;
	}

	/* Check for Max GPE number out-of-range */

	if (gpe_number_max > ACPI_GPE_MAX) {
		ACPI_ERROR((AE_INFO,
			    "Maximum GPE number from FADT is too large: 0x%X",
			    gpe_number_max));
		status = AE_BAD_VALUE;
		goto cleanup;
	}

      cleanup:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(AE_OK);
}
