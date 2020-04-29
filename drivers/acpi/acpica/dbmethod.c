// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: dbmethod - Debug commands for control methods
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acdispat.h"
#include "acnamesp.h"
#include "acdebug.h"
#include "acparser.h"
#include "acpredef.h"

#define _COMPONENT          ACPI_CA_DEBUGGER
ACPI_MODULE_NAME("dbmethod")

/* Local prototypes */
static acpi_status
acpi_db_walk_for_execute(acpi_handle obj_handle,
			 u32 nesting_level, void *context, void **return_value);

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_set_method_breakpoint
 *
 * PARAMETERS:  location            - AML offset of breakpoint
 *              walk_state          - Current walk info
 *              op                  - Current Op (from parse walk)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set a breakpoint in a control method at the specified
 *              AML offset
 *
 ******************************************************************************/

void
acpi_db_set_method_breakpoint(char *location,
			      struct acpi_walk_state *walk_state,
			      union acpi_parse_object *op)
{
	u32 address;
	u32 aml_offset;

	if (!op) {
		acpi_os_printf("There is no method currently executing\n");
		return;
	}

	/* Get and verify the breakpoint address */

	address = strtoul(location, NULL, 16);
	aml_offset = (u32)ACPI_PTR_DIFF(op->common.aml,
					walk_state->parser_state.aml_start);
	if (address <= aml_offset) {
		acpi_os_printf("Breakpoint %X is beyond current address %X\n",
			       address, aml_offset);
	}

	/* Save breakpoint in current walk */

