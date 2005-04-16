/******************************************************************************
 *
 * Module Name: dsinit - Object initialization namespace walk
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2005, R. Byron Moore
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
#include <acpi/acdispat.h>
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_DISPATCHER
	 ACPI_MODULE_NAME    ("dsinit")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_init_one_object
 *
 * PARAMETERS:  obj_handle      - Node
 *              Level           - Current nesting level
 *              Context         - Points to a init info struct
 *              return_value    - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Callback from acpi_walk_namespace. Invoked for every object
 *              within the namespace.
 *
 *              Currently, the only objects that require initialization are:
 *              1) Methods
 *              2) Operation Regions
 *
 ******************************************************************************/

acpi_status
acpi_ds_init_one_object (
	acpi_handle                     obj_handle,
	u32                             level,
	void                            *context,
	void                            **return_value)
{
	acpi_object_type                type;
	acpi_status                     status;
	struct acpi_init_walk_info      *info = (struct acpi_init_walk_info *) context;


	ACPI_FUNCTION_NAME ("ds_init_one_object");


	/*
	 * We are only interested in objects owned by the table that
	 * was just loaded
	 */
	if (((struct acpi_namespace_node *) obj_handle)->owner_id !=
			info->table_desc->table_id) {
		return (AE_OK);
	}

	info->object_count++;

	/* And even then, we are only interested in a few object types */

	type = acpi_ns_get_type (obj_handle);

	switch (type) {
	case ACPI_TYPE_REGION:

		status = acpi_ds_initialize_region (obj_handle);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Region %p [%4.4s] - Init failure, %s\n",
				obj_handle, acpi_ut_get_node_name (obj_handle),
				acpi_format_exception (status)));
		}

		info->op_region_count++;
		break;


	case ACPI_TYPE_METHOD:

		info->method_count++;

		/* Print a dot for each method unless we are going to print the entire pathname */

		if (!(acpi_dbg_level & ACPI_LV_INIT_NAMES)) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT, "."));
		}

		/*
		 * Set the execution data width (32 or 64) based upon the
		 * revision number of the parent ACPI table.
		 * TBD: This is really for possible future support of integer width
		 * on a per-table basis. Currently, we just use a global for the width.
		 */
		if (info->table_desc->pointer->revision == 1) {
			((struct acpi_namespace_node *) obj_handle)->flags |= ANOBJ_DATA_WIDTH_32;
		}

		/*
		 * Always parse methods to detect errors, we will delete
		 * the parse tree below
		 */
		status = acpi_ds_parse_method (obj_handle);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Method %p [%4.4s] - parse failure, %s\n",
				obj_handle, acpi_ut_get_node_name (obj_handle),
				acpi_format_exception (status)));

			/* This parse failed, but we will continue parsing more methods */

			break;
		}

		/*
		 * Delete the parse tree.  We simply re-parse the method
		 * for every execution since there isn't much overhead
		 */
		acpi_ns_delete_namespace_subtree (obj_handle);
		acpi_ns_delete_namespace_by_owner (((struct acpi_namespace_node *) obj_handle)->object->method.owning_id);
		break;


	case ACPI_TYPE_DEVICE:

		info->device_count++;
		break;


	default:
		break;
	}

	/*
	 * We ignore errors from above, and always return OK, since
	 * we don't want to abort the walk on a single error.
	 */
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ds_initialize_objects
 *
 * PARAMETERS:  table_desc      - Descriptor for parent ACPI table
 *              start_node      - Root of subtree to be initialized.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk the namespace starting at "start_node" and perform any
 *              necessary initialization on the objects found therein
 *
 ******************************************************************************/

acpi_status
acpi_ds_initialize_objects (
	struct acpi_table_desc          *table_desc,
	struct acpi_namespace_node      *start_node)
{
	acpi_status                     status;
	struct acpi_init_walk_info      info;


	ACPI_FUNCTION_TRACE ("ds_initialize_objects");


	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"**** Starting initialization of namespace objects ****\n"));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT, "Parsing all Control Methods:"));

	info.method_count   = 0;
	info.op_region_count = 0;
	info.object_count   = 0;
	info.device_count   = 0;
	info.table_desc     = table_desc;

	/* Walk entire namespace from the supplied root */

	status = acpi_walk_namespace (ACPI_TYPE_ANY, start_node, ACPI_UINT32_MAX,
			  acpi_ds_init_one_object, &info, NULL);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "walk_namespace failed, %s\n",
			acpi_format_exception (status)));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT,
		"\nTable [%4.4s](id %4.4X) - %hd Objects with %hd Devices %hd Methods %hd Regions\n",
		table_desc->pointer->signature, table_desc->table_id, info.object_count,
		info.device_count, info.method_count, info.op_region_count));

	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"%hd Methods, %hd Regions\n", info.method_count, info.op_region_count));

	return_ACPI_STATUS (AE_OK);
}


