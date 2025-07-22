// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: extrace - Support for interpreter execution tracing
 *
 * Copyright (C) 2000 - 2025, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("extrace")

static union acpi_operand_object *acpi_gbl_trace_method_object = NULL;

/* Local prototypes */

#ifdef ACPI_DEBUG_OUTPUT
static const char *acpi_ex_get_trace_event_name(acpi_trace_event_type type);
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_interpreter_trace_enabled
 *
 * PARAMETERS:  name                - Whether method name should be matched,
 *                                    this should be checked before starting
 *                                    the tracer
 *
 * RETURN:      TRUE if interpreter trace is enabled.
 *
 * DESCRIPTION: Check whether interpreter trace is enabled
 *
 ******************************************************************************/

static u8 acpi_ex_interpreter_trace_enabled(char *name)
{

	/* Check if tracing is enabled */

	if (!(acpi_gbl_trace_flags & ACPI_TRACE_ENABLED)) {
		return (FALSE);
	}

	/*
	 * Check if tracing is filtered:
	 *
	 * 1. If the tracer is started, acpi_gbl_trace_method_object should have
	 *    been filled by the trace starter
	 * 2. If the tracer is not started, acpi_gbl_trace_method_name should be
	 *    matched if it is specified
	 * 3. If the tracer is oneshot style, acpi_gbl_trace_method_name should
	 *    not be cleared by the trace stopper during the first match
	 */
	if (acpi_gbl_trace_method_object) {
		return (TRUE);
	}

	if (name &&
	    (acpi_gbl_trace_method_name &&
	     strcmp(acpi_gbl_trace_method_name, name))) {
		return (FALSE);
	}

	if ((acpi_gbl_trace_flags & ACPI_TRACE_ONESHOT) &&
	    !acpi_gbl_trace_method_name) {
		return (FALSE);
	}

	return (TRUE);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_get_trace_event_name
 *
 * PARAMETERS:  type            - Trace event type
 *
 * RETURN:      Trace event name.
 *
 * DESCRIPTION: Used to obtain the full trace event name.
 *
 ******************************************************************************/

#ifdef ACPI_DEBUG_OUTPUT

static const char *acpi_ex_get_trace_event_name(acpi_trace_event_type type)
{

	switch (type) {
	case ACPI_TRACE_AML_METHOD:

		return "Method";

	case ACPI_TRACE_AML_OPCODE:

		return "Opcode";

	case ACPI_TRACE_AML_REGION:

		return "Region";

	default:

		return "";
	}
}

#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_trace_point
 *
 * PARAMETERS:  type                - Trace event type
 *              begin               - TRUE if before execution
 *              aml                 - Executed AML address
 *              pathname            - Object path
 *
 * RETURN:      None
 *
 * DESCRIPTION: Internal interpreter execution trace.
 *
 ******************************************************************************/

void
acpi_ex_trace_point(acpi_trace_event_type type,
		    u8 begin, u8 *aml, char *pathname)
{

	ACPI_FUNCTION_NAME(ex_trace_point);

	if (pathname) {
		ACPI_DEBUG_PRINT((ACPI_DB_TRACE_POINT,
				  "%s %s [%s] execution.\n",
				  acpi_ex_get_trace_event_name(type),
				  begin ? "Begin" : "End", pathname));
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_TRACE_POINT,
				  "%s %s [0x%p] execution.\n",
				  acpi_ex_get_trace_event_name(type),
				  begin ? "Begin" : "End", aml));
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_trace_args
 *
 * PARAMETERS:  params            - AML method arguments
 *              count             - numer of method arguments
 *
 * RETURN:      None
 *
 * DESCRIPTION: Trace any arguments
 *
 ******************************************************************************/

void
acpi_ex_trace_args(union acpi_operand_object **params, u32 count)
{
	u32 i;

	ACPI_FUNCTION_NAME(ex_trace_args);

	for (i = 0; i < count; i++) {
		union acpi_operand_object *obj_desc = params[i];

		if (!i) {
			ACPI_DEBUG_PRINT((ACPI_DB_TRACE_POINT, " "));
		}

		switch (obj_desc->common.type) {
		case ACPI_TYPE_INTEGER:
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_TRACE_POINT, "%llx", obj_desc->integer.value));
			break;
		case ACPI_TYPE_STRING:
			if (!obj_desc->string.length) {
				ACPI_DEBUG_PRINT_RAW((ACPI_DB_TRACE_POINT, "NULL"));
				continue;
			}
			if (ACPI_IS_DEBUG_ENABLED(ACPI_LV_TRACE_POINT, _COMPONENT))
				acpi_ut_print_string(obj_desc->string.pointer, ACPI_UINT8_MAX);
			break;
		default:
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_TRACE_POINT, "Unknown"));
			break;
		}
		if (i+1 == count) {
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_TRACE_POINT, "\n"));
		} else {
			ACPI_DEBUG_PRINT_RAW((ACPI_DB_TRACE_POINT, ", "));
		}
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_start_trace_method
 *
 * PARAMETERS:  method_node         - Node of the method
 *              obj_desc            - The method object
 *              walk_state          - current state, NULL if not yet executing
 *                                    a method.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Start control method execution trace
 *
 ******************************************************************************/

