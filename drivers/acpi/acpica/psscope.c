// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: psscope - Parser scope stack management routines
 *
 * Copyright (C) 2000 - 2021, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acparser.h"

#define _COMPONENT          ACPI_PARSER
ACPI_MODULE_NAME("psscope")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_parent_scope
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *
 * RETURN:      Pointer to an Op object
 *
 * DESCRIPTION: Get parent of current op being parsed
 *
 ******************************************************************************/
union acpi_parse_object *acpi_ps_get_parent_scope(struct acpi_parse_state
						  *parser_state)
{

	return (parser_state->scope->parse_scope.op);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_has_completed_scope
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *
 * RETURN:      Boolean, TRUE = scope completed.
 *
 * DESCRIPTION: Is parsing of current argument complete?  Determined by
 *              1) AML pointer is at or beyond the end of the scope
 *              2) The scope argument count has reached zero.
 *
 ******************************************************************************/

u8 acpi_ps_has_completed_scope(struct acpi_parse_state * parser_state)
{

	return ((u8)
		((parser_state->aml >= parser_state->scope->parse_scope.arg_end
		  || !parser_state->scope->parse_scope.arg_count)));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_init_scope
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *              root                - the Root Node of this new scope
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Allocate and init a new scope object
 *
 ******************************************************************************/

acpi_status
acpi_ps_init_scope(struct acpi_parse_state * parser_state,
		   union acpi_parse_object * root_op)
{
	union acpi_generic_state *scope;

	ACPI_FUNCTION_TRACE_PTR(ps_init_scope, root_op);

	scope = acpi_ut_create_generic_state();
	if (!scope) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	scope->common.descriptor_type = ACPI_DESC_TYPE_STATE_RPSCOPE;
	scope->parse_scope.op = root_op;
	scope->parse_scope.arg_count = ACPI_VAR_ARGS;
	scope->parse_scope.arg_end = parser_state->aml_end;
	scope->parse_scope.pkg_end = parser_state->aml_end;

	parser_state->scope = scope;
	parser_state->start_op = root_op;

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_push_scope
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *              op                  - Current op to be pushed
 *              remaining_args      - List of args remaining
 *              arg_count           - Fixed or variable number of args
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push current op to begin parsing its argument
 *
 ******************************************************************************/

acpi_status
acpi_ps_push_scope(struct acpi_parse_state *parser_state,
		   union acpi_parse_object *op,
		   u32 remaining_args, u32 arg_count)
{
	union acpi_generic_state *scope;

	ACPI_FUNCTION_TRACE_PTR(ps_push_scope, op);

	scope = acpi_ut_create_generic_state();
	if (!scope) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	scope->common.descriptor_type = ACPI_DESC_TYPE_STATE_PSCOPE;
	scope->parse_scope.op = op;
	scope->parse_scope.arg_list = remaining_args;
	scope->parse_scope.arg_count = arg_count;
	scope->parse_scope.pkg_end = parser_state->pkg_end;

	/* Push onto scope stack */

	acpi_ut_push_generic_state(&parser_state->scope, scope);

	if (arg_count == ACPI_VAR_ARGS) {

		/* Multiple arguments */

		scope->parse_scope.arg_end = parser_state->pkg_end;
	} else {
		/* Single argument */

		scope->parse_scope.arg_end = ACPI_TO_POINTER(ACPI_MAX_PTR);
	}

	return_ACPI_STATUS(AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_pop_scope
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *              op                  - Where the popped op is returned
 *              arg_list            - Where the popped "next argument" is
 *                                    returned
 *              arg_count           - Count of objects in arg_list
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return to parsing a previous op
 *
 ******************************************************************************/

void
acpi_ps_pop_scope(struct acpi_parse_state *parser_state,
		  union acpi_parse_object **op, u32 * arg_list, u32 * arg_count)
{
	union acpi_generic_state *scope = parser_state->scope;

	ACPI_FUNCTION_TRACE(ps_pop_scope);

	/* Only pop the scope if there is in fact a next scope */

	if (scope->common.next) {
		scope = acpi_ut_pop_generic_state(&parser_state->scope);

		/* Return to parsing previous op */

		*op = scope->parse_scope.op;
		*arg_list = scope->parse_scope.arg_list;
		*arg_count = scope->parse_scope.arg_count;
		parser_state->pkg_end = scope->parse_scope.pkg_end;

		/* All done with this scope state structure */

		acpi_ut_delete_generic_state(scope);
	} else {
		/* Empty parse stack, prepare to fetch next opcode */

		*op = NULL;
		*arg_list = 0;
		*arg_count = 0;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_PARSE,
			  "Popped Op %p Args %X\n", *op, *arg_count));
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_cleanup_scope
 *
 * PARAMETERS:  parser_state        - Current parser state object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Destroy available list, remaining stack levels, and return
 *              root scope
 *
 ******************************************************************************/

void acpi_ps_cleanup_scope(struct acpi_parse_state *parser_state)
{
	union acpi_generic_state *scope;

	ACPI_FUNCTION_TRACE_PTR(ps_cleanup_scope, parser_state);

	if (!parser_state) {
		return_VOID;
	}

	/* Delete anything on the scope stack */

	while (parser_state->scope) {
		scope = acpi_ut_pop_generic_state(&parser_state->scope);
		acpi_ut_delete_generic_state(scope);
	}

	return_VOID;
}
