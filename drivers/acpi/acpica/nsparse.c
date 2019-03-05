// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: nsparse - namespace interface to AML parser
 *
 * Copyright (C) 2000 - 2018, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"
#include "acparser.h"
#include "acdispat.h"
#include "actables.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_NAMESPACE
ACPI_MODULE_NAME("nsparse")

/*******************************************************************************
 *
 * FUNCTION:    ns_execute_table
 *
 * PARAMETERS:  table_desc      - An ACPI table descriptor for table to parse
 *              start_node      - Where to enter the table into the namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load ACPI/AML table by executing the entire table as a single
 *              large control method.
 *
 * NOTE: The point of this is to execute any module-level code in-place
 * as the table is parsed. Some AML code depends on this behavior.
 *
 * It is a run-time option at this time, but will eventually become
 * the default.
 *
 * Note: This causes the table to only have a single-pass parse.
 * However, this is compatible with other ACPI implementations.
 *
 ******************************************************************************/
acpi_status
acpi_ns_execute_table(u32 table_index, struct acpi_namespace_node *start_node)
{
	acpi_status status;
	struct acpi_table_header *table;
	acpi_owner_id owner_id;
	struct acpi_evaluate_info *info = NULL;
	u32 aml_length;
	u8 *aml_start;
	union acpi_operand_object *method_obj = NULL;

	ACPI_FUNCTION_TRACE(ns_execute_table);

	status = acpi_get_table_by_index(table_index, &table);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Table must consist of at least a complete header */

	if (table->length < sizeof(struct acpi_table_header)) {
		return_ACPI_STATUS(AE_BAD_HEADER);
	}

	aml_start = (u8 *)table + sizeof(struct acpi_table_header);
	aml_length = table->length - sizeof(struct acpi_table_header);

	status = acpi_tb_get_owner_id(table_index, &owner_id);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Create, initialize, and link a new temporary method object */

	method_obj = acpi_ut_create_internal_object(ACPI_TYPE_METHOD);
	if (!method_obj) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Allocate the evaluation information block */

	info = ACPI_ALLOCATE_ZEROED(sizeof(struct acpi_evaluate_info));
	if (!info) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_PARSE,
			      "%s: Create table pseudo-method for [%4.4s] @%p, method %p\n",
			      ACPI_GET_FUNCTION_NAME, table->signature, table,
			      method_obj));

	method_obj->method.aml_start = aml_start;
	method_obj->method.aml_length = aml_length;
	method_obj->method.owner_id = owner_id;
	method_obj->method.info_flags |= ACPI_METHOD_MODULE_LEVEL;

	info->pass_number = ACPI_IMODE_EXECUTE;
	info->node = start_node;
	info->obj_desc = method_obj;
	info->node_flags = info->node->flags;
	info->full_pathname = acpi_ns_get_normalized_pathname(info->node, TRUE);
	if (!info->full_pathname) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/* Optional object evaluation log */

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_EVALUATION,
			      "%-26s:  (Definition Block level)\n",
			      "Module-level evaluation"));

	status = acpi_ps_execute_table(info);

	/* Optional object evaluation log */

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_EVALUATION,
			      "%-26s:  (Definition Block level)\n",
			      "Module-level complete"));

cleanup:
	if (info) {
		ACPI_FREE(info->full_pathname);
		info->full_pathname = NULL;
	}
	ACPI_FREE(info);
	acpi_ut_remove_reference(method_obj);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    ns_one_complete_parse
 *
 * PARAMETERS:  pass_number             - 1 or 2
 *              table_desc              - The table to be parsed.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform one complete parse of an ACPI/AML table.
 *
 ******************************************************************************/

acpi_status
acpi_ns_one_complete_parse(u32 pass_number,
			   u32 table_index,
			   struct acpi_namespace_node *start_node)
{
	union acpi_parse_object *parse_root;
	acpi_status status;
	u32 aml_length;
	u8 *aml_start;
	struct acpi_walk_state *walk_state;
	struct acpi_table_header *table;
	acpi_owner_id owner_id;

	ACPI_FUNCTION_TRACE(ns_one_complete_parse);

	status = acpi_get_table_by_index(table_index, &table);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Table must consist of at least a complete header */

