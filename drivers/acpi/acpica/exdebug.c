/******************************************************************************
 *
 * Module Name: exdebug - Support for stores to the AML Debug Object
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2015, Intel Corp.
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
#include "acnamesp.h"
#include "acinterp.h"
#include "acparser.h"

#define _COMPONENT          ACPI_EXECUTER
ACPI_MODULE_NAME("exdebug")

static union acpi_operand_object *acpi_gbl_trace_method_object = NULL;

/* Local prototypes */

#ifdef ACPI_DEBUG_OUTPUT
static const char *acpi_ex_get_trace_event_name(acpi_trace_event_type type);
#endif

#ifndef ACPI_NO_ERROR_MESSAGES
/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_do_debug_object
 *
 * PARAMETERS:  source_desc         - Object to be output to "Debug Object"
 *              level               - Indentation level (used for packages)
 *              index               - Current package element, zero if not pkg
 *
 * RETURN:      None
 *
 * DESCRIPTION: Handles stores to the AML Debug Object. For example:
 *              Store(INT1, Debug)
 *
 * This function is not compiled if ACPI_NO_ERROR_MESSAGES is set.
 *
 * This function is only enabled if acpi_gbl_enable_aml_debug_object is set, or
 * if ACPI_LV_DEBUG_OBJECT is set in the acpi_dbg_level. Thus, in the normal
 * operational case, stores to the debug object are ignored but can be easily
 * enabled if necessary.
 *
 ******************************************************************************/

