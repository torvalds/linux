/******************************************************************************
 *
 * Module Name: exconfig - Namespace reconfiguration (Load/Unload opcodes)
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
#include <acpi/acinterp.h>
#include <acpi/amlcode.h>
#include <acpi/acnamesp.h>
#include <acpi/acevents.h>
#include <acpi/actables.h>
#include <acpi/acdispat.h>

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exconfig")

/* Local prototypes */
static acpi_status
acpi_ex_add_table(struct acpi_table_header *table,
		  struct acpi_namespace_node *parent_node,
		  union acpi_operand_object **ddb_handle);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_add_table
 *
 * PARAMETERS:  Table               - Pointer to raw table
 *              parent_node         - Where to load the table (scope)
 *              ddb_handle          - Where to return the table handle.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Common function to Install and Load an ACPI table with a
 *              returned table handle.
 *
 ******************************************************************************/

static acpi_status
acpi_ex_add_table(struct acpi_table_header *table,
		  struct acpi_namespace_node *parent_node,
		  union acpi_operand_object **ddb_handle)
{
	acpi_status status;
	struct acpi_table_desc table_info;
	union acpi_operand_object *obj_desc;

	ACPI_FUNCTION_TRACE("ex_add_table");

	/* Create an object to be the table handle */

	obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_LOCAL_REFERENCE);
	if (!obj_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Init the table handle */

	obj_desc->reference.opcode = AML_LOAD_OP;
	*ddb_handle = obj_desc;

	/* Install the new table into the local data structures */

	ACPI_MEMSET(&table_info, 0, sizeof(struct acpi_table_desc));

	table_info.type = ACPI_TABLE_SSDT;
	table_info.pointer = table;
	table_info.length = (acpi_size) table->length;
	table_info.allocation = ACPI_MEM_ALLOCATED;

	status = acpi_tb_install_table(&table_info);
	obj_desc->reference.object = table_info.installed_desc;

	if (ACPI_FAILURE(status)) {
		if (status == AE_ALREADY_EXISTS) {
			/* Table already exists, just return the handle */

			return_ACPI_STATUS(AE_OK);
		}
		goto cleanup;
	}

	/* Add the table to the namespace */

	status = acpi_ns_load_table(table_info.installed_desc, parent_node);
	if (ACPI_FAILURE(status)) {
		/* Uninstall table on error */

		(void)acpi_tb_uninstall_table(table_info.installed_desc);
		goto cleanup;
	}

	return_ACPI_STATUS(AE_OK);

      cleanup:
	acpi_ut_remove_reference(obj_desc);
	*ddb_handle = NULL;
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_load_table_op
 *
 * PARAMETERS:  walk_state          - Current state with operands
 *              return_desc         - Where to store the return object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load an ACPI table
 *
 ******************************************************************************/

acpi_status
acpi_ex_load_table_op(struct acpi_walk_state *walk_state,
		      union acpi_operand_object **return_desc)
{
	acpi_status status;
	union acpi_operand_object **operand = &walk_state->operands[0];
	struct acpi_table_header *table;
	struct acpi_namespace_node *parent_node;
	struct acpi_namespace_node *start_node;
	struct acpi_namespace_node *parameter_node = NULL;
	union acpi_operand_object *ddb_handle;

	ACPI_FUNCTION_TRACE("ex_load_table_op");

#if 0
	/*
	 * Make sure that the signature does not match one of the tables that
	 * is already loaded.
	 */
	status = acpi_tb_match_signature(operand[0]->string.pointer, NULL);
	if (status == AE_OK) {
		/* Signature matched -- don't allow override */

		return_ACPI_STATUS(AE_ALREADY_EXISTS);
	}
#endif

	/* Find the ACPI table */

	status = acpi_tb_find_table(operand[0]->string.pointer,
				    operand[1]->string.pointer,
				    operand[2]->string.pointer, &table);
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND) {
			return_ACPI_STATUS(status);
		}

		/* Table not found, return an Integer=0 and AE_OK */

		ddb_handle = acpi_ut_create_internal_object(ACPI_TYPE_INTEGER);
		if (!ddb_handle) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		ddb_handle->integer.value = 0;
		*return_desc = ddb_handle;

		return_ACPI_STATUS(AE_OK);
	}

	/* Default nodes */

	start_node = walk_state->scope_info->scope.node;
	parent_node = acpi_gbl_root_node;

	/* root_path (optional parameter) */

	if (operand[3]->string.length > 0) {
		/*
		 * Find the node referenced by the root_path_string. This is the
		 * location within the namespace where the table will be loaded.
		 */
		status =
		    acpi_ns_get_node_by_path(operand[3]->string.pointer,
					     start_node, ACPI_NS_SEARCH_PARENT,
					     &parent_node);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/* parameter_path (optional parameter) */

	if (operand[4]->string.length > 0) {
		if ((operand[4]->string.pointer[0] != '\\') &&
		    (operand[4]->string.pointer[0] != '^')) {
			/*
			 * Path is not absolute, so it will be relative to the node
			 * referenced by the root_path_string (or the NS root if omitted)
			 */
			start_node = parent_node;
		}

		/* Find the node referenced by the parameter_path_string */

		status =
		    acpi_ns_get_node_by_path(operand[4]->string.pointer,
					     start_node, ACPI_NS_SEARCH_PARENT,
					     &parameter_node);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/* Load the table into the namespace */

	status = acpi_ex_add_table(table, parent_node, &ddb_handle);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Parameter Data (optional) */

	if (parameter_node) {
		/* Store the parameter data into the optional parameter object */

		status = acpi_ex_store(operand[5],
				       ACPI_CAST_PTR(union acpi_operand_object,
						     parameter_node),
				       walk_state);
		if (ACPI_FAILURE(status)) {
			(void)acpi_ex_unload_table(ddb_handle);
			return_ACPI_STATUS(status);
		}
	}

	*return_desc = ddb_handle;
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_load_op
 *
 * PARAMETERS:  obj_desc        - Region or Field where the table will be
 *                                obtained
 *              Target          - Where a handle to the table will be stored
 *              walk_state      - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load an ACPI table from a field or operation region
 *
 ******************************************************************************/

acpi_status
acpi_ex_load_op(union acpi_operand_object *obj_desc,
		union acpi_operand_object *target,
		struct acpi_walk_state *walk_state)
{
	acpi_status status;
	union acpi_operand_object *ddb_handle;
	union acpi_operand_object *buffer_desc = NULL;
	struct acpi_table_header *table_ptr = NULL;
	acpi_physical_address address;
	struct acpi_table_header table_header;
	u32 i;

	ACPI_FUNCTION_TRACE("ex_load_op");

	/* Object can be either an op_region or a Field */

	switch (ACPI_GET_OBJECT_TYPE(obj_desc)) {
	case ACPI_TYPE_REGION:

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Load from Region %p %s\n",
				  obj_desc,
				  acpi_ut_get_object_type_name(obj_desc)));

		/*
		 * If the Region Address and Length have not been previously evaluated,
		 * evaluate them now and save the results.
		 */
		if (!(obj_desc->common.flags & AOPOBJ_DATA_VALID)) {
			status = acpi_ds_get_region_arguments(obj_desc);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		}

		/* Get the base physical address of the region */

		address = obj_desc->region.address;

		/* Get the table length from the table header */

		table_header.length = 0;
		for (i = 0; i < 8; i++) {
			status =
			    acpi_ev_address_space_dispatch(obj_desc, ACPI_READ,
							   (acpi_physical_address)
							   (i + address), 8,
							   ((u8 *) &
							    table_header) + i);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		}

		/* Sanity check the table length */

		if (table_header.length < sizeof(struct acpi_table_header)) {
			return_ACPI_STATUS(AE_BAD_HEADER);
		}

		/* Allocate a buffer for the entire table */

		table_ptr = ACPI_MEM_ALLOCATE(table_header.length);
		if (!table_ptr) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		/* Get the entire table from the op region */

		for (i = 0; i < table_header.length; i++) {
			status =
			    acpi_ev_address_space_dispatch(obj_desc, ACPI_READ,
							   (acpi_physical_address)
							   (i + address), 8,
							   ((u8 *) table_ptr +
							    i));
			if (ACPI_FAILURE(status)) {
				goto cleanup;
			}
		}
		break;

	case ACPI_TYPE_LOCAL_REGION_FIELD:
	case ACPI_TYPE_LOCAL_BANK_FIELD:
	case ACPI_TYPE_LOCAL_INDEX_FIELD:

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC, "Load from Field %p %s\n",
				  obj_desc,
				  acpi_ut_get_object_type_name(obj_desc)));

		/*
		 * The length of the field must be at least as large as the table.
		 * Read the entire field and thus the entire table.  Buffer is
		 * allocated during the read.
		 */
		status =
		    acpi_ex_read_data_from_field(walk_state, obj_desc,
						 &buffer_desc);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		table_ptr = ACPI_CAST_PTR(struct acpi_table_header,
					  buffer_desc->buffer.pointer);

		/* All done with the buffer_desc, delete it */

		buffer_desc->buffer.pointer = NULL;
		acpi_ut_remove_reference(buffer_desc);

		/* Sanity check the table length */

		if (table_ptr->length < sizeof(struct acpi_table_header)) {
			status = AE_BAD_HEADER;
			goto cleanup;
		}
		break;

	default:
		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	/* The table must be either an SSDT or a PSDT */

	if ((!ACPI_STRNCMP(table_ptr->signature,
			   acpi_gbl_table_data[ACPI_TABLE_PSDT].signature,
			   acpi_gbl_table_data[ACPI_TABLE_PSDT].sig_length)) &&
	    (!ACPI_STRNCMP(table_ptr->signature,
			   acpi_gbl_table_data[ACPI_TABLE_SSDT].signature,
			   acpi_gbl_table_data[ACPI_TABLE_SSDT].sig_length))) {
		ACPI_ERROR((AE_INFO,
			    "Table has invalid signature [%4.4s], must be SSDT or PSDT",
			    table_ptr->signature));
		status = AE_BAD_SIGNATURE;
		goto cleanup;
	}

	/* Install the new table into the local data structures */

	status = acpi_ex_add_table(table_ptr, acpi_gbl_root_node, &ddb_handle);
	if (ACPI_FAILURE(status)) {
		/* On error, table_ptr was deallocated above */

		return_ACPI_STATUS(status);
	}

	/* Store the ddb_handle into the Target operand */

	status = acpi_ex_store(ddb_handle, target, walk_state);
	if (ACPI_FAILURE(status)) {
		(void)acpi_ex_unload_table(ddb_handle);

		/* table_ptr was deallocated above */

		return_ACPI_STATUS(status);
	}

      cleanup:
	if (ACPI_FAILURE(status)) {
		ACPI_MEM_FREE(table_ptr);
	}
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_unload_table
 *
 * PARAMETERS:  ddb_handle          - Handle to a previously loaded table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Unload an ACPI table
 *
 ******************************************************************************/

