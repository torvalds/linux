/******************************************************************************
 *
 * Module Name: pstree - Parser op tree manipulation/traversal/search
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2012, Intel Corp.
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
#include "acparser.h"
#include "amlcode.h"

#define _COMPONENT          ACPI_PARSER
ACPI_MODULE_NAME("pstree")

/* Local prototypes */
#ifdef ACPI_OBSOLETE_FUNCTIONS
union acpi_parse_object *acpi_ps_get_child(union acpi_parse_object *op);
#endif

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_arg
 *
 * PARAMETERS:  Op              - Get an argument for this op
 *              Argn            - Nth argument to get
 *
 * RETURN:      The argument (as an Op object). NULL if argument does not exist
 *
 * DESCRIPTION: Get the specified op's argument.
 *
 ******************************************************************************/

union acpi_parse_object *acpi_ps_get_arg(union acpi_parse_object *op, u32 argn)
{
	union acpi_parse_object *arg = NULL;
	const struct acpi_opcode_info *op_info;

	ACPI_FUNCTION_ENTRY();

/*
	if (Op->Common.aml_opcode == AML_INT_CONNECTION_OP)
	{
		return (Op->Common.Value.Arg);
	}
*/
	/* Get the info structure for this opcode */

	op_info = acpi_ps_get_opcode_info(op->common.aml_opcode);
	if (op_info->class == AML_CLASS_UNKNOWN) {

		/* Invalid opcode or ASCII character */

		return (NULL);
	}

	/* Check if this opcode requires argument sub-objects */

	if (!(op_info->flags & AML_HAS_ARGS)) {

		/* Has no linked argument objects */

		return (NULL);
	}

	/* Get the requested argument object */

	arg = op->common.value.arg;
	while (arg && argn) {
		argn--;
		arg = arg->common.next;
	}

	return (arg);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_append_arg
 *
 * PARAMETERS:  Op              - Append an argument to this Op.
 *              Arg             - Argument Op to append
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Append an argument to an op's argument list (a NULL arg is OK)
 *
 ******************************************************************************/

void
acpi_ps_append_arg(union acpi_parse_object *op, union acpi_parse_object *arg)
{
	union acpi_parse_object *prev_arg;
	const struct acpi_opcode_info *op_info;

	ACPI_FUNCTION_ENTRY();

	if (!op) {
		return;
	}

	/* Get the info structure for this opcode */

	op_info = acpi_ps_get_opcode_info(op->common.aml_opcode);
	if (op_info->class == AML_CLASS_UNKNOWN) {

		/* Invalid opcode */

		ACPI_ERROR((AE_INFO, "Invalid AML Opcode: 0x%2.2X",
			    op->common.aml_opcode));
		return;
	}

	/* Check if this opcode requires argument sub-objects */

	if (!(op_info->flags & AML_HAS_ARGS)) {

		/* Has no linked argument objects */

		return;
	}

	/* Append the argument to the linked argument list */

	if (op->common.value.arg) {

		/* Append to existing argument list */

		prev_arg = op->common.value.arg;
		while (prev_arg->common.next) {
			prev_arg = prev_arg->common.next;
		}
		prev_arg->common.next = arg;
	} else {
		/* No argument list, this will be the first argument */

		op->common.value.arg = arg;
	}

	/* Set the parent in this arg and any args linked after it */

	while (arg) {
		arg->common.parent = op;
		arg = arg->common.next;

		op->common.arg_list_length++;
	}
}

#ifdef ACPI_FUTURE_USAGE
/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_depth_next
 *
 * PARAMETERS:  Origin          - Root of subtree to search
 *              Op              - Last (previous) Op that was found
 *
 * RETURN:      Next Op found in the search.
 *
 * DESCRIPTION: Get next op in tree (walking the tree in depth-first order)
 *              Return NULL when reaching "origin" or when walking up from root
 *
 ******************************************************************************/

union acpi_parse_object *acpi_ps_get_depth_next(union acpi_parse_object *origin,
						union acpi_parse_object *op)
{
	union acpi_parse_object *next = NULL;
	union acpi_parse_object *parent;
	union acpi_parse_object *arg;

	ACPI_FUNCTION_ENTRY();

	if (!op) {
		return (NULL);
	}

	/* Look for an argument or child */

	next = acpi_ps_get_arg(op, 0);
	if (next) {
		return (next);
	}

	/* Look for a sibling */

	next = op->common.next;
	if (next) {
		return (next);
	}

	/* Look for a sibling of parent */

	parent = op->common.parent;

	while (parent) {
		arg = acpi_ps_get_arg(parent, 0);
		while (arg && (arg != origin) && (arg != op)) {
			arg = arg->common.next;
		}

		if (arg == origin) {

			/* Reached parent of origin, end search */

			return (NULL);
		}

		if (parent->common.next) {

			/* Found sibling of parent */

			return (parent->common.next);
		}

		op = parent;
		parent = parent->common.parent;
	}

	return (next);
}

#ifdef ACPI_OBSOLETE_FUNCTIONS
/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_get_child
 *
 * PARAMETERS:  Op              - Get the child of this Op
 *
 * RETURN:      Child Op, Null if none is found.
 *
 * DESCRIPTION: Get op's children or NULL if none
 *
 ******************************************************************************/

union acpi_parse_object *acpi_ps_get_child(union acpi_parse_object *op)
{
	union acpi_parse_object *child = NULL;

	ACPI_FUNCTION_ENTRY();

	switch (op->common.aml_opcode) {
	case AML_SCOPE_OP:
	case AML_ELSE_OP:
	case AML_DEVICE_OP:
	case AML_THERMAL_ZONE_OP:
	case AML_INT_METHODCALL_OP:

		child = acpi_ps_get_arg(op, 0);
		break;

	case AML_BUFFER_OP:
	case AML_PACKAGE_OP:
	case AML_METHOD_OP:
	case AML_IF_OP:
	case AML_WHILE_OP:
	case AML_FIELD_OP:

		child = acpi_ps_get_arg(op, 1);
		break;

	case AML_POWER_RES_OP:
	case AML_INDEX_FIELD_OP:

		child = acpi_ps_get_arg(op, 2);
		break;

	case AML_PROCESSOR_OP:
	case AML_BANK_FIELD_OP:

		child = acpi_ps_get_arg(op, 3);
		break;

	default:
		/* All others have no children */
		break;
	}

	return (child);
}
#endif
#endif				/*  ACPI_FUTURE_USAGE  */