void
acpi_ex_do_debug_object(union acpi_operand_object *source_desc,
			u32 level, u32 index)
{
	u32 i;
	u32 timer;
	union acpi_operand_object *object_desc;
	u32 value;

	ACPI_FUNCTION_TRACE_PTR(ex_do_debug_object, source_desc);

	/* Output must be enabled via the debug_object global or the dbg_level */

	if (!acpi_gbl_enable_aml_debug_object &&
	    !(acpi_dbg_level & ACPI_LV_DEBUG_OBJECT)) {
		return_VOID;
	}

	/*
	 * We will emit the current timer value (in microseconds) with each
	 * debug output. Only need the lower 26 bits. This allows for 67
	 * million microseconds or 67 seconds before rollover.
	 */
	timer = ((u32)acpi_os_get_timer() / 10);	/* (100 nanoseconds to microseconds) */
	timer &= 0x03FFFFFF;

	/*
	 * Print line header as long as we are not in the middle of an
	 * object display
	 */
	if (!((level > 0) && index == 0)) {
		acpi_os_printf("[ACPI Debug %.8u] %*s", timer, level, " ");
	}

	/* Display the index for package output only */

	if (index > 0) {
		acpi_os_printf("(%.2u) ", index - 1);
	}

	if (!source_desc) {
		acpi_os_printf("[Null Object]\n");
		return_VOID;
	}

	if (ACPI_GET_DESCRIPTOR_TYPE(source_desc) == ACPI_DESC_TYPE_OPERAND) {
		acpi_os_printf("%s ",
			       acpi_ut_get_object_type_name(source_desc));

		if (!acpi_ut_valid_internal_object(source_desc)) {
			acpi_os_printf("%p, Invalid Internal Object!\n",
				       source_desc);
			return_VOID;
		}
	} else if (ACPI_GET_DESCRIPTOR_TYPE(source_desc) ==
		   ACPI_DESC_TYPE_NAMED) {
		acpi_os_printf("%s: %p\n",
			       acpi_ut_get_type_name(((struct
						       acpi_namespace_node *)
						      source_desc)->type),
			       source_desc);
		return_VOID;
	} else {
		return_VOID;
	}

	/* source_desc is of type ACPI_DESC_TYPE_OPERAND */

	switch (source_desc->common.type) {
	case ACPI_TYPE_INTEGER:

		/* Output correct integer width */

		if (acpi_gbl_integer_byte_width == 4) {
			acpi_os_printf("0x%8.8X\n",
				       (u32)source_desc->integer.value);
		} else {
			acpi_os_printf("0x%8.8X%8.8X\n",
				       ACPI_FORMAT_UINT64(source_desc->integer.
							  value));
		}
		break;

	case ACPI_TYPE_BUFFER:

		acpi_os_printf("[0x%.2X]\n", (u32)source_desc->buffer.length);
		acpi_ut_dump_buffer(source_desc->buffer.pointer,
				    (source_desc->buffer.length < 256) ?
				    source_desc->buffer.length : 256,
				    DB_BYTE_DISPLAY, 0);
		break;

	case ACPI_TYPE_STRING:

		acpi_os_printf("[0x%.2X] \"%s\"\n",
			       source_desc->string.length,
			       source_desc->string.pointer);
		break;

	case ACPI_TYPE_PACKAGE:

		acpi_os_printf("[Contains 0x%.2X Elements]\n",
			       source_desc->package.count);

		/* Output the entire contents of the package */

		for (i = 0; i < source_desc->package.count; i++) {
			acpi_ex_do_debug_object(source_desc->package.
						elements[i], level + 4, i + 1);
		}
		break;

	case ACPI_TYPE_LOCAL_REFERENCE:

		acpi_os_printf("[%s] ",
			       acpi_ut_get_reference_name(source_desc));

		/* Decode the reference */

		switch (source_desc->reference.class) {
		case ACPI_REFCLASS_INDEX:

			acpi_os_printf("0x%X\n", source_desc->reference.value);
			break;

		case ACPI_REFCLASS_TABLE:

			/* Case for ddb_handle */

			acpi_os_printf("Table Index 0x%X\n",
				       source_desc->reference.value);
			return_VOID;

		default:

			break;
		}

		acpi_os_printf(" ");

		/* Check for valid node first, then valid object */

		if (source_desc->reference.node) {
			if (ACPI_GET_DESCRIPTOR_TYPE
			    (source_desc->reference.node) !=
			    ACPI_DESC_TYPE_NAMED) {
				acpi_os_printf
				    (" %p - Not a valid namespace node\n",
				     source_desc->reference.node);
			} else {
				acpi_os_printf("Node %p [%4.4s] ",
					       source_desc->reference.node,
					       (source_desc->reference.node)->
					       name.ascii);

				switch ((source_desc->reference.node)->type) {

					/* These types have no attached object */

				case ACPI_TYPE_DEVICE:
					acpi_os_printf("Device\n");
					break;

				case ACPI_TYPE_THERMAL:
					acpi_os_printf("Thermal Zone\n");
					break;

				default:

					acpi_ex_do_debug_object((source_desc->
								 reference.
								 node)->object,
								level + 4, 0);
					break;
				}
			}
		} else if (source_desc->reference.object) {
			if (ACPI_GET_DESCRIPTOR_TYPE
			    (source_desc->reference.object) ==
			    ACPI_DESC_TYPE_NAMED) {
				acpi_ex_do_debug_object(((struct
							  acpi_namespace_node *)
							 source_desc->reference.
							 object)->object,
							level + 4, 0);
			} else {
				object_desc = source_desc->reference.object;
				value = source_desc->reference.value;

				switch (object_desc->common.type) {
				case ACPI_TYPE_BUFFER:

					acpi_os_printf("Buffer[%u] = 0x%2.2X\n",
						       value,
						       *source_desc->reference.
						       index_pointer);
					break;

				case ACPI_TYPE_STRING:

					acpi_os_printf
					    ("String[%u] = \"%c\" (0x%2.2X)\n",
					     value,
					     *source_desc->reference.
					     index_pointer,
					     *source_desc->reference.
					     index_pointer);
					break;

				case ACPI_TYPE_PACKAGE:

					acpi_os_printf("Package[%u] = ", value);
					acpi_ex_do_debug_object(*source_desc->
								reference.where,
								level + 4, 0);
					break;

				default:

					acpi_os_printf
					    ("Unknown Reference object type %X\n",
					     object_desc->common.type);
					break;
				}
			}
		}
		break;

	default:

		acpi_os_printf("%p\n", source_desc);
		break;
	}

	ACPI_DEBUG_PRINT_RAW((ACPI_DB_EXEC, "\n"));
	return_VOID;
}
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
				  "%s %s [0x%p:%s] execution.\n",
				  acpi_ex_get_trace_event_name(type),
				  begin ? "Begin" : "End", aml, pathname));
	} else {
		ACPI_DEBUG_PRINT((ACPI_DB_TRACE_POINT,
				  "%s %s [0x%p] execution.\n",
				  acpi_ex_get_trace_event_name(type),
				  begin ? "Begin" : "End", aml));
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
	acpi_status status;
	char *pathname = NULL;
	u8 enabled = FALSE;

	ACPI_FUNCTION_NAME(ex_start_trace_method);

	if (method_node) {
		pathname = acpi_ns_get_normalized_pathname(method_node, TRUE);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		goto exit;
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
	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);

exit:
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
	acpi_status status;
	char *pathname = NULL;
	u8 enabled;

	ACPI_FUNCTION_NAME(ex_stop_trace_method);

	if (method_node) {
		pathname = acpi_ns_get_normalized_pathname(method_node, TRUE);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		goto exit_path;
	}

	enabled = acpi_ex_interpreter_trace_enabled(NULL);

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);

	if (enabled) {
		ACPI_TRACE_POINT(ACPI_TRACE_AML_METHOD, FALSE,
				 obj_desc ? obj_desc->method.aml_start : NULL,
				 pathname);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE(status)) {
		goto exit_path;
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

	(void)acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);

exit_path:
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