	walk_state->user_breakpoint = address;
	acpi_os_printf("Breakpoint set at AML offset %X\n", address);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_set_method_call_breakpoint
 *
 * PARAMETERS:  op                  - Current Op (from parse walk)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set a breakpoint in a control method at the specified
 *              AML offset
 *
 ******************************************************************************/

void acpi_db_set_method_call_breakpoint(union acpi_parse_object *op)
{

	if (!op) {
		acpi_os_printf("There is no method currently executing\n");
		return;
	}

	acpi_gbl_step_to_next_call = TRUE;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_set_method_data
 *
 * PARAMETERS:  type_arg        - L for local, A for argument
 *              index_arg       - which one
 *              value_arg       - Value to set.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set a local or argument for the running control method.
 *              NOTE: only object supported is Number.
 *
 ******************************************************************************/

void acpi_db_set_method_data(char *type_arg, char *index_arg, char *value_arg)
{
	char type;
	u32 index;
	u32 value;
	struct acpi_walk_state *walk_state;
	union acpi_operand_object *obj_desc;
	acpi_status status;
	struct acpi_namespace_node *node;

	/* Validate type_arg */

	acpi_ut_strupr(type_arg);
	type = type_arg[0];
	if ((type != 'L') && (type != 'A') && (type != 'N')) {
		acpi_os_printf("Invalid SET operand: %s\n", type_arg);
		return;
	}

	value = strtoul(value_arg, NULL, 16);

	if (type == 'N') {
		node = acpi_db_convert_to_node(index_arg);
		if (!node) {
			return;
		}

		if (node->type != ACPI_TYPE_INTEGER) {
			acpi_os_printf("Can only set Integer nodes\n");
			return;
		}
		obj_desc = node->object;
		obj_desc->integer.value = value;
		return;
	}

	/* Get the index and value */

	index = strtoul(index_arg, NULL, 16);

	walk_state = acpi_ds_get_current_walk_state(acpi_gbl_current_walk_list);
	if (!walk_state) {
		acpi_os_printf("There is no method currently executing\n");
		return;
	}

	/* Create and initialize the new object */

	obj_desc = acpi_ut_create_integer_object((u64)value);
	if (!obj_desc) {
		acpi_os_printf("Could not create an internal object\n");
		return;
	}

	/* Store the new object into the target */

	switch (type) {
	case 'A':

		/* Set a method argument */

		if (index > ACPI_METHOD_MAX_ARG) {
			acpi_os_printf("Arg%u - Invalid argument name\n",
				       index);
			goto cleanup;
		}

		status = acpi_ds_store_object_to_local(ACPI_REFCLASS_ARG,
						       index, obj_desc,
						       walk_state);
		if (ACPI_FAILURE(status)) {
			goto cleanup;
		}

		obj_desc = walk_state->arguments[index].object;

		acpi_os_printf("Arg%u: ", index);
		acpi_db_display_internal_object(obj_desc, walk_state);
		break;

	case 'L':

		/* Set a method local */

		if (index > ACPI_METHOD_MAX_LOCAL) {
			acpi_os_printf
			    ("Local%u - Invalid local variable name\n", index);
			goto cleanup;
		}

		status = acpi_ds_store_object_to_local(ACPI_REFCLASS_LOCAL,
						       index, obj_desc,
						       walk_state);
		if (ACPI_FAILURE(status)) {
			goto cleanup;
		}

		obj_desc = walk_state->local_variables[index].object;

		acpi_os_printf("Local%u: ", index);
		acpi_db_display_internal_object(obj_desc, walk_state);
		break;

	default:

		break;
	}

cleanup:
	acpi_ut_remove_reference(obj_desc);
}

#ifdef ACPI_DISASSEMBLER
/*******************************************************************************
 *
 * FUNCTION:    acpi_db_disassemble_aml
 *
 * PARAMETERS:  statements          - Number of statements to disassemble
 *              op                  - Current Op (from parse walk)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display disassembled AML (ASL) starting from Op for the number
 *              of statements specified.
 *
 ******************************************************************************/

void acpi_db_disassemble_aml(char *statements, union acpi_parse_object *op)
{
	u32 num_statements = 8;

	if (!op) {
		acpi_os_printf("There is no method currently executing\n");
		return;
	}

	if (statements) {
		num_statements = strtoul(statements, NULL, 0);
	}

	acpi_dm_disassemble(NULL, op, num_statements);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_disassemble_method
 *
 * PARAMETERS:  name            - Name of control method
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display disassembled AML (ASL) starting from Op for the number
 *              of statements specified.
 *
 ******************************************************************************/

acpi_status acpi_db_disassemble_method(char *name)
{
	acpi_status status;
	union acpi_parse_object *op;
	struct acpi_walk_state *walk_state;
	union acpi_operand_object *obj_desc;
	struct acpi_namespace_node *method;

	method = acpi_db_convert_to_node(name);
	if (!method) {
		return (AE_BAD_PARAMETER);
	}

	if (method->type != ACPI_TYPE_METHOD) {
		ACPI_ERROR((AE_INFO, "%s (%s): Object must be a control method",
			    name, acpi_ut_get_type_name(method->type)));
		return (AE_BAD_PARAMETER);
	}

	obj_desc = method->object;

	op = acpi_ps_create_scope_op(obj_desc->method.aml_start);
	if (!op) {
		return (AE_NO_MEMORY);
	}

	/* Create and initialize a new walk state */

	walk_state = acpi_ds_create_walk_state(0, op, NULL, NULL);
	if (!walk_state) {
		return (AE_NO_MEMORY);
	}

	status = acpi_ds_init_aml_walk(walk_state, op, NULL,
				       obj_desc->method.aml_start,
				       obj_desc->method.aml_length, NULL,
				       ACPI_IMODE_LOAD_PASS1);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	status = acpi_ut_allocate_owner_id(&obj_desc->method.owner_id);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	walk_state->owner_id = obj_desc->method.owner_id;

	/* Push start scope on scope stack and make it current */

	status = acpi_ds_scope_stack_push(method, method->type, walk_state);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Parse the entire method AML including deferred operators */

	walk_state->parse_flags &= ~ACPI_PARSE_DELETE_TREE;
	walk_state->parse_flags |= ACPI_PARSE_DISASSEMBLE;

	status = acpi_ps_parse_aml(walk_state);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	(void)acpi_dm_parse_deferred_ops(op);

	/* Now we can disassemble the method */

	acpi_gbl_dm_opt_verbose = FALSE;
	acpi_dm_disassemble(NULL, op, 0);
	acpi_gbl_dm_opt_verbose = TRUE;

	acpi_ps_delete_parse_tree(op);

	/* Method cleanup */

	acpi_ns_delete_namespace_subtree(method);
	acpi_ns_delete_namespace_by_owner(obj_desc->method.owner_id);
	acpi_ut_release_owner_id(&obj_desc->method.owner_id);
	return (AE_OK);
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_walk_for_execute
 *
 * PARAMETERS:  Callback from walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Batch execution module. Currently only executes predefined
 *              ACPI names.
 *
 ******************************************************************************/

static acpi_status
acpi_db_walk_for_execute(acpi_handle obj_handle,
			 u32 nesting_level, void *context, void **return_value)
{
	struct acpi_namespace_node *node =
	    (struct acpi_namespace_node *)obj_handle;
	struct acpi_db_execute_walk *info =
	    (struct acpi_db_execute_walk *)context;
	struct acpi_buffer return_obj;
	acpi_status status;
	char *pathname;
	u32 i;
	struct acpi_device_info *obj_info;
	struct acpi_object_list param_objects;
	union acpi_object params[ACPI_METHOD_NUM_ARGS];
	const union acpi_predefined_info *predefined;

	predefined = acpi_ut_match_predefined_method(node->name.ascii);
	if (!predefined) {
		return (AE_OK);
	}

	if (node->type == ACPI_TYPE_LOCAL_SCOPE) {
		return (AE_OK);
	}

	pathname = acpi_ns_get_external_pathname(node);
	if (!pathname) {
		return (AE_OK);
	}

	/* Get the object info for number of method parameters */

	status = acpi_get_object_info(obj_handle, &obj_info);
	if (ACPI_FAILURE(status)) {
		ACPI_FREE(pathname);
		return (status);
	}

	param_objects.pointer = NULL;
	param_objects.count = 0;

	if (obj_info->type == ACPI_TYPE_METHOD) {

		/* Setup default parameters */

		for (i = 0; i < obj_info->param_count; i++) {
			params[i].type = ACPI_TYPE_INTEGER;
			params[i].integer.value = 1;
		}

		param_objects.pointer = params;
		param_objects.count = obj_info->param_count;
	}

	ACPI_FREE(obj_info);
	return_obj.pointer = NULL;
	return_obj.length = ACPI_ALLOCATE_BUFFER;

	/* Do the actual method execution */

	acpi_gbl_method_executing = TRUE;

	status = acpi_evaluate_object(node, NULL, &param_objects, &return_obj);

	acpi_os_printf("%-32s returned %s\n", pathname,
		       acpi_format_exception(status));
	acpi_gbl_method_executing = FALSE;
	ACPI_FREE(pathname);

	/* Ignore status from method execution */

	status = AE_OK;

	/* Update count, check if we have executed enough methods */

	info->count++;
	if (info->count >= info->max_count) {
		status = AE_CTRL_TERMINATE;
	}

	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_evaluate_predefined_names
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Namespace batch execution. Execute predefined names in the
 *              namespace, up to the max count, if specified.
 *
 ******************************************************************************/

void acpi_db_evaluate_predefined_names(void)
{
	struct acpi_db_execute_walk info;

	info.count = 0;
	info.max_count = ACPI_UINT32_MAX;

	/* Search all nodes in namespace */

	(void)acpi_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
				  ACPI_UINT32_MAX, acpi_db_walk_for_execute,
				  NULL, (void *)&info, NULL);

	acpi_os_printf("Evaluated %u predefined names in the namespace\n",
		       info.count);
}
