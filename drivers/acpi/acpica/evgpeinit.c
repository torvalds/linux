// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: evgpeinit - System GPE initialization and update
 *
 * Copyright (C) 2000 - 2019, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acevents.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EVENTS
ACPI_MODULE_NAME("evgpeinit")
#if (!ACPI_REDUCED_HARDWARE)	/* Entire module */
/*
 * Note: History of _PRW support in ACPICA
 *
 * Originally (2000 - 2010), the GPE initialization code performed a walk of
 * the entire namespace to execute the _PRW methods and detect all GPEs
 * capable of waking the system.
 *
 * As of 10/2010, the _PRW method execution has been removed since it is
 * actually unnecessary. The host OS must in fact execute all _PRW methods
 * in order to identify the device/power-resource dependencies. We now put
 * the onus on the host OS to identify the wake GPEs as part of this process
 * and to inform ACPICA of these GPEs via the acpi_setup_gpe_for_wake interface. This
 * not only reduces the complexity of the ACPICA initialization code, but in
 * some cases (on systems with very large namespaces) it should reduce the
 * kernel boot time as well.
 */

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_gpe_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize the GPE data structures and the FADT GPE 0/1 blocks
 *
 ******************************************************************************/
acpi_status acpi_ev_gpe_initialize(void)
{
	u32 register_count0 = 0;
	u32 register_count1 = 0;
	u32 gpe_number_max = 0;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ev_gpe_initialize);

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_INIT,
			      "Initializing General Purpose Events (GPEs):\n"));

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

		register_count0 = (u16)(acpi_gbl_FADT.gpe0_block_length / 2);
		gpe_number_max =
		    (register_count0 * ACPI_GPE_REGISTER_WIDTH) - 1;

		/* Install GPE Block 0 */

		status = acpi_ev_create_gpe_block(acpi_gbl_fadt_gpe_device,
						  acpi_gbl_FADT.xgpe0_block.
						  address,
						  acpi_gbl_FADT.xgpe0_block.
						  space_id, register_count0, 0,
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

		register_count1 = (u16)(acpi_gbl_FADT.gpe1_block_length / 2);

		/* Check for GPE0/GPE1 overlap (if both banks exist) */

		if ((register_count0) &&
		    (gpe_number_max >= acpi_gbl_FADT.gpe1_base)) {
			ACPI_ERROR((AE_INFO,
				    "GPE0 block (GPE 0 to %u) overlaps the GPE1 block "
				    "(GPE %u to %u) - Ignoring GPE1",
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
						     acpi_gbl_FADT.xgpe1_block.
						     address,
						     acpi_gbl_FADT.xgpe1_block.
						     space_id, register_count1,
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

cleanup:
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_update_gpes
 *
 * PARAMETERS:  table_owner_id      - ID of the newly-loaded ACPI table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Check for new GPE methods (_Lxx/_Exx) made available as a
 *              result of a Load() or load_table() operation. If new GPE
 *              methods have been installed, register the new methods.
 *
 ******************************************************************************/

void acpi_ev_update_gpes(acpi_owner_id table_owner_id)
{
	struct acpi_gpe_xrupt_info *gpe_xrupt_info;
	struct acpi_gpe_block_info *gpe_block;
	struct acpi_gpe_walk_info walk_info;
	acpi_status status = AE_OK;

	/*
	 * Find any _Lxx/_Exx GPE methods that have just been loaded.
	 *
	 * Any GPEs that correspond to new _Lxx/_Exx methods are immediately
	 * enabled.
	 *
	 * Examine the namespace underneath each gpe_device within the
	 * gpe_block lists.
	 */
	status = acpi_ut_acquire_mutex(ACPI_MTX_EVENTS);
	if (ACPI_FAILURE(status)) {
		return;
	}

	walk_info.count = 0;
	walk_info.owner_id = table_owner_id;
	walk_info.execute_by_owner_id = TRUE;

	/* Walk the interrupt level descriptor list */

	gpe_xrupt_info = acpi_gbl_gpe_xrupt_list_head;
	while (gpe_xrupt_info) {

		/* Walk all Gpe Blocks attached to this interrupt level */

		gpe_block = gpe_xrupt_info->gpe_block_list_head;
		while (gpe_block) {
			walk_info.gpe_block = gpe_block;
			walk_info.gpe_device = gpe_block->node;

			status = acpi_ns_walk_namespace(ACPI_TYPE_METHOD,
							walk_info.gpe_device,
							ACPI_UINT32_MAX,
							ACPI_NS_WALK_NO_UNLOCK,
							acpi_ev_match_gpe_method,
							NULL, &walk_info, NULL);
			if (ACPI_FAILURE(status)) {
				ACPI_EXCEPTION((AE_INFO, status,
						"While decoding _Lxx/_Exx methods"));
			}

			gpe_block = gpe_block->next;
		}

		gpe_xrupt_info = gpe_xrupt_info->next;
	}

	if (walk_info.count) {
		ACPI_INFO(("Enabled %u new GPEs", walk_info.count));
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_EVENTS);
	return;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_match_gpe_method
 *
 * PARAMETERS:  Callback from walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Called from acpi_walk_namespace. Expects each object to be a
 *              control method under the _GPE portion of the namespace.
 *              Extract the name and GPE type from the object, saving this
 *              information for quick lookup during GPE dispatch. Allows a
 *              per-owner_id evaluation if execute_by_owner_id is TRUE in the
 *              walk_info parameter block.
 *
 *              The name of each GPE control method is of the form:
 *              "_Lxx" or "_Exx", where:
 *                  L      - means that the GPE is level triggered
 *                  E      - means that the GPE is edge triggered
 *                  xx     - is the GPE number [in HEX]
 *
 * If walk_info->execute_by_owner_id is TRUE, we only execute examine GPE methods
 * with that owner.
 *
 ******************************************************************************/

acpi_status
acpi_ev_match_gpe_method(acpi_handle obj_handle,
			 u32 level, void *context, void **return_value)
{
	struct acpi_namespace_node *method_node =
	    ACPI_CAST_PTR(struct acpi_namespace_node, obj_handle);
	struct acpi_gpe_walk_info *walk_info =
	    ACPI_CAST_PTR(struct acpi_gpe_walk_info, context);
	struct acpi_gpe_event_info *gpe_event_info;
	acpi_status status;
	u32 gpe_number;
	u8 temp_gpe_number;
	char name[ACPI_NAME_SIZE + 1];
	u8 type;

	ACPI_FUNCTION_TRACE(ev_match_gpe_method);

	/* Check if requested owner_id matches this owner_id */

	if ((walk_info->execute_by_owner_id) &&
	    (method_node->owner_id != walk_info->owner_id)) {
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Match and decode the _Lxx and _Exx GPE method names
	 *
	 * 1) Extract the method name and null terminate it
	 */
	ACPI_MOVE_32_TO_32(name, &method_node->name.integer);
	name[ACPI_NAME_SIZE] = 0;

	/* 2) Name must begin with an underscore */

	if (name[0] != '_') {
		return_ACPI_STATUS(AE_OK);	/* Ignore this method */
	}

	/*
	 * 3) Edge/Level determination is based on the 2nd character
	 *    of the method name
	 */
	switch (name[1]) {
	case 'L':

		type = ACPI_GPE_LEVEL_TRIGGERED;
		break;

	case 'E':

		type = ACPI_GPE_EDGE_TRIGGERED;
		break;

	default:

		/* Unknown method type, just ignore it */

		ACPI_DEBUG_PRINT((ACPI_DB_LOAD,
				  "Ignoring unknown GPE method type: %s "
				  "(name not of form _Lxx or _Exx)", name));
		return_ACPI_STATUS(AE_OK);
	}

	/* 4) The last two characters of the name are the hex GPE Number */

	status = acpi_ut_ascii_to_hex_byte(&name[2], &temp_gpe_number);
	if (ACPI_FAILURE(status)) {

		/* Conversion failed; invalid method, just ignore it */

		ACPI_DEBUG_PRINT((ACPI_DB_LOAD,
				  "Could not extract GPE number from name: %s "
				  "(name is not of form _Lxx or _Exx)", name));
		return_ACPI_STATUS(AE_OK);
	}

	/* Ensure that we have a valid GPE number for this GPE block */

	gpe_number = (u32)temp_gpe_number;
	gpe_event_info =
	    acpi_ev_low_get_gpe_info(gpe_number, walk_info->gpe_block);
	if (!gpe_event_info) {
		/*
		 * This gpe_number is not valid for this GPE block, just ignore it.
		 * However, it may be valid for a different GPE block, since GPE0
		 * and GPE1 methods both appear under \_GPE.
		 */
		return_ACPI_STATUS(AE_OK);
	}

	if ((ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) ==
	     ACPI_GPE_DISPATCH_HANDLER) ||
	    (ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) ==
	     ACPI_GPE_DISPATCH_RAW_HANDLER)) {

		/* If there is already a handler, ignore this GPE method */

		return_ACPI_STATUS(AE_OK);
	}

	if (ACPI_GPE_DISPATCH_TYPE(gpe_event_info->flags) ==
	    ACPI_GPE_DISPATCH_METHOD) {
		/*
		 * If there is already a method, ignore this method. But check
		 * for a type mismatch (if both the _Lxx AND _Exx exist)
		 */
		if (type != (gpe_event_info->flags & ACPI_GPE_XRUPT_TYPE_MASK)) {
			ACPI_ERROR((AE_INFO,
				    "For GPE 0x%.2X, found both _L%2.2X and _E%2.2X methods",
				    gpe_number, gpe_number, gpe_number));
		}
		return_ACPI_STATUS(AE_OK);
	}

	/* Disable the GPE in case it's been enabled already. */

	(void)acpi_hw_low_set_gpe(gpe_event_info, ACPI_GPE_DISABLE);

	/*
	 * Add the GPE information from above to the gpe_event_info block for
	 * use during dispatch of this GPE.
	 */
	gpe_event_info->flags &= ~(ACPI_GPE_DISPATCH_MASK);
	gpe_event_info->flags |= (u8)(type | ACPI_GPE_DISPATCH_METHOD);
	gpe_event_info->dispatch.method_node = method_node;

	ACPI_DEBUG_PRINT((ACPI_DB_LOAD,
			  "Registered GPE method %s as GPE number 0x%.2X\n",
			  name, gpe_number));
	return_ACPI_STATUS(AE_OK);
}

#endif				/* !ACPI_REDUCED_HARDWARE */