acpi_status acpi_ex_unload_table(union acpi_operand_object *ddb_handle)
{
	acpi_status status = AE_OK;
	union acpi_operand_object *table_desc = ddb_handle;
	struct acpi_table_desc *table_info;

	ACPI_FUNCTION_TRACE("ex_unload_table");

	/*
	 * Validate the handle
	 * Although the handle is partially validated in acpi_ex_reconfiguration(),
	 * when it calls acpi_ex_resolve_operands(), the handle is more completely
	 * validated here.
	 */
	if ((!ddb_handle) ||
	    (ACPI_GET_DESCRIPTOR_TYPE(ddb_handle) != ACPI_DESC_TYPE_OPERAND) ||
	    (ACPI_GET_OBJECT_TYPE(ddb_handle) != ACPI_TYPE_LOCAL_REFERENCE)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Get the actual table descriptor from the ddb_handle */

	table_info = (struct acpi_table_desc *)table_desc->reference.object;

	/*
	 * Delete the entire namespace under this table Node
	 * (Offset contains the table_id)
	 */
	acpi_ns_delete_namespace_by_owner(table_info->owner_id);
	acpi_ut_release_owner_id(&table_info->owner_id);

	/* Delete the table itself */

	(void)acpi_tb_uninstall_table(table_info->installed_desc);

	/* Delete the table descriptor (ddb_handle) */

	acpi_ut_remove_reference(table_desc);
	return_ACPI_STATUS(status);
}
