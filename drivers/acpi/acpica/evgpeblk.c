// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: evgpeblk - GPE block creation and initialization.
 *
 * Copyright (C) 2000 - 2018, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acevents.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evgpeblk")
#if (!ACPI_REDUCED_HARDWARE)	/* Entire module */
/* Local prototypes */
static acpi_status
acpi_ev_install_gpe_block(struct acpi_gpe_block_info *gpe_block,
			  u32 interrupt_number);

static acpi_status
acpi_ev_create_gpe_info_blocks(struct acpi_gpe_block_info *gpe_block);

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

	status =
	    acpi_ev_get_gpe_xrupt_block(interrupt_number, &gpe_xrupt_block);
	if (ACPI_FAILURE(status)) {
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
	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
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

	acpi_current_gpe_count -= gpe_block->gpe_count;

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

	gpe_register_info = ACPI_ALLOCATE_ZEROED((acpi_size)gpe_block->
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
	gpe_event_info = ACPI_ALLOCATE_ZEROED((acpi_size)gpe_block->gpe_count *
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

		this_register->base_gpe_number = (u16)
		    (gpe_block->block_base_number +
		     (i * ACPI_GPE_REGISTER_WIDTH));

		this_register->status_address.address = gpe_block->address + i;

		this_register->enable_address.address =
		    gpe_block->address + i + gpe_block->register_count;

		this_register->status_address.space_id = gpe_block->space_id;
		this_register->enable_address.space_id = gpe_block->space_id;
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
 *              gpe_block_address   - Address and space_ID
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
			 u64 address,
			 u8 space_id,
			 u32 register_count,
			 u16 gpe_block_base_number,
			 u32 interrupt_number,
			 struct acpi_gpe_block_info **return_gpe_block)
{
	acpi_status status;
	struct acpi_gpe_block_info *gpe_block;
	struct acpi_gpe_walk_info walk_info;

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

	gpe_block->address = address;
	gpe_block->space_id = space_id;
	gpe_block->node = gpe_device;
	gpe_block->gpe_count = (u16)(register_count * ACPI_GPE_REGISTER_WIDTH);
	gpe_block->initialized = FALSE;
	gpe_block->register_count = register_count;
	gpe_block->block_base_number = gpe_block_base_number;

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
		ACPI_FREE(gpe_block->register_info);
		ACPI_FREE(gpe_block->event_info);
		ACPI_FREE(gpe_block);
		return_ACPI_STATUS(status);
	}

	acpi_gbl_all_gpes_initialized = FALSE;

	/* Find all GPE methods (_Lxx or_Exx) for this block */

	walk_info.gpe_block = gpe_block;
	walk_info.gpe_device = gpe_device;
	walk_info.execute_by_owner_id = FALSE;

	status = acpi_ns_walk_namespace(ACPI_TYPE_METHOD, gpe_device,
					ACPI_UINT32_MAX, ACPI_NS_WALK_NO_UNLOCK,
					acpi_ev_match_gpe_method, NULL,
					&walk_info, NULL);

	/* Return the new block */

	if (return_gpe_block) {
		(*return_gpe_block) = gpe_block;
	}

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_INIT,
			      "    Initialized GPE %02X to %02X [%4.4s] %u regs on interrupt 0x%X%s\n",
			      (u32)gpe_block->block_base_number,
			      (u32)(gpe_block->block_base_number +
				    (gpe_block->gpe_count - 1)),
			      gpe_device->name.ascii, gpe_block->register_count,
			      interrupt_number,
			      interrupt_number ==
			      acpi_gbl_FADT.sci_interrupt ? " (SCI)" : ""));

	/* Update global count of currently available GPEs */

	acpi_current_gpe_count += gpe_block->gpe_count;
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_initialize_gpe_block
 *
 * PARAMETERS:  acpi_gpe_callback
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize and enable a GPE block. Enable GPEs that have
 *              associated methods.
 *              Note: Assumes namespace is locked.
 *
 ******************************************************************************/

acpi_status
acpi_ev_initialize_gpe_block(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
			     struct acpi_gpe_block_info *gpe_block,
			     void *context)
{
	acpi_status status;
	struct acpi_gpe_event_info *gpe_event_info;
	u32 gpe_enabled_count;
	u32 gpe_index;
	u32 i;
	u32 j;
	u8 *is_polling_needed = context;
	ACPI_ERROR_ONLY(u32 gpe_number);

	ACPI_FUNCTION_TRACE(ev_initialize_gpe_block);

	/*
	 * Ignore a null GPE block (e.g., if no GPE block 1 exists), and
	 * any GPE blocks that have been initialized already.
	 */
	if (!gpe_block || gpe_block->initialized) {
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Enable all GPEs that have a corresponding method and have the
	 * ACPI_GPE_CAN_WAKE flag unset. Any other GPEs within this block
	 * must be enabled via the acpi_enable_gpe() interface.
	 */
	gpe_enabled_count = 0;

	for (i = 0; i < gpe_block->register_count; i++) {
		for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++) {

			/* Get the info block for this particular GPE */

			gpe_index = (i * ACPI_GPE_REGISTER_WIDTH) + j;
			gpe_event_info = &gpe_block->event_info[gpe_index];
			ACPI_ERROR_ONLY(gpe_number =
					gpe_block->block_base_number +
					gpe_index);
			gpe_event_info->flags |= ACPI_GPE_INITIALIZED;

			/*
			 * Ignore GPEs that have no corresponding _Lxx/_Exx method
			 * and GPEs that are used for wakeup
			 */
			if ((ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) !=
			     ACPI_GPE_DISPATCH_METHOD)
			    || (gpe_event_info->flags & ACPI_GPE_CAN_WAKE)) {
				continue;
			}

			status = acpi_ev_add_gpe_reference(gpe_event_info);
			if (ACPI_FAILURE(status)) {
				ACPI_EXCEPTION((AE_INFO, status,
					"Could not enable GPE 0x%02X",
					gpe_number));
				continue;
			}

			gpe_event_info->flags |= ACPI_GPE_AUTO_ENABLED;

			if (is_polling_needed &&
			    ACPI_GPE_IS_POLLING_NEEDED(gpe_event_info)) {
				*is_polling_needed = TRUE;
			}

			gpe_enabled_count++;
		}
	}

	if (gpe_enabled_count) {
		ACPI_INFO(("Enabled %u GPEs in block %02X to %02X",
			   gpe_enabled_count, (u32)gpe_block->block_base_number,
			   (u32)(gpe_block->block_base_number +
				 (gpe_block->gpe_count - 1))));
	}

	gpe_block->initialized = TRUE;

	return_ACPI_STATUS(AE_OK);
}

#endif				/* !ACPI_REDUCED_HARDWARE */
