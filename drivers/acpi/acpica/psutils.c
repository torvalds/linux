// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: psutils - Parser miscellaneous utilities (Parser only)
 *
 * Copyright (C) 2000 - 2020, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acparser.h"
#include "amlcode.h"
#include "acconvert.h"

#define _COMPONENT          ACPI_PARSER
ACPI_MODULE_NAME("psutils")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_create_scope_op
 *
 * PARAMETERS:  None
 *
 * RETURN:      A new Scope object, null on failure
 *
 * DESCRIPTION: Create a Scope and associated namepath op with the root name
 *
 ******************************************************************************/
union acpi_parse_object *acpi_ps_create_scope_op(u8 *aml)
{
	union acpi_parse_object *scope_op;

	scope_op = acpi_ps_alloc_op(AML_SCOPE_OP, aml);
	if (!scope_op) {
		return (NULL);
	}

	scope_op->named.name = ACPI_ROOT_NAME;
	return (scope_op);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_init_op
 *
 * PARAMETERS:  op              - A newly allocated Op object
 *              opcode          - Opcode to store in the Op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize a parse (Op) object
 *
 ******************************************************************************/

void acpi_ps_init_op(union acpi_parse_object *op, u16 opcode)
{
	ACPI_FUNCTION_ENTRY();

	op->common.descriptor_type = ACPI_DESC_TYPE_PARSER;
	op->common.aml_opcode = opcode;

	ACPI_DISASM_ONLY_MEMBERS(acpi_ut_safe_strncpy(op->common.aml_op_name,
						      (acpi_ps_get_opcode_info
						       (opcode))->name,
						      sizeof(op->common.
							     aml_op_name)));
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_alloc_op
 *
 * PARAMETERS:  opcode          - Opcode that will be stored in the new Op
 *              aml             - Address of the opcode
 *
 * RETURN:      Pointer to the new Op, null on failure
 *
 * DESCRIPTION: Allocate an acpi_op, choose op type (and thus size) based on
 *              opcode. A cache of opcodes is available for the pure
 *              GENERIC_OP, since this is by far the most commonly used.
 *
 ******************************************************************************/

union acpi_parse_object *acpi_ps_alloc_op(u16 opcode, u8 *aml)
{
	union acpi_parse_object *op;
	const struct acpi_opcode_info *op_info;
	u8 flags = ACPI_PARSEOP_GENERIC;

	ACPI_FUNCTION_ENTRY();

	op_info = acpi_ps_get_opcode_info(opcode);

	/* Determine type of parse_op required */

	if (op_info->flags & AML_DEFER) {
		flags = ACPI_PARSEOP_DEFERRED;
	} else if (op_info->flags & AML_NAMED) {
		flags = ACPI_PARSEOP_NAMED_OBJECT;
	} else if (opcode == AML_INT_BYTELIST_OP) {
		flags = ACPI_PARSEOP_BYTELIST;
	}

	/* Allocate the minimum required size object */

	if (flags == ACPI_PARSEOP_GENERIC) {

		/* The generic op (default) is by far the most common (16 to 1) */

		op = acpi_os_acquire_object(acpi_gbl_ps_node_cache);
	} else {
		/* Extended parseop */

		op = acpi_os_acquire_object(acpi_gbl_ps_node_ext_cache);
	}

	/* Initialize the Op */

	if (op) {
		acpi_ps_init_op(op, opcode);
		op->common.aml = aml;
		op->common.flags = flags;
		ASL_CV_CLEAR_OP_COMMENTS(op);

		if (opcode == AML_SCOPE_OP) {
			acpi_gbl_current_scope = op;
		}

		if (acpi_gbl_capture_comments) {
			ASL_CV_TRANSFER_COMMENTS(op);
		}
	}

	return (op);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_free_op
 *
 * PARAMETERS:  op              - Op to be freed
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Free an Op object. Either put it on the GENERIC_OP cache list
 *              or actually free it.
 *
 ******************************************************************************/

void acpi_ps_free_op(union acpi_parse_object *op)
{
	ACPI_FUNCTION_NAME(ps_free_op);

	ASL_CV_CLEAR_OP_COMMENTS(op);
	if (op->common.aml_opcode == AML_INT_RETURN_VALUE_OP) {
		ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS,
				  "Free retval op: %p\n", op));
	}

	if (op->common.flags & ACPI_PARSEOP_GENERIC) {
		(void)acpi_os_release_object(acpi_gbl_ps_node_cache, op);
	} else {
		(void)acpi_os_release_object(acpi_gbl_ps_node_ext_cache, op);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    Utility functions
 *
 * DESCRIPTION: Low level character and object functions
 *
 ******************************************************************************/

/*
 * Is "c" a namestring lead character?
 */
u8 acpi_ps_is_leading_char(u32 c)
{
	return ((u8) (c == '_' || (c >= 'A' && c <= 'Z')));
}

/*
 * Get op's name (4-byte name segment) or 0 if unnamed
 */
u32 acpi_ps_get_name(union acpi_parse_object * op)
{

	/* The "generic" object has no name associated with it */

	if (op->common.flags & ACPI_PARSEOP_GENERIC) {
		return (0);
	}

	/* Only the "Extended" parse objects have a name */

	return (op->named.name);
}

/*
 * Set op's name
 */
void acpi_ps_set_name(union acpi_parse_object *op, u32 name)
{

	/* The "generic" object has no name associated with it */

	if (op->common.flags & ACPI_PARSEOP_GENERIC) {
		return;
	}

	op->named.name = name;
}