	if (table->length < sizeof(struct acpi_table_header)) {
		return_ACPI_STATUS(AE_BAD_HEADER);
	}

	aml_start = (u8 *)table + sizeof(struct acpi_table_header);
	aml_length = table->length - sizeof(struct acpi_table_header);

	status = acpi_tb_get_owner_id(table_index, &owner_id);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/* Create and init a Root Node */

	parse_root = acpi_ps_create_scope_op(aml_start);
	if (!parse_root) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/* Create and initialize a new walk state */

	walk_state = acpi_ds_create_walk_state(owner_id, NULL, NULL, NULL);
	if (!walk_state) {
		acpi_ps_free_op(parse_root);
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	status = acpi_ds_init_aml_walk(walk_state, parse_root, NULL,
				       aml_start, aml_length, NULL,
				       (u8)pass_number);
	if (ACPI_FAILURE(status)) {
		acpi_ds_delete_walk_state(walk_state);
		goto cleanup;
	}

	/* Found OSDT table, enable the namespace override feature */

	if (ACPI_COMPARE_NAME(table->signature, ACPI_SIG_OSDT) &&
	    pass_number == ACPI_IMODE_LOAD_PASS1) {
		walk_state->namespace_override = TRUE;
	}

	/* start_node is the default location to load the table */

	if (start_node && start_node != acpi_gbl_root_node) {
		status =
		    acpi_ds_scope_stack_push(start_node, ACPI_TYPE_METHOD,
					     walk_state);
		if (ACPI_FAILURE(status)) {
			acpi_ds_delete_walk_state(walk_state);
			goto cleanup;
		}
	}

	/* Parse the AML */

	ACPI_DEBUG_PRINT((ACPI_DB_PARSE,
			  "*PARSE* pass %u parse\n", pass_number));
	acpi_ex_enter_interpreter();
	status = acpi_ps_parse_aml(walk_state);
	acpi_ex_exit_interpreter();

cleanup:
	acpi_ps_delete_parse_tree(parse_root);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_parse_table
 *
 * PARAMETERS:  table_desc      - An ACPI table descriptor for table to parse
 *              start_node      - Where to enter the table into the namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Parse AML within an ACPI table and return a tree of ops
 *
 ******************************************************************************/

acpi_status
acpi_ns_parse_table(u32 table_index, struct acpi_namespace_node *start_node)
{
	acpi_status status;

	ACPI_FUNCTION_TRACE(ns_parse_table);

	if (acpi_gbl_execute_tables_as_methods) {
		/*
		 * This case executes the AML table as one large control method.
		 * The point of this is to execute any module-level code in-place
		 * as the table is parsed. Some AML code depends on this behavior.
		 *
		 * It is a run-time option at this time, but will eventually become
		 * the default.
		 *
		 * Note: This causes the table to only have a single-pass parse.
		 * However, this is compatible with other ACPI implementations.
		 */
		ACPI_DEBUG_PRINT_RAW((ACPI_DB_PARSE,
				      "%s: **** Start table execution pass\n",
				      ACPI_GET_FUNCTION_NAME));

		status = acpi_ns_execute_table(table_index, start_node);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	} else {
		/*
		 * AML Parse, pass 1
		 *
		 * In this pass, we load most of the namespace. Control methods
		 * are not parsed until later. A parse tree is not created.
		 * Instead, each Parser Op subtree is deleted when it is finished.
		 * This saves a great deal of memory, and allows a small cache of
		 * parse objects to service the entire parse. The second pass of
		 * the parse then performs another complete parse of the AML.
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_PARSE, "**** Start pass 1\n"));

		status = acpi_ns_one_complete_parse(ACPI_IMODE_LOAD_PASS1,
						    table_index, start_node);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}

		/*
		 * AML Parse, pass 2
		 *
		 * In this pass, we resolve forward references and other things
		 * that could not be completed during the first pass.
		 * Another complete parse of the AML is performed, but the
		 * overhead of this is compensated for by the fact that the
		 * parse objects are all cached.
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_PARSE, "**** Start pass 2\n"));
		status = acpi_ns_one_complete_parse(ACPI_IMODE_LOAD_PASS2,
						    table_index, start_node);
		if (ACPI_FAILURE(status)) {
			return_ACPI_STATUS(status);
		}
	}

	return_ACPI_STATUS(status);
}
