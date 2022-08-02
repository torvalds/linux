// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: exconfig - Namespace reconfiguration (Load/Unload opcodes)
 *
 * Copyright (C) 2000 - 2022, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acinterp.h"
#include "acnamesp.h"
#include "actables.h"
#include "acdispat.h"
#include "acevents.h"
#include "amlcode.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exconfig")

/* Local prototypes */
static acpi_status
acpi_ex_add_table(u32 table_index, union acpi_operand_object **ddb_handle);

static acpi_status
acpi_ex_region_read(union acpi_operand_object *obj_desc,
		    u32 length, u8 *buffer);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_add_table
 *
 * PARAMETERS:  table               - Pointer to raw table
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
acpi_ex_add_table(u32 table_index, union acpi_operand_object **ddb_handle)
{
	union acpi_operand_object *obj_desc;

	ACPI_FUNCTION_TRACE(ex_add_table);

	/* Create an object to be the table handle */

	obj_desc = acpi_ut_create_internal_object(ACPI_TYPE_LOCAL_REFERENCE);
	if (!obj_desc) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Init the table handle */

	obj_desc->common.flags |= AOPOBJ_DATA_VALID;
	obj_desc->reference.class = ACPI_REFCLASS_TABLE;
	obj_desc->reference.value = table_index;
	*ddb_handle = obj_desc;
	return_ACPI_STATUS(AE_OK);
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
	union acpi_operand_object *return_obj;
	union acpi_operand_object *ddb_handle;
	u32 table_index;

	ACPI_FUNCTION_TRACE(ex_load_table_op);

	/* Create the return object */

	return_obj = acpi_ut_create_integer_object((u64)0);
	if (!return_obj) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	*return_desc = return_obj;

	/* Find the ACPI table in the RSDT/XSDT */

	acpi_ex_exit_interpreter();
	status = acpi_tb_find_table(operand[0]->string.pointer,
				    operand[1]->string.pointer,
				    operand[2]->string.pointer, &table_index);
	acpi_ex_enter_interpreter();
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND) {
			return_ACPI_STATUS(status);
		}

		/* Table not found, return an Integer=0 and AE_OK */

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
		status = acpi_ns_get_node_unlocked(start_node,
						   operand[3]->string.pointer,
						   ACPI_NS_SEARCH_PARENT,
						   &parent_node);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/* parameter_path (optional parameter) */

	if (operand[4]->string.length > 0) {
		if ((operand[4]->string.pointer[0] != AML_ROOT_PREFIX) &&
		    (operand[4]->string.pointer[0] != AML_PARENT_PREFIX)) {
			/*
			 * Path is not absolute, so it will be relative to the node
			 * referenced by the root_path_string (or the NS root if omitted)
			 */
			start_node = parent_node;
		}

		/* Find the node referenced by the parameter_path_string */

		status = acpi_ns_get_node_unlocked(start_node,
						   operand[4]->string.pointer,
						   ACPI_NS_SEARCH_PARENT,
						   &parameter_node);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	/* Load the table into the namespace */

	ACPI_INFO(("Dynamic OEM Table Load:"));
	acpi_ex_exit_interpreter();
	status = acpi_tb_load_table(table_index, parent_node);
	acpi_ex_enter_interpreter();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	status = acpi_ex_add_table(table_index, &ddb_handle);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Complete the initialization/resolution of new objects */

	acpi_ex_exit_interpreter();
	acpi_ns_initialize_objects();
	acpi_ex_enter_interpreter();

	/* Parameter Data (optional) */

	if (parameter_node) {

		/* Store the parameter data into the optional parameter object */

		status = acpi_ex_store(operand[5],
				       ACPI_CAST_PTR(union acpi_operand_object,
						     parameter_node),
				       walk_state);
		if (ACPI_FAILURE(status)) {
			(void)acpi_ex_unload_table(ddb_handle);

			acpi_ut_remove_reference(ddb_handle);
			return_ACPI_STATUS(status);
		}
	}

	/* Remove the reference to ddb_handle created by acpi_ex_add_table above */

	acpi_ut_remove_reference(ddb_handle);

	/* Return -1 (non-zero) indicates success */

	return_obj->integer.value = 0xFFFFFFFFFFFFFFFF;
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_region_read
 *
 * PARAMETERS:  obj_desc        - Region descriptor
 *              length          - Number of bytes to read
 *              buffer          - Pointer to where to put the data
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read data from an operation region. The read starts from the
 *              beginning of the region.
 *
 ******************************************************************************/

static acpi_status
acpi_ex_region_read(union acpi_operand_object *obj_desc, u32 length, u8 *buffer)
{
	acpi_status status;
	u64 value;
	u32 region_offset = 0;
	u32 i;

	/* Bytewise reads */

	for (i = 0; i < length; i++) {
		status =
		    acpi_ev_address_space_dispatch(obj_desc, NULL, ACPI_READ,
						   region_offset, 8, &value);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		*buffer = (u8)value;
		buffer++;
		region_offset++;
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_load_op
 *
 * PARAMETERS:  obj_desc        - Region or Buffer/Field where the table will be
 *                                obtained
 *              target          - Where the status of the load will be stored
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
	struct acpi_table_header *table_header;
	struct acpi_table_header *table;
	u32 table_index;
	acpi_status status;
	u32 length;

	ACPI_FUNCTION_TRACE(ex_load_op);

	if (target->common.descriptor_type == ACPI_DESC_TYPE_NAMED) {
		target =
		    acpi_ns_get_attached_object(ACPI_CAST_PTR
						(struct acpi_namespace_node,
						 target));
	}
	if (target->common.type != ACPI_TYPE_INTEGER) {
		ACPI_EXCEPTION((AE_INFO, AE_TYPE,
				"Type not integer: %X\n", target->common.type));
		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	target->integer.value = 0;

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
		 * If the Region Address and Length have not been previously
		 * evaluated, evaluate them now and save the results.
		 */
		if (!(obj_desc->common.flags & AOPOBJ_DATA_VALID)) {
			status = acpi_ds_get_region_arguments(obj_desc);
			if (ACPI_FAILURE(status)) {
				return_ACPI_STATUS(status);
			}
		}

		/* Get the table header first so we can get the table length */

		table_header = ACPI_ALLOCATE(sizeof(struct acpi_table_header));
		if (!table_header) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		status =
		    acpi_ex_region_read(obj_desc,
					sizeof(struct acpi_table_header),
					ACPI_CAST_PTR(u8, table_header));
		length = table_header->length;
		ACPI_FREE(table_header);

		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		/* Must have at least an ACPI table header */

		if (length < sizeof(struct acpi_table_header)) {
			return_ACPI_STATUS(AE_INVALID_TABLE_LENGTH);
		}

		/*
		 * The original implementation simply mapped the table, with no copy.
		 * However, the memory region is not guaranteed to remain stable and
		 * we must copy the table to a local buffer. For example, the memory
		 * region is corrupted after suspend on some machines. Dynamically
		 * loaded tables are usually small, so this overhead is minimal.
		 *
		 * The latest implementation (5/2009) does not use a mapping at all.
		 * We use the low-level operation region interface to read the table
		 * instead of the obvious optimization of using a direct mapping.
		 * This maintains a consistent use of operation regions across the
		 * entire subsystem. This is important if additional processing must
		 * be performed in the (possibly user-installed) operation region
		 * handler. For example, acpi_exec and ASLTS depend on this.
		 */

		/* Allocate a buffer for the table */

		table = ACPI_ALLOCATE(length);
		if (!table) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		/* Read the entire table */

		status = acpi_ex_region_read(obj_desc, length,
					     ACPI_CAST_PTR(u8, table));
		if (ACPI_FAILURE(status)) {
			ACPI_FREE(table);
			return_ACPI_STATUS(status);
		}
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

		table_header =
		    ACPI_CAST_PTR(struct acpi_table_header,
				  obj_desc->buffer.pointer);
		length = table_header->length;

		/* Table cannot extend beyond the buffer */

		if (length > obj_desc->buffer.length) {
			return_ACPI_STATUS(AE_AML_BUFFER_LIMIT);
		}
		if (length < sizeof(struct acpi_table_header)) {
			return_ACPI_STATUS(AE_INVALID_TABLE_LENGTH);
		}

		/*
		 * Copy the table from the buffer because the buffer could be
		 * modified or even deleted in the future
		 */
		table = ACPI_ALLOCATE(length);
		if (!table) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		memcpy(table, table_header, length);
		break;

	default:

		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	/* Install the new table into the local data structures */

	ACPI_INFO(("Dynamic OEM Table Load:"));
	acpi_ex_exit_interpreter();
	status = acpi_tb_install_and_load_table(ACPI_PTR_TO_PHYSADDR(table),
						ACPI_TABLE_ORIGIN_INTERNAL_VIRTUAL,
						table, TRUE, &table_index);
	acpi_ex_enter_interpreter();
	if (ACPI_FAILURE(status)) {

		/* Delete allocated table buffer */

		ACPI_FREE(table);
		return_ACPI_STATUS(status);
	}

	/*
	 * Add the table to the namespace.
	 *
	 * Note: Load the table objects relative to the root of the namespace.
	 * This appears to go against the ACPI specification, but we do it for
	 * compatibility with other ACPI implementations.
	 */
	status = acpi_ex_add_table(table_index, &ddb_handle);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Complete the initialization/resolution of new objects */

	acpi_ex_exit_interpreter();
	acpi_ns_initialize_objects();
	acpi_ex_enter_interpreter();

	/* Remove the reference to ddb_handle created by acpi_ex_add_table above */

	acpi_ut_remove_reference(ddb_handle);

	/* Return -1 (non-zero) indicates success */

	target->integer.value = 0xFFFFFFFFFFFFFFFF;
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

	ACPI_FUNCTION_TRACE(ex_unload_table);

	/*
	 * Temporarily emit a warning so that the ASL for the machine can be
	 * hopefully obtained. This is to say that the Unload() operator is
	 * extremely rare if not completely unused.
	 */
	ACPI_WARNING((AE_INFO, "Received request to unload an ACPI table"));

	/*
	 * May 2018: Unload is no longer supported for the following reasons:
	 * 1) A correct implementation on some hosts may not be possible.
	 * 2) Other ACPI implementations do not correctly/fully support it.
	 * 3) It requires host device driver support which does not exist.
	 *    (To properly support namespace unload out from underneath.)
	 * 4) This AML operator has never been seen in the field.
	 */
	ACPI_EXCEPTION((AE_INFO, AE_NOT_IMPLEMENTED,
			"AML Unload operator is not supported"));

	/*
	 * Validate the handle
	 * Although the handle is partially validated in acpi_ex_reconfiguration()
	 * when it calls acpi_ex_resolve_operands(), the handle is more completely
	 * validated here.
	 *
	 * Handle must be a valid operand object of type reference. Also, the
	 * ddb_handle must still be marked valid (table has not been previously
	 * unloaded)
	 */
	if ((!ddb_handle) ||
	    (ACPI_GET_DESCRIPTOR_TYPE(ddb_handle) != ACPI_DESC_TYPE_OPERAND) ||
	    (ddb_handle->common.type != ACPI_TYPE_LOCAL_REFERENCE) ||
	    (!(ddb_handle->common.flags & AOPOBJ_DATA_VALID))) {
		return_ACPI_STATUS(AE_AML_OPERAND_TYPE);
	}

	/* Get the table index from the ddb_handle */

	table_index = table_desc->reference.value;

	/*
	 * Release the interpreter lock so that the table lock won't have
	 * strict order requirement against it.
	 */
	acpi_ex_exit_interpreter();
	status = acpi_tb_unload_table(table_index);
	acpi_ex_enter_interpreter();

	/*
	 * Invalidate the handle. We do this because the handle may be stored
	 * in a named object and may not be actually deleted until much later.
	 */
	if (ACPI_SUCCESS(status)) {
		ddb_handle->common.flags &= ~AOPOBJ_DATA_VALID;
	}
	return_ACPI_STATUS(status);
}
