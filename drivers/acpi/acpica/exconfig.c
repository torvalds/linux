/******************************************************************************
 *
 * Module Name: exconfig - Namespace reconfiguration (Load/Unload opcodes)
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2008, Intel Corp.
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
#include "acinterp.h"
#include "acnamesp.h"
#include "actables.h"
#include "acdispat.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exconfig")

/* Local prototypes */
static acpi_status
acpi_ex_add_table(u32 table_index,
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
acpi_ex_add_table(u32 table_index,
		  struct acpi_namespace_node *parent_node,
		  union acpi_operand_object **ddb_handle)
{
	acpi_status status;
	union acpi_operand_object *obj_desc;

	ACPI_FUNCTION_TRACE(ex_add_table);

	/* Create an object to be the table handle */

	obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_LOCAL_REFERENCE);
	if (!obj_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Init the table handle */

	obj_desc->reference.class = ACPI_REFCLASS_TABLE;
	*ddb_handle = obj_desc;

	/* Install the new table into the local data structures */

	obj_desc->reference.value = table_index;

	/* Add the table to the namespace */

	status = acpi_ns_load_table(table_index, parent_node);
	if (ACPI_FAILURE(status)) {
		acpi_ut_remove_reference(obj_desc);
		*ddb_handle = NULL;
	}

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
 * DESCRIPTION: Load an ACPI table from the RSDT/XSDT
 *
 ******************************************************************************/

acpi_status
acpi_ex_load_table_op(struct acpi_walk_state *walk_state,
		      union acpi_operand_object **return_desc)
{
	acpi_status status;
	union acpi_operand_object **operand = &walk_state->operands[0];
	struct acpi_namespace_node *parent_node;
	struct acpi_namespace_node *start_node;
	struct acpi_namespace_node *parameter_node = NULL;
	union acpi_operand_object *ddb_handle;
	struct acpi_table_header *table;
	u32 table_index;

	ACPI_FUNCTION_TRACE(ex_load_table_op);

	/* Validate lengths for the signature_string, OEMIDString, OEMtable_iD */

	if ((operand[0]->string.length > ACPI_NAME_SIZE) ||
	    (operand[1]->string.length > ACPI_OEM_ID_SIZE) ||
	    (operand[2]->string.length > ACPI_OEM_TABLE_ID_SIZE)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Find the ACPI table in the RSDT/XSDT */

	status = acpi_tb_find_table(operand[0]->string.pointer,
				    operand[1]->string.pointer,
				    operand[2]->string.pointer, &table_index);
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
		    acpi_ns_get_node(start_node, operand[3]->string.pointer,
				     ACPI_NS_SEARCH_PARENT, &parent_node);
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
		    acpi_ns_get_node(start_node, operand[4]->string.pointer,
				     ACPI_NS_SEARCH_PARENT, &parameter_node);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/* Load the table into the namespace */

	status = acpi_ex_add_table(table_index, parent_node, &ddb_handle);
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

	status = acpi_get_table_by_index(table_index, &table);
	if (ACPI_SUCCESS(status)) {
		ACPI_INFO((AE_INFO,
			   "Dynamic OEM Table Load - [%.4s] OemId [%.6s] OemTableId [%.8s]",
			   table->signature, table->oem_id,
			   table->oem_table_id));
	}

	/* Invoke table handler if present */

	if (acpi_gbl_table_handler) {
		(void)acpi_gbl_table_handler(ACPI_TABLE_EVENT_LOAD, table,
					     acpi_gbl_table_handler_context);
	}

	*return_desc = ddb_handle;
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_load_op
 *
 * PARAMETERS:  obj_desc        - Region or Buffer/Field where the table will be
 *                                obtained
 *              Target          - Where a handle to the table will be stored
 *              walk_state      - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load an ACPI table from a field or operation region
 *
 * NOTE: Region Fields (Field, bank_field, index_fields) are resolved to buffer
 *       objects before this code is reached.
 *
 *       If source is an operation region, it must refer to system_memory, as
 *       per the ACPI specification.
 *
 ******************************************************************************/

acpi_status
acpi_ex_load_op(union acpi_operand_object *obj_desc,
		union acpi_operand_object *target,
		struct acpi_walk_state *walk_state)
{
	union acpi_operand_object *ddb_handle;
	struct acpi_table_header *table;
	struct acpi_table_desc table_desc;
	u32 table_index;
	acpi_status status;
	u32 length;

	ACPI_FUNCTION_TRACE(ex_load_op);

	ACPI_MEMSET(&table_desc, 0, sizeof(struct acpi_table_desc));

	/* Source Object can be either an op_region or a Buffer/Field */

	switch (obj_desc->common.type) {
	case ACPI_TYPE_REGION:

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "Load table from Region %p\n", obj_desc));

		/* Region must be system_memory (from ACPI spec) */

		if (obj_desc->region.space_id != ACPI_ADR_SPACE_SYSTEM_MEMORY) {
			return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
		}

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

		/*
		 * Map the table header and get the actual table length. The region
		 * length is not guaranteed to be the same as the table length.
		 */
		table = acpi_os_map_memory(obj_desc->region.address,
					   sizeof(struct acpi_table_header));
		if (!table) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		length = table->length;
		acpi_os_unmap_memory(table, sizeof(struct acpi_table_header));

		/* Must have at least an ACPI table header */

		if (length < sizeof(struct acpi_table_header)) {
			return_ACPI_STATUS(AE_INVALID_TABLE_LENGTH);
		}

		/*
		 * The memory region is not guaranteed to remain stable and we must
		 * copy the table to a local buffer. For example, the memory region
		 * is corrupted after suspend on some machines. Dynamically loaded
		 * tables are usually small, so this overhead is minimal.
		 */

		/* Allocate a buffer for the table */

		table_desc.pointer = ACPI_ALLOCATE(length);
		if (!table_desc.pointer) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		/* Map the entire table and copy it */

		table = acpi_os_map_memory(obj_desc->region.address, length);
		if (!table) {
			ACPI_FREE(table_desc.pointer);
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		ACPI_MEMCPY(table_desc.pointer, table, length);
		acpi_os_unmap_memory(table, length);

		table_desc.address = obj_desc->region.address;
		break;

	case ACPI_TYPE_BUFFER:	/* Buffer or resolved region_field */

		ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
				  "Load table from Buffer or Field %p\n",
				  obj_desc));

		/* Must have at least an ACPI table header */

		if (obj_desc->buffer.length < sizeof(struct acpi_table_header)) {
			return_ACPI_STATUS(AE_INVALID_TABLE_LENGTH);
		}

		/* Get the actual table length from the table header */

		table =
		    ACPI_CAST_PTR(struct acpi_table_header,
				  obj_desc->buffer.pointer);
		length = table->length;

		/* Table cannot extend beyond the buffer */

		if (length > obj_desc->buffer.length) {
			return_ACPI_STATUS(AE_AML_BUFFER_LIMIT);
		}
		if (length < sizeof(struct acpi_table_header)) {
			return_ACPI_STATUS(AE_INVALID_TABLE_LENGTH);
		}

		/*
		 * Copy the table from the buffer because the buffer could be modified
		 * or even deleted in the future
		 */
		table_desc.pointer = ACPI_ALLOCATE(length);
		if (!table_desc.pointer) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		ACPI_MEMCPY(table_desc.pointer, table, length);
		table_desc.address = ACPI_TO_INTEGER(table_desc.pointer);
		break;

	default:
		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	/* Validate table checksum (will not get validated in tb_add_table) */

	status = acpi_tb_verify_checksum(table_desc.pointer, length);
	if (ACPI_FAILURE(status)) {
		ACPI_FREE(table_desc.pointer);
		return_ACPI_STATUS(status);
	}

	/* Complete the table descriptor */

	table_desc.length = length;
	table_desc.flags = ACPI_TABLE_ORIGIN_ALLOCATED;

	/* Install the new table into the local data structures */

	status = acpi_tb_add_table(&table_desc, &table_index);
	if (ACPI_FAILURE(status)) {
		goto cleanup;
	}

	/*
	 * Add the table to the namespace.
	 *
	 * Note: Load the table objects relative to the root of the namespace.
	 * This appears to go against the ACPI specification, but we do it for
	 * compatibility with other ACPI implementations.
	 */
	status =
	    acpi_ex_add_table(table_index, acpi_gbl_root_node, &ddb_handle);
	if (ACPI_FAILURE(status)) {

		/* On error, table_ptr was deallocated above */

		return_ACPI_STATUS(status);
	}

	/* Store the ddb_handle into the Target operand */

	status = acpi_ex_store(ddb_handle, target, walk_state);
	if (ACPI_FAILURE(status)) {
		(void)acpi_ex_unload_table(ddb_handle);

		/* table_ptr was deallocated above */

		acpi_ut_remove_reference(ddb_handle);
		return_ACPI_STATUS(status);
	}

	/* Invoke table handler if present */

	if (acpi_gbl_table_handler) {
		(void)acpi_gbl_table_handler(ACPI_TABLE_EVENT_LOAD,
					     table_desc.pointer,
					     acpi_gbl_table_handler_context);
	}

      cleanup:
	if (ACPI_FAILURE(status)) {

		/* Delete allocated table buffer */

		acpi_tb_delete_table(&table_desc);
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
	u32 table_index;
	struct acpi_table_header *table;

	ACPI_FUNCTION_TRACE(ex_unload_table);

	/*
	 * Validate the handle
	 * Although the handle is partially validated in acpi_ex_reconfiguration(),
	 * when it calls acpi_ex_resolve_operands(), the handle is more completely
	 * validated here.
	 */
	if ((!ddb_handle) ||
	    (ACPI_GET_DESCRIPTOR_TYPE(ddb_handle) != ACPI_DESC_TYPE_OPERAND) ||
	    (ddb_handle->common.type != ACPI_TYPE_LOCAL_REFERENCE)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Get the table index from the ddb_handle */

	table_index = table_desc->reference.value;

	/* Invoke table handler if present */

	if (acpi_gbl_table_handler) {
		status = acpi_get_table_by_index(table_index, &table);
		if (ACPI_SUCCESS(status)) {
			(void)acpi_gbl_table_handler(ACPI_TABLE_EVENT_UNLOAD,
						     table,
						     acpi_gbl_table_handler_context);
		}
	}

	/* Delete the portion of the namespace owned by this table */

	status = acpi_tb_delete_namespace_by_owner(table_index);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	(void)acpi_tb_release_owner_id(table_index);
	acpi_tb_set_table_loaded_flag(table_index, FALSE);

	/* Table unloaded, remove a reference to the ddb_handle object */

	acpi_ut_remove_reference(ddb_handle);
	return_ACPI_STATUS(AE_OK);
}