void
acpi_ex_start_trace_method(struct acpi_namespace_node *method_node,
			   union acpi_operand_object *obj_desc,
			   struct acpi_walk_state *walk_state)
{
	char *pathname = NULL;
	u8 enabled = FALSE;

	ACPI_FUNCTION_NAME(ex_start_trace_method);

	if (method_node) {
		pathname = acpi_ns_get_normalized_pathname(method_node, TRUE);
	}

	enabled = acpi_ex_interpreter_trace_enabled(pathname);
	if (enabled && !acpi_gbl_trace_method_object) {
		acpi_gbl_trace_method_object = obj_desc;
		acpi_gbl_original_dbg_level = acpi_dbg_level;
		acpi_gbl_original_dbg_layer = acpi_dbg_layer;
		acpi_dbg_level = ACPI_TRACE_LEVEL_ALL;
		acpi_dbg_layer = ACPI_TRACE_LAYER_ALL;

		if (acpi_gbl_trace_dbg_level) {
			acpi_dbg_level = acpi_gbl_trace_dbg_level;
		}

		if (acpi_gbl_trace_dbg_layer) {
			acpi_dbg_layer = acpi_gbl_trace_dbg_layer;
		}
	}

	if (enabled) {
		ACPI_TRACE_POINT(ACPI_TRACE_AML_METHOD, TRUE,
				 obj_desc ? obj_desc->method.aml_start : NULL,
				 pathname);
	}

	if (pathname) {
		ACPI_FREE(pathname);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_stop_trace_method
 *
 * PARAMETERS:  method_node         - Node of the method
 *              obj_desc            - The method object
 *              walk_state          - current state, NULL if not yet executing
 *                                    a method.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Stop control method execution trace
 *
 ******************************************************************************/

void
acpi_ex_stop_trace_method(struct acpi_namespace_node *method_node,
			  union acpi_operand_object *obj_desc,
			  struct acpi_walk_state *walk_state)
{
	char *pathname = NULL;
	u8 enabled;

	ACPI_FUNCTION_NAME(ex_stop_trace_method);

	if (method_node) {
		pathname = acpi_ns_get_normalized_pathname(method_node, TRUE);
	}

	enabled = acpi_ex_interpreter_trace_enabled(NULL);

	if (enabled) {
		ACPI_TRACE_POINT(ACPI_TRACE_AML_METHOD, FALSE,
				 obj_desc ? obj_desc->method.aml_start : NULL,
				 pathname);
	}

	/* Check whether the tracer should be stopped */

	if (acpi_gbl_trace_method_object == obj_desc) {

		/* Disable further tracing if type is one-shot */

		if (acpi_gbl_trace_flags & ACPI_TRACE_ONESHOT) {
			acpi_gbl_trace_method_name = NULL;
		}

		acpi_dbg_level = acpi_gbl_original_dbg_level;
		acpi_dbg_layer = acpi_gbl_original_dbg_layer;
		acpi_gbl_trace_method_object = NULL;
	}

	if (pathname) {
		ACPI_FREE(pathname);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_start_trace_opcode
 *
 * PARAMETERS:  op                  - The parser opcode object
 *              walk_state          - current state, NULL if not yet executing
 *                                    a method.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Start opcode execution trace
 *
 ******************************************************************************/

void
acpi_ex_start_trace_opcode(union acpi_parse_object *op,
			   struct acpi_walk_state *walk_state)
{

	ACPI_FUNCTION_NAME(ex_start_trace_opcode);

	if (acpi_ex_interpreter_trace_enabled(NULL) &&
	    (acpi_gbl_trace_flags & ACPI_TRACE_OPCODE)) {
		ACPI_TRACE_POINT(ACPI_TRACE_AML_OPCODE, TRUE,
				 op->common.aml, op->common.aml_op_name);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_stop_trace_opcode
 *
 * PARAMETERS:  op                  - The parser opcode object
 *              walk_state          - current state, NULL if not yet executing
 *                                    a method.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Stop opcode execution trace
 *
 ******************************************************************************/

void
acpi_ex_stop_trace_opcode(union acpi_parse_object *op,
			  struct acpi_walk_state *walk_state)
{

	ACPI_FUNCTION_NAME(ex_stop_trace_opcode);

	if (acpi_ex_interpreter_trace_enabled(NULL) &&
	    (acpi_gbl_trace_flags & ACPI_TRACE_OPCODE)) {
		ACPI_TRACE_POINT(ACPI_TRACE_AML_OPCODE, FALSE,
				 op->common.aml, op->common.aml_op_name);
	}
}
